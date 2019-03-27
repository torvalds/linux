/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-07 Applied Micro Circuits Corporation.
 * Copyright (c) 2004-05 Vinod Kashyap.
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
 *	$FreeBSD$
 */

/*
 * AMCC'S 3ware driver for 9000 series storage controllers.
 *
 * Author: Vinod Kashyap
 * Modifications by: Adam Radford
 */



#ifndef TW_OSL_EXTERNS_H

#define TW_OSL_EXTERNS_H


/*
 * Data structures and functions global to the OS Layer.
 */


/* External data structures. */

extern int	mp_ncpus;



/* Functions in tw_osl_freebsd.c */

/* Build a firmware passthru cmd pkt, and submit it to CL. */
extern TW_INT32	tw_osli_fw_passthru(struct twa_softc *sc, TW_INT8 *buf);

/* Get an OSL internal request context packet. */ 
extern struct tw_osli_req_context *tw_osli_get_request(struct twa_softc *sc);

/* Map data to DMA'able memory. */
extern TW_INT32	tw_osli_map_request(struct tw_osli_req_context *req);

/* Undo mapping. */
extern TW_VOID	tw_osli_unmap_request(struct tw_osli_req_context *req);



/* Functions in tw_osl_cam.c */

/* Attach to CAM. */
extern TW_INT32	tw_osli_cam_attach(struct twa_softc *sc);

/* Detach from CAM. */
extern TW_VOID	tw_osli_cam_detach(struct twa_softc *sc);

/* Request CAM for a bus scan. */
extern TW_INT32	tw_osli_request_bus_scan(struct twa_softc *sc);

/* Freeze ccb flow from CAM. */
extern TW_VOID	tw_osli_disallow_new_requests(struct twa_softc *sc,
	struct tw_cl_req_handle *req_handle);

/* OSL's completion routine for SCSI I/O's. */
extern TW_VOID	tw_osl_complete_io(struct tw_cl_req_handle *req_handle);

/* OSL's completion routine for passthru requests. */
extern TW_VOID	tw_osl_complete_passthru(struct tw_cl_req_handle *req_handle);



#endif /* TW_OSL_EXTERNS_H */
