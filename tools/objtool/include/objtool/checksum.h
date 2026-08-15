/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _OBJTOOL_CHECKSUM_H
#define _OBJTOOL_CHECKSUM_H

#include <objtool/elf.h>

#ifdef BUILD_KLP

static inline void checksum_init(struct symbol *sym)
{
	if (sym && !sym->csum.state) {
		sym->csum.state = XXH3_createState();
		XXH3_64bits_reset(sym->csum.state);
	}
}

static inline void __checksum_update(struct symbol *sym, const void *data,
				     size_t size)
{
	XXH3_64bits_update(sym->csum.state, data, size);
}

static inline void __checksum_update_insn(struct symbol *sym,
					  struct instruction *insn,
					  const void *data, size_t size)
{
	__checksum_update(sym, data, size);
	dbg_checksum_insn(sym, insn, XXH3_64bits_digest(sym->csum.state));
}

static inline void __checksum_update_object(struct symbol *sym,
					    unsigned long offset,
					    const char *what, const void *data,
					    size_t size)
{
	__checksum_update(sym, &offset, sizeof(offset));
	__checksum_update(sym, data, size);
	dbg_checksum_object(sym, offset, what, XXH3_64bits_digest(sym->csum.state));
}

static inline void checksum_finish(struct symbol *sym)
{
	if (sym && sym->csum.state) {
		sym->csum.checksum = XXH3_64bits_digest(sym->csum.state);
		XXH3_freeState(sym->csum.state);
		sym->csum.state = NULL;
	}
}

int calculate_checksums(struct objtool_file *file);
int create_sym_checksum_section(struct objtool_file *file);

#else /* !BUILD_KLP */

static inline int calculate_checksums(struct objtool_file *file) { return -ENOSYS; }
static inline int create_sym_checksum_section(struct objtool_file *file) { return -EINVAL; }

#endif /* !BUILD_KLP */

#endif /* _OBJTOOL_CHECKSUM_H */
