/*
 * Common code for wl command line utility
 *
 * Copyright 2002, Broadcom Corporation
 * All Rights Reserved.
 *
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied or
 * duplicated in any form, in whole or in part, without the prior written
 * permission of Broadcom Corporation.
 *
 * $Id: wlu.h,v 1.42.82.3 2010/12/14 05:41:24 Exp $
 */

#ifndef _wlu_h_
#define _wlu_h_

#include "wlu_cmd.h"

extern const char *wlu_av0;

/* parse common option */
extern int wl_option(char ***pargv, char **pifname, int *phelp);
extern void wl_cmd_init(void);
extern void wlu_init(void);

/* print usage */
extern void wl_cmd_usage(FILE *fid, cmd_t *cmd);
extern void wl_usage(FILE *fid, cmd_t *port_cmds);
extern void wl_cmds_usage(FILE *fid, cmd_t *port_cmds);

/* print helpers */
extern void wl_printlasterror(void *wl);
extern void wl_printint(int val);

/* pretty print an SSID */
extern int wl_format_ssid(char* buf, uint8* ssid, int ssid_len);

/* pretty hex print a contiguous buffer */
extern void wl_hexdump(uchar *buf, uint nbytes);

/* check driver version */
extern int wl_check(void *wl);

/* wl functions used by the ndis wl. */
extern void dump_rateset(uint8 *rates, uint count);
extern uint freq2channel(uint freq);
extern int wl_ether_atoe(const char *a, struct ether_addr *n);
extern char *wl_ether_etoa(const struct ether_addr *n);
struct ipv4_addr;	/* forward declaration */
extern int wl_atoip(const char *a, struct ipv4_addr *n);
extern char *wl_iptoa(const struct ipv4_addr *n);
extern cmd_func_t wl_int;
extern cmd_func_t wl_varint;
extern void wl_dump_raw_ie(bcm_tlv_t *ie, uint len);

extern void wl_printlasterror(void *wl);
extern bool wc_cmd_check(const char *cmd);


/* functions for downloading firmware to a device via serial or other transport */
#ifdef SERDOWNLOAD
extern int dhd_init(void *dhd, cmd_t *cmd, char **argv);
extern int dhd_download(void *dhd, cmd_t *cmd, char **argv);
#endif /* SERDOWNLOAD */

#ifdef BCMDLL
#ifdef LOCAL
extern FILE *dll_fd;
#else
extern void * dll_fd_out;
extern void * dll_fd_in;
#endif
#undef printf
#undef fprintf
#define printf printf_to_fprintf	/* printf to stdout */
#define fprintf fprintf_to_fprintf	/* fprintf to stderr */
extern void fprintf_to_fprintf(FILE * stderror, const char *fmt, ...);
extern void printf_to_fprintf(const char *fmt, ...);
extern void raw_puts(const char *buf, void *dll_fd_out);
#define	fputs(buf, stdout) raw_puts(buf, dll_fd_out)
#endif /* BCMDLL */

#define RAM_SIZE_4325  0x60000
#define RAM_SIZE_4329  0x48000
#define RAM_SIZE_43291 0x60000
#define RAM_SIZE_4330_a1  0x3c000
#define RAM_SIZE_4330_b0  0x48000

/* useful macros */
#ifndef ARRAYSIZE
#define ARRAYSIZE(a)  (sizeof(a)/sizeof(a[0]))
#endif /* ARRAYSIZE */

#define USAGE_ERROR  -1		/* Error code for Usage */
#define IOCTL_ERROR  -2		/* Error code for Ioctl failure */
#define BAD_PARAM -3 /* Error code for bad params, but don't dump cmd_help */
#define CMD_DEPRECATED -4 /* Commands that are functionally deprecated or don't provide
			   * a useful value to a specific OS port of wl
			   */

/* integer output format */
#define INT_FMT_DEC	0	/* signed integer */
#define INT_FMT_UINT	1	/* unsigned integer */
#define INT_FMT_HEX	2	/* hexdecimal */

/* command line argument usage */
#define CMD_ERR	-1	/* Error for command */
#define CMD_OPT	0	/* a command line option */
#define CMD_WL	1	/* the start of a wl command */

#endif /* _wlu_h_ */
