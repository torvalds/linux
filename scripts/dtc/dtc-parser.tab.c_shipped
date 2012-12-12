
/* A Bison parser, made by GNU Bison 2.4.1.  */

/* Skeleton implementation for Bison's Yacc-like parsers in C
   
      Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.
   
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.
   
   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Bison version.  */
#define YYBISON_VERSION "2.4.1"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1

/* Using locations.  */
#define YYLSP_NEEDED 0



/* Copy the first part of user declarations.  */

/* Line 189 of yacc.c  */
#line 21 "dtc-parser.y"

#include <stdio.h>

#include "dtc.h"
#include "srcpos.h"

YYLTYPE yylloc;

extern int yylex(void);
extern void print_error(char const *fmt, ...);
extern void yyerror(char const *s);

extern struct boot_info *the_boot_info;
extern int treesource_error;

static unsigned long long eval_literal(const char *s, int base, int bits);
static unsigned char eval_char_literal(const char *s);


/* Line 189 of yacc.c  */
#line 93 "dtc-parser.tab.c"

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

/* Enabling the token table.  */
#ifndef YYTOKEN_TABLE
# define YYTOKEN_TABLE 0
#endif


/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     DT_V1 = 258,
     DT_MEMRESERVE = 259,
     DT_LSHIFT = 260,
     DT_RSHIFT = 261,
     DT_LE = 262,
     DT_GE = 263,
     DT_EQ = 264,
     DT_NE = 265,
     DT_AND = 266,
     DT_OR = 267,
     DT_BITS = 268,
     DT_DEL_PROP = 269,
     DT_DEL_NODE = 270,
     DT_PROPNODENAME = 271,
     DT_LITERAL = 272,
     DT_CHAR_LITERAL = 273,
     DT_BASE = 274,
     DT_BYTE = 275,
     DT_STRING = 276,
     DT_LABEL = 277,
     DT_REF = 278,
     DT_INCBIN = 279
   };
#endif



#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
{

/* Line 214 of yacc.c  */
#line 40 "dtc-parser.y"

	char *propnodename;
	char *literal;
	char *labelref;
	unsigned int cbase;
	uint8_t byte;
	struct data data;

	struct {
		struct data	data;
		int		bits;
	} array;

	struct property *prop;
	struct property *proplist;
	struct node *node;
	struct node *nodelist;
	struct reserve_info *re;
	uint64_t integer;



/* Line 214 of yacc.c  */
#line 176 "dtc-parser.tab.c"
} YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
#endif


/* Copy the second part of user declarations.  */


/* Line 264 of yacc.c  */
#line 188 "dtc-parser.tab.c"

#ifdef short
# undef short
#endif

#ifdef YYTYPE_UINT8
typedef YYTYPE_UINT8 yytype_uint8;
#else
typedef unsigned char yytype_uint8;
#endif

#ifdef YYTYPE_INT8
typedef YYTYPE_INT8 yytype_int8;
#elif (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
typedef signed char yytype_int8;
#else
typedef short int yytype_int8;
#endif

#ifdef YYTYPE_UINT16
typedef YYTYPE_UINT16 yytype_uint16;
#else
typedef unsigned short int yytype_uint16;
#endif

#ifdef YYTYPE_INT16
typedef YYTYPE_INT16 yytype_int16;
#else
typedef short int yytype_int16;
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif ! defined YYSIZE_T && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned int
# endif
#endif

#define YYSIZE_MAXIMUM ((YYSIZE_T) -1)

#ifndef YY_
# if YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(msgid) dgettext ("bison-runtime", msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(msgid) msgid
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YYUSE(e) ((void) (e))
#else
# define YYUSE(e) /* empty */
#endif

/* Identity function, used to suppress warnings about constant conditions.  */
#ifndef lint
# define YYID(n) (n)
#else
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static int
YYID (int yyi)
#else
static int
YYID (yyi)
    int yyi;
#endif
{
  return yyi;
}
#endif

#if ! defined yyoverflow || YYERROR_VERBOSE

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#     ifndef _STDLIB_H
#      define _STDLIB_H 1
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's `empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (YYID (0))
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined _STDLIB_H \
       && ! ((defined YYMALLOC || defined malloc) \
	     && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef _STDLIB_H
#    define _STDLIB_H 1
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* ! defined yyoverflow || YYERROR_VERBOSE */


#if (! defined yyoverflow \
     && (! defined __cplusplus \
	 || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yytype_int16 yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (yytype_int16) + sizeof (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

/* Copy COUNT objects from FROM to TO.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(To, From, Count) \
      __builtin_memcpy (To, From, (Count) * sizeof (*(From)))
#  else
#   define YYCOPY(To, From, Count)		\
      do					\
	{					\
	  YYSIZE_T yyi;				\
	  for (yyi = 0; yyi < (Count); yyi++)	\
	    (To)[yyi] = (From)[yyi];		\
	}					\
      while (YYID (0))
#  endif
# endif

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)				\
    do									\
      {									\
	YYSIZE_T yynewbytes;						\
	YYCOPY (&yyptr->Stack_alloc, Stack, yysize);			\
	Stack = &yyptr->Stack_alloc;					\
	yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
	yyptr += yynewbytes / sizeof (*yyptr);				\
      }									\
    while (YYID (0))

#endif

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  4
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   133

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  48
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  28
/* YYNRULES -- Number of rules.  */
#define YYNRULES  79
/* YYNRULES -- Number of states.  */
#define YYNSTATES  141

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   279

#define YYTRANSLATE(YYX)						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const yytype_uint8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    47,     2,     2,     2,    45,    41,     2,
      33,    35,    44,    42,    34,    43,     2,    26,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    38,    25,
      36,    29,    30,    37,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,    31,     2,    32,    40,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    27,    39,    28,    46,     2,     2,     2,
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
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const yytype_uint16 yyprhs[] =
{
       0,     0,     3,     8,     9,    12,    17,    20,    23,    27,
      31,    36,    42,    43,    46,    51,    54,    58,    61,    64,
      68,    73,    76,    86,    92,    95,    96,    99,   102,   106,
     108,   111,   114,   117,   119,   121,   125,   127,   129,   135,
     137,   141,   143,   147,   149,   153,   155,   159,   161,   165,
     167,   171,   175,   177,   181,   185,   189,   193,   197,   201,
     203,   207,   211,   213,   217,   221,   225,   227,   229,   232,
     235,   238,   239,   242,   245,   246,   249,   252,   255,   259
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int8 yyrhs[] =
{
      49,     0,    -1,     3,    25,    50,    52,    -1,    -1,    51,
      50,    -1,     4,    59,    59,    25,    -1,    22,    51,    -1,
      26,    53,    -1,    52,    26,    53,    -1,    52,    23,    53,
      -1,    52,    15,    23,    25,    -1,    27,    54,    74,    28,
      25,    -1,    -1,    54,    55,    -1,    16,    29,    56,    25,
      -1,    16,    25,    -1,    14,    16,    25,    -1,    22,    55,
      -1,    57,    21,    -1,    57,    58,    30,    -1,    57,    31,
      73,    32,    -1,    57,    23,    -1,    57,    24,    33,    21,
      34,    59,    34,    59,    35,    -1,    57,    24,    33,    21,
      35,    -1,    56,    22,    -1,    -1,    56,    34,    -1,    57,
      22,    -1,    13,    17,    36,    -1,    36,    -1,    58,    59,
      -1,    58,    23,    -1,    58,    22,    -1,    17,    -1,    18,
      -1,    33,    60,    35,    -1,    61,    -1,    62,    -1,    62,
      37,    60,    38,    61,    -1,    63,    -1,    62,    12,    63,
      -1,    64,    -1,    63,    11,    64,    -1,    65,    -1,    64,
      39,    65,    -1,    66,    -1,    65,    40,    66,    -1,    67,
      -1,    66,    41,    67,    -1,    68,    -1,    67,     9,    68,
      -1,    67,    10,    68,    -1,    69,    -1,    68,    36,    69,
      -1,    68,    30,    69,    -1,    68,     7,    69,    -1,    68,
       8,    69,    -1,    69,     5,    70,    -1,    69,     6,    70,
      -1,    70,    -1,    70,    42,    71,    -1,    70,    43,    71,
      -1,    71,    -1,    71,    44,    72,    -1,    71,    26,    72,
      -1,    71,    45,    72,    -1,    72,    -1,    59,    -1,    43,
      72,    -1,    46,    72,    -1,    47,    72,    -1,    -1,    73,
      20,    -1,    73,    22,    -1,    -1,    75,    74,    -1,    75,
      55,    -1,    16,    53,    -1,    15,    16,    25,    -1,    22,
      75,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   109,   109,   118,   121,   128,   132,   140,   144,   148,
     158,   172,   180,   183,   190,   194,   198,   202,   210,   214,
     218,   222,   226,   243,   253,   261,   264,   268,   275,   290,
     295,   315,   329,   336,   340,   344,   351,   355,   356,   360,
     361,   365,   366,   370,   371,   375,   376,   380,   381,   385,
     386,   387,   391,   392,   393,   394,   395,   399,   400,   401,
     405,   406,   407,   411,   412,   413,   414,   418,   419,   420,
     421,   426,   429,   433,   441,   444,   448,   456,   460,   464
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || YYTOKEN_TABLE
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "DT_V1", "DT_MEMRESERVE", "DT_LSHIFT",
  "DT_RSHIFT", "DT_LE", "DT_GE", "DT_EQ", "DT_NE", "DT_AND", "DT_OR",
  "DT_BITS", "DT_DEL_PROP", "DT_DEL_NODE", "DT_PROPNODENAME", "DT_LITERAL",
  "DT_CHAR_LITERAL", "DT_BASE", "DT_BYTE", "DT_STRING", "DT_LABEL",
  "DT_REF", "DT_INCBIN", "';'", "'/'", "'{'", "'}'", "'='", "'>'", "'['",
  "']'", "'('", "','", "')'", "'<'", "'?'", "':'", "'|'", "'^'", "'&'",
  "'+'", "'-'", "'*'", "'%'", "'~'", "'!'", "$accept", "sourcefile",
  "memreserves", "memreserve", "devicetree", "nodedef", "proplist",
  "propdef", "propdata", "propdataprefix", "arrayprefix", "integer_prim",
  "integer_expr", "integer_trinary", "integer_or", "integer_and",
  "integer_bitor", "integer_bitxor", "integer_bitand", "integer_eq",
  "integer_rela", "integer_shift", "integer_add", "integer_mul",
  "integer_unary", "bytestring", "subnodes", "subnode", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const yytype_uint16 yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,    59,    47,   123,   125,    61,
      62,    91,    93,    40,    44,    41,    60,    63,    58,   124,
      94,    38,    43,    45,    42,    37,   126,    33
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,    48,    49,    50,    50,    51,    51,    52,    52,    52,
      52,    53,    54,    54,    55,    55,    55,    55,    56,    56,
      56,    56,    56,    56,    56,    57,    57,    57,    58,    58,
      58,    58,    58,    59,    59,    59,    60,    61,    61,    62,
      62,    63,    63,    64,    64,    65,    65,    66,    66,    67,
      67,    67,    68,    68,    68,    68,    68,    69,    69,    69,
      70,    70,    70,    71,    71,    71,    71,    72,    72,    72,
      72,    73,    73,    73,    74,    74,    74,    75,    75,    75
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     4,     0,     2,     4,     2,     2,     3,     3,
       4,     5,     0,     2,     4,     2,     3,     2,     2,     3,
       4,     2,     9,     5,     2,     0,     2,     2,     3,     1,
       2,     2,     2,     1,     1,     3,     1,     1,     5,     1,
       3,     1,     3,     1,     3,     1,     3,     1,     3,     1,
       3,     3,     1,     3,     3,     3,     3,     3,     3,     1,
       3,     3,     1,     3,     3,     3,     1,     1,     2,     2,
       2,     0,     2,     2,     0,     2,     2,     2,     3,     2
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       0,     0,     0,     3,     1,     0,     0,     0,     3,    33,
      34,     0,     0,     6,     0,     2,     4,     0,     0,     0,
      67,     0,    36,    37,    39,    41,    43,    45,    47,    49,
      52,    59,    62,    66,     0,    12,     7,     0,     0,     0,
      68,    69,    70,    35,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     5,    74,     0,     9,     8,    40,     0,
      42,    44,    46,    48,    50,    51,    55,    56,    54,    53,
      57,    58,    60,    61,    64,    63,    65,     0,     0,     0,
       0,    13,     0,    74,    10,     0,     0,     0,    15,    25,
      77,    17,    79,     0,    76,    75,    38,    16,    78,     0,
       0,    11,    24,    14,    26,     0,    18,    27,    21,     0,
      71,    29,     0,     0,     0,     0,    32,    31,    19,    30,
      28,     0,    72,    73,    20,     0,    23,     0,     0,     0,
      22
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int8 yydefgoto[] =
{
      -1,     2,     7,     8,    15,    36,    64,    91,   109,   110,
     122,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    32,    33,   125,    92,    93
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -78
static const yytype_int8 yypact[] =
{
      22,    11,    51,    10,   -78,    23,    10,     2,    10,   -78,
     -78,    -9,    23,   -78,    30,    38,   -78,    -9,    -9,    -9,
     -78,    35,   -78,    -6,    52,    29,    48,    49,    33,     3,
      71,    36,     0,   -78,    64,   -78,   -78,    68,    30,    30,
     -78,   -78,   -78,   -78,    -9,    -9,    -9,    -9,    -9,    -9,
      -9,    -9,    -9,    -9,    -9,    -9,    -9,    -9,    -9,    -9,
      -9,    -9,    -9,   -78,    44,    67,   -78,   -78,    52,    55,
      29,    48,    49,    33,     3,     3,    71,    71,    71,    71,
      36,    36,     0,     0,   -78,   -78,   -78,    78,    79,    42,
      44,   -78,    69,    44,   -78,    -9,    73,    74,   -78,   -78,
     -78,   -78,   -78,    75,   -78,   -78,   -78,   -78,   -78,    -7,
      -1,   -78,   -78,   -78,   -78,    84,   -78,   -78,   -78,    63,
     -78,   -78,    32,    66,    82,    -3,   -78,   -78,   -78,   -78,
     -78,    46,   -78,   -78,   -78,    23,   -78,    70,    23,    72,
     -78
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int8 yypgoto[] =
{
     -78,   -78,    97,   100,   -78,   -37,   -78,   -77,   -78,   -78,
     -78,    -5,    65,    13,   -78,    76,    77,    62,    80,    83,
      34,    20,    26,    28,   -14,   -78,    18,    24
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -1
static const yytype_uint8 yytable[] =
{
      12,    66,    67,    40,    41,    42,    44,    34,     9,    10,
      52,    53,   115,   101,     5,   112,   104,   132,   113,   133,
     116,   117,   118,   119,    11,     1,    60,   114,    14,   134,
     120,    45,     6,    54,    17,   121,     3,    18,    19,    55,
       9,    10,    50,    51,    61,    62,    84,    85,    86,     9,
      10,     4,   100,    37,   126,   127,    11,    35,    87,    88,
      89,    38,   128,    46,    39,    11,    90,    98,    47,    35,
      43,    99,    76,    77,    78,    79,    56,    57,    58,    59,
     135,   136,    80,    81,    74,    75,    82,    83,    48,    63,
      49,    65,    94,    95,    96,    97,   124,   103,   107,   108,
     111,   123,   130,   131,   138,    16,    13,   140,   106,    71,
      69,   105,     0,     0,   102,     0,     0,   129,     0,     0,
      68,     0,     0,    70,     0,     0,     0,     0,    72,     0,
     137,     0,    73,   139
};

static const yytype_int16 yycheck[] =
{
       5,    38,    39,    17,    18,    19,    12,    12,    17,    18,
       7,     8,    13,    90,     4,    22,    93,    20,    25,    22,
      21,    22,    23,    24,    33,     3,    26,    34,    26,    32,
      31,    37,    22,    30,    43,    36,    25,    46,    47,    36,
      17,    18,     9,    10,    44,    45,    60,    61,    62,    17,
      18,     0,    89,    15,    22,    23,    33,    27,    14,    15,
      16,    23,    30,    11,    26,    33,    22,    25,    39,    27,
      35,    29,    52,    53,    54,    55,     5,     6,    42,    43,
      34,    35,    56,    57,    50,    51,    58,    59,    40,    25,
      41,    23,    25,    38,    16,    16,    33,    28,    25,    25,
      25,    17,    36,    21,    34,     8,     6,    35,    95,    47,
      45,    93,    -1,    -1,    90,    -1,    -1,   122,    -1,    -1,
      44,    -1,    -1,    46,    -1,    -1,    -1,    -1,    48,    -1,
     135,    -1,    49,   138
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,     3,    49,    25,     0,     4,    22,    50,    51,    17,
      18,    33,    59,    51,    26,    52,    50,    43,    46,    47,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    59,    27,    53,    15,    23,    26,
      72,    72,    72,    35,    12,    37,    11,    39,    40,    41,
       9,    10,     7,     8,    30,    36,     5,     6,    42,    43,
      26,    44,    45,    25,    54,    23,    53,    53,    63,    60,
      64,    65,    66,    67,    68,    68,    69,    69,    69,    69,
      70,    70,    71,    71,    72,    72,    72,    14,    15,    16,
      22,    55,    74,    75,    25,    38,    16,    16,    25,    29,
      53,    55,    75,    28,    55,    74,    61,    25,    25,    56,
      57,    25,    22,    25,    34,    13,    21,    22,    23,    24,
      31,    36,    58,    17,    33,    73,    22,    23,    30,    59,
      36,    21,    20,    22,    32,    34,    35,    59,    34,    59,
      35
};

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
      YYPOPSTACK (1);						\
      goto yybackup;						\
    }								\
  else								\
    {								\
      yyerror (YY_("syntax error: cannot back up")); \
      YYERROR;							\
    }								\
while (YYID (0))


#define YYTERROR	1
#define YYERRCODE	256


/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#define YYRHSLOC(Rhs, K) ((Rhs)[K])
#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)				\
    do									\
      if (YYID (N))                                                    \
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
    while (YYID (0))
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
} while (YYID (0))

# define YY_SYMBOL_PRINT(Title, Type, Value, Location)			  \
do {									  \
  if (yydebug)								  \
    {									  \
      YYFPRINTF (stderr, "%s ", Title);					  \
      yy_symbol_print (stderr,						  \
		  Type, Value); \
      YYFPRINTF (stderr, "\n");						  \
    }									  \
} while (YYID (0))


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_symbol_value_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
#else
static void
yy_symbol_value_print (yyoutput, yytype, yyvaluep)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
#endif
{
  if (!yyvaluep)
    return;
# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# else
  YYUSE (yyoutput);
# endif
  switch (yytype)
    {
      default:
	break;
    }
}


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_symbol_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
#else
static void
yy_symbol_print (yyoutput, yytype, yyvaluep)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
#endif
{
  if (yytype < YYNTOKENS)
    YYFPRINTF (yyoutput, "token %s (", yytname[yytype]);
  else
    YYFPRINTF (yyoutput, "nterm %s (", yytname[yytype]);

  yy_symbol_value_print (yyoutput, yytype, yyvaluep);
  YYFPRINTF (yyoutput, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_stack_print (yytype_int16 *yybottom, yytype_int16 *yytop)
#else
static void
yy_stack_print (yybottom, yytop)
    yytype_int16 *yybottom;
    yytype_int16 *yytop;
#endif
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)				\
do {								\
  if (yydebug)							\
    yy_stack_print ((Bottom), (Top));				\
} while (YYID (0))


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_reduce_print (YYSTYPE *yyvsp, int yyrule)
#else
static void
yy_reduce_print (yyvsp, yyrule)
    YYSTYPE *yyvsp;
    int yyrule;
#endif
{
  int yynrhs = yyr2[yyrule];
  int yyi;
  unsigned long int yylno = yyrline[yyrule];
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %lu):\n",
	     yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr, yyrhs[yyprhs[yyrule] + yyi],
		       &(yyvsp[(yyi + 1) - (yynrhs)])
		       		       );
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)		\
do {					\
  if (yydebug)				\
    yy_reduce_print (yyvsp, Rule); \
} while (YYID (0))

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
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif



#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined __GLIBC__ && defined _STRING_H
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static YYSIZE_T
yystrlen (const char *yystr)
#else
static YYSIZE_T
yystrlen (yystr)
    const char *yystr;
#endif
{
  YYSIZE_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static char *
yystpcpy (char *yydest, const char *yysrc)
#else
static char *
yystpcpy (yydest, yysrc)
    char *yydest;
    const char *yysrc;
#endif
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif

# ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYSIZE_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYSIZE_T yyn = 0;
      char const *yyp = yystr;

      for (;;)
	switch (*++yyp)
	  {
	  case '\'':
	  case ',':
	    goto do_not_strip_quotes;

	  case '\\':
	    if (*++yyp != '\\')
	      goto do_not_strip_quotes;
	    /* Fall through.  */
	  default:
	    if (yyres)
	      yyres[yyn] = *yyp;
	    yyn++;
	    break;

	  case '"':
	    if (yyres)
	      yyres[yyn] = '\0';
	    return yyn;
	  }
    do_not_strip_quotes: ;
    }

  if (! yyres)
    return yystrlen (yystr);

  return yystpcpy (yyres, yystr) - yyres;
}
# endif

/* Copy into YYRESULT an error message about the unexpected token
   YYCHAR while in state YYSTATE.  Return the number of bytes copied,
   including the terminating null byte.  If YYRESULT is null, do not
   copy anything; just return the number of bytes that would be
   copied.  As a special case, return 0 if an ordinary "syntax error"
   message will do.  Return YYSIZE_MAXIMUM if overflow occurs during
   size calculation.  */
static YYSIZE_T
yysyntax_error (char *yyresult, int yystate, int yychar)
{
  int yyn = yypact[yystate];

  if (! (YYPACT_NINF < yyn && yyn <= YYLAST))
    return 0;
  else
    {
      int yytype = YYTRANSLATE (yychar);
      YYSIZE_T yysize0 = yytnamerr (0, yytname[yytype]);
      YYSIZE_T yysize = yysize0;
      YYSIZE_T yysize1;
      int yysize_overflow = 0;
      enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
      char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
      int yyx;

# if 0
      /* This is so xgettext sees the translatable formats that are
	 constructed on the fly.  */
      YY_("syntax error, unexpected %s");
      YY_("syntax error, unexpected %s, expecting %s");
      YY_("syntax error, unexpected %s, expecting %s or %s");
      YY_("syntax error, unexpected %s, expecting %s or %s or %s");
      YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s");
# endif
      char *yyfmt;
      char const *yyf;
      static char const yyunexpected[] = "syntax error, unexpected %s";
      static char const yyexpecting[] = ", expecting %s";
      static char const yyor[] = " or %s";
      char yyformat[sizeof yyunexpected
		    + sizeof yyexpecting - 1
		    + ((YYERROR_VERBOSE_ARGS_MAXIMUM - 2)
		       * (sizeof yyor - 1))];
      char const *yyprefix = yyexpecting;

      /* Start YYX at -YYN if negative to avoid negative indexes in
	 YYCHECK.  */
      int yyxbegin = yyn < 0 ? -yyn : 0;

      /* Stay within bounds of both yycheck and yytname.  */
      int yychecklim = YYLAST - yyn + 1;
      int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
      int yycount = 1;

      yyarg[0] = yytname[yytype];
      yyfmt = yystpcpy (yyformat, yyunexpected);

      for (yyx = yyxbegin; yyx < yyxend; ++yyx)
	if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
	  {
	    if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
	      {
		yycount = 1;
		yysize = yysize0;
		yyformat[sizeof yyunexpected - 1] = '\0';
		break;
	      }
	    yyarg[yycount++] = yytname[yyx];
	    yysize1 = yysize + yytnamerr (0, yytname[yyx]);
	    yysize_overflow |= (yysize1 < yysize);
	    yysize = yysize1;
	    yyfmt = yystpcpy (yyfmt, yyprefix);
	    yyprefix = yyor;
	  }

      yyf = YY_(yyformat);
      yysize1 = yysize + yystrlen (yyf);
      yysize_overflow |= (yysize1 < yysize);
      yysize = yysize1;

      if (yysize_overflow)
	return YYSIZE_MAXIMUM;

      if (yyresult)
	{
	  /* Avoid sprintf, as that infringes on the user's name space.
	     Don't have undefined behavior even if the translation
	     produced a string with the wrong number of "%s"s.  */
	  char *yyp = yyresult;
	  int yyi = 0;
	  while ((*yyp = *yyf) != '\0')
	    {
	      if (*yyp == '%' && yyf[1] == 's' && yyi < yycount)
		{
		  yyp += yytnamerr (yyp, yyarg[yyi++]);
		  yyf += 2;
		}
	      else
		{
		  yyp++;
		  yyf++;
		}
	    }
	}
      return yysize;
    }
}
#endif /* YYERROR_VERBOSE */


/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
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
  YYUSE (yyvaluep);

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
#if defined __STDC__ || defined __cplusplus
int yyparse (void *YYPARSE_PARAM);
#else
int yyparse ();
#endif
#else /* ! YYPARSE_PARAM */
#if defined __STDC__ || defined __cplusplus
int yyparse (void);
#else
int yyparse ();
#endif
#endif /* ! YYPARSE_PARAM */


/* The lookahead symbol.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;

/* Number of syntax errors so far.  */
int yynerrs;



/*-------------------------.
| yyparse or yypush_parse.  |
`-------------------------*/

#ifdef YYPARSE_PARAM
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
int
yyparse (void *YYPARSE_PARAM)
#else
int
yyparse (YYPARSE_PARAM)
    void *YYPARSE_PARAM;
#endif
#else /* ! YYPARSE_PARAM */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
int
yyparse (void)
#else
int
yyparse ()

#endif
#endif
{


    int yystate;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus;

    /* The stacks and their tools:
       `yyss': related to states.
       `yyvs': related to semantic values.

       Refer to the stacks thru separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* The state stack.  */
    yytype_int16 yyssa[YYINITDEPTH];
    yytype_int16 *yyss;
    yytype_int16 *yyssp;

    /* The semantic value stack.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs;
    YYSTYPE *yyvsp;

    YYSIZE_T yystacksize;

  int yyn;
  int yyresult;
  /* Lookahead token as an internal (translated) token number.  */
  int yytoken;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;

#if YYERROR_VERBOSE
  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYSIZE_T yymsg_alloc = sizeof yymsgbuf;
#endif

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  yytoken = 0;
  yyss = yyssa;
  yyvs = yyvsa;
  yystacksize = YYINITDEPTH;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY; /* Cause a token to be read.  */

  /* Initialize stack pointers.
     Waste one element of value and location stack
     so that they stay on the same level as the state stack.
     The wasted elements are never initialized.  */
  yyssp = yyss;
  yyvsp = yyvs;

  goto yysetstate;

/*------------------------------------------------------------.
| yynewstate -- Push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
 yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyss + yystacksize - 1 <= yyssp)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
	/* Give user a chance to reallocate the stack.  Use copies of
	   these so that the &'s don't force the real ones into
	   memory.  */
	YYSTYPE *yyvs1 = yyvs;
	yytype_int16 *yyss1 = yyss;

	/* Each stack pointer address is followed by the size of the
	   data in use in that stack, in bytes.  This used to be a
	   conditional around just the two extra args, but that might
	   be undefined if yyoverflow is a macro.  */
	yyoverflow (YY_("memory exhausted"),
		    &yyss1, yysize * sizeof (*yyssp),
		    &yyvs1, yysize * sizeof (*yyvsp),
		    &yystacksize);

	yyss = yyss1;
	yyvs = yyvs1;
      }
#else /* no yyoverflow */
# ifndef YYSTACK_RELOCATE
      goto yyexhaustedlab;
# else
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
	goto yyexhaustedlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
	yystacksize = YYMAXDEPTH;

      {
	yytype_int16 *yyss1 = yyss;
	union yyalloc *yyptr =
	  (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
	if (! yyptr)
	  goto yyexhaustedlab;
	YYSTACK_RELOCATE (yyss_alloc, yyss);
	YYSTACK_RELOCATE (yyvs_alloc, yyvs);
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

  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;

/*-----------.
| yybackup.  |
`-----------*/
yybackup:

  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yyn == YYPACT_NINF)
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid lookahead symbol.  */
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

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);

  /* Discard the shifted token.  */
  yychar = YYEMPTY;

  yystate = yyn;
  *++yyvsp = yylval;

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
        case 2:

/* Line 1455 of yacc.c  */
#line 110 "dtc-parser.y"
    {
			the_boot_info = build_boot_info((yyvsp[(3) - (4)].re), (yyvsp[(4) - (4)].node),
							guess_boot_cpuid((yyvsp[(4) - (4)].node)));
		;}
    break;

  case 3:

/* Line 1455 of yacc.c  */
#line 118 "dtc-parser.y"
    {
			(yyval.re) = NULL;
		;}
    break;

  case 4:

/* Line 1455 of yacc.c  */
#line 122 "dtc-parser.y"
    {
			(yyval.re) = chain_reserve_entry((yyvsp[(1) - (2)].re), (yyvsp[(2) - (2)].re));
		;}
    break;

  case 5:

/* Line 1455 of yacc.c  */
#line 129 "dtc-parser.y"
    {
			(yyval.re) = build_reserve_entry((yyvsp[(2) - (4)].integer), (yyvsp[(3) - (4)].integer));
		;}
    break;

  case 6:

/* Line 1455 of yacc.c  */
#line 133 "dtc-parser.y"
    {
			add_label(&(yyvsp[(2) - (2)].re)->labels, (yyvsp[(1) - (2)].labelref));
			(yyval.re) = (yyvsp[(2) - (2)].re);
		;}
    break;

  case 7:

/* Line 1455 of yacc.c  */
#line 141 "dtc-parser.y"
    {
			(yyval.node) = name_node((yyvsp[(2) - (2)].node), "");
		;}
    break;

  case 8:

/* Line 1455 of yacc.c  */
#line 145 "dtc-parser.y"
    {
			(yyval.node) = merge_nodes((yyvsp[(1) - (3)].node), (yyvsp[(3) - (3)].node));
		;}
    break;

  case 9:

/* Line 1455 of yacc.c  */
#line 149 "dtc-parser.y"
    {
			struct node *target = get_node_by_ref((yyvsp[(1) - (3)].node), (yyvsp[(2) - (3)].labelref));

			if (target)
				merge_nodes(target, (yyvsp[(3) - (3)].node));
			else
				print_error("label or path, '%s', not found", (yyvsp[(2) - (3)].labelref));
			(yyval.node) = (yyvsp[(1) - (3)].node);
		;}
    break;

  case 10:

/* Line 1455 of yacc.c  */
#line 159 "dtc-parser.y"
    {
			struct node *target = get_node_by_ref((yyvsp[(1) - (4)].node), (yyvsp[(3) - (4)].labelref));

			if (!target)
				print_error("label or path, '%s', not found", (yyvsp[(3) - (4)].labelref));
			else
				delete_node(target);

			(yyval.node) = (yyvsp[(1) - (4)].node);
		;}
    break;

  case 11:

/* Line 1455 of yacc.c  */
#line 173 "dtc-parser.y"
    {
			(yyval.node) = build_node((yyvsp[(2) - (5)].proplist), (yyvsp[(3) - (5)].nodelist));
		;}
    break;

  case 12:

/* Line 1455 of yacc.c  */
#line 180 "dtc-parser.y"
    {
			(yyval.proplist) = NULL;
		;}
    break;

  case 13:

/* Line 1455 of yacc.c  */
#line 184 "dtc-parser.y"
    {
			(yyval.proplist) = chain_property((yyvsp[(2) - (2)].prop), (yyvsp[(1) - (2)].proplist));
		;}
    break;

  case 14:

/* Line 1455 of yacc.c  */
#line 191 "dtc-parser.y"
    {
			(yyval.prop) = build_property((yyvsp[(1) - (4)].propnodename), (yyvsp[(3) - (4)].data));
		;}
    break;

  case 15:

/* Line 1455 of yacc.c  */
#line 195 "dtc-parser.y"
    {
			(yyval.prop) = build_property((yyvsp[(1) - (2)].propnodename), empty_data);
		;}
    break;

  case 16:

/* Line 1455 of yacc.c  */
#line 199 "dtc-parser.y"
    {
			(yyval.prop) = build_property_delete((yyvsp[(2) - (3)].propnodename));
		;}
    break;

  case 17:

/* Line 1455 of yacc.c  */
#line 203 "dtc-parser.y"
    {
			add_label(&(yyvsp[(2) - (2)].prop)->labels, (yyvsp[(1) - (2)].labelref));
			(yyval.prop) = (yyvsp[(2) - (2)].prop);
		;}
    break;

  case 18:

/* Line 1455 of yacc.c  */
#line 211 "dtc-parser.y"
    {
			(yyval.data) = data_merge((yyvsp[(1) - (2)].data), (yyvsp[(2) - (2)].data));
		;}
    break;

  case 19:

/* Line 1455 of yacc.c  */
#line 215 "dtc-parser.y"
    {
			(yyval.data) = data_merge((yyvsp[(1) - (3)].data), (yyvsp[(2) - (3)].array).data);
		;}
    break;

  case 20:

/* Line 1455 of yacc.c  */
#line 219 "dtc-parser.y"
    {
			(yyval.data) = data_merge((yyvsp[(1) - (4)].data), (yyvsp[(3) - (4)].data));
		;}
    break;

  case 21:

/* Line 1455 of yacc.c  */
#line 223 "dtc-parser.y"
    {
			(yyval.data) = data_add_marker((yyvsp[(1) - (2)].data), REF_PATH, (yyvsp[(2) - (2)].labelref));
		;}
    break;

  case 22:

/* Line 1455 of yacc.c  */
#line 227 "dtc-parser.y"
    {
			FILE *f = srcfile_relative_open((yyvsp[(4) - (9)].data).val, NULL);
			struct data d;

			if ((yyvsp[(6) - (9)].integer) != 0)
				if (fseek(f, (yyvsp[(6) - (9)].integer), SEEK_SET) != 0)
					print_error("Couldn't seek to offset %llu in \"%s\": %s",
						     (unsigned long long)(yyvsp[(6) - (9)].integer),
						     (yyvsp[(4) - (9)].data).val,
						     strerror(errno));

			d = data_copy_file(f, (yyvsp[(8) - (9)].integer));

			(yyval.data) = data_merge((yyvsp[(1) - (9)].data), d);
			fclose(f);
		;}
    break;

  case 23:

/* Line 1455 of yacc.c  */
#line 244 "dtc-parser.y"
    {
			FILE *f = srcfile_relative_open((yyvsp[(4) - (5)].data).val, NULL);
			struct data d = empty_data;

			d = data_copy_file(f, -1);

			(yyval.data) = data_merge((yyvsp[(1) - (5)].data), d);
			fclose(f);
		;}
    break;

  case 24:

/* Line 1455 of yacc.c  */
#line 254 "dtc-parser.y"
    {
			(yyval.data) = data_add_marker((yyvsp[(1) - (2)].data), LABEL, (yyvsp[(2) - (2)].labelref));
		;}
    break;

  case 25:

/* Line 1455 of yacc.c  */
#line 261 "dtc-parser.y"
    {
			(yyval.data) = empty_data;
		;}
    break;

  case 26:

/* Line 1455 of yacc.c  */
#line 265 "dtc-parser.y"
    {
			(yyval.data) = (yyvsp[(1) - (2)].data);
		;}
    break;

  case 27:

/* Line 1455 of yacc.c  */
#line 269 "dtc-parser.y"
    {
			(yyval.data) = data_add_marker((yyvsp[(1) - (2)].data), LABEL, (yyvsp[(2) - (2)].labelref));
		;}
    break;

  case 28:

/* Line 1455 of yacc.c  */
#line 276 "dtc-parser.y"
    {
			(yyval.array).data = empty_data;
			(yyval.array).bits = eval_literal((yyvsp[(2) - (3)].literal), 0, 7);

			if (((yyval.array).bits !=  8) &&
			    ((yyval.array).bits != 16) &&
			    ((yyval.array).bits != 32) &&
			    ((yyval.array).bits != 64))
			{
				print_error("Only 8, 16, 32 and 64-bit elements"
					    " are currently supported");
				(yyval.array).bits = 32;
			}
		;}
    break;

  case 29:

/* Line 1455 of yacc.c  */
#line 291 "dtc-parser.y"
    {
			(yyval.array).data = empty_data;
			(yyval.array).bits = 32;
		;}
    break;

  case 30:

/* Line 1455 of yacc.c  */
#line 296 "dtc-parser.y"
    {
			if ((yyvsp[(1) - (2)].array).bits < 64) {
				uint64_t mask = (1ULL << (yyvsp[(1) - (2)].array).bits) - 1;
				/*
				 * Bits above mask must either be all zero
				 * (positive within range of mask) or all one
				 * (negative and sign-extended). The second
				 * condition is true if when we set all bits
				 * within the mask to one (i.e. | in the
				 * mask), all bits are one.
				 */
				if (((yyvsp[(2) - (2)].integer) > mask) && (((yyvsp[(2) - (2)].integer) | mask) != -1ULL))
					print_error(
						"integer value out of range "
						"%016lx (%d bits)", (yyvsp[(1) - (2)].array).bits);
			}

			(yyval.array).data = data_append_integer((yyvsp[(1) - (2)].array).data, (yyvsp[(2) - (2)].integer), (yyvsp[(1) - (2)].array).bits);
		;}
    break;

  case 31:

/* Line 1455 of yacc.c  */
#line 316 "dtc-parser.y"
    {
			uint64_t val = ~0ULL >> (64 - (yyvsp[(1) - (2)].array).bits);

			if ((yyvsp[(1) - (2)].array).bits == 32)
				(yyvsp[(1) - (2)].array).data = data_add_marker((yyvsp[(1) - (2)].array).data,
							  REF_PHANDLE,
							  (yyvsp[(2) - (2)].labelref));
			else
				print_error("References are only allowed in "
					    "arrays with 32-bit elements.");

			(yyval.array).data = data_append_integer((yyvsp[(1) - (2)].array).data, val, (yyvsp[(1) - (2)].array).bits);
		;}
    break;

  case 32:

/* Line 1455 of yacc.c  */
#line 330 "dtc-parser.y"
    {
			(yyval.array).data = data_add_marker((yyvsp[(1) - (2)].array).data, LABEL, (yyvsp[(2) - (2)].labelref));
		;}
    break;

  case 33:

/* Line 1455 of yacc.c  */
#line 337 "dtc-parser.y"
    {
			(yyval.integer) = eval_literal((yyvsp[(1) - (1)].literal), 0, 64);
		;}
    break;

  case 34:

/* Line 1455 of yacc.c  */
#line 341 "dtc-parser.y"
    {
			(yyval.integer) = eval_char_literal((yyvsp[(1) - (1)].literal));
		;}
    break;

  case 35:

/* Line 1455 of yacc.c  */
#line 345 "dtc-parser.y"
    {
			(yyval.integer) = (yyvsp[(2) - (3)].integer);
		;}
    break;

  case 38:

/* Line 1455 of yacc.c  */
#line 356 "dtc-parser.y"
    { (yyval.integer) = (yyvsp[(1) - (5)].integer) ? (yyvsp[(3) - (5)].integer) : (yyvsp[(5) - (5)].integer); ;}
    break;

  case 40:

/* Line 1455 of yacc.c  */
#line 361 "dtc-parser.y"
    { (yyval.integer) = (yyvsp[(1) - (3)].integer) || (yyvsp[(3) - (3)].integer); ;}
    break;

  case 42:

/* Line 1455 of yacc.c  */
#line 366 "dtc-parser.y"
    { (yyval.integer) = (yyvsp[(1) - (3)].integer) && (yyvsp[(3) - (3)].integer); ;}
    break;

  case 44:

/* Line 1455 of yacc.c  */
#line 371 "dtc-parser.y"
    { (yyval.integer) = (yyvsp[(1) - (3)].integer) | (yyvsp[(3) - (3)].integer); ;}
    break;

  case 46:

/* Line 1455 of yacc.c  */
#line 376 "dtc-parser.y"
    { (yyval.integer) = (yyvsp[(1) - (3)].integer) ^ (yyvsp[(3) - (3)].integer); ;}
    break;

  case 48:

/* Line 1455 of yacc.c  */
#line 381 "dtc-parser.y"
    { (yyval.integer) = (yyvsp[(1) - (3)].integer) & (yyvsp[(3) - (3)].integer); ;}
    break;

  case 50:

/* Line 1455 of yacc.c  */
#line 386 "dtc-parser.y"
    { (yyval.integer) = (yyvsp[(1) - (3)].integer) == (yyvsp[(3) - (3)].integer); ;}
    break;

  case 51:

/* Line 1455 of yacc.c  */
#line 387 "dtc-parser.y"
    { (yyval.integer) = (yyvsp[(1) - (3)].integer) != (yyvsp[(3) - (3)].integer); ;}
    break;

  case 53:

/* Line 1455 of yacc.c  */
#line 392 "dtc-parser.y"
    { (yyval.integer) = (yyvsp[(1) - (3)].integer) < (yyvsp[(3) - (3)].integer); ;}
    break;

  case 54:

/* Line 1455 of yacc.c  */
#line 393 "dtc-parser.y"
    { (yyval.integer) = (yyvsp[(1) - (3)].integer) > (yyvsp[(3) - (3)].integer); ;}
    break;

  case 55:

/* Line 1455 of yacc.c  */
#line 394 "dtc-parser.y"
    { (yyval.integer) = (yyvsp[(1) - (3)].integer) <= (yyvsp[(3) - (3)].integer); ;}
    break;

  case 56:

/* Line 1455 of yacc.c  */
#line 395 "dtc-parser.y"
    { (yyval.integer) = (yyvsp[(1) - (3)].integer) >= (yyvsp[(3) - (3)].integer); ;}
    break;

  case 57:

/* Line 1455 of yacc.c  */
#line 399 "dtc-parser.y"
    { (yyval.integer) = (yyvsp[(1) - (3)].integer) << (yyvsp[(3) - (3)].integer); ;}
    break;

  case 58:

/* Line 1455 of yacc.c  */
#line 400 "dtc-parser.y"
    { (yyval.integer) = (yyvsp[(1) - (3)].integer) >> (yyvsp[(3) - (3)].integer); ;}
    break;

  case 60:

/* Line 1455 of yacc.c  */
#line 405 "dtc-parser.y"
    { (yyval.integer) = (yyvsp[(1) - (3)].integer) + (yyvsp[(3) - (3)].integer); ;}
    break;

  case 61:

/* Line 1455 of yacc.c  */
#line 406 "dtc-parser.y"
    { (yyval.integer) = (yyvsp[(1) - (3)].integer) - (yyvsp[(3) - (3)].integer); ;}
    break;

  case 63:

/* Line 1455 of yacc.c  */
#line 411 "dtc-parser.y"
    { (yyval.integer) = (yyvsp[(1) - (3)].integer) * (yyvsp[(3) - (3)].integer); ;}
    break;

  case 64:

/* Line 1455 of yacc.c  */
#line 412 "dtc-parser.y"
    { (yyval.integer) = (yyvsp[(1) - (3)].integer) / (yyvsp[(3) - (3)].integer); ;}
    break;

  case 65:

/* Line 1455 of yacc.c  */
#line 413 "dtc-parser.y"
    { (yyval.integer) = (yyvsp[(1) - (3)].integer) % (yyvsp[(3) - (3)].integer); ;}
    break;

  case 68:

/* Line 1455 of yacc.c  */
#line 419 "dtc-parser.y"
    { (yyval.integer) = -(yyvsp[(2) - (2)].integer); ;}
    break;

  case 69:

/* Line 1455 of yacc.c  */
#line 420 "dtc-parser.y"
    { (yyval.integer) = ~(yyvsp[(2) - (2)].integer); ;}
    break;

  case 70:

/* Line 1455 of yacc.c  */
#line 421 "dtc-parser.y"
    { (yyval.integer) = !(yyvsp[(2) - (2)].integer); ;}
    break;

  case 71:

/* Line 1455 of yacc.c  */
#line 426 "dtc-parser.y"
    {
			(yyval.data) = empty_data;
		;}
    break;

  case 72:

/* Line 1455 of yacc.c  */
#line 430 "dtc-parser.y"
    {
			(yyval.data) = data_append_byte((yyvsp[(1) - (2)].data), (yyvsp[(2) - (2)].byte));
		;}
    break;

  case 73:

/* Line 1455 of yacc.c  */
#line 434 "dtc-parser.y"
    {
			(yyval.data) = data_add_marker((yyvsp[(1) - (2)].data), LABEL, (yyvsp[(2) - (2)].labelref));
		;}
    break;

  case 74:

/* Line 1455 of yacc.c  */
#line 441 "dtc-parser.y"
    {
			(yyval.nodelist) = NULL;
		;}
    break;

  case 75:

/* Line 1455 of yacc.c  */
#line 445 "dtc-parser.y"
    {
			(yyval.nodelist) = chain_node((yyvsp[(1) - (2)].node), (yyvsp[(2) - (2)].nodelist));
		;}
    break;

  case 76:

/* Line 1455 of yacc.c  */
#line 449 "dtc-parser.y"
    {
			print_error("syntax error: properties must precede subnodes");
			YYERROR;
		;}
    break;

  case 77:

/* Line 1455 of yacc.c  */
#line 457 "dtc-parser.y"
    {
			(yyval.node) = name_node((yyvsp[(2) - (2)].node), (yyvsp[(1) - (2)].propnodename));
		;}
    break;

  case 78:

/* Line 1455 of yacc.c  */
#line 461 "dtc-parser.y"
    {
			(yyval.node) = name_node(build_node_delete(), (yyvsp[(2) - (3)].propnodename));
		;}
    break;

  case 79:

/* Line 1455 of yacc.c  */
#line 465 "dtc-parser.y"
    {
			add_label(&(yyvsp[(2) - (2)].node)->labels, (yyvsp[(1) - (2)].labelref));
			(yyval.node) = (yyvsp[(2) - (2)].node);
		;}
    break;



/* Line 1455 of yacc.c  */
#line 2124 "dtc-parser.tab.c"
      default: break;
    }
  YY_SYMBOL_PRINT ("-> $$ =", yyr1[yyn], &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;
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
#if ! YYERROR_VERBOSE
      yyerror (YY_("syntax error"));
#else
      {
	YYSIZE_T yysize = yysyntax_error (0, yystate, yychar);
	if (yymsg_alloc < yysize && yymsg_alloc < YYSTACK_ALLOC_MAXIMUM)
	  {
	    YYSIZE_T yyalloc = 2 * yysize;
	    if (! (yysize <= yyalloc && yyalloc <= YYSTACK_ALLOC_MAXIMUM))
	      yyalloc = YYSTACK_ALLOC_MAXIMUM;
	    if (yymsg != yymsgbuf)
	      YYSTACK_FREE (yymsg);
	    yymsg = (char *) YYSTACK_ALLOC (yyalloc);
	    if (yymsg)
	      yymsg_alloc = yyalloc;
	    else
	      {
		yymsg = yymsgbuf;
		yymsg_alloc = sizeof yymsgbuf;
	      }
	  }

	if (0 < yysize && yysize <= yymsg_alloc)
	  {
	    (void) yysyntax_error (yymsg, yystate, yychar);
	    yyerror (yymsg);
	  }
	else
	  {
	    yyerror (YY_("syntax error"));
	    if (yysize != 0)
	      goto yyexhaustedlab;
	  }
      }
#endif
    }



  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
	 error, discard it.  */

      if (yychar <= YYEOF)
	{
	  /* Return failure if at end of input.  */
	  if (yychar == YYEOF)
	    YYABORT;
	}
      else
	{
	  yydestruct ("Error: discarding",
		      yytoken, &yylval);
	  yychar = YYEMPTY;
	}
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:

  /* Pacify compilers like GCC when the user code never invokes
     YYERROR and the label yyerrorlab therefore never appears in user
     code.  */
  if (/*CONSTCOND*/ 0)
     goto yyerrorlab;

  /* Do not reclaim the symbols of the rule which action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
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


      yydestruct ("Error: popping",
		  yystos[yystate], yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  *++yyvsp = yylval;


  /* Shift the error token.  */
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
  yyresult = 1;
  goto yyreturn;

#if !defined(yyoverflow) || YYERROR_VERBOSE
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
  if (yychar != YYEMPTY)
     yydestruct ("Cleanup: discarding lookahead",
		 yytoken, &yylval);
  /* Do not reclaim the symbols of the rule which action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
		  yystos[*yyssp], yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
#if YYERROR_VERBOSE
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
#endif
  /* Make sure YYID is used.  */
  return YYID (yyresult);
}



/* Line 1675 of yacc.c  */
#line 471 "dtc-parser.y"


void print_error(char const *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	srcpos_verror(&yylloc, fmt, va);
	va_end(va);

	treesource_error = 1;
}

void yyerror(char const *s) {
	print_error("%s", s);
}

static unsigned long long eval_literal(const char *s, int base, int bits)
{
	unsigned long long val;
	char *e;

	errno = 0;
	val = strtoull(s, &e, base);
	if (*e) {
		size_t uls = strspn(e, "UL");
		if (e[uls])
			print_error("bad characters in literal");
	}
	if ((errno == ERANGE)
		 || ((bits < 64) && (val >= (1ULL << bits))))
		print_error("literal out of range");
	else if (errno != 0)
		print_error("bad literal");
	return val;
}

static unsigned char eval_char_literal(const char *s)
{
	int i = 1;
	char c = s[0];

	if (c == '\0')
	{
		print_error("empty character literal");
		return 0;
	}

	/*
	 * If the first character in the character literal is a \ then process
	 * the remaining characters as an escape encoding. If the first
	 * character is neither an escape or a terminator it should be the only
	 * character in the literal and will be returned.
	 */
	if (c == '\\')
		c = get_escape_char(s, &i);

	if (s[i] != '\0')
		print_error("malformed character literal");

	return c;
}

