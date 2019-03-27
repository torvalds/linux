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

#if 0
/*
 * Status codes for block read/write commands, etc.
 *
 * XXX many of these would not normally be returned, as they are
 * relevant only to FSA operations.
 */
static const struct aac_code_lookup aac_command_status_table[] = {
	{"OK",					ST_OK},
	{"operation not permitted",		ST_PERM},
	{"not found",				ST_NOENT},
	{"I/O error",				ST_IO},
	{"device not configured",		ST_NXIO},
	{"too big",				ST_E2BIG},
	{"permission denied",			ST_ACCES},
	{"file exists",				ST_EXIST},
	{"cross-device link",			ST_XDEV},
	{"operation not supported by device",	ST_NODEV},
	{"not a directory",			ST_NOTDIR},
	{"is a directory",			ST_ISDIR},
	{"invalid argument",			ST_INVAL},
	{"file too large",			ST_FBIG},
	{"no space on device",			ST_NOSPC},
	{"readonly filesystem",			ST_ROFS},
	{"too many links",			ST_MLINK},
	{"operation would block",		ST_WOULDBLOCK},
	{"file name too long",			ST_NAMETOOLONG},
	{"directory not empty",			ST_NOTEMPTY},
	{"quota exceeded",			ST_DQUOT},
	{"stale file handle",			ST_STALE},
	{"too many levels of remote in path",	ST_REMOTE},
	{"device busy (spinning up)",		ST_NOT_READY},
	{"bad file handle",			ST_BADHANDLE},
	{"not sync",				ST_NOT_SYNC},
	{"bad cookie",				ST_BAD_COOKIE},
	{"operation not supported",		ST_NOTSUPP},
	{"too small",				ST_TOOSMALL},
	{"server fault",			ST_SERVERFAULT},
	{"bad type",				ST_BADTYPE},
	{"jukebox",				ST_JUKEBOX},
	{"not mounted",				ST_NOTMOUNTED},
	{"in maintenance mode",			ST_MAINTMODE},
	{"stale ACL",				ST_STALEACL},
	{"bus reset - command aborted",		ST_BUS_RESET},
	{NULL, 					0},
	{"unknown command status",		0}
};

#define AAC_COMMAND_STATUS(x)	aac_describe_code(aac_command_status_table, x)
#endif

static const struct aac_code_lookup aac_cpu_variant[] = {
	{"i960JX",		CPUI960_JX},
	{"i960CX",		CPUI960_CX},
	{"i960HX",		CPUI960_HX},
	{"i960RX",		CPUI960_RX},
	{"i960 80303",		CPUI960_80303},
	{"StrongARM SA110",	CPUARM_SA110},
	{"PPC603e",		CPUPPC_603e},
	{"XScale 80321",	CPU_XSCALE_80321},
	{"MIPS 4KC",		CPU_MIPS_4KC},
	{"MIPS 5KC",		CPU_MIPS_5KC},
	{"Unknown StrongARM",	CPUARM_xxx},
	{"Unknown PowerPC",	CPUPPC_xxx},
	{NULL, 0},
	{"Unknown processor",	0}
};

static const struct aac_code_lookup aac_battery_platform[] = {
	{"required battery present",		PLATFORM_BAT_REQ_PRESENT},
	{"REQUIRED BATTERY NOT PRESENT",	PLATFORM_BAT_REQ_NOTPRESENT},
	{"optional battery present",		PLATFORM_BAT_OPT_PRESENT},
	{"optional battery not installed",	PLATFORM_BAT_OPT_NOTPRESENT},
	{"no battery support",			PLATFORM_BAT_NOT_SUPPORTED},
	{NULL, 0},
	{"unknown battery platform",		0}
};

static const struct aac_code_lookup aac_container_types[] = {
	{"Volume",		CT_VOLUME},
	{"RAID 1 (Mirror)",	CT_MIRROR},
	{"RAID 0 (Stripe)",	CT_STRIPE},
	{"RAID 5",		CT_RAID5},
	{"SSRW",		CT_SSRW},
	{"SSRO",		CT_SSRO},
	{"Morph",		CT_MORPH},
	{"Passthrough",		CT_PASSTHRU},
	{"RAID 4",		CT_RAID4},
	{"RAID 0/1",		CT_RAID10},
	{"RAID 0/0",		CT_RAID00},
	{"Volume of Mirrors",	CT_VOLUME_OF_MIRRORS},
	{"Pseudo RAID 3",	CT_PSEUDO_RAID3},
	{"RAID 0/5",		CT_RAID50},
	{"RAID 5D",		CT_RAID5D},
	{"RAID 0/5D",		CT_RAID5D0},
	{"RAID 1E",		CT_RAID1E},
	{"RAID 6",		CT_RAID6},
	{"RAID 0/6",		CT_RAID60},
	{NULL, 0},
	{"unknown",		0}
};
