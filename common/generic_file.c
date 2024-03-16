/*
 * Please only put APIs related to file operation in this source file.
 */

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

#define DBG(format,...) printf(format"\n", ##__VA_ARGS__)

/*
 *inputs: filename : file path
 *		  integer  : written value
 *return: if success return 1, if error return 0
 */
int utils_file_write_integer(const char *filename, int integer)
{
	int ssize;
	char buf[32] = {0};
	FILE *fp = NULL;
	ssize = sprintf(buf, "%d\n", integer);
	fp = fopen(filename,"w+");
	if (fp == NULL)
		return 0;
	ssize = fwrite(buf,ssize,1,fp);
	fclose(fp);
	return ssize == 1;
}
/*
 *inputs: filename : file path
 *		  string   : string to be written
 *		  size	   : the length of the string
 *return: if success return 1, if error return 0
 */
int utils_file_write_string(const char *filename, const char *string, unsigned int size)
{
	FILE *fp = NULL;
	fp = fopen(filename, "w+");
	if (fp == NULL)
		return 0;
	size = fwrite(string, size, 1, fp);
	fwrite("\n",1,1,fp);
	fclose(fp);
    return size == 1;
}
/*
 *inputs: filename : file path
 *		  string   : string to be written after the original string
 *		  size	   : the length of the string
 *return: if success return 1, if error return 0
 */
int utils_file_append_string(const char *filename, const char *string, unsigned int size)
{
	FILE *fp = NULL;
	fp = fopen(filename, "a+");
	if (fp == NULL)
		return 0;
	size = fwrite(string, size, 1, fp);
	fwrite("\n",1,1,fp);
	fclose(fp);
    return size == 1;
}
/*
 *inputs: filename : file path
 *		  dvalue   : default value
 *return: if success return the read value, if error return default value
 */
int utils_file_read_integer(const char *filename, int dvalue)
{
	int integer = dvalue;
	FILE *fp = NULL;
	char buf[64] = {0};
	fp = fopen(filename, "r");
	if (fp != NULL) {
		if (NULL != fgets(buf, sizeof(buf), fp)) {
			sscanf(buf, "%d", &integer);
		}
		fclose(fp);
	}
	return integer;
}
/*
 *inputs: filename : file path
 *		  dest     : dest buffer
 *		  size	   : max size of dest buffer
 *return: if success return the string length, if error return -1
 */
int utils_file_read_string(const char *filename, char *dest, unsigned int size)
{
	int i;
	FILE *fp = NULL;
	fp = fopen(filename, "r");
	if (fp == NULL)
		return -1;
	if (NULL == fgets(dest, size, fp)) {
		fclose(fp);
		return -1;
	}
	fclose(fp);
	for (i = 0; dest[i] != '\0'; i++) {
		if (dest[i] == '\n') {
			dest[i] = '\0';
			break;
		}
	}
	return i;
}

int utils_file_copy(const char *src_file, const char *dst_file)
{
	int 		dst_fd	= -1;
	int 		src_fd	= -1;
	char	   *src_mm	= NULL;
	struct stat src_st;
	
	src_fd = open(src_file, O_RDONLY);
	
	if (src_fd == -1) {
		DBG("open %s failed:%d", src_file, errno);
		goto err;
	}
	
	dst_fd = open(dst_file, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR);
	
	if (dst_fd == -1) {
		DBG("open %s failed:%d", dst_file, errno);
		goto err;
	}
	
	if (fstat(src_fd, &src_st) == -1)
		goto err;

	src_mm = mmap(0, src_st.st_size, PROT_READ, MAP_PRIVATE, src_fd, 0);

	if (src_mm == (void *)-1) {
		DBG("mmap errno:%d", errno);
		goto err;
	}
	int size = write(dst_fd, src_mm, src_st.st_size);
	close(dst_fd);
	close(src_fd);
	munmap(src_mm, src_st.st_size);
	return size == src_st.st_size ? 0 : -1;
err:
	if (src_fd != -1)
		close(src_fd);
	if (dst_fd != -1)
		close(dst_fd);
	
	return -1;
}


int utils_file_trunc(const char* filename)
{
	FILE *fp = NULL;
	fp = fopen(filename, "w+");
	if(fp == NULL)
		return -1;
	fclose(fp);
    return 0;
}
int sys_file_wronly_integer(const char *filename, int integer)
{
	int size, len;
	char buf[16] = {0};
	int fd;
	len = sprintf(buf, "%d\n", integer);
	fd = open(filename, O_WRONLY|O_TRUNC);
	if (fd < 0) {
		DBG("%s:open file err", filename);
		return 0;
	}
	size = write(fd,buf, len);
	close(fd);
	return size == len;
}
int sys_file_wronly_string(const char *filename, const char *string)
{
	int size, len;
	int fd;
	fd = open(filename, O_WRONLY|O_TRUNC);
	if (fd < 0) {
		DBG("%s:open file err", filename);
		return 0;
	}
	len = strlen(string);
	size = write(fd, string, len);
	close(fd);
	return size == len;
}
int sys_file_write_integer(const char *filename, int integer)
{
	int size, len;
	char buf[16] = {0};
	int fd;
	len = sprintf(buf, "%d\n", integer);
	fd = open(filename, O_WRONLY|O_TRUNC|O_CREAT, S_IRUSR|S_IWUSR);
	if (fd < 0) {
		DBG("%s:open file err", filename);
		return 0;
	}
	size = write(fd,buf, len);
	close(fd);
	return size == len;
}
int sys_file_write_string(const char *filename, const char *string, unsigned int size)
{
	int ssize;
	int fd;
	fd = open(filename, O_WRONLY|O_TRUNC|O_CREAT, S_IRUSR|S_IWUSR);
	if (fd < 0) {
		DBG("%s:open file err", filename);
		return 0;
	}
	ssize = write(fd, string, size);
	write(fd, "\n", 1);
	close(fd);
	return size == ssize;
}
/*
 *inputs: filename : file path
 *		  dvalue   : default value
 *return: if success return the read value, if error return default value
 */
int sys_file_read_integer(const char *filename, int dvalue)
{
	int fd;
	int integer = dvalue;
	char buf[32] = {0};

	fd = open(filename, O_RDONLY);
	if (fd > 0) {
		if (read(fd, buf, sizeof(buf)) > 0)
			integer = atoi(buf);
		close(fd);
	}
	return integer;
}
/*
 *inputs: filename : file path
 *		  dest     : dest buffer
 *		  size	   : max size of dest buffer
 *return: if success return the string length, if error return -1
 */
int sys_file_read_string(const char *filename, char *dest, unsigned int size)
{
	int i;
	int fd;
	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return -1;
	size = read(fd, dest, size - 1);
	if ( size < 0) {
		close(fd);
		return -1;
	}
	close(fd);
	for (i = 0; i < size; i++) {
		if (dest[i] == '\n')
			break;
	}
	dest[i] = '\0';
	return i;
}
int sys_file_trunc(char* filename)
{
	int fd = -1;

	if (NULL == filename)
	    return -1;

	fd = open(filename, O_WRONLY | O_CREAT| O_TRUNC, S_IRUSR | S_IWUSR);
	if ( fd < 0 ) {
	    DBG("Can't open file: %s", filename);
	    return -1;
	}
	close(fd);
	return 0;
}
