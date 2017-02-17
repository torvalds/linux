/* termios.h: generic termios/termio user copying/translation
 */

#ifndef _ASM_GENERIC_TERMIOS_BASE_H
#define _ASM_GENERIC_TERMIOS_BASE_H

#include <linux/uaccess.h>

#ifndef __ARCH_TERMIO_GETPUT

/*
 * Translate a "termio" structure into a "termios". Ugh.
 */
static inline int user_termio_to_kernel_termios(struct ktermios *termios,
						struct termio __user *termio)
{
	unsigned short tmp;

	if (get_user(tmp, &termio->c_iflag) < 0)
		goto fault;
	termios->c_iflag = (0xffff0000 & termios->c_iflag) | tmp;

	if (get_user(tmp, &termio->c_oflag) < 0)
		goto fault;
	termios->c_oflag = (0xffff0000 & termios->c_oflag) | tmp;

	if (get_user(tmp, &termio->c_cflag) < 0)
		goto fault;
	termios->c_cflag = (0xffff0000 & termios->c_cflag) | tmp;

	if (get_user(tmp, &termio->c_lflag) < 0)
		goto fault;
	termios->c_lflag = (0xffff0000 & termios->c_lflag) | tmp;

	if (get_user(termios->c_line, &termio->c_line) < 0)
		goto fault;

	if (copy_from_user(termios->c_cc, termio->c_cc, NCC) != 0)
		goto fault;

	return 0;

 fault:
	return -EFAULT;
}

/*
 * Translate a "termios" structure into a "termio". Ugh.
 */
static inline int kernel_termios_to_user_termio(struct termio __user *termio,
						struct ktermios *termios)
{
	if (put_user(termios->c_iflag, &termio->c_iflag) < 0 ||
	    put_user(termios->c_oflag, &termio->c_oflag) < 0 ||
	    put_user(termios->c_cflag, &termio->c_cflag) < 0 ||
	    put_user(termios->c_lflag, &termio->c_lflag) < 0 ||
	    put_user(termios->c_line,  &termio->c_line) < 0 ||
	    copy_to_user(termio->c_cc, termios->c_cc, NCC) != 0)
		return -EFAULT;

	return 0;
}

#ifndef user_termios_to_kernel_termios
#define user_termios_to_kernel_termios(k, u) copy_from_user(k, u, sizeof(struct termios))
#endif

#ifndef kernel_termios_to_user_termios
#define kernel_termios_to_user_termios(u, k) copy_to_user(u, k, sizeof(struct termios))
#endif

#define user_termios_to_kernel_termios_1(k, u) copy_from_user(k, u, sizeof(struct termios))
#define kernel_termios_to_user_termios_1(u, k) copy_to_user(u, k, sizeof(struct termios))

#endif	/* __ARCH_TERMIO_GETPUT */

#endif /* _ASM_GENERIC_TERMIOS_BASE_H */
