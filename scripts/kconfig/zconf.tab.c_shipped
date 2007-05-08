/* A Bison parser, made by GNU Bison 2.1.  */

/* Skeleton parser for Yacc-like parsing with Bison,
   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

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
   Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

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

/* Bison version.  */
#define YYBISON_VERSION "2.1"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Using locations.  */
#define YYLSP_NEEDED 0

/* Substitute the variable and function names.  */
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
     T_TYPE = 275,
     T_DEFAULT = 276,
     T_SELECT = 277,
     T_RANGE = 278,
     T_OPTION = 279,
     T_ON = 280,
     T_WORD = 281,
     T_WORD_QUOTE = 282,
     T_UNEQUAL = 283,
     T_CLOSE_PAREN = 284,
     T_OPEN_PAREN = 285,
     T_EOL = 286,
     T_OR = 287,
     T_AND = 288,
     T_EQUAL = 289,
     T_NOT = 290
   };
#endif
/* Tokens.  */
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
#define T_TYPE 275
#define T_DEFAULT 276
#define T_SELECT 277
#define T_RANGE 278
#define T_OPTION 279
#define T_ON 280
#define T_WORD 281
#define T_WORD_QUOTE 282
#define T_UNEQUAL 283
#define T_CLOSE_PAREN 284
#define T_OPEN_PAREN 285
#define T_EOL 286
#define T_OR 287
#define T_AND 288
#define T_EQUAL 289
#define T_NOT 290




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

#include "zconf.hash.c"

#define printd(mask, fmt...) if (cdebug & (mask)) printf(fmt)

#define PRINTD		0x0001
#define DEBUG_PARSE	0x0002

int cdebug = PRINTD;

extern int zconflex(void);
static void zconfprint(const char *err, ...);
static void zconf_error(const char *err, ...);
static void zconferror(const char *err);
static bool zconf_endtoken(struct kconf_id *id, int starttoken, int endtoken);

struct symbol *symbol_hash[257];

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

#if ! defined (YYSTYPE) && ! defined (YYSTYPE_IS_DECLARED)

typedef union YYSTYPE {
	char *string;
	struct file *file;
	struct symbol *symbol;
	struct expr *expr;
	struct menu *menu;
	struct kconf_id *id;
} YYSTYPE;
/* Line 196 of yacc.c.  */

# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif



/* Copy the second part of user declarations.  */


/* Line 219 of yacc.c.  */


#if ! defined (YYSIZE_T) && defined (__SIZE_TYPE__)
# define YYSIZE_T __SIZE_TYPE__
#endif
#if ! defined (YYSIZE_T) && defined (size_t)
# define YYSIZE_T size_t
#endif
#if ! defined (YYSIZE_T) && (defined (__STDC__) || defined (__cplusplus))
# include <stddef.h> /* INFRINGES ON USER NAME SPACE */
# define YYSIZE_T size_t
#endif
#if ! defined (YYSIZE_T)
# define YYSIZE_T unsigned int
#endif

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

#if ! defined (yyoverflow) || YYERROR_VERBOSE

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if defined (__STDC__) || defined (__cplusplus)
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#     define YYINCLUDED_STDLIB_H
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's `empty if-body' warning. */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2005 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM ((YYSIZE_T) -1)
#  endif
#  ifdef __cplusplus
extern "C" {
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if (! defined (malloc) && ! defined (YYINCLUDED_STDLIB_H) \
	&& (defined (__STDC__) || defined (__cplusplus)))
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if (! defined (free) && ! defined (YYINCLUDED_STDLIB_H) \
	&& (defined (__STDC__) || defined (__cplusplus)))
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifdef __cplusplus
}
#  endif
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
	  YYSIZE_T yyi;				\
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
#define YYFINAL  3
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   275

/* YYNTOKENS -- Number of terminals. */
#define YYNTOKENS  36
/* YYNNTS -- Number of nonterminals. */
#define YYNNTS  45
/* YYNRULES -- Number of rules. */
#define YYNRULES  110
/* YYNRULES -- Number of states. */
#define YYNSTATES  183

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   290

#define YYTRANSLATE(YYX)						\
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
      35
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const unsigned short int yyprhs[] =
{
       0,     0,     3,     5,     6,     9,    12,    15,    20,    23,
      28,    33,    37,    39,    41,    43,    45,    47,    49,    51,
      53,    55,    57,    59,    61,    63,    67,    70,    74,    77,
      81,    84,    85,    88,    91,    94,    97,   100,   103,   107,
     112,   117,   122,   128,   132,   133,   137,   138,   141,   144,
     147,   149,   153,   154,   157,   160,   163,   166,   169,   174,
     178,   181,   186,   187,   190,   194,   196,   200,   201,   204,
     207,   210,   214,   217,   219,   223,   224,   227,   230,   233,
     237,   241,   244,   247,   250,   251,   254,   257,   260,   265,
     269,   273,   274,   277,   279,   281,   284,   287,   290,   292,
     295,   296,   299,   301,   305,   309,   313,   316,   320,   324,
     326
};

/* YYRHS -- A `-1'-separated list of the rules' RHS. */
static const yysigned_char yyrhs[] =
{
      37,     0,    -1,    38,    -1,    -1,    38,    40,    -1,    38,
      54,    -1,    38,    65,    -1,    38,     3,    75,    77,    -1,
      38,    76,    -1,    38,    26,     1,    31,    -1,    38,    39,
       1,    31,    -1,    38,     1,    31,    -1,    16,    -1,    19,
      -1,    20,    -1,    22,    -1,    18,    -1,    23,    -1,    21,
      -1,    31,    -1,    60,    -1,    69,    -1,    43,    -1,    45,
      -1,    67,    -1,    26,     1,    31,    -1,     1,    31,    -1,
      10,    26,    31,    -1,    42,    46,    -1,    11,    26,    31,
      -1,    44,    46,    -1,    -1,    46,    47,    -1,    46,    48,
      -1,    46,    73,    -1,    46,    71,    -1,    46,    41,    -1,
      46,    31,    -1,    20,    74,    31,    -1,    19,    75,    78,
      31,    -1,    21,    79,    78,    31,    -1,    22,    26,    78,
      31,    -1,    23,    80,    80,    78,    31,    -1,    24,    49,
      31,    -1,    -1,    49,    26,    50,    -1,    -1,    34,    75,
      -1,     7,    31,    -1,    51,    55,    -1,    76,    -1,    52,
      57,    53,    -1,    -1,    55,    56,    -1,    55,    73,    -1,
      55,    71,    -1,    55,    31,    -1,    55,    41,    -1,    19,
      75,    78,    31,    -1,    20,    74,    31,    -1,    18,    31,
      -1,    21,    26,    78,    31,    -1,    -1,    57,    40,    -1,
      14,    79,    77,    -1,    76,    -1,    58,    61,    59,    -1,
      -1,    61,    40,    -1,    61,    65,    -1,    61,    54,    -1,
       4,    75,    31,    -1,    62,    72,    -1,    76,    -1,    63,
      66,    64,    -1,    -1,    66,    40,    -1,    66,    65,    -1,
      66,    54,    -1,     6,    75,    31,    -1,     9,    75,    31,
      -1,    68,    72,    -1,    12,    31,    -1,    70,    13,    -1,
      -1,    72,    73,    -1,    72,    31,    -1,    72,    41,    -1,
      16,    25,    79,    31,    -1,    16,    79,    31,    -1,    17,
      79,    31,    -1,    -1,    75,    78,    -1,    26,    -1,    27,
      -1,     5,    31,    -1,     8,    31,    -1,    15,    31,    -1,
      31,    -1,    77,    31,    -1,    -1,    14,    79,    -1,    80,
      -1,    80,    34,    80,    -1,    80,    28,    80,    -1,    30,
      79,    29,    -1,    35,    79,    -1,    79,    32,    79,    -1,
      79,    33,    79,    -1,    26,    -1,    27,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const unsigned short int yyrline[] =
{
       0,   105,   105,   107,   109,   110,   111,   112,   113,   114,
     115,   119,   123,   123,   123,   123,   123,   123,   123,   127,
     128,   129,   130,   131,   132,   136,   137,   143,   151,   157,
     165,   175,   177,   178,   179,   180,   181,   182,   185,   193,
     199,   209,   215,   221,   224,   226,   237,   238,   243,   252,
     257,   265,   268,   270,   271,   272,   273,   274,   277,   283,
     294,   300,   310,   312,   317,   325,   333,   336,   338,   339,
     340,   345,   352,   357,   365,   368,   370,   371,   372,   375,
     383,   390,   397,   403,   410,   412,   413,   414,   417,   422,
     427,   435,   437,   442,   443,   446,   447,   448,   452,   453,
     456,   457,   460,   461,   462,   463,   464,   465,   466,   469,
     470
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || YYTOKEN_TABLE
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals. */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "T_MAINMENU", "T_MENU", "T_ENDMENU",
  "T_SOURCE", "T_CHOICE", "T_ENDCHOICE", "T_COMMENT", "T_CONFIG",
  "T_MENUCONFIG", "T_HELP", "T_HELPTEXT", "T_IF", "T_ENDIF", "T_DEPENDS",
  "T_REQUIRES", "T_OPTIONAL", "T_PROMPT", "T_TYPE", "T_DEFAULT",
  "T_SELECT", "T_RANGE", "T_OPTION", "T_ON", "T_WORD", "T_WORD_QUOTE",
  "T_UNEQUAL", "T_CLOSE_PAREN", "T_OPEN_PAREN", "T_EOL", "T_OR", "T_AND",
  "T_EQUAL", "T_NOT", "$accept", "input", "stmt_list", "option_name",
  "common_stmt", "option_error", "config_entry_start", "config_stmt",
  "menuconfig_entry_start", "menuconfig_stmt", "config_option_list",
  "config_option", "symbol_option", "symbol_option_list",
  "symbol_option_arg", "choice", "choice_entry", "choice_end",
  "choice_stmt", "choice_option_list", "choice_option", "choice_block",
  "if_entry", "if_end", "if_stmt", "if_block", "menu", "menu_entry",
  "menu_end", "menu_stmt", "menu_block", "source_stmt", "comment",
  "comment_stmt", "help_start", "help", "depends_list", "depends",
  "prompt_stmt_opt", "prompt", "end", "nl", "if_expr", "expr", "symbol", 0
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
     285,   286,   287,   288,   289,   290
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const unsigned char yyr1[] =
{
       0,    36,    37,    38,    38,    38,    38,    38,    38,    38,
      38,    38,    39,    39,    39,    39,    39,    39,    39,    40,
      40,    40,    40,    40,    40,    41,    41,    42,    43,    44,
      45,    46,    46,    46,    46,    46,    46,    46,    47,    47,
      47,    47,    47,    48,    49,    49,    50,    50,    51,    52,
      53,    54,    55,    55,    55,    55,    55,    55,    56,    56,
      56,    56,    57,    57,    58,    59,    60,    61,    61,    61,
      61,    62,    63,    64,    65,    66,    66,    66,    66,    67,
      68,    69,    70,    71,    72,    72,    72,    72,    73,    73,
      73,    74,    74,    75,    75,    76,    76,    76,    77,    77,
      78,    78,    79,    79,    79,    79,    79,    79,    79,    80,
      80
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const unsigned char yyr2[] =
{
       0,     2,     1,     0,     2,     2,     2,     4,     2,     4,
       4,     3,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     3,     2,     3,     2,     3,
       2,     0,     2,     2,     2,     2,     2,     2,     3,     4,
       4,     4,     5,     3,     0,     3,     0,     2,     2,     2,
       1,     3,     0,     2,     2,     2,     2,     2,     4,     3,
       2,     4,     0,     2,     3,     1,     3,     0,     2,     2,
       2,     3,     2,     1,     3,     0,     2,     2,     2,     3,
       3,     2,     2,     2,     0,     2,     2,     2,     4,     3,
       3,     0,     2,     1,     1,     2,     2,     2,     1,     2,
       0,     2,     1,     3,     3,     3,     2,     3,     3,     1,
       1
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const unsigned char yydefact[] =
{
       3,     0,     0,     1,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    12,    16,    13,    14,
      18,    15,    17,     0,    19,     0,     4,    31,    22,    31,
      23,    52,    62,     5,    67,    20,    84,    75,     6,    24,
      84,    21,     8,    11,    93,    94,     0,     0,    95,     0,
      48,    96,     0,     0,     0,   109,   110,     0,     0,     0,
     102,    97,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    98,     7,    71,    79,    80,    27,    29,     0,
     106,     0,     0,    64,     0,     0,     9,    10,     0,     0,
       0,     0,     0,    91,     0,     0,     0,    44,     0,    37,
      36,    32,    33,     0,    35,    34,     0,     0,    91,     0,
      56,    57,    53,    55,    54,    63,    51,    50,    68,    70,
      66,    69,    65,    86,    87,    85,    76,    78,    74,    77,
      73,    99,   105,   107,   108,   104,   103,    26,    82,     0,
       0,     0,   100,     0,   100,   100,   100,     0,     0,     0,
      83,    60,   100,     0,   100,     0,    89,    90,     0,     0,
      38,    92,     0,     0,   100,    46,    43,    25,     0,    59,
       0,    88,   101,    39,    40,    41,     0,     0,    45,    58,
      61,    42,    47
};

/* YYDEFGOTO[NTERM-NUM]. */
static const short int yydefgoto[] =
{
      -1,     1,     2,    25,    26,   100,    27,    28,    29,    30,
      64,   101,   102,   148,   178,    31,    32,   116,    33,    66,
     112,    67,    34,   120,    35,    68,    36,    37,   128,    38,
      70,    39,    40,    41,   103,   104,    69,   105,   143,   144,
      42,    73,   159,    59,    60
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -135
static const short int yypact[] =
{
    -135,     2,   170,  -135,   -14,    56,    56,    -8,    56,    24,
      67,    56,     7,    14,    62,    97,  -135,  -135,  -135,  -135,
    -135,  -135,  -135,   156,  -135,   166,  -135,  -135,  -135,  -135,
    -135,  -135,  -135,  -135,  -135,  -135,  -135,  -135,  -135,  -135,
    -135,  -135,  -135,  -135,  -135,  -135,   138,   151,  -135,   152,
    -135,  -135,   163,   167,   176,  -135,  -135,    62,    62,   185,
     -19,  -135,   188,   190,    42,   103,   194,    85,    70,   222,
      70,   132,  -135,   191,  -135,  -135,  -135,  -135,  -135,   127,
    -135,    62,    62,   191,   104,   104,  -135,  -135,   193,   203,
       9,    62,    56,    56,    62,   161,   104,  -135,   196,  -135,
    -135,  -135,  -135,   233,  -135,  -135,   204,    56,    56,   221,
    -135,  -135,  -135,  -135,  -135,  -135,  -135,  -135,  -135,  -135,
    -135,  -135,  -135,  -135,  -135,  -135,  -135,  -135,  -135,  -135,
    -135,  -135,  -135,   219,  -135,  -135,  -135,  -135,  -135,    62,
     209,   212,   240,   224,   240,    -1,   240,   104,    41,   225,
    -135,  -135,   240,   226,   240,   218,  -135,  -135,    62,   227,
    -135,  -135,   228,   229,   240,   230,  -135,  -135,   231,  -135,
     232,  -135,   112,  -135,  -135,  -135,   234,    56,  -135,  -135,
    -135,  -135,  -135
};

/* YYPGOTO[NTERM-NUM].  */
static const short int yypgoto[] =
{
    -135,  -135,  -135,  -135,    94,   -45,  -135,  -135,  -135,  -135,
     237,  -135,  -135,  -135,  -135,  -135,  -135,  -135,   -54,  -135,
    -135,  -135,  -135,  -135,  -135,  -135,  -135,  -135,  -135,     1,
    -135,  -135,  -135,  -135,  -135,   195,   235,   -44,   159,    -5,
      98,   210,  -134,   -53,   -77
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -82
static const short int yytable[] =
{
      46,    47,     3,    49,    79,    80,    52,   135,   136,    84,
     161,   162,   163,   158,   119,    85,   127,    43,   168,   147,
     170,   111,   114,    48,   124,   125,   124,   125,   133,   134,
     176,    81,    82,    53,   139,    55,    56,   140,   141,    57,
      54,   145,   -28,    88,    58,   -28,   -28,   -28,   -28,   -28,
     -28,   -28,   -28,   -28,    89,    50,   -28,   -28,    90,    91,
     -28,    92,    93,    94,    95,    96,    97,   165,    98,   121,
     164,   129,   166,    99,     6,     7,     8,     9,    10,    11,
      12,    13,    44,    45,    14,    15,   155,   142,    55,    56,
       7,     8,    57,    10,    11,    12,    13,    58,    51,    14,
      15,    24,   152,   -30,    88,   172,   -30,   -30,   -30,   -30,
     -30,   -30,   -30,   -30,   -30,    89,    24,   -30,   -30,    90,
      91,   -30,    92,    93,    94,    95,    96,    97,    61,    98,
      55,    56,   -81,    88,    99,   -81,   -81,   -81,   -81,   -81,
     -81,   -81,   -81,   -81,    81,    82,   -81,   -81,    90,    91,
     -81,   -81,   -81,   -81,   -81,   -81,   132,    62,    98,    81,
      82,   115,   118,   123,   126,   117,   122,    63,   130,    72,
      -2,     4,   182,     5,     6,     7,     8,     9,    10,    11,
      12,    13,    74,    75,    14,    15,    16,   146,    17,    18,
      19,    20,    21,    22,    76,    88,    23,   149,    77,   -49,
     -49,    24,   -49,   -49,   -49,   -49,    89,    78,   -49,   -49,
      90,    91,   106,   107,   108,   109,    72,    81,    82,    86,
      98,    87,   131,    88,   137,   110,   -72,   -72,   -72,   -72,
     -72,   -72,   -72,   -72,   138,   151,   -72,   -72,    90,    91,
     156,    81,    82,   157,    81,    82,   150,   154,    98,   171,
      81,    82,    82,   123,   158,   160,   167,   169,   173,   174,
     175,   113,   179,   180,   177,   181,    65,   153,     0,    83,
       0,     0,     0,     0,     0,    71
};

static const short int yycheck[] =
{
       5,     6,     0,     8,    57,    58,    11,    84,    85,    28,
     144,   145,   146,    14,    68,    34,    70,    31,   152,    96,
     154,    66,    66,    31,    69,    69,    71,    71,    81,    82,
     164,    32,    33,    26,    25,    26,    27,    90,    91,    30,
      26,    94,     0,     1,    35,     3,     4,     5,     6,     7,
       8,     9,    10,    11,    12,    31,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    26,    26,    68,
     147,    70,    31,    31,     4,     5,     6,     7,     8,     9,
      10,    11,    26,    27,    14,    15,   139,    92,    26,    27,
       5,     6,    30,     8,     9,    10,    11,    35,    31,    14,
      15,    31,   107,     0,     1,   158,     3,     4,     5,     6,
       7,     8,     9,    10,    11,    12,    31,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,    31,    26,
      26,    27,     0,     1,    31,     3,     4,     5,     6,     7,
       8,     9,    10,    11,    32,    33,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    29,     1,    26,    32,
      33,    67,    68,    31,    70,    67,    68,     1,    70,    31,
       0,     1,   177,     3,     4,     5,     6,     7,     8,     9,
      10,    11,    31,    31,    14,    15,    16,    26,    18,    19,
      20,    21,    22,    23,    31,     1,    26,     1,    31,     5,
       6,    31,     8,     9,    10,    11,    12,    31,    14,    15,
      16,    17,    18,    19,    20,    21,    31,    32,    33,    31,
      26,    31,    31,     1,    31,    31,     4,     5,     6,     7,
       8,     9,    10,    11,    31,    31,    14,    15,    16,    17,
      31,    32,    33,    31,    32,    33,    13,    26,    26,    31,
      32,    33,    33,    31,    14,    31,    31,    31,    31,    31,
      31,    66,    31,    31,    34,    31,    29,   108,    -1,    59,
      -1,    -1,    -1,    -1,    -1,    40
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const unsigned char yystos[] =
{
       0,    37,    38,     0,     1,     3,     4,     5,     6,     7,
       8,     9,    10,    11,    14,    15,    16,    18,    19,    20,
      21,    22,    23,    26,    31,    39,    40,    42,    43,    44,
      45,    51,    52,    54,    58,    60,    62,    63,    65,    67,
      68,    69,    76,    31,    26,    27,    75,    75,    31,    75,
      31,    31,    75,    26,    26,    26,    27,    30,    35,    79,
      80,    31,     1,     1,    46,    46,    55,    57,    61,    72,
      66,    72,    31,    77,    31,    31,    31,    31,    31,    79,
      79,    32,    33,    77,    28,    34,    31,    31,     1,    12,
      16,    17,    19,    20,    21,    22,    23,    24,    26,    31,
      41,    47,    48,    70,    71,    73,    18,    19,    20,    21,
      31,    41,    56,    71,    73,    40,    53,    76,    40,    54,
      59,    65,    76,    31,    41,    73,    40,    54,    64,    65,
      76,    31,    29,    79,    79,    80,    80,    31,    31,    25,
      79,    79,    75,    74,    75,    79,    26,    80,    49,     1,
      13,    31,    75,    74,    26,    79,    31,    31,    14,    78,
      31,    78,    78,    78,    80,    26,    31,    31,    78,    31,
      78,    31,    79,    31,    31,    31,    78,    34,    50,    31,
      31,    31,    75
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
      YYPOPSTACK;						\
      goto yybackup;						\
    }								\
  else								\
    {								\
      yyerror (YY_("syntax error: cannot back up")); \
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
      yysymprint (stderr,					\
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
  unsigned long int yylno = yyrline[yyrule];
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %lu), ",
             yyrule - 1, yylno);
  /* Print the symbols being reduced, and their result.  */
  for (yyi = yyprhs[yyrule]; 0 <= yyrhs[yyi]; yyi++)
    YYFPRINTF (stderr, "%s ", yytname[yyrhs[yyi]]);
  YYFPRINTF (stderr, "-> %s\n", yytname[yyr1[yyrule]]);
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
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
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
  const char *yys = yystr;

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
      size_t yyn = 0;
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

#endif /* YYERROR_VERBOSE */



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
      case 63: /* "menu_entry" */

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
    ;
#endif
#endif
{
  
  int yystate;
  int yyn;
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
  short int *yyssp;

  /* The semantic value stack.  */
  YYSTYPE yyvsa[YYINITDEPTH];
  YYSTYPE *yyvs = yyvsa;
  YYSTYPE *yyvsp;



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
	short int *yyss1 = yyss;


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
	short int *yyss1 = yyss;
	union yyalloc *yyptr =
	  (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
	if (! yyptr)
	  goto yyexhaustedlab;
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
        case 8:

    { zconf_error("unexpected end statement"); ;}
    break;

  case 9:

    { zconf_error("unknown statement \"%s\"", (yyvsp[-2].string)); ;}
    break;

  case 10:

    {
	zconf_error("unexpected option \"%s\"", kconf_id_strings + (yyvsp[-2].id)->name);
;}
    break;

  case 11:

    { zconf_error("invalid statement"); ;}
    break;

  case 25:

    { zconf_error("unknown option \"%s\"", (yyvsp[-2].string)); ;}
    break;

  case 26:

    { zconf_error("invalid option"); ;}
    break;

  case 27:

    {
	struct symbol *sym = sym_lookup((yyvsp[-1].string), 0);
	sym->flags |= SYMBOL_OPTIONAL;
	menu_add_entry(sym);
	printd(DEBUG_PARSE, "%s:%d:config %s\n", zconf_curname(), zconf_lineno(), (yyvsp[-1].string));
;}
    break;

  case 28:

    {
	menu_end_entry();
	printd(DEBUG_PARSE, "%s:%d:endconfig\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 29:

    {
	struct symbol *sym = sym_lookup((yyvsp[-1].string), 0);
	sym->flags |= SYMBOL_OPTIONAL;
	menu_add_entry(sym);
	printd(DEBUG_PARSE, "%s:%d:menuconfig %s\n", zconf_curname(), zconf_lineno(), (yyvsp[-1].string));
;}
    break;

  case 30:

    {
	if (current_entry->prompt)
		current_entry->prompt->type = P_MENU;
	else
		zconfprint("warning: menuconfig statement without prompt");
	menu_end_entry();
	printd(DEBUG_PARSE, "%s:%d:endconfig\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 38:

    {
	menu_set_type((yyvsp[-2].id)->stype);
	printd(DEBUG_PARSE, "%s:%d:type(%u)\n",
		zconf_curname(), zconf_lineno(),
		(yyvsp[-2].id)->stype);
;}
    break;

  case 39:

    {
	menu_add_prompt(P_PROMPT, (yyvsp[-2].string), (yyvsp[-1].expr));
	printd(DEBUG_PARSE, "%s:%d:prompt\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 40:

    {
	menu_add_expr(P_DEFAULT, (yyvsp[-2].expr), (yyvsp[-1].expr));
	if ((yyvsp[-3].id)->stype != S_UNKNOWN)
		menu_set_type((yyvsp[-3].id)->stype);
	printd(DEBUG_PARSE, "%s:%d:default(%u)\n",
		zconf_curname(), zconf_lineno(),
		(yyvsp[-3].id)->stype);
;}
    break;

  case 41:

    {
	menu_add_symbol(P_SELECT, sym_lookup((yyvsp[-2].string), 0), (yyvsp[-1].expr));
	printd(DEBUG_PARSE, "%s:%d:select\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 42:

    {
	menu_add_expr(P_RANGE, expr_alloc_comp(E_RANGE,(yyvsp[-3].symbol), (yyvsp[-2].symbol)), (yyvsp[-1].expr));
	printd(DEBUG_PARSE, "%s:%d:range\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 45:

    {
	struct kconf_id *id = kconf_id_lookup((yyvsp[-1].string), strlen((yyvsp[-1].string)));
	if (id && id->flags & TF_OPTION)
		menu_add_option(id->token, (yyvsp[0].string));
	else
		zconfprint("warning: ignoring unknown option %s", (yyvsp[-1].string));
	free((yyvsp[-1].string));
;}
    break;

  case 46:

    { (yyval.string) = NULL; ;}
    break;

  case 47:

    { (yyval.string) = (yyvsp[0].string); ;}
    break;

  case 48:

    {
	struct symbol *sym = sym_lookup(NULL, 0);
	sym->flags |= SYMBOL_CHOICE;
	menu_add_entry(sym);
	menu_add_expr(P_CHOICE, NULL, NULL);
	printd(DEBUG_PARSE, "%s:%d:choice\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 49:

    {
	(yyval.menu) = menu_add_menu();
;}
    break;

  case 50:

    {
	if (zconf_endtoken((yyvsp[0].id), T_CHOICE, T_ENDCHOICE)) {
		menu_end_menu();
		printd(DEBUG_PARSE, "%s:%d:endchoice\n", zconf_curname(), zconf_lineno());
	}
;}
    break;

  case 58:

    {
	menu_add_prompt(P_PROMPT, (yyvsp[-2].string), (yyvsp[-1].expr));
	printd(DEBUG_PARSE, "%s:%d:prompt\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 59:

    {
	if ((yyvsp[-2].id)->stype == S_BOOLEAN || (yyvsp[-2].id)->stype == S_TRISTATE) {
		menu_set_type((yyvsp[-2].id)->stype);
		printd(DEBUG_PARSE, "%s:%d:type(%u)\n",
			zconf_curname(), zconf_lineno(),
			(yyvsp[-2].id)->stype);
	} else
		YYERROR;
;}
    break;

  case 60:

    {
	current_entry->sym->flags |= SYMBOL_OPTIONAL;
	printd(DEBUG_PARSE, "%s:%d:optional\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 61:

    {
	if ((yyvsp[-3].id)->stype == S_UNKNOWN) {
		menu_add_symbol(P_DEFAULT, sym_lookup((yyvsp[-2].string), 0), (yyvsp[-1].expr));
		printd(DEBUG_PARSE, "%s:%d:default\n",
			zconf_curname(), zconf_lineno());
	} else
		YYERROR;
;}
    break;

  case 64:

    {
	printd(DEBUG_PARSE, "%s:%d:if\n", zconf_curname(), zconf_lineno());
	menu_add_entry(NULL);
	menu_add_dep((yyvsp[-1].expr));
	(yyval.menu) = menu_add_menu();
;}
    break;

  case 65:

    {
	if (zconf_endtoken((yyvsp[0].id), T_IF, T_ENDIF)) {
		menu_end_menu();
		printd(DEBUG_PARSE, "%s:%d:endif\n", zconf_curname(), zconf_lineno());
	}
;}
    break;

  case 71:

    {
	menu_add_entry(NULL);
	menu_add_prompt(P_MENU, (yyvsp[-1].string), NULL);
	printd(DEBUG_PARSE, "%s:%d:menu\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 72:

    {
	(yyval.menu) = menu_add_menu();
;}
    break;

  case 73:

    {
	if (zconf_endtoken((yyvsp[0].id), T_MENU, T_ENDMENU)) {
		menu_end_menu();
		printd(DEBUG_PARSE, "%s:%d:endmenu\n", zconf_curname(), zconf_lineno());
	}
;}
    break;

  case 79:

    {
	printd(DEBUG_PARSE, "%s:%d:source %s\n", zconf_curname(), zconf_lineno(), (yyvsp[-1].string));
	zconf_nextfile((yyvsp[-1].string));
;}
    break;

  case 80:

    {
	menu_add_entry(NULL);
	menu_add_prompt(P_COMMENT, (yyvsp[-1].string), NULL);
	printd(DEBUG_PARSE, "%s:%d:comment\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 81:

    {
	menu_end_entry();
;}
    break;

  case 82:

    {
	printd(DEBUG_PARSE, "%s:%d:help\n", zconf_curname(), zconf_lineno());
	zconf_starthelp();
;}
    break;

  case 83:

    {
	current_entry->sym->help = (yyvsp[0].string);
;}
    break;

  case 88:

    {
	menu_add_dep((yyvsp[-1].expr));
	printd(DEBUG_PARSE, "%s:%d:depends on\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 89:

    {
	menu_add_dep((yyvsp[-1].expr));
	printd(DEBUG_PARSE, "%s:%d:depends\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 90:

    {
	menu_add_dep((yyvsp[-1].expr));
	printd(DEBUG_PARSE, "%s:%d:requires\n", zconf_curname(), zconf_lineno());
;}
    break;

  case 92:

    {
	menu_add_prompt(P_PROMPT, (yyvsp[-1].string), (yyvsp[0].expr));
;}
    break;

  case 95:

    { (yyval.id) = (yyvsp[-1].id); ;}
    break;

  case 96:

    { (yyval.id) = (yyvsp[-1].id); ;}
    break;

  case 97:

    { (yyval.id) = (yyvsp[-1].id); ;}
    break;

  case 100:

    { (yyval.expr) = NULL; ;}
    break;

  case 101:

    { (yyval.expr) = (yyvsp[0].expr); ;}
    break;

  case 102:

    { (yyval.expr) = expr_alloc_symbol((yyvsp[0].symbol)); ;}
    break;

  case 103:

    { (yyval.expr) = expr_alloc_comp(E_EQUAL, (yyvsp[-2].symbol), (yyvsp[0].symbol)); ;}
    break;

  case 104:

    { (yyval.expr) = expr_alloc_comp(E_UNEQUAL, (yyvsp[-2].symbol), (yyvsp[0].symbol)); ;}
    break;

  case 105:

    { (yyval.expr) = (yyvsp[-1].expr); ;}
    break;

  case 106:

    { (yyval.expr) = expr_alloc_one(E_NOT, (yyvsp[0].expr)); ;}
    break;

  case 107:

    { (yyval.expr) = expr_alloc_two(E_OR, (yyvsp[-2].expr), (yyvsp[0].expr)); ;}
    break;

  case 108:

    { (yyval.expr) = expr_alloc_two(E_AND, (yyvsp[-2].expr), (yyvsp[0].expr)); ;}
    break;

  case 109:

    { (yyval.symbol) = sym_lookup((yyvsp[0].string), 0); free((yyvsp[0].string)); ;}
    break;

  case 110:

    { (yyval.symbol) = sym_lookup((yyvsp[0].string), 1); free((yyvsp[0].string)); ;}
    break;


      default: break;
    }

/* Line 1126 of yacc.c.  */


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
	  int yytype = YYTRANSLATE (yychar);
	  YYSIZE_T yysize0 = yytnamerr (0, yytname[yytype]);
	  YYSIZE_T yysize = yysize0;
	  YYSIZE_T yysize1;
	  int yysize_overflow = 0;
	  char *yymsg = 0;
#	  define YYERROR_VERBOSE_ARGS_MAXIMUM 5
	  char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
	  int yyx;

#if 0
	  /* This is so xgettext sees the translatable formats that are
	     constructed on the fly.  */
	  YY_("syntax error, unexpected %s");
	  YY_("syntax error, unexpected %s, expecting %s");
	  YY_("syntax error, unexpected %s, expecting %s or %s");
	  YY_("syntax error, unexpected %s, expecting %s or %s or %s");
	  YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s");
#endif
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
	  int yychecklim = YYLAST - yyn;
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
		yysize_overflow |= yysize1 < yysize;
		yysize = yysize1;
		yyfmt = yystpcpy (yyfmt, yyprefix);
		yyprefix = yyor;
	      }

	  yyf = YY_(yyformat);
	  yysize1 = yysize + yystrlen (yyf);
	  yysize_overflow |= yysize1 < yysize;
	  yysize = yysize1;

	  if (!yysize_overflow && yysize <= YYSTACK_ALLOC_MAXIMUM)
	    yymsg = (char *) YYSTACK_ALLOC (yysize);
	  if (yymsg)
	    {
	      /* Avoid sprintf, as that infringes on the user's name space.
		 Don't have undefined behavior even if the translation
		 produced a string with the wrong number of "%s"s.  */
	      char *yyp = yymsg;
	      int yyi = 0;
	      while ((*yyp = *yyf))
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
	      yyerror (yymsg);
	      YYSTACK_FREE (yymsg);
	    }
	  else
	    {
	      yyerror (YY_("syntax error"));
	      goto yyexhaustedlab;
	    }
	}
      else
#endif /* YYERROR_VERBOSE */
	yyerror (YY_("syntax error"));
    }



  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse look-ahead token after an
	 error, discard it.  */

      if (yychar <= YYEOF)
        {
	  /* Return failure if at end of input.  */
	  if (yychar == YYEOF)
	    YYABORT;
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

  /* Pacify compilers like GCC when the user code never invokes
     YYERROR and the label yyerrorlab therefore never appears in user
     code.  */
  if (0)
     goto yyerrorlab;

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
  yyresult = 1;
  goto yyreturn;

#ifndef yyoverflow
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
  if (yychar != YYEOF && yychar != YYEMPTY)
     yydestruct ("Cleanup: discarding lookahead",
		 yytoken, &yylval);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
		  yystos[*yyssp], yyvsp);
      YYPOPSTACK;
    }
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
	menu_finalize(&rootmenu);
	for_all_symbols(i, sym) {
		if (sym_check_deps(sym))
			zconfnerrs++;
        }
	if (zconfnerrs)
		exit(1);
	sym_set_change_count(1);
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


