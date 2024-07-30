/*!
 * @brief Emulate "current directory" path for operating with file system
 * Implementation file
 * 	@file	cwd_emulate.cpp
 *	@author	(Solomatov A.A. (aso)
 *	@date Created 27.04.2024
 *	      Updated 29.07.2024
 *	Version	1.0
 */


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
#include <regex>
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
	path = astr::trim(std::move(path));

	if (path.empty())
	    return get();	//< path is empty - return current cwd
	// argument - absolute path
	if (fs::absolute_path(path))
	    path = std::string(freewrapper<char>(realpath(path.c_str(), NULL)));
	else
	    // relative path - finalize processing: refine the path: add leading slash & remove tailing slash
	    path = std::string(freewrapper<char>(realpath((get() + CWD::refine(path)).c_str(), NULL)));

	/// Check, the path is exist?
	if (stat(path.c_str(), &CWD::statbuf) == 0)
	    CWD::err = 0;
	else CWD::err = errno;

	return path;

    }; /* Exec::CWD::compose() */


    /// change cwd dir
    esp_err_t CWD::change(std::string path)
    {

	path = compose(std::move(path));

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
	path = astr::trim(std::move(path));

	    size_t base_len = strlen(basename(path.c_str()));
	    mark sign;
        for (const char &scan: aso::adaptors::constant::reverse(path))
	{
	    switch (scan)
	    {
	    // decision point
	    case '/':
	    //case delim_ch:

		compose(path.substr(0, &scan - path.data()));	// Check the processed part path is exist or a not
		switch (sign.ctrl)
		{
		// initial state - nothing to do
		case mark::init:
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
		    return false;

		case mark::point:
		    // if more then 3 point sequence in substring
		    if (sign.cnt > sign.pt_max)
			return false;
		    [[fallthrough]];

		case mark::mixed:
		    [[fallthrough]];

		default:
		    // If base part of filename processing
		    switch (sign.phase)
		    {
		    case mark::tag::base:	// only for alphabetical or mixed basename, for 'point' char - interceped
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
		}; /* switch (sign_ctrl) */

	    }; /* switch *scan */

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
