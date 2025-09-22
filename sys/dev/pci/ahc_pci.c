/*	$OpenBSD: ahc_pci.c,v 1.64 2024/05/24 06:02:53 jsg Exp $	*/
/*	$NetBSD: ahc_pci.c,v 1.43 2003/08/18 09:16:22 taca Exp $	*/

/*
 * Product specific probe and attach routines for:
 *      3940, 2940, aic7895, aic7890, aic7880,
 *	aic7870, aic7860 and aic7850 SCSI controllers
 *
 * Copyright (c) 1994-2001 Justin T. Gibbs.
 * Copyright (c) 2000-2001 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id: ahc_pci.c,v 1.64 2024/05/24 06:02:53 jsg Exp $
 *
 * //depot/aic7xxx/aic7xxx/aic7xxx_pci.c#57 $
 *
 * $FreeBSD: /repoman/r/ncvs/src/sys/dev/aic7xxx/aic7xxx_pci.c,v 1.22 2003/01/20 20:44:55 gibbs Exp $
 */
/*
 * Ported from FreeBSD by Pascal Renauld, Network Storage Solutions, Inc. - April 2003
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#define AHC_PCI_IOADDR	PCI_MAPREG_START	/* I/O Address */
#define AHC_PCI_MEMADDR	(PCI_MAPREG_START + 4)	/* Mem I/O Address */

#include <dev/ic/aic7xxx_openbsd.h>
#include <dev/ic/aic7xxx_inline.h>

#ifndef __i386__
#define AHC_ALLOW_MEMIO
#endif

static __inline uint64_t
ahc_compose_id(u_int device, u_int vendor, u_int subdevice, u_int subvendor)
{
	uint64_t id;

	id = subvendor
	   | (subdevice << 16)
	   | ((uint64_t)vendor << 32)
	   | ((uint64_t)device << 48);

	return (id);
}

#define ID_ALL_MASK			0xFFFFFFFFFFFFFFFFull
#define ID_DEV_VENDOR_MASK		0xFFFFFFFF00000000ull
#define ID_9005_GENERIC_MASK		0xFFF0FFFF00000000ull
#define ID_9005_SISL_MASK		0x000FFFFF00000000ull
#define ID_9005_SISL_ID			0x0005900500000000ull
#define ID_AIC7850			0x5078900400000000ull
#define ID_AHA_2902_04_10_15_20_30C	0x5078900478509004ull
#define ID_AIC7855			0x5578900400000000ull
#define ID_AIC7859			0x3860900400000000ull
#define ID_AHA_2930CU			0x3860900438699004ull
#define ID_AIC7860			0x6078900400000000ull
#define ID_AIC7860C			0x6078900478609004ull
#define ID_AHA_1480A			0x6075900400000000ull
#define ID_AHA_2940AU_0			0x6178900400000000ull
#define ID_AHA_2940AU_1			0x6178900478619004ull
#define ID_AHA_2940AU_CN		0x2178900478219004ull
#define ID_AHA_2930C_VAR		0x6038900438689004ull

#define ID_AIC7870			0x7078900400000000ull
#define ID_AHA_2940			0x7178900400000000ull
#define ID_AHA_3940			0x7278900400000000ull
#define ID_AHA_398X			0x7378900400000000ull
#define ID_AHA_2944			0x7478900400000000ull
#define ID_AHA_3944			0x7578900400000000ull
#define ID_AHA_4944			0x7678900400000000ull

#define ID_AIC7880			0x8078900400000000ull
#define ID_AIC7880_B			0x8078900478809004ull
#define ID_AHA_2940U			0x8178900400000000ull
#define ID_AHA_3940U			0x8278900400000000ull
#define ID_AHA_2944U			0x8478900400000000ull
#define ID_AHA_3944U			0x8578900400000000ull
#define ID_AHA_398XU			0x8378900400000000ull
#define ID_AHA_4944U			0x8678900400000000ull
#define ID_AHA_2940UB			0x8178900478819004ull
#define ID_AHA_2930U			0x8878900478889004ull
#define ID_AHA_2940U_PRO		0x8778900478879004ull
#define ID_AHA_2940U_CN			0x0078900478009004ull

#define ID_AIC7895			0x7895900478959004ull
#define ID_AIC7895_ARO			0x7890900478939004ull
#define ID_AIC7895_ARO_MASK		0xFFF0FFFFFFFFFFFFull
#define ID_AHA_2940U_DUAL		0x7895900478919004ull
#define ID_AHA_3940AU			0x7895900478929004ull
#define ID_AHA_3944AU			0x7895900478949004ull

#define ID_AIC7890			0x001F9005000F9005ull
#define ID_AIC7890_ARO			0x00139005000F9005ull
#define ID_AAA_131U2			0x0013900500039005ull
#define ID_AHA_2930U2			0x0011900501819005ull
#define ID_AHA_2940U2B			0x00109005A1009005ull
#define ID_AHA_2940U2_OEM		0x0010900521809005ull
#define ID_AHA_2940U2			0x00109005A1809005ull
#define ID_AHA_2950U2B			0x00109005E1009005ull

#define ID_AIC7892			0x008F9005FFFF9005ull
#define ID_AIC7892_ARO			0x00839005FFFF9005ull
#define ID_AHA_2915LP			0x0082900502109005ull
#define ID_AHA_29160			0x00809005E2A09005ull
#define ID_AHA_29160_CPQ		0x00809005E2A00E11ull
#define ID_AHA_29160N			0x0080900562A09005ull
#define ID_AHA_29160C			0x0080900562209005ull
#define ID_AHA_29160B			0x00809005E2209005ull
#define ID_AHA_19160B			0x0081900562A19005ull

#define ID_AIC7896			0x005F9005FFFF9005ull
#define ID_AIC7896_ARO			0x00539005FFFF9005ull
#define ID_AHA_3950U2B_0		0x00509005FFFF9005ull
#define ID_AHA_3950U2B_1		0x00509005F5009005ull
#define ID_AHA_3950U2D_0		0x00519005FFFF9005ull
#define ID_AHA_3950U2D_1		0x00519005B5009005ull

#define ID_AIC7899			0x00CF9005FFFF9005ull
#define ID_AIC7899_ARO			0x00C39005FFFF9005ull
#define ID_AHA_3960D			0x00C09005F6209005ull
#define ID_AHA_3960D_CPQ		0x00C09005F6200E11ull

#define ID_AIC7810			0x1078900400000000ull
#define ID_AIC7815			0x7815900400000000ull

#define DEVID_9005_TYPE(id) ((id) & 0xF)
#define		DEVID_9005_TYPE_HBA		0x0	/* Standard Card */
#define		DEVID_9005_TYPE_AAA		0x3	/* RAID Card */
#define		DEVID_9005_TYPE_SISL		0x5	/* Container ROMB */
#define		DEVID_9005_TYPE_MB		0xF	/* On Motherboard */

#define DEVID_9005_MAXRATE(id) (((id) & 0x30) >> 4)
#define		DEVID_9005_MAXRATE_U160		0x0
#define		DEVID_9005_MAXRATE_ULTRA2	0x1
#define		DEVID_9005_MAXRATE_ULTRA	0x2
#define		DEVID_9005_MAXRATE_FAST		0x3

#define DEVID_9005_MFUNC(id) (((id) & 0x40) >> 6)

#define DEVID_9005_CLASS(id) (((id) & 0xFF00) >> 8)
#define		DEVID_9005_CLASS_SPI		0x0	/* Parallel SCSI */

#define SUBID_9005_TYPE(id) ((id) & 0xF)
#define		SUBID_9005_TYPE_MB		0xF	/* On Motherboard */
#define		SUBID_9005_TYPE_CARD		0x0	/* Standard Card */
#define		SUBID_9005_TYPE_LCCARD		0x1	/* Low Cost Card */
#define		SUBID_9005_TYPE_RAID		0x3	/* Combined with Raid */

#define SUBID_9005_TYPE_KNOWN(id)			\
	  ((((id) & 0xF) == SUBID_9005_TYPE_MB)		\
	|| (((id) & 0xF) == SUBID_9005_TYPE_CARD)	\
	|| (((id) & 0xF) == SUBID_9005_TYPE_LCCARD)	\
	|| (((id) & 0xF) == SUBID_9005_TYPE_RAID))

#define SUBID_9005_MAXRATE(id) (((id) & 0x30) >> 4)
#define		SUBID_9005_MAXRATE_ULTRA2	0x0
#define		SUBID_9005_MAXRATE_ULTRA	0x1
#define		SUBID_9005_MAXRATE_U160		0x2
#define		SUBID_9005_MAXRATE_RESERVED	0x3

#define SUBID_9005_SEEPTYPE(id)						\
	((SUBID_9005_TYPE(id) == SUBID_9005_TYPE_MB)			\
	 ? ((id) & 0xC0) >> 6						\
	 : ((id) & 0x300) >> 8)
#define		SUBID_9005_SEEPTYPE_NONE	0x0
#define		SUBID_9005_SEEPTYPE_1K		0x1
#define		SUBID_9005_SEEPTYPE_2K_4K	0x2
#define		SUBID_9005_SEEPTYPE_RESERVED	0x3
#define SUBID_9005_AUTOTERM(id)						\
	((SUBID_9005_TYPE(id) == SUBID_9005_TYPE_MB)			\
	 ? (((id) & 0x400) >> 10) == 0					\
	 : (((id) & 0x40) >> 6) == 0)

#define SUBID_9005_NUMCHAN(id)						\
	((SUBID_9005_TYPE(id) == SUBID_9005_TYPE_MB)			\
	 ? ((id) & 0x300) >> 8						\
	 : ((id) & 0xC00) >> 10)

#define SUBID_9005_LEGACYCONN(id)					\
	((SUBID_9005_TYPE(id) == SUBID_9005_TYPE_MB)			\
	 ? 0								\
	 : ((id) & 0x80) >> 7)

#define SUBID_9005_MFUNCENB(id)						\
	((SUBID_9005_TYPE(id) == SUBID_9005_TYPE_MB)			\
	 ? ((id) & 0x800) >> 11						\
	 : ((id) & 0x1000) >> 12)
/*
 * Informational only. Should use chip register to be
 * certain, but may be use in identification strings.
 */
#define SUBID_9005_CARD_SCSIWIDTH_MASK	0x2000
#define SUBID_9005_CARD_PCIWIDTH_MASK	0x4000
#define SUBID_9005_CARD_SEDIFF_MASK	0x8000

static ahc_device_setup_t ahc_aic785X_setup;
static ahc_device_setup_t ahc_aic7860_setup;
static ahc_device_setup_t ahc_apa1480_setup;
static ahc_device_setup_t ahc_aic7870_setup;
static ahc_device_setup_t ahc_aha394X_setup;
static ahc_device_setup_t ahc_aha494X_setup;
static ahc_device_setup_t ahc_aha398X_setup;
static ahc_device_setup_t ahc_aic7880_setup;
static ahc_device_setup_t ahc_aha2940Pro_setup;
static ahc_device_setup_t ahc_aha394XU_setup;
static ahc_device_setup_t ahc_aha398XU_setup;
static ahc_device_setup_t ahc_aic7890_setup;
static ahc_device_setup_t ahc_aic7892_setup;
static ahc_device_setup_t ahc_aic7895_setup;
static ahc_device_setup_t ahc_aic7896_setup;
static ahc_device_setup_t ahc_aic7899_setup;
static ahc_device_setup_t ahc_aha29160C_setup;
static ahc_device_setup_t ahc_raid_setup;
static ahc_device_setup_t ahc_aha394XX_setup;
static ahc_device_setup_t ahc_aha494XX_setup;
static ahc_device_setup_t ahc_aha398XX_setup;

const struct ahc_pci_identity ahc_pci_ident_table [] =
{
	/* aic7850 based controllers */
	{
		ID_AHA_2902_04_10_15_20_30C,
		ID_ALL_MASK,
		ahc_aic785X_setup
	},
	/* aic7860 based controllers */
	{
		ID_AHA_2930CU,
		ID_ALL_MASK,
		ahc_aic7860_setup
	},
	{
		ID_AHA_1480A & ID_DEV_VENDOR_MASK,
		ID_DEV_VENDOR_MASK,
		ahc_apa1480_setup
	},
	{
		ID_AHA_2940AU_0 & ID_DEV_VENDOR_MASK,
		ID_DEV_VENDOR_MASK,
		ahc_aic7860_setup
	},
	{
		ID_AHA_2940AU_CN & ID_DEV_VENDOR_MASK,
		ID_DEV_VENDOR_MASK,
		ahc_aic7860_setup
	},
	{
		ID_AHA_2930C_VAR & ID_DEV_VENDOR_MASK,
		ID_DEV_VENDOR_MASK,
		ahc_aic7860_setup
	},
	/* aic7870 based controllers */
	{
		ID_AHA_2940,
		ID_ALL_MASK,
		ahc_aic7870_setup
	},
	{
		ID_AHA_3940,
		ID_ALL_MASK,
		ahc_aha394X_setup
	},
	{
		ID_AHA_398X,
		ID_ALL_MASK,
		ahc_aha398X_setup
	},
	{
		ID_AHA_2944,
		ID_ALL_MASK,
		ahc_aic7870_setup
	},
	{
		ID_AHA_3944,
		ID_ALL_MASK,
		ahc_aha394X_setup
	},
	{
		ID_AHA_4944,
		ID_ALL_MASK,
		ahc_aha494X_setup
	},
	/* aic7880 based controllers */
	{
		ID_AHA_2940U & ID_DEV_VENDOR_MASK,
		ID_DEV_VENDOR_MASK,
		ahc_aic7880_setup
	},
	{
		ID_AHA_3940U & ID_DEV_VENDOR_MASK,
		ID_DEV_VENDOR_MASK,
		ahc_aha394XU_setup
	},
	{
		ID_AHA_2944U & ID_DEV_VENDOR_MASK,
		ID_DEV_VENDOR_MASK,
		ahc_aic7880_setup
	},
	{
		ID_AHA_3944U & ID_DEV_VENDOR_MASK,
		ID_DEV_VENDOR_MASK,
		ahc_aha394XU_setup
	},
	{
		ID_AHA_398XU & ID_DEV_VENDOR_MASK,
		ID_DEV_VENDOR_MASK,
		ahc_aha398XU_setup
	},
	{
		/*
		 * XXX Don't know the slot numbers
		 * so we can't identify channels
		 */
		ID_AHA_4944U & ID_DEV_VENDOR_MASK,
		ID_DEV_VENDOR_MASK,
		ahc_aic7880_setup
	},
	{
		ID_AHA_2930U & ID_DEV_VENDOR_MASK,
		ID_DEV_VENDOR_MASK,
		ahc_aic7880_setup
	},
	{
		ID_AHA_2940U_PRO & ID_DEV_VENDOR_MASK,
		ID_DEV_VENDOR_MASK,
		ahc_aha2940Pro_setup
	},
	{
		ID_AHA_2940U_CN & ID_DEV_VENDOR_MASK,
		ID_DEV_VENDOR_MASK,
		ahc_aic7880_setup
	},
	/* Ignore all SISL (AAC on MB) based controllers. */
	{
		ID_9005_SISL_ID,
		ID_9005_SISL_MASK,
		NULL
	},
	/* aic7890 based controllers */
	{
		ID_AHA_2930U2,
		ID_ALL_MASK,
		ahc_aic7890_setup
	},
	{
		ID_AHA_2940U2B,
		ID_ALL_MASK,
		ahc_aic7890_setup
	},
	{
		ID_AHA_2940U2_OEM,
		ID_ALL_MASK,
		ahc_aic7890_setup
	},
	{
		ID_AHA_2940U2,
		ID_ALL_MASK,
		ahc_aic7890_setup
	},
	{
		ID_AHA_2950U2B,
		ID_ALL_MASK,
		ahc_aic7890_setup
	},
	{
		ID_AIC7890_ARO,
		ID_ALL_MASK,
		ahc_aic7890_setup
	},
	{
		ID_AAA_131U2,
		ID_ALL_MASK,
		ahc_aic7890_setup
	},
	/* aic7892 based controllers */
	{
		ID_AHA_29160,
		ID_ALL_MASK,
		ahc_aic7892_setup
	},
	{
		ID_AHA_29160_CPQ,
		ID_ALL_MASK,
		ahc_aic7892_setup
	},
	{
		ID_AHA_29160N,
		ID_ALL_MASK,
		ahc_aic7892_setup
	},
	{
		ID_AHA_29160C,
		ID_ALL_MASK,
		ahc_aha29160C_setup
	},
	{
		ID_AHA_29160B,
		ID_ALL_MASK,
		ahc_aic7892_setup
	},
	{
		ID_AHA_19160B,
		ID_ALL_MASK,
		ahc_aic7892_setup
	},
	{
		ID_AIC7892_ARO,
		ID_ALL_MASK,
		ahc_aic7892_setup
	},
	{
		ID_AHA_2915LP,
		ID_ALL_MASK,
		ahc_aic7892_setup
	},
	/* aic7895 based controllers */	
	{
		ID_AHA_2940U_DUAL,
		ID_ALL_MASK,
		ahc_aic7895_setup
	},
	{
		ID_AHA_3940AU,
		ID_ALL_MASK,
		ahc_aic7895_setup
	},
	{
		ID_AHA_3944AU,
		ID_ALL_MASK,
		ahc_aic7895_setup
	},
	{
		ID_AIC7895_ARO,
		ID_AIC7895_ARO_MASK,
		ahc_aic7895_setup
	},
	/* aic7896/97 based controllers */	
	{
		ID_AHA_3950U2B_0,
		ID_ALL_MASK,
		ahc_aic7896_setup
	},
	{
		ID_AHA_3950U2B_1,
		ID_ALL_MASK,
		ahc_aic7896_setup
	},
	{
		ID_AHA_3950U2D_0,
		ID_ALL_MASK,
		ahc_aic7896_setup
	},
	{
		ID_AHA_3950U2D_1,
		ID_ALL_MASK,
		ahc_aic7896_setup
	},
	{
		ID_AIC7896_ARO,
		ID_ALL_MASK,
		ahc_aic7896_setup
	},
	/* aic7899 based controllers */	
	{
		ID_AHA_3960D,
		ID_ALL_MASK,
		ahc_aic7899_setup
	},
	{
		ID_AHA_3960D_CPQ,
		ID_ALL_MASK,
		ahc_aic7899_setup
	},
	{
		ID_AIC7899_ARO,
		ID_ALL_MASK,
		ahc_aic7899_setup
	},
	/* Generic chip probes for devices we don't know 'exactly' */
	{
		ID_AIC7850 & ID_DEV_VENDOR_MASK,
		ID_DEV_VENDOR_MASK,
		ahc_aic785X_setup
	},
	{
		ID_AIC7855 & ID_DEV_VENDOR_MASK,
		ID_DEV_VENDOR_MASK,
		ahc_aic785X_setup
	},
	{
		ID_AIC7859 & ID_DEV_VENDOR_MASK,
		ID_DEV_VENDOR_MASK,
		ahc_aic7860_setup
	},
	{
		ID_AIC7860 & ID_DEV_VENDOR_MASK,
		ID_DEV_VENDOR_MASK,
		ahc_aic7860_setup
	},
	{
		ID_AIC7870 & ID_DEV_VENDOR_MASK,
		ID_DEV_VENDOR_MASK,
		ahc_aic7870_setup
	},
	{
		ID_AIC7880 & ID_DEV_VENDOR_MASK,
		ID_DEV_VENDOR_MASK,
		ahc_aic7880_setup
	},
	{
		ID_AIC7890 & ID_9005_GENERIC_MASK,
		ID_9005_GENERIC_MASK,
		ahc_aic7890_setup
	},
	{
		ID_AIC7892 & ID_9005_GENERIC_MASK,
		ID_9005_GENERIC_MASK,
		ahc_aic7892_setup
	},
	{
		ID_AIC7895 & ID_DEV_VENDOR_MASK,
		ID_DEV_VENDOR_MASK,
		ahc_aic7895_setup
	},
	{
		ID_AIC7896 & ID_9005_GENERIC_MASK,
		ID_9005_GENERIC_MASK,
		ahc_aic7896_setup
	},
	{
		ID_AIC7899 & ID_9005_GENERIC_MASK,
		ID_9005_GENERIC_MASK,
		ahc_aic7899_setup
	},
	{
		ID_AIC7810 & ID_DEV_VENDOR_MASK,
		ID_DEV_VENDOR_MASK,
		ahc_raid_setup
	},
	{
		ID_AIC7815 & ID_DEV_VENDOR_MASK,
		ID_DEV_VENDOR_MASK,
		ahc_raid_setup
	}
};

#define AHC_394X_SLOT_CHANNEL_A	4
#define AHC_394X_SLOT_CHANNEL_B	5

#define AHC_398X_SLOT_CHANNEL_A	4
#define AHC_398X_SLOT_CHANNEL_B	8
#define AHC_398X_SLOT_CHANNEL_C	12

#define AHC_494X_SLOT_CHANNEL_A	4
#define AHC_494X_SLOT_CHANNEL_B	5
#define AHC_494X_SLOT_CHANNEL_C	6
#define AHC_494X_SLOT_CHANNEL_D	7

#define	DEVCONFIG		0x40
#define		PCIERRGENDIS	0x80000000ul
#define		SCBSIZE32	0x00010000ul	/* aic789X only */
#define		REXTVALID	0x00001000ul	/* ultra cards only */
#define		MPORTMODE	0x00000400ul	/* aic7870+ only */
#define		RAMPSM		0x00000200ul	/* aic7870+ only */
#define		VOLSENSE	0x00000100ul
#define		PCI64BIT	0x00000080ul	/* 64Bit PCI bus (Ultra2 Only)*/
#define		SCBRAMSEL	0x00000080ul
#define		MRDCEN		0x00000040ul
#define		EXTSCBTIME	0x00000020ul	/* aic7870 only */
#define		EXTSCBPEN	0x00000010ul	/* aic7870 only */
#define		BERREN		0x00000008ul
#define		DACEN		0x00000004ul
#define		STPWLEVEL	0x00000002ul
#define		DIFACTNEGEN	0x00000001ul	/* aic7870 only */

#define	CSIZE_LATTIME		0x0c
#define		CACHESIZE	0x0000003ful	/* only 5 bits */
#define		LATTIME		0x0000ff00ul

/* PCI STATUS definitions */
#define	DPE	0x80
#define SSE	0x40
#define	RMA	0x20
#define	RTA	0x10
#define STA	0x08
#define DPR	0x01

static int ahc_9005_subdevinfo_valid(uint16_t vendor, uint16_t device,
				     uint16_t subvendor, uint16_t subdevice);
static int ahc_ext_scbram_present(struct ahc_softc *ahc);
static void ahc_scbram_config(struct ahc_softc *ahc, int enable,
				  int pcheck, int fast, int large);
static void ahc_probe_ext_scbram(struct ahc_softc *ahc);
static int  ahc_pci_chip_init(struct ahc_softc *ahc);

int ahc_pci_probe(struct device *, void *, void *);
void ahc_pci_attach(struct device *, struct device *, void *);


const struct cfattach ahc_pci_ca = {
	sizeof(struct ahc_softc), ahc_pci_probe, ahc_pci_attach
};

const struct ahc_pci_identity *
ahc_find_pci_device(pcireg_t id, pcireg_t subid, u_int func)
{
	u_int64_t  full_id;
	const struct	   ahc_pci_identity *entry;
	u_int	   i;

	full_id = ahc_compose_id(PCI_PRODUCT(id), PCI_VENDOR(id),
				 PCI_PRODUCT(subid), PCI_VENDOR(subid));

	/*
	 * If the second function is not hooked up, ignore it.
	 * Unfortunately, not all MB vendors implement the
	 * subdevice ID as per the Adaptec spec, so do our best
	 * to sanity check it prior to accepting the subdevice
	 * ID as valid.
	 */
	if (func > 0
	    && ahc_9005_subdevinfo_valid(PCI_VENDOR(id), PCI_PRODUCT(id), 
					 PCI_VENDOR(subid), PCI_PRODUCT(subid))
	    && SUBID_9005_MFUNCENB(PCI_PRODUCT(subid)) == 0)
		return (NULL);

	for (i = 0; i < NUM_ELEMENTS(ahc_pci_ident_table); i++) {
		entry = &ahc_pci_ident_table[i];
		if (entry->full_id == (full_id & entry->id_mask))
			return (entry);
	}
	return (NULL);
}

int
ahc_pci_probe(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;
	const struct	   ahc_pci_identity *entry;
	pcireg_t   subid;

	subid = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_SUBSYS_ID_REG);
	entry = ahc_find_pci_device(pa->pa_id, subid, pa->pa_function);
	return (entry != NULL && entry->setup != NULL) ? 1 : 0;
}

void
ahc_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct pci_attach_args *pa = aux;
	const struct	   ahc_pci_identity *entry;
	struct		   ahc_softc *ahc = (void *)self;
	pcireg_t	   command;
	u_int		   our_id = 0;
	u_int		   sxfrctl1;
	u_int		   scsiseq;
	u_int		   sblkctl;
	uint8_t 	   dscommand0;
	uint32_t	   devconfig;
	int		   error;
	pcireg_t	   subid;
	int		   ioh_valid;
	bus_space_tag_t    st, iot;
	bus_space_handle_t sh, ioh;
#ifdef AHC_ALLOW_MEMIO
	int		   memh_valid;
	bus_space_tag_t    memt;
	bus_space_handle_t memh;
	pcireg_t memtype;
#endif
	pci_intr_handle_t  ih;
	const char        *intrstr;
	struct ahc_pci_busdata *bd;
	int i;

	/*
	 * Instead of ahc_alloc() as in FreeBSD, do the few relevant
	 * initializations manually.
	 */
	LIST_INIT(&ahc->pending_scbs);
	ahc->channel = 'A';
	ahc->seqctl = FASTMODE;
	for (i = 0; i < AHC_NUM_TARGETS; i++)
		TAILQ_INIT(&ahc->untagged_queues[i]);

	/*
	 * SCSI_IS_SCSIBUS_B() must returns false until sc_channel_b
	 * has been properly initialized.
	 */
	ahc->sc_child_b = NULL;

	ahc->dev_softc = pa;

	ahc_set_name(ahc, ahc->sc_dev.dv_xname);
	ahc->parent_dmat = pa->pa_dmat;

	subid = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_SUBSYS_ID_REG);
	entry = ahc_find_pci_device(pa->pa_id, subid, pa->pa_function);
	if (entry == NULL)
		return;

	/* Keep information about the PCI bus */
	bd = malloc(sizeof (struct ahc_pci_busdata), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (bd == NULL) {
		printf("%s: unable to allocate bus-specific data\n",
		    ahc_name(ahc));
		return;
	}

	bd->pc = pa->pa_pc;
	bd->tag = pa->pa_tag;
	bd->func = pa->pa_function;
	bd->dev = pa->pa_device;
	bd->class = pa->pa_class;

	ahc->bd = bd;

	error = entry->setup(ahc);
	if (error != 0)
		return;

	ioh_valid = 0;

#ifdef AHC_ALLOW_MEMIO
	memh_valid = 0;
	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, AHC_PCI_MEMADDR);
	switch (memtype) {
	case PCI_MAPREG_TYPE_MEM | PCI_MAPREG_MEM_TYPE_32BIT:
	case PCI_MAPREG_TYPE_MEM | PCI_MAPREG_MEM_TYPE_64BIT:
		memh_valid = (pci_mapreg_map(pa, AHC_PCI_MEMADDR,
					     memtype, 0, &memt, &memh, NULL, NULL, 0) == 0);
		break;
	default:
		memh_valid = 0;
	}
	if (memh_valid == 0)
#endif
		ioh_valid = (pci_mapreg_map(pa, AHC_PCI_IOADDR,
		    PCI_MAPREG_TYPE_IO, 0, &iot, &ioh, NULL, NULL, 0) == 0);
#if 0
	printf("%s: mem mapping: memt 0x%lx, memh 0x%lx, iot 0x%lx, ioh 0x%lx\n",
	       ahc_name(ahc), (u_long)memt, memh, (u_long)iot, ioh);
#endif

	if (ioh_valid) {
		st = iot;
		sh = ioh;
#ifdef AHC_ALLOW_MEMIO
	} else if (memh_valid) {
		st = memt;
		sh = memh;
#endif
	} else {
		printf(": unable to map registers\n");
		return;
	}
	ahc->tag = st;
	ahc->bsh = sh;

	ahc->chip |= AHC_PCI;
	/*
	 * Before we continue probing the card, ensure that
	 * its interrupts are *disabled*.  We don't want
	 * a misstep to hang the machine in an interrupt
	 * storm.
	 */
	ahc_intr_enable(ahc, FALSE);

	/*
	 * XXX somehow reading this once fails on some sparc64 systems.
	 *     This may be a problem in the sparc64 PCI code. Doing it
	 *     twice works around it.
	 */
	devconfig = pci_conf_read(pa->pa_pc, pa->pa_tag, DEVCONFIG);
	devconfig = pci_conf_read(pa->pa_pc, pa->pa_tag, DEVCONFIG);

	/*
	 * If we need to support high memory, enable dual
	 * address cycles.  This bit must be set to enable
	 * high address bit generation even if we are on a
	 * 64bit bus (PCI64BIT set in devconfig).
	 */
	if ((ahc->flags & AHC_39BIT_ADDRESSING) != 0) {

		if (1/*bootverbose*/)
			printf("%s: Enabling 39Bit Addressing\n",
			       ahc_name(ahc));
		devconfig |= DACEN;
	}
	
	/* Ensure that pci error generation, a test feature, is disabled. */
	devconfig |= PCIERRGENDIS;

	pci_conf_write(pa->pa_pc, pa->pa_tag, DEVCONFIG, devconfig);

	/*
	 * Disable PCI parity error reporting.  Users typically
	 * do this to work around broken PCI chipsets that get
	 * the parity timing wrong and thus generate lots of spurious
	 * errors.
	 */
	if ((ahc->flags & AHC_DISABLE_PCI_PERR) != 0) {
		command = pci_conf_read(pa->pa_pc, pa->pa_tag,
		    PCI_COMMAND_STATUS_REG);
		pci_conf_write(pa->pa_pc, pa->pa_tag, PCI_COMMAND_STATUS_REG,
		    command & ~PCI_COMMAND_PARITY_ENABLE);
	}

	/* On all PCI adapters, we allow SCB paging */
	ahc->flags |= AHC_PAGESCBS;
	error = ahc_softc_init(ahc);
	if (error != 0)
		goto error_out;

	ahc->bus_intr = ahc_pci_intr;
	ahc->bus_chip_init = ahc_pci_chip_init;

	/* Remember how the card was setup in case there is no SEEPROM */
	if ((ahc_inb(ahc, HCNTRL) & POWRDN) == 0) {
		ahc_pause(ahc);
		if ((ahc->features & AHC_ULTRA2) != 0)
			our_id = ahc_inb(ahc, SCSIID_ULTRA2) & OID;
		else
			our_id = ahc_inb(ahc, SCSIID) & OID;
		sxfrctl1 = ahc_inb(ahc, SXFRCTL1) & STPWEN;
		scsiseq = ahc_inb(ahc, SCSISEQ);
	} else {
		sxfrctl1 = STPWEN;
		our_id = 7;
		scsiseq = 0;
	}

	error = ahc_reset(ahc, /*reinit*/FALSE);
	if (error != 0)
		goto error_out;

	if ((ahc->features & AHC_DT) != 0) {
		u_int sfunct;

		/* Perform ALT-Mode Setup */
		sfunct = ahc_inb(ahc, SFUNCT) & ~ALT_MODE;
		ahc_outb(ahc, SFUNCT, sfunct | ALT_MODE);
		ahc_outb(ahc, OPTIONMODE,
			 OPTIONMODE_DEFAULTS|AUTOACKEN|BUSFREEREV|EXPPHASEDIS);
		ahc_outb(ahc, SFUNCT, sfunct);

		/* Normal mode setup */
		ahc_outb(ahc, CRCCONTROL1, CRCVALCHKEN|CRCENDCHKEN|CRCREQCHKEN
					  |TARGCRCENDEN);
	}

	if (pci_intr_map(pa, &ih)) {
		printf("%s: couldn't map interrupt\n", ahc_name(ahc));
		ahc_free(ahc);
		return;
	}
	intrstr = pci_intr_string(pa->pa_pc, ih);
	ahc->ih = pci_intr_establish(pa->pa_pc, ih, IPL_BIO,
	    ahc_platform_intr, ahc, ahc->sc_dev.dv_xname);
	if (ahc->ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		ahc_free(ahc);
		return;
	} else
		printf(": %s\n", intrstr ? intrstr : "?");

	dscommand0 = ahc_inb(ahc, DSCOMMAND0);
	dscommand0 |= MPARCKEN|CACHETHEN;
	if ((ahc->features & AHC_ULTRA2) != 0) {

		/*
		 * DPARCKEN doesn't work correctly on
		 * some MBs so don't use it.
		 */
		dscommand0 &= ~DPARCKEN;
	}

	/*
	 * Handle chips that must have cache line
	 * streaming (dis/en)abled.
	 */
	if ((ahc->bugs & AHC_CACHETHEN_DIS_BUG) != 0)
		dscommand0 |= CACHETHEN;

	if ((ahc->bugs & AHC_CACHETHEN_BUG) != 0)
		dscommand0 &= ~CACHETHEN;

	ahc_outb(ahc, DSCOMMAND0, dscommand0);

	ahc->pci_cachesize =
	    pci_conf_read(pa->pa_pc, pa->pa_tag, CSIZE_LATTIME) & CACHESIZE;
	ahc->pci_cachesize *= 4;

	if ((ahc->bugs & AHC_PCI_2_1_RETRY_BUG) != 0
	    && ahc->pci_cachesize == 4) {
		pci_conf_write(pa->pa_pc, pa->pa_tag, CSIZE_LATTIME, 0);
		ahc->pci_cachesize = 0;
	}

	/*
	 * We cannot perform ULTRA speeds without the presence
	 * of the external precision resistor.
	 */
	if ((ahc->features & AHC_ULTRA) != 0) {
		uint32_t devconfig;

		devconfig = pci_conf_read(pa->pa_pc, pa->pa_tag, DEVCONFIG);
		if ((devconfig & REXTVALID) == 0)
			ahc->features &= ~AHC_ULTRA;
	}

	ahc->seep_config = malloc(sizeof(*ahc->seep_config), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (ahc->seep_config == NULL)
		goto error_out;
	
	/* See if we have a SEEPROM and perform auto-term */
	ahc_check_extport(ahc, &sxfrctl1);

	/*
	 * Take the LED out of diagnostic mode
	 */
	sblkctl = ahc_inb(ahc, SBLKCTL);
	ahc_outb(ahc, SBLKCTL, (sblkctl & ~(DIAGLEDEN|DIAGLEDON)));

	if ((ahc->features & AHC_ULTRA2) != 0) {
		ahc_outb(ahc, DFF_THRSH, RD_DFTHRSH_MAX|WR_DFTHRSH_MAX);
	} else {
		ahc_outb(ahc, DSPCISTATUS, DFTHRSH_100);
	}

	if (ahc->flags & AHC_USEDEFAULTS) {
		/*
		 * PCI Adapter default setup
		 * Should only be used if the adapter does not have
		 * a SEEPROM.
		 */
		/* See if someone else set us up already */
		if ((ahc->flags & AHC_NO_BIOS_INIT) == 0
		 && scsiseq != 0) {
			printf("%s: Using left over BIOS settings\n",
				ahc_name(ahc));
			ahc->flags &= ~AHC_USEDEFAULTS;
			ahc->flags |= AHC_BIOS_ENABLED;
		} else {
			/*
			 * Assume only one connector and always turn
			 * on termination.
			 */
 			our_id = 0x07;
			sxfrctl1 = STPWEN;
		}
		ahc_outb(ahc, SCSICONF, our_id|ENSPCHK|RESET_SCSI);

		ahc->our_id = our_id;
	}

	/*
	 * Take a look to see if we have external SRAM.
	 * We currently do not attempt to use SRAM that is
	 * shared among multiple controllers.
	 */
	ahc_probe_ext_scbram(ahc);

	/*
	 * Record our termination setting for the
	 * generic initialization routine.
	 */
	if ((sxfrctl1 & STPWEN) != 0)
		ahc->flags |= AHC_TERM_ENB_A;

	/*
	 * Save chip register configuration data for chip resets
	 * that occur during runtime and resume events.
	 */
	ahc->bus_softc.pci_softc.devconfig =
	    pci_conf_read(pa->pa_pc, pa->pa_tag, DEVCONFIG);
	ahc->bus_softc.pci_softc.command =
	    pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_COMMAND_STATUS_REG);
	ahc->bus_softc.pci_softc.csize_lattime =
	    pci_conf_read(pa->pa_pc, pa->pa_tag, CSIZE_LATTIME);
	ahc->bus_softc.pci_softc.dscommand0 = ahc_inb(ahc, DSCOMMAND0);
	ahc->bus_softc.pci_softc.dspcistatus = ahc_inb(ahc, DSPCISTATUS);
	if ((ahc->features & AHC_DT) != 0) {
		u_int sfunct;

		sfunct = ahc_inb(ahc, SFUNCT) & ~ALT_MODE;
		ahc_outb(ahc, SFUNCT, sfunct | ALT_MODE);
		ahc->bus_softc.pci_softc.optionmode = ahc_inb(ahc, OPTIONMODE);
		ahc->bus_softc.pci_softc.targcrccnt = ahc_inw(ahc, TARGCRCCNT);
		ahc_outb(ahc, SFUNCT, sfunct);
		ahc->bus_softc.pci_softc.crccontrol1 =
		    ahc_inb(ahc, CRCCONTROL1);
	}
	if ((ahc->features & AHC_MULTI_FUNC) != 0)
		ahc->bus_softc.pci_softc.scbbaddr = ahc_inb(ahc, SCBBADDR);

	if ((ahc->features & AHC_ULTRA2) != 0)
		ahc->bus_softc.pci_softc.dff_thrsh = ahc_inb(ahc, DFF_THRSH);

	/* Core initialization */
	if (ahc_init(ahc))
		goto error_out;

	ahc_attach(ahc);

	return;

 error_out:
	ahc_free(ahc);
	return;
}

static int
ahc_9005_subdevinfo_valid(uint16_t device, uint16_t vendor,
			  uint16_t subdevice, uint16_t subvendor)
{
	int result;

	/* Default to invalid. */
	result = 0;
	if (vendor == 0x9005
	 && subvendor == 0x9005
         && subdevice != device
         && SUBID_9005_TYPE_KNOWN(subdevice) != 0) {

		switch (SUBID_9005_TYPE(subdevice)) {
		case SUBID_9005_TYPE_MB:
			break;
		case SUBID_9005_TYPE_CARD:
		case SUBID_9005_TYPE_LCCARD:
			/*
			 * Currently only trust Adaptec cards to
			 * get the sub device info correct.
			 */
			if (DEVID_9005_TYPE(device) == DEVID_9005_TYPE_HBA)
				result = 1;
			break;
		case SUBID_9005_TYPE_RAID:
			break;
		default:
			break;
		}
	}
	return (result);
}


/*
 * Test for the presence of external sram in an
 * "unshared" configuration.
 */
static int
ahc_ext_scbram_present(struct ahc_softc *ahc)
{
	u_int chip;
	int ramps;
	int single_user;
	uint32_t devconfig;

	chip = ahc->chip & AHC_CHIPID_MASK;
	devconfig = pci_conf_read(ahc->bd->pc, ahc->bd->tag, DEVCONFIG);
	single_user = (devconfig & MPORTMODE) != 0;

	if ((ahc->features & AHC_ULTRA2) != 0)
		ramps = (ahc_inb(ahc, DSCOMMAND0) & RAMPS) != 0;
	else if (chip == AHC_AIC7895 || chip == AHC_AIC7895C)
		/*
		 * External SCBRAM arbitration is flakey
		 * on these chips.  Unfortunately this means
		 * we don't use the extra SCB ram space on the
		 * 3940AUW.
		 */
		ramps = 0;
	else if (chip >= AHC_AIC7870)
		ramps = (devconfig & RAMPSM) != 0;
	else
		ramps = 0;

	if (ramps && single_user)
		return (1);
	return (0);
}

/*
 * Enable external scbram.
 */
static void
ahc_scbram_config(struct ahc_softc *ahc, int enable, int pcheck,
		  int fast, int large)
{
	uint32_t devconfig;

	if (ahc->features & AHC_MULTI_FUNC) {
		/*
		 * Set the SCB Base addr (highest address bit)
		 * depending on which channel we are.
		 */
		ahc_outb(ahc, SCBBADDR, ahc->bd->func);
	}

	ahc->flags &= ~AHC_LSCBS_ENABLED;
	if (large)
		ahc->flags |= AHC_LSCBS_ENABLED;
	devconfig = pci_conf_read(ahc->bd->pc, ahc->bd->tag, DEVCONFIG);
	if ((ahc->features & AHC_ULTRA2) != 0) {
		u_int dscommand0;

		dscommand0 = ahc_inb(ahc, DSCOMMAND0);
		if (enable)
			dscommand0 &= ~INTSCBRAMSEL;
		else
			dscommand0 |= INTSCBRAMSEL;
		if (large)
			dscommand0 &= ~USCBSIZE32;
		else
			dscommand0 |= USCBSIZE32;
		ahc_outb(ahc, DSCOMMAND0, dscommand0);
	} else {
		if (fast)
			devconfig &= ~EXTSCBTIME;
		else
			devconfig |= EXTSCBTIME;
		if (enable)
			devconfig &= ~SCBRAMSEL;
		else
			devconfig |= SCBRAMSEL;
		if (large)
			devconfig &= ~SCBSIZE32;
		else
			devconfig |= SCBSIZE32;
	}
	if (pcheck)
		devconfig |= EXTSCBPEN;
	else
		devconfig &= ~EXTSCBPEN;

	pci_conf_write(ahc->bd->pc, ahc->bd->tag, DEVCONFIG, devconfig);
}

/*
 * Take a look to see if we have external SRAM.
 * We currently do not attempt to use SRAM that is
 * shared among multiple controllers.
 */
static void
ahc_probe_ext_scbram(struct ahc_softc *ahc)
{
	int num_scbs;
	int test_num_scbs;
	int enable;
	int pcheck;
	int fast;
	int large;

	enable = FALSE;
	pcheck = FALSE;
	fast = FALSE;
	large = FALSE;
	num_scbs = 0;
	
	if (ahc_ext_scbram_present(ahc) == 0)
		goto done;

	/*
	 * Probe for the best parameters to use.
	 */
	ahc_scbram_config(ahc, /*enable*/TRUE, pcheck, fast, large);
	num_scbs = ahc_probe_scbs(ahc);
	if (num_scbs == 0) {
		/* The SRAM wasn't really present. */
		goto done;
	}
	enable = TRUE;

	/*
	 * Clear any outstanding parity error
	 * and ensure that parity error reporting
	 * is enabled.
	 */
	ahc_outb(ahc, SEQCTL, 0);
	ahc_outb(ahc, CLRINT, CLRPARERR);
	ahc_outb(ahc, CLRINT, CLRBRKADRINT);

	/* Now see if we can do parity */
	ahc_scbram_config(ahc, enable, /*pcheck*/TRUE, fast, large);
	num_scbs = ahc_probe_scbs(ahc);
	if ((ahc_inb(ahc, INTSTAT) & BRKADRINT) == 0
	 || (ahc_inb(ahc, ERROR) & MPARERR) == 0)
		pcheck = TRUE;

	/* Clear any resulting parity error */
	ahc_outb(ahc, CLRINT, CLRPARERR);
	ahc_outb(ahc, CLRINT, CLRBRKADRINT);

	/* Now see if we can do fast timing */
	ahc_scbram_config(ahc, enable, pcheck, /*fast*/TRUE, large);
	test_num_scbs = ahc_probe_scbs(ahc);
	if (test_num_scbs == num_scbs
	 && ((ahc_inb(ahc, INTSTAT) & BRKADRINT) == 0
	  || (ahc_inb(ahc, ERROR) & MPARERR) == 0))
		fast = TRUE;

	/*
	 * See if we can use large SCBs and still maintain
	 * the same overall count of SCBs.
	 */
	if ((ahc->features & AHC_LARGE_SCBS) != 0) {
		ahc_scbram_config(ahc, enable, pcheck, fast, /*large*/TRUE);
		test_num_scbs = ahc_probe_scbs(ahc);
		if (test_num_scbs >= num_scbs) {
			large = TRUE;
			num_scbs = test_num_scbs;
	 		if (num_scbs >= 64) {
				/*
				 * We have enough space to move the
				 * "busy targets table" into SCB space
				 * and make it qualify all the way to the
				 * lun level.
				 */
				ahc->flags |= AHC_SCB_BTT;
			}
		}
	}
done:
	/*
	 * Disable parity error reporting until we
	 * can load instruction ram.
	 */
	ahc_outb(ahc, SEQCTL, PERRORDIS|FAILDIS);
	/* Clear any latched parity error */
	ahc_outb(ahc, CLRINT, CLRPARERR);
	ahc_outb(ahc, CLRINT, CLRBRKADRINT);
	if (1/*bootverbose*/ && enable) {
		printf("%s: External SRAM, %s access%s, %dbytes/SCB\n",
		       ahc_name(ahc), fast ? "fast" : "slow", 
		       pcheck ? ", parity checking enabled" : "",
		       large ? 64 : 32);
	}
	ahc_scbram_config(ahc, enable, pcheck, fast, large);
}

#if 0
/*
 * Perform some simple tests that should catch situations where
 * our registers are invalidly mapped.
 */
int
ahc_pci_test_register_access(struct ahc_softc *ahc)
{
	int	 error;
	u_int	 status1;
	uint32_t cmd;
	uint8_t	 hcntrl;

	error = EIO;

	/*
	 * Enable PCI error interrupt status, but suppress NMIs
	 * generated by SERR raised due to target aborts.
	 */
	cmd = pci_conf_read(ahc->bd->pc, ahc->bd->tag, PCIR_COMMAND);
	pci_conf_write(ahc->bd->pc, ahc->bd->tag, PCIR_COMMAND,
		       cmd & ~PCIM_CMD_SERRESPEN);

	/*
	 * First a simple test to see if any
	 * registers can be read.  Reading
	 * HCNTRL has no side effects and has
	 * at least one bit that is guaranteed to
	 * be zero so it is a good register to
	 * use for this test.
	 */
	hcntrl = ahc_inb(ahc, HCNTRL);
	if (hcntrl == 0xFF)
		goto fail;

	/*
	 * Next create a situation where write combining
	 * or read prefetching could be initiated by the
	 * CPU or host bridge.  Our device does not support
	 * either, so look for data corruption and/or flagged
	 * PCI errors.
	 */
	ahc_outb(ahc, HCNTRL, hcntrl|PAUSE);
	while (ahc_is_paused(ahc) == 0)
		;
	ahc_outb(ahc, SEQCTL, PERRORDIS);
	ahc_outb(ahc, SCBPTR, 0);
	ahc_outl(ahc, SCB_BASE, 0x5aa555aa);
	if (ahc_inl(ahc, SCB_BASE) != 0x5aa555aa)
		goto fail;

	status1 = pci_conf_read(ahc->bd->pc, ahc->bd->tag,
				PCI_COMMAND_STATUS_REG + 1);
	if ((status1 & STA) != 0)
		goto fail;

	error = 0;

fail:
	/* Silently clear any latched errors. */
	status1 = pci_conf_read(ahc->bd->pc, ahc->bd->tag, PCI_COMMAND_STATUS_REG + 1);
	ahc_pci_write_config(ahc->dev_softc, PCIR_STATUS + 1,
			     status1, /*bytes*/1);
	ahc_outb(ahc, CLRINT, CLRPARERR);
	ahc_outb(ahc, SEQCTL, PERRORDIS|FAILDIS);
	ahc_pci_write_config(ahc->dev_softc, PCIR_COMMAND, cmd, /*bytes*/2);
	return (error);
}
#endif

void
ahc_pci_intr(struct ahc_softc *ahc)
{
	u_int error;
	u_int status1;

	error = ahc_inb(ahc, ERROR);
	if ((error & PCIERRSTAT) == 0)
		return;

	status1 = pci_conf_read(ahc->bd->pc, ahc->bd->tag, PCI_COMMAND_STATUS_REG);

	printf("%s: PCI error Interrupt at seqaddr = 0x%x\n",
	      ahc_name(ahc),
	      ahc_inb(ahc, SEQADDR0) | (ahc_inb(ahc, SEQADDR1) << 8));

	if (status1 & DPE) {
		printf("%s: Data Parity Error Detected during address "
		       "or write data phase\n", ahc_name(ahc));
	}
	if (status1 & SSE) {
		printf("%s: Signal System Error Detected\n", ahc_name(ahc));
	}
	if (status1 & RMA) {
		printf("%s: Received a Master Abort\n", ahc_name(ahc));
	}
	if (status1 & RTA) {
		printf("%s: Received a Target Abort\n", ahc_name(ahc));
	}
	if (status1 & STA) {
		printf("%s: Signaled a Target Abort\n", ahc_name(ahc));
	}
	if (status1 & DPR) {
		printf("%s: Data Parity Error has been reported via PERR#\n",
		       ahc_name(ahc));
	}

	/* Clear latched errors. */
	pci_conf_write(ahc->bd->pc, ahc->bd->tag,  PCI_COMMAND_STATUS_REG, status1);

	if ((status1 & (DPE|SSE|RMA|RTA|STA|DPR)) == 0) {
		printf("%s: Latched PCIERR interrupt with "
		       "no status bits set\n", ahc_name(ahc)); 
	} else {
		ahc_outb(ahc, CLRINT, CLRPARERR);
	}

	ahc_unpause(ahc);
}

static int
ahc_pci_chip_init(struct ahc_softc *ahc)
{
	ahc_outb(ahc, DSCOMMAND0, ahc->bus_softc.pci_softc.dscommand0);
	ahc_outb(ahc, DSPCISTATUS, ahc->bus_softc.pci_softc.dspcistatus);
	if ((ahc->features & AHC_DT) != 0) {
		u_int sfunct;

		sfunct = ahc_inb(ahc, SFUNCT) & ~ALT_MODE;
		ahc_outb(ahc, SFUNCT, sfunct | ALT_MODE);
		ahc_outb(ahc, OPTIONMODE, ahc->bus_softc.pci_softc.optionmode);
		ahc_outw(ahc, TARGCRCCNT, ahc->bus_softc.pci_softc.targcrccnt);
		ahc_outb(ahc, SFUNCT, sfunct);
		ahc_outb(ahc, CRCCONTROL1,
			 ahc->bus_softc.pci_softc.crccontrol1);
	}
	if ((ahc->features & AHC_MULTI_FUNC) != 0)
		ahc_outb(ahc, SCBBADDR, ahc->bus_softc.pci_softc.scbbaddr);

	if ((ahc->features & AHC_ULTRA2) != 0)
		ahc_outb(ahc, DFF_THRSH, ahc->bus_softc.pci_softc.dff_thrsh);

	return (ahc_chip_init(ahc));
}

static int
ahc_aic785X_setup(struct ahc_softc *ahc)
{
	uint8_t rev;

	ahc->channel = 'A';
	ahc->chip = AHC_AIC7850;
	ahc->features = AHC_AIC7850_FE;
	ahc->bugs |= AHC_TMODE_WIDEODD_BUG|AHC_CACHETHEN_BUG|AHC_PCI_MWI_BUG;
	rev = PCI_REVISION(ahc->bd->class);
	if (rev >= 1)
		ahc->bugs |= AHC_PCI_2_1_RETRY_BUG;
	ahc->instruction_ram_size = 512;
	return (0);
}

static int
ahc_aic7860_setup(struct ahc_softc *ahc)
{
	uint8_t rev;

	ahc->channel = 'A';
	ahc->chip = AHC_AIC7860;
	ahc->features = AHC_AIC7860_FE;
	ahc->bugs |= AHC_TMODE_WIDEODD_BUG|AHC_CACHETHEN_BUG|AHC_PCI_MWI_BUG;
	rev = PCI_REVISION(ahc->bd->class);
	if (rev >= 1)
		ahc->bugs |= AHC_PCI_2_1_RETRY_BUG;
	ahc->instruction_ram_size = 512;
	return (0);
}

static int
ahc_apa1480_setup(struct ahc_softc *ahc)
{
	int error;

	error = ahc_aic7860_setup(ahc);
	if (error != 0)
		return (error);
	ahc->features |= AHC_REMOVABLE;
	return (0);
}

static int
ahc_aic7870_setup(struct ahc_softc *ahc)
{

	ahc->channel = 'A';
	ahc->chip = AHC_AIC7870;
	ahc->features = AHC_AIC7870_FE;
	ahc->bugs |= AHC_TMODE_WIDEODD_BUG|AHC_CACHETHEN_BUG|AHC_PCI_MWI_BUG;
	ahc->instruction_ram_size = 512;
	return (0);
}

static int
ahc_aha394X_setup(struct ahc_softc *ahc)
{
	int error;

	error = ahc_aic7870_setup(ahc);
	if (error == 0)
		error = ahc_aha394XX_setup(ahc);
	return (error);
}

static int
ahc_aha398X_setup(struct ahc_softc *ahc)
{
	int error;

	error = ahc_aic7870_setup(ahc);
	if (error == 0)
		error = ahc_aha398XX_setup(ahc);
	return (error);
}

static int
ahc_aha494X_setup(struct ahc_softc *ahc)
{
	int error;

	error = ahc_aic7870_setup(ahc);
	if (error == 0)
		error = ahc_aha494XX_setup(ahc);
	return (error);
}

static int
ahc_aic7880_setup(struct ahc_softc *ahc)
{
	uint8_t rev;

	ahc->channel = 'A';
	ahc->chip = AHC_AIC7880;
	ahc->features = AHC_AIC7880_FE;
	ahc->bugs |= AHC_TMODE_WIDEODD_BUG;
	rev = PCI_REVISION(ahc->bd->class);
	if (rev >= 1) {
		ahc->bugs |= AHC_PCI_2_1_RETRY_BUG;
	} else {
		ahc->bugs |= AHC_CACHETHEN_BUG|AHC_PCI_MWI_BUG;
	}
	ahc->instruction_ram_size = 512;
	return (0);
}

static int
ahc_aha2940Pro_setup(struct ahc_softc *ahc)
{

	ahc->flags |= AHC_INT50_SPEEDFLEX;
	return (ahc_aic7880_setup(ahc));
}

static int
ahc_aha394XU_setup(struct ahc_softc *ahc)
{
	int error;

	error = ahc_aic7880_setup(ahc);
	if (error == 0)
		error = ahc_aha394XX_setup(ahc);
	return (error);
}

static int
ahc_aha398XU_setup(struct ahc_softc *ahc)
{
	int error;

	error = ahc_aic7880_setup(ahc);
	if (error == 0)
		error = ahc_aha398XX_setup(ahc);
	return (error);
}

static int
ahc_aic7890_setup(struct ahc_softc *ahc)
{
	uint8_t rev;

	ahc->channel = 'A';
	ahc->chip = AHC_AIC7890;
	ahc->features = AHC_AIC7890_FE;
	ahc->flags |= AHC_NEWEEPROM_FMT;
	rev = PCI_REVISION(ahc->bd->class);
	if (rev == 0)
		ahc->bugs |= AHC_AUTOFLUSH_BUG|AHC_CACHETHEN_BUG;
	ahc->instruction_ram_size = 768;
	return (0);
}

static int
ahc_aic7892_setup(struct ahc_softc *ahc)
{

	ahc->channel = 'A';
	ahc->chip = AHC_AIC7892;
	ahc->features = AHC_AIC7892_FE;
	ahc->flags |= AHC_NEWEEPROM_FMT;
	ahc->bugs |= AHC_SCBCHAN_UPLOAD_BUG;
	ahc->instruction_ram_size = 1024;
	return (0);
}

static int
ahc_aic7895_setup(struct ahc_softc *ahc)
{
	uint8_t rev;

	ahc->channel = (ahc->bd->func == 1) ? 'B' : 'A';
	/*
	 * The 'C' revision of the aic7895 has a few additional features.
	 */
	rev = PCI_REVISION(ahc->bd->class);
	if (rev >= 4) {
		ahc->chip = AHC_AIC7895C;
		ahc->features = AHC_AIC7895C_FE;
	} else  {
		u_int command;

		ahc->chip = AHC_AIC7895;
		ahc->features = AHC_AIC7895_FE;

		/*
		 * The BIOS disables the use of MWI transactions
		 * since it does not have the MWI bug work around
		 * we have.  Disabling MWI reduces performance, so
		 * turn it on again.
		 */
		command = pci_conf_read(ahc->bd->pc, ahc->bd->tag, PCI_COMMAND_STATUS_REG);
		command |=  PCI_COMMAND_INVALIDATE_ENABLE;
		pci_conf_write(ahc->bd->pc, ahc->bd->tag, PCI_COMMAND_STATUS_REG, command);
		ahc->bugs |= AHC_PCI_MWI_BUG;
	}
	/*
	 * XXX Does CACHETHEN really not work???  What about PCI retry?
	 * on C level chips.  Need to test, but for now, play it safe.
	 */
	ahc->bugs |= AHC_TMODE_WIDEODD_BUG|AHC_PCI_2_1_RETRY_BUG
		  |  AHC_CACHETHEN_BUG;

#if 0
	uint32_t devconfig;

	/*
	 * Cachesize must also be zero due to stray DAC
	 * problem when sitting behind some bridges.
	 */
	pci_conf_write(ahc->bd->pc, ahc->bd->tag, CSIZE_LATTIME, 0);
	devconfig = pci_conf_read(ahc->bd->pc, ahc->bd->tag, DEVCONFIG);
	devconfig |= MRDCEN;
	pci_conf_write(ahc->bd->pc, ahc->bd->tag, DEVCONFIG, devconfig);
#endif
	ahc->flags |= AHC_NEWEEPROM_FMT;
	ahc->instruction_ram_size = 512;
	return (0);
}

static int
ahc_aic7896_setup(struct ahc_softc *ahc)
{
	ahc->channel = (ahc->bd->func == 1) ? 'B' : 'A';
	ahc->chip = AHC_AIC7896;
	ahc->features = AHC_AIC7896_FE;
	ahc->flags |= AHC_NEWEEPROM_FMT;
	ahc->bugs |= AHC_CACHETHEN_DIS_BUG;
	ahc->instruction_ram_size = 768;
	return (0);
}

static int
ahc_aic7899_setup(struct ahc_softc *ahc)
{
	ahc->channel = (ahc->bd->func == 1) ? 'B' : 'A';
	ahc->chip = AHC_AIC7899;
	ahc->features = AHC_AIC7899_FE;
	ahc->flags |= AHC_NEWEEPROM_FMT;
	ahc->bugs |= AHC_SCBCHAN_UPLOAD_BUG;
	ahc->instruction_ram_size = 1024;
	return (0);
}

static int
ahc_aha29160C_setup(struct ahc_softc *ahc)
{
	int error;

	error = ahc_aic7899_setup(ahc);
	if (error != 0)
		return (error);
	ahc->features |= AHC_REMOVABLE;
	return (0);
}

static int
ahc_raid_setup(struct ahc_softc *ahc)
{
	printf("RAID functionality unsupported\n");
	return (ENXIO);
}

static int
ahc_aha394XX_setup(struct ahc_softc *ahc)
{

	switch (ahc->bd->dev) {
	case AHC_394X_SLOT_CHANNEL_A:
		ahc->channel = 'A';
		break;
	case AHC_394X_SLOT_CHANNEL_B:
		ahc->channel = 'B';
		break;
	default:
		printf("adapter at unexpected slot %d\n"
		       "unable to map to a channel\n",
		       ahc->bd->dev);
		ahc->channel = 'A';
	}
	return (0);
}

static int
ahc_aha398XX_setup(struct ahc_softc *ahc)
{

	switch (ahc->bd->dev) {
	case AHC_398X_SLOT_CHANNEL_A:
		ahc->channel = 'A';
		break;
	case AHC_398X_SLOT_CHANNEL_B:
		ahc->channel = 'B';
		break;
	case AHC_398X_SLOT_CHANNEL_C:
		ahc->channel = 'C';
		break;
	default:
		printf("adapter at unexpected slot %d\n"
		       "unable to map to a channel\n",
		       ahc->bd->dev);
		ahc->channel = 'A';
		break;
	}
	ahc->flags |= AHC_LARGE_SEEPROM;
	return (0);
}

static int
ahc_aha494XX_setup(struct ahc_softc *ahc)
{

	switch (ahc->bd->dev) {
	case AHC_494X_SLOT_CHANNEL_A:
		ahc->channel = 'A';
		break;
	case AHC_494X_SLOT_CHANNEL_B:
		ahc->channel = 'B';
		break;
	case AHC_494X_SLOT_CHANNEL_C:
		ahc->channel = 'C';
		break;
	case AHC_494X_SLOT_CHANNEL_D:
		ahc->channel = 'D';
		break;
	default:
		printf("adapter at unexpected slot %d\n"
		       "unable to map to a channel\n",
		       ahc->bd->dev);
		ahc->channel = 'A';
	}
	ahc->flags |= AHC_LARGE_SEEPROM;
	return (0);
}
