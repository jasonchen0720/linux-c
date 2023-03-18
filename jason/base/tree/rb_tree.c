/*
 * Copyright (c) 2021, <-Jason Chen->
 * Author: Jie Chen <jasonchen@163.com>
 * Date  : 2021/02/20
 * Description : the implementation of red-black tree
 */

#include <stdio.h>
#include <stdlib.h>
#include "rb_tree.h"



/* type : 0 root, 1 left, 2 right. level */
static void print_tree(struct rb_node *n, 
		void (*printer)(const struct rb_node *), int type,  int level)
{
	int i;
	if (n)
		print_tree(n->right, printer, 2, level + 1);
	switch (type) {
	case 0:
		printer(n);
		break;
	case 1:
		for (i = 0; i < level; i++)	
			printf("\t");
		printf("\\ ");
		printer(n);
		break;
	case 2:
		for (i = 0; i < level; i++)	
			printf("\t");
		printf("/ ");
		printer(n);
		break;	
	}
	if (n)
		print_tree(n->left, printer, 1,  level + 1);
}
void rb_print(struct rb_tree *tree)
{
	printf("--------------------------------------------------------------------------\n");
	printf("RB tree count:%lu\n", tree->rb_count);
	print_tree(tree->root, tree->printer, 0, 0);
	printf("--------------------------------------------------------------------------\n");
}
static void rb_rotate_left(struct rb_node *node, struct rb_tree *tree)
{
	/*
	 * Suppose struct rb_node *node -> N.
	 *       P                      P
	 *       |                      |
	 *       N                      X
	 *    /     \                 /   \
	 *   A       X     --->      N     Z?
	 *  / \     / \            /   \
	 * B?  C?  Y?  Z?         A     Y?
	 *                       / \
	 *                      B?  C? 
	 */
	struct rb_node *parent = node->parent;
	struct rb_node *right  = node->right;

	/* <pair> */
	node->right = right->left;
	if (right->left)
		right->left->parent = node;

	/* <pair> */
	right->left = node;
	node->parent = right;

	/* <pair> */
	if (parent) {
		if (parent->left == node)
			parent->left = right;
		else
			parent->right = right;
	} else {
		tree->root = right;
	}
	right->parent = parent;
}
static void rb_rotate_right(struct rb_node *node, struct rb_tree *tree)
{
	/*
	 * Suppose struct rb_node *node -> N.
	 *        P                       P
	 *        |                       |
	 *        N                       A
	 *     /     \                  /   \
	 *    A       X     --->       B?    N
	 *  /   \    /  \                   /  \
	 * B?   C?  Y?  Z?                 C?    X
	 *                                     /   \
	 *                                     Y?  Z?
	 */
	struct rb_node *parent = node->parent;
	struct rb_node *left  = node->left;

	/* <pair> */
	node->left = left->right;
	if (left->right)
		left->right->parent = node;

	/* <pair> */
	left->right = node;
	node->parent = left;

	
	/* <pair> */
	if (parent) {
		if (parent->left == node)
			parent->left = left;
		else
			parent->right = left;
	} else {
		tree->root = left;
	}
	left->parent = parent;
}
void rb_insert_color(struct rb_node *node, struct rb_tree *tree)
{
	struct rb_node *uncle;
	struct rb_node *parent;
	struct rb_node *grandparent;
	//print_entry(node, "Insert color");
	for (parent = node->parent; rb_is_red_safe(parent); parent = node->parent) {
		grandparent = parent->parent;
		if (parent == grandparent->left) {
			uncle = grandparent->right;
			/* 
			 * case 1A: uncle is red.
			 *
			 * 			  G(b)                G(r) <-(n)
			 *	   		 /    \              /    \
			 *	  	    P(r)  U(r)   --->   P(b)  U(b)
			 *     	    |				    |
			 *    (n)-> N(r)			    N(r)
			 */
			if (rb_is_red_safe(uncle)) {
				rb_set_black(parent);
				rb_set_black(uncle);
				rb_set_red(grandparent);
				node = grandparent;
				continue;
			}
			/* 
			 * case 2A: uncle is black and (parent - node : L - R).
			 *
			 *          G(b)                   G(b)
			 *	   	   /    \                 /    \
			 *	  	  P(r)  U(B)    --->     N(r)  U(B)
			 *     	    \				    /
			 *    (n)-> N(r)         (n)-> P(r)
			 */
			if (node == parent->right) {
				struct rb_node *tmp;
				rb_rotate_left(parent, tree);
				tmp = node;
				node = parent;
				parent = tmp;
			}
			/* 
			 * case 3A: uncle is black and (parent - node : L - L).
			 *
			 *           G(b)                     P(b)
			 *	   	    /    \                   /    \
			 *	  	   P(r)  U(B)   --->  (n)-> N(r)  G(r)
			 *     	  /				                    \
			 * (n)-> N(r)                   		    U(B)
			 */
			rb_set_red(grandparent);
			rb_set_black(parent);
			rb_rotate_right(grandparent, tree);
			return;
		} else {
			uncle = grandparent->left;
			/* 
			 * case 1B: uncle is red.
			 *
			 * 		 G(b)                     G(r) <-(n)
			 *	   	/    \                   /    \
			 *	   U(r)  P(r)      --->     U(b)   P(b)
			 *     	      |				            |
			 *           N(r) <-(n)		           N(r)
			 */
			if (rb_is_red_safe(uncle)) {
				rb_set_black(parent);
				rb_set_black(uncle);
				rb_set_red(grandparent);
				node = grandparent;
				continue;
			}
			/* 
			 * case 2B: uncle is black and (parent - node : R - L).
			 *
			 *          G(b)                   G(b)
			 *	   	   /    \                 /    \
			 *	  	  U(B)  P(r)    --->     U(B)  N(r)
			 *     	       /				         \
			 *      (n)-> N(r)                 (n)-> P(r)
			 */
			if (node == parent->left) {
				struct rb_node *tmp;
				rb_rotate_right(parent, tree);
				tmp = node;
				node = parent;
				parent = tmp;
			}
			/* 
			 * case 3B: uncle is black and (parent - node : R - R).
			 *
			 *           G(b)                P(b)
			 *	   	    /    \              /    \
			 *	  	  U(B)  P(r)   --->   G(r)  N(r) <-(n)
			 *     	           \		  /
			 *            (n)-> N(r)     U(B)
			 */
			rb_set_red(grandparent);
			rb_set_black(parent);
			rb_rotate_left(grandparent, tree);
			return;
		}
	}
	/* Anyway, set the root black */
	rb_set_black(tree->root);
}
struct rb_node * rb_insert(struct rb_node *node, struct rb_tree *tree)
{
	struct rb_node **link;
	struct rb_node *parent = NULL;            
	int cmp;
  	for (link = &tree->root; *link != NULL; link = cmp > 0 ? &(*link)->right : &(*link)->left) {
		//print_entry(*link, "link");
      	cmp = tree->comparator(node, *link);
      	if (cmp == 0)
        	return *link;
		parent = *link;
    }
	node->color = RB_RED;
  	node->left 	= NULL;
	node->right = NULL;
	node->parent = parent;
	*link = node;
	rb_insert_color(node, tree);
	tree->rb_count++;
	//rb_print(tree);
  	return node;
}

void rb_remove_color(struct rb_node *node, struct rb_node *parent, struct rb_tree *tree)
{
	/* 
	 * NR: right nephew
	 * NL: left nephew  
	 * P : parent  
	 * S : silbing  
	 * N : current node
	 *
	 *(r): red
	 *(b): black
	 *(B): black or nil
	 *(*): black or red
	 */
	struct rb_node *silbing;
	//struct rb_node *nephew;
	while (rb_is_black(node) && node != tree->root) {
		if (parent->left == node) {
			silbing = parent->right;
			/* 
			 * case 1A: silbing is red.This case, Nephews must be present and black.
			 * convert to case 2A
			 * 			  P(b)                       S(b)
			 *	   		 /    \                     /    \
			 *	 (n)-> N(B)   S(r)     --->       P(r)   NR(b)   
			 *     	         /	  \		         /	  \    
			 *              NL(b) NR(b)  (n)-> N(B)   NL(b)
			 */
			if (rb_is_red(silbing)) {
				//printf("node: %p, silbing->left: %p, silbing->right: %p.\n", node, silbing->left, silbing->right);
				rb_set_red(parent);
				rb_set_black(silbing);
				rb_rotate_left(parent, tree);
				silbing = parent->right;
			}
			/* case 2A: silbing is black, nephews are black.
			 *
			 * 			  P(*)                 (n)-> P(*)
			 *	   		 /    \                     /    \
			 *	 (n)-> N(B)    S(b)      --->     N(B)    S(r)
			 *     	          /	   \		             /	  \    
			 *              NL(B)  NR(B)	           NL(B)  NR(B)
			 */
			if (rb_is_black(silbing->left) && rb_is_black(silbing->right)) {
				rb_set_red(silbing);
				node = parent;
				parent = node->parent;
				continue;
			}
			/* case 3A: silbing is black, left nephew is red.
			 * convert to case 4A
			 *
			 * 			  P(*)                      P(*)                    
			 *	   		 /    \                    /    \  
			 *	 (n)-> N(B)    S(b)   --->  (n)-> N(B)  NL(b) 
			 *     	          /	   \		              \     
			 *              NL(r)  NR(B)	               S(r) 
			 *                                              \
			 *                                              NR(B)
			 */
			if (rb_is_black(silbing->right)) {
				rb_set_red(silbing);
				rb_set_black(silbing->left);
				rb_rotate_right(silbing, tree);
				silbing = parent->right;
			}
			/* case 4A: silbing is black, right nephew is red.
			 *
			 * 		 P(*)                   P(b)                    S(*)                     
			 *	   	/    \                 /    \                  /    \                
			 *	  N(B)   S(b)    --->    N(B)  S(*)     --->      P(b)   NR(b) 
			 *     	     /	  \		            /	\            /    \           
			 *         NL(B) NR(r)	          NL(B) NR(b)      N(B) NL(B)            
			 *                                              
			 *                                              
			 */
			parent->color  = parent->color ^ silbing->color;
			silbing->color = parent->color ^ silbing->color;
			parent->color  = parent->color ^ silbing->color;
			rb_set_black(silbing->right);
			rb_rotate_left(parent, tree);
			node = tree->root;
			break;
		 } else {
			silbing = parent->left;
			/*
			 * case 1B: silbing is red.This case, Nephews must be present and black.
			 * convert to case 2B
			 * 			  P(b)                       S(b)
			 *	   		 /    \                     /    \
			 *	       S(r)   N(B)     --->      NL(b)   P(r)   
			 *     	  /	   \		                     /	  \    
			 *       NL(b) NR(b)                        NR(b) N(B)
			 */
			if (rb_is_red(silbing)) {
				//printf("node: %p, silbing->left: %p, silbing->right: %p.\n", node, silbing->left, silbing->right);
				rb_set_black(silbing);
				rb_set_red(parent);
				rb_rotate_right(parent, tree);
				silbing = parent->left;
			}
			/* case 2B: silbing is black, nephews are black.
			 *
			 * 		  P(*)                        P(*) <-(n)
			 *	     /    \                      /    \
			 *	   S(b)   N(B) <-(n)   --->     S(r)   N(B)
			 *    /	   \		               /    \    
			 *  NL(B)  NR(B)	              NL(B) NR(B)
			 */
			if (rb_is_black(silbing->left) && rb_is_black(silbing->right)) {
				rb_set_red(silbing);
				node = parent;
				parent = node->parent;
				continue;
			}
			/* case 3B: silbing is black, right nephew is red.
			 * convert to case 4B
			 *
			 * 			  P(*)                   P(*)                    
			 *	   		 /    \                 /    \  
			 *	       S(b)   N(B)   --->     NR(b) N(B) 
			 *     	  /	   \		          /     
			 *       NL(B) NR(r)	         S(r) 
			 *                              /           
			 *                             NL(B)  
			 */
			if (rb_is_black(silbing->left)) {
				rb_set_red(silbing);
				rb_set_black(silbing->right);
				rb_rotate_left(silbing, tree);
				silbing = parent->left;
			}
			/* case 4B: silbing is black, left nephew is red.
			 *
			 * 			  P(*)                 P(b)                S(*)                     
			 *	   		 /    \               /    \              /    \                
			 *	       S(b)   N(B)  --->   S(*)    N(B)  --->  NL(b)  P(b) 
			 *     	  /   \		           /    \                      /    \       
			 *       NL(r) NR(B)	      NL(b) NR(B)               NR(B) N(B)           
			 *                                              
			 *                                              
			 */
			parent->color  = parent->color ^ silbing->color;
			silbing->color = parent->color ^ silbing->color;
			parent->color  = parent->color ^ silbing->color;
			rb_set_black(silbing->left);
			rb_rotate_right(parent, tree);
			node = tree->root;
			break;
		}
	}
	if (node)
		rb_set_black(node);
}
void rb_remove(struct rb_node *node, struct rb_tree *tree)
{
	struct rb_node *child;
	struct rb_node *parent;
	if (!node->left)
		child = node->right;
	else if (!node->right)
		child = node->left;
	else {
		struct rb_node *replace = node->right;
		while (replace->left)
			replace = replace->left;
		/* backup the child and parent of replace node */
		child = replace->right;
		parent = replace->parent;
		/* 
		 * Update the child of the parent of removing node.
		 * If the parent of removing node is null, 
		 * this indicates the removing node is root node, set the replace node as the new root node.
		 */
		/* <pair> : replace->parent */
		if (node->parent) {
			if (node->parent->left == node)
				node->parent->left = replace;
			else
				node->parent->right = replace;
		} else
			tree->root = replace;
		replace->parent = node->parent;
		
		/* <pair> : replace->left */
		replace->left = node->left;
		node->left->parent = replace;

		
		if (parent == node)
			parent = replace;
		else {
			/* delete and link the child to the new parent */
			if (child)
				child->parent = parent;
			parent->left = child;
			
			/* <pair>  : replace->right */
			replace->right = node->right;
			node->right->parent = replace;
		}
		/* swap the color */
		replace->color = replace->color ^ node->color;
		node->color    = replace->color ^ node->color;
		replace->color = replace->color ^ node->color;
		goto color_fix;
	}
	parent = node->parent;
	if (child)
		child->parent = parent;
	if (parent) {
		if (parent->left == node)
			parent->left = child;
		else
			parent->right = child;
	} else
		tree->root = child;
color_fix:
	if (node->color == RB_BLACK)
		rb_remove_color(child, parent, tree);
	tree->rb_count--;
	//rb_print(tree);
}
struct rb_node *rb_search(void *item, struct rb_tree *tree)
{
	int cmp;
	struct rb_node *t;
  	for (t = tree->root; t != NULL; ) {
      	cmp = tree->searcher(item, t);
      	if (cmp < 0)
        	t = t->left;
      	else if (cmp > 0)
        	t = t->right;
      	else
        	return t;
	}
  	return NULL;
}
struct rb_node *rb_first(const struct rb_tree *tree)
{
    struct rb_node *n;

    n = tree->root;
    if (!n)
        return NULL;
    while (n->left)
        n = n->left;
    return n;
}
struct rb_node *rb_last(const struct rb_tree *tree)
{
    struct rb_node *n;

    n = tree->root;
    if (!n)
        return NULL;
    while (n->right)
        n = n->right;
    return n;
}
struct rb_node *rb_next(struct rb_node *node)
{
	if (node->right) {
		node = node->right;
		while (node->left)
			node = node->left;
		return node;
	}

	while (node->parent && node->parent->right == node)
		node = node->parent;

	return node->parent;
}


struct rb_node *rb_prev(struct rb_node *node)
{
	if (node->left) {
		node = node->left;
		while (node->right)
			node = node->right;
		return node;
	}
	while (node->parent && node->parent->left == node)
		node = node->parent;

	return node->parent;
}
