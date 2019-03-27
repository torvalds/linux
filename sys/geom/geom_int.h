/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2002 Poul-Henning Kamp
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Poul-Henning Kamp
 * and NAI Labs, the Security Research Division of Network Associates, Inc.
 * under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the
 * DARPA CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

LIST_HEAD(class_list_head, g_class);
TAILQ_HEAD(g_tailq_head, g_geom);

extern int g_collectstats;
#define G_STATS_PROVIDERS	1	/* Collect I/O stats for providers */
#define G_STATS_CONSUMERS	2	/* Collect I/O stats for consumers */

extern int g_debugflags;
/*
 * 1	G_T_TOPOLOGY
 * 2	G_T_BIO
 * 4	G_T_ACCESS
 * 8	(unused)
 * 16	Allow footshooting on rank#1 providers
 * 32	G_T_DETAILS
 */
#define G_F_DISKIOCTL	64
#define G_F_CTLDUMP	128

/* geom_dump.c */
void g_confxml(void *, int flag);
void g_conf_specific(struct sbuf *sb, struct g_class *mp, struct g_geom *gp, struct g_provider *pp, struct g_consumer *cp);
void g_conf_printf_escaped(struct sbuf *sb, const char *fmt, ...);
void g_confdot(void *, int flag);
void g_conftxt(void *, int flag);

/* geom_event.c */
void g_event_init(void);
void g_run_events(void);
void g_do_wither(void);

/* geom_subr.c */
extern struct class_list_head g_classes;
extern char *g_wait_event, *g_wait_sim, *g_wait_up, *g_wait_down;
void g_wither_washer(void);

/* geom_io.c */
void g_io_init(void);
void g_io_schedule_down(struct thread *tp);
void g_io_schedule_up(struct thread *tp);

/* geom_kern.c / geom_kernsim.c */
void g_init(void);
extern int g_shutdown;
extern int g_notaste;

/* geom_ctl.c */
void g_ctl_init(void);
