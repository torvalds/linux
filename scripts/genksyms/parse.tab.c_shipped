/* A Bison parser, made by GNU Bison 2.7.  */

/* Bison implementation for Yacc-like parsers in C
   
      Copyright (C) 1984, 1989-1990, 2000-2012 Free Software Foundation, Inc.
   
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
#define YYBISON_VERSION "2.7"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* Copy the first part of user declarations.  */



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

/* Record definition of a struct/union/enum */
static void record_compound(struct string_list **keyw,
		       struct string_list **ident,
		       struct string_list **body,
		       enum symbol_type type)
{
	struct string_list *b = *body, *i = *ident, *r;

	if (i->in_source_file) {
		remove_node(keyw);
		(*ident)->tag = type;
		remove_list(body, ident);
		return;
	}
	r = copy_node(i); r->tag = type;
	r->next = (*keyw)->next; *body = r; (*keyw)->next = NULL;
	add_symbol(i->string, type, b, is_extern);
}




# ifndef YY_NULL
#  if defined __cplusplus && 201103L <= __cplusplus
#   define YY_NULL nullptr
#  else
#   define YY_NULL 0
#  endif
# endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 0
#endif


/* Enabling traces.  */
#ifndef YYDEBUG
# define YYDEBUG 1
#endif
#if YYDEBUG
extern int yydebug;
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
     VA_LIST_KEYW = 284,
     EXPORT_SYMBOL_KEYW = 285,
     ASM_PHRASE = 286,
     ATTRIBUTE_PHRASE = 287,
     TYPEOF_PHRASE = 288,
     BRACE_PHRASE = 289,
     BRACKET_PHRASE = 290,
     EXPRESSION_PHRASE = 291,
     CHAR = 292,
     DOTS = 293,
     IDENT = 294,
     INT = 295,
     REAL = 296,
     STRING = 297,
     TYPE = 298,
     OTHER = 299,
     FILENAME = 300
   };
#endif


#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef int YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
#endif

extern YYSTYPE yylval;

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



/* Copy the second part of user declarations.  */



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
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YYUSE(E) ((void) (E))
#else
# define YYUSE(E) /* empty */
#endif

/* Identity function, used to suppress warnings about constant conditions.  */
#ifndef lint
# define YYID(N) (N)
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
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
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
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
	     && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS && (defined __STDC__ || defined __C99__FUNC__ \
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

# define YYCOPY_NEEDED 1

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

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, (Count) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYSIZE_T yyi;                         \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (YYID (0))
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  4
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   522

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  55
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  49
/* YYNRULES -- Number of rules.  */
#define YYNRULES  133
/* YYNRULES -- Number of states.  */
#define YYNSTATES  187

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   300

#define YYTRANSLATE(YYX)						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const yytype_uint8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      49,    50,    51,     2,    48,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    54,    46,
       2,    52,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    53,     2,    47,     2,     2,     2,     2,
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
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const yytype_uint16 yyprhs[] =
{
       0,     0,     3,     5,     8,     9,    12,    13,    18,    19,
      23,    25,    27,    29,    31,    34,    37,    41,    42,    44,
      46,    50,    55,    56,    58,    60,    63,    65,    67,    69,
      71,    73,    75,    77,    79,    81,    86,    88,    91,    94,
      97,   101,   105,   109,   112,   115,   118,   120,   122,   124,
     126,   128,   130,   132,   134,   136,   138,   140,   142,   145,
     146,   148,   150,   153,   155,   157,   159,   161,   164,   166,
     168,   170,   175,   180,   183,   187,   190,   192,   194,   196,
     201,   206,   209,   213,   217,   220,   222,   226,   227,   229,
     231,   235,   238,   241,   243,   244,   246,   248,   253,   258,
     261,   265,   269,   273,   274,   276,   279,   283,   287,   288,
     290,   292,   295,   299,   302,   303,   305,   307,   311,   314,
     317,   319,   322,   323,   326,   330,   335,   337,   341,   343,
     347,   350,   351,   353
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int8 yyrhs[] =
{
      56,     0,    -1,    57,    -1,    56,    57,    -1,    -1,    58,
      59,    -1,    -1,    12,    23,    60,    62,    -1,    -1,    23,
      61,    62,    -1,    62,    -1,    86,    -1,   101,    -1,   103,
      -1,     1,    46,    -1,     1,    47,    -1,    66,    63,    46,
      -1,    -1,    64,    -1,    65,    -1,    64,    48,    65,    -1,
      76,   102,    97,    87,    -1,    -1,    67,    -1,    68,    -1,
      67,    68,    -1,    69,    -1,    70,    -1,     5,    -1,    17,
      -1,    21,    -1,    11,    -1,    14,    -1,    71,    -1,    75,
      -1,    28,    49,    83,    50,    -1,    33,    -1,    22,    39,
      -1,    24,    39,    -1,    10,    39,    -1,    22,    39,    89,
      -1,    24,    39,    89,    -1,    10,    39,    98,    -1,    10,
      98,    -1,    22,    89,    -1,    24,    89,    -1,     7,    -1,
      19,    -1,    15,    -1,    16,    -1,    20,    -1,    25,    -1,
      13,    -1,     9,    -1,    26,    -1,     6,    -1,    29,    -1,
      43,    -1,    51,    73,    -1,    -1,    74,    -1,    75,    -1,
      74,    75,    -1,     8,    -1,    27,    -1,    32,    -1,    18,
      -1,    72,    76,    -1,    77,    -1,    39,    -1,    43,    -1,
      77,    49,    80,    50,    -1,    77,    49,     1,    50,    -1,
      77,    35,    -1,    49,    76,    50,    -1,    72,    78,    -1,
      79,    -1,    39,    -1,    43,    -1,    79,    49,    80,    50,
      -1,    79,    49,     1,    50,    -1,    79,    35,    -1,    49,
      78,    50,    -1,    49,     1,    50,    -1,    81,    38,    -1,
      81,    -1,    82,    48,    38,    -1,    -1,    82,    -1,    83,
      -1,    82,    48,    83,    -1,    67,    84,    -1,    72,    84,
      -1,    85,    -1,    -1,    39,    -1,    43,    -1,    85,    49,
      80,    50,    -1,    85,    49,     1,    50,    -1,    85,    35,
      -1,    49,    84,    50,    -1,    49,     1,    50,    -1,    66,
      76,    34,    -1,    -1,    88,    -1,    52,    36,    -1,    53,
      90,    47,    -1,    53,     1,    47,    -1,    -1,    91,    -1,
      92,    -1,    91,    92,    -1,    66,    93,    46,    -1,     1,
      46,    -1,    -1,    94,    -1,    95,    -1,    94,    48,    95,
      -1,    78,    97,    -1,    39,    96,    -1,    96,    -1,    54,
      36,    -1,    -1,    97,    32,    -1,    53,    99,    47,    -1,
      53,    99,    48,    47,    -1,   100,    -1,    99,    48,   100,
      -1,    39,    -1,    39,    52,    36,    -1,    31,    46,    -1,
      -1,    31,    -1,    30,    49,    39,    50,    46,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   125,   125,   126,   130,   130,   136,   136,   138,   138,
     140,   141,   142,   143,   144,   145,   149,   163,   164,   168,
     176,   189,   195,   196,   200,   201,   205,   211,   215,   216,
     217,   218,   219,   223,   224,   225,   226,   230,   232,   234,
     238,   240,   242,   247,   250,   251,   255,   256,   257,   258,
     259,   260,   261,   262,   263,   264,   265,   266,   270,   275,
     276,   280,   281,   285,   285,   285,   286,   294,   295,   299,
     308,   317,   319,   321,   323,   330,   331,   335,   336,   337,
     339,   341,   343,   345,   350,   351,   352,   356,   357,   361,
     362,   367,   372,   374,   378,   379,   387,   391,   393,   395,
     397,   399,   404,   413,   414,   419,   424,   425,   429,   430,
     434,   435,   439,   441,   446,   447,   451,   452,   456,   457,
     458,   462,   466,   467,   471,   472,   476,   477,   480,   485,
     493,   497,   498,   502
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || 0
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
  "VOLATILE_KEYW", "TYPEOF_KEYW", "VA_LIST_KEYW", "EXPORT_SYMBOL_KEYW",
  "ASM_PHRASE", "ATTRIBUTE_PHRASE", "TYPEOF_PHRASE", "BRACE_PHRASE",
  "BRACKET_PHRASE", "EXPRESSION_PHRASE", "CHAR", "DOTS", "IDENT", "INT",
  "REAL", "STRING", "TYPE", "OTHER", "FILENAME", "';'", "'}'", "','",
  "'('", "')'", "'*'", "'='", "'{'", "':'", "$accept", "declaration_seq",
  "declaration", "$@1", "declaration1", "$@2", "$@3", "simple_declaration",
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
  "asm_phrase_opt", "export_definition", YY_NULL
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
     295,   296,   297,   298,   299,   300,    59,   125,    44,    40,
      41,    42,    61,   123,    58
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,    55,    56,    56,    58,    57,    60,    59,    61,    59,
      59,    59,    59,    59,    59,    59,    62,    63,    63,    64,
      64,    65,    66,    66,    67,    67,    68,    68,    69,    69,
      69,    69,    69,    70,    70,    70,    70,    70,    70,    70,
      70,    70,    70,    70,    70,    70,    71,    71,    71,    71,
      71,    71,    71,    71,    71,    71,    71,    71,    72,    73,
      73,    74,    74,    75,    75,    75,    75,    76,    76,    77,
      77,    77,    77,    77,    77,    78,    78,    79,    79,    79,
      79,    79,    79,    79,    80,    80,    80,    81,    81,    82,
      82,    83,    84,    84,    85,    85,    85,    85,    85,    85,
      85,    85,    86,    87,    87,    88,    89,    89,    90,    90,
      91,    91,    92,    92,    93,    93,    94,    94,    95,    95,
      95,    96,    97,    97,    98,    98,    99,    99,   100,   100,
     101,   102,   102,   103
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     1,     2,     0,     2,     0,     4,     0,     3,
       1,     1,     1,     1,     2,     2,     3,     0,     1,     1,
       3,     4,     0,     1,     1,     2,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     4,     1,     2,     2,     2,
       3,     3,     3,     2,     2,     2,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     2,     0,
       1,     1,     2,     1,     1,     1,     1,     2,     1,     1,
       1,     4,     4,     2,     3,     2,     1,     1,     1,     4,
       4,     2,     3,     3,     2,     1,     3,     0,     1,     1,
       3,     2,     2,     1,     0,     1,     1,     4,     4,     2,
       3,     3,     3,     0,     1,     2,     3,     3,     0,     1,
       1,     2,     3,     2,     0,     1,     1,     3,     2,     2,
       1,     2,     0,     2,     3,     4,     1,     3,     1,     3,
       2,     0,     1,     5
};

/* YYDEFACT[STATE-NAME] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       4,     4,     2,     0,     1,     3,     0,    28,    55,    46,
      63,    53,     0,    31,     0,    52,    32,    48,    49,    29,
      66,    47,    50,    30,     0,     8,     0,    51,    54,    64,
       0,    56,     0,     0,    65,    36,    57,     5,    10,    17,
      23,    24,    26,    27,    33,    34,    11,    12,    13,    14,
      15,    39,     0,    43,     6,    37,     0,    44,    22,    38,
      45,     0,     0,   130,    69,    70,     0,    59,     0,    18,
      19,     0,   131,    68,    25,    42,   128,     0,   126,    22,
      40,     0,   114,     0,     0,   110,     9,    17,    41,    94,
       0,     0,     0,    58,    60,    61,    16,     0,    67,   132,
     102,   122,    73,     0,     0,   124,     0,     7,   113,   107,
      77,    78,     0,     0,     0,   122,    76,     0,   115,   116,
     120,   106,     0,   111,   131,    95,    57,     0,    94,    91,
      93,    35,     0,    74,    62,    20,   103,     0,     0,    85,
      88,    89,   129,   125,   127,   119,     0,    77,     0,   121,
      75,   118,    81,     0,   112,     0,     0,    96,     0,    92,
      99,     0,   133,   123,     0,    21,   104,    72,    71,    84,
       0,    83,    82,     0,     0,   117,   101,   100,     0,     0,
     105,    86,    90,    80,    79,    98,    97
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,     1,     2,     3,    37,    79,    58,    38,    68,    69,
      70,    82,    40,    41,    42,    43,    44,    71,    93,    94,
      45,   124,    73,   115,   116,   138,   139,   140,   141,   129,
     130,    46,   165,   166,    57,    83,    84,    85,   117,   118,
     119,   120,   136,    53,    77,    78,    47,   101,    48
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -94
static const yytype_int16 yypact[] =
{
     -94,    15,   -94,   208,   -94,   -94,    34,   -94,   -94,   -94,
     -94,   -94,   -27,   -94,    -5,   -94,   -94,   -94,   -94,   -94,
     -94,   -94,   -94,   -94,   -25,   -94,   -16,   -94,   -94,   -94,
      -4,   -94,    19,   -24,   -94,   -94,   -94,   -94,   -94,    24,
     479,   -94,   -94,   -94,   -94,   -94,   -94,   -94,   -94,   -94,
     -94,    29,    48,   -94,   -94,    37,   106,   -94,   479,    37,
     -94,   479,    54,   -94,   -94,   -94,    24,    -2,    49,    53,
     -94,    24,   -14,   -11,   -94,   -94,    47,    38,   -94,   479,
     -94,    51,    23,    55,   157,   -94,   -94,    24,   -94,   393,
      56,    58,    68,   -94,    -2,   -94,   -94,    24,   -94,   -94,
     -94,   -94,   -94,   255,    67,   -94,     5,   -94,   -94,   -94,
      50,   -94,     7,    69,    40,   -94,    -8,    83,    88,   -94,
     -94,   -94,    91,   -94,   109,   -94,   -94,     4,    45,   -94,
      16,   -94,    95,   -94,   -94,   -94,   -23,    92,    93,   108,
      96,   -94,   -94,   -94,   -94,   -94,    97,   -94,    98,   -94,
     -94,   118,   -94,   301,   -94,    23,   101,   -94,   104,   -94,
     -94,   347,   -94,   -94,   120,   -94,   -94,   -94,   -94,   -94,
     440,   -94,   -94,   111,   119,   -94,   -94,   -94,   130,   137,
     -94,   -94,   -94,   -94,   -94,   -94,   -94
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
     -94,   -94,   158,   -94,   -94,   -94,   -94,   -45,   -94,   -94,
      94,    -1,   -61,   -29,   -94,   -94,   -94,   -79,   -94,   -94,
     -63,    -7,   -94,   -93,   -94,   -92,   -94,   -94,   -60,   -57,
     -94,   -94,   -94,   -94,   -19,   -94,   -94,   110,   -94,   -94,
      33,    82,    78,   144,   -94,    99,   -94,   -94,   -94
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -110
static const yytype_int16 yytable[] =
{
      89,    90,    39,   114,    95,   156,    10,    60,   146,   163,
     128,    74,    51,    86,    55,     4,    20,    99,    54,   148,
     100,   150,    63,    59,   102,    29,    52,   152,    56,   164,
      34,   134,    72,   114,   107,   114,    80,    56,   103,   -94,
      88,   153,    89,   125,    76,    61,   147,   157,   128,   128,
     111,   160,   143,   127,   -94,    67,   112,    87,    67,    92,
      74,   174,   110,    64,    98,   161,   111,    65,    62,   179,
     158,   159,   112,    66,    67,    67,   114,   113,    87,   147,
      49,    50,    52,   111,   125,   105,   106,    76,   157,   112,
      56,    67,    89,    91,   127,    96,    67,   108,   109,   104,
      89,    97,   121,   142,   113,   149,   131,    81,   132,    89,
     182,     7,     8,     9,    10,    11,    12,    13,   133,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,   154,
      26,    27,    28,    29,    30,    31,   155,   108,    34,    35,
      99,   162,   167,   168,   170,   -22,   169,   171,   172,    36,
     163,   176,   -22,  -108,   177,   -22,   180,   -22,   122,     5,
     -22,   183,     7,     8,     9,    10,    11,    12,    13,   184,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
     185,    26,    27,    28,    29,    30,    31,   186,   175,    34,
      35,   135,   145,   151,   123,    75,   -22,     0,     0,     0,
      36,     0,     0,   -22,  -109,   144,   -22,     0,   -22,     6,
       0,   -22,     0,     7,     8,     9,    10,    11,    12,    13,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,    28,    29,    30,    31,    32,    33,
      34,    35,     0,     0,     0,     0,     0,   -22,     0,     0,
       0,    36,     0,     0,   -22,     0,   137,   -22,     0,   -22,
       7,     8,     9,    10,    11,    12,    13,     0,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,     0,    26,
      27,    28,    29,    30,    31,     0,     0,    34,    35,     0,
       0,     0,     0,   -87,     0,     0,     0,     0,    36,     0,
       0,     0,   173,     0,     0,   -87,     7,     8,     9,    10,
      11,    12,    13,     0,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,     0,    26,    27,    28,    29,    30,
      31,     0,     0,    34,    35,     0,     0,     0,     0,   -87,
       0,     0,     0,     0,    36,     0,     0,     0,   178,     0,
       0,   -87,     7,     8,     9,    10,    11,    12,    13,     0,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
       0,    26,    27,    28,    29,    30,    31,     0,     0,    34,
      35,     0,     0,     0,     0,   -87,     0,     0,     0,     0,
      36,     0,     0,     0,     0,     0,     0,   -87,     7,     8,
       9,    10,    11,    12,    13,     0,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,     0,    26,    27,    28,
      29,    30,    31,     0,     0,    34,    35,     0,     0,     0,
       0,     0,   125,     0,     0,     0,   126,     0,     0,     0,
       0,     0,   127,     0,    67,     7,     8,     9,    10,    11,
      12,    13,     0,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,     0,    26,    27,    28,    29,    30,    31,
       0,     0,    34,    35,     0,     0,     0,     0,   181,     0,
       0,     0,     0,    36,     7,     8,     9,    10,    11,    12,
      13,     0,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,     0,    26,    27,    28,    29,    30,    31,     0,
       0,    34,    35,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    36
};

#define yypact_value_is_default(Yystate) \
  (!!((Yystate) == (-94)))

#define yytable_value_is_error(Yytable_value) \
  YYID (0)

static const yytype_int16 yycheck[] =
{
      61,    61,     3,    82,    67,     1,     8,    26,     1,    32,
      89,    40,    39,    58,    39,     0,    18,    31,    23,   112,
      34,   114,    46,    39,    35,    27,    53,    35,    53,    52,
      32,    94,    39,   112,    79,   114,    55,    53,    49,    35,
      59,    49,   103,    39,    39,    49,    39,    43,   127,   128,
      43,    35,    47,    49,    50,    51,    49,    58,    51,    66,
      89,   153,    39,    39,    71,    49,    43,    43,    49,   161,
     127,   128,    49,    49,    51,    51,   155,    54,    79,    39,
      46,    47,    53,    43,    39,    47,    48,    39,    43,    49,
      53,    51,   153,    39,    49,    46,    51,    46,    47,    52,
     161,    48,    47,    36,    54,    36,    50,     1,    50,   170,
     170,     5,     6,     7,     8,     9,    10,    11,    50,    13,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    46,
      24,    25,    26,    27,    28,    29,    48,    46,    32,    33,
      31,    46,    50,    50,    48,    39,    38,    50,    50,    43,
      32,    50,    46,    47,    50,    49,    36,    51,     1,     1,
      54,    50,     5,     6,     7,     8,     9,    10,    11,    50,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      50,    24,    25,    26,    27,    28,    29,    50,   155,    32,
      33,    97,   110,   115,    84,    51,    39,    -1,    -1,    -1,
      43,    -1,    -1,    46,    47,   106,    49,    -1,    51,     1,
      -1,    54,    -1,     5,     6,     7,     8,     9,    10,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    -1,    -1,    -1,    -1,    -1,    39,    -1,    -1,
      -1,    43,    -1,    -1,    46,    -1,     1,    49,    -1,    51,
       5,     6,     7,     8,     9,    10,    11,    -1,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    -1,    24,
      25,    26,    27,    28,    29,    -1,    -1,    32,    33,    -1,
      -1,    -1,    -1,    38,    -1,    -1,    -1,    -1,    43,    -1,
      -1,    -1,     1,    -1,    -1,    50,     5,     6,     7,     8,
       9,    10,    11,    -1,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    -1,    24,    25,    26,    27,    28,
      29,    -1,    -1,    32,    33,    -1,    -1,    -1,    -1,    38,
      -1,    -1,    -1,    -1,    43,    -1,    -1,    -1,     1,    -1,
      -1,    50,     5,     6,     7,     8,     9,    10,    11,    -1,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      -1,    24,    25,    26,    27,    28,    29,    -1,    -1,    32,
      33,    -1,    -1,    -1,    -1,    38,    -1,    -1,    -1,    -1,
      43,    -1,    -1,    -1,    -1,    -1,    -1,    50,     5,     6,
       7,     8,     9,    10,    11,    -1,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    -1,    24,    25,    26,
      27,    28,    29,    -1,    -1,    32,    33,    -1,    -1,    -1,
      -1,    -1,    39,    -1,    -1,    -1,    43,    -1,    -1,    -1,
      -1,    -1,    49,    -1,    51,     5,     6,     7,     8,     9,
      10,    11,    -1,    13,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    -1,    24,    25,    26,    27,    28,    29,
      -1,    -1,    32,    33,    -1,    -1,    -1,    -1,    38,    -1,
      -1,    -1,    -1,    43,     5,     6,     7,     8,     9,    10,
      11,    -1,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    -1,    24,    25,    26,    27,    28,    29,    -1,
      -1,    32,    33,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    43
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    56,    57,    58,     0,    57,     1,     5,     6,     7,
       8,     9,    10,    11,    12,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    43,    59,    62,    66,
      67,    68,    69,    70,    71,    75,    86,   101,   103,    46,
      47,    39,    53,    98,    23,    39,    53,    89,    61,    39,
      89,    49,    49,    46,    39,    43,    49,    51,    63,    64,
      65,    72,    76,    77,    68,    98,    39,    99,   100,    60,
      89,     1,    66,    90,    91,    92,    62,    66,    89,    67,
      83,    39,    76,    73,    74,    75,    46,    48,    76,    31,
      34,   102,    35,    49,    52,    47,    48,    62,    46,    47,
      39,    43,    49,    54,    72,    78,    79,    93,    94,    95,
      96,    47,     1,    92,    76,    39,    43,    49,    72,    84,
      85,    50,    50,    50,    75,    65,    97,     1,    80,    81,
      82,    83,    36,    47,   100,    96,     1,    39,    78,    36,
      78,    97,    35,    49,    46,    48,     1,    43,    84,    84,
      35,    49,    46,    32,    52,    87,    88,    50,    50,    38,
      48,    50,    50,     1,    80,    95,    50,    50,     1,    80,
      36,    38,    83,    50,    50,    50,    50
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
   Once GCC version 2 has supplanted version 1, this can go.  However,
   YYFAIL appears to be in use.  Nevertheless, it is formally deprecated
   in Bison 2.4.2's NEWS entry, where a plan to phase it out is
   discussed.  */

#define YYFAIL		goto yyerrlab
#if defined YYFAIL
  /* This is here to suppress warnings from the GCC cpp's
     -Wunused-macros.  Normally we don't worry about that warning, but
     some users do, and we want to make it easy for users to remove
     YYFAIL uses, which will produce warnings from Bison 2.5.  */
#endif

#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                  \
do                                                              \
  if (yychar == YYEMPTY)                                        \
    {                                                           \
      yychar = (Token);                                         \
      yylval = (Value);                                         \
      YYPOPSTACK (yylen);                                       \
      yystate = *yyssp;                                         \
      goto yybackup;                                            \
    }                                                           \
  else                                                          \
    {                                                           \
      yyerror (YY_("syntax error: cannot back up")); \
      YYERROR;							\
    }								\
while (YYID (0))

/* Error token number */
#define YYTERROR	1
#define YYERRCODE	256


/* This macro is provided for backward compatibility. */
#ifndef YY_LOCATION_PRINT
# define YY_LOCATION_PRINT(File, Loc) ((void) 0)
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
  FILE *yyo = yyoutput;
  YYUSE (yyo);
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

/* Copy into *YYMSG, which is of size *YYMSG_ALLOC, an error message
   about the unexpected token YYTOKEN for the state stack whose top is
   YYSSP.

   Return 0 if *YYMSG was successfully written.  Return 1 if *YYMSG is
   not large enough to hold the message.  In that case, also set
   *YYMSG_ALLOC to the required number of bytes.  Return 2 if the
   required number of bytes is too large to store.  */
static int
yysyntax_error (YYSIZE_T *yymsg_alloc, char **yymsg,
                yytype_int16 *yyssp, int yytoken)
{
  YYSIZE_T yysize0 = yytnamerr (YY_NULL, yytname[yytoken]);
  YYSIZE_T yysize = yysize0;
  enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
  /* Internationalized format string. */
  const char *yyformat = YY_NULL;
  /* Arguments of yyformat. */
  char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
  /* Number of reported tokens (one for the "unexpected", one per
     "expected"). */
  int yycount = 0;

  /* There are many possibilities here to consider:
     - Assume YYFAIL is not used.  It's too flawed to consider.  See
       <http://lists.gnu.org/archive/html/bison-patches/2009-12/msg00024.html>
       for details.  YYERROR is fine as it does not invoke this
       function.
     - If this state is a consistent state with a default action, then
       the only way this function was invoked is if the default action
       is an error action.  In that case, don't check for expected
       tokens because there are none.
     - The only way there can be no lookahead present (in yychar) is if
       this state is a consistent state with a default action.  Thus,
       detecting the absence of a lookahead is sufficient to determine
       that there is no unexpected or expected token to report.  In that
       case, just report a simple "syntax error".
     - Don't assume there isn't a lookahead just because this state is a
       consistent state with a default action.  There might have been a
       previous inconsistent state, consistent state with a non-default
       action, or user semantic action that manipulated yychar.
     - Of course, the expected token list depends on states to have
       correct lookahead information, and it depends on the parser not
       to perform extra reductions after fetching a lookahead from the
       scanner and before detecting a syntax error.  Thus, state merging
       (from LALR or IELR) and default reductions corrupt the expected
       token list.  However, the list is correct for canonical LR with
       one exception: it will still contain any token that will not be
       accepted due to an error action in a later state.
  */
  if (yytoken != YYEMPTY)
    {
      int yyn = yypact[*yyssp];
      yyarg[yycount++] = yytname[yytoken];
      if (!yypact_value_is_default (yyn))
        {
          /* Start YYX at -YYN if negative to avoid negative indexes in
             YYCHECK.  In other words, skip the first -YYN actions for
             this state because they are default actions.  */
          int yyxbegin = yyn < 0 ? -yyn : 0;
          /* Stay within bounds of both yycheck and yytname.  */
          int yychecklim = YYLAST - yyn + 1;
          int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
          int yyx;

          for (yyx = yyxbegin; yyx < yyxend; ++yyx)
            if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR
                && !yytable_value_is_error (yytable[yyx + yyn]))
              {
                if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
                  {
                    yycount = 1;
                    yysize = yysize0;
                    break;
                  }
                yyarg[yycount++] = yytname[yyx];
                {
                  YYSIZE_T yysize1 = yysize + yytnamerr (YY_NULL, yytname[yyx]);
                  if (! (yysize <= yysize1
                         && yysize1 <= YYSTACK_ALLOC_MAXIMUM))
                    return 2;
                  yysize = yysize1;
                }
              }
        }
    }

  switch (yycount)
    {
# define YYCASE_(N, S)                      \
      case N:                               \
        yyformat = S;                       \
      break
      YYCASE_(0, YY_("syntax error"));
      YYCASE_(1, YY_("syntax error, unexpected %s"));
      YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
      YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
      YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
      YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
# undef YYCASE_
    }

  {
    YYSIZE_T yysize1 = yysize + yystrlen (yyformat);
    if (! (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM))
      return 2;
    yysize = yysize1;
  }

  if (*yymsg_alloc < yysize)
    {
      *yymsg_alloc = 2 * yysize;
      if (! (yysize <= *yymsg_alloc
             && *yymsg_alloc <= YYSTACK_ALLOC_MAXIMUM))
        *yymsg_alloc = YYSTACK_ALLOC_MAXIMUM;
      return 1;
    }

  /* Avoid sprintf, as that infringes on the user's name space.
     Don't have undefined behavior even if the translation
     produced a string with the wrong number of "%s"s.  */
  {
    char *yyp = *yymsg;
    int yyi = 0;
    while ((*yyp = *yyformat) != '\0')
      if (*yyp == '%' && yyformat[1] == 's' && yyi < yycount)
        {
          yyp += yytnamerr (yyp, yyarg[yyi++]);
          yyformat += 2;
        }
      else
        {
          yyp++;
          yyformat++;
        }
  }
  return 0;
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




/* The lookahead symbol.  */
int yychar;


#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval YY_INITIAL_VALUE(yyval_default);

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
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus;

    /* The stacks and their tools:
       `yyss': related to states.
       `yyvs': related to semantic values.

       Refer to the stacks through separate pointers, to allow yyoverflow
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
  int yytoken = 0;
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

  yyssp = yyss = yyssa;
  yyvsp = yyvs = yyvsa;
  yystacksize = YYINITDEPTH;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY; /* Cause a token to be read.  */
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
  if (yypact_value_is_default (yyn))
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
      if (yytable_value_is_error (yyn))
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
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

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

    { is_typedef = 0; is_extern = 0; current_name = NULL; decl_spec = NULL; }
    break;

  case 5:

    { free_list(*(yyvsp[(2) - (2)]), NULL); *(yyvsp[(2) - (2)]) = NULL; }
    break;

  case 6:

    { is_typedef = 1; }
    break;

  case 7:

    { (yyval) = (yyvsp[(4) - (4)]); }
    break;

  case 8:

    { is_typedef = 1; }
    break;

  case 9:

    { (yyval) = (yyvsp[(3) - (3)]); }
    break;

  case 14:

    { (yyval) = (yyvsp[(2) - (2)]); }
    break;

  case 15:

    { (yyval) = (yyvsp[(2) - (2)]); }
    break;

  case 16:

    { if (current_name) {
		    struct string_list *decl = (*(yyvsp[(3) - (3)]))->next;
		    (*(yyvsp[(3) - (3)]))->next = NULL;
		    add_symbol(current_name,
			       is_typedef ? SYM_TYPEDEF : SYM_NORMAL,
			       decl, is_extern);
		    current_name = NULL;
		  }
		  (yyval) = (yyvsp[(3) - (3)]);
		}
    break;

  case 17:

    { (yyval) = NULL; }
    break;

  case 19:

    { struct string_list *decl = *(yyvsp[(1) - (1)]);
		  *(yyvsp[(1) - (1)]) = NULL;
		  add_symbol(current_name,
			     is_typedef ? SYM_TYPEDEF : SYM_NORMAL, decl, is_extern);
		  current_name = NULL;
		  (yyval) = (yyvsp[(1) - (1)]);
		}
    break;

  case 20:

    { struct string_list *decl = *(yyvsp[(3) - (3)]);
		  *(yyvsp[(3) - (3)]) = NULL;
		  free_list(*(yyvsp[(2) - (3)]), NULL);
		  *(yyvsp[(2) - (3)]) = decl_spec;
		  add_symbol(current_name,
			     is_typedef ? SYM_TYPEDEF : SYM_NORMAL, decl, is_extern);
		  current_name = NULL;
		  (yyval) = (yyvsp[(3) - (3)]);
		}
    break;

  case 21:

    { (yyval) = (yyvsp[(4) - (4)]) ? (yyvsp[(4) - (4)]) : (yyvsp[(3) - (4)]) ? (yyvsp[(3) - (4)]) : (yyvsp[(2) - (4)]) ? (yyvsp[(2) - (4)]) : (yyvsp[(1) - (4)]); }
    break;

  case 22:

    { decl_spec = NULL; }
    break;

  case 24:

    { decl_spec = *(yyvsp[(1) - (1)]); }
    break;

  case 25:

    { decl_spec = *(yyvsp[(2) - (2)]); }
    break;

  case 26:

    { /* Version 2 checksumming ignores storage class, as that
		     is really irrelevant to the linkage.  */
		  remove_node((yyvsp[(1) - (1)]));
		  (yyval) = (yyvsp[(1) - (1)]);
		}
    break;

  case 31:

    { is_extern = 1; (yyval) = (yyvsp[(1) - (1)]); }
    break;

  case 32:

    { is_extern = 0; (yyval) = (yyvsp[(1) - (1)]); }
    break;

  case 37:

    { remove_node((yyvsp[(1) - (2)])); (*(yyvsp[(2) - (2)]))->tag = SYM_STRUCT; (yyval) = (yyvsp[(2) - (2)]); }
    break;

  case 38:

    { remove_node((yyvsp[(1) - (2)])); (*(yyvsp[(2) - (2)]))->tag = SYM_UNION; (yyval) = (yyvsp[(2) - (2)]); }
    break;

  case 39:

    { remove_node((yyvsp[(1) - (2)])); (*(yyvsp[(2) - (2)]))->tag = SYM_ENUM; (yyval) = (yyvsp[(2) - (2)]); }
    break;

  case 40:

    { record_compound((yyvsp[(1) - (3)]), (yyvsp[(2) - (3)]), (yyvsp[(3) - (3)]), SYM_STRUCT); (yyval) = (yyvsp[(3) - (3)]); }
    break;

  case 41:

    { record_compound((yyvsp[(1) - (3)]), (yyvsp[(2) - (3)]), (yyvsp[(3) - (3)]), SYM_UNION); (yyval) = (yyvsp[(3) - (3)]); }
    break;

  case 42:

    { record_compound((yyvsp[(1) - (3)]), (yyvsp[(2) - (3)]), (yyvsp[(3) - (3)]), SYM_ENUM); (yyval) = (yyvsp[(3) - (3)]); }
    break;

  case 43:

    { add_symbol(NULL, SYM_ENUM, NULL, 0); (yyval) = (yyvsp[(2) - (2)]); }
    break;

  case 44:

    { (yyval) = (yyvsp[(2) - (2)]); }
    break;

  case 45:

    { (yyval) = (yyvsp[(2) - (2)]); }
    break;

  case 57:

    { (*(yyvsp[(1) - (1)]))->tag = SYM_TYPEDEF; (yyval) = (yyvsp[(1) - (1)]); }
    break;

  case 58:

    { (yyval) = (yyvsp[(2) - (2)]) ? (yyvsp[(2) - (2)]) : (yyvsp[(1) - (2)]); }
    break;

  case 59:

    { (yyval) = NULL; }
    break;

  case 62:

    { (yyval) = (yyvsp[(2) - (2)]); }
    break;

  case 66:

    { /* restrict has no effect in prototypes so ignore it */
		  remove_node((yyvsp[(1) - (1)]));
		  (yyval) = (yyvsp[(1) - (1)]);
		}
    break;

  case 67:

    { (yyval) = (yyvsp[(2) - (2)]); }
    break;

  case 69:

    { if (current_name != NULL) {
		    error_with_pos("unexpected second declaration name");
		    YYERROR;
		  } else {
		    current_name = (*(yyvsp[(1) - (1)]))->string;
		    (yyval) = (yyvsp[(1) - (1)]);
		  }
		}
    break;

  case 70:

    { if (current_name != NULL) {
		    error_with_pos("unexpected second declaration name");
		    YYERROR;
		  } else {
		    current_name = (*(yyvsp[(1) - (1)]))->string;
		    (yyval) = (yyvsp[(1) - (1)]);
		  }
		}
    break;

  case 71:

    { (yyval) = (yyvsp[(4) - (4)]); }
    break;

  case 72:

    { (yyval) = (yyvsp[(4) - (4)]); }
    break;

  case 73:

    { (yyval) = (yyvsp[(2) - (2)]); }
    break;

  case 74:

    { (yyval) = (yyvsp[(3) - (3)]); }
    break;

  case 75:

    { (yyval) = (yyvsp[(2) - (2)]); }
    break;

  case 79:

    { (yyval) = (yyvsp[(4) - (4)]); }
    break;

  case 80:

    { (yyval) = (yyvsp[(4) - (4)]); }
    break;

  case 81:

    { (yyval) = (yyvsp[(2) - (2)]); }
    break;

  case 82:

    { (yyval) = (yyvsp[(3) - (3)]); }
    break;

  case 83:

    { (yyval) = (yyvsp[(3) - (3)]); }
    break;

  case 84:

    { (yyval) = (yyvsp[(2) - (2)]); }
    break;

  case 86:

    { (yyval) = (yyvsp[(3) - (3)]); }
    break;

  case 87:

    { (yyval) = NULL; }
    break;

  case 90:

    { (yyval) = (yyvsp[(3) - (3)]); }
    break;

  case 91:

    { (yyval) = (yyvsp[(2) - (2)]) ? (yyvsp[(2) - (2)]) : (yyvsp[(1) - (2)]); }
    break;

  case 92:

    { (yyval) = (yyvsp[(2) - (2)]) ? (yyvsp[(2) - (2)]) : (yyvsp[(1) - (2)]); }
    break;

  case 94:

    { (yyval) = NULL; }
    break;

  case 95:

    { /* For version 2 checksums, we don't want to remember
		     private parameter names.  */
		  remove_node((yyvsp[(1) - (1)]));
		  (yyval) = (yyvsp[(1) - (1)]);
		}
    break;

  case 96:

    { remove_node((yyvsp[(1) - (1)]));
		  (yyval) = (yyvsp[(1) - (1)]);
		}
    break;

  case 97:

    { (yyval) = (yyvsp[(4) - (4)]); }
    break;

  case 98:

    { (yyval) = (yyvsp[(4) - (4)]); }
    break;

  case 99:

    { (yyval) = (yyvsp[(2) - (2)]); }
    break;

  case 100:

    { (yyval) = (yyvsp[(3) - (3)]); }
    break;

  case 101:

    { (yyval) = (yyvsp[(3) - (3)]); }
    break;

  case 102:

    { struct string_list *decl = *(yyvsp[(2) - (3)]);
		  *(yyvsp[(2) - (3)]) = NULL;
		  add_symbol(current_name, SYM_NORMAL, decl, is_extern);
		  (yyval) = (yyvsp[(3) - (3)]);
		}
    break;

  case 103:

    { (yyval) = NULL; }
    break;

  case 105:

    { remove_list((yyvsp[(2) - (2)]), &(*(yyvsp[(1) - (2)]))->next); (yyval) = (yyvsp[(2) - (2)]); }
    break;

  case 106:

    { (yyval) = (yyvsp[(3) - (3)]); }
    break;

  case 107:

    { (yyval) = (yyvsp[(3) - (3)]); }
    break;

  case 108:

    { (yyval) = NULL; }
    break;

  case 111:

    { (yyval) = (yyvsp[(2) - (2)]); }
    break;

  case 112:

    { (yyval) = (yyvsp[(3) - (3)]); }
    break;

  case 113:

    { (yyval) = (yyvsp[(2) - (2)]); }
    break;

  case 114:

    { (yyval) = NULL; }
    break;

  case 117:

    { (yyval) = (yyvsp[(3) - (3)]); }
    break;

  case 118:

    { (yyval) = (yyvsp[(2) - (2)]) ? (yyvsp[(2) - (2)]) : (yyvsp[(1) - (2)]); }
    break;

  case 119:

    { (yyval) = (yyvsp[(2) - (2)]); }
    break;

  case 121:

    { (yyval) = (yyvsp[(2) - (2)]); }
    break;

  case 122:

    { (yyval) = NULL; }
    break;

  case 124:

    { (yyval) = (yyvsp[(3) - (3)]); }
    break;

  case 125:

    { (yyval) = (yyvsp[(4) - (4)]); }
    break;

  case 128:

    {
			const char *name = strdup((*(yyvsp[(1) - (1)]))->string);
			add_symbol(name, SYM_ENUM_CONST, NULL, 0);
		}
    break;

  case 129:

    {
			const char *name = strdup((*(yyvsp[(1) - (3)]))->string);
			struct string_list *expr = copy_list_range(*(yyvsp[(3) - (3)]), *(yyvsp[(2) - (3)]));
			add_symbol(name, SYM_ENUM_CONST, expr, 0);
		}
    break;

  case 130:

    { (yyval) = (yyvsp[(2) - (2)]); }
    break;

  case 131:

    { (yyval) = NULL; }
    break;

  case 133:

    { export_symbol((*(yyvsp[(3) - (5)]))->string); (yyval) = (yyvsp[(5) - (5)]); }
    break;



      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
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
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYEMPTY : YYTRANSLATE (yychar);

  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if ! YYERROR_VERBOSE
      yyerror (YY_("syntax error"));
#else
# define YYSYNTAX_ERROR yysyntax_error (&yymsg_alloc, &yymsg, \
                                        yyssp, yytoken)
      {
        char const *yymsgp = YY_("syntax error");
        int yysyntax_error_status;
        yysyntax_error_status = YYSYNTAX_ERROR;
        if (yysyntax_error_status == 0)
          yymsgp = yymsg;
        else if (yysyntax_error_status == 1)
          {
            if (yymsg != yymsgbuf)
              YYSTACK_FREE (yymsg);
            yymsg = (char *) YYSTACK_ALLOC (yymsg_alloc);
            if (!yymsg)
              {
                yymsg = yymsgbuf;
                yymsg_alloc = sizeof yymsgbuf;
                yysyntax_error_status = 2;
              }
            else
              {
                yysyntax_error_status = YYSYNTAX_ERROR;
                yymsgp = yymsg;
              }
          }
        yyerror (yymsgp);
        if (yysyntax_error_status == 2)
          goto yyexhaustedlab;
      }
# undef YYSYNTAX_ERROR
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
      if (!yypact_value_is_default (yyn))
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

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


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

#if !defined yyoverflow || YYERROR_VERBOSE
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
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
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





static void
yyerror(const char *e)
{
  error_with_pos("%s", e);
}
