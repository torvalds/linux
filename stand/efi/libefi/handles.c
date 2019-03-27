/*-
 * Copyright (c) 2006 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <efi.h>
#include <efilib.h>

struct entry {
	EFI_HANDLE handle;
	EFI_HANDLE alias;
	struct devsw *dev;
	int unit;
	uint64_t extra;
};

struct entry *entry;
int nentries;

int
efi_register_handles(struct devsw *sw, EFI_HANDLE *handles,
    EFI_HANDLE *aliases, int count)
{
	size_t sz;
	int idx, unit;

	idx = nentries;
	nentries += count;
	sz = nentries * sizeof(struct entry);
	entry = (entry == NULL) ? malloc(sz) : realloc(entry, sz);
	for (unit = 0; idx < nentries; idx++, unit++) {
		entry[idx].handle = handles[unit];
		if (aliases != NULL)
			entry[idx].alias = aliases[unit];
		else
			entry[idx].alias = NULL;
		entry[idx].dev = sw;
		entry[idx].unit = unit;
	}
	return (0);
}

EFI_HANDLE
efi_find_handle(struct devsw *dev, int unit)
{
	int idx;

	for (idx = 0; idx < nentries; idx++) {
		if (entry[idx].dev != dev)
			continue;
		if (entry[idx].unit != unit)
			continue;
		return (entry[idx].handle);
	}
	return (NULL);
}

int
efi_handle_lookup(EFI_HANDLE h, struct devsw **dev, int *unit, uint64_t *extra)
{
	int idx;

	for (idx = 0; idx < nentries; idx++) {
		if (entry[idx].handle != h && entry[idx].alias != h)
			continue;
		if (dev != NULL)
			*dev = entry[idx].dev;
		if (unit != NULL)
			*unit = entry[idx].unit;
		if (extra != NULL)
			*extra = entry[idx].extra;
		return (0);
	}
	return (ENOENT);
}

int
efi_handle_update_dev(EFI_HANDLE h, struct devsw *dev, int unit,
    uint64_t guid)
{
	int idx;

	for (idx = 0; idx < nentries; idx++) {
		if (entry[idx].handle != h)
			continue;
		entry[idx].dev = dev;
		entry[idx].unit = unit;
		entry[idx].alias = NULL;
		entry[idx].extra = guid;
		return (0);
	}

	return (ENOENT);
}
