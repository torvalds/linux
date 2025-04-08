/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2015 Josh Poimboeuf <jpoimboe@redhat.com>
 */

#ifndef _WARN_H
#define _WARN_H

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <objtool/builtin.h>
#include <objtool/elf.h>

extern const char *objname;

static inline char *offstr(struct section *sec, unsigned long offset)
{
	bool is_text = (sec->sh.sh_flags & SHF_EXECINSTR);
	struct symbol *sym = NULL;
	char *str;
	int len;

	if (is_text)
		sym = find_func_containing(sec, offset);
	if (!sym)
		sym = find_symbol_containing(sec, offset);

	if (sym) {
		str = malloc(strlen(sym->name) + strlen(sec->name) + 40);
		len = sprintf(str, "%s+0x%lx", sym->name, offset - sym->offset);
		if (opts.sec_address)
			sprintf(str+len, " (%s+0x%lx)", sec->name, offset);
	} else {
		str = malloc(strlen(sec->name) + 20);
		sprintf(str, "%s+0x%lx", sec->name, offset);
	}

	return str;
}

#define ___WARN(severity, extra, format, ...)				\
	fprintf(stderr,							\
		"%s%s%s: objtool" extra ": " format "\n",		\
		objname ?: "",						\
		objname ? ": " : "",					\
		severity,						\
		##__VA_ARGS__)

#define __WARN(severity, format, ...)					\
	___WARN(severity, "", format, ##__VA_ARGS__)

#define __WARN_LINE(severity, format, ...)				\
	___WARN(severity, " [%s:%d]", format, __FILE__, __LINE__, ##__VA_ARGS__)

#define __WARN_ELF(severity, format, ...)				\
	__WARN_LINE(severity, "%s: " format " failed: %s", __func__, ##__VA_ARGS__, elf_errmsg(-1))

#define __WARN_GLIBC(severity, format, ...)				\
	__WARN_LINE(severity, "%s: " format " failed: %s", __func__, ##__VA_ARGS__, strerror(errno))

#define __WARN_FUNC(severity, sec, offset, format, ...)			\
({									\
	char *_str = offstr(sec, offset);				\
	__WARN(severity, "%s: " format, _str, ##__VA_ARGS__);		\
	free(_str);							\
})

#define WARN_STR (opts.werror ? "error" : "warning")

#define WARN(format, ...) __WARN(WARN_STR, format, ##__VA_ARGS__)
#define WARN_FUNC(sec, offset, format, ...) __WARN_FUNC(WARN_STR, sec, offset, format, ##__VA_ARGS__)

#define WARN_INSN(insn, format, ...)					\
({									\
	struct instruction *_insn = (insn);				\
	if (!_insn->sym || !_insn->sym->warned)				\
		WARN_FUNC(_insn->sec, _insn->offset, format,		\
			  ##__VA_ARGS__);				\
	if (_insn->sym)							\
		_insn->sym->warned = 1;					\
})

#define BT_INSN(insn, format, ...)				\
({								\
	if (opts.verbose || opts.backtrace) {			\
		struct instruction *_insn = (insn);		\
		char *_str = offstr(_insn->sec, _insn->offset); \
		WARN("  %s: " format, _str, ##__VA_ARGS__);	\
		free(_str);					\
	}							\
})

#define ERROR_STR "error"

#define ERROR(format, ...) __WARN(ERROR_STR, format, ##__VA_ARGS__)
#define ERROR_ELF(format, ...) __WARN_ELF(ERROR_STR, format, ##__VA_ARGS__)
#define ERROR_GLIBC(format, ...) __WARN_GLIBC(ERROR_STR, format, ##__VA_ARGS__)
#define ERROR_FUNC(sec, offset, format, ...) __WARN_FUNC(ERROR_STR, sec, offset, format, ##__VA_ARGS__)
#define ERROR_INSN(insn, format, ...) WARN_FUNC(insn->sec, insn->offset, format, ##__VA_ARGS__)

#endif /* _WARN_H */
