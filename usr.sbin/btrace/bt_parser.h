/*	$OpenBSD: bt_parser.h,v 1.27 2025/01/23 11:17:32 mpi Exp $	*/

/*
 * Copyright (c) 2019-2021 Martin Pieuchot <mpi@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef BT_PARSER_H
#define BT_PARSER_H

#ifndef nitems
#define nitems(_a)	(sizeof((_a)) / sizeof((_a)[0]))
#endif

/*
 * Probes represent entry points where events can be recorded.
 *
 * Those specified in a given bt(5) script are enabled at runtime. They
 * are represented as:
 *
 *	"provider:function:name"
 * or
 *	"provider:time_unit:number"
 *
 * Multiple probes can be associated to the same action.
 */
struct bt_probe {
	SLIST_ENTRY(bt_probe)	 bp_next;	/* next probe for this rule */
	const char		*bp_prov;	/* provider */
	const char		*bp_func;	/* function or time unit */
	const char		*bp_name;
	uint64_t		 bp_nsecs;
#define bp_unit	bp_func
	enum bt_ptype {
		 B_PT_BEGIN = 1,
		 B_PT_END,
		 B_PT_PROBE,
	}			 bp_type;	/* BEGIN, END or 'probe' */
	void			*bp_cookie;	/* ioctl request */
	uint32_t		 bp_pbn;	/* ID assigned by the kernel */
};


/*
 * Event filters correspond to checks performed in-kernel.
 */
struct bt_evtfilter {
	int			bf_op;
	enum bt_filtervar {
		B_FV_NONE = 1,
		B_FV_PID,
		B_FV_TID
	}			 bf_var;
	uint32_t		 bf_val;
};

/*
 * Filters, also known as predicates, describe under which set of
 * conditions a rule is executed.
 *
 * They are performed when a rule is evaluated and events might be
 * discarded at runtime.
 */
struct bt_filter {
	struct bt_stmt		 *bf_condition;	/* per event condition */
};

TAILQ_HEAD(bt_ruleq, bt_rule);

/*
 * A rule is the language representation of which 'action' to attach to
 * which 'probe' under which conditions ('filter').  In other words it
 * represents the following:
 *
 *	probe / filter / { action }
 */
struct bt_rule {
	TAILQ_ENTRY(bt_rule)	 br_next;	/* linkage in global list */
	SLIST_HEAD(, bt_probe)	 br_probes;	/* list of probes */
	struct bt_filter	*br_filter;
	SLIST_HEAD(, bt_stmt)	 br_action;
	SLIST_HEAD(, bt_var)	 br_variables;	/* local variables */
};

/*
 * Global and local variable representation.
 *
 * Variables are untyped and also include maps and histograms.
 */
struct bt_var {
	SLIST_ENTRY(bt_var)	 bv_next;	/* linkage in global list */
	const char		*bv_name;	/* name of the variable */
	struct bt_arg		*bv_value;	/* corresponding value */
	enum bt_vartype	{
		B_VT_STR = 1,
		B_VT_LONG,
		B_VT_TUPLE,
		B_VT_MAP,
		B_VT_HIST,
	}			 bv_type;
};

/*
 * Representation of an argument.
 *
 * A so called "argument" can be any symbol representing a value or
 * a combination of those through an operation.
 */
struct bt_arg {
	SLIST_ENTRY(bt_arg)	 ba_next;
	void			*ba_value;
	struct bt_arg		*ba_key;	/* key for maps/histograms */
	enum bt_argtype {
		B_AT_STR = 1,			/* C-style string */
		B_AT_LONG,			/* Number (integer) */
		B_AT_VAR,			/* global/local variable */
		B_AT_MAP,			/* global map (@map[]) */
		B_AT_HIST,			/* histogram */
		B_AT_TUPLE,			/* tuple (1, 42, "str") */
		B_AT_TMEMBER,			/* tuple member $t.2 */
		B_AT_NIL,			/* empty value */

		B_AT_BI_PID,
		B_AT_BI_TID,
		B_AT_BI_COMM,
		B_AT_BI_CPU,
		B_AT_BI_NSECS,
		B_AT_BI_KSTACK,
		B_AT_BI_USTACK,
		B_AT_BI_ARG0,
		B_AT_BI_ARG1,
		B_AT_BI_ARG2,
		B_AT_BI_ARG3,
		B_AT_BI_ARG4,
		B_AT_BI_ARG5,
		B_AT_BI_ARG6,
		B_AT_BI_ARG7,
		B_AT_BI_ARG8,
		B_AT_BI_ARG9,
		B_AT_BI_ARGS,
		B_AT_BI_RETVAL,
		B_AT_BI_PROBE,

		B_AT_FN_STR,			/* str($1); str($1, 3); */

		B_AT_MF_COUNT,			/* @map[key] = count() */
		B_AT_MF_MAX,			/* @map[key] = max(nsecs) */
		B_AT_MF_MIN,			/* @map[key] = min(pid) */
		B_AT_MF_SUM,			/* @map[key] = sum(@elapsed) */

		B_AT_OP_PLUS,
		B_AT_OP_MINUS,
		B_AT_OP_MULT,
		B_AT_OP_DIVIDE,
		B_AT_OP_MODULO,
		B_AT_OP_BAND,
		B_AT_OP_XOR,
		B_AT_OP_BOR,
		B_AT_OP_EQ,
		B_AT_OP_NE,
		B_AT_OP_LE,
		B_AT_OP_LT,
		B_AT_OP_GE,
		B_AT_OP_GT,
		B_AT_OP_LAND,
		B_AT_OP_LOR,
	}			 ba_type;
};

#define BA_INITIALIZER(v, t)	{ { NULL }, (void *)(v), NULL, (t) }

/*
 * Represents branches of an if-else statement.
 */
struct bt_cond {
	struct bt_stmt		*bc_condbs;
	struct bt_stmt		*bc_elsebs;
};

/*
 * Each action associated with a given probe is made of at least one
 * statement.
 *
 * Statements are interpreted linearly in userland to format data
 * recorded in the form of events.
 */
struct bt_stmt {
	SLIST_ENTRY(bt_stmt)	 bs_next;
	struct bt_var		*bs_var;	/* for STOREs */
	SLIST_HEAD(, bt_arg)	 bs_args;
	enum bt_action {
		B_AC_BUCKETIZE,			/* @h = hist(42) */
		B_AC_CLEAR,			/* clear(@map) */
		B_AC_DELETE,			/* delete(@map[key]) */
		B_AC_EXIT,			/* exit() */
		B_AC_INSERT,			/* @map[key] = 42 */
		B_AC_PRINT,			/* print(@map, 10) */
		B_AC_PRINTF,			/* printf("hello!\n") */
		B_AC_STORE,			/* @a = 3 */
		B_AC_TEST,			/* if (@a) */
		B_AC_TIME,			/* time("%H:%M:%S  ") */
		B_AC_ZERO,			/* zero(@map) */
	}			 bs_act;
};

extern struct bt_ruleq	 g_rules;	/* Successfully parsed rules. */
extern int		 g_nprobes;	/* # of probes to attach */
extern struct bt_arg 	 g_nullba;
extern struct bt_arg	 g_maxba;

int			 btparse(const char *, size_t, const char *, int);

#define ba_new(v, t)	 ba_new0((void *)(v), (t))
struct bt_arg		*ba_new0(void *, enum bt_argtype);

const char		*bv_name(struct bt_var *);

#endif /* BT_PARSER_H */
