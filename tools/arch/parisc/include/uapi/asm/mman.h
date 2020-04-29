/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef TOOLS_ARCH_PARISC_UAPI_ASM_MMAN_FIX_H
#define TOOLS_ARCH_PARISC_UAPI_ASM_MMAN_FIX_H
#define MADV_DODUMP	70
#define MADV_DOFORK	11
#define MADV_DONTDUMP   69
#define MADV_DONTFORK	10
#define MADV_DONTNEED   4
#define MADV_FREE	8
#define MADV_HUGEPAGE	67
#define MADV_MERGEABLE   65
#define MADV_NOHUGEPAGE	68
#define MADV_NORMAL     0
#define MADV_RANDOM     1
#define MADV_REMOVE	9
#define MADV_SEQUENTIAL 2
#define MADV_UNMERGEABLE 66
#define MADV_WILLNEED   3
#define MAP_ANONYMOUS	0x10
#define MAP_DENYWRITE	0x0800
#define MAP_EXECUTABLE	0x1000
#define MAP_FILE	0
#define MAP_FIXED	0x04
#define MAP_GROWSDOWN	0x8000
#define MAP_HUGETLB	0x80000
#define MAP_LOCKED	0x2000
#define MAP_NONBLOCK	0x20000
#define MAP_NORESERVE	0x4000
#define MAP_POPULATE	0x10000
#define MAP_STACK	0x40000
#define PROT_EXEC	0x4
#define PROT_GROWSDOWN	0x01000000
#define PROT_GROWSUP	0x02000000
#define PROT_NONE	0x0
#define PROT_READ	0x1
#define PROT_SEM	0x8
#define PROT_WRITE	0x2
#define MADV_HWPOISON	100
#define MADV_SOFT_OFFLINE 101
/* MAP_32BIT is undefined on parisc, fix it for perf */
#define MAP_32BIT	0
/* MAP_UNINITIALIZED is undefined on parisc, fix it for perf */
#define MAP_UNINITIALIZED	0
#endif
