#ifndef __BST_H__
#define __BST_H__

#include <stddef.h>
#define bst_entry(ptr, type, member)   \
	((type *)((char *)(ptr)-(char *)(&((type *)0)->member)))

/* A binary search tree node. */
struct bst_node
{
	struct bst_node *bst_link[2];   /* Subtrees. */
};
/* BST iterator structure. */
struct bst_iterator
{
	struct bst_table *bst_table;        		/* Tree being traversed. */
	struct bst_node *bst_node;          		/* Current node in tree. */
	struct bst_node **bst_stack; 				/* All the nodes above |bst_node|. */
	size_t bst_height;                  		/* Number of nodes in |bst_parent|. */
	unsigned long bst_generation;       		/* Generation number. */
};

/* Function types. */
typedef int bst_comparator (const struct bst_node *n1, const struct bst_node *n2);
typedef int bst_searcher(const void *, const struct bst_node *n);
typedef void bst_destroyer (struct bst_node *n);
typedef void bst_printer (const struct bst_node *n);
/* Tree data structure. */
struct bst_table
{
	struct bst_node *bst_root;          /* Tree's root. */
	bst_comparator 	*bst_compare;   	/* Comparison function. */
	bst_searcher 	*bst_search;
	bst_printer 	*bst_print;
	size_t 			 bst_count;         /* Number of items in tree. */
	unsigned int 	 bst_max_height;	/* Max height of this tree */
	unsigned long 	 bst_generation;    /* Generation number. */
};
/* Table functions. */
struct bst_table *bst_create(bst_comparator *comparator, bst_searcher *searcher, bst_printer *printer, unsigned int height);
void   bst_destroy(struct bst_table *tree, bst_destroyer *destroyer, int cleanall);
struct bst_node *bst_insert (struct bst_table *tree, struct bst_node *n);
struct bst_node *bst_delete(struct bst_table *tree, const void *item);
struct bst_node *bst_search (const struct bst_table *tree, const void *item);

#define bst_count(table) ((size_t) (table)->bst_count)

/* Table bst_iterator functions. */
struct bst_iterator * bst_iterator_init(struct bst_table *tree);
struct bst_node * bst_iterator_first(struct bst_iterator *iterator, struct bst_table *tree);
struct bst_node * bst_iterator_last(struct bst_iterator *iterator, struct bst_table *tree);
struct bst_node * bst_iterator_find(struct bst_iterator *iterator, struct bst_table *tree, void *item);
struct bst_node * bst_iterator_insert(struct bst_iterator *iterator, struct bst_table *tree, struct bst_node *n);
struct bst_node * bst_iterator_next(struct bst_iterator *iterator);
struct bst_node * bst_iterator_prev(struct bst_iterator *iterator);
struct bst_node * bst_iterator_cur(struct bst_iterator *iterator);
void   bst_iterator_free(struct bst_iterator *iterator);
void bst_balance (struct bst_table *tree);

#endif

