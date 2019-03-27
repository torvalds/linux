/*-
 * Copyright (c) 2010, Oracle America, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the "Oracle America, Inc." nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 *   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Remote execution (rex) protocol specification
 */

#ifndef RPC_HDR
%#ifndef lint
%/*static char sccsid[] = "from: @(#)rex.x 1.3 87/09/18 Copyr 1987 Sun Micro";*/
%/*static char sccsid[] = "from: @(#)rex.x	2.1 88/08/01 4.0 RPCSRC";*/
%#endif /* not lint */
%#include <sys/cdefs.h>
%__FBSDID("$FreeBSD$");
#endif

const STRINGSIZE = 1024;
typedef string rexstring<1024>;

/*
 * values to pass to REXPROC_SIGNAL
 */
const SIGINT = 2;	/* interrupt */

/*
 * Values for rst_flags, below 
 */
const REX_INTERACTIVE = 1;	/* interactive mode */

struct rex_start {
	rexstring rst_cmd<>;	/* list of command and args */
	rexstring rst_host;	/* working directory host name */
	rexstring rst_fsname;	/* working directory file system name */
	rexstring rst_dirwithin;/* working directory within file system */
	rexstring rst_env<>;	/* list of environment */
	unsigned int rst_port0;	/* port for stdin */
	unsigned int rst_port1;	/* port for stdout */
	unsigned int rst_port2;	/* port for stderr */
	unsigned int rst_flags;	/* options - see const above */
};

struct rex_result {
   	int rlt_stat;		/* integer status code */
	rexstring rlt_message;	/* string message for human consumption */
};


struct sgttyb {
	unsigned four;	/* always equals 4 */
	opaque chars[4];
	/* chars[0] == input speed */
	/* chars[1] == output speed */
	/* chars[2] == kill character */
	/* chars[3] == erase character */
	unsigned flags;
};
/* values for speeds above (baud rates)  */
const B0  = 0;
const B50 = 1;
const B75 = 2;
const B110 = 3;
const B134 = 4;
const B150 = 5;
const B200 = 6;
const B300 = 7;
const B600 = 8;
const B1200 = 9;
const B1800 = 10;
const B2400 = 11;
const B4800 = 12;
const B9600 = 13;
const B19200 = 14;
const B38400 = 15;

/* values for flags above */
const TANDEM = 0x00000001; /* send stopc on out q full */
const CBREAK = 0x00000002; /* half-cooked mode */
const LCASE = 0x00000004; /* simulate lower case */
const ECHO = 0x00000008; /* echo input */
const CRMOD = 0x00000010; /* map \r to \r\n on output */
const RAW = 0x00000020; /* no i/o processing */
const ODDP = 0x00000040; /* get/send odd parity */
const EVENP = 0x00000080; /* get/send even parity */
const ANYP = 0x000000c0; /* get any parity/send none */
const NLDELAY = 0x00000300; /* \n delay */
const  NL0 = 0x00000000;
const  NL1 = 0x00000100; /* tty 37 */
const  NL2 = 0x00000200; /* vt05 */
const  NL3 = 0x00000300;
const TBDELAY = 0x00000c00; /* horizontal tab delay */
const  TAB0 = 0x00000000;
const  TAB1 = 0x00000400; /* tty 37 */
const  TAB2 = 0x00000800;
const XTABS = 0x00000c00; /* expand tabs on output */
const CRDELAY = 0x00003000; /* \r delay */
const  CR0 = 0x00000000;
const  CR1 = 0x00001000; /* tn 300 */
const  CR2 = 0x00002000; /* tty 37 */
const  CR3 = 0x00003000; /* concept 100 */
const VTDELAY = 0x00004000; /* vertical tab delay */
const  FF0 = 0x00000000;
const  FF1 = 0x00004000; /* tty 37 */
const BSDELAY = 0x00008000; /* \b delay */
const  BS0 = 0x00000000;
const  BS1 = 0x00008000;
const CRTBS = 0x00010000; /* do backspacing for crt */
const PRTERA = 0x00020000; /* \ ... / erase */
const CRTERA = 0x00040000; /* " \b " to wipe out char */
const TILDE = 0x00080000; /* hazeltine tilde kludge */
const MDMBUF = 0x00100000; /* start/stop output on carrier intr */
const LITOUT = 0x00200000; /* literal output */
const TOSTOP = 0x00400000; /* SIGTTOU on background output */
const FLUSHO = 0x00800000; /* flush output to terminal */
const NOHANG = 0x01000000; /* no SIGHUP on carrier drop */
const L001000 = 0x02000000;
const CRTKIL = 0x04000000; /* kill line with " \b " */
const PASS8 = 0x08000000;
const CTLECH = 0x10000000; /* echo control chars as ^X */
const PENDIN = 0x20000000; /* tp->t_rawq needs reread */
const DECCTQ = 0x40000000; /* only ^Q starts after ^S */
const NOFLSH = 0x80000000; /* no output flush on signal */

struct tchars {
	unsigned six;	/* always equals 6 */
	opaque chars[6];
	/* chars[0] == interrupt char */
	/* chars[1] == quit char */
	/* chars[2] == start output char */
	/* chars[3] == stop output char */
	/* chars[4] == end-of-file char */
	/* chars[5] == input delimeter (like nl) */
};

struct ltchars {
	unsigned six;	/* always equals 6 */
	opaque chars[6];
	/* chars[0] == stop process signal */
	/* chars[1] == delayed stop process signal */
	/* chars[2] == reprint line */
	/* chars[3] == flush output */
	/* chars[4] == word erase */
	/* chars[5] == literal next character */
	unsigned mode;
};

struct rex_ttysize {
	int ts_lines;
	int ts_cols;
};

struct rex_ttymode {
    sgttyb basic;    /* standard unix tty flags */
    tchars more; /* interrupt, kill characters, etc. */
    ltchars yetmore; /* special Berkeley characters */
    unsigned andmore;     /* and Berkeley modes */
};

/* values for andmore above */
const LCRTBS = 0x0001;	/* do backspacing for crt */
const LPRTERA = 0x0002;	/* \ ... / erase */
const LCRTERA = 0x0004;	/* " \b " to wipe out char */
const LTILDE = 0x0008;	/* hazeltine tilde kludge */
const LMDMBUF = 0x0010;	/* start/stop output on carrier intr */
const LLITOUT = 0x0020;	/* literal output */
const LTOSTOP = 0x0040;	/* SIGTTOU on background output */
const LFLUSHO = 0x0080;	/* flush output to terminal */
const LNOHANG = 0x0100;	/* no SIGHUP on carrier drop */
const LL001000 = 0x0200;
const LCRTKIL = 0x0400;	/* kill line with " \b " */
const LPASS8 = 0x0800;
const LCTLECH = 0x1000;	/* echo control chars as ^X */
const LPENDIN = 0x2000;	/* needs reread */
const LDECCTQ = 0x4000;	/* only ^Q starts after ^S */
const LNOFLSH = 0x8000;	/* no output flush on signal */

program REXPROG {
	version REXVERS {

		/*
		 * Start remote execution
		 */
		rex_result 
		REXPROC_START(rex_start) = 1;

		/*
		 * Wait for remote execution to terminate
		 */
		rex_result
		REXPROC_WAIT(void) = 2;

		/*
		 * Send tty modes
		 */
		void
		REXPROC_MODES(rex_ttymode) = 3;

		/*
		 * Send window size change
		 */
		void
		REXPROC_WINCH(rex_ttysize) = 4;

		/*
		 * Send other signal
		 */
		void
		REXPROC_SIGNAL(int) = 5;
	} = 1;
} = 100017;
