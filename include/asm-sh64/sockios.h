#ifndef __ASM_SH64_SOCKIOS_H
#define __ASM_SH64_SOCKIOS_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/sockios.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 *
 */

/* Socket-level I/O control calls. */
#define FIOGETOWN	_IOR('f', 123, int)
#define FIOSETOWN 	_IOW('f', 124, int)

#define SIOCATMARK	_IOR('s', 7, int)
#define SIOCSPGRP	_IOW('s', 8, pid_t)
#define SIOCGPGRP	_IOR('s', 9, pid_t)

#define SIOCGSTAMP	_IOR('s', 100, struct timeval) /* Get stamp - linux-specific */
#endif /* __ASM_SH64_SOCKIOS_H */
