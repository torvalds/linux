/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Poul-Henning Kamp
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
#ifndef _LIBGEOM_H_
#define _LIBGEOM_H_

#include <sys/cdefs.h>

#include <sys/queue.h>
#include <sys/time.h>

#include <geom/geom_ctl.h>

__BEGIN_DECLS

#ifndef DEBUG_LIBGEOM
#define DEBUG_LIBGEOM 0
#endif

void geom_stats_close(void);
void geom_stats_resync(void);
int geom_stats_open(void);
void *geom_stats_snapshot_get(void);
void geom_stats_snapshot_free(void *);
void geom_stats_snapshot_timestamp(void *, struct timespec *);
void geom_stats_snapshot_reset(void *);
struct devstat *geom_stats_snapshot_next(void *);

char *geom_getxml(void);

/* geom_xml2tree.c */

/*
 * These structs are used to build the tree based on the XML.
 * they're named as the kernel variant without the first '_'.
 */

struct gclass;
struct ggeom;
struct gconsumer;
struct gprovider;

LIST_HEAD(gconf, gconfig);

struct gident {
	void			*lg_id;
	void			*lg_ptr;
	enum {	ISCLASS,
		ISGEOM,
		ISPROVIDER,
		ISCONSUMER }	lg_what;
};

struct gmesh {
	LIST_HEAD(, gclass)	lg_class;
	struct gident		*lg_ident;
};

struct gconfig {
	LIST_ENTRY(gconfig)	lg_config;
	char			*lg_name;
	char			*lg_val;
};

struct gclass {
	void			*lg_id;
	char			*lg_name;
	LIST_ENTRY(gclass)	lg_class;
	LIST_HEAD(, ggeom)	lg_geom;
	struct gconf		lg_config;
};

struct ggeom {
	void			*lg_id;
	struct gclass		*lg_class;
	char			*lg_name;
	u_int			lg_rank;
	LIST_ENTRY(ggeom)	lg_geom;
	LIST_HEAD(, gconsumer)	lg_consumer;
	LIST_HEAD(, gprovider)	lg_provider;
	struct gconf		lg_config;
};

struct gconsumer {
	void			*lg_id;
	struct ggeom		*lg_geom;
	LIST_ENTRY(gconsumer)	lg_consumer;
	struct gprovider	*lg_provider;
	LIST_ENTRY(gconsumer)	lg_consumers;
	char			*lg_mode;
	struct gconf		lg_config;
};

struct gprovider {
	void			*lg_id;
	char			*lg_name;
	struct ggeom		*lg_geom;
	LIST_ENTRY(gprovider)	lg_provider;
	LIST_HEAD(, gconsumer)	lg_consumers;
	char			*lg_mode;
	off_t			lg_mediasize;
	u_int			lg_sectorsize;
	off_t			lg_stripeoffset;
	off_t			lg_stripesize;
	struct gconf		lg_config;
};

struct gident * geom_lookupid(struct gmesh *, const void *);
int geom_xml2tree(struct gmesh *, char *);
int geom_gettree(struct gmesh *);
void geom_deletetree(struct gmesh *);

/* geom_ctl.c */

struct gctl_req;

#ifdef _STDIO_H_			/* limit #include pollution */
void gctl_dump(struct gctl_req *, FILE *);
#endif
void gctl_free(struct gctl_req *);
struct gctl_req *gctl_get_handle(void);
const char *gctl_issue(struct gctl_req *);
void gctl_ro_param(struct gctl_req *, const char *, int, const void *);
void gctl_rw_param(struct gctl_req *, const char *, int, void *);

/* geom_util.c */
int g_open(const char *, int);
int g_close(int);
off_t g_mediasize(int);
ssize_t g_sectorsize(int);
off_t g_stripeoffset(int);
off_t g_stripesize(int);
int g_flush(int);
int g_delete(int, off_t, off_t);
int g_get_ident(int, char *, size_t);
int g_get_name(const char *, char *, size_t);
int g_open_by_ident(const char *, int, char *, size_t);
char *g_device_path(const char *);
char *g_providername(int);

__END_DECLS

#endif /* _LIBGEOM_H_ */
