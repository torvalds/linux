
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
#line 24 "scripts/genksyms/parse.y"


#include <assert.h>
#include <stdlib.h>
#include <string.h>
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



/* Line 189 of yacc.c  */
#line 106 "scripts/genksyms/parse.c"

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



#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef int YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
#endif


/* Copy the second part of user declarations.  */


/* Line 264 of yacc.c  */
#line 191 "scripts/genksyms/parse.c"

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
#define YYLAST   532

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  53
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  49
/* YYNRULES -- Number of rules.  */
#define YYNRULES  132
/* YYNRULES -- Number of states.  */
#define YYNSTATES  188

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
     323,   326,   327,   330,   334,   339,   341,   345,   347,   351,
     354,   355,   357
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int8 yyrhs[] =
{
      54,     0,    -1,    55,    -1,    54,    55,    -1,    -1,    56,
      57,    -1,    -1,    12,    23,    58,    60,    -1,    -1,    23,
      59,    60,    -1,    60,    -1,    84,    -1,    99,    -1,   101,
      -1,     1,    44,    -1,     1,    45,    -1,    64,    61,    44,
      -1,    -1,    62,    -1,    63,    -1,    62,    46,    63,    -1,
      74,   100,    95,    85,    -1,    -1,    65,    -1,    66,    -1,
      65,    66,    -1,    67,    -1,    68,    -1,     5,    -1,    17,
      -1,    21,    -1,    11,    -1,    14,    -1,    69,    -1,    73,
      -1,    28,    47,    65,    48,    49,    -1,    28,    47,    65,
      49,    -1,    22,    37,    -1,    24,    37,    -1,    10,    37,
      -1,    22,    37,    87,    -1,    24,    37,    87,    -1,    10,
      37,    96,    -1,    10,    96,    -1,    22,    87,    -1,    24,
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
      51,    97,    45,    -1,    51,    97,    46,    45,    -1,    98,
      -1,    97,    46,    98,    -1,    37,    -1,    37,    50,    34,
      -1,    30,    44,    -1,    -1,    30,    -1,    29,    47,    37,
      49,    44,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   104,   104,   105,   109,   109,   115,   115,   117,   117,
     119,   120,   121,   122,   123,   124,   128,   142,   143,   147,
     155,   168,   174,   175,   179,   180,   184,   190,   194,   195,
     196,   197,   198,   202,   203,   204,   205,   209,   211,   213,
     217,   224,   231,   241,   244,   245,   249,   250,   251,   252,
     253,   254,   255,   256,   257,   258,   259,   263,   268,   269,
     273,   274,   278,   278,   278,   279,   287,   288,   292,   301,
     303,   305,   307,   309,   316,   317,   321,   322,   323,   325,
     327,   329,   331,   336,   337,   338,   342,   343,   347,   348,
     353,   358,   360,   364,   365,   373,   377,   379,   381,   383,
     385,   390,   399,   400,   405,   410,   411,   415,   416,   420,
     421,   425,   427,   432,   433,   437,   438,   442,   443,   444,
     448,   452,   453,   457,   458,   462,   463,   466,   471,   479,
     483,   484,   488
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
  "'='", "'{'", "':'", "$accept", "declaration_seq", "declaration", "$@1",
  "declaration1", "$@2", "$@3", "simple_declaration",
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
  "enum_body", "enumerator_list", "enumerator", "asm_definition",
  "asm_phrase_opt", "export_definition", 0
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
      94,    95,    95,    96,    96,    97,    97,    98,    98,    99,
     100,   100,   101
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
       2,     0,     2,     3,     4,     1,     3,     1,     3,     2,
       0,     1,     5
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
      26,    27,    33,    34,    11,    12,    13,    14,    15,    39,
       0,    43,     6,    37,     0,    44,    22,    38,    45,     0,
       0,   129,    68,     0,    58,     0,    18,    19,     0,   130,
      67,    25,    42,   127,     0,   125,    22,    40,     0,   113,
       0,     0,   109,     9,    17,    41,     0,     0,     0,     0,
      57,    59,    60,    16,     0,    66,   131,   101,   121,    71,
       0,     0,   123,     0,     7,   112,   106,    76,    77,     0,
       0,     0,   121,    75,     0,   114,   115,   119,   105,     0,
     110,   130,     0,    36,     0,    73,    72,    61,    20,   102,
       0,    93,     0,    84,    87,    88,   128,   124,   126,   118,
       0,    76,     0,   120,    74,   117,    80,     0,   111,     0,
      35,   132,   122,     0,    21,   103,    70,    94,    56,     0,
      93,    90,    92,    69,    83,     0,    82,    81,     0,     0,
     116,   104,     0,    95,     0,    91,    98,     0,    85,    89,
      79,    78,   100,    99,     0,     0,    97,    96
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,     1,     2,     3,    35,    76,    56,    36,    65,    66,
      67,    79,    38,    39,    40,    41,    42,    68,    90,    91,
      43,   121,    70,   112,   113,   132,   133,   134,   135,   161,
     162,    44,   154,   155,    55,    80,    81,    82,   114,   115,
     116,   117,   129,    51,    74,    75,    45,    98,    46
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -135
static const yytype_int16 yypact[] =
{
    -135,    20,  -135,   321,  -135,  -135,    30,  -135,  -135,  -135,
    -135,  -135,   -28,  -135,     2,  -135,  -135,  -135,  -135,  -135,
    -135,  -135,  -135,  -135,    -6,  -135,     9,  -135,  -135,  -135,
      -5,    15,   -17,  -135,  -135,  -135,  -135,    18,   491,  -135,
    -135,  -135,  -135,  -135,  -135,  -135,  -135,  -135,  -135,   -22,
      31,  -135,  -135,    19,   106,  -135,   491,    19,  -135,   491,
      50,  -135,  -135,    11,    -3,    51,    57,  -135,    18,   -14,
      14,  -135,  -135,    48,    46,  -135,   491,  -135,    33,    32,
      59,   154,  -135,  -135,    18,  -135,   365,    56,    60,    61,
    -135,    -3,  -135,  -135,    18,  -135,  -135,  -135,  -135,  -135,
     202,    74,  -135,   -23,  -135,  -135,  -135,    77,  -135,    16,
     101,    49,  -135,    34,    92,    93,  -135,  -135,  -135,    94,
    -135,   110,    95,  -135,    97,  -135,  -135,  -135,  -135,   -20,
      96,   410,    99,   113,   100,  -135,  -135,  -135,  -135,  -135,
     103,  -135,   107,  -135,  -135,   111,  -135,   239,  -135,    32,
    -135,  -135,  -135,   123,  -135,  -135,  -135,  -135,  -135,     3,
      52,  -135,    38,  -135,  -135,   454,  -135,  -135,   117,   128,
    -135,  -135,   134,  -135,   135,  -135,  -135,   276,  -135,  -135,
    -135,  -135,  -135,  -135,   137,   138,  -135,  -135
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -135,  -135,   187,  -135,  -135,  -135,  -135,   -50,  -135,  -135,
      98,     0,   -59,   -37,  -135,  -135,  -135,   -77,  -135,  -135,
     -54,   -30,  -135,   -90,  -135,  -134,  -135,  -135,    24,   -58,
    -135,  -135,  -135,  -135,   -18,  -135,  -135,   109,  -135,  -135,
      44,    87,    84,   148,  -135,   102,  -135,  -135,  -135
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -109
static const yytype_int16 yytable[] =
{
      86,    71,   111,    37,   172,    10,    83,    69,    58,    49,
      92,   152,    88,   169,    73,    20,    96,   140,    97,   142,
       4,   144,   137,    50,    29,    52,   104,    61,    33,    50,
     153,    53,   111,    89,   111,    77,   -93,   127,    95,    85,
     157,   131,    59,   185,   173,    54,    57,    99,    62,    71,
     159,    64,   -93,   141,   160,    62,    84,   108,    63,    64,
      54,   100,    60,   109,    64,    63,    64,   146,    73,   107,
      54,   176,   111,   108,    47,    48,    84,   105,   106,   109,
      64,   147,   160,   160,   110,   177,   141,    87,   131,   157,
     108,   102,   103,   173,    71,    93,   109,    64,   101,   159,
      64,   174,   175,    94,   118,   124,   131,    78,   136,   125,
     126,     7,     8,     9,    10,    11,    12,    13,   131,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,   110,
      26,    27,    28,    29,    30,   143,   148,    33,   105,   149,
      96,   151,   152,   -22,   150,   156,   165,    34,   163,   164,
     -22,  -107,   166,   -22,   -22,   119,   167,   171,   -22,     7,
       8,     9,    10,    11,    12,    13,   180,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,   181,    26,    27,
      28,    29,    30,   182,   183,    33,   186,   187,     5,   179,
     120,   -22,   128,   170,   139,    34,   145,    72,   -22,  -108,
       0,   -22,   -22,   130,     0,   138,   -22,     7,     8,     9,
      10,    11,    12,    13,     0,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,     0,    26,    27,    28,    29,
      30,     0,     0,    33,     0,     0,     0,     0,   -86,     0,
     168,     0,     0,    34,     7,     8,     9,    10,    11,    12,
      13,   -86,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,     0,    26,    27,    28,    29,    30,     0,     0,
      33,     0,     0,     0,     0,   -86,     0,   184,     0,     0,
      34,     7,     8,     9,    10,    11,    12,    13,   -86,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,     0,
      26,    27,    28,    29,    30,     0,     0,    33,     0,     0,
       0,     0,   -86,     0,     0,     0,     0,    34,     0,     0,
       0,     0,     6,     0,     0,   -86,     7,     8,     9,    10,
      11,    12,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    32,    33,     0,     0,     0,     0,     0,   -22,     0,
       0,     0,    34,     0,     0,   -22,     0,     0,   -22,   -22,
       7,     8,     9,    10,    11,    12,    13,     0,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,     0,    26,
      27,    28,    29,    30,     0,     0,    33,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    34,     0,     0,     0,
       0,     0,     0,   122,   123,     7,     8,     9,    10,    11,
      12,    13,     0,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,     0,    26,    27,    28,    29,    30,     0,
       0,    33,     0,     0,     0,     0,     0,   157,     0,     0,
       0,   158,     0,     0,     0,     0,     0,   159,    64,     7,
       8,     9,    10,    11,    12,    13,     0,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,     0,    26,    27,
      28,    29,    30,     0,     0,    33,     0,     0,     0,     0,
     178,     0,     0,     0,     0,    34,     7,     8,     9,    10,
      11,    12,    13,     0,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,     0,    26,    27,    28,    29,    30,
       0,     0,    33,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    34
};

static const yytype_int16 yycheck[] =
{
      59,    38,    79,     3,     1,     8,    56,    37,    26,    37,
      64,    31,     1,   147,    37,    18,    30,     1,    32,   109,
       0,   111,    45,    51,    27,    23,    76,    44,    31,    51,
      50,    37,   109,    63,   111,    53,    33,    91,    68,    57,
      37,   100,    47,   177,    41,    51,    37,    33,    37,    86,
      47,    48,    49,    37,   131,    37,    56,    41,    47,    48,
      51,    47,    47,    47,    48,    47,    48,    33,    37,    37,
      51,    33,   149,    41,    44,    45,    76,    44,    45,    47,
      48,    47,   159,   160,    52,    47,    37,    37,   147,    37,
      41,    45,    46,    41,   131,    44,    47,    48,    50,    47,
      48,   159,   160,    46,    45,    49,   165,     1,    34,    49,
      49,     5,     6,     7,     8,     9,    10,    11,   177,    13,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    52,
      24,    25,    26,    27,    28,    34,    44,    31,    44,    46,
      30,    44,    31,    37,    49,    49,    46,    41,    49,    36,
      44,    45,    49,    47,    48,     1,    49,    34,    52,     5,
       6,     7,     8,     9,    10,    11,    49,    13,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    49,    24,    25,
      26,    27,    28,    49,    49,    31,    49,    49,     1,   165,
      81,    37,    94,   149,   107,    41,   112,    49,    44,    45,
      -1,    47,    48,     1,    -1,   103,    52,     5,     6,     7,
       8,     9,    10,    11,    -1,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    -1,    24,    25,    26,    27,
      28,    -1,    -1,    31,    -1,    -1,    -1,    -1,    36,    -1,
       1,    -1,    -1,    41,     5,     6,     7,     8,     9,    10,
      11,    49,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    -1,    24,    25,    26,    27,    28,    -1,    -1,
      31,    -1,    -1,    -1,    -1,    36,    -1,     1,    -1,    -1,
      41,     5,     6,     7,     8,     9,    10,    11,    49,    13,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    -1,
      24,    25,    26,    27,    28,    -1,    -1,    31,    -1,    -1,
      -1,    -1,    36,    -1,    -1,    -1,    -1,    41,    -1,    -1,
      -1,    -1,     1,    -1,    -1,    49,     5,     6,     7,     8,
       9,    10,    11,    12,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    -1,    -1,    -1,    -1,    -1,    37,    -1,
      -1,    -1,    41,    -1,    -1,    44,    -1,    -1,    47,    48,
       5,     6,     7,     8,     9,    10,    11,    -1,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    -1,    24,
      25,    26,    27,    28,    -1,    -1,    31,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    41,    -1,    -1,    -1,
      -1,    -1,    -1,    48,    49,     5,     6,     7,     8,     9,
      10,    11,    -1,    13,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    -1,    24,    25,    26,    27,    28,    -1,
      -1,    31,    -1,    -1,    -1,    -1,    -1,    37,    -1,    -1,
      -1,    41,    -1,    -1,    -1,    -1,    -1,    47,    48,     5,
       6,     7,     8,     9,    10,    11,    -1,    13,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    -1,    24,    25,
      26,    27,    28,    -1,    -1,    31,    -1,    -1,    -1,    -1,
      36,    -1,    -1,    -1,    -1,    41,     5,     6,     7,     8,
       9,    10,    11,    -1,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    -1,    24,    25,    26,    27,    28,
      -1,    -1,    31,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    41
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    54,    55,    56,     0,    55,     1,     5,     6,     7,
       8,     9,    10,    11,    12,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    41,    57,    60,    64,    65,    66,
      67,    68,    69,    73,    84,    99,   101,    44,    45,    37,
      51,    96,    23,    37,    51,    87,    59,    37,    87,    47,
      47,    44,    37,    47,    48,    61,    62,    63,    70,    74,
      75,    66,    96,    37,    97,    98,    58,    87,     1,    64,
      88,    89,    90,    60,    64,    87,    65,    37,     1,    74,
      71,    72,    73,    44,    46,    74,    30,    32,   100,    33,
      47,    50,    45,    46,    60,    44,    45,    37,    41,    47,
      52,    70,    76,    77,    91,    92,    93,    94,    45,     1,
      90,    74,    48,    49,    49,    49,    49,    73,    63,    95,
       1,    65,    78,    79,    80,    81,    34,    45,    98,    94,
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
        case 4:

/* Line 1455 of yacc.c  */
#line 109 "scripts/genksyms/parse.y"
    { is_typedef = 0; is_extern = 0; current_name = NULL; decl_spec = NULL; ;}
    break;

  case 5:

/* Line 1455 of yacc.c  */
#line 111 "scripts/genksyms/parse.y"
    { free_list(*(yyvsp[(2) - (2)]), NULL); *(yyvsp[(2) - (2)]) = NULL; ;}
    break;

  case 6:

/* Line 1455 of yacc.c  */
#line 115 "scripts/genksyms/parse.y"
    { is_typedef = 1; ;}
    break;

  case 7:

/* Line 1455 of yacc.c  */
#line 116 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(4) - (4)]); ;}
    break;

  case 8:

/* Line 1455 of yacc.c  */
#line 117 "scripts/genksyms/parse.y"
    { is_typedef = 1; ;}
    break;

  case 9:

/* Line 1455 of yacc.c  */
#line 118 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 14:

/* Line 1455 of yacc.c  */
#line 123 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 15:

/* Line 1455 of yacc.c  */
#line 124 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 16:

/* Line 1455 of yacc.c  */
#line 129 "scripts/genksyms/parse.y"
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

/* Line 1455 of yacc.c  */
#line 142 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 19:

/* Line 1455 of yacc.c  */
#line 148 "scripts/genksyms/parse.y"
    { struct string_list *decl = *(yyvsp[(1) - (1)]);
		  *(yyvsp[(1) - (1)]) = NULL;
		  add_symbol(current_name,
			     is_typedef ? SYM_TYPEDEF : SYM_NORMAL, decl, is_extern);
		  current_name = NULL;
		  (yyval) = (yyvsp[(1) - (1)]);
		;}
    break;

  case 20:

/* Line 1455 of yacc.c  */
#line 156 "scripts/genksyms/parse.y"
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

/* Line 1455 of yacc.c  */
#line 169 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(4) - (4)]) ? (yyvsp[(4) - (4)]) : (yyvsp[(3) - (4)]) ? (yyvsp[(3) - (4)]) : (yyvsp[(2) - (4)]) ? (yyvsp[(2) - (4)]) : (yyvsp[(1) - (4)]); ;}
    break;

  case 22:

/* Line 1455 of yacc.c  */
#line 174 "scripts/genksyms/parse.y"
    { decl_spec = NULL; ;}
    break;

  case 24:

/* Line 1455 of yacc.c  */
#line 179 "scripts/genksyms/parse.y"
    { decl_spec = *(yyvsp[(1) - (1)]); ;}
    break;

  case 25:

/* Line 1455 of yacc.c  */
#line 180 "scripts/genksyms/parse.y"
    { decl_spec = *(yyvsp[(2) - (2)]); ;}
    break;

  case 26:

/* Line 1455 of yacc.c  */
#line 185 "scripts/genksyms/parse.y"
    { /* Version 2 checksumming ignores storage class, as that
		     is really irrelevant to the linkage.  */
		  remove_node((yyvsp[(1) - (1)]));
		  (yyval) = (yyvsp[(1) - (1)]);
		;}
    break;

  case 31:

/* Line 1455 of yacc.c  */
#line 197 "scripts/genksyms/parse.y"
    { is_extern = 1; (yyval) = (yyvsp[(1) - (1)]); ;}
    break;

  case 32:

/* Line 1455 of yacc.c  */
#line 198 "scripts/genksyms/parse.y"
    { is_extern = 0; (yyval) = (yyvsp[(1) - (1)]); ;}
    break;

  case 37:

/* Line 1455 of yacc.c  */
#line 210 "scripts/genksyms/parse.y"
    { remove_node((yyvsp[(1) - (2)])); (*(yyvsp[(2) - (2)]))->tag = SYM_STRUCT; (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 38:

/* Line 1455 of yacc.c  */
#line 212 "scripts/genksyms/parse.y"
    { remove_node((yyvsp[(1) - (2)])); (*(yyvsp[(2) - (2)]))->tag = SYM_UNION; (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 39:

/* Line 1455 of yacc.c  */
#line 214 "scripts/genksyms/parse.y"
    { remove_node((yyvsp[(1) - (2)])); (*(yyvsp[(2) - (2)]))->tag = SYM_ENUM; (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 40:

/* Line 1455 of yacc.c  */
#line 218 "scripts/genksyms/parse.y"
    { struct string_list *s = *(yyvsp[(3) - (3)]), *i = *(yyvsp[(2) - (3)]), *r;
		  r = copy_node(i); r->tag = SYM_STRUCT;
		  r->next = (*(yyvsp[(1) - (3)]))->next; *(yyvsp[(3) - (3)]) = r; (*(yyvsp[(1) - (3)]))->next = NULL;
		  add_symbol(i->string, SYM_STRUCT, s, is_extern);
		  (yyval) = (yyvsp[(3) - (3)]);
		;}
    break;

  case 41:

/* Line 1455 of yacc.c  */
#line 225 "scripts/genksyms/parse.y"
    { struct string_list *s = *(yyvsp[(3) - (3)]), *i = *(yyvsp[(2) - (3)]), *r;
		  r = copy_node(i); r->tag = SYM_UNION;
		  r->next = (*(yyvsp[(1) - (3)]))->next; *(yyvsp[(3) - (3)]) = r; (*(yyvsp[(1) - (3)]))->next = NULL;
		  add_symbol(i->string, SYM_UNION, s, is_extern);
		  (yyval) = (yyvsp[(3) - (3)]);
		;}
    break;

  case 42:

/* Line 1455 of yacc.c  */
#line 232 "scripts/genksyms/parse.y"
    { struct string_list *s = *(yyvsp[(3) - (3)]), *i = *(yyvsp[(2) - (3)]), *r;
		  r = copy_node(i); r->tag = SYM_ENUM;
		  r->next = (*(yyvsp[(1) - (3)]))->next; *(yyvsp[(3) - (3)]) = r; (*(yyvsp[(1) - (3)]))->next = NULL;
		  add_symbol(i->string, SYM_ENUM, s, is_extern);
		  (yyval) = (yyvsp[(3) - (3)]);
		;}
    break;

  case 43:

/* Line 1455 of yacc.c  */
#line 242 "scripts/genksyms/parse.y"
    { add_symbol(NULL, SYM_ENUM, NULL, 0); (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 44:

/* Line 1455 of yacc.c  */
#line 244 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 45:

/* Line 1455 of yacc.c  */
#line 245 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 56:

/* Line 1455 of yacc.c  */
#line 259 "scripts/genksyms/parse.y"
    { (*(yyvsp[(1) - (1)]))->tag = SYM_TYPEDEF; (yyval) = (yyvsp[(1) - (1)]); ;}
    break;

  case 57:

/* Line 1455 of yacc.c  */
#line 264 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]) ? (yyvsp[(2) - (2)]) : (yyvsp[(1) - (2)]); ;}
    break;

  case 58:

/* Line 1455 of yacc.c  */
#line 268 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 61:

/* Line 1455 of yacc.c  */
#line 274 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 65:

/* Line 1455 of yacc.c  */
#line 280 "scripts/genksyms/parse.y"
    { /* restrict has no effect in prototypes so ignore it */
		  remove_node((yyvsp[(1) - (1)]));
		  (yyval) = (yyvsp[(1) - (1)]);
		;}
    break;

  case 66:

/* Line 1455 of yacc.c  */
#line 287 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 68:

/* Line 1455 of yacc.c  */
#line 293 "scripts/genksyms/parse.y"
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

/* Line 1455 of yacc.c  */
#line 302 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(4) - (4)]); ;}
    break;

  case 70:

/* Line 1455 of yacc.c  */
#line 304 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(4) - (4)]); ;}
    break;

  case 71:

/* Line 1455 of yacc.c  */
#line 306 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 72:

/* Line 1455 of yacc.c  */
#line 308 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 73:

/* Line 1455 of yacc.c  */
#line 310 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 74:

/* Line 1455 of yacc.c  */
#line 316 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 78:

/* Line 1455 of yacc.c  */
#line 324 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(4) - (4)]); ;}
    break;

  case 79:

/* Line 1455 of yacc.c  */
#line 326 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(4) - (4)]); ;}
    break;

  case 80:

/* Line 1455 of yacc.c  */
#line 328 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 81:

/* Line 1455 of yacc.c  */
#line 330 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 82:

/* Line 1455 of yacc.c  */
#line 332 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 83:

/* Line 1455 of yacc.c  */
#line 336 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 85:

/* Line 1455 of yacc.c  */
#line 338 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 86:

/* Line 1455 of yacc.c  */
#line 342 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 89:

/* Line 1455 of yacc.c  */
#line 349 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 90:

/* Line 1455 of yacc.c  */
#line 354 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]) ? (yyvsp[(2) - (2)]) : (yyvsp[(1) - (2)]); ;}
    break;

  case 91:

/* Line 1455 of yacc.c  */
#line 359 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]) ? (yyvsp[(2) - (2)]) : (yyvsp[(1) - (2)]); ;}
    break;

  case 93:

/* Line 1455 of yacc.c  */
#line 364 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 94:

/* Line 1455 of yacc.c  */
#line 366 "scripts/genksyms/parse.y"
    { /* For version 2 checksums, we don't want to remember
		     private parameter names.  */
		  remove_node((yyvsp[(1) - (1)]));
		  (yyval) = (yyvsp[(1) - (1)]);
		;}
    break;

  case 95:

/* Line 1455 of yacc.c  */
#line 374 "scripts/genksyms/parse.y"
    { remove_node((yyvsp[(1) - (1)]));
		  (yyval) = (yyvsp[(1) - (1)]);
		;}
    break;

  case 96:

/* Line 1455 of yacc.c  */
#line 378 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(4) - (4)]); ;}
    break;

  case 97:

/* Line 1455 of yacc.c  */
#line 380 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(4) - (4)]); ;}
    break;

  case 98:

/* Line 1455 of yacc.c  */
#line 382 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 99:

/* Line 1455 of yacc.c  */
#line 384 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 100:

/* Line 1455 of yacc.c  */
#line 386 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 101:

/* Line 1455 of yacc.c  */
#line 391 "scripts/genksyms/parse.y"
    { struct string_list *decl = *(yyvsp[(2) - (3)]);
		  *(yyvsp[(2) - (3)]) = NULL;
		  add_symbol(current_name, SYM_NORMAL, decl, is_extern);
		  (yyval) = (yyvsp[(3) - (3)]);
		;}
    break;

  case 102:

/* Line 1455 of yacc.c  */
#line 399 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 104:

/* Line 1455 of yacc.c  */
#line 406 "scripts/genksyms/parse.y"
    { remove_list((yyvsp[(2) - (2)]), &(*(yyvsp[(1) - (2)]))->next); (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 105:

/* Line 1455 of yacc.c  */
#line 410 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 106:

/* Line 1455 of yacc.c  */
#line 411 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 107:

/* Line 1455 of yacc.c  */
#line 415 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 110:

/* Line 1455 of yacc.c  */
#line 421 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 111:

/* Line 1455 of yacc.c  */
#line 426 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 112:

/* Line 1455 of yacc.c  */
#line 428 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 113:

/* Line 1455 of yacc.c  */
#line 432 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 116:

/* Line 1455 of yacc.c  */
#line 438 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 117:

/* Line 1455 of yacc.c  */
#line 442 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]) ? (yyvsp[(2) - (2)]) : (yyvsp[(1) - (2)]); ;}
    break;

  case 118:

/* Line 1455 of yacc.c  */
#line 443 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 120:

/* Line 1455 of yacc.c  */
#line 448 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 121:

/* Line 1455 of yacc.c  */
#line 452 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 123:

/* Line 1455 of yacc.c  */
#line 457 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(3) - (3)]); ;}
    break;

  case 124:

/* Line 1455 of yacc.c  */
#line 458 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(4) - (4)]); ;}
    break;

  case 127:

/* Line 1455 of yacc.c  */
#line 467 "scripts/genksyms/parse.y"
    {
			const char *name = strdup((*(yyvsp[(1) - (1)]))->string);
			add_symbol(name, SYM_ENUM_CONST, NULL, 0);
		;}
    break;

  case 128:

/* Line 1455 of yacc.c  */
#line 472 "scripts/genksyms/parse.y"
    {
			const char *name = strdup((*(yyvsp[(1) - (3)]))->string);
			struct string_list *expr = copy_list_range(*(yyvsp[(3) - (3)]), *(yyvsp[(2) - (3)]));
			add_symbol(name, SYM_ENUM_CONST, expr, 0);
		;}
    break;

  case 129:

/* Line 1455 of yacc.c  */
#line 479 "scripts/genksyms/parse.y"
    { (yyval) = (yyvsp[(2) - (2)]); ;}
    break;

  case 130:

/* Line 1455 of yacc.c  */
#line 483 "scripts/genksyms/parse.y"
    { (yyval) = NULL; ;}
    break;

  case 132:

/* Line 1455 of yacc.c  */
#line 489 "scripts/genksyms/parse.y"
    { export_symbol((*(yyvsp[(3) - (5)]))->string); (yyval) = (yyvsp[(5) - (5)]); ;}
    break;



/* Line 1455 of yacc.c  */
#line 2301 "scripts/genksyms/parse.c"
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
#line 493 "scripts/genksyms/parse.y"


static void
yyerror(const char *e)
{
  error_with_pos("%s", e);
}

