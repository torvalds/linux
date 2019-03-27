/*-
 * Copyright (c) 2014 Gleb Kurtsou <gleb@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/user.h>
#include <sys/socket.h>
#include <string.h>

#include "libprocstat.h"

#define	SPECNAMELEN_COMPAT12	63

struct freebsd11_ptsstat {
	uint32_t	dev;
	char		devname[SPECNAMELEN_COMPAT12 + 1];
};

struct freebsd11_vnstat {
	uint64_t	vn_fileid;
	uint64_t	vn_size;
	char		*vn_mntdir;
	uint32_t	vn_dev;
	uint32_t	vn_fsid;
	int		vn_type;
	uint16_t	vn_mode;
	char		vn_devname[SPECNAMELEN_COMPAT12 + 1];
};
struct freebsd11_semstat {
	uint32_t	value;
	uint16_t	mode;
};
struct freebsd11_shmstat {
	uint64_t	size;
	uint16_t	mode;
};

struct freebsd11_sockstat {
	uint64_t	inp_ppcb;
	uint64_t	so_addr;
	uint64_t	so_pcb;
	uint64_t	unp_conn;
	int		dom_family;
	int		proto;
	int		so_rcv_sb_state;
	int		so_snd_sb_state;
	struct sockaddr_storage	sa_local;	/* Socket address. */
	struct sockaddr_storage	sa_peer;	/* Peer address. */
	int		type;
	char		dname[32];
};

struct freebsd12_vnstat {
	uint64_t	vn_fileid;
	uint64_t	vn_size;
	uint64_t	vn_dev;
	uint64_t	vn_fsid;
	char		*vn_mntdir;
	int		vn_type;
	uint16_t	vn_mode;
	char		vn_devname[SPECNAMELEN_COMPAT12 + 1];
};
struct freebsd12_ptsstat {
	uint64_t	dev;
	char		devname[SPECNAMELEN_COMPAT12 + 1];
};

int	freebsd11_procstat_get_pts_info(struct procstat *procstat,
    struct filestat *fst, struct freebsd11_ptsstat *pts, char *errbuf);
int	freebsd12_procstat_get_pts_info(struct procstat *procstat,
    struct filestat *fst, struct freebsd12_ptsstat *pts_compat, char *errbuf);
int	freebsd11_procstat_get_sem_info(struct procstat *procstat,
    struct filestat *fst, struct freebsd11_semstat *sem, char *errbuf);
int	freebsd11_procstat_get_shm_info(struct procstat *procstat,
    struct filestat *fst, struct freebsd11_shmstat *shm, char *errbuf);
int	freebsd11_procstat_get_socket_info(struct procstat *procstat,
    struct filestat *fst, struct freebsd11_sockstat *sock, char *errbuf);
int	freebsd11_procstat_get_vnode_info(struct procstat *procstat,
    struct filestat *fst, struct freebsd11_vnstat *vn, char *errbuf);
int	freebsd12_procstat_get_vnode_info(struct procstat *procstat,
    struct filestat *fst, struct freebsd12_vnstat *vn_compat, char *errbuf);

static const char trunc_name[] = "<TRUNCATED>";

int
freebsd11_procstat_get_pts_info(struct procstat *procstat,
    struct filestat *fst, struct freebsd11_ptsstat *pts_compat, char *errbuf)
{
	struct ptsstat pts;
	int r;

	r = procstat_get_pts_info(procstat, fst, &pts, errbuf);
	if (r != 0)
		return (r);
	pts_compat->dev = pts.dev;
	if (strlen(pts.devname) >= sizeof(pts_compat->devname))
		strcpy(pts_compat->devname, trunc_name);
	else
		memcpy(pts_compat->devname, pts.devname,
		    sizeof(pts_compat->devname));
	return (0);
}

int
freebsd12_procstat_get_pts_info(struct procstat *procstat,
    struct filestat *fst, struct freebsd12_ptsstat *pts_compat, char *errbuf)
{
	struct ptsstat pts;
	int r;

	r = procstat_get_pts_info(procstat, fst, &pts, errbuf);
	if (r != 0)
		return (r);
	pts_compat->dev = pts.dev;
	if (strlen(pts.devname) >= sizeof(pts_compat->devname))
		strcpy(pts_compat->devname, trunc_name);
	else
		memcpy(pts_compat->devname, pts.devname,
		    sizeof(pts_compat->devname));
	return (0);
}

int
freebsd11_procstat_get_sem_info(struct procstat *procstat,
    struct filestat *fst, struct freebsd11_semstat *sem_compat, char *errbuf)
{
	struct semstat sem;
	int r;

	r = procstat_get_sem_info(procstat, fst, &sem, errbuf);
	if (r != 0)
		return (r);
	sem_compat->value = sem.value;
	sem_compat->mode = sem.mode;
	return (0);
}

int
freebsd11_procstat_get_shm_info(struct procstat *procstat,
    struct filestat *fst, struct freebsd11_shmstat *shm_compat, char *errbuf)
{
	struct shmstat shm;
	int r;

	r = procstat_get_shm_info(procstat, fst, &shm, errbuf);
	if (r != 0)
		return (r);
	shm_compat->size = shm.size;
	shm_compat->mode = shm.mode;
	return (0);
}

int
freebsd11_procstat_get_socket_info(struct procstat *procstat, struct filestat *fst,
    struct freebsd11_sockstat *sock_compat, char *errbuf)
{
	struct sockstat sock;
	int r;

	r = procstat_get_socket_info(procstat, fst, &sock, errbuf);
	if (r != 0)
		return (r);
	sock_compat->inp_ppcb = sock.inp_ppcb;
	sock_compat->so_addr = sock.so_addr;
	sock_compat->so_pcb = sock.so_pcb;
	sock_compat->unp_conn = sock.unp_conn;
	sock_compat->dom_family = sock.dom_family;
	sock_compat->proto = sock.proto;
	sock_compat->so_rcv_sb_state = sock.so_rcv_sb_state;
	sock_compat->so_snd_sb_state = sock.so_snd_sb_state;
	sock_compat->sa_local = sock.sa_local;
	sock_compat->sa_peer = sock.sa_peer;
	sock_compat->type = sock.type;
	memcpy(sock_compat->dname, sock.dname, sizeof(sock.dname));
	return (0);
}

int
freebsd11_procstat_get_vnode_info(struct procstat *procstat,
    struct filestat *fst, struct freebsd11_vnstat *vn_compat, char *errbuf)
{
	struct vnstat vn;
	int r;

	r = procstat_get_vnode_info(procstat, fst, &vn, errbuf);
	if (r != 0)
		return (r);
	vn_compat->vn_fileid = vn.vn_fileid;
	vn_compat->vn_size = vn.vn_size;
	vn_compat->vn_mntdir = vn.vn_mntdir;
	vn_compat->vn_dev = vn.vn_dev;
	vn_compat->vn_fsid = vn.vn_fsid;
	vn_compat->vn_type = vn.vn_type;
	vn_compat->vn_mode = vn.vn_mode;
	if (strlen(vn.vn_devname) >= sizeof(vn_compat->vn_devname))
		strcpy(vn_compat->vn_devname, trunc_name);
	else
		memcpy(vn_compat->vn_devname, vn.vn_devname,
		    sizeof(vn_compat->vn_devname));
	return (0);
}

int
freebsd12_procstat_get_vnode_info(struct procstat *procstat,
    struct filestat *fst, struct freebsd12_vnstat *vn_compat, char *errbuf)
{
	struct vnstat vn;
	int r;

	r = procstat_get_vnode_info(procstat, fst, &vn, errbuf);
	if (r != 0)
		return (r);
	vn_compat->vn_fileid = vn.vn_fileid;
	vn_compat->vn_size = vn.vn_size;
	vn_compat->vn_mntdir = vn.vn_mntdir;
	vn_compat->vn_dev = vn.vn_dev;
	vn_compat->vn_fsid = vn.vn_fsid;
	vn_compat->vn_type = vn.vn_type;
	vn_compat->vn_mode = vn.vn_mode;
	if (strlen(vn.vn_devname) >= sizeof(vn_compat->vn_devname))
		strcpy(vn_compat->vn_devname, trunc_name);
	else
		memcpy(vn_compat->vn_devname, vn.vn_devname,
		    sizeof(vn_compat->vn_devname));
	return (0);
}

__sym_compat(procstat_get_pts_info, freebsd11_procstat_get_pts_info, FBSD_1.2);
__sym_compat(procstat_get_socket_info, freebsd11_procstat_get_socket_info,
    FBSD_1.2);
__sym_compat(procstat_get_vnode_info, freebsd11_procstat_get_vnode_info,
    FBSD_1.2);
__sym_compat(procstat_get_sem_info, freebsd11_procstat_get_sem_info, FBSD_1.3);
__sym_compat(procstat_get_shm_info, freebsd11_procstat_get_shm_info, FBSD_1.3);
__sym_compat(procstat_get_pts_info, freebsd12_procstat_get_pts_info, FBSD_1.5);
__sym_compat(procstat_get_vnode_info, freebsd12_procstat_get_vnode_info,
    FBSD_1.5);
