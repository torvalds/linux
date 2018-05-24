/*
 * trace_events_filter - generic event filtering
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) 2009 Tom Zanussi <tzanussi@gmail.com>
 */

#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/mutex.h>
#include <linux/perf_event.h>
#include <linux/slab.h>

#include "trace.h"
#include "trace_output.h"

#define DEFAULT_SYS_FILTER_MESSAGE					\
	"### global filter ###\n"					\
	"# Use this to set filters for multiple events.\n"		\
	"# Only events with the given fields will be affected.\n"	\
	"# If no events are modified, an error message will be displayed here"

/* Due to token parsing '<=' must be before '<' and '>=' must be before '>' */
#define OPS					\
	C( OP_GLOB,	"~"  ),			\
	C( OP_NE,	"!=" ),			\
	C( OP_EQ,	"==" ),			\
	C( OP_LE,	"<=" ),			\
	C( OP_LT,	"<"  ),			\
	C( OP_GE,	">=" ),			\
	C( OP_GT,	">"  ),			\
	C( OP_BAND,	"&"  ),			\
	C( OP_MAX,	NULL )

#undef C
#define C(a, b)	a

enum filter_op_ids { OPS };

#undef C
#define C(a, b)	b

static const char * ops[] = { OPS };

/*
 * pred functions are OP_LE, OP_LT, OP_GE, OP_GT, and OP_BAND
 * pred_funcs_##type below must match the order of them above.
 */
#define PRED_FUNC_START			OP_LE
#define PRED_FUNC_MAX			(OP_BAND - PRED_FUNC_START)

#define ERRORS								\
	C(NONE,			"No error"),				\
	C(INVALID_OP,		"Invalid operator"),			\
	C(TOO_MANY_OPEN,	"Too many '('"),			\
	C(TOO_MANY_CLOSE,	"Too few '('"),				\
	C(MISSING_QUOTE,	"Missing matching quote"),		\
	C(OPERAND_TOO_LONG,	"Operand too long"),			\
	C(EXPECT_STRING,	"Expecting string field"),		\
	C(EXPECT_DIGIT,		"Expecting numeric field"),		\
	C(ILLEGAL_FIELD_OP,	"Illegal operation for field type"),	\
	C(FIELD_NOT_FOUND,	"Field not found"),			\
	C(ILLEGAL_INTVAL,	"Illegal integer value"),		\
	C(BAD_SUBSYS_FILTER,	"Couldn't find or set field in one of a subsystem's events"), \
	C(TOO_MANY_PREDS,	"Too many terms in predicate expression"), \
	C(INVALID_FILTER,	"Meaningless filter expression"),	\
	C(IP_FIELD_ONLY,	"Only 'ip' field is supported for function trace"), \
	C(INVALID_VALUE,	"Invalid value (did you forget quotes)?"),

#undef C
#define C(a, b)		FILT_ERR_##a

enum { ERRORS };

#undef C
#define C(a, b)		b

static char *err_text[] = { ERRORS };

/* Called after a '!' character but "!=" and "!~" are not "not"s */
static bool is_not(const char *str)
{
	switch (str[1]) {
	case '=':
	case '~':
		return false;
	}
	return true;
}

/**
 * prog_entry - a singe entry in the filter program
 * @target:	     Index to jump to on a branch (actually one minus the index)
 * @when_to_branch:  The value of the result of the predicate to do a branch
 * @pred:	     The predicate to execute.
 */
struct prog_entry {
	int			target;
	int			when_to_branch;
	struct filter_pred	*pred;
};

/**
 * update_preds- assign a program entry a label target
 * @prog: The program array
 * @N: The index of the current entry in @prog
 * @when_to_branch: What to assign a program entry for its branch condition
 *
 * The program entry at @N has a target that points to the index of a program
 * entry that can have its target and when_to_branch fields updated.
 * Update the current program entry denoted by index @N target field to be
 * that of the updated entry. This will denote the entry to update if
 * we are processing an "||" after an "&&"
 */
static void update_preds(struct prog_entry *prog, int N, int invert)
{
	int t, s;

	t = prog[N].target;
	s = prog[t].target;
	prog[t].when_to_branch = invert;
	prog[t].target = N;
	prog[N].target = s;
}

struct filter_parse_error {
	int lasterr;
	int lasterr_pos;
};

static void parse_error(struct filter_parse_error *pe, int err, int pos)
{
	pe->lasterr = err;
	pe->lasterr_pos = pos;
}

typedef int (*parse_pred_fn)(const char *str, void *data, int pos,
			     struct filter_parse_error *pe,
			     struct filter_pred **pred);

enum {
	INVERT		= 1,
	PROCESS_AND	= 2,
	PROCESS_OR	= 4,
};

/*
 * Without going into a formal proof, this explains the method that is used in
 * parsing the logical expressions.
 *
 * For example, if we have: "a && !(!b || (c && g)) || d || e && !f"
 * The first pass will convert it into the following program:
 *
 * n1: r=a;       l1: if (!r) goto l4;
 * n2: r=b;       l2: if (!r) goto l4;
 * n3: r=c; r=!r; l3: if (r) goto l4;
 * n4: r=g; r=!r; l4: if (r) goto l5;
 * n5: r=d;       l5: if (r) goto T
 * n6: r=e;       l6: if (!r) goto l7;
 * n7: r=f; r=!r; l7: if (!r) goto F
 * T: return TRUE
 * F: return FALSE
 *
 * To do this, we use a data structure to represent each of the above
 * predicate and conditions that has:
 *
 *  predicate, when_to_branch, invert, target
 *
 * The "predicate" will hold the function to determine the result "r".
 * The "when_to_branch" denotes what "r" should be if a branch is to be taken
 * "&&" would contain "!r" or (0) and "||" would contain "r" or (1).
 * The "invert" holds whether the value should be reversed before testing.
 * The "target" contains the label "l#" to jump to.
 *
 * A stack is created to hold values when parentheses are used.
 *
 * To simplify the logic, the labels will start at 0 and not 1.
 *
 * The possible invert values are 1 and 0. The number of "!"s that are in scope
 * before the predicate determines the invert value, if the number is odd then
 * the invert value is 1 and 0 otherwise. This means the invert value only
 * needs to be toggled when a new "!" is introduced compared to what is stored
 * on the stack, where parentheses were used.
 *
 * The top of the stack and "invert" are initialized to zero.
 *
 * ** FIRST PASS **
 *
 * #1 A loop through all the tokens is done:
 *
 * #2 If the token is an "(", the stack is push, and the current stack value
 *    gets the current invert value, and the loop continues to the next token.
 *    The top of the stack saves the "invert" value to keep track of what
 *    the current inversion is. As "!(a && !b || c)" would require all
 *    predicates being affected separately by the "!" before the parentheses.
 *    And that would end up being equivalent to "(!a || b) && !c"
 *
 * #3 If the token is an "!", the current "invert" value gets inverted, and
 *    the loop continues. Note, if the next token is a predicate, then
 *    this "invert" value is only valid for the current program entry,
 *    and does not affect other predicates later on.
 *
 * The only other acceptable token is the predicate string.
 *
 * #4 A new entry into the program is added saving: the predicate and the
 *    current value of "invert". The target is currently assigned to the
 *    previous program index (this will not be its final value).
 *
 * #5 We now enter another loop and look at the next token. The only valid
 *    tokens are ")", "&&", "||" or end of the input string "\0".
 *
 * #6 The invert variable is reset to the current value saved on the top of
 *    the stack.
 *
 * #7 The top of the stack holds not only the current invert value, but also
 *    if a "&&" or "||" needs to be processed. Note, the "&&" takes higher
 *    precedence than "||". That is "a && b || c && d" is equivalent to
 *    "(a && b) || (c && d)". Thus the first thing to do is to see if "&&" needs
 *    to be processed. This is the case if an "&&" was the last token. If it was
 *    then we call update_preds(). This takes the program, the current index in
 *    the program, and the current value of "invert".  More will be described
 *    below about this function.
 *
 * #8 If the next token is "&&" then we set a flag in the top of the stack
 *    that denotes that "&&" needs to be processed, break out of this loop
 *    and continue with the outer loop.
 *
 * #9 Otherwise, if a "||" needs to be processed then update_preds() is called.
 *    This is called with the program, the current index in the program, but
 *    this time with an inverted value of "invert" (that is !invert). This is
 *    because the value taken will become the "when_to_branch" value of the
 *    program.
 *    Note, this is called when the next token is not an "&&". As stated before,
 *    "&&" takes higher precedence, and "||" should not be processed yet if the
 *    next logical operation is "&&".
 *
 * #10 If the next token is "||" then we set a flag in the top of the stack
 *     that denotes that "||" needs to be processed, break out of this loop
 *     and continue with the outer loop.
 *
 * #11 If this is the end of the input string "\0" then we break out of both
 *     loops.
 *
 * #12 Otherwise, the next token is ")", where we pop the stack and continue
 *     this inner loop.
 *
 * Now to discuss the update_pred() function, as that is key to the setting up
 * of the program. Remember the "target" of the program is initialized to the
 * previous index and not the "l" label. The target holds the index into the
 * program that gets affected by the operand. Thus if we have something like
 *  "a || b && c", when we process "a" the target will be "-1" (undefined).
 * When we process "b", its target is "0", which is the index of "a", as that's
 * the predicate that is affected by "||". But because the next token after "b"
 * is "&&" we don't call update_preds(). Instead continue to "c". As the
 * next token after "c" is not "&&" but the end of input, we first process the
 * "&&" by calling update_preds() for the "&&" then we process the "||" by
 * callin updates_preds() with the values for processing "||".
 *
 * What does that mean? What update_preds() does is to first save the "target"
 * of the program entry indexed by the current program entry's "target"
 * (remember the "target" is initialized to previous program entry), and then
 * sets that "target" to the current index which represents the label "l#".
 * That entry's "when_to_branch" is set to the value passed in (the "invert"
 * or "!invert"). Then it sets the current program entry's target to the saved
 * "target" value (the old value of the program that had its "target" updated
 * to the label).
 *
 * Looking back at "a || b && c", we have the following steps:
 *  "a"  - prog[0] = { "a", X, -1 } // pred, when_to_branch, target
 *  "||" - flag that we need to process "||"; continue outer loop
 *  "b"  - prog[1] = { "b", X, 0 }
 *  "&&" - flag that we need to process "&&"; continue outer loop
 * (Notice we did not process "||")
 *  "c"  - prog[2] = { "c", X, 1 }
 *  update_preds(prog, 2, 0); // invert = 0 as we are processing "&&"
 *    t = prog[2].target; // t = 1
 *    s = prog[t].target; // s = 0
 *    prog[t].target = 2; // Set target to "l2"
 *    prog[t].when_to_branch = 0;
 *    prog[2].target = s;
 * update_preds(prog, 2, 1); // invert = 1 as we are now processing "||"
 *    t = prog[2].target; // t = 0
 *    s = prog[t].target; // s = -1
 *    prog[t].target = 2; // Set target to "l2"
 *    prog[t].when_to_branch = 1;
 *    prog[2].target = s;
 *
 * #13 Which brings us to the final step of the first pass, which is to set
 *     the last program entry's when_to_branch and target, which will be
 *     when_to_branch = 0; target = N; ( the label after the program entry after
 *     the last program entry processed above).
 *
 * If we denote "TRUE" to be the entry after the last program entry processed,
 * and "FALSE" the program entry after that, we are now done with the first
 * pass.
 *
 * Making the above "a || b && c" have a progam of:
 *  prog[0] = { "a", 1, 2 }
 *  prog[1] = { "b", 0, 2 }
 *  prog[2] = { "c", 0, 3 }
 *
 * Which translates into:
 * n0: r = a; l0: if (r) goto l2;
 * n1: r = b; l1: if (!r) goto l2;
 * n2: r = c; l2: if (!r) goto l3;  // Which is the same as "goto F;"
 * T: return TRUE; l3:
 * F: return FALSE
 *
 * Although, after the first pass, the program is correct, it is
 * inefficient. The simple sample of "a || b && c" could be easily been
 * converted into:
 * n0: r = a; if (r) goto T
 * n1: r = b; if (!r) goto F
 * n2: r = c; if (!r) goto F
 * T: return TRUE;
 * F: return FALSE;
 *
 * The First Pass is over the input string. The next too passes are over
 * the program itself.
 *
 * ** SECOND PASS **
 *
 * Which brings us to the second pass. If a jump to a label has the
 * same condition as that label, it can instead jump to its target.
 * The original example of "a && !(!b || (c && g)) || d || e && !f"
 * where the first pass gives us:
 *
 * n1: r=a;       l1: if (!r) goto l4;
 * n2: r=b;       l2: if (!r) goto l4;
 * n3: r=c; r=!r; l3: if (r) goto l4;
 * n4: r=g; r=!r; l4: if (r) goto l5;
 * n5: r=d;       l5: if (r) goto T
 * n6: r=e;       l6: if (!r) goto l7;
 * n7: r=f; r=!r; l7: if (!r) goto F:
 * T: return TRUE;
 * F: return FALSE
 *
 * We can see that "l3: if (r) goto l4;" and at l4, we have "if (r) goto l5;".
 * And "l5: if (r) goto T", we could optimize this by converting l3 and l4
 * to go directly to T. To accomplish this, we start from the last
 * entry in the program and work our way back. If the target of the entry
 * has the same "when_to_branch" then we could use that entry's target.
 * Doing this, the above would end up as:
 *
 * n1: r=a;       l1: if (!r) goto l4;
 * n2: r=b;       l2: if (!r) goto l4;
 * n3: r=c; r=!r; l3: if (r) goto T;
 * n4: r=g; r=!r; l4: if (r) goto T;
 * n5: r=d;       l5: if (r) goto T;
 * n6: r=e;       l6: if (!r) goto F;
 * n7: r=f; r=!r; l7: if (!r) goto F;
 * T: return TRUE
 * F: return FALSE
 *
 * In that same pass, if the "when_to_branch" doesn't match, we can simply
 * go to the program entry after the label. That is, "l2: if (!r) goto l4;"
 * where "l4: if (r) goto T;", then we can convert l2 to be:
 * "l2: if (!r) goto n5;".
 *
 * This will have the second pass give us:
 * n1: r=a;       l1: if (!r) goto n5;
 * n2: r=b;       l2: if (!r) goto n5;
 * n3: r=c; r=!r; l3: if (r) goto T;
 * n4: r=g; r=!r; l4: if (r) goto T;
 * n5: r=d;       l5: if (r) goto T
 * n6: r=e;       l6: if (!r) goto F;
 * n7: r=f; r=!r; l7: if (!r) goto F
 * T: return TRUE
 * F: return FALSE
 *
 * Notice, all the "l#" labels are no longer used, and they can now
 * be discarded.
 *
 * ** THIRD PASS **
 *
 * For the third pass we deal with the inverts. As they simply just
 * make the "when_to_branch" get inverted, a simple loop over the
 * program to that does: "when_to_branch ^= invert;" will do the
 * job, leaving us with:
 * n1: r=a; if (!r) goto n5;
 * n2: r=b; if (!r) goto n5;
 * n3: r=c: if (!r) goto T;
 * n4: r=g; if (!r) goto T;
 * n5: r=d; if (r) goto T
 * n6: r=e; if (!r) goto F;
 * n7: r=f; if (r) goto F
 * T: return TRUE
 * F: return FALSE
 *
 * As "r = a; if (!r) goto n5;" is obviously the same as
 * "if (!a) goto n5;" without doing anything we can interperate the
 * program as:
 * n1: if (!a) goto n5;
 * n2: if (!b) goto n5;
 * n3: if (!c) goto T;
 * n4: if (!g) goto T;
 * n5: if (d) goto T
 * n6: if (!e) goto F;
 * n7: if (f) goto F
 * T: return TRUE
 * F: return FALSE
 *
 * Since the inverts are discarded at the end, there's no reason to store
 * them in the program array (and waste memory). A separate array to hold
 * the inverts is used and freed at the end.
 */
static struct prog_entry *
predicate_parse(const char *str, int nr_parens, int nr_preds,
		parse_pred_fn parse_pred, void *data,
		struct filter_parse_error *pe)
{
	struct prog_entry *prog_stack;
	struct prog_entry *prog;
	const char *ptr = str;
	char *inverts = NULL;
	int *op_stack;
	int *top;
	int invert = 0;
	int ret = -ENOMEM;
	int len;
	int N = 0;
	int i;

	nr_preds += 2; /* For TRUE and FALSE */

	op_stack = kmalloc(sizeof(*op_stack) * nr_parens, GFP_KERNEL);
	if (!op_stack)
		return ERR_PTR(-ENOMEM);
	prog_stack = kmalloc(sizeof(*prog_stack) * nr_preds, GFP_KERNEL);
	if (!prog_stack) {
		parse_error(pe, -ENOMEM, 0);
		goto out_free;
	}
	inverts = kmalloc(sizeof(*inverts) * nr_preds, GFP_KERNEL);
	if (!inverts) {
		parse_error(pe, -ENOMEM, 0);
		goto out_free;
	}

	top = op_stack;
	prog = prog_stack;
	*top = 0;

	/* First pass */
	while (*ptr) {						/* #1 */
		const char *next = ptr++;

		if (isspace(*next))
			continue;

		switch (*next) {
		case '(':					/* #2 */
			if (top - op_stack > nr_parens)
				return ERR_PTR(-EINVAL);
			*(++top) = invert;
			continue;
		case '!':					/* #3 */
			if (!is_not(next))
				break;
			invert = !invert;
			continue;
		}

		if (N >= nr_preds) {
			parse_error(pe, FILT_ERR_TOO_MANY_PREDS, next - str);
			goto out_free;
		}

		inverts[N] = invert;				/* #4 */
		prog[N].target = N-1;

		len = parse_pred(next, data, ptr - str, pe, &prog[N].pred);
		if (len < 0) {
			ret = len;
			goto out_free;
		}
		ptr = next + len;

		N++;

		ret = -1;
		while (1) {					/* #5 */
			next = ptr++;
			if (isspace(*next))
				continue;

			switch (*next) {
			case ')':
			case '\0':
				break;
			case '&':
			case '|':
				if (next[1] == next[0]) {
					ptr++;
					break;
				}
			default:
				parse_error(pe, FILT_ERR_TOO_MANY_PREDS,
					    next - str);
				goto out_free;
			}

			invert = *top & INVERT;

			if (*top & PROCESS_AND) {		/* #7 */
				update_preds(prog, N - 1, invert);
				*top &= ~PROCESS_AND;
			}
			if (*next == '&') {			/* #8 */
				*top |= PROCESS_AND;
				break;
			}
			if (*top & PROCESS_OR) {		/* #9 */
				update_preds(prog, N - 1, !invert);
				*top &= ~PROCESS_OR;
			}
			if (*next == '|') {			/* #10 */
				*top |= PROCESS_OR;
				break;
			}
			if (!*next)				/* #11 */
				goto out;

			if (top == op_stack) {
				ret = -1;
				/* Too few '(' */
				parse_error(pe, FILT_ERR_TOO_MANY_CLOSE, ptr - str);
				goto out_free;
			}
			top--;					/* #12 */
		}
	}
 out:
	if (top != op_stack) {
		/* Too many '(' */
		parse_error(pe, FILT_ERR_TOO_MANY_OPEN, ptr - str);
		goto out_free;
	}

	prog[N].pred = NULL;					/* #13 */
	prog[N].target = 1;		/* TRUE */
	prog[N+1].pred = NULL;
	prog[N+1].target = 0;		/* FALSE */
	prog[N-1].target = N;
	prog[N-1].when_to_branch = false;

	/* Second Pass */
	for (i = N-1 ; i--; ) {
		int target = prog[i].target;
		if (prog[i].when_to_branch == prog[target].when_to_branch)
			prog[i].target = prog[target].target;
	}

	/* Third Pass */
	for (i = 0; i < N; i++) {
		invert = inverts[i] ^ prog[i].when_to_branch;
		prog[i].when_to_branch = invert;
		/* Make sure the program always moves forward */
		if (WARN_ON(prog[i].target <= i)) {
			ret = -EINVAL;
			goto out_free;
		}
	}

	return prog;
out_free:
	kfree(op_stack);
	kfree(prog_stack);
	kfree(inverts);
	return ERR_PTR(ret);
}

#define DEFINE_COMPARISON_PRED(type)					\
static int filter_pred_LT_##type(struct filter_pred *pred, void *event)	\
{									\
	type *addr = (type *)(event + pred->offset);			\
	type val = (type)pred->val;					\
	return *addr < val;						\
}									\
static int filter_pred_LE_##type(struct filter_pred *pred, void *event)	\
{									\
	type *addr = (type *)(event + pred->offset);			\
	type val = (type)pred->val;					\
	return *addr <= val;						\
}									\
static int filter_pred_GT_##type(struct filter_pred *pred, void *event)	\
{									\
	type *addr = (type *)(event + pred->offset);			\
	type val = (type)pred->val;					\
	return *addr > val;					\
}									\
static int filter_pred_GE_##type(struct filter_pred *pred, void *event)	\
{									\
	type *addr = (type *)(event + pred->offset);			\
	type val = (type)pred->val;					\
	return *addr >= val;						\
}									\
static int filter_pred_BAND_##type(struct filter_pred *pred, void *event) \
{									\
	type *addr = (type *)(event + pred->offset);			\
	type val = (type)pred->val;					\
	return !!(*addr & val);						\
}									\
static const filter_pred_fn_t pred_funcs_##type[] = {			\
	filter_pred_LE_##type,						\
	filter_pred_LT_##type,						\
	filter_pred_GE_##type,						\
	filter_pred_GT_##type,						\
	filter_pred_BAND_##type,					\
};

#define DEFINE_EQUALITY_PRED(size)					\
static int filter_pred_##size(struct filter_pred *pred, void *event)	\
{									\
	u##size *addr = (u##size *)(event + pred->offset);		\
	u##size val = (u##size)pred->val;				\
	int match;							\
									\
	match = (val == *addr) ^ pred->not;				\
									\
	return match;							\
}

DEFINE_COMPARISON_PRED(s64);
DEFINE_COMPARISON_PRED(u64);
DEFINE_COMPARISON_PRED(s32);
DEFINE_COMPARISON_PRED(u32);
DEFINE_COMPARISON_PRED(s16);
DEFINE_COMPARISON_PRED(u16);
DEFINE_COMPARISON_PRED(s8);
DEFINE_COMPARISON_PRED(u8);

DEFINE_EQUALITY_PRED(64);
DEFINE_EQUALITY_PRED(32);
DEFINE_EQUALITY_PRED(16);
DEFINE_EQUALITY_PRED(8);

/* Filter predicate for fixed sized arrays of characters */
static int filter_pred_string(struct filter_pred *pred, void *event)
{
	char *addr = (char *)(event + pred->offset);
	int cmp, match;

	cmp = pred->regex.match(addr, &pred->regex, pred->regex.field_len);

	match = cmp ^ pred->not;

	return match;
}

/* Filter predicate for char * pointers */
static int filter_pred_pchar(struct filter_pred *pred, void *event)
{
	char **addr = (char **)(event + pred->offset);
	int cmp, match;
	int len = strlen(*addr) + 1;	/* including tailing '\0' */

	cmp = pred->regex.match(*addr, &pred->regex, len);

	match = cmp ^ pred->not;

	return match;
}

/*
 * Filter predicate for dynamic sized arrays of characters.
 * These are implemented through a list of strings at the end
 * of the entry.
 * Also each of these strings have a field in the entry which
 * contains its offset from the beginning of the entry.
 * We have then first to get this field, dereference it
 * and add it to the address of the entry, and at last we have
 * the address of the string.
 */
static int filter_pred_strloc(struct filter_pred *pred, void *event)
{
	u32 str_item = *(u32 *)(event + pred->offset);
	int str_loc = str_item & 0xffff;
	int str_len = str_item >> 16;
	char *addr = (char *)(event + str_loc);
	int cmp, match;

	cmp = pred->regex.match(addr, &pred->regex, str_len);

	match = cmp ^ pred->not;

	return match;
}

/* Filter predicate for CPUs. */
static int filter_pred_cpu(struct filter_pred *pred, void *event)
{
	int cpu, cmp;

	cpu = raw_smp_processor_id();
	cmp = pred->val;

	switch (pred->op) {
	case OP_EQ:
		return cpu == cmp;
	case OP_NE:
		return cpu != cmp;
	case OP_LT:
		return cpu < cmp;
	case OP_LE:
		return cpu <= cmp;
	case OP_GT:
		return cpu > cmp;
	case OP_GE:
		return cpu >= cmp;
	default:
		return 0;
	}
}

/* Filter predicate for COMM. */
static int filter_pred_comm(struct filter_pred *pred, void *event)
{
	int cmp;

	cmp = pred->regex.match(current->comm, &pred->regex,
				TASK_COMM_LEN);
	return cmp ^ pred->not;
}

static int filter_pred_none(struct filter_pred *pred, void *event)
{
	return 0;
}

/*
 * regex_match_foo - Basic regex callbacks
 *
 * @str: the string to be searched
 * @r:   the regex structure containing the pattern string
 * @len: the length of the string to be searched (including '\0')
 *
 * Note:
 * - @str might not be NULL-terminated if it's of type DYN_STRING
 *   or STATIC_STRING
 */

static int regex_match_full(char *str, struct regex *r, int len)
{
	if (strncmp(str, r->pattern, len) == 0)
		return 1;
	return 0;
}

static int regex_match_front(char *str, struct regex *r, int len)
{
	if (len < r->len)
		return 0;

	if (strncmp(str, r->pattern, r->len) == 0)
		return 1;
	return 0;
}

static int regex_match_middle(char *str, struct regex *r, int len)
{
	if (strnstr(str, r->pattern, len))
		return 1;
	return 0;
}

static int regex_match_end(char *str, struct regex *r, int len)
{
	int strlen = len - 1;

	if (strlen >= r->len &&
	    memcmp(str + strlen - r->len, r->pattern, r->len) == 0)
		return 1;
	return 0;
}

static int regex_match_glob(char *str, struct regex *r, int len __maybe_unused)
{
	if (glob_match(r->pattern, str))
		return 1;
	return 0;
}

/**
 * filter_parse_regex - parse a basic regex
 * @buff:   the raw regex
 * @len:    length of the regex
 * @search: will point to the beginning of the string to compare
 * @not:    tell whether the match will have to be inverted
 *
 * This passes in a buffer containing a regex and this function will
 * set search to point to the search part of the buffer and
 * return the type of search it is (see enum above).
 * This does modify buff.
 *
 * Returns enum type.
 *  search returns the pointer to use for comparison.
 *  not returns 1 if buff started with a '!'
 *     0 otherwise.
 */
enum regex_type filter_parse_regex(char *buff, int len, char **search, int *not)
{
	int type = MATCH_FULL;
	int i;

	if (buff[0] == '!') {
		*not = 1;
		buff++;
		len--;
	} else
		*not = 0;

	*search = buff;

	for (i = 0; i < len; i++) {
		if (buff[i] == '*') {
			if (!i) {
				type = MATCH_END_ONLY;
			} else if (i == len - 1) {
				if (type == MATCH_END_ONLY)
					type = MATCH_MIDDLE_ONLY;
				else
					type = MATCH_FRONT_ONLY;
				buff[i] = 0;
				break;
			} else {	/* pattern continues, use full glob */
				return MATCH_GLOB;
			}
		} else if (strchr("[?\\", buff[i])) {
			return MATCH_GLOB;
		}
	}
	if (buff[0] == '*')
		*search = buff + 1;

	return type;
}

static void filter_build_regex(struct filter_pred *pred)
{
	struct regex *r = &pred->regex;
	char *search;
	enum regex_type type = MATCH_FULL;

	if (pred->op == OP_GLOB) {
		type = filter_parse_regex(r->pattern, r->len, &search, &pred->not);
		r->len = strlen(search);
		memmove(r->pattern, search, r->len+1);
	}

	switch (type) {
	case MATCH_FULL:
		r->match = regex_match_full;
		break;
	case MATCH_FRONT_ONLY:
		r->match = regex_match_front;
		break;
	case MATCH_MIDDLE_ONLY:
		r->match = regex_match_middle;
		break;
	case MATCH_END_ONLY:
		r->match = regex_match_end;
		break;
	case MATCH_GLOB:
		r->match = regex_match_glob;
		break;
	}
}

/* return 1 if event matches, 0 otherwise (discard) */
int filter_match_preds(struct event_filter *filter, void *rec)
{
	struct prog_entry *prog;
	int i;

	/* no filter is considered a match */
	if (!filter)
		return 1;

	prog = rcu_dereference_sched(filter->prog);
	if (!prog)
		return 1;

	for (i = 0; prog[i].pred; i++) {
		struct filter_pred *pred = prog[i].pred;
		int match = pred->fn(pred, rec);
		if (match == prog[i].when_to_branch)
			i = prog[i].target;
	}
	return prog[i].target;
}
EXPORT_SYMBOL_GPL(filter_match_preds);

static void remove_filter_string(struct event_filter *filter)
{
	if (!filter)
		return;

	kfree(filter->filter_string);
	filter->filter_string = NULL;
}

static void append_filter_err(struct filter_parse_error *pe,
			      struct event_filter *filter)
{
	struct trace_seq *s;
	int pos = pe->lasterr_pos;
	char *buf;
	int len;

	if (WARN_ON(!filter->filter_string))
		return;

	s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return;
	trace_seq_init(s);

	len = strlen(filter->filter_string);
	if (pos > len)
		pos = len;

	/* indexing is off by one */
	if (pos)
		pos++;

	trace_seq_puts(s, filter->filter_string);
	if (pe->lasterr > 0) {
		trace_seq_printf(s, "\n%*s", pos, "^");
		trace_seq_printf(s, "\nparse_error: %s\n", err_text[pe->lasterr]);
	} else {
		trace_seq_printf(s, "\nError: (%d)\n", pe->lasterr);
	}
	trace_seq_putc(s, 0);
	buf = kmemdup_nul(s->buffer, s->seq.len, GFP_KERNEL);
	if (buf) {
		kfree(filter->filter_string);
		filter->filter_string = buf;
	}
	kfree(s);
}

static inline struct event_filter *event_filter(struct trace_event_file *file)
{
	return file->filter;
}

/* caller must hold event_mutex */
void print_event_filter(struct trace_event_file *file, struct trace_seq *s)
{
	struct event_filter *filter = event_filter(file);

	if (filter && filter->filter_string)
		trace_seq_printf(s, "%s\n", filter->filter_string);
	else
		trace_seq_puts(s, "none\n");
}

void print_subsystem_event_filter(struct event_subsystem *system,
				  struct trace_seq *s)
{
	struct event_filter *filter;

	mutex_lock(&event_mutex);
	filter = system->filter;
	if (filter && filter->filter_string)
		trace_seq_printf(s, "%s\n", filter->filter_string);
	else
		trace_seq_puts(s, DEFAULT_SYS_FILTER_MESSAGE "\n");
	mutex_unlock(&event_mutex);
}

static void free_prog(struct event_filter *filter)
{
	struct prog_entry *prog;
	int i;

	prog = rcu_access_pointer(filter->prog);
	if (!prog)
		return;

	for (i = 0; prog[i].pred; i++)
		kfree(prog[i].pred);
	kfree(prog);
}

static void filter_disable(struct trace_event_file *file)
{
	unsigned long old_flags = file->flags;

	file->flags &= ~EVENT_FILE_FL_FILTERED;

	if (old_flags != file->flags)
		trace_buffered_event_disable();
}

static void __free_filter(struct event_filter *filter)
{
	if (!filter)
		return;

	free_prog(filter);
	kfree(filter->filter_string);
	kfree(filter);
}

void free_event_filter(struct event_filter *filter)
{
	__free_filter(filter);
}

static inline void __remove_filter(struct trace_event_file *file)
{
	filter_disable(file);
	remove_filter_string(file->filter);
}

static void filter_free_subsystem_preds(struct trace_subsystem_dir *dir,
					struct trace_array *tr)
{
	struct trace_event_file *file;

	list_for_each_entry(file, &tr->events, list) {
		if (file->system != dir)
			continue;
		__remove_filter(file);
	}
}

static inline void __free_subsystem_filter(struct trace_event_file *file)
{
	__free_filter(file->filter);
	file->filter = NULL;
}

static void filter_free_subsystem_filters(struct trace_subsystem_dir *dir,
					  struct trace_array *tr)
{
	struct trace_event_file *file;

	list_for_each_entry(file, &tr->events, list) {
		if (file->system != dir)
			continue;
		__free_subsystem_filter(file);
	}
}

int filter_assign_type(const char *type)
{
	if (strstr(type, "__data_loc") && strstr(type, "char"))
		return FILTER_DYN_STRING;

	if (strchr(type, '[') && strstr(type, "char"))
		return FILTER_STATIC_STRING;

	return FILTER_OTHER;
}

static filter_pred_fn_t select_comparison_fn(enum filter_op_ids op,
					    int field_size, int field_is_signed)
{
	filter_pred_fn_t fn = NULL;
	int pred_func_index = -1;

	switch (op) {
	case OP_EQ:
	case OP_NE:
		break;
	default:
		if (WARN_ON_ONCE(op < PRED_FUNC_START))
			return NULL;
		pred_func_index = op - PRED_FUNC_START;
		if (WARN_ON_ONCE(pred_func_index > PRED_FUNC_MAX))
			return NULL;
	}

	switch (field_size) {
	case 8:
		if (pred_func_index < 0)
			fn = filter_pred_64;
		else if (field_is_signed)
			fn = pred_funcs_s64[pred_func_index];
		else
			fn = pred_funcs_u64[pred_func_index];
		break;
	case 4:
		if (pred_func_index < 0)
			fn = filter_pred_32;
		else if (field_is_signed)
			fn = pred_funcs_s32[pred_func_index];
		else
			fn = pred_funcs_u32[pred_func_index];
		break;
	case 2:
		if (pred_func_index < 0)
			fn = filter_pred_16;
		else if (field_is_signed)
			fn = pred_funcs_s16[pred_func_index];
		else
			fn = pred_funcs_u16[pred_func_index];
		break;
	case 1:
		if (pred_func_index < 0)
			fn = filter_pred_8;
		else if (field_is_signed)
			fn = pred_funcs_s8[pred_func_index];
		else
			fn = pred_funcs_u8[pred_func_index];
		break;
	}

	return fn;
}

/* Called when a predicate is encountered by predicate_parse() */
static int parse_pred(const char *str, void *data,
		      int pos, struct filter_parse_error *pe,
		      struct filter_pred **pred_ptr)
{
	struct trace_event_call *call = data;
	struct ftrace_event_field *field;
	struct filter_pred *pred = NULL;
	char num_buf[24];	/* Big enough to hold an address */
	char *field_name;
	char q;
	u64 val;
	int len;
	int ret;
	int op;
	int s;
	int i = 0;

	/* First find the field to associate to */
	while (isspace(str[i]))
		i++;
	s = i;

	while (isalnum(str[i]) || str[i] == '_')
		i++;

	len = i - s;

	if (!len)
		return -1;

	field_name = kmemdup_nul(str + s, len, GFP_KERNEL);
	if (!field_name)
		return -ENOMEM;

	/* Make sure that the field exists */

	field = trace_find_event_field(call, field_name);
	kfree(field_name);
	if (!field) {
		parse_error(pe, FILT_ERR_FIELD_NOT_FOUND, pos + i);
		return -EINVAL;
	}

	while (isspace(str[i]))
		i++;

	/* Make sure this op is supported */
	for (op = 0; ops[op]; op++) {
		/* This is why '<=' must come before '<' in ops[] */
		if (strncmp(str + i, ops[op], strlen(ops[op])) == 0)
			break;
	}

	if (!ops[op]) {
		parse_error(pe, FILT_ERR_INVALID_OP, pos + i);
		goto err_free;
	}

	i += strlen(ops[op]);

	while (isspace(str[i]))
		i++;

	s = i;

	pred = kzalloc(sizeof(*pred), GFP_KERNEL);
	if (!pred)
		return -ENOMEM;

	pred->field = field;
	pred->offset = field->offset;
	pred->op = op;

	if (ftrace_event_is_function(call)) {
		/*
		 * Perf does things different with function events.
		 * It only allows an "ip" field, and expects a string.
		 * But the string does not need to be surrounded by quotes.
		 * If it is a string, the assigned function as a nop,
		 * (perf doesn't use it) and grab everything.
		 */
		if (strcmp(field->name, "ip") != 0) {
			 parse_error(pe, FILT_ERR_IP_FIELD_ONLY, pos + i);
			 goto err_free;
		 }
		 pred->fn = filter_pred_none;

		 /*
		  * Quotes are not required, but if they exist then we need
		  * to read them till we hit a matching one.
		  */
		 if (str[i] == '\'' || str[i] == '"')
			 q = str[i];
		 else
			 q = 0;

		 for (i++; str[i]; i++) {
			 if (q && str[i] == q)
				 break;
			 if (!q && (str[i] == ')' || str[i] == '&' ||
				    str[i] == '|'))
				 break;
		 }
		 /* Skip quotes */
		 if (q)
			 s++;
		len = i - s;
		if (len >= MAX_FILTER_STR_VAL) {
			parse_error(pe, FILT_ERR_OPERAND_TOO_LONG, pos + i);
			goto err_free;
		}

		pred->regex.len = len;
		strncpy(pred->regex.pattern, str + s, len);
		pred->regex.pattern[len] = 0;

	/* This is either a string, or an integer */
	} else if (str[i] == '\'' || str[i] == '"') {
		char q = str[i];

		/* Make sure the op is OK for strings */
		switch (op) {
		case OP_NE:
			pred->not = 1;
			/* Fall through */
		case OP_GLOB:
		case OP_EQ:
			break;
		default:
			parse_error(pe, FILT_ERR_ILLEGAL_FIELD_OP, pos + i);
			goto err_free;
		}

		/* Make sure the field is OK for strings */
		if (!is_string_field(field)) {
			parse_error(pe, FILT_ERR_EXPECT_DIGIT, pos + i);
			goto err_free;
		}

		for (i++; str[i]; i++) {
			if (str[i] == q)
				break;
		}
		if (!str[i]) {
			parse_error(pe, FILT_ERR_MISSING_QUOTE, pos + i);
			goto err_free;
		}

		/* Skip quotes */
		s++;
		len = i - s;
		if (len >= MAX_FILTER_STR_VAL) {
			parse_error(pe, FILT_ERR_OPERAND_TOO_LONG, pos + i);
			goto err_free;
		}

		pred->regex.len = len;
		strncpy(pred->regex.pattern, str + s, len);
		pred->regex.pattern[len] = 0;

		filter_build_regex(pred);

		if (field->filter_type == FILTER_COMM) {
			pred->fn = filter_pred_comm;

		} else if (field->filter_type == FILTER_STATIC_STRING) {
			pred->fn = filter_pred_string;
			pred->regex.field_len = field->size;

		} else if (field->filter_type == FILTER_DYN_STRING)
			pred->fn = filter_pred_strloc;
		else
			pred->fn = filter_pred_pchar;
		/* go past the last quote */
		i++;

	} else if (isdigit(str[i])) {

		/* Make sure the field is not a string */
		if (is_string_field(field)) {
			parse_error(pe, FILT_ERR_EXPECT_STRING, pos + i);
			goto err_free;
		}

		if (op == OP_GLOB) {
			parse_error(pe, FILT_ERR_ILLEGAL_FIELD_OP, pos + i);
			goto err_free;
		}

		/* We allow 0xDEADBEEF */
		while (isalnum(str[i]))
			i++;

		len = i - s;
		/* 0xfeedfacedeadbeef is 18 chars max */
		if (len >= sizeof(num_buf)) {
			parse_error(pe, FILT_ERR_OPERAND_TOO_LONG, pos + i);
			goto err_free;
		}

		strncpy(num_buf, str + s, len);
		num_buf[len] = 0;

		/* Make sure it is a value */
		if (field->is_signed)
			ret = kstrtoll(num_buf, 0, &val);
		else
			ret = kstrtoull(num_buf, 0, &val);
		if (ret) {
			parse_error(pe, FILT_ERR_ILLEGAL_INTVAL, pos + s);
			goto err_free;
		}

		pred->val = val;

		if (field->filter_type == FILTER_CPU)
			pred->fn = filter_pred_cpu;
		else {
			pred->fn = select_comparison_fn(pred->op, field->size,
							field->is_signed);
			if (pred->op == OP_NE)
				pred->not = 1;
		}

	} else {
		parse_error(pe, FILT_ERR_INVALID_VALUE, pos + i);
		goto err_free;
	}

	*pred_ptr = pred;
	return i;

err_free:
	kfree(pred);
	return -EINVAL;
}

enum {
	TOO_MANY_CLOSE		= -1,
	TOO_MANY_OPEN		= -2,
	MISSING_QUOTE		= -3,
};

/*
 * Read the filter string once to calculate the number of predicates
 * as well as how deep the parentheses go.
 *
 * Returns:
 *   0 - everything is fine (err is undefined)
 *  -1 - too many ')'
 *  -2 - too many '('
 *  -3 - No matching quote
 */
static int calc_stack(const char *str, int *parens, int *preds, int *err)
{
	bool is_pred = false;
	int nr_preds = 0;
	int open = 1; /* Count the expression as "(E)" */
	int last_quote = 0;
	int max_open = 1;
	int quote = 0;
	int i;

	*err = 0;

	for (i = 0; str[i]; i++) {
		if (isspace(str[i]))
			continue;
		if (quote) {
			if (str[i] == quote)
			       quote = 0;
			continue;
		}

		switch (str[i]) {
		case '\'':
		case '"':
			quote = str[i];
			last_quote = i;
			break;
		case '|':
		case '&':
			if (str[i+1] != str[i])
				break;
			is_pred = false;
			continue;
		case '(':
			is_pred = false;
			open++;
			if (open > max_open)
				max_open = open;
			continue;
		case ')':
			is_pred = false;
			if (open == 1) {
				*err = i;
				return TOO_MANY_CLOSE;
			}
			open--;
			continue;
		}
		if (!is_pred) {
			nr_preds++;
			is_pred = true;
		}
	}

	if (quote) {
		*err = last_quote;
		return MISSING_QUOTE;
	}

	if (open != 1) {
		int level = open;

		/* find the bad open */
		for (i--; i; i--) {
			if (quote) {
				if (str[i] == quote)
					quote = 0;
				continue;
			}
			switch (str[i]) {
			case '(':
				if (level == open) {
					*err = i;
					return TOO_MANY_OPEN;
				}
				level--;
				break;
			case ')':
				level++;
				break;
			case '\'':
			case '"':
				quote = str[i];
				break;
			}
		}
		/* First character is the '(' with missing ')' */
		*err = 0;
		return TOO_MANY_OPEN;
	}

	/* Set the size of the required stacks */
	*parens = max_open;
	*preds = nr_preds;
	return 0;
}

static int process_preds(struct trace_event_call *call,
			 const char *filter_string,
			 struct event_filter *filter,
			 struct filter_parse_error *pe)
{
	struct prog_entry *prog;
	int nr_parens;
	int nr_preds;
	int index;
	int ret;

	ret = calc_stack(filter_string, &nr_parens, &nr_preds, &index);
	if (ret < 0) {
		switch (ret) {
		case MISSING_QUOTE:
			parse_error(pe, FILT_ERR_MISSING_QUOTE, index);
			break;
		case TOO_MANY_OPEN:
			parse_error(pe, FILT_ERR_TOO_MANY_OPEN, index);
			break;
		default:
			parse_error(pe, FILT_ERR_TOO_MANY_CLOSE, index);
		}
		return ret;
	}

	if (!nr_preds)
		return -EINVAL;

	prog = predicate_parse(filter_string, nr_parens, nr_preds,
			       parse_pred, call, pe);
	if (IS_ERR(prog))
		return PTR_ERR(prog);

	rcu_assign_pointer(filter->prog, prog);
	return 0;
}

static inline void event_set_filtered_flag(struct trace_event_file *file)
{
	unsigned long old_flags = file->flags;

	file->flags |= EVENT_FILE_FL_FILTERED;

	if (old_flags != file->flags)
		trace_buffered_event_enable();
}

static inline void event_set_filter(struct trace_event_file *file,
				    struct event_filter *filter)
{
	rcu_assign_pointer(file->filter, filter);
}

static inline void event_clear_filter(struct trace_event_file *file)
{
	RCU_INIT_POINTER(file->filter, NULL);
}

static inline void
event_set_no_set_filter_flag(struct trace_event_file *file)
{
	file->flags |= EVENT_FILE_FL_NO_SET_FILTER;
}

static inline void
event_clear_no_set_filter_flag(struct trace_event_file *file)
{
	file->flags &= ~EVENT_FILE_FL_NO_SET_FILTER;
}

static inline bool
event_no_set_filter_flag(struct trace_event_file *file)
{
	if (file->flags & EVENT_FILE_FL_NO_SET_FILTER)
		return true;

	return false;
}

struct filter_list {
	struct list_head	list;
	struct event_filter	*filter;
};

static int process_system_preds(struct trace_subsystem_dir *dir,
				struct trace_array *tr,
				struct filter_parse_error *pe,
				char *filter_string)
{
	struct trace_event_file *file;
	struct filter_list *filter_item;
	struct event_filter *filter = NULL;
	struct filter_list *tmp;
	LIST_HEAD(filter_list);
	bool fail = true;
	int err;

	list_for_each_entry(file, &tr->events, list) {

		if (file->system != dir)
			continue;

		filter = kzalloc(sizeof(*filter), GFP_KERNEL);
		if (!filter)
			goto fail_mem;

		filter->filter_string = kstrdup(filter_string, GFP_KERNEL);
		if (!filter->filter_string)
			goto fail_mem;

		err = process_preds(file->event_call, filter_string, filter, pe);
		if (err) {
			filter_disable(file);
			parse_error(pe, FILT_ERR_BAD_SUBSYS_FILTER, 0);
			append_filter_err(pe, filter);
		} else
			event_set_filtered_flag(file);


		filter_item = kzalloc(sizeof(*filter_item), GFP_KERNEL);
		if (!filter_item)
			goto fail_mem;

		list_add_tail(&filter_item->list, &filter_list);
		/*
		 * Regardless of if this returned an error, we still
		 * replace the filter for the call.
		 */
		filter_item->filter = event_filter(file);
		event_set_filter(file, filter);
		filter = NULL;

		fail = false;
	}

	if (fail)
		goto fail;

	/*
	 * The calls can still be using the old filters.
	 * Do a synchronize_sched() to ensure all calls are
	 * done with them before we free them.
	 */
	synchronize_sched();
	list_for_each_entry_safe(filter_item, tmp, &filter_list, list) {
		__free_filter(filter_item->filter);
		list_del(&filter_item->list);
		kfree(filter_item);
	}
	return 0;
 fail:
	/* No call succeeded */
	list_for_each_entry_safe(filter_item, tmp, &filter_list, list) {
		list_del(&filter_item->list);
		kfree(filter_item);
	}
	parse_error(pe, FILT_ERR_BAD_SUBSYS_FILTER, 0);
	return -EINVAL;
 fail_mem:
	kfree(filter);
	/* If any call succeeded, we still need to sync */
	if (!fail)
		synchronize_sched();
	list_for_each_entry_safe(filter_item, tmp, &filter_list, list) {
		__free_filter(filter_item->filter);
		list_del(&filter_item->list);
		kfree(filter_item);
	}
	return -ENOMEM;
}

static int create_filter_start(char *filter_string, bool set_str,
			       struct filter_parse_error **pse,
			       struct event_filter **filterp)
{
	struct event_filter *filter;
	struct filter_parse_error *pe = NULL;
	int err = 0;

	if (WARN_ON_ONCE(*pse || *filterp))
		return -EINVAL;

	filter = kzalloc(sizeof(*filter), GFP_KERNEL);
	if (filter && set_str) {
		filter->filter_string = kstrdup(filter_string, GFP_KERNEL);
		if (!filter->filter_string)
			err = -ENOMEM;
	}

	pe = kzalloc(sizeof(*pe), GFP_KERNEL);

	if (!filter || !pe || err) {
		kfree(pe);
		__free_filter(filter);
		return -ENOMEM;
	}

	/* we're committed to creating a new filter */
	*filterp = filter;
	*pse = pe;

	return 0;
}

static void create_filter_finish(struct filter_parse_error *pe)
{
	kfree(pe);
}

/**
 * create_filter - create a filter for a trace_event_call
 * @call: trace_event_call to create a filter for
 * @filter_str: filter string
 * @set_str: remember @filter_str and enable detailed error in filter
 * @filterp: out param for created filter (always updated on return)
 *
 * Creates a filter for @call with @filter_str.  If @set_str is %true,
 * @filter_str is copied and recorded in the new filter.
 *
 * On success, returns 0 and *@filterp points to the new filter.  On
 * failure, returns -errno and *@filterp may point to %NULL or to a new
 * filter.  In the latter case, the returned filter contains error
 * information if @set_str is %true and the caller is responsible for
 * freeing it.
 */
static int create_filter(struct trace_event_call *call,
			 char *filter_string, bool set_str,
			 struct event_filter **filterp)
{
	struct filter_parse_error *pe = NULL;
	int err;

	err = create_filter_start(filter_string, set_str, &pe, filterp);
	if (err)
		return err;

	err = process_preds(call, filter_string, *filterp, pe);
	if (err && set_str)
		append_filter_err(pe, *filterp);

	return err;
}

int create_event_filter(struct trace_event_call *call,
			char *filter_str, bool set_str,
			struct event_filter **filterp)
{
	return create_filter(call, filter_str, set_str, filterp);
}

/**
 * create_system_filter - create a filter for an event_subsystem
 * @system: event_subsystem to create a filter for
 * @filter_str: filter string
 * @filterp: out param for created filter (always updated on return)
 *
 * Identical to create_filter() except that it creates a subsystem filter
 * and always remembers @filter_str.
 */
static int create_system_filter(struct trace_subsystem_dir *dir,
				struct trace_array *tr,
				char *filter_str, struct event_filter **filterp)
{
	struct filter_parse_error *pe = NULL;
	int err;

	err = create_filter_start(filter_str, true, &pe, filterp);
	if (!err) {
		err = process_system_preds(dir, tr, pe, filter_str);
		if (!err) {
			/* System filters just show a default message */
			kfree((*filterp)->filter_string);
			(*filterp)->filter_string = NULL;
		} else {
			append_filter_err(pe, *filterp);
		}
	}
	create_filter_finish(pe);

	return err;
}

/* caller must hold event_mutex */
int apply_event_filter(struct trace_event_file *file, char *filter_string)
{
	struct trace_event_call *call = file->event_call;
	struct event_filter *filter = NULL;
	int err;

	if (!strcmp(strstrip(filter_string), "0")) {
		filter_disable(file);
		filter = event_filter(file);

		if (!filter)
			return 0;

		event_clear_filter(file);

		/* Make sure the filter is not being used */
		synchronize_sched();
		__free_filter(filter);

		return 0;
	}

	err = create_filter(call, filter_string, true, &filter);

	/*
	 * Always swap the call filter with the new filter
	 * even if there was an error. If there was an error
	 * in the filter, we disable the filter and show the error
	 * string
	 */
	if (filter) {
		struct event_filter *tmp;

		tmp = event_filter(file);
		if (!err)
			event_set_filtered_flag(file);
		else
			filter_disable(file);

		event_set_filter(file, filter);

		if (tmp) {
			/* Make sure the call is done with the filter */
			synchronize_sched();
			__free_filter(tmp);
		}
	}

	return err;
}

int apply_subsystem_event_filter(struct trace_subsystem_dir *dir,
				 char *filter_string)
{
	struct event_subsystem *system = dir->subsystem;
	struct trace_array *tr = dir->tr;
	struct event_filter *filter = NULL;
	int err = 0;

	mutex_lock(&event_mutex);

	/* Make sure the system still has events */
	if (!dir->nr_events) {
		err = -ENODEV;
		goto out_unlock;
	}

	if (!strcmp(strstrip(filter_string), "0")) {
		filter_free_subsystem_preds(dir, tr);
		remove_filter_string(system->filter);
		filter = system->filter;
		system->filter = NULL;
		/* Ensure all filters are no longer used */
		synchronize_sched();
		filter_free_subsystem_filters(dir, tr);
		__free_filter(filter);
		goto out_unlock;
	}

	err = create_system_filter(dir, tr, filter_string, &filter);
	if (filter) {
		/*
		 * No event actually uses the system filter
		 * we can free it without synchronize_sched().
		 */
		__free_filter(system->filter);
		system->filter = filter;
	}
out_unlock:
	mutex_unlock(&event_mutex);

	return err;
}

#ifdef CONFIG_PERF_EVENTS

void ftrace_profile_free_filter(struct perf_event *event)
{
	struct event_filter *filter = event->filter;

	event->filter = NULL;
	__free_filter(filter);
}

struct function_filter_data {
	struct ftrace_ops *ops;
	int first_filter;
	int first_notrace;
};

#ifdef CONFIG_FUNCTION_TRACER
static char **
ftrace_function_filter_re(char *buf, int len, int *count)
{
	char *str, **re;

	str = kstrndup(buf, len, GFP_KERNEL);
	if (!str)
		return NULL;

	/*
	 * The argv_split function takes white space
	 * as a separator, so convert ',' into spaces.
	 */
	strreplace(str, ',', ' ');

	re = argv_split(GFP_KERNEL, str, count);
	kfree(str);
	return re;
}

static int ftrace_function_set_regexp(struct ftrace_ops *ops, int filter,
				      int reset, char *re, int len)
{
	int ret;

	if (filter)
		ret = ftrace_set_filter(ops, re, len, reset);
	else
		ret = ftrace_set_notrace(ops, re, len, reset);

	return ret;
}

static int __ftrace_function_set_filter(int filter, char *buf, int len,
					struct function_filter_data *data)
{
	int i, re_cnt, ret = -EINVAL;
	int *reset;
	char **re;

	reset = filter ? &data->first_filter : &data->first_notrace;

	/*
	 * The 'ip' field could have multiple filters set, separated
	 * either by space or comma. We first cut the filter and apply
	 * all pieces separatelly.
	 */
	re = ftrace_function_filter_re(buf, len, &re_cnt);
	if (!re)
		return -EINVAL;

	for (i = 0; i < re_cnt; i++) {
		ret = ftrace_function_set_regexp(data->ops, filter, *reset,
						 re[i], strlen(re[i]));
		if (ret)
			break;

		if (*reset)
			*reset = 0;
	}

	argv_free(re);
	return ret;
}

static int ftrace_function_check_pred(struct filter_pred *pred)
{
	struct ftrace_event_field *field = pred->field;

	/*
	 * Check the predicate for function trace, verify:
	 *  - only '==' and '!=' is used
	 *  - the 'ip' field is used
	 */
	if ((pred->op != OP_EQ) && (pred->op != OP_NE))
		return -EINVAL;

	if (strcmp(field->name, "ip"))
		return -EINVAL;

	return 0;
}

static int ftrace_function_set_filter_pred(struct filter_pred *pred,
					   struct function_filter_data *data)
{
	int ret;

	/* Checking the node is valid for function trace. */
	ret = ftrace_function_check_pred(pred);
	if (ret)
		return ret;

	return __ftrace_function_set_filter(pred->op == OP_EQ,
					    pred->regex.pattern,
					    pred->regex.len,
					    data);
}

static bool is_or(struct prog_entry *prog, int i)
{
	int target;

	/*
	 * Only "||" is allowed for function events, thus,
	 * all true branches should jump to true, and any
	 * false branch should jump to false.
	 */
	target = prog[i].target + 1;
	/* True and false have NULL preds (all prog entries should jump to one */
	if (prog[target].pred)
		return false;

	/* prog[target].target is 1 for TRUE, 0 for FALSE */
	return prog[i].when_to_branch == prog[target].target;
}

static int ftrace_function_set_filter(struct perf_event *event,
				      struct event_filter *filter)
{
	struct prog_entry *prog = rcu_dereference_protected(filter->prog,
						lockdep_is_held(&event_mutex));
	struct function_filter_data data = {
		.first_filter  = 1,
		.first_notrace = 1,
		.ops           = &event->ftrace_ops,
	};
	int i;

	for (i = 0; prog[i].pred; i++) {
		struct filter_pred *pred = prog[i].pred;

		if (!is_or(prog, i))
			return -EINVAL;

		if (ftrace_function_set_filter_pred(pred, &data) < 0)
			return -EINVAL;
	}
	return 0;
}
#else
static int ftrace_function_set_filter(struct perf_event *event,
				      struct event_filter *filter)
{
	return -ENODEV;
}
#endif /* CONFIG_FUNCTION_TRACER */

int ftrace_profile_set_filter(struct perf_event *event, int event_id,
			      char *filter_str)
{
	int err;
	struct event_filter *filter = NULL;
	struct trace_event_call *call;

	mutex_lock(&event_mutex);

	call = event->tp_event;

	err = -EINVAL;
	if (!call)
		goto out_unlock;

	err = -EEXIST;
	if (event->filter)
		goto out_unlock;

	err = create_filter(call, filter_str, false, &filter);
	if (err)
		goto free_filter;

	if (ftrace_event_is_function(call))
		err = ftrace_function_set_filter(event, filter);
	else
		event->filter = filter;

free_filter:
	if (err || ftrace_event_is_function(call))
		__free_filter(filter);

out_unlock:
	mutex_unlock(&event_mutex);

	return err;
}

#endif /* CONFIG_PERF_EVENTS */

#ifdef CONFIG_FTRACE_STARTUP_TEST

#include <linux/types.h>
#include <linux/tracepoint.h>

#define CREATE_TRACE_POINTS
#include "trace_events_filter_test.h"

#define DATA_REC(m, va, vb, vc, vd, ve, vf, vg, vh, nvisit) \
{ \
	.filter = FILTER, \
	.rec    = { .a = va, .b = vb, .c = vc, .d = vd, \
		    .e = ve, .f = vf, .g = vg, .h = vh }, \
	.match  = m, \
	.not_visited = nvisit, \
}
#define YES 1
#define NO  0

static struct test_filter_data_t {
	char *filter;
	struct trace_event_raw_ftrace_test_filter rec;
	int match;
	char *not_visited;
} test_filter_data[] = {
#define FILTER "a == 1 && b == 1 && c == 1 && d == 1 && " \
	       "e == 1 && f == 1 && g == 1 && h == 1"
	DATA_REC(YES, 1, 1, 1, 1, 1, 1, 1, 1, ""),
	DATA_REC(NO,  0, 1, 1, 1, 1, 1, 1, 1, "bcdefgh"),
	DATA_REC(NO,  1, 1, 1, 1, 1, 1, 1, 0, ""),
#undef FILTER
#define FILTER "a == 1 || b == 1 || c == 1 || d == 1 || " \
	       "e == 1 || f == 1 || g == 1 || h == 1"
	DATA_REC(NO,  0, 0, 0, 0, 0, 0, 0, 0, ""),
	DATA_REC(YES, 0, 0, 0, 0, 0, 0, 0, 1, ""),
	DATA_REC(YES, 1, 0, 0, 0, 0, 0, 0, 0, "bcdefgh"),
#undef FILTER
#define FILTER "(a == 1 || b == 1) && (c == 1 || d == 1) && " \
	       "(e == 1 || f == 1) && (g == 1 || h == 1)"
	DATA_REC(NO,  0, 0, 1, 1, 1, 1, 1, 1, "dfh"),
	DATA_REC(YES, 0, 1, 0, 1, 0, 1, 0, 1, ""),
	DATA_REC(YES, 1, 0, 1, 0, 0, 1, 0, 1, "bd"),
	DATA_REC(NO,  1, 0, 1, 0, 0, 1, 0, 0, "bd"),
#undef FILTER
#define FILTER "(a == 1 && b == 1) || (c == 1 && d == 1) || " \
	       "(e == 1 && f == 1) || (g == 1 && h == 1)"
	DATA_REC(YES, 1, 0, 1, 1, 1, 1, 1, 1, "efgh"),
	DATA_REC(YES, 0, 0, 0, 0, 0, 0, 1, 1, ""),
	DATA_REC(NO,  0, 0, 0, 0, 0, 0, 0, 1, ""),
#undef FILTER
#define FILTER "(a == 1 && b == 1) && (c == 1 && d == 1) && " \
	       "(e == 1 && f == 1) || (g == 1 && h == 1)"
	DATA_REC(YES, 1, 1, 1, 1, 1, 1, 0, 0, "gh"),
	DATA_REC(NO,  0, 0, 0, 0, 0, 0, 0, 1, ""),
	DATA_REC(YES, 1, 1, 1, 1, 1, 0, 1, 1, ""),
#undef FILTER
#define FILTER "((a == 1 || b == 1) || (c == 1 || d == 1) || " \
	       "(e == 1 || f == 1)) && (g == 1 || h == 1)"
	DATA_REC(YES, 1, 1, 1, 1, 1, 1, 0, 1, "bcdef"),
	DATA_REC(NO,  0, 0, 0, 0, 0, 0, 0, 0, ""),
	DATA_REC(YES, 1, 1, 1, 1, 1, 0, 1, 1, "h"),
#undef FILTER
#define FILTER "((((((((a == 1) && (b == 1)) || (c == 1)) && (d == 1)) || " \
	       "(e == 1)) && (f == 1)) || (g == 1)) && (h == 1))"
	DATA_REC(YES, 1, 1, 1, 1, 1, 1, 1, 1, "ceg"),
	DATA_REC(NO,  0, 1, 0, 1, 0, 1, 0, 1, ""),
	DATA_REC(NO,  1, 0, 1, 0, 1, 0, 1, 0, ""),
#undef FILTER
#define FILTER "((((((((a == 1) || (b == 1)) && (c == 1)) || (d == 1)) && " \
	       "(e == 1)) || (f == 1)) && (g == 1)) || (h == 1))"
	DATA_REC(YES, 1, 1, 1, 1, 1, 1, 1, 1, "bdfh"),
	DATA_REC(YES, 0, 1, 0, 1, 0, 1, 0, 1, ""),
	DATA_REC(YES, 1, 0, 1, 0, 1, 0, 1, 0, "bdfh"),
};

#undef DATA_REC
#undef FILTER
#undef YES
#undef NO

#define DATA_CNT ARRAY_SIZE(test_filter_data)

static int test_pred_visited;

static int test_pred_visited_fn(struct filter_pred *pred, void *event)
{
	struct ftrace_event_field *field = pred->field;

	test_pred_visited = 1;
	printk(KERN_INFO "\npred visited %s\n", field->name);
	return 1;
}

static void update_pred_fn(struct event_filter *filter, char *fields)
{
	struct prog_entry *prog = rcu_dereference_protected(filter->prog,
						lockdep_is_held(&event_mutex));
	int i;

	for (i = 0; prog[i].pred; i++) {
		struct filter_pred *pred = prog[i].pred;
		struct ftrace_event_field *field = pred->field;

		WARN_ON_ONCE(!pred->fn);

		if (!field) {
			WARN_ONCE(1, "all leafs should have field defined %d", i);
			continue;
		}

		if (!strchr(fields, *field->name))
			continue;

		pred->fn = test_pred_visited_fn;
	}
}

static __init int ftrace_test_event_filter(void)
{
	int i;

	printk(KERN_INFO "Testing ftrace filter: ");

	for (i = 0; i < DATA_CNT; i++) {
		struct event_filter *filter = NULL;
		struct test_filter_data_t *d = &test_filter_data[i];
		int err;

		err = create_filter(&event_ftrace_test_filter, d->filter,
				    false, &filter);
		if (err) {
			printk(KERN_INFO
			       "Failed to get filter for '%s', err %d\n",
			       d->filter, err);
			__free_filter(filter);
			break;
		}

		/* Needed to dereference filter->prog */
		mutex_lock(&event_mutex);
		/*
		 * The preemption disabling is not really needed for self
		 * tests, but the rcu dereference will complain without it.
		 */
		preempt_disable();
		if (*d->not_visited)
			update_pred_fn(filter, d->not_visited);

		test_pred_visited = 0;
		err = filter_match_preds(filter, &d->rec);
		preempt_enable();

		mutex_unlock(&event_mutex);

		__free_filter(filter);

		if (test_pred_visited) {
			printk(KERN_INFO
			       "Failed, unwanted pred visited for filter %s\n",
			       d->filter);
			break;
		}

		if (err != d->match) {
			printk(KERN_INFO
			       "Failed to match filter '%s', expected %d\n",
			       d->filter, d->match);
			break;
		}
	}

	if (i == DATA_CNT)
		printk(KERN_CONT "OK\n");

	return 0;
}

late_initcall(ftrace_test_event_filter);

#endif /* CONFIG_FTRACE_STARTUP_TEST */
