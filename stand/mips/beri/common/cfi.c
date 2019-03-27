/*-
 * Copyright (c) 2013 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

#include "stand.h"
#include "mips.h"
#include "cfi.h"

/*
 * Memory-mapped Intel StrataFlash mini-driver.  Very mini.  Nothing fancy --
 * and few seatbelts.
 *
 * XXXRW: Should we be making some effort to reset isf to a known-good state
 * before starting, in case there was a soft reset mid-transaction.
 *
 * XXXRW: Would be nice to support multiple devices and also handle SD cards
 * here ... and probably not too hard.
 */
extern void	*__cheri_flash_bootfs_vaddr__;
extern void	*__cheri_flash_bootfs_len__;

#define	CHERI_BOOTFS_BASE	((uintptr_t)&__cheri_flash_bootfs_vaddr__)
#define	CHERI_BOOTFS_LENGTH	((uintptr_t)&__cheri_flash_bootfs_len__)

int
cfi_read(void *buf, unsigned lba, unsigned nblk)
{

	if ((lba << 9) + (nblk << 9) > CHERI_BOOTFS_LENGTH)
		return (-1);
	memcpy(buf, (void *)(CHERI_BOOTFS_BASE + (lba << 9)), nblk << 9);
	return (0);
}

uint64_t
cfi_get_mediasize(void)
{

	return (CHERI_BOOTFS_LENGTH);
}

uint64_t
cfi_get_sectorsize(void)
{

	return (512);	/* Always a good sector size. */
}
