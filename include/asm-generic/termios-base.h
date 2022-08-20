/* SPDX-License-Identifier: GPL-2.0 */
/* termios.h: generic termios/termio user copying/translation
 */

#ifndef _ASM_GENERIC_TERMIOS_BASE_H
#define _ASM_GENERIC_TERMIOS_BASE_H

#include <linux/uaccess.h>

#ifndef __ARCH_TERMIO_GETPUT

int user_termio_to_kernel_termios(struct ktermios *, struct termio __user *);
int kernel_termios_to_user_termio(struct termio __user *, struct ktermios *);
int user_termios_to_kernel_termios(struct ktermios *, struct termios2 __user *);
int kernel_termios_to_user_termios(struct termios2 __user *, struct ktermios *);
int user_termios_to_kernel_termios_1(struct ktermios *, struct termios __user *);
int kernel_termios_to_user_termios_1(struct termios __user *, struct ktermios *);

#endif	/* __ARCH_TERMIO_GETPUT */

#endif /* _ASM_GENERIC_TERMIOS_BASE_H */
