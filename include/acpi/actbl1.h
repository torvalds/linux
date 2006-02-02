/******************************************************************************
 *
 * Name: actbl1.h - ACPI 1.0 tables
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2006, R. Byron Moore
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
 */

#ifndef __ACTBL1_H__
#define __ACTBL1_H__

#pragma pack(1)

/*
 * ACPI 1.0 Root System Description Table (RSDT)
 */
struct rsdt_descriptor_rev1 {
	ACPI_TABLE_HEADER_DEF	/* ACPI common table header */
	u32 table_offset_entry[1];	/* Array of pointers to ACPI tables */
};

/*
 * ACPI 1.0 Firmware ACPI Control Structure (FACS)
 */
struct facs_descriptor_rev1 {
	char signature[4];	/* ASCII table signature */
	u32 length;		/* Length of structure in bytes */
	u32 hardware_signature;	/* Hardware configuration signature */
	u32 firmware_waking_vector;	/* ACPI OS waking vector */
	u32 global_lock;	/* Global Lock */

	/* Flags (32 bits) */

	u8 S4bios_f:1;		/* 00:    S4BIOS support is present */
	 u8:7;			/* 01-07: Reserved, must be zero */
	u8 reserved1[3];	/* 08-31: Reserved, must be zero */

	u8 reserved2[40];	/* Reserved, must be zero */
};

/*
 * ACPI 1.0 Fixed ACPI Description Table (FADT)
 */
struct fadt_descriptor_rev1 {
	ACPI_TABLE_HEADER_DEF	/* ACPI common table header */
	u32 firmware_ctrl;	/* Physical address of FACS */
	u32 dsdt;		/* Physical address of DSDT */
	u8 model;		/* System Interrupt Model */
	u8 reserved1;		/* Reserved, must be zero */
	u16 sci_int;		/* System vector of SCI interrupt */
	u32 smi_cmd;		/* Port address of SMI command port */
	u8 acpi_enable;		/* Value to write to smi_cmd to enable ACPI */
	u8 acpi_disable;	/* Value to write to smi_cmd to disable ACPI */
	u8 S4bios_req;		/* Value to write to SMI CMD to enter S4BIOS state */
	u8 reserved2;		/* Reserved, must be zero */
	u32 pm1a_evt_blk;	/* Port address of Power Mgt 1a acpi_event Reg Blk */
	u32 pm1b_evt_blk;	/* Port address of Power Mgt 1b acpi_event Reg Blk */
	u32 pm1a_cnt_blk;	/* Port address of Power Mgt 1a Control Reg Blk */
	u32 pm1b_cnt_blk;	/* Port address of Power Mgt 1b Control Reg Blk */
	u32 pm2_cnt_blk;	/* Port address of Power Mgt 2 Control Reg Blk */
	u32 pm_tmr_blk;		/* Port address of Power Mgt Timer Ctrl Reg Blk */
	u32 gpe0_blk;		/* Port addr of General Purpose acpi_event 0 Reg Blk */
	u32 gpe1_blk;		/* Port addr of General Purpose acpi_event 1 Reg Blk */
	u8 pm1_evt_len;		/* Byte length of ports at pm1_x_evt_blk */
	u8 pm1_cnt_len;		/* Byte length of ports at pm1_x_cnt_blk */
	u8 pm2_cnt_len;		/* Byte Length of ports at pm2_cnt_blk */
	u8 pm_tm_len;		/* Byte Length of ports at pm_tm_blk */
	u8 gpe0_blk_len;	/* Byte Length of ports at gpe0_blk */
	u8 gpe1_blk_len;	/* Byte Length of ports at gpe1_blk */
	u8 gpe1_base;		/* Offset in gpe model where gpe1 events start */
	u8 reserved3;		/* Reserved, must be zero */
	u16 plvl2_lat;		/* Worst case HW latency to enter/exit C2 state */
	u16 plvl3_lat;		/* Worst case HW latency to enter/exit C3 state */
	u16 flush_size;		/* Size of area read to flush caches */
	u16 flush_stride;	/* Stride used in flushing caches */
	u8 duty_offset;		/* Bit location of duty cycle field in p_cnt reg */
	u8 duty_width;		/* Bit width of duty cycle field in p_cnt reg */
	u8 day_alrm;		/* Index to day-of-month alarm in RTC CMOS RAM */
	u8 mon_alrm;		/* Index to month-of-year alarm in RTC CMOS RAM */
	u8 century;		/* Index to century in RTC CMOS RAM */
	u8 reserved4[3];	/* Reserved, must be zero */

	/* Flags (32 bits) */

	u8 wb_invd:1;		/* 00:    The wbinvd instruction works properly */
	u8 wb_invd_flush:1;	/* 01:    The wbinvd flushes but does not invalidate */
	u8 proc_c1:1;		/* 02:    All processors support C1 state */
	u8 plvl2_up:1;		/* 03:    C2 state works on MP system */
	u8 pwr_button:1;	/* 04:    Power button is handled as a generic feature */
	u8 sleep_button:1;	/* 05:    Sleep button is handled as a generic feature, or not present */
	u8 fixed_rTC:1;		/* 06:    RTC wakeup stat not in fixed register space */
	u8 rtcs4:1;		/* 07:    RTC wakeup stat not possible from S4 */
	u8 tmr_val_ext:1;	/* 08:    tmr_val width is 32 bits (0 = 24 bits) */
	 u8:7;			/* 09-15: Reserved, must be zero */
	u8 reserved5[2];	/* 16-31: Reserved, must be zero */
};

#pragma pack()

#endif				/* __ACTBL1_H__ */
