/*-
 * Data structures and definitions for dealing with the 
 * Common Access Method Transport (xpt) layer from peripheral
 * drivers.
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

#ifndef _CAM_CAM_XPT_PERIPH_H
#define _CAM_CAM_XPT_PERIPH_H 1

#include <cam/cam_queue.h>
#include <cam/cam_xpt.h>

/* Functions accessed by the peripheral drivers */
#ifdef _KERNEL
void		xpt_polled_action(union ccb *ccb);
void		xpt_release_ccb(union ccb *released_ccb);
void		xpt_schedule(struct cam_periph *perph, u_int32_t new_priority);
int32_t		xpt_add_periph(struct cam_periph *periph);
void		xpt_remove_periph(struct cam_periph *periph);
void		xpt_announce_periph(struct cam_periph *periph,
				    char *announce_string);
void		xpt_announce_periph_sbuf(struct cam_periph *periph,
					 struct sbuf *sb,
					 char *announce_string);
void		xpt_announce_quirks(struct cam_periph *periph,
				    int quirks, char *bit_string);
void		xpt_announce_quirks_sbuf(struct cam_periph *periph,
				    struct sbuf *sb,
				    int quirks, char *bit_string);
void		xpt_denounce_periph(struct cam_periph *periph);
void		xpt_denounce_periph_sbuf(struct cam_periph *periph, struct sbuf *sb);
#endif

#endif /* _CAM_CAM_XPT_PERIPH_H */
