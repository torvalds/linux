/*
 * Copyright (C) 2009, 2010 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License (not later!)
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  The parts for function graph printing was taken and modified from the
 *  Linux Kernel that were written by
 *    - Copyright (C) 2009  Frederic Weisbecker,
 *  Frederic Weisbecker gave his permission to relicense the code to
 *  the Lesser General Public License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <limits.h>

#include "event-parse.h"
#include "event-utils.h"

static const char *input_buf;
static unsigned long long input_buf_ptr;
static unsigned long long input_buf_siz;

static int is_flag_field;
static int is_symbolic_field;

static int show_warning = 1;

#define do_warning(fmt, ...)				\
	do {						\
		if (show_warning)			\
			warning(fmt, ##__VA_ARGS__);	\
	} while (0)

static void init_input_buf(const char *buf, unsigned long long size)
{
	input_buf = buf;
	input_buf_siz = size;
	input_buf_ptr = 0;
}

const char *pevent_get_input_buf(void)
{
	return input_buf;
}

unsigned long long pevent_get_input_buf_ptr(void)
{
	return input_buf_ptr;
}

struct event_handler {
	struct event_handler		*next;
	int				id;
	const char			*sys_name;
	const char			*event_name;
	pevent_event_handler_func	func;
	void				*context;
};

struct pevent_func_params {
	struct pevent_func_params	*next;
	enum pevent_func_arg_type	type;
};

struct pevent_function_handler {
	struct pevent_function_handler	*next;
	enum pevent_func_arg_type	ret_type;
	char				*name;
	pevent_func_handler		func;
	struct pevent_func_params	*params;
	int				nr_args;
};

static unsigned long long
process_defined_func(struct trace_seq *s, void *data, int size,
		     struct event_format *event, struct print_arg *arg);

static void free_func_handle(struct pevent_function_handler *func);

/**
 * pevent_buffer_init - init buffer for parsing
 * @buf: buffer to parse
 * @size: the size of the buffer
 *
 * For use with pevent_read_token(), this initializes the internal
 * buffer that pevent_read_token() will parse.
 */
void pevent_buffer_init(const char *buf, unsigned long long size)
{
	init_input_buf(buf, size);
}

void breakpoint(void)
{
	static int x;
	x++;
}

struct print_arg *alloc_arg(void)
{
	return calloc(1, sizeof(struct print_arg));
}

struct cmdline {
	char *comm;
	int pid;
};

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

struct cmdline_list {
	struct cmdline_list	*next;
	char			*comm;
	int			pid;
};

static int cmdline_init(struct pevent *pevent)
{
	struct cmdline_list *cmdlist = pevent->cmdlist;
	struct cmdline_list *item;
	struct cmdline *cmdlines;
	int i;

	cmdlines = malloc(sizeof(*cmdlines) * pevent->cmdline_count);
	if (!cmdlines)
		return -1;

	i = 0;
	while (cmdlist) {
		cmdlines[i].pid = cmdlist->pid;
		cmdlines[i].comm = cmdlist->comm;
		i++;
		item = cmdlist;
		cmdlist = cmdlist->next;
		free(item);
	}

	qsort(cmdlines, pevent->cmdline_count, sizeof(*cmdlines), cmdline_cmp);

	pevent->cmdlines = cmdlines;
	pevent->cmdlist = NULL;

	return 0;
}

static char *find_cmdline(struct pevent *pevent, int pid)
{
	const struct cmdline *comm;
	struct cmdline key;

	if (!pid)
		return "<idle>";

	if (!pevent->cmdlines && cmdline_init(pevent))
		return "<not enough memory for cmdlines!>";

	key.pid = pid;

	comm = bsearch(&key, pevent->cmdlines, pevent->cmdline_count,
		       sizeof(*pevent->cmdlines), cmdline_cmp);

	if (comm)
		return comm->comm;
	return "<...>";
}

/**
 * pevent_pid_is_registered - return if a pid has a cmdline registered
 * @pevent: handle for the pevent
 * @pid: The pid to check if it has a cmdline registered with.
 *
 * Returns 1 if the pid has a cmdline mapped to it
 * 0 otherwise.
 */
int pevent_pid_is_registered(struct pevent *pevent, int pid)
{
	const struct cmdline *comm;
	struct cmdline key;

	if (!pid)
		return 1;

	if (!pevent->cmdlines && cmdline_init(pevent))
		return 0;

	key.pid = pid;

	comm = bsearch(&key, pevent->cmdlines, pevent->cmdline_count,
		       sizeof(*pevent->cmdlines), cmdline_cmp);

	if (comm)
		return 1;
	return 0;
}

/*
 * If the command lines have been converted to an array, then
 * we must add this pid. This is much slower than when cmdlines
 * are added before the array is initialized.
 */
static int add_new_comm(struct pevent *pevent, const char *comm, int pid)
{
	struct cmdline *cmdlines = pevent->cmdlines;
	const struct cmdline *cmdline;
	struct cmdline key;

	if (!pid)
		return 0;

	/* avoid duplicates */
	key.pid = pid;

	cmdline = bsearch(&key, pevent->cmdlines, pevent->cmdline_count,
		       sizeof(*pevent->cmdlines), cmdline_cmp);
	if (cmdline) {
		errno = EEXIST;
		return -1;
	}

	cmdlines = realloc(cmdlines, sizeof(*cmdlines) * (pevent->cmdline_count + 1));
	if (!cmdlines) {
		errno = ENOMEM;
		return -1;
	}

	cmdlines[pevent->cmdline_count].comm = strdup(comm);
	if (!cmdlines[pevent->cmdline_count].comm) {
		free(cmdlines);
		errno = ENOMEM;
		return -1;
	}

	cmdlines[pevent->cmdline_count].pid = pid;
		
	if (cmdlines[pevent->cmdline_count].comm)
		pevent->cmdline_count++;

	qsort(cmdlines, pevent->cmdline_count, sizeof(*cmdlines), cmdline_cmp);
	pevent->cmdlines = cmdlines;

	return 0;
}

/**
 * pevent_register_comm - register a pid / comm mapping
 * @pevent: handle for the pevent
 * @comm: the command line to register
 * @pid: the pid to map the command line to
 *
 * This adds a mapping to search for command line names with
 * a given pid. The comm is duplicated.
 */
int pevent_register_comm(struct pevent *pevent, const char *comm, int pid)
{
	struct cmdline_list *item;

	if (pevent->cmdlines)
		return add_new_comm(pevent, comm, pid);

	item = malloc(sizeof(*item));
	if (!item)
		return -1;

	item->comm = strdup(comm);
	if (!item->comm) {
		free(item);
		return -1;
	}
	item->pid = pid;
	item->next = pevent->cmdlist;

	pevent->cmdlist = item;
	pevent->cmdline_count++;

	return 0;
}

struct func_map {
	unsigned long long		addr;
	char				*func;
	char				*mod;
};

struct func_list {
	struct func_list	*next;
	unsigned long long	addr;
	char			*func;
	char			*mod;
};

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

static int func_map_init(struct pevent *pevent)
{
	struct func_list *funclist;
	struct func_list *item;
	struct func_map *func_map;
	int i;

	func_map = malloc(sizeof(*func_map) * (pevent->func_count + 1));
	if (!func_map)
		return -1;

	funclist = pevent->funclist;

	i = 0;
	while (funclist) {
		func_map[i].func = funclist->func;
		func_map[i].addr = funclist->addr;
		func_map[i].mod = funclist->mod;
		i++;
		item = funclist;
		funclist = funclist->next;
		free(item);
	}

	qsort(func_map, pevent->func_count, sizeof(*func_map), func_cmp);

	/*
	 * Add a special record at the end.
	 */
	func_map[pevent->func_count].func = NULL;
	func_map[pevent->func_count].addr = 0;
	func_map[pevent->func_count].mod = NULL;

	pevent->func_map = func_map;
	pevent->funclist = NULL;

	return 0;
}

static struct func_map *
find_func(struct pevent *pevent, unsigned long long addr)
{
	struct func_map *func;
	struct func_map key;

	if (!pevent->func_map)
		func_map_init(pevent);

	key.addr = addr;

	func = bsearch(&key, pevent->func_map, pevent->func_count,
		       sizeof(*pevent->func_map), func_bcmp);

	return func;
}

/**
 * pevent_find_function - find a function by a given address
 * @pevent: handle for the pevent
 * @addr: the address to find the function with
 *
 * Returns a pointer to the function stored that has the given
 * address. Note, the address does not have to be exact, it
 * will select the function that would contain the address.
 */
const char *pevent_find_function(struct pevent *pevent, unsigned long long addr)
{
	struct func_map *map;

	map = find_func(pevent, addr);
	if (!map)
		return NULL;

	return map->func;
}

/**
 * pevent_find_function_address - find a function address by a given address
 * @pevent: handle for the pevent
 * @addr: the address to find the function with
 *
 * Returns the address the function starts at. This can be used in
 * conjunction with pevent_find_function to print both the function
 * name and the function offset.
 */
unsigned long long
pevent_find_function_address(struct pevent *pevent, unsigned long long addr)
{
	struct func_map *map;

	map = find_func(pevent, addr);
	if (!map)
		return 0;

	return map->addr;
}

/**
 * pevent_register_function - register a function with a given address
 * @pevent: handle for the pevent
 * @function: the function name to register
 * @addr: the address the function starts at
 * @mod: the kernel module the function may be in (NULL for none)
 *
 * This registers a function name with an address and module.
 * The @func passed in is duplicated.
 */
int pevent_register_function(struct pevent *pevent, char *func,
			     unsigned long long addr, char *mod)
{
	struct func_list *item = malloc(sizeof(*item));

	if (!item)
		return -1;

	item->next = pevent->funclist;
	item->func = strdup(func);
	if (!item->func)
		goto out_free;

	if (mod) {
		item->mod = strdup(mod);
		if (!item->mod)
			goto out_free_func;
	} else
		item->mod = NULL;
	item->addr = addr;

	pevent->funclist = item;
	pevent->func_count++;

	return 0;

out_free_func:
	free(item->func);
	item->func = NULL;
out_free:
	free(item);
	errno = ENOMEM;
	return -1;
}

/**
 * pevent_print_funcs - print out the stored functions
 * @pevent: handle for the pevent
 *
 * This prints out the stored functions.
 */
void pevent_print_funcs(struct pevent *pevent)
{
	int i;

	if (!pevent->func_map)
		func_map_init(pevent);

	for (i = 0; i < (int)pevent->func_count; i++) {
		printf("%016llx %s",
		       pevent->func_map[i].addr,
		       pevent->func_map[i].func);
		if (pevent->func_map[i].mod)
			printf(" [%s]\n", pevent->func_map[i].mod);
		else
			printf("\n");
	}
}

struct printk_map {
	unsigned long long		addr;
	char				*printk;
};

struct printk_list {
	struct printk_list	*next;
	unsigned long long	addr;
	char			*printk;
};

static int printk_cmp(const void *a, const void *b)
{
	const struct printk_map *pa = a;
	const struct printk_map *pb = b;

	if (pa->addr < pb->addr)
		return -1;
	if (pa->addr > pb->addr)
		return 1;

	return 0;
}

static int printk_map_init(struct pevent *pevent)
{
	struct printk_list *printklist;
	struct printk_list *item;
	struct printk_map *printk_map;
	int i;

	printk_map = malloc(sizeof(*printk_map) * (pevent->printk_count + 1));
	if (!printk_map)
		return -1;

	printklist = pevent->printklist;

	i = 0;
	while (printklist) {
		printk_map[i].printk = printklist->printk;
		printk_map[i].addr = printklist->addr;
		i++;
		item = printklist;
		printklist = printklist->next;
		free(item);
	}

	qsort(printk_map, pevent->printk_count, sizeof(*printk_map), printk_cmp);

	pevent->printk_map = printk_map;
	pevent->printklist = NULL;

	return 0;
}

static struct printk_map *
find_printk(struct pevent *pevent, unsigned long long addr)
{
	struct printk_map *printk;
	struct printk_map key;

	if (!pevent->printk_map && printk_map_init(pevent))
		return NULL;

	key.addr = addr;

	printk = bsearch(&key, pevent->printk_map, pevent->printk_count,
			 sizeof(*pevent->printk_map), printk_cmp);

	return printk;
}

/**
 * pevent_register_print_string - register a string by its address
 * @pevent: handle for the pevent
 * @fmt: the string format to register
 * @addr: the address the string was located at
 *
 * This registers a string by the address it was stored in the kernel.
 * The @fmt passed in is duplicated.
 */
int pevent_register_print_string(struct pevent *pevent, char *fmt,
				 unsigned long long addr)
{
	struct printk_list *item = malloc(sizeof(*item));

	if (!item)
		return -1;

	item->next = pevent->printklist;
	item->addr = addr;

	item->printk = strdup(fmt);
	if (!item->printk)
		goto out_free;

	pevent->printklist = item;
	pevent->printk_count++;

	return 0;

out_free:
	free(item);
	errno = ENOMEM;
	return -1;
}

/**
 * pevent_print_printk - print out the stored strings
 * @pevent: handle for the pevent
 *
 * This prints the string formats that were stored.
 */
void pevent_print_printk(struct pevent *pevent)
{
	int i;

	if (!pevent->printk_map)
		printk_map_init(pevent);

	for (i = 0; i < (int)pevent->printk_count; i++) {
		printf("%016llx %s\n",
		       pevent->printk_map[i].addr,
		       pevent->printk_map[i].printk);
	}
}

static struct event_format *alloc_event(void)
{
	return calloc(1, sizeof(struct event_format));
}

static int add_event(struct pevent *pevent, struct event_format *event)
{
	int i;
	struct event_format **events = realloc(pevent->events, sizeof(event) *
					       (pevent->nr_events + 1));
	if (!events)
		return -1;

	pevent->events = events;

	for (i = 0; i < pevent->nr_events; i++) {
		if (pevent->events[i]->id > event->id)
			break;
	}
	if (i < pevent->nr_events)
		memmove(&pevent->events[i + 1],
			&pevent->events[i],
			sizeof(event) * (pevent->nr_events - i));

	pevent->events[i] = event;
	pevent->nr_events++;

	event->pevent = pevent;

	return 0;
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

static void free_flag_sym(struct print_flag_sym *fsym)
{
	struct print_flag_sym *next;

	while (fsym) {
		next = fsym->next;
		free(fsym->value);
		free(fsym->str);
		free(fsym);
		fsym = next;
	}
}

static void free_arg(struct print_arg *arg)
{
	struct print_arg *farg;

	if (!arg)
		return;

	switch (arg->type) {
	case PRINT_ATOM:
		free(arg->atom.atom);
		break;
	case PRINT_FIELD:
		free(arg->field.name);
		break;
	case PRINT_FLAGS:
		free_arg(arg->flags.field);
		free(arg->flags.delim);
		free_flag_sym(arg->flags.flags);
		break;
	case PRINT_SYMBOL:
		free_arg(arg->symbol.field);
		free_flag_sym(arg->symbol.symbols);
		break;
	case PRINT_HEX:
		free_arg(arg->hex.field);
		free_arg(arg->hex.size);
		break;
	case PRINT_TYPE:
		free(arg->typecast.type);
		free_arg(arg->typecast.item);
		break;
	case PRINT_STRING:
	case PRINT_BSTRING:
		free(arg->string.string);
		break;
	case PRINT_DYNAMIC_ARRAY:
		free(arg->dynarray.index);
		break;
	case PRINT_OP:
		free(arg->op.op);
		free_arg(arg->op.left);
		free_arg(arg->op.right);
		break;
	case PRINT_FUNC:
		while (arg->func.args) {
			farg = arg->func.args;
			arg->func.args = farg->next;
			free_arg(farg);
		}
		break;

	case PRINT_NULL:
	default:
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

/**
 * pevent_peek_char - peek at the next character that will be read
 *
 * Returns the next character read, or -1 if end of buffer.
 */
int pevent_peek_char(void)
{
	return __peek_char();
}

static int extend_token(char **tok, char *buf, int size)
{
	char *newtok = realloc(*tok, size);

	if (!newtok) {
		free(*tok);
		*tok = NULL;
		return -1;
	}

	if (!*tok)
		strcpy(newtok, buf);
	else
		strcat(newtok, buf);
	*tok = newtok;

	return 0;
}

static enum event_type force_token(const char *str, char **tok);

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
		if (asprintf(tok, "%c", ch) < 0)
			return EVENT_ERROR;

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
		goto out;

	case EVENT_DQUOTE:
	case EVENT_SQUOTE:
		/* don't keep quotes */
		i--;
		quote_ch = ch;
		last_ch = 0;
 concat:
		do {
			if (i == (BUFSIZ - 1)) {
				buf[i] = 0;
				tok_size += BUFSIZ;

				if (extend_token(tok, buf, tok_size) < 0)
					return EVENT_NONE;
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

		/*
		 * For strings (double quotes) check the next token.
		 * If it is another string, concatinate the two.
		 */
		if (type == EVENT_DQUOTE) {
			unsigned long long save_input_buf_ptr = input_buf_ptr;

			do {
				ch = __read_char();
			} while (isspace(ch));
			if (ch == '"')
				goto concat;
			input_buf_ptr = save_input_buf_ptr;
		}

		goto out;

	case EVENT_ERROR ... EVENT_SPACE:
	case EVENT_ITEM:
	default:
		break;
	}

	while (get_type(__peek_char()) == type) {
		if (i == (BUFSIZ - 1)) {
			buf[i] = 0;
			tok_size += BUFSIZ;

			if (extend_token(tok, buf, tok_size) < 0)
				return EVENT_NONE;
			i = 0;
		}
		ch = __read_char();
		buf[i++] = ch;
	}

 out:
	buf[i] = 0;
	if (extend_token(tok, buf, tok_size + i + 1) < 0)
		return EVENT_NONE;

	if (type == EVENT_ITEM) {
		/*
		 * Older versions of the kernel has a bug that
		 * creates invalid symbols and will break the mac80211
		 * parsing. This is a work around to that bug.
		 *
		 * See Linux kernel commit:
		 *  811cb50baf63461ce0bdb234927046131fc7fa8b
		 */
		if (strcmp(*tok, "LOCAL_PR_FMT") == 0) {
			free(*tok);
			*tok = NULL;
			return force_token("\"\%s\" ", tok);
		} else if (strcmp(*tok, "STA_PR_FMT") == 0) {
			free(*tok);
			*tok = NULL;
			return force_token("\" sta:%pM\" ", tok);
		} else if (strcmp(*tok, "VIF_PR_FMT") == 0) {
			free(*tok);
			*tok = NULL;
			return force_token("\" vif:%p(%d)\" ", tok);
		}
	}

	return type;
}

static enum event_type force_token(const char *str, char **tok)
{
	const char *save_input_buf;
	unsigned long long save_input_buf_ptr;
	unsigned long long save_input_buf_siz;
	enum event_type type;
	
	/* save off the current input pointers */
	save_input_buf = input_buf;
	save_input_buf_ptr = input_buf_ptr;
	save_input_buf_siz = input_buf_siz;

	init_input_buf(str, strlen(str));

	type = __read_token(tok);

	/* reset back to original token */
	input_buf = save_input_buf;
	input_buf_ptr = save_input_buf_ptr;
	input_buf_siz = save_input_buf_siz;

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
	*tok = NULL;
	return EVENT_NONE;
}

/**
 * pevent_read_token - access to utilites to use the pevent parser
 * @tok: The token to return
 *
 * This will parse tokens from the string given by
 * pevent_init_data().
 *
 * Returns the token type.
 */
enum event_type pevent_read_token(char **tok)
{
	return read_token(tok);
}

/**
 * pevent_free_token - free a token returned by pevent_read_token
 * @token: the token to free
 */
void pevent_free_token(char *token)
{
	free_token(token);
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
		*tok = NULL;
	}

	/* not reached */
	*tok = NULL;
	return EVENT_NONE;
}

static int test_type(enum event_type type, enum event_type expect)
{
	if (type != expect) {
		do_warning("Error: expected type %d but read %d",
		    expect, type);
		return -1;
	}
	return 0;
}

static int test_type_token(enum event_type type, const char *token,
		    enum event_type expect, const char *expect_tok)
{
	if (type != expect) {
		do_warning("Error: expected type %d but read %d",
		    expect, type);
		return -1;
	}

	if (strcmp(token, expect_tok) != 0) {
		do_warning("Error: expected '%s' but read '%s'",
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

static int __read_expected(enum event_type expect, const char *str,
			   int newline_ok)
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
	    (strstr(field->type, "char") || strstr(field->type, "u8") ||
	     strstr(field->type, "s8")))
		return 1;

	return 0;
}

static int field_is_dynamic(struct format_field *field)
{
	if (strncmp(field->type, "__data_loc", 10) == 0)
		return 1;

	return 0;
}

static int field_is_long(struct format_field *field)
{
	/* includes long long */
	if (strstr(field->type, "long"))
		return 1;

	return 0;
}

static int event_read_fields(struct event_format *event, struct format_field **fields)
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
			goto fail;

		free_token(token);
		if (read_expect_type(EVENT_ITEM, &token) < 0)
			goto fail;

		last_token = token;

		field = calloc(1, sizeof(*field));
		if (!field)
			goto fail;

		field->event = event;

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
					char *new_type;
					new_type = realloc(field->type,
							   strlen(field->type) +
							   strlen(last_token) + 2);
					if (!new_type) {
						free(last_token);
						goto fail;
					}
					field->type = new_type;
					strcat(field->type, " ");
					strcat(field->type, last_token);
					free(last_token);
				} else
					field->type = last_token;
				last_token = token;
				continue;
			}

			break;
		}

		if (!field->type) {
			do_warning("%s: no type found", __func__);
			goto fail;
		}
		field->name = last_token;

		if (test_type(type, EVENT_OP))
			goto fail;

		if (strcmp(token, "[") == 0) {
			enum event_type last_type = type;
			char *brackets = token;
			char *new_brackets;
			int len;

			field->flags |= FIELD_IS_ARRAY;

			type = read_token(&token);

			if (type == EVENT_ITEM)
				field->arraylen = strtoul(token, NULL, 0);
			else
				field->arraylen = 0;

		        while (strcmp(token, "]") != 0) {
				if (last_type == EVENT_ITEM &&
				    type == EVENT_ITEM)
					len = 2;
				else
					len = 1;
				last_type = type;

				new_brackets = realloc(brackets,
						       strlen(brackets) +
						       strlen(token) + len);
				if (!new_brackets) {
					free(brackets);
					goto fail;
				}
				brackets = new_brackets;
				if (len == 2)
					strcat(brackets, " ");
				strcat(brackets, token);
				/* We only care about the last token */
				field->arraylen = strtoul(token, NULL, 0);
				free_token(token);
				type = read_token(&token);
				if (type == EVENT_NONE) {
					do_warning("failed to find token");
					goto fail;
				}
			}

			free_token(token);

			new_brackets = realloc(brackets, strlen(brackets) + 2);
			if (!new_brackets) {
				free(brackets);
				goto fail;
			}
			brackets = new_brackets;
			strcat(brackets, "]");

			/* add brackets to type */

			type = read_token(&token);
			/*
			 * If the next token is not an OP, then it is of
			 * the format: type [] item;
			 */
			if (type == EVENT_ITEM) {
				char *new_type;
				new_type = realloc(field->type,
						   strlen(field->type) +
						   strlen(field->name) +
						   strlen(brackets) + 2);
				if (!new_type) {
					free(brackets);
					goto fail;
				}
				field->type = new_type;
				strcat(field->type, " ");
				strcat(field->type, field->name);
				free_token(field->name);
				strcat(field->type, brackets);
				field->name = token;
				type = read_token(&token);
			} else {
				char *new_type;
				new_type = realloc(field->type,
						   strlen(field->type) +
						   strlen(brackets) + 1);
				if (!new_type) {
					free(brackets);
					goto fail;
				}
				field->type = new_type;
				strcat(field->type, brackets);
			}
			free(brackets);
		}

		if (field_is_string(field))
			field->flags |= FIELD_IS_STRING;
		if (field_is_dynamic(field))
			field->flags |= FIELD_IS_DYNAMIC;
		if (field_is_long(field))
			field->flags |= FIELD_IS_LONG;

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

			/* add signed type */

			free_token(token);
			if (read_expected(EVENT_OP, ";") < 0)
				goto fail_expect;

			if (read_expect_type(EVENT_NEWLINE, &token))
				goto fail;
		}

		free_token(token);

		if (field->flags & FIELD_IS_ARRAY) {
			if (field->arraylen)
				field->elementsize = field->size / field->arraylen;
			else if (field->flags & FIELD_IS_STRING)
				field->elementsize = 1;
			else
				field->elementsize = event->pevent->long_size;
		} else
			field->elementsize = field->size;

		*fields = field;
		fields = &field->next;

	} while (1);

	return 0;

fail:
	free_token(token);
fail_expect:
	if (field) {
		free(field->type);
		free(field->name);
		free(field);
	}
	return -1;
}

static int event_read_format(struct event_format *event)
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

static enum event_type
process_arg_token(struct event_format *event, struct print_arg *arg,
		  char **tok, enum event_type type);

static enum event_type
process_arg(struct event_format *event, struct print_arg *arg, char **tok)
{
	enum event_type type;
	char *token;

	type = read_token(&token);
	*tok = token;

	return process_arg_token(event, arg, tok, type);
}

static enum event_type
process_op(struct event_format *event, struct print_arg *arg, char **tok);

static enum event_type
process_cond(struct event_format *event, struct print_arg *top, char **tok)
{
	struct print_arg *arg, *left, *right;
	enum event_type type;
	char *token = NULL;

	arg = alloc_arg();
	left = alloc_arg();
	right = alloc_arg();

	if (!arg || !left || !right) {
		do_warning("%s: not enough memory!", __func__);
		/* arg will be freed at out_free */
		free_arg(left);
		free_arg(right);
		goto out_free;
	}

	arg->type = PRINT_OP;
	arg->op.left = left;
	arg->op.right = right;

	*tok = NULL;
	type = process_arg(event, left, &token);

 again:
	/* Handle other operations in the arguments */
	if (type == EVENT_OP && strcmp(token, ":") != 0) {
		type = process_op(event, left, &token);
		goto again;
	}

	if (test_type_token(type, token, EVENT_OP, ":"))
		goto out_free;

	arg->op.op = token;

	type = process_arg(event, right, &token);

	top->op.right = arg;

	*tok = token;
	return type;

out_free:
	/* Top may point to itself */
	top->op.right = NULL;
	free_token(token);
	free_arg(arg);
	return EVENT_ERROR;
}

static enum event_type
process_array(struct event_format *event, struct print_arg *top, char **tok)
{
	struct print_arg *arg;
	enum event_type type;
	char *token = NULL;

	arg = alloc_arg();
	if (!arg) {
		do_warning("%s: not enough memory!", __func__);
		/* '*tok' is set to top->op.op.  No need to free. */
		*tok = NULL;
		return EVENT_ERROR;
	}

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
	free_token(token);
	free_arg(arg);
	return EVENT_ERROR;
}

static int get_op_prio(char *op)
{
	if (!op[1]) {
		switch (op[0]) {
		case '~':
		case '!':
			return 4;
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
			do_warning("unknown op '%c'", op[0]);
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
			do_warning("unknown op '%s'", op);
			return -1;
		}
	}
}

static int set_op_prio(struct print_arg *arg)
{

	/* single ops are the greatest */
	if (!arg->op.left || arg->op.left->type == PRINT_NULL)
		arg->op.prio = 0;
	else
		arg->op.prio = get_op_prio(arg->op.op);

	return arg->op.prio;
}

/* Note, *tok does not get freed, but will most likely be saved */
static enum event_type
process_op(struct event_format *event, struct print_arg *arg, char **tok)
{
	struct print_arg *left, *right = NULL;
	enum event_type type;
	char *token;

	/* the op is passed in via tok */
	token = *tok;

	if (arg->type == PRINT_OP && !arg->op.left) {
		/* handle single op */
		if (token[1]) {
			do_warning("bad op token %s", token);
			goto out_free;
		}
		switch (token[0]) {
		case '~':
		case '!':
		case '+':
		case '-':
			break;
		default:
			do_warning("bad op token %s", token);
			goto out_free;

		}

		/* make an empty left */
		left = alloc_arg();
		if (!left)
			goto out_warn_free;

		left->type = PRINT_NULL;
		arg->op.left = left;

		right = alloc_arg();
		if (!right)
			goto out_warn_free;

		arg->op.right = right;

		/* do not free the token, it belongs to an op */
		*tok = NULL;
		type = process_arg(event, right, tok);

	} else if (strcmp(token, "?") == 0) {

		left = alloc_arg();
		if (!left)
			goto out_warn_free;

		/* copy the top arg to the left */
		*left = *arg;

		arg->type = PRINT_OP;
		arg->op.op = token;
		arg->op.left = left;
		arg->op.prio = 0;

		/* it will set arg->op.right */
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

		left = alloc_arg();
		if (!left)
			goto out_warn_free;

		/* copy the top arg to the left */
		*left = *arg;

		arg->type = PRINT_OP;
		arg->op.op = token;
		arg->op.left = left;
		arg->op.right = NULL;

		if (set_op_prio(arg) == -1) {
			event->flags |= EVENT_FL_FAILED;
			/* arg->op.op (= token) will be freed at out_free */
			arg->op.op = NULL;
			goto out_free;
		}

		type = read_token_item(&token);
		*tok = token;

		/* could just be a type pointer */
		if ((strcmp(arg->op.op, "*") == 0) &&
		    type == EVENT_DELIM && (strcmp(token, ")") == 0)) {
			char *new_atom;

			if (left->type != PRINT_ATOM) {
				do_warning("bad pointer type");
				goto out_free;
			}
			new_atom = realloc(left->atom.atom,
					    strlen(left->atom.atom) + 3);
			if (!new_atom)
				goto out_warn_free;

			left->atom.atom = new_atom;
			strcat(left->atom.atom, " *");
			free(arg->op.op);
			*arg = *left;
			free(left);

			return type;
		}

		right = alloc_arg();
		if (!right)
			goto out_warn_free;

		type = process_arg_token(event, right, tok, type);
		arg->op.right = right;

	} else if (strcmp(token, "[") == 0) {

		left = alloc_arg();
		if (!left)
			goto out_warn_free;

		*left = *arg;

		arg->type = PRINT_OP;
		arg->op.op = token;
		arg->op.left = left;

		arg->op.prio = 0;

		/* it will set arg->op.right */
		type = process_array(event, arg, tok);

	} else {
		do_warning("unknown op '%s'", token);
		event->flags |= EVENT_FL_FAILED;
		/* the arg is now the left side */
		goto out_free;
	}

	if (type == EVENT_OP && strcmp(*tok, ":") != 0) {
		int prio;

		/* higher prios need to be closer to the root */
		prio = get_op_prio(*tok);

		if (prio > arg->op.prio)
			return process_op(event, arg, tok);

		return process_op(event, right, tok);
	}

	return type;

out_warn_free:
	do_warning("%s: not enough memory!", __func__);
out_free:
	free_token(token);
	*tok = NULL;
	return EVENT_ERROR;
}

static enum event_type
process_entry(struct event_format *event __maybe_unused, struct print_arg *arg,
	      char **tok)
{
	enum event_type type;
	char *field;
	char *token;

	if (read_expected(EVENT_OP, "->") < 0)
		goto out_err;

	if (read_expect_type(EVENT_ITEM, &token) < 0)
		goto out_free;
	field = token;

	arg->type = PRINT_FIELD;
	arg->field.name = field;

	if (is_flag_field) {
		arg->field.field = pevent_find_any_field(event, arg->field.name);
		arg->field.field->flags |= FIELD_IS_FLAG;
		is_flag_field = 0;
	} else if (is_symbolic_field) {
		arg->field.field = pevent_find_any_field(event, arg->field.name);
		arg->field.field->flags |= FIELD_IS_SYMBOLIC;
		is_symbolic_field = 0;
	}

	type = read_token(&token);
	*tok = token;

	return type;

 out_free:
	free_token(token);
 out_err:
	*tok = NULL;
	return EVENT_ERROR;
}

static char *arg_eval (struct print_arg *arg);

static unsigned long long
eval_type_str(unsigned long long val, const char *type, int pointer)
{
	int sign = 0;
	char *ref;
	int len;

	len = strlen(type);

	if (pointer) {

		if (type[len-1] != '*') {
			do_warning("pointer expected with non pointer type");
			return val;
		}

		ref = malloc(len);
		if (!ref) {
			do_warning("%s: not enough memory!", __func__);
			return val;
		}
		memcpy(ref, type, len);

		/* chop off the " *" */
		ref[len - 2] = 0;

		val = eval_type_str(val, ref, 0);
		free(ref);
		return val;
	}

	/* check if this is a pointer */
	if (type[len - 1] == '*')
		return val;

	/* Try to figure out the arg size*/
	if (strncmp(type, "struct", 6) == 0)
		/* all bets off */
		return val;

	if (strcmp(type, "u8") == 0)
		return val & 0xff;

	if (strcmp(type, "u16") == 0)
		return val & 0xffff;

	if (strcmp(type, "u32") == 0)
		return val & 0xffffffff;

	if (strcmp(type, "u64") == 0 ||
	    strcmp(type, "s64"))
		return val;

	if (strcmp(type, "s8") == 0)
		return (unsigned long long)(char)val & 0xff;

	if (strcmp(type, "s16") == 0)
		return (unsigned long long)(short)val & 0xffff;

	if (strcmp(type, "s32") == 0)
		return (unsigned long long)(int)val & 0xffffffff;

	if (strncmp(type, "unsigned ", 9) == 0) {
		sign = 0;
		type += 9;
	}

	if (strcmp(type, "char") == 0) {
		if (sign)
			return (unsigned long long)(char)val & 0xff;
		else
			return val & 0xff;
	}

	if (strcmp(type, "short") == 0) {
		if (sign)
			return (unsigned long long)(short)val & 0xffff;
		else
			return val & 0xffff;
	}

	if (strcmp(type, "int") == 0) {
		if (sign)
			return (unsigned long long)(int)val & 0xffffffff;
		else
			return val & 0xffffffff;
	}

	return val;
}

/*
 * Try to figure out the type.
 */
static unsigned long long
eval_type(unsigned long long val, struct print_arg *arg, int pointer)
{
	if (arg->type != PRINT_TYPE) {
		do_warning("expected type argument");
		return 0;
	}

	return eval_type_str(val, arg->typecast.type, pointer);
}

static int arg_num_eval(struct print_arg *arg, long long *val)
{
	long long left, right;
	int ret = 1;

	switch (arg->type) {
	case PRINT_ATOM:
		*val = strtoll(arg->atom.atom, NULL, 0);
		break;
	case PRINT_TYPE:
		ret = arg_num_eval(arg->typecast.item, val);
		if (!ret)
			break;
		*val = eval_type(*val, arg, 0);
		break;
	case PRINT_OP:
		switch (arg->op.op[0]) {
		case '|':
			ret = arg_num_eval(arg->op.left, &left);
			if (!ret)
				break;
			ret = arg_num_eval(arg->op.right, &right);
			if (!ret)
				break;
			if (arg->op.op[1])
				*val = left || right;
			else
				*val = left | right;
			break;
		case '&':
			ret = arg_num_eval(arg->op.left, &left);
			if (!ret)
				break;
			ret = arg_num_eval(arg->op.right, &right);
			if (!ret)
				break;
			if (arg->op.op[1])
				*val = left && right;
			else
				*val = left & right;
			break;
		case '<':
			ret = arg_num_eval(arg->op.left, &left);
			if (!ret)
				break;
			ret = arg_num_eval(arg->op.right, &right);
			if (!ret)
				break;
			switch (arg->op.op[1]) {
			case 0:
				*val = left < right;
				break;
			case '<':
				*val = left << right;
				break;
			case '=':
				*val = left <= right;
				break;
			default:
				do_warning("unknown op '%s'", arg->op.op);
				ret = 0;
			}
			break;
		case '>':
			ret = arg_num_eval(arg->op.left, &left);
			if (!ret)
				break;
			ret = arg_num_eval(arg->op.right, &right);
			if (!ret)
				break;
			switch (arg->op.op[1]) {
			case 0:
				*val = left > right;
				break;
			case '>':
				*val = left >> right;
				break;
			case '=':
				*val = left >= right;
				break;
			default:
				do_warning("unknown op '%s'", arg->op.op);
				ret = 0;
			}
			break;
		case '=':
			ret = arg_num_eval(arg->op.left, &left);
			if (!ret)
				break;
			ret = arg_num_eval(arg->op.right, &right);
			if (!ret)
				break;

			if (arg->op.op[1] != '=') {
				do_warning("unknown op '%s'", arg->op.op);
				ret = 0;
			} else
				*val = left == right;
			break;
		case '!':
			ret = arg_num_eval(arg->op.left, &left);
			if (!ret)
				break;
			ret = arg_num_eval(arg->op.right, &right);
			if (!ret)
				break;

			switch (arg->op.op[1]) {
			case '=':
				*val = left != right;
				break;
			default:
				do_warning("unknown op '%s'", arg->op.op);
				ret = 0;
			}
			break;
		case '-':
			/* check for negative */
			if (arg->op.left->type == PRINT_NULL)
				left = 0;
			else
				ret = arg_num_eval(arg->op.left, &left);
			if (!ret)
				break;
			ret = arg_num_eval(arg->op.right, &right);
			if (!ret)
				break;
			*val = left - right;
			break;
		case '+':
			if (arg->op.left->type == PRINT_NULL)
				left = 0;
			else
				ret = arg_num_eval(arg->op.left, &left);
			if (!ret)
				break;
			ret = arg_num_eval(arg->op.right, &right);
			if (!ret)
				break;
			*val = left + right;
			break;
		default:
			do_warning("unknown op '%s'", arg->op.op);
			ret = 0;
		}
		break;

	case PRINT_NULL:
	case PRINT_FIELD ... PRINT_SYMBOL:
	case PRINT_STRING:
	case PRINT_BSTRING:
	default:
		do_warning("invalid eval type %d", arg->type);
		ret = 0;

	}
	return ret;
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
		if (!arg_num_eval(arg, &val))
			break;
		sprintf(buf, "%lld", val);
		return buf;

	case PRINT_NULL:
	case PRINT_FIELD ... PRINT_SYMBOL:
	case PRINT_STRING:
	case PRINT_BSTRING:
	default:
		do_warning("invalid eval type %d", arg->type);
		break;
	}

	return NULL;
}

static enum event_type
process_fields(struct event_format *event, struct print_flag_sym **list, char **tok)
{
	enum event_type type;
	struct print_arg *arg = NULL;
	struct print_flag_sym *field;
	char *token = *tok;
	char *value;

	do {
		free_token(token);
		type = read_token_item(&token);
		if (test_type_token(type, token, EVENT_OP, "{"))
			break;

		arg = alloc_arg();
		if (!arg)
			goto out_free;

		free_token(token);
		type = process_arg(event, arg, &token);

		if (type == EVENT_OP)
			type = process_op(event, arg, &token);

		if (type == EVENT_ERROR)
			goto out_free;

		if (test_type_token(type, token, EVENT_DELIM, ","))
			goto out_free;

		field = calloc(1, sizeof(*field));
		if (!field)
			goto out_free;

		value = arg_eval(arg);
		if (value == NULL)
			goto out_free_field;
		field->value = strdup(value);
		if (field->value == NULL)
			goto out_free_field;

		free_arg(arg);
		arg = alloc_arg();
		if (!arg)
			goto out_free;

		free_token(token);
		type = process_arg(event, arg, &token);
		if (test_type_token(type, token, EVENT_OP, "}"))
			goto out_free_field;

		value = arg_eval(arg);
		if (value == NULL)
			goto out_free_field;
		field->str = strdup(value);
		if (field->str == NULL)
			goto out_free_field;
		free_arg(arg);
		arg = NULL;

		*list = field;
		list = &field->next;

		free_token(token);
		type = read_token_item(&token);
	} while (type == EVENT_DELIM && strcmp(token, ",") == 0);

	*tok = token;
	return type;

out_free_field:
	free_flag_sym(field);
out_free:
	free_arg(arg);
	free_token(token);
	*tok = NULL;

	return EVENT_ERROR;
}

static enum event_type
process_flags(struct event_format *event, struct print_arg *arg, char **tok)
{
	struct print_arg *field;
	enum event_type type;
	char *token;

	memset(arg, 0, sizeof(*arg));
	arg->type = PRINT_FLAGS;

	field = alloc_arg();
	if (!field) {
		do_warning("%s: not enough memory!", __func__);
		goto out_free;
	}

	type = process_arg(event, field, &token);

	/* Handle operations in the first argument */
	while (type == EVENT_OP)
		type = process_op(event, field, &token);

	if (test_type_token(type, token, EVENT_DELIM, ","))
		goto out_free_field;
	free_token(token);

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

out_free_field:
	free_arg(field);
out_free:
	free_token(token);
	*tok = NULL;
	return EVENT_ERROR;
}

static enum event_type
process_symbols(struct event_format *event, struct print_arg *arg, char **tok)
{
	struct print_arg *field;
	enum event_type type;
	char *token;

	memset(arg, 0, sizeof(*arg));
	arg->type = PRINT_SYMBOL;

	field = alloc_arg();
	if (!field) {
		do_warning("%s: not enough memory!", __func__);
		goto out_free;
	}

	type = process_arg(event, field, &token);
	if (test_type_token(type, token, EVENT_DELIM, ","))
		goto out_free_field;

	arg->symbol.field = field;

	type = process_fields(event, &arg->symbol.symbols, &token);
	if (test_type_token(type, token, EVENT_DELIM, ")"))
		goto out_free;

	free_token(token);
	type = read_token_item(tok);
	return type;

out_free_field:
	free_arg(field);
out_free:
	free_token(token);
	*tok = NULL;
	return EVENT_ERROR;
}

static enum event_type
process_hex(struct event_format *event, struct print_arg *arg, char **tok)
{
	struct print_arg *field;
	enum event_type type;
	char *token;

	memset(arg, 0, sizeof(*arg));
	arg->type = PRINT_HEX;

	field = alloc_arg();
	if (!field) {
		do_warning("%s: not enough memory!", __func__);
		goto out_free;
	}

	type = process_arg(event, field, &token);

	if (test_type_token(type, token, EVENT_DELIM, ","))
		goto out_free;

	arg->hex.field = field;

	free_token(token);

	field = alloc_arg();
	if (!field) {
		do_warning("%s: not enough memory!", __func__);
		*tok = NULL;
		return EVENT_ERROR;
	}

	type = process_arg(event, field, &token);

	if (test_type_token(type, token, EVENT_DELIM, ")"))
		goto out_free;

	arg->hex.size = field;

	free_token(token);
	type = read_token_item(tok);
	return type;

 out_free:
	free_arg(field);
	free_token(token);
	*tok = NULL;
	return EVENT_ERROR;
}

static enum event_type
process_dynamic_array(struct event_format *event, struct print_arg *arg, char **tok)
{
	struct format_field *field;
	enum event_type type;
	char *token;

	memset(arg, 0, sizeof(*arg));
	arg->type = PRINT_DYNAMIC_ARRAY;

	/*
	 * The item within the parenthesis is another field that holds
	 * the index into where the array starts.
	 */
	type = read_token(&token);
	*tok = token;
	if (type != EVENT_ITEM)
		goto out_free;

	/* Find the field */

	field = pevent_find_field(event, token);
	if (!field)
		goto out_free;

	arg->dynarray.field = field;
	arg->dynarray.index = 0;

	if (read_expected(EVENT_DELIM, ")") < 0)
		goto out_free;

	free_token(token);
	type = read_token_item(&token);
	*tok = token;
	if (type != EVENT_OP || strcmp(token, "[") != 0)
		return type;

	free_token(token);
	arg = alloc_arg();
	if (!field) {
		do_warning("%s: not enough memory!", __func__);
		*tok = NULL;
		return EVENT_ERROR;
	}

	type = process_arg(event, arg, &token);
	if (type == EVENT_ERROR)
		goto out_free_arg;

	if (!test_type_token(type, token, EVENT_OP, "]"))
		goto out_free_arg;

	free_token(token);
	type = read_token_item(tok);
	return type;

 out_free_arg:
	free_arg(arg);
 out_free:
	free_token(token);
	*tok = NULL;
	return EVENT_ERROR;
}

static enum event_type
process_paren(struct event_format *event, struct print_arg *arg, char **tok)
{
	struct print_arg *item_arg;
	enum event_type type;
	char *token;

	type = process_arg(event, arg, &token);

	if (type == EVENT_ERROR)
		goto out_free;

	if (type == EVENT_OP)
		type = process_op(event, arg, &token);

	if (type == EVENT_ERROR)
		goto out_free;

	if (test_type_token(type, token, EVENT_DELIM, ")"))
		goto out_free;

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
		if (arg->type != PRINT_ATOM) {
			do_warning("previous needed to be PRINT_ATOM");
			goto out_free;
		}

		item_arg = alloc_arg();
		if (!item_arg) {
			do_warning("%s: not enough memory!", __func__);
			goto out_free;
		}

		arg->type = PRINT_TYPE;
		arg->typecast.type = arg->atom.atom;
		arg->typecast.item = item_arg;
		type = process_arg_token(event, item_arg, &token, type);

	}

	*tok = token;
	return type;

 out_free:
	free_token(token);
	*tok = NULL;
	return EVENT_ERROR;
}


static enum event_type
process_str(struct event_format *event __maybe_unused, struct print_arg *arg,
	    char **tok)
{
	enum event_type type;
	char *token;

	if (read_expect_type(EVENT_ITEM, &token) < 0)
		goto out_free;

	arg->type = PRINT_STRING;
	arg->string.string = token;
	arg->string.offset = -1;

	if (read_expected(EVENT_DELIM, ")") < 0)
		goto out_err;

	type = read_token(&token);
	*tok = token;

	return type;

 out_free:
	free_token(token);
 out_err:
	*tok = NULL;
	return EVENT_ERROR;
}

static struct pevent_function_handler *
find_func_handler(struct pevent *pevent, char *func_name)
{
	struct pevent_function_handler *func;

	if (!pevent)
		return NULL;

	for (func = pevent->func_handlers; func; func = func->next) {
		if (strcmp(func->name, func_name) == 0)
			break;
	}

	return func;
}

static void remove_func_handler(struct pevent *pevent, char *func_name)
{
	struct pevent_function_handler *func;
	struct pevent_function_handler **next;

	next = &pevent->func_handlers;
	while ((func = *next)) {
		if (strcmp(func->name, func_name) == 0) {
			*next = func->next;
			free_func_handle(func);
			break;
		}
		next = &func->next;
	}
}

static enum event_type
process_func_handler(struct event_format *event, struct pevent_function_handler *func,
		     struct print_arg *arg, char **tok)
{
	struct print_arg **next_arg;
	struct print_arg *farg;
	enum event_type type;
	char *token;
	char *test;
	int i;

	arg->type = PRINT_FUNC;
	arg->func.func = func;

	*tok = NULL;

	next_arg = &(arg->func.args);
	for (i = 0; i < func->nr_args; i++) {
		farg = alloc_arg();
		if (!farg) {
			do_warning("%s: not enough memory!", __func__);
			return EVENT_ERROR;
		}

		type = process_arg(event, farg, &token);
		if (i < (func->nr_args - 1))
			test = ",";
		else
			test = ")";

		if (test_type_token(type, token, EVENT_DELIM, test)) {
			free_arg(farg);
			free_token(token);
			return EVENT_ERROR;
		}

		*next_arg = farg;
		next_arg = &(farg->next);
		free_token(token);
	}

	type = read_token(&token);
	*tok = token;

	return type;
}

static enum event_type
process_function(struct event_format *event, struct print_arg *arg,
		 char *token, char **tok)
{
	struct pevent_function_handler *func;

	if (strcmp(token, "__print_flags") == 0) {
		free_token(token);
		is_flag_field = 1;
		return process_flags(event, arg, tok);
	}
	if (strcmp(token, "__print_symbolic") == 0) {
		free_token(token);
		is_symbolic_field = 1;
		return process_symbols(event, arg, tok);
	}
	if (strcmp(token, "__print_hex") == 0) {
		free_token(token);
		return process_hex(event, arg, tok);
	}
	if (strcmp(token, "__get_str") == 0) {
		free_token(token);
		return process_str(event, arg, tok);
	}
	if (strcmp(token, "__get_dynamic_array") == 0) {
		free_token(token);
		return process_dynamic_array(event, arg, tok);
	}

	func = find_func_handler(event->pevent, token);
	if (func) {
		free_token(token);
		return process_func_handler(event, func, arg, tok);
	}

	do_warning("function %s not defined", token);
	free_token(token);
	return EVENT_ERROR;
}

static enum event_type
process_arg_token(struct event_format *event, struct print_arg *arg,
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
			break;
		}
		atom = token;
		/* test the next token */
		type = read_token_item(&token);

		/*
		 * If the next token is a parenthesis, then this
		 * is a function.
		 */
		if (type == EVENT_DELIM && strcmp(token, "(") == 0) {
			free_token(token);
			token = NULL;
			/* this will free atom. */
			type = process_function(event, arg, atom, &token);
			break;
		}
		/* atoms can be more than one token long */
		while (type == EVENT_ITEM) {
			char *new_atom;
			new_atom = realloc(atom,
					   strlen(atom) + strlen(token) + 2);
			if (!new_atom) {
				free(atom);
				*tok = NULL;
				free_token(token);
				return EVENT_ERROR;
			}
			atom = new_atom;
			strcat(atom, " ");
			strcat(atom, token);
			free_token(token);
			type = read_token_item(&token);
		}

		arg->type = PRINT_ATOM;
		arg->atom.atom = atom;
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

		/* On error, the op is freed */
		if (type == EVENT_ERROR)
			arg->op.op = NULL;

		/* return error type if errored */
		break;

	case EVENT_ERROR ... EVENT_NEWLINE:
	default:
		do_warning("unexpected type %d", type);
		return EVENT_ERROR;
	}
	*tok = token;

	return type;
}

static int event_read_print_args(struct event_format *event, struct print_arg **list)
{
	enum event_type type = EVENT_ERROR;
	struct print_arg *arg;
	char *token;
	int args = 0;

	do {
		if (type == EVENT_NEWLINE) {
			type = read_token_item(&token);
			continue;
		}

		arg = alloc_arg();
		if (!arg) {
			do_warning("%s: not enough memory!", __func__);
			return -1;
		}

		type = process_arg(event, arg, &token);

		if (type == EVENT_ERROR) {
			free_token(token);
			free_arg(arg);
			return -1;
		}

		*list = arg;
		args++;

		if (type == EVENT_OP) {
			type = process_op(event, arg, &token);
			free_token(token);
			if (type == EVENT_ERROR) {
				*list = NULL;
				free_arg(arg);
				return -1;
			}
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

	if (type != EVENT_NONE && type != EVENT_ERROR)
		free_token(token);

	return args;
}

static int event_read_print(struct event_format *event)
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

	/* Handle concatenation of print lines */
	if (type == EVENT_DQUOTE) {
		char *cat;

		if (asprintf(&cat, "%s%s", event->print_fmt.format, token) < 0)
			goto fail;
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

/**
 * pevent_find_common_field - return a common field by event
 * @event: handle for the event
 * @name: the name of the common field to return
 *
 * Returns a common field from the event by the given @name.
 * This only searchs the common fields and not all field.
 */
struct format_field *
pevent_find_common_field(struct event_format *event, const char *name)
{
	struct format_field *format;

	for (format = event->format.common_fields;
	     format; format = format->next) {
		if (strcmp(format->name, name) == 0)
			break;
	}

	return format;
}

/**
 * pevent_find_field - find a non-common field
 * @event: handle for the event
 * @name: the name of the non-common field
 *
 * Returns a non-common field by the given @name.
 * This does not search common fields.
 */
struct format_field *
pevent_find_field(struct event_format *event, const char *name)
{
	struct format_field *format;

	for (format = event->format.fields;
	     format; format = format->next) {
		if (strcmp(format->name, name) == 0)
			break;
	}

	return format;
}

/**
 * pevent_find_any_field - find any field by name
 * @event: handle for the event
 * @name: the name of the field
 *
 * Returns a field by the given @name.
 * This searchs the common field names first, then
 * the non-common ones if a common one was not found.
 */
struct format_field *
pevent_find_any_field(struct event_format *event, const char *name)
{
	struct format_field *format;

	format = pevent_find_common_field(event, name);
	if (format)
		return format;
	return pevent_find_field(event, name);
}

/**
 * pevent_read_number - read a number from data
 * @pevent: handle for the pevent
 * @ptr: the raw data
 * @size: the size of the data that holds the number
 *
 * Returns the number (converted to host) from the
 * raw data.
 */
unsigned long long pevent_read_number(struct pevent *pevent,
				      const void *ptr, int size)
{
	switch (size) {
	case 1:
		return *(unsigned char *)ptr;
	case 2:
		return data2host2(pevent, ptr);
	case 4:
		return data2host4(pevent, ptr);
	case 8:
		return data2host8(pevent, ptr);
	default:
		/* BUG! */
		return 0;
	}
}

/**
 * pevent_read_number_field - read a number from data
 * @field: a handle to the field
 * @data: the raw data to read
 * @value: the value to place the number in
 *
 * Reads raw data according to a field offset and size,
 * and translates it into @value.
 *
 * Returns 0 on success, -1 otherwise.
 */
int pevent_read_number_field(struct format_field *field, const void *data,
			     unsigned long long *value)
{
	if (!field)
		return -1;
	switch (field->size) {
	case 1:
	case 2:
	case 4:
	case 8:
		*value = pevent_read_number(field->event->pevent,
					    data + field->offset, field->size);
		return 0;
	default:
		return -1;
	}
}

static int get_common_info(struct pevent *pevent,
			   const char *type, int *offset, int *size)
{
	struct event_format *event;
	struct format_field *field;

	/*
	 * All events should have the same common elements.
	 * Pick any event to find where the type is;
	 */
	if (!pevent->events) {
		do_warning("no event_list!");
		return -1;
	}

	event = pevent->events[0];
	field = pevent_find_common_field(event, type);
	if (!field)
		return -1;

	*offset = field->offset;
	*size = field->size;

	return 0;
}

static int __parse_common(struct pevent *pevent, void *data,
			  int *size, int *offset, const char *name)
{
	int ret;

	if (!*size) {
		ret = get_common_info(pevent, name, offset, size);
		if (ret < 0)
			return ret;
	}
	return pevent_read_number(pevent, data + *offset, *size);
}

static int trace_parse_common_type(struct pevent *pevent, void *data)
{
	return __parse_common(pevent, data,
			      &pevent->type_size, &pevent->type_offset,
			      "common_type");
}

static int parse_common_pid(struct pevent *pevent, void *data)
{
	return __parse_common(pevent, data,
			      &pevent->pid_size, &pevent->pid_offset,
			      "common_pid");
}

static int parse_common_pc(struct pevent *pevent, void *data)
{
	return __parse_common(pevent, data,
			      &pevent->pc_size, &pevent->pc_offset,
			      "common_preempt_count");
}

static int parse_common_flags(struct pevent *pevent, void *data)
{
	return __parse_common(pevent, data,
			      &pevent->flags_size, &pevent->flags_offset,
			      "common_flags");
}

static int parse_common_lock_depth(struct pevent *pevent, void *data)
{
	return __parse_common(pevent, data,
			      &pevent->ld_size, &pevent->ld_offset,
			      "common_lock_depth");
}

static int parse_common_migrate_disable(struct pevent *pevent, void *data)
{
	return __parse_common(pevent, data,
			      &pevent->ld_size, &pevent->ld_offset,
			      "common_migrate_disable");
}

static int events_id_cmp(const void *a, const void *b);

/**
 * pevent_find_event - find an event by given id
 * @pevent: a handle to the pevent
 * @id: the id of the event
 *
 * Returns an event that has a given @id.
 */
struct event_format *pevent_find_event(struct pevent *pevent, int id)
{
	struct event_format **eventptr;
	struct event_format key;
	struct event_format *pkey = &key;

	/* Check cache first */
	if (pevent->last_event && pevent->last_event->id == id)
		return pevent->last_event;

	key.id = id;

	eventptr = bsearch(&pkey, pevent->events, pevent->nr_events,
			   sizeof(*pevent->events), events_id_cmp);

	if (eventptr) {
		pevent->last_event = *eventptr;
		return *eventptr;
	}

	return NULL;
}

/**
 * pevent_find_event_by_name - find an event by given name
 * @pevent: a handle to the pevent
 * @sys: the system name to search for
 * @name: the name of the event to search for
 *
 * This returns an event with a given @name and under the system
 * @sys. If @sys is NULL the first event with @name is returned.
 */
struct event_format *
pevent_find_event_by_name(struct pevent *pevent,
			  const char *sys, const char *name)
{
	struct event_format *event;
	int i;

	if (pevent->last_event &&
	    strcmp(pevent->last_event->name, name) == 0 &&
	    (!sys || strcmp(pevent->last_event->system, sys) == 0))
		return pevent->last_event;

	for (i = 0; i < pevent->nr_events; i++) {
		event = pevent->events[i];
		if (strcmp(event->name, name) == 0) {
			if (!sys)
				break;
			if (strcmp(event->system, sys) == 0)
				break;
		}
	}
	if (i == pevent->nr_events)
		event = NULL;

	pevent->last_event = event;
	return event;
}

static unsigned long long
eval_num_arg(void *data, int size, struct event_format *event, struct print_arg *arg)
{
	struct pevent *pevent = event->pevent;
	unsigned long long val = 0;
	unsigned long long left, right;
	struct print_arg *typearg = NULL;
	struct print_arg *larg;
	unsigned long offset;
	unsigned int field_size;

	switch (arg->type) {
	case PRINT_NULL:
		/* ?? */
		return 0;
	case PRINT_ATOM:
		return strtoull(arg->atom.atom, NULL, 0);
	case PRINT_FIELD:
		if (!arg->field.field) {
			arg->field.field = pevent_find_any_field(event, arg->field.name);
			if (!arg->field.field)
				goto out_warning_field;
			
		}
		/* must be a number */
		val = pevent_read_number(pevent, data + arg->field.field->offset,
				arg->field.field->size);
		break;
	case PRINT_FLAGS:
	case PRINT_SYMBOL:
	case PRINT_HEX:
		break;
	case PRINT_TYPE:
		val = eval_num_arg(data, size, event, arg->typecast.item);
		return eval_type(val, arg, 0);
	case PRINT_STRING:
	case PRINT_BSTRING:
		return 0;
	case PRINT_FUNC: {
		struct trace_seq s;
		trace_seq_init(&s);
		val = process_defined_func(&s, data, size, event, arg);
		trace_seq_destroy(&s);
		return val;
	}
	case PRINT_OP:
		if (strcmp(arg->op.op, "[") == 0) {
			/*
			 * Arrays are special, since we don't want
			 * to read the arg as is.
			 */
			right = eval_num_arg(data, size, event, arg->op.right);

			/* handle typecasts */
			larg = arg->op.left;
			while (larg->type == PRINT_TYPE) {
				if (!typearg)
					typearg = larg;
				larg = larg->typecast.item;
			}

			/* Default to long size */
			field_size = pevent->long_size;

			switch (larg->type) {
			case PRINT_DYNAMIC_ARRAY:
				offset = pevent_read_number(pevent,
						   data + larg->dynarray.field->offset,
						   larg->dynarray.field->size);
				if (larg->dynarray.field->elementsize)
					field_size = larg->dynarray.field->elementsize;
				/*
				 * The actual length of the dynamic array is stored
				 * in the top half of the field, and the offset
				 * is in the bottom half of the 32 bit field.
				 */
				offset &= 0xffff;
				offset += right;
				break;
			case PRINT_FIELD:
				if (!larg->field.field) {
					larg->field.field =
						pevent_find_any_field(event, larg->field.name);
					if (!larg->field.field) {
						arg = larg;
						goto out_warning_field;
					}
				}
				field_size = larg->field.field->elementsize;
				offset = larg->field.field->offset +
					right * larg->field.field->elementsize;
				break;
			default:
				goto default_op; /* oops, all bets off */
			}
			val = pevent_read_number(pevent,
						 data + offset, field_size);
			if (typearg)
				val = eval_type(val, typearg, 1);
			break;
		} else if (strcmp(arg->op.op, "?") == 0) {
			left = eval_num_arg(data, size, event, arg->op.left);
			arg = arg->op.right;
			if (left)
				val = eval_num_arg(data, size, event, arg->op.left);
			else
				val = eval_num_arg(data, size, event, arg->op.right);
			break;
		}
 default_op:
		left = eval_num_arg(data, size, event, arg->op.left);
		right = eval_num_arg(data, size, event, arg->op.right);
		switch (arg->op.op[0]) {
		case '!':
			switch (arg->op.op[1]) {
			case 0:
				val = !right;
				break;
			case '=':
				val = left != right;
				break;
			default:
				goto out_warning_op;
			}
			break;
		case '~':
			val = ~right;
			break;
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
				goto out_warning_op;
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
				goto out_warning_op;
			}
			break;
		case '=':
			if (arg->op.op[1] != '=')
				goto out_warning_op;

			val = left == right;
			break;
		case '-':
			val = left - right;
			break;
		case '+':
			val = left + right;
			break;
		case '/':
			val = left / right;
			break;
		case '*':
			val = left * right;
			break;
		default:
			goto out_warning_op;
		}
		break;
	default: /* not sure what to do there */
		return 0;
	}
	return val;

out_warning_op:
	do_warning("%s: unknown op '%s'", __func__, arg->op.op);
	return 0;

out_warning_field:
	do_warning("%s: field %s not found", __func__, arg->field.name);
	return 0;
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

static unsigned long long eval_flag(const char *flag)
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

static void print_str_to_seq(struct trace_seq *s, const char *format,
			     int len_arg, const char *str)
{
	if (len_arg >= 0)
		trace_seq_printf(s, format, len_arg, str);
	else
		trace_seq_printf(s, format, str);
}

static void print_str_arg(struct trace_seq *s, void *data, int size,
			  struct event_format *event, const char *format,
			  int len_arg, struct print_arg *arg)
{
	struct pevent *pevent = event->pevent;
	struct print_flag_sym *flag;
	struct format_field *field;
	unsigned long long val, fval;
	unsigned long addr;
	char *str;
	unsigned char *hex;
	int print;
	int i, len;

	switch (arg->type) {
	case PRINT_NULL:
		/* ?? */
		return;
	case PRINT_ATOM:
		print_str_to_seq(s, format, len_arg, arg->atom.atom);
		return;
	case PRINT_FIELD:
		field = arg->field.field;
		if (!field) {
			field = pevent_find_any_field(event, arg->field.name);
			if (!field) {
				str = arg->field.name;
				goto out_warning_field;
			}
			arg->field.field = field;
		}
		/* Zero sized fields, mean the rest of the data */
		len = field->size ? : size - field->offset;

		/*
		 * Some events pass in pointers. If this is not an array
		 * and the size is the same as long_size, assume that it
		 * is a pointer.
		 */
		if (!(field->flags & FIELD_IS_ARRAY) &&
		    field->size == pevent->long_size) {
			addr = *(unsigned long *)(data + field->offset);
			trace_seq_printf(s, "%lx", addr);
			break;
		}
		str = malloc(len + 1);
		if (!str) {
			do_warning("%s: not enough memory!", __func__);
			return;
		}
		memcpy(str, data + field->offset, len);
		str[len] = 0;
		print_str_to_seq(s, format, len_arg, str);
		free(str);
		break;
	case PRINT_FLAGS:
		val = eval_num_arg(data, size, event, arg->flags.field);
		print = 0;
		for (flag = arg->flags.flags; flag; flag = flag->next) {
			fval = eval_flag(flag->value);
			if (!val && !fval) {
				print_str_to_seq(s, format, len_arg, flag->str);
				break;
			}
			if (fval && (val & fval) == fval) {
				if (print && arg->flags.delim)
					trace_seq_puts(s, arg->flags.delim);
				print_str_to_seq(s, format, len_arg, flag->str);
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
				print_str_to_seq(s, format, len_arg, flag->str);
				break;
			}
		}
		break;
	case PRINT_HEX:
		field = arg->hex.field->field.field;
		if (!field) {
			str = arg->hex.field->field.name;
			field = pevent_find_any_field(event, str);
			if (!field)
				goto out_warning_field;
			arg->hex.field->field.field = field;
		}
		hex = data + field->offset;
		len = eval_num_arg(data, size, event, arg->hex.size);
		for (i = 0; i < len; i++) {
			if (i)
				trace_seq_putc(s, ' ');
			trace_seq_printf(s, "%02x", hex[i]);
		}
		break;

	case PRINT_TYPE:
		break;
	case PRINT_STRING: {
		int str_offset;

		if (arg->string.offset == -1) {
			struct format_field *f;

			f = pevent_find_any_field(event, arg->string.string);
			arg->string.offset = f->offset;
		}
		str_offset = data2host4(pevent, data + arg->string.offset);
		str_offset &= 0xffff;
		print_str_to_seq(s, format, len_arg, ((char *)data) + str_offset);
		break;
	}
	case PRINT_BSTRING:
		print_str_to_seq(s, format, len_arg, arg->string.string);
		break;
	case PRINT_OP:
		/*
		 * The only op for string should be ? :
		 */
		if (arg->op.op[0] != '?')
			return;
		val = eval_num_arg(data, size, event, arg->op.left);
		if (val)
			print_str_arg(s, data, size, event,
				      format, len_arg, arg->op.right->op.left);
		else
			print_str_arg(s, data, size, event,
				      format, len_arg, arg->op.right->op.right);
		break;
	case PRINT_FUNC:
		process_defined_func(s, data, size, event, arg);
		break;
	default:
		/* well... */
		break;
	}

	return;

out_warning_field:
	do_warning("%s: field %s not found", __func__, arg->field.name);
}

static unsigned long long
process_defined_func(struct trace_seq *s, void *data, int size,
		     struct event_format *event, struct print_arg *arg)
{
	struct pevent_function_handler *func_handle = arg->func.func;
	struct pevent_func_params *param;
	unsigned long long *args;
	unsigned long long ret;
	struct print_arg *farg;
	struct trace_seq str;
	struct save_str {
		struct save_str *next;
		char *str;
	} *strings = NULL, *string;
	int i;

	if (!func_handle->nr_args) {
		ret = (*func_handle->func)(s, NULL);
		goto out;
	}

	farg = arg->func.args;
	param = func_handle->params;

	ret = ULLONG_MAX;
	args = malloc(sizeof(*args) * func_handle->nr_args);
	if (!args)
		goto out;

	for (i = 0; i < func_handle->nr_args; i++) {
		switch (param->type) {
		case PEVENT_FUNC_ARG_INT:
		case PEVENT_FUNC_ARG_LONG:
		case PEVENT_FUNC_ARG_PTR:
			args[i] = eval_num_arg(data, size, event, farg);
			break;
		case PEVENT_FUNC_ARG_STRING:
			trace_seq_init(&str);
			print_str_arg(&str, data, size, event, "%s", -1, farg);
			trace_seq_terminate(&str);
			string = malloc(sizeof(*string));
			if (!string) {
				do_warning("%s(%d): malloc str", __func__, __LINE__);
				goto out_free;
			}
			string->next = strings;
			string->str = strdup(str.buffer);
			if (!string->str) {
				free(string);
				do_warning("%s(%d): malloc str", __func__, __LINE__);
				goto out_free;
			}
			args[i] = (uintptr_t)string->str;
			strings = string;
			trace_seq_destroy(&str);
			break;
		default:
			/*
			 * Something went totally wrong, this is not
			 * an input error, something in this code broke.
			 */
			do_warning("Unexpected end of arguments\n");
			goto out_free;
		}
		farg = farg->next;
		param = param->next;
	}

	ret = (*func_handle->func)(s, args);
out_free:
	free(args);
	while (strings) {
		string = strings;
		strings = string->next;
		free(string->str);
		free(string);
	}

 out:
	/* TBD : handle return type here */
	return ret;
}

static void free_args(struct print_arg *args)
{
	struct print_arg *next;

	while (args) {
		next = args->next;

		free_arg(args);
		args = next;
	}
}

static struct print_arg *make_bprint_args(char *fmt, void *data, int size, struct event_format *event)
{
	struct pevent *pevent = event->pevent;
	struct format_field *field, *ip_field;
	struct print_arg *args, *arg, **next;
	unsigned long long ip, val;
	char *ptr;
	void *bptr;
	int vsize;

	field = pevent->bprint_buf_field;
	ip_field = pevent->bprint_ip_field;

	if (!field) {
		field = pevent_find_field(event, "buf");
		if (!field) {
			do_warning("can't find buffer field for binary printk");
			return NULL;
		}
		ip_field = pevent_find_field(event, "ip");
		if (!ip_field) {
			do_warning("can't find ip field for binary printk");
			return NULL;
		}
		pevent->bprint_buf_field = field;
		pevent->bprint_ip_field = ip_field;
	}

	ip = pevent_read_number(pevent, data + ip_field->offset, ip_field->size);

	/*
	 * The first arg is the IP pointer.
	 */
	args = alloc_arg();
	if (!args) {
		do_warning("%s(%d): not enough memory!", __func__, __LINE__);
		return NULL;
	}
	arg = args;
	arg->next = NULL;
	next = &arg->next;

	arg->type = PRINT_ATOM;
		
	if (asprintf(&arg->atom.atom, "%lld", ip) < 0)
		goto out_free;

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
			case '.':
				goto process_again;
			case 'p':
				ls = 1;
				/* fall through */
			case 'd':
			case 'u':
			case 'x':
			case 'i':
				switch (ls) {
				case 0:
					vsize = 4;
					break;
				case 1:
					vsize = pevent->long_size;
					break;
				case 2:
					vsize = 8;
					break;
				default:
					vsize = ls; /* ? */
					break;
				}
			/* fall through */
			case '*':
				if (*ptr == '*')
					vsize = 4;

				/* the pointers are always 4 bytes aligned */
				bptr = (void *)(((unsigned long)bptr + 3) &
						~3);
				val = pevent_read_number(pevent, bptr, vsize);
				bptr += vsize;
				arg = alloc_arg();
				if (!arg) {
					do_warning("%s(%d): not enough memory!",
						   __func__, __LINE__);
					goto out_free;
				}
				arg->next = NULL;
				arg->type = PRINT_ATOM;
				if (asprintf(&arg->atom.atom, "%lld", val) < 0) {
					free(arg);
					goto out_free;
				}
				*next = arg;
				next = &arg->next;
				/*
				 * The '*' case means that an arg is used as the length.
				 * We need to continue to figure out for what.
				 */
				if (*ptr == '*')
					goto process_again;

				break;
			case 's':
				arg = alloc_arg();
				if (!arg) {
					do_warning("%s(%d): not enough memory!",
						   __func__, __LINE__);
					goto out_free;
				}
				arg->next = NULL;
				arg->type = PRINT_BSTRING;
				arg->string.string = strdup(bptr);
				if (!arg->string.string)
					goto out_free;
				bptr += strlen(bptr) + 1;
				*next = arg;
				next = &arg->next;
			default:
				break;
			}
		}
	}

	return args;

out_free:
	free_args(args);
	return NULL;
}

static char *
get_bprint_format(void *data, int size __maybe_unused,
		  struct event_format *event)
{
	struct pevent *pevent = event->pevent;
	unsigned long long addr;
	struct format_field *field;
	struct printk_map *printk;
	char *format;
	char *p;

	field = pevent->bprint_fmt_field;

	if (!field) {
		field = pevent_find_field(event, "fmt");
		if (!field) {
			do_warning("can't find format field for binary printk");
			return NULL;
		}
		pevent->bprint_fmt_field = field;
	}

	addr = pevent_read_number(pevent, data + field->offset, field->size);

	printk = find_printk(pevent, addr);
	if (!printk) {
		if (asprintf(&format, "%%pf : (NO FORMAT FOUND at %llx)\n", addr) < 0)
			return NULL;
		return format;
	}

	p = printk->printk;
	/* Remove any quotes. */
	if (*p == '"')
		p++;
	if (asprintf(&format, "%s : %s", "%pf", p) < 0)
		return NULL;
	/* remove ending quotes and new line since we will add one too */
	p = format + strlen(format) - 1;
	if (*p == '"')
		*p = 0;

	p -= 2;
	if (strcmp(p, "\\n") == 0)
		*p = 0;

	return format;
}

static void print_mac_arg(struct trace_seq *s, int mac, void *data, int size,
			  struct event_format *event, struct print_arg *arg)
{
	unsigned char *buf;
	char *fmt = "%.2x:%.2x:%.2x:%.2x:%.2x:%.2x";

	if (arg->type == PRINT_FUNC) {
		process_defined_func(s, data, size, event, arg);
		return;
	}

	if (arg->type != PRINT_FIELD) {
		trace_seq_printf(s, "ARG TYPE NOT FIELD BUT %d",
				 arg->type);
		return;
	}

	if (mac == 'm')
		fmt = "%.2x%.2x%.2x%.2x%.2x%.2x";
	if (!arg->field.field) {
		arg->field.field =
			pevent_find_any_field(event, arg->field.name);
		if (!arg->field.field) {
			do_warning("%s: field %s not found",
				   __func__, arg->field.name);
			return;
		}
	}
	if (arg->field.field->size != 6) {
		trace_seq_printf(s, "INVALIDMAC");
		return;
	}
	buf = data + arg->field.field->offset;
	trace_seq_printf(s, fmt, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
}

static int is_printable_array(char *p, unsigned int len)
{
	unsigned int i;

	for (i = 0; i < len && p[i]; i++)
		if (!isprint(p[i]))
		    return 0;
	return 1;
}

static void print_event_fields(struct trace_seq *s, void *data, int size,
			       struct event_format *event)
{
	struct format_field *field;
	unsigned long long val;
	unsigned int offset, len, i;

	field = event->format.fields;
	while (field) {
		trace_seq_printf(s, " %s=", field->name);
		if (field->flags & FIELD_IS_ARRAY) {
			offset = field->offset;
			len = field->size;
			if (field->flags & FIELD_IS_DYNAMIC) {
				val = pevent_read_number(event->pevent, data + offset, len);
				offset = val;
				len = offset >> 16;
				offset &= 0xffff;
			}
			if (field->flags & FIELD_IS_STRING &&
			    is_printable_array(data + offset, len)) {
				trace_seq_printf(s, "%s", (char *)data + offset);
			} else {
				trace_seq_puts(s, "ARRAY[");
				for (i = 0; i < len; i++) {
					if (i)
						trace_seq_puts(s, ", ");
					trace_seq_printf(s, "%02x",
							 *((unsigned char *)data + offset + i));
				}
				trace_seq_putc(s, ']');
				field->flags &= ~FIELD_IS_STRING;
			}
		} else {
			val = pevent_read_number(event->pevent, data + field->offset,
						 field->size);
			if (field->flags & FIELD_IS_POINTER) {
				trace_seq_printf(s, "0x%llx", val);
			} else if (field->flags & FIELD_IS_SIGNED) {
				switch (field->size) {
				case 4:
					/*
					 * If field is long then print it in hex.
					 * A long usually stores pointers.
					 */
					if (field->flags & FIELD_IS_LONG)
						trace_seq_printf(s, "0x%x", (int)val);
					else
						trace_seq_printf(s, "%d", (int)val);
					break;
				case 2:
					trace_seq_printf(s, "%2d", (short)val);
					break;
				case 1:
					trace_seq_printf(s, "%1d", (char)val);
					break;
				default:
					trace_seq_printf(s, "%lld", val);
				}
			} else {
				if (field->flags & FIELD_IS_LONG)
					trace_seq_printf(s, "0x%llx", val);
				else
					trace_seq_printf(s, "%llu", val);
			}
		}
		field = field->next;
	}
}

static void pretty_print(struct trace_seq *s, void *data, int size, struct event_format *event)
{
	struct pevent *pevent = event->pevent;
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
	int len_as_arg;
	int len_arg;
	int len;
	int ls;

	if (event->flags & EVENT_FL_FAILED) {
		trace_seq_printf(s, "[FAILED TO PARSE]");
		print_event_fields(s, data, size, event);
		return;
	}

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
				trace_seq_putc(s, '\n');
				break;
			case 't':
				trace_seq_putc(s, '\t');
				break;
			case 'r':
				trace_seq_putc(s, '\r');
				break;
			case '\\':
				trace_seq_putc(s, '\\');
				break;
			default:
				trace_seq_putc(s, *ptr);
				break;
			}

		} else if (*ptr == '%') {
			saveptr = ptr;
			show_func = 0;
			len_as_arg = 0;
 cont_process:
			ptr++;
			switch (*ptr) {
			case '%':
				trace_seq_putc(s, '%');
				break;
			case '#':
				/* FIXME: need to handle properly */
				goto cont_process;
			case 'h':
				ls--;
				goto cont_process;
			case 'l':
				ls++;
				goto cont_process;
			case 'L':
				ls = 2;
				goto cont_process;
			case '*':
				/* The argument is the length. */
				if (!arg) {
					do_warning("no argument match");
					event->flags |= EVENT_FL_FAILED;
					goto out_failed;
				}
				len_arg = eval_num_arg(data, size, event, arg);
				len_as_arg = 1;
				arg = arg->next;
				goto cont_process;
			case '.':
			case 'z':
			case 'Z':
			case '0' ... '9':
				goto cont_process;
			case 'p':
				if (pevent->long_size == 4)
					ls = 1;
				else
					ls = 2;

				if (*(ptr+1) == 'F' ||
				    *(ptr+1) == 'f') {
					ptr++;
					show_func = *ptr;
				} else if (*(ptr+1) == 'M' || *(ptr+1) == 'm') {
					print_mac_arg(s, *(ptr+1), data, size, event, arg);
					ptr++;
					arg = arg->next;
					break;
				}

				/* fall through */
			case 'd':
			case 'i':
			case 'x':
			case 'X':
			case 'u':
				if (!arg) {
					do_warning("no argument match");
					event->flags |= EVENT_FL_FAILED;
					goto out_failed;
				}

				len = ((unsigned long)ptr + 1) -
					(unsigned long)saveptr;

				/* should never happen */
				if (len > 31) {
					do_warning("bad format!");
					event->flags |= EVENT_FL_FAILED;
					len = 31;
				}

				memcpy(format, saveptr, len);
				format[len] = 0;

				val = eval_num_arg(data, size, event, arg);
				arg = arg->next;

				if (show_func) {
					func = find_func(pevent, val);
					if (func) {
						trace_seq_puts(s, func->func);
						if (show_func == 'F')
							trace_seq_printf(s,
							       "+0x%llx",
							       val - func->addr);
						break;
					}
				}
				if (pevent->long_size == 8 && ls &&
				    sizeof(long) != 8) {
					char *p;

					ls = 2;
					/* make %l into %ll */
					p = strchr(format, 'l');
					if (p)
						memmove(p+1, p, strlen(p)+1);
					else if (strcmp(format, "%p") == 0)
						strcpy(format, "0x%llx");
				}
				switch (ls) {
				case -2:
					if (len_as_arg)
						trace_seq_printf(s, format, len_arg, (char)val);
					else
						trace_seq_printf(s, format, (char)val);
					break;
				case -1:
					if (len_as_arg)
						trace_seq_printf(s, format, len_arg, (short)val);
					else
						trace_seq_printf(s, format, (short)val);
					break;
				case 0:
					if (len_as_arg)
						trace_seq_printf(s, format, len_arg, (int)val);
					else
						trace_seq_printf(s, format, (int)val);
					break;
				case 1:
					if (len_as_arg)
						trace_seq_printf(s, format, len_arg, (long)val);
					else
						trace_seq_printf(s, format, (long)val);
					break;
				case 2:
					if (len_as_arg)
						trace_seq_printf(s, format, len_arg,
								 (long long)val);
					else
						trace_seq_printf(s, format, (long long)val);
					break;
				default:
					do_warning("bad count (%d)", ls);
					event->flags |= EVENT_FL_FAILED;
				}
				break;
			case 's':
				if (!arg) {
					do_warning("no matching argument");
					event->flags |= EVENT_FL_FAILED;
					goto out_failed;
				}

				len = ((unsigned long)ptr + 1) -
					(unsigned long)saveptr;

				/* should never happen */
				if (len > 31) {
					do_warning("bad format!");
					event->flags |= EVENT_FL_FAILED;
					len = 31;
				}

				memcpy(format, saveptr, len);
				format[len] = 0;
				if (!len_as_arg)
					len_arg = -1;
				print_str_arg(s, data, size, event,
					      format, len_arg, arg);
				arg = arg->next;
				break;
			default:
				trace_seq_printf(s, ">%c<", *ptr);

			}
		} else
			trace_seq_putc(s, *ptr);
	}

	if (event->flags & EVENT_FL_FAILED) {
out_failed:
		trace_seq_printf(s, "[FAILED TO PARSE]");
	}

	if (args) {
		free_args(args);
		free(bprint_fmt);
	}
}

/**
 * pevent_data_lat_fmt - parse the data for the latency format
 * @pevent: a handle to the pevent
 * @s: the trace_seq to write to
 * @record: the record to read from
 *
 * This parses out the Latency format (interrupts disabled,
 * need rescheduling, in hard/soft interrupt, preempt count
 * and lock depth) and places it into the trace_seq.
 */
void pevent_data_lat_fmt(struct pevent *pevent,
			 struct trace_seq *s, struct pevent_record *record)
{
	static int check_lock_depth = 1;
	static int check_migrate_disable = 1;
	static int lock_depth_exists;
	static int migrate_disable_exists;
	unsigned int lat_flags;
	unsigned int pc;
	int lock_depth;
	int migrate_disable;
	int hardirq;
	int softirq;
	void *data = record->data;

	lat_flags = parse_common_flags(pevent, data);
	pc = parse_common_pc(pevent, data);
	/* lock_depth may not always exist */
	if (lock_depth_exists)
		lock_depth = parse_common_lock_depth(pevent, data);
	else if (check_lock_depth) {
		lock_depth = parse_common_lock_depth(pevent, data);
		if (lock_depth < 0)
			check_lock_depth = 0;
		else
			lock_depth_exists = 1;
	}

	/* migrate_disable may not always exist */
	if (migrate_disable_exists)
		migrate_disable = parse_common_migrate_disable(pevent, data);
	else if (check_migrate_disable) {
		migrate_disable = parse_common_migrate_disable(pevent, data);
		if (migrate_disable < 0)
			check_migrate_disable = 0;
		else
			migrate_disable_exists = 1;
	}

	hardirq = lat_flags & TRACE_FLAG_HARDIRQ;
	softirq = lat_flags & TRACE_FLAG_SOFTIRQ;

	trace_seq_printf(s, "%c%c%c",
	       (lat_flags & TRACE_FLAG_IRQS_OFF) ? 'd' :
	       (lat_flags & TRACE_FLAG_IRQS_NOSUPPORT) ?
	       'X' : '.',
	       (lat_flags & TRACE_FLAG_NEED_RESCHED) ?
	       'N' : '.',
	       (hardirq && softirq) ? 'H' :
	       hardirq ? 'h' : softirq ? 's' : '.');

	if (pc)
		trace_seq_printf(s, "%x", pc);
	else
		trace_seq_putc(s, '.');

	if (migrate_disable_exists) {
		if (migrate_disable < 0)
			trace_seq_putc(s, '.');
		else
			trace_seq_printf(s, "%d", migrate_disable);
	}

	if (lock_depth_exists) {
		if (lock_depth < 0)
			trace_seq_putc(s, '.');
		else
			trace_seq_printf(s, "%d", lock_depth);
	}

	trace_seq_terminate(s);
}

/**
 * pevent_data_type - parse out the given event type
 * @pevent: a handle to the pevent
 * @rec: the record to read from
 *
 * This returns the event id from the @rec.
 */
int pevent_data_type(struct pevent *pevent, struct pevent_record *rec)
{
	return trace_parse_common_type(pevent, rec->data);
}

/**
 * pevent_data_event_from_type - find the event by a given type
 * @pevent: a handle to the pevent
 * @type: the type of the event.
 *
 * This returns the event form a given @type;
 */
struct event_format *pevent_data_event_from_type(struct pevent *pevent, int type)
{
	return pevent_find_event(pevent, type);
}

/**
 * pevent_data_pid - parse the PID from raw data
 * @pevent: a handle to the pevent
 * @rec: the record to parse
 *
 * This returns the PID from a raw data.
 */
int pevent_data_pid(struct pevent *pevent, struct pevent_record *rec)
{
	return parse_common_pid(pevent, rec->data);
}

/**
 * pevent_data_comm_from_pid - return the command line from PID
 * @pevent: a handle to the pevent
 * @pid: the PID of the task to search for
 *
 * This returns a pointer to the command line that has the given
 * @pid.
 */
const char *pevent_data_comm_from_pid(struct pevent *pevent, int pid)
{
	const char *comm;

	comm = find_cmdline(pevent, pid);
	return comm;
}

/**
 * pevent_data_comm_from_pid - parse the data into the print format
 * @s: the trace_seq to write to
 * @event: the handle to the event
 * @record: the record to read from
 *
 * This parses the raw @data using the given @event information and
 * writes the print format into the trace_seq.
 */
void pevent_event_info(struct trace_seq *s, struct event_format *event,
		       struct pevent_record *record)
{
	int print_pretty = 1;

	if (event->pevent->print_raw)
		print_event_fields(s, record->data, record->size, event);
	else {

		if (event->handler)
			print_pretty = event->handler(s, record, event,
						      event->context);

		if (print_pretty)
			pretty_print(s, record->data, record->size, event);
	}

	trace_seq_terminate(s);
}

void pevent_print_event(struct pevent *pevent, struct trace_seq *s,
			struct pevent_record *record)
{
	static char *spaces = "                    "; /* 20 spaces */
	struct event_format *event;
	unsigned long secs;
	unsigned long usecs;
	unsigned long nsecs;
	const char *comm;
	void *data = record->data;
	int type;
	int pid;
	int len;
	int p;

	secs = record->ts / NSECS_PER_SEC;
	nsecs = record->ts - secs * NSECS_PER_SEC;

	if (record->size < 0) {
		do_warning("ug! negative record size %d", record->size);
		return;
	}

	type = trace_parse_common_type(pevent, data);

	event = pevent_find_event(pevent, type);
	if (!event) {
		do_warning("ug! no event found for type %d", type);
		return;
	}

	pid = parse_common_pid(pevent, data);
	comm = find_cmdline(pevent, pid);

	if (pevent->latency_format) {
		trace_seq_printf(s, "%8.8s-%-5d %3d",
		       comm, pid, record->cpu);
		pevent_data_lat_fmt(pevent, s, record);
	} else
		trace_seq_printf(s, "%16s-%-5d [%03d]", comm, pid, record->cpu);

	if (pevent->flags & PEVENT_NSEC_OUTPUT) {
		usecs = nsecs;
		p = 9;
	} else {
		usecs = (nsecs + 500) / NSECS_PER_USEC;
		p = 6;
	}

	trace_seq_printf(s, " %5lu.%0*lu: %s: ", secs, p, usecs, event->name);

	/* Space out the event names evenly. */
	len = strlen(event->name);
	if (len < 20)
		trace_seq_printf(s, "%.*s", 20 - len, spaces);

	pevent_event_info(s, event, record);
}

static int events_id_cmp(const void *a, const void *b)
{
	struct event_format * const * ea = a;
	struct event_format * const * eb = b;

	if ((*ea)->id < (*eb)->id)
		return -1;

	if ((*ea)->id > (*eb)->id)
		return 1;

	return 0;
}

static int events_name_cmp(const void *a, const void *b)
{
	struct event_format * const * ea = a;
	struct event_format * const * eb = b;
	int res;

	res = strcmp((*ea)->name, (*eb)->name);
	if (res)
		return res;

	res = strcmp((*ea)->system, (*eb)->system);
	if (res)
		return res;

	return events_id_cmp(a, b);
}

static int events_system_cmp(const void *a, const void *b)
{
	struct event_format * const * ea = a;
	struct event_format * const * eb = b;
	int res;

	res = strcmp((*ea)->system, (*eb)->system);
	if (res)
		return res;

	res = strcmp((*ea)->name, (*eb)->name);
	if (res)
		return res;

	return events_id_cmp(a, b);
}

struct event_format **pevent_list_events(struct pevent *pevent, enum event_sort_type sort_type)
{
	struct event_format **events;
	int (*sort)(const void *a, const void *b);

	events = pevent->sort_events;

	if (events && pevent->last_type == sort_type)
		return events;

	if (!events) {
		events = malloc(sizeof(*events) * (pevent->nr_events + 1));
		if (!events)
			return NULL;

		memcpy(events, pevent->events, sizeof(*events) * pevent->nr_events);
		events[pevent->nr_events] = NULL;

		pevent->sort_events = events;

		/* the internal events are sorted by id */
		if (sort_type == EVENT_SORT_ID) {
			pevent->last_type = sort_type;
			return events;
		}
	}

	switch (sort_type) {
	case EVENT_SORT_ID:
		sort = events_id_cmp;
		break;
	case EVENT_SORT_NAME:
		sort = events_name_cmp;
		break;
	case EVENT_SORT_SYSTEM:
		sort = events_system_cmp;
		break;
	default:
		return events;
	}

	qsort(events, pevent->nr_events, sizeof(*events), sort);
	pevent->last_type = sort_type;

	return events;
}

static struct format_field **
get_event_fields(const char *type, const char *name,
		 int count, struct format_field *list)
{
	struct format_field **fields;
	struct format_field *field;
	int i = 0;

	fields = malloc(sizeof(*fields) * (count + 1));
	if (!fields)
		return NULL;

	for (field = list; field; field = field->next) {
		fields[i++] = field;
		if (i == count + 1) {
			do_warning("event %s has more %s fields than specified",
				name, type);
			i--;
			break;
		}
	}

	if (i != count)
		do_warning("event %s has less %s fields than specified",
			name, type);

	fields[i] = NULL;

	return fields;
}

/**
 * pevent_event_common_fields - return a list of common fields for an event
 * @event: the event to return the common fields of.
 *
 * Returns an allocated array of fields. The last item in the array is NULL.
 * The array must be freed with free().
 */
struct format_field **pevent_event_common_fields(struct event_format *event)
{
	return get_event_fields("common", event->name,
				event->format.nr_common,
				event->format.common_fields);
}

/**
 * pevent_event_fields - return a list of event specific fields for an event
 * @event: the event to return the fields of.
 *
 * Returns an allocated array of fields. The last item in the array is NULL.
 * The array must be freed with free().
 */
struct format_field **pevent_event_fields(struct event_format *event)
{
	return get_event_fields("event", event->name,
				event->format.nr_fields,
				event->format.fields);
}

static void print_fields(struct trace_seq *s, struct print_flag_sym *field)
{
	trace_seq_printf(s, "{ %s, %s }", field->value, field->str);
	if (field->next) {
		trace_seq_puts(s, ", ");
		print_fields(s, field->next);
	}
}

/* for debugging */
static void print_args(struct print_arg *args)
{
	int print_paren = 1;
	struct trace_seq s;

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
		trace_seq_init(&s);
		print_fields(&s, args->flags.flags);
		trace_seq_do_printf(&s);
		trace_seq_destroy(&s);
		printf(")");
		break;
	case PRINT_SYMBOL:
		printf("__print_symbolic(");
		print_args(args->symbol.field);
		printf(", ");
		trace_seq_init(&s);
		print_fields(&s, args->symbol.symbols);
		trace_seq_do_printf(&s);
		trace_seq_destroy(&s);
		printf(")");
		break;
	case PRINT_HEX:
		printf("__print_hex(");
		print_args(args->hex.field);
		printf(", ");
		print_args(args->hex.size);
		printf(")");
		break;
	case PRINT_STRING:
	case PRINT_BSTRING:
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
			       int *offset, int *size, int mandatory)
{
	unsigned long long save_input_buf_ptr;
	unsigned long long save_input_buf_siz;
	char *token;
	int type;

	save_input_buf_ptr = input_buf_ptr;
	save_input_buf_siz = input_buf_siz;

	if (read_expected(EVENT_ITEM, "field") < 0)
		return;
	if (read_expected(EVENT_OP, ":") < 0)
		return;

	/* type */
	if (read_expect_type(EVENT_ITEM, &token) < 0)
		goto fail;
	free_token(token);

	/*
	 * If this is not a mandatory field, then test it first.
	 */
	if (mandatory) {
		if (read_expected(EVENT_ITEM, field) < 0)
			return;
	} else {
		if (read_expect_type(EVENT_ITEM, &token) < 0)
			goto fail;
		if (strcmp(token, field) != 0)
			goto discard;
		free_token(token);
	}

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
	return;

 discard:
	input_buf_ptr = save_input_buf_ptr;
	input_buf_siz = save_input_buf_siz;
	*offset = 0;
	*size = 0;
	free_token(token);
}

/**
 * pevent_parse_header_page - parse the data stored in the header page
 * @pevent: the handle to the pevent
 * @buf: the buffer storing the header page format string
 * @size: the size of @buf
 * @long_size: the long size to use if there is no header
 *
 * This parses the header page format for information on the
 * ring buffer used. The @buf should be copied from
 *
 * /sys/kernel/debug/tracing/events/header_page
 */
int pevent_parse_header_page(struct pevent *pevent, char *buf, unsigned long size,
			     int long_size)
{
	int ignore;

	if (!size) {
		/*
		 * Old kernels did not have header page info.
		 * Sorry but we just use what we find here in user space.
		 */
		pevent->header_page_ts_size = sizeof(long long);
		pevent->header_page_size_size = long_size;
		pevent->header_page_data_offset = sizeof(long long) + long_size;
		pevent->old_format = 1;
		return -1;
	}
	init_input_buf(buf, size);

	parse_header_field("timestamp", &pevent->header_page_ts_offset,
			   &pevent->header_page_ts_size, 1);
	parse_header_field("commit", &pevent->header_page_size_offset,
			   &pevent->header_page_size_size, 1);
	parse_header_field("overwrite", &pevent->header_page_overwrite,
			   &ignore, 0);
	parse_header_field("data", &pevent->header_page_data_offset,
			   &pevent->header_page_data_size, 1);

	return 0;
}

static int event_matches(struct event_format *event,
			 int id, const char *sys_name,
			 const char *event_name)
{
	if (id >= 0 && id != event->id)
		return 0;

	if (event_name && (strcmp(event_name, event->name) != 0))
		return 0;

	if (sys_name && (strcmp(sys_name, event->system) != 0))
		return 0;

	return 1;
}

static void free_handler(struct event_handler *handle)
{
	free((void *)handle->sys_name);
	free((void *)handle->event_name);
	free(handle);
}

static int find_event_handle(struct pevent *pevent, struct event_format *event)
{
	struct event_handler *handle, **next;

	for (next = &pevent->handlers; *next;
	     next = &(*next)->next) {
		handle = *next;
		if (event_matches(event, handle->id,
				  handle->sys_name,
				  handle->event_name))
			break;
	}

	if (!(*next))
		return 0;

	pr_stat("overriding event (%d) %s:%s with new print handler",
		event->id, event->system, event->name);

	event->handler = handle->func;
	event->context = handle->context;

	*next = handle->next;
	free_handler(handle);

	return 1;
}

/**
 * __pevent_parse_format - parse the event format
 * @buf: the buffer storing the event format string
 * @size: the size of @buf
 * @sys: the system the event belongs to
 *
 * This parses the event format and creates an event structure
 * to quickly parse raw data for a given event.
 *
 * These files currently come from:
 *
 * /sys/kernel/debug/tracing/events/.../.../format
 */
enum pevent_errno __pevent_parse_format(struct event_format **eventp,
					struct pevent *pevent, const char *buf,
					unsigned long size, const char *sys)
{
	struct event_format *event;
	int ret;

	init_input_buf(buf, size);

	*eventp = event = alloc_event();
	if (!event)
		return PEVENT_ERRNO__MEM_ALLOC_FAILED;

	event->name = event_read_name();
	if (!event->name) {
		/* Bad event? */
		ret = PEVENT_ERRNO__MEM_ALLOC_FAILED;
		goto event_alloc_failed;
	}

	if (strcmp(sys, "ftrace") == 0) {
		event->flags |= EVENT_FL_ISFTRACE;

		if (strcmp(event->name, "bprint") == 0)
			event->flags |= EVENT_FL_ISBPRINT;
	}
		
	event->id = event_read_id();
	if (event->id < 0) {
		ret = PEVENT_ERRNO__READ_ID_FAILED;
		/*
		 * This isn't an allocation error actually.
		 * But as the ID is critical, just bail out.
		 */
		goto event_alloc_failed;
	}

	event->system = strdup(sys);
	if (!event->system) {
		ret = PEVENT_ERRNO__MEM_ALLOC_FAILED;
		goto event_alloc_failed;
	}

	/* Add pevent to event so that it can be referenced */
	event->pevent = pevent;

	ret = event_read_format(event);
	if (ret < 0) {
		ret = PEVENT_ERRNO__READ_FORMAT_FAILED;
		goto event_parse_failed;
	}

	/*
	 * If the event has an override, don't print warnings if the event
	 * print format fails to parse.
	 */
	if (pevent && find_event_handle(pevent, event))
		show_warning = 0;

	ret = event_read_print(event);
	show_warning = 1;

	if (ret < 0) {
		ret = PEVENT_ERRNO__READ_PRINT_FAILED;
		goto event_parse_failed;
	}

	if (!ret && (event->flags & EVENT_FL_ISFTRACE)) {
		struct format_field *field;
		struct print_arg *arg, **list;

		/* old ftrace had no args */
		list = &event->print_fmt.args;
		for (field = event->format.fields; field; field = field->next) {
			arg = alloc_arg();
			if (!arg) {
				event->flags |= EVENT_FL_FAILED;
				return PEVENT_ERRNO__OLD_FTRACE_ARG_FAILED;
			}
			arg->type = PRINT_FIELD;
			arg->field.name = strdup(field->name);
			if (!arg->field.name) {
				event->flags |= EVENT_FL_FAILED;
				free_arg(arg);
				return PEVENT_ERRNO__OLD_FTRACE_ARG_FAILED;
			}
			arg->field.field = field;
			*list = arg;
			list = &arg->next;
		}
		return 0;
	}

	return 0;

 event_parse_failed:
	event->flags |= EVENT_FL_FAILED;
	return ret;

 event_alloc_failed:
	free(event->system);
	free(event->name);
	free(event);
	*eventp = NULL;
	return ret;
}

/**
 * pevent_parse_format - parse the event format
 * @buf: the buffer storing the event format string
 * @size: the size of @buf
 * @sys: the system the event belongs to
 *
 * This parses the event format and creates an event structure
 * to quickly parse raw data for a given event.
 *
 * These files currently come from:
 *
 * /sys/kernel/debug/tracing/events/.../.../format
 */
enum pevent_errno pevent_parse_format(struct event_format **eventp, const char *buf,
				      unsigned long size, const char *sys)
{
	return __pevent_parse_format(eventp, NULL, buf, size, sys);
}

/**
 * pevent_parse_event - parse the event format
 * @pevent: the handle to the pevent
 * @buf: the buffer storing the event format string
 * @size: the size of @buf
 * @sys: the system the event belongs to
 *
 * This parses the event format and creates an event structure
 * to quickly parse raw data for a given event.
 *
 * These files currently come from:
 *
 * /sys/kernel/debug/tracing/events/.../.../format
 */
enum pevent_errno pevent_parse_event(struct pevent *pevent, const char *buf,
				     unsigned long size, const char *sys)
{
	struct event_format *event = NULL;
	int ret = __pevent_parse_format(&event, pevent, buf, size, sys);

	if (event == NULL)
		return ret;

	if (add_event(pevent, event)) {
		ret = PEVENT_ERRNO__MEM_ALLOC_FAILED;
		goto event_add_failed;
	}

#define PRINT_ARGS 0
	if (PRINT_ARGS && event->print_fmt.args)
		print_args(event->print_fmt.args);

	return 0;

event_add_failed:
	pevent_free_format(event);
	return ret;
}

#undef _PE
#define _PE(code, str) str
static const char * const pevent_error_str[] = {
	PEVENT_ERRORS
};
#undef _PE

int pevent_strerror(struct pevent *pevent, enum pevent_errno errnum,
		    char *buf, size_t buflen)
{
	int idx;
	const char *msg;

	if (errnum >= 0) {
		msg = strerror_r(errnum, buf, buflen);
		if (msg != buf) {
			size_t len = strlen(msg);
			memcpy(buf, msg, min(buflen - 1, len));
			*(buf + min(buflen - 1, len)) = '\0';
		}
		return 0;
	}

	if (errnum <= __PEVENT_ERRNO__START ||
	    errnum >= __PEVENT_ERRNO__END)
		return -1;

	idx = errnum - __PEVENT_ERRNO__START - 1;
	msg = pevent_error_str[idx];

	switch (errnum) {
	case PEVENT_ERRNO__MEM_ALLOC_FAILED:
	case PEVENT_ERRNO__PARSE_EVENT_FAILED:
	case PEVENT_ERRNO__READ_ID_FAILED:
	case PEVENT_ERRNO__READ_FORMAT_FAILED:
	case PEVENT_ERRNO__READ_PRINT_FAILED:
	case PEVENT_ERRNO__OLD_FTRACE_ARG_FAILED:
		snprintf(buf, buflen, "%s", msg);
		break;

	default:
		/* cannot reach here */
		break;
	}

	return 0;
}

int get_field_val(struct trace_seq *s, struct format_field *field,
		  const char *name, struct pevent_record *record,
		  unsigned long long *val, int err)
{
	if (!field) {
		if (err)
			trace_seq_printf(s, "<CANT FIND FIELD %s>", name);
		return -1;
	}

	if (pevent_read_number_field(field, record->data, val)) {
		if (err)
			trace_seq_printf(s, " %s=INVALID", name);
		return -1;
	}

	return 0;
}

/**
 * pevent_get_field_raw - return the raw pointer into the data field
 * @s: The seq to print to on error
 * @event: the event that the field is for
 * @name: The name of the field
 * @record: The record with the field name.
 * @len: place to store the field length.
 * @err: print default error if failed.
 *
 * Returns a pointer into record->data of the field and places
 * the length of the field in @len.
 *
 * On failure, it returns NULL.
 */
void *pevent_get_field_raw(struct trace_seq *s, struct event_format *event,
			   const char *name, struct pevent_record *record,
			   int *len, int err)
{
	struct format_field *field;
	void *data = record->data;
	unsigned offset;
	int dummy;

	if (!event)
		return NULL;

	field = pevent_find_field(event, name);

	if (!field) {
		if (err)
			trace_seq_printf(s, "<CANT FIND FIELD %s>", name);
		return NULL;
	}

	/* Allow @len to be NULL */
	if (!len)
		len = &dummy;

	offset = field->offset;
	if (field->flags & FIELD_IS_DYNAMIC) {
		offset = pevent_read_number(event->pevent,
					    data + offset, field->size);
		*len = offset >> 16;
		offset &= 0xffff;
	} else
		*len = field->size;

	return data + offset;
}

/**
 * pevent_get_field_val - find a field and return its value
 * @s: The seq to print to on error
 * @event: the event that the field is for
 * @name: The name of the field
 * @record: The record with the field name.
 * @val: place to store the value of the field.
 * @err: print default error if failed.
 *
 * Returns 0 on success -1 on field not found.
 */
int pevent_get_field_val(struct trace_seq *s, struct event_format *event,
			 const char *name, struct pevent_record *record,
			 unsigned long long *val, int err)
{
	struct format_field *field;

	if (!event)
		return -1;

	field = pevent_find_field(event, name);

	return get_field_val(s, field, name, record, val, err);
}

/**
 * pevent_get_common_field_val - find a common field and return its value
 * @s: The seq to print to on error
 * @event: the event that the field is for
 * @name: The name of the field
 * @record: The record with the field name.
 * @val: place to store the value of the field.
 * @err: print default error if failed.
 *
 * Returns 0 on success -1 on field not found.
 */
int pevent_get_common_field_val(struct trace_seq *s, struct event_format *event,
				const char *name, struct pevent_record *record,
				unsigned long long *val, int err)
{
	struct format_field *field;

	if (!event)
		return -1;

	field = pevent_find_common_field(event, name);

	return get_field_val(s, field, name, record, val, err);
}

/**
 * pevent_get_any_field_val - find a any field and return its value
 * @s: The seq to print to on error
 * @event: the event that the field is for
 * @name: The name of the field
 * @record: The record with the field name.
 * @val: place to store the value of the field.
 * @err: print default error if failed.
 *
 * Returns 0 on success -1 on field not found.
 */
int pevent_get_any_field_val(struct trace_seq *s, struct event_format *event,
			     const char *name, struct pevent_record *record,
			     unsigned long long *val, int err)
{
	struct format_field *field;

	if (!event)
		return -1;

	field = pevent_find_any_field(event, name);

	return get_field_val(s, field, name, record, val, err);
}

/**
 * pevent_print_num_field - print a field and a format
 * @s: The seq to print to
 * @fmt: The printf format to print the field with.
 * @event: the event that the field is for
 * @name: The name of the field
 * @record: The record with the field name.
 * @err: print default error if failed.
 *
 * Returns: 0 on success, -1 field not found, or 1 if buffer is full.
 */
int pevent_print_num_field(struct trace_seq *s, const char *fmt,
			   struct event_format *event, const char *name,
			   struct pevent_record *record, int err)
{
	struct format_field *field = pevent_find_field(event, name);
	unsigned long long val;

	if (!field)
		goto failed;

	if (pevent_read_number_field(field, record->data, &val))
		goto failed;

	return trace_seq_printf(s, fmt, val);

 failed:
	if (err)
		trace_seq_printf(s, "CAN'T FIND FIELD \"%s\"", name);
	return -1;
}

static void free_func_handle(struct pevent_function_handler *func)
{
	struct pevent_func_params *params;

	free(func->name);

	while (func->params) {
		params = func->params;
		func->params = params->next;
		free(params);
	}

	free(func);
}

/**
 * pevent_register_print_function - register a helper function
 * @pevent: the handle to the pevent
 * @func: the function to process the helper function
 * @ret_type: the return type of the helper function
 * @name: the name of the helper function
 * @parameters: A list of enum pevent_func_arg_type
 *
 * Some events may have helper functions in the print format arguments.
 * This allows a plugin to dynamically create a way to process one
 * of these functions.
 *
 * The @parameters is a variable list of pevent_func_arg_type enums that
 * must end with PEVENT_FUNC_ARG_VOID.
 */
int pevent_register_print_function(struct pevent *pevent,
				   pevent_func_handler func,
				   enum pevent_func_arg_type ret_type,
				   char *name, ...)
{
	struct pevent_function_handler *func_handle;
	struct pevent_func_params **next_param;
	struct pevent_func_params *param;
	enum pevent_func_arg_type type;
	va_list ap;
	int ret;

	func_handle = find_func_handler(pevent, name);
	if (func_handle) {
		/*
		 * This is most like caused by the users own
		 * plugins updating the function. This overrides the
		 * system defaults.
		 */
		pr_stat("override of function helper '%s'", name);
		remove_func_handler(pevent, name);
	}

	func_handle = calloc(1, sizeof(*func_handle));
	if (!func_handle) {
		do_warning("Failed to allocate function handler");
		return PEVENT_ERRNO__MEM_ALLOC_FAILED;
	}

	func_handle->ret_type = ret_type;
	func_handle->name = strdup(name);
	func_handle->func = func;
	if (!func_handle->name) {
		do_warning("Failed to allocate function name");
		free(func_handle);
		return PEVENT_ERRNO__MEM_ALLOC_FAILED;
	}

	next_param = &(func_handle->params);
	va_start(ap, name);
	for (;;) {
		type = va_arg(ap, enum pevent_func_arg_type);
		if (type == PEVENT_FUNC_ARG_VOID)
			break;

		if (type < 0 || type >= PEVENT_FUNC_ARG_MAX_TYPES) {
			do_warning("Invalid argument type %d", type);
			ret = PEVENT_ERRNO__INVALID_ARG_TYPE;
			goto out_free;
		}

		param = malloc(sizeof(*param));
		if (!param) {
			do_warning("Failed to allocate function param");
			ret = PEVENT_ERRNO__MEM_ALLOC_FAILED;
			goto out_free;
		}
		param->type = type;
		param->next = NULL;

		*next_param = param;
		next_param = &(param->next);

		func_handle->nr_args++;
	}
	va_end(ap);

	func_handle->next = pevent->func_handlers;
	pevent->func_handlers = func_handle;

	return 0;
 out_free:
	va_end(ap);
	free_func_handle(func_handle);
	return ret;
}

/**
 * pevent_register_event_handler - register a way to parse an event
 * @pevent: the handle to the pevent
 * @id: the id of the event to register
 * @sys_name: the system name the event belongs to
 * @event_name: the name of the event
 * @func: the function to call to parse the event information
 * @context: the data to be passed to @func
 *
 * This function allows a developer to override the parsing of
 * a given event. If for some reason the default print format
 * is not sufficient, this function will register a function
 * for an event to be used to parse the data instead.
 *
 * If @id is >= 0, then it is used to find the event.
 * else @sys_name and @event_name are used.
 */
int pevent_register_event_handler(struct pevent *pevent,
				  int id, char *sys_name, char *event_name,
				  pevent_event_handler_func func,
				  void *context)
{
	struct event_format *event;
	struct event_handler *handle;

	if (id >= 0) {
		/* search by id */
		event = pevent_find_event(pevent, id);
		if (!event)
			goto not_found;
		if (event_name && (strcmp(event_name, event->name) != 0))
			goto not_found;
		if (sys_name && (strcmp(sys_name, event->system) != 0))
			goto not_found;
	} else {
		event = pevent_find_event_by_name(pevent, sys_name, event_name);
		if (!event)
			goto not_found;
	}

	pr_stat("overriding event (%d) %s:%s with new print handler",
		event->id, event->system, event->name);

	event->handler = func;
	event->context = context;
	return 0;

 not_found:
	/* Save for later use. */
	handle = calloc(1, sizeof(*handle));
	if (!handle) {
		do_warning("Failed to allocate event handler");
		return PEVENT_ERRNO__MEM_ALLOC_FAILED;
	}

	handle->id = id;
	if (event_name)
		handle->event_name = strdup(event_name);
	if (sys_name)
		handle->sys_name = strdup(sys_name);

	if ((event_name && !handle->event_name) ||
	    (sys_name && !handle->sys_name)) {
		do_warning("Failed to allocate event/sys name");
		free((void *)handle->event_name);
		free((void *)handle->sys_name);
		free(handle);
		return PEVENT_ERRNO__MEM_ALLOC_FAILED;
	}

	handle->func = func;
	handle->next = pevent->handlers;
	pevent->handlers = handle;
	handle->context = context;

	return -1;
}

/**
 * pevent_alloc - create a pevent handle
 */
struct pevent *pevent_alloc(void)
{
	struct pevent *pevent = calloc(1, sizeof(*pevent));

	if (pevent)
		pevent->ref_count = 1;

	return pevent;
}

void pevent_ref(struct pevent *pevent)
{
	pevent->ref_count++;
}

static void free_format_fields(struct format_field *field)
{
	struct format_field *next;

	while (field) {
		next = field->next;
		free(field->type);
		free(field->name);
		free(field);
		field = next;
	}
}

static void free_formats(struct format *format)
{
	free_format_fields(format->common_fields);
	free_format_fields(format->fields);
}

void pevent_free_format(struct event_format *event)
{
	free(event->name);
	free(event->system);

	free_formats(&event->format);

	free(event->print_fmt.format);
	free_args(event->print_fmt.args);

	free(event);
}

/**
 * pevent_free - free a pevent handle
 * @pevent: the pevent handle to free
 */
void pevent_free(struct pevent *pevent)
{
	struct cmdline_list *cmdlist, *cmdnext;
	struct func_list *funclist, *funcnext;
	struct printk_list *printklist, *printknext;
	struct pevent_function_handler *func_handler;
	struct event_handler *handle;
	int i;

	if (!pevent)
		return;

	cmdlist = pevent->cmdlist;
	funclist = pevent->funclist;
	printklist = pevent->printklist;

	pevent->ref_count--;
	if (pevent->ref_count)
		return;

	if (pevent->cmdlines) {
		for (i = 0; i < pevent->cmdline_count; i++)
			free(pevent->cmdlines[i].comm);
		free(pevent->cmdlines);
	}

	while (cmdlist) {
		cmdnext = cmdlist->next;
		free(cmdlist->comm);
		free(cmdlist);
		cmdlist = cmdnext;
	}

	if (pevent->func_map) {
		for (i = 0; i < pevent->func_count; i++) {
			free(pevent->func_map[i].func);
			free(pevent->func_map[i].mod);
		}
		free(pevent->func_map);
	}

	while (funclist) {
		funcnext = funclist->next;
		free(funclist->func);
		free(funclist->mod);
		free(funclist);
		funclist = funcnext;
	}

	while (pevent->func_handlers) {
		func_handler = pevent->func_handlers;
		pevent->func_handlers = func_handler->next;
		free_func_handle(func_handler);
	}

	if (pevent->printk_map) {
		for (i = 0; i < pevent->printk_count; i++)
			free(pevent->printk_map[i].printk);
		free(pevent->printk_map);
	}

	while (printklist) {
		printknext = printklist->next;
		free(printklist->printk);
		free(printklist);
		printklist = printknext;
	}

	for (i = 0; i < pevent->nr_events; i++)
		pevent_free_format(pevent->events[i]);

	while (pevent->handlers) {
		handle = pevent->handlers;
		pevent->handlers = handle->next;
		free_handler(handle);
	}

	free(pevent->events);
	free(pevent->sort_events);

	free(pevent);
}

void pevent_unref(struct pevent *pevent)
{
	pevent_free(pevent);
}
