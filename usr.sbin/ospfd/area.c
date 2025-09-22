/*	$OpenBSD: area.c,v 1.10 2015/11/22 13:09:10 claudio Exp $ */

/*
 * Copyright (c) 2004, 2005 Esben Norby <norby@openbsd.org>
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
#include <sys/tree.h>
#include <err.h>
#include <stdlib.h>

#include "ospf.h"
#include "ospfd.h"
#include "ospfe.h"
#include "rde.h"
#include "log.h"

struct area *
area_new(void)
{
	struct area *area = NULL;

	if ((area = calloc(1, sizeof(*area))) == NULL)
		errx(1, "area_new: calloc");

	LIST_INIT(&area->iface_list);
	LIST_INIT(&area->nbr_list);
	RB_INIT(&area->lsa_tree);
	SIMPLEQ_INIT(&area->redist_list);

	return (area);
}

int
area_del(struct area *area)
{
	struct iface	*iface = NULL;
	struct vertex	*v, *nv;
	struct rde_nbr	*n;
	struct redistribute *r;

	/* area is removed so neutralize the demotion done by the area */
	if (area->active == 0)
		ospfe_demote_area(area, 1);

	/* clean lists */
	while ((iface = LIST_FIRST(&area->iface_list)) != NULL) {
		LIST_REMOVE(iface, entry);
		if_del(iface);
	}

	while ((n = LIST_FIRST(&area->nbr_list)) != NULL)
		rde_nbr_del(n);

	for (v = RB_MIN(lsa_tree, &area->lsa_tree); v != NULL; v = nv) {
		nv = RB_NEXT(lsa_tree, &area->lsa_tree, v);
		vertex_free(v);
	}

	while ((r = SIMPLEQ_FIRST(&area->redist_list)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&area->redist_list, entry);
		free(r);
	}

	free(area);

	return (0);
}

struct area *
area_find(struct ospfd_conf *conf, struct in_addr area_id)
{
	struct area	*area;

	LIST_FOREACH(area, &conf->area_list, entry) {
		if (area->id.s_addr == area_id.s_addr) {
			return (area);
		}
	}

	return (NULL);
}

void
area_track(struct area *area)
{
	int		old = area->active;
	struct iface	*iface;

	area->active = 0;
	LIST_FOREACH(iface, &area->iface_list, entry) {
		if (iface->state & IF_STA_DOWN)
			continue;
		area->active = 1;
		break;
	}

	if (area->active != old) {
		ospfe_imsg_compose_rde(IMSG_AREA_CHANGE, area->id.s_addr, 0,
		    &area->active, sizeof(area->active));
		ospfe_demote_area(area, old == 0);
	}
}

int
area_border_router(struct ospfd_conf *conf)
{
	struct area	*area;
	int		 active = 0;

	LIST_FOREACH(area, &conf->area_list, entry)
		if (area->active)
			active++;

	return (active > 1);
}

u_int8_t
area_ospf_options(struct area *area)
{
	u_int8_t	opt = 0;

	if (area && !area->stub)
		opt |= OSPF_OPTION_E;

	return (opt);
}
