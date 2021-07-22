/* SPDX-License-Identifier: GPL-2.0-only */
/* Authors: Karl MacMillan <kmacmillan@tresys.com>
 *          Frank Mayer <mayerf@tresys.com>
 *
 * Copyright (C) 2003 - 2004 Tresys Technology, LLC
 */

#ifndef _CONDITIONAL_H_
#define _CONDITIONAL_H_

#include "avtab.h"
#include "symtab.h"
#include "policydb.h"
#include "../include/conditional.h"

#define COND_EXPR_MAXDEPTH 10

/*
 * A conditional expression is a list of operators and operands
 * in reverse polish notation.
 */
struct cond_expr_node {
#define COND_BOOL	1 /* plain bool */
#define COND_NOT	2 /* !bool */
#define COND_OR		3 /* bool || bool */
#define COND_AND	4 /* bool && bool */
#define COND_XOR	5 /* bool ^ bool */
#define COND_EQ		6 /* bool == bool */
#define COND_NEQ	7 /* bool != bool */
#define COND_LAST	COND_NEQ
	u32 expr_type;
	u32 bool;
};

struct cond_expr {
	struct cond_expr_node *nodes;
	u32 len;
};

/*
 * Each cond_node contains a list of rules to be enabled/disabled
 * depending on the current value of the conditional expression. This
 * struct is for that list.
 */
struct cond_av_list {
	struct avtab_node **nodes;
	u32 len;
};

/*
 * A cond node represents a conditional block in a policy. It
 * contains a conditional expression, the current state of the expression,
 * two lists of rules to enable/disable depending on the value of the
 * expression (the true list corresponds to if and the false list corresponds
 * to else)..
 */
struct cond_node {
	int cur_state;
	struct cond_expr expr;
	struct cond_av_list true_list;
	struct cond_av_list false_list;
};

void cond_policydb_init(struct policydb *p);
void cond_policydb_destroy(struct policydb *p);

int cond_init_bool_indexes(struct policydb *p);
int cond_destroy_bool(void *key, void *datum, void *p);

int cond_index_bool(void *key, void *datum, void *datap);

int cond_read_bool(struct policydb *p, struct symtab *s, void *fp);
int cond_read_list(struct policydb *p, void *fp);
int cond_write_bool(void *key, void *datum, void *ptr);
int cond_write_list(struct policydb *p, void *fp);

void cond_compute_av(struct avtab *ctab, struct avtab_key *key,
		struct av_decision *avd, struct extended_perms *xperms);
void cond_compute_xperms(struct avtab *ctab, struct avtab_key *key,
		struct extended_perms_decision *xpermd);
void evaluate_cond_nodes(struct policydb *p);
void cond_policydb_destroy_dup(struct policydb *p);
int cond_policydb_dup(struct policydb *new, struct policydb *orig);

#endif /* _CONDITIONAL_H_ */
