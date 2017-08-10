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

typedef void (*check_fn)(struct check *c, struct dt_info *dti, struct node *node);

struct check {
	const char *name;
	check_fn fn;
	void *data;
	bool warn, error;
	enum checkstatus status;
	bool inprogress;
	int num_prereqs;
	struct check **prereq;
};

#define CHECK_ENTRY(_nm, _fn, _d, _w, _e, ...)	       \
	static struct check *_nm##_prereqs[] = { __VA_ARGS__ }; \
	static struct check _nm = { \
		.name = #_nm, \
		.fn = (_fn), \
		.data = (_d), \
		.warn = (_w), \
		.error = (_e), \
		.status = UNCHECKED, \
		.num_prereqs = ARRAY_SIZE(_nm##_prereqs), \
		.prereq = _nm##_prereqs, \
	};
#define WARNING(_nm, _fn, _d, ...) \
	CHECK_ENTRY(_nm, _fn, _d, true, false, __VA_ARGS__)
#define ERROR(_nm, _fn, _d, ...) \
	CHECK_ENTRY(_nm, _fn, _d, false, true, __VA_ARGS__)
#define CHECK(_nm, _fn, _d, ...) \
	CHECK_ENTRY(_nm, _fn, _d, false, false, __VA_ARGS__)

static inline void  PRINTF(3, 4) check_msg(struct check *c, struct dt_info *dti,
					   const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);

	if ((c->warn && (quiet < 1))
	    || (c->error && (quiet < 2))) {
		fprintf(stderr, "%s: %s (%s): ",
			strcmp(dti->outname, "-") ? dti->outname : "<stdout>",
			(c->error) ? "ERROR" : "Warning", c->name);
		vfprintf(stderr, fmt, ap);
		fprintf(stderr, "\n");
	}
	va_end(ap);
}

#define FAIL(c, dti, ...)						\
	do {								\
		TRACE((c), "\t\tFAILED at %s:%d", __FILE__, __LINE__);	\
		(c)->status = FAILED;					\
		check_msg((c), dti, __VA_ARGS__);			\
	} while (0)

static void check_nodes_props(struct check *c, struct dt_info *dti, struct node *node)
{
	struct node *child;

	TRACE(c, "%s", node->fullpath);
	if (c->fn)
		c->fn(c, dti, node);

	for_each_child(node, child)
		check_nodes_props(c, dti, child);
}

static bool run_check(struct check *c, struct dt_info *dti)
{
	struct node *dt = dti->dt;
	bool error = false;
	int i;

	assert(!c->inprogress);

	if (c->status != UNCHECKED)
		goto out;

	c->inprogress = true;

	for (i = 0; i < c->num_prereqs; i++) {
		struct check *prq = c->prereq[i];
		error = error || run_check(prq, dti);
		if (prq->status != PASSED) {
			c->status = PREREQ;
			check_msg(c, dti, "Failed prerequisite '%s'",
				  c->prereq[i]->name);
		}
	}

	if (c->status != UNCHECKED)
		goto out;

	check_nodes_props(c, dti, dt);

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
static inline void check_always_fail(struct check *c, struct dt_info *dti,
				     struct node *node)
{
	FAIL(c, dti, "always_fail check");
}
CHECK(always_fail, check_always_fail, NULL);

static void check_is_string(struct check *c, struct dt_info *dti,
			    struct node *node)
{
	struct property *prop;
	char *propname = c->data;

	prop = get_property(node, propname);
	if (!prop)
		return; /* Not present, assumed ok */

	if (!data_is_one_string(prop->val))
		FAIL(c, dti, "\"%s\" property in %s is not a string",
		     propname, node->fullpath);
}
#define WARNING_IF_NOT_STRING(nm, propname) \
	WARNING(nm, check_is_string, (propname))
#define ERROR_IF_NOT_STRING(nm, propname) \
	ERROR(nm, check_is_string, (propname))

static void check_is_cell(struct check *c, struct dt_info *dti,
			  struct node *node)
{
	struct property *prop;
	char *propname = c->data;

	prop = get_property(node, propname);
	if (!prop)
		return; /* Not present, assumed ok */

	if (prop->val.len != sizeof(cell_t))
		FAIL(c, dti, "\"%s\" property in %s is not a single cell",
		     propname, node->fullpath);
}
#define WARNING_IF_NOT_CELL(nm, propname) \
	WARNING(nm, check_is_cell, (propname))
#define ERROR_IF_NOT_CELL(nm, propname) \
	ERROR(nm, check_is_cell, (propname))

/*
 * Structural check functions
 */

static void check_duplicate_node_names(struct check *c, struct dt_info *dti,
				       struct node *node)
{
	struct node *child, *child2;

	for_each_child(node, child)
		for (child2 = child->next_sibling;
		     child2;
		     child2 = child2->next_sibling)
			if (streq(child->name, child2->name))
				FAIL(c, dti, "Duplicate node name %s",
				     child->fullpath);
}
ERROR(duplicate_node_names, check_duplicate_node_names, NULL);

static void check_duplicate_property_names(struct check *c, struct dt_info *dti,
					   struct node *node)
{
	struct property *prop, *prop2;

	for_each_property(node, prop) {
		for (prop2 = prop->next; prop2; prop2 = prop2->next) {
			if (prop2->deleted)
				continue;
			if (streq(prop->name, prop2->name))
				FAIL(c, dti, "Duplicate property name %s in %s",
				     prop->name, node->fullpath);
		}
	}
}
ERROR(duplicate_property_names, check_duplicate_property_names, NULL);

#define LOWERCASE	"abcdefghijklmnopqrstuvwxyz"
#define UPPERCASE	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define DIGITS		"0123456789"
#define PROPNODECHARS	LOWERCASE UPPERCASE DIGITS ",._+*#?-"
#define PROPNODECHARSSTRICT	LOWERCASE UPPERCASE DIGITS ",-"

static void check_node_name_chars(struct check *c, struct dt_info *dti,
				  struct node *node)
{
	int n = strspn(node->name, c->data);

	if (n < strlen(node->name))
		FAIL(c, dti, "Bad character '%c' in node %s",
		     node->name[n], node->fullpath);
}
ERROR(node_name_chars, check_node_name_chars, PROPNODECHARS "@");

static void check_node_name_chars_strict(struct check *c, struct dt_info *dti,
					 struct node *node)
{
	int n = strspn(node->name, c->data);

	if (n < node->basenamelen)
		FAIL(c, dti, "Character '%c' not recommended in node %s",
		     node->name[n], node->fullpath);
}
CHECK(node_name_chars_strict, check_node_name_chars_strict, PROPNODECHARSSTRICT);

static void check_node_name_format(struct check *c, struct dt_info *dti,
				   struct node *node)
{
	if (strchr(get_unitname(node), '@'))
		FAIL(c, dti, "Node %s has multiple '@' characters in name",
		     node->fullpath);
}
ERROR(node_name_format, check_node_name_format, NULL, &node_name_chars);

static void check_unit_address_vs_reg(struct check *c, struct dt_info *dti,
				      struct node *node)
{
	const char *unitname = get_unitname(node);
	struct property *prop = get_property(node, "reg");

	if (!prop) {
		prop = get_property(node, "ranges");
		if (prop && !prop->val.len)
			prop = NULL;
	}

	if (prop) {
		if (!unitname[0])
			FAIL(c, dti, "Node %s has a reg or ranges property, but no unit name",
			    node->fullpath);
	} else {
		if (unitname[0])
			FAIL(c, dti, "Node %s has a unit name, but no reg property",
			    node->fullpath);
	}
}
WARNING(unit_address_vs_reg, check_unit_address_vs_reg, NULL);

static void check_property_name_chars(struct check *c, struct dt_info *dti,
				      struct node *node)
{
	struct property *prop;

	for_each_property(node, prop) {
		int n = strspn(prop->name, c->data);

		if (n < strlen(prop->name))
			FAIL(c, dti, "Bad character '%c' in property name \"%s\", node %s",
			     prop->name[n], prop->name, node->fullpath);
	}
}
ERROR(property_name_chars, check_property_name_chars, PROPNODECHARS);

static void check_property_name_chars_strict(struct check *c,
					     struct dt_info *dti,
					     struct node *node)
{
	struct property *prop;

	for_each_property(node, prop) {
		const char *name = prop->name;
		int n = strspn(name, c->data);

		if (n == strlen(prop->name))
			continue;

		/* Certain names are whitelisted */
		if (streq(name, "device_type"))
			continue;

		/*
		 * # is only allowed at the beginning of property names not counting
		 * the vendor prefix.
		 */
		if (name[n] == '#' && ((n == 0) || (name[n-1] == ','))) {
			name += n + 1;
			n = strspn(name, c->data);
		}
		if (n < strlen(name))
			FAIL(c, dti, "Character '%c' not recommended in property name \"%s\", node %s",
			     name[n], prop->name, node->fullpath);
	}
}
CHECK(property_name_chars_strict, check_property_name_chars_strict, PROPNODECHARSSTRICT);

#define DESCLABEL_FMT	"%s%s%s%s%s"
#define DESCLABEL_ARGS(node,prop,mark)		\
	((mark) ? "value of " : ""),		\
	((prop) ? "'" : ""), \
	((prop) ? (prop)->name : ""), \
	((prop) ? "' in " : ""), (node)->fullpath

static void check_duplicate_label(struct check *c, struct dt_info *dti,
				  const char *label, struct node *node,
				  struct property *prop, struct marker *mark)
{
	struct node *dt = dti->dt;
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
		FAIL(c, dti, "Duplicate label '%s' on " DESCLABEL_FMT
		     " and " DESCLABEL_FMT,
		     label, DESCLABEL_ARGS(node, prop, mark),
		     DESCLABEL_ARGS(othernode, otherprop, othermark));
}

static void check_duplicate_label_node(struct check *c, struct dt_info *dti,
				       struct node *node)
{
	struct label *l;
	struct property *prop;

	for_each_label(node->labels, l)
		check_duplicate_label(c, dti, l->label, node, NULL, NULL);

	for_each_property(node, prop) {
		struct marker *m = prop->val.markers;

		for_each_label(prop->labels, l)
			check_duplicate_label(c, dti, l->label, node, prop, NULL);

		for_each_marker_of_type(m, LABEL)
			check_duplicate_label(c, dti, m->ref, node, prop, m);
	}
}
ERROR(duplicate_label, check_duplicate_label_node, NULL);

static cell_t check_phandle_prop(struct check *c, struct dt_info *dti,
				 struct node *node, const char *propname)
{
	struct node *root = dti->dt;
	struct property *prop;
	struct marker *m;
	cell_t phandle;

	prop = get_property(node, propname);
	if (!prop)
		return 0;

	if (prop->val.len != sizeof(cell_t)) {
		FAIL(c, dti, "%s has bad length (%d) %s property",
		     node->fullpath, prop->val.len, prop->name);
		return 0;
	}

	m = prop->val.markers;
	for_each_marker_of_type(m, REF_PHANDLE) {
		assert(m->offset == 0);
		if (node != get_node_by_ref(root, m->ref))
			/* "Set this node's phandle equal to some
			 * other node's phandle".  That's nonsensical
			 * by construction. */ {
			FAIL(c, dti, "%s in %s is a reference to another node",
			     prop->name, node->fullpath);
		}
		/* But setting this node's phandle equal to its own
		 * phandle is allowed - that means allocate a unique
		 * phandle for this node, even if it's not otherwise
		 * referenced.  The value will be filled in later, so
		 * we treat it as having no phandle data for now. */
		return 0;
	}

	phandle = propval_cell(prop);

	if ((phandle == 0) || (phandle == -1)) {
		FAIL(c, dti, "%s has bad value (0x%x) in %s property",
		     node->fullpath, phandle, prop->name);
		return 0;
	}

	return phandle;
}

static void check_explicit_phandles(struct check *c, struct dt_info *dti,
				    struct node *node)
{
	struct node *root = dti->dt;
	struct node *other;
	cell_t phandle, linux_phandle;

	/* Nothing should have assigned phandles yet */
	assert(!node->phandle);

	phandle = check_phandle_prop(c, dti, node, "phandle");

	linux_phandle = check_phandle_prop(c, dti, node, "linux,phandle");

	if (!phandle && !linux_phandle)
		/* No valid phandles; nothing further to check */
		return;

	if (linux_phandle && phandle && (phandle != linux_phandle))
		FAIL(c, dti, "%s has mismatching 'phandle' and 'linux,phandle'"
		     " properties", node->fullpath);

	if (linux_phandle && !phandle)
		phandle = linux_phandle;

	other = get_node_by_phandle(root, phandle);
	if (other && (other != node)) {
		FAIL(c, dti, "%s has duplicated phandle 0x%x (seen before at %s)",
		     node->fullpath, phandle, other->fullpath);
		return;
	}

	node->phandle = phandle;
}
ERROR(explicit_phandles, check_explicit_phandles, NULL);

static void check_name_properties(struct check *c, struct dt_info *dti,
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
		FAIL(c, dti, "\"name\" property in %s is incorrect (\"%s\" instead"
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
ERROR(name_properties, check_name_properties, NULL, &name_is_string);

/*
 * Reference fixup functions
 */

static void fixup_phandle_references(struct check *c, struct dt_info *dti,
				     struct node *node)
{
	struct node *dt = dti->dt;
	struct property *prop;

	for_each_property(node, prop) {
		struct marker *m = prop->val.markers;
		struct node *refnode;
		cell_t phandle;

		for_each_marker_of_type(m, REF_PHANDLE) {
			assert(m->offset + sizeof(cell_t) <= prop->val.len);

			refnode = get_node_by_ref(dt, m->ref);
			if (! refnode) {
				if (!(dti->dtsflags & DTSF_PLUGIN))
					FAIL(c, dti, "Reference to non-existent node or "
							"label \"%s\"\n", m->ref);
				else /* mark the entry as unresolved */
					*((fdt32_t *)(prop->val.val + m->offset)) =
						cpu_to_fdt32(0xffffffff);
				continue;
			}

			phandle = get_node_phandle(dt, refnode);
			*((fdt32_t *)(prop->val.val + m->offset)) = cpu_to_fdt32(phandle);
		}
	}
}
ERROR(phandle_references, fixup_phandle_references, NULL,
      &duplicate_node_names, &explicit_phandles);

static void fixup_path_references(struct check *c, struct dt_info *dti,
				  struct node *node)
{
	struct node *dt = dti->dt;
	struct property *prop;

	for_each_property(node, prop) {
		struct marker *m = prop->val.markers;
		struct node *refnode;
		char *path;

		for_each_marker_of_type(m, REF_PATH) {
			assert(m->offset <= prop->val.len);

			refnode = get_node_by_ref(dt, m->ref);
			if (!refnode) {
				FAIL(c, dti, "Reference to non-existent node or label \"%s\"\n",
				     m->ref);
				continue;
			}

			path = refnode->fullpath;
			prop->val = data_insert_at_marker(prop->val, m, path,
							  strlen(path) + 1);
		}
	}
}
ERROR(path_references, fixup_path_references, NULL, &duplicate_node_names);

/*
 * Semantic checks
 */
WARNING_IF_NOT_CELL(address_cells_is_cell, "#address-cells");
WARNING_IF_NOT_CELL(size_cells_is_cell, "#size-cells");
WARNING_IF_NOT_CELL(interrupt_cells_is_cell, "#interrupt-cells");

WARNING_IF_NOT_STRING(device_type_is_string, "device_type");
WARNING_IF_NOT_STRING(model_is_string, "model");
WARNING_IF_NOT_STRING(status_is_string, "status");

static void fixup_addr_size_cells(struct check *c, struct dt_info *dti,
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
WARNING(addr_size_cells, fixup_addr_size_cells, NULL,
	&address_cells_is_cell, &size_cells_is_cell);

#define node_addr_cells(n) \
	(((n)->addr_cells == -1) ? 2 : (n)->addr_cells)
#define node_size_cells(n) \
	(((n)->size_cells == -1) ? 1 : (n)->size_cells)

static void check_reg_format(struct check *c, struct dt_info *dti,
			     struct node *node)
{
	struct property *prop;
	int addr_cells, size_cells, entrylen;

	prop = get_property(node, "reg");
	if (!prop)
		return; /* No "reg", that's fine */

	if (!node->parent) {
		FAIL(c, dti, "Root node has a \"reg\" property");
		return;
	}

	if (prop->val.len == 0)
		FAIL(c, dti, "\"reg\" property in %s is empty", node->fullpath);

	addr_cells = node_addr_cells(node->parent);
	size_cells = node_size_cells(node->parent);
	entrylen = (addr_cells + size_cells) * sizeof(cell_t);

	if (!entrylen || (prop->val.len % entrylen) != 0)
		FAIL(c, dti, "\"reg\" property in %s has invalid length (%d bytes) "
		     "(#address-cells == %d, #size-cells == %d)",
		     node->fullpath, prop->val.len, addr_cells, size_cells);
}
WARNING(reg_format, check_reg_format, NULL, &addr_size_cells);

static void check_ranges_format(struct check *c, struct dt_info *dti,
				struct node *node)
{
	struct property *prop;
	int c_addr_cells, p_addr_cells, c_size_cells, p_size_cells, entrylen;

	prop = get_property(node, "ranges");
	if (!prop)
		return;

	if (!node->parent) {
		FAIL(c, dti, "Root node has a \"ranges\" property");
		return;
	}

	p_addr_cells = node_addr_cells(node->parent);
	p_size_cells = node_size_cells(node->parent);
	c_addr_cells = node_addr_cells(node);
	c_size_cells = node_size_cells(node);
	entrylen = (p_addr_cells + c_addr_cells + c_size_cells) * sizeof(cell_t);

	if (prop->val.len == 0) {
		if (p_addr_cells != c_addr_cells)
			FAIL(c, dti, "%s has empty \"ranges\" property but its "
			     "#address-cells (%d) differs from %s (%d)",
			     node->fullpath, c_addr_cells, node->parent->fullpath,
			     p_addr_cells);
		if (p_size_cells != c_size_cells)
			FAIL(c, dti, "%s has empty \"ranges\" property but its "
			     "#size-cells (%d) differs from %s (%d)",
			     node->fullpath, c_size_cells, node->parent->fullpath,
			     p_size_cells);
	} else if ((prop->val.len % entrylen) != 0) {
		FAIL(c, dti, "\"ranges\" property in %s has invalid length (%d bytes) "
		     "(parent #address-cells == %d, child #address-cells == %d, "
		     "#size-cells == %d)", node->fullpath, prop->val.len,
		     p_addr_cells, c_addr_cells, c_size_cells);
	}
}
WARNING(ranges_format, check_ranges_format, NULL, &addr_size_cells);

static const struct bus_type pci_bus = {
	.name = "PCI",
};

static void check_pci_bridge(struct check *c, struct dt_info *dti, struct node *node)
{
	struct property *prop;
	cell_t *cells;

	prop = get_property(node, "device_type");
	if (!prop || !streq(prop->val.val, "pci"))
		return;

	node->bus = &pci_bus;

	if (!strneq(node->name, "pci", node->basenamelen) &&
	    !strneq(node->name, "pcie", node->basenamelen))
		FAIL(c, dti, "Node %s node name is not \"pci\" or \"pcie\"",
			     node->fullpath);

	prop = get_property(node, "ranges");
	if (!prop)
		FAIL(c, dti, "Node %s missing ranges for PCI bridge (or not a bridge)",
			     node->fullpath);

	if (node_addr_cells(node) != 3)
		FAIL(c, dti, "Node %s incorrect #address-cells for PCI bridge",
			     node->fullpath);
	if (node_size_cells(node) != 2)
		FAIL(c, dti, "Node %s incorrect #size-cells for PCI bridge",
			     node->fullpath);

	prop = get_property(node, "bus-range");
	if (!prop) {
		FAIL(c, dti, "Node %s missing bus-range for PCI bridge",
			     node->fullpath);
		return;
	}
	if (prop->val.len != (sizeof(cell_t) * 2)) {
		FAIL(c, dti, "Node %s bus-range must be 2 cells",
			     node->fullpath);
		return;
	}
	cells = (cell_t *)prop->val.val;
	if (fdt32_to_cpu(cells[0]) > fdt32_to_cpu(cells[1]))
		FAIL(c, dti, "Node %s bus-range 1st cell must be less than or equal to 2nd cell",
			     node->fullpath);
	if (fdt32_to_cpu(cells[1]) > 0xff)
		FAIL(c, dti, "Node %s bus-range maximum bus number must be less than 256",
			     node->fullpath);
}
WARNING(pci_bridge, check_pci_bridge, NULL,
	&device_type_is_string, &addr_size_cells);

static void check_pci_device_bus_num(struct check *c, struct dt_info *dti, struct node *node)
{
	struct property *prop;
	unsigned int bus_num, min_bus, max_bus;
	cell_t *cells;

	if (!node->parent || (node->parent->bus != &pci_bus))
		return;

	prop = get_property(node, "reg");
	if (!prop)
		return;

	cells = (cell_t *)prop->val.val;
	bus_num = (fdt32_to_cpu(cells[0]) & 0x00ff0000) >> 16;

	prop = get_property(node->parent, "bus-range");
	if (!prop) {
		min_bus = max_bus = 0;
	} else {
		cells = (cell_t *)prop->val.val;
		min_bus = fdt32_to_cpu(cells[0]);
		max_bus = fdt32_to_cpu(cells[0]);
	}
	if ((bus_num < min_bus) || (bus_num > max_bus))
		FAIL(c, dti, "Node %s PCI bus number %d out of range, expected (%d - %d)",
		     node->fullpath, bus_num, min_bus, max_bus);
}
WARNING(pci_device_bus_num, check_pci_device_bus_num, NULL, &reg_format, &pci_bridge);

static void check_pci_device_reg(struct check *c, struct dt_info *dti, struct node *node)
{
	struct property *prop;
	const char *unitname = get_unitname(node);
	char unit_addr[5];
	unsigned int dev, func, reg;
	cell_t *cells;

	if (!node->parent || (node->parent->bus != &pci_bus))
		return;

	prop = get_property(node, "reg");
	if (!prop) {
		FAIL(c, dti, "Node %s missing PCI reg property", node->fullpath);
		return;
	}

	cells = (cell_t *)prop->val.val;
	if (cells[1] || cells[2])
		FAIL(c, dti, "Node %s PCI reg config space address cells 2 and 3 must be 0",
			     node->fullpath);

	reg = fdt32_to_cpu(cells[0]);
	dev = (reg & 0xf800) >> 11;
	func = (reg & 0x700) >> 8;

	if (reg & 0xff000000)
		FAIL(c, dti, "Node %s PCI reg address is not configuration space",
			     node->fullpath);
	if (reg & 0x000000ff)
		FAIL(c, dti, "Node %s PCI reg config space address register number must be 0",
			     node->fullpath);

	if (func == 0) {
		snprintf(unit_addr, sizeof(unit_addr), "%x", dev);
		if (streq(unitname, unit_addr))
			return;
	}

	snprintf(unit_addr, sizeof(unit_addr), "%x,%x", dev, func);
	if (streq(unitname, unit_addr))
		return;

	FAIL(c, dti, "Node %s PCI unit address format error, expected \"%s\"",
	     node->fullpath, unit_addr);
}
WARNING(pci_device_reg, check_pci_device_reg, NULL, &reg_format, &pci_bridge);

static const struct bus_type simple_bus = {
	.name = "simple-bus",
};

static bool node_is_compatible(struct node *node, const char *compat)
{
	struct property *prop;
	const char *str, *end;

	prop = get_property(node, "compatible");
	if (!prop)
		return false;

	for (str = prop->val.val, end = str + prop->val.len; str < end;
	     str += strnlen(str, end - str) + 1) {
		if (strneq(str, compat, end - str))
			return true;
	}
	return false;
}

static void check_simple_bus_bridge(struct check *c, struct dt_info *dti, struct node *node)
{
	if (node_is_compatible(node, "simple-bus"))
		node->bus = &simple_bus;
}
WARNING(simple_bus_bridge, check_simple_bus_bridge, NULL, &addr_size_cells);

static void check_simple_bus_reg(struct check *c, struct dt_info *dti, struct node *node)
{
	struct property *prop;
	const char *unitname = get_unitname(node);
	char unit_addr[17];
	unsigned int size;
	uint64_t reg = 0;
	cell_t *cells = NULL;

	if (!node->parent || (node->parent->bus != &simple_bus))
		return;

	prop = get_property(node, "reg");
	if (prop)
		cells = (cell_t *)prop->val.val;
	else {
		prop = get_property(node, "ranges");
		if (prop && prop->val.len)
			/* skip of child address */
			cells = ((cell_t *)prop->val.val) + node_addr_cells(node);
	}

	if (!cells) {
		if (node->parent->parent && !(node->bus == &simple_bus))
			FAIL(c, dti, "Node %s missing or empty reg/ranges property", node->fullpath);
		return;
	}

	size = node_addr_cells(node->parent);
	while (size--)
		reg = (reg << 32) | fdt32_to_cpu(*(cells++));

	snprintf(unit_addr, sizeof(unit_addr), "%zx", reg);
	if (!streq(unitname, unit_addr))
		FAIL(c, dti, "Node %s simple-bus unit address format error, expected \"%s\"",
		     node->fullpath, unit_addr);
}
WARNING(simple_bus_reg, check_simple_bus_reg, NULL, &reg_format, &simple_bus_bridge);

static void check_unit_address_format(struct check *c, struct dt_info *dti,
				      struct node *node)
{
	const char *unitname = get_unitname(node);

	if (node->parent && node->parent->bus)
		return;

	if (!unitname[0])
		return;

	if (!strncmp(unitname, "0x", 2)) {
		FAIL(c, dti, "Node %s unit name should not have leading \"0x\"",
		    node->fullpath);
		/* skip over 0x for next test */
		unitname += 2;
	}
	if (unitname[0] == '0' && isxdigit(unitname[1]))
		FAIL(c, dti, "Node %s unit name should not have leading 0s",
		    node->fullpath);
}
WARNING(unit_address_format, check_unit_address_format, NULL,
	&node_name_format, &pci_bridge, &simple_bus_bridge);

/*
 * Style checks
 */
static void check_avoid_default_addr_size(struct check *c, struct dt_info *dti,
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
		FAIL(c, dti, "Relying on default #address-cells value for %s",
		     node->fullpath);

	if (node->parent->size_cells == -1)
		FAIL(c, dti, "Relying on default #size-cells value for %s",
		     node->fullpath);
}
WARNING(avoid_default_addr_size, check_avoid_default_addr_size, NULL,
	&addr_size_cells);

static void check_obsolete_chosen_interrupt_controller(struct check *c,
						       struct dt_info *dti,
						       struct node *node)
{
	struct node *dt = dti->dt;
	struct node *chosen;
	struct property *prop;

	if (node != dt)
		return;


	chosen = get_node_by_path(dt, "/chosen");
	if (!chosen)
		return;

	prop = get_property(chosen, "interrupt-controller");
	if (prop)
		FAIL(c, dti, "/chosen has obsolete \"interrupt-controller\" "
		     "property");
}
WARNING(obsolete_chosen_interrupt_controller,
	check_obsolete_chosen_interrupt_controller, NULL);

static struct check *check_table[] = {
	&duplicate_node_names, &duplicate_property_names,
	&node_name_chars, &node_name_format, &property_name_chars,
	&name_is_string, &name_properties,

	&duplicate_label,

	&explicit_phandles,
	&phandle_references, &path_references,

	&address_cells_is_cell, &size_cells_is_cell, &interrupt_cells_is_cell,
	&device_type_is_string, &model_is_string, &status_is_string,

	&property_name_chars_strict,
	&node_name_chars_strict,

	&addr_size_cells, &reg_format, &ranges_format,

	&unit_address_vs_reg,
	&unit_address_format,

	&pci_bridge,
	&pci_device_reg,
	&pci_device_bus_num,

	&simple_bus_bridge,
	&simple_bus_reg,

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

void process_checks(bool force, struct dt_info *dti)
{
	int i;
	int error = 0;

	for (i = 0; i < ARRAY_SIZE(check_table); i++) {
		struct check *c = check_table[i];

		if (c->warn || c->error)
			error = error || run_check(c, dti);
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
