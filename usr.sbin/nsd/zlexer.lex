%{
/*
 * zlexer.lex - lexical analyzer for (DNS) zone files
 * 
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved
 *
 * See LICENSE for the license.
 *
 */
/* because flex keeps having sign-unsigned compare problems that are unfixed*/
#if defined(__clang__)||(defined(__GNUC__)&&((__GNUC__ >4)||(defined(__GNUC_MINOR__)&&(__GNUC__ ==4)&&(__GNUC_MINOR__ >=2))))
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
/* ignore fallthrough warnings in the generated parse code case statements */
#if defined(__clang__)||(defined(__GNUC__)&&(__GNUC__ >=7))
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <strings.h>

#include "zonec.h"
#include "dname.h"
#include "zparser.h"

#if 0
#define LEXOUT(s)  printf s /* used ONLY when debugging */
#else
#define LEXOUT(s)
#endif

enum lexer_state {
	EXPECT_OWNER,
	PARSING_OWNER,
	PARSING_TTL_CLASS_TYPE,
	PARSING_RDATA
};

static int parse_token(int token, char *yytext, enum lexer_state *lexer_state);

static YY_BUFFER_STATE include_stack[MAXINCLUDES];
static zparser_type zparser_stack[MAXINCLUDES];
static int include_stack_ptr = 0;

/*
 * Saves the file specific variables on the include stack.
 */
static void
push_parser_state(FILE *input)
{
	zparser_stack[include_stack_ptr].filename = parser->filename;
	zparser_stack[include_stack_ptr].line = parser->line;
	zparser_stack[include_stack_ptr].origin = parser->origin;
	include_stack[include_stack_ptr] = YY_CURRENT_BUFFER;
	yy_switch_to_buffer(yy_create_buffer(input, YY_BUF_SIZE));
	++include_stack_ptr;
}

/*
 * Restores the file specific variables from the include stack.
 */
static void
pop_parser_state(void)
{
	if (parser->filename)
		region_recycle(parser->region, (void *)parser->filename,
			strlen(parser->filename)+1);

	--include_stack_ptr;
	parser->filename = zparser_stack[include_stack_ptr].filename;
	parser->line = zparser_stack[include_stack_ptr].line;
	parser->origin = zparser_stack[include_stack_ptr].origin;
	yy_delete_buffer(YY_CURRENT_BUFFER);
	yy_switch_to_buffer(include_stack[include_stack_ptr]);
}

static YY_BUFFER_STATE oldstate;
/* Start string scan */
void
parser_push_stringbuf(char* str)
{
	oldstate = YY_CURRENT_BUFFER;
	yy_switch_to_buffer(yy_scan_string(str));
}

void
parser_pop_stringbuf(void)
{
	yy_delete_buffer(YY_CURRENT_BUFFER);
	yy_switch_to_buffer(oldstate);
	oldstate = NULL;
}

	static int paren_open = 0;
	static enum lexer_state lexer_state = EXPECT_OWNER;
void
parser_flush(void)
{
	YY_FLUSH_BUFFER;
	paren_open = 0;
	lexer_state = EXPECT_OWNER;
}

int at_eof(void)
{
	static int once = 1;
	return (once = !once) ? 0 : NL;
}

#ifndef yy_set_bol /* compat definition, for flex 2.4.6 */
#define yy_set_bol(at_bol) \
	{ \
		if ( ! yy_current_buffer ) \
			yy_current_buffer = yy_create_buffer( yyin, YY_BUF_SIZE ); \
		yy_current_buffer->yy_ch_buf[0] = ((at_bol)?'\n':' '); \
	}
#endif
	
%}
%option noinput
%option nounput
%{
#ifndef YY_NO_UNPUT
#define YY_NO_UNPUT 1
#endif
#ifndef YY_NO_INPUT
#define YY_NO_INPUT 1
#endif
%}

SPACE   [ \t]
LETTER  [a-zA-Z]
NEWLINE [\n\r]
ZONESTR [^ \t\n\r();.\"\$]|\\.|\\\n
CHARSTR [^ \t\n\r();.\"]|\\.|\\\n
QUOTE   \"
DOLLAR  \$
COMMENT ;
DOT     \.
BIT	[^\]\n]|\\.
ANY     [^\"\n\\]|\\.

%x	incl bitlabel quotedstring

%%
{SPACE}*{COMMENT}.*	/* ignore */
^{DOLLAR}TTL            { lexer_state = PARSING_RDATA; return DOLLAR_TTL; }
^{DOLLAR}ORIGIN         { lexer_state = PARSING_RDATA; return DOLLAR_ORIGIN; }

	/*
	 * Handle $INCLUDE directives.  See
	 * http://dinosaur.compilertools.net/flex/flex_12.html#SEC12.
	 */
^{DOLLAR}INCLUDE        {
	BEGIN(incl);
	/* ignore case statement fallthrough on incl<EOF> flex rule */
}
<incl>\n		|
<incl><<EOF>>		{
	int error_occurred = parser->error_occurred;
	BEGIN(INITIAL);
	zc_error("missing file name in $INCLUDE directive");
	yy_set_bol(1); /* Set beginning of line, so "^" rules match.  */
	++parser->line;
	parser->error_occurred = error_occurred;
}
<incl>.+ 		{ 	
	char *tmp;
	domain_type *origin = parser->origin;
	int error_occurred = parser->error_occurred;
	
	BEGIN(INITIAL);
	if (include_stack_ptr >= MAXINCLUDES ) {
		zc_error("includes nested too deeply, skipped (>%d)",
			 MAXINCLUDES);
	} else {
		FILE *input;

		/* Remove trailing comment.  */
		tmp = strrchr(yytext, ';');
		if (tmp) {
			*tmp = '\0';
		}
		strip_string(yytext);
		
		/* Parse origin for include file.  */
		tmp = strrchr(yytext, ' ');
		if (!tmp) {
			tmp = strrchr(yytext, '\t');
		}
		if (tmp) {
			const dname_type *dname;
			
			/* split the original yytext */
			*tmp = '\0';
			strip_string(yytext);

			dname = dname_parse(parser->region, tmp + 1);
			if (!dname) {
				zc_error("incorrect include origin '%s'",
					 tmp + 1);
			} else if (*(tmp + strlen(tmp + 1)) != '.') {
				zc_error("$INCLUDE directive requires absolute domain name");
			} else {
				origin = domain_table_insert(
					parser->db->domains, dname);
			}
		}
		
		if (strlen(yytext) == 0) {
			zc_error("missing file name in $INCLUDE directive");
		} else if (!(input = fopen(yytext, "r"))) {
			zc_error("cannot open include file '%s': %s",
				 yytext, strerror(errno));
		} else {
			/* Initialize parser for include file.  */
			char *filename = region_strdup(parser->region, yytext);
			push_parser_state(input); /* Destroys yytext.  */
			parser->filename = filename;
			parser->line = 1;
			parser->origin = origin;
			lexer_state = EXPECT_OWNER;
		}
	}

	parser->error_occurred = error_occurred;
}
<INITIAL><<EOF>>	{
	int eo = at_eof();
	yy_set_bol(1); /* Set beginning of line, so "^" rules match.  */
	if (include_stack_ptr == 0) {
		if(eo == NL)
			return eo;
		yyterminate();
	} else {
		fclose(yyin);
		pop_parser_state();
		if(eo == NL)
			return eo;
	}
}
^{DOLLAR}{LETTER}+	{ zc_warning("Unknown directive: %s", yytext); }
{DOT}	{
	LEXOUT((". "));
	return parse_token('.', yytext, &lexer_state);
}
@	{
	LEXOUT(("@ "));
	return parse_token('@', yytext, &lexer_state);
}
\\#	{
	LEXOUT(("\\# "));
	return parse_token(URR, yytext, &lexer_state);
}
{NEWLINE}	{
	++parser->line;
	if (!paren_open) { 
		lexer_state = EXPECT_OWNER;
		LEXOUT(("NL\n"));
		return NL;
	} else {
		LEXOUT(("SP "));
		return SP;
	}
}
\(	{
	if (paren_open) {
		zc_error("nested parentheses");
		yyterminate();
	}
	LEXOUT(("( "));
	paren_open = 1;
	return SP;
}
\)	{
	if (!paren_open) {
		zc_error("closing parentheses without opening parentheses");
		yyterminate();
	}
	LEXOUT((") "));
	paren_open = 0;
	return SP;
}
{SPACE}+	{
	if (!paren_open && lexer_state == EXPECT_OWNER) {
		lexer_state = PARSING_TTL_CLASS_TYPE;
		LEXOUT(("PREV "));
		return PREV;
	}
	if (lexer_state == PARSING_OWNER) {
		lexer_state = PARSING_TTL_CLASS_TYPE;
	}
	LEXOUT(("SP "));
	return SP;
}

	/* Bitlabels.  Strip leading and ending brackets.  */
\\\[			{ BEGIN(bitlabel); }
<bitlabel><<EOF>>	{
	zc_error("EOF inside bitlabel");
	BEGIN(INITIAL);
	yyrestart(yyin); /* this is so that lex does not give an internal err */
	yyterminate();
}
<bitlabel>{BIT}*	{ yymore(); }
<bitlabel>\n		{ ++parser->line; yymore(); }
<bitlabel>\]		{
	BEGIN(INITIAL);
	yytext[yyleng - 1] = '\0';
	return parse_token(BITLAB, yytext, &lexer_state);
}

	/* Quoted strings.  Strip leading and ending quotes.  */
{QUOTE}			{ BEGIN(quotedstring); LEXOUT(("\" ")); }
<quotedstring><<EOF>> 	{
	zc_error("EOF inside quoted string");
	BEGIN(INITIAL);
	yyrestart(yyin); /* this is so that lex does not give an internal err */
	yyterminate();
}
<quotedstring>{ANY}*	{ LEXOUT(("QSTR ")); yymore(); }
<quotedstring>\n 	{ ++parser->line; yymore(); }
<quotedstring>{QUOTE} {
	LEXOUT(("\" "));
	BEGIN(INITIAL);
	yytext[yyleng - 1] = '\0';
	return parse_token(QSTR, yytext, &lexer_state);
}

{ZONESTR}({CHARSTR})* {
	/* Any allowed word.  */
	return parse_token(STR, yytext, &lexer_state);
}
. {
	zc_error("unknown character '%c' (\\%03d) seen - is this a zonefile?",
		 (int) yytext[0], (int) yytext[0]);
}
%%

/*
 * Analyze "word" to see if it matches an RR type, possibly by using
 * the "TYPExxx" notation.  If it matches, the corresponding token is
 * returned and the TYPE parameter is set to the RR type value.
 */
static int
rrtype_to_token(const char *word, uint16_t *type) 
{
	uint16_t t = rrtype_from_string(word);
	if (t != 0) {
		rrtype_descriptor_type *entry = rrtype_descriptor_by_type(t);
		*type = t;
		return entry->token;
	}

	return 0;
}


/*
 * Remove \DDD constructs from the input. See RFC 1035, section 5.1.
 */
static size_t
zoctet(char *text) 
{
	/*
	 * s follows the string, p lags behind and rebuilds the new
	 * string
	 */
	char *s;
	char *p;
	
	for (s = p = text; *s; ++s, ++p) {
		assert(p <= s);
		if (s[0] != '\\') {
			/* Ordinary character.  */
			*p = *s;
		} else if (isdigit((unsigned char)s[1]) && isdigit((unsigned char)s[2]) && isdigit((unsigned char)s[3])) {
			/* \DDD escape.  */
			int val = (hexdigit_to_int(s[1]) * 100 +
				   hexdigit_to_int(s[2]) * 10 +
				   hexdigit_to_int(s[3]));
			if (0 <= val && val <= 255) {
				s += 3;
				*p = val;
			} else {
				zc_warning("text escape \\DDD overflow");
				*p = *++s;
			}
		} else if (s[1] != '\0') {
			/* \X where X is any character, keep X.  */
			*p = *++s;
		} else {
			/* Trailing backslash, ignore it.  */
			zc_warning("trailing backslash ignored");
			--p;
		}
	}
	*p = '\0';
	return p - text;
}

static int
parse_token(int token, char *yytext, enum lexer_state *lexer_state)
{
	size_t len;
	char *str;

	if (*lexer_state == EXPECT_OWNER) {
		*lexer_state = PARSING_OWNER;
	} else if (*lexer_state == PARSING_TTL_CLASS_TYPE) {
		const char *t;
		int token;
		uint16_t rrclass;
		
		/* type */
		token = rrtype_to_token(yytext, &yylval.type);
		if (token != 0) {
			*lexer_state = PARSING_RDATA;
			LEXOUT(("%d[%s] ", token, yytext));
			return token;
		}

		/* class */
		rrclass = rrclass_from_string(yytext);
		if (rrclass != 0) {
			yylval.klass = rrclass;
			LEXOUT(("CLASS "));
			return T_RRCLASS;
		}

		/* ttl */
		yylval.ttl = strtottl(yytext, &t);
		if (*t == '\0') {
			LEXOUT(("TTL "));
			return T_TTL;
		}
	}

	str = region_strdup(parser->rr_region, yytext);
	len = zoctet(str);

	yylval.data.str = str;
	yylval.data.len = len;
	
	LEXOUT(("%d[%s] ", token, yytext));
	return token;
}
