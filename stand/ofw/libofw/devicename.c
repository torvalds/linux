/*-
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
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

#include "bootstrap.h"
#include "libofw.h"
#include "libzfs.h"

static int ofw_parsedev(struct ofw_devdesc **, const char *, const char **);

/* 
 * Point (dev) at an allocated device specifier for the device matching the
 * path in (devspec). If it contains an explicit device specification,
 * use that.  If not, use the default device.
 */
int
ofw_getdev(void **vdev, const char *devspec, const char **path)
{
    struct ofw_devdesc **dev = (struct ofw_devdesc **)vdev;
    int				rv;

    /*
     * If it looks like this is just a path and no
     * device, go with the current device.
     */
    if ((devspec == NULL) || 
	((strchr(devspec, '@') == NULL) &&
	(strchr(devspec, ':') == NULL))) {

	if (((rv = ofw_parsedev(dev, getenv("currdev"), NULL)) == 0) &&
	    (path != NULL))
		*path = devspec;
	return(rv);
    }
    
    /*
     * Try to parse the device name off the beginning of the devspec
     */
    return(ofw_parsedev(dev, devspec, path));
}

/*
 * Point (dev) at an allocated device specifier matching the string version
 * at the beginning of (devspec).  Return a pointer to the remaining
 * text in (path).
 */
static int
ofw_parsedev(struct ofw_devdesc **dev, const char *devspec, const char **path)
{
    struct ofw_devdesc	*idev;
    struct devsw	*dv;
    phandle_t		handle;
    const char		*p;
    const char		*s;
    char		*ep;
    char		name[256];
    char		type[64];
    int			err;
    int			len;
    int			i;

    for (p = s = devspec; *s != '\0'; p = s) {
	if ((s = strchr(p + 1, '/')) == NULL)
	    s = strchr(p, '\0');
	len = s - devspec;
	bcopy(devspec, name, len);
	name[len] = '\0';
	if ((handle = OF_finddevice(name)) == -1) {
	    bcopy(name, type, len);
	    type[len] = '\0';
	} else if (OF_getprop(handle, "device_type", type, sizeof(type)) == -1)
	    continue;
	for (i = 0; (dv = devsw[i]) != NULL; i++) {
	    if (strncmp(dv->dv_name, type, strlen(dv->dv_name)) == 0)
		goto found;
	}
    }
    return(ENOENT);

found:
    if (path != NULL)
	*path = s;
    idev = malloc(sizeof(struct ofw_devdesc));
    if (idev == NULL) {
	printf("ofw_parsedev: malloc failed\n");
	return ENOMEM;
    }
    strcpy(idev->d_path, name);
    idev->dd.d_dev = dv;
    if (dv->dv_type == DEVT_ZFS) {
	p = devspec + strlen(dv->dv_name);
	err = zfs_parsedev((struct zfs_devdesc *)idev, p, path);
	if (err != 0) {
	    free(idev);
	    return (err);
	}
    }

    if (dev == NULL) {
	free(idev);
    } else {
	*dev = idev;
    }
    return(0);
}

int
ofw_setcurrdev(struct env_var *ev, int flags, const void *value)
{
    struct ofw_devdesc	*ncurr;
    int			rv;

    if ((rv = ofw_parsedev(&ncurr, value, NULL)) != 0)
	return rv;

    free(ncurr);
    env_setenv(ev->ev_name, flags | EV_NOHOOK, value, NULL, NULL);
    return 0;
}
