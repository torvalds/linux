/*
 * Copyright (C) 2016 Cavium Inc.
 * All rights reserved.
 *
 * Developed by Semihalf.
 * Based on work by Nathan Whitehorn.
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
#include <string.h>

#include "partedit.h"

/* EFI partition size in bytes */
#define	EFI_BOOTPART_SIZE	(260 * 1024 * 1024)

const char *
default_scheme(void)
{

	return ("GPT");
}

int
is_scheme_bootable(const char *part_type)
{

	if (strcmp(part_type, "GPT") == 0)
		return (1);

	return (0);
}

int
is_fs_bootable(const char *part_type, const char *fs)
{

	if (strcmp(fs, "freebsd-ufs") == 0)
		return (1);

	return (0);
}

size_t
bootpart_size(const char *scheme)
{

	/* We only support GPT with EFI */
	if (strcmp(scheme, "GPT") != 0)
		return (0);

	return (EFI_BOOTPART_SIZE);
}

const char *
bootpart_type(const char *scheme, const char **mountpoint)
{

	/* Only EFI is supported as boot partition */
	return ("efi");
}

const char *
bootcode_path(const char *part_type)
{

	return (NULL);
}

const char *
partcode_path(const char *part_type, const char *fs_type)
{

	/* No boot partition data for ARM64 */
	return (NULL);
}

