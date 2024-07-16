/*!
 * @brief Emulate "current directory" path for operating with file system
 * Implementation file
 * 	@file	cwd_emulate
 *	@author	(Solomatov A.A. (aso)
 *	Created	27.04.2024
 *	Version	0.3
 */




#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG	// 4 - set 'DEBUG' logging level

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


#include "extrstream"
#include "astring.h"

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
	    path = std::string(freewrapper<char>(realpath(path.c_str(), /*std::nullptr*/ NULL)));
	else
	    // relative path - finalize processing: refine the path: add leading slash & remove tailing slash
	    path = std::string(freewrapper<char>(realpath((get() + CWD::refine(path)).c_str(), /*std::nullptr*/ NULL)));

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
	if (!last::is_exist())
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


    /// Control values for parsing a path
    class sign {
    public:
	constexpr static unsigned int place = 0x2;	/*!< place for the sign */
	constexpr static unsigned int slash = 0x0;	/*!< slash char in the prev pass */
	constexpr static unsigned int point = 0x1;	/*!< the "point" sign */
	constexpr static unsigned int alpha = 0x2;	/*!< any aplhabetical sign */
	    /*! mark for initial pass */
	constexpr static unsigned int init = (0x3 << 4*place);
	    /*! mask if alphabetical chars is present */
	constexpr static unsigned int mask_aplpha = (alpha | (alpha << 1*place) | (alpha << 2*place));
	    /*! mask if three point present */
	constexpr static unsigned int mark_three = (point | (point << 1*place) | (point << 2*place));
    }; /* class sign */



    /// @details check, if the basename (the last part of the path) -
    /// has the characteristics of a directory name, and a dirname
    /// (the path prefix) - is an existing file, not a directory,
    /// or any other impossible variants of the full file/path name
    /// and return 'false' in this case
    bool CWD::valid(std::string path)
    {

	path = astr::trim(std::move(path));

	    size_t base_len = strlen(basename(path.c_str()));

	if (path.empty())
	    return true;

	// if full path length - one char only, it's a root or one char base, without dir
	if (path.length() == 1)
	    return true;

	// if path - only base, not a dir, or a dirname - one symbol length (it can only be the slash)
	if ((path.length() - base_len) < 2)
	{
	    if (path == delimiter() + parent())	//< if path == '/..' - it's invalid
		return false;
	    return true;
	}; /* if (base - path) < 2 */

	if (!(path.length() > base_len))
	    return true;


		unsigned int ctrl_cnt = sign::init;	// marked the firs pass of the control loop
		unsigned int idx_ctrl = 0;

	// scan the dirname of the path for found '/.' or '/..' sequence
	for (auto scan = path.crbegin() + base_len + 1; scan < path.crend(); scan++)
	{
	    ESP_LOGD("CWD::valid", "current char from the path is: '%c', ctrl_cnt is %2X", *scan, ctrl_cnt);

	    switch (*scan)
	    {
	    // solution point
	    case '/':
		idx_ctrl = 0;	// reset the idx_ctrl
		ESP_LOGD("CWD::valid", "###### Solution point: current path char ######");
		switch (ctrl_cnt)
		{
		// double slash - prev symbol is slash
		case sign::slash:
		    return false;

		// if more then 3 point sequence in substring
		case sign::mark_three:
		    break;

		case sign::init:
		    [[fallthrough]];
		default:
		    // if non point sign is present in tested substring
		    if (ctrl_cnt & sign::mask_aplpha)
		    {
			ctrl_cnt = 0;
			continue;
		    }; /* if ctrl_cnt & alpha_present_mask */
		    std::string tmp = compose(path.substr(0, std::distance(scan, path.crend())));
		    if ((last::is_exist() && !last::is_dir()) || is_root(tmp))
			return false;
		}; /* switch ctrl_cnt */
		ctrl_cnt = sign::slash;
		break;

		// point symbol handling
	    case '.':
		[[fallthrough]];
		// all other symbols
	    default:

		if (idx_ctrl < 3)
		    ctrl_cnt |= (((*scan == '.')? sign::point: sign::alpha) << idx_ctrl++ * sign::place);
	    }; /* switch *scan */
	}; /* for auto scan = path.crbegin() + base_len + 1; scan < path.crend(); scan++ */

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
