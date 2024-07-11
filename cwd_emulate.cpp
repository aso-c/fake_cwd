/*!
 * @brief Emulate "current directory" path for operating with file system
 * Implementation file
 * 	@file	cwd_emulate
 *	@author	(Solomatov A.A. (aso)
 *	Created	27.04.2024
 *	Version	0.3
 */


#define __PURE_C__


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
#ifdef __PURE_C__
//#include <fcntl.h>
#include <dirent.h>
#else
#if __cplusplus < 201703L
#include <fcntl.h>
#include <dirent.h>
#else
#endif // __cplusplus < 201703L
#endif // ifdef __PURE_C__

#include <esp_vfs_fat.h>

#include "cwd_emulate"


#include "extrstream"
#include "astring.h"

using namespace std;



namespace Exec	//-----------------------------------------------------------------------------------------------------
{

    [[maybe_unused]]
    static const char *TAG = "CWD emulating";


    //--[ class CWD_emulating ]-----------------------------------------------------------------------------------------


    // get current dir (if path == NULL or "") or generate fullpath for sended path
    // absent trailing slash in returned string is guaranteed

    // return current pwd (current dir) only
    std::string CWD::get() const
    {
	ESP_LOGD(__PRETTY_FUNCTION__, "\"path\" argument is absent");
	ESP_LOGD(__PRETTY_FUNCTION__, "\"len\" argument is absent too");
	return pwd;
    }; /* char* CWD::get() */


    // compose the full path from the current directory with addition specified part of the passed path
    // return full path appling current dir, use desired part of the passed path
    // return current dir (if path == NULL or "") or generate fullpath for sended path
    // trailing slash in returned string is absent always
    std::string CWD::compose(std::string path)
    {
	// drop trailing & leading spaces
	path = astr::trim(std::move(path));
	ESP_LOGD(__PRETTY_FUNCTION__, "\"path\" argument is %s", path.c_str());

	// argument - absolute path
	if (fs::absolute_path(path))
	{
//	    realpath(path.c_str(), operative_path_buff);
	//    ESP_LOGD("CWD_emulating:", "%s: composed path is: \"%s\"", __func__, freewrapper<char>(realpath(path.c_str(), /*std::nullptr*/ NULL)));
//	    return operative_path_buff;
	    return std::string(freewrapper<char>(realpath(path.c_str(), /*std::nullptr*/ NULL)));
	}; /* if path[0] != '/' */


	// pwd == "" --> catch it
	if (empty(pwd))
	    return path;

	// argument - empty string
	//if (astr::is_space(path))
	if (path.empty())
	    return get();

	// relative path - finalize processing
	ESP_LOGD(__PRETTY_FUNCTION__, "processing relative path: updating path on top of the current pwd");
	return realpath((pwd + (((*pwd.end()) != '/')? "/": "") + path).c_str(), /*std::nullptr*/ NULL);

}; /* CWD::compose() */



/// change cwd dir
esp_err_t CWD::change(/*const*/ std::string/*&*/ path)
{
	const std::string tmpstr = compose(path);
	struct stat statbuf;

//    esp_log_level_set("CWD_emulating::change", ESP_LOG_DEBUG);	/* for debug purposes */

    path = compose(std::move(path));
    ESP_LOGD("CWD_emulating::change_dir", "The \"path\" parameter is: \"%s\"", path.c_str());
    ESP_LOGD("CWD_emulating::change_dir", "The \"tmpstr\" variable is: \"%s\"", tmpstr.c_str());

    if (astr::is_space(path))
    {
	ESP_LOGE("CWD_emulating::change_dir", "Change dir is failed");
	return ESP_FAIL;
    }; /* if astr::is_space(tmpstr) */
    // if dir changed to root - exclusively change dir
    if (is_root(path))
	goto final;
    if (stat(path.c_str(), &statbuf) == -1)
    {
	ESP_LOGE("CWD_emulating::change_dir", "Change dir is failed - requested path to change \"%s\" is not exist;\n"
		"\t\t\t\tcurrent directory was not changing", path.c_str());
	return ESP_ERR_NOT_FOUND;
    }; /* if stat(tmpstr.c_str(), &statbuf) == -1 */
    ESP_LOGD("CWD_emulating::change_dir", "to %s which is a %s\n", path.c_str(),
	    statmode2txt(statbuf));
    if (!S_ISDIR(statbuf.st_mode))
    {
	ESP_LOGE("CWD_emulating::change_dir", "Change dir is failed - requested path to change \"%s\" is not directory;\n"
		"\t\t\t\tleave current directory without changing", path.c_str());
	return ESP_ERR_NOT_SUPPORTED;
    }; /* if !S_ISDIR(statbuf.st_mode) */

final:
    // copy tmpstr to pwd at the final
    pwd = path;
    return ESP_OK;

}; /* CWD::change() */


/// @details if the basename (the last part of the path) - has the characteristics
/// of a directory name, and a dirname (the path prefix) -
/// is an existing file, not a directory, or any other impossible variants
/// of the full file/path name
//bool CWD_emulating::valid(const char path[])
 // FIXME Dirty code - need upgrading to correct usung std::string parameters
bool CWD::valid(std::string/*&&*/ path)
{
    //selective_log_level_set("Device::valid_path", ESP_LOG_DEBUG);	// for debug purposes
    ESP_LOGD(__PRETTY_FUNCTION__, "==== Call the fs::CWD_emulating::valid(std::string&&) procedure, std::string rvalue ref version ===");

    path = astr::trim(std::move(path));

	struct stat st;
	std::string base = basename(path.c_str());	// get a filename of a path

/*esp_log_level_set("Device::valid_path", ESP_LOG_DEBUG);*//* for debug purposes */
    ESP_LOGD(__PRETTY_FUNCTION__, "basename of the path is: \"%s\"", base.c_str());
    ESP_LOGD(__PRETTY_FUNCTION__, "full path is: \"%s\"", path.c_str());
    ESP_LOGD(__PRETTY_FUNCTION__, "dirname path is: \"%.*s\"", path.length() - base.length(), path.c_str());

    if (path.empty())
	return true;	// ESP_LOGD("Device::valid_path", "path is empty, always valid");

    // if path - only base, not a dir
    if (path.length() == 1)
	return true;	// ESP_LOGD("Device::valid_path", "len of the path - is 1, always valid");

    // if dirname - empty or one symbol length (it can only be the slash)
    if ((path.length() - base.length()) < 2)
    {
	if (path == "/..")	// if path == '/..' - it's invalid
	    return false;
	return true;	// ESP_LOGD("Device::valid_path", "Len of dirname is 1 or 0, then path is valid");
    }; /* if (base - path) < 2 */

#if 0	// old variant of base-scan
    // if base is not empty
    if (!empty(base))
	base--;	// set base to a last slash in the path
    else
    {
	if (stat(compose(path).c_str(), &st) == 0)
	    if (!S_ISDIR(st.st_mode))	// ESP_LOGD("Device::valid_path", "###!!! the path basename - is empty, test the path \"%s\" (real path is %s) exist and a directory... ###", path, fake_cwd.get_current());
		return false;	// the path is invalid (inconsist) // ESP_LOGE("Device::valid_path", "Path \"%s\" (real path %s) is a file, but marked as a directory, it's invalid!!!", path, fake_cwd.get_current());
	ESP_LOGD("Device::valid_path", "###!!! test dirname \"%s\" (real path is %s) preliminary is OK, seek to begin of last dir manually for continue test... ###", path.c_str(), current().c_str());
	for (base -= 2; base > path; base--)
	{
	    ESP_LOGD("Device::valid_path", "=== base[0] is \"%c\" ===", base[0]);
	    if (*base == '/')
		break;
	}; /* for base--; base > path; base-- */
    }; /* if empty(base) */
#else	// new variant of the base-scan
	size_t base_idx = path.length() - base.length();
//	auto base_scan = path.end() - base.length();
//    auto base_scan = path.rbegin() + base.length();	// for reversed scan
    // if dirname is not empty
    if (base_idx > 0)
	base_idx--;	// set base to a last slash in the path
    else return true;
#if 0
    else
    {
	if (stat(compose(path).c_str(), &st) == 0)
	    if (!S_ISDIR(st.st_mode))	// ESP_LOGD("Device::valid_path", "###!!! the path basename - is empty, test the path \"%s\" (real path is %s) exist and a directory... ###", path, fake_cwd.get_current());
		return false;	// the path is invalid (inconsist) // ESP_LOGE("Device::valid_path", "Path \"%s\" (real path %s) is a file, but marked as a directory, it's invalid!!!", path, fake_cwd.get_current());
	ESP_LOGD("Device::valid_path", "###!!! test dirname \"%s\" (real path is %s) preliminary is OK, seek to begin of last dir manually for continue test... ###", path.c_str(), current().c_str());
	/// Slide back to first '/' symbol
	for (base_idx -= 2; base_idx > 0; base_idx--)
	{
	    ESP_LOGD("Device::valid_path", "=== path(base_idx) is \"%c\" ===", path[base_idx]);
	    if (path[base_idx] == '/')
		break;
	}; /* for base_idx -= 2; base_idx > 0; base_idx-- */
    }; /* if empty(base) */
#endif	// 0
#endif	// all base-scan variants

#define sign_place 0x2	// with of the place for the sign
#define point_sign 0x1	// mark a point symbol in a string
#define alpha_sign 0x2	// mark a non-point or a non-slash symbol in a string
#define init_pass (0x3 << 4*sign_place)	// mark for the initial pass of the control of the path validity
#define alpha_present_mask (alpha_sign | (alpha_sign << 1*sign_place) | (alpha_sign << 2*sign_place))
#define three_point_mark (point_sign | (point_sign << 1*sign_place) | (point_sign << 2*sign_place))

	unsigned int ctrl_cnt = init_pass;	// marked the firs pass of the control loop
	unsigned int idx_ctrl = 0;
    // scan the dirname of the path for found '/.' or '/..' sequence
//    for (char* scan = base; scan >= path.c_str(); scan--)
	//    for (auto scan : path)
    for (auto scan = base_idx; scan > 0; scan--)
    {
	ESP_LOGD("Device::valid_path", "current char from the path is: '%c', ctrl_cnt is %2X", path[scan], ctrl_cnt);

//	switch (scan[0])
	switch (path[scan])
	{
	// solution point
	case '/':

	    idx_ctrl = 0;	// reset the idx_ctrl
	    ESP_LOGD("Device::valid_path", "###### Solution point: current path char ######");
	    switch (ctrl_cnt)
	    {
	    // double slash - prev symbol is slash
	    case 0:
		ESP_LOGD("Device::valid_path", "**** double slash and more - is not valid sequence in the path name ****");
		return false;

	    case three_point_mark:
		// if more then 3 point sequence in substring
		ESP_LOGD("Device::valid_path", "3 point or more sequence is present in current substring - nothing to do, continue");
		break;

	    case init_pass:
		ESP_LOGD(__PRETTY_FUNCTION__, "++++++ The first pass of the control loop ++++++");
		[[fallthrough]];
	    default:
		// if non point sign is present in tested substring
		if (ctrl_cnt & alpha_present_mask)
		{
		    ESP_LOGD("Device::valid_path", "alpha or other then point or slash symbol is present in current processing substring - test subpath for exist, continue");
		    ctrl_cnt = 0;
		    continue;
		}; /* if ctrl_cnt & alpha_present_mask */
		ESP_LOGD(__PRETTY_FUNCTION__, "====== One or two point sequence in the current meaning substring, ctrl_cnt is %2X, test current subpath for existing ======", ctrl_cnt);
//		ESP_LOGD(__PRETTY_FUNCTION__, "### Testing the current substring \"%s\" for existing ###", compose(path.c_str(), scan - path.c_str()));
		ESP_LOGD(__PRETTY_FUNCTION__, "### Testing the current substring \"%s\" for existing ###", compose(path.substr(0, base_idx)).c_str());
//		if ((stat(compose(path, scan - path.c_str()).c_str(), &st) == 0)? !S_ISDIR(st.st_mode): (is_root(current()/*strcmp(current(), "/"*/) != 0))
		std::string tmp = compose(path.substr(0, base_idx));
//		if ((stat(compose(path.substr(0, base_idx)).c_str(), &st) == 0)? !S_ISDIR(st.st_mode): is_root(get()))
		if ((stat(tmp.c_str(), &st) == 0)? !S_ISDIR(st.st_mode): is_root(tmp))
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
//		ctrl_cnt |= (((scan[0] == '.')? point_sign: alpha_sign) << idx_ctrl++ * sign_place);	// ESP_LOGD("Device::valid_path", "%d symbol of the processing substring, symbol is \"%c\"", idx_ctrl, scan[0]);
		ctrl_cnt |= (((path[scan] == '.')? point_sign: alpha_sign) << idx_ctrl++ * sign_place);	// ESP_LOGD("Device::valid_path", "%d symbol of the processing substring, symbol is \"%c\"", idx_ctrl, scan[0]);
	}; /* switch scan[0] */
    }; /* for char* scan = base; scan >= path; scan-- */

    return true;
}; /* CWD::valid_path() */



}; // namespace Exec	//---------------------------------------------------------------------------------------------


//--[ cwd_emulate.cpp ]------------------------------------------------------------------------------------------------
