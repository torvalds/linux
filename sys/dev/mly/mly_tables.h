/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Michael Smith
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
struct mly_code_lookup {
    char	*string;
    u_int32_t	code;
};

static char	*mly_describe_code(struct mly_code_lookup *table, u_int32_t code);

/********************************************************************************
 * Look up a text description of a numeric code and return a pointer to same.
 */
static char *
mly_describe_code(struct mly_code_lookup *table, u_int32_t code)
{
    int		i;

    for (i = 0; table[i].string != NULL; i++)
	if (table[i].code == code)
	    return(table[i].string);
    return(table[i+1].string);
}

#if 0
static struct mly_code_lookup mly_table_bustype[] = {
    {"SCSI",		0x00},
    {"FC-AL",		0x01},
    {"PCI",		0x03},
    {NULL,		0},
    {"unknown bus",	0}
};
#endif

static struct mly_code_lookup mly_table_controllertype[] = {
#if 0	/* not supported by this driver */
    {"DAC960E",		0x01},	/* EISA */
    {"DAC960M",		0x08},	/* MCA */
    {"DAC960PD",	0x10},	/* PCI Dual */
    {"DAC960PL",	0x11},	/* PCU low-cost */
    {"DAC960PDU",	0x12},	/* PD Ultra */
    {"DAC960PE",	0x13},	/* Peregrine low-cost */
    {"DAC960PG",	0x14},	/* Peregrine high-performance */
    {"DAC960PJ",	0x15},	/* Road Runner */
    {"DAC960PTL0",	0x16},	/* Jaguar */
    {"DAC960PR",	0x17},	/* Road Runner (again?) */
    {"DAC960PRL",	0x18},	/* Tomcat */
    {"DAC960PT",	0x19},	/* Road Runner (yet again?) */
    {"DAC1164P",	0x1a},	/* Little Apple */
    {"DAC960PTL1",	0x1b},	/* Jaguar+ */
#endif
    {"EXR2000P",	0x1c},	/* Big Apple */
    {"EXR3000P",	0x1d},	/* Fibre Apple */
    {"AcceleRAID 352",	0x1e},	/* Leopard */
    {"AcceleRAID 170",	0x1f},	/* Lynx */
    {"AcceleRAID 160",	0x20},	/* Bobcat */
    {NULL,		0},
    {"unknown adapter",	0}
};    

static struct mly_code_lookup mly_table_oemname[] = {
    {"Mylex",		MLY_OEM_MYLEX},
    {"IBM",		MLY_OEM_IBM},
    {"Hewlett-Packard",	MLY_OEM_HP},
    {"DEC/Compaq",	MLY_OEM_DEC},
    {"Siemens",		MLY_OEM_SIEMENS},
    {"Intel",		MLY_OEM_INTEL},
    {NULL,		0},
    {"unknown OEM",	0}
};

static struct mly_code_lookup mly_table_memorytype[] = {
    {"DRAM",		0x01},
    {"EDRAM",		0x02},
    {"EDO RAM",		0x03},
    {"SDRAM",		0x04},
    {NULL,		0},
    {"unknown memory",	0}
};

static struct mly_code_lookup mly_table_cputype[] = {
    {"i960CA",		0x01},
    {"i960RD",		0x02},
    {"i960RN",		0x03},
    {"i960RP",		0x04},
    {"NorthBay(?)",	0x05},
    {"StrongArm",	0x06},
    {"i960RM",		0x07},
    {NULL,		0},
    {"unknown CPU",	0}
};

/*
 * This table is directly derived from the corresponding table in the
 * Linux driver, and uses a derivative encoding for simplicity's sake.
 *
 * The first character of the string determines the format of the message.
 *
 * p  "physical device <channel>:<target> <text>"	(physical device status)
 * s  "physical device <channel>:<target> <text>"	(scsi message or error)
 *    "  sense key <key>  asc <asc>  ascq <ascq>"
 *    "  info <info>   csi <csi>"
 * l  "logical drive <unit>: <text>"			(logical device status)
 * m  "logical drive <unit>: <text>"			(logical device message)
 *
 * Messages which are typically suppressed have the first character capitalised.
 * These messages will only be printed if bootverbose is set.
 *
 * The second character in the string indicates an action to be taken as a
 * result of the event.
 *
 * r	rescan the device for possible state change
 *
 */
static struct mly_code_lookup mly_table_event[] = {
    /* physical device events (0x0000 - 0x007f) */
    {"pr online",							0x0001},
    {"pr standby",							0x0002},
    {"p  automatic rebuild started",					0x0005},
    {"p  manual rebuild started",					0x0006},
    {"pr rebuild completed",						0x0007},
    {"pr rebuild cancelled",						0x0008},
    {"pr rebuild failed for unknown reasons",				0x0009},
    {"pr rebuild failed due to new physical device",			0x000a},
    {"pr rebuild failed due to logical drive failure",			0x000b},
    {"sr offline",							0x000c},
    {"pr found",							0x000d},
    {"pr gone",								0x000e},
    {"p  unconfigured",							0x000f},
    {"p  expand capacity started",					0x0010},
    {"pr expand capacity completed",					0x0011},
    {"pr expand capacity failed",					0x0012},
    {"p  parity error",							0x0016},
    {"p  soft error",							0x0017},
    {"p  miscellaneous error",						0x0018},
    {"p  reset",							0x0019},
    {"p  active spare found",						0x001a},
    {"p  warm spare found",						0x001b},
    {"s  sense data received",						0x001c},
    {"p  initialization started",					0x001d},
    {"pr initialization completed",					0x001e},
    {"pr initialization failed",					0x001f},
    {"pr initialization cancelled",					0x0020},
    {"P  write recovery failed",					0x0021},
    {"p  scsi bus reset failed",					0x0022},
    {"p  double check condition",					0x0023},
    {"p  device cannot be accessed",					0x0024},
    {"p  gross error on scsi processor",				0x0025},
    {"p  bad tag from device",						0x0026},
    {"p  command timeout",						0x0027},
    {"pr system reset",							0x0028},
    {"p  busy status or parity error",					0x0029},
    {"pr host set device to failed state",				0x002a},
    {"pr selection timeout",						0x002b},
    {"p  scsi bus phase error",						0x002c},
    {"pr device returned unknown status",				0x002d},
    {"pr device not ready",						0x002e},
    {"p  device not found at startup",					0x002f},
    {"p  COD write operation failed",					0x0030},
    {"p  BDT write operation failed",					0x0031},
    {"p  missing at startup",						0x0039},
    {"p  start rebuild failed due to physical drive too small",		0x003a},
    /* logical device events (0x0080 - 0x00ff) */
    {"m  consistency check started",					0x0080},
    {"mr consistency check completed",					0x0081},
    {"mr consistency check cancelled",					0x0082},
    {"mr consistency check completed with errors",			0x0083},
    {"mr consistency check failed due to logical drive failure",	0x0084},
    {"mr consistency check failed due to physical device failure",	0x0085},
    {"lr offline",							0x0086},
    {"lr critical",							0x0087},
    {"lr online",							0x0088},
    {"m  automatic rebuild started",					0x0089},
    {"m  manual rebuild started",					0x008a},
    {"mr rebuild completed",						0x008b},
    {"mr rebuild cancelled",						0x008c},
    {"mr rebuild failed for unknown reasons",				0x008d},
    {"mr rebuild failed due to new physical device",			0x008e},
    {"mr rebuild failed due to logical drive failure",			0x008f},
    {"l  initialization started",					0x0090},
    {"lr initialization completed",					0x0091},
    {"lr initialization cancelled",					0x0092},
    {"lr initialization failed",					0x0093},
    {"lr found",							0x0094},
    {"lr gone",								0x0095},
    {"l  expand capacity started",					0x0096},
    {"lr expand capacity completed",					0x0097},
    {"lr expand capacity failed",					0x0098},
    {"l  bad block found",						0x0099},
    {"lr size changed",							0x009a},
    {"lr type changed",							0x009b},
    {"l  bad data block found",						0x009c},
    {"l  read of data block in bdt",					0x009e},
    {"l  write back data for disk block lost",				0x009f},
    /* enclosure management events (0x0100 - 0x017f) */
    {"e  enclosure %d fan %d failed",					0x0140},
    {"e  enclosure %d fan %d ok",					0x0141},
    {"e  enclosure %d fan %d not present",				0x0142},
    {"e  enclosure %d power supply %d failed",				0x0143},
    {"e  enclosure %d power supply %d ok",				0x0144},
    {"e  enclosure %d power supply %d not present",			0x0145},
    {"e  enclosure %d temperature sensor %d failed",			0x0146},
    {"e  enclosure %d temperature sensor %d critical",			0x0147},
    {"e  enclosure %d temperature sensor %d ok",			0x0148},
    {"e  enclosure %d temperature sensor %d not present",		0x0149},
    {"e  enclosure %d unit %d access critical",				0x014a},
    {"e  enclosure %d unit %d access ok",				0x014b},
    {"e  enclosure %d unit %d access offline",				0x014c},
    /* controller events (0x0180 - 0x01ff) */
    {"c  cache write back error",					0x0181},
    {"c  battery backup unit found",					0x0188},
    {"c  battery backup unit charge level low",				0x0189},
    {"c  battery backup unit charge level ok",				0x018a},
    {"c  installation aborted",						0x0193},
    {"c  mirror race recovery in progress",				0x0195},
    {"c  mirror race on critical drive",				0x0196},
    {"c  memory soft ecc error",					0x019e},
    {"c  memory hard ecc error",					0x019f},
    {"c  battery backup unit failed",					0x01a2},
    {NULL, 0},
    {"?  unknown event code",						0}
};

/*
 * Values here must be 16 characters or less, as they are packed into
 * the 'product' field in the SCSI inquiry data.
 */
static struct mly_code_lookup mly_table_device_state[] = {
    {"offline",		MLY_DEVICE_STATE_OFFLINE},
    {"unconfigured",	MLY_DEVICE_STATE_UNCONFIGURED},
    {"online",		MLY_DEVICE_STATE_ONLINE},
    {"critical",	MLY_DEVICE_STATE_CRITICAL},
    {"writeonly",	MLY_DEVICE_STATE_WRITEONLY},
    {"standby",		MLY_DEVICE_STATE_STANDBY},
    {"missing",		MLY_DEVICE_STATE_MISSING},
    {NULL, 0},
    {"unknown state",	0}
};

/*
 * Values here must be 8 characters or less, as they are packed into
 * the 'vendor' field in the SCSI inquiry data.
 */
static struct mly_code_lookup mly_table_device_type[] = {
    {"RAID 0",		MLY_DEVICE_TYPE_RAID0},
    {"RAID 1",		MLY_DEVICE_TYPE_RAID1},
    {"RAID 3",		MLY_DEVICE_TYPE_RAID3},		/* right asymmetric parity */
    {"RAID 5",		MLY_DEVICE_TYPE_RAID5},		/* right asymmetric parity */
    {"RAID 6",		MLY_DEVICE_TYPE_RAID6},		/* Mylex RAID 6 */
    {"RAID 7",		MLY_DEVICE_TYPE_RAID7},		/* JBOD */
    {"SPAN",		MLY_DEVICE_TYPE_NEWSPAN},	/* New Mylex SPAN */
    {"RAID 3",		MLY_DEVICE_TYPE_RAID3F},	/* fixed parity */
    {"RAID 3",		MLY_DEVICE_TYPE_RAID3L},	/* left symmetric parity */
    {"SPAN",		MLY_DEVICE_TYPE_SPAN},		/* current spanning implementation */
    {"RAID 5",		MLY_DEVICE_TYPE_RAID5L},	/* left symmetric parity */
    {"RAID E",		MLY_DEVICE_TYPE_RAIDE},		/* concatenation */
    {"PHYSICAL",	MLY_DEVICE_TYPE_PHYSICAL},	/* physical device */
    {NULL, 0},
    {"UNKNOWN",		0}
};

#if 0
static struct mly_code_lookup mly_table_stripe_size[] = {
    {"NONE",		MLY_STRIPE_ZERO},
    {"512B",		MLY_STRIPE_512b},
    {"1k",		MLY_STRIPE_1k},
    {"2k",		MLY_STRIPE_2k},
    {"4k",		MLY_STRIPE_4k},
    {"8k",		MLY_STRIPE_8k},
    {"16k",		MLY_STRIPE_16k},
    {"32k",		MLY_STRIPE_32k},
    {"64k",		MLY_STRIPE_64k},
    {"128k",		MLY_STRIPE_128k},
    {"256k",		MLY_STRIPE_256k},
    {"512k",		MLY_STRIPE_512k},
    {"1M",		MLY_STRIPE_1m},
    {NULL, 0},
    {"unknown",		0}
};

static struct mly_code_lookup mly_table_cacheline_size[] = {
    {"NONE",		MLY_CACHELINE_ZERO},
    {"512B",		MLY_CACHELINE_512b},
    {"1k",		MLY_CACHELINE_1k},
    {"2k",		MLY_CACHELINE_2k},
    {"4k",		MLY_CACHELINE_4k},
    {"8k",		MLY_CACHELINE_8k},
    {"16k",		MLY_CACHELINE_16k},
    {"32k",		MLY_CACHELINE_32k},
    {"64k",		MLY_CACHELINE_64k},
    {NULL, 0},
    {"unknown",		0}
};
#endif
