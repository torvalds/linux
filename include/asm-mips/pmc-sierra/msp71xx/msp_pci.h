/*
 * Copyright (c) 2000-2006 PMC-Sierra INC.
 *
 *     This program is free software; you can redistribute it
 *     and/or modify it under the terms of the GNU General
 *     Public License as published by the Free Software
 *     Foundation; either version 2 of the License, or (at your
 *     option) any later version.
 *
 *     This program is distributed in the hope that it will be
 *     useful, but WITHOUT ANY WARRANTY; without even the implied
 *     warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *     PURPOSE.  See the GNU General Public License for more
 *     details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA
 *     02139, USA.
 *
 * PMC-SIERRA INC. DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS
 * SOFTWARE.
 */

#ifndef _MSP_PCI_H_
#define _MSP_PCI_H_

#define MSP_HAS_PCI(ID)	(((u32)(ID) <= 0x4236) && ((u32)(ID) >= 0x4220))

/*
 * It is convenient to program the OATRAN register so that
 * Athena virtual address space and PCI address space are
 * the same. This is not a requirement, just a convenience.
 *
 * The only hard restrictions on the value of OATRAN is that
 * OATRAN must not be programmed to allow translated memory
 * addresses to fall within the lowest 512MB of
 * PCI address space. This region is hardcoded
 * for use as Athena PCI Host Controller target
 * access memory space to the Athena's SDRAM.
 *
 * Note that OATRAN applies only to memory accesses, not
 * to I/O accesses.
 *
 * To program OATRAN to make Athena virtual address space
 * and PCI address space have the same values, OATRAN
 * is to be programmed to 0xB8000000. The top seven
 * bits of the value mimic the seven bits clipped off
 * by the PCI Host controller.
 *
 * With OATRAN at the said value, when the CPU does
 * an access to its virtual address at, say 0xB900_5000,
 * the address appearing on the PCI bus will be
 * 0xB900_5000.
 *    - Michael Penner
 */
#define MSP_PCI_OATRAN		0xB8000000UL

#define MSP_PCI_SPACE_BASE	(MSP_PCI_OATRAN + 0x1002000UL)
#define MSP_PCI_SPACE_SIZE	(0x3000000UL - 0x2000)
#define MSP_PCI_SPACE_END \
		(MSP_PCI_SPACE_BASE + MSP_PCI_SPACE_SIZE - 1)
#define MSP_PCI_IOSPACE_BASE	(MSP_PCI_OATRAN + 0x1001000UL)
#define MSP_PCI_IOSPACE_SIZE	0x1000
#define MSP_PCI_IOSPACE_END  \
		(MSP_PCI_IOSPACE_BASE + MSP_PCI_IOSPACE_SIZE - 1)

/* IRQ for PCI status interrupts */
#define PCI_STAT_IRQ	20

#define QFLUSH_REG_1	0xB7F40000

typedef volatile unsigned int pcireg;
typedef void * volatile ppcireg;

struct pci_block_copy
{
    pcireg   unused1; /* +0x00 */
    pcireg   unused2; /* +0x04 */
    ppcireg  unused3; /* +0x08 */
    ppcireg  unused4; /* +0x0C */
    pcireg   unused5; /* +0x10 */
    pcireg   unused6; /* +0x14 */
    pcireg   unused7; /* +0x18 */
    ppcireg  unused8; /* +0x1C */
    ppcireg  unused9; /* +0x20 */
    pcireg   unusedA; /* +0x24 */
    ppcireg  unusedB; /* +0x28 */
    ppcireg  unusedC; /* +0x2C */
};

enum
{
    config_device_vendor,  /* 0 */
    config_status_command, /* 1 */
    config_class_revision, /* 2 */
    config_BIST_header_latency_cache, /* 3 */
    config_BAR0,           /* 4 */
    config_BAR1,           /* 5 */
    config_BAR2,           /* 6 */
    config_not_used7,      /* 7 */
    config_not_used8,      /* 8 */
    config_not_used9,      /* 9 */
    config_CIS,            /* 10 */
    config_subsystem,      /* 11 */
    config_not_used12,     /* 12 */
    config_capabilities,   /* 13 */
    config_not_used14,     /* 14 */
    config_lat_grant_irq,  /* 15 */
    config_message_control,/* 16 */
    config_message_addr,   /* 17 */
    config_message_data,   /* 18 */
    config_VPD_addr,       /* 19 */
    config_VPD_data,       /* 20 */
    config_maxregs         /* 21 - number of registers */
};

struct msp_pci_regs
{
    pcireg hop_unused_00; /* +0x00 */
    pcireg hop_unused_04; /* +0x04 */
    pcireg hop_unused_08; /* +0x08 */
    pcireg hop_unused_0C; /* +0x0C */
    pcireg hop_unused_10; /* +0x10 */
    pcireg hop_unused_14; /* +0x14 */
    pcireg hop_unused_18; /* +0x18 */
    pcireg hop_unused_1C; /* +0x1C */
    pcireg hop_unused_20; /* +0x20 */
    pcireg hop_unused_24; /* +0x24 */
    pcireg hop_unused_28; /* +0x28 */
    pcireg hop_unused_2C; /* +0x2C */
    pcireg hop_unused_30; /* +0x30 */
    pcireg hop_unused_34; /* +0x34 */
    pcireg if_control;    /* +0x38 */
    pcireg oatran;        /* +0x3C */
    pcireg reset_ctl;     /* +0x40 */
    pcireg config_addr;   /* +0x44 */
    pcireg hop_unused_48; /* +0x48 */
    pcireg msg_signaled_int_status; /* +0x4C */
    pcireg msg_signaled_int_mask;   /* +0x50 */
    pcireg if_status;     /* +0x54 */
    pcireg if_mask;       /* +0x58 */
    pcireg hop_unused_5C; /* +0x5C */
    pcireg hop_unused_60; /* +0x60 */
    pcireg hop_unused_64; /* +0x64 */
    pcireg hop_unused_68; /* +0x68 */
    pcireg hop_unused_6C; /* +0x6C */
    pcireg hop_unused_70; /* +0x70 */

    struct pci_block_copy pci_bc[2] __attribute__((aligned(64)));

    pcireg error_hdr1; /* +0xE0 */
    pcireg error_hdr2; /* +0xE4 */

    pcireg config[config_maxregs] __attribute__((aligned(256)));

};

#define BPCI_CFGADDR_BUSNUM_SHF 16
#define BPCI_CFGADDR_FUNCTNUM_SHF 8
#define BPCI_CFGADDR_REGNUM_SHF 2
#define BPCI_CFGADDR_ENABLE (1<<31)

#define BPCI_IFCONTROL_RTO (1<<20) /* Retry timeout */
#define BPCI_IFCONTROL_HCE (1<<16) /* Host configuration enable */
#define BPCI_IFCONTROL_CTO_SHF 12  /* Shift count for CTO bits */
#define BPCI_IFCONTROL_SE  (1<<5)  /* Enable exceptions on errors */
#define BPCI_IFCONTROL_BIST (1<<4) /* Use BIST in per. mode */
#define BPCI_IFCONTROL_CAP (1<<3)  /* Enable capabilities */
#define BPCI_IFCONTROL_MMC_SHF 0   /* Shift count for MMC bits */

#define BPCI_IFSTATUS_MGT  (1<<8)  /* Master Grant timeout */
#define BPCI_IFSTATUS_MTT  (1<<9)  /* Master TRDY timeout */
#define BPCI_IFSTATUS_MRT  (1<<10) /* Master retry timeout */
#define BPCI_IFSTATUS_BC0F (1<<13) /* Block copy 0 fault */
#define BPCI_IFSTATUS_BC1F (1<<14) /* Block copy 1 fault */
#define BPCI_IFSTATUS_PCIU (1<<15) /* PCI unable to respond */
#define BPCI_IFSTATUS_BSIZ (1<<16) /* PCI access with illegal size */
#define BPCI_IFSTATUS_BADD (1<<17) /* PCI access with illegal addr */
#define BPCI_IFSTATUS_RTO  (1<<18) /* Retry time out */
#define BPCI_IFSTATUS_SER  (1<<19) /* System error */
#define BPCI_IFSTATUS_PER  (1<<20) /* Parity error */
#define BPCI_IFSTATUS_LCA  (1<<21) /* Local CPU abort */
#define BPCI_IFSTATUS_MEM  (1<<22) /* Memory prot. violation */
#define BPCI_IFSTATUS_ARB  (1<<23) /* Arbiter timed out */
#define BPCI_IFSTATUS_STA  (1<<27) /* Signaled target abort */
#define BPCI_IFSTATUS_TA   (1<<28) /* Target abort */
#define BPCI_IFSTATUS_MA   (1<<29) /* Master abort */
#define BPCI_IFSTATUS_PEI  (1<<30) /* Parity error as initiator */
#define BPCI_IFSTATUS_PET  (1<<31) /* Parity error as target */

#define BPCI_RESETCTL_PR (1<<0)    /* True if reset asserted */
#define BPCI_RESETCTL_RT (1<<4)    /* Release time */
#define BPCI_RESETCTL_CT (1<<8)    /* Config time */
#define BPCI_RESETCTL_PE (1<<12)   /* PCI enabled */
#define BPCI_RESETCTL_HM (1<<13)   /* PCI host mode */
#define BPCI_RESETCTL_RI (1<<14)   /* PCI reset in */

extern struct msp_pci_regs msp_pci_regs
			__attribute__((section(".register")));
extern unsigned long msp_pci_config_space
			__attribute__((section(".register")));

#endif /* !_MSP_PCI_H_ */
