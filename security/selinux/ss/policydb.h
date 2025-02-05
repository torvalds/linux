/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * A policy database (policydb) specifies the
 * configuration data for the security policy.
 *
 * Author : Stephen Smalley, <stephen.smalley.work@gmail.com>
 */

/*
 * Updated: Trusted Computer Solutions, Inc. <dgoeddel@trustedcs.com>
 *          Support for enhanced MLS infrastructure.
 *          Copyright (C) 2004-2005 Trusted Computer Solutions, Inc.
 *
 * Updated: Frank Mayer <mayerf@tresys.com> and
 *          Karl MacMillan <kmacmillan@tresys.com>
 *          Added conditional policy language extensions
 *          Copyright (C) 2003-2004 Tresys Technology, LLC
 */

#ifndef _SS_POLICYDB_H_
#define _SS_POLICYDB_H_

#include "symtab.h"
#include "avtab.h"
#include "sidtab.h"
#include "ebitmap.h"
#include "mls_types.h"
#include "context.h"
#include "constraint.h"

/*
 * A datum type is defined for each kind of symbol
 * in the configuration data:  individual permissions,
 * common prefixes for access vectors, classes,
 * users, roles, types, sensitivities, categories, etc.
 */

/* Permission attributes */
struct perm_datum {
	u32 value; /* permission bit + 1 */
};

/* Attributes of a common prefix for access vectors */
struct common_datum {
	u32 value; /* internal common value */
	struct symtab permissions; /* common permissions */
};

/* Class attributes */
struct class_datum {
	u32 value; /* class value */
	char *comkey; /* common name */
	struct common_datum *comdatum; /* common datum */
	struct symtab permissions; /* class-specific permission symbol table */
	struct constraint_node *constraints; /* constraints on class perms */
	struct constraint_node *validatetrans; /* special transition rules */
/* Options how a new object user, role, and type should be decided */
#define DEFAULT_SOURCE 1
#define DEFAULT_TARGET 2
	char default_user;
	char default_role;
	char default_type;
/* Options how a new object range should be decided */
#define DEFAULT_SOURCE_LOW	1
#define DEFAULT_SOURCE_HIGH	2
#define DEFAULT_SOURCE_LOW_HIGH 3
#define DEFAULT_TARGET_LOW	4
#define DEFAULT_TARGET_HIGH	5
#define DEFAULT_TARGET_LOW_HIGH 6
#define DEFAULT_GLBLUB		7
	char default_range;
};

/* Role attributes */
struct role_datum {
	u32 value; /* internal role value */
	u32 bounds; /* boundary of role */
	struct ebitmap dominates; /* set of roles dominated by this role */
	struct ebitmap types; /* set of authorized types for role */
};

struct role_trans_key {
	u32 role; /* current role */
	u32 type; /* program executable type, or new object type */
	u32 tclass; /* process class, or new object class */
};

struct role_trans_datum {
	u32 new_role; /* new role */
};

struct filename_trans_key {
	u32 ttype; /* parent dir context */
	u16 tclass; /* class of new object */
	const char *name; /* last path component */
};

struct filename_trans_datum {
	struct ebitmap stypes; /* bitmap of source types for this otype */
	u32 otype; /* resulting type of new object */
	struct filename_trans_datum *next; /* record for next otype*/
};

struct role_allow {
	u32 role; /* current role */
	u32 new_role; /* new role */
	struct role_allow *next;
};

/* Type attributes */
struct type_datum {
	u32 value; /* internal type value */
	u32 bounds; /* boundary of type */
	unsigned char primary; /* primary name? */
	unsigned char attribute; /* attribute ?*/
};

/* User attributes */
struct user_datum {
	u32 value; /* internal user value */
	u32 bounds; /* bounds of user */
	struct ebitmap roles; /* set of authorized roles for user */
	struct mls_range range; /* MLS range (min - max) for user */
	struct mls_level dfltlevel; /* default login MLS level for user */
};

/* Sensitivity attributes */
struct level_datum {
	struct mls_level level; /* sensitivity and associated categories */
	unsigned char isalias; /* is this sensitivity an alias for another? */
};

/* Category attributes */
struct cat_datum {
	u32 value; /* internal category bit + 1 */
	unsigned char isalias; /* is this category an alias for another? */
};

struct range_trans {
	u32 source_type;
	u32 target_type;
	u32 target_class;
};

/* Boolean data type */
struct cond_bool_datum {
	u32 value; /* internal type value */
	int state;
};

struct cond_node;

/*
 * type set preserves data needed to determine constraint info from
 * policy source. This is not used by the kernel policy but allows
 * utilities such as audit2allow to determine constraint denials.
 */
struct type_set {
	struct ebitmap types;
	struct ebitmap negset;
	u32 flags;
};

/*
 * The configuration data includes security contexts for
 * initial SIDs, unlabeled file systems, TCP and UDP port numbers,
 * network interfaces, and nodes.  This structure stores the
 * relevant data for one such entry.  Entries of the same kind
 * (e.g. all initial SIDs) are linked together into a list.
 */
struct ocontext {
	union {
		char *name; /* name of initial SID, fs, netif, fstype, path */
		struct {
			u8 protocol;
			u16 low_port;
			u16 high_port;
		} port; /* TCP or UDP port information */
		struct {
			u32 addr;
			u32 mask;
		} node; /* node information */
		struct {
			u32 addr[4];
			u32 mask[4];
		} node6; /* IPv6 node information */
		struct {
			u64 subnet_prefix;
			u16 low_pkey;
			u16 high_pkey;
		} ibpkey;
		struct {
			char *dev_name;
			u8 port;
		} ibendport;
	} u;
	union {
		u32 sclass; /* security class for genfs */
		u32 behavior; /* labeling behavior for fs_use */
	} v;
	struct context context[2]; /* security context(s) */
	u32 sid[2]; /* SID(s) */
	struct ocontext *next;
};

struct genfs {
	char *fstype;
	struct ocontext *head;
	struct genfs *next;
};

/* symbol table array indices */
#define SYM_COMMONS 0
#define SYM_CLASSES 1
#define SYM_ROLES   2
#define SYM_TYPES   3
#define SYM_USERS   4
#define SYM_BOOLS   5
#define SYM_LEVELS  6
#define SYM_CATS    7
#define SYM_NUM	    8

/* object context array indices */
#define OCON_ISID      0 /* initial SIDs */
#define OCON_FS	       1 /* unlabeled file systems (deprecated) */
#define OCON_PORT      2 /* TCP and UDP port numbers */
#define OCON_NETIF     3 /* network interfaces */
#define OCON_NODE      4 /* nodes */
#define OCON_FSUSE     5 /* fs_use */
#define OCON_NODE6     6 /* IPv6 nodes */
#define OCON_IBPKEY    7 /* Infiniband PKeys */
#define OCON_IBENDPORT 8 /* Infiniband end ports */
#define OCON_NUM       9

/* The policy database */
struct policydb {
	int mls_enabled;

	/* symbol tables */
	struct symtab symtab[SYM_NUM];
#define p_commons symtab[SYM_COMMONS]
#define p_classes symtab[SYM_CLASSES]
#define p_roles	  symtab[SYM_ROLES]
#define p_types	  symtab[SYM_TYPES]
#define p_users	  symtab[SYM_USERS]
#define p_bools	  symtab[SYM_BOOLS]
#define p_levels  symtab[SYM_LEVELS]
#define p_cats	  symtab[SYM_CATS]

	/* symbol names indexed by (value - 1) */
	char **sym_val_to_name[SYM_NUM];

	/* class, role, and user attributes indexed by (value - 1) */
	struct class_datum **class_val_to_struct;
	struct role_datum **role_val_to_struct;
	struct user_datum **user_val_to_struct;
	struct type_datum **type_val_to_struct;

	/* type enforcement access vectors and transitions */
	struct avtab te_avtab;

	/* role transitions */
	struct hashtab role_tr;

	/* file transitions with the last path component */
	/* quickly exclude lookups when parent ttype has no rules */
	struct ebitmap filename_trans_ttypes;
	/* actual set of filename_trans rules */
	struct hashtab filename_trans;
	/* only used if policyvers < POLICYDB_VERSION_COMP_FTRANS */
	u32 compat_filename_trans_count;

	/* bools indexed by (value - 1) */
	struct cond_bool_datum **bool_val_to_struct;
	/* type enforcement conditional access vectors and transitions */
	struct avtab te_cond_avtab;
	/* array indexing te_cond_avtab by conditional */
	struct cond_node *cond_list;
	u32 cond_list_len;

	/* role allows */
	struct role_allow *role_allow;

	/* security contexts of initial SIDs, unlabeled file systems,
	   TCP or UDP port numbers, network interfaces and nodes */
	struct ocontext *ocontexts[OCON_NUM];

	/* security contexts for files in filesystems that cannot support
	   a persistent label mapping or use another
	   fixed labeling behavior. */
	struct genfs *genfs;

	/* range transitions table (range_trans_key -> mls_range) */
	struct hashtab range_tr;

	/* type -> attribute reverse mapping */
	struct ebitmap *type_attr_map_array;

	struct ebitmap policycaps;

	struct ebitmap permissive_map;

	/* length of this policy when it was loaded */
	size_t len;

	unsigned int policyvers;

	unsigned int reject_unknown : 1;
	unsigned int allow_unknown : 1;

	u16 process_class;
	u32 process_trans_perms;
} __randomize_layout;

struct policy_file {
	char *data;
	size_t len;
};

extern void policydb_destroy(struct policydb *p);
extern int policydb_load_isids(struct policydb *p, struct sidtab *s);
extern int policydb_context_isvalid(struct policydb *p, struct context *c);
extern int policydb_class_isvalid(struct policydb *p, unsigned int class);
extern int policydb_type_isvalid(struct policydb *p, unsigned int type);
extern int policydb_role_isvalid(struct policydb *p, unsigned int role);
extern int policydb_read(struct policydb *p, struct policy_file *fp);
extern int policydb_write(struct policydb *p, struct policy_file *fp);

extern struct filename_trans_datum *
policydb_filenametr_search(struct policydb *p, struct filename_trans_key *key);

extern struct mls_range *policydb_rangetr_search(struct policydb *p,
						 struct range_trans *key);

extern struct role_trans_datum *
policydb_roletr_search(struct policydb *p, struct role_trans_key *key);

#define POLICYDB_CONFIG_MLS 1

/* the config flags related to unknown classes/perms are bits 2 and 3 */
#define REJECT_UNKNOWN 0x00000002
#define ALLOW_UNKNOWN  0x00000004

#define OBJECT_R     "object_r"
#define OBJECT_R_VAL 1

#define POLICYDB_MAGIC	SELINUX_MAGIC
#define POLICYDB_STRING "SE Linux"

struct policy_data {
	struct policydb *p;
	struct policy_file *fp;
};

static inline int next_entry(void *buf, struct policy_file *fp, size_t bytes)
{
	if (bytes > fp->len)
		return -EINVAL;

	memcpy(buf, fp->data, bytes);
	fp->data += bytes;
	fp->len -= bytes;
	return 0;
}

static inline int put_entry(const void *buf, size_t bytes, size_t num,
			    struct policy_file *fp)
{
	size_t len;

	if (unlikely(check_mul_overflow(bytes, num, &len)))
		return -EINVAL;

	if (len > fp->len)
		return -EINVAL;
	memcpy(fp->data, buf, len);
	fp->data += len;
	fp->len -= len;

	return 0;
}

static inline char *sym_name(struct policydb *p, unsigned int sym_num,
			     unsigned int element_nr)
{
	return p->sym_val_to_name[sym_num][element_nr];
}

extern int str_read(char **strp, gfp_t flags, struct policy_file *fp, u32 len);

extern u16 string_to_security_class(struct policydb *p, const char *name);
extern u32 string_to_av_perm(struct policydb *p, u16 tclass, const char *name);

#endif /* _SS_POLICYDB_H_ */
