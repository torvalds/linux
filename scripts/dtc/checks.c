/*
 * (C) Copyright David Gibson <dwg@au1.ibm.com>, IBM Corporation.  2007.
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *                                                                   USA
 */

#include "dtc.h"

#ifdef TRACE_CHECKS
#define TRACE(c, ...) \
	do { \
		fprintf(stderr, "=== %s: ", (c)->name); \
		fprintf(stderr, __VA_ARGS__); \
		fprintf(stderr, "\n"); \
	} while (0)
#else
#define TRACE(c, fmt, ...)	do { } while (0)
#endif

enum checkstatus {
	UNCHECKED = 0,
	PREREQ,
	PASSED,
	FAILED,
};

struct check;

typedef void (*tree_check_fn)(struct check *c, struct node *dt);
typedef void (*node_check_fn)(struct check *c, struct node *dt, struct node *node);
typedef void (*prop_check_fn)(struct check *c, struct node *dt,
			      struct node *node, struct property *prop);

struct check {
	const char *name;
	tree_check_fn tree_fn;
	node_check_fn node_fn;
	prop_check_fn prop_fn;
	void *data;
	bool warn, error;
	enum checkstatus status;
	bool inprogress;
	int num_prereqs;
	struct check **prereq;
};

#define CHECK_ENTRY(nm, tfn, nfn, pfn, d, w, e, ...)	       \
	static struct check *nm##_prereqs[] = { __VA_ARGS__ }; \
	static struct check nm = { \
		.name = #nm, \
		.tree_fn = (tfn), \
		.node_fn = (nfn), \
		.prop_fn = (pfn), \
		.data = (d), \
		.warn = (w), \
		.error = (e), \
		.status = UNCHECKED, \
		.num_prereqs = ARRAY_SIZE(nm##_prereqs), \
		.prereq = nm##_prereqs, \
	};
#define WARNING(nm, tfn, nfn, pfn, d, ...) \
	CHECK_ENTRY(nm, tfn, nfn, pfn, d, true, false, __VA_ARGS__)
#define ERROR(nm, tfn, nfn, pfn, d, ...) \
	CHECK_ENTRY(nm, tfn, nfn, pfn, d, false, true, __VA_ARGS__)
#define CHECK(nm, tfn, nfn, pfn, d, ...) \
	CHECK_ENTRY(nm, tfn, nfn, pfn, d, false, false, __VA_ARGS__)

#define TREE_WARNING(nm, d, ...) \
	WARNING(nm, check_##nm, NULL, NULL, d, __VA_ARGS__)
#define TREE_ERROR(nm, d, ...) \
	ERROR(nm, check_##nm, NULL, NULL, d, __VA_ARGS__)
#define TREE_CHECK(nm, d, ...) \
	CHECK(nm, check_##nm, NULL, NULL, d, __VA_ARGS__)
#define NODE_WARNING(nm, d, ...) \
	WARNING(nm, NULL, check_##nm, NULL, d,  __VA_ARGS__)
#define NODE_ERROR(nm, d, ...) \
	ERROR(nm, NULL, check_##nm, NULL, d, __VA_ARGS__)
#define NODE_CHECK(nm, d, ...) \
	CHECK(nm, NULL, check_##nm, NULL, d, __VA_ARGS__)
#define PROP_WARNING(nm, d, ...) \
	WARNING(nm, NULL, NULL, check_##nm, d, __VA_ARGS__)
#define PROP_ERROR(nm, d, ...) \
	ERROR(nm, NULL, NULL, check_##nm, d, __VA_ARGS__)
#define PROP_CHECK(nm, d, ...) \
	CHECK(nm, NULL, NULL, check_##nm, d, __VA_ARGS__)

#ifdef __GNUC__
static inline void check_msg(struct check *c, const char *fmt, ...) __attribute__((format (printf, 2, 3)));
#endif
static inline void check_msg(struct check *c, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);

	if ((c->warn && (quiet < 1))
	    || (c->error && (quiet < 2))) {
		fprintf(stderr, "%s (%s): ",
			(c->error) ? "ERROR" : "Warning", c->name);
		vfprintf(stderr, fmt, ap);
		fprintf(stderr, "\n");
	}
	va_end(ap);
}

#define FAIL(c, ...) \
	do { \
		TRACE((c), "\t\tFAILED at %s:%d", __FILE__, __LINE__); \
		(c)->status = FAILED; \
		check_msg((c), __VA_ARGS__); \
	} while (0)

static void check_nodes_props(struct check *c, struct node *dt, struct node *node)
{
	struct node *child;
	struct property *prop;

	TRACE(c, "%s", node->fullpath);
	if (c->node_fn)
		c->node_fn(c, dt, node);

	if (c->prop_fn)
		for_each_property(node, prop) {
			TRACE(c, "%s\t'%s'", node->fullpath, prop->name);
			c->prop_fn(c, dt, node, prop);
		}

	for_each_child(node, child)
		check_nodes_props(c, dt, child);
}

static bool run_check(struct check *c, struct node *dt)
{
	bool error = false;
	int i;

	assert(!c->inprogress);

	if (c->status != UNCHECKED)
		goto out;

	c->inprogress = true;

	for (i = 0; i < c->num_prereqs; i++) {
		struct check *prq = c->prereq[i];
		error = error || run_check(prq, dt);
		if (prq->status != PASSED) {
			c->status = PREREQ;
			check_msg(c, "Failed prerequisite '%s'",
				  c->prereq[i]->name);
		}
	}

	if (c->status != UNCHECKED)
		goto out;

	if (c->node_fn || c->prop_fn)
		check_nodes_props(c, dt, dt);

	if (c->tree_fn)
		c->tree_fn(c, dt);
	if (c->status == UNCHECKED)
		c->status = PASSED;

	TRACE(c, "\tCompleted, status %d", c->status);

out:
	c->inprogress = false;
	if ((c->status != PASSED) && (c->error))
		error = true;
	return error;
}

/*
 * Utility check functions
 */

/* A check which always fails, for testing purposes only */
static inline void check_always_fail(struct check *c, struct node *dt)
{
	FAIL(c, "always_fail check");
}
TREE_CHECK(always_fail, NULL);

static void check_is_string(struct check *c, struct node *root,
			    struct node *node)
{
	struct property *prop;
	char *propname = c->data;

	prop = get_property(node, propname);
	if (!prop)
		return; /* Not present, assumed ok */

	if (!data_is_one_string(prop->val))
		FAIL(c, "\"%s\" property in %s is not a string",
		     propname, node->fullpath);
}
#define WARNING_IF_NOT_STRING(nm, propname) \
	WARNING(nm, NULL, check_is_string, NULL, (propname))
#define ERROR_IF_NOT_STRING(nm, propname) \
	ERROR(nm, NULL, check_is_string, NULL, (propname))

static void check_is_cell(struct check *c, struct node *root,
			  struct node *node)
{
	struct property *prop;
	char *propname = c->data;

	prop = get_property(node, propname);
	if (!prop)
		return; /* Not present, assumed ok */

	if (prop->val.len != sizeof(cell_t))
		FAIL(c, "\"%s\" property in %s is not a single cell",
		     propname, node->fullpath);
}
#define WARNING_IF_NOT_CELL(nm, propname) \
	WARNING(nm, NULL, check_is_cell, NULL, (propname))
#define ERROR_IF_NOT_CELL(nm, propname) \
	ERROR(nm, NULL, check_is_cell, NULL, (propname))

/*
 * Structural check functions
 */

static void check_duplicate_node_names(struct check *c, struct node *dt,
				       struct node *node)
{
	struct node *child, *child2;

	for_each_child(node, child)
		for (child2 = child->next_sibling;
		     child2;
		     child2 = child2->next_sibling)
			if (streq(child->name, child2->name))
				FAIL(c, "Duplicate node name %s",
				     child->fullpath);
}
NODE_ERROR(duplicate_node_names, NULL);

static void check_duplicate_property_names(struct check *c, struct node *dt,
					   struct node *node)
{
	struct property *prop, *prop2;

	for_each_property(node, prop) {
		for (prop2 = prop->next; prop2; prop2 = prop2->next) {
			if (prop2->deleted)
				continue;
			if (streq(prop->name, prop2->name))
				FAIL(c, "Duplicate property name %s in %s",
				     prop->name, node->fullpath);
		}
	}
}
NODE_ERROR(duplicate_property_names, NULL);

#define LOWERCASE	"abcdefghijklmnopqrstuvwxyz"
#define UPPERCASE	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define DIGITS		"0123456789"
#define PROPNODECHARS	LOWERCASE UPPERCASE DIGITS ",._+*#?-"

static void check_node_name_chars(struct check *c, struct node *dt,
				  struct node *node)
{
	int n = strspn(node->name, c->data);

	if (n < strlen(node->name))
		FAIL(c, "Bad character '%c' in node %s",
		     node->name[n], node->fullpath);
}
NODE_ERROR(node_name_chars, PROPNODECHARS "@");

static void check_node_name_format(struct check *c, struct node *dt,
				   struct node *node)
{
	if (strchr(get_unitname(node), '@'))
		FAIL(c, "Node %s has multiple '@' characters in name",
		     node->fullpath);
}
NODE_ERROR(node_name_format, NULL, &node_name_chars);

static void check_property_name_chars(struct check *c, struct node *dt,
				      struct node *node, struct property *prop)
{
	int n = strspn(prop->name, c->data);

	if (n < strlen(prop->name))
		FAIL(c, "Bad character '%c' in property name \"%s\", node %s",
		     prop->name[n], prop->name, node->fullpath);
}
PROP_ERROR(property_name_chars, PROPNODECHARS);

#define DESCLABEL_FMT	"%s%s%s%s%s"
#define DESCLABEL_ARGS(node,prop,mark)		\
	((mark) ? "value of " : ""),		\
	((prop) ? "'" : ""), \
	((prop) ? (prop)->name : ""), \
	((prop) ? "' in " : ""), (node)->fullpath

static void check_duplicate_label(struct check *c, struct node *dt,
				  const char *label, struct node *node,
				  struct property *prop, struct marker *mark)
{
	struct node *othernode = NULL;
	struct property *otherprop = NULL;
	struct marker *othermark = NULL;

	othernode = get_node_by_label(dt, label);

	if (!othernode)
		otherprop = get_property_by_label(dt, label, &othernode);
	if (!othernode)
		othermark = get_marker_label(dt, label, &othernode,
					       &otherprop);

	if (!othernode)
		return;

	if ((othernode != node) || (otherprop != prop) || (othermark != mark))
		FAIL(c, "Duplicate label '%s' on " DESCLABEL_FMT
		     " and " DESCLABEL_FMT,
		     label, DESCLABEL_ARGS(node, prop, mark),
		     DESCLABEL_ARGS(othernode, otherprop, othermark));
}

static void check_duplicate_label_node(struct check *c, struct node *dt,
				       struct node *node)
{
	struct label *l;

	for_each_label(node->labels, l)
		check_duplicate_label(c, dt, l->label, node, NULL, NULL);
}
static void check_duplicate_label_prop(struct check *c, struct node *dt,
				       struct node *node, struct property *prop)
{
	struct marker *m = prop->val.markers;
	struct label *l;

	for_each_label(prop->labels, l)
		check_duplicate_label(c, dt, l->label, node, prop, NULL);

	for_each_marker_of_type(m, LABEL)
		check_duplicate_label(c, dt, m->ref, node, prop, m);
}
ERROR(duplicate_label, NULL, check_duplicate_label_node,
      check_duplicate_label_prop, NULL);

static void check_explicit_phandles(struct check *c, struct node *root,
				    struct node *node, struct property *prop)
{
	struct marker *m;
	struct node *other;
	cell_t phandle;

	if (!streq(prop->name, "phandle")
	    && !streq(prop->name, "linux,phandle"))
		return;

	if (prop->val.len != sizeof(cell_t)) {
		FAIL(c, "%s has bad length (%d) %s property",
		     node->fullpath, prop->val.len, prop->name);
		return;
	}

	m = prop->val.markers;
	for_each_marker_of_type(m, REF_PHANDLE) {
		assert(m->offset == 0);
		if (node != get_node_by_ref(root, m->ref))
			/* "Set this node's phandle equal to some
			 * other node's phandle".  That's nonsensical
			 * by construction. */ {
			FAIL(c, "%s in %s is a reference to another node",
			     prop->name, node->fullpath);
			return;
		}
		/* But setting this node's phandle equal to its own
		 * phandle is allowed - that means allocate a unique
		 * phandle for this node, even if it's not otherwise
		 * referenced.  The value will be filled in later, so
		 * no further checking for now. */
		return;
	}

	phandle = propval_cell(prop);

	if ((phandle == 0) || (phandle == -1)) {
		FAIL(c, "%s has bad value (0x%x) in %s property",
		     node->fullpath, phandle, prop->name);
		return;
	}

	if (node->phandle && (node->phandle != phandle))
		FAIL(c, "%s has %s property which replaces existing phandle information",
		     node->fullpath, prop->name);

	other = get_node_by_phandle(root, phandle);
	if (other && (other != node)) {
		FAIL(c, "%s has duplicated phandle 0x%x (seen before at %s)",
		     node->fullpath, phandle, other->fullpath);
		return;
	}

	node->phandle = phandle;
}
PROP_ERROR(explicit_phandles, NULL);

static void check_name_properties(struct check *c, struct node *root,
				  struct node *node)
{
	struct property **pp, *prop = NULL;

	for (pp = &node->proplist; *pp; pp = &((*pp)->next))
		if (streq((*pp)->name, "name")) {
			prop = *pp;
			break;
		}

	if (!prop)
		return; /* No name property, that's fine */

	if ((prop->val.len != node->basenamelen+1)
	    || (memcmp(prop->val.val, node->name, node->basenamelen) != 0)) {
		FAIL(c, "\"name\" property in %s is incorrect (\"%s\" instead"
		     " of base node name)", node->fullpath, prop->val.val);
	} else {
		/* The name property is correct, and therefore redundant.
		 * Delete it */
		*pp = prop->next;
		free(prop->name);
		data_free(prop->val);
		free(prop);
	}
}
ERROR_IF_NOT_STRING(name_is_string, "name");
NODE_ERROR(name_properties, NULL, &name_is_string);

/*
 * Reference fixup functions
 */

static void fixup_phandle_references(struct check *c, struct node *dt,
				     struct node *node, struct property *prop)
{
	struct marker *m = prop->val.markers;
	struct node *refnode;
	cell_t phandle;

	for_each_marker_of_type(m, REF_PHANDLE) {
		assert(m->offset + sizeof(cell_t) <= prop->val.len);

		refnode = get_node_by_ref(dt, m->ref);
		if (! refnode) {
			FAIL(c, "Reference to non-existent node or label \"%s\"\n",
			     m->ref);
			continue;
		}

		phandle = get_node_phandle(dt, refnode);
		*((cell_t *)(prop->val.val + m->offset)) = cpu_to_fdt32(phandle);
	}
}
ERROR(phandle_references, NULL, NULL, fixup_phandle_references, NULL,
      &duplicate_node_names, &explicit_phandles);

static void fixup_path_references(struct check *c, struct node *dt,
				  struct node *node, struct property *prop)
{
	struct marker *m = prop->val.markers;
	struct node *refnode;
	char *path;

	for_each_marker_of_type(m, REF_PATH) {
		assert(m->offset <= prop->val.len);

		refnode = get_node_by_ref(dt, m->ref);
		if (!refnode) {
			FAIL(c, "Reference to non-existent node or label \"%s\"\n",
			     m->ref);
			continue;
		}

		path = refnode->fullpath;
		prop->val = data_insert_at_marker(prop->val, m, path,
						  strlen(path) + 1);
	}
}
ERROR(path_references, NULL, NULL, fixup_path_references, NULL,
      &duplicate_node_names);

/*
 * Semantic checks
 */
WARNING_IF_NOT_CELL(address_cells_is_cell, "#address-cells");
WARNING_IF_NOT_CELL(size_cells_is_cell, "#size-cells");
WARNING_IF_NOT_CELL(interrupt_cells_is_cell, "#interrupt-cells");

WARNING_IF_NOT_STRING(device_type_is_string, "device_type");
WARNING_IF_NOT_STRING(model_is_string, "model");
WARNING_IF_NOT_STRING(status_is_string, "status");

static void fixup_addr_size_cells(struct check *c, struct node *dt,
				  struct node *node)
{
	struct property *prop;

	node->addr_cells = -1;
	node->size_cells = -1;

	prop = get_property(node, "#address-cells");
	if (prop)
		node->addr_cells = propval_cell(prop);

	prop = get_property(node, "#size-cells");
	if (prop)
		node->size_cells = propval_cell(prop);
}
WARNING(addr_size_cells, NULL, fixup_addr_size_cells, NULL, NULL,
	&address_cells_is_cell, &size_cells_is_cell);

#define node_addr_cells(n) \
	(((n)->addr_cells == -1) ? 2 : (n)->addr_cells)
#define node_size_cells(n) \
	(((n)->size_cells == -1) ? 1 : (n)->size_cells)

static void check_reg_format(struct check *c, struct node *dt,
			     struct node *node)
{
	struct property *prop;
	int addr_cells, size_cells, entrylen;

	prop = get_property(node, "reg");
	if (!prop)
		return; /* No "reg", that's fine */

	if (!node->parent) {
		FAIL(c, "Root node has a \"reg\" property");
		return;
	}

	if (prop->val.len == 0)
		FAIL(c, "\"reg\" property in %s is empty", node->fullpath);

	addr_cells = node_addr_cells(node->parent);
	size_cells = node_size_cells(node->parent);
	entrylen = (addr_cells + size_cells) * sizeof(cell_t);

	if ((prop->val.len % entrylen) != 0)
		FAIL(c, "\"reg\" property in %s has invalid length (%d bytes) "
		     "(#address-cells == %d, #size-cells == %d)",
		     node->fullpath, prop->val.len, addr_cells, size_cells);
}
NODE_WARNING(reg_format, NULL, &addr_size_cells);

static void check_ranges_format(struct check *c, struct node *dt,
				struct node *node)
{
	struct property *prop;
	int c_addr_cells, p_addr_cells, c_size_cells, p_size_cells, entrylen;

	prop = get_property(node, "ranges");
	if (!prop)
		return;

	if (!node->parent) {
		FAIL(c, "Root node has a \"ranges\" property");
		return;
	}

	p_addr_cells = node_addr_cells(node->parent);
	p_size_cells = node_size_cells(node->parent);
	c_addr_cells = node_addr_cells(node);
	c_size_cells = node_size_cells(node);
	entrylen = (p_addr_cells + c_addr_cells + c_size_cells) * sizeof(cell_t);

	if (prop->val.len == 0) {
		if (p_addr_cells != c_addr_cells)
			FAIL(c, "%s has empty \"ranges\" property but its "
			     "#address-cells (%d) differs from %s (%d)",
			     node->fullpath, c_addr_cells, node->parent->fullpath,
			     p_addr_cells);
		if (p_size_cells != c_size_cells)
			FAIL(c, "%s has empty \"ranges\" property but its "
			     "#size-cells (%d) differs from %s (%d)",
			     node->fullpath, c_size_cells, node->parent->fullpath,
			     p_size_cells);
	} else if ((prop->val.len % entrylen) != 0) {
		FAIL(c, "\"ranges\" property in %s has invalid length (%d bytes) "
		     "(parent #address-cells == %d, child #address-cells == %d, "
		     "#size-cells == %d)", node->fullpath, prop->val.len,
		     p_addr_cells, c_addr_cells, c_size_cells);
	}
}
NODE_WARNING(ranges_format, NULL, &addr_size_cells);

/*
 * Style checks
 */
static void check_avoid_default_addr_size(struct check *c, struct node *dt,
					  struct node *node)
{
	struct property *reg, *ranges;

	if (!node->parent)
		return; /* Ignore root node */

	reg = get_property(node, "reg");
	ranges = get_property(node, "ranges");

	if (!reg && !ranges)
		return;

	if (node->parent->addr_cells == -1)
		FAIL(c, "Relying on default #address-cells value for %s",
		     node->fullpath);

	if (node->parent->size_cells == -1)
		FAIL(c, "Relying on default #size-cells value for %s",
		     node->fullpath);
}
NODE_WARNING(avoid_default_addr_size, NULL, &addr_size_cells);

static void check_obsolete_chosen_interrupt_controller(struct check *c,
						       struct node *dt)
{
	struct node *chosen;
	struct property *prop;

	chosen = get_node_by_path(dt, "/chosen");
	if (!chosen)
		return;

	prop = get_property(chosen, "interrupt-controller");
	if (prop)
		FAIL(c, "/chosen has obsolete \"interrupt-controller\" "
		     "property");
}
TREE_WARNING(obsolete_chosen_interrupt_controller, NULL);

static struct check *check_table[] = {
	&duplicate_node_names, &duplicate_property_names,
	&node_name_chars, &node_name_format, &property_name_chars,
	&name_is_string, &name_properties,

	&duplicate_label,

	&explicit_phandles,
	&phandle_references, &path_references,

	&address_cells_is_cell, &size_cells_is_cell, &interrupt_cells_is_cell,
	&device_type_is_string, &model_is_string, &status_is_string,

	&addr_size_cells, &reg_format, &ranges_format,

	&avoid_default_addr_size,
	&obsolete_chosen_interrupt_controller,

	&always_fail,
};

static void enable_warning_error(struct check *c, bool warn, bool error)
{
	int i;

	/* Raising level, also raise it for prereqs */
	if ((warn && !c->warn) || (error && !c->error))
		for (i = 0; i < c->num_prereqs; i++)
			enable_warning_error(c->prereq[i], warn, error);

	c->warn = c->warn || warn;
	c->error = c->error || error;
}

static void disable_warning_error(struct check *c, bool warn, bool error)
{
	int i;

	/* Lowering level, also lower it for things this is the prereq
	 * for */
	if ((warn && c->warn) || (error && c->error)) {
		for (i = 0; i < ARRAY_SIZE(check_table); i++) {
			struct check *cc = check_table[i];
			int j;

			for (j = 0; j < cc->num_prereqs; j++)
				if (cc->prereq[j] == c)
					disable_warning_error(cc, warn, error);
		}
	}

	c->warn = c->warn && !warn;
	c->error = c->error && !error;
}

void parse_checks_option(bool warn, bool error, const char *arg)
{
	int i;
	const char *name = arg;
	bool enable = true;

	if ((strncmp(arg, "no-", 3) == 0)
	    || (strncmp(arg, "no_", 3) == 0)) {
		name = arg + 3;
		enable = false;
	}

	for (i = 0; i < ARRAY_SIZE(check_table); i++) {
		struct check *c = check_table[i];

		if (streq(c->name, name)) {
			if (enable)
				enable_warning_error(c, warn, error);
			else
				disable_warning_error(c, warn, error);
			return;
		}
	}

	die("Unrecognized check name \"%s\"\n", name);
}

void process_checks(bool force, struct boot_info *bi)
{
	struct node *dt = bi->dt;
	int i;
	int error = 0;

	for (i = 0; i < ARRAY_SIZE(check_table); i++) {
		struct check *c = check_table[i];

		if (c->warn || c->error)
			error = error || run_check(c, dt);
	}

	if (error) {
		if (!force) {
			fprintf(stderr, "ERROR: Input tree has errors, aborting "
				"(use -f to force output)\n");
			exit(2);
		} else if (quiet < 3) {
			fprintf(stderr, "Warning: Input tree has errors, "
				"output forced\n");
		}
	}
}
