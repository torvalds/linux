
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

/* Substitute the variable and function names.  */
#define yyparse         zconfparse
#define yylex           zconflex
#define yyerror         zconferror
#define yylval          zconflval
#define yychar          zconfchar
#define yydebug         zconfdebug
#define yynerrs         zconfnerrs


/* Copy the first part of user declarations.  */


/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 * Released under the terms of the GNU GPL v2.0.
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define LKC_DIRECT_LINK
#include "lkc.h"

#define printd(mask, fmt...) if (cdebug & (mask)) printf(fmt)

#define PRINTD		0x0001
#define DEBUG_PARSE	0x0002

int cdebug = PRINTD;

extern int zconflex(void);
static void zconfprint(const char *err, ...);
static void zconf_error(const char *err, ...);
static void zconferror(const char *err);
static bool zconf_endtoken(struct kconf_id *id, int starttoken, int endtoken);

struct symbol *symbol_hash[SYMBOL_HASHSIZE];

static struct menu *current_menu, *current_entry;

#define YYDEBUG 0
#if YYDEBUG
#define YYERROR_VERBOSE
#endif



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
     T_MAINMENU = 258,
     T_MENU = 259,
     T_ENDMENU = 260,
     T_SOURCE = 261,
     T_CHOICE = 262,
     T_ENDCHOICE = 263,
     T_COMMENT = 264,
     T_CONFIG = 265,
     T_MENUCONFIG = 266,
     T_HELP = 267,
     T_HELPTEXT = 268,
     T_IF = 269,
     T_ENDIF = 270,
     T_DEPENDS = 271,
     T_OPTIONAL = 272,
     T_PROMPT = 273,
     T_TYPE = 274,
     T_DEFAULT = 275,
     T_SELECT = 276,
     T_RANGE = 277,
     T_OPTION = 278,
     T_ON = 279,
     T_WORD = 280,
     T_WORD_QUOTE = 281,
     T_UNEQUAL = 282,
     T_CLOSE_PAREN = 283,
     T_OPEN_PAREN = 284,
     T_EOL = 285,
     T_OR = 286,
     T_AND = 287,
     T_EQUAL = 288,
     T_NOT = 289
   };
#endif



#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
{


	char *string;
	struct file *file;
	struct symbol *symbol;
	struct expr *expr;
	struct menu *menu;
	struct kconf_id *id;



} YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
#endif


/* Copy the second part of user declarations.  */


/* Include zconf.hash.c here so it can see the token constants. */
#include "zconf.hash.c"



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
#define YYFINAL  11
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   277

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  35
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  48
/* YYNRULES -- Number of rules.  */
#define YYNRULES  113
/* YYNRULES -- Number of states.  */
#define YYNSTATES  185

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   289

#define YYTRANSLATE(YYX)						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const yytype_uint8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
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
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const yytype_uint16 yyprhs[] =
{
       0,     0,     3,     6,     8,    11,    13,    14,    17,    20,
      23,    26,    31,    36,    40,    42,    44,    46,    48,    50,
      52,    54,    56,    58,    60,    62,    64,    66,    70,    73,
      77,    80,    84,    87,    88,    91,    94,    97,   100,   103,
     106,   110,   115,   120,   125,   131,   135,   136,   140,   141,
     144,   148,   151,   153,   157,   158,   161,   164,   167,   170,
     173,   178,   182,   185,   190,   191,   194,   198,   200,   204,
     205,   208,   211,   214,   218,   222,   225,   227,   231,   232,
     235,   238,   241,   245,   249,   252,   255,   258,   259,   262,
     265,   268,   273,   274,   277,   279,   281,   284,   287,   290,
     292,   295,   296,   299,   301,   305,   309,   313,   316,   320,
     324,   326,   328,   329
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int8 yyrhs[] =
{
      36,     0,    -1,    78,    37,    -1,    37,    -1,    62,    38,
      -1,    38,    -1,    -1,    38,    40,    -1,    38,    54,    -1,
      38,    66,    -1,    38,    77,    -1,    38,    25,     1,    30,
      -1,    38,    39,     1,    30,    -1,    38,     1,    30,    -1,
      16,    -1,    18,    -1,    19,    -1,    21,    -1,    17,    -1,
      22,    -1,    20,    -1,    30,    -1,    60,    -1,    70,    -1,
      43,    -1,    45,    -1,    68,    -1,    25,     1,    30,    -1,
       1,    30,    -1,    10,    25,    30,    -1,    42,    46,    -1,
      11,    25,    30,    -1,    44,    46,    -1,    -1,    46,    47,
      -1,    46,    48,    -1,    46,    74,    -1,    46,    72,    -1,
      46,    41,    -1,    46,    30,    -1,    19,    75,    30,    -1,
      18,    76,    79,    30,    -1,    20,    80,    79,    30,    -1,
      21,    25,    79,    30,    -1,    22,    81,    81,    79,    30,
      -1,    23,    49,    30,    -1,    -1,    49,    25,    50,    -1,
      -1,    33,    76,    -1,     7,    82,    30,    -1,    51,    55,
      -1,    77,    -1,    52,    57,    53,    -1,    -1,    55,    56,
      -1,    55,    74,    -1,    55,    72,    -1,    55,    30,    -1,
      55,    41,    -1,    18,    76,    79,    30,    -1,    19,    75,
      30,    -1,    17,    30,    -1,    20,    25,    79,    30,    -1,
      -1,    57,    40,    -1,    14,    80,    78,    -1,    77,    -1,
      58,    61,    59,    -1,    -1,    61,    40,    -1,    61,    66,
      -1,    61,    54,    -1,     3,    76,    78,    -1,     4,    76,
      30,    -1,    63,    73,    -1,    77,    -1,    64,    67,    65,
      -1,    -1,    67,    40,    -1,    67,    66,    -1,    67,    54,
      -1,     6,    76,    30,    -1,     9,    76,    30,    -1,    69,
      73,    -1,    12,    30,    -1,    71,    13,    -1,    -1,    73,
      74,    -1,    73,    30,    -1,    73,    41,    -1,    16,    24,
      80,    30,    -1,    -1,    76,    79,    -1,    25,    -1,    26,
      -1,     5,    30,    -1,     8,    30,    -1,    15,    30,    -1,
      30,    -1,    78,    30,    -1,    -1,    14,    80,    -1,    81,
      -1,    81,    33,    81,    -1,    81,    27,    81,    -1,    29,
      80,    28,    -1,    34,    80,    -1,    80,    31,    80,    -1,
      80,    32,    80,    -1,    25,    -1,    26,    -1,    -1,    25,
      -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   107,   107,   107,   109,   109,   111,   113,   114,   115,
     116,   117,   118,   122,   126,   126,   126,   126,   126,   126,
     126,   130,   131,   132,   133,   134,   135,   139,   140,   146,
     154,   160,   168,   178,   180,   181,   182,   183,   184,   185,
     188,   196,   202,   212,   218,   224,   227,   229,   240,   241,
     246,   255,   260,   268,   271,   273,   274,   275,   276,   277,
     280,   286,   297,   303,   313,   315,   320,   328,   336,   339,
     341,   342,   343,   348,   355,   362,   367,   375,   378,   380,
     381,   382,   385,   393,   400,   407,   413,   420,   422,   423,
     424,   427,   435,   437,   442,   443,   446,   447,   448,   452,
     453,   456,   457,   460,   461,   462,   463,   464,   465,   466,
     469,   470,   473,   474
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || YYTOKEN_TABLE
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "T_MAINMENU", "T_MENU", "T_ENDMENU",
  "T_SOURCE", "T_CHOICE", "T_ENDCHOICE", "T_COMMENT", "T_CONFIG",
  "T_MENUCONFIG", "T_HELP", "T_HELPTEXT", "T_IF", "T_ENDIF", "T_DEPENDS",
  "T_OPTIONAL", "T_PROMPT", "T_TYPE", "T_DEFAULT", "T_SELECT", "T_RANGE",
  "T_OPTION", "T_ON", "T_WORD", "T_WORD_QUOTE", "T_UNEQUAL",
  "T_CLOSE_PAREN", "T_OPEN_PAREN", "T_EOL", "T_OR", "T_AND", "T_EQUAL",
  "T_NOT", "$accept", "input", "start", "stmt_list", "option_name",
  "common_stmt", "option_error", "config_entry_start", "config_stmt",
  "menuconfig_entry_start", "menuconfig_stmt", "config_option_list",
  "config_option", "symbol_option", "symbol_option_list",
  "symbol_option_arg", "choice", "choice_entry", "choice_end",
  "choice_stmt", "choice_option_list", "choice_option", "choice_block",
  "if_entry", "if_end", "if_stmt", "if_block", "mainmenu_stmt", "menu",
  "menu_entry", "menu_end", "menu_stmt", "menu_block", "source_stmt",
  "comment", "comment_stmt", "help_start", "help", "depends_list",
  "depends", "prompt_stmt_opt", "prompt", "end", "nl", "if_expr", "expr",
  "symbol", "word_opt", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const yytype_uint16 yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,    35,    36,    36,    37,    37,    38,    38,    38,    38,
      38,    38,    38,    38,    39,    39,    39,    39,    39,    39,
      39,    40,    40,    40,    40,    40,    40,    41,    41,    42,
      43,    44,    45,    46,    46,    46,    46,    46,    46,    46,
      47,    47,    47,    47,    47,    48,    49,    49,    50,    50,
      51,    52,    53,    54,    55,    55,    55,    55,    55,    55,
      56,    56,    56,    56,    57,    57,    58,    59,    60,    61,
      61,    61,    61,    62,    63,    64,    65,    66,    67,    67,
      67,    67,    68,    69,    70,    71,    72,    73,    73,    73,
      73,    74,    75,    75,    76,    76,    77,    77,    77,    78,
      78,    79,    79,    80,    80,    80,    80,    80,    80,    80,
      81,    81,    82,    82
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     2,     1,     2,     1,     0,     2,     2,     2,
       2,     4,     4,     3,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     3,     2,     3,
       2,     3,     2,     0,     2,     2,     2,     2,     2,     2,
       3,     4,     4,     4,     5,     3,     0,     3,     0,     2,
       3,     2,     1,     3,     0,     2,     2,     2,     2,     2,
       4,     3,     2,     4,     0,     2,     3,     1,     3,     0,
       2,     2,     2,     3,     3,     2,     1,     3,     0,     2,
       2,     2,     3,     3,     2,     2,     2,     0,     2,     2,
       2,     4,     0,     2,     1,     1,     2,     2,     2,     1,
       2,     0,     2,     1,     3,     3,     3,     2,     3,     3,
       1,     1,     0,     1
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       6,     0,    99,     0,     3,     0,     6,     6,    94,    95,
       0,     1,     0,     0,     0,     0,   112,     0,     0,     0,
       0,     0,     0,    14,    18,    15,    16,    20,    17,    19,
       0,    21,     0,     7,    33,    24,    33,    25,    54,    64,
       8,    69,    22,    87,    78,     9,    26,    87,    23,    10,
       0,   100,     2,    73,    13,     0,    96,     0,   113,     0,
      97,     0,     0,     0,   110,   111,     0,     0,     0,   103,
      98,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    74,    82,    50,    83,    29,    31,     0,   107,     0,
       0,    66,     0,     0,    11,    12,     0,     0,     0,     0,
      92,     0,     0,     0,    46,     0,    39,    38,    34,    35,
       0,    37,    36,     0,     0,    92,     0,    58,    59,    55,
      57,    56,    65,    53,    52,    70,    72,    68,    71,    67,
      89,    90,    88,    79,    81,    77,    80,    76,   106,   108,
     109,   105,   104,    28,    85,     0,   101,     0,   101,   101,
     101,     0,     0,     0,    86,    62,   101,     0,   101,     0,
       0,     0,    40,    93,     0,     0,   101,    48,    45,    27,
       0,    61,     0,    91,   102,    41,    42,    43,     0,     0,
      47,    60,    63,    44,    49
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,     3,     4,     5,    32,    33,   107,    34,    35,    36,
      37,    73,   108,   109,   152,   180,    38,    39,   123,    40,
      75,   119,    76,    41,   127,    42,    77,     6,    43,    44,
     135,    45,    79,    46,    47,    48,   110,   111,    78,   112,
     147,   148,    49,     7,   161,    68,    69,    59
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -89
static const yytype_int16 yypact[] =
{
       3,     4,   -89,    20,   -89,   100,   -89,     7,   -89,   -89,
      -8,   -89,    17,     4,    28,     4,    37,    36,     4,    68,
      87,   -18,    69,   -89,   -89,   -89,   -89,   -89,   -89,   -89,
     128,   -89,   138,   -89,   -89,   -89,   -89,   -89,   -89,   -89,
     -89,   -89,   -89,   -89,   -89,   -89,   -89,   -89,   -89,   -89,
     127,   -89,   -89,   110,   -89,   126,   -89,   136,   -89,   137,
     -89,   147,   150,   152,   -89,   -89,   -18,   -18,   171,   -14,
     -89,   153,   157,    34,    67,   180,   233,   220,   207,   220,
     154,   -89,   -89,   -89,   -89,   -89,   -89,     0,   -89,   -18,
     -18,   110,    44,    44,   -89,   -89,   163,   174,   182,     4,
       4,   -18,   194,    44,   -89,   219,   -89,   -89,   -89,   -89,
     223,   -89,   -89,   203,     4,     4,   215,   -89,   -89,   -89,
     -89,   -89,   -89,   -89,   -89,   -89,   -89,   -89,   -89,   -89,
     -89,   -89,   -89,   -89,   -89,   -89,   -89,   -89,   -89,   213,
     -89,   -89,   -89,   -89,   -89,   -18,   232,   227,   232,    -5,
     232,    44,    35,   234,   -89,   -89,   232,   235,   232,   224,
     -18,   236,   -89,   -89,   237,   238,   232,   216,   -89,   -89,
     240,   -89,   241,   -89,    71,   -89,   -89,   -89,   242,     4,
     -89,   -89,   -89,   -89,   -89
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
     -89,   -89,   255,   267,   -89,    47,   -57,   -89,   -89,   -89,
     -89,   239,   -89,   -89,   -89,   -89,   -89,   -89,   -89,   130,
     -89,   -89,   -89,   -89,   -89,   -89,   -89,   -89,   -89,   -89,
     -89,   181,   -89,   -89,   -89,   -89,   -89,   199,   229,    16,
     162,    -1,    74,    -7,   103,   -65,   -88,   -89
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -85
static const yytype_int16 yytable[] =
{
      10,    87,    88,    53,   141,   142,     1,    64,    65,   160,
       1,    66,    55,    92,    57,   151,    67,    61,   118,    93,
      11,   131,     2,   131,   139,   140,    89,    90,   138,     8,
       9,    89,    90,     2,   -30,    96,   149,    51,   -30,   -30,
     -30,   -30,   -30,   -30,   -30,   -30,    97,    54,   -30,   -30,
      98,   -30,    99,   100,   101,   102,   103,   104,    56,   105,
     167,    91,    58,   166,   106,   168,    60,   -32,    96,    64,
      65,   -32,   -32,   -32,   -32,   -32,   -32,   -32,   -32,    97,
     159,   -32,   -32,    98,   -32,    99,   100,   101,   102,   103,
     104,   121,   105,    62,   132,   174,   132,   106,   146,    70,
      -5,    12,    89,    90,    13,    14,    15,    16,    17,    18,
      19,    20,    63,   156,    21,    22,    23,    24,    25,    26,
      27,    28,    29,   122,   125,    30,   133,    -4,    12,    71,
      31,    13,    14,    15,    16,    17,    18,    19,    20,    72,
      51,    21,    22,    23,    24,    25,    26,    27,    28,    29,
     124,   129,    30,   137,   -84,    96,    81,    31,   -84,   -84,
     -84,   -84,   -84,   -84,   -84,   -84,    82,    83,   -84,   -84,
      98,   -84,   -84,   -84,   -84,   -84,   -84,    84,   184,   105,
      85,    96,    86,    94,   130,   -51,   -51,    95,   -51,   -51,
     -51,   -51,    97,   143,   -51,   -51,    98,   113,   114,   115,
     116,     2,    89,    90,   144,   105,   145,   126,    96,   134,
     117,   -75,   -75,   -75,   -75,   -75,   -75,   -75,   -75,   150,
     153,   -75,   -75,    98,    13,    14,    15,    16,    17,    18,
      19,    20,   105,   155,    21,    22,   154,   130,    14,    15,
     158,    17,    18,    19,    20,    90,   160,    21,    22,   179,
      31,   163,   164,   165,   173,    89,    90,   162,   128,   170,
     136,   172,    52,    31,   169,   171,   175,   176,   177,   178,
     181,   182,   183,    50,   120,    74,    80,   157
};

static const yytype_uint8 yycheck[] =
{
       1,    66,    67,    10,    92,    93,     3,    25,    26,    14,
       3,    29,    13,    27,    15,   103,    34,    18,    75,    33,
       0,    78,    30,    80,    89,    90,    31,    32,    28,    25,
      26,    31,    32,    30,     0,     1,   101,    30,     4,     5,
       6,     7,     8,     9,    10,    11,    12,    30,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    30,    25,
      25,    68,    25,   151,    30,    30,    30,     0,     1,    25,
      26,     4,     5,     6,     7,     8,     9,    10,    11,    12,
     145,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    75,    25,    25,    78,   160,    80,    30,    99,    30,
       0,     1,    31,    32,     4,     5,     6,     7,     8,     9,
      10,    11,    25,   114,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    76,    77,    25,    79,     0,     1,     1,
      30,     4,     5,     6,     7,     8,     9,    10,    11,     1,
      30,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      76,    77,    25,    79,     0,     1,    30,    30,     4,     5,
       6,     7,     8,     9,    10,    11,    30,    30,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    30,   179,    25,
      30,     1,    30,    30,    30,     5,     6,    30,     8,     9,
      10,    11,    12,    30,    14,    15,    16,    17,    18,    19,
      20,    30,    31,    32,    30,    25,    24,    77,     1,    79,
      30,     4,     5,     6,     7,     8,     9,    10,    11,    25,
       1,    14,    15,    16,     4,     5,     6,     7,     8,     9,
      10,    11,    25,    30,    14,    15,    13,    30,     5,     6,
      25,     8,     9,    10,    11,    32,    14,    14,    15,    33,
      30,   148,   149,   150,    30,    31,    32,    30,    77,   156,
      79,   158,     7,    30,    30,    30,    30,    30,    30,   166,
      30,    30,    30,     6,    75,    36,    47,   115
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,     3,    30,    36,    37,    38,    62,    78,    25,    26,
      76,     0,     1,     4,     5,     6,     7,     8,     9,    10,
      11,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      25,    30,    39,    40,    42,    43,    44,    45,    51,    52,
      54,    58,    60,    63,    64,    66,    68,    69,    70,    77,
      38,    30,    37,    78,    30,    76,    30,    76,    25,    82,
      30,    76,    25,    25,    25,    26,    29,    34,    80,    81,
      30,     1,     1,    46,    46,    55,    57,    61,    73,    67,
      73,    30,    30,    30,    30,    30,    30,    80,    80,    31,
      32,    78,    27,    33,    30,    30,     1,    12,    16,    18,
      19,    20,    21,    22,    23,    25,    30,    41,    47,    48,
      71,    72,    74,    17,    18,    19,    20,    30,    41,    56,
      72,    74,    40,    53,    77,    40,    54,    59,    66,    77,
      30,    41,    74,    40,    54,    65,    66,    77,    28,    80,
      80,    81,    81,    30,    30,    24,    76,    75,    76,    80,
      25,    81,    49,     1,    13,    30,    76,    75,    25,    80,
      14,    79,    30,    79,    79,    79,    81,    25,    30,    30,
      79,    30,    79,    30,    80,    30,    30,    30,    79,    33,
      50,    30,    30,    30,    76
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
      case 52: /* "choice_entry" */

	{
	fprintf(stderr, "%s:%d: missing end statement for this entry\n",
		(yyvaluep->menu)->file->name, (yyvaluep->menu)->lineno);
	if (current_menu == (yyvaluep->menu))
		menu_end_menu();
};

	break;
      case 58: /* "if_entry" */

	{
	fprintf(stderr, "%s:%d: missing end statement for this entry\n",
		(yyvaluep->menu)->file->name, (yyvaluep->menu)->lineno);
	if (current_menu == (yyvaluep->menu))
		menu_end_menu();
};

	break;
      case 64: /* "menu_entry" */

	{
	fprintf(stderr, "%s:%d: missing end statement for this entry\n",
		(yyvaluep->menu)->file->name, (yyvaluep->menu)->lineno);
	if (current_menu == (yyvaluep->menu))
		menu_end_menu();
};

	break;

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
        case 10:

    { zconf_error("unexpected end statement"); ;}
    break;

  case 11:

    { zconf_error("unknown statement \"%s\"", (yyvsp[(2) - (4)].string)); ;}
    break;

  case 12:

    {
	zconf_error("unexpected option \"%s\"", kconf_id_strings + (yyvsp[(2) - (4)].id)->name);
;}
    break;

  case 13:

    { zconf_error("invalid statement"); ;}
    break;

  case 27:

    { zconf_error("unknown option \"%s\"", (yyvsp[(1) - (3)].string)); ;}
    break;

  case 28:

    { zconf_error("invalid option"); ;}
    break;

  case 29:

    {
	struct symbol *sym = sym_lookup((yyvsp[(2) - (3)].string), 0);
	sym->flags |= SYMBOL_OPTIONAL;
	menu_add_entry(sym);
	printd(DEBUG_PARSE, "%s:%d:config %s\n", zconf_curname(), zconf_lineno(), (yyvsp[(2) - (3)].string));
;}
    break;

  case 30:

    {
	menu_end_entry();
	printd(DEBUG_PARSE, "%s:%d:endconfig\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 31:

    {
	struct symbol *sym = sym_lookup((yyvsp[(2) - (3)].string), 0);
	sym->flags |= SYMBOL_OPTIONAL;
	menu_add_entry(sym);
	printd(DEBUG_PARSE, "%s:%d:menuconfig %s\n", zconf_curname(), zconf_lineno(), (yyvsp[(2) - (3)].string));
;}
    break;

  case 32:

    {
	if (current_entry->prompt)
		current_entry->prompt->type = P_MENU;
	else
		zconfprint("warning: menuconfig statement without prompt");
	menu_end_entry();
	printd(DEBUG_PARSE, "%s:%d:endconfig\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 40:

    {
	menu_set_type((yyvsp[(1) - (3)].id)->stype);
	printd(DEBUG_PARSE, "%s:%d:type(%u)\n",
		zconf_curname(), zconf_lineno(),
		(yyvsp[(1) - (3)].id)->stype);
;}
    break;

  case 41:

    {
	menu_add_prompt(P_PROMPT, (yyvsp[(2) - (4)].string), (yyvsp[(3) - (4)].expr));
	printd(DEBUG_PARSE, "%s:%d:prompt\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 42:

    {
	menu_add_expr(P_DEFAULT, (yyvsp[(2) - (4)].expr), (yyvsp[(3) - (4)].expr));
	if ((yyvsp[(1) - (4)].id)->stype != S_UNKNOWN)
		menu_set_type((yyvsp[(1) - (4)].id)->stype);
	printd(DEBUG_PARSE, "%s:%d:default(%u)\n",
		zconf_curname(), zconf_lineno(),
		(yyvsp[(1) - (4)].id)->stype);
;}
    break;

  case 43:

    {
	menu_add_symbol(P_SELECT, sym_lookup((yyvsp[(2) - (4)].string), 0), (yyvsp[(3) - (4)].expr));
	printd(DEBUG_PARSE, "%s:%d:select\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 44:

    {
	menu_add_expr(P_RANGE, expr_alloc_comp(E_RANGE,(yyvsp[(2) - (5)].symbol), (yyvsp[(3) - (5)].symbol)), (yyvsp[(4) - (5)].expr));
	printd(DEBUG_PARSE, "%s:%d:range\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 47:

    {
	struct kconf_id *id = kconf_id_lookup((yyvsp[(2) - (3)].string), strlen((yyvsp[(2) - (3)].string)));
	if (id && id->flags & TF_OPTION)
		menu_add_option(id->token, (yyvsp[(3) - (3)].string));
	else
		zconfprint("warning: ignoring unknown option %s", (yyvsp[(2) - (3)].string));
	free((yyvsp[(2) - (3)].string));
;}
    break;

  case 48:

    { (yyval.string) = NULL; ;}
    break;

  case 49:

    { (yyval.string) = (yyvsp[(2) - (2)].string); ;}
    break;

  case 50:

    {
	struct symbol *sym = sym_lookup((yyvsp[(2) - (3)].string), SYMBOL_CHOICE);
	sym->flags |= SYMBOL_AUTO;
	menu_add_entry(sym);
	menu_add_expr(P_CHOICE, NULL, NULL);
	printd(DEBUG_PARSE, "%s:%d:choice\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 51:

    {
	(yyval.menu) = menu_add_menu();
;}
    break;

  case 52:

    {
	if (zconf_endtoken((yyvsp[(1) - (1)].id), T_CHOICE, T_ENDCHOICE)) {
		menu_end_menu();
		printd(DEBUG_PARSE, "%s:%d:endchoice\n", zconf_curname(), zconf_lineno());
	}
;}
    break;

  case 60:

    {
	menu_add_prompt(P_PROMPT, (yyvsp[(2) - (4)].string), (yyvsp[(3) - (4)].expr));
	printd(DEBUG_PARSE, "%s:%d:prompt\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 61:

    {
	if ((yyvsp[(1) - (3)].id)->stype == S_BOOLEAN || (yyvsp[(1) - (3)].id)->stype == S_TRISTATE) {
		menu_set_type((yyvsp[(1) - (3)].id)->stype);
		printd(DEBUG_PARSE, "%s:%d:type(%u)\n",
			zconf_curname(), zconf_lineno(),
			(yyvsp[(1) - (3)].id)->stype);
	} else
		YYERROR;
;}
    break;

  case 62:

    {
	current_entry->sym->flags |= SYMBOL_OPTIONAL;
	printd(DEBUG_PARSE, "%s:%d:optional\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 63:

    {
	if ((yyvsp[(1) - (4)].id)->stype == S_UNKNOWN) {
		menu_add_symbol(P_DEFAULT, sym_lookup((yyvsp[(2) - (4)].string), 0), (yyvsp[(3) - (4)].expr));
		printd(DEBUG_PARSE, "%s:%d:default\n",
			zconf_curname(), zconf_lineno());
	} else
		YYERROR;
;}
    break;

  case 66:

    {
	printd(DEBUG_PARSE, "%s:%d:if\n", zconf_curname(), zconf_lineno());
	menu_add_entry(NULL);
	menu_add_dep((yyvsp[(2) - (3)].expr));
	(yyval.menu) = menu_add_menu();
;}
    break;

  case 67:

    {
	if (zconf_endtoken((yyvsp[(1) - (1)].id), T_IF, T_ENDIF)) {
		menu_end_menu();
		printd(DEBUG_PARSE, "%s:%d:endif\n", zconf_curname(), zconf_lineno());
	}
;}
    break;

  case 73:

    {
	menu_add_prompt(P_MENU, (yyvsp[(2) - (3)].string), NULL);
;}
    break;

  case 74:

    {
	menu_add_entry(NULL);
	menu_add_prompt(P_MENU, (yyvsp[(2) - (3)].string), NULL);
	printd(DEBUG_PARSE, "%s:%d:menu\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 75:

    {
	(yyval.menu) = menu_add_menu();
;}
    break;

  case 76:

    {
	if (zconf_endtoken((yyvsp[(1) - (1)].id), T_MENU, T_ENDMENU)) {
		menu_end_menu();
		printd(DEBUG_PARSE, "%s:%d:endmenu\n", zconf_curname(), zconf_lineno());
	}
;}
    break;

  case 82:

    {
	printd(DEBUG_PARSE, "%s:%d:source %s\n", zconf_curname(), zconf_lineno(), (yyvsp[(2) - (3)].string));
	zconf_nextfile((yyvsp[(2) - (3)].string));
;}
    break;

  case 83:

    {
	menu_add_entry(NULL);
	menu_add_prompt(P_COMMENT, (yyvsp[(2) - (3)].string), NULL);
	printd(DEBUG_PARSE, "%s:%d:comment\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 84:

    {
	menu_end_entry();
;}
    break;

  case 85:

    {
	printd(DEBUG_PARSE, "%s:%d:help\n", zconf_curname(), zconf_lineno());
	zconf_starthelp();
;}
    break;

  case 86:

    {
	current_entry->help = (yyvsp[(2) - (2)].string);
;}
    break;

  case 91:

    {
	menu_add_dep((yyvsp[(3) - (4)].expr));
	printd(DEBUG_PARSE, "%s:%d:depends on\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 93:

    {
	menu_add_prompt(P_PROMPT, (yyvsp[(1) - (2)].string), (yyvsp[(2) - (2)].expr));
;}
    break;

  case 96:

    { (yyval.id) = (yyvsp[(1) - (2)].id); ;}
    break;

  case 97:

    { (yyval.id) = (yyvsp[(1) - (2)].id); ;}
    break;

  case 98:

    { (yyval.id) = (yyvsp[(1) - (2)].id); ;}
    break;

  case 101:

    { (yyval.expr) = NULL; ;}
    break;

  case 102:

    { (yyval.expr) = (yyvsp[(2) - (2)].expr); ;}
    break;

  case 103:

    { (yyval.expr) = expr_alloc_symbol((yyvsp[(1) - (1)].symbol)); ;}
    break;

  case 104:

    { (yyval.expr) = expr_alloc_comp(E_EQUAL, (yyvsp[(1) - (3)].symbol), (yyvsp[(3) - (3)].symbol)); ;}
    break;

  case 105:

    { (yyval.expr) = expr_alloc_comp(E_UNEQUAL, (yyvsp[(1) - (3)].symbol), (yyvsp[(3) - (3)].symbol)); ;}
    break;

  case 106:

    { (yyval.expr) = (yyvsp[(2) - (3)].expr); ;}
    break;

  case 107:

    { (yyval.expr) = expr_alloc_one(E_NOT, (yyvsp[(2) - (2)].expr)); ;}
    break;

  case 108:

    { (yyval.expr) = expr_alloc_two(E_OR, (yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); ;}
    break;

  case 109:

    { (yyval.expr) = expr_alloc_two(E_AND, (yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); ;}
    break;

  case 110:

    { (yyval.symbol) = sym_lookup((yyvsp[(1) - (1)].string), 0); free((yyvsp[(1) - (1)].string)); ;}
    break;

  case 111:

    { (yyval.symbol) = sym_lookup((yyvsp[(1) - (1)].string), SYMBOL_CONST); free((yyvsp[(1) - (1)].string)); ;}
    break;

  case 112:

    { (yyval.string) = NULL; ;}
    break;



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





void conf_parse(const char *name)
{
	struct symbol *sym;
	int i;

	zconf_initscan(name);

	sym_init();
	_menu_init();
	modules_sym = sym_lookup(NULL, 0);
	modules_sym->type = S_BOOLEAN;
	modules_sym->flags |= SYMBOL_AUTO;
	rootmenu.prompt = menu_add_prompt(P_MENU, "Linux Kernel Configuration", NULL);

#if YYDEBUG
	if (getenv("ZCONF_DEBUG"))
		zconfdebug = 1;
#endif
	zconfparse();
	if (zconfnerrs)
		exit(1);
	if (!modules_sym->prop) {
		struct property *prop;

		prop = prop_alloc(P_DEFAULT, modules_sym);
		prop->expr = expr_alloc_symbol(sym_lookup("MODULES", 0));
	}

	rootmenu.prompt->text = _(rootmenu.prompt->text);
	rootmenu.prompt->text = sym_expand_string_value(rootmenu.prompt->text);

	menu_finalize(&rootmenu);
	for_all_symbols(i, sym) {
		if (sym_check_deps(sym))
			zconfnerrs++;
        }
	if (zconfnerrs)
		exit(1);
	sym_set_change_count(1);
}

static const char *zconf_tokenname(int token)
{
	switch (token) {
	case T_MENU:		return "menu";
	case T_ENDMENU:		return "endmenu";
	case T_CHOICE:		return "choice";
	case T_ENDCHOICE:	return "endchoice";
	case T_IF:		return "if";
	case T_ENDIF:		return "endif";
	case T_DEPENDS:		return "depends";
	}
	return "<token>";
}

static bool zconf_endtoken(struct kconf_id *id, int starttoken, int endtoken)
{
	if (id->token != endtoken) {
		zconf_error("unexpected '%s' within %s block",
			kconf_id_strings + id->name, zconf_tokenname(starttoken));
		zconfnerrs++;
		return false;
	}
	if (current_menu->file != current_file) {
		zconf_error("'%s' in different file than '%s'",
			kconf_id_strings + id->name, zconf_tokenname(starttoken));
		fprintf(stderr, "%s:%d: location of the '%s'\n",
			current_menu->file->name, current_menu->lineno,
			zconf_tokenname(starttoken));
		zconfnerrs++;
		return false;
	}
	return true;
}

static void zconfprint(const char *err, ...)
{
	va_list ap;

	fprintf(stderr, "%s:%d: ", zconf_curname(), zconf_lineno());
	va_start(ap, err);
	vfprintf(stderr, err, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}

static void zconf_error(const char *err, ...)
{
	va_list ap;

	zconfnerrs++;
	fprintf(stderr, "%s:%d: ", zconf_curname(), zconf_lineno());
	va_start(ap, err);
	vfprintf(stderr, err, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}

static void zconferror(const char *err)
{
#if YYDEBUG
	fprintf(stderr, "%s:%d: %s\n", zconf_curname(), zconf_lineno() + 1, err);
#endif
}

static void print_quoted_string(FILE *out, const char *str)
{
	const char *p;
	int len;

	putc('"', out);
	while ((p = strchr(str, '"'))) {
		len = p - str;
		if (len)
			fprintf(out, "%.*s", len, str);
		fputs("\\\"", out);
		str = p + 1;
	}
	fputs(str, out);
	putc('"', out);
}

static void print_symbol(FILE *out, struct menu *menu)
{
	struct symbol *sym = menu->sym;
	struct property *prop;

	if (sym_is_choice(sym))
		fprintf(out, "\nchoice\n");
	else
		fprintf(out, "\nconfig %s\n", sym->name);
	switch (sym->type) {
	case S_BOOLEAN:
		fputs("  boolean\n", out);
		break;
	case S_TRISTATE:
		fputs("  tristate\n", out);
		break;
	case S_STRING:
		fputs("  string\n", out);
		break;
	case S_INT:
		fputs("  integer\n", out);
		break;
	case S_HEX:
		fputs("  hex\n", out);
		break;
	default:
		fputs("  ???\n", out);
		break;
	}
	for (prop = sym->prop; prop; prop = prop->next) {
		if (prop->menu != menu)
			continue;
		switch (prop->type) {
		case P_PROMPT:
			fputs("  prompt ", out);
			print_quoted_string(out, prop->text);
			if (!expr_is_yes(prop->visible.expr)) {
				fputs(" if ", out);
				expr_fprint(prop->visible.expr, out);
			}
			fputc('\n', out);
			break;
		case P_DEFAULT:
			fputs( "  default ", out);
			expr_fprint(prop->expr, out);
			if (!expr_is_yes(prop->visible.expr)) {
				fputs(" if ", out);
				expr_fprint(prop->visible.expr, out);
			}
			fputc('\n', out);
			break;
		case P_CHOICE:
			fputs("  #choice value\n", out);
			break;
		case P_SELECT:
			fputs( "  select ", out);
			expr_fprint(prop->expr, out);
			fputc('\n', out);
			break;
		case P_RANGE:
			fputs( "  range ", out);
			expr_fprint(prop->expr, out);
			fputc('\n', out);
			break;
		case P_MENU:
			fputs( "  menu ", out);
			print_quoted_string(out, prop->text);
			fputc('\n', out);
			break;
		default:
			fprintf(out, "  unknown prop %d!\n", prop->type);
			break;
		}
	}
	if (menu->help) {
		int len = strlen(menu->help);
		while (menu->help[--len] == '\n')
			menu->help[len] = 0;
		fprintf(out, "  help\n%s\n", menu->help);
	}
}

void zconfdump(FILE *out)
{
	struct property *prop;
	struct symbol *sym;
	struct menu *menu;

	menu = rootmenu.list;
	while (menu) {
		if ((sym = menu->sym))
			print_symbol(out, menu);
		else if ((prop = menu->prompt)) {
			switch (prop->type) {
			case P_COMMENT:
				fputs("\ncomment ", out);
				print_quoted_string(out, prop->text);
				fputs("\n", out);
				break;
			case P_MENU:
				fputs("\nmenu ", out);
				print_quoted_string(out, prop->text);
				fputs("\n", out);
				break;
			default:
				;
			}
			if (!expr_is_yes(prop->visible.expr)) {
				fputs("  depends ", out);
				expr_fprint(prop->visible.expr, out);
				fputc('\n', out);
			}
		}

		if (menu->list)
			menu = menu->list;
		else if (menu->next)
			menu = menu->next;
		else while ((menu = menu->parent)) {
			if (menu->prompt && menu->prompt->type == P_MENU)
				fputs("\nendmenu\n", out);
			if (menu->next) {
				menu = menu->next;
				break;
			}
		}
	}
}

#include "lex.zconf.c"
#include "util.c"
#include "confdata.c"
#include "expr.c"
#include "symbol.c"
#include "menu.c"

