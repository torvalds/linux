/*	$OpenBSD: pci_subr.c,v 1.22 2017/03/22 07:21:39 jsg Exp $	*/
/*	$NetBSD: pci_subr.c,v 1.19 1996/10/13 01:38:29 christos Exp $	*/

/*
 * Copyright (c) 1995, 1996 Christopher G. Demetriou.  All rights reserved.
 * Copyright (c) 1994 Charles Hannum.  All rights reserved.
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

/*
 * PCI autoconfiguration support functions.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#ifdef PCIVERBOSE
#include <dev/pci/pcidevs.h>
#include <dev/pci/pcidevs_data.h>
#endif

/*
 * Descriptions of known PCI classes and subclasses.
 *
 * Subclasses are described in the same way as classes, but have a
 * NULL subclass pointer.
 */
struct pci_class {
	const char	*name;
	int		val;		/* as wide as pci_{,sub}class_t */
	const struct pci_class *subclasses;
};

const struct pci_class pci_subclass_prehistoric[] = {
	{ "miscellaneous",	PCI_SUBCLASS_PREHISTORIC_MISC,		},
	{ "VGA",		PCI_SUBCLASS_PREHISTORIC_VGA,		},
	{ 0 }
};

const struct pci_class pci_subclass_mass_storage[] = {
	{ "SCSI",		PCI_SUBCLASS_MASS_STORAGE_SCSI,		},
	{ "IDE",		PCI_SUBCLASS_MASS_STORAGE_IDE,		},
	{ "floppy",		PCI_SUBCLASS_MASS_STORAGE_FLOPPY,	},
	{ "IPI",		PCI_SUBCLASS_MASS_STORAGE_IPI,		},
	{ "RAID",		PCI_SUBCLASS_MASS_STORAGE_RAID,		},
	{ "ATA",		PCI_SUBCLASS_MASS_STORAGE_ATA,		},
	{ "SATA",		PCI_SUBCLASS_MASS_STORAGE_SATA,		},
	{ "SAS",		PCI_SUBCLASS_MASS_STORAGE_SAS,		},
	{ "UFS",		PCI_SUBCLASS_MASS_STORAGE_UFS,		},
	{ "miscellaneous",	PCI_SUBCLASS_MASS_STORAGE_MISC,		},
	{ 0 },
};

const struct pci_class pci_subclass_network[] = {
	{ "ethernet",		PCI_SUBCLASS_NETWORK_ETHERNET,		},
	{ "token ring",		PCI_SUBCLASS_NETWORK_TOKENRING,		},
	{ "FDDI",		PCI_SUBCLASS_NETWORK_FDDI,		},
	{ "ATM",		PCI_SUBCLASS_NETWORK_ATM,		},
	{ "ISDN",		PCI_SUBCLASS_NETWORK_ISDN,		},
	{ "WorldFip",		PCI_SUBCLASS_NETWORK_WORLDFIP,		},
	{ "PCMIG Multi Computing", PCI_SUBCLASS_NETWORK_PCIMGMULTICOMP,	},
	{ "InfiniBand",		PCI_SUBCLASS_NETWORK_INFINIBAND,	},
	{ "miscellaneous",	PCI_SUBCLASS_NETWORK_MISC,		},
	{ 0 },
};

const struct pci_class pci_subclass_display[] = {
	{ "VGA",		PCI_SUBCLASS_DISPLAY_VGA,		},
	{ "XGA",		PCI_SUBCLASS_DISPLAY_XGA,		},
	{ "3D",			PCI_SUBCLASS_DISPLAY_3D,		},
	{ "miscellaneous",	PCI_SUBCLASS_DISPLAY_MISC,		},
	{ 0 },
};

const struct pci_class pci_subclass_multimedia[] = {
	{ "video",		PCI_SUBCLASS_MULTIMEDIA_VIDEO,		},
	{ "audio",		PCI_SUBCLASS_MULTIMEDIA_AUDIO,		},
	{ "telephony",		PCI_SUBCLASS_MULTIMEDIA_TELEPHONY,	},
	{ "hdaudio",		PCI_SUBCLASS_MULTIMEDIA_HDAUDIO,	},
	{ "miscellaneous",	PCI_SUBCLASS_MULTIMEDIA_MISC,		},
	{ 0 },
};

const struct pci_class pci_subclass_memory[] = {
	{ "RAM",		PCI_SUBCLASS_MEMORY_RAM,		},
	{ "flash",		PCI_SUBCLASS_MEMORY_FLASH,		},
	{ "miscellaneous",	PCI_SUBCLASS_MEMORY_MISC,		},
	{ 0 },
};

const struct pci_class pci_subclass_bridge[] = {
	{ "host",		PCI_SUBCLASS_BRIDGE_HOST,		},
	{ "ISA",		PCI_SUBCLASS_BRIDGE_ISA,		},
	{ "EISA",		PCI_SUBCLASS_BRIDGE_EISA,		},
	{ "MicroChannel",	PCI_SUBCLASS_BRIDGE_MC,			},
	{ "PCI",		PCI_SUBCLASS_BRIDGE_PCI,		},
	{ "PCMCIA",		PCI_SUBCLASS_BRIDGE_PCMCIA,		},
	{ "NuBus",		PCI_SUBCLASS_BRIDGE_NUBUS,		},
	{ "CardBus",		PCI_SUBCLASS_BRIDGE_CARDBUS,		},
	{ "RACEway",		PCI_SUBCLASS_BRIDGE_RACEWAY,		},
	{ "Semi-transparent PCI", PCI_SUBCLASS_BRIDGE_STPCI,		},
	{ "InfiniBand",		PCI_SUBCLASS_BRIDGE_INFINIBAND,		},
	{ "miscellaneous",	PCI_SUBCLASS_BRIDGE_MISC,		},
	{ "advanced switching",	PCI_SUBCLASS_BRIDGE_AS,			},
	{ 0 },
};

const struct pci_class pci_subclass_communications[] = {
	{ "serial",		PCI_SUBCLASS_COMMUNICATIONS_SERIAL,	},
	{ "parallel",		PCI_SUBCLASS_COMMUNICATIONS_PARALLEL,	},
	{ "multi-port serial",	PCI_SUBCLASS_COMMUNICATIONS_MPSERIAL,	},
	{ "modem",		PCI_SUBCLASS_COMMUNICATIONS_MODEM,	},
	{ "GPIB",		PCI_SUBCLASS_COMMUNICATIONS_GPIB,	},
	{ "smartcard",		PCI_SUBCLASS_COMMUNICATIONS_SMARTCARD,	},
	{ "miscellaneous",	PCI_SUBCLASS_COMMUNICATIONS_MISC,	},
	{ 0 },
};

const struct pci_class pci_subclass_system[] = {
	{ "interrupt",		PCI_SUBCLASS_SYSTEM_PIC,		},
	{ "8237 DMA",		PCI_SUBCLASS_SYSTEM_DMA,		},
	{ "8254 timer",		PCI_SUBCLASS_SYSTEM_TIMER,		},
	{ "RTC",		PCI_SUBCLASS_SYSTEM_RTC,		},
	{ "PCI Hot-Plug",	PCI_SUBCLASS_SYSTEM_PCIHOTPLUG,		},
	{ "SD Host Controller",	PCI_SUBCLASS_SYSTEM_SDHC,		},
	{ "IOMMU",		PCI_SUBCLASS_SYSTEM_IOMMU,		},
	{ "root complex event",	PCI_SUBCLASS_SYSTEM_ROOTCOMPEVENT,	},
	{ "miscellaneous",	PCI_SUBCLASS_SYSTEM_MISC,		},
	{ 0 },
};

const struct pci_class pci_subclass_input[] = {
	{ "keyboard",		PCI_SUBCLASS_INPUT_KEYBOARD,		},
	{ "digitizer",		PCI_SUBCLASS_INPUT_DIGITIZER,		},
	{ "mouse",		PCI_SUBCLASS_INPUT_MOUSE,		},
	{ "scanner",		PCI_SUBCLASS_INPUT_SCANNER,		},
	{ "game port",		PCI_SUBCLASS_INPUT_GAMEPORT,		},
	{ "miscellaneous",	PCI_SUBCLASS_INPUT_MISC,		},
	{ 0 },
};

const struct pci_class pci_subclass_dock[] = {
	{ "generic",		PCI_SUBCLASS_DOCK_GENERIC,		},
	{ "miscellaneous",	PCI_SUBCLASS_DOCK_MISC,			},
	{ 0 },
};

const struct pci_class pci_subclass_processor[] = {
	{ "386",		PCI_SUBCLASS_PROCESSOR_386,		},
	{ "486",		PCI_SUBCLASS_PROCESSOR_486,		},
	{ "Pentium",		PCI_SUBCLASS_PROCESSOR_PENTIUM,		},
	{ "Alpha",		PCI_SUBCLASS_PROCESSOR_ALPHA,		},
	{ "PowerPC",		PCI_SUBCLASS_PROCESSOR_POWERPC,		},
	{ "MIPS",		PCI_SUBCLASS_PROCESSOR_MIPS,		},
	{ "Co-processor",	PCI_SUBCLASS_PROCESSOR_COPROC,		},
	{ 0 },
};

const struct pci_class pci_subclass_serialbus[] = {
	{ "Firewire",		PCI_SUBCLASS_SERIALBUS_FIREWIRE,	},
	{ "ACCESS.bus",		PCI_SUBCLASS_SERIALBUS_ACCESS,		},
	{ "SSA",		PCI_SUBCLASS_SERIALBUS_SSA,		},
	{ "USB",		PCI_SUBCLASS_SERIALBUS_USB,		},
	/* XXX Fiber Channel/_FIBRECHANNEL */
	{ "Fiber Channel",	PCI_SUBCLASS_SERIALBUS_FIBER,		},
	{ "SMBus",		PCI_SUBCLASS_SERIALBUS_SMBUS,		},
	{ "InfiniBand",		PCI_SUBCLASS_SERIALBUS_INFINIBAND,	},
	{ "IPMI",		PCI_SUBCLASS_SERIALBUS_IPMI,		},
	{ "SERCOS",		PCI_SUBCLASS_SERIALBUS_SERCOS,		},
	{ "CANbus",		PCI_SUBCLASS_SERIALBUS_CANBUS,		},
	{ 0 },
};

const struct pci_class pci_subclass_wireless[] = {
	{ "IrDA",		PCI_SUBCLASS_WIRELESS_IRDA,		},
	{ "Consumer IR",	PCI_SUBCLASS_WIRELESS_CONSUMERIR,	},
	{ "RF",			PCI_SUBCLASS_WIRELESS_RF,		},
	{ "bluetooth",		PCI_SUBCLASS_WIRELESS_BLUETOOTH,	},
	{ "broadband",		PCI_SUBCLASS_WIRELESS_BROADBAND,	},
	{ "802.11a (5 GHz)",	PCI_SUBCLASS_WIRELESS_802_11A,		},
	{ "802.11b (2.4 GHz)",	PCI_SUBCLASS_WIRELESS_802_11B,		},
	{ "miscellaneous",	PCI_SUBCLASS_WIRELESS_MISC,		},
	{ 0 },
};

const struct pci_class pci_subclass_i2o[] = {
	{ "standard",		PCI_SUBCLASS_I2O_STANDARD,		},
	{ 0 },
};

const struct pci_class pci_subclass_satcom[] = {
	{ "TV",			PCI_SUBCLASS_SATCOM_TV,			},
	{ "audio",		PCI_SUBCLASS_SATCOM_AUDIO,		},
	{ "voice",		PCI_SUBCLASS_SATCOM_VOICE,		},
	{ "data",		PCI_SUBCLASS_SATCOM_DATA,		},
	{ 0 },
};

const struct pci_class pci_subclass_crypto[] = {
	{ "network/computing",	PCI_SUBCLASS_CRYPTO_NETCOMP,		},
	{ "entertainment",	PCI_SUBCLASS_CRYPTO_ENTERTAINMENT,	},
	{ "miscellaneous",	PCI_SUBCLASS_CRYPTO_MISC,		},
	{ 0 },
};

const struct pci_class pci_subclass_dasp[] = {
	{ "DPIO",		PCI_SUBCLASS_DASP_DPIO,			},
	{ "Time and Frequency",	PCI_SUBCLASS_DASP_TIMEFREQ,		},
	{ "synchronization",	PCI_SUBCLASS_DASP_SYNC,			},
	{ "management",		PCI_SUBCLASS_DASP_MGMT,			},
	{ "miscellaneous",	PCI_SUBCLASS_DASP_MISC,			},
	{ 0 },
};

const struct pci_class pci_class[] = {
	{ "prehistoric",	PCI_CLASS_PREHISTORIC,
	    pci_subclass_prehistoric,				},
	{ "mass storage",	PCI_CLASS_MASS_STORAGE,
	    pci_subclass_mass_storage,				},
	{ "network",		PCI_CLASS_NETWORK,
	    pci_subclass_network,				},
	{ "display",		PCI_CLASS_DISPLAY,
	    pci_subclass_display,				},
	{ "multimedia",		PCI_CLASS_MULTIMEDIA,
	    pci_subclass_multimedia,				},
	{ "memory",		PCI_CLASS_MEMORY,
	    pci_subclass_memory,				},
	{ "bridge",		PCI_CLASS_BRIDGE,
	    pci_subclass_bridge,				},
	{ "communications",	PCI_CLASS_COMMUNICATIONS,
	    pci_subclass_communications,			},
	{ "system",		PCI_CLASS_SYSTEM,
	    pci_subclass_system,				},
	{ "input",		PCI_CLASS_INPUT,
	    pci_subclass_input,					},
	{ "dock",		PCI_CLASS_DOCK,
	    pci_subclass_dock,					},
	{ "processor",		PCI_CLASS_PROCESSOR,
	    pci_subclass_processor,				},
	{ "serial bus",		PCI_CLASS_SERIALBUS,
	    pci_subclass_serialbus,				},
	{ "wireless",		PCI_CLASS_WIRELESS,
	    pci_subclass_wireless,				},
	{ "I2O",		PCI_CLASS_I2O,
	    pci_subclass_i2o,					},
	{ "satellite comm",	PCI_CLASS_SATCOM,
	    pci_subclass_satcom,				},
	{ "crypto",		PCI_CLASS_CRYPTO,
	    pci_subclass_crypto,				},
	{ "DASP",		PCI_CLASS_DASP,
	    pci_subclass_dasp,					},
	{ "accelerator",	PCI_CLASS_ACCELERATOR,
	    NULL,						},
	{ "instrumentation",	PCI_CLASS_INSTRUMENTATION,
	    NULL,						},
	{ "undefined",		PCI_CLASS_UNDEFINED,
	    0,							},
	{ 0 },
};

const char *
pci_findvendor(pcireg_t id_reg)
{
#ifdef PCIVERBOSE
	pci_vendor_id_t vendor = PCI_VENDOR(id_reg);
	const struct pci_known_vendor *kdp;

	kdp = pci_known_vendors;
        while (kdp->vendorname != NULL) {	/* all have vendor name */
                if (kdp->vendor == vendor)
                        break;
		kdp++;
	}
        return (kdp->vendorname);
#else
	return (NULL);
#endif
}

const char *
pci_findproduct(pcireg_t id_reg)
{
#ifdef PCIVERBOSE
	pci_vendor_id_t vendor = PCI_VENDOR(id_reg);
	pci_product_id_t product = PCI_PRODUCT(id_reg);
	const struct pci_known_product *pkp;

	pkp = pci_known_products;
	while (pkp->productname != NULL) {	/* all have product name */
		if (pkp->vendor == vendor && pkp->product == product)
			break;
		pkp++;
	}
	return (pkp->productname);
#else
	return NULL;
#endif
}

void
pci_devinfo(pcireg_t id_reg, pcireg_t class_reg, int showclass, char *cp,
	    size_t cp_max)
{
	pci_vendor_id_t vendor;
	pci_product_id_t product;
	pci_class_t class;
	pci_subclass_t subclass;
	pci_interface_t interface;
	pci_revision_t revision;
	const char *vendor_namep = NULL, *product_namep = NULL;
	const struct pci_class *classp, *subclassp;
	size_t cp_len = 0;
#ifdef PCIVERBOSE
	const char *unmatched = "unknown ";
#else
	const char *unmatched = "";
#endif

	vendor = PCI_VENDOR(id_reg);
	product = PCI_PRODUCT(id_reg);

	class = PCI_CLASS(class_reg);
	subclass = PCI_SUBCLASS(class_reg);
	interface = PCI_INTERFACE(class_reg);
	revision = PCI_REVISION(class_reg);

#ifdef PCIVERBOSE
	vendor_namep = pci_findvendor(id_reg);
	if (vendor_namep != NULL)
		product_namep = pci_findproduct(id_reg);
#endif /* PCIVERBOSE */

	classp = pci_class;
	while (classp->name != NULL) {
		if (class == classp->val)
			break;
		classp++;
	}

	subclassp = (classp->name != NULL) ? classp->subclasses : NULL;
	while (subclassp && subclassp->name != NULL) {
		if (subclass == subclassp->val)
			break;
		subclassp++;
	}

	if (vendor_namep == NULL)
		snprintf(cp, cp_max, "%svendor 0x%04x product 0x%04x",
		    unmatched, vendor, product);
	else if (product_namep != NULL)
		snprintf(cp, cp_max, "\"%s %s\"", vendor_namep, product_namep);
	else
		snprintf(cp, cp_max, "vendor \"%s\", unknown product 0x%04x",
		    vendor_namep, product);
	if (showclass && product_namep == NULL) {
		strlcat(cp, " (", cp_max);
		cp_len = strlen(cp);
		if (classp->name == NULL)
			snprintf(cp + cp_len, cp_max - cp_len,
			    "unknown class 0x%02x, subclass 0x%02x",
			    class, subclass);
		else if (subclassp == NULL || subclassp->name == NULL)
			snprintf(cp + cp_len, cp_max - cp_len,
			    "class %s unknown subclass 0x%02x", classp->name,
			    subclass);
		else
			snprintf(cp + cp_len, cp_max - cp_len,
			    "class %s subclass %s", classp->name,
			    subclassp->name);
#if 0 /* not very useful */
		cp_len = strlen(cp);
		snprintf(cp + cp_len, cp_max - cp_len,
		    ", interface 0x%02x", interface);
#endif
		cp_len = strlen(cp);
		snprintf(cp + cp_len, cp_max - cp_len,
		    ", rev 0x%02x)", revision);
	} else {
		cp_len = strlen(cp);
		snprintf(cp + cp_len, cp_max - cp_len, " rev 0x%02x",
		    revision);
	}
}
