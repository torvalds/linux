/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _OBJTOOL_CHECKSUM_H
#define _OBJTOOL_CHECKSUM_H

#include <objtool/elf.h>

#ifdef BUILD_KLP

static inline void checksum_init(struct symbol *func)
{
	if (func && !func->csum.state) {
		func->csum.state = XXH3_createState();
		XXH3_64bits_reset(func->csum.state);
	}
}

static inline void checksum_update(struct symbol *func,
				   struct instruction *insn,
				   const void *data, size_t size)
{
	XXH3_64bits_update(func->csum.state, data, size);
	dbg_checksum(func, insn, XXH3_64bits_digest(func->csum.state));
}

static inline void checksum_finish(struct symbol *func)
{
	if (func && func->csum.state) {
		func->csum.checksum = XXH3_64bits_digest(func->csum.state);
		func->csum.state = NULL;
	}
}

#else /* !BUILD_KLP */

static inline void checksum_init(struct symbol *func) {}
static inline void checksum_update(struct symbol *func,
				   struct instruction *insn,
				   const void *data, size_t size) {}
static inline void checksum_finish(struct symbol *func) {}

#endif /* !BUILD_KLP */

#endif /* _OBJTOOL_CHECKSUM_H */
