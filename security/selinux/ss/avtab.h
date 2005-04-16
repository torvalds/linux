/*
 * An access vector table (avtab) is a hash table
 * of access vectors and transition types indexed
 * by a type pair and a class.  An access vector
 * table is used to represent the type enforcement
 * tables.
 *
 *  Author : Stephen Smalley, <sds@epoch.ncsc.mil>
 */

/* Updated: Frank Mayer <mayerf@tresys.com> and Karl MacMillan <kmacmillan@tresys.com>
 *
 * 	Added conditional policy language extensions
 *
 * Copyright (C) 2003 Tresys Technology, LLC
 *	This program is free software; you can redistribute it and/or modify
 *  	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, version 2.
 */
#ifndef _SS_AVTAB_H_
#define _SS_AVTAB_H_

struct avtab_key {
	u32 source_type;	/* source type */
	u32 target_type;	/* target type */
	u32 target_class;	/* target object class */
};

struct avtab_datum {
#define AVTAB_ALLOWED     1
#define AVTAB_AUDITALLOW  2
#define AVTAB_AUDITDENY   4
#define AVTAB_AV         (AVTAB_ALLOWED | AVTAB_AUDITALLOW | AVTAB_AUDITDENY)
#define AVTAB_TRANSITION 16
#define AVTAB_MEMBER     32
#define AVTAB_CHANGE     64
#define AVTAB_TYPE       (AVTAB_TRANSITION | AVTAB_MEMBER | AVTAB_CHANGE)
#define AVTAB_ENABLED    0x80000000 /* reserved for used in cond_avtab */
	u32 specified;	/* what fields are specified */
	u32 data[3];	/* access vectors or types */
#define avtab_allowed(x) (x)->data[0]
#define avtab_auditdeny(x) (x)->data[1]
#define avtab_auditallow(x) (x)->data[2]
#define avtab_transition(x) (x)->data[0]
#define avtab_change(x) (x)->data[1]
#define avtab_member(x) (x)->data[2]
};

struct avtab_node {
	struct avtab_key key;
	struct avtab_datum datum;
	struct avtab_node *next;
};

struct avtab {
	struct avtab_node **htable;
	u32 nel;	/* number of elements */
};

int avtab_init(struct avtab *);
struct avtab_datum *avtab_search(struct avtab *h, struct avtab_key *k, int specified);
void avtab_destroy(struct avtab *h);
void avtab_hash_eval(struct avtab *h, char *tag);

int avtab_read_item(void *fp, struct avtab_datum *avdatum, struct avtab_key *avkey);
int avtab_read(struct avtab *a, void *fp, u32 config);

struct avtab_node *avtab_insert_nonunique(struct avtab *h, struct avtab_key *key,
					  struct avtab_datum *datum);

struct avtab_node *avtab_search_node(struct avtab *h, struct avtab_key *key, int specified);

struct avtab_node *avtab_search_node_next(struct avtab_node *node, int specified);

void avtab_cache_init(void);
void avtab_cache_destroy(void);

#define AVTAB_HASH_BITS 15
#define AVTAB_HASH_BUCKETS (1 << AVTAB_HASH_BITS)
#define AVTAB_HASH_MASK (AVTAB_HASH_BUCKETS-1)

#define AVTAB_SIZE AVTAB_HASH_BUCKETS

#endif	/* _SS_AVTAB_H_ */

