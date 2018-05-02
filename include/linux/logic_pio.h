// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 HiSilicon Limited, All Rights Reserved.
 * Author: Gabriele Paoloni <gabriele.paoloni@huawei.com>
 * Author: Zhichang Yuan <yuanzhichang@hisilicon.com>
 */

#ifndef __LINUX_LOGIC_PIO_H
#define __LINUX_LOGIC_PIO_H

#include <linux/fwnode.h>

enum {
	LOGIC_PIO_INDIRECT,		/* Indirect IO flag */
	LOGIC_PIO_CPU_MMIO,		/* Memory-mapped IO flag */
};

struct logic_pio_hwaddr {
	struct list_head list;
	struct fwnode_handle *fwnode;
	resource_size_t hw_start;
	resource_size_t io_start;
	resource_size_t size; /* range size populated */
	unsigned long flags;

	void *hostdata;
	const struct logic_pio_host_ops *ops;
};

struct logic_pio_host_ops {
	u32 (*in)(void *hostdata, unsigned long addr, size_t dwidth);
	void (*out)(void *hostdata, unsigned long addr, u32 val,
		    size_t dwidth);
	u32 (*ins)(void *hostdata, unsigned long addr, void *buffer,
		   size_t dwidth, unsigned int count);
	void (*outs)(void *hostdata, unsigned long addr, const void *buffer,
		     size_t dwidth, unsigned int count);
};

#ifdef CONFIG_INDIRECT_PIO
u8 logic_inb(unsigned long addr);
void logic_outb(u8 value, unsigned long addr);
void logic_outw(u16 value, unsigned long addr);
void logic_outl(u32 value, unsigned long addr);
u16 logic_inw(unsigned long addr);
u32 logic_inl(unsigned long addr);
void logic_outb(u8 value, unsigned long addr);
void logic_outw(u16 value, unsigned long addr);
void logic_outl(u32 value, unsigned long addr);
void logic_insb(unsigned long addr, void *buffer, unsigned int count);
void logic_insl(unsigned long addr, void *buffer, unsigned int count);
void logic_insw(unsigned long addr, void *buffer, unsigned int count);
void logic_outsb(unsigned long addr, const void *buffer, unsigned int count);
void logic_outsw(unsigned long addr, const void *buffer, unsigned int count);
void logic_outsl(unsigned long addr, const void *buffer, unsigned int count);

#ifndef inb
#define inb logic_inb
#endif

#ifndef inw
#define inw logic_inw
#endif

#ifndef inl
#define inl logic_inl
#endif

#ifndef outb
#define outb logic_outb
#endif

#ifndef outw
#define outw logic_outw
#endif

#ifndef outl
#define outl logic_outl
#endif

#ifndef insb
#define insb logic_insb
#endif

#ifndef insw
#define insw logic_insw
#endif

#ifndef insl
#define insl logic_insl
#endif

#ifndef outsb
#define outsb logic_outsb
#endif

#ifndef outsw
#define outsw logic_outsw
#endif

#ifndef outsl
#define outsl logic_outsl
#endif

/*
 * We reserve 0x4000 bytes for Indirect IO as so far this library is only
 * used by the HiSilicon LPC Host. If needed, we can reserve a wider IO
 * area by redefining the macro below.
 */
#define PIO_INDIRECT_SIZE 0x4000
#define MMIO_UPPER_LIMIT (IO_SPACE_LIMIT - PIO_INDIRECT_SIZE)
#else
#define MMIO_UPPER_LIMIT IO_SPACE_LIMIT
#endif /* CONFIG_INDIRECT_PIO */

struct logic_pio_hwaddr *find_io_range_by_fwnode(struct fwnode_handle *fwnode);
unsigned long logic_pio_trans_hwaddr(struct fwnode_handle *fwnode,
			resource_size_t hw_addr, resource_size_t size);
int logic_pio_register_range(struct logic_pio_hwaddr *newrange);
resource_size_t logic_pio_to_hwaddr(unsigned long pio);
unsigned long logic_pio_trans_cpuaddr(resource_size_t hw_addr);

#endif /* __LINUX_LOGIC_PIO_H */
