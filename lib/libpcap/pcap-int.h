/*	$OpenBSD: pcap-int.h,v 1.14 2018/04/05 03:47:27 lteo Exp $	*/

/*
 * Copyright (c) 1994, 1995, 1996
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the Computer Systems
 *	Engineering Group at Lawrence Berkeley Laboratory.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef pcap_int_h
#define pcap_int_h

#include <pcap.h>

/*
 * Stuff to do when we close.
 */
#define MUST_CLEAR_RFMON	0x00000002	/* clear rfmon (monitor) mode */

struct pcap_opt {
	int	buffer_size;
	char	*source;
	int	promisc;
	int	rfmon;
	int	immediate;	/* immediate mode - deliver packets as soon as they arrive */
};

/*
 * Savefile
 */
struct pcap_sf {
	FILE *rfile;
	int swapped;
	int version_major;
	int version_minor;
	u_char *base;
};

struct pcap_md {
	struct pcap_stat stat;
	/*XXX*/
	int use_bpf;
	u_long	TotPkts;	/* can't oflow for 79 hrs on ether */
	u_long	TotAccepted;	/* count accepted by filter */
	u_long	TotDrops;	/* count of dropped packets */
	long	TotMissed;	/* missed by i/f during this run */
	long	OrigMissed;	/* missed by i/f before this run */
	int	timeout;	/* timeout for buffering */
	int	must_do_on_close; /* stuff we must do when we close */
	struct pcap *next;	/* list of open pcaps that need stuff cleared on close */
};

struct pcap {
	int fd;
	int snapshot;
	int linktype;
	int tzoff;		/* timezone offset */
	int offset;		/* offset for proper alignment */
	int activated;		/* true if the capture is really started */
	int oldstyle;		/* if we're opening with pcap_open_live() */
	int break_loop;		/* force break from packet-reading loop */

	struct pcap_sf sf;
	struct pcap_md md;
	struct pcap_opt opt;

	/*
	 * Read buffer.
	 */
	int bufsize;
	u_char *buffer;
	u_char *bp;
	int cc;

	/*
	 * Place holder for pcap_next().
	 */
	u_char *pkt;

	
	/*
	 * Placeholder for filter code if bpf not in kernel.
	 */
	struct bpf_program fcode;

	/*
	 * Datalink types supported on underlying fd
	 */
	int dlt_count;
	u_int *dlt_list;

	char errbuf[PCAP_ERRBUF_SIZE];

	struct pcap_pkthdr pcap_header;	/* This is needed for the pcap_next_ex() to work */
};

/*
 * How a `pcap_pkthdr' is actually stored in the dumpfile.
 */

struct pcap_sf_pkthdr {
    struct bpf_timeval ts;	/* time stamp */
    bpf_u_int32 caplen;		/* length of portion present */
    bpf_u_int32 len;		/* length this packet (off wire) */
};

int	yylex(void);

#ifndef min
#define min(a, b) ((a) > (b) ? (b) : (a))
#endif

/* Not all systems have IFF_LOOPBACK */
#ifdef IFF_LOOPBACK
#define ISLOOPBACK(name, flags) ((flags) & IFF_LOOPBACK)
#else
#define ISLOOPBACK(name, flags) ((name)[0] == 'l' && (name)[1] == 'o' && \
    (isdigit((unsigned char)((name)[2])) || (name)[2] == '\0'))
#endif

/* XXX should these be in pcap.h? */
int	pcap_offline_read(pcap_t *, int, pcap_handler, u_char *);
int	pcap_read(pcap_t *, int cnt, pcap_handler, u_char *);

int	pcap_do_addexit(pcap_t *);
void	pcap_add_to_pcaps_to_close(pcap_t *);
void	pcap_remove_from_pcaps_to_close(pcap_t *);
int	pcap_check_activated(pcap_t *);

/* Ultrix pads to make everything line up on a nice boundary */
#if defined(ultrix) || defined(__alpha)
#define       PCAP_FDDIPAD 3
#endif

/* XXX */
extern int pcap_fddipad;
#endif
