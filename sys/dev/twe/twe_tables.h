/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2003 Paul Saab
 * Copyright (c) 2003 Vinod Kashyap
 * Copyright (c) 2000 BSDi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD$
 */

/*
 * Lookup table for code-to-text translations.
 */
struct twe_code_lookup {
    char	*string;
    u_int32_t	code;
};

extern char	*twe_describe_code(struct twe_code_lookup *table, u_int32_t code);

#ifndef TWE_DEFINE_TABLES
extern struct twe_code_lookup twe_table_status[];
extern struct twe_code_lookup twe_table_unitstate[];
extern struct twe_code_lookup twe_table_unittype[];
extern struct twe_code_lookup twe_table_aen[];
extern struct twe_code_lookup twe_table_opcode[];
#else /* TWE_DEFINE_TABLES */

struct twe_code_lookup twe_table_status[] = {
    /* success */
    {"successful completion",					0x00},
    /* info */
    {"command in progress",					0x42},
    {"retrying interface CRC error from UDMA command",		0x6c},
    /* warning */
    {"redundant/inconsequential request ignored",		0x81},
    {"failed to write zeroes to LBA 0",				0x8e},
    {"failed to profile TwinStor zones",			0x8f},
    /* fatal */
    {"aborted due to system command or reconfiguration",	0xc1},
    {"aborted",							0xc4},
    {"access error",						0xc5},
    {"access violation",					0xc6},
    {"device failure",						0xc7},	/* high byte may be port number */
    {"controller error",					0xc8},
    {"timed out",						0xc9},
    {"invalid unit number",					0xcb},
    {"unit not available",					0xcf},
    {"undefined opcode",					0xd2},
    {"request incompatible with unit",				0xdb},
    {"invalid request",						0xdc},
    {"firmware error, reset requested",				0xff},
    {NULL,	0},
    {"unknown status",	0}
};

struct twe_code_lookup twe_table_unitstate[] = {
    {"Normal",		TWE_PARAM_UNITSTATUS_Normal},
    {"Initialising",	TWE_PARAM_UNITSTATUS_Initialising},
    {"Degraded",	TWE_PARAM_UNITSTATUS_Degraded},
    {"Rebuilding",	TWE_PARAM_UNITSTATUS_Rebuilding},
    {"Verifying",	TWE_PARAM_UNITSTATUS_Verifying},
    {"Corrupt",		TWE_PARAM_UNITSTATUS_Corrupt},
    {"Missing",		TWE_PARAM_UNITSTATUS_Missing},
    {NULL, 0},
    {"unknown state",	0}
};

struct twe_code_lookup twe_table_unittype[] = {
    {"RAID0",		TWE_UD_CONFIG_RAID0},
    {"RAID1",		TWE_UD_CONFIG_RAID1},
    {"TwinStor",	TWE_UD_CONFIG_TwinStor},
    {"RAID5",		TWE_UD_CONFIG_RAID5},
    {"RAID10",		TWE_UD_CONFIG_RAID10},
    {"CBOD",		TWE_UD_CONFIG_CBOD},
    {"SPARE",		TWE_UD_CONFIG_SPARE},
    {"SUBUNIT",		TWE_UD_CONFIG_SUBUNIT},
    {"JBOD",		TWE_UD_CONFIG_JBOD},
    {NULL, 0},
    {"unknown type",	0}
};

struct twe_code_lookup twe_table_aen[] = {
    {"q queue empty",			0x00},
    {"q soft reset",			0x01},
    {"c degraded unit",			0x02},
    {"a controller error",		0x03},
    {"c rebuild fail",			0x04},
    {"c rebuild done",			0x05},
    {"c incomplete unit",		0x06},
    {"c initialisation done",		0x07},
    {"c unclean shutdown detected",	0x08},
    {"c drive timeout",			0x09},
    {"c drive error",			0x0a},
    {"c rebuild started",		0x0b},
    {"c init started",			0x0c},
    {"c logical unit deleted",		0x0d},
    {"p SMART threshold exceeded",	0x0f},
    {"p ATA UDMA downgrade",		0x21},
    {"p ATA UDMA upgrade",		0x22},
    {"p sector repair occurred",	0x23},
    {"a SBUF integrity check failure",	0x24},
    {"p lost cached write",		0x25},
    {"p drive ECC error detected",	0x26},
    {"p DCB checksum error",		0x27},
    {"p DCB unsupported version",	0x28},
    {"c verify started",		0x29},
    {"c verify failed",			0x2a},
    {"c verify complete",		0x2b},
    {"p overwrote bad sector during rebuild",	0x2c},
    {"p encountered bad sector during rebuild",	0x2d},
    {"a replacement drive too small", 0x2e},
    {"c array not previously initialized", 0x2f},
    {"p drive not supported", 0x30},
    {"a aen queue full",		0xff},
    {NULL, 0},
    {"x unknown AEN",		0}
};

struct twe_code_lookup twe_table_opcode[] = {
    {"NOP",			0x00},
    {"INIT_CONNECTION",		0x01},
    {"READ",			0x02},
    {"WRITE",			0x03},
    {"READVERIFY",		0x04},
    {"VERIFY",			0x05},
    {"ZEROUNIT",		0x08},
    {"REPLACEUNIT",		0x09},
    {"HOTSWAP",			0x0a},
    {"SETATAFEATURE",		0x0c},
    {"FLUSH",			0x0e},
    {"ABORT",			0x0f},
    {"CHECKSTATUS",		0x10},
    {"GET_PARAM",		0x12},
    {"SET_PARAM",		0x13},
    {"CREATEUNIT",		0x14},
    {"DELETEUNIT",		0x15},
    {"REBUILDUNIT",		0x17},
    {"SECTOR_INFO",		0x1a},
    {"AEN_LISTEN",		0x1c},
    {"CMD_PACKET",		0x1d},
    {NULL, 0},
    {"unknown opcode",		0}
};    
    
#endif
