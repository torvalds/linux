/* $Id: pconf.h,v 1.1 1996/12/02 00:09:10 davem Exp $
 * pconf.h: pathconf() and fpathconf() defines for SunOS
 *          system call compatibility.
 *
 * Copyright (C) 1995, 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC64_PCONF_H
#define _SPARC64_PCONF_H

#include <linux/fs.h>
#include <linux/limits.h>

#define _PCONF_LINK       1 /* Max number of links to an object        */
#define _PCONF_CANON      2 /* TTY input buffer line size              */
#define _PCONF_INPUT      3 /* Biggest packet a tty can imbibe at once */
#define _PCONF_NAME       4 /* Filename length max                     */
#define _PCONF_PATH       5 /* Max size of a pathname                  */
#define _PCONF_PIPE       6 /* Buffer size for a pipe                  */
#define _PCONF_CHRESTRICT 7 /* Can only root chown files?              */
#define _PCONF_NOTRUNC    8 /* Are pathnames truncated if too big?     */
#define _PCONF_VDISABLE   9 /* Magic char to disable special tty chars */
#define _PCONF_MAXPCONF   9

#endif /* !(_SPARC64_PCONF_H) */
