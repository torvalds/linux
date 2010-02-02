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

#include "trace.h"
#include "trace_output.h"

enum filter_op_ids
{
	OP_OR,
	OP_AND,
	OP_GLOB,
	OP_NE,
	OP_EQ,
	OP_LT,
	OP_LE,
	OP_GT,
	OP_GE,
	OP_NONE,
	OP_OPEN_PAREN,
};

struct filter_op {
	int id;
	char *string;
	int precedence;
};

static struct filter_op filter_ops[] = {
	{ OP_OR,	"||",		1 },
	{ OP_AND,	"&&",		2 },
	{ OP_GLOB,	"~",		4 },
	{ OP_NE,	"!=",		4 },
	{ OP_EQ,	"==",		4 },
	{ OP_LT,	"<",		5 },
	{ OP_LE,	"<=",		5 },
	{ OP_GT,	">",		5 },
	{ OP_GE,	">=",		5 },
	{ OP_NONE,	"OP_NONE",	0 },
	{ OP_OPEN_PAREN, "(",		0 },
};

enum {
	FILT_ERR_NONE,
	FILT_ERR_INVALID_OP,
	FILT_ERR_UNBALANCED_PAREN,
	FILT_ERR_TOO_MANY_OPERANDS,
	FILT_ERR_OPERAND_TOO_LONG,
	FILT_ERR_FIELD_NOT_FOUND,
	FILT_ERR_ILLEGAL_FIELD_OP,
	FILT_ERR_ILLEGAL_INTVAL,
	FILT_ERR_BAD_SUBSYS_FILTER,
	FILT_ERR_TOO_MANY_PREDS,
	FILT_ERR_MISSING_FIELD,
	FILT_ERR_INVALID_FILTER,
};

static char *err_text[] = {
	"No error",
	"Invalid operator",
	"Unbalanced parens",
	"Too many operands",
	"Operand too long",
	"Field not found",
	"Illegal operation for field type",
	"Illegal integer value",
	"Couldn't find or set field in one of a subsystem's events",
	"Too many terms in predicate expression",
	"Missing field name and/or value",
	"Meaningless filter expression",
};

struct opstack_op {
	int op;
	struct list_head list;
};

struct postfix_elt {
	int op;
	char *operand;
	struct list_head list;
};

struct filter_parse_state {
	struct filter_op *ops;
	struct list_head opstack;
	struct list_head postfix;
	int lasterr;
	int lasterr_pos;

	struct {
		char *string;
		unsigned int cnt;
		unsigned int tail;
	} infix;

	struct {
		char string[MAX_FILTER_STR_VAL];
		int pos;
		unsigned int tail;
	} operand;
};

#define DEFINE_COMPARISON_PRED(type)					\
static int filter_pred_##type(struct filter_pred *pred, void *event,	\
			      int val1, int val2)			\
{									\
	type *addr = (type *)(event + pred->offset);			\
	type val = (type)pred->val;					\
	int match = 0;							\
									\
	switch (pred->op) {						\
	case OP_LT:							\
		match = (*addr < val);					\
		break;							\
	case OP_LE:							\
		match = (*addr <= val);					\
		break;							\
	case OP_GT:							\
		match = (*addr > val);					\
		break;							\
	case OP_GE:							\
		match = (*addr >= val);					\
		break;							\
	default:							\
		break;							\
	}								\
									\
	return match;							\
}

#define DEFINE_EQUALITY_PRED(size)					\
static int filter_pred_##size(struct filter_pred *pred, void *event,	\
			      int val1, int val2)			\
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

static int filter_pred_and(struct filter_pred *pred __attribute((unused)),
			   void *event __attribute((unused)),
			   int val1, int val2)
{
	return val1 && val2;
}

static int filter_pred_or(struct filter_pred *pred __attribute((unused)),
			  void *event __attribute((unused)),
			  int val1, int val2)
{
	return val1 || val2;
}

/* Filter predicate for fixed sized arrays of characters */
static int filter_pred_string(struct filter_pred *pred, void *event,
			      int val1, int val2)
{
	char *addr = (char *)(event + pred->offset);
	int cmp, match;

	cmp = pred->regex.match(addr, &pred->regex, pred->regex.field_len);

	match = cmp ^ pred->not;

	return match;
}

/* Filter predicate for char * pointers */
static int filter_pred_pchar(struct filter_pred *pred, void *event,
			     int val1, int val2)
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
static int filter_pred_strloc(struct filter_pred *pred, void *event,
			      int val1, int val2)
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

static int filter_pred_none(struct filter_pred *pred, void *event,
			    int val1, int val2)
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
				*search = buff + 1;
				type = MATCH_END_ONLY;
			} else {
				if (type == MATCH_END_ONLY)
					type = MATCH_MIDDLE_ONLY;
				else
					type = MATCH_FRONT_ONLY;
				buff[i] = 0;
				break;
			}
		}
	}

	return type;
}

static void filter_build_regex(struct filter_pred *pred)
{
	struct regex *r = &pred->regex;
	char *search;
	enum regex_type type = MATCH_FULL;
	int not = 0;

	if (pred->op == OP_GLOB) {
		type = filter_parse_regex(r->pattern, r->len, &search, &not);
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
	}

	pred->not ^= not;
}

/* return 1 if event matches, 0 otherwise (discard) */
int filter_match_preds(struct event_filter *filter, void *rec)
{
	int match, top = 0, val1 = 0, val2 = 0;
	int stack[MAX_FILTER_PRED];
	struct filter_pred *pred;
	int i;

	for (i = 0; i < filter->n_preds; i++) {
		pred = filter->preds[i];
		if (!pred->pop_n) {
			match = pred->fn(pred, rec, val1, val2);
			stack[top++] = match;
			continue;
		}
		if (pred->pop_n > top) {
			WARN_ON_ONCE(1);
			return 0;
		}
		val1 = stack[--top];
		val2 = stack[--top];
		match = pred->fn(pred, rec, val1, val2);
		stack[top++] = match;
	}

	return stack[--top];
}
EXPORT_SYMBOL_GPL(filter_match_preds);

static void parse_error(struct filter_parse_state *ps, int err, int pos)
{
	ps->lasterr = err;
	ps->lasterr_pos = pos;
}

static void remove_filter_string(struct event_filter *filter)
{
	kfree(filter->filter_string);
	filter->filter_string = NULL;
}

static int replace_filter_string(struct event_filter *filter,
				 char *filter_string)
{
	kfree(filter->filter_string);
	filter->filter_string = kstrdup(filter_string, GFP_KERNEL);
	if (!filter->filter_string)
		return -ENOMEM;

	return 0;
}

static int append_filter_string(struct event_filter *filter,
				char *string)
{
	int newlen;
	char *new_filter_string;

	BUG_ON(!filter->filter_string);
	newlen = strlen(filter->filter_string) + strlen(string) + 1;
	new_filter_string = kmalloc(newlen, GFP_KERNEL);
	if (!new_filter_string)
		return -ENOMEM;

	strcpy(new_filter_string, filter->filter_string);
	strcat(new_filter_string, string);
	kfree(filter->filter_string);
	filter->filter_string = new_filter_string;

	return 0;
}

static void append_filter_err(struct filter_parse_state *ps,
			      struct event_filter *filter)
{
	int pos = ps->lasterr_pos;
	char *buf, *pbuf;

	buf = (char *)__get_free_page(GFP_TEMPORARY);
	if (!buf)
		return;

	append_filter_string(filter, "\n");
	memset(buf, ' ', PAGE_SIZE);
	if (pos > PAGE_SIZE - 128)
		pos = 0;
	buf[pos] = '^';
	pbuf = &buf[pos] + 1;

	sprintf(pbuf, "\nparse_error: %s\n", err_text[ps->lasterr]);
	append_filter_string(filter, buf);
	free_page((unsigned long) buf);
}

void print_event_filter(struct ftrace_event_call *call, struct trace_seq *s)
{
	struct event_filter *filter = call->filter;

	mutex_lock(&event_mutex);
	if (filter && filter->filter_string)
		trace_seq_printf(s, "%s\n", filter->filter_string);
	else
		trace_seq_printf(s, "none\n");
	mutex_unlock(&event_mutex);
}

void print_subsystem_event_filter(struct event_subsystem *system,
				  struct trace_seq *s)
{
	struct event_filter *filter = system->filter;

	mutex_lock(&event_mutex);
	if (filter && filter->filter_string)
		trace_seq_printf(s, "%s\n", filter->filter_string);
	else
		trace_seq_printf(s, "none\n");
	mutex_unlock(&event_mutex);
}

static struct ftrace_event_field *
find_event_field(struct ftrace_event_call *call, char *name)
{
	struct ftrace_event_field *field;

	list_for_each_entry(field, &call->fields, link) {
		if (!strcmp(field->name, name))
			return field;
	}

	return NULL;
}

static void filter_free_pred(struct filter_pred *pred)
{
	if (!pred)
		return;

	kfree(pred->field_name);
	kfree(pred);
}

static void filter_clear_pred(struct filter_pred *pred)
{
	kfree(pred->field_name);
	pred->field_name = NULL;
	pred->regex.len = 0;
}

static int filter_set_pred(struct filter_pred *dest,
			   struct filter_pred *src,
			   filter_pred_fn_t fn)
{
	*dest = *src;
	if (src->field_name) {
		dest->field_name = kstrdup(src->field_name, GFP_KERNEL);
		if (!dest->field_name)
			return -ENOMEM;
	}
	dest->fn = fn;

	return 0;
}

static void filter_disable_preds(struct ftrace_event_call *call)
{
	struct event_filter *filter = call->filter;
	int i;

	call->filter_active = 0;
	filter->n_preds = 0;

	for (i = 0; i < MAX_FILTER_PRED; i++)
		filter->preds[i]->fn = filter_pred_none;
}

static void __free_preds(struct event_filter *filter)
{
	int i;

	if (!filter)
		return;

	for (i = 0; i < MAX_FILTER_PRED; i++) {
		if (filter->preds[i])
			filter_free_pred(filter->preds[i]);
	}
	kfree(filter->preds);
	kfree(filter->filter_string);
	kfree(filter);
}

void destroy_preds(struct ftrace_event_call *call)
{
	__free_preds(call->filter);
	call->filter = NULL;
	call->filter_active = 0;
}

static struct event_filter *__alloc_preds(void)
{
	struct event_filter *filter;
	struct filter_pred *pred;
	int i;

	filter = kzalloc(sizeof(*filter), GFP_KERNEL);
	if (!filter)
		return ERR_PTR(-ENOMEM);

	filter->n_preds = 0;

	filter->preds = kzalloc(MAX_FILTER_PRED * sizeof(pred), GFP_KERNEL);
	if (!filter->preds)
		goto oom;

	for (i = 0; i < MAX_FILTER_PRED; i++) {
		pred = kzalloc(sizeof(*pred), GFP_KERNEL);
		if (!pred)
			goto oom;
		pred->fn = filter_pred_none;
		filter->preds[i] = pred;
	}

	return filter;

oom:
	__free_preds(filter);
	return ERR_PTR(-ENOMEM);
}

static int init_preds(struct ftrace_event_call *call)
{
	if (call->filter)
		return 0;

	call->filter_active = 0;
	call->filter = __alloc_preds();
	if (IS_ERR(call->filter))
		return PTR_ERR(call->filter);

	return 0;
}

static int init_subsystem_preds(struct event_subsystem *system)
{
	struct ftrace_event_call *call;
	int err;

	list_for_each_entry(call, &ftrace_events, list) {
		if (!call->define_fields)
			continue;

		if (strcmp(call->system, system->name) != 0)
			continue;

		err = init_preds(call);
		if (err)
			return err;
	}

	return 0;
}

static void filter_free_subsystem_preds(struct event_subsystem *system)
{
	struct ftrace_event_call *call;

	list_for_each_entry(call, &ftrace_events, list) {
		if (!call->define_fields)
			continue;

		if (strcmp(call->system, system->name) != 0)
			continue;

		filter_disable_preds(call);
		remove_filter_string(call->filter);
	}
}

static int filter_add_pred_fn(struct filter_parse_state *ps,
			      struct ftrace_event_call *call,
			      struct event_filter *filter,
			      struct filter_pred *pred,
			      filter_pred_fn_t fn)
{
	int idx, err;

	if (filter->n_preds == MAX_FILTER_PRED) {
		parse_error(ps, FILT_ERR_TOO_MANY_PREDS, 0);
		return -ENOSPC;
	}

	idx = filter->n_preds;
	filter_clear_pred(filter->preds[idx]);
	err = filter_set_pred(filter->preds[idx], pred, fn);
	if (err)
		return err;

	filter->n_preds++;

	return 0;
}

int filter_assign_type(const char *type)
{
	if (strstr(type, "__data_loc") && strstr(type, "char"))
		return FILTER_DYN_STRING;

	if (strchr(type, '[') && strstr(type, "char"))
		return FILTER_STATIC_STRING;

	return FILTER_OTHER;
}

static bool is_string_field(struct ftrace_event_field *field)
{
	return field->filter_type == FILTER_DYN_STRING ||
	       field->filter_type == FILTER_STATIC_STRING ||
	       field->filter_type == FILTER_PTR_STRING;
}

static int is_legal_op(struct ftrace_event_field *field, int op)
{
	if (is_string_field(field) &&
	    (op != OP_EQ && op != OP_NE && op != OP_GLOB))
		return 0;
	if (!is_string_field(field) && op == OP_GLOB)
		return 0;

	return 1;
}

static filter_pred_fn_t select_comparison_fn(int op, int field_size,
					     int field_is_signed)
{
	filter_pred_fn_t fn = NULL;

	switch (field_size) {
	case 8:
		if (op == OP_EQ || op == OP_NE)
			fn = filter_pred_64;
		else if (field_is_signed)
			fn = filter_pred_s64;
		else
			fn = filter_pred_u64;
		break;
	case 4:
		if (op == OP_EQ || op == OP_NE)
			fn = filter_pred_32;
		else if (field_is_signed)
			fn = filter_pred_s32;
		else
			fn = filter_pred_u32;
		break;
	case 2:
		if (op == OP_EQ || op == OP_NE)
			fn = filter_pred_16;
		else if (field_is_signed)
			fn = filter_pred_s16;
		else
			fn = filter_pred_u16;
		break;
	case 1:
		if (op == OP_EQ || op == OP_NE)
			fn = filter_pred_8;
		else if (field_is_signed)
			fn = filter_pred_s8;
		else
			fn = filter_pred_u8;
		break;
	}

	return fn;
}

static int filter_add_pred(struct filter_parse_state *ps,
			   struct ftrace_event_call *call,
			   struct event_filter *filter,
			   struct filter_pred *pred,
			   bool dry_run)
{
	struct ftrace_event_field *field;
	filter_pred_fn_t fn;
	unsigned long long val;
	int ret;

	pred->fn = filter_pred_none;

	if (pred->op == OP_AND) {
		pred->pop_n = 2;
		fn = filter_pred_and;
		goto add_pred_fn;
	} else if (pred->op == OP_OR) {
		pred->pop_n = 2;
		fn = filter_pred_or;
		goto add_pred_fn;
	}

	field = find_event_field(call, pred->field_name);
	if (!field) {
		parse_error(ps, FILT_ERR_FIELD_NOT_FOUND, 0);
		return -EINVAL;
	}

	pred->offset = field->offset;

	if (!is_legal_op(field, pred->op)) {
		parse_error(ps, FILT_ERR_ILLEGAL_FIELD_OP, 0);
		return -EINVAL;
	}

	if (is_string_field(field)) {
		filter_build_regex(pred);

		if (field->filter_type == FILTER_STATIC_STRING) {
			fn = filter_pred_string;
			pred->regex.field_len = field->size;
		} else if (field->filter_type == FILTER_DYN_STRING)
			fn = filter_pred_strloc;
		else
			fn = filter_pred_pchar;
	} else {
		if (field->is_signed)
			ret = strict_strtoll(pred->regex.pattern, 0, &val);
		else
			ret = strict_strtoull(pred->regex.pattern, 0, &val);
		if (ret) {
			parse_error(ps, FILT_ERR_ILLEGAL_INTVAL, 0);
			return -EINVAL;
		}
		pred->val = val;

		fn = select_comparison_fn(pred->op, field->size,
					  field->is_signed);
		if (!fn) {
			parse_error(ps, FILT_ERR_INVALID_OP, 0);
			return -EINVAL;
		}
	}

	if (pred->op == OP_NE)
		pred->not = 1;

add_pred_fn:
	if (!dry_run)
		return filter_add_pred_fn(ps, call, filter, pred, fn);
	return 0;
}

static void parse_init(struct filter_parse_state *ps,
		       struct filter_op *ops,
		       char *infix_string)
{
	memset(ps, '\0', sizeof(*ps));

	ps->infix.string = infix_string;
	ps->infix.cnt = strlen(infix_string);
	ps->ops = ops;

	INIT_LIST_HEAD(&ps->opstack);
	INIT_LIST_HEAD(&ps->postfix);
}

static char infix_next(struct filter_parse_state *ps)
{
	ps->infix.cnt--;

	return ps->infix.string[ps->infix.tail++];
}

static char infix_peek(struct filter_parse_state *ps)
{
	if (ps->infix.tail == strlen(ps->infix.string))
		return 0;

	return ps->infix.string[ps->infix.tail];
}

static void infix_advance(struct filter_parse_state *ps)
{
	ps->infix.cnt--;
	ps->infix.tail++;
}

static inline int is_precedence_lower(struct filter_parse_state *ps,
				      int a, int b)
{
	return ps->ops[a].precedence < ps->ops[b].precedence;
}

static inline int is_op_char(struct filter_parse_state *ps, char c)
{
	int i;

	for (i = 0; strcmp(ps->ops[i].string, "OP_NONE"); i++) {
		if (ps->ops[i].string[0] == c)
			return 1;
	}

	return 0;
}

static int infix_get_op(struct filter_parse_state *ps, char firstc)
{
	char nextc = infix_peek(ps);
	char opstr[3];
	int i;

	opstr[0] = firstc;
	opstr[1] = nextc;
	opstr[2] = '\0';

	for (i = 0; strcmp(ps->ops[i].string, "OP_NONE"); i++) {
		if (!strcmp(opstr, ps->ops[i].string)) {
			infix_advance(ps);
			return ps->ops[i].id;
		}
	}

	opstr[1] = '\0';

	for (i = 0; strcmp(ps->ops[i].string, "OP_NONE"); i++) {
		if (!strcmp(opstr, ps->ops[i].string))
			return ps->ops[i].id;
	}

	return OP_NONE;
}

static inline void clear_operand_string(struct filter_parse_state *ps)
{
	memset(ps->operand.string, '\0', MAX_FILTER_STR_VAL);
	ps->operand.tail = 0;
}

static inline int append_operand_char(struct filter_parse_state *ps, char c)
{
	if (ps->operand.tail == MAX_FILTER_STR_VAL - 1)
		return -EINVAL;

	ps->operand.string[ps->operand.tail++] = c;

	return 0;
}

static int filter_opstack_push(struct filter_parse_state *ps, int op)
{
	struct opstack_op *opstack_op;

	opstack_op = kmalloc(sizeof(*opstack_op), GFP_KERNEL);
	if (!opstack_op)
		return -ENOMEM;

	opstack_op->op = op;
	list_add(&opstack_op->list, &ps->opstack);

	return 0;
}

static int filter_opstack_empty(struct filter_parse_state *ps)
{
	return list_empty(&ps->opstack);
}

static int filter_opstack_top(struct filter_parse_state *ps)
{
	struct opstack_op *opstack_op;

	if (filter_opstack_empty(ps))
		return OP_NONE;

	opstack_op = list_first_entry(&ps->opstack, struct opstack_op, list);

	return opstack_op->op;
}

static int filter_opstack_pop(struct filter_parse_state *ps)
{
	struct opstack_op *opstack_op;
	int op;

	if (filter_opstack_empty(ps))
		return OP_NONE;

	opstack_op = list_first_entry(&ps->opstack, struct opstack_op, list);
	op = opstack_op->op;
	list_del(&opstack_op->list);

	kfree(opstack_op);

	return op;
}

static void filter_opstack_clear(struct filter_parse_state *ps)
{
	while (!filter_opstack_empty(ps))
		filter_opstack_pop(ps);
}

static char *curr_operand(struct filter_parse_state *ps)
{
	return ps->operand.string;
}

static int postfix_append_operand(struct filter_parse_state *ps, char *operand)
{
	struct postfix_elt *elt;

	elt = kmalloc(sizeof(*elt), GFP_KERNEL);
	if (!elt)
		return -ENOMEM;

	elt->op = OP_NONE;
	elt->operand = kstrdup(operand, GFP_KERNEL);
	if (!elt->operand) {
		kfree(elt);
		return -ENOMEM;
	}

	list_add_tail(&elt->list, &ps->postfix);

	return 0;
}

static int postfix_append_op(struct filter_parse_state *ps, int op)
{
	struct postfix_elt *elt;

	elt = kmalloc(sizeof(*elt), GFP_KERNEL);
	if (!elt)
		return -ENOMEM;

	elt->op = op;
	elt->operand = NULL;

	list_add_tail(&elt->list, &ps->postfix);

	return 0;
}

static void postfix_clear(struct filter_parse_state *ps)
{
	struct postfix_elt *elt;

	while (!list_empty(&ps->postfix)) {
		elt = list_first_entry(&ps->postfix, struct postfix_elt, list);
		list_del(&elt->list);
		kfree(elt->operand);
		kfree(elt);
	}
}

static int filter_parse(struct filter_parse_state *ps)
{
	int in_string = 0;
	int op, top_op;
	char ch;

	while ((ch = infix_next(ps))) {
		if (ch == '"') {
			in_string ^= 1;
			continue;
		}

		if (in_string)
			goto parse_operand;

		if (isspace(ch))
			continue;

		if (is_op_char(ps, ch)) {
			op = infix_get_op(ps, ch);
			if (op == OP_NONE) {
				parse_error(ps, FILT_ERR_INVALID_OP, 0);
				return -EINVAL;
			}

			if (strlen(curr_operand(ps))) {
				postfix_append_operand(ps, curr_operand(ps));
				clear_operand_string(ps);
			}

			while (!filter_opstack_empty(ps)) {
				top_op = filter_opstack_top(ps);
				if (!is_precedence_lower(ps, top_op, op)) {
					top_op = filter_opstack_pop(ps);
					postfix_append_op(ps, top_op);
					continue;
				}
				break;
			}

			filter_opstack_push(ps, op);
			continue;
		}

		if (ch == '(') {
			filter_opstack_push(ps, OP_OPEN_PAREN);
			continue;
		}

		if (ch == ')') {
			if (strlen(curr_operand(ps))) {
				postfix_append_operand(ps, curr_operand(ps));
				clear_operand_string(ps);
			}

			top_op = filter_opstack_pop(ps);
			while (top_op != OP_NONE) {
				if (top_op == OP_OPEN_PAREN)
					break;
				postfix_append_op(ps, top_op);
				top_op = filter_opstack_pop(ps);
			}
			if (top_op == OP_NONE) {
				parse_error(ps, FILT_ERR_UNBALANCED_PAREN, 0);
				return -EINVAL;
			}
			continue;
		}
parse_operand:
		if (append_operand_char(ps, ch)) {
			parse_error(ps, FILT_ERR_OPERAND_TOO_LONG, 0);
			return -EINVAL;
		}
	}

	if (strlen(curr_operand(ps)))
		postfix_append_operand(ps, curr_operand(ps));

	while (!filter_opstack_empty(ps)) {
		top_op = filter_opstack_pop(ps);
		if (top_op == OP_NONE)
			break;
		if (top_op == OP_OPEN_PAREN) {
			parse_error(ps, FILT_ERR_UNBALANCED_PAREN, 0);
			return -EINVAL;
		}
		postfix_append_op(ps, top_op);
	}

	return 0;
}

static struct filter_pred *create_pred(int op, char *operand1, char *operand2)
{
	struct filter_pred *pred;

	pred = kzalloc(sizeof(*pred), GFP_KERNEL);
	if (!pred)
		return NULL;

	pred->field_name = kstrdup(operand1, GFP_KERNEL);
	if (!pred->field_name) {
		kfree(pred);
		return NULL;
	}

	strcpy(pred->regex.pattern, operand2);
	pred->regex.len = strlen(pred->regex.pattern);

	pred->op = op;

	return pred;
}

static struct filter_pred *create_logical_pred(int op)
{
	struct filter_pred *pred;

	pred = kzalloc(sizeof(*pred), GFP_KERNEL);
	if (!pred)
		return NULL;

	pred->op = op;

	return pred;
}

static int check_preds(struct filter_parse_state *ps)
{
	int n_normal_preds = 0, n_logical_preds = 0;
	struct postfix_elt *elt;

	list_for_each_entry(elt, &ps->postfix, list) {
		if (elt->op == OP_NONE)
			continue;

		if (elt->op == OP_AND || elt->op == OP_OR) {
			n_logical_preds++;
			continue;
		}
		n_normal_preds++;
	}

	if (!n_normal_preds || n_logical_preds >= n_normal_preds) {
		parse_error(ps, FILT_ERR_INVALID_FILTER, 0);
		return -EINVAL;
	}

	return 0;
}

static int replace_preds(struct ftrace_event_call *call,
			 struct event_filter *filter,
			 struct filter_parse_state *ps,
			 char *filter_string,
			 bool dry_run)
{
	char *operand1 = NULL, *operand2 = NULL;
	struct filter_pred *pred;
	struct postfix_elt *elt;
	int err;
	int n_preds = 0;

	err = check_preds(ps);
	if (err)
		return err;

	list_for_each_entry(elt, &ps->postfix, list) {
		if (elt->op == OP_NONE) {
			if (!operand1)
				operand1 = elt->operand;
			else if (!operand2)
				operand2 = elt->operand;
			else {
				parse_error(ps, FILT_ERR_TOO_MANY_OPERANDS, 0);
				return -EINVAL;
			}
			continue;
		}

		if (n_preds++ == MAX_FILTER_PRED) {
			parse_error(ps, FILT_ERR_TOO_MANY_PREDS, 0);
			return -ENOSPC;
		}

		if (elt->op == OP_AND || elt->op == OP_OR) {
			pred = create_logical_pred(elt->op);
			goto add_pred;
		}

		if (!operand1 || !operand2) {
			parse_error(ps, FILT_ERR_MISSING_FIELD, 0);
			return -EINVAL;
		}

		pred = create_pred(elt->op, operand1, operand2);
add_pred:
		if (!pred)
			return -ENOMEM;
		err = filter_add_pred(ps, call, filter, pred, dry_run);
		filter_free_pred(pred);
		if (err)
			return err;

		operand1 = operand2 = NULL;
	}

	return 0;
}

static int replace_system_preds(struct event_subsystem *system,
				struct filter_parse_state *ps,
				char *filter_string)
{
	struct ftrace_event_call *call;
	bool fail = true;
	int err;

	list_for_each_entry(call, &ftrace_events, list) {
		struct event_filter *filter = call->filter;

		if (!call->define_fields)
			continue;

		if (strcmp(call->system, system->name) != 0)
			continue;

		/* try to see if the filter can be applied */
		err = replace_preds(call, filter, ps, filter_string, true);
		if (err)
			continue;

		/* really apply the filter */
		filter_disable_preds(call);
		err = replace_preds(call, filter, ps, filter_string, false);
		if (err)
			filter_disable_preds(call);
		else {
			call->filter_active = 1;
			replace_filter_string(filter, filter_string);
		}
		fail = false;
	}

	if (fail) {
		parse_error(ps, FILT_ERR_BAD_SUBSYS_FILTER, 0);
		return -EINVAL;
	}
	return 0;
}

int apply_event_filter(struct ftrace_event_call *call, char *filter_string)
{
	int err;
	struct filter_parse_state *ps;

	mutex_lock(&event_mutex);

	err = init_preds(call);
	if (err)
		goto out_unlock;

	if (!strcmp(strstrip(filter_string), "0")) {
		filter_disable_preds(call);
		remove_filter_string(call->filter);
		goto out_unlock;
	}

	err = -ENOMEM;
	ps = kzalloc(sizeof(*ps), GFP_KERNEL);
	if (!ps)
		goto out_unlock;

	filter_disable_preds(call);
	replace_filter_string(call->filter, filter_string);

	parse_init(ps, filter_ops, filter_string);
	err = filter_parse(ps);
	if (err) {
		append_filter_err(ps, call->filter);
		goto out;
	}

	err = replace_preds(call, call->filter, ps, filter_string, false);
	if (err)
		append_filter_err(ps, call->filter);
	else
		call->filter_active = 1;
out:
	filter_opstack_clear(ps);
	postfix_clear(ps);
	kfree(ps);
out_unlock:
	mutex_unlock(&event_mutex);

	return err;
}

int apply_subsystem_event_filter(struct event_subsystem *system,
				 char *filter_string)
{
	int err;
	struct filter_parse_state *ps;

	mutex_lock(&event_mutex);

	err = init_subsystem_preds(system);
	if (err)
		goto out_unlock;

	if (!strcmp(strstrip(filter_string), "0")) {
		filter_free_subsystem_preds(system);
		remove_filter_string(system->filter);
		goto out_unlock;
	}

	err = -ENOMEM;
	ps = kzalloc(sizeof(*ps), GFP_KERNEL);
	if (!ps)
		goto out_unlock;

	replace_filter_string(system->filter, filter_string);

	parse_init(ps, filter_ops, filter_string);
	err = filter_parse(ps);
	if (err) {
		append_filter_err(ps, system->filter);
		goto out;
	}

	err = replace_system_preds(system, ps, filter_string);
	if (err)
		append_filter_err(ps, system->filter);

out:
	filter_opstack_clear(ps);
	postfix_clear(ps);
	kfree(ps);
out_unlock:
	mutex_unlock(&event_mutex);

	return err;
}

#ifdef CONFIG_EVENT_PROFILE

void ftrace_profile_free_filter(struct perf_event *event)
{
	struct event_filter *filter = event->filter;

	event->filter = NULL;
	__free_preds(filter);
}

int ftrace_profile_set_filter(struct perf_event *event, int event_id,
			      char *filter_str)
{
	int err;
	struct event_filter *filter;
	struct filter_parse_state *ps;
	struct ftrace_event_call *call = NULL;

	mutex_lock(&event_mutex);

	list_for_each_entry(call, &ftrace_events, list) {
		if (call->id == event_id)
			break;
	}

	err = -EINVAL;
	if (!call)
		goto out_unlock;

	err = -EEXIST;
	if (event->filter)
		goto out_unlock;

	filter = __alloc_preds();
	if (IS_ERR(filter)) {
		err = PTR_ERR(filter);
		goto out_unlock;
	}

	err = -ENOMEM;
	ps = kzalloc(sizeof(*ps), GFP_KERNEL);
	if (!ps)
		goto free_preds;

	parse_init(ps, filter_ops, filter_str);
	err = filter_parse(ps);
	if (err)
		goto free_ps;

	err = replace_preds(call, filter, ps, filter_str, false);
	if (!err)
		event->filter = filter;

free_ps:
	filter_opstack_clear(ps);
	postfix_clear(ps);
	kfree(ps);

free_preds:
	if (err)
		__free_preds(filter);

out_unlock:
	mutex_unlock(&event_mutex);

	return err;
}

#endif /* CONFIG_EVENT_PROFILE */

