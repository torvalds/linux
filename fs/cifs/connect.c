/*
 *   fs/cifs/connect.c
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2011
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <linux/fs.h>
#include <linux/net.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/ctype.h>
#include <linux/utsname.h>
#include <linux/mempool.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/pagevec.h>
#include <linux/freezer.h>
#include <linux/namei.h>
#include <asm/uaccess.h>
#include <asm/processor.h>
#include <linux/inet.h>
#include <linux/module.h>
#include <keys/user-type.h>
#include <net/ipv6.h>
#include <linux/parser.h>

#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"
#include "ntlmssp.h"
#include "nterr.h"
#include "rfc1002pdu.h"
#include "fscache.h"

#define CIFS_PORT 445
#define RFC1001_PORT 139

extern mempool_t *cifs_req_poolp;

/* FIXME: should these be tunable? */
#define TLINK_ERROR_EXPIRE	(1 * HZ)
#define TLINK_IDLE_EXPIRE	(600 * HZ)

enum {

	/* Mount options that take no arguments */
	Opt_user_xattr, Opt_nouser_xattr,
	Opt_forceuid, Opt_noforceuid,
	Opt_forcegid, Opt_noforcegid,
	Opt_noblocksend, Opt_noautotune,
	Opt_hard, Opt_soft, Opt_perm, Opt_noperm,
	Opt_mapposix, Opt_nomapposix,
	Opt_mapchars, Opt_nomapchars, Opt_sfu,
	Opt_nosfu, Opt_nodfs, Opt_posixpaths,
	Opt_noposixpaths, Opt_nounix,
	Opt_nocase,
	Opt_brl, Opt_nobrl,
	Opt_forcemandatorylock, Opt_setuids,
	Opt_nosetuids, Opt_dynperm, Opt_nodynperm,
	Opt_nohard, Opt_nosoft,
	Opt_nointr, Opt_intr,
	Opt_nostrictsync, Opt_strictsync,
	Opt_serverino, Opt_noserverino,
	Opt_rwpidforward, Opt_cifsacl, Opt_nocifsacl,
	Opt_acl, Opt_noacl, Opt_locallease,
	Opt_sign, Opt_seal, Opt_noac,
	Opt_fsc, Opt_mfsymlinks,
	Opt_multiuser, Opt_sloppy, Opt_nosharesock,
	Opt_persistent, Opt_nopersistent,
	Opt_resilient, Opt_noresilient,

	/* Mount options which take numeric value */
	Opt_backupuid, Opt_backupgid, Opt_uid,
	Opt_cruid, Opt_gid, Opt_file_mode,
	Opt_dirmode, Opt_port,
	Opt_rsize, Opt_wsize, Opt_actimeo,

	/* Mount options which take string value */
	Opt_user, Opt_pass, Opt_ip,
	Opt_domain, Opt_srcaddr, Opt_iocharset,
	Opt_netbiosname, Opt_servern,
	Opt_ver, Opt_vers, Opt_sec, Opt_cache,

	/* Mount options to be ignored */
	Opt_ignore,

	/* Options which could be blank */
	Opt_blank_pass,
	Opt_blank_user,
	Opt_blank_ip,

	Opt_err
};

static const match_table_t cifs_mount_option_tokens = {

	{ Opt_user_xattr, "user_xattr" },
	{ Opt_nouser_xattr, "nouser_xattr" },
	{ Opt_forceuid, "forceuid" },
	{ Opt_noforceuid, "noforceuid" },
	{ Opt_forcegid, "forcegid" },
	{ Opt_noforcegid, "noforcegid" },
	{ Opt_noblocksend, "noblocksend" },
	{ Opt_noautotune, "noautotune" },
	{ Opt_hard, "hard" },
	{ Opt_soft, "soft" },
	{ Opt_perm, "perm" },
	{ Opt_noperm, "noperm" },
	{ Opt_mapchars, "mapchars" }, /* SFU style */
	{ Opt_nomapchars, "nomapchars" },
	{ Opt_mapposix, "mapposix" }, /* SFM style */
	{ Opt_nomapposix, "nomapposix" },
	{ Opt_sfu, "sfu" },
	{ Opt_nosfu, "nosfu" },
	{ Opt_nodfs, "nodfs" },
	{ Opt_posixpaths, "posixpaths" },
	{ Opt_noposixpaths, "noposixpaths" },
	{ Opt_nounix, "nounix" },
	{ Opt_nounix, "nolinux" },
	{ Opt_nocase, "nocase" },
	{ Opt_nocase, "ignorecase" },
	{ Opt_brl, "brl" },
	{ Opt_nobrl, "nobrl" },
	{ Opt_nobrl, "nolock" },
	{ Opt_forcemandatorylock, "forcemandatorylock" },
	{ Opt_forcemandatorylock, "forcemand" },
	{ Opt_setuids, "setuids" },
	{ Opt_nosetuids, "nosetuids" },
	{ Opt_dynperm, "dynperm" },
	{ Opt_nodynperm, "nodynperm" },
	{ Opt_nohard, "nohard" },
	{ Opt_nosoft, "nosoft" },
	{ Opt_nointr, "nointr" },
	{ Opt_intr, "intr" },
	{ Opt_nostrictsync, "nostrictsync" },
	{ Opt_strictsync, "strictsync" },
	{ Opt_serverino, "serverino" },
	{ Opt_noserverino, "noserverino" },
	{ Opt_rwpidforward, "rwpidforward" },
	{ Opt_cifsacl, "cifsacl" },
	{ Opt_nocifsacl, "nocifsacl" },
	{ Opt_acl, "acl" },
	{ Opt_noacl, "noacl" },
	{ Opt_locallease, "locallease" },
	{ Opt_sign, "sign" },
	{ Opt_seal, "seal" },
	{ Opt_noac, "noac" },
	{ Opt_fsc, "fsc" },
	{ Opt_mfsymlinks, "mfsymlinks" },
	{ Opt_multiuser, "multiuser" },
	{ Opt_sloppy, "sloppy" },
	{ Opt_nosharesock, "nosharesock" },
	{ Opt_persistent, "persistenthandles"},
	{ Opt_nopersistent, "nopersistenthandles"},
	{ Opt_resilient, "resilienthandles"},
	{ Opt_noresilient, "noresilienthandles"},

	{ Opt_backupuid, "backupuid=%s" },
	{ Opt_backupgid, "backupgid=%s" },
	{ Opt_uid, "uid=%s" },
	{ Opt_cruid, "cruid=%s" },
	{ Opt_gid, "gid=%s" },
	{ Opt_file_mode, "file_mode=%s" },
	{ Opt_dirmode, "dirmode=%s" },
	{ Opt_dirmode, "dir_mode=%s" },
	{ Opt_port, "port=%s" },
	{ Opt_rsize, "rsize=%s" },
	{ Opt_wsize, "wsize=%s" },
	{ Opt_actimeo, "actimeo=%s" },

	{ Opt_blank_user, "user=" },
	{ Opt_blank_user, "username=" },
	{ Opt_user, "user=%s" },
	{ Opt_user, "username=%s" },
	{ Opt_blank_pass, "pass=" },
	{ Opt_blank_pass, "password=" },
	{ Opt_pass, "pass=%s" },
	{ Opt_pass, "password=%s" },
	{ Opt_blank_ip, "ip=" },
	{ Opt_blank_ip, "addr=" },
	{ Opt_ip, "ip=%s" },
	{ Opt_ip, "addr=%s" },
	{ Opt_ignore, "unc=%s" },
	{ Opt_ignore, "target=%s" },
	{ Opt_ignore, "path=%s" },
	{ Opt_domain, "dom=%s" },
	{ Opt_domain, "domain=%s" },
	{ Opt_domain, "workgroup=%s" },
	{ Opt_srcaddr, "srcaddr=%s" },
	{ Opt_ignore, "prefixpath=%s" },
	{ Opt_iocharset, "iocharset=%s" },
	{ Opt_netbiosname, "netbiosname=%s" },
	{ Opt_servern, "servern=%s" },
	{ Opt_ver, "ver=%s" },
	{ Opt_vers, "vers=%s" },
	{ Opt_sec, "sec=%s" },
	{ Opt_cache, "cache=%s" },

	{ Opt_ignore, "cred" },
	{ Opt_ignore, "credentials" },
	{ Opt_ignore, "cred=%s" },
	{ Opt_ignore, "credentials=%s" },
	{ Opt_ignore, "guest" },
	{ Opt_ignore, "rw" },
	{ Opt_ignore, "ro" },
	{ Opt_ignore, "suid" },
	{ Opt_ignore, "nosuid" },
	{ Opt_ignore, "exec" },
	{ Opt_ignore, "noexec" },
	{ Opt_ignore, "nodev" },
	{ Opt_ignore, "noauto" },
	{ Opt_ignore, "dev" },
	{ Opt_ignore, "mand" },
	{ Opt_ignore, "nomand" },
	{ Opt_ignore, "_netdev" },

	{ Opt_err, NULL }
};

enum {
	Opt_sec_krb5, Opt_sec_krb5i, Opt_sec_krb5p,
	Opt_sec_ntlmsspi, Opt_sec_ntlmssp,
	Opt_ntlm, Opt_sec_ntlmi, Opt_sec_ntlmv2,
	Opt_sec_ntlmv2i, Opt_sec_lanman,
	Opt_sec_none,

	Opt_sec_err
};

static const match_table_t cifs_secflavor_tokens = {
	{ Opt_sec_krb5, "krb5" },
	{ Opt_sec_krb5i, "krb5i" },
	{ Opt_sec_krb5p, "krb5p" },
	{ Opt_sec_ntlmsspi, "ntlmsspi" },
	{ Opt_sec_ntlmssp, "ntlmssp" },
	{ Opt_ntlm, "ntlm" },
	{ Opt_sec_ntlmi, "ntlmi" },
	{ Opt_sec_ntlmv2, "nontlm" },
	{ Opt_sec_ntlmv2, "ntlmv2" },
	{ Opt_sec_ntlmv2i, "ntlmv2i" },
	{ Opt_sec_lanman, "lanman" },
	{ Opt_sec_none, "none" },

	{ Opt_sec_err, NULL }
};

/* cache flavors */
enum {
	Opt_cache_loose,
	Opt_cache_strict,
	Opt_cache_none,
	Opt_cache_err
};

static const match_table_t cifs_cacheflavor_tokens = {
	{ Opt_cache_loose, "loose" },
	{ Opt_cache_strict, "strict" },
	{ Opt_cache_none, "none" },
	{ Opt_cache_err, NULL }
};

static const match_table_t cifs_smb_version_tokens = {
	{ Smb_1, SMB1_VERSION_STRING },
	{ Smb_20, SMB20_VERSION_STRING},
	{ Smb_21, SMB21_VERSION_STRING },
	{ Smb_30, SMB30_VERSION_STRING },
	{ Smb_302, SMB302_VERSION_STRING },
#ifdef CONFIG_CIFS_SMB311
	{ Smb_311, SMB311_VERSION_STRING },
	{ Smb_311, ALT_SMB311_VERSION_STRING },
#endif /* SMB311 */
	{ Smb_version_err, NULL }
};

static int ip_connect(struct TCP_Server_Info *server);
static int generic_ip_connect(struct TCP_Server_Info *server);
static void tlink_rb_insert(struct rb_root *root, struct tcon_link *new_tlink);
static void cifs_prune_tlinks(struct work_struct *work);
static int cifs_setup_volume_info(struct smb_vol *volume_info, char *mount_data,
					const char *devname);

/*
 * cifs tcp session reconnection
 *
 * mark tcp session as reconnecting so temporarily locked
 * mark all smb sessions as reconnecting for tcp session
 * reconnect tcp session
 * wake up waiters on reconnection? - (not needed currently)
 */
int
cifs_reconnect(struct TCP_Server_Info *server)
{
	int rc = 0;
	struct list_head *tmp, *tmp2;
	struct cifs_ses *ses;
	struct cifs_tcon *tcon;
	struct mid_q_entry *mid_entry;
	struct list_head retry_list;

	spin_lock(&GlobalMid_Lock);
	if (server->tcpStatus == CifsExiting) {
		/* the demux thread will exit normally
		next time through the loop */
		spin_unlock(&GlobalMid_Lock);
		return rc;
	} else
		server->tcpStatus = CifsNeedReconnect;
	spin_unlock(&GlobalMid_Lock);
	server->maxBuf = 0;
#ifdef CONFIG_CIFS_SMB2
	server->max_read = 0;
#endif

	cifs_dbg(FYI, "Reconnecting tcp session\n");

	/* before reconnecting the tcp session, mark the smb session (uid)
		and the tid bad so they are not used until reconnected */
	cifs_dbg(FYI, "%s: marking sessions and tcons for reconnect\n",
		 __func__);
	spin_lock(&cifs_tcp_ses_lock);
	list_for_each(tmp, &server->smb_ses_list) {
		ses = list_entry(tmp, struct cifs_ses, smb_ses_list);
		ses->need_reconnect = true;
		ses->ipc_tid = 0;
		list_for_each(tmp2, &ses->tcon_list) {
			tcon = list_entry(tmp2, struct cifs_tcon, tcon_list);
			tcon->need_reconnect = true;
		}
	}
	spin_unlock(&cifs_tcp_ses_lock);

	/* do not want to be sending data on a socket we are freeing */
	cifs_dbg(FYI, "%s: tearing down socket\n", __func__);
	mutex_lock(&server->srv_mutex);
	if (server->ssocket) {
		cifs_dbg(FYI, "State: 0x%x Flags: 0x%lx\n",
			 server->ssocket->state, server->ssocket->flags);
		kernel_sock_shutdown(server->ssocket, SHUT_WR);
		cifs_dbg(FYI, "Post shutdown state: 0x%x Flags: 0x%lx\n",
			 server->ssocket->state, server->ssocket->flags);
		sock_release(server->ssocket);
		server->ssocket = NULL;
	}
	server->sequence_number = 0;
	server->session_estab = false;
	kfree(server->session_key.response);
	server->session_key.response = NULL;
	server->session_key.len = 0;
	server->lstrp = jiffies;

	/* mark submitted MIDs for retry and issue callback */
	INIT_LIST_HEAD(&retry_list);
	cifs_dbg(FYI, "%s: moving mids to private list\n", __func__);
	spin_lock(&GlobalMid_Lock);
	list_for_each_safe(tmp, tmp2, &server->pending_mid_q) {
		mid_entry = list_entry(tmp, struct mid_q_entry, qhead);
		if (mid_entry->mid_state == MID_REQUEST_SUBMITTED)
			mid_entry->mid_state = MID_RETRY_NEEDED;
		list_move(&mid_entry->qhead, &retry_list);
	}
	spin_unlock(&GlobalMid_Lock);
	mutex_unlock(&server->srv_mutex);

	cifs_dbg(FYI, "%s: issuing mid callbacks\n", __func__);
	list_for_each_safe(tmp, tmp2, &retry_list) {
		mid_entry = list_entry(tmp, struct mid_q_entry, qhead);
		list_del_init(&mid_entry->qhead);
		mid_entry->callback(mid_entry);
	}

	do {
		try_to_freeze();

		/* we should try only the port we connected to before */
		mutex_lock(&server->srv_mutex);
		rc = generic_ip_connect(server);
		if (rc) {
			cifs_dbg(FYI, "reconnect error %d\n", rc);
			mutex_unlock(&server->srv_mutex);
			msleep(3000);
		} else {
			atomic_inc(&tcpSesReconnectCount);
			spin_lock(&GlobalMid_Lock);
			if (server->tcpStatus != CifsExiting)
				server->tcpStatus = CifsNeedNegotiate;
			spin_unlock(&GlobalMid_Lock);
			mutex_unlock(&server->srv_mutex);
		}
	} while (server->tcpStatus == CifsNeedReconnect);

	return rc;
}

static void
cifs_echo_request(struct work_struct *work)
{
	int rc;
	struct TCP_Server_Info *server = container_of(work,
					struct TCP_Server_Info, echo.work);

	/*
	 * We cannot send an echo if it is disabled or until the
	 * NEGOTIATE_PROTOCOL request is done, which is indicated by
	 * server->ops->need_neg() == true. Also, no need to ping if
	 * we got a response recently.
	 */

	if (server->tcpStatus == CifsNeedReconnect ||
	    server->tcpStatus == CifsExiting || server->tcpStatus == CifsNew ||
	    (server->ops->can_echo && !server->ops->can_echo(server)) ||
	    time_before(jiffies, server->lstrp + SMB_ECHO_INTERVAL - HZ))
		goto requeue_echo;

	rc = server->ops->echo ? server->ops->echo(server) : -ENOSYS;
	if (rc)
		cifs_dbg(FYI, "Unable to send echo request to server: %s\n",
			 server->hostname);

requeue_echo:
	queue_delayed_work(cifsiod_wq, &server->echo, SMB_ECHO_INTERVAL);
}

static bool
allocate_buffers(struct TCP_Server_Info *server)
{
	if (!server->bigbuf) {
		server->bigbuf = (char *)cifs_buf_get();
		if (!server->bigbuf) {
			cifs_dbg(VFS, "No memory for large SMB response\n");
			msleep(3000);
			/* retry will check if exiting */
			return false;
		}
	} else if (server->large_buf) {
		/* we are reusing a dirty large buf, clear its start */
		memset(server->bigbuf, 0, HEADER_SIZE(server));
	}

	if (!server->smallbuf) {
		server->smallbuf = (char *)cifs_small_buf_get();
		if (!server->smallbuf) {
			cifs_dbg(VFS, "No memory for SMB response\n");
			msleep(1000);
			/* retry will check if exiting */
			return false;
		}
		/* beginning of smb buffer is cleared in our buf_get */
	} else {
		/* if existing small buf clear beginning */
		memset(server->smallbuf, 0, HEADER_SIZE(server));
	}

	return true;
}

static bool
server_unresponsive(struct TCP_Server_Info *server)
{
	/*
	 * We need to wait 2 echo intervals to make sure we handle such
	 * situations right:
	 * 1s  client sends a normal SMB request
	 * 2s  client gets a response
	 * 30s echo workqueue job pops, and decides we got a response recently
	 *     and don't need to send another
	 * ...
	 * 65s kernel_recvmsg times out, and we see that we haven't gotten
	 *     a response in >60s.
	 */
	if (server->tcpStatus == CifsGood &&
	    time_after(jiffies, server->lstrp + 2 * SMB_ECHO_INTERVAL)) {
		cifs_dbg(VFS, "Server %s has not responded in %d seconds. Reconnecting...\n",
			 server->hostname, (2 * SMB_ECHO_INTERVAL) / HZ);
		cifs_reconnect(server);
		wake_up(&server->response_q);
		return true;
	}

	return false;
}

/*
 * kvec_array_init - clone a kvec array, and advance into it
 * @new:	pointer to memory for cloned array
 * @iov:	pointer to original array
 * @nr_segs:	number of members in original array
 * @bytes:	number of bytes to advance into the cloned array
 *
 * This function will copy the array provided in iov to a section of memory
 * and advance the specified number of bytes into the new array. It returns
 * the number of segments in the new array. "new" must be at least as big as
 * the original iov array.
 */
static unsigned int
kvec_array_init(struct kvec *new, struct kvec *iov, unsigned int nr_segs,
		size_t bytes)
{
	size_t base = 0;

	while (bytes || !iov->iov_len) {
		int copy = min(bytes, iov->iov_len);

		bytes -= copy;
		base += copy;
		if (iov->iov_len == base) {
			iov++;
			nr_segs--;
			base = 0;
		}
	}
	memcpy(new, iov, sizeof(*iov) * nr_segs);
	new->iov_base += base;
	new->iov_len -= base;
	return nr_segs;
}

static struct kvec *
get_server_iovec(struct TCP_Server_Info *server, unsigned int nr_segs)
{
	struct kvec *new_iov;

	if (server->iov && nr_segs <= server->nr_iov)
		return server->iov;

	/* not big enough -- allocate a new one and release the old */
	new_iov = kmalloc(sizeof(*new_iov) * nr_segs, GFP_NOFS);
	if (new_iov) {
		kfree(server->iov);
		server->iov = new_iov;
		server->nr_iov = nr_segs;
	}
	return new_iov;
}

int
cifs_readv_from_socket(struct TCP_Server_Info *server, struct kvec *iov_orig,
		       unsigned int nr_segs, unsigned int to_read)
{
	int length = 0;
	int total_read;
	unsigned int segs;
	struct msghdr smb_msg;
	struct kvec *iov;

	iov = get_server_iovec(server, nr_segs);
	if (!iov)
		return -ENOMEM;

	smb_msg.msg_control = NULL;
	smb_msg.msg_controllen = 0;

	for (total_read = 0; to_read; total_read += length, to_read -= length) {
		try_to_freeze();

		if (server_unresponsive(server)) {
			total_read = -ECONNABORTED;
			break;
		}

		segs = kvec_array_init(iov, iov_orig, nr_segs, total_read);

		length = kernel_recvmsg(server->ssocket, &smb_msg,
					iov, segs, to_read, 0);

		if (server->tcpStatus == CifsExiting) {
			total_read = -ESHUTDOWN;
			break;
		} else if (server->tcpStatus == CifsNeedReconnect) {
			cifs_reconnect(server);
			total_read = -ECONNABORTED;
			break;
		} else if (length == -ERESTARTSYS ||
			   length == -EAGAIN ||
			   length == -EINTR) {
			/*
			 * Minimum sleep to prevent looping, allowing socket
			 * to clear and app threads to set tcpStatus
			 * CifsNeedReconnect if server hung.
			 */
			usleep_range(1000, 2000);
			length = 0;
			continue;
		} else if (length <= 0) {
			cifs_dbg(FYI, "Received no data or error: expecting %d\n"
				 "got %d", to_read, length);
			cifs_reconnect(server);
			total_read = -ECONNABORTED;
			break;
		}
	}
	return total_read;
}

int
cifs_read_from_socket(struct TCP_Server_Info *server, char *buf,
		      unsigned int to_read)
{
	struct kvec iov;

	iov.iov_base = buf;
	iov.iov_len = to_read;

	return cifs_readv_from_socket(server, &iov, 1, to_read);
}

static bool
is_smb_response(struct TCP_Server_Info *server, unsigned char type)
{
	/*
	 * The first byte big endian of the length field,
	 * is actually not part of the length but the type
	 * with the most common, zero, as regular data.
	 */
	switch (type) {
	case RFC1002_SESSION_MESSAGE:
		/* Regular SMB response */
		return true;
	case RFC1002_SESSION_KEEP_ALIVE:
		cifs_dbg(FYI, "RFC 1002 session keep alive\n");
		break;
	case RFC1002_POSITIVE_SESSION_RESPONSE:
		cifs_dbg(FYI, "RFC 1002 positive session response\n");
		break;
	case RFC1002_NEGATIVE_SESSION_RESPONSE:
		/*
		 * We get this from Windows 98 instead of an error on
		 * SMB negprot response.
		 */
		cifs_dbg(FYI, "RFC 1002 negative session response\n");
		/* give server a second to clean up */
		msleep(1000);
		/*
		 * Always try 445 first on reconnect since we get NACK
		 * on some if we ever connected to port 139 (the NACK
		 * is since we do not begin with RFC1001 session
		 * initialize frame).
		 */
		cifs_set_port((struct sockaddr *)&server->dstaddr, CIFS_PORT);
		cifs_reconnect(server);
		wake_up(&server->response_q);
		break;
	default:
		cifs_dbg(VFS, "RFC 1002 unknown response type 0x%x\n", type);
		cifs_reconnect(server);
	}

	return false;
}

void
dequeue_mid(struct mid_q_entry *mid, bool malformed)
{
#ifdef CONFIG_CIFS_STATS2
	mid->when_received = jiffies;
#endif
	spin_lock(&GlobalMid_Lock);
	if (!malformed)
		mid->mid_state = MID_RESPONSE_RECEIVED;
	else
		mid->mid_state = MID_RESPONSE_MALFORMED;
	list_del_init(&mid->qhead);
	spin_unlock(&GlobalMid_Lock);
}

static void
handle_mid(struct mid_q_entry *mid, struct TCP_Server_Info *server,
	   char *buf, int malformed)
{
	if (server->ops->check_trans2 &&
	    server->ops->check_trans2(mid, server, buf, malformed))
		return;
	mid->resp_buf = buf;
	mid->large_buf = server->large_buf;
	/* Was previous buf put in mpx struct for multi-rsp? */
	if (!mid->multiRsp) {
		/* smb buffer will be freed by user thread */
		if (server->large_buf)
			server->bigbuf = NULL;
		else
			server->smallbuf = NULL;
	}
	dequeue_mid(mid, malformed);
}

static void clean_demultiplex_info(struct TCP_Server_Info *server)
{
	int length;

	/* take it off the list, if it's not already */
	spin_lock(&cifs_tcp_ses_lock);
	list_del_init(&server->tcp_ses_list);
	spin_unlock(&cifs_tcp_ses_lock);

	spin_lock(&GlobalMid_Lock);
	server->tcpStatus = CifsExiting;
	spin_unlock(&GlobalMid_Lock);
	wake_up_all(&server->response_q);

	/* check if we have blocked requests that need to free */
	spin_lock(&server->req_lock);
	if (server->credits <= 0)
		server->credits = 1;
	spin_unlock(&server->req_lock);
	/*
	 * Although there should not be any requests blocked on this queue it
	 * can not hurt to be paranoid and try to wake up requests that may
	 * haven been blocked when more than 50 at time were on the wire to the
	 * same server - they now will see the session is in exit state and get
	 * out of SendReceive.
	 */
	wake_up_all(&server->request_q);
	/* give those requests time to exit */
	msleep(125);

	if (server->ssocket) {
		sock_release(server->ssocket);
		server->ssocket = NULL;
	}

	if (!list_empty(&server->pending_mid_q)) {
		struct list_head dispose_list;
		struct mid_q_entry *mid_entry;
		struct list_head *tmp, *tmp2;

		INIT_LIST_HEAD(&dispose_list);
		spin_lock(&GlobalMid_Lock);
		list_for_each_safe(tmp, tmp2, &server->pending_mid_q) {
			mid_entry = list_entry(tmp, struct mid_q_entry, qhead);
			cifs_dbg(FYI, "Clearing mid 0x%llx\n", mid_entry->mid);
			mid_entry->mid_state = MID_SHUTDOWN;
			list_move(&mid_entry->qhead, &dispose_list);
		}
		spin_unlock(&GlobalMid_Lock);

		/* now walk dispose list and issue callbacks */
		list_for_each_safe(tmp, tmp2, &dispose_list) {
			mid_entry = list_entry(tmp, struct mid_q_entry, qhead);
			cifs_dbg(FYI, "Callback mid 0x%llx\n", mid_entry->mid);
			list_del_init(&mid_entry->qhead);
			mid_entry->callback(mid_entry);
		}
		/* 1/8th of sec is more than enough time for them to exit */
		msleep(125);
	}

	if (!list_empty(&server->pending_mid_q)) {
		/*
		 * mpx threads have not exited yet give them at least the smb
		 * send timeout time for long ops.
		 *
		 * Due to delays on oplock break requests, we need to wait at
		 * least 45 seconds before giving up on a request getting a
		 * response and going ahead and killing cifsd.
		 */
		cifs_dbg(FYI, "Wait for exit from demultiplex thread\n");
		msleep(46000);
		/*
		 * If threads still have not exited they are probably never
		 * coming home not much else we can do but free the memory.
		 */
	}

	kfree(server->hostname);
	kfree(server->iov);
	kfree(server);

	length = atomic_dec_return(&tcpSesAllocCount);
	if (length > 0)
		mempool_resize(cifs_req_poolp, length + cifs_min_rcv);
}

static int
standard_receive3(struct TCP_Server_Info *server, struct mid_q_entry *mid)
{
	int length;
	char *buf = server->smallbuf;
	unsigned int pdu_length = get_rfc1002_length(buf);

	/* make sure this will fit in a large buffer */
	if (pdu_length > CIFSMaxBufSize + MAX_HEADER_SIZE(server) - 4) {
		cifs_dbg(VFS, "SMB response too long (%u bytes)\n", pdu_length);
		cifs_reconnect(server);
		wake_up(&server->response_q);
		return -ECONNABORTED;
	}

	/* switch to large buffer if too big for a small one */
	if (pdu_length > MAX_CIFS_SMALL_BUFFER_SIZE - 4) {
		server->large_buf = true;
		memcpy(server->bigbuf, buf, server->total_read);
		buf = server->bigbuf;
	}

	/* now read the rest */
	length = cifs_read_from_socket(server, buf + HEADER_SIZE(server) - 1,
				pdu_length - HEADER_SIZE(server) + 1 + 4);
	if (length < 0)
		return length;
	server->total_read += length;

	dump_smb(buf, server->total_read);

	/*
	 * We know that we received enough to get to the MID as we
	 * checked the pdu_length earlier. Now check to see
	 * if the rest of the header is OK. We borrow the length
	 * var for the rest of the loop to avoid a new stack var.
	 *
	 * 48 bytes is enough to display the header and a little bit
	 * into the payload for debugging purposes.
	 */
	length = server->ops->check_message(buf, server->total_read);
	if (length != 0)
		cifs_dump_mem("Bad SMB: ", buf,
			min_t(unsigned int, server->total_read, 48));

	if (server->ops->is_status_pending &&
	    server->ops->is_status_pending(buf, server, length))
		return -1;

	if (!mid)
		return length;

	handle_mid(mid, server, buf, length);
	return 0;
}

static int
cifs_demultiplex_thread(void *p)
{
	int length;
	struct TCP_Server_Info *server = p;
	unsigned int pdu_length;
	char *buf = NULL;
	struct task_struct *task_to_wake = NULL;
	struct mid_q_entry *mid_entry;

	current->flags |= PF_MEMALLOC;
	cifs_dbg(FYI, "Demultiplex PID: %d\n", task_pid_nr(current));

	length = atomic_inc_return(&tcpSesAllocCount);
	if (length > 1)
		mempool_resize(cifs_req_poolp, length + cifs_min_rcv);

	set_freezable();
	while (server->tcpStatus != CifsExiting) {
		if (try_to_freeze())
			continue;

		if (!allocate_buffers(server))
			continue;

		server->large_buf = false;
		buf = server->smallbuf;
		pdu_length = 4; /* enough to get RFC1001 header */

		length = cifs_read_from_socket(server, buf, pdu_length);
		if (length < 0)
			continue;
		server->total_read = length;

		/*
		 * The right amount was read from socket - 4 bytes,
		 * so we can now interpret the length field.
		 */
		pdu_length = get_rfc1002_length(buf);

		cifs_dbg(FYI, "RFC1002 header 0x%x\n", pdu_length);
		if (!is_smb_response(server, buf[0]))
			continue;

		/* make sure we have enough to get to the MID */
		if (pdu_length < HEADER_SIZE(server) - 1 - 4) {
			cifs_dbg(VFS, "SMB response too short (%u bytes)\n",
				 pdu_length);
			cifs_reconnect(server);
			wake_up(&server->response_q);
			continue;
		}

		/* read down to the MID */
		length = cifs_read_from_socket(server, buf + 4,
					       HEADER_SIZE(server) - 1 - 4);
		if (length < 0)
			continue;
		server->total_read += length;

		mid_entry = server->ops->find_mid(server, buf);

		if (!mid_entry || !mid_entry->receive)
			length = standard_receive3(server, mid_entry);
		else
			length = mid_entry->receive(server, mid_entry);

		if (length < 0)
			continue;

		if (server->large_buf)
			buf = server->bigbuf;

		server->lstrp = jiffies;
		if (mid_entry != NULL) {
			if (!mid_entry->multiRsp || mid_entry->multiEnd)
				mid_entry->callback(mid_entry);
		} else if (!server->ops->is_oplock_break ||
			   !server->ops->is_oplock_break(buf, server)) {
			cifs_dbg(VFS, "No task to wake, unknown frame received! NumMids %d\n",
				 atomic_read(&midCount));
			cifs_dump_mem("Received Data is: ", buf,
				      HEADER_SIZE(server));
#ifdef CONFIG_CIFS_DEBUG2
			if (server->ops->dump_detail)
				server->ops->dump_detail(buf);
			cifs_dump_mids(server);
#endif /* CIFS_DEBUG2 */

		}
	} /* end while !EXITING */

	/* buffer usually freed in free_mid - need to free it here on exit */
	cifs_buf_release(server->bigbuf);
	if (server->smallbuf) /* no sense logging a debug message if NULL */
		cifs_small_buf_release(server->smallbuf);

	task_to_wake = xchg(&server->tsk, NULL);
	clean_demultiplex_info(server);

	/* if server->tsk was NULL then wait for a signal before exiting */
	if (!task_to_wake) {
		set_current_state(TASK_INTERRUPTIBLE);
		while (!signal_pending(current)) {
			schedule();
			set_current_state(TASK_INTERRUPTIBLE);
		}
		set_current_state(TASK_RUNNING);
	}

	module_put_and_exit(0);
}

/* extract the host portion of the UNC string */
static char *
extract_hostname(const char *unc)
{
	const char *src;
	char *dst, *delim;
	unsigned int len;

	/* skip double chars at beginning of string */
	/* BB: check validity of these bytes? */
	src = unc + 2;

	/* delimiter between hostname and sharename is always '\\' now */
	delim = strchr(src, '\\');
	if (!delim)
		return ERR_PTR(-EINVAL);

	len = delim - src;
	dst = kmalloc((len + 1), GFP_KERNEL);
	if (dst == NULL)
		return ERR_PTR(-ENOMEM);

	memcpy(dst, src, len);
	dst[len] = '\0';

	return dst;
}

static int get_option_ul(substring_t args[], unsigned long *option)
{
	int rc;
	char *string;

	string = match_strdup(args);
	if (string == NULL)
		return -ENOMEM;
	rc = kstrtoul(string, 0, option);
	kfree(string);

	return rc;
}

static int get_option_uid(substring_t args[], kuid_t *result)
{
	unsigned long value;
	kuid_t uid;
	int rc;

	rc = get_option_ul(args, &value);
	if (rc)
		return rc;

	uid = make_kuid(current_user_ns(), value);
	if (!uid_valid(uid))
		return -EINVAL;

	*result = uid;
	return 0;
}

static int get_option_gid(substring_t args[], kgid_t *result)
{
	unsigned long value;
	kgid_t gid;
	int rc;

	rc = get_option_ul(args, &value);
	if (rc)
		return rc;

	gid = make_kgid(current_user_ns(), value);
	if (!gid_valid(gid))
		return -EINVAL;

	*result = gid;
	return 0;
}

static int cifs_parse_security_flavors(char *value,
				       struct smb_vol *vol)
{

	substring_t args[MAX_OPT_ARGS];

	/*
	 * With mount options, the last one should win. Reset any existing
	 * settings back to default.
	 */
	vol->sectype = Unspecified;
	vol->sign = false;

	switch (match_token(value, cifs_secflavor_tokens, args)) {
	case Opt_sec_krb5p:
		cifs_dbg(VFS, "sec=krb5p is not supported!\n");
		return 1;
	case Opt_sec_krb5i:
		vol->sign = true;
		/* Fallthrough */
	case Opt_sec_krb5:
		vol->sectype = Kerberos;
		break;
	case Opt_sec_ntlmsspi:
		vol->sign = true;
		/* Fallthrough */
	case Opt_sec_ntlmssp:
		vol->sectype = RawNTLMSSP;
		break;
	case Opt_sec_ntlmi:
		vol->sign = true;
		/* Fallthrough */
	case Opt_ntlm:
		vol->sectype = NTLM;
		break;
	case Opt_sec_ntlmv2i:
		vol->sign = true;
		/* Fallthrough */
	case Opt_sec_ntlmv2:
		vol->sectype = NTLMv2;
		break;
#ifdef CONFIG_CIFS_WEAK_PW_HASH
	case Opt_sec_lanman:
		vol->sectype = LANMAN;
		break;
#endif
	case Opt_sec_none:
		vol->nullauth = 1;
		break;
	default:
		cifs_dbg(VFS, "bad security option: %s\n", value);
		return 1;
	}

	return 0;
}

static int
cifs_parse_cache_flavor(char *value, struct smb_vol *vol)
{
	substring_t args[MAX_OPT_ARGS];

	switch (match_token(value, cifs_cacheflavor_tokens, args)) {
	case Opt_cache_loose:
		vol->direct_io = false;
		vol->strict_io = false;
		break;
	case Opt_cache_strict:
		vol->direct_io = false;
		vol->strict_io = true;
		break;
	case Opt_cache_none:
		vol->direct_io = true;
		vol->strict_io = false;
		break;
	default:
		cifs_dbg(VFS, "bad cache= option: %s\n", value);
		return 1;
	}
	return 0;
}

static int
cifs_parse_smb_version(char *value, struct smb_vol *vol)
{
	substring_t args[MAX_OPT_ARGS];

	switch (match_token(value, cifs_smb_version_tokens, args)) {
	case Smb_1:
		vol->ops = &smb1_operations;
		vol->vals = &smb1_values;
		break;
#ifdef CONFIG_CIFS_SMB2
	case Smb_20:
		vol->ops = &smb20_operations;
		vol->vals = &smb20_values;
		break;
	case Smb_21:
		vol->ops = &smb21_operations;
		vol->vals = &smb21_values;
		break;
	case Smb_30:
		vol->ops = &smb30_operations;
		vol->vals = &smb30_values;
		break;
	case Smb_302:
		vol->ops = &smb30_operations; /* currently identical with 3.0 */
		vol->vals = &smb302_values;
		break;
#ifdef CONFIG_CIFS_SMB311
	case Smb_311:
		vol->ops = &smb311_operations;
		vol->vals = &smb311_values;
		break;
#endif /* SMB311 */
#endif
	default:
		cifs_dbg(VFS, "Unknown vers= option specified: %s\n", value);
		return 1;
	}
	return 0;
}

/*
 * Parse a devname into substrings and populate the vol->UNC and vol->prepath
 * fields with the result. Returns 0 on success and an error otherwise.
 */
static int
cifs_parse_devname(const char *devname, struct smb_vol *vol)
{
	char *pos;
	const char *delims = "/\\";
	size_t len;

	/* make sure we have a valid UNC double delimiter prefix */
	len = strspn(devname, delims);
	if (len != 2)
		return -EINVAL;

	/* find delimiter between host and sharename */
	pos = strpbrk(devname + 2, delims);
	if (!pos)
		return -EINVAL;

	/* skip past delimiter */
	++pos;

	/* now go until next delimiter or end of string */
	len = strcspn(pos, delims);

	/* move "pos" up to delimiter or NULL */
	pos += len;
	vol->UNC = kstrndup(devname, pos - devname, GFP_KERNEL);
	if (!vol->UNC)
		return -ENOMEM;

	convert_delimiter(vol->UNC, '\\');

	/* If pos is NULL, or is a bogus trailing delimiter then no prepath */
	if (!*pos++ || !*pos)
		return 0;

	vol->prepath = kstrdup(pos, GFP_KERNEL);
	if (!vol->prepath)
		return -ENOMEM;

	return 0;
}

static int
cifs_parse_mount_options(const char *mountdata, const char *devname,
			 struct smb_vol *vol)
{
	char *data, *end;
	char *mountdata_copy = NULL, *options;
	unsigned int  temp_len, i, j;
	char separator[2];
	short int override_uid = -1;
	short int override_gid = -1;
	bool uid_specified = false;
	bool gid_specified = false;
	bool sloppy = false;
	char *invalid = NULL;
	char *nodename = utsname()->nodename;
	char *string = NULL;
	char *tmp_end, *value;
	char delim;
	bool got_ip = false;
	unsigned short port = 0;
	struct sockaddr *dstaddr = (struct sockaddr *)&vol->dstaddr;

	separator[0] = ',';
	separator[1] = 0;
	delim = separator[0];

	/* ensure we always start with zeroed-out smb_vol */
	memset(vol, 0, sizeof(*vol));

	/*
	 * does not have to be perfect mapping since field is
	 * informational, only used for servers that do not support
	 * port 445 and it can be overridden at mount time
	 */
	memset(vol->source_rfc1001_name, 0x20, RFC1001_NAME_LEN);
	for (i = 0; i < strnlen(nodename, RFC1001_NAME_LEN); i++)
		vol->source_rfc1001_name[i] = toupper(nodename[i]);

	vol->source_rfc1001_name[RFC1001_NAME_LEN] = 0;
	/* null target name indicates to use *SMBSERVR default called name
	   if we end up sending RFC1001 session initialize */
	vol->target_rfc1001_name[0] = 0;
	vol->cred_uid = current_uid();
	vol->linux_uid = current_uid();
	vol->linux_gid = current_gid();

	/*
	 * default to SFM style remapping of seven reserved characters
	 * unless user overrides it or we negotiate CIFS POSIX where
	 * it is unnecessary.  Can not simultaneously use more than one mapping
	 * since then readdir could list files that open could not open
	 */
	vol->remap = true;

	/* default to only allowing write access to owner of the mount */
	vol->dir_mode = vol->file_mode = S_IRUGO | S_IXUGO | S_IWUSR;

	/* vol->retry default is 0 (i.e. "soft" limited retry not hard retry) */
	/* default is always to request posix paths. */
	vol->posix_paths = 1;
	/* default to using server inode numbers where available */
	vol->server_ino = 1;

	/* default is to use strict cifs caching semantics */
	vol->strict_io = true;

	vol->actimeo = CIFS_DEF_ACTIMEO;

	/* FIXME: add autonegotiation -- for now, SMB1 is default */
	vol->ops = &smb1_operations;
	vol->vals = &smb1_values;

	if (!mountdata)
		goto cifs_parse_mount_err;

	mountdata_copy = kstrndup(mountdata, PAGE_SIZE, GFP_KERNEL);
	if (!mountdata_copy)
		goto cifs_parse_mount_err;

	options = mountdata_copy;
	end = options + strlen(options);

	if (strncmp(options, "sep=", 4) == 0) {
		if (options[4] != 0) {
			separator[0] = options[4];
			options += 5;
		} else {
			cifs_dbg(FYI, "Null separator not allowed\n");
		}
	}
	vol->backupuid_specified = false; /* no backup intent for a user */
	vol->backupgid_specified = false; /* no backup intent for a group */

	switch (cifs_parse_devname(devname, vol)) {
	case 0:
		break;
	case -ENOMEM:
		cifs_dbg(VFS, "Unable to allocate memory for devname.\n");
		goto cifs_parse_mount_err;
	case -EINVAL:
		cifs_dbg(VFS, "Malformed UNC in devname.\n");
		goto cifs_parse_mount_err;
	default:
		cifs_dbg(VFS, "Unknown error parsing devname.\n");
		goto cifs_parse_mount_err;
	}

	while ((data = strsep(&options, separator)) != NULL) {
		substring_t args[MAX_OPT_ARGS];
		unsigned long option;
		int token;

		if (!*data)
			continue;

		token = match_token(data, cifs_mount_option_tokens, args);

		switch (token) {

		/* Ingnore the following */
		case Opt_ignore:
			break;

		/* Boolean values */
		case Opt_user_xattr:
			vol->no_xattr = 0;
			break;
		case Opt_nouser_xattr:
			vol->no_xattr = 1;
			break;
		case Opt_forceuid:
			override_uid = 1;
			break;
		case Opt_noforceuid:
			override_uid = 0;
			break;
		case Opt_forcegid:
			override_gid = 1;
			break;
		case Opt_noforcegid:
			override_gid = 0;
			break;
		case Opt_noblocksend:
			vol->noblocksnd = 1;
			break;
		case Opt_noautotune:
			vol->noautotune = 1;
			break;
		case Opt_hard:
			vol->retry = 1;
			break;
		case Opt_soft:
			vol->retry = 0;
			break;
		case Opt_perm:
			vol->noperm = 0;
			break;
		case Opt_noperm:
			vol->noperm = 1;
			break;
		case Opt_mapchars:
			vol->sfu_remap = true;
			vol->remap = false; /* disable SFM mapping */
			break;
		case Opt_nomapchars:
			vol->sfu_remap = false;
			break;
		case Opt_mapposix:
			vol->remap = true;
			vol->sfu_remap = false; /* disable SFU mapping */
			break;
		case Opt_nomapposix:
			vol->remap = false;
			break;
		case Opt_sfu:
			vol->sfu_emul = 1;
			break;
		case Opt_nosfu:
			vol->sfu_emul = 0;
			break;
		case Opt_nodfs:
			vol->nodfs = 1;
			break;
		case Opt_posixpaths:
			vol->posix_paths = 1;
			break;
		case Opt_noposixpaths:
			vol->posix_paths = 0;
			break;
		case Opt_nounix:
			vol->no_linux_ext = 1;
			break;
		case Opt_nocase:
			vol->nocase = 1;
			break;
		case Opt_brl:
			vol->nobrl =  0;
			break;
		case Opt_nobrl:
			vol->nobrl =  1;
			/*
			 * turn off mandatory locking in mode
			 * if remote locking is turned off since the
			 * local vfs will do advisory
			 */
			if (vol->file_mode ==
				(S_IALLUGO & ~(S_ISUID | S_IXGRP)))
				vol->file_mode = S_IALLUGO;
			break;
		case Opt_forcemandatorylock:
			vol->mand_lock = 1;
			break;
		case Opt_setuids:
			vol->setuids = 1;
			break;
		case Opt_nosetuids:
			vol->setuids = 0;
			break;
		case Opt_dynperm:
			vol->dynperm = true;
			break;
		case Opt_nodynperm:
			vol->dynperm = false;
			break;
		case Opt_nohard:
			vol->retry = 0;
			break;
		case Opt_nosoft:
			vol->retry = 1;
			break;
		case Opt_nointr:
			vol->intr = 0;
			break;
		case Opt_intr:
			vol->intr = 1;
			break;
		case Opt_nostrictsync:
			vol->nostrictsync = 1;
			break;
		case Opt_strictsync:
			vol->nostrictsync = 0;
			break;
		case Opt_serverino:
			vol->server_ino = 1;
			break;
		case Opt_noserverino:
			vol->server_ino = 0;
			break;
		case Opt_rwpidforward:
			vol->rwpidforward = 1;
			break;
		case Opt_cifsacl:
			vol->cifs_acl = 1;
			break;
		case Opt_nocifsacl:
			vol->cifs_acl = 0;
			break;
		case Opt_acl:
			vol->no_psx_acl = 0;
			break;
		case Opt_noacl:
			vol->no_psx_acl = 1;
			break;
		case Opt_locallease:
			vol->local_lease = 1;
			break;
		case Opt_sign:
			vol->sign = true;
			break;
		case Opt_seal:
			/* we do not do the following in secFlags because seal
			 * is a per tree connection (mount) not a per socket
			 * or per-smb connection option in the protocol
			 * vol->secFlg |= CIFSSEC_MUST_SEAL;
			 */
			vol->seal = 1;
			break;
		case Opt_noac:
			pr_warn("CIFS: Mount option noac not supported. Instead set /proc/fs/cifs/LookupCacheEnabled to 0\n");
			break;
		case Opt_fsc:
#ifndef CONFIG_CIFS_FSCACHE
			cifs_dbg(VFS, "FS-Cache support needs CONFIG_CIFS_FSCACHE kernel config option set\n");
			goto cifs_parse_mount_err;
#endif
			vol->fsc = true;
			break;
		case Opt_mfsymlinks:
			vol->mfsymlinks = true;
			break;
		case Opt_multiuser:
			vol->multiuser = true;
			break;
		case Opt_sloppy:
			sloppy = true;
			break;
		case Opt_nosharesock:
			vol->nosharesock = true;
			break;
		case Opt_nopersistent:
			vol->nopersistent = true;
			if (vol->persistent) {
				cifs_dbg(VFS,
				  "persistenthandles mount options conflict\n");
				goto cifs_parse_mount_err;
			}
			break;
		case Opt_persistent:
			vol->persistent = true;
			if ((vol->nopersistent) || (vol->resilient)) {
				cifs_dbg(VFS,
				  "persistenthandles mount options conflict\n");
				goto cifs_parse_mount_err;
			}
			break;
		case Opt_resilient:
			vol->resilient = true;
			if (vol->persistent) {
				cifs_dbg(VFS,
				  "persistenthandles mount options conflict\n");
				goto cifs_parse_mount_err;
			}
			break;
		case Opt_noresilient:
			vol->resilient = false; /* already the default */
			break;

		/* Numeric Values */
		case Opt_backupuid:
			if (get_option_uid(args, &vol->backupuid)) {
				cifs_dbg(VFS, "%s: Invalid backupuid value\n",
					 __func__);
				goto cifs_parse_mount_err;
			}
			vol->backupuid_specified = true;
			break;
		case Opt_backupgid:
			if (get_option_gid(args, &vol->backupgid)) {
				cifs_dbg(VFS, "%s: Invalid backupgid value\n",
					 __func__);
				goto cifs_parse_mount_err;
			}
			vol->backupgid_specified = true;
			break;
		case Opt_uid:
			if (get_option_uid(args, &vol->linux_uid)) {
				cifs_dbg(VFS, "%s: Invalid uid value\n",
					 __func__);
				goto cifs_parse_mount_err;
			}
			uid_specified = true;
			break;
		case Opt_cruid:
			if (get_option_uid(args, &vol->cred_uid)) {
				cifs_dbg(VFS, "%s: Invalid cruid value\n",
					 __func__);
				goto cifs_parse_mount_err;
			}
			break;
		case Opt_gid:
			if (get_option_gid(args, &vol->linux_gid)) {
				cifs_dbg(VFS, "%s: Invalid gid value\n",
					 __func__);
				goto cifs_parse_mount_err;
			}
			gid_specified = true;
			break;
		case Opt_file_mode:
			if (get_option_ul(args, &option)) {
				cifs_dbg(VFS, "%s: Invalid file_mode value\n",
					 __func__);
				goto cifs_parse_mount_err;
			}
			vol->file_mode = option;
			break;
		case Opt_dirmode:
			if (get_option_ul(args, &option)) {
				cifs_dbg(VFS, "%s: Invalid dir_mode value\n",
					 __func__);
				goto cifs_parse_mount_err;
			}
			vol->dir_mode = option;
			break;
		case Opt_port:
			if (get_option_ul(args, &option) ||
			    option > USHRT_MAX) {
				cifs_dbg(VFS, "%s: Invalid port value\n",
					 __func__);
				goto cifs_parse_mount_err;
			}
			port = (unsigned short)option;
			break;
		case Opt_rsize:
			if (get_option_ul(args, &option)) {
				cifs_dbg(VFS, "%s: Invalid rsize value\n",
					 __func__);
				goto cifs_parse_mount_err;
			}
			vol->rsize = option;
			break;
		case Opt_wsize:
			if (get_option_ul(args, &option)) {
				cifs_dbg(VFS, "%s: Invalid wsize value\n",
					 __func__);
				goto cifs_parse_mount_err;
			}
			vol->wsize = option;
			break;
		case Opt_actimeo:
			if (get_option_ul(args, &option)) {
				cifs_dbg(VFS, "%s: Invalid actimeo value\n",
					 __func__);
				goto cifs_parse_mount_err;
			}
			vol->actimeo = HZ * option;
			if (vol->actimeo > CIFS_MAX_ACTIMEO) {
				cifs_dbg(VFS, "attribute cache timeout too large\n");
				goto cifs_parse_mount_err;
			}
			break;

		/* String Arguments */

		case Opt_blank_user:
			/* null user, ie. anonymous authentication */
			vol->nullauth = 1;
			vol->username = NULL;
			break;
		case Opt_user:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;

			if (strnlen(string, CIFS_MAX_USERNAME_LEN) >
							CIFS_MAX_USERNAME_LEN) {
				pr_warn("CIFS: username too long\n");
				goto cifs_parse_mount_err;
			}

			kfree(vol->username);
			vol->username = kstrdup(string, GFP_KERNEL);
			if (!vol->username)
				goto cifs_parse_mount_err;
			break;
		case Opt_blank_pass:
			/* passwords have to be handled differently
			 * to allow the character used for deliminator
			 * to be passed within them
			 */

			/*
			 * Check if this is a case where the  password
			 * starts with a delimiter
			 */
			tmp_end = strchr(data, '=');
			tmp_end++;
			if (!(tmp_end < end && tmp_end[1] == delim)) {
				/* No it is not. Set the password to NULL */
				kfree(vol->password);
				vol->password = NULL;
				break;
			}
			/* Yes it is. Drop down to Opt_pass below.*/
		case Opt_pass:
			/* Obtain the value string */
			value = strchr(data, '=');
			value++;

			/* Set tmp_end to end of the string */
			tmp_end = (char *) value + strlen(value);

			/* Check if following character is the deliminator
			 * If yes, we have encountered a double deliminator
			 * reset the NULL character to the deliminator
			 */
			if (tmp_end < end && tmp_end[1] == delim) {
				tmp_end[0] = delim;

				/* Keep iterating until we get to a single
				 * deliminator OR the end
				 */
				while ((tmp_end = strchr(tmp_end, delim))
					!= NULL && (tmp_end[1] == delim)) {
						tmp_end = (char *) &tmp_end[2];
				}

				/* Reset var options to point to next element */
				if (tmp_end) {
					tmp_end[0] = '\0';
					options = (char *) &tmp_end[1];
				} else
					/* Reached the end of the mount option
					 * string */
					options = end;
			}

			kfree(vol->password);
			/* Now build new password string */
			temp_len = strlen(value);
			vol->password = kzalloc(temp_len+1, GFP_KERNEL);
			if (vol->password == NULL) {
				pr_warn("CIFS: no memory for password\n");
				goto cifs_parse_mount_err;
			}

			for (i = 0, j = 0; i < temp_len; i++, j++) {
				vol->password[j] = value[i];
				if ((value[i] == delim) &&
				     value[i+1] == delim)
					/* skip the second deliminator */
					i++;
			}
			vol->password[j] = '\0';
			break;
		case Opt_blank_ip:
			/* FIXME: should this be an error instead? */
			got_ip = false;
			break;
		case Opt_ip:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;

			if (!cifs_convert_address(dstaddr, string,
					strlen(string))) {
				pr_err("CIFS: bad ip= option (%s).\n", string);
				goto cifs_parse_mount_err;
			}
			got_ip = true;
			break;
		case Opt_domain:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;

			if (strnlen(string, CIFS_MAX_DOMAINNAME_LEN)
					== CIFS_MAX_DOMAINNAME_LEN) {
				pr_warn("CIFS: domain name too long\n");
				goto cifs_parse_mount_err;
			}

			kfree(vol->domainname);
			vol->domainname = kstrdup(string, GFP_KERNEL);
			if (!vol->domainname) {
				pr_warn("CIFS: no memory for domainname\n");
				goto cifs_parse_mount_err;
			}
			cifs_dbg(FYI, "Domain name set\n");
			break;
		case Opt_srcaddr:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;

			if (!cifs_convert_address(
					(struct sockaddr *)&vol->srcaddr,
					string, strlen(string))) {
				pr_warn("CIFS: Could not parse srcaddr: %s\n",
					string);
				goto cifs_parse_mount_err;
			}
			break;
		case Opt_iocharset:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;

			if (strnlen(string, 1024) >= 65) {
				pr_warn("CIFS: iocharset name too long.\n");
				goto cifs_parse_mount_err;
			}

			 if (strncasecmp(string, "default", 7) != 0) {
				kfree(vol->iocharset);
				vol->iocharset = kstrdup(string,
							 GFP_KERNEL);
				if (!vol->iocharset) {
					pr_warn("CIFS: no memory for charset\n");
					goto cifs_parse_mount_err;
				}
			}
			/* if iocharset not set then load_nls_default
			 * is used by caller
			 */
			 cifs_dbg(FYI, "iocharset set to %s\n", string);
			break;
		case Opt_netbiosname:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;

			memset(vol->source_rfc1001_name, 0x20,
				RFC1001_NAME_LEN);
			/*
			 * FIXME: are there cases in which a comma can
			 * be valid in workstation netbios name (and
			 * need special handling)?
			 */
			for (i = 0; i < RFC1001_NAME_LEN; i++) {
				/* don't ucase netbiosname for user */
				if (string[i] == 0)
					break;
				vol->source_rfc1001_name[i] = string[i];
			}
			/* The string has 16th byte zero still from
			 * set at top of the function
			 */
			if (i == RFC1001_NAME_LEN && string[i] != 0)
				pr_warn("CIFS: netbiosname longer than 15 truncated.\n");
			break;
		case Opt_servern:
			/* servernetbiosname specified override *SMBSERVER */
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;

			/* last byte, type, is 0x20 for servr type */
			memset(vol->target_rfc1001_name, 0x20,
				RFC1001_NAME_LEN_WITH_NULL);

			/* BB are there cases in which a comma can be
			   valid in this workstation netbios name
			   (and need special handling)? */

			/* user or mount helper must uppercase the
			   netbios name */
			for (i = 0; i < 15; i++) {
				if (string[i] == 0)
					break;
				vol->target_rfc1001_name[i] = string[i];
			}
			/* The string has 16th byte zero still from
			   set at top of the function  */
			if (i == RFC1001_NAME_LEN && string[i] != 0)
				pr_warn("CIFS: server netbiosname longer than 15 truncated.\n");
			break;
		case Opt_ver:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;

			if (strncasecmp(string, "1", 1) == 0) {
				/* This is the default */
				break;
			}
			/* For all other value, error */
			pr_warn("CIFS: Invalid version specified\n");
			goto cifs_parse_mount_err;
		case Opt_vers:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;

			if (cifs_parse_smb_version(string, vol) != 0)
				goto cifs_parse_mount_err;
			break;
		case Opt_sec:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;

			if (cifs_parse_security_flavors(string, vol) != 0)
				goto cifs_parse_mount_err;
			break;
		case Opt_cache:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;

			if (cifs_parse_cache_flavor(string, vol) != 0)
				goto cifs_parse_mount_err;
			break;
		default:
			/*
			 * An option we don't recognize. Save it off for later
			 * if we haven't already found one
			 */
			if (!invalid)
				invalid = data;
			break;
		}
		/* Free up any allocated string */
		kfree(string);
		string = NULL;
	}

	if (!sloppy && invalid) {
		pr_err("CIFS: Unknown mount option \"%s\"\n", invalid);
		goto cifs_parse_mount_err;
	}

#ifndef CONFIG_KEYS
	/* Muliuser mounts require CONFIG_KEYS support */
	if (vol->multiuser) {
		cifs_dbg(VFS, "Multiuser mounts require kernels with CONFIG_KEYS enabled\n");
		goto cifs_parse_mount_err;
	}
#endif
	if (!vol->UNC) {
		cifs_dbg(VFS, "CIFS mount error: No usable UNC path provided in device string!\n");
		goto cifs_parse_mount_err;
	}

	/* make sure UNC has a share name */
	if (!strchr(vol->UNC + 3, '\\')) {
		cifs_dbg(VFS, "Malformed UNC. Unable to find share name.\n");
		goto cifs_parse_mount_err;
	}

	if (!got_ip) {
		/* No ip= option specified? Try to get it from UNC */
		if (!cifs_convert_address(dstaddr, &vol->UNC[2],
						strlen(&vol->UNC[2]))) {
			pr_err("Unable to determine destination address.\n");
			goto cifs_parse_mount_err;
		}
	}

	/* set the port that we got earlier */
	cifs_set_port(dstaddr, port);

	if (uid_specified)
		vol->override_uid = override_uid;
	else if (override_uid == 1)
		pr_notice("CIFS: ignoring forceuid mount option specified with no uid= option.\n");

	if (gid_specified)
		vol->override_gid = override_gid;
	else if (override_gid == 1)
		pr_notice("CIFS: ignoring forcegid mount option specified with no gid= option.\n");

	kfree(mountdata_copy);
	return 0;

out_nomem:
	pr_warn("Could not allocate temporary buffer\n");
cifs_parse_mount_err:
	kfree(string);
	kfree(mountdata_copy);
	return 1;
}

/** Returns true if srcaddr isn't specified and rhs isn't
 * specified, or if srcaddr is specified and
 * matches the IP address of the rhs argument.
 */
static bool
srcip_matches(struct sockaddr *srcaddr, struct sockaddr *rhs)
{
	switch (srcaddr->sa_family) {
	case AF_UNSPEC:
		return (rhs->sa_family == AF_UNSPEC);
	case AF_INET: {
		struct sockaddr_in *saddr4 = (struct sockaddr_in *)srcaddr;
		struct sockaddr_in *vaddr4 = (struct sockaddr_in *)rhs;
		return (saddr4->sin_addr.s_addr == vaddr4->sin_addr.s_addr);
	}
	case AF_INET6: {
		struct sockaddr_in6 *saddr6 = (struct sockaddr_in6 *)srcaddr;
		struct sockaddr_in6 *vaddr6 = (struct sockaddr_in6 *)rhs;
		return ipv6_addr_equal(&saddr6->sin6_addr, &vaddr6->sin6_addr);
	}
	default:
		WARN_ON(1);
		return false; /* don't expect to be here */
	}
}

/*
 * If no port is specified in addr structure, we try to match with 445 port
 * and if it fails - with 139 ports. It should be called only if address
 * families of server and addr are equal.
 */
static bool
match_port(struct TCP_Server_Info *server, struct sockaddr *addr)
{
	__be16 port, *sport;

	switch (addr->sa_family) {
	case AF_INET:
		sport = &((struct sockaddr_in *) &server->dstaddr)->sin_port;
		port = ((struct sockaddr_in *) addr)->sin_port;
		break;
	case AF_INET6:
		sport = &((struct sockaddr_in6 *) &server->dstaddr)->sin6_port;
		port = ((struct sockaddr_in6 *) addr)->sin6_port;
		break;
	default:
		WARN_ON(1);
		return false;
	}

	if (!port) {
		port = htons(CIFS_PORT);
		if (port == *sport)
			return true;

		port = htons(RFC1001_PORT);
	}

	return port == *sport;
}

static bool
match_address(struct TCP_Server_Info *server, struct sockaddr *addr,
	      struct sockaddr *srcaddr)
{
	switch (addr->sa_family) {
	case AF_INET: {
		struct sockaddr_in *addr4 = (struct sockaddr_in *)addr;
		struct sockaddr_in *srv_addr4 =
					(struct sockaddr_in *)&server->dstaddr;

		if (addr4->sin_addr.s_addr != srv_addr4->sin_addr.s_addr)
			return false;
		break;
	}
	case AF_INET6: {
		struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)addr;
		struct sockaddr_in6 *srv_addr6 =
					(struct sockaddr_in6 *)&server->dstaddr;

		if (!ipv6_addr_equal(&addr6->sin6_addr,
				     &srv_addr6->sin6_addr))
			return false;
		if (addr6->sin6_scope_id != srv_addr6->sin6_scope_id)
			return false;
		break;
	}
	default:
		WARN_ON(1);
		return false; /* don't expect to be here */
	}

	if (!srcip_matches(srcaddr, (struct sockaddr *)&server->srcaddr))
		return false;

	return true;
}

static bool
match_security(struct TCP_Server_Info *server, struct smb_vol *vol)
{
	/*
	 * The select_sectype function should either return the vol->sectype
	 * that was specified, or "Unspecified" if that sectype was not
	 * compatible with the given NEGOTIATE request.
	 */
	if (select_sectype(server, vol->sectype) == Unspecified)
		return false;

	/*
	 * Now check if signing mode is acceptable. No need to check
	 * global_secflags at this point since if MUST_SIGN is set then
	 * the server->sign had better be too.
	 */
	if (vol->sign && !server->sign)
		return false;

	return true;
}

static int match_server(struct TCP_Server_Info *server, struct smb_vol *vol)
{
	struct sockaddr *addr = (struct sockaddr *)&vol->dstaddr;

	if (vol->nosharesock)
		return 0;

	if ((server->vals != vol->vals) || (server->ops != vol->ops))
		return 0;

	if (!net_eq(cifs_net_ns(server), current->nsproxy->net_ns))
		return 0;

	if (!match_address(server, addr,
			   (struct sockaddr *)&vol->srcaddr))
		return 0;

	if (!match_port(server, addr))
		return 0;

	if (!match_security(server, vol))
		return 0;

	return 1;
}

static struct TCP_Server_Info *
cifs_find_tcp_session(struct smb_vol *vol)
{
	struct TCP_Server_Info *server;

	spin_lock(&cifs_tcp_ses_lock);
	list_for_each_entry(server, &cifs_tcp_ses_list, tcp_ses_list) {
		if (!match_server(server, vol))
			continue;

		++server->srv_count;
		spin_unlock(&cifs_tcp_ses_lock);
		cifs_dbg(FYI, "Existing tcp session with server found\n");
		return server;
	}
	spin_unlock(&cifs_tcp_ses_lock);
	return NULL;
}

static void
cifs_put_tcp_session(struct TCP_Server_Info *server)
{
	struct task_struct *task;

	spin_lock(&cifs_tcp_ses_lock);
	if (--server->srv_count > 0) {
		spin_unlock(&cifs_tcp_ses_lock);
		return;
	}

	put_net(cifs_net_ns(server));

	list_del_init(&server->tcp_ses_list);
	spin_unlock(&cifs_tcp_ses_lock);

	cancel_delayed_work_sync(&server->echo);

	spin_lock(&GlobalMid_Lock);
	server->tcpStatus = CifsExiting;
	spin_unlock(&GlobalMid_Lock);

	cifs_crypto_shash_release(server);
	cifs_fscache_release_client_cookie(server);

	kfree(server->session_key.response);
	server->session_key.response = NULL;
	server->session_key.len = 0;

	task = xchg(&server->tsk, NULL);
	if (task)
		force_sig(SIGKILL, task);
}

static struct TCP_Server_Info *
cifs_get_tcp_session(struct smb_vol *volume_info)
{
	struct TCP_Server_Info *tcp_ses = NULL;
	int rc;

	cifs_dbg(FYI, "UNC: %s\n", volume_info->UNC);

	/* see if we already have a matching tcp_ses */
	tcp_ses = cifs_find_tcp_session(volume_info);
	if (tcp_ses)
		return tcp_ses;

	tcp_ses = kzalloc(sizeof(struct TCP_Server_Info), GFP_KERNEL);
	if (!tcp_ses) {
		rc = -ENOMEM;
		goto out_err;
	}

	tcp_ses->ops = volume_info->ops;
	tcp_ses->vals = volume_info->vals;
	cifs_set_net_ns(tcp_ses, get_net(current->nsproxy->net_ns));
	tcp_ses->hostname = extract_hostname(volume_info->UNC);
	if (IS_ERR(tcp_ses->hostname)) {
		rc = PTR_ERR(tcp_ses->hostname);
		goto out_err_crypto_release;
	}

	tcp_ses->noblocksnd = volume_info->noblocksnd;
	tcp_ses->noautotune = volume_info->noautotune;
	tcp_ses->tcp_nodelay = volume_info->sockopt_tcp_nodelay;
	tcp_ses->in_flight = 0;
	tcp_ses->credits = 1;
	init_waitqueue_head(&tcp_ses->response_q);
	init_waitqueue_head(&tcp_ses->request_q);
	INIT_LIST_HEAD(&tcp_ses->pending_mid_q);
	mutex_init(&tcp_ses->srv_mutex);
	memcpy(tcp_ses->workstation_RFC1001_name,
		volume_info->source_rfc1001_name, RFC1001_NAME_LEN_WITH_NULL);
	memcpy(tcp_ses->server_RFC1001_name,
		volume_info->target_rfc1001_name, RFC1001_NAME_LEN_WITH_NULL);
	tcp_ses->session_estab = false;
	tcp_ses->sequence_number = 0;
	tcp_ses->lstrp = jiffies;
	spin_lock_init(&tcp_ses->req_lock);
	INIT_LIST_HEAD(&tcp_ses->tcp_ses_list);
	INIT_LIST_HEAD(&tcp_ses->smb_ses_list);
	INIT_DELAYED_WORK(&tcp_ses->echo, cifs_echo_request);
	memcpy(&tcp_ses->srcaddr, &volume_info->srcaddr,
	       sizeof(tcp_ses->srcaddr));
	memcpy(&tcp_ses->dstaddr, &volume_info->dstaddr,
		sizeof(tcp_ses->dstaddr));
#ifdef CONFIG_CIFS_SMB2
	generate_random_uuid(tcp_ses->client_guid);
#endif
	/*
	 * at this point we are the only ones with the pointer
	 * to the struct since the kernel thread not created yet
	 * no need to spinlock this init of tcpStatus or srv_count
	 */
	tcp_ses->tcpStatus = CifsNew;
	++tcp_ses->srv_count;

	rc = ip_connect(tcp_ses);
	if (rc < 0) {
		cifs_dbg(VFS, "Error connecting to socket. Aborting operation.\n");
		goto out_err_crypto_release;
	}

	/*
	 * since we're in a cifs function already, we know that
	 * this will succeed. No need for try_module_get().
	 */
	__module_get(THIS_MODULE);
	tcp_ses->tsk = kthread_run(cifs_demultiplex_thread,
				  tcp_ses, "cifsd");
	if (IS_ERR(tcp_ses->tsk)) {
		rc = PTR_ERR(tcp_ses->tsk);
		cifs_dbg(VFS, "error %d create cifsd thread\n", rc);
		module_put(THIS_MODULE);
		goto out_err_crypto_release;
	}
	tcp_ses->tcpStatus = CifsNeedNegotiate;

	/* thread spawned, put it on the list */
	spin_lock(&cifs_tcp_ses_lock);
	list_add(&tcp_ses->tcp_ses_list, &cifs_tcp_ses_list);
	spin_unlock(&cifs_tcp_ses_lock);

	cifs_fscache_get_client_cookie(tcp_ses);

	/* queue echo request delayed work */
	queue_delayed_work(cifsiod_wq, &tcp_ses->echo, SMB_ECHO_INTERVAL);

	return tcp_ses;

out_err_crypto_release:
	cifs_crypto_shash_release(tcp_ses);

	put_net(cifs_net_ns(tcp_ses));

out_err:
	if (tcp_ses) {
		if (!IS_ERR(tcp_ses->hostname))
			kfree(tcp_ses->hostname);
		if (tcp_ses->ssocket)
			sock_release(tcp_ses->ssocket);
		kfree(tcp_ses);
	}
	return ERR_PTR(rc);
}

static int match_session(struct cifs_ses *ses, struct smb_vol *vol)
{
	if (vol->sectype != Unspecified &&
	    vol->sectype != ses->sectype)
		return 0;

	switch (ses->sectype) {
	case Kerberos:
		if (!uid_eq(vol->cred_uid, ses->cred_uid))
			return 0;
		break;
	default:
		/* NULL username means anonymous session */
		if (ses->user_name == NULL) {
			if (!vol->nullauth)
				return 0;
			break;
		}

		/* anything else takes username/password */
		if (strncmp(ses->user_name,
			    vol->username ? vol->username : "",
			    CIFS_MAX_USERNAME_LEN))
			return 0;
		if ((vol->username && strlen(vol->username) != 0) &&
		    ses->password != NULL &&
		    strncmp(ses->password,
			    vol->password ? vol->password : "",
			    CIFS_MAX_PASSWORD_LEN))
			return 0;
	}
	return 1;
}

static struct cifs_ses *
cifs_find_smb_ses(struct TCP_Server_Info *server, struct smb_vol *vol)
{
	struct cifs_ses *ses;

	spin_lock(&cifs_tcp_ses_lock);
	list_for_each_entry(ses, &server->smb_ses_list, smb_ses_list) {
		if (ses->status == CifsExiting)
			continue;
		if (!match_session(ses, vol))
			continue;
		++ses->ses_count;
		spin_unlock(&cifs_tcp_ses_lock);
		return ses;
	}
	spin_unlock(&cifs_tcp_ses_lock);
	return NULL;
}

static void
cifs_put_smb_ses(struct cifs_ses *ses)
{
	unsigned int rc, xid;
	struct TCP_Server_Info *server = ses->server;

	cifs_dbg(FYI, "%s: ses_count=%d\n", __func__, ses->ses_count);

	spin_lock(&cifs_tcp_ses_lock);
	if (ses->status == CifsExiting) {
		spin_unlock(&cifs_tcp_ses_lock);
		return;
	}
	if (--ses->ses_count > 0) {
		spin_unlock(&cifs_tcp_ses_lock);
		return;
	}
	if (ses->status == CifsGood)
		ses->status = CifsExiting;
	spin_unlock(&cifs_tcp_ses_lock);

	if (ses->status == CifsExiting && server->ops->logoff) {
		xid = get_xid();
		rc = server->ops->logoff(xid, ses);
		if (rc)
			cifs_dbg(VFS, "%s: Session Logoff failure rc=%d\n",
				__func__, rc);
		_free_xid(xid);
	}

	spin_lock(&cifs_tcp_ses_lock);
	list_del_init(&ses->smb_ses_list);
	spin_unlock(&cifs_tcp_ses_lock);

	sesInfoFree(ses);
	cifs_put_tcp_session(server);
}

#ifdef CONFIG_KEYS

/* strlen("cifs:a:") + CIFS_MAX_DOMAINNAME_LEN + 1 */
#define CIFSCREDS_DESC_SIZE (7 + CIFS_MAX_DOMAINNAME_LEN + 1)

/* Populate username and pw fields from keyring if possible */
static int
cifs_set_cifscreds(struct smb_vol *vol, struct cifs_ses *ses)
{
	int rc = 0;
	const char *delim, *payload;
	char *desc;
	ssize_t len;
	struct key *key;
	struct TCP_Server_Info *server = ses->server;
	struct sockaddr_in *sa;
	struct sockaddr_in6 *sa6;
	const struct user_key_payload *upayload;

	desc = kmalloc(CIFSCREDS_DESC_SIZE, GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	/* try to find an address key first */
	switch (server->dstaddr.ss_family) {
	case AF_INET:
		sa = (struct sockaddr_in *)&server->dstaddr;
		sprintf(desc, "cifs:a:%pI4", &sa->sin_addr.s_addr);
		break;
	case AF_INET6:
		sa6 = (struct sockaddr_in6 *)&server->dstaddr;
		sprintf(desc, "cifs:a:%pI6c", &sa6->sin6_addr.s6_addr);
		break;
	default:
		cifs_dbg(FYI, "Bad ss_family (%hu)\n",
			 server->dstaddr.ss_family);
		rc = -EINVAL;
		goto out_err;
	}

	cifs_dbg(FYI, "%s: desc=%s\n", __func__, desc);
	key = request_key(&key_type_logon, desc, "");
	if (IS_ERR(key)) {
		if (!ses->domainName) {
			cifs_dbg(FYI, "domainName is NULL\n");
			rc = PTR_ERR(key);
			goto out_err;
		}

		/* didn't work, try to find a domain key */
		sprintf(desc, "cifs:d:%s", ses->domainName);
		cifs_dbg(FYI, "%s: desc=%s\n", __func__, desc);
		key = request_key(&key_type_logon, desc, "");
		if (IS_ERR(key)) {
			rc = PTR_ERR(key);
			goto out_err;
		}
	}

	down_read(&key->sem);
	upayload = user_key_payload(key);
	if (IS_ERR_OR_NULL(upayload)) {
		rc = upayload ? PTR_ERR(upayload) : -EINVAL;
		goto out_key_put;
	}

	/* find first : in payload */
	payload = upayload->data;
	delim = strnchr(payload, upayload->datalen, ':');
	cifs_dbg(FYI, "payload=%s\n", payload);
	if (!delim) {
		cifs_dbg(FYI, "Unable to find ':' in payload (datalen=%d)\n",
			 upayload->datalen);
		rc = -EINVAL;
		goto out_key_put;
	}

	len = delim - payload;
	if (len > CIFS_MAX_USERNAME_LEN || len <= 0) {
		cifs_dbg(FYI, "Bad value from username search (len=%zd)\n",
			 len);
		rc = -EINVAL;
		goto out_key_put;
	}

	vol->username = kstrndup(payload, len, GFP_KERNEL);
	if (!vol->username) {
		cifs_dbg(FYI, "Unable to allocate %zd bytes for username\n",
			 len);
		rc = -ENOMEM;
		goto out_key_put;
	}
	cifs_dbg(FYI, "%s: username=%s\n", __func__, vol->username);

	len = key->datalen - (len + 1);
	if (len > CIFS_MAX_PASSWORD_LEN || len <= 0) {
		cifs_dbg(FYI, "Bad len for password search (len=%zd)\n", len);
		rc = -EINVAL;
		kfree(vol->username);
		vol->username = NULL;
		goto out_key_put;
	}

	++delim;
	vol->password = kstrndup(delim, len, GFP_KERNEL);
	if (!vol->password) {
		cifs_dbg(FYI, "Unable to allocate %zd bytes for password\n",
			 len);
		rc = -ENOMEM;
		kfree(vol->username);
		vol->username = NULL;
		goto out_key_put;
	}

out_key_put:
	up_read(&key->sem);
	key_put(key);
out_err:
	kfree(desc);
	cifs_dbg(FYI, "%s: returning %d\n", __func__, rc);
	return rc;
}
#else /* ! CONFIG_KEYS */
static inline int
cifs_set_cifscreds(struct smb_vol *vol __attribute__((unused)),
		   struct cifs_ses *ses __attribute__((unused)))
{
	return -ENOSYS;
}
#endif /* CONFIG_KEYS */

static struct cifs_ses *
cifs_get_smb_ses(struct TCP_Server_Info *server, struct smb_vol *volume_info)
{
	int rc = -ENOMEM;
	unsigned int xid;
	struct cifs_ses *ses;
	struct sockaddr_in *addr = (struct sockaddr_in *)&server->dstaddr;
	struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&server->dstaddr;

	xid = get_xid();

	ses = cifs_find_smb_ses(server, volume_info);
	if (ses) {
		cifs_dbg(FYI, "Existing smb sess found (status=%d)\n",
			 ses->status);

		mutex_lock(&ses->session_mutex);
		rc = cifs_negotiate_protocol(xid, ses);
		if (rc) {
			mutex_unlock(&ses->session_mutex);
			/* problem -- put our ses reference */
			cifs_put_smb_ses(ses);
			free_xid(xid);
			return ERR_PTR(rc);
		}
		if (ses->need_reconnect) {
			cifs_dbg(FYI, "Session needs reconnect\n");
			rc = cifs_setup_session(xid, ses,
						volume_info->local_nls);
			if (rc) {
				mutex_unlock(&ses->session_mutex);
				/* problem -- put our reference */
				cifs_put_smb_ses(ses);
				free_xid(xid);
				return ERR_PTR(rc);
			}
		}
		mutex_unlock(&ses->session_mutex);

		/* existing SMB ses has a server reference already */
		cifs_put_tcp_session(server);
		free_xid(xid);
		return ses;
	}

	cifs_dbg(FYI, "Existing smb sess not found\n");
	ses = sesInfoAlloc();
	if (ses == NULL)
		goto get_ses_fail;

	/* new SMB session uses our server ref */
	ses->server = server;
	if (server->dstaddr.ss_family == AF_INET6)
		sprintf(ses->serverName, "%pI6", &addr6->sin6_addr);
	else
		sprintf(ses->serverName, "%pI4", &addr->sin_addr);

	if (volume_info->username) {
		ses->user_name = kstrdup(volume_info->username, GFP_KERNEL);
		if (!ses->user_name)
			goto get_ses_fail;
	}

	/* volume_info->password freed at unmount */
	if (volume_info->password) {
		ses->password = kstrdup(volume_info->password, GFP_KERNEL);
		if (!ses->password)
			goto get_ses_fail;
	}
	if (volume_info->domainname) {
		ses->domainName = kstrdup(volume_info->domainname, GFP_KERNEL);
		if (!ses->domainName)
			goto get_ses_fail;
	}
	ses->cred_uid = volume_info->cred_uid;
	ses->linux_uid = volume_info->linux_uid;

	ses->sectype = volume_info->sectype;
	ses->sign = volume_info->sign;

	mutex_lock(&ses->session_mutex);
	rc = cifs_negotiate_protocol(xid, ses);
	if (!rc)
		rc = cifs_setup_session(xid, ses, volume_info->local_nls);
	mutex_unlock(&ses->session_mutex);
	if (rc)
		goto get_ses_fail;

	/* success, put it on the list */
	spin_lock(&cifs_tcp_ses_lock);
	list_add(&ses->smb_ses_list, &server->smb_ses_list);
	spin_unlock(&cifs_tcp_ses_lock);

	free_xid(xid);
	return ses;

get_ses_fail:
	sesInfoFree(ses);
	free_xid(xid);
	return ERR_PTR(rc);
}

static int match_tcon(struct cifs_tcon *tcon, const char *unc)
{
	if (tcon->tidStatus == CifsExiting)
		return 0;
	if (strncmp(tcon->treeName, unc, MAX_TREE_SIZE))
		return 0;
	return 1;
}

static struct cifs_tcon *
cifs_find_tcon(struct cifs_ses *ses, const char *unc)
{
	struct list_head *tmp;
	struct cifs_tcon *tcon;

	spin_lock(&cifs_tcp_ses_lock);
	list_for_each(tmp, &ses->tcon_list) {
		tcon = list_entry(tmp, struct cifs_tcon, tcon_list);
		if (!match_tcon(tcon, unc))
			continue;
		++tcon->tc_count;
		spin_unlock(&cifs_tcp_ses_lock);
		return tcon;
	}
	spin_unlock(&cifs_tcp_ses_lock);
	return NULL;
}

static void
cifs_put_tcon(struct cifs_tcon *tcon)
{
	unsigned int xid;
	struct cifs_ses *ses = tcon->ses;

	cifs_dbg(FYI, "%s: tc_count=%d\n", __func__, tcon->tc_count);
	spin_lock(&cifs_tcp_ses_lock);
	if (--tcon->tc_count > 0) {
		spin_unlock(&cifs_tcp_ses_lock);
		return;
	}

	list_del_init(&tcon->tcon_list);
	spin_unlock(&cifs_tcp_ses_lock);

	xid = get_xid();
	if (ses->server->ops->tree_disconnect)
		ses->server->ops->tree_disconnect(xid, tcon);
	_free_xid(xid);

	cifs_fscache_release_super_cookie(tcon);
	tconInfoFree(tcon);
	cifs_put_smb_ses(ses);
}

static struct cifs_tcon *
cifs_get_tcon(struct cifs_ses *ses, struct smb_vol *volume_info)
{
	int rc, xid;
	struct cifs_tcon *tcon;

	tcon = cifs_find_tcon(ses, volume_info->UNC);
	if (tcon) {
		cifs_dbg(FYI, "Found match on UNC path\n");
		/* existing tcon already has a reference */
		cifs_put_smb_ses(ses);
		if (tcon->seal != volume_info->seal)
			cifs_dbg(VFS, "transport encryption setting conflicts with existing tid\n");
		return tcon;
	}

	if (!ses->server->ops->tree_connect) {
		rc = -ENOSYS;
		goto out_fail;
	}

	tcon = tconInfoAlloc();
	if (tcon == NULL) {
		rc = -ENOMEM;
		goto out_fail;
	}

	tcon->ses = ses;
	if (volume_info->password) {
		tcon->password = kstrdup(volume_info->password, GFP_KERNEL);
		if (!tcon->password) {
			rc = -ENOMEM;
			goto out_fail;
		}
	}

	/*
	 * BB Do we need to wrap session_mutex around this TCon call and Unix
	 * SetFS as we do on SessSetup and reconnect?
	 */
	xid = get_xid();
	rc = ses->server->ops->tree_connect(xid, ses, volume_info->UNC, tcon,
					    volume_info->local_nls);
	free_xid(xid);
	cifs_dbg(FYI, "Tcon rc = %d\n", rc);
	if (rc)
		goto out_fail;

	if (volume_info->nodfs) {
		tcon->Flags &= ~SMB_SHARE_IS_IN_DFS;
		cifs_dbg(FYI, "DFS disabled (%d)\n", tcon->Flags);
	}
	tcon->seal = volume_info->seal;
	tcon->use_persistent = false;
	/* check if SMB2 or later, CIFS does not support persistent handles */
	if (volume_info->persistent) {
		if (ses->server->vals->protocol_id == 0) {
			cifs_dbg(VFS,
			     "SMB3 or later required for persistent handles\n");
			rc = -EOPNOTSUPP;
			goto out_fail;
#ifdef CONFIG_CIFS_SMB2
		} else if (ses->server->capabilities &
			   SMB2_GLOBAL_CAP_PERSISTENT_HANDLES)
			tcon->use_persistent = true;
		else /* persistent handles requested but not supported */ {
			cifs_dbg(VFS,
				"Persistent handles not supported on share\n");
			rc = -EOPNOTSUPP;
			goto out_fail;
#endif /* CONFIG_CIFS_SMB2 */
		}
#ifdef CONFIG_CIFS_SMB2
	} else if ((tcon->capabilities & SMB2_SHARE_CAP_CONTINUOUS_AVAILABILITY)
	     && (ses->server->capabilities & SMB2_GLOBAL_CAP_PERSISTENT_HANDLES)
	     && (volume_info->nopersistent == false)) {
		cifs_dbg(FYI, "enabling persistent handles\n");
		tcon->use_persistent = true;
#endif /* CONFIG_CIFS_SMB2 */
	} else if (volume_info->resilient) {
		if (ses->server->vals->protocol_id == 0) {
			cifs_dbg(VFS,
			     "SMB2.1 or later required for resilient handles\n");
			rc = -EOPNOTSUPP;
			goto out_fail;
		}
		tcon->use_resilient = true;
	}

	/*
	 * We can have only one retry value for a connection to a share so for
	 * resources mounted more than once to the same server share the last
	 * value passed in for the retry flag is used.
	 */
	tcon->retry = volume_info->retry;
	tcon->nocase = volume_info->nocase;
	tcon->local_lease = volume_info->local_lease;
	INIT_LIST_HEAD(&tcon->pending_opens);

	spin_lock(&cifs_tcp_ses_lock);
	list_add(&tcon->tcon_list, &ses->tcon_list);
	spin_unlock(&cifs_tcp_ses_lock);

	cifs_fscache_get_super_cookie(tcon);

	return tcon;

out_fail:
	tconInfoFree(tcon);
	return ERR_PTR(rc);
}

void
cifs_put_tlink(struct tcon_link *tlink)
{
	if (!tlink || IS_ERR(tlink))
		return;

	if (!atomic_dec_and_test(&tlink->tl_count) ||
	    test_bit(TCON_LINK_IN_TREE, &tlink->tl_flags)) {
		tlink->tl_time = jiffies;
		return;
	}

	if (!IS_ERR(tlink_tcon(tlink)))
		cifs_put_tcon(tlink_tcon(tlink));
	kfree(tlink);
	return;
}

static inline struct tcon_link *
cifs_sb_master_tlink(struct cifs_sb_info *cifs_sb)
{
	return cifs_sb->master_tlink;
}

static int
compare_mount_options(struct super_block *sb, struct cifs_mnt_data *mnt_data)
{
	struct cifs_sb_info *old = CIFS_SB(sb);
	struct cifs_sb_info *new = mnt_data->cifs_sb;

	if ((sb->s_flags & CIFS_MS_MASK) != (mnt_data->flags & CIFS_MS_MASK))
		return 0;

	if ((old->mnt_cifs_flags & CIFS_MOUNT_MASK) !=
	    (new->mnt_cifs_flags & CIFS_MOUNT_MASK))
		return 0;

	/*
	 * We want to share sb only if we don't specify an r/wsize or
	 * specified r/wsize is greater than or equal to existing one.
	 */
	if (new->wsize && new->wsize < old->wsize)
		return 0;

	if (new->rsize && new->rsize < old->rsize)
		return 0;

	if (!uid_eq(old->mnt_uid, new->mnt_uid) || !gid_eq(old->mnt_gid, new->mnt_gid))
		return 0;

	if (old->mnt_file_mode != new->mnt_file_mode ||
	    old->mnt_dir_mode != new->mnt_dir_mode)
		return 0;

	if (strcmp(old->local_nls->charset, new->local_nls->charset))
		return 0;

	if (old->actimeo != new->actimeo)
		return 0;

	return 1;
}

int
cifs_match_super(struct super_block *sb, void *data)
{
	struct cifs_mnt_data *mnt_data = (struct cifs_mnt_data *)data;
	struct smb_vol *volume_info;
	struct cifs_sb_info *cifs_sb;
	struct TCP_Server_Info *tcp_srv;
	struct cifs_ses *ses;
	struct cifs_tcon *tcon;
	struct tcon_link *tlink;
	int rc = 0;

	spin_lock(&cifs_tcp_ses_lock);
	cifs_sb = CIFS_SB(sb);
	tlink = cifs_get_tlink(cifs_sb_master_tlink(cifs_sb));
	if (IS_ERR(tlink)) {
		spin_unlock(&cifs_tcp_ses_lock);
		return rc;
	}
	tcon = tlink_tcon(tlink);
	ses = tcon->ses;
	tcp_srv = ses->server;

	volume_info = mnt_data->vol;

	if (!match_server(tcp_srv, volume_info) ||
	    !match_session(ses, volume_info) ||
	    !match_tcon(tcon, volume_info->UNC)) {
		rc = 0;
		goto out;
	}

	rc = compare_mount_options(sb, mnt_data);
out:
	spin_unlock(&cifs_tcp_ses_lock);
	cifs_put_tlink(tlink);
	return rc;
}

int
get_dfs_path(const unsigned int xid, struct cifs_ses *ses, const char *old_path,
	     const struct nls_table *nls_codepage, unsigned int *num_referrals,
	     struct dfs_info3_param **referrals, int remap)
{
	char *temp_unc;
	int rc = 0;

	if (!ses->server->ops->tree_connect || !ses->server->ops->get_dfs_refer)
		return -ENOSYS;

	*num_referrals = 0;
	*referrals = NULL;

	if (ses->ipc_tid == 0) {
		temp_unc = kmalloc(2 /* for slashes */ +
			strnlen(ses->serverName, SERVER_NAME_LEN_WITH_NULL * 2)
				+ 1 + 4 /* slash IPC$ */ + 2, GFP_KERNEL);
		if (temp_unc == NULL)
			return -ENOMEM;
		temp_unc[0] = '\\';
		temp_unc[1] = '\\';
		strcpy(temp_unc + 2, ses->serverName);
		strcpy(temp_unc + 2 + strlen(ses->serverName), "\\IPC$");
		rc = ses->server->ops->tree_connect(xid, ses, temp_unc, NULL,
						    nls_codepage);
		cifs_dbg(FYI, "Tcon rc = %d ipc_tid = %d\n", rc, ses->ipc_tid);
		kfree(temp_unc);
	}
	if (rc == 0)
		rc = ses->server->ops->get_dfs_refer(xid, ses, old_path,
						     referrals, num_referrals,
						     nls_codepage, remap);
	/*
	 * BB - map targetUNCs to dfs_info3 structures, here or in
	 * ses->server->ops->get_dfs_refer.
	 */

	return rc;
}

#ifdef CONFIG_DEBUG_LOCK_ALLOC
static struct lock_class_key cifs_key[2];
static struct lock_class_key cifs_slock_key[2];

static inline void
cifs_reclassify_socket4(struct socket *sock)
{
	struct sock *sk = sock->sk;
	BUG_ON(sock_owned_by_user(sk));
	sock_lock_init_class_and_name(sk, "slock-AF_INET-CIFS",
		&cifs_slock_key[0], "sk_lock-AF_INET-CIFS", &cifs_key[0]);
}

static inline void
cifs_reclassify_socket6(struct socket *sock)
{
	struct sock *sk = sock->sk;
	BUG_ON(sock_owned_by_user(sk));
	sock_lock_init_class_and_name(sk, "slock-AF_INET6-CIFS",
		&cifs_slock_key[1], "sk_lock-AF_INET6-CIFS", &cifs_key[1]);
}
#else
static inline void
cifs_reclassify_socket4(struct socket *sock)
{
}

static inline void
cifs_reclassify_socket6(struct socket *sock)
{
}
#endif

/* See RFC1001 section 14 on representation of Netbios names */
static void rfc1002mangle(char *target, char *source, unsigned int length)
{
	unsigned int i, j;

	for (i = 0, j = 0; i < (length); i++) {
		/* mask a nibble at a time and encode */
		target[j] = 'A' + (0x0F & (source[i] >> 4));
		target[j+1] = 'A' + (0x0F & source[i]);
		j += 2;
	}

}

static int
bind_socket(struct TCP_Server_Info *server)
{
	int rc = 0;
	if (server->srcaddr.ss_family != AF_UNSPEC) {
		/* Bind to the specified local IP address */
		struct socket *socket = server->ssocket;
		rc = socket->ops->bind(socket,
				       (struct sockaddr *) &server->srcaddr,
				       sizeof(server->srcaddr));
		if (rc < 0) {
			struct sockaddr_in *saddr4;
			struct sockaddr_in6 *saddr6;
			saddr4 = (struct sockaddr_in *)&server->srcaddr;
			saddr6 = (struct sockaddr_in6 *)&server->srcaddr;
			if (saddr6->sin6_family == AF_INET6)
				cifs_dbg(VFS, "Failed to bind to: %pI6c, error: %d\n",
					 &saddr6->sin6_addr, rc);
			else
				cifs_dbg(VFS, "Failed to bind to: %pI4, error: %d\n",
					 &saddr4->sin_addr.s_addr, rc);
		}
	}
	return rc;
}

static int
ip_rfc1001_connect(struct TCP_Server_Info *server)
{
	int rc = 0;
	/*
	 * some servers require RFC1001 sessinit before sending
	 * negprot - BB check reconnection in case where second
	 * sessinit is sent but no second negprot
	 */
	struct rfc1002_session_packet *ses_init_buf;
	struct smb_hdr *smb_buf;
	ses_init_buf = kzalloc(sizeof(struct rfc1002_session_packet),
			       GFP_KERNEL);
	if (ses_init_buf) {
		ses_init_buf->trailer.session_req.called_len = 32;

		if (server->server_RFC1001_name &&
		    server->server_RFC1001_name[0] != 0)
			rfc1002mangle(ses_init_buf->trailer.
				      session_req.called_name,
				      server->server_RFC1001_name,
				      RFC1001_NAME_LEN_WITH_NULL);
		else
			rfc1002mangle(ses_init_buf->trailer.
				      session_req.called_name,
				      DEFAULT_CIFS_CALLED_NAME,
				      RFC1001_NAME_LEN_WITH_NULL);

		ses_init_buf->trailer.session_req.calling_len = 32;

		/*
		 * calling name ends in null (byte 16) from old smb
		 * convention.
		 */
		if (server->workstation_RFC1001_name[0] != 0)
			rfc1002mangle(ses_init_buf->trailer.
				      session_req.calling_name,
				      server->workstation_RFC1001_name,
				      RFC1001_NAME_LEN_WITH_NULL);
		else
			rfc1002mangle(ses_init_buf->trailer.
				      session_req.calling_name,
				      "LINUX_CIFS_CLNT",
				      RFC1001_NAME_LEN_WITH_NULL);

		ses_init_buf->trailer.session_req.scope1 = 0;
		ses_init_buf->trailer.session_req.scope2 = 0;
		smb_buf = (struct smb_hdr *)ses_init_buf;

		/* sizeof RFC1002_SESSION_REQUEST with no scope */
		smb_buf->smb_buf_length = cpu_to_be32(0x81000044);
		rc = smb_send(server, smb_buf, 0x44);
		kfree(ses_init_buf);
		/*
		 * RFC1001 layer in at least one server
		 * requires very short break before negprot
		 * presumably because not expecting negprot
		 * to follow so fast.  This is a simple
		 * solution that works without
		 * complicating the code and causes no
		 * significant slowing down on mount
		 * for everyone else
		 */
		usleep_range(1000, 2000);
	}
	/*
	 * else the negprot may still work without this
	 * even though malloc failed
	 */

	return rc;
}

static int
generic_ip_connect(struct TCP_Server_Info *server)
{
	int rc = 0;
	__be16 sport;
	int slen, sfamily;
	struct socket *socket = server->ssocket;
	struct sockaddr *saddr;

	saddr = (struct sockaddr *) &server->dstaddr;

	if (server->dstaddr.ss_family == AF_INET6) {
		sport = ((struct sockaddr_in6 *) saddr)->sin6_port;
		slen = sizeof(struct sockaddr_in6);
		sfamily = AF_INET6;
	} else {
		sport = ((struct sockaddr_in *) saddr)->sin_port;
		slen = sizeof(struct sockaddr_in);
		sfamily = AF_INET;
	}

	if (socket == NULL) {
		rc = __sock_create(cifs_net_ns(server), sfamily, SOCK_STREAM,
				   IPPROTO_TCP, &socket, 1);
		if (rc < 0) {
			cifs_dbg(VFS, "Error %d creating socket\n", rc);
			server->ssocket = NULL;
			return rc;
		}

		/* BB other socket options to set KEEPALIVE, NODELAY? */
		cifs_dbg(FYI, "Socket created\n");
		server->ssocket = socket;
		socket->sk->sk_allocation = GFP_NOFS;
		if (sfamily == AF_INET6)
			cifs_reclassify_socket6(socket);
		else
			cifs_reclassify_socket4(socket);
	}

	rc = bind_socket(server);
	if (rc < 0)
		return rc;

	/*
	 * Eventually check for other socket options to change from
	 * the default. sock_setsockopt not used because it expects
	 * user space buffer
	 */
	socket->sk->sk_rcvtimeo = 7 * HZ;
	socket->sk->sk_sndtimeo = 5 * HZ;

	/* make the bufsizes depend on wsize/rsize and max requests */
	if (server->noautotune) {
		if (socket->sk->sk_sndbuf < (200 * 1024))
			socket->sk->sk_sndbuf = 200 * 1024;
		if (socket->sk->sk_rcvbuf < (140 * 1024))
			socket->sk->sk_rcvbuf = 140 * 1024;
	}

	if (server->tcp_nodelay) {
		int val = 1;
		rc = kernel_setsockopt(socket, SOL_TCP, TCP_NODELAY,
				(char *)&val, sizeof(val));
		if (rc)
			cifs_dbg(FYI, "set TCP_NODELAY socket option error %d\n",
				 rc);
	}

	cifs_dbg(FYI, "sndbuf %d rcvbuf %d rcvtimeo 0x%lx\n",
		 socket->sk->sk_sndbuf,
		 socket->sk->sk_rcvbuf, socket->sk->sk_rcvtimeo);

	rc = socket->ops->connect(socket, saddr, slen, 0);
	if (rc < 0) {
		cifs_dbg(FYI, "Error %d connecting to server\n", rc);
		sock_release(socket);
		server->ssocket = NULL;
		return rc;
	}

	if (sport == htons(RFC1001_PORT))
		rc = ip_rfc1001_connect(server);

	return rc;
}

static int
ip_connect(struct TCP_Server_Info *server)
{
	__be16 *sport;
	struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&server->dstaddr;
	struct sockaddr_in *addr = (struct sockaddr_in *)&server->dstaddr;

	if (server->dstaddr.ss_family == AF_INET6)
		sport = &addr6->sin6_port;
	else
		sport = &addr->sin_port;

	if (*sport == 0) {
		int rc;

		/* try with 445 port at first */
		*sport = htons(CIFS_PORT);

		rc = generic_ip_connect(server);
		if (rc >= 0)
			return rc;

		/* if it failed, try with 139 port */
		*sport = htons(RFC1001_PORT);
	}

	return generic_ip_connect(server);
}

void reset_cifs_unix_caps(unsigned int xid, struct cifs_tcon *tcon,
			  struct cifs_sb_info *cifs_sb, struct smb_vol *vol_info)
{
	/* if we are reconnecting then should we check to see if
	 * any requested capabilities changed locally e.g. via
	 * remount but we can not do much about it here
	 * if they have (even if we could detect it by the following)
	 * Perhaps we could add a backpointer to array of sb from tcon
	 * or if we change to make all sb to same share the same
	 * sb as NFS - then we only have one backpointer to sb.
	 * What if we wanted to mount the server share twice once with
	 * and once without posixacls or posix paths? */
	__u64 saved_cap = le64_to_cpu(tcon->fsUnixInfo.Capability);

	if (vol_info && vol_info->no_linux_ext) {
		tcon->fsUnixInfo.Capability = 0;
		tcon->unix_ext = 0; /* Unix Extensions disabled */
		cifs_dbg(FYI, "Linux protocol extensions disabled\n");
		return;
	} else if (vol_info)
		tcon->unix_ext = 1; /* Unix Extensions supported */

	if (tcon->unix_ext == 0) {
		cifs_dbg(FYI, "Unix extensions disabled so not set on reconnect\n");
		return;
	}

	if (!CIFSSMBQFSUnixInfo(xid, tcon)) {
		__u64 cap = le64_to_cpu(tcon->fsUnixInfo.Capability);
		cifs_dbg(FYI, "unix caps which server supports %lld\n", cap);
		/* check for reconnect case in which we do not
		   want to change the mount behavior if we can avoid it */
		if (vol_info == NULL) {
			/* turn off POSIX ACL and PATHNAMES if not set
			   originally at mount time */
			if ((saved_cap & CIFS_UNIX_POSIX_ACL_CAP) == 0)
				cap &= ~CIFS_UNIX_POSIX_ACL_CAP;
			if ((saved_cap & CIFS_UNIX_POSIX_PATHNAMES_CAP) == 0) {
				if (cap & CIFS_UNIX_POSIX_PATHNAMES_CAP)
					cifs_dbg(VFS, "POSIXPATH support change\n");
				cap &= ~CIFS_UNIX_POSIX_PATHNAMES_CAP;
			} else if ((cap & CIFS_UNIX_POSIX_PATHNAMES_CAP) == 0) {
				cifs_dbg(VFS, "possible reconnect error\n");
				cifs_dbg(VFS, "server disabled POSIX path support\n");
			}
		}

		if (cap & CIFS_UNIX_TRANSPORT_ENCRYPTION_MANDATORY_CAP)
			cifs_dbg(VFS, "per-share encryption not supported yet\n");

		cap &= CIFS_UNIX_CAP_MASK;
		if (vol_info && vol_info->no_psx_acl)
			cap &= ~CIFS_UNIX_POSIX_ACL_CAP;
		else if (CIFS_UNIX_POSIX_ACL_CAP & cap) {
			cifs_dbg(FYI, "negotiated posix acl support\n");
			if (cifs_sb)
				cifs_sb->mnt_cifs_flags |=
					CIFS_MOUNT_POSIXACL;
		}

		if (vol_info && vol_info->posix_paths == 0)
			cap &= ~CIFS_UNIX_POSIX_PATHNAMES_CAP;
		else if (cap & CIFS_UNIX_POSIX_PATHNAMES_CAP) {
			cifs_dbg(FYI, "negotiate posix pathnames\n");
			if (cifs_sb)
				cifs_sb->mnt_cifs_flags |=
					CIFS_MOUNT_POSIX_PATHS;
		}

		cifs_dbg(FYI, "Negotiate caps 0x%x\n", (int)cap);
#ifdef CONFIG_CIFS_DEBUG2
		if (cap & CIFS_UNIX_FCNTL_CAP)
			cifs_dbg(FYI, "FCNTL cap\n");
		if (cap & CIFS_UNIX_EXTATTR_CAP)
			cifs_dbg(FYI, "EXTATTR cap\n");
		if (cap & CIFS_UNIX_POSIX_PATHNAMES_CAP)
			cifs_dbg(FYI, "POSIX path cap\n");
		if (cap & CIFS_UNIX_XATTR_CAP)
			cifs_dbg(FYI, "XATTR cap\n");
		if (cap & CIFS_UNIX_POSIX_ACL_CAP)
			cifs_dbg(FYI, "POSIX ACL cap\n");
		if (cap & CIFS_UNIX_LARGE_READ_CAP)
			cifs_dbg(FYI, "very large read cap\n");
		if (cap & CIFS_UNIX_LARGE_WRITE_CAP)
			cifs_dbg(FYI, "very large write cap\n");
		if (cap & CIFS_UNIX_TRANSPORT_ENCRYPTION_CAP)
			cifs_dbg(FYI, "transport encryption cap\n");
		if (cap & CIFS_UNIX_TRANSPORT_ENCRYPTION_MANDATORY_CAP)
			cifs_dbg(FYI, "mandatory transport encryption cap\n");
#endif /* CIFS_DEBUG2 */
		if (CIFSSMBSetFSUnixInfo(xid, tcon, cap)) {
			if (vol_info == NULL) {
				cifs_dbg(FYI, "resetting capabilities failed\n");
			} else
				cifs_dbg(VFS, "Negotiating Unix capabilities with the server failed. Consider mounting with the Unix Extensions disabled if problems are found by specifying the nounix mount option.\n");

		}
	}
}

void cifs_setup_cifs_sb(struct smb_vol *pvolume_info,
			struct cifs_sb_info *cifs_sb)
{
	INIT_DELAYED_WORK(&cifs_sb->prune_tlinks, cifs_prune_tlinks);

	spin_lock_init(&cifs_sb->tlink_tree_lock);
	cifs_sb->tlink_tree = RB_ROOT;

	/*
	 * Temporarily set r/wsize for matching superblock. If we end up using
	 * new sb then client will later negotiate it downward if needed.
	 */
	cifs_sb->rsize = pvolume_info->rsize;
	cifs_sb->wsize = pvolume_info->wsize;

	cifs_sb->mnt_uid = pvolume_info->linux_uid;
	cifs_sb->mnt_gid = pvolume_info->linux_gid;
	cifs_sb->mnt_file_mode = pvolume_info->file_mode;
	cifs_sb->mnt_dir_mode = pvolume_info->dir_mode;
	cifs_dbg(FYI, "file mode: 0x%hx  dir mode: 0x%hx\n",
		 cifs_sb->mnt_file_mode, cifs_sb->mnt_dir_mode);

	cifs_sb->actimeo = pvolume_info->actimeo;
	cifs_sb->local_nls = pvolume_info->local_nls;

	if (pvolume_info->noperm)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NO_PERM;
	if (pvolume_info->setuids)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_SET_UID;
	if (pvolume_info->server_ino)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_SERVER_INUM;
	if (pvolume_info->remap)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_MAP_SFM_CHR;
	if (pvolume_info->sfu_remap)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_MAP_SPECIAL_CHR;
	if (pvolume_info->no_xattr)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NO_XATTR;
	if (pvolume_info->sfu_emul)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_UNX_EMUL;
	if (pvolume_info->nobrl)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NO_BRL;
	if (pvolume_info->nostrictsync)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NOSSYNC;
	if (pvolume_info->mand_lock)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NOPOSIXBRL;
	if (pvolume_info->rwpidforward)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_RWPIDFORWARD;
	if (pvolume_info->cifs_acl)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_CIFS_ACL;
	if (pvolume_info->backupuid_specified) {
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_CIFS_BACKUPUID;
		cifs_sb->mnt_backupuid = pvolume_info->backupuid;
	}
	if (pvolume_info->backupgid_specified) {
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_CIFS_BACKUPGID;
		cifs_sb->mnt_backupgid = pvolume_info->backupgid;
	}
	if (pvolume_info->override_uid)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_OVERR_UID;
	if (pvolume_info->override_gid)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_OVERR_GID;
	if (pvolume_info->dynperm)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_DYNPERM;
	if (pvolume_info->fsc)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_FSCACHE;
	if (pvolume_info->multiuser)
		cifs_sb->mnt_cifs_flags |= (CIFS_MOUNT_MULTIUSER |
					    CIFS_MOUNT_NO_PERM);
	if (pvolume_info->strict_io)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_STRICT_IO;
	if (pvolume_info->direct_io) {
		cifs_dbg(FYI, "mounting share using direct i/o\n");
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_DIRECT_IO;
	}
	if (pvolume_info->mfsymlinks) {
		if (pvolume_info->sfu_emul) {
			/*
			 * Our SFU ("Services for Unix" emulation does not allow
			 * creating symlinks but does allow reading existing SFU
			 * symlinks (it does allow both creating and reading SFU
			 * style mknod and FIFOs though). When "mfsymlinks" and
			 * "sfu" are both enabled at the same time, it allows
			 * reading both types of symlinks, but will only create
			 * them with mfsymlinks format. This allows better
			 * Apple compatibility (probably better for Samba too)
			 * while still recognizing old Windows style symlinks.
			 */
			cifs_dbg(VFS, "mount options mfsymlinks and sfu both enabled\n");
		}
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_MF_SYMLINKS;
	}

	if ((pvolume_info->cifs_acl) && (pvolume_info->dynperm))
		cifs_dbg(VFS, "mount option dynperm ignored if cifsacl mount option supported\n");
}

static void
cleanup_volume_info_contents(struct smb_vol *volume_info)
{
	kfree(volume_info->username);
	kzfree(volume_info->password);
	kfree(volume_info->UNC);
	kfree(volume_info->domainname);
	kfree(volume_info->iocharset);
	kfree(volume_info->prepath);
}

void
cifs_cleanup_volume_info(struct smb_vol *volume_info)
{
	if (!volume_info)
		return;
	cleanup_volume_info_contents(volume_info);
	kfree(volume_info);
}


#ifdef CONFIG_CIFS_DFS_UPCALL
/*
 * cifs_build_path_to_root returns full path to root when we do not have an
 * exiting connection (tcon)
 */
static char *
build_unc_path_to_root(const struct smb_vol *vol,
		const struct cifs_sb_info *cifs_sb)
{
	char *full_path, *pos;
	unsigned int pplen = vol->prepath ? strlen(vol->prepath) + 1 : 0;
	unsigned int unc_len = strnlen(vol->UNC, MAX_TREE_SIZE + 1);

	full_path = kmalloc(unc_len + pplen + 1, GFP_KERNEL);
	if (full_path == NULL)
		return ERR_PTR(-ENOMEM);

	strncpy(full_path, vol->UNC, unc_len);
	pos = full_path + unc_len;

	if (pplen) {
		*pos = CIFS_DIR_SEP(cifs_sb);
		strncpy(pos + 1, vol->prepath, pplen);
		pos += pplen;
	}

	*pos = '\0'; /* add trailing null */
	convert_delimiter(full_path, CIFS_DIR_SEP(cifs_sb));
	cifs_dbg(FYI, "%s: full_path=%s\n", __func__, full_path);
	return full_path;
}

/*
 * Perform a dfs referral query for a share and (optionally) prefix
 *
 * If a referral is found, cifs_sb->mountdata will be (re-)allocated
 * to a string containing updated options for the submount.  Otherwise it
 * will be left untouched.
 *
 * Returns the rc from get_dfs_path to the caller, which can be used to
 * determine whether there were referrals.
 */
static int
expand_dfs_referral(const unsigned int xid, struct cifs_ses *ses,
		    struct smb_vol *volume_info, struct cifs_sb_info *cifs_sb,
		    int check_prefix)
{
	int rc;
	unsigned int num_referrals = 0;
	struct dfs_info3_param *referrals = NULL;
	char *full_path = NULL, *ref_path = NULL, *mdata = NULL;

	full_path = build_unc_path_to_root(volume_info, cifs_sb);
	if (IS_ERR(full_path))
		return PTR_ERR(full_path);

	/* For DFS paths, skip the first '\' of the UNC */
	ref_path = check_prefix ? full_path + 1 : volume_info->UNC + 1;

	rc = get_dfs_path(xid, ses, ref_path, cifs_sb->local_nls,
			  &num_referrals, &referrals, cifs_remap(cifs_sb));

	if (!rc && num_referrals > 0) {
		char *fake_devname = NULL;

		mdata = cifs_compose_mount_options(cifs_sb->mountdata,
						   full_path + 1, referrals,
						   &fake_devname);

		free_dfs_info_array(referrals, num_referrals);

		if (IS_ERR(mdata)) {
			rc = PTR_ERR(mdata);
			mdata = NULL;
		} else {
			cleanup_volume_info_contents(volume_info);
			rc = cifs_setup_volume_info(volume_info, mdata,
							fake_devname);
		}
		kfree(fake_devname);
		kfree(cifs_sb->mountdata);
		cifs_sb->mountdata = mdata;
	}
	kfree(full_path);
	return rc;
}
#endif

static int
cifs_setup_volume_info(struct smb_vol *volume_info, char *mount_data,
			const char *devname)
{
	int rc = 0;

	if (cifs_parse_mount_options(mount_data, devname, volume_info))
		return -EINVAL;

	if (volume_info->nullauth) {
		cifs_dbg(FYI, "Anonymous login\n");
		kfree(volume_info->username);
		volume_info->username = NULL;
	} else if (volume_info->username) {
		/* BB fixme parse for domain name here */
		cifs_dbg(FYI, "Username: %s\n", volume_info->username);
	} else {
		cifs_dbg(VFS, "No username specified\n");
	/* In userspace mount helper we can get user name from alternate
	   locations such as env variables and files on disk */
		return -EINVAL;
	}

	/* this is needed for ASCII cp to Unicode converts */
	if (volume_info->iocharset == NULL) {
		/* load_nls_default cannot return null */
		volume_info->local_nls = load_nls_default();
	} else {
		volume_info->local_nls = load_nls(volume_info->iocharset);
		if (volume_info->local_nls == NULL) {
			cifs_dbg(VFS, "CIFS mount error: iocharset %s not found\n",
				 volume_info->iocharset);
			return -ELIBACC;
		}
	}

	return rc;
}

struct smb_vol *
cifs_get_volume_info(char *mount_data, const char *devname)
{
	int rc;
	struct smb_vol *volume_info;

	volume_info = kmalloc(sizeof(struct smb_vol), GFP_KERNEL);
	if (!volume_info)
		return ERR_PTR(-ENOMEM);

	rc = cifs_setup_volume_info(volume_info, mount_data, devname);
	if (rc) {
		cifs_cleanup_volume_info(volume_info);
		volume_info = ERR_PTR(rc);
	}

	return volume_info;
}

static int
cifs_are_all_path_components_accessible(struct TCP_Server_Info *server,
					unsigned int xid,
					struct cifs_tcon *tcon,
					struct cifs_sb_info *cifs_sb,
					char *full_path)
{
	int rc;
	char *s;
	char sep, tmp;

	sep = CIFS_DIR_SEP(cifs_sb);
	s = full_path;

	rc = server->ops->is_path_accessible(xid, tcon, cifs_sb, "");
	while (rc == 0) {
		/* skip separators */
		while (*s == sep)
			s++;
		if (!*s)
			break;
		/* next separator */
		while (*s && *s != sep)
			s++;

		/*
		 * temporarily null-terminate the path at the end of
		 * the current component
		 */
		tmp = *s;
		*s = 0;
		rc = server->ops->is_path_accessible(xid, tcon, cifs_sb,
						     full_path);
		*s = tmp;
	}
	return rc;
}

int
cifs_mount(struct cifs_sb_info *cifs_sb, struct smb_vol *volume_info)
{
	int rc;
	unsigned int xid;
	struct cifs_ses *ses;
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;
	char   *full_path;
	struct tcon_link *tlink;
#ifdef CONFIG_CIFS_DFS_UPCALL
	int referral_walks_count = 0;
#endif

	rc = bdi_setup_and_register(&cifs_sb->bdi, "cifs");
	if (rc)
		return rc;

#ifdef CONFIG_CIFS_DFS_UPCALL
try_mount_again:
	/* cleanup activities if we're chasing a referral */
	if (referral_walks_count) {
		if (tcon)
			cifs_put_tcon(tcon);
		else if (ses)
			cifs_put_smb_ses(ses);

		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_POSIX_PATHS;

		free_xid(xid);
	}
#endif
	rc = 0;
	tcon = NULL;
	ses = NULL;
	server = NULL;
	full_path = NULL;
	tlink = NULL;

	xid = get_xid();

	/* get a reference to a tcp session */
	server = cifs_get_tcp_session(volume_info);
	if (IS_ERR(server)) {
		rc = PTR_ERR(server);
		bdi_destroy(&cifs_sb->bdi);
		goto out;
	}

	/* get a reference to a SMB session */
	ses = cifs_get_smb_ses(server, volume_info);
	if (IS_ERR(ses)) {
		rc = PTR_ERR(ses);
		ses = NULL;
		goto mount_fail_check;
	}

#ifdef CONFIG_CIFS_SMB2
	if ((volume_info->persistent == true) && ((ses->server->capabilities &
		SMB2_GLOBAL_CAP_PERSISTENT_HANDLES) == 0)) {
		cifs_dbg(VFS, "persistent handles not supported by server\n");
		rc = -EOPNOTSUPP;
		goto mount_fail_check;
	}
#endif /* CONFIG_CIFS_SMB2*/

	/* search for existing tcon to this server share */
	tcon = cifs_get_tcon(ses, volume_info);
	if (IS_ERR(tcon)) {
		rc = PTR_ERR(tcon);
		tcon = NULL;
		goto remote_path_check;
	}

	/* tell server which Unix caps we support */
	if (cap_unix(tcon->ses)) {
		/* reset of caps checks mount to see if unix extensions
		   disabled for just this mount */
		reset_cifs_unix_caps(xid, tcon, cifs_sb, volume_info);
		if ((tcon->ses->server->tcpStatus == CifsNeedReconnect) &&
		    (le64_to_cpu(tcon->fsUnixInfo.Capability) &
		     CIFS_UNIX_TRANSPORT_ENCRYPTION_MANDATORY_CAP)) {
			rc = -EACCES;
			goto mount_fail_check;
		}
	} else
		tcon->unix_ext = 0; /* server does not support them */

	/* do not care if a following call succeed - informational */
	if (!tcon->ipc && server->ops->qfs_tcon)
		server->ops->qfs_tcon(xid, tcon);

	cifs_sb->wsize = server->ops->negotiate_wsize(tcon, volume_info);
	cifs_sb->rsize = server->ops->negotiate_rsize(tcon, volume_info);

	/* tune readahead according to rsize */
	cifs_sb->bdi.ra_pages = cifs_sb->rsize / PAGE_CACHE_SIZE;

remote_path_check:
#ifdef CONFIG_CIFS_DFS_UPCALL
	/*
	 * Perform an unconditional check for whether there are DFS
	 * referrals for this path without prefix, to provide support
	 * for DFS referrals from w2k8 servers which don't seem to respond
	 * with PATH_NOT_COVERED to requests that include the prefix.
	 * Chase the referral if found, otherwise continue normally.
	 */
	if (referral_walks_count == 0) {
		int refrc = expand_dfs_referral(xid, ses, volume_info, cifs_sb,
						false);
		if (!refrc) {
			referral_walks_count++;
			goto try_mount_again;
		}
	}
#endif

	/* check if a whole path is not remote */
	if (!rc && tcon) {
		if (!server->ops->is_path_accessible) {
			rc = -ENOSYS;
			goto mount_fail_check;
		}
		/*
		 * cifs_build_path_to_root works only when we have a valid tcon
		 */
		full_path = cifs_build_path_to_root(volume_info, cifs_sb, tcon);
		if (full_path == NULL) {
			rc = -ENOMEM;
			goto mount_fail_check;
		}
		rc = server->ops->is_path_accessible(xid, tcon, cifs_sb,
						     full_path);
		if (rc != 0 && rc != -EREMOTE) {
			kfree(full_path);
			goto mount_fail_check;
		}

		if (rc != -EREMOTE) {
			rc = cifs_are_all_path_components_accessible(server,
							     xid, tcon, cifs_sb,
							     full_path);
			if (rc != 0) {
				cifs_dbg(VFS, "cannot query dirs between root and final path, "
					 "enabling CIFS_MOUNT_USE_PREFIX_PATH\n");
				cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_USE_PREFIX_PATH;
				rc = 0;
			}
		}
		kfree(full_path);
	}

	/* get referral if needed */
	if (rc == -EREMOTE) {
#ifdef CONFIG_CIFS_DFS_UPCALL
		if (referral_walks_count > MAX_NESTED_LINKS) {
			/*
			 * BB: when we implement proper loop detection,
			 *     we will remove this check. But now we need it
			 *     to prevent an indefinite loop if 'DFS tree' is
			 *     misconfigured (i.e. has loops).
			 */
			rc = -ELOOP;
			goto mount_fail_check;
		}

		rc = expand_dfs_referral(xid, ses, volume_info, cifs_sb, true);

		if (!rc) {
			referral_walks_count++;
			goto try_mount_again;
		}
		goto mount_fail_check;
#else /* No DFS support, return error on mount */
		rc = -EOPNOTSUPP;
#endif
	}

	if (rc)
		goto mount_fail_check;

	/* now, hang the tcon off of the superblock */
	tlink = kzalloc(sizeof *tlink, GFP_KERNEL);
	if (tlink == NULL) {
		rc = -ENOMEM;
		goto mount_fail_check;
	}

	tlink->tl_uid = ses->linux_uid;
	tlink->tl_tcon = tcon;
	tlink->tl_time = jiffies;
	set_bit(TCON_LINK_MASTER, &tlink->tl_flags);
	set_bit(TCON_LINK_IN_TREE, &tlink->tl_flags);

	cifs_sb->master_tlink = tlink;
	spin_lock(&cifs_sb->tlink_tree_lock);
	tlink_rb_insert(&cifs_sb->tlink_tree, tlink);
	spin_unlock(&cifs_sb->tlink_tree_lock);

	queue_delayed_work(cifsiod_wq, &cifs_sb->prune_tlinks,
				TLINK_IDLE_EXPIRE);

mount_fail_check:
	/* on error free sesinfo and tcon struct if needed */
	if (rc) {
		/* If find_unc succeeded then rc == 0 so we can not end */
		/* up accidentally freeing someone elses tcon struct */
		if (tcon)
			cifs_put_tcon(tcon);
		else if (ses)
			cifs_put_smb_ses(ses);
		else
			cifs_put_tcp_session(server);
		bdi_destroy(&cifs_sb->bdi);
	}

out:
	free_xid(xid);
	return rc;
}

/*
 * Issue a TREE_CONNECT request. Note that for IPC$ shares, that the tcon
 * pointer may be NULL.
 */
int
CIFSTCon(const unsigned int xid, struct cifs_ses *ses,
	 const char *tree, struct cifs_tcon *tcon,
	 const struct nls_table *nls_codepage)
{
	struct smb_hdr *smb_buffer;
	struct smb_hdr *smb_buffer_response;
	TCONX_REQ *pSMB;
	TCONX_RSP *pSMBr;
	unsigned char *bcc_ptr;
	int rc = 0;
	int length;
	__u16 bytes_left, count;

	if (ses == NULL)
		return -EIO;

	smb_buffer = cifs_buf_get();
	if (smb_buffer == NULL)
		return -ENOMEM;

	smb_buffer_response = smb_buffer;

	header_assemble(smb_buffer, SMB_COM_TREE_CONNECT_ANDX,
			NULL /*no tid */ , 4 /*wct */ );

	smb_buffer->Mid = get_next_mid(ses->server);
	smb_buffer->Uid = ses->Suid;
	pSMB = (TCONX_REQ *) smb_buffer;
	pSMBr = (TCONX_RSP *) smb_buffer_response;

	pSMB->AndXCommand = 0xFF;
	pSMB->Flags = cpu_to_le16(TCON_EXTENDED_SECINFO);
	bcc_ptr = &pSMB->Password[0];
	if (!tcon || (ses->server->sec_mode & SECMODE_USER)) {
		pSMB->PasswordLength = cpu_to_le16(1);	/* minimum */
		*bcc_ptr = 0; /* password is null byte */
		bcc_ptr++;              /* skip password */
		/* already aligned so no need to do it below */
	} else {
		pSMB->PasswordLength = cpu_to_le16(CIFS_AUTH_RESP_SIZE);
		/* BB FIXME add code to fail this if NTLMv2 or Kerberos
		   specified as required (when that support is added to
		   the vfs in the future) as only NTLM or the much
		   weaker LANMAN (which we do not send by default) is accepted
		   by Samba (not sure whether other servers allow
		   NTLMv2 password here) */
#ifdef CONFIG_CIFS_WEAK_PW_HASH
		if ((global_secflags & CIFSSEC_MAY_LANMAN) &&
		    (ses->sectype == LANMAN))
			calc_lanman_hash(tcon->password, ses->server->cryptkey,
					 ses->server->sec_mode &
					    SECMODE_PW_ENCRYPT ? true : false,
					 bcc_ptr);
		else
#endif /* CIFS_WEAK_PW_HASH */
		rc = SMBNTencrypt(tcon->password, ses->server->cryptkey,
					bcc_ptr, nls_codepage);
		if (rc) {
			cifs_dbg(FYI, "%s Can't generate NTLM rsp. Error: %d\n",
				 __func__, rc);
			cifs_buf_release(smb_buffer);
			return rc;
		}

		bcc_ptr += CIFS_AUTH_RESP_SIZE;
		if (ses->capabilities & CAP_UNICODE) {
			/* must align unicode strings */
			*bcc_ptr = 0; /* null byte password */
			bcc_ptr++;
		}
	}

	if (ses->server->sign)
		smb_buffer->Flags2 |= SMBFLG2_SECURITY_SIGNATURE;

	if (ses->capabilities & CAP_STATUS32) {
		smb_buffer->Flags2 |= SMBFLG2_ERR_STATUS;
	}
	if (ses->capabilities & CAP_DFS) {
		smb_buffer->Flags2 |= SMBFLG2_DFS;
	}
	if (ses->capabilities & CAP_UNICODE) {
		smb_buffer->Flags2 |= SMBFLG2_UNICODE;
		length =
		    cifs_strtoUTF16((__le16 *) bcc_ptr, tree,
			6 /* max utf8 char length in bytes */ *
			(/* server len*/ + 256 /* share len */), nls_codepage);
		bcc_ptr += 2 * length;	/* convert num 16 bit words to bytes */
		bcc_ptr += 2;	/* skip trailing null */
	} else {		/* ASCII */
		strcpy(bcc_ptr, tree);
		bcc_ptr += strlen(tree) + 1;
	}
	strcpy(bcc_ptr, "?????");
	bcc_ptr += strlen("?????");
	bcc_ptr += 1;
	count = bcc_ptr - &pSMB->Password[0];
	pSMB->hdr.smb_buf_length = cpu_to_be32(be32_to_cpu(
					pSMB->hdr.smb_buf_length) + count);
	pSMB->ByteCount = cpu_to_le16(count);

	rc = SendReceive(xid, ses, smb_buffer, smb_buffer_response, &length,
			 0);

	/* above now done in SendReceive */
	if ((rc == 0) && (tcon != NULL)) {
		bool is_unicode;

		tcon->tidStatus = CifsGood;
		tcon->need_reconnect = false;
		tcon->tid = smb_buffer_response->Tid;
		bcc_ptr = pByteArea(smb_buffer_response);
		bytes_left = get_bcc(smb_buffer_response);
		length = strnlen(bcc_ptr, bytes_left - 2);
		if (smb_buffer->Flags2 & SMBFLG2_UNICODE)
			is_unicode = true;
		else
			is_unicode = false;


		/* skip service field (NB: this field is always ASCII) */
		if (length == 3) {
			if ((bcc_ptr[0] == 'I') && (bcc_ptr[1] == 'P') &&
			    (bcc_ptr[2] == 'C')) {
				cifs_dbg(FYI, "IPC connection\n");
				tcon->ipc = 1;
			}
		} else if (length == 2) {
			if ((bcc_ptr[0] == 'A') && (bcc_ptr[1] == ':')) {
				/* the most common case */
				cifs_dbg(FYI, "disk share connection\n");
			}
		}
		bcc_ptr += length + 1;
		bytes_left -= (length + 1);
		strlcpy(tcon->treeName, tree, sizeof(tcon->treeName));

		/* mostly informational -- no need to fail on error here */
		kfree(tcon->nativeFileSystem);
		tcon->nativeFileSystem = cifs_strndup_from_utf16(bcc_ptr,
						      bytes_left, is_unicode,
						      nls_codepage);

		cifs_dbg(FYI, "nativeFileSystem=%s\n", tcon->nativeFileSystem);

		if ((smb_buffer_response->WordCount == 3) ||
			 (smb_buffer_response->WordCount == 7))
			/* field is in same location */
			tcon->Flags = le16_to_cpu(pSMBr->OptionalSupport);
		else
			tcon->Flags = 0;
		cifs_dbg(FYI, "Tcon flags: 0x%x\n", tcon->Flags);
	} else if ((rc == 0) && tcon == NULL) {
		/* all we need to save for IPC$ connection */
		ses->ipc_tid = smb_buffer_response->Tid;
	}

	cifs_buf_release(smb_buffer);
	return rc;
}

static void delayed_free(struct rcu_head *p)
{
	struct cifs_sb_info *sbi = container_of(p, struct cifs_sb_info, rcu);
	unload_nls(sbi->local_nls);
	kfree(sbi);
}

void
cifs_umount(struct cifs_sb_info *cifs_sb)
{
	struct rb_root *root = &cifs_sb->tlink_tree;
	struct rb_node *node;
	struct tcon_link *tlink;

	cancel_delayed_work_sync(&cifs_sb->prune_tlinks);

	spin_lock(&cifs_sb->tlink_tree_lock);
	while ((node = rb_first(root))) {
		tlink = rb_entry(node, struct tcon_link, tl_rbnode);
		cifs_get_tlink(tlink);
		clear_bit(TCON_LINK_IN_TREE, &tlink->tl_flags);
		rb_erase(node, root);

		spin_unlock(&cifs_sb->tlink_tree_lock);
		cifs_put_tlink(tlink);
		spin_lock(&cifs_sb->tlink_tree_lock);
	}
	spin_unlock(&cifs_sb->tlink_tree_lock);

	bdi_destroy(&cifs_sb->bdi);
	kfree(cifs_sb->mountdata);
	kfree(cifs_sb->prepath);
	call_rcu(&cifs_sb->rcu, delayed_free);
}

int
cifs_negotiate_protocol(const unsigned int xid, struct cifs_ses *ses)
{
	int rc = 0;
	struct TCP_Server_Info *server = ses->server;

	if (!server->ops->need_neg || !server->ops->negotiate)
		return -ENOSYS;

	/* only send once per connect */
	if (!server->ops->need_neg(server))
		return 0;

	set_credits(server, 1);

	rc = server->ops->negotiate(xid, ses);
	if (rc == 0) {
		spin_lock(&GlobalMid_Lock);
		if (server->tcpStatus == CifsNeedNegotiate)
			server->tcpStatus = CifsGood;
		else
			rc = -EHOSTDOWN;
		spin_unlock(&GlobalMid_Lock);
	}

	return rc;
}

int
cifs_setup_session(const unsigned int xid, struct cifs_ses *ses,
		   struct nls_table *nls_info)
{
	int rc = -ENOSYS;
	struct TCP_Server_Info *server = ses->server;

	ses->capabilities = server->capabilities;
	if (linuxExtEnabled == 0)
		ses->capabilities &= (~server->vals->cap_unix);

	cifs_dbg(FYI, "Security Mode: 0x%x Capabilities: 0x%x TimeAdjust: %d\n",
		 server->sec_mode, server->capabilities, server->timeAdj);

	if (server->ops->sess_setup)
		rc = server->ops->sess_setup(xid, ses, nls_info);

	if (rc)
		cifs_dbg(VFS, "Send error in SessSetup = %d\n", rc);

	return rc;
}

static int
cifs_set_vol_auth(struct smb_vol *vol, struct cifs_ses *ses)
{
	vol->sectype = ses->sectype;

	/* krb5 is special, since we don't need username or pw */
	if (vol->sectype == Kerberos)
		return 0;

	return cifs_set_cifscreds(vol, ses);
}

static struct cifs_tcon *
cifs_construct_tcon(struct cifs_sb_info *cifs_sb, kuid_t fsuid)
{
	int rc;
	struct cifs_tcon *master_tcon = cifs_sb_master_tcon(cifs_sb);
	struct cifs_ses *ses;
	struct cifs_tcon *tcon = NULL;
	struct smb_vol *vol_info;

	vol_info = kzalloc(sizeof(*vol_info), GFP_KERNEL);
	if (vol_info == NULL)
		return ERR_PTR(-ENOMEM);

	vol_info->local_nls = cifs_sb->local_nls;
	vol_info->linux_uid = fsuid;
	vol_info->cred_uid = fsuid;
	vol_info->UNC = master_tcon->treeName;
	vol_info->retry = master_tcon->retry;
	vol_info->nocase = master_tcon->nocase;
	vol_info->local_lease = master_tcon->local_lease;
	vol_info->no_linux_ext = !master_tcon->unix_ext;
	vol_info->sectype = master_tcon->ses->sectype;
	vol_info->sign = master_tcon->ses->sign;

	rc = cifs_set_vol_auth(vol_info, master_tcon->ses);
	if (rc) {
		tcon = ERR_PTR(rc);
		goto out;
	}

	/* get a reference for the same TCP session */
	spin_lock(&cifs_tcp_ses_lock);
	++master_tcon->ses->server->srv_count;
	spin_unlock(&cifs_tcp_ses_lock);

	ses = cifs_get_smb_ses(master_tcon->ses->server, vol_info);
	if (IS_ERR(ses)) {
		tcon = (struct cifs_tcon *)ses;
		cifs_put_tcp_session(master_tcon->ses->server);
		goto out;
	}

	tcon = cifs_get_tcon(ses, vol_info);
	if (IS_ERR(tcon)) {
		cifs_put_smb_ses(ses);
		goto out;
	}

	if (cap_unix(ses))
		reset_cifs_unix_caps(0, tcon, NULL, vol_info);
out:
	kfree(vol_info->username);
	kfree(vol_info->password);
	kfree(vol_info);

	return tcon;
}

struct cifs_tcon *
cifs_sb_master_tcon(struct cifs_sb_info *cifs_sb)
{
	return tlink_tcon(cifs_sb_master_tlink(cifs_sb));
}

/* find and return a tlink with given uid */
static struct tcon_link *
tlink_rb_search(struct rb_root *root, kuid_t uid)
{
	struct rb_node *node = root->rb_node;
	struct tcon_link *tlink;

	while (node) {
		tlink = rb_entry(node, struct tcon_link, tl_rbnode);

		if (uid_gt(tlink->tl_uid, uid))
			node = node->rb_left;
		else if (uid_lt(tlink->tl_uid, uid))
			node = node->rb_right;
		else
			return tlink;
	}
	return NULL;
}

/* insert a tcon_link into the tree */
static void
tlink_rb_insert(struct rb_root *root, struct tcon_link *new_tlink)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;
	struct tcon_link *tlink;

	while (*new) {
		tlink = rb_entry(*new, struct tcon_link, tl_rbnode);
		parent = *new;

		if (uid_gt(tlink->tl_uid, new_tlink->tl_uid))
			new = &((*new)->rb_left);
		else
			new = &((*new)->rb_right);
	}

	rb_link_node(&new_tlink->tl_rbnode, parent, new);
	rb_insert_color(&new_tlink->tl_rbnode, root);
}

/*
 * Find or construct an appropriate tcon given a cifs_sb and the fsuid of the
 * current task.
 *
 * If the superblock doesn't refer to a multiuser mount, then just return
 * the master tcon for the mount.
 *
 * First, search the rbtree for an existing tcon for this fsuid. If one
 * exists, then check to see if it's pending construction. If it is then wait
 * for construction to complete. Once it's no longer pending, check to see if
 * it failed and either return an error or retry construction, depending on
 * the timeout.
 *
 * If one doesn't exist then insert a new tcon_link struct into the tree and
 * try to construct a new one.
 */
struct tcon_link *
cifs_sb_tlink(struct cifs_sb_info *cifs_sb)
{
	int ret;
	kuid_t fsuid = current_fsuid();
	struct tcon_link *tlink, *newtlink;

	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MULTIUSER))
		return cifs_get_tlink(cifs_sb_master_tlink(cifs_sb));

	spin_lock(&cifs_sb->tlink_tree_lock);
	tlink = tlink_rb_search(&cifs_sb->tlink_tree, fsuid);
	if (tlink)
		cifs_get_tlink(tlink);
	spin_unlock(&cifs_sb->tlink_tree_lock);

	if (tlink == NULL) {
		newtlink = kzalloc(sizeof(*tlink), GFP_KERNEL);
		if (newtlink == NULL)
			return ERR_PTR(-ENOMEM);
		newtlink->tl_uid = fsuid;
		newtlink->tl_tcon = ERR_PTR(-EACCES);
		set_bit(TCON_LINK_PENDING, &newtlink->tl_flags);
		set_bit(TCON_LINK_IN_TREE, &newtlink->tl_flags);
		cifs_get_tlink(newtlink);

		spin_lock(&cifs_sb->tlink_tree_lock);
		/* was one inserted after previous search? */
		tlink = tlink_rb_search(&cifs_sb->tlink_tree, fsuid);
		if (tlink) {
			cifs_get_tlink(tlink);
			spin_unlock(&cifs_sb->tlink_tree_lock);
			kfree(newtlink);
			goto wait_for_construction;
		}
		tlink = newtlink;
		tlink_rb_insert(&cifs_sb->tlink_tree, tlink);
		spin_unlock(&cifs_sb->tlink_tree_lock);
	} else {
wait_for_construction:
		ret = wait_on_bit(&tlink->tl_flags, TCON_LINK_PENDING,
				  TASK_INTERRUPTIBLE);
		if (ret) {
			cifs_put_tlink(tlink);
			return ERR_PTR(-ERESTARTSYS);
		}

		/* if it's good, return it */
		if (!IS_ERR(tlink->tl_tcon))
			return tlink;

		/* return error if we tried this already recently */
		if (time_before(jiffies, tlink->tl_time + TLINK_ERROR_EXPIRE)) {
			cifs_put_tlink(tlink);
			return ERR_PTR(-EACCES);
		}

		if (test_and_set_bit(TCON_LINK_PENDING, &tlink->tl_flags))
			goto wait_for_construction;
	}

	tlink->tl_tcon = cifs_construct_tcon(cifs_sb, fsuid);
	clear_bit(TCON_LINK_PENDING, &tlink->tl_flags);
	wake_up_bit(&tlink->tl_flags, TCON_LINK_PENDING);

	if (IS_ERR(tlink->tl_tcon)) {
		cifs_put_tlink(tlink);
		return ERR_PTR(-EACCES);
	}

	return tlink;
}

/*
 * periodic workqueue job that scans tcon_tree for a superblock and closes
 * out tcons.
 */
static void
cifs_prune_tlinks(struct work_struct *work)
{
	struct cifs_sb_info *cifs_sb = container_of(work, struct cifs_sb_info,
						    prune_tlinks.work);
	struct rb_root *root = &cifs_sb->tlink_tree;
	struct rb_node *node = rb_first(root);
	struct rb_node *tmp;
	struct tcon_link *tlink;

	/*
	 * Because we drop the spinlock in the loop in order to put the tlink
	 * it's not guarded against removal of links from the tree. The only
	 * places that remove entries from the tree are this function and
	 * umounts. Because this function is non-reentrant and is canceled
	 * before umount can proceed, this is safe.
	 */
	spin_lock(&cifs_sb->tlink_tree_lock);
	node = rb_first(root);
	while (node != NULL) {
		tmp = node;
		node = rb_next(tmp);
		tlink = rb_entry(tmp, struct tcon_link, tl_rbnode);

		if (test_bit(TCON_LINK_MASTER, &tlink->tl_flags) ||
		    atomic_read(&tlink->tl_count) != 0 ||
		    time_after(tlink->tl_time + TLINK_IDLE_EXPIRE, jiffies))
			continue;

		cifs_get_tlink(tlink);
		clear_bit(TCON_LINK_IN_TREE, &tlink->tl_flags);
		rb_erase(tmp, root);

		spin_unlock(&cifs_sb->tlink_tree_lock);
		cifs_put_tlink(tlink);
		spin_lock(&cifs_sb->tlink_tree_lock);
	}
	spin_unlock(&cifs_sb->tlink_tree_lock);

	queue_delayed_work(cifsiod_wq, &cifs_sb->prune_tlinks,
				TLINK_IDLE_EXPIRE);
}
