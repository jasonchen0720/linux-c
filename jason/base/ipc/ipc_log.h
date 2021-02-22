#ifndef __IPC_LOG_H__
#define __IPC_LOG_H__
#include <stdio.h>
//#define __DEBUG_INFO__ 
#ifdef __DEBUG_INFO__  
#define IPC_INFO(tag,format,...) 		printf("INFO [%s]:%s(%d): "format"\n", tag,__FUNCTION__, __LINE__, ##__VA_ARGS__)  
#else  
#define IPC_INFO(tag,format,...)  
#endif  

//#define __DEBUG_WARNING__  
#ifdef __DEBUG_WARNING__  
#define IPC_WARNING(tag,format,...) 	printf("WARNING [%s]:%s(%d): "format"\n", tag,__FUNCTION__, __LINE__, ##__VA_ARGS__)  
#else  
#define IPC_WARNING(tag,format,...)  
#endif  

#define __DEBUG_ERR__  
#ifdef __DEBUG_ERR__  
#define IPC_ERR(tag,format,...) 		printf("ERROR [%s]:%s(%d): "format"\n", tag,__FUNCTION__, __LINE__, ##__VA_ARGS__)  
#else  
#define IPC_ERR(tag,format,...)  
#endif  

#define IPC_LOG_FILE			"/tmp/ipclog"
#define IPC_LOG_FILE_BAK		"/tmp/ipclog.0"
#define IPC_LOG_MAX_SIZE 		512*1024

void IPC_LOG(const char *format, ...);
#endif
