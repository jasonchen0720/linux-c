#ifndef __CO_LOG_H__
#define __CO_LOG_H__
#include <stdio.h>

#define __GENERIC_DBG 1
#include "generic_log.h"

#define LOG_TAG "co"
#define LOG(format,...) LOGI(format"\n", ##__VA_ARGS__)

#define rolec "co"
#define roles "sc"

#ifdef __GENERIC_DBG
#define LOGT(role, p, format, ...) GENERIC_LOG("T %s@%p: %s(%d): "format"\n", role, p, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define LOGT(role, p, format, ...) SIMPLE_LOG(LOG_FILE, LOG_SIZE, "T %s@%p: %s(%d): "format"\n", role, p, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#endif

#define TRACE(role, p, format, ...)  LOGT(role, p, format"\n", ##__VA_ARGS__)
#endif
