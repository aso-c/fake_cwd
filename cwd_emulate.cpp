/*!
 * @brief Emulate "current directory" path for operating with file system
 * Implementation file
 * 	@file	cwd_emulate.cpp
 *	@author	(Solomatov A.A. (aso)
 *	@date Created 27.04.2024
 *	      Updated 07.08.2024
 *	@version 1.1.1
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


    // compose the full path from the current directory with addition specified part of the passed path
    // return full path appling current dir, use desired part of the passed path
    // return current dir (if path == NULL or "") or generate fullpath for sended path
    // trailing slash in returned string is absent always
    std::string CWD::compose(std::string path) const
    {
	// drop trailing & leading spaces
	astr::trim(path);
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

//	esp_log_level_set("EXEC::CWD::change", ESP_LOG_DEBUG);	/* for debug purposes */

	ESP_LOGD("EXEC::CWD::change", "Original value of the \"path\" parameter is: \"%s\"", path.c_str());
	path = compose(std::move(path));
	ESP_LOGD("EXEC::CWD::change", "Composed value of the \"path\" parameter is: \"%s\"", path.c_str());

	if (path.empty())
	{
	    ESP_LOGE("CWD_emulating::change_dir", "Change dir is failed");
	    return err = (ESP_FAIL);
	}; /* if path.empty() */
	// if dir changed to root - exclusively change dir
	if (is_root(path))
	    goto final;
	if (!last::exist())
	{
	    ESP_LOGE("CWD_emulating::change_dir", "Change dir is failed - requested path to change \"%s\" is not exist;\n"
		    "\t\t\t\tcurrent directory was not changing", path.c_str());
	    return (err = ESP_ERR_NOT_FOUND);
	}; /* if !last::exist() */
	ESP_LOGD("EXEC::CWD::change", "to %s which is a %s\n", path.c_str(),
		statmode2txt(CWD::statbuf));
	if (!last::is_dir())
	{
	    ESP_LOGE("EXEC::CWD::change", "Change dir is failed - requested path to change \"%s\" is not directory;\n"
		    "\t\t\t\tleave current directory without changing", path.c_str());
	    return (err = ESP_ERR_NOT_SUPPORTED);
	}; /* if !last::is_dir() */

final:
	// set current pwd value at the final
	set(path);

	err = ESP_OK;
	return ESP_OK;

    }; /* CWD::change() */


    /// Control parsing a path
    class mark
    {
    public:

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
//	esp_log_level_set("CWD::valid()", ESP_LOG_DEBUG);	/* for debug purposes */

	ESP_LOGD("CWD::valid()", "==== Call the Exec::CWD::valid(std::string) procedure, std::string own value version ===");

	astr::trim(path);

	    size_t base_len = strlen(basename(path.c_str()));

	ESP_LOGD("CWD::valid()", "basename of the path is: \"%s\"", path.c_str() + path.length() - base_len);
	ESP_LOGD("CWD::valid()", "full path is: \"%s\"", path.c_str());
	ESP_LOGD("CWD::valid()", "dirname path is: \"%.*s\"", path.length() - base_len, path.c_str());

	    mark sign;
        for (const char &scan: aso::adaptors::constant::reverse(path))
	{
	    ESP_LOGD("CWD::valid()", "current char from the path is: '%c', sign::ctrl is %2X, sign::cnt = %u", scan,
					(unsigned)sign.ctrl, sign.cnt);
	    ESP_LOGD("CWD::valid()", "==== current scanning part of path is: %s", path.substr(0, &scan - path.data() + 1).c_str());

		std::string curr;
	    switch (scan)
	    {
	    // decision point
	    case '/':
	    //case delim_ch:

		curr = std::string(path.cbegin(), std::string::const_iterator(&scan));
		ESP_LOGD("CWD::valid()", "###### Decision point: current subpath is \"%s\", current char is '%c' ######", curr.c_str(), scan);
		curr = compose(std::move(curr));	// Check the processed part path is exist or a not

		ESP_LOGD("CWD::valid()", "###### Decision point: composed path is \"%s\"                       ######", curr.c_str());
		switch (sign.ctrl)
		{
		// initial state - nothing to do
		case mark::init:
		    ESP_LOGD("CWD::valid()", "++++++ The first pass of the control loop ++++++");
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
		    ESP_LOGD("CWD::valid()", "====== The %u point sequence at the current substring \"%s\", ctrl_cnt is %2X, test current subpath for existing ======",
				sign.cnt, curr.c_str(), sign.ctrl);
		    [[fallthrough]];

		case mark::mixed:
		    ESP_LOGD("CWD::valid()", "Or mix point & alpha symbol is present in current processing substring - test subpath for exist");
		    [[fallthrough]];
		default:
		    // If base part of filename processing
		    switch (sign.phase)
		    {
		    case mark::tag::base:	// only for alphabetical or mixed basename, for 'point' char - interceped
			ESP_LOGD("CWD::valid()", "------ The mark::tag::base phase point");
		        sign.phase = (CWD::last::exist() || is_root(curr))? mark::tag::mandatory: mark::tag::optional;
		        break;

		    case mark::tag::optional:
			ESP_LOGD("CWD::valid()", "~~~~~~ The mark::tag::optional phase: subpath must be unexist or must be is directory");
			// subpath must be unexist or must be is directory
			if (is_root(curr))	// intercept the 'root' case - root is exist always!!! Asign phase as "mandatiry"
			    sign.phase = mark::tag::mandatory;
			else if (CWD::last::exist())
		        {
		            if (CWD::last::is_dir())
		        	sign.phase = mark::tag::mandatory;
		            else
		        	return false;
		        }; /* if CWD::last::exist() */
		        break;

		    case mark::tag::mandatory:
			ESP_LOGD("CWD::valid()", "****** The mark::tag::mandatory phase: subpath must be exist && must be is directory or must be is root");
			// subpath must be exist && must be is directory or must be is root
			if (is_root(curr))	// intercept the 'root' case - delete check the existing
			    break;
		        if (!CWD::last::exist() || !CWD::last::is_dir())
		            return false;

		    }; /* switch sign.phase */

		}; /* switch sign.ctrl */

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

		case mark::init:	// preset in initial state
		    if (path == "/..")	// impossible path - invalid name
			return false;
		    compose(path);
		    if (CWD::last::exist())
			sign.phase = mark::tag::mandatory;
		    else sign.phase = mark::tag::optional;
		    [[fallthrough]];

		case mark::slash:
		    [[fallthrough]];

		default:
		    sign.cnt = 1;
		    sign.ctrl = mark::point;
		}; /* switch (sign.ctrl) */
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
		    compose(path);
		    if (CWD::last::exist())
			sign.phase = mark::tag::mandatory;
		    else sign.phase = mark::tag::optional;
		    [[fallthrough]];
		case mark::slash:
		[[fallthrough]];
		default:
		    sign.cnt = 1;
		    sign.ctrl = mark::alpha;
		}; /* switch (sign.ctrl) */

		ESP_LOGD("CWD::valid()", "%d symbol of the processing substring, symbol is \"%c\"", sign.cnt, scan);
	    }; /* switch scan */

	}; /* for const char &scan: aso::adaptors::constant::reverse(path) */

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
