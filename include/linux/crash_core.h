/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_CRASH_CORE_H
#define LINUX_CRASH_CORE_H

#include <linux/linkage.h>
#include <linux/elfcore.h>
#include <linux/elf.h>

/* Alignment required for elf header segment */
#define ELF_CORE_HEADER_ALIGN   4096

struct crash_mem {
	unsigned int max_nr_ranges;
	unsigned int nr_ranges;
	struct range ranges[] __counted_by(max_nr_ranges);
};

extern int crash_exclude_mem_range(struct crash_mem *mem,
				   unsigned long long mstart,
				   unsigned long long mend);
extern int crash_prepare_elf64_headers(struct crash_mem *mem, int need_kernel_map,
				       void **addr, unsigned long *sz);

struct kimage;
struct kexec_segment;

#define KEXEC_CRASH_HP_NONE			0
#define KEXEC_CRASH_HP_ADD_CPU			1
#define KEXEC_CRASH_HP_REMOVE_CPU		2
#define KEXEC_CRASH_HP_ADD_MEMORY		3
#define KEXEC_CRASH_HP_REMOVE_MEMORY		4
#define KEXEC_CRASH_HP_INVALID_CPU		-1U

#endif /* LINUX_CRASH_CORE_H */
