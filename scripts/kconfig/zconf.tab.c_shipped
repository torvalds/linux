/* A Bison parser, made by GNU Bison 1.875a.  */

/* Skeleton parser for Yacc-like parsing with Bison,
   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003 Free Software Foundation, Inc.

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

/* If NAME_PREFIX is specified substitute the variables and functions
   names.  */
#define yyparse zconfparse
#define yylex   zconflex
#define yyerror zconferror
#define yylval  zconflval
#define yychar  zconfchar
#define yydebug zconfdebug
#define yynerrs zconfnerrs


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
     T_REQUIRES = 272,
     T_OPTIONAL = 273,
     T_PROMPT = 274,
     T_DEFAULT = 275,
     T_TRISTATE = 276,
     T_DEF_TRISTATE = 277,
     T_BOOLEAN = 278,
     T_DEF_BOOLEAN = 279,
     T_STRING = 280,
     T_INT = 281,
     T_HEX = 282,
     T_WORD = 283,
     T_WORD_QUOTE = 284,
     T_UNEQUAL = 285,
     T_EOF = 286,
     T_EOL = 287,
     T_CLOSE_PAREN = 288,
     T_OPEN_PAREN = 289,
     T_ON = 290,
     T_SELECT = 291,
     T_RANGE = 292,
     T_OR = 293,
     T_AND = 294,
     T_EQUAL = 295,
     T_NOT = 296
   };
#endif
#define T_MAINMENU 258
#define T_MENU 259
#define T_ENDMENU 260
#define T_SOURCE 261
#define T_CHOICE 262
#define T_ENDCHOICE 263
#define T_COMMENT 264
#define T_CONFIG 265
#define T_MENUCONFIG 266
#define T_HELP 267
#define T_HELPTEXT 268
#define T_IF 269
#define T_ENDIF 270
#define T_DEPENDS 271
#define T_REQUIRES 272
#define T_OPTIONAL 273
#define T_PROMPT 274
#define T_DEFAULT 275
#define T_TRISTATE 276
#define T_DEF_TRISTATE 277
#define T_BOOLEAN 278
#define T_DEF_BOOLEAN 279
#define T_STRING 280
#define T_INT 281
#define T_HEX 282
#define T_WORD 283
#define T_WORD_QUOTE 284
#define T_UNEQUAL 285
#define T_EOF 286
#define T_EOL 287
#define T_CLOSE_PAREN 288
#define T_OPEN_PAREN 289
#define T_ON 290
#define T_SELECT 291
#define T_RANGE 292
#define T_OR 293
#define T_AND 294
#define T_EQUAL 295
#define T_NOT 296




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

#define printd(mask, fmt...) if (cdebug & (mask)) printf(fmt)

#define PRINTD		0x0001
#define DEBUG_PARSE	0x0002

int cdebug = PRINTD;

extern int zconflex(void);
static void zconfprint(const char *err, ...);
static void zconferror(const char *err);
static bool zconf_endtoken(int token, int starttoken, int endtoken);

struct symbol *symbol_hash[257];

static struct menu *current_menu, *current_entry;

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

typedef union YYSTYPE {
	int token;
	char *string;
	struct symbol *symbol;
	struct expr *expr;
	struct menu *menu;
} YYSTYPE;
/* Line 191 of yacc.c.  */

# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif



/* Copy the second part of user declarations.  */


#define LKC_DIRECT_LINK
#include "lkc.h"


/* Line 214 of yacc.c.  */


#if ! defined (yyoverflow) || YYERROR_VERBOSE

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# if YYSTACK_USE_ALLOCA
#  define YYSTACK_ALLOC alloca
# else
#  ifndef YYSTACK_USE_ALLOCA
#   if defined (alloca) || defined (_ALLOCA_H)
#    define YYSTACK_ALLOC alloca
#   else
#    ifdef __GNUC__
#     define YYSTACK_ALLOC __builtin_alloca
#    endif
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
#  define YYSTACK_ALLOC malloc
#  define YYSTACK_FREE free
# endif
#endif /* ! defined (yyoverflow) || YYERROR_VERBOSE */


#if (! defined (yyoverflow) \
     && (! defined (__cplusplus) \
	 || (YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  short yyss;
  YYSTYPE yyvs;
  };

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (short) + sizeof (YYSTYPE))				\
      + YYSTACK_GAP_MAXIMUM)

/* Copy COUNT objects from FROM to TO.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if 1 < __GNUC__
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
   typedef short yysigned_char;
#endif

/* YYFINAL -- State number of the termination state. */
#define YYFINAL  2
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   201

/* YYNTOKENS -- Number of terminals. */
#define YYNTOKENS  42
/* YYNNTS -- Number of nonterminals. */
#define YYNNTS  41
/* YYNRULES -- Number of rules. */
#define YYNRULES  104
/* YYNRULES -- Number of states. */
#define YYNSTATES  182

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   296

#define YYTRANSLATE(YYX) 						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const unsigned char yytranslate[] =
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
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const unsigned short yyprhs[] =
{
       0,     0,     3,     4,     7,     9,    11,    13,    17,    19,
      21,    23,    26,    28,    30,    32,    34,    36,    38,    42,
      45,    49,    52,    53,    56,    59,    62,    65,    69,    74,
      78,    83,    87,    91,    95,   100,   105,   110,   116,   119,
     122,   124,   128,   131,   132,   135,   138,   141,   144,   149,
     153,   157,   160,   165,   166,   169,   173,   175,   179,   182,
     183,   186,   189,   192,   196,   199,   201,   205,   208,   209,
     212,   215,   218,   222,   226,   228,   232,   235,   238,   241,
     242,   245,   248,   253,   257,   261,   262,   265,   267,   269,
     272,   275,   278,   280,   282,   283,   286,   288,   292,   296,
     300,   303,   307,   311,   313
};

/* YYRHS -- A `-1'-separated list of the rules' RHS. */
static const yysigned_char yyrhs[] =
{
      43,     0,    -1,    -1,    43,    44,    -1,    45,    -1,    55,
      -1,    66,    -1,     3,    77,    79,    -1,     5,    -1,    15,
      -1,     8,    -1,     1,    79,    -1,    61,    -1,    71,    -1,
      47,    -1,    49,    -1,    69,    -1,    79,    -1,    10,    28,
      32,    -1,    46,    50,    -1,    11,    28,    32,    -1,    48,
      50,    -1,    -1,    50,    51,    -1,    50,    75,    -1,    50,
      73,    -1,    50,    32,    -1,    21,    76,    32,    -1,    22,
      81,    80,    32,    -1,    23,    76,    32,    -1,    24,    81,
      80,    32,    -1,    26,    76,    32,    -1,    27,    76,    32,
      -1,    25,    76,    32,    -1,    19,    77,    80,    32,    -1,
      20,    81,    80,    32,    -1,    36,    28,    80,    32,    -1,
      37,    82,    82,    80,    32,    -1,     7,    32,    -1,    52,
      56,    -1,    78,    -1,    53,    58,    54,    -1,    53,    58,
      -1,    -1,    56,    57,    -1,    56,    75,    -1,    56,    73,
      -1,    56,    32,    -1,    19,    77,    80,    32,    -1,    21,
      76,    32,    -1,    23,    76,    32,    -1,    18,    32,    -1,
      20,    28,    80,    32,    -1,    -1,    58,    45,    -1,    14,
      81,    32,    -1,    78,    -1,    59,    62,    60,    -1,    59,
      62,    -1,    -1,    62,    45,    -1,    62,    66,    -1,    62,
      55,    -1,     4,    77,    32,    -1,    63,    74,    -1,    78,
      -1,    64,    67,    65,    -1,    64,    67,    -1,    -1,    67,
      45,    -1,    67,    66,    -1,    67,    55,    -1,    67,     1,
      32,    -1,     6,    77,    32,    -1,    68,    -1,     9,    77,
      32,    -1,    70,    74,    -1,    12,    32,    -1,    72,    13,
      -1,    -1,    74,    75,    -1,    74,    32,    -1,    16,    35,
      81,    32,    -1,    16,    81,    32,    -1,    17,    81,    32,
      -1,    -1,    77,    80,    -1,    28,    -1,    29,    -1,     5,
      79,    -1,     8,    79,    -1,    15,    79,    -1,    32,    -1,
      31,    -1,    -1,    14,    81,    -1,    82,    -1,    82,    40,
      82,    -1,    82,    30,    82,    -1,    34,    81,    33,    -1,
      41,    81,    -1,    81,    38,    81,    -1,    81,    39,    81,
      -1,    28,    -1,    29,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const unsigned short yyrline[] =
{
       0,    94,    94,    95,    98,    99,   100,   101,   102,   103,
     104,   105,   109,   110,   111,   112,   113,   114,   120,   128,
     134,   142,   152,   154,   155,   156,   157,   160,   166,   173,
     179,   186,   192,   198,   204,   210,   216,   222,   230,   239,
     245,   254,   255,   261,   263,   264,   265,   266,   269,   275,
     281,   287,   293,   299,   301,   306,   315,   324,   325,   331,
     333,   334,   335,   340,   347,   353,   362,   363,   369,   371,
     372,   373,   374,   377,   383,   390,   397,   404,   410,   417,
     418,   419,   422,   427,   432,   440,   442,   447,   448,   451,
     452,   453,   457,   457,   459,   460,   463,   464,   465,   466,
     467,   468,   469,   472,   473
};
#endif

#if YYDEBUG || YYERROR_VERBOSE
/* YYTNME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals. */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "T_MAINMENU", "T_MENU", "T_ENDMENU", 
  "T_SOURCE", "T_CHOICE", "T_ENDCHOICE", "T_COMMENT", "T_CONFIG", 
  "T_MENUCONFIG", "T_HELP", "T_HELPTEXT", "T_IF", "T_ENDIF", "T_DEPENDS", 
  "T_REQUIRES", "T_OPTIONAL", "T_PROMPT", "T_DEFAULT", "T_TRISTATE", 
  "T_DEF_TRISTATE", "T_BOOLEAN", "T_DEF_BOOLEAN", "T_STRING", "T_INT", 
  "T_HEX", "T_WORD", "T_WORD_QUOTE", "T_UNEQUAL", "T_EOF", "T_EOL", 
  "T_CLOSE_PAREN", "T_OPEN_PAREN", "T_ON", "T_SELECT", "T_RANGE", "T_OR", 
  "T_AND", "T_EQUAL", "T_NOT", "$accept", "input", "block", 
  "common_block", "config_entry_start", "config_stmt", 
  "menuconfig_entry_start", "menuconfig_stmt", "config_option_list", 
  "config_option", "choice", "choice_entry", "choice_end", "choice_stmt", 
  "choice_option_list", "choice_option", "choice_block", "if", "if_end", 
  "if_stmt", "if_block", "menu", "menu_entry", "menu_end", "menu_stmt", 
  "menu_block", "source", "source_stmt", "comment", "comment_stmt", 
  "help_start", "help", "depends_list", "depends", "prompt_stmt_opt", 
  "prompt", "end", "nl_or_eof", "if_expr", "expr", "symbol", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const unsigned short yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const unsigned char yyr1[] =
{
       0,    42,    43,    43,    44,    44,    44,    44,    44,    44,
      44,    44,    45,    45,    45,    45,    45,    45,    46,    47,
      48,    49,    50,    50,    50,    50,    50,    51,    51,    51,
      51,    51,    51,    51,    51,    51,    51,    51,    52,    53,
      54,    55,    55,    56,    56,    56,    56,    56,    57,    57,
      57,    57,    57,    58,    58,    59,    60,    61,    61,    62,
      62,    62,    62,    63,    64,    65,    66,    66,    67,    67,
      67,    67,    67,    68,    69,    70,    71,    72,    73,    74,
      74,    74,    75,    75,    75,    76,    76,    77,    77,    78,
      78,    78,    79,    79,    80,    80,    81,    81,    81,    81,
      81,    81,    81,    82,    82
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const unsigned char yyr2[] =
{
       0,     2,     0,     2,     1,     1,     1,     3,     1,     1,
       1,     2,     1,     1,     1,     1,     1,     1,     3,     2,
       3,     2,     0,     2,     2,     2,     2,     3,     4,     3,
       4,     3,     3,     3,     4,     4,     4,     5,     2,     2,
       1,     3,     2,     0,     2,     2,     2,     2,     4,     3,
       3,     2,     4,     0,     2,     3,     1,     3,     2,     0,
       2,     2,     2,     3,     2,     1,     3,     2,     0,     2,
       2,     2,     3,     3,     1,     3,     2,     2,     2,     0,
       2,     2,     4,     3,     3,     0,     2,     1,     1,     2,
       2,     2,     1,     1,     0,     2,     1,     3,     3,     3,
       2,     3,     3,     1,     1
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const unsigned char yydefact[] =
{
       2,     0,     1,     0,     0,     0,     8,     0,     0,    10,
       0,     0,     0,     0,     9,    93,    92,     3,     4,    22,
      14,    22,    15,    43,    53,     5,    59,    12,    79,    68,
       6,    74,    16,    79,    13,    17,    11,    87,    88,     0,
       0,     0,    38,     0,     0,     0,   103,   104,     0,     0,
       0,    96,    19,    21,    39,    42,    58,    64,     0,    76,
       7,    63,    73,    75,    18,    20,     0,   100,    55,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    85,     0,
      85,     0,    85,    85,    85,    26,     0,     0,    23,     0,
      25,    24,     0,     0,     0,    85,    85,    47,    44,    46,
      45,     0,     0,     0,    54,    41,    40,    60,    62,    57,
      61,    56,    81,    80,     0,    69,    71,    66,    70,    65,
      99,   101,   102,    98,    97,    77,     0,     0,     0,    94,
      94,     0,    94,    94,     0,    94,     0,     0,     0,    94,
       0,    78,    51,    94,    94,     0,     0,    89,    90,    91,
      72,     0,    83,    84,     0,     0,     0,    27,    86,     0,
      29,     0,    33,    31,    32,     0,    94,     0,     0,    49,
      50,    82,    95,    34,    35,    28,    30,    36,     0,    48,
      52,    37
};

/* YYDEFGOTO[NTERM-NUM]. */
static const short yydefgoto[] =
{
      -1,     1,    17,    18,    19,    20,    21,    22,    52,    88,
      23,    24,   105,    25,    54,    98,    55,    26,   109,    27,
      56,    28,    29,   117,    30,    58,    31,    32,    33,    34,
      89,    90,    57,    91,   131,   132,   106,    35,   155,    50,
      51
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -99
static const short yypact[] =
{
     -99,    48,   -99,    38,    46,    46,   -99,    46,   -29,   -99,
      46,   -17,    -3,   -11,   -99,   -99,   -99,   -99,   -99,   -99,
     -99,   -99,   -99,   -99,   -99,   -99,   -99,   -99,   -99,   -99,
     -99,   -99,   -99,   -99,   -99,   -99,   -99,   -99,   -99,    38,
      12,    15,   -99,    18,    51,    62,   -99,   -99,   -11,   -11,
       4,   -24,   138,   138,   160,   121,   110,    -4,    81,    -4,
     -99,   -99,   -99,   -99,   -99,   -99,   -19,   -99,   -99,   -11,
     -11,    70,    70,    73,    32,   -11,    46,   -11,    46,   -11,
      46,   -11,    46,    46,    46,   -99,    36,    70,   -99,    95,
     -99,   -99,    96,    46,   106,    46,    46,   -99,   -99,   -99,
     -99,    38,    38,    38,   -99,   -99,   -99,   -99,   -99,   -99,
     -99,   -99,   -99,   -99,   112,   -99,   -99,   -99,   -99,   -99,
     -99,   117,   -99,   -99,   -99,   -99,   -11,    33,    65,   131,
       1,   119,   131,     1,   136,     1,   153,   154,   155,   131,
      70,   -99,   -99,   131,   131,   156,   157,   -99,   -99,   -99,
     -99,   101,   -99,   -99,   -11,   158,   159,   -99,   -99,   161,
     -99,   162,   -99,   -99,   -99,   163,   131,   164,   165,   -99,
     -99,   -99,    99,   -99,   -99,   -99,   -99,   -99,   166,   -99,
     -99,   -99
};

/* YYPGOTO[NTERM-NUM].  */
static const short yypgoto[] =
{
     -99,   -99,   -99,   111,   -99,   -99,   -99,   -99,   178,   -99,
     -99,   -99,   -99,    91,   -99,   -99,   -99,   -99,   -99,   -99,
     -99,   -99,   -99,   -99,   115,   -99,   -99,   -99,   -99,   -99,
     -99,   146,   168,    89,    27,     0,   126,    -1,   -98,   -48,
     -63
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -68
static const short yytable[] =
{
      66,    67,    36,    42,    39,    40,    71,    41,   123,   124,
      43,    44,    74,    75,   120,   154,    72,    46,    47,    69,
      70,   121,   122,    48,   140,    45,   127,   128,   112,   130,
      49,   133,   156,   135,   158,   159,    68,   161,    60,    69,
      70,   165,    69,    70,    61,   167,   168,    62,     2,     3,
      63,     4,     5,     6,     7,     8,     9,    10,    11,    12,
      46,    47,    13,    14,   139,   152,    48,   126,   178,    15,
      16,    69,    70,    49,    37,    38,   129,   166,   151,    15,
      16,   -67,   114,    64,   -67,     5,   101,     7,     8,   102,
      10,    11,    12,   143,    65,    13,   103,   153,    46,    47,
     147,   148,   149,    69,    70,   125,   172,   134,   141,   136,
     137,   138,    15,    16,     5,   101,     7,     8,   102,    10,
      11,    12,   145,   146,    13,   103,   101,     7,   142,   102,
      10,    11,    12,   171,   144,    13,   103,    69,    70,    69,
      70,    15,    16,   100,   150,   154,   113,   108,   113,   116,
      73,   157,    15,    16,    74,    75,    70,    76,    77,    78,
      79,    80,    81,    82,    83,    84,   104,   107,   160,   115,
      85,   110,    73,   118,    86,    87,    74,    75,    92,    93,
      94,    95,   111,    96,   119,   162,   163,   164,   169,   170,
     173,   174,    97,   175,   176,   177,   179,   180,   181,    53,
      99,    59
};

static const unsigned char yycheck[] =
{
      48,    49,     3,    32,     4,     5,    30,     7,    71,    72,
      10,    28,    16,    17,    33,    14,    40,    28,    29,    38,
      39,    69,    70,    34,    87,    28,    74,    75,    32,    77,
      41,    79,   130,    81,   132,   133,    32,   135,    39,    38,
      39,   139,    38,    39,    32,   143,   144,    32,     0,     1,
      32,     3,     4,     5,     6,     7,     8,     9,    10,    11,
      28,    29,    14,    15,    28,    32,    34,    35,   166,    31,
      32,    38,    39,    41,    28,    29,    76,   140,   126,    31,
      32,     0,     1,    32,     3,     4,     5,     6,     7,     8,
       9,    10,    11,    93,    32,    14,    15,    32,    28,    29,
     101,   102,   103,    38,    39,    32,   154,    80,    13,    82,
      83,    84,    31,    32,     4,     5,     6,     7,     8,     9,
      10,    11,    95,    96,    14,    15,     5,     6,    32,     8,
       9,    10,    11,    32,    28,    14,    15,    38,    39,    38,
      39,    31,    32,    54,    32,    14,    57,    56,    59,    58,
      12,    32,    31,    32,    16,    17,    39,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    55,    56,    32,    58,
      32,    56,    12,    58,    36,    37,    16,    17,    18,    19,
      20,    21,    56,    23,    58,    32,    32,    32,    32,    32,
      32,    32,    32,    32,    32,    32,    32,    32,    32,    21,
      54,    33
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const unsigned char yystos[] =
{
       0,    43,     0,     1,     3,     4,     5,     6,     7,     8,
       9,    10,    11,    14,    15,    31,    32,    44,    45,    46,
      47,    48,    49,    52,    53,    55,    59,    61,    63,    64,
      66,    68,    69,    70,    71,    79,    79,    28,    29,    77,
      77,    77,    32,    77,    28,    28,    28,    29,    34,    41,
      81,    82,    50,    50,    56,    58,    62,    74,    67,    74,
      79,    32,    32,    32,    32,    32,    81,    81,    32,    38,
      39,    30,    40,    12,    16,    17,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    32,    36,    37,    51,    72,
      73,    75,    18,    19,    20,    21,    23,    32,    57,    73,
      75,     5,     8,    15,    45,    54,    78,    45,    55,    60,
      66,    78,    32,    75,     1,    45,    55,    65,    66,    78,
      33,    81,    81,    82,    82,    32,    35,    81,    81,    77,
      81,    76,    77,    81,    76,    81,    76,    76,    76,    28,
      82,    13,    32,    77,    28,    76,    76,    79,    79,    79,
      32,    81,    32,    32,    14,    80,    80,    32,    80,    80,
      32,    80,    32,    32,    32,    80,    82,    80,    80,    32,
      32,    32,    81,    32,    32,    32,    32,    32,    80,    32,
      32,    32
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
#define YYERROR		goto yyerrlab1


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

/* YYLLOC_DEFAULT -- Compute the default location (before the actions
   are run).  */

#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)         \
  Current.first_line   = Rhs[1].first_line;      \
  Current.first_column = Rhs[1].first_column;    \
  Current.last_line    = Rhs[N].last_line;       \
  Current.last_column  = Rhs[N].last_column;
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

# define YYDSYMPRINT(Args)			\
do {						\
  if (yydebug)					\
    yysymprint Args;				\
} while (0)

# define YYDSYMPRINTF(Title, Token, Value, Location)		\
do {								\
  if (yydebug)							\
    {								\
      YYFPRINTF (stderr, "%s ", Title);				\
      yysymprint (stderr, 					\
                  Token, Value);	\
      YYFPRINTF (stderr, "\n");					\
    }								\
} while (0)

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (cinluded).                                                   |
`------------------------------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yy_stack_print (short *bottom, short *top)
#else
static void
yy_stack_print (bottom, top)
    short *bottom;
    short *top;
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
  unsigned int yylineno = yyrline[yyrule];
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %u), ",
             yyrule - 1, yylineno);
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
# define YYDSYMPRINT(Args)
# define YYDSYMPRINTF(Title, Token, Value, Location)
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

#if YYMAXDEPTH == 0
# undef YYMAXDEPTH
#endif

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
    {
      YYFPRINTF (yyoutput, "token %s (", yytname[yytype]);
# ifdef YYPRINT
      YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# endif
    }
  else
    YYFPRINTF (yyoutput, "nterm %s (", yytname[yytype]);

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
yydestruct (int yytype, YYSTYPE *yyvaluep)
#else
static void
yydestruct (yytype, yyvaluep)
    int yytype;
    YYSTYPE *yyvaluep;
#endif
{
  /* Pacify ``unused variable'' warnings.  */
  (void) yyvaluep;

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



/* The lookahead symbol.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
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
  /* Lookahead token as an internal (translated) token number.  */
  int yytoken = 0;

  /* Three stacks and their tools:
     `yyss': related to states,
     `yyvs': related to semantic values,
     `yyls': related to locations.

     Refer to the stacks thru separate pointers, to allow yyoverflow
     to reallocate them elsewhere.  */

  /* The state stack.  */
  short	yyssa[YYINITDEPTH];
  short *yyss = yyssa;
  register short *yyssp;

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
	short *yyss1 = yyss;


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
	short *yyss1 = yyss;
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
/* Read a lookahead token if we need one and don't already have one.  */
/* yyresume: */

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
      YYDSYMPRINTF ("Next token is", yytoken, &yylval, &yylloc);
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

  /* Shift the lookahead token.  */
  YYDPRINTF ((stderr, "Shifting token %s, ", yytname[yytoken]));

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
        case 8:

    { zconfprint("unexpected 'endmenu' statement"); ;}
    break;

  case 9:

    { zconfprint("unexpected 'endif' statement"); ;}
    break;

  case 10:

    { zconfprint("unexpected 'endchoice' statement"); ;}
    break;

  case 11:

    { zconfprint("syntax error"); yyerrok; ;}
    break;

  case 18:

    {
	struct symbol *sym = sym_lookup(yyvsp[-1].string, 0);
	sym->flags |= SYMBOL_OPTIONAL;
	menu_add_entry(sym);
	printd(DEBUG_PARSE, "%s:%d:config %s\n", zconf_curname(), zconf_lineno(), yyvsp[-1].string);
;}
    break;

  case 19:

    {
	menu_end_entry();
	printd(DEBUG_PARSE, "%s:%d:endconfig\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 20:

    {
	struct symbol *sym = sym_lookup(yyvsp[-1].string, 0);
	sym->flags |= SYMBOL_OPTIONAL;
	menu_add_entry(sym);
	printd(DEBUG_PARSE, "%s:%d:menuconfig %s\n", zconf_curname(), zconf_lineno(), yyvsp[-1].string);
;}
    break;

  case 21:

    {
	if (current_entry->prompt)
		current_entry->prompt->type = P_MENU;
	else
		zconfprint("warning: menuconfig statement without prompt");
	menu_end_entry();
	printd(DEBUG_PARSE, "%s:%d:endconfig\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 27:

    {
	menu_set_type(S_TRISTATE);
	printd(DEBUG_PARSE, "%s:%d:tristate\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 28:

    {
	menu_add_expr(P_DEFAULT, yyvsp[-2].expr, yyvsp[-1].expr);
	menu_set_type(S_TRISTATE);
	printd(DEBUG_PARSE, "%s:%d:def_boolean\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 29:

    {
	menu_set_type(S_BOOLEAN);
	printd(DEBUG_PARSE, "%s:%d:boolean\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 30:

    {
	menu_add_expr(P_DEFAULT, yyvsp[-2].expr, yyvsp[-1].expr);
	menu_set_type(S_BOOLEAN);
	printd(DEBUG_PARSE, "%s:%d:def_boolean\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 31:

    {
	menu_set_type(S_INT);
	printd(DEBUG_PARSE, "%s:%d:int\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 32:

    {
	menu_set_type(S_HEX);
	printd(DEBUG_PARSE, "%s:%d:hex\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 33:

    {
	menu_set_type(S_STRING);
	printd(DEBUG_PARSE, "%s:%d:string\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 34:

    {
	menu_add_prompt(P_PROMPT, yyvsp[-2].string, yyvsp[-1].expr);
	printd(DEBUG_PARSE, "%s:%d:prompt\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 35:

    {
	menu_add_expr(P_DEFAULT, yyvsp[-2].expr, yyvsp[-1].expr);
	printd(DEBUG_PARSE, "%s:%d:default\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 36:

    {
	menu_add_symbol(P_SELECT, sym_lookup(yyvsp[-2].string, 0), yyvsp[-1].expr);
	printd(DEBUG_PARSE, "%s:%d:select\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 37:

    {
	menu_add_expr(P_RANGE, expr_alloc_comp(E_RANGE,yyvsp[-3].symbol, yyvsp[-2].symbol), yyvsp[-1].expr);
	printd(DEBUG_PARSE, "%s:%d:range\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 38:

    {
	struct symbol *sym = sym_lookup(NULL, 0);
	sym->flags |= SYMBOL_CHOICE;
	menu_add_entry(sym);
	menu_add_expr(P_CHOICE, NULL, NULL);
	printd(DEBUG_PARSE, "%s:%d:choice\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 39:

    {
	menu_end_entry();
	menu_add_menu();
;}
    break;

  case 40:

    {
	if (zconf_endtoken(yyvsp[0].token, T_CHOICE, T_ENDCHOICE)) {
		menu_end_menu();
		printd(DEBUG_PARSE, "%s:%d:endchoice\n", zconf_curname(), zconf_lineno());
	}
;}
    break;

  case 42:

    {
	printf("%s:%d: missing 'endchoice' for this 'choice' statement\n", current_menu->file->name, current_menu->lineno);
	zconfnerrs++;
;}
    break;

  case 48:

    {
	menu_add_prompt(P_PROMPT, yyvsp[-2].string, yyvsp[-1].expr);
	printd(DEBUG_PARSE, "%s:%d:prompt\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 49:

    {
	menu_set_type(S_TRISTATE);
	printd(DEBUG_PARSE, "%s:%d:tristate\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 50:

    {
	menu_set_type(S_BOOLEAN);
	printd(DEBUG_PARSE, "%s:%d:boolean\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 51:

    {
	current_entry->sym->flags |= SYMBOL_OPTIONAL;
	printd(DEBUG_PARSE, "%s:%d:optional\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 52:

    {
	menu_add_symbol(P_DEFAULT, sym_lookup(yyvsp[-2].string, 0), yyvsp[-1].expr);
	printd(DEBUG_PARSE, "%s:%d:default\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 55:

    {
	printd(DEBUG_PARSE, "%s:%d:if\n", zconf_curname(), zconf_lineno());
	menu_add_entry(NULL);
	menu_add_dep(yyvsp[-1].expr);
	menu_end_entry();
	menu_add_menu();
;}
    break;

  case 56:

    {
	if (zconf_endtoken(yyvsp[0].token, T_IF, T_ENDIF)) {
		menu_end_menu();
		printd(DEBUG_PARSE, "%s:%d:endif\n", zconf_curname(), zconf_lineno());
	}
;}
    break;

  case 58:

    {
	printf("%s:%d: missing 'endif' for this 'if' statement\n", current_menu->file->name, current_menu->lineno);
	zconfnerrs++;
;}
    break;

  case 63:

    {
	menu_add_entry(NULL);
	menu_add_prop(P_MENU, yyvsp[-1].string, NULL, NULL);
	printd(DEBUG_PARSE, "%s:%d:menu\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 64:

    {
	menu_end_entry();
	menu_add_menu();
;}
    break;

  case 65:

    {
	if (zconf_endtoken(yyvsp[0].token, T_MENU, T_ENDMENU)) {
		menu_end_menu();
		printd(DEBUG_PARSE, "%s:%d:endmenu\n", zconf_curname(), zconf_lineno());
	}
;}
    break;

  case 67:

    {
	printf("%s:%d: missing 'endmenu' for this 'menu' statement\n", current_menu->file->name, current_menu->lineno);
	zconfnerrs++;
;}
    break;

  case 72:

    { zconfprint("invalid menu option"); yyerrok; ;}
    break;

  case 73:

    {
	yyval.string = yyvsp[-1].string;
	printd(DEBUG_PARSE, "%s:%d:source %s\n", zconf_curname(), zconf_lineno(), yyvsp[-1].string);
;}
    break;

  case 74:

    {
	zconf_nextfile(yyvsp[0].string);
;}
    break;

  case 75:

    {
	menu_add_entry(NULL);
	menu_add_prop(P_COMMENT, yyvsp[-1].string, NULL, NULL);
	printd(DEBUG_PARSE, "%s:%d:comment\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 76:

    {
	menu_end_entry();
;}
    break;

  case 77:

    {
	printd(DEBUG_PARSE, "%s:%d:help\n", zconf_curname(), zconf_lineno());
	zconf_starthelp();
;}
    break;

  case 78:

    {
	current_entry->sym->help = yyvsp[0].string;
;}
    break;

  case 82:

    {
	menu_add_dep(yyvsp[-1].expr);
	printd(DEBUG_PARSE, "%s:%d:depends on\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 83:

    {
	menu_add_dep(yyvsp[-1].expr);
	printd(DEBUG_PARSE, "%s:%d:depends\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 84:

    {
	menu_add_dep(yyvsp[-1].expr);
	printd(DEBUG_PARSE, "%s:%d:requires\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 86:

    {
	menu_add_prop(P_PROMPT, yyvsp[-1].string, NULL, yyvsp[0].expr);
;}
    break;

  case 89:

    { yyval.token = T_ENDMENU; ;}
    break;

  case 90:

    { yyval.token = T_ENDCHOICE; ;}
    break;

  case 91:

    { yyval.token = T_ENDIF; ;}
    break;

  case 94:

    { yyval.expr = NULL; ;}
    break;

  case 95:

    { yyval.expr = yyvsp[0].expr; ;}
    break;

  case 96:

    { yyval.expr = expr_alloc_symbol(yyvsp[0].symbol); ;}
    break;

  case 97:

    { yyval.expr = expr_alloc_comp(E_EQUAL, yyvsp[-2].symbol, yyvsp[0].symbol); ;}
    break;

  case 98:

    { yyval.expr = expr_alloc_comp(E_UNEQUAL, yyvsp[-2].symbol, yyvsp[0].symbol); ;}
    break;

  case 99:

    { yyval.expr = yyvsp[-1].expr; ;}
    break;

  case 100:

    { yyval.expr = expr_alloc_one(E_NOT, yyvsp[0].expr); ;}
    break;

  case 101:

    { yyval.expr = expr_alloc_two(E_OR, yyvsp[-2].expr, yyvsp[0].expr); ;}
    break;

  case 102:

    { yyval.expr = expr_alloc_two(E_AND, yyvsp[-2].expr, yyvsp[0].expr); ;}
    break;

  case 103:

    { yyval.symbol = sym_lookup(yyvsp[0].string, 0); free(yyvsp[0].string); ;}
    break;

  case 104:

    { yyval.symbol = sym_lookup(yyvsp[0].string, 1); free(yyvsp[0].string); ;}
    break;


    }

/* Line 999 of yacc.c.  */


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
	  char *yymsg;
	  int yyx, yycount;

	  yycount = 0;
	  /* Start YYX at -YYN if negative to avoid negative indexes in
	     YYCHECK.  */
	  for (yyx = yyn < 0 ? -yyn : 0;
	       yyx < (int) (sizeof (yytname) / sizeof (char *)); yyx++)
	    if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
	      yysize += yystrlen (yytname[yyx]) + 15, yycount++;
	  yysize += yystrlen ("syntax error, unexpected ") + 1;
	  yysize += yystrlen (yytname[yytype]);
	  yymsg = (char *) YYSTACK_ALLOC (yysize);
	  if (yymsg != 0)
	    {
	      char *yyp = yystpcpy (yymsg, "syntax error, unexpected ");
	      yyp = yystpcpy (yyp, yytname[yytype]);

	      if (yycount < 5)
		{
		  yycount = 0;
		  for (yyx = yyn < 0 ? -yyn : 0;
		       yyx < (int) (sizeof (yytname) / sizeof (char *));
		       yyx++)
		    if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
		      {
			const char *yyq = ! yycount ? ", expecting " : " or ";
			yyp = yystpcpy (yyp, yyq);
			yyp = yystpcpy (yyp, yytname[yyx]);
			yycount++;
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
      /* If just tried and failed to reuse lookahead token after an
	 error, discard it.  */

      /* Return failure if at end of input.  */
      if (yychar == YYEOF)
        {
	  /* Pop the error token.  */
          YYPOPSTACK;
	  /* Pop the rest of the stack.  */
	  while (yyss < yyssp)
	    {
	      YYDSYMPRINTF ("Error: popping", yystos[*yyssp], yyvsp, yylsp);
	      yydestruct (yystos[*yyssp], yyvsp);
	      YYPOPSTACK;
	    }
	  YYABORT;
        }

      YYDSYMPRINTF ("Error: discarding", yytoken, &yylval, &yylloc);
      yydestruct (yytoken, &yylval);
      yychar = YYEMPTY;

    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*----------------------------------------------------.
| yyerrlab1 -- error raised explicitly by an action.  |
`----------------------------------------------------*/
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

      YYDSYMPRINTF ("Error: popping", yystos[*yyssp], yyvsp, yylsp);
      yydestruct (yystos[yystate], yyvsp);
      yyvsp--;
      yystate = *--yyssp;

      YY_STACK_PRINT (yyss, yyssp);
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  YYDPRINTF ((stderr, "Shifting error token, "));

  *++yyvsp = yylval;


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





void conf_parse(const char *name)
{
	struct symbol *sym;
	int i;

	zconf_initscan(name);

	sym_init();
	menu_init();
	modules_sym = sym_lookup("MODULES", 0);
	rootmenu.prompt = menu_add_prop(P_MENU, "Linux Kernel Configuration", NULL, NULL);

	//zconfdebug = 1;
	zconfparse();
	if (zconfnerrs)
		exit(1);
	menu_finalize(&rootmenu);
	for_all_symbols(i, sym) {
                if (!(sym->flags & SYMBOL_CHECKED) && sym_check_deps(sym))
                        printf("\n");
		else
			sym->flags |= SYMBOL_CHECK_DONE;
        }

	sym_change_count = 1;
}

const char *zconf_tokenname(int token)
{
	switch (token) {
	case T_MENU:		return "menu";
	case T_ENDMENU:		return "endmenu";
	case T_CHOICE:		return "choice";
	case T_ENDCHOICE:	return "endchoice";
	case T_IF:		return "if";
	case T_ENDIF:		return "endif";
	}
	return "<token>";
}

static bool zconf_endtoken(int token, int starttoken, int endtoken)
{
	if (token != endtoken) {
		zconfprint("unexpected '%s' within %s block", zconf_tokenname(token), zconf_tokenname(starttoken));
		zconfnerrs++;
		return false;
	}
	if (current_menu->file != current_file) {
		zconfprint("'%s' in different file than '%s'", zconf_tokenname(token), zconf_tokenname(starttoken));
		zconfprint("location of the '%s'", zconf_tokenname(starttoken));
		zconfnerrs++;
		return false;
	}
	return true;
}

static void zconfprint(const char *err, ...)
{
	va_list ap;

	fprintf(stderr, "%s:%d: ", zconf_curname(), zconf_lineno() + 1);
	va_start(ap, err);
	vfprintf(stderr, err, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}

static void zconferror(const char *err)
{
	fprintf(stderr, "%s:%d: %s\n", zconf_curname(), zconf_lineno() + 1, err);
}

void print_quoted_string(FILE *out, const char *str)
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

void print_symbol(FILE *out, struct menu *menu)
{
	struct symbol *sym = menu->sym;
	struct property *prop;

	if (sym_is_choice(sym))
		fprintf(out, "choice\n");
	else
		fprintf(out, "config %s\n", sym->name);
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
		default:
			fprintf(out, "  unknown prop %d!\n", prop->type);
			break;
		}
	}
	if (sym->help) {
		int len = strlen(sym->help);
		while (sym->help[--len] == '\n')
			sym->help[len] = 0;
		fprintf(out, "  help\n%s\n", sym->help);
	}
	fputc('\n', out);
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
			fputs("\n", out);
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


