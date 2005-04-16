/******************************************************************************
 *
 * Name: actbl71.h - IA-64 Extensions to the ACPI Spec Rev. 0.71
 *                   This file includes tables specific to this
 *                   specification revision.
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 - 2003, R. Byron Moore
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __ACTBL71_H__
#define __ACTBL71_H__


/* 0.71 FADT address_space data item bitmasks defines */
/* If the associated bit is zero then it is in memory space else in io space */

#define SMI_CMD_ADDRESS_SPACE       0x01
#define PM1_BLK_ADDRESS_SPACE       0x02
#define PM2_CNT_BLK_ADDRESS_SPACE   0x04
#define PM_TMR_BLK_ADDRESS_SPACE    0x08
#define GPE0_BLK_ADDRESS_SPACE      0x10
#define GPE1_BLK_ADDRESS_SPACE      0x20

/* Only for clarity in declarations */

typedef u64                         IO_ADDRESS;


#pragma pack(1)
struct  /* Root System Descriptor Pointer */
{
	NATIVE_CHAR             signature [8];          /* contains "RSD PTR " */
	u8                              checksum;               /* to make sum of struct == 0 */
	NATIVE_CHAR             oem_id [6];             /* OEM identification */
	u8                              reserved;               /* Must be 0 for 1.0, 2 for 2.0 */
	u64                             rsdt_physical_address;  /* 64-bit physical address of RSDT */
};


/*****************************************/
/* IA64 Extensions to ACPI Spec Rev 0.71 */
/* for the Root System Description Table */
/*****************************************/
struct
{
	struct acpi_table_header    header;                 /* Table header */
	u32                         reserved_pad;           /* IA64 alignment, must be 0 */
	u64                         table_offset_entry [1]; /* Array of pointers to other */
			   /* tables' headers */
};


/*******************************************/
/* IA64 Extensions to ACPI Spec Rev 0.71   */
/* for the Firmware ACPI Control Structure */
/*******************************************/
struct
{
	NATIVE_CHAR         signature[4];         /* signature "FACS" */
	u32                         length;               /* length of structure, in bytes */
	u32                         hardware_signature;   /* hardware configuration signature */
	u32                         reserved4;            /* must be 0 */
	u64                         firmware_waking_vector; /* ACPI OS waking vector */
	u64                         global_lock;          /* Global Lock */
	u32                         S4bios_f      : 1;    /* Indicates if S4BIOS support is present */
	u32                         reserved1     : 31;   /* must be 0 */
	u8                          reserved3 [28];       /* reserved - must be zero */
};


/******************************************/
/* IA64 Extensions to ACPI Spec Rev 0.71  */
/* for the Fixed ACPI Description Table   */
/******************************************/
struct
{
	struct acpi_table_header    header;             /* table header */
	u32                         reserved_pad;       /* IA64 alignment, must be 0 */
	u64                         firmware_ctrl;      /* 64-bit Physical address of FACS */
	u64                         dsdt;               /* 64-bit Physical address of DSDT */
	u8                          model;              /* System Interrupt Model */
	u8                          address_space;      /* Address Space Bitmask */
	u16                         sci_int;            /* System vector of SCI interrupt */
	u8                          acpi_enable;        /* value to write to smi_cmd to enable ACPI */
	u8                          acpi_disable;       /* value to write to smi_cmd to disable ACPI */
	u8                          S4bios_req;         /* Value to write to SMI CMD to enter S4BIOS state */
	u8                          reserved2;          /* reserved - must be zero */
	u64                         smi_cmd;            /* Port address of SMI command port */
	u64                         pm1a_evt_blk;       /* Port address of Power Mgt 1a acpi_event Reg Blk */
	u64                         pm1b_evt_blk;       /* Port address of Power Mgt 1b acpi_event Reg Blk */
	u64                         pm1a_cnt_blk;       /* Port address of Power Mgt 1a Control Reg Blk */
	u64                         pm1b_cnt_blk;       /* Port address of Power Mgt 1b Control Reg Blk */
	u64                         pm2_cnt_blk;        /* Port address of Power Mgt 2 Control Reg Blk */
	u64                         pm_tmr_blk;         /* Port address of Power Mgt Timer Ctrl Reg Blk */
	u64                         gpe0_blk;           /* Port addr of General Purpose acpi_event 0 Reg Blk */
	u64                         gpe1_blk;           /* Port addr of General Purpose acpi_event 1 Reg Blk */
	u8                          pm1_evt_len;        /* Byte length of ports at pm1_x_evt_blk */
	u8                          pm1_cnt_len;        /* Byte length of ports at pm1_x_cnt_blk */
	u8                          pm2_cnt_len;        /* Byte Length of ports at pm2_cnt_blk */
	u8                          pm_tm_len;          /* Byte Length of ports at pm_tm_blk */
	u8                          gpe0_blk_len;       /* Byte Length of ports at gpe0_blk */
	u8                          gpe1_blk_len;       /* Byte Length of ports at gpe1_blk */
	u8                          gpe1_base;          /* offset in gpe model where gpe1 events start */
	u8                          reserved3;          /* reserved */
	u16                         plvl2_lat;          /* worst case HW latency to enter/exit C2 state */
	u16                         plvl3_lat;          /* worst case HW latency to enter/exit C3 state */
	u8                          day_alrm;           /* index to day-of-month alarm in RTC CMOS RAM */
	u8                          mon_alrm;           /* index to month-of-year alarm in RTC CMOS RAM */
	u8                          century;            /* index to century in RTC CMOS RAM */
	u8                          reserved4;          /* reserved */
	u32                         flush_cash  : 1;    /* PAL_FLUSH_CACHE is correctly supported */
	u32                         reserved5   : 1;    /* reserved - must be zero */
	u32                         proc_c1     : 1;    /* all processors support C1 state */
	u32                         plvl2_up    : 1;    /* C2 state works on MP system */
	u32                         pwr_button  : 1;    /* Power button is handled as a generic feature */
	u32                         sleep_button : 1;   /* Sleep button is handled as a generic feature, or not present */
	u32                         fixed_rTC   : 1;    /* RTC wakeup stat not in fixed register space */
	u32                         rtcs4       : 1;    /* RTC wakeup stat not possible from S4 */
	u32                         tmr_val_ext : 1;    /* tmr_val is 32 bits */
	u32                         dock_cap    : 1;    /* Supports Docking */
	u32                         reserved6   : 22;    /* reserved - must be zero */
};

#pragma pack()

#endif /* __ACTBL71_H__ */

