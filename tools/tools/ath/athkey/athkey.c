/*-
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */

#include "diag.h"

#include "ah.h"
#include "ah_internal.h"

#include <string.h>
#include <stdlib.h>
#include <err.h>
#include <ctype.h>
#include <getopt.h>

const char *progname;

static int
toint(int c)
{
	return isdigit(c) ? c - '0' : isupper(c) ? c - 'A' + 10 : c - 'a' + 10;
}

static int
getdata(const char *arg, u_int8_t *data, size_t maxlen)
{
	const char *cp = arg;
	int len;

	if (cp[0] == '0' && (cp[1] == 'x' || cp[1] == 'X'))
		cp += 2;
	len = 0;
	while (*cp) {
		int b0, b1;
		if (cp[0] == ':' || cp[0] == '-' || cp[0] == '.') {
			cp++;
			continue;
		}
		if (!isxdigit(cp[0])) {
			fprintf(stderr, "%s: invalid data value %c (not hex)\n",
				progname, cp[0]);
			exit(-1);
		}
		b0 = toint(cp[0]);
		if (cp[1] != '\0') {
			if (!isxdigit(cp[1])) {
				fprintf(stderr, "%s: invalid data value %c "
					"(not hex)\n", progname, cp[1]);
				exit(-1);
			}
			b1 = toint(cp[1]);
			cp += 2;
		} else {			/* fake up 0<n> */
			b1 = b0, b0 = 0;
			cp += 1;
		}
		if (len > maxlen) {
			fprintf(stderr,
				"%s: too much data in %s, max %llu bytes\n",
				progname, arg, (unsigned long long) maxlen);
		}
		data[len++] = (b0<<4) | b1;
	}
	return len;
}

/* XXX this assumes 5212 key types are common to 5211 and 5210 */

static int
getcipher(const char *name)
{
#define	streq(a,b)	(strcasecmp(a,b) == 0)

	if (streq(name, "wep"))
		return HAL_CIPHER_WEP;
	if (streq(name, "tkip"))
		return HAL_CIPHER_TKIP;
	if (streq(name, "aes-ocb") || streq(name, "ocb"))
		return HAL_CIPHER_AES_OCB;
	if (streq(name, "aes-ccm") || streq(name, "ccm") ||
	    streq(name, "aes"))
		return HAL_CIPHER_AES_CCM;
	if (streq(name, "ckip"))
		return HAL_CIPHER_CKIP;
	if (streq(name, "none") || streq(name, "clr"))
		return HAL_CIPHER_CLR;

	fprintf(stderr, "%s: unknown cipher %s\n", progname, name);
	exit(-1);
#undef streq
}

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-i device] keyix cipher keyval [mac]\n",
		progname);
	exit(-1);
}

int
main(int argc, char *argv[])
{
	const char *ifname;
	struct ath_diag atd;
	HAL_DIAG_KEYVAL setkey;
	const char *cp;
	int s, c;
	u_int16_t keyix;
	int op = HAL_DIAG_SETKEY;
	int xor = 0;

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		err(1, "socket");
	ifname = getenv("ATH");
	if (!ifname)
		ifname = ATH_DEFAULT;

	progname = argv[0];
	while ((c = getopt(argc, argv, "di:x")) != -1)
		switch (c) {
		case 'd':
			op = HAL_DIAG_RESETKEY;
			break;
		case 'i':
			ifname = optarg;
			break;
		case 'x':
			xor = 1;
			break;
		default:
			usage();
			/*NOTREACHED*/
		}
	argc -= optind;
	argv += optind;
	if (argc < 1)
		usage();

	keyix = (u_int16_t) atoi(argv[0]);
	if (keyix > 127)
		errx(-1, "%s: invalid key index %s, must be [0..127]",
			progname, argv[0]);
	strncpy(atd.ad_name, ifname, sizeof (atd.ad_name));
	atd.ad_id = op | ATH_DIAG_IN | ATH_DIAG_DYN;
	atd.ad_out_data = NULL;
	atd.ad_out_size = 0;
	switch (op) {
	case HAL_DIAG_RESETKEY:
		atd.ad_in_data = (caddr_t) &keyix;
		atd.ad_in_size = sizeof(u_int16_t);
		if (ioctl(s, SIOCGATHDIAG, &atd) < 0)
			err(1, "ioctl: %s", atd.ad_name);
		return 0;
	case HAL_DIAG_SETKEY:
		if (argc != 3 && argc != 4)
			usage();
		memset(&setkey, 0, sizeof(setkey));
		setkey.dk_keyix = keyix;
		setkey.dk_xor = xor;
		setkey.dk_keyval.kv_type = getcipher(argv[1]);
		setkey.dk_keyval.kv_len = getdata(argv[2],
		    setkey.dk_keyval.kv_val, sizeof(setkey.dk_keyval.kv_val));
		/* XXX MIC */
		if (argc == 4)
			(void) getdata(argv[3], setkey.dk_mac,
				IEEE80211_ADDR_LEN);
		atd.ad_in_data = (caddr_t) &setkey;
		atd.ad_in_size = sizeof(setkey);
		if (ioctl(s, SIOCGATHDIAG, &atd) < 0)
			err(1, "ioctl: %s", atd.ad_name);
		return 0;
	}
	return -1;
}
