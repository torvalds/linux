/*-
 * Copyright (c) 2006 The FreeBSD Project
 * All rights reserved.
 *
 * Author: Shteryana Shopova <syrinx@FreeBSD.org>
 *
 * Redistribution of this software and documentation and use in source and
 * binary forms, with or without modification, are permitted provided that
 * the following conditions are met:
 *
 * 1. Redistributions of source code or documentation must retain the above
 *    copyright notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Read file containing table description - reuse magic from gensnmptree.c.
 * Hopefully one day most of the code here will be part of libbsnmp and
 * this duplication won't be necessary.
 *
 * Syntax is:
 * ---------
 * file := top | top file
 *
 * top := tree | typedef | include
 *
 * tree := head elements ')'
 *
 * entry := head ':' index STRING elements ')'
 *
 * leaf := head type STRING ACCESS ')'
 *
 * column := head type ACCESS ')'
 *
 * type := BASETYPE | BASETYPE '|' subtype | enum | bits
 *
 * subtype := STRING
 *
 * enum := ENUM '(' value ')'
 *
 * bits := BITS '(' value ')'
 *
 * value := INT STRING | INT STRING value
 *
 * head := '(' INT STRING
 *
 * elements := EMPTY | elements element
 *
 * element := tree | leaf | column
 *
 * index := type | index type
 *
 * typedef := 'typedef' STRING type
 *
 * include := 'include' filespec
 *
 * filespec := '"' STRING '"' | '<' STRING '>'
 */

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/uio.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <bsnmp/asn1.h>
#include <bsnmp/snmp.h>
#include <bsnmp/snmpagent.h>	/* SNMP_INDEXES_MAX */
#include "bsnmptc.h"
#include "bsnmptools.h"

enum snmp_tbl_entry {
	ENTRY_NONE = 0,
	ENTRY_INDEX,
	ENTRY_DATA
};

enum {
	FL_GET	= 0x01,
	FL_SET	= 0x02,
};

/************************************************************
 *
 * Allocate memory and panic just in the case...
 */
static void *
xalloc(size_t size)
{
	void *ptr;

	if ((ptr = malloc(size)) == NULL)
		err(1, "allocing %zu bytes", size);

	return (ptr);
}

static char *
savestr(const char *s)
{
	if (s == NULL)
		return (NULL);

	return (strcpy(xalloc(strlen(s) + 1), s));
}

/************************************************************
 *
 * Input stack
 */
struct input {
	FILE		*fp;
	uint32_t	lno;
	char		*fname;
	char		*path;
	LIST_ENTRY(input) link;
};

static LIST_HEAD(, input) inputs = LIST_HEAD_INITIALIZER(inputs);
static struct input *input = NULL;
static int32_t pbchar = -1;

#define	MAX_PATHS	100

static const char *paths[MAX_PATHS + 1] = {
	"/usr/share/snmp/defs",
	"/usr/local/share/snmp/defs",
	NULL
};

static void
input_new(FILE *fp, const char *path, const char *fname)
{
	struct input *ip;

	ip = xalloc(sizeof(*ip));
	ip->fp = fp;
	ip->lno = 1;
	ip->fname = savestr(fname);
	ip->path = savestr(path);
	LIST_INSERT_HEAD(&inputs, ip, link);

	input = ip;
}

static void
input_close(void)
{
	if (input == NULL)
		return;

	fclose(input->fp);
	free(input->fname);
	free(input->path);
	LIST_REMOVE(input, link);
	free(input);

	input = LIST_FIRST(&inputs);
}

static FILE *
tryopen(const char *path, const char *fname)
{
	char *fn;
	FILE *fp;

	if (path == NULL)
		fn = savestr(fname);
	else {
		fn = xalloc(strlen(path) + strlen(fname) + 2);
		sprintf(fn, "%s/%s", path, fname);
	}
	fp = fopen(fn, "r");
	free(fn);
	return (fp);
}

static int32_t
input_fopen(const char *fname)
{
	FILE *fp;
	u_int p;

	if (fname[0] == '/' || fname[0] == '.' || fname[0] == '~') {
		if ((fp = tryopen(NULL, fname)) != NULL) {
			input_new(fp, NULL, fname);
			return (0);
		}

	} else {

		for (p = 0; paths[p] != NULL; p++)
			if ((fp = tryopen(paths[p], fname)) != NULL) {
				input_new(fp, paths[p], fname);
				return (0);
			}
	}

	warnx("cannot open '%s'", fname);
	return (-1);
}

static int32_t
tgetc(void)
{
	int c;

	if (pbchar != -1) {
		c = pbchar;
		pbchar = -1;
		return (c);
	}

	for (;;) {
		if (input == NULL)
			return (EOF);

		if ((c = getc(input->fp)) != EOF)
			return (c);

		input_close();
	}
}

static int32_t
tungetc(int c)
{

	if (pbchar != -1)
		return (-1);

	pbchar = c;
	return (1);
}

/************************************************************
 *
 * Parsing input
 */
enum tok {
	TOK_EOF = 0200,	/* end-of-file seen */
	TOK_NUM,	/* number */
	TOK_STR,	/* string */
	TOK_ACCESS,	/* access operator */
	TOK_TYPE,	/* type operator */
	TOK_ENUM,	/* enum token (kind of a type) */
	TOK_TYPEDEF,	/* typedef directive */
	TOK_DEFTYPE,	/* defined type */
	TOK_INCLUDE,	/* include directive */
	TOK_FILENAME,	/* filename ("foo.bar" or <foo.bar>) */
	TOK_BITS,	/* bits token (kind of a type) */
	TOK_ERR		/* unexpected char - exit */
};

static const struct {
	const char	*str;
	enum tok	tok;
	uint32_t	val;
} keywords[] = {
	{ "GET", TOK_ACCESS, FL_GET },
	{ "SET", TOK_ACCESS, FL_SET },
	{ "NULL", TOK_TYPE, SNMP_SYNTAX_NULL },
	{ "INTEGER", TOK_TYPE, SNMP_SYNTAX_INTEGER },
	{ "INTEGER32", TOK_TYPE, SNMP_SYNTAX_INTEGER },
	{ "UNSIGNED32", TOK_TYPE, SNMP_SYNTAX_GAUGE },
	{ "OCTETSTRING", TOK_TYPE, SNMP_SYNTAX_OCTETSTRING },
	{ "IPADDRESS", TOK_TYPE, SNMP_SYNTAX_IPADDRESS },
	{ "OID", TOK_TYPE, SNMP_SYNTAX_OID },
	{ "TIMETICKS", TOK_TYPE, SNMP_SYNTAX_TIMETICKS },
	{ "COUNTER", TOK_TYPE, SNMP_SYNTAX_COUNTER },
	{ "GAUGE", TOK_TYPE, SNMP_SYNTAX_GAUGE },
	{ "COUNTER64", TOK_TYPE, SNMP_SYNTAX_COUNTER64 },
	{ "ENUM", TOK_ENUM, SNMP_SYNTAX_INTEGER },
	{ "BITS", TOK_BITS, SNMP_SYNTAX_OCTETSTRING },
	{ "typedef", TOK_TYPEDEF, 0 },
	{ "include", TOK_INCLUDE, 0 },
	{ NULL, 0, 0 }
};

static struct {
	/* Current OID type, regarding table membership. */
	enum snmp_tbl_entry	tbl_type;
	/* A pointer to a structure in table list to add to its members. */
	struct snmp_index_entry	*table_idx;
} table_data;

static struct asn_oid current_oid;
static char nexttok[MAXSTR];
static u_long val;		/* integer values */
static int32_t	all_cond;	/* all conditions are true */
static int32_t saved_token = -1;

/* Prepare the global data before parsing a new file. */
static void
snmp_import_init(struct asn_oid *append)
{
	memset(&table_data, 0, sizeof(table_data));
	memset(&current_oid, 0, sizeof(struct asn_oid));
	memset(nexttok, 0, MAXSTR);

	if (append != NULL)
		asn_append_oid(&current_oid, append);

	all_cond = 0;
	val = 0;
	saved_token = -1;
}

static int32_t
gettoken(struct snmp_toolinfo *snmptoolctx)
{
	int c;
	struct enum_type *t;

	if (saved_token != -1) {
		c = saved_token;
		saved_token = -1;
		return (c);
	}

  again:
	/*
	 * Skip any whitespace before the next token.
	 */
	while ((c = tgetc()) != EOF) {
		if (c == '\n')
			input->lno++;
		if (!isspace(c))
			break;
	}
	if (c == EOF)
		return (TOK_EOF);

	if (!isascii(c)) {
		warnx("unexpected character %#2x", (u_int) c);
		return (TOK_ERR);
	}

	/*
	 * Skip comments.
	 */
	if (c == '#') {
		while ((c = tgetc()) != EOF) {
			if (c == '\n') {
				input->lno++;
				goto again;
			}
		}
		warnx("unexpected EOF in comment");
		return (TOK_ERR);
	}

	/*
	 * Single character tokens.
	 */
	if (strchr("():|", c) != NULL)
		return (c);

	if (c == '"' || c == '<') {
		int32_t end = c;
		size_t n = 0;

		val = 1;
		if (c == '<') {
			val = 0;
			end = '>';
		}

		while ((c = tgetc()) != EOF) {
			if (c == end)
				break;
			if (n == sizeof(nexttok) - 1) {
				nexttok[n++] = '\0';
				warnx("filename too long '%s...'", nexttok);
				return (TOK_ERR);
			}
			nexttok[n++] = c;
		}
		nexttok[n++] = '\0';
		return (TOK_FILENAME);
	}

	/*
	 * Sort out numbers.
	 */
	if (isdigit(c)) {
		size_t n = 0;
		nexttok[n++] = c;
		while ((c = tgetc()) != EOF) {
			if (!isdigit(c)) {
				if (tungetc(c) < 0)
					return (TOK_ERR);
				break;
			}
			if (n == sizeof(nexttok) - 1) {
				nexttok[n++] = '\0';
				warnx("number too long '%s...'", nexttok);
				return (TOK_ERR);
			}
			nexttok[n++] = c;
		}
		nexttok[n++] = '\0';
		sscanf(nexttok, "%lu", &val);
		return (TOK_NUM);
	}

	/*
	 * So that has to be a string.
	 */
	if (isalpha(c) || c == '_' || c == '-') {
		size_t n = 0;
		nexttok[n++] = c;
		while ((c = tgetc()) != EOF) {
			if (!isalnum(c) && c != '_' && c != '-') {
				if (tungetc (c) < 0)
					return (TOK_ERR);
				break;
			}
			if (n == sizeof(nexttok) - 1) {
				nexttok[n++] = '\0';
				warnx("string too long '%s...'", nexttok);
				return (TOK_ERR);
			}
			nexttok[n++] = c;
		}
		nexttok[n++] = '\0';

		/*
		 * Keywords.
		 */
		for (c = 0; keywords[c].str != NULL; c++)
			if (strcmp(keywords[c].str, nexttok) == 0) {
				val = keywords[c].val;
				return (keywords[c].tok);
			}

		if ((t = snmp_enumtc_lookup(snmptoolctx, nexttok)) != NULL) {
			val = t->syntax;
			return (TOK_DEFTYPE);
		}

		return (TOK_STR);
	}

	if (isprint(c))
		warnx("%u: unexpected character '%c'", input->lno, c);
	else
		warnx("%u: unexpected character 0x%02x", input->lno, (u_int) c);

	return (TOK_ERR);
}

/*
 * Update table information.
 */
static struct snmp_index_entry *
snmp_import_update_table(enum snmp_tbl_entry te, struct snmp_index_entry *tbl)
{
	switch (te) {
		case ENTRY_NONE:
			if (table_data.tbl_type == ENTRY_NONE)
				return (NULL);
			if (table_data.tbl_type == ENTRY_INDEX)
				table_data.table_idx = NULL;
			table_data.tbl_type--;
			return (NULL);

		case ENTRY_INDEX:
			if (tbl == NULL)
				warnx("No table_index to add!!!");
			table_data.table_idx = tbl;
			table_data.tbl_type = ENTRY_INDEX;
			return (tbl);

		case ENTRY_DATA:
			if (table_data.tbl_type == ENTRY_INDEX) {
				table_data.tbl_type = ENTRY_DATA;
				return (table_data.table_idx);
			}
			return (NULL);

		default:
			/* NOTREACHED */
			warnx("Unknown table entry type!!!");
			break;
	}

	return (NULL);
}

static int32_t
parse_enum(struct snmp_toolinfo *snmptoolctx, int32_t *tok,
    struct enum_pairs *enums)
{
	while ((*tok = gettoken(snmptoolctx)) == TOK_STR) {
		if (enum_pair_insert(enums, val, nexttok) < 0)
			return (-1);
		if ((*tok = gettoken(snmptoolctx)) != TOK_NUM)
			break;
	}

	if (*tok != ')') {
		warnx("')' at end of enums");
		return (-1);
	}

	return (1);
}

static int32_t
parse_subtype(struct snmp_toolinfo *snmptoolctx, int32_t *tok,
    enum snmp_tc *tc)
{
	if ((*tok = gettoken(snmptoolctx)) != TOK_STR) {
		warnx("subtype expected after '|'");
		return (-1);
	}

	*tc = snmp_get_tc(nexttok);
	*tok = gettoken(snmptoolctx);

	return (1);
}

static int32_t
parse_type(struct snmp_toolinfo *snmptoolctx, int32_t *tok,
    enum snmp_tc *tc, struct enum_pairs **snmp_enum)
{
	int32_t syntax, mem;

	syntax = val;
	*tc = 0;

	if (*tok == TOK_ENUM || *tok == TOK_BITS) {
		if (*snmp_enum == NULL) {
			if ((*snmp_enum = enum_pairs_init()) == NULL)
				return (-1);
			mem = 1;
			*tc = SNMP_TC_OWN;
		} else
			mem = 0;

		if (gettoken(snmptoolctx) != '(') {
			warnx("'(' expected after ENUM/BITS");
			return (-1);
		}

		if ((*tok = gettoken(snmptoolctx)) != TOK_NUM) {
			warnx("need value for ENUM//BITS");
			if (mem == 1) {
				free(*snmp_enum);
				*snmp_enum = NULL;
			}
			return (-1);
		}

		if (parse_enum(snmptoolctx, tok, *snmp_enum) < 0) {
			enum_pairs_free(*snmp_enum);
			*snmp_enum = NULL;
			return (-1);
		}

		*tok = gettoken(snmptoolctx);

	} else if (*tok == TOK_DEFTYPE) {
		struct enum_type *t;

		*tc = 0;
		t = snmp_enumtc_lookup(snmptoolctx, nexttok);
		if (t != NULL)
			*snmp_enum = t->snmp_enum;

		*tok = gettoken(snmptoolctx);

	} else {
		if ((*tok = gettoken(snmptoolctx)) == '|') {
			if (parse_subtype(snmptoolctx, tok, tc) < 0)
				return (-1);
		}
	}

	return (syntax);
}

static int32_t
snmp_import_head(struct snmp_toolinfo *snmptoolctx)
{
	enum tok tok;

	if ((tok = gettoken(snmptoolctx)) == '(')
		tok = gettoken(snmptoolctx);

	if (tok != TOK_NUM  || val > ASN_MAXID ) {
		warnx("Suboid expected - line %d", input->lno);
		return (-1);
	}

	if (gettoken(snmptoolctx) != TOK_STR) {
		warnx("Node name expected at line %d", input->lno);
		return (-1);
	}

	return (1);
}

static int32_t
snmp_import_table(struct snmp_toolinfo *snmptoolctx, struct snmp_oid2str *obj)
{
	int32_t i, tok;
	enum snmp_tc tc;
	struct snmp_index_entry *entry;

	if ((entry = calloc(1, sizeof(struct snmp_index_entry))) == NULL) {
		syslog(LOG_ERR, "malloc() failed: %s", strerror(errno));
		return (-1);
	}

	STAILQ_INIT(&(entry->index_list));

	for (i = 0, tok = gettoken(snmptoolctx); i < SNMP_INDEXES_MAX; i++) {
		int32_t syntax;
		struct enum_pairs *enums = NULL;

		if (tok != TOK_TYPE && tok != TOK_DEFTYPE && tok != TOK_ENUM &&
		    tok != TOK_BITS)
			break;

		if ((syntax = parse_type(snmptoolctx, &tok, &tc, &enums)) < 0) {
			enum_pairs_free(enums);
			snmp_index_listfree(&(entry->index_list));
			free(entry);
			return (-1);
		}

		if (snmp_syntax_insert(&(entry->index_list), enums, syntax,
		    tc) < 0) {
			snmp_index_listfree(&(entry->index_list));
			enum_pairs_free(enums);
			free(entry);
			return (-1);
		}
	}

	if (i == 0 || i > SNMP_INDEXES_MAX) {
		warnx("Bad number of indexes at line %d", input->lno);
		snmp_index_listfree(&(entry->index_list));
		free(entry);
		return (-1);
	}

	if (tok != TOK_STR) {
		warnx("String expected after indexes at line %d", input->lno);
		snmp_index_listfree(&(entry->index_list));
		free(entry);
		return (-1);
	}

	entry->string = obj->string;
	entry->strlen = obj->strlen;
	asn_append_oid(&(entry->var), &(obj->var));

	if ((i = snmp_table_insert(snmptoolctx, entry)) < 0) {
		snmp_index_listfree(&(entry->index_list));
		free(entry);
		return (-1);
	} else if (i == 0) {
		/* Same entry already present in lists. */
		free(entry->string);
		free(entry);
		return (0);
	}

	(void) snmp_import_update_table(ENTRY_INDEX, entry);

	return (1);
}

/*
 * Read everything after the syntax type that is certainly a leaf OID info.
 */
static int32_t
snmp_import_leaf(struct snmp_toolinfo *snmptoolctx, int32_t *tok,
    struct snmp_oid2str *oid2str)
{
	int32_t i, syntax;

	if ((syntax = parse_type(snmptoolctx, tok, &(oid2str->tc), &(oid2str->snmp_enum)))
	    < 0)
		return(-1);

	oid2str->syntax = syntax;
	/*
	 * That is the name of the function, corresponding to the entry.
	 * It is used by bsnmpd, but is not interesting for us.
	 */
	if (*tok == TOK_STR)
		*tok = gettoken(snmptoolctx);

	for (i = 0; i < SNMP_ACCESS_GETSET && *tok == TOK_ACCESS; i++) {
		oid2str->access |=  (uint32_t) val;
		*tok = gettoken(snmptoolctx);
	}

	if (*tok != ')') {
		warnx("')' expected at end of line %d", input->lno);
		return (-1);
	}

	oid2str->table_idx = snmp_import_update_table(ENTRY_DATA,  NULL);

	if ((i = snmp_leaf_insert(snmptoolctx, oid2str)) < 0) {
		warnx("Error adding leaf %s to list", oid2str->string);
		return (-1);
	}

	/*
	 * Same entry is already present in the mapping lists and
	 * the new one was not inserted.
	 */
	if (i == 0)  {
		free(oid2str->string);
		free(oid2str);
	}

	(void) snmp_import_update_table(ENTRY_NONE, NULL);

	return (1);
}

static int32_t
snmp_import_object(struct snmp_toolinfo *snmptoolctx)
{
	char *string;
	int i;
	int32_t tok;
	struct snmp_oid2str *oid2str;

	if (snmp_import_head(snmptoolctx) < 0)
		return (-1);

	if ((oid2str = calloc(1, sizeof(struct snmp_oid2str))) == NULL) {
		syslog(LOG_ERR, "calloc() failed: %s", strerror(errno));
		return (-1);
	}

	if ((string = strdup(nexttok)) == NULL) {
		syslog(LOG_ERR, "strdup() failed: %s", strerror(errno));
		free(oid2str);
		return (-1);
	}

	oid2str->string = string;
	oid2str->strlen = strlen(nexttok);

	asn_append_oid(&(oid2str->var), &(current_oid));
	if (snmp_suboid_append(&(oid2str->var), (asn_subid_t) val) < 0)
		goto error;

	/*
	 * Prepared the entry - now figure out where to insert it.
	 * After the object we have following options:
	 * 1) new line, blank, ) - then it is an enum oid -> snmp_enumlist;
	 * 2) new line , ( - nonleaf oid -> snmp_nodelist;
	 * 2) ':' - table entry - a variable length SYNTAX_TYPE (one or more)
	 *     may follow and second string must end line -> snmp_tablelist;
	 * 3) OID , string  ) - this is a trap entry or a leaf -> snmp_oidlist;
	 * 4) SYNTAX_TYPE, string (not always), get/set modifier - always last
	 *     and )- this is definitely a leaf.
	 */

	switch (tok = gettoken(snmptoolctx)) {
	    case  ')':
		if ((i = snmp_enum_insert(snmptoolctx, oid2str)) < 0)
			goto error;
		if (i == 0) {
			free(oid2str->string);
			free(oid2str);
		}
		return (1);

	    case '(':
		if (snmp_suboid_append(&current_oid, (asn_subid_t) val) < 0)
			goto error;

		/*
		 * Ignore the error for nodes since the .def files currently
		 * contain different strings for 1.3.6.1.2.1 - mibII. Only make
		 * sure the memory is freed and don't complain.
		 */
		if ((i = snmp_node_insert(snmptoolctx, oid2str)) <= 0) {
			free(string);
			free(oid2str);
		}
		return (snmp_import_object(snmptoolctx));

	    case ':':
		if (snmp_suboid_append(&current_oid, (asn_subid_t) val) < 0)
			goto error;
		if (snmp_import_table(snmptoolctx, oid2str) < 0)
			goto error;
		/*
		 * A different table entry type was malloced and the data is
		 * contained there.
		 */
		free(oid2str);
		return (1);

	    case TOK_TYPE:
		/* FALLTHROUGH */
	    case TOK_DEFTYPE:
		/* FALLTHROUGH */
	    case TOK_ENUM:
	    	/* FALLTHROUGH */
	    case TOK_BITS:
		if (snmp_import_leaf(snmptoolctx, &tok, oid2str) < 0)
				goto error;
		return (1);

	    default:
		warnx("Unexpected token at line %d - %s", input->lno,
		    input->fname);
		break;
	}

error:
	snmp_mapping_entryfree(oid2str);

	return (-1);
}

static int32_t
snmp_import_tree(struct snmp_toolinfo *snmptoolctx, int32_t *tok)
{
	while (*tok != TOK_EOF) {
		switch (*tok) {
		    case TOK_ERR:
			return (-1);
		    case '(':
			if (snmp_import_object(snmptoolctx) < 0)
			    return (-1);
			break;
		    case ')':
			if (snmp_suboid_pop(&current_oid) < 0)
			    return (-1);
			(void) snmp_import_update_table(ENTRY_NONE, NULL);
			break;
		    default:
			/* Anything else here would be illegal. */
			return (-1);
		}
		*tok = gettoken(snmptoolctx);
	}

	return (0);
}

static int32_t
snmp_import_top(struct snmp_toolinfo *snmptoolctx, int32_t *tok)
{
	enum snmp_tc tc;
	struct enum_type *t;

	if (*tok == '(')
		return (snmp_import_tree(snmptoolctx, tok));

	if (*tok == TOK_TYPEDEF) {
		if ((*tok = gettoken(snmptoolctx)) != TOK_STR) {
			warnx("type name expected after typedef - %s",
			    input->fname);
			return (-1);
		}

		t = snmp_enumtc_init(nexttok);

		*tok = gettoken(snmptoolctx);
		t->is_enum = (*tok == TOK_ENUM);
		t->is_bits = (*tok == TOK_BITS);
		t->syntax = parse_type(snmptoolctx, tok, &tc, &(t->snmp_enum));
		snmp_enumtc_insert(snmptoolctx, t);

		return (1);
	}

	if (*tok == TOK_INCLUDE) {
		int i;

		*tok = gettoken(snmptoolctx);
		if (*tok != TOK_FILENAME) {
			warnx("filename expected in include directive - %s",
			    nexttok);
			return (-1);
		}

		if (( i = add_filename(snmptoolctx, nexttok, NULL, 1)) == 0) {
			*tok = gettoken(snmptoolctx);
			return (1);
		}

		if (i == -1)
			return (-1);

		input_fopen(nexttok);
		*tok = gettoken(snmptoolctx);
		return (1);
	}

	warnx("'(' or 'typedef' expected - %s", nexttok);
	return (-1);
}

static int32_t
snmp_import(struct snmp_toolinfo *snmptoolctx)
{
	int i;
	int32_t tok;

	tok = gettoken(snmptoolctx);

	do
		i = snmp_import_top(snmptoolctx, &tok);
	while (i > 0);

	return (i);
}

/*
 * Read a .def file and import oid<->string mapping.
 * Mappings are inserted into a global structure containing list for each OID
 * syntax type.
 */
int32_t
snmp_import_file(struct snmp_toolinfo *snmptoolctx, struct fname *file)
{
	int idx;

	snmp_import_init(&(file->cut));
	input_fopen(file->name);
	if ((idx = snmp_import(snmptoolctx)) < 0)
		warnx("Failed to read mappings from file %s", file->name);

	input_close();

	return (idx);
}
