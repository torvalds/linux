#ifndef _LINUX_MMIOTRACE_H
#define _LINUX_MMIOTRACE_H

#include <linux/types.h>
#include <linux/list.h>

struct kmmio_probe;
struct pt_regs;

typedef void (*kmmio_pre_handler_t)(struct kmmio_probe *,
				struct pt_regs *, unsigned long addr);
typedef void (*kmmio_post_handler_t)(struct kmmio_probe *,
				unsigned long condition, struct pt_regs *);

struct kmmio_probe {
	/* kmmio internal list: */
	struct list_head	list;
	/* start location of the probe point: */
	unsigned long		addr;
	/* length of the probe region: */
	unsigned long		len;
	/* Called before addr is executed: */
	kmmio_pre_handler_t	pre_handler;
	/* Called after addr is executed: */
	kmmio_post_handler_t	post_handler;
	void			*private;
};

extern unsigned int kmmio_count;

extern int register_kmmio_probe(struct kmmio_probe *p);
extern void unregister_kmmio_probe(struct kmmio_probe *p);
extern int kmmio_init(void);
extern void kmmio_cleanup(void);

#ifdef CONFIG_MMIOTRACE
/* kmmio is active by some kmmio_probes? */
static inline int is_kmmio_active(void)
{
	return kmmio_count;
}

/* Called from page fault handler. */
extern int kmmio_handler(struct pt_regs *regs, unsigned long addr);

/* Called from ioremap.c */
extern void mmiotrace_ioremap(resource_size_t offset, unsigned long size,
							void __iomem *addr);
extern void mmiotrace_iounmap(volatile void __iomem *addr);

/* For anyone to insert markers. Remember trailing newline. */
extern int mmiotrace_printk(const char *fmt, ...)
				__attribute__ ((format (printf, 1, 2)));
#else /* !CONFIG_MMIOTRACE: */
static inline int is_kmmio_active(void)
{
	return 0;
}

static inline int kmmio_handler(struct pt_regs *regs, unsigned long addr)
{
	return 0;
}

static inline void mmiotrace_ioremap(resource_size_t offset,
					unsigned long size, void __iomem *addr)
{
}

static inline void mmiotrace_iounmap(volatile void __iomem *addr)
{
}

static inline int mmiotrace_printk(const char *fmt, ...)
				__attribute__ ((format (printf, 1, 0)));

static inline int mmiotrace_printk(const char *fmt, ...)
{
	return 0;
}
#endif /* CONFIG_MMIOTRACE */

enum mm_io_opcode {
	MMIO_READ	= 0x1,	/* struct mmiotrace_rw */
	MMIO_WRITE	= 0x2,	/* struct mmiotrace_rw */
	MMIO_PROBE	= 0x3,	/* struct mmiotrace_map */
	MMIO_UNPROBE	= 0x4,	/* struct mmiotrace_map */
	MMIO_UNKNOWN_OP = 0x5,	/* struct mmiotrace_rw */
};

struct mmiotrace_rw {
	resource_size_t	phys;	/* PCI address of register */
	unsigned long	value;
	unsigned long	pc;	/* optional program counter */
	int		map_id;
	unsigned char	opcode;	/* one of MMIO_{READ,WRITE,UNKNOWN_OP} */
	unsigned char	width;	/* size of register access in bytes */
};

struct mmiotrace_map {
	resource_size_t	phys;	/* base address in PCI space */
	unsigned long	virt;	/* base virtual address */
	unsigned long	len;	/* mapping size */
	int		map_id;
	unsigned char	opcode;	/* MMIO_PROBE or MMIO_UNPROBE */
};

/* in kernel/trace/trace_mmiotrace.c */
extern void enable_mmiotrace(void);
extern void disable_mmiotrace(void);
extern void mmio_trace_rw(struct mmiotrace_rw *rw);
extern void mmio_trace_mapping(struct mmiotrace_map *map);
extern int mmio_trace_printk(const char *fmt, va_list args);

#endif /* _LINUX_MMIOTRACE_H */
