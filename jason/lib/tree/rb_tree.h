#ifndef __RB_TREE_H__
#define __RB_TREE_H__
struct rb_node
{
	struct rb_node *parent;
	struct rb_node *left;
	struct rb_node *right;
	int color;
};
struct rb_tree
{
	struct rb_node *root;
	/* 
	 * comparator(A, B);
	 * A gt B, return positive;
	 * A eq B, return zero;
	 * A lt B, return negative;
	 */
	int (*comparator)(const struct rb_node *, const struct rb_node *);
	int (*searcher)(const void *, const struct rb_node *);
	void (*printer)(const struct rb_node *);
	unsigned long rb_count;
};
#define rb_tree_init(rbt, c, s, p) \
	do {\
		(rbt)->comparator	= (c);\
		(rbt)->searcher		= (s);\
		(rbt)->printer		= (p);\
		(rbt)->root			= NULL;\
		(rbt)->rb_count		= 0UL;\
	} while (0)
#define rb_tree_initializer(c, s, p) \
{ /* root */NULL, \
  /* comparator */c, \
  /* searcher */s, \
  /* printer */p, \
  /* handler */0UL, \
}
#define RB_RED		0
#define RB_BLACK	1
#define rb_is_red(n) 		((n)->color == RB_RED)
#define rb_is_red_safe(n)	 ((n) && (n)->color == RB_RED)
#define rb_is_black(n)		(!(n) || (n)->color == RB_BLACK)
#define rb_set_red(n)	do {(n)->color = RB_RED;} while (0)
#define rb_set_black(n)	do {(n)->color = RB_BLACK;} while (0)
#define rb_entry(ptr, type, member)   \
((type *)((char *)(ptr)-(char *)(&((type *)0)->member)))

#define rb_count(tree) ((tree)->rb_count)
void rb_print(struct rb_tree *tree);
struct rb_node * rb_insert(struct rb_node *node, struct rb_tree *tree);
void rb_remove(struct rb_node *node, struct rb_tree *tree);
struct rb_node *rb_search(void *item, struct rb_tree *tree);
struct rb_node *rb_first(const struct rb_tree *tree);
struct rb_node *rb_last(const struct rb_tree *tree);
struct rb_node *rb_next(struct rb_node *node);
struct rb_node *rb_prev(struct rb_node *node);
#endif