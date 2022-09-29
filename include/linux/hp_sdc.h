/*
 * HP i8042 System Device Controller -- header
 *
 * Copyright (c) 2001 Brian S. Julin
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *
 * References:
 * 
 * HP-HIL Technical Reference Manual.  Hewlett Packard Product No. 45918A
 *
 * System Device Controller Microprocessor Firmware Theory of Operation
 * 	for Part Number 1820-4784 Revision B.  Dwg No. A-1820-4784-2
 *
 */

#ifndef _LINUX_HP_SDC_H
#define _LINUX_HP_SDC_H

#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/time.h>
#include <linux/timer.h>
#if defined(__hppa__)
#include <asm/hardware.h>
#endif


/* No 4X status reads take longer than this (in usec).
 */
#define HP_SDC_MAX_REG_DELAY 20000

typedef void (hp_sdc_irqhook) (int irq, void *dev_id, 
			       uint8_t status, uint8_t data);

int hp_sdc_request_timer_irq(hp_sdc_irqhook *callback);
int hp_sdc_request_hil_irq(hp_sdc_irqhook *callback);
int hp_sdc_request_cooked_irq(hp_sdc_irqhook *callback);
int hp_sdc_release_timer_irq(hp_sdc_irqhook *callback);
int hp_sdc_release_hil_irq(hp_sdc_irqhook *callback);
int hp_sdc_release_cooked_irq(hp_sdc_irqhook *callback);

typedef struct {
	int actidx;	/* Start of act.  Acts are atomic WRT I/O to SDC */
	int idx;	/* Index within the act */
	int endidx;	/* transaction is over and done if idx == endidx */
	uint8_t *seq;	/* commands/data for the transaction */
	union {
	  hp_sdc_irqhook   *irqhook;	/* Callback, isr or tasklet context */
	  struct semaphore *semaphore;	/* Semaphore to sleep on. */
	} act;
} hp_sdc_transaction;
int __hp_sdc_enqueue_transaction(hp_sdc_transaction *this);
int hp_sdc_enqueue_transaction(hp_sdc_transaction *this);
int hp_sdc_dequeue_transaction(hp_sdc_transaction *this);

/* The HP_SDC_ACT* values are peculiar to this driver.
 * Nuance: never HP_SDC_ACT_DATAIN | HP_SDC_ACT_DEALLOC, use another
 * act to perform the dealloc.
 */
#define HP_SDC_ACT_PRECMD	0x01		/* Send a command first */
#define HP_SDC_ACT_DATAREG	0x02		/* Set data registers */
#define HP_SDC_ACT_DATAOUT	0x04		/* Send data bytes */
#define HP_SDC_ACT_POSTCMD      0x08            /* Send command after */
#define HP_SDC_ACT_DATAIN	0x10		/* Collect data after */
#define HP_SDC_ACT_DURING	0x1f
#define HP_SDC_ACT_SEMAPHORE    0x20            /* Raise semaphore after */
#define HP_SDC_ACT_CALLBACK	0x40		/* Pass data to IRQ handler */
#define HP_SDC_ACT_DEALLOC	0x80		/* Destroy transaction after */
#define HP_SDC_ACT_AFTER	0xe0
#define HP_SDC_ACT_DEAD		0x60		/* Act timed out. */

/* Rest of the flags are straightforward representation of the SDC interface */
#define HP_SDC_STATUS_IBF	0x02	/* Input buffer full */

#define HP_SDC_STATUS_IRQMASK	0xf0	/* Bits containing "level 1" irq */
#define HP_SDC_STATUS_PERIODIC  0x10    /* Periodic 10ms timer */
#define HP_SDC_STATUS_USERTIMER 0x20    /* "Special purpose" timer */
#define HP_SDC_STATUS_TIMER     0x30    /* Both PERIODIC and USERTIMER */
#define HP_SDC_STATUS_REG	0x40	/* Data from an i8042 register */
#define HP_SDC_STATUS_HILCMD    0x50	/* Command from HIL MLC */
#define HP_SDC_STATUS_HILDATA   0x60	/* Data from HIL MLC */
#define HP_SDC_STATUS_PUP	0x70	/* Successful power-up self test */
#define HP_SDC_STATUS_KCOOKED	0x80	/* Key from cooked kbd */
#define HP_SDC_STATUS_KRPG	0xc0	/* Key from Repeat Gen */
#define HP_SDC_STATUS_KMOD_SUP	0x10	/* Shift key is up */
#define HP_SDC_STATUS_KMOD_CUP	0x20	/* Control key is up */

#define HP_SDC_NMISTATUS_FHS	0x40	/* NMI is a fast handshake irq */

/* Internal i8042 registers (there are more, but they are not too useful). */

#define HP_SDC_USE		0x02	/* Resource usage (including OB bit) */
#define HP_SDC_IM		0x04	/* Interrupt mask */
#define HP_SDC_CFG		0x11	/* Configuration register */
#define HP_SDC_KBLANGUAGE	0x12	/* Keyboard language */

#define HP_SDC_D0		0x70	/* General purpose data buffer 0 */
#define HP_SDC_D1		0x71	/* General purpose data buffer 1 */
#define HP_SDC_D2		0x72	/* General purpose data buffer 2 */
#define HP_SDC_D3		0x73	/* General purpose data buffer 3 */
#define HP_SDC_VT1		0x74	/* Timer for voice 1 */
#define HP_SDC_VT2		0x75	/* Timer for voice 2 */
#define HP_SDC_VT3		0x76	/* Timer for voice 3 */
#define HP_SDC_VT4		0x77	/* Timer for voice 4 */
#define HP_SDC_KBN		0x78	/* Which HIL devs are Nimitz */
#define HP_SDC_KBC		0x79	/* Which HIL devs are cooked kbds */
#define HP_SDC_LPS		0x7a	/* i8042's view of HIL status */
#define HP_SDC_LPC		0x7b	/* i8042's view of HIL "control" */
#define HP_SDC_RSV  		0x7c	/* Reserved "for testing" */
#define HP_SDC_LPR		0x7d    /* i8042 count of HIL reconfigs */
#define HP_SDC_XTD		0x7e    /* "Extended Configuration" register */
#define HP_SDC_STR		0x7f    /* i8042 self-test result */

/* Bitfields for above registers */
#define HP_SDC_USE_LOOP		0x04	/* Command is currently on the loop. */

#define HP_SDC_IM_MASK          0x1f    /* these bits not part of cmd/status */
#define HP_SDC_IM_FH		0x10	/* Mask the fast handshake irq */
#define HP_SDC_IM_PT		0x08	/* Mask the periodic timer irq */
#define HP_SDC_IM_TIMERS	0x04	/* Mask the MT/DT/CT irq */
#define HP_SDC_IM_RESET		0x02	/* Mask the reset key irq */
#define HP_SDC_IM_HIL		0x01	/* Mask the HIL MLC irq */

#define HP_SDC_CFG_ROLLOVER	0x08	/* WTF is "N-key rollover"? */
#define HP_SDC_CFG_KBD		0x10	/* There is a keyboard */
#define HP_SDC_CFG_NEW		0x20	/* Supports/uses HIL MLC */
#define HP_SDC_CFG_KBD_OLD	0x03	/* keyboard code for non-HIL */
#define HP_SDC_CFG_KBD_NEW	0x07	/* keyboard code from HIL autoconfig */
#define HP_SDC_CFG_REV		0x40	/* Code revision bit */
#define HP_SDC_CFG_IDPROM	0x80	/* IDPROM present in kbd (not HIL) */

#define HP_SDC_LPS_NDEV		0x07	/* # devices autoconfigured on HIL */
#define HP_SDC_LPS_ACSUCC	0x08	/* loop autoconfigured successfully */
#define HP_SDC_LPS_ACFAIL	0x80	/* last loop autoconfigure failed */

#define HP_SDC_LPC_APE_IPF	0x01	/* HIL MLC APE/IPF (autopoll) set */
#define HP_SDC_LPC_ARCONERR	0x02	/* i8042 autoreconfigs loop on err */
#define HP_SDC_LPC_ARCQUIET	0x03	/* i8042 doesn't report autoreconfigs*/
#define HP_SDC_LPC_COOK		0x10	/* i8042 cooks devices in _KBN */
#define HP_SDC_LPC_RC		0x80	/* causes autoreconfig */

#define HP_SDC_XTD_REV		0x07	/* contains revision code */
#define HP_SDC_XTD_REV_STRINGS(val, str) \
switch (val) {						\
	case 0x1: str = "1820-3712"; break;		\
	case 0x2: str = "1820-4379"; break;		\
	case 0x3: str = "1820-4784"; break;		\
	default: str = "unknown";			\
};
#define HP_SDC_XTD_BEEPER	0x08	/* TI SN76494 beeper available */
#define HP_SDC_XTD_BBRTC	0x20	/* OKI MSM-58321 BBRTC present */

#define HP_SDC_CMD_LOAD_RT	0x31	/* Load real time (from 8042) */
#define HP_SDC_CMD_LOAD_FHS	0x36	/* Load the fast handshake timer */
#define HP_SDC_CMD_LOAD_MT	0x38	/* Load the match timer */
#define HP_SDC_CMD_LOAD_DT	0x3B	/* Load the delay timer */
#define HP_SDC_CMD_LOAD_CT	0x3E	/* Load the cycle timer */

#define HP_SDC_CMD_SET_IM	0x40    /* 010xxxxx == set irq mask */

/* The documents provided do not explicitly state that all registers between
 * 0x01 and 0x1f inclusive can be read by sending their register index as a 
 * command, but this is implied and appears to be the case.
 */
#define HP_SDC_CMD_READ_RAM	0x00	/* Load from i8042 RAM (autoinc) */
#define HP_SDC_CMD_READ_USE	0x02	/* Undocumented! Load from usage reg */
#define HP_SDC_CMD_READ_IM	0x04	/* Load current interrupt mask */
#define HP_SDC_CMD_READ_KCC	0x11	/* Load primary kbd config code */
#define HP_SDC_CMD_READ_KLC	0x12	/* Load primary kbd language code */
#define HP_SDC_CMD_READ_T1	0x13	/* Load timer output buffer byte 1 */
#define HP_SDC_CMD_READ_T2	0x14	/* Load timer output buffer byte 1 */
#define HP_SDC_CMD_READ_T3	0x15	/* Load timer output buffer byte 1 */
#define HP_SDC_CMD_READ_T4	0x16	/* Load timer output buffer byte 1 */
#define HP_SDC_CMD_READ_T5	0x17	/* Load timer output buffer byte 1 */
#define HP_SDC_CMD_READ_D0	0xf0	/* Load from i8042 RAM location 0x70 */
#define HP_SDC_CMD_READ_D1	0xf1	/* Load from i8042 RAM location 0x71 */
#define HP_SDC_CMD_READ_D2	0xf2	/* Load from i8042 RAM location 0x72 */
#define HP_SDC_CMD_READ_D3	0xf3	/* Load from i8042 RAM location 0x73 */
#define HP_SDC_CMD_READ_VT1	0xf4	/* Load from i8042 RAM location 0x74 */
#define HP_SDC_CMD_READ_VT2	0xf5	/* Load from i8042 RAM location 0x75 */
#define HP_SDC_CMD_READ_VT3	0xf6	/* Load from i8042 RAM location 0x76 */
#define HP_SDC_CMD_READ_VT4	0xf7	/* Load from i8042 RAM location 0x77 */
#define HP_SDC_CMD_READ_KBN	0xf8	/* Load from i8042 RAM location 0x78 */
#define HP_SDC_CMD_READ_KBC	0xf9	/* Load from i8042 RAM location 0x79 */
#define HP_SDC_CMD_READ_LPS	0xfa	/* Load from i8042 RAM location 0x7a */
#define HP_SDC_CMD_READ_LPC	0xfb	/* Load from i8042 RAM location 0x7b */
#define HP_SDC_CMD_READ_RSV	0xfc	/* Load from i8042 RAM location 0x7c */
#define HP_SDC_CMD_READ_LPR	0xfd	/* Load from i8042 RAM location 0x7d */
#define HP_SDC_CMD_READ_XTD	0xfe	/* Load from i8042 RAM location 0x7e */
#define HP_SDC_CMD_READ_STR	0xff	/* Load from i8042 RAM location 0x7f */

#define HP_SDC_CMD_SET_ARD	0xA0	/* Set emulated autorepeat delay */
#define HP_SDC_CMD_SET_ARR	0xA2	/* Set emulated autorepeat rate */
#define HP_SDC_CMD_SET_BELL	0xA3	/* Set voice 3 params for "beep" cmd */
#define HP_SDC_CMD_SET_RPGR	0xA6	/* Set "RPG" irq rate (doesn't work) */
#define HP_SDC_CMD_SET_RTMS	0xAD	/* Set the RTC time (milliseconds) */
#define HP_SDC_CMD_SET_RTD	0xAF	/* Set the RTC time (days) */
#define HP_SDC_CMD_SET_FHS	0xB2	/* Set fast handshake timer */
#define HP_SDC_CMD_SET_MT	0xB4	/* Set match timer */
#define HP_SDC_CMD_SET_DT	0xB7	/* Set delay timer */
#define HP_SDC_CMD_SET_CT	0xBA	/* Set cycle timer */
#define HP_SDC_CMD_SET_RAMP	0xC1	/* Reset READ_RAM autoinc counter */
#define HP_SDC_CMD_SET_D0	0xe0	/* Load to i8042 RAM location 0x70 */
#define HP_SDC_CMD_SET_D1	0xe1	/* Load to i8042 RAM location 0x71 */
#define HP_SDC_CMD_SET_D2	0xe2	/* Load to i8042 RAM location 0x72 */
#define HP_SDC_CMD_SET_D3	0xe3	/* Load to i8042 RAM location 0x73 */
#define HP_SDC_CMD_SET_VT1	0xe4	/* Load to i8042 RAM location 0x74 */
#define HP_SDC_CMD_SET_VT2	0xe5	/* Load to i8042 RAM location 0x75 */
#define HP_SDC_CMD_SET_VT3	0xe6	/* Load to i8042 RAM location 0x76 */
#define HP_SDC_CMD_SET_VT4	0xe7	/* Load to i8042 RAM location 0x77 */
#define HP_SDC_CMD_SET_KBN	0xe8	/* Load to i8042 RAM location 0x78 */
#define HP_SDC_CMD_SET_KBC	0xe9	/* Load to i8042 RAM location 0x79 */
#define HP_SDC_CMD_SET_LPS	0xea	/* Load to i8042 RAM location 0x7a */
#define HP_SDC_CMD_SET_LPC	0xeb	/* Load to i8042 RAM location 0x7b */
#define HP_SDC_CMD_SET_RSV	0xec	/* Load to i8042 RAM location 0x7c */
#define HP_SDC_CMD_SET_LPR	0xed	/* Load to i8042 RAM location 0x7d */
#define HP_SDC_CMD_SET_XTD	0xee	/* Load to i8042 RAM location 0x7e */
#define HP_SDC_CMD_SET_STR	0xef	/* Load to i8042 RAM location 0x7f */

#define HP_SDC_CMD_DO_RTCW	0xc2	/* i8042 RAM 0x70 --> RTC */
#define HP_SDC_CMD_DO_RTCR	0xc3	/* RTC[0x70 0:3] --> irq/status/data */
#define HP_SDC_CMD_DO_BEEP	0xc4	/* i8042 RAM 0x70-74  --> beeper,VT3 */
#define HP_SDC_CMD_DO_HIL	0xc5	/* i8042 RAM 0x70-73 --> 
					   HIL MLC R0,R1 i8042 HIL watchdog */

/* Values used to (de)mangle input/output to/from the HIL MLC */
#define HP_SDC_DATA		0x40	/* Data from an 8042 register */
#define HP_SDC_HIL_CMD		0x50	/* Data from HIL MLC R1/8042 */
#define HP_SDC_HIL_R1MASK	0x0f	/* Contents of HIL MLC R1 0:3 */
#define HP_SDC_HIL_AUTO		0x10	/* Set if POL results from i8042 */   
#define HP_SDC_HIL_ISERR	0x80	/* Has meaning as in next 4 values */
#define HP_SDC_HIL_RC_DONE	0x80	/* i8042 auto-configured loop */
#define HP_SDC_HIL_ERR		0x81	/* HIL MLC R2 had a bit set */
#define HP_SDC_HIL_TO		0x82	/* i8042 HIL watchdog expired */
#define HP_SDC_HIL_RC		0x84	/* i8042 is auto-configuring loop */
#define HP_SDC_HIL_DAT		0x60	/* Data from HIL MLC R0 */


typedef struct {
	rwlock_t	ibf_lock;
	rwlock_t	lock;		/* user/tasklet lock */
	rwlock_t	rtq_lock;	/* isr/tasklet lock */
	rwlock_t	hook_lock;	/* isr/user lock for handler add/del */

	unsigned int	irq, nmi;	/* Our IRQ lines */
	unsigned long	base_io, status_io, data_io; /* Our IO ports */

	uint8_t		im;		/* Interrupt mask */
	int		set_im; 	/* Interrupt mask needs to be set. */

	int		ibf;		/* Last known status of IBF flag */
	uint8_t		wi;		/* current i8042 write index */
	uint8_t		r7[4];          /* current i8042[0x70 - 0x74] values */
	uint8_t		r11, r7e;	/* Values from version/revision regs */

	hp_sdc_irqhook	*timer, *reg, *hil, *pup, *cooked;

#define HP_SDC_QUEUE_LEN 16
	hp_sdc_transaction *tq[HP_SDC_QUEUE_LEN]; /* All pending read/writes */

	int		rcurr, rqty;	/* Current read transact in process */
	ktime_t		rtime;		/* Time when current read started */
	int		wcurr;		/* Current write transact in process */

	int		dev_err;	/* carries status from registration */
#if defined(__hppa__)
	struct parisc_device	*dev;
#elif defined(__mc68000__)
	void		*dev;
#else
#error No support for device registration on this arch yet.
#endif

	struct timer_list kicker;	/* Keeps below task alive */
	struct tasklet_struct	task;

} hp_i8042_sdc;

#endif /* _LINUX_HP_SDC_H */
