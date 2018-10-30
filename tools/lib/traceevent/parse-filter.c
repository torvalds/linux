// SPDX-License-Identifier: LGPL-2.1
/*
 * Copyright (C) 2010 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>

#include "event-parse.h"
#include "event-parse-local.h"
#include "event-utils.h"

#define COMM "COMM"
#define CPU "CPU"

static struct tep_format_field comm = {
	.name = "COMM",
};

static struct tep_format_field cpu = {
	.name = "CPU",
};

struct event_list {
	struct event_list	*next;
	struct tep_event_format	*event;
};

static void show_error(char *error_buf, const char *fmt, ...)
{
	unsigned long long index;
	const char *input;
	va_list ap;
	int len;
	int i;

	input = tep_get_input_buf();
	index = tep_get_input_buf_ptr();
	len = input ? strlen(input) : 0;

	if (len) {
		strcpy(error_buf, input);
		error_buf[len] = '\n';
		for (i = 1; i < len && i < index; i++)
			error_buf[len+i] = ' ';
		error_buf[len + i] = '^';
		error_buf[len + i + 1] = '\n';
		len += i+2;
	}

	va_start(ap, fmt);
	vsnprintf(error_buf + len, TEP_FILTER_ERROR_BUFSZ - len, fmt, ap);
	va_end(ap);
}

static void free_token(char *token)
{
	tep_free_token(token);
}

static enum tep_event_type read_token(char **tok)
{
	enum tep_event_type type;
	char *token = NULL;

	do {
		free_token(token);
		type = tep_read_token(&token);
	} while (type == TEP_EVENT_NEWLINE || type == TEP_EVENT_SPACE);

	/* If token is = or ! check to see if the next char is ~ */
	if (token &&
	    (strcmp(token, "=") == 0 || strcmp(token, "!") == 0) &&
	    tep_peek_char() == '~') {
		/* append it */
		*tok = malloc(3);
		if (*tok == NULL) {
			free_token(token);
			return TEP_EVENT_ERROR;
		}
		sprintf(*tok, "%c%c", *token, '~');
		free_token(token);
		/* Now remove the '~' from the buffer */
		tep_read_token(&token);
		free_token(token);
	} else
		*tok = token;

	return type;
}

static int filter_cmp(const void *a, const void *b)
{
	const struct tep_filter_type *ea = a;
	const struct tep_filter_type *eb = b;

	if (ea->event_id < eb->event_id)
		return -1;

	if (ea->event_id > eb->event_id)
		return 1;

	return 0;
}

static struct tep_filter_type *
find_filter_type(struct tep_event_filter *filter, int id)
{
	struct tep_filter_type *filter_type;
	struct tep_filter_type key;

	key.event_id = id;

	filter_type = bsearch(&key, filter->event_filters,
			      filter->filters,
			      sizeof(*filter->event_filters),
			      filter_cmp);

	return filter_type;
}

static struct tep_filter_type *
add_filter_type(struct tep_event_filter *filter, int id)
{
	struct tep_filter_type *filter_type;
	int i;

	filter_type = find_filter_type(filter, id);
	if (filter_type)
		return filter_type;

	filter_type = realloc(filter->event_filters,
			      sizeof(*filter->event_filters) *
			      (filter->filters + 1));
	if (!filter_type)
		return NULL;

	filter->event_filters = filter_type;

	for (i = 0; i < filter->filters; i++) {
		if (filter->event_filters[i].event_id > id)
			break;
	}

	if (i < filter->filters)
		memmove(&filter->event_filters[i+1],
			&filter->event_filters[i],
			sizeof(*filter->event_filters) *
			(filter->filters - i));

	filter_type = &filter->event_filters[i];
	filter_type->event_id = id;
	filter_type->event = tep_find_event(filter->pevent, id);
	filter_type->filter = NULL;

	filter->filters++;

	return filter_type;
}

/**
 * tep_filter_alloc - create a new event filter
 * @pevent: The pevent that this filter is associated with
 */
struct tep_event_filter *tep_filter_alloc(struct tep_handle *pevent)
{
	struct tep_event_filter *filter;

	filter = malloc(sizeof(*filter));
	if (filter == NULL)
		return NULL;

	memset(filter, 0, sizeof(*filter));
	filter->pevent = pevent;
	tep_ref(pevent);

	return filter;
}

static struct tep_filter_arg *allocate_arg(void)
{
	return calloc(1, sizeof(struct tep_filter_arg));
}

static void free_arg(struct tep_filter_arg *arg)
{
	if (!arg)
		return;

	switch (arg->type) {
	case TEP_FILTER_ARG_NONE:
	case TEP_FILTER_ARG_BOOLEAN:
		break;

	case TEP_FILTER_ARG_NUM:
		free_arg(arg->num.left);
		free_arg(arg->num.right);
		break;

	case TEP_FILTER_ARG_EXP:
		free_arg(arg->exp.left);
		free_arg(arg->exp.right);
		break;

	case TEP_FILTER_ARG_STR:
		free(arg->str.val);
		regfree(&arg->str.reg);
		free(arg->str.buffer);
		break;

	case TEP_FILTER_ARG_VALUE:
		if (arg->value.type == TEP_FILTER_STRING ||
		    arg->value.type == TEP_FILTER_CHAR)
			free(arg->value.str);
		break;

	case TEP_FILTER_ARG_OP:
		free_arg(arg->op.left);
		free_arg(arg->op.right);
	default:
		break;
	}

	free(arg);
}

static int add_event(struct event_list **events,
		     struct tep_event_format *event)
{
	struct event_list *list;

	list = malloc(sizeof(*list));
	if (list == NULL)
		return -1;

	list->next = *events;
	*events = list;
	list->event = event;
	return 0;
}

static int event_match(struct tep_event_format *event,
		       regex_t *sreg, regex_t *ereg)
{
	if (sreg) {
		return !regexec(sreg, event->system, 0, NULL, 0) &&
			!regexec(ereg, event->name, 0, NULL, 0);
	}

	return !regexec(ereg, event->system, 0, NULL, 0) ||
		!regexec(ereg, event->name, 0, NULL, 0);
}

static enum tep_errno
find_event(struct tep_handle *pevent, struct event_list **events,
	   char *sys_name, char *event_name)
{
	struct tep_event_format *event;
	regex_t ereg;
	regex_t sreg;
	int match = 0;
	int fail = 0;
	char *reg;
	int ret;
	int i;

	if (!event_name) {
		/* if no name is given, then swap sys and name */
		event_name = sys_name;
		sys_name = NULL;
	}

	ret = asprintf(&reg, "^%s$", event_name);
	if (ret < 0)
		return TEP_ERRNO__MEM_ALLOC_FAILED;

	ret = regcomp(&ereg, reg, REG_ICASE|REG_NOSUB);
	free(reg);

	if (ret)
		return TEP_ERRNO__INVALID_EVENT_NAME;

	if (sys_name) {
		ret = asprintf(&reg, "^%s$", sys_name);
		if (ret < 0) {
			regfree(&ereg);
			return TEP_ERRNO__MEM_ALLOC_FAILED;
		}

		ret = regcomp(&sreg, reg, REG_ICASE|REG_NOSUB);
		free(reg);
		if (ret) {
			regfree(&ereg);
			return TEP_ERRNO__INVALID_EVENT_NAME;
		}
	}

	for (i = 0; i < pevent->nr_events; i++) {
		event = pevent->events[i];
		if (event_match(event, sys_name ? &sreg : NULL, &ereg)) {
			match = 1;
			if (add_event(events, event) < 0) {
				fail = 1;
				break;
			}
		}
	}

	regfree(&ereg);
	if (sys_name)
		regfree(&sreg);

	if (!match)
		return TEP_ERRNO__EVENT_NOT_FOUND;
	if (fail)
		return TEP_ERRNO__MEM_ALLOC_FAILED;

	return 0;
}

static void free_events(struct event_list *events)
{
	struct event_list *event;

	while (events) {
		event = events;
		events = events->next;
		free(event);
	}
}

static enum tep_errno
create_arg_item(struct tep_event_format *event, const char *token,
		enum tep_event_type type, struct tep_filter_arg **parg, char *error_str)
{
	struct tep_format_field *field;
	struct tep_filter_arg *arg;

	arg = allocate_arg();
	if (arg == NULL) {
		show_error(error_str, "failed to allocate filter arg");
		return TEP_ERRNO__MEM_ALLOC_FAILED;
	}

	switch (type) {

	case TEP_EVENT_SQUOTE:
	case TEP_EVENT_DQUOTE:
		arg->type = TEP_FILTER_ARG_VALUE;
		arg->value.type =
			type == TEP_EVENT_DQUOTE ? TEP_FILTER_STRING : TEP_FILTER_CHAR;
		arg->value.str = strdup(token);
		if (!arg->value.str) {
			free_arg(arg);
			show_error(error_str, "failed to allocate string filter arg");
			return TEP_ERRNO__MEM_ALLOC_FAILED;
		}
		break;
	case TEP_EVENT_ITEM:
		/* if it is a number, then convert it */
		if (isdigit(token[0])) {
			arg->type = TEP_FILTER_ARG_VALUE;
			arg->value.type = TEP_FILTER_NUMBER;
			arg->value.val = strtoull(token, NULL, 0);
			break;
		}
		/* Consider this a field */
		field = tep_find_any_field(event, token);
		if (!field) {
			/* If token is 'COMM' or 'CPU' then it is special */
			if (strcmp(token, COMM) == 0) {
				field = &comm;
			} else if (strcmp(token, CPU) == 0) {
				field = &cpu;
			} else {
				/* not a field, Make it false */
				arg->type = TEP_FILTER_ARG_BOOLEAN;
				arg->boolean.value = TEP_FILTER_FALSE;
				break;
			}
		}
		arg->type = TEP_FILTER_ARG_FIELD;
		arg->field.field = field;
		break;
	default:
		free_arg(arg);
		show_error(error_str, "expected a value but found %s", token);
		return TEP_ERRNO__UNEXPECTED_TYPE;
	}
	*parg = arg;
	return 0;
}

static struct tep_filter_arg *
create_arg_op(enum tep_filter_op_type btype)
{
	struct tep_filter_arg *arg;

	arg = allocate_arg();
	if (!arg)
		return NULL;

	arg->type = TEP_FILTER_ARG_OP;
	arg->op.type = btype;

	return arg;
}

static struct tep_filter_arg *
create_arg_exp(enum tep_filter_exp_type etype)
{
	struct tep_filter_arg *arg;

	arg = allocate_arg();
	if (!arg)
		return NULL;

	arg->type = TEP_FILTER_ARG_EXP;
	arg->exp.type = etype;

	return arg;
}

static struct tep_filter_arg *
create_arg_cmp(enum tep_filter_cmp_type ctype)
{
	struct tep_filter_arg *arg;

	arg = allocate_arg();
	if (!arg)
		return NULL;

	/* Use NUM and change if necessary */
	arg->type = TEP_FILTER_ARG_NUM;
	arg->num.type = ctype;

	return arg;
}

static enum tep_errno
add_right(struct tep_filter_arg *op, struct tep_filter_arg *arg, char *error_str)
{
	struct tep_filter_arg *left;
	char *str;
	int op_type;
	int ret;

	switch (op->type) {
	case TEP_FILTER_ARG_EXP:
		if (op->exp.right)
			goto out_fail;
		op->exp.right = arg;
		break;

	case TEP_FILTER_ARG_OP:
		if (op->op.right)
			goto out_fail;
		op->op.right = arg;
		break;

	case TEP_FILTER_ARG_NUM:
		if (op->op.right)
			goto out_fail;
		/*
		 * The arg must be num, str, or field
		 */
		switch (arg->type) {
		case TEP_FILTER_ARG_VALUE:
		case TEP_FILTER_ARG_FIELD:
			break;
		default:
			show_error(error_str, "Illegal rvalue");
			return TEP_ERRNO__ILLEGAL_RVALUE;
		}

		/*
		 * Depending on the type, we may need to
		 * convert this to a string or regex.
		 */
		switch (arg->value.type) {
		case TEP_FILTER_CHAR:
			/*
			 * A char should be converted to number if
			 * the string is 1 byte, and the compare
			 * is not a REGEX.
			 */
			if (strlen(arg->value.str) == 1 &&
			    op->num.type != TEP_FILTER_CMP_REGEX &&
			    op->num.type != TEP_FILTER_CMP_NOT_REGEX) {
				arg->value.type = TEP_FILTER_NUMBER;
				goto do_int;
			}
			/* fall through */
		case TEP_FILTER_STRING:

			/* convert op to a string arg */
			op_type = op->num.type;
			left = op->num.left;
			str = arg->value.str;

			/* reset the op for the new field */
			memset(op, 0, sizeof(*op));

			/*
			 * If left arg was a field not found then
			 * NULL the entire op.
			 */
			if (left->type == TEP_FILTER_ARG_BOOLEAN) {
				free_arg(left);
				free_arg(arg);
				op->type = TEP_FILTER_ARG_BOOLEAN;
				op->boolean.value = TEP_FILTER_FALSE;
				break;
			}

			/* Left arg must be a field */
			if (left->type != TEP_FILTER_ARG_FIELD) {
				show_error(error_str,
					   "Illegal lvalue for string comparison");
				return TEP_ERRNO__ILLEGAL_LVALUE;
			}

			/* Make sure this is a valid string compare */
			switch (op_type) {
			case TEP_FILTER_CMP_EQ:
				op_type = TEP_FILTER_CMP_MATCH;
				break;
			case TEP_FILTER_CMP_NE:
				op_type = TEP_FILTER_CMP_NOT_MATCH;
				break;

			case TEP_FILTER_CMP_REGEX:
			case TEP_FILTER_CMP_NOT_REGEX:
				ret = regcomp(&op->str.reg, str, REG_ICASE|REG_NOSUB);
				if (ret) {
					show_error(error_str,
						   "RegEx '%s' did not compute",
						   str);
					return TEP_ERRNO__INVALID_REGEX;
				}
				break;
			default:
				show_error(error_str,
					   "Illegal comparison for string");
				return TEP_ERRNO__ILLEGAL_STRING_CMP;
			}

			op->type = TEP_FILTER_ARG_STR;
			op->str.type = op_type;
			op->str.field = left->field.field;
			op->str.val = strdup(str);
			if (!op->str.val) {
				show_error(error_str, "Failed to allocate string filter");
				return TEP_ERRNO__MEM_ALLOC_FAILED;
			}
			/*
			 * Need a buffer to copy data for tests
			 */
			op->str.buffer = malloc(op->str.field->size + 1);
			if (!op->str.buffer) {
				show_error(error_str, "Failed to allocate string filter");
				return TEP_ERRNO__MEM_ALLOC_FAILED;
			}
			/* Null terminate this buffer */
			op->str.buffer[op->str.field->size] = 0;

			/* We no longer have left or right args */
			free_arg(arg);
			free_arg(left);

			break;

		case TEP_FILTER_NUMBER:

 do_int:
			switch (op->num.type) {
			case TEP_FILTER_CMP_REGEX:
			case TEP_FILTER_CMP_NOT_REGEX:
				show_error(error_str,
					   "Op not allowed with integers");
				return TEP_ERRNO__ILLEGAL_INTEGER_CMP;

			default:
				break;
			}

			/* numeric compare */
			op->num.right = arg;
			break;
		default:
			goto out_fail;
		}
		break;
	default:
		goto out_fail;
	}

	return 0;

 out_fail:
	show_error(error_str, "Syntax error");
	return TEP_ERRNO__SYNTAX_ERROR;
}

static struct tep_filter_arg *
rotate_op_right(struct tep_filter_arg *a, struct tep_filter_arg *b)
{
	struct tep_filter_arg *arg;

	arg = a->op.right;
	a->op.right = b;
	return arg;
}

static enum tep_errno add_left(struct tep_filter_arg *op, struct tep_filter_arg *arg)
{
	switch (op->type) {
	case TEP_FILTER_ARG_EXP:
		if (arg->type == TEP_FILTER_ARG_OP)
			arg = rotate_op_right(arg, op);
		op->exp.left = arg;
		break;

	case TEP_FILTER_ARG_OP:
		op->op.left = arg;
		break;
	case TEP_FILTER_ARG_NUM:
		if (arg->type == TEP_FILTER_ARG_OP)
			arg = rotate_op_right(arg, op);

		/* left arg of compares must be a field */
		if (arg->type != TEP_FILTER_ARG_FIELD &&
		    arg->type != TEP_FILTER_ARG_BOOLEAN)
			return TEP_ERRNO__INVALID_ARG_TYPE;
		op->num.left = arg;
		break;
	default:
		return TEP_ERRNO__INVALID_ARG_TYPE;
	}
	return 0;
}

enum op_type {
	OP_NONE,
	OP_BOOL,
	OP_NOT,
	OP_EXP,
	OP_CMP,
};

static enum op_type process_op(const char *token,
			       enum tep_filter_op_type *btype,
			       enum tep_filter_cmp_type *ctype,
			       enum tep_filter_exp_type *etype)
{
	*btype = TEP_FILTER_OP_NOT;
	*etype = TEP_FILTER_EXP_NONE;
	*ctype = TEP_FILTER_CMP_NONE;

	if (strcmp(token, "&&") == 0)
		*btype = TEP_FILTER_OP_AND;
	else if (strcmp(token, "||") == 0)
		*btype = TEP_FILTER_OP_OR;
	else if (strcmp(token, "!") == 0)
		return OP_NOT;

	if (*btype != TEP_FILTER_OP_NOT)
		return OP_BOOL;

	/* Check for value expressions */
	if (strcmp(token, "+") == 0) {
		*etype = TEP_FILTER_EXP_ADD;
	} else if (strcmp(token, "-") == 0) {
		*etype = TEP_FILTER_EXP_SUB;
	} else if (strcmp(token, "*") == 0) {
		*etype = TEP_FILTER_EXP_MUL;
	} else if (strcmp(token, "/") == 0) {
		*etype = TEP_FILTER_EXP_DIV;
	} else if (strcmp(token, "%") == 0) {
		*etype = TEP_FILTER_EXP_MOD;
	} else if (strcmp(token, ">>") == 0) {
		*etype = TEP_FILTER_EXP_RSHIFT;
	} else if (strcmp(token, "<<") == 0) {
		*etype = TEP_FILTER_EXP_LSHIFT;
	} else if (strcmp(token, "&") == 0) {
		*etype = TEP_FILTER_EXP_AND;
	} else if (strcmp(token, "|") == 0) {
		*etype = TEP_FILTER_EXP_OR;
	} else if (strcmp(token, "^") == 0) {
		*etype = TEP_FILTER_EXP_XOR;
	} else if (strcmp(token, "~") == 0)
		*etype = TEP_FILTER_EXP_NOT;

	if (*etype != TEP_FILTER_EXP_NONE)
		return OP_EXP;

	/* Check for compares */
	if (strcmp(token, "==") == 0)
		*ctype = TEP_FILTER_CMP_EQ;
	else if (strcmp(token, "!=") == 0)
		*ctype = TEP_FILTER_CMP_NE;
	else if (strcmp(token, "<") == 0)
		*ctype = TEP_FILTER_CMP_LT;
	else if (strcmp(token, ">") == 0)
		*ctype = TEP_FILTER_CMP_GT;
	else if (strcmp(token, "<=") == 0)
		*ctype = TEP_FILTER_CMP_LE;
	else if (strcmp(token, ">=") == 0)
		*ctype = TEP_FILTER_CMP_GE;
	else if (strcmp(token, "=~") == 0)
		*ctype = TEP_FILTER_CMP_REGEX;
	else if (strcmp(token, "!~") == 0)
		*ctype = TEP_FILTER_CMP_NOT_REGEX;
	else
		return OP_NONE;

	return OP_CMP;
}

static int check_op_done(struct tep_filter_arg *arg)
{
	switch (arg->type) {
	case TEP_FILTER_ARG_EXP:
		return arg->exp.right != NULL;

	case TEP_FILTER_ARG_OP:
		return arg->op.right != NULL;

	case TEP_FILTER_ARG_NUM:
		return arg->num.right != NULL;

	case TEP_FILTER_ARG_STR:
		/* A string conversion is always done */
		return 1;

	case TEP_FILTER_ARG_BOOLEAN:
		/* field not found, is ok */
		return 1;

	default:
		return 0;
	}
}

enum filter_vals {
	FILTER_VAL_NORM,
	FILTER_VAL_FALSE,
	FILTER_VAL_TRUE,
};

static enum tep_errno
reparent_op_arg(struct tep_filter_arg *parent, struct tep_filter_arg *old_child,
		struct tep_filter_arg *arg, char *error_str)
{
	struct tep_filter_arg *other_child;
	struct tep_filter_arg **ptr;

	if (parent->type != TEP_FILTER_ARG_OP &&
	    arg->type != TEP_FILTER_ARG_OP) {
		show_error(error_str, "can not reparent other than OP");
		return TEP_ERRNO__REPARENT_NOT_OP;
	}

	/* Get the sibling */
	if (old_child->op.right == arg) {
		ptr = &old_child->op.right;
		other_child = old_child->op.left;
	} else if (old_child->op.left == arg) {
		ptr = &old_child->op.left;
		other_child = old_child->op.right;
	} else {
		show_error(error_str, "Error in reparent op, find other child");
		return TEP_ERRNO__REPARENT_FAILED;
	}

	/* Detach arg from old_child */
	*ptr = NULL;

	/* Check for root */
	if (parent == old_child) {
		free_arg(other_child);
		*parent = *arg;
		/* Free arg without recussion */
		free(arg);
		return 0;
	}

	if (parent->op.right == old_child)
		ptr = &parent->op.right;
	else if (parent->op.left == old_child)
		ptr = &parent->op.left;
	else {
		show_error(error_str, "Error in reparent op");
		return TEP_ERRNO__REPARENT_FAILED;
	}

	*ptr = arg;

	free_arg(old_child);
	return 0;
}

/* Returns either filter_vals (success) or tep_errno (failfure) */
static int test_arg(struct tep_filter_arg *parent, struct tep_filter_arg *arg,
		    char *error_str)
{
	int lval, rval;

	switch (arg->type) {

		/* bad case */
	case TEP_FILTER_ARG_BOOLEAN:
		return FILTER_VAL_FALSE + arg->boolean.value;

		/* good cases: */
	case TEP_FILTER_ARG_STR:
	case TEP_FILTER_ARG_VALUE:
	case TEP_FILTER_ARG_FIELD:
		return FILTER_VAL_NORM;

	case TEP_FILTER_ARG_EXP:
		lval = test_arg(arg, arg->exp.left, error_str);
		if (lval != FILTER_VAL_NORM)
			return lval;
		rval = test_arg(arg, arg->exp.right, error_str);
		if (rval != FILTER_VAL_NORM)
			return rval;
		return FILTER_VAL_NORM;

	case TEP_FILTER_ARG_NUM:
		lval = test_arg(arg, arg->num.left, error_str);
		if (lval != FILTER_VAL_NORM)
			return lval;
		rval = test_arg(arg, arg->num.right, error_str);
		if (rval != FILTER_VAL_NORM)
			return rval;
		return FILTER_VAL_NORM;

	case TEP_FILTER_ARG_OP:
		if (arg->op.type != TEP_FILTER_OP_NOT) {
			lval = test_arg(arg, arg->op.left, error_str);
			switch (lval) {
			case FILTER_VAL_NORM:
				break;
			case FILTER_VAL_TRUE:
				if (arg->op.type == TEP_FILTER_OP_OR)
					return FILTER_VAL_TRUE;
				rval = test_arg(arg, arg->op.right, error_str);
				if (rval != FILTER_VAL_NORM)
					return rval;

				return reparent_op_arg(parent, arg, arg->op.right,
						       error_str);

			case FILTER_VAL_FALSE:
				if (arg->op.type == TEP_FILTER_OP_AND)
					return FILTER_VAL_FALSE;
				rval = test_arg(arg, arg->op.right, error_str);
				if (rval != FILTER_VAL_NORM)
					return rval;

				return reparent_op_arg(parent, arg, arg->op.right,
						       error_str);

			default:
				return lval;
			}
		}

		rval = test_arg(arg, arg->op.right, error_str);
		switch (rval) {
		case FILTER_VAL_NORM:
		default:
			break;

		case FILTER_VAL_TRUE:
			if (arg->op.type == TEP_FILTER_OP_OR)
				return FILTER_VAL_TRUE;
			if (arg->op.type == TEP_FILTER_OP_NOT)
				return FILTER_VAL_FALSE;

			return reparent_op_arg(parent, arg, arg->op.left,
					       error_str);

		case FILTER_VAL_FALSE:
			if (arg->op.type == TEP_FILTER_OP_AND)
				return FILTER_VAL_FALSE;
			if (arg->op.type == TEP_FILTER_OP_NOT)
				return FILTER_VAL_TRUE;

			return reparent_op_arg(parent, arg, arg->op.left,
					       error_str);
		}

		return rval;
	default:
		show_error(error_str, "bad arg in filter tree");
		return TEP_ERRNO__BAD_FILTER_ARG;
	}
	return FILTER_VAL_NORM;
}

/* Remove any unknown event fields */
static int collapse_tree(struct tep_filter_arg *arg,
			 struct tep_filter_arg **arg_collapsed, char *error_str)
{
	int ret;

	ret = test_arg(arg, arg, error_str);
	switch (ret) {
	case FILTER_VAL_NORM:
		break;

	case FILTER_VAL_TRUE:
	case FILTER_VAL_FALSE:
		free_arg(arg);
		arg = allocate_arg();
		if (arg) {
			arg->type = TEP_FILTER_ARG_BOOLEAN;
			arg->boolean.value = ret == FILTER_VAL_TRUE;
		} else {
			show_error(error_str, "Failed to allocate filter arg");
			ret = TEP_ERRNO__MEM_ALLOC_FAILED;
		}
		break;

	default:
		/* test_arg() already set the error_str */
		free_arg(arg);
		arg = NULL;
		break;
	}

	*arg_collapsed = arg;
	return ret;
}

static enum tep_errno
process_filter(struct tep_event_format *event, struct tep_filter_arg **parg,
	       char *error_str, int not)
{
	enum tep_event_type type;
	char *token = NULL;
	struct tep_filter_arg *current_op = NULL;
	struct tep_filter_arg *current_exp = NULL;
	struct tep_filter_arg *left_item = NULL;
	struct tep_filter_arg *arg = NULL;
	enum op_type op_type;
	enum tep_filter_op_type btype;
	enum tep_filter_exp_type etype;
	enum tep_filter_cmp_type ctype;
	enum tep_errno ret;

	*parg = NULL;

	do {
		free(token);
		type = read_token(&token);
		switch (type) {
		case TEP_EVENT_SQUOTE:
		case TEP_EVENT_DQUOTE:
		case TEP_EVENT_ITEM:
			ret = create_arg_item(event, token, type, &arg, error_str);
			if (ret < 0)
				goto fail;
			if (!left_item)
				left_item = arg;
			else if (current_exp) {
				ret = add_right(current_exp, arg, error_str);
				if (ret < 0)
					goto fail;
				left_item = NULL;
				/* Not's only one one expression */
				if (not) {
					arg = NULL;
					if (current_op)
						goto fail_syntax;
					free(token);
					*parg = current_exp;
					return 0;
				}
			} else
				goto fail_syntax;
			arg = NULL;
			break;

		case TEP_EVENT_DELIM:
			if (*token == ',') {
				show_error(error_str, "Illegal token ','");
				ret = TEP_ERRNO__ILLEGAL_TOKEN;
				goto fail;
			}

			if (*token == '(') {
				if (left_item) {
					show_error(error_str,
						   "Open paren can not come after item");
					ret = TEP_ERRNO__INVALID_PAREN;
					goto fail;
				}
				if (current_exp) {
					show_error(error_str,
						   "Open paren can not come after expression");
					ret = TEP_ERRNO__INVALID_PAREN;
					goto fail;
				}

				ret = process_filter(event, &arg, error_str, 0);
				if (ret != TEP_ERRNO__UNBALANCED_PAREN) {
					if (ret == 0) {
						show_error(error_str,
							   "Unbalanced number of '('");
						ret = TEP_ERRNO__UNBALANCED_PAREN;
					}
					goto fail;
				}
				ret = 0;

				/* A not wants just one expression */
				if (not) {
					if (current_op)
						goto fail_syntax;
					*parg = arg;
					return 0;
				}

				if (current_op)
					ret = add_right(current_op, arg, error_str);
				else
					current_exp = arg;

				if (ret < 0)
					goto fail;

			} else { /* ')' */
				if (!current_op && !current_exp)
					goto fail_syntax;

				/* Make sure everything is finished at this level */
				if (current_exp && !check_op_done(current_exp))
					goto fail_syntax;
				if (current_op && !check_op_done(current_op))
					goto fail_syntax;

				if (current_op)
					*parg = current_op;
				else
					*parg = current_exp;
				free(token);
				return TEP_ERRNO__UNBALANCED_PAREN;
			}
			break;

		case TEP_EVENT_OP:
			op_type = process_op(token, &btype, &ctype, &etype);

			/* All expect a left arg except for NOT */
			switch (op_type) {
			case OP_BOOL:
				/* Logic ops need a left expression */
				if (!current_exp && !current_op)
					goto fail_syntax;
				/* fall through */
			case OP_NOT:
				/* logic only processes ops and exp */
				if (left_item)
					goto fail_syntax;
				break;
			case OP_EXP:
			case OP_CMP:
				if (!left_item)
					goto fail_syntax;
				break;
			case OP_NONE:
				show_error(error_str,
					   "Unknown op token %s", token);
				ret = TEP_ERRNO__UNKNOWN_TOKEN;
				goto fail;
			}

			ret = 0;
			switch (op_type) {
			case OP_BOOL:
				arg = create_arg_op(btype);
				if (arg == NULL)
					goto fail_alloc;
				if (current_op)
					ret = add_left(arg, current_op);
				else
					ret = add_left(arg, current_exp);
				current_op = arg;
				current_exp = NULL;
				break;

			case OP_NOT:
				arg = create_arg_op(btype);
				if (arg == NULL)
					goto fail_alloc;
				if (current_op)
					ret = add_right(current_op, arg, error_str);
				if (ret < 0)
					goto fail;
				current_exp = arg;
				ret = process_filter(event, &arg, error_str, 1);
				if (ret < 0)
					goto fail;
				ret = add_right(current_exp, arg, error_str);
				if (ret < 0)
					goto fail;
				break;

			case OP_EXP:
			case OP_CMP:
				if (op_type == OP_EXP)
					arg = create_arg_exp(etype);
				else
					arg = create_arg_cmp(ctype);
				if (arg == NULL)
					goto fail_alloc;

				if (current_op)
					ret = add_right(current_op, arg, error_str);
				if (ret < 0)
					goto fail;
				ret = add_left(arg, left_item);
				if (ret < 0) {
					arg = NULL;
					goto fail_syntax;
				}
				current_exp = arg;
				break;
			default:
				break;
			}
			arg = NULL;
			if (ret < 0)
				goto fail_syntax;
			break;
		case TEP_EVENT_NONE:
			break;
		case TEP_EVENT_ERROR:
			goto fail_alloc;
		default:
			goto fail_syntax;
		}
	} while (type != TEP_EVENT_NONE);

	if (!current_op && !current_exp)
		goto fail_syntax;

	if (!current_op)
		current_op = current_exp;

	ret = collapse_tree(current_op, parg, error_str);
	/* collapse_tree() may free current_op, and updates parg accordingly */
	current_op = NULL;
	if (ret < 0)
		goto fail;

	free(token);
	return 0;

 fail_alloc:
	show_error(error_str, "failed to allocate filter arg");
	ret = TEP_ERRNO__MEM_ALLOC_FAILED;
	goto fail;
 fail_syntax:
	show_error(error_str, "Syntax error");
	ret = TEP_ERRNO__SYNTAX_ERROR;
 fail:
	free_arg(current_op);
	free_arg(current_exp);
	free_arg(arg);
	free(token);
	return ret;
}

static enum tep_errno
process_event(struct tep_event_format *event, const char *filter_str,
	      struct tep_filter_arg **parg, char *error_str)
{
	int ret;

	tep_buffer_init(filter_str, strlen(filter_str));

	ret = process_filter(event, parg, error_str, 0);
	if (ret < 0)
		return ret;

	/* If parg is NULL, then make it into FALSE */
	if (!*parg) {
		*parg = allocate_arg();
		if (*parg == NULL)
			return TEP_ERRNO__MEM_ALLOC_FAILED;

		(*parg)->type = TEP_FILTER_ARG_BOOLEAN;
		(*parg)->boolean.value = TEP_FILTER_FALSE;
	}

	return 0;
}

static enum tep_errno
filter_event(struct tep_event_filter *filter, struct tep_event_format *event,
	     const char *filter_str, char *error_str)
{
	struct tep_filter_type *filter_type;
	struct tep_filter_arg *arg;
	enum tep_errno ret;

	if (filter_str) {
		ret = process_event(event, filter_str, &arg, error_str);
		if (ret < 0)
			return ret;

	} else {
		/* just add a TRUE arg */
		arg = allocate_arg();
		if (arg == NULL)
			return TEP_ERRNO__MEM_ALLOC_FAILED;

		arg->type = TEP_FILTER_ARG_BOOLEAN;
		arg->boolean.value = TEP_FILTER_TRUE;
	}

	filter_type = add_filter_type(filter, event->id);
	if (filter_type == NULL)
		return TEP_ERRNO__MEM_ALLOC_FAILED;

	if (filter_type->filter)
		free_arg(filter_type->filter);
	filter_type->filter = arg;

	return 0;
}

static void filter_init_error_buf(struct tep_event_filter *filter)
{
	/* clear buffer to reset show error */
	tep_buffer_init("", 0);
	filter->error_buffer[0] = '\0';
}

/**
 * tep_filter_add_filter_str - add a new filter
 * @filter: the event filter to add to
 * @filter_str: the filter string that contains the filter
 *
 * Returns 0 if the filter was successfully added or a
 * negative error code.  Use tep_filter_strerror() to see
 * actual error message in case of error.
 */
enum tep_errno tep_filter_add_filter_str(struct tep_event_filter *filter,
					 const char *filter_str)
{
	struct tep_handle *pevent = filter->pevent;
	struct event_list *event;
	struct event_list *events = NULL;
	const char *filter_start;
	const char *next_event;
	char *this_event;
	char *event_name = NULL;
	char *sys_name = NULL;
	char *sp;
	enum tep_errno rtn = 0; /* TEP_ERRNO__SUCCESS */
	int len;
	int ret;

	filter_init_error_buf(filter);

	filter_start = strchr(filter_str, ':');
	if (filter_start)
		len = filter_start - filter_str;
	else
		len = strlen(filter_str);

	do {
		next_event = strchr(filter_str, ',');
		if (next_event &&
		    (!filter_start || next_event < filter_start))
			len = next_event - filter_str;
		else if (filter_start)
			len = filter_start - filter_str;
		else
			len = strlen(filter_str);

		this_event = malloc(len + 1);
		if (this_event == NULL) {
			/* This can only happen when events is NULL, but still */
			free_events(events);
			return TEP_ERRNO__MEM_ALLOC_FAILED;
		}
		memcpy(this_event, filter_str, len);
		this_event[len] = 0;

		if (next_event)
			next_event++;

		filter_str = next_event;

		sys_name = strtok_r(this_event, "/", &sp);
		event_name = strtok_r(NULL, "/", &sp);

		if (!sys_name) {
			/* This can only happen when events is NULL, but still */
			free_events(events);
			free(this_event);
			return TEP_ERRNO__FILTER_NOT_FOUND;
		}

		/* Find this event */
		ret = find_event(pevent, &events, strim(sys_name), strim(event_name));
		if (ret < 0) {
			free_events(events);
			free(this_event);
			return ret;
		}
		free(this_event);
	} while (filter_str);

	/* Skip the ':' */
	if (filter_start)
		filter_start++;

	/* filter starts here */
	for (event = events; event; event = event->next) {
		ret = filter_event(filter, event->event, filter_start,
				   filter->error_buffer);
		/* Failures are returned if a parse error happened */
		if (ret < 0)
			rtn = ret;

		if (ret >= 0 && pevent->test_filters) {
			char *test;
			test = tep_filter_make_string(filter, event->event->id);
			if (test) {
				printf(" '%s: %s'\n", event->event->name, test);
				free(test);
			}
		}
	}

	free_events(events);

	if (rtn >= 0 && pevent->test_filters)
		exit(0);

	return rtn;
}

static void free_filter_type(struct tep_filter_type *filter_type)
{
	free_arg(filter_type->filter);
}

/**
 * tep_filter_strerror - fill error message in a buffer
 * @filter: the event filter contains error
 * @err: the error code
 * @buf: the buffer to be filled in
 * @buflen: the size of the buffer
 *
 * Returns 0 if message was filled successfully, -1 if error
 */
int tep_filter_strerror(struct tep_event_filter *filter, enum tep_errno err,
			char *buf, size_t buflen)
{
	if (err <= __TEP_ERRNO__START || err >= __TEP_ERRNO__END)
		return -1;

	if (strlen(filter->error_buffer) > 0) {
		size_t len = snprintf(buf, buflen, "%s", filter->error_buffer);

		if (len > buflen)
			return -1;
		return 0;
	}

	return tep_strerror(filter->pevent, err, buf, buflen);
}

/**
 * tep_filter_remove_event - remove a filter for an event
 * @filter: the event filter to remove from
 * @event_id: the event to remove a filter for
 *
 * Removes the filter saved for an event defined by @event_id
 * from the @filter.
 *
 * Returns 1: if an event was removed
 *   0: if the event was not found
 */
int tep_filter_remove_event(struct tep_event_filter *filter,
			    int event_id)
{
	struct tep_filter_type *filter_type;
	unsigned long len;

	if (!filter->filters)
		return 0;

	filter_type = find_filter_type(filter, event_id);

	if (!filter_type)
		return 0;

	free_filter_type(filter_type);

	/* The filter_type points into the event_filters array */
	len = (unsigned long)(filter->event_filters + filter->filters) -
		(unsigned long)(filter_type + 1);

	memmove(filter_type, filter_type + 1, len);
	filter->filters--;

	memset(&filter->event_filters[filter->filters], 0,
	       sizeof(*filter_type));

	return 1;
}

/**
 * tep_filter_reset - clear all filters in a filter
 * @filter: the event filter to reset
 *
 * Removes all filters from a filter and resets it.
 */
void tep_filter_reset(struct tep_event_filter *filter)
{
	int i;

	for (i = 0; i < filter->filters; i++)
		free_filter_type(&filter->event_filters[i]);

	free(filter->event_filters);
	filter->filters = 0;
	filter->event_filters = NULL;
}

void tep_filter_free(struct tep_event_filter *filter)
{
	tep_unref(filter->pevent);

	tep_filter_reset(filter);

	free(filter);
}

static char *arg_to_str(struct tep_event_filter *filter, struct tep_filter_arg *arg);

static int copy_filter_type(struct tep_event_filter *filter,
			    struct tep_event_filter *source,
			    struct tep_filter_type *filter_type)
{
	struct tep_filter_arg *arg;
	struct tep_event_format *event;
	const char *sys;
	const char *name;
	char *str;

	/* Can't assume that the pevent's are the same */
	sys = filter_type->event->system;
	name = filter_type->event->name;
	event = tep_find_event_by_name(filter->pevent, sys, name);
	if (!event)
		return -1;

	str = arg_to_str(source, filter_type->filter);
	if (!str)
		return -1;

	if (strcmp(str, "TRUE") == 0 || strcmp(str, "FALSE") == 0) {
		/* Add trivial event */
		arg = allocate_arg();
		if (arg == NULL)
			return -1;

		arg->type = TEP_FILTER_ARG_BOOLEAN;
		if (strcmp(str, "TRUE") == 0)
			arg->boolean.value = 1;
		else
			arg->boolean.value = 0;

		filter_type = add_filter_type(filter, event->id);
		if (filter_type == NULL)
			return -1;

		filter_type->filter = arg;

		free(str);
		return 0;
	}

	filter_event(filter, event, str, NULL);
	free(str);

	return 0;
}

/**
 * tep_filter_copy - copy a filter using another filter
 * @dest - the filter to copy to
 * @source - the filter to copy from
 *
 * Returns 0 on success and -1 if not all filters were copied
 */
int tep_filter_copy(struct tep_event_filter *dest, struct tep_event_filter *source)
{
	int ret = 0;
	int i;

	tep_filter_reset(dest);

	for (i = 0; i < source->filters; i++) {
		if (copy_filter_type(dest, source, &source->event_filters[i]))
			ret = -1;
	}
	return ret;
}


/**
 * tep_update_trivial - update the trivial filters with the given filter
 * @dest - the filter to update
 * @source - the filter as the source of the update
 * @type - the type of trivial filter to update.
 *
 * Scan dest for trivial events matching @type to replace with the source.
 *
 * Returns 0 on success and -1 if there was a problem updating, but
 *   events may have still been updated on error.
 */
int tep_update_trivial(struct tep_event_filter *dest, struct tep_event_filter *source,
		       enum tep_filter_trivial_type type)
{
	struct tep_handle *src_pevent;
	struct tep_handle *dest_pevent;
	struct tep_event_format *event;
	struct tep_filter_type *filter_type;
	struct tep_filter_arg *arg;
	char *str;
	int i;

	src_pevent = source->pevent;
	dest_pevent = dest->pevent;

	/* Do nothing if either of the filters has nothing to filter */
	if (!dest->filters || !source->filters)
		return 0;

	for (i = 0; i < dest->filters; i++) {
		filter_type = &dest->event_filters[i];
		arg = filter_type->filter;
		if (arg->type != TEP_FILTER_ARG_BOOLEAN)
			continue;
		if ((arg->boolean.value && type == TEP_FILTER_TRIVIAL_FALSE) ||
		    (!arg->boolean.value && type == TEP_FILTER_TRIVIAL_TRUE))
			continue;

		event = filter_type->event;

		if (src_pevent != dest_pevent) {
			/* do a look up */
			event = tep_find_event_by_name(src_pevent,
						       event->system,
						       event->name);
			if (!event)
				return -1;
		}

		str = tep_filter_make_string(source, event->id);
		if (!str)
			continue;

		/* Don't bother if the filter is trivial too */
		if (strcmp(str, "TRUE") != 0 && strcmp(str, "FALSE") != 0)
			filter_event(dest, event, str, NULL);
		free(str);
	}
	return 0;
}

/**
 * tep_filter_clear_trivial - clear TRUE and FALSE filters
 * @filter: the filter to remove trivial filters from
 * @type: remove only true, false, or both
 *
 * Removes filters that only contain a TRUE or FALES boolean arg.
 *
 * Returns 0 on success and -1 if there was a problem.
 */
int tep_filter_clear_trivial(struct tep_event_filter *filter,
			     enum tep_filter_trivial_type type)
{
	struct tep_filter_type *filter_type;
	int count = 0;
	int *ids = NULL;
	int i;

	if (!filter->filters)
		return 0;

	/*
	 * Two steps, first get all ids with trivial filters.
	 *  then remove those ids.
	 */
	for (i = 0; i < filter->filters; i++) {
		int *new_ids;

		filter_type = &filter->event_filters[i];
		if (filter_type->filter->type != TEP_FILTER_ARG_BOOLEAN)
			continue;
		switch (type) {
		case TEP_FILTER_TRIVIAL_FALSE:
			if (filter_type->filter->boolean.value)
				continue;
			break;
		case TEP_FILTER_TRIVIAL_TRUE:
			if (!filter_type->filter->boolean.value)
				continue;
		default:
			break;
		}

		new_ids = realloc(ids, sizeof(*ids) * (count + 1));
		if (!new_ids) {
			free(ids);
			return -1;
		}

		ids = new_ids;
		ids[count++] = filter_type->event_id;
	}

	if (!count)
		return 0;

	for (i = 0; i < count; i++)
		tep_filter_remove_event(filter, ids[i]);

	free(ids);
	return 0;
}

/**
 * tep_filter_event_has_trivial - return true event contains trivial filter
 * @filter: the filter with the information
 * @event_id: the id of the event to test
 * @type: trivial type to test for (TRUE, FALSE, EITHER)
 *
 * Returns 1 if the event contains a matching trivial type
 *  otherwise 0.
 */
int tep_filter_event_has_trivial(struct tep_event_filter *filter,
				 int event_id,
				 enum tep_filter_trivial_type type)
{
	struct tep_filter_type *filter_type;

	if (!filter->filters)
		return 0;

	filter_type = find_filter_type(filter, event_id);

	if (!filter_type)
		return 0;

	if (filter_type->filter->type != TEP_FILTER_ARG_BOOLEAN)
		return 0;

	switch (type) {
	case TEP_FILTER_TRIVIAL_FALSE:
		return !filter_type->filter->boolean.value;

	case TEP_FILTER_TRIVIAL_TRUE:
		return filter_type->filter->boolean.value;
	default:
		return 1;
	}
}

static int test_filter(struct tep_event_format *event, struct tep_filter_arg *arg,
		       struct tep_record *record, enum tep_errno *err);

static const char *
get_comm(struct tep_event_format *event, struct tep_record *record)
{
	const char *comm;
	int pid;

	pid = tep_data_pid(event->pevent, record);
	comm = tep_data_comm_from_pid(event->pevent, pid);
	return comm;
}

static unsigned long long
get_value(struct tep_event_format *event,
	  struct tep_format_field *field, struct tep_record *record)
{
	unsigned long long val;

	/* Handle our dummy "comm" field */
	if (field == &comm) {
		const char *name;

		name = get_comm(event, record);
		return (unsigned long)name;
	}

	/* Handle our dummy "cpu" field */
	if (field == &cpu)
		return record->cpu;

	tep_read_number_field(field, record->data, &val);

	if (!(field->flags & TEP_FIELD_IS_SIGNED))
		return val;

	switch (field->size) {
	case 1:
		return (char)val;
	case 2:
		return (short)val;
	case 4:
		return (int)val;
	case 8:
		return (long long)val;
	}
	return val;
}

static unsigned long long
get_arg_value(struct tep_event_format *event, struct tep_filter_arg *arg,
	      struct tep_record *record, enum tep_errno *err);

static unsigned long long
get_exp_value(struct tep_event_format *event, struct tep_filter_arg *arg,
	      struct tep_record *record, enum tep_errno *err)
{
	unsigned long long lval, rval;

	lval = get_arg_value(event, arg->exp.left, record, err);
	rval = get_arg_value(event, arg->exp.right, record, err);

	if (*err) {
		/*
		 * There was an error, no need to process anymore.
		 */
		return 0;
	}

	switch (arg->exp.type) {
	case TEP_FILTER_EXP_ADD:
		return lval + rval;

	case TEP_FILTER_EXP_SUB:
		return lval - rval;

	case TEP_FILTER_EXP_MUL:
		return lval * rval;

	case TEP_FILTER_EXP_DIV:
		return lval / rval;

	case TEP_FILTER_EXP_MOD:
		return lval % rval;

	case TEP_FILTER_EXP_RSHIFT:
		return lval >> rval;

	case TEP_FILTER_EXP_LSHIFT:
		return lval << rval;

	case TEP_FILTER_EXP_AND:
		return lval & rval;

	case TEP_FILTER_EXP_OR:
		return lval | rval;

	case TEP_FILTER_EXP_XOR:
		return lval ^ rval;

	case TEP_FILTER_EXP_NOT:
	default:
		if (!*err)
			*err = TEP_ERRNO__INVALID_EXP_TYPE;
	}
	return 0;
}

static unsigned long long
get_arg_value(struct tep_event_format *event, struct tep_filter_arg *arg,
	      struct tep_record *record, enum tep_errno *err)
{
	switch (arg->type) {
	case TEP_FILTER_ARG_FIELD:
		return get_value(event, arg->field.field, record);

	case TEP_FILTER_ARG_VALUE:
		if (arg->value.type != TEP_FILTER_NUMBER) {
			if (!*err)
				*err = TEP_ERRNO__NOT_A_NUMBER;
		}
		return arg->value.val;

	case TEP_FILTER_ARG_EXP:
		return get_exp_value(event, arg, record, err);

	default:
		if (!*err)
			*err = TEP_ERRNO__INVALID_ARG_TYPE;
	}
	return 0;
}

static int test_num(struct tep_event_format *event, struct tep_filter_arg *arg,
		    struct tep_record *record, enum tep_errno *err)
{
	unsigned long long lval, rval;

	lval = get_arg_value(event, arg->num.left, record, err);
	rval = get_arg_value(event, arg->num.right, record, err);

	if (*err) {
		/*
		 * There was an error, no need to process anymore.
		 */
		return 0;
	}

	switch (arg->num.type) {
	case TEP_FILTER_CMP_EQ:
		return lval == rval;

	case TEP_FILTER_CMP_NE:
		return lval != rval;

	case TEP_FILTER_CMP_GT:
		return lval > rval;

	case TEP_FILTER_CMP_LT:
		return lval < rval;

	case TEP_FILTER_CMP_GE:
		return lval >= rval;

	case TEP_FILTER_CMP_LE:
		return lval <= rval;

	default:
		if (!*err)
			*err = TEP_ERRNO__ILLEGAL_INTEGER_CMP;
		return 0;
	}
}

static const char *get_field_str(struct tep_filter_arg *arg, struct tep_record *record)
{
	struct tep_event_format *event;
	struct tep_handle *pevent;
	unsigned long long addr;
	const char *val = NULL;
	unsigned int size;
	char hex[64];

	/* If the field is not a string convert it */
	if (arg->str.field->flags & TEP_FIELD_IS_STRING) {
		val = record->data + arg->str.field->offset;
		size = arg->str.field->size;

		if (arg->str.field->flags & TEP_FIELD_IS_DYNAMIC) {
			addr = *(unsigned int *)val;
			val = record->data + (addr & 0xffff);
			size = addr >> 16;
		}

		/*
		 * We need to copy the data since we can't be sure the field
		 * is null terminated.
		 */
		if (*(val + size - 1)) {
			/* copy it */
			memcpy(arg->str.buffer, val, arg->str.field->size);
			/* the buffer is already NULL terminated */
			val = arg->str.buffer;
		}

	} else {
		event = arg->str.field->event;
		pevent = event->pevent;
		addr = get_value(event, arg->str.field, record);

		if (arg->str.field->flags & (TEP_FIELD_IS_POINTER | TEP_FIELD_IS_LONG))
			/* convert to a kernel symbol */
			val = tep_find_function(pevent, addr);

		if (val == NULL) {
			/* just use the hex of the string name */
			snprintf(hex, 64, "0x%llx", addr);
			val = hex;
		}
	}

	return val;
}

static int test_str(struct tep_event_format *event, struct tep_filter_arg *arg,
		    struct tep_record *record, enum tep_errno *err)
{
	const char *val;

	if (arg->str.field == &comm)
		val = get_comm(event, record);
	else
		val = get_field_str(arg, record);

	switch (arg->str.type) {
	case TEP_FILTER_CMP_MATCH:
		return strcmp(val, arg->str.val) == 0;

	case TEP_FILTER_CMP_NOT_MATCH:
		return strcmp(val, arg->str.val) != 0;

	case TEP_FILTER_CMP_REGEX:
		/* Returns zero on match */
		return !regexec(&arg->str.reg, val, 0, NULL, 0);

	case TEP_FILTER_CMP_NOT_REGEX:
		return regexec(&arg->str.reg, val, 0, NULL, 0);

	default:
		if (!*err)
			*err = TEP_ERRNO__ILLEGAL_STRING_CMP;
		return 0;
	}
}

static int test_op(struct tep_event_format *event, struct tep_filter_arg *arg,
		   struct tep_record *record, enum tep_errno *err)
{
	switch (arg->op.type) {
	case TEP_FILTER_OP_AND:
		return test_filter(event, arg->op.left, record, err) &&
			test_filter(event, arg->op.right, record, err);

	case TEP_FILTER_OP_OR:
		return test_filter(event, arg->op.left, record, err) ||
			test_filter(event, arg->op.right, record, err);

	case TEP_FILTER_OP_NOT:
		return !test_filter(event, arg->op.right, record, err);

	default:
		if (!*err)
			*err = TEP_ERRNO__INVALID_OP_TYPE;
		return 0;
	}
}

static int test_filter(struct tep_event_format *event, struct tep_filter_arg *arg,
		       struct tep_record *record, enum tep_errno *err)
{
	if (*err) {
		/*
		 * There was an error, no need to process anymore.
		 */
		return 0;
	}

	switch (arg->type) {
	case TEP_FILTER_ARG_BOOLEAN:
		/* easy case */
		return arg->boolean.value;

	case TEP_FILTER_ARG_OP:
		return test_op(event, arg, record, err);

	case TEP_FILTER_ARG_NUM:
		return test_num(event, arg, record, err);

	case TEP_FILTER_ARG_STR:
		return test_str(event, arg, record, err);

	case TEP_FILTER_ARG_EXP:
	case TEP_FILTER_ARG_VALUE:
	case TEP_FILTER_ARG_FIELD:
		/*
		 * Expressions, fields and values evaluate
		 * to true if they return non zero
		 */
		return !!get_arg_value(event, arg, record, err);

	default:
		if (!*err)
			*err = TEP_ERRNO__INVALID_ARG_TYPE;
		return 0;
	}
}

/**
 * tep_event_filtered - return true if event has filter
 * @filter: filter struct with filter information
 * @event_id: event id to test if filter exists
 *
 * Returns 1 if filter found for @event_id
 *   otherwise 0;
 */
int tep_event_filtered(struct tep_event_filter *filter, int event_id)
{
	struct tep_filter_type *filter_type;

	if (!filter->filters)
		return 0;

	filter_type = find_filter_type(filter, event_id);

	return filter_type ? 1 : 0;
}

/**
 * tep_filter_match - test if a record matches a filter
 * @filter: filter struct with filter information
 * @record: the record to test against the filter
 *
 * Returns: match result or error code (prefixed with TEP_ERRNO__)
 * FILTER_MATCH - filter found for event and @record matches
 * FILTER_MISS  - filter found for event and @record does not match
 * FILTER_NOT_FOUND - no filter found for @record's event
 * NO_FILTER - if no filters exist
 * otherwise - error occurred during test
 */
enum tep_errno tep_filter_match(struct tep_event_filter *filter,
				struct tep_record *record)
{
	struct tep_handle *pevent = filter->pevent;
	struct tep_filter_type *filter_type;
	int event_id;
	int ret;
	enum tep_errno err = 0;

	filter_init_error_buf(filter);

	if (!filter->filters)
		return TEP_ERRNO__NO_FILTER;

	event_id = tep_data_type(pevent, record);

	filter_type = find_filter_type(filter, event_id);
	if (!filter_type)
		return TEP_ERRNO__FILTER_NOT_FOUND;

	ret = test_filter(filter_type->event, filter_type->filter, record, &err);
	if (err)
		return err;

	return ret ? TEP_ERRNO__FILTER_MATCH : TEP_ERRNO__FILTER_MISS;
}

static char *op_to_str(struct tep_event_filter *filter, struct tep_filter_arg *arg)
{
	char *str = NULL;
	char *left = NULL;
	char *right = NULL;
	char *op = NULL;
	int left_val = -1;
	int right_val = -1;
	int val;

	switch (arg->op.type) {
	case TEP_FILTER_OP_AND:
		op = "&&";
		/* fall through */
	case TEP_FILTER_OP_OR:
		if (!op)
			op = "||";

		left = arg_to_str(filter, arg->op.left);
		right = arg_to_str(filter, arg->op.right);
		if (!left || !right)
			break;

		/* Try to consolidate boolean values */
		if (strcmp(left, "TRUE") == 0)
			left_val = 1;
		else if (strcmp(left, "FALSE") == 0)
			left_val = 0;

		if (strcmp(right, "TRUE") == 0)
			right_val = 1;
		else if (strcmp(right, "FALSE") == 0)
			right_val = 0;

		if (left_val >= 0) {
			if ((arg->op.type == TEP_FILTER_OP_AND && !left_val) ||
			    (arg->op.type == TEP_FILTER_OP_OR && left_val)) {
				/* Just return left value */
				str = left;
				left = NULL;
				break;
			}
			if (right_val >= 0) {
				/* just evaluate this. */
				val = 0;
				switch (arg->op.type) {
				case TEP_FILTER_OP_AND:
					val = left_val && right_val;
					break;
				case TEP_FILTER_OP_OR:
					val = left_val || right_val;
					break;
				default:
					break;
				}
				asprintf(&str, val ? "TRUE" : "FALSE");
				break;
			}
		}
		if (right_val >= 0) {
			if ((arg->op.type == TEP_FILTER_OP_AND && !right_val) ||
			    (arg->op.type == TEP_FILTER_OP_OR && right_val)) {
				/* Just return right value */
				str = right;
				right = NULL;
				break;
			}
			/* The right value is meaningless */
			str = left;
			left = NULL;
			break;
		}

		asprintf(&str, "(%s) %s (%s)", left, op, right);
		break;

	case TEP_FILTER_OP_NOT:
		op = "!";
		right = arg_to_str(filter, arg->op.right);
		if (!right)
			break;

		/* See if we can consolidate */
		if (strcmp(right, "TRUE") == 0)
			right_val = 1;
		else if (strcmp(right, "FALSE") == 0)
			right_val = 0;
		if (right_val >= 0) {
			/* just return the opposite */
			asprintf(&str, right_val ? "FALSE" : "TRUE");
			break;
		}
		asprintf(&str, "%s(%s)", op, right);
		break;

	default:
		/* ?? */
		break;
	}
	free(left);
	free(right);
	return str;
}

static char *val_to_str(struct tep_event_filter *filter, struct tep_filter_arg *arg)
{
	char *str = NULL;

	asprintf(&str, "%lld", arg->value.val);

	return str;
}

static char *field_to_str(struct tep_event_filter *filter, struct tep_filter_arg *arg)
{
	return strdup(arg->field.field->name);
}

static char *exp_to_str(struct tep_event_filter *filter, struct tep_filter_arg *arg)
{
	char *lstr;
	char *rstr;
	char *op;
	char *str = NULL;

	lstr = arg_to_str(filter, arg->exp.left);
	rstr = arg_to_str(filter, arg->exp.right);
	if (!lstr || !rstr)
		goto out;

	switch (arg->exp.type) {
	case TEP_FILTER_EXP_ADD:
		op = "+";
		break;
	case TEP_FILTER_EXP_SUB:
		op = "-";
		break;
	case TEP_FILTER_EXP_MUL:
		op = "*";
		break;
	case TEP_FILTER_EXP_DIV:
		op = "/";
		break;
	case TEP_FILTER_EXP_MOD:
		op = "%";
		break;
	case TEP_FILTER_EXP_RSHIFT:
		op = ">>";
		break;
	case TEP_FILTER_EXP_LSHIFT:
		op = "<<";
		break;
	case TEP_FILTER_EXP_AND:
		op = "&";
		break;
	case TEP_FILTER_EXP_OR:
		op = "|";
		break;
	case TEP_FILTER_EXP_XOR:
		op = "^";
		break;
	default:
		op = "[ERROR IN EXPRESSION TYPE]";
		break;
	}

	asprintf(&str, "%s %s %s", lstr, op, rstr);
out:
	free(lstr);
	free(rstr);

	return str;
}

static char *num_to_str(struct tep_event_filter *filter, struct tep_filter_arg *arg)
{
	char *lstr;
	char *rstr;
	char *str = NULL;
	char *op = NULL;

	lstr = arg_to_str(filter, arg->num.left);
	rstr = arg_to_str(filter, arg->num.right);
	if (!lstr || !rstr)
		goto out;

	switch (arg->num.type) {
	case TEP_FILTER_CMP_EQ:
		op = "==";
		/* fall through */
	case TEP_FILTER_CMP_NE:
		if (!op)
			op = "!=";
		/* fall through */
	case TEP_FILTER_CMP_GT:
		if (!op)
			op = ">";
		/* fall through */
	case TEP_FILTER_CMP_LT:
		if (!op)
			op = "<";
		/* fall through */
	case TEP_FILTER_CMP_GE:
		if (!op)
			op = ">=";
		/* fall through */
	case TEP_FILTER_CMP_LE:
		if (!op)
			op = "<=";

		asprintf(&str, "%s %s %s", lstr, op, rstr);
		break;

	default:
		/* ?? */
		break;
	}

out:
	free(lstr);
	free(rstr);
	return str;
}

static char *str_to_str(struct tep_event_filter *filter, struct tep_filter_arg *arg)
{
	char *str = NULL;
	char *op = NULL;

	switch (arg->str.type) {
	case TEP_FILTER_CMP_MATCH:
		op = "==";
		/* fall through */
	case TEP_FILTER_CMP_NOT_MATCH:
		if (!op)
			op = "!=";
		/* fall through */
	case TEP_FILTER_CMP_REGEX:
		if (!op)
			op = "=~";
		/* fall through */
	case TEP_FILTER_CMP_NOT_REGEX:
		if (!op)
			op = "!~";

		asprintf(&str, "%s %s \"%s\"",
			 arg->str.field->name, op, arg->str.val);
		break;

	default:
		/* ?? */
		break;
	}
	return str;
}

static char *arg_to_str(struct tep_event_filter *filter, struct tep_filter_arg *arg)
{
	char *str = NULL;

	switch (arg->type) {
	case TEP_FILTER_ARG_BOOLEAN:
		asprintf(&str, arg->boolean.value ? "TRUE" : "FALSE");
		return str;

	case TEP_FILTER_ARG_OP:
		return op_to_str(filter, arg);

	case TEP_FILTER_ARG_NUM:
		return num_to_str(filter, arg);

	case TEP_FILTER_ARG_STR:
		return str_to_str(filter, arg);

	case TEP_FILTER_ARG_VALUE:
		return val_to_str(filter, arg);

	case TEP_FILTER_ARG_FIELD:
		return field_to_str(filter, arg);

	case TEP_FILTER_ARG_EXP:
		return exp_to_str(filter, arg);

	default:
		/* ?? */
		return NULL;
	}

}

/**
 * tep_filter_make_string - return a string showing the filter
 * @filter: filter struct with filter information
 * @event_id: the event id to return the filter string with
 *
 * Returns a string that displays the filter contents.
 *  This string must be freed with free(str).
 *  NULL is returned if no filter is found or allocation failed.
 */
char *
tep_filter_make_string(struct tep_event_filter *filter, int event_id)
{
	struct tep_filter_type *filter_type;

	if (!filter->filters)
		return NULL;

	filter_type = find_filter_type(filter, event_id);

	if (!filter_type)
		return NULL;

	return arg_to_str(filter, filter_type->filter);
}

/**
 * tep_filter_compare - compare two filters and return if they are the same
 * @filter1: Filter to compare with @filter2
 * @filter2: Filter to compare with @filter1
 *
 * Returns:
 *  1 if the two filters hold the same content.
 *  0 if they do not.
 */
int tep_filter_compare(struct tep_event_filter *filter1, struct tep_event_filter *filter2)
{
	struct tep_filter_type *filter_type1;
	struct tep_filter_type *filter_type2;
	char *str1, *str2;
	int result;
	int i;

	/* Do the easy checks first */
	if (filter1->filters != filter2->filters)
		return 0;
	if (!filter1->filters && !filter2->filters)
		return 1;

	/*
	 * Now take a look at each of the events to see if they have the same
	 * filters to them.
	 */
	for (i = 0; i < filter1->filters; i++) {
		filter_type1 = &filter1->event_filters[i];
		filter_type2 = find_filter_type(filter2, filter_type1->event_id);
		if (!filter_type2)
			break;
		if (filter_type1->filter->type != filter_type2->filter->type)
			break;
		switch (filter_type1->filter->type) {
		case TEP_FILTER_TRIVIAL_FALSE:
		case TEP_FILTER_TRIVIAL_TRUE:
			/* trivial types just need the type compared */
			continue;
		default:
			break;
		}
		/* The best way to compare complex filters is with strings */
		str1 = arg_to_str(filter1, filter_type1->filter);
		str2 = arg_to_str(filter2, filter_type2->filter);
		if (str1 && str2)
			result = strcmp(str1, str2) != 0;
		else
			/* bail out if allocation fails */
			result = 1;

		free(str1);
		free(str2);
		if (result)
			break;
	}

	if (i < filter1->filters)
		return 0;
	return 1;
}

