/*-
 * Copyright (c) 2012 SRI International
 * Copyright (c) 2013 Bjoern A. Zeeb
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-11-C-0249)
 * ("MRC2"), as part of the DARPA MRC research programme.
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
 * $ FreeBSD: head/usr.sbin/isfctl/isfctl.c 239685 2012-08-25 18:08:20Z brooks $
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/socket.h>

#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <inttypes.h>
#include <kenv.h>
#include <md5.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <net/if_dl.h>
#include <net/ethernet.h>


#define	CONFIG_BLOCK (128 * 1024)
#define	DEV_CFI0_PATH	"/dev/cfi0"

static u_char block[CONFIG_BLOCK];

#define	UNKNOWN	0
#define	CFI	1
static int fdev	= UNKNOWN;
static const char *fdevs[] = {
	"UNKNOWN",
	"CFI"
};
static int gflag;

/* XXX-BZ should include if_atsereg.h. */
#define	ALTERA_ETHERNET_OPTION_BITS_OFF 0x00008000
#define	ALTERA_ETHERNET_OPTION_BITS_LEN 0x00007fff


static void
usage(int rc)
{

	fprintf(stderr, "usage: atsectl [-ghlu] [-s <etheraddr>]\n");
	exit(rc);
}

static void
read_block(void)
{
	int fd;

	fd = open(DEV_CFI0_PATH, O_RDONLY, 0);
	if (fd == -1)
		errx(1, "Failed to open " DEV_CFI0_PATH);
	else
		fdev = CFI;

	if (read(fd, block, sizeof(block)) != CONFIG_BLOCK)
		errx(1, "Short read from %s", fdevs[fdev]);

	close(fd);
}

static void
write_block(void)
{
	int fd;

	assert(fdev == CFI);

	fd = open(DEV_CFI0_PATH, O_WRONLY, 0);
	if (fd == -1)
		errx(1, "Failed to open " DEV_CFI0_PATH);

	if (write(fd, block, sizeof(block)) != CONFIG_BLOCK)
		errx(1, "Short write on %s", fdevs[fdev]);

	close(fd);
}

static void
print_eaddr(void)
{
	uint32_t safe;
	
	/*
	 * XXX-BZ we are on our own: keep in sync with atse(4).
	 * Everything past the first address is a guess currently.
	 * So we will always only write one address into there.
	 */
#if 0
root@cheri1:/root # dd if=/dev/isf0 bs=32k skip=1 count=1 | hd
00000000  fe 5a 00 00 00 07 ed ff  ed 15 ff ff c0 a8 01 ea  |.Z..............|
00000010  ff ff ff ff ff ff ff 00  c0 a8 01 ff ff ff ff ff  |................|
00000020  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
*
1+0 records in
1+0 records out
32768 bytes transferred in 0.053036 secs (617845 bytes/sec)
00008000
#endif

	safe  = block[ALTERA_ETHERNET_OPTION_BITS_OFF + 0] << 24;
	safe |= block[ALTERA_ETHERNET_OPTION_BITS_OFF + 1] << 16;
	safe |= block[ALTERA_ETHERNET_OPTION_BITS_OFF + 2] << 8;
	safe |= block[ALTERA_ETHERNET_OPTION_BITS_OFF + 3];

	printf("%02x:%02x:%02x:%02x:%02x:%02x%s\n",
	    block[ALTERA_ETHERNET_OPTION_BITS_OFF + 4],
	    block[ALTERA_ETHERNET_OPTION_BITS_OFF + 5],
	    block[ALTERA_ETHERNET_OPTION_BITS_OFF + 6],
	    block[ALTERA_ETHERNET_OPTION_BITS_OFF + 7],
	    block[ALTERA_ETHERNET_OPTION_BITS_OFF + 8],
	    block[ALTERA_ETHERNET_OPTION_BITS_OFF + 9],
	    (safe != le32toh(0x00005afe)) ?
		" (invalid control pattern)" : "");
}

static void
list(void)
{

	read_block();
	print_eaddr();
	exit(0);
}

static void
_set(uint8_t *eaddr)
{
	uint8_t buf[32];
	MD5_CTX ctx;
	int rc;

	printf("Original:\n");
	read_block();
	print_eaddr();

	if (eaddr == NULL) {
		/* cfi0.factory_ppr="0x0123456789abcdef" */
		rc = kenv(KENV_GET, "cfi0.factory_ppr", buf, sizeof(buf));
		if (rc == -1)
			err(1, "Could not find Intel flash PPR serial\n");

		MD5Init(&ctx);
		MD5Update(&ctx, buf+2, 16);
		MD5Final(buf, &ctx);
		
		/* Set the device specifc address (prefix). */
		block[ALTERA_ETHERNET_OPTION_BITS_OFF + 7] =
		    buf[14] << 4 | buf[13] >> 4;
		block[ALTERA_ETHERNET_OPTION_BITS_OFF + 8] =
		    buf[13] << 4 | buf[12] >> 4;
		block[ALTERA_ETHERNET_OPTION_BITS_OFF + 9] = buf[12] << 4;
		/* Just make sure the last half-byte is really zero. */
		block[ALTERA_ETHERNET_OPTION_BITS_OFF + 9] &= ~0x0f;

		/* Set (or clear) locally administred flag. */
		if (gflag == 0)
			block[ALTERA_ETHERNET_OPTION_BITS_OFF + 4] |= 2;
		else
			block[ALTERA_ETHERNET_OPTION_BITS_OFF + 4] &= ~2;
		/* Make sure it is not a MC address by accident we start with. */
		block[ALTERA_ETHERNET_OPTION_BITS_OFF + 4] &= ~1;
	} else {
		int e;

		block[ALTERA_ETHERNET_OPTION_BITS_OFF + 4] = eaddr[0];
		block[ALTERA_ETHERNET_OPTION_BITS_OFF + 5] = eaddr[1];
		block[ALTERA_ETHERNET_OPTION_BITS_OFF + 6] = eaddr[2];
		block[ALTERA_ETHERNET_OPTION_BITS_OFF + 7] = eaddr[3];
		block[ALTERA_ETHERNET_OPTION_BITS_OFF + 8] = eaddr[4];
		block[ALTERA_ETHERNET_OPTION_BITS_OFF + 9] = eaddr[5];

		e = 0;
		if ((eaddr[5] & 0xf) != 0x0) {
			e++;
			warnx("WARN: Selected Ethernet Address is "
			    "not multi-MAC compatible.\n");
		}
		if (gflag == 0 && ((eaddr[0] & 0x2) == 0x0)) {
			e++;
			warnx("WARN: Locally administered bit not set.\n");
		}
		if ((eaddr[0] & 0x1) != 0x0) {
			e++;
			warnx("WARN: You are setting a Multicast address.\n");
		}
		if (e != 0)
			warnx("Suggesting to re-run with: "
			    "%02x:%02x:%02x:%02x:%02x:%02x",
			    (eaddr[0] & 0xfe) | 0x2,
			    eaddr[1], eaddr[2], eaddr[3], eaddr[4],
			    eaddr[5] & 0xf0);
	}

	/* Write the "safe" out, just to be sure. */
	block[ALTERA_ETHERNET_OPTION_BITS_OFF + 0] = 0xfe;
	block[ALTERA_ETHERNET_OPTION_BITS_OFF + 1] = 0x5a;
	block[ALTERA_ETHERNET_OPTION_BITS_OFF + 2] = 0x00;
	block[ALTERA_ETHERNET_OPTION_BITS_OFF + 3] = 0x00;

	write_block();

	printf("Updated to:\n");
	read_block();
	print_eaddr();
	exit(0);
}

static void
update(void)
{

	_set(NULL);
	exit(0);
}

static void
set(char *eaddrstr)
{
	uint8_t eaddr[ETHER_ADDR_LEN];
	char *p;
	long l;
	int i;

	memset(eaddr, 0x00, ETHER_ADDR_LEN);
	i = 0;
	while ((p = strsep(&eaddrstr, ":")) != NULL && i < ETHER_ADDR_LEN) {
		errno = 0;
		l = strtol(p, (char **)NULL, 16);
		if (l == 0 && errno != 0)
			errx(1, "Failed to parse Ethernet address given: %s\n", p);
		if (l < 0x00 || l > 0xff)
			errx(1, "Failed to parse Ethernet address given: %lx\n", l);
		eaddr[i++] = strtol(p, (char **)NULL, 16);
	}

	if (i != ETHER_ADDR_LEN)
		errx(1, "Failed to parse Ethernet address given\n");

	_set(eaddr);
	exit(0);
}

int
main(int argc, char **argv)
{
	char ch, *s;

	s = NULL;
	while ((ch = getopt(argc, argv, "ghlus:")) != -1) {
		switch (ch) {
		case 'g':
			gflag = 1;
			break;
		case 'h':
			usage(0);
			/* NOTREACHED */
			break;
		case 'l':
			list();
			/* NOTREACHED */
			break;
		case 'u':
			update();
			/* NOTREACHED */
			break;

		case 's':
			set(optarg);
			/* NOTREACHED */
			break;

		case '?':
		default:
			usage(1);
			/* NOTREACHED */
			break;
		}
	}

	usage(1);
	/* NOTREACHED */

	return (0);
}
