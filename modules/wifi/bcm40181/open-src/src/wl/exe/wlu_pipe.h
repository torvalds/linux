/*
 * OS independent declarations
 *
 * Copyright (C) 2011, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 * $Id: wlu_pipe.h,v 1.6 2009/08/07 06:57:00 Exp $
 */
#ifndef _wlu_pipe_h
#define _wlu_pipe_h

/* Function prototypes defined in wlu_pipe.c */

/* Macros to access globals */
extern char *g_rwl_device_name_serial;
#define rwl_get_serial_port_name()		(g_rwl_device_name_serial)
#define rwl_set_serial_port_name(name) 	(g_rwl_device_name_serial = name)
extern char *g_rwl_buf_mac;
#define rwl_get_wifi_mac()				(g_rwl_buf_mac)

extern int remote_CDC_tx(void *wl, uint cmd, uchar *buf, uint buf_len,
uint data_len, uint flags, int debug);
extern rem_ioctl_t *remote_CDC_rx_hdr(void *remote, int debug);
extern int remote_CDC_rx(void *wl, rem_ioctl_t *rem_ptr, uchar *readbuf, uint buflen, int debug);

extern void* rwl_open_pipe(int remote_type, char *port, int ReadTotalTimeout, int debug);
extern int rwl_close_pipe(int remote_type, void* hndle);

extern int rwl_sockconnect(int SockDes, struct sockaddr *servAddr, int size);

extern int remote_CDC_DATA_wifi_rx_frag(void *wl, rem_ioctl_t *rem_ptr, uint input_len,
void *input, bool shell);
extern int remote_CDC_DATA_wifi_rx(void *wl, struct dot11_action_wifi_vendor_specific *rec_frame);
extern int
remote_CDC_wifi_tx(void *wl, uint cmd, uchar *buf, uint buf_len, uint data_len, uint flags);
extern void rwl_wifi_server_response(void *wl, struct dot11_action_wifi_vendor_specific *rec_frame);
extern int rwl_find_remote_wifi_server(void *wl, char *id);
extern void rwl_wifi_purge_actionframes(void *wl);
extern void rwl_wifi_swap_remote_type(int flag);
extern void remote_wifi_ser_init_cmds(void *wl);
#endif /* _wlu_pipe_h */
