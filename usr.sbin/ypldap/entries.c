/*	$OpenBSD: entries.c,v 1.3 2015/01/16 06:40:22 deraadt Exp $ */
/*	$FreeBSD$ */
/*
 * Copyright (c) 2008 Pierre-Yves Ritschard <pyr@openbsd.org>
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
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/tree.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <unistd.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "ypldap.h"

void
flatten_entries(struct env *env)
{
	size_t		 wrlen;
	size_t		 len;
	char		*linep;
	char		*endp;
	char		*tmp;
	struct userent	*ue;
	struct groupent	*ge;

	log_debug("flattening trees");
	/*
	 * This takes all the line pointers in RB elements and
	 * concatenates them in a single string, to be able to
	 * implement next element lookup without tree traversal.
	 *
	 * An extra octet is alloced to make space for an additional NUL.
	 */
	wrlen = env->sc_user_line_len;
	if ((linep = calloc(1, env->sc_user_line_len + 1)) == NULL) {
		/*
		 * XXX: try allocating a smaller chunk of memory
		 */
		fatal("out of memory");
	}
	endp = linep;

	RB_FOREACH(ue, user_name_tree, env->sc_user_names) {
		/*
		 * we convert the first nul back to a column,
		 * copy the string and then convert it back to a nul.
		 */
		ue->ue_line[strlen(ue->ue_line)] = ':';
		log_debug("pushing line: %s", ue->ue_line);
		len = strlen(ue->ue_line) + 1;
		memcpy(endp, ue->ue_line, len);
		endp[strcspn(endp, ":")] = '\0';
		free(ue->ue_line);
		ue->ue_line = endp;
		endp += len;
		wrlen -= len;

		/*
		 * To save memory strdup(3) the netid_line which originally used
		 * LINE_WIDTH bytes
		 */
		tmp = ue->ue_netid_line;
		ue->ue_netid_line = strdup(tmp);
		if (ue->ue_netid_line == NULL) {
			fatal("out of memory");
		}
		free(tmp);
	}
	env->sc_user_lines = linep;
	log_debug("done pushing users");

	wrlen = env->sc_group_line_len;
	if ((linep = calloc(1, env->sc_group_line_len + 1)) == NULL) {
		/*
		 * XXX: try allocating a smaller chunk of memory
		 */
		fatal("out of memory");
	}
	endp = linep;
	RB_FOREACH(ge, group_name_tree, env->sc_group_names) {
		/*
		 * we convert the first nul back to a column,
		 * copy the string and then convert it back to a nul.
		 */
		ge->ge_line[strlen(ge->ge_line)] = ':';
		log_debug("pushing line: %s", ge->ge_line);
		len = strlen(ge->ge_line) + 1;
		memcpy(endp, ge->ge_line, len);
		endp[strcspn(endp, ":")] = '\0';
		free(ge->ge_line);
		ge->ge_line = endp;
		endp += len;
		wrlen -= len;
	}
	env->sc_group_lines = linep;
	log_debug("done pushing groups");
}

int
userent_name_cmp(struct userent *ue1, struct userent *ue2)
{
	return (strcmp(ue1->ue_line, ue2->ue_line));
}

int
userent_uid_cmp(struct userent *ue1, struct userent *ue2)
{
	return (ue1->ue_uid - ue2->ue_uid);
}

int
groupent_name_cmp(struct groupent *ge1, struct groupent *ge2)
{
	return (strcmp(ge1->ge_line, ge2->ge_line));
}

int
groupent_gid_cmp(struct groupent *ge1, struct groupent *ge2)
{
	return (ge1->ge_gid - ge2->ge_gid);
}

RB_GENERATE(user_name_tree, userent, ue_name_node, userent_name_cmp);
RB_GENERATE(user_uid_tree, userent, ue_uid_node, userent_uid_cmp);
RB_GENERATE(group_name_tree, groupent, ge_name_node, groupent_name_cmp);
RB_GENERATE(group_gid_tree, groupent, ge_gid_node, groupent_gid_cmp);
