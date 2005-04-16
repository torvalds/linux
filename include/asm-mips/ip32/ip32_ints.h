/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Harald Koerfgen
 */

#ifndef __ASM_IP32_INTS_H
#define __ASM_IP32_INTS_H

/*
 * This list reflects the assignment of interrupt numbers to
 * interrupting events.  Order is fairly irrelevant to handling
 * priority.  This differs from irix.
 */

/* CPU */
#define IP32_R4K_TIMER_IRQ		0

/* MACE */
#define MACE_VID_IN1_IRQ		1
#define MACE_VID_IN2_IRQ		2
#define MACE_VID_OUT_IRQ		3
#define MACE_ETHERNET_IRQ		4
/* SUPERIO, MISC, and AUDIO are MACEISA */
#define MACE_PCI_BRIDGE_IRQ		8

/* MACEPCI */
#define MACEPCI_SCSI0_IRQ		9
#define MACEPCI_SCSI1_IRQ		10
#define MACEPCI_SLOT0_IRQ		11
#define MACEPCI_SLOT1_IRQ		12
#define MACEPCI_SLOT2_IRQ		13
#define MACEPCI_SHARED0_IRQ		14
#define MACEPCI_SHARED1_IRQ		15
#define MACEPCI_SHARED2_IRQ		16

/* CRIME */
#define CRIME_GBE0_IRQ			17
#define CRIME_GBE1_IRQ			18
#define CRIME_GBE2_IRQ			19
#define CRIME_GBE3_IRQ			20
#define CRIME_CPUERR_IRQ		21
#define CRIME_MEMERR_IRQ		22
#define CRIME_RE_EMPTY_E_IRQ		23
#define CRIME_RE_FULL_E_IRQ		24
#define CRIME_RE_IDLE_E_IRQ		25
#define CRIME_RE_EMPTY_L_IRQ		26
#define CRIME_RE_FULL_L_IRQ		27
#define CRIME_RE_IDLE_L_IRQ		28
#define CRIME_SOFT0_IRQ			29
#define CRIME_SOFT1_IRQ			30
#define CRIME_SOFT2_IRQ			31
#define CRIME_SYSCORERR_IRQ		CRIME_SOFT2_IRQ
#define CRIME_VICE_IRQ			32

/* MACEISA */
#define MACEISA_AUDIO_SW_IRQ		33
#define MACEISA_AUDIO_SC_IRQ		34
#define MACEISA_AUDIO1_DMAT_IRQ		35
#define MACEISA_AUDIO1_OF_IRQ		36
#define MACEISA_AUDIO2_DMAT_IRQ		37
#define MACEISA_AUDIO2_MERR_IRQ		38
#define MACEISA_AUDIO3_DMAT_IRQ		39
#define MACEISA_AUDIO3_MERR_IRQ		40
#define MACEISA_RTC_IRQ			41
#define MACEISA_KEYB_IRQ		42
/* MACEISA_KEYB_POLL is not an IRQ */
#define MACEISA_MOUSE_IRQ		44
/* MACEISA_MOUSE_POLL is not an IRQ */
#define MACEISA_TIMER0_IRQ		46
#define MACEISA_TIMER1_IRQ		47
#define MACEISA_TIMER2_IRQ		48
#define MACEISA_PARALLEL_IRQ		49
#define MACEISA_PAR_CTXA_IRQ		50
#define MACEISA_PAR_CTXB_IRQ		51
#define MACEISA_PAR_MERR_IRQ		52
#define MACEISA_SERIAL1_IRQ		53
#define MACEISA_SERIAL1_TDMAT_IRQ	54
#define MACEISA_SERIAL1_TDMAPR_IRQ	55
#define MACEISA_SERIAL1_TDMAME_IRQ	56
#define MACEISA_SERIAL1_RDMAT_IRQ	57
#define MACEISA_SERIAL1_RDMAOR_IRQ	58
#define MACEISA_SERIAL2_IRQ		59
#define MACEISA_SERIAL2_TDMAT_IRQ	60
#define MACEISA_SERIAL2_TDMAPR_IRQ	61
#define MACEISA_SERIAL2_TDMAME_IRQ	62
#define MACEISA_SERIAL2_RDMAT_IRQ	63
#define MACEISA_SERIAL2_RDMAOR_IRQ	64

#define IP32_IRQ_MAX			MACEISA_SERIAL2_RDMAOR_IRQ

#endif /* __ASM_IP32_INTS_H */
