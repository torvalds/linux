/*-
 * Copyright (c) 2016 Adrian Chadd <adrian@FreeBSD.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */
#ifndef	__ATH_CTRL_H__
#define	__ATH_CTRL_H__

struct ath_stats;
struct ath_diag;
struct ath_tx_aggr_stats;
struct ath_rateioctl;

struct ath_driver_req {
	/* Open socket, or -1 */
	int s;
	/* The interface name in question */
	char *ifname;
};

extern	int ath_driver_req_init(struct ath_driver_req *req);
extern	int ath_driver_req_open(struct ath_driver_req *req, const char *ifname);
extern	int ath_driver_req_close(struct ath_driver_req *req);
extern	int ath_driver_req_fetch_diag(struct ath_driver_req *req,
	    unsigned long cmd, struct ath_diag *ad);
extern	int ath_driver_req_zero_stats(struct ath_driver_req *req);
extern	int ath_driver_req_fetch_stats(struct ath_driver_req *req,
	    struct ath_stats *st);
extern	int ath_drive_req_fetch_aggr_stats(struct ath_driver_req *req,
    struct ath_tx_aggr_stats *tx);
extern	int ath_drive_req_fetch_ratectrl_stats(struct ath_driver_req *req,
    struct ath_rateioctl *r);

#endif
