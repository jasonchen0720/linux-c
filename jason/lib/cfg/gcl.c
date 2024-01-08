#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "gcl.h"

#define GCL_BUFSIZE 1024
#define GCL_TOKSIZE 256
enum PARSE_STATE
{
  GCL_PARSE_INITIAL,
  GCL_PARSE_COMMENT,
  GCL_PARSE_QUOTED,
  GCL_PARSE_UNQUOTED,
  GCL_HANDLE_NEWLINE,
  GCL_HANDLE_SEP
};
static int ascend(struct gcl_item *c, int n)
{
	int i;
	for(i = 0; i < n -1; i++)
	{
		if(strcmp(c[i].key, c[i+1].key) >= 0)
		{
			fprintf(stderr, "Jason: %s & %s order error\n", c[i].key, c[i+1].key);
			return 0;
		}
		c[i].value = NULL;
	}
	c[i-1].value = NULL;
	return 1;
}
static struct gcl_item *search(struct gcl_item *c, int n, const char *key)
{
    int l,r,m;
	int cmp;
	for(l = 0, r = n -1; l <= r;)
	{
		m = (l + r) >> 1;
		//printf("l m r: %d %d %d\n", l , m ,n);
		cmp = strcmp(key, c[m].key);
		if (cmp < 0)
			r = m - 1;
		else if(cmp > 0)
			l = m + 1;
		else
			return &c[m];

	}
	return NULL;
}
int gcl_set(struct gcl_item *c, int n, const char *key, const char *value)
{
	struct gcl_item *it = search(c, n, key);
	if(it == NULL)
		return -1;
	char *v;
	if(it->value)
	{
		if(!strcmp(value, it->value))
			return 0;
		if((v = strdup(value)) == NULL)
			return -1;
		free(it->value);
	}
	else
	{
		if((v = strdup(value)) == NULL)
			return -1;
	}
	it->value = v;
	return 1;
}
const char * gcl_get(struct gcl_item *c, int n, const char *key)
{
	struct gcl_item *it = search(c, n, key);
	if(it == NULL)
		return NULL;
	return it->value ? it->value : it->dval;
}
int gcl_init(struct gcl_item *c, int n, const char *path)
{
	FILE *f;
	char buf[GCL_BUFSIZE];
	char tok[GCL_TOKSIZE];
	char *p;
	char *t, *limit;
	int result = 0;
	int state = GCL_PARSE_INITIAL;
	size_t count;
	size_t line = 1;
	struct gcl_item *item = NULL;
	if(!ascend(c, n))
	{
		return EINVAL;
	}
	f = fopen(path, "r");
	if(f == NULL) 
	{
		fprintf(stderr, "Jason: Unable to open '%s'\n", path);
		return ENOENT;
	}
	limit = tok + (GCL_TOKSIZE - 1);
	t = tok;
	do 
	{
    	count = fread(buf, sizeof(char), GCL_BUFSIZE, f);
		for(p = buf; p < buf + count; /* ++p */ ) 
		{
			switch(state) 
			{
				case GCL_PARSE_INITIAL:			/* Initial parsing state */
					if(*p == '#') {
					  	state = GCL_PARSE_COMMENT;
					  	++p;
					}else if(*p == '"') {
					  	state = GCL_PARSE_QUOTED;
					  	++p;
					}else if(*p == '\n') {
					  	state = GCL_HANDLE_NEWLINE;
					}else if(*p == '=') {
					  	state = GCL_HANDLE_SEP;
					  	//++p;
					}else if(isspace(*p)) {
					  	++p;
					}else {
						if(t < limit)
					  		*t++ = *p;
						p++;
					  	state = GCL_PARSE_UNQUOTED;
					}
					break;
				case GCL_PARSE_COMMENT:		/* Parse comments */
					if(*p == '\n')
						state = GCL_HANDLE_NEWLINE;
					else
						p++;
					break;
				case GCL_PARSE_QUOTED:		/* Parse quoted strings */
					if(*p == '"') {
					 	state = GCL_PARSE_INITIAL;
					  	++p;
					}else if(*p == '\n') {
					  	fprintf(stderr, "Jason: Unterminated string (%s:%zi)\n",path, line);
						state = GCL_HANDLE_NEWLINE;
					}else {
					  	if(t < limit)
					  		*t++ = *p;
						p++;
					}
					break;
				case GCL_PARSE_UNQUOTED:	/* Parse unquoted strings */
					if(*p == '#') {
					  	state = GCL_PARSE_COMMENT;
					  	++p;
					}else if(*p == '\n') {
					  	state = GCL_HANDLE_NEWLINE;
					}else if(*p == '=') {
					  	state = GCL_HANDLE_SEP;
					  	//++p;
					}else if(isspace(*p)) {		/* In this mode a space ends the current token */
						state = GCL_PARSE_INITIAL;
						++p;
					}else {
						if(t < limit)
					  		*t++ = *p;
						p++;
					}
					break;
				case GCL_HANDLE_SEP:		/* Process separator characters */
					if(item)
					{
						if(*p == '\n')
							state = GCL_HANDLE_NEWLINE;
						else
							p++;
					}
					else
					{
						if(t == tok) {
						  	fprintf(stderr, "Jason: Missing key (%s:%zi)\n", path, line);
						}else {
							*t = '\0';
							item = search(c, n, tok);
							t = tok;
						}
						state = GCL_PARSE_INITIAL;
						++p;
					}
					break;
				case GCL_HANDLE_NEWLINE:	/* Process newlines */
					if(item != NULL) 
					{
						if(item->value)
						{
							*t = '\0'; 
							fprintf(stderr, "Jason: duplicate pair:%s(%s)(%s:%zi)\n", item->key, tok, path, line);
						}
					  	else
						{
							if(t > tok) {		/* Some type of token was parsed */
								*t = '\0';
					    		item->value = strdup(tok);
					  		}else {
					    		item->value = strdup("");	/* In this case we read a key but no value */
					  		}
					  		if(item->value == NULL) {
					    		result = ENOMEM;
					    		goto out;
					  		}
					  	}
						printf("pair finish->%s:%s\n", item->key, item->value);
						item = NULL;
					}
					t = tok;
					state = GCL_PARSE_INITIAL;
					++line;
					++p;
					break;
			} 
		}    
	} 
	while(feof(f) == 0);
	if(item)
	{
		if(t > tok) {	
			*t = '\0';
    		item->value = strdup(tok);
  		}else {
    		item->value = strdup("");
  		}
  		if(item->value == NULL) {
    		result = ENOMEM;
  		}
	}
  out:
	fclose(f);
	return result;
}
void gcl_free(struct gcl_item *c, int n)
{
	int i;
	for(i = 0; i < n; i++)
	{
		free(c[i].value);
		c[i].value = NULL;
	}
}
int gcl_reset(struct gcl_item *c, int n, const char *key)
{
	struct gcl_item *it = search(c, n, key);
	if(it == NULL)
		return -1;
	char *v;
	if(it->value)
	{
		if(!strcmp(it->dval, it->value))
			return 0;
		if((v = strdup(it->dval)) == NULL)
			return -1;
		free(it->value);
		it->value = v;
		return 1;
	}
	else
	{
		if((v = strdup(it->dval)) == NULL)
			return -1;
		it->value = v;
		return 0;
	}
}

int gcl_sync(struct gcl_item *c, int n, const char *path)
{
	int i, count;
	int len = 0;
	char buf[GCL_BUFSIZE] = {0};
	FILE *fp;
	
	fp = fopen(path, "w");
	if(fp == NULL) 
	{
		fprintf(stderr, "unable to open '%s'\n", path);
		return 0;
	}
	for(i = 0; i < n; i++)
	{
		len = sprintf(buf, "%s=%s\n", c[i].key, c[i].value?c[i].value:c[i].dval);
		count = fwrite(buf, sizeof(char), len, fp);
		if(len != count)
		{
			fprintf(stderr, "write config failed\n");
			return 0;
		}
		memset(buf, 0, sizeof(buf));
	}
	fflush(fp);
	fclose(fp);
	return 1;
}