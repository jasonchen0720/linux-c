#ifndef __CO_LOG_H__
#define __CO_LOG_H__
#include <stdio.h>

#define __GENERIC_DBG 1
#include "generic_log.h"

#define LOG_TAG "co"
#define LOG(format,...) LOGI(format"\n", ##__VA_ARGS__)

#endif
