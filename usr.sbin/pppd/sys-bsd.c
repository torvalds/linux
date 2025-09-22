/*	$OpenBSD: sys-bsd.c,v 1.37 2024/11/04 11:12:52 deraadt Exp $	*/

/*
 * sys-bsd.c - System-dependent procedures for setting up
 * PPP interfaces on bsd-4.4-ish systems (including 386BSD, NetBSD, etc.)
 *
 * Copyright (c) 1984-2000 Carnegie Mellon University. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The name "Carnegie Mellon University" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For permission or any legal
 *    details, please contact
 *      Office of Technology Transfer
 *      Carnegie Mellon University
 *      5000 Forbes Avenue
 *      Pittsburgh, PA  15213-3890
 *      (412) 268-4387, fax: (412) 268-7395
 *      tech-transfer@andrew.cmu.edu
 *
 * 4. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by Computing Services
 *     at Carnegie Mellon University (http://www.cmu.edu/computing/)."
 *
 * CARNEGIE MELLON UNIVERSITY DISCLAIMS ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS, IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY BE LIABLE
 * FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Copyright (c) 1989-2002 Paul Mackerras. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The name(s) of the authors of this software must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission.
 *
 * 4. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by Paul Mackerras
 *     <paulus@samba.org>".
 *
 * THE AUTHORS OF THIS SOFTWARE DISCLAIM ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS, IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * TODO:
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/stat.h>

#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <util.h>
#include <ifaddrs.h>

#ifdef PPP_FILTER
#include <net/bpf.h>
#endif
#include <net/if.h>
#include <net/ppp_defs.h>
#include <net/if_ppp.h>
#include <net/route.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>

#include "pppd.h"
#include "fsm.h"
#include "ipcp.h"

#define ok_error(num) ((num)==EIO)

static int initdisc = -1;	/* Initial TTY discipline for ppp_fd */
static int initfdflags = -1;	/* Initial file descriptor flags for ppp_fd */
static int ppp_fd = -1;		/* fd which is set to PPP discipline */
static int rtm_seq;

static int restore_term;	/* 1 => we've munged the terminal */
static struct termios inittermios; /* Initial TTY termios */
static struct winsize wsinfo;	/* Initial window size info */

static char *lock_file;		/* name of lock file created */

static int loop_slave = -1;
static int loop_master;
static char loop_name[20];

static unsigned char inbuf[512]; /* buffer for chars read from loopback */

static int sockfd;		/* socket for doing interface ioctls */

static int if_is_up;		/* the interface is currently up */
static u_int32_t ifaddrs[2];	/* local and remote addresses we set */
static u_int32_t default_route_gateway;	/* gateway addr for default route */
static u_int32_t proxy_arp_addr;	/* remote addr for proxy arp */

/* Prototypes for procedures local to this file. */
static int dodefaultroute(u_int32_t, int);
static int get_ether_addr(u_int32_t, struct sockaddr_dl *);


/*
 * sys_init - System-dependent initialization.
 */
void
sys_init(void)
{
    /* Get an internet socket for doing socket ioctl's on. */
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
	syslog(LOG_ERR, "Couldn't create IP socket: %m");
	die(1);
    }
}

/*
 * sys_cleanup - restore any system state we modified before exiting:
 * mark the interface down, delete default route and/or proxy arp entry.
 * This should call die() because it's called from die().
 */
void
sys_cleanup(void)
{
    struct ifreq ifr;

    if (if_is_up) {
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(sockfd, SIOCGIFFLAGS, &ifr) == 0
	    && ((ifr.ifr_flags & IFF_UP) != 0)) {
	    ifr.ifr_flags &= ~IFF_UP;
	    ioctl(sockfd, SIOCSIFFLAGS, &ifr);
	}
    }
    if (ifaddrs[0] != 0)
	cifaddr(0, ifaddrs[0], ifaddrs[1]);
    if (default_route_gateway)
	cifdefaultroute(0, 0, default_route_gateway);
    if (proxy_arp_addr)
	cifproxyarp(0, proxy_arp_addr);
}

/*
 * sys_close - Clean up in a child process before execing.
 */
void
sys_close(void)
{
    close(sockfd);
    if (loop_slave >= 0) {
	close(loop_slave);
	close(loop_master);
    }
}

/*
 * sys_check_options - check the options that the user specified
 */
void
sys_check_options(void)
{
}

/*
 * ppp_available - check whether the system has any ppp interfaces
 * (in fact we check whether we can do an ioctl on ppp0).
 */
int
ppp_available(void)
{
    int s, ok;
    struct ifreq ifr;
    extern char *no_ppp_msg;

    if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
	return 1;		/* can't tell */

    strlcpy(ifr.ifr_name, "ppp0", sizeof(ifr.ifr_name));
    ok = ioctl(s, SIOCGIFFLAGS, (caddr_t) &ifr) == 0;
    close(s);

    no_ppp_msg = "\
PPP device not available. Make sure the device is created with\n\
ifconfig and that the kernel supports PPP. See ifconfig(8) and ppp(4).";
    return ok;
}

/*
 * establish_ppp - Turn the serial port into a ppp interface.
 */
void
establish_ppp(int fd)
{
    int pppdisc = PPPDISC;
    int x;

    if (demand) {
	/*
	 * Demand mode - prime the old ppp device to relinquish the unit.
	 */
	if (ioctl(ppp_fd, PPPIOCXFERUNIT, 0) == -1) {
	    syslog(LOG_ERR, "ioctl(transfer ppp unit): %m");
	    die(1);
	}
    }

    /*
     * Save the old line discipline of fd, and set it to PPP.
     */
    if (ioctl(fd, TIOCGETD, &initdisc) == -1) {
	syslog(LOG_ERR, "ioctl(TIOCGETD): %m");
	die(1);
    }
    if (ioctl(fd, TIOCSETD, &pppdisc) == -1) {
	syslog(LOG_ERR, "ioctl(TIOCSETD): %m");
	die(1);
    }

    if (!demand) {
	/*
	 * Find out which interface we were given.
	 */
	if (ioctl(fd, PPPIOCGUNIT, &ifunit) == -1) {	
	    syslog(LOG_ERR, "ioctl(PPPIOCGUNIT): %m");
	    die(1);
	}
    } else {
	/*
	 * Check that we got the same unit again.
	 */
	if (ioctl(fd, PPPIOCGUNIT, &x) == -1) {	
	    syslog(LOG_ERR, "ioctl(PPPIOCGUNIT): %m");
	    die(1);
	}
	if (x != ifunit) {
	    syslog(LOG_ERR, "transfer_ppp failed: wanted unit %d, got %d",
		   ifunit, x);
	    die(1);
	}
	x = TTYDISC;
	ioctl(loop_slave, TIOCSETD, &x);
    }

    ppp_fd = fd;

    /*
     * Enable debug in the driver if requested.
     */
    if (kdebugflag) {
	if (ioctl(fd, PPPIOCGFLAGS, (caddr_t) &x) == -1) {
	    syslog(LOG_WARNING, "ioctl (PPPIOCGFLAGS): %m");
	} else {
	    x |= (kdebugflag & 0xFF) * SC_DEBUG;
	    if (ioctl(fd, PPPIOCSFLAGS, (caddr_t) &x) == -1)
		syslog(LOG_WARNING, "ioctl(PPPIOCSFLAGS): %m");
	}
    }

    /*
     * Set device for non-blocking reads.
     */
    if ((initfdflags = fcntl(fd, F_GETFL)) == -1
	|| fcntl(fd, F_SETFL, initfdflags | O_NONBLOCK) == -1) {
	syslog(LOG_WARNING, "Couldn't set device to non-blocking mode: %m");
    }
}

/*
 * restore_loop - reattach the ppp unit to the loopback.
 */
void
restore_loop(void)
{
    int x;

    /*
     * Transfer the ppp interface back to the loopback.
     */
    if (ioctl(ppp_fd, PPPIOCXFERUNIT, 0) == -1) {
	syslog(LOG_ERR, "ioctl(transfer ppp unit): %m");
	die(1);
    }
    x = PPPDISC;
    if (ioctl(loop_slave, TIOCSETD, &x) == -1) {
	syslog(LOG_ERR, "ioctl(TIOCSETD): %m");
	die(1);
    }

    /*
     * Check that we got the same unit again.
     */
    if (ioctl(loop_slave, PPPIOCGUNIT, &x) == -1) {	
	syslog(LOG_ERR, "ioctl(PPPIOCGUNIT): %m");
	die(1);
    }
    if (x != ifunit) {
	syslog(LOG_ERR, "transfer_ppp failed: wanted unit %d, got %d",
	       ifunit, x);
	die(1);
    }
    ppp_fd = loop_slave;
}

/*
 * disestablish_ppp - Restore the serial port to normal operation.
 * This shouldn't call die() because it's called from die().
 */
void
disestablish_ppp(int fd)
{
    /* Reset non-blocking mode on fd. */
    if (initfdflags != -1 && fcntl(fd, F_SETFL, initfdflags) == -1)
	syslog(LOG_WARNING, "Couldn't restore device fd flags: %m");
    initfdflags = -1;

    /* Restore old line discipline. */
    if (initdisc >= 0 && ioctl(fd, TIOCSETD, &initdisc) == -1)
	syslog(LOG_ERR, "ioctl(TIOCSETD): %m");
    initdisc = -1;

    if (fd == ppp_fd)
	ppp_fd = -1;
}

/*
 * Check whether the link seems not to be 8-bit clean.
 */
void
clean_check(void)
{
    int x;
    char *s;

    if (ioctl(ppp_fd, PPPIOCGFLAGS, (caddr_t) &x) == 0) {
	s = NULL;
	switch (~x & (SC_RCV_B7_0|SC_RCV_B7_1|SC_RCV_EVNP|SC_RCV_ODDP)) {
	case SC_RCV_B7_0:
	    s = "bit 7 set to 1";
	    break;
	case SC_RCV_B7_1:
	    s = "bit 7 set to 0";
	    break;
	case SC_RCV_EVNP:
	    s = "odd parity";
	    break;
	case SC_RCV_ODDP:
	    s = "even parity";
	    break;
	}
	if (s != NULL) {
	    syslog(LOG_WARNING, "Serial link is not 8-bit clean:");
	    syslog(LOG_WARNING, "All received characters had %s", s);
	}
    }
}

/*
 * set_up_tty: Set up the serial port on `fd' for 8 bits, no parity,
 * at the requested speed, etc.  If `local' is true, set CLOCAL
 * regardless of whether the modem option was specified.
 *
 * For *BSD, we assume that speed_t values numerically equal bits/second.
 */
void
set_up_tty(int fd, int local)
{
    struct termios tios;

    if (tcgetattr(fd, &tios) == -1) {
	syslog(LOG_ERR, "tcgetattr: %m");
	die(1);
    }

    if (!restore_term) {
	inittermios = tios;
	ioctl(fd, TIOCGWINSZ, &wsinfo);
    }

    tios.c_cflag &= ~(CSIZE | CSTOPB | PARENB | CLOCAL);
    if (crtscts > 0 && modem)
	tios.c_cflag |= CRTSCTS;
    else if (crtscts < 0)
	tios.c_cflag &= ~CRTSCTS;

    tios.c_cflag |= CS8 | CREAD | HUPCL;
    if (local || !modem)
	tios.c_cflag |= CLOCAL;
    tios.c_iflag = IGNBRK | IGNPAR;
    tios.c_oflag = 0;
    tios.c_lflag = 0;
    tios.c_cc[VMIN] = 1;
    tios.c_cc[VTIME] = 0;

    if (crtscts == -2) {
	tios.c_iflag |= IXON | IXOFF;
	tios.c_cc[VSTOP] = 0x13;	/* DC3 = XOFF = ^S */
	tios.c_cc[VSTART] = 0x11;	/* DC1 = XON  = ^Q */
    }

    if (inspeed) {
	cfsetospeed(&tios, inspeed);
	cfsetispeed(&tios, inspeed);
    } else {
	inspeed = cfgetospeed(&tios);
	/*
	 * We can't proceed if the serial port speed is 0,
	 * since that implies that the serial port is disabled.
	 */
	if (inspeed == 0) {
	    syslog(LOG_ERR, "Baud rate for %s is 0; need explicit baud rate",
		   devnam);
	    die(1);
	}
    }
    baud_rate = inspeed;

    if (tcsetattr(fd, TCSAFLUSH, &tios) == -1) {
	syslog(LOG_ERR, "tcsetattr: %m");
	die(1);
    }

    restore_term = 1;
}

/*
 * restore_tty - restore the terminal to the saved settings.
 */
void
restore_tty(int fd)
{
    if (restore_term) {
	if (!default_device) {
	    /*
	     * Turn off echoing, because otherwise we can get into
	     * a loop with the tty and the modem echoing to each other.
	     * We presume we are the sole user of this tty device, so
	     * when we close it, it will revert to its defaults anyway.
	     */
	    inittermios.c_lflag &= ~(ECHO | ECHONL);
	}
	if (tcsetattr(fd, TCSAFLUSH, &inittermios) == -1)
	    if (errno != ENXIO)
		syslog(LOG_WARNING, "tcsetattr: %m");
	ioctl(fd, TIOCSWINSZ, &wsinfo);
	restore_term = 0;
    }
}

/*
 * setdtr - control the DTR line on the serial port.
 * This is called from die(), so it shouldn't call die().
 */
void
setdtr(int fd, int on)
{
    int modembits = TIOCM_DTR;

    ioctl(fd, (on? TIOCMBIS: TIOCMBIC), &modembits);
}


/*
 * open_ppp_loopback - open the device we use for getting
 * packets in demand mode, and connect it to a ppp interface.
 * Here we use a pty.
 */
void
open_ppp_loopback(void)
{
    int flags;
    struct termios tios;
    int pppdisc = PPPDISC;

    if (openpty(&loop_master, &loop_slave, loop_name, NULL, NULL) == -1) {
	syslog(LOG_ERR, "No free pty for loopback");
	die(1);
    }
    SYSDEBUG((LOG_DEBUG, "using %s for loopback", loop_name));

    if (tcgetattr(loop_slave, &tios) == 0) {
	tios.c_cflag &= ~(CSIZE | CSTOPB | PARENB);
	tios.c_cflag |= CS8 | CREAD;
	tios.c_iflag = IGNPAR;
	tios.c_oflag = 0;
	tios.c_lflag = 0;
	if (tcsetattr(loop_slave, TCSAFLUSH, &tios) == -1)
	    syslog(LOG_WARNING, "couldn't set attributes on loopback: %m");
    }

    if ((flags = fcntl(loop_master, F_GETFL)) != -1) 
	if (fcntl(loop_master, F_SETFL, flags | O_NONBLOCK) == -1)
	    syslog(LOG_WARNING, "couldn't set loopback to nonblock: %m");

    ppp_fd = loop_slave;
    if (ioctl(ppp_fd, TIOCSETD, &pppdisc) == -1) {
	syslog(LOG_ERR, "ioctl(TIOCSETD): %m");
	die(1);
    }

    /*
     * Find out which interface we were given.
     */
    if (ioctl(ppp_fd, PPPIOCGUNIT, &ifunit) == -1) {	
	syslog(LOG_ERR, "ioctl(PPPIOCGUNIT): %m");
	die(1);
    }

    /*
     * Enable debug in the driver if requested.
     */
    if (kdebugflag) {
	if (ioctl(ppp_fd, PPPIOCGFLAGS, (caddr_t) &flags) == -1) {
	    syslog(LOG_WARNING, "ioctl (PPPIOCGFLAGS): %m");
	} else {
	    flags |= (kdebugflag & 0xFF) * SC_DEBUG;
	    if (ioctl(ppp_fd, PPPIOCSFLAGS, (caddr_t) &flags) == -1)
		syslog(LOG_WARNING, "ioctl(PPPIOCSFLAGS): %m");
	}
    }

}


/*
 * output - Output PPP packet.
 */
void
output(int unit, u_char *p, int len)
{
    if (debug)
	log_packet(p, len, "sent ", LOG_DEBUG);

    if (write(ttyfd, p, len) == -1) {
	if (errno != EIO)
	    syslog(LOG_ERR, "write: %m");
    }
}


/*
 * wait_input - wait until there is data available on ttyfd,
 * for the length of time specified by *timo (indefinite
 * if timo is NULL).
 */
void
wait_input(struct timeval *timo)
{
    fd_set *fdsp = NULL;
    int fdsn;
    int n;

    fdsn = howmany(ttyfd+1, NFDBITS) * sizeof(fd_mask);
    if ((fdsp = (fd_set *)malloc(fdsn)) == NULL)
	err(1, "malloc");
    memset(fdsp, 0, fdsn);
    FD_SET(ttyfd, fdsp);

    n = select(ttyfd+1, fdsp, NULL, fdsp, timo);
    if (n == -1 && errno != EINTR) {
	syslog(LOG_ERR, "select: %m");
	free(fdsp);
	die(1);
    }
    free(fdsp);
}


/*
 * wait_loop_output - wait until there is data available on the
 * loopback, for the length of time specified by *timo (indefinite
 * if timo is NULL).
 */
void
wait_loop_output(struct timeval *timo)
{
    fd_set *fdsp = NULL;
    int fdsn;
    int n;

    fdsn = howmany(loop_master+1, NFDBITS) * sizeof(fd_mask);
    if ((fdsp = (fd_set *)malloc(fdsn)) == NULL)
	err(1, "malloc");
    memset(fdsp, 0, fdsn);
    FD_SET(loop_master, fdsp);

    n = select(loop_master + 1, fdsp, NULL, fdsp, timo);
    if (n == -1 && errno != EINTR) {
	syslog(LOG_ERR, "select: %m");
	free(fdsp);
	die(1);
    }
    free(fdsp);
}


/*
 * wait_time - wait for a given length of time or until a
 * signal is received.
 */
void
wait_time(struct timeval *timo)
{
    int n;

    n = select(0, NULL, NULL, NULL, timo);
    if (n == -1 && errno != EINTR) {
	syslog(LOG_ERR, "select: %m");
	die(1);
    }
}


/*
 * read_packet - get a PPP packet from the serial device.
 */
int
read_packet(u_char *buf)
{
    int len;

    if ((len = read(ttyfd, buf, PPP_MTU + PPP_HDRLEN)) == -1) {
	if (errno == EWOULDBLOCK || errno == EINTR)
	    return -1;
	syslog(LOG_ERR, "read: %m");
	die(1);
    }
    return len;
}


/*
 * get_loop_output - read characters from the loopback, form them
 * into frames, and detect when we want to bring the real link up.
 * Return value is 1 if we need to bring up the link, 0 otherwise.
 */
int
get_loop_output(void)
{
    int rv = 0;
    int n;

    while ((n = read(loop_master, inbuf, sizeof(inbuf))) >= 0) {
	if (loop_chars(inbuf, n))
	    rv = 1;
    }

    if (n == 0) {
	syslog(LOG_ERR, "eof on loopback");
	die(1);
    } else if (errno != EWOULDBLOCK){
	syslog(LOG_ERR, "read from loopback: %m");
	die(1);
    }

    return rv;
}


/*
 * ppp_send_config - configure the transmit characteristics of
 * the ppp interface.
 */
void
ppp_send_config(int unit, int mtu, u_int32_t asyncmap, int pcomp, int accomp)
{
    u_int x;
    struct ifreq ifr;

    strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
    ifr.ifr_mtu = mtu;
    if (ioctl(sockfd, SIOCSIFMTU, (caddr_t) &ifr) == -1) {
	syslog(LOG_ERR, "ioctl(SIOCSIFMTU): %m");
	quit();
    }

    if (ioctl(ppp_fd, PPPIOCSASYNCMAP, (caddr_t) &asyncmap) == -1) {
	syslog(LOG_ERR, "ioctl(PPPIOCSASYNCMAP): %m");
	quit();
    }

    if (ioctl(ppp_fd, PPPIOCGFLAGS, (caddr_t) &x) == -1) {
	syslog(LOG_ERR, "ioctl (PPPIOCGFLAGS): %m");
	quit();
    }
    x = pcomp? x | SC_COMP_PROT: x &~ SC_COMP_PROT;
    x = accomp? x | SC_COMP_AC: x &~ SC_COMP_AC;
    if (ioctl(ppp_fd, PPPIOCSFLAGS, (caddr_t) &x) == -1) {
	syslog(LOG_ERR, "ioctl(PPPIOCSFLAGS): %m");
	quit();
    }
}


/*
 * ppp_set_xaccm - set the extended transmit ACCM for the interface.
 */
void
ppp_set_xaccm(int unit, ext_accm accm)
{
    if (ioctl(ppp_fd, PPPIOCSXASYNCMAP, accm) == -1 && errno != ENOTTY)
	syslog(LOG_WARNING, "ioctl(set extended ACCM): %m");
}


/*
 * ppp_recv_config - configure the receive-side characteristics of
 * the ppp interface.
 */
void
ppp_recv_config(int unit, int mru, u_int32_t asyncmap, int pcomp, int accomp)
{
    int x;

    if (ioctl(ppp_fd, PPPIOCSMRU, (caddr_t) &mru) == -1) {
	syslog(LOG_ERR, "ioctl(PPPIOCSMRU): %m");
	quit();
    }
    if (ioctl(ppp_fd, PPPIOCSRASYNCMAP, (caddr_t) &asyncmap) == -1) {
	syslog(LOG_ERR, "ioctl(PPPIOCSRASYNCMAP): %m");
	quit();
    }
    if (ioctl(ppp_fd, PPPIOCGFLAGS, (caddr_t) &x) == -1) {
	syslog(LOG_ERR, "ioctl (PPPIOCGFLAGS): %m");
	quit();
    }
    x = !accomp? x | SC_REJ_COMP_AC: x &~ SC_REJ_COMP_AC;
    if (ioctl(ppp_fd, PPPIOCSFLAGS, (caddr_t) &x) == -1) {
	syslog(LOG_ERR, "ioctl(PPPIOCSFLAGS): %m");
	quit();
    }
}

/*
 * ccp_test - ask kernel whether a given compression method
 * is acceptable for use.  Returns 1 if the method and parameters
 * are OK, 0 if the method is known but the parameters are not OK
 * (e.g. code size should be reduced), or -1 if the method is unknown.
 */
int
ccp_test(int unit, u_char *opt_ptr, int opt_len, int for_transmit)
{
    struct ppp_option_data data;

    data.ptr = opt_ptr;
    data.length = opt_len;
    data.transmit = for_transmit;
    if (ioctl(ttyfd, PPPIOCSCOMPRESS, (caddr_t) &data) >= 0)
	return 1;
    return (errno == ENOBUFS)? 0: -1;
}

/*
 * ccp_flags_set - inform kernel about the current state of CCP.
 */
void
ccp_flags_set(int unit, int isopen, int isup)
{
    int x;

    if (ioctl(ppp_fd, PPPIOCGFLAGS, (caddr_t) &x) == -1) {
	syslog(LOG_ERR, "ioctl (PPPIOCGFLAGS): %m");
	return;
    }
    x = isopen? x | SC_CCP_OPEN: x &~ SC_CCP_OPEN;
    x = isup? x | SC_CCP_UP: x &~ SC_CCP_UP;
    if (ioctl(ppp_fd, PPPIOCSFLAGS, (caddr_t) &x) == -1)
	syslog(LOG_ERR, "ioctl(PPPIOCSFLAGS): %m");
}

/*
 * ccp_fatal_error - returns 1 if decompression was disabled as a
 * result of an error detected after decompression of a packet,
 * 0 otherwise.  This is necessary because of patent nonsense.
 */
int
ccp_fatal_error(int unit)
{
    int x;

    if (ioctl(ppp_fd, PPPIOCGFLAGS, (caddr_t) &x) == -1) {
	syslog(LOG_ERR, "ioctl(PPPIOCGFLAGS): %m");
	return 0;
    }
    return x & SC_DC_FERROR;
}

/*
 * get_idle_time - return how long the link has been idle.
 */
int
get_idle_time(int u, struct ppp_idle *ip)
{
    return ioctl(ppp_fd, PPPIOCGIDLE, ip) >= 0;
}


#ifdef PPP_FILTER
/*
 * set_filters - transfer the pass and active filters to the kernel.
 */
int
set_filters(struct bpf_program *pass, struct bpf_program *active)
{
    int ret = 1;

    if (pass->bf_len > 0) {
	if (ioctl(ppp_fd, PPPIOCSPASS, pass) == -1) {
	    syslog(LOG_ERR, "Couldn't set pass-filter in kernel: %m");
	    ret = 0;
	}
    }
    if (active->bf_len > 0) {
	if (ioctl(ppp_fd, PPPIOCSACTIVE, active) == -1) {
	    syslog(LOG_ERR, "Couldn't set active-filter in kernel: %m");
	    ret = 0;
	}
    }
    return ret;
}
#endif

/*
 * sifvjcomp - config tcp header compression
 */
int
sifvjcomp(int u, int vjcomp, int cidcomp, int maxcid)
{
    u_int x;

    if (ioctl(ppp_fd, PPPIOCGFLAGS, (caddr_t) &x) == -1) {
	syslog(LOG_ERR, "ioctl (PPPIOCGFLAGS): %m");
	return 0;
    }
    x = vjcomp ? x | SC_COMP_TCP: x &~ SC_COMP_TCP;
    x = cidcomp? x & ~SC_NO_TCP_CCID: x | SC_NO_TCP_CCID;
    if (ioctl(ppp_fd, PPPIOCSFLAGS, (caddr_t) &x) == -1) {
	syslog(LOG_ERR, "ioctl(PPPIOCSFLAGS): %m");
	return 0;
    }
    if (vjcomp && ioctl(ppp_fd, PPPIOCSMAXCID, (caddr_t) &maxcid) == -1) {
	syslog(LOG_ERR, "ioctl(PPPIOCSFLAGS): %m");
	return 0;
    }
    return 1;
}

/*
 * sifup - Config the interface up and enable IP packets to pass.
 */
int
sifup(int u)
{
    struct ifreq ifr;

    strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
    if (ioctl(sockfd, SIOCGIFFLAGS, (caddr_t) &ifr) == -1) {
	syslog(LOG_ERR, "ioctl (SIOCGIFFLAGS): %m");
	return 0;
    }
    ifr.ifr_flags |= IFF_UP;
    if (ioctl(sockfd, SIOCSIFFLAGS, (caddr_t) &ifr) == -1) {
	syslog(LOG_ERR, "ioctl(SIOCSIFFLAGS): %m");
	return 0;
    }
    if_is_up = 1;
    return 1;
}

/*
 * sifnpmode - Set the mode for handling packets for a given NP.
 */
int
sifnpmode(int u, int proto, enum NPmode mode)
{
    struct npioctl npi;

    npi.protocol = proto;
    npi.mode = mode;
    if (ioctl(ppp_fd, PPPIOCSNPMODE, &npi) == -1) {
	syslog(LOG_ERR, "ioctl(set NP %d mode to %d): %m", proto, mode);
	return 0;
    }
    return 1;
}

/*
 * sifdown - Config the interface down and disable IP.
 */
int
sifdown(int u)
{
    struct ifreq ifr;
    int rv;
    struct npioctl npi;

    rv = 1;
    npi.protocol = PPP_IP;
    npi.mode = NPMODE_ERROR;
    ioctl(ppp_fd, PPPIOCSNPMODE, (caddr_t) &npi);
    /* ignore errors, because ppp_fd might have been closed by now. */

    strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
    if (ioctl(sockfd, SIOCGIFFLAGS, (caddr_t) &ifr) == -1) {
	syslog(LOG_ERR, "ioctl (SIOCGIFFLAGS): %m");
	rv = 0;
    } else {
	ifr.ifr_flags &= ~IFF_UP;
	if (ioctl(sockfd, SIOCSIFFLAGS, (caddr_t) &ifr) == -1) {
	    syslog(LOG_ERR, "ioctl(SIOCSIFFLAGS): %m");
	    rv = 0;
	} else
	    if_is_up = 0;
    }
    return rv;
}

/*
 * SET_SA_FAMILY - set the sa_family field of a struct sockaddr,
 * if it exists.
 */
#define SET_SA_FAMILY(addr, family)		\
    BZERO((char *) &(addr), sizeof(addr));	\
    addr.sa_family = (family); 			\
    addr.sa_len = sizeof(addr);

/*
 * sifaddr - Config the interface IP addresses and netmask.
 */
int
sifaddr(int u, u_int32_t o, u_int32_t h, u_int32_t m)
{
    struct ifaliasreq ifra;
    struct ifreq ifr;
    char s1[64], s2[64];

    strlcpy(ifra.ifra_name, ifname, sizeof(ifra.ifra_name));
    SET_SA_FAMILY(ifra.ifra_addr, AF_INET);
    ((struct sockaddr_in *) &ifra.ifra_addr)->sin_addr.s_addr = o;
    SET_SA_FAMILY(ifra.ifra_broadaddr, AF_INET);
    ((struct sockaddr_in *) &ifra.ifra_broadaddr)->sin_addr.s_addr = h;
    if (m != 0) {
	SET_SA_FAMILY(ifra.ifra_mask, AF_INET);
	((struct sockaddr_in *) &ifra.ifra_mask)->sin_addr.s_addr = m;
    } else
	BZERO(&ifra.ifra_mask, sizeof(ifra.ifra_mask));
    BZERO(&ifr, sizeof(ifr));
    strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
    if (ioctl(sockfd, SIOCDIFADDR, (caddr_t) &ifr) == -1) {
	if (errno != EADDRNOTAVAIL)
	    syslog(LOG_WARNING, "Couldn't remove interface address: %m");
    }
    if (ioctl(sockfd, SIOCAIFADDR, (caddr_t) &ifra) == -1) {
	if (errno != EEXIST) {
	    syslog(LOG_ERR, "Couldn't set interface address: %m");
	    return 0;
	}
	strlcpy(s1, ip_ntoa(o), sizeof(s1));
	strlcpy(s2, ip_ntoa(h), sizeof(s2));
	syslog(LOG_WARNING,
	       "Couldn't set interface address: "
	       "Address %s or destination %s already exists", s1, s2);
    }
    ifaddrs[0] = o;
    ifaddrs[1] = h;
    return 1;
}

/*
 * cifaddr - Clear the interface IP addresses, and delete routes
 * through the interface if possible.
 */
int
cifaddr(int u, u_int32_t o, u_int32_t h)
{
    struct ifaliasreq ifra;

    ifaddrs[0] = 0;
    strlcpy(ifra.ifra_name, ifname, sizeof(ifra.ifra_name));
    SET_SA_FAMILY(ifra.ifra_addr, AF_INET);
    ((struct sockaddr_in *) &ifra.ifra_addr)->sin_addr.s_addr = o;
    SET_SA_FAMILY(ifra.ifra_broadaddr, AF_INET);
    ((struct sockaddr_in *) &ifra.ifra_broadaddr)->sin_addr.s_addr = h;
    BZERO(&ifra.ifra_mask, sizeof(ifra.ifra_mask));
    if (ioctl(sockfd, SIOCDIFADDR, (caddr_t) &ifra) == -1) {
	if (errno != EADDRNOTAVAIL)
	    syslog(LOG_WARNING, "Couldn't delete interface address: %m");
	return 0;
    }
    return 1;
}

/*
 * getrtableid - return routing table id of ppp interface
 */
int
getrtableid(void)
{
    struct ifreq ifr;
    int tableid = 0;

    memset(&ifr, 0, sizeof(ifr));
    strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
    if (ioctl(sockfd, SIOCGIFRDOMAIN, (caddr_t) &ifr) == 0)
	tableid = ifr.ifr_rdomainid;

    return tableid;
}

/*
 * sifdefaultroute - assign a default route through the address given.
 */
int
sifdefaultroute(int u, u_int32_t l, u_int32_t g)
{
    return dodefaultroute(g, 's');
}

/*
 * cifdefaultroute - delete a default route through the address given.
 */
int
cifdefaultroute(int u, u_int32_t l, u_int32_t g)
{
    return dodefaultroute(g, 'c');
}

/*
 * dodefaultroute - talk to a routing socket to add/delete a default route.
 */
static int
dodefaultroute(u_int32_t g, int cmd)
{
    int routes;
    struct {
	struct rt_msghdr	hdr;
	struct sockaddr_in	dst;
	struct sockaddr_in	gway;
	struct sockaddr_in	mask;
    } rtmsg;

    if ((routes = socket(AF_ROUTE, SOCK_RAW, AF_INET)) == -1) {
	syslog(LOG_ERR, "Couldn't %s default route: socket: %m",
	       cmd=='s'? "add": "delete");
	return 0;
    }

    memset(&rtmsg, 0, sizeof(rtmsg));
    rtmsg.hdr.rtm_type = cmd == 's'? RTM_ADD: RTM_DELETE;
    rtmsg.hdr.rtm_flags = RTF_UP | RTF_GATEWAY;
    rtmsg.hdr.rtm_version = RTM_VERSION;
    rtmsg.hdr.rtm_tableid = getrtableid();
    rtmsg.hdr.rtm_seq = ++rtm_seq;
    rtmsg.hdr.rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK;
    rtmsg.dst.sin_len = sizeof(rtmsg.dst);
    rtmsg.dst.sin_family = AF_INET;
    rtmsg.gway.sin_len = sizeof(rtmsg.gway);
    rtmsg.gway.sin_family = AF_INET;
    rtmsg.gway.sin_addr.s_addr = g;
    rtmsg.mask.sin_len = sizeof(rtmsg.mask);
    rtmsg.mask.sin_family = AF_INET;

    rtmsg.hdr.rtm_msglen = sizeof(rtmsg);
    if (write(routes, &rtmsg, sizeof(rtmsg)) == -1) {
	syslog(LOG_ERR, "Couldn't %s default route: %m",
	       cmd=='s'? "add": "delete");
	close(routes);
	return 0;
    }

    close(routes);
    default_route_gateway = (cmd == 's')? g: 0;
    return 1;
}

#if RTM_VERSION >= 3

/*
 * sifproxyarp - Make a proxy ARP entry for the peer.
 */
static struct {
    struct rt_msghdr		hdr;
    struct sockaddr_inarp	dst;
    struct sockaddr_dl		hwa;
    char			extra[128];
} arpmsg;

static int arpmsg_valid;

int
sifproxyarp(int unit, u_int32_t hisaddr)
{
    int routes;

    /*
     * Get the hardware address of an interface on the same subnet
     * as our local address.
     */
    memset(&arpmsg, 0, sizeof(arpmsg));
    if (!get_ether_addr(hisaddr, &arpmsg.hwa)) {
	syslog(LOG_ERR, "Cannot determine ethernet address for proxy ARP");
	return 0;
    }

    if ((routes = socket(AF_ROUTE, SOCK_RAW, AF_INET)) == -1) {
	syslog(LOG_ERR, "Couldn't add proxy arp entry: socket: %m");
	return 0;
    }

    arpmsg.hdr.rtm_type = RTM_ADD;
    arpmsg.hdr.rtm_flags = RTF_ANNOUNCE | RTF_HOST | RTF_STATIC;
    arpmsg.hdr.rtm_version = RTM_VERSION;
    arpmsg.hdr.rtm_seq = ++rtm_seq;
    arpmsg.hdr.rtm_addrs = RTA_DST | RTA_GATEWAY;
    arpmsg.hdr.rtm_inits = RTV_EXPIRE;
    arpmsg.dst.sin_len = sizeof(struct sockaddr_inarp);
    arpmsg.dst.sin_family = AF_INET;
    arpmsg.dst.sin_addr.s_addr = hisaddr;
    arpmsg.dst.sin_other = SIN_PROXY;

    arpmsg.hdr.rtm_msglen = (char *) &arpmsg.hwa - (char *) &arpmsg
	+ arpmsg.hwa.sdl_len;
    if (write(routes, &arpmsg, arpmsg.hdr.rtm_msglen) == -1) {
	syslog(LOG_ERR, "Couldn't add proxy arp entry: %m");
	close(routes);
	return 0;
    }

    close(routes);
    arpmsg_valid = 1;
    proxy_arp_addr = hisaddr;
    return 1;
}

/*
 * cifproxyarp - Delete the proxy ARP entry for the peer.
 */
int
cifproxyarp(int unit, u_int32_t hisaddr)
{
    int routes;

    if (!arpmsg_valid)
	return 0;
    arpmsg_valid = 0;

    arpmsg.hdr.rtm_type = RTM_DELETE;
    arpmsg.hdr.rtm_seq = ++rtm_seq;

    if ((routes = socket(AF_ROUTE, SOCK_RAW, AF_INET)) == -1) {
	syslog(LOG_ERR, "Couldn't delete proxy arp entry: socket: %m");
	return 0;
    }

    if (write(routes, &arpmsg, arpmsg.hdr.rtm_msglen) == -1) {
	syslog(LOG_ERR, "Couldn't delete proxy arp entry: %m");
	close(routes);
	return 0;
    }

    close(routes);
    proxy_arp_addr = 0;
    return 1;
}

#else	/* RTM_VERSION */

/*
 * sifproxyarp - Make a proxy ARP entry for the peer.
 */
int
sifproxyarp(unit, hisaddr)
    int unit;
    u_int32_t hisaddr;
{
    struct arpreq arpreq;
    struct {
	struct sockaddr_dl	sdl;
	char			space[128];
    } dls;

    BZERO(&arpreq, sizeof(arpreq));

    /*
     * Get the hardware address of an interface on the same subnet
     * as our local address.
     */
    if (!get_ether_addr(hisaddr, &dls.sdl)) {
	syslog(LOG_ERR, "Cannot determine ethernet address for proxy ARP");
	return 0;
    }

    arpreq.arp_ha.sa_len = sizeof(struct sockaddr);
    arpreq.arp_ha.sa_family = AF_UNSPEC;
    BCOPY(LLADDR(&dls.sdl), arpreq.arp_ha.sa_data, dls.sdl.sdl_alen);
    SET_SA_FAMILY(arpreq.arp_pa, AF_INET);
    ((struct sockaddr_in *) &arpreq.arp_pa)->sin_addr.s_addr = hisaddr;
    arpreq.arp_flags = ATF_PERM | ATF_PUBL;
    if (ioctl(sockfd, SIOCSARP, (caddr_t)&arpreq) == -1) {
	syslog(LOG_ERR, "Couldn't add proxy arp entry: %m");
	return 0;
    }

    proxy_arp_addr = hisaddr;
    return 1;
}

/*
 * cifproxyarp - Delete the proxy ARP entry for the peer.
 */
int
cifproxyarp(unit, hisaddr)
    int unit;
    u_int32_t hisaddr;
{
    struct arpreq arpreq;

    BZERO(&arpreq, sizeof(arpreq));
    SET_SA_FAMILY(arpreq.arp_pa, AF_INET);
    ((struct sockaddr_in *) &arpreq.arp_pa)->sin_addr.s_addr = hisaddr;
    if (ioctl(sockfd, SIOCDARP, (caddr_t)&arpreq) == -1) {
	syslog(LOG_WARNING, "Couldn't delete proxy arp entry: %m");
	return 0;
    }
    proxy_arp_addr = 0;
    return 1;
}
#endif	/* RTM_VERSION */


/*
 * get_ether_addr - get the hardware address of an interface on the
 * the same subnet as ipaddr.
 */
#define MAX_IFS		32

static int
get_ether_addr(u_int32_t ipaddr, struct sockaddr_dl *hwaddr)
{
    u_int32_t ina, mask;
    struct sockaddr_dl *dla;
    struct ifaddrs *ifap, *ifa, *ifp;

    if (getifaddrs(&ifap) != 0) {
	syslog(LOG_ERR, "getifaddrs: %m");
	return 0;
    }

    /*
     * Scan through looking for an interface with an Internet
     * address on the same subnet as `ipaddr'.
     */
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
	if (ifa->ifa_addr == NULL)
		continue;
	if (ifa->ifa_addr->sa_family == AF_INET) {
	    ina = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr;
	    /*
	     * Check that the interface is up, and not point-to-point
	     * or loopback.
	     */
	    if ((ifa->ifa_flags &
		 (IFF_UP|IFF_BROADCAST|IFF_POINTOPOINT|IFF_LOOPBACK|IFF_NOARP))
		 != (IFF_UP|IFF_BROADCAST))
		continue;
	    /*
	     * Get its netmask and check that it's on the right subnet.
	     */
	    mask = ((struct sockaddr_in *)ifa->ifa_netmask)->sin_addr.s_addr;
	    if ((ipaddr & mask) != (ina & mask))
		continue;

	    break;
	}
    }

    if (ifa == NULL) {
	freeifaddrs(ifap);
	return 0;
    }
    syslog(LOG_INFO, "found interface %s for proxy arp", ifa->ifa_name);

    /*
     * Now scan through again looking for a link-level address
     * for this interface.
     */
    ifp = ifa;
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
	if (ifa->ifa_addr == NULL)
		continue;
	if (strcmp(ifp->ifa_name, ifa->ifa_name) == 0
	    && ifa->ifa_addr->sa_family == AF_LINK) {
	    /*
	     * Found the link-level address - copy it out
	     */
	    dla = (struct sockaddr_dl *)ifa->ifa_addr;
	    BCOPY(dla, hwaddr, dla->sdl_len);
	    return 1;
	}
    }

    freeifaddrs(ifap);
    return 0;
}

/*
 * Return user specified netmask, modified by any mask we might determine
 * for address `addr' (in network byte order).
 * Here we scan through the system's list of interfaces, looking for
 * any non-point-to-point interfaces which might appear to be on the same
 * network as `addr'.  If we find any, we OR in their netmask to the
 * user-specified netmask.
 */
u_int32_t
GetMask(u_int32_t addr)
{
    u_int32_t mask, nmask, ina;
    struct ifaddrs *ifap, *ifa;

    addr = ntohl(addr);
    if (IN_CLASSA(addr))	/* determine network mask for address class */
	nmask = IN_CLASSA_NET;
    else if (IN_CLASSB(addr))
	nmask = IN_CLASSB_NET;
    else
	nmask = IN_CLASSC_NET;
    /* class D nets are disallowed by bad_ip_adrs */
    mask = netmask | htonl(nmask);

    /*
     * Scan through the system's network interfaces.
     */
    if (getifaddrs(&ifap) != 0) {
	syslog(LOG_WARNING, "getifaddrs: %m");
	return mask;
    }
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
	/*
	 * Check the interface's internet address.
	 */
	if (ifa->ifa_addr == NULL ||
	    ifa->ifa_addr->sa_family != AF_INET)
		continue;
	ina = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr;
	if ((ntohl(ina) & nmask) != (addr & nmask))
	    continue;
	/*
	 * Check that the interface is up, and not point-to-point or loopback.
	 */
	if ((ifa->ifa_flags & (IFF_UP|IFF_POINTOPOINT|IFF_LOOPBACK))
	    != IFF_UP)
	    continue;
	/*
	 * Get its netmask and OR it into our mask.
	 */
	mask |= ((struct sockaddr_in *)ifa->ifa_netmask)->sin_addr.s_addr;
    }

    freeifaddrs(ifap);
    return mask;
}

/*
 * lock - create a lock file for the named lock device
 */
#define	LOCK_PREFIX	"/var/spool/lock/LCK.."

int
lock(char *dev)
{
    const char *errstr;
    char hdb_lock_buffer[12];
    int fd, n;
    pid_t pid;
    char *p;

    if ((p = strrchr(dev, '/')) != NULL)
	dev = p + 1;
    if (asprintf(&lock_file, "%s%s", LOCK_PREFIX, dev) == -1)
	novm("lock file name");

    while ((fd = open(lock_file, O_EXCL | O_CREAT | O_RDWR, 0644)) == -1) {
	if (errno == EEXIST
	    && (fd = open(lock_file, O_RDONLY)) >= 0) {
	    /* Read the lock file to find out who has the device locked */
	    n = read(fd, hdb_lock_buffer, 11);
	    if (n <= 0) {
		syslog(LOG_ERR, "Can't read pid from lock file %s", lock_file);
		close(fd);
	    } else {
		hdb_lock_buffer[n] = 0;
		pid = strtonum(hdb_lock_buffer, 1, 65535, &errstr);
		if (errstr)
		    syslog(LOG_NOTICE, "lock file %s contains garbage: %s\n",
			   dev, errstr);
		else if (kill(pid, 0) == -1 && errno == ESRCH) {
		    /* pid no longer exists - remove the lock file */
		    if (unlink(lock_file) == 0) {
			close(fd);
			syslog(LOG_NOTICE, "Removed stale lock on %s (pid %ld)",
			       dev, (long)pid);
			continue;
		    } else
			syslog(LOG_WARNING, "Couldn't remove stale lock on %s",
			       dev);
		} else
		    syslog(LOG_NOTICE, "Device %s is locked by pid %ld",
			   dev, (long)pid);
	    }
	    close(fd);
	} else
	    syslog(LOG_ERR, "Can't create lock file %s: %m", lock_file);
	free(lock_file);
	lock_file = NULL;
	return -1;
    }

    snprintf(hdb_lock_buffer, sizeof hdb_lock_buffer, "%10ld\n", (long)getpid());
    write(fd, hdb_lock_buffer, 11);

    close(fd);
    return 0;
}

/*
 * unlock - remove our lockfile
 */
void
unlock(void)
{
    if (lock_file) {
	unlink(lock_file);
	free(lock_file);
	lock_file = NULL;
    }
}
