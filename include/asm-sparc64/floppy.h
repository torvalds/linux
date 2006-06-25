/* $Id: floppy.h,v 1.32 2001/10/26 17:59:36 davem Exp $
 * asm-sparc64/floppy.h: Sparc specific parts of the Floppy driver.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *
 * Ultra/PCI support added: Sep 1997  Eddie C. Dost  (ecd@skynet.be)
 */

#ifndef __ASM_SPARC64_FLOPPY_H
#define __ASM_SPARC64_FLOPPY_H

#include <linux/init.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/idprom.h>
#include <asm/oplib.h>
#include <asm/auxio.h>
#include <asm/sbus.h>
#include <asm/irq.h>


/*
 * Define this to enable exchanging drive 0 and 1 if only drive 1 is
 * probed on PCI machines.
 */
#undef PCI_FDC_SWAP_DRIVES


/* References:
 * 1) Netbsd Sun floppy driver.
 * 2) NCR 82077 controller manual
 * 3) Intel 82077 controller manual
 */
struct sun_flpy_controller {
	volatile unsigned char status1_82077; /* Auxiliary Status reg. 1 */
	volatile unsigned char status2_82077; /* Auxiliary Status reg. 2 */
	volatile unsigned char dor_82077;     /* Digital Output reg. */
	volatile unsigned char tapectl_82077; /* Tape Control reg */
	volatile unsigned char status_82077;  /* Main Status Register. */
#define drs_82077              status_82077   /* Digital Rate Select reg. */
	volatile unsigned char data_82077;    /* Data fifo. */
	volatile unsigned char ___unused;
	volatile unsigned char dir_82077;     /* Digital Input reg. */
#define dcr_82077              dir_82077      /* Config Control reg. */
};

/* You'll only ever find one controller on an Ultra anyways. */
static struct sun_flpy_controller *sun_fdc = (struct sun_flpy_controller *)-1;
unsigned long fdc_status;
static struct sbus_dev *floppy_sdev = NULL;

struct sun_floppy_ops {
	unsigned char	(*fd_inb) (unsigned long port);
	void		(*fd_outb) (unsigned char value, unsigned long port);
	void		(*fd_enable_dma) (void);
	void		(*fd_disable_dma) (void);
	void		(*fd_set_dma_mode) (int);
	void		(*fd_set_dma_addr) (char *);
	void		(*fd_set_dma_count) (int);
	unsigned int	(*get_dma_residue) (void);
	int		(*fd_request_irq) (void);
	void		(*fd_free_irq) (void);
	int		(*fd_eject) (int);
};

static struct sun_floppy_ops sun_fdops;

#define fd_inb(port)              sun_fdops.fd_inb(port)
#define fd_outb(value,port)       sun_fdops.fd_outb(value,port)
#define fd_enable_dma()           sun_fdops.fd_enable_dma()
#define fd_disable_dma()          sun_fdops.fd_disable_dma()
#define fd_request_dma()          (0) /* nothing... */
#define fd_free_dma()             /* nothing... */
#define fd_clear_dma_ff()         /* nothing... */
#define fd_set_dma_mode(mode)     sun_fdops.fd_set_dma_mode(mode)
#define fd_set_dma_addr(addr)     sun_fdops.fd_set_dma_addr(addr)
#define fd_set_dma_count(count)   sun_fdops.fd_set_dma_count(count)
#define get_dma_residue(x)        sun_fdops.get_dma_residue()
#define fd_cacheflush(addr, size) /* nothing... */
#define fd_request_irq()          sun_fdops.fd_request_irq()
#define fd_free_irq()             sun_fdops.fd_free_irq()
#define fd_eject(drive)           sun_fdops.fd_eject(drive)

static int FLOPPY_MOTOR_MASK = 0x10;

/* Super paranoid... */
#undef HAVE_DISABLE_HLT

static int sun_floppy_types[2] = { 0, 0 };

/* Here is where we catch the floppy driver trying to initialize,
 * therefore this is where we call the PROM device tree probing
 * routine etc. on the Sparc.
 */
#define FLOPPY0_TYPE		sun_floppy_init()
#define FLOPPY1_TYPE		sun_floppy_types[1]

#define FDC1			((unsigned long)sun_fdc)

#define N_FDC    1
#define N_DRIVE  8

/* No 64k boundary crossing problems on the Sparc. */
#define CROSS_64KB(a,s) (0)

static unsigned char sun_82077_fd_inb(unsigned long port)
{
	udelay(5);
	switch(port & 7) {
	default:
		printk("floppy: Asked to read unknown port %lx\n", port);
		panic("floppy: Port bolixed.");
	case 4: /* FD_STATUS */
		return sbus_readb(&sun_fdc->status_82077) & ~STATUS_DMA;
	case 5: /* FD_DATA */
		return sbus_readb(&sun_fdc->data_82077);
	case 7: /* FD_DIR */
		/* XXX: Is DCL on 0x80 in sun4m? */
		return sbus_readb(&sun_fdc->dir_82077);
	};
	panic("sun_82072_fd_inb: How did I get here?");
}

static void sun_82077_fd_outb(unsigned char value, unsigned long port)
{
	udelay(5);
	switch(port & 7) {
	default:
		printk("floppy: Asked to write to unknown port %lx\n", port);
		panic("floppy: Port bolixed.");
	case 2: /* FD_DOR */
		/* Happily, the 82077 has a real DOR register. */
		sbus_writeb(value, &sun_fdc->dor_82077);
		break;
	case 5: /* FD_DATA */
		sbus_writeb(value, &sun_fdc->data_82077);
		break;
	case 7: /* FD_DCR */
		sbus_writeb(value, &sun_fdc->dcr_82077);
		break;
	case 4: /* FD_STATUS */
		sbus_writeb(value, &sun_fdc->status_82077);
		break;
	};
	return;
}

/* For pseudo-dma (Sun floppy drives have no real DMA available to
 * them so we must eat the data fifo bytes directly ourselves) we have
 * three state variables.  doing_pdma tells our inline low-level
 * assembly floppy interrupt entry point whether it should sit and eat
 * bytes from the fifo or just transfer control up to the higher level
 * floppy interrupt c-code.  I tried very hard but I could not get the
 * pseudo-dma to work in c-code without getting many overruns and
 * underruns.  If non-zero, doing_pdma encodes the direction of
 * the transfer for debugging.  1=read 2=write
 */
unsigned char *pdma_vaddr;
unsigned long pdma_size;
volatile int doing_pdma = 0;

/* This is software state */
char *pdma_base = NULL;
unsigned long pdma_areasize;

/* Common routines to all controller types on the Sparc. */
static void sun_fd_disable_dma(void)
{
	doing_pdma = 0;
	if (pdma_base) {
		mmu_unlockarea(pdma_base, pdma_areasize);
		pdma_base = NULL;
	}
}

static void sun_fd_set_dma_mode(int mode)
{
	switch(mode) {
	case DMA_MODE_READ:
		doing_pdma = 1;
		break;
	case DMA_MODE_WRITE:
		doing_pdma = 2;
		break;
	default:
		printk("Unknown dma mode %d\n", mode);
		panic("floppy: Giving up...");
	}
}

static void sun_fd_set_dma_addr(char *buffer)
{
	pdma_vaddr = buffer;
}

static void sun_fd_set_dma_count(int length)
{
	pdma_size = length;
}

static void sun_fd_enable_dma(void)
{
	pdma_vaddr = mmu_lockarea(pdma_vaddr, pdma_size);
	pdma_base = pdma_vaddr;
	pdma_areasize = pdma_size;
}

extern irqreturn_t sparc_floppy_irq(int, void *, struct pt_regs *);

static int sun_fd_request_irq(void)
{
	static int once = 0;
	int error;

	if(!once) {
		once = 1;

		error = request_irq(FLOPPY_IRQ, sparc_floppy_irq, 
				    SA_INTERRUPT, "floppy", NULL);

		return ((error == 0) ? 0 : -1);
	}
	return 0;
}

static void sun_fd_free_irq(void)
{
}

static unsigned int sun_get_dma_residue(void)
{
	/* XXX This isn't really correct. XXX */
	return 0;
}

static int sun_fd_eject(int drive)
{
	set_dor(0x00, 0xff, 0x90);
	udelay(500);
	set_dor(0x00, 0x6f, 0x00);
	udelay(500);
	return 0;
}

#ifdef CONFIG_PCI
#include <asm/ebus.h>
#include <asm/isa.h>
#include <asm/ns87303.h>

static struct ebus_dma_info sun_pci_fd_ebus_dma;
static struct pci_dev *sun_pci_ebus_dev;
static int sun_pci_broken_drive = -1;

struct sun_pci_dma_op {
	unsigned int 	addr;
	int		len;
	int		direction;
	char		*buf;
};
static struct sun_pci_dma_op sun_pci_dma_current = { -1U, 0, 0, NULL};
static struct sun_pci_dma_op sun_pci_dma_pending = { -1U, 0, 0, NULL};

extern irqreturn_t floppy_interrupt(int irq, void *dev_id, struct pt_regs *regs);

static unsigned char sun_pci_fd_inb(unsigned long port)
{
	udelay(5);
	return inb(port);
}

static void sun_pci_fd_outb(unsigned char val, unsigned long port)
{
	udelay(5);
	outb(val, port);
}

static void sun_pci_fd_broken_outb(unsigned char val, unsigned long port)
{
	udelay(5);
	/*
	 * XXX: Due to SUN's broken floppy connector on AX and AXi
	 *      we need to turn on MOTOR_0 also, if the floppy is
	 *      jumpered to DS1 (like most PC floppies are). I hope
	 *      this does not hurt correct hardware like the AXmp.
	 *      (Eddie, Sep 12 1998).
	 */
	if (port == ((unsigned long)sun_fdc) + 2) {
		if (((val & 0x03) == sun_pci_broken_drive) && (val & 0x20)) {
			val |= 0x10;
		}
	}
	outb(val, port);
}

#ifdef PCI_FDC_SWAP_DRIVES
static void sun_pci_fd_lde_broken_outb(unsigned char val, unsigned long port)
{
	udelay(5);
	/*
	 * XXX: Due to SUN's broken floppy connector on AX and AXi
	 *      we need to turn on MOTOR_0 also, if the floppy is
	 *      jumpered to DS1 (like most PC floppies are). I hope
	 *      this does not hurt correct hardware like the AXmp.
	 *      (Eddie, Sep 12 1998).
	 */
	if (port == ((unsigned long)sun_fdc) + 2) {
		if (((val & 0x03) == sun_pci_broken_drive) && (val & 0x10)) {
			val &= ~(0x03);
			val |= 0x21;
		}
	}
	outb(val, port);
}
#endif /* PCI_FDC_SWAP_DRIVES */

static void sun_pci_fd_enable_dma(void)
{
	BUG_ON((NULL == sun_pci_dma_pending.buf) 	||
	    (0	  == sun_pci_dma_pending.len) 	||
	    (0	  == sun_pci_dma_pending.direction));

	sun_pci_dma_current.buf = sun_pci_dma_pending.buf;
	sun_pci_dma_current.len = sun_pci_dma_pending.len;
	sun_pci_dma_current.direction = sun_pci_dma_pending.direction;

	sun_pci_dma_pending.buf  = NULL;
	sun_pci_dma_pending.len  = 0;
	sun_pci_dma_pending.direction = 0;
	sun_pci_dma_pending.addr = -1U;

	sun_pci_dma_current.addr = 
		pci_map_single(sun_pci_ebus_dev,
			       sun_pci_dma_current.buf,
			       sun_pci_dma_current.len,
			       sun_pci_dma_current.direction);

	ebus_dma_enable(&sun_pci_fd_ebus_dma, 1);

	if (ebus_dma_request(&sun_pci_fd_ebus_dma,
			     sun_pci_dma_current.addr,
			     sun_pci_dma_current.len))
		BUG();
}

static void sun_pci_fd_disable_dma(void)
{
	ebus_dma_enable(&sun_pci_fd_ebus_dma, 0);
	if (sun_pci_dma_current.addr != -1U)
		pci_unmap_single(sun_pci_ebus_dev,
				 sun_pci_dma_current.addr,
				 sun_pci_dma_current.len,
				 sun_pci_dma_current.direction);
	sun_pci_dma_current.addr = -1U;
}

static void sun_pci_fd_set_dma_mode(int mode)
{
	if (mode == DMA_MODE_WRITE)
		sun_pci_dma_pending.direction = PCI_DMA_TODEVICE;
	else
		sun_pci_dma_pending.direction = PCI_DMA_FROMDEVICE;

	ebus_dma_prepare(&sun_pci_fd_ebus_dma, mode != DMA_MODE_WRITE);
}

static void sun_pci_fd_set_dma_count(int length)
{
	sun_pci_dma_pending.len = length;
}

static void sun_pci_fd_set_dma_addr(char *buffer)
{
	sun_pci_dma_pending.buf = buffer;
}

static unsigned int sun_pci_get_dma_residue(void)
{
	return ebus_dma_residue(&sun_pci_fd_ebus_dma);
}

static int sun_pci_fd_request_irq(void)
{
	return ebus_dma_irq_enable(&sun_pci_fd_ebus_dma, 1);
}

static void sun_pci_fd_free_irq(void)
{
	ebus_dma_irq_enable(&sun_pci_fd_ebus_dma, 0);
}

static int sun_pci_fd_eject(int drive)
{
	return -EINVAL;
}

void sun_pci_fd_dma_callback(struct ebus_dma_info *p, int event, void *cookie)
{
	floppy_interrupt(0, NULL, NULL);
}

/*
 * Floppy probing, we'd like to use /dev/fd0 for a single Floppy on PCI,
 * even if this is configured using DS1, thus looks like /dev/fd1 with
 * the cabling used in Ultras.
 */
#define DOR	(port + 2)
#define MSR	(port + 4)
#define FIFO	(port + 5)

static void sun_pci_fd_out_byte(unsigned long port, unsigned char val,
			        unsigned long reg)
{
	unsigned char status;
	int timeout = 1000;

	while (!((status = inb(MSR)) & 0x80) && --timeout)
		udelay(100);
	outb(val, reg);
}

static unsigned char sun_pci_fd_sensei(unsigned long port)
{
	unsigned char result[2] = { 0x70, 0x00 };
	unsigned char status;
	int i = 0;

	sun_pci_fd_out_byte(port, 0x08, FIFO);
	do {
		int timeout = 1000;

		while (!((status = inb(MSR)) & 0x80) && --timeout)
			udelay(100);

		if (!timeout)
			break;

		if ((status & 0xf0) == 0xd0)
			result[i++] = inb(FIFO);
		else
			break;
	} while (i < 2);

	return result[0];
}

static void sun_pci_fd_reset(unsigned long port)
{
	unsigned char mask = 0x00;
	unsigned char status;
	int timeout = 10000;

	outb(0x80, MSR);
	do {
		status = sun_pci_fd_sensei(port);
		if ((status & 0xc0) == 0xc0)
			mask |= 1 << (status & 0x03);
		else
			udelay(100);
	} while ((mask != 0x0f) && --timeout);
}

static int sun_pci_fd_test_drive(unsigned long port, int drive)
{
	unsigned char status, data;
	int timeout = 1000;
	int ready;

	sun_pci_fd_reset(port);

	data = (0x10 << drive) | 0x0c | drive;
	sun_pci_fd_out_byte(port, data, DOR);

	sun_pci_fd_out_byte(port, 0x07, FIFO);
	sun_pci_fd_out_byte(port, drive & 0x03, FIFO);

	do {
		udelay(100);
		status = sun_pci_fd_sensei(port);
	} while (((status & 0xc0) == 0x80) && --timeout);

	if (!timeout)
		ready = 0;
	else
		ready = (status & 0x10) ? 0 : 1;

	sun_pci_fd_reset(port);
	return ready;
}
#undef FIFO
#undef MSR
#undef DOR

#endif /* CONFIG_PCI */

#ifdef CONFIG_PCI
static int __init ebus_fdthree_p(struct linux_ebus_device *edev)
{
	if (!strcmp(edev->prom_node->name, "fdthree"))
		return 1;
	if (!strcmp(edev->prom_node->name, "floppy")) {
		char *compat;

		compat = of_get_property(edev->prom_node,
					 "compatible", NULL);
		if (compat && !strcmp(compat, "fdthree"))
			return 1;
	}
	return 0;
}
#endif

#ifdef CONFIG_PCI
#undef ISA_FLOPPY_WORKS

#ifdef ISA_FLOPPY_WORKS
static unsigned long __init isa_floppy_init(void)
{
	struct sparc_isa_bridge *isa_br;
	struct sparc_isa_device *isa_dev = NULL;

	for_each_isa(isa_br) {
		for_each_isadev(isa_dev, isa_br) {
			if (!strcmp(isa_dev->prom_node->name, "dma")) {
				struct sparc_isa_device *child =
					isa_dev->child;

				while (child) {
					if (!strcmp(child->prom_node->name,
						    "floppy")) {
						isa_dev = child;
						goto isa_done;
					}
					child = child->next;
				}
			}
		}
	}
isa_done:
	if (!isa_dev)
		return 0;

	/* We could use DMA on devices behind the ISA bridge, but...
	 *
	 * There is a slight problem.  Normally on x86 kit the x86 processor
	 * delays I/O port instructions when the ISA bus "dma in progress"
	 * signal is active.  Well, sparc64 systems do not monitor this
	 * signal thus we would need to block all I/O port accesses in software
	 * when a dma transfer is active for some device.
	 */

	sun_fdc = (struct sun_flpy_controller *)isa_dev->resource.start;
	FLOPPY_IRQ = isa_dev->irq;

	sun_fdops.fd_inb = sun_pci_fd_inb;
	sun_fdops.fd_outb = sun_pci_fd_outb;

	can_use_virtual_dma = use_virtual_dma = 1;
	sun_fdops.fd_enable_dma = sun_fd_enable_dma;
	sun_fdops.fd_disable_dma = sun_fd_disable_dma;
	sun_fdops.fd_set_dma_mode = sun_fd_set_dma_mode;
	sun_fdops.fd_set_dma_addr = sun_fd_set_dma_addr;
	sun_fdops.fd_set_dma_count = sun_fd_set_dma_count;
	sun_fdops.get_dma_residue = sun_get_dma_residue;

	sun_fdops.fd_request_irq = sun_fd_request_irq;
	sun_fdops.fd_free_irq = sun_fd_free_irq;

	/* Floppy eject is manual.   Actually, could determine this
	 * via presence of 'manual' property in OBP node.
	 */
	sun_fdops.fd_eject = sun_pci_fd_eject;

        fdc_status = (unsigned long) &sun_fdc->status_82077;
	FLOPPY_MOTOR_MASK = 0xf0;

	allowed_drive_mask = 0;
	sun_floppy_types[0] = 0;
	sun_floppy_types[1] = 4;

	sun_pci_broken_drive = 1;
	sun_fdops.fd_outb = sun_pci_fd_broken_outb;

	return sun_floppy_types[0];
}
#endif /* ISA_FLOPPY_WORKS */

#endif

static unsigned long __init sun_floppy_init(void)
{
	char state[128];
	struct sbus_bus *bus;
	struct sbus_dev *sdev = NULL;
	static int initialized = 0;

	if (initialized)
		return sun_floppy_types[0];
	initialized = 1;

	for_all_sbusdev (sdev, bus) {
		if (!strcmp(sdev->prom_name, "SUNW,fdtwo")) 
			break;
	}
	if(sdev) {
		floppy_sdev = sdev;
		FLOPPY_IRQ = sdev->irqs[0];
	} else {
#ifdef CONFIG_PCI
		struct linux_ebus *ebus;
		struct linux_ebus_device *edev = NULL;
		unsigned long config = 0;
		void __iomem *auxio_reg;
		char *state_prop;

		for_each_ebus(ebus) {
			for_each_ebusdev(edev, ebus) {
				if (ebus_fdthree_p(edev))
					goto ebus_done;
			}
		}
	ebus_done:
		if (!edev) {
#ifdef ISA_FLOPPY_WORKS
			return isa_floppy_init();
#else
			return 0;
#endif
		}

		state_prop = of_get_property(edev->prom_node, "status", NULL);
		if (state_prop && !strncmp(state_prop, "disabled", 8))
			return 0;
			
		FLOPPY_IRQ = edev->irqs[0];

		/* Make sure the high density bit is set, some systems
		 * (most notably Ultra5/Ultra10) come up with it clear.
		 */
		auxio_reg = (void __iomem *) edev->resource[2].start;
		writel(readl(auxio_reg)|0x2, auxio_reg);

		sun_pci_ebus_dev = ebus->self;

		spin_lock_init(&sun_pci_fd_ebus_dma.lock);

		/* XXX ioremap */
		sun_pci_fd_ebus_dma.regs = (void __iomem *)
			edev->resource[1].start;
		if (!sun_pci_fd_ebus_dma.regs)
			return 0;

		sun_pci_fd_ebus_dma.flags = (EBUS_DMA_FLAG_USE_EBDMA_HANDLER |
					     EBUS_DMA_FLAG_TCI_DISABLE);
		sun_pci_fd_ebus_dma.callback = sun_pci_fd_dma_callback;
		sun_pci_fd_ebus_dma.client_cookie = NULL;
		sun_pci_fd_ebus_dma.irq = FLOPPY_IRQ;
		strcpy(sun_pci_fd_ebus_dma.name, "floppy");
		if (ebus_dma_register(&sun_pci_fd_ebus_dma))
			return 0;

		/* XXX ioremap */
		sun_fdc = (struct sun_flpy_controller *)edev->resource[0].start;

		sun_fdops.fd_inb = sun_pci_fd_inb;
		sun_fdops.fd_outb = sun_pci_fd_outb;

		can_use_virtual_dma = use_virtual_dma = 0;
		sun_fdops.fd_enable_dma = sun_pci_fd_enable_dma;
		sun_fdops.fd_disable_dma = sun_pci_fd_disable_dma;
		sun_fdops.fd_set_dma_mode = sun_pci_fd_set_dma_mode;
		sun_fdops.fd_set_dma_addr = sun_pci_fd_set_dma_addr;
		sun_fdops.fd_set_dma_count = sun_pci_fd_set_dma_count;
		sun_fdops.get_dma_residue = sun_pci_get_dma_residue;

		sun_fdops.fd_request_irq = sun_pci_fd_request_irq;
		sun_fdops.fd_free_irq = sun_pci_fd_free_irq;

		sun_fdops.fd_eject = sun_pci_fd_eject;

        	fdc_status = (unsigned long) &sun_fdc->status_82077;
		FLOPPY_MOTOR_MASK = 0xf0;

		/*
		 * XXX: Find out on which machines this is really needed.
		 */
		if (1) {
			sun_pci_broken_drive = 1;
			sun_fdops.fd_outb = sun_pci_fd_broken_outb;
		}

		allowed_drive_mask = 0;
		if (sun_pci_fd_test_drive((unsigned long)sun_fdc, 0))
			sun_floppy_types[0] = 4;
		if (sun_pci_fd_test_drive((unsigned long)sun_fdc, 1))
			sun_floppy_types[1] = 4;

		/*
		 * Find NS87303 SuperIO config registers (through ecpp).
		 */
		for_each_ebus(ebus) {
			for_each_ebusdev(edev, ebus) {
				if (!strcmp(edev->prom_node->name, "ecpp")) {
					config = edev->resource[1].start;
					goto config_done;
				}
			}
		}
	config_done:

		/*
		 * Sanity check, is this really the NS87303?
		 */
		switch (config & 0x3ff) {
		case 0x02e:
		case 0x15c:
		case 0x26e:
		case 0x398:
			break;
		default:
			config = 0;
		}

		if (!config)
			return sun_floppy_types[0];

		/* Enable PC-AT mode. */
		ns87303_modify(config, ASC, 0, 0xc0);

#ifdef PCI_FDC_SWAP_DRIVES
		/*
		 * If only Floppy 1 is present, swap drives.
		 */
		if (!sun_floppy_types[0] && sun_floppy_types[1]) {
			/*
			 * Set the drive exchange bit in FCR on NS87303,
			 * make sure other bits are sane before doing so.
			 */
			ns87303_modify(config, FER, FER_EDM, 0);
			ns87303_modify(config, ASC, ASC_DRV2_SEL, 0);
			ns87303_modify(config, FCR, 0, FCR_LDE);

			config = sun_floppy_types[0];
			sun_floppy_types[0] = sun_floppy_types[1];
			sun_floppy_types[1] = config;

			if (sun_pci_broken_drive != -1) {
				sun_pci_broken_drive = 1 - sun_pci_broken_drive;
				sun_fdops.fd_outb = sun_pci_fd_lde_broken_outb;
			}
		}
#endif /* PCI_FDC_SWAP_DRIVES */

		return sun_floppy_types[0];
#else
		return 0;
#endif
	}
	prom_getproperty(sdev->prom_node, "status", state, sizeof(state));
	if(!strncmp(state, "disabled", 8))
		return 0;

	/*
	 * We cannot do sbus_ioremap here: it does request_region,
	 * which the generic floppy driver tries to do once again.
	 * But we must use the sdev resource values as they have
	 * had parent ranges applied.
	 */
	sun_fdc = (struct sun_flpy_controller *)
		(sdev->resource[0].start +
		 ((sdev->resource[0].flags & 0x1ffUL) << 32UL));

	/* Last minute sanity check... */
	if(sbus_readb(&sun_fdc->status1_82077) == 0xff) {
		sun_fdc = (struct sun_flpy_controller *)-1;
		return 0;
	}

        sun_fdops.fd_inb = sun_82077_fd_inb;
        sun_fdops.fd_outb = sun_82077_fd_outb;

	can_use_virtual_dma = use_virtual_dma = 1;
	sun_fdops.fd_enable_dma = sun_fd_enable_dma;
	sun_fdops.fd_disable_dma = sun_fd_disable_dma;
	sun_fdops.fd_set_dma_mode = sun_fd_set_dma_mode;
	sun_fdops.fd_set_dma_addr = sun_fd_set_dma_addr;
	sun_fdops.fd_set_dma_count = sun_fd_set_dma_count;
	sun_fdops.get_dma_residue = sun_get_dma_residue;

	sun_fdops.fd_request_irq = sun_fd_request_irq;
	sun_fdops.fd_free_irq = sun_fd_free_irq;

	sun_fdops.fd_eject = sun_fd_eject;

        fdc_status = (unsigned long) &sun_fdc->status_82077;

	/* Success... */
	allowed_drive_mask = 0x01;
	sun_floppy_types[0] = 4;
	sun_floppy_types[1] = 0;

	return sun_floppy_types[0];
}

#define EXTRA_FLOPPY_PARAMS

#endif /* !(__ASM_SPARC64_FLOPPY_H) */
