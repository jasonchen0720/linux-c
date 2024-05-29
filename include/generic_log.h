#ifndef __GENERIC_LOG_H__
#define __GENERIC_LOG_H__

int  GENERIC_LOG_INIT(const char *file_path, int file_size, int file_count);
void GENERIC_LOG(const char *format, ...);
void SIMPLE_LOG(const char *file_path, int file_size, const char *format, ...);
#define __GENERIC_DBG 1
#if __GENERIC_DBG
#define LOGI(format,...) GENERIC_LOG("I %s: %s(%d): "format"\n", __LOG_TAG, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define LOGW(format,...) GENERIC_LOG("W %s: %s(%d): "format"\n", __LOG_TAG, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define LOGE(format,...) GENERIC_LOG("E %s: %s(%d): "format"\n", __LOG_TAG, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define LOGD(format,...) GENERIC_LOG("D %s: %s(%d): "format"\n", __LOG_TAG, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define LOGI(format,...) SIMPLE_LOG(__LOG_FILE, __LOG_SIZE, "I %s: %s(%d): "format"\n", __LOG_TAG, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define LOGW(format,...) SIMPLE_LOG(__LOG_FILE, __LOG_SIZE, "W %s: %s(%d): "format"\n", __LOG_TAG, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define LOGE(format,...) SIMPLE_LOG(__LOG_FILE, __LOG_SIZE, "E %s: %s(%d): "format"\n", __LOG_TAG, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define LOGD(format,...) SIMPLE_LOG(__LOG_FILE, __LOG_SIZE, "D %s: %s(%d): "format"\n", __LOG_TAG, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#endif
#endif
