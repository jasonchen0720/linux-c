#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>



enum CONF_PARSE_STATE {
  CONF_PARSE_INITIAL,
  CONF_PARSE_COMMENT,
  CONF_PARSE_QUOTED,
  CONF_PARSE_UNQUOTED,
  CONF_HANDLE_NEWLINE,
  CONF_HANDLE_SEP
};
#define CONF_BUFSIZE 	512
#define CONF_TOKSIZE	128
int config_parser(
	char comment_char,  /* Character that indicates the start of a comment */
	char sep_char,		/* Character that separates keys from values */
	char str_char,		/* Character that delineates string literals */
	const char *path,
	void (*fn)(char *, char *, void *), /* Prototype: void fn(char *key, char *value, void *arg); */ 
	void *arg)
{
	FILE *f;
	char buf[CONF_BUFSIZE];
	char key[CONF_TOKSIZE] = {0};
	char val[CONF_TOKSIZE] = {0};
	char *p;
	char *t = key; 
	int state = CONF_PARSE_INITIAL;
	int count, line = 1, i = 0;
	if (path == NULL)
		return EINVAL;

	f = fopen(path, "r");
	if (f == NULL)  {
		fprintf(stderr, "Unable to open '%s'\n", path);
		return ENOENT;
	}
	do {
    	count = fread(buf, sizeof(char), CONF_BUFSIZE, f);
		for (p = buf; p < buf + count;) {
			switch (state) {
			case CONF_PARSE_INITIAL:			/* Initial parsing state */
				if (*p == comment_char) {
				  	state = CONF_PARSE_COMMENT;
				  	++p;
				} else if (*p == str_char) {
				  	state = CONF_PARSE_QUOTED;
				  	++p;
				} else if (*p == '\n') {
				  	state = CONF_HANDLE_NEWLINE;
				} else if (*p == sep_char) {
				  	state = CONF_HANDLE_SEP;
				} else if (isspace(*p)) {
				  	++p;
				} else {
					if (i < CONF_TOKSIZE - 1)
				  		t[i++] = *p;
					p++;
				  	state = CONF_PARSE_UNQUOTED;
				}
				break;
			case CONF_PARSE_COMMENT:			/* Parse comments */
				if (*p == '\n') {
				  state = CONF_HANDLE_NEWLINE;
				} else {
				  ++p;
				}
				break;
			case CONF_PARSE_QUOTED:			/* Parse quoted strings */
				if (*p == str_char) {
				 	state = CONF_PARSE_INITIAL;
				  	++p;
				} else if (*p == '\n') {
				  	fprintf(stderr, "Unterminated string (%s:%d)\n", path, line);
					state = CONF_HANDLE_NEWLINE;
				} else {
				  	if (i < CONF_TOKSIZE - 1)
				  		t[i++] = *p;
					p++;
				}
				break;
			case CONF_PARSE_UNQUOTED:		/* Parse unquoted strings */
				if (*p == comment_char) {
				  	state = CONF_PARSE_COMMENT;
				  	++p;
				} else if (*p == '\n') {
				  	state = CONF_HANDLE_NEWLINE;
				} else if (*p == sep_char) {
				  	state = CONF_HANDLE_SEP;
				} else if (isspace(*p)) {		/* In this mode a space ends the current token */
					state = CONF_PARSE_INITIAL;
					++p;
				} else {
					if (i < CONF_TOKSIZE - 1)
				  		t[i++] = *p;
					p++;
				}
				break;
			case CONF_HANDLE_SEP:			/* Process separator characters */
				if (t == val) {		/* in case of key==value */
					if (*p == '\n')
						state = CONF_HANDLE_NEWLINE;
					else
						p++;
				} else {
					t[i] = '\0';
					//printf("Found key (%s:%d):%s\n", path, line, t);
					t = val;
					i = 0;
					state = CONF_PARSE_INITIAL;
					++p;
				}
				break;
			case CONF_HANDLE_NEWLINE:		/* Process newlines */
				if (key[0] != '\0') {
					t[i] = '\0';
					//printf("Found Value (%s:%d):%s\n", path, line, t);
					fn(key, val, arg);
					key[0] = '\0';
					val[0] = '\0';
				}
				t = key;
				i = 0;
				state = CONF_PARSE_INITIAL;
				++line;
				++p;
				break;
			} 
		}    
	} 
	while (feof(f) == 0);
	if (key[0] != '\0') {
		t[i] = '\0';
		fn(key, val, arg);
	}
	fclose(f);
	return 0;
}

