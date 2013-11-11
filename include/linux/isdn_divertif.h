/* $Id: isdn_divertif.h,v 1.4.6.1 2001/09/23 22:25:05 kai Exp $
 *
 * Header for the diversion supplementary interface for i4l.
 *
 * Author    Werner Cornelius (werner@titro.de)
 * Copyright by Werner Cornelius (werner@titro.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */
#ifndef _LINUX_ISDN_DIVERTIF_H
#define _LINUX_ISDN_DIVERTIF_H

#include <linux/isdnif.h>
#include <linux/types.h>
#include <uapi/linux/isdn_divertif.h>

/***************************************************************/
/* structure exchanging data between isdn hl and divert module */
/***************************************************************/ 
typedef struct
  { ulong if_magic; /* magic info and version */
    int cmd; /* command */
    int (*stat_callback)(isdn_ctrl *); /* supplied by divert module when calling */
    int (*ll_cmd)(isdn_ctrl *); /* supplied by hl on return */
    char * (*drv_to_name)(int); /* map a driver id to name, supplied by hl */
    int (*name_to_drv)(char *); /* map a driver id to name, supplied by hl */
  } isdn_divert_if;

/*********************/
/* function register */
/*********************/
extern int DIVERT_REG_NAME(isdn_divert_if *);
#endif /* _LINUX_ISDN_DIVERTIF_H */
