/*
 * Copyright (c) 2021, <-Jason Chen->
 * Author: Jie Chen <jasonchen@163.com>
 * Date  : 2021/02/11
 * Description : the implementation of binary search tree
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bs_tree.h"

static void print_node(const struct bst_node *n)
{
	printf("%p\n",n);
}

/* type : 0 root, 1 left, 2 right. level */
static void print_tree(struct bst_node *n, void (*print_entry)(const struct bst_node *), int type,  int level)
{
	int i;

	if (NULL == n)
		return;

	print_tree(n->bst_link[1], print_entry, 2, level + 1);
	switch (type) {
	case 0:
		print_entry(n);
		break;
	case 1:
		for (i = 0; i < level; i++)	
			printf("\t");
		printf("\\ ");
		print_entry(n);
		break;
	case 2:
		for (i = 0; i < level; i++)	
			printf("\t");
		printf("/ ");
		print_entry(n);
		break;	
	}
	print_tree(n->bst_link[0], print_entry, 1, level + 1);
}
void bst_print_tree(struct bst_table *tree)
{
	printf("--------------------------------------------------------------------------\n");
	print_tree(tree->bst_root, tree->bst_print, 0, 0);
	printf("--------------------------------------------------------------------------\n");
}
#define bst_malloc  malloc
#define bst_free	free
/* 
 * @comparator: comparison function.
 * @searcher  : comparison function for searching.
 * @printer   : printer for a bst node.
 * @height    : max height of tree.
 * Returns the new table or Returns NULL if memory allocation failed
 */
struct bst_table *bst_create(bst_comparator *comparator, bst_searcher *searcher, bst_printer *printer, unsigned int height)
{
  	struct bst_table *tree;
  	tree = (struct bst_table *)bst_malloc(sizeof *tree);
  	if (tree == NULL)
    	return NULL;
	if (printer == NULL)
		printer = print_node;
  	tree->bst_root = NULL;
  	tree->bst_compare = comparator;
	tree->bst_search = searcher;
	tree->bst_print = printer;
  	tree->bst_count = 0;
  	tree->bst_generation = 0;
	tree->bst_max_height = height;
  	return tree;
}

/* Search @tree for a node matching @item, and return it if found, Otherwise return NULL */
struct bst_node * bst_search (const struct bst_table *tree, const void *item)
{
	int cmp;
	struct bst_node *t;
  	for (t = tree->bst_root; t != NULL; ) {
      	cmp = tree->bst_search(item, t);
      	if (cmp < 0)
        	t = t->bst_link[0];
      	else if (cmp > 0)
        	t = t->bst_link[1];
      	else
        	return t;
	}
  	return NULL;
}
/*
 * Insert a node into @tree, and return the duplicate node, If found, Otherwise return @n itself.
 * Will fail to insert only when duplicate node existed.
 */
struct bst_node *bst_insert (struct bst_table *tree, struct bst_node *n)
{
  	struct bst_node *t, *p;
  	int dir;                
	int cmp;
  	for (p = NULL, t = tree->bst_root; t != NULL; p = t, t = t->bst_link[dir]) {
      	cmp = tree->bst_compare(n, t);
      	if (cmp == 0)
        	return t;
      	dir = cmp > 0;
    }
  	tree->bst_count++;
  	n->bst_link[0] = NULL;
	n->bst_link[1] = NULL;
  	if (p != NULL)
    	p->bst_link[dir] = n;
  	else
    	tree->bst_root = n;
  	return n;
}
struct bst_node *bst_remove(struct bst_table *tree, struct bst_node *n, int self)
{
  	struct bst_node *d, *p; /* Node to delete and its parent. */
  	int cmp;                /* Comparison between |p->_data| and |item|. */
  	int dir;                /* Side of |q| on which |p| is located. */
  	d = (struct bst_node *)&tree->bst_root;
  	for (cmp = -1; cmp != 0; cmp = tree->bst_compare(n, d)) {
      	dir = cmp > 0;
      	p = d;
      	d = d->bst_link[dir];
      	if (d == NULL)
        	return NULL;
    }
	if (d == n || !self) {
	  	if (d->bst_link[1] == NULL)
	    	p->bst_link[dir] = d->bst_link[0];
		else {
	      	struct bst_node *r = d->bst_link[1];
	      	if (r->bst_link[0] == NULL) {
	          	r->bst_link[0] = d->bst_link[0];
	          	p->bst_link[dir] = r;
	        } else {
	          	struct bst_node *s;
	          	for (;;) {
	              	s = r->bst_link[0];
	              	if (s->bst_link[0] == NULL)
	                	break;
	              	r = s;
	            }
	          	r->bst_link[0] = s->bst_link[1];
	          	s->bst_link[0] = d->bst_link[0];
	          	s->bst_link[1] = d->bst_link[1];
	          	p->bst_link[dir] = s;
	        }
	    }
		tree->bst_count--;
		tree->bst_generation++;
	}
	return d;
}

/* Deletes from |tree| and returns an item matching |item|.
   Returns a null pointer if no matching item found. */
struct bst_node *bst_delete(struct bst_table *tree, const void *item)
{
  	struct bst_node *d, *p;
  	int cmp;
  	int dir;
  	d = (struct bst_node *) &tree->bst_root;
  	for (cmp = -1; cmp != 0; cmp = tree->bst_search(item, d)) {
      	dir = cmp > 0;
      	p = d;
      	d = d->bst_link[dir];
      	if (d == NULL)
        	return NULL;
    }
  	if (d->bst_link[1] == NULL)
    	p->bst_link[dir] = d->bst_link[0];
	else {
      	struct bst_node *r = d->bst_link[1];
      	if (r->bst_link[0] == NULL) {
          	r->bst_link[0] = d->bst_link[0];
          	p->bst_link[dir] = r;
        } else {
          	struct bst_node *s;
          	for (;;) {
              	s = r->bst_link[0];
              	if (s->bst_link[0] == NULL)
                	break;
              	r = s;
            }
          	r->bst_link[0] = s->bst_link[1];
          	s->bst_link[0] = d->bst_link[0];
          	s->bst_link[1] = d->bst_link[1];
          	p->bst_link[dir] = s;
        }
    }
	tree->bst_count--;
	tree->bst_generation++;
	return d;
}
/* Converts @tree into left slanted tree */
static void slant_left (struct bst_table *tree)
{
  	struct bst_node *q, *p;
  	q = (struct bst_node *) &tree->bst_root;
  	p = tree->bst_root;
  	while (p != NULL) {
	    if (p->bst_link[1] == NULL) {
			q = p;
			p = p->bst_link[0];
		} else {
			struct bst_node *r = p->bst_link[1];
			//tree->bst_printer(r);
			p->bst_link[1] = r->bst_link[0];
			r->bst_link[0] = p;
			p = r;
			q->bst_link[0] = r;
			//bst_print_tree(tree);
		}
  	}
}

/* Performs a compression transformation @count times, starting at root */
static void slant_left_back (struct bst_node *root, unsigned long count)
{
	//struct bst_table *tree = (struct bst_table *)root;
	while (count--) {
		struct bst_node *red = root->bst_link[0];
		//print_entry(red);
		struct bst_node *black = red->bst_link[0];
		root->bst_link[0] = black;
		red->bst_link[0] = black->bst_link[1];
		black->bst_link[1] = red;
		root = black;
		//bst_print_tree(tree);
    }	
}

/* Converts @tree, which must be in the shape of a vine, into a balanced tree */
static void slant_back (struct bst_table *tree)
{
  	unsigned long vine;   /* Number of nodes in main vine. */
  	unsigned long leaves; /* Nodes in incomplete bottom level, if any. */
  	int height;           /* Height of produced balanced tree. */
  	leaves = tree->bst_count + 1;
  	for (;;) {
	  	unsigned long next = leaves & (leaves - 1);
	  	if (next == 0)
	    	break;
	  	leaves = next;
	}
  	leaves = tree->bst_count + 1 - leaves;
	printf("count: %lu, leaves: %lu.\n", tree->bst_count, leaves);
  	slant_left_back ((struct bst_node *) &tree->bst_root, leaves);
	bst_print_tree(tree);
  	vine = tree->bst_count - leaves;
  	height = 1 + (leaves > 0);
  	while (vine > 1) {
		printf("vine: %lu\n", vine);
      	slant_left_back ((struct bst_node *) &tree->bst_root, vine >> 1);
      	vine >>= 1;
      	height++;
    }
  	if (height > tree->bst_max_height) {
      	fprintf (stderr, "Tree too big (%lu nodes) to handle\n",(unsigned long) tree->bst_count);
     	//exit(EXIT_FAILURE);
    }
}

/* 
 * Balances @tree.
 * Ensures that no simple path from the root to a leaf has more than @tree->bst_max_height nodes. 
 */
void bst_balance (struct bst_table *tree)
{
	bst_print_tree(tree);
	slant_left (tree);
	slant_back (tree);
	tree->bst_generation++;
	bst_print_tree(tree);
}

/* 
 * Frees storage allocated for @tree.
 * @destroyer: used to destroy each node in inorder.
 * @cleanall: if true, clean the destroy.
 */
void bst_destroy(struct bst_table *tree, bst_destroyer *destroyer, int cleanall)
{
	
  	struct bst_node *p, *q;
	assert (tree != NULL);
  	for (p = tree->bst_root; p != NULL; p = q) {
	    if (p->bst_link[0] == NULL) {
	        q = p->bst_link[1];
	        if (destroyer != NULL)
	          	destroyer(p);
		} else {
		    /*
			 *        p                    q             
			 *     /     \               /   \         
			 *    q       a      --->   y     p       
			 *  /   \    /  \                / \         
			 * y     z  b    c              z   a
			 *                                 / \
			 *                                b   c
			 */
	        q = p->bst_link[0];
	        p->bst_link[0] = q->bst_link[1];
	        q->bst_link[1] = p;
		}
  	}
	if (cleanall) {
  		bst_free(tree);
		return;
	}
	tree->bst_count = 0;
	tree->bst_generation = 0;
	tree->bst_root = NULL;
}
/* Refreshes the stack of parent pointers in @iterator and updates its generation number. */
static void bst_iterator_refresh (struct bst_iterator *iterator)
{
	iterator->bst_generation = iterator->bst_table->bst_generation;
	if (iterator->bst_node != NULL) {
		bst_comparator *cmp = iterator->bst_table->bst_compare;
		struct bst_node *node = iterator->bst_node;
		struct bst_node *i;
		iterator->bst_height = 0;
      	for (i = iterator->bst_table->bst_root; i != node; ) {
			assert (iterator->bst_height < iterator->bst_table->bst_max_height);
			assert (i != NULL);
          	iterator->bst_stack[iterator->bst_height++] = i;
          	i = i->bst_link[cmp(node, i) > 0];
        }
    }
}

/* Initializes @iterator for use with @tree and selects the null node. */
struct bst_iterator * bst_iterator_init(struct bst_table *tree)
{
	struct bst_iterator *iterator = (struct bst_iterator *)bst_malloc(sizeof(struct bst_iterator));
	if (!iterator)
		return NULL;
	iterator->bst_stack = (struct bst_node **)bst_malloc(tree->bst_max_height * sizeof(struct bst_node *));
	if (!iterator->bst_stack) {
		bst_free(iterator);
		return NULL;
	}	
	iterator->bst_table = tree;
	iterator->bst_node = NULL;
	iterator->bst_height = 0;
	iterator->bst_generation = tree->bst_generation;
	return iterator;
}
void bst_iterator_free(struct bst_iterator *iterator)
{
	if (iterator) {
		if (iterator->bst_stack)
			bst_free(iterator->bst_stack);
		bst_free(iterator);
	}
}

/* Initializes @iterator for @tree and selects and returns a pointer to its least-valued node.
   Returns NULL if @tree contains no nodes. */
struct bst_node * bst_iterator_first (struct bst_iterator *iterator, struct bst_table *tree)
{
	struct bst_node *x;
	iterator->bst_table = tree;
	iterator->bst_height = 0;
	iterator->bst_generation = tree->bst_generation;
	x = tree->bst_root;
	if (x != NULL) {
		while (x->bst_link[0] != NULL) {
			if (iterator->bst_height >= tree->bst_max_height) {
		    	bst_balance(tree);
		    	return bst_iterator_first(iterator, tree);
		  	}
			iterator->bst_stack[iterator->bst_height++] = x;
			x = x->bst_link[0];
		}
	}
	iterator->bst_node = x;
	return x;
}

/* Initializes @iterator for @tree and selects and returns a pointer to its greatest-valued node.
   Returns NULL if @tree contains no nodes. */
struct bst_node * bst_iterator_last (struct bst_iterator *iterator, struct bst_table *tree)
{
	struct bst_node *x;
	iterator->bst_table = tree;
	iterator->bst_height = 0;
	iterator->bst_generation = tree->bst_generation;
	x = tree->bst_root;
	if (x != NULL) {
		while (x->bst_link[1] != NULL) {
			if (iterator->bst_height >= tree->bst_max_height) {
			    bst_balance(tree);
			    return bst_iterator_last(iterator, tree);
			}
			iterator->bst_stack[iterator->bst_height++] = x;
			x = x->bst_link[1];
		}
	}
	iterator->bst_node = x;
	return x;
}

/* 
 * Searches for @item in @tree.
 * If found, initializes @iterator to the item found and returns the item as well.
 * If there is no matching node, initializes @iterator to the null node and returns NULL. 
 */
struct bst_node * bst_iterator_find (struct bst_iterator *iterator, struct bst_table *tree, void *item)
{
	int cmp;
	struct bst_node *p, *q;
	iterator->bst_table = tree;
	iterator->bst_height = 0;
	iterator->bst_generation = tree->bst_generation;
	for (p = tree->bst_root; p != NULL; p = q)
    {
		cmp = tree->bst_search(item, p);
		if (cmp < 0)
			q = p->bst_link[0];
		else if (cmp > 0)
			q = p->bst_link[1];
		else { /* |cmp == 0| */
          	iterator->bst_node = p;
          	return p;
        }
      	if (iterator->bst_height >= tree->bst_max_height) {
          	bst_balance(iterator->bst_table);
          	return bst_iterator_find(iterator, tree, item);
        }
      	iterator->bst_stack[iterator->bst_height++] = p;
    }
	iterator->bst_height = 0;
  	iterator->bst_node = NULL;
  	return NULL;
}

/*
 * Attempts to insert @n into @tree.
 * If @n is inserted successfully, it is returned and @iterator is initialized to its location.
 * If a duplicate is found, it is returned and @iterator is initialized to its location. 
 * No replacement of the item occurs. 
 */
struct bst_node * bst_iterator_insert(struct bst_iterator *iterator, struct bst_table *tree, struct bst_node *n)
{
	int cmp;
	struct bst_node **q;
	iterator->bst_table = tree;
	iterator->bst_height = 0;
	q = &tree->bst_root;
	while (*q != NULL) {
      	cmp = tree->bst_compare(n, (*q));
      	if (cmp == 0) {
          	iterator->bst_node = *q;
          	iterator->bst_generation = tree->bst_generation;
          	return (*q);
        }
      	if (iterator->bst_height >= tree->bst_max_height) {
          	bst_balance (tree);
          	return bst_iterator_insert(iterator, tree, n);
        }
      	iterator->bst_stack[iterator->bst_height++] = *q;
      	q = &(*q)->bst_link[cmp > 0];
    }
  	iterator->bst_node = *q = n;
  	(*q)->bst_link[0] = (*q)->bst_link[1] = NULL;
  	tree->bst_count++;
  	iterator->bst_generation = tree->bst_generation;
  	return (*q);
}
/* 
 * Returns the next data item in inorder within the tree being traversed with @iterator,
 * or if there are no more data items returns NULL. 
 */
struct bst_node *bst_iterator_next (struct bst_iterator *iterator)
{
	struct bst_node *x;
	if (iterator->bst_generation != iterator->bst_table->bst_generation)
		bst_iterator_refresh (iterator);

	x = iterator->bst_node;
	if (x == NULL) {
      	return bst_iterator_first (iterator, iterator->bst_table);
    } else if (x->bst_link[1] != NULL) {
    	//printf("height: %lu max_height: %u\n", iterator->bst_height, iterator->bst_table->bst_max_height);
		if (iterator->bst_height >= iterator->bst_table->bst_max_height) {
          	bst_balance(iterator->bst_table);
          	return bst_iterator_next(iterator);
        }
		iterator->bst_stack[iterator->bst_height++] = x;
		x = x->bst_link[1];
		while (x->bst_link[0] != NULL) {
			if (iterator->bst_height >= iterator->bst_table->bst_max_height) {
				bst_balance(iterator->bst_table);
				return bst_iterator_next(iterator);
            }
			iterator->bst_stack[iterator->bst_height++] = x;
			x = x->bst_link[0];
        }
    } else {
		struct bst_node *y;
		do {
			if (iterator->bst_height == 0) {
				iterator->bst_node = NULL;
				return NULL;
			}
			y = x;
			x = iterator->bst_stack[--iterator->bst_height];
        } while (y == x->bst_link[1]);
    }
	iterator->bst_node = x;
	return x;
}

/* 
 * Returns the previous node in inorder within the tree being traversed with @iterator,
 * or if there are no more node returns NULL. 
 */
struct bst_node *bst_iterator_prev (struct bst_iterator *iterator)
{
	struct bst_node *x;
	if (iterator->bst_generation != iterator->bst_table->bst_generation)
    	bst_iterator_refresh(iterator);
	x = iterator->bst_node;
  	if (x == NULL) {
      	return bst_iterator_last (iterator, iterator->bst_table);
    } else if (x->bst_link[0] != NULL) {
    	//printf("height: %lu max_height: %u\n", iterator->bst_height, iterator->bst_table->bst_max_height);
      	if (iterator->bst_height >= iterator->bst_table->bst_max_height) {
			bst_balance(iterator->bst_table);
			return bst_iterator_prev(iterator);
        }

		iterator->bst_stack[iterator->bst_height++] = x;
		x = x->bst_link[0];

		while (x->bst_link[1] != NULL) {
			if (iterator->bst_height >= iterator->bst_table->bst_max_height)
            {
				bst_balance(iterator->bst_table);
				return bst_iterator_prev(iterator);
            }
          	iterator->bst_stack[iterator->bst_height++] = x;
          	x = x->bst_link[1];
        }
    } else {
		struct bst_node *y;
      	do {
          	if (iterator->bst_height == 0) {
              iterator->bst_node = NULL;
              return NULL;
            }
          	y = x;
          	x = iterator->bst_stack[--iterator->bst_height];
        } while (y == x->bst_link[0]);
    }
  	iterator->bst_node = x;
  	return x;
}

/* Returns @iterator's current node. */
struct bst_node *bst_iterator_cur (struct bst_iterator *iterator)
{
	return iterator->bst_node;
}
