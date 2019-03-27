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

#ifndef _GEOM_GEOM_H_
#define _GEOM_GEOM_H_

#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/queue.h>
#include <sys/ioccom.h>
#include <sys/conf.h>
#include <sys/module.h>

struct g_class;
struct g_geom;
struct g_consumer;
struct g_provider;
struct g_stat;
struct thread;
struct bio;
struct sbuf;
struct gctl_req;
struct g_configargs;
struct disk_zone_args;

typedef int g_config_t (struct g_configargs *ca);
typedef void g_ctl_req_t (struct gctl_req *, struct g_class *cp, char const *verb);
typedef int g_ctl_create_geom_t (struct gctl_req *, struct g_class *cp, struct g_provider *pp);
typedef int g_ctl_destroy_geom_t (struct gctl_req *, struct g_class *cp, struct g_geom *gp);
typedef int g_ctl_config_geom_t (struct gctl_req *, struct g_geom *gp, const char *verb);
typedef void g_init_t (struct g_class *mp);
typedef void g_fini_t (struct g_class *mp);
typedef struct g_geom * g_taste_t (struct g_class *, struct g_provider *, int flags);
typedef int g_ioctl_t(struct g_provider *pp, u_long cmd, void *data, int fflag, struct thread *td);
#define G_TF_NORMAL		0
#define G_TF_INSIST		1
#define G_TF_TRANSPARENT	2
typedef int g_access_t (struct g_provider *, int, int, int);
/* XXX: not sure about the thread arg */
typedef void g_orphan_t (struct g_consumer *);

typedef void g_start_t (struct bio *);
typedef void g_spoiled_t (struct g_consumer *);
typedef void g_attrchanged_t (struct g_consumer *, const char *attr);
typedef void g_provgone_t (struct g_provider *);
typedef void g_dumpconf_t (struct sbuf *, const char *indent, struct g_geom *,
    struct g_consumer *, struct g_provider *);
typedef void g_resize_t(struct g_consumer *cp);

/*
 * The g_class structure describes a transformation class.  In other words
 * all BSD disklabel handlers share one g_class, all MBR handlers share
 * one common g_class and so on.
 * Certain operations are instantiated on the class, most notably the
 * taste and config_geom functions.
 */
struct g_class {
	const char		*name;
	u_int			version;
	u_int			spare0;
	g_taste_t		*taste;
	g_config_t		*config;
	g_ctl_req_t		*ctlreq;
	g_init_t		*init;
	g_fini_t		*fini;
	g_ctl_destroy_geom_t	*destroy_geom;
	/*
	 * Default values for geom methods
	 */
	g_start_t		*start;
	g_spoiled_t		*spoiled;
	g_attrchanged_t		*attrchanged;
	g_dumpconf_t		*dumpconf;
	g_access_t		*access;
	g_orphan_t		*orphan;
	g_ioctl_t		*ioctl;
	g_provgone_t		*providergone;
	g_resize_t		*resize;
	void			*spare1;
	void			*spare2;
	/*
	 * The remaining elements are private
	 */
	LIST_ENTRY(g_class)	class;
	LIST_HEAD(,g_geom)	geom;
};

/*
 * The g_geom_alias is a list node for aliases for the geom name
 * for device node creation.
 */
struct g_geom_alias {
	LIST_ENTRY(g_geom_alias) ga_next;
	const char		*ga_alias;
};

#define G_VERSION_00	0x19950323
#define G_VERSION_01	0x20041207	/* add fflag to g_ioctl_t */
#define G_VERSION	G_VERSION_01

/*
 * The g_geom is an instance of a g_class.
 */
struct g_geom {
	char			*name;
	struct g_class		*class;
	LIST_ENTRY(g_geom)	geom;
	LIST_HEAD(,g_consumer)	consumer;
	LIST_HEAD(,g_provider)	provider;
	TAILQ_ENTRY(g_geom)	geoms;	/* XXX: better name */
	int			rank;
	g_start_t		*start;
	g_spoiled_t		*spoiled;
	g_attrchanged_t		*attrchanged;
	g_dumpconf_t		*dumpconf;
	g_access_t		*access;
	g_orphan_t		*orphan;
	g_ioctl_t		*ioctl;
	g_provgone_t		*providergone;
	g_resize_t		*resize;
	void			*spare0;
	void			*spare1;
	void			*softc;
	unsigned		flags;
#define	G_GEOM_WITHER		0x01
#define	G_GEOM_VOLATILE_BIO	0x02
#define	G_GEOM_IN_ACCESS	0x04
#define	G_GEOM_ACCESS_WAIT	0x08
	LIST_HEAD(,g_geom_alias) aliases;
};

/*
 * The g_bioq is a queue of struct bio's.
 * XXX: possibly collection point for statistics.
 * XXX: should (possibly) be collapsed with sys/bio.h::bio_queue_head.
 */
struct g_bioq {
	TAILQ_HEAD(, bio)	bio_queue;
	struct mtx		bio_queue_lock;
	int			bio_queue_length;
};

/*
 * A g_consumer is an attachment point for a g_provider.  One g_consumer
 * can only be attached to one g_provider, but multiple g_consumers
 * can be attached to one g_provider.
 */

struct g_consumer {
	struct g_geom		*geom;
	LIST_ENTRY(g_consumer)	consumer;
	struct g_provider	*provider;
	LIST_ENTRY(g_consumer)	consumers;	/* XXX: better name */
	int			acr, acw, ace;
	int			flags;
#define G_CF_SPOILED		0x1
#define G_CF_ORPHAN		0x4
#define G_CF_DIRECT_SEND	0x10
#define G_CF_DIRECT_RECEIVE	0x20
	struct devstat		*stat;
	u_int			nstart, nend;

	/* Two fields for the implementing class to use */
	void			*private;
	u_int			index;
};

/*
 * A g_provider is a "logical disk".
 */
struct g_provider {
	char			*name;
	LIST_ENTRY(g_provider)	provider;
	struct g_geom		*geom;
	LIST_HEAD(,g_consumer)	consumers;
	int			acr, acw, ace;
	int			error;
	TAILQ_ENTRY(g_provider)	orphan;
	off_t			mediasize;
	u_int			sectorsize;
	off_t			stripesize;
	off_t			stripeoffset;
	struct devstat		*stat;
	u_int			nstart, nend;
	u_int			flags;
#define G_PF_WITHER		0x2
#define G_PF_ORPHAN		0x4
#define	G_PF_ACCEPT_UNMAPPED	0x8
#define G_PF_DIRECT_SEND	0x10
#define G_PF_DIRECT_RECEIVE	0x20

	/* Two fields for the implementing class to use */
	void			*private;
	u_int			index;
};

/*
 * Descriptor of a classifier. We can register a function and
 * an argument, which is called by g_io_request() on bio's
 * that are not previously classified.
 */
struct g_classifier_hook {
	TAILQ_ENTRY(g_classifier_hook) link;
	int			(*func)(void *arg, struct bio *bp);
	void			*arg;
};

/* BIO_GETATTR("GEOM::setstate") argument values. */
#define G_STATE_FAILED		0
#define G_STATE_REBUILD		1
#define G_STATE_RESYNC		2
#define G_STATE_ACTIVE		3

/* geom_dev.c */
struct cdev;
void g_dev_print(void);
void g_dev_physpath_changed(void);
struct g_provider *g_dev_getprovider(struct cdev *dev);

/* geom_dump.c */
void g_trace(int level, const char *, ...);
#	define G_T_TOPOLOGY	1
#	define G_T_BIO		2
#	define G_T_ACCESS	4


/* geom_event.c */
typedef void g_event_t(void *, int flag);
#define EV_CANCEL	1
int g_post_event(g_event_t *func, void *arg, int flag, ...);
int g_waitfor_event(g_event_t *func, void *arg, int flag, ...);
void g_cancel_event(void *ref);
int g_attr_changed(struct g_provider *pp, const char *attr, int flag);
int g_media_changed(struct g_provider *pp, int flag);
int g_media_gone(struct g_provider *pp, int flag);
void g_orphan_provider(struct g_provider *pp, int error);
void g_waitidlelock(void);

/* geom_subr.c */
int g_access(struct g_consumer *cp, int nread, int nwrite, int nexcl);
int g_attach(struct g_consumer *cp, struct g_provider *pp);
int g_compare_names(const char *namea, const char *nameb);
void g_destroy_consumer(struct g_consumer *cp);
void g_destroy_geom(struct g_geom *pp);
void g_destroy_provider(struct g_provider *pp);
void g_detach(struct g_consumer *cp);
void g_error_provider(struct g_provider *pp, int error);
struct g_provider *g_provider_by_name(char const *arg);
void g_geom_add_alias(struct g_geom *gp, const char *alias);
int g_getattr__(const char *attr, struct g_consumer *cp, void *var, int len);
#define g_getattr(a, c, v) g_getattr__((a), (c), (v), sizeof *(v))
int g_handleattr(struct bio *bp, const char *attribute, const void *val,
    int len);
int g_handleattr_int(struct bio *bp, const char *attribute, int val);
int g_handleattr_off_t(struct bio *bp, const char *attribute, off_t val);
int g_handleattr_uint16_t(struct bio *bp, const char *attribute, uint16_t val);
int g_handleattr_str(struct bio *bp, const char *attribute, const char *str);
struct g_consumer * g_new_consumer(struct g_geom *gp);
struct g_geom * g_new_geomf(struct g_class *mp, const char *fmt, ...)
    __printflike(2, 3);
struct g_provider * g_new_providerf(struct g_geom *gp, const char *fmt, ...)
    __printflike(2, 3);
void g_resize_provider(struct g_provider *pp, off_t size);
int g_retaste(struct g_class *mp);
void g_spoil(struct g_provider *pp, struct g_consumer *cp);
int g_std_access(struct g_provider *pp, int dr, int dw, int de);
void g_std_done(struct bio *bp);
void g_std_spoiled(struct g_consumer *cp);
void g_wither_geom(struct g_geom *gp, int error);
void g_wither_geom_close(struct g_geom *gp, int error);
void g_wither_provider(struct g_provider *pp, int error);

#if defined(DIAGNOSTIC) || defined(DDB)
int g_valid_obj(void const *ptr);
#endif
#ifdef DIAGNOSTIC
#define G_VALID_CLASS(foo) \
    KASSERT(g_valid_obj(foo) == 1, ("%p is not a g_class", foo))
#define G_VALID_GEOM(foo) \
    KASSERT(g_valid_obj(foo) == 2, ("%p is not a g_geom", foo))
#define G_VALID_CONSUMER(foo) \
    KASSERT(g_valid_obj(foo) == 3, ("%p is not a g_consumer", foo))
#define G_VALID_PROVIDER(foo) \
    KASSERT(g_valid_obj(foo) == 4, ("%p is not a g_provider", foo))
#else
#define G_VALID_CLASS(foo) do { } while (0)
#define G_VALID_GEOM(foo) do { } while (0)
#define G_VALID_CONSUMER(foo) do { } while (0)
#define G_VALID_PROVIDER(foo) do { } while (0)
#endif

int g_modevent(module_t, int, void *);

/* geom_io.c */
struct bio * g_clone_bio(struct bio *);
struct bio * g_duplicate_bio(struct bio *);
void g_destroy_bio(struct bio *);
void g_io_deliver(struct bio *bp, int error);
int g_io_getattr(const char *attr, struct g_consumer *cp, int *len, void *ptr);
int g_io_zonecmd(struct disk_zone_args *zone_args, struct g_consumer *cp);
int g_io_flush(struct g_consumer *cp);
int g_register_classifier(struct g_classifier_hook *hook);
void g_unregister_classifier(struct g_classifier_hook *hook);
void g_io_request(struct bio *bp, struct g_consumer *cp);
struct bio *g_new_bio(void);
struct bio *g_alloc_bio(void);
void g_reset_bio(struct bio *);
void * g_read_data(struct g_consumer *cp, off_t offset, off_t length, int *error);
int g_write_data(struct g_consumer *cp, off_t offset, void *ptr, off_t length);
int g_delete_data(struct g_consumer *cp, off_t offset, off_t length);
void g_print_bio(struct bio *bp);
int g_use_g_read_data(void *, off_t, void **, int);
int g_use_g_write_data(void *, off_t, void *, int);

/* geom_kern.c / geom_kernsim.c */

#ifdef _KERNEL

extern struct sx topology_lock;

struct g_kerneldump {
	off_t		offset;
	off_t		length;
	struct dumperinfo di;
};

MALLOC_DECLARE(M_GEOM);

static __inline void *
g_malloc(int size, int flags)
{
	void *p;

	p = malloc(size, M_GEOM, flags);
	return (p);
}

static __inline void
g_free(void *ptr)
{

#ifdef DIAGNOSTIC
	if (sx_xlocked(&topology_lock)) {
		KASSERT(g_valid_obj(ptr) == 0,
		    ("g_free(%p) of live object, type %d", ptr,
		    g_valid_obj(ptr)));
	}
#endif
	free(ptr, M_GEOM);
}

#define g_topology_lock() 					\
	do {							\
		sx_xlock(&topology_lock);			\
	} while (0)

#define g_topology_try_lock()	sx_try_xlock(&topology_lock)

#define g_topology_unlock()					\
	do {							\
		sx_xunlock(&topology_lock);			\
	} while (0)

#define g_topology_assert()					\
	do {							\
		sx_assert(&topology_lock, SX_XLOCKED);		\
	} while (0)

#define g_topology_assert_not()					\
	do {							\
		sx_assert(&topology_lock, SX_UNLOCKED);		\
	} while (0)

#define g_topology_sleep(chan, timo)				\
	sx_sleep(chan, &topology_lock, 0, "gtopol", timo)

#define DECLARE_GEOM_CLASS(class, name) 			\
	static moduledata_t name##_mod = {			\
		#name, g_modevent, &class			\
	};							\
	DECLARE_MODULE(name, name##_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);

int g_is_geom_thread(struct thread *td);

#endif /* _KERNEL */

/* geom_ctl.c */
int gctl_set_param(struct gctl_req *req, const char *param, void const *ptr, int len);
void gctl_set_param_err(struct gctl_req *req, const char *param, void const *ptr, int len);
void *gctl_get_param(struct gctl_req *req, const char *param, int *len);
char const *gctl_get_asciiparam(struct gctl_req *req, const char *param);
void *gctl_get_paraml(struct gctl_req *req, const char *param, int len);
int gctl_error(struct gctl_req *req, const char *fmt, ...) __printflike(2, 3);
struct g_class *gctl_get_class(struct gctl_req *req, char const *arg);
struct g_geom *gctl_get_geom(struct gctl_req *req, struct g_class *mpr, char const *arg);
struct g_provider *gctl_get_provider(struct gctl_req *req, char const *arg);

#endif /* _GEOM_GEOM_H_ */
