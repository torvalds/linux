/*-
 * Data structures and definitions for dealing with the 
 * Common Access Method Transport (xpt) layer.
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Justin T. Gibbs.
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

#ifndef _CAM_CAM_XPT_H
#define _CAM_CAM_XPT_H 1

#ifdef _KERNEL
#include <sys/cdefs.h>
#include <cam/cam_ccb.h>
#endif


/* Forward Declarations */
union ccb;
struct cam_periph;
struct cam_ed;
struct cam_sim;
struct sbuf;

/*
 * Definition of a CAM path.  Paths are created from bus, target, and lun ids
 * via xpt_create_path and allow for reference to devices without recurring
 * lookups in the edt.
 */
struct cam_path;

/* Path functions */

#ifdef _KERNEL

/*
 * Definition of an async handler callback block.  These are used to add
 * SIMs and peripherals to the async callback lists.
 */
struct async_node {
	SLIST_ENTRY(async_node)	links;
	u_int32_t	event_enable;	/* Async Event enables */
	u_int32_t	event_lock;	/* Take SIM lock for handlers. */
	void		(*callback)(void *arg, u_int32_t code,
				    struct cam_path *path, void *args);
	void		*callback_arg;
};

SLIST_HEAD(async_list, async_node);
SLIST_HEAD(periph_list, cam_periph);

void			xpt_action(union ccb *new_ccb);
void			xpt_action_default(union ccb *new_ccb);
union ccb		*xpt_alloc_ccb(void);
union ccb		*xpt_alloc_ccb_nowait(void);
void			xpt_free_ccb(union ccb *free_ccb);
void			xpt_setup_ccb_flags(struct ccb_hdr *ccb_h,
					    struct cam_path *path,
					    u_int32_t priority,
					    u_int32_t flags);
void			xpt_setup_ccb(struct ccb_hdr *ccb_h,
				      struct cam_path *path,
				      u_int32_t priority);
void			xpt_merge_ccb(union ccb *master_ccb,
				      union ccb *slave_ccb);
cam_status		xpt_create_path(struct cam_path **new_path_ptr,
					struct cam_periph *perph,
					path_id_t path_id,
					target_id_t target_id, lun_id_t lun_id);
cam_status		xpt_create_path_unlocked(struct cam_path **new_path_ptr,
					struct cam_periph *perph,
					path_id_t path_id,
					target_id_t target_id, lun_id_t lun_id);
int			xpt_getattr(char *buf, size_t len, const char *attr,
				    struct cam_path *path);
void			xpt_free_path(struct cam_path *path);
void			xpt_path_counts(struct cam_path *path, uint32_t *bus_ref,
					uint32_t *periph_ref, uint32_t *target_ref,
					uint32_t *device_ref);
int			xpt_path_comp(struct cam_path *path1,
				      struct cam_path *path2);
int			xpt_path_comp_dev(struct cam_path *path,
					  struct cam_ed *dev);
void			xpt_print_path(struct cam_path *path);
void			xpt_print_device(struct cam_ed *device);
void			xpt_print(struct cam_path *path, const char *fmt, ...);
int			xpt_path_string(struct cam_path *path, char *str,
					size_t str_len);
int			xpt_path_sbuf(struct cam_path *path, struct sbuf *sb);
path_id_t		xpt_path_path_id(struct cam_path *path);
target_id_t		xpt_path_target_id(struct cam_path *path);
lun_id_t		xpt_path_lun_id(struct cam_path *path);
struct cam_sim		*xpt_path_sim(struct cam_path *path);
struct cam_periph	*xpt_path_periph(struct cam_path *path);
void			xpt_async(u_int32_t async_code, struct cam_path *path,
				  void *async_arg);
void			xpt_rescan(union ccb *ccb);
void			xpt_hold_boot(void);
void			xpt_release_boot(void);
void			xpt_lock_buses(void);
void			xpt_unlock_buses(void);
struct mtx *		xpt_path_mtx(struct cam_path *path);
#define xpt_path_lock(path)	mtx_lock(xpt_path_mtx(path))
#define xpt_path_unlock(path)	mtx_unlock(xpt_path_mtx(path))
#define xpt_path_assert(path, what)	mtx_assert(xpt_path_mtx(path), (what))
#define xpt_path_owned(path)	mtx_owned(xpt_path_mtx(path))
#define xpt_path_sleep(path, chan, priority, wmesg, timo)		\
    msleep((chan), xpt_path_mtx(path), (priority), (wmesg), (timo))
cam_status		xpt_register_async(int event, ac_callback_t *cbfunc,
					   void *cbarg, struct cam_path *path);
cam_status		xpt_compile_path(struct cam_path *new_path,
					 struct cam_periph *perph,
					 path_id_t path_id,
					 target_id_t target_id,
					 lun_id_t lun_id);
cam_status		xpt_clone_path(struct cam_path **new_path,
				      struct cam_path *path);
void			xpt_copy_path(struct cam_path *new_path,
				      struct cam_path *path);

void			xpt_release_path(struct cam_path *path);

const char *		xpt_action_name(uint32_t action);
void			xpt_pollwait(union ccb *start_ccb, uint32_t timeout);
uint32_t		xpt_poll_setup(union ccb *start_ccb);
void			xpt_sim_poll(struct cam_sim *sim);

/*
 * Perform a path inquiry at the request priority. The bzero may be
 * unnecessary.
 */
static inline void
xpt_path_inq(struct ccb_pathinq *cpi, struct cam_path *path)
{

	bzero(cpi, sizeof(*cpi));
	xpt_setup_ccb(&cpi->ccb_h, path, CAM_PRIORITY_NORMAL);
	cpi->ccb_h.func_code = XPT_PATH_INQ;
	xpt_action((union ccb *)cpi);
}

#endif /* _KERNEL */

#endif /* _CAM_CAM_XPT_H */
