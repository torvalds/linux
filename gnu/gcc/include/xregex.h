/* This file redefines all regex external names before including
   a renamed copy of glibc's regex.h.  */

#ifndef _XREGEX_H
#define _XREGEX_H 1

#  define regfree xregfree 
#  define regexec xregexec
#  define regcomp xregcomp
#  define regerror xregerror
#  define re_set_registers xre_set_registers
#  define re_match_2 xre_match_2
#  define re_match xre_match
#  define re_search xre_search
#  define re_compile_pattern xre_compile_pattern
#  define re_set_syntax xre_set_syntax
#  define re_search_2 xre_search_2
#  define re_compile_fastmap xre_compile_fastmap
#  define re_syntax_options xre_syntax_options
#  define re_max_failures xre_max_failures

#  define _REGEX_RE_COMP
#  define re_comp xre_comp
#  define re_exec xre_exec

#include "xregex2.h"

#endif /* xregex.h */
