/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000, 2001 Michael Smith
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
 */

/*
 * Print information about system device configuration.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "devinfo.h"

static int	rflag;
static int	vflag;

static void	print_resource(struct devinfo_res *);
static int	print_device_matching_resource(struct devinfo_res *, void *);
static int	print_device_rman_resources(struct devinfo_rman *, void *);
static int	print_device(struct devinfo_dev *, void *);
static int	print_rman_resource(struct devinfo_res *, void *);
static int	print_rman(struct devinfo_rman *, void *);

struct indent_arg
{
	int	indent;
	void	*arg;
};

/*
 * Print a resource.
 */
void
print_resource(struct devinfo_res *res)
{
	struct devinfo_rman	*rman;
	int			hexmode;

	rman = devinfo_handle_to_rman(res->dr_rman);
	hexmode =  (rman->dm_size > 1000) || (rman->dm_size == 0);
	printf(hexmode ? "0x%jx" : "%ju", res->dr_start);
	if (res->dr_size > 1)
		printf(hexmode ? "-0x%jx" : "-%ju",
		    res->dr_start + res->dr_size - 1);
}

/*
 * Print resource information if this resource matches the
 * given device.
 *
 * If the given indent is 0, return an indicator that a matching
 * resource exists.
 */
int
print_device_matching_resource(struct devinfo_res *res, void *arg)
{
	struct indent_arg	*ia = (struct indent_arg *)arg;
	struct devinfo_dev	*dev = (struct devinfo_dev *)ia->arg;
	int			i;

	if (devinfo_handle_to_device(res->dr_device) == dev) {
		/* in 'detect' mode, found a match */
		if (ia->indent == 0)
			return(1);
		for (i = 0; i < ia->indent; i++)
			printf(" ");
		print_resource(res);
		printf("\n");
	}
	return(0);
}

/*
 * Print resource information for this device and resource manager.
 */
int
print_device_rman_resources(struct devinfo_rman *rman, void *arg)
{
	struct indent_arg	*ia = (struct indent_arg *)arg;
	int			indent, i;

	indent = ia->indent;

	/* check whether there are any resources matching this device */
	ia->indent = 0;
	if (devinfo_foreach_rman_resource(rman,
	    print_device_matching_resource, ia) != 0) {

		/* there are, print header */
		for (i = 0; i < indent; i++)
			printf(" ");
		printf("%s:\n", rman->dm_desc);

		/* print resources */
		ia->indent = indent + 4;
		devinfo_foreach_rman_resource(rman,
		    print_device_matching_resource, ia);
	}
	ia->indent = indent;
	return(0);
}

static void
print_dev(struct devinfo_dev *dev)
{

	printf("%s", dev->dd_name[0] ? dev->dd_name : "unknown");
	if (vflag && *dev->dd_pnpinfo)
		printf(" pnpinfo %s", dev->dd_pnpinfo);
	if (vflag && *dev->dd_location)
		printf(" at %s", dev->dd_location);
	if (!(dev->dd_flags & DF_ENABLED))
		printf(" (disabled)");
	else if (dev->dd_flags & DF_SUSPENDED)
		printf(" (suspended)");
}


/*
 * Print information about a device.
 */
int
print_device(struct devinfo_dev *dev, void *arg)
{
	struct indent_arg	ia;
	int			i, indent;

	if (vflag || (dev->dd_name[0] != 0 && dev->dd_state >= DS_ATTACHED)) {
		indent = (int)(intptr_t)arg;
		for (i = 0; i < indent; i++)
			printf(" ");
		print_dev(dev);
		printf("\n");
		if (rflag) {
			ia.indent = indent + 4;
			ia.arg = dev;
			devinfo_foreach_rman(print_device_rman_resources,
			    (void *)&ia);
		}
	}

	return(devinfo_foreach_device_child(dev, print_device,
	    (void *)((char *)arg + 2)));
}

/*
 * Print information about a resource under a resource manager.
 */
int
print_rman_resource(struct devinfo_res *res, void *arg __unused)
{
	struct devinfo_dev	*dev;
	
	printf("    ");
	print_resource(res);
	dev = devinfo_handle_to_device(res->dr_device);
	if ((dev != NULL) && (dev->dd_name[0] != 0)) {
		printf(" (%s)", dev->dd_name);
	} else {
		printf(" ----");
	}
	printf("\n");
	return(0);
}

/*
 * Print information about a resource manager.
 */
int
print_rman(struct devinfo_rman *rman, void *arg __unused)
{
	printf("%s:\n", rman->dm_desc);
	devinfo_foreach_rman_resource(rman, print_rman_resource, 0);
	return(0);
}

static int
print_path(struct devinfo_dev *dev, void *xname)
{
	const char *name = xname;
	int rv;

	if (strcmp(dev->dd_name, name) == 0) {
		print_dev(dev);
		if (vflag)
			printf("\n");
		return (1);
	}

	rv = devinfo_foreach_device_child(dev, print_path, xname);
	if (rv == 1) {
		printf(" ");
		print_dev(dev);
		if (vflag)
			printf("\n");
	}
	return (rv);
}

static void __dead2
usage(void)
{
	fprintf(stderr, "%s\n%s\n%s\n",
	    "usage: devinfo [-rv]",
	    "       devinfo -u",
	    "       devifno -p dev [-v]");
	exit(1);
}

int
main(int argc, char *argv[]) 
{
	struct devinfo_dev	*root;
	int			c, uflag, rv;
	char			*path = NULL;

	uflag = 0;
	while ((c = getopt(argc, argv, "p:ruv")) != -1) {
		switch(c) {
		case 'p':
			path = optarg;
			break;
		case 'r':
			rflag++;
			break;
		case 'u':
			uflag++;
			break;
		case 'v':
			vflag++;
			break;
		default:
			usage();
		}
	}

	if (path && (rflag || uflag))
		usage();

	if ((rv = devinfo_init()) != 0) {
		errno = rv;
		err(1, "devinfo_init");
	}

	if ((root = devinfo_handle_to_device(DEVINFO_ROOT_DEVICE)) == NULL)
		errx(1, "can't find root device");

	if (path) {
		if (devinfo_foreach_device_child(root, print_path, (void *)path) == 0)
			errx(1, "%s: Not found", path);
		if (!vflag)
			printf("\n");
	} else if (uflag) {
		/* print resource usage? */
		devinfo_foreach_rman(print_rman, NULL);
	} else {
		/* print device hierarchy */
		devinfo_foreach_device_child(root, print_device, (void *)0);
	}
	return(0);
}
