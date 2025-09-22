/*	$OpenBSD: pflogd.h,v 1.8 2024/05/21 05:00:47 jsg Exp $ */

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

#include <sys/limits.h>
#include <pcap.h>

#define DEF_SNAPLEN 160		/* pfloghdr + ip hdr + proto hdr fit usually */
#define PCAP_TO_MS 500		/* pcap read timeout (ms) */
#define PCAP_NUM_PKTS 1000	/* max number of packets to process at each loop */
#define PCAP_OPT_FIL 1		/* filter optimization */
#define FLUSH_DELAY 60		/* flush delay */

#define PFLOGD_LOG_FILE		"/var/log/pflog"
#define PFLOGD_DEFAULT_IF	"pflog0"

#define PFLOGD_MAXSNAPLEN	INT_MAX
#define PFLOGD_BUFSIZE		65536	/* buffer size for incoming packets */

void  logmsg(int priority, const char *message, ...);

/* Privilege separation */
void	priv_init(int, int, char **);
int	priv_init_pcap(int);
int	priv_set_snaplen(int snaplen);
int	priv_open_log(void);

int   init_pcap(void);
void set_pcap_filter(void);
/* File descriptor send/recv */
void send_fd(int, int);
int  receive_fd(int);

extern int Debug;
