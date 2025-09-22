/*	$OpenBSD: iocmd-limit.c,v 1.2 2023/07/10 17:45:17 anton Exp $ */

/*
 * Copyright (c) 2023 Alexandr Nedvedicky <sashan@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <net/if.h>
#include <netinet/in.h>
#include <net/pfvar.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define REGRESS_ANCHOR "regress"

static void
usage(const char *progname)
{
	fprintf(stderr,
	    "%s -c iocmd [-i iterations ]\n"
	    "\t-c iocmd to test, currently DIOCGETRULES "
	    "and DIOCXEND are supported\n"
	    "\t-i number of iterations is 1 by default\n", progname);
	exit(1);
}

static int
do_DIOCGETRULES_test(int dev)
{
	struct pfioc_rule pr;
	int rv;

	memset(&pr, 0, sizeof(pr));
	memcpy(pr.anchor, REGRESS_ANCHOR, sizeof(REGRESS_ANCHOR));
	pr.rule.action = PF_PASS;

	if ((rv = ioctl(dev, DIOCGETRULES, &pr)) == -1) {
		/*
		 * we expect to see EBUSY anything else is odd and we should
		 * exit right away.
		 */
		if (errno != EBUSY)
			err(1, "%s DIOCGETRULES", __func__);
	}

	return (rv);
}

static int
result_DIOCGETRULES(unsigned int iterations, unsigned int limit)
{
	int	rv;
	/*
	 * DIOCGETRULES must see EBUSY before iterations reach limit
	 * to conclude test is successful.
	 */
	rv = (iterations < limit) ? 0 : 1;
	if (rv)
		printf(
		    "DIOCGETRULES could obtain %u tickets, reaching the limit "
		    "of %u tickets\n",
		    iterations, limit);

	return (rv);
}

static int
do_DIOCXEND_test(int dev)
{
	struct pfioc_rule pr;
	int rv;

	memset(&pr, 0, sizeof(pr));
	memcpy(pr.anchor, REGRESS_ANCHOR, sizeof(REGRESS_ANCHOR));
	pr.rule.action = PF_PASS;

	if ((rv = ioctl(dev, DIOCGETRULES, &pr)) == -1)
		warn("%s DIOCGETRULES", __func__);
	else if ((rv = ioctl(dev, DIOCXEND, &pr.ticket)) == -1)
		warn("%s DIOCXEND", __func__);

	return (rv);
}

static int
result_DIOCXEND(unsigned int iterations, unsigned int limit)
{
	int rv;
	/*
	 * failing to reach limit when also closing tickets
	 * using DIOXXEND is an error.
	 */
	rv = (iterations < limit) ? 1 : 0;
	if (rv)
		printf(
		    "Although test is is using DIOCXEND it still"
		    "hits limit (%u)\n", iterations);
	return (rv);
}

static struct iocmd_test {
	const char *iocmd_name;
	int (*iocmd_test)(int);
	int (*iocmd_result)(unsigned int, unsigned int);
} iocmd_test_tab[] = {
	{ "DIOCGETRULES", do_DIOCGETRULES_test, result_DIOCGETRULES },
	{ "DIOCXEND", do_DIOCXEND_test, result_DIOCXEND },
	{ NULL, NULL }
};

static struct iocmd_test *
parse_iocmd_name(const char *iocmd_name)
{
	int	i = 0;

	while (iocmd_test_tab[i].iocmd_name != NULL) {
		if (strcasecmp(iocmd_test_tab[i].iocmd_name, iocmd_name) == 0)
			break;
		i++;
	}

	return ((iocmd_test_tab[i].iocmd_name == NULL) ?
	    NULL : &iocmd_test_tab[i]);
}

int
main(int argc, char *const argv[])
{
	const char *errstr = NULL;
	unsigned int iterations = 1;
	unsigned int i = 0;
	int dev;
	int c;
	struct iocmd_test *test_iocmd = NULL;

	while ((c = getopt(argc, argv, "i:c:")) != -1) {
		switch (c) {
		case 'i':
			iterations = strtonum(optarg, 1, UINT32_MAX, &errstr);
			if (errstr != NULL) {
				fprintf(stderr,
				    "%s: number of iteration (-i %s) "
				    "is invalid: %s\n",
				    argv[0], optarg, errstr);
				usage(argv[0]);
			}
			break;
		case 'c':
			test_iocmd = parse_iocmd_name(optarg);
			if (test_iocmd == NULL) {
				fprintf(stderr, "%s invalid iocmd: %s\n",
				    argv[0], optarg);
				usage(argv[0]);
			}
			break;
		default:
			usage(argv[0]);
		}
	}

	if (test_iocmd == NULL) {
		fprintf(stderr, "%s -c option is required\n", argv[0]);
		usage(argv[0]);
	}

	dev = open("/dev/pf", O_RDONLY);
	if (dev < 0)
		err(1, "open(\"dev/pf\")");

	while (i < iterations) {
		if (test_iocmd->iocmd_test(dev) != 0)
			break;
		i++;
	}

	return (test_iocmd->iocmd_result(i, iterations));
}
