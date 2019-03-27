/* $FreeBSD$ */
#ifndef _REGEX_H

#ifndef __USE_GNU
#define	__USE_GNU
#endif

#include <posix/regex.h>

/* Document internal interfaces.  */
extern reg_syntax_t __re_set_syntax (reg_syntax_t __syntax);

extern const char *__re_compile_pattern (const char *__pattern, size_t __length,
					struct re_pattern_buffer *__buffer);

extern int __re_compile_fastmap (struct re_pattern_buffer *__buffer);

extern int __re_search (struct re_pattern_buffer *__buffer, const char *__string,
		       int __length, int __start, int __range,
		       struct re_registers *__regs);

extern int __re_search_2 (struct re_pattern_buffer *__buffer,
			 const char *__string1, int __length1,
			 const char *__string2, int __length2, int __start,
			 int __range, struct re_registers *__regs, int __stop);

extern int __re_match (struct re_pattern_buffer *__buffer, const char *__string,
		      int __length, int __start, struct re_registers *__regs);

extern int __re_match_2 (struct re_pattern_buffer *__buffer,
			const char *__string1, int __length1,
			const char *__string2, int __length2, int __start,
			struct re_registers *__regs, int __stop);

extern void __re_set_registers (struct re_pattern_buffer *__buffer,
			       struct re_registers *__regs,
			       unsigned int __num_regs,
			       regoff_t *__starts, regoff_t *__ends);

extern int __regcomp (regex_t *__restrict __preg,
		     const char *__restrict __pattern,
		     int __cflags);

extern int __regexec (const regex_t *__restrict __preg,
		     const char *__restrict __string, size_t __nmatch,
		     regmatch_t __pmatch[__restrict_arr],
		     int __eflags);

extern size_t __regerror (int __errcode, const regex_t *__restrict __preg,
			 char *__restrict __errbuf, size_t __errbuf_size);

extern void __regfree (regex_t *__preg);

#endif	/* _REGEX_H */
