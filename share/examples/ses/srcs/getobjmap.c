/* $FreeBSD$ */
/*
 * Copyright (c) 2000 by Matthew Jacob
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * the GNU Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * Matthew Jacob
 * Feral Software
 * mjacob@feral.com
 */

#include <unistd.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_ses.h>

#include "eltsub.h"

int
main(int a, char **v)
{
	ses_object *objp;
	int nobj, fd, i;

	while (*++v) {
		fd = open(*v, O_RDONLY);
		if (fd < 0) {
			perror(*v);
			continue;
		}
		if (ioctl(fd, SESIOC_GETNOBJ, (caddr_t) &nobj) < 0) {
			perror("SESIOC_GETNOBJ");
			(void) close(fd);
			continue;
		}
		fprintf(stdout, "%s: %d objects\n", *v, nobj);
		if (nobj == 0) {
			(void) close(fd);
			continue;
		}
		objp = calloc(nobj, sizeof (ses_object));
		if (objp == NULL) {
			perror("calloc");
			(void) close(fd);
			continue;
		}
		if (ioctl(fd, SESIOC_GETOBJMAP, (caddr_t) objp) < 0) {
			perror("SESIOC_GETOBJMAP");
			(void) close(fd);
			continue;
		}
		for (i = 0; i < nobj; i++) {
			printf(" Object %d: ID 0x%x Type '%s'\n", i,
			    objp[i].obj_id, geteltnm((int)objp[i].object_type));
		}
		free(objp);
		(void) close(fd);
	}
	return (0);
}
