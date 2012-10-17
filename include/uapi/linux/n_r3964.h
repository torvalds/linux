/* r3964 linediscipline for linux
 *
 * -----------------------------------------------------------
 * Copyright by
 * Philips Automation Projects
 * Kassel (Germany)
 * -----------------------------------------------------------
 * This software may be used and distributed according to the terms of
 * the GNU General Public License, incorporated herein by reference.
 *
 * Author:
 * L. Haag
 *
 * $Log: r3964.h,v $
 * Revision 1.4  2005/12/21 19:54:24  Kurt Huwig <kurt huwig de>
 * Fixed HZ usage on 2.6 kernels
 * Removed unnecessary include
 *
 * Revision 1.3  2001/03/18 13:02:24  dwmw2
 * Fix timer usage, use spinlocks properly.
 *
 * Revision 1.2  2001/03/18 12:53:15  dwmw2
 * Merge changes in 2.4.2
 *
 * Revision 1.1.1.1  1998/10/13 16:43:14  dwmw2
 * This'll screw the version control
 *
 * Revision 1.6  1998/09/30 00:40:38  dwmw2
 * Updated to use kernel's N_R3964 if available
 *
 * Revision 1.4  1998/04/02 20:29:44  lhaag
 * select, blocking, ...
 *
 * Revision 1.3  1998/02/12 18:58:43  root
 * fixed some memory leaks
 * calculation of checksum characters
 *
 * Revision 1.2  1998/02/07 13:03:17  root
 * ioctl read_telegram
 *
 * Revision 1.1  1998/02/06 19:19:43  root
 * Initial revision
 *
 *
 */

#ifndef _UAPI__LINUX_N_R3964_H__
#define _UAPI__LINUX_N_R3964_H__

/* line disciplines for r3964 protocol */


/*
 * Ioctl-commands
 */

#define R3964_ENABLE_SIGNALS      0x5301
#define R3964_SETPRIORITY         0x5302
#define R3964_USE_BCC             0x5303
#define R3964_READ_TELEGRAM       0x5304

/* Options for R3964_SETPRIORITY */
#define R3964_MASTER   0
#define R3964_SLAVE    1

/* Options for R3964_ENABLE_SIGNALS */
#define R3964_SIG_ACK   0x0001
#define R3964_SIG_DATA  0x0002
#define R3964_SIG_ALL   0x000f
#define R3964_SIG_NONE  0x0000
#define R3964_USE_SIGIO 0x1000

/*
 * r3964 operation states:
 */

/* types for msg_id: */
enum {R3964_MSG_ACK=1, R3964_MSG_DATA };

#define R3964_MAX_MSG_COUNT 32

/* error codes for client messages */
#define R3964_OK 0        /* no error. */
#define R3964_TX_FAIL -1  /* transmission error, block NOT sent */
#define R3964_OVERFLOW -2 /* msg queue overflow */

/* the client gets this struct when calling read(fd,...): */
struct r3964_client_message {
	  int     msg_id;
	  int     arg;
	  int     error_code;
};

#define R3964_MTU      256



#endif /* _UAPI__LINUX_N_R3964_H__ */
