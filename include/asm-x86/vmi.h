/*
 * VMI interface definition
 *
 * Copyright (C) 2005, VMware, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Maintained by: Zachary Amsden zach@vmware.com
 *
 */
#include <linux/types.h>

/*
 *---------------------------------------------------------------------
 *
 *  VMI Option ROM API
 *
 *---------------------------------------------------------------------
 */
#define VMI_SIGNATURE 0x696d5663   /* "cVmi" */

#define PCI_VENDOR_ID_VMWARE            0x15AD
#define PCI_DEVICE_ID_VMWARE_VMI        0x0801

/*
 * We use two version numbers for compatibility, with the major
 * number signifying interface breakages, and the minor number
 * interface extensions.
 */
#define VMI_API_REV_MAJOR       3
#define VMI_API_REV_MINOR       0

#define VMI_CALL_CPUID			0
#define VMI_CALL_WRMSR			1
#define VMI_CALL_RDMSR			2
#define VMI_CALL_SetGDT			3
#define VMI_CALL_SetLDT			4
#define VMI_CALL_SetIDT			5
#define VMI_CALL_SetTR			6
#define VMI_CALL_GetGDT			7
#define VMI_CALL_GetLDT			8
#define VMI_CALL_GetIDT			9
#define VMI_CALL_GetTR			10
#define VMI_CALL_WriteGDTEntry		11
#define VMI_CALL_WriteLDTEntry		12
#define VMI_CALL_WriteIDTEntry		13
#define VMI_CALL_UpdateKernelStack	14
#define VMI_CALL_SetCR0			15
#define VMI_CALL_SetCR2			16
#define VMI_CALL_SetCR3			17
#define VMI_CALL_SetCR4			18
#define VMI_CALL_GetCR0			19
#define VMI_CALL_GetCR2			20
#define VMI_CALL_GetCR3			21
#define VMI_CALL_GetCR4			22
#define VMI_CALL_WBINVD			23
#define VMI_CALL_SetDR			24
#define VMI_CALL_GetDR			25
#define VMI_CALL_RDPMC			26
#define VMI_CALL_RDTSC			27
#define VMI_CALL_CLTS			28
#define VMI_CALL_EnableInterrupts	29
#define VMI_CALL_DisableInterrupts	30
#define VMI_CALL_GetInterruptMask	31
#define VMI_CALL_SetInterruptMask	32
#define VMI_CALL_IRET			33
#define VMI_CALL_SYSEXIT		34
#define VMI_CALL_Halt			35
#define VMI_CALL_Reboot			36
#define VMI_CALL_Shutdown		37
#define VMI_CALL_SetPxE			38
#define VMI_CALL_SetPxELong		39
#define VMI_CALL_UpdatePxE		40
#define VMI_CALL_UpdatePxELong		41
#define VMI_CALL_MachineToPhysical	42
#define VMI_CALL_PhysicalToMachine	43
#define VMI_CALL_AllocatePage		44
#define VMI_CALL_ReleasePage		45
#define VMI_CALL_InvalPage		46
#define VMI_CALL_FlushTLB		47
#define VMI_CALL_SetLinearMapping	48

#define VMI_CALL_SetIOPLMask		61
#define VMI_CALL_SetInitialAPState	62
#define VMI_CALL_APICWrite		63
#define VMI_CALL_APICRead		64
#define VMI_CALL_IODelay		65
#define VMI_CALL_SetLazyMode		73

/*
 *---------------------------------------------------------------------
 *
 * MMU operation flags
 *
 *---------------------------------------------------------------------
 */

/* Flags used by VMI_{Allocate|Release}Page call */
#define VMI_PAGE_PAE             0x10  /* Allocate PAE shadow */
#define VMI_PAGE_CLONE           0x20  /* Clone from another shadow */
#define VMI_PAGE_ZEROED          0x40  /* Page is pre-zeroed */


/* Flags shared by Allocate|Release Page and PTE updates */
#define VMI_PAGE_PT              0x01
#define VMI_PAGE_PD              0x02
#define VMI_PAGE_PDP             0x04
#define VMI_PAGE_PML4            0x08

#define VMI_PAGE_NORMAL          0x00 /* for debugging */

/* Flags used by PTE updates */
#define VMI_PAGE_CURRENT_AS      0x10 /* implies VMI_PAGE_VA_MASK is valid */
#define VMI_PAGE_DEFER           0x20 /* may queue update until TLB inval */
#define VMI_PAGE_VA_MASK         0xfffff000

#ifdef CONFIG_X86_PAE
#define VMI_PAGE_L1		(VMI_PAGE_PT | VMI_PAGE_PAE | VMI_PAGE_ZEROED)
#define VMI_PAGE_L2		(VMI_PAGE_PD | VMI_PAGE_PAE | VMI_PAGE_ZEROED)
#else
#define VMI_PAGE_L1		(VMI_PAGE_PT | VMI_PAGE_ZEROED)
#define VMI_PAGE_L2		(VMI_PAGE_PD | VMI_PAGE_ZEROED)
#endif

/* Flags used by VMI_FlushTLB call */
#define VMI_FLUSH_TLB            0x01
#define VMI_FLUSH_GLOBAL         0x02

/*
 *---------------------------------------------------------------------
 *
 *  VMI relocation definitions for ROM call get_reloc
 *
 *---------------------------------------------------------------------
 */

/* VMI Relocation types */
#define VMI_RELOCATION_NONE     0
#define VMI_RELOCATION_CALL_REL 1
#define VMI_RELOCATION_JUMP_REL 2
#define VMI_RELOCATION_NOP	3

#ifndef __ASSEMBLY__
struct vmi_relocation_info {
	unsigned char           *eip;
	unsigned char           type;
	unsigned char           reserved[3];
};
#endif


/*
 *---------------------------------------------------------------------
 *
 *  Generic ROM structures and definitions
 *
 *---------------------------------------------------------------------
 */

#ifndef __ASSEMBLY__

struct vrom_header {
	u16     rom_signature;  /* option ROM signature */
	u8      rom_length;     /* ROM length in 512 byte chunks */
	u8      rom_entry[4];   /* 16-bit code entry point */
	u8      rom_pad0;       /* 4-byte align pad */
	u32     vrom_signature; /* VROM identification signature */
	u8      api_version_min;/* Minor version of API */
	u8      api_version_maj;/* Major version of API */
	u8      jump_slots;     /* Number of jump slots */
	u8      reserved1;      /* Reserved for expansion */
	u32     virtual_top;    /* Hypervisor virtual address start */
	u16     reserved2;      /* Reserved for expansion */
	u16	license_offs;	/* Offset to License string */
	u16     pci_header_offs;/* Offset to PCI OPROM header */
	u16     pnp_header_offs;/* Offset to PnP OPROM header */
	u32     rom_pad3;       /* PnP reserverd / VMI reserved */
	u8      reserved[96];   /* Reserved for headers */
	char    vmi_init[8];    /* VMI_Init jump point */
	char    get_reloc[8];   /* VMI_GetRelocationInfo jump point */
} __attribute__((packed));

struct pnp_header {
	char sig[4];
	char rev;
	char size;
	short next;
	short res;
	long devID;
	unsigned short manufacturer_offset;
	unsigned short product_offset;
} __attribute__((packed));

struct pci_header {
	char sig[4];
	short vendorID;
	short deviceID;
	short vpdData;
	short size;
	char rev;
	char class;
	char subclass;
	char interface;
	short chunks;
	char rom_version_min;
	char rom_version_maj;
	char codetype;
	char lastRom;
	short reserved;
} __attribute__((packed));

/* Function prototypes for bootstrapping */
extern void vmi_init(void);
extern void vmi_bringup(void);
extern void vmi_apply_boot_page_allocations(void);

/* State needed to start an application processor in an SMP system. */
struct vmi_ap_state {
	u32 cr0;
	u32 cr2;
	u32 cr3;
	u32 cr4;

	u64 efer;

	u32 eip;
	u32 eflags;
	u32 eax;
	u32 ebx;
	u32 ecx;
	u32 edx;
	u32 esp;
	u32 ebp;
	u32 esi;
	u32 edi;
	u16 cs;
	u16 ss;
	u16 ds;
	u16 es;
	u16 fs;
	u16 gs;
	u16 ldtr;

	u16 gdtr_limit;
	u32 gdtr_base;
	u32 idtr_base;
	u16 idtr_limit;
};

#endif
