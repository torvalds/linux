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
 *
 * Updated: Yuichi Nakamura <ynakam@hitachisoft.jp>
 * 	Tuned number of hash slots for avtab to reduce memory usage
 */
#ifndef _SS_AVTAB_H_
#define _SS_AVTAB_H_

struct avtab_key {
	u16 source_type;	/* source type */
	u16 target_type;	/* target type */
	u16 target_class;	/* target object class */
#define AVTAB_ALLOWED     1
#define AVTAB_AUDITALLOW  2
#define AVTAB_AUDITDENY   4
#define AVTAB_AV         (AVTAB_ALLOWED | AVTAB_AUDITALLOW | AVTAB_AUDITDENY)
#define AVTAB_TRANSITION 16
#define AVTAB_MEMBER     32
#define AVTAB_CHANGE     64
#define AVTAB_TYPE       (AVTAB_TRANSITION | AVTAB_MEMBER | AVTAB_CHANGE)
#define AVTAB_ENABLED_OLD    0x80000000 /* reserved for used in cond_avtab */
#define AVTAB_ENABLED    0x8000 /* reserved for used in cond_avtab */
	u16 specified;	/* what field is specified */
};

struct avtab_datum {
	u32 data; /* access vector or type value */
};

struct avtab_node {
	struct avtab_key key;
	struct avtab_datum datum;
	struct avtab_node *next;
};

struct avtab {
	struct avtab_node **htable;
	u32 nel;	/* number of elements */
	u32 nslot;      /* number of hash slots */
	u16 mask;       /* mask to compute hash func */

};

int avtab_init(struct avtab *);
int avtab_alloc(struct avtab *, u32);
struct avtab_datum *avtab_search(struct avtab *h, struct avtab_key *k);
void avtab_destroy(struct avtab *h);
void avtab_hash_eval(struct avtab *h, char *tag);

struct policydb;
int avtab_read_item(struct avtab *a, void *fp, struct policydb *pol,
		    int (*insert)(struct avtab *a, struct avtab_key *k,
				  struct avtab_datum *d, void *p),
		    void *p);

int avtab_read(struct avtab *a, void *fp, struct policydb *pol);

struct avtab_node *avtab_insert_nonunique(struct avtab *h, struct avtab_key *key,
					  struct avtab_datum *datum);

struct avtab_node *avtab_search_node(struct avtab *h, struct avtab_key *key);

struct avtab_node *avtab_search_node_next(struct avtab_node *node, int specified);

void avtab_cache_init(void);
void avtab_cache_destroy(void);

#define MAX_AVTAB_HASH_BITS 13
#define MAX_AVTAB_HASH_BUCKETS (1 << MAX_AVTAB_HASH_BITS)
#define MAX_AVTAB_HASH_MASK (MAX_AVTAB_HASH_BUCKETS-1)
#define MAX_AVTAB_SIZE MAX_AVTAB_HASH_BUCKETS

#endif	/* _SS_AVTAB_H_ */

