/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Nathan Whitehorn
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
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/sysctl.h>
#include <string.h>

#include "partedit.h"

/* EFI partition size in bytes */
#define	EFI_BOOTPART_SIZE	(260 * 1024 * 1024)

static const char *
x86_bootmethod(void)
{
	static char fw[255] = "";
	size_t len = sizeof(fw);
	int error;
	
	if (strlen(fw) == 0) {
		error = sysctlbyname("machdep.bootmethod", fw, &len, NULL, -1);
		if (error != 0)
			return ("BIOS");
	}

	return (fw);
}

const char *
default_scheme(void)
{
	if (strcmp(x86_bootmethod(), "UEFI") == 0)
		return ("GPT");
	else
		return ("MBR");
}

int
is_scheme_bootable(const char *part_type)
{

	if (strcmp(part_type, "GPT") == 0)
		return (1);
	if (strcmp(x86_bootmethod(), "BIOS") == 0) {
		if (strcmp(part_type, "BSD") == 0)
			return (1);
		if (strcmp(part_type, "MBR") == 0)
			return (1);
	}

	return (0);
}

int
is_fs_bootable(const char *part_type, const char *fs)
{

	if (strcmp(fs, "freebsd-ufs") == 0)
		return (1);

	if (strcmp(fs, "freebsd-zfs") == 0 &&
	    strcmp(part_type, "GPT") == 0 &&
	    strcmp(x86_bootmethod(), "BIOS") == 0)
		return (1);

	return (0);
}

size_t
bootpart_size(const char *scheme)
{

	/* No partcode except for GPT */
	if (strcmp(scheme, "GPT") != 0)
		return (0);

	if (strcmp(x86_bootmethod(), "BIOS") == 0)
		return (512*1024);
	else 
		return (EFI_BOOTPART_SIZE);

	return (0);
}

const char *
bootpart_type(const char *scheme, const char **mountpoint)
{

	if (strcmp(x86_bootmethod(), "UEFI") == 0)
		return ("efi");

	return ("freebsd-boot");
}

const char *
bootcode_path(const char *part_type)
{

	if (strcmp(x86_bootmethod(), "UEFI") == 0)
		return (NULL);

	if (strcmp(part_type, "GPT") == 0)
		return ("/boot/pmbr");
	if (strcmp(part_type, "MBR") == 0)
		return ("/boot/mbr");
	if (strcmp(part_type, "BSD") == 0)
		return ("/boot/boot");

	return (NULL);
}
	
const char *
partcode_path(const char *part_type, const char *fs_type)
{

	if (strcmp(part_type, "GPT") == 0 && strcmp(x86_bootmethod(), "UEFI") != 0) {
		if (strcmp(fs_type, "zfs") == 0)
			return ("/boot/gptzfsboot");
		else
			return ("/boot/gptboot");
	}
	
	/* No partcode except for non-UEFI GPT */
	return (NULL);
}

