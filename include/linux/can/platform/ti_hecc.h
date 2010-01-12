/*
 * TI HECC (High End CAN Controller) driver platform header
 *
 * Copyright (C) 2009 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed as is WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/**
 * struct hecc_platform_data - HECC Platform Data
 *
 * @scc_hecc_offset:	mostly 0 - should really never change
 * @scc_ram_offset:	SCC RAM offset
 * @hecc_ram_offset:	HECC RAM offset
 * @mbx_offset:		Mailbox RAM offset
 * @int_line:		Interrupt line to use - 0 or 1
 * @version:		version for future use
 *
 * Platform data structure to get all platform specific settings.
 * this structure also accounts the fact that the IP may have different
 * RAM and mailbox offsets for different SOC's
 */
struct ti_hecc_platform_data {
	u32 scc_hecc_offset;
	u32 scc_ram_offset;
	u32 hecc_ram_offset;
	u32 mbx_offset;
	u32 int_line;
	u32 version;
};


