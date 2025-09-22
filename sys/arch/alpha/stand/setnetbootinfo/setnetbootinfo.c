/*	$OpenBSD: setnetbootinfo.c,v 1.5 2021/10/24 21:24:22 deraadt Exp $	*/
/*	$NetBSD: setnetbootinfo.c,v 1.5 1997/04/06 08:41:37 cgd Exp $	*/

/*
 * Copyright (c) 1997 Christopher G. Demetriou
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>						/* XXX */
#include <net/if.h>						/* XXX */
#include <netinet/in.h>
#include <netinet/if_ether.h>

#include "bbinfo.h"

int	verbose, force, unset;
char	*netboot, *outfile, *addr, *host;

char	*outfilename;

struct ether_addr *ether_addr, _ether_addr;

static void
usage()
{

	(void)fprintf(stderr, "usage:\n");
	(void)fprintf(stderr, "\tsetnetboot [-v] [-f] [-o outfile] \\\n");
	(void)fprintf(stderr, "\t    [-a ether-address | -h ether-host] infile\n");
	(void)fprintf(stderr, "\tsetnetboot [-v] -u -o outfile infile\n");
	exit(1);
}

int
main(argc, argv)
	int argc;
	char *argv[];
{
	struct netbbinfo *netbbinfop;
	struct stat sb;
	u_int64_t *qp, csum;
	char *netbb;
	int c, fd, i;

	while ((c = getopt(argc, argv, "a:fh:o:uv")) != -1) {
		switch (c) {
		case 'a':
			/* use the argument as an ethernet address */
			addr = optarg;
			break;
		case 'f':
			/* set force flag in network boot block */
			force = 1;
			break;
		case 'h':
			/* use the argument as a host to find in /etc/ethers */
			host = optarg;
			break;
		case 'o':
			/* use the argument as the output file name */
			outfile = optarg;
			break;
		case 'u':
			/* remove configuration information */
			unset = 1;
			break;
		case 'v':
			/* Chat */
			verbose = 1;
			break;
		default:
			usage();
		}
	}

	if ((argc - optind) != 1)
		usage();
	netboot = argv[optind];

	if (unset && (force || host != NULL || addr != NULL))
		errx(1, "-u can't be used with -f, -h, or -a");

	if (unset) {
		if (force || host != NULL || addr != NULL)
			errx(1, "-u can't be used with -f, -h, or -a");
		if (outfile == NULL)
			errx(1, "-u cannot be used without -o");
	} else {
		if ((host == NULL && addr == NULL) ||
		    (host != NULL && addr != NULL))
			usage();

		if (host != NULL) {
			if (ether_hostton(host, &_ether_addr) == -1)
				errx(1, "ethernet address couldn't be found for \"%s\"",
				    host);
			ether_addr = &_ether_addr;
		} else { /* addr != NULL */
			ether_addr = ether_aton(addr);
			if (ether_addr == NULL)
				errx(1, "ethernet address \"%s\" is invalid",
				    addr);
		}
	}

	if (outfile != NULL)
		outfilename = outfile;
	else {
		/* name + 12 for enet addr + '.' before enet addr + NUL */
		size_t len = strlen(netboot) + 14;

		outfilename = malloc(len);
		if (outfilename == NULL)
			err(1, "malloc of output file name failed");
		snprintf(outfilename, len,
		    "%s.%02x%02x%02x%02x%02x%02x", netboot,
		    ether_addr->ether_addr_octet[0],
		    ether_addr->ether_addr_octet[1],
		    ether_addr->ether_addr_octet[2],
		    ether_addr->ether_addr_octet[3],
		    ether_addr->ether_addr_octet[4],
		    ether_addr->ether_addr_octet[5]);
	}

	if (verbose) {
		printf("netboot: %s\n", netboot);
		if (unset)
			printf("unsetting configuration\n");
		else
			printf("ethernet address: %s (%s), force = %d\n",
			    ether_ntoa(ether_addr), host ? host : addr, force);
		printf("output netboot: %s\n", outfilename);
	}


	if (verbose)
		printf("opening %s...\n", netboot);
	if ((fd = open(netboot, O_RDONLY)) == -1)
		err(1, "open: %s", netboot);
	if (fstat(fd, &sb) == -1)
		err(1, "fstat: %s", netboot);
	if (!S_ISREG(sb.st_mode))
		errx(1, "%s must be a regular file", netboot);

	if (verbose)
		printf("reading %s...\n", netboot);
	netbb = malloc(sb.st_size);
	if (netbb == NULL)
		err(1, "malloc of %lu for %s failed",
		    (unsigned long)sb.st_size, netboot);
	if (read(fd, netbb, sb.st_size) != sb.st_size)
		err(1, "read of %lu from %s failed",
		    (unsigned long)sb.st_size, netboot);
	
	if (verbose)
		printf("closing %s...\n", netboot);
	close(fd);

	if (verbose)
		printf("looking for netbbinfo...\n");
	netbbinfop = NULL;
	for (qp = (u_int64_t *)netbb; qp < (u_int64_t *)(netbb + sb.st_size);
	    qp++) {
		if (((struct netbbinfo *)qp)->magic1 == 0xfeedbabedeadbeef &&
		    ((struct netbbinfo *)qp)->magic2 == 0xfeedbeefdeadbabe) {
			netbbinfop = (struct netbbinfo *)qp;
			break;
		}
	}
	if (netbbinfop == NULL)
		errx(1, "netboot information structure not found in %s",
		    netboot);
	if (verbose)
		printf("found netbbinfo structure at offset 0x%lx.\n",
		    (unsigned long)((char *)netbbinfop - netbb));

	if (verbose)
		printf("setting netbbinfo structure...\n");
	bzero(netbbinfop, sizeof *netbbinfop);
	netbbinfop->magic1 = 0xfeedbabedeadbeef;
	netbbinfop->magic2 = 0xfeedbeefdeadbabe;
	netbbinfop->set = unset ? 0 : 1;
	if (netbbinfop->set) {
		for (i = 0; i < 6; i++)
			netbbinfop->ether_addr[i] =
			    ether_addr->ether_addr_octet[i];
		netbbinfop->force = force;
	}
	netbbinfop->cksum = 0;

	if (verbose)
		printf("setting netbbinfo checksum...\n");
	csum = 0;
	for (i = 0, qp = (u_int64_t *)netbbinfop;
	    i < (sizeof *netbbinfop / sizeof (u_int64_t)); i++, qp++)
		csum += *qp;
	netbbinfop->cksum = -csum;

	if (verbose)
		printf("opening %s...\n", outfilename);
	if ((fd = open(outfilename, O_WRONLY | O_CREAT, 0666)) == -1)
		err(1, "open: %s", outfilename);

	if (verbose)
		printf("writing %s...\n", outfilename);
	if (write(fd, netbb, sb.st_size) != sb.st_size)
		err(1, "write of %lu to %s failed",
		    (unsigned long)sb.st_size, outfilename);

	if (verbose)
		printf("closing %s...\n", outfilename);
	close(fd);

	free(netbb);
	if (outfile == NULL)
		free(outfilename);

	exit (0);
}
