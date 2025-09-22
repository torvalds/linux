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
     ENT_TYPEDEF_STRUCT = 258,
     ENT_STRUCT = 259,
     ENT_EXTERNSTATIC = 260,
     ENT_YACCUNION = 261,
     GTY_TOKEN = 262,
     UNION = 263,
     STRUCT = 264,
     ENUM = 265,
     ALIAS = 266,
     NESTED_PTR = 267,
     PARAM_IS = 268,
     NUM = 269,
     PERCENTPERCENT = 270,
     SCALAR = 271,
     ID = 272,
     STRING = 273,
     ARRAY = 274,
     PERCENT_ID = 275,
     CHAR = 276
   };
#endif
#define ENT_TYPEDEF_STRUCT 258
#define ENT_STRUCT 259
#define ENT_EXTERNSTATIC 260
#define ENT_YACCUNION 261
#define GTY_TOKEN 262
#define UNION 263
#define STRUCT 264
#define ENUM 265
#define ALIAS 266
#define NESTED_PTR 267
#define PARAM_IS 268
#define NUM 269
#define PERCENTPERCENT 270
#define SCALAR 271
#define ID 272
#define STRING 273
#define ARRAY 274
#define PERCENT_ID 275
#define CHAR 276




/* Copy the first part of user declarations.  */
#line 22 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"

#include "bconfig.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "gengtype.h"
#define YYERROR_VERBOSE


/* Enabling traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 0
#endif

#if ! defined (YYSTYPE) && ! defined (YYSTYPE_IS_DECLARED)
#line 31 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
typedef union YYSTYPE {
  type_p t;
  pair_p p;
  options_p o;
  const char *s;
} YYSTYPE;
/* Line 190 of yacc.c.  */
#line 134 "gengtype-yacc.c"
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif



/* Copy the second part of user declarations.  */


/* Line 213 of yacc.c.  */
#line 146 "gengtype-yacc.c"

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
#define YYFINAL  14
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   118

/* YYNTOKENS -- Number of terminals. */
#define YYNTOKENS  33
/* YYNNTS -- Number of nonterminals. */
#define YYNNTS  23
/* YYNRULES -- Number of rules. */
#define YYNRULES  59
/* YYNRULES -- Number of states. */
#define YYNSTATES  127

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   276

#define YYTRANSLATE(YYX) 						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const unsigned char yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      31,    32,    29,     2,    30,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    28,    24,
      26,    25,    27,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    22,     2,    23,     2,     2,     2,     2,
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
      15,    16,    17,    18,    19,    20,    21
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const unsigned char yyprhs[] =
{
       0,     0,     3,     4,     7,    10,    13,    14,    23,    24,
      32,    38,    45,    53,    55,    57,    59,    66,    67,    71,
      78,    79,    82,    85,    86,    93,   100,   108,   114,   115,
     118,   120,   122,   124,   126,   129,   135,   138,   144,   147,
     150,   156,   157,   163,   167,   170,   171,   173,   180,   182,
     184,   186,   191,   196,   205,   207,   211,   212,   214,   216
};

/* YYRHS -- A `-1'-separated list of the rules' RHS. */
static const yysigned_char yyrhs[] =
{
      34,     0,    -1,    -1,    35,    34,    -1,    38,    34,    -1,
      41,    34,    -1,    -1,     3,    50,    22,    44,    23,    17,
      36,    24,    -1,    -1,     4,    50,    22,    44,    23,    37,
      24,    -1,     5,    50,    39,    17,    40,    -1,     5,    50,
      39,    17,    19,    40,    -1,     5,    50,    39,    17,    19,
      19,    40,    -1,    47,    -1,    24,    -1,    25,    -1,     6,
      50,    44,    23,    42,    15,    -1,    -1,    42,    20,    43,
      -1,    42,    20,    26,    17,    27,    43,    -1,    -1,    43,
      17,    -1,    43,    21,    -1,    -1,    47,    49,    17,    45,
      24,    44,    -1,    47,    49,    17,    19,    24,    44,    -1,
      47,    49,    17,    19,    19,    24,    44,    -1,    47,    28,
      46,    24,    44,    -1,    -1,    28,    46,    -1,    14,    -1,
      17,    -1,    16,    -1,    17,    -1,    47,    29,    -1,     9,
      17,    22,    44,    23,    -1,     9,    17,    -1,     8,    17,
      22,    44,    23,    -1,     8,    17,    -1,    10,    17,    -1,
      10,    17,    22,    48,    23,    -1,    -1,    17,    25,    14,
      30,    48,    -1,    17,    30,    48,    -1,    17,    48,    -1,
      -1,    50,    -1,     7,    31,    31,    54,    32,    32,    -1,
      11,    -1,    13,    -1,    17,    -1,    17,    31,    55,    32,
      -1,    51,    31,    47,    32,    -1,    12,    31,    47,    30,
      55,    30,    55,    32,    -1,    52,    -1,    53,    30,    52,
      -1,    -1,    53,    -1,    18,    -1,    55,    18,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const unsigned short int yyrline[] =
{
       0,    65,    65,    66,    67,    68,    72,    71,    81,    80,
      90,    95,   100,   108,   115,   116,   119,   127,   128,   141,
     160,   161,   172,   185,   186,   196,   206,   216,   220,   221,
     224,   224,   228,   230,   232,   234,   236,   238,   240,   242,
     244,   248,   249,   251,   253,   257,   258,   261,   265,   267,
     271,   273,   275,   277,   289,   294,   301,   302,   305,   307
};
#endif

#if YYDEBUG || YYERROR_VERBOSE
/* YYTNME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals. */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "ENT_TYPEDEF_STRUCT", "ENT_STRUCT",
  "ENT_EXTERNSTATIC", "ENT_YACCUNION", "GTY_TOKEN", "UNION", "STRUCT",
  "ENUM", "ALIAS", "NESTED_PTR", "PARAM_IS", "NUM", "\"%%\"", "SCALAR",
  "ID", "STRING", "ARRAY", "PERCENT_ID", "CHAR", "'{'", "'}'", "';'",
  "'='", "'<'", "'>'", "':'", "'*'", "','", "'('", "')'", "$accept",
  "start", "typedef_struct", "@1", "@2", "externstatic", "lasttype",
  "semiequal", "yacc_union", "yacc_typematch", "yacc_ids", "struct_fields",
  "bitfieldopt", "bitfieldlen", "type", "enum_items", "optionsopt",
  "options", "type_option", "option", "optionseq", "optionseqopt",
  "stringseq", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const unsigned short int yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   123,   125,    59,    61,    60,    62,    58,    42,
      44,    40,    41
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const unsigned char yyr1[] =
{
       0,    33,    34,    34,    34,    34,    36,    35,    37,    35,
      38,    38,    38,    39,    40,    40,    41,    42,    42,    42,
      43,    43,    43,    44,    44,    44,    44,    44,    45,    45,
      46,    46,    47,    47,    47,    47,    47,    47,    47,    47,
      47,    48,    48,    48,    48,    49,    49,    50,    51,    51,
      52,    52,    52,    52,    53,    53,    54,    54,    55,    55
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const unsigned char yyr2[] =
{
       0,     2,     0,     2,     2,     2,     0,     8,     0,     7,
       5,     6,     7,     1,     1,     1,     6,     0,     3,     6,
       0,     2,     2,     0,     6,     6,     7,     5,     0,     2,
       1,     1,     1,     1,     2,     5,     2,     5,     2,     2,
       5,     0,     5,     3,     2,     0,     1,     6,     1,     1,
       1,     4,     4,     8,     1,     3,     0,     1,     1,     2
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const unsigned char yydefact[] =
{
       2,     0,     0,     0,     0,     0,     2,     2,     2,     0,
       0,     0,     0,    23,     1,     3,     4,     5,     0,    23,
      23,     0,     0,     0,    32,    33,     0,    13,     0,    45,
      56,     0,     0,    38,    36,    39,     0,    34,    17,     0,
       0,    46,    48,     0,    49,    50,     0,    54,    57,     0,
       0,     8,    23,    23,    41,     0,    14,    15,    10,     0,
      30,    31,     0,    28,     0,     0,     0,     0,     0,     6,
       0,     0,     0,    41,     0,     0,    11,    16,    20,    23,
       0,     0,     0,     0,    58,     0,     0,    55,    47,     0,
       9,    37,    35,     0,    41,    44,    40,    12,     0,    18,
      27,     0,    23,    29,    23,     0,    59,    51,    52,     7,
       0,    43,     0,    21,    22,    23,    25,    24,     0,    41,
      20,    26,     0,    42,    19,     0,    53
};

/* YYDEFGOTO[NTERM-NUM]. */
static const yysigned_char yydefgoto[] =
{
      -1,     5,     6,    89,    70,     7,    26,    58,     8,    59,
      99,    28,    82,    62,    29,    74,    40,    10,    46,    47,
      48,    49,    85
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -98
static const yysigned_char yypact[] =
{
      58,    12,    12,    12,    12,    43,    58,    58,    58,     5,
      45,    53,    29,    29,   -98,   -98,   -98,   -98,    28,    29,
      29,    41,    59,    60,   -98,   -98,    61,    50,    57,     0,
       4,    63,    65,    62,    67,    68,    16,   -98,   -98,    51,
      70,   -98,   -98,    66,   -98,    69,    71,   -98,    52,    49,
      74,   -98,    29,    29,    75,    23,   -98,   -98,   -98,    -2,
     -98,   -98,    77,    -8,    29,    76,    29,     4,    72,   -98,
      79,    82,    83,    -3,    84,    27,   -98,   -98,    73,    29,
      31,    51,    85,    44,   -98,    -9,    37,   -98,   -98,    86,
     -98,   -98,   -98,    81,    75,   -98,   -98,   -98,    91,    36,
     -98,    87,    29,   -98,    29,    76,   -98,   -98,   -98,   -98,
      88,   -98,    89,   -98,   -98,    29,   -98,   -98,    14,    75,
     -98,   -98,    76,   -98,    36,    -6,   -98
};

/* YYPGOTO[NTERM-NUM].  */
static const yysigned_char yypgoto[] =
{
     -98,    64,   -98,   -98,   -98,   -98,   -98,   -45,   -98,   -98,
     -27,   -19,   -98,    17,   -10,   -70,   -98,     2,   -98,    46,
     -98,   -98,   -97
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -1
static const unsigned char yytable[] =
{
      31,    32,    27,    95,    11,    12,    13,     9,   118,   106,
      76,    80,   106,    77,    73,    42,    43,    44,    78,     9,
      81,    45,    93,   107,   111,   125,   126,    94,    39,    37,
      97,    41,   106,    71,    72,    55,    18,    21,    22,    23,
      56,    57,    75,    14,   122,    24,    25,    56,    57,   123,
     101,    56,    57,   113,    83,   102,    86,   114,    33,    30,
     100,     1,     2,     3,     4,    60,    37,    19,    61,   108,
      15,    16,    17,    37,   105,    20,    34,    35,    36,    37,
      38,    68,    67,   116,    52,   117,    50,    63,    51,    53,
      54,    69,    73,   124,    84,   110,   121,    64,   103,    98,
      65,    79,    66,    90,    88,    91,    92,    96,   112,   104,
     109,   115,     0,    87,     0,     0,   120,     0,   119
};

static const yysigned_char yycheck[] =
{
      19,    20,    12,    73,     2,     3,     4,     7,   105,    18,
      55,    19,    18,    15,    17,    11,    12,    13,    20,     7,
      28,    17,    25,    32,    94,   122,    32,    30,    28,    29,
      75,    29,    18,    52,    53,    19,    31,     8,     9,    10,
      24,    25,    19,     0,    30,    16,    17,    24,    25,   119,
      19,    24,    25,    17,    64,    24,    66,    21,    17,    31,
      79,     3,     4,     5,     6,    14,    29,    22,    17,    32,
       6,     7,     8,    29,    30,    22,    17,    17,    17,    29,
      23,    32,    30,   102,    22,   104,    23,    17,    23,    22,
      22,    17,    17,   120,    18,    14,   115,    31,    81,    26,
      31,    24,    31,    24,    32,    23,    23,    23,    17,    24,
      24,    24,    -1,    67,    -1,    -1,    27,    -1,    30
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const unsigned char yystos[] =
{
       0,     3,     4,     5,     6,    34,    35,    38,    41,     7,
      50,    50,    50,    50,     0,    34,    34,    34,    31,    22,
      22,     8,     9,    10,    16,    17,    39,    47,    44,    47,
      31,    44,    44,    17,    17,    17,    17,    29,    23,    28,
      49,    50,    11,    12,    13,    17,    51,    52,    53,    54,
      23,    23,    22,    22,    22,    19,    24,    25,    40,    42,
      14,    17,    46,    17,    31,    31,    31,    30,    32,    17,
      37,    44,    44,    17,    48,    19,    40,    15,    20,    24,
      19,    28,    45,    47,    18,    55,    47,    52,    32,    36,
      24,    23,    23,    25,    30,    48,    23,    40,    26,    43,
      44,    19,    24,    46,    24,    30,    18,    32,    32,    24,
      14,    48,    17,    17,    21,    24,    44,    44,    55,    30,
      27,    44,    30,    48,    43,    55,    32
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
        case 6:
#line 72 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    {
		     new_structure ((yyvsp[-5].t)->u.s.tag, UNION_P ((yyvsp[-5].t)), &lexer_line,
				    (yyvsp[-2].p), (yyvsp[-4].o));
		     do_typedef ((yyvsp[0].s), (yyvsp[-5].t), &lexer_line);
		     lexer_toplevel_done = 1;
		   ;}
    break;

  case 7:
#line 79 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    {;}
    break;

  case 8:
#line 81 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    {
		     new_structure ((yyvsp[-4].t)->u.s.tag, UNION_P ((yyvsp[-4].t)), &lexer_line,
				    (yyvsp[-1].p), (yyvsp[-3].o));
		     lexer_toplevel_done = 1;
		   ;}
    break;

  case 9:
#line 87 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    {;}
    break;

  case 10:
#line 91 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    {
	           note_variable ((yyvsp[-1].s), adjust_field_type ((yyvsp[-2].t), (yyvsp[-3].o)), (yyvsp[-3].o),
				  &lexer_line);
	         ;}
    break;

  case 11:
#line 96 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    {
	           note_variable ((yyvsp[-2].s), create_array ((yyvsp[-3].t), (yyvsp[-1].s)),
	      		    (yyvsp[-4].o), &lexer_line);
	         ;}
    break;

  case 12:
#line 101 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    {
	           note_variable ((yyvsp[-3].s), create_array (create_array ((yyvsp[-4].t), (yyvsp[-1].s)),
	      				      (yyvsp[-2].s)),
	      		    (yyvsp[-5].o), &lexer_line);
	         ;}
    break;

  case 13:
#line 109 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    {
	      lexer_toplevel_done = 1;
	      (yyval.t) = (yyvsp[0].t);
	    ;}
    break;

  case 16:
#line 121 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    {
	        note_yacc_type ((yyvsp[-4].o), (yyvsp[-3].p), (yyvsp[-1].p), &lexer_line);
	      ;}
    break;

  case 17:
#line 127 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { (yyval.p) = NULL; ;}
    break;

  case 18:
#line 129 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    {
		     pair_p p;
		     for (p = (yyvsp[0].p); p->next != NULL; p = p->next)
		       {
		         p->name = NULL;
			 p->type = NULL;
		       }
		     p->name = NULL;
		     p->type = NULL;
		     p->next = (yyvsp[-2].p);
		     (yyval.p) = (yyvsp[0].p);
		   ;}
    break;

  case 19:
#line 142 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    {
		     pair_p p;
		     type_p newtype = NULL;
		     if (strcmp ((yyvsp[-4].s), "type") == 0)
		       newtype = (type_p) 1;
		     for (p = (yyvsp[0].p); p->next != NULL; p = p->next)
		       {
		         p->name = (yyvsp[-2].s);
		         p->type = newtype;
		       }
		     p->name = (yyvsp[-2].s);
		     p->next = (yyvsp[-5].p);
		     p->type = newtype;
		     (yyval.p) = (yyvsp[0].p);
		   ;}
    break;

  case 20:
#line 160 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { (yyval.p) = NULL; ;}
    break;

  case 21:
#line 162 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    {
	  pair_p p = XCNEW (struct pair);
	  p->next = (yyvsp[-1].p);
	  p->line = lexer_line;
	  p->opt = XNEW (struct options);
	  p->opt->name = "tag";
	  p->opt->next = NULL;
	  p->opt->info = (char *)(yyvsp[0].s);
	  (yyval.p) = p;
	;}
    break;

  case 22:
#line 173 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    {
	  pair_p p = XCNEW (struct pair);
	  p->next = (yyvsp[-1].p);
	  p->line = lexer_line;
	  p->opt = XNEW (struct options);
	  p->opt->name = "tag";
	  p->opt->next = NULL;
	  p->opt->info = xasprintf ("'%s'", (yyvsp[0].s));
	  (yyval.p) = p;
	;}
    break;

  case 23:
#line 185 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { (yyval.p) = NULL; ;}
    break;

  case 24:
#line 187 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    {
	            pair_p p = XNEW (struct pair);
		    p->type = adjust_field_type ((yyvsp[-5].t), (yyvsp[-4].o));
		    p->opt = (yyvsp[-4].o);
		    p->name = (yyvsp[-3].s);
		    p->next = (yyvsp[0].p);
		    p->line = lexer_line;
		    (yyval.p) = p;
		  ;}
    break;

  case 25:
#line 197 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    {
	            pair_p p = XNEW (struct pair);
		    p->type = adjust_field_type (create_array ((yyvsp[-5].t), (yyvsp[-2].s)), (yyvsp[-4].o));
		    p->opt = (yyvsp[-4].o);
		    p->name = (yyvsp[-3].s);
		    p->next = (yyvsp[0].p);
		    p->line = lexer_line;
		    (yyval.p) = p;
		  ;}
    break;

  case 26:
#line 207 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    {
	            pair_p p = XNEW (struct pair);
		    p->type = create_array (create_array ((yyvsp[-6].t), (yyvsp[-2].s)), (yyvsp[-3].s));
		    p->opt = (yyvsp[-5].o);
		    p->name = (yyvsp[-4].s);
		    p->next = (yyvsp[0].p);
		    p->line = lexer_line;
		    (yyval.p) = p;
		  ;}
    break;

  case 27:
#line 217 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { (yyval.p) = (yyvsp[0].p); ;}
    break;

  case 31:
#line 225 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { ;}
    break;

  case 32:
#line 229 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { (yyval.t) = (yyvsp[0].t); ;}
    break;

  case 33:
#line 231 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { (yyval.t) = resolve_typedef ((yyvsp[0].s), &lexer_line); ;}
    break;

  case 34:
#line 233 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { (yyval.t) = create_pointer ((yyvsp[-1].t)); ;}
    break;

  case 35:
#line 235 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { (yyval.t) = new_structure ((yyvsp[-3].s), 0, &lexer_line, (yyvsp[-1].p), NULL); ;}
    break;

  case 36:
#line 237 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { (yyval.t) = find_structure ((yyvsp[0].s), 0); ;}
    break;

  case 37:
#line 239 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { (yyval.t) = new_structure ((yyvsp[-3].s), 1, &lexer_line, (yyvsp[-1].p), NULL); ;}
    break;

  case 38:
#line 241 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { (yyval.t) = find_structure ((yyvsp[0].s), 1); ;}
    break;

  case 39:
#line 243 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { (yyval.t) = create_scalar_type ((yyvsp[0].s), strlen ((yyvsp[0].s))); ;}
    break;

  case 40:
#line 245 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { (yyval.t) = create_scalar_type ((yyvsp[-3].s), strlen ((yyvsp[-3].s))); ;}
    break;

  case 42:
#line 250 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { ;}
    break;

  case 43:
#line 252 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { ;}
    break;

  case 44:
#line 254 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { ;}
    break;

  case 45:
#line 257 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { (yyval.o) = NULL; ;}
    break;

  case 46:
#line 258 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { (yyval.o) = (yyvsp[0].o); ;}
    break;

  case 47:
#line 262 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { (yyval.o) = (yyvsp[-2].o); ;}
    break;

  case 48:
#line 266 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { (yyval.s) = "ptr_alias"; ;}
    break;

  case 49:
#line 268 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { (yyval.s) = (yyvsp[0].s); ;}
    break;

  case 50:
#line 272 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { (yyval.o) = create_option (NULL, (yyvsp[0].s), (void *)""); ;}
    break;

  case 51:
#line 274 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { (yyval.o) = create_option (NULL, (yyvsp[-3].s), (void *)(yyvsp[-1].s)); ;}
    break;

  case 52:
#line 276 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { (yyval.o) = create_option (NULL, (yyvsp[-3].s), adjust_field_type ((yyvsp[-1].t), NULL)); ;}
    break;

  case 53:
#line 278 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    {
	      struct nested_ptr_data d;

	      d.type = adjust_field_type ((yyvsp[-5].t), NULL);
	      d.convert_to = (yyvsp[-3].s);
	      d.convert_from = (yyvsp[-1].s);
	      (yyval.o) = create_option (NULL, "nested_ptr",
				  xmemdup (&d, sizeof (d), sizeof (d)));
	    ;}
    break;

  case 54:
#line 290 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    {
	        (yyvsp[0].o)->next = NULL;
		(yyval.o) = (yyvsp[0].o);
	      ;}
    break;

  case 55:
#line 295 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    {
	        (yyvsp[0].o)->next = (yyvsp[-2].o);
		(yyval.o) = (yyvsp[0].o);
	      ;}
    break;

  case 56:
#line 301 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { (yyval.o) = NULL; ;}
    break;

  case 57:
#line 302 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { (yyval.o) = (yyvsp[0].o); ;}
    break;

  case 58:
#line 306 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    { (yyval.s) = (yyvsp[0].s); ;}
    break;

  case 59:
#line 308 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"
    {
	       size_t l1 = strlen ((yyvsp[-1].s));
	       size_t l2 = strlen ((yyvsp[0].s));
	       char *s = XRESIZEVEC (char, (yyvsp[-1].s), l1 + l2 + 1);
	       memcpy (s + l1, (yyvsp[0].s), l2 + 1);
	       XDELETE ((yyvsp[0].s));
	       (yyval.s) = s;
	     ;}
    break;


    }

/* Line 1037 of yacc.c.  */
#line 1508 "gengtype-yacc.c"

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


#line 317 "/scratch/mitchell/gcc-releases/gcc-4.2.1/gcc-4.2.1/gcc/gengtype-yacc.y"


