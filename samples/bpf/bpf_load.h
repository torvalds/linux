/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BPF_LOAD_H
#define __BPF_LOAD_H

#include <bpf/bpf.h>

#define MAX_MAPS 32
#define MAX_PROGS 32

struct bpf_load_map_def {
	unsigned int type;
	unsigned int key_size;
	unsigned int value_size;
	unsigned int max_entries;
	unsigned int map_flags;
	unsigned int inner_map_idx;
	unsigned int numa_node;
};

struct bpf_map_data {
	int fd;
	char *name;
	size_t elf_offset;
	struct bpf_load_map_def def;
};

typedef void (*fixup_map_cb)(struct bpf_map_data *map, int idx);

extern int prog_fd[MAX_PROGS];
extern int event_fd[MAX_PROGS];
extern char bpf_log_buf[BPF_LOG_BUF_SIZE];
extern int prog_cnt;

/* There is a one-to-one mapping between map_fd[] and map_data[].
 * The map_data[] just contains more rich info on the given map.
 */
extern int map_fd[MAX_MAPS];
extern struct bpf_map_data map_data[MAX_MAPS];
extern int map_data_count;

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
int load_bpf_file_fixup_map(const char *path, fixup_map_cb fixup_map);

void read_trace_pipe(void);
int bpf_set_link_xdp_fd(int ifindex, int fd, __u32 flags);
#endif
