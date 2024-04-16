/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2014 Broadcom Corporation
 */

/* Included by drivers/net/ethernet/broadcom/bcmsysport.c and
 * net/dsa/tag_brcm.c
 */
#ifndef _NET_DSA_BRCM_H
#define _NET_DSA_BRCM_H

/* Broadcom tag specific helpers to insert and extract queue/port number */
#define BRCM_TAG_SET_PORT_QUEUE(p, q)	((p) << 8 | q)
#define BRCM_TAG_GET_PORT(v)		((v) >> 8)
#define BRCM_TAG_GET_QUEUE(v)		((v) & 0xff)

#endif
