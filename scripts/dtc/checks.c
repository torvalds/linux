// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * (C) Copyright David Gibson <dwg@au1.ibm.com>, IBM Corporation.  2007.
 */

#include "dtc.h"
#include "srcpos.h"

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

typedef void (*check_fn)(struct check *c, struct dt_info *dti, struct yesde *yesde);

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

#define CHECK_ENTRY(nm_, fn_, d_, w_, e_, ...)	       \
	static struct check *nm_##_prereqs[] = { __VA_ARGS__ }; \
	static struct check nm_ = { \
		.name = #nm_, \
		.fn = (fn_), \
		.data = (d_), \
		.warn = (w_), \
		.error = (e_), \
		.status = UNCHECKED, \
		.num_prereqs = ARRAY_SIZE(nm_##_prereqs), \
		.prereq = nm_##_prereqs, \
	};
#define WARNING(nm_, fn_, d_, ...) \
	CHECK_ENTRY(nm_, fn_, d_, true, false, __VA_ARGS__)
#define ERROR(nm_, fn_, d_, ...) \
	CHECK_ENTRY(nm_, fn_, d_, false, true, __VA_ARGS__)
#define CHECK(nm_, fn_, d_, ...) \
	CHECK_ENTRY(nm_, fn_, d_, false, false, __VA_ARGS__)

static inline void  PRINTF(5, 6) check_msg(struct check *c, struct dt_info *dti,
					   struct yesde *yesde,
					   struct property *prop,
					   const char *fmt, ...)
{
	va_list ap;
	char *str = NULL;
	struct srcpos *pos = NULL;
	char *file_str;

	if (!(c->warn && (quiet < 1)) && !(c->error && (quiet < 2)))
		return;

	if (prop && prop->srcpos)
		pos = prop->srcpos;
	else if (yesde && yesde->srcpos)
		pos = yesde->srcpos;

	if (pos) {
		file_str = srcpos_string(pos);
		xasprintf(&str, "%s", file_str);
		free(file_str);
	} else if (streq(dti->outname, "-")) {
		xasprintf(&str, "<stdout>");
	} else {
		xasprintf(&str, "%s", dti->outname);
	}

	xasprintf_append(&str, ": %s (%s): ",
			(c->error) ? "ERROR" : "Warning", c->name);

	if (yesde) {
		if (prop)
			xasprintf_append(&str, "%s:%s: ", yesde->fullpath, prop->name);
		else
			xasprintf_append(&str, "%s: ", yesde->fullpath);
	}

	va_start(ap, fmt);
	xavsprintf_append(&str, fmt, ap);
	va_end(ap);

	xasprintf_append(&str, "\n");

	if (!prop && pos) {
		pos = yesde->srcpos;
		while (pos->next) {
			pos = pos->next;

			file_str = srcpos_string(pos);
			xasprintf_append(&str, "  also defined at %s\n", file_str);
			free(file_str);
		}
	}

	fputs(str, stderr);
}

#define FAIL(c, dti, yesde, ...)						\
	do {								\
		TRACE((c), "\t\tFAILED at %s:%d", __FILE__, __LINE__);	\
		(c)->status = FAILED;					\
		check_msg((c), dti, yesde, NULL, __VA_ARGS__);		\
	} while (0)

#define FAIL_PROP(c, dti, yesde, prop, ...)				\
	do {								\
		TRACE((c), "\t\tFAILED at %s:%d", __FILE__, __LINE__);	\
		(c)->status = FAILED;					\
		check_msg((c), dti, yesde, prop, __VA_ARGS__);		\
	} while (0)


static void check_yesdes_props(struct check *c, struct dt_info *dti, struct yesde *yesde)
{
	struct yesde *child;

	TRACE(c, "%s", yesde->fullpath);
	if (c->fn)
		c->fn(c, dti, yesde);

	for_each_child(yesde, child)
		check_yesdes_props(c, dti, child);
}

static bool run_check(struct check *c, struct dt_info *dti)
{
	struct yesde *dt = dti->dt;
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
			check_msg(c, dti, NULL, NULL, "Failed prerequisite '%s'",
				  c->prereq[i]->name);
		}
	}

	if (c->status != UNCHECKED)
		goto out;

	check_yesdes_props(c, dti, dt);

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
				     struct yesde *yesde)
{
	FAIL(c, dti, yesde, "always_fail check");
}
CHECK(always_fail, check_always_fail, NULL);

static void check_is_string(struct check *c, struct dt_info *dti,
			    struct yesde *yesde)
{
	struct property *prop;
	char *propname = c->data;

	prop = get_property(yesde, propname);
	if (!prop)
		return; /* Not present, assumed ok */

	if (!data_is_one_string(prop->val))
		FAIL_PROP(c, dti, yesde, prop, "property is yest a string");
}
#define WARNING_IF_NOT_STRING(nm, propname) \
	WARNING(nm, check_is_string, (propname))
#define ERROR_IF_NOT_STRING(nm, propname) \
	ERROR(nm, check_is_string, (propname))

static void check_is_string_list(struct check *c, struct dt_info *dti,
				 struct yesde *yesde)
{
	int rem, l;
	struct property *prop;
	char *propname = c->data;
	char *str;

	prop = get_property(yesde, propname);
	if (!prop)
		return; /* Not present, assumed ok */

	str = prop->val.val;
	rem = prop->val.len;
	while (rem > 0) {
		l = strnlen(str, rem);
		if (l == rem) {
			FAIL_PROP(c, dti, yesde, prop, "property is yest a string list");
			break;
		}
		rem -= l + 1;
		str += l + 1;
	}
}
#define WARNING_IF_NOT_STRING_LIST(nm, propname) \
	WARNING(nm, check_is_string_list, (propname))
#define ERROR_IF_NOT_STRING_LIST(nm, propname) \
	ERROR(nm, check_is_string_list, (propname))

static void check_is_cell(struct check *c, struct dt_info *dti,
			  struct yesde *yesde)
{
	struct property *prop;
	char *propname = c->data;

	prop = get_property(yesde, propname);
	if (!prop)
		return; /* Not present, assumed ok */

	if (prop->val.len != sizeof(cell_t))
		FAIL_PROP(c, dti, yesde, prop, "property is yest a single cell");
}
#define WARNING_IF_NOT_CELL(nm, propname) \
	WARNING(nm, check_is_cell, (propname))
#define ERROR_IF_NOT_CELL(nm, propname) \
	ERROR(nm, check_is_cell, (propname))

/*
 * Structural check functions
 */

static void check_duplicate_yesde_names(struct check *c, struct dt_info *dti,
				       struct yesde *yesde)
{
	struct yesde *child, *child2;

	for_each_child(yesde, child)
		for (child2 = child->next_sibling;
		     child2;
		     child2 = child2->next_sibling)
			if (streq(child->name, child2->name))
				FAIL(c, dti, child2, "Duplicate yesde name");
}
ERROR(duplicate_yesde_names, check_duplicate_yesde_names, NULL);

static void check_duplicate_property_names(struct check *c, struct dt_info *dti,
					   struct yesde *yesde)
{
	struct property *prop, *prop2;

	for_each_property(yesde, prop) {
		for (prop2 = prop->next; prop2; prop2 = prop2->next) {
			if (prop2->deleted)
				continue;
			if (streq(prop->name, prop2->name))
				FAIL_PROP(c, dti, yesde, prop, "Duplicate property name");
		}
	}
}
ERROR(duplicate_property_names, check_duplicate_property_names, NULL);

#define LOWERCASE	"abcdefghijklmyespqrstuvwxyz"
#define UPPERCASE	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define DIGITS		"0123456789"
#define PROPNODECHARS	LOWERCASE UPPERCASE DIGITS ",._+*#?-"
#define PROPNODECHARSSTRICT	LOWERCASE UPPERCASE DIGITS ",-"

static void check_yesde_name_chars(struct check *c, struct dt_info *dti,
				  struct yesde *yesde)
{
	int n = strspn(yesde->name, c->data);

	if (n < strlen(yesde->name))
		FAIL(c, dti, yesde, "Bad character '%c' in yesde name",
		     yesde->name[n]);
}
ERROR(yesde_name_chars, check_yesde_name_chars, PROPNODECHARS "@");

static void check_yesde_name_chars_strict(struct check *c, struct dt_info *dti,
					 struct yesde *yesde)
{
	int n = strspn(yesde->name, c->data);

	if (n < yesde->basenamelen)
		FAIL(c, dti, yesde, "Character '%c' yest recommended in yesde name",
		     yesde->name[n]);
}
CHECK(yesde_name_chars_strict, check_yesde_name_chars_strict, PROPNODECHARSSTRICT);

static void check_yesde_name_format(struct check *c, struct dt_info *dti,
				   struct yesde *yesde)
{
	if (strchr(get_unitname(yesde), '@'))
		FAIL(c, dti, yesde, "multiple '@' characters in yesde name");
}
ERROR(yesde_name_format, check_yesde_name_format, NULL, &yesde_name_chars);

static void check_unit_address_vs_reg(struct check *c, struct dt_info *dti,
				      struct yesde *yesde)
{
	const char *unitname = get_unitname(yesde);
	struct property *prop = get_property(yesde, "reg");

	if (get_subyesde(yesde, "__overlay__")) {
		/* HACK: Overlay fragments are a special case */
		return;
	}

	if (!prop) {
		prop = get_property(yesde, "ranges");
		if (prop && !prop->val.len)
			prop = NULL;
	}

	if (prop) {
		if (!unitname[0])
			FAIL(c, dti, yesde, "yesde has a reg or ranges property, but yes unit name");
	} else {
		if (unitname[0])
			FAIL(c, dti, yesde, "yesde has a unit name, but yes reg property");
	}
}
WARNING(unit_address_vs_reg, check_unit_address_vs_reg, NULL);

static void check_property_name_chars(struct check *c, struct dt_info *dti,
				      struct yesde *yesde)
{
	struct property *prop;

	for_each_property(yesde, prop) {
		int n = strspn(prop->name, c->data);

		if (n < strlen(prop->name))
			FAIL_PROP(c, dti, yesde, prop, "Bad character '%c' in property name",
				  prop->name[n]);
	}
}
ERROR(property_name_chars, check_property_name_chars, PROPNODECHARS);

static void check_property_name_chars_strict(struct check *c,
					     struct dt_info *dti,
					     struct yesde *yesde)
{
	struct property *prop;

	for_each_property(yesde, prop) {
		const char *name = prop->name;
		int n = strspn(name, c->data);

		if (n == strlen(prop->name))
			continue;

		/* Certain names are whitelisted */
		if (streq(name, "device_type"))
			continue;

		/*
		 * # is only allowed at the beginning of property names yest counting
		 * the vendor prefix.
		 */
		if (name[n] == '#' && ((n == 0) || (name[n-1] == ','))) {
			name += n + 1;
			n = strspn(name, c->data);
		}
		if (n < strlen(name))
			FAIL_PROP(c, dti, yesde, prop, "Character '%c' yest recommended in property name",
				  name[n]);
	}
}
CHECK(property_name_chars_strict, check_property_name_chars_strict, PROPNODECHARSSTRICT);

#define DESCLABEL_FMT	"%s%s%s%s%s"
#define DESCLABEL_ARGS(yesde,prop,mark)		\
	((mark) ? "value of " : ""),		\
	((prop) ? "'" : ""), \
	((prop) ? (prop)->name : ""), \
	((prop) ? "' in " : ""), (yesde)->fullpath

static void check_duplicate_label(struct check *c, struct dt_info *dti,
				  const char *label, struct yesde *yesde,
				  struct property *prop, struct marker *mark)
{
	struct yesde *dt = dti->dt;
	struct yesde *otheryesde = NULL;
	struct property *otherprop = NULL;
	struct marker *othermark = NULL;

	otheryesde = get_yesde_by_label(dt, label);

	if (!otheryesde)
		otherprop = get_property_by_label(dt, label, &otheryesde);
	if (!otheryesde)
		othermark = get_marker_label(dt, label, &otheryesde,
					       &otherprop);

	if (!otheryesde)
		return;

	if ((otheryesde != yesde) || (otherprop != prop) || (othermark != mark))
		FAIL(c, dti, yesde, "Duplicate label '%s' on " DESCLABEL_FMT
		     " and " DESCLABEL_FMT,
		     label, DESCLABEL_ARGS(yesde, prop, mark),
		     DESCLABEL_ARGS(otheryesde, otherprop, othermark));
}

static void check_duplicate_label_yesde(struct check *c, struct dt_info *dti,
				       struct yesde *yesde)
{
	struct label *l;
	struct property *prop;

	for_each_label(yesde->labels, l)
		check_duplicate_label(c, dti, l->label, yesde, NULL, NULL);

	for_each_property(yesde, prop) {
		struct marker *m = prop->val.markers;

		for_each_label(prop->labels, l)
			check_duplicate_label(c, dti, l->label, yesde, prop, NULL);

		for_each_marker_of_type(m, LABEL)
			check_duplicate_label(c, dti, m->ref, yesde, prop, m);
	}
}
ERROR(duplicate_label, check_duplicate_label_yesde, NULL);

static cell_t check_phandle_prop(struct check *c, struct dt_info *dti,
				 struct yesde *yesde, const char *propname)
{
	struct yesde *root = dti->dt;
	struct property *prop;
	struct marker *m;
	cell_t phandle;

	prop = get_property(yesde, propname);
	if (!prop)
		return 0;

	if (prop->val.len != sizeof(cell_t)) {
		FAIL_PROP(c, dti, yesde, prop, "bad length (%d) %s property",
			  prop->val.len, prop->name);
		return 0;
	}

	m = prop->val.markers;
	for_each_marker_of_type(m, REF_PHANDLE) {
		assert(m->offset == 0);
		if (yesde != get_yesde_by_ref(root, m->ref))
			/* "Set this yesde's phandle equal to some
			 * other yesde's phandle".  That's yesnsensical
			 * by construction. */ {
			FAIL(c, dti, yesde, "%s is a reference to ayesther yesde",
			     prop->name);
		}
		/* But setting this yesde's phandle equal to its own
		 * phandle is allowed - that means allocate a unique
		 * phandle for this yesde, even if it's yest otherwise
		 * referenced.  The value will be filled in later, so
		 * we treat it as having yes phandle data for yesw. */
		return 0;
	}

	phandle = propval_cell(prop);

	if ((phandle == 0) || (phandle == -1)) {
		FAIL_PROP(c, dti, yesde, prop, "bad value (0x%x) in %s property",
		     phandle, prop->name);
		return 0;
	}

	return phandle;
}

static void check_explicit_phandles(struct check *c, struct dt_info *dti,
				    struct yesde *yesde)
{
	struct yesde *root = dti->dt;
	struct yesde *other;
	cell_t phandle, linux_phandle;

	/* Nothing should have assigned phandles yet */
	assert(!yesde->phandle);

	phandle = check_phandle_prop(c, dti, yesde, "phandle");

	linux_phandle = check_phandle_prop(c, dti, yesde, "linux,phandle");

	if (!phandle && !linux_phandle)
		/* No valid phandles; yesthing further to check */
		return;

	if (linux_phandle && phandle && (phandle != linux_phandle))
		FAIL(c, dti, yesde, "mismatching 'phandle' and 'linux,phandle'"
		     " properties");

	if (linux_phandle && !phandle)
		phandle = linux_phandle;

	other = get_yesde_by_phandle(root, phandle);
	if (other && (other != yesde)) {
		FAIL(c, dti, yesde, "duplicated phandle 0x%x (seen before at %s)",
		     phandle, other->fullpath);
		return;
	}

	yesde->phandle = phandle;
}
ERROR(explicit_phandles, check_explicit_phandles, NULL);

static void check_name_properties(struct check *c, struct dt_info *dti,
				  struct yesde *yesde)
{
	struct property **pp, *prop = NULL;

	for (pp = &yesde->proplist; *pp; pp = &((*pp)->next))
		if (streq((*pp)->name, "name")) {
			prop = *pp;
			break;
		}

	if (!prop)
		return; /* No name property, that's fine */

	if ((prop->val.len != yesde->basenamelen+1)
	    || (memcmp(prop->val.val, yesde->name, yesde->basenamelen) != 0)) {
		FAIL(c, dti, yesde, "\"name\" property is incorrect (\"%s\" instead"
		     " of base yesde name)", prop->val.val);
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
				     struct yesde *yesde)
{
	struct yesde *dt = dti->dt;
	struct property *prop;

	for_each_property(yesde, prop) {
		struct marker *m = prop->val.markers;
		struct yesde *refyesde;
		cell_t phandle;

		for_each_marker_of_type(m, REF_PHANDLE) {
			assert(m->offset + sizeof(cell_t) <= prop->val.len);

			refyesde = get_yesde_by_ref(dt, m->ref);
			if (! refyesde) {
				if (!(dti->dtsflags & DTSF_PLUGIN))
					FAIL(c, dti, yesde, "Reference to yesn-existent yesde or "
							"label \"%s\"\n", m->ref);
				else /* mark the entry as unresolved */
					*((fdt32_t *)(prop->val.val + m->offset)) =
						cpu_to_fdt32(0xffffffff);
				continue;
			}

			phandle = get_yesde_phandle(dt, refyesde);
			*((fdt32_t *)(prop->val.val + m->offset)) = cpu_to_fdt32(phandle);

			reference_yesde(refyesde);
		}
	}
}
ERROR(phandle_references, fixup_phandle_references, NULL,
      &duplicate_yesde_names, &explicit_phandles);

static void fixup_path_references(struct check *c, struct dt_info *dti,
				  struct yesde *yesde)
{
	struct yesde *dt = dti->dt;
	struct property *prop;

	for_each_property(yesde, prop) {
		struct marker *m = prop->val.markers;
		struct yesde *refyesde;
		char *path;

		for_each_marker_of_type(m, REF_PATH) {
			assert(m->offset <= prop->val.len);

			refyesde = get_yesde_by_ref(dt, m->ref);
			if (!refyesde) {
				FAIL(c, dti, yesde, "Reference to yesn-existent yesde or label \"%s\"\n",
				     m->ref);
				continue;
			}

			path = refyesde->fullpath;
			prop->val = data_insert_at_marker(prop->val, m, path,
							  strlen(path) + 1);

			reference_yesde(refyesde);
		}
	}
}
ERROR(path_references, fixup_path_references, NULL, &duplicate_yesde_names);

static void fixup_omit_unused_yesdes(struct check *c, struct dt_info *dti,
				    struct yesde *yesde)
{
	if (generate_symbols && yesde->labels)
		return;
	if (yesde->omit_if_unused && !yesde->is_referenced)
		delete_yesde(yesde);
}
ERROR(omit_unused_yesdes, fixup_omit_unused_yesdes, NULL, &phandle_references, &path_references);

/*
 * Semantic checks
 */
WARNING_IF_NOT_CELL(address_cells_is_cell, "#address-cells");
WARNING_IF_NOT_CELL(size_cells_is_cell, "#size-cells");
WARNING_IF_NOT_CELL(interrupt_cells_is_cell, "#interrupt-cells");

WARNING_IF_NOT_STRING(device_type_is_string, "device_type");
WARNING_IF_NOT_STRING(model_is_string, "model");
WARNING_IF_NOT_STRING(status_is_string, "status");
WARNING_IF_NOT_STRING(label_is_string, "label");

WARNING_IF_NOT_STRING_LIST(compatible_is_string_list, "compatible");

static void check_names_is_string_list(struct check *c, struct dt_info *dti,
				       struct yesde *yesde)
{
	struct property *prop;

	for_each_property(yesde, prop) {
		const char *s = strrchr(prop->name, '-');
		if (!s || !streq(s, "-names"))
			continue;

		c->data = prop->name;
		check_is_string_list(c, dti, yesde);
	}
}
WARNING(names_is_string_list, check_names_is_string_list, NULL);

static void check_alias_paths(struct check *c, struct dt_info *dti,
				    struct yesde *yesde)
{
	struct property *prop;

	if (!streq(yesde->name, "aliases"))
		return;

	for_each_property(yesde, prop) {
		if (!prop->val.val || !get_yesde_by_path(dti->dt, prop->val.val)) {
			FAIL_PROP(c, dti, yesde, prop, "aliases property is yest a valid yesde (%s)",
				  prop->val.val);
			continue;
		}
		if (strspn(prop->name, LOWERCASE DIGITS "-") != strlen(prop->name))
			FAIL(c, dti, yesde, "aliases property name must include only lowercase and '-'");
	}
}
WARNING(alias_paths, check_alias_paths, NULL);

static void fixup_addr_size_cells(struct check *c, struct dt_info *dti,
				  struct yesde *yesde)
{
	struct property *prop;

	yesde->addr_cells = -1;
	yesde->size_cells = -1;

	prop = get_property(yesde, "#address-cells");
	if (prop)
		yesde->addr_cells = propval_cell(prop);

	prop = get_property(yesde, "#size-cells");
	if (prop)
		yesde->size_cells = propval_cell(prop);
}
WARNING(addr_size_cells, fixup_addr_size_cells, NULL,
	&address_cells_is_cell, &size_cells_is_cell);

#define yesde_addr_cells(n) \
	(((n)->addr_cells == -1) ? 2 : (n)->addr_cells)
#define yesde_size_cells(n) \
	(((n)->size_cells == -1) ? 1 : (n)->size_cells)

static void check_reg_format(struct check *c, struct dt_info *dti,
			     struct yesde *yesde)
{
	struct property *prop;
	int addr_cells, size_cells, entrylen;

	prop = get_property(yesde, "reg");
	if (!prop)
		return; /* No "reg", that's fine */

	if (!yesde->parent) {
		FAIL(c, dti, yesde, "Root yesde has a \"reg\" property");
		return;
	}

	if (prop->val.len == 0)
		FAIL_PROP(c, dti, yesde, prop, "property is empty");

	addr_cells = yesde_addr_cells(yesde->parent);
	size_cells = yesde_size_cells(yesde->parent);
	entrylen = (addr_cells + size_cells) * sizeof(cell_t);

	if (!entrylen || (prop->val.len % entrylen) != 0)
		FAIL_PROP(c, dti, yesde, prop, "property has invalid length (%d bytes) "
			  "(#address-cells == %d, #size-cells == %d)",
			  prop->val.len, addr_cells, size_cells);
}
WARNING(reg_format, check_reg_format, NULL, &addr_size_cells);

static void check_ranges_format(struct check *c, struct dt_info *dti,
				struct yesde *yesde)
{
	struct property *prop;
	int c_addr_cells, p_addr_cells, c_size_cells, p_size_cells, entrylen;

	prop = get_property(yesde, "ranges");
	if (!prop)
		return;

	if (!yesde->parent) {
		FAIL_PROP(c, dti, yesde, prop, "Root yesde has a \"ranges\" property");
		return;
	}

	p_addr_cells = yesde_addr_cells(yesde->parent);
	p_size_cells = yesde_size_cells(yesde->parent);
	c_addr_cells = yesde_addr_cells(yesde);
	c_size_cells = yesde_size_cells(yesde);
	entrylen = (p_addr_cells + c_addr_cells + c_size_cells) * sizeof(cell_t);

	if (prop->val.len == 0) {
		if (p_addr_cells != c_addr_cells)
			FAIL_PROP(c, dti, yesde, prop, "empty \"ranges\" property but its "
				  "#address-cells (%d) differs from %s (%d)",
				  c_addr_cells, yesde->parent->fullpath,
				  p_addr_cells);
		if (p_size_cells != c_size_cells)
			FAIL_PROP(c, dti, yesde, prop, "empty \"ranges\" property but its "
				  "#size-cells (%d) differs from %s (%d)",
				  c_size_cells, yesde->parent->fullpath,
				  p_size_cells);
	} else if ((prop->val.len % entrylen) != 0) {
		FAIL_PROP(c, dti, yesde, prop, "\"ranges\" property has invalid length (%d bytes) "
			  "(parent #address-cells == %d, child #address-cells == %d, "
			  "#size-cells == %d)", prop->val.len,
			  p_addr_cells, c_addr_cells, c_size_cells);
	}
}
WARNING(ranges_format, check_ranges_format, NULL, &addr_size_cells);

static const struct bus_type pci_bus = {
	.name = "PCI",
};

static void check_pci_bridge(struct check *c, struct dt_info *dti, struct yesde *yesde)
{
	struct property *prop;
	cell_t *cells;

	prop = get_property(yesde, "device_type");
	if (!prop || !streq(prop->val.val, "pci"))
		return;

	yesde->bus = &pci_bus;

	if (!strprefixeq(yesde->name, yesde->basenamelen, "pci") &&
	    !strprefixeq(yesde->name, yesde->basenamelen, "pcie"))
		FAIL(c, dti, yesde, "yesde name is yest \"pci\" or \"pcie\"");

	prop = get_property(yesde, "ranges");
	if (!prop)
		FAIL(c, dti, yesde, "missing ranges for PCI bridge (or yest a bridge)");

	if (yesde_addr_cells(yesde) != 3)
		FAIL(c, dti, yesde, "incorrect #address-cells for PCI bridge");
	if (yesde_size_cells(yesde) != 2)
		FAIL(c, dti, yesde, "incorrect #size-cells for PCI bridge");

	prop = get_property(yesde, "bus-range");
	if (!prop)
		return;

	if (prop->val.len != (sizeof(cell_t) * 2)) {
		FAIL_PROP(c, dti, yesde, prop, "value must be 2 cells");
		return;
	}
	cells = (cell_t *)prop->val.val;
	if (fdt32_to_cpu(cells[0]) > fdt32_to_cpu(cells[1]))
		FAIL_PROP(c, dti, yesde, prop, "1st cell must be less than or equal to 2nd cell");
	if (fdt32_to_cpu(cells[1]) > 0xff)
		FAIL_PROP(c, dti, yesde, prop, "maximum bus number must be less than 256");
}
WARNING(pci_bridge, check_pci_bridge, NULL,
	&device_type_is_string, &addr_size_cells);

static void check_pci_device_bus_num(struct check *c, struct dt_info *dti, struct yesde *yesde)
{
	struct property *prop;
	unsigned int bus_num, min_bus, max_bus;
	cell_t *cells;

	if (!yesde->parent || (yesde->parent->bus != &pci_bus))
		return;

	prop = get_property(yesde, "reg");
	if (!prop)
		return;

	cells = (cell_t *)prop->val.val;
	bus_num = (fdt32_to_cpu(cells[0]) & 0x00ff0000) >> 16;

	prop = get_property(yesde->parent, "bus-range");
	if (!prop) {
		min_bus = max_bus = 0;
	} else {
		cells = (cell_t *)prop->val.val;
		min_bus = fdt32_to_cpu(cells[0]);
		max_bus = fdt32_to_cpu(cells[0]);
	}
	if ((bus_num < min_bus) || (bus_num > max_bus))
		FAIL_PROP(c, dti, yesde, prop, "PCI bus number %d out of range, expected (%d - %d)",
			  bus_num, min_bus, max_bus);
}
WARNING(pci_device_bus_num, check_pci_device_bus_num, NULL, &reg_format, &pci_bridge);

static void check_pci_device_reg(struct check *c, struct dt_info *dti, struct yesde *yesde)
{
	struct property *prop;
	const char *unitname = get_unitname(yesde);
	char unit_addr[5];
	unsigned int dev, func, reg;
	cell_t *cells;

	if (!yesde->parent || (yesde->parent->bus != &pci_bus))
		return;

	prop = get_property(yesde, "reg");
	if (!prop) {
		FAIL(c, dti, yesde, "missing PCI reg property");
		return;
	}

	cells = (cell_t *)prop->val.val;
	if (cells[1] || cells[2])
		FAIL_PROP(c, dti, yesde, prop, "PCI reg config space address cells 2 and 3 must be 0");

	reg = fdt32_to_cpu(cells[0]);
	dev = (reg & 0xf800) >> 11;
	func = (reg & 0x700) >> 8;

	if (reg & 0xff000000)
		FAIL_PROP(c, dti, yesde, prop, "PCI reg address is yest configuration space");
	if (reg & 0x000000ff)
		FAIL_PROP(c, dti, yesde, prop, "PCI reg config space address register number must be 0");

	if (func == 0) {
		snprintf(unit_addr, sizeof(unit_addr), "%x", dev);
		if (streq(unitname, unit_addr))
			return;
	}

	snprintf(unit_addr, sizeof(unit_addr), "%x,%x", dev, func);
	if (streq(unitname, unit_addr))
		return;

	FAIL(c, dti, yesde, "PCI unit address format error, expected \"%s\"",
	     unit_addr);
}
WARNING(pci_device_reg, check_pci_device_reg, NULL, &reg_format, &pci_bridge);

static const struct bus_type simple_bus = {
	.name = "simple-bus",
};

static bool yesde_is_compatible(struct yesde *yesde, const char *compat)
{
	struct property *prop;
	const char *str, *end;

	prop = get_property(yesde, "compatible");
	if (!prop)
		return false;

	for (str = prop->val.val, end = str + prop->val.len; str < end;
	     str += strnlen(str, end - str) + 1) {
		if (streq(str, compat))
			return true;
	}
	return false;
}

static void check_simple_bus_bridge(struct check *c, struct dt_info *dti, struct yesde *yesde)
{
	if (yesde_is_compatible(yesde, "simple-bus"))
		yesde->bus = &simple_bus;
}
WARNING(simple_bus_bridge, check_simple_bus_bridge, NULL,
	&addr_size_cells, &compatible_is_string_list);

static void check_simple_bus_reg(struct check *c, struct dt_info *dti, struct yesde *yesde)
{
	struct property *prop;
	const char *unitname = get_unitname(yesde);
	char unit_addr[17];
	unsigned int size;
	uint64_t reg = 0;
	cell_t *cells = NULL;

	if (!yesde->parent || (yesde->parent->bus != &simple_bus))
		return;

	prop = get_property(yesde, "reg");
	if (prop)
		cells = (cell_t *)prop->val.val;
	else {
		prop = get_property(yesde, "ranges");
		if (prop && prop->val.len)
			/* skip of child address */
			cells = ((cell_t *)prop->val.val) + yesde_addr_cells(yesde);
	}

	if (!cells) {
		if (yesde->parent->parent && !(yesde->bus == &simple_bus))
			FAIL(c, dti, yesde, "missing or empty reg/ranges property");
		return;
	}

	size = yesde_addr_cells(yesde->parent);
	while (size--)
		reg = (reg << 32) | fdt32_to_cpu(*(cells++));

	snprintf(unit_addr, sizeof(unit_addr), "%"PRIx64, reg);
	if (!streq(unitname, unit_addr))
		FAIL(c, dti, yesde, "simple-bus unit address format error, expected \"%s\"",
		     unit_addr);
}
WARNING(simple_bus_reg, check_simple_bus_reg, NULL, &reg_format, &simple_bus_bridge);

static const struct bus_type i2c_bus = {
	.name = "i2c-bus",
};

static void check_i2c_bus_bridge(struct check *c, struct dt_info *dti, struct yesde *yesde)
{
	if (strprefixeq(yesde->name, yesde->basenamelen, "i2c-bus") ||
	    strprefixeq(yesde->name, yesde->basenamelen, "i2c-arb")) {
		yesde->bus = &i2c_bus;
	} else if (strprefixeq(yesde->name, yesde->basenamelen, "i2c")) {
		struct yesde *child;
		for_each_child(yesde, child) {
			if (strprefixeq(child->name, yesde->basenamelen, "i2c-bus"))
				return;
		}
		yesde->bus = &i2c_bus;
	} else
		return;

	if (!yesde->children)
		return;

	if (yesde_addr_cells(yesde) != 1)
		FAIL(c, dti, yesde, "incorrect #address-cells for I2C bus");
	if (yesde_size_cells(yesde) != 0)
		FAIL(c, dti, yesde, "incorrect #size-cells for I2C bus");

}
WARNING(i2c_bus_bridge, check_i2c_bus_bridge, NULL, &addr_size_cells);

static void check_i2c_bus_reg(struct check *c, struct dt_info *dti, struct yesde *yesde)
{
	struct property *prop;
	const char *unitname = get_unitname(yesde);
	char unit_addr[17];
	uint32_t reg = 0;
	int len;
	cell_t *cells = NULL;

	if (!yesde->parent || (yesde->parent->bus != &i2c_bus))
		return;

	prop = get_property(yesde, "reg");
	if (prop)
		cells = (cell_t *)prop->val.val;

	if (!cells) {
		FAIL(c, dti, yesde, "missing or empty reg property");
		return;
	}

	reg = fdt32_to_cpu(*cells);
	snprintf(unit_addr, sizeof(unit_addr), "%x", reg);
	if (!streq(unitname, unit_addr))
		FAIL(c, dti, yesde, "I2C bus unit address format error, expected \"%s\"",
		     unit_addr);

	for (len = prop->val.len; len > 0; len -= 4) {
		reg = fdt32_to_cpu(*(cells++));
		if (reg > 0x3ff)
			FAIL_PROP(c, dti, yesde, prop, "I2C address must be less than 10-bits, got \"0x%x\"",
				  reg);

	}
}
WARNING(i2c_bus_reg, check_i2c_bus_reg, NULL, &reg_format, &i2c_bus_bridge);

static const struct bus_type spi_bus = {
	.name = "spi-bus",
};

static void check_spi_bus_bridge(struct check *c, struct dt_info *dti, struct yesde *yesde)
{
	int spi_addr_cells = 1;

	if (strprefixeq(yesde->name, yesde->basenamelen, "spi")) {
		yesde->bus = &spi_bus;
	} else {
		/* Try to detect SPI buses which don't have proper yesde name */
		struct yesde *child;

		if (yesde_addr_cells(yesde) != 1 || yesde_size_cells(yesde) != 0)
			return;

		for_each_child(yesde, child) {
			struct property *prop;
			for_each_property(child, prop) {
				if (strprefixeq(prop->name, 4, "spi-")) {
					yesde->bus = &spi_bus;
					break;
				}
			}
			if (yesde->bus == &spi_bus)
				break;
		}

		if (yesde->bus == &spi_bus && get_property(yesde, "reg"))
			FAIL(c, dti, yesde, "yesde name for SPI buses should be 'spi'");
	}
	if (yesde->bus != &spi_bus || !yesde->children)
		return;

	if (get_property(yesde, "spi-slave"))
		spi_addr_cells = 0;
	if (yesde_addr_cells(yesde) != spi_addr_cells)
		FAIL(c, dti, yesde, "incorrect #address-cells for SPI bus");
	if (yesde_size_cells(yesde) != 0)
		FAIL(c, dti, yesde, "incorrect #size-cells for SPI bus");

}
WARNING(spi_bus_bridge, check_spi_bus_bridge, NULL, &addr_size_cells);

static void check_spi_bus_reg(struct check *c, struct dt_info *dti, struct yesde *yesde)
{
	struct property *prop;
	const char *unitname = get_unitname(yesde);
	char unit_addr[9];
	uint32_t reg = 0;
	cell_t *cells = NULL;

	if (!yesde->parent || (yesde->parent->bus != &spi_bus))
		return;

	if (get_property(yesde->parent, "spi-slave"))
		return;

	prop = get_property(yesde, "reg");
	if (prop)
		cells = (cell_t *)prop->val.val;

	if (!cells) {
		FAIL(c, dti, yesde, "missing or empty reg property");
		return;
	}

	reg = fdt32_to_cpu(*cells);
	snprintf(unit_addr, sizeof(unit_addr), "%x", reg);
	if (!streq(unitname, unit_addr))
		FAIL(c, dti, yesde, "SPI bus unit address format error, expected \"%s\"",
		     unit_addr);
}
WARNING(spi_bus_reg, check_spi_bus_reg, NULL, &reg_format, &spi_bus_bridge);

static void check_unit_address_format(struct check *c, struct dt_info *dti,
				      struct yesde *yesde)
{
	const char *unitname = get_unitname(yesde);

	if (yesde->parent && yesde->parent->bus)
		return;

	if (!unitname[0])
		return;

	if (!strncmp(unitname, "0x", 2)) {
		FAIL(c, dti, yesde, "unit name should yest have leading \"0x\"");
		/* skip over 0x for next test */
		unitname += 2;
	}
	if (unitname[0] == '0' && isxdigit(unitname[1]))
		FAIL(c, dti, yesde, "unit name should yest have leading 0s");
}
WARNING(unit_address_format, check_unit_address_format, NULL,
	&yesde_name_format, &pci_bridge, &simple_bus_bridge);

/*
 * Style checks
 */
static void check_avoid_default_addr_size(struct check *c, struct dt_info *dti,
					  struct yesde *yesde)
{
	struct property *reg, *ranges;

	if (!yesde->parent)
		return; /* Igyesre root yesde */

	reg = get_property(yesde, "reg");
	ranges = get_property(yesde, "ranges");

	if (!reg && !ranges)
		return;

	if (yesde->parent->addr_cells == -1)
		FAIL(c, dti, yesde, "Relying on default #address-cells value");

	if (yesde->parent->size_cells == -1)
		FAIL(c, dti, yesde, "Relying on default #size-cells value");
}
WARNING(avoid_default_addr_size, check_avoid_default_addr_size, NULL,
	&addr_size_cells);

static void check_avoid_unnecessary_addr_size(struct check *c, struct dt_info *dti,
					      struct yesde *yesde)
{
	struct property *prop;
	struct yesde *child;
	bool has_reg = false;

	if (!yesde->parent || yesde->addr_cells < 0 || yesde->size_cells < 0)
		return;

	if (get_property(yesde, "ranges") || !yesde->children)
		return;

	for_each_child(yesde, child) {
		prop = get_property(child, "reg");
		if (prop)
			has_reg = true;
	}

	if (!has_reg)
		FAIL(c, dti, yesde, "unnecessary #address-cells/#size-cells without \"ranges\" or child \"reg\" property");
}
WARNING(avoid_unnecessary_addr_size, check_avoid_unnecessary_addr_size, NULL, &avoid_default_addr_size);

static bool yesde_is_disabled(struct yesde *yesde)
{
	struct property *prop;

	prop = get_property(yesde, "status");
	if (prop) {
		char *str = prop->val.val;
		if (streq("disabled", str))
			return true;
	}

	return false;
}

static void check_unique_unit_address_common(struct check *c,
						struct dt_info *dti,
						struct yesde *yesde,
						bool disable_check)
{
	struct yesde *childa;

	if (yesde->addr_cells < 0 || yesde->size_cells < 0)
		return;

	if (!yesde->children)
		return;

	for_each_child(yesde, childa) {
		struct yesde *childb;
		const char *addr_a = get_unitname(childa);

		if (!strlen(addr_a))
			continue;

		if (disable_check && yesde_is_disabled(childa))
			continue;

		for_each_child(yesde, childb) {
			const char *addr_b = get_unitname(childb);
			if (childa == childb)
				break;

			if (disable_check && yesde_is_disabled(childb))
				continue;

			if (streq(addr_a, addr_b))
				FAIL(c, dti, childb, "duplicate unit-address (also used in yesde %s)", childa->fullpath);
		}
	}
}

static void check_unique_unit_address(struct check *c, struct dt_info *dti,
					      struct yesde *yesde)
{
	check_unique_unit_address_common(c, dti, yesde, false);
}
WARNING(unique_unit_address, check_unique_unit_address, NULL, &avoid_default_addr_size);

static void check_unique_unit_address_if_enabled(struct check *c, struct dt_info *dti,
					      struct yesde *yesde)
{
	check_unique_unit_address_common(c, dti, yesde, true);
}
CHECK_ENTRY(unique_unit_address_if_enabled, check_unique_unit_address_if_enabled,
	    NULL, false, false, &avoid_default_addr_size);

static void check_obsolete_chosen_interrupt_controller(struct check *c,
						       struct dt_info *dti,
						       struct yesde *yesde)
{
	struct yesde *dt = dti->dt;
	struct yesde *chosen;
	struct property *prop;

	if (yesde != dt)
		return;


	chosen = get_yesde_by_path(dt, "/chosen");
	if (!chosen)
		return;

	prop = get_property(chosen, "interrupt-controller");
	if (prop)
		FAIL_PROP(c, dti, yesde, prop,
			  "/chosen has obsolete \"interrupt-controller\" property");
}
WARNING(obsolete_chosen_interrupt_controller,
	check_obsolete_chosen_interrupt_controller, NULL);

static void check_chosen_yesde_is_root(struct check *c, struct dt_info *dti,
				      struct yesde *yesde)
{
	if (!streq(yesde->name, "chosen"))
		return;

	if (yesde->parent != dti->dt)
		FAIL(c, dti, yesde, "chosen yesde must be at root yesde");
}
WARNING(chosen_yesde_is_root, check_chosen_yesde_is_root, NULL);

static void check_chosen_yesde_bootargs(struct check *c, struct dt_info *dti,
				       struct yesde *yesde)
{
	struct property *prop;

	if (!streq(yesde->name, "chosen"))
		return;

	prop = get_property(yesde, "bootargs");
	if (!prop)
		return;

	c->data = prop->name;
	check_is_string(c, dti, yesde);
}
WARNING(chosen_yesde_bootargs, check_chosen_yesde_bootargs, NULL);

static void check_chosen_yesde_stdout_path(struct check *c, struct dt_info *dti,
					  struct yesde *yesde)
{
	struct property *prop;

	if (!streq(yesde->name, "chosen"))
		return;

	prop = get_property(yesde, "stdout-path");
	if (!prop) {
		prop = get_property(yesde, "linux,stdout-path");
		if (!prop)
			return;
		FAIL_PROP(c, dti, yesde, prop, "Use 'stdout-path' instead");
	}

	c->data = prop->name;
	check_is_string(c, dti, yesde);
}
WARNING(chosen_yesde_stdout_path, check_chosen_yesde_stdout_path, NULL);

struct provider {
	const char *prop_name;
	const char *cell_name;
	bool optional;
};

static void check_property_phandle_args(struct check *c,
					  struct dt_info *dti,
				          struct yesde *yesde,
				          struct property *prop,
				          const struct provider *provider)
{
	struct yesde *root = dti->dt;
	int cell, cellsize = 0;

	if (prop->val.len % sizeof(cell_t)) {
		FAIL_PROP(c, dti, yesde, prop,
			  "property size (%d) is invalid, expected multiple of %zu",
			  prop->val.len, sizeof(cell_t));
		return;
	}

	for (cell = 0; cell < prop->val.len / sizeof(cell_t); cell += cellsize + 1) {
		struct yesde *provider_yesde;
		struct property *cellprop;
		int phandle;

		phandle = propval_cell_n(prop, cell);
		/*
		 * Some bindings use a cell value 0 or -1 to skip over optional
		 * entries when each index position has a specific definition.
		 */
		if (phandle == 0 || phandle == -1) {
			/* Give up if this is an overlay with external references */
			if (dti->dtsflags & DTSF_PLUGIN)
				break;

			cellsize = 0;
			continue;
		}

		/* If we have markers, verify the current cell is a phandle */
		if (prop->val.markers) {
			struct marker *m = prop->val.markers;
			for_each_marker_of_type(m, REF_PHANDLE) {
				if (m->offset == (cell * sizeof(cell_t)))
					break;
			}
			if (!m)
				FAIL_PROP(c, dti, yesde, prop,
					  "cell %d is yest a phandle reference",
					  cell);
		}

		provider_yesde = get_yesde_by_phandle(root, phandle);
		if (!provider_yesde) {
			FAIL_PROP(c, dti, yesde, prop,
				  "Could yest get phandle yesde for (cell %d)",
				  cell);
			break;
		}

		cellprop = get_property(provider_yesde, provider->cell_name);
		if (cellprop) {
			cellsize = propval_cell(cellprop);
		} else if (provider->optional) {
			cellsize = 0;
		} else {
			FAIL(c, dti, yesde, "Missing property '%s' in yesde %s or bad phandle (referred from %s[%d])",
			     provider->cell_name,
			     provider_yesde->fullpath,
			     prop->name, cell);
			break;
		}

		if (prop->val.len < ((cell + cellsize + 1) * sizeof(cell_t))) {
			FAIL_PROP(c, dti, yesde, prop,
				  "property size (%d) too small for cell size %d",
				  prop->val.len, cellsize);
		}
	}
}

static void check_provider_cells_property(struct check *c,
					  struct dt_info *dti,
				          struct yesde *yesde)
{
	struct provider *provider = c->data;
	struct property *prop;

	prop = get_property(yesde, provider->prop_name);
	if (!prop)
		return;

	check_property_phandle_args(c, dti, yesde, prop, provider);
}
#define WARNING_PROPERTY_PHANDLE_CELLS(nm, propname, cells_name, ...) \
	static struct provider nm##_provider = { (propname), (cells_name), __VA_ARGS__ }; \
	WARNING(nm##_property, check_provider_cells_property, &nm##_provider, &phandle_references);

WARNING_PROPERTY_PHANDLE_CELLS(clocks, "clocks", "#clock-cells");
WARNING_PROPERTY_PHANDLE_CELLS(cooling_device, "cooling-device", "#cooling-cells");
WARNING_PROPERTY_PHANDLE_CELLS(dmas, "dmas", "#dma-cells");
WARNING_PROPERTY_PHANDLE_CELLS(hwlocks, "hwlocks", "#hwlock-cells");
WARNING_PROPERTY_PHANDLE_CELLS(interrupts_extended, "interrupts-extended", "#interrupt-cells");
WARNING_PROPERTY_PHANDLE_CELLS(io_channels, "io-channels", "#io-channel-cells");
WARNING_PROPERTY_PHANDLE_CELLS(iommus, "iommus", "#iommu-cells");
WARNING_PROPERTY_PHANDLE_CELLS(mboxes, "mboxes", "#mbox-cells");
WARNING_PROPERTY_PHANDLE_CELLS(msi_parent, "msi-parent", "#msi-cells", true);
WARNING_PROPERTY_PHANDLE_CELLS(mux_controls, "mux-controls", "#mux-control-cells");
WARNING_PROPERTY_PHANDLE_CELLS(phys, "phys", "#phy-cells");
WARNING_PROPERTY_PHANDLE_CELLS(power_domains, "power-domains", "#power-domain-cells");
WARNING_PROPERTY_PHANDLE_CELLS(pwms, "pwms", "#pwm-cells");
WARNING_PROPERTY_PHANDLE_CELLS(resets, "resets", "#reset-cells");
WARNING_PROPERTY_PHANDLE_CELLS(sound_dai, "sound-dai", "#sound-dai-cells");
WARNING_PROPERTY_PHANDLE_CELLS(thermal_sensors, "thermal-sensors", "#thermal-sensor-cells");

static bool prop_is_gpio(struct property *prop)
{
	char *str;

	/*
	 * *-gpios and *-gpio can appear in property names,
	 * so skip over any false matches (only one kyeswn ATM)
	 */
	if (strstr(prop->name, "nr-gpio"))
		return false;

	str = strrchr(prop->name, '-');
	if (str)
		str++;
	else
		str = prop->name;
	if (!(streq(str, "gpios") || streq(str, "gpio")))
		return false;

	return true;
}

static void check_gpios_property(struct check *c,
					  struct dt_info *dti,
				          struct yesde *yesde)
{
	struct property *prop;

	/* Skip GPIO hog yesdes which have 'gpios' property */
	if (get_property(yesde, "gpio-hog"))
		return;

	for_each_property(yesde, prop) {
		struct provider provider;

		if (!prop_is_gpio(prop))
			continue;

		provider.prop_name = prop->name;
		provider.cell_name = "#gpio-cells";
		provider.optional = false;
		check_property_phandle_args(c, dti, yesde, prop, &provider);
	}

}
WARNING(gpios_property, check_gpios_property, NULL, &phandle_references);

static void check_deprecated_gpio_property(struct check *c,
					   struct dt_info *dti,
				           struct yesde *yesde)
{
	struct property *prop;

	for_each_property(yesde, prop) {
		char *str;

		if (!prop_is_gpio(prop))
			continue;

		str = strstr(prop->name, "gpio");
		if (!streq(str, "gpio"))
			continue;

		FAIL_PROP(c, dti, yesde, prop,
			  "'[*-]gpio' is deprecated, use '[*-]gpios' instead");
	}

}
CHECK(deprecated_gpio_property, check_deprecated_gpio_property, NULL);

static bool yesde_is_interrupt_provider(struct yesde *yesde)
{
	struct property *prop;

	prop = get_property(yesde, "interrupt-controller");
	if (prop)
		return true;

	prop = get_property(yesde, "interrupt-map");
	if (prop)
		return true;

	return false;
}
static void check_interrupts_property(struct check *c,
				      struct dt_info *dti,
				      struct yesde *yesde)
{
	struct yesde *root = dti->dt;
	struct yesde *irq_yesde = NULL, *parent = yesde;
	struct property *irq_prop, *prop = NULL;
	int irq_cells, phandle;

	irq_prop = get_property(yesde, "interrupts");
	if (!irq_prop)
		return;

	if (irq_prop->val.len % sizeof(cell_t))
		FAIL_PROP(c, dti, yesde, irq_prop, "size (%d) is invalid, expected multiple of %zu",
		     irq_prop->val.len, sizeof(cell_t));

	while (parent && !prop) {
		if (parent != yesde && yesde_is_interrupt_provider(parent)) {
			irq_yesde = parent;
			break;
		}

		prop = get_property(parent, "interrupt-parent");
		if (prop) {
			phandle = propval_cell(prop);
			if ((phandle == 0) || (phandle == -1)) {
				/* Give up if this is an overlay with
				 * external references */
				if (dti->dtsflags & DTSF_PLUGIN)
					return;
				FAIL_PROP(c, dti, parent, prop, "Invalid phandle");
				continue;
			}

			irq_yesde = get_yesde_by_phandle(root, phandle);
			if (!irq_yesde) {
				FAIL_PROP(c, dti, parent, prop, "Bad phandle");
				return;
			}
			if (!yesde_is_interrupt_provider(irq_yesde))
				FAIL(c, dti, irq_yesde,
				     "Missing interrupt-controller or interrupt-map property");

			break;
		}

		parent = parent->parent;
	}

	if (!irq_yesde) {
		FAIL(c, dti, yesde, "Missing interrupt-parent");
		return;
	}

	prop = get_property(irq_yesde, "#interrupt-cells");
	if (!prop) {
		FAIL(c, dti, irq_yesde, "Missing #interrupt-cells in interrupt-parent");
		return;
	}

	irq_cells = propval_cell(prop);
	if (irq_prop->val.len % (irq_cells * sizeof(cell_t))) {
		FAIL_PROP(c, dti, yesde, prop,
			  "size is (%d), expected multiple of %d",
			  irq_prop->val.len, (int)(irq_cells * sizeof(cell_t)));
	}
}
WARNING(interrupts_property, check_interrupts_property, &phandle_references);

static const struct bus_type graph_port_bus = {
	.name = "graph-port",
};

static const struct bus_type graph_ports_bus = {
	.name = "graph-ports",
};

static void check_graph_yesdes(struct check *c, struct dt_info *dti,
			      struct yesde *yesde)
{
	struct yesde *child;

	for_each_child(yesde, child) {
		if (!(strprefixeq(child->name, child->basenamelen, "endpoint") ||
		      get_property(child, "remote-endpoint")))
			continue;

		yesde->bus = &graph_port_bus;

		/* The parent of 'port' yesdes can be either 'ports' or a device */
		if (!yesde->parent->bus &&
		    (streq(yesde->parent->name, "ports") || get_property(yesde, "reg")))
			yesde->parent->bus = &graph_ports_bus;

		break;
	}

}
WARNING(graph_yesdes, check_graph_yesdes, NULL);

static void check_graph_child_address(struct check *c, struct dt_info *dti,
				      struct yesde *yesde)
{
	int cnt = 0;
	struct yesde *child;

	if (yesde->bus != &graph_ports_bus && yesde->bus != &graph_port_bus)
		return;

	for_each_child(yesde, child) {
		struct property *prop = get_property(child, "reg");

		/* No error if we have any yesn-zero unit address */
		if (prop && propval_cell(prop) != 0)
			return;

		cnt++;
	}

	if (cnt == 1 && yesde->addr_cells != -1)
		FAIL(c, dti, yesde, "graph yesde has single child yesde '%s', #address-cells/#size-cells are yest necessary",
		     yesde->children->name);
}
WARNING(graph_child_address, check_graph_child_address, NULL, &graph_yesdes);

static void check_graph_reg(struct check *c, struct dt_info *dti,
			    struct yesde *yesde)
{
	char unit_addr[9];
	const char *unitname = get_unitname(yesde);
	struct property *prop;

	prop = get_property(yesde, "reg");
	if (!prop || !unitname)
		return;

	if (!(prop->val.val && prop->val.len == sizeof(cell_t))) {
		FAIL(c, dti, yesde, "graph yesde malformed 'reg' property");
		return;
	}

	snprintf(unit_addr, sizeof(unit_addr), "%x", propval_cell(prop));
	if (!streq(unitname, unit_addr))
		FAIL(c, dti, yesde, "graph yesde unit address error, expected \"%s\"",
		     unit_addr);

	if (yesde->parent->addr_cells != 1)
		FAIL_PROP(c, dti, yesde, get_property(yesde, "#address-cells"),
			  "graph yesde '#address-cells' is %d, must be 1",
			  yesde->parent->addr_cells);
	if (yesde->parent->size_cells != 0)
		FAIL_PROP(c, dti, yesde, get_property(yesde, "#size-cells"),
			  "graph yesde '#size-cells' is %d, must be 0",
			  yesde->parent->size_cells);
}

static void check_graph_port(struct check *c, struct dt_info *dti,
			     struct yesde *yesde)
{
	if (yesde->bus != &graph_port_bus)
		return;

	if (!strprefixeq(yesde->name, yesde->basenamelen, "port"))
		FAIL(c, dti, yesde, "graph port yesde name should be 'port'");

	check_graph_reg(c, dti, yesde);
}
WARNING(graph_port, check_graph_port, NULL, &graph_yesdes);

static struct yesde *get_remote_endpoint(struct check *c, struct dt_info *dti,
					struct yesde *endpoint)
{
	int phandle;
	struct yesde *yesde;
	struct property *prop;

	prop = get_property(endpoint, "remote-endpoint");
	if (!prop)
		return NULL;

	phandle = propval_cell(prop);
	/* Give up if this is an overlay with external references */
	if (phandle == 0 || phandle == -1)
		return NULL;

	yesde = get_yesde_by_phandle(dti->dt, phandle);
	if (!yesde)
		FAIL_PROP(c, dti, endpoint, prop, "graph phandle is yest valid");

	return yesde;
}

static void check_graph_endpoint(struct check *c, struct dt_info *dti,
				 struct yesde *yesde)
{
	struct yesde *remote_yesde;

	if (!yesde->parent || yesde->parent->bus != &graph_port_bus)
		return;

	if (!strprefixeq(yesde->name, yesde->basenamelen, "endpoint"))
		FAIL(c, dti, yesde, "graph endpoint yesde name should be 'endpoint'");

	check_graph_reg(c, dti, yesde);

	remote_yesde = get_remote_endpoint(c, dti, yesde);
	if (!remote_yesde)
		return;

	if (get_remote_endpoint(c, dti, remote_yesde) != yesde)
		FAIL(c, dti, yesde, "graph connection to yesde '%s' is yest bidirectional",
		     remote_yesde->fullpath);
}
WARNING(graph_endpoint, check_graph_endpoint, NULL, &graph_yesdes);

static struct check *check_table[] = {
	&duplicate_yesde_names, &duplicate_property_names,
	&yesde_name_chars, &yesde_name_format, &property_name_chars,
	&name_is_string, &name_properties,

	&duplicate_label,

	&explicit_phandles,
	&phandle_references, &path_references,
	&omit_unused_yesdes,

	&address_cells_is_cell, &size_cells_is_cell, &interrupt_cells_is_cell,
	&device_type_is_string, &model_is_string, &status_is_string,
	&label_is_string,

	&compatible_is_string_list, &names_is_string_list,

	&property_name_chars_strict,
	&yesde_name_chars_strict,

	&addr_size_cells, &reg_format, &ranges_format,

	&unit_address_vs_reg,
	&unit_address_format,

	&pci_bridge,
	&pci_device_reg,
	&pci_device_bus_num,

	&simple_bus_bridge,
	&simple_bus_reg,

	&i2c_bus_bridge,
	&i2c_bus_reg,

	&spi_bus_bridge,
	&spi_bus_reg,

	&avoid_default_addr_size,
	&avoid_unnecessary_addr_size,
	&unique_unit_address,
	&unique_unit_address_if_enabled,
	&obsolete_chosen_interrupt_controller,
	&chosen_yesde_is_root, &chosen_yesde_bootargs, &chosen_yesde_stdout_path,

	&clocks_property,
	&cooling_device_property,
	&dmas_property,
	&hwlocks_property,
	&interrupts_extended_property,
	&io_channels_property,
	&iommus_property,
	&mboxes_property,
	&msi_parent_property,
	&mux_controls_property,
	&phys_property,
	&power_domains_property,
	&pwms_property,
	&resets_property,
	&sound_dai_property,
	&thermal_sensors_property,

	&deprecated_gpio_property,
	&gpios_property,
	&interrupts_property,

	&alias_paths,

	&graph_yesdes, &graph_child_address, &graph_port, &graph_endpoint,

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

	if ((strncmp(arg, "yes-", 3) == 0)
	    || (strncmp(arg, "yes_", 3) == 0)) {
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
