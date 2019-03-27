/*-
 * Copyright (c) 2011-2012 Semihalf.
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

#ifndef _BMAN_H
#define _BMAN_H

#include <machine/vmparam.h>

#include <contrib/ncsw/inc/Peripherals/bm_ext.h>

/*
 * BMAN Configuration
 */

/* Maximum number of buffers in all BMAN pools */
#define BMAN_MAX_BUFFERS	4096

/*
 * Portal definitions
 */
#define BMAN_CE_PA(base)	(base)
#define BMAN_CI_PA(base)	((base) + 0x100000)

#define BMAN_PORTAL_CE_PA(base, n)	\
    (BMAN_CE_PA(base) + ((n) * BMAN_PORTAL_CE_SIZE))
#define BMAN_PORTAL_CI_PA(base, n)	\
    (BMAN_CI_PA(base) + ((n) * BMAN_PORTAL_CI_SIZE))

#define BMAN_CCSR_SIZE		0x1000

struct bman_softc {
	device_t	sc_dev;			/* device handle */
	int		sc_rrid;		/* register rid */
	struct resource	*sc_rres;		/* register resource */
	int		sc_irid;		/* interrupt rid */
	struct resource	*sc_ires;		/* interrupt resource */

	bool		sc_regs_mapped[MAXCPU];	/* register mapping status */

	t_Handle	sc_bh;			/* BMAN handle */
	t_Handle	sc_bph[MAXCPU];		/* BMAN portal handles */
	vm_paddr_t	sc_bp_pa;		/* BMAN portals PA */
	unsigned int	sc_bpool_cpu[BM_MAX_NUM_OF_POOLS];
};

/*
 * External API
 */

/*
 * @brief Function to create BMAN pool.
 *
 * @param bpid		The pointer to variable where Buffer Pool ID will be
 *			stored.
 *
 * @param bufferSize	The size of buffers in newly created pool.
 *
 * @param maxBuffers	The maximum number of buffers in software stockpile.
 *			Set to 0 if software stockpile should not be created.
 *
 * @param minBuffers	The minimum number of buffers in software stockpile.
 *			Set to 0 if software stockpile should not be created.
 *
 * @param allocBuffers	The number of buffers to preallocate during pool
 *			creation.
 *
 * @param f_GetBuf	The buffer allocating function. Called only by
 *			bman_pool_create() and bman_pool_fill().
 *
 * @param f_PutBuf	The buffer freeing function. Called only by
 *			bman_pool_destroy().
 *
 * @param dep_sw_entry	The software portal depletion entry threshold.
 *			Set to 0 if depletion should not be signaled on
 *			software portal.
 *
 * @param dep_sw_exit	The software portal depletion exit threshold.
 *			Set to 0 if depletion should not be signaled on
 *			software portal.
 *
 * @param dep_hw_entry	The hardware portal depletion entry threshold.
 *			Set to 0 if depletion should not be signaled on
 *			hardware portal.
 *
 * @param dep_hw_exit	The hardware portal depletion exit threshold.
 *			Set to 0 if depletion should not be signaled on
 *			hardware portal.
 *
 * @param f_Depletion	The software portal depletion notification function.
 *			Set to NULL if depletion notification is not used.
 *
 * @param h_BufferPool	The user provided buffer pool context passed to
 *			f_GetBuf, f_PutBuf and f_Depletion functions.
 *
 * @param f_PhysToVirt	The PA to VA translation function. Set to NULL if
 *			default	one should be used.
 *
 * @param f_VirtToPhys	The VA to PA translation function. Set to NULL if
 *			default one should be used.
 *
 * @returns		Handle to newly created BMAN pool or NULL on error.
 *
 * @cautions		If pool uses software stockpile, all accesses to given
 *			pool must be protected by lock. Even if only hardware
 *			portal depletion notification is used, the caller must
 *			provide valid @p f_Depletion function.
 */
t_Handle bman_pool_create(uint8_t *bpid, uint16_t bufferSize,
    uint16_t maxBuffers, uint16_t minBuffers, uint16_t allocBuffers,
    t_GetBufFunction *f_GetBuf, t_PutBufFunction *f_PutBuf,
    uint32_t dep_sw_entry, uint32_t dep_sw_exit, uint32_t dep_hw_entry,
    uint32_t dep_hw_exit, t_BmDepletionCallback *f_Depletion,
    t_Handle h_BufferPool, t_PhysToVirt *f_PhysToVirt,
    t_VirtToPhys *f_VirtToPhys);

/*
 * @brief Fill pool with buffers.
 *
 * The bman_pool_fill() function fills the BMAN pool with buffers. The buffers
 * are allocated through f_GetBuf function (see bman_pool_create() description).
 *
 * @param pool		The BMAN pool handle.
 * @param nbufs		The number of buffers to allocate. To maximize
 *			performance this value should be multiple of 8.
 *
 * @returns		Zero on success or error code on failure.
 */
int bman_pool_fill(t_Handle pool, uint16_t nbufs);

/*
 * @brief Destroy pool.
 *
 * The bman_pool_destroy() function destroys the BMAN pool. Buffers for pool
 * are free through f_PutBuf function (see bman_pool_create() description).
 *
 * @param pool		The BMAN pool handle.
 *
 * @returns		Zero on success or error code on failure.
 */
int bman_pool_destroy(t_Handle pool);

/*
 * @brief Get a buffer from BMAN pool.
 *
 * @param pool		The BMAN pool handle.
 *
 * @returns		Pointer to the buffer or NULL if pool is empty.
 */
void *bman_get_buffer(t_Handle pool);

/*
 * @brief Put a buffer to BMAN pool.
 *
 * @param pool		The BMAN pool handle.
 * @param buffer	The pointer to buffer.
 *
 * @returns		Zero on success or error code on failure.
 */
int bman_put_buffer(t_Handle pool, void *buffer);

/*
 * @brief Count free buffers in given pool.
 *
 * @param pool		The BMAN pool handle.
 *
 * @returns		Number of free buffers in pool.
 */
uint32_t bman_count(t_Handle pool);

/*
 * Bus i/f
 */
int bman_attach(device_t dev);
int bman_detach(device_t dev);
int bman_suspend(device_t dev);
int bman_resume(device_t dev);
int bman_shutdown(device_t dev);

#endif /* BMAN_H */
