/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_FDREG_H
#define _LINUX_FDREG_H
/*
 * This file contains some defines for the floppy disk controller.
 * Various sources. Mostly "IBM Microcomputers: A Programmers
 * Handbook", Sanches and Canton.
 */

/* 82077's auxiliary status registers A & B (R) */
#define FD_SRA		0
#define FD_SRB		1

/* Digital Output Register */
#define FD_DOR		2

/* 82077's tape drive register (R/W) */
#define FD_TDR		3

/* 82077's data rate select register (W) */
#define FD_DSR		4

/* Fd controller regs. S&C, about page 340 */
#define FD_STATUS	4
#define FD_DATA		5

/* Digital Input Register (read) */
#define FD_DIR		7

/* Diskette Control Register (write)*/
#define FD_DCR		7

/* Bits of main status register */
#define STATUS_BUSYMASK	0x0F		/* drive busy mask */
#define STATUS_BUSY	0x10		/* FDC busy */
#define STATUS_DMA	0x20		/* 0- DMA mode */
#define STATUS_DIR	0x40		/* 0- cpu->fdc */
#define STATUS_READY	0x80		/* Data reg ready */

/* Bits of FD_ST0 */
#define ST0_DS		0x03		/* drive select mask */
#define ST0_HA		0x04		/* Head (Address) */
#define ST0_NR		0x08		/* Not Ready */
#define ST0_ECE		0x10		/* Equipment check error */
#define ST0_SE		0x20		/* Seek end */
#define ST0_INTR	0xC0		/* Interrupt code mask */

/* Bits of FD_ST1 */
#define ST1_MAM		0x01		/* Missing Address Mark */
#define ST1_WP		0x02		/* Write Protect */
#define ST1_ND		0x04		/* No Data - unreadable */
#define ST1_OR		0x10		/* OverRun */
#define ST1_CRC		0x20		/* CRC error in data or addr */
#define ST1_EOC		0x80		/* End Of Cylinder */

/* Bits of FD_ST2 */
#define ST2_MAM		0x01		/* Missing Address Mark (again) */
#define ST2_BC		0x02		/* Bad Cylinder */
#define ST2_SNS		0x04		/* Scan Not Satisfied */
#define ST2_SEH		0x08		/* Scan Equal Hit */
#define ST2_WC		0x10		/* Wrong Cylinder */
#define ST2_CRC		0x20		/* CRC error in data field */
#define ST2_CM		0x40		/* Control Mark = deleted */

/* Bits of FD_ST3 */
#define ST3_HA		0x04		/* Head (Address) */
#define ST3_DS		0x08		/* drive is double-sided */
#define ST3_TZ		0x10		/* Track Zero signal (1=track 0) */
#define ST3_RY		0x20		/* drive is ready */
#define ST3_WP		0x40		/* Write Protect */
#define ST3_FT		0x80		/* Drive Fault */

/* Values for FD_COMMAND */
#define FD_RECALIBRATE		0x07	/* move to track 0 */
#define FD_SEEK			0x0F	/* seek track */
#define FD_READ			0xE6	/* read with MT, MFM, SKip deleted */
#define FD_WRITE		0xC5	/* write with MT, MFM */
#define FD_SENSEI		0x08	/* Sense Interrupt Status */
#define FD_SPECIFY		0x03	/* specify HUT etc */
#define FD_FORMAT		0x4D	/* format one track */
#define FD_VERSION		0x10	/* get version code */
#define FD_CONFIGURE		0x13	/* configure FIFO operation */
#define FD_PERPENDICULAR	0x12	/* perpendicular r/w mode */
#define FD_GETSTATUS		0x04	/* read ST3 */
#define FD_DUMPREGS		0x0E	/* dump the contents of the fdc regs */
#define FD_READID		0xEA	/* prints the header of a sector */
#define FD_UNLOCK		0x14	/* Fifo config unlock */
#define FD_LOCK			0x94	/* Fifo config lock */
#define FD_RSEEK_OUT		0x8f	/* seek out (i.e. to lower tracks) */
#define FD_RSEEK_IN		0xcf	/* seek in (i.e. to higher tracks) */

/* the following commands are new in the 82078. They are not used in the
 * floppy driver, except the first three. These commands may be useful for apps
 * which use the FDRAWCMD interface. For doc, get the 82078 spec sheets at
 * http://www.intel.com/design/archives/periphrl/docs/29046803.htm */

#define FD_PARTID		0x18	/* part id ("extended" version cmd) */
#define FD_SAVE			0x2e	/* save fdc regs for later restore */
#define FD_DRIVESPEC		0x8e	/* drive specification: Access to the
					 * 2 Mbps data transfer rate for tape
					 * drives */

#define FD_RESTORE		0x4e    /* later restore */
#define FD_POWERDOWN		0x27	/* configure FDC's powersave features */
#define FD_FORMAT_N_WRITE	0xef    /* format and write in one go. */
#define FD_OPTION		0x33	/* ISO format (which is a clean way to
					 * pack more sectors on a track) */

/* DMA commands */
#define DMA_READ	0x46
#define DMA_WRITE	0x4A

/* FDC version return types */
#define FDC_NONE	0x00
#define FDC_UNKNOWN	0x10	/* DO NOT USE THIS TYPE EXCEPT IF IDENTIFICATION
				   FAILS EARLY */
#define FDC_8272A	0x20	/* Intel 8272a, NEC 765 */
#define FDC_765ED	0x30	/* Non-Intel 1MB-compatible FDC, can't detect */
#define FDC_82072	0x40	/* Intel 82072; 8272a + FIFO + DUMPREGS */
#define FDC_82072A	0x45	/* 82072A (on Sparcs) */
#define FDC_82077_ORIG	0x51	/* Original version of 82077AA, sans LOCK */
#define FDC_82077	0x52	/* 82077AA-1 */
#define FDC_82078_UNKN	0x5f	/* Unknown 82078 variant */
#define FDC_82078	0x60	/* 44pin 82078 or 64pin 82078SL */
#define FDC_82078_1	0x61	/* 82078-1 (2Mbps fdc) */
#define FDC_S82078B	0x62	/* S82078B (first seen on Adaptec AVA-2825 VLB
				 * SCSI/EIDE/Floppy controller) */
#define FDC_87306	0x63	/* National Semiconductor PC 87306 */

/*
 * Beware: the fdc type list is roughly sorted by increasing features.
 * Presence of features is tested by comparing the FDC version id with the
 * "oldest" version that has the needed feature.
 * If during FDC detection, an obscure test fails late in the sequence, don't
 * assign FDC_UNKNOWN. Else the FDC will be treated as a dumb 8272a, or worse.
 * This is especially true if the tests are unneeded.
 */

#define FD_RESET_DELAY 20
#endif
