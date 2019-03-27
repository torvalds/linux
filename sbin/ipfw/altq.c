/*-
 * Copyright (c) 2002-2003 Luigi Rizzo
 * Copyright (c) 1996 Alex Nash, Paul Traina, Poul-Henning Kamp
 * Copyright (c) 1994 Ugen J.S.Antsilevich
 *
 * Idea and grammar partially left from:
 * Copyright (c) 1993 Daniel Boulet
 *
 * Redistribution and use in source forms, with and without modification,
 * are permitted provided that this entire comment appears intact.
 *
 * Redistribution in binary form may occur without any restrictions.
 * Obviously, it would be nice if you gave credit where credit is due
 * but requiring it would be too onerous.
 *
 * This software is provided ``AS IS'' without any warranties of any kind.
 *
 * NEW command line interface for IP firewall facility
 *
 * $FreeBSD$
 *
 * altq interface
 */

#define PFIOC_USE_LATEST

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include "ipfw2.h"

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <fcntl.h>

#include <net/if.h>		/* IFNAMSIZ */
#include <net/pfvar.h>
#include <netinet/in.h>	/* in_addr */
#include <netinet/ip_fw.h>

/*
 * Map between current altq queue id numbers and names.
 */
static TAILQ_HEAD(, pf_altq) altq_entries =
	TAILQ_HEAD_INITIALIZER(altq_entries);

void
altq_set_enabled(int enabled)
{
	int pffd;

	pffd = open("/dev/pf", O_RDWR);
	if (pffd == -1)
		err(EX_UNAVAILABLE,
		    "altq support opening pf(4) control device");
	if (enabled) {
		if (ioctl(pffd, DIOCSTARTALTQ) != 0 && errno != EEXIST)
			err(EX_UNAVAILABLE, "enabling altq");
	} else {
		if (ioctl(pffd, DIOCSTOPALTQ) != 0 && errno != ENOENT)
			err(EX_UNAVAILABLE, "disabling altq");
	}
	close(pffd);
}

static void
altq_fetch(void)
{
	struct pfioc_altq pfioc;
	struct pf_altq *altq;
	int pffd;
	unsigned int mnr;
	static int altq_fetched = 0;

	if (altq_fetched)
		return;
	altq_fetched = 1;
	pffd = open("/dev/pf", O_RDONLY);
	if (pffd == -1) {
		warn("altq support opening pf(4) control device");
		return;
	}
	bzero(&pfioc, sizeof(pfioc));
	pfioc.version = PFIOC_ALTQ_VERSION;
	if (ioctl(pffd, DIOCGETALTQS, &pfioc) != 0) {
		warn("altq support getting queue list");
		close(pffd);
		return;
	}
	mnr = pfioc.nr;
	for (pfioc.nr = 0; pfioc.nr < mnr; pfioc.nr++) {
		if (ioctl(pffd, DIOCGETALTQ, &pfioc) != 0) {
			if (errno == EBUSY)
				break;
			warn("altq support getting queue list");
			close(pffd);
			return;
		}
		if (pfioc.altq.qid == 0)
			continue;
		altq = safe_calloc(1, sizeof(*altq));
		*altq = pfioc.altq;
		TAILQ_INSERT_TAIL(&altq_entries, altq, entries);
	}
	close(pffd);
}

u_int32_t
altq_name_to_qid(const char *name)
{
	struct pf_altq *altq;

	altq_fetch();
	TAILQ_FOREACH(altq, &altq_entries, entries)
		if (strcmp(name, altq->qname) == 0)
			break;
	if (altq == NULL)
		errx(EX_DATAERR, "altq has no queue named `%s'", name);
	return altq->qid;
}

static const char *
altq_qid_to_name(u_int32_t qid)
{
	struct pf_altq *altq;

	altq_fetch();
	TAILQ_FOREACH(altq, &altq_entries, entries)
		if (qid == altq->qid)
			break;
	if (altq == NULL)
		return NULL;
	return altq->qname;
}

void
print_altq_cmd(struct buf_pr *bp, ipfw_insn_altq *altqptr)
{
	if (altqptr) {
		const char *qname;

		qname = altq_qid_to_name(altqptr->qid);
		if (qname == NULL)
			bprintf(bp, " altq ?<%u>", altqptr->qid);
		else
			bprintf(bp, " altq %s", qname);
	}
}
