/*	$OpenBSD: pcap-bpf.c,v 1.39 2023/03/08 04:43:05 guenther Exp $	*/

/*
 * Copyright (c) 1993, 1994, 1995, 1996, 1998
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <net/if.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <net/if_media.h>

#include "pcap-int.h"

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

#include "gencode.h"

static int find_802_11(struct bpf_dltlist *);
static int monitor_mode(pcap_t *, int);

int
pcap_stats(pcap_t *p, struct pcap_stat *ps)
{
	struct bpf_stat s;

	if (ioctl(p->fd, BIOCGSTATS, (caddr_t)&s) == -1) {
		snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "BIOCGSTATS: %s",
		    pcap_strerror(errno));
		return (PCAP_ERROR);
	}

	ps->ps_recv = s.bs_recv;
	ps->ps_drop = s.bs_drop;
	return (0);
}

int
pcap_read(pcap_t *p, int cnt, pcap_handler callback, u_char *user)
{
	int cc;
	int n = 0;
	u_char *bp, *ep;

 again:
	/*
	 * Has "pcap_breakloop()" been called?
	 */
	if (p->break_loop) {
		/*
		 * Yes - clear the flag that indicates that it
		 * has, and return PCAP_ERROR_BREAK to indicate
		 * that we were told to break out of the loop.
		 */
		p->break_loop = 0;
		return (PCAP_ERROR_BREAK);
	}

	cc = p->cc;
	if (p->cc == 0) {
		cc = read(p->fd, (char *)p->buffer, p->bufsize);
		if (cc == -1) {
			/* Don't choke when we get ptraced */
			switch (errno) {

			case EINTR:
				goto again;

			case EWOULDBLOCK:
				return (0);

			case ENXIO:
				/*
				 * The device on which we're capturing
				 * went away.
				 *
				 * XXX - we should really return
				 * PCAP_ERROR_IFACE_NOT_UP, but
				 * pcap_dispatch() etc. aren't
				 * defined to return that.
				 */
				snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
				    "The interface went down");
				return (PCAP_ERROR);

			}
			snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "read: %s",
			    pcap_strerror(errno));
			return (PCAP_ERROR);
		}
		bp = p->buffer;
	} else
		bp = p->bp;

	/*
	 * Loop through each packet.
	 */
#define bhp ((struct bpf_hdr *)bp)
	ep = bp + cc;
	while (bp < ep) {
		int caplen, hdrlen;

		/*
		 * Has "pcap_breakloop()" been called?
		 * If so, return immediately - if we haven't read any
		 * packets, clear the flag and return PCAP_ERROR_BREAK
		 * to indicate that we were told to break out of the loop,
		 * otherwise leave the flag set, so that the *next* call
		 * will break out of the loop without having read any
		 * packets, and return the number of packets we've
		 * processed so far.
		 */
		if (p->break_loop) {
			p->bp = bp;
			p->cc = ep - bp;
			/*
			 * ep is set based on the return value of read(),
			 * but read() from a BPF device doesn't necessarily
			 * return a value that's a multiple of the alignment
			 * value for BPF_WORDALIGN().  However, whenever we
			 * increment bp, we round up the increment value by
			 * a value rounded up by BPF_WORDALIGN(), so we
			 * could increment bp past ep after processing the
			 * last packet in the buffer.
			 *
			 * We treat ep < bp as an indication that this
			 * happened, and just set p->cc to 0.
			 */
			if (p->cc < 0)
				p->cc = 0;
			if (n == 0) {
				p->break_loop = 0;
				return (PCAP_ERROR_BREAK);
			} else
				return (n);
		}

		caplen = bhp->bh_caplen;
		hdrlen = bhp->bh_hdrlen;
		/*
		 * XXX A bpf_hdr matches a pcap_pkthdr.
		 */
		(*callback)(user, (struct pcap_pkthdr*)bp, bp + hdrlen);
		bp += BPF_WORDALIGN(caplen + hdrlen);
		if (++n >= cnt && cnt > 0) {
			p->bp = bp;
			p->cc = ep - bp;
			return (n);
		}
	}
#undef bhp
	p->cc = 0;
	return (n);
}

int
pcap_inject(pcap_t *p, const void *buf, size_t len)
{
	return (write(p->fd, buf, len));
}

int
pcap_sendpacket(pcap_t *p, const u_char *buf, int size)
{
	return (pcap_inject(p, buf, size) == -1 ? -1 : 0);
}

static __inline int
bpf_open(pcap_t *p)
{
	int fd;

	fd = open("/dev/bpf", O_RDWR);
	if (fd == -1 && errno == EACCES)
		fd = open("/dev/bpf", O_RDONLY);

	if (fd == -1) {
		if (errno == EACCES)
			fd = PCAP_ERROR_PERM_DENIED;
		else
			fd = PCAP_ERROR;

		snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
		    "(cannot open BPF device): %s",
		    pcap_strerror(errno));
	}

	return (fd);
}

static int
get_dlt_list(int fd, int v, struct bpf_dltlist *bdlp, char *ebuf)
{
	memset(bdlp, 0, sizeof(*bdlp));
	if (ioctl(fd, BIOCGDLTLIST, (caddr_t)bdlp) == 0) {
		bdlp->bfl_list = calloc(bdlp->bfl_len + 1, sizeof(u_int));
		if (bdlp->bfl_list == NULL) {
			(void)snprintf(ebuf, PCAP_ERRBUF_SIZE, "malloc: %s",
			    pcap_strerror(errno));
			return (PCAP_ERROR);
		}

		if (ioctl(fd, BIOCGDLTLIST, (caddr_t)bdlp) == -1) {
			(void)snprintf(ebuf, PCAP_ERRBUF_SIZE,
			    "BIOCGDLTLIST: %s", pcap_strerror(errno));
			free(bdlp->bfl_list);
			return (PCAP_ERROR);
		}
	} else {
		/*
		 * EINVAL just means "we don't support this ioctl on
		 * this device"; don't treat it as an error.
		 */
		if (errno != EINVAL) {
			(void)snprintf(ebuf, PCAP_ERRBUF_SIZE,
			    "BIOCGDLTLIST: %s", pcap_strerror(errno));
			return (PCAP_ERROR);
		}
	}
	return (0);
}

/*
 * Returns 1 if rfmon mode can be set on the pcap_t, 0 if it can't,
 * a PCAP_ERROR value on an error.
 */
int
pcap_can_set_rfmon(pcap_t *p)
{
#if defined(HAVE_BSD_IEEE80211)
	int ret;

	ret = monitor_mode(p, 0);
	if (ret == PCAP_ERROR_RFMON_NOTSUP)
		return (0);	/* not an error, just a "can't do" */
	if (ret == 0)
		return (1);	/* success */
	return (ret);
#else
	return (0);
#endif
}

static void
pcap_cleanup_bpf(pcap_t *p)
{
#ifdef HAVE_BSD_IEEE80211
	int sock;
	struct ifmediareq req;
	struct ifreq ifr;
#endif

	if (p->md.must_do_on_close != 0) {
		/*
		 * There's something we have to do when closing this
		 * pcap_t.
		 */
#ifdef HAVE_BSD_IEEE80211
		if (p->md.must_do_on_close & MUST_CLEAR_RFMON) {
			/*
			 * We put the interface into rfmon mode;
			 * take it out of rfmon mode.
			 *
			 * XXX - if somebody else wants it in rfmon
			 * mode, this code cannot know that, so it'll take
			 * it out of rfmon mode.
			 */
			sock = socket(AF_INET, SOCK_DGRAM, 0);
			if (sock == -1) {
				fprintf(stderr,
				    "Can't restore interface flags (socket() failed: %s).\n"
				    "Please adjust manually.\n",
				    strerror(errno));
			} else {
				memset(&req, 0, sizeof(req));
				(void)strlcpy(req.ifm_name, p->opt.source,
				    sizeof(req.ifm_name));
				if (ioctl(sock, SIOCGIFMEDIA, &req) == -1) {
					fprintf(stderr,
					    "Can't restore interface flags "
					    "(SIOCGIFMEDIA failed: %s).\n"
					    "Please adjust manually.\n",
					    strerror(errno));
				} else if (req.ifm_current & IFM_IEEE80211_MONITOR) {
					/*
					 * Rfmon mode is currently on;
					 * turn it off.
					 */
					memset(&ifr, 0, sizeof(ifr));
					(void)strlcpy(ifr.ifr_name,
					    p->opt.source,
					    sizeof(ifr.ifr_name));
					ifr.ifr_media =
					    req.ifm_current & ~IFM_IEEE80211_MONITOR;
					if (ioctl(sock, SIOCSIFMEDIA,
					    &ifr) == -1) {
						fprintf(stderr,
						    "Can't restore interface flags "
						    "(SIOCSIFMEDIA failed: %s).\n"
						    "Please adjust manually.\n",
						    strerror(errno));
					}
				}
				close(sock);
			}
		}
#endif /* HAVE_BSD_IEEE80211 */

		/*
		 * Take this pcap out of the list of pcaps for which we
		 * have to take the interface out of some mode.
		 */
		pcap_remove_from_pcaps_to_close(p);
		p->md.must_do_on_close = 0;
	}

	/*XXX*/
	if (p->fd >= 0) {
		close(p->fd);
		p->fd = -1;
	}
	if (p->sf.rfile != NULL) {
		(void)fclose(p->sf.rfile);
		free(p->sf.base);
	} else
		free(p->buffer);

	pcap_freecode(&p->fcode);
	if (p->dlt_list != NULL) {
		free(p->dlt_list);
		p->dlt_list = NULL;
		p->dlt_count = 0;
	}
}

void
pcap_close(pcap_t *p)
{
	pcap_cleanup_bpf(p);
	free(p->opt.source);
	free(p);
}


static int
check_setif_failure(pcap_t *p, int error)
{
	if (error == ENXIO) {
		/*
		 * No such device.
		 */
		snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "BIOCSETIF failed: %s",
		    pcap_strerror(errno));
		return (PCAP_ERROR_NO_SUCH_DEVICE);
	} else if (errno == ENETDOWN) {
		/*
		 * Return a "network down" indication, so that
		 * the application can report that rather than
		 * saying we had a mysterious failure and
		 * suggest that they report a problem to the
		 * libpcap developers.
		 */
		return (PCAP_ERROR_IFACE_NOT_UP);
	} else {
		/*
		 * Some other error; fill in the error string, and
		 * return PCAP_ERROR.
		 */
		snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "BIOCSETIF: %s: %s",
		    p->opt.source, pcap_strerror(errno));
		return (PCAP_ERROR);
	}
}

int
pcap_activate(pcap_t *p)
{
	int status = 0;
	int fd;
	struct ifreq ifr;
	struct bpf_version bv;
	struct bpf_dltlist bdl;
	int new_dlt;
	u_int v;

	fd = bpf_open(p);
	if (fd < 0) {
		status = fd;
		goto bad;
	}

	p->fd = fd;

	if (ioctl(fd, BIOCVERSION, (caddr_t)&bv) == -1) {
		snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "BIOCVERSION: %s",
		    pcap_strerror(errno));
		status = PCAP_ERROR;
		goto bad;
	}
	if (bv.bv_major != BPF_MAJOR_VERSION ||
	    bv.bv_minor < BPF_MINOR_VERSION) {
		snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
		    "kernel bpf filter out of date");
		status = PCAP_ERROR;
		goto bad;
	}

#if 0
	/* Just use the kernel default */
	v = 32768;	/* XXX this should be a user-accessible hook */
	/* Ignore the return value - this is because the call fails on
	 * BPF systems that don't have kernel malloc.  And if the call
	 * fails, it's no big deal, we just continue to use the standard
	 * buffer size.
	 */
	(void) ioctl(fd, BIOCSBLEN, (caddr_t)&v);
#endif

	/*
	 * Set the buffer size.
	 */
	if (p->opt.buffer_size != 0) {
		/*
		 * A buffer size was explicitly specified; use it.
		 */
		if (ioctl(fd, BIOCSBLEN,
		    (caddr_t)&p->opt.buffer_size) == -1) {
			snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
			    "BIOCSBLEN: %s: %s", p->opt.source,
			    pcap_strerror(errno));
			status = PCAP_ERROR;
			goto bad;
		}
	}

	/*
	 * Now bind to the device.
	 */
	(void)strlcpy(ifr.ifr_name, p->opt.source, sizeof(ifr.ifr_name));
	if (ioctl(fd, BIOCSETIF, (caddr_t)&ifr) == -1) {
		status = check_setif_failure(p, errno);
		goto bad;
	}
	/* Get the data link layer type. */
	if (ioctl(fd, BIOCGDLT, (caddr_t)&v) == -1) {
		snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "BIOCGDLT: %s",
		    pcap_strerror(errno));
		status = PCAP_ERROR;
		goto bad;
	}

	/*
	 * We know the default link type -- now determine all the DLTs
	 * this interface supports.  If this fails with EINVAL, it's
	 * not fatal; we just don't get to use the feature later.
	 */
	if (get_dlt_list(fd, v, &bdl, p->errbuf) == -1) {
		status = PCAP_ERROR;
		goto bad;
	}
	p->dlt_count = bdl.bfl_len;
	p->dlt_list = bdl.bfl_list;

	/*
	 * *BSD with the new 802.11 ioctls.
	 * Do we want monitor mode?
	 */
	if (p->opt.rfmon) {
		/*
		 * Try to put the interface into monitor mode.
		 */
		status = monitor_mode(p, 1);
		if (status != 0) {
			/*
			 * We failed.
			 */
			goto bad;
		}

		/*
		 * We're in monitor mode.
		 * Try to find the best 802.11 DLT_ value and, if we
		 * succeed, try to switch to that mode if we're not
		 * already in that mode.
		 */
		new_dlt = find_802_11(&bdl);
		if (new_dlt != -1) {
			/*
			 * We have at least one 802.11 DLT_ value.
			 * new_dlt is the best of the 802.11
			 * DLT_ values in the list.
			 *
			 * If the new mode we want isn't the default mode,
			 * attempt to select the new mode.
			 */
			if (new_dlt != v) {
				if (ioctl(p->fd, BIOCSDLT, &new_dlt) != -1) {
					/*
					 * We succeeded; make this the
					 * new DLT_ value.
					 */
					v = new_dlt;
				}
			}
		}
	}
	p->linktype = v;

	/* set timeout */
	if (p->md.timeout) {
		struct timeval to;
		to.tv_sec = p->md.timeout / 1000;
		to.tv_usec = (p->md.timeout * 1000) % 1000000;
		if (ioctl(p->fd, BIOCSRTIMEOUT, (caddr_t)&to) == -1) {
			snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
			    "BIOCSRTIMEOUT: %s", pcap_strerror(errno));
			status = PCAP_ERROR;
			goto bad;
		}
	}

	if (p->opt.immediate) {
		v = 1;
		if (ioctl(p->fd, BIOCIMMEDIATE, &v) == -1) {
			snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
			    "BIOCIMMEDIATE: %s", pcap_strerror(errno));
			status = PCAP_ERROR;
			goto bad;
		}
	}

	if (p->opt.promisc) {
		/* set promiscuous mode, just warn if it fails */
		if (ioctl(p->fd, BIOCPROMISC, NULL) == -1) {
			snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "BIOCPROMISC: %s",
			    pcap_strerror(errno));
			status = PCAP_WARNING_PROMISC_NOTSUP;
		}
	}

	if (ioctl(fd, BIOCGBLEN, (caddr_t)&v) == -1) {
		snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "BIOCGBLEN: %s",
		    pcap_strerror(errno));
		status = PCAP_ERROR;
		goto bad;
	}
	p->bufsize = v;
	p->buffer = malloc(p->bufsize);
	if (p->buffer == NULL) {
		snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "malloc: %s",
		    pcap_strerror(errno));
		status = PCAP_ERROR;
		goto bad;
	}

	if (status < 0)
		goto bad;

	p->activated = 1;

	return (status);
 bad:
	pcap_cleanup_bpf(p);

	if (p->errbuf[0] == '\0') {
		/*
		 * No error message supplied by the activate routine;
		 * for the benefit of programs that don't specially
		 * handle errors other than PCAP_ERROR, return the
		 * error message corresponding to the status.
		 */
		snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "%s",
		    pcap_statustostr(status));
	}

	return (status);
}

static int
monitor_mode(pcap_t *p, int set)
{
	int sock;
	struct ifmediareq req;
	uint64_t *media_list;
	int i;
	int can_do;
	struct ifreq ifr;

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == -1) {
		snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "can't open socket: %s",
		    pcap_strerror(errno));
		return (PCAP_ERROR);
	}

	memset(&req, 0, sizeof req);
	(void)strlcpy(req.ifm_name, p->opt.source, sizeof req.ifm_name);

	/*
	 * Find out how many media types we have.
	 */
	if (ioctl(sock, SIOCGIFMEDIA, &req) == -1) {
		/*
		 * Can't get the media types.
		 */
		switch (errno) {

		case ENXIO:
			/*
			 * There's no such device.
			 */
			close(sock);
			return (PCAP_ERROR_NO_SUCH_DEVICE);

		case EINVAL:
		case ENOTTY:
			/*
			 * Interface doesn't support SIOC{G,S}IFMEDIA.
			 */
			close(sock);
			return (PCAP_ERROR_RFMON_NOTSUP);

		default:
			snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
			    "SIOCGIFMEDIA 1: %s", pcap_strerror(errno));
			close(sock);
			return (PCAP_ERROR);
		}
	}
	if (req.ifm_count == 0) {
		/*
		 * No media types.
		 */
		close(sock);
		return (PCAP_ERROR_RFMON_NOTSUP);
	}

	/*
	 * Allocate a buffer to hold all the media types, and
	 * get the media types.
	 */
	media_list = calloc(req.ifm_count, sizeof(*media_list));
	if (media_list == NULL) {
		snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "malloc: %s",
		    pcap_strerror(errno));
		close(sock);
		return (PCAP_ERROR);
	}
	req.ifm_ulist = media_list;
	if (ioctl(sock, SIOCGIFMEDIA, &req) == -1) {
		snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "SIOCGIFMEDIA: %s",
		    pcap_strerror(errno));
		free(media_list);
		close(sock);
		return (PCAP_ERROR);
	}

	/*
	 * Look for an 802.11 "automatic" media type.
	 * We assume that all 802.11 adapters have that media type,
	 * and that it will carry the monitor mode supported flag.
	 */
	can_do = 0;
	for (i = 0; i < req.ifm_count; i++) {
		if (IFM_TYPE(media_list[i]) == IFM_IEEE80211
		    && IFM_SUBTYPE(media_list[i]) == IFM_AUTO) {
			/* OK, does it do monitor mode? */
			if (media_list[i] & IFM_IEEE80211_MONITOR) {
				can_do = 1;
				break;
			}
		}
	}
	free(media_list);
	if (!can_do) {
		/*
		 * This adapter doesn't support monitor mode.
		 */
		close(sock);
		return (PCAP_ERROR_RFMON_NOTSUP);
	}

	if (set) {
		/*
		 * Don't just check whether we can enable monitor mode,
		 * do so, if it's not already enabled.
		 */
		if ((req.ifm_current & IFM_IEEE80211_MONITOR) == 0) {
			/*
			 * Monitor mode isn't currently on, so turn it on,
			 * and remember that we should turn it off when the
			 * pcap_t is closed.
			 */

			/*
			 * If we haven't already done so, arrange to have
			 * "pcap_close_all()" called when we exit.
			 */
			if (!pcap_do_addexit(p)) {
				/*
				 * "atexit()" failed; don't put the interface
				 * in monitor mode, just give up.
				 */
				snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
				     "atexit failed");
				close(sock);
				return (PCAP_ERROR);
			}
			memset(&ifr, 0, sizeof(ifr));
			(void)strlcpy(ifr.ifr_name, p->opt.source,
			    sizeof(ifr.ifr_name));
			ifr.ifr_media = req.ifm_current | IFM_IEEE80211_MONITOR;
			if (ioctl(sock, SIOCSIFMEDIA, &ifr) == -1) {
				snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
				     "SIOCSIFMEDIA: %s", pcap_strerror(errno));
				close(sock);
				return (PCAP_ERROR);
			}

			p->md.must_do_on_close |= MUST_CLEAR_RFMON;

			/*
			 * Add this to the list of pcaps to close when we exit.
			 */
			pcap_add_to_pcaps_to_close(p);
		}
	}
	return (0);
}

/*
 * Check whether we have any 802.11 link-layer types; return the best
 * of the 802.11 link-layer types if we find one, and return -1
 * otherwise.
 *
 * DLT_IEEE802_11_RADIO, with the radiotap header, is considered the
 * best 802.11 link-layer type; any of the other 802.11-plus-radio
 * headers are second-best; 802.11 with no radio information is
 * the least good.
 */
static int
find_802_11(struct bpf_dltlist *bdlp)
{
	int new_dlt;
	int i;

	/*
	 * Scan the list of DLT_ values, looking for 802.11 values,
	 * and, if we find any, choose the best of them.
	 */
	new_dlt = -1;
	for (i = 0; i < bdlp->bfl_len; i++) {
		switch (bdlp->bfl_list[i]) {

		case DLT_IEEE802_11:
			/*
			 * 802.11, but no radio.
			 *
			 * Offer this, and select it as the new mode
			 * unless we've already found an 802.11
			 * header with radio information.
			 */
			if (new_dlt == -1)
				new_dlt = bdlp->bfl_list[i];
			break;

		case DLT_IEEE802_11_RADIO:
			/*
			 * 802.11 with radiotap.
			 *
			 * Offer this, and select it as the new mode.
			 */
			new_dlt = bdlp->bfl_list[i];
			break;

		default:
			/*
			 * Not 802.11.
			 */
			break;
		}
	}

	return (new_dlt);
}

pcap_t *
pcap_create(const char *device, char *errbuf)
{
	pcap_t *p;

	p = calloc(1, sizeof(*p));
	if (p == NULL) {
		snprintf(errbuf, PCAP_ERRBUF_SIZE, "malloc: %s",
		    pcap_strerror(errno));
		return (NULL);
	}
	p->fd = -1;	/* not opened yet */

	p->opt.source = strdup(device);
	if (p->opt.source == NULL) {
		snprintf(errbuf, PCAP_ERRBUF_SIZE, "malloc: %s",
		    pcap_strerror(errno));
		free(p);
		return (NULL);
	}

	/* put in some defaults*/
	pcap_set_timeout(p, 0);
	pcap_set_snaplen(p, 65535);	/* max packet size */
	p->opt.promisc = 0;
	p->opt.buffer_size = 0;
	p->opt.immediate = 0;
	return (p);
}

pcap_t *
pcap_open_live(const char *source, int snaplen, int promisc, int to_ms,
    char *errbuf)
{
	pcap_t *p;
	int status;

	p = pcap_create(source, errbuf);
	if (p == NULL)
		return (NULL);
	status = pcap_set_snaplen(p, snaplen);
	if (status < 0)
		goto fail;
	status = pcap_set_promisc(p, promisc);
	if (status < 0)
		goto fail;
	status = pcap_set_timeout(p, to_ms);
	if (status < 0)
		goto fail;
	/*
	 * Mark this as opened with pcap_open_live(), so that, for
	 * example, we show the full list of DLT_ values, rather
	 * than just the ones that are compatible with capturing
	 * when not in monitor mode.  That allows existing applications
	 * to work the way they used to work, but allows new applications
	 * that know about the new open API to, for example, find out the
	 * DLT_ values that they can select without changing whether
	 * the adapter is in monitor mode or not.
	 */
	p->oldstyle = 1;
	status = pcap_activate(p);
	if (status < 0)
		goto fail;
	return (p);
fail:
	if (status == PCAP_ERROR)
		snprintf(errbuf, PCAP_ERRBUF_SIZE, "%s: %s", source,
		    p->errbuf);
	else if (status == PCAP_ERROR_NO_SUCH_DEVICE ||
	    status == PCAP_ERROR_PERM_DENIED ||
	    status == PCAP_ERROR_PROMISC_PERM_DENIED)
		snprintf(errbuf, PCAP_ERRBUF_SIZE, "%s: %s (%s)", source,
		    pcap_statustostr(status), p->errbuf);
	else
		snprintf(errbuf, PCAP_ERRBUF_SIZE, "%s: %s", source,
		    pcap_statustostr(status));
	pcap_close(p);
	return (NULL);
}

int
pcap_setfilter(pcap_t *p, struct bpf_program *fp)
{
	/*
	 * It looks that BPF code generated by gen_protochain() is not
	 * compatible with some of kernel BPF code (for example BSD/OS 3.1).
	 * Take a safer side for now.
	 */
	if (no_optimize || (p->sf.rfile != NULL)){
		if (p->fcode.bf_insns != NULL)
			pcap_freecode(&p->fcode);
		p->fcode.bf_len = fp->bf_len;
		p->fcode.bf_insns = reallocarray(NULL,
		    fp->bf_len, sizeof(*fp->bf_insns));
		if (p->fcode.bf_insns == NULL) {
			snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "malloc: %s",
			    pcap_strerror(errno));
			return (-1);
		}
		memcpy(p->fcode.bf_insns, fp->bf_insns,
		    fp->bf_len * sizeof(*fp->bf_insns));
	} else if (ioctl(p->fd, BIOCSETF, (caddr_t)fp) == -1) {
		snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "BIOCSETF: %s",
		    pcap_strerror(errno));
		return (-1);
	}
	return (0);
}

int
pcap_setdirection(pcap_t *p, pcap_direction_t d)
{
	u_int dirfilt;

	switch (d) {
	case PCAP_D_INOUT:
		dirfilt = 0;
		break;
	case PCAP_D_IN:
		dirfilt = BPF_DIRECTION_OUT;
		break;
	case PCAP_D_OUT:
		dirfilt = BPF_DIRECTION_IN;
		break;
	default:
		snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "Invalid direction");
		return (-1);
	}
	if (ioctl(p->fd, BIOCSDIRFILT, &dirfilt) == -1) {
		snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "BIOCSDIRFILT: %s",
		    pcap_strerror(errno));
		return (-1);
	}
	return (0);
}

int
pcap_set_datalink(pcap_t *p, int dlt)
{
	int i;

	if (p->dlt_count == 0) {
		/*
		 * We couldn't fetch the list of DLTs, or we don't
		 * have a "set datalink" operation, which means
		 * this platform doesn't support changing the
		 * DLT for an interface.  Check whether the new
		 * DLT is the one this interface supports.
		 */
		if (p->linktype != dlt)
			goto unsupported;

		/*
		 * It is, so there's nothing we need to do here.
		 */
		return (0);
	}
	for (i = 0; i < p->dlt_count; i++)
		if (p->dlt_list[i] == dlt)
			break;
	if (i >= p->dlt_count)
		goto unsupported;
	if (ioctl(p->fd, BIOCSDLT, &dlt) == -1) {
		(void) snprintf(p->errbuf, sizeof(p->errbuf),
		    "Cannot set DLT %d: %s", dlt, strerror(errno));
		return (-1);
	}
	p->linktype = dlt;
	return (0);

unsupported:
	(void) snprintf(p->errbuf, sizeof(p->errbuf),
	    "DLT %d is not one of the DLTs supported by this device",
	    dlt);
	return (-1);
}

