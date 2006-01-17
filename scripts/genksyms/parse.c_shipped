/* A Bison parser, made by GNU Bison 2.0.  */

/* Skeleton parser for Yacc-like parsing with Bison,
   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004 Free Software Foundation, Inc.

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

/* Written by Richard Stallman by simplifying the original so called
   ``semantic'' parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Using locations.  */
#define YYLSP_NEEDED 0



/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     ASM_KEYW = 258,
     ATTRIBUTE_KEYW = 259,
     AUTO_KEYW = 260,
     BOOL_KEYW = 261,
     CHAR_KEYW = 262,
     CONST_KEYW = 263,
     DOUBLE_KEYW = 264,
     ENUM_KEYW = 265,
     EXTERN_KEYW = 266,
     FLOAT_KEYW = 267,
     INLINE_KEYW = 268,
     INT_KEYW = 269,
     LONG_KEYW = 270,
     REGISTER_KEYW = 271,
     RESTRICT_KEYW = 272,
     SHORT_KEYW = 273,
     SIGNED_KEYW = 274,
     STATIC_KEYW = 275,
     STRUCT_KEYW = 276,
     TYPEDEF_KEYW = 277,
     UNION_KEYW = 278,
     UNSIGNED_KEYW = 279,
     VOID_KEYW = 280,
     VOLATILE_KEYW = 281,
     TYPEOF_KEYW = 282,
     EXPORT_SYMBOL_KEYW = 283,
     ASM_PHRASE = 284,
     ATTRIBUTE_PHRASE = 285,
     BRACE_PHRASE = 286,
     BRACKET_PHRASE = 287,
     EXPRESSION_PHRASE = 288,
     CHAR = 289,
     DOTS = 290,
     IDENT = 291,
     INT = 292,
     REAL = 293,
     STRING = 294,
     TYPE = 295,
     OTHER = 296,
     FILENAME = 297
   };
#endif
#define ASM_KEYW 258
#define ATTRIBUTE_KEYW 259
#define AUTO_KEYW 260
#define BOOL_KEYW 261
#define CHAR_KEYW 262
#define CONST_KEYW 263
#define DOUBLE_KEYW 264
#define ENUM_KEYW 265
#define EXTERN_KEYW 266
#define FLOAT_KEYW 267
#define INLINE_KEYW 268
#define INT_KEYW 269
#define LONG_KEYW 270
#define REGISTER_KEYW 271
#define RESTRICT_KEYW 272
#define SHORT_KEYW 273
#define SIGNED_KEYW 274
#define STATIC_KEYW 275
#define STRUCT_KEYW 276
#define TYPEDEF_KEYW 277
#define UNION_KEYW 278
#define UNSIGNED_KEYW 279
#define VOID_KEYW 280
#define VOLATILE_KEYW 281
#define TYPEOF_KEYW 282
#define EXPORT_SYMBOL_KEYW 283
#define ASM_PHRASE 284
#define ATTRIBUTE_PHRASE 285
#define BRACE_PHRASE 286
#define BRACKET_PHRASE 287
#define EXPRESSION_PHRASE 288
#define CHAR 289
#define DOTS 290
#define IDENT 291
#define INT 292
#define REAL 293
#define STRING 294
#define TYPE 295
#define OTHER 296
#define FILENAME 297




/* Copy the first part of user declarations.  */
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



/* Enabling traces.  */
#ifndef YYDEBUG
# define YYDEBUG 1
#endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 0
#endif

#if ! defined (YYSTYPE) && ! defined (YYSTYPE_IS_DECLARED)
typedef int YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif



/* Copy the second part of user declarations.  */


/* Line 213 of yacc.c.  */
#line 202 "scripts/genksyms/parse.c"

#if ! defined (yyoverflow) || YYERROR_VERBOSE

# ifndef YYFREE
#  define YYFREE free
# endif
# ifndef YYMALLOC
#  define YYMALLOC malloc
# endif

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   else
#    define YYSTACK_ALLOC alloca
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's `empty if-body' warning. */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
# else
#  if defined (__STDC__) || defined (__cplusplus)
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   define YYSIZE_T size_t
#  endif
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
# endif
#endif /* ! defined (yyoverflow) || YYERROR_VERBOSE */


#if (! defined (yyoverflow) \
     && (! defined (__cplusplus) \
	 || (defined (YYSTYPE_IS_TRIVIAL) && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  short int yyss;
  YYSTYPE yyvs;
  };

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (short int) + sizeof (YYSTYPE))			\
      + YYSTACK_GAP_MAXIMUM)

/* Copy COUNT objects from FROM to TO.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined (__GNUC__) && 1 < __GNUC__
#   define YYCOPY(To, From, Count) \
      __builtin_memcpy (To, From, (Count) * sizeof (*(From)))
#  else
#   define YYCOPY(To, From, Count)		\
      do					\
	{					\
	  register YYSIZE_T yyi;		\
	  for (yyi = 0; yyi < (Count); yyi++)	\
	    (To)[yyi] = (From)[yyi];		\
	}					\
      while (0)
#  endif
# endif

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack)					\
    do									\
      {									\
	YYSIZE_T yynewbytes;						\
	YYCOPY (&yyptr->Stack, Stack, yysize);				\
	Stack = &yyptr->Stack;						\
	yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
	yyptr += yynewbytes / sizeof (*yyptr);				\
      }									\
    while (0)

#endif

#if defined (__STDC__) || defined (__cplusplus)
   typedef signed char yysigned_char;
#else
   typedef short int yysigned_char;
#endif

/* YYFINAL -- State number of the termination state. */
#define YYFINAL  4
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   535

/* YYNTOKENS -- Number of terminals. */
#define YYNTOKENS  52
/* YYNNTS -- Number of nonterminals. */
#define YYNNTS  45
/* YYNRULES -- Number of rules. */
#define YYNRULES  124
/* YYNRULES -- Number of states. */
#define YYNSTATES  174

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   297

#define YYTRANSLATE(YYX) 						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const unsigned char yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      46,    48,    47,     2,    45,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    51,    43,
       2,    49,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    50,     2,    44,     2,     2,     2,     2,
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
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const unsigned short int yyprhs[] =
{
       0,     0,     3,     5,     8,     9,    12,    13,    17,    19,
      21,    23,    25,    28,    31,    35,    36,    38,    40,    44,
      49,    50,    52,    54,    57,    59,    61,    63,    65,    67,
      69,    71,    73,    75,    81,    86,    89,    92,    95,    99,
     103,   107,   110,   113,   116,   118,   120,   122,   124,   126,
     128,   130,   132,   134,   136,   138,   141,   142,   144,   146,
     149,   151,   153,   155,   157,   160,   162,   164,   169,   174,
     177,   181,   185,   188,   190,   192,   194,   199,   204,   207,
     211,   215,   218,   220,   224,   225,   227,   229,   233,   236,
     239,   241,   242,   244,   246,   251,   256,   259,   263,   267,
     271,   272,   274,   277,   281,   285,   286,   288,   290,   293,
     297,   300,   301,   303,   305,   309,   312,   315,   317,   320,
     321,   323,   326,   327,   329
};

/* YYRHS -- A `-1'-separated list of the rules' RHS. */
static const yysigned_char yyrhs[] =
{
      53,     0,    -1,    54,    -1,    53,    54,    -1,    -1,    55,
      56,    -1,    -1,    22,    57,    58,    -1,    58,    -1,    82,
      -1,    94,    -1,    96,    -1,     1,    43,    -1,     1,    44,
      -1,    62,    59,    43,    -1,    -1,    60,    -1,    61,    -1,
      60,    45,    61,    -1,    72,    95,    93,    83,    -1,    -1,
      63,    -1,    64,    -1,    63,    64,    -1,    65,    -1,    66,
      -1,     5,    -1,    16,    -1,    20,    -1,    11,    -1,    13,
      -1,    67,    -1,    71,    -1,    27,    46,    63,    47,    48,
      -1,    27,    46,    63,    48,    -1,    21,    36,    -1,    23,
      36,    -1,    10,    36,    -1,    21,    36,    85,    -1,    23,
      36,    85,    -1,    10,    36,    31,    -1,    10,    31,    -1,
      21,    85,    -1,    23,    85,    -1,     7,    -1,    18,    -1,
      14,    -1,    15,    -1,    19,    -1,    24,    -1,    12,    -1,
       9,    -1,    25,    -1,     6,    -1,    40,    -1,    47,    69,
      -1,    -1,    70,    -1,    71,    -1,    70,    71,    -1,     8,
      -1,    26,    -1,    30,    -1,    17,    -1,    68,    72,    -1,
      73,    -1,    36,    -1,    73,    46,    76,    48,    -1,    73,
      46,     1,    48,    -1,    73,    32,    -1,    46,    72,    48,
      -1,    46,     1,    48,    -1,    68,    74,    -1,    75,    -1,
      36,    -1,    40,    -1,    75,    46,    76,    48,    -1,    75,
      46,     1,    48,    -1,    75,    32,    -1,    46,    74,    48,
      -1,    46,     1,    48,    -1,    77,    35,    -1,    77,    -1,
      78,    45,    35,    -1,    -1,    78,    -1,    79,    -1,    78,
      45,    79,    -1,    63,    80,    -1,    68,    80,    -1,    81,
      -1,    -1,    36,    -1,    40,    -1,    81,    46,    76,    48,
      -1,    81,    46,     1,    48,    -1,    81,    32,    -1,    46,
      80,    48,    -1,    46,     1,    48,    -1,    62,    72,    31,
      -1,    -1,    84,    -1,    49,    33,    -1,    50,    86,    44,
      -1,    50,     1,    44,    -1,    -1,    87,    -1,    88,    -1,
      87,    88,    -1,    62,    89,    43,    -1,     1,    43,    -1,
      -1,    90,    -1,    91,    -1,    90,    45,    91,    -1,    74,
      93,    -1,    36,    92,    -1,    92,    -1,    51,    33,    -1,
      -1,    30,    -1,    29,    43,    -1,    -1,    29,    -1,    28,
      46,    36,    48,    43,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const unsigned short int yyrline[] =
{
       0,   102,   102,   103,   107,   107,   113,   113,   115,   116,
     117,   118,   119,   120,   124,   138,   139,   143,   151,   164,
     170,   171,   175,   176,   180,   186,   190,   191,   192,   193,
     194,   198,   199,   200,   201,   205,   207,   209,   213,   220,
     227,   236,   237,   238,   242,   243,   244,   245,   246,   247,
     248,   249,   250,   251,   252,   256,   261,   262,   266,   267,
     271,   271,   271,   272,   280,   281,   285,   294,   296,   298,
     300,   302,   309,   310,   314,   315,   316,   318,   320,   322,
     324,   329,   330,   331,   335,   336,   340,   341,   346,   351,
     353,   357,   358,   366,   370,   372,   374,   376,   378,   383,
     392,   393,   398,   403,   404,   408,   409,   413,   414,   418,
     420,   425,   426,   430,   431,   435,   436,   437,   441,   445,
     446,   450,   454,   455,   459
};
#endif

#if YYDEBUG || YYERROR_VERBOSE
/* YYTNME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals. */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "ASM_KEYW", "ATTRIBUTE_KEYW",
  "AUTO_KEYW", "BOOL_KEYW", "CHAR_KEYW", "CONST_KEYW", "DOUBLE_KEYW",
  "ENUM_KEYW", "EXTERN_KEYW", "FLOAT_KEYW", "INLINE_KEYW", "INT_KEYW",
  "LONG_KEYW", "REGISTER_KEYW", "RESTRICT_KEYW", "SHORT_KEYW",
  "SIGNED_KEYW", "STATIC_KEYW", "STRUCT_KEYW", "TYPEDEF_KEYW",
  "UNION_KEYW", "UNSIGNED_KEYW", "VOID_KEYW", "VOLATILE_KEYW",
  "TYPEOF_KEYW", "EXPORT_SYMBOL_KEYW", "ASM_PHRASE", "ATTRIBUTE_PHRASE",
  "BRACE_PHRASE", "BRACKET_PHRASE", "EXPRESSION_PHRASE", "CHAR", "DOTS",
  "IDENT", "INT", "REAL", "STRING", "TYPE", "OTHER", "FILENAME", "';'",
  "'}'", "','", "'('", "'*'", "')'", "'='", "'{'", "':'", "$accept",
  "declaration_seq", "declaration", "@1", "declaration1", "@2",
  "simple_declaration", "init_declarator_list_opt", "init_declarator_list",
  "init_declarator", "decl_specifier_seq_opt", "decl_specifier_seq",
  "decl_specifier", "storage_class_specifier", "type_specifier",
  "simple_type_specifier", "ptr_operator", "cvar_qualifier_seq_opt",
  "cvar_qualifier_seq", "cvar_qualifier", "declarator",
  "direct_declarator", "nested_declarator", "direct_nested_declarator",
  "parameter_declaration_clause", "parameter_declaration_list_opt",
  "parameter_declaration_list", "parameter_declaration",
  "m_abstract_declarator", "direct_m_abstract_declarator",
  "function_definition", "initializer_opt", "initializer", "class_body",
  "member_specification_opt", "member_specification", "member_declaration",
  "member_declarator_list_opt", "member_declarator_list",
  "member_declarator", "member_bitfield_declarator", "attribute_opt",
  "asm_definition", "asm_phrase_opt", "export_definition", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const unsigned short int yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,    59,   125,    44,    40,    42,    41,    61,
     123,    58
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const unsigned char yyr1[] =
{
       0,    52,    53,    53,    55,    54,    57,    56,    56,    56,
      56,    56,    56,    56,    58,    59,    59,    60,    60,    61,
      62,    62,    63,    63,    64,    64,    65,    65,    65,    65,
      65,    66,    66,    66,    66,    66,    66,    66,    66,    66,
      66,    66,    66,    66,    67,    67,    67,    67,    67,    67,
      67,    67,    67,    67,    67,    68,    69,    69,    70,    70,
      71,    71,    71,    71,    72,    72,    73,    73,    73,    73,
      73,    73,    74,    74,    75,    75,    75,    75,    75,    75,
      75,    76,    76,    76,    77,    77,    78,    78,    79,    80,
      80,    81,    81,    81,    81,    81,    81,    81,    81,    82,
      83,    83,    84,    85,    85,    86,    86,    87,    87,    88,
      88,    89,    89,    90,    90,    91,    91,    91,    92,    93,
      93,    94,    95,    95,    96
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const unsigned char yyr2[] =
{
       0,     2,     1,     2,     0,     2,     0,     3,     1,     1,
       1,     1,     2,     2,     3,     0,     1,     1,     3,     4,
       0,     1,     1,     2,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     5,     4,     2,     2,     2,     3,     3,
       3,     2,     2,     2,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     2,     0,     1,     1,     2,
       1,     1,     1,     1,     2,     1,     1,     4,     4,     2,
       3,     3,     2,     1,     1,     1,     4,     4,     2,     3,
       3,     2,     1,     3,     0,     1,     1,     3,     2,     2,
       1,     0,     1,     1,     4,     4,     2,     3,     3,     3,
       0,     1,     2,     3,     3,     0,     1,     1,     2,     3,
       2,     0,     1,     1,     3,     2,     2,     1,     2,     0,
       1,     2,     0,     1,     5
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const unsigned char yydefact[] =
{
       4,     4,     2,     0,     1,     3,     0,    26,    53,    44,
      60,    51,     0,    29,    50,    30,    46,    47,    27,    63,
      45,    48,    28,     0,     6,     0,    49,    52,    61,     0,
       0,     0,    62,    54,     5,     8,    15,    21,    22,    24,
      25,    31,    32,     9,    10,    11,    12,    13,    41,    37,
      35,     0,    42,    20,    36,    43,     0,     0,   121,    66,
       0,    56,     0,    16,    17,     0,   122,    65,    23,    40,
      38,     0,   111,     0,     0,   107,     7,    15,    39,     0,
       0,     0,     0,    55,    57,    58,    14,     0,    64,   123,
      99,   119,    69,     0,   110,   104,    74,    75,     0,     0,
       0,   119,    73,     0,   112,   113,   117,   103,     0,   108,
     122,     0,    34,     0,    71,    70,    59,    18,   120,   100,
       0,    91,     0,    82,    85,    86,   116,     0,    74,     0,
     118,    72,   115,    78,     0,   109,     0,    33,   124,     0,
      19,   101,    68,    92,    54,     0,    91,    88,    90,    67,
      81,     0,    80,    79,     0,     0,   114,   102,     0,    93,
       0,    89,    96,     0,    83,    87,    77,    76,    98,    97,
       0,     0,    95,    94
};

/* YYDEFGOTO[NTERM-NUM]. */
static const short int yydefgoto[] =
{
      -1,     1,     2,     3,    34,    53,    35,    62,    63,    64,
      72,    37,    38,    39,    40,    41,    65,    83,    84,    42,
     110,    67,   101,   102,   122,   123,   124,   125,   147,   148,
      43,   140,   141,    52,    73,    74,    75,   103,   104,   105,
     106,   119,    44,    91,    45
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -128
static const short int yypact[] =
{
    -128,    13,  -128,   329,  -128,  -128,    36,  -128,  -128,  -128,
    -128,  -128,   -16,  -128,  -128,  -128,  -128,  -128,  -128,  -128,
    -128,  -128,  -128,   -25,  -128,   -24,  -128,  -128,  -128,   -29,
      -4,   -22,  -128,  -128,  -128,  -128,   -28,   495,  -128,  -128,
    -128,  -128,  -128,  -128,  -128,  -128,  -128,  -128,  -128,    16,
     -23,   103,  -128,   495,   -23,  -128,   495,    35,  -128,  -128,
       3,    15,     9,    17,  -128,   -28,   -15,    -8,  -128,  -128,
    -128,    47,    23,    44,   150,  -128,  -128,   -28,  -128,   372,
      33,    48,    49,  -128,    15,  -128,  -128,   -28,  -128,  -128,
    -128,    64,  -128,   197,  -128,  -128,    50,  -128,    21,    65,
      37,    64,    14,    56,    55,  -128,  -128,  -128,    59,  -128,
      74,    57,  -128,    63,  -128,  -128,  -128,  -128,  -128,    76,
      83,   416,    84,    99,    90,  -128,  -128,    88,  -128,    89,
    -128,  -128,  -128,  -128,   241,  -128,    23,  -128,  -128,   105,
    -128,  -128,  -128,  -128,  -128,     8,    46,  -128,    26,  -128,
    -128,   459,  -128,  -128,    92,    93,  -128,  -128,    94,  -128,
      96,  -128,  -128,   285,  -128,  -128,  -128,  -128,  -128,  -128,
      97,   100,  -128,  -128
};

/* YYPGOTO[NTERM-NUM].  */
static const short int yypgoto[] =
{
    -128,  -128,   151,  -128,  -128,  -128,   119,  -128,  -128,    66,
       0,   -56,   -36,  -128,  -128,  -128,   -70,  -128,  -128,   -51,
     -31,  -128,   -11,  -128,  -127,  -128,  -128,    27,   -81,  -128,
    -128,  -128,  -128,   -19,  -128,  -128,   107,  -128,  -128,    43,
      86,    82,  -128,  -128,  -128
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -107
static const short int yytable[] =
{
      79,    68,   100,    36,    81,    66,    55,   155,    59,   158,
      85,    50,    54,     4,    89,    48,    90,    56,    60,    61,
      49,    58,   127,    10,    92,    51,    51,    51,   100,    82,
     100,    70,    19,   116,    88,    78,   171,   121,    93,    59,
     -91,    28,    57,    68,   143,    32,   133,    69,   159,    60,
      61,   146,    86,    77,   145,    61,   -91,   128,   162,    96,
     134,    97,    87,    97,   160,   161,   100,    98,    61,    98,
      61,    80,   163,   128,    99,   146,   146,    97,   121,    46,
      47,   113,   143,    98,    61,    68,   159,   129,   107,   131,
      94,    95,   145,    61,   118,   121,   114,   115,   130,   135,
     136,    99,    94,    89,    71,   137,   138,   121,     7,     8,
       9,    10,    11,    12,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,   139,    25,    26,    27,    28,
      29,   142,   149,    32,   150,   151,   152,   153,   157,   -20,
     166,   167,   168,    33,   169,   172,   -20,  -105,   173,   -20,
     -20,   108,     5,   117,   -20,     7,     8,     9,    10,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    76,    25,    26,    27,    28,    29,   165,   156,
      32,   109,   126,   132,     0,     0,   -20,     0,     0,     0,
      33,     0,     0,   -20,  -106,     0,   -20,   -20,   120,     0,
       0,   -20,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,     0,
      25,    26,    27,    28,    29,     0,     0,    32,     0,     0,
       0,     0,   -84,     0,     0,     0,     0,    33,     0,     0,
       0,     0,   154,     0,     0,   -84,     7,     8,     9,    10,
      11,    12,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,     0,    25,    26,    27,    28,    29,     0,
       0,    32,     0,     0,     0,     0,   -84,     0,     0,     0,
       0,    33,     0,     0,     0,     0,   170,     0,     0,   -84,
       7,     8,     9,    10,    11,    12,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,     0,    25,    26,
      27,    28,    29,     0,     0,    32,     0,     0,     0,     0,
     -84,     0,     0,     0,     0,    33,     0,     0,     0,     0,
       6,     0,     0,   -84,     7,     8,     9,    10,    11,    12,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    32,
       0,     0,     0,     0,     0,   -20,     0,     0,     0,    33,
       0,     0,   -20,     0,     0,   -20,   -20,     7,     8,     9,
      10,    11,    12,    13,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,     0,    25,    26,    27,    28,    29,
       0,     0,    32,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    33,     0,     0,     0,     0,     0,     0,   111,
     112,     7,     8,     9,    10,    11,    12,    13,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,     0,    25,
      26,    27,    28,    29,     0,     0,    32,     0,     0,     0,
       0,     0,   143,     0,     0,     0,   144,     0,     0,     0,
       0,     0,   145,    61,     7,     8,     9,    10,    11,    12,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,     0,    25,    26,    27,    28,    29,     0,     0,    32,
       0,     0,     0,     0,   164,     0,     0,     0,     0,    33,
       7,     8,     9,    10,    11,    12,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,     0,    25,    26,
      27,    28,    29,     0,     0,    32,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    33
};

static const short int yycheck[] =
{
      56,    37,    72,     3,     1,    36,    25,   134,    36,     1,
      61,    36,    36,     0,    29,    31,    31,    46,    46,    47,
      36,    43,     1,     8,    32,    50,    50,    50,    98,    60,
     100,    50,    17,    84,    65,    54,   163,    93,    46,    36,
      32,    26,    46,    79,    36,    30,    32,    31,    40,    46,
      47,   121,    43,    53,    46,    47,    48,    36,    32,    36,
      46,    40,    45,    40,   145,   146,   136,    46,    47,    46,
      47,    36,    46,    36,    51,   145,   146,    40,   134,    43,
      44,    48,    36,    46,    47,   121,    40,    98,    44,   100,
      43,    44,    46,    47,    30,   151,    48,    48,    33,    43,
      45,    51,    43,    29,     1,    48,    43,   163,     5,     6,
       7,     8,     9,    10,    11,    12,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    49,    23,    24,    25,    26,
      27,    48,    48,    30,    35,    45,    48,    48,    33,    36,
      48,    48,    48,    40,    48,    48,    43,    44,    48,    46,
      47,     1,     1,    87,    51,     5,     6,     7,     8,     9,
      10,    11,    12,    13,    14,    15,    16,    17,    18,    19,
      20,    21,    53,    23,    24,    25,    26,    27,   151,   136,
      30,    74,    96,   101,    -1,    -1,    36,    -1,    -1,    -1,
      40,    -1,    -1,    43,    44,    -1,    46,    47,     1,    -1,
      -1,    51,     5,     6,     7,     8,     9,    10,    11,    12,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    -1,
      23,    24,    25,    26,    27,    -1,    -1,    30,    -1,    -1,
      -1,    -1,    35,    -1,    -1,    -1,    -1,    40,    -1,    -1,
      -1,    -1,     1,    -1,    -1,    48,     5,     6,     7,     8,
       9,    10,    11,    12,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    -1,    23,    24,    25,    26,    27,    -1,
      -1,    30,    -1,    -1,    -1,    -1,    35,    -1,    -1,    -1,
      -1,    40,    -1,    -1,    -1,    -1,     1,    -1,    -1,    48,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    -1,    23,    24,
      25,    26,    27,    -1,    -1,    30,    -1,    -1,    -1,    -1,
      35,    -1,    -1,    -1,    -1,    40,    -1,    -1,    -1,    -1,
       1,    -1,    -1,    48,     5,     6,     7,     8,     9,    10,
      11,    12,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,    29,    30,
      -1,    -1,    -1,    -1,    -1,    36,    -1,    -1,    -1,    40,
      -1,    -1,    43,    -1,    -1,    46,    47,     5,     6,     7,
       8,     9,    10,    11,    12,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    -1,    23,    24,    25,    26,    27,
      -1,    -1,    30,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    40,    -1,    -1,    -1,    -1,    -1,    -1,    47,
      48,     5,     6,     7,     8,     9,    10,    11,    12,    13,
      14,    15,    16,    17,    18,    19,    20,    21,    -1,    23,
      24,    25,    26,    27,    -1,    -1,    30,    -1,    -1,    -1,
      -1,    -1,    36,    -1,    -1,    -1,    40,    -1,    -1,    -1,
      -1,    -1,    46,    47,     5,     6,     7,     8,     9,    10,
      11,    12,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    -1,    23,    24,    25,    26,    27,    -1,    -1,    30,
      -1,    -1,    -1,    -1,    35,    -1,    -1,    -1,    -1,    40,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    -1,    23,    24,
      25,    26,    27,    -1,    -1,    30,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    40
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const unsigned char yystos[] =
{
       0,    53,    54,    55,     0,    54,     1,     5,     6,     7,
       8,     9,    10,    11,    12,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    40,    56,    58,    62,    63,    64,    65,
      66,    67,    71,    82,    94,    96,    43,    44,    31,    36,
      36,    50,    85,    57,    36,    85,    46,    46,    43,    36,
      46,    47,    59,    60,    61,    68,    72,    73,    64,    31,
      85,     1,    62,    86,    87,    88,    58,    62,    85,    63,
      36,     1,    72,    69,    70,    71,    43,    45,    72,    29,
      31,    95,    32,    46,    43,    44,    36,    40,    46,    51,
      68,    74,    75,    89,    90,    91,    92,    44,     1,    88,
      72,    47,    48,    48,    48,    48,    71,    61,    30,    93,
       1,    63,    76,    77,    78,    79,    92,     1,    36,    74,
      33,    74,    93,    32,    46,    43,    45,    48,    43,    49,
      83,    84,    48,    36,    40,    46,    68,    80,    81,    48,
      35,    45,    48,    48,     1,    76,    91,    33,     1,    40,
      80,    80,    32,    46,    35,    79,    48,    48,    48,    48,
       1,    76,    48,    48
};

#if ! defined (YYSIZE_T) && defined (__SIZE_TYPE__)
# define YYSIZE_T __SIZE_TYPE__
#endif
#if ! defined (YYSIZE_T) && defined (size_t)
# define YYSIZE_T size_t
#endif
#if ! defined (YYSIZE_T)
# if defined (__STDC__) || defined (__cplusplus)
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# endif
#endif
#if ! defined (YYSIZE_T)
# define YYSIZE_T unsigned int
#endif

#define yyerrok		(yyerrstatus = 0)
#define yyclearin	(yychar = YYEMPTY)
#define YYEMPTY		(-2)
#define YYEOF		0

#define YYACCEPT	goto yyacceptlab
#define YYABORT		goto yyabortlab
#define YYERROR		goto yyerrorlab


/* Like YYERROR except do call yyerror.  This remains here temporarily
   to ease the transition to the new meaning of YYERROR, for GCC.
   Once GCC version 2 has supplanted version 1, this can go.  */

#define YYFAIL		goto yyerrlab

#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)					\
do								\
  if (yychar == YYEMPTY && yylen == 1)				\
    {								\
      yychar = (Token);						\
      yylval = (Value);						\
      yytoken = YYTRANSLATE (yychar);				\
      YYPOPSTACK;						\
      goto yybackup;						\
    }								\
  else								\
    { 								\
      yyerror ("syntax error: cannot back up");\
      YYERROR;							\
    }								\
while (0)


#define YYTERROR	1
#define YYERRCODE	256


/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#define YYRHSLOC(Rhs, K) ((Rhs)[K])
#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)				\
    do									\
      if (N)								\
	{								\
	  (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;	\
	  (Current).first_column = YYRHSLOC (Rhs, 1).first_column;	\
	  (Current).last_line    = YYRHSLOC (Rhs, N).last_line;		\
	  (Current).last_column  = YYRHSLOC (Rhs, N).last_column;	\
	}								\
      else								\
	{								\
	  (Current).first_line   = (Current).last_line   =		\
	    YYRHSLOC (Rhs, 0).last_line;				\
	  (Current).first_column = (Current).last_column =		\
	    YYRHSLOC (Rhs, 0).last_column;				\
	}								\
    while (0)
#endif


/* YY_LOCATION_PRINT -- Print the location on the stream.
   This macro was not mandated originally: define only if we know
   we won't break user code: when these are the locations we know.  */

#ifndef YY_LOCATION_PRINT
# if YYLTYPE_IS_TRIVIAL
#  define YY_LOCATION_PRINT(File, Loc)			\
     fprintf (File, "%d.%d-%d.%d",			\
              (Loc).first_line, (Loc).first_column,	\
              (Loc).last_line,  (Loc).last_column)
# else
#  define YY_LOCATION_PRINT(File, Loc) ((void) 0)
# endif
#endif


/* YYLEX -- calling `yylex' with the right arguments.  */

#ifdef YYLEX_PARAM
# define YYLEX yylex (YYLEX_PARAM)
#else
# define YYLEX yylex ()
#endif

/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)			\
do {						\
  if (yydebug)					\
    YYFPRINTF Args;				\
} while (0)

# define YY_SYMBOL_PRINT(Title, Type, Value, Location)		\
do {								\
  if (yydebug)							\
    {								\
      YYFPRINTF (stderr, "%s ", Title);				\
      yysymprint (stderr, 					\
                  Type, Value);	\
      YYFPRINTF (stderr, "\n");					\
    }								\
} while (0)

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yy_stack_print (short int *bottom, short int *top)
#else
static void
yy_stack_print (bottom, top)
    short int *bottom;
    short int *top;
#endif
{
  YYFPRINTF (stderr, "Stack now");
  for (/* Nothing. */; bottom <= top; ++bottom)
    YYFPRINTF (stderr, " %d", *bottom);
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)				\
do {								\
  if (yydebug)							\
    yy_stack_print ((Bottom), (Top));				\
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yy_reduce_print (int yyrule)
#else
static void
yy_reduce_print (yyrule)
    int yyrule;
#endif
{
  int yyi;
  unsigned int yylno = yyrline[yyrule];
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %u), ",
             yyrule - 1, yylno);
  /* Print the symbols being reduced, and their result.  */
  for (yyi = yyprhs[yyrule]; 0 <= yyrhs[yyi]; yyi++)
    YYFPRINTF (stderr, "%s ", yytname [yyrhs[yyi]]);
  YYFPRINTF (stderr, "-> %s\n", yytname [yyr1[yyrule]]);
}

# define YY_REDUCE_PRINT(Rule)		\
do {					\
  if (yydebug)				\
    yy_reduce_print (Rule);		\
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args)
# define YY_SYMBOL_PRINT(Title, Type, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef	YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   SIZE_MAX < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif



#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined (__GLIBC__) && defined (_STRING_H)
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
static YYSIZE_T
#   if defined (__STDC__) || defined (__cplusplus)
yystrlen (const char *yystr)
#   else
yystrlen (yystr)
     const char *yystr;
#   endif
{
  register const char *yys = yystr;

  while (*yys++ != '\0')
    continue;

  return yys - yystr - 1;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined (__GLIBC__) && defined (_STRING_H) && defined (_GNU_SOURCE)
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
#   if defined (__STDC__) || defined (__cplusplus)
yystpcpy (char *yydest, const char *yysrc)
#   else
yystpcpy (yydest, yysrc)
     char *yydest;
     const char *yysrc;
#   endif
{
  register char *yyd = yydest;
  register const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif

#endif /* !YYERROR_VERBOSE */



#if YYDEBUG
/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yysymprint (FILE *yyoutput, int yytype, YYSTYPE *yyvaluep)
#else
static void
yysymprint (yyoutput, yytype, yyvaluep)
    FILE *yyoutput;
    int yytype;
    YYSTYPE *yyvaluep;
#endif
{
  /* Pacify ``unused variable'' warnings.  */
  (void) yyvaluep;

  if (yytype < YYNTOKENS)
    YYFPRINTF (yyoutput, "token %s (", yytname[yytype]);
  else
    YYFPRINTF (yyoutput, "nterm %s (", yytname[yytype]);


# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# endif
  switch (yytype)
    {
      default:
        break;
    }
  YYFPRINTF (yyoutput, ")");
}

#endif /* ! YYDEBUG */
/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep)
#else
static void
yydestruct (yymsg, yytype, yyvaluep)
    const char *yymsg;
    int yytype;
    YYSTYPE *yyvaluep;
#endif
{
  /* Pacify ``unused variable'' warnings.  */
  (void) yyvaluep;

  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  switch (yytype)
    {

      default:
        break;
    }
}


/* Prevent warnings from -Wmissing-prototypes.  */

#ifdef YYPARSE_PARAM
# if defined (__STDC__) || defined (__cplusplus)
int yyparse (void *YYPARSE_PARAM);
# else
int yyparse ();
# endif
#else /* ! YYPARSE_PARAM */
#if defined (__STDC__) || defined (__cplusplus)
int yyparse (void);
#else
int yyparse ();
#endif
#endif /* ! YYPARSE_PARAM */



/* The look-ahead symbol.  */
int yychar;

/* The semantic value of the look-ahead symbol.  */
YYSTYPE yylval;

/* Number of syntax errors so far.  */
int yynerrs;



/*----------.
| yyparse.  |
`----------*/

#ifdef YYPARSE_PARAM
# if defined (__STDC__) || defined (__cplusplus)
int yyparse (void *YYPARSE_PARAM)
# else
int yyparse (YYPARSE_PARAM)
  void *YYPARSE_PARAM;
# endif
#else /* ! YYPARSE_PARAM */
#if defined (__STDC__) || defined (__cplusplus)
int
yyparse (void)
#else
int
yyparse ()

#endif
#endif
{
  
  register int yystate;
  register int yyn;
  int yyresult;
  /* Number of tokens to shift before error messages enabled.  */
  int yyerrstatus;
  /* Look-ahead token as an internal (translated) token number.  */
  int yytoken = 0;

  /* Three stacks and their tools:
     `yyss': related to states,
     `yyvs': related to semantic values,
     `yyls': related to locations.

     Refer to the stacks thru separate pointers, to allow yyoverflow
     to reallocate them elsewhere.  */

  /* The state stack.  */
  short int yyssa[YYINITDEPTH];
  short int *yyss = yyssa;
  register short int *yyssp;

  /* The semantic value stack.  */
  YYSTYPE yyvsa[YYINITDEPTH];
  YYSTYPE *yyvs = yyvsa;
  register YYSTYPE *yyvsp;



#define YYPOPSTACK   (yyvsp--, yyssp--)

  YYSIZE_T yystacksize = YYINITDEPTH;

  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;


  /* When reducing, the number of symbols on the RHS of the reduced
     rule.  */
  int yylen;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY;		/* Cause a token to be read.  */

  /* Initialize stack pointers.
     Waste one element of value and location stack
     so that they stay on the same level as the state stack.
     The wasted elements are never initialized.  */

  yyssp = yyss;
  yyvsp = yyvs;


  yyvsp[0] = yylval;

  goto yysetstate;

/*------------------------------------------------------------.
| yynewstate -- Push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
 yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed. so pushing a state here evens the stacks.
     */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyss + yystacksize - 1 <= yyssp)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
	/* Give user a chance to reallocate the stack. Use copies of
	   these so that the &'s don't force the real ones into
	   memory.  */
	YYSTYPE *yyvs1 = yyvs;
	short int *yyss1 = yyss;


	/* Each stack pointer address is followed by the size of the
	   data in use in that stack, in bytes.  This used to be a
	   conditional around just the two extra args, but that might
	   be undefined if yyoverflow is a macro.  */
	yyoverflow ("parser stack overflow",
		    &yyss1, yysize * sizeof (*yyssp),
		    &yyvs1, yysize * sizeof (*yyvsp),

		    &yystacksize);

	yyss = yyss1;
	yyvs = yyvs1;
      }
#else /* no yyoverflow */
# ifndef YYSTACK_RELOCATE
      goto yyoverflowlab;
# else
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
	goto yyoverflowlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
	yystacksize = YYMAXDEPTH;

      {
	short int *yyss1 = yyss;
	union yyalloc *yyptr =
	  (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
	if (! yyptr)
	  goto yyoverflowlab;
	YYSTACK_RELOCATE (yyss);
	YYSTACK_RELOCATE (yyvs);

#  undef YYSTACK_RELOCATE
	if (yyss1 != yyssa)
	  YYSTACK_FREE (yyss1);
      }
# endif
#endif /* no yyoverflow */

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;


      YYDPRINTF ((stderr, "Stack size increased to %lu\n",
		  (unsigned long int) yystacksize));

      if (yyss + yystacksize - 1 <= yyssp)
	YYABORT;
    }

  YYDPRINTF ((stderr, "Entering state %d\n", yystate));

  goto yybackup;

/*-----------.
| yybackup.  |
`-----------*/
yybackup:

/* Do appropriate processing given the current state.  */
/* Read a look-ahead token if we need one and don't already have one.  */
/* yyresume: */

  /* First try to decide what to do without reference to look-ahead token.  */

  yyn = yypact[yystate];
  if (yyn == YYPACT_NINF)
    goto yydefault;

  /* Not known => get a look-ahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid look-ahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = YYLEX;
    }

  if (yychar <= YYEOF)
    {
      yychar = yytoken = YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yyn == 0 || yyn == YYTABLE_NINF)
	goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  /* Shift the look-ahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);

  /* Discard the token being shifted unless it is eof.  */
  if (yychar != YYEOF)
    yychar = YYEMPTY;

  *++yyvsp = yylval;


  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  yystate = yyn;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- Do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     `$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 4:
#line 107 "scripts/genksyms/parse.y"
    { is_typedef = 0; is_extern = 0; current_name = NULL; decl_spec = NULL; ;}
    break;

  case 5:
#line 109 "scripts/genksyms/parse.y"
    { free_list(*(yyvsp[0]), NULL); *(yyvsp[0]) = NULL; ;}
    break;

  case 6:
#line 113 "scripts/genksyms/parse.y"
    { is_typedef = 1; ;}
    break;

  case 7:
#line 114 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 12:
#line 119 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 13:
#line 120 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 14:
#line 125 "scripts/genksyms/parse.y"
    { if (current_name) {
		    struct string_list *decl = (*(yyvsp[0]))->next;
		    (*(yyvsp[0]))->next = NULL;
		    add_symbol(current_name,
			       is_typedef ? SYM_TYPEDEF : SYM_NORMAL,
			       decl, is_extern);
		    current_name = NULL;
		  }
		  (yyval) = (yyvsp[0]);
		;}
    break;

  case 15:
#line 138 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 17:
#line 144 "scripts/genksyms/parse.y"
    { struct string_list *decl = *(yyvsp[0]);
		  *(yyvsp[0]) = NULL;
		  add_symbol(current_name,
			     is_typedef ? SYM_TYPEDEF : SYM_NORMAL, decl, is_extern);
		  current_name = NULL;
		  (yyval) = (yyvsp[0]);
		;}
    break;

  case 18:
#line 152 "scripts/genksyms/parse.y"
    { struct string_list *decl = *(yyvsp[0]);
		  *(yyvsp[0]) = NULL;
		  free_list(*(yyvsp[-1]), NULL);
		  *(yyvsp[-1]) = decl_spec;
		  add_symbol(current_name,
			     is_typedef ? SYM_TYPEDEF : SYM_NORMAL, decl, is_extern);
		  current_name = NULL;
		  (yyval) = (yyvsp[0]);
		;}
    break;

  case 19:
#line 165 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]) ? (yyvsp[0]) : (yyvsp[-1]) ? (yyvsp[-1]) : (yyvsp[-2]) ? (yyvsp[-2]) : (yyvsp[-3]); ;}
    break;

  case 20:
#line 170 "scripts/genksyms/parse.y"
    { decl_spec = NULL; ;}
    break;

  case 22:
#line 175 "scripts/genksyms/parse.y"
    { decl_spec = *(yyvsp[0]); ;}
    break;

  case 23:
#line 176 "scripts/genksyms/parse.y"
    { decl_spec = *(yyvsp[0]); ;}
    break;

  case 24:
#line 181 "scripts/genksyms/parse.y"
    { /* Version 2 checksumming ignores storage class, as that
		     is really irrelevant to the linkage.  */
		  remove_node((yyvsp[0]));
		  (yyval) = (yyvsp[0]);
		;}
    break;

  case 29:
#line 193 "scripts/genksyms/parse.y"
    { is_extern = 1; (yyval) = (yyvsp[0]); ;}
    break;

  case 30:
#line 194 "scripts/genksyms/parse.y"
    { is_extern = 0; (yyval) = (yyvsp[0]); ;}
    break;

  case 35:
#line 206 "scripts/genksyms/parse.y"
    { remove_node((yyvsp[-1])); (*(yyvsp[0]))->tag = SYM_STRUCT; (yyval) = (yyvsp[0]); ;}
    break;

  case 36:
#line 208 "scripts/genksyms/parse.y"
    { remove_node((yyvsp[-1])); (*(yyvsp[0]))->tag = SYM_UNION; (yyval) = (yyvsp[0]); ;}
    break;

  case 37:
#line 210 "scripts/genksyms/parse.y"
    { remove_node((yyvsp[-1])); (*(yyvsp[0]))->tag = SYM_ENUM; (yyval) = (yyvsp[0]); ;}
    break;

  case 38:
#line 214 "scripts/genksyms/parse.y"
    { struct string_list *s = *(yyvsp[0]), *i = *(yyvsp[-1]), *r;
		  r = copy_node(i); r->tag = SYM_STRUCT;
		  r->next = (*(yyvsp[-2]))->next; *(yyvsp[0]) = r; (*(yyvsp[-2]))->next = NULL;
		  add_symbol(i->string, SYM_STRUCT, s, is_extern);
		  (yyval) = (yyvsp[0]);
		;}
    break;

  case 39:
#line 221 "scripts/genksyms/parse.y"
    { struct string_list *s = *(yyvsp[0]), *i = *(yyvsp[-1]), *r;
		  r = copy_node(i); r->tag = SYM_UNION;
		  r->next = (*(yyvsp[-2]))->next; *(yyvsp[0]) = r; (*(yyvsp[-2]))->next = NULL;
		  add_symbol(i->string, SYM_UNION, s, is_extern);
		  (yyval) = (yyvsp[0]);
		;}
    break;

  case 40:
#line 228 "scripts/genksyms/parse.y"
    { struct string_list *s = *(yyvsp[0]), *i = *(yyvsp[-1]), *r;
		  r = copy_node(i); r->tag = SYM_ENUM;
		  r->next = (*(yyvsp[-2]))->next; *(yyvsp[0]) = r; (*(yyvsp[-2]))->next = NULL;
		  add_symbol(i->string, SYM_ENUM, s, is_extern);
		  (yyval) = (yyvsp[0]);
		;}
    break;

  case 41:
#line 236 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 42:
#line 237 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 43:
#line 238 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 54:
#line 252 "scripts/genksyms/parse.y"
    { (*(yyvsp[0]))->tag = SYM_TYPEDEF; (yyval) = (yyvsp[0]); ;}
    break;

  case 55:
#line 257 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]) ? (yyvsp[0]) : (yyvsp[-1]); ;}
    break;

  case 56:
#line 261 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 59:
#line 267 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 63:
#line 273 "scripts/genksyms/parse.y"
    { /* restrict has no effect in prototypes so ignore it */
		  remove_node((yyvsp[0]));
		  (yyval) = (yyvsp[0]);
		;}
    break;

  case 64:
#line 280 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 66:
#line 286 "scripts/genksyms/parse.y"
    { if (current_name != NULL) {
		    error_with_pos("unexpected second declaration name");
		    YYERROR;
		  } else {
		    current_name = (*(yyvsp[0]))->string;
		    (yyval) = (yyvsp[0]);
		  }
		;}
    break;

  case 67:
#line 295 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 68:
#line 297 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 69:
#line 299 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 70:
#line 301 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 71:
#line 303 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 72:
#line 309 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 76:
#line 317 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 77:
#line 319 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 78:
#line 321 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 79:
#line 323 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 80:
#line 325 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 81:
#line 329 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 83:
#line 331 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 84:
#line 335 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 87:
#line 342 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 88:
#line 347 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]) ? (yyvsp[0]) : (yyvsp[-1]); ;}
    break;

  case 89:
#line 352 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]) ? (yyvsp[0]) : (yyvsp[-1]); ;}
    break;

  case 91:
#line 357 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 92:
#line 359 "scripts/genksyms/parse.y"
    { /* For version 2 checksums, we don't want to remember
		     private parameter names.  */
		  remove_node((yyvsp[0]));
		  (yyval) = (yyvsp[0]);
		;}
    break;

  case 93:
#line 367 "scripts/genksyms/parse.y"
    { remove_node((yyvsp[0]));
		  (yyval) = (yyvsp[0]);
		;}
    break;

  case 94:
#line 371 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 95:
#line 373 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 96:
#line 375 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 97:
#line 377 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 98:
#line 379 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 99:
#line 384 "scripts/genksyms/parse.y"
    { struct string_list *decl = *(yyvsp[-1]);
		  *(yyvsp[-1]) = NULL;
		  add_symbol(current_name, SYM_NORMAL, decl, is_extern);
		  (yyval) = (yyvsp[0]);
		;}
    break;

  case 100:
#line 392 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 102:
#line 399 "scripts/genksyms/parse.y"
    { remove_list((yyvsp[0]), &(*(yyvsp[-1]))->next); (yyval) = (yyvsp[0]); ;}
    break;

  case 103:
#line 403 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 104:
#line 404 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 105:
#line 408 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 108:
#line 414 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 109:
#line 419 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 110:
#line 421 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 111:
#line 425 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 114:
#line 431 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 115:
#line 435 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]) ? (yyvsp[0]) : (yyvsp[-1]); ;}
    break;

  case 116:
#line 436 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 118:
#line 441 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 119:
#line 445 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 121:
#line 450 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[0]); ;}
    break;

  case 122:
#line 454 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 124:
#line 460 "scripts/genksyms/parse.y"
    { export_symbol((*(yyvsp[-2]))->string); (yyval) = (yyvsp[0]); ;}
    break;


    }

/* Line 1037 of yacc.c.  */
#line 1816 "scripts/genksyms/parse.c"

  yyvsp -= yylen;
  yyssp -= yylen;


  YY_STACK_PRINT (yyss, yyssp);

  *++yyvsp = yyval;


  /* Now `shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTOKENS] + *yyssp;
  if (0 <= yystate && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTOKENS];

  goto yynewstate;


/*------------------------------------.
| yyerrlab -- here on detecting error |
`------------------------------------*/
yyerrlab:
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if YYERROR_VERBOSE
      yyn = yypact[yystate];

      if (YYPACT_NINF < yyn && yyn < YYLAST)
	{
	  YYSIZE_T yysize = 0;
	  int yytype = YYTRANSLATE (yychar);
	  const char* yyprefix;
	  char *yymsg;
	  int yyx;

	  /* Start YYX at -YYN if negative to avoid negative indexes in
	     YYCHECK.  */
	  int yyxbegin = yyn < 0 ? -yyn : 0;

	  /* Stay within bounds of both yycheck and yytname.  */
	  int yychecklim = YYLAST - yyn;
	  int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
	  int yycount = 0;

	  yyprefix = ", expecting ";
	  for (yyx = yyxbegin; yyx < yyxend; ++yyx)
	    if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
	      {
		yysize += yystrlen (yyprefix) + yystrlen (yytname [yyx]);
		yycount += 1;
		if (yycount == 5)
		  {
		    yysize = 0;
		    break;
		  }
	      }
	  yysize += (sizeof ("syntax error, unexpected ")
		     + yystrlen (yytname[yytype]));
	  yymsg = (char *) YYSTACK_ALLOC (yysize);
	  if (yymsg != 0)
	    {
	      char *yyp = yystpcpy (yymsg, "syntax error, unexpected ");
	      yyp = yystpcpy (yyp, yytname[yytype]);

	      if (yycount < 5)
		{
		  yyprefix = ", expecting ";
		  for (yyx = yyxbegin; yyx < yyxend; ++yyx)
		    if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
		      {
			yyp = yystpcpy (yyp, yyprefix);
			yyp = yystpcpy (yyp, yytname[yyx]);
			yyprefix = " or ";
		      }
		}
	      yyerror (yymsg);
	      YYSTACK_FREE (yymsg);
	    }
	  else
	    yyerror ("syntax error; also virtual memory exhausted");
	}
      else
#endif /* YYERROR_VERBOSE */
	yyerror ("syntax error");
    }



  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse look-ahead token after an
	 error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* If at end of input, pop the error token,
	     then the rest of the stack, then return failure.  */
	  if (yychar == YYEOF)
	     for (;;)
	       {

		 YYPOPSTACK;
		 if (yyssp == yyss)
		   YYABORT;
		 yydestruct ("Error: popping",
                             yystos[*yyssp], yyvsp);
	       }
        }
      else
	{
	  yydestruct ("Error: discarding", yytoken, &yylval);
	  yychar = YYEMPTY;
	}
    }

  /* Else will try to reuse look-ahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:

#ifdef __GNUC__
  /* Pacify GCC when the user code never invokes YYERROR and the label
     yyerrorlab therefore never appears in user code.  */
  if (0)
     goto yyerrorlab;
#endif

yyvsp -= yylen;
  yyssp -= yylen;
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;	/* Each real token shifted decrements this.  */

  for (;;)
    {
      yyn = yypact[yystate];
      if (yyn != YYPACT_NINF)
	{
	  yyn += YYTERROR;
	  if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYTERROR)
	    {
	      yyn = yytable[yyn];
	      if (0 < yyn)
		break;
	    }
	}

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
	YYABORT;


      yydestruct ("Error: popping", yystos[yystate], yyvsp);
      YYPOPSTACK;
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  *++yyvsp = yylval;


  /* Shift the error token. */
  YY_SYMBOL_PRINT ("Shifting", yystos[yyn], yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturn;

/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yydestruct ("Error: discarding lookahead",
              yytoken, &yylval);
  yychar = YYEMPTY;
  yyresult = 1;
  goto yyreturn;

#ifndef yyoverflow
/*----------------------------------------------.
| yyoverflowlab -- parser overflow comes here.  |
`----------------------------------------------*/
yyoverflowlab:
  yyerror ("parser stack overflow");
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
  return yyresult;
}


#line 464 "scripts/genksyms/parse.y"


static void
yyerror(const char *e)
{
  error_with_pos("%s", e);
}

