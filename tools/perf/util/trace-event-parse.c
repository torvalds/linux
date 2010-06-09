/*
 * Copyright (C) 2009, Steven Rostedt <srostedt@redhat.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License (not later!)
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  The parts for function graph printing was taken and modified from the
 *  Linux Kernel that were written by Frederic Weisbecker.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#undef _GNU_SOURCE
#include "../perf.h"
#include "util.h"
#include "trace-event.h"

int header_page_ts_offset;
int header_page_ts_size;
int header_page_size_offset;
int header_page_size_size;
int header_page_data_offset;
int header_page_data_size;

int latency_format;

static char *input_buf;
static unsigned long long input_buf_ptr;
static unsigned long long input_buf_siz;

static int cpus;
static int long_size;
static int is_flag_field;
static int is_symbolic_field;

static struct format_field *
find_any_field(struct event *event, const char *name);

static void init_input_buf(char *buf, unsigned long long size)
{
	input_buf = buf;
	input_buf_siz = size;
	input_buf_ptr = 0;
}

struct cmdline {
	char *comm;
	int pid;
};

static struct cmdline *cmdlines;
static int cmdline_count;

static int cmdline_cmp(const void *a, const void *b)
{
	const struct cmdline *ca = a;
	const struct cmdline *cb = b;

	if (ca->pid < cb->pid)
		return -1;
	if (ca->pid > cb->pid)
		return 1;

	return 0;
}

void parse_cmdlines(char *file, int size __unused)
{
	struct cmdline_list {
		struct cmdline_list	*next;
		char			*comm;
		int			pid;
	} *list = NULL, *item;
	char *line;
	char *next = NULL;
	int i;

	line = strtok_r(file, "\n", &next);
	while (line) {
		item = malloc_or_die(sizeof(*item));
		sscanf(line, "%d %as", &item->pid,
		       (float *)(void *)&item->comm); /* workaround gcc warning */
		item->next = list;
		list = item;
		line = strtok_r(NULL, "\n", &next);
		cmdline_count++;
	}

	cmdlines = malloc_or_die(sizeof(*cmdlines) * cmdline_count);

	i = 0;
	while (list) {
		cmdlines[i].pid = list->pid;
		cmdlines[i].comm = list->comm;
		i++;
		item = list;
		list = list->next;
		free(item);
	}

	qsort(cmdlines, cmdline_count, sizeof(*cmdlines), cmdline_cmp);
}

static struct func_map {
	unsigned long long		addr;
	char				*func;
	char				*mod;
} *func_list;
static unsigned int func_count;

static int func_cmp(const void *a, const void *b)
{
	const struct func_map *fa = a;
	const struct func_map *fb = b;

	if (fa->addr < fb->addr)
		return -1;
	if (fa->addr > fb->addr)
		return 1;

	return 0;
}

void parse_proc_kallsyms(char *file, unsigned int size __unused)
{
	struct func_list {
		struct func_list	*next;
		unsigned long long	addr;
		char			*func;
		char			*mod;
	} *list = NULL, *item;
	char *line;
	char *next = NULL;
	char *addr_str;
	char ch;
	int ret;
	int i;

	line = strtok_r(file, "\n", &next);
	while (line) {
		item = malloc_or_die(sizeof(*item));
		item->mod = NULL;
		ret = sscanf(line, "%as %c %as\t[%as",
			     (float *)(void *)&addr_str, /* workaround gcc warning */
			     &ch,
			     (float *)(void *)&item->func,
			     (float *)(void *)&item->mod);
		item->addr = strtoull(addr_str, NULL, 16);
		free(addr_str);

		/* truncate the extra ']' */
		if (item->mod)
			item->mod[strlen(item->mod) - 1] = 0;


		item->next = list;
		list = item;
		line = strtok_r(NULL, "\n", &next);
		func_count++;
	}

	func_list = malloc_or_die(sizeof(*func_list) * (func_count + 1));

	i = 0;
	while (list) {
		func_list[i].func = list->func;
		func_list[i].addr = list->addr;
		func_list[i].mod = list->mod;
		i++;
		item = list;
		list = list->next;
		free(item);
	}

	qsort(func_list, func_count, sizeof(*func_list), func_cmp);

	/*
	 * Add a special record at the end.
	 */
	func_list[func_count].func = NULL;
	func_list[func_count].addr = 0;
	func_list[func_count].mod = NULL;
}

/*
 * We are searching for a record in between, not an exact
 * match.
 */
static int func_bcmp(const void *a, const void *b)
{
	const struct func_map *fa = a;
	const struct func_map *fb = b;

	if ((fa->addr == fb->addr) ||

	    (fa->addr > fb->addr &&
	     fa->addr < (fb+1)->addr))
		return 0;

	if (fa->addr < fb->addr)
		return -1;

	return 1;
}

static struct func_map *find_func(unsigned long long addr)
{
	struct func_map *func;
	struct func_map key;

	key.addr = addr;

	func = bsearch(&key, func_list, func_count, sizeof(*func_list),
		       func_bcmp);

	return func;
}

void print_funcs(void)
{
	int i;

	for (i = 0; i < (int)func_count; i++) {
		printf("%016llx %s",
		       func_list[i].addr,
		       func_list[i].func);
		if (func_list[i].mod)
			printf(" [%s]\n", func_list[i].mod);
		else
			printf("\n");
	}
}

static struct printk_map {
	unsigned long long		addr;
	char				*printk;
} *printk_list;
static unsigned int printk_count;

static int printk_cmp(const void *a, const void *b)
{
	const struct func_map *fa = a;
	const struct func_map *fb = b;

	if (fa->addr < fb->addr)
		return -1;
	if (fa->addr > fb->addr)
		return 1;

	return 0;
}

static struct printk_map *find_printk(unsigned long long addr)
{
	struct printk_map *printk;
	struct printk_map key;

	key.addr = addr;

	printk = bsearch(&key, printk_list, printk_count, sizeof(*printk_list),
			 printk_cmp);

	return printk;
}

void parse_ftrace_printk(char *file, unsigned int size __unused)
{
	struct printk_list {
		struct printk_list	*next;
		unsigned long long	addr;
		char			*printk;
	} *list = NULL, *item;
	char *line;
	char *next = NULL;
	char *addr_str;
	int i;

	line = strtok_r(file, "\n", &next);
	while (line) {
		addr_str = strsep(&line, ":");
		if (!line) {
			warning("error parsing print strings");
			break;
		}
		item = malloc_or_die(sizeof(*item));
		item->addr = strtoull(addr_str, NULL, 16);
		/* fmt still has a space, skip it */
		item->printk = strdup(line+1);
		item->next = list;
		list = item;
		line = strtok_r(NULL, "\n", &next);
		printk_count++;
	}

	printk_list = malloc_or_die(sizeof(*printk_list) * printk_count + 1);

	i = 0;
	while (list) {
		printk_list[i].printk = list->printk;
		printk_list[i].addr = list->addr;
		i++;
		item = list;
		list = list->next;
		free(item);
	}

	qsort(printk_list, printk_count, sizeof(*printk_list), printk_cmp);
}

void print_printk(void)
{
	int i;

	for (i = 0; i < (int)printk_count; i++) {
		printf("%016llx %s\n",
		       printk_list[i].addr,
		       printk_list[i].printk);
	}
}

static struct event *alloc_event(void)
{
	struct event *event;

	event = malloc_or_die(sizeof(*event));
	memset(event, 0, sizeof(*event));

	return event;
}

enum event_type {
	EVENT_ERROR,
	EVENT_NONE,
	EVENT_SPACE,
	EVENT_NEWLINE,
	EVENT_OP,
	EVENT_DELIM,
	EVENT_ITEM,
	EVENT_DQUOTE,
	EVENT_SQUOTE,
};

static struct event *event_list;

static void add_event(struct event *event)
{
	event->next = event_list;
	event_list = event;
}

static int event_item_type(enum event_type type)
{
	switch (type) {
	case EVENT_ITEM ... EVENT_SQUOTE:
		return 1;
	case EVENT_ERROR ... EVENT_DELIM:
	default:
		return 0;
	}
}

static void free_arg(struct print_arg *arg)
{
	if (!arg)
		return;

	switch (arg->type) {
	case PRINT_ATOM:
		if (arg->atom.atom)
			free(arg->atom.atom);
		break;
	case PRINT_NULL:
	case PRINT_FIELD ... PRINT_OP:
	default:
		/* todo */
		break;
	}

	free(arg);
}

static enum event_type get_type(int ch)
{
	if (ch == '\n')
		return EVENT_NEWLINE;
	if (isspace(ch))
		return EVENT_SPACE;
	if (isalnum(ch) || ch == '_')
		return EVENT_ITEM;
	if (ch == '\'')
		return EVENT_SQUOTE;
	if (ch == '"')
		return EVENT_DQUOTE;
	if (!isprint(ch))
		return EVENT_NONE;
	if (ch == '(' || ch == ')' || ch == ',')
		return EVENT_DELIM;

	return EVENT_OP;
}

static int __read_char(void)
{
	if (input_buf_ptr >= input_buf_siz)
		return -1;

	return input_buf[input_buf_ptr++];
}

static int __peek_char(void)
{
	if (input_buf_ptr >= input_buf_siz)
		return -1;

	return input_buf[input_buf_ptr];
}

static enum event_type __read_token(char **tok)
{
	char buf[BUFSIZ];
	int ch, last_ch, quote_ch, next_ch;
	int i = 0;
	int tok_size = 0;
	enum event_type type;

	*tok = NULL;


	ch = __read_char();
	if (ch < 0)
		return EVENT_NONE;

	type = get_type(ch);
	if (type == EVENT_NONE)
		return type;

	buf[i++] = ch;

	switch (type) {
	case EVENT_NEWLINE:
	case EVENT_DELIM:
		*tok = malloc_or_die(2);
		(*tok)[0] = ch;
		(*tok)[1] = 0;
		return type;

	case EVENT_OP:
		switch (ch) {
		case '-':
			next_ch = __peek_char();
			if (next_ch == '>') {
				buf[i++] = __read_char();
				break;
			}
			/* fall through */
		case '+':
		case '|':
		case '&':
		case '>':
		case '<':
			last_ch = ch;
			ch = __peek_char();
			if (ch != last_ch)
				goto test_equal;
			buf[i++] = __read_char();
			switch (last_ch) {
			case '>':
			case '<':
				goto test_equal;
			default:
				break;
			}
			break;
		case '!':
		case '=':
			goto test_equal;
		default: /* what should we do instead? */
			break;
		}
		buf[i] = 0;
		*tok = strdup(buf);
		return type;

 test_equal:
		ch = __peek_char();
		if (ch == '=')
			buf[i++] = __read_char();
		break;

	case EVENT_DQUOTE:
	case EVENT_SQUOTE:
		/* don't keep quotes */
		i--;
		quote_ch = ch;
		last_ch = 0;
		do {
			if (i == (BUFSIZ - 1)) {
				buf[i] = 0;
				if (*tok) {
					*tok = realloc(*tok, tok_size + BUFSIZ);
					if (!*tok)
						return EVENT_NONE;
					strcat(*tok, buf);
				} else
					*tok = strdup(buf);

				if (!*tok)
					return EVENT_NONE;
				tok_size += BUFSIZ;
				i = 0;
			}
			last_ch = ch;
			ch = __read_char();
			buf[i++] = ch;
			/* the '\' '\' will cancel itself */
			if (ch == '\\' && last_ch == '\\')
				last_ch = 0;
		} while (ch != quote_ch || last_ch == '\\');
		/* remove the last quote */
		i--;
		goto out;

	case EVENT_ERROR ... EVENT_SPACE:
	case EVENT_ITEM:
	default:
		break;
	}

	while (get_type(__peek_char()) == type) {
		if (i == (BUFSIZ - 1)) {
			buf[i] = 0;
			if (*tok) {
				*tok = realloc(*tok, tok_size + BUFSIZ);
				if (!*tok)
					return EVENT_NONE;
				strcat(*tok, buf);
			} else
				*tok = strdup(buf);

			if (!*tok)
				return EVENT_NONE;
			tok_size += BUFSIZ;
			i = 0;
		}
		ch = __read_char();
		buf[i++] = ch;
	}

 out:
	buf[i] = 0;
	if (*tok) {
		*tok = realloc(*tok, tok_size + i);
		if (!*tok)
			return EVENT_NONE;
		strcat(*tok, buf);
	} else
		*tok = strdup(buf);
	if (!*tok)
		return EVENT_NONE;

	return type;
}

static void free_token(char *tok)
{
	if (tok)
		free(tok);
}

static enum event_type read_token(char **tok)
{
	enum event_type type;

	for (;;) {
		type = __read_token(tok);
		if (type != EVENT_SPACE)
			return type;

		free_token(*tok);
	}

	/* not reached */
	return EVENT_NONE;
}

/* no newline */
static enum event_type read_token_item(char **tok)
{
	enum event_type type;

	for (;;) {
		type = __read_token(tok);
		if (type != EVENT_SPACE && type != EVENT_NEWLINE)
			return type;

		free_token(*tok);
	}

	/* not reached */
	return EVENT_NONE;
}

static int test_type(enum event_type type, enum event_type expect)
{
	if (type != expect) {
		warning("Error: expected type %d but read %d",
		    expect, type);
		return -1;
	}
	return 0;
}

static int test_type_token(enum event_type type, char *token,
		    enum event_type expect, const char *expect_tok)
{
	if (type != expect) {
		warning("Error: expected type %d but read %d",
		    expect, type);
		return -1;
	}

	if (strcmp(token, expect_tok) != 0) {
		warning("Error: expected '%s' but read '%s'",
		    expect_tok, token);
		return -1;
	}
	return 0;
}

static int __read_expect_type(enum event_type expect, char **tok, int newline_ok)
{
	enum event_type type;

	if (newline_ok)
		type = read_token(tok);
	else
		type = read_token_item(tok);
	return test_type(type, expect);
}

static int read_expect_type(enum event_type expect, char **tok)
{
	return __read_expect_type(expect, tok, 1);
}

static int __read_expected(enum event_type expect, const char *str, int newline_ok)
{
	enum event_type type;
	char *token;
	int ret;

	if (newline_ok)
		type = read_token(&token);
	else
		type = read_token_item(&token);

	ret = test_type_token(type, token, expect, str);

	free_token(token);

	return ret;
}

static int read_expected(enum event_type expect, const char *str)
{
	return __read_expected(expect, str, 1);
}

static int read_expected_item(enum event_type expect, const char *str)
{
	return __read_expected(expect, str, 0);
}

static char *event_read_name(void)
{
	char *token;

	if (read_expected(EVENT_ITEM, "name") < 0)
		return NULL;

	if (read_expected(EVENT_OP, ":") < 0)
		return NULL;

	if (read_expect_type(EVENT_ITEM, &token) < 0)
		goto fail;

	return token;

 fail:
	free_token(token);
	return NULL;
}

static int event_read_id(void)
{
	char *token;
	int id;

	if (read_expected_item(EVENT_ITEM, "ID") < 0)
		return -1;

	if (read_expected(EVENT_OP, ":") < 0)
		return -1;

	if (read_expect_type(EVENT_ITEM, &token) < 0)
		goto fail;

	id = strtoul(token, NULL, 0);
	free_token(token);
	return id;

 fail:
	free_token(token);
	return -1;
}

static int field_is_string(struct format_field *field)
{
	if ((field->flags & FIELD_IS_ARRAY) &&
	    (!strstr(field->type, "char") || !strstr(field->type, "u8") ||
	     !strstr(field->type, "s8")))
		return 1;

	return 0;
}

static int field_is_dynamic(struct format_field *field)
{
	if (!strcmp(field->type, "__data_loc"))
		return 1;

	return 0;
}

static int event_read_fields(struct event *event, struct format_field **fields)
{
	struct format_field *field = NULL;
	enum event_type type;
	char *token;
	char *last_token;
	int count = 0;

	do {
		type = read_token(&token);
		if (type == EVENT_NEWLINE) {
			free_token(token);
			return count;
		}

		count++;

		if (test_type_token(type, token, EVENT_ITEM, "field"))
			goto fail;
		free_token(token);

		type = read_token(&token);
		/*
		 * The ftrace fields may still use the "special" name.
		 * Just ignore it.
		 */
		if (event->flags & EVENT_FL_ISFTRACE &&
		    type == EVENT_ITEM && strcmp(token, "special") == 0) {
			free_token(token);
			type = read_token(&token);
		}

		if (test_type_token(type, token, EVENT_OP, ":") < 0)
			return -1;

		if (read_expect_type(EVENT_ITEM, &token) < 0)
			goto fail;

		last_token = token;

		field = malloc_or_die(sizeof(*field));
		memset(field, 0, sizeof(*field));

		/* read the rest of the type */
		for (;;) {
			type = read_token(&token);
			if (type == EVENT_ITEM ||
			    (type == EVENT_OP && strcmp(token, "*") == 0) ||
			    /*
			     * Some of the ftrace fields are broken and have
			     * an illegal "." in them.
			     */
			    (event->flags & EVENT_FL_ISFTRACE &&
			     type == EVENT_OP && strcmp(token, ".") == 0)) {

				if (strcmp(token, "*") == 0)
					field->flags |= FIELD_IS_POINTER;

				if (field->type) {
					field->type = realloc(field->type,
							      strlen(field->type) +
							      strlen(last_token) + 2);
					strcat(field->type, " ");
					strcat(field->type, last_token);
				} else
					field->type = last_token;
				last_token = token;
				continue;
			}

			break;
		}

		if (!field->type) {
			die("no type found");
			goto fail;
		}
		field->name = last_token;

		if (test_type(type, EVENT_OP))
			goto fail;

		if (strcmp(token, "[") == 0) {
			enum event_type last_type = type;
			char *brackets = token;
			int len;

			field->flags |= FIELD_IS_ARRAY;

			type = read_token(&token);
		        while (strcmp(token, "]") != 0) {
				if (last_type == EVENT_ITEM &&
				    type == EVENT_ITEM)
					len = 2;
				else
					len = 1;
				last_type = type;

				brackets = realloc(brackets,
						   strlen(brackets) +
						   strlen(token) + len);
				if (len == 2)
					strcat(brackets, " ");
				strcat(brackets, token);
				free_token(token);
				type = read_token(&token);
				if (type == EVENT_NONE) {
					die("failed to find token");
					goto fail;
				}
			}

			free_token(token);

			brackets = realloc(brackets, strlen(brackets) + 2);
			strcat(brackets, "]");

			/* add brackets to type */

			type = read_token(&token);
			/*
			 * If the next token is not an OP, then it is of
			 * the format: type [] item;
			 */
			if (type == EVENT_ITEM) {
				field->type = realloc(field->type,
						      strlen(field->type) +
						      strlen(field->name) +
						      strlen(brackets) + 2);
				strcat(field->type, " ");
				strcat(field->type, field->name);
				free_token(field->name);
				strcat(field->type, brackets);
				field->name = token;
				type = read_token(&token);
			} else {
				field->type = realloc(field->type,
						      strlen(field->type) +
						      strlen(brackets) + 1);
				strcat(field->type, brackets);
			}
			free(brackets);
		}

		if (field_is_string(field)) {
			field->flags |= FIELD_IS_STRING;
			if (field_is_dynamic(field))
				field->flags |= FIELD_IS_DYNAMIC;
		}

		if (test_type_token(type, token,  EVENT_OP, ";"))
			goto fail;
		free_token(token);

		if (read_expected(EVENT_ITEM, "offset") < 0)
			goto fail_expect;

		if (read_expected(EVENT_OP, ":") < 0)
			goto fail_expect;

		if (read_expect_type(EVENT_ITEM, &token))
			goto fail;
		field->offset = strtoul(token, NULL, 0);
		free_token(token);

		if (read_expected(EVENT_OP, ";") < 0)
			goto fail_expect;

		if (read_expected(EVENT_ITEM, "size") < 0)
			goto fail_expect;

		if (read_expected(EVENT_OP, ":") < 0)
			goto fail_expect;

		if (read_expect_type(EVENT_ITEM, &token))
			goto fail;
		field->size = strtoul(token, NULL, 0);
		free_token(token);

		if (read_expected(EVENT_OP, ";") < 0)
			goto fail_expect;

		type = read_token(&token);
		if (type != EVENT_NEWLINE) {
			/* newer versions of the kernel have a "signed" type */
			if (test_type_token(type, token, EVENT_ITEM, "signed"))
				goto fail;

			free_token(token);

			if (read_expected(EVENT_OP, ":") < 0)
				goto fail_expect;

			if (read_expect_type(EVENT_ITEM, &token))
				goto fail;

			if (strtoul(token, NULL, 0))
				field->flags |= FIELD_IS_SIGNED;

			free_token(token);
			if (read_expected(EVENT_OP, ";") < 0)
				goto fail_expect;

			if (read_expect_type(EVENT_NEWLINE, &token))
				goto fail;
		}

		free_token(token);

		*fields = field;
		fields = &field->next;

	} while (1);

	return 0;

fail:
	free_token(token);
fail_expect:
	if (field)
		free(field);
	return -1;
}

static int event_read_format(struct event *event)
{
	char *token;
	int ret;

	if (read_expected_item(EVENT_ITEM, "format") < 0)
		return -1;

	if (read_expected(EVENT_OP, ":") < 0)
		return -1;

	if (read_expect_type(EVENT_NEWLINE, &token))
		goto fail;
	free_token(token);

	ret = event_read_fields(event, &event->format.common_fields);
	if (ret < 0)
		return ret;
	event->format.nr_common = ret;

	ret = event_read_fields(event, &event->format.fields);
	if (ret < 0)
		return ret;
	event->format.nr_fields = ret;

	return 0;

 fail:
	free_token(token);
	return -1;
}

enum event_type
process_arg_token(struct event *event, struct print_arg *arg,
		  char **tok, enum event_type type);

static enum event_type
process_arg(struct event *event, struct print_arg *arg, char **tok)
{
	enum event_type type;
	char *token;

	type = read_token(&token);
	*tok = token;

	return process_arg_token(event, arg, tok, type);
}

static enum event_type
process_cond(struct event *event, struct print_arg *top, char **tok)
{
	struct print_arg *arg, *left, *right;
	enum event_type type;
	char *token = NULL;

	arg = malloc_or_die(sizeof(*arg));
	memset(arg, 0, sizeof(*arg));

	left = malloc_or_die(sizeof(*left));

	right = malloc_or_die(sizeof(*right));

	arg->type = PRINT_OP;
	arg->op.left = left;
	arg->op.right = right;

	*tok = NULL;
	type = process_arg(event, left, &token);
	if (test_type_token(type, token, EVENT_OP, ":"))
		goto out_free;

	arg->op.op = token;

	type = process_arg(event, right, &token);

	top->op.right = arg;

	*tok = token;
	return type;

out_free:
	free_token(*tok);
	free(right);
	free(left);
	free_arg(arg);
	return EVENT_ERROR;
}

static enum event_type
process_array(struct event *event, struct print_arg *top, char **tok)
{
	struct print_arg *arg;
	enum event_type type;
	char *token = NULL;

	arg = malloc_or_die(sizeof(*arg));
	memset(arg, 0, sizeof(*arg));

	*tok = NULL;
	type = process_arg(event, arg, &token);
	if (test_type_token(type, token, EVENT_OP, "]"))
		goto out_free;

	top->op.right = arg;

	free_token(token);
	type = read_token_item(&token);
	*tok = token;

	return type;

out_free:
	free_token(*tok);
	free_arg(arg);
	return EVENT_ERROR;
}

static int get_op_prio(char *op)
{
	if (!op[1]) {
		switch (op[0]) {
		case '*':
		case '/':
		case '%':
			return 6;
		case '+':
		case '-':
			return 7;
			/* '>>' and '<<' are 8 */
		case '<':
		case '>':
			return 9;
			/* '==' and '!=' are 10 */
		case '&':
			return 11;
		case '^':
			return 12;
		case '|':
			return 13;
		case '?':
			return 16;
		default:
			die("unknown op '%c'", op[0]);
			return -1;
		}
	} else {
		if (strcmp(op, "++") == 0 ||
		    strcmp(op, "--") == 0) {
			return 3;
		} else if (strcmp(op, ">>") == 0 ||
			   strcmp(op, "<<") == 0) {
			return 8;
		} else if (strcmp(op, ">=") == 0 ||
			   strcmp(op, "<=") == 0) {
			return 9;
		} else if (strcmp(op, "==") == 0 ||
			   strcmp(op, "!=") == 0) {
			return 10;
		} else if (strcmp(op, "&&") == 0) {
			return 14;
		} else if (strcmp(op, "||") == 0) {
			return 15;
		} else {
			die("unknown op '%s'", op);
			return -1;
		}
	}
}

static void set_op_prio(struct print_arg *arg)
{

	/* single ops are the greatest */
	if (!arg->op.left || arg->op.left->type == PRINT_NULL) {
		arg->op.prio = 0;
		return;
	}

	arg->op.prio = get_op_prio(arg->op.op);
}

static enum event_type
process_op(struct event *event, struct print_arg *arg, char **tok)
{
	struct print_arg *left, *right = NULL;
	enum event_type type;
	char *token;

	/* the op is passed in via tok */
	token = *tok;

	if (arg->type == PRINT_OP && !arg->op.left) {
		/* handle single op */
		if (token[1]) {
			die("bad op token %s", token);
			return EVENT_ERROR;
		}
		switch (token[0]) {
		case '!':
		case '+':
		case '-':
			break;
		default:
			die("bad op token %s", token);
			return EVENT_ERROR;
		}

		/* make an empty left */
		left = malloc_or_die(sizeof(*left));
		left->type = PRINT_NULL;
		arg->op.left = left;

		right = malloc_or_die(sizeof(*right));
		arg->op.right = right;

		type = process_arg(event, right, tok);

	} else if (strcmp(token, "?") == 0) {

		left = malloc_or_die(sizeof(*left));
		/* copy the top arg to the left */
		*left = *arg;

		arg->type = PRINT_OP;
		arg->op.op = token;
		arg->op.left = left;
		arg->op.prio = 0;

		type = process_cond(event, arg, tok);

	} else if (strcmp(token, ">>") == 0 ||
		   strcmp(token, "<<") == 0 ||
		   strcmp(token, "&") == 0 ||
		   strcmp(token, "|") == 0 ||
		   strcmp(token, "&&") == 0 ||
		   strcmp(token, "||") == 0 ||
		   strcmp(token, "-") == 0 ||
		   strcmp(token, "+") == 0 ||
		   strcmp(token, "*") == 0 ||
		   strcmp(token, "^") == 0 ||
		   strcmp(token, "/") == 0 ||
		   strcmp(token, "<") == 0 ||
		   strcmp(token, ">") == 0 ||
		   strcmp(token, "==") == 0 ||
		   strcmp(token, "!=") == 0) {

		left = malloc_or_die(sizeof(*left));

		/* copy the top arg to the left */
		*left = *arg;

		arg->type = PRINT_OP;
		arg->op.op = token;
		arg->op.left = left;

		set_op_prio(arg);

		right = malloc_or_die(sizeof(*right));

		type = read_token_item(&token);
		*tok = token;

		/* could just be a type pointer */
		if ((strcmp(arg->op.op, "*") == 0) &&
		    type == EVENT_DELIM && (strcmp(token, ")") == 0)) {
			if (left->type != PRINT_ATOM)
				die("bad pointer type");
			left->atom.atom = realloc(left->atom.atom,
					    sizeof(left->atom.atom) + 3);
			strcat(left->atom.atom, " *");
			*arg = *left;
			free(arg);

			return type;
		}

		type = process_arg_token(event, right, tok, type);

		arg->op.right = right;

	} else if (strcmp(token, "[") == 0) {

		left = malloc_or_die(sizeof(*left));
		*left = *arg;

		arg->type = PRINT_OP;
		arg->op.op = token;
		arg->op.left = left;

		arg->op.prio = 0;
		type = process_array(event, arg, tok);

	} else {
		warning("unknown op '%s'", token);
		event->flags |= EVENT_FL_FAILED;
		/* the arg is now the left side */
		return EVENT_NONE;
	}

	if (type == EVENT_OP) {
		int prio;

		/* higher prios need to be closer to the root */
		prio = get_op_prio(*tok);

		if (prio > arg->op.prio)
			return process_op(event, arg, tok);

		return process_op(event, right, tok);
	}

	return type;
}

static enum event_type
process_entry(struct event *event __unused, struct print_arg *arg,
	      char **tok)
{
	enum event_type type;
	char *field;
	char *token;

	if (read_expected(EVENT_OP, "->") < 0)
		return EVENT_ERROR;

	if (read_expect_type(EVENT_ITEM, &token) < 0)
		goto fail;
	field = token;

	arg->type = PRINT_FIELD;
	arg->field.name = field;

	if (is_flag_field) {
		arg->field.field = find_any_field(event, arg->field.name);
		arg->field.field->flags |= FIELD_IS_FLAG;
		is_flag_field = 0;
	} else if (is_symbolic_field) {
		arg->field.field = find_any_field(event, arg->field.name);
		arg->field.field->flags |= FIELD_IS_SYMBOLIC;
		is_symbolic_field = 0;
	}

	type = read_token(&token);
	*tok = token;

	return type;

fail:
	free_token(token);
	return EVENT_ERROR;
}

static char *arg_eval (struct print_arg *arg);

static long long arg_num_eval(struct print_arg *arg)
{
	long long left, right;
	long long val = 0;

	switch (arg->type) {
	case PRINT_ATOM:
		val = strtoll(arg->atom.atom, NULL, 0);
		break;
	case PRINT_TYPE:
		val = arg_num_eval(arg->typecast.item);
		break;
	case PRINT_OP:
		switch (arg->op.op[0]) {
		case '|':
			left = arg_num_eval(arg->op.left);
			right = arg_num_eval(arg->op.right);
			if (arg->op.op[1])
				val = left || right;
			else
				val = left | right;
			break;
		case '&':
			left = arg_num_eval(arg->op.left);
			right = arg_num_eval(arg->op.right);
			if (arg->op.op[1])
				val = left && right;
			else
				val = left & right;
			break;
		case '<':
			left = arg_num_eval(arg->op.left);
			right = arg_num_eval(arg->op.right);
			switch (arg->op.op[1]) {
			case 0:
				val = left < right;
				break;
			case '<':
				val = left << right;
				break;
			case '=':
				val = left <= right;
				break;
			default:
				die("unknown op '%s'", arg->op.op);
			}
			break;
		case '>':
			left = arg_num_eval(arg->op.left);
			right = arg_num_eval(arg->op.right);
			switch (arg->op.op[1]) {
			case 0:
				val = left > right;
				break;
			case '>':
				val = left >> right;
				break;
			case '=':
				val = left >= right;
				break;
			default:
				die("unknown op '%s'", arg->op.op);
			}
			break;
		case '=':
			left = arg_num_eval(arg->op.left);
			right = arg_num_eval(arg->op.right);

			if (arg->op.op[1] != '=')
				die("unknown op '%s'", arg->op.op);

			val = left == right;
			break;
		case '!':
			left = arg_num_eval(arg->op.left);
			right = arg_num_eval(arg->op.right);

			switch (arg->op.op[1]) {
			case '=':
				val = left != right;
				break;
			default:
				die("unknown op '%s'", arg->op.op);
			}
			break;
		default:
			die("unknown op '%s'", arg->op.op);
		}
		break;

	case PRINT_NULL:
	case PRINT_FIELD ... PRINT_SYMBOL:
	case PRINT_STRING:
	default:
		die("invalid eval type %d", arg->type);

	}
	return val;
}

static char *arg_eval (struct print_arg *arg)
{
	long long val;
	static char buf[20];

	switch (arg->type) {
	case PRINT_ATOM:
		return arg->atom.atom;
	case PRINT_TYPE:
		return arg_eval(arg->typecast.item);
	case PRINT_OP:
		val = arg_num_eval(arg);
		sprintf(buf, "%lld", val);
		return buf;

	case PRINT_NULL:
	case PRINT_FIELD ... PRINT_SYMBOL:
	case PRINT_STRING:
	default:
		die("invalid eval type %d", arg->type);
		break;
	}

	return NULL;
}

static enum event_type
process_fields(struct event *event, struct print_flag_sym **list, char **tok)
{
	enum event_type type;
	struct print_arg *arg = NULL;
	struct print_flag_sym *field;
	char *token = NULL;
	char *value;

	do {
		free_token(token);
		type = read_token_item(&token);
		if (test_type_token(type, token, EVENT_OP, "{"))
			break;

		arg = malloc_or_die(sizeof(*arg));

		free_token(token);
		type = process_arg(event, arg, &token);
		if (test_type_token(type, token, EVENT_DELIM, ","))
			goto out_free;

		field = malloc_or_die(sizeof(*field));
		memset(field, 0, sizeof(*field));

		value = arg_eval(arg);
		field->value = strdup(value);

		free_token(token);
		type = process_arg(event, arg, &token);
		if (test_type_token(type, token, EVENT_OP, "}"))
			goto out_free;

		value = arg_eval(arg);
		field->str = strdup(value);
		free_arg(arg);
		arg = NULL;

		*list = field;
		list = &field->next;

		free_token(token);
		type = read_token_item(&token);
	} while (type == EVENT_DELIM && strcmp(token, ",") == 0);

	*tok = token;
	return type;

out_free:
	free_arg(arg);
	free_token(token);

	return EVENT_ERROR;
}

static enum event_type
process_flags(struct event *event, struct print_arg *arg, char **tok)
{
	struct print_arg *field;
	enum event_type type;
	char *token;

	memset(arg, 0, sizeof(*arg));
	arg->type = PRINT_FLAGS;

	if (read_expected_item(EVENT_DELIM, "(") < 0)
		return EVENT_ERROR;

	field = malloc_or_die(sizeof(*field));

	type = process_arg(event, field, &token);
	if (test_type_token(type, token, EVENT_DELIM, ","))
		goto out_free;

	arg->flags.field = field;

	type = read_token_item(&token);
	if (event_item_type(type)) {
		arg->flags.delim = token;
		type = read_token_item(&token);
	}

	if (test_type_token(type, token, EVENT_DELIM, ","))
		goto out_free;

	type = process_fields(event, &arg->flags.flags, &token);
	if (test_type_token(type, token, EVENT_DELIM, ")"))
		goto out_free;

	free_token(token);
	type = read_token_item(tok);
	return type;

out_free:
	free_token(token);
	return EVENT_ERROR;
}

static enum event_type
process_symbols(struct event *event, struct print_arg *arg, char **tok)
{
	struct print_arg *field;
	enum event_type type;
	char *token;

	memset(arg, 0, sizeof(*arg));
	arg->type = PRINT_SYMBOL;

	if (read_expected_item(EVENT_DELIM, "(") < 0)
		return EVENT_ERROR;

	field = malloc_or_die(sizeof(*field));

	type = process_arg(event, field, &token);
	if (test_type_token(type, token, EVENT_DELIM, ","))
		goto out_free;

	arg->symbol.field = field;

	type = process_fields(event, &arg->symbol.symbols, &token);
	if (test_type_token(type, token, EVENT_DELIM, ")"))
		goto out_free;

	free_token(token);
	type = read_token_item(tok);
	return type;

out_free:
	free_token(token);
	return EVENT_ERROR;
}

static enum event_type
process_paren(struct event *event, struct print_arg *arg, char **tok)
{
	struct print_arg *item_arg;
	enum event_type type;
	char *token;

	type = process_arg(event, arg, &token);

	if (type == EVENT_ERROR)
		return EVENT_ERROR;

	if (type == EVENT_OP)
		type = process_op(event, arg, &token);

	if (type == EVENT_ERROR)
		return EVENT_ERROR;

	if (test_type_token(type, token, EVENT_DELIM, ")")) {
		free_token(token);
		return EVENT_ERROR;
	}

	free_token(token);
	type = read_token_item(&token);

	/*
	 * If the next token is an item or another open paren, then
	 * this was a typecast.
	 */
	if (event_item_type(type) ||
	    (type == EVENT_DELIM && strcmp(token, "(") == 0)) {

		/* make this a typecast and contine */

		/* prevous must be an atom */
		if (arg->type != PRINT_ATOM)
			die("previous needed to be PRINT_ATOM");

		item_arg = malloc_or_die(sizeof(*item_arg));

		arg->type = PRINT_TYPE;
		arg->typecast.type = arg->atom.atom;
		arg->typecast.item = item_arg;
		type = process_arg_token(event, item_arg, &token, type);

	}

	*tok = token;
	return type;
}


static enum event_type
process_str(struct event *event __unused, struct print_arg *arg, char **tok)
{
	enum event_type type;
	char *token;

	if (read_expected(EVENT_DELIM, "(") < 0)
		return EVENT_ERROR;

	if (read_expect_type(EVENT_ITEM, &token) < 0)
		goto fail;

	arg->type = PRINT_STRING;
	arg->string.string = token;
	arg->string.offset = -1;

	if (read_expected(EVENT_DELIM, ")") < 0)
		return EVENT_ERROR;

	type = read_token(&token);
	*tok = token;

	return type;
fail:
	free_token(token);
	return EVENT_ERROR;
}

enum event_type
process_arg_token(struct event *event, struct print_arg *arg,
		  char **tok, enum event_type type)
{
	char *token;
	char *atom;

	token = *tok;

	switch (type) {
	case EVENT_ITEM:
		if (strcmp(token, "REC") == 0) {
			free_token(token);
			type = process_entry(event, arg, &token);
		} else if (strcmp(token, "__print_flags") == 0) {
			free_token(token);
			is_flag_field = 1;
			type = process_flags(event, arg, &token);
		} else if (strcmp(token, "__print_symbolic") == 0) {
			free_token(token);
			is_symbolic_field = 1;
			type = process_symbols(event, arg, &token);
		} else if (strcmp(token, "__get_str") == 0) {
			free_token(token);
			type = process_str(event, arg, &token);
		} else {
			atom = token;
			/* test the next token */
			type = read_token_item(&token);

			/* atoms can be more than one token long */
			while (type == EVENT_ITEM) {
				atom = realloc(atom, strlen(atom) + strlen(token) + 2);
				strcat(atom, " ");
				strcat(atom, token);
				free_token(token);
				type = read_token_item(&token);
			}

			/* todo, test for function */

			arg->type = PRINT_ATOM;
			arg->atom.atom = atom;
		}
		break;
	case EVENT_DQUOTE:
	case EVENT_SQUOTE:
		arg->type = PRINT_ATOM;
		arg->atom.atom = token;
		type = read_token_item(&token);
		break;
	case EVENT_DELIM:
		if (strcmp(token, "(") == 0) {
			free_token(token);
			type = process_paren(event, arg, &token);
			break;
		}
	case EVENT_OP:
		/* handle single ops */
		arg->type = PRINT_OP;
		arg->op.op = token;
		arg->op.left = NULL;
		type = process_op(event, arg, &token);

		break;

	case EVENT_ERROR ... EVENT_NEWLINE:
	default:
		die("unexpected type %d", type);
	}
	*tok = token;

	return type;
}

static int event_read_print_args(struct event *event, struct print_arg **list)
{
	enum event_type type = EVENT_ERROR;
	struct print_arg *arg;
	char *token;
	int args = 0;

	do {
		if (type == EVENT_NEWLINE) {
			free_token(token);
			type = read_token_item(&token);
			continue;
		}

		arg = malloc_or_die(sizeof(*arg));
		memset(arg, 0, sizeof(*arg));

		type = process_arg(event, arg, &token);

		if (type == EVENT_ERROR) {
			free_arg(arg);
			return -1;
		}

		*list = arg;
		args++;

		if (type == EVENT_OP) {
			type = process_op(event, arg, &token);
			list = &arg->next;
			continue;
		}

		if (type == EVENT_DELIM && strcmp(token, ",") == 0) {
			free_token(token);
			*list = arg;
			list = &arg->next;
			continue;
		}
		break;
	} while (type != EVENT_NONE);

	if (type != EVENT_NONE)
		free_token(token);

	return args;
}

static int event_read_print(struct event *event)
{
	enum event_type type;
	char *token;
	int ret;

	if (read_expected_item(EVENT_ITEM, "print") < 0)
		return -1;

	if (read_expected(EVENT_ITEM, "fmt") < 0)
		return -1;

	if (read_expected(EVENT_OP, ":") < 0)
		return -1;

	if (read_expect_type(EVENT_DQUOTE, &token) < 0)
		goto fail;

 concat:
	event->print_fmt.format = token;
	event->print_fmt.args = NULL;

	/* ok to have no arg */
	type = read_token_item(&token);

	if (type == EVENT_NONE)
		return 0;

	/* Handle concatination of print lines */
	if (type == EVENT_DQUOTE) {
		char *cat;

		cat = malloc_or_die(strlen(event->print_fmt.format) +
				    strlen(token) + 1);
		strcpy(cat, event->print_fmt.format);
		strcat(cat, token);
		free_token(token);
		free_token(event->print_fmt.format);
		event->print_fmt.format = NULL;
		token = cat;
		goto concat;
	}

	if (test_type_token(type, token, EVENT_DELIM, ","))
		goto fail;

	free_token(token);

	ret = event_read_print_args(event, &event->print_fmt.args);
	if (ret < 0)
		return -1;

	return ret;

 fail:
	free_token(token);
	return -1;
}

static struct format_field *
find_common_field(struct event *event, const char *name)
{
	struct format_field *format;

	for (format = event->format.common_fields;
	     format; format = format->next) {
		if (strcmp(format->name, name) == 0)
			break;
	}

	return format;
}

static struct format_field *
find_field(struct event *event, const char *name)
{
	struct format_field *format;

	for (format = event->format.fields;
	     format; format = format->next) {
		if (strcmp(format->name, name) == 0)
			break;
	}

	return format;
}

static struct format_field *
find_any_field(struct event *event, const char *name)
{
	struct format_field *format;

	format = find_common_field(event, name);
	if (format)
		return format;
	return find_field(event, name);
}

unsigned long long read_size(void *ptr, int size)
{
	switch (size) {
	case 1:
		return *(unsigned char *)ptr;
	case 2:
		return data2host2(ptr);
	case 4:
		return data2host4(ptr);
	case 8:
		return data2host8(ptr);
	default:
		/* BUG! */
		return 0;
	}
}

unsigned long long
raw_field_value(struct event *event, const char *name, void *data)
{
	struct format_field *field;

	field = find_any_field(event, name);
	if (!field)
		return 0ULL;

	return read_size(data + field->offset, field->size);
}

void *raw_field_ptr(struct event *event, const char *name, void *data)
{
	struct format_field *field;

	field = find_any_field(event, name);
	if (!field)
		return NULL;

	if (field->flags & FIELD_IS_DYNAMIC) {
		int offset;

		offset = *(int *)(data + field->offset);
		offset &= 0xffff;

		return data + offset;
	}

	return data + field->offset;
}

static int get_common_info(const char *type, int *offset, int *size)
{
	struct event *event;
	struct format_field *field;

	/*
	 * All events should have the same common elements.
	 * Pick any event to find where the type is;
	 */
	if (!event_list)
		die("no event_list!");

	event = event_list;
	field = find_common_field(event, type);
	if (!field)
		die("field '%s' not found", type);

	*offset = field->offset;
	*size = field->size;

	return 0;
}

static int __parse_common(void *data, int *size, int *offset,
			  const char *name)
{
	int ret;

	if (!*size) {
		ret = get_common_info(name, offset, size);
		if (ret < 0)
			return ret;
	}
	return read_size(data + *offset, *size);
}

int trace_parse_common_type(void *data)
{
	static int type_offset;
	static int type_size;

	return __parse_common(data, &type_size, &type_offset,
			      "common_type");
}

int trace_parse_common_pid(void *data)
{
	static int pid_offset;
	static int pid_size;

	return __parse_common(data, &pid_size, &pid_offset,
			      "common_pid");
}

int parse_common_pc(void *data)
{
	static int pc_offset;
	static int pc_size;

	return __parse_common(data, &pc_size, &pc_offset,
			      "common_preempt_count");
}

int parse_common_flags(void *data)
{
	static int flags_offset;
	static int flags_size;

	return __parse_common(data, &flags_size, &flags_offset,
			      "common_flags");
}

int parse_common_lock_depth(void *data)
{
	static int ld_offset;
	static int ld_size;
	int ret;

	ret = __parse_common(data, &ld_size, &ld_offset,
			     "common_lock_depth");
	if (ret < 0)
		return -1;

	return ret;
}

struct event *trace_find_event(int id)
{
	struct event *event;

	for (event = event_list; event; event = event->next) {
		if (event->id == id)
			break;
	}
	return event;
}

struct event *trace_find_next_event(struct event *event)
{
	if (!event)
		return event_list;

	return event->next;
}

static unsigned long long eval_num_arg(void *data, int size,
				   struct event *event, struct print_arg *arg)
{
	unsigned long long val = 0;
	unsigned long long left, right;
	struct print_arg *larg;

	switch (arg->type) {
	case PRINT_NULL:
		/* ?? */
		return 0;
	case PRINT_ATOM:
		return strtoull(arg->atom.atom, NULL, 0);
	case PRINT_FIELD:
		if (!arg->field.field) {
			arg->field.field = find_any_field(event, arg->field.name);
			if (!arg->field.field)
				die("field %s not found", arg->field.name);
		}
		/* must be a number */
		val = read_size(data + arg->field.field->offset,
				arg->field.field->size);
		break;
	case PRINT_FLAGS:
	case PRINT_SYMBOL:
		break;
	case PRINT_TYPE:
		return eval_num_arg(data, size, event, arg->typecast.item);
	case PRINT_STRING:
		return 0;
		break;
	case PRINT_OP:
		if (strcmp(arg->op.op, "[") == 0) {
			/*
			 * Arrays are special, since we don't want
			 * to read the arg as is.
			 */
			if (arg->op.left->type != PRINT_FIELD)
				goto default_op; /* oops, all bets off */
			larg = arg->op.left;
			if (!larg->field.field) {
				larg->field.field =
					find_any_field(event, larg->field.name);
				if (!larg->field.field)
					die("field %s not found", larg->field.name);
			}
			right = eval_num_arg(data, size, event, arg->op.right);
			val = read_size(data + larg->field.field->offset +
					right * long_size, long_size);
			break;
		}
 default_op:
		left = eval_num_arg(data, size, event, arg->op.left);
		right = eval_num_arg(data, size, event, arg->op.right);
		switch (arg->op.op[0]) {
		case '|':
			if (arg->op.op[1])
				val = left || right;
			else
				val = left | right;
			break;
		case '&':
			if (arg->op.op[1])
				val = left && right;
			else
				val = left & right;
			break;
		case '<':
			switch (arg->op.op[1]) {
			case 0:
				val = left < right;
				break;
			case '<':
				val = left << right;
				break;
			case '=':
				val = left <= right;
				break;
			default:
				die("unknown op '%s'", arg->op.op);
			}
			break;
		case '>':
			switch (arg->op.op[1]) {
			case 0:
				val = left > right;
				break;
			case '>':
				val = left >> right;
				break;
			case '=':
				val = left >= right;
				break;
			default:
				die("unknown op '%s'", arg->op.op);
			}
			break;
		case '=':
			if (arg->op.op[1] != '=')
				die("unknown op '%s'", arg->op.op);
			val = left == right;
			break;
		case '-':
			val = left - right;
			break;
		case '+':
			val = left + right;
			break;
		default:
			die("unknown op '%s'", arg->op.op);
		}
		break;
	default: /* not sure what to do there */
		return 0;
	}
	return val;
}

struct flag {
	const char *name;
	unsigned long long value;
};

static const struct flag flags[] = {
	{ "HI_SOFTIRQ", 0 },
	{ "TIMER_SOFTIRQ", 1 },
	{ "NET_TX_SOFTIRQ", 2 },
	{ "NET_RX_SOFTIRQ", 3 },
	{ "BLOCK_SOFTIRQ", 4 },
	{ "BLOCK_IOPOLL_SOFTIRQ", 5 },
	{ "TASKLET_SOFTIRQ", 6 },
	{ "SCHED_SOFTIRQ", 7 },
	{ "HRTIMER_SOFTIRQ", 8 },
	{ "RCU_SOFTIRQ", 9 },

	{ "HRTIMER_NORESTART", 0 },
	{ "HRTIMER_RESTART", 1 },
};

unsigned long long eval_flag(const char *flag)
{
	int i;

	/*
	 * Some flags in the format files do not get converted.
	 * If the flag is not numeric, see if it is something that
	 * we already know about.
	 */
	if (isdigit(flag[0]))
		return strtoull(flag, NULL, 0);

	for (i = 0; i < (int)(sizeof(flags)/sizeof(flags[0])); i++)
		if (strcmp(flags[i].name, flag) == 0)
			return flags[i].value;

	return 0;
}

static void print_str_arg(void *data, int size,
			  struct event *event, struct print_arg *arg)
{
	struct print_flag_sym *flag;
	unsigned long long val, fval;
	char *str;
	int print;

	switch (arg->type) {
	case PRINT_NULL:
		/* ?? */
		return;
	case PRINT_ATOM:
		printf("%s", arg->atom.atom);
		return;
	case PRINT_FIELD:
		if (!arg->field.field) {
			arg->field.field = find_any_field(event, arg->field.name);
			if (!arg->field.field)
				die("field %s not found", arg->field.name);
		}
		str = malloc_or_die(arg->field.field->size + 1);
		memcpy(str, data + arg->field.field->offset,
		       arg->field.field->size);
		str[arg->field.field->size] = 0;
		printf("%s", str);
		free(str);
		break;
	case PRINT_FLAGS:
		val = eval_num_arg(data, size, event, arg->flags.field);
		print = 0;
		for (flag = arg->flags.flags; flag; flag = flag->next) {
			fval = eval_flag(flag->value);
			if (!val && !fval) {
				printf("%s", flag->str);
				break;
			}
			if (fval && (val & fval) == fval) {
				if (print && arg->flags.delim)
					printf("%s", arg->flags.delim);
				printf("%s", flag->str);
				print = 1;
				val &= ~fval;
			}
		}
		break;
	case PRINT_SYMBOL:
		val = eval_num_arg(data, size, event, arg->symbol.field);
		for (flag = arg->symbol.symbols; flag; flag = flag->next) {
			fval = eval_flag(flag->value);
			if (val == fval) {
				printf("%s", flag->str);
				break;
			}
		}
		break;

	case PRINT_TYPE:
		break;
	case PRINT_STRING: {
		int str_offset;

		if (arg->string.offset == -1) {
			struct format_field *f;

			f = find_any_field(event, arg->string.string);
			arg->string.offset = f->offset;
		}
		str_offset = *(int *)(data + arg->string.offset);
		str_offset &= 0xffff;
		printf("%s", ((char *)data) + str_offset);
		break;
	}
	case PRINT_OP:
		/*
		 * The only op for string should be ? :
		 */
		if (arg->op.op[0] != '?')
			return;
		val = eval_num_arg(data, size, event, arg->op.left);
		if (val)
			print_str_arg(data, size, event, arg->op.right->op.left);
		else
			print_str_arg(data, size, event, arg->op.right->op.right);
		break;
	default:
		/* well... */
		break;
	}
}

static struct print_arg *make_bprint_args(char *fmt, void *data, int size, struct event *event)
{
	static struct format_field *field, *ip_field;
	struct print_arg *args, *arg, **next;
	unsigned long long ip, val;
	char *ptr;
	void *bptr;

	if (!field) {
		field = find_field(event, "buf");
		if (!field)
			die("can't find buffer field for binary printk");
		ip_field = find_field(event, "ip");
		if (!ip_field)
			die("can't find ip field for binary printk");
	}

	ip = read_size(data + ip_field->offset, ip_field->size);

	/*
	 * The first arg is the IP pointer.
	 */
	args = malloc_or_die(sizeof(*args));
	arg = args;
	arg->next = NULL;
	next = &arg->next;

	arg->type = PRINT_ATOM;
	arg->atom.atom = malloc_or_die(32);
	sprintf(arg->atom.atom, "%lld", ip);

	/* skip the first "%pf : " */
	for (ptr = fmt + 6, bptr = data + field->offset;
	     bptr < data + size && *ptr; ptr++) {
		int ls = 0;

		if (*ptr == '%') {
 process_again:
			ptr++;
			switch (*ptr) {
			case '%':
				break;
			case 'l':
				ls++;
				goto process_again;
			case 'L':
				ls = 2;
				goto process_again;
			case '0' ... '9':
				goto process_again;
			case 'p':
				ls = 1;
				/* fall through */
			case 'd':
			case 'u':
			case 'x':
			case 'i':
				/* the pointers are always 4 bytes aligned */
				bptr = (void *)(((unsigned long)bptr + 3) &
						~3);
				switch (ls) {
				case 0:
				case 1:
					ls = long_size;
					break;
				case 2:
					ls = 8;
				default:
					break;
				}
				val = read_size(bptr, ls);
				bptr += ls;
				arg = malloc_or_die(sizeof(*arg));
				arg->next = NULL;
				arg->type = PRINT_ATOM;
				arg->atom.atom = malloc_or_die(32);
				sprintf(arg->atom.atom, "%lld", val);
				*next = arg;
				next = &arg->next;
				break;
			case 's':
				arg = malloc_or_die(sizeof(*arg));
				arg->next = NULL;
				arg->type = PRINT_STRING;
				arg->string.string = strdup(bptr);
				bptr += strlen(bptr) + 1;
				*next = arg;
				next = &arg->next;
			default:
				break;
			}
		}
	}

	return args;
}

static void free_args(struct print_arg *args)
{
	struct print_arg *next;

	while (args) {
		next = args->next;

		if (args->type == PRINT_ATOM)
			free(args->atom.atom);
		else
			free(args->string.string);
		free(args);
		args = next;
	}
}

static char *get_bprint_format(void *data, int size __unused, struct event *event)
{
	unsigned long long addr;
	static struct format_field *field;
	struct printk_map *printk;
	char *format;
	char *p;

	if (!field) {
		field = find_field(event, "fmt");
		if (!field)
			die("can't find format field for binary printk");
		printf("field->offset = %d size=%d\n", field->offset, field->size);
	}

	addr = read_size(data + field->offset, field->size);

	printk = find_printk(addr);
	if (!printk) {
		format = malloc_or_die(45);
		sprintf(format, "%%pf : (NO FORMAT FOUND at %llx)\n",
			addr);
		return format;
	}

	p = printk->printk;
	/* Remove any quotes. */
	if (*p == '"')
		p++;
	format = malloc_or_die(strlen(p) + 10);
	sprintf(format, "%s : %s", "%pf", p);
	/* remove ending quotes and new line since we will add one too */
	p = format + strlen(format) - 1;
	if (*p == '"')
		*p = 0;

	p -= 2;
	if (strcmp(p, "\\n") == 0)
		*p = 0;

	return format;
}

static void pretty_print(void *data, int size, struct event *event)
{
	struct print_fmt *print_fmt = &event->print_fmt;
	struct print_arg *arg = print_fmt->args;
	struct print_arg *args = NULL;
	const char *ptr = print_fmt->format;
	unsigned long long val;
	struct func_map *func;
	const char *saveptr;
	char *bprint_fmt = NULL;
	char format[32];
	int show_func;
	int len;
	int ls;

	if (event->flags & EVENT_FL_ISFUNC)
		ptr = " %pF <-- %pF";

	if (event->flags & EVENT_FL_ISBPRINT) {
		bprint_fmt = get_bprint_format(data, size, event);
		args = make_bprint_args(bprint_fmt, data, size, event);
		arg = args;
		ptr = bprint_fmt;
	}

	for (; *ptr; ptr++) {
		ls = 0;
		if (*ptr == '\\') {
			ptr++;
			switch (*ptr) {
			case 'n':
				printf("\n");
				break;
			case 't':
				printf("\t");
				break;
			case 'r':
				printf("\r");
				break;
			case '\\':
				printf("\\");
				break;
			default:
				printf("%c", *ptr);
				break;
			}

		} else if (*ptr == '%') {
			saveptr = ptr;
			show_func = 0;
 cont_process:
			ptr++;
			switch (*ptr) {
			case '%':
				printf("%%");
				break;
			case 'l':
				ls++;
				goto cont_process;
			case 'L':
				ls = 2;
				goto cont_process;
			case 'z':
			case 'Z':
			case '0' ... '9':
				goto cont_process;
			case 'p':
				if (long_size == 4)
					ls = 1;
				else
					ls = 2;

				if (*(ptr+1) == 'F' ||
				    *(ptr+1) == 'f') {
					ptr++;
					show_func = *ptr;
				}

				/* fall through */
			case 'd':
			case 'i':
			case 'x':
			case 'X':
			case 'u':
				if (!arg)
					die("no argument match");

				len = ((unsigned long)ptr + 1) -
					(unsigned long)saveptr;

				/* should never happen */
				if (len > 32)
					die("bad format!");

				memcpy(format, saveptr, len);
				format[len] = 0;

				val = eval_num_arg(data, size, event, arg);
				arg = arg->next;

				if (show_func) {
					func = find_func(val);
					if (func) {
						printf("%s", func->func);
						if (show_func == 'F')
							printf("+0x%llx",
							       val - func->addr);
						break;
					}
				}
				switch (ls) {
				case 0:
					printf(format, (int)val);
					break;
				case 1:
					printf(format, (long)val);
					break;
				case 2:
					printf(format, (long long)val);
					break;
				default:
					die("bad count (%d)", ls);
				}
				break;
			case 's':
				if (!arg)
					die("no matching argument");

				print_str_arg(data, size, event, arg);
				arg = arg->next;
				break;
			default:
				printf(">%c<", *ptr);

			}
		} else
			printf("%c", *ptr);
	}

	if (args) {
		free_args(args);
		free(bprint_fmt);
	}
}

static inline int log10_cpu(int nb)
{
	if (nb / 100)
		return 3;
	if (nb / 10)
		return 2;
	return 1;
}

static void print_lat_fmt(void *data, int size __unused)
{
	unsigned int lat_flags;
	unsigned int pc;
	int lock_depth;
	int hardirq;
	int softirq;

	lat_flags = parse_common_flags(data);
	pc = parse_common_pc(data);
	lock_depth = parse_common_lock_depth(data);

	hardirq = lat_flags & TRACE_FLAG_HARDIRQ;
	softirq = lat_flags & TRACE_FLAG_SOFTIRQ;

	printf("%c%c%c",
	       (lat_flags & TRACE_FLAG_IRQS_OFF) ? 'd' :
	       (lat_flags & TRACE_FLAG_IRQS_NOSUPPORT) ?
	       'X' : '.',
	       (lat_flags & TRACE_FLAG_NEED_RESCHED) ?
	       'N' : '.',
	       (hardirq && softirq) ? 'H' :
	       hardirq ? 'h' : softirq ? 's' : '.');

	if (pc)
		printf("%x", pc);
	else
		printf(".");

	if (lock_depth < 0)
		printf(".");
	else
		printf("%d", lock_depth);
}

/* taken from Linux, written by Frederic Weisbecker */
static void print_graph_cpu(int cpu)
{
	int i;
	int log10_this = log10_cpu(cpu);
	int log10_all = log10_cpu(cpus);


	/*
	 * Start with a space character - to make it stand out
	 * to the right a bit when trace output is pasted into
	 * email:
	 */
	printf(" ");

	/*
	 * Tricky - we space the CPU field according to the max
	 * number of online CPUs. On a 2-cpu system it would take
	 * a maximum of 1 digit - on a 128 cpu system it would
	 * take up to 3 digits:
	 */
	for (i = 0; i < log10_all - log10_this; i++)
		printf(" ");

	printf("%d) ", cpu);
}

#define TRACE_GRAPH_PROCINFO_LENGTH	14
#define TRACE_GRAPH_INDENT	2

static void print_graph_proc(int pid, const char *comm)
{
	/* sign + log10(MAX_INT) + '\0' */
	char pid_str[11];
	int spaces = 0;
	int len;
	int i;

	sprintf(pid_str, "%d", pid);

	/* 1 stands for the "-" character */
	len = strlen(comm) + strlen(pid_str) + 1;

	if (len < TRACE_GRAPH_PROCINFO_LENGTH)
		spaces = TRACE_GRAPH_PROCINFO_LENGTH - len;

	/* First spaces to align center */
	for (i = 0; i < spaces / 2; i++)
		printf(" ");

	printf("%s-%s", comm, pid_str);

	/* Last spaces to align center */
	for (i = 0; i < spaces - (spaces / 2); i++)
		printf(" ");
}

static struct record *
get_return_for_leaf(int cpu, int cur_pid, unsigned long long cur_func,
		    struct record *next)
{
	struct format_field *field;
	struct event *event;
	unsigned long val;
	int type;
	int pid;

	type = trace_parse_common_type(next->data);
	event = trace_find_event(type);
	if (!event)
		return NULL;

	if (!(event->flags & EVENT_FL_ISFUNCRET))
		return NULL;

	pid = trace_parse_common_pid(next->data);
	field = find_field(event, "func");
	if (!field)
		die("function return does not have field func");

	val = read_size(next->data + field->offset, field->size);

	if (cur_pid != pid || cur_func != val)
		return NULL;

	/* this is a leaf, now advance the iterator */
	return trace_read_data(cpu);
}

/* Signal a overhead of time execution to the output */
static void print_graph_overhead(unsigned long long duration)
{
	/* Non nested entry or return */
	if (duration == ~0ULL)
		return (void)printf("  ");

	/* Duration exceeded 100 msecs */
	if (duration > 100000ULL)
		return (void)printf("! ");

	/* Duration exceeded 10 msecs */
	if (duration > 10000ULL)
		return (void)printf("+ ");

	printf("  ");
}

static void print_graph_duration(unsigned long long duration)
{
	unsigned long usecs = duration / 1000;
	unsigned long nsecs_rem = duration % 1000;
	/* log10(ULONG_MAX) + '\0' */
	char msecs_str[21];
	char nsecs_str[5];
	int len;
	int i;

	sprintf(msecs_str, "%lu", usecs);

	/* Print msecs */
	len = printf("%lu", usecs);

	/* Print nsecs (we don't want to exceed 7 numbers) */
	if (len < 7) {
		snprintf(nsecs_str, 8 - len, "%03lu", nsecs_rem);
		len += printf(".%s", nsecs_str);
	}

	printf(" us ");

	/* Print remaining spaces to fit the row's width */
	for (i = len; i < 7; i++)
		printf(" ");

	printf("|  ");
}

static void
print_graph_entry_leaf(struct event *event, void *data, struct record *ret_rec)
{
	unsigned long long rettime, calltime;
	unsigned long long duration, depth;
	unsigned long long val;
	struct format_field *field;
	struct func_map *func;
	struct event *ret_event;
	int type;
	int i;

	type = trace_parse_common_type(ret_rec->data);
	ret_event = trace_find_event(type);

	field = find_field(ret_event, "rettime");
	if (!field)
		die("can't find rettime in return graph");
	rettime = read_size(ret_rec->data + field->offset, field->size);

	field = find_field(ret_event, "calltime");
	if (!field)
		die("can't find rettime in return graph");
	calltime = read_size(ret_rec->data + field->offset, field->size);

	duration = rettime - calltime;

	/* Overhead */
	print_graph_overhead(duration);

	/* Duration */
	print_graph_duration(duration);

	field = find_field(event, "depth");
	if (!field)
		die("can't find depth in entry graph");
	depth = read_size(data + field->offset, field->size);

	/* Function */
	for (i = 0; i < (int)(depth * TRACE_GRAPH_INDENT); i++)
		printf(" ");

	field = find_field(event, "func");
	if (!field)
		die("can't find func in entry graph");
	val = read_size(data + field->offset, field->size);
	func = find_func(val);

	if (func)
		printf("%s();", func->func);
	else
		printf("%llx();", val);
}

static void print_graph_nested(struct event *event, void *data)
{
	struct format_field *field;
	unsigned long long depth;
	unsigned long long val;
	struct func_map *func;
	int i;

	/* No overhead */
	print_graph_overhead(-1);

	/* No time */
	printf("           |  ");

	field = find_field(event, "depth");
	if (!field)
		die("can't find depth in entry graph");
	depth = read_size(data + field->offset, field->size);

	/* Function */
	for (i = 0; i < (int)(depth * TRACE_GRAPH_INDENT); i++)
		printf(" ");

	field = find_field(event, "func");
	if (!field)
		die("can't find func in entry graph");
	val = read_size(data + field->offset, field->size);
	func = find_func(val);

	if (func)
		printf("%s() {", func->func);
	else
		printf("%llx() {", val);
}

static void
pretty_print_func_ent(void *data, int size, struct event *event,
		      int cpu, int pid, const char *comm,
		      unsigned long secs, unsigned long usecs)
{
	struct format_field *field;
	struct record *rec;
	void *copy_data;
	unsigned long val;

	printf("%5lu.%06lu |  ", secs, usecs);

	print_graph_cpu(cpu);
	print_graph_proc(pid, comm);

	printf(" | ");

	if (latency_format) {
		print_lat_fmt(data, size);
		printf(" | ");
	}

	field = find_field(event, "func");
	if (!field)
		die("function entry does not have func field");

	val = read_size(data + field->offset, field->size);

	/*
	 * peek_data may unmap the data pointer. Copy it first.
	 */
	copy_data = malloc_or_die(size);
	memcpy(copy_data, data, size);
	data = copy_data;

	rec = trace_peek_data(cpu);
	if (rec) {
		rec = get_return_for_leaf(cpu, pid, val, rec);
		if (rec) {
			print_graph_entry_leaf(event, data, rec);
			goto out_free;
		}
	}
	print_graph_nested(event, data);
out_free:
	free(data);
}

static void
pretty_print_func_ret(void *data, int size __unused, struct event *event,
		      int cpu, int pid, const char *comm,
		      unsigned long secs, unsigned long usecs)
{
	unsigned long long rettime, calltime;
	unsigned long long duration, depth;
	struct format_field *field;
	int i;

	printf("%5lu.%06lu |  ", secs, usecs);

	print_graph_cpu(cpu);
	print_graph_proc(pid, comm);

	printf(" | ");

	if (latency_format) {
		print_lat_fmt(data, size);
		printf(" | ");
	}

	field = find_field(event, "rettime");
	if (!field)
		die("can't find rettime in return graph");
	rettime = read_size(data + field->offset, field->size);

	field = find_field(event, "calltime");
	if (!field)
		die("can't find calltime in return graph");
	calltime = read_size(data + field->offset, field->size);

	duration = rettime - calltime;

	/* Overhead */
	print_graph_overhead(duration);

	/* Duration */
	print_graph_duration(duration);

	field = find_field(event, "depth");
	if (!field)
		die("can't find depth in entry graph");
	depth = read_size(data + field->offset, field->size);

	/* Function */
	for (i = 0; i < (int)(depth * TRACE_GRAPH_INDENT); i++)
		printf(" ");

	printf("}");
}

static void
pretty_print_func_graph(void *data, int size, struct event *event,
			int cpu, int pid, const char *comm,
			unsigned long secs, unsigned long usecs)
{
	if (event->flags & EVENT_FL_ISFUNCENT)
		pretty_print_func_ent(data, size, event,
				      cpu, pid, comm, secs, usecs);
	else if (event->flags & EVENT_FL_ISFUNCRET)
		pretty_print_func_ret(data, size, event,
				      cpu, pid, comm, secs, usecs);
	printf("\n");
}

void print_event(int cpu, void *data, int size, unsigned long long nsecs,
		  char *comm)
{
	struct event *event;
	unsigned long secs;
	unsigned long usecs;
	int type;
	int pid;

	secs = nsecs / NSECS_PER_SEC;
	nsecs -= secs * NSECS_PER_SEC;
	usecs = nsecs / NSECS_PER_USEC;

	type = trace_parse_common_type(data);

	event = trace_find_event(type);
	if (!event) {
		warning("ug! no event found for type %d", type);
		return;
	}

	pid = trace_parse_common_pid(data);

	if (event->flags & (EVENT_FL_ISFUNCENT | EVENT_FL_ISFUNCRET))
		return pretty_print_func_graph(data, size, event, cpu,
					       pid, comm, secs, usecs);

	if (latency_format) {
		printf("%8.8s-%-5d %3d",
		       comm, pid, cpu);
		print_lat_fmt(data, size);
	} else
		printf("%16s-%-5d [%03d]", comm, pid,  cpu);

	printf(" %5lu.%06lu: %s: ", secs, usecs, event->name);

	if (event->flags & EVENT_FL_FAILED) {
		printf("EVENT '%s' FAILED TO PARSE\n",
		       event->name);
		return;
	}

	pretty_print(data, size, event);
	printf("\n");
}

static void print_fields(struct print_flag_sym *field)
{
	printf("{ %s, %s }", field->value, field->str);
	if (field->next) {
		printf(", ");
		print_fields(field->next);
	}
}

static void print_args(struct print_arg *args)
{
	int print_paren = 1;

	switch (args->type) {
	case PRINT_NULL:
		printf("null");
		break;
	case PRINT_ATOM:
		printf("%s", args->atom.atom);
		break;
	case PRINT_FIELD:
		printf("REC->%s", args->field.name);
		break;
	case PRINT_FLAGS:
		printf("__print_flags(");
		print_args(args->flags.field);
		printf(", %s, ", args->flags.delim);
		print_fields(args->flags.flags);
		printf(")");
		break;
	case PRINT_SYMBOL:
		printf("__print_symbolic(");
		print_args(args->symbol.field);
		printf(", ");
		print_fields(args->symbol.symbols);
		printf(")");
		break;
	case PRINT_STRING:
		printf("__get_str(%s)", args->string.string);
		break;
	case PRINT_TYPE:
		printf("(%s)", args->typecast.type);
		print_args(args->typecast.item);
		break;
	case PRINT_OP:
		if (strcmp(args->op.op, ":") == 0)
			print_paren = 0;
		if (print_paren)
			printf("(");
		print_args(args->op.left);
		printf(" %s ", args->op.op);
		print_args(args->op.right);
		if (print_paren)
			printf(")");
		break;
	default:
		/* we should warn... */
		return;
	}
	if (args->next) {
		printf("\n");
		print_args(args->next);
	}
}

static void parse_header_field(const char *field,
			       int *offset, int *size)
{
	char *token;
	int type;

	if (read_expected(EVENT_ITEM, "field") < 0)
		return;
	if (read_expected(EVENT_OP, ":") < 0)
		return;

	/* type */
	if (read_expect_type(EVENT_ITEM, &token) < 0)
		goto fail;
	free_token(token);

	if (read_expected(EVENT_ITEM, field) < 0)
		return;
	if (read_expected(EVENT_OP, ";") < 0)
		return;
	if (read_expected(EVENT_ITEM, "offset") < 0)
		return;
	if (read_expected(EVENT_OP, ":") < 0)
		return;
	if (read_expect_type(EVENT_ITEM, &token) < 0)
		goto fail;
	*offset = atoi(token);
	free_token(token);
	if (read_expected(EVENT_OP, ";") < 0)
		return;
	if (read_expected(EVENT_ITEM, "size") < 0)
		return;
	if (read_expected(EVENT_OP, ":") < 0)
		return;
	if (read_expect_type(EVENT_ITEM, &token) < 0)
		goto fail;
	*size = atoi(token);
	free_token(token);
	if (read_expected(EVENT_OP, ";") < 0)
		return;
	type = read_token(&token);
	if (type != EVENT_NEWLINE) {
		/* newer versions of the kernel have a "signed" type */
		if (type != EVENT_ITEM)
			goto fail;

		if (strcmp(token, "signed") != 0)
			goto fail;

		free_token(token);

		if (read_expected(EVENT_OP, ":") < 0)
			return;

		if (read_expect_type(EVENT_ITEM, &token))
			goto fail;

		free_token(token);
		if (read_expected(EVENT_OP, ";") < 0)
			return;

		if (read_expect_type(EVENT_NEWLINE, &token))
			goto fail;
	}
 fail:
	free_token(token);
}

int parse_header_page(char *buf, unsigned long size)
{
	init_input_buf(buf, size);

	parse_header_field("timestamp", &header_page_ts_offset,
			   &header_page_ts_size);
	parse_header_field("commit", &header_page_size_offset,
			   &header_page_size_size);
	parse_header_field("data", &header_page_data_offset,
			   &header_page_data_size);

	return 0;
}

int parse_ftrace_file(char *buf, unsigned long size)
{
	struct format_field *field;
	struct print_arg *arg, **list;
	struct event *event;
	int ret;

	init_input_buf(buf, size);

	event = alloc_event();
	if (!event)
		return -ENOMEM;

	event->flags |= EVENT_FL_ISFTRACE;

	event->name = event_read_name();
	if (!event->name)
		die("failed to read ftrace event name");

	if (strcmp(event->name, "function") == 0)
		event->flags |= EVENT_FL_ISFUNC;

	else if (strcmp(event->name, "funcgraph_entry") == 0)
		event->flags |= EVENT_FL_ISFUNCENT;

	else if (strcmp(event->name, "funcgraph_exit") == 0)
		event->flags |= EVENT_FL_ISFUNCRET;

	else if (strcmp(event->name, "bprint") == 0)
		event->flags |= EVENT_FL_ISBPRINT;

	event->id = event_read_id();
	if (event->id < 0)
		die("failed to read ftrace event id");

	add_event(event);

	ret = event_read_format(event);
	if (ret < 0)
		die("failed to read ftrace event format");

	ret = event_read_print(event);
	if (ret < 0)
		die("failed to read ftrace event print fmt");

	/* New ftrace handles args */
	if (ret > 0)
		return 0;
	/*
	 * The arguments for ftrace files are parsed by the fields.
	 * Set up the fields as their arguments.
	 */
	list = &event->print_fmt.args;
	for (field = event->format.fields; field; field = field->next) {
		arg = malloc_or_die(sizeof(*arg));
		memset(arg, 0, sizeof(*arg));
		*list = arg;
		list = &arg->next;
		arg->type = PRINT_FIELD;
		arg->field.name = field->name;
		arg->field.field = field;
	}
	return 0;
}

int parse_event_file(char *buf, unsigned long size, char *sys)
{
	struct event *event;
	int ret;

	init_input_buf(buf, size);

	event = alloc_event();
	if (!event)
		return -ENOMEM;

	event->name = event_read_name();
	if (!event->name)
		die("failed to read event name");

	event->id = event_read_id();
	if (event->id < 0)
		die("failed to read event id");

	ret = event_read_format(event);
	if (ret < 0) {
		warning("failed to read event format for %s", event->name);
		goto event_failed;
	}

	ret = event_read_print(event);
	if (ret < 0) {
		warning("failed to read event print fmt for %s", event->name);
		goto event_failed;
	}

	event->system = strdup(sys);

#define PRINT_ARGS 0
	if (PRINT_ARGS && event->print_fmt.args)
		print_args(event->print_fmt.args);

	add_event(event);
	return 0;

 event_failed:
	event->flags |= EVENT_FL_FAILED;
	/* still add it even if it failed */
	add_event(event);
	return -1;
}

void parse_set_info(int nr_cpus, int long_sz)
{
	cpus = nr_cpus;
	long_size = long_sz;
}

int common_pc(struct scripting_context *context)
{
	return parse_common_pc(context->event_data);
}

int common_flags(struct scripting_context *context)
{
	return parse_common_flags(context->event_data);
}

int common_lock_depth(struct scripting_context *context)
{
	return parse_common_lock_depth(context->event_data);
}
