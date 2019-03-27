/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2011 Robert N. M. Watson
 * Copyright (c) 2015 Allan Jude <allanjude@freebsd.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/un.h>
#include <sys/user.h>

#include <netinet/in.h>

#include <arpa/inet.h>

#include <err.h>
#include <libprocstat.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "procstat.h"

static const char *
protocol_to_string(int domain, int type, int protocol)
{

	switch (domain) {
	case AF_INET:
	case AF_INET6:
		switch (protocol) {
		case IPPROTO_TCP:
			return ("TCP");
		case IPPROTO_UDP:
			return ("UDP");
		case IPPROTO_ICMP:
			return ("ICM");
		case IPPROTO_RAW:
			return ("RAW");
		case IPPROTO_SCTP:
			return ("SCT");
		case IPPROTO_DIVERT:
			return ("IPD");
		default:
			return ("IP?");
		}

	case AF_LOCAL:
		switch (type) {
		case SOCK_STREAM:
			return ("UDS");
		case SOCK_DGRAM:
			return ("UDD");
		default:
			return ("UD?");
		}
	default:
		return ("?");
	}
}

static void
addr_to_string(struct sockaddr_storage *ss, char *buffer, int buflen)
{
	char buffer2[INET6_ADDRSTRLEN];
	struct sockaddr_in6 *sin6;
	struct sockaddr_in *sin;
	struct sockaddr_un *sun;

	switch (ss->ss_family) {
	case AF_LOCAL:
		sun = (struct sockaddr_un *)ss;
		if (strlen(sun->sun_path) == 0)
			strlcpy(buffer, "-", buflen);
		else
			strlcpy(buffer, sun->sun_path, buflen);
		break;

	case AF_INET:
		sin = (struct sockaddr_in *)ss;
		snprintf(buffer, buflen, "%s:%d", inet_ntoa(sin->sin_addr),
		    ntohs(sin->sin_port));
		break;

	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)ss;
		if (inet_ntop(AF_INET6, &sin6->sin6_addr, buffer2,
		    sizeof(buffer2)) != NULL)
			snprintf(buffer, buflen, "%s.%d", buffer2,
			    ntohs(sin6->sin6_port));
		else
			strlcpy(buffer, "-", buflen);
		break;

	default:
		strlcpy(buffer, "", buflen);
		break;
	}
}

static struct cap_desc {
	uint64_t	 cd_right;
	const char	*cd_desc;
} cap_desc[] = {
	/* General file I/O. */
	{ CAP_READ,		"rd" },
	{ CAP_WRITE,		"wr" },
	{ CAP_SEEK,		"se" },
	{ CAP_MMAP,		"mm" },
	{ CAP_CREATE,		"cr" },
	{ CAP_FEXECVE,		"fe" },
	{ CAP_FSYNC,		"fy" },
	{ CAP_FTRUNCATE,	"ft" },

	/* VFS methods. */
	{ CAP_FCHDIR,		"cd" },
	{ CAP_FCHFLAGS,		"cf" },
	{ CAP_FCHMOD,		"cm" },
	{ CAP_FCHOWN,		"cn" },
	{ CAP_FCNTL,		"fc" },
	{ CAP_FLOCK,		"fl" },
	{ CAP_FPATHCONF,	"fp" },
	{ CAP_FSCK,		"fk" },
	{ CAP_FSTAT,		"fs" },
	{ CAP_FSTATFS,		"sf" },
	{ CAP_FUTIMES,		"fu" },
	{ CAP_LINKAT_SOURCE,	"ls" },
	{ CAP_LINKAT_TARGET,	"lt" },
	{ CAP_MKDIRAT,		"md" },
	{ CAP_MKFIFOAT,		"mf" },
	{ CAP_MKNODAT,		"mn" },
	{ CAP_RENAMEAT_SOURCE,	"rs" },
	{ CAP_RENAMEAT_TARGET,	"rt" },
	{ CAP_SYMLINKAT,	"sl" },
	{ CAP_UNLINKAT,		"un" },

	/* Lookups - used to constrain *at() calls. */
	{ CAP_LOOKUP,		"lo" },

	/* Extended attributes. */
	{ CAP_EXTATTR_GET,	"eg" },
	{ CAP_EXTATTR_SET,	"es" },
	{ CAP_EXTATTR_DELETE,	"ed" },
	{ CAP_EXTATTR_LIST,	"el" },

	/* Access Control Lists. */
	{ CAP_ACL_GET,		"ag" },
	{ CAP_ACL_SET,		"as" },
	{ CAP_ACL_DELETE,	"ad" },
	{ CAP_ACL_CHECK,	"ac" },

	/* Socket operations. */
	{ CAP_ACCEPT,		"at" },
	{ CAP_BIND,		"bd" },
	{ CAP_CONNECT,		"co" },
	{ CAP_GETPEERNAME,	"pn" },
	{ CAP_GETSOCKNAME,	"sn" },
	{ CAP_GETSOCKOPT,	"gs" },
	{ CAP_LISTEN,		"ln" },
	{ CAP_PEELOFF,		"pf" },
	{ CAP_SETSOCKOPT,	"ss" },
	{ CAP_SHUTDOWN,		"sh" },

	/* Mandatory Access Control. */
	{ CAP_MAC_GET,		"mg" },
	{ CAP_MAC_SET,		"ms" },

	/* Methods on semaphores. */
	{ CAP_SEM_GETVALUE,	"sg" },
	{ CAP_SEM_POST,		"sp" },
	{ CAP_SEM_WAIT,		"sw" },

	/* Event monitoring and posting. */
	{ CAP_EVENT,		"ev" },
	{ CAP_KQUEUE_EVENT,	"ke" },
	{ CAP_KQUEUE_CHANGE,	"kc" },

	/* Strange and powerful rights that should not be given lightly. */
	{ CAP_IOCTL,		"io" },
	{ CAP_TTYHOOK,		"ty" },

	/* Process management via process descriptors. */
	{ CAP_PDGETPID,		"pg" },
	{ CAP_PDWAIT,		"pw" },
	{ CAP_PDKILL,		"pk" },

	/*
	 * Rights that allow to use bindat(2) and connectat(2) syscalls on a
	 * directory descriptor.
	 */
	{ CAP_BINDAT,		"ba" },
	{ CAP_CONNECTAT,	"ca" },

	/* Aliases and defines that combine multiple rights. */
	{ CAP_PREAD,		"prd" },
	{ CAP_PWRITE,		"pwr" },

	{ CAP_MMAP_R,		"mmr" },
	{ CAP_MMAP_W,		"mmw" },
	{ CAP_MMAP_X,		"mmx" },
	{ CAP_MMAP_RW,		"mrw" },
	{ CAP_MMAP_RX,		"mrx" },
	{ CAP_MMAP_WX,		"mwx" },
	{ CAP_MMAP_RWX,		"mma" },

	{ CAP_RECV,		"re" },
	{ CAP_SEND,		"sd" },

	{ CAP_SOCK_CLIENT,	"scl" },
	{ CAP_SOCK_SERVER,	"ssr" },
};
static const u_int	cap_desc_count = nitems(cap_desc);

static u_int
width_capability(cap_rights_t *rightsp)
{
	u_int count, i, width;

	count = 0;
	width = 0;
	for (i = 0; i < cap_desc_count; i++) {
		if (cap_rights_is_set(rightsp, cap_desc[i].cd_right)) {
			width += strlen(cap_desc[i].cd_desc);
			if (count)
				width++;
			count++;
		}
	}
	return (width);
}

static void
print_capability(cap_rights_t *rightsp, u_int capwidth)
{
	u_int count, i, width;

	count = 0;
	width = 0;
	for (i = width_capability(rightsp); i < capwidth; i++) {
		if (i != 0)
			xo_emit(" ");
		else
			xo_emit("-");
	}
	xo_open_list("capabilities");
	for (i = 0; i < cap_desc_count; i++) {
		if (cap_rights_is_set(rightsp, cap_desc[i].cd_right)) {
			xo_emit("{D:/%s}{l:capabilities/%s}", count ? "," : "",
			    cap_desc[i].cd_desc);
			width += strlen(cap_desc[i].cd_desc);
			if (count)
				width++;
			count++;
		}
	}
	xo_close_list("capabilities");
}

void
procstat_files(struct procstat *procstat, struct kinfo_proc *kipp)
{
	struct sockstat sock;
	struct filestat_list *head;
	struct filestat *fst;
	const char *str;
	struct vnstat vn;
	u_int capwidth, width;
	int error;
	char src_addr[PATH_MAX];
	char dst_addr[PATH_MAX];

	/*
	 * To print the header in capability mode, we need to know the width
	 * of the widest capability string.  Even if we get no processes
	 * back, we will print the header, so we defer aborting due to a lack
	 * of processes until after the header logic.
	 */
	capwidth = 0;
	head = procstat_getfiles(procstat, kipp, 0);
	if (head != NULL &&
	    (procstat_opts & PS_OPT_CAPABILITIES) != 0) {
		STAILQ_FOREACH(fst, head, next) {
			width = width_capability(&fst->fs_cap_rights);
			if (width > capwidth)
				capwidth = width;
		}
		if (capwidth < strlen("CAPABILITIES"))
			capwidth = strlen("CAPABILITIES");
	}

	if ((procstat_opts & PS_OPT_NOHEADER) == 0) {
		if ((procstat_opts & PS_OPT_CAPABILITIES) != 0)
			xo_emit("{T:/%5s %-16s %5s %1s %-8s %-*s "
			    "%-3s %-12s}\n", "PID", "COMM", "FD", "T",
			    "FLAGS", capwidth, "CAPABILITIES", "PRO",
			    "NAME");
		else
			xo_emit("{T:/%5s %-16s %5s %1s %1s %-8s "
			    "%3s %7s %-3s %-12s}\n", "PID", "COMM", "FD", "T",
			    "V", "FLAGS", "REF", "OFFSET", "PRO", "NAME");
	}

	if (head == NULL)
		return;
	xo_emit("{ek:process_id/%5d/%d}", kipp->ki_pid);
	xo_emit("{e:command/%-16s/%s}", kipp->ki_comm);
	xo_open_list("files");
	STAILQ_FOREACH(fst, head, next) {
		xo_open_instance("files");
		xo_emit("{dk:process_id/%5d/%d} ", kipp->ki_pid);
		xo_emit("{d:command/%-16s/%s} ", kipp->ki_comm);
		if (fst->fs_uflags & PS_FST_UFLAG_CTTY)
			xo_emit("{P: }{:fd/%s} ", "ctty");
		else if (fst->fs_uflags & PS_FST_UFLAG_CDIR)
			xo_emit("{P:  }{:fd/%s} ", "cwd");
		else if (fst->fs_uflags & PS_FST_UFLAG_JAIL)
			xo_emit("{P: }{:fd/%s} ", "jail");
		else if (fst->fs_uflags & PS_FST_UFLAG_RDIR)
			xo_emit("{P: }{:fd/%s} ", "root");
		else if (fst->fs_uflags & PS_FST_UFLAG_TEXT)
			xo_emit("{P: }{:fd/%s} ", "text");
		else if (fst->fs_uflags & PS_FST_UFLAG_TRACE)
			xo_emit("{:fd/%s} ", "trace");
		else
			xo_emit("{:fd/%5d} ", fst->fs_fd);

		switch (fst->fs_type) {
		case PS_FST_TYPE_VNODE:
			str = "v";
			xo_emit("{eq:fd_type/vnode}");
			break;

		case PS_FST_TYPE_SOCKET:
			str = "s";
			xo_emit("{eq:fd_type/socket}");
			break;

		case PS_FST_TYPE_PIPE:
			str = "p";
			xo_emit("{eq:fd_type/pipe}");
			break;

		case PS_FST_TYPE_FIFO:
			str = "f";
			xo_emit("{eq:fd_type/fifo}");
			break;

		case PS_FST_TYPE_KQUEUE:
			str = "k";
			xo_emit("{eq:fd_type/kqueue}");
			break;

		case PS_FST_TYPE_CRYPTO:
			str = "c";
			xo_emit("{eq:fd_type/crypto}");
			break;

		case PS_FST_TYPE_MQUEUE:
			str = "m";
			xo_emit("{eq:fd_type/mqueue}");
			break;

		case PS_FST_TYPE_SHM:
			str = "h";
			xo_emit("{eq:fd_type/shm}");
			break;

		case PS_FST_TYPE_PTS:
			str = "t";
			xo_emit("{eq:fd_type/pts}");
			break;

		case PS_FST_TYPE_SEM:
			str = "e";
			xo_emit("{eq:fd_type/sem}");
			break;

		case PS_FST_TYPE_PROCDESC:
			str = "P";
			xo_emit("{eq:fd_type/procdesc}");
			break;

		case PS_FST_TYPE_DEV:
			str = "D";
			xo_emit("{eq:fd_type/dev}");
			break;

		case PS_FST_TYPE_NONE:
			str = "?";
			xo_emit("{eq:fd_type/none}");
			break;

		case PS_FST_TYPE_UNKNOWN:
		default:
			str = "?";
			xo_emit("{eq:fd_type/unknown}");
			break;
		}
		xo_emit("{d:fd_type/%1s/%s} ", str);
		if ((procstat_opts & PS_OPT_CAPABILITIES) == 0) {
			str = "-";
			if (fst->fs_type == PS_FST_TYPE_VNODE) {
				error = procstat_get_vnode_info(procstat, fst,
				    &vn, NULL);
				switch (vn.vn_type) {
				case PS_FST_VTYPE_VREG:
					str = "r";
					xo_emit("{eq:vode_type/regular}");
					break;

				case PS_FST_VTYPE_VDIR:
					str = "d";
					xo_emit("{eq:vode_type/directory}");
					break;

				case PS_FST_VTYPE_VBLK:
					str = "b";
					xo_emit("{eq:vode_type/block}");
					break;

				case PS_FST_VTYPE_VCHR:
					str = "c";
					xo_emit("{eq:vode_type/character}");
					break;

				case PS_FST_VTYPE_VLNK:
					str = "l";
					xo_emit("{eq:vode_type/link}");
					break;

				case PS_FST_VTYPE_VSOCK:
					str = "s";
					xo_emit("{eq:vode_type/socket}");
					break;

				case PS_FST_VTYPE_VFIFO:
					str = "f";
					xo_emit("{eq:vode_type/fifo}");
					break;

				case PS_FST_VTYPE_VBAD:
					str = "x";
					xo_emit("{eq:vode_type/revoked_device}");
					break;

				case PS_FST_VTYPE_VNON:
					str = "?";
					xo_emit("{eq:vode_type/non}");
					break;

				case PS_FST_VTYPE_UNKNOWN:
				default:
					str = "?";
					xo_emit("{eq:vode_type/unknown}");
					break;
				}
			}
			xo_emit("{d:vnode_type/%1s/%s} ", str);
		}
		
		xo_emit("{d:/%s}", fst->fs_fflags & PS_FST_FFLAG_READ ?
		    "r" : "-");
		xo_emit("{d:/%s}", fst->fs_fflags & PS_FST_FFLAG_WRITE ?
		    "w" : "-");
		xo_emit("{d:/%s}", fst->fs_fflags & PS_FST_FFLAG_APPEND ?
		    "a" : "-");
		xo_emit("{d:/%s}", fst->fs_fflags & PS_FST_FFLAG_ASYNC ?
		    "s" : "-");
		xo_emit("{d:/%s}", fst->fs_fflags & PS_FST_FFLAG_SYNC ?
		    "f" : "-");
		xo_emit("{d:/%s}", fst->fs_fflags & PS_FST_FFLAG_NONBLOCK ?
		    "n" : "-");
		xo_emit("{d:/%s}", fst->fs_fflags & PS_FST_FFLAG_DIRECT ?
		    "d" : "-");
		xo_emit("{d:/%s}", fst->fs_fflags & PS_FST_FFLAG_HASLOCK ?
		    "l" : "-");
		xo_emit(" ");
		xo_open_list("fd_flags");
		if (fst->fs_fflags & PS_FST_FFLAG_READ)
			xo_emit("{elq:fd_flags/read}");
		if (fst->fs_fflags & PS_FST_FFLAG_WRITE)
			xo_emit("{elq:fd_flags/write}");
		if (fst->fs_fflags & PS_FST_FFLAG_APPEND)
			xo_emit("{elq:fd_flags/append}");
		if (fst->fs_fflags & PS_FST_FFLAG_ASYNC)
			xo_emit("{elq:fd_flags/async}");
		if (fst->fs_fflags & PS_FST_FFLAG_SYNC)
			xo_emit("{elq:fd_flags/fsync}");
		if (fst->fs_fflags & PS_FST_FFLAG_NONBLOCK)
			xo_emit("{elq:fd_flags/nonblocking}");
		if (fst->fs_fflags & PS_FST_FFLAG_DIRECT)
			xo_emit("{elq:fd_flags/direct_io}");
		if (fst->fs_fflags & PS_FST_FFLAG_HASLOCK)
			xo_emit("{elq:fd_flags/lock_held}");
		xo_close_list("fd_flags");

		if ((procstat_opts & PS_OPT_CAPABILITIES) == 0) {
			if (fst->fs_ref_count > -1)
				xo_emit("{:ref_count/%3d/%d} ",
				    fst->fs_ref_count);
			else
				xo_emit("{q:ref_count/%3c/%c} ", '-');
			if (fst->fs_offset > -1)
				xo_emit("{:offset/%7jd/%jd} ",
				    (intmax_t)fst->fs_offset);
			else
				xo_emit("{q:offset/%7c/%c} ", '-');
		}
		if ((procstat_opts & PS_OPT_CAPABILITIES) != 0) {
			print_capability(&fst->fs_cap_rights, capwidth);
			xo_emit(" ");
		}
		switch (fst->fs_type) {
		case PS_FST_TYPE_SOCKET:
			error = procstat_get_socket_info(procstat, fst, &sock,
			    NULL);
			if (error != 0)
				break;
			xo_emit("{:protocol/%-3s/%s} ",
			    protocol_to_string(sock.dom_family,
			    sock.type, sock.proto));
			if (sock.proto == IPPROTO_TCP ||
			    sock.proto == IPPROTO_SCTP ||
			    sock.type == SOCK_STREAM) {
				xo_emit("{:sendq/%u} ", sock.sendq);
				xo_emit("{:recvq/%u} ", sock.recvq);
			}
			/*
			 * While generally we like to print two addresses,
			 * local and peer, for sockets, it turns out to be
			 * more useful to print the first non-nul address for
			 * local sockets, as typically they aren't bound and
			 *  connected, and the path strings can get long.
			 */
			if (sock.dom_family == AF_LOCAL) {
				struct sockaddr_un *sun =
				    (struct sockaddr_un *)&sock.sa_local;

				if (sun->sun_path[0] != 0)
					addr_to_string(&sock.sa_local,
					    src_addr, sizeof(src_addr));
				else
					addr_to_string(&sock.sa_peer,
					    src_addr, sizeof(src_addr));
				xo_emit("{:path/%s}", src_addr);
			} else {
				addr_to_string(&sock.sa_local, src_addr,
				    sizeof(src_addr));
				addr_to_string(&sock.sa_peer, dst_addr,
				    sizeof(dst_addr));
				xo_emit("{:path/%s %s}", src_addr, dst_addr);
			}
			break;

		default:
			xo_emit("{:protocol/%-3s/%s} ", "-");
			xo_emit("{:path/%-18s/%s}", fst->fs_path != NULL ?
			    fst->fs_path : "-");
		}

		xo_emit("\n");
		xo_close_instance("files");
	}
	xo_close_list("files");
	procstat_freefiles(procstat, head);
}
