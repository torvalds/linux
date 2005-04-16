
/*  A Bison parser, made from scripts/genksyms/parse.y
    by GNU Bison version 1.28  */

#define YYBISON 1  /* Identify Bison output.  */

#define	ASM_KEYW	257
#define	ATTRIBUTE_KEYW	258
#define	AUTO_KEYW	259
#define	BOOL_KEYW	260
#define	CHAR_KEYW	261
#define	CONST_KEYW	262
#define	DOUBLE_KEYW	263
#define	ENUM_KEYW	264
#define	EXTERN_KEYW	265
#define	FLOAT_KEYW	266
#define	INLINE_KEYW	267
#define	INT_KEYW	268
#define	LONG_KEYW	269
#define	REGISTER_KEYW	270
#define	RESTRICT_KEYW	271
#define	SHORT_KEYW	272
#define	SIGNED_KEYW	273
#define	STATIC_KEYW	274
#define	STRUCT_KEYW	275
#define	TYPEDEF_KEYW	276
#define	UNION_KEYW	277
#define	UNSIGNED_KEYW	278
#define	VOID_KEYW	279
#define	VOLATILE_KEYW	280
#define	TYPEOF_KEYW	281
#define	EXPORT_SYMBOL_KEYW	282
#define	ASM_PHRASE	283
#define	ATTRIBUTE_PHRASE	284
#define	BRACE_PHRASE	285
#define	BRACKET_PHRASE	286
#define	EXPRESSION_PHRASE	287
#define	CHAR	288
#define	DOTS	289
#define	IDENT	290
#define	INT	291
#define	REAL	292
#define	STRING	293
#define	TYPE	294
#define	OTHER	295
#define	FILENAME	296

#line 24 "scripts/genksyms/parse.y"


#include <assert.h>
#include <malloc.h>
#include "genksyms.h"

static int is_typedef;
static int is_extern;
static char *current_name;
static struct string_list *decl_spec;

static void yyerror(const char *);

static inline void
remove_node(struct string_list **p)
{
  struct string_list *node = *p;
  *p = node->next;
  free_node(node);
}

static inline void
remove_list(struct string_list **pb, struct string_list **pe)
{
  struct string_list *b = *pb, *e = *pe;
  *pb = e;
  free_list(b, e);
}

#ifndef YYSTYPE
#define YYSTYPE int
#endif
#ifndef YYDEBUG
#define YYDEBUG 1
#endif

#include <stdio.h>

#ifndef __cplusplus
#ifndef __STDC__
#define const
#endif
#endif



#define	YYFINAL		172
#define	YYFLAG		-32768
#define	YYNTBASE	52

#define YYTRANSLATE(x) ((unsigned)(x) <= 296 ? yytranslate[x] : 96)

static const char yytranslate[] = {     0,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,    46,
    47,    48,     2,    45,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,    51,    43,     2,
    49,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,    50,     2,    44,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     2,     2,     2,     2,     2,     1,     3,     4,     5,     6,
     7,     8,     9,    10,    11,    12,    13,    14,    15,    16,
    17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
    27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
    37,    38,    39,    40,    41,    42
};

#if YYDEBUG != 0
static const short yyprhs[] = {     0,
     0,     2,     5,     6,     9,    10,    14,    16,    18,    20,
    22,    25,    28,    32,    33,    35,    37,    41,    46,    47,
    49,    51,    54,    56,    58,    60,    62,    64,    66,    68,
    70,    72,    77,    80,    83,    86,    90,    94,    98,   101,
   104,   107,   109,   111,   113,   115,   117,   119,   121,   123,
   125,   127,   129,   132,   133,   135,   137,   140,   142,   144,
   146,   148,   151,   153,   155,   160,   165,   168,   172,   176,
   179,   181,   183,   185,   190,   195,   198,   202,   206,   209,
   211,   215,   216,   218,   220,   224,   227,   230,   232,   233,
   235,   237,   242,   247,   250,   254,   258,   262,   263,   265,
   268,   272,   276,   277,   279,   281,   284,   288,   291,   292,
   294,   296,   300,   303,   306,   308,   311,   312,   314,   317,
   318,   320
};

static const short yyrhs[] = {    53,
     0,    52,    53,     0,     0,    54,    55,     0,     0,    22,
    56,    57,     0,    57,     0,    81,     0,    93,     0,    95,
     0,     1,    43,     0,     1,    44,     0,    61,    58,    43,
     0,     0,    59,     0,    60,     0,    59,    45,    60,     0,
    71,    94,    92,    82,     0,     0,    62,     0,    63,     0,
    62,    63,     0,    64,     0,    65,     0,     5,     0,    16,
     0,    20,     0,    11,     0,    13,     0,    66,     0,    70,
     0,    27,    46,    62,    47,     0,    21,    36,     0,    23,
    36,     0,    10,    36,     0,    21,    36,    84,     0,    23,
    36,    84,     0,    10,    36,    31,     0,    10,    31,     0,
    21,    84,     0,    23,    84,     0,     7,     0,    18,     0,
    14,     0,    15,     0,    19,     0,    24,     0,    12,     0,
     9,     0,    25,     0,     6,     0,    40,     0,    48,    68,
     0,     0,    69,     0,    70,     0,    69,    70,     0,     8,
     0,    26,     0,    30,     0,    17,     0,    67,    71,     0,
    72,     0,    36,     0,    72,    46,    75,    47,     0,    72,
    46,     1,    47,     0,    72,    32,     0,    46,    71,    47,
     0,    46,     1,    47,     0,    67,    73,     0,    74,     0,
    36,     0,    40,     0,    74,    46,    75,    47,     0,    74,
    46,     1,    47,     0,    74,    32,     0,    46,    73,    47,
     0,    46,     1,    47,     0,    76,    35,     0,    76,     0,
    77,    45,    35,     0,     0,    77,     0,    78,     0,    77,
    45,    78,     0,    62,    79,     0,    67,    79,     0,    80,
     0,     0,    36,     0,    40,     0,    80,    46,    75,    47,
     0,    80,    46,     1,    47,     0,    80,    32,     0,    46,
    79,    47,     0,    46,     1,    47,     0,    61,    71,    31,
     0,     0,    83,     0,    49,    33,     0,    50,    85,    44,
     0,    50,     1,    44,     0,     0,    86,     0,    87,     0,
    86,    87,     0,    61,    88,    43,     0,     1,    43,     0,
     0,    89,     0,    90,     0,    89,    45,    90,     0,    73,
    92,     0,    36,    91,     0,    91,     0,    51,    33,     0,
     0,    30,     0,    29,    43,     0,     0,    29,     0,    28,
    46,    36,    47,    43,     0
};

#endif

#if YYDEBUG != 0
static const short yyrline[] = { 0,
   101,   103,   106,   109,   112,   114,   115,   116,   117,   118,
   119,   120,   123,   137,   139,   142,   151,   163,   169,   171,
   174,   176,   179,   186,   189,   191,   192,   193,   194,   197,
   199,   200,   204,   206,   208,   212,   219,   226,   235,   236,
   237,   240,   242,   243,   244,   245,   246,   247,   248,   249,
   250,   251,   254,   259,   261,   264,   266,   269,   270,   270,
   271,   278,   280,   283,   293,   295,   297,   299,   301,   307,
   309,   312,   314,   315,   317,   319,   321,   323,   327,   329,
   330,   333,   335,   338,   340,   344,   349,   352,   355,   357,
   365,   369,   371,   373,   375,   377,   381,   390,   392,   396,
   401,   403,   406,   408,   411,   413,   416,   419,   423,   425,
   428,   430,   433,   435,   436,   439,   443,   445,   448,   452,
   454,   457
};
#endif


#if YYDEBUG != 0 || defined (YYERROR_VERBOSE)

static const char * const yytname[] = {   "$","error","$undefined.","ASM_KEYW",
"ATTRIBUTE_KEYW","AUTO_KEYW","BOOL_KEYW","CHAR_KEYW","CONST_KEYW","DOUBLE_KEYW",
"ENUM_KEYW","EXTERN_KEYW","FLOAT_KEYW","INLINE_KEYW","INT_KEYW","LONG_KEYW",
"REGISTER_KEYW","RESTRICT_KEYW","SHORT_KEYW","SIGNED_KEYW","STATIC_KEYW","STRUCT_KEYW",
"TYPEDEF_KEYW","UNION_KEYW","UNSIGNED_KEYW","VOID_KEYW","VOLATILE_KEYW","TYPEOF_KEYW",
"EXPORT_SYMBOL_KEYW","ASM_PHRASE","ATTRIBUTE_PHRASE","BRACE_PHRASE","BRACKET_PHRASE",
"EXPRESSION_PHRASE","CHAR","DOTS","IDENT","INT","REAL","STRING","TYPE","OTHER",
"FILENAME","';'","'}'","','","'('","')'","'*'","'='","'{'","':'","declaration_seq",
"declaration","@1","declaration1","@2","simple_declaration","init_declarator_list_opt",
"init_declarator_list","init_declarator","decl_specifier_seq_opt","decl_specifier_seq",
"decl_specifier","storage_class_specifier","type_specifier","simple_type_specifier",
"ptr_operator","cvar_qualifier_seq_opt","cvar_qualifier_seq","cvar_qualifier",
"declarator","direct_declarator","nested_declarator","direct_nested_declarator",
"parameter_declaration_clause","parameter_declaration_list_opt","parameter_declaration_list",
"parameter_declaration","m_abstract_declarator","direct_m_abstract_declarator",
"function_definition","initializer_opt","initializer","class_body","member_specification_opt",
"member_specification","member_declaration","member_declarator_list_opt","member_declarator_list",
"member_declarator","member_bitfield_declarator","attribute_opt","asm_definition",
"asm_phrase_opt","export_definition", NULL
};
#endif

static const short yyr1[] = {     0,
    52,    52,    54,    53,    56,    55,    55,    55,    55,    55,
    55,    55,    57,    58,    58,    59,    59,    60,    61,    61,
    62,    62,    63,    63,    64,    64,    64,    64,    64,    65,
    65,    65,    65,    65,    65,    65,    65,    65,    65,    65,
    65,    66,    66,    66,    66,    66,    66,    66,    66,    66,
    66,    66,    67,    68,    68,    69,    69,    70,    70,    70,
    70,    71,    71,    72,    72,    72,    72,    72,    72,    73,
    73,    74,    74,    74,    74,    74,    74,    74,    75,    75,
    75,    76,    76,    77,    77,    78,    79,    79,    80,    80,
    80,    80,    80,    80,    80,    80,    81,    82,    82,    83,
    84,    84,    85,    85,    86,    86,    87,    87,    88,    88,
    89,    89,    90,    90,    90,    91,    92,    92,    93,    94,
    94,    95
};

static const short yyr2[] = {     0,
     1,     2,     0,     2,     0,     3,     1,     1,     1,     1,
     2,     2,     3,     0,     1,     1,     3,     4,     0,     1,
     1,     2,     1,     1,     1,     1,     1,     1,     1,     1,
     1,     4,     2,     2,     2,     3,     3,     3,     2,     2,
     2,     1,     1,     1,     1,     1,     1,     1,     1,     1,
     1,     1,     2,     0,     1,     1,     2,     1,     1,     1,
     1,     2,     1,     1,     4,     4,     2,     3,     3,     2,
     1,     1,     1,     4,     4,     2,     3,     3,     2,     1,
     3,     0,     1,     1,     3,     2,     2,     1,     0,     1,
     1,     4,     4,     2,     3,     3,     3,     0,     1,     2,
     3,     3,     0,     1,     1,     2,     3,     2,     0,     1,
     1,     3,     2,     2,     1,     2,     0,     1,     2,     0,
     1,     5
};

static const short yydefact[] = {     3,
     3,     1,     0,     2,     0,    25,    51,    42,    58,    49,
     0,    28,    48,    29,    44,    45,    26,    61,    43,    46,
    27,     0,     5,     0,    47,    50,    59,     0,     0,     0,
    60,    52,     4,     7,    14,    20,    21,    23,    24,    30,
    31,     8,     9,    10,    11,    12,    39,    35,    33,     0,
    40,    19,    34,    41,     0,     0,   119,    64,     0,    54,
     0,    15,    16,     0,   120,    63,    22,    38,    36,     0,
   109,     0,     0,   105,     6,    14,    37,     0,     0,     0,
     0,    53,    55,    56,    13,     0,    62,   121,    97,   117,
    67,     0,   108,   102,    72,    73,     0,     0,     0,   117,
    71,     0,   110,   111,   115,   101,     0,   106,   120,    32,
     0,    69,    68,    57,    17,   118,    98,     0,    89,     0,
    80,    83,    84,   114,     0,    72,     0,   116,    70,   113,
    76,     0,   107,     0,   122,     0,    18,    99,    66,    90,
    52,     0,    89,    86,    88,    65,    79,     0,    78,    77,
     0,     0,   112,   100,     0,    91,     0,    87,    94,     0,
    81,    85,    75,    74,    96,    95,     0,     0,    93,    92,
     0,     0
};

static const short yydefgoto[] = {     1,
     2,     3,    33,    52,    34,    61,    62,    63,    71,    36,
    37,    38,    39,    40,    64,    82,    83,    41,   109,    66,
   100,   101,   120,   121,   122,   123,   144,   145,    42,   137,
   138,    51,    72,    73,    74,   102,   103,   104,   105,   117,
    43,    90,    44
};

static const short yypact[] = {-32768,
    15,-32768,   197,-32768,    23,-32768,-32768,-32768,-32768,-32768,
   -18,-32768,-32768,-32768,-32768,-32768,-32768,-32768,-32768,-32768,
-32768,   -28,-32768,   -25,-32768,-32768,-32768,   -26,   -22,   -12,
-32768,-32768,-32768,-32768,    49,   493,-32768,-32768,-32768,-32768,
-32768,-32768,-32768,-32768,-32768,-32768,-32768,    27,    -8,   101,
-32768,   493,    -8,-32768,   493,    10,-32768,-32768,    11,     9,
    18,    26,-32768,    49,   -15,   -13,-32768,-32768,-32768,    25,
    24,    48,   149,-32768,-32768,    49,-32768,   414,    39,    40,
    47,-32768,     9,-32768,-32768,    49,-32768,-32768,-32768,    66,
-32768,   241,-32768,-32768,    50,-32768,     5,    65,    42,    66,
    17,    56,    55,-32768,-32768,-32768,    60,-32768,    75,-32768,
    80,-32768,-32768,-32768,-32768,-32768,    81,    82,   370,    85,
    98,    89,-32768,-32768,    88,-32768,    91,-32768,-32768,-32768,
-32768,   284,-32768,    24,-32768,   103,-32768,-32768,-32768,-32768,
-32768,     8,    43,-32768,    30,-32768,-32768,   457,-32768,-32768,
    92,    93,-32768,-32768,    95,-32768,    96,-32768,-32768,   327,
-32768,-32768,-32768,-32768,-32768,-32768,    99,   104,-32768,-32768,
   148,-32768
};

static const short yypgoto[] = {-32768,
   152,-32768,-32768,-32768,   119,-32768,-32768,    94,     0,   -55,
   -35,-32768,-32768,-32768,   -69,-32768,-32768,   -56,   -30,-32768,
   -76,-32768,  -122,-32768,-32768,    29,   -62,-32768,-32768,-32768,
-32768,   -17,-32768,-32768,   105,-32768,-32768,    52,    86,    83,
-32768,-32768,-32768
};


#define	YYLAST		533


static const short yytable[] = {    78,
    67,    99,    35,    84,    65,   125,    54,    49,   155,   152,
    53,    80,    47,    88,   171,    89,     9,    48,    91,    55,
   127,    50,   129,    56,    50,    18,   114,    99,    81,    99,
    57,    69,    92,    87,    27,    77,   119,   168,    31,   -89,
   126,    50,    67,   140,    96,    79,    58,   156,   131,   143,
    97,    76,    60,   142,   -89,    60,    59,    68,    60,    95,
    85,   159,   132,    96,    99,    45,    46,    93,    94,    97,
    86,    60,   143,   143,    98,   160,   119,   126,   140,   157,
   158,    96,   156,    67,    58,   111,   112,    97,   142,    60,
    60,   106,   119,   113,    59,   116,    60,   128,   133,   134,
    98,    70,    93,    88,   119,     6,     7,     8,     9,    10,
    11,    12,    13,    14,    15,    16,    17,    18,    19,    20,
    21,    22,   135,    24,    25,    26,    27,    28,   139,   136,
    31,   146,   147,   148,   149,   154,   -19,   150,   163,   164,
    32,   165,   166,   -19,  -103,   169,   -19,   172,   -19,   107,
   170,   -19,     4,     6,     7,     8,     9,    10,    11,    12,
    13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
    75,    24,    25,    26,    27,    28,   162,   108,    31,   115,
   124,     0,   130,     0,   -19,   153,     0,     0,    32,     0,
     0,   -19,  -104,     0,   -19,     0,   -19,     5,     0,   -19,
     0,     6,     7,     8,     9,    10,    11,    12,    13,    14,
    15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
    25,    26,    27,    28,    29,    30,    31,     0,     0,     0,
     0,     0,   -19,     0,     0,     0,    32,     0,     0,   -19,
     0,   118,   -19,     0,   -19,     6,     7,     8,     9,    10,
    11,    12,    13,    14,    15,    16,    17,    18,    19,    20,
    21,    22,     0,    24,    25,    26,    27,    28,     0,     0,
    31,     0,     0,     0,     0,   -82,     0,     0,     0,     0,
    32,     0,     0,     0,   151,     0,     0,   -82,     6,     7,
     8,     9,    10,    11,    12,    13,    14,    15,    16,    17,
    18,    19,    20,    21,    22,     0,    24,    25,    26,    27,
    28,     0,     0,    31,     0,     0,     0,     0,   -82,     0,
     0,     0,     0,    32,     0,     0,     0,   167,     0,     0,
   -82,     6,     7,     8,     9,    10,    11,    12,    13,    14,
    15,    16,    17,    18,    19,    20,    21,    22,     0,    24,
    25,    26,    27,    28,     0,     0,    31,     0,     0,     0,
     0,   -82,     0,     0,     0,     0,    32,     0,     0,     0,
     0,     0,     0,   -82,     6,     7,     8,     9,    10,    11,
    12,    13,    14,    15,    16,    17,    18,    19,    20,    21,
    22,     0,    24,    25,    26,    27,    28,     0,     0,    31,
     0,     0,     0,     0,     0,   140,     0,     0,     0,   141,
     0,     0,     0,     0,     0,   142,     0,    60,     6,     7,
     8,     9,    10,    11,    12,    13,    14,    15,    16,    17,
    18,    19,    20,    21,    22,     0,    24,    25,    26,    27,
    28,     0,     0,    31,     0,     0,     0,     0,     0,     0,
     0,     0,     0,    32,     0,     0,     0,     0,     0,     0,
   110,     6,     7,     8,     9,    10,    11,    12,    13,    14,
    15,    16,    17,    18,    19,    20,    21,    22,     0,    24,
    25,    26,    27,    28,     0,     0,    31,     0,     0,     0,
     0,   161,     0,     0,     0,     0,    32,     6,     7,     8,
     9,    10,    11,    12,    13,    14,    15,    16,    17,    18,
    19,    20,    21,    22,     0,    24,    25,    26,    27,    28,
     0,     0,    31,     0,     0,     0,     0,     0,     0,     0,
     0,     0,    32
};

static const short yycheck[] = {    55,
    36,    71,     3,    60,    35,     1,    24,    36,     1,   132,
    36,     1,    31,    29,     0,    31,     8,    36,    32,    46,
    97,    50,    99,    46,    50,    17,    83,    97,    59,    99,
    43,    49,    46,    64,    26,    53,    92,   160,    30,    32,
    36,    50,    78,    36,    40,    36,    36,    40,    32,   119,
    46,    52,    48,    46,    47,    48,    46,    31,    48,    36,
    43,    32,    46,    40,   134,    43,    44,    43,    44,    46,
    45,    48,   142,   143,    51,    46,   132,    36,    36,   142,
   143,    40,    40,   119,    36,    47,    47,    46,    46,    48,
    48,    44,   148,    47,    46,    30,    48,    33,    43,    45,
    51,     1,    43,    29,   160,     5,     6,     7,     8,     9,
    10,    11,    12,    13,    14,    15,    16,    17,    18,    19,
    20,    21,    43,    23,    24,    25,    26,    27,    47,    49,
    30,    47,    35,    45,    47,    33,    36,    47,    47,    47,
    40,    47,    47,    43,    44,    47,    46,     0,    48,     1,
    47,    51,     1,     5,     6,     7,     8,     9,    10,    11,
    12,    13,    14,    15,    16,    17,    18,    19,    20,    21,
    52,    23,    24,    25,    26,    27,   148,    73,    30,    86,
    95,    -1,   100,    -1,    36,   134,    -1,    -1,    40,    -1,
    -1,    43,    44,    -1,    46,    -1,    48,     1,    -1,    51,
    -1,     5,     6,     7,     8,     9,    10,    11,    12,    13,
    14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
    24,    25,    26,    27,    28,    29,    30,    -1,    -1,    -1,
    -1,    -1,    36,    -1,    -1,    -1,    40,    -1,    -1,    43,
    -1,     1,    46,    -1,    48,     5,     6,     7,     8,     9,
    10,    11,    12,    13,    14,    15,    16,    17,    18,    19,
    20,    21,    -1,    23,    24,    25,    26,    27,    -1,    -1,
    30,    -1,    -1,    -1,    -1,    35,    -1,    -1,    -1,    -1,
    40,    -1,    -1,    -1,     1,    -1,    -1,    47,     5,     6,
     7,     8,     9,    10,    11,    12,    13,    14,    15,    16,
    17,    18,    19,    20,    21,    -1,    23,    24,    25,    26,
    27,    -1,    -1,    30,    -1,    -1,    -1,    -1,    35,    -1,
    -1,    -1,    -1,    40,    -1,    -1,    -1,     1,    -1,    -1,
    47,     5,     6,     7,     8,     9,    10,    11,    12,    13,
    14,    15,    16,    17,    18,    19,    20,    21,    -1,    23,
    24,    25,    26,    27,    -1,    -1,    30,    -1,    -1,    -1,
    -1,    35,    -1,    -1,    -1,    -1,    40,    -1,    -1,    -1,
    -1,    -1,    -1,    47,     5,     6,     7,     8,     9,    10,
    11,    12,    13,    14,    15,    16,    17,    18,    19,    20,
    21,    -1,    23,    24,    25,    26,    27,    -1,    -1,    30,
    -1,    -1,    -1,    -1,    -1,    36,    -1,    -1,    -1,    40,
    -1,    -1,    -1,    -1,    -1,    46,    -1,    48,     5,     6,
     7,     8,     9,    10,    11,    12,    13,    14,    15,    16,
    17,    18,    19,    20,    21,    -1,    23,    24,    25,    26,
    27,    -1,    -1,    30,    -1,    -1,    -1,    -1,    -1,    -1,
    -1,    -1,    -1,    40,    -1,    -1,    -1,    -1,    -1,    -1,
    47,     5,     6,     7,     8,     9,    10,    11,    12,    13,
    14,    15,    16,    17,    18,    19,    20,    21,    -1,    23,
    24,    25,    26,    27,    -1,    -1,    30,    -1,    -1,    -1,
    -1,    35,    -1,    -1,    -1,    -1,    40,     5,     6,     7,
     8,     9,    10,    11,    12,    13,    14,    15,    16,    17,
    18,    19,    20,    21,    -1,    23,    24,    25,    26,    27,
    -1,    -1,    30,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
    -1,    -1,    40
};
/* -*-C-*-  Note some compilers choke on comments on `#line' lines.  */
#line 3 "/usr/lib/bison.simple"
/* This file comes from bison-1.28.  */

/* Skeleton output parser for bison,
   Copyright (C) 1984, 1989, 1990 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/* As a special exception, when this file is copied by Bison into a
   Bison output file, you may use that output file without restriction.
   This special exception was added by the Free Software Foundation
   in version 1.24 of Bison.  */

/* This is the parser code that is written into each bison parser
  when the %semantic_parser declaration is not specified in the grammar.
  It was written by Richard Stallman by simplifying the hairy parser
  used when %semantic_parser is specified.  */

#ifndef YYSTACK_USE_ALLOCA
#ifdef alloca
#define YYSTACK_USE_ALLOCA
#else /* alloca not defined */
#ifdef __GNUC__
#define YYSTACK_USE_ALLOCA
#define alloca __builtin_alloca
#else /* not GNU C.  */
#if (!defined (__STDC__) && defined (sparc)) || defined (__sparc__) || defined (__sparc) || defined (__sgi) || (defined (__sun) && defined (__i386))
#define YYSTACK_USE_ALLOCA
#include <alloca.h>
#else /* not sparc */
/* We think this test detects Watcom and Microsoft C.  */
/* This used to test MSDOS, but that is a bad idea
   since that symbol is in the user namespace.  */
#if (defined (_MSDOS) || defined (_MSDOS_)) && !defined (__TURBOC__)
#if 0 /* No need for malloc.h, which pollutes the namespace;
	 instead, just don't use alloca.  */
#include <malloc.h>
#endif
#else /* not MSDOS, or __TURBOC__ */
#if defined(_AIX)
/* I don't know what this was needed for, but it pollutes the namespace.
   So I turned it off.   rms, 2 May 1997.  */
/* #include <malloc.h>  */
 #pragma alloca
#define YYSTACK_USE_ALLOCA
#else /* not MSDOS, or __TURBOC__, or _AIX */
#if 0
#ifdef __hpux /* haible@ilog.fr says this works for HPUX 9.05 and up,
		 and on HPUX 10.  Eventually we can turn this on.  */
#define YYSTACK_USE_ALLOCA
#define alloca __builtin_alloca
#endif /* __hpux */
#endif
#endif /* not _AIX */
#endif /* not MSDOS, or __TURBOC__ */
#endif /* not sparc */
#endif /* not GNU C */
#endif /* alloca not defined */
#endif /* YYSTACK_USE_ALLOCA not defined */

#ifdef YYSTACK_USE_ALLOCA
#define YYSTACK_ALLOC alloca
#else
#define YYSTACK_ALLOC malloc
#endif

/* Note: there must be only one dollar sign in this file.
   It is replaced by the list of actions, each action
   as one case of the switch.  */

#define yyerrok		(yyerrstatus = 0)
#define yyclearin	(yychar = YYEMPTY)
#define YYEMPTY		-2
#define YYEOF		0
#define YYACCEPT	goto yyacceptlab
#define YYABORT 	goto yyabortlab
#define YYERROR		goto yyerrlab1
/* Like YYERROR except do call yyerror.
   This remains here temporarily to ease the
   transition to the new meaning of YYERROR, for GCC.
   Once GCC version 2 has supplanted version 1, this can go.  */
#define YYFAIL		goto yyerrlab
#define YYRECOVERING()  (!!yyerrstatus)
#define YYBACKUP(token, value) \
do								\
  if (yychar == YYEMPTY && yylen == 1)				\
    { yychar = (token), yylval = (value);			\
      yychar1 = YYTRANSLATE (yychar);				\
      YYPOPSTACK;						\
      goto yybackup;						\
    }								\
  else								\
    { yyerror ("syntax error: cannot back up"); YYERROR; }	\
while (0)

#define YYTERROR	1
#define YYERRCODE	256

#ifndef YYPURE
#define YYLEX		yylex()
#endif

#ifdef YYPURE
#ifdef YYLSP_NEEDED
#ifdef YYLEX_PARAM
#define YYLEX		yylex(&yylval, &yylloc, YYLEX_PARAM)
#else
#define YYLEX		yylex(&yylval, &yylloc)
#endif
#else /* not YYLSP_NEEDED */
#ifdef YYLEX_PARAM
#define YYLEX		yylex(&yylval, YYLEX_PARAM)
#else
#define YYLEX		yylex(&yylval)
#endif
#endif /* not YYLSP_NEEDED */
#endif

/* If nonreentrant, generate the variables here */

#ifndef YYPURE

int	yychar;			/*  the lookahead symbol		*/
YYSTYPE	yylval;			/*  the semantic value of the		*/
				/*  lookahead symbol			*/

#ifdef YYLSP_NEEDED
YYLTYPE yylloc;			/*  location data for the lookahead	*/
				/*  symbol				*/
#endif

int yynerrs;			/*  number of parse errors so far       */
#endif  /* not YYPURE */

#if YYDEBUG != 0
int yydebug;			/*  nonzero means print parse trace	*/
/* Since this is uninitialized, it does not stop multiple parsers
   from coexisting.  */
#endif

/*  YYINITDEPTH indicates the initial size of the parser's stacks	*/

#ifndef	YYINITDEPTH
#define YYINITDEPTH 200
#endif

/*  YYMAXDEPTH is the maximum size the stacks can grow to
    (effective only if the built-in stack extension method is used).  */

#if YYMAXDEPTH == 0
#undef YYMAXDEPTH
#endif

#ifndef YYMAXDEPTH
#define YYMAXDEPTH 10000
#endif

/* Define __yy_memcpy.  Note that the size argument
   should be passed with type unsigned int, because that is what the non-GCC
   definitions require.  With GCC, __builtin_memcpy takes an arg
   of type size_t, but it can handle unsigned int.  */

#if __GNUC__ > 1		/* GNU C and GNU C++ define this.  */
#define __yy_memcpy(TO,FROM,COUNT)	__builtin_memcpy(TO,FROM,COUNT)
#else				/* not GNU C or C++ */
#ifndef __cplusplus

/* This is the most reliable way to avoid incompatibilities
   in available built-in functions on various systems.  */
static void
__yy_memcpy (to, from, count)
     char *to;
     char *from;
     unsigned int count;
{
  register char *f = from;
  register char *t = to;
  register int i = count;

  while (i-- > 0)
    *t++ = *f++;
}

#else /* __cplusplus */

/* This is the most reliable way to avoid incompatibilities
   in available built-in functions on various systems.  */
static void
__yy_memcpy (char *to, char *from, unsigned int count)
{
  register char *t = to;
  register char *f = from;
  register int i = count;

  while (i-- > 0)
    *t++ = *f++;
}

#endif
#endif

#line 217 "/usr/lib/bison.simple"

/* The user can define YYPARSE_PARAM as the name of an argument to be passed
   into yyparse.  The argument should have type void *.
   It should actually point to an object.
   Grammar actions can access the variable by casting it
   to the proper pointer type.  */

#ifdef YYPARSE_PARAM
#ifdef __cplusplus
#define YYPARSE_PARAM_ARG void *YYPARSE_PARAM
#define YYPARSE_PARAM_DECL
#else /* not __cplusplus */
#define YYPARSE_PARAM_ARG YYPARSE_PARAM
#define YYPARSE_PARAM_DECL void *YYPARSE_PARAM;
#endif /* not __cplusplus */
#else /* not YYPARSE_PARAM */
#define YYPARSE_PARAM_ARG
#define YYPARSE_PARAM_DECL
#endif /* not YYPARSE_PARAM */

/* Prevent warning if -Wstrict-prototypes.  */
#ifdef __GNUC__
#ifdef YYPARSE_PARAM
int yyparse (void *);
#else
int yyparse (void);
#endif
#endif

int
yyparse(YYPARSE_PARAM_ARG)
     YYPARSE_PARAM_DECL
{
  register int yystate;
  register int yyn;
  register short *yyssp;
  register YYSTYPE *yyvsp;
  int yyerrstatus;	/*  number of tokens to shift before error messages enabled */
  int yychar1 = 0;		/*  lookahead token as an internal (translated) token number */

  short	yyssa[YYINITDEPTH];	/*  the state stack			*/
  YYSTYPE yyvsa[YYINITDEPTH];	/*  the semantic value stack		*/

  short *yyss = yyssa;		/*  refer to the stacks thru separate pointers */
  YYSTYPE *yyvs = yyvsa;	/*  to allow yyoverflow to reallocate them elsewhere */

#ifdef YYLSP_NEEDED
  YYLTYPE yylsa[YYINITDEPTH];	/*  the location stack			*/
  YYLTYPE *yyls = yylsa;
  YYLTYPE *yylsp;

#define YYPOPSTACK   (yyvsp--, yyssp--, yylsp--)
#else
#define YYPOPSTACK   (yyvsp--, yyssp--)
#endif

  int yystacksize = YYINITDEPTH;
  int yyfree_stacks = 0;

#ifdef YYPURE
  int yychar;
  YYSTYPE yylval;
  int yynerrs;
#ifdef YYLSP_NEEDED
  YYLTYPE yylloc;
#endif
#endif

  YYSTYPE yyval;		/*  the variable used to return		*/
				/*  semantic values from the action	*/
				/*  routines				*/

  int yylen;

#if YYDEBUG != 0
  if (yydebug)
    fprintf(stderr, "Starting parse\n");
#endif

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY;		/* Cause a token to be read.  */

  /* Initialize stack pointers.
     Waste one element of value and location stack
     so that they stay on the same level as the state stack.
     The wasted elements are never initialized.  */

  yyssp = yyss - 1;
  yyvsp = yyvs;
#ifdef YYLSP_NEEDED
  yylsp = yyls;
#endif

/* Push a new state, which is found in  yystate  .  */
/* In all cases, when you get here, the value and location stacks
   have just been pushed. so pushing a state here evens the stacks.  */
yynewstate:

  *++yyssp = yystate;

  if (yyssp >= yyss + yystacksize - 1)
    {
      /* Give user a chance to reallocate the stack */
      /* Use copies of these so that the &'s don't force the real ones into memory. */
      YYSTYPE *yyvs1 = yyvs;
      short *yyss1 = yyss;
#ifdef YYLSP_NEEDED
      YYLTYPE *yyls1 = yyls;
#endif

      /* Get the current used size of the three stacks, in elements.  */
      int size = yyssp - yyss + 1;

#ifdef yyoverflow
      /* Each stack pointer address is followed by the size of
	 the data in use in that stack, in bytes.  */
#ifdef YYLSP_NEEDED
      /* This used to be a conditional around just the two extra args,
	 but that might be undefined if yyoverflow is a macro.  */
      yyoverflow("parser stack overflow",
		 &yyss1, size * sizeof (*yyssp),
		 &yyvs1, size * sizeof (*yyvsp),
		 &yyls1, size * sizeof (*yylsp),
		 &yystacksize);
#else
      yyoverflow("parser stack overflow",
		 &yyss1, size * sizeof (*yyssp),
		 &yyvs1, size * sizeof (*yyvsp),
		 &yystacksize);
#endif

      yyss = yyss1; yyvs = yyvs1;
#ifdef YYLSP_NEEDED
      yyls = yyls1;
#endif
#else /* no yyoverflow */
      /* Extend the stack our own way.  */
      if (yystacksize >= YYMAXDEPTH)
	{
	  yyerror("parser stack overflow");
	  if (yyfree_stacks)
	    {
	      free (yyss);
	      free (yyvs);
#ifdef YYLSP_NEEDED
	      free (yyls);
#endif
	    }
	  return 2;
	}
      yystacksize *= 2;
      if (yystacksize > YYMAXDEPTH)
	yystacksize = YYMAXDEPTH;
#ifndef YYSTACK_USE_ALLOCA
      yyfree_stacks = 1;
#endif
      yyss = (short *) YYSTACK_ALLOC (yystacksize * sizeof (*yyssp));
      __yy_memcpy ((char *)yyss, (char *)yyss1,
		   size * (unsigned int) sizeof (*yyssp));
      yyvs = (YYSTYPE *) YYSTACK_ALLOC (yystacksize * sizeof (*yyvsp));
      __yy_memcpy ((char *)yyvs, (char *)yyvs1,
		   size * (unsigned int) sizeof (*yyvsp));
#ifdef YYLSP_NEEDED
      yyls = (YYLTYPE *) YYSTACK_ALLOC (yystacksize * sizeof (*yylsp));
      __yy_memcpy ((char *)yyls, (char *)yyls1,
		   size * (unsigned int) sizeof (*yylsp));
#endif
#endif /* no yyoverflow */

      yyssp = yyss + size - 1;
      yyvsp = yyvs + size - 1;
#ifdef YYLSP_NEEDED
      yylsp = yyls + size - 1;
#endif

#if YYDEBUG != 0
      if (yydebug)
	fprintf(stderr, "Stack size increased to %d\n", yystacksize);
#endif

      if (yyssp >= yyss + yystacksize - 1)
	YYABORT;
    }

#if YYDEBUG != 0
  if (yydebug)
    fprintf(stderr, "Entering state %d\n", yystate);
#endif

  goto yybackup;
 yybackup:

/* Do appropriate processing given the current state.  */
/* Read a lookahead token if we need one and don't already have one.  */
/* yyresume: */

  /* First try to decide what to do without reference to lookahead token.  */

  yyn = yypact[yystate];
  if (yyn == YYFLAG)
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* yychar is either YYEMPTY or YYEOF
     or a valid token in external form.  */

  if (yychar == YYEMPTY)
    {
#if YYDEBUG != 0
      if (yydebug)
	fprintf(stderr, "Reading a token: ");
#endif
      yychar = YYLEX;
    }

  /* Convert token to internal form (in yychar1) for indexing tables with */

  if (yychar <= 0)		/* This means end of input. */
    {
      yychar1 = 0;
      yychar = YYEOF;		/* Don't call YYLEX any more */

#if YYDEBUG != 0
      if (yydebug)
	fprintf(stderr, "Now at end of input.\n");
#endif
    }
  else
    {
      yychar1 = YYTRANSLATE(yychar);

#if YYDEBUG != 0
      if (yydebug)
	{
	  fprintf (stderr, "Next token is %d (%s", yychar, yytname[yychar1]);
	  /* Give the individual parser a way to print the precise meaning
	     of a token, for further debugging info.  */
#ifdef YYPRINT
	  YYPRINT (stderr, yychar, yylval);
#endif
	  fprintf (stderr, ")\n");
	}
#endif
    }

  yyn += yychar1;
  if (yyn < 0 || yyn > YYLAST || yycheck[yyn] != yychar1)
    goto yydefault;

  yyn = yytable[yyn];

  /* yyn is what to do for this token type in this state.
     Negative => reduce, -yyn is rule number.
     Positive => shift, yyn is new state.
       New state is final state => don't bother to shift,
       just return success.
     0, or most negative number => error.  */

  if (yyn < 0)
    {
      if (yyn == YYFLAG)
	goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }
  else if (yyn == 0)
    goto yyerrlab;

  if (yyn == YYFINAL)
    YYACCEPT;

  /* Shift the lookahead token.  */

#if YYDEBUG != 0
  if (yydebug)
    fprintf(stderr, "Shifting token %d (%s), ", yychar, yytname[yychar1]);
#endif

  /* Discard the token being shifted unless it is eof.  */
  if (yychar != YYEOF)
    yychar = YYEMPTY;

  *++yyvsp = yylval;
#ifdef YYLSP_NEEDED
  *++yylsp = yylloc;
#endif

  /* count tokens shifted since error; after three, turn off error status.  */
  if (yyerrstatus) yyerrstatus--;

  yystate = yyn;
  goto yynewstate;

/* Do the default action for the current state.  */
yydefault:

  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;

/* Do a reduction.  yyn is the number of a rule to reduce with.  */
yyreduce:
  yylen = yyr2[yyn];
  if (yylen > 0)
    yyval = yyvsp[1-yylen]; /* implement default value of the action */

#if YYDEBUG != 0
  if (yydebug)
    {
      int i;

      fprintf (stderr, "Reducing via rule %d (line %d), ",
	       yyn, yyrline[yyn]);

      /* Print the symbols being reduced, and their result.  */
      for (i = yyprhs[yyn]; yyrhs[i] > 0; i++)
	fprintf (stderr, "%s ", yytname[yyrhs[i]]);
      fprintf (stderr, " -> %s\n", yytname[yyr1[yyn]]);
    }
#endif


  switch (yyn) {

case 3:
#line 107 "scripts/genksyms/parse.y"
{ is_typedef = 0; is_extern = 0; current_name = NULL; decl_spec = NULL; ;
    break;}
case 4:
#line 109 "scripts/genksyms/parse.y"
{ free_list(*yyvsp[0], NULL); *yyvsp[0] = NULL; ;
    break;}
case 5:
#line 113 "scripts/genksyms/parse.y"
{ is_typedef = 1; ;
    break;}
case 6:
#line 114 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 11:
#line 119 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 12:
#line 120 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 13:
#line 125 "scripts/genksyms/parse.y"
{ if (current_name) {
		    struct string_list *decl = (*yyvsp[0])->next;
		    (*yyvsp[0])->next = NULL;
		    add_symbol(current_name,
			       is_typedef ? SYM_TYPEDEF : SYM_NORMAL,
			       decl, is_extern);
		    current_name = NULL;
		  }
		  yyval = yyvsp[0];
		;
    break;}
case 14:
#line 138 "scripts/genksyms/parse.y"
{ yyval = NULL; ;
    break;}
case 16:
#line 144 "scripts/genksyms/parse.y"
{ struct string_list *decl = *yyvsp[0];
		  *yyvsp[0] = NULL;
		  add_symbol(current_name,
			     is_typedef ? SYM_TYPEDEF : SYM_NORMAL, decl, is_extern);
		  current_name = NULL;
		  yyval = yyvsp[0];
		;
    break;}
case 17:
#line 152 "scripts/genksyms/parse.y"
{ struct string_list *decl = *yyvsp[0];
		  *yyvsp[0] = NULL;
		  free_list(*yyvsp[-1], NULL);
		  *yyvsp[-1] = decl_spec;
		  add_symbol(current_name,
			     is_typedef ? SYM_TYPEDEF : SYM_NORMAL, decl, is_extern);
		  current_name = NULL;
		  yyval = yyvsp[0];
		;
    break;}
case 18:
#line 165 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0] ? yyvsp[0] : yyvsp[-1] ? yyvsp[-1] : yyvsp[-2] ? yyvsp[-2] : yyvsp[-3]; ;
    break;}
case 19:
#line 170 "scripts/genksyms/parse.y"
{ decl_spec = NULL; ;
    break;}
case 21:
#line 175 "scripts/genksyms/parse.y"
{ decl_spec = *yyvsp[0]; ;
    break;}
case 22:
#line 176 "scripts/genksyms/parse.y"
{ decl_spec = *yyvsp[0]; ;
    break;}
case 23:
#line 181 "scripts/genksyms/parse.y"
{ /* Version 2 checksumming ignores storage class, as that
		     is really irrelevant to the linkage.  */
		  remove_node(yyvsp[0]);
		  yyval = yyvsp[0];
		;
    break;}
case 28:
#line 193 "scripts/genksyms/parse.y"
{ is_extern = 1; yyval = yyvsp[0]; ;
    break;}
case 29:
#line 194 "scripts/genksyms/parse.y"
{ is_extern = 0; yyval = yyvsp[0]; ;
    break;}
case 33:
#line 205 "scripts/genksyms/parse.y"
{ remove_node(yyvsp[-1]); (*yyvsp[0])->tag = SYM_STRUCT; yyval = yyvsp[0]; ;
    break;}
case 34:
#line 207 "scripts/genksyms/parse.y"
{ remove_node(yyvsp[-1]); (*yyvsp[0])->tag = SYM_UNION; yyval = yyvsp[0]; ;
    break;}
case 35:
#line 209 "scripts/genksyms/parse.y"
{ remove_node(yyvsp[-1]); (*yyvsp[0])->tag = SYM_ENUM; yyval = yyvsp[0]; ;
    break;}
case 36:
#line 213 "scripts/genksyms/parse.y"
{ struct string_list *s = *yyvsp[0], *i = *yyvsp[-1], *r;
		  r = copy_node(i); r->tag = SYM_STRUCT;
		  r->next = (*yyvsp[-2])->next; *yyvsp[0] = r; (*yyvsp[-2])->next = NULL;
		  add_symbol(i->string, SYM_STRUCT, s, is_extern);
		  yyval = yyvsp[0];
		;
    break;}
case 37:
#line 220 "scripts/genksyms/parse.y"
{ struct string_list *s = *yyvsp[0], *i = *yyvsp[-1], *r;
		  r = copy_node(i); r->tag = SYM_UNION;
		  r->next = (*yyvsp[-2])->next; *yyvsp[0] = r; (*yyvsp[-2])->next = NULL;
		  add_symbol(i->string, SYM_UNION, s, is_extern);
		  yyval = yyvsp[0];
		;
    break;}
case 38:
#line 227 "scripts/genksyms/parse.y"
{ struct string_list *s = *yyvsp[0], *i = *yyvsp[-1], *r;
		  r = copy_node(i); r->tag = SYM_ENUM;
		  r->next = (*yyvsp[-2])->next; *yyvsp[0] = r; (*yyvsp[-2])->next = NULL;
		  add_symbol(i->string, SYM_ENUM, s, is_extern);
		  yyval = yyvsp[0];
		;
    break;}
case 39:
#line 235 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 40:
#line 236 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 41:
#line 237 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 52:
#line 251 "scripts/genksyms/parse.y"
{ (*yyvsp[0])->tag = SYM_TYPEDEF; yyval = yyvsp[0]; ;
    break;}
case 53:
#line 256 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0] ? yyvsp[0] : yyvsp[-1]; ;
    break;}
case 54:
#line 260 "scripts/genksyms/parse.y"
{ yyval = NULL; ;
    break;}
case 57:
#line 266 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 61:
#line 272 "scripts/genksyms/parse.y"
{ /* restrict has no effect in prototypes so ignore it */
		  remove_node(yyvsp[0]);
		  yyval = yyvsp[0];
		;
    break;}
case 62:
#line 279 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 64:
#line 285 "scripts/genksyms/parse.y"
{ if (current_name != NULL) {
		    error_with_pos("unexpected second declaration name");
		    YYERROR;
		  } else {
		    current_name = (*yyvsp[0])->string;
		    yyval = yyvsp[0];
		  }
		;
    break;}
case 65:
#line 294 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 66:
#line 296 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 67:
#line 298 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 68:
#line 300 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 69:
#line 302 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 70:
#line 308 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 74:
#line 316 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 75:
#line 318 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 76:
#line 320 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 77:
#line 322 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 78:
#line 324 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 79:
#line 328 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 81:
#line 330 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 82:
#line 334 "scripts/genksyms/parse.y"
{ yyval = NULL; ;
    break;}
case 85:
#line 341 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 86:
#line 346 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0] ? yyvsp[0] : yyvsp[-1]; ;
    break;}
case 87:
#line 351 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0] ? yyvsp[0] : yyvsp[-1]; ;
    break;}
case 89:
#line 356 "scripts/genksyms/parse.y"
{ yyval = NULL; ;
    break;}
case 90:
#line 358 "scripts/genksyms/parse.y"
{ /* For version 2 checksums, we don't want to remember
		     private parameter names.  */
		  remove_node(yyvsp[0]);
		  yyval = yyvsp[0];
		;
    break;}
case 91:
#line 366 "scripts/genksyms/parse.y"
{ remove_node(yyvsp[0]);
		  yyval = yyvsp[0];
		;
    break;}
case 92:
#line 370 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 93:
#line 372 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 94:
#line 374 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 95:
#line 376 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 96:
#line 378 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 97:
#line 383 "scripts/genksyms/parse.y"
{ struct string_list *decl = *yyvsp[-1];
		  *yyvsp[-1] = NULL;
		  add_symbol(current_name, SYM_NORMAL, decl, is_extern);
		  yyval = yyvsp[0];
		;
    break;}
case 98:
#line 391 "scripts/genksyms/parse.y"
{ yyval = NULL; ;
    break;}
case 100:
#line 398 "scripts/genksyms/parse.y"
{ remove_list(yyvsp[0], &(*yyvsp[-1])->next); yyval = yyvsp[0]; ;
    break;}
case 101:
#line 402 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 102:
#line 403 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 103:
#line 407 "scripts/genksyms/parse.y"
{ yyval = NULL; ;
    break;}
case 106:
#line 413 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 107:
#line 418 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 108:
#line 420 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 109:
#line 424 "scripts/genksyms/parse.y"
{ yyval = NULL; ;
    break;}
case 112:
#line 430 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 113:
#line 434 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0] ? yyvsp[0] : yyvsp[-1]; ;
    break;}
case 114:
#line 435 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 116:
#line 440 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 117:
#line 444 "scripts/genksyms/parse.y"
{ yyval = NULL; ;
    break;}
case 119:
#line 449 "scripts/genksyms/parse.y"
{ yyval = yyvsp[0]; ;
    break;}
case 120:
#line 453 "scripts/genksyms/parse.y"
{ yyval = NULL; ;
    break;}
case 122:
#line 459 "scripts/genksyms/parse.y"
{ export_symbol((*yyvsp[-2])->string); yyval = yyvsp[0]; ;
    break;}
}
   /* the action file gets copied in in place of this dollarsign */
#line 543 "/usr/lib/bison.simple"

  yyvsp -= yylen;
  yyssp -= yylen;
#ifdef YYLSP_NEEDED
  yylsp -= yylen;
#endif

#if YYDEBUG != 0
  if (yydebug)
    {
      short *ssp1 = yyss - 1;
      fprintf (stderr, "state stack now");
      while (ssp1 != yyssp)
	fprintf (stderr, " %d", *++ssp1);
      fprintf (stderr, "\n");
    }
#endif

  *++yyvsp = yyval;

#ifdef YYLSP_NEEDED
  yylsp++;
  if (yylen == 0)
    {
      yylsp->first_line = yylloc.first_line;
      yylsp->first_column = yylloc.first_column;
      yylsp->last_line = (yylsp-1)->last_line;
      yylsp->last_column = (yylsp-1)->last_column;
      yylsp->text = 0;
    }
  else
    {
      yylsp->last_line = (yylsp+yylen-1)->last_line;
      yylsp->last_column = (yylsp+yylen-1)->last_column;
    }
#endif

  /* Now "shift" the result of the reduction.
     Determine what state that goes to,
     based on the state we popped back to
     and the rule number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTBASE] + *yyssp;
  if (yystate >= 0 && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTBASE];

  goto yynewstate;

yyerrlab:   /* here on detecting error */

  if (! yyerrstatus)
    /* If not already recovering from an error, report this error.  */
    {
      ++yynerrs;

#ifdef YYERROR_VERBOSE
      yyn = yypact[yystate];

      if (yyn > YYFLAG && yyn < YYLAST)
	{
	  int size = 0;
	  char *msg;
	  int x, count;

	  count = 0;
	  /* Start X at -yyn if nec to avoid negative indexes in yycheck.  */
	  for (x = (yyn < 0 ? -yyn : 0);
	       x < (sizeof(yytname) / sizeof(char *)); x++)
	    if (yycheck[x + yyn] == x)
	      size += strlen(yytname[x]) + 15, count++;
	  msg = (char *) malloc(size + 15);
	  if (msg != 0)
	    {
	      strcpy(msg, "parse error");

	      if (count < 5)
		{
		  count = 0;
		  for (x = (yyn < 0 ? -yyn : 0);
		       x < (sizeof(yytname) / sizeof(char *)); x++)
		    if (yycheck[x + yyn] == x)
		      {
			strcat(msg, count == 0 ? ", expecting `" : " or `");
			strcat(msg, yytname[x]);
			strcat(msg, "'");
			count++;
		      }
		}
	      yyerror(msg);
	      free(msg);
	    }
	  else
	    yyerror ("parse error; also virtual memory exceeded");
	}
      else
#endif /* YYERROR_VERBOSE */
	yyerror("parse error");
    }

  goto yyerrlab1;
yyerrlab1:   /* here on error raised explicitly by an action */

  if (yyerrstatus == 3)
    {
      /* if just tried and failed to reuse lookahead token after an error, discard it.  */

      /* return failure if at end of input */
      if (yychar == YYEOF)
	YYABORT;

#if YYDEBUG != 0
      if (yydebug)
	fprintf(stderr, "Discarding token %d (%s).\n", yychar, yytname[yychar1]);
#endif

      yychar = YYEMPTY;
    }

  /* Else will try to reuse lookahead token
     after shifting the error token.  */

  yyerrstatus = 3;		/* Each real token shifted decrements this */

  goto yyerrhandle;

yyerrdefault:  /* current state does not do anything special for the error token. */

#if 0
  /* This is wrong; only states that explicitly want error tokens
     should shift them.  */
  yyn = yydefact[yystate];  /* If its default is to accept any token, ok.  Otherwise pop it.*/
  if (yyn) goto yydefault;
#endif

yyerrpop:   /* pop the current state because it cannot handle the error token */

  if (yyssp == yyss) YYABORT;
  yyvsp--;
  yystate = *--yyssp;
#ifdef YYLSP_NEEDED
  yylsp--;
#endif

#if YYDEBUG != 0
  if (yydebug)
    {
      short *ssp1 = yyss - 1;
      fprintf (stderr, "Error: state stack now");
      while (ssp1 != yyssp)
	fprintf (stderr, " %d", *++ssp1);
      fprintf (stderr, "\n");
    }
#endif

yyerrhandle:

  yyn = yypact[yystate];
  if (yyn == YYFLAG)
    goto yyerrdefault;

  yyn += YYTERROR;
  if (yyn < 0 || yyn > YYLAST || yycheck[yyn] != YYTERROR)
    goto yyerrdefault;

  yyn = yytable[yyn];
  if (yyn < 0)
    {
      if (yyn == YYFLAG)
	goto yyerrpop;
      yyn = -yyn;
      goto yyreduce;
    }
  else if (yyn == 0)
    goto yyerrpop;

  if (yyn == YYFINAL)
    YYACCEPT;

#if YYDEBUG != 0
  if (yydebug)
    fprintf(stderr, "Shifting error token, ");
#endif

  *++yyvsp = yylval;
#ifdef YYLSP_NEEDED
  *++yylsp = yylloc;
#endif

  yystate = yyn;
  goto yynewstate;

 yyacceptlab:
  /* YYACCEPT comes here.  */
  if (yyfree_stacks)
    {
      free (yyss);
      free (yyvs);
#ifdef YYLSP_NEEDED
      free (yyls);
#endif
    }
  return 0;

 yyabortlab:
  /* YYABORT comes here.  */
  if (yyfree_stacks)
    {
      free (yyss);
      free (yyvs);
#ifdef YYLSP_NEEDED
      free (yyls);
#endif
    }
  return 1;
}
#line 463 "scripts/genksyms/parse.y"


static void
yyerror(const char *e)
{
  error_with_pos("%s", e);
}
