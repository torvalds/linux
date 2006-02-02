#ifndef __ASM_SH64_IOCTLS_H
#define __ASM_SH64_IOCTLS_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/ioctls.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2004  Richard Curnow
 *
 */

#include <asm/ioctl.h>

#define FIOCLEX		0x6601		/* _IO('f', 1) */
#define FIONCLEX	0x6602		/* _IO('f', 2) */
#define FIOASYNC	0x4004667d	/* _IOW('f', 125, int) */
#define FIONBIO		0x4004667e	/* _IOW('f', 126, int) */
#define FIONREAD	0x8004667f	/* _IOW('f', 127, int) */
#define TIOCINQ		FIONREAD
#define FIOQSIZE	0x80086680	/* _IOR('f', 128, loff_t) */

#define TCGETS		0x5401
#define TCSETS		0x5402
#define TCSETSW		0x5403
#define TCSETSF		0x5404

#define TCGETA		0x80127417	/* _IOR('t', 23, struct termio) */
#define TCSETA		0x40127418	/* _IOW('t', 24, struct termio) */
#define TCSETAW		0x40127419	/* _IOW('t', 25, struct termio) */
#define TCSETAF		0x4012741c	/* _IOW('t', 28, struct termio) */

#define TCSBRK		0x741d		/* _IO('t', 29) */
#define TCXONC		0x741e		/* _IO('t', 30) */
#define TCFLSH		0x741f		/* _IO('t', 31) */

#define TIOCSWINSZ	0x40087467	/* _IOW('t', 103, struct winsize) */
#define TIOCGWINSZ	0x80087468	/* _IOR('t', 104, struct winsize) */
#define	TIOCSTART	0x746e		/* _IO('t', 110)  start output, like ^Q */
#define	TIOCSTOP	0x746f		/* _IO('t', 111)  stop output, like ^S */
#define TIOCOUTQ        0x80047473	/* _IOR('t', 115, int) output queue size */

#define TIOCSPGRP	0x40047476	/* _IOW('t', 118, int) */
#define TIOCGPGRP	0x80047477	/* _IOR('t', 119, int) */

#define TIOCEXCL	0x540c		/* _IO('T', 12) */
#define TIOCNXCL	0x540d		/* _IO('T', 13) */
#define TIOCSCTTY	0x540e		/* _IO('T', 14) */

#define TIOCSTI		0x40015412	/* _IOW('T', 18, char) 0x5412 */
#define TIOCMGET	0x80045415	/* _IOR('T', 21, unsigned int) 0x5415 */
#define TIOCMBIS	0x40045416	/* _IOW('T', 22, unsigned int) 0x5416 */
#define TIOCMBIC	0x40045417	/* _IOW('T', 23, unsigned int) 0x5417 */
#define TIOCMSET	0x40045418	/* _IOW('T', 24, unsigned int) 0x5418 */

#define TIOCM_LE	0x001
#define TIOCM_DTR	0x002
#define TIOCM_RTS	0x004
#define TIOCM_ST	0x008
#define TIOCM_SR	0x010
#define TIOCM_CTS	0x020
#define TIOCM_CAR	0x040
#define TIOCM_RNG	0x080
#define TIOCM_DSR	0x100
#define TIOCM_CD	TIOCM_CAR
#define TIOCM_RI	TIOCM_RNG

#define TIOCGSOFTCAR	0x80045419	/* _IOR('T', 25, unsigned int) 0x5419 */
#define TIOCSSOFTCAR	0x4004541a	/* _IOW('T', 26, unsigned int) 0x541A */
#define TIOCLINUX	0x4004541c	/* _IOW('T', 28, char) 0x541C */
#define TIOCCONS	0x541d		/* _IO('T', 29) */
#define TIOCGSERIAL	0x803c541e	/* _IOR('T', 30, struct serial_struct) 0x541E */
#define TIOCSSERIAL	0x403c541f	/* _IOW('T', 31, struct serial_struct) 0x541F */
#define TIOCPKT		0x40045420	/* _IOW('T', 32, int) 0x5420 */

#define TIOCPKT_DATA		 0
#define TIOCPKT_FLUSHREAD	 1
#define TIOCPKT_FLUSHWRITE	 2
#define TIOCPKT_STOP		 4
#define TIOCPKT_START		 8
#define TIOCPKT_NOSTOP		16
#define TIOCPKT_DOSTOP		32


#define TIOCNOTTY	0x5422		/* _IO('T', 34) */
#define TIOCSETD	0x40045423	/* _IOW('T', 35, int) 0x5423 */
#define TIOCGETD	0x80045424	/* _IOR('T', 36, int) 0x5424 */
#define TCSBRKP		0x40045424	/* _IOW('T', 37, int) 0x5425 */	/* Needed for POSIX tcsendbreak() */
#define TIOCTTYGSTRUCT	0x8c105426	/* _IOR('T', 38, struct tty_struct) 0x5426 */ /* For debugging only */
#define TIOCSBRK	0x5427		/* _IO('T', 39) */ /* BSD compatibility */
#define TIOCCBRK	0x5428		/* _IO('T', 40) */ /* BSD compatibility */
#define TIOCGSID	0x80045429	/* _IOR('T', 41, pid_t) 0x5429 */ /* Return the session ID of FD */
#define TIOCGPTN	0x80045430	/* _IOR('T',0x30, unsigned int) 0x5430 Get Pty Number (of pty-mux device) */
#define TIOCSPTLCK	0x40045431	/* _IOW('T',0x31, int) Lock/unlock Pty */

#define TIOCSERCONFIG	0x5453		/* _IO('T', 83) */
#define TIOCSERGWILD	0x80045454	/* _IOR('T', 84,  int) 0x5454 */
#define TIOCSERSWILD	0x40045455	/* _IOW('T', 85,  int) 0x5455 */
#define TIOCGLCKTRMIOS	0x5456
#define TIOCSLCKTRMIOS	0x5457
#define TIOCSERGSTRUCT	0x80d85458	/* _IOR('T', 88, struct async_struct) 0x5458 */ /* For debugging only */
#define TIOCSERGETLSR   0x80045459	/* _IOR('T', 89, unsigned int) 0x5459 */ /* Get line status register */

/* ioctl (fd, TIOCSERGETLSR, &result) where result may be as below */
#define TIOCSER_TEMT    0x01	/* Transmitter physically empty */

#define TIOCSERGETMULTI 0x80a8545a	/* _IOR('T', 90, struct serial_multiport_struct) 0x545A */ /* Get multiport config  */
#define TIOCSERSETMULTI 0x40a8545b	/* _IOW('T', 91, struct serial_multiport_struct) 0x545B */ /* Set multiport config */

#define TIOCMIWAIT	0x545c		/* _IO('T', 92) wait for a change on serial input line(s) */
#define TIOCGICOUNT	0x545d		/* read serial port inline interrupt counts */

#endif /* __ASM_SH64_IOCTLS_H */
