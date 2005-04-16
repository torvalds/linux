/****************************************************************************/
/*
 *  linux/include/asm-m68knommu/ide.h
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors
 *	Copyright (C) 2001       Lineo Inc., davidm@uclinux.org
 */
/****************************************************************************/
#ifndef _M68KNOMMU_IDE_H
#define _M68KNOMMU_IDE_H

#ifdef __KERNEL__
/****************************************************************************/

#include <linux/config.h>
#include <linux/interrupt.h>

#include <asm/setup.h>
#include <asm/io.h>
#include <asm/irq.h>

/****************************************************************************/
/*
 *	some coldfire specifics
 */

#ifdef CONFIG_COLDFIRE
#include <asm/coldfire.h>
#include <asm/mcfsim.h>

/*
 *	Save some space,  only have 1 interface
 */
#define MAX_HWIFS		  1	/* we only have one interface for now */

#ifdef CONFIG_SECUREEDGEMP3
#define	MCFSIM_LOCALCS	  MCFSIM_CSCR4
#else
#define	MCFSIM_LOCALCS	  MCFSIM_CSCR6
#endif

#endif /* CONFIG_COLDFIRE */

/****************************************************************************/
/*
 *	Fix up things that may not have been provided
 */

#ifndef MAX_HWIFS
#define MAX_HWIFS	4	/* same as the other archs */
#endif

#undef SUPPORT_SLOW_DATA_PORTS
#define SUPPORT_SLOW_DATA_PORTS 0

#undef SUPPORT_VLB_SYNC
#define SUPPORT_VLB_SYNC 0

/* this definition is used only on startup .. */
#undef HD_DATA
#define HD_DATA NULL

#define	DBGIDE(fmt,a...)
// #define	DBGIDE(fmt,a...) printk(fmt, ##a)
#define IDE_INLINE __inline__
// #define IDE_INLINE

/****************************************************************************/

typedef union {
	unsigned all			: 8;	/* all of the bits together */
	struct {
		unsigned bit7		: 1;	/* always 1 */
		unsigned lba		: 1;	/* using LBA instead of CHS */
		unsigned bit5		: 1;	/* always 1 */
		unsigned unit		: 1;	/* drive select number, 0 or 1 */
		unsigned head		: 4;	/* always zeros here */
	} b;
} select_t;

/*
 *	our list of ports/irq's for different boards
 */

static struct m68k_ide_defaults {
	ide_ioreg_t	base;
	int			irq;
} m68k_ide_defaults[MAX_HWIFS] = {
#if defined(CONFIG_SECUREEDGEMP3)
	{ ((ide_ioreg_t)0x30800000), 29 },
#elif defined(CONFIG_eLIA)
	{ ((ide_ioreg_t)0x30c00000), 29 },
#else
	{ ((ide_ioreg_t)0x0), 0 }
#endif
};

/****************************************************************************/

static IDE_INLINE int ide_default_irq(ide_ioreg_t base)
{
	int i;

	for (i = 0; i < MAX_HWIFS; i++)
		if (m68k_ide_defaults[i].base == base)
			return(m68k_ide_defaults[i].irq);
	return 0;
}

static IDE_INLINE ide_ioreg_t ide_default_io_base(int index)
{
	if (index >= 0 && index < MAX_HWIFS)
		return(m68k_ide_defaults[index].base);
	return 0;
}


/*
 * Set up a hw structure for a specified data port, control port and IRQ.
 * This should follow whatever the default interface uses.
 */
static IDE_INLINE void ide_init_hwif_ports(
	hw_regs_t *hw,
	ide_ioreg_t data_port,
	ide_ioreg_t ctrl_port,
	int *irq)
{
	ide_ioreg_t reg = data_port;
	int i;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg;
		reg += 1;
	}
	if (ctrl_port) {
		hw->io_ports[IDE_CONTROL_OFFSET] = ctrl_port;
	} else {
		hw->io_ports[IDE_CONTROL_OFFSET] = data_port + 0xe;
	}
}

#define ide_init_default_irq(base)	ide_default_irq(base)

static IDE_INLINE int
ide_request_irq(
	unsigned int irq,
	void (*handler)(int, void *, struct pt_regs *),
	unsigned long flags,
	const char *device,
	void *dev_id)
{
#ifdef CONFIG_COLDFIRE
	mcf_autovector(irq);
#endif
	return(request_irq(irq, handler, flags, device, dev_id));
}


static IDE_INLINE void
ide_free_irq(unsigned int irq, void *dev_id)
{
	free_irq(irq, dev_id);
}


static IDE_INLINE int
ide_check_region(ide_ioreg_t from, unsigned int extent)
{
	return 0;
}


static IDE_INLINE void
ide_request_region(ide_ioreg_t from, unsigned int extent, const char *name)
{
}


static IDE_INLINE void
ide_release_region(ide_ioreg_t from, unsigned int extent)
{
}


static IDE_INLINE void
ide_fix_driveid(struct hd_driveid *id)
{
#ifdef CONFIG_COLDFIRE
	int i, n;
	unsigned short *wp = (unsigned short *) id;
	int avoid[] = {49, 51, 52, 59, -1 }; /* do not swap these words */

	/* Need to byte swap shorts,  but not char fields */
	for (i = n = 0; i < sizeof(*id) / sizeof(*wp); i++, wp++) {
		if (avoid[n] == i) {
			n++;
			continue;
		}
		*wp = ((*wp & 0xff) << 8) | ((*wp >> 8) & 0xff);
	}
	/* have to word swap the one 32 bit field */
	id->lba_capacity = ((id->lba_capacity & 0xffff) << 16) |
				((id->lba_capacity >> 16) & 0xffff);
#endif
}


static IDE_INLINE void
ide_release_lock (int *ide_lock)
{
}


static IDE_INLINE void
ide_get_lock(
	int *ide_lock,
	void (*handler)(int, void *, struct pt_regs *),
	void *data)
{
}


#define ide_ack_intr(hwif) \
	((hwif)->hw.ack_intr ? (hwif)->hw.ack_intr(hwif) : 1)
#define	ide__sti()	__sti()

/****************************************************************************/
/*
 *	System specific IO requirements
 */

#ifdef CONFIG_COLDFIRE

#ifdef CONFIG_SECUREEDGEMP3

/* Replace standard IO functions for funky mapping of MP3 board */
#undef outb
#undef outb_p
#undef inb
#undef inb_p

#define outb(v, a)          ide_outb(v, (unsigned long) (a))
#define outb_p(v, a)        ide_outb(v, (unsigned long) (a))
#define inb(a)              ide_inb((unsigned long) (a))
#define inb_p(a)            ide_inb((unsigned long) (a))

#define ADDR8_PTR(addr)		(((addr) & 0x1) ? (0x8000 + (addr) - 1) : (addr))
#define ADDR16_PTR(addr)	(addr)
#define ADDR32_PTR(addr)	(addr)
#define SWAP8(w)			((((w) & 0xffff) << 8) | (((w) & 0xffff) >> 8))
#define SWAP16(w)			(w)
#define SWAP32(w)			(w)


static IDE_INLINE void
ide_outb(unsigned int val, unsigned int addr)
{
	volatile unsigned short	*rp;

	DBGIDE("%s(val=%x,addr=%x)\n", __FUNCTION__, val, addr);
	rp = (volatile unsigned short *) ADDR8_PTR(addr);
	*rp = SWAP8(val);
}


static IDE_INLINE int
ide_inb(unsigned int addr)
{
	volatile unsigned short	*rp, val;

	DBGIDE("%s(addr=%x)\n", __FUNCTION__, addr);
	rp = (volatile unsigned short *) ADDR8_PTR(addr);
	val = *rp;
	return(SWAP8(val));
}


static IDE_INLINE void
ide_outw(unsigned int val, unsigned int addr)
{
	volatile unsigned short	*rp;

	DBGIDE("%s(val=%x,addr=%x)\n", __FUNCTION__, val, addr);
	rp = (volatile unsigned short *) ADDR16_PTR(addr);
	*rp = SWAP16(val);
}

static IDE_INLINE void
ide_outsw(unsigned int addr, const void *vbuf, unsigned long len)
{
	volatile unsigned short	*rp, val;
	unsigned short   	*buf;

	DBGIDE("%s(addr=%x,vbuf=%p,len=%x)\n", __FUNCTION__, addr, vbuf, len);
	buf = (unsigned short *) vbuf;
	rp = (volatile unsigned short *) ADDR16_PTR(addr);
	for (; (len > 0); len--) {
		val = *buf++;
		*rp = SWAP16(val);
	}
}

static IDE_INLINE int
ide_inw(unsigned int addr)
{
	volatile unsigned short *rp, val;

	DBGIDE("%s(addr=%x)\n", __FUNCTION__, addr);
	rp = (volatile unsigned short *) ADDR16_PTR(addr);
	val = *rp;
	return(SWAP16(val));
}

static IDE_INLINE void
ide_insw(unsigned int addr, void *vbuf, unsigned long len)
{
	volatile unsigned short *rp;
	unsigned short          w, *buf;

	DBGIDE("%s(addr=%x,vbuf=%p,len=%x)\n", __FUNCTION__, addr, vbuf, len);
	buf = (unsigned short *) vbuf;
	rp = (volatile unsigned short *) ADDR16_PTR(addr);
	for (; (len > 0); len--) {
		w = *rp;
		*buf++ = SWAP16(w);
	}
}

static IDE_INLINE void
ide_insl(unsigned int addr, void *vbuf, unsigned long len)
{
	volatile unsigned long *rp;
	unsigned long          w, *buf;

	DBGIDE("%s(addr=%x,vbuf=%p,len=%x)\n", __FUNCTION__, addr, vbuf, len);
	buf = (unsigned long *) vbuf;
	rp = (volatile unsigned long *) ADDR32_PTR(addr);
	for (; (len > 0); len--) {
		w = *rp;
		*buf++ = SWAP32(w);
	}
}

static IDE_INLINE void
ide_outsl(unsigned int addr, const void *vbuf, unsigned long len)
{
	volatile unsigned long	*rp, val;
	unsigned long   	*buf;

	DBGIDE("%s(addr=%x,vbuf=%p,len=%x)\n", __FUNCTION__, addr, vbuf, len);
	buf = (unsigned long *) vbuf;
	rp = (volatile unsigned long *) ADDR32_PTR(addr);
	for (; (len > 0); len--) {
		val = *buf++;
		*rp = SWAP32(val);
	}
}

#elif CONFIG_eLIA

/* 8/16 bit acesses are controlled by flicking bits in the CS register */
#define	ACCESS_MODE_16BIT()	\
	*((volatile unsigned short *) (MCF_MBAR + MCFSIM_LOCALCS)) = 0x0080
#define	ACCESS_MODE_8BIT()	\
	*((volatile unsigned short *) (MCF_MBAR + MCFSIM_LOCALCS)) = 0x0040


static IDE_INLINE void
ide_outw(unsigned int val, unsigned int addr)
{
	ACCESS_MODE_16BIT();
	outw(val, addr);
	ACCESS_MODE_8BIT();
}

static IDE_INLINE void
ide_outsw(unsigned int addr, const void *vbuf, unsigned long len)
{
	ACCESS_MODE_16BIT();
	outsw(addr, vbuf, len);
	ACCESS_MODE_8BIT();
}

static IDE_INLINE int
ide_inw(unsigned int addr)
{
	int ret;

	ACCESS_MODE_16BIT();
	ret = inw(addr);
	ACCESS_MODE_8BIT();
	return(ret);
}

static IDE_INLINE void
ide_insw(unsigned int addr, void *vbuf, unsigned long len)
{
	ACCESS_MODE_16BIT();
	insw(addr, vbuf, len);
	ACCESS_MODE_8BIT();
}

static IDE_INLINE void
ide_insl(unsigned int addr, void *vbuf, unsigned long len)
{
	ACCESS_MODE_16BIT();
	insl(addr, vbuf, len);
	ACCESS_MODE_8BIT();
}

static IDE_INLINE void
ide_outsl(unsigned int addr, const void *vbuf, unsigned long len)
{
	ACCESS_MODE_16BIT();
	outsl(addr, vbuf, len);
	ACCESS_MODE_8BIT();
}

#endif /* CONFIG_SECUREEDGEMP3 */

#undef outw
#undef outw_p
#undef outsw
#undef inw
#undef inw_p
#undef insw
#undef insl
#undef outsl

#define	outw(v, a)	     ide_outw(v, (unsigned long) (a))
#define	outw_p(v, a)     ide_outw(v, (unsigned long) (a))
#define outsw(a, b, n)   ide_outsw((unsigned long) (a), b, n)
#define	inw(a)	         ide_inw((unsigned long) (a))
#define	inw_p(a)	     ide_inw((unsigned long) (a))
#define insw(a, b, n)    ide_insw((unsigned long) (a), b, n)
#define insl(a, b, n)    ide_insl((unsigned long) (a), b, n)
#define outsl(a, b, n)   ide_outsl((unsigned long) (a), b, n)

#endif CONFIG_COLDFIRE

/****************************************************************************/
#endif /* __KERNEL__ */
#endif /* _M68KNOMMU_IDE_H */
/****************************************************************************/
