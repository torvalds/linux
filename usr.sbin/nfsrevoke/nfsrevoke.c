/*-
 * Copyright (c) 2009 Rick Macklem, University of Guelph
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>

#include <netinet/in.h>

#include <nfs/nfssvc.h>

#include <fs/nfs/rpcv2.h>
#include <fs/nfs/nfsproto.h>
#include <fs/nfs/nfskpiport.h>
#include <fs/nfs/nfs.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(void);

int
main(int argc, char **argv)
{
	char *cp;
	u_char val;
	int cnt, even;
	struct nfsd_clid revoke_handle;

	if (modfind("nfsd") < 0)
		errx(1, "nfsd not loaded - self terminating");
	if (argc != 2)
		usage();
	cnt = 0;
	cp = argv[1];
	if (strlen(cp) % 2)
		even = 0;
	else
		even = 1;
	val = 0;
	while (*cp) {
		if (*cp >= '0' && *cp <= '9')
			val += (u_char)(*cp - '0');
		else if (*cp >= 'A' && *cp <= 'F')
			val += ((u_char)(*cp - 'A')) + 0xa;
		else if (*cp >= 'a' && *cp <= 'f')
			val += ((u_char)(*cp - 'a')) + 0xa;
		else
			errx(1, "Non hexadecimal digit in %s", argv[1]);
		if (even) {
			val <<= 4;
			even = 0;
		} else {
			revoke_handle.nclid_id[cnt++] = val;
			if (cnt > NFSV4_OPAQUELIMIT)
				errx(1, "Clientid %s, loo long", argv[1]);
			val = 0;
			even = 1;
		}
		cp++;
	}

	/*
	 * Do the revocation system call.
	 */
	revoke_handle.nclid_idlen = cnt;
#ifdef DEBUG
	printf("Idlen=%d\n", revoke_handle.nclid_idlen);
	for (cnt = 0; cnt < revoke_handle.nclid_idlen; cnt++)
		printf("%02x", revoke_handle.nclid_id[cnt]);
	printf("\n");
#else
	if (nfssvc(NFSSVC_ADMINREVOKE, &revoke_handle) < 0)
		err(1, "Admin revoke failed");
#endif
}

static void
usage(void)
{

	errx(1, "Usage: nfsrevoke <ClientID>");
}
