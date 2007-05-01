/* $Id: parport.h,v 1.11 2001/05/11 07:54:24 davem Exp $
 * parport.h: sparc64 specific parport initialization and dma.
 *
 * Copyright (C) 1999  Eddie C. Dost  (ecd@skynet.be)
 */

#ifndef _ASM_SPARC64_PARPORT_H
#define _ASM_SPARC64_PARPORT_H 1

#include <asm/ebus.h>
#include <asm/isa.h>
#include <asm/ns87303.h>

#define PARPORT_PC_MAX_PORTS	PARPORT_MAX

/*
 * While sparc64 doesn't have an ISA DMA API, we provide something that looks
 * close enough to make parport_pc happy
 */
#define HAS_DMA

static DEFINE_SPINLOCK(dma_spin_lock);

#define claim_dma_lock() \
({	unsigned long flags; \
	spin_lock_irqsave(&dma_spin_lock, flags); \
	flags; \
})

#define release_dma_lock(__flags) \
	spin_unlock_irqrestore(&dma_spin_lock, __flags);

static struct sparc_ebus_info {
	struct ebus_dma_info info;
	unsigned int addr;
	unsigned int count;
	int lock;
} sparc_ebus_dmas[PARPORT_PC_MAX_PORTS];

static __inline__ int request_dma(unsigned int dmanr, const char *device_id)
{
	if (dmanr >= PARPORT_PC_MAX_PORTS)
		return -EINVAL;
	if (xchg(&sparc_ebus_dmas[dmanr].lock, 1) != 0)
		return -EBUSY;
	return 0;
}

static __inline__ void free_dma(unsigned int dmanr)
{
	if (dmanr >= PARPORT_PC_MAX_PORTS) {
		printk(KERN_WARNING "Trying to free DMA%d\n", dmanr);
		return;
	}
	if (xchg(&sparc_ebus_dmas[dmanr].lock, 0) == 0) {
		printk(KERN_WARNING "Trying to free free DMA%d\n", dmanr);
		return;
	}	
}

static __inline__ void enable_dma(unsigned int dmanr)
{
	ebus_dma_enable(&sparc_ebus_dmas[dmanr].info, 1);

	if (ebus_dma_request(&sparc_ebus_dmas[dmanr].info,
			     sparc_ebus_dmas[dmanr].addr,
			     sparc_ebus_dmas[dmanr].count))
		BUG();
}

static __inline__ void disable_dma(unsigned int dmanr)
{
	ebus_dma_enable(&sparc_ebus_dmas[dmanr].info, 0);
}

static __inline__ void clear_dma_ff(unsigned int dmanr)
{
	/* nothing */
}

static __inline__ void set_dma_mode(unsigned int dmanr, char mode)
{
	ebus_dma_prepare(&sparc_ebus_dmas[dmanr].info, (mode != DMA_MODE_WRITE));
}

static __inline__ void set_dma_addr(unsigned int dmanr, unsigned int addr)
{
	sparc_ebus_dmas[dmanr].addr = addr;
}

static __inline__ void set_dma_count(unsigned int dmanr, unsigned int count)
{
	sparc_ebus_dmas[dmanr].count = count;
}

static __inline__ unsigned int get_dma_residue(unsigned int dmanr)
{
	return ebus_dma_residue(&sparc_ebus_dmas[dmanr].info);
}

static int ebus_ecpp_p(struct linux_ebus_device *edev)
{
	if (!strcmp(edev->prom_node->name, "ecpp"))
		return 1;
	if (!strcmp(edev->prom_node->name, "parallel")) {
		const char *compat;

		compat = of_get_property(edev->prom_node,
					 "compatible", NULL);
		if (compat &&
		    (!strcmp(compat, "ecpp") ||
		     !strcmp(compat, "ns87317-ecpp") ||
		     !strcmp(compat + 13, "ecpp")))
			return 1;
	}
	return 0;
}

static int parport_isa_probe(int count)
{
	struct sparc_isa_bridge *isa_br;
	struct sparc_isa_device *isa_dev;

	for_each_isa(isa_br) {
		for_each_isadev(isa_dev, isa_br) {
			struct sparc_isa_device *child;
			unsigned long base;

			if (strcmp(isa_dev->prom_node->name, "dma"))
				continue;

			child = isa_dev->child;
			while (child) {
				if (!strcmp(child->prom_node->name, "parallel"))
					break;
				child = child->next;
			}
			if (!child)
				continue;

			base = child->resource.start;

			/* No DMA, see commentary in
			 * asm-sparc64/floppy.h:isa_floppy_init()
			 */
			if (parport_pc_probe_port(base, base + 0x400,
						  child->irq, PARPORT_DMA_NOFIFO,
						  child->bus->self))
				count++;
		}
	}

	return count;
}

static int parport_pc_find_nonpci_ports (int autoirq, int autodma)
{
	struct linux_ebus *ebus;
	struct linux_ebus_device *edev;
	int count = 0;

	for_each_ebus(ebus) {
		for_each_ebusdev(edev, ebus) {
			if (ebus_ecpp_p(edev)) {
				unsigned long base = edev->resource[0].start;
				unsigned long config = edev->resource[1].start;
				unsigned long d_base = edev->resource[2].start;
				unsigned long d_len;

				spin_lock_init(&sparc_ebus_dmas[count].info.lock);
				d_len = (edev->resource[2].end -
					 d_base) + 1;
				sparc_ebus_dmas[count].info.regs =
					ioremap(d_base, d_len);
				if (!sparc_ebus_dmas[count].info.regs)
					continue;
				sparc_ebus_dmas[count].info.flags = 0;
				sparc_ebus_dmas[count].info.callback = NULL;
				sparc_ebus_dmas[count].info.client_cookie = NULL;
				sparc_ebus_dmas[count].info.irq = 0xdeadbeef;
				strcpy(sparc_ebus_dmas[count].info.name, "parport");
				if (ebus_dma_register(&sparc_ebus_dmas[count].info))
					continue;
				ebus_dma_irq_enable(&sparc_ebus_dmas[count].info, 1);

				/* Configure IRQ to Push Pull, Level Low */
				/* Enable ECP, set bit 2 of the CTR first */
				outb(0x04, base + 0x02);
				ns87303_modify(config, PCR,
					       PCR_EPP_ENABLE |
					       PCR_IRQ_ODRAIN,
					       PCR_ECP_ENABLE |
					       PCR_ECP_CLK_ENA |
					       PCR_IRQ_POLAR);

				/* CTR bit 5 controls direction of port */
				ns87303_modify(config, PTR,
					       0, PTR_LPT_REG_DIR);

				if (parport_pc_probe_port(base, base + 0x400,
							  edev->irqs[0],
							  count, ebus->self))
					count++;
			}
		}
	}

	count = parport_isa_probe(count);

	return count;
}

#endif /* !(_ASM_SPARC64_PARPORT_H */
