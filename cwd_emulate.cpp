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

//	esp_log_level_set("CWD::change", ESP_LOG_DEBUG);	/* for debug purposes */

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
	if (!last::is_exist())
	{
	    ESP_LOGE("CWD_emulating::change_dir", "Change dir is failed - requested path to change \"%s\" is not exist;\n"
		    "\t\t\t\tcurrent directory was not changing", path.c_str());
	    return ESP_ERR_NOT_FOUND;
	}; /* if stat(path.c_str(), &statbuf) == -1 */
	ESP_LOGD("CWD_emulating::change_dir", "to %s which is a %s\n", path.c_str(),
		statmode2txt(CWD::statbuf));
	if (!last::is_dir())
	{
	    ESP_LOGE("CWD_emulating::change_dir", "Change dir is failed - requested path to change \"%s\" is not directory;\n"
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
	constexpr static unsigned int idle = 0x0;	/*!< idle pass - other sign then processing */
	constexpr static unsigned int point = 0x1;	/*!< the "point" sign */
	constexpr static unsigned int alpha = 0x2;	/*!< any aplhabetical sign */
	    /*! mark for initial pass */
	constexpr static unsigned int init = (0x3 << 4*place);
	    /*! mask if alphabetical chars is present */
	constexpr static unsigned int mask_aplpha = (alpha | (alpha << 1*place) | (alpha << 2*place));
	    /*! mask if three point present */
	constexpr static unsigned int mark_three = (point | (point << 1*place) | (point << 2*place));
    }; /* class sign */



/// @details if the basename (the last part of the path) - has the characteristics
/// of a directory name, and a dirname (the path prefix) -
/// is an existing file, not a directory, or any other impossible variants
/// of the full file/path name
bool CWD::valid(std::string path)
{
    esp_log_level_set("CWD::valid", ESP_LOG_DEBUG);	/* for debug purposes */

    ESP_LOGD("CWD::valid", "==== Call the Exec::CWD::valid(std::string) procedure, std::string own value version ===");

    path = astr::trim(std::move(path));

//	std::string base = basename(path.c_str());	// get a filename of a path
	size_t base_len = strlen(basename(path.c_str()));

//    ESP_LOGD(__PRETTY_FUNCTION__, "basename of the path is: \"%s\"", base.c_str());
    ESP_LOGD("CWD::valid", "basename of the path is: \"%s\"", path.c_str() + path.length() - base_len);
    ESP_LOGD("CWD::valid", "full path is: \"%s\"", path.c_str());
//    ESP_LOGD(__PRETTY_FUNCTION__, "dirname path is: \"%.*s\"", path.length() - base.length(), path.c_str());
    ESP_LOGD("CWD::valid", "dirname path is: \"%.*s\"", path.length() - base_len, path.c_str());



    if (path.empty())
	return true;	// ESP_LOGD("Device::valid_path", "path is empty, always valid");

    // if path - only base, not a dir
    if (path.length() == 1)
	return true;	// ESP_LOGD("Device::valid_path", "len of the path - is 1, always valid");

    // if dirname - empty or one symbol length (it can only be the slash)
//    if ((path.length() - base.length()) < 2)
    if ((path.length() - base_len) < 2)
    {
	//if (path == (std::string(delimiter) + ".."))	//< if path == '/..' - it's invalid
	if (path == delimiter() + parent() /*"/.."*/)	//< if path == '/..' - it's invalid
	    return false;
	return true;	// ESP_LOGD("Device::valid_path", "Len of dirname is 1 or 0, then path is valid");
    }; /* if (base - path) < 2 */

//	auto base_scan = path.crbegin() + base.length();	// for reversed scan
    // if dirname is not empty
//    if (base_scan < path.crend())
    if (!(path.length() > base_len))
	return true;

    //base_scan++;	// set base to a last slash in the path


#define sign_place 0x2	// with of the place for the sign
#define point_sign 0x1	// mark a point symbol in a string
#define alpha_sign 0x2	// mark a non-point or a non-slash symbol in a string
#define init_pass (0x3 << 4*sign_place)	// mark for the initial pass of the control of the path validity
#define alpha_present_mask (alpha_sign | (alpha_sign << 1*sign_place) | (alpha_sign << 2*sign_place))
#define three_point_mark (point_sign | (point_sign << 1*sign_place) | (point_sign << 2*sign_place))

//    enum class sign: unsigned {
//			place = 0x2,	/* place for the sign */
//			idle = 0x0,	/* idle pass - other sign then processing */
//			point = 0x1,	/* the "point" sign */
//			alpha = 0x2,	/* any aplhabetical sign */
//			init = (0x3 << 4*sign_place),	/* mark for initial pass */
//			mask_aplpha = (alpha | (alpha << 1*place) | (alpha << 2*place)),    /* mask if alphabetical present */
//			mark_three = (point | (point << 1*place) | (point << 2*place))    /* mask if three point present */
//    }; /* enum class sign */

	unsigned int ctrl_cnt = /*init_pass*/sign::init;	// marked the firs pass of the control loop
	unsigned int idx_ctrl = 0;
    // scan the dirname of the path for found '/.' or '/..' sequence

    //    for (auto scan : path)
    //for (auto scan = base_scan; scan < path.crend(); scan++)
    for (auto scan = path.crbegin() + base_len + 1; scan < path.crend(); scan++)
    {
	ESP_LOGD("CWD::valid", "current char from the path is: '%c', ctrl_cnt is %2X", *scan, ctrl_cnt);

	switch (*scan)
	{
	// solution point
	case '/':
	//case delim_ch:

	    idx_ctrl = 0;	// reset the idx_ctrl
	    ESP_LOGD("CWD::valid", "###### Solution point: current path char ######");
	    switch (ctrl_cnt)
	    {
	    // double slash - prev symbol is slash
//	    case 0:
	    case sign::idle:
		ESP_LOGD("CWD::valid", "**** double slash and more - is not valid sequence in the path name ****");
		return false;

//	    case three_point_mark:
	    case sign::mark_three:
		// if more then 3 point sequence in substring
		ESP_LOGD("CWD::valid", "3 point or more sequence is present in current substring - nothing to do, continue");
		break;

//	    case init_pass:
	    case sign::init:
		ESP_LOGD(__PRETTY_FUNCTION__, "++++++ The first pass of the control loop ++++++");
		[[fallthrough]];
	    default:
		// if non point sign is present in tested substring
		if (ctrl_cnt & alpha_present_mask)
		{
		    ESP_LOGD("CWD::valid", "alpha or other then point or slash symbol is present in current processing substring - test subpath for exist, continue");
		    ctrl_cnt = 0;
		    continue;
		}; /* if ctrl_cnt & alpha_present_mask */
		ESP_LOGD(__PRETTY_FUNCTION__, "====== One or two point sequence in the current meaning substring, ctrl_cnt is %2X, test current subpath for existing ======", ctrl_cnt);
		ESP_LOGD(__PRETTY_FUNCTION__, "### Testing the current substring \"%s\" for existing ###", compose(path.substr(0, std::distance(scan, path.crend()))).c_str());
		std::string tmp = compose(path.substr(0, std::distance(scan, path.crend())));
		if ((last::is_exist() && !last::is_dir()) || is_root(tmp))
		    return false;
	    }; /* switch ctrl_cnt */
	    ctrl_cnt = 0;
	    break;

	// point symbol handling
	case '.':
	    [[fallthrough]];
	// all other symbols
	default:

	    if (idx_ctrl < 3)
		ctrl_cnt |= (((*scan == '.')? point_sign: alpha_sign) << idx_ctrl++ * sign_place);
	    ESP_LOGD("CWD::valid", "%d symbol of the processing substring, symbol is \"%c\"", idx_ctrl, *scan);
	}; /* switch path[scan] */
    }; /* for auto scan = base_scan; scan < path.crend(); scan++ */

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
