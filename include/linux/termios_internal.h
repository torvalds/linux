/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TERMIOS_CONV_H
#define _LINUX_TERMIOS_CONV_H

#include <linux/uaccess.h>
#include <asm/termios.h>

#ifndef INIT_C_CC
/*	intr=^C		quit=^\		erase=del	kill=^U
	eof=^D		vtime=\0	vmin=\1		sxtc=\0
	start=^Q	stop=^S		susp=^Z		eol=\0
	reprint=^R	discard=^U	werase=^W	lnext=^V
	eol2=\0
*/
#define INIT_C_CC "\003\034\177\025\004\0\1\0\021\023\032\0\022\017\027\026\0"
#endif

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
