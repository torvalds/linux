/* $OpenBSD: log.c,v 1.65 2024/04/28 16:43:42 florian Exp $	 */
/* $EOM: log.c,v 1.30 2000/09/29 08:19:23 niklas Exp $	 */

/*
 * Copyright (c) 1998, 1999, 2001 Niklas Hallqvist.  All rights reserved.
 * Copyright (c) 1999, 2000, 2001, 2003 Håkan Olsson.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This code was written under funding by Ericsson Radio Systems.
 */

#include <sys/types.h>
#include <sys/time.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/udp.h>
#include <arpa/inet.h>

#include <pcap.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <stdarg.h>
#include <unistd.h>

#include "conf.h"
#include "isakmp_num.h"
#include "log.h"
#include "monitor.h"
#include "util.h"

static void	_log_print(int, int, const char *, va_list, int, int);

static FILE	*log_output;

int		verbose_logging = 0;
static int	log_level[LOG_ENDCLASS];

#define TCPDUMP_MAGIC	0xa1b2c3d4
#define SNAPLEN		(64 * 1024)

struct packhdr {
	struct pcap_pkthdr pcap;/* pcap file packet header */
	u_int32_t sa_family;	/* address family */
	union {
		struct ip       ip4;	/* IPv4 header (w/o options) */
		struct ip6_hdr  ip6;	/* IPv6 header */
	} ip;
};

struct isakmp_hdr {
	u_int8_t        icookie[8], rcookie[8];
	u_int8_t        next, ver, type, flags;
	u_int32_t       msgid, len;
};

static char    *pcaplog_file = NULL;
static FILE    *packet_log;
static u_int8_t *packet_buf = NULL;

static int      udp_cksum(struct packhdr *, const struct udphdr *,
    u_int16_t *);
static u_int16_t in_cksum(const u_int16_t *, int);

void
log_init(int debug)
{
	if (debug)
		log_output = stderr;
	else
		log_to(0);	/* syslog */
}

void
log_reinit(void)
{
	struct conf_list *logging;
	struct conf_list_node *logclass;
	int		class, level;

	logging = conf_get_list("General", "Logverbose");
	if (logging) {
		verbose_logging = 1;
		conf_free_list(logging);
	}
	logging = conf_get_list("General", "Loglevel");
	if (!logging)
		return;

	for (logclass = TAILQ_FIRST(&logging->fields); logclass;
	    logclass = TAILQ_NEXT(logclass, link)) {
		if (sscanf(logclass->field, "%d=%d", &class, &level) != 2) {
			if (sscanf(logclass->field, "A=%d", &level) == 1)
				for (class = 0; class < LOG_ENDCLASS; class++)
					log_debug_cmd(class, level);
			else {
				log_print("init: invalid logging class or "
				    "level: %s", logclass->field);
				continue;
			}
		} else
			log_debug_cmd(class, level);
	}
	conf_free_list(logging);
}

void
log_to(FILE *f)
{
	if (!log_output && f)
		closelog();
	log_output = f;
	if (!f)
		openlog("isakmpd", LOG_PID | LOG_CONS, LOG_DAEMON);
}

FILE *
log_current(void)
{
	return log_output;
}

static char *
_log_get_class(int error_class)
{
	/* XXX For test purposes. To be removed later on?  */
	static char	*class_text[] = LOG_CLASSES_TEXT;

	if (error_class < 0)
		return "Dflt";
	else if (error_class >= LOG_ENDCLASS)
		return "Unkn";
	else
		return class_text[error_class];
}

static void
_log_print(int error, int syslog_level, const char *fmt, va_list ap,
    int class, int level)
{
	char		buffer[LOG_SIZE], nbuf[LOG_SIZE + 32];
	static const char fallback_msg[] =
	    "write to log file failed (errno %d), redirecting to syslog";
	int		len;
	struct tm      *tm;
	struct timeval  now;
	time_t          t;

	len = vsnprintf(buffer, sizeof buffer, fmt, ap);
	if (len > 0 && len < (int) sizeof buffer - 1 && error)
		snprintf(buffer + len, sizeof buffer - len, ": %s",
		    strerror(errno));
	if (log_output) {
		gettimeofday(&now, 0);
		t = now.tv_sec;
		if ((tm = localtime(&t)) == NULL) {
			/* Invalid time, use the epoch. */
			t = 0;
			tm = localtime(&t);
		}
		if (class >= 0)
			snprintf(nbuf, sizeof nbuf,
			    "%02d%02d%02d.%06ld %s %02d ",
			    tm->tm_hour, tm->tm_min, tm->tm_sec, now.tv_usec,
			    _log_get_class(class), level);
		else /* LOG_PRINT (-1) or LOG_REPORT (-2) */
			snprintf(nbuf, sizeof nbuf, "%02d%02d%02d.%06ld %s ",
			    tm->tm_hour, tm->tm_min, tm->tm_sec, now.tv_usec,
			    class == LOG_PRINT ? "Default" : "Report>");
		strlcat(nbuf, buffer, sizeof nbuf);
		strlcat(nbuf, getuid() ? "" : " [priv]", LOG_SIZE + 32);
		strlcat(nbuf, "\n", sizeof nbuf);

		if (fwrite(nbuf, strlen(nbuf), 1, log_output) == 0) {
			/* Report fallback.  */
			syslog(LOG_ALERT, fallback_msg, errno);
			fprintf(log_output, fallback_msg, errno);

			/*
			 * Close log_output to prevent isakmpd from locking
			 * the file.  We may need to explicitly close stdout
			 * to do this properly.
			 * XXX - Figure out how to match two FILE *'s and
			 * rewrite.
			 */
			if (fileno(log_output) != -1 &&
			    fileno(stdout) == fileno(log_output))
				fclose(stdout);
			fclose(log_output);

			/* Fallback to syslog.  */
			log_to(0);

			/* (Re)send current message to syslog().  */
			syslog(class == LOG_REPORT ? LOG_ALERT :
			    syslog_level, "%s", buffer);
		}
	} else
		syslog(class == LOG_REPORT ? LOG_ALERT : syslog_level, "%s",
		    buffer);
}

void
log_debug(int cls, int level, const char *fmt, ...)
{
	va_list         ap;

	/*
	 * If we are not debugging this class, or the level is too low, just
	 * return.
	 */
	if (cls >= 0 && (log_level[cls] == 0 || level > log_level[cls]))
		return;
	va_start(ap, fmt);
	_log_print(0, LOG_INFO, fmt, ap, cls, level);
	va_end(ap);
}

void
log_debug_buf(int cls, int level, const char *header, const u_int8_t *buf,
    size_t sz)
{
	size_t	i, j;
	char	s[73];

	/*
	 * If we are not debugging this class, or the level is too low, just
	 * return.
	 */
	if (cls >= 0 && (log_level[cls] == 0 || level > log_level[cls]))
		return;

	log_debug(cls, level, "%s:", header);
	for (i = j = 0; i < sz;) {
		snprintf(s + j, sizeof s - j, "%02x", buf[i++]);
		j += strlen(s + j);
		if (i % 4 == 0) {
			if (i % 32 == 0) {
				s[j] = '\0';
				log_debug(cls, level, "%s", s);
				j = 0;
			} else
				s[j++] = ' ';
		}
	}
	if (j) {
		s[j] = '\0';
		log_debug(cls, level, "%s", s);
	}
}

void
log_debug_cmd(int cls, int level)
{
	if (cls < 0 || cls >= LOG_ENDCLASS) {
		log_print("log_debug_cmd: invalid debugging class %d", cls);
		return;
	}
	if (level < 0) {
		log_print("log_debug_cmd: invalid debugging level %d for "
		    "class %d", level, cls);
		return;
	}
	if (level == log_level[cls])
		log_print("log_debug_cmd: log level unchanged for class %d",
		    cls);
	else {
		log_print("log_debug_cmd: log level changed from %d to %d "
		    "for class %d", log_level[cls], level, cls);
		log_level[cls] = level;
	}
}

void
log_debug_toggle(void)
{
	static int	log_level_copy[LOG_ENDCLASS], toggle = 0;

	if (!toggle) {
		LOG_DBG((LOG_MISC, 50, "log_debug_toggle: "
		    "debug levels cleared"));
		memcpy(&log_level_copy, &log_level, sizeof log_level);
		bzero(&log_level, sizeof log_level);
	} else {
		memcpy(&log_level, &log_level_copy, sizeof log_level);
		LOG_DBG((LOG_MISC, 50, "log_debug_toggle: "
		    "debug levels restored"));
	}
	toggle = !toggle;
}

void
log_print(const char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);
	_log_print(0, LOG_NOTICE, fmt, ap, LOG_PRINT, 0);
	va_end(ap);
}

void
log_verbose(const char *fmt, ...)
{
	va_list	ap;

	if (verbose_logging == 0)
		return;

	va_start(ap, fmt);
	_log_print(0, LOG_NOTICE, fmt, ap, LOG_PRINT, 0);
	va_end(ap);
}

void
log_error(const char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);
	_log_print(1, LOG_ERR, fmt, ap, LOG_PRINT, 0);
	va_end(ap);
}

void
log_errorx(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	_log_print(0, LOG_ERR, fmt, ap, LOG_PRINT, 0);
	va_end(ap);
}

void
log_fatal(const char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);
	_log_print(1, LOG_CRIT, fmt, ap, LOG_PRINT, 0);
	va_end(ap);
	monitor_exit(1);
}

void
log_fatalx(const char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);
	_log_print(0, LOG_CRIT, fmt, ap, LOG_PRINT, 0);
	va_end(ap);
	monitor_exit(1);
}

void
log_packet_init(char *newname)
{
	struct pcap_file_header sf_hdr;
	struct stat     st;
	mode_t          old_umask;
	char           *mode;

	/* Allocate packet buffer first time through.  */
	if (!packet_buf)
		packet_buf = malloc(SNAPLEN);

	if (!packet_buf) {
		log_error("log_packet_init: malloc (%d) failed", SNAPLEN);
		return;
	}
	if (pcaplog_file && strcmp(pcaplog_file, PCAP_FILE_DEFAULT) != 0)
		free(pcaplog_file);

	pcaplog_file = strdup(newname);
	if (!pcaplog_file) {
		log_error("log_packet_init: strdup (\"%s\") failed", newname);
		return;
	}
	/* Does the file already exist?  XXX lstat() or stat()?  */
	/* XXX This is a fstat! */
	if (monitor_stat(pcaplog_file, &st) == 0) {
		/* Sanity checks.  */
		if (!S_ISREG(st.st_mode)) {
			log_print("log_packet_init: existing capture file is "
			    "not a regular file");
			return;
		}
		if ((st.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
			log_print("log_packet_init: existing capture "
			    "file has bad modes");
			return;
		}
		/*
		 * XXX It would be nice to check if it actually is a pcap
		 * file...
		 */

		mode = "a";
	} else
		mode = "w";

	old_umask = umask(S_IRWXG | S_IRWXO);
	packet_log = monitor_fopen(pcaplog_file, mode);
	umask(old_umask);

	if (!packet_log) {
		log_error("log_packet_init: fopen (\"%s\", \"%s\") failed",
		    pcaplog_file, mode);
		return;
	}
	log_print("log_packet_init: "
	    "starting IKE packet capture to file \"%s\"", pcaplog_file);

	/* If this is a new file, we need to write a PCAP header to it.  */
	if (*mode == 'w') {
		sf_hdr.magic = TCPDUMP_MAGIC;
		sf_hdr.version_major = PCAP_VERSION_MAJOR;
		sf_hdr.version_minor = PCAP_VERSION_MINOR;
		sf_hdr.thiszone = 0;
		sf_hdr.snaplen = SNAPLEN;
		sf_hdr.sigfigs = 0;
		sf_hdr.linktype = DLT_LOOP;

		fwrite((char *) &sf_hdr, sizeof sf_hdr, 1, packet_log);
		fflush(packet_log);
	}
}

void
log_packet_restart(char *newname)
{
	if (packet_log) {
		log_print("log_packet_restart: capture already active on "
		    "file \"%s\"", pcaplog_file);
		return;
	}
	if (newname)
		log_packet_init(newname);
	else if (!pcaplog_file)
		log_packet_init(PCAP_FILE_DEFAULT);
	else
		log_packet_init(pcaplog_file);
}

void
log_packet_stop(void)
{
	/* Stop capture.  */
	if (packet_log) {
		fclose(packet_log);
		log_print("log_packet_stop: stopped capture");
	}
	packet_log = 0;
}

void
log_packet_iov(struct sockaddr *src, struct sockaddr *dst, struct iovec *iov,
    int iovcnt)
{
	struct isakmp_hdr *isakmphdr;
	struct packhdr  hdr;
	struct udphdr   udp;
	struct timeval  tv;
	int             off, datalen, hdrlen, i, add_espmarker = 0;
	const u_int32_t	espmarker = 0;

	for (i = 0, datalen = 0; i < iovcnt; i++)
		datalen += iov[i].iov_len;

	if (!packet_log || datalen > SNAPLEN)
		return;

	/* copy packet into buffer */
	for (i = 0, off = 0; i < iovcnt; i++) {
		memcpy(packet_buf + off, iov[i].iov_base, iov[i].iov_len);
		off += iov[i].iov_len;
	}

	bzero(&hdr, sizeof hdr);
	bzero(&udp, sizeof udp);

	/* isakmp - turn off the encryption bit in the isakmp hdr */
	isakmphdr = (struct isakmp_hdr *) packet_buf;
	isakmphdr->flags &= ~(ISAKMP_FLAGS_ENC);

	/* udp */
	udp.uh_sport = sockaddr_port(src);
	udp.uh_dport = sockaddr_port(dst);
	datalen += sizeof udp;
	if (ntohs(udp.uh_sport) == 4500 ||
	    ntohs(udp.uh_dport) == 4500) { /* XXX Quick and dirty */
		add_espmarker = 1;
		datalen += sizeof espmarker;
	}
	udp.uh_ulen = htons(datalen);

	/* ip */
	hdr.sa_family = htonl(src->sa_family);
	switch (src->sa_family) {
	default:
		/* Assume IPv4. XXX Can 'default' ever happen here?  */
		hdr.sa_family = htonl(AF_INET);
		hdr.ip.ip4.ip_src.s_addr = 0x02020202;
		hdr.ip.ip4.ip_dst.s_addr = 0x01010101;
		/* The rest of the setup is common to AF_INET.  */
		goto setup_ip4;

	case AF_INET:
		hdr.ip.ip4.ip_src.s_addr =
		    ((struct sockaddr_in *)src)->sin_addr.s_addr;
		hdr.ip.ip4.ip_dst.s_addr =
		    ((struct sockaddr_in *)dst)->sin_addr.s_addr;

setup_ip4:
		hdrlen = sizeof hdr.ip.ip4;
		hdr.ip.ip4.ip_v = 0x4;
		hdr.ip.ip4.ip_hl = 0x5;
		hdr.ip.ip4.ip_p = IPPROTO_UDP;
		hdr.ip.ip4.ip_len = htons(datalen + hdrlen);
		/* Let's use the IP ID as a "packet counter".  */
		i = ntohs(hdr.ip.ip4.ip_id) + 1;
		hdr.ip.ip4.ip_id = htons(i);
		/* Calculate IP header checksum. */
		hdr.ip.ip4.ip_sum = in_cksum((u_int16_t *) & hdr.ip.ip4,
		    hdr.ip.ip4.ip_hl << 2);
		break;

	case AF_INET6:
		hdrlen = sizeof(hdr.ip.ip6);
		hdr.ip.ip6.ip6_vfc = IPV6_VERSION;
		hdr.ip.ip6.ip6_nxt = IPPROTO_UDP;
		hdr.ip.ip6.ip6_plen = udp.uh_ulen;
		memcpy(&hdr.ip.ip6.ip6_src,
		    &((struct sockaddr_in6 *)src)->sin6_addr,
		    sizeof hdr.ip.ip6.ip6_src);
		memcpy(&hdr.ip.ip6.ip6_dst,
		    &((struct sockaddr_in6 *)dst)->sin6_addr,
		    sizeof hdr.ip.ip6.ip6_dst);
		break;
	}

	/* Calculate UDP checksum.  */
	udp.uh_sum = udp_cksum(&hdr, &udp, (u_int16_t *) packet_buf);
	hdrlen += sizeof hdr.sa_family;

	/* pcap file packet header */
	gettimeofday(&tv, 0);
	hdr.pcap.ts.tv_sec = tv.tv_sec;
	hdr.pcap.ts.tv_usec = tv.tv_usec;
	hdr.pcap.caplen = datalen + hdrlen;
	hdr.pcap.len = datalen + hdrlen;

	hdrlen += sizeof(struct pcap_pkthdr);
	datalen -= sizeof(struct udphdr);

	/* Write to pcap file.  */
	fwrite(&hdr, hdrlen, 1, packet_log);	/* pcap + IP */
	fwrite(&udp, sizeof(struct udphdr), 1, packet_log);	/* UDP */
	if (add_espmarker) {
		fwrite(&espmarker, sizeof espmarker, 1, packet_log);
		datalen -= sizeof espmarker;
	}
	fwrite(packet_buf, datalen, 1, packet_log);	/* IKE-data */
	fflush(packet_log);
}

/* Copied from tcpdump/print-udp.c, mostly rewritten.  */
static int
udp_cksum(struct packhdr *hdr, const struct udphdr *u, u_int16_t *d)
{
	struct ip	*ip4;
	struct ip6_hdr	*ip6;
	int	i, hdrlen, tlen = ntohs(u->uh_ulen) - sizeof(struct udphdr);

	union phu {
		struct ip4pseudo {
			struct in_addr  src;
			struct in_addr  dst;
			u_int8_t        z;
			u_int8_t        proto;
			u_int16_t       len;
		} ip4p;
		struct ip6pseudo {
			struct in6_addr src;
			struct in6_addr dst;
			u_int32_t       plen;
			u_int16_t       z0;
			u_int8_t        z1;
			u_int8_t        nxt;
		} ip6p;
		u_int16_t       pa[20];
	} phu;
	const u_int16_t *sp;
	u_int32_t       sum;

	/* Setup pseudoheader.  */
	bzero(phu.pa, sizeof phu);
	switch (ntohl(hdr->sa_family)) {
	case AF_INET:
		ip4 = &hdr->ip.ip4;
		memcpy(&phu.ip4p.src, &ip4->ip_src, sizeof(struct in_addr));
		memcpy(&phu.ip4p.dst, &ip4->ip_dst, sizeof(struct in_addr));
		phu.ip4p.proto = ip4->ip_p;
		phu.ip4p.len = u->uh_ulen;
		hdrlen = sizeof phu.ip4p;
		break;

	case AF_INET6:
		ip6 = &hdr->ip.ip6;
		memcpy(&phu.ip6p.src, &ip6->ip6_src, sizeof(phu.ip6p.src));
		memcpy(&phu.ip6p.dst, &ip6->ip6_dst, sizeof(phu.ip6p.dst));
		phu.ip6p.plen = u->uh_ulen;
		phu.ip6p.nxt = ip6->ip6_nxt;
		hdrlen = sizeof phu.ip6p;
		break;

	default:
		return 0;
	}

	/* IPv6 wants a 0xFFFF checksum "on error", not 0x0.  */
	if (tlen < 0)
		return (ntohl(hdr->sa_family) == AF_INET ? 0 : 0xFFFF);

	sum = 0;
	for (i = 0; i < hdrlen; i += 2)
		sum += phu.pa[i / 2];

	sp = (const u_int16_t *)u;
	for (i = 0; i < (int)sizeof(struct udphdr); i += 2)
		sum += *sp++;

	sp = d;
	for (i = 0; i < (tlen & ~1); i += 2)
		sum += *sp++;

	if (tlen & 1)
		sum += htons((*(const char *)sp) << 8);

	while (sum > 0xffff)
		sum = (sum & 0xffff) + (sum >> 16);
	sum = ~sum & 0xffff;

	return sum;
}

/* Copied from tcpdump/print-ip.c, modified.  */
static u_int16_t
in_cksum(const u_int16_t *w, int len)
{
	int		nleft = len, sum = 0;
	u_int16_t       answer;

	while (nleft > 1) {
		sum += *w++;
		nleft -= 2;
	}
	if (nleft == 1)
		sum += htons(*(const u_char *)w << 8);

	sum = (sum >> 16) + (sum & 0xffff);	/* add hi 16 to low 16 */
	sum += (sum >> 16);	/* add carry */
	answer = ~sum;		/* truncate to 16 bits */
	return answer;
}
