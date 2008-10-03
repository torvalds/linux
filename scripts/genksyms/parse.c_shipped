/* A Bison parser, made by GNU Bison 2.3.  */

/* Skeleton implementation for Bison's Yacc-like parsers in C

   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

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
#define YYBISON_VERSION "2.3"

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
     EXTENSION_KEYW = 267,
     FLOAT_KEYW = 268,
     INLINE_KEYW = 269,
     INT_KEYW = 270,
     LONG_KEYW = 271,
     REGISTER_KEYW = 272,
     RESTRICT_KEYW = 273,
     SHORT_KEYW = 274,
     SIGNED_KEYW = 275,
     STATIC_KEYW = 276,
     STRUCT_KEYW = 277,
     TYPEDEF_KEYW = 278,
     UNION_KEYW = 279,
     UNSIGNED_KEYW = 280,
     VOID_KEYW = 281,
     VOLATILE_KEYW = 282,
     TYPEOF_KEYW = 283,
     EXPORT_SYMBOL_KEYW = 284,
     ASM_PHRASE = 285,
     ATTRIBUTE_PHRASE = 286,
     BRACE_PHRASE = 287,
     BRACKET_PHRASE = 288,
     EXPRESSION_PHRASE = 289,
     CHAR = 290,
     DOTS = 291,
     IDENT = 292,
     INT = 293,
     REAL = 294,
     STRING = 295,
     TYPE = 296,
     OTHER = 297,
     FILENAME = 298
   };
#endif
/* Tokens.  */
#define ASM_KEYW 258
#define ATTRIBUTE_KEYW 259
#define AUTO_KEYW 260
#define BOOL_KEYW 261
#define CHAR_KEYW 262
#define CONST_KEYW 263
#define DOUBLE_KEYW 264
#define ENUM_KEYW 265
#define EXTERN_KEYW 266
#define EXTENSION_KEYW 267
#define FLOAT_KEYW 268
#define INLINE_KEYW 269
#define INT_KEYW 270
#define LONG_KEYW 271
#define REGISTER_KEYW 272
#define RESTRICT_KEYW 273
#define SHORT_KEYW 274
#define SIGNED_KEYW 275
#define STATIC_KEYW 276
#define STRUCT_KEYW 277
#define TYPEDEF_KEYW 278
#define UNION_KEYW 279
#define UNSIGNED_KEYW 280
#define VOID_KEYW 281
#define VOLATILE_KEYW 282
#define TYPEOF_KEYW 283
#define EXPORT_SYMBOL_KEYW 284
#define ASM_PHRASE 285
#define ATTRIBUTE_PHRASE 286
#define BRACE_PHRASE 287
#define BRACKET_PHRASE 288
#define EXPRESSION_PHRASE 289
#define CHAR 290
#define DOTS 291
#define IDENT 292
#define INT 293
#define REAL 294
#define STRING 295
#define TYPE 296
#define OTHER 297
#define FILENAME 298




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

/* Enabling the token table.  */
#ifndef YYTOKEN_TABLE
# define YYTOKEN_TABLE 0
#endif

#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef int YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif



/* Copy the second part of user declarations.  */


/* Line 216 of yacc.c.  */
#line 223 "scripts/genksyms/parse.c"

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
YYID (int i)
#else
static int
YYID (i)
    int i;
#endif
{
  return i;
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
  yytype_int16 yyss;
  YYSTYPE yyvs;
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
# define YYSTACK_RELOCATE(Stack)					\
    do									\
      {									\
	YYSIZE_T yynewbytes;						\
	YYCOPY (&yyptr->Stack, Stack, yysize);				\
	Stack = &yyptr->Stack;						\
	yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
	yyptr += yynewbytes / sizeof (*yyptr);				\
      }									\
    while (YYID (0))

#endif

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  4
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   523

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  53
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  46
/* YYNRULES -- Number of rules.  */
#define YYNRULES  126
/* YYNRULES -- Number of states.  */
#define YYNSTATES  178

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   298

#define YYTRANSLATE(YYX)						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const yytype_uint8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      47,    49,    48,     2,    46,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    52,    44,
       2,    50,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    51,     2,    45,     2,     2,     2,     2,
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
      35,    36,    37,    38,    39,    40,    41,    42,    43
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const yytype_uint16 yyprhs[] =
{
       0,     0,     3,     5,     8,     9,    12,    13,    18,    19,
      23,    25,    27,    29,    31,    34,    37,    41,    42,    44,
      46,    50,    55,    56,    58,    60,    63,    65,    67,    69,
      71,    73,    75,    77,    79,    81,    87,    92,    95,    98,
     101,   105,   109,   113,   116,   119,   122,   124,   126,   128,
     130,   132,   134,   136,   138,   140,   142,   144,   147,   148,
     150,   152,   155,   157,   159,   161,   163,   166,   168,   170,
     175,   180,   183,   187,   191,   194,   196,   198,   200,   205,
     210,   213,   217,   221,   224,   226,   230,   231,   233,   235,
     239,   242,   245,   247,   248,   250,   252,   257,   262,   265,
     269,   273,   277,   278,   280,   283,   287,   291,   292,   294,
     296,   299,   303,   306,   307,   309,   311,   315,   318,   321,
     323,   326,   327,   330,   333,   334,   336
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int8 yyrhs[] =
{
      54,     0,    -1,    55,    -1,    54,    55,    -1,    -1,    56,
      57,    -1,    -1,    12,    23,    58,    60,    -1,    -1,    23,
      59,    60,    -1,    60,    -1,    84,    -1,    96,    -1,    98,
      -1,     1,    44,    -1,     1,    45,    -1,    64,    61,    44,
      -1,    -1,    62,    -1,    63,    -1,    62,    46,    63,    -1,
      74,    97,    95,    85,    -1,    -1,    65,    -1,    66,    -1,
      65,    66,    -1,    67,    -1,    68,    -1,     5,    -1,    17,
      -1,    21,    -1,    11,    -1,    14,    -1,    69,    -1,    73,
      -1,    28,    47,    65,    48,    49,    -1,    28,    47,    65,
      49,    -1,    22,    37,    -1,    24,    37,    -1,    10,    37,
      -1,    22,    37,    87,    -1,    24,    37,    87,    -1,    10,
      37,    32,    -1,    10,    32,    -1,    22,    87,    -1,    24,
      87,    -1,     7,    -1,    19,    -1,    15,    -1,    16,    -1,
      20,    -1,    25,    -1,    13,    -1,     9,    -1,    26,    -1,
       6,    -1,    41,    -1,    48,    71,    -1,    -1,    72,    -1,
      73,    -1,    72,    73,    -1,     8,    -1,    27,    -1,    31,
      -1,    18,    -1,    70,    74,    -1,    75,    -1,    37,    -1,
      75,    47,    78,    49,    -1,    75,    47,     1,    49,    -1,
      75,    33,    -1,    47,    74,    49,    -1,    47,     1,    49,
      -1,    70,    76,    -1,    77,    -1,    37,    -1,    41,    -1,
      77,    47,    78,    49,    -1,    77,    47,     1,    49,    -1,
      77,    33,    -1,    47,    76,    49,    -1,    47,     1,    49,
      -1,    79,    36,    -1,    79,    -1,    80,    46,    36,    -1,
      -1,    80,    -1,    81,    -1,    80,    46,    81,    -1,    65,
      82,    -1,    70,    82,    -1,    83,    -1,    -1,    37,    -1,
      41,    -1,    83,    47,    78,    49,    -1,    83,    47,     1,
      49,    -1,    83,    33,    -1,    47,    82,    49,    -1,    47,
       1,    49,    -1,    64,    74,    32,    -1,    -1,    86,    -1,
      50,    34,    -1,    51,    88,    45,    -1,    51,     1,    45,
      -1,    -1,    89,    -1,    90,    -1,    89,    90,    -1,    64,
      91,    44,    -1,     1,    44,    -1,    -1,    92,    -1,    93,
      -1,    92,    46,    93,    -1,    76,    95,    -1,    37,    94,
      -1,    94,    -1,    52,    34,    -1,    -1,    95,    31,    -1,
      30,    44,    -1,    -1,    30,    -1,    29,    47,    37,    49,
      44,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   103,   103,   104,   108,   108,   114,   114,   116,   116,
     118,   119,   120,   121,   122,   123,   127,   141,   142,   146,
     154,   167,   173,   174,   178,   179,   183,   189,   193,   194,
     195,   196,   197,   201,   202,   203,   204,   208,   210,   212,
     216,   223,   230,   239,   240,   241,   245,   246,   247,   248,
     249,   250,   251,   252,   253,   254,   255,   259,   264,   265,
     269,   270,   274,   274,   274,   275,   283,   284,   288,   297,
     299,   301,   303,   305,   312,   313,   317,   318,   319,   321,
     323,   325,   327,   332,   333,   334,   338,   339,   343,   344,
     349,   354,   356,   360,   361,   369,   373,   375,   377,   379,
     381,   386,   395,   396,   401,   406,   407,   411,   412,   416,
     417,   421,   423,   428,   429,   433,   434,   438,   439,   440,
     444,   448,   449,   453,   457,   458,   462
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || YYTOKEN_TABLE
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "ASM_KEYW", "ATTRIBUTE_KEYW",
  "AUTO_KEYW", "BOOL_KEYW", "CHAR_KEYW", "CONST_KEYW", "DOUBLE_KEYW",
  "ENUM_KEYW", "EXTERN_KEYW", "EXTENSION_KEYW", "FLOAT_KEYW",
  "INLINE_KEYW", "INT_KEYW", "LONG_KEYW", "REGISTER_KEYW", "RESTRICT_KEYW",
  "SHORT_KEYW", "SIGNED_KEYW", "STATIC_KEYW", "STRUCT_KEYW",
  "TYPEDEF_KEYW", "UNION_KEYW", "UNSIGNED_KEYW", "VOID_KEYW",
  "VOLATILE_KEYW", "TYPEOF_KEYW", "EXPORT_SYMBOL_KEYW", "ASM_PHRASE",
  "ATTRIBUTE_PHRASE", "BRACE_PHRASE", "BRACKET_PHRASE",
  "EXPRESSION_PHRASE", "CHAR", "DOTS", "IDENT", "INT", "REAL", "STRING",
  "TYPE", "OTHER", "FILENAME", "';'", "'}'", "','", "'('", "'*'", "')'",
  "'='", "'{'", "':'", "$accept", "declaration_seq", "declaration", "@1",
  "declaration1", "@2", "@3", "simple_declaration",
  "init_declarator_list_opt", "init_declarator_list", "init_declarator",
  "decl_specifier_seq_opt", "decl_specifier_seq", "decl_specifier",
  "storage_class_specifier", "type_specifier", "simple_type_specifier",
  "ptr_operator", "cvar_qualifier_seq_opt", "cvar_qualifier_seq",
  "cvar_qualifier", "declarator", "direct_declarator", "nested_declarator",
  "direct_nested_declarator", "parameter_declaration_clause",
  "parameter_declaration_list_opt", "parameter_declaration_list",
  "parameter_declaration", "m_abstract_declarator",
  "direct_m_abstract_declarator", "function_definition", "initializer_opt",
  "initializer", "class_body", "member_specification_opt",
  "member_specification", "member_declaration",
  "member_declarator_list_opt", "member_declarator_list",
  "member_declarator", "member_bitfield_declarator", "attribute_opt",
  "asm_definition", "asm_phrase_opt", "export_definition", 0
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
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,   298,    59,   125,    44,    40,    42,    41,
      61,   123,    58
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,    53,    54,    54,    56,    55,    58,    57,    59,    57,
      57,    57,    57,    57,    57,    57,    60,    61,    61,    62,
      62,    63,    64,    64,    65,    65,    66,    66,    67,    67,
      67,    67,    67,    68,    68,    68,    68,    68,    68,    68,
      68,    68,    68,    68,    68,    68,    69,    69,    69,    69,
      69,    69,    69,    69,    69,    69,    69,    70,    71,    71,
      72,    72,    73,    73,    73,    73,    74,    74,    75,    75,
      75,    75,    75,    75,    76,    76,    77,    77,    77,    77,
      77,    77,    77,    78,    78,    78,    79,    79,    80,    80,
      81,    82,    82,    83,    83,    83,    83,    83,    83,    83,
      83,    84,    85,    85,    86,    87,    87,    88,    88,    89,
      89,    90,    90,    91,    91,    92,    92,    93,    93,    93,
      94,    95,    95,    96,    97,    97,    98
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     1,     2,     0,     2,     0,     4,     0,     3,
       1,     1,     1,     1,     2,     2,     3,     0,     1,     1,
       3,     4,     0,     1,     1,     2,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     5,     4,     2,     2,     2,
       3,     3,     3,     2,     2,     2,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     2,     0,     1,
       1,     2,     1,     1,     1,     1,     2,     1,     1,     4,
       4,     2,     3,     3,     2,     1,     1,     1,     4,     4,
       2,     3,     3,     2,     1,     3,     0,     1,     1,     3,
       2,     2,     1,     0,     1,     1,     4,     4,     2,     3,
       3,     3,     0,     1,     2,     3,     3,     0,     1,     1,
       2,     3,     2,     0,     1,     1,     3,     2,     2,     1,
       2,     0,     2,     2,     0,     1,     5
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       4,     4,     2,     0,     1,     3,     0,    28,    55,    46,
      62,    53,     0,    31,     0,    52,    32,    48,    49,    29,
      65,    47,    50,    30,     0,     8,     0,    51,    54,    63,
       0,     0,     0,    64,    56,     5,    10,    17,    23,    24,
      26,    27,    33,    34,    11,    12,    13,    14,    15,    43,
      39,     6,    37,     0,    44,    22,    38,    45,     0,     0,
     123,    68,     0,    58,     0,    18,    19,     0,   124,    67,
      25,    42,    22,    40,     0,   113,     0,     0,   109,     9,
      17,    41,     0,     0,     0,     0,    57,    59,    60,    16,
       0,    66,   125,   101,   121,    71,     0,     7,   112,   106,
      76,    77,     0,     0,     0,   121,    75,     0,   114,   115,
     119,   105,     0,   110,   124,     0,    36,     0,    73,    72,
      61,    20,   102,     0,    93,     0,    84,    87,    88,   118,
       0,    76,     0,   120,    74,   117,    80,     0,   111,     0,
      35,   126,   122,     0,    21,   103,    70,    94,    56,     0,
      93,    90,    92,    69,    83,     0,    82,    81,     0,     0,
     116,   104,     0,    95,     0,    91,    98,     0,    85,    89,
      79,    78,   100,    99,     0,     0,    97,    96
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,     1,     2,     3,    35,    72,    55,    36,    64,    65,
      66,    75,    38,    39,    40,    41,    42,    67,    86,    87,
      43,   114,    69,   105,   106,   125,   126,   127,   128,   151,
     152,    44,   144,   145,    54,    76,    77,    78,   107,   108,
     109,   110,   122,    45,    94,    46
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -134
static const yytype_int16 yypact[] =
{
    -134,    16,  -134,   312,  -134,  -134,    20,  -134,  -134,  -134,
    -134,  -134,   -18,  -134,    -3,  -134,  -134,  -134,  -134,  -134,
    -134,  -134,  -134,  -134,   -26,  -134,   -25,  -134,  -134,  -134,
      -7,     5,    27,  -134,  -134,  -134,  -134,    46,   482,  -134,
    -134,  -134,  -134,  -134,  -134,  -134,  -134,  -134,  -134,  -134,
      -8,  -134,    30,    97,  -134,   482,    30,  -134,   482,     7,
    -134,  -134,    12,    10,    42,    55,  -134,    46,   -15,    15,
    -134,  -134,   482,  -134,    25,    26,    47,   145,  -134,  -134,
      46,  -134,   356,    39,    71,    77,  -134,    10,  -134,  -134,
      46,  -134,  -134,  -134,  -134,  -134,   193,  -134,  -134,  -134,
      75,  -134,     6,    95,    43,  -134,    28,    86,    85,  -134,
    -134,  -134,    88,  -134,   103,    87,  -134,    91,  -134,  -134,
    -134,  -134,   -23,    90,   401,    94,   101,   102,  -134,  -134,
      98,  -134,   108,  -134,  -134,   109,  -134,   230,  -134,    26,
    -134,  -134,  -134,   134,  -134,  -134,  -134,  -134,  -134,     9,
      48,  -134,    35,  -134,  -134,   445,  -134,  -134,   125,   126,
    -134,  -134,   128,  -134,   129,  -134,  -134,   267,  -134,  -134,
    -134,  -134,  -134,  -134,   130,   131,  -134,  -134
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -134,  -134,   180,  -134,  -134,  -134,  -134,   -33,  -134,  -134,
      93,     0,   -58,   -37,  -134,  -134,  -134,   -73,  -134,  -134,
     -54,   -32,  -134,   -81,  -134,  -133,  -134,  -134,    29,   -50,
    -134,  -134,  -134,  -134,   -20,  -134,  -134,   110,  -134,  -134,
      49,    96,    80,  -134,  -134,  -134
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -109
static const yytype_int16 yytable[] =
{
      82,    70,   104,    37,   159,    68,    57,   130,   142,    88,
     162,    52,    56,    84,    49,    92,     4,    93,    10,    50,
      51,   132,    79,   134,    71,    53,    53,   143,    20,   104,
      85,   104,    73,   120,   175,    91,    81,    29,   124,    97,
      58,    33,   -93,   131,    83,    70,   147,   101,    95,    61,
     163,   150,    59,   102,    63,    80,   149,    63,   -93,    62,
      63,   136,    96,   100,    47,    48,   104,   101,   166,    98,
      99,    60,    80,   102,    63,   137,   150,   150,   103,   124,
     131,    53,   167,    61,   101,   147,    89,    70,   117,   163,
     102,    63,   111,    62,    63,   149,    63,   124,    74,   164,
     165,    90,     7,     8,     9,    10,    11,    12,    13,   124,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
     118,    26,    27,    28,    29,    30,   119,   103,    33,   133,
     138,   139,    98,    92,   -22,   141,   140,   154,    34,   146,
     142,   -22,  -107,   153,   -22,   -22,   112,   156,   155,   -22,
       7,     8,     9,    10,    11,    12,    13,   157,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,   161,    26,
      27,    28,    29,    30,   170,   171,    33,   172,   173,   176,
     177,     5,   -22,   121,   169,   135,    34,   113,   160,   -22,
    -108,     0,   -22,   -22,   123,     0,   129,   -22,     7,     8,
       9,    10,    11,    12,    13,     0,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,     0,    26,    27,    28,
      29,    30,     0,     0,    33,     0,     0,     0,     0,   -86,
       0,   158,     0,     0,    34,     7,     8,     9,    10,    11,
      12,    13,   -86,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,     0,    26,    27,    28,    29,    30,     0,
       0,    33,     0,     0,     0,     0,   -86,     0,   174,     0,
       0,    34,     7,     8,     9,    10,    11,    12,    13,   -86,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
       0,    26,    27,    28,    29,    30,     0,     0,    33,     0,
       0,     0,     0,   -86,     0,     0,     0,     0,    34,     0,
       0,     0,     0,     6,     0,     0,   -86,     7,     8,     9,
      10,    11,    12,    13,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,    28,    29,
      30,    31,    32,    33,     0,     0,     0,     0,     0,   -22,
       0,     0,     0,    34,     0,     0,   -22,     0,     0,   -22,
     -22,     7,     8,     9,    10,    11,    12,    13,     0,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,     0,
      26,    27,    28,    29,    30,     0,     0,    33,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    34,     0,     0,
       0,     0,     0,     0,   115,   116,     7,     8,     9,    10,
      11,    12,    13,     0,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,     0,    26,    27,    28,    29,    30,
       0,     0,    33,     0,     0,     0,     0,     0,   147,     0,
       0,     0,   148,     0,     0,     0,     0,     0,   149,    63,
       7,     8,     9,    10,    11,    12,    13,     0,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,     0,    26,
      27,    28,    29,    30,     0,     0,    33,     0,     0,     0,
       0,   168,     0,     0,     0,     0,    34,     7,     8,     9,
      10,    11,    12,    13,     0,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,     0,    26,    27,    28,    29,
      30,     0,     0,    33,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    34
};

static const yytype_int16 yycheck[] =
{
      58,    38,    75,     3,   137,    37,    26,     1,    31,    63,
       1,    37,    37,     1,    32,    30,     0,    32,     8,    37,
      23,   102,    55,   104,    32,    51,    51,    50,    18,   102,
      62,   104,    52,    87,   167,    67,    56,    27,    96,    72,
      47,    31,    33,    37,    37,    82,    37,    41,    33,    37,
      41,   124,    47,    47,    48,    55,    47,    48,    49,    47,
      48,    33,    47,    37,    44,    45,   139,    41,    33,    44,
      45,    44,    72,    47,    48,    47,   149,   150,    52,   137,
      37,    51,    47,    37,    41,    37,    44,   124,    49,    41,
      47,    48,    45,    47,    48,    47,    48,   155,     1,   149,
     150,    46,     5,     6,     7,     8,     9,    10,    11,   167,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      49,    24,    25,    26,    27,    28,    49,    52,    31,    34,
      44,    46,    44,    30,    37,    44,    49,    36,    41,    49,
      31,    44,    45,    49,    47,    48,     1,    49,    46,    52,
       5,     6,     7,     8,     9,    10,    11,    49,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    34,    24,
      25,    26,    27,    28,    49,    49,    31,    49,    49,    49,
      49,     1,    37,    90,   155,   105,    41,    77,   139,    44,
      45,    -1,    47,    48,     1,    -1,   100,    52,     5,     6,
       7,     8,     9,    10,    11,    -1,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    -1,    24,    25,    26,
      27,    28,    -1,    -1,    31,    -1,    -1,    -1,    -1,    36,
      -1,     1,    -1,    -1,    41,     5,     6,     7,     8,     9,
      10,    11,    49,    13,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    -1,    24,    25,    26,    27,    28,    -1,
      -1,    31,    -1,    -1,    -1,    -1,    36,    -1,     1,    -1,
      -1,    41,     5,     6,     7,     8,     9,    10,    11,    49,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      -1,    24,    25,    26,    27,    28,    -1,    -1,    31,    -1,
      -1,    -1,    -1,    36,    -1,    -1,    -1,    -1,    41,    -1,
      -1,    -1,    -1,     1,    -1,    -1,    49,     5,     6,     7,
       8,     9,    10,    11,    12,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    -1,    -1,    -1,    -1,    -1,    37,
      -1,    -1,    -1,    41,    -1,    -1,    44,    -1,    -1,    47,
      48,     5,     6,     7,     8,     9,    10,    11,    -1,    13,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    -1,
      24,    25,    26,    27,    28,    -1,    -1,    31,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    41,    -1,    -1,
      -1,    -1,    -1,    -1,    48,    49,     5,     6,     7,     8,
       9,    10,    11,    -1,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    -1,    24,    25,    26,    27,    28,
      -1,    -1,    31,    -1,    -1,    -1,    -1,    -1,    37,    -1,
      -1,    -1,    41,    -1,    -1,    -1,    -1,    -1,    47,    48,
       5,     6,     7,     8,     9,    10,    11,    -1,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    -1,    24,
      25,    26,    27,    28,    -1,    -1,    31,    -1,    -1,    -1,
      -1,    36,    -1,    -1,    -1,    -1,    41,     5,     6,     7,
       8,     9,    10,    11,    -1,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    -1,    24,    25,    26,    27,
      28,    -1,    -1,    31,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    41
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    54,    55,    56,     0,    55,     1,     5,     6,     7,
       8,     9,    10,    11,    12,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    41,    57,    60,    64,    65,    66,
      67,    68,    69,    73,    84,    96,    98,    44,    45,    32,
      37,    23,    37,    51,    87,    59,    37,    87,    47,    47,
      44,    37,    47,    48,    61,    62,    63,    70,    74,    75,
      66,    32,    58,    87,     1,    64,    88,    89,    90,    60,
      64,    87,    65,    37,     1,    74,    71,    72,    73,    44,
      46,    74,    30,    32,    97,    33,    47,    60,    44,    45,
      37,    41,    47,    52,    70,    76,    77,    91,    92,    93,
      94,    45,     1,    90,    74,    48,    49,    49,    49,    49,
      73,    63,    95,     1,    65,    78,    79,    80,    81,    94,
       1,    37,    76,    34,    76,    95,    33,    47,    44,    46,
      49,    44,    31,    50,    85,    86,    49,    37,    41,    47,
      70,    82,    83,    49,    36,    46,    49,    49,     1,    78,
      93,    34,     1,    41,    82,    82,    33,    47,    36,    81,
      49,    49,    49,    49,     1,    78,    49,    49
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
yy_stack_print (yytype_int16 *bottom, yytype_int16 *top)
#else
static void
yy_stack_print (bottom, top)
    yytype_int16 *bottom;
    yytype_int16 *top;
#endif
{
  YYFPRINTF (stderr, "Stack now");
  for (; bottom <= top; ++bottom)
    YYFPRINTF (stderr, " %d", *bottom);
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
      fprintf (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr, yyrhs[yyprhs[yyrule] + yyi],
		       &(yyvsp[(yyi + 1) - (yynrhs)])
		       		       );
      fprintf (stderr, "\n");
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
  int yyn;
  int yyresult;
  /* Number of tokens to shift before error messages enabled.  */
  int yyerrstatus;
  /* Look-ahead token as an internal (translated) token number.  */
  int yytoken = 0;
#if YYERROR_VERBOSE
  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYSIZE_T yymsg_alloc = sizeof yymsgbuf;
#endif

  /* Three stacks and their tools:
     `yyss': related to states,
     `yyvs': related to semantic values,
     `yyls': related to locations.

     Refer to the stacks thru separate pointers, to allow yyoverflow
     to reallocate them elsewhere.  */

  /* The state stack.  */
  yytype_int16 yyssa[YYINITDEPTH];
  yytype_int16 *yyss = yyssa;
  yytype_int16 *yyssp;

  /* The semantic value stack.  */
  YYSTYPE yyvsa[YYINITDEPTH];
  YYSTYPE *yyvs = yyvsa;
  YYSTYPE *yyvsp;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  YYSIZE_T yystacksize = YYINITDEPTH;

  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;


  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

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

  /* Do appropriate processing given the current state.  Read a
     look-ahead token if we need one and don't already have one.  */

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

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the look-ahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);

  /* Discard the shifted token unless it is eof.  */
  if (yychar != YYEOF)
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
        case 4:
#line 108 "scripts/genksyms/parse.y"
    { is_typedef = 0; is_extern = 0; current_name = NULL; decl_spec = NULL; ;}
    break;

  case 5:
#line 110 "scripts/genksyms/parse.y"
    { free_list(*(yyvsp[(2) - (2)]), NULL); *(yyvsp[(2) - (2)]) = NULL; ;}
    break;

  case 6:
#line 114 "scripts/genksyms/parse.y"
    { is_typedef = 1; ;}
    break;

  case 7:
#line 115 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(4) - (4)]); ;}
    break;

  case 8:
#line 116 "scripts/genksyms/parse.y"
    { is_typedef = 1; ;}
    break;

  case 9:
#line 117 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 14:
#line 122 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 15:
#line 123 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 16:
#line 128 "scripts/genksyms/parse.y"
    { if (current_name) {
		    struct string_list *decl = (*(yyvsp[(3) - (3)]))->next;
		    (*(yyvsp[(3) - (3)]))->next = NULL;
		    add_symbol(current_name,
			       is_typedef ? SYM_TYPEDEF : SYM_NORMAL,
			       decl, is_extern);
		    current_name = NULL;
		  }
		  (yyval) = (yyvsp[(3) - (3)]);
		;}
    break;

  case 17:
#line 141 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 19:
#line 147 "scripts/genksyms/parse.y"
    { struct string_list *decl = *(yyvsp[(1) - (1)]);
		  *(yyvsp[(1) - (1)]) = NULL;
		  add_symbol(current_name,
			     is_typedef ? SYM_TYPEDEF : SYM_NORMAL, decl, is_extern);
		  current_name = NULL;
		  (yyval) = (yyvsp[(1) - (1)]);
		;}
    break;

  case 20:
#line 155 "scripts/genksyms/parse.y"
    { struct string_list *decl = *(yyvsp[(3) - (3)]);
		  *(yyvsp[(3) - (3)]) = NULL;
		  free_list(*(yyvsp[(2) - (3)]), NULL);
		  *(yyvsp[(2) - (3)]) = decl_spec;
		  add_symbol(current_name,
			     is_typedef ? SYM_TYPEDEF : SYM_NORMAL, decl, is_extern);
		  current_name = NULL;
		  (yyval) = (yyvsp[(3) - (3)]);
		;}
    break;

  case 21:
#line 168 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(4) - (4)]) ? (yyvsp[(4) - (4)]) : (yyvsp[(3) - (4)]) ? (yyvsp[(3) - (4)]) : (yyvsp[(2) - (4)]) ? (yyvsp[(2) - (4)]) : (yyvsp[(1) - (4)]); ;}
    break;

  case 22:
#line 173 "scripts/genksyms/parse.y"
    { decl_spec = NULL; ;}
    break;

  case 24:
#line 178 "scripts/genksyms/parse.y"
    { decl_spec = *(yyvsp[(1) - (1)]); ;}
    break;

  case 25:
#line 179 "scripts/genksyms/parse.y"
    { decl_spec = *(yyvsp[(2) - (2)]); ;}
    break;

  case 26:
#line 184 "scripts/genksyms/parse.y"
    { /* Version 2 checksumming ignores storage class, as that
		     is really irrelevant to the linkage.  */
		  remove_node((yyvsp[(1) - (1)]));
		  (yyval) = (yyvsp[(1) - (1)]);
		;}
    break;

  case 31:
#line 196 "scripts/genksyms/parse.y"
    { is_extern = 1; (yyval) = (yyvsp[(1) - (1)]); ;}
    break;

  case 32:
#line 197 "scripts/genksyms/parse.y"
    { is_extern = 0; (yyval) = (yyvsp[(1) - (1)]); ;}
    break;

  case 37:
#line 209 "scripts/genksyms/parse.y"
    { remove_node((yyvsp[(1) - (2)])); (*(yyvsp[(2) - (2)]))->tag = SYM_STRUCT; (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 38:
#line 211 "scripts/genksyms/parse.y"
    { remove_node((yyvsp[(1) - (2)])); (*(yyvsp[(2) - (2)]))->tag = SYM_UNION; (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 39:
#line 213 "scripts/genksyms/parse.y"
    { remove_node((yyvsp[(1) - (2)])); (*(yyvsp[(2) - (2)]))->tag = SYM_ENUM; (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 40:
#line 217 "scripts/genksyms/parse.y"
    { struct string_list *s = *(yyvsp[(3) - (3)]), *i = *(yyvsp[(2) - (3)]), *r;
		  r = copy_node(i); r->tag = SYM_STRUCT;
		  r->next = (*(yyvsp[(1) - (3)]))->next; *(yyvsp[(3) - (3)]) = r; (*(yyvsp[(1) - (3)]))->next = NULL;
		  add_symbol(i->string, SYM_STRUCT, s, is_extern);
		  (yyval) = (yyvsp[(3) - (3)]);
		;}
    break;

  case 41:
#line 224 "scripts/genksyms/parse.y"
    { struct string_list *s = *(yyvsp[(3) - (3)]), *i = *(yyvsp[(2) - (3)]), *r;
		  r = copy_node(i); r->tag = SYM_UNION;
		  r->next = (*(yyvsp[(1) - (3)]))->next; *(yyvsp[(3) - (3)]) = r; (*(yyvsp[(1) - (3)]))->next = NULL;
		  add_symbol(i->string, SYM_UNION, s, is_extern);
		  (yyval) = (yyvsp[(3) - (3)]);
		;}
    break;

  case 42:
#line 231 "scripts/genksyms/parse.y"
    { struct string_list *s = *(yyvsp[(3) - (3)]), *i = *(yyvsp[(2) - (3)]), *r;
		  r = copy_node(i); r->tag = SYM_ENUM;
		  r->next = (*(yyvsp[(1) - (3)]))->next; *(yyvsp[(3) - (3)]) = r; (*(yyvsp[(1) - (3)]))->next = NULL;
		  add_symbol(i->string, SYM_ENUM, s, is_extern);
		  (yyval) = (yyvsp[(3) - (3)]);
		;}
    break;

  case 43:
#line 239 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 44:
#line 240 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 45:
#line 241 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 56:
#line 255 "scripts/genksyms/parse.y"
    { (*(yyvsp[(1) - (1)]))->tag = SYM_TYPEDEF; (yyval) = (yyvsp[(1) - (1)]); ;}
    break;

  case 57:
#line 260 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]) ? (yyvsp[(2) - (2)]) : (yyvsp[(1) - (2)]); ;}
    break;

  case 58:
#line 264 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 61:
#line 270 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 65:
#line 276 "scripts/genksyms/parse.y"
    { /* restrict has no effect in prototypes so ignore it */
		  remove_node((yyvsp[(1) - (1)]));
		  (yyval) = (yyvsp[(1) - (1)]);
		;}
    break;

  case 66:
#line 283 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 68:
#line 289 "scripts/genksyms/parse.y"
    { if (current_name != NULL) {
		    error_with_pos("unexpected second declaration name");
		    YYERROR;
		  } else {
		    current_name = (*(yyvsp[(1) - (1)]))->string;
		    (yyval) = (yyvsp[(1) - (1)]);
		  }
		;}
    break;

  case 69:
#line 298 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(4) - (4)]); ;}
    break;

  case 70:
#line 300 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(4) - (4)]); ;}
    break;

  case 71:
#line 302 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 72:
#line 304 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 73:
#line 306 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 74:
#line 312 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 78:
#line 320 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(4) - (4)]); ;}
    break;

  case 79:
#line 322 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(4) - (4)]); ;}
    break;

  case 80:
#line 324 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 81:
#line 326 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 82:
#line 328 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 83:
#line 332 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 85:
#line 334 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 86:
#line 338 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 89:
#line 345 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 90:
#line 350 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]) ? (yyvsp[(2) - (2)]) : (yyvsp[(1) - (2)]); ;}
    break;

  case 91:
#line 355 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]) ? (yyvsp[(2) - (2)]) : (yyvsp[(1) - (2)]); ;}
    break;

  case 93:
#line 360 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 94:
#line 362 "scripts/genksyms/parse.y"
    { /* For version 2 checksums, we don't want to remember
		     private parameter names.  */
		  remove_node((yyvsp[(1) - (1)]));
		  (yyval) = (yyvsp[(1) - (1)]);
		;}
    break;

  case 95:
#line 370 "scripts/genksyms/parse.y"
    { remove_node((yyvsp[(1) - (1)]));
		  (yyval) = (yyvsp[(1) - (1)]);
		;}
    break;

  case 96:
#line 374 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(4) - (4)]); ;}
    break;

  case 97:
#line 376 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(4) - (4)]); ;}
    break;

  case 98:
#line 378 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 99:
#line 380 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 100:
#line 382 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 101:
#line 387 "scripts/genksyms/parse.y"
    { struct string_list *decl = *(yyvsp[(2) - (3)]);
		  *(yyvsp[(2) - (3)]) = NULL;
		  add_symbol(current_name, SYM_NORMAL, decl, is_extern);
		  (yyval) = (yyvsp[(3) - (3)]);
		;}
    break;

  case 102:
#line 395 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 104:
#line 402 "scripts/genksyms/parse.y"
    { remove_list((yyvsp[(2) - (2)]), &(*(yyvsp[(1) - (2)]))->next); (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 105:
#line 406 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 106:
#line 407 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 107:
#line 411 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 110:
#line 417 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 111:
#line 422 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 112:
#line 424 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 113:
#line 428 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 116:
#line 434 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 117:
#line 438 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]) ? (yyvsp[(2) - (2)]) : (yyvsp[(1) - (2)]); ;}
    break;

  case 118:
#line 439 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 120:
#line 444 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 121:
#line 448 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 123:
#line 453 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 124:
#line 457 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 126:
#line 463 "scripts/genksyms/parse.y"
    { export_symbol((*(yyvsp[(3) - (5)]))->string); (yyval) = (yyvsp[(5) - (5)]); ;}
    break;


/* Line 1267 of yacc.c.  */
#line 2132 "scripts/genksyms/parse.c"
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
	  yydestruct ("Error: discarding",
		      yytoken, &yylval);
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

  if (yyn == YYFINAL)
    YYACCEPT;

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


#line 467 "scripts/genksyms/parse.y"


static void
yyerror(const char *e)
{
  error_with_pos("%s", e);
}

