/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 John Birrell
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* AMD Elan SC520 Memory Mapped Configuration Region (MMCR).
 *
 * The layout of this structure is documented by AMD in the Elan SC520
 * Microcontroller Register Set Manual. The field names match those
 * described in that document. The overall structure size must be 4096
 * bytes. Ignore fields with the 'pad' prefix - they are only present for
 * alignment purposes.
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_ELAN_MMCR_H_
#define	_MACHINE_ELAN_MMCR_H_ 1

struct elan_mmcr {
	/* CPU */
	u_int16_t	REVID;
	u_int8_t	CPUCTL;
	u_int8_t	pad_0x003[0xd];

	/* SDRAM Controller */
	u_int16_t	DRCCTL;
	u_int16_t	DRCTMCTL;
	u_int16_t	DRCCFG;
	u_int16_t	DRCBENDADR;
	u_int8_t	pad_0x01a[0x6];
	u_int8_t	ECCCTL;
	u_int8_t	ECCSTA;
	u_int8_t	ECCCKBPOS;
	u_int8_t	ECCCKTEST;
	u_int32_t	ECCSBADD;
	u_int32_t	ECCMBADD;
	u_int8_t	pad_0x02c[0x14];

	/* SDRAM Buffer */
	u_int8_t	DBCTL;
	u_int8_t	pad_0x041[0xf];

	/* ROM/Flash Controller */
	u_int16_t	BOOTCSCTL;
	u_int8_t	pad_0x052[0x2];
	u_int16_t	ROMCS1CTL;
	u_int16_t	ROMCS2CTL;
	u_int8_t	pad_0x058[0x8];

	/* PCI Bus Host Bridge */
	u_int16_t	HBCTL;
	u_int16_t	HBTGTIRQCTL;
	u_int16_t	HBTGTIRQSTA;
	u_int16_t	HBMSTIRQCTL;
	u_int16_t	HBMSTIRQSTA;
	u_int8_t	pad_0x06a[0x2];
	u_int32_t	MSTINTADD;

	/* System Arbitration */
	u_int8_t	SYSARBCTL;
	u_int8_t	PCIARBSTA;
	u_int16_t	SYSARBMENB;
	u_int32_t	ARBPRICTL;
	u_int8_t	pad_0x078[0x8];

	/* System Address Mapping */
	u_int32_t	ADDDECCTL;
	u_int32_t	WPVSTA;
	u_int32_t	PAR0;
	u_int32_t	PAR1;
	u_int32_t	PAR2;
	u_int32_t	PAR3;
	u_int32_t	PAR4;
	u_int32_t	PAR5;
	u_int32_t	PAR6;
	u_int32_t	PAR7;
	u_int32_t	PAR8;
	u_int32_t	PAR9;
	u_int32_t	PAR10;
	u_int32_t	PAR11;
	u_int32_t	PAR12;
	u_int32_t	PAR13;
	u_int32_t	PAR14;
	u_int32_t	PAR15;
	u_int8_t	pad_0x0c8[0xb38];

	/* GP Bus Controller */
	u_int8_t	GPECHO;
	u_int8_t	GPCSDW;
	u_int16_t	GPCSQUAL;
	u_int8_t	pad_0xc04[0x4];
	u_int8_t	GPCSRT;
	u_int8_t	GPCSPW;
	u_int8_t	GPCSOFF;
	u_int8_t	GPRDW;
	u_int8_t	GPRDOFF;
	u_int8_t	GPWRW;
	u_int8_t	GPWROFF;
	u_int8_t	GPALEW;
	u_int8_t	GPALEOFF;
	u_int8_t	pad_0xc11[0xf];

	/* Programmable Input/Output */
	u_int16_t	PIOPFS15_0;
	u_int16_t	PIOPFS31_16;
	u_int8_t	CSPFS;
	u_int8_t	pad_0xc25;
	u_int8_t	CLKSEL;
	u_int8_t	pad_0xc27;
	u_int16_t	DSCTL;
	u_int16_t	PIODIR15_0;
	u_int16_t	PIODIR31_16;
	u_int8_t	 pad_0xc2e[0x2];
	u_int16_t	PIODATA15_0;
	u_int16_t	PIODATA31_16;
	u_int16_t	PIOSET15_0;
	u_int16_t	PIOSET31_16;
	u_int16_t	PIOCLR15_0;
	u_int16_t	PIOCLR31_16;
	u_int8_t	pad_0xc3c[0x24];

	/* Software Timer */
	u_int16_t	SWTMRMILLI;
	u_int16_t	SWTMRMICRO;
	u_int8_t	SWTMRCFG;
	u_int8_t	pad_0xc65[0xb];

	/* General-Purpose Timers */
	u_int8_t	GPTMRSTA;
	u_int8_t	pad_0xc71;
	u_int16_t	GPTMR0CTL;
	u_int16_t	GPTMR0CNT;
	u_int16_t	GPTMR0MAXCMPA;
	u_int16_t	GPTMR0MAXCMPB;
	u_int16_t	GPTMR1CTL;
	u_int16_t	GPTMR1CNT;
	u_int16_t	GPTMR1MAXCMPA;
	u_int16_t	GPTMR1MAXCMPB;
	u_int16_t	GPTMR2CTL;
	u_int16_t	GPTMR2CNT;
	u_int8_t	pad_0xc86[0x8];
	u_int16_t	GPTMR2MAXCMPA;
	u_int8_t	pad_0xc90[0x20];

	/* Watchdog Timer */
	u_int16_t	WDTMRCTL;
	u_int16_t	WDTMRCNTL;
	u_int16_t	WDTMRCNTH;
	u_int8_t	pad_0xcb6[0xa];

	/* UART Serial Ports */
	u_int8_t	UART1CTL;
	u_int8_t	UART1STA;
	u_int8_t	UART1FCRSHAD;
	u_int8_t	pad_0xcc3;
	u_int8_t	UART2CTL;
	u_int8_t	UART2STA;
	u_int8_t	UART2FCRSHAD;
	u_int8_t	pad_0xcc7[0x9];

	/* Synchronous Serial Interface */
	u_int8_t	SSICTL;
	u_int8_t	SSIXMIT;
	u_int8_t	SSICMD;
	u_int8_t	SSISTA;
	u_int8_t	SSIRCV;
	u_int8_t	pad_0xcd5[0x2b];

	/* Programmable Interrupt Controller */
	u_int8_t	PICICR;
	u_int8_t	pad_0xd01;
	u_int8_t	MPICMODE;
	u_int8_t	SL1PICMODE;
	u_int8_t	SL2PICMODE;
	u_int8_t	pad_0xd05[0x3];
	u_int16_t	SWINT16_1;
	u_int8_t	SWINT22_17;
	u_int8_t	pad_0xd0b[0x5];
	u_int16_t	INTPINPOL;
	u_int8_t	pad_0xd12[0x2];
	u_int16_t	PCIHOSTMAP;
	u_int8_t	pad_0xd16[0x2];
	u_int16_t	ECCMAP;
	u_int8_t	GPTMR0MAP;
	u_int8_t	GPTMR1MAP;
	u_int8_t	GPTMR2MAP;
	u_int8_t	pad_0xd1d[0x3];
	u_int8_t	PIT0MAP;
	u_int8_t	PIT1MAP;
	u_int8_t	PIT2MAP;
	u_int8_t	pad_0xd23[0x5];
	u_int8_t	UART1MAP;
	u_int8_t	UART2MAP;
	u_int8_t	pad_0xd2a[0x6];
	u_int8_t	PCIINTAMAP;
	u_int8_t	PCIINTBMAP;
	u_int8_t	PCIINTCMAP;
	u_int8_t	PCIINTDMAP;
	u_int8_t	pad_0xd34[0xc];
	u_int8_t	DMABCINTMAP;
	u_int8_t	SSIMAP;
	u_int8_t	WDTMAP;
	u_int8_t	RTCMAP;
	u_int8_t	WPVMAP;
	u_int8_t	ICEMAP;
	u_int8_t	FERRMAP;
	u_int8_t	pad_0xd47[0x9];
	u_int8_t	GP0IMAP;
	u_int8_t	GP1IMAP;
	u_int8_t	GP2IMAP;
	u_int8_t	GP3IMAP;
	u_int8_t	GP4IMAP;
	u_int8_t	GP5IMAP;
	u_int8_t	GP6IMAP;
	u_int8_t	GP7IMAP;
	u_int8_t	GP8IMAP;
	u_int8_t	GP9IMAP;
	u_int8_t	GP10IMAP;
	u_int8_t	pad_0xd5b[0x15];

	/* Reset Generation */
	u_int8_t	SYSINFO;
	u_int8_t	pad_0xd71;
	u_int8_t	RESCFG;
	u_int8_t	pad_0xd73;
	u_int8_t	RESSTA;
	u_int8_t	pad_0xd75[0xb];

	/* GP DMA Controller */
	u_int8_t	GPDMACTL;
	u_int8_t	GPDMAMMIO;
	u_int16_t	GPDMAEXTCHMAPA;
	u_int16_t	GPDMAEXTCHMAPB;
	u_int8_t	GPDMAEXTPG0;
	u_int8_t	GPDMAEXTPG1;
	u_int8_t	GPDMAEXTPG2;
	u_int8_t	GPDMAEXTPG3;
	u_int8_t	GPDMAEXTPG5;
	u_int8_t	GPDMAEXTPG6;
	u_int8_t	GPDMAEXTPG7;
	u_int8_t	pad_0xd8d[0x3];
	u_int8_t	GPDMAEXTTC3;
	u_int8_t	GPDMAEXTTC5;
	u_int8_t	GPDMAEXTTC6;
	u_int8_t	GPDMAEXTTC7;
	u_int8_t	pad_0xd94[0x4];
	u_int8_t	GPDMABCCTL;
	u_int8_t	GPDMABCSTA;
	u_int8_t	GPDMABSINTENB;
	u_int8_t	GPDMABCVAL;
	u_int8_t	pad_0xd9c[0x4];
	u_int16_t	GPDMANXTADDL3;
	u_int16_t	GPDMANXTADDH3;
	u_int16_t	GPDMANXTADDL5;
	u_int16_t	GPDMANXTADDH5;
	u_int16_t	GPDMANXTADDL6;
	u_int16_t	GPDMANXTADDH6;
	u_int16_t	GPDMANXTADDL7;
	u_int16_t	GPDMANXTADDH7;
	u_int16_t	GPDMANXTTCL3;
	u_int8_t	GPDMANXTTCH3;
	u_int8_t	pad_0xdb3;
	u_int16_t	GPDMANXTTCL5;
	u_int8_t	GPDMANXTTCH5;
	u_int8_t	pad_0xdb7;
	u_int16_t	GPDMANXTTCL6;
	u_int8_t	GPDMANXTTCH6;
	u_int8_t	pad_0xdbb;
	u_int16_t	GPDMANXTTCL7;
	u_int8_t	GPDMANXTTCH7;
	u_int8_t	pad_0xdc0[0x240];
	};

CTASSERT(sizeof(struct elan_mmcr) == 4096);

extern volatile struct elan_mmcr * elan_mmcr;

#endif /* _MACHINE_ELAN_MMCR_H_ */
