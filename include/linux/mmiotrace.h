#ifndef MMIOTRACE_H
#define MMIOTRACE_H

#include <linux/types.h>
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
	void *private;
};

/* kmmio is active by some kmmio_probes? */
static inline int is_kmmio_active(void)
{
	extern unsigned int kmmio_count;
	return kmmio_count;
}

extern int register_kmmio_probe(struct kmmio_probe *p);
extern void unregister_kmmio_probe(struct kmmio_probe *p);

/* Called from page fault handler. */
extern int kmmio_handler(struct pt_regs *regs, unsigned long addr);

/* Called from ioremap.c */
#ifdef CONFIG_MMIOTRACE
extern void mmiotrace_ioremap(resource_size_t offset, unsigned long size,
							void __iomem *addr);
extern void mmiotrace_iounmap(volatile void __iomem *addr);
#else
static inline void mmiotrace_ioremap(resource_size_t offset,
					unsigned long size, void __iomem *addr)
{
}

static inline void mmiotrace_iounmap(volatile void __iomem *addr)
{
}
#endif /* CONFIG_MMIOTRACE_HOOKS */

enum mm_io_opcode {
	MMIO_READ = 0x1,     /* struct mmiotrace_rw */
	MMIO_WRITE = 0x2,    /* struct mmiotrace_rw */
	MMIO_PROBE = 0x3,    /* struct mmiotrace_map */
	MMIO_UNPROBE = 0x4,  /* struct mmiotrace_map */
	MMIO_MARKER = 0x5,   /* raw char data */
	MMIO_UNKNOWN_OP = 0x6, /* struct mmiotrace_rw */
};

struct mmiotrace_rw {
	resource_size_t phys;	/* PCI address of register */
	unsigned long value;
	unsigned long pc;	/* optional program counter */
	int map_id;
	unsigned char opcode;	/* one of MMIO_{READ,WRITE,UNKNOWN_OP} */
	unsigned char width;	/* size of register access in bytes */
};

struct mmiotrace_map {
	resource_size_t phys;	/* base address in PCI space */
	unsigned long virt;	/* base virtual address */
	unsigned long len;	/* mapping size */
	int map_id;
	unsigned char opcode;	/* MMIO_PROBE or MMIO_UNPROBE */
};

/* in kernel/trace/trace_mmiotrace.c */
extern void enable_mmiotrace(void);
extern void disable_mmiotrace(void);
extern void mmio_trace_rw(struct mmiotrace_rw *rw);
extern void mmio_trace_mapping(struct mmiotrace_map *map);

#endif /* MMIOTRACE_H */
