/* $FreeBSD$ */
#ifndef lint
static const char yysccsid[] = "@(#)yaccpar	1.9 (Berkeley) 02/21/93";
#endif

#define YYBYACC 1
#define YYMAJOR 1
#define YYMINOR 9

#define YYEMPTY        (-1)
#define yyclearin      (yychar = YYEMPTY)
#define yyerrok        (yyerrflag = 0)
#define YYRECOVERING() (yyerrflag != 0)

#define YYPREFIX "yy"

#define YYPURE 0

#line 35 "parse.y"
/* SPDX-License-Identifier: BSD-2-Clause */

/*  Copyright (c) 1990 The Regents of the University of California. */
/*  All rights reserved. */

/*  This code is derived from software contributed to Berkeley by */
/*  Vern Paxson. */

/*  The United States Government has rights in this work pursuant */
/*  to contract no. DE-AC03-76SF00098 between the United States */
/*  Department of Energy and the University of California. */

/*  This file is part of flex. */

/*  Redistribution and use in source and binary forms, with or without */
/*  modification, are permitted provided that the following conditions */
/*  are met: */

/*  1. Redistributions of source code must retain the above copyright */
/*     notice, this list of conditions and the following disclaimer. */
/*  2. Redistributions in binary form must reproduce the above copyright */
/*     notice, this list of conditions and the following disclaimer in the */
/*     documentation and/or other materials provided with the distribution. */

/*  Neither the name of the University nor the names of its contributors */
/*  may be used to endorse or promote products derived from this software */
/*  without specific prior written permission. */

/*  THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR */
/*  IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED */
/*  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR */
/*  PURPOSE. */

#include "flexdef.h"
#include "tables.h"

int pat, scnum, eps, headcnt, trailcnt, lastchar, i, rulelen;
int trlcontxt, xcluflg, currccl, cclsorted, varlength, variable_trail_rule;

int *scon_stk;
int scon_stk_ptr;

static int madeany = false;  /* whether we've made the '.' character class */
static int ccldot, cclany;
int previous_continued_action;	/* whether the previous rule's action was '|' */

#define format_warn3(fmt, a1, a2) \
	do{ \
        char fw3_msg[MAXLINE];\
        snprintf( fw3_msg, MAXLINE,(fmt), (a1), (a2) );\
        warn( fw3_msg );\
	}while(0)

/* Expand a POSIX character class expression. */
#define CCL_EXPR(func) \
	do{ \
	int c; \
	for ( c = 0; c < csize; ++c ) \
		if ( isascii(c) && func(c) ) \
			ccladd( currccl, c ); \
	}while(0)

/* negated class */
#define CCL_NEG_EXPR(func) \
	do{ \
	int c; \
	for ( c = 0; c < csize; ++c ) \
		if ( !func(c) ) \
			ccladd( currccl, c ); \
	}while(0)

/* While POSIX defines isblank(), it's not ANSI C. */
#define IS_BLANK(c) ((c) == ' ' || (c) == '\t')

/* On some over-ambitious machines, such as DEC Alpha's, the default
 * token type is "long" instead of "int"; this leads to problems with
 * declaring yylval in flexdef.h.  But so far, all the yacc's I've seen
 * wrap their definitions of YYSTYPE with "#ifndef YYSTYPE"'s, so the
 * following should ensure that the default token type is "int".
 */
#define YYSTYPE int

#line 99 "parse.c"

#ifndef YYSTYPE
typedef int YYSTYPE;
#endif

/* compatibility with bison */
#ifdef YYPARSE_PARAM
/* compatibility with FreeBSD */
# ifdef YYPARSE_PARAM_TYPE
#  define YYPARSE_DECL() yyparse(YYPARSE_PARAM_TYPE YYPARSE_PARAM)
# else
#  define YYPARSE_DECL() yyparse(void *YYPARSE_PARAM)
# endif
#else
# define YYPARSE_DECL() yyparse(void)
#endif

/* Parameters sent to lex. */
#ifdef YYLEX_PARAM
# define YYLEX_DECL() yylex(void *YYLEX_PARAM)
# define YYLEX yylex(YYLEX_PARAM)
#else
# define YYLEX_DECL() yylex(void)
# define YYLEX yylex()
#endif

/* Parameters sent to yyerror. */
#ifndef YYERROR_DECL
#define YYERROR_DECL() yyerror(const char *s)
#endif
#ifndef YYERROR_CALL
#define YYERROR_CALL(msg) yyerror(msg)
#endif

extern int YYPARSE_DECL();

#define CHAR 257
#define NUMBER 258
#define SECTEND 259
#define SCDECL 260
#define XSCDECL 261
#define NAME 262
#define PREVCCL 263
#define EOF_OP 264
#define OPTION_OP 265
#define OPT_OUTFILE 266
#define OPT_PREFIX 267
#define OPT_YYCLASS 268
#define OPT_HEADER 269
#define OPT_EXTRA_TYPE 270
#define OPT_TABLES 271
#define CCE_ALNUM 272
#define CCE_ALPHA 273
#define CCE_BLANK 274
#define CCE_CNTRL 275
#define CCE_DIGIT 276
#define CCE_GRAPH 277
#define CCE_LOWER 278
#define CCE_PRINT 279
#define CCE_PUNCT 280
#define CCE_SPACE 281
#define CCE_UPPER 282
#define CCE_XDIGIT 283
#define CCE_NEG_ALNUM 284
#define CCE_NEG_ALPHA 285
#define CCE_NEG_BLANK 286
#define CCE_NEG_CNTRL 287
#define CCE_NEG_DIGIT 288
#define CCE_NEG_GRAPH 289
#define CCE_NEG_LOWER 290
#define CCE_NEG_PRINT 291
#define CCE_NEG_PUNCT 292
#define CCE_NEG_SPACE 293
#define CCE_NEG_UPPER 294
#define CCE_NEG_XDIGIT 295
#define CCL_OP_DIFF 296
#define CCL_OP_UNION 297
#define BEGIN_REPEAT_POSIX 298
#define END_REPEAT_POSIX 299
#define BEGIN_REPEAT_FLEX 300
#define END_REPEAT_FLEX 301
#define YYERRCODE 256
static const short yylhs[] = {                           -1,
    0,    1,    2,    2,    2,    2,    3,    6,    6,    7,
    7,    7,    8,    9,    9,   10,   10,   10,   10,   10,
   10,    4,    4,    4,    5,   12,   12,   12,   12,   14,
   11,   11,   11,   15,   15,   15,   16,   13,   13,   13,
   13,   18,   18,   17,   19,   19,   19,   19,   19,   20,
   20,   20,   20,   20,   20,   20,   20,   20,   20,   20,
   20,   21,   21,   21,   23,   23,   24,   24,   24,   24,
   25,   25,   25,   25,   25,   25,   25,   25,   25,   25,
   25,   25,   25,   25,   25,   25,   25,   25,   25,   25,
   25,   25,   25,   25,   22,   22,
};
static const short yylen[] = {                            2,
    5,    0,    3,    2,    0,    1,    1,    1,    1,    2,
    1,    1,    2,    2,    0,    3,    3,    3,    3,    3,
    3,    5,    5,    0,    0,    2,    1,    1,    1,    0,
    4,    3,    0,    3,    1,    1,    1,    2,    3,    2,
    1,    3,    1,    2,    2,    1,    6,    5,    4,    2,
    2,    2,    6,    5,    4,    1,    1,    1,    3,    3,
    1,    3,    3,    1,    3,    4,    4,    2,    2,    0,
    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,
    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,
    1,    1,    1,    1,    2,    0,
};
static const short yydefred[] = {                         2,
    0,    0,    6,    0,    7,    8,    9,   15,   24,    0,
    4,    0,    0,   12,   11,    0,    0,    0,    0,    0,
    0,    0,   14,    0,    1,    0,   10,    0,    0,    0,
    0,    0,    0,    0,    0,   24,    0,   16,   18,   19,
   20,   17,   21,   32,   36,   37,    0,   35,    0,   29,
   61,   58,   28,    0,   56,   96,    0,    0,    0,   27,
    0,    0,    0,    0,    0,   64,   31,    0,   23,   26,
    0,    0,   70,    0,   22,    0,   40,    0,   44,    0,
    0,    0,   50,   51,   52,    0,    0,   34,   95,   59,
   60,    0,    0,   71,   72,   73,   74,   75,   76,   77,
   78,   79,   80,   82,   81,   83,   84,   85,   86,   87,
   88,   93,   89,   90,   91,   94,   92,   65,   69,   39,
    0,    0,    0,   62,   63,   66,    0,   49,    0,   55,
    0,   67,    0,   48,    0,   54,   47,   53,
};
static const short yydgoto[] = {                          1,
    2,    4,    9,   13,   25,   10,   16,   11,   12,   23,
   26,   59,   60,   35,   47,   48,   61,   62,   63,   64,
   65,   71,   66,   74,  119,
};
static const short yysindex[] = {                         0,
    0, -222,    0, -155,    0,    0,    0,    0,    0, -215,
    0, -123,    6,    0,    0, -193,   10,   21,   26,   31,
   35,   37,    0,   59,    0,  -44,    0, -147, -145, -140,
 -133, -132, -129,   75, -214,    0,  -19,    0,    0,    0,
    0,    0,    0,    0,    0,    0,   23,    0,  -48,    0,
    0,    0,    0,  -17,    0,    0,  -17,   27,  128,    0,
  -17,   -1,  -30,  -41, -189,    0,    0, -121,    0,    0,
  -31,  -34,    0,  -87,    0,  -25,    0,  -17,    0, -109,
  -41, -108,    0,    0,    0,   60,   60,    0,    0,    0,
    0,   46,  107,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
  -30,  -36,  -39,    0,    0,    0, -104,    0, -219,    0,
 -238,    0, -144,    0, -143,    0,    0,    0,
};
static const short yyrindex[] = {                         0,
    0, -141,    0,    0,    0,    0,    0,    0,    0,    0,
    0, -134,    9,    0,    0, -125,    0,    0,    0,    0,
    0,    0,    0, -178,    0,   22,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,  -21,    0,
    0,    0,    0,    0,    0,    0,    0,   85,    0,    0,
    0,  144,   47,    4,  -10,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,  146,    0,    0,    0,    0,
   18,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,  124,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
   50,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
};
static const short yygindex[] = {                         0,
    0,    0,    0,  121,  133,    0,    0,    0,    0,    0,
    0,    0,  106,    0,    0,   93,    0,   32,   84,  -45,
    0,    0,   25,   90,    0,
};
#define YYTABLESIZE 419
static const short yytable[] = {                         57,
   83,   84,   90,   56,  131,  118,   91,  129,   25,   57,
  120,   24,   33,   46,   56,   55,   56,   81,   33,  135,
   57,   85,   57,   57,   33,   57,   55,   45,   55,   57,
   57,   57,   57,    3,   77,   57,   57,   46,  133,   46,
   14,   45,   33,   46,   46,   79,   15,   46,   33,   46,
   46,   45,   57,   45,   33,   25,   43,   45,   45,   42,
   58,   25,  136,   45,   45,   24,   68,   25,   27,   33,
   28,   58,   33,   58,   54,   81,   69,   30,   36,  134,
   57,   29,   43,   30,   67,   42,   30,   43,   72,   78,
   42,   31,   76,   43,   46,   32,   42,   33,   78,   33,
   34,   33,   33,    5,    6,    7,   86,   87,   45,    8,
  124,  125,   25,   57,   38,   25,   39,    5,    5,    5,
   73,   40,   78,    5,   13,   13,   13,   46,   41,   42,
   13,   33,   43,    3,    3,    3,   44,   75,  126,    3,
   46,   45,   17,   18,   19,   20,   21,   22,  122,  123,
   58,  127,  132,   41,  137,   38,   49,  138,   37,   70,
   88,  121,   92,    0,    0,    0,    0,    0,    0,   93,
   43,    0,    0,   42,    0,    0,    0,   70,    0,    0,
    0,    0,    0,    0,   94,   95,   96,   97,   98,   99,
  100,  101,  102,  103,  104,  105,  106,  107,  108,  109,
  110,  111,  112,  113,  114,  115,  116,  117,    0,    0,
    0,    0,    0,    0,    0,    0,   68,    0,    0,    0,
    0,    0,    0,    0,    0,   89,   51,    0,    0,    0,
    0,    0,   52,    0,   33,   33,   50,   51,    0,   51,
    0,   33,   33,   52,   53,   52,   57,    0,    0,    0,
    0,    0,   57,    0,    0,    0,    0,    0,   82,    0,
   46,  130,  128,    0,   33,   33,   46,   80,    0,    0,
    0,   33,   33,    0,   45,    0,    0,   25,   25,    0,
   45,    0,    0,    0,   25,   25,    0,   57,    0,   57,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,   46,   93,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,   45,    0,   94,   95,   96,
   97,   98,   99,  100,  101,  102,  103,  104,  105,  106,
  107,  108,  109,  110,  111,  112,  113,  114,  115,  116,
  117,   70,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,   70,   70,   70,   70,
   70,   70,   70,   70,   70,   70,   70,   70,   70,   70,
   70,   70,   70,   70,   70,   70,   70,   70,   70,   70,
   68,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,   68,   68,   68,   68,   68,
   68,   68,   68,   68,   68,   68,   68,   68,   68,   68,
   68,   68,   68,   68,   68,   68,   68,   68,   68,
};
static const short yycheck[] = {                         10,
   42,   43,   34,   34,   44,   93,   41,   44,    0,   40,
   36,   60,   34,   10,   34,   46,   34,   63,   40,  258,
   40,   63,   40,   34,   46,   36,   46,   10,   46,   40,
   41,   42,   43,  256,   36,   46,   47,   34,  258,   36,
  256,  256,   34,   40,   41,   47,  262,  262,   40,   46,
   47,   34,   63,   36,   46,   34,   10,   40,   41,   10,
   91,   40,  301,   46,   47,   60,   44,   46,  262,   91,
   61,   91,   94,   91,   94,  121,  125,  256,  123,  299,
   91,   61,   36,  262,   62,   36,   61,   41,   57,  124,
   41,   61,   61,   47,   91,   61,   47,   61,  124,   91,
   42,  123,   94,  259,  260,  261,  296,  297,   91,  265,
   86,   87,   91,  124,  262,   94,  262,  259,  260,  261,
   94,  262,  124,  265,  259,  260,  261,  124,  262,  262,
  265,  123,  262,  259,  260,  261,   62,   10,   93,  265,
  262,  124,  266,  267,  268,  269,  270,  271,  258,  258,
   91,   45,  257,   10,  299,   10,   36,  301,   26,   54,
   68,   78,   73,   -1,   -1,   -1,   -1,   -1,   -1,  257,
  124,   -1,   -1,  124,   -1,   -1,   -1,   93,   -1,   -1,
   -1,   -1,   -1,   -1,  272,  273,  274,  275,  276,  277,
  278,  279,  280,  281,  282,  283,  284,  285,  286,  287,
  288,  289,  290,  291,  292,  293,  294,  295,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   93,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,  257,  257,   -1,   -1,   -1,
   -1,   -1,  263,   -1,  256,  257,  256,  257,   -1,  257,
   -1,  263,  264,  263,  264,  263,  257,   -1,   -1,   -1,
   -1,   -1,  263,   -1,   -1,   -1,   -1,   -1,  300,   -1,
  257,  301,  299,   -1,  256,  257,  263,  298,   -1,   -1,
   -1,  263,  264,   -1,  257,   -1,   -1,  256,  257,   -1,
  263,   -1,   -1,   -1,  263,  264,   -1,  298,   -1,  300,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,  298,  257,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,  298,   -1,  272,  273,  274,
  275,  276,  277,  278,  279,  280,  281,  282,  283,  284,
  285,  286,  287,  288,  289,  290,  291,  292,  293,  294,
  295,  257,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,  272,  273,  274,  275,
  276,  277,  278,  279,  280,  281,  282,  283,  284,  285,
  286,  287,  288,  289,  290,  291,  292,  293,  294,  295,
  257,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,  272,  273,  274,  275,  276,
  277,  278,  279,  280,  281,  282,  283,  284,  285,  286,
  287,  288,  289,  290,  291,  292,  293,  294,  295,
};
#define YYFINAL 1
#ifndef YYDEBUG
#define YYDEBUG 0
#endif
#define YYMAXTOKEN 301
#if YYDEBUG
static const char *yyname[] = {

"end-of-file",0,0,0,0,0,0,0,0,0,"'\\n'",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,"'\"'",0,"'$'",0,0,0,"'('","')'","'*'","'+'","','","'-'","'.'","'/'",0,0,
0,0,0,0,0,0,0,0,0,0,"'<'","'='","'>'","'?'",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,"'['",0,"']'","'^'",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,"'{'","'|'","'}'",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,"CHAR","NUMBER","SECTEND",
"SCDECL","XSCDECL","NAME","PREVCCL","EOF_OP","OPTION_OP","OPT_OUTFILE",
"OPT_PREFIX","OPT_YYCLASS","OPT_HEADER","OPT_EXTRA_TYPE","OPT_TABLES",
"CCE_ALNUM","CCE_ALPHA","CCE_BLANK","CCE_CNTRL","CCE_DIGIT","CCE_GRAPH",
"CCE_LOWER","CCE_PRINT","CCE_PUNCT","CCE_SPACE","CCE_UPPER","CCE_XDIGIT",
"CCE_NEG_ALNUM","CCE_NEG_ALPHA","CCE_NEG_BLANK","CCE_NEG_CNTRL","CCE_NEG_DIGIT",
"CCE_NEG_GRAPH","CCE_NEG_LOWER","CCE_NEG_PRINT","CCE_NEG_PUNCT","CCE_NEG_SPACE",
"CCE_NEG_UPPER","CCE_NEG_XDIGIT","CCL_OP_DIFF","CCL_OP_UNION",
"BEGIN_REPEAT_POSIX","END_REPEAT_POSIX","BEGIN_REPEAT_FLEX","END_REPEAT_FLEX",
};
static const char *yyrule[] = {
"$accept : goal",
"goal : initlex sect1 sect1end sect2 initforrule",
"initlex :",
"sect1 : sect1 startconddecl namelist1",
"sect1 : sect1 options",
"sect1 :",
"sect1 : error",
"sect1end : SECTEND",
"startconddecl : SCDECL",
"startconddecl : XSCDECL",
"namelist1 : namelist1 NAME",
"namelist1 : NAME",
"namelist1 : error",
"options : OPTION_OP optionlist",
"optionlist : optionlist option",
"optionlist :",
"option : OPT_OUTFILE '=' NAME",
"option : OPT_EXTRA_TYPE '=' NAME",
"option : OPT_PREFIX '=' NAME",
"option : OPT_YYCLASS '=' NAME",
"option : OPT_HEADER '=' NAME",
"option : OPT_TABLES '=' NAME",
"sect2 : sect2 scon initforrule flexrule '\\n'",
"sect2 : sect2 scon '{' sect2 '}'",
"sect2 :",
"initforrule :",
"flexrule : '^' rule",
"flexrule : rule",
"flexrule : EOF_OP",
"flexrule : error",
"scon_stk_ptr :",
"scon : '<' scon_stk_ptr namelist2 '>'",
"scon : '<' '*' '>'",
"scon :",
"namelist2 : namelist2 ',' sconname",
"namelist2 : sconname",
"namelist2 : error",
"sconname : NAME",
"rule : re2 re",
"rule : re2 re '$'",
"rule : re '$'",
"rule : re",
"re : re '|' series",
"re : series",
"re2 : re '/'",
"series : series singleton",
"series : singleton",
"series : series BEGIN_REPEAT_POSIX NUMBER ',' NUMBER END_REPEAT_POSIX",
"series : series BEGIN_REPEAT_POSIX NUMBER ',' END_REPEAT_POSIX",
"series : series BEGIN_REPEAT_POSIX NUMBER END_REPEAT_POSIX",
"singleton : singleton '*'",
"singleton : singleton '+'",
"singleton : singleton '?'",
"singleton : singleton BEGIN_REPEAT_FLEX NUMBER ',' NUMBER END_REPEAT_FLEX",
"singleton : singleton BEGIN_REPEAT_FLEX NUMBER ',' END_REPEAT_FLEX",
"singleton : singleton BEGIN_REPEAT_FLEX NUMBER END_REPEAT_FLEX",
"singleton : '.'",
"singleton : fullccl",
"singleton : PREVCCL",
"singleton : '\"' string '\"'",
"singleton : '(' re ')'",
"singleton : CHAR",
"fullccl : fullccl CCL_OP_DIFF braceccl",
"fullccl : fullccl CCL_OP_UNION braceccl",
"fullccl : braceccl",
"braceccl : '[' ccl ']'",
"braceccl : '[' '^' ccl ']'",
"ccl : ccl CHAR '-' CHAR",
"ccl : ccl CHAR",
"ccl : ccl ccl_expr",
"ccl :",
"ccl_expr : CCE_ALNUM",
"ccl_expr : CCE_ALPHA",
"ccl_expr : CCE_BLANK",
"ccl_expr : CCE_CNTRL",
"ccl_expr : CCE_DIGIT",
"ccl_expr : CCE_GRAPH",
"ccl_expr : CCE_LOWER",
"ccl_expr : CCE_PRINT",
"ccl_expr : CCE_PUNCT",
"ccl_expr : CCE_SPACE",
"ccl_expr : CCE_XDIGIT",
"ccl_expr : CCE_UPPER",
"ccl_expr : CCE_NEG_ALNUM",
"ccl_expr : CCE_NEG_ALPHA",
"ccl_expr : CCE_NEG_BLANK",
"ccl_expr : CCE_NEG_CNTRL",
"ccl_expr : CCE_NEG_DIGIT",
"ccl_expr : CCE_NEG_GRAPH",
"ccl_expr : CCE_NEG_PRINT",
"ccl_expr : CCE_NEG_PUNCT",
"ccl_expr : CCE_NEG_SPACE",
"ccl_expr : CCE_NEG_XDIGIT",
"ccl_expr : CCE_NEG_LOWER",
"ccl_expr : CCE_NEG_UPPER",
"string : string CHAR",
"string :",

};
#endif

int      yydebug;
int      yynerrs;

int      yyerrflag;
int      yychar;
YYSTYPE  yyval;
YYSTYPE  yylval;

/* define the initial stack-sizes */
#ifdef YYSTACKSIZE
#undef YYMAXDEPTH
#define YYMAXDEPTH  YYSTACKSIZE
#else
#ifdef YYMAXDEPTH
#define YYSTACKSIZE YYMAXDEPTH
#else
#define YYSTACKSIZE 500
#define YYMAXDEPTH  500
#endif
#endif

#define YYINITSTACKSIZE 500

typedef struct {
    unsigned stacksize;
    short    *s_base;
    short    *s_mark;
    short    *s_last;
    YYSTYPE  *l_base;
    YYSTYPE  *l_mark;
} YYSTACKDATA;
/* variables for the parser stack */
static YYSTACKDATA yystack;
#line 948 "parse.y"


/* build_eof_action - build the "<<EOF>>" action for the active start
 *                    conditions
 */

void build_eof_action()
	{
	int i;
	char action_text[MAXLINE];

	for ( i = 1; i <= scon_stk_ptr; ++i )
		{
		if ( sceof[scon_stk[i]] )
			format_pinpoint_message(
				"multiple <<EOF>> rules for start condition %s",
				scname[scon_stk[i]] );

		else
			{
			sceof[scon_stk[i]] = true;

			if (previous_continued_action /* && previous action was regular */)
				add_action("YY_RULE_SETUP\n");

			snprintf( action_text, sizeof(action_text), "case YY_STATE_EOF(%s):\n",
				scname[scon_stk[i]] );
			add_action( action_text );
			}
		}

	line_directive_out( (FILE *) 0, 1 );

	/* This isn't a normal rule after all - don't count it as
	 * such, so we don't have any holes in the rule numbering
	 * (which make generating "rule can never match" warnings
	 * more difficult.
	 */
	--num_rules;
	++num_eof_rules;
	}


/* format_synerr - write out formatted syntax error */

void format_synerr( msg, arg )
const char *msg, arg[];
	{
	char errmsg[MAXLINE];

	(void) snprintf( errmsg, sizeof(errmsg), msg, arg );
	synerr( errmsg );
	}


/* synerr - report a syntax error */

void synerr( str )
const char *str;
	{
	syntaxerror = true;
	pinpoint_message( str );
	}


/* format_warn - write out formatted warning */

void format_warn( msg, arg )
const char *msg, arg[];
	{
	char warn_msg[MAXLINE];

	snprintf( warn_msg, sizeof(warn_msg), msg, arg );
	warn( warn_msg );
	}


/* warn - report a warning, unless -w was given */

void warn( str )
const char *str;
	{
	line_warning( str, linenum );
	}

/* format_pinpoint_message - write out a message formatted with one string,
 *			     pinpointing its location
 */

void format_pinpoint_message( msg, arg )
const char *msg, arg[];
	{
	char errmsg[MAXLINE];

	snprintf( errmsg, sizeof(errmsg), msg, arg );
	pinpoint_message( errmsg );
	}


/* pinpoint_message - write out a message, pinpointing its location */

void pinpoint_message( str )
const char *str;
	{
	line_pinpoint( str, linenum );
	}


/* line_warning - report a warning at a given line, unless -w was given */

void line_warning( str, line )
const char *str;
int line;
	{
	char warning[MAXLINE];

	if ( ! nowarn )
		{
		snprintf( warning, sizeof(warning), "warning, %s", str );
		line_pinpoint( warning, line );
		}
	}


/* line_pinpoint - write out a message, pinpointing it at the given line */

void line_pinpoint( str, line )
const char *str;
int line;
	{
	fprintf( stderr, "%s:%d: %s\n", infilename, line, str );
	}


/* yyerror - eat up an error message from the parser;
 *	     currently, messages are ignore
 */

void yyerror( msg )
const char *msg;
	{
	}
#line 656 "parse.c"

#if YYDEBUG
#include <stdio.h>		/* needed for printf */
#endif

#include <stdlib.h>	/* needed for malloc, etc */
#include <string.h>	/* needed for memset */

/* allocate initial stack or double stack size, up to YYMAXDEPTH */
static int yygrowstack(YYSTACKDATA *data)
{
    int i;
    unsigned newsize;
    short *newss;
    YYSTYPE *newvs;

    if ((newsize = data->stacksize) == 0)
        newsize = YYINITSTACKSIZE;
    else if (newsize >= YYMAXDEPTH)
        return -1;
    else if ((newsize *= 2) > YYMAXDEPTH)
        newsize = YYMAXDEPTH;

    i = data->s_mark - data->s_base;
    newss = (short *)realloc(data->s_base, newsize * sizeof(*newss));
    if (newss == NULL)
        return -1;

    data->s_base = newss;
    data->s_mark = newss + i;

    newvs = (YYSTYPE *)realloc(data->l_base, newsize * sizeof(*newvs));
    if (newvs == NULL)
        return -1;

    data->l_base = newvs;
    data->l_mark = newvs + i;

    data->stacksize = newsize;
    data->s_last = data->s_base + newsize - 1;
    return 0;
}

#if YYPURE || defined(YY_NO_LEAKS)
static void yyfreestack(YYSTACKDATA *data)
{
    free(data->s_base);
    free(data->l_base);
    memset(data, 0, sizeof(*data));
}
#else
#define yyfreestack(data) /* nothing */
#endif

#define YYABORT  goto yyabort
#define YYREJECT goto yyabort
#define YYACCEPT goto yyaccept
#define YYERROR  goto yyerrlab

int
YYPARSE_DECL()
{
    int yym, yyn, yystate;
#if YYDEBUG
    const char *yys;

    if ((yys = getenv("YYDEBUG")) != NULL)
    {
        yyn = *yys;
        if (yyn >= '0' && yyn <= '9')
            yydebug = yyn - '0';
    }
#endif

    yynerrs = 0;
    yyerrflag = 0;
    yychar = YYEMPTY;
    yystate = 0;

#if YYPURE
    memset(&yystack, 0, sizeof(yystack));
#endif

    if (yystack.s_base == NULL && yygrowstack(&yystack)) goto yyoverflow;
    yystack.s_mark = yystack.s_base;
    yystack.l_mark = yystack.l_base;
    yystate = 0;
    *yystack.s_mark = 0;

yyloop:
    if ((yyn = yydefred[yystate]) != 0) goto yyreduce;
    if (yychar < 0)
    {
        if ((yychar = YYLEX) < 0) yychar = 0;
#if YYDEBUG
        if (yydebug)
        {
            yys = 0;
            if (yychar <= YYMAXTOKEN) yys = yyname[yychar];
            if (!yys) yys = "illegal-symbol";
            printf("%sdebug: state %d, reading %d (%s)\n",
                    YYPREFIX, yystate, yychar, yys);
        }
#endif
    }
    if ((yyn = yysindex[yystate]) && (yyn += yychar) >= 0 &&
            yyn <= YYTABLESIZE && yycheck[yyn] == yychar)
    {
#if YYDEBUG
        if (yydebug)
            printf("%sdebug: state %d, shifting to state %d\n",
                    YYPREFIX, yystate, yytable[yyn]);
#endif
        if (yystack.s_mark >= yystack.s_last && yygrowstack(&yystack))
        {
            goto yyoverflow;
        }
        yystate = yytable[yyn];
        *++yystack.s_mark = yytable[yyn];
        *++yystack.l_mark = yylval;
        yychar = YYEMPTY;
        if (yyerrflag > 0)  --yyerrflag;
        goto yyloop;
    }
    if ((yyn = yyrindex[yystate]) && (yyn += yychar) >= 0 &&
            yyn <= YYTABLESIZE && yycheck[yyn] == yychar)
    {
        yyn = yytable[yyn];
        goto yyreduce;
    }
    if (yyerrflag) goto yyinrecovery;

    yyerror("syntax error");

    goto yyerrlab;

yyerrlab:
    ++yynerrs;

yyinrecovery:
    if (yyerrflag < 3)
    {
        yyerrflag = 3;
        for (;;)
        {
            if ((yyn = yysindex[*yystack.s_mark]) && (yyn += YYERRCODE) >= 0 &&
                    yyn <= YYTABLESIZE && yycheck[yyn] == YYERRCODE)
            {
#if YYDEBUG
                if (yydebug)
                    printf("%sdebug: state %d, error recovery shifting\
 to state %d\n", YYPREFIX, *yystack.s_mark, yytable[yyn]);
#endif
                if (yystack.s_mark >= yystack.s_last && yygrowstack(&yystack))
                {
                    goto yyoverflow;
                }
                yystate = yytable[yyn];
                *++yystack.s_mark = yytable[yyn];
                *++yystack.l_mark = yylval;
                goto yyloop;
            }
            else
            {
#if YYDEBUG
                if (yydebug)
                    printf("%sdebug: error recovery discarding state %d\n",
                            YYPREFIX, *yystack.s_mark);
#endif
                if (yystack.s_mark <= yystack.s_base) goto yyabort;
                --yystack.s_mark;
                --yystack.l_mark;
            }
        }
    }
    else
    {
        if (yychar == 0) goto yyabort;
#if YYDEBUG
        if (yydebug)
        {
            yys = 0;
            if (yychar <= YYMAXTOKEN) yys = yyname[yychar];
            if (!yys) yys = "illegal-symbol";
            printf("%sdebug: state %d, error recovery discards token %d (%s)\n",
                    YYPREFIX, yystate, yychar, yys);
        }
#endif
        yychar = YYEMPTY;
        goto yyloop;
    }

yyreduce:
#if YYDEBUG
    if (yydebug)
        printf("%sdebug: state %d, reducing by rule %d (%s)\n",
                YYPREFIX, yystate, yyn, yyrule[yyn]);
#endif
    yym = yylen[yyn];
    if (yym)
        yyval = yystack.l_mark[1-yym];
    else
        memset(&yyval, 0, sizeof yyval);
    switch (yyn)
    {
case 1:
#line 119 "parse.y"
	{ /* add default rule */
			int def_rule;

			pat = cclinit();
			cclnegate( pat );

			def_rule = mkstate( -pat );

			/* Remember the number of the default rule so we
			 * don't generate "can't match" warnings for it.
			 */
			default_rule = num_rules;

			finish_rule( def_rule, false, 0, 0, 0);

			for ( i = 1; i <= lastsc; ++i )
				scset[i] = mkbranch( scset[i], def_rule );

			if ( spprdflt )
				add_action(
				"YY_FATAL_ERROR( \"flex scanner jammed\" )" );
			else
				add_action( "ECHO" );

			add_action( ";\n\tYY_BREAK\n" );
			}
break;
case 2:
#line 148 "parse.y"
	{ /* initialize for processing rules */

			/* Create default DFA start condition. */
			scinstal( "INITIAL", false );
			}
break;
case 6:
#line 159 "parse.y"
	{ synerr( _("unknown error processing section 1") ); }
break;
case 7:
#line 163 "parse.y"
	{
			check_options();
			scon_stk = allocate_integer_array( lastsc + 1 );
			scon_stk_ptr = 0;
			}
break;
case 8:
#line 171 "parse.y"
	{ xcluflg = false; }
break;
case 9:
#line 174 "parse.y"
	{ xcluflg = true; }
break;
case 10:
#line 178 "parse.y"
	{ scinstal( nmstr, xcluflg ); }
break;
case 11:
#line 181 "parse.y"
	{ scinstal( nmstr, xcluflg ); }
break;
case 12:
#line 184 "parse.y"
	{ synerr( _("bad start condition list") ); }
break;
case 16:
#line 195 "parse.y"
	{
			outfilename = copy_string( nmstr );
			did_outfilename = 1;
			}
break;
case 17:
#line 200 "parse.y"
	{ extra_type = copy_string( nmstr ); }
break;
case 18:
#line 202 "parse.y"
	{ prefix = copy_string( nmstr ); }
break;
case 19:
#line 204 "parse.y"
	{ yyclass = copy_string( nmstr ); }
break;
case 20:
#line 206 "parse.y"
	{ headerfilename = copy_string( nmstr ); }
break;
case 21:
#line 208 "parse.y"
	{ tablesext = true; tablesfilename = copy_string( nmstr ); }
break;
case 22:
#line 212 "parse.y"
	{ scon_stk_ptr = yystack.l_mark[-3]; }
break;
case 23:
#line 214 "parse.y"
	{ scon_stk_ptr = yystack.l_mark[-3]; }
break;
case 25:
#line 219 "parse.y"
	{
			/* Initialize for a parse of one rule. */
			trlcontxt = variable_trail_rule = varlength = false;
			trailcnt = headcnt = rulelen = 0;
			current_state_type = STATE_NORMAL;
			previous_continued_action = continued_action;
			in_rule = true;

			new_rule();
			}
break;
case 26:
#line 232 "parse.y"
	{
			pat = yystack.l_mark[0];
			finish_rule( pat, variable_trail_rule,
				headcnt, trailcnt , previous_continued_action);

			if ( scon_stk_ptr > 0 )
				{
				for ( i = 1; i <= scon_stk_ptr; ++i )
					scbol[scon_stk[i]] =
						mkbranch( scbol[scon_stk[i]],
								pat );
				}

			else
				{
				/* Add to all non-exclusive start conditions,
				 * including the default (0) start condition.
				 */

				for ( i = 1; i <= lastsc; ++i )
					if ( ! scxclu[i] )
						scbol[i] = mkbranch( scbol[i],
									pat );
				}

			if ( ! bol_needed )
				{
				bol_needed = true;

				if ( performance_report > 1 )
					pinpoint_message(
			"'^' operator results in sub-optimal performance" );
				}
			}
break;
case 27:
#line 268 "parse.y"
	{
			pat = yystack.l_mark[0];
			finish_rule( pat, variable_trail_rule,
				headcnt, trailcnt , previous_continued_action);

			if ( scon_stk_ptr > 0 )
				{
				for ( i = 1; i <= scon_stk_ptr; ++i )
					scset[scon_stk[i]] =
						mkbranch( scset[scon_stk[i]],
								pat );
				}

			else
				{
				for ( i = 1; i <= lastsc; ++i )
					if ( ! scxclu[i] )
						scset[i] =
							mkbranch( scset[i],
								pat );
				}
			}
break;
case 28:
#line 292 "parse.y"
	{
			if ( scon_stk_ptr > 0 )
				build_eof_action();
	
			else
				{
				/* This EOF applies to all start conditions
				 * which don't already have EOF actions.
				 */
				for ( i = 1; i <= lastsc; ++i )
					if ( ! sceof[i] )
						scon_stk[++scon_stk_ptr] = i;

				if ( scon_stk_ptr == 0 )
					warn(
			"all start conditions already have <<EOF>> rules" );

				else
					build_eof_action();
				}
			}
break;
case 29:
#line 315 "parse.y"
	{ synerr( _("unrecognized rule") ); }
break;
case 30:
#line 319 "parse.y"
	{ yyval = scon_stk_ptr; }
break;
case 31:
#line 323 "parse.y"
	{ yyval = yystack.l_mark[-2]; }
break;
case 32:
#line 326 "parse.y"
	{
			yyval = scon_stk_ptr;

			for ( i = 1; i <= lastsc; ++i )
				{
				int j;

				for ( j = 1; j <= scon_stk_ptr; ++j )
					if ( scon_stk[j] == i )
						break;

				if ( j > scon_stk_ptr )
					scon_stk[++scon_stk_ptr] = i;
				}
			}
break;
case 33:
#line 343 "parse.y"
	{ yyval = scon_stk_ptr; }
break;
case 36:
#line 351 "parse.y"
	{ synerr( _("bad start condition list") ); }
break;
case 37:
#line 355 "parse.y"
	{
			if ( (scnum = sclookup( nmstr )) == 0 )
				format_pinpoint_message(
					"undeclared start condition %s",
					nmstr );
			else
				{
				for ( i = 1; i <= scon_stk_ptr; ++i )
					if ( scon_stk[i] == scnum )
						{
						format_warn(
							"<%s> specified twice",
							scname[scnum] );
						break;
						}

				if ( i > scon_stk_ptr )
					scon_stk[++scon_stk_ptr] = scnum;
				}
			}
break;
case 38:
#line 378 "parse.y"
	{
			if ( transchar[lastst[yystack.l_mark[0]]] != SYM_EPSILON )
				/* Provide final transition \now/ so it
				 * will be marked as a trailing context
				 * state.
				 */
				yystack.l_mark[0] = link_machines( yystack.l_mark[0],
						mkstate( SYM_EPSILON ) );

			mark_beginning_as_normal( yystack.l_mark[0] );
			current_state_type = STATE_NORMAL;

			if ( previous_continued_action )
				{
				/* We need to treat this as variable trailing
				 * context so that the backup does not happen
				 * in the action but before the action switch
				 * statement.  If the backup happens in the
				 * action, then the rules "falling into" this
				 * one's action will *also* do the backup,
				 * erroneously.
				 */
				if ( ! varlength || headcnt != 0 )
					warn(
		"trailing context made variable due to preceding '|' action" );

				/* Mark as variable. */
				varlength = true;
				headcnt = 0;

				}

			if ( lex_compat || (varlength && headcnt == 0) )
				{ /* variable trailing context rule */
				/* Mark the first part of the rule as the
				 * accepting "head" part of a trailing
				 * context rule.
				 *
				 * By the way, we didn't do this at the
				 * beginning of this production because back
				 * then current_state_type was set up for a
				 * trail rule, and add_accept() can create
				 * a new state ...
				 */
				add_accept( yystack.l_mark[-1],
					num_rules | YY_TRAILING_HEAD_MASK );
				variable_trail_rule = true;
				}
			
			else
				trailcnt = rulelen;

			yyval = link_machines( yystack.l_mark[-1], yystack.l_mark[0] );
			}
break;
case 39:
#line 434 "parse.y"
	{ synerr( _("trailing context used twice") ); }
break;
case 40:
#line 437 "parse.y"
	{
			headcnt = 0;
			trailcnt = 1;
			rulelen = 1;
			varlength = false;

			current_state_type = STATE_TRAILING_CONTEXT;

			if ( trlcontxt )
				{
				synerr( _("trailing context used twice") );
				yyval = mkstate( SYM_EPSILON );
				}

			else if ( previous_continued_action )
				{
				/* See the comment in the rule for "re2 re"
				 * above.
				 */
				warn(
		"trailing context made variable due to preceding '|' action" );

				varlength = true;
				}

			if ( lex_compat || varlength )
				{
				/* Again, see the comment in the rule for
				 * "re2 re" above.
				 */
				add_accept( yystack.l_mark[-1],
					num_rules | YY_TRAILING_HEAD_MASK );
				variable_trail_rule = true;
				}

			trlcontxt = true;

			eps = mkstate( SYM_EPSILON );
			yyval = link_machines( yystack.l_mark[-1],
				link_machines( eps, mkstate( '\n' ) ) );
			}
break;
case 41:
#line 480 "parse.y"
	{
			yyval = yystack.l_mark[0];

			if ( trlcontxt )
				{
				if ( lex_compat || (varlength && headcnt == 0) )
					/* Both head and trail are
					 * variable-length.
					 */
					variable_trail_rule = true;
				else
					trailcnt = rulelen;
				}
			}
break;
case 42:
#line 498 "parse.y"
	{
			varlength = true;
			yyval = mkor( yystack.l_mark[-2], yystack.l_mark[0] );
			}
break;
case 43:
#line 504 "parse.y"
	{ yyval = yystack.l_mark[0]; }
break;
case 44:
#line 509 "parse.y"
	{
			/* This rule is written separately so the
			 * reduction will occur before the trailing
			 * series is parsed.
			 */

			if ( trlcontxt )
				synerr( _("trailing context used twice") );
			else
				trlcontxt = true;

			if ( varlength )
				/* We hope the trailing context is
				 * fixed-length.
				 */
				varlength = false;
			else
				headcnt = rulelen;

			rulelen = 0;

			current_state_type = STATE_TRAILING_CONTEXT;
			yyval = yystack.l_mark[-1];
			}
break;
case 45:
#line 536 "parse.y"
	{
			/* This is where concatenation of adjacent patterns
			 * gets done.
			 */
			yyval = link_machines( yystack.l_mark[-1], yystack.l_mark[0] );
			}
break;
case 46:
#line 544 "parse.y"
	{ yyval = yystack.l_mark[0]; }
break;
case 47:
#line 547 "parse.y"
	{
			varlength = true;

			if ( yystack.l_mark[-3] > yystack.l_mark[-1] || yystack.l_mark[-3] < 0 )
				{
				synerr( _("bad iteration values") );
				yyval = yystack.l_mark[-5];
				}
			else
				{
				if ( yystack.l_mark[-3] == 0 )
					{
					if ( yystack.l_mark[-1] <= 0 )
						{
						synerr(
						_("bad iteration values") );
						yyval = yystack.l_mark[-5];
						}
					else
						yyval = mkopt(
							mkrep( yystack.l_mark[-5], 1, yystack.l_mark[-1] ) );
					}
				else
					yyval = mkrep( yystack.l_mark[-5], yystack.l_mark[-3], yystack.l_mark[-1] );
				}
			}
break;
case 48:
#line 575 "parse.y"
	{
			varlength = true;

			if ( yystack.l_mark[-2] <= 0 )
				{
				synerr( _("iteration value must be positive") );
				yyval = yystack.l_mark[-4];
				}

			else
				yyval = mkrep( yystack.l_mark[-4], yystack.l_mark[-2], INFINITE_REPEAT );
			}
break;
case 49:
#line 589 "parse.y"
	{
			/* The series could be something like "(foo)",
			 * in which case we have no idea what its length
			 * is, so we punt here.
			 */
			varlength = true;

			if ( yystack.l_mark[-1] <= 0 )
				{
				  synerr( _("iteration value must be positive")
					  );
				yyval = yystack.l_mark[-3];
				}

			else
				yyval = link_machines( yystack.l_mark[-3],
						copysingl( yystack.l_mark[-3], yystack.l_mark[-1] - 1 ) );
			}
break;
case 50:
#line 611 "parse.y"
	{
			varlength = true;

			yyval = mkclos( yystack.l_mark[-1] );
			}
break;
case 51:
#line 618 "parse.y"
	{
			varlength = true;
			yyval = mkposcl( yystack.l_mark[-1] );
			}
break;
case 52:
#line 624 "parse.y"
	{
			varlength = true;
			yyval = mkopt( yystack.l_mark[-1] );
			}
break;
case 53:
#line 630 "parse.y"
	{
			varlength = true;

			if ( yystack.l_mark[-3] > yystack.l_mark[-1] || yystack.l_mark[-3] < 0 )
				{
				synerr( _("bad iteration values") );
				yyval = yystack.l_mark[-5];
				}
			else
				{
				if ( yystack.l_mark[-3] == 0 )
					{
					if ( yystack.l_mark[-1] <= 0 )
						{
						synerr(
						_("bad iteration values") );
						yyval = yystack.l_mark[-5];
						}
					else
						yyval = mkopt(
							mkrep( yystack.l_mark[-5], 1, yystack.l_mark[-1] ) );
					}
				else
					yyval = mkrep( yystack.l_mark[-5], yystack.l_mark[-3], yystack.l_mark[-1] );
				}
			}
break;
case 54:
#line 658 "parse.y"
	{
			varlength = true;

			if ( yystack.l_mark[-2] <= 0 )
				{
				synerr( _("iteration value must be positive") );
				yyval = yystack.l_mark[-4];
				}

			else
				yyval = mkrep( yystack.l_mark[-4], yystack.l_mark[-2], INFINITE_REPEAT );
			}
break;
case 55:
#line 672 "parse.y"
	{
			/* The singleton could be something like "(foo)",
			 * in which case we have no idea what its length
			 * is, so we punt here.
			 */
			varlength = true;

			if ( yystack.l_mark[-1] <= 0 )
				{
				synerr( _("iteration value must be positive") );
				yyval = yystack.l_mark[-3];
				}

			else
				yyval = link_machines( yystack.l_mark[-3],
						copysingl( yystack.l_mark[-3], yystack.l_mark[-1] - 1 ) );
			}
break;
case 56:
#line 691 "parse.y"
	{
			if ( ! madeany )
				{
				/* Create the '.' character class. */
                    ccldot = cclinit();
                    ccladd( ccldot, '\n' );
                    cclnegate( ccldot );

                    if ( useecs )
                        mkeccl( ccltbl + cclmap[ccldot],
                            ccllen[ccldot], nextecm,
                            ecgroup, csize, csize );

				/* Create the (?s:'.') character class. */
                    cclany = cclinit();
                    cclnegate( cclany );

                    if ( useecs )
                        mkeccl( ccltbl + cclmap[cclany],
                            ccllen[cclany], nextecm,
                            ecgroup, csize, csize );

				madeany = true;
				}

			++rulelen;

            if (sf_dot_all())
                yyval = mkstate( -cclany );
            else
                yyval = mkstate( -ccldot );
			}
break;
case 57:
#line 725 "parse.y"
	{
				/* Sort characters for fast searching.
				 */
				qsort( ccltbl + cclmap[yystack.l_mark[0]], ccllen[yystack.l_mark[0]], sizeof (*ccltbl), cclcmp );

			if ( useecs )
				mkeccl( ccltbl + cclmap[yystack.l_mark[0]], ccllen[yystack.l_mark[0]],
					nextecm, ecgroup, csize, csize );

			++rulelen;

			if (ccl_has_nl[yystack.l_mark[0]])
				rule_has_nl[num_rules] = true;

			yyval = mkstate( -yystack.l_mark[0] );
			}
break;
case 58:
#line 743 "parse.y"
	{
			++rulelen;

			if (ccl_has_nl[yystack.l_mark[0]])
				rule_has_nl[num_rules] = true;

			yyval = mkstate( -yystack.l_mark[0] );
			}
break;
case 59:
#line 753 "parse.y"
	{ yyval = yystack.l_mark[-1]; }
break;
case 60:
#line 756 "parse.y"
	{ yyval = yystack.l_mark[-1]; }
break;
case 61:
#line 759 "parse.y"
	{
			++rulelen;

			if (yystack.l_mark[0] == nlch)
				rule_has_nl[num_rules] = true;

            if (sf_case_ins() && has_case(yystack.l_mark[0]))
                /* create an alternation, as in (a|A) */
                yyval = mkor (mkstate(yystack.l_mark[0]), mkstate(reverse_case(yystack.l_mark[0])));
            else
                yyval = mkstate( yystack.l_mark[0] );
			}
break;
case 62:
#line 773 "parse.y"
	{ yyval = ccl_set_diff  (yystack.l_mark[-2], yystack.l_mark[0]); }
break;
case 63:
#line 774 "parse.y"
	{ yyval = ccl_set_union (yystack.l_mark[-2], yystack.l_mark[0]); }
break;
case 65:
#line 780 "parse.y"
	{ yyval = yystack.l_mark[-1]; }
break;
case 66:
#line 783 "parse.y"
	{
			cclnegate( yystack.l_mark[-1] );
			yyval = yystack.l_mark[-1];
			}
break;
case 67:
#line 790 "parse.y"
	{

			if (sf_case_ins())
			  {

			    /* If one end of the range has case and the other
			     * does not, or the cases are different, then we're not
			     * sure what range the user is trying to express.
			     * Examples: [@-z] or [S-t]
			     */
			    if (has_case (yystack.l_mark[-2]) != has_case (yystack.l_mark[0])
				     || (has_case (yystack.l_mark[-2]) && (b_islower (yystack.l_mark[-2]) != b_islower (yystack.l_mark[0])))
				     || (has_case (yystack.l_mark[-2]) && (b_isupper (yystack.l_mark[-2]) != b_isupper (yystack.l_mark[0]))))
			      format_warn3 (
			      _("the character range [%c-%c] is ambiguous in a case-insensitive scanner"),
					    yystack.l_mark[-2], yystack.l_mark[0]);

			    /* If the range spans uppercase characters but not
			     * lowercase (or vice-versa), then should we automatically
			     * include lowercase characters in the range?
			     * Example: [@-_] spans [a-z] but not [A-Z]
			     */
			    else if (!has_case (yystack.l_mark[-2]) && !has_case (yystack.l_mark[0]) && !range_covers_case (yystack.l_mark[-2], yystack.l_mark[0]))
			      format_warn3 (
			      _("the character range [%c-%c] is ambiguous in a case-insensitive scanner"),
					    yystack.l_mark[-2], yystack.l_mark[0]);
			  }

			if ( yystack.l_mark[-2] > yystack.l_mark[0] )
				synerr( _("negative range in character class") );

			else
				{
				for ( i = yystack.l_mark[-2]; i <= yystack.l_mark[0]; ++i )
					ccladd( yystack.l_mark[-3], i );

				/* Keep track if this ccl is staying in
				 * alphabetical order.
				 */
				cclsorted = cclsorted && (yystack.l_mark[-2] > lastchar);
				lastchar = yystack.l_mark[0];

                /* Do it again for upper/lowercase */
                if (sf_case_ins() && has_case(yystack.l_mark[-2]) && has_case(yystack.l_mark[0])){
                    yystack.l_mark[-2] = reverse_case (yystack.l_mark[-2]);
                    yystack.l_mark[0] = reverse_case (yystack.l_mark[0]);
                    
                    for ( i = yystack.l_mark[-2]; i <= yystack.l_mark[0]; ++i )
                        ccladd( yystack.l_mark[-3], i );

                    cclsorted = cclsorted && (yystack.l_mark[-2] > lastchar);
                    lastchar = yystack.l_mark[0];
                }

				}

			yyval = yystack.l_mark[-3];
			}
break;
case 68:
#line 850 "parse.y"
	{
			ccladd( yystack.l_mark[-1], yystack.l_mark[0] );
			cclsorted = cclsorted && (yystack.l_mark[0] > lastchar);
			lastchar = yystack.l_mark[0];

            /* Do it again for upper/lowercase */
            if (sf_case_ins() && has_case(yystack.l_mark[0])){
                yystack.l_mark[0] = reverse_case (yystack.l_mark[0]);
                ccladd (yystack.l_mark[-1], yystack.l_mark[0]);

                cclsorted = cclsorted && (yystack.l_mark[0] > lastchar);
                lastchar = yystack.l_mark[0];
            }

			yyval = yystack.l_mark[-1];
			}
break;
case 69:
#line 868 "parse.y"
	{
			/* Too hard to properly maintain cclsorted. */
			cclsorted = false;
			yyval = yystack.l_mark[-1];
			}
break;
case 70:
#line 875 "parse.y"
	{
			cclsorted = true;
			lastchar = 0;
			currccl = yyval = cclinit();
			}
break;
case 71:
#line 883 "parse.y"
	{ CCL_EXPR(isalnum); }
break;
case 72:
#line 884 "parse.y"
	{ CCL_EXPR(isalpha); }
break;
case 73:
#line 885 "parse.y"
	{ CCL_EXPR(IS_BLANK); }
break;
case 74:
#line 886 "parse.y"
	{ CCL_EXPR(iscntrl); }
break;
case 75:
#line 887 "parse.y"
	{ CCL_EXPR(isdigit); }
break;
case 76:
#line 888 "parse.y"
	{ CCL_EXPR(isgraph); }
break;
case 77:
#line 889 "parse.y"
	{ 
                          CCL_EXPR(islower);
                          if (sf_case_ins())
                              CCL_EXPR(isupper);
                        }
break;
case 78:
#line 894 "parse.y"
	{ CCL_EXPR(isprint); }
break;
case 79:
#line 895 "parse.y"
	{ CCL_EXPR(ispunct); }
break;
case 80:
#line 896 "parse.y"
	{ CCL_EXPR(isspace); }
break;
case 81:
#line 897 "parse.y"
	{ CCL_EXPR(isxdigit); }
break;
case 82:
#line 898 "parse.y"
	{
                    CCL_EXPR(isupper);
                    if (sf_case_ins())
                        CCL_EXPR(islower);
				}
break;
case 83:
#line 904 "parse.y"
	{ CCL_NEG_EXPR(isalnum); }
break;
case 84:
#line 905 "parse.y"
	{ CCL_NEG_EXPR(isalpha); }
break;
case 85:
#line 906 "parse.y"
	{ CCL_NEG_EXPR(IS_BLANK); }
break;
case 86:
#line 907 "parse.y"
	{ CCL_NEG_EXPR(iscntrl); }
break;
case 87:
#line 908 "parse.y"
	{ CCL_NEG_EXPR(isdigit); }
break;
case 88:
#line 909 "parse.y"
	{ CCL_NEG_EXPR(isgraph); }
break;
case 89:
#line 910 "parse.y"
	{ CCL_NEG_EXPR(isprint); }
break;
case 90:
#line 911 "parse.y"
	{ CCL_NEG_EXPR(ispunct); }
break;
case 91:
#line 912 "parse.y"
	{ CCL_NEG_EXPR(isspace); }
break;
case 92:
#line 913 "parse.y"
	{ CCL_NEG_EXPR(isxdigit); }
break;
case 93:
#line 914 "parse.y"
	{ 
				if ( sf_case_ins() )
					warn(_("[:^lower:] is ambiguous in case insensitive scanner"));
				else
					CCL_NEG_EXPR(islower);
				}
break;
case 94:
#line 920 "parse.y"
	{
				if ( sf_case_ins() )
					warn(_("[:^upper:] ambiguous in case insensitive scanner"));
				else
					CCL_NEG_EXPR(isupper);
				}
break;
case 95:
#line 929 "parse.y"
	{
			if ( yystack.l_mark[0] == nlch )
				rule_has_nl[num_rules] = true;

			++rulelen;

            if (sf_case_ins() && has_case(yystack.l_mark[0]))
                yyval = mkor (mkstate(yystack.l_mark[0]), mkstate(reverse_case(yystack.l_mark[0])));
            else
                yyval = mkstate (yystack.l_mark[0]);

			yyval = link_machines( yystack.l_mark[-1], yyval);
			}
break;
case 96:
#line 944 "parse.y"
	{ yyval = mkstate( SYM_EPSILON ); }
break;
#line 1787 "parse.c"
    }
    yystack.s_mark -= yym;
    yystate = *yystack.s_mark;
    yystack.l_mark -= yym;
    yym = yylhs[yyn];
    if (yystate == 0 && yym == 0)
    {
#if YYDEBUG
        if (yydebug)
            printf("%sdebug: after reduction, shifting from state 0 to\
 state %d\n", YYPREFIX, YYFINAL);
#endif
        yystate = YYFINAL;
        *++yystack.s_mark = YYFINAL;
        *++yystack.l_mark = yyval;
        if (yychar < 0)
        {
            if ((yychar = YYLEX) < 0) yychar = 0;
#if YYDEBUG
            if (yydebug)
            {
                yys = 0;
                if (yychar <= YYMAXTOKEN) yys = yyname[yychar];
                if (!yys) yys = "illegal-symbol";
                printf("%sdebug: state %d, reading %d (%s)\n",
                        YYPREFIX, YYFINAL, yychar, yys);
            }
#endif
        }
        if (yychar == 0) goto yyaccept;
        goto yyloop;
    }
    if ((yyn = yygindex[yym]) && (yyn += yystate) >= 0 &&
            yyn <= YYTABLESIZE && yycheck[yyn] == yystate)
        yystate = yytable[yyn];
    else
        yystate = yydgoto[yym];
#if YYDEBUG
    if (yydebug)
        printf("%sdebug: after reduction, shifting from state %d \
to state %d\n", YYPREFIX, *yystack.s_mark, yystate);
#endif
    if (yystack.s_mark >= yystack.s_last && yygrowstack(&yystack))
    {
        goto yyoverflow;
    }
    *++yystack.s_mark = (short) yystate;
    *++yystack.l_mark = yyval;
    goto yyloop;

yyoverflow:
    yyerror("yacc stack overflow");

yyabort:
    yyfreestack(&yystack);
    return (1);

yyaccept:
    yyfreestack(&yystack);
    return (0);
}
