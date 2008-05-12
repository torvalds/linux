#ifndef MMIOTRACE_H
#define MMIOTRACE_H

#include <asm/types.h>

#define MMIO_VERSION 0x04

/* mm_io_header.type */
#define MMIO_OPCODE_MASK 0xff
#define MMIO_OPCODE_SHIFT 0
#define MMIO_WIDTH_MASK 0xff00
#define MMIO_WIDTH_SHIFT 8
#define MMIO_MAGIC (0x6f000000 | (MMIO_VERSION<<16))
#define MMIO_MAGIC_MASK 0xffff0000

enum mm_io_opcode {          /* payload type: */
	MMIO_READ = 0x1,     /* struct mm_io_rw */
	MMIO_WRITE = 0x2,    /* struct mm_io_rw */
	MMIO_PROBE = 0x3,    /* struct mm_io_map */
	MMIO_UNPROBE = 0x4,  /* struct mm_io_map */
	MMIO_MARKER = 0x5,   /* raw char data */
	MMIO_UNKNOWN_OP = 0x6, /* struct mm_io_rw */
};

struct mm_io_header {
	__u32 type;
	__u32 sec;      /* timestamp */
	__u32 nsec;
	__u32 pid;      /* PID of the process, or 0 for kernel core */
	__u16 data_len; /* length of the following payload */
};

struct mm_io_rw {
	__u64 address; /* virtual address of register */
	__u64 value;
	__u64 pc;      /* optional program counter */
};

struct mm_io_map {
	__u64 phys;  /* base address in PCI space */
	__u64 addr;  /* base virtual address */
	__u64 len;   /* mapping size */
	__u64 pc;    /* optional program counter */
};


/*
 * These structures are used to allow a single relay_write()
 * call to write a full packet.
 */

struct mm_io_header_rw {
	struct mm_io_header header;
	struct mm_io_rw rw;
} __attribute__((packed));

struct mm_io_header_map {
	struct mm_io_header header;
	struct mm_io_map map;
} __attribute__((packed));

#endif /* MMIOTRACE_H */
