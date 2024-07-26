/*!
 * @brief Emulate "current directory" path for operating with file system
 * Implementation file
 * 	@file	cwd_emulate
 *	@author	(Solomatov A.A. (aso)
 *	@date Created 27.04.2024
 *	      Updated 19.07.2024
 *	Version	0.9
 */




//#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG	// 4 - set 'DEBUG' logging level

#include <limits>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <cstdarg>

#include <cstring>
#include <cctype>
#include <sys/unistd.h>
#include <cerrno>
#include <esp_log.h>
#include <sys/stat.h>
#include <sys/types.h>
//#include <unistd.h>
#include <regex>
//#include <fcntl.h>
#include <dirent.h>

#include <esp_vfs_fat.h>

#include "cwd_emulate"


#include <extrstream>
#include <astring.h>
#include <reversing.hpp>

using namespace std;



namespace Exec	//-----------------------------------------------------------------------------------------------------
{

    [[maybe_unused]]
    static const char *TAG = "CWD emulating";


    //--[ class Exec::CWD ]--------------------------------------------------------------------------------------------


//    // get current dir (if path == NULL or "") or generate fullpath for sended path
//    // absent trailing slash in returned string is guaranteed
//
//    // return current pwd (current dir) only
//    const std::string& CWD::get() const
//    {
//	return pwd;
//    }; /* char* CWD::get() */


    // compose the full path from the current directory with addition specified part of the passed path
    // return full path appling current dir, use desired part of the passed path
    // return current dir (if path == NULL or "") or generate fullpath for sended path
    // trailing slash in returned string is absent always
    std::string CWD::compose(std::string path) const
    {
	// drop trailing & leading spaces
	path = astr::trim(std::move(path));
	ESP_LOGD(__PRETTY_FUNCTION__, "\"path\" argument is %s", path.c_str());

	if (path.empty())
	    return get();	//< path is empty - return current cwd
	// argument - absolute path
	if (fs::absolute_path(path))
	    path = std::string(freewrapper<char>(realpath(path.c_str(), /*std::nullptr*/ NULL)));
	else
	{
	    // relative path - finalize processing
	    ESP_LOGD(__PRETTY_FUNCTION__, "processing relative path: updating path on top of the current pwd");
	    // refine the path: add leading slash & remove tailing slash
	    path = std::string(freewrapper<char>(realpath((get() + CWD::refine(path)).c_str(), /*std::nullptr*/ NULL)));
	}; /* else if fs::absolute_path(path) */

	/// Check, the path is exist?
	if (stat(path.c_str(), &CWD::statbuf) == 0)
	    CWD::err = 0;
	else CWD::err = errno;

	return path;

    }; /* Exec::CWD::compose() */


    /// change cwd dir
    esp_err_t CWD::change(std::string path)
    {
	    //struct stat statbuf;

//	esp_log_level_set("EXEC::CWD::change", ESP_LOG_DEBUG);	/* for debug purposes */

	ESP_LOGD("EXEC::CWD::change", "Original value of the \"path\" parameter is: \"%s\"", path.c_str());
	path = compose(std::move(path));
	ESP_LOGD("EXEC::CWD::change", "Composed value of the \"path\" parameter is: \"%s\"", path.c_str());

//	if (astr::is_space(path))
	if (path.empty())
	{
	    ESP_LOGE("CWD_emulating::change_dir", "Change dir is failed");
	    return ESP_FAIL;
	}; /* if astr::is_space(path) */
	// if dir changed to root - exclusively change dir
	if (is_root(path))
	    goto final;
	if (!last::exist())
	{
	    ESP_LOGE("CWD_emulating::change_dir", "Change dir is failed - requested path to change \"%s\" is not exist;\n"
		    "\t\t\t\tcurrent directory was not changing", path.c_str());
	    return ESP_ERR_NOT_FOUND;
	}; /* if stat(path.c_str(), &statbuf) == -1 */
	ESP_LOGD("EXEC::CWD::change", "to %s which is a %s\n", path.c_str(),
		statmode2txt(CWD::statbuf));
	if (!last::is_dir())
	{
	    ESP_LOGE("EXEC::CWD::change", "Change dir is failed - requested path to change \"%s\" is not directory;\n"
		    "\t\t\t\tleave current directory without changing", path.c_str());
	    return ESP_ERR_NOT_SUPPORTED;
	}; /* if !S_ISDIR(statbuf.st_mode) */

final:
	// set current pwd value at the final
	set(path);

	return ESP_OK;

    }; /* CWD::change() */


    /// Control parsing a path
    class mark
    {
    public:

//	mark(): ctrl(init), cnt(0) {};

	/// marker id of the parced char enum
	enum id {
	    init  = 0x0,	/*!< the initial state */
	    alpha = 0x1,	/*!< any aplhabetical sign */
	    point = 0x2,	/*!< the "point" sign */
	    mixed = 0x3,	/*!< mixed sign's - point & alphabetical */
	    slash = 0x4,	/*!< slash char in the prev pass */
	}; /* enum id */

	/// parsing path phase designation
	enum class tag {
	    base,	///< base part of filename is parsed
	    optional,	///< optional part of filename is parsed - may be exist or not
	    mandatory	///< mandatory part of filename is parsed - *must* be exist
	}; /* enum class tag */

	id ctrl = init;	///< mark the precedence parced char or sequence
	tag phase = tag::base;
	unsigned int cnt = 0;///< char sequence counter

	static constexpr unsigned int pt_max = 2;	///< valid point sequence maximum length
	static constexpr unsigned int slash_max = 1;	///< valid slash sequence maximum length

    }; /* class mark */


    /// @details check, if the basename (the last part of the path) -
    /// has the characteristics of a directory name, and a dirname
    /// (the path prefix) - is an existing file, not a directory,
    /// or any other impossible variants of the full file/path name
    /// and return 'false' in this case
    bool CWD::valid(std::string path)
    {
	esp_log_level_set("CWD::valid", ESP_LOG_DEBUG);	/* for debug purposes */
	esp_log_level_set("CWD::valid()", ESP_LOG_DEBUG);	/* for debug purposes */

	ESP_LOGD("CWD::valid", "==== Call the Exec::CWD::valid(std::string) procedure, std::string own value version ===");

	path = astr::trim(std::move(path));

	    size_t base_len = strlen(basename(path.c_str()));

	ESP_LOGD("CWD::valid()", "basename of the path is: \"%s\"", path.c_str() + path.length() - base_len);
	ESP_LOGD("CWD::valid()", "full path is: \"%s\"", path.c_str());
	ESP_LOGD("CWD::valid()", "dirname path is: \"%.*s\"", path.length() - base_len, path.c_str());

//	if (path.empty())
//	    return true;	// ESP_LOGD("CWD::valid", "path is empty, always valid");

	// if full path length - one char only, it's a root or one char base, without dir
//	if (path.length() == 1)
//	    return true;	// ESP_LOGD("Device::valid_path", "len of the path - is 1, always valid");

	// if path - only base, not a dir, or a dirname - one symbol length (it can only be the slash)
	if ((path.length() - base_len) < 2)
	{
	    if (path == delimiter() + parent())	//< if path == '/..' - it's invalid
		return false;
	    return true;	// ESP_LOGD("Device::valid_path", "Len of dirname is 1 or 0, then path is valid");
	}; /* if (base - path) < 2 */

//	if (!(path.length() > base_len))
//	    return true;

	//------------------------------------
	    mark sign;
        for (const char &scan: aso::adaptors::constant::reverse(path/*, base_len + 1*/))
	{
	    ESP_LOGD("CWD::valid()", "current char from the path is: '%c', sign::ctrl is %2X, sign::cnt = %u", /***/scan,
					(unsigned)sign.ctrl, sign.cnt);
	    switch (scan)
	    {
	    // solution point
	    case '/':
	    //case delim_ch:

		compose(path.substr(0, &scan - path.data()));	// Check the processed part path is exist or a not

		ESP_LOGD("CWD::valid()", "###### Solution point: current path char is '/' ######");
		switch (sign.ctrl)
		{
		// initial state - nothing to do
		case mark::init:
		    ESP_LOGD("CWD::valid()", "++++++ The first pass of the control loop ++++++");
//		    if (is_root(compose(path.substr(0, &scan - path.data()))))
		    // Check pre-condition path validity
		    if (path.empty())
			return true;
		    if (is_root(path))
			return true;
		    if (CWD::last::exist())
			if (CWD::last::is_dir())
			    sign.phase = mark::tag::mandatory;
			else return false;
		    else sign.phase = mark::tag::optional;
		    break;

		// double slash - prev symbol is slash
		case mark::slash:
		    ESP_LOGD("CWD::valid()", "**** double slash and more - is not valid sequence in the path name ****");
		    return false;

		case mark::point:
		    // if more then 3 point sequence in substring
		    if (sign.cnt > sign.pt_max)
		    {
			ESP_LOGD("CWD::valid()", "3 point or more sequence is present in current substring - invalid sequence, return");
			return false;
		    };
		    ESP_LOGD("CWD::valid()", "====== The %u point sequence in the current meaning substring, ctrl_cnt is %2X, test current subpath for existing ======", sign.cnt, sign.ctrl);
//		    break;
		    [[fallthrough]];

		case mark::mixed:
		    ESP_LOGD("CWD::valid()", "Or mix point & alpha symbol is present in current processing substring - test subpath for exist");
		    [[fallthrough]];
		default:
		// If base part of filename processing
//		    if (sign.phase == mark::tag::base)
		    switch (sign.phase)
		    {
		    case mark::tag::base:
//		        if (CWD::last::exist())
//		            sign.phase = mark::tag::mandatory;
//		        else sign.phase = mark::tag::optional;
		        sign.phase = CWD::last::exist()? mark::tag::mandatory: mark::tag::optional;
		        break;

		    case mark::tag::optional:
			// subpath must be unexist or must be is directory
		        if (CWD::last::exist())
		        {
		            if (CWD::last::is_dir())
		        	sign.phase = mark::tag::mandatory;
		            else
		        	return false;
		        }
		        break;

		    case mark::tag::mandatory:
			// subpath must be exist && must be is directory
		        if (!CWD::last::exist() || !CWD::last::is_dir())
		            return false;

		    }; /* switch sign.phase */
//		    }; /* if sign.phase == mark::tag::base */
		    ;
		}; /* switch ctrl_cnt */

		//ESP_LOGD("CWD::valid()", "### Testing the current substring \"%s\" for existing ###", compose(path.substr(0, &scan - path.data())).c_str());
		sign.ctrl = mark::slash;
		break;


		// point symbol handling
	    case '.':
		switch (sign.ctrl)
		{
		case mark::alpha:
		    sign.cnt = 2;
		    sign.ctrl = mark::mixed;
		    break;

		case mark::point:
		    sign.cnt++;
		    break;

		case mark::mixed:
		    break;

		case mark::init:
		[[fallthrough]];
		case mark::slash:
		[[fallthrough]];
		default:
		    sign.cnt = 1;
		    sign.ctrl = mark::point;
		}; /* switch (sign_ctrl) */
		break;

		// all other symbols - it's "alpha"
	    default:

		switch (sign.ctrl)
		{
		case mark::alpha:	// prev chars - is only alpha
		    sign.cnt++;
		    break;

		case mark::point:	// prev chars - is only point
		    sign.cnt = 2;
		    sign.ctrl = mark::mixed;
		    break;

		case mark::mixed:	// prev chars - mix of alpha & points
		    break;

		case mark::init:
		[[fallthrough]];
		case mark::slash:
		[[fallthrough]];
		default:
		    sign.cnt = 1;
		    sign.ctrl = mark::alpha;
		}; /* switch (sign_ctrl) */

		ESP_LOGD("CWD::valid()", "%d symbol of the processing substring, symbol is \"%c\"", sign.cnt, scan);
	    }; /* switch *scan */

	    if (&scan == path.data())
	    {
		ESP_LOGD("CWD::valid()", "###### End of sequence processing, first char is reached: additional solution point: current path char is %c ######", scan);

	    }; /* if &scan == &(*path.crend()) */

#if 0	// Not needed this
	    /// Final decision points of the pass
	    if (scan == '/' || &scan == path.data())
	    {

	    }; /* if scan == '/' || &scan == path.data() */
#endif	// 0 - Not needed this



	}; /* for const char &scan: aso::adaptors::constant::reverse(path, base_len + 1) */

	return true;
    }; /* CWD::valid() */


    /// Force assign value of current working directory
    /// Use carefully! Possible incorrect behavior!!!
    void CWD::set(std::string str)
    {
	astr::trim(str);
	if (str.empty())
	    pwd = str;
	else
	    pwd = CWD::refine(str);

    }; /* CWD::set() */


    /// Tune-up any string for preparing as full path:
    /// - trim both spaces;
    /// - add leading symbol '/' (delimiter), if exist;
    /// - remove tailing slash (delimiter), if exist;
    std::string CWD::refine(std::string str)
    {
	str = astr::trim(str);
	// if  string was ended from '/' - drop it
	while (*str.end() == '/')
	    str.erase(str.end());
	if (str.empty())
	    str = '/';
	// if  string was not started from '/' - add it
	if (str[0] != '/')
	    str.insert(str.begin(), '/');
	return str;
    }; /* CWD::refine() */


    int CWD::err;		//< error state of the last operation compose() etc.
    struct stat CWD::statbuf;	//< file/directory existing status of the last operation compose() etc.



}; // namespace Exec	//---------------------------------------------------------------------------------------------


//--[ cwd_emulate.cpp ]------------------------------------------------------------------------------------------------
