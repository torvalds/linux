/*
 * Copyright (c) 1990 Jan-Simon Pendry
 * Copyright (c) 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 *	from: @(#)srvr_nfs.c	8.1 (Berkeley) 6/6/93
 *	$Id: srvr_nfs.c,v 1.10 2014/10/26 03:28:41 guenther Exp $
 */

/*
 * NFS server modeling
 */

#include "am.h"
#include <netdb.h>
#include <rpc/pmap_prot.h>
#include "mount.h"

extern qelem nfs_srvr_list;
qelem nfs_srvr_list = { &nfs_srvr_list, &nfs_srvr_list };

typedef struct nfs_private {
	u_short np_mountd;	/* Mount daemon port number */
	char np_mountd_inval;	/* Port *may* be invalid */
	int np_ping;		/* Number of failed ping attempts */
	time_t np_ttl;		/* Time when server is thought dead */
	int np_xid;		/* RPC transaction id for pings */
	int np_error;		/* Error during portmap request */
} nfs_private;

static int np_xid;	/* For NFS pings */
#define	NPXID_ALLOC()	(++np_xid)
/*#define	NPXID_ALLOC()	((++np_xid&0x0fffffff) == 0 ? npxid_gc() : np_xid)*/

/*
 * Number of pings allowed to fail before host is declared down
 * - three-fifths of the allowed mount time...
#define	MAX_ALLOWED_PINGS	((((ALLOWED_MOUNT_TIME + 5 * AM_PINGER - 1) * 3) / 5) / AM_PINGER)
 */
#define	MAX_ALLOWED_PINGS	(3 + /* for luck ... */ 1)

/*
 * How often to ping when starting a new server
 */
#define	FAST_NFS_PING		3

#if (FAST_NFS_PING * MAX_ALLOWED_PINGS) >= ALLOWED_MOUNT_TIME
 #error: sanity check failed
/*
 you cannot do things this way...
 sufficient fast pings must be given the chance to fail
 within the allowed mount time
 */
#endif /* (FAST_NFS_PING * MAX_ALLOWED_PINGS) >= ALLOWED_MOUNT_TIME */

static int ping_len;
static char ping_buf[sizeof(struct rpc_msg) + 32];

/*
 * Flush any cached data
 */
void
flush_srvr_nfs_cache(void)
{
	fserver *fs = 0;

	ITER(fs, fserver, &nfs_srvr_list) {
		nfs_private *np = (nfs_private *) fs->fs_private;
		if (np) {
			np->np_mountd_inval = TRUE;
			np->np_error = -1;
		}
	}
}

/*
 * Startup the NFS ping
 */
static void
start_ping(void)
{
	XDR ping_xdr;
	struct rpc_msg ping_msg;

	rpc_msg_init(&ping_msg, NFS_PROGRAM, NFS_VERSION, NFSPROC_NULL);

	/*
	 * Create an XDR endpoint
	 */
	xdrmem_create(&ping_xdr, ping_buf, sizeof(ping_buf), XDR_ENCODE);

	/*
	 * Create the NFS ping message
	 */
	if (!xdr_callmsg(&ping_xdr, &ping_msg)) {
		plog(XLOG_ERROR, "Couldn't create ping RPC message");
		going_down(3);
	}

	/*
	 * Find out how long it is
	 */
	ping_len = xdr_getpos(&ping_xdr);

	/*
	 * Destroy the XDR endpoint - we don't need it anymore
	 */
	xdr_destroy(&ping_xdr);
}


/*
 * Called when a portmap reply arrives
 */
static void
got_portmap(void *pkt, int len, struct sockaddr_in *sa,
    struct sockaddr_in *ia, void *idv, int done)
{
	fserver *fs2 = (fserver *) idv;
	fserver *fs = 0;

	/*
	 * Find which fileserver we are talking about
	 */
	ITER(fs, fserver, &nfs_srvr_list)
		if (fs == fs2)
			break;

	if (fs == fs2) {
		u_long port = 0;	/* XXX - should be short but protocol is naff */
		int error = done ? pickup_rpc_reply(pkt, len, &port, xdr_u_long) : -1;
		nfs_private *np = (nfs_private *) fs->fs_private;
		if (!error && port) {
#ifdef DEBUG
			dlog("got port (%d) for mountd on %s", port, fs->fs_host);
#endif /* DEBUG */
			/*
			 * Grab the port number.  Portmap sends back
			 * an unsigned long in native ordering, so it
			 * needs converting to a unsigned short in
			 * network ordering.
			 */
			np->np_mountd = htons((u_short) port);
			np->np_mountd_inval = FALSE;
			np->np_error = 0;
		} else {
#ifdef DEBUG
			dlog("Error fetching port for mountd on %s", fs->fs_host);
#endif /* DEBUG */
			/*
			 * Almost certainly no mountd running on remote host
			 */
			np->np_error = error ? error : ETIMEDOUT;
		}
		if (fs->fs_flags & FSF_WANT)
			wakeup_srvr(fs);
	} else if (done) {
#ifdef DEBUG
		dlog("Got portmap for old port request");
#endif /* DEBUG */
	} else {
#ifdef DEBUG
		dlog("portmap request timed out");
#endif /* DEBUG */
	}
}

/*
 * Obtain portmap information
 */
static int
call_portmap(fserver *fs, AUTH *auth, unsigned long prog,
    unsigned long vers, unsigned long prot)
{
	struct rpc_msg pmap_msg;
	int len;
	char iobuf[UDPMSGSIZE];
	int error;
	struct pmap pmap;

	rpc_msg_init(&pmap_msg, PMAPPROG, PMAPVERS, (unsigned long) 0);
	pmap.pm_prog = prog;
	pmap.pm_vers = vers;
	pmap.pm_prot = prot;
	pmap.pm_port = 0;
	len = make_rpc_packet(iobuf, sizeof(iobuf), PMAPPROC_GETPORT,
			&pmap_msg, &pmap, xdr_pmap, auth);
	if (len > 0) {
		struct sockaddr_in sin;
		bzero(&sin, sizeof(sin));
		sin = *fs->fs_ip;
		sin.sin_port = htons(PMAPPORT);
		error = fwd_packet(RPC_XID_PORTMAP, iobuf, len,
				&sin, &sin, fs, got_portmap);
	} else {
		error = -len;
	}
	return error;
}

static void nfs_keepalive(void *);

static void
recompute_portmap(fserver *fs)
{
	int error;

	if (nfs_auth)
		error = 0;
	else
		error = make_nfs_auth();

	if (error) {
		nfs_private *np = (nfs_private *) fs->fs_private;
		np->np_error = error;
	} else {
		call_portmap(fs, nfs_auth, MOUNTPROG,
			MOUNTVERS, (unsigned long) IPPROTO_UDP);
	}
}

/*
 * This is called when we get a reply to an RPC ping.
 * The value of id was taken from the nfs_private
 * structure when the ping was transmitted.
 */
static void
nfs_pinged(void *pkt, int len, struct sockaddr_in *sp,
    struct sockaddr_in *tsp, void *idv, int done)
{
	/* XXX EVIL! XXX */
	int xid = (int) ((long)idv);
	fserver *fs;
#ifdef DEBUG
	int found_map = 0;
#endif /* DEBUG */

	if (!done)
		return;

	/*
	 * For each node...
	 */
	ITER(fs, fserver, &nfs_srvr_list) {
		nfs_private *np = (nfs_private *) fs->fs_private;
		if (np->np_xid == xid) {
			/*
			 * Reset the ping counter.
			 * Update the keepalive timer.
			 * Log what happened.
			 */
			if (fs->fs_flags & FSF_DOWN) {
				fs->fs_flags &= ~FSF_DOWN;
				if (fs->fs_flags & FSF_VALID) {
					srvrlog(fs, "is up");
				} else {
					if (np->np_ping > 1)
						srvrlog(fs, "ok");
#ifdef DEBUG
					else
						srvrlog(fs, "starts up");
#endif
					fs->fs_flags |= FSF_VALID;
				}

#ifdef notdef
				/* why ??? */
				if (fs->fs_flags & FSF_WANT)
					wakeup_srvr(fs);
#endif /* notdef */
				map_flush_srvr(fs);
			} else {
				if (fs->fs_flags & FSF_VALID) {
#ifdef DEBUG
					dlog("file server %s type nfs is still up", fs->fs_host);
#endif /* DEBUG */
				} else {
					if (np->np_ping > 1)
						srvrlog(fs, "ok");
					fs->fs_flags |= FSF_VALID;
				}
			}

			/*
			 * Adjust ping interval
			 */
			untimeout(fs->fs_cid);
			fs->fs_cid = timeout(fs->fs_pinger, nfs_keepalive, fs);

			/*
			 * Update ttl for this server
			 */
			np->np_ttl = clocktime() +
				(MAX_ALLOWED_PINGS - 1) * FAST_NFS_PING + fs->fs_pinger - 1;

			/*
			 * New RPC xid...
			 */
			np->np_xid = NPXID_ALLOC();

			/*
			 * Failed pings is zero...
			 */
			np->np_ping = 0;

			/*
			 * Recompute portmap information if not known
			 */
			if (np->np_mountd_inval)
				recompute_portmap(fs);

#ifdef DEBUG
			found_map++;
#endif /* DEBUG */
			break;
		}
	}

#ifdef DEBUG
	if (found_map == 0)
		dlog("Spurious ping packet");
#endif /* DEBUG */
}

/*
 * Called when no ping-reply received
 */
static void
nfs_timed_out(void *arg)
{
	fserver *fs = arg;

	nfs_private *np = (nfs_private *) fs->fs_private;

	/*
	 * Another ping has failed
	 */
	np->np_ping++;

	/*
	 * Not known to be up any longer
	 */
	if (FSRV_ISUP(fs)) {
		fs->fs_flags &= ~FSF_VALID;
		if (np->np_ping > 1)
			srvrlog(fs, "not responding");
	}

	/*
	 * If ttl has expired then guess that it is dead
	 */
	if (np->np_ttl < clocktime()) {
		int oflags = fs->fs_flags;
		if ((fs->fs_flags & FSF_DOWN) == 0) {
			/*
			 * Server was up, but is now down.
			 */
			srvrlog(fs, "is down");
			fs->fs_flags |= FSF_DOWN|FSF_VALID;
			/*
			 * Since the server is down, the portmap
			 * information may now be wrong, so it
			 * must be flushed from the local cache
			 */
			flush_nfs_fhandle_cache(fs);
			np->np_error = -1;
#ifdef notdef
			/*
			 * Pretend just one ping has failed now
			 */
			np->np_ping = 1;
#endif
		} else {
			/*
			 * Known to be down
			 */
#ifdef DEBUG
			if ((fs->fs_flags & FSF_VALID) == 0)
				srvrlog(fs, "starts down");
#endif
			fs->fs_flags |= FSF_VALID;
		}
		if (oflags != fs->fs_flags && (fs->fs_flags & FSF_WANT))
			wakeup_srvr(fs);
	} else {
#ifdef DEBUG
		if (np->np_ping > 1)
			dlog("%d pings to %s failed - at most %d allowed", np->np_ping, fs->fs_host, MAX_ALLOWED_PINGS);
#endif /* DEBUG */
	}

	/*
	 * Run keepalive again
	 */
	nfs_keepalive(fs);
}

/*
 * Keep track of whether a server is alive
 */
static void
nfs_keepalive(void *arg)
{
	fserver *fs = arg;

	int error;
	nfs_private *np = (nfs_private *) fs->fs_private;
	int fstimeo = -1;

	/*
	 * Send an NFS ping to this node
	 */

	if (ping_len == 0)
		start_ping();

	/*
	 * Queue the packet...
	 */
	/*
	 * XXX EVIL!  We cast xid to a pointer, then back to an int when
	 * XXX we get the reply.
	 */
	error = fwd_packet(MK_RPC_XID(RPC_XID_NFSPING, np->np_xid), ping_buf,
		ping_len, fs->fs_ip, NULL, (void *)((long)np->np_xid),
		nfs_pinged);

	/*
	 * See if a hard error occured
	 */
	switch (error) {
	case ENETDOWN:
	case ENETUNREACH:
	case EHOSTDOWN:
	case EHOSTUNREACH:
		np->np_ping = MAX_ALLOWED_PINGS;	/* immediately down */
		np->np_ttl = (time_t) 0;
		/*
		 * This causes an immediate call to nfs_timed_out
		 * whenever the server was thought to be up.
		 * See +++ below.
		 */
		fstimeo = 0;
		break;

	case 0:
#ifdef DEBUG
		dlog("Sent NFS ping to %s", fs->fs_host);
#endif /* DEBUG */
		break;
	}

#ifdef DEBUG
	/*dlog("keepalive, ping = %d", np->np_ping);*/
#endif /* DEBUG */

	/*
	 * Back off the ping interval if we are not getting replies and
	 * the remote system is know to be down.
	 */
	switch (fs->fs_flags & (FSF_DOWN|FSF_VALID)) {
	case FSF_VALID:			/* Up */
		if (fstimeo < 0)	/* +++ see above */
			fstimeo = FAST_NFS_PING;
		break;

	case FSF_VALID|FSF_DOWN:	/* Down */
		fstimeo = fs->fs_pinger;
		break;

	default:			/* Unknown */
		fstimeo = FAST_NFS_PING;
		break;
	}

#ifdef DEBUG
	dlog("NFS timeout in %d seconds", fstimeo);
#endif /* DEBUG */

	fs->fs_cid = timeout(fstimeo, nfs_timed_out, fs);
}

int
nfs_srvr_port(fserver *fs, u_short *port, void *wchan)
{
	int error = -1;
	if ((fs->fs_flags & FSF_VALID) == FSF_VALID) {
		if ((fs->fs_flags & FSF_DOWN) == 0) {
			nfs_private *np = (nfs_private *) fs->fs_private;
			if (np->np_error == 0) {
				*port = np->np_mountd;
				error = 0;
			} else {
				error = np->np_error;
			}
			/*
			 * Now go get the port mapping again in case it changed.
			 * Note that it is used even if (np_mountd_inval)
			 * is True.  The flag is used simply as an
			 * indication that the mountd may be invalid, not
			 * that it is known to be invalid.
			 */
			if (np->np_mountd_inval)
				recompute_portmap(fs);
			else
				np->np_mountd_inval = TRUE;
		} else {
			error = EWOULDBLOCK;
		}
	}
	if (error < 0 && wchan && !(fs->fs_flags & FSF_WANT)) {
		/*
		 * If a wait channel is supplied, and no
		 * error has yet occured, then arrange
		 * that a wakeup is done on the wait channel,
		 * whenever a wakeup is done on this fs node.
		 * Wakeup's are done on the fs node whenever
		 * it changes state - thus causing control to
		 * come back here and new, better things to happen.
		 */
		fs->fs_flags |= FSF_WANT;
		sched_task(wakeup_task, wchan, fs);
	}
	return error;
}

static void
start_nfs_pings(fserver *fs, int pingval)
{
	if (!(fs->fs_flags & FSF_PINGING)) {
		fs->fs_flags |= FSF_PINGING;
		if (fs->fs_cid)
			untimeout(fs->fs_cid);
		if (pingval < 0) {
			srvrlog(fs, "wired up");
			fs->fs_flags |= FSF_VALID;
			fs->fs_flags &= ~FSF_DOWN;
		} else {
			nfs_keepalive(fs);
		}
	} else {
#ifdef DEBUG
		dlog("Already running pings to %s", fs->fs_host);
#endif /* DEBUG */
	}
}

/*
 * Find an nfs server for a host.
 */
fserver *
find_nfs_srvr(mntfs *mf)
{
	fserver *fs;
	struct hostent *hp = 0;
	char *host = mf->mf_fo->opt_rhost;
	struct sockaddr_in *ip;
	nfs_private *np;
	int pingval;

	/*
	 * Get ping interval from mount options.
	 * Current only used to decide whether pings
	 * are required or not.  < 0 = no pings.
	 */
	{ struct mntent mnt;
	  mnt.mnt_opts = mf->mf_mopts;
	  pingval = hasmntval(&mnt, "ping");
	  /*
	   * Over TCP mount, don't bother to do pings.
	   * This is experimental - maybe you want to
	   * do pings anyway...
	   */
	  if (pingval == 0 && hasmntopt(&mnt, "tcp"))
		pingval = -1;
	}


	/*
	 * lookup host address and canonical name
	 */
	hp = gethostbyname(host);

	/*
	 * New code from Bob Harris <harris@basil-rathbone.mit.edu>
	 * Use canonical name to keep track of file server
	 * information.  This way aliases do not generate
	 * multiple NFS pingers.  (Except when we're normalizing
	 * hosts.)
	 */
	if (hp && !normalize_hosts) host = hp->h_name;

	ITER(fs, fserver, &nfs_srvr_list) {
		if (STREQ(host, fs->fs_host)) {
			start_nfs_pings(fs, pingval);
			fs->fs_refc++;
			return fs;
		}
	}



	/*
	 * Get here if we can't find an entry
	 */
	if (hp) {
		switch (hp->h_addrtype) {
		case AF_INET:
			ip = ALLOC(sockaddr_in);
			bzero(ip, sizeof(*ip));
			ip->sin_family = AF_INET;
			bcopy(hp->h_addr, &ip->sin_addr, sizeof(ip->sin_addr));

			ip->sin_port = htons(NFS_PORT);
			break;

		default:
			ip = 0;
			break;
		}
	} else {
		plog(XLOG_USER, "Unknown host: %s", host);
		ip = 0;
	}

	/*
	 * Allocate a new server
	 */
	fs = ALLOC(fserver);
	fs->fs_refc = 1;
	fs->fs_host = strdup(hp ? hp->h_name : "unknown_hostname");
	if (normalize_hosts) host_normalize(&fs->fs_host);
	fs->fs_ip = ip;
	fs->fs_cid = 0;
	if (ip) {
		fs->fs_flags = FSF_DOWN;	/* Starts off down */
	} else {
		fs->fs_flags = FSF_ERROR|FSF_VALID;
		mf->mf_flags |= MFF_ERROR;
		mf->mf_error = ENOENT;
	}
	fs->fs_type = "nfs";
	fs->fs_pinger = AM_PINGER;
	np = ALLOC(nfs_private);
	bzero(np, sizeof(*np));
	np->np_mountd_inval = TRUE;
	np->np_xid = NPXID_ALLOC();
	np->np_error = -1;
	/*
	 * Initially the server will be deemed dead after
	 * MAX_ALLOWED_PINGS of the fast variety have failed.
	 */
	np->np_ttl = clocktime() + MAX_ALLOWED_PINGS * FAST_NFS_PING - 1;
	fs->fs_private = np;
	fs->fs_prfree = free;

	if (!(fs->fs_flags & FSF_ERROR)) {
		/*
		 * Start of keepalive timer
		 */
		start_nfs_pings(fs, pingval);
	}

	/*
	 * Add to list of servers
	 */
	ins_que(&fs->fs_q, &nfs_srvr_list);

	return fs;
}
