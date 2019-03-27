/*-
 * CAM IO Scheduler Interface
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015 Netflix, Inc.
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

#ifndef _CAM_CAM_IOSCHED_H
#define _CAM_CAM_IOSCHED_H

/* No user-serviceable parts in here. */
#ifdef _KERNEL

/* Forward declare all structs to keep interface thin */
struct cam_iosched_softc;
struct sysctl_ctx_list;
struct sysctl_oid;
union ccb;
struct bio;

/*
 * For 64-bit platforms, we know that uintptr_t is the same size as sbintime_t
 * so we can store values in it. For 32-bit systems, however, uintptr_t is only
 * 32-bits, so it won't fit. For those systems, store 24 bits of fraction and 8
 * bits of seconds. This allows us to measure an interval of up to ~256s, which
 * is ~200x what our current uses require. Provide some convenience functions to
 * get the time, subtract two times and convert back to sbintime_t in a safe way
 * that can be centralized.
 */
#ifdef __LP64__
#define CAM_IOSCHED_TIME_SHIFT 0
#else
#define CAM_IOSCHED_TIME_SHIFT 8
#endif
static inline uintptr_t
cam_iosched_now(void)
{

	/* Cast here is to avoid right shifting a signed value */
	return (uintptr_t)((uint64_t)sbinuptime() >> CAM_IOSCHED_TIME_SHIFT);
}

static inline uintptr_t
cam_iosched_delta_t(uintptr_t then)
{

	/* Since the types are identical, wrapping works correctly */
	return (cam_iosched_now() - then);
}

static inline sbintime_t
cam_iosched_sbintime_t(uintptr_t delta)
{

	/* Cast here is to widen the type so the left shift doesn't lose precision */
	return (sbintime_t)((uint64_t)delta << CAM_IOSCHED_TIME_SHIFT);
}

typedef void (*cam_iosched_latfcn_t)(void *, sbintime_t, struct bio *);

int cam_iosched_init(struct cam_iosched_softc **, struct cam_periph *periph);
void cam_iosched_fini(struct cam_iosched_softc *);
void cam_iosched_sysctl_init(struct cam_iosched_softc *, struct sysctl_ctx_list *, struct sysctl_oid *);
struct bio *cam_iosched_next_trim(struct cam_iosched_softc *isc);
struct bio *cam_iosched_get_trim(struct cam_iosched_softc *isc);
struct bio *cam_iosched_next_bio(struct cam_iosched_softc *isc);
void cam_iosched_queue_work(struct cam_iosched_softc *isc, struct bio *bp);
void cam_iosched_flush(struct cam_iosched_softc *isc, struct devstat *stp, int err);
void cam_iosched_schedule(struct cam_iosched_softc *isc, struct cam_periph *periph);
void cam_iosched_finish_trim(struct cam_iosched_softc *isc);
void cam_iosched_submit_trim(struct cam_iosched_softc *isc);
void cam_iosched_put_back_trim(struct cam_iosched_softc *isc, struct bio *bp);
void cam_iosched_set_sort_queue(struct cam_iosched_softc *isc, int val);
int cam_iosched_has_work_flags(struct cam_iosched_softc *isc, uint32_t flags);
void cam_iosched_set_work_flags(struct cam_iosched_softc *isc, uint32_t flags);
void cam_iosched_clr_work_flags(struct cam_iosched_softc *isc, uint32_t flags);
void cam_iosched_trim_done(struct cam_iosched_softc *isc);
int cam_iosched_bio_complete(struct cam_iosched_softc *isc, struct bio *bp, union ccb *done_ccb);
void cam_iosched_set_latfcn(struct cam_iosched_softc *isc, cam_iosched_latfcn_t, void *);
void cam_iosched_set_trim_goal(struct cam_iosched_softc *isc, int goal);
void cam_iosched_set_trim_ticks(struct cam_iosched_softc *isc, int ticks);
#endif
#endif
