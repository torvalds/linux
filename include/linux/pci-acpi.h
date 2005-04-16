/*
 * File		pci-acpi.h
 *
 * Copyright (C) 2004 Intel
 * Copyright (C) Tom Long Nguyen (tom.l.nguyen@intel.com)
 */

#ifndef _PCI_ACPI_H_
#define _PCI_ACPI_H_

#define OSC_QUERY_TYPE			0
#define OSC_SUPPORT_TYPE 		1
#define OSC_CONTROL_TYPE		2
#define OSC_SUPPORT_MASKS		0x1f

/*
 * _OSC DW0 Definition 
 */
#define OSC_QUERY_ENABLE		1
#define OSC_REQUEST_ERROR		2
#define OSC_INVALID_UUID_ERROR		4
#define OSC_INVALID_REVISION_ERROR	8
#define OSC_CAPABILITIES_MASK_ERROR	16

/*
 * _OSC DW1 Definition (OS Support Fields)
 */
#define OSC_EXT_PCI_CONFIG_SUPPORT		1
#define OSC_ACTIVE_STATE_PWR_SUPPORT 		2
#define OSC_CLOCK_PWR_CAPABILITY_SUPPORT	4
#define OSC_PCI_SEGMENT_GROUPS_SUPPORT		8
#define OSC_MSI_SUPPORT				16

/*
 * _OSC DW1 Definition (OS Control Fields)
 */
#define OSC_PCI_EXPRESS_NATIVE_HP_CONTROL	1
#define OSC_SHPC_NATIVE_HP_CONTROL 		2
#define OSC_PCI_EXPRESS_PME_CONTROL		4
#define OSC_PCI_EXPRESS_AER_CONTROL		8
#define OSC_PCI_EXPRESS_CAP_STRUCTURE_CONTROL	16

#define OSC_CONTROL_MASKS 	(OSC_PCI_EXPRESS_NATIVE_HP_CONTROL | 	\
				OSC_SHPC_NATIVE_HP_CONTROL | 		\
				OSC_PCI_EXPRESS_PME_CONTROL |		\
				OSC_PCI_EXPRESS_AER_CONTROL |		\
				OSC_PCI_EXPRESS_CAP_STRUCTURE_CONTROL)

#ifdef CONFIG_ACPI
extern acpi_status pci_osc_control_set(u32 flags);
extern acpi_status pci_osc_support_set(u32 flags);
#else
#if !defined(acpi_status)
typedef u32 		acpi_status;
#define AE_ERROR      	(acpi_status) (0x0001)
#endif    
static inline acpi_status pci_osc_control_set(u32 flags) {return AE_ERROR;}
static inline acpi_status pci_osc_support_set(u32 flags) {return AE_ERROR;} 
#endif

#endif	/* _PCI_ACPI_H_ */
