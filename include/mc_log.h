#ifndef __MC_LOG_H__
#define __MC_LOG_H__
#include "generic_log.h"


#define LOG_FILE		"/tmp/mc.log"
#define LOG_SIZE		64 * 1024

/* Persistence logs */
#define PLOG_FILE		"/tmp/mc.log"
#define PLOG_SIZE		50 * 1024
#define LOGP(format,...) SIMPLE_LOG(PLOG_FILE, PLOG_SIZE, LOG_TAG": %s:%d "format"\n", __FUNCTION__, __LINE__, ##__VA_ARGS__)

/* heartbeat log */
#define HLOG_FILE		"/tmp/ht.log"
#define HLOG_SIZE		 10 * 1024
#define LOGH(format,...) SIMPLE_LOG(HLOG_FILE, HLOG_SIZE, "%.4s "format"\n", LOG_TAG, ##__VA_ARGS__)
#endif
