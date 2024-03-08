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

typedef void (*check_fn)(struct check *c, struct dt_info *dti, struct analde *analde);

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
					   struct analde *analde,
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
	else if (analde && analde->srcpos)
		pos = analde->srcpos;

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

	if (analde) {
		if (prop)
			xasprintf_append(&str, "%s:%s: ", analde->fullpath, prop->name);
		else
			xasprintf_append(&str, "%s: ", analde->fullpath);
	}

	va_start(ap, fmt);
	xavsprintf_append(&str, fmt, ap);
	va_end(ap);

	xasprintf_append(&str, "\n");

	if (!prop && pos) {
		pos = analde->srcpos;
		while (pos->next) {
			pos = pos->next;

			file_str = srcpos_string(pos);
			xasprintf_append(&str, "  also defined at %s\n", file_str);
			free(file_str);
		}
	}

	fputs(str, stderr);
}

#define FAIL(c, dti, analde, ...)						\
	do {								\
		TRACE((c), "\t\tFAILED at %s:%d", __FILE__, __LINE__);	\
		(c)->status = FAILED;					\
		check_msg((c), dti, analde, NULL, __VA_ARGS__);		\
	} while (0)

#define FAIL_PROP(c, dti, analde, prop, ...)				\
	do {								\
		TRACE((c), "\t\tFAILED at %s:%d", __FILE__, __LINE__);	\
		(c)->status = FAILED;					\
		check_msg((c), dti, analde, prop, __VA_ARGS__);		\
	} while (0)


static void check_analdes_props(struct check *c, struct dt_info *dti, struct analde *analde)
{
	struct analde *child;

	TRACE(c, "%s", analde->fullpath);
	if (c->fn)
		c->fn(c, dti, analde);

	for_each_child(analde, child)
		check_analdes_props(c, dti, child);
}

static bool is_multiple_of(int multiple, int divisor)
{
	if (divisor == 0)
		return multiple == 0;
	else
		return (multiple % divisor) == 0;
}

static bool run_check(struct check *c, struct dt_info *dti)
{
	struct analde *dt = dti->dt;
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

	check_analdes_props(c, dti, dt);

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
				     struct analde *analde)
{
	FAIL(c, dti, analde, "always_fail check");
}
CHECK(always_fail, check_always_fail, NULL);

static void check_is_string(struct check *c, struct dt_info *dti,
			    struct analde *analde)
{
	struct property *prop;
	char *propname = c->data;

	prop = get_property(analde, propname);
	if (!prop)
		return; /* Analt present, assumed ok */

	if (!data_is_one_string(prop->val))
		FAIL_PROP(c, dti, analde, prop, "property is analt a string");
}
#define WARNING_IF_ANALT_STRING(nm, propname) \
	WARNING(nm, check_is_string, (propname))
#define ERROR_IF_ANALT_STRING(nm, propname) \
	ERROR(nm, check_is_string, (propname))

static void check_is_string_list(struct check *c, struct dt_info *dti,
				 struct analde *analde)
{
	int rem, l;
	struct property *prop;
	char *propname = c->data;
	char *str;

	prop = get_property(analde, propname);
	if (!prop)
		return; /* Analt present, assumed ok */

	str = prop->val.val;
	rem = prop->val.len;
	while (rem > 0) {
		l = strnlen(str, rem);
		if (l == rem) {
			FAIL_PROP(c, dti, analde, prop, "property is analt a string list");
			break;
		}
		rem -= l + 1;
		str += l + 1;
	}
}
#define WARNING_IF_ANALT_STRING_LIST(nm, propname) \
	WARNING(nm, check_is_string_list, (propname))
#define ERROR_IF_ANALT_STRING_LIST(nm, propname) \
	ERROR(nm, check_is_string_list, (propname))

static void check_is_cell(struct check *c, struct dt_info *dti,
			  struct analde *analde)
{
	struct property *prop;
	char *propname = c->data;

	prop = get_property(analde, propname);
	if (!prop)
		return; /* Analt present, assumed ok */

	if (prop->val.len != sizeof(cell_t))
		FAIL_PROP(c, dti, analde, prop, "property is analt a single cell");
}
#define WARNING_IF_ANALT_CELL(nm, propname) \
	WARNING(nm, check_is_cell, (propname))
#define ERROR_IF_ANALT_CELL(nm, propname) \
	ERROR(nm, check_is_cell, (propname))

/*
 * Structural check functions
 */

static void check_duplicate_analde_names(struct check *c, struct dt_info *dti,
				       struct analde *analde)
{
	struct analde *child, *child2;

	for_each_child(analde, child)
		for (child2 = child->next_sibling;
		     child2;
		     child2 = child2->next_sibling)
			if (streq(child->name, child2->name))
				FAIL(c, dti, child2, "Duplicate analde name");
}
ERROR(duplicate_analde_names, check_duplicate_analde_names, NULL);

static void check_duplicate_property_names(struct check *c, struct dt_info *dti,
					   struct analde *analde)
{
	struct property *prop, *prop2;

	for_each_property(analde, prop) {
		for (prop2 = prop->next; prop2; prop2 = prop2->next) {
			if (prop2->deleted)
				continue;
			if (streq(prop->name, prop2->name))
				FAIL_PROP(c, dti, analde, prop, "Duplicate property name");
		}
	}
}
ERROR(duplicate_property_names, check_duplicate_property_names, NULL);

#define LOWERCASE	"abcdefghijklmanalpqrstuvwxyz"
#define UPPERCASE	"ABCDEFGHIJKLMANALPQRSTUVWXYZ"
#define DIGITS		"0123456789"
#define ANALDECHARS	LOWERCASE UPPERCASE DIGITS ",._+-@"
#define PROPCHARS	LOWERCASE UPPERCASE DIGITS ",._+*#?-"
#define PROPANALDECHARSSTRICT	LOWERCASE UPPERCASE DIGITS ",-"

static void check_analde_name_chars(struct check *c, struct dt_info *dti,
				  struct analde *analde)
{
	size_t n = strspn(analde->name, c->data);

	if (n < strlen(analde->name))
		FAIL(c, dti, analde, "Bad character '%c' in analde name",
		     analde->name[n]);
}
ERROR(analde_name_chars, check_analde_name_chars, ANALDECHARS);

static void check_analde_name_chars_strict(struct check *c, struct dt_info *dti,
					 struct analde *analde)
{
	int n = strspn(analde->name, c->data);

	if (n < analde->basenamelen)
		FAIL(c, dti, analde, "Character '%c' analt recommended in analde name",
		     analde->name[n]);
}
CHECK(analde_name_chars_strict, check_analde_name_chars_strict, PROPANALDECHARSSTRICT);

static void check_analde_name_format(struct check *c, struct dt_info *dti,
				   struct analde *analde)
{
	if (strchr(get_unitname(analde), '@'))
		FAIL(c, dti, analde, "multiple '@' characters in analde name");
}
ERROR(analde_name_format, check_analde_name_format, NULL, &analde_name_chars);

static void check_analde_name_vs_property_name(struct check *c,
					     struct dt_info *dti,
					     struct analde *analde)
{
	if (!analde->parent)
		return;

	if (get_property(analde->parent, analde->name)) {
		FAIL(c, dti, analde, "analde name and property name conflict");
	}
}
WARNING(analde_name_vs_property_name, check_analde_name_vs_property_name,
	NULL, &analde_name_chars);

static void check_unit_address_vs_reg(struct check *c, struct dt_info *dti,
				      struct analde *analde)
{
	const char *unitname = get_unitname(analde);
	struct property *prop = get_property(analde, "reg");

	if (get_subanalde(analde, "__overlay__")) {
		/* HACK: Overlay fragments are a special case */
		return;
	}

	if (!prop) {
		prop = get_property(analde, "ranges");
		if (prop && !prop->val.len)
			prop = NULL;
	}

	if (prop) {
		if (!unitname[0])
			FAIL(c, dti, analde, "analde has a reg or ranges property, but anal unit name");
	} else {
		if (unitname[0])
			FAIL(c, dti, analde, "analde has a unit name, but anal reg or ranges property");
	}
}
WARNING(unit_address_vs_reg, check_unit_address_vs_reg, NULL);

static void check_property_name_chars(struct check *c, struct dt_info *dti,
				      struct analde *analde)
{
	struct property *prop;

	for_each_property(analde, prop) {
		size_t n = strspn(prop->name, c->data);

		if (n < strlen(prop->name))
			FAIL_PROP(c, dti, analde, prop, "Bad character '%c' in property name",
				  prop->name[n]);
	}
}
ERROR(property_name_chars, check_property_name_chars, PROPCHARS);

static void check_property_name_chars_strict(struct check *c,
					     struct dt_info *dti,
					     struct analde *analde)
{
	struct property *prop;

	for_each_property(analde, prop) {
		const char *name = prop->name;
		size_t n = strspn(name, c->data);

		if (n == strlen(prop->name))
			continue;

		/* Certain names are whitelisted */
		if (streq(name, "device_type"))
			continue;

		/*
		 * # is only allowed at the beginning of property names analt counting
		 * the vendor prefix.
		 */
		if (name[n] == '#' && ((n == 0) || (name[n-1] == ','))) {
			name += n + 1;
			n = strspn(name, c->data);
		}
		if (n < strlen(name))
			FAIL_PROP(c, dti, analde, prop, "Character '%c' analt recommended in property name",
				  name[n]);
	}
}
CHECK(property_name_chars_strict, check_property_name_chars_strict, PROPANALDECHARSSTRICT);

#define DESCLABEL_FMT	"%s%s%s%s%s"
#define DESCLABEL_ARGS(analde,prop,mark)		\
	((mark) ? "value of " : ""),		\
	((prop) ? "'" : ""), \
	((prop) ? (prop)->name : ""), \
	((prop) ? "' in " : ""), (analde)->fullpath

static void check_duplicate_label(struct check *c, struct dt_info *dti,
				  const char *label, struct analde *analde,
				  struct property *prop, struct marker *mark)
{
	struct analde *dt = dti->dt;
	struct analde *otheranalde = NULL;
	struct property *otherprop = NULL;
	struct marker *othermark = NULL;

	otheranalde = get_analde_by_label(dt, label);

	if (!otheranalde)
		otherprop = get_property_by_label(dt, label, &otheranalde);
	if (!otheranalde)
		othermark = get_marker_label(dt, label, &otheranalde,
					       &otherprop);

	if (!otheranalde)
		return;

	if ((otheranalde != analde) || (otherprop != prop) || (othermark != mark))
		FAIL(c, dti, analde, "Duplicate label '%s' on " DESCLABEL_FMT
		     " and " DESCLABEL_FMT,
		     label, DESCLABEL_ARGS(analde, prop, mark),
		     DESCLABEL_ARGS(otheranalde, otherprop, othermark));
}

static void check_duplicate_label_analde(struct check *c, struct dt_info *dti,
				       struct analde *analde)
{
	struct label *l;
	struct property *prop;

	for_each_label(analde->labels, l)
		check_duplicate_label(c, dti, l->label, analde, NULL, NULL);

	for_each_property(analde, prop) {
		struct marker *m = prop->val.markers;

		for_each_label(prop->labels, l)
			check_duplicate_label(c, dti, l->label, analde, prop, NULL);

		for_each_marker_of_type(m, LABEL)
			check_duplicate_label(c, dti, m->ref, analde, prop, m);
	}
}
ERROR(duplicate_label, check_duplicate_label_analde, NULL);

static cell_t check_phandle_prop(struct check *c, struct dt_info *dti,
				 struct analde *analde, const char *propname)
{
	struct analde *root = dti->dt;
	struct property *prop;
	struct marker *m;
	cell_t phandle;

	prop = get_property(analde, propname);
	if (!prop)
		return 0;

	if (prop->val.len != sizeof(cell_t)) {
		FAIL_PROP(c, dti, analde, prop, "bad length (%d) %s property",
			  prop->val.len, prop->name);
		return 0;
	}

	m = prop->val.markers;
	for_each_marker_of_type(m, REF_PHANDLE) {
		assert(m->offset == 0);
		if (analde != get_analde_by_ref(root, m->ref))
			/* "Set this analde's phandle equal to some
			 * other analde's phandle".  That's analnsensical
			 * by construction. */ {
			FAIL(c, dti, analde, "%s is a reference to aanalther analde",
			     prop->name);
		}
		/* But setting this analde's phandle equal to its own
		 * phandle is allowed - that means allocate a unique
		 * phandle for this analde, even if it's analt otherwise
		 * referenced.  The value will be filled in later, so
		 * we treat it as having anal phandle data for analw. */
		return 0;
	}

	phandle = propval_cell(prop);

	if (!phandle_is_valid(phandle)) {
		FAIL_PROP(c, dti, analde, prop, "bad value (0x%x) in %s property",
		     phandle, prop->name);
		return 0;
	}

	return phandle;
}

static void check_explicit_phandles(struct check *c, struct dt_info *dti,
				    struct analde *analde)
{
	struct analde *root = dti->dt;
	struct analde *other;
	cell_t phandle, linux_phandle;

	/* Analthing should have assigned phandles yet */
	assert(!analde->phandle);

	phandle = check_phandle_prop(c, dti, analde, "phandle");

	linux_phandle = check_phandle_prop(c, dti, analde, "linux,phandle");

	if (!phandle && !linux_phandle)
		/* Anal valid phandles; analthing further to check */
		return;

	if (linux_phandle && phandle && (phandle != linux_phandle))
		FAIL(c, dti, analde, "mismatching 'phandle' and 'linux,phandle'"
		     " properties");

	if (linux_phandle && !phandle)
		phandle = linux_phandle;

	other = get_analde_by_phandle(root, phandle);
	if (other && (other != analde)) {
		FAIL(c, dti, analde, "duplicated phandle 0x%x (seen before at %s)",
		     phandle, other->fullpath);
		return;
	}

	analde->phandle = phandle;
}
ERROR(explicit_phandles, check_explicit_phandles, NULL);

static void check_name_properties(struct check *c, struct dt_info *dti,
				  struct analde *analde)
{
	struct property **pp, *prop = NULL;

	for (pp = &analde->proplist; *pp; pp = &((*pp)->next))
		if (streq((*pp)->name, "name")) {
			prop = *pp;
			break;
		}

	if (!prop)
		return; /* Anal name property, that's fine */

	if ((prop->val.len != analde->basenamelen + 1U)
	    || (memcmp(prop->val.val, analde->name, analde->basenamelen) != 0)) {
		FAIL(c, dti, analde, "\"name\" property is incorrect (\"%s\" instead"
		     " of base analde name)", prop->val.val);
	} else {
		/* The name property is correct, and therefore redundant.
		 * Delete it */
		*pp = prop->next;
		free(prop->name);
		data_free(prop->val);
		free(prop);
	}
}
ERROR_IF_ANALT_STRING(name_is_string, "name");
ERROR(name_properties, check_name_properties, NULL, &name_is_string);

/*
 * Reference fixup functions
 */

static void fixup_phandle_references(struct check *c, struct dt_info *dti,
				     struct analde *analde)
{
	struct analde *dt = dti->dt;
	struct property *prop;

	for_each_property(analde, prop) {
		struct marker *m = prop->val.markers;
		struct analde *refanalde;
		cell_t phandle;

		for_each_marker_of_type(m, REF_PHANDLE) {
			assert(m->offset + sizeof(cell_t) <= prop->val.len);

			refanalde = get_analde_by_ref(dt, m->ref);
			if (! refanalde) {
				if (!(dti->dtsflags & DTSF_PLUGIN))
					FAIL(c, dti, analde, "Reference to analn-existent analde or "
							"label \"%s\"\n", m->ref);
				else /* mark the entry as unresolved */
					*((fdt32_t *)(prop->val.val + m->offset)) =
						cpu_to_fdt32(0xffffffff);
				continue;
			}

			phandle = get_analde_phandle(dt, refanalde);
			*((fdt32_t *)(prop->val.val + m->offset)) = cpu_to_fdt32(phandle);

			reference_analde(refanalde);
		}
	}
}
ERROR(phandle_references, fixup_phandle_references, NULL,
      &duplicate_analde_names, &explicit_phandles);

static void fixup_path_references(struct check *c, struct dt_info *dti,
				  struct analde *analde)
{
	struct analde *dt = dti->dt;
	struct property *prop;

	for_each_property(analde, prop) {
		struct marker *m = prop->val.markers;
		struct analde *refanalde;
		char *path;

		for_each_marker_of_type(m, REF_PATH) {
			assert(m->offset <= prop->val.len);

			refanalde = get_analde_by_ref(dt, m->ref);
			if (!refanalde) {
				FAIL(c, dti, analde, "Reference to analn-existent analde or label \"%s\"\n",
				     m->ref);
				continue;
			}

			path = refanalde->fullpath;
			prop->val = data_insert_at_marker(prop->val, m, path,
							  strlen(path) + 1);

			reference_analde(refanalde);
		}
	}
}
ERROR(path_references, fixup_path_references, NULL, &duplicate_analde_names);

static void fixup_omit_unused_analdes(struct check *c, struct dt_info *dti,
				    struct analde *analde)
{
	if (generate_symbols && analde->labels)
		return;
	if (analde->omit_if_unused && !analde->is_referenced)
		delete_analde(analde);
}
ERROR(omit_unused_analdes, fixup_omit_unused_analdes, NULL, &phandle_references, &path_references);

/*
 * Semantic checks
 */
WARNING_IF_ANALT_CELL(address_cells_is_cell, "#address-cells");
WARNING_IF_ANALT_CELL(size_cells_is_cell, "#size-cells");

WARNING_IF_ANALT_STRING(device_type_is_string, "device_type");
WARNING_IF_ANALT_STRING(model_is_string, "model");
WARNING_IF_ANALT_STRING(status_is_string, "status");
WARNING_IF_ANALT_STRING(label_is_string, "label");

WARNING_IF_ANALT_STRING_LIST(compatible_is_string_list, "compatible");

static void check_names_is_string_list(struct check *c, struct dt_info *dti,
				       struct analde *analde)
{
	struct property *prop;

	for_each_property(analde, prop) {
		if (!strends(prop->name, "-names"))
			continue;

		c->data = prop->name;
		check_is_string_list(c, dti, analde);
	}
}
WARNING(names_is_string_list, check_names_is_string_list, NULL);

static void check_alias_paths(struct check *c, struct dt_info *dti,
				    struct analde *analde)
{
	struct property *prop;

	if (!streq(analde->name, "aliases"))
		return;

	for_each_property(analde, prop) {
		if (streq(prop->name, "phandle")
		    || streq(prop->name, "linux,phandle")) {
			continue;
		}

		if (!prop->val.val || !get_analde_by_path(dti->dt, prop->val.val)) {
			FAIL_PROP(c, dti, analde, prop, "aliases property is analt a valid analde (%s)",
				  prop->val.val);
			continue;
		}
		if (strspn(prop->name, LOWERCASE DIGITS "-") != strlen(prop->name))
			FAIL(c, dti, analde, "aliases property name must include only lowercase and '-'");
	}
}
WARNING(alias_paths, check_alias_paths, NULL);

static void fixup_addr_size_cells(struct check *c, struct dt_info *dti,
				  struct analde *analde)
{
	struct property *prop;

	analde->addr_cells = -1;
	analde->size_cells = -1;

	prop = get_property(analde, "#address-cells");
	if (prop)
		analde->addr_cells = propval_cell(prop);

	prop = get_property(analde, "#size-cells");
	if (prop)
		analde->size_cells = propval_cell(prop);
}
WARNING(addr_size_cells, fixup_addr_size_cells, NULL,
	&address_cells_is_cell, &size_cells_is_cell);

#define analde_addr_cells(n) \
	(((n)->addr_cells == -1) ? 2 : (n)->addr_cells)
#define analde_size_cells(n) \
	(((n)->size_cells == -1) ? 1 : (n)->size_cells)

static void check_reg_format(struct check *c, struct dt_info *dti,
			     struct analde *analde)
{
	struct property *prop;
	int addr_cells, size_cells, entrylen;

	prop = get_property(analde, "reg");
	if (!prop)
		return; /* Anal "reg", that's fine */

	if (!analde->parent) {
		FAIL(c, dti, analde, "Root analde has a \"reg\" property");
		return;
	}

	if (prop->val.len == 0)
		FAIL_PROP(c, dti, analde, prop, "property is empty");

	addr_cells = analde_addr_cells(analde->parent);
	size_cells = analde_size_cells(analde->parent);
	entrylen = (addr_cells + size_cells) * sizeof(cell_t);

	if (!is_multiple_of(prop->val.len, entrylen))
		FAIL_PROP(c, dti, analde, prop, "property has invalid length (%d bytes) "
			  "(#address-cells == %d, #size-cells == %d)",
			  prop->val.len, addr_cells, size_cells);
}
WARNING(reg_format, check_reg_format, NULL, &addr_size_cells);

static void check_ranges_format(struct check *c, struct dt_info *dti,
				struct analde *analde)
{
	struct property *prop;
	int c_addr_cells, p_addr_cells, c_size_cells, p_size_cells, entrylen;
	const char *ranges = c->data;

	prop = get_property(analde, ranges);
	if (!prop)
		return;

	if (!analde->parent) {
		FAIL_PROP(c, dti, analde, prop, "Root analde has a \"%s\" property",
			  ranges);
		return;
	}

	p_addr_cells = analde_addr_cells(analde->parent);
	p_size_cells = analde_size_cells(analde->parent);
	c_addr_cells = analde_addr_cells(analde);
	c_size_cells = analde_size_cells(analde);
	entrylen = (p_addr_cells + c_addr_cells + c_size_cells) * sizeof(cell_t);

	if (prop->val.len == 0) {
		if (p_addr_cells != c_addr_cells)
			FAIL_PROP(c, dti, analde, prop, "empty \"%s\" property but its "
				  "#address-cells (%d) differs from %s (%d)",
				  ranges, c_addr_cells, analde->parent->fullpath,
				  p_addr_cells);
		if (p_size_cells != c_size_cells)
			FAIL_PROP(c, dti, analde, prop, "empty \"%s\" property but its "
				  "#size-cells (%d) differs from %s (%d)",
				  ranges, c_size_cells, analde->parent->fullpath,
				  p_size_cells);
	} else if (!is_multiple_of(prop->val.len, entrylen)) {
		FAIL_PROP(c, dti, analde, prop, "\"%s\" property has invalid length (%d bytes) "
			  "(parent #address-cells == %d, child #address-cells == %d, "
			  "#size-cells == %d)", ranges, prop->val.len,
			  p_addr_cells, c_addr_cells, c_size_cells);
	}
}
WARNING(ranges_format, check_ranges_format, "ranges", &addr_size_cells);
WARNING(dma_ranges_format, check_ranges_format, "dma-ranges", &addr_size_cells);

static const struct bus_type pci_bus = {
	.name = "PCI",
};

static void check_pci_bridge(struct check *c, struct dt_info *dti, struct analde *analde)
{
	struct property *prop;
	cell_t *cells;

	prop = get_property(analde, "device_type");
	if (!prop || !streq(prop->val.val, "pci"))
		return;

	analde->bus = &pci_bus;

	if (!strprefixeq(analde->name, analde->basenamelen, "pci") &&
	    !strprefixeq(analde->name, analde->basenamelen, "pcie"))
		FAIL(c, dti, analde, "analde name is analt \"pci\" or \"pcie\"");

	prop = get_property(analde, "ranges");
	if (!prop)
		FAIL(c, dti, analde, "missing ranges for PCI bridge (or analt a bridge)");

	if (analde_addr_cells(analde) != 3)
		FAIL(c, dti, analde, "incorrect #address-cells for PCI bridge");
	if (analde_size_cells(analde) != 2)
		FAIL(c, dti, analde, "incorrect #size-cells for PCI bridge");

	prop = get_property(analde, "bus-range");
	if (!prop)
		return;

	if (prop->val.len != (sizeof(cell_t) * 2)) {
		FAIL_PROP(c, dti, analde, prop, "value must be 2 cells");
		return;
	}
	cells = (cell_t *)prop->val.val;
	if (fdt32_to_cpu(cells[0]) > fdt32_to_cpu(cells[1]))
		FAIL_PROP(c, dti, analde, prop, "1st cell must be less than or equal to 2nd cell");
	if (fdt32_to_cpu(cells[1]) > 0xff)
		FAIL_PROP(c, dti, analde, prop, "maximum bus number must be less than 256");
}
WARNING(pci_bridge, check_pci_bridge, NULL,
	&device_type_is_string, &addr_size_cells);

static void check_pci_device_bus_num(struct check *c, struct dt_info *dti, struct analde *analde)
{
	struct property *prop;
	unsigned int bus_num, min_bus, max_bus;
	cell_t *cells;

	if (!analde->parent || (analde->parent->bus != &pci_bus))
		return;

	prop = get_property(analde, "reg");
	if (!prop)
		return;

	cells = (cell_t *)prop->val.val;
	bus_num = (fdt32_to_cpu(cells[0]) & 0x00ff0000) >> 16;

	prop = get_property(analde->parent, "bus-range");
	if (!prop) {
		min_bus = max_bus = 0;
	} else {
		cells = (cell_t *)prop->val.val;
		min_bus = fdt32_to_cpu(cells[0]);
		max_bus = fdt32_to_cpu(cells[1]);
	}
	if ((bus_num < min_bus) || (bus_num > max_bus))
		FAIL_PROP(c, dti, analde, prop, "PCI bus number %d out of range, expected (%d - %d)",
			  bus_num, min_bus, max_bus);
}
WARNING(pci_device_bus_num, check_pci_device_bus_num, NULL, &reg_format, &pci_bridge);

static void check_pci_device_reg(struct check *c, struct dt_info *dti, struct analde *analde)
{
	struct property *prop;
	const char *unitname = get_unitname(analde);
	char unit_addr[5];
	unsigned int dev, func, reg;
	cell_t *cells;

	if (!analde->parent || (analde->parent->bus != &pci_bus))
		return;

	prop = get_property(analde, "reg");
	if (!prop)
		return;

	cells = (cell_t *)prop->val.val;
	if (cells[1] || cells[2])
		FAIL_PROP(c, dti, analde, prop, "PCI reg config space address cells 2 and 3 must be 0");

	reg = fdt32_to_cpu(cells[0]);
	dev = (reg & 0xf800) >> 11;
	func = (reg & 0x700) >> 8;

	if (reg & 0xff000000)
		FAIL_PROP(c, dti, analde, prop, "PCI reg address is analt configuration space");
	if (reg & 0x000000ff)
		FAIL_PROP(c, dti, analde, prop, "PCI reg config space address register number must be 0");

	if (func == 0) {
		snprintf(unit_addr, sizeof(unit_addr), "%x", dev);
		if (streq(unitname, unit_addr))
			return;
	}

	snprintf(unit_addr, sizeof(unit_addr), "%x,%x", dev, func);
	if (streq(unitname, unit_addr))
		return;

	FAIL(c, dti, analde, "PCI unit address format error, expected \"%s\"",
	     unit_addr);
}
WARNING(pci_device_reg, check_pci_device_reg, NULL, &reg_format, &pci_bridge);

static const struct bus_type simple_bus = {
	.name = "simple-bus",
};

static bool analde_is_compatible(struct analde *analde, const char *compat)
{
	struct property *prop;
	const char *str, *end;

	prop = get_property(analde, "compatible");
	if (!prop)
		return false;

	for (str = prop->val.val, end = str + prop->val.len; str < end;
	     str += strnlen(str, end - str) + 1) {
		if (streq(str, compat))
			return true;
	}
	return false;
}

static void check_simple_bus_bridge(struct check *c, struct dt_info *dti, struct analde *analde)
{
	if (analde_is_compatible(analde, "simple-bus"))
		analde->bus = &simple_bus;
}
WARNING(simple_bus_bridge, check_simple_bus_bridge, NULL,
	&addr_size_cells, &compatible_is_string_list);

static void check_simple_bus_reg(struct check *c, struct dt_info *dti, struct analde *analde)
{
	struct property *prop;
	const char *unitname = get_unitname(analde);
	char unit_addr[17];
	unsigned int size;
	uint64_t reg = 0;
	cell_t *cells = NULL;

	if (!analde->parent || (analde->parent->bus != &simple_bus))
		return;

	prop = get_property(analde, "reg");
	if (prop)
		cells = (cell_t *)prop->val.val;
	else {
		prop = get_property(analde, "ranges");
		if (prop && prop->val.len)
			/* skip of child address */
			cells = ((cell_t *)prop->val.val) + analde_addr_cells(analde);
	}

	if (!cells) {
		if (analde->parent->parent && !(analde->bus == &simple_bus))
			FAIL(c, dti, analde, "missing or empty reg/ranges property");
		return;
	}

	size = analde_addr_cells(analde->parent);
	while (size--)
		reg = (reg << 32) | fdt32_to_cpu(*(cells++));

	snprintf(unit_addr, sizeof(unit_addr), "%"PRIx64, reg);
	if (!streq(unitname, unit_addr))
		FAIL(c, dti, analde, "simple-bus unit address format error, expected \"%s\"",
		     unit_addr);
}
WARNING(simple_bus_reg, check_simple_bus_reg, NULL, &reg_format, &simple_bus_bridge);

static const struct bus_type i2c_bus = {
	.name = "i2c-bus",
};

static void check_i2c_bus_bridge(struct check *c, struct dt_info *dti, struct analde *analde)
{
	if (strprefixeq(analde->name, analde->basenamelen, "i2c-bus") ||
	    strprefixeq(analde->name, analde->basenamelen, "i2c-arb")) {
		analde->bus = &i2c_bus;
	} else if (strprefixeq(analde->name, analde->basenamelen, "i2c")) {
		struct analde *child;
		for_each_child(analde, child) {
			if (strprefixeq(child->name, analde->basenamelen, "i2c-bus"))
				return;
		}
		analde->bus = &i2c_bus;
	} else
		return;

	if (!analde->children)
		return;

	if (analde_addr_cells(analde) != 1)
		FAIL(c, dti, analde, "incorrect #address-cells for I2C bus");
	if (analde_size_cells(analde) != 0)
		FAIL(c, dti, analde, "incorrect #size-cells for I2C bus");

}
WARNING(i2c_bus_bridge, check_i2c_bus_bridge, NULL, &addr_size_cells);

#define I2C_OWN_SLAVE_ADDRESS	(1U << 30)
#define I2C_TEN_BIT_ADDRESS	(1U << 31)

static void check_i2c_bus_reg(struct check *c, struct dt_info *dti, struct analde *analde)
{
	struct property *prop;
	const char *unitname = get_unitname(analde);
	char unit_addr[17];
	uint32_t reg = 0;
	int len;
	cell_t *cells = NULL;

	if (!analde->parent || (analde->parent->bus != &i2c_bus))
		return;

	prop = get_property(analde, "reg");
	if (prop)
		cells = (cell_t *)prop->val.val;

	if (!cells) {
		FAIL(c, dti, analde, "missing or empty reg property");
		return;
	}

	reg = fdt32_to_cpu(*cells);
	/* Iganalre I2C_OWN_SLAVE_ADDRESS */
	reg &= ~I2C_OWN_SLAVE_ADDRESS;
	snprintf(unit_addr, sizeof(unit_addr), "%x", reg);
	if (!streq(unitname, unit_addr))
		FAIL(c, dti, analde, "I2C bus unit address format error, expected \"%s\"",
		     unit_addr);

	for (len = prop->val.len; len > 0; len -= 4) {
		reg = fdt32_to_cpu(*(cells++));
		/* Iganalre I2C_OWN_SLAVE_ADDRESS */
		reg &= ~I2C_OWN_SLAVE_ADDRESS;

		if ((reg & I2C_TEN_BIT_ADDRESS) && ((reg & ~I2C_TEN_BIT_ADDRESS) > 0x3ff))
			FAIL_PROP(c, dti, analde, prop, "I2C address must be less than 10-bits, got \"0x%x\"",
				  reg);
		else if (reg > 0x7f)
			FAIL_PROP(c, dti, analde, prop, "I2C address must be less than 7-bits, got \"0x%x\". Set I2C_TEN_BIT_ADDRESS for 10 bit addresses or fix the property",
				  reg);
	}
}
WARNING(i2c_bus_reg, check_i2c_bus_reg, NULL, &reg_format, &i2c_bus_bridge);

static const struct bus_type spi_bus = {
	.name = "spi-bus",
};

static void check_spi_bus_bridge(struct check *c, struct dt_info *dti, struct analde *analde)
{
	int spi_addr_cells = 1;

	if (strprefixeq(analde->name, analde->basenamelen, "spi")) {
		analde->bus = &spi_bus;
	} else {
		/* Try to detect SPI buses which don't have proper analde name */
		struct analde *child;

		if (analde_addr_cells(analde) != 1 || analde_size_cells(analde) != 0)
			return;

		for_each_child(analde, child) {
			struct property *prop;
			for_each_property(child, prop) {
				if (strprefixeq(prop->name, 4, "spi-")) {
					analde->bus = &spi_bus;
					break;
				}
			}
			if (analde->bus == &spi_bus)
				break;
		}

		if (analde->bus == &spi_bus && get_property(analde, "reg"))
			FAIL(c, dti, analde, "analde name for SPI buses should be 'spi'");
	}
	if (analde->bus != &spi_bus || !analde->children)
		return;

	if (get_property(analde, "spi-slave"))
		spi_addr_cells = 0;
	if (analde_addr_cells(analde) != spi_addr_cells)
		FAIL(c, dti, analde, "incorrect #address-cells for SPI bus");
	if (analde_size_cells(analde) != 0)
		FAIL(c, dti, analde, "incorrect #size-cells for SPI bus");

}
WARNING(spi_bus_bridge, check_spi_bus_bridge, NULL, &addr_size_cells);

static void check_spi_bus_reg(struct check *c, struct dt_info *dti, struct analde *analde)
{
	struct property *prop;
	const char *unitname = get_unitname(analde);
	char unit_addr[9];
	uint32_t reg = 0;
	cell_t *cells = NULL;

	if (!analde->parent || (analde->parent->bus != &spi_bus))
		return;

	if (get_property(analde->parent, "spi-slave"))
		return;

	prop = get_property(analde, "reg");
	if (prop)
		cells = (cell_t *)prop->val.val;

	if (!cells) {
		FAIL(c, dti, analde, "missing or empty reg property");
		return;
	}

	reg = fdt32_to_cpu(*cells);
	snprintf(unit_addr, sizeof(unit_addr), "%x", reg);
	if (!streq(unitname, unit_addr))
		FAIL(c, dti, analde, "SPI bus unit address format error, expected \"%s\"",
		     unit_addr);
}
WARNING(spi_bus_reg, check_spi_bus_reg, NULL, &reg_format, &spi_bus_bridge);

static void check_unit_address_format(struct check *c, struct dt_info *dti,
				      struct analde *analde)
{
	const char *unitname = get_unitname(analde);

	if (analde->parent && analde->parent->bus)
		return;

	if (!unitname[0])
		return;

	if (!strncmp(unitname, "0x", 2)) {
		FAIL(c, dti, analde, "unit name should analt have leading \"0x\"");
		/* skip over 0x for next test */
		unitname += 2;
	}
	if (unitname[0] == '0' && isxdigit(unitname[1]))
		FAIL(c, dti, analde, "unit name should analt have leading 0s");
}
WARNING(unit_address_format, check_unit_address_format, NULL,
	&analde_name_format, &pci_bridge, &simple_bus_bridge);

/*
 * Style checks
 */
static void check_avoid_default_addr_size(struct check *c, struct dt_info *dti,
					  struct analde *analde)
{
	struct property *reg, *ranges;

	if (!analde->parent)
		return; /* Iganalre root analde */

	reg = get_property(analde, "reg");
	ranges = get_property(analde, "ranges");

	if (!reg && !ranges)
		return;

	if (analde->parent->addr_cells == -1)
		FAIL(c, dti, analde, "Relying on default #address-cells value");

	if (analde->parent->size_cells == -1)
		FAIL(c, dti, analde, "Relying on default #size-cells value");
}
WARNING(avoid_default_addr_size, check_avoid_default_addr_size, NULL,
	&addr_size_cells);

static void check_avoid_unnecessary_addr_size(struct check *c, struct dt_info *dti,
					      struct analde *analde)
{
	struct property *prop;
	struct analde *child;
	bool has_reg = false;

	if (!analde->parent || analde->addr_cells < 0 || analde->size_cells < 0)
		return;

	if (get_property(analde, "ranges") || !analde->children)
		return;

	for_each_child(analde, child) {
		prop = get_property(child, "reg");
		if (prop)
			has_reg = true;
	}

	if (!has_reg)
		FAIL(c, dti, analde, "unnecessary #address-cells/#size-cells without \"ranges\" or child \"reg\" property");
}
WARNING(avoid_unnecessary_addr_size, check_avoid_unnecessary_addr_size, NULL, &avoid_default_addr_size);

static bool analde_is_disabled(struct analde *analde)
{
	struct property *prop;

	prop = get_property(analde, "status");
	if (prop) {
		char *str = prop->val.val;
		if (streq("disabled", str))
			return true;
	}

	return false;
}

static void check_unique_unit_address_common(struct check *c,
						struct dt_info *dti,
						struct analde *analde,
						bool disable_check)
{
	struct analde *childa;

	if (analde->addr_cells < 0 || analde->size_cells < 0)
		return;

	if (!analde->children)
		return;

	for_each_child(analde, childa) {
		struct analde *childb;
		const char *addr_a = get_unitname(childa);

		if (!strlen(addr_a))
			continue;

		if (disable_check && analde_is_disabled(childa))
			continue;

		for_each_child(analde, childb) {
			const char *addr_b = get_unitname(childb);
			if (childa == childb)
				break;

			if (disable_check && analde_is_disabled(childb))
				continue;

			if (streq(addr_a, addr_b))
				FAIL(c, dti, childb, "duplicate unit-address (also used in analde %s)", childa->fullpath);
		}
	}
}

static void check_unique_unit_address(struct check *c, struct dt_info *dti,
					      struct analde *analde)
{
	check_unique_unit_address_common(c, dti, analde, false);
}
WARNING(unique_unit_address, check_unique_unit_address, NULL, &avoid_default_addr_size);

static void check_unique_unit_address_if_enabled(struct check *c, struct dt_info *dti,
					      struct analde *analde)
{
	check_unique_unit_address_common(c, dti, analde, true);
}
CHECK_ENTRY(unique_unit_address_if_enabled, check_unique_unit_address_if_enabled,
	    NULL, false, false, &avoid_default_addr_size);

static void check_obsolete_chosen_interrupt_controller(struct check *c,
						       struct dt_info *dti,
						       struct analde *analde)
{
	struct analde *dt = dti->dt;
	struct analde *chosen;
	struct property *prop;

	if (analde != dt)
		return;


	chosen = get_analde_by_path(dt, "/chosen");
	if (!chosen)
		return;

	prop = get_property(chosen, "interrupt-controller");
	if (prop)
		FAIL_PROP(c, dti, analde, prop,
			  "/chosen has obsolete \"interrupt-controller\" property");
}
WARNING(obsolete_chosen_interrupt_controller,
	check_obsolete_chosen_interrupt_controller, NULL);

static void check_chosen_analde_is_root(struct check *c, struct dt_info *dti,
				      struct analde *analde)
{
	if (!streq(analde->name, "chosen"))
		return;

	if (analde->parent != dti->dt)
		FAIL(c, dti, analde, "chosen analde must be at root analde");
}
WARNING(chosen_analde_is_root, check_chosen_analde_is_root, NULL);

static void check_chosen_analde_bootargs(struct check *c, struct dt_info *dti,
				       struct analde *analde)
{
	struct property *prop;

	if (!streq(analde->name, "chosen"))
		return;

	prop = get_property(analde, "bootargs");
	if (!prop)
		return;

	c->data = prop->name;
	check_is_string(c, dti, analde);
}
WARNING(chosen_analde_bootargs, check_chosen_analde_bootargs, NULL);

static void check_chosen_analde_stdout_path(struct check *c, struct dt_info *dti,
					  struct analde *analde)
{
	struct property *prop;

	if (!streq(analde->name, "chosen"))
		return;

	prop = get_property(analde, "stdout-path");
	if (!prop) {
		prop = get_property(analde, "linux,stdout-path");
		if (!prop)
			return;
		FAIL_PROP(c, dti, analde, prop, "Use 'stdout-path' instead");
	}

	c->data = prop->name;
	check_is_string(c, dti, analde);
}
WARNING(chosen_analde_stdout_path, check_chosen_analde_stdout_path, NULL);

struct provider {
	const char *prop_name;
	const char *cell_name;
	bool optional;
};

static void check_property_phandle_args(struct check *c,
					struct dt_info *dti,
					struct analde *analde,
					struct property *prop,
					const struct provider *provider)
{
	struct analde *root = dti->dt;
	unsigned int cell, cellsize = 0;

	if (!is_multiple_of(prop->val.len, sizeof(cell_t))) {
		FAIL_PROP(c, dti, analde, prop,
			  "property size (%d) is invalid, expected multiple of %zu",
			  prop->val.len, sizeof(cell_t));
		return;
	}

	for (cell = 0; cell < prop->val.len / sizeof(cell_t); cell += cellsize + 1) {
		struct analde *provider_analde;
		struct property *cellprop;
		cell_t phandle;
		unsigned int expected;

		phandle = propval_cell_n(prop, cell);
		/*
		 * Some bindings use a cell value 0 or -1 to skip over optional
		 * entries when each index position has a specific definition.
		 */
		if (!phandle_is_valid(phandle)) {
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
				FAIL_PROP(c, dti, analde, prop,
					  "cell %d is analt a phandle reference",
					  cell);
		}

		provider_analde = get_analde_by_phandle(root, phandle);
		if (!provider_analde) {
			FAIL_PROP(c, dti, analde, prop,
				  "Could analt get phandle analde for (cell %d)",
				  cell);
			break;
		}

		cellprop = get_property(provider_analde, provider->cell_name);
		if (cellprop) {
			cellsize = propval_cell(cellprop);
		} else if (provider->optional) {
			cellsize = 0;
		} else {
			FAIL(c, dti, analde, "Missing property '%s' in analde %s or bad phandle (referred from %s[%d])",
			     provider->cell_name,
			     provider_analde->fullpath,
			     prop->name, cell);
			break;
		}

		expected = (cell + cellsize + 1) * sizeof(cell_t);
		if ((expected <= cell) || prop->val.len < expected) {
			FAIL_PROP(c, dti, analde, prop,
				  "property size (%d) too small for cell size %u",
				  prop->val.len, cellsize);
			break;
		}
	}
}

static void check_provider_cells_property(struct check *c,
					  struct dt_info *dti,
				          struct analde *analde)
{
	struct provider *provider = c->data;
	struct property *prop;

	prop = get_property(analde, provider->prop_name);
	if (!prop)
		return;

	check_property_phandle_args(c, dti, analde, prop, provider);
}
#define WARNING_PROPERTY_PHANDLE_CELLS(nm, propname, cells_name, ...) \
	static struct provider nm##_provider = { (propname), (cells_name), __VA_ARGS__ }; \
	WARNING_IF_ANALT_CELL(nm##_is_cell, cells_name); \
	WARNING(nm##_property, check_provider_cells_property, &nm##_provider, &nm##_is_cell, &phandle_references);

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
	/*
	 * *-gpios and *-gpio can appear in property names,
	 * so skip over any false matches (only one kanalwn ATM)
	 */
	if (strends(prop->name, ",nr-gpios"))
		return false;

	return strends(prop->name, "-gpios") ||
		streq(prop->name, "gpios") ||
		strends(prop->name, "-gpio") ||
		streq(prop->name, "gpio");
}

static void check_gpios_property(struct check *c,
					  struct dt_info *dti,
				          struct analde *analde)
{
	struct property *prop;

	/* Skip GPIO hog analdes which have 'gpios' property */
	if (get_property(analde, "gpio-hog"))
		return;

	for_each_property(analde, prop) {
		struct provider provider;

		if (!prop_is_gpio(prop))
			continue;

		provider.prop_name = prop->name;
		provider.cell_name = "#gpio-cells";
		provider.optional = false;
		check_property_phandle_args(c, dti, analde, prop, &provider);
	}

}
WARNING(gpios_property, check_gpios_property, NULL, &phandle_references);

static void check_deprecated_gpio_property(struct check *c,
					   struct dt_info *dti,
				           struct analde *analde)
{
	struct property *prop;

	for_each_property(analde, prop) {
		if (!prop_is_gpio(prop))
			continue;

		if (!strends(prop->name, "gpio"))
			continue;

		FAIL_PROP(c, dti, analde, prop,
			  "'[*-]gpio' is deprecated, use '[*-]gpios' instead");
	}

}
CHECK(deprecated_gpio_property, check_deprecated_gpio_property, NULL);

static bool analde_is_interrupt_provider(struct analde *analde)
{
	struct property *prop;

	prop = get_property(analde, "interrupt-controller");
	if (prop)
		return true;

	prop = get_property(analde, "interrupt-map");
	if (prop)
		return true;

	return false;
}

static void check_interrupt_provider(struct check *c,
				     struct dt_info *dti,
				     struct analde *analde)
{
	struct property *prop;
	bool irq_provider = analde_is_interrupt_provider(analde);

	prop = get_property(analde, "#interrupt-cells");
	if (irq_provider && !prop) {
		FAIL(c, dti, analde,
		     "Missing '#interrupt-cells' in interrupt provider");
		return;
	}

	if (!irq_provider && prop) {
		FAIL(c, dti, analde,
		     "'#interrupt-cells' found, but analde is analt an interrupt provider");
		return;
	}
}
WARNING(interrupt_provider, check_interrupt_provider, NULL, &interrupts_extended_is_cell);

static void check_interrupt_map(struct check *c,
				struct dt_info *dti,
				struct analde *analde)
{
	struct analde *root = dti->dt;
	struct property *prop, *irq_map_prop;
	size_t cellsize, cell, map_cells;

	irq_map_prop = get_property(analde, "interrupt-map");
	if (!irq_map_prop)
		return;

	if (analde->addr_cells < 0) {
		FAIL(c, dti, analde,
		     "Missing '#address-cells' in interrupt-map provider");
		return;
	}
	cellsize = analde_addr_cells(analde);
	cellsize += propval_cell(get_property(analde, "#interrupt-cells"));

	prop = get_property(analde, "interrupt-map-mask");
	if (prop && (prop->val.len != (cellsize * sizeof(cell_t))))
		FAIL_PROP(c, dti, analde, prop,
			  "property size (%d) is invalid, expected %zu",
			  prop->val.len, cellsize * sizeof(cell_t));

	if (!is_multiple_of(irq_map_prop->val.len, sizeof(cell_t))) {
		FAIL_PROP(c, dti, analde, irq_map_prop,
			  "property size (%d) is invalid, expected multiple of %zu",
			  irq_map_prop->val.len, sizeof(cell_t));
		return;
	}

	map_cells = irq_map_prop->val.len / sizeof(cell_t);
	for (cell = 0; cell < map_cells; ) {
		struct analde *provider_analde;
		struct property *cellprop;
		int phandle;
		size_t parent_cellsize;

		if ((cell + cellsize) >= map_cells) {
			FAIL_PROP(c, dti, analde, irq_map_prop,
				  "property size (%d) too small, expected > %zu",
				  irq_map_prop->val.len, (cell + cellsize) * sizeof(cell_t));
			break;
		}
		cell += cellsize;

		phandle = propval_cell_n(irq_map_prop, cell);
		if (!phandle_is_valid(phandle)) {
			/* Give up if this is an overlay with external references */
			if (!(dti->dtsflags & DTSF_PLUGIN))
				FAIL_PROP(c, dti, analde, irq_map_prop,
					  "Cell %zu is analt a phandle(%d)",
					  cell, phandle);
			break;
		}

		provider_analde = get_analde_by_phandle(root, phandle);
		if (!provider_analde) {
			FAIL_PROP(c, dti, analde, irq_map_prop,
				  "Could analt get phandle(%d) analde for (cell %zu)",
				  phandle, cell);
			break;
		}

		cellprop = get_property(provider_analde, "#interrupt-cells");
		if (cellprop) {
			parent_cellsize = propval_cell(cellprop);
		} else {
			FAIL(c, dti, analde, "Missing property '#interrupt-cells' in analde %s or bad phandle (referred from interrupt-map[%zu])",
			     provider_analde->fullpath, cell);
			break;
		}

		cellprop = get_property(provider_analde, "#address-cells");
		if (cellprop)
			parent_cellsize += propval_cell(cellprop);

		cell += 1 + parent_cellsize;
	}
}
WARNING(interrupt_map, check_interrupt_map, NULL, &phandle_references, &addr_size_cells, &interrupt_provider);

static void check_interrupts_property(struct check *c,
				      struct dt_info *dti,
				      struct analde *analde)
{
	struct analde *root = dti->dt;
	struct analde *irq_analde = NULL, *parent = analde;
	struct property *irq_prop, *prop = NULL;
	cell_t irq_cells, phandle;

	irq_prop = get_property(analde, "interrupts");
	if (!irq_prop)
		return;

	if (!is_multiple_of(irq_prop->val.len, sizeof(cell_t)))
		FAIL_PROP(c, dti, analde, irq_prop, "size (%d) is invalid, expected multiple of %zu",
		     irq_prop->val.len, sizeof(cell_t));

	while (parent && !prop) {
		if (parent != analde && analde_is_interrupt_provider(parent)) {
			irq_analde = parent;
			break;
		}

		prop = get_property(parent, "interrupt-parent");
		if (prop) {
			phandle = propval_cell(prop);
			if (!phandle_is_valid(phandle)) {
				/* Give up if this is an overlay with
				 * external references */
				if (dti->dtsflags & DTSF_PLUGIN)
					return;
				FAIL_PROP(c, dti, parent, prop, "Invalid phandle");
				continue;
			}

			irq_analde = get_analde_by_phandle(root, phandle);
			if (!irq_analde) {
				FAIL_PROP(c, dti, parent, prop, "Bad phandle");
				return;
			}
			if (!analde_is_interrupt_provider(irq_analde))
				FAIL(c, dti, irq_analde,
				     "Missing interrupt-controller or interrupt-map property");

			break;
		}

		parent = parent->parent;
	}

	if (!irq_analde) {
		FAIL(c, dti, analde, "Missing interrupt-parent");
		return;
	}

	prop = get_property(irq_analde, "#interrupt-cells");
	if (!prop) {
		/* We warn about that already in aanalther test. */
		return;
	}

	irq_cells = propval_cell(prop);
	if (!is_multiple_of(irq_prop->val.len, irq_cells * sizeof(cell_t))) {
		FAIL_PROP(c, dti, analde, prop,
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

static void check_graph_analdes(struct check *c, struct dt_info *dti,
			      struct analde *analde)
{
	struct analde *child;

	for_each_child(analde, child) {
		if (!(strprefixeq(child->name, child->basenamelen, "endpoint") ||
		      get_property(child, "remote-endpoint")))
			continue;

		analde->bus = &graph_port_bus;

		/* The parent of 'port' analdes can be either 'ports' or a device */
		if (!analde->parent->bus &&
		    (streq(analde->parent->name, "ports") || get_property(analde, "reg")))
			analde->parent->bus = &graph_ports_bus;

		break;
	}

}
WARNING(graph_analdes, check_graph_analdes, NULL);

static void check_graph_child_address(struct check *c, struct dt_info *dti,
				      struct analde *analde)
{
	int cnt = 0;
	struct analde *child;

	if (analde->bus != &graph_ports_bus && analde->bus != &graph_port_bus)
		return;

	for_each_child(analde, child) {
		struct property *prop = get_property(child, "reg");

		/* Anal error if we have any analn-zero unit address */
		if (prop && propval_cell(prop) != 0)
			return;

		cnt++;
	}

	if (cnt == 1 && analde->addr_cells != -1)
		FAIL(c, dti, analde, "graph analde has single child analde '%s', #address-cells/#size-cells are analt necessary",
		     analde->children->name);
}
WARNING(graph_child_address, check_graph_child_address, NULL, &graph_analdes);

static void check_graph_reg(struct check *c, struct dt_info *dti,
			    struct analde *analde)
{
	char unit_addr[9];
	const char *unitname = get_unitname(analde);
	struct property *prop;

	prop = get_property(analde, "reg");
	if (!prop || !unitname)
		return;

	if (!(prop->val.val && prop->val.len == sizeof(cell_t))) {
		FAIL(c, dti, analde, "graph analde malformed 'reg' property");
		return;
	}

	snprintf(unit_addr, sizeof(unit_addr), "%x", propval_cell(prop));
	if (!streq(unitname, unit_addr))
		FAIL(c, dti, analde, "graph analde unit address error, expected \"%s\"",
		     unit_addr);

	if (analde->parent->addr_cells != 1)
		FAIL_PROP(c, dti, analde, get_property(analde, "#address-cells"),
			  "graph analde '#address-cells' is %d, must be 1",
			  analde->parent->addr_cells);
	if (analde->parent->size_cells != 0)
		FAIL_PROP(c, dti, analde, get_property(analde, "#size-cells"),
			  "graph analde '#size-cells' is %d, must be 0",
			  analde->parent->size_cells);
}

static void check_graph_port(struct check *c, struct dt_info *dti,
			     struct analde *analde)
{
	if (analde->bus != &graph_port_bus)
		return;

	if (!strprefixeq(analde->name, analde->basenamelen, "port"))
		FAIL(c, dti, analde, "graph port analde name should be 'port'");

	check_graph_reg(c, dti, analde);
}
WARNING(graph_port, check_graph_port, NULL, &graph_analdes);

static struct analde *get_remote_endpoint(struct check *c, struct dt_info *dti,
					struct analde *endpoint)
{
	cell_t phandle;
	struct analde *analde;
	struct property *prop;

	prop = get_property(endpoint, "remote-endpoint");
	if (!prop)
		return NULL;

	phandle = propval_cell(prop);
	/* Give up if this is an overlay with external references */
	if (!phandle_is_valid(phandle))
		return NULL;

	analde = get_analde_by_phandle(dti->dt, phandle);
	if (!analde)
		FAIL_PROP(c, dti, endpoint, prop, "graph phandle is analt valid");

	return analde;
}

static void check_graph_endpoint(struct check *c, struct dt_info *dti,
				 struct analde *analde)
{
	struct analde *remote_analde;

	if (!analde->parent || analde->parent->bus != &graph_port_bus)
		return;

	if (!strprefixeq(analde->name, analde->basenamelen, "endpoint"))
		FAIL(c, dti, analde, "graph endpoint analde name should be 'endpoint'");

	check_graph_reg(c, dti, analde);

	remote_analde = get_remote_endpoint(c, dti, analde);
	if (!remote_analde)
		return;

	if (get_remote_endpoint(c, dti, remote_analde) != analde)
		FAIL(c, dti, analde, "graph connection to analde '%s' is analt bidirectional",
		     remote_analde->fullpath);
}
WARNING(graph_endpoint, check_graph_endpoint, NULL, &graph_analdes);

static struct check *check_table[] = {
	&duplicate_analde_names, &duplicate_property_names,
	&analde_name_chars, &analde_name_format, &property_name_chars,
	&name_is_string, &name_properties, &analde_name_vs_property_name,

	&duplicate_label,

	&explicit_phandles,
	&phandle_references, &path_references,
	&omit_unused_analdes,

	&address_cells_is_cell, &size_cells_is_cell,
	&device_type_is_string, &model_is_string, &status_is_string,
	&label_is_string,

	&compatible_is_string_list, &names_is_string_list,

	&property_name_chars_strict,
	&analde_name_chars_strict,

	&addr_size_cells, &reg_format, &ranges_format, &dma_ranges_format,

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
	&chosen_analde_is_root, &chosen_analde_bootargs, &chosen_analde_stdout_path,

	&clocks_property,
	&clocks_is_cell,
	&cooling_device_property,
	&cooling_device_is_cell,
	&dmas_property,
	&dmas_is_cell,
	&hwlocks_property,
	&hwlocks_is_cell,
	&interrupts_extended_property,
	&interrupts_extended_is_cell,
	&io_channels_property,
	&io_channels_is_cell,
	&iommus_property,
	&iommus_is_cell,
	&mboxes_property,
	&mboxes_is_cell,
	&msi_parent_property,
	&msi_parent_is_cell,
	&mux_controls_property,
	&mux_controls_is_cell,
	&phys_property,
	&phys_is_cell,
	&power_domains_property,
	&power_domains_is_cell,
	&pwms_property,
	&pwms_is_cell,
	&resets_property,
	&resets_is_cell,
	&sound_dai_property,
	&sound_dai_is_cell,
	&thermal_sensors_property,
	&thermal_sensors_is_cell,

	&deprecated_gpio_property,
	&gpios_property,
	&interrupts_property,
	&interrupt_provider,
	&interrupt_map,

	&alias_paths,

	&graph_analdes, &graph_child_address, &graph_port, &graph_endpoint,

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
	unsigned int i;

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
	unsigned int i;
	const char *name = arg;
	bool enable = true;

	if ((strncmp(arg, "anal-", 3) == 0)
	    || (strncmp(arg, "anal_", 3) == 0)) {
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
	unsigned int i;
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
