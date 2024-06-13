/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TERMIOS_CONV_H
#define _LINUX_TERMIOS_CONV_H

#include <linux/uaccess.h>
#include <asm/termios.h>

/*	intr=^C		quit=^\		erase=del	kill=^U
	eof=^D		vtime=\0	vmin=\1		sxtc=\0
	start=^Q	stop=^S		susp=^Z		eol=\0
	reprint=^R	discard=^O	werase=^W	lnext=^V
	eol2=\0
*/

#ifdef VDSUSP
#define INIT_C_CC_VDSUSP_EXTRA [VDSUSP] = 'Y'-0x40,
#else
#define INIT_C_CC_VDSUSP_EXTRA
#endif

#define INIT_C_CC {		\
	[VINTR] = 'C'-0x40,	\
	[VQUIT] = '\\'-0x40,	\
	[VERASE] = '\177',	\
	[VKILL] = 'U'-0x40,	\
	[VEOF] = 'D'-0x40,	\
	[VSTART] = 'Q'-0x40,	\
	[VSTOP] = 'S'-0x40,	\
	[VSUSP] = 'Z'-0x40,	\
	[VREPRINT] = 'R'-0x40,	\
	[VDISCARD] = 'O'-0x40,	\
	[VWERASE] = 'W'-0x40,	\
	[VLNEXT] = 'V'-0x40,	\
	INIT_C_CC_VDSUSP_EXTRA	\
	[VMIN] = 1 }

int user_termio_to_kernel_termios(struct ktermios *, struct termio __user *);
int kernel_termios_to_user_termio(struct termio __user *, struct ktermios *);
#ifdef TCGETS2
int user_termios_to_kernel_termios(struct ktermios *, struct termios2 __user *);
int kernel_termios_to_user_termios(struct termios2 __user *, struct ktermios *);
int user_termios_to_kernel_termios_1(struct ktermios *, struct termios __user *);
int kernel_termios_to_user_termios_1(struct termios __user *, struct ktermios *);
#else /* TCGETS2 */
int user_termios_to_kernel_termios(struct ktermios *, struct termios __user *);
int kernel_termios_to_user_termios(struct termios __user *, struct ktermios *);
#endif /* TCGETS2 */

#endif /* _LINUX_TERMIOS_CONV_H */
