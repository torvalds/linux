/*	$OpenBSD: pcap.h,v 1.22 2021/01/18 09:26:35 sthen Exp $	*/

/*
 * Copyright (c) 1993, 1994, 1995, 1996, 1997
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

#ifndef lib_pcap_h
#define lib_pcap_h

#include <sys/types.h>
#include <sys/time.h>

#include <net/bpf.h>

#include <stdio.h>

#define PCAP_VERSION_MAJOR 2
#define PCAP_VERSION_MINOR 4

#define PCAP_ERRBUF_SIZE 256

/*
 * Compatibility for systems that have a bpf.h that
 * predates the bpf typedefs for 64-bit support.
 */
#if BPF_RELEASE - 0 < 199406
typedef	int bpf_int32;
typedef	u_int bpf_u_int32;
#endif

typedef struct pcap pcap_t;
typedef struct pcap_if pcap_if_t;
typedef struct pcap_addr pcap_addr_t;
typedef struct pcap_dumper pcap_dumper_t;

/*
 * The first record in the file contains saved values for some
 * of the flags used in the printout phases of tcpdump.
 * Many fields here are 32 bit ints so compilers won't insert unwanted
 * padding; these files need to be interchangeable across architectures.
 */
struct pcap_file_header {
	bpf_u_int32 magic;
	u_short version_major;
	u_short version_minor;
	bpf_int32 thiszone;	/* gmt to local correction */
	bpf_u_int32 sigfigs;	/* accuracy of timestamps */
	bpf_u_int32 snaplen;	/* max length saved portion of each pkt */
	bpf_u_int32 linktype;	/* data link type (DLT_*) */
};

typedef enum {
       PCAP_D_INOUT = 0,
       PCAP_D_IN,
       PCAP_D_OUT
} pcap_direction_t;

/*
 * Each packet in the dump file is prepended with this generic header.
 * This gets around the problem of different headers for different
 * packet interfaces.
 */
struct pcap_pkthdr {
	struct bpf_timeval ts;	/* time stamp */
	bpf_u_int32 caplen;	/* length of portion present */
	bpf_u_int32 len;	/* length of this packet (off wire) */
};

/*
 * As returned by the pcap_stats()
 */
struct pcap_stat {
	u_int ps_recv;		/* number of packets received */
	u_int ps_drop;		/* number of packets dropped */
	u_int ps_ifdrop;	/* drops by interface XXX not yet supported */
};

/*
 * Item in a list of interfaces.
 */
struct pcap_if {
	struct pcap_if *next;
	char *name;		/* name to hand to "pcap_open_live()" */
	char *description;	/* textual description of interface, or NULL */
	struct pcap_addr *addresses;
	bpf_u_int32 flags;	/* PCAP_IF_ interface flags */
};

#define PCAP_IF_LOOPBACK	0x00000001	/* interface is loopback */

/*
 * Representation of an interface address.
 */
struct pcap_addr {
	struct pcap_addr *next;
	struct sockaddr *addr;		/* address */
	struct sockaddr *netmask;	/* netmask for that address */
	struct sockaddr *broadaddr;	/* broadcast address for that address */
	struct sockaddr *dstaddr;	/* P2P destination address for that address */
};

/*
 * Error codes for the pcap API.
 * These will all be negative, so you can check for the success or
 * failure of a call that returns these codes by checking for a
 * negative value.
 */
#define PCAP_ERROR			-1	/* generic error code */
#define PCAP_ERROR_BREAK		-2	/* loop terminated by pcap_breakloop */
#define PCAP_ERROR_NOT_ACTIVATED	-3	/* the capture needs to be activated */
#define PCAP_ERROR_ACTIVATED		-4	/* the operation can't be performed on already activated captures */
#define PCAP_ERROR_NO_SUCH_DEVICE	-5	/* no such device exists */
#define PCAP_ERROR_RFMON_NOTSUP		-6	/* this device doesn't support rfmon (monitor) mode */
#define PCAP_ERROR_NOT_RFMON		-7	/* operation supported only in monitor mode */
#define PCAP_ERROR_PERM_DENIED		-8	/* no permission to open the device */
#define PCAP_ERROR_IFACE_NOT_UP		-9	/* interface isn't up */
#define PCAP_ERROR_CANTSET_TSTAMP_TYPE	-10	/* this device doesn't support setting the time stamp type */
#define PCAP_ERROR_PROMISC_PERM_DENIED	-11	/* you don't have permission to capture in promiscuous mode */

/*
 * Warning codes for the pcap API.
 * These will all be positive and non-zero, so they won't look like
 * errors.
 */
#define PCAP_WARNING			1	/* generic warning code */
#define PCAP_WARNING_PROMISC_NOTSUP	2	/* this device doesn't support promiscuous mode */
#define PCAP_WARNING_TSTAMP_TYPE_NOTSUP	3	/* the requested time stamp type is not supported */

/*
 * Value to pass to pcap_compile() as the netmask if you don't know what
 * the netmask is.
 */
#define PCAP_NETMASK_UNKNOWN		0xffffffff

typedef void (*pcap_handler)(u_char *, const struct pcap_pkthdr *,
			     const u_char *);

__BEGIN_DECLS
char	*pcap_lookupdev(char *);
int	pcap_lookupnet(const char *, bpf_u_int32 *, bpf_u_int32 *, char *);

pcap_t	*pcap_create(const char *, char *);
int	pcap_set_snaplen(pcap_t *, int);
int	pcap_set_promisc(pcap_t *, int);
int	pcap_can_set_rfmon(pcap_t *);
int	pcap_set_rfmon(pcap_t *, int);
int	pcap_set_timeout(pcap_t *, int);
int	pcap_set_immediate_mode(pcap_t *, int);
int	pcap_set_buffer_size(pcap_t *, int);
int	pcap_activate(pcap_t *);

pcap_t	*pcap_open_live(const char *, int, int, int, char *);
pcap_t	*pcap_open_dead(int, int);
pcap_t	*pcap_open_offline(const char *, char *);
pcap_t	*pcap_fopen_offline(FILE *, char *);
void	pcap_close(pcap_t *);
int	pcap_loop(pcap_t *, int, pcap_handler, u_char *);
int	pcap_dispatch(pcap_t *, int, pcap_handler, u_char *);
const u_char*
	pcap_next(pcap_t *, struct pcap_pkthdr *);
int 	pcap_next_ex(pcap_t *, struct pcap_pkthdr **, const u_char **);
void	pcap_breakloop(pcap_t *);
int	pcap_stats(pcap_t *, struct pcap_stat *);
int	pcap_setfilter(pcap_t *, struct bpf_program *);
int 	pcap_setdirection(pcap_t *, pcap_direction_t);
int	pcap_getnonblock(pcap_t *, char *);
int	pcap_setnonblock(pcap_t *, int, char *);
void	pcap_perror(pcap_t *, const char *);
int	pcap_inject(pcap_t *, const void *, size_t);
int	pcap_sendpacket(pcap_t *, const u_char *, int);
const char *pcap_statustostr(int);
const char *pcap_strerror(int);
char	*pcap_geterr(pcap_t *);
int	pcap_compile(pcap_t *, struct bpf_program *, const char *, int,
	    bpf_u_int32);
int	pcap_compile_nopcap(int, int, struct bpf_program *,
	    const char *, int, bpf_u_int32);
void	pcap_freecode(struct bpf_program *);
int	pcap_offline_filter(const struct bpf_program *,
	    const struct pcap_pkthdr *, const u_char *);
int	pcap_datalink(pcap_t *);
int	pcap_list_datalinks(pcap_t *, int **);
int	pcap_set_datalink(pcap_t *, int);
void	pcap_free_datalinks(int *);
int	pcap_datalink_name_to_val(const char *);
const char *pcap_datalink_val_to_name(int);
const char *pcap_datalink_val_to_description(int);
int	pcap_snapshot(pcap_t *);
int	pcap_is_swapped(pcap_t *);
int	pcap_major_version(pcap_t *);
int	pcap_minor_version(pcap_t *);

/* XXX */
FILE	*pcap_file(pcap_t *);
int	pcap_fileno(pcap_t *);

pcap_dumper_t *pcap_dump_open(pcap_t *, const char *);
pcap_dumper_t *pcap_dump_fopen(pcap_t *, FILE *fp);
FILE	*pcap_dump_file(pcap_dumper_t *);
long	pcap_dump_ftell(pcap_dumper_t *);
int	pcap_dump_flush(pcap_dumper_t *);
void	pcap_dump_close(pcap_dumper_t *);
void	pcap_dump(u_char *, const struct pcap_pkthdr *, const u_char *);

int	pcap_findalldevs(pcap_if_t **, char *);
void	pcap_freealldevs(pcap_if_t *);

const char *pcap_lib_version(void);

char	*bpf_image(const struct bpf_insn *, int);

int	pcap_get_selectable_fd(pcap_t *);

__END_DECLS
#endif
