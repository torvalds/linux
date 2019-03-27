/*-
 * Copyright (c) 1996-1999 Eduardo Horvath
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR  ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR  BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: NetBSD: sbusreg.h,v 1.7 1999/06/07 05:28:03 eeh Exp
 *
 * $FreeBSD$
 */

#ifndef _SPARC64_SBUS_SBUSREG_H_
#define _SPARC64_SBUS_SBUSREG_H_

/*
 * Sbus device addresses are obtained from the FORTH PROMs.  They come
 * in `absolute' and `relative' address flavors, so we have to handle both.
 * Relative addresses do *not* include the slot number.
 */
#define	SBUS_BASE		0xf8000000
#define	SBUS_ADDR(slot, off)	(SBUS_BASE + ((slot) << 25) + (off))
#define	SBUS_ABS(a)		((unsigned)(a) >= SBUS_BASE)
#define	SBUS_ABS_TO_SLOT(a)	(((a) - SBUS_BASE) >> 25)
#define	SBUS_ABS_TO_OFFSET(a)	(((a) - SBUS_BASE) & 0x1ffffff)

/*
 * Sun4u S-bus definitions.  Here's where we deal w/the machine
 * dependencies of sysio.
 *
 * SYSIO implements or is the interface to several things:
 *
 * o The SBUS interface itself
 * o The IOMMU
 * o The DVMA units
 * o The interrupt controller
 * o The counter/timers
 *
 * Since it has registers to control lots of different things
 * as well as several on-board SBUS devices and external SBUS
 * slots scattered throughout its address space, it's a pain.
 *
 * One good point, however, is that all registers are 64-bit.
 */
#define	SBR_UPA_PORTID		0x0000	/* UPA port ID register */
#define	SBR_UPA_CONFIG		0x0008	/* UPA config register */
#define	SBR_CS			0x0010	/* SYSIO control/status register */
#define	SBR_ECCC		0x0020	/* ECC control register */
#define	SBR_UE_AFS		0x0030	/* Uncorrectable Error AFSR */
#define	SBR_UE_AFA		0x0038	/* Uncorrectable Error AFAR */
#define	SBR_CE_AFS		0x0040	/* Correctable Error AFSR */
#define	SBR_CE_AFA		0x0048	/* Correctable Error AFAR */
#define	SBR_PM_CTL		0x0100	/* Performance monitor control reg */
#define	SBR_PM_COUNT		0x0108	/* Performance monitor counter reg */
#define	SBR_CTL			0x2000	/* SBUS Control Register */
#define	SBR_AFS			0x2010	/* SBUS AFSR */
#define	SBR_AFA			0x2018	/* SBUS AFAR */
#define	SBR_CONFIG0		0x2020	/* SBUS Slot 0 config register */
#define	SBR_CONFIG1		0x2028	/* SBUS Slot 1 config register */
#define	SBR_CONFIG2		0x2030	/* SBUS Slot 2 config register */
#define	SBR_CONFIG3		0x2038	/* SBUS Slot 3 config register */
#define	SBR_CONFIG13		0x2040	/* Slot 13 config register <audio> */
#define	SBR_CONFIG14		0x2048	/* Slot 14 config register <macio> */
#define	SBR_CONFIG15		0x2050	/* Slot 15 config register <slavio> */
#define	SBR_IOMMU		0x2400	/* IOMMU register block */
#define	SBR_STRBUF		0x2800	/* stream buffer register block */
#define	SBR_SLOT0_INT_MAP	0x2c00	/* SBUS slot 0 interrupt map reg */
#define	SBR_SLOT1_INT_MAP	0x2c08	/* SBUS slot 1 interrupt map reg */
#define	SBR_SLOT2_INT_MAP	0x2c10	/* SBUS slot 2 interrupt map reg */
#define	SBR_SLOT3_INT_MAP	0x2c18	/* SBUS slot 3 interrupt map reg */
#define	SBR_INTR_RETRY_TIM	0x2c20	/* interrupt retry timer reg */
#define	SBR_SCSI_INT_MAP	0x3000	/* SCSI interrupt map reg */
#define	SBR_ETHER_INT_MAP	0x3008	/* ethernet interrupt map reg */
#define	SBR_BPP_INT_MAP		0x3010	/* parallel interrupt map reg */
#define	SBR_AUDIO_INT_MAP	0x3018	/* audio interrupt map reg */
#define	SBR_POWER_INT_MAP	0x3020	/* power fail interrupt map reg */
#define	SBR_SKBDMS_INT_MAP	0x3028	/* serial/kbd/mouse interrupt map reg */
#define	SBR_FD_INT_MAP		0x3030	/* floppy interrupt map reg */
#define	SBR_THERM_INT_MAP	0x3038	/* thermal warn interrupt map reg */
#define	SBR_KBD_INT_MAP		0x3040	/* kbd [unused] interrupt map reg */
#define	SBR_MOUSE_INT_MAP	0x3048	/* mouse [unused] interrupt map reg */
#define	SBR_SERIAL_INT_MAP	0x3050	/* second serial interrupt map reg */
#define	SBR_TIMER0_INT_MAP	0x3060	/* timer 0 interrupt map reg */
#define	SBR_TIMER1_INT_MAP	0x3068	/* timer 1 interrupt map reg */
#define	SBR_UE_INT_MAP		0x3070	/* UE interrupt map reg */
#define	SBR_CE_INT_MAP		0x3078	/* CE interrupt map reg */
#define	SBR_ASYNC_INT_MAP	0x3080	/* SBUS error interrupt map reg */
#define	SBR_PWRMGT_INT_MAP	0x3088	/* power mgmt wake interrupt map reg */
#define	SBR_UPAGR_INT_MAP	0x3090	/* UPA graphics interrupt map reg */
#define	SBR_RESERVED_INT_MAP	0x3098	/* reserved interrupt map reg */
/*
 * Note: clear interrupt 0 registers are not really used
 */
#define	SBR_SLOT0_INT_CLR	0x3400	/* SBUS slot 0 clear int regs 0..7 */
#define	SBR_SLOT1_INT_CLR	0x3440	/* SBUS slot 1 clear int regs 0..7 */
#define	SBR_SLOT2_INT_CLR	0x3480	/* SBUS slot 2 clear int regs 0..7 */
#define	SBR_SLOT3_INT_CLR	0x34c0	/* SBUS slot 3 clear int regs 0..7 */
#define	SBR_SCSI_INT_CLR	0x3800	/* SCSI clear int reg */
#define	SBR_ETHER_INT_CLR	0x3808	/* ethernet clear int reg */
#define	SBR_BPP_INT_CLR		0x3810	/* parallel clear int reg */
#define	SBR_AUDIO_INT_CLR	0x3818	/* audio clear int reg */
#define	SBR_POWER_INT_CLR	0x3820	/* power fail clear int reg */
#define	SBR_SKBDMS_INT_CLR	0x3828	/* serial/kbd/mouse clear int reg */
#define	SBR_FD_INT_CLR		0x3830	/* floppy clear int reg */
#define	SBR_THERM_INT_CLR	0x3838	/* thermal warn clear int reg */
#define	SBR_KBD_INT_CLR		0x3840	/* kbd [unused] clear int reg */
#define	SBR_MOUSE_INT_CLR	0x3848	/* mouse [unused] clear int reg */
#define	SBR_SERIAL_INT_CLR	0x3850	/* second serial clear int reg */
#define	SBR_TIMER0_INT_CLR	0x3860	/* timer 0 clear int reg */
#define	SBR_TIMER1_INT_CLR	0x3868	/* timer 1 clear int reg */
#define	SBR_UE_INT_CLR		0x3870	/* UE clear int reg */
#define	SBR_CE_INT_CLR		0x3878	/* CE clear int reg */
#define	SBR_ASYNC_INT_CLR	0x3880	/* SBUS error clr interrupt reg */
#define	SBR_PWRMGT_INT_CLR	0x3888	/* power mgmt wake clr interrupt reg */
#define	SBR_TC0			0x3c00	/* timer/counter 0 */
#define	SBR_TC1			0x3c10	/* timer/counter 1 */
#define	SBR_IOMMU_SVADIAG	0x4400	/* SBUS virtual addr diag reg */
#define	SBR_IOMMU_QUEUE_DIAG	0x4500	/* IOMMU LRU queue diag 0..15 */
#define	SBR_IOMMU_TLB_TAG_DIAG	0x4580	/* TLB tag diag 0..15 */
#define	SBR_IOMMU_TLB_DATA_DIAG	0x4600 	/* TLB data RAM diag 0..31 */
#define	SBR_INT_DIAG		0x4800	/* SBUS int state diag reg */
#define	SBR_OBIO_DIAG		0x4808	/* OBIO and misc int state diag reg */
#define	SBR_STRBUF_DIAG		0x5000	/* Streaming buffer diag regs */

/* INO defines */
#define	SBUS_MAX_INO		0x3f

/* Width of the physical addresses the IOMMU translates to */
#define	SBUS_IOMMU_BITS	41

#endif /* _SPARC64_SBUS_SBUSREG_H_ */
