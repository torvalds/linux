#ifndef _ASM_GENERIC_TERMIOS_H
#define _ASM_GENERIC_TERMIOS_H


#include <linux/uaccess.h>
#include <uapi/asm-generic/termios.h>

/*	intr=^C		quit=^\		erase=del	kill=^U
	eof=^D		vtime=\0	vmin=\1		sxtc=\0
	start=^Q	stop=^S		susp=^Z		eol=\0
	reprint=^R	discard=^U	werase=^W	lnext=^V
	eol2=\0
*/
#define INIT_C_CC "\003\034\177\025\004\0\1\0\021\023\032\0\022\017\027\026\0"

/*
 * Translate a "termio" structure into a "termios". Ugh.
 */
static inline int user_termio_to_kernel_termios(struct ktermios *termios,
						const struct termio __user *termio)
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

#ifdef TCGETS2
static inline int user_termios_to_kernel_termios(struct ktermios *k,
						 struct termios2 __user *u)
{
	return copy_from_user(k, u, sizeof(struct termios2));
}

static inline int kernel_termios_to_user_termios(struct termios2 __user *u,
						 struct ktermios *k)
{
	return copy_to_user(u, k, sizeof(struct termios2));
}

static inline int user_termios_to_kernel_termios_1(struct ktermios *k,
						   struct termios __user *u)
{
	return copy_from_user(k, u, sizeof(struct termios));
}

static inline int kernel_termios_to_user_termios_1(struct termios __user *u,
						   struct ktermios *k)
{
	return copy_to_user(u, k, sizeof(struct termios));
}
#else /* TCGETS2 */
static inline int user_termios_to_kernel_termios(struct ktermios *k,
						 struct termios __user *u)
{
	return copy_from_user(k, u, sizeof(struct termios));
}

static inline int kernel_termios_to_user_termios(struct termios __user *u,
						 struct ktermios *k)
{
	return copy_to_user(u, k, sizeof(struct termios));
}
#endif /* TCGETS2 */

#endif /* _ASM_GENERIC_TERMIOS_H */
