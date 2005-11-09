/*
 * proc_devtree.c - handles /proc/device-tree
 *
 * Copyright 1997 Paul Mackerras
 */
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <asm/prom.h>
#include <asm/uaccess.h>

#ifndef HAVE_ARCH_DEVTREE_FIXUPS
static inline void set_node_proc_entry(struct device_node *np,
				       struct proc_dir_entry *de)
{
}
#endif

static struct proc_dir_entry *proc_device_tree;

/*
 * Supply data on a read from /proc/device-tree/node/property.
 */
static int property_read_proc(char *page, char **start, off_t off,
			      int count, int *eof, void *data)
{
	struct property *pp = data;
	int n;

	if (off >= pp->length) {
		*eof = 1;
		return 0;
	}
	n = pp->length - off;
	if (n > count)
		n = count;
	else
		*eof = 1;
	memcpy(page, pp->value + off, n);
	*start = page;
	return n;
}

/*
 * For a node with a name like "gc@10", we make symlinks called "gc"
 * and "@10" to it.
 */

/*
 * Add a property to a node
 */
static struct proc_dir_entry *
__proc_device_tree_add_prop(struct proc_dir_entry *de, struct property *pp)
{
	struct proc_dir_entry *ent;

	/*
	 * Unfortunately proc_register puts each new entry
	 * at the beginning of the list.  So we rearrange them.
	 */
	ent = create_proc_read_entry(pp->name,
				     strncmp(pp->name, "security-", 9)
				     ? S_IRUGO : S_IRUSR, de,
				     property_read_proc, pp);
	if (ent == NULL)
		return NULL;

	if (!strncmp(pp->name, "security-", 9))
		ent->size = 0; /* don't leak number of password chars */
	else
		ent->size = pp->length;

	return ent;
}


void proc_device_tree_add_prop(struct proc_dir_entry *pde, struct property *prop)
{
	__proc_device_tree_add_prop(pde, prop);
}

/*
 * Process a node, adding entries for its children and its properties.
 */
void proc_device_tree_add_node(struct device_node *np,
			       struct proc_dir_entry *de)
{
	struct property *pp;
	struct proc_dir_entry *ent;
	struct device_node *child;
	const char *p;

	set_node_proc_entry(np, de);
	for (child = NULL; (child = of_get_next_child(np, child));) {
		p = strrchr(child->full_name, '/');
		if (!p)
			p = child->full_name;
		else
			++p;
		ent = proc_mkdir(p, de);
		if (ent == 0)
			break;
		proc_device_tree_add_node(child, ent);
	}
	of_node_put(child);
	for (pp = np->properties; pp != 0; pp = pp->next) {
		/*
		 * Yet another Apple device-tree bogosity: on some machines,
		 * they have properties & nodes with the same name. Those
		 * properties are quite unimportant for us though, thus we
		 * simply "skip" them here, but we do have to check.
		 */
		for (ent = de->subdir; ent != NULL; ent = ent->next)
			if (!strcmp(ent->name, pp->name))
				break;
		if (ent != NULL) {
			printk(KERN_WARNING "device-tree: property \"%s\" name"
			       " conflicts with node in %s\n", pp->name,
			       np->full_name);
			continue;
		}

		ent = __proc_device_tree_add_prop(de, pp);
		if (ent == 0)
			break;
	}
}

/*
 * Called on initialization to set up the /proc/device-tree subtree
 */
void proc_device_tree_init(void)
{
	struct device_node *root;
	if ( !have_of )
		return;
	proc_device_tree = proc_mkdir("device-tree", NULL);
	if (proc_device_tree == 0)
		return;
	root = of_find_node_by_path("/");
	if (root == 0) {
		printk(KERN_ERR "/proc/device-tree: can't find root\n");
		return;
	}
	proc_device_tree_add_node(root, proc_device_tree);
	of_node_put(root);
}
