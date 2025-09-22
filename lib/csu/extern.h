/*
 * Copyright (c) 2004 Marc Espie <espie@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

void	__init(void) __dso_hidden;
int	main(int argc, char *argv[], char *envp[]);

typedef const void *dl_cb_cb(int);
typedef void (*initarray_f)(int, char **, char **, dl_cb_cb *);
typedef void (*init_f)(void);

/*
 * Provide default implementations of these.  Only archs with weird
 * ASM stuff (hppa, arm) need to override them
 */
#ifndef MD_DATA_SECTION_FLAGS_SYMBOL
# ifdef __LP64__
#  define VALUE_ALIGN		".balign 8"
#  define VALUE_DIRECTIVE	".quad"
# else
#  define VALUE_ALIGN		".balign 4"
#  define VALUE_DIRECTIVE	".int"
# endif
# define MD_DATA_SECTION_FLAGS_SYMBOL(section, flags, type, symbol)	\
	extern __dso_hidden type symbol[];				\
	__asm("	.section "section",\""flags"\",@progbits		\n" \
	"	"VALUE_ALIGN"						\n" \
	#symbol":							\n" \
	"	.previous")
# define MD_DATA_SECTION_SYMBOL_VALUE(section, type, symbol, value)	\
	extern __dso_hidden type symbol[];				\
	__asm("	.section "section",\"aw\",@progbits			\n" \
	"	"VALUE_ALIGN"						\n" \
	#symbol":							\n" \
	"	"VALUE_DIRECTIVE" "#value"				\n" \
	"	.previous")
# define MD_DATA_SECTION_FLAGS_VALUE(section, flags, value)		\
	__asm("	.section "section",\""flags"\",@progbits		\n" \
	"	"VALUE_ALIGN"						\n" \
	"	"VALUE_DIRECTIVE" "#value"				\n" \
	"	.previous")
#endif
