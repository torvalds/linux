/*
 * wl server declarations
 *
 * Copyright (C) 2011, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 * $Id: wlu_server_shared.h,v 1.3.48.1 2010/12/14 05:41:24 Exp $
 */

#ifndef _wlu_server_shared_h
#define _wlu_server_shared_h

	extern int wl_ioctl(void *wl, int cmd, void *buf, int len, bool set);

	extern int dhd_ioctl(void *dhd, int cmd, void *buf, int len, bool set);

#ifdef RWLASD
/* streams' buffers */
BYTE *xcCmdBuf = NULL, *parmsVal = NULL;
BYTE *trafficBuf = NULL, *respBuf = NULL;
struct timeval *toutvalp = NULL;
#endif

#define POLLING_TIME      			200
#define DONGLE_TX_FRAME_SIZE   		1024
#define MESSAGE_LENGTH				1024
#define MAX_SHELL_FILE_LENGTH       50
#define MAX_IOVAR				10000
int remote_type = NO_REMOTE;
rem_ioctl_t *g_rem_ptr;

extern int wl_ioctl(void *wl, int cmd, void *buf, int len, bool set);

/* Function prototypes from shellpoc_linux.c/shell_ce.c */
extern int rwl_create_dir(void);
extern int remote_shell_execute(char *buf_ptr, void *wl);
extern int remote_shell_get_resp(char* shell_fname, void *wl);
extern void rwl_wifi_find_server_response(void *wl, dot11_action_wifi_vendor_specific_t *rec_frame);
extern dot11_action_wifi_vendor_specific_t *rwl_wifi_allocate_actionframe();

/* Common code for serial and wifi */
#if defined(RWL_DONGLE) || defined(RWL_WIFI) || defined(RWL_SERIAL)
typedef struct rem_packet {
	rem_ioctl_t rem_cdc;
	uchar message[MESSAGE_LENGTH];
} rem_packet_t;
#define REMOTE_PACKET_SIZE sizeof(rem_packet_t)

rem_packet_t *g_rem_pkt_ptr;
rem_packet_t g_rem_pkt;
#endif

static struct ether_addr rwlea;

static union {
	uchar bufdata[WLC_IOCTL_MAXLEN];
	uint32 alignme;
} bufstruct_wlu;
static uchar* rwl_buf = (uchar*) &bufstruct_wlu.bufdata;
extern int need_speedy_response;

#endif /* _wlu_server_shared_h_ */
