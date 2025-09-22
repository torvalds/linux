/*	$OpenBSD: parse.y,v 1.56 2021/10/15 15:01:28 naddy Exp $	*/

/*
 * Copyright (c) 2004 Ryan McBride <mcbride@openbsd.org>
 * Copyright (c) 2002, 2003, 2004 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2001 Markus Friedl.  All rights reserved.
 * Copyright (c) 2001 Daniel Hartmeier.  All rights reserved.
 * Copyright (c) 2001 Theo de Raadt.  All rights reserved.
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

%{
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

#include <ctype.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <event.h>

#include "ifstated.h"
#include "log.h"

TAILQ_HEAD(files, file)		 files = TAILQ_HEAD_INITIALIZER(files);
static struct file {
	TAILQ_ENTRY(file)	 entry;
	FILE			*stream;
	char			*name;
	int			 lineno;
	int			 errors;
} *file, *topfile;
struct file	*pushfile(const char *, int);
int		 popfile(void);
int		 check_file_secrecy(int, const char *);
int		 yyparse(void);
int		 yylex(void);
int		 yyerror(const char *, ...)
    __attribute__((__format__ (printf, 1, 2)))
    __attribute__((__nonnull__ (1)));
int		 kw_cmp(const void *, const void *);
int		 lookup(char *);
int		 lgetc(int);
int		 lungetc(int);
int		 findeol(void);

TAILQ_HEAD(symhead, sym)	 symhead = TAILQ_HEAD_INITIALIZER(symhead);
struct sym {
	TAILQ_ENTRY(sym)	 entry;
	int			 used;
	int			 persist;
	char			*nam;
	char			*val;
};
int		 symset(const char *, const char *, int);
char		*symget(const char *);

static struct ifsd_config	*conf;
char				*start_state;

struct ifsd_action		*curaction;
struct ifsd_state		*curstate;

void			 link_states(struct ifsd_action *);
void			 set_expression_depth(struct ifsd_expression *, int);
void			 init_state(struct ifsd_state *);
struct ifsd_ifstate	*new_ifstate(char *, int);
struct ifsd_external	*new_external(char *, u_int32_t);

typedef struct {
	union {
		int64_t		 number;
		char		*string;
		struct in_addr	 addr;

		struct ifsd_expression	*expression;
		struct ifsd_ifstate	*ifstate;
		struct ifsd_external	*external;

	} v;
	int lineno;
} YYSTYPE;

%}

%token	STATE INITSTATE
%token	LINK UP DOWN UNKNOWN
%token	IF RUN SETSTATE EVERY INIT
%left	AND OR
%left	UNARY
%token	ERROR
%token	<v.string>	STRING
%token	<v.number>	NUMBER
%type	<v.string>	string
%type	<v.string>	interface
%type	<v.ifstate>	if_test
%type	<v.external>	ext_test
%type	<v.expression>	expr term
%%

grammar		: /* empty */
		| grammar '\n'
		| grammar conf_main '\n'
		| grammar varset '\n'
		| grammar action '\n'
		| grammar state '\n'
		| grammar error '\n'		{ file->errors++; }
		;

string		: string STRING				{
			if (asprintf(&$$, "%s %s", $1, $2) == -1) {
				free($1);
				free($2);
				yyerror("string: asprintf");
				YYERROR;
			}
			free($1);
			free($2);
		}
		| STRING
		;

varset		: STRING '=' string		{
			char *s = $1;
			if (conf->opts & IFSD_OPT_VERBOSE)
				printf("%s = \"%s\"\n", $1, $3);
			while (*s++) {
				if (isspace((unsigned char)*s)) {
					yyerror("macro name cannot contain "
					    "whitespace");
					free($1);
					free($3);
					YYERROR;
				}
			}
			if (symset($1, $3, 0) == -1) {
				free($1);
				free($3);
				yyerror("cannot store variable");
				YYERROR;
			}
			free($1);
			free($3);
		}
		;

conf_main	: INITSTATE STRING		{
			start_state = $2;
		}
		;

interface	: STRING		{
			if (if_nametoindex($1) == 0) {
				yyerror("unknown interface %s", $1);
				free($1);
				YYERROR;
			}
			$$ = $1;
		}
		;

optnl		: '\n' optnl
		|
		;

nl		: '\n' optnl		/* one newline or more */
		;

action		: RUN STRING		{
			struct ifsd_action *action;

			if ((action = calloc(1, sizeof(*action))) == NULL)
				err(1, "action: calloc");
			action->type = IFSD_ACTION_COMMAND;
			action->act.command = $2;
			TAILQ_INSERT_TAIL(&curaction->act.c.actions,
			    action, entries);
		}
		| SETSTATE STRING	{
			struct ifsd_action *action;

			if (curstate == NULL) {
				free($2);
				yyerror("set-state must be used inside 'if'");
				YYERROR;
			}
			if ((action = calloc(1, sizeof(*action))) == NULL)
				err(1, "action: calloc");
			action->type = IFSD_ACTION_CHANGESTATE;
			action->act.statename = $2;
			TAILQ_INSERT_TAIL(&curaction->act.c.actions,
			    action, entries);
		}
		| IF {
			struct ifsd_action *action;

			if ((action = calloc(1, sizeof(*action))) == NULL)
				err(1, "action: calloc");
			action->type = IFSD_ACTION_CONDITION;
			TAILQ_INIT(&action->act.c.actions);
			TAILQ_INSERT_TAIL(&curaction->act.c.actions,
			    action, entries);
			action->parent = curaction;
			curaction = action;
		} expr action_block {
			set_expression_depth(curaction->act.c.expression, 0);
			curaction = curaction->parent;
		}
		;

action_block	: optnl '{' optnl action_l '}'
		| optnl action
		;

action_l	: action_l action nl
		| action nl
		;

init		: INIT {
			if (curstate != NULL)
				curaction = curstate->init;
			else
				curaction = conf->initstate.init;
		} action_block {
			if (curstate != NULL)
				curaction = curstate->body;
			else
				curaction = conf->initstate.body;
		}
		;

if_test		: interface '.' LINK '.' UP		{
			$$ = new_ifstate($1, IFSD_LINKUP);
		}
		| interface '.' LINK '.' DOWN		{
			$$ = new_ifstate($1, IFSD_LINKDOWN);
		}
		| interface '.' LINK '.' UNKNOWN	{
			$$ = new_ifstate($1, IFSD_LINKUNKNOWN);
		}
		;

ext_test	: STRING EVERY NUMBER {
			if ($3 <= 0 || $3 > UINT_MAX) {
				yyerror("invalid interval: %lld", $3);
				free($1);
				YYERROR;
			}
			$$ = new_external($1, $3);
			free($1);
		}
		;

term		: if_test {
			if (($$ = calloc(1, sizeof(*$$))) == NULL)
				err(1, NULL);
			curaction->act.c.expression = $$;
			$$->type = IFSD_OPER_IFSTATE;
			$$->u.ifstate = $1;
			TAILQ_INSERT_TAIL(&$1->expressions, $$, entries);
		}
		| ext_test {
			if (($$ = calloc(1, sizeof(*$$))) == NULL)
				err(1, NULL);
			curaction->act.c.expression = $$;
			$$->type = IFSD_OPER_EXTERNAL;
			$$->u.external = $1;
			TAILQ_INSERT_TAIL(&$1->expressions, $$, entries);
		}
		| '(' expr ')'			{
			$$ = $2;
		}
		;

expr		: '!' expr %prec UNARY			{
			if (($$ = calloc(1, sizeof(*$$))) == NULL)
				err(1, NULL);
			curaction->act.c.expression = $$;
			$$->type = IFSD_OPER_NOT;
			$2->parent = $$;
			$$->right = $2;
		}
		| expr AND expr			{
			if (($$ = calloc(1, sizeof(*$$))) == NULL)
				err(1, NULL);
			curaction->act.c.expression = $$;
			$$->type = IFSD_OPER_AND;
			$1->parent = $$;
			$3->parent = $$;
			$$->left = $1;
			$$->right = $3;
		}
		| expr OR expr			{
			if (($$ = calloc(1, sizeof(*$$))) == NULL)
				err(1, NULL);
			curaction->act.c.expression = $$;
			$$->type = IFSD_OPER_OR;
			$1->parent = $$;
			$3->parent = $$;
			$$->left = $1;
			$$->right = $3;
		}
		| term
		;

state		: STATE string {
			struct ifsd_state *state = NULL;

			TAILQ_FOREACH(state, &conf->states, entries)
				if (!strcmp(state->name, $2)) {
					yyerror("state %s already exists", $2);
					free($2);
					YYERROR;
				}
			if ((state = calloc(1, sizeof(*curstate))) == NULL)
				err(1, NULL);
			init_state(state);
			state->name = $2;
			curstate = state;
			curaction = state->body;
		} optnl '{' optnl stateopts_l '}' {
			TAILQ_INSERT_TAIL(&conf->states, curstate, entries);
			curstate = NULL;
			curaction = conf->initstate.body;
		}
		;

stateopts_l	: stateopts_l stateoptsl
		| stateoptsl
		;

stateoptsl	: init nl
		| action nl
		;

%%

struct keywords {
	const char	*k_name;
	int		 k_val;
};

int
yyerror(const char *fmt, ...)
{
	va_list		 ap;
	char		*msg;

	file->errors++;
	va_start(ap, fmt);
	if (vasprintf(&msg, fmt, ap) == -1)
		fatalx("yyerror vasprintf");
	va_end(ap);
	logit(LOG_CRIT, "%s:%d: %s", file->name, yylval.lineno, msg);
	free(msg);
	return (0);
}

int
kw_cmp(const void *k, const void *e)
{
	return (strcmp(k, ((const struct keywords *)e)->k_name));
}

int
lookup(char *s)
{
	/* this has to be sorted always */
	static const struct keywords keywords[] = {
		{ "&&",			AND},
		{ "down",		DOWN},
		{ "every",		EVERY},
		{ "if",			IF},
		{ "init",		INIT},
		{ "init-state",		INITSTATE},
		{ "link",		LINK},
		{ "run",		RUN},
		{ "set-state",		SETSTATE},
		{ "state",		STATE},
		{ "unknown",		UNKNOWN},
		{ "up",			UP},
		{ "||",			OR}
	};
	const struct keywords	*p;

	p = bsearch(s, keywords, sizeof(keywords)/sizeof(keywords[0]),
	    sizeof(keywords[0]), kw_cmp);

	if (p)
		return (p->k_val);
	else
		return (STRING);
}

#define MAXPUSHBACK	128

char	*parsebuf;
int	 parseindex;
char	 pushback_buffer[MAXPUSHBACK];
int	 pushback_index = 0;

int
lgetc(int quotec)
{
	int		c, next;

	if (parsebuf) {
		/* Read character from the parsebuffer instead of input. */
		if (parseindex >= 0) {
			c = (unsigned char)parsebuf[parseindex++];
			if (c != '\0')
				return (c);
			parsebuf = NULL;
		} else
			parseindex++;
	}

	if (pushback_index)
		return ((unsigned char)pushback_buffer[--pushback_index]);

	if (quotec) {
		if ((c = getc(file->stream)) == EOF) {
			yyerror("reached end of file while parsing "
			    "quoted string");
			if (file == topfile || popfile() == EOF)
				return (EOF);
			return (quotec);
		}
		return (c);
	}

	while ((c = getc(file->stream)) == '\\') {
		next = getc(file->stream);
		if (next != '\n') {
			c = next;
			break;
		}
		yylval.lineno = file->lineno;
		file->lineno++;
	}

	while (c == EOF) {
		if (file == topfile || popfile() == EOF)
			return (EOF);
		c = getc(file->stream);
	}
	return (c);
}

int
lungetc(int c)
{
	if (c == EOF)
		return (EOF);
	if (parsebuf) {
		parseindex--;
		if (parseindex >= 0)
			return (c);
	}
	if (pushback_index + 1 >= MAXPUSHBACK)
		return (EOF);
	pushback_buffer[pushback_index++] = c;
	return (c);
}

int
findeol(void)
{
	int	c;

	parsebuf = NULL;

	/* skip to either EOF or the first real EOL */
	while (1) {
		if (pushback_index)
			c = (unsigned char)pushback_buffer[--pushback_index];
		else
			c = lgetc(0);
		if (c == '\n') {
			file->lineno++;
			break;
		}
		if (c == EOF)
			break;
	}
	return (ERROR);
}

int
yylex(void)
{
	char	 buf[8096];
	char	*p, *val;
	int	 quotec, next, c;
	int	 token;

top:
	p = buf;
	while ((c = lgetc(0)) == ' ' || c == '\t')
		; /* nothing */

	yylval.lineno = file->lineno;
	if (c == '#')
		while ((c = lgetc(0)) != '\n' && c != EOF)
			; /* nothing */
	if (c == '$' && parsebuf == NULL) {
		while (1) {
			if ((c = lgetc(0)) == EOF)
				return (0);

			if (p + 1 >= buf + sizeof(buf) - 1) {
				yyerror("string too long");
				return (findeol());
			}
			if (isalnum(c) || c == '_') {
				*p++ = c;
				continue;
			}
			*p = '\0';
			lungetc(c);
			break;
		}
		val = symget(buf);
		if (val == NULL) {
			yyerror("macro '%s' not defined", buf);
			return (findeol());
		}
		parsebuf = val;
		parseindex = 0;
		goto top;
	}

	switch (c) {
	case '\'':
	case '"':
		quotec = c;
		while (1) {
			if ((c = lgetc(quotec)) == EOF)
				return (0);
			if (c == '\n') {
				file->lineno++;
				continue;
			} else if (c == '\\') {
				if ((next = lgetc(quotec)) == EOF)
					return (0);
				if (next == quotec || next == ' ' ||
				    next == '\t')
					c = next;
				else if (next == '\n') {
					file->lineno++;
					continue;
				} else
					lungetc(next);
			} else if (c == quotec) {
				*p = '\0';
				break;
			} else if (c == '\0') {
				yyerror("syntax error");
				return (findeol());
			}
			if (p + 1 >= buf + sizeof(buf) - 1) {
				yyerror("string too long");
				return (findeol());
			}
			*p++ = c;
		}
		yylval.v.string = strdup(buf);
		if (yylval.v.string == NULL)
			err(1, "%s", __func__);
		return (STRING);
	}

#define allowed_to_end_number(x) \
	(isspace(x) || x == ')' || x ==',' || x == '/' || x == '}' || x == '=')

	if (c == '-' || isdigit(c)) {
		do {
			*p++ = c;
			if ((size_t)(p-buf) >= sizeof(buf)) {
				yyerror("string too long");
				return (findeol());
			}
		} while ((c = lgetc(0)) != EOF && isdigit(c));
		lungetc(c);
		if (p == buf + 1 && buf[0] == '-')
			goto nodigits;
		if (c == EOF || allowed_to_end_number(c)) {
			const char *errstr = NULL;

			*p = '\0';
			yylval.v.number = strtonum(buf, LLONG_MIN,
			    LLONG_MAX, &errstr);
			if (errstr) {
				yyerror("\"%s\" invalid number: %s",
				    buf, errstr);
				return (findeol());
			}
			return (NUMBER);
		} else {
nodigits:
			while (p > buf + 1)
				lungetc((unsigned char)*--p);
			c = (unsigned char)*--p;
			if (c == '-')
				return (c);
		}
	}

#define allowed_in_string(x) \
	(isalnum(x) || (ispunct(x) && x != '(' && x != ')' && \
	x != '{' && x != '}' && \
	x != '!' && x != '=' && x != '#' && \
	x != ',' && x != '.'))

	if (isalnum(c) || c == ':' || c == '_' || c == '&' || c == '|') {
		do {
			*p++ = c;
			if ((size_t)(p-buf) >= sizeof(buf)) {
				yyerror("string too long");
				return (findeol());
			}
		} while ((c = lgetc(0)) != EOF && (allowed_in_string(c)));
		lungetc(c);
		*p = '\0';
		if ((token = lookup(buf)) == STRING)
			if ((yylval.v.string = strdup(buf)) == NULL)
				err(1, "%s", __func__);
		return (token);
	}
	if (c == '\n') {
		yylval.lineno = file->lineno;
		file->lineno++;
	}
	if (c == EOF)
		return (0);
	return (c);
}

int
check_file_secrecy(int fd, const char *fname)
{
	struct stat	st;

	if (fstat(fd, &st)) {
		warn("cannot stat %s", fname);
		return (-1);
	}
	if (st.st_uid != 0 && st.st_uid != getuid()) {
		warnx("%s: owner not root or current user", fname);
		return (-1);
	}
	if (st.st_mode & (S_IWGRP | S_IXGRP | S_IRWXO)) {
		warnx("%s: group writable or world read/writable", fname);
		return (-1);
	}
	return (0);
}

struct file *
pushfile(const char *name, int secret)
{
	struct file	*nfile;

	if ((nfile = calloc(1, sizeof(struct file))) == NULL) {
		warn("%s", __func__);
		return (NULL);
	}
	if ((nfile->name = strdup(name)) == NULL) {
		warn("%s", __func__);
		free(nfile);
		return (NULL);
	}
	if ((nfile->stream = fopen(nfile->name, "r")) == NULL) {
		warn("%s: %s", __func__, nfile->name);
		free(nfile->name);
		free(nfile);
		return (NULL);
	} else if (secret &&
	    check_file_secrecy(fileno(nfile->stream), nfile->name)) {
		fclose(nfile->stream);
		free(nfile->name);
		free(nfile);
		return (NULL);
	}
	nfile->lineno = 1;
	TAILQ_INSERT_TAIL(&files, nfile, entry);
	return (nfile);
}

int
popfile(void)
{
	struct file	*prev;

	if ((prev = TAILQ_PREV(file, files, entry)) != NULL)
		prev->errors += file->errors;

	TAILQ_REMOVE(&files, file, entry);
	fclose(file->stream);
	free(file->name);
	free(file);
	file = prev;
	return (file ? 0 : EOF);
}

struct ifsd_config *
parse_config(char *filename, int opts)
{
	int		 errors = 0;
	struct sym	*sym, *next;
	struct ifsd_state *state;

	if ((conf = calloc(1, sizeof(struct ifsd_config))) == NULL) {
		err(1, "%s", __func__);
		return (NULL);
	}

	if ((file = pushfile(filename, 0)) == NULL) {
		free(conf);
		return (NULL);
	}
	topfile = file;

	TAILQ_INIT(&conf->states);

	init_state(&conf->initstate);
	curaction = conf->initstate.body;
	conf->opts = opts;

	yyparse();

	/* Link states */
	TAILQ_FOREACH(state, &conf->states, entries) {
		link_states(state->init);
		link_states(state->body);
	}

	errors = file->errors;
	popfile();

	if (start_state != NULL) {
		TAILQ_FOREACH(state, &conf->states, entries) {
			if (strcmp(start_state, state->name) == 0) {
				conf->curstate = state;
				break;
			}
		}
		if (conf->curstate == NULL)
			errx(1, "invalid start state %s", start_state);
	} else {
		conf->curstate = TAILQ_FIRST(&conf->states);
	}

	/* Free macros and check which have not been used. */
	TAILQ_FOREACH_SAFE(sym, &symhead, entry, next) {
		if ((conf->opts & IFSD_OPT_VERBOSE2) && !sym->used)
			fprintf(stderr, "warning: macro '%s' not "
			    "used\n", sym->nam);
		if (!sym->persist) {
			free(sym->nam);
			free(sym->val);
			TAILQ_REMOVE(&symhead, sym, entry);
			free(sym);
		}
	}

	if (errors) {
		clear_config(conf);
		errors = 0;
		return (NULL);
	}

	return (conf);
}

void
link_states(struct ifsd_action *action)
{
	struct ifsd_action *subaction;

	switch (action->type) {
	default:
	case IFSD_ACTION_COMMAND:
		break;
	case IFSD_ACTION_CHANGESTATE: {
		struct ifsd_state *state;

		TAILQ_FOREACH(state, &conf->states, entries) {
			if (strcmp(action->act.statename,
			    state->name) == 0) {
				action->act.nextstate = state;
				break;
			}
		}
		if (state == NULL) {
			fprintf(stderr, "error: state '%s' not declared\n",
			    action->act.statename);
			file->errors++;
		}
		break;
	}
	case IFSD_ACTION_CONDITION:
		TAILQ_FOREACH(subaction, &action->act.c.actions, entries)
			link_states(subaction);
		break;
	}
}

int
symset(const char *nam, const char *val, int persist)
{
	struct sym	*sym;

	TAILQ_FOREACH(sym, &symhead, entry) {
		if (strcmp(nam, sym->nam) == 0)
			break;
	}

	if (sym != NULL) {
		if (sym->persist == 1)
			return (0);
		else {
			free(sym->nam);
			free(sym->val);
			TAILQ_REMOVE(&symhead, sym, entry);
			free(sym);
		}
	}
	if ((sym = calloc(1, sizeof(*sym))) == NULL)
		return (-1);

	sym->nam = strdup(nam);
	if (sym->nam == NULL) {
		free(sym);
		return (-1);
	}
	sym->val = strdup(val);
	if (sym->val == NULL) {
		free(sym->nam);
		free(sym);
		return (-1);
	}
	sym->used = 0;
	sym->persist = persist;
	TAILQ_INSERT_TAIL(&symhead, sym, entry);
	return (0);
}

int
cmdline_symset(char *s)
{
	char	*sym, *val;
	int	ret;

	if ((val = strrchr(s, '=')) == NULL)
		return (-1);
	sym = strndup(s, val - s);
	if (sym == NULL)
		err(1, "%s", __func__);
	ret = symset(sym, val + 1, 1);
	free(sym);

	return (ret);
}

char *
symget(const char *nam)
{
	struct sym	*sym;

	TAILQ_FOREACH(sym, &symhead, entry) {
		if (strcmp(nam, sym->nam) == 0) {
			sym->used = 1;
			return (sym->val);
		}
	}
	return (NULL);
}

void
set_expression_depth(struct ifsd_expression *expression, int depth)
{
	expression->depth = depth;
	if (conf->maxdepth < depth)
		conf->maxdepth = depth;
	if (expression->left != NULL)
		set_expression_depth(expression->left, depth + 1);
	if (expression->right != NULL)
		set_expression_depth(expression->right, depth + 1);
}

void
init_state(struct ifsd_state *state)
{
	TAILQ_INIT(&state->interface_states);
	TAILQ_INIT(&state->external_tests);

	if ((state->init = calloc(1, sizeof(*state->init))) == NULL)
		err(1, "%s", __func__);
	state->init->type = IFSD_ACTION_CONDITION;
	TAILQ_INIT(&state->init->act.c.actions);

	if ((state->body = calloc(1, sizeof(*state->body))) == NULL)
		err(1, "%s", __func__);
	state->body->type = IFSD_ACTION_CONDITION;
	TAILQ_INIT(&state->body->act.c.actions);
}

struct ifsd_ifstate *
new_ifstate(char *ifname, int s)
{
	struct ifsd_ifstate *ifstate = NULL;
	struct ifsd_state *state;

	if (curstate != NULL)
		state = curstate;
	else
		state = &conf->initstate;

	TAILQ_FOREACH(ifstate, &state->interface_states, entries)
		if (strcmp(ifstate->ifname, ifname) == 0 &&
		    ifstate->ifstate == s)
			break;
	if (ifstate == NULL) {
		if ((ifstate = calloc(1, sizeof(*ifstate))) == NULL)
			err(1, "%s", __func__);
		if (strlcpy(ifstate->ifname, ifname,
		    sizeof(ifstate->ifname)) >= sizeof(ifstate->ifname))
			errx(1, "ifname strlcpy truncation");
		free(ifname);
		ifstate->ifstate = s;
		TAILQ_INIT(&ifstate->expressions);
		TAILQ_INSERT_TAIL(&state->interface_states, ifstate, entries);
	}
	ifstate->prevstate = -1;
	ifstate->refcount++;
	return (ifstate);
}

struct ifsd_external *
new_external(char *command, u_int32_t frequency)
{
	struct ifsd_external *external = NULL;
	struct ifsd_state *state;

	if (curstate != NULL)
		state = curstate;
	else
		state = &conf->initstate;

	TAILQ_FOREACH(external, &state->external_tests, entries)
		if (strcmp(external->command, command) == 0 &&
		    external->frequency == frequency)
			break;
	if (external == NULL) {
		if ((external = calloc(1, sizeof(*external))) == NULL)
			err(1, "%s", __func__);
		if ((external->command = strdup(command)) == NULL)
			err(1, "%s", __func__);
		external->frequency = frequency;
		TAILQ_INIT(&external->expressions);
		TAILQ_INSERT_TAIL(&state->external_tests, external, entries);
	}
	external->prevstatus = -1;
	external->refcount++;
	return (external);
}
