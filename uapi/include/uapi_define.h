#ifndef __UAPI_DEFINE_H__
#define __UAPI_DEFINE_H__
#include <string.h>

#define UAPI_PATH_PREFIX	"uapi:"
#define UAPI_PATH(path)		UAPI_PATH_PREFIX#path

static inline int is_uapi_path(const char *path)
{
	return !strncmp(path, UAPI_PATH_PREFIX, sizeof(UAPI_PATH_PREFIX) - 1);
}
#endif
