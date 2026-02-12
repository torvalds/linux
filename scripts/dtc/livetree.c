// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * (C) Copyright David Gibson <dwg@au1.ibm.com>, IBM Corporation.  2005.
 */

#include "dtc.h"
#include "srcpos.h"

/*
 * Tree building functions
 */

void add_label(struct label **labels, char *label)
{
	struct label *new;

	/* Make sure the label isn't already there */
	for_each_label_withdel(*labels, new)
		if (streq(new->label, label)) {
			new->deleted = 0;
			return;
		}

	new = xmalloc(sizeof(*new));
	memset(new, 0, sizeof(*new));
	new->label = label;
	new->next = *labels;
	*labels = new;
}

void delete_labels(struct label **labels)
{
	struct label *label;

	for_each_label(*labels, label)
		label->deleted = 1;
}

struct property *build_property(const char *name, struct data val,
				struct srcpos *srcpos)
{
	struct property *new = xmalloc(sizeof(*new));

	memset(new, 0, sizeof(*new));

	new->name = xstrdup(name);
	new->val = val;
	new->srcpos = srcpos_copy(srcpos);

	return new;
}

struct property *build_property_delete(const char *name)
{
	struct property *new = xmalloc(sizeof(*new));

	memset(new, 0, sizeof(*new));

	new->name = xstrdup(name);
	new->deleted = 1;

	return new;
}

struct property *chain_property(struct property *first, struct property *list)
{
	assert(first->next == NULL);

	first->next = list;
	return first;
}

struct property *reverse_properties(struct property *first)
{
	struct property *p = first;
	struct property *head = NULL;
	struct property *next;

	while (p) {
		next = p->next;
		p->next = head;
		head = p;
		p = next;
	}
	return head;
}

struct node *build_node(struct property *proplist, struct node *children,
			struct srcpos *srcpos)
{
	struct node *new = xmalloc(sizeof(*new));
	struct node *child;

	memset(new, 0, sizeof(*new));

	new->proplist = reverse_properties(proplist);
	new->children = children;
	new->srcpos = srcpos_copy(srcpos);

	for_each_child(new, child) {
		child->parent = new;
	}

	return new;
}

struct node *build_node_delete(struct srcpos *srcpos)
{
	struct node *new = xmalloc(sizeof(*new));

	memset(new, 0, sizeof(*new));

	new->deleted = 1;
	new->srcpos = srcpos_copy(srcpos);

	return new;
}

struct node *name_node(struct node *node, const char *name)
{
	assert(node->name == NULL);

	node->name = xstrdup(name);

	return node;
}

struct node *omit_node_if_unused(struct node *node)
{
	node->omit_if_unused = 1;

	return node;
}

struct node *reference_node(struct node *node)
{
	node->is_referenced = 1;

	return node;
}

struct node *merge_nodes(struct node *old_node, struct node *new_node)
{
	struct property *new_prop, *old_prop;
	struct node *new_child, *old_child;
	struct label *l;

	old_node->deleted = 0;

	/* Add new node labels to old node */
	for_each_label_withdel(new_node->labels, l)
		add_label(&old_node->labels, l->label);

	/* Move properties from the new node to the old node.  If there
	 * is a collision, replace the old value with the new */
	while (new_node->proplist) {
		/* Pop the property off the list */
		new_prop = new_node->proplist;
		new_node->proplist = new_prop->next;
		new_prop->next = NULL;

		if (new_prop->deleted) {
			delete_property_by_name(old_node, new_prop->name);
			free(new_prop);
			continue;
		}

		/* Look for a collision, set new value if there is */
		for_each_property_withdel(old_node, old_prop) {
			if (streq(old_prop->name, new_prop->name)) {
				/* Add new labels to old property */
				for_each_label_withdel(new_prop->labels, l)
					add_label(&old_prop->labels, l->label);

				old_prop->val = new_prop->val;
				old_prop->deleted = 0;
				srcpos_free(old_prop->srcpos);
				old_prop->srcpos = new_prop->srcpos;
				free(new_prop);
				new_prop = NULL;
				break;
			}
		}

		/* if no collision occurred, add property to the old node. */
		if (new_prop)
			add_property(old_node, new_prop);
	}

	/* Move the override child nodes into the primary node.  If
	 * there is a collision, then merge the nodes. */
	while (new_node->children) {
		/* Pop the child node off the list */
		new_child = new_node->children;
		new_node->children = new_child->next_sibling;
		new_child->parent = NULL;
		new_child->next_sibling = NULL;

		if (new_child->deleted) {
			delete_node_by_name(old_node, new_child->name);
			free(new_child);
			continue;
		}

		/* Search for a collision.  Merge if there is */
		for_each_child_withdel(old_node, old_child) {
			if (streq(old_child->name, new_child->name)) {
				merge_nodes(old_child, new_child);
				new_child = NULL;
				break;
			}
		}

		/* if no collision occurred, add child to the old node. */
		if (new_child)
			add_child(old_node, new_child);
	}

	old_node->srcpos = srcpos_extend(old_node->srcpos, new_node->srcpos);

	/* The new node contents are now merged into the old node.  Free
	 * the new node. */
	free(new_node);

	return old_node;
}

struct node * add_orphan_node(struct node *dt, struct node *new_node, char *ref)
{
	static unsigned int next_orphan_fragment = 0;
	struct node *node;
	struct property *p;
	struct data d = empty_data;
	char *name;

	if (ref[0] == '/') {
		d = data_add_marker(d, TYPE_STRING, ref);
		d = data_append_data(d, ref, strlen(ref) + 1);

		p = build_property("target-path", d, NULL);
	} else {
		d = data_add_marker(d, REF_PHANDLE, ref);
		d = data_append_integer(d, 0xffffffff, 32);

		p = build_property("target", d, NULL);
	}

	xasprintf(&name, "fragment@%u",
			next_orphan_fragment++);
	name_node(new_node, "__overlay__");
	node = build_node(p, new_node, NULL);
	name_node(node, name);
	free(name);

	add_child(dt, node);
	return dt;
}

struct node *chain_node(struct node *first, struct node *list)
{
	assert(first->next_sibling == NULL);

	first->next_sibling = list;
	return first;
}

void add_property(struct node *node, struct property *prop)
{
	struct property **p;

	prop->next = NULL;

	p = &node->proplist;
	while (*p)
		p = &((*p)->next);

	*p = prop;
}

void delete_property_by_name(struct node *node, char *name)
{
	struct property *prop = node->proplist;

	while (prop) {
		if (streq(prop->name, name)) {
			delete_property(prop);
			return;
		}
		prop = prop->next;
	}
}

void delete_property(struct property *prop)
{
	prop->deleted = 1;
	delete_labels(&prop->labels);
}

void add_child(struct node *parent, struct node *child)
{
	struct node **p;

	child->next_sibling = NULL;
	child->parent = parent;

	p = &parent->children;
	while (*p)
		p = &((*p)->next_sibling);

	*p = child;
}

void delete_node_by_name(struct node *parent, char *name)
{
	struct node *node = parent->children;

	while (node) {
		if (streq(node->name, name)) {
			delete_node(node);
			return;
		}
		node = node->next_sibling;
	}
}

void delete_node(struct node *node)
{
	struct property *prop;
	struct node *child;

	node->deleted = 1;
	for_each_child(node, child)
		delete_node(child);
	for_each_property(node, prop)
		delete_property(prop);
	delete_labels(&node->labels);
}

void append_to_property(struct node *node,
			char *name, const void *data, int len,
			enum markertype type)
{
	struct property *p;

	p = get_property(node, name);
	if (!p) {
		p = build_property(name, empty_data, NULL);
		add_property(node, p);
	}

	p->val = data_add_marker(p->val, type, name);
	p->val = data_append_data(p->val, data, len);
}

static int append_unique_str_to_property(struct node *node,
					 char *name, const char *data, int len)
{
	struct property *p;

	p = get_property(node, name);
	if (p) {
		const char *s;

		if (p->val.len && p->val.val[p->val.len - 1] != '\0')
			/* The current content doesn't look like a string */
			return -1;

		for (s = p->val.val; s < p->val.val + p->val.len; s = strchr(s, '\0') + 1) {
			if (strcmp(data, s) == 0)
				/* data already contained in node.name */
				return 0;
		}
	} else {
		p = build_property(name, empty_data, NULL);
		add_property(node, p);
	}

	p->val = data_add_marker(p->val, TYPE_STRING, name);
	p->val = data_append_data(p->val, data, len);

	return 0;
}

static int append_unique_u32_to_property(struct node *node, char *name, fdt32_t value)
{
	struct property *p;

	p = get_property(node, name);
	if (p) {
		const fdt32_t *v, *val_end = (const fdt32_t *)p->val.val + p->val.len / 4;

		if (p->val.len % 4 != 0)
			/* The current content doesn't look like a u32 array */
			return -1;

		for (v = (const void *)p->val.val; v < val_end; v++) {
			if (*v == value)
				/* value already contained */
				return 0;
		}
	} else {
		p = build_property(name, empty_data, NULL);
		add_property(node, p);
	}

	p->val = data_add_marker(p->val, TYPE_UINT32, name);
	p->val = data_append_data(p->val, &value, 4);

	return 0;
}

struct reserve_info *build_reserve_entry(uint64_t address, uint64_t size)
{
	struct reserve_info *new = xmalloc(sizeof(*new));

	memset(new, 0, sizeof(*new));

	new->address = address;
	new->size = size;

	return new;
}

struct reserve_info *chain_reserve_entry(struct reserve_info *first,
					struct reserve_info *list)
{
	assert(first->next == NULL);

	first->next = list;
	return first;
}

struct reserve_info *add_reserve_entry(struct reserve_info *list,
				      struct reserve_info *new)
{
	struct reserve_info *last;

	new->next = NULL;

	if (! list)
		return new;

	for (last = list; last->next; last = last->next)
		;

	last->next = new;

	return list;
}

struct dt_info *build_dt_info(unsigned int dtsflags,
			      struct reserve_info *reservelist,
			      struct node *tree, uint32_t boot_cpuid_phys)
{
	struct dt_info *dti;

	dti = xmalloc(sizeof(*dti));
	dti->dtsflags = dtsflags;
	dti->reservelist = reservelist;
	dti->dt = tree;
	dti->boot_cpuid_phys = boot_cpuid_phys;

	return dti;
}

/*
 * Tree accessor functions
 */

const char *get_unitname(struct node *node)
{
	if (node->name[node->basenamelen] == '\0')
		return "";
	else
		return node->name + node->basenamelen + 1;
}

struct property *get_property(struct node *node, const char *propname)
{
	struct property *prop;

	for_each_property(node, prop)
		if (streq(prop->name, propname))
			return prop;

	return NULL;
}

cell_t propval_cell(struct property *prop)
{
	assert(prop->val.len == sizeof(cell_t));
	return fdt32_to_cpu(*((fdt32_t *)prop->val.val));
}

cell_t propval_cell_n(struct property *prop, unsigned int n)
{
	assert(prop->val.len / sizeof(cell_t) > n);
	return fdt32_to_cpu(*((fdt32_t *)prop->val.val + n));
}

struct property *get_property_by_label(struct node *tree, const char *label,
				       struct node **node)
{
	struct property *prop;
	struct node *c;

	*node = tree;

	for_each_property(tree, prop) {
		struct label *l;

		for_each_label(prop->labels, l)
			if (streq(l->label, label))
				return prop;
	}

	for_each_child(tree, c) {
		prop = get_property_by_label(c, label, node);
		if (prop)
			return prop;
	}

	*node = NULL;
	return NULL;
}

struct marker *get_marker_label(struct node *tree, const char *label,
				struct node **node, struct property **prop)
{
	struct marker *m;
	struct property *p;
	struct node *c;

	*node = tree;

	for_each_property(tree, p) {
		*prop = p;
		m = p->val.markers;
		for_each_marker_of_type(m, LABEL)
			if (streq(m->ref, label))
				return m;
	}

	for_each_child(tree, c) {
		m = get_marker_label(c, label, node, prop);
		if (m)
			return m;
	}

	*prop = NULL;
	*node = NULL;
	return NULL;
}

struct node *get_subnode(struct node *node, const char *nodename)
{
	struct node *child;

	for_each_child(node, child)
		if (streq(child->name, nodename) && !child->deleted)
			return child;

	return NULL;
}

struct node *get_node_by_path(struct node *tree, const char *path)
{
	const char *p;
	struct node *child;

	if (!path || ! (*path)) {
		if (tree->deleted)
			return NULL;
		return tree;
	}

	while (path[0] == '/')
		path++;

	p = strchr(path, '/');

	for_each_child(tree, child) {
		if (p && strprefixeq(path, (size_t)(p - path), child->name))
			return get_node_by_path(child, p+1);
		else if (!p && streq(path, child->name))
			return child;
	}

	return NULL;
}

struct node *get_node_by_label(struct node *tree, const char *label)
{
	struct node *child, *node;
	struct label *l;

	assert(label && (strlen(label) > 0));

	for_each_label(tree->labels, l)
		if (streq(l->label, label))
			return tree;

	for_each_child(tree, child) {
		node = get_node_by_label(child, label);
		if (node)
			return node;
	}

	return NULL;
}

struct node *get_node_by_phandle(struct node *tree, cell_t phandle)
{
	struct node *child, *node;

	if (!phandle_is_valid(phandle)) {
		assert(generate_fixups);
		return NULL;
	}

	if (tree->phandle == phandle) {
		if (tree->deleted)
			return NULL;
		return tree;
	}

	for_each_child(tree, child) {
		node = get_node_by_phandle(child, phandle);
		if (node)
			return node;
	}

	return NULL;
}

struct node *get_node_by_ref(struct node *tree, const char *ref)
{
	struct node *target = tree;
	const char *label = NULL, *path = NULL;

	if (streq(ref, "/"))
		return tree;

	if (ref[0] == '/')
		path = ref;
	else
		label = ref;

	if (label) {
		const char *slash = strchr(label, '/');
		char *buf = NULL;

		if (slash) {
			buf = xstrndup(label, slash - label);
			label = buf;
			path = slash + 1;
		}

		target = get_node_by_label(tree, label);

		free(buf);

		if (!target)
			return NULL;
	}

	if (path)
		target = get_node_by_path(target, path);

	return target;
}

static void add_phandle_property(struct node *node,
				 const char *name, int format)
{
	struct data d;

	if (!(phandle_format & format))
		return;
	if (get_property(node, name))
		return;

	d = data_add_marker(empty_data, TYPE_UINT32, NULL);
	d = data_append_cell(d, node->phandle);

	add_property(node, build_property(name, d, NULL));
}

cell_t get_node_phandle(struct node *root, struct node *node)
{
	static cell_t phandle = 1; /* FIXME: ick, static local */

	if (phandle_is_valid(node->phandle))
		return node->phandle;

	while (get_node_by_phandle(root, phandle))
		phandle++;

	node->phandle = phandle;

	add_phandle_property(node, "linux,phandle", PHANDLE_LEGACY);
	add_phandle_property(node, "phandle", PHANDLE_EPAPR);

	/* If the node *does* have a phandle property, we must
	 * be dealing with a self-referencing phandle, which will be
	 * fixed up momentarily in the caller */

	return node->phandle;
}

uint32_t guess_boot_cpuid(struct node *tree)
{
	struct node *cpus, *bootcpu;
	struct property *reg;

	cpus = get_node_by_path(tree, "/cpus");
	if (!cpus)
		return 0;


	bootcpu = cpus->children;
	if (!bootcpu)
		return 0;

	reg = get_property(bootcpu, "reg");
	if (!reg || (reg->val.len != sizeof(uint32_t)))
		return 0;

	/* FIXME: Sanity check node? */

	return propval_cell(reg);
}

static int cmp_reserve_info(const void *ax, const void *bx)
{
	const struct reserve_info *a, *b;

	a = *((const struct reserve_info * const *)ax);
	b = *((const struct reserve_info * const *)bx);

	if (a->address < b->address)
		return -1;
	else if (a->address > b->address)
		return 1;
	else if (a->size < b->size)
		return -1;
	else if (a->size > b->size)
		return 1;
	else
		return 0;
}

static void sort_reserve_entries(struct dt_info *dti)
{
	struct reserve_info *ri, **tbl;
	int n = 0, i = 0;

	for (ri = dti->reservelist;
	     ri;
	     ri = ri->next)
		n++;

	if (n == 0)
		return;

	tbl = xmalloc(n * sizeof(*tbl));

	for (ri = dti->reservelist;
	     ri;
	     ri = ri->next)
		tbl[i++] = ri;

	qsort(tbl, n, sizeof(*tbl), cmp_reserve_info);

	dti->reservelist = tbl[0];
	for (i = 0; i < (n-1); i++)
		tbl[i]->next = tbl[i+1];
	tbl[n-1]->next = NULL;

	free(tbl);
}

static int cmp_prop(const void *ax, const void *bx)
{
	const struct property *a, *b;

	a = *((const struct property * const *)ax);
	b = *((const struct property * const *)bx);

	return strcmp(a->name, b->name);
}

static void sort_properties(struct node *node)
{
	int n = 0, i = 0;
	struct property *prop, **tbl;

	for_each_property_withdel(node, prop)
		n++;

	if (n == 0)
		return;

	tbl = xmalloc(n * sizeof(*tbl));

	for_each_property_withdel(node, prop)
		tbl[i++] = prop;

	qsort(tbl, n, sizeof(*tbl), cmp_prop);

	node->proplist = tbl[0];
	for (i = 0; i < (n-1); i++)
		tbl[i]->next = tbl[i+1];
	tbl[n-1]->next = NULL;

	free(tbl);
}

static int cmp_subnode(const void *ax, const void *bx)
{
	const struct node *a, *b;

	a = *((const struct node * const *)ax);
	b = *((const struct node * const *)bx);

	return strcmp(a->name, b->name);
}

static void sort_subnodes(struct node *node)
{
	int n = 0, i = 0;
	struct node *subnode, **tbl;

	for_each_child_withdel(node, subnode)
		n++;

	if (n == 0)
		return;

	tbl = xmalloc(n * sizeof(*tbl));

	for_each_child_withdel(node, subnode)
		tbl[i++] = subnode;

	qsort(tbl, n, sizeof(*tbl), cmp_subnode);

	node->children = tbl[0];
	for (i = 0; i < (n-1); i++)
		tbl[i]->next_sibling = tbl[i+1];
	tbl[n-1]->next_sibling = NULL;

	free(tbl);
}

static void sort_node(struct node *node)
{
	struct node *c;

	sort_properties(node);
	sort_subnodes(node);
	for_each_child_withdel(node, c)
		sort_node(c);
}

void sort_tree(struct dt_info *dti)
{
	sort_reserve_entries(dti);
	sort_node(dti->dt);
}

/* utility helper to avoid code duplication */
static struct node *build_and_name_child_node(struct node *parent, const char *name)
{
	struct node *node;

	node = build_node(NULL, NULL, NULL);
	name_node(node, name);
	add_child(parent, node);

	return node;
}

static struct node *build_root_node(struct node *dt, const char *name)
{
	struct node *an;

	an = get_subnode(dt, name);
	if (!an)
		an = build_and_name_child_node(dt, name);

	if (!an)
		die("Could not build root node /%s\n", name);

	return an;
}

static bool any_label_tree(struct dt_info *dti, struct node *node)
{
	struct node *c;

	if (node->labels)
		return true;

	for_each_child(node, c)
		if (any_label_tree(dti, c))
			return true;

	return false;
}

static void generate_label_tree_internal(struct dt_info *dti,
					 struct node *an, struct node *node,
					 bool allocph)
{
	struct node *dt = dti->dt;
	struct node *c;
	struct property *p;
	struct label *l;

	/* if there are labels */
	if (node->labels) {

		/* now add the label in the node */
		for_each_label(node->labels, l) {

			/* check whether the label already exists */
			p = get_property(an, l->label);
			if (p) {
				fprintf(stderr, "WARNING: label %s already"
					" exists in /%s", l->label,
					an->name);
				continue;
			}

			/* insert it */
			p = build_property(l->label,
				data_copy_escape_string(node->fullpath,
						strlen(node->fullpath)),
				NULL);
			add_property(an, p);
		}

		/* force allocation of a phandle for this node */
		if (allocph)
			(void)get_node_phandle(dt, node);
	}

	for_each_child(node, c)
		generate_label_tree_internal(dti, an, c, allocph);
}

static bool any_fixup_tree(struct dt_info *dti, struct node *node)
{
	struct node *c;
	struct property *prop;
	struct marker *m;

	for_each_property(node, prop) {
		m = prop->val.markers;
		for_each_marker_of_type(m, REF_PHANDLE) {
			if (!get_node_by_ref(dti->dt, m->ref))
				return true;
		}
	}

	for_each_child(node, c) {
		if (any_fixup_tree(dti, c))
			return true;
	}

	return false;
}

static int add_fixup_entry(struct dt_info *dti, struct node *fn,
			   struct node *node, struct property *prop,
			   struct marker *m)
{
	char *entry;
	int ret;

	/* m->ref can only be a REF_PHANDLE, but check anyway */
	assert(m->type == REF_PHANDLE);

	/* The format only permits fixups for references to label, not
	 * references to path */
	if (strchr(m->ref, '/'))
		die("Can't generate fixup for reference to path &{%s}\n",
		    m->ref);

	/* there shouldn't be any ':' in the arguments */
	if (strchr(node->fullpath, ':') || strchr(prop->name, ':'))
		die("arguments should not contain ':'\n");

	xasprintf(&entry, "%s:%s:%u",
			node->fullpath, prop->name, m->offset);
	ret = append_unique_str_to_property(fn, m->ref, entry, strlen(entry) + 1);

	free(entry);

	return ret;
}

static int generate_fixups_tree_internal(struct dt_info *dti,
					 struct node *fn,
					 struct node *node)
{
	struct node *dt = dti->dt;
	struct node *c;
	struct property *prop;
	struct marker *m;
	struct node *refnode;
	int ret = 0;

	for_each_property(node, prop) {
		m = prop->val.markers;
		for_each_marker_of_type(m, REF_PHANDLE) {
			refnode = get_node_by_ref(dt, m->ref);
			if (!refnode)
				if (add_fixup_entry(dti, fn, node, prop, m))
					ret = -1;
		}
	}

	for_each_child(node, c)
		if (generate_fixups_tree_internal(dti, fn, c))
			ret = -1;

	return ret;
}

static bool any_local_fixup_tree(struct dt_info *dti, struct node *node)
{
	struct node *c;
	struct property *prop;
	struct marker *m;

	for_each_property(node, prop) {
		m = prop->val.markers;
		for_each_marker_of_type(m, REF_PHANDLE) {
			if (get_node_by_ref(dti->dt, m->ref))
				return true;
		}
	}

	for_each_child(node, c) {
		if (any_local_fixup_tree(dti, c))
			return true;
	}

	return false;
}

static int add_local_fixup_entry(struct dt_info *dti,
		struct node *lfn, struct node *node,
		struct property *prop, struct marker *m,
		struct node *refnode)
{
	struct node *wn, *nwn;	/* local fixup node, walk node, new */
	fdt32_t value_32;
	char **compp;
	int i, depth;

	/* walk back retrieving depth */
	depth = 0;
	for (wn = node; wn; wn = wn->parent)
		depth++;

	/* allocate name array */
	compp = xmalloc(sizeof(*compp) * depth);

	/* store names in the array */
	for (wn = node, i = depth - 1; wn; wn = wn->parent, i--)
		compp[i] = wn->name;

	/* walk the path components creating nodes if they don't exist */
	for (wn = lfn, i = 1; i < depth; i++, wn = nwn) {
		/* if no node exists, create it */
		nwn = build_root_node(wn, compp[i]);
	}

	free(compp);

	value_32 = cpu_to_fdt32(m->offset);
	return append_unique_u32_to_property(wn, prop->name, value_32);
}

static int generate_local_fixups_tree_internal(struct dt_info *dti,
					       struct node *lfn,
					       struct node *node)
{
	struct node *dt = dti->dt;
	struct node *c;
	struct property *prop;
	struct marker *m;
	struct node *refnode;
	int ret = 0;

	for_each_property(node, prop) {
		m = prop->val.markers;
		for_each_marker_of_type(m, REF_PHANDLE) {
			refnode = get_node_by_ref(dt, m->ref);
			if (refnode)
				if (add_local_fixup_entry(dti, lfn, node, prop, m, refnode))
					ret = -1;
		}
	}

	for_each_child(node, c)
		if (generate_local_fixups_tree_internal(dti, lfn, c))
			ret = -1;

	return ret;
}

void generate_labels_from_tree(struct dt_info *dti, const char *name)
{
	struct node *an;
	struct property *p;

	an = get_subnode(dti->dt, name);
	if (!an)
		return;

	for_each_property(an, p) {
		struct node *labeled_node;

		labeled_node = get_node_by_path(dti->dt, p->val.val);
		if (labeled_node)
			add_label(&labeled_node->labels, p->name);
		else if (quiet < 1)
			fprintf(stderr, "Warning: Path %s referenced in property %s/%s missing",
				p->val.val, name, p->name);
	}
}

void generate_label_tree(struct dt_info *dti, const char *name, bool allocph)
{
	if (!any_label_tree(dti, dti->dt))
		return;
	generate_label_tree_internal(dti, build_root_node(dti->dt, name),
				     dti->dt, allocph);
}

void generate_fixups_tree(struct dt_info *dti, const char *name)
{
	if (!any_fixup_tree(dti, dti->dt))
		return;
	if (generate_fixups_tree_internal(dti, build_root_node(dti->dt, name), dti->dt))
		fprintf(stderr,
			"Warning: Preexisting data in %s malformed, some content could not be added.\n",
			name);
}

void fixup_phandles(struct dt_info *dti, const char *name)
{
	struct node *an;
	struct property *fp;

	an = get_subnode(dti->dt, name);
	if (!an)
		return;

	for_each_property(an, fp) {
		char *fnext = fp->val.val;
		char *fv;
		unsigned int fl;

		while ((fl = fp->val.len - (fnext - fp->val.val))) {
			char *propname, *soffset;
			struct node *n;
			struct property *p;
			long offset;

			fv = fnext;
			fnext = memchr(fv, 0, fl);

			if (!fnext) {
				if (quiet < 1)
					fprintf(stderr, "Warning: Malformed fixup entry for label %s\n",
						fp->name);
				break;
			}
			fnext += 1;

			propname = memchr(fv, ':', fnext - 1 - fv);
			if (!propname) {
				if (quiet < 1)
					fprintf(stderr, "Warning: Malformed fixup entry for label %s\n",
						fp->name);
				continue;
			}
			propname++;

			soffset = memchr(propname, ':', fnext - 1 - propname);
			if (!soffset) {
				if (quiet < 1)
					fprintf(stderr, "Warning: Malformed fixup entry for label %s\n",
						fp->name);
				continue;
			}
			soffset++;

			/*
			 * temporarily modify the property to not have to create
			 * a copy for the node path.
			 */
			*(propname - 1) = '\0';

			n = get_node_by_path(dti->dt, fv);
			if (!n && quiet < 1)
				fprintf(stderr, "Warning: Label %s references non-existing node %s\n",
					fp->name, fv);

			*(propname - 1) = ':';

			if (!n)
				continue;

			/*
			 * temporarily modify the property to not have to create
			 * a copy for the property name.
			 */
			*(soffset - 1) = '\0';

			p = get_property(n, propname);

			if (!p && quiet < 1)
				fprintf(stderr, "Warning: Label %s references non-existing property %s in node %s\n",
					fp->name, n->fullpath, propname);

			*(soffset - 1) = ':';

			if (!p)
				continue;

			offset = strtol(soffset, NULL, 0);
			if (offset < 0 || offset + 4 > p->val.len) {
				if (quiet < 1)
					fprintf(stderr,
						"Warning: Label %s contains invalid offset for property %s in node %s\n",
						fp->name, p->name, n->fullpath);
				continue;
			}

			property_add_marker(p, REF_PHANDLE, offset, fp->name);
		}
	}
}

void generate_local_fixups_tree(struct dt_info *dti, const char *name)
{
	if (!any_local_fixup_tree(dti, dti->dt))
		return;
	if (generate_local_fixups_tree_internal(dti, build_root_node(dti->dt, name), dti->dt))
		fprintf(stderr,
			"Warning: Preexisting data in %s malformed, some content could not be added.\n",
			name);
}

static void local_fixup_phandles_node(struct dt_info *dti, struct node *lf, struct node *n)
{
	struct property *lfp;
	struct node *lfsubnode;

	for_each_property(lf, lfp) {
		struct property *p = get_property(n, lfp->name);
		fdt32_t *offsets = (fdt32_t *)lfp->val.val;
		size_t i;

		if (!p) {
			if (quiet < 1)
				fprintf(stderr, "Warning: Property %s in %s referenced in __local_fixups__ missing\n",
					lfp->name, n->fullpath);
			continue;
		}

		/*
		 * Each property in the __local_fixups__ tree is a concatenation
		 * of offsets, so it must be a multiple of sizeof(fdt32_t).
		 */
		if (lfp->val.len % sizeof(fdt32_t)) {
			if (quiet < 1)
				fprintf(stderr, "Warning: property %s in /__local_fixups__%s malformed\n",
					lfp->name, n->fullpath);
			continue;
		}

		for (i = 0; i < lfp->val.len / sizeof(fdt32_t); i++)
			add_phandle_marker(dti, p, dtb_ld32(offsets + i));
	}

	for_each_child(lf, lfsubnode) {
		struct node *subnode = get_subnode(n, lfsubnode->name);

		if (!subnode) {
			if (quiet < 1)
				fprintf(stderr, "Warning: node %s/%s referenced in __local_fixups__ missing\n",
					lfsubnode->name, n->fullpath);
			continue;
		}

		local_fixup_phandles_node(dti, lfsubnode, subnode);
	}
}

void local_fixup_phandles(struct dt_info *dti, const char *name)
{
	struct node *an;

	an = get_subnode(dti->dt, name);
	if (!an)
		return;

	local_fixup_phandles_node(dti, an, dti->dt);
}
