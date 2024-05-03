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



namespace fs	//-----------------------------------------------------------------------------------------------------
{

    [[maybe_unused]]
    static const char *TAG = "CWD emulating";


//--[ class CWD_emulating ]-----------------------------------------------------------------------------------------


// get current dir (if path == NULL or "") or generate fullpath for sended path
// absent trailing slash in returned string is guaranteed

// return current pwd (current dir) only
char* CWD_emulating::get()
{
    ESP_LOGD("CWD_emulating:", "%s: \"path\" argument is absent",  __func__);
    ESP_LOGD("CWD_emulating:", "%s: \"len\" argument is absent too",  __func__);
    strcpy(operative_path_buff, pwd);
    return operative_path_buff;
}; /* char* CWD_emulating::get() */

//// return full path appling current dir
//char* get(const char path[]);

// return full path appling current dir, use desired part of the passed path
// return current dir (if path == NULL or "") or generate fullpath for sended path
// trailing slash in returned string is absent always
char* CWD_emulating::get(const char path[], size_t len)
{
    ESP_LOGD("CWD_emulating:", "%s: \"path\" argument is %s",  __func__, path);
    ESP_LOGD("CWD_emulating:", "%s: \"len\" argument is %d",  __func__, len);
    // if len not defined
    if (len == 0)
	return get(path);

    // argument - absolute path
    if (!empty(path) && absolute_path(path))
    {
	if (len > sizeof(operative_path_buff) / sizeof(char) - 1)
	    clearbuff();	// path don't fit in operative_path_buff - error, return empty str
	else
	{
		char* tmpstr = (char*)malloc(len + 1);
	    strncpy(tmpstr, path, len/* + 1*/);
	    tmpstr[len] = '\0';	// complete the NULL-terminated string
	    realpath(tmpstr, operative_path_buff);
	    free(tmpstr);
	}; /* if len > (sizeof(operative_path_buff) / sizeof(char) - 1) */
	ESP_LOGD("CWD_emulating:", "%s: operative_path_buff is \"%s\"", __func__, operative_path_buff);
	return operative_path_buff;
    }; /* if path[0] != '/' */

    get();
    // pwd == "" --> catch it
    if (empty(pwd))
	return get_current();

    // argument - NULL or empty string
    if (empty(path))
	return get_current();

    // relative path - finalize processing
    ESP_LOGD("CWD_emulating:", "%s: processing relative path: updating path on top of the current pwd", __func__);
    // add a trailing slash at end of the relative path base
    if (operative_path_buff[strlen(operative_path_buff) - 1] != '/')
    {
	ESP_LOGD("CWD_emulating:", "%s: operative_path_buff before adding trailing slash is: \"%s\"", __func__, operative_path_buff);
	// add EOL behind the string data in the operative_path_buff
	// add trailing '/' at the operative_path_buff
	operative_path_buff[strlen(operative_path_buff) + 1] = '\0';
	operative_path_buff[strlen(operative_path_buff)] = '/';
	//strcat(operative_path_buff, "/");
	ESP_LOGD("CWD_emulating:", "%s: operative_path_buff after adding trailing slash is: \"%s\"", __func__, operative_path_buff);
    }; /* if operative_path_buffer[strlen(operative_path_buff) - 1] != '/' */

    // copy path on top of base bath
    if (strlen(operative_path_buff) + len < sizeof(operative_path_buff) / sizeof(char))
	strncat(operative_path_buff, path, len);
    else
	return clearbuff();

    ESP_LOGD("CWD_emulating:", "%s: final operative_path_buff after drop it's trailing slash: \"%s\"", __func__, operative_path_buff);

	 char* src = realpath(operative_path_buff, NULL);	// resolve dirty path
    strcpy(operative_path_buff, src);
    free(src);

    return operative_path_buff;
}; /* CWD_emulating::get */


// raw_get path with current dir:
// only concatenate path with current dir,
// not processing output with realpath().
char* CWD_emulating::raw_get(const char path[])
{
    if (empty(path))
	return strcpy(operative_path_buff, pwd);
    if(absolute_path(path))
	return strcpy(operative_path_buff, path);
    raw_get();
    strcat(operative_path_buff, "/");	// add directory separator at end of the default path
    return strcat(operative_path_buff, path);	// add the path above to default path
}; /* CWD_emulating::raw_get */


// change cwd dir
esp_err_t CWD_emulating::change_dir(const char path[])
{
	const char* tmpstr = get(path);
	struct stat statbuf;

    ESP_LOGD("CWD_emulating::change_dir", "The \"path\" parameter is: \"%s\"", path);
    ESP_LOGD("CWD_emulating::change_dir", "The \"tmpstr\" variable is: \"%s\"", tmpstr);

    if (tmpstr == nullptr || tmpstr[0] == '\0')
    {
	ESP_LOGE("CWD_emulating::change_dir", "Change dir is failed");
	return ESP_FAIL;
    }; /* if tmpstr == nullptr || tmpstr[0] == '\0' */
    // if dir changed to root - exclusively change dir
    if (strcmp(tmpstr,"/") == 0)
	goto final_copy;
    if (stat(tmpstr, &statbuf) == -1)
    {
	ESP_LOGE("CWD_emulating::change_dir", "Change dir is failed - requested path to change \"%s\" is not exist;\n"
		"\t\t\t\tcurrent directory was not changing", tmpstr);
	return ESP_ERR_NOT_FOUND;
    }; /* if stat(tmpstr, &statbuf) == -1 */
    ESP_LOGD("CWD_emulating::change_dir", "to %s which is a %s\n", tmpstr,
	    (S_ISLNK(statbuf.st_mode))? "[symlink]":
	    (S_ISREG(statbuf.st_mode))? "(file)":
	    (S_ISDIR(statbuf.st_mode))? "<DIR>":
	    (S_ISCHR(statbuf.st_mode))? "[char dev]":
	    (S_ISBLK(statbuf.st_mode))? "[blk dev]":
	    (S_ISFIFO(statbuf.st_mode))? "[FIFO]":
	    (S_ISSOCK(statbuf.st_mode))? "[socket]":
	    "[unknown type]");
    if (!S_ISDIR(statbuf.st_mode))
    {
	ESP_LOGE("CWD_emulating::change_dir", "Change dir is failed - requested path to change \"%s\" is not directory;\n"
		"\t\t\t\tleave current directory without changing", tmpstr);
	return ESP_ERR_NOT_SUPPORTED;
    }; /* if stat(tmpstr, &statbuf) == -1 */

final_copy:
    // copy tmpstr to pwd at the final
    strcpy(pwd, tmpstr);
    return ESP_OK;
}; /* CWD_emulating::change_dir */


/// @details if the basename (the last part of the path) - has the characteristics
/// of a directory name, and a dirname (the path prefix) -
/// is an existing file, not a directory, or any other impossible variants
/// of the full file/path name
bool CWD_emulating::valid_path(const char path[])
{

	struct stat st;
	char *base = basename(path);	// get a filename of a path

/*esp_log_level_set("Device::valid_path", ESP_LOG_DEBUG);*//* for debug purposes */
    ESP_LOGD(__PRETTY_FUNCTION__, "basename of the path is: \"%s\"", base);
    ESP_LOGD(__PRETTY_FUNCTION__, "full path is: \"%s\"", path);
    ESP_LOGD(__PRETTY_FUNCTION__, "dirname path is: \"%.*s\"", base - path, path);

//    strcpy(given_path, fake_cwd.curr_get());
    // if path is empty
    if (empty(path))
	return true;	// ESP_LOGD("Device::valid_path", "path is empty, always valid");

    // if path - only base, not a dir
    if (strlen(path) == 1)
	return true;	// ESP_LOGD("Device::valid_path", "len of the path - is 1, always valid");

    // if dirname - empty or one symbol length (it can only be the slash)
    if ((base - path) < 2)
    {
	if (strcmp(path, "/..") == 0)	// if path == '/..' - it's invalid
	    return false;
	return true;	// ESP_LOGD("Device::valid_path", "Len of dirname is 1 or 0, then path is valid");
    }; /* if (base - path) < 2 */

    // if base is empty
    if (!empty(base))
	base--;	// set base to a last slash in the path
    else
    {
	if (stat(get(path), &st) == 0)
	    if (!S_ISDIR(st.st_mode))	// ESP_LOGD("Device::valid_path", "###!!! the path basename - is empty, test the path \"%s\" (real path is %s) exist and a directory... ###", path, fake_cwd.get_current());
		return false;	// the path is invalid (inconsist) // ESP_LOGE("Device::valid_path", "Path \"%s\" (real path %s) is a file, but marked as a directory, it's invalid!!!", path, fake_cwd.get_current());
	ESP_LOGD("Device::valid_path", "###!!! test dirname \"%s\" (real path is %s) preliminary is OK, seek to begin of last dir manually for continue test... ###", path, get_current());
	for (base -= 2; base > path; base--)
	{
	    ESP_LOGD("Device::valid_path", "=== base[0] is \"%c\" ===", base[0]);
	    if (*base == '/')
		break;
	}; /* for base--; base > path; base-- */
    }; /* if empty(base) */

#define sign_place 0x2	// with of the place for the sign
#define point_sign 0x1	// mark a point symbol in a string
#define alpha_sign 0x2	// mark a non-point or a non-slash symbol in a string
#define initial_ctrl (0x3 << 4*sign_place)	// mark for the initial pass of the control of the path validity
#define alpha_present_mask (alpha_sign | (alpha_sign << 1*sign_place) | (alpha_sign << 2*sign_place))
#define three_point_mark (point_sign | (point_sign << 1*sign_place) | (point_sign << 2*sign_place))

	unsigned int ctrl_cnt = initial_ctrl;	// marked the firs pass of the control loop
	unsigned int idx_ctrl = 0;
    // scan the dirname of the path for found '/.' or '/..' sequence
    for (char* scan = base; scan >= path; scan--)
    {
	ESP_LOGD("Device::valid_path", "current char from the path is: '%c', ctrl_cnt is %2X", *scan, ctrl_cnt);

	switch (scan[0])
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

	    case initial_ctrl:
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough="
		ESP_LOGD(__PRETTY_FUNCTION__, "++++++ The first pass of the control loop ++++++");
#pragma GCC diagnostic pop

	    default:
		// if non point sign is present in tested substring
		if (ctrl_cnt & alpha_present_mask)
		{
		    ESP_LOGD("Device::valid_path", "alpha or other then point or slash symbol is present in current processing substring - test subpath for exist, continue");
		    ctrl_cnt = 0;
		    continue;
		}; /* if ctrl_cnt & alpha_present_mask */
		ESP_LOGD(__PRETTY_FUNCTION__, "====== One or two point sequence in the current meaning substring, ctrl_cnt is %2X, test current subpath for existing ======", ctrl_cnt);
		ESP_LOGD(__PRETTY_FUNCTION__, "### Testing the current substring \"%s\" for existing ###", get(path, scan - path));
		if ((stat(get(path, scan - path), &st) == 0)? !S_ISDIR(st.st_mode): (strcmp(get_current(), "/") != 0))
		    return false;
	    }; /* switch ctrl_cnt */
	    ctrl_cnt = 0;
	    break;

	// point symbol handling
	case '.':
	// all other symbols
	default:

	    if (idx_ctrl < 3)
		ctrl_cnt |= (((scan[0] == '.')? point_sign: alpha_sign) << idx_ctrl++ * sign_place);	// ESP_LOGD("Device::valid_path", "%d symbol of the processing substring, symbol is \"%c\"", idx_ctrl, scan[0]);
	}; /* switch scan[0] */
    }; /* for char* scan = base; scan >= path; scan-- */

    return true;
}; /* CWD_emulating::valid_path() */



// temporary buffer for file fullpath composing
char CWD_emulating::operative_path_buff[PATH_MAX];


}; /* namespace fs */  //----------------------------------------------------------------------------------------------


//--[ cwd_emulate.cpp ]------------------------------------------------------------------------------------------------
