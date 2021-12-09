/*
 * Copyright (c) 2021, <-Jason Chen->
 * Author: Jie Chen <jasonchen@163.com>
 * Date  : Created at 2021/02/21
 * Description : the implementation of customizable configuration
 */
#include <stdlib.h> 
#include <stdio.h>
#include <string.h>	
#include <ctype.h>	
#include <errno.h>
#include "ccl.h"

#define CCL_BUFSIZE 1024
#define CCL_TOKSIZE 256
enum PARSE_STATE{
  CCL_PARSE_INITIAL,
  CCL_PARSE_COMMENT,
  CCL_PARSE_QUOTED,
  CCL_PARSE_UNQUOTED,
  CCL_HANDLE_NEWLINE,
  CCL_HANDLE_SEP
};
static void ccl_free_pair(struct ccl_pair *pair)
{
	if (pair) {
		free(pair->key);
	  	free(pair->value);
	  	free(pair);
	}
}
static int ccl_comparator(const struct bst_node *bst_a, const struct bst_node *bst_b)
{
	const struct ccl_pair *a = bst_entry(bst_a, struct ccl_pair, node);
	const struct ccl_pair *b = bst_entry(bst_b, struct ccl_pair, node);
	return atoi(a->key) - atoi(b->key);
}
static int ccl_searcher(const void *item, const struct bst_node *bst_b)
{
	const struct ccl_pair *b = bst_entry(bst_b, struct ccl_pair, node);
	return atoi((const char *)item) - atoi(b->key);
}
static void ccl_destroyer(struct bst_node *n)
{
  struct ccl_pair *pair = bst_entry(n, struct ccl_pair, node);
  free(pair->key);
  free(pair->value);
  free(pair);
}
static void ccl_printer(const struct bst_node *n)
{
  struct ccl_pair *pair = bst_entry(n, struct ccl_pair, node);
  printf("%s\n", pair->key);
}
int ccl_parse(struct ccl_t *data, const char *path)
{
	FILE *f;
	char buf[CCL_BUFSIZE];
	char tok[CCL_TOKSIZE];
	char *p;
	char *t = tok; 
	char *tok_limit;
	int result = 0;
	int state = CCL_PARSE_INITIAL;
	int count, line = 1;
	struct ccl_pair *pair = NULL;
	struct bst_node *dup;
	if (data == NULL || path == NULL)
		return EINVAL;
	data->table = bst_create(ccl_comparator, ccl_searcher, ccl_printer, 3);
	if (data->table == NULL)
		return ENOMEM;    
	data->iterator = bst_iterator_init(data->table);
	if (data->iterator == NULL)
		return ENOMEM;
	data->iterating = 0;
	f = fopen(path, "r");
	if (f == NULL) {
		fprintf(stderr, "Jason: Unable to open '%s'\n", path);
		return ENOENT;
	}
	tok_limit = tok + (CCL_TOKSIZE - 1);
	do {
    	count = fread(buf, sizeof(char), CCL_BUFSIZE, f);
		for (p = buf; p < buf + count;) {
			switch (state) {
			case CCL_PARSE_INITIAL:			/* Initial parsing state */
				if (*p == data->comment_char) {
				  	state = CCL_PARSE_COMMENT;
				  	++p;
				} else if (*p == data->str_char) {
				  	state = CCL_PARSE_QUOTED;
				  	++p;
				} else if (*p == '\n') {
				  	state = CCL_HANDLE_NEWLINE;
				} else if (*p == data->sep_char) {
				  	state = CCL_HANDLE_SEP;
				  	//++p;
				} else if (isspace(*p)) {
				  	++p;
				} else {
					if (t < tok_limit)
				  		*t++ = *p;
					p++;
				  	state = CCL_PARSE_UNQUOTED;
				}
				break;
			case CCL_PARSE_COMMENT:			/* Parse comments */
				if (*p == '\n') {
				  state = CCL_HANDLE_NEWLINE;
				} else {
				  ++p;
				}
				break;
			case CCL_PARSE_QUOTED:			/* Parse quoted strings */
				if (*p == data->str_char) {
				 	state = CCL_PARSE_INITIAL;
				  	++p;
				} else if (*p == '\n') {
				  	fprintf(stderr, "Jason: Unterminated string (%s:%d)\n",path, line);
					state = CCL_HANDLE_NEWLINE;
				} else {
				  	if(t < tok_limit)
				  		*t++ = *p;
					p++;
				}
				break;
			case CCL_PARSE_UNQUOTED:		/* Parse unquoted strings */
				if (*p == data->comment_char) {
				  	state = CCL_PARSE_COMMENT;
				  	++p;
				} else if (*p == '\n') {
				  	state = CCL_HANDLE_NEWLINE;
				} else if (*p == data->sep_char) {
				  	state = CCL_HANDLE_SEP;
				  	//++p;
				} else if (isspace(*p)) {		/* In this mode a space ends the current token */
					state = CCL_PARSE_INITIAL;
					++p;
				} else {
					if(t < tok_limit)
				  		*t++ = *p;
					p++;
				}
				break;
			case CCL_HANDLE_SEP:			/* Process separator characters */
				if (pair) {					/* in case of key==value */
					if(*p == '\n')
						state = CCL_HANDLE_NEWLINE;
					else
						p++;
				} else {
					if (t == tok) {
					  	pair = NULL;
					  	fprintf(stderr, "Jason: Missing key (%s:%d)%p %p %s\n", path, line, t, tok, tok);
					} else {
						pair = (struct ccl_pair*) malloc(sizeof(struct ccl_pair));
						if (pair == NULL) {
					    	result = ENOMEM;
					    	goto cleanup;
						}
						*t = '\0';
						pair->key = strdup(tok);
						pair->value = NULL;
						if(pair->key == NULL) {
							result = ENOMEM;
							goto cleanup;
						}
						t = tok;
					}
					state = CCL_PARSE_INITIAL;
					++p;
				}
				break;
			case CCL_HANDLE_NEWLINE:		/* Process newlines */
				if (pair != NULL) {
				  	if(t > tok) {
						*t = '\0';
				    	pair->value = strdup(tok);
				  	} else {
				    	pair->value = strdup(""); 	/* In this case we read a key but no value */
				  	}
				  	if (pair->value == NULL) {
				    	result = ENOMEM;
				    	goto cleanup;
				  	}
				  	if ((dup = bst_insert(data->table, &pair->node)) != &pair->node) {
						fprintf(stderr, "Jason: duplicate key '%s' (%s:%d)\n", pair->key, path, line);
				    	ccl_free_pair(pair);
				  	}
					pair = NULL;
				}
				t = tok;
				state = CCL_PARSE_INITIAL;
				++line;
				++p;
				break;
			} 
		}    
	} while (feof(f) == 0);
	if (pair) {
		if (t > tok) {	
			*t = '\0';
    		pair->value = strdup(tok);
  		} else {
    		pair->value = strdup("");
  		}
  		if (pair->value == NULL) {
    		result = ENOMEM;
			goto cleanup;
  		}
		if ((dup = bst_insert(data->table, &pair->node)) != &pair->node) {
			fprintf(stderr, "Jason: duplicate key '%s' (%s:%d)\n", pair->key, path, line);
	    	ccl_free_pair(pair);
	  	}
		pair = NULL;
	}
  cleanup:
  	ccl_free_pair(pair);
	fclose(f);
	return result;
}

void ccl_release(struct ccl_t *data)
{
	if (data == NULL)
		return;
	if (data->table) {
		bst_destroy(data->table, ccl_destroyer, 1);
		data->table = NULL;
	}
	if (data->iterator) {
		bst_iterator_free(data->iterator);
		data->iterator = NULL;
	}
}
const struct ccl_pair * ccl_iterator(struct ccl_t *data)
{
	struct ccl_pair *pair = NULL;
	struct bst_node *n;
	if (data->iterating) {
		n = bst_iterator_next(data->iterator);	
	}else {
		data->iterating = 1;
		n = bst_iterator_first(data->iterator, data->table);
	}
	if (n)
		pair = bst_entry(n, struct ccl_pair, node);
	return pair;
}
void ccl_reset(struct ccl_t *data)
{
	if (data != 0) {
		data->iterating = 0;
	}
}
const char* ccl_get(const struct ccl_t *data, const char *key)
{
	const struct ccl_pair 	*pair;
	//struct ccl_pair 		temp;
	//temp.key = (char*) key;
	//temp.value = NULL;
	struct bst_node *n = bst_search(data->table, key);
	pair = bst_entry(n, struct ccl_pair, node);

	if (pair == NULL)
		return NULL;
	return pair->value;
}
int ccl_set(const struct ccl_t *data, const char *key, const char *value)
{
	struct ccl_pair *pair;
	char *rp;
	struct bst_node *n = bst_search(data->table, key);
	pair = bst_entry(n, struct ccl_pair, node);
	if (pair == NULL)
		return -1;
	if (0 == strcmp(pair->value, value))
		return 0;
	rp = strdup((char*) value);
	if (rp == NULL)
		return -1;
	free(pair->value);
	pair->value = rp;
	return 1;
}
int ccl_add(const struct ccl_t *data, const char *key, const char *value)
{
	struct ccl_pair *pair;
	struct bst_node *dup;
	pair = (struct ccl_pair*) malloc(sizeof(struct ccl_pair));
	if (!pair)
		return 0;
	pair->key = strdup((char*) key);
	if (!pair->key)
		goto cleanup;
	pair->value = strdup((char*) value);
	if (!pair->value)
		goto cleanup;
	if ((dup = bst_insert(data->table, &pair->node)) && dup == &pair->node)
		return 1;
cleanup:
  	ccl_free_pair(pair);
	return 0;
}
