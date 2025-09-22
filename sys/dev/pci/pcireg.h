/*	$OpenBSD: pcireg.h,v 1.63 2024/05/13 10:01:53 kettenis Exp $	*/
/*	$NetBSD: pcireg.h,v 1.26 2000/05/10 16:58:42 thorpej Exp $	*/

/*
 * Copyright (c) 1995, 1996 Christopher G. Demetriou.  All rights reserved.
 * Copyright (c) 1994, 1996 Charles Hannum.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Charles Hannum.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_PCI_PCIREG_H_
#define	_DEV_PCI_PCIREG_H_

/*
 * Standardized PCI configuration information
 *
 * XXX This is not complete.
 */

#define	PCI_CONFIG_SPACE_SIZE		0x100
#define	PCIE_CONFIG_SPACE_SIZE		0x1000

/*
 * Device identification register; contains a vendor ID and a device ID.
 */
#define	PCI_ID_REG			0x00

typedef u_int16_t pci_vendor_id_t;
typedef u_int16_t pci_product_id_t;

#define	PCI_VENDOR_SHIFT			0
#define	PCI_VENDOR_MASK				0xffff
#define	PCI_VENDOR(id) \
	    (((id) >> PCI_VENDOR_SHIFT) & PCI_VENDOR_MASK)

#define	PCI_PRODUCT_SHIFT			16
#define	PCI_PRODUCT_MASK			0xffff
#define	PCI_PRODUCT(id) \
	    (((id) >> PCI_PRODUCT_SHIFT) & PCI_PRODUCT_MASK)

#define PCI_ID_CODE(vid,pid) \
	((((vid) & PCI_VENDOR_MASK) << PCI_VENDOR_SHIFT) | \
	 (((pid) & PCI_PRODUCT_MASK) << PCI_PRODUCT_SHIFT))

/*
 * Command and status register.
 */
#define	PCI_COMMAND_STATUS_REG			0x04

#define	PCI_COMMAND_IO_ENABLE			0x00000001
#define	PCI_COMMAND_MEM_ENABLE			0x00000002
#define	PCI_COMMAND_MASTER_ENABLE		0x00000004
#define	PCI_COMMAND_SPECIAL_ENABLE		0x00000008
#define	PCI_COMMAND_INVALIDATE_ENABLE		0x00000010
#define	PCI_COMMAND_PALETTE_ENABLE		0x00000020
#define	PCI_COMMAND_PARITY_ENABLE		0x00000040
#define	PCI_COMMAND_STEPPING_ENABLE		0x00000080
#define	PCI_COMMAND_SERR_ENABLE			0x00000100
#define	PCI_COMMAND_BACKTOBACK_ENABLE		0x00000200
#define PCI_COMMAND_INTERRUPT_DISABLE		0x00000400

#define	PCI_STATUS_CAPLIST_SUPPORT		0x00100000
#define	PCI_STATUS_66MHZ_SUPPORT		0x00200000
#define	PCI_STATUS_UDF_SUPPORT			0x00400000
#define	PCI_STATUS_BACKTOBACK_SUPPORT		0x00800000
#define	PCI_STATUS_PARITY_ERROR			0x01000000
#define	PCI_STATUS_DEVSEL_FAST			0x00000000
#define	PCI_STATUS_DEVSEL_MEDIUM		0x02000000
#define	PCI_STATUS_DEVSEL_SLOW			0x04000000
#define	PCI_STATUS_DEVSEL_MASK			0x06000000
#define	PCI_STATUS_TARGET_TARGET_ABORT		0x08000000
#define	PCI_STATUS_MASTER_TARGET_ABORT		0x10000000
#define	PCI_STATUS_MASTER_ABORT			0x20000000
#define	PCI_STATUS_SPECIAL_ERROR		0x40000000
#define	PCI_STATUS_PARITY_DETECT		0x80000000

#define	PCI_COMMAND_STATUS_BITS \
    ("\020\01IO\02MEM\03MASTER\04SPECIAL\05INVALIDATE\06PALETTE\07PARITY"\
     "\010STEPPING\011SERR\012BACKTOBACK\025CAPLIST\026CLK66\027UDF"\
     "\030BACK2BACK_STAT\031PARITY_STAT\032DEVSEL_MEDIUM\033DEVSEL_SLOW"\
     "\034TARGET_TARGET_ABORT\035MASTER_TARGET_ABORT\036MASTER_ABORT"\
     "\037SPECIAL_ERROR\040PARITY_DETECT")
/*
 * PCI Class and Revision Register; defines type and revision of device.
 */
#define	PCI_CLASS_REG			0x08

typedef u_int8_t pci_class_t;
typedef u_int8_t pci_subclass_t;
typedef u_int8_t pci_interface_t;
typedef u_int8_t pci_revision_t;

#define	PCI_CLASS_SHIFT				24
#define	PCI_CLASS_MASK				0xff
#define	PCI_CLASS(cr) \
	    (((cr) >> PCI_CLASS_SHIFT) & PCI_CLASS_MASK)

#define	PCI_SUBCLASS_SHIFT			16
#define	PCI_SUBCLASS_MASK			0xff
#define	PCI_SUBCLASS(cr) \
	    (((cr) >> PCI_SUBCLASS_SHIFT) & PCI_SUBCLASS_MASK)

#define	PCI_INTERFACE_SHIFT			8
#define	PCI_INTERFACE_MASK			0xff
#define	PCI_INTERFACE(cr) \
	    (((cr) >> PCI_INTERFACE_SHIFT) & PCI_INTERFACE_MASK)

#define	PCI_REVISION_SHIFT			0
#define	PCI_REVISION_MASK			0xff
#define	PCI_REVISION(cr) \
	    (((cr) >> PCI_REVISION_SHIFT) & PCI_REVISION_MASK)

/* base classes */
#define	PCI_CLASS_PREHISTORIC			0x00
#define	PCI_CLASS_MASS_STORAGE			0x01
#define	PCI_CLASS_NETWORK			0x02
#define	PCI_CLASS_DISPLAY			0x03
#define	PCI_CLASS_MULTIMEDIA			0x04
#define	PCI_CLASS_MEMORY			0x05
#define	PCI_CLASS_BRIDGE			0x06
#define	PCI_CLASS_COMMUNICATIONS		0x07
#define	PCI_CLASS_SYSTEM			0x08
#define	PCI_CLASS_INPUT				0x09
#define	PCI_CLASS_DOCK				0x0a
#define	PCI_CLASS_PROCESSOR			0x0b
#define	PCI_CLASS_SERIALBUS			0x0c
#define	PCI_CLASS_WIRELESS			0x0d
#define	PCI_CLASS_I2O				0x0e
#define	PCI_CLASS_SATCOM			0x0f
#define	PCI_CLASS_CRYPTO			0x10
#define	PCI_CLASS_DASP				0x11
#define	PCI_CLASS_ACCELERATOR			0x12
#define	PCI_CLASS_INSTRUMENTATION		0x13
#define	PCI_CLASS_UNDEFINED			0xff

/* 0x00 prehistoric subclasses */
#define	PCI_SUBCLASS_PREHISTORIC_MISC		0x00
#define	PCI_SUBCLASS_PREHISTORIC_VGA		0x01

/* 0x01 mass storage subclasses */
#define	PCI_SUBCLASS_MASS_STORAGE_SCSI		0x00
#define	PCI_SUBCLASS_MASS_STORAGE_IDE		0x01
#define	PCI_SUBCLASS_MASS_STORAGE_FLOPPY	0x02
#define	PCI_SUBCLASS_MASS_STORAGE_IPI		0x03
#define	PCI_SUBCLASS_MASS_STORAGE_RAID		0x04
#define	PCI_SUBCLASS_MASS_STORAGE_ATA		0x05
#define	PCI_SUBCLASS_MASS_STORAGE_SATA		0x06
#define	 PCI_INTERFACE_SATA_AHCI10		0x01
#define	PCI_SUBCLASS_MASS_STORAGE_SAS		0x07
#define	PCI_SUBCLASS_MASS_STORAGE_NVM		0x08
#define	PCI_SUBCLASS_MASS_STORAGE_UFS		0x09
#define	PCI_SUBCLASS_MASS_STORAGE_MISC		0x80

/* 0x02 network subclasses */
#define	PCI_SUBCLASS_NETWORK_ETHERNET		0x00
#define	PCI_SUBCLASS_NETWORK_TOKENRING		0x01
#define	PCI_SUBCLASS_NETWORK_FDDI		0x02
#define	PCI_SUBCLASS_NETWORK_ATM		0x03
#define	PCI_SUBCLASS_NETWORK_ISDN		0x04
#define	PCI_SUBCLASS_NETWORK_WORLDFIP		0x05
#define	PCI_SUBCLASS_NETWORK_PCIMGMULTICOMP	0x06
#define	PCI_SUBCLASS_NETWORK_INFINIBAND		0x07
#define	PCI_SUBCLASS_NETWORK_MISC		0x80

/* 0x03 display subclasses */
#define	PCI_SUBCLASS_DISPLAY_VGA		0x00
#define	PCI_SUBCLASS_DISPLAY_XGA		0x01
#define	PCI_SUBCLASS_DISPLAY_3D			0x02
#define	PCI_SUBCLASS_DISPLAY_MISC		0x80

/* 0x04 multimedia subclasses */
#define	PCI_SUBCLASS_MULTIMEDIA_VIDEO		0x00
#define	PCI_SUBCLASS_MULTIMEDIA_AUDIO		0x01
#define	PCI_SUBCLASS_MULTIMEDIA_TELEPHONY	0x02
#define	PCI_SUBCLASS_MULTIMEDIA_HDAUDIO		0x03
#define	PCI_SUBCLASS_MULTIMEDIA_MISC		0x80

/* 0x05 memory subclasses */
#define	PCI_SUBCLASS_MEMORY_RAM			0x00
#define	PCI_SUBCLASS_MEMORY_FLASH		0x01
#define	PCI_SUBCLASS_MEMORY_MISC		0x80

/* 0x06 bridge subclasses */
#define	PCI_SUBCLASS_BRIDGE_HOST		0x00
#define	PCI_SUBCLASS_BRIDGE_ISA			0x01
#define	PCI_SUBCLASS_BRIDGE_EISA		0x02
#define	PCI_SUBCLASS_BRIDGE_MC			0x03
#define	PCI_SUBCLASS_BRIDGE_PCI			0x04
#define	PCI_SUBCLASS_BRIDGE_PCMCIA		0x05
#define	PCI_SUBCLASS_BRIDGE_NUBUS		0x06
#define	PCI_SUBCLASS_BRIDGE_CARDBUS		0x07
#define	PCI_SUBCLASS_BRIDGE_RACEWAY		0x08
#define	PCI_SUBCLASS_BRIDGE_STPCI		0x09
#define	PCI_SUBCLASS_BRIDGE_INFINIBAND		0x0a
#define	PCI_SUBCLASS_BRIDGE_AS			0x0b
#define	PCI_SUBCLASS_BRIDGE_MISC		0x80

/* 0x07 communications subclasses */
#define	PCI_SUBCLASS_COMMUNICATIONS_SERIAL	0x00
#define	PCI_SUBCLASS_COMMUNICATIONS_PARALLEL	0x01
#define	PCI_SUBCLASS_COMMUNICATIONS_MPSERIAL	0x02
#define	PCI_SUBCLASS_COMMUNICATIONS_MODEM	0x03
#define	PCI_SUBCLASS_COMMUNICATIONS_GPIB	0x04
#define	PCI_SUBCLASS_COMMUNICATIONS_SMARTCARD	0x05
#define	PCI_SUBCLASS_COMMUNICATIONS_MISC	0x80

/* 0x08 system subclasses */
#define	PCI_SUBCLASS_SYSTEM_PIC			0x00
#define	PCI_SUBCLASS_SYSTEM_DMA			0x01
#define	PCI_SUBCLASS_SYSTEM_TIMER		0x02
#define	PCI_SUBCLASS_SYSTEM_RTC			0x03
#define	PCI_SUBCLASS_SYSTEM_PCIHOTPLUG		0x04
#define	PCI_SUBCLASS_SYSTEM_SDHC		0x05
#define	PCI_SUBCLASS_SYSTEM_IOMMU		0x06
#define	PCI_SUBCLASS_SYSTEM_ROOTCOMPEVENT	0x07
#define	PCI_SUBCLASS_SYSTEM_MISC		0x80

/* 0x09 input subclasses */
#define	PCI_SUBCLASS_INPUT_KEYBOARD		0x00
#define	PCI_SUBCLASS_INPUT_DIGITIZER		0x01
#define	PCI_SUBCLASS_INPUT_MOUSE		0x02
#define	PCI_SUBCLASS_INPUT_SCANNER		0x03
#define	PCI_SUBCLASS_INPUT_GAMEPORT		0x04
#define	PCI_SUBCLASS_INPUT_MISC			0x80

/* 0x0a dock subclasses */
#define	PCI_SUBCLASS_DOCK_GENERIC		0x00
#define	PCI_SUBCLASS_DOCK_MISC			0x80

/* 0x0b processor subclasses */
#define	PCI_SUBCLASS_PROCESSOR_386		0x00
#define	PCI_SUBCLASS_PROCESSOR_486		0x01
#define	PCI_SUBCLASS_PROCESSOR_PENTIUM		0x02
#define	PCI_SUBCLASS_PROCESSOR_ALPHA		0x10
#define	PCI_SUBCLASS_PROCESSOR_POWERPC		0x20
#define	PCI_SUBCLASS_PROCESSOR_MIPS		0x30
#define	PCI_SUBCLASS_PROCESSOR_COPROC		0x40

/* 0x0c serial bus subclasses */
#define	PCI_SUBCLASS_SERIALBUS_FIREWIRE		0x00
#define	PCI_SUBCLASS_SERIALBUS_ACCESS		0x01
#define	PCI_SUBCLASS_SERIALBUS_SSA		0x02
#define	PCI_SUBCLASS_SERIALBUS_USB		0x03
#define	PCI_SUBCLASS_SERIALBUS_FIBER		0x04
#define	PCI_SUBCLASS_SERIALBUS_SMBUS		0x05
#define	PCI_SUBCLASS_SERIALBUS_INFINIBAND	0x06
#define	PCI_SUBCLASS_SERIALBUS_IPMI		0x07
#define	PCI_SUBCLASS_SERIALBUS_SERCOS		0x08
#define	PCI_SUBCLASS_SERIALBUS_CANBUS		0x09

/* 0x0d wireless subclasses */
#define	PCI_SUBCLASS_WIRELESS_IRDA		0x00
#define	PCI_SUBCLASS_WIRELESS_CONSUMERIR	0x01
#define	PCI_SUBCLASS_WIRELESS_RF		0x10
#define	PCI_SUBCLASS_WIRELESS_BLUETOOTH		0x11
#define	PCI_SUBCLASS_WIRELESS_BROADBAND		0x12
#define	PCI_SUBCLASS_WIRELESS_802_11A		0x20
#define	PCI_SUBCLASS_WIRELESS_802_11B		0x21
#define	PCI_SUBCLASS_WIRELESS_MISC		0x80

/* 0x0e I2O (Intelligent I/O) subclasses */
#define	PCI_SUBCLASS_I2O_STANDARD		0x00

/* 0x0f satellite communication subclasses */
/*	PCI_SUBCLASS_SATCOM_???			0x00    / * XXX ??? */
#define	PCI_SUBCLASS_SATCOM_TV			0x01
#define	PCI_SUBCLASS_SATCOM_AUDIO		0x02
#define	PCI_SUBCLASS_SATCOM_VOICE		0x03
#define	PCI_SUBCLASS_SATCOM_DATA		0x04

/* 0x10 encryption/decryption subclasses */
#define	PCI_SUBCLASS_CRYPTO_NETCOMP		0x00
#define	PCI_SUBCLASS_CRYPTO_ENTERTAINMENT	0x10
#define	PCI_SUBCLASS_CRYPTO_MISC		0x80

/* 0x11 data acquisition and signal processing subclasses */
#define	PCI_SUBCLASS_DASP_DPIO			0x00
#define	PCI_SUBCLASS_DASP_TIMEFREQ		0x01
#define	PCI_SUBCLASS_DASP_SYNC			0x10
#define	PCI_SUBCLASS_DASP_MGMT			0x20
#define	PCI_SUBCLASS_DASP_MISC			0x80

/*
 * PCI BIST/Header Type/Latency Timer/Cache Line Size Register.
 */
#define	PCI_BHLC_REG			0x0c

#define	PCI_BIST_SHIFT				24
#define	PCI_BIST_MASK				0xff
#define	PCI_BIST(bhlcr) \
	    (((bhlcr) >> PCI_BIST_SHIFT) & PCI_BIST_MASK)

#define	PCI_HDRTYPE_SHIFT			16
#define	PCI_HDRTYPE_MASK			0xff
#define	PCI_HDRTYPE(bhlcr) \
	    (((bhlcr) >> PCI_HDRTYPE_SHIFT) & PCI_HDRTYPE_MASK)

#define PCI_HDRTYPE_TYPE(bhlcr) \
	    (PCI_HDRTYPE(bhlcr) & 0x7f)
#define	PCI_HDRTYPE_MULTIFN(bhlcr) \
	    ((PCI_HDRTYPE(bhlcr) & 0x80) != 0)

#define	PCI_LATTIMER_SHIFT			8
#define	PCI_LATTIMER_MASK			0xff
#define	PCI_LATTIMER(bhlcr) \
	    (((bhlcr) >> PCI_LATTIMER_SHIFT) & PCI_LATTIMER_MASK)

#define	PCI_CACHELINE_SHIFT			0
#define	PCI_CACHELINE_MASK			0xff
#define	PCI_CACHELINE(bhlcr) \
	    (((bhlcr) >> PCI_CACHELINE_SHIFT) & PCI_CACHELINE_MASK)

/* config registers for header type 0 devices */

#define PCI_MAPS	0x10
#define PCI_CARDBUSCIS	0x28
#define PCI_SUBVEND_0	0x2c
#define PCI_SUBDEV_0	0x2e
#define PCI_EXROMADDR_0	0x30
#define PCI_INTLINE	0x3c
#define PCI_INTPIN	0x3d
#define PCI_MINGNT	0x3e
#define PCI_MAXLAT	0x3f

/* config registers for header type 1 devices */

#define PCI_SECSTAT_1	0 /**/

#define PCI_PRIBUS_1	0x18
#define PCI_SECBUS_1	0x19
#define PCI_SUBBUS_1	0x1a
#define PCI_SECLAT_1	0x1b

#define PCI_IOBASEL_1	0x1c
#define PCI_IOLIMITL_1	0x1d
#define PCI_IOBASEH_1	0 /**/
#define PCI_IOLIMITH_1	0 /**/

#define PCI_MEMBASE_1	0x20
#define PCI_MEMLIMIT_1	0x22

#define PCI_PMBASEL_1	0x24
#define PCI_PMLIMITL_1	0x26
#define PCI_PMBASEH_1	0 /**/
#define PCI_PMLIMITH_1	0 /**/

#define PCI_BRIDGECTL_1 0 /**/

#define PCI_SUBVEND_1	0x34
#define PCI_SUBDEV_1	0x36
#define PCI_EXROMADDR_1	0x38

/* config registers for header type 2 devices */

#define PCI_SECSTAT_2	0x16

#define PCI_PRIBUS_2	0x18
#define PCI_SECBUS_2	0x19
#define PCI_SUBBUS_2	0x1a
#define PCI_SECLAT_2	0x1b

#define PCI_MEMBASE0_2	0x1c
#define PCI_MEMLIMIT0_2 0x20
#define PCI_MEMBASE1_2	0x24
#define PCI_MEMLIMIT1_2 0x28
#define PCI_IOBASE0_2	0x2c
#define PCI_IOLIMIT0_2	0x30
#define PCI_IOBASE1_2	0x34
#define PCI_IOLIMIT1_2	0x38

#define PCI_BRIDGECTL_2 0x3e

#define PCI_SUBVEND_2	0x40
#define PCI_SUBDEV_2	0x42

#define PCI_PCCARDIF_2	0x44

/*
 * Mapping registers
 */
#define	PCI_MAPREG_START		0x10
#define	PCI_MAPREG_END			0x28
#define	PCI_MAPREG_PPB_END		0x18
#define	PCI_MAPREG_PCB_END		0x14

#define	PCI_MAPREG_TYPE(mr)						\
	    ((mr) & PCI_MAPREG_TYPE_MASK)
#define	PCI_MAPREG_TYPE_MASK			0x00000001

#define	PCI_MAPREG_TYPE_MEM			0x00000000
#define	PCI_MAPREG_TYPE_IO			0x00000001

#define	PCI_MAPREG_MEM_TYPE(mr)						\
	    ((mr) & PCI_MAPREG_MEM_TYPE_MASK)
#define	PCI_MAPREG_MEM_TYPE_MASK		0x00000006

#define	PCI_MAPREG_MEM_TYPE_32BIT		0x00000000
#define	PCI_MAPREG_MEM_TYPE_32BIT_1M		0x00000002
#define	PCI_MAPREG_MEM_TYPE_64BIT		0x00000004

#define _PCI_MAPREG_TYPEBITS(reg) \
	(PCI_MAPREG_TYPE(reg) == PCI_MAPREG_TYPE_IO ? \
	reg & PCI_MAPREG_TYPE_MASK : \
	reg & (PCI_MAPREG_TYPE_MASK|PCI_MAPREG_MEM_TYPE_MASK))

#define	PCI_MAPREG_MEM_PREFETCHABLE(mr)					\
	    (((mr) & PCI_MAPREG_MEM_PREFETCHABLE_MASK) != 0)
#define	PCI_MAPREG_MEM_PREFETCHABLE_MASK	0x00000008

#define	PCI_MAPREG_MEM_ADDR(mr)						\
	    ((mr) & PCI_MAPREG_MEM_ADDR_MASK)
#define	PCI_MAPREG_MEM_SIZE(mr)						\
	    (PCI_MAPREG_MEM_ADDR(mr) & -PCI_MAPREG_MEM_ADDR(mr))
#define	PCI_MAPREG_MEM_ADDR_MASK		0xfffffff0

#define	PCI_MAPREG_MEM64_ADDR(mr)					\
	    ((mr) & PCI_MAPREG_MEM64_ADDR_MASK)
#define	PCI_MAPREG_MEM64_SIZE(mr)					\
	    (PCI_MAPREG_MEM64_ADDR(mr) & -PCI_MAPREG_MEM64_ADDR(mr))
#define	PCI_MAPREG_MEM64_ADDR_MASK		0xfffffffffffffff0ULL

#define	PCI_MAPREG_IO_ADDR(mr)						\
	    ((mr) & PCI_MAPREG_IO_ADDR_MASK)
#define	PCI_MAPREG_IO_SIZE(mr)						\
	    (PCI_MAPREG_IO_ADDR(mr) & -PCI_MAPREG_IO_ADDR(mr))
#define	PCI_MAPREG_IO_ADDR_MASK			0xfffffffe

/*
 * Cardbus CIS pointer (PCI rev. 2.1)
 */
#define PCI_CARDBUS_CIS_REG 0x28

/*
 * Subsystem identification register; contains a vendor ID and a device ID.
 * Types/macros for PCI_ID_REG apply.
 * (PCI rev. 2.1)
 */
#define PCI_SUBSYS_ID_REG 0x2c

/*
 * Expansion ROM Base Address register
 * (PCI rev. 2.0)
 */
#define PCI_ROM_REG 0x30

#define PCI_ROM_ENABLE			0x00000001
#define PCI_ROM_ADDR_MASK		0xfffff800
#define PCI_ROM_ADDR(mr)						\
	    ((mr) & PCI_ROM_ADDR_MASK)
#define PCI_ROM_SIZE(mr)						\
	    (PCI_ROM_ADDR(mr) & -PCI_ROM_ADDR(mr))

/*
 * capabilities link list (PCI rev. 2.2)
 */
#define PCI_CAPLISTPTR_REG		0x34	/* header type 0 */
#define PCI_CARDBUS_CAPLISTPTR_REG	0x14	/* header type 2 */
#define PCI_CAPLIST_PTR(cpr) ((cpr) & 0xff)
#define PCI_CAPLIST_NEXT(cr) (((cr) >> 8) & 0xff)
#define PCI_CAPLIST_CAP(cr) ((cr) & 0xff)

#define PCI_CAP_RESERVED	0x00
#define PCI_CAP_PWRMGMT		0x01
#define PCI_CAP_AGP		0x02
#define PCI_CAP_VPD		0x03
#define PCI_CAP_SLOTID		0x04
#define PCI_CAP_MSI		0x05
#define PCI_CAP_CPCI_HOTSWAP	0x06
#define PCI_CAP_PCIX		0x07
#define PCI_CAP_HT		0x08
#define PCI_CAP_VENDSPEC	0x09
#define PCI_CAP_DEBUGPORT	0x0a
#define PCI_CAP_CPCI_RSRCCTL	0x0b
#define PCI_CAP_HOTPLUG		0x0c
#define PCI_CAP_AGP8		0x0e
#define PCI_CAP_SECURE		0x0f
#define PCI_CAP_PCIEXPRESS     	0x10
#define PCI_CAP_MSIX		0x11
#define PCI_CAP_SATA		0x12

/*
 * Vital Product Data; access via capability pointer (PCI rev 2.2).
 */
#define	PCI_VPD_ADDRESS_MASK	0x7fff
#define	PCI_VPD_ADDRESS_SHIFT	16
#define	PCI_VPD_ADDRESS(ofs)	\
	(((ofs) & PCI_VPD_ADDRESS_MASK) << PCI_VPD_ADDRESS_SHIFT)
#define	PCI_VPD_DATAREG(ofs)	((ofs) + 4)
#define	PCI_VPD_OPFLAG		0x80000000

/*
 * Message Signaled Interrupts; access via capability pointer.
 */
#define PCI_MSI_MC		0x00
#define PCI_MSI_MC_PVMASK	0x01000000
#define PCI_MSI_MC_C64		0x00800000
#define PCI_MSI_MC_MME_MASK	0x00700000
#define PCI_MSI_MC_MME_SHIFT	20
#define PCI_MSI_MC_MMC_MASK	0x000e0000
#define PCI_MSI_MC_MMC_SHIFT	17
#define PCI_MSI_MC_MSIE		0x00010000
#define PCI_MSI_MA		0x04
#define PCI_MSI_MAU32		0x08
#define PCI_MSI_MD32		0x08
#define PCI_MSI_MD64		0x0c
#define PCI_MSI_MASK32		0x0c
#define PCI_MSI_MASK64		0x10

/*
 * Power Management Control Status Register; access via capability pointer.
 */
#define PCI_PMCSR		0x04
#define PCI_PMCSR_STATE_MASK	0x0003
#define PCI_PMCSR_STATE_D0	0x0000
#define PCI_PMCSR_STATE_D1	0x0001
#define PCI_PMCSR_STATE_D2	0x0002
#define PCI_PMCSR_STATE_D3	0x0003
#define PCI_PMCSR_PME_STATUS	0x8000
#define PCI_PMCSR_PME_EN	0x0100

/*
 * HyperTransport; access via capability pointer.
 */
#define PCI_HT_CAP(cr) ((((cr) >> 27) < 0x08) ? \
    (((cr) >> 27) & 0x1c) : (((cr) >> 27) & 0x1f))

#define PCI_HT_CAP_SLAVE	0x00
#define PCI_HT_CAP_HOST		0x04
#define PCI_HT_CAP_INTR		0x10
#define PCI_HT_CAP_MSI		0x15

#define PCI_HT_MSI_ENABLED	0x00010000
#define PCI_HT_MSI_FIXED	0x00020000

#define PCI_HT_MSI_FIXED_ADDR	0xfee00000UL

#define PCI_HT_MSI_ADDR		0x04
#define PCI_HT_MSI_ADDR_HI32	0x08

#define PCI_HT_INTR_DATA	0x04

/*
 * PCI Express; access via capability pointer.
 */
#define PCI_PCIE_XCAP		0x00
#define PCI_PCIE_XCAP_SI	0x01000000
#define PCI_PCIE_XCAP_VER(x)	(((x) >> 16) & 0x0f)
#define PCI_PCIE_XCAP_TYPE(x)	(((x) >> 20) & 0x0f)
#define  PCI_PCIE_XCAP_TYPE_RP		0x4
#define  PCI_PCIE_XCAP_TYPE_DOWN	0x6
#define  PCI_PCIE_XCAP_TYPE_PCI2PCIE	0x8
#define PCI_PCIE_DCAP		0x04
#define PCI_PCIE_DCSR		0x08
#define PCI_PCIE_DCSR_ERO	0x00000010
#define PCI_PCIE_DCSR_ENS	0x00000800
#define PCI_PCIE_DCSR_MPS	0x00007000
#define PCI_PCIE_DCSR_CEE	0x00010000
#define PCI_PCIE_DCSR_NFE	0x00020000
#define PCI_PCIE_DCSR_FEE	0x00040000
#define PCI_PCIE_DCSR_URE	0x00080000
#define PCI_PCIE_LCAP		0x0c
#define PCI_PCIE_LCSR		0x10
#define PCI_PCIE_LCSR_ASPM_L0S	0x00000001
#define PCI_PCIE_LCSR_ASPM_L1	0x00000002
#define PCI_PCIE_LCSR_RL	0x00000020
#define PCI_PCIE_LCSR_CCC	0x00000040
#define PCI_PCIE_LCSR_ES	0x00000080
#define PCI_PCIE_LCSR_ECPM	0x00000100
#define PCI_PCIE_LCSR_CLS	0x000f0000
#define PCI_PCIE_LCSR_CLS_2_5	0x00010000
#define PCI_PCIE_LCSR_CLS_5	0x00020000
#define PCI_PCIE_LCSR_CLS_8	0x00030000
#define PCI_PCIE_LCSR_CLS_16	0x00040000
#define PCI_PCIE_LCSR_CLS_32	0x00050000
#define PCI_PCIE_LCSR_LT	0x08000000
#define PCI_PCIE_LCSR_SCC	0x10000000
#define PCI_PCIE_SLCAP		0x14
#define PCI_PCIE_SLCAP_ABP	0x00000001
#define PCI_PCIE_SLCAP_PCP	0x00000002
#define PCI_PCIE_SLCAP_MSP	0x00000004
#define PCI_PCIE_SLCAP_AIP	0x00000008
#define PCI_PCIE_SLCAP_PIP	0x00000010
#define PCI_PCIE_SLCAP_HPS	0x00000020
#define PCI_PCIE_SLCAP_HPC	0x00000040
#define PCI_PCIE_SLCSR		0x18
#define PCI_PCIE_SLCSR_ABE	0x00000001
#define PCI_PCIE_SLCSR_PFE	0x00000002
#define PCI_PCIE_SLCSR_MSE	0x00000004
#define PCI_PCIE_SLCSR_PDE	0x00000008
#define PCI_PCIE_SLCSR_CCE	0x00000010
#define PCI_PCIE_SLCSR_HPE	0x00000020
#define PCI_PCIE_SLCSR_ABP	0x00010000
#define PCI_PCIE_SLCSR_PFD	0x00020000
#define PCI_PCIE_SLCSR_MSC	0x00040000
#define PCI_PCIE_SLCSR_PDC	0x00080000
#define PCI_PCIE_SLCSR_CC	0x00100000
#define PCI_PCIE_SLCSR_MS	0x00200000
#define PCI_PCIE_SLCSR_PDS	0x00400000
#define PCI_PCIE_SLCSR_LACS	0x01000000
#define PCI_PCIE_RCSR		0x1c
#define PCI_PCIE_DCSR2		0x28
#define PCI_PCIE_DCSR2_LTREN	0x00000400
#define PCI_PCIE_LCAP2		0x2c
#define PCI_PCIE_LCSR2		0x30
#define PCI_PCIE_LCSR2_TLS	0x0000000f
#define PCI_PCIE_LCSR2_TLS_2_5	0x00000001
#define PCI_PCIE_LCSR2_TLS_5	0x00000002
#define PCI_PCIE_LCSR2_TLS_8	0x00000003
#define PCI_PCIE_LCSR2_TLS_16	0x00000004
#define PCI_PCIE_LCSR2_TLS_32	0x00000005

/*
 * PCI Express; enhanced capabilities
 */
#define PCI_PCIE_ECAP		0x100
#define	PCI_PCIE_ECAP_ID(x)	(((x) & 0x0000ffff))
#define PCI_PCIE_ECAP_VER(x)	(((x) >> 16) & 0x0f)
#define	PCI_PCIE_ECAP_NEXT(x)	(((x) >> 20) & 0xffc)
#define PCI_PCIE_ECAP_LAST	0x0

/*
 * Extended Message Signaled Interrupts; access via capability pointer.
 */
#define PCI_MSIX_MC_MSIXE	0x80000000
#define PCI_MSIX_MC_FM		0x40000000
#define PCI_MSIX_MC_TBLSZ_MASK	0x07ff0000
#define PCI_MSIX_MC_TBLSZ_SHIFT	16
#define PCI_MSIX_MC_TBLSZ(reg)	\
	(((reg) & PCI_MSIX_MC_TBLSZ_MASK) >> PCI_MSIX_MC_TBLSZ_SHIFT)
#define PCI_MSIX_TABLE		0x04
#define  PCI_MSIX_TABLE_BIR	0x00000007
#define  PCI_MSIX_TABLE_OFF	~(PCI_MSIX_TABLE_BIR)

#define PCI_MSIX_MA(i)		((i) * 16 + 0)
#define PCI_MSIX_MAU32(i)	((i) * 16 + 4)
#define PCI_MSIX_MD(i)		((i) * 16 + 8)
#define PCI_MSIX_VC(i)		((i) * 16 + 12)
#define  PCI_MSIX_VC_MASK	0x00000001

/*
 * Interrupt Configuration Register; contains interrupt pin and line.
 */
#define	PCI_INTERRUPT_REG		0x3c

typedef u_int8_t pci_intr_pin_t;
typedef u_int8_t pci_intr_line_t;

#define	PCI_INTERRUPT_PIN_SHIFT			8
#define	PCI_INTERRUPT_PIN_MASK			0xff
#define	PCI_INTERRUPT_PIN(icr) \
	    (((icr) >> PCI_INTERRUPT_PIN_SHIFT) & PCI_INTERRUPT_PIN_MASK)

#define	PCI_INTERRUPT_LINE_SHIFT		0
#define	PCI_INTERRUPT_LINE_MASK			0xff
#define	PCI_INTERRUPT_LINE(icr) \
	    (((icr) >> PCI_INTERRUPT_LINE_SHIFT) & PCI_INTERRUPT_LINE_MASK)

#define	PCI_MIN_GNT_SHIFT			16
#define	PCI_MIN_GNT_MASK			0xff
#define	PCI_MIN_GNT(icr) \
	    (((icr) >> PCI_MIN_GNT_SHIFT) & PCI_MIN_GNT_MASK)

#define	PCI_MAX_LAT_SHIFT			24
#define	PCI_MAX_LAT_MASK			0xff
#define	PCI_MAX_LAT(icr) \
	    (((icr) >> PCI_MAX_LAT_SHIFT) & PCI_MAX_LAT_MASK)

#define	PCI_INTERRUPT_PIN_NONE			0x00
#define	PCI_INTERRUPT_PIN_A			0x01
#define	PCI_INTERRUPT_PIN_B			0x02
#define	PCI_INTERRUPT_PIN_C			0x03
#define	PCI_INTERRUPT_PIN_D			0x04
#define	PCI_INTERRUPT_PIN_MAX			0x04

/*
 * Vital Product Data resource tags.
 */
struct pci_vpd_smallres {
	uint8_t		vpdres_byte0;		/* length of data + tag */
	/* Actual data. */
} __packed;

struct pci_vpd_largeres {
	uint8_t		vpdres_byte0;
	uint8_t		vpdres_len_lsb;		/* length of data only */
	uint8_t		vpdres_len_msb;
	/* Actual data. */
} __packed;

#define	PCI_VPDRES_ISLARGE(x)			((x) & 0x80)

#define	PCI_VPDRES_SMALL_LENGTH(x)		((x) & 0x7)
#define	PCI_VPDRES_SMALL_NAME(x)		(((x) >> 3) & 0xf)

#define	PCI_VPDRES_LARGE_NAME(x)		((x) & 0x7f)

#define	PCI_VPDRES_TYPE_COMPATIBLE_DEVICE_ID	0x3	/* small */
#define	PCI_VPDRES_TYPE_VENDOR_DEFINED		0xe	/* small */
#define	PCI_VPDRES_TYPE_END_TAG			0xf	/* small */

#define	PCI_VPDRES_TYPE_IDENTIFIER_STRING	0x02	/* large */
#define	PCI_VPDRES_TYPE_VPD			0x10	/* large */

struct pci_vpd {
	uint8_t		vpd_key0;
	uint8_t		vpd_key1;
	uint8_t		vpd_len;		/* length of data only */
	/* Actual data. */
} __packed;

/*
 * Recommended VPD fields:
 *
 *	PN		Part number of assembly
 *	FN		FRU part number
 *	EC		EC level of assembly
 *	MN		Manufacture ID
 *	SN		Serial Number
 *
 * Conditionally recommended VPD fields:
 *
 *	LI		Load ID
 *	RL		ROM Level
 *	RM		Alterable ROM Level
 *	NA		Network Address
 *	DD		Device Driver Level
 *	DG		Diagnostic Level
 *	LL		Loadable Microcode Level
 *	VI		Vendor ID/Device ID
 *	FU		Function Number
 *	SI		Subsystem Vendor ID/Subsystem ID
 *
 * Additional VPD fields:
 *
 *	Z0-ZZ		User/Product Specific
 */

#endif /* _DEV_PCI_PCIREG_H_ */
