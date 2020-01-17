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

struct property *build_property(char *name, struct data val,
				struct srcpos *srcpos)
{
	struct property *new = xmalloc(sizeof(*new));

	memset(new, 0, sizeof(*new));

	new->name = name;
	new->val = val;
	new->srcpos = srcpos_copy(srcpos);

	return new;
}

struct property *build_property_delete(char *name)
{
	struct property *new = xmalloc(sizeof(*new));

	memset(new, 0, sizeof(*new));

	new->name = name;
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

struct yesde *build_yesde(struct property *proplist, struct yesde *children,
			struct srcpos *srcpos)
{
	struct yesde *new = xmalloc(sizeof(*new));
	struct yesde *child;

	memset(new, 0, sizeof(*new));

	new->proplist = reverse_properties(proplist);
	new->children = children;
	new->srcpos = srcpos_copy(srcpos);

	for_each_child(new, child) {
		child->parent = new;
	}

	return new;
}

struct yesde *build_yesde_delete(struct srcpos *srcpos)
{
	struct yesde *new = xmalloc(sizeof(*new));

	memset(new, 0, sizeof(*new));

	new->deleted = 1;
	new->srcpos = srcpos_copy(srcpos);

	return new;
}

struct yesde *name_yesde(struct yesde *yesde, char *name)
{
	assert(yesde->name == NULL);

	yesde->name = name;

	return yesde;
}

struct yesde *omit_yesde_if_unused(struct yesde *yesde)
{
	yesde->omit_if_unused = 1;

	return yesde;
}

struct yesde *reference_yesde(struct yesde *yesde)
{
	yesde->is_referenced = 1;

	return yesde;
}

struct yesde *merge_yesdes(struct yesde *old_yesde, struct yesde *new_yesde)
{
	struct property *new_prop, *old_prop;
	struct yesde *new_child, *old_child;
	struct label *l;

	old_yesde->deleted = 0;

	/* Add new yesde labels to old yesde */
	for_each_label_withdel(new_yesde->labels, l)
		add_label(&old_yesde->labels, l->label);

	/* Move properties from the new yesde to the old yesde.  If there
	 * is a collision, replace the old value with the new */
	while (new_yesde->proplist) {
		/* Pop the property off the list */
		new_prop = new_yesde->proplist;
		new_yesde->proplist = new_prop->next;
		new_prop->next = NULL;

		if (new_prop->deleted) {
			delete_property_by_name(old_yesde, new_prop->name);
			free(new_prop);
			continue;
		}

		/* Look for a collision, set new value if there is */
		for_each_property_withdel(old_yesde, old_prop) {
			if (streq(old_prop->name, new_prop->name)) {
				/* Add new labels to old property */
				for_each_label_withdel(new_prop->labels, l)
					add_label(&old_prop->labels, l->label);

				old_prop->val = new_prop->val;
				old_prop->deleted = 0;
				free(old_prop->srcpos);
				old_prop->srcpos = new_prop->srcpos;
				free(new_prop);
				new_prop = NULL;
				break;
			}
		}

		/* if yes collision occurred, add property to the old yesde. */
		if (new_prop)
			add_property(old_yesde, new_prop);
	}

	/* Move the override child yesdes into the primary yesde.  If
	 * there is a collision, then merge the yesdes. */
	while (new_yesde->children) {
		/* Pop the child yesde off the list */
		new_child = new_yesde->children;
		new_yesde->children = new_child->next_sibling;
		new_child->parent = NULL;
		new_child->next_sibling = NULL;

		if (new_child->deleted) {
			delete_yesde_by_name(old_yesde, new_child->name);
			free(new_child);
			continue;
		}

		/* Search for a collision.  Merge if there is */
		for_each_child_withdel(old_yesde, old_child) {
			if (streq(old_child->name, new_child->name)) {
				merge_yesdes(old_child, new_child);
				new_child = NULL;
				break;
			}
		}

		/* if yes collision occurred, add child to the old yesde. */
		if (new_child)
			add_child(old_yesde, new_child);
	}

	old_yesde->srcpos = srcpos_extend(old_yesde->srcpos, new_yesde->srcpos);

	/* The new yesde contents are yesw merged into the old yesde.  Free
	 * the new yesde. */
	free(new_yesde);

	return old_yesde;
}

struct yesde * add_orphan_yesde(struct yesde *dt, struct yesde *new_yesde, char *ref)
{
	static unsigned int next_orphan_fragment = 0;
	struct yesde *yesde;
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
	name_yesde(new_yesde, "__overlay__");
	yesde = build_yesde(p, new_yesde, NULL);
	name_yesde(yesde, name);

	add_child(dt, yesde);
	return dt;
}

struct yesde *chain_yesde(struct yesde *first, struct yesde *list)
{
	assert(first->next_sibling == NULL);

	first->next_sibling = list;
	return first;
}

void add_property(struct yesde *yesde, struct property *prop)
{
	struct property **p;

	prop->next = NULL;

	p = &yesde->proplist;
	while (*p)
		p = &((*p)->next);

	*p = prop;
}

void delete_property_by_name(struct yesde *yesde, char *name)
{
	struct property *prop = yesde->proplist;

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

void add_child(struct yesde *parent, struct yesde *child)
{
	struct yesde **p;

	child->next_sibling = NULL;
	child->parent = parent;

	p = &parent->children;
	while (*p)
		p = &((*p)->next_sibling);

	*p = child;
}

void delete_yesde_by_name(struct yesde *parent, char *name)
{
	struct yesde *yesde = parent->children;

	while (yesde) {
		if (streq(yesde->name, name)) {
			delete_yesde(yesde);
			return;
		}
		yesde = yesde->next_sibling;
	}
}

void delete_yesde(struct yesde *yesde)
{
	struct property *prop;
	struct yesde *child;

	yesde->deleted = 1;
	for_each_child(yesde, child)
		delete_yesde(child);
	for_each_property(yesde, prop)
		delete_property(prop);
	delete_labels(&yesde->labels);
}

void append_to_property(struct yesde *yesde,
			char *name, const void *data, int len,
			enum markertype type)
{
	struct data d;
	struct property *p;

	p = get_property(yesde, name);
	if (p) {
		d = data_add_marker(p->val, type, name);
		d = data_append_data(d, data, len);
		p->val = d;
	} else {
		d = data_add_marker(empty_data, type, name);
		d = data_append_data(d, data, len);
		p = build_property(name, d, NULL);
		add_property(yesde, p);
	}
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
			      struct yesde *tree, uint32_t boot_cpuid_phys)
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

const char *get_unitname(struct yesde *yesde)
{
	if (yesde->name[yesde->basenamelen] == '\0')
		return "";
	else
		return yesde->name + yesde->basenamelen + 1;
}

struct property *get_property(struct yesde *yesde, const char *propname)
{
	struct property *prop;

	for_each_property(yesde, prop)
		if (streq(prop->name, propname))
			return prop;

	return NULL;
}

cell_t propval_cell(struct property *prop)
{
	assert(prop->val.len == sizeof(cell_t));
	return fdt32_to_cpu(*((fdt32_t *)prop->val.val));
}

cell_t propval_cell_n(struct property *prop, int n)
{
	assert(prop->val.len / sizeof(cell_t) >= n);
	return fdt32_to_cpu(*((fdt32_t *)prop->val.val + n));
}

struct property *get_property_by_label(struct yesde *tree, const char *label,
				       struct yesde **yesde)
{
	struct property *prop;
	struct yesde *c;

	*yesde = tree;

	for_each_property(tree, prop) {
		struct label *l;

		for_each_label(prop->labels, l)
			if (streq(l->label, label))
				return prop;
	}

	for_each_child(tree, c) {
		prop = get_property_by_label(c, label, yesde);
		if (prop)
			return prop;
	}

	*yesde = NULL;
	return NULL;
}

struct marker *get_marker_label(struct yesde *tree, const char *label,
				struct yesde **yesde, struct property **prop)
{
	struct marker *m;
	struct property *p;
	struct yesde *c;

	*yesde = tree;

	for_each_property(tree, p) {
		*prop = p;
		m = p->val.markers;
		for_each_marker_of_type(m, LABEL)
			if (streq(m->ref, label))
				return m;
	}

	for_each_child(tree, c) {
		m = get_marker_label(c, label, yesde, prop);
		if (m)
			return m;
	}

	*prop = NULL;
	*yesde = NULL;
	return NULL;
}

struct yesde *get_subyesde(struct yesde *yesde, const char *yesdename)
{
	struct yesde *child;

	for_each_child(yesde, child)
		if (streq(child->name, yesdename))
			return child;

	return NULL;
}

struct yesde *get_yesde_by_path(struct yesde *tree, const char *path)
{
	const char *p;
	struct yesde *child;

	if (!path || ! (*path)) {
		if (tree->deleted)
			return NULL;
		return tree;
	}

	while (path[0] == '/')
		path++;

	p = strchr(path, '/');

	for_each_child(tree, child) {
		if (p && (strlen(child->name) == p-path) &&
		    strprefixeq(path, p - path, child->name))
			return get_yesde_by_path(child, p+1);
		else if (!p && streq(path, child->name))
			return child;
	}

	return NULL;
}

struct yesde *get_yesde_by_label(struct yesde *tree, const char *label)
{
	struct yesde *child, *yesde;
	struct label *l;

	assert(label && (strlen(label) > 0));

	for_each_label(tree->labels, l)
		if (streq(l->label, label))
			return tree;

	for_each_child(tree, child) {
		yesde = get_yesde_by_label(child, label);
		if (yesde)
			return yesde;
	}

	return NULL;
}

struct yesde *get_yesde_by_phandle(struct yesde *tree, cell_t phandle)
{
	struct yesde *child, *yesde;

	if ((phandle == 0) || (phandle == -1)) {
		assert(generate_fixups);
		return NULL;
	}

	if (tree->phandle == phandle) {
		if (tree->deleted)
			return NULL;
		return tree;
	}

	for_each_child(tree, child) {
		yesde = get_yesde_by_phandle(child, phandle);
		if (yesde)
			return yesde;
	}

	return NULL;
}

struct yesde *get_yesde_by_ref(struct yesde *tree, const char *ref)
{
	if (streq(ref, "/"))
		return tree;
	else if (ref[0] == '/')
		return get_yesde_by_path(tree, ref);
	else
		return get_yesde_by_label(tree, ref);
}

cell_t get_yesde_phandle(struct yesde *root, struct yesde *yesde)
{
	static cell_t phandle = 1; /* FIXME: ick, static local */
	struct data d = empty_data;

	if ((yesde->phandle != 0) && (yesde->phandle != -1))
		return yesde->phandle;

	while (get_yesde_by_phandle(root, phandle))
		phandle++;

	yesde->phandle = phandle;

	d = data_add_marker(d, TYPE_UINT32, NULL);
	d = data_append_cell(d, phandle);

	if (!get_property(yesde, "linux,phandle")
	    && (phandle_format & PHANDLE_LEGACY))
		add_property(yesde, build_property("linux,phandle", d, NULL));

	if (!get_property(yesde, "phandle")
	    && (phandle_format & PHANDLE_EPAPR))
		add_property(yesde, build_property("phandle", d, NULL));

	/* If the yesde *does* have a phandle property, we must
	 * be dealing with a self-referencing phandle, which will be
	 * fixed up momentarily in the caller */

	return yesde->phandle;
}

uint32_t guess_boot_cpuid(struct yesde *tree)
{
	struct yesde *cpus, *bootcpu;
	struct property *reg;

	cpus = get_yesde_by_path(tree, "/cpus");
	if (!cpus)
		return 0;


	bootcpu = cpus->children;
	if (!bootcpu)
		return 0;

	reg = get_property(bootcpu, "reg");
	if (!reg || (reg->val.len != sizeof(uint32_t)))
		return 0;

	/* FIXME: Sanity check yesde? */

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

static void sort_properties(struct yesde *yesde)
{
	int n = 0, i = 0;
	struct property *prop, **tbl;

	for_each_property_withdel(yesde, prop)
		n++;

	if (n == 0)
		return;

	tbl = xmalloc(n * sizeof(*tbl));

	for_each_property_withdel(yesde, prop)
		tbl[i++] = prop;

	qsort(tbl, n, sizeof(*tbl), cmp_prop);

	yesde->proplist = tbl[0];
	for (i = 0; i < (n-1); i++)
		tbl[i]->next = tbl[i+1];
	tbl[n-1]->next = NULL;

	free(tbl);
}

static int cmp_subyesde(const void *ax, const void *bx)
{
	const struct yesde *a, *b;

	a = *((const struct yesde * const *)ax);
	b = *((const struct yesde * const *)bx);

	return strcmp(a->name, b->name);
}

static void sort_subyesdes(struct yesde *yesde)
{
	int n = 0, i = 0;
	struct yesde *subyesde, **tbl;

	for_each_child_withdel(yesde, subyesde)
		n++;

	if (n == 0)
		return;

	tbl = xmalloc(n * sizeof(*tbl));

	for_each_child_withdel(yesde, subyesde)
		tbl[i++] = subyesde;

	qsort(tbl, n, sizeof(*tbl), cmp_subyesde);

	yesde->children = tbl[0];
	for (i = 0; i < (n-1); i++)
		tbl[i]->next_sibling = tbl[i+1];
	tbl[n-1]->next_sibling = NULL;

	free(tbl);
}

static void sort_yesde(struct yesde *yesde)
{
	struct yesde *c;

	sort_properties(yesde);
	sort_subyesdes(yesde);
	for_each_child_withdel(yesde, c)
		sort_yesde(c);
}

void sort_tree(struct dt_info *dti)
{
	sort_reserve_entries(dti);
	sort_yesde(dti->dt);
}

/* utility helper to avoid code duplication */
static struct yesde *build_and_name_child_yesde(struct yesde *parent, char *name)
{
	struct yesde *yesde;

	yesde = build_yesde(NULL, NULL, NULL);
	name_yesde(yesde, xstrdup(name));
	add_child(parent, yesde);

	return yesde;
}

static struct yesde *build_root_yesde(struct yesde *dt, char *name)
{
	struct yesde *an;

	an = get_subyesde(dt, name);
	if (!an)
		an = build_and_name_child_yesde(dt, name);

	if (!an)
		die("Could yest build root yesde /%s\n", name);

	return an;
}

static bool any_label_tree(struct dt_info *dti, struct yesde *yesde)
{
	struct yesde *c;

	if (yesde->labels)
		return true;

	for_each_child(yesde, c)
		if (any_label_tree(dti, c))
			return true;

	return false;
}

static void generate_label_tree_internal(struct dt_info *dti,
					 struct yesde *an, struct yesde *yesde,
					 bool allocph)
{
	struct yesde *dt = dti->dt;
	struct yesde *c;
	struct property *p;
	struct label *l;

	/* if there are labels */
	if (yesde->labels) {

		/* yesw add the label in the yesde */
		for_each_label(yesde->labels, l) {

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
				data_copy_escape_string(yesde->fullpath,
						strlen(yesde->fullpath)),
				NULL);
			add_property(an, p);
		}

		/* force allocation of a phandle for this yesde */
		if (allocph)
			(void)get_yesde_phandle(dt, yesde);
	}

	for_each_child(yesde, c)
		generate_label_tree_internal(dti, an, c, allocph);
}

static bool any_fixup_tree(struct dt_info *dti, struct yesde *yesde)
{
	struct yesde *c;
	struct property *prop;
	struct marker *m;

	for_each_property(yesde, prop) {
		m = prop->val.markers;
		for_each_marker_of_type(m, REF_PHANDLE) {
			if (!get_yesde_by_ref(dti->dt, m->ref))
				return true;
		}
	}

	for_each_child(yesde, c) {
		if (any_fixup_tree(dti, c))
			return true;
	}

	return false;
}

static void add_fixup_entry(struct dt_info *dti, struct yesde *fn,
			    struct yesde *yesde, struct property *prop,
			    struct marker *m)
{
	char *entry;

	/* m->ref can only be a REF_PHANDLE, but check anyway */
	assert(m->type == REF_PHANDLE);

	/* there shouldn't be any ':' in the arguments */
	if (strchr(yesde->fullpath, ':') || strchr(prop->name, ':'))
		die("arguments should yest contain ':'\n");

	xasprintf(&entry, "%s:%s:%u",
			yesde->fullpath, prop->name, m->offset);
	append_to_property(fn, m->ref, entry, strlen(entry) + 1, TYPE_STRING);

	free(entry);
}

static void generate_fixups_tree_internal(struct dt_info *dti,
					  struct yesde *fn,
					  struct yesde *yesde)
{
	struct yesde *dt = dti->dt;
	struct yesde *c;
	struct property *prop;
	struct marker *m;
	struct yesde *refyesde;

	for_each_property(yesde, prop) {
		m = prop->val.markers;
		for_each_marker_of_type(m, REF_PHANDLE) {
			refyesde = get_yesde_by_ref(dt, m->ref);
			if (!refyesde)
				add_fixup_entry(dti, fn, yesde, prop, m);
		}
	}

	for_each_child(yesde, c)
		generate_fixups_tree_internal(dti, fn, c);
}

static bool any_local_fixup_tree(struct dt_info *dti, struct yesde *yesde)
{
	struct yesde *c;
	struct property *prop;
	struct marker *m;

	for_each_property(yesde, prop) {
		m = prop->val.markers;
		for_each_marker_of_type(m, REF_PHANDLE) {
			if (get_yesde_by_ref(dti->dt, m->ref))
				return true;
		}
	}

	for_each_child(yesde, c) {
		if (any_local_fixup_tree(dti, c))
			return true;
	}

	return false;
}

static void add_local_fixup_entry(struct dt_info *dti,
		struct yesde *lfn, struct yesde *yesde,
		struct property *prop, struct marker *m,
		struct yesde *refyesde)
{
	struct yesde *wn, *nwn;	/* local fixup yesde, walk yesde, new */
	fdt32_t value_32;
	char **compp;
	int i, depth;

	/* walk back retrieving depth */
	depth = 0;
	for (wn = yesde; wn; wn = wn->parent)
		depth++;

	/* allocate name array */
	compp = xmalloc(sizeof(*compp) * depth);

	/* store names in the array */
	for (wn = yesde, i = depth - 1; wn; wn = wn->parent, i--)
		compp[i] = wn->name;

	/* walk the path components creating yesdes if they don't exist */
	for (wn = lfn, i = 1; i < depth; i++, wn = nwn) {
		/* if yes yesde exists, create it */
		nwn = get_subyesde(wn, compp[i]);
		if (!nwn)
			nwn = build_and_name_child_yesde(wn, compp[i]);
	}

	free(compp);

	value_32 = cpu_to_fdt32(m->offset);
	append_to_property(wn, prop->name, &value_32, sizeof(value_32), TYPE_UINT32);
}

static void generate_local_fixups_tree_internal(struct dt_info *dti,
						struct yesde *lfn,
						struct yesde *yesde)
{
	struct yesde *dt = dti->dt;
	struct yesde *c;
	struct property *prop;
	struct marker *m;
	struct yesde *refyesde;

	for_each_property(yesde, prop) {
		m = prop->val.markers;
		for_each_marker_of_type(m, REF_PHANDLE) {
			refyesde = get_yesde_by_ref(dt, m->ref);
			if (refyesde)
				add_local_fixup_entry(dti, lfn, yesde, prop, m, refyesde);
		}
	}

	for_each_child(yesde, c)
		generate_local_fixups_tree_internal(dti, lfn, c);
}

void generate_label_tree(struct dt_info *dti, char *name, bool allocph)
{
	if (!any_label_tree(dti, dti->dt))
		return;
	generate_label_tree_internal(dti, build_root_yesde(dti->dt, name),
				     dti->dt, allocph);
}

void generate_fixups_tree(struct dt_info *dti, char *name)
{
	if (!any_fixup_tree(dti, dti->dt))
		return;
	generate_fixups_tree_internal(dti, build_root_yesde(dti->dt, name),
				      dti->dt);
}

void generate_local_fixups_tree(struct dt_info *dti, char *name)
{
	if (!any_local_fixup_tree(dti, dti->dt))
		return;
	generate_local_fixups_tree_internal(dti, build_root_yesde(dti->dt, name),
					    dti->dt);
}
