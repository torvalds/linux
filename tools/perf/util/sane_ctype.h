/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PERF_SANE_CTYPE_H
#define _PERF_SANE_CTYPE_H

extern const char *graph_dotted_line;
extern const char *dots;

/* Sane ctype - no locale, and works with signed chars */
#undef isascii
#undef isspace
#undef isdigit
#undef isxdigit
#undef isalpha
#undef isprint
#undef isalnum
#undef islower
#undef isupper
#undef tolower
#undef toupper

extern unsigned char sane_ctype[256];
#define GIT_SPACE		0x01
#define GIT_DIGIT		0x02
#define GIT_ALPHA		0x04
#define GIT_GLOB_SPECIAL	0x08
#define GIT_REGEX_SPECIAL	0x10
#define GIT_PRINT_EXTRA		0x20
#define GIT_PRINT		0x3E
#define sane_istest(x,mask) ((sane_ctype[(unsigned char)(x)] & (mask)) != 0)
#define isascii(x) (((x) & ~0x7f) == 0)
#define isspace(x) sane_istest(x,GIT_SPACE)
#define isdigit(x) sane_istest(x,GIT_DIGIT)
#define isxdigit(x)	\
	(sane_istest(toupper(x), GIT_ALPHA | GIT_DIGIT) && toupper(x) < 'G')
#define isalpha(x) sane_istest(x,GIT_ALPHA)
#define isalnum(x) sane_istest(x,GIT_ALPHA | GIT_DIGIT)
#define isprint(x) sane_istest(x,GIT_PRINT)
#define islower(x) (sane_istest(x,GIT_ALPHA) && (x & 0x20))
#define isupper(x) (sane_istest(x,GIT_ALPHA) && !(x & 0x20))
#define tolower(x) sane_case((unsigned char)(x), 0x20)
#define toupper(x) sane_case((unsigned char)(x), 0)

static inline int sane_case(int x, int high)
{
	if (sane_istest(x, GIT_ALPHA))
		x = (x & ~0x20) | high;
	return x;
}

#endif /* _PERF_SANE_CTYPE_H */
