/*-
 * Copyright (c) 2010 Marcel Moolenaar
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/disk.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/queue.h>
#include <stddef.h>
#include <stdarg.h>

#include <bootstrap.h>

#include <efi.h>
#include <efilib.h>
#include <efiprot.h>
#include <efichar.h>
#include <disk.h>

static EFI_GUID blkio_guid = BLOCK_IO_PROTOCOL;

static int efipart_initfd(void);
static int efipart_initcd(void);
static int efipart_inithd(void);

static int efipart_strategy(void *, int, daddr_t, size_t, char *, size_t *);
static int efipart_realstrategy(void *, int, daddr_t, size_t, char *, size_t *);

static int efipart_open(struct open_file *, ...);
static int efipart_close(struct open_file *);
static int efipart_ioctl(struct open_file *, u_long, void *);

static int efipart_printfd(int);
static int efipart_printcd(int);
static int efipart_printhd(int);

/* EISA PNP ID's for floppy controllers */
#define	PNP0604	0x604
#define	PNP0700	0x700
#define	PNP0701	0x701

struct devsw efipart_fddev = {
	.dv_name = "fd",
	.dv_type = DEVT_FD,
	.dv_init = efipart_initfd,
	.dv_strategy = efipart_strategy,
	.dv_open = efipart_open,
	.dv_close = efipart_close,
	.dv_ioctl = efipart_ioctl,
	.dv_print = efipart_printfd,
	.dv_cleanup = NULL
};

struct devsw efipart_cddev = {
	.dv_name = "cd",
	.dv_type = DEVT_CD,
	.dv_init = efipart_initcd,
	.dv_strategy = efipart_strategy,
	.dv_open = efipart_open,
	.dv_close = efipart_close,
	.dv_ioctl = efipart_ioctl,
	.dv_print = efipart_printcd,
	.dv_cleanup = NULL
};

struct devsw efipart_hddev = {
	.dv_name = "disk",
	.dv_type = DEVT_DISK,
	.dv_init = efipart_inithd,
	.dv_strategy = efipart_strategy,
	.dv_open = efipart_open,
	.dv_close = efipart_close,
	.dv_ioctl = efipart_ioctl,
	.dv_print = efipart_printhd,
	.dv_cleanup = NULL
};

static pdinfo_list_t fdinfo;
static pdinfo_list_t cdinfo;
static pdinfo_list_t hdinfo;

static EFI_HANDLE *efipart_handles = NULL;
static UINTN efipart_nhandles = 0;

pdinfo_list_t *
efiblk_get_pdinfo_list(struct devsw *dev)
{
	if (dev->dv_type == DEVT_DISK)
		return (&hdinfo);
	if (dev->dv_type == DEVT_CD)
		return (&cdinfo);
	if (dev->dv_type == DEVT_FD)
		return (&fdinfo);
	return (NULL);
}

/* XXX this gets called way way too often, investigate */
pdinfo_t *
efiblk_get_pdinfo(struct devdesc *dev)
{
	pdinfo_list_t *pdi;
	pdinfo_t *pd = NULL;

	pdi = efiblk_get_pdinfo_list(dev->d_dev);
	if (pdi == NULL)
		return (pd);

	STAILQ_FOREACH(pd, pdi, pd_link) {
		if (pd->pd_unit == dev->d_unit)
			return (pd);
	}
	return (pd);
}

pdinfo_t *
efiblk_get_pdinfo_by_device_path(EFI_DEVICE_PATH *path)
{
	unsigned i;
	EFI_DEVICE_PATH *media, *devpath;
	EFI_HANDLE h;

	media = efi_devpath_to_media_path(path);
	if (media == NULL)
		return (NULL);
	for (i = 0; i < efipart_nhandles; i++) {
		h = efipart_handles[i];
		devpath = efi_lookup_devpath(h);
		if (devpath == NULL)
			continue;
		if (!efi_devpath_match_node(media, efi_devpath_to_media_path(devpath)))
			continue;
		return (efiblk_get_pdinfo_by_handle(h));
	}
	return (NULL);
}

static bool
same_handle(pdinfo_t *pd, EFI_HANDLE h)
{

	return (pd->pd_handle == h || pd->pd_alias == h);
}

pdinfo_t *
efiblk_get_pdinfo_by_handle(EFI_HANDLE h)
{
	pdinfo_t *dp, *pp;

	/*
	 * Check hard disks, then cd, then floppy
	 */
	STAILQ_FOREACH(dp, &hdinfo, pd_link) {
		if (same_handle(dp, h))
			return (dp);
		STAILQ_FOREACH(pp, &dp->pd_part, pd_link) {
			if (same_handle(pp, h))
				return (pp);
		}
	}
	STAILQ_FOREACH(dp, &cdinfo, pd_link) {
		if (same_handle(dp, h))
			return (dp);
	}
	STAILQ_FOREACH(dp, &fdinfo, pd_link) {
		if (same_handle(dp, h))
			return (dp);
	}
	return (NULL);
}

static int
efiblk_pdinfo_count(pdinfo_list_t *pdi)
{
	pdinfo_t *pd;
	int i = 0;

	STAILQ_FOREACH(pd, pdi, pd_link) {
		i++;
	}
	return (i);
}

int
efipart_inithandles(void)
{
	UINTN sz;
	EFI_HANDLE *hin;
	EFI_STATUS status;

	if (efipart_nhandles != 0) {
		free(efipart_handles);
		efipart_handles = NULL;
		efipart_nhandles = 0;
	}

	sz = 0;
	hin = NULL;
	status = BS->LocateHandle(ByProtocol, &blkio_guid, 0, &sz, hin);
	if (status == EFI_BUFFER_TOO_SMALL) {
		hin = malloc(sz);
		status = BS->LocateHandle(ByProtocol, &blkio_guid, 0, &sz,
		    hin);
		if (EFI_ERROR(status))
			free(hin);
	}
	if (EFI_ERROR(status))
		return (efi_status_to_errno(status));

	efipart_handles = hin;
	efipart_nhandles = sz / sizeof(*hin);
#ifdef EFIPART_DEBUG
	printf("%s: Got %d BLOCK IO MEDIA handle(s)\n", __func__,
	    efipart_nhandles);
#endif
	return (0);
}

static ACPI_HID_DEVICE_PATH *
efipart_floppy(EFI_DEVICE_PATH *node)
{
	ACPI_HID_DEVICE_PATH *acpi;

	if (DevicePathType(node) == ACPI_DEVICE_PATH &&
	    DevicePathSubType(node) == ACPI_DP) {
		acpi = (ACPI_HID_DEVICE_PATH *) node;
		if (acpi->HID == EISA_PNP_ID(PNP0604) ||
		    acpi->HID == EISA_PNP_ID(PNP0700) ||
		    acpi->HID == EISA_PNP_ID(PNP0701)) {
			return (acpi);
		}
	}
	return (NULL);
}

/*
 * Determine if the provided device path is hdd.
 *
 * There really is no simple fool proof way to classify the devices.
 * Since we do build three lists of devices - floppy, cd and hdd, we
 * will try to see  if the device is floppy or cd, and list anything else
 * as hdd.
 */
static bool
efipart_hdd(EFI_DEVICE_PATH *dp)
{
	unsigned i;
	EFI_DEVICE_PATH *devpath, *node;
	EFI_BLOCK_IO *blkio;
	EFI_STATUS status;

	if (dp == NULL)
		return (false);

	if ((node = efi_devpath_last_node(dp)) == NULL)
		return (false);

	if (efipart_floppy(node) != NULL)
		return (false);

	/*
	 * Test every EFI BLOCK IO handle to make sure dp is not device path
	 * for CD/DVD.
	 */
	for (i = 0; i < efipart_nhandles; i++) {
		devpath = efi_lookup_devpath(efipart_handles[i]);
		if (devpath == NULL)
			return (false);

		/* Only continue testing when dp is prefix in devpath. */
		if (!efi_devpath_is_prefix(dp, devpath))
			continue;

		/*
		 * The device path has to have last node describing the
		 *  device, or we can not test the type.
		 */
		if ((node = efi_devpath_last_node(devpath)) == NULL)
			return (false);

		if (DevicePathType(node) == MEDIA_DEVICE_PATH &&
		    DevicePathSubType(node) == MEDIA_CDROM_DP) {
			return (false);
		}

		/* Make sure we do have the media. */
		status = BS->HandleProtocol(efipart_handles[i],
		    &blkio_guid, (void **)&blkio);
		if (EFI_ERROR(status))
			return (false);

		/* USB or SATA cd without the media. */
		if (blkio->Media->RemovableMedia &&
		    !blkio->Media->MediaPresent) {
			return (false);
		}

		/*
		 * We assume the block size 512 or greater power of 2. 
		 * iPXE is known to insert stub BLOCK IO device with
		 * BlockSize 1.
		 */
		if (blkio->Media->BlockSize < 512 ||
		    !powerof2(blkio->Media->BlockSize)) {
			return (false);
		}
	}
	return (true);
}

/*
 * Add or update entries with new handle data.
 */
static int
efipart_fdinfo_add(EFI_HANDLE handle, uint32_t uid, EFI_DEVICE_PATH *devpath)
{
	pdinfo_t *fd;

	fd = calloc(1, sizeof(pdinfo_t));
	if (fd == NULL) {
		printf("Failed to register floppy %d, out of memory\n", uid);
		return (ENOMEM);
	}
	STAILQ_INIT(&fd->pd_part);

	fd->pd_unit = uid;
	fd->pd_handle = handle;
	fd->pd_devpath = devpath;
	fd->pd_parent = NULL;
	fd->pd_devsw = &efipart_fddev;
	STAILQ_INSERT_TAIL(&fdinfo, fd, pd_link);
	return (0);
}

static void
efipart_updatefd(void)
{
	EFI_DEVICE_PATH *devpath, *node;
	ACPI_HID_DEVICE_PATH *acpi;
	int i;

	for (i = 0; i < efipart_nhandles; i++) {
		devpath = efi_lookup_devpath(efipart_handles[i]);
		if (devpath == NULL)
			continue;

		if ((node = efi_devpath_last_node(devpath)) == NULL)
			continue;
		if ((acpi = efipart_floppy(node)) != NULL) {
			efipart_fdinfo_add(efipart_handles[i], acpi->UID,
			    devpath);
		}
	}
}

static int
efipart_initfd(void)
{

	STAILQ_INIT(&fdinfo);

	efipart_updatefd();

	bcache_add_dev(efiblk_pdinfo_count(&fdinfo));
	return (0);
}

/*
 * Add or update entries with new handle data.
 */
static int
efipart_cdinfo_add(EFI_HANDLE handle, EFI_HANDLE alias,
    EFI_DEVICE_PATH *devpath)
{
	int unit;
	pdinfo_t *cd;
	pdinfo_t *pd;

	unit = 0;
	STAILQ_FOREACH(pd, &cdinfo, pd_link) {
		if (efi_devpath_match(pd->pd_devpath, devpath) == true) {
			pd->pd_handle = handle;
			pd->pd_alias = alias;
			return (0);
		}
		unit++;
	}

	cd = calloc(1, sizeof(pdinfo_t));
	if (cd == NULL) {
		printf("Failed to add cd %d, out of memory\n", unit);
		return (ENOMEM);
	}
	STAILQ_INIT(&cd->pd_part);

	cd->pd_handle = handle;
	cd->pd_unit = unit;
	cd->pd_alias = alias;
	cd->pd_devpath = devpath;
	cd->pd_parent = NULL;
	cd->pd_devsw = &efipart_cddev;
	STAILQ_INSERT_TAIL(&cdinfo, cd, pd_link);
	return (0);
}

static void
efipart_updatecd(void)
{
	int i;
	EFI_DEVICE_PATH *devpath, *devpathcpy, *tmpdevpath, *node;
	EFI_HANDLE handle;
	EFI_BLOCK_IO *blkio;
	EFI_STATUS status;

	for (i = 0; i < efipart_nhandles; i++) {
		devpath = efi_lookup_devpath(efipart_handles[i]);
		if (devpath == NULL)
			continue;

		if ((node = efi_devpath_last_node(devpath)) == NULL)
			continue;

		if (efipart_floppy(node) != NULL)
			continue;

		if (efipart_hdd(devpath))
			continue;

		status = BS->HandleProtocol(efipart_handles[i],
		    &blkio_guid, (void **)&blkio);
		if (EFI_ERROR(status))
			continue;
		/*
		 * If we come across a logical partition of subtype CDROM
		 * it doesn't refer to the CD filesystem itself, but rather
		 * to any usable El Torito boot image on it. In this case
		 * we try to find the parent device and add that instead as
		 * that will be the CD filesystem.
		 */
		if (DevicePathType(node) == MEDIA_DEVICE_PATH &&
		    DevicePathSubType(node) == MEDIA_CDROM_DP) {
			devpathcpy = efi_devpath_trim(devpath);
			if (devpathcpy == NULL)
				continue;
			tmpdevpath = devpathcpy;
			status = BS->LocateDevicePath(&blkio_guid, &tmpdevpath,
			    &handle);
			free(devpathcpy);
			if (EFI_ERROR(status))
				continue;
			devpath = efi_lookup_devpath(handle);
			efipart_cdinfo_add(handle, efipart_handles[i],
			    devpath);
			continue;
		}

		if (DevicePathType(node) == MESSAGING_DEVICE_PATH &&
		    DevicePathSubType(node) == MSG_ATAPI_DP) {
			efipart_cdinfo_add(efipart_handles[i], NULL,
			    devpath);
			continue;
		}

		/* USB or SATA cd without the media. */
		if (blkio->Media->RemovableMedia &&
		    !blkio->Media->MediaPresent) {
			efipart_cdinfo_add(efipart_handles[i], NULL,
			    devpath);
		}
	}
}

static int
efipart_initcd(void)
{

	STAILQ_INIT(&cdinfo);

	efipart_updatecd();

	bcache_add_dev(efiblk_pdinfo_count(&cdinfo));
	return (0);
}

static int
efipart_hdinfo_add(EFI_HANDLE disk_handle, EFI_HANDLE part_handle)
{
	EFI_DEVICE_PATH *disk_devpath, *part_devpath;
	HARDDRIVE_DEVICE_PATH *node;
	int unit;
	pdinfo_t *hd, *pd, *last;

	disk_devpath = efi_lookup_devpath(disk_handle);
	if (disk_devpath == NULL)
		return (ENOENT);

	if (part_handle != NULL) {
		part_devpath = efi_lookup_devpath(part_handle);
		if (part_devpath == NULL)
			return (ENOENT);
		node = (HARDDRIVE_DEVICE_PATH *)
		    efi_devpath_last_node(part_devpath);
		if (node == NULL)
			return (ENOENT);	/* This should not happen. */
	} else {
		part_devpath = NULL;
		node = NULL;
	}

	pd = calloc(1, sizeof(pdinfo_t));
	if (pd == NULL) {
		printf("Failed to add disk, out of memory\n");
		return (ENOMEM);
	}
	STAILQ_INIT(&pd->pd_part);

	STAILQ_FOREACH(hd, &hdinfo, pd_link) {
		if (efi_devpath_match(hd->pd_devpath, disk_devpath) == true) {
			if (part_devpath == NULL)
				return (0);

			/* Add the partition. */
			pd->pd_handle = part_handle;
			pd->pd_unit = node->PartitionNumber;
			pd->pd_devpath = part_devpath;
			pd->pd_parent = hd;
			pd->pd_devsw = &efipart_hddev;
			STAILQ_INSERT_TAIL(&hd->pd_part, pd, pd_link);
			return (0);
		}
	}

	last = STAILQ_LAST(&hdinfo, pdinfo, pd_link);
	if (last != NULL)
		unit = last->pd_unit + 1;
	else
		unit = 0;

	/* Add the disk. */
	hd = pd;
	hd->pd_handle = disk_handle;
	hd->pd_unit = unit;
	hd->pd_devpath = disk_devpath;
	hd->pd_parent = NULL;
	hd->pd_devsw = &efipart_hddev;
	STAILQ_INSERT_TAIL(&hdinfo, hd, pd_link);

	if (part_devpath == NULL)
		return (0);

	pd = calloc(1, sizeof(pdinfo_t));
	if (pd == NULL) {
		printf("Failed to add partition, out of memory\n");
		return (ENOMEM);
	}
	STAILQ_INIT(&pd->pd_part);

	/* Add the partition. */
	pd->pd_handle = part_handle;
	pd->pd_unit = node->PartitionNumber;
	pd->pd_devpath = part_devpath;
	pd->pd_parent = hd;
	pd->pd_devsw = &efipart_hddev;
	STAILQ_INSERT_TAIL(&hd->pd_part, pd, pd_link);

	return (0);
}

/*
 * The MEDIA_FILEPATH_DP has device name.
 * From U-Boot sources it looks like names are in the form
 * of typeN:M, where type is interface type, N is disk id
 * and M is partition id.
 */
static int
efipart_hdinfo_add_filepath(EFI_HANDLE disk_handle)
{
	EFI_DEVICE_PATH *devpath;
	FILEPATH_DEVICE_PATH *node;
	char *pathname, *p;
	int unit, len;
	pdinfo_t *pd, *last;

	/* First collect and verify all the data */
	if ((devpath = efi_lookup_devpath(disk_handle)) == NULL)
		return (ENOENT);
	node = (FILEPATH_DEVICE_PATH *)efi_devpath_last_node(devpath);
	if (node == NULL)
		return (ENOENT);	/* This should not happen. */

	pd = calloc(1, sizeof(pdinfo_t));
	if (pd == NULL) {
		printf("Failed to add disk, out of memory\n");
		return (ENOMEM);
	}
	STAILQ_INIT(&pd->pd_part);
	last = STAILQ_LAST(&hdinfo, pdinfo, pd_link);
	if (last != NULL)
		unit = last->pd_unit + 1;
	else
		unit = 0;

	/* FILEPATH_DEVICE_PATH has 0 terminated string */
	len = ucs2len(node->PathName);
	if ((pathname = malloc(len + 1)) == NULL) {
		printf("Failed to add disk, out of memory\n");
		free(pd);
		return (ENOMEM);
	}
	cpy16to8(node->PathName, pathname, len + 1);
	p = strchr(pathname, ':');

	/*
	 * Assume we are receiving handles in order, first disk handle,
	 * then partitions for this disk. If this assumption proves
	 * false, this code would need update.
	 */
	if (p == NULL) {	/* no colon, add the disk */
		pd->pd_handle = disk_handle;
		pd->pd_unit = unit;
		pd->pd_devpath = devpath;
		pd->pd_parent = NULL;
		pd->pd_devsw = &efipart_hddev;
		STAILQ_INSERT_TAIL(&hdinfo, pd, pd_link);
		free(pathname);
		return (0);
	}
	p++;	/* skip the colon */
	errno = 0;
	unit = (int)strtol(p, NULL, 0);
	if (errno != 0) {
		printf("Bad unit number for partition \"%s\"\n", pathname);
		free(pathname);
		free(pd);
		return (EUNIT);
	}

	/*
	 * We should have disk registered, if not, we are receiving
	 * handles out of order, and this code should be reworked
	 * to create "blank" disk for partition, and to find the
	 * disk based on PathName compares.
	 */
	if (last == NULL) {
		printf("BUG: No disk for partition \"%s\"\n", pathname);
		free(pathname);
		free(pd);
		return (EINVAL);
	}
	/* Add the partition. */
	pd->pd_handle = disk_handle;
	pd->pd_unit = unit;
	pd->pd_devpath = devpath;
	pd->pd_parent = last;
	pd->pd_devsw = &efipart_hddev;
	STAILQ_INSERT_TAIL(&last->pd_part, pd, pd_link);
	free(pathname);
	return (0);
}

static void
efipart_updatehd(void)
{
	int i;
	EFI_DEVICE_PATH *devpath, *devpathcpy, *tmpdevpath, *node;
	EFI_HANDLE handle;
	EFI_BLOCK_IO *blkio;
	EFI_STATUS status;

	for (i = 0; i < efipart_nhandles; i++) {
		devpath = efi_lookup_devpath(efipart_handles[i]);
		if (devpath == NULL)
			continue;

		if ((node = efi_devpath_last_node(devpath)) == NULL)
			continue;

		if (!efipart_hdd(devpath))
			continue;

		status = BS->HandleProtocol(efipart_handles[i],
		    &blkio_guid, (void **)&blkio);
		if (EFI_ERROR(status))
			continue;

		if (DevicePathType(node) == MEDIA_DEVICE_PATH &&
		    DevicePathSubType(node) == MEDIA_FILEPATH_DP) {
			efipart_hdinfo_add_filepath(efipart_handles[i]);
			continue;
		}

		if (DevicePathType(node) == MEDIA_DEVICE_PATH &&
		    DevicePathSubType(node) == MEDIA_HARDDRIVE_DP) {
			devpathcpy = efi_devpath_trim(devpath);
			if (devpathcpy == NULL)
				continue;
			tmpdevpath = devpathcpy;
			status = BS->LocateDevicePath(&blkio_guid, &tmpdevpath,
			    &handle);
			free(devpathcpy);
			if (EFI_ERROR(status))
				continue;
			/*
			 * We do not support nested partitions.
			 */
			devpathcpy = efi_lookup_devpath(handle);
			if (devpathcpy == NULL)
				continue;
			if ((node = efi_devpath_last_node(devpathcpy)) == NULL)
				continue;

			if (DevicePathType(node) == MEDIA_DEVICE_PATH &&
			    DevicePathSubType(node) == MEDIA_HARDDRIVE_DP)
				continue;

			efipart_hdinfo_add(handle, efipart_handles[i]);
			continue;
		}

		efipart_hdinfo_add(efipart_handles[i], NULL);
	}
}

static int
efipart_inithd(void)
{

	STAILQ_INIT(&hdinfo);

	efipart_updatehd();

	bcache_add_dev(efiblk_pdinfo_count(&hdinfo));
	return (0);
}

static int
efipart_print_common(struct devsw *dev, pdinfo_list_t *pdlist, int verbose)
{
	int ret = 0;
	EFI_BLOCK_IO *blkio;
	EFI_STATUS status;
	EFI_HANDLE h;
	pdinfo_t *pd;
	CHAR16 *text;
	struct disk_devdesc pd_dev;
	char line[80];

	if (STAILQ_EMPTY(pdlist))
		return (0);

	printf("%s devices:", dev->dv_name);
	if ((ret = pager_output("\n")) != 0)
		return (ret);

	STAILQ_FOREACH(pd, pdlist, pd_link) {
		h = pd->pd_handle;
		if (verbose) {	/* Output the device path. */
			text = efi_devpath_name(efi_lookup_devpath(h));
			if (text != NULL) {
				printf("  %S", text);
				efi_free_devpath_name(text);
				if ((ret = pager_output("\n")) != 0)
					break;
			}
		}
		snprintf(line, sizeof(line),
		    "    %s%d", dev->dv_name, pd->pd_unit);
		printf("%s:", line);
		status = BS->HandleProtocol(h, &blkio_guid, (void **)&blkio);
		if (!EFI_ERROR(status)) {
			printf("    %llu",
			    blkio->Media->LastBlock == 0? 0:
			    (unsigned long long) (blkio->Media->LastBlock + 1));
			if (blkio->Media->LastBlock != 0) {
				printf(" X %u", blkio->Media->BlockSize);
			}
			printf(" blocks");
			if (blkio->Media->MediaPresent) {
				if (blkio->Media->RemovableMedia)
					printf(" (removable)");
			} else {
				printf(" (no media)");
			}
			if ((ret = pager_output("\n")) != 0)
				break;
			if (!blkio->Media->MediaPresent)
				continue;

			pd->pd_blkio = blkio;
			pd_dev.dd.d_dev = dev;
			pd_dev.dd.d_unit = pd->pd_unit;
			pd_dev.d_slice = D_SLICENONE;
			pd_dev.d_partition = D_PARTNONE;
			ret = disk_open(&pd_dev, blkio->Media->BlockSize *
			    (blkio->Media->LastBlock + 1),
			    blkio->Media->BlockSize);
			if (ret == 0) {
				ret = disk_print(&pd_dev, line, verbose);
				disk_close(&pd_dev);
				if (ret != 0)
					return (ret);
			} else {
				/* Do not fail from disk_open() */
				ret = 0;
			}
		} else {
			if ((ret = pager_output("\n")) != 0)
				break;
		}
	}
	return (ret);
}

static int
efipart_printfd(int verbose)
{
	return (efipart_print_common(&efipart_fddev, &fdinfo, verbose));
}

static int
efipart_printcd(int verbose)
{
	return (efipart_print_common(&efipart_cddev, &cdinfo, verbose));
}

static int
efipart_printhd(int verbose)
{
	return (efipart_print_common(&efipart_hddev, &hdinfo, verbose));
}

static int
efipart_open(struct open_file *f, ...)
{
	va_list args;
	struct disk_devdesc *dev;
	pdinfo_t *pd;
	EFI_BLOCK_IO *blkio;
	EFI_STATUS status;

	va_start(args, f);
	dev = va_arg(args, struct disk_devdesc*);
	va_end(args);
	if (dev == NULL)
		return (EINVAL);

	pd = efiblk_get_pdinfo((struct devdesc *)dev);
	if (pd == NULL)
		return (EIO);

	if (pd->pd_blkio == NULL) {
		status = BS->HandleProtocol(pd->pd_handle, &blkio_guid,
		    (void **)&pd->pd_blkio);
		if (EFI_ERROR(status))
			return (efi_status_to_errno(status));
	}

	blkio = pd->pd_blkio;
	if (!blkio->Media->MediaPresent)
		return (EAGAIN);

	pd->pd_open++;
	if (pd->pd_bcache == NULL)
		pd->pd_bcache = bcache_allocate();

	if (dev->dd.d_dev->dv_type == DEVT_DISK) {
		int rc;

		rc = disk_open(dev,
		    blkio->Media->BlockSize * (blkio->Media->LastBlock + 1),
		    blkio->Media->BlockSize);
		if (rc != 0) {
			pd->pd_open--;
			if (pd->pd_open == 0) {
				pd->pd_blkio = NULL;
				bcache_free(pd->pd_bcache);
				pd->pd_bcache = NULL;
			}
		}
		return (rc);
	}
	return (0);
}

static int
efipart_close(struct open_file *f)
{
	struct disk_devdesc *dev;
	pdinfo_t *pd;

	dev = (struct disk_devdesc *)(f->f_devdata);
	if (dev == NULL)
		return (EINVAL);

	pd = efiblk_get_pdinfo((struct devdesc *)dev);
	if (pd == NULL)
		return (EINVAL);

	pd->pd_open--;
	if (pd->pd_open == 0) {
		pd->pd_blkio = NULL;
		bcache_free(pd->pd_bcache);
		pd->pd_bcache = NULL;
	}
	if (dev->dd.d_dev->dv_type == DEVT_DISK)
		return (disk_close(dev));
	return (0);
}

static int
efipart_ioctl(struct open_file *f, u_long cmd, void *data)
{
	struct disk_devdesc *dev;
	pdinfo_t *pd;
	int rc;

	dev = (struct disk_devdesc *)(f->f_devdata);
	if (dev == NULL)
		return (EINVAL);

	pd = efiblk_get_pdinfo((struct devdesc *)dev);
	if (pd == NULL)
		return (EINVAL);

	if (dev->dd.d_dev->dv_type == DEVT_DISK) {
		rc = disk_ioctl(dev, cmd, data);
		if (rc != ENOTTY)
			return (rc);
	}

	switch (cmd) {
	case DIOCGSECTORSIZE:
		*(u_int *)data = pd->pd_blkio->Media->BlockSize;
		break;
	case DIOCGMEDIASIZE:
		*(uint64_t *)data = pd->pd_blkio->Media->BlockSize *
		    (pd->pd_blkio->Media->LastBlock + 1);
		break;
	default:
		return (ENOTTY);
	}

	return (0);
}

/*
 * efipart_readwrite()
 * Internal equivalent of efipart_strategy(), which operates on the
 * media-native block size. This function expects all I/O requests
 * to be within the media size and returns an error if such is not
 * the case.
 */
static int
efipart_readwrite(EFI_BLOCK_IO *blkio, int rw, daddr_t blk, daddr_t nblks,
    char *buf)
{
	EFI_STATUS status;

	if (blkio == NULL)
		return (ENXIO);
	if (blk < 0 || blk > blkio->Media->LastBlock)
		return (EIO);
	if ((blk + nblks - 1) > blkio->Media->LastBlock)
		return (EIO);

	switch (rw & F_MASK) {
	case F_READ:
		status = blkio->ReadBlocks(blkio, blkio->Media->MediaId, blk,
		    nblks * blkio->Media->BlockSize, buf);
		break;
	case F_WRITE:
		if (blkio->Media->ReadOnly)
			return (EROFS);
		status = blkio->WriteBlocks(blkio, blkio->Media->MediaId, blk,
		    nblks * blkio->Media->BlockSize, buf);
		break;
	default:
		return (ENOSYS);
	}

	if (EFI_ERROR(status)) {
		printf("%s: rw=%d, blk=%ju size=%ju status=%lu\n", __func__, rw,
		    blk, nblks, EFI_ERROR_CODE(status));
	}
	return (efi_status_to_errno(status));
}

static int
efipart_strategy(void *devdata, int rw, daddr_t blk, size_t size,
    char *buf, size_t *rsize)
{
	struct bcache_devdata bcd;
	struct disk_devdesc *dev;
	pdinfo_t *pd;

	dev = (struct disk_devdesc *)devdata;
	if (dev == NULL)
		return (EINVAL);

	pd = efiblk_get_pdinfo((struct devdesc *)dev);
	if (pd == NULL)
		return (EINVAL);

	if (pd->pd_blkio->Media->RemovableMedia &&
	    !pd->pd_blkio->Media->MediaPresent)
		return (ENXIO);

	bcd.dv_strategy = efipart_realstrategy;
	bcd.dv_devdata = devdata;
	bcd.dv_cache = pd->pd_bcache;

	if (dev->dd.d_dev->dv_type == DEVT_DISK) {
		daddr_t offset;

		offset = dev->d_offset * pd->pd_blkio->Media->BlockSize;
		offset /= 512;
		return (bcache_strategy(&bcd, rw, blk + offset,
		    size, buf, rsize));
	}
	return (bcache_strategy(&bcd, rw, blk, size, buf, rsize));
}

static int
efipart_realstrategy(void *devdata, int rw, daddr_t blk, size_t size,
    char *buf, size_t *rsize)
{
	struct disk_devdesc *dev = (struct disk_devdesc *)devdata;
	pdinfo_t *pd;
	EFI_BLOCK_IO *blkio;
	uint64_t off, disk_blocks, d_offset = 0;
	char *blkbuf;
	size_t blkoff, blksz;
	int error;
	size_t diskend, readstart;

	if (dev == NULL || blk < 0)
		return (EINVAL);

	pd = efiblk_get_pdinfo((struct devdesc *)dev);
	if (pd == NULL)
		return (EINVAL);

	blkio = pd->pd_blkio;
	if (blkio == NULL)
		return (ENXIO);

	if (size == 0 || (size % 512) != 0)
		return (EIO);

	off = blk * 512;
	/*
	 * Get disk blocks, this value is either for whole disk or for
	 * partition.
	 */
	disk_blocks = 0;
	if (dev->dd.d_dev->dv_type == DEVT_DISK) {
		if (disk_ioctl(dev, DIOCGMEDIASIZE, &disk_blocks) == 0) {
			/* DIOCGMEDIASIZE does return bytes. */
			disk_blocks /= blkio->Media->BlockSize;
		}
		d_offset = dev->d_offset;
	}
	if (disk_blocks == 0)
		disk_blocks = blkio->Media->LastBlock + 1 - d_offset;

	/* make sure we don't read past disk end */
	if ((off + size) / blkio->Media->BlockSize > d_offset + disk_blocks) {
		diskend = d_offset + disk_blocks;
		readstart = off / blkio->Media->BlockSize;

		if (diskend <= readstart) {
			if (rsize != NULL)
				*rsize = 0;

			return (EIO);
		}
		size = diskend - readstart;
		size = size * blkio->Media->BlockSize;
	}

	if (rsize != NULL)
		*rsize = size;

	if ((size % blkio->Media->BlockSize == 0) &&
	    (off % blkio->Media->BlockSize == 0))
		return (efipart_readwrite(blkio, rw,
		    off / blkio->Media->BlockSize,
		    size / blkio->Media->BlockSize, buf));

	/*
	 * The block size of the media is not a multiple of I/O.
	 */
	blkbuf = malloc(blkio->Media->BlockSize);
	if (blkbuf == NULL)
		return (ENOMEM);

	error = 0;
	blk = off / blkio->Media->BlockSize;
	blkoff = off % blkio->Media->BlockSize;
	blksz = blkio->Media->BlockSize - blkoff;
	while (size > 0) {
		error = efipart_readwrite(blkio, rw, blk, 1, blkbuf);
		if (error)
			break;
		if (size < blksz)
			blksz = size;
		bcopy(blkbuf + blkoff, buf, blksz);
		buf += blksz;
		size -= blksz;
		blk++;
		blkoff = 0;
		blksz = blkio->Media->BlockSize;
	}

	free(blkbuf);
	return (error);
}
