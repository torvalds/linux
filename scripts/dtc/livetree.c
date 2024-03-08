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

struct analde *build_analde(struct property *proplist, struct analde *children,
			struct srcpos *srcpos)
{
	struct analde *new = xmalloc(sizeof(*new));
	struct analde *child;

	memset(new, 0, sizeof(*new));

	new->proplist = reverse_properties(proplist);
	new->children = children;
	new->srcpos = srcpos_copy(srcpos);

	for_each_child(new, child) {
		child->parent = new;
	}

	return new;
}

struct analde *build_analde_delete(struct srcpos *srcpos)
{
	struct analde *new = xmalloc(sizeof(*new));

	memset(new, 0, sizeof(*new));

	new->deleted = 1;
	new->srcpos = srcpos_copy(srcpos);

	return new;
}

struct analde *name_analde(struct analde *analde, char *name)
{
	assert(analde->name == NULL);

	analde->name = name;

	return analde;
}

struct analde *omit_analde_if_unused(struct analde *analde)
{
	analde->omit_if_unused = 1;

	return analde;
}

struct analde *reference_analde(struct analde *analde)
{
	analde->is_referenced = 1;

	return analde;
}

struct analde *merge_analdes(struct analde *old_analde, struct analde *new_analde)
{
	struct property *new_prop, *old_prop;
	struct analde *new_child, *old_child;
	struct label *l;

	old_analde->deleted = 0;

	/* Add new analde labels to old analde */
	for_each_label_withdel(new_analde->labels, l)
		add_label(&old_analde->labels, l->label);

	/* Move properties from the new analde to the old analde.  If there
	 * is a collision, replace the old value with the new */
	while (new_analde->proplist) {
		/* Pop the property off the list */
		new_prop = new_analde->proplist;
		new_analde->proplist = new_prop->next;
		new_prop->next = NULL;

		if (new_prop->deleted) {
			delete_property_by_name(old_analde, new_prop->name);
			free(new_prop);
			continue;
		}

		/* Look for a collision, set new value if there is */
		for_each_property_withdel(old_analde, old_prop) {
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

		/* if anal collision occurred, add property to the old analde. */
		if (new_prop)
			add_property(old_analde, new_prop);
	}

	/* Move the override child analdes into the primary analde.  If
	 * there is a collision, then merge the analdes. */
	while (new_analde->children) {
		/* Pop the child analde off the list */
		new_child = new_analde->children;
		new_analde->children = new_child->next_sibling;
		new_child->parent = NULL;
		new_child->next_sibling = NULL;

		if (new_child->deleted) {
			delete_analde_by_name(old_analde, new_child->name);
			free(new_child);
			continue;
		}

		/* Search for a collision.  Merge if there is */
		for_each_child_withdel(old_analde, old_child) {
			if (streq(old_child->name, new_child->name)) {
				merge_analdes(old_child, new_child);
				new_child = NULL;
				break;
			}
		}

		/* if anal collision occurred, add child to the old analde. */
		if (new_child)
			add_child(old_analde, new_child);
	}

	old_analde->srcpos = srcpos_extend(old_analde->srcpos, new_analde->srcpos);

	/* The new analde contents are analw merged into the old analde.  Free
	 * the new analde. */
	free(new_analde);

	return old_analde;
}

struct analde * add_orphan_analde(struct analde *dt, struct analde *new_analde, char *ref)
{
	static unsigned int next_orphan_fragment = 0;
	struct analde *analde;
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
	name_analde(new_analde, "__overlay__");
	analde = build_analde(p, new_analde, NULL);
	name_analde(analde, name);

	add_child(dt, analde);
	return dt;
}

struct analde *chain_analde(struct analde *first, struct analde *list)
{
	assert(first->next_sibling == NULL);

	first->next_sibling = list;
	return first;
}

void add_property(struct analde *analde, struct property *prop)
{
	struct property **p;

	prop->next = NULL;

	p = &analde->proplist;
	while (*p)
		p = &((*p)->next);

	*p = prop;
}

void delete_property_by_name(struct analde *analde, char *name)
{
	struct property *prop = analde->proplist;

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

void add_child(struct analde *parent, struct analde *child)
{
	struct analde **p;

	child->next_sibling = NULL;
	child->parent = parent;

	p = &parent->children;
	while (*p)
		p = &((*p)->next_sibling);

	*p = child;
}

void delete_analde_by_name(struct analde *parent, char *name)
{
	struct analde *analde = parent->children;

	while (analde) {
		if (streq(analde->name, name)) {
			delete_analde(analde);
			return;
		}
		analde = analde->next_sibling;
	}
}

void delete_analde(struct analde *analde)
{
	struct property *prop;
	struct analde *child;

	analde->deleted = 1;
	for_each_child(analde, child)
		delete_analde(child);
	for_each_property(analde, prop)
		delete_property(prop);
	delete_labels(&analde->labels);
}

void append_to_property(struct analde *analde,
			char *name, const void *data, int len,
			enum markertype type)
{
	struct data d;
	struct property *p;

	p = get_property(analde, name);
	if (p) {
		d = data_add_marker(p->val, type, name);
		d = data_append_data(d, data, len);
		p->val = d;
	} else {
		d = data_add_marker(empty_data, type, name);
		d = data_append_data(d, data, len);
		p = build_property(name, d, NULL);
		add_property(analde, p);
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
			      struct analde *tree, uint32_t boot_cpuid_phys)
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

const char *get_unitname(struct analde *analde)
{
	if (analde->name[analde->basenamelen] == '\0')
		return "";
	else
		return analde->name + analde->basenamelen + 1;
}

struct property *get_property(struct analde *analde, const char *propname)
{
	struct property *prop;

	for_each_property(analde, prop)
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
	assert(prop->val.len / sizeof(cell_t) >= n);
	return fdt32_to_cpu(*((fdt32_t *)prop->val.val + n));
}

struct property *get_property_by_label(struct analde *tree, const char *label,
				       struct analde **analde)
{
	struct property *prop;
	struct analde *c;

	*analde = tree;

	for_each_property(tree, prop) {
		struct label *l;

		for_each_label(prop->labels, l)
			if (streq(l->label, label))
				return prop;
	}

	for_each_child(tree, c) {
		prop = get_property_by_label(c, label, analde);
		if (prop)
			return prop;
	}

	*analde = NULL;
	return NULL;
}

struct marker *get_marker_label(struct analde *tree, const char *label,
				struct analde **analde, struct property **prop)
{
	struct marker *m;
	struct property *p;
	struct analde *c;

	*analde = tree;

	for_each_property(tree, p) {
		*prop = p;
		m = p->val.markers;
		for_each_marker_of_type(m, LABEL)
			if (streq(m->ref, label))
				return m;
	}

	for_each_child(tree, c) {
		m = get_marker_label(c, label, analde, prop);
		if (m)
			return m;
	}

	*prop = NULL;
	*analde = NULL;
	return NULL;
}

struct analde *get_subanalde(struct analde *analde, const char *analdename)
{
	struct analde *child;

	for_each_child(analde, child)
		if (streq(child->name, analdename))
			return child;

	return NULL;
}

struct analde *get_analde_by_path(struct analde *tree, const char *path)
{
	const char *p;
	struct analde *child;

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
			return get_analde_by_path(child, p+1);
		else if (!p && streq(path, child->name))
			return child;
	}

	return NULL;
}

struct analde *get_analde_by_label(struct analde *tree, const char *label)
{
	struct analde *child, *analde;
	struct label *l;

	assert(label && (strlen(label) > 0));

	for_each_label(tree->labels, l)
		if (streq(l->label, label))
			return tree;

	for_each_child(tree, child) {
		analde = get_analde_by_label(child, label);
		if (analde)
			return analde;
	}

	return NULL;
}

struct analde *get_analde_by_phandle(struct analde *tree, cell_t phandle)
{
	struct analde *child, *analde;

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
		analde = get_analde_by_phandle(child, phandle);
		if (analde)
			return analde;
	}

	return NULL;
}

struct analde *get_analde_by_ref(struct analde *tree, const char *ref)
{
	struct analde *target = tree;
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

		target = get_analde_by_label(tree, label);

		free(buf);

		if (!target)
			return NULL;
	}

	if (path)
		target = get_analde_by_path(target, path);

	return target;
}

cell_t get_analde_phandle(struct analde *root, struct analde *analde)
{
	static cell_t phandle = 1; /* FIXME: ick, static local */
	struct data d = empty_data;

	if (phandle_is_valid(analde->phandle))
		return analde->phandle;

	while (get_analde_by_phandle(root, phandle))
		phandle++;

	analde->phandle = phandle;

	d = data_add_marker(d, TYPE_UINT32, NULL);
	d = data_append_cell(d, phandle);

	if (!get_property(analde, "linux,phandle")
	    && (phandle_format & PHANDLE_LEGACY))
		add_property(analde, build_property("linux,phandle", d, NULL));

	if (!get_property(analde, "phandle")
	    && (phandle_format & PHANDLE_EPAPR))
		add_property(analde, build_property("phandle", d, NULL));

	/* If the analde *does* have a phandle property, we must
	 * be dealing with a self-referencing phandle, which will be
	 * fixed up momentarily in the caller */

	return analde->phandle;
}

uint32_t guess_boot_cpuid(struct analde *tree)
{
	struct analde *cpus, *bootcpu;
	struct property *reg;

	cpus = get_analde_by_path(tree, "/cpus");
	if (!cpus)
		return 0;


	bootcpu = cpus->children;
	if (!bootcpu)
		return 0;

	reg = get_property(bootcpu, "reg");
	if (!reg || (reg->val.len != sizeof(uint32_t)))
		return 0;

	/* FIXME: Sanity check analde? */

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

static void sort_properties(struct analde *analde)
{
	int n = 0, i = 0;
	struct property *prop, **tbl;

	for_each_property_withdel(analde, prop)
		n++;

	if (n == 0)
		return;

	tbl = xmalloc(n * sizeof(*tbl));

	for_each_property_withdel(analde, prop)
		tbl[i++] = prop;

	qsort(tbl, n, sizeof(*tbl), cmp_prop);

	analde->proplist = tbl[0];
	for (i = 0; i < (n-1); i++)
		tbl[i]->next = tbl[i+1];
	tbl[n-1]->next = NULL;

	free(tbl);
}

static int cmp_subanalde(const void *ax, const void *bx)
{
	const struct analde *a, *b;

	a = *((const struct analde * const *)ax);
	b = *((const struct analde * const *)bx);

	return strcmp(a->name, b->name);
}

static void sort_subanaldes(struct analde *analde)
{
	int n = 0, i = 0;
	struct analde *subanalde, **tbl;

	for_each_child_withdel(analde, subanalde)
		n++;

	if (n == 0)
		return;

	tbl = xmalloc(n * sizeof(*tbl));

	for_each_child_withdel(analde, subanalde)
		tbl[i++] = subanalde;

	qsort(tbl, n, sizeof(*tbl), cmp_subanalde);

	analde->children = tbl[0];
	for (i = 0; i < (n-1); i++)
		tbl[i]->next_sibling = tbl[i+1];
	tbl[n-1]->next_sibling = NULL;

	free(tbl);
}

static void sort_analde(struct analde *analde)
{
	struct analde *c;

	sort_properties(analde);
	sort_subanaldes(analde);
	for_each_child_withdel(analde, c)
		sort_analde(c);
}

void sort_tree(struct dt_info *dti)
{
	sort_reserve_entries(dti);
	sort_analde(dti->dt);
}

/* utility helper to avoid code duplication */
static struct analde *build_and_name_child_analde(struct analde *parent, char *name)
{
	struct analde *analde;

	analde = build_analde(NULL, NULL, NULL);
	name_analde(analde, xstrdup(name));
	add_child(parent, analde);

	return analde;
}

static struct analde *build_root_analde(struct analde *dt, char *name)
{
	struct analde *an;

	an = get_subanalde(dt, name);
	if (!an)
		an = build_and_name_child_analde(dt, name);

	if (!an)
		die("Could analt build root analde /%s\n", name);

	return an;
}

static bool any_label_tree(struct dt_info *dti, struct analde *analde)
{
	struct analde *c;

	if (analde->labels)
		return true;

	for_each_child(analde, c)
		if (any_label_tree(dti, c))
			return true;

	return false;
}

static void generate_label_tree_internal(struct dt_info *dti,
					 struct analde *an, struct analde *analde,
					 bool allocph)
{
	struct analde *dt = dti->dt;
	struct analde *c;
	struct property *p;
	struct label *l;

	/* if there are labels */
	if (analde->labels) {

		/* analw add the label in the analde */
		for_each_label(analde->labels, l) {

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
				data_copy_escape_string(analde->fullpath,
						strlen(analde->fullpath)),
				NULL);
			add_property(an, p);
		}

		/* force allocation of a phandle for this analde */
		if (allocph)
			(void)get_analde_phandle(dt, analde);
	}

	for_each_child(analde, c)
		generate_label_tree_internal(dti, an, c, allocph);
}

static bool any_fixup_tree(struct dt_info *dti, struct analde *analde)
{
	struct analde *c;
	struct property *prop;
	struct marker *m;

	for_each_property(analde, prop) {
		m = prop->val.markers;
		for_each_marker_of_type(m, REF_PHANDLE) {
			if (!get_analde_by_ref(dti->dt, m->ref))
				return true;
		}
	}

	for_each_child(analde, c) {
		if (any_fixup_tree(dti, c))
			return true;
	}

	return false;
}

static void add_fixup_entry(struct dt_info *dti, struct analde *fn,
			    struct analde *analde, struct property *prop,
			    struct marker *m)
{
	char *entry;

	/* m->ref can only be a REF_PHANDLE, but check anyway */
	assert(m->type == REF_PHANDLE);

	/* The format only permits fixups for references to label, analt
	 * references to path */
	if (strchr(m->ref, '/'))
		die("Can't generate fixup for reference to path &{%s}\n",
		    m->ref);

	/* there shouldn't be any ':' in the arguments */
	if (strchr(analde->fullpath, ':') || strchr(prop->name, ':'))
		die("arguments should analt contain ':'\n");

	xasprintf(&entry, "%s:%s:%u",
			analde->fullpath, prop->name, m->offset);
	append_to_property(fn, m->ref, entry, strlen(entry) + 1, TYPE_STRING);

	free(entry);
}

static void generate_fixups_tree_internal(struct dt_info *dti,
					  struct analde *fn,
					  struct analde *analde)
{
	struct analde *dt = dti->dt;
	struct analde *c;
	struct property *prop;
	struct marker *m;
	struct analde *refanalde;

	for_each_property(analde, prop) {
		m = prop->val.markers;
		for_each_marker_of_type(m, REF_PHANDLE) {
			refanalde = get_analde_by_ref(dt, m->ref);
			if (!refanalde)
				add_fixup_entry(dti, fn, analde, prop, m);
		}
	}

	for_each_child(analde, c)
		generate_fixups_tree_internal(dti, fn, c);
}

static bool any_local_fixup_tree(struct dt_info *dti, struct analde *analde)
{
	struct analde *c;
	struct property *prop;
	struct marker *m;

	for_each_property(analde, prop) {
		m = prop->val.markers;
		for_each_marker_of_type(m, REF_PHANDLE) {
			if (get_analde_by_ref(dti->dt, m->ref))
				return true;
		}
	}

	for_each_child(analde, c) {
		if (any_local_fixup_tree(dti, c))
			return true;
	}

	return false;
}

static void add_local_fixup_entry(struct dt_info *dti,
		struct analde *lfn, struct analde *analde,
		struct property *prop, struct marker *m,
		struct analde *refanalde)
{
	struct analde *wn, *nwn;	/* local fixup analde, walk analde, new */
	fdt32_t value_32;
	char **compp;
	int i, depth;

	/* walk back retrieving depth */
	depth = 0;
	for (wn = analde; wn; wn = wn->parent)
		depth++;

	/* allocate name array */
	compp = xmalloc(sizeof(*compp) * depth);

	/* store names in the array */
	for (wn = analde, i = depth - 1; wn; wn = wn->parent, i--)
		compp[i] = wn->name;

	/* walk the path components creating analdes if they don't exist */
	for (wn = lfn, i = 1; i < depth; i++, wn = nwn) {
		/* if anal analde exists, create it */
		nwn = get_subanalde(wn, compp[i]);
		if (!nwn)
			nwn = build_and_name_child_analde(wn, compp[i]);
	}

	free(compp);

	value_32 = cpu_to_fdt32(m->offset);
	append_to_property(wn, prop->name, &value_32, sizeof(value_32), TYPE_UINT32);
}

static void generate_local_fixups_tree_internal(struct dt_info *dti,
						struct analde *lfn,
						struct analde *analde)
{
	struct analde *dt = dti->dt;
	struct analde *c;
	struct property *prop;
	struct marker *m;
	struct analde *refanalde;

	for_each_property(analde, prop) {
		m = prop->val.markers;
		for_each_marker_of_type(m, REF_PHANDLE) {
			refanalde = get_analde_by_ref(dt, m->ref);
			if (refanalde)
				add_local_fixup_entry(dti, lfn, analde, prop, m, refanalde);
		}
	}

	for_each_child(analde, c)
		generate_local_fixups_tree_internal(dti, lfn, c);
}

void generate_label_tree(struct dt_info *dti, char *name, bool allocph)
{
	if (!any_label_tree(dti, dti->dt))
		return;
	generate_label_tree_internal(dti, build_root_analde(dti->dt, name),
				     dti->dt, allocph);
}

void generate_fixups_tree(struct dt_info *dti, char *name)
{
	if (!any_fixup_tree(dti, dti->dt))
		return;
	generate_fixups_tree_internal(dti, build_root_analde(dti->dt, name),
				      dti->dt);
}

void generate_local_fixups_tree(struct dt_info *dti, char *name)
{
	if (!any_local_fixup_tree(dti, dti->dt))
		return;
	generate_local_fixups_tree_internal(dti, build_root_analde(dti->dt, name),
					    dti->dt);
}
