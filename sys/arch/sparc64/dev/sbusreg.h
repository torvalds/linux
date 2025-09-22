/*	$OpenBSD: sbusreg.h,v 1.4 2008/07/14 20:01:37 miod Exp $	*/
/*	$NetBSD: sbusreg.h,v 1.7 1999/06/07 05:28:03 eeh Exp $ */

/*
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
 */


/*
 * SBus device addresses are obtained from the FORTH PROMs.  They come
 * in `absolute' and `relative' address flavors, so we have to handle both.
 * Relative addresses do *not* include the slot number.
 */
#define	SBUS_BASE		0xf8000000
#define	SBUS_ADDR(slot, off)	(SBUS_BASE + ((slot) << 25) + (off))
#define	SBUS_ABS(a)		((unsigned)(a) >= SBUS_BASE)
#define	SBUS_ABS_TO_SLOT(a)	(((a) - SBUS_BASE) >> 25)
#define	SBUS_ABS_TO_OFFSET(a)	(((a) - SBUS_BASE) & 0x1ffffff)

/*
 * Sun4u SBus definitions.  Here's where we deal w/the machine
 * dependencies of sysio.
 *
 * SYSIO implements or is the interface to several things:  
 *
 * o The SBus interface itself
 * o The IOMMU
 * o The DVMA units
 * o The interrupt controller
 * o The counter/timers
 *
 * Since it has registers to control lots of different things
 * as well as several on-board SBus devices and external SBus
 * slots scattered throughout its address space, it's a pain.
 *
 * One good point, however, is that all registers are 64-bit.
 */

struct sysioreg {
	struct upareg {
		u_int64_t	upa_portid;		/* UPA port ID register */		/* 1fe.0000.0000 */
		u_int64_t	upa_config;		/* UPA config register */		/* 1fe.0000.0008 */
	} sys_upa;

	u_int64_t	sys_csr;		/* SYSIO control/status register */	/* 1fe.0000.0010 */
	u_int64_t	pad0;
	u_int64_t	sys_ecccr;		/* ECC control register */		/* 1fe.0000.0020 */
	u_int64_t	reserved;							/* 1fe.0000.0028 */
	u_int64_t	sys_ue_afsr;		/* Uncorrectable Error AFSR */		/* 1fe.0000.0030 */
	u_int64_t	sys_ue_afar;		/* Uncorrectable Error AFAR */		/* 1fe.0000.0038 */
	u_int64_t	sys_ce_afsr;		/* Correctable Error AFSR */		/* 1fe.0000.0040 */
	u_int64_t	sys_ce_afar;		/* Correctable Error AFAR */		/* 1fe.0000.0048 */

	u_int64_t	pad1[22];

	struct perfmon {
		u_int64_t	pm_cr;			/* Performance monitor control reg */	/* 1fe.0000.0100 */
		u_int64_t	pm_count;		/* Performance monitor counter reg */	/* 1fe.0000.0108 */
	} sys_pm;

	u_int64_t	pad2[990];

	struct sbusreg {
		u_int64_t	sbus_cr;		/* SBus Control Register */		/* 1fe.0000.2000 */
		u_int64_t	reserved;							/* 1fe.0000.2008 */
		u_int64_t	sbus_afsr;		/* SBus AFSR */				/* 1fe.0000.2010 */
		u_int64_t	sbus_afar;		/* SBus AFAR */				/* 1fe.0000.2018 */
		u_int64_t	sbus_config0;	/* SBus Slot 0 config register */	/* 1fe.0000.2020 */
		u_int64_t	sbus_config1;	/* SBus Slot 1 config register */	/* 1fe.0000.2028 */
		u_int64_t	sbus_config2;	/* SBus Slot 2 config register */	/* 1fe.0000.2030 */
		u_int64_t	sbus_config3;	/* SBus Slot 3 config register */	/* 1fe.0000.2038 */
		u_int64_t	sbus_config13;	/* Slot 13 config register <audio> */	/* 1fe.0000.2040 */
		u_int64_t	sbus_config14;	/* Slot 14 config register <macio> */	/* 1fe.0000.2048 */
		u_int64_t	sbus_config15;	/* Slot 15 config register <slavio> */	/* 1fe.0000.2050 */
	} sys_sbus;

	u_int64_t	pad3[117];

	struct iommureg sys_iommu;							/* 1fe.0000.2400-25f8 */

	u_int64_t	pad4[64];

	struct iommu_strbuf	sys_strbuf;						/* 1fe.0000.2800-2810 */

	u_int64_t	pad5[125];

	u_int64_t	sbus_slot0_int;		/* SBus slot 0 interrupt map reg */	/* 1fe.0000.2c00 */
	u_int64_t	sbus_slot1_int;		/* SBus slot 1 interrupt map reg */	/* 1fe.0000.2c08 */
	u_int64_t	sbus_slot2_int;		/* SBus slot 2 interrupt map reg */	/* 1fe.0000.2c10 */
	u_int64_t	sbus_slot3_int;		/* SBus slot 3 interrupt map reg */	/* 1fe.0000.2c18 */
	u_int64_t	intr_retry;		/* interrupt retry timer reg */		/* 1fe.0000.2c20 */

	u_int64_t	pad6[123];

	u_int64_t	scsi_int_map;		/* SCSI interrupt map reg */		/* 1fe.0000.3000 */
	u_int64_t	ether_int_map;		/* ethernet interrupt map reg */	/* 1fe.0000.3008 */
	u_int64_t	bpp_int_map;		/* parallel interrupt map reg */	/* 1fe.0000.3010 */
	u_int64_t	audio_int_map;		/* audio interrupt map reg */		/* 1fe.0000.3018 */
	u_int64_t	power_int_map;		/* power fail interrupt map reg */	/* 1fe.0000.3020 */
	u_int64_t	ser_kbd_ms_int_map;	/* serial/kbd/mouse interrupt map reg *//* 1fe.0000.3028 */
	u_int64_t	fd_int_map;		/* floppy interrupt map reg */		/* 1fe.0000.3030 */
	u_int64_t	therm_int_map;		/* thermal warn interrupt map reg */	/* 1fe.0000.3038 */
	u_int64_t	kbd_int_map;		/* kbd [unused] interrupt map reg */	/* 1fe.0000.3040 */
	u_int64_t	mouse_int_map;		/* mouse [unused] interrupt map reg */	/* 1fe.0000.3048 */
	u_int64_t	serial_int_map;		/* second serial interrupt map reg */	/* 1fe.0000.3050 */
	u_int64_t	pad7;
	u_int64_t	timer0_int_map;		/* timer 0 interrupt map reg */		/* 1fe.0000.3060 */
	u_int64_t	timer1_int_map;		/* timer 1 interrupt map reg */		/* 1fe.0000.3068 */
	u_int64_t	ue_int_map;		/* UE interrupt map reg */		/* 1fe.0000.3070 */
	u_int64_t	ce_int_map;		/* CE interrupt map reg */		/* 1fe.0000.3078 */
	u_int64_t	sbus_async_int_map;	/* SBus error interrupt map reg */	/* 1fe.0000.3080 */
	u_int64_t	pwrmgt_int_map;		/* power mgmt wake interrupt map reg */	/* 1fe.0000.3088 */
	u_int64_t	upagr_int_map;		/* UPA graphics interrupt map reg */	/* 1fe.0000.3090 */
	u_int64_t	reserved_int_map;	/* reserved interrupt map reg */	/* 1fe.0000.3098 */

	u_int64_t	pad8[108];

	/* Note: clear interrupt 0 registers are not really used */
	u_int64_t	sbus0_clr_int[8];	/* SBus slot 0 clear int regs 0..7 */	/* 1fe.0000.3400-3438 */
	u_int64_t	sbus1_clr_int[8];	/* SBus slot 1 clear int regs 0..7 */	/* 1fe.0000.3440-3478 */
	u_int64_t	sbus2_clr_int[8];	/* SBus slot 2 clear int regs 0..7 */	/* 1fe.0000.3480-34b8 */
	u_int64_t	sbus3_clr_int[8];	/* SBus slot 3 clear int regs 0..7 */	/* 1fe.0000.34c0-34f8 */

	u_int64_t	pad9[96];

	u_int64_t	scsi_clr_int;		/* SCSI clear int reg */		/* 1fe.0000.3800 */
	u_int64_t	ether_clr_int;		/* ethernet clear int reg */		/* 1fe.0000.3808 */
	u_int64_t	bpp_clr_int;		/* parallel clear int reg */		/* 1fe.0000.3810 */
	u_int64_t	audio_clr_int;		/* audio clear int reg */		/* 1fe.0000.3818 */
	u_int64_t	power_clr_int;		/* power fail clear int reg */		/* 1fe.0000.3820 */
	u_int64_t	ser_kb_ms_clr_int;	/* serial/kbd/mouse clear int reg */	/* 1fe.0000.3828 */
	u_int64_t	fd_clr_int;		/* floppy clear int reg */		/* 1fe.0000.3830 */
	u_int64_t	therm_clr_int;		/* thermal warn clear int reg */	/* 1fe.0000.3838 */
	u_int64_t	kbd_clr_int;		/* kbd [unused] clear int reg */	/* 1fe.0000.3840 */
	u_int64_t	mouse_clr_int;		/* mouse [unused] clear int reg */	/* 1fe.0000.3848 */
	u_int64_t	serial_clr_int;		/* second serial clear int reg */	/* 1fe.0000.3850 */
	u_int64_t	pad10;
	u_int64_t	timer0_clr_int;		/* timer 0 clear int reg */		/* 1fe.0000.3860 */
	u_int64_t	timer1_clr_int;		/* timer 1 clear int reg */		/* 1fe.0000.3868 */
	u_int64_t	ue_clr_int;		/* UE clear int reg */			/* 1fe.0000.3870 */
	u_int64_t	ce_clr_int;		/* CE clear int reg */			/* 1fe.0000.3878 */
	u_int64_t	sbus_clr_async_int;	/* SBus error clr interrupt reg */	/* 1fe.0000.3880 */
	u_int64_t	pwrmgt_clr_int;		/* power mgmt wake clr interrupt reg */	/* 1fe.0000.3888 */

	u_int64_t	pad11[110];

	struct timer_counter {
		u_int64_t	tc_count;	/* timer/counter 0/1 count register */	/* ife.0000.3c00,3c10 */
		u_int64_t	tc_limit;	/* timer/counter 0/1 limit register */	/* ife.0000.3c08,3c18 */
	} tc[2];

	u_int64_t	pad12[252];

	u_int64_t	sys_svadiag;		/* SBus virtual addr diag reg */	/* 1fe.0000.4400 */
	
	u_int64_t	pad13[31];

	u_int64_t	iommu_queue_diag[16];	/* IOMMU LRU queue diag */		/* 1fe.0000.4500-457f */
	u_int64_t	tlb_tag_diag[16];	/* TLB tag diag */			/* 1fe.0000.4580-45ff */
	u_int64_t	tlb_data_diag[32];	/* TLB data RAM diag */			/* 1fe.0000.4600-46ff */

	u_int64_t	pad14[32];

	u_int64_t	sbus_int_diag;		/* SBus int state diag reg */		/* 1fe.0000.4800 */
	u_int64_t	obio_int_diag;		/* OBIO and misc int state diag reg */	/* 1fe.0000.4808 */

	u_int64_t	pad15[254];

	u_int64_t	strbuf_data_diag[128];	/* streaming buffer data RAM diag */	/* 1fe.0000.5000-53f8 */
	u_int64_t	strbuf_error_diag[128];	/* streaming buffer error status diag *//* 1fe.0000.5400-57f8 */
	u_int64_t	strbuf_pg_tag_diag[16];	/* streaming buffer page tag diag */	/* 1fe.0000.5800-5878 */
	u_int64_t	pad16[16];
	u_int64_t	strbuf_ln_tag_diag[16];	/* streaming buffer line tag diag */	/* 1fe.0000.5900-5978 */
};
