/*	$OpenBSD: pcap.c,v 1.24 2018/06/03 10:29:28 sthen Exp $	*/

/*
 * Copyright (c) 1993, 1994, 1995, 1996, 1997, 1998
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

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

#include "pcap-int.h"

static const char pcap_version_string[] = "OpenBSD libpcap";

int
pcap_dispatch(pcap_t *p, int cnt, pcap_handler callback, u_char *user)
{

	if (p->sf.rfile != NULL)
		return (pcap_offline_read(p, cnt, callback, user));
	return (pcap_read(p, cnt, callback, user));
}

int
pcap_loop(pcap_t *p, int cnt, pcap_handler callback, u_char *user)
{
	int n;

	for (;;) {
		if (p->sf.rfile != NULL)
			n = pcap_offline_read(p, cnt, callback, user);
		else {
			/*
			 * XXX keep reading until we get something
			 * (or an error occurs)
			 */
			do {
				n = pcap_read(p, cnt, callback, user);
			} while (n == 0);
		}
		if (n <= 0)
			return (n);
		if (cnt > 0) {
			cnt -= n;
			if (cnt <= 0)
				return (0);
		}
	}
}

struct singleton {
	struct pcap_pkthdr *hdr;
	const u_char *pkt;
};


static void
pcap_oneshot(u_char *userData, const struct pcap_pkthdr *h, const u_char *pkt)
{
	struct singleton *sp = (struct singleton *)userData;
	*sp->hdr = *h;
	sp->pkt = pkt;
}

const u_char *
pcap_next(pcap_t *p, struct pcap_pkthdr *h)
{
	struct singleton s;

	s.hdr = h;
	if (pcap_dispatch(p, 1, pcap_oneshot, (u_char*)&s) <= 0)
		return (0);
	return (s.pkt);
}

struct pkt_for_fakecallback {
	struct pcap_pkthdr *hdr;
	const u_char **pkt;
};

static void
pcap_fakecallback(u_char *userData, const struct pcap_pkthdr *h,
    const u_char *pkt)
{
	struct pkt_for_fakecallback *sp = (struct pkt_for_fakecallback *)userData;

	*sp->hdr = *h;
	*sp->pkt = pkt;
}

int 
pcap_next_ex(pcap_t *p, struct pcap_pkthdr **pkt_header,
    const u_char **pkt_data)
{
	struct pkt_for_fakecallback s;

	s.hdr = &p->pcap_header;
	s.pkt = pkt_data;

	/* Saves a pointer to the packet headers */
	*pkt_header= &p->pcap_header;

	if (p->sf.rfile != NULL) {
		int status;

		/* We are on an offline capture */
		status = pcap_offline_read(p, 1, pcap_fakecallback,
		    (u_char *)&s);

		/*
		 * Return codes for pcap_offline_read() are:
		 *   -  0: EOF
		 *   - -1: error
		 *   - >1: OK
		 * The first one ('0') conflicts with the return code of
		 * 0 from pcap_read() meaning "no packets arrived before
		 * the timeout expired", so we map it to -2 so you can
		 * distinguish between an EOF from a savefile and a
		 * "no packets arrived before the timeout expired, try
		 * again" from a live capture.
		 */
		if (status == 0)
			return (-2);
		else
			return (status);
	}

	/*
	 * Return codes for pcap_read() are:
	 *   -  0: timeout
	 *   - -1: error
	 *   - -2: loop was broken out of with pcap_breakloop()
	 *   - >1: OK
	 * The first one ('0') conflicts with the return code of 0 from
	 * pcap_offline_read() meaning "end of file".
	*/
	return (pcap_read(p, 1, pcap_fakecallback, (u_char *)&s));
}

int
pcap_check_activated(pcap_t *p)
{
	if (p->activated) {
		snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "can't perform "
			" operation on activated capture");
		return -1;
	}
	return 0;
}

int
pcap_set_snaplen(pcap_t *p, int snaplen)
{
	if (pcap_check_activated(p))
		return PCAP_ERROR_ACTIVATED;
	p->snapshot = snaplen;
	return 0;
}

int
pcap_set_promisc(pcap_t *p, int promisc)
{
	if (pcap_check_activated(p))
		return PCAP_ERROR_ACTIVATED;
	p->opt.promisc = promisc;
	return 0;
}

int
pcap_set_rfmon(pcap_t *p, int rfmon)
{
	if (pcap_check_activated(p))
		return PCAP_ERROR_ACTIVATED;
	p->opt.rfmon = rfmon;
	return 0;
}

int
pcap_set_timeout(pcap_t *p, int timeout_ms)
{
	if (pcap_check_activated(p))
		return PCAP_ERROR_ACTIVATED;
	p->md.timeout = timeout_ms;
	return 0;
}

int
pcap_set_immediate_mode(pcap_t *p, int immediate)
{
	if (pcap_check_activated(p))
		return PCAP_ERROR_ACTIVATED;
	p->opt.immediate = immediate;
	return 0;
}

int
pcap_set_buffer_size(pcap_t *p, int buffer_size)
{
	if (pcap_check_activated(p))
		return PCAP_ERROR_ACTIVATED;
	p->opt.buffer_size = buffer_size;
	return 0;
}

/*
 * Force the loop in "pcap_read()" or "pcap_read_offline()" to terminate.
 */
void
pcap_breakloop(pcap_t *p)
{
	p->break_loop = 1;
}

int
pcap_datalink(pcap_t *p)
{
	return (p->linktype);
}

int
pcap_list_datalinks(pcap_t *p, int **dlt_buffer)
{
	if (p->dlt_count == 0) {
		/*
		 * We couldn't fetch the list of DLTs, which means
		 * this platform doesn't support changing the
		 * DLT for an interface.  Return a list of DLTs
		 * containing only the DLT this device supports.
		 */
		*dlt_buffer = malloc(sizeof(**dlt_buffer));
		if (*dlt_buffer == NULL) {
			(void)snprintf(p->errbuf, sizeof(p->errbuf),
			    "malloc: %s", pcap_strerror(errno));
			return (-1);
		}
		**dlt_buffer = p->linktype;
		return (1);
	} else {
		*dlt_buffer = reallocarray(NULL, sizeof(**dlt_buffer),
		    p->dlt_count);
		if (*dlt_buffer == NULL) {
			(void)snprintf(p->errbuf, sizeof(p->errbuf),
			    "malloc: %s", pcap_strerror(errno));
			return (-1);
		}
		(void)memcpy(*dlt_buffer, p->dlt_list,
		    sizeof(**dlt_buffer) * p->dlt_count);
		return (p->dlt_count);
	}
}

/*
 * In Windows, you might have a library built with one version of the
 * C runtime library and an application built with another version of
 * the C runtime library, which means that the library might use one
 * version of malloc() and free() and the application might use another
 * version of malloc() and free().  If so, that means something
 * allocated by the library cannot be freed by the application, so we   
 * need to have a pcap_free_datalinks() routine to free up the list
 * allocated by pcap_list_datalinks(), even though it's just a wrapper
 * around free().
 */
void
pcap_free_datalinks(int *dlt_list)
{
	free(dlt_list);
}

struct dlt_choice {
	const char *name;
	const char *description;
	int	dlt;
};

static struct dlt_choice dlts[] = {
#define DLT_CHOICE(code, description) { #code, description, code }
DLT_CHOICE(DLT_NULL, "no link-layer encapsulation"),
DLT_CHOICE(DLT_EN10MB, "Ethernet (10Mb)"),
DLT_CHOICE(DLT_EN3MB, "Experimental Ethernet (3Mb)"),
DLT_CHOICE(DLT_AX25, "Amateur Radio AX.25"),
DLT_CHOICE(DLT_PRONET, "Proteon ProNET Token Ring"),
DLT_CHOICE(DLT_CHAOS, "Chaos"),
DLT_CHOICE(DLT_IEEE802, "IEEE 802 Networks"),
DLT_CHOICE(DLT_ARCNET, "ARCNET"),
DLT_CHOICE(DLT_SLIP, "Serial Line IP"),
DLT_CHOICE(DLT_PPP, "Point-to-point Protocol"),
DLT_CHOICE(DLT_PPP_SERIAL, "PPP over serial"),
DLT_CHOICE(DLT_FDDI, "FDDI"),
DLT_CHOICE(DLT_ATM_RFC1483, "LLC/SNAP encapsulated atm"),
DLT_CHOICE(DLT_LOOP, "loopback type (af header)"),
DLT_CHOICE(DLT_ENC, "IPSEC enc type (af header, spi, flags)"),
DLT_CHOICE(DLT_RAW, "raw IP"),
DLT_CHOICE(DLT_SLIP_BSDOS, "BSD/OS Serial Line IP"),
DLT_CHOICE(DLT_PPP_BSDOS, "BSD/OS Point-to-point Protocol"),
DLT_CHOICE(DLT_PFSYNC, "Packet filter state syncing"),
DLT_CHOICE(DLT_PPP_ETHER, "PPP over Ethernet; session only w/o ether header"),
DLT_CHOICE(DLT_IEEE802_11, "IEEE 802.11 wireless"),
DLT_CHOICE(DLT_PFLOG, "Packet filter logging, by pcap people"),
DLT_CHOICE(DLT_IEEE802_11_RADIO, "IEEE 802.11 plus WLAN header"),
DLT_CHOICE(DLT_OPENFLOW, "OpenFlow"),
DLT_CHOICE(DLT_USBPCAP, "USB"),
#undef DLT_CHOICE
	{ NULL, NULL, -1}
};

int
pcap_datalink_name_to_val(const char *name)
{
	int i;

	for (i = 0; dlts[i].name != NULL; i++) {
		/* Skip leading "DLT_" */
		if (strcasecmp(dlts[i].name + 4, name) == 0)
			return (dlts[i].dlt);
	}
	return (-1);
}

const char *
pcap_datalink_val_to_name(int dlt)
{
	int i;

	for (i = 0; dlts[i].name != NULL; i++) {
		if (dlts[i].dlt == dlt)
			return (dlts[i].name + 4); /* Skip leading "DLT_" */
	}
	return (NULL);
}

const char *
pcap_datalink_val_to_description(int dlt)
{
	int i;

	for (i = 0; dlts[i].name != NULL; i++) {
		if (dlts[i].dlt == dlt)
			return (dlts[i].description);
	}
	return (NULL);
}

int
pcap_snapshot(pcap_t *p)
{
	return (p->snapshot);
}

int
pcap_is_swapped(pcap_t *p)
{
	return (p->sf.swapped);
}

int
pcap_major_version(pcap_t *p)
{
	return (p->sf.version_major);
}

int
pcap_minor_version(pcap_t *p)
{
	return (p->sf.version_minor);
}

FILE *
pcap_file(pcap_t *p)
{
	return (p->sf.rfile);
}

int
pcap_fileno(pcap_t *p)
{
	return (p->fd);
}

void
pcap_perror(pcap_t *p, const char *prefix)
{
	fprintf(stderr, "%s: %s\n", prefix, p->errbuf);
}

int
pcap_get_selectable_fd(pcap_t *p)
{
	return (p->fd);
}

char *
pcap_geterr(pcap_t *p)
{
	return (p->errbuf);
}

int
pcap_getnonblock(pcap_t *p, char *errbuf)
{
	int fdflags;

	fdflags = fcntl(p->fd, F_GETFL);
	if (fdflags == -1) {
		snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "F_GETFL: %s",
		    pcap_strerror(errno));
		return (-1);
	}
	if (fdflags & O_NONBLOCK)
		return (1);
	else
		return (0);
}

int
pcap_setnonblock(pcap_t *p, int nonblock, char *errbuf)
{
	int fdflags;

	fdflags = fcntl(p->fd, F_GETFL);
	if (fdflags == -1) {
		snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "F_GETFL: %s",
		    pcap_strerror(errno));
		return (-1);
	}
	if (nonblock)
		fdflags |= O_NONBLOCK;
	else
		fdflags &= ~O_NONBLOCK;
	if (fcntl(p->fd, F_SETFL, fdflags) == -1) {
		snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "F_SETFL: %s",
		    pcap_strerror(errno));
		return (-1);
	}
	return (0);
}

/*
 * Generate error strings for PCAP_ERROR_ and PCAP_WARNING_ values.
 */
const char *
pcap_statustostr(int errnum)
{
	static char ebuf[15+10+1];

	switch (errnum) {

	case PCAP_WARNING:
		return("Generic warning");

	case PCAP_WARNING_TSTAMP_TYPE_NOTSUP:
		return ("That type of time stamp is not supported by that device");

	case PCAP_WARNING_PROMISC_NOTSUP:
		return ("That device doesn't support promiscuous mode");

	case PCAP_ERROR:
		return("Generic error");

	case PCAP_ERROR_BREAK:
		return("Loop terminated by pcap_breakloop");

	case PCAP_ERROR_NOT_ACTIVATED:
		return("The pcap_t has not been activated");

	case PCAP_ERROR_ACTIVATED:
		return ("The setting can't be changed after the pcap_t is activated");

	case PCAP_ERROR_NO_SUCH_DEVICE:
		return ("No such device exists");

	case PCAP_ERROR_RFMON_NOTSUP:
		return ("That device doesn't support monitor mode");

	case PCAP_ERROR_NOT_RFMON:
		return ("That operation is supported only in monitor mode");

	case PCAP_ERROR_PERM_DENIED:
		return ("You don't have permission to capture on that device");

	case PCAP_ERROR_IFACE_NOT_UP:
		return ("That device is not up");

	case PCAP_ERROR_CANTSET_TSTAMP_TYPE:
		return ("That device doesn't support setting the time stamp type");

	case PCAP_ERROR_PROMISC_PERM_DENIED:
		return ("You don't have permission to capture in promiscuous mode on that device");
	}
	(void)snprintf(ebuf, sizeof ebuf, "Unknown error: %d", errnum);
	return(ebuf);
}

/*
 * Not all systems have strerror().
 */
const char *
pcap_strerror(int errnum)
{
#ifdef HAVE_STRERROR
	return (strerror(errnum));
#else
	extern int sys_nerr;
	extern const char *const sys_errlist[];
	static char ebuf[20];

	if ((unsigned int)errnum < sys_nerr)
		return ((char *)sys_errlist[errnum]);
	(void)snprintf(ebuf, sizeof ebuf, "Unknown error: %d", errnum);
	return(ebuf);
#endif
}

/*
 * On some platforms, we need to clean up promiscuous or monitor mode
 * when we close a device - and we want that to happen even if the
 * application just exits without explicitl closing devices.
 * On those platforms, we need to register a "close all the pcaps"
 * routine to be called when we exit, and need to maintain a list of
 * pcaps that need to be closed to clean up modes.
 *
 * XXX - not thread-safe.
 */

/*
 * List of pcaps on which we've done something that needs to be
 * cleaned up.
 * If there are any such pcaps, we arrange to call "pcap_close_all()"
 * when we exit, and have it close all of them.
 */
static struct pcap *pcaps_to_close;

/*
 * TRUE if we've already called "atexit()" to cause "pcap_close_all()" to
 * be called on exit.
 */
static int did_atexit;

static void
pcap_close_all(void)
{
	struct pcap *handle;

	while ((handle = pcaps_to_close) != NULL)
		pcap_close(handle);
}

int
pcap_do_addexit(pcap_t *p)
{
	/*
	 * If we haven't already done so, arrange to have
	 * "pcap_close_all()" called when we exit.
	 */
	if (!did_atexit) {
		if (atexit(pcap_close_all) == -1) {
			/*
			 * "atexit()" failed; let our caller know.
			 */
			(void)strlcpy(p->errbuf, "atexit failed",
			    PCAP_ERRBUF_SIZE);
			return (0);
		}
		did_atexit = 1;
	}
	return (1);
}

void
pcap_add_to_pcaps_to_close(pcap_t *p)
{
	p->md.next = pcaps_to_close;
	pcaps_to_close = p;
}

void
pcap_remove_from_pcaps_to_close(pcap_t *p)
{
	pcap_t *pc, *prevpc;

	for (pc = pcaps_to_close, prevpc = NULL; pc != NULL;
	    prevpc = pc, pc = pc->md.next) {
		if (pc == p) {
			/*
			 * Found it.  Remove it from the list.
			 */
			if (prevpc == NULL) {
				/*
				 * It was at the head of the list.
				 */
				pcaps_to_close = pc->md.next;
			} else {
				/*
				 * It was in the middle of the list.
				 */
				prevpc->md.next = pc->md.next;
			}
			break;
		}
	}
}

pcap_t *
pcap_open_dead(int linktype, int snaplen)
{
	pcap_t *p;

	p = calloc(1, sizeof(*p));
	if (p == NULL)
		return NULL;
	p->snapshot = snaplen;
	p->linktype = linktype;
	p->fd = -1;
	return p;
}

/*
 * Given a BPF program, a pcap_pkthdr structure for a packet, and the raw
 * data for the packet, check whether the packet passes the filter.
 * Returns the return value of the filter program, which will be zero if
 * the packet doesn't pass and non-zero if the packet does pass.
 */
int
pcap_offline_filter(const struct bpf_program *fp, const struct pcap_pkthdr *h,
        const u_char *pkt)
{
	struct bpf_insn *fcode = fp->bf_insns;

	if (fcode != NULL)
		return (bpf_filter(fcode, pkt, h->len, h->caplen));
	else
		return (0);
}

const char *
pcap_lib_version(void)
{
	return (pcap_version_string);
}

