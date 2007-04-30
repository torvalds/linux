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
	memcpy(page, (char *)pp->value + off, n);
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
__proc_device_tree_add_prop(struct proc_dir_entry *de, struct property *pp,
		const char *name)
{
	struct proc_dir_entry *ent;

	/*
	 * Unfortunately proc_register puts each new entry
	 * at the beginning of the list.  So we rearrange them.
	 */
	ent = create_proc_read_entry(name,
				     strncmp(name, "security-", 9)
				     ? S_IRUGO : S_IRUSR, de,
				     property_read_proc, pp);
	if (ent == NULL)
		return NULL;

	if (!strncmp(name, "security-", 9))
		ent->size = 0; /* don't leak number of password chars */
	else
		ent->size = pp->length;

	return ent;
}


void proc_device_tree_add_prop(struct proc_dir_entry *pde, struct property *prop)
{
	__proc_device_tree_add_prop(pde, prop, prop->name);
}

void proc_device_tree_remove_prop(struct proc_dir_entry *pde,
				  struct property *prop)
{
	remove_proc_entry(prop->name, pde);
}

void proc_device_tree_update_prop(struct proc_dir_entry *pde,
				  struct property *newprop,
				  struct property *oldprop)
{
	struct proc_dir_entry *ent;

	for (ent = pde->subdir; ent != NULL; ent = ent->next)
		if (ent->data == oldprop)
			break;
	if (ent == NULL) {
		printk(KERN_WARNING "device-tree: property \"%s\" "
		       " does not exist\n", oldprop->name);
	} else {
		ent->data = newprop;
		ent->size = newprop->length;
	}
}

/*
 * Various dodgy firmware might give us nodes and/or properties with
 * conflicting names. That's generally ok, except for exporting via /proc,
 * so munge names here to ensure they're unique.
 */

static int duplicate_name(struct proc_dir_entry *de, const char *name)
{
	struct proc_dir_entry *ent;
	int found = 0;

	spin_lock(&proc_subdir_lock);

	for (ent = de->subdir; ent != NULL; ent = ent->next) {
		if (strcmp(ent->name, name) == 0) {
			found = 1;
			break;
		}
	}

	spin_unlock(&proc_subdir_lock);

	return found;
}

static const char *fixup_name(struct device_node *np, struct proc_dir_entry *de,
		const char *name)
{
	char *fixed_name;
	int fixup_len = strlen(name) + 2 + 1; /* name + #x + \0 */
	int i = 1, size;

realloc:
	fixed_name = kmalloc(fixup_len, GFP_KERNEL);
	if (fixed_name == NULL) {
		printk(KERN_ERR "device-tree: Out of memory trying to fixup "
				"name \"%s\"\n", name);
		return name;
	}

retry:
	size = snprintf(fixed_name, fixup_len, "%s#%d", name, i);
	size++; /* account for NULL */

	if (size > fixup_len) {
		/* We ran out of space, free and reallocate. */
		kfree(fixed_name);
		fixup_len = size;
		goto realloc;
	}

	if (duplicate_name(de, fixed_name)) {
		/* Multiple duplicates. Retry with a different offset. */
		i++;
		goto retry;
	}

	printk(KERN_WARNING "device-tree: Duplicate name in %s, "
			"renamed to \"%s\"\n", np->full_name, fixed_name);

	return fixed_name;
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
		/* Use everything after the last slash, or the full name */
		p = strrchr(child->full_name, '/');
		if (!p)
			p = child->full_name;
		else
			++p;

		if (duplicate_name(de, p))
			p = fixup_name(np, de, p);

		ent = proc_mkdir(p, de);
		if (ent == 0)
			break;
		proc_device_tree_add_node(child, ent);
	}
	of_node_put(child);

	for (pp = np->properties; pp != 0; pp = pp->next) {
		p = pp->name;

		if (duplicate_name(de, p))
			p = fixup_name(np, de, p);

		ent = __proc_device_tree_add_prop(de, pp, p);
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
