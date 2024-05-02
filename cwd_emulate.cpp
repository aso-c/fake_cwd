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
//#include <esp_console.h>
//#include <esp_system.h>
//#include <argtable3/argtable3.h>
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
//#include <sdmmc_cmd.h>
//#include <driver/sdmmc_host.h>

#include "cwd_emulate"

//#include "sdcard_io"

#include "extrstream"
#include "astring.h"

//using namespace idf;
using namespace std;


//#define SD_MOUNT_POINT "/sdcard"


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

// temporary buffer for file fullpath composing
char CWD_emulating::operative_path_buff[PATH_MAX];


}; /* namespace fs */  //----------------------------------------------------------------------------------------------


//--[ cwd_emulate.cpp ]------------------------------------------------------------------------------------------------
