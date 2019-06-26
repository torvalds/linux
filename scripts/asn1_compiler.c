/* Simplified ASN.1 notation parser
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <linux/asn1_ber_bytecode.h>

enum token_type {
	DIRECTIVE_ABSENT,
	DIRECTIVE_ALL,
	DIRECTIVE_ANY,
	DIRECTIVE_APPLICATION,
	DIRECTIVE_AUTOMATIC,
	DIRECTIVE_BEGIN,
	DIRECTIVE_BIT,
	DIRECTIVE_BMPString,
	DIRECTIVE_BOOLEAN,
	DIRECTIVE_BY,
	DIRECTIVE_CHARACTER,
	DIRECTIVE_CHOICE,
	DIRECTIVE_CLASS,
	DIRECTIVE_COMPONENT,
	DIRECTIVE_COMPONENTS,
	DIRECTIVE_CONSTRAINED,
	DIRECTIVE_CONTAINING,
	DIRECTIVE_DEFAULT,
	DIRECTIVE_DEFINED,
	DIRECTIVE_DEFINITIONS,
	DIRECTIVE_EMBEDDED,
	DIRECTIVE_ENCODED,
	DIRECTIVE_ENCODING_CONTROL,
	DIRECTIVE_END,
	DIRECTIVE_ENUMERATED,
	DIRECTIVE_EXCEPT,
	DIRECTIVE_EXPLICIT,
	DIRECTIVE_EXPORTS,
	DIRECTIVE_EXTENSIBILITY,
	DIRECTIVE_EXTERNAL,
	DIRECTIVE_FALSE,
	DIRECTIVE_FROM,
	DIRECTIVE_GeneralString,
	DIRECTIVE_GeneralizedTime,
	DIRECTIVE_GraphicString,
	DIRECTIVE_IA5String,
	DIRECTIVE_IDENTIFIER,
	DIRECTIVE_IMPLICIT,
	DIRECTIVE_IMPLIED,
	DIRECTIVE_IMPORTS,
	DIRECTIVE_INCLUDES,
	DIRECTIVE_INSTANCE,
	DIRECTIVE_INSTRUCTIONS,
	DIRECTIVE_INTEGER,
	DIRECTIVE_INTERSECTION,
	DIRECTIVE_ISO646String,
	DIRECTIVE_MAX,
	DIRECTIVE_MIN,
	DIRECTIVE_MINUS_INFINITY,
	DIRECTIVE_NULL,
	DIRECTIVE_NumericString,
	DIRECTIVE_OBJECT,
	DIRECTIVE_OCTET,
	DIRECTIVE_OF,
	DIRECTIVE_OPTIONAL,
	DIRECTIVE_ObjectDescriptor,
	DIRECTIVE_PATTERN,
	DIRECTIVE_PDV,
	DIRECTIVE_PLUS_INFINITY,
	DIRECTIVE_PRESENT,
	DIRECTIVE_PRIVATE,
	DIRECTIVE_PrintableString,
	DIRECTIVE_REAL,
	DIRECTIVE_RELATIVE_OID,
	DIRECTIVE_SEQUENCE,
	DIRECTIVE_SET,
	DIRECTIVE_SIZE,
	DIRECTIVE_STRING,
	DIRECTIVE_SYNTAX,
	DIRECTIVE_T61String,
	DIRECTIVE_TAGS,
	DIRECTIVE_TRUE,
	DIRECTIVE_TeletexString,
	DIRECTIVE_UNION,
	DIRECTIVE_UNIQUE,
	DIRECTIVE_UNIVERSAL,
	DIRECTIVE_UTCTime,
	DIRECTIVE_UTF8String,
	DIRECTIVE_UniversalString,
	DIRECTIVE_VideotexString,
	DIRECTIVE_VisibleString,
	DIRECTIVE_WITH,
	NR__DIRECTIVES,
	TOKEN_ASSIGNMENT = NR__DIRECTIVES,
	TOKEN_OPEN_CURLY,
	TOKEN_CLOSE_CURLY,
	TOKEN_OPEN_SQUARE,
	TOKEN_CLOSE_SQUARE,
	TOKEN_OPEN_ACTION,
	TOKEN_CLOSE_ACTION,
	TOKEN_COMMA,
	TOKEN_NUMBER,
	TOKEN_TYPE_NAME,
	TOKEN_ELEMENT_NAME,
	NR__TOKENS
};

static const unsigned char token_to_tag[NR__TOKENS] = {
	/* EOC goes first */
	[DIRECTIVE_BOOLEAN]		= ASN1_BOOL,
	[DIRECTIVE_INTEGER]		= ASN1_INT,
	[DIRECTIVE_BIT]			= ASN1_BTS,
	[DIRECTIVE_OCTET]		= ASN1_OTS,
	[DIRECTIVE_NULL]		= ASN1_NULL,
	[DIRECTIVE_OBJECT]		= ASN1_OID,
	[DIRECTIVE_ObjectDescriptor]	= ASN1_ODE,
	[DIRECTIVE_EXTERNAL]		= ASN1_EXT,
	[DIRECTIVE_REAL]		= ASN1_REAL,
	[DIRECTIVE_ENUMERATED]		= ASN1_ENUM,
	[DIRECTIVE_EMBEDDED]		= 0,
	[DIRECTIVE_UTF8String]		= ASN1_UTF8STR,
	[DIRECTIVE_RELATIVE_OID]	= ASN1_RELOID,
	/* 14 */
	/* 15 */
	[DIRECTIVE_SEQUENCE]		= ASN1_SEQ,
	[DIRECTIVE_SET]			= ASN1_SET,
	[DIRECTIVE_NumericString]	= ASN1_NUMSTR,
	[DIRECTIVE_PrintableString]	= ASN1_PRNSTR,
	[DIRECTIVE_T61String]		= ASN1_TEXSTR,
	[DIRECTIVE_TeletexString]	= ASN1_TEXSTR,
	[DIRECTIVE_VideotexString]	= ASN1_VIDSTR,
	[DIRECTIVE_IA5String]		= ASN1_IA5STR,
	[DIRECTIVE_UTCTime]		= ASN1_UNITIM,
	[DIRECTIVE_GeneralizedTime]	= ASN1_GENTIM,
	[DIRECTIVE_GraphicString]	= ASN1_GRASTR,
	[DIRECTIVE_VisibleString]	= ASN1_VISSTR,
	[DIRECTIVE_GeneralString]	= ASN1_GENSTR,
	[DIRECTIVE_UniversalString]	= ASN1_UNITIM,
	[DIRECTIVE_CHARACTER]		= ASN1_CHRSTR,
	[DIRECTIVE_BMPString]		= ASN1_BMPSTR,
};

static const char asn1_classes[4][5] = {
	[ASN1_UNIV]	= "UNIV",
	[ASN1_APPL]	= "APPL",
	[ASN1_CONT]	= "CONT",
	[ASN1_PRIV]	= "PRIV"
};

static const char asn1_methods[2][5] = {
	[ASN1_UNIV]	= "PRIM",
	[ASN1_APPL]	= "CONS"
};

static const char *const asn1_universal_tags[32] = {
	"EOC",
	"BOOL",
	"INT",
	"BTS",
	"OTS",
	"NULL",
	"OID",
	"ODE",
	"EXT",
	"REAL",
	"ENUM",
	"EPDV",
	"UTF8STR",
	"RELOID",
	NULL,		/* 14 */
	NULL,		/* 15 */
	"SEQ",
	"SET",
	"NUMSTR",
	"PRNSTR",
	"TEXSTR",
	"VIDSTR",
	"IA5STR",
	"UNITIM",
	"GENTIM",
	"GRASTR",
	"VISSTR",
	"GENSTR",
	"UNISTR",
	"CHRSTR",
	"BMPSTR",
	NULL		/* 31 */
};

static const char *filename;
static const char *grammar_name;
static const char *outputname;
static const char *headername;

static const char *const directives[NR__DIRECTIVES] = {
#define _(X) [DIRECTIVE_##X] = #X
	_(ABSENT),
	_(ALL),
	_(ANY),
	_(APPLICATION),
	_(AUTOMATIC),
	_(BEGIN),
	_(BIT),
	_(BMPString),
	_(BOOLEAN),
	_(BY),
	_(CHARACTER),
	_(CHOICE),
	_(CLASS),
	_(COMPONENT),
	_(COMPONENTS),
	_(CONSTRAINED),
	_(CONTAINING),
	_(DEFAULT),
	_(DEFINED),
	_(DEFINITIONS),
	_(EMBEDDED),
	_(ENCODED),
	[DIRECTIVE_ENCODING_CONTROL] = "ENCODING-CONTROL",
	_(END),
	_(ENUMERATED),
	_(EXCEPT),
	_(EXPLICIT),
	_(EXPORTS),
	_(EXTENSIBILITY),
	_(EXTERNAL),
	_(FALSE),
	_(FROM),
	_(GeneralString),
	_(GeneralizedTime),
	_(GraphicString),
	_(IA5String),
	_(IDENTIFIER),
	_(IMPLICIT),
	_(IMPLIED),
	_(IMPORTS),
	_(INCLUDES),
	_(INSTANCE),
	_(INSTRUCTIONS),
	_(INTEGER),
	_(INTERSECTION),
	_(ISO646String),
	_(MAX),
	_(MIN),
	[DIRECTIVE_MINUS_INFINITY] = "MINUS-INFINITY",
	[DIRECTIVE_NULL] = "NULL",
	_(NumericString),
	_(OBJECT),
	_(OCTET),
	_(OF),
	_(OPTIONAL),
	_(ObjectDescriptor),
	_(PATTERN),
	_(PDV),
	[DIRECTIVE_PLUS_INFINITY] = "PLUS-INFINITY",
	_(PRESENT),
	_(PRIVATE),
	_(PrintableString),
	_(REAL),
	[DIRECTIVE_RELATIVE_OID] = "RELATIVE-OID",
	_(SEQUENCE),
	_(SET),
	_(SIZE),
	_(STRING),
	_(SYNTAX),
	_(T61String),
	_(TAGS),
	_(TRUE),
	_(TeletexString),
	_(UNION),
	_(UNIQUE),
	_(UNIVERSAL),
	_(UTCTime),
	_(UTF8String),
	_(UniversalString),
	_(VideotexString),
	_(VisibleString),
	_(WITH)
};

struct action {
	struct action	*next;
	char		*name;
	unsigned char	index;
};

static struct action *action_list;
static unsigned nr_actions;

struct token {
	unsigned short	line;
	enum token_type	token_type : 8;
	unsigned char	size;
	struct action	*action;
	char		*content;
	struct type	*type;
};

static struct token *token_list;
static unsigned nr_tokens;
static bool verbose_opt;
static bool debug_opt;

#define verbose(fmt, ...) do { if (verbose_opt) printf(fmt, ## __VA_ARGS__); } while (0)
#define debug(fmt, ...) do { if (debug_opt) printf(fmt, ## __VA_ARGS__); } while (0)

static int directive_compare(const void *_key, const void *_pdir)
{
	const struct token *token = _key;
	const char *const *pdir = _pdir, *dir = *pdir;
	size_t dlen, clen;
	int val;

	dlen = strlen(dir);
	clen = (dlen < token->size) ? dlen : token->size;

	//debug("cmp(%s,%s) = ", token->content, dir);

	val = memcmp(token->content, dir, clen);
	if (val != 0) {
		//debug("%d [cmp]\n", val);
		return val;
	}

	if (dlen == token->size) {
		//debug("0\n");
		return 0;
	}
	//debug("%d\n", (int)dlen - (int)token->size);
	return dlen - token->size; /* shorter -> negative */
}

/*
 * Tokenise an ASN.1 grammar
 */
static void tokenise(char *buffer, char *end)
{
	struct token *tokens;
	char *line, *nl, *start, *p, *q;
	unsigned tix, lineno;

	/* Assume we're going to have half as many tokens as we have
	 * characters
	 */
	token_list = tokens = calloc((end - buffer) / 2, sizeof(struct token));
	if (!tokens) {
		perror(NULL);
		exit(1);
	}
	tix = 0;

	lineno = 0;
	while (buffer < end) {
		/* First of all, break out a line */
		lineno++;
		line = buffer;
		nl = memchr(line, '\n', end - buffer);
		if (!nl) {
			buffer = nl = end;
		} else {
			buffer = nl + 1;
			*nl = '\0';
		}

		/* Remove "--" comments */
		p = line;
	next_comment:
		while ((p = memchr(p, '-', nl - p))) {
			if (p[1] == '-') {
				/* Found a comment; see if there's a terminator */
				q = p + 2;
				while ((q = memchr(q, '-', nl - q))) {
					if (q[1] == '-') {
						/* There is - excise the comment */
						q += 2;
						memmove(p, q, nl - q);
						goto next_comment;
					}
					q++;
				}
				*p = '\0';
				nl = p;
				break;
			} else {
				p++;
			}
		}

		p = line;
		while (p < nl) {
			/* Skip white space */
			while (p < nl && isspace(*p))
				*(p++) = 0;
			if (p >= nl)
				break;

			tokens[tix].line = lineno;
			start = p;

			/* Handle string tokens */
			if (isalpha(*p)) {
				const char **dir;

				/* Can be a directive, type name or element
				 * name.  Find the end of the name.
				 */
				q = p + 1;
				while (q < nl && (isalnum(*q) || *q == '-' || *q == '_'))
					q++;
				tokens[tix].size = q - p;
				p = q;

				tokens[tix].content = malloc(tokens[tix].size + 1);
				if (!tokens[tix].content) {
					perror(NULL);
					exit(1);
				}
				memcpy(tokens[tix].content, start, tokens[tix].size);
				tokens[tix].content[tokens[tix].size] = 0;
				
				/* If it begins with a lowercase letter then
				 * it's an element name
				 */
				if (islower(tokens[tix].content[0])) {
					tokens[tix++].token_type = TOKEN_ELEMENT_NAME;
					continue;
				}

				/* Otherwise we need to search the directive
				 * table
				 */
				dir = bsearch(&tokens[tix], directives,
					      sizeof(directives) / sizeof(directives[1]),
					      sizeof(directives[1]),
					      directive_compare);
				if (dir) {
					tokens[tix++].token_type = dir - directives;
					continue;
				}

				tokens[tix++].token_type = TOKEN_TYPE_NAME;
				continue;
			}

			/* Handle numbers */
			if (isdigit(*p)) {
				/* Find the end of the number */
				q = p + 1;
				while (q < nl && (isdigit(*q)))
					q++;
				tokens[tix].size = q - p;
				p = q;
				tokens[tix].content = malloc(tokens[tix].size + 1);
				if (!tokens[tix].content) {
					perror(NULL);
					exit(1);
				}
				memcpy(tokens[tix].content, start, tokens[tix].size);
				tokens[tix].content[tokens[tix].size] = 0;
				tokens[tix++].token_type = TOKEN_NUMBER;
				continue;
			}

			if (nl - p >= 3) {
				if (memcmp(p, "::=", 3) == 0) {
					p += 3;
					tokens[tix].size = 3;
					tokens[tix].content = "::=";
					tokens[tix++].token_type = TOKEN_ASSIGNMENT;
					continue;
				}
			}

			if (nl - p >= 2) {
				if (memcmp(p, "({", 2) == 0) {
					p += 2;
					tokens[tix].size = 2;
					tokens[tix].content = "({";
					tokens[tix++].token_type = TOKEN_OPEN_ACTION;
					continue;
				}
				if (memcmp(p, "})", 2) == 0) {
					p += 2;
					tokens[tix].size = 2;
					tokens[tix].content = "})";
					tokens[tix++].token_type = TOKEN_CLOSE_ACTION;
					continue;
				}
			}

			if (nl - p >= 1) {
				tokens[tix].size = 1;
				switch (*p) {
				case '{':
					p += 1;
					tokens[tix].content = "{";
					tokens[tix++].token_type = TOKEN_OPEN_CURLY;
					continue;
				case '}':
					p += 1;
					tokens[tix].content = "}";
					tokens[tix++].token_type = TOKEN_CLOSE_CURLY;
					continue;
				case '[':
					p += 1;
					tokens[tix].content = "[";
					tokens[tix++].token_type = TOKEN_OPEN_SQUARE;
					continue;
				case ']':
					p += 1;
					tokens[tix].content = "]";
					tokens[tix++].token_type = TOKEN_CLOSE_SQUARE;
					continue;
				case ',':
					p += 1;
					tokens[tix].content = ",";
					tokens[tix++].token_type = TOKEN_COMMA;
					continue;
				default:
					break;
				}
			}

			fprintf(stderr, "%s:%u: Unknown character in grammar: '%c'\n",
				filename, lineno, *p);
			exit(1);
		}
	}

	nr_tokens = tix;
	verbose("Extracted %u tokens\n", nr_tokens);

#if 0
	{
		int n;
		for (n = 0; n < nr_tokens; n++)
			debug("Token %3u: '%s'\n", n, token_list[n].content);
	}
#endif
}

static void build_type_list(void);
static void parse(void);
static void dump_elements(void);
static void render(FILE *out, FILE *hdr);

/*
 *
 */
int main(int argc, char **argv)
{
	struct stat st;
	ssize_t readlen;
	FILE *out, *hdr;
	char *buffer, *p;
	char *kbuild_verbose;
	int fd;

	kbuild_verbose = getenv("KBUILD_VERBOSE");
	if (kbuild_verbose)
		verbose_opt = atoi(kbuild_verbose);

	while (argc > 4) {
		if (strcmp(argv[1], "-v") == 0)
			verbose_opt = true;
		else if (strcmp(argv[1], "-d") == 0)
			debug_opt = true;
		else
			break;
		memmove(&argv[1], &argv[2], (argc - 2) * sizeof(char *));
		argc--;
	}

	if (argc != 4) {
		fprintf(stderr, "Format: %s [-v] [-d] <grammar-file> <c-file> <hdr-file>\n",
			argv[0]);
		exit(2);
	}

	filename = argv[1];
	outputname = argv[2];
	headername = argv[3];

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		perror(filename);
		exit(1);
	}

	if (fstat(fd, &st) < 0) {
		perror(filename);
		exit(1);
	}

	if (!(buffer = malloc(st.st_size + 1))) {
		perror(NULL);
		exit(1);
	}

	if ((readlen = read(fd, buffer, st.st_size)) < 0) {
		perror(filename);
		exit(1);
	}

	if (close(fd) < 0) {
		perror(filename);
		exit(1);
	}

	if (readlen != st.st_size) {
		fprintf(stderr, "%s: Short read\n", filename);
		exit(1);
	}

	p = strrchr(argv[1], '/');
	p = p ? p + 1 : argv[1];
	grammar_name = strdup(p);
	if (!p) {
		perror(NULL);
		exit(1);
	}
	p = strchr(grammar_name, '.');
	if (p)
		*p = '\0';

	buffer[readlen] = 0;
	tokenise(buffer, buffer + readlen);
	build_type_list();
	parse();
	dump_elements();

	out = fopen(outputname, "w");
	if (!out) {
		perror(outputname);
		exit(1);
	}

	hdr = fopen(headername, "w");
	if (!hdr) {
		perror(headername);
		exit(1);
	}

	render(out, hdr);

	if (fclose(out) < 0) {
		perror(outputname);
		exit(1);
	}

	if (fclose(hdr) < 0) {
		perror(headername);
		exit(1);
	}

	return 0;
}

enum compound {
	NOT_COMPOUND,
	SET,
	SET_OF,
	SEQUENCE,
	SEQUENCE_OF,
	CHOICE,
	ANY,
	TYPE_REF,
	TAG_OVERRIDE
};

struct element {
	struct type	*type_def;
	struct token	*name;
	struct token	*type;
	struct action	*action;
	struct element	*children;
	struct element	*next;
	struct element	*render_next;
	struct element	*list_next;
	uint8_t		n_elements;
	enum compound	compound : 8;
	enum asn1_class	class : 8;
	enum asn1_method method : 8;
	uint8_t		tag;
	unsigned	entry_index;
	unsigned	flags;
#define ELEMENT_IMPLICIT	0x0001
#define ELEMENT_EXPLICIT	0x0002
#define ELEMENT_TAG_SPECIFIED	0x0004
#define ELEMENT_RENDERED	0x0008
#define ELEMENT_SKIPPABLE	0x0010
#define ELEMENT_CONDITIONAL	0x0020
};

struct type {
	struct token	*name;
	struct token	*def;
	struct element	*element;
	unsigned	ref_count;
	unsigned	flags;
#define TYPE_STOP_MARKER	0x0001
#define TYPE_BEGIN		0x0002
};

static struct type *type_list;
static struct type **type_index;
static unsigned nr_types;

static int type_index_compare(const void *_a, const void *_b)
{
	const struct type *const *a = _a, *const *b = _b;

	if ((*a)->name->size != (*b)->name->size)
		return (*a)->name->size - (*b)->name->size;
	else
		return memcmp((*a)->name->content, (*b)->name->content,
			      (*a)->name->size);
}

static int type_finder(const void *_key, const void *_ti)
{
	const struct token *token = _key;
	const struct type *const *ti = _ti;
	const struct type *type = *ti;

	if (token->size != type->name->size)
		return token->size - type->name->size;
	else
		return memcmp(token->content, type->name->content,
			      token->size);
}

/*
 * Build up a list of types and a sorted index to that list.
 */
static void build_type_list(void)
{
	struct type *types;
	unsigned nr, t, n;

	nr = 0;
	for (n = 0; n < nr_tokens - 1; n++)
		if (token_list[n + 0].token_type == TOKEN_TYPE_NAME &&
		    token_list[n + 1].token_type == TOKEN_ASSIGNMENT)
			nr++;

	if (nr == 0) {
		fprintf(stderr, "%s: No defined types\n", filename);
		exit(1);
	}

	nr_types = nr;
	types = type_list = calloc(nr + 1, sizeof(type_list[0]));
	if (!type_list) {
		perror(NULL);
		exit(1);
	}
	type_index = calloc(nr, sizeof(type_index[0]));
	if (!type_index) {
		perror(NULL);
		exit(1);
	}

	t = 0;
	types[t].flags |= TYPE_BEGIN;
	for (n = 0; n < nr_tokens - 1; n++) {
		if (token_list[n + 0].token_type == TOKEN_TYPE_NAME &&
		    token_list[n + 1].token_type == TOKEN_ASSIGNMENT) {
			types[t].name = &token_list[n];
			type_index[t] = &types[t];
			t++;
		}
	}
	types[t].name = &token_list[n + 1];
	types[t].flags |= TYPE_STOP_MARKER;

	qsort(type_index, nr, sizeof(type_index[0]), type_index_compare);

	verbose("Extracted %u types\n", nr_types);
#if 0
	for (n = 0; n < nr_types; n++) {
		struct type *type = type_index[n];
		debug("- %*.*s\n", type->name->content);
	}
#endif
}

static struct element *parse_type(struct token **_cursor, struct token *stop,
				  struct token *name);

/*
 * Parse the token stream
 */
static void parse(void)
{
	struct token *cursor;
	struct type *type;

	/* Parse one type definition statement at a time */
	type = type_list;
	do {
		cursor = type->name;

		if (cursor[0].token_type != TOKEN_TYPE_NAME ||
		    cursor[1].token_type != TOKEN_ASSIGNMENT)
			abort();
		cursor += 2;

		type->element = parse_type(&cursor, type[1].name, NULL);
		type->element->type_def = type;

		if (cursor != type[1].name) {
			fprintf(stderr, "%s:%d: Parse error at token '%s'\n",
				filename, cursor->line, cursor->content);
			exit(1);
		}

	} while (type++, !(type->flags & TYPE_STOP_MARKER));

	verbose("Extracted %u actions\n", nr_actions);
}

static struct element *element_list;

static struct element *alloc_elem(struct token *type)
{
	struct element *e = calloc(1, sizeof(*e));
	if (!e) {
		perror(NULL);
		exit(1);
	}
	e->list_next = element_list;
	element_list = e;
	return e;
}

static struct element *parse_compound(struct token **_cursor, struct token *end,
				      int alternates);

/*
 * Parse one type definition statement
 */
static struct element *parse_type(struct token **_cursor, struct token *end,
				  struct token *name)
{
	struct element *top, *element;
	struct action *action, **ppaction;
	struct token *cursor = *_cursor;
	struct type **ref;
	char *p;
	int labelled = 0, implicit = 0;

	top = element = alloc_elem(cursor);
	element->class = ASN1_UNIV;
	element->method = ASN1_PRIM;
	element->tag = token_to_tag[cursor->token_type];
	element->name = name;

	/* Extract the tag value if one given */
	if (cursor->token_type == TOKEN_OPEN_SQUARE) {
		cursor++;
		if (cursor >= end)
			goto overrun_error;
		switch (cursor->token_type) {
		case DIRECTIVE_UNIVERSAL:
			element->class = ASN1_UNIV;
			cursor++;
			break;
		case DIRECTIVE_APPLICATION:
			element->class = ASN1_APPL;
			cursor++;
			break;
		case TOKEN_NUMBER:
			element->class = ASN1_CONT;
			break;
		case DIRECTIVE_PRIVATE:
			element->class = ASN1_PRIV;
			cursor++;
			break;
		default:
			fprintf(stderr, "%s:%d: Unrecognised tag class token '%s'\n",
				filename, cursor->line, cursor->content);
			exit(1);
		}

		if (cursor >= end)
			goto overrun_error;
		if (cursor->token_type != TOKEN_NUMBER) {
			fprintf(stderr, "%s:%d: Missing tag number '%s'\n",
				filename, cursor->line, cursor->content);
			exit(1);
		}

		element->tag &= ~0x1f;
		element->tag |= strtoul(cursor->content, &p, 10);
		element->flags |= ELEMENT_TAG_SPECIFIED;
		if (p - cursor->content != cursor->size)
			abort();
		cursor++;

		if (cursor >= end)
			goto overrun_error;
		if (cursor->token_type != TOKEN_CLOSE_SQUARE) {
			fprintf(stderr, "%s:%d: Missing closing square bracket '%s'\n",
				filename, cursor->line, cursor->content);
			exit(1);
		}
		cursor++;
		if (cursor >= end)
			goto overrun_error;
		labelled = 1;
	}

	/* Handle implicit and explicit markers */
	if (cursor->token_type == DIRECTIVE_IMPLICIT) {
		element->flags |= ELEMENT_IMPLICIT;
		implicit = 1;
		cursor++;
		if (cursor >= end)
			goto overrun_error;
	} else if (cursor->token_type == DIRECTIVE_EXPLICIT) {
		element->flags |= ELEMENT_EXPLICIT;
		cursor++;
		if (cursor >= end)
			goto overrun_error;
	}

	if (labelled) {
		if (!implicit)
			element->method |= ASN1_CONS;
		element->compound = implicit ? TAG_OVERRIDE : SEQUENCE;
		element->children = alloc_elem(cursor);
		element = element->children;
		element->class = ASN1_UNIV;
		element->method = ASN1_PRIM;
		element->tag = token_to_tag[cursor->token_type];
		element->name = name;
	}

	/* Extract the type we're expecting here */
	element->type = cursor;
	switch (cursor->token_type) {
	case DIRECTIVE_ANY:
		element->compound = ANY;
		cursor++;
		break;

	case DIRECTIVE_NULL:
	case DIRECTIVE_BOOLEAN:
	case DIRECTIVE_ENUMERATED:
	case DIRECTIVE_INTEGER:
		element->compound = NOT_COMPOUND;
		cursor++;
		break;

	case DIRECTIVE_EXTERNAL:
		element->method = ASN1_CONS;

	case DIRECTIVE_BMPString:
	case DIRECTIVE_GeneralString:
	case DIRECTIVE_GraphicString:
	case DIRECTIVE_IA5String:
	case DIRECTIVE_ISO646String:
	case DIRECTIVE_NumericString:
	case DIRECTIVE_PrintableString:
	case DIRECTIVE_T61String:
	case DIRECTIVE_TeletexString:
	case DIRECTIVE_UniversalString:
	case DIRECTIVE_UTF8String:
	case DIRECTIVE_VideotexString:
	case DIRECTIVE_VisibleString:
	case DIRECTIVE_ObjectDescriptor:
	case DIRECTIVE_GeneralizedTime:
	case DIRECTIVE_UTCTime:
		element->compound = NOT_COMPOUND;
		cursor++;
		break;

	case DIRECTIVE_BIT:
	case DIRECTIVE_OCTET:
		element->compound = NOT_COMPOUND;
		cursor++;
		if (cursor >= end)
			goto overrun_error;
		if (cursor->token_type != DIRECTIVE_STRING)
			goto parse_error;
		cursor++;
		break;

	case DIRECTIVE_OBJECT:
		element->compound = NOT_COMPOUND;
		cursor++;
		if (cursor >= end)
			goto overrun_error;
		if (cursor->token_type != DIRECTIVE_IDENTIFIER)
			goto parse_error;
		cursor++;
		break;

	case TOKEN_TYPE_NAME:
		element->compound = TYPE_REF;
		ref = bsearch(cursor, type_index, nr_types, sizeof(type_index[0]),
			      type_finder);
		if (!ref) {
			fprintf(stderr, "%s:%d: Type '%s' undefined\n",
				filename, cursor->line, cursor->content);
			exit(1);
		}
		cursor->type = *ref;
		(*ref)->ref_count++;
		cursor++;
		break;

	case DIRECTIVE_CHOICE:
		element->compound = CHOICE;
		cursor++;
		element->children = parse_compound(&cursor, end, 1);
		break;

	case DIRECTIVE_SEQUENCE:
		element->compound = SEQUENCE;
		element->method = ASN1_CONS;
		cursor++;
		if (cursor >= end)
			goto overrun_error;
		if (cursor->token_type == DIRECTIVE_OF) {
			element->compound = SEQUENCE_OF;
			cursor++;
			if (cursor >= end)
				goto overrun_error;
			element->children = parse_type(&cursor, end, NULL);
		} else {
			element->children = parse_compound(&cursor, end, 0);
		}
		break;

	case DIRECTIVE_SET:
		element->compound = SET;
		element->method = ASN1_CONS;
		cursor++;
		if (cursor >= end)
			goto overrun_error;
		if (cursor->token_type == DIRECTIVE_OF) {
			element->compound = SET_OF;
			cursor++;
			if (cursor >= end)
				goto parse_error;
			element->children = parse_type(&cursor, end, NULL);
		} else {
			element->children = parse_compound(&cursor, end, 1);
		}
		break;

	default:
		fprintf(stderr, "%s:%d: Token '%s' does not introduce a type\n",
			filename, cursor->line, cursor->content);
		exit(1);
	}

	/* Handle elements that are optional */
	if (cursor < end && (cursor->token_type == DIRECTIVE_OPTIONAL ||
			     cursor->token_type == DIRECTIVE_DEFAULT)
	    ) {
		cursor++;
		top->flags |= ELEMENT_SKIPPABLE;
	}

	if (cursor < end && cursor->token_type == TOKEN_OPEN_ACTION) {
		cursor++;
		if (cursor >= end)
			goto overrun_error;
		if (cursor->token_type != TOKEN_ELEMENT_NAME) {
			fprintf(stderr, "%s:%d: Token '%s' is not an action function name\n",
				filename, cursor->line, cursor->content);
			exit(1);
		}

		action = malloc(sizeof(struct action));
		if (!action) {
			perror(NULL);
			exit(1);
		}
		action->index = 0;
		action->name = cursor->content;

		for (ppaction = &action_list;
		     *ppaction;
		     ppaction = &(*ppaction)->next
		     ) {
			int cmp = strcmp(action->name, (*ppaction)->name);
			if (cmp == 0) {
				free(action);
				action = *ppaction;
				goto found;
			}
			if (cmp < 0) {
				action->next = *ppaction;
				*ppaction = action;
				nr_actions++;
				goto found;
			}
		}
		action->next = NULL;
		*ppaction = action;
		nr_actions++;
	found:

		element->action = action;
		cursor->action = action;
		cursor++;
		if (cursor >= end)
			goto overrun_error;
		if (cursor->token_type != TOKEN_CLOSE_ACTION) {
			fprintf(stderr, "%s:%d: Missing close action, got '%s'\n",
				filename, cursor->line, cursor->content);
			exit(1);
		}
		cursor++;
	}

	*_cursor = cursor;
	return top;

parse_error:
	fprintf(stderr, "%s:%d: Unexpected token '%s'\n",
		filename, cursor->line, cursor->content);
	exit(1);

overrun_error:
	fprintf(stderr, "%s: Unexpectedly hit EOF\n", filename);
	exit(1);
}

/*
 * Parse a compound type list
 */
static struct element *parse_compound(struct token **_cursor, struct token *end,
				      int alternates)
{
	struct element *children, **child_p = &children, *element;
	struct token *cursor = *_cursor, *name;

	if (cursor->token_type != TOKEN_OPEN_CURLY) {
		fprintf(stderr, "%s:%d: Expected compound to start with brace not '%s'\n",
			filename, cursor->line, cursor->content);
		exit(1);
	}
	cursor++;
	if (cursor >= end)
		goto overrun_error;

	if (cursor->token_type == TOKEN_OPEN_CURLY) {
		fprintf(stderr, "%s:%d: Empty compound\n",
			filename, cursor->line);
		exit(1);
	}

	for (;;) {
		name = NULL;
		if (cursor->token_type == TOKEN_ELEMENT_NAME) {
			name = cursor;
			cursor++;
			if (cursor >= end)
				goto overrun_error;
		}

		element = parse_type(&cursor, end, name);
		if (alternates)
			element->flags |= ELEMENT_SKIPPABLE | ELEMENT_CONDITIONAL;

		*child_p = element;
		child_p = &element->next;

		if (cursor >= end)
			goto overrun_error;
		if (cursor->token_type != TOKEN_COMMA)
			break;
		cursor++;
		if (cursor >= end)
			goto overrun_error;
	}

	children->flags &= ~ELEMENT_CONDITIONAL;

	if (cursor->token_type != TOKEN_CLOSE_CURLY) {
		fprintf(stderr, "%s:%d: Expected compound closure, got '%s'\n",
			filename, cursor->line, cursor->content);
		exit(1);
	}
	cursor++;

	*_cursor = cursor;
	return children;

overrun_error:
	fprintf(stderr, "%s: Unexpectedly hit EOF\n", filename);
	exit(1);
}

static void dump_element(const struct element *e, int level)
{
	const struct element *c;
	const struct type *t = e->type_def;
	const char *name = e->name ? e->name->content : ".";
	const char *tname = t && t->name ? t->name->content : ".";
	char tag[32];

	if (e->class == 0 && e->method == 0 && e->tag == 0)
		strcpy(tag, "<...>");
	else if (e->class == ASN1_UNIV)
		sprintf(tag, "%s %s %s",
			asn1_classes[e->class],
			asn1_methods[e->method],
			asn1_universal_tags[e->tag]);
	else
		sprintf(tag, "%s %s %u",
			asn1_classes[e->class],
			asn1_methods[e->method],
			e->tag);

	printf("%c%c%c%c%c %c %*s[*] \e[33m%s\e[m %s %s \e[35m%s\e[m\n",
	       e->flags & ELEMENT_IMPLICIT ? 'I' : '-',
	       e->flags & ELEMENT_EXPLICIT ? 'E' : '-',
	       e->flags & ELEMENT_TAG_SPECIFIED ? 'T' : '-',
	       e->flags & ELEMENT_SKIPPABLE ? 'S' : '-',
	       e->flags & ELEMENT_CONDITIONAL ? 'C' : '-',
	       "-tTqQcaro"[e->compound],
	       level, "",
	       tag,
	       tname,
	       name,
	       e->action ? e->action->name : "");
	if (e->compound == TYPE_REF)
		dump_element(e->type->type->element, level + 3);
	else
		for (c = e->children; c; c = c->next)
			dump_element(c, level + 3);
}

static void dump_elements(void)
{
	if (debug_opt)
		dump_element(type_list[0].element, 0);
}

static void render_element(FILE *out, struct element *e, struct element *tag);
static void render_out_of_line_list(FILE *out);

static int nr_entries;
static int render_depth = 1;
static struct element *render_list, **render_list_p = &render_list;

__attribute__((format(printf, 2, 3)))
static void render_opcode(FILE *out, const char *fmt, ...)
{
	va_list va;

	if (out) {
		fprintf(out, "\t[%4d] =%*s", nr_entries, render_depth, "");
		va_start(va, fmt);
		vfprintf(out, fmt, va);
		va_end(va);
	}
	nr_entries++;
}

__attribute__((format(printf, 2, 3)))
static void render_more(FILE *out, const char *fmt, ...)
{
	va_list va;

	if (out) {
		va_start(va, fmt);
		vfprintf(out, fmt, va);
		va_end(va);
	}
}

/*
 * Render the grammar into a state machine definition.
 */
static void render(FILE *out, FILE *hdr)
{
	struct element *e;
	struct action *action;
	struct type *root;
	int index;

	fprintf(hdr, "/*\n");
	fprintf(hdr, " * Automatically generated by asn1_compiler.  Do not edit\n");
	fprintf(hdr, " *\n");
	fprintf(hdr, " * ASN.1 parser for %s\n", grammar_name);
	fprintf(hdr, " */\n");
	fprintf(hdr, "#include <linux/asn1_decoder.h>\n");
	fprintf(hdr, "\n");
	fprintf(hdr, "extern const struct asn1_decoder %s_decoder;\n", grammar_name);
	if (ferror(hdr)) {
		perror(headername);
		exit(1);
	}

	fprintf(out, "/*\n");
	fprintf(out, " * Automatically generated by asn1_compiler.  Do not edit\n");
	fprintf(out, " *\n");
	fprintf(out, " * ASN.1 parser for %s\n", grammar_name);
	fprintf(out, " */\n");
	fprintf(out, "#include <linux/asn1_ber_bytecode.h>\n");
	fprintf(out, "#include \"%s.asn1.h\"\n", grammar_name);
	fprintf(out, "\n");
	if (ferror(out)) {
		perror(outputname);
		exit(1);
	}

	/* Tabulate the action functions we might have to call */
	fprintf(hdr, "\n");
	index = 0;
	for (action = action_list; action; action = action->next) {
		action->index = index++;
		fprintf(hdr,
			"extern int %s(void *, size_t, unsigned char,"
			" const void *, size_t);\n",
			action->name);
	}
	fprintf(hdr, "\n");

	fprintf(out, "enum %s_actions {\n", grammar_name);
	for (action = action_list; action; action = action->next)
		fprintf(out, "\tACT_%s = %u,\n",
			action->name, action->index);
	fprintf(out, "\tNR__%s_actions = %u\n", grammar_name, nr_actions);
	fprintf(out, "};\n");

	fprintf(out, "\n");
	fprintf(out, "static const asn1_action_t %s_action_table[NR__%s_actions] = {\n",
		grammar_name, grammar_name);
	for (action = action_list; action; action = action->next)
		fprintf(out, "\t[%4u] = %s,\n", action->index, action->name);
	fprintf(out, "};\n");

	if (ferror(out)) {
		perror(outputname);
		exit(1);
	}

	/* We do two passes - the first one calculates all the offsets */
	verbose("Pass 1\n");
	nr_entries = 0;
	root = &type_list[0];
	render_element(NULL, root->element, NULL);
	render_opcode(NULL, "ASN1_OP_COMPLETE,\n");
	render_out_of_line_list(NULL);

	for (e = element_list; e; e = e->list_next)
		e->flags &= ~ELEMENT_RENDERED;

	/* And then we actually render */
	verbose("Pass 2\n");
	fprintf(out, "\n");
	fprintf(out, "static const unsigned char %s_machine[] = {\n",
		grammar_name);

	nr_entries = 0;
	root = &type_list[0];
	render_element(out, root->element, NULL);
	render_opcode(out, "ASN1_OP_COMPLETE,\n");
	render_out_of_line_list(out);

	fprintf(out, "};\n");

	fprintf(out, "\n");
	fprintf(out, "const struct asn1_decoder %s_decoder = {\n", grammar_name);
	fprintf(out, "\t.machine = %s_machine,\n", grammar_name);
	fprintf(out, "\t.machlen = sizeof(%s_machine),\n", grammar_name);
	fprintf(out, "\t.actions = %s_action_table,\n", grammar_name);
	fprintf(out, "};\n");
}

/*
 * Render the out-of-line elements
 */
static void render_out_of_line_list(FILE *out)
{
	struct element *e, *ce;
	const char *act;
	int entry;

	while ((e = render_list)) {
		render_list = e->render_next;
		if (!render_list)
			render_list_p = &render_list;

		render_more(out, "\n");
		e->entry_index = entry = nr_entries;
		render_depth++;
		for (ce = e->children; ce; ce = ce->next)
			render_element(out, ce, NULL);
		render_depth--;

		act = e->action ? "_ACT" : "";
		switch (e->compound) {
		case SEQUENCE:
			render_opcode(out, "ASN1_OP_END_SEQ%s,\n", act);
			break;
		case SEQUENCE_OF:
			render_opcode(out, "ASN1_OP_END_SEQ_OF%s,\n", act);
			render_opcode(out, "_jump_target(%u),\n", entry);
			break;
		case SET:
			render_opcode(out, "ASN1_OP_END_SET%s,\n", act);
			break;
		case SET_OF:
			render_opcode(out, "ASN1_OP_END_SET_OF%s,\n", act);
			render_opcode(out, "_jump_target(%u),\n", entry);
			break;
		default:
			break;
		}
		if (e->action)
			render_opcode(out, "_action(ACT_%s),\n",
				      e->action->name);
		render_opcode(out, "ASN1_OP_RETURN,\n");
	}
}

/*
 * Render an element.
 */
static void render_element(FILE *out, struct element *e, struct element *tag)
{
	struct element *ec, *x;
	const char *cond, *act;
	int entry, skippable = 0, outofline = 0;

	if (e->flags & ELEMENT_SKIPPABLE ||
	    (tag && tag->flags & ELEMENT_SKIPPABLE))
		skippable = 1;

	if ((e->type_def && e->type_def->ref_count > 1) ||
	    skippable)
		outofline = 1;

	if (e->type_def && out) {
		render_more(out, "\t// %s\n", e->type_def->name->content);
	}

	/* Render the operation */
	cond = (e->flags & ELEMENT_CONDITIONAL ||
		(tag && tag->flags & ELEMENT_CONDITIONAL)) ? "COND_" : "";
	act = e->action ? "_ACT" : "";
	switch (e->compound) {
	case ANY:
		render_opcode(out, "ASN1_OP_%sMATCH_ANY%s%s,",
			      cond, act, skippable ? "_OR_SKIP" : "");
		if (e->name)
			render_more(out, "\t\t// %s", e->name->content);
		render_more(out, "\n");
		goto dont_render_tag;

	case TAG_OVERRIDE:
		render_element(out, e->children, e);
		return;

	case SEQUENCE:
	case SEQUENCE_OF:
	case SET:
	case SET_OF:
		render_opcode(out, "ASN1_OP_%sMATCH%s%s,",
			      cond,
			      outofline ? "_JUMP" : "",
			      skippable ? "_OR_SKIP" : "");
		break;

	case CHOICE:
		goto dont_render_tag;

	case TYPE_REF:
		if (e->class == ASN1_UNIV && e->method == ASN1_PRIM && e->tag == 0)
			goto dont_render_tag;
	default:
		render_opcode(out, "ASN1_OP_%sMATCH%s%s,",
			      cond, act,
			      skippable ? "_OR_SKIP" : "");
		break;
	}

	x = tag ?: e;
	if (x->name)
		render_more(out, "\t\t// %s", x->name->content);
	render_more(out, "\n");

	/* Render the tag */
	if (!tag || !(tag->flags & ELEMENT_TAG_SPECIFIED))
		tag = e;

	if (tag->class == ASN1_UNIV &&
	    tag->tag != 14 &&
	    tag->tag != 15 &&
	    tag->tag != 31)
		render_opcode(out, "_tag(%s, %s, %s),\n",
			      asn1_classes[tag->class],
			      asn1_methods[tag->method | e->method],
			      asn1_universal_tags[tag->tag]);
	else
		render_opcode(out, "_tagn(%s, %s, %2u),\n",
			      asn1_classes[tag->class],
			      asn1_methods[tag->method | e->method],
			      tag->tag);
	tag = NULL;
dont_render_tag:

	/* Deal with compound types */
	switch (e->compound) {
	case TYPE_REF:
		render_element(out, e->type->type->element, tag);
		if (e->action)
			render_opcode(out, "ASN1_OP_%sACT,\n",
				      skippable ? "MAYBE_" : "");
		break;

	case SEQUENCE:
		if (outofline) {
			/* Render out-of-line for multiple use or
			 * skipability */
			render_opcode(out, "_jump_target(%u),", e->entry_index);
			if (e->type_def && e->type_def->name)
				render_more(out, "\t\t// --> %s",
					    e->type_def->name->content);
			render_more(out, "\n");
			if (!(e->flags & ELEMENT_RENDERED)) {
				e->flags |= ELEMENT_RENDERED;
				*render_list_p = e;
				render_list_p = &e->render_next;
			}
			return;
		} else {
			/* Render inline for single use */
			render_depth++;
			for (ec = e->children; ec; ec = ec->next)
				render_element(out, ec, NULL);
			render_depth--;
			render_opcode(out, "ASN1_OP_END_SEQ%s,\n", act);
		}
		break;

	case SEQUENCE_OF:
	case SET_OF:
		if (outofline) {
			/* Render out-of-line for multiple use or
			 * skipability */
			render_opcode(out, "_jump_target(%u),", e->entry_index);
			if (e->type_def && e->type_def->name)
				render_more(out, "\t\t// --> %s",
					    e->type_def->name->content);
			render_more(out, "\n");
			if (!(e->flags & ELEMENT_RENDERED)) {
				e->flags |= ELEMENT_RENDERED;
				*render_list_p = e;
				render_list_p = &e->render_next;
			}
			return;
		} else {
			/* Render inline for single use */
			entry = nr_entries;
			render_depth++;
			render_element(out, e->children, NULL);
			render_depth--;
			if (e->compound == SEQUENCE_OF)
				render_opcode(out, "ASN1_OP_END_SEQ_OF%s,\n", act);
			else
				render_opcode(out, "ASN1_OP_END_SET_OF%s,\n", act);
			render_opcode(out, "_jump_target(%u),\n", entry);
		}
		break;

	case SET:
		/* I can't think of a nice way to do SET support without having
		 * a stack of bitmasks to make sure no element is repeated.
		 * The bitmask has also to be checked that no non-optional
		 * elements are left out whilst not preventing optional
		 * elements from being left out.
		 */
		fprintf(stderr, "The ASN.1 SET type is not currently supported.\n");
		exit(1);

	case CHOICE:
		for (ec = e->children; ec; ec = ec->next)
			render_element(out, ec, ec);
		if (!skippable)
			render_opcode(out, "ASN1_OP_COND_FAIL,\n");
		if (e->action)
			render_opcode(out, "ASN1_OP_ACT,\n");
		break;

	default:
		break;
	}

	if (e->action)
		render_opcode(out, "_action(ACT_%s),\n", e->action->name);
}
