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

#ifndef _QMAN_H
#define _QMAN_H

#include <machine/vmparam.h>

#include <contrib/ncsw/inc/Peripherals/qm_ext.h>


/**
 * @group QMan private defines/declarations
 * @{
 */
/**
 * Maximum number of frame queues in all QMans.
 */
#define		QMAN_MAX_FQIDS			16

/**
 * Pool channel common to all software portals.
 * @note Value of 0 reflects the e_QM_FQ_CHANNEL_POOL1 from e_QmFQChannel
 *       type used in qman_fqr_create().
 */
#define		QMAN_COMMON_POOL_CHANNEL	0

#define		QMAN_FQID_BASE			1

#define		QMAN_CCSR_SIZE			0x1000

/*
 * Portal defines
 */
#define QMAN_CE_PA(base)	(base)
#define QMAN_CI_PA(base)	((base) + 0x100000)

#define QMAN_PORTAL_CE_PA(base, n)	\
    (QMAN_CE_PA(base) + ((n) * QMAN_PORTAL_CE_SIZE))
#define QMAN_PORTAL_CI_PA(base, n)	\
    (QMAN_CI_PA(base) + ((n) * QMAN_PORTAL_CI_SIZE))

struct qman_softc {
	device_t	sc_dev;			/* device handle */
	int		sc_rrid;		/* register rid */
	struct resource	*sc_rres;		/* register resource */
	int		sc_irid;		/* interrupt rid */
	struct resource	*sc_ires;		/* interrupt resource */

	bool		sc_regs_mapped[MAXCPU];

	t_Handle	sc_qh;			/* QMAN handle */
	t_Handle	sc_qph[MAXCPU];		/* QMAN portal handles */
	vm_paddr_t	sc_qp_pa;		/* QMAN portal PA */

	int		sc_fqr_cpu[QMAN_MAX_FQIDS];
};
/** @> */


/**
 * @group QMan bus interface
 * @{
 */
int qman_attach(device_t dev);
int qman_detach(device_t dev);
int qman_suspend(device_t dev);
int qman_resume(device_t dev);
int qman_shutdown(device_t dev);
/** @> */


/**
 * @group QMan API
 * @{
 */

/**
 * Create Frame Queue Range.
 *
 * @param fqids_num			Number of frame queues in the range.
 *
 * @param channel			Dedicated channel serviced by this
 * 					Frame Queue Range.
 *
 * @param wq				Work Queue Number within the channel.
 *
 * @param force_fqid			If TRUE, fore allocation of specific
 * 					FQID. Notice that there can not be two
 * 					frame queues with the same ID in the
 * 					system.
 *
 * @param fqid_or_align			FQID if @force_fqid == TRUE, alignment
 * 					of FQIDs entries otherwise.
 *
 * @param init_parked			If TRUE, FQ state is initialized to
 * 					"parked" state on creation. Otherwise,
 * 					to "scheduled" state.
 *
 * @param hold_active			If TRUE, the FQ may be held in the
 * 					portal in "held active" state in
 * 					anticipation of more frames being
 * 					dequeued from it after the head frame
 * 					is removed from the FQ and the dequeue
 * 					response is returned. If FALSE the
 * 					"held_active" state of the FQ is not
 * 					allowed. This affects only on queues
 * 					destined to software portals. Refer to
 * 					the 6.3.4.6 of DPAA Reference Manual.
 *
 * @param prefer_in_cache		If TRUE, prefer this FQR to be in QMan
 * 					internal cache memory for all states.
 *
 * @param congst_avoid_ena		If TRUE, enable congestion avoidance
 * 					mechanism.
 *
 * @param congst_group			A handle to the congestion group. Only
 * 					relevant when @congst_avoid_ena == TRUE.
 *
 * @param overhead_accounting_len	For each frame add this number for CG
 * 					calculation (may be negative), if 0 -
 * 					disable feature.
 *
 * @param tail_drop_threshold		If not 0 - enable tail drop on this
 * 					FQR.
 *
 * @return				A handle to newly created FQR object.
 */
t_Handle qman_fqr_create(uint32_t fqids_num, e_QmFQChannel channel, uint8_t wq,
    bool force_fqid, uint32_t fqid_or_align, bool init_parked,
    bool hold_active, bool prefer_in_cache, bool congst_avoid_ena,
    t_Handle congst_group, int8_t overhead_accounting_len,
    uint32_t tail_drop_threshold);

/**
 * Free Frame Queue Range.
 *
 * @param fqr	A handle to FQR to be freed.
 * @return	E_OK on success; error code otherwise.
 */
t_Error qman_fqr_free(t_Handle fqr);

/**
 * Register the callback function.
 * The callback function will be called when a frame comes from this FQR.
 *
 * @param fqr		A handle to FQR.
 * @param callback	A pointer to the callback function.
 * @param app		A pointer to the user's data.
 * @return		E_OK on success; error code otherwise.
 */
t_Error	qman_fqr_register_cb(t_Handle fqr, t_QmReceivedFrameCallback *callback,
    t_Handle app);

/**
 * Enqueue a frame on a given FQR.
 *
 * @param fqr		A handle to FQR.
 * @param fqid_off	FQID offset wihin the FQR.
 * @param frame		A frame to be enqueued to the transmission.
 * @return		E_OK on success; error code otherwise.
 */
t_Error qman_fqr_enqueue(t_Handle fqr, uint32_t fqid_off, t_DpaaFD *frame);

/**
 * Get one of the FQR counter's value.
 *
 * @param fqr		A handle to FQR.
 * @param fqid_off	FQID offset within the FQR.
 * @param counter	The requested counter.
 * @return		Counter's current value.
 */
uint32_t qman_fqr_get_counter(t_Handle fqr, uint32_t fqid_off,
    e_QmFqrCounters counter);

/**
 * Pull frame from FQR.
 *
 * @param fqr		A handle to FQR.
 * @param fqid_off	FQID offset within the FQR.
 * @param frame		The received frame.
 * @return		E_OK on success; error code otherwise.
 */
t_Error qman_fqr_pull_frame(t_Handle fqr, uint32_t fqid_off, t_DpaaFD *frame);

/**
 * Get base FQID of the FQR.
 * @param fqr	A handle to FQR.
 * @return	Base FQID of the FQR.
 */
uint32_t qman_fqr_get_base_fqid(t_Handle fqr);

/**
 * Poll frames from QMan.
 * This polls frames from the current software portal.
 *
 * @param source	Type of frames to be polled.
 * @return		E_OK on success; error otherwise.
 */
t_Error qman_poll(e_QmPortalPollSource source);

/**
 * General received frame callback.
 * This is called, when user did not register his own callback for a given
 * frame queue range (fqr).
 */
e_RxStoreResponse qman_received_frame_callback(t_Handle app, t_Handle qm_fqr,
    t_Handle qm_portal, uint32_t fqid_offset, t_DpaaFD *frame);

/**
 * General rejected frame callback.
 * This is called, when user did not register his own callback for a given
 * frame queue range (fqr).
 */
e_RxStoreResponse qman_rejected_frame_callback(t_Handle app, t_Handle qm_fqr,
    t_Handle qm_portal, uint32_t fqid_offset, t_DpaaFD *frame,
    t_QmRejectedFrameInfo *qm_rejected_frame_info);

/** @} */

#endif /* QMAN_H */
