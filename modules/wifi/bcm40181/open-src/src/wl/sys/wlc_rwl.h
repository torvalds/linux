/*
 * RWL module  of
 * Broadcom 802.11bang Networking Device Driver
 *
 * Copyright (C) 2011, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 * 
 * $Id: wlc_rwl.h,v 1.4.4.1 2010/05/16 19:58:15 Exp $
 *
 */

#ifndef _wlc_rwl_h_
#define _wlc_rwl_h_

#if defined(RWL_WIFI) || defined(WIFI_REFLECTOR)

#include <rwl_wifi.h>

typedef struct rwl_info {
	wlc_info_t	*wlc;
	wlc_pub_t	*pub;
	rwl_request_t *rwl_first_action_node;
	rwl_request_t *rwl_last_action_node;
	struct ether_addr rwl_ea;
} rwl_info_t;

extern rwl_info_t* wlc_rwl_attach(wlc_pub_t *pub, wlc_info_t *wlc);
extern int wlc_rwl_detach(rwl_info_t *rwlh);
extern void wlc_rwl_init(rwl_info_t *rwlh);
extern void wlc_rwl_deinit(rwl_info_t *rwlh);
extern void wlc_rwl_up(wlc_info_t *wlc);
extern uint wlc_rwl_down(wlc_info_t *wlc);
extern void wlc_rwl_frameaction(rwl_info_t *rwlh, struct dot11_management_header *hdr,
                                uint8 *body, int body_len);
extern void wlc_recv_wifi_mgmtact(rwl_info_t *rwlh, uint8 *body, const struct ether_addr * sa);

#else /* !defined(RWL_WIFI) && !defined(WIFI_REFLECTOR) */

typedef struct rwl_info {
	wlc_info_t	*wlc;
	wlc_pub_t	*pub;
} rwl_info_t;

#define wlc_rwl_attach(a, b)		(rwl_info_t *)0xdeadbeef
#define wlc_rwl_detach(a)		0
#define wlc_rwl_init(a)			do {} while (0)
#define wlc_rwl_deinit(a)		do {} while (0)
#define wlc_rwl_up(a)			do {} while (0)
#define wlc_rwl_down(a)			0
#define wlc_rwl_frameaction(a)		do {} while (0)

#endif /* !defined(RWL_WIFI) && !defined(WIFI_REFLECTOR) */

#endif	/* _wlc_rwl_h_ */
