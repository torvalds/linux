/*-
 * Data structures and definitions for CAM peripheral ("type") drivers.
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997, 1998 Justin T. Gibbs.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _CAM_CAM_PERIPH_H
#define _CAM_CAM_PERIPH_H 1

#include <sys/queue.h>
#include <cam/cam_sim.h>

#ifdef _KERNEL
#include <sys/sysctl.h>
#include <sys/taskqueue.h>

#include <cam/cam_xpt.h>

struct devstat;

extern struct cam_periph *xpt_periph;

extern struct periph_driver **periph_drivers;
void periphdriver_register(void *);
int periphdriver_unregister(void *);
void periphdriver_init(int level);

#include <sys/module.h>
#define PERIPHDRIVER_DECLARE(name, driver) \
	static int name ## _modevent(module_t mod, int type, void *data) \
	{ \
		switch (type) { \
		case MOD_LOAD: \
			periphdriver_register(data); \
			break; \
		case MOD_UNLOAD: \
			return (periphdriver_unregister(data)); \
		default: \
			return EOPNOTSUPP; \
		} \
		return 0; \
	} \
	static moduledata_t name ## _mod = { \
		#name, \
		name ## _modevent, \
		(void *)&driver \
	}; \
	DECLARE_MODULE(name, name ## _mod, SI_SUB_DRIVERS, SI_ORDER_ANY); \
	MODULE_DEPEND(name, cam, 1, 1, 1)

/*
 * Callback informing the peripheral driver it can perform it's
 * initialization since the XPT is now fully initialized.
 */
typedef void (periph_init_t)(void);

/*
 * Callback requesting the peripheral driver to remove its instances
 * and shutdown, if possible.
 */
typedef int (periph_deinit_t)(void);

struct periph_driver {
	periph_init_t		*init;
	char			*driver_name;
	TAILQ_HEAD(,cam_periph)	 units;
	u_int			 generation;
	u_int			 flags;
#define CAM_PERIPH_DRV_EARLY		0x01
	periph_deinit_t		*deinit;
};

typedef enum {
	CAM_PERIPH_BIO
} cam_periph_type;

/* Generically useful offsets into the peripheral private area */
#define ppriv_ptr0 periph_priv.entries[0].ptr
#define ppriv_ptr1 periph_priv.entries[1].ptr
#define ppriv_field0 periph_priv.entries[0].field
#define ppriv_field1 periph_priv.entries[1].field

typedef void		periph_start_t (struct cam_periph *periph,
					union ccb *start_ccb);
typedef cam_status	periph_ctor_t (struct cam_periph *periph,
				       void *arg);
typedef void		periph_oninv_t (struct cam_periph *periph);
typedef void		periph_dtor_t (struct cam_periph *periph);
struct cam_periph {
	periph_start_t		*periph_start;
	periph_oninv_t		*periph_oninval;
	periph_dtor_t		*periph_dtor;
	char			*periph_name;
	struct cam_path		*path;	/* Compiled path to device */
	void			*softc;
	struct cam_sim		*sim;
	u_int32_t		 unit_number;
	cam_periph_type		 type;
	u_int32_t		 flags;
#define CAM_PERIPH_RUNNING		0x01
#define CAM_PERIPH_LOCKED		0x02
#define CAM_PERIPH_LOCK_WANTED		0x04
#define CAM_PERIPH_INVALID		0x08
#define CAM_PERIPH_NEW_DEV_FOUND	0x10
#define CAM_PERIPH_RECOVERY_INPROG	0x20
#define CAM_PERIPH_RUN_TASK		0x40
#define CAM_PERIPH_FREE			0x80
#define CAM_PERIPH_ANNOUNCED		0x100
	uint32_t		 scheduled_priority;
	uint32_t		 immediate_priority;
	int			 periph_allocating;
	int			 periph_allocated;
	u_int32_t		 refcount;
	SLIST_HEAD(, ccb_hdr)	 ccb_list;	/* For "immediate" requests */
	SLIST_ENTRY(cam_periph)  periph_links;
	TAILQ_ENTRY(cam_periph)  unit_links;
	ac_callback_t		*deferred_callback; 
	ac_code			 deferred_ac;
	struct task		 periph_run_task;
};

#define CAM_PERIPH_MAXMAPS	2

struct cam_periph_map_info {
	int		num_bufs_used;
	struct buf	*bp[CAM_PERIPH_MAXMAPS];
};

cam_status cam_periph_alloc(periph_ctor_t *periph_ctor,
			    periph_oninv_t *periph_oninvalidate,
			    periph_dtor_t *periph_dtor,
			    periph_start_t *periph_start,
			    char *name, cam_periph_type type, struct cam_path *,
			    ac_callback_t *, ac_code, void *arg);
struct cam_periph *cam_periph_find(struct cam_path *path, char *name);
int		cam_periph_list(struct cam_path *, struct sbuf *);
int		cam_periph_acquire(struct cam_periph *periph);
void		cam_periph_doacquire(struct cam_periph *periph);
void		cam_periph_release(struct cam_periph *periph);
void		cam_periph_release_locked(struct cam_periph *periph);
void		cam_periph_release_locked_buses(struct cam_periph *periph);
int		cam_periph_hold(struct cam_periph *periph, int priority);
void		cam_periph_unhold(struct cam_periph *periph);
void		cam_periph_invalidate(struct cam_periph *periph);
int		cam_periph_mapmem(union ccb *ccb,
				  struct cam_periph_map_info *mapinfo,
				  u_int maxmap);
void		cam_periph_unmapmem(union ccb *ccb,
				    struct cam_periph_map_info *mapinfo);
union ccb	*cam_periph_getccb(struct cam_periph *periph,
				   u_int32_t priority);
int		cam_periph_runccb(union ccb *ccb,
				  int (*error_routine)(union ccb *ccb,
						       cam_flags camflags,
						       u_int32_t sense_flags),
				  cam_flags camflags, u_int32_t sense_flags,
				  struct devstat *ds);
int		cam_periph_ioctl(struct cam_periph *periph, u_long cmd, 
				 caddr_t addr,
				 int (*error_routine)(union ccb *ccb,
						      cam_flags camflags,
						      u_int32_t sense_flags));
void		cam_freeze_devq(struct cam_path *path);
u_int32_t	cam_release_devq(struct cam_path *path, u_int32_t relsim_flags,
				 u_int32_t opening_reduction, u_int32_t arg,
				 int getcount_only);
void		cam_periph_async(struct cam_periph *periph, u_int32_t code,
		 		 struct cam_path *path, void *arg);
void		cam_periph_bus_settle(struct cam_periph *periph,
				      u_int bus_settle_ms);
void		cam_periph_freeze_after_event(struct cam_periph *periph,
					      struct timeval* event_time,
					      u_int duration_ms);
int		cam_periph_error(union ccb *ccb, cam_flags camflags,
				 u_int32_t sense_flags);
int		cam_periph_invalidate_sysctl(SYSCTL_HANDLER_ARGS);

static __inline struct mtx *
cam_periph_mtx(struct cam_periph *periph)
{
	if (periph != NULL)
		return (xpt_path_mtx(periph->path));
	else
		return (NULL);
}

#define cam_periph_owned(periph)					\
	mtx_owned(xpt_path_mtx((periph)->path))

#define cam_periph_lock(periph)						\
	mtx_lock(xpt_path_mtx((periph)->path))

#define cam_periph_unlock(periph)					\
	mtx_unlock(xpt_path_mtx((periph)->path))

#define cam_periph_assert(periph, what)					\
	mtx_assert(xpt_path_mtx((periph)->path), (what))

#define cam_periph_sleep(periph, chan, priority, wmesg, timo)		\
	xpt_path_sleep((periph)->path, (chan), (priority), (wmesg), (timo))

static inline struct cam_periph *
cam_periph_acquire_first(struct periph_driver *driver)
{
	struct cam_periph *periph;

	xpt_lock_buses();
	periph = TAILQ_FIRST(&driver->units);
	while (periph != NULL && (periph->flags & CAM_PERIPH_INVALID) != 0)
		periph = TAILQ_NEXT(periph, unit_links);
	if (periph != NULL)
		periph->refcount++;
	xpt_unlock_buses();
	return (periph);
}

static inline struct cam_periph *
cam_periph_acquire_next(struct cam_periph *pperiph)
{
	struct cam_periph *periph = pperiph;

	cam_periph_assert(pperiph, MA_NOTOWNED);
	xpt_lock_buses();
	do {
		periph = TAILQ_NEXT(periph, unit_links);
	} while (periph != NULL && (periph->flags & CAM_PERIPH_INVALID) != 0);
	if (periph != NULL)
		periph->refcount++;
	xpt_unlock_buses();
	cam_periph_release(pperiph);
	return (periph);
}

#define CAM_PERIPH_FOREACH(periph, driver)				\
	for ((periph) = cam_periph_acquire_first(driver);		\
	    (periph) != NULL;						\
	    (periph) = cam_periph_acquire_next(periph))

#define CAM_PERIPH_PRINT(p, msg, args...)				\
    printf("%s%d:" msg, (periph)->periph_name, (periph)->unit_number, ##args)

#endif /* _KERNEL */
#endif /* _CAM_CAM_PERIPH_H */
