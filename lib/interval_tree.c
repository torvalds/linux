#include <linux/init.h>
#include <linux/interval_tree.h>

#define ITSTRUCT   struct interval_tree_node
#define ITRB       rb
#define ITTYPE     unsigned long
#define ITSUBTREE  __subtree_last
#define ITSTART(n) ((n)->start)
#define ITLAST(n)  ((n)->last)
#define ITSTATIC
#define ITPREFIX   interval_tree

#include <linux/interval_tree_tmpl.h>
