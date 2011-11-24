/*
 * Copyright (c) 2005-2006 Atheros Communications, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */


#ifndef _DFS_COMMON_H_
#define _DFS_COMMON_H_

enum {
	DFS_UNINIT_DOMAIN	= 0,	/* Uninitialized dfs domain */
	DFS_FCC_DOMAIN		= 1,	/* FCC3 dfs domain */
	DFS_ETSI_DOMAIN		= 2,	/* ETSI dfs domain */
	DFS_MKK4_DOMAIN		= 3	/* Japan dfs domain */
};



#define MAX_BIN5_DUR  131 /* 105 * 1.25*/

PREPACK struct ath_dfs_capinfo {
    A_UINT64 ext_chan_busy_ts;
    A_UINT8 enable_ar;
    A_UINT8 enable_radar;
} POSTPACK;

typedef struct ath_dfs_capinfo WMI_DFS_HOST_ATTACH_EVENT;

PREPACK struct ath_dfs_info {
    A_UINT32 dfs_domain;
} POSTPACK;

typedef struct ath_dfs_info WMI_DFS_HOST_INIT_EVENT;


PREPACK struct dfs_event_info {
    A_UINT64  full_ts;    /* 64-bit full timestamp from interrupt time */
    A_UINT32  ts;         /* Original 15 bit recv timestamp */
    A_UINT32  ext_chan_busy; /* Ext chan busy % */
    A_UINT8   rssi;       /* rssi of radar event */
    A_UINT8   dur;        /* duration of radar pulse */
    A_UINT8   chanindex;  /* Channel of event */
    A_UINT8   flags; 
#define CH_TYPE_MASK 1
#define PRIMARY_CH 0
#define EXT_CH 1
#define EVENT_TYPE_MASK 2
#define AR_EVENT 0
#define DFS_EVENT 2
} POSTPACK;

/* XXX: Replace 256 with WMI_SVC_MAX_BUFFERED_EVENT_SIZE */
#define WMI_DFS_EVENT_MAX_BUFFER_SIZE ((256 - 1)/sizeof(struct dfs_event_info))
/* Fill in event info */
PREPACK struct dfs_ev_buffer {
    A_UINT8 num_events;
    struct dfs_event_info ev_info[WMI_DFS_EVENT_MAX_BUFFER_SIZE];
} POSTPACK;

typedef struct dfs_ev_buffer WMI_DFS_PHYERR_EVENT;


 /* This should match the table from if_ath.c */
enum {
    ATH_DEBUG_DFS       = 0x00000100,   /* Minimal DFS debug */
    ATH_DEBUG_DFS1      = 0x00000200,   /* Normal DFS debug */
    ATH_DEBUG_DFS2      = 0x00000400,   /* Maximal DFS debug */
    ATH_DEBUG_DFS3      = 0x00000800,   /* matched filterID display */
};

#define TRAFFIC_DETECTED 1

#endif  /* _DFS_H_ */
