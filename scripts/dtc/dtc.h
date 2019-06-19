/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef DTC_H
#define DTC_H

/*
 * (C) Copyright David Gibson <dwg@au1.ibm.com>, IBM Corporation.  2005.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <inttypes.h>

#include <libfdt_env.h>
#include <fdt.h>

#include "util.h"

#ifdef DEBUG
#define debug(...)	printf(__VA_ARGS__)
#else
#define debug(...)
#endif

#define DEFAULT_FDT_VERSION	17

/*
 * Command line options
 */
extern int quiet;		/* Level of quietness */
extern int reservenum;		/* Number of memory reservation slots */
extern int minsize;		/* Minimum blob size */
extern int padsize;		/* Additional padding to blob */
extern int alignsize;		/* Additional padding to blob accroding to the alignsize */
extern int phandle_format;	/* Use linux,phandle or phandle properties */
extern int generate_symbols;	/* generate symbols for nodes with labels */
extern int generate_fixups;	/* generate fixups */
extern int auto_label_aliases;	/* auto generate labels -> aliases */
extern int annotate;		/* annotate .dts with input source location */

#define PHANDLE_LEGACY	0x1
#define PHANDLE_EPAPR	0x2
#define PHANDLE_BOTH	0x3

typedef uint32_t cell_t;


#define streq(a, b)	(strcmp((a), (b)) == 0)
#define strstarts(s, prefix)	(strncmp((s), (prefix), strlen(prefix)) == 0)
#define strprefixeq(a, n, b)	(strlen(b) == (n) && (memcmp(a, b, n) == 0))

#define ALIGN(x, a)	(((x) + (a) - 1) & ~((a) - 1))

/* Data blobs */
enum markertype {
	TYPE_NONE,
	REF_PHANDLE,
	REF_PATH,
	LABEL,
	TYPE_UINT8,
	TYPE_UINT16,
	TYPE_UINT32,
	TYPE_UINT64,
	TYPE_STRING,
};
extern const char *markername(enum markertype markertype);

struct  marker {
	enum markertype type;
	int offset;
	char *ref;
	struct marker *next;
};

struct data {
	int len;
	char *val;
	struct marker *markers;
};


#define empty_data ((struct data){ 0 /* all .members = 0 or NULL */ })

#define for_each_marker(m) \
	for (; (m); (m) = (m)->next)
#define for_each_marker_of_type(m, t) \
	for_each_marker(m) \
		if ((m)->type == (t))

size_t type_marker_length(struct marker *m);

void data_free(struct data d);

struct data data_grow_for(struct data d, int xlen);

struct data data_copy_mem(const char *mem, int len);
struct data data_copy_escape_string(const char *s, int len);
struct data data_copy_file(FILE *f, size_t len);

struct data data_append_data(struct data d, const void *p, int len);
struct data data_insert_at_marker(struct data d, struct marker *m,
				  const void *p, int len);
struct data data_merge(struct data d1, struct data d2);
struct data data_append_cell(struct data d, cell_t word);
struct data data_append_integer(struct data d, uint64_t word, int bits);
struct data data_append_re(struct data d, uint64_t address, uint64_t size);
struct data data_append_addr(struct data d, uint64_t addr);
struct data data_append_byte(struct data d, uint8_t byte);
struct data data_append_zeroes(struct data d, int len);
struct data data_append_align(struct data d, int align);

struct data data_add_marker(struct data d, enum markertype type, char *ref);

bool data_is_one_string(struct data d);

/* DT constraints */

#define MAX_PROPNAME_LEN	31
#define MAX_NODENAME_LEN	31

/* Live trees */
struct label {
	bool deleted;
	char *label;
	struct label *next;
};

struct bus_type {
	const char *name;
};

struct property {
	bool deleted;
	char *name;
	struct data val;

	struct property *next;

	struct label *labels;
	struct srcpos *srcpos;
};

struct node {
	bool deleted;
	char *name;
	struct property *proplist;
	struct node *children;

	struct node *parent;
	struct node *next_sibling;

	char *fullpath;
	int basenamelen;

	cell_t phandle;
	int addr_cells, size_cells;

	struct label *labels;
	const struct bus_type *bus;
	struct srcpos *srcpos;

	bool omit_if_unused, is_referenced;
};

#define for_each_label_withdel(l0, l) \
	for ((l) = (l0); (l); (l) = (l)->next)

#define for_each_label(l0, l) \
	for_each_label_withdel(l0, l) \
		if (!(l)->deleted)

#define for_each_property_withdel(n, p) \
	for ((p) = (n)->proplist; (p); (p) = (p)->next)

#define for_each_property(n, p) \
	for_each_property_withdel(n, p) \
		if (!(p)->deleted)

#define for_each_child_withdel(n, c) \
	for ((c) = (n)->children; (c); (c) = (c)->next_sibling)

#define for_each_child(n, c) \
	for_each_child_withdel(n, c) \
		if (!(c)->deleted)

void add_label(struct label **labels, char *label);
void delete_labels(struct label **labels);

struct property *build_property(char *name, struct data val,
				struct srcpos *srcpos);
struct property *build_property_delete(char *name);
struct property *chain_property(struct property *first, struct property *list);
struct property *reverse_properties(struct property *first);

struct node *build_node(struct property *proplist, struct node *children,
			struct srcpos *srcpos);
struct node *build_node_delete(struct srcpos *srcpos);
struct node *name_node(struct node *node, char *name);
struct node *omit_node_if_unused(struct node *node);
struct node *reference_node(struct node *node);
struct node *chain_node(struct node *first, struct node *list);
struct node *merge_nodes(struct node *old_node, struct node *new_node);
struct node *add_orphan_node(struct node *old_node, struct node *new_node, char *ref);

void add_property(struct node *node, struct property *prop);
void delete_property_by_name(struct node *node, char *name);
void delete_property(struct property *prop);
void add_child(struct node *parent, struct node *child);
void delete_node_by_name(struct node *parent, char *name);
void delete_node(struct node *node);
void append_to_property(struct node *node,
			char *name, const void *data, int len);

const char *get_unitname(struct node *node);
struct property *get_property(struct node *node, const char *propname);
cell_t propval_cell(struct property *prop);
cell_t propval_cell_n(struct property *prop, int n);
struct property *get_property_by_label(struct node *tree, const char *label,
				       struct node **node);
struct marker *get_marker_label(struct node *tree, const char *label,
				struct node **node, struct property **prop);
struct node *get_subnode(struct node *node, const char *nodename);
struct node *get_node_by_path(struct node *tree, const char *path);
struct node *get_node_by_label(struct node *tree, const char *label);
struct node *get_node_by_phandle(struct node *tree, cell_t phandle);
struct node *get_node_by_ref(struct node *tree, const char *ref);
cell_t get_node_phandle(struct node *root, struct node *node);

uint32_t guess_boot_cpuid(struct node *tree);

/* Boot info (tree plus memreserve information */

struct reserve_info {
	uint64_t address, size;

	struct reserve_info *next;

	struct label *labels;
};

struct reserve_info *build_reserve_entry(uint64_t start, uint64_t len);
struct reserve_info *chain_reserve_entry(struct reserve_info *first,
					 struct reserve_info *list);
struct reserve_info *add_reserve_entry(struct reserve_info *list,
				       struct reserve_info *new);


struct dt_info {
	unsigned int dtsflags;
	struct reserve_info *reservelist;
	uint32_t boot_cpuid_phys;
	struct node *dt;		/* the device tree */
	const char *outname;		/* filename being written to, "-" for stdout */
};

/* DTS version flags definitions */
#define DTSF_V1		0x0001	/* /dts-v1/ */
#define DTSF_PLUGIN	0x0002	/* /plugin/ */

struct dt_info *build_dt_info(unsigned int dtsflags,
			      struct reserve_info *reservelist,
			      struct node *tree, uint32_t boot_cpuid_phys);
void sort_tree(struct dt_info *dti);
void generate_label_tree(struct dt_info *dti, char *name, bool allocph);
void generate_fixups_tree(struct dt_info *dti, char *name);
void generate_local_fixups_tree(struct dt_info *dti, char *name);

/* Checks */

void parse_checks_option(bool warn, bool error, const char *arg);
void process_checks(bool force, struct dt_info *dti);

/* Flattened trees */

void dt_to_blob(FILE *f, struct dt_info *dti, int version);
void dt_to_asm(FILE *f, struct dt_info *dti, int version);

struct dt_info *dt_from_blob(const char *fname);

/* Tree source */

void dt_to_source(FILE *f, struct dt_info *dti);
struct dt_info *dt_from_source(const char *f);

/* YAML source */

void dt_to_yaml(FILE *f, struct dt_info *dti);

/* FS trees */

struct dt_info *dt_from_fs(const char *dirname);

#endif /* DTC_H */
