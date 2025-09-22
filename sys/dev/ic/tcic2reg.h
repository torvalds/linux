/*	$OpenBSD: tcic2reg.h,v 1.6 2023/04/11 00:45:08 jsg Exp $	*/
/*	$NetBSD: tcic2reg.h,v 1.1 1999/03/23 20:04:14 bad Exp $	*/

/*-
 * Copyright (c) 1998, 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christoph Badura.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * All information is from the Databook DB86082 TCIC PC Card Controller for
 * Notebook PCs -- Hardware Design Guide, March 22, 1994.
 */

#ifndef _TCIC2REG_H
#define _TCIC2REG_H
#define	TCIC_IOSIZE		16

/* TCIC primary registers */
#define	TCIC_R_DATA		0		/* Data register, 16 bit */
#define	TCIC_R_ADDR		2		/* Address register, 32 bit */
#define	TCIC_R_ADDR2		(TCIC_R_ADDR+2)	/* high word of addr. reg. */
#define	TCIC_R_SCTRL		6		/* Socket control reg., 8 bit */
#define	TCIC_R_SSTAT		7		/* Socket status reg., 8 bit */
#define	TCIC_R_MODE		8		/* Mode register, 8 bit */
#define	TCIC_R_PWR		9		/* Power control reg., 8 bit */
#define	TCIC_R_EDC		0xA		/* Error detect code, 16 bit */
#define	TCIC_R_ICSR		0xC		/* Interrupt ctrl/status, 8 bit */
#define	TCIC_R_IENA		0xD		/* Interrupt enable, 8 bit */
#define	TCIC_R_AUX		0xE		/* Auxiliary Register, 16 bit */

/*
 * TCIC auxiliary registers.
 * These are all 16 bit registers.
 * They are accessed by selecting the appropriate index in
 * bits 7:5 of the mode register.
 */
#define	TCIC_AR_MASK		0xe0		/* for masking the mode reg. */
#define	TCIC_AR_TCTL		0x00		/* timing control register */
#define	TCIC_AR_PCTL		0x20		/* programming pulse ctrl. */
#define	TCIC_AR_WCTL		0x40		/* wait state control */
#define	TCIC_AR_EXTERN		0x60		/* external access */
#define	TCIC_AR_PDATA		0x80		/* programming data */
#define	TCIC_AR_SYSCFG		0xA0		/* system configuration */
#define	TCIC_AR_ILOCK		0xC0		/* interlock control/status */
#define	TCIC_AR_TEST		0xE0		/* test */

/*
 * TCIC indirect registers.
 * These are all 16 bit.
 * They are accessed by selecting the appropriate address in
 * bits 9:0 of the address register with indirect register access mode
 * enabled.
 */
#define	TCIC_WR_MEM_BASE	0x100	/* base address */
#define	TCIC_WR_MEM_SHFT	3	/* log2 size of one reg set */
#define	TCIC_WR_MEXT_N(n)	((TCIC_WR_MEM_BASE+((n)<<TCIC_WR_MEM_SHFT))+0)
#define	TCIC_WR_MBASE_N(n)	((TCIC_WR_MEM_BASE+((n)<<TCIC_WR_MEM_SHFT))+2)
#define	TCIC_WR_MMAP_N(n)	((TCIC_WR_MEM_BASE+((n)<<TCIC_WR_MEM_SHFT))+4)
#define	TCIC_WR_MCTL_N(n)	((TCIC_WR_MEM_BASE+((n)<<TCIC_WR_MEM_SHFT))+6)

#define	TCIC_WR_IO_BASE		0x200	/* base address */
#define	TCIC_WR_IO_SHFT		2	/* log2 size of one reg set */
#define	TCIC_WR_IBASE_N(n)	((TCIC_WR_IO_BASE+((n)<<TCIC_WR_IO_SHFT))+0)
#define	TCIC_WR_ICTL_N(n)	((TCIC_WR_IO_BASE+((n)<<TCIC_WR_IO_SHFT))+2)

#define	TCIC_IR_SCF_BASE	0	/* base address */
#define	TCIC_IR_SCF_SHFT	3	/* log2 size of one reg set */
#define	TCIC_IR_SCF1_N(n)	((TCIC_IR_SCF_BASE+((n)<<TCIC_IR_SCF_SHFT))+0)
#define	TCIC_IR_SCF2_N(n)	((TCIC_IR_SCF_BASE+((n)<<TCIC_IR_SCF_SHFT))+2)


/* Bits in the ADDR2 register */
#define	TCIC_SS_SHIFT		12	/* location of socket select bits */
#define	TCIC_SS_MASK		(7<<(TCIC_SS_SHIFT))	/* socket select mask */

#define	TCIC_ADDR2_REG		(1 << 15)	/* select REG space */
#define	TCIC_ADDR2_SS_SHFT	TCIC_SS_SHIFT	/* select sockets the usual way */
#define	TCIC_ADDR2_SS_MASK	TCIC_SS_MASK	/* ditto */
#define	TCIC_ADDR2_INDREG	(1 << 11)	/* access indirect registers
						 * (not card data)
						 */
#define	TCIC_ADDR2_IO		(1 << 10)	/* select I/O cycles, readback
						 * card /IORD, /IOWR in diag-
						 * nostic mode.
						 */

/* Bits in address register */
#define	TCIC_ADDR_REG	(u_int32_t) TCIC_ADDR2_REG << 16)	/* OR with this for REG space */
#define	TCIC_ADDR_SS_SHFT	((u_int32_t) TCIC_ADDR2_SS_SHFT + 16)
						/* shift count, cast so that
						 * you'll get the right type
						 * if you use it but forget
						 * to cast the left arg.
						 */
#define	TCIC_ADDR_SS_MASK	((u_int32_t) TCIC_ADDR2_SS_MASK << 16)
#define	TCIC_ADDR_INDREG	((u_int32_t) TCIC_ADDR2_INDREG << 16)
#define	TCIC_ADDR_IO		((u_int32_t) TCIC_ADDR2_IO << 16)

#define	TCIC_ADDR_SPACE_SIZE	((u_int32_t) 1 << 26)
#define	TCIC_ADDR_MASK	(ADDR_SPACE_SIZE - 1)

/* The following bits are defined in diagnostic mode */
#define	TCIC_ADDR_DIAG_NREG	((u_int32_t) 1U << 31)	/* inverted! */
#define	TCIC_ADDR_DIAG_NCEH	((u_int32_t) 1U << 30)
#define	TCIC_ADDR_DIAG_NCEL	((u_int32_t) 1U << 29)
#define	TCIC_ADDR_DIAG_NCWR	((u_int32_t) 1U << 28)
#define	TCIC_ADDR_DIAG_NCRD	((u_int32_t) 1U << 27)
#define	TCIC_ADDR_DIAG_CRESET	((u_int32_t) 1U << 26)

/* Bits in socket control register */
#define	TCIC_SCTRL_ENA		(1 << 0)	/* enable access to card */
#define	TCIC_SCTRL_INCMODE	(3 << 3)	/* mask for increment mode:  */
#define	TCIC_SCTRL_INCMODE_AUTO	(3 << 3)	/*   auto-increment mode */
#define	TCIC_SCTRL_INCMODE_HOLD	(0 << 3)	/*   byte hold mode */
#define	TCIC_SCTRL_INCMODE_WORD	(1 << 3)	/*   word hold mode */
#define	TCIC_SCTRL_INCMODE_REG	(2 << 3)	/*   reg-space increment mode */
#define	TCIC_SCTRL_EDCSUM	(1 << 5)	/* if set, use checksum (not CRC) */
#define	TCIC_SCTRL_RESET	(1 << 7)	/* internal software reset */
#define	TCIC_SCTRL_RSVD		0x46		/* reserved bits, MBZ */

/* Bits in the socket status register */
#define	TCIC_SSTAT_6US		(1<<0)		/* 6 usec have elapsed */
#define	TCIC_SSTAT_10US		(1<<1)		/* 10 usec have elapsed */
#define	TCIC_SSTAT_PROGTIME	(1<<2)		/* programming pulse timeout */
#define	TCIC_SSTAT_LBAT1	(1<<3)		/* low battery 1 */
#define	TCIC_SSTAT_LBAT2	(1<<4)		/* low battery 2 */
#define	TCIC_SSTAT_BATOK	(0<<3)		/* battery is OK */
#define	TCIC_SSTAT_BATBAD1	(1<<3)		/* battery is low */
#define	TCIC_SSTAT_BATLO	(2<<3)		/* battery is getting low */
#define	TCIC_SSTAT_BATBAD2	(3<<3)		/* battery is low */
#define	TCIC_SSTAT_RDY		(1<<5)		/* card is ready (not busy) */
#define	TCIC_SSTAT_WP		(1<<6)		/* card is write-protected */
#define	TCIC_SSTAT_CD		(1<<7)		/* card present */
#define	TCIC_SSTAT_STAT_MASK	0xf8

/* Mode register contents (R_MODE) */
#define	TCIC_MODE_PGMMASK	(0x1F)		/* the programming mode bits */
#define	TCIC_MODE_NORMAL	(0)		/*   normal mode */
#define	TCIC_MODE_PGMWR		(1 << 0)	/*   assert /WR */
#define	TCIC_MODE_PGMRD		(1 << 1)	/*   assert /RD */
#define	TCIC_MODE_PGMCE		(1 << 2)	/*   assert /CEx */
#define	TCIC_MODE_PGMDBW	(1 << 3)	/*   databus in write mode */
#define	TCIC_MODE_PGMWORD	(1 << 4)	/*   word programming mode */

/* Power control register contents (R_PWR) */
#define	TCIC_PWR_VCC_SHFT	(0)		/* the VCC ctl shift */
#define	TCIC_PWR_VCC_MASK	(3 << TCIC_PWR_VCC_SHFT)

#define	TCIC_PWR_VPP_SHFT	(3)		/* the VPP ctl shift */
#define	TCIC_PWR_VPP_MASK	(3 << TCIC_PWR_VPP_SHFT)
#define	TCIC_PWR_ENA		(1 << 5)	/* on 084, successors, this
						 * must be set to turn on
						 * power.
						 */
#define	TCIC_PWR_VCC5V		(1 << 2)	/* enable +5 (not +3) */
#if 0
#define	TCIC_PWR_VOFF_POFF	(0)		/* turn off VCC, VPP */
#define	TCIC_PWR_VON_PVCC	(1)		/* turn on VCC, VPP=VCC */
#define	TCIC_PWR_VON_PVPP	(2)		/* turn on VCC, VPP=12V */
#define	TCIC_PWR_VON_POFF	(3)		/* turn on VCC, VPP=0V */
#endif
#define	TCIC_PWR_VCC_N(n)	(1<<((n)))	/* VCCSEL for socket n */
#define	TCIC_PWR_VPP_N(n)	(1<<(3+(n)))	/* VPPSEL for socket n */

#define	TCIC_PWR_CLIMENA	(1 << 6)	/* the current-limit enable */
#define	TCIC_PWR_CLIMSTAT	(1 << 7)	/* current limit sense (r/o) */

/* Bits in the icsr register. */
#define	TCIC_ICSR_IOCHK		(1<<7)		/* I/O check */
#define	TCIC_ICSR_CDCHG		(1<<6)		/* card status change, see SSTAT */
#define	TCIC_ICSR_ERR		(1<<5)		/* error condition */
#define	TCIC_ICSR_PROGTIME	(1<<4)		/* program timer ding */
#define	TCIC_ICSR_ILOCK		(1<<3)		/* interlock change */
#define	TCIC_ICSR_STOPCPU	(1<<2)		/* Stop CPU was asserted */
#define	TCIC_ICSR_SET		(1<<1)		/* (w/o) enable writes that set bits */
#define	TCIC_ICSR_CLEAR		(1<<0)		/* (w/o) enable writes that clear */
#define	TCIC_ICSR_JAM		(TCIC_ICSR_SET|TCIC_ICSR_CLEAR)
						/* jam value into ICSR */

/* bits in the interrupt enable register */
#define	TCIC_IENA_CDCHG		(1 << 6)	/* enable INT when ICSR_CDCHG is set */
#define	TCIC_IENA_ERR		(1 << 5)	/* enable INT when ICSR_ERR is set */
#define	TCIC_IENA_PROGTIME	(1 << 4)	/* enable INT when ICSR_PROGTIME " */
#define	TCIC_IENA_ILOCK		(1 << 3)	/* enable INT when ICSR_ILOCK is set */
#define	TCIC_IENA_CFG_MASK	(3 << 0)	/* select the bits for IRQ config: */
#define	TCIC_IENA_CFG_OFF	(0 << 0)	/* IRQ is high-impedance */
#define	TCIC_IENA_CFG_OD	(1 << 0)	/* IRQ is active low, open drain. */
#define	TCIC_IENA_CFG_LOW	(2 << 0)	/* IRQ is active low, totem pole */
#define	TCIC_IENA_CFG_HIGH	(3 << 0)	/* IRQ is active high, totem pole */
#define	TCIC_IENA_RSVD		0x84		/* reserved bits, MBZ */


/*
 * Bits in the auxiliary registers
 */

/* Bits in the timing control register (AR_TCTL) */
#define	TCIC_TCTL_6US_SHFT	(0)	/* the shift count for the 6 us ctr */
#define	TCIC_TCTL_10US_SHFT	(8)	/* the shift count for the 10 us ctr */
#define	TCIC_TCTL_6US_MASK	(0xFF << TCIC_TCTL_6US_SHFT)
#define	TCIC_TCTL_10US_MASK	(0xFF << TCIC_TCTL_10US_SHFT)

#define	TCIC_R_TCTL_6US	(TCIC_R_AUX + 0)	/* the byte access handle */
#define	TCIC_R_TCTL_10US	(TCIC_R_AUX + 1)	/* the byte access handle */

/* Bits in the programming pulse register (AR_PCTL) */
#define	TCIC_R_PULSE_LO	(TCIC_R_AUX + 0)
#define	TCIC_R_PULSE_HI	(TCIC_R_AUX + 1)

/* Bits in the wait state control register (AR_WCTL) */
#define	TCIC_WAIT_COUNT_MASK	(0x1F)	/* the count of 1/2 wait states */
#define	TCIC_WAIT_COUNT_SHFT	(0)	/* the wait-count shift */
#define	TCIC_WAIT_SYNC	(1 << 5)	/* set for synch, clear for asynch cycles */
#define	TCIC_WAIT_ASYNC	(0)

#define	TCIC_WAIT_SENSE	(1 << 6)	/* select rising (1) or falling (0)
					 * edge of wait clock as reference
					 * edge.
					 */
#define	TCIC_WAIT_SRC	(1 << 7)	/* select constant clock (0) or bus
					 * clock (1) as the timing source
					 */

/* Some derived constants */
#define	TCIC_WAIT_BCLK		(1 * TCIC_WAIT_SRC)
#define	TCIC_WAIT_CCLK		(0 * TCIC_WAIT_SRC)
#define	TCIC_WAIT_RISING	(1 * TCIC_WAIT_SENSE)
#define	TCIC_WAIT_FALLING	(0 * TCIC_WAIT_SENSE)

/* high byte */
#define	TCIC_WCTL_WR		(1 << 8)	/* control:  pulse write */
#define	TCIC_WCTL_RD		(1 << 9)	/* control:  pulse read */
#define	TCIC_WCTL_CE		(1 << 10)	/* control:  pulse chip ena */
#define	TCIC_WCTL_LLBAT1	(1 << 11)	/* status:  latched LBAT1 */
#define	TCIC_WCTL_LLBAT2	(1 << 12)	/* status:  latched LBAT2 */
#define	TCIC_WCTL_LRDY		(1 << 13)	/* status:  latched RDY */
#define	TCIC_WCTL_LWP		(1 << 14)	/* status:  latched WP */
#define	TCIC_WCTL_LCD		(1 << 15)	/* status:  latched CD */

/* The same thing, from a byte perspective */
#define	TCIC_R_WCTL_WAIT	(TCIC_R_AUX + 0)	/* the wait state control byte */
#define	TCIC_R_WCTL_XCSR	(TCIC_R_AUX + 1)	/* extended control/status */

#define	TCIC_XCSR_WR		(1 << 0)	/* control:  pulse write */
#define	TCIC_XCSR_RD		(1 << 1)	/* control:  pulse read */
#define	TCIC_XCSR_CE		(1 << 2)	/* control:  pulse chip ena */
#define	TCIC_XCSR_LLBAT1	(1 << 3)	/* status:  latched LBAT1 */
#define	TCIC_XCSR_LLBAT2	(1 << 4)	/* status:  latched LBAT2 */
#define	TCIC_XCSR_LRDY		(1 << 5)	/* status:  latched RDY */
#define	TCIC_XCSR_LWP		(1 << 6)	/* status:  latched WP */
#define	TCIC_XCSR_LCD		(1 << 7)	/* status:  latched CD */
#define	TCIC_XCSR_STAT_MASK	0xf8

/* Bits in the programming data register (AR_PDATA) */
#define	TCIC_R_PDATA_LO	(TCIC_R_AUX + 0)
#define	TCIC_R_PDATA_HI	(TCIC_R_AUX + 1)

/* Bits in the system configuration register (AR_SYSCFG) */
/*
 * The bottom four bits specify the steering of the socket IRQ.  On
 * the 2N, the socket IRQ is (by default) pointed at the dedicated
 * pin.
 */
#define	TCIC_SYSCFG_IRQ_MASK		(0xF)	/* mask for this bit field. */
#define	TCIC_SYSCFG_SSIRQDFLT		(0)	/* default:  use SKTIRQ (2/N)
						 *	disable (2/P)
						 */
#define	TCIC_SYSCFG_SSIRQ		(0x1)	/* use SKTIRQ (explicit) (2/N)
						 *	do not use (2/P)
						 */
#define	TCIC_SYSCFG_SIRQ3		(0x3)	/* use IRQ3 */
#define	TCIC_SYSCFG_SIRQ4		(0x4)	/* use IRQ4 */
#define	TCIC_SYSCFG_SIRQ5		(0x5)	/* use IRQ5 (2/N) */
#define	TCIC_SYSCFG_SIRQ6		(0x6)	/* use IRQ6 (2/N) */
#define	TCIC_SYSCFG_SIRQ7		(0x7)	/* use IRQ7 (2/N) */
#define	TCIC_SYSCFG_SIRQ10		(0xA)	/* use IRQ10 */
#define	TCIC_SYSCFG_SIRQ14		(0xE)	/* use IRQ14 */

#define	TCIC_SYSCFG_MCSFULL	(1 << 4)
	/*
	 * If set, use full address (a[12:23]) for MCS16 generation.
	 * If clear, run in ISA-compatible mode (only using a[17:23]).
	 * With many chip sets, the TCIC-2/N's timing will allow full
	 * address decoding to be used rather than limiting us to LA[17:23];
	 * thus we can get around the ISA spec which limits the granularity
	 * of bus sizing to 128K blocks.
	 */
#define	TCIC_SYSCFG_IO1723	(1 << 5)
	/*
	 * Flag indicating that LA[17:23] can be trusted to be zero during a
	 * true I/O cycle.  Setting this bit will allow us to reduce power
	 * consumption further by eliminating I/O address broadcasts for
	 * memory cycles.
	 *
	 * Unfortunately, you cannot trust LA[17:23] to be zero on all systems,
	 * because the ISA specs do not require that LA[17:23] be zero when an
	 * alternate bus master runs an I/O cycle.  However, on a palmtop or
	 * notebook, it is a good guess.
	 */

#define	TCIC_SYSCFG_MCSXB	(1 << 6)
	/*
	 * If set, assume presence of an external buffer for MCS16:  operate
	 * the driver as a totem-pole output.
	 * 
	 * If clear, run in pseudo-ISA mode; output is open drain.  But note
	 * that on the 082 the output buffers cannot drive a 300-ohm
	 * load.
	 */
#define	TCIC_SYSCFG_ICSXB	(1 << 7)
	/*
	 * If set, assume presence of an external buffer for IOCS16*; operate
	 * the buffer as a totem-pole output.
	 * 
	 * If clear, run in pseudo-ISA mode; output is open drain.  But note
	 * that on the 082 the output buffers cannot drive a 300-ohm
	 * load.
	 */
#define	TCIC_SYSCFG_NOPDN	(1 << 8)
	/*
	 * If set, disable the auto power-down sequencing.  The chip will
	 * run card cycles somewhat more quickly (though perhaps not
	 * significantly so); but it will dissipate significantly more power.
	 * 
	 * If clear, the low-power operating modes are enabled.  This
	 * causes the part to go into low-power mode automatically at
	 * system reset.
	 */
#define	TCIC_SYSCFG_MPSEL_SHFT	(9)
#define	TCIC_SYSCFG_MPSEL_MASK	(7 << 9)
	/*
	 * This field controls the operation of the multipurpose pin on the
	 * 86082.  It has the following codes:
	 */
#define	TCIC_SYSCFG_MPSEL_OFF	(0 << TCIC_SYSCFG_MPSEL_SHFT)
	/*
	 * This is the reset state; it indicates that the Multi-purpose
	 * pin is not used.  The pin will be held in a high-impedance
	 * state.  It can be read by monitoring SYSCFG_MPSENSE.
	 */
#define	TCIC_SYSCFG_MPSEL_NEEDCLK	(1 << TCIC_SYSCFG_MPSEL_SHFT)
	/*
	 * NMULTI is an output.
	 * External indication that CCLK or BCLK are needed in order
	 * to complete an internal operation.  External logic can use
	 * this to control the clocks coming to the chip.
	 */
#define	TCIC_SYSCFG_MPSEL_MIO	(2 << TCIC_SYSCFG_MPSEL_SHFT)
	/*
	 * NMULTI is an input; it is an unambiguous M/IO signal, issued
	 * with timing similar to the LA[] lines.
	 */
#define	TCIC_SYSCFG_MPSEL_EXTSEL	(3 << TCIC_SYSCFG_MPSEL_SHFT)
	/*
	 * NMULTI is an output; it is the external register select
	 * pulse, generated whenever software attempts to access
	 * aux register AR_EXTRN. Of course, the 86082 will ignore
	 * writes to AR_EXTRN, and will float the data bus if
	 * the CPU reads from AR_EXTRN.
	*/

/*				(4 << TCIC_SYSCFG_MPSEL_SHFT)	 is reserved */

#define	TCIC_SYSCFG_MPSEL_RI	(5 << TCIC_SYSCFG_MPSEL_SHFT)
	/*
	 * NMULTI is an output; it indicates a RI (active-going)
	 * transition has occurred lately on a an appropriately-
	 * configured socket.  The output is active low.
	 */
/*
 * Codes 4, 6 and 7 are reserved, and must NOT be output.  It is
 * indeed possibly hazardous to your system to encode values in
 * this field that do not match your hardware!
 */

/*				1 << 12		reserved */

#define	TCIC_SYSCFG_MPSENSE	(1 << 13)
	/*
	 * This bit, when read, returns the sense of the multi-purpose pin.
	 */

#define	TCIC_SYSCFG_AUTOBUSY	(1 << 14)
	/*
	 * This bit, when set, causes the busy led to be gated with the
	 * SYSCFG_ACC bit.  When clear, the busy led reflects whether the
	 * socket is actually enabled.  If AUTOBUSY is set and ACC is clear,
	 * then the busy light will be off, even if a socket is enabled.
	 * If AUTOBUSY is clear, then the busy light will be on if either
	 * socket is enabled.
	 * 
	 * Note, that when in a programming mode, you should either clear this
	 * bit (causing the busy light to be on whenever the socket is enabled)
	 * or set both this bit and the ACC bit (causing the light to be on
	 * all the time).
	 * 
	 * On the '084 and '184, this bit is per-socket.
	 */

#define	TCIC_SYSCFG_ACC	(1<<15)	
	/*
	 * This bit will be set automatically by the hardware whenever the CPU
	 * accesses data on a card.  It can be cleared under software control.
	 * 
	 * In AUTOBUSY mode, it has the additional effect of turning on the
	 * busy light.
	 * 
	 * Since we'll tristate the command lines as the card is going out of
	 * the socket, and since the shared lines idle low, there's no real
	 * danger if the busy light is off even though the socket is enabled.
	 * 
	 * On the '084 and '184, this bit is per-socket.
	 */


/* Bits in the ilock aux. register. */
#define	TCIC_ILOCK_OUT	(1 << 0)	/* interlock output
					 * per-socket on x84
					 */
#define	TCIC_ILOCK_SENSE	(1 << 1)	/* (r/o) interlock sense
						 *  0 -> /cilock not asserted;
						 *  1 -> /cilock is asserted.
						 * per-socket on x84.
						 */
#define	TCIC_ILOCK_CRESET	(1 << 2)	/* card reset output level(S) */
#define	TCIC_ILOCK_CRESENA	(1 << 3)	/* enable card reset output (S) */
#define	TCIC_ILOCK_CWAIT	(1 << 4)	/* enable card wait (S) */
#define	TCIC_ILOCK_CWAITSNS	(1 << 5)	/* (r/o) sense current state of wait
						 *  0 -> /cwait not asserted;
						 *  1 -> /cwait is asserted
						 * (S)
						 */
/* The shift count & mask for the hold-time control */
#define	TCIC_ILOCK_HOLD_SHIFT	6	/* shift count for the hold-time ctl (G) */
#define	TCIC_ILOCK_HOLD_MASK	(3 << TCIC_ILOCK_HOLD_SHIFT)

/*
 * Quick hold mode waits until we observe that the strobe is high,
 * guaranteeing 10ns or so of hold time.
 */
#define	TCIC_ILOCK_HOLD_QUICK	(0 << TCIC_ILOCK_HOLD_SHIFT)

/*
 * CCLK hold mode waits (asynchronously) for an edge on CCLK.  Minimum is 1
 * CCLK + epsilon; maximum is 2 CCLKs + epsilon.
 *
 * for the 86081 & '82, this mode enables the multi-step
 * sequencer that generates setup and hold times based on CCLK.  This
 * is the recommended mode of operation for the '81 and '82.
 *
 */
#define	TCIC_ILOCK_HOLD_CCLK	(3 << TCIC_ILOCK_HOLD_SHIFT)

/* The following bits are only present on the x84 and later parts */
#define	TCIC_ILOCK_INPACK	(1 << 11)	/* (r/o, S) this bit is a diagnostic
						 * read-back for card input
						 * acknowledge.
						 * The sense is inverted from
						 * the level at the pin.
						 */
#define	TCIC_ILOCK_CP0	(1 << 12)	/* (r/o, S) this bit is a diagnostic
					 * monitor for card present pin 0.
					 * The sense is inverted from the
					 * level at the pin.
					 */
#define	TCIC_ILOCK_CP1	(1 << 13)	/* (r/o, S) this bit is a diagnostic
					 * monitor for card present pin 1.
					 * The sense is inverted from the
					 * level at the pin.
					 */
#define	TCIC_ILOCK_VS1	(1 << 14)	/* (r/o, S) this bit is the primary
					 * monitor for Card Voltage Sense
					 * pin 1.
					 * The sense is inverted from the
					 * level at the pin.
					 */
#define	TCIC_ILOCK_VS2	(1 << 15)	/* (r/o, S) this bit is the primary
					 * monitor for Card Voltage Sense
					 * pin 2.
					 * The sense is inverted from the
					 * level at the pin.
					 */
/*
 *	Silicon Version Register
 *
 * In diagnostic mode, the high byte of the interlock register is defined
 * as the silicon identity byte.
 * 
 * In order to read this byte, the chip must be placed in diagnostic
 * mode by setting bit 15 of the TESTDIAG register.  (This may or may
 * not be enforced by the silicon.)
 * 
 * The layout is:
 * 
 * 	15 14 13 12 11 10 9 8    7 6 5 4 3 2 1 0
 * 	m  <-------ID------->	 <----ILOCK---->
 * 
 * The fields are:
 * 
 * m	Always reset.
 * 
 * ID	This field is one of the following:
 * 
 * 	0x02	the db86082
 * 	0x03	the db86082a
 * 	0x04	the db86084
 * 	0x05	the DB86072ES,	(Engineering Sample)
 * 	0x07	the db86082bES,	(Engineering Sample)
 * 	0x08	the db86084a
 * 	0x14	the DB86184
 * 	0x15	the DB86072,	(Production)
 * 	0x17	the db86082b,	(Production)
 */

/*
 * Defines for Chip IDs described above.
 *
 * Use the following convention for defining TCIC_CHIPID_DBxxxxxY:
 *
 *	TCIC_CHIPID_DBxxxxx_1		The First step of chip.
 *	TCIC_CHIPID_DBxxxxxA		The Second step of chip.
 *	TCIC_CHIPID_DBxxxxxB		The Third step of chip.
 *	TCIC_CHIPID_DBxxxxx...	The ... step of chip.
 *
 *	TCIC_CHIPID_DBxxxxx"step of chip"_ES	An Engineering Sample of chip.
 *
 */
#define	TCIC_CHIPID_DB86082_1		(0x02)
#define	TCIC_CHIPID_DB86082A		(0x03)
#define	TCIC_CHIPID_DB86082B_ES		(0x07)
#define	TCIC_CHIPID_DB86082B		(0x17)

#define	TCIC_CHIPID_DB86084_1		(0x04)
#define	TCIC_CHIPID_DB86084A		(0x08)

#define	TCIC_CHIPID_DB86184_1		(0x14)

#define	TCIC_CHIPID_DB86072_1_ES		(0x05)
#define	TCIC_CHIPID_DB86072_1		(0x15)


/* the high order bits (in diag mode) give the chip version */
#define	TCIC_R_ILOCK_ID		(TCIC_R_AUX + 1)

#define	TCIC_ILOCKTEST_ID_SHFT	8		/* the shift count */
#define	TCIC_ILOCKTEST_ID_MASK	(0x7F << TCIC_ILOCKTEST_ID_SHFT)
						/* the mask for the field */
/*
 * Use the following convention for defining TCIC_ILOCKTEST_DBxxxxxY:
 *
 *	TCIC_ILOCKTEST_DBxxxxx_1	The First step of chip.
 *	TCIC_ILOCKTEST_DBxxxxxA	The Second step of chip.
 *	TCIC_ILOCKTEST_DBxxxxxB	The Third step of chip.
 *	TCIC_ILOCKTEST_DBxxxxx...	The ... step of chip.
 *
 *	TCIC_ILOCKTEST_DBxxxxx"step of chip"_ES	An Engineering Sample of chip.
 *
 */
#define	TCIC_ILOCKTEST_TCIC2N_1		((TCIC_CHIPID_DB86082_1) << TCIC_ILOCKTEST_ID_SHFT)
#define	TCIC_ILOCKTEST_DB86082_1	TCIC_ILOCKTEST_TCIC2N_1
#define	TCIC_ILOCKTEST_TCIC2N_2		((TCIC_CHIPID_DB86082A) << TCIC_ILOCKTEST_ID_SHFT)
#define	TCIC_ILOCKTEST_DB86082A		TCIC_ILOCKTEST_TCIC2N_2
#define	TCIC_ILOCKTEST_TCIC2N_3		((TCIC_CHIPID_DB86082B_ES) << TCIC_ILOCKTEST_ID_SHFT)
#define	TCIC_ILOCKTEST_DB86082B_ES	TCIC_ILOCKTEST_TCIC2N_3

#define	TCIC_ILOCKTEST_DB86082B		((TCIC_CHIPID_DB86082B) << TCIC_ILOCKTEST_ID_SHFT)

#define	TCIC_ILOCKTEST_DB86084_1	((TCIC_CHIPID_DB86084_1) << TCIC_ILOCKTEST_ID_SHFT)
#define	TCIC_ILOCKTEST_DB86084A		((TCIC_CHIPID_DB86084A) << TCIC_ILOCKTEST_ID_SHFT)

#define	TCIC_ILOCKTEST_DB86184_1	((TCIC_CHIPID_DB86184_1) << TCIC_ILOCKTEST_ID_SHFT)

#define	TCIC_ILOCKTEST_DB86072_1	((TCIC_CHIPID_DB86072_1) << TCIC_ILOCKTEST_ID_SHFT)
#define	TCIC_ILOCKTEST_DB86072_1_ES	((TCIC_CHIPID_DB86072_1_ES) << TCIC_ILOCKTEST_ID_SHFT)


/* Bits in the test control register (AR_TEST) */
#define	TCIC_R_TEST	(TCIC_R_AUX + 0)
#define	TCIC_TEST_AEN	(1 << 0)	/* force card AEN */
#define	TCIC_TEST_CEN	(1 << 1)	/* force card CEN */
#define	TCIC_TEST_CTR	(1 << 2)	/* test programming pulse, address ctrs */
#define	TCIC_TEST_ENA	(1 << 3)	/* force card-present (for test), and
					 * special VPP test mode
					 */
#define	TCIC_TEST_IO	(1 << 4)	/* feed back some I/O signals
					 * internally.
					 */
#define	TCIC_TEST_OUT1	(1 << 5)	/* force special address output mode */
#define	TCIC_TEST_ZPB	(1 << 6)	/* enter ZPB test mode */
#define	TCIC_TEST_WAIT	(1 << 7)	/* force-enable WAIT pin */
#define	TCIC_TEST_PCTR	(1 << 8)	/* program counter in read-test mode */
#define	TCIC_TEST_VCTL	(1 << 9)	/* force-enable power-supply controls */
#define	TCIC_TEST_EXTA	(1 << 10)	/* external access doesn't override
					|| internal decoding.
					*/
#define	TCIC_TEST_DRIVECDB	(1 << 11)	/* drive the card data bus all the time */
#define	TCIC_TEST_ISTP	(1 << 12)	/* turn off CCLK to the interrupt CSR */
#define	TCIC_TEST_BSTP	(1 << 13)	/* turn off BCLK internal to the chip */
#define	TCIC_TEST_CSTP	(1 << 14)	/* turn off CCLK except to int CSR */
#define	TCIC_TEST_DIAG	(1 << 15)	/* enable diagnostic read-back mode */

/* Bits in the SCF1 register */
#define	TCIC_SCF1_IRQ_MASK	(0xF)	/* mask for this bit field */
#define	TCIC_SCF1_IRQOFF	(0)	/* disable */
#define	TCIC_SCF1_SIRQ		(0x1)	/* use SKTIRQ (2/N) */
#define	TCIC_SCF1_IRQ3		(0x3)	/* use IRQ3 */
#define	TCIC_SCF1_IRQ4		(0x4)	/* use IRQ4 */
#define	TCIC_SCF1_IRQ5		(0x5)	/* use IRQ5 */
#define	TCIC_SCF1_IRQ6		(0x6)	/* use IRQ6 */
#define	TCIC_SCF1_IRQ7		(0x7)	/* use IRQ7 */
#define	TCIC_SCF1_IRQ9		(0x9)	/* use IRQ9 */
#define	TCIC_SCF1_IRQ10		(0xA)	/* use IRQ10 */
#define	TCIC_SCF1_IRQ11		(0xB)	/* use IRQ11 */
#define	TCIC_SCF1_IRQ12		(0xC)	/* use IRQ12 */
#define	TCIC_SCF1_IRQ14		(0xE)	/* use IRQ14 */
#define	TCIC_SCF1_IRQ15		(0xF)	/* use IRQ15 */

/* XXX doc bug? -chb */
#define	TCIC_SCF1_IRQOD		(1 << 4)
#define	TCIC_SCF1_IRQOC		(0)		/* selected IRQ is
						 * open-collector, and active
						 * low; otherwise it's totem-
						 * pole and active hi.
						 */
#define	TCIC_SCF1_PCVT		(1 << 5)	/* convert level-mode IRQ
						 * to pulse mode, or stretch
						 * pulses from card.
						 */
#define	TCIC_SCF1_IRDY		(1 << 6)	/* interrupt from RDY (not
						 * from /IREQ).  Used with
						 * ATA drives.
						 */
#define	TCIC_SCF1_ATA		(1 << 7)	/* Special ATA drive mode.
						 * CEL/H become CE1/2 in
						 * the IDE sense; CEL is
						 * activated for even window
						 * matches, and CEH for
						 * odd window matches.
						 */
#define	TCIC_SCF1_DMA_SHIFT	8		/* offset to DMA selects; */
#define	TCIC_SCF1_DMA_MASK	(0x7 << IRSCFG_DMA_SHIFT)

#define	TCIC_SCF1_DMAOFF	(0 << IRSCFG_DMA_SHIFT)	/* disable DMA */
#define	TCIC_SCF1_DREQ2		(2 << IRSCFG_DMA_SHIFT)	/* enable DMA on DRQ2 */

#define	TCIC_SCF1_IOSTS		(1 << 11)	/* enable I/O status mode;
						 *  allows CIORD/CIOWR to
						 *  become low-Z.
						 */
#define	TCIC_SCF1_SPKR		(1 << 12)	/* enable SPKR output from
						 * this card
						 */
#define	TCIC_SCF1_FINPACK	(1 << 13)	/* force card input
						 * acknowledge during I/O
						 * cycles.  Has no effect
						 * if no windows map to card
						 */
#define	TCIC_SCF1_DELWR		(1 << 14)	/* force -all- data to
						 * meet 60ns setup time
						 * ("DELay WRite")
						 */
#define	TCIC_SCF1_HD7IDE	(1 << 15)	/* Enable special IDE
						 * data register mode:  odd
						 * byte addresses in odd
						 * I/O windows will not
						 * drive HD7.
						 */

/* Bits in the scrf2 register */
#define	TCIC_SCF2_RI	(1 << 0)		/* enable RI pin from STSCHG
						 * (2/N)
						 `*/
#define	TCIC_SCF2_IDBR	(1 << 1)		/* force I/O data bus routing
						 * for this socket, regardless
						 * of cycle type. (2/N)
						 `*/
#define	TCIC_SCF2_MDBR	(1 << 2)		/* force memory window data
						 * bus routing for this
						 * socket, regardless of cycle
						 * type. (2/N)
						 */
#define	TCIC_SCF2_MLBAT1	(1 << 3)	/* disable status change
						 * ints from LBAT1 (or
						 * "STSCHG"
						 */
#define	TCIC_SCF2_MLBAT2	(1 << 4)	/* disable status change
						 * ints from LBAT2 (or "SPKR")
						 */
#define	TCIC_SCF2_MRDY	(1 << 5)		/* disable status change ints
						 * from RDY/BSY (or /IREQ).
						 * note that you get ints on
						 * both high- and low-going
						 * edges if this is enabled.
						 */
#define	TCIC_SCF2_MWP	(1 << 6)		/* disable status-change ints
						 * from WP (or /IOIS16).
						 * If you're using status
						 * change ints, you better set
						 * this once an I/O window is
						 * enabled, before accessing
						 * it.
						 */
#define	TCIC_SCF2_MCD	(1 << 7)		/* disable status-change ints
						 * from Card Detect.
						 */

/*
 * note that these bits match the top 5 bits of the socket status register
 * in order and sense.
 */
#define	TCIC_SCF2_DMASRC_MASK	(0x3 << 8)	/* mask for this bit field */
						/*-- DMA Source --*/
#define	TCIC_SCF2_DRQ_BVD2	(0x0 << 8)	/*     BVD2       */
#define	TCIC_SCF2_DRQ_IOIS16	(0x1 << 8)	/*     IOIS16     */
#define	TCIC_SCF2_DRQ_INPACK	(0x2 << 8)	/*     INPACK     */
#define	TCIC_SCF2_DRQ_FORCE	(0x3 << 8)	/*     Force it   */

#define	TCIC_SCFS2_RSVD		(0xFC00)	/* top 6 bits are RFU */

/* Bits in the MBASE window registers */
#define	TCIC_MBASE_4K		(1 << 14)	/* window size  is 4K */
#define	TCIC_MBASE_ADDR_MASK	0x0fff		/* bits holding the address */

/* Bits in the MMAP window registers */
#define	TCIC_MMAP_ATTR		(1 << 15)	/* map attr or common space */
#define	TCIC_MMAP_ADDR_MASK	0x3fff		/* bits holding the address */

/* Bits in the MCTL window registers */
#define	TCIC_MCTL_ENA		(1 << 15)	/* enable this window */
#define	TCIC_MCTL_SS_SHIFT	12
#define	TCIC_MCTL_SS_MASK	(7 << TCIC_MCTL_SS_SHIFT) /* which socket does this window map to */
#define	TCIC_MCTL_B8		(1 << 11)	/* 8/16 bit access select */
#define	TCIC_MCTL_EDC		(1 << 10)	/* do EDC calc. on access */
#define	TCIC_MCTL_KE		(1 << 9)	/* accesses are cacheable */
#define	TCIC_MCTL_ACC		(1 << 8)	/* window has been accessed */
#define	TCIC_MCTL_WP		(1 << 7)	/* window is write protected */
#define	TCIC_MCTL_QUIET		(1 << 6)	/* enable quiet socket mode */
#define	TCIC_MCTL_WSCNT_MASK	0x0f		/* wait state counter */

/* Bits in the ICTL window registers */
#define	TCIC_ICTL_ENA		(1 << 15)	/* enable this window */
#define	TCIC_ICTL_SS_SHIFT	12
#define	TCIC_ICTL_SS_MASK	(7 << TCIC_ICTL_SS_SHIFT) /* which socket does this window map to */
#define	TCIC_ICTL_AUTOSZ	0		/* auto size 8/16 bit acc. */
#define	TCIC_ICTL_B8		(1 << 11)	/* all accesses 8 bit */
#define	TCIC_ICTL_B16		(1 << 10)	/* all accesses 16 bit */
#define	TCIC_ICTL_ATA		(3 << 10)	/* special ATA mode */
#define	TCIC_ICTL_TINY		(1 << 9)	/* window size 1 byte */
#define	TCIC_ICTL_ACC		(1 << 8)	/* window has been accessed */
#define	TCIC_ICTL_1K		(1 << 7)	/* only 10 bits io decoding */
#define	TCIC_ICTL_QUIET		(1 << 6)	/* enable quiet socket mode */
#define	TCIC_ICTL_PASS16	(1 << 5)	/* pass all 16 bits to card */
#define	TCIC_ICTL_WSCNT_MASK	0x0f		/* wait state counter */

/* Various validity tests */
/*
 * From Databook sample source:
 * MODE_AR_SYSCFG must have, with j = ***read*** (***, R_AUX)
 * and k = (j>>9)&7:
 *	if (k&4) k == 5
 *	And also:
 *	j&0x0f is none of 2, 8, 9, b, c, d, f
 *		if (j&8) must have (j&3 == 2)
 *		Can't have j==2
 */
#if 0
/* this is from the Databook sample code and apparently is wrong */
#define	INVALID_AR_SYSCFG(x)	((((x)&0x1000) && (((x)&0x0c00) != 0x0200)) \
				|| (((((x)&0x08) == 0) || (((x)&0x03) == 2)) \
				&& ((x) != 0x02)))
#else
#define	INVALID_AR_SYSCFG(x)	((((x)&0x0800) && (((x)&0x0600) != 0x0100)) \
				|| ((((((x)&0x08) == 0) && (((x)&0x03) == 2)) \
				    || (((x)&0x03) == 2)) \
				&& ((x) != 0x02)))
#endif
/* AR_ILOCK must have bits 6 and 7 the same: */
#define	INVALID_AR_ILOCK(x)	(((x)&0xc0)==0 || (((x)&0xc0)==0xc0))

/* AR_TEST has some reserved bits: */
#define	INVALID_AR_TEST(x)	(((x)&0154) != 0)


#define	TCIC_IO_WINS	2
#define	TCIC_MAX_MEM_WINS	5

/*
 * Memory window addresses refer to bits A23-A12 of the ISA system memory
 * address.  This is a shift of 12 bits.  The LSB contains A19-A12, and the
 * MSB contains A23-A20, plus some other bits.
 */

#define	TCIC_MEM_SHIFT	12
#define	TCIC_MEM_PAGESIZE	(1<<TCIC_MEM_SHIFT)

#endif	/* _TCIC2REG_H */
