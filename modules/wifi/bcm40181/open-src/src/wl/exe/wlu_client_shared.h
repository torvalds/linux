/*
 * OS independent client specific declarations
 *
 * Copyright (C) 2011, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 * $Id: wlu_client_shared.h,v 1.3 2009/05/22 19:19:18 Exp $
 */
#ifndef _wlu_client_shared_h
#define _wlu_client_shared_h

/* Newly added wrappers from wlu_client_shared.c */
extern int
rwl_shell_cmd_proc(void *wl, char **argv, int cmd);
extern int
rwl_queryinformation_fe(void *wl, int cmd, void* input_buf, unsigned long *input_len,
int debug, int rem_ioctl_select);
extern int
rwl_setinformation_fe(void *wl, int cmd, void* buf, unsigned long *len, int debug,
int rem_ioctl_select);
extern void
rwl_usage(int remote_type);

extern int rwl_shell_createproc(void *wl);
extern void rwl_shell_killproc(int pid);
/* declaring these as extern to be used in wlu_ndis.c and wlu_linux.c */
extern volatile sig_atomic_t g_sig_ctrlc;
extern void ctrlc_handler(int num);

#endif /* _wlu_client_share_h */
