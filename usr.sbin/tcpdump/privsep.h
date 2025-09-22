/*
 * Copyright (c) 2003 Can Erkin Acar
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _PRIVSEP_H_
#define _PRIVSEP_H_

#include <pcap-int.h>

#define TCPDUMP_MAGIC 0xa1b2c3d4

enum cmd_types {
	PRIV_OPEN_BPF,		/* open a bpf descriptor */
	PRIV_OPEN_DUMP,		/* open dump file for reading */
	PRIV_OPEN_PFOSFP,	/* open pf.os(5) fingerprint db for reading */
	PRIV_OPEN_OUTPUT,	/* open output file */
	PRIV_SETFILTER,		/* set a bpf read filter */
	PRIV_GETHOSTBYADDR,	/* resolve numeric address into hostname */
	PRIV_ETHER_NTOHOST,	/* translate ethernet address into host name */
	PRIV_GETRPCBYNUMBER,	/* translate rpc number into name */
	PRIV_GETSERVENTRIES,	/* get the service entries table */
	PRIV_GETPROTOENTRIES,	/* get the ip protocol entries table */
	PRIV_LOCALTIME,		/* return localtime */
	PRIV_INIT_DONE,		/* signal that the initialization is done */
	PRIV_PCAP_STATS		/* get pcap_stats() results */
};

struct ether_addr;

/* Privilege separation */
int	priv_init(int, char **);
__dead void priv_exec(int, char **);
void    priv_init_done(void);

int	setfilter(int, int, char *);
int	pcap_live(const char *, int, int, u_int, u_int, u_int);

struct bpf_program *priv_pcap_setfilter(pcap_t *, int, u_int32_t);
pcap_t *priv_pcap_live(const char *, int, int, int, char *, u_int,
	    u_int, u_int);
pcap_t *priv_pcap_offline(const char *, char *);

size_t	priv_gethostbyaddr(char *, size_t, int, char *, size_t);
size_t	priv_ether_ntohost(char *, size_t, struct ether_addr *);
size_t	priv_getrpcbynumber(int, char *, size_t);

struct tm *priv_localtime(const time_t *);

/* Start getting service entries */
void	priv_getserventries(void);

/* Retrieve a single service entry, should be called repeatedly after
   calling priv_getserventries() until it returns zero */
size_t	priv_getserventry(char *, size_t, int *, char *, size_t);

/* Start getting ip protocol entries */
void	priv_getprotoentries(void);

/* Retrieve a single protocol entry, should be called repeatedly after
   calling priv_getprotoentries() until it returns zero */
size_t	priv_getprotoentry(char *, size_t, int *);

/* Retrieve pf.os(5) fingerprints file descriptor */
int	priv_open_pfosfp();

/* Return the pcap statistics upon completion */
int	priv_pcap_stats(struct pcap_stat *);

pcap_dumper_t *priv_pcap_dump_open(pcap_t *, char *);

/* File descriptor send/recv */
void	send_fd(int, int);
int	receive_fd(int);

/* communications over the channel */
int	may_read(int, void *, size_t);
void	must_read(int, void *, size_t);
void	must_write(int, const void *, size_t);
size_t	read_block(int, char *, size_t, const char *);
size_t	read_string(int, char *, size_t, const char *);
void	write_block(int, size_t, const char *);
void	write_command(int, int);
void	write_string(int, const char *);
void	write_zero(int);

extern int priv_fd;

#endif
