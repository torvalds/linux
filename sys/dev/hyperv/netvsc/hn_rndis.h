/*-
 * Copyright (c) 2009-2012,2016-2017 Microsoft Corp.
 * Copyright (c) 2010-2012 Citrix Inc.
 * Copyright (c) 2012 NetApp Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _HN_RNDIS_H_
#define _HN_RNDIS_H_

struct hn_softc;

int		hn_rndis_attach(struct hn_softc *sc, int mtu, int *init_done);
void		hn_rndis_detach(struct hn_softc *sc);
int		hn_rndis_conf_rss(struct hn_softc *sc, uint16_t flags);
int		hn_rndis_query_rsscaps(struct hn_softc *sc, int *rxr_cnt);
int		hn_rndis_get_eaddr(struct hn_softc *sc, uint8_t *eaddr);
/* link_status: NDIS_MEDIA_STATE_ */
int		hn_rndis_get_linkstatus(struct hn_softc *sc,
		    uint32_t *link_status);
int		hn_rndis_get_mtu(struct hn_softc *sc, uint32_t *mtu);
/* filter: NDIS_PACKET_TYPE_. */
int		hn_rndis_set_rxfilter(struct hn_softc *sc, uint32_t filter);
void		hn_rndis_rx_ctrl(struct hn_softc *sc, const void *data,
		    int dlen);

#endif  /* !_HN_RNDIS_H_ */
