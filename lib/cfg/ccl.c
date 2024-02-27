/*
 * Copyright (c) 2018, <-Jason Chen->
 * Author: Jie Chen <jasonchen@163.com>
 *
 * Date : 2018/12/13
 *
 * Description : the implementation of customizable configuration
 *
 */
#include <stdlib.h> 
#include <stdio.h>
#include <string.h>	
#include <ctype.h>	
#include <errno.h>
#include "ccl.h"
#define CCL_BST_HEIGHT	8
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
static int ccl_comparator(const struct bst_node *n1, const struct bst_node *n2)
{
	const struct ccl_pair *a = bst_entry(n1, struct ccl_pair, node);
	const struct ccl_pair *b = bst_entry(n2, struct ccl_pair, node);
	//return strcmp(a->key, b->key);
	return atoi(a->key) - atoi(b->key);
}
static int ccl_searcher(const void *item, const struct bst_node *n)
{
	const struct ccl_pair *entry = bst_entry(n, struct ccl_pair, node);
	const char *key = (const char *)item;
	//return strcmp(key, entry->key);
	return atoi(key) - atoi(entry->key);
}
static void ccl_printer(const struct bst_node *n)
{
	const struct ccl_pair *entry = bst_entry(n, struct ccl_pair, node);
	printf("%s\n", entry->key);
}

static void ccl_destroyer(struct bst_node *n)
{
	struct ccl_pair *pair = bst_entry(n, struct ccl_pair, node);
	free(pair->key);
	free(pair->value);
	free(pair);
}
int ccl_parse(struct ccl_t *data, const char *path)
{
	FILE *f = NULL;
	char buf[CCL_BUFSIZE];
	char tok[CCL_TOKSIZE];
	char *p;
	char *t = tok; 
	char *tok_limit;
	int result = ENOMEM;
	int state = CCL_PARSE_INITIAL;
	int count, line = 1;
	struct ccl_pair *pair = NULL;
	if (data == NULL || path == NULL)
		return EINVAL;

	data->table 	= NULL;
	data->iterator  = NULL;
	
	f = fopen(path, "r");
	if (f == NULL) {
		fprintf(stderr, "ccl: Unable to open '%s'\n", path);
		return ENOENT;
	}
	
	data->table = bst_create(ccl_comparator, ccl_searcher, ccl_printer, CCL_BST_HEIGHT);
	if (data->table == NULL) {
		goto cleanup;
	}
	data->iterator = bst_iterator_init(data->table);

	if (data->iterator == NULL) {
		goto cleanup;
	}
	data->iterating = 0;
	
	tok_limit = tok + (CCL_TOKSIZE - 1);
	do {
    	count = fread(buf, sizeof(char), CCL_BUFSIZE, f);
		for (p = buf; p < buf + count;) {
			switch(state) {
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
				  	fprintf(stderr, "ccl: Unterminated string (%s:%d)\n", path, line);
					state = CCL_HANDLE_NEWLINE;
				} else {
				  	if (t < tok_limit)
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
					if (t < tok_limit)
				  		*t++ = *p;
					p++;
				}
				break;
			case CCL_HANDLE_SEP:			/* Process separator characters */
				if (pair) {					/* in case of key==value */
					if (*p == '\n')
						state = CCL_HANDLE_NEWLINE;
					else
						p++;
				} else {
					if (t > tok) {
						pair = (struct ccl_pair*) malloc(sizeof(struct ccl_pair));
						if (pair == NULL) {
					    	goto cleanup;
						}
						*t = '\0';
						pair->key = strdup(tok);
						pair->value = NULL;
						if (pair->key == NULL) {
							goto cleanup;
						}
						t = tok;
					} else
						fprintf(stderr, "ccl: Missing key (%s:%d)%p %p %s\n", path, line, t, tok, tok);
					state = CCL_PARSE_INITIAL;
					++p;
				}
				break;
			case CCL_HANDLE_NEWLINE:		/* Process newlines */
				if (pair != NULL) {
					*t = '\0'; /* t == tok, In this case we read a key but no value: pair->value = strdup("") */
					pair->value = strdup(tok);
				  	if (pair->value == NULL) {
				    	goto cleanup;
				  	}
					if (bst_insert(data->table, &pair->node) != &pair->node) {
						fprintf(stderr, "ccl: duplicate key '%s' (%s:%d)\n", pair->key, path, line);
						ccl_free_pair(pair);
					}
					pair = NULL;
				} else if (t > tok) {
					*t = '\0';
					fprintf(stderr, "ccl: Format invalid(%s:%d)%p %p %s\n", path, line, t, tok, tok);
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
		*t = '\0';
		pair->value = strdup(tok);
  		if (pair->value == NULL) {
			goto cleanup;
  		}
		if (bst_insert(data->table, &pair->node) != &pair->node) {
			fprintf(stderr, "ccl: duplicate key '%s' (%s:%d)\n", pair->key, path, line);
			ccl_free_pair(pair);
		}
	}
	fclose(f);
	return 0;
  cleanup:
  	ccl_free_pair(pair);
	if (f)
		fclose(f);
	if (data->table) {
		bst_destroy(data->table, ccl_destroyer, 1);
		data->table = NULL;
	}
	if (data->iterator) {
		bst_iterator_free(data->iterator);
		data->iterator = NULL;
	}
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
	} else {
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
	struct bst_node *n = bst_search(data->table, key);

	if (n == NULL)
		return NULL;

	const struct ccl_pair *pair = bst_entry(n, struct ccl_pair, node);
	return pair->value;
}
int ccl_set(const struct ccl_t *data, const char *key, const char *value)
{
	struct bst_node *n = bst_search(data->table, key);
	if (n == NULL)
		return -1;

	struct ccl_pair *pair = bst_entry(n, struct ccl_pair, node);
	if (0 == strcmp(pair->value, value))
		return 0;
	char *newval = strdup(value);
	if (newval == NULL)
		return -1;
	free(pair->value);
	pair->value = newval;
	return 1;
}
int ccl_add(const struct ccl_t *data, const char *key, const char *value)
{
	struct ccl_pair *pair;
	pair = (struct ccl_pair*) malloc(sizeof(struct ccl_pair));
	if (!pair)
		return 0;
	pair->key = strdup(key);
	if (!pair->key)
		goto cleanup;
	pair->value = strdup(value);
	if (!pair->value)
		goto cleanup;
	if (bst_insert(data->table, &pair->node) == &pair->node)
		return 1;
cleanup:
  	ccl_free_pair(pair);
	return 0;
}
