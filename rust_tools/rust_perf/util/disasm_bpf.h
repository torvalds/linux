// SPDX-License-Identifier: GPL-2.0-only

#ifndef __PERF_DISASM_BPF_H
#define __PERF_DISASM_BPF_H

struct symbol;
struct annotate_args;

int symbol__disassemble_bpf(struct symbol *sym, struct annotate_args *args);
int symbol__disassemble_bpf_image(struct symbol *sym, struct annotate_args *args);

#endif /* __PERF_DISASM_BPF_H */
