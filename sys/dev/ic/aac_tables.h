/*	$OpenBSD: aac_tables.h,v 1.6 2022/01/09 05:42:37 jsg Exp $	*/

/*-
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2000 BSDi
 * Copyright (c) 2000 Niklas Hallqvist
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
 *	$FreeBSD: /c/ncvs/src/sys/dev/aac/aac_tables.h,v 1.1 2000/09/13 03:20:34 msmith Exp $
 */

/*
 * Status codes for block read/write commands, etc.
 *
 * XXX many of these would not normally be returned, as they are
 * relevant only to FSA operations.
 */
const struct aac_code_lookup aac_command_status_table[] = {
	{ "OK",					0 },
	{ "operation not permitted",		1 },
	{ "not found",				2 },
	{ "I/O error",				5 },
	{ "device not configured",		6 },
	{ "too big",				7 },
	{ "permission denied",			13 },
	{ "file exists",			17 },
	{ "cross-device link",			18 },
	{ "operation not supported by device",	19 },
	{ "not a directory",			20 },
	{ "is a directory",			21 },
	{ "invalid argument",			22 },
	{ "file too large",			27 },
	{ "no space on device",			28 },
	{ "readonly filesystem",		30 },
	{ "too many links",			31 },
	{ "operation would block",		35 },
	{ "file name too long",			63 },
	{ "directory not empty",		66 },
	{ "quota exceeded",			69 },
	{ "stale file handle",			70 },
	{ "too many levels of remote in path",	71 },
	{ "bad file handle",			10001 },
	{ "not sync",				10002 },
	{ "bad cookie",				10003 },
	{ "operation not supported",		10004 },
	{ "too small",				10005 },
	{ "server fault",			10006 },
	{ "bad type",				10007 },
	{ "jukebox",				10008 },
	{ "not mounted",			10009 },
	{ "in maintenance mode",		10010 },
	{ "stale ACL",				10011 },
	{ NULL, 				0 },
	{ "unknown command status",		0 }
};

#define AAC_COMMAND_STATUS(x)	aac_describe_code(aac_command_status_table, x)

static struct aac_code_lookup aac_cpu_variant[] = {
	{ "i960JX",			CPUI960_JX },
	{ "i960CX",			CPUI960_CX },
	{ "i960HX",			CPUI960_HX },
	{ "i960RX",			CPUI960_RX },
	{ "StrongARM SA110",		CPUARM_SA110 },
	{ "MPC824x",			CPUMPC_824x },
	{ "Unknown StrongARM",		CPUARM_xxx },
	{ "Unknown PowerPC",		CPUPPC_xxx },
	{ "Intel GC80302 IOP",		CPUI960_302},
	{ "XScale 80321",		CPU_XSCALE_80321 },
	{ "MIPS 4KC",			CPU_MIPS_4KC },
	{ "MIPS 5KC",			CPU_MIPS_5KC },
	{ NULL, 			0 },
	{ "Unknown processor",		0 }
};

static struct aac_code_lookup aac_battery_platform[] = {
	{ "required battery present",		PLATFORM_BAT_REQ_PRESENT },
	{ "REQUIRED BATTERY NOT PRESENT",	PLATFORM_BAT_REQ_NOTPRESENT },
	{ "optional battery present",		PLATFORM_BAT_OPT_PRESENT },
	{ "optional battery not installed",	PLATFORM_BAT_OPT_NOTPRESENT },
	{ "no battery support",			PLATFORM_BAT_NOT_SUPPORTED },
	{ NULL,					0 },
	{ "unknown battery platform",		0 }
};

#if 0
static struct aac_code_lookup aac_container_types[] = {
	{ "Volume",		CT_VOLUME },
	{ "RAID 1 (Mirror)",	CT_MIRROR },
	{ "RAID 0 (Stripe)",	CT_STRIPE },
	{ "RAID 5",		CT_RAID5 },
	{ "SSRW",		CT_SSRW },
	{ "SSRO",		CT_SSRO },
	{ "Morph",		CT_MORPH },
	{ "Passthrough",	CT_PASSTHRU },
	{ "RAID 4",		CT_RAID4 },
	{ "RAID 10",		CT_RAID10 },
	{ "RAID 00",		CT_RAID00 },
	{ "Volume of Mirrors",	CT_VOLUME_OF_MIRRORS },
	{ "Pseudo RAID 3",	CT_PSEUDO_RAID3 },
	{ NULL,			0 },
	{ "unknown",		0 }
};
#endif
