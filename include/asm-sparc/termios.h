/* $Id: termios.h,v 1.32 2001/06/01 08:12:11 davem Exp $ */
#ifndef _SPARC_TERMIOS_H
#define _SPARC_TERMIOS_H

#include <asm/ioctls.h>
#include <asm/termbits.h>

#if defined(__KERNEL__) || defined(__DEFINE_BSD_TERMIOS)
struct sgttyb {
	char	sg_ispeed;
	char	sg_ospeed;
	char	sg_erase;
	char	sg_kill;
	short	sg_flags;
};

struct tchars {
	char	t_intrc;
	char	t_quitc;
	char	t_startc;
	char	t_stopc;
	char	t_eofc;
	char	t_brkc;
};

struct ltchars {
	char	t_suspc;
	char	t_dsuspc;
	char	t_rprntc;
	char	t_flushc;
	char	t_werasc;
	char	t_lnextc;
};
#endif /* __KERNEL__ */

struct sunos_ttysize {
	int st_lines;   /* Lines on the terminal */
	int st_columns; /* Columns on the terminal */
};

struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};

/* line disciplines */
#define N_TTY		0
#define N_SLIP		1
#define N_MOUSE		2
#define N_PPP		3
#define N_STRIP		4
#define N_AX25		5
#define N_X25		6
#define N_6PACK		7
#define N_MASC		8	/* Reserved for Mobitex module <kaz@cafe.net> */
#define N_R3964		9	/* Reserved for Simatic R3964 module */
#define N_PROFIBUS_FDL	10	/* Reserved for Profibus <Dave@mvhi.com> */
#define N_IRDA		11	/* Linux IrDa - http://irda.sourceforge.net/ */
#define N_SMSBLOCK	12	/* SMS block mode - for talking to GSM data cards about SMS messages */
#define N_HDLC		13	/* synchronous HDLC */
#define N_SYNC_PPP	14	/* synchronous PPP */
#define N_HCI		15  /* Bluetooth HCI UART */

#ifdef __KERNEL__
#include <linux/module.h>

/*
 * c_cc characters in the termio structure.  Oh, how I love being
 * backwardly compatible.  Notice that character 4 and 5 are
 * interpreted differently depending on whether ICANON is set in
 * c_lflag.  If it's set, they are used as _VEOF and _VEOL, otherwise
 * as _VMIN and V_TIME.  This is for compatibility with OSF/1 (which
 * is compatible with sysV)...
 */
#define _VMIN	4
#define _VTIME	5


/*	intr=^C		quit=^\		erase=del	kill=^U
	eof=^D		eol=\0		eol2=\0		sxtc=\0
	start=^Q	stop=^S		susp=^Z		dsusp=^Y
	reprint=^R	discard=^U	werase=^W	lnext=^V
	vmin=\1         vtime=\0
*/
#define INIT_C_CC "\003\034\177\025\004\000\000\000\021\023\032\031\022\025\027\026\001"

/*
 * Translate a "termio" structure into a "termios". Ugh.
 */
#define user_termio_to_kernel_termios(termios, termio) \
({ \
	unsigned short tmp; \
	get_user(tmp, &(termio)->c_iflag); \
	(termios)->c_iflag = (0xffff0000 & ((termios)->c_iflag)) | tmp; \
	get_user(tmp, &(termio)->c_oflag); \
	(termios)->c_oflag = (0xffff0000 & ((termios)->c_oflag)) | tmp; \
	get_user(tmp, &(termio)->c_cflag); \
	(termios)->c_cflag = (0xffff0000 & ((termios)->c_cflag)) | tmp; \
	get_user(tmp, &(termio)->c_lflag); \
	(termios)->c_lflag = (0xffff0000 & ((termios)->c_lflag)) | tmp; \
	copy_from_user((termios)->c_cc, (termio)->c_cc, NCC); \
	0; \
})

/*
 * Translate a "termios" structure into a "termio". Ugh.
 *
 * Note the "fun" _VMIN overloading.
 */
#define kernel_termios_to_user_termio(termio, termios) \
({ \
	put_user((termios)->c_iflag, &(termio)->c_iflag); \
	put_user((termios)->c_oflag, &(termio)->c_oflag); \
	put_user((termios)->c_cflag, &(termio)->c_cflag); \
	put_user((termios)->c_lflag, &(termio)->c_lflag); \
	put_user((termios)->c_line,  &(termio)->c_line); \
	copy_to_user((termio)->c_cc, (termios)->c_cc, NCC); \
	if (!((termios)->c_lflag & ICANON)) { \
		put_user((termios)->c_cc[VMIN], &(termio)->c_cc[_VMIN]); \
		put_user((termios)->c_cc[VTIME], &(termio)->c_cc[_VTIME]); \
	} \
	0; \
})

#define user_termios_to_kernel_termios(k, u) \
({ \
	get_user((k)->c_iflag, &(u)->c_iflag); \
	get_user((k)->c_oflag, &(u)->c_oflag); \
	get_user((k)->c_cflag, &(u)->c_cflag); \
	get_user((k)->c_lflag, &(u)->c_lflag); \
	get_user((k)->c_line,  &(u)->c_line); \
	copy_from_user((k)->c_cc, (u)->c_cc, NCCS); \
	if((k)->c_lflag & ICANON) { \
		get_user((k)->c_cc[VEOF], &(u)->c_cc[VEOF]); \
		get_user((k)->c_cc[VEOL], &(u)->c_cc[VEOL]); \
	} else { \
		get_user((k)->c_cc[VMIN],  &(u)->c_cc[_VMIN]); \
		get_user((k)->c_cc[VTIME], &(u)->c_cc[_VTIME]); \
	} \
	0; \
})

#define kernel_termios_to_user_termios(u, k) \
({ \
	put_user((k)->c_iflag, &(u)->c_iflag); \
	put_user((k)->c_oflag, &(u)->c_oflag); \
	put_user((k)->c_cflag, &(u)->c_cflag); \
	put_user((k)->c_lflag, &(u)->c_lflag); \
	put_user((k)->c_line, &(u)->c_line); \
	copy_to_user((u)->c_cc, (k)->c_cc, NCCS); \
	if(!((k)->c_lflag & ICANON)) { \
		put_user((k)->c_cc[VMIN],  &(u)->c_cc[_VMIN]); \
		put_user((k)->c_cc[VTIME], &(u)->c_cc[_VTIME]); \
	} else { \
		put_user((k)->c_cc[VEOF], &(u)->c_cc[VEOF]); \
		put_user((k)->c_cc[VEOL], &(u)->c_cc[VEOL]); \
	} \
	0; \
})

#endif	/* __KERNEL__ */

#endif /* _SPARC_TERMIOS_H */
