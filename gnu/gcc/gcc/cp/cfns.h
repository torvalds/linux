/* ANSI-C code produced by gperf version 3.0.1 */
/* Command-line: gperf -o -C -E -k '1-6,$' -j1 -D -N libc_name_p -L ANSI-C ../../gcc/gcc/cp/cfns.gperf  */

#if !((' ' == 32) && ('!' == 33) && ('"' == 34) && ('#' == 35) \
      && ('%' == 37) && ('&' == 38) && ('\'' == 39) && ('(' == 40) \
      && (')' == 41) && ('*' == 42) && ('+' == 43) && (',' == 44) \
      && ('-' == 45) && ('.' == 46) && ('/' == 47) && ('0' == 48) \
      && ('1' == 49) && ('2' == 50) && ('3' == 51) && ('4' == 52) \
      && ('5' == 53) && ('6' == 54) && ('7' == 55) && ('8' == 56) \
      && ('9' == 57) && (':' == 58) && (';' == 59) && ('<' == 60) \
      && ('=' == 61) && ('>' == 62) && ('?' == 63) && ('A' == 65) \
      && ('B' == 66) && ('C' == 67) && ('D' == 68) && ('E' == 69) \
      && ('F' == 70) && ('G' == 71) && ('H' == 72) && ('I' == 73) \
      && ('J' == 74) && ('K' == 75) && ('L' == 76) && ('M' == 77) \
      && ('N' == 78) && ('O' == 79) && ('P' == 80) && ('Q' == 81) \
      && ('R' == 82) && ('S' == 83) && ('T' == 84) && ('U' == 85) \
      && ('V' == 86) && ('W' == 87) && ('X' == 88) && ('Y' == 89) \
      && ('Z' == 90) && ('[' == 91) && ('\\' == 92) && (']' == 93) \
      && ('^' == 94) && ('_' == 95) && ('a' == 97) && ('b' == 98) \
      && ('c' == 99) && ('d' == 100) && ('e' == 101) && ('f' == 102) \
      && ('g' == 103) && ('h' == 104) && ('i' == 105) && ('j' == 106) \
      && ('k' == 107) && ('l' == 108) && ('m' == 109) && ('n' == 110) \
      && ('o' == 111) && ('p' == 112) && ('q' == 113) && ('r' == 114) \
      && ('s' == 115) && ('t' == 116) && ('u' == 117) && ('v' == 118) \
      && ('w' == 119) && ('x' == 120) && ('y' == 121) && ('z' == 122) \
      && ('{' == 123) && ('|' == 124) && ('}' == 125) && ('~' == 126))
/* The character set is not based on ISO-646.  */
#error "gperf generated tables don't work with this execution character set. Please report a bug to <bug-gnu-gperf@gnu.org>."
#endif

#line 1 "../../gcc/gcc/cp/cfns.gperf"

#ifdef __GNUC__
__inline
#endif
static unsigned int hash (const char *, unsigned int);
#ifdef __GNUC__
__inline
#endif
const char * libc_name_p (const char *, unsigned int);
/* maximum key range = 391, duplicates = 0 */

#ifdef __GNUC__
__inline
#else
#ifdef __cplusplus
inline
#endif
#endif
static unsigned int
hash (register const char *str, register unsigned int len)
{
  static const unsigned short asso_values[] =
    {
      400, 400, 400, 400, 400, 400, 400, 400, 400, 400,
      400, 400, 400, 400, 400, 400, 400, 400, 400, 400,
      400, 400, 400, 400, 400, 400, 400, 400, 400, 400,
      400, 400, 400, 400, 400, 400, 400, 400, 400, 400,
      400, 400, 400, 400, 400, 400, 400, 400,   0,   0,
	1, 400, 400, 400, 400, 400, 400, 400, 400, 400,
      400, 400, 400, 400, 400, 400, 400, 400, 400, 400,
      400, 400, 400, 400, 400, 400, 400, 400, 400, 400,
      400, 400, 400, 400, 400, 400, 400, 400, 400, 400,
      400, 400, 400, 400, 400, 400, 400,  28,  90,   0,
       95,   0,  51,  93, 114,  26, 109, 124,   5,   1,
	6,  13,  37, 128,   3,   0,   0,  49,  38,   0,
      104,  45,   0, 400, 400, 400, 400, 400, 400, 400,
      400, 400, 400, 400, 400, 400, 400, 400, 400, 400,
      400, 400, 400, 400, 400, 400, 400, 400, 400, 400,
      400, 400, 400, 400, 400, 400, 400, 400, 400, 400,
      400, 400, 400, 400, 400, 400, 400, 400, 400, 400,
      400, 400, 400, 400, 400, 400, 400, 400, 400, 400,
      400, 400, 400, 400, 400, 400, 400, 400, 400, 400,
      400, 400, 400, 400, 400, 400, 400, 400, 400, 400,
      400, 400, 400, 400, 400, 400, 400, 400, 400, 400,
      400, 400, 400, 400, 400, 400, 400, 400, 400, 400,
      400, 400, 400, 400, 400, 400, 400, 400, 400, 400,
      400, 400, 400, 400, 400, 400, 400, 400, 400, 400,
      400, 400, 400, 400, 400, 400, 400, 400, 400, 400,
      400, 400, 400, 400, 400, 400, 400
    };
  register int hval = len;

  switch (hval)
    {
      default:
	hval += asso_values[(unsigned char)str[5]+1];
      /*FALLTHROUGH*/
      case 5:
	hval += asso_values[(unsigned char)str[4]];
      /*FALLTHROUGH*/
      case 4:
	hval += asso_values[(unsigned char)str[3]];
      /*FALLTHROUGH*/
      case 3:
	hval += asso_values[(unsigned char)str[2]];
      /*FALLTHROUGH*/
      case 2:
	hval += asso_values[(unsigned char)str[1]];
      /*FALLTHROUGH*/
      case 1:
	hval += asso_values[(unsigned char)str[0]];
	break;
    }
  return hval + asso_values[(unsigned char)str[len - 1]];
}

#ifdef __GNUC__
__inline
#endif
const char *
libc_name_p (register const char *str, register unsigned int len)
{
  enum
    {
      TOTAL_KEYWORDS = 156,
      MIN_WORD_LENGTH = 3,
      MAX_WORD_LENGTH = 10,
      MIN_HASH_VALUE = 9,
      MAX_HASH_VALUE = 399
    };

  static const char * const wordlist[] =
    {
      "wcsstr",
      "strstr",
      "cos",
      "towctrans",
      "memmove",
      "wcstol",
      "wcscoll",
      "wcstombs",
      "strtol",
      "strcoll",
      "wcslen",
      "time",
      "ctime",
      "strlen",
      "iswctype",
      "wmemchr",
      "wcsrchr",
      "ceil",
      "sin",
      "strrchr",
      "tan",
      "iscntrl",
      "acos",
      "wmemmove",
      "wcsrtombs",
      "wctrans",
      "wmemcmp",
      "pow",
      "atol",
      "wcsncmp",
      "memset",
      "free",
      "strncmp",
      "wmemset",
      "wcsspn",
      "wcstoul",
      "strspn",
      "strtoul",
      "asctime",
      "atan2",
      "asin",
      "atan",
      "ferror",
      "iswalnum",
      "wcscat",
      "realloc",
      "strcat",
      "wcscpy",
      "memcpy",
      "strcpy",
      "tolower",
      "floor",
      "iswcntrl",
      "atoi",
      "clearerr",
      "swscanf",
      "wcsncat",
      "islower",
      "strncat",
      "btowc",
      "localtime",
      "wctomb",
      "isalnum",
      "isprint",
      "mblen",
      "wcstod",
      "log10",
      "strtod",
      "wcrtomb",
      "abs",
      "setlocale",
      "wcschr",
      "mbrlen",
      "memchr",
      "strchr",
      "labs",
      "iswpunct",
      "exit",
      "sqrt",
      "swprintf",
      "wctype",
      "mbsrtowcs",
      "wcscspn",
      "getenv",
      "strcspn",
      "towlower",
      "atof",
      "wcstok",
      "localeconv",
      "strtok",
      "calloc",
      "malloc",
      "isalpha",
      "iswlower",
      "iswspace",
      "wcsxfrm",
      "signal",
      "strxfrm",
      "wcsftime",
      "feof",
      "strftime",
      "wcscmp",
      "fabs",
      "memcmp",
      "strcmp",
      "vsprintf",
      "fwide",
      "gmtime",
      "sprintf",
      "exp",
      "wmemcpy",
      "iswprint",
      "sscanf",
      "wcsncpy",
      "strncpy",
      "isspace",
      "toupper",
      "wctob",
      "div",
      "mbtowc",
      "ldiv",
      "log",
      "mktime",
      "isupper",
      "atexit",
      "modf",
      "mbstowcs",
      "mbrtowc",
      "ispunct",
      "iswalpha",
      "setvbuf",
      "rand",
      "srand",
      "frexp",
      "towupper",
      "mbsinit",
      "cosh",
      "vswprintf",
      "iswupper",
      "wcspbrk",
      "fmod",
      "strpbrk",
      "sinh",
      "tanh",
      "iswdigit",
      "clock",
      "longjmp",
      "ldexp",
      "setbuf",
      "fseek",
      "iswgraph",
      "difftime",
      "iswxdigit",
      "isdigit",
      "isxdigit",
      "isgraph"
    };

  static const short lookup[] =
    {
       -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,   0,
       -1,  -1,   1,  -1,  -1,  -1,   2,  -1,  -1,  -1,
       -1,  -1,   3,   4,  -1,   5,   6,   7,   8,   9,
       10,  11,  12,  13,  14,  -1,  -1,  -1,  15,  16,
       17,  18,  19,  20,  21,  22,  -1,  -1,  23,  24,
       -1,  25,  26,  27,  -1,  28,  29,  30,  31,  32,
       33,  -1,  34,  35,  -1,  36,  37,  38,  -1,  39,
       40,  -1,  41,  -1,  -1,  -1,  -1,  -1,  -1,  42,
       -1,  43,  -1,  44,  -1,  45,  46,  -1,  47,  -1,
       48,  49,  50,  51,  52,  -1,  -1,  53,  54,  55,
       -1,  -1,  -1,  56,  -1,  57,  58,  -1,  59,  60,
       61,  62,  63,  64,  65,  -1,  66,  67,  -1,  68,
       -1,  69,  70,  71,  72,  73,  74,  75,  -1,  -1,
       -1,  -1,  -1,  76,  77,  78,  -1,  -1,  79,  80,
       81,  82,  -1,  83,  84,  -1,  85,  86,  87,  -1,
       88,  89,  90,  91,  -1,  -1,  -1,  92,  -1,  93,
       -1,  94,  -1,  95,  -1,  96,  97,  -1,  98,  -1,
       99, 100, 101, 102, 103, 104, 105, 106, 107, 108,
       -1, 109, 110, 111, 112,  -1, 113,  -1,  -1, 114,
       -1,  -1,  -1, 115,  -1,  -1,  -1, 116, 117,  -1,
      118,  -1,  -1,  -1,  -1, 119, 120, 121,  -1, 122,
      123,  -1,  -1, 124,  -1, 125, 126,  -1, 127,  -1,
      128,  -1,  -1, 129, 130,  -1,  -1,  -1,  -1,  -1,
       -1, 131, 132,  -1,  -1,  -1,  -1, 133, 134, 135,
       -1,  -1,  -1,  -1,  -1, 136,  -1, 137,  -1,  -1,
       -1, 138,  -1,  -1,  -1,  -1,  -1,  -1, 139, 140,
       -1, 141,  -1,  -1, 142,  -1, 143,  -1,  -1, 144,
       -1, 145,  -1,  -1,  -1,  -1, 146,  -1,  -1,  -1,
       -1,  -1,  -1, 147,  -1,  -1,  -1,  -1,  -1, 148,
       -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
       -1,  -1,  -1,  -1, 149,  -1,  -1,  -1,  -1,  -1,
       -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
       -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
       -1,  -1,  -1,  -1, 150,  -1,  -1,  -1,  -1,  -1,
      151,  -1,  -1, 152,  -1,  -1,  -1,  -1,  -1,  -1,
       -1,  -1,  -1,  -1,  -1,  -1, 153,  -1,  -1,  -1,
       -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
       -1,  -1,  -1, 154,  -1,  -1,  -1,  -1,  -1,  -1,
       -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
       -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1, 155
    };

  if (len <= MAX_WORD_LENGTH && len >= MIN_WORD_LENGTH)
    {
      register int key = hash (str, len);

      if (key <= MAX_HASH_VALUE && key >= 0)
	{
	  register int index = lookup[key];

	  if (index >= 0)
	    {
	      register const char *s = wordlist[index];

	      if (*str == *s && !strcmp (str + 1, s + 1))
		return s;
	    }
	}
    }
  return 0;
}
