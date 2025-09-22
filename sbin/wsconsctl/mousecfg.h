/* $OpenBSD: mousecfg.h,v 1.5 2023/07/02 21:44:04 bru Exp $ */

/*
 * Copyright (c) 2017 Ulf Brosziewski
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

extern struct wsmouse_parameters cfg_tapping;
extern struct wsmouse_parameters cfg_mtbuttons;
extern struct wsmouse_parameters cfg_scaling;
extern struct wsmouse_parameters cfg_edges;
extern struct wsmouse_parameters cfg_swapsides;
extern struct wsmouse_parameters cfg_disable;
extern struct wsmouse_parameters cfg_revscroll;
extern struct wsmouse_parameters cfg_param;
extern int cfg_touchpad;

int mousecfg_init(int, const char **);
int mousecfg_get_field(struct wsmouse_parameters *);
int mousecfg_put_field(int, struct wsmouse_parameters *);
void mousecfg_pr_field(struct wsmouse_parameters *);
void mousecfg_rd_field(struct wsmouse_parameters *, char *);
