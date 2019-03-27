/*-
 * Copyright (c) 2015 Allan Jude <allanjude@FreeBSD.org>
 * Copyright (c) 2015 Xin LI <delphij@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#include <sys/types.h>
#include <cddl/compat/opensolaris/sys/types.h>
#include <sys/time.h>
#include <cddl/compat/opensolaris/sys/time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libnvpair.h>
#include <sys/vdev_impl.h>

#include "fstyp.h"

int
fstyp_zfs(FILE *fp, char *label, size_t labelsize)
{
	vdev_label_t *vdev_label = NULL;
	vdev_phys_t *vdev_phys;
	char *zpool_name = NULL;
	nvlist_t *config = NULL;
	int err = 0;

	/*
	 * Read in the first ZFS vdev label ("L0"), located at the beginning
	 * of the vdev and extract the pool name from it.
	 *
	 * TODO: the checksum of label should be validated.
	 */
	vdev_label = (vdev_label_t *)read_buf(fp, 0, sizeof(*vdev_label));
	if (vdev_label == NULL)
		return (1);

	vdev_phys = &(vdev_label->vl_vdev_phys);

	if ((nvlist_unpack(vdev_phys->vp_nvlist, sizeof(vdev_phys->vp_nvlist),
	    &config, 0)) == 0 &&
	    (nvlist_lookup_string(config, "name", &zpool_name) == 0)) {
		strlcpy(label, zpool_name, labelsize);
	} else
		err = 1;

	nvlist_free(config);
	free(vdev_label);

	return (err);
}
