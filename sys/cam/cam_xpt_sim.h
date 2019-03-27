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

#ifndef _CAM_CAM_XPT_SIM_H
#define _CAM_CAM_XPT_SIM_H 1

#include <cam/cam_xpt.h>
#include <cam/cam_queue.h>

/* Functions accessed by SIM drivers */
#ifdef _KERNEL
int32_t		xpt_bus_register(struct cam_sim *sim, device_t parent,
				 u_int32_t bus);
int32_t		xpt_bus_deregister(path_id_t path_id);
u_int32_t	xpt_freeze_simq(struct cam_sim *sim, u_int count);
void		xpt_release_simq(struct cam_sim *sim, int run_queue);
u_int32_t	xpt_freeze_devq(struct cam_path *path, u_int count);
void		xpt_release_devq(struct cam_path *path,
		    u_int count, int run_queue);
void		xpt_done(union ccb *done_ccb);
void		xpt_done_direct(union ccb *done_ccb);
#endif

#endif /* _CAM_CAM_XPT_SIM_H */

