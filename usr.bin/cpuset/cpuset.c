/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007, 2008 	Jeffrey Roberson <jeff@freebsd.org>
 * All rights reserved.
 *
 * Copyright (c) 2008 Nokia Corporation
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
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

#include <sys/param.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/cpuset.h>
#include <sys/domainset.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <jail.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>

static int Cflag;
static int cflag;
static int dflag;
static int gflag;
static int iflag;
static int jflag;
static int lflag;
static int nflag;
static int pflag;
static int rflag;
static int sflag;
static int tflag;
static int xflag;
static id_t id;
static cpulevel_t level;
static cpuwhich_t which;

static void usage(void);

struct numa_policy {
	const char 	*name;
	int		policy;
};

static struct numa_policy policies[] = {
	{ "round-robin", DOMAINSET_POLICY_ROUNDROBIN },
	{ "rr", DOMAINSET_POLICY_ROUNDROBIN },
	{ "first-touch", DOMAINSET_POLICY_FIRSTTOUCH },
	{ "ft", DOMAINSET_POLICY_FIRSTTOUCH },
	{ "prefer", DOMAINSET_POLICY_PREFER },
	{ "interleave", DOMAINSET_POLICY_INTERLEAVE},
	{ "il", DOMAINSET_POLICY_INTERLEAVE},
	{ NULL, DOMAINSET_POLICY_INVALID }
};

static void printset(struct bitset *mask, int size);

static void
parselist(char *list, struct bitset *mask, int size)
{
	enum { NONE, NUM, DASH } state;
	int lastnum;
	int curnum;
	char *l;

	state = NONE;
	curnum = lastnum = 0;
	for (l = list; *l != '\0';) {
		if (isdigit(*l)) {
			curnum = atoi(l);
			if (curnum > size)
				errx(EXIT_FAILURE,
				    "List entry %d exceeds maximum of %d",
				    curnum, size);
			while (isdigit(*l))
				l++;
			switch (state) {
			case NONE:
				lastnum = curnum;
				state = NUM;
				break;
			case DASH:
				for (; lastnum <= curnum; lastnum++)
					BIT_SET(size, lastnum, mask);
				state = NONE;
				break;
			case NUM:
			default:
				goto parserr;
			}
			continue;
		}
		switch (*l) {
		case ',':
			switch (state) {
			case NONE:
				break;
			case NUM:
				BIT_SET(size, curnum, mask);
				state = NONE;
				break;
			case DASH:
				goto parserr;
				break;
			}
			break;
		case '-':
			if (state != NUM)
				goto parserr;
			state = DASH;
			break;
		default:
			goto parserr;
		}
		l++;
	}
	switch (state) {
		case NONE:
			break;
		case NUM:
			BIT_SET(size, curnum, mask);
			break;
		case DASH:
			goto parserr;
	}
	return;
parserr:
	errx(EXIT_FAILURE, "Malformed list %s", list);
}

static void
parsecpulist(char *list, cpuset_t *mask)
{

	if (strcasecmp(list, "all") == 0) {
		if (cpuset_getaffinity(CPU_LEVEL_ROOT, CPU_WHICH_PID, -1,
		    sizeof(*mask), mask) != 0)
			err(EXIT_FAILURE, "getaffinity");
		return;
	}
	parselist(list, (struct bitset *)mask, CPU_SETSIZE);
}

/*
 * permissively parse policy:domain list
 * allow:
 *	round-robin:0-4		explicit
 *	round-robin:all		explicit root domains
 *	0-4			implicit root policy
 *	round-robin		implicit root domains
 *	all			explicit root domains and implicit policy
 */
static void
parsedomainlist(char *list, domainset_t *mask, int *policyp)
{
	domainset_t rootmask;
	struct numa_policy *policy;
	char *l;
	int p;

	/*
	 * Use the rootset's policy as the default for unspecified policies.
	 */
	if (cpuset_getdomain(CPU_LEVEL_ROOT, CPU_WHICH_PID, -1,
	    sizeof(rootmask), &rootmask, &p) != 0)
		err(EXIT_FAILURE, "getdomain");

	l = list;
	for (policy = &policies[0]; policy->name != NULL; policy++) {
		if (strncasecmp(l, policy->name, strlen(policy->name)) == 0) {
			p = policy->policy;
			l += strlen(policy->name);
			if (*l != ':' && *l != '\0')
				errx(EXIT_FAILURE, "Malformed list %s", list);
			if (*l == ':')
				l++;
			break;
		}
	}
	*policyp = p;
	if (strcasecmp(l, "all") == 0 || *l == '\0') {
		DOMAINSET_COPY(&rootmask, mask);
		return;
	}
	parselist(l, (struct bitset *)mask, DOMAINSET_SETSIZE);
}

static void
printset(struct bitset *mask, int size)
{
	int once;
	int bit;

	for (once = 0, bit = 0; bit < size; bit++) {
		if (BIT_ISSET(size, bit, mask)) {
			if (once == 0) {
				printf("%d", bit);
				once = 1;
			} else
				printf(", %d", bit);
		}
	}
	printf("\n");
}

static const char *whichnames[] = { NULL, "tid", "pid", "cpuset", "irq", "jail",
				    "domain" };
static const char *levelnames[] = { NULL, " root", " cpuset", "" };
static const char *policynames[] = { "invalid", "round-robin", "first-touch",
				    "prefer", "interleave" };

static void
printaffinity(void)
{
	domainset_t domain;
	cpuset_t mask;
	int policy;

	if (cpuset_getaffinity(level, which, id, sizeof(mask), &mask) != 0)
		err(EXIT_FAILURE, "getaffinity");
	printf("%s %jd%s mask: ", whichnames[which], (intmax_t)id,
	    levelnames[level]);
	printset((struct bitset *)&mask, CPU_SETSIZE);
	if (dflag)
		goto out;
	if (cpuset_getdomain(level, which, id, sizeof(domain), &domain,
	    &policy) != 0)
		err(EXIT_FAILURE, "getdomain");
	printf("%s %jd%s domain policy: %s mask: ", whichnames[which],
	    (intmax_t)id, levelnames[level], policynames[policy]);
	printset((struct bitset *)&domain, DOMAINSET_SETSIZE);
out:
	exit(EXIT_SUCCESS);
}

static void
printsetid(void)
{
	cpusetid_t setid;

	/*
	 * Only LEVEL_WHICH && WHICH_CPUSET has a numbered id.
	 */
	if (level == CPU_LEVEL_WHICH && !sflag)
		level = CPU_LEVEL_CPUSET;
	if (cpuset_getid(level, which, id, &setid))
		err(errno, "getid");
	printf("%s %jd%s id: %d\n", whichnames[which], (intmax_t)id,
	    levelnames[level], setid);
}

int
main(int argc, char *argv[])
{
	domainset_t domains;
	cpusetid_t setid;
	cpuset_t mask;
	int policy;
	lwpid_t tid;
	pid_t pid;
	int ch;

	CPU_ZERO(&mask);
	DOMAINSET_ZERO(&domains);
	policy = DOMAINSET_POLICY_INVALID;
	level = CPU_LEVEL_WHICH;
	which = CPU_WHICH_PID;
	id = pid = tid = setid = -1;
	while ((ch = getopt(argc, argv, "Ccd:gij:l:n:p:rs:t:x:")) != -1) {
		switch (ch) {
		case 'C':
			Cflag = 1;
			break;
		case 'c':
			cflag = 1;
			level = CPU_LEVEL_CPUSET;
			break;
		case 'd':
			dflag = 1;
			which = CPU_WHICH_DOMAIN;
			id = atoi(optarg);
			break;
		case 'g':
			gflag = 1;
			break;
		case 'i':
			iflag = 1;
			break;
		case 'j':
			jflag = 1;
			which = CPU_WHICH_JAIL;
			id = jail_getid(optarg);
			if (id < 0)
				errx(EXIT_FAILURE, "%s", jail_errmsg);
			break;
		case 'l':
			lflag = 1;
			parsecpulist(optarg, &mask);
			break;
		case 'n':
			nflag = 1;
			parsedomainlist(optarg, &domains, &policy);
			break;
		case 'p':
			pflag = 1;
			which = CPU_WHICH_PID;
			id = pid = atoi(optarg);
			break;
		case 'r':
			level = CPU_LEVEL_ROOT;
			rflag = 1;
			break;
		case 's':
			sflag = 1;
			which = CPU_WHICH_CPUSET;
			id = setid = atoi(optarg);
			break;
		case 't':
			tflag = 1;
			which = CPU_WHICH_TID;
			id = tid = atoi(optarg);
			break;
		case 'x':
			xflag = 1;
			which = CPU_WHICH_IRQ;
			id = atoi(optarg);
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (gflag) {
		if (argc || Cflag || lflag || nflag)
			usage();
		/* Only one identity specifier. */
		if (dflag + jflag + xflag + sflag + pflag + tflag > 1)
			usage();
		if (iflag)
			printsetid();
		else
			printaffinity();
		exit(EXIT_SUCCESS);
	}

	if (dflag || iflag || rflag)
		usage();
	/*
	 * The user wants to run a command with a set and possibly cpumask.
	 */
	if (argc) {
		if (Cflag || pflag || tflag || xflag || jflag)
			usage();
		if (sflag) {
			if (cpuset_setid(CPU_WHICH_PID, -1, setid))
				err(argc, "setid");
		} else {
			if (cpuset(&setid))
				err(argc, "newid");
		}
		if (lflag) {
			if (cpuset_setaffinity(level, CPU_WHICH_PID,
			    -1, sizeof(mask), &mask) != 0)
				err(EXIT_FAILURE, "setaffinity");
		}
		if (nflag) {
			if (cpuset_setdomain(level, CPU_WHICH_PID,
			    -1, sizeof(domains), &domains, policy) != 0)
				err(EXIT_FAILURE, "setdomain");
		}
		errno = 0;
		execvp(*argv, argv);
		err(errno == ENOENT ? 127 : 126, "%s", *argv);
	}
	/*
	 * We're modifying something that presently exists.
	 */
	if (Cflag && (jflag || !pflag || sflag || tflag || xflag))
		usage();
	if ((!lflag && !nflag) && cflag)
		usage();
	if ((!lflag && !nflag) && !(Cflag || sflag))
		usage();
	/* You can only set a mask on a thread. */
	if (tflag && (sflag | pflag | xflag | jflag))
		usage();
	/* You can only set a mask on an irq. */
	if (xflag && (jflag | pflag | sflag | tflag))
		usage();
	if (Cflag) {
		/*
		 * Create a new cpuset and move the specified process
		 * into the set.
		 */
		if (cpuset(&setid) < 0)
			err(EXIT_FAILURE, "newid");
		sflag = 1;
	}
	if (pflag && sflag) {
		if (cpuset_setid(CPU_WHICH_PID, pid, setid))
			err(EXIT_FAILURE, "setid");
		/*
		 * If the user specifies a set and a list we want the mask
		 * to effect the pid and not the set.
		 */
		which = CPU_WHICH_PID;
		id = pid;
	}
	if (lflag) {
		if (cpuset_setaffinity(level, which, id, sizeof(mask),
		    &mask) != 0)
			err(EXIT_FAILURE, "setaffinity");
	}
	if (nflag) {
		if (cpuset_setdomain(level, which, id, sizeof(domains),
		    &domains, policy) != 0)
			err(EXIT_FAILURE, "setdomain");
	}

	exit(EXIT_SUCCESS);
}

static void
usage(void)
{

	fprintf(stderr,
	    "usage: cpuset [-l cpu-list] [-s setid] cmd ...\n");
	fprintf(stderr,
	    "       cpuset [-l cpu-list] [-s setid] -p pid\n");
	fprintf(stderr,
	    "       cpuset [-c] [-l cpu-list] -C -p pid\n");
	fprintf(stderr,
	    "       cpuset [-c] [-l cpu-list] [-j jailid | -p pid | -t tid | -s setid | -x irq]\n");
	fprintf(stderr,
	    "       cpuset -g [-cir] [-d domain | -j jailid | -p pid | -t tid | -s setid |\n"
	    "              -x irq]\n");
	exit(1);
}
