/*-
 * Copyright (c) 2015 Eric McCorkle
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
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <efi.h>

#include "boot_module.h"

#include "libzfs.h"
#include "zfsimpl.c"

static dev_info_t *devices;

uint64_t
ldi_get_size(void *priv)
{
	dev_info_t *devinfo = priv;

	return (devinfo->dev->Media->BlockSize *
	    (devinfo->dev->Media->LastBlock + 1));
}

static int
vdev_read(vdev_t *vdev, void *priv, off_t off, void *buf, size_t bytes)
{
	dev_info_t *devinfo;
	uint64_t lba;
	size_t size, remainder, rb_size, blksz;
	char *bouncebuf = NULL, *rb_buf;
	EFI_STATUS status;

	devinfo = (dev_info_t *)priv;
	lba = off / devinfo->dev->Media->BlockSize;
	remainder = off % devinfo->dev->Media->BlockSize;

	rb_buf = buf;
	rb_size = bytes;

	/*
	 * If we have remainder from off, we need to add remainder part.
	 * Since buffer must be multiple of the BlockSize, round it all up.
	 */
	size = roundup2(bytes + remainder, devinfo->dev->Media->BlockSize);
	blksz = size;
	if (remainder != 0 || size != bytes) {
		rb_size = devinfo->dev->Media->BlockSize;
		bouncebuf = malloc(rb_size);
		if (bouncebuf == NULL) {
			printf("vdev_read: out of memory\n");
			return (-1);
		}
		rb_buf = bouncebuf;
		blksz = rb_size - remainder;
	}

	while (bytes > 0) {
		status = devinfo->dev->ReadBlocks(devinfo->dev,
		    devinfo->dev->Media->MediaId, lba, rb_size, rb_buf);
		if (EFI_ERROR(status))
				goto error;
		if (bytes < blksz)
			blksz = bytes;
		if (bouncebuf != NULL)
			memcpy(buf, rb_buf + remainder, blksz);
		buf = (void *)((uintptr_t)buf + blksz);
		bytes -= blksz;
		lba++;
		remainder = 0;
		blksz = rb_size;
	}

	free(bouncebuf);
	return (0);

error:
	free(bouncebuf);
	DPRINTF("vdev_read: failed dev: %p, id: %u, lba: %ju, size: %zu,"
	    " rb_size: %zu, status: %lu\n", devinfo->dev,
	    devinfo->dev->Media->MediaId, (uintmax_t)lba, bytes, rb_size,
	    EFI_ERROR_CODE(status));
	return (-1);
}

static EFI_STATUS
probe(dev_info_t *dev)
{
	spa_t *spa;
	dev_info_t *tdev;
	EFI_STATUS status;

	/* ZFS consumes the dev on success so we need a copy. */
	if ((status = BS->AllocatePool(EfiLoaderData, sizeof(*dev),
	    (void**)&tdev)) != EFI_SUCCESS) {
		DPRINTF("Failed to allocate tdev (%lu)\n",
		    EFI_ERROR_CODE(status));
		return (status);
	}
	memcpy(tdev, dev, sizeof(*dev));

	if (vdev_probe(vdev_read, tdev, &spa) != 0) {
		(void)BS->FreePool(tdev);
		return (EFI_UNSUPPORTED);
	}

	dev->devdata = spa;
	add_device(&devices, dev);

	return (EFI_SUCCESS);
}

static EFI_STATUS
load(const char *filepath, dev_info_t *devinfo, void **bufp, size_t *bufsize)
{
	spa_t *spa;
	struct zfsmount zfsmount;
	dnode_phys_t dn;
	struct stat st;
	int err;
	void *buf;
	EFI_STATUS status;

	spa = devinfo->devdata;

#ifdef EFI_DEBUG
	{
		CHAR16 *text = efi_devpath_name(devinfo->devpath);
		DPRINTF("load: '%s' spa: '%s', devpath: %S\n", filepath,
		    spa->spa_name, text);
		efi_free_devpath_name(text);
	}
#endif
	if ((err = zfs_spa_init(spa)) != 0) {
		DPRINTF("Failed to load pool '%s' (%d)\n", spa->spa_name, err);
		return (EFI_NOT_FOUND);
	}

	if ((err = zfs_mount(spa, 0, &zfsmount)) != 0) {
		DPRINTF("Failed to mount pool '%s' (%d)\n", spa->spa_name, err);
		return (EFI_NOT_FOUND);
	}

	if ((err = zfs_lookup(&zfsmount, filepath, &dn)) != 0) {
		if (err == ENOENT) {
			DPRINTF("Failed to find '%s' on pool '%s' (%d)\n",
			    filepath, spa->spa_name, err);
			return (EFI_NOT_FOUND);
		}
		printf("Failed to lookup '%s' on pool '%s' (%d)\n", filepath,
		    spa->spa_name, err);
		return (EFI_INVALID_PARAMETER);
	}

	if ((err = zfs_dnode_stat(spa, &dn, &st)) != 0) {
		printf("Failed to stat '%s' on pool '%s' (%d)\n", filepath,
		    spa->spa_name, err);
		return (EFI_INVALID_PARAMETER);
	}

	if ((status = BS->AllocatePool(EfiLoaderData, (UINTN)st.st_size, &buf))
	    != EFI_SUCCESS) {
		printf("Failed to allocate load buffer %jd for pool '%s' for '%s' "
		    "(%lu)\n", (intmax_t)st.st_size, spa->spa_name, filepath, EFI_ERROR_CODE(status));
		return (EFI_INVALID_PARAMETER);
	}

	if ((err = dnode_read(spa, &dn, 0, buf, st.st_size)) != 0) {
		printf("Failed to read node from %s (%d)\n", spa->spa_name,
		    err);
		(void)BS->FreePool(buf);
		return (EFI_INVALID_PARAMETER);
	}

	*bufsize = st.st_size;
	*bufp = buf;

	return (EFI_SUCCESS);
}

static void
status(void)
{
	spa_t *spa;

	spa = STAILQ_FIRST(&zfs_pools);
	if (spa == NULL) {
		printf("%s found no pools\n", zfs_module.name);
		return;
	}

	printf("%s found the following pools:", zfs_module.name);
	STAILQ_FOREACH(spa, &zfs_pools, spa_link)
		printf(" %s", spa->spa_name);

	printf("\n");
}

static void
init(void)
{

	zfs_init();
}

static dev_info_t *
_devices(void)
{

	return (devices);
}

const boot_module_t zfs_module =
{
	.name = "ZFS",
	.init = init,
	.probe = probe,
	.load = load,
	.status = status,
	.devices = _devices
};
