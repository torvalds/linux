/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */

#ifndef __JIT_DISASM_HELPERS_H
#define __JIT_DISASM_HELPERS_H

#include <stddef.h>

int get_jited_program_text(int fd, char *text, size_t text_sz);

#endif /* __JIT_DISASM_HELPERS_H */
