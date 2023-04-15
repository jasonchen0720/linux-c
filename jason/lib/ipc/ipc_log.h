#ifndef __IPC_LOG_H__
#define __IPC_LOG_H__
#include <stdio.h>

#define IPC_LOG_FILE			"/tmp/ipclog"
#define IPC_LOG_FILE_BAK		"/tmp/ipclog.0"
#define IPC_LOG_MAX_SIZE 		512*1024

void IPC_LOG(const char *format, ...);
#define IPC_LOG_CONSOLE 	0
#define IPC_LOG_DEBUG 		0
#if IPC_LOG_CONSOLE
#define IPC_LOGI(format,...)  printf("I [%s] %s(%d): "format"\n", __LOGTAG__, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define IPC_LOGW(format,...)  printf("W [%s] %s(%d): "format"\n", __LOGTAG__, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define IPC_LOGE(format,...)  printf("E [%s] %s(%d): "format"\n", __LOGTAG__, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define IPC_LOGD(format,...)  printf("D [%s] %s(%d): "format"\n", __LOGTAG__, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define IPC_LOGI(format,...) IPC_LOG("I [%s] %s(%d): "format"\n", __LOGTAG__, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define IPC_LOGW(format,...) IPC_LOG("W [%s] %s(%d): "format"\n", __LOGTAG__, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define IPC_LOGE(format,...) IPC_LOG("E [%s] %s(%d): "format"\n", __LOGTAG__, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#if IPC_LOG_DEBUG
#define IPC_LOGD(format,...) IPC_LOG("D [%s] %s(%d): "format"\n", __LOGTAG__, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define IPC_LOGD(format,...)
#endif
#endif

#endif
