/* ANSI-C code produced by gperf version 2.7.2 */
/* Command-line: gperf -L ANSI-C -a -C -E -g -H is_reserved_hash -k '1,3,$' -N is_reserved_word -p -t scripts/genksyms/keywords.gperf  */
struct resword { const char *name; int token; };
/* maximum key range = 109, duplicates = 0 */

#ifdef __GNUC__
__inline
#else
#ifdef __cplusplus
inline
#endif
#endif
static unsigned int
is_reserved_hash (register const char *str, register unsigned int len)
{
  static const unsigned char asso_values[] =
    {
      113, 113, 113, 113, 113, 113, 113, 113, 113, 113,
      113, 113, 113, 113, 113, 113, 113, 113, 113, 113,
      113, 113, 113, 113, 113, 113, 113, 113, 113, 113,
      113, 113, 113, 113, 113, 113, 113, 113, 113, 113,
      113, 113, 113, 113, 113, 113, 113, 113, 113, 113,
      113, 113, 113, 113, 113, 113, 113, 113, 113, 113,
      113, 113, 113, 113, 113, 113, 113, 113, 113,   5,
      113, 113, 113, 113, 113, 113,   0, 113, 113, 113,
        0, 113, 113, 113, 113, 113, 113, 113, 113, 113,
      113, 113, 113, 113, 113,   0, 113,   0, 113,  20,
       25,   0,  35,  30, 113,  20, 113, 113,  40,  30,
       30,   0,   0, 113,   0,  51,   0,  15,   5, 113,
      113, 113, 113, 113, 113, 113, 113, 113, 113, 113,
      113, 113, 113, 113, 113, 113, 113, 113, 113, 113,
      113, 113, 113, 113, 113, 113, 113, 113, 113, 113,
      113, 113, 113, 113, 113, 113, 113, 113, 113, 113,
      113, 113, 113, 113, 113, 113, 113, 113, 113, 113,
      113, 113, 113, 113, 113, 113, 113, 113, 113, 113,
      113, 113, 113, 113, 113, 113, 113, 113, 113, 113,
      113, 113, 113, 113, 113, 113, 113, 113, 113, 113,
      113, 113, 113, 113, 113, 113, 113, 113, 113, 113,
      113, 113, 113, 113, 113, 113, 113, 113, 113, 113,
      113, 113, 113, 113, 113, 113, 113, 113, 113, 113,
      113, 113, 113, 113, 113, 113, 113, 113, 113, 113,
      113, 113, 113, 113, 113, 113, 113, 113, 113, 113,
      113, 113, 113, 113, 113, 113
    };
  return len + asso_values[(unsigned char)str[2]] + asso_values[(unsigned char)str[0]] + asso_values[(unsigned char)str[len - 1]];
}

#ifdef __GNUC__
__inline
#endif
const struct resword *
is_reserved_word (register const char *str, register unsigned int len)
{
  enum
    {
      TOTAL_KEYWORDS = 41,
      MIN_WORD_LENGTH = 3,
      MAX_WORD_LENGTH = 17,
      MIN_HASH_VALUE = 4,
      MAX_HASH_VALUE = 112
    };

  static const struct resword wordlist[] =
    {
      {""}, {""}, {""}, {""},
      {"auto", AUTO_KEYW},
      {""}, {""},
      {"__asm__", ASM_KEYW},
      {""},
      {"_restrict", RESTRICT_KEYW},
      {"__typeof__", TYPEOF_KEYW},
      {"__attribute", ATTRIBUTE_KEYW},
      {"__restrict__", RESTRICT_KEYW},
      {"__attribute__", ATTRIBUTE_KEYW},
      {""},
      {"__volatile", VOLATILE_KEYW},
      {""},
      {"__volatile__", VOLATILE_KEYW},
      {"EXPORT_SYMBOL", EXPORT_SYMBOL_KEYW},
      {""}, {""}, {""},
      {"EXPORT_SYMBOL_GPL", EXPORT_SYMBOL_KEYW},
      {"int", INT_KEYW},
      {"char", CHAR_KEYW},
      {""}, {""},
      {"__const", CONST_KEYW},
      {"__inline", INLINE_KEYW},
      {"__const__", CONST_KEYW},
      {"__inline__", INLINE_KEYW},
      {""}, {""}, {""}, {""},
      {"__asm", ASM_KEYW},
      {"extern", EXTERN_KEYW},
      {""},
      {"register", REGISTER_KEYW},
      {""},
      {"float", FLOAT_KEYW},
      {"typeof", TYPEOF_KEYW},
      {"typedef", TYPEDEF_KEYW},
      {""}, {""},
      {"_Bool", BOOL_KEYW},
      {"double", DOUBLE_KEYW},
      {""}, {""},
      {"enum", ENUM_KEYW},
      {""}, {""}, {""},
      {"volatile", VOLATILE_KEYW},
      {"void", VOID_KEYW},
      {"const", CONST_KEYW},
      {"short", SHORT_KEYW},
      {"struct", STRUCT_KEYW},
      {""},
      {"restrict", RESTRICT_KEYW},
      {""},
      {"__signed__", SIGNED_KEYW},
      {""},
      {"asm", ASM_KEYW},
      {""}, {""},
      {"inline", INLINE_KEYW},
      {""}, {""}, {""},
      {"union", UNION_KEYW},
      {""}, {""}, {""}, {""}, {""}, {""},
      {"static", STATIC_KEYW},
      {""}, {""}, {""}, {""}, {""}, {""},
      {"__signed", SIGNED_KEYW},
      {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
      {""}, {""}, {""}, {""}, {""},
      {"unsigned", UNSIGNED_KEYW},
      {""}, {""}, {""}, {""},
      {"long", LONG_KEYW},
      {""}, {""}, {""}, {""}, {""}, {""}, {""},
      {"signed", SIGNED_KEYW}
    };

  if (len <= MAX_WORD_LENGTH && len >= MIN_WORD_LENGTH)
    {
      register int key = is_reserved_hash (str, len);

      if (key <= MAX_HASH_VALUE && key >= 0)
        {
          register const char *s = wordlist[key].name;

          if (*str == *s && !strcmp (str + 1, s + 1))
            return &wordlist[key];
        }
    }
  return 0;
}
