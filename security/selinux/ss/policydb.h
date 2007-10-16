/*
 * A policy database (policydb) specifies the
 * configuration data for the security policy.
 *
 * Author : Stephen Smalley, <sds@epoch.ncsc.mil>
 */

/*
 * Updated: Trusted Computer Solutions, Inc. <dgoeddel@trustedcs.com>
 *
 *	Support for enhanced MLS infrastructure.
 *
 * Updated: Frank Mayer <mayerf@tresys.com> and Karl MacMillan <kmacmillan@tresys.com>
 *
 * 	Added conditional policy language extensions
 *
 * Copyright (C) 2004-2005 Trusted Computer Solutions, Inc.
 * Copyright (C) 2003 - 2004 Tresys Technology, LLC
 *	This program is free software; you can redistribute it and/or modify
 *  	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, version 2.
 */

#ifndef _SS_POLICYDB_H_
#define _SS_POLICYDB_H_

#include "symtab.h"
#include "avtab.h"
#include "sidtab.h"
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
	u32 value;		/* permission bit + 1 */
};

/* Attributes of a common prefix for access vectors */
struct common_datum {
	u32 value;			/* internal common value */
	struct symtab permissions;	/* common permissions */
};

/* Class attributes */
struct class_datum {
	u32 value;			/* class value */
	char *comkey;			/* common name */
	struct common_datum *comdatum;	/* common datum */
	struct symtab permissions;	/* class-specific permission symbol table */
	struct constraint_node *constraints;	/* constraints on class permissions */
	struct constraint_node *validatetrans;	/* special transition rules */
};

/* Role attributes */
struct role_datum {
	u32 value;			/* internal role value */
	struct ebitmap dominates;	/* set of roles dominated by this role */
	struct ebitmap types;		/* set of authorized types for role */
};

struct role_trans {
	u32 role;		/* current role */
	u32 type;		/* program executable type */
	u32 new_role;		/* new role */
	struct role_trans *next;
};

struct role_allow {
	u32 role;		/* current role */
	u32 new_role;		/* new role */
	struct role_allow *next;
};

/* Type attributes */
struct type_datum {
	u32 value;		/* internal type value */
	unsigned char primary;	/* primary name? */
};

/* User attributes */
struct user_datum {
	u32 value;			/* internal user value */
	struct ebitmap roles;		/* set of authorized roles for user */
	struct mls_range range;		/* MLS range (min - max) for user */
	struct mls_level dfltlevel;	/* default login MLS level for user */
};


/* Sensitivity attributes */
struct level_datum {
	struct mls_level *level;	/* sensitivity and associated categories */
	unsigned char isalias;	/* is this sensitivity an alias for another? */
};

/* Category attributes */
struct cat_datum {
	u32 value;		/* internal category bit + 1 */
	unsigned char isalias;  /* is this category an alias for another? */
};

struct range_trans {
	u32 source_type;
	u32 target_type;
	u32 target_class;
	struct mls_range target_range;
	struct range_trans *next;
};

/* Boolean data type */
struct cond_bool_datum {
	__u32 value;		/* internal type value */
	int state;
};

struct cond_node;

/*
 * The configuration data includes security contexts for
 * initial SIDs, unlabeled file systems, TCP and UDP port numbers,
 * network interfaces, and nodes.  This structure stores the
 * relevant data for one such entry.  Entries of the same kind
 * (e.g. all initial SIDs) are linked together into a list.
 */
struct ocontext {
	union {
		char *name;	/* name of initial SID, fs, netif, fstype, path */
		struct {
			u8 protocol;
			u16 low_port;
			u16 high_port;
		} port;		/* TCP or UDP port information */
		struct {
			u32 addr;
			u32 mask;
		} node;		/* node information */
		struct {
			u32 addr[4];
			u32 mask[4];
		} node6;        /* IPv6 node information */
	} u;
	union {
		u32 sclass;  /* security class for genfs */
		u32 behavior;  /* labeling behavior for fs_use */
	} v;
	struct context context[2];	/* security context(s) */
	u32 sid[2];	/* SID(s) */
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
#define SYM_NUM     8

/* object context array indices */
#define OCON_ISID  0	/* initial SIDs */
#define OCON_FS    1	/* unlabeled file systems */
#define OCON_PORT  2	/* TCP and UDP port numbers */
#define OCON_NETIF 3	/* network interfaces */
#define OCON_NODE  4	/* nodes */
#define OCON_FSUSE 5	/* fs_use */
#define OCON_NODE6 6	/* IPv6 nodes */
#define OCON_NUM   7

/* The policy database */
struct policydb {
	/* symbol tables */
	struct symtab symtab[SYM_NUM];
#define p_commons symtab[SYM_COMMONS]
#define p_classes symtab[SYM_CLASSES]
#define p_roles symtab[SYM_ROLES]
#define p_types symtab[SYM_TYPES]
#define p_users symtab[SYM_USERS]
#define p_bools symtab[SYM_BOOLS]
#define p_levels symtab[SYM_LEVELS]
#define p_cats symtab[SYM_CATS]

	/* symbol names indexed by (value - 1) */
	char **sym_val_to_name[SYM_NUM];
#define p_common_val_to_name sym_val_to_name[SYM_COMMONS]
#define p_class_val_to_name sym_val_to_name[SYM_CLASSES]
#define p_role_val_to_name sym_val_to_name[SYM_ROLES]
#define p_type_val_to_name sym_val_to_name[SYM_TYPES]
#define p_user_val_to_name sym_val_to_name[SYM_USERS]
#define p_bool_val_to_name sym_val_to_name[SYM_BOOLS]
#define p_sens_val_to_name sym_val_to_name[SYM_LEVELS]
#define p_cat_val_to_name sym_val_to_name[SYM_CATS]

	/* class, role, and user attributes indexed by (value - 1) */
	struct class_datum **class_val_to_struct;
	struct role_datum **role_val_to_struct;
	struct user_datum **user_val_to_struct;

	/* type enforcement access vectors and transitions */
	struct avtab te_avtab;

	/* role transitions */
	struct role_trans *role_tr;

	/* bools indexed by (value - 1) */
	struct cond_bool_datum **bool_val_to_struct;
	/* type enforcement conditional access vectors and transitions */
	struct avtab te_cond_avtab;
	/* linked list indexing te_cond_avtab by conditional */
	struct cond_node* cond_list;

	/* role allows */
	struct role_allow *role_allow;

	/* security contexts of initial SIDs, unlabeled file systems,
	   TCP or UDP port numbers, network interfaces and nodes */
	struct ocontext *ocontexts[OCON_NUM];

        /* security contexts for files in filesystems that cannot support
	   a persistent label mapping or use another
	   fixed labeling behavior. */
  	struct genfs *genfs;

	/* range transitions */
	struct range_trans *range_tr;

	/* type -> attribute reverse mapping */
	struct ebitmap *type_attr_map;

	unsigned int policyvers;

	unsigned int reject_unknown : 1;
	unsigned int allow_unknown : 1;
	u32 *undefined_perms;
};

extern void policydb_destroy(struct policydb *p);
extern int policydb_load_isids(struct policydb *p, struct sidtab *s);
extern int policydb_context_isvalid(struct policydb *p, struct context *c);
extern int policydb_read(struct policydb *p, void *fp);

#define PERM_SYMTAB_SIZE 32

#define POLICYDB_CONFIG_MLS    1

/* the config flags related to unknown classes/perms are bits 2 and 3 */
#define REJECT_UNKNOWN	0x00000002
#define ALLOW_UNKNOWN	0x00000004

#define OBJECT_R "object_r"
#define OBJECT_R_VAL 1

#define POLICYDB_MAGIC SELINUX_MAGIC
#define POLICYDB_STRING "SE Linux"

struct policy_file {
	char *data;
	size_t len;
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

#endif	/* _SS_POLICYDB_H_ */

