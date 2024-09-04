/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */

#ifndef __DISASM_HELPERS_H
#define __DISASM_HELPERS_H

#include <stdlib.h>

struct bpf_insn;

struct bpf_insn *disasm_insn(struct bpf_insn *insn, char *buf, size_t buf_sz);

#endif /* __DISASM_HELPERS_H */
