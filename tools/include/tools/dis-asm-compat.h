/* SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause */
#ifndef _TOOLS_DIS_ASM_COMPAT_H
#define _TOOLS_DIS_ASM_COMPAT_H

#include <stdio.h>
#include <dis-asm.h>

/* define types for older binutils version, to centralize ifdef'ery a bit */
#ifndef DISASM_INIT_STYLED
enum disassembler_style {DISASSEMBLER_STYLE_NOT_EMPTY};
typedef int (*fprintf_styled_ftype) (void *, enum disassembler_style, const char*, ...);
#endif

/*
 * Trivial fprintf wrapper to be used as the fprintf_styled_func argument to
 * init_disassemble_info_compat() when normal fprintf suffices.
 */
static inline int fprintf_styled(void *out,
				 enum disassembler_style style,
				 const char *fmt, ...)
{
	va_list args;
	int r;

	(void)style;

	va_start(args, fmt);
	r = vfprintf(out, fmt, args);
	va_end(args);

	return r;
}

/*
 * Wrapper for init_disassemble_info() that hides version
 * differences. Depending on binutils version and architecture either
 * fprintf_func or fprintf_styled_func will be called.
 */
static inline void init_disassemble_info_compat(struct disassemble_info *info,
						void *stream,
						fprintf_ftype unstyled_func,
						fprintf_styled_ftype styled_func)
{
#ifdef DISASM_INIT_STYLED
	init_disassemble_info(info, stream,
			      unstyled_func,
			      styled_func);
#else
	(void)styled_func;
	init_disassemble_info(info, stream,
			      unstyled_func);
#endif
}

#endif /* _TOOLS_DIS_ASM_COMPAT_H */
