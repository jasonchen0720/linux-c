#ifndef __CO_LOG_H__
#define __CO_LOG_H__
#include <stdio.h>
#define LOG(format,...) printf("%s(%d): "format"\n\n",  __FUNCTION__, __LINE__, ##__VA_ARGS__)
#endif
