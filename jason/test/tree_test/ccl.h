#ifndef __CCL_H__
#define __CCL_H__

#include "bst.h"

struct ccl_pair
{
	/* please always put |key| the first member*/
	char *key;
	char *value;
	struct bst_node node;
};

/*
 * Data structure encapsulating a parsed configuration file 
 * In the file:
 *  - comments begin with comment_char and continue to a newline
 *  - Keys are separated from values by sep_char
 *  - String literals enclosed in str_char may contain any character,
 * including spaces.
 */
struct ccl_t
{
	char comment_char;	/* Character that indicates the start of a comment */
	char sep_char;		/* Character that separates keys from values */
	char str_char;		/* Character that delineates string literals */

	/* Implementation details below, subject to change */

	/* The parsed file data, stored as a binary tree*/
	struct bst_table *table;

	/* The table iterator */
	struct bst_iterator *iterator;

	/* Currently traversing? */
	int iterating;
};

/*
 * Parse a configuration file
 *
 * This function will attempt to parse the configuration file path, using the comment, separator, and quote characters specified in data.
 * This function allocates memory; use ccl_release to clean up.
 * param data The ccl_t in which to store the parsed data
 * param path The file to parse
 * return 0 if successful, nonzero otherwise
 */
int ccl_parse(struct ccl_t *data, const char *path);

/*
 * Release memory associated with a configuration file
 * This function frees any dynamically-allocated memory in data.
 * param data The ccl_t that is no longer needed
 */
void ccl_release(struct ccl_t *data);

/*
 * Extract a value from a configuration file
 * This function searches the parsed configuration file data for key, and returns the value associated with that key.
 * If key was found in the configuration file, the returned value will never be 0
 * The value returned belongs to ccl, and should not be free'd.
 * param data The ccl_t to query
 * param key The key to query
 * return The value associated with key, or NULL if key was not found
 */
const char* ccl_get(const struct ccl_t *data, const char *key);

/*
 * Iterate through all key/value pairs in a configuration file
 * This function allows iteration through all key/value pairs in a configuration file.
 * This function maintains internal state; to reset the iterator call ccl_reset
 * The value returned belongs to ccl, and should not be free'd.<br>
 * param data The ccl_t to query
 * return A key/value pair, or NULL if no more exist in data
 */
const struct ccl_pair* ccl_iterator(struct ccl_t *data);

/*
 * Reset a configuration file iterator
 * This function resets the internal iterator in data to the first key/value pair.
 * param data The ccl_t to reset
 */
void ccl_reset(struct ccl_t *data);

  /*
   * Set a value in a configuration file
   * This function sets a value in data as though it were read from a configuration file.
   * param data The ccl_t to which the key/value pair should be addded.
   * param key The key
   * param value The value
   * return -1, 0, 1
   */
int ccl_set(const struct ccl_t *data, const char *key, const char *value);
#endif

