#ifndef __GENERIC_FILE_H__
#define __GENERIC_FILE_H__


int utils_file_write_integer(const char *filename, int integer);
int utils_file_write_string(const char *filename, const char *string, unsigned int size);
int utils_file_append_string(const char *filename, const char *string, unsigned int size);
int utils_file_read_integer(const char *filename, int dvalue);
int utils_file_read_string(const char *filename, char *dest, unsigned int size);
int utils_file_copy(const char *src_file, const char *dst_file);
int utils_file_trunc(const char* filename);

int sys_file_wronly_integer(const char *filename, int integer);
int sys_file_wronly_string(const char *filename, const char *string);
int sys_file_write_integer(const char *filename, int integer);
int sys_file_write_string(const char *filename, const char *string, unsigned int size);
int sys_file_read_integer(const char *filename, int dvalue);
int sys_file_read_string(const char *filename, char *dest, unsigned int size);
int sys_file_trunc(char* filename);
#endif