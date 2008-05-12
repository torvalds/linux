#ifndef MMIOTRACE_H
#define MMIOTRACE_H

#include <asm/types.h>

#ifdef __KERNEL__

#include <linux/list.h>

struct kmmio_probe;
struct pt_regs;

typedef void (*kmmio_pre_handler_t)(struct kmmio_probe *,
				struct pt_regs *, unsigned long addr);
typedef void (*kmmio_post_handler_t)(struct kmmio_probe *,
				unsigned long condition, struct pt_regs *);

struct kmmio_probe {
	struct list_head list; /* kmmio internal list */
	unsigned long addr; /* start location of the probe point */
	unsigned long len; /* length of the probe region */
	kmmio_pre_handler_t pre_handler; /* Called before addr is executed. */
	kmmio_post_handler_t post_handler; /* Called after addr is executed */
	void *user_data;
};

/* kmmio is active by some kmmio_probes? */
static inline int is_kmmio_active(void)
{
	extern unsigned int kmmio_count;
	return kmmio_count;
}

extern void reference_kmmio(void);
extern void unreference_kmmio(void);
extern int register_kmmio_probe(struct kmmio_probe *p);
extern void unregister_kmmio_probe(struct kmmio_probe *p);

/* Called from page fault handler. */
extern int kmmio_handler(struct pt_regs *regs, unsigned long addr);

/* Called from ioremap.c */
#ifdef CONFIG_MMIOTRACE
extern void
mmiotrace_ioremap(unsigned long offset, unsigned long size, void __iomem *addr);
extern void mmiotrace_iounmap(volatile void __iomem *addr);
#else
static inline void
mmiotrace_ioremap(unsigned long offset, unsigned long size, void __iomem *addr)
{
}
static inline void mmiotrace_iounmap(volatile void __iomem *addr)
{
}
#endif /* CONFIG_MMIOTRACE_HOOKS */

#endif /* __KERNEL__ */


/*
 * If you change anything here, you must bump MMIO_VERSION.
 * This is the relay data format for user space.
 */
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
	__u32 type;     /* see MMIO_* macros above */
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
