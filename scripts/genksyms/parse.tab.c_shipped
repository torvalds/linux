/* A Bison parser, made by GNU Bison 3.0.4.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015 Free Software Foundation, Inc.

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
#define YYBISON_VERSION "3.0.4"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* Copy the first part of user declarations.  */
#line 24 "parse.y" /* yacc.c:339  */


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


#line 117 "parse.tab.c" /* yacc.c:339  */

# ifndef YY_NULLPTR
#  if defined __cplusplus && 201103L <= __cplusplus
#   define YY_NULLPTR nullptr
#  else
#   define YY_NULLPTR 0
#  endif
# endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 0
#endif

/* In a future release of Bison, this section will be replaced
   by #include "parse.tab.h".  */
#ifndef YY_YY_PARSE_TAB_H_INCLUDED
# define YY_YY_PARSE_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 1
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token type.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
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
    TYPEOF_PHRASE = 287,
    BRACE_PHRASE = 288,
    BRACKET_PHRASE = 289,
    EXPRESSION_PHRASE = 290,
    CHAR = 291,
    DOTS = 292,
    IDENT = 293,
    INT = 294,
    REAL = 295,
    STRING = 296,
    TYPE = 297,
    OTHER = 298,
    FILENAME = 299
  };
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef int YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;

int yyparse (void);

#endif /* !YY_YY_PARSE_TAB_H_INCLUDED  */

/* Copy the second part of user declarations.  */

#line 213 "parse.tab.c" /* yacc.c:358  */

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
#else
typedef signed char yytype_int8;
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
# elif ! defined YYSIZE_T
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

#ifndef YY_ATTRIBUTE
# if (defined __GNUC__                                               \
      && (2 < __GNUC__ || (__GNUC__ == 2 && 96 <= __GNUC_MINOR__)))  \
     || defined __SUNPRO_C && 0x5110 <= __SUNPRO_C
#  define YY_ATTRIBUTE(Spec) __attribute__(Spec)
# else
#  define YY_ATTRIBUTE(Spec) /* empty */
# endif
#endif

#ifndef YY_ATTRIBUTE_PURE
# define YY_ATTRIBUTE_PURE   YY_ATTRIBUTE ((__pure__))
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# define YY_ATTRIBUTE_UNUSED YY_ATTRIBUTE ((__unused__))
#endif

#if !defined _Noreturn \
     && (!defined __STDC_VERSION__ || __STDC_VERSION__ < 201112)
# if defined _MSC_VER && 1200 <= _MSC_VER
#  define _Noreturn __declspec (noreturn)
# else
#  define _Noreturn YY_ATTRIBUTE ((__noreturn__))
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YYUSE(E) ((void) (E))
#else
# define YYUSE(E) /* empty */
#endif

#if defined __GNUC__ && 407 <= __GNUC__ * 100 + __GNUC_MINOR__
/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN \
    _Pragma ("GCC diagnostic push") \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")\
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# define YY_IGNORE_MAYBE_UNINITIALIZED_END \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
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
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
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
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
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
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
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
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYSIZE_T yynewbytes;                                            \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / sizeof (*yyptr);                          \
      }                                                                 \
    while (0)

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
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  4
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   513

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  54
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  49
/* YYNRULES -- Number of rules.  */
#define YYNRULES  132
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  186

/* YYTRANSLATE[YYX] -- Symbol number corresponding to YYX as returned
   by yylex, with out-of-bounds checking.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   299

#define YYTRANSLATE(YYX)                                                \
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, without out-of-bounds checking.  */
static const yytype_uint8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      48,    49,    50,     2,    47,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    53,    45,
       2,    51,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    52,     2,    46,     2,     2,     2,     2,
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
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44
};

#if YYDEBUG
  /* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   124,   124,   125,   129,   129,   135,   135,   137,   137,
     139,   140,   141,   142,   143,   144,   148,   162,   163,   167,
     175,   188,   194,   195,   199,   200,   204,   210,   214,   215,
     216,   217,   218,   222,   223,   224,   225,   229,   231,   233,
     237,   239,   241,   246,   249,   250,   254,   255,   256,   257,
     258,   259,   260,   261,   262,   263,   264,   268,   273,   274,
     278,   279,   283,   283,   283,   284,   292,   293,   297,   306,
     315,   317,   319,   321,   328,   329,   333,   334,   335,   337,
     339,   341,   343,   348,   349,   350,   354,   355,   359,   360,
     365,   370,   372,   376,   377,   385,   389,   391,   393,   395,
     397,   402,   411,   412,   417,   422,   423,   427,   428,   432,
     433,   437,   439,   444,   445,   449,   450,   454,   455,   456,
     460,   464,   465,   469,   470,   474,   475,   478,   483,   491,
     495,   496,   500
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
  "VOLATILE_KEYW", "TYPEOF_KEYW", "EXPORT_SYMBOL_KEYW", "ASM_PHRASE",
  "ATTRIBUTE_PHRASE", "TYPEOF_PHRASE", "BRACE_PHRASE", "BRACKET_PHRASE",
  "EXPRESSION_PHRASE", "CHAR", "DOTS", "IDENT", "INT", "REAL", "STRING",
  "TYPE", "OTHER", "FILENAME", "';'", "'}'", "','", "'('", "')'", "'*'",
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
  "asm_phrase_opt", "export_definition", YY_NULLPTR
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[NUM] -- (External) token number corresponding to the
   (internal) symbol number NUM (which must be that of a token).  */
static const yytype_uint16 yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,   298,   299,    59,   125,    44,    40,    41,
      42,    61,   123,    58
};
# endif

#define YYPACT_NINF -135

#define yypact_value_is_default(Yystate) \
  (!!((Yystate) == (-135)))

#define YYTABLE_NINF -109

#define yytable_value_is_error(Yytable_value) \
  0

  /* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
     STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -135,    38,  -135,   206,  -135,  -135,    22,  -135,  -135,  -135,
    -135,  -135,   -24,  -135,    20,  -135,  -135,  -135,  -135,  -135,
    -135,  -135,  -135,  -135,   -23,  -135,     6,  -135,  -135,  -135,
      -2,    15,    24,  -135,  -135,  -135,  -135,  -135,    41,   471,
    -135,  -135,  -135,  -135,  -135,  -135,  -135,  -135,  -135,  -135,
      13,    36,  -135,  -135,    35,   106,  -135,   471,    35,  -135,
     471,    44,  -135,  -135,  -135,    41,    39,    45,    48,  -135,
      41,   -10,    25,  -135,  -135,    47,    34,  -135,   471,  -135,
      26,   -26,    53,   156,  -135,  -135,    41,  -135,   387,    52,
      57,    59,  -135,    39,  -135,  -135,    41,  -135,  -135,  -135,
    -135,  -135,   252,    67,  -135,   -21,  -135,  -135,  -135,    51,
    -135,    12,    83,    46,  -135,    27,    84,    88,  -135,  -135,
    -135,    91,  -135,   109,  -135,  -135,     3,    55,  -135,    30,
    -135,    95,  -135,  -135,  -135,   -20,    92,    93,   108,    96,
    -135,  -135,  -135,  -135,  -135,    97,  -135,    98,  -135,  -135,
     118,  -135,   297,  -135,   -26,   101,  -135,   104,  -135,  -135,
     342,  -135,  -135,   120,  -135,  -135,  -135,  -135,  -135,   433,
    -135,  -135,   111,   119,  -135,  -135,  -135,   130,   136,  -135,
    -135,  -135,  -135,  -135,  -135,  -135
};

  /* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
     Performed when YYTABLE does not specify something else to do.  Zero
     means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       4,     4,     2,     0,     1,     3,     0,    28,    55,    46,
      62,    53,     0,    31,     0,    52,    32,    48,    49,    29,
      65,    47,    50,    30,     0,     8,     0,    51,    54,    63,
       0,     0,     0,    64,    36,    56,     5,    10,    17,    23,
      24,    26,    27,    33,    34,    11,    12,    13,    14,    15,
      39,     0,    43,     6,    37,     0,    44,    22,    38,    45,
       0,     0,   129,    68,    69,     0,    58,     0,    18,    19,
       0,   130,    67,    25,    42,   127,     0,   125,    22,    40,
       0,   113,     0,     0,   109,     9,    17,    41,    93,     0,
       0,     0,    57,    59,    60,    16,     0,    66,   131,   101,
     121,    72,     0,     0,   123,     0,     7,   112,   106,    76,
      77,     0,     0,     0,   121,    75,     0,   114,   115,   119,
     105,     0,   110,   130,    94,    56,     0,    93,    90,    92,
      35,     0,    73,    61,    20,   102,     0,     0,    84,    87,
      88,   128,   124,   126,   118,     0,    76,     0,   120,    74,
     117,    80,     0,   111,     0,     0,    95,     0,    91,    98,
       0,   132,   122,     0,    21,   103,    71,    70,    83,     0,
      82,    81,     0,     0,   116,   100,    99,     0,     0,   104,
      85,    89,    79,    78,    97,    96
};

  /* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -135,  -135,   157,  -135,  -135,  -135,  -135,   -48,  -135,  -135,
      90,    -1,   -60,   -33,  -135,  -135,  -135,   -78,  -135,  -135,
     -61,   -31,  -135,   -92,  -135,  -134,  -135,  -135,   -59,   -41,
    -135,  -135,  -135,  -135,   -18,  -135,  -135,   107,  -135,  -135,
      37,    80,    78,   143,  -135,    94,  -135,  -135,  -135
};

  /* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,     1,     2,     3,    36,    78,    57,    37,    67,    68,
      69,    81,    39,    40,    41,    42,    43,    70,    92,    93,
      44,   123,    72,   114,   115,   137,   138,   139,   140,   128,
     129,    45,   164,   165,    56,    82,    83,    84,   116,   117,
     118,   119,   135,    52,    76,    77,    46,   100,    47
};

  /* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
     positive, shift that token.  If negative, reduce the rule whose
     number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      88,    89,    38,   113,   155,    94,    73,    71,    59,    85,
     127,   162,   109,   145,    50,    54,   110,    75,   173,   147,
      98,   149,   111,    99,    66,   142,   178,   112,    51,    55,
     106,   163,   133,   113,    91,   113,    79,   -93,     4,    97,
      87,   124,    88,    53,    58,   156,    60,    10,   127,   127,
     146,   126,   -93,    66,   110,    73,    86,    20,    55,   101,
     111,   151,    66,    61,   159,    51,    29,    48,    49,    62,
      33,   107,   108,   102,    75,   152,   113,    86,   160,    63,
     104,   105,    90,    64,   146,   157,   158,    55,   110,    65,
      95,    66,    88,   124,   111,    96,    66,   156,   103,   120,
      88,   130,   141,   126,   112,    66,   131,    80,   132,    88,
     181,     7,     8,     9,    10,    11,    12,    13,   148,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,   153,
      26,    27,    28,    29,    30,   154,   107,    33,    34,    98,
     161,   166,   167,   169,   -22,   168,   170,   171,    35,   162,
     175,   -22,  -107,   176,   -22,   179,   -22,   121,     5,   -22,
     182,     7,     8,     9,    10,    11,    12,    13,   183,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,   184,
      26,    27,    28,    29,    30,   185,   134,    33,    34,   144,
     122,   174,   150,    74,   -22,     0,     0,     0,    35,   143,
       0,   -22,  -108,     0,   -22,     0,   -22,     6,     0,   -22,
       0,     7,     8,     9,    10,    11,    12,    13,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    32,    33,    34,     0,
       0,     0,     0,     0,   -22,     0,     0,     0,    35,     0,
       0,   -22,     0,   136,   -22,     0,   -22,     7,     8,     9,
      10,    11,    12,    13,     0,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,     0,    26,    27,    28,    29,
      30,     0,     0,    33,    34,     0,     0,     0,     0,   -86,
       0,     0,     0,     0,    35,     0,     0,     0,   172,     0,
       0,   -86,     7,     8,     9,    10,    11,    12,    13,     0,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
       0,    26,    27,    28,    29,    30,     0,     0,    33,    34,
       0,     0,     0,     0,   -86,     0,     0,     0,     0,    35,
       0,     0,     0,   177,     0,     0,   -86,     7,     8,     9,
      10,    11,    12,    13,     0,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,     0,    26,    27,    28,    29,
      30,     0,     0,    33,    34,     0,     0,     0,     0,   -86,
       0,     0,     0,     0,    35,     0,     0,     0,     0,     0,
       0,   -86,     7,     8,     9,    10,    11,    12,    13,     0,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
       0,    26,    27,    28,    29,    30,     0,     0,    33,    34,
       0,     0,     0,     0,     0,   124,     0,     0,     0,   125,
       0,     0,     0,     0,     0,   126,     0,    66,     7,     8,
       9,    10,    11,    12,    13,     0,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,     0,    26,    27,    28,
      29,    30,     0,     0,    33,    34,     0,     0,     0,     0,
     180,     0,     0,     0,     0,    35,     7,     8,     9,    10,
      11,    12,    13,     0,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,     0,    26,    27,    28,    29,    30,
       0,     0,    33,    34,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    35
};

static const yytype_int16 yycheck[] =
{
      60,    60,     3,    81,     1,    66,    39,    38,    26,    57,
      88,    31,    38,     1,    38,    38,    42,    38,   152,   111,
      30,   113,    48,    33,    50,    46,   160,    53,    52,    52,
      78,    51,    93,   111,    65,   113,    54,    34,     0,    70,
      58,    38,   102,    23,    38,    42,    48,     8,   126,   127,
      38,    48,    49,    50,    42,    88,    57,    18,    52,    34,
      48,    34,    50,    48,    34,    52,    27,    45,    46,    45,
      31,    45,    46,    48,    38,    48,   154,    78,    48,    38,
      46,    47,    38,    42,    38,   126,   127,    52,    42,    48,
      45,    50,   152,    38,    48,    47,    50,    42,    51,    46,
     160,    49,    35,    48,    53,    50,    49,     1,    49,   169,
     169,     5,     6,     7,     8,     9,    10,    11,    35,    13,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    45,
      24,    25,    26,    27,    28,    47,    45,    31,    32,    30,
      45,    49,    49,    47,    38,    37,    49,    49,    42,    31,
      49,    45,    46,    49,    48,    35,    50,     1,     1,    53,
      49,     5,     6,     7,     8,     9,    10,    11,    49,    13,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    49,
      24,    25,    26,    27,    28,    49,    96,    31,    32,   109,
      83,   154,   114,    50,    38,    -1,    -1,    -1,    42,   105,
      -1,    45,    46,    -1,    48,    -1,    50,     1,    -1,    53,
      -1,     5,     6,     7,     8,     9,    10,    11,    12,    13,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,    28,    29,    30,    31,    32,    -1,
      -1,    -1,    -1,    -1,    38,    -1,    -1,    -1,    42,    -1,
      -1,    45,    -1,     1,    48,    -1,    50,     5,     6,     7,
       8,     9,    10,    11,    -1,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    -1,    24,    25,    26,    27,
      28,    -1,    -1,    31,    32,    -1,    -1,    -1,    -1,    37,
      -1,    -1,    -1,    -1,    42,    -1,    -1,    -1,     1,    -1,
      -1,    49,     5,     6,     7,     8,     9,    10,    11,    -1,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      -1,    24,    25,    26,    27,    28,    -1,    -1,    31,    32,
      -1,    -1,    -1,    -1,    37,    -1,    -1,    -1,    -1,    42,
      -1,    -1,    -1,     1,    -1,    -1,    49,     5,     6,     7,
       8,     9,    10,    11,    -1,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    -1,    24,    25,    26,    27,
      28,    -1,    -1,    31,    32,    -1,    -1,    -1,    -1,    37,
      -1,    -1,    -1,    -1,    42,    -1,    -1,    -1,    -1,    -1,
      -1,    49,     5,     6,     7,     8,     9,    10,    11,    -1,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      -1,    24,    25,    26,    27,    28,    -1,    -1,    31,    32,
      -1,    -1,    -1,    -1,    -1,    38,    -1,    -1,    -1,    42,
      -1,    -1,    -1,    -1,    -1,    48,    -1,    50,     5,     6,
       7,     8,     9,    10,    11,    -1,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    -1,    24,    25,    26,
      27,    28,    -1,    -1,    31,    32,    -1,    -1,    -1,    -1,
      37,    -1,    -1,    -1,    -1,    42,     5,     6,     7,     8,
       9,    10,    11,    -1,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    -1,    24,    25,    26,    27,    28,
      -1,    -1,    31,    32,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    42
};

  /* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
     symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    55,    56,    57,     0,    56,     1,     5,     6,     7,
       8,     9,    10,    11,    12,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    32,    42,    58,    61,    65,    66,
      67,    68,    69,    70,    74,    85,   100,   102,    45,    46,
      38,    52,    97,    23,    38,    52,    88,    60,    38,    88,
      48,    48,    45,    38,    42,    48,    50,    62,    63,    64,
      71,    75,    76,    67,    97,    38,    98,    99,    59,    88,
       1,    65,    89,    90,    91,    61,    65,    88,    66,    82,
      38,    75,    72,    73,    74,    45,    47,    75,    30,    33,
     101,    34,    48,    51,    46,    47,    61,    45,    46,    38,
      42,    48,    53,    71,    77,    78,    92,    93,    94,    95,
      46,     1,    91,    75,    38,    42,    48,    71,    83,    84,
      49,    49,    49,    74,    64,    96,     1,    79,    80,    81,
      82,    35,    46,    99,    95,     1,    38,    77,    35,    77,
      96,    34,    48,    45,    47,     1,    42,    83,    83,    34,
      48,    45,    31,    51,    86,    87,    49,    49,    37,    47,
      49,    49,     1,    79,    94,    49,    49,     1,    79,    35,
      37,    82,    49,    49,    49,    49
};

  /* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,    54,    55,    55,    57,    56,    59,    58,    60,    58,
      58,    58,    58,    58,    58,    58,    61,    62,    62,    63,
      63,    64,    65,    65,    66,    66,    67,    67,    68,    68,
      68,    68,    68,    69,    69,    69,    69,    69,    69,    69,
      69,    69,    69,    69,    69,    69,    70,    70,    70,    70,
      70,    70,    70,    70,    70,    70,    70,    71,    72,    72,
      73,    73,    74,    74,    74,    74,    75,    75,    76,    76,
      76,    76,    76,    76,    77,    77,    78,    78,    78,    78,
      78,    78,    78,    79,    79,    79,    80,    80,    81,    81,
      82,    83,    83,    84,    84,    84,    84,    84,    84,    84,
      84,    85,    86,    86,    87,    88,    88,    89,    89,    90,
      90,    91,    91,    92,    92,    93,    93,    94,    94,    94,
      95,    96,    96,    97,    97,    98,    98,    99,    99,   100,
     101,   101,   102
};

  /* YYR2[YYN] -- Number of symbols on the right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     1,     2,     0,     2,     0,     4,     0,     3,
       1,     1,     1,     1,     2,     2,     3,     0,     1,     1,
       3,     4,     0,     1,     1,     2,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     4,     1,     2,     2,     2,
       3,     3,     3,     2,     2,     2,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     2,     0,     1,
       1,     2,     1,     1,     1,     1,     2,     1,     1,     1,
       4,     4,     2,     3,     2,     1,     1,     1,     4,     4,
       2,     3,     3,     2,     1,     3,     0,     1,     1,     3,
       2,     2,     1,     0,     1,     1,     4,     4,     2,     3,
       3,     3,     0,     1,     2,     3,     3,     0,     1,     1,
       2,     3,     2,     0,     1,     1,     3,     2,     2,     1,
       2,     0,     2,     3,     4,     1,     3,     1,     3,     2,
       0,     1,     5
};


#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)
#define YYEMPTY         (-2)
#define YYEOF           0

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab


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
      YYERROR;                                                  \
    }                                                           \
while (0)

/* Error token number */
#define YYTERROR        1
#define YYERRCODE       256



/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)

/* This macro is provided for backward compatibility. */
#ifndef YY_LOCATION_PRINT
# define YY_LOCATION_PRINT(File, Loc) ((void) 0)
#endif


# define YY_SYMBOL_PRINT(Title, Type, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Type, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*----------------------------------------.
| Print this symbol's value on YYOUTPUT.  |
`----------------------------------------*/

static void
yy_symbol_value_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
{
  FILE *yyo = yyoutput;
  YYUSE (yyo);
  if (!yyvaluep)
    return;
# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# endif
  YYUSE (yytype);
}


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

static void
yy_symbol_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyoutput, "%s %s (",
             yytype < YYNTOKENS ? "token" : "nterm", yytname[yytype]);

  yy_symbol_value_print (yyoutput, yytype, yyvaluep);
  YYFPRINTF (yyoutput, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yytype_int16 *yybottom, yytype_int16 *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yytype_int16 *yyssp, YYSTYPE *yyvsp, int yyrule)
{
  unsigned long int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %lu):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       yystos[yyssp[yyi + 1 - yynrhs]],
                       &(yyvsp[(yyi + 1) - (yynrhs)])
                                              );
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
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
#ifndef YYINITDEPTH
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
static YYSIZE_T
yystrlen (const char *yystr)
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
static char *
yystpcpy (char *yydest, const char *yysrc)
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
  YYSIZE_T yysize0 = yytnamerr (YY_NULLPTR, yytname[yytoken]);
  YYSIZE_T yysize = yysize0;
  enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
  /* Internationalized format string. */
  const char *yyformat = YY_NULLPTR;
  /* Arguments of yyformat. */
  char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
  /* Number of reported tokens (one for the "unexpected", one per
     "expected"). */
  int yycount = 0;

  /* There are many possibilities here to consider:
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
                  YYSIZE_T yysize1 = yysize + yytnamerr (YY_NULLPTR, yytname[yyx]);
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

static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep)
{
  YYUSE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YYUSE (yytype);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}




/* The lookahead symbol.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;


/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    int yystate;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus;

    /* The stacks and their tools:
       'yyss': related to states.
       'yyvs': related to semantic values.

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
      yychar = yylex ();
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
     '$$ = $1'.

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
#line 129 "parse.y" /* yacc.c:1646  */
    { is_typedef = 0; is_extern = 0; current_name = NULL; decl_spec = NULL; }
#line 1515 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 5:
#line 131 "parse.y" /* yacc.c:1646  */
    { free_list(*(yyvsp[0]), NULL); *(yyvsp[0]) = NULL; }
#line 1521 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 6:
#line 135 "parse.y" /* yacc.c:1646  */
    { is_typedef = 1; }
#line 1527 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 7:
#line 136 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1533 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 8:
#line 137 "parse.y" /* yacc.c:1646  */
    { is_typedef = 1; }
#line 1539 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 9:
#line 138 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1545 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 14:
#line 143 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1551 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 15:
#line 144 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1557 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 16:
#line 149 "parse.y" /* yacc.c:1646  */
    { if (current_name) {
		    struct string_list *decl = (*(yyvsp[0]))->next;
		    (*(yyvsp[0]))->next = NULL;
		    add_symbol(current_name,
			       is_typedef ? SYM_TYPEDEF : SYM_NORMAL,
			       decl, is_extern);
		    current_name = NULL;
		  }
		  (yyval) = (yyvsp[0]);
		}
#line 1572 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 17:
#line 162 "parse.y" /* yacc.c:1646  */
    { (yyval) = NULL; }
#line 1578 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 19:
#line 168 "parse.y" /* yacc.c:1646  */
    { struct string_list *decl = *(yyvsp[0]);
		  *(yyvsp[0]) = NULL;
		  add_symbol(current_name,
			     is_typedef ? SYM_TYPEDEF : SYM_NORMAL, decl, is_extern);
		  current_name = NULL;
		  (yyval) = (yyvsp[0]);
		}
#line 1590 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 20:
#line 176 "parse.y" /* yacc.c:1646  */
    { struct string_list *decl = *(yyvsp[0]);
		  *(yyvsp[0]) = NULL;
		  free_list(*(yyvsp[-1]), NULL);
		  *(yyvsp[-1]) = decl_spec;
		  add_symbol(current_name,
			     is_typedef ? SYM_TYPEDEF : SYM_NORMAL, decl, is_extern);
		  current_name = NULL;
		  (yyval) = (yyvsp[0]);
		}
#line 1604 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 21:
#line 189 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]) ? (yyvsp[0]) : (yyvsp[-1]) ? (yyvsp[-1]) : (yyvsp[-2]) ? (yyvsp[-2]) : (yyvsp[-3]); }
#line 1610 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 22:
#line 194 "parse.y" /* yacc.c:1646  */
    { decl_spec = NULL; }
#line 1616 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 24:
#line 199 "parse.y" /* yacc.c:1646  */
    { decl_spec = *(yyvsp[0]); }
#line 1622 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 25:
#line 200 "parse.y" /* yacc.c:1646  */
    { decl_spec = *(yyvsp[0]); }
#line 1628 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 26:
#line 205 "parse.y" /* yacc.c:1646  */
    { /* Version 2 checksumming ignores storage class, as that
		     is really irrelevant to the linkage.  */
		  remove_node((yyvsp[0]));
		  (yyval) = (yyvsp[0]);
		}
#line 1638 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 31:
#line 217 "parse.y" /* yacc.c:1646  */
    { is_extern = 1; (yyval) = (yyvsp[0]); }
#line 1644 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 32:
#line 218 "parse.y" /* yacc.c:1646  */
    { is_extern = 0; (yyval) = (yyvsp[0]); }
#line 1650 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 37:
#line 230 "parse.y" /* yacc.c:1646  */
    { remove_node((yyvsp[-1])); (*(yyvsp[0]))->tag = SYM_STRUCT; (yyval) = (yyvsp[0]); }
#line 1656 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 38:
#line 232 "parse.y" /* yacc.c:1646  */
    { remove_node((yyvsp[-1])); (*(yyvsp[0]))->tag = SYM_UNION; (yyval) = (yyvsp[0]); }
#line 1662 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 39:
#line 234 "parse.y" /* yacc.c:1646  */
    { remove_node((yyvsp[-1])); (*(yyvsp[0]))->tag = SYM_ENUM; (yyval) = (yyvsp[0]); }
#line 1668 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 40:
#line 238 "parse.y" /* yacc.c:1646  */
    { record_compound((yyvsp[-2]), (yyvsp[-1]), (yyvsp[0]), SYM_STRUCT); (yyval) = (yyvsp[0]); }
#line 1674 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 41:
#line 240 "parse.y" /* yacc.c:1646  */
    { record_compound((yyvsp[-2]), (yyvsp[-1]), (yyvsp[0]), SYM_UNION); (yyval) = (yyvsp[0]); }
#line 1680 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 42:
#line 242 "parse.y" /* yacc.c:1646  */
    { record_compound((yyvsp[-2]), (yyvsp[-1]), (yyvsp[0]), SYM_ENUM); (yyval) = (yyvsp[0]); }
#line 1686 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 43:
#line 247 "parse.y" /* yacc.c:1646  */
    { add_symbol(NULL, SYM_ENUM, NULL, 0); (yyval) = (yyvsp[0]); }
#line 1692 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 44:
#line 249 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1698 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 45:
#line 250 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1704 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 56:
#line 264 "parse.y" /* yacc.c:1646  */
    { (*(yyvsp[0]))->tag = SYM_TYPEDEF; (yyval) = (yyvsp[0]); }
#line 1710 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 57:
#line 269 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]) ? (yyvsp[0]) : (yyvsp[-1]); }
#line 1716 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 58:
#line 273 "parse.y" /* yacc.c:1646  */
    { (yyval) = NULL; }
#line 1722 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 61:
#line 279 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1728 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 65:
#line 285 "parse.y" /* yacc.c:1646  */
    { /* restrict has no effect in prototypes so ignore it */
		  remove_node((yyvsp[0]));
		  (yyval) = (yyvsp[0]);
		}
#line 1737 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 66:
#line 292 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1743 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 68:
#line 298 "parse.y" /* yacc.c:1646  */
    { if (current_name != NULL) {
		    error_with_pos("unexpected second declaration name");
		    YYERROR;
		  } else {
		    current_name = (*(yyvsp[0]))->string;
		    (yyval) = (yyvsp[0]);
		  }
		}
#line 1756 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 69:
#line 307 "parse.y" /* yacc.c:1646  */
    { if (current_name != NULL) {
		    error_with_pos("unexpected second declaration name");
		    YYERROR;
		  } else {
		    current_name = (*(yyvsp[0]))->string;
		    (yyval) = (yyvsp[0]);
		  }
		}
#line 1769 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 70:
#line 316 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1775 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 71:
#line 318 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1781 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 72:
#line 320 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1787 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 73:
#line 322 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1793 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 74:
#line 328 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1799 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 78:
#line 336 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1805 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 79:
#line 338 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1811 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 80:
#line 340 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1817 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 81:
#line 342 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1823 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 82:
#line 344 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1829 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 83:
#line 348 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1835 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 85:
#line 350 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1841 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 86:
#line 354 "parse.y" /* yacc.c:1646  */
    { (yyval) = NULL; }
#line 1847 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 89:
#line 361 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1853 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 90:
#line 366 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]) ? (yyvsp[0]) : (yyvsp[-1]); }
#line 1859 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 91:
#line 371 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]) ? (yyvsp[0]) : (yyvsp[-1]); }
#line 1865 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 93:
#line 376 "parse.y" /* yacc.c:1646  */
    { (yyval) = NULL; }
#line 1871 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 94:
#line 378 "parse.y" /* yacc.c:1646  */
    { /* For version 2 checksums, we don't want to remember
		     private parameter names.  */
		  remove_node((yyvsp[0]));
		  (yyval) = (yyvsp[0]);
		}
#line 1881 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 95:
#line 386 "parse.y" /* yacc.c:1646  */
    { remove_node((yyvsp[0]));
		  (yyval) = (yyvsp[0]);
		}
#line 1889 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 96:
#line 390 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1895 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 97:
#line 392 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1901 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 98:
#line 394 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1907 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 99:
#line 396 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1913 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 100:
#line 398 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1919 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 101:
#line 403 "parse.y" /* yacc.c:1646  */
    { struct string_list *decl = *(yyvsp[-1]);
		  *(yyvsp[-1]) = NULL;
		  add_symbol(current_name, SYM_NORMAL, decl, is_extern);
		  (yyval) = (yyvsp[0]);
		}
#line 1929 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 102:
#line 411 "parse.y" /* yacc.c:1646  */
    { (yyval) = NULL; }
#line 1935 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 104:
#line 418 "parse.y" /* yacc.c:1646  */
    { remove_list((yyvsp[0]), &(*(yyvsp[-1]))->next); (yyval) = (yyvsp[0]); }
#line 1941 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 105:
#line 422 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1947 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 106:
#line 423 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1953 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 107:
#line 427 "parse.y" /* yacc.c:1646  */
    { (yyval) = NULL; }
#line 1959 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 110:
#line 433 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1965 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 111:
#line 438 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1971 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 112:
#line 440 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1977 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 113:
#line 444 "parse.y" /* yacc.c:1646  */
    { (yyval) = NULL; }
#line 1983 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 116:
#line 450 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 1989 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 117:
#line 454 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]) ? (yyvsp[0]) : (yyvsp[-1]); }
#line 1995 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 118:
#line 455 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 2001 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 120:
#line 460 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 2007 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 121:
#line 464 "parse.y" /* yacc.c:1646  */
    { (yyval) = NULL; }
#line 2013 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 123:
#line 469 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 2019 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 124:
#line 470 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 2025 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 127:
#line 479 "parse.y" /* yacc.c:1646  */
    {
			const char *name = strdup((*(yyvsp[0]))->string);
			add_symbol(name, SYM_ENUM_CONST, NULL, 0);
		}
#line 2034 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 128:
#line 484 "parse.y" /* yacc.c:1646  */
    {
			const char *name = strdup((*(yyvsp[-2]))->string);
			struct string_list *expr = copy_list_range(*(yyvsp[0]), *(yyvsp[-1]));
			add_symbol(name, SYM_ENUM_CONST, expr, 0);
		}
#line 2044 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 129:
#line 491 "parse.y" /* yacc.c:1646  */
    { (yyval) = (yyvsp[0]); }
#line 2050 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 130:
#line 495 "parse.y" /* yacc.c:1646  */
    { (yyval) = NULL; }
#line 2056 "parse.tab.c" /* yacc.c:1646  */
    break;

  case 132:
#line 501 "parse.y" /* yacc.c:1646  */
    { export_symbol((*(yyvsp[-2]))->string); (yyval) = (yyvsp[0]); }
#line 2062 "parse.tab.c" /* yacc.c:1646  */
    break;


#line 2066 "parse.tab.c" /* yacc.c:1646  */
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

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTOKENS] + *yyssp;
  if (0 <= yystate && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTOKENS];

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
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

  /* Do not reclaim the symbols of the rule whose action triggered
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
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

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
  /* Do not reclaim the symbols of the rule whose action triggered
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
  return yyresult;
}
#line 505 "parse.y" /* yacc.c:1906  */


static void
yyerror(const char *e)
{
  error_with_pos("%s", e);
}
