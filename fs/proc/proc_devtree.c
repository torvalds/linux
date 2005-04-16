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
static inline void set_node_proc_entry(struct device_node *np, struct proc_dir_entry *de)
{
}

static void inline set_node_name_link(struct device_node *np, struct proc_dir_entry *de)
{
}

static void inline set_node_addr_link(struct device_node *np, struct proc_dir_entry *de)
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
 * Process a node, adding entries for its children and its properties.
 */
void proc_device_tree_add_node(struct device_node *np, struct proc_dir_entry *de)
{
	struct property *pp;
	struct proc_dir_entry *ent;
	struct device_node *child, *sib;
	const char *p, *at;
	int l;
	struct proc_dir_entry *list, **lastp, *al;

	set_node_proc_entry(np, de);
	lastp = &list;
	for (pp = np->properties; pp != 0; pp = pp->next) {
		/*
		 * Unfortunately proc_register puts each new entry
		 * at the beginning of the list.  So we rearrange them.
		 */
		ent = create_proc_read_entry(pp->name, strncmp(pp->name, "security-", 9) ?
					     S_IRUGO : S_IRUSR, de, property_read_proc, pp);
		if (ent == 0)
			break;
		if (!strncmp(pp->name, "security-", 9))
		     ent->size = 0; /* don't leak number of password chars */
		else
		     ent->size = pp->length;
		*lastp = ent;
		lastp = &ent->next;
	}
	child = NULL;
	while ((child = of_get_next_child(np, child))) {
		p = strrchr(child->full_name, '/');
		if (!p)
			p = child->full_name;
		else
			++p;
		/* chop off '@0' if the name ends with that */
		l = strlen(p);
		if (l > 2 && p[l-2] == '@' && p[l-1] == '0')
			l -= 2;
		ent = proc_mkdir(p, de);
		if (ent == 0)
			break;
		*lastp = ent;
		lastp = &ent->next;
		proc_device_tree_add_node(child, ent);

		/*
		 * If we left the address part on the name, consider
		 * adding symlinks from the name and address parts.
		 */
		if (p[l] != 0 || (at = strchr(p, '@')) == 0)
			continue;

		/*
		 * If this is the first node with a given name property,
		 * add a symlink with the name property as its name.
		 */
		sib = NULL;
		while ((sib = of_get_next_child(np, sib)) && sib != child)
			if (sib->name && strcmp(sib->name, child->name) == 0)
				break;
		if (sib == child && strncmp(p, child->name, l) != 0) {
			al = proc_symlink(child->name, de, ent->name);
			if (al == 0) {
				of_node_put(sib);
				break;
			}
			set_node_name_link(child, al);
			*lastp = al;
			lastp = &al->next;
		}
		of_node_put(sib);
		/*
		 * Add another directory with the @address part as its name.
		 */
		al = proc_symlink(at, de, ent->name);
		if (al == 0)
			break;
		set_node_addr_link(child, al);
		*lastp = al;
		lastp = &al->next;
	}
	of_node_put(child);
	*lastp = NULL;
	de->subdir = list;
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
