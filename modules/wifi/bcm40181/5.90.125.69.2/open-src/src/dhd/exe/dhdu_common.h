/*
 * Linux port of dhd command line utility, hacked from wl utility.
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
 * $Id: dhdu_common.h,v 1.4.34.1 2011-02-05 00:43:28 $
 */

/* Common header file for dhdu_linux.c and dhdu_ndis.c */

#ifndef _dhdu_common_h
#define _dhdu_common_h

/* DHD utility function declarations */
extern int dhd_check(void *dhd);
extern int dhd_atoip(const char *a, struct ipv4_addr *n);
extern int dhd_option(char ***pargv, char **pifname, int *phelp);
void dhd_usage(cmd_t *port_cmds);

/* Remote DHD declarations */
int remote_type = NO_REMOTE;
extern char *g_rwl_buf_mac;
extern char* g_rwl_device_name_serial;
unsigned short g_rwl_servport;
char *g_rwl_servIP = NULL;
unsigned short defined_debug = DEBUG_ERR | DEBUG_INFO;


static int process_args(struct ifreq* ifr, char **argv);


#define dtoh32(i) i
#define dtoh16(i) i

#endif  /* _dhdu_common_h_ */
