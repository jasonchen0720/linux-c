#ifndef __IPC_LOG_H__
#define __IPC_LOG_H__
#include <stdio.h>
//#define __DEBUG_INFO__
#ifdef __DEBUG_INFO__
#define IPC_INFO(format,...) 		printf("I [%s] %s(%d): "format"\n", __LOGTAG__,__FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define IPC_INFO(format,...)
#endif

//#define __DEBUG_WARNING__
#ifdef __DEBUG_WARNING__
#define IPC_WARNING(format,...) 	printf("W [%s] %s(%d): "format"\n", __LOGTAG__,__FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define IPC_WARNING(format,...)
#endif

#define __DEBUG_ERR__
#ifdef __DEBUG_ERR__
#define IPC_ERR(format,...) 		printf("E [%s] %s(%d): "format"\n", __LOGTAG__,__FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define IPC_ERR(format,...)
#endif

#define IPC_LOG_FILE			"/tmp/ipclog"
#define IPC_LOG_FILE_BAK		"/tmp/ipclog.0"
#define IPC_LOG_MAX_SIZE 		512*1024

void IPC_LOG(const char *format, ...);
//#define __FULL_DEBUG__
#ifdef __FULL_DEBUG__
#define IPC_LOGI(format,...) IPC_INFO(format, ##__VA_ARGS__)
#define IPC_LOGW(format,...) IPC_WARNING(format, ##__VA_ARGS__)
#define IPC_LOGE(format,...) IPC_ERR(format, ##__VA_ARGS__)
#else
#define IPC_LOGI(format,...) IPC_LOG("I [%s] %s(%d): "format"\n", __LOGTAG__, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define IPC_LOGW(format,...) IPC_LOG("W [%s] %s(%d): "format"\n", __LOGTAG__, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define IPC_LOGE(format,...) IPC_LOG("E [%s] %s(%d): "format"\n", __LOGTAG__, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#endif

#endif
