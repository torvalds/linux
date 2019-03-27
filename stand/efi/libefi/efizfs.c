/*-
 * Copyright (c) 2008-2010 Rui Paulo
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

#include <sys/param.h>
#include <stand.h>

#ifdef EFI_ZFS_BOOT
#include <libzfs.h>
#endif

#include <efi.h>
#include <efilib.h>

#include "efizfs.h"

#ifdef EFI_ZFS_BOOT
static zfsinfo_list_t zfsinfo;

uint64_t pool_guid;

static EFI_HANDLE preferred;

void
efizfs_set_preferred(EFI_HANDLE h)
{
	preferred = h;
}

zfsinfo_list_t *
efizfs_get_zfsinfo_list(void)
{
	return (&zfsinfo);
}

EFI_HANDLE
efizfs_get_handle_by_guid(uint64_t guid)
{
	zfsinfo_t *zi;

	STAILQ_FOREACH(zi, &zfsinfo, zi_link) {
		if (zi->zi_pool_guid == guid) {
			return (zi->zi_handle);
		}
	}
	return (NULL);
}

bool
efizfs_get_guid_by_handle(EFI_HANDLE handle, uint64_t *guid)
{
	zfsinfo_t *zi;

	if (guid == NULL)
		return (false);
	STAILQ_FOREACH(zi, &zfsinfo, zi_link) {
		if (zi->zi_handle == handle) {
			*guid = zi->zi_pool_guid;
			return (true);
		}
	}
	return (false);
}

static void
insert_zfs(EFI_HANDLE handle, uint64_t guid)
{
        zfsinfo_t *zi;

        zi = malloc(sizeof(zfsinfo_t));
        zi->zi_handle = handle;
        zi->zi_pool_guid = guid;
        STAILQ_INSERT_TAIL(&zfsinfo, zi, zi_link);
}

void
efi_zfs_probe(void)
{
	pdinfo_list_t *hdi;
	pdinfo_t *hd, *pd = NULL;
	char devname[SPECNAMELEN + 1];
        uint64_t guid;

	hdi = efiblk_get_pdinfo_list(&efipart_hddev);
	STAILQ_INIT(&zfsinfo);

	/*
	 * Find the handle for the boot device. The boot1 did find the
	 * device with loader binary, now we need to search for the
	 * same device and if it is part of the zfs pool, we record the
	 * pool GUID for currdev setup.
	 */
	STAILQ_FOREACH(hd, hdi, pd_link) {
		STAILQ_FOREACH(pd, &hd->pd_part, pd_link) {
			snprintf(devname, sizeof(devname), "%s%dp%d:",
			    efipart_hddev.dv_name, hd->pd_unit, pd->pd_unit);
			if (zfs_probe_dev(devname, &guid) == 0) {
				insert_zfs(pd->pd_handle, guid);
				if (pd->pd_handle == preferred)
					pool_guid = guid;
			}

		}
	}
}
#endif
