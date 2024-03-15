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
	if(fp == NULL)
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
	if(fp == NULL)
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
	if(fp == NULL)
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
	if(fp != NULL)
	{
		if(NULL != fgets(buf, sizeof(buf), fp))
		{
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
	if (NULL == fgets(dest, size, fp))
	{
		fclose(fp);
		return -1;
	}
	fclose(fp);
	for(i = 0; dest[i] != '\0'; i++)
	{
		if(dest[i] == '\n')
		{
			dest[i] = '\0';
			break;
		}
	}
	return i;
}

int utils_file_copy(char * src_file, char *dest_file)
{
    #define  BUFFSIZE  1024

	char buff[BUFFSIZE+1]={0};
	int ret = 0;
	if(NULL == src_file || NULL == dest_file)
	{
	    printf("NULL point input\n");
		return -1;
	}
        
	FILE *fin = fopen(src_file,"r");
	if(NULL == fin)
	{
	    printf("open file %s error\n",src_file);
        return -1;
	}
    FILE *fout = fopen(dest_file,"w+");
    if( NULL  == fout)
	{
        printf("open file %s error\n",dest_file);
		fclose(fin);
        return -1;
    }

	while(!feof(fin))
	{
		ret = fread(buff, 1, BUFFSIZE, fin);

		if (ret != BUFFSIZE)
		{
			fwrite(buff, ret, 1, fout);
		}
		else
		{
			fwrite(buff, BUFFSIZE, 1, fout);
		}
		memset(buff,0,sizeof(buff));
	}

	fclose(fin);
	fclose(fout);
	return 0;
}

int utils_file_recursion_copy(char * src_file, char *dest_file)
{
	char buff[BUFFSIZE+1]={0};
	int ret = 0;
	if(NULL == src_file || NULL == dest_file)
	{
	    printf("NULL point input\n");
		return -1;
	}

    char* p = strrchr(dest_file, '/');
    if (p != NULL) {
        char work_dir[128] = {0};
        strncpy(work_dir, dest_file, p - dest_file);
        if (access(work_dir, F_OK) != 0)
            mkdir(work_dir, 0775);
        else if (access(work_dir, W_OK) != 0)
            chmod(work_dir, 0775);
    }
    
	FILE *fin = fopen(src_file,"r");
	if(NULL == fin)
	{
	    printf("open file %s error\n",src_file);
        return -1;
	}
    FILE *fout = fopen(dest_file,"w+");
    if( NULL  == fout)
	{
        printf("open file %s error\n",dest_file);
		fclose(fin);
        return -1;
    }

	while(!feof(fin))
	{
		ret = fread(buff, 1, BUFFSIZE, fin);

		if (ret != BUFFSIZE)
		{
			fwrite(buff, ret, 1, fout);
		}
		else
		{
			fwrite(buff, BUFFSIZE, 1, fout);
		}
		memset(buff,0,sizeof(buff));
	}

	fclose(fin);
	fclose(fout);
	return 0;
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
	if(fd < 0)
	{
		printf("%s:open file err\n", filename);
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
	if(fd < 0)
	{
		printf("%s:open file err\n", filename);
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
	if(fd < 0)
	{
		printf("%s:open file err\n", filename);
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
	if(fd < 0)
	{
		printf("%s:open file err\n", filename);
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
	    printf("Can't open file: %s\n", filename);
	    return -1;
	}
	close(fd);
	return 0;
}
int sys_file_lock(int fd) 
{
	struct flock lock;
	lock.l_type = F_WRLCK;
	lock.l_start = 0;
	lock.l_len = 0;
	lock.l_whence = SEEK_SET;

	int ret;
	do {
		ret = fcntl(fd, F_SETLKW, &lock);
	} while (ret < 0 && errno == EINTR);
	
	return ret;
}

int sys_file_unlock(int fd) 
{
	struct flock lock;
	lock.l_type = F_UNLCK;
	lock.l_start = 0;
	lock.l_len = 0;
	lock.l_whence = SEEK_SET;

	int ret;
	do {
		ret = fcntl(fd, F_SETLK, &lock);
	} while (ret < 0 && errno == EINTR);
	
 	return ret;
}

/*
 * Add here, Please only put APIs related to file operation in this source file.
 */

struct xml_element {
	int taglen;
	int vallen;
	const char *value;
	const char *tagfront; 	/* Left  tag Address in XML file mmap memory region */
	const char *tagrear; 	/* Right tag Address in XML file mmap memory region */
	struct xml_element *next;
};

static int xml_findtag(struct xml_element *xmle, const char *tag_path, const char *value, const char *xml_mem)
{
	int len = strlen(tag_path);
	
	char *tag_buf = malloc(2 * len + 3);
	if (tag_buf == NULL)
		return -1;

	const char *p 	= xml_mem;
	const char *tag = tag_buf;
	char *c;
	char *tag_rear = tag_buf + len + 1;
	strcpy(tag_buf, tag_path);
	for (c = strchr(tag_buf, '/'); ;c = strchr(tag, '/')) {
		if (c) {
			c[0] = '\0';
			p = strstr(p, tag);
			if (p == NULL)
				break;
			tag = c + 1;
		} else {
			p = strstr(p, tag);
			break;
		}
	}

	if (p) {
		xmle->value	 = value;
		xmle->taglen = strlen(tag);
		xmle->vallen = strlen(value);
		xmle->tagfront = p;

		/* tag with attribute */
		c = strchr(tag, ' ');
		if (c) {
			c[0] = '>';
			c[1] = '\0';
		}
		len = sprintf(tag_rear, "</%s", tag + 1);
		xmle->tagrear  = strstr(p, tag_rear);
		
		if (xmle->tagrear == NULL) {
			printf("tag_path : %s -- %s not found.\n", tag_path, tag_rear);
			return 0;
		}

		/* Current value length */
		len = xmle->tagrear - xmle->tagfront - xmle->taglen;
		/* Compare whether the values are same */
		if (xmle->vallen == len && memcmp(xmle->tagfront + xmle->taglen, value, len) == 0) {
			printf("tag same: %s:%s", tag, value);
			return 0;
		}
#if 1
		char v[256] = {0};
		memcpy(v, xmle->tagfront + xmle->taglen, len < 256 ? len : 255);
		printf("tag changed: %s, %s --> %s", tag, v, value);
#endif		
		free(tag_buf);
		return 1;
	} else {
		printf("tag_path : %s -> %s not found.\n", tag_path, tag);
		free(tag_buf);
		return 0;
	}
}
 /*
  * @xml_path:  XML file path
  * @n:			XML tags number
  * @tag:		XML tags's name to be modified.
  *  eg: 
  *  tags without attribute:
  * 			<student>
  *					<name>Judy</name>
  *					<age>8</age>
  *					<sex>female</sex>
  *				</student>
  *
  *				tag of name: "<name>" or "<student>/<name>"
  *
  *	 tags with attribute:
  *				<students>
  *					<student id="1">
  *						<name>Judy</name>
  *						<age>8</age>
  *						<sex>female</sex>
  *					</student>
  * 				<student id="2">
  *						<name>Tom</name>
  *						<age>8</age>
  *						<sex>male</sex>
  *					</student>
  *				</students>
  *
  *				tag of student: "<student id="1">" or "<students>/<student id="1">"
  *
  * @value:		New values.
  */
int utils_file_xml_modify(const char *xml_path, int n, const char *tag[], const char *value[])
{
	char *xml_mem = NULL;
	int   xml_fd = -1;
	struct stat xml_stat;
	struct xml_element *xml_els = malloc(n * sizeof(struct xml_element));
	if (xml_els == NULL) 
		return -1;
	 
	xml_fd = open(xml_path, O_RDWR);
 
	if (xml_fd == -1) {
		printf("open %s failed:%d", xml_path, errno);
		return -1;
	}
 
	fstat(xml_fd, &xml_stat);

	printf("xml %s size:%ld", xml_path, xml_stat.st_size);

	xml_mem = mmap(0, xml_stat.st_size, PROT_READ, MAP_PRIVATE, xml_fd, 0);
 
	if (xml_mem == (void *)-1) {
		printf("mmap errno:%d", errno);
		goto err;
	}
	int i;
	int len = 0;
	struct xml_element *t;
	struct xml_element *h = NULL;
	struct xml_element**p = &h;
	for (i = 0; i < n; i++) {
		if (xml_findtag(&xml_els[i], tag[i], value[i], xml_mem) != 1)
			continue;

		len += xml_els[i].vallen;

		for (t = *p; t; t = *p) {
			if (xml_els[i].tagfront < t->tagfront)
	 			break;
			p = &t->next;
		}
		xml_els[i].next = t;
		*p = &xml_els[i];
	}
	if (h) {
#if 1
		for (t = h; t; t = t->next) {
			printf("acs tag:%p", t->tagfront);
		}
#endif
		char *xml_buf = (char *)malloc(xml_stat.st_size + len);
		if (xml_buf == NULL) {
			printf("malloc(%ld) errno:%d", xml_stat.st_size + len, errno);
			goto err;
		}
		char *pos	= xml_buf;
		const char *rear = xml_mem;
	 
		for (t = h; t; t = t->next) {
			len = t->tagfront - rear;
			memcpy(pos, rear, len);
			pos	 += len;

			memcpy(pos, t->tagfront, t->taglen);
			printf("Changing %s as %s  --- %p", pos, t->value, t->tagfront);
			pos += t->taglen;

			memcpy(pos, t->value, t->vallen);
			pos += t->vallen;

			memcpy(pos, t->tagrear, t->taglen + 1);
			pos += t->taglen + 1;

			rear = t->taglen + 1 + t->tagrear;
		}
	 
		char *xml_end = xml_mem + xml_stat.st_size;

		len = xml_end - rear;
		memcpy(pos, rear, len);
		pos += len;
		
		ftruncate(xml_fd, 0);

		len = pos - xml_buf;
		if (write(xml_fd, xml_buf, len) != len)
			printf("write errno:%d", errno);
		
		free(xml_buf);
		printf("xml %s modified, new size:%d", xml_path, len);
	} else 
		printf("xml %s no changes.", xml_path);
	munmap(xml_mem, xml_stat.st_size);
	close(xml_fd);
	free(xml_els);
	return 0;
 err:
	if (xml_mem)
		munmap(xml_mem, xml_stat.st_size);
	if (xml_fd > 0)
		close(xml_fd);
	if (xml_els)
		free(xml_els);
	return -1;
}

