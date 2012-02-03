/*
 * Command structure for dhd command line utility, copied from wl utility
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
 * $Id: dhdu_cmd.h,v 1.6 2008-06-05 00:36:42 $
 */

#ifndef _dhdu_cmd_h_
#define _dhdu_cmd_h_

typedef struct cmd cmd_t;
typedef int (cmd_func_t)(void *dhd, cmd_t *cmd, char **argv);

/* generic command line argument handler */
struct cmd {
	char *name;
	cmd_func_t *func;
	int get;
	int set;
	char *help;
};

/* list of command line arguments */
extern cmd_t dhd_cmds[];
extern cmd_t dhd_varcmd;

/* Special set cmds to do download via dev node interface if present */
#define	DHD_DLDN_ST		0x400
#define	DHD_DLDN_WRITE		(DHD_DLDN_ST + 1)
#define	DHD_DLDN_END		(DHD_DLDN_ST + 2)

/* per-port ioctl handlers */
extern int dhd_get(void *dhd, int cmd, void *buf, int len);
extern int dhd_set(void *dhd, int cmd, void *buf, int len);

#endif /* _dhdu_cmd_h_ */
