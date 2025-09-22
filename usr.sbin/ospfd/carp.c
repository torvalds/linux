/*	$OpenBSD: carp.c,v 1.3 2009/09/30 12:22:03 claudio Exp $ */

/*
 * Copyright (c) 2006 Henning Brauer <henning@openbsd.org>
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
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "ospfd.h"
#include "log.h"

struct carpgroup {
	TAILQ_ENTRY(carpgroup)	 entry;
	char			*group;
	int			 do_demote;
	int			 changed_by;
};

TAILQ_HEAD(carpgroups, carpgroup)	carpgroups =
    TAILQ_HEAD_INITIALIZER(carpgroups);

struct carpgroup	*carp_group_find(char *group);
int			 carp_demote_ioctl(char *, int);

struct carpgroup *
carp_group_find(char *group)
{
	struct carpgroup	*c;

	TAILQ_FOREACH(c, &carpgroups, entry)
		if (!strcmp(c->group, group))
			return (c);

	return (NULL);
}

int
carp_demote_init(char *group, int force)
{
	struct carpgroup	*c;
	int			 level;

	if ((c = carp_group_find(group)) == NULL) {
		if ((c = calloc(1, sizeof(struct carpgroup))) == NULL) {
			log_warn("carp_demote_init calloc");
			return (-1);
		}
		if ((c->group = strdup(group)) == NULL) {
			log_warn("carp_demote_init strdup");
			free(c);
			return (-1);
		}

		/* only demote if this group already is demoted */
		if ((level = carp_demote_get(group)) == -1) {
			free(c->group);
			free(c);
			return (-1);
		}
		if (level > 0 || force)
			c->do_demote = 1;

		TAILQ_INSERT_TAIL(&carpgroups, c, entry);
	}

	return (0);
}

void
carp_demote_shutdown(void)
{
	struct carpgroup	*c;

	while ((c = TAILQ_FIRST(&carpgroups)) != NULL) {
		TAILQ_REMOVE(&carpgroups, c, entry);
		if (c->do_demote && c->changed_by > 0)
			carp_demote_ioctl(c->group, -c->changed_by);

		free(c->group);
		free(c);
	}
}

int
carp_demote_get(char *group)
{
	int			s;
	struct ifgroupreq	ifgr;

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		log_warn("carp_demote_get: socket");
		return (-1);
	}

	bzero(&ifgr, sizeof(ifgr));
	strlcpy(ifgr.ifgr_name, group, sizeof(ifgr.ifgr_name));

	if (ioctl(s, SIOCGIFGATTR, (caddr_t)&ifgr) == -1) {
		if (errno == ENOENT)
			log_warnx("group \"%s\" does not exist", group);
		else
			log_warn("carp_demote_get: ioctl");
		close(s);
		return (-1);
	}

	close(s);
	return ((int)ifgr.ifgr_attrib.ifg_carp_demoted);
}

int
carp_demote_set(char *group, int demote)
{
	struct carpgroup	*c;

	if ((c = carp_group_find(group)) == NULL) {
		log_warnx("carp_group_find for %s returned NULL?!", group);
		return (-1);
	}

	if (c->changed_by + demote < 0) {
		log_warnx("carp_demote_set: changed_by + demote < 0");
		return (-1);
	}

	if (c->do_demote && carp_demote_ioctl(group, demote) == -1)
		return (-1);

	c->changed_by += demote;

	/* enable demotion when we return to 0, i. e. all sessions up */
	if (demote < 0 && c->changed_by == 0)
		c->do_demote = 1;

	return (0);
}

int
carp_demote_ioctl(char *group, int demote)
{
	int			s, res;
	struct ifgroupreq	ifgr;

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		log_warn("carp_demote_get: socket");
		return (-1);
	}

	bzero(&ifgr, sizeof(ifgr));
	strlcpy(ifgr.ifgr_name, group, sizeof(ifgr.ifgr_name));
	ifgr.ifgr_attrib.ifg_carp_demoted = demote;

	if ((res = ioctl(s, SIOCSIFGATTR, (caddr_t)&ifgr)) == -1)
		log_warn("unable to %s the demote state "
		    "of group '%s'", (demote > 0) ? "increment" : "decrement",
		    group);
	else
		log_info("%s the demote state of group '%s'",
		    (demote > 0) ? "incremented" : "decremented", group);

	close(s);
	return (res);
}
