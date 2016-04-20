#ifndef __BPF_LOAD_H
#define __BPF_LOAD_H

#define MAX_MAPS 32
#define MAX_PROGS 32

extern int map_fd[MAX_MAPS];
extern int prog_fd[MAX_PROGS];
extern int event_fd[MAX_PROGS];

/* parses elf file compiled by llvm .c->.o
 * . parses 'maps' section and creates maps via BPF syscall
 * . parses 'license' section and passes it to syscall
 * . parses elf relocations for BPF maps and adjusts BPF_LD_IMM64 insns by
 *   storing map_fd into insn->imm and marking such insns as BPF_PSEUDO_MAP_FD
 * . loads eBPF programs via BPF syscall
 *
 * One ELF file can contain multiple BPF programs which will be loaded
 * and their FDs stored stored in prog_fd array
 *
 * returns zero on success
 */
int load_bpf_file(char *path);

void read_trace_pipe(void);
struct ksym {
	long addr;
	char *name;
};

int load_kallsyms(void);
struct ksym *ksym_search(long key);
#endif
