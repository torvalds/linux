/*
 * Definitions for DHD command-line utility
 *
 * Copyright (C) 1999-2011, Broadcom Corporation
 * 
 *         Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: dhdu.h,v 1.7 2009-05-22 19:23:29 $
 */

#ifndef _dhdu_h_
#define _dhdu_h_

#include "dhdu_cmd.h"

extern char *dhdu_av0;

/* parse common option */
extern int dhd_option(char ***pargv, char **pifname, int *phelp);
extern void dhd_cmd_init(void);

/* print usage */
extern void dhd_cmd_usage(cmd_t *cmd);
extern void dhd_usage(cmd_t *port_cmds);
extern void dhd_cmds_usage(cmd_t *port_cmds);

/* print helpers */
extern void dhd_printlasterror(void *dhd);
extern void dhd_printint(int val);

/* check driver version */
extern int dhd_check(void *dhd);

/* utility functions */
struct ipv4_addr;
int dhd_ether_atoe(const char *a, struct ether_addr *n);
int dhd_atoip(const char *a, struct ipv4_addr *n);
/* useful macros */
#define ARRAYSIZE(a)  (sizeof(a)/sizeof(a[0]))

#define USAGE_ERROR    -1	/* Error code for Usage */
#define IOCTL_ERROR    -2	/* Error code for ioctl failure */
#define COMMAND_ERROR  -3	/* Error code for general command failure */

/* integer output format */
#define INT_FMT_DEC	0	/* signed integer */
#define INT_FMT_UINT	1	/* unsigned integer */
#define INT_FMT_HEX	2	/* hexdecimal */

/* command line argument usage */
#define CMD_ERR		-1	/* Error for command */
#define CMD_OPT		0	/* a command line option */
#define CMD_DHD		1	/* the start of a dhd command */

#endif /* _dhdu_h_ */
