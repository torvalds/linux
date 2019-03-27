/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 * $FreeBSD$
 */

/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"@(#)devname2devid.c	1.3	07/05/25 SMI"

#include <sys/types.h>
#include <sys/stat.h>
#include <devid.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

/*
 * Usage: devname2devid <devicepath>
 *
 * Examples:
 *	# ./devname2devid /dev/c1t4d0s0
 *	devid id1,sd@SSEAGATE_ST318404LSUN18G_3BT2G0Z300002146G4CR/a
 *	# ./devname2devid /dev/c1t4d0
 *	devid id1,sd@SSEAGATE_ST318404LSUN18G_3BT2G0Z300002146G4CR/wd
 *	# ./devname2devid /dev/c1t4d0s1
 *	devid id1,sd@SSEAGATE_ST318404LSUN18G_3BT2G0Z300002146G4CR/b
 *	#
 *
 * This program accepts a disk or disk slice path and prints a
 * device id.
 *
 * Exit values:
 *	0 - means success
 *	1 - means failure
 *
 */
int
main(int argc, char *argv[])
{
	int		fd;
	ddi_devid_t	devid;
	char		*minor_name, *devidstr, *device;
#ifdef DEBUG
	devid_nmlist_t  *list = NULL;
	char		*search_path;
	int		i;
#endif

	if (argc == 1) {
		(void) printf("%s <devicepath> [search path]\n",
		    argv[0]);
		exit(1);
	}
	device = argv[1];

	if ((fd = open(device, O_RDONLY|O_NDELAY)) < 0) {
		perror(device);
		exit(1);
	}
	if (devid_get(fd, &devid) != 0) {
		perror("devid_get");
		exit(1);
	}
	if (devid_get_minor_name(fd, &minor_name) != 0) {
		perror("devid_get_minor_name");
		exit(1);
	}
	if ((devidstr = devid_str_encode(devid, minor_name)) == 0) {
		perror("devid_str_encode");
		exit(1);
	}

	(void) printf("devid %s\n", devidstr);

	devid_str_free(devidstr);

#ifdef DEBUG
	if (argc == 3) {
		search_path = argv[2];
	} else {
		search_path = "/dev/";
	}

	if (devid_deviceid_to_nmlist(search_path, devid, DEVID_MINOR_NAME_ALL,
	    &list)) {
		perror("devid_deviceid_to_nmlist");
		exit(1);
	}

	/* loop through list and process device names and numbers */
	for (i = 0; list[i].devname != NULL; i++) {
		(void) printf("devname: %s %p\n", list[i].devname, list[i].dev);
	}
	devid_free_nmlist(list);

#endif /* DEBUG */

	devid_str_free(minor_name);
	devid_free(devid);

	return (0);
}
