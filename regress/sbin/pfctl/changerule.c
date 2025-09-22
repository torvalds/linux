/* $OpenBSD: changerule.c,v 1.2 2024/08/23 12:56:26 anton Exp $ */
/*
 * Copyright (c) 2021 Alexandr Nedvedicky <sashan@openbsd.org>
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

/*
 * changerule - simple tool to test DIOCCHANGERULE functionality (see pf(4))
 * Tool reads firewall rules from stdin only. If more rules are passed, then
 * only the first one is being used. Examples:
 *	echo 'pass all' | changerule -a test -i 0
 *		inserts a rule to the first position in ruleset test
 *
 *	echo 'pass all' | changerule -a test -i -1
 *		inserts a rule to the last position in ruleset test
 *
 *	echo 'pass all' | changerule -a test -i 3
 *		inserts a rule before existing No. 3 rule (rules numbering
 *		starts with 0) in ruleset test
 *
 *	echo 'pass all' | changerule -a test -I 3
 *		inserts a rule after existing No. 3 rule (rules numbering
 *		starts with 0) in ruleset test
 *
 *	changerule -a test -r 3
 *		removes existing No. 3 rule from ruleset test
 *
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <net/if.h>
#include <netinet/in.h>
#include <net/pfvar.h>
#include <arpa/inet.h>
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <stdarg.h>
#include <libgen.h>

#include "pfctl_parser.h"
#include "pfctl.h"

void	changerule_usage(void);
int	do_chng_cmd(char *, int, int);

extern int dev;
extern char *anchoropt;
extern char *pf_device;

__dead void
changerule_usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s", __progname);
	fprintf(stderr, "[-a anchor] [ -i ruleNo ] [ -I ruleNo ]\n");
	exit(1);
}

int
do_chng_cmd(char *anchorname, int cmd, int rule_no)
{
	struct pfctl		pf;
	struct pf_anchor	rs_anchor;
	struct pf_ruleset	*rs = &rs_anchor.ruleset;
	struct pfioc_rule	pcr;

	memset(&pf, 0, sizeof(pf));
	memset(&rs_anchor, 0, sizeof(rs_anchor));
	pf.anchor = &rs_anchor;
	pf_init_ruleset(rs);

	if (strlcpy(pf.anchor->path, anchorname,
	    sizeof(pf.anchor->path)) >= sizeof (pf.anchor->path))
		errx(1, "%s: strlcpy", __func__);

	pf.astack[0] = pf.anchor;
	pf.asd = 0;
	pf.dev = dev;

	memset(&pcr, 0, sizeof(pcr));
	strlcpy(pcr.anchor, anchorname, sizeof(pcr.anchor));
	pcr.action = PF_CHANGE_GET_TICKET;
	if (ioctl(dev, DIOCCHANGERULE, &pcr) < 0)
		errx(1, "ioctl(ticket) @ %s", __func__);

	pcr.action = cmd;
	pcr.nr = rule_no;
	if (cmd != PF_CHANGE_REMOVE) {
		if (parse_config("-", &pf) < 0) {
			errx(1, "Syntax error in rule");
			return (1);
		}

		if (TAILQ_FIRST(rs->rules.active.ptr) != NULL)
			memcpy(&pcr.rule, TAILQ_FIRST(rs->rules.active.ptr),
			    sizeof(pcr.rule));
		else
			errx(1, "no rule");
	}

	if (ioctl(dev, DIOCCHANGERULE, &pcr) < 0) {
		errx(1, "ioctl(commit) @ %s", __func__);
	}

	return (0);
}

int
main(int argc, char *argv[])
{
	char	 anchorname[PATH_MAX];
	const char *errstr;
	int ch;
	int rule_no;
	int chng_cmd;
	int after = 0;

	if (argc < 2)
		changerule_usage();

	while ((ch = getopt(argc, argv, "a:i:I:r:")) != -1) {
		switch (ch) {
		case 'a':
			anchoropt = optarg;
			break;
		case 'I':
			after = 1;
			/* FALLTHROUGH */
		case 'i':
			rule_no = strtonum(optarg, -1, 0x7fffffff, &errstr);
			if (errstr != NULL) {
				warnx("Rule number outside of range <%d, %d\n",
				    -1, 0x7fffffff);
				exit(1);
			}
			switch (rule_no) {
			case 0:
				chng_cmd = PF_CHANGE_ADD_HEAD;
				break;
			case -1:
				chng_cmd = PF_CHANGE_ADD_TAIL;
				break;
			default:
				if (after)
					chng_cmd = PF_CHANGE_ADD_AFTER;
				else
					chng_cmd = PF_CHANGE_ADD_BEFORE;
			}
			break;
		case 'r':
			rule_no = strtonum(optarg, -1, 0x7fffffff, &errstr);
			if (errstr != NULL) {
				warnx("Rule number outside of range <%d, %d\n",
				    -1, 0x7fffffff);
				exit(1);
			}
			chng_cmd = PF_CHANGE_REMOVE;
			break;
		default:
			changerule_usage();
			/* NOTREACHED */
		}
	}

	if (argc != optind) {
		warnx("unknown command line argument: %s ...", argv[optind]);
		changerule_usage();
		/* NOTREACHED */
	}

	memset(anchorname, 0, sizeof(anchorname));
	if (anchoropt != NULL) {
		if (anchoropt[0] == '\0')
			errx(1, "anchor name must not be empty");

		if (anchoropt[0] == '_' || strstr(anchoropt, "/_") != NULL)
			errx(1, "anchor names beginning with '_' cannot "
			    "be modified from the command line");
		int len = strlen(anchoropt);

		if (anchoropt[len - 1] == '*') {
			warnx("wildcard anchors not supported");
			changerule_usage();
		}
		if (strlcpy(anchorname, anchoropt,
		    sizeof(anchorname)) >= sizeof(anchorname))
			errx(1, "anchor name '%s' too long",
			    anchoropt);
	}

	dev = open(pf_device, O_RDWR);
	if (dev == -1)
		err(1, "/dev/pf");

	return (do_chng_cmd(anchoropt, chng_cmd, rule_no));
}
