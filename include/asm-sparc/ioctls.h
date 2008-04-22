#ifndef _ASM_SPARC_IOCTLS_H
#define _ASM_SPARC_IOCTLS_H

#include <asm/ioctl.h>

/* Big T */
#define TCGETA		_IOR('T', 1, struct termio)
#define TCSETA		_IOW('T', 2, struct termio)
#define TCSETAW		_IOW('T', 3, struct termio)
#define TCSETAF		_IOW('T', 4, struct termio)
#define TCSBRK		_IO('T', 5)
#define TCXONC		_IO('T', 6)
#define TCFLSH		_IO('T', 7)
#define TCGETS		_IOR('T', 8, struct termios)
#define TCSETS		_IOW('T', 9, struct termios)
#define TCSETSW		_IOW('T', 10, struct termios)
#define TCSETSF		_IOW('T', 11, struct termios)
#define TCGETS2		_IOR('T', 12, struct termios2)
#define TCSETS2		_IOW('T', 13, struct termios2)
#define TCSETSW2	_IOW('T', 14, struct termios2)
#define TCSETSF2	_IOW('T', 15, struct termios2)

/* Note that all the ioctls that are not available in Linux have a 
 * double underscore on the front to: a) avoid some programs to
 * thing we support some ioctls under Linux (autoconfiguration stuff)
 */
/* Little t */
#define TIOCGETD	_IOR('t', 0, int)
#define TIOCSETD	_IOW('t', 1, int)
#define __TIOCHPCL        _IO('t', 2) /* SunOS Specific */
#define __TIOCMODG        _IOR('t', 3, int) /* SunOS Specific */
#define __TIOCMODS        _IOW('t', 4, int) /* SunOS Specific */
#define __TIOCGETP        _IOR('t', 8, struct sgttyb) /* SunOS Specific */
#define __TIOCSETP        _IOW('t', 9, struct sgttyb) /* SunOS Specific */
#define __TIOCSETN        _IOW('t', 10, struct sgttyb) /* SunOS Specific */
#define TIOCEXCL	_IO('t', 13)
#define TIOCNXCL	_IO('t', 14)
#define __TIOCFLUSH       _IOW('t', 16, int) /* SunOS Specific */
#define __TIOCSETC        _IOW('t', 17, struct tchars) /* SunOS Specific */
#define __TIOCGETC        _IOR('t', 18, struct tchars) /* SunOS Specific */
#define __TIOCTCNTL       _IOW('t', 32, int) /* SunOS Specific */
#define __TIOCSIGNAL      _IOW('t', 33, int) /* SunOS Specific */
#define __TIOCSETX        _IOW('t', 34, int) /* SunOS Specific */
#define __TIOCGETX        _IOR('t', 35, int) /* SunOS Specific */
#define TIOCCONS	_IO('t', 36)
#define TIOCGSOFTCAR	_IOR('t', 100, int)
#define TIOCSSOFTCAR	_IOW('t', 101, int)
#define __TIOCUCNTL       _IOW('t', 102, int) /* SunOS Specific */
#define TIOCSWINSZ	_IOW('t', 103, struct winsize)
#define TIOCGWINSZ	_IOR('t', 104, struct winsize)
#define __TIOCREMOTE      _IOW('t', 105, int) /* SunOS Specific */
#define TIOCMGET	_IOR('t', 106, int)
#define TIOCMBIC	_IOW('t', 107, int)
#define TIOCMBIS	_IOW('t', 108, int)
#define TIOCMSET	_IOW('t', 109, int)
#define TIOCSTART       _IO('t', 110)
#define TIOCSTOP        _IO('t', 111)
#define TIOCPKT		_IOW('t', 112, int)
#define TIOCNOTTY	_IO('t', 113)
#define TIOCSTI		_IOW('t', 114, char)
#define TIOCOUTQ	_IOR('t', 115, int)
#define __TIOCGLTC        _IOR('t', 116, struct ltchars) /* SunOS Specific */
#define __TIOCSLTC        _IOW('t', 117, struct ltchars) /* SunOS Specific */
/* 118 is the non-posix setpgrp tty ioctl */
/* 119 is the non-posix getpgrp tty ioctl */
#define __TIOCCDTR        _IO('t', 120) /* SunOS Specific */
#define __TIOCSDTR        _IO('t', 121) /* SunOS Specific */
#define TIOCCBRK        _IO('t', 122)
#define TIOCSBRK        _IO('t', 123)
#define __TIOCLGET        _IOW('t', 124, int) /* SunOS Specific */
#define __TIOCLSET        _IOW('t', 125, int) /* SunOS Specific */
#define __TIOCLBIC        _IOW('t', 126, int) /* SunOS Specific */
#define __TIOCLBIS        _IOW('t', 127, int) /* SunOS Specific */
#define __TIOCISPACE      _IOR('t', 128, int) /* SunOS Specific */
#define __TIOCISIZE       _IOR('t', 129, int) /* SunOS Specific */
#define TIOCSPGRP	_IOW('t', 130, int)
#define TIOCGPGRP	_IOR('t', 131, int)
#define TIOCSCTTY	_IO('t', 132)
#define TIOCGSID	_IOR('t', 133, int)
/* Get minor device of a pty master's FD -- Solaris equiv is ISPTM */
#define TIOCGPTN	_IOR('t', 134, unsigned int) /* Get Pty Number */
#define TIOCSPTLCK	_IOW('t', 135, int) /* Lock/unlock PTY */

/* Little f */
#define FIOCLEX		_IO('f', 1)
#define FIONCLEX	_IO('f', 2)
#define FIOASYNC	_IOW('f', 125, int)
#define FIONBIO		_IOW('f', 126, int)
#define FIONREAD	_IOR('f', 127, int)
#define TIOCINQ		FIONREAD
#define FIOQSIZE	_IOR('f', 128, loff_t)

/* SCARY Rutgers local SunOS kernel hackery, perhaps I will support it
 * someday.  This is completely bogus, I know...
 */
#define __TCGETSTAT       _IO('T', 200) /* Rutgers specific */
#define __TCSETSTAT       _IO('T', 201) /* Rutgers specific */

/* Linux specific, no SunOS equivalent. */
#define TIOCLINUX	0x541C
#define TIOCGSERIAL	0x541E
#define TIOCSSERIAL	0x541F
#define TCSBRKP		0x5425
#define TIOCSERCONFIG	0x5453
#define TIOCSERGWILD	0x5454
#define TIOCSERSWILD	0x5455
#define TIOCGLCKTRMIOS	0x5456
#define TIOCSLCKTRMIOS	0x5457
#define TIOCSERGSTRUCT	0x5458 /* For debugging only */
#define TIOCSERGETLSR   0x5459 /* Get line status register */
#define TIOCSERGETMULTI 0x545A /* Get multiport config  */
#define TIOCSERSETMULTI 0x545B /* Set multiport config */
#define TIOCMIWAIT	0x545C /* Wait input */
#define TIOCGICOUNT	0x545D /* Read serial port inline interrupt counts */

/* Kernel definitions */
#ifdef __KERNEL__
#define TIOCGETC __TIOCGETC
#define TIOCGETP __TIOCGETP
#define TIOCGLTC __TIOCGLTC
#define TIOCSLTC __TIOCSLTC
#define TIOCSETP __TIOCSETP
#define TIOCSETN __TIOCSETN
#define TIOCSETC __TIOCSETC
#endif

/* Used for packet mode */
#define TIOCPKT_DATA		 0
#define TIOCPKT_FLUSHREAD	 1
#define TIOCPKT_FLUSHWRITE	 2
#define TIOCPKT_STOP		 4
#define TIOCPKT_START		 8
#define TIOCPKT_NOSTOP		16
#define TIOCPKT_DOSTOP		32

#endif /* !(_ASM_SPARC_IOCTLS_H) */
