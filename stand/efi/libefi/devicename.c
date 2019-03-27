/*-
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
 * Copyright (c) 2006 Marcel Moolenaar
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

#include <stand.h>
#include <string.h>
#include <sys/disklabel.h>
#include <sys/param.h>
#include <bootstrap.h>
#include <disk.h>
#ifdef EFI_ZFS_BOOT
#include <libzfs.h>
#endif

#include <efi.h>
#include <efilib.h>

static int efi_parsedev(struct devdesc **, const char *, const char **);

/*
 * Point (dev) at an allocated device specifier for the device matching the
 * path in (devspec). If it contains an explicit device specification,
 * use that.  If not, use the default device.
 */
int
efi_getdev(void **vdev, const char *devspec, const char **path)
{
	struct devdesc **dev = (struct devdesc **)vdev;
	int rv;

	/*
	 * If it looks like this is just a path and no device, then
	 * use the current device instead.
	 */
	if (devspec == NULL || *devspec == '/' || !strchr(devspec, ':')) {
		rv = efi_parsedev(dev, getenv("currdev"), NULL);
		if (rv == 0 && path != NULL)
			*path = devspec;
		return (rv);
	}

	/* Parse the device name off the beginning of the devspec. */
	return (efi_parsedev(dev, devspec, path));
}

/*
 * Point (dev) at an allocated device specifier matching the string version
 * at the beginning of (devspec).  Return a pointer to the remaining
 * text in (path).
 *
 * In all cases, the beginning of (devspec) is compared to the names
 * of known devices in the device switch, and then any following text
 * is parsed according to the rules applied to the device type.
 *
 * For disk-type devices, the syntax is:
 *
 * fs<unit>:
 */
static int
efi_parsedev(struct devdesc **dev, const char *devspec, const char **path)
{
	struct devdesc *idev;
	struct devsw *dv;
	int i, unit, err;
	char *cp;
	const char *np;

	/* minimum length check */
	if (strlen(devspec) < 2)
		return (EINVAL);

	/* look for a device that matches */
	for (i = 0; devsw[i] != NULL; i++) {
		dv = devsw[i];
		if (!strncmp(devspec, dv->dv_name, strlen(dv->dv_name)))
			break;
	}
	if (devsw[i] == NULL)
		return (ENOENT);

	np = devspec + strlen(dv->dv_name);
	idev = NULL;
	err = 0;

	switch (dv->dv_type) {
	case DEVT_NONE:
		break;

	case DEVT_DISK:
		idev = malloc(sizeof(struct disk_devdesc));
		if (idev == NULL)
			return (ENOMEM);

		err = disk_parsedev((struct disk_devdesc *)idev, np, path);
		if (err != 0)
			goto fail;
		break;

#ifdef EFI_ZFS_BOOT
	case DEVT_ZFS:
		idev = malloc(sizeof(struct zfs_devdesc));
		if (idev == NULL)
			return (ENOMEM);

		err = zfs_parsedev((struct zfs_devdesc*)idev, np, path);
		if (err != 0)
			goto fail;
		break;
#endif
	default:
		idev = malloc(sizeof(struct devdesc));
		if (idev == NULL)
			return (ENOMEM);

		unit = 0;
		cp = (char *)np;

		if (*np != '\0' && *np != ':') {
			errno = 0;
			unit = strtol(np, &cp, 0);
			if (errno != 0 || cp == np) {
				err = EUNIT;
				goto fail;
			}
		}
		if (*cp != '\0' && *cp != ':') {
			err = EINVAL;
			goto fail;
		}

		idev->d_unit = unit;
		if (path != NULL)
			*path = (*cp == 0) ? cp : cp + 1;
		break;
	}

	idev->d_dev = dv;

	if (dev != NULL)
		*dev = idev;
	else
		free(idev);
	return (0);

fail:
	free(idev);
	return (err);
}

char *
efi_fmtdev(void *vdev)
{
	struct devdesc *dev = (struct devdesc *)vdev;
	static char buf[SPECNAMELEN + 1];

	switch(dev->d_dev->dv_type) {
	case DEVT_NONE:
		strcpy(buf, "(no device)");
		break;

	case DEVT_DISK:
		return (disk_fmtdev(vdev));

#ifdef EFI_ZFS_BOOT
	case DEVT_ZFS:
		return (zfs_fmtdev(dev));
#endif
	default:
		sprintf(buf, "%s%d:", dev->d_dev->dv_name, dev->d_unit);
		break;
	}

	return (buf);
}

/*
 * Set currdev to suit the value being supplied in (value)
 */
int
efi_setcurrdev(struct env_var *ev, int flags, const void *value)
{
	struct devdesc *ncurr;
	int rv;

	rv = efi_parsedev(&ncurr, value, NULL);
	if (rv != 0)
		return (rv);

	free(ncurr);
	env_setenv(ev->ev_name, flags | EV_NOHOOK, value, NULL, NULL);
	return (0);
}
