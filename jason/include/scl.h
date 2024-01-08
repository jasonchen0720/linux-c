#ifndef __SCL_H__
#define __SCL_H__


int config_parser(
	char comment_char,  /* Character that indicates the start of a comment */
	char sep_char,		/* Character that separates keys from values */
	char str_char,		/* Character that delineates string literals */
	const char *path,
	void (*fn)(char *, char *, void *), /* Prototype: void fn(char *key, char *value, void *arg); */ 
	void *arg);
#endif
