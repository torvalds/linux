/*	$OpenBSD: ofw_pinctrl.c,v 1.3 2020/06/06 16:59:43 patrick Exp $	*/
/*
 * Copyright (c) 2016 Mark Kettenis
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
#include <sys/systm.h>
#include <sys/malloc.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_pinctrl.h>

struct pinctrl {
	int	pc_phandle;
	int	(*pc_pinctrl)(uint32_t, void *);
	void	*pc_cookie;

	LIST_ENTRY(pinctrl) pc_list;
};

void pinctrl_register_child(int, int (*)(uint32_t, void *), void *);

LIST_HEAD(, pinctrl) pinctrls = LIST_HEAD_INITIALIZER(pinctrl);

void
pinctrl_register(int node, int (*pinctrl)(uint32_t, void *), void *cookie)
{
	for (node = OF_child(node); node; node = OF_peer(node))
		pinctrl_register_child(node, pinctrl, cookie);
}

void
pinctrl_register_child(int node, int (*pinctrl)(uint32_t, void *), void *cookie)
{
	struct pinctrl *pc;
	uint32_t phandle;

	phandle = OF_getpropint(node, "phandle", 0);
	if (phandle) {
		pc = malloc(sizeof(struct pinctrl), M_DEVBUF, M_WAITOK);
		pc->pc_phandle = phandle;
		pc->pc_pinctrl = pinctrl;
		pc->pc_cookie = cookie;
		LIST_INSERT_HEAD(&pinctrls, pc, pc_list);
	}

	for (node = OF_child(node); node; node = OF_peer(node))
		pinctrl_register_child(node, pinctrl, cookie);
}

int
pinctrl_byphandle(uint32_t phandle)
{
	struct pinctrl *pc;

	if (phandle == 0)
		return -1;

	LIST_FOREACH(pc, &pinctrls, pc_list) {
		if (pc->pc_phandle == phandle)
			return pc->pc_pinctrl(pc->pc_phandle, pc->pc_cookie);
	}

	return -1;
}

int
pinctrl_byid(int node, int id)
{
	char pinctrl[32];
	uint32_t *phandles;
	int len, i;

	snprintf(pinctrl, sizeof(pinctrl), "pinctrl-%d", id);
	len = OF_getproplen(node, pinctrl);
	if (len <= 0)
		return -1;

	phandles = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(node, pinctrl, phandles, len);
	for (i = 0; i < len / sizeof(uint32_t); i++)
		pinctrl_byphandle(phandles[i]);
	free(phandles, M_TEMP, len);
	return 0;
}

int
pinctrl_byname(int node, const char *config)
{
	int id;

	id = OF_getindex(node, config, "pinctrl-names");
	if (id < 0)
		return -1;

	return pinctrl_byid(node, id);
}
