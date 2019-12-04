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
#include <linux/sched/signal.h>
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
#include <linux/uuid.h>
#include <linux/uaccess.h>
#include <asm/processor.h>
#include <linux/inet.h>
#include <linux/module.h>
#include <keys/user-type.h>
#include <net/ipv6.h>
#include <linux/parser.h>
#include <linux/bvec.h>
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
#include "smb2proto.h"
#include "smbdirect.h"
#include "dns_resolve.h"
#include "cifsfs.h"
#ifdef CONFIG_CIFS_DFS_UPCALL
#include "dfs_cache.h"
#endif

extern mempool_t *cifs_req_poolp;
extern bool disable_legacy_dialects;

/* FIXME: should these be tunable? */
#define TLINK_ERROR_EXPIRE	(1 * HZ)
#define TLINK_IDLE_EXPIRE	(600 * HZ)

enum {
	/* Mount options that take no arguments */
	Opt_user_xattr, Opt_nouser_xattr,
	Opt_forceuid, Opt_noforceuid,
	Opt_forcegid, Opt_noforcegid,
	Opt_noblocksend, Opt_noautotune, Opt_nolease,
	Opt_hard, Opt_soft, Opt_perm, Opt_noperm,
	Opt_mapposix, Opt_nomapposix,
	Opt_mapchars, Opt_nomapchars, Opt_sfu,
	Opt_nosfu, Opt_nodfs, Opt_posixpaths,
	Opt_noposixpaths, Opt_nounix, Opt_unix,
	Opt_nocase,
	Opt_brl, Opt_nobrl,
	Opt_handlecache, Opt_nohandlecache,
	Opt_forcemandatorylock, Opt_setuidfromacl, Opt_setuids,
	Opt_nosetuids, Opt_dynperm, Opt_nodynperm,
	Opt_nohard, Opt_nosoft,
	Opt_nointr, Opt_intr,
	Opt_nostrictsync, Opt_strictsync,
	Opt_serverino, Opt_noserverino,
	Opt_rwpidforward, Opt_cifsacl, Opt_nocifsacl,
	Opt_acl, Opt_noacl, Opt_locallease,
	Opt_sign, Opt_ignore_signature, Opt_seal, Opt_noac,
	Opt_fsc, Opt_mfsymlinks,
	Opt_multiuser, Opt_sloppy, Opt_nosharesock,
	Opt_persistent, Opt_nopersistent,
	Opt_resilient, Opt_noresilient,
	Opt_domainauto, Opt_rdma, Opt_modesid, Opt_rootfs,
	Opt_multichannel, Opt_nomultichannel,
	Opt_compress,

	/* Mount options which take numeric value */
	Opt_backupuid, Opt_backupgid, Opt_uid,
	Opt_cruid, Opt_gid, Opt_file_mode,
	Opt_dirmode, Opt_port,
	Opt_min_enc_offload,
	Opt_blocksize, Opt_rsize, Opt_wsize, Opt_actimeo,
	Opt_echo_interval, Opt_max_credits, Opt_handletimeout,
	Opt_snapshot, Opt_max_channels,

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
	{ Opt_nolease, "nolease" },
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
	{ Opt_nounix, "noposix" },
	{ Opt_unix, "unix" },
	{ Opt_unix, "linux" },
	{ Opt_unix, "posix" },
	{ Opt_nocase, "nocase" },
	{ Opt_nocase, "ignorecase" },
	{ Opt_brl, "brl" },
	{ Opt_nobrl, "nobrl" },
	{ Opt_handlecache, "handlecache" },
	{ Opt_nohandlecache, "nohandlecache" },
	{ Opt_nobrl, "nolock" },
	{ Opt_forcemandatorylock, "forcemandatorylock" },
	{ Opt_forcemandatorylock, "forcemand" },
	{ Opt_setuids, "setuids" },
	{ Opt_nosetuids, "nosetuids" },
	{ Opt_setuidfromacl, "idsfromsid" },
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
	{ Opt_modesid, "modefromsid" },
	{ Opt_cifsacl, "cifsacl" },
	{ Opt_nocifsacl, "nocifsacl" },
	{ Opt_acl, "acl" },
	{ Opt_noacl, "noacl" },
	{ Opt_locallease, "locallease" },
	{ Opt_sign, "sign" },
	{ Opt_ignore_signature, "signloosely" },
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
	{ Opt_domainauto, "domainauto"},
	{ Opt_rdma, "rdma"},
	{ Opt_multichannel, "multichannel" },
	{ Opt_nomultichannel, "nomultichannel" },

	{ Opt_backupuid, "backupuid=%s" },
	{ Opt_backupgid, "backupgid=%s" },
	{ Opt_uid, "uid=%s" },
	{ Opt_cruid, "cruid=%s" },
	{ Opt_gid, "gid=%s" },
	{ Opt_file_mode, "file_mode=%s" },
	{ Opt_dirmode, "dirmode=%s" },
	{ Opt_dirmode, "dir_mode=%s" },
	{ Opt_port, "port=%s" },
	{ Opt_min_enc_offload, "esize=%s" },
	{ Opt_blocksize, "bsize=%s" },
	{ Opt_rsize, "rsize=%s" },
	{ Opt_wsize, "wsize=%s" },
	{ Opt_actimeo, "actimeo=%s" },
	{ Opt_handletimeout, "handletimeout=%s" },
	{ Opt_echo_interval, "echo_interval=%s" },
	{ Opt_max_credits, "max_credits=%s" },
	{ Opt_snapshot, "snapshot=%s" },
	{ Opt_max_channels, "max_channels=%s" },
	{ Opt_compress, "compress=%s" },

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
	{ Opt_ignore, "relatime" },
	{ Opt_ignore, "_netdev" },
	{ Opt_rootfs, "rootfs" },

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
	Opt_cache_ro,
	Opt_cache_rw,
	Opt_cache_err
};

static const match_table_t cifs_cacheflavor_tokens = {
	{ Opt_cache_loose, "loose" },
	{ Opt_cache_strict, "strict" },
	{ Opt_cache_none, "none" },
	{ Opt_cache_ro, "ro" },
	{ Opt_cache_rw, "singleclient" },
	{ Opt_cache_err, NULL }
};

static const match_table_t cifs_smb_version_tokens = {
	{ Smb_1, SMB1_VERSION_STRING },
	{ Smb_20, SMB20_VERSION_STRING},
	{ Smb_21, SMB21_VERSION_STRING },
	{ Smb_30, SMB30_VERSION_STRING },
	{ Smb_302, SMB302_VERSION_STRING },
	{ Smb_302, ALT_SMB302_VERSION_STRING },
	{ Smb_311, SMB311_VERSION_STRING },
	{ Smb_311, ALT_SMB311_VERSION_STRING },
	{ Smb_3any, SMB3ANY_VERSION_STRING },
	{ Smb_default, SMBDEFAULT_VERSION_STRING },
	{ Smb_version_err, NULL }
};

static int ip_connect(struct TCP_Server_Info *server);
static int generic_ip_connect(struct TCP_Server_Info *server);
static void tlink_rb_insert(struct rb_root *root, struct tcon_link *new_tlink);
static void cifs_prune_tlinks(struct work_struct *work);
static char *extract_hostname(const char *unc);

/*
 * Resolve hostname and set ip addr in tcp ses. Useful for hostnames that may
 * get their ip addresses changed at some point.
 *
 * This should be called with server->srv_mutex held.
 */
#ifdef CONFIG_CIFS_DFS_UPCALL
static int reconn_set_ipaddr(struct TCP_Server_Info *server)
{
	int rc;
	int len;
	char *unc, *ipaddr = NULL;

	if (!server->hostname)
		return -EINVAL;

	len = strlen(server->hostname) + 3;

	unc = kmalloc(len, GFP_KERNEL);
	if (!unc) {
		cifs_dbg(FYI, "%s: failed to create UNC path\n", __func__);
		return -ENOMEM;
	}
	scnprintf(unc, len, "\\\\%s", server->hostname);

	rc = dns_resolve_server_name_to_ip(unc, &ipaddr);
	kfree(unc);

	if (rc < 0) {
		cifs_dbg(FYI, "%s: failed to resolve server part of %s to IP: %d\n",
			 __func__, server->hostname, rc);
		return rc;
	}

	rc = cifs_convert_address((struct sockaddr *)&server->dstaddr, ipaddr,
				  strlen(ipaddr));
	kfree(ipaddr);

	return !rc ? -1 : 0;
}
#else
static inline int reconn_set_ipaddr(struct TCP_Server_Info *server)
{
	return 0;
}
#endif

#ifdef CONFIG_CIFS_DFS_UPCALL
struct super_cb_data {
	struct TCP_Server_Info *server;
	struct super_block *sb;
};

/* These functions must be called with server->srv_mutex held */

static void super_cb(struct super_block *sb, void *arg)
{
	struct super_cb_data *d = arg;
	struct cifs_sb_info *cifs_sb;
	struct cifs_tcon *tcon;

	if (d->sb)
		return;

	cifs_sb = CIFS_SB(sb);
	tcon = cifs_sb_master_tcon(cifs_sb);
	if (tcon->ses->server == d->server)
		d->sb = sb;
}

static struct super_block *get_tcp_super(struct TCP_Server_Info *server)
{
	struct super_cb_data d = {
		.server = server,
		.sb = NULL,
	};

	iterate_supers_type(&cifs_fs_type, super_cb, &d);

	if (unlikely(!d.sb))
		return ERR_PTR(-ENOENT);
	/*
	 * Grab an active reference in order to prevent automounts (DFS links)
	 * of expiring and then freeing up our cifs superblock pointer while
	 * we're doing failover.
	 */
	cifs_sb_active(d.sb);
	return d.sb;
}

static inline void put_tcp_super(struct super_block *sb)
{
	if (!IS_ERR_OR_NULL(sb))
		cifs_sb_deactive(sb);
}

static void reconn_inval_dfs_target(struct TCP_Server_Info *server,
				    struct cifs_sb_info *cifs_sb,
				    struct dfs_cache_tgt_list *tgt_list,
				    struct dfs_cache_tgt_iterator **tgt_it)
{
	const char *name;

	if (!cifs_sb || !cifs_sb->origin_fullpath || !tgt_list ||
	    !server->nr_targets)
		return;

	if (!*tgt_it) {
		*tgt_it = dfs_cache_get_tgt_iterator(tgt_list);
	} else {
		*tgt_it = dfs_cache_get_next_tgt(tgt_list, *tgt_it);
		if (!*tgt_it)
			*tgt_it = dfs_cache_get_tgt_iterator(tgt_list);
	}

	cifs_dbg(FYI, "%s: UNC: %s\n", __func__, cifs_sb->origin_fullpath);

	name = dfs_cache_get_tgt_name(*tgt_it);

	kfree(server->hostname);

	server->hostname = extract_hostname(name);
	if (IS_ERR(server->hostname)) {
		cifs_dbg(FYI,
			 "%s: failed to extract hostname from target: %ld\n",
			 __func__, PTR_ERR(server->hostname));
	}
}

static inline int reconn_setup_dfs_targets(struct cifs_sb_info *cifs_sb,
					   struct dfs_cache_tgt_list *tl,
					   struct dfs_cache_tgt_iterator **it)
{
	if (!cifs_sb->origin_fullpath)
		return -EOPNOTSUPP;
	return dfs_cache_noreq_find(cifs_sb->origin_fullpath + 1, NULL, tl);
}
#endif

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
#ifdef CONFIG_CIFS_DFS_UPCALL
	struct super_block *sb = NULL;
	struct cifs_sb_info *cifs_sb = NULL;
	struct dfs_cache_tgt_list tgt_list = {0};
	struct dfs_cache_tgt_iterator *tgt_it = NULL;
#endif

	spin_lock(&GlobalMid_Lock);
	server->nr_targets = 1;
#ifdef CONFIG_CIFS_DFS_UPCALL
	spin_unlock(&GlobalMid_Lock);
	sb = get_tcp_super(server);
	if (IS_ERR(sb)) {
		rc = PTR_ERR(sb);
		cifs_dbg(FYI, "%s: will not do DFS failover: rc = %d\n",
			 __func__, rc);
		sb = NULL;
	} else {
		cifs_sb = CIFS_SB(sb);

		rc = reconn_setup_dfs_targets(cifs_sb, &tgt_list, &tgt_it);
		if (rc && (rc != -EOPNOTSUPP)) {
			cifs_server_dbg(VFS, "%s: no target servers for DFS failover\n",
				 __func__);
		} else {
			server->nr_targets = dfs_cache_get_nr_tgts(&tgt_list);
		}
	}
	cifs_dbg(FYI, "%s: will retry %d target(s)\n", __func__,
		 server->nr_targets);
	spin_lock(&GlobalMid_Lock);
#endif
	if (server->tcpStatus == CifsExiting) {
		/* the demux thread will exit normally
		next time through the loop */
		spin_unlock(&GlobalMid_Lock);
#ifdef CONFIG_CIFS_DFS_UPCALL
		dfs_cache_free_tgts(&tgt_list);
		put_tcp_super(sb);
#endif
		return rc;
	} else
		server->tcpStatus = CifsNeedReconnect;
	spin_unlock(&GlobalMid_Lock);
	server->maxBuf = 0;
	server->max_read = 0;

	cifs_dbg(FYI, "Mark tcp session as need reconnect\n");
	trace_smb3_reconnect(server->CurrentMid, server->hostname);

	/* before reconnecting the tcp session, mark the smb session (uid)
		and the tid bad so they are not used until reconnected */
	cifs_dbg(FYI, "%s: marking sessions and tcons for reconnect\n",
		 __func__);
	spin_lock(&cifs_tcp_ses_lock);
	list_for_each(tmp, &server->smb_ses_list) {
		ses = list_entry(tmp, struct cifs_ses, smb_ses_list);
		ses->need_reconnect = true;
		list_for_each(tmp2, &ses->tcon_list) {
			tcon = list_entry(tmp2, struct cifs_tcon, tcon_list);
			tcon->need_reconnect = true;
		}
		if (ses->tcon_ipc)
			ses->tcon_ipc->need_reconnect = true;
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
		kref_get(&mid_entry->refcount);
		if (mid_entry->mid_state == MID_REQUEST_SUBMITTED)
			mid_entry->mid_state = MID_RETRY_NEEDED;
		list_move(&mid_entry->qhead, &retry_list);
		mid_entry->mid_flags |= MID_DELETED;
	}
	spin_unlock(&GlobalMid_Lock);
	mutex_unlock(&server->srv_mutex);

	cifs_dbg(FYI, "%s: issuing mid callbacks\n", __func__);
	list_for_each_safe(tmp, tmp2, &retry_list) {
		mid_entry = list_entry(tmp, struct mid_q_entry, qhead);
		list_del_init(&mid_entry->qhead);
		mid_entry->callback(mid_entry);
		cifs_mid_q_entry_release(mid_entry);
	}

	if (cifs_rdma_enabled(server)) {
		mutex_lock(&server->srv_mutex);
		smbd_destroy(server);
		mutex_unlock(&server->srv_mutex);
	}

	do {
		try_to_freeze();

		mutex_lock(&server->srv_mutex);
		/*
		 * Set up next DFS target server (if any) for reconnect. If DFS
		 * feature is disabled, then we will retry last server we
		 * connected to before.
		 */
		if (cifs_rdma_enabled(server))
			rc = smbd_reconnect(server);
		else
			rc = generic_ip_connect(server);
		if (rc) {
			cifs_dbg(FYI, "reconnect error %d\n", rc);
#ifdef CONFIG_CIFS_DFS_UPCALL
			reconn_inval_dfs_target(server, cifs_sb, &tgt_list,
						&tgt_it);
#endif
			rc = reconn_set_ipaddr(server);
			if (rc) {
				cifs_dbg(FYI, "%s: failed to resolve hostname: %d\n",
					 __func__, rc);
			}
			mutex_unlock(&server->srv_mutex);
			msleep(3000);
		} else {
			atomic_inc(&tcpSesReconnectCount);
			set_credits(server, 1);
			spin_lock(&GlobalMid_Lock);
			if (server->tcpStatus != CifsExiting)
				server->tcpStatus = CifsNeedNegotiate;
			spin_unlock(&GlobalMid_Lock);
			mutex_unlock(&server->srv_mutex);
		}
	} while (server->tcpStatus == CifsNeedReconnect);

#ifdef CONFIG_CIFS_DFS_UPCALL
	if (tgt_it) {
		rc = dfs_cache_noreq_update_tgthint(cifs_sb->origin_fullpath + 1,
						    tgt_it);
		if (rc) {
			cifs_server_dbg(VFS, "%s: failed to update DFS target hint: rc = %d\n",
				 __func__, rc);
		}
		rc = dfs_cache_update_vol(cifs_sb->origin_fullpath, server);
		if (rc) {
			cifs_server_dbg(VFS, "%s: failed to update vol info in DFS cache: rc = %d\n",
				 __func__, rc);
		}
		dfs_cache_free_tgts(&tgt_list);

	}

	put_tcp_super(sb);
#endif
	if (server->tcpStatus == CifsNeedNegotiate)
		mod_delayed_work(cifsiod_wq, &server->echo, 0);

	return rc;
}

static void
cifs_echo_request(struct work_struct *work)
{
	int rc;
	struct TCP_Server_Info *server = container_of(work,
					struct TCP_Server_Info, echo.work);
	unsigned long echo_interval;

	/*
	 * If we need to renegotiate, set echo interval to zero to
	 * immediately call echo service where we can renegotiate.
	 */
	if (server->tcpStatus == CifsNeedNegotiate)
		echo_interval = 0;
	else
		echo_interval = server->echo_interval;

	/*
	 * We cannot send an echo if it is disabled.
	 * Also, no need to ping if we got a response recently.
	 */

	if (server->tcpStatus == CifsNeedReconnect ||
	    server->tcpStatus == CifsExiting ||
	    server->tcpStatus == CifsNew ||
	    (server->ops->can_echo && !server->ops->can_echo(server)) ||
	    time_before(jiffies, server->lstrp + echo_interval - HZ))
		goto requeue_echo;

	rc = server->ops->echo ? server->ops->echo(server) : -ENOSYS;
	if (rc)
		cifs_dbg(FYI, "Unable to send echo request to server: %s\n",
			 server->hostname);

requeue_echo:
	queue_delayed_work(cifsiod_wq, &server->echo, server->echo_interval);
}

static bool
allocate_buffers(struct TCP_Server_Info *server)
{
	if (!server->bigbuf) {
		server->bigbuf = (char *)cifs_buf_get();
		if (!server->bigbuf) {
			cifs_server_dbg(VFS, "No memory for large SMB response\n");
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
			cifs_server_dbg(VFS, "No memory for SMB response\n");
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
	 * We need to wait 3 echo intervals to make sure we handle such
	 * situations right:
	 * 1s  client sends a normal SMB request
	 * 2s  client gets a response
	 * 30s echo workqueue job pops, and decides we got a response recently
	 *     and don't need to send another
	 * ...
	 * 65s kernel_recvmsg times out, and we see that we haven't gotten
	 *     a response in >60s.
	 */
	if ((server->tcpStatus == CifsGood ||
	    server->tcpStatus == CifsNeedNegotiate) &&
	    time_after(jiffies, server->lstrp + 3 * server->echo_interval)) {
		cifs_server_dbg(VFS, "has not responded in %lu seconds. Reconnecting...\n",
			 (3 * server->echo_interval) / HZ);
		cifs_reconnect(server);
		wake_up(&server->response_q);
		return true;
	}

	return false;
}

static inline bool
zero_credits(struct TCP_Server_Info *server)
{
	int val;

	spin_lock(&server->req_lock);
	val = server->credits + server->echo_credits + server->oplock_credits;
	if (server->in_flight == 0 && val == 0) {
		spin_unlock(&server->req_lock);
		return true;
	}
	spin_unlock(&server->req_lock);
	return false;
}

static int
cifs_readv_from_socket(struct TCP_Server_Info *server, struct msghdr *smb_msg)
{
	int length = 0;
	int total_read;

	smb_msg->msg_control = NULL;
	smb_msg->msg_controllen = 0;

	for (total_read = 0; msg_data_left(smb_msg); total_read += length) {
		try_to_freeze();

		/* reconnect if no credits and no requests in flight */
		if (zero_credits(server)) {
			cifs_reconnect(server);
			return -ECONNABORTED;
		}

		if (server_unresponsive(server))
			return -ECONNABORTED;
		if (cifs_rdma_enabled(server) && server->smbd_conn)
			length = smbd_recv(server->smbd_conn, smb_msg);
		else
			length = sock_recvmsg(server->ssocket, smb_msg, 0);

		if (server->tcpStatus == CifsExiting)
			return -ESHUTDOWN;

		if (server->tcpStatus == CifsNeedReconnect) {
			cifs_reconnect(server);
			return -ECONNABORTED;
		}

		if (length == -ERESTARTSYS ||
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
		}

		if (length <= 0) {
			cifs_dbg(FYI, "Received no data or error: %d\n", length);
			cifs_reconnect(server);
			return -ECONNABORTED;
		}
	}
	return total_read;
}

int
cifs_read_from_socket(struct TCP_Server_Info *server, char *buf,
		      unsigned int to_read)
{
	struct msghdr smb_msg;
	struct kvec iov = {.iov_base = buf, .iov_len = to_read};
	iov_iter_kvec(&smb_msg.msg_iter, READ, &iov, 1, to_read);

	return cifs_readv_from_socket(server, &smb_msg);
}

int
cifs_read_page_from_socket(struct TCP_Server_Info *server, struct page *page,
	unsigned int page_offset, unsigned int to_read)
{
	struct msghdr smb_msg;
	struct bio_vec bv = {
		.bv_page = page, .bv_len = to_read, .bv_offset = page_offset};
	iov_iter_bvec(&smb_msg.msg_iter, READ, &bv, 1, to_read);
	return cifs_readv_from_socket(server, &smb_msg);
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
		cifs_server_dbg(VFS, "RFC 1002 unknown response type 0x%x\n", type);
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
	/*
	 * Trying to handle/dequeue a mid after the send_recv()
	 * function has finished processing it is a bug.
	 */
	if (mid->mid_flags & MID_DELETED)
		printk_once(KERN_WARNING
			    "trying to dequeue a deleted mid\n");
	else {
		list_del_init(&mid->qhead);
		mid->mid_flags |= MID_DELETED;
	}
	spin_unlock(&GlobalMid_Lock);
}

static unsigned int
smb2_get_credits_from_hdr(char *buffer, struct TCP_Server_Info *server)
{
	struct smb2_sync_hdr *shdr = (struct smb2_sync_hdr *)buffer;

	/*
	 * SMB1 does not use credits.
	 */
	if (server->vals->header_preamble_size)
		return 0;

	return le16_to_cpu(shdr->CreditRequest);
}

static void
handle_mid(struct mid_q_entry *mid, struct TCP_Server_Info *server,
	   char *buf, int malformed)
{
	if (server->ops->check_trans2 &&
	    server->ops->check_trans2(mid, server, buf, malformed))
		return;
	mid->credits_received = smb2_get_credits_from_hdr(buf, server);
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
	if (cifs_rdma_enabled(server))
		smbd_destroy(server);
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
			kref_get(&mid_entry->refcount);
			mid_entry->mid_state = MID_SHUTDOWN;
			list_move(&mid_entry->qhead, &dispose_list);
			mid_entry->mid_flags |= MID_DELETED;
		}
		spin_unlock(&GlobalMid_Lock);

		/* now walk dispose list and issue callbacks */
		list_for_each_safe(tmp, tmp2, &dispose_list) {
			mid_entry = list_entry(tmp, struct mid_q_entry, qhead);
			cifs_dbg(FYI, "Callback mid 0x%llx\n", mid_entry->mid);
			list_del_init(&mid_entry->qhead);
			mid_entry->callback(mid_entry);
			cifs_mid_q_entry_release(mid_entry);
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
	unsigned int pdu_length = server->pdu_size;

	/* make sure this will fit in a large buffer */
	if (pdu_length > CIFSMaxBufSize + MAX_HEADER_SIZE(server) -
		server->vals->header_preamble_size) {
		cifs_server_dbg(VFS, "SMB response too long (%u bytes)\n", pdu_length);
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
				       pdu_length - HEADER_SIZE(server) + 1
				       + server->vals->header_preamble_size);

	if (length < 0)
		return length;
	server->total_read += length;

	dump_smb(buf, server->total_read);

	return cifs_handle_standard(server, mid);
}

int
cifs_handle_standard(struct TCP_Server_Info *server, struct mid_q_entry *mid)
{
	char *buf = server->large_buf ? server->bigbuf : server->smallbuf;
	int length;

	/*
	 * We know that we received enough to get to the MID as we
	 * checked the pdu_length earlier. Now check to see
	 * if the rest of the header is OK. We borrow the length
	 * var for the rest of the loop to avoid a new stack var.
	 *
	 * 48 bytes is enough to display the header and a little bit
	 * into the payload for debugging purposes.
	 */
	length = server->ops->check_message(buf, server->total_read, server);
	if (length != 0)
		cifs_dump_mem("Bad SMB: ", buf,
			min_t(unsigned int, server->total_read, 48));

	if (server->ops->is_session_expired &&
	    server->ops->is_session_expired(buf)) {
		cifs_reconnect(server);
		wake_up(&server->response_q);
		return -1;
	}

	if (server->ops->is_status_pending &&
	    server->ops->is_status_pending(buf, server))
		return -1;

	if (!mid)
		return length;

	handle_mid(mid, server, buf, length);
	return 0;
}

static void
smb2_add_credits_from_hdr(char *buffer, struct TCP_Server_Info *server)
{
	struct smb2_sync_hdr *shdr = (struct smb2_sync_hdr *)buffer;

	/*
	 * SMB1 does not use credits.
	 */
	if (server->vals->header_preamble_size)
		return;

	if (shdr->CreditRequest) {
		spin_lock(&server->req_lock);
		server->credits += le16_to_cpu(shdr->CreditRequest);
		spin_unlock(&server->req_lock);
		wake_up(&server->request_q);
	}
}


static int
cifs_demultiplex_thread(void *p)
{
	int i, num_mids, length;
	struct TCP_Server_Info *server = p;
	unsigned int pdu_length;
	unsigned int next_offset;
	char *buf = NULL;
	struct task_struct *task_to_wake = NULL;
	struct mid_q_entry *mids[MAX_COMPOUND];
	char *bufs[MAX_COMPOUND];

	current->flags |= PF_MEMALLOC;
	cifs_dbg(FYI, "Demultiplex PID: %d\n", task_pid_nr(current));

	length = atomic_inc_return(&tcpSesAllocCount);
	if (length > 1)
		mempool_resize(cifs_req_poolp, length + cifs_min_rcv);

	set_freezable();
	allow_kernel_signal(SIGKILL);
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

		if (server->vals->header_preamble_size == 0)
			server->total_read = 0;
		else
			server->total_read = length;

		/*
		 * The right amount was read from socket - 4 bytes,
		 * so we can now interpret the length field.
		 */
		pdu_length = get_rfc1002_length(buf);

		cifs_dbg(FYI, "RFC1002 header 0x%x\n", pdu_length);
		if (!is_smb_response(server, buf[0]))
			continue;
next_pdu:
		server->pdu_size = pdu_length;

		/* make sure we have enough to get to the MID */
		if (server->pdu_size < HEADER_SIZE(server) - 1 -
		    server->vals->header_preamble_size) {
			cifs_server_dbg(VFS, "SMB response too short (%u bytes)\n",
				 server->pdu_size);
			cifs_reconnect(server);
			wake_up(&server->response_q);
			continue;
		}

		/* read down to the MID */
		length = cifs_read_from_socket(server,
			     buf + server->vals->header_preamble_size,
			     HEADER_SIZE(server) - 1
			     - server->vals->header_preamble_size);
		if (length < 0)
			continue;
		server->total_read += length;

		if (server->ops->next_header) {
			next_offset = server->ops->next_header(buf);
			if (next_offset)
				server->pdu_size = next_offset;
		}

		memset(mids, 0, sizeof(mids));
		memset(bufs, 0, sizeof(bufs));
		num_mids = 0;

		if (server->ops->is_transform_hdr &&
		    server->ops->receive_transform &&
		    server->ops->is_transform_hdr(buf)) {
			length = server->ops->receive_transform(server,
								mids,
								bufs,
								&num_mids);
		} else {
			mids[0] = server->ops->find_mid(server, buf);
			bufs[0] = buf;
			num_mids = 1;

			if (!mids[0] || !mids[0]->receive)
				length = standard_receive3(server, mids[0]);
			else
				length = mids[0]->receive(server, mids[0]);
		}

		if (length < 0) {
			for (i = 0; i < num_mids; i++)
				if (mids[i])
					cifs_mid_q_entry_release(mids[i]);
			continue;
		}

		server->lstrp = jiffies;

		for (i = 0; i < num_mids; i++) {
			if (mids[i] != NULL) {
				mids[i]->resp_buf_size = server->pdu_size;

				if (!mids[i]->multiRsp || mids[i]->multiEnd)
					mids[i]->callback(mids[i]);

				cifs_mid_q_entry_release(mids[i]);
			} else if (server->ops->is_oplock_break &&
				   server->ops->is_oplock_break(bufs[i],
								server)) {
				smb2_add_credits_from_hdr(bufs[i], server);
				cifs_dbg(FYI, "Received oplock break\n");
			} else {
				cifs_server_dbg(VFS, "No task to wake, unknown frame "
					 "received! NumMids %d\n",
					 atomic_read(&midCount));
				cifs_dump_mem("Received Data is: ", bufs[i],
					      HEADER_SIZE(server));
				smb2_add_credits_from_hdr(bufs[i], server);
#ifdef CONFIG_CIFS_DEBUG2
				if (server->ops->dump_detail)
					server->ops->dump_detail(bufs[i],
								 server);
				cifs_dump_mids(server);
#endif /* CIFS_DEBUG2 */
			}
		}

		if (pdu_length > server->pdu_size) {
			if (!allocate_buffers(server))
				continue;
			pdu_length -= server->pdu_size;
			server->total_read = 0;
			server->large_buf = false;
			buf = server->smallbuf;
			goto next_pdu;
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
	if (strlen(unc) < 3)
		return ERR_PTR(-EINVAL);
	for (src = unc; *src && *src == '\\'; src++)
		;
	if (!*src)
		return ERR_PTR(-EINVAL);

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
		vol->cache_ro = false;
		vol->cache_rw = false;
		break;
	case Opt_cache_strict:
		vol->direct_io = false;
		vol->strict_io = true;
		vol->cache_ro = false;
		vol->cache_rw = false;
		break;
	case Opt_cache_none:
		vol->direct_io = true;
		vol->strict_io = false;
		vol->cache_ro = false;
		vol->cache_rw = false;
		break;
	case Opt_cache_ro:
		vol->direct_io = false;
		vol->strict_io = false;
		vol->cache_ro = true;
		vol->cache_rw = false;
		break;
	case Opt_cache_rw:
		vol->direct_io = false;
		vol->strict_io = false;
		vol->cache_ro = false;
		vol->cache_rw = true;
		break;
	default:
		cifs_dbg(VFS, "bad cache= option: %s\n", value);
		return 1;
	}
	return 0;
}

static int
cifs_parse_smb_version(char *value, struct smb_vol *vol, bool is_smb3)
{
	substring_t args[MAX_OPT_ARGS];

	switch (match_token(value, cifs_smb_version_tokens, args)) {
#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
	case Smb_1:
		if (disable_legacy_dialects) {
			cifs_dbg(VFS, "mount with legacy dialect disabled\n");
			return 1;
		}
		if (is_smb3) {
			cifs_dbg(VFS, "vers=1.0 (cifs) not permitted when mounting with smb3\n");
			return 1;
		}
		vol->ops = &smb1_operations;
		vol->vals = &smb1_values;
		break;
	case Smb_20:
		if (disable_legacy_dialects) {
			cifs_dbg(VFS, "mount with legacy dialect disabled\n");
			return 1;
		}
		if (is_smb3) {
			cifs_dbg(VFS, "vers=2.0 not permitted when mounting with smb3\n");
			return 1;
		}
		vol->ops = &smb20_operations;
		vol->vals = &smb20_values;
		break;
#else
	case Smb_1:
		cifs_dbg(VFS, "vers=1.0 (cifs) mount not permitted when legacy dialects disabled\n");
		return 1;
	case Smb_20:
		cifs_dbg(VFS, "vers=2.0 mount not permitted when legacy dialects disabled\n");
		return 1;
#endif /* CIFS_ALLOW_INSECURE_LEGACY */
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
	case Smb_311:
		vol->ops = &smb311_operations;
		vol->vals = &smb311_values;
		break;
	case Smb_3any:
		vol->ops = &smb30_operations; /* currently identical with 3.0 */
		vol->vals = &smb3any_values;
		break;
	case Smb_default:
		vol->ops = &smb30_operations; /* currently identical with 3.0 */
		vol->vals = &smbdefault_values;
		break;
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

	if (unlikely(!devname || !*devname)) {
		cifs_dbg(VFS, "Device name not specified.\n");
		return -EINVAL;
	}

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

	/* skip any delimiter */
	if (*pos == '/' || *pos == '\\')
		pos++;

	/* If pos is NULL then no prepath */
	if (!*pos)
		return 0;

	vol->prepath = kstrdup(pos, GFP_KERNEL);
	if (!vol->prepath)
		return -ENOMEM;

	return 0;
}

static int
cifs_parse_mount_options(const char *mountdata, const char *devname,
			 struct smb_vol *vol, bool is_smb3)
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
	bool got_version = false;
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
	vol->bsize = 1024 * 1024; /* can improve cp performance significantly */
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

	/* Most clients set timeout to 0, allows server to use its default */
	vol->handle_timeout = 0; /* See MS-SMB2 spec section 2.2.14.2.12 */

	/* offer SMB2.1 and later (SMB3 etc). Secure and widely accepted */
	vol->ops = &smb30_operations;
	vol->vals = &smbdefault_values;

	vol->echo_interval = SMB_ECHO_INTERVAL_DEFAULT;

	/* default to no multichannel (single server connection) */
	vol->multichannel = false;
	vol->max_channels = 1;

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
		case Opt_nolease:
			vol->no_lease = 1;
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
		case Opt_rootfs:
#ifdef CONFIG_CIFS_ROOT
			vol->rootfs = true;
#endif
			break;
		case Opt_posixpaths:
			vol->posix_paths = 1;
			break;
		case Opt_noposixpaths:
			vol->posix_paths = 0;
			break;
		case Opt_nounix:
			if (vol->linux_ext)
				cifs_dbg(VFS,
					"conflicting unix mount options\n");
			vol->no_linux_ext = 1;
			break;
		case Opt_unix:
			if (vol->no_linux_ext)
				cifs_dbg(VFS,
					"conflicting unix mount options\n");
			vol->linux_ext = 1;
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
		case Opt_nohandlecache:
			vol->nohandlecache = 1;
			break;
		case Opt_handlecache:
			vol->nohandlecache = 0;
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
		case Opt_setuidfromacl:
			vol->setuidfromacl = 1;
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
		case Opt_modesid:
			vol->mode_ace = 1;
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
		case Opt_ignore_signature:
			vol->sign = true;
			vol->ignore_signature = true;
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
		case Opt_domainauto:
			vol->domainauto = true;
			break;
		case Opt_rdma:
			vol->rdma = true;
			break;
		case Opt_multichannel:
			vol->multichannel = true;
			break;
		case Opt_nomultichannel:
			vol->multichannel = false;
			break;
		case Opt_compress:
			vol->compression = UNKNOWN_TYPE;
			cifs_dbg(VFS,
				"SMB3 compression support is experimental\n");
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
		case Opt_min_enc_offload:
			if (get_option_ul(args, &option)) {
				cifs_dbg(VFS, "Invalid minimum encrypted read offload size (esize)\n");
				goto cifs_parse_mount_err;
			}
			vol->min_offload = option;
			break;
		case Opt_blocksize:
			if (get_option_ul(args, &option)) {
				cifs_dbg(VFS, "%s: Invalid blocksize value\n",
					__func__);
				goto cifs_parse_mount_err;
			}
			/*
			 * inode blocksize realistically should never need to be
			 * less than 16K or greater than 16M and default is 1MB.
			 * Note that small inode block sizes (e.g. 64K) can lead
			 * to very poor performance of common tools like cp and scp
			 */
			if ((option < CIFS_MAX_MSGSIZE) ||
			   (option > (4 * SMB3_DEFAULT_IOSIZE))) {
				cifs_dbg(VFS, "%s: Invalid blocksize\n",
					__func__);
				goto cifs_parse_mount_err;
			}
			vol->bsize = option;
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
		case Opt_handletimeout:
			if (get_option_ul(args, &option)) {
				cifs_dbg(VFS, "%s: Invalid handletimeout value\n",
					 __func__);
				goto cifs_parse_mount_err;
			}
			vol->handle_timeout = option;
			if (vol->handle_timeout > SMB3_MAX_HANDLE_TIMEOUT) {
				cifs_dbg(VFS, "Invalid handle cache timeout, longer than 16 minutes\n");
				goto cifs_parse_mount_err;
			}
			break;
		case Opt_echo_interval:
			if (get_option_ul(args, &option)) {
				cifs_dbg(VFS, "%s: Invalid echo interval value\n",
					 __func__);
				goto cifs_parse_mount_err;
			}
			vol->echo_interval = option;
			break;
		case Opt_snapshot:
			if (get_option_ul(args, &option)) {
				cifs_dbg(VFS, "%s: Invalid snapshot time\n",
					 __func__);
				goto cifs_parse_mount_err;
			}
			vol->snapshot_time = option;
			break;
		case Opt_max_credits:
			if (get_option_ul(args, &option) || (option < 20) ||
			    (option > 60000)) {
				cifs_dbg(VFS, "%s: Invalid max_credits value\n",
					 __func__);
				goto cifs_parse_mount_err;
			}
			vol->max_credits = option;
			break;
		case Opt_max_channels:
			if (get_option_ul(args, &option) || option < 1 ||
				option > CIFS_MAX_CHANNELS) {
				cifs_dbg(VFS, "%s: Invalid max_channels value, needs to be 1-%d\n",
					 __func__, CIFS_MAX_CHANNELS);
				goto cifs_parse_mount_err;
			}
			vol->max_channels = option;
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
				kzfree(vol->password);
				vol->password = NULL;
				break;
			}
			/* Fallthrough - to Opt_pass below.*/
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

			kzfree(vol->password);
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
			/* version of mount userspace tools, not dialect */
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;

			/* If interface changes in mount.cifs bump to new ver */
			if (strncasecmp(string, "1", 1) == 0) {
				if (strlen(string) > 1) {
					pr_warn("Bad mount helper ver=%s. Did "
						"you want SMB1 (CIFS) dialect "
						"and mean to type vers=1.0 "
						"instead?\n", string);
					goto cifs_parse_mount_err;
				}
				/* This is the default */
				break;
			}
			/* For all other value, error */
			pr_warn("CIFS: Invalid mount helper version specified\n");
			goto cifs_parse_mount_err;
		case Opt_vers:
			/* protocol version (dialect) */
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;

			if (cifs_parse_smb_version(string, vol, is_smb3) != 0)
				goto cifs_parse_mount_err;
			got_version = true;
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

	if (vol->rdma && vol->vals->protocol_id < SMB30_PROT_ID) {
		cifs_dbg(VFS, "SMB Direct requires Version >=3.0\n");
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
		int len;
		const char *slash;

		/* No ip= option specified? Try to get it from UNC */
		/* Use the address part of the UNC. */
		slash = strchr(&vol->UNC[2], '\\');
		len = slash - &vol->UNC[2];
		if (!cifs_convert_address(dstaddr, &vol->UNC[2], len)) {
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

	if (got_version == false)
		pr_warn("No dialect specified on mount. Default has changed to "
			"a more secure dialect, SMB2.1 or later (e.g. SMB3), from CIFS "
			"(SMB1). To use the less secure SMB1 dialect to access "
			"old servers which do not support SMB3 (or SMB2.1) specify vers=1.0"
			" on mount.\n");

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

	/* SMBDirect manages its own ports, don't match it here */
	if (server->rdma)
		return true;

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
	if (server->ops->select_sectype(server, vol->sectype)
	     == Unspecified)
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

	/* If multidialect negotiation see if existing sessions match one */
	if (strcmp(vol->vals->version_string, SMB3ANY_VERSION_STRING) == 0) {
		if (server->vals->protocol_id < SMB30_PROT_ID)
			return 0;
	} else if (strcmp(vol->vals->version_string,
		   SMBDEFAULT_VERSION_STRING) == 0) {
		if (server->vals->protocol_id < SMB21_PROT_ID)
			return 0;
	} else if ((server->vals != vol->vals) || (server->ops != vol->ops))
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

	if (server->echo_interval != vol->echo_interval * HZ)
		return 0;

	if (server->rdma != vol->rdma)
		return 0;

	if (server->ignore_signature != vol->ignore_signature)
		return 0;

	if (server->min_offload != vol->min_offload)
		return 0;

	return 1;
}

struct TCP_Server_Info *
cifs_find_tcp_session(struct smb_vol *vol)
{
	struct TCP_Server_Info *server;

	spin_lock(&cifs_tcp_ses_lock);
	list_for_each_entry(server, &cifs_tcp_ses_list, tcp_ses_list) {
		/*
		 * Skip ses channels since they're only handled in lower layers
		 * (e.g. cifs_send_recv).
		 */
		if (server->is_channel || !match_server(server, vol))
			continue;

		++server->srv_count;
		spin_unlock(&cifs_tcp_ses_lock);
		cifs_dbg(FYI, "Existing tcp session with server found\n");
		return server;
	}
	spin_unlock(&cifs_tcp_ses_lock);
	return NULL;
}

void
cifs_put_tcp_session(struct TCP_Server_Info *server, int from_reconnect)
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

	if (from_reconnect)
		/*
		 * Avoid deadlock here: reconnect work calls
		 * cifs_put_tcp_session() at its end. Need to be sure
		 * that reconnect work does nothing with server pointer after
		 * that step.
		 */
		cancel_delayed_work(&server->reconnect);
	else
		cancel_delayed_work_sync(&server->reconnect);

	spin_lock(&GlobalMid_Lock);
	server->tcpStatus = CifsExiting;
	spin_unlock(&GlobalMid_Lock);

	cifs_crypto_secmech_release(server);
	cifs_fscache_release_client_cookie(server);

	kfree(server->session_key.response);
	server->session_key.response = NULL;
	server->session_key.len = 0;

	task = xchg(&server->tsk, NULL);
	if (task)
		send_sig(SIGKILL, task, 1);
}

struct TCP_Server_Info *
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

	tcp_ses->noblockcnt = volume_info->rootfs;
	tcp_ses->noblocksnd = volume_info->noblocksnd || volume_info->rootfs;
	tcp_ses->noautotune = volume_info->noautotune;
	tcp_ses->tcp_nodelay = volume_info->sockopt_tcp_nodelay;
	tcp_ses->rdma = volume_info->rdma;
	tcp_ses->in_flight = 0;
	tcp_ses->max_in_flight = 0;
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
	tcp_ses->reconnect_instance = 1;
	tcp_ses->lstrp = jiffies;
	tcp_ses->compress_algorithm = cpu_to_le16(volume_info->compression);
	spin_lock_init(&tcp_ses->req_lock);
	INIT_LIST_HEAD(&tcp_ses->tcp_ses_list);
	INIT_LIST_HEAD(&tcp_ses->smb_ses_list);
	INIT_DELAYED_WORK(&tcp_ses->echo, cifs_echo_request);
	INIT_DELAYED_WORK(&tcp_ses->reconnect, smb2_reconnect_server);
	mutex_init(&tcp_ses->reconnect_mutex);
	memcpy(&tcp_ses->srcaddr, &volume_info->srcaddr,
	       sizeof(tcp_ses->srcaddr));
	memcpy(&tcp_ses->dstaddr, &volume_info->dstaddr,
		sizeof(tcp_ses->dstaddr));
	if (volume_info->use_client_guid)
		memcpy(tcp_ses->client_guid, volume_info->client_guid,
		       SMB2_CLIENT_GUID_SIZE);
	else
		generate_random_uuid(tcp_ses->client_guid);
	/*
	 * at this point we are the only ones with the pointer
	 * to the struct since the kernel thread not created yet
	 * no need to spinlock this init of tcpStatus or srv_count
	 */
	tcp_ses->tcpStatus = CifsNew;
	++tcp_ses->srv_count;

	if (volume_info->echo_interval >= SMB_ECHO_INTERVAL_MIN &&
		volume_info->echo_interval <= SMB_ECHO_INTERVAL_MAX)
		tcp_ses->echo_interval = volume_info->echo_interval * HZ;
	else
		tcp_ses->echo_interval = SMB_ECHO_INTERVAL_DEFAULT * HZ;
	if (tcp_ses->rdma) {
#ifndef CONFIG_CIFS_SMB_DIRECT
		cifs_dbg(VFS, "CONFIG_CIFS_SMB_DIRECT is not enabled\n");
		rc = -ENOENT;
		goto out_err_crypto_release;
#endif
		tcp_ses->smbd_conn = smbd_get_connection(
			tcp_ses, (struct sockaddr *)&volume_info->dstaddr);
		if (tcp_ses->smbd_conn) {
			cifs_dbg(VFS, "RDMA transport established\n");
			rc = 0;
			goto smbd_connected;
		} else {
			rc = -ENOENT;
			goto out_err_crypto_release;
		}
	}
	rc = ip_connect(tcp_ses);
	if (rc < 0) {
		cifs_dbg(VFS, "Error connecting to socket. Aborting operation.\n");
		goto out_err_crypto_release;
	}
smbd_connected:
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
	tcp_ses->min_offload = volume_info->min_offload;
	tcp_ses->tcpStatus = CifsNeedNegotiate;

	tcp_ses->nr_targets = 1;
	tcp_ses->ignore_signature = volume_info->ignore_signature;
	/* thread spawned, put it on the list */
	spin_lock(&cifs_tcp_ses_lock);
	list_add(&tcp_ses->tcp_ses_list, &cifs_tcp_ses_list);
	spin_unlock(&cifs_tcp_ses_lock);

	cifs_fscache_get_client_cookie(tcp_ses);

	/* queue echo request delayed work */
	queue_delayed_work(cifsiod_wq, &tcp_ses->echo, tcp_ses->echo_interval);

	return tcp_ses;

out_err_crypto_release:
	cifs_crypto_secmech_release(tcp_ses);

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

	/*
	 * If an existing session is limited to less channels than
	 * requested, it should not be reused
	 */
	if (ses->chan_max < vol->max_channels)
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

/**
 * cifs_setup_ipc - helper to setup the IPC tcon for the session
 *
 * A new IPC connection is made and stored in the session
 * tcon_ipc. The IPC tcon has the same lifetime as the session.
 */
static int
cifs_setup_ipc(struct cifs_ses *ses, struct smb_vol *volume_info)
{
	int rc = 0, xid;
	struct cifs_tcon *tcon;
	struct nls_table *nls_codepage;
	char unc[SERVER_NAME_LENGTH + sizeof("//x/IPC$")] = {0};
	bool seal = false;
	struct TCP_Server_Info *server = ses->server;

	/*
	 * If the mount request that resulted in the creation of the
	 * session requires encryption, force IPC to be encrypted too.
	 */
	if (volume_info->seal) {
		if (server->capabilities & SMB2_GLOBAL_CAP_ENCRYPTION)
			seal = true;
		else {
			cifs_server_dbg(VFS,
				 "IPC: server doesn't support encryption\n");
			return -EOPNOTSUPP;
		}
	}

	tcon = tconInfoAlloc();
	if (tcon == NULL)
		return -ENOMEM;

	scnprintf(unc, sizeof(unc), "\\\\%s\\IPC$", server->hostname);

	/* cannot fail */
	nls_codepage = load_nls_default();

	xid = get_xid();
	tcon->ses = ses;
	tcon->ipc = true;
	tcon->seal = seal;
	rc = server->ops->tree_connect(xid, ses, unc, tcon, nls_codepage);
	free_xid(xid);

	if (rc) {
		cifs_server_dbg(VFS, "failed to connect to IPC (rc=%d)\n", rc);
		tconInfoFree(tcon);
		goto out;
	}

	cifs_dbg(FYI, "IPC tcon rc = %d ipc tid = %d\n", rc, tcon->tid);

	ses->tcon_ipc = tcon;
out:
	unload_nls(nls_codepage);
	return rc;
}

/**
 * cifs_free_ipc - helper to release the session IPC tcon
 *
 * Needs to be called everytime a session is destroyed
 */
static int
cifs_free_ipc(struct cifs_ses *ses)
{
	int rc = 0, xid;
	struct cifs_tcon *tcon = ses->tcon_ipc;

	if (tcon == NULL)
		return 0;

	if (ses->server->ops->tree_disconnect) {
		xid = get_xid();
		rc = ses->server->ops->tree_disconnect(xid, tcon);
		free_xid(xid);
	}

	if (rc)
		cifs_dbg(FYI, "failed to disconnect IPC tcon (rc=%d)\n", rc);

	tconInfoFree(tcon);
	ses->tcon_ipc = NULL;
	return rc;
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

void cifs_put_smb_ses(struct cifs_ses *ses)
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

	cifs_free_ipc(ses);

	if (ses->status == CifsExiting && server->ops->logoff) {
		xid = get_xid();
		rc = server->ops->logoff(xid, ses);
		if (rc)
			cifs_server_dbg(VFS, "%s: Session Logoff failure rc=%d\n",
				__func__, rc);
		_free_xid(xid);
	}

	spin_lock(&cifs_tcp_ses_lock);
	list_del_init(&ses->smb_ses_list);
	spin_unlock(&cifs_tcp_ses_lock);

	/* close any extra channels */
	if (ses->chan_count > 1) {
		int i;

		for (i = 1; i < ses->chan_count; i++)
			cifs_put_tcp_session(ses->chans[i].server, 0);
	}

	sesInfoFree(ses);
	cifs_put_tcp_session(server, 0);
}

#ifdef CONFIG_KEYS

/* strlen("cifs:a:") + CIFS_MAX_DOMAINNAME_LEN + 1 */
#define CIFSCREDS_DESC_SIZE (7 + CIFS_MAX_DOMAINNAME_LEN + 1)

/* Populate username and pw fields from keyring if possible */
static int
cifs_set_cifscreds(struct smb_vol *vol, struct cifs_ses *ses)
{
	int rc = 0;
	int is_domain = 0;
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
		is_domain = 1;
	}

	down_read(&key->sem);
	upayload = user_key_payload_locked(key);
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

	/*
	 * If we have a domain key then we must set the domainName in the
	 * for the request.
	 */
	if (is_domain && ses->domainName) {
		vol->domainname = kstrndup(ses->domainName,
					   strlen(ses->domainName),
					   GFP_KERNEL);
		if (!vol->domainname) {
			cifs_dbg(FYI, "Unable to allocate %zd bytes for "
				 "domain\n", len);
			rc = -ENOMEM;
			kfree(vol->username);
			vol->username = NULL;
			kzfree(vol->password);
			vol->password = NULL;
			goto out_key_put;
		}
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

/**
 * cifs_get_smb_ses - get a session matching @volume_info data from @server
 *
 * This function assumes it is being called from cifs_mount() where we
 * already got a server reference (server refcount +1). See
 * cifs_get_tcon() for refcount explanations.
 */
struct cifs_ses *
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
		cifs_put_tcp_session(server, 0);
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
	if (volume_info->domainauto)
		ses->domainAuto = volume_info->domainauto;
	ses->cred_uid = volume_info->cred_uid;
	ses->linux_uid = volume_info->linux_uid;

	ses->sectype = volume_info->sectype;
	ses->sign = volume_info->sign;
	mutex_lock(&ses->session_mutex);

	/* add server as first channel */
	ses->chans[0].server = server;
	ses->chan_count = 1;
	ses->chan_max = volume_info->multichannel ? volume_info->max_channels:1;

	rc = cifs_negotiate_protocol(xid, ses);
	if (!rc)
		rc = cifs_setup_session(xid, ses, volume_info->local_nls);

	/* each channel uses a different signing key */
	memcpy(ses->chans[0].signkey, ses->smb3signingkey,
	       sizeof(ses->smb3signingkey));

	mutex_unlock(&ses->session_mutex);
	if (rc)
		goto get_ses_fail;

	/* success, put it on the list and add it as first channel */
	spin_lock(&cifs_tcp_ses_lock);
	list_add(&ses->smb_ses_list, &server->smb_ses_list);
	spin_unlock(&cifs_tcp_ses_lock);

	free_xid(xid);

	cifs_setup_ipc(ses, volume_info);

	return ses;

get_ses_fail:
	sesInfoFree(ses);
	free_xid(xid);
	return ERR_PTR(rc);
}

static int match_tcon(struct cifs_tcon *tcon, struct smb_vol *volume_info)
{
	if (tcon->tidStatus == CifsExiting)
		return 0;
	if (strncmp(tcon->treeName, volume_info->UNC, MAX_TREE_SIZE))
		return 0;
	if (tcon->seal != volume_info->seal)
		return 0;
	if (tcon->snapshot_time != volume_info->snapshot_time)
		return 0;
	if (tcon->handle_timeout != volume_info->handle_timeout)
		return 0;
	if (tcon->no_lease != volume_info->no_lease)
		return 0;
	return 1;
}

static struct cifs_tcon *
cifs_find_tcon(struct cifs_ses *ses, struct smb_vol *volume_info)
{
	struct list_head *tmp;
	struct cifs_tcon *tcon;

	spin_lock(&cifs_tcp_ses_lock);
	list_for_each(tmp, &ses->tcon_list) {
		tcon = list_entry(tmp, struct cifs_tcon, tcon_list);
		if (!match_tcon(tcon, volume_info))
			continue;
		++tcon->tc_count;
		spin_unlock(&cifs_tcp_ses_lock);
		return tcon;
	}
	spin_unlock(&cifs_tcp_ses_lock);
	return NULL;
}

void
cifs_put_tcon(struct cifs_tcon *tcon)
{
	unsigned int xid;
	struct cifs_ses *ses;

	/*
	 * IPC tcon share the lifetime of their session and are
	 * destroyed in the session put function
	 */
	if (tcon == NULL || tcon->ipc)
		return;

	ses = tcon->ses;
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

/**
 * cifs_get_tcon - get a tcon matching @volume_info data from @ses
 *
 * - tcon refcount is the number of mount points using the tcon.
 * - ses refcount is the number of tcon using the session.
 *
 * 1. This function assumes it is being called from cifs_mount() where
 *    we already got a session reference (ses refcount +1).
 *
 * 2. Since we're in the context of adding a mount point, the end
 *    result should be either:
 *
 * a) a new tcon already allocated with refcount=1 (1 mount point) and
 *    its session refcount incremented (1 new tcon). This +1 was
 *    already done in (1).
 *
 * b) an existing tcon with refcount+1 (add a mount point to it) and
 *    identical ses refcount (no new tcon). Because of (1) we need to
 *    decrement the ses refcount.
 */
static struct cifs_tcon *
cifs_get_tcon(struct cifs_ses *ses, struct smb_vol *volume_info)
{
	int rc, xid;
	struct cifs_tcon *tcon;

	tcon = cifs_find_tcon(ses, volume_info);
	if (tcon) {
		/*
		 * tcon has refcount already incremented but we need to
		 * decrement extra ses reference gotten by caller (case b)
		 */
		cifs_dbg(FYI, "Found match on UNC path\n");
		cifs_put_smb_ses(ses);
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

	if (volume_info->snapshot_time) {
		if (ses->server->vals->protocol_id == 0) {
			cifs_dbg(VFS,
			     "Use SMB2 or later for snapshot mount option\n");
			rc = -EOPNOTSUPP;
			goto out_fail;
		} else
			tcon->snapshot_time = volume_info->snapshot_time;
	}

	if (volume_info->handle_timeout) {
		if (ses->server->vals->protocol_id == 0) {
			cifs_dbg(VFS,
			     "Use SMB2.1 or later for handle timeout option\n");
			rc = -EOPNOTSUPP;
			goto out_fail;
		} else
			tcon->handle_timeout = volume_info->handle_timeout;
	}

	tcon->ses = ses;
	if (volume_info->password) {
		tcon->password = kstrdup(volume_info->password, GFP_KERNEL);
		if (!tcon->password) {
			rc = -ENOMEM;
			goto out_fail;
		}
	}

	if (volume_info->seal) {
		if (ses->server->vals->protocol_id == 0) {
			cifs_dbg(VFS,
				 "SMB3 or later required for encryption\n");
			rc = -EOPNOTSUPP;
			goto out_fail;
		} else if (tcon->ses->server->capabilities &
					SMB2_GLOBAL_CAP_ENCRYPTION)
			tcon->seal = true;
		else {
			cifs_dbg(VFS, "Encryption is not supported on share\n");
			rc = -EOPNOTSUPP;
			goto out_fail;
		}
	}

	if (volume_info->linux_ext) {
		if (ses->server->posix_ext_supported) {
			tcon->posix_extensions = true;
			printk_once(KERN_WARNING
				"SMB3.11 POSIX Extensions are experimental\n");
		} else {
			cifs_dbg(VFS, "Server does not support mounting with posix SMB3.11 extensions.\n");
			rc = -EOPNOTSUPP;
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

	tcon->use_persistent = false;
	/* check if SMB2 or later, CIFS does not support persistent handles */
	if (volume_info->persistent) {
		if (ses->server->vals->protocol_id == 0) {
			cifs_dbg(VFS,
			     "SMB3 or later required for persistent handles\n");
			rc = -EOPNOTSUPP;
			goto out_fail;
		} else if (ses->server->capabilities &
			   SMB2_GLOBAL_CAP_PERSISTENT_HANDLES)
			tcon->use_persistent = true;
		else /* persistent handles requested but not supported */ {
			cifs_dbg(VFS,
				"Persistent handles not supported on share\n");
			rc = -EOPNOTSUPP;
			goto out_fail;
		}
	} else if ((tcon->capabilities & SMB2_SHARE_CAP_CONTINUOUS_AVAILABILITY)
	     && (ses->server->capabilities & SMB2_GLOBAL_CAP_PERSISTENT_HANDLES)
	     && (volume_info->nopersistent == false)) {
		cifs_dbg(FYI, "enabling persistent handles\n");
		tcon->use_persistent = true;
	} else if (volume_info->resilient) {
		if (ses->server->vals->protocol_id == 0) {
			cifs_dbg(VFS,
			     "SMB2.1 or later required for resilient handles\n");
			rc = -EOPNOTSUPP;
			goto out_fail;
		}
		tcon->use_resilient = true;
	}

	/* If the user really knows what they are doing they can override */
	if (tcon->share_flags & SMB2_SHAREFLAG_NO_CACHING) {
		if (volume_info->cache_ro)
			cifs_dbg(VFS, "cache=ro requested on mount but NO_CACHING flag set on share\n");
		else if (volume_info->cache_rw)
			cifs_dbg(VFS, "cache=singleclient requested on mount but NO_CACHING flag set on share\n");
	}

	/*
	 * We can have only one retry value for a connection to a share so for
	 * resources mounted more than once to the same server share the last
	 * value passed in for the retry flag is used.
	 */
	tcon->retry = volume_info->retry;
	tcon->nocase = volume_info->nocase;
	tcon->nohandlecache = volume_info->nohandlecache;
	tcon->local_lease = volume_info->local_lease;
	tcon->no_lease = volume_info->no_lease;
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

static int
compare_mount_options(struct super_block *sb, struct cifs_mnt_data *mnt_data)
{
	struct cifs_sb_info *old = CIFS_SB(sb);
	struct cifs_sb_info *new = mnt_data->cifs_sb;
	unsigned int oldflags = old->mnt_cifs_flags & CIFS_MOUNT_MASK;
	unsigned int newflags = new->mnt_cifs_flags & CIFS_MOUNT_MASK;

	if ((sb->s_flags & CIFS_MS_MASK) != (mnt_data->flags & CIFS_MS_MASK))
		return 0;

	if (old->mnt_cifs_serverino_autodisabled)
		newflags &= ~CIFS_MOUNT_SERVER_INUM;

	if (oldflags != newflags)
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

static int
match_prepath(struct super_block *sb, struct cifs_mnt_data *mnt_data)
{
	struct cifs_sb_info *old = CIFS_SB(sb);
	struct cifs_sb_info *new = mnt_data->cifs_sb;
	bool old_set = old->mnt_cifs_flags & CIFS_MOUNT_USE_PREFIX_PATH;
	bool new_set = new->mnt_cifs_flags & CIFS_MOUNT_USE_PREFIX_PATH;

	if (old_set && new_set && !strcmp(new->prepath, old->prepath))
		return 1;
	else if (!old_set && !new_set)
		return 1;

	return 0;
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
	    !match_tcon(tcon, volume_info) ||
	    !match_prepath(sb, mnt_data)) {
		rc = 0;
		goto out;
	}

	rc = compare_mount_options(sb, mnt_data);
out:
	spin_unlock(&cifs_tcp_ses_lock);
	cifs_put_tlink(tlink);
	return rc;
}

#ifdef CONFIG_DEBUG_LOCK_ALLOC
static struct lock_class_key cifs_key[2];
static struct lock_class_key cifs_slock_key[2];

static inline void
cifs_reclassify_socket4(struct socket *sock)
{
	struct sock *sk = sock->sk;
	BUG_ON(!sock_allow_reclassification(sk));
	sock_lock_init_class_and_name(sk, "slock-AF_INET-CIFS",
		&cifs_slock_key[0], "sk_lock-AF_INET-CIFS", &cifs_key[0]);
}

static inline void
cifs_reclassify_socket6(struct socket *sock)
{
	struct sock *sk = sock->sk;
	BUG_ON(!sock_allow_reclassification(sk));
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
				cifs_server_dbg(VFS, "Failed to bind to: %pI6c, error: %d\n",
					 &saddr6->sin6_addr, rc);
			else
				cifs_server_dbg(VFS, "Failed to bind to: %pI4, error: %d\n",
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

		if (server->server_RFC1001_name[0] != 0)
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
			cifs_server_dbg(VFS, "Error %d creating socket\n", rc);
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

	rc = socket->ops->connect(socket, saddr, slen,
				  server->noblockcnt ? O_NONBLOCK : 0);
	/*
	 * When mounting SMB root file systems, we do not want to block in
	 * connect. Otherwise bail out and then let cifs_reconnect() perform
	 * reconnect failover - if possible.
	 */
	if (server->noblockcnt && rc == -EINPROGRESS)
		rc = 0;
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

int cifs_setup_cifs_sb(struct smb_vol *pvolume_info,
			struct cifs_sb_info *cifs_sb)
{
	INIT_DELAYED_WORK(&cifs_sb->prune_tlinks, cifs_prune_tlinks);

	spin_lock_init(&cifs_sb->tlink_tree_lock);
	cifs_sb->tlink_tree = RB_ROOT;

	cifs_sb->bsize = pvolume_info->bsize;
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

	if (pvolume_info->nodfs)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NO_DFS;
	if (pvolume_info->noperm)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NO_PERM;
	if (pvolume_info->setuids)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_SET_UID;
	if (pvolume_info->setuidfromacl)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_UID_FROM_ACL;
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
	if (pvolume_info->nohandlecache)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NO_HANDLE_CACHE;
	if (pvolume_info->nostrictsync)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NOSSYNC;
	if (pvolume_info->mand_lock)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NOPOSIXBRL;
	if (pvolume_info->rwpidforward)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_RWPIDFORWARD;
	if (pvolume_info->mode_ace)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_MODE_FROM_SID;
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
	if (pvolume_info->cache_ro) {
		cifs_dbg(VFS, "mounting share with read only caching. Ensure that the share will not be modified while in use.\n");
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_RO_CACHE;
	} else if (pvolume_info->cache_rw) {
		cifs_dbg(VFS, "mounting share in single client RW caching mode. Ensure that no other systems will be accessing the share.\n");
		cifs_sb->mnt_cifs_flags |= (CIFS_MOUNT_RO_CACHE |
					    CIFS_MOUNT_RW_CACHE);
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

	if (pvolume_info->prepath) {
		cifs_sb->prepath = kstrdup(pvolume_info->prepath, GFP_KERNEL);
		if (cifs_sb->prepath == NULL)
			return -ENOMEM;
	}

	return 0;
}

void
cifs_cleanup_volume_info_contents(struct smb_vol *volume_info)
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
	cifs_cleanup_volume_info_contents(volume_info);
	kfree(volume_info);
}

/* Release all succeed connections */
static inline void mount_put_conns(struct cifs_sb_info *cifs_sb,
				   unsigned int xid,
				   struct TCP_Server_Info *server,
				   struct cifs_ses *ses, struct cifs_tcon *tcon)
{
	int rc = 0;

	if (tcon)
		cifs_put_tcon(tcon);
	else if (ses)
		cifs_put_smb_ses(ses);
	else if (server)
		cifs_put_tcp_session(server, 0);
	cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_POSIX_PATHS;
	free_xid(xid);
}

/* Get connections for tcp, ses and tcon */
static int mount_get_conns(struct smb_vol *vol, struct cifs_sb_info *cifs_sb,
			   unsigned int *xid,
			   struct TCP_Server_Info **nserver,
			   struct cifs_ses **nses, struct cifs_tcon **ntcon)
{
	int rc = 0;
	struct TCP_Server_Info *server;
	struct cifs_ses *ses;
	struct cifs_tcon *tcon;

	*nserver = NULL;
	*nses = NULL;
	*ntcon = NULL;

	*xid = get_xid();

	/* get a reference to a tcp session */
	server = cifs_get_tcp_session(vol);
	if (IS_ERR(server)) {
		rc = PTR_ERR(server);
		return rc;
	}

	*nserver = server;

	if ((vol->max_credits < 20) || (vol->max_credits > 60000))
		server->max_credits = SMB2_MAX_CREDITS_AVAILABLE;
	else
		server->max_credits = vol->max_credits;

	/* get a reference to a SMB session */
	ses = cifs_get_smb_ses(server, vol);
	if (IS_ERR(ses)) {
		rc = PTR_ERR(ses);
		return rc;
	}

	*nses = ses;

	if ((vol->persistent == true) && (!(ses->server->capabilities &
					    SMB2_GLOBAL_CAP_PERSISTENT_HANDLES))) {
		cifs_server_dbg(VFS, "persistent handles not supported by server\n");
		return -EOPNOTSUPP;
	}

	/* search for existing tcon to this server share */
	tcon = cifs_get_tcon(ses, vol);
	if (IS_ERR(tcon)) {
		rc = PTR_ERR(tcon);
		return rc;
	}

	*ntcon = tcon;

	/* if new SMB3.11 POSIX extensions are supported do not remap / and \ */
	if (tcon->posix_extensions)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_POSIX_PATHS;

	/* tell server which Unix caps we support */
	if (cap_unix(tcon->ses)) {
		/*
		 * reset of caps checks mount to see if unix extensions disabled
		 * for just this mount.
		 */
		reset_cifs_unix_caps(*xid, tcon, cifs_sb, vol);
		if ((tcon->ses->server->tcpStatus == CifsNeedReconnect) &&
		    (le64_to_cpu(tcon->fsUnixInfo.Capability) &
		     CIFS_UNIX_TRANSPORT_ENCRYPTION_MANDATORY_CAP))
			return -EACCES;
	} else
		tcon->unix_ext = 0; /* server does not support them */

	/* do not care if a following call succeed - informational */
	if (!tcon->pipe && server->ops->qfs_tcon) {
		server->ops->qfs_tcon(*xid, tcon);
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_RO_CACHE) {
			if (tcon->fsDevInfo.DeviceCharacteristics &
			    cpu_to_le32(FILE_READ_ONLY_DEVICE))
				cifs_dbg(VFS, "mounted to read only share\n");
			else if ((cifs_sb->mnt_cifs_flags &
				  CIFS_MOUNT_RW_CACHE) == 0)
				cifs_dbg(VFS, "read only mount of RW share\n");
			/* no need to log a RW mount of a typical RW share */
		}
	}

	cifs_sb->wsize = server->ops->negotiate_wsize(tcon, vol);
	cifs_sb->rsize = server->ops->negotiate_rsize(tcon, vol);

	return 0;
}

static int mount_setup_tlink(struct cifs_sb_info *cifs_sb, struct cifs_ses *ses,
			     struct cifs_tcon *tcon)
{
	struct tcon_link *tlink;

	/* hang the tcon off of the superblock */
	tlink = kzalloc(sizeof(*tlink), GFP_KERNEL);
	if (tlink == NULL)
		return -ENOMEM;

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
	return 0;
}

#ifdef CONFIG_CIFS_DFS_UPCALL
/*
 * cifs_build_path_to_root returns full path to root when we do not have an
 * exiting connection (tcon)
 */
static char *
build_unc_path_to_root(const struct smb_vol *vol,
		       const struct cifs_sb_info *cifs_sb, bool useppath)
{
	char *full_path, *pos;
	unsigned int pplen = useppath && vol->prepath ?
		strlen(vol->prepath) + 1 : 0;
	unsigned int unc_len = strnlen(vol->UNC, MAX_TREE_SIZE + 1);

	if (unc_len > MAX_TREE_SIZE)
		return ERR_PTR(-EINVAL);

	full_path = kmalloc(unc_len + pplen + 1, GFP_KERNEL);
	if (full_path == NULL)
		return ERR_PTR(-ENOMEM);

	memcpy(full_path, vol->UNC, unc_len);
	pos = full_path + unc_len;

	if (pplen) {
		*pos = CIFS_DIR_SEP(cifs_sb);
		memcpy(pos + 1, vol->prepath, pplen);
		pos += pplen;
	}

	*pos = '\0'; /* add trailing null */
	convert_delimiter(full_path, CIFS_DIR_SEP(cifs_sb));
	cifs_dbg(FYI, "%s: full_path=%s\n", __func__, full_path);
	return full_path;
}

/**
 * expand_dfs_referral - Perform a dfs referral query and update the cifs_sb
 *
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
	struct dfs_info3_param referral = {0};
	char *full_path = NULL, *ref_path = NULL, *mdata = NULL;

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_DFS)
		return -EREMOTE;

	full_path = build_unc_path_to_root(volume_info, cifs_sb, true);
	if (IS_ERR(full_path))
		return PTR_ERR(full_path);

	/* For DFS paths, skip the first '\' of the UNC */
	ref_path = check_prefix ? full_path + 1 : volume_info->UNC + 1;

	rc = dfs_cache_find(xid, ses, cifs_sb->local_nls, cifs_remap(cifs_sb),
			    ref_path, &referral, NULL);
	if (!rc) {
		char *fake_devname = NULL;

		mdata = cifs_compose_mount_options(cifs_sb->mountdata,
						   full_path + 1, &referral,
						   &fake_devname);
		free_dfs_info_param(&referral);

		if (IS_ERR(mdata)) {
			rc = PTR_ERR(mdata);
			mdata = NULL;
		} else {
			cifs_cleanup_volume_info_contents(volume_info);
			rc = cifs_setup_volume_info(volume_info, mdata,
						    fake_devname, false);
		}
		kfree(fake_devname);
		kfree(cifs_sb->mountdata);
		cifs_sb->mountdata = mdata;
	}
	kfree(full_path);
	return rc;
}

static inline int get_next_dfs_tgt(const char *path,
				   struct dfs_cache_tgt_list *tgt_list,
				   struct dfs_cache_tgt_iterator **tgt_it)
{
	if (!*tgt_it)
		*tgt_it = dfs_cache_get_tgt_iterator(tgt_list);
	else
		*tgt_it = dfs_cache_get_next_tgt(tgt_list, *tgt_it);
	return !*tgt_it ? -EHOSTDOWN : 0;
}

static int update_vol_info(const struct dfs_cache_tgt_iterator *tgt_it,
			   struct smb_vol *fake_vol, struct smb_vol *vol)
{
	const char *tgt = dfs_cache_get_tgt_name(tgt_it);
	int len = strlen(tgt) + 2;
	char *new_unc;

	new_unc = kmalloc(len, GFP_KERNEL);
	if (!new_unc)
		return -ENOMEM;
	scnprintf(new_unc, len, "\\%s", tgt);

	kfree(vol->UNC);
	vol->UNC = new_unc;

	if (fake_vol->prepath) {
		kfree(vol->prepath);
		vol->prepath = fake_vol->prepath;
		fake_vol->prepath = NULL;
	}
	memcpy(&vol->dstaddr, &fake_vol->dstaddr, sizeof(vol->dstaddr));

	return 0;
}

static int setup_dfs_tgt_conn(const char *path,
			      const struct dfs_cache_tgt_iterator *tgt_it,
			      struct cifs_sb_info *cifs_sb,
			      struct smb_vol *vol,
			      unsigned int *xid,
			      struct TCP_Server_Info **server,
			      struct cifs_ses **ses,
			      struct cifs_tcon **tcon)
{
	int rc;
	struct dfs_info3_param ref = {0};
	char *mdata = NULL, *fake_devname = NULL;
	struct smb_vol fake_vol = {NULL};

	cifs_dbg(FYI, "%s: dfs path: %s\n", __func__, path);

	rc = dfs_cache_get_tgt_referral(path, tgt_it, &ref);
	if (rc)
		return rc;

	mdata = cifs_compose_mount_options(cifs_sb->mountdata, path, &ref,
					   &fake_devname);
	free_dfs_info_param(&ref);

	if (IS_ERR(mdata)) {
		rc = PTR_ERR(mdata);
		mdata = NULL;
	} else {
		cifs_dbg(FYI, "%s: fake_devname: %s\n", __func__, fake_devname);
		rc = cifs_setup_volume_info(&fake_vol, mdata, fake_devname,
					    false);
	}
	kfree(mdata);
	kfree(fake_devname);

	if (!rc) {
		/*
		 * We use a 'fake_vol' here because we need pass it down to the
		 * mount_{get,put} functions to test connection against new DFS
		 * targets.
		 */
		mount_put_conns(cifs_sb, *xid, *server, *ses, *tcon);
		rc = mount_get_conns(&fake_vol, cifs_sb, xid, server, ses,
				     tcon);
		if (!rc) {
			/*
			 * We were able to connect to new target server.
			 * Update current volume info with new target server.
			 */
			rc = update_vol_info(tgt_it, &fake_vol, vol);
		}
	}
	cifs_cleanup_volume_info_contents(&fake_vol);
	return rc;
}

static int mount_do_dfs_failover(const char *path,
				 struct cifs_sb_info *cifs_sb,
				 struct smb_vol *vol,
				 struct cifs_ses *root_ses,
				 unsigned int *xid,
				 struct TCP_Server_Info **server,
				 struct cifs_ses **ses,
				 struct cifs_tcon **tcon)
{
	int rc;
	struct dfs_cache_tgt_list tgt_list;
	struct dfs_cache_tgt_iterator *tgt_it = NULL;

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_DFS)
		return -EOPNOTSUPP;

	rc = dfs_cache_noreq_find(path, NULL, &tgt_list);
	if (rc)
		return rc;

	for (;;) {
		/* Get next DFS target server - if any */
		rc = get_next_dfs_tgt(path, &tgt_list, &tgt_it);
		if (rc)
			break;
		/* Connect to next DFS target */
		rc = setup_dfs_tgt_conn(path, tgt_it, cifs_sb, vol, xid, server,
					ses, tcon);
		if (!rc || rc == -EACCES || rc == -EOPNOTSUPP)
			break;
	}
	if (!rc) {
		/*
		 * Update DFS target hint in DFS referral cache with the target
		 * server we successfully reconnected to.
		 */
		rc = dfs_cache_update_tgthint(*xid, root_ses ? root_ses : *ses,
					      cifs_sb->local_nls,
					      cifs_remap(cifs_sb), path,
					      tgt_it);
	}
	dfs_cache_free_tgts(&tgt_list);
	return rc;
}
#endif

int
cifs_setup_volume_info(struct smb_vol *volume_info, char *mount_data,
			const char *devname, bool is_smb3)
{
	int rc = 0;

	if (cifs_parse_mount_options(mount_data, devname, volume_info, is_smb3))
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
cifs_get_volume_info(char *mount_data, const char *devname, bool is_smb3)
{
	int rc;
	struct smb_vol *volume_info;

	volume_info = kmalloc(sizeof(struct smb_vol), GFP_KERNEL);
	if (!volume_info)
		return ERR_PTR(-ENOMEM);

	rc = cifs_setup_volume_info(volume_info, mount_data, devname, is_smb3);
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
					char *full_path,
					int added_treename)
{
	int rc;
	char *s;
	char sep, tmp;
	int skip = added_treename ? 1 : 0;

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
		 * if the treename is added, we then have to skip the first
		 * part within the separators
		 */
		if (skip) {
			skip = 0;
			continue;
		}
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

/*
 * Check if path is remote (e.g. a DFS share). Return -EREMOTE if it is,
 * otherwise 0.
 */
static int is_path_remote(struct cifs_sb_info *cifs_sb, struct smb_vol *vol,
			  const unsigned int xid,
			  struct TCP_Server_Info *server,
			  struct cifs_tcon *tcon)
{
	int rc;
	char *full_path;

	if (!server->ops->is_path_accessible)
		return -EOPNOTSUPP;

	/*
	 * cifs_build_path_to_root works only when we have a valid tcon
	 */
	full_path = cifs_build_path_to_root(vol, cifs_sb, tcon,
					    tcon->Flags & SMB_SHARE_IS_IN_DFS);
	if (full_path == NULL)
		return -ENOMEM;

	cifs_dbg(FYI, "%s: full_path: %s\n", __func__, full_path);

	rc = server->ops->is_path_accessible(xid, tcon, cifs_sb,
					     full_path);
	if (rc != 0 && rc != -EREMOTE) {
		kfree(full_path);
		return rc;
	}

	if (rc != -EREMOTE) {
		rc = cifs_are_all_path_components_accessible(server, xid, tcon,
			cifs_sb, full_path, tcon->Flags & SMB_SHARE_IS_IN_DFS);
		if (rc != 0) {
			cifs_server_dbg(VFS, "cannot query dirs between root and final path, "
				 "enabling CIFS_MOUNT_USE_PREFIX_PATH\n");
			cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_USE_PREFIX_PATH;
			rc = 0;
		}
	}

	kfree(full_path);
	return rc;
}

#ifdef CONFIG_CIFS_DFS_UPCALL
static inline void set_root_tcon(struct cifs_sb_info *cifs_sb,
				 struct cifs_tcon *tcon,
				 struct cifs_tcon **root)
{
	spin_lock(&cifs_tcp_ses_lock);
	tcon->tc_count++;
	tcon->remap = cifs_remap(cifs_sb);
	spin_unlock(&cifs_tcp_ses_lock);
	*root = tcon;
}

int cifs_mount(struct cifs_sb_info *cifs_sb, struct smb_vol *vol)
{
	int rc = 0;
	unsigned int xid;
	struct cifs_ses *ses;
	struct cifs_tcon *root_tcon = NULL;
	struct cifs_tcon *tcon = NULL;
	struct TCP_Server_Info *server;
	char *root_path = NULL, *full_path = NULL;
	char *old_mountdata, *origin_mountdata = NULL;
	int count;

	rc = mount_get_conns(vol, cifs_sb, &xid, &server, &ses, &tcon);
	if (!rc && tcon) {
		/* If not a standalone DFS root, then check if path is remote */
		rc = dfs_cache_find(xid, ses, cifs_sb->local_nls,
				    cifs_remap(cifs_sb), vol->UNC + 1, NULL,
				    NULL);
		if (rc) {
			rc = is_path_remote(cifs_sb, vol, xid, server, tcon);
			if (!rc)
				goto out;
			if (rc != -EREMOTE)
				goto error;
		}
	}
	/*
	 * If first DFS target server went offline and we failed to connect it,
	 * server and ses pointers are NULL at this point, though we still have
	 * chance to get a cached DFS referral in expand_dfs_referral() and
	 * retry next target available in it.
	 *
	 * If a NULL ses ptr is passed to dfs_cache_find(), a lookup will be
	 * performed against DFS path and *no* requests will be sent to server
	 * for any new DFS referrals. Hence it's safe to skip checking whether
	 * server or ses ptr is NULL.
	 */
	if (rc == -EACCES || rc == -EOPNOTSUPP)
		goto error;

	root_path = build_unc_path_to_root(vol, cifs_sb, false);
	if (IS_ERR(root_path)) {
		rc = PTR_ERR(root_path);
		root_path = NULL;
		goto error;
	}

	full_path = build_unc_path_to_root(vol, cifs_sb, true);
	if (IS_ERR(full_path)) {
		rc = PTR_ERR(full_path);
		full_path = NULL;
		goto error;
	}
	/*
	 * Perform an unconditional check for whether there are DFS
	 * referrals for this path without prefix, to provide support
	 * for DFS referrals from w2k8 servers which don't seem to respond
	 * with PATH_NOT_COVERED to requests that include the prefix.
	 * Chase the referral if found, otherwise continue normally.
	 */
	old_mountdata = cifs_sb->mountdata;
	(void)expand_dfs_referral(xid, ses, vol, cifs_sb, false);

	if (cifs_sb->mountdata == NULL) {
		rc = -ENOENT;
		goto error;
	}

	/* Save DFS root volume information for DFS refresh worker */
	origin_mountdata = kstrndup(cifs_sb->mountdata,
				    strlen(cifs_sb->mountdata), GFP_KERNEL);
	if (!origin_mountdata) {
		rc = -ENOMEM;
		goto error;
	}

	if (cifs_sb->mountdata != old_mountdata) {
		/* If we were redirected, reconnect to new target server */
		mount_put_conns(cifs_sb, xid, server, ses, tcon);
		rc = mount_get_conns(vol, cifs_sb, &xid, &server, &ses, &tcon);
	}
	if (rc) {
		if (rc == -EACCES || rc == -EOPNOTSUPP)
			goto error;
		/* Perform DFS failover to any other DFS targets */
		rc = mount_do_dfs_failover(root_path + 1, cifs_sb, vol, NULL,
					   &xid, &server, &ses, &tcon);
		if (rc)
			goto error;
	}

	kfree(root_path);
	root_path = build_unc_path_to_root(vol, cifs_sb, false);
	if (IS_ERR(root_path)) {
		rc = PTR_ERR(root_path);
		root_path = NULL;
		goto error;
	}
	/* Cache out resolved root server */
	(void)dfs_cache_find(xid, ses, cifs_sb->local_nls, cifs_remap(cifs_sb),
			     root_path + 1, NULL, NULL);
	kfree(root_path);
	root_path = NULL;

	set_root_tcon(cifs_sb, tcon, &root_tcon);

	for (count = 1; ;) {
		if (!rc && tcon) {
			rc = is_path_remote(cifs_sb, vol, xid, server, tcon);
			if (!rc || rc != -EREMOTE)
				break;
		}
		/*
		 * BB: when we implement proper loop detection,
		 *     we will remove this check. But now we need it
		 *     to prevent an indefinite loop if 'DFS tree' is
		 *     misconfigured (i.e. has loops).
		 */
		if (count++ > MAX_NESTED_LINKS) {
			rc = -ELOOP;
			break;
		}

		kfree(full_path);
		full_path = build_unc_path_to_root(vol, cifs_sb, true);
		if (IS_ERR(full_path)) {
			rc = PTR_ERR(full_path);
			full_path = NULL;
			break;
		}

		old_mountdata = cifs_sb->mountdata;
		rc = expand_dfs_referral(xid, root_tcon->ses, vol, cifs_sb,
					 true);
		if (rc)
			break;

		if (cifs_sb->mountdata != old_mountdata) {
			mount_put_conns(cifs_sb, xid, server, ses, tcon);
			rc = mount_get_conns(vol, cifs_sb, &xid, &server, &ses,
					     &tcon);
			/*
			 * Ensure that DFS referrals go through new root server.
			 */
			if (!rc && tcon &&
			    (tcon->share_flags & (SHI1005_FLAGS_DFS |
						  SHI1005_FLAGS_DFS_ROOT))) {
				cifs_put_tcon(root_tcon);
				set_root_tcon(cifs_sb, tcon, &root_tcon);
			}
		}
		if (rc) {
			if (rc == -EACCES || rc == -EOPNOTSUPP)
				break;
			/* Perform DFS failover to any other DFS targets */
			rc = mount_do_dfs_failover(full_path + 1, cifs_sb, vol,
						   root_tcon->ses, &xid,
						   &server, &ses, &tcon);
			if (rc == -EACCES || rc == -EOPNOTSUPP || !server ||
			    !ses)
				goto error;
		}
	}
	cifs_put_tcon(root_tcon);

	if (rc)
		goto error;

	spin_lock(&cifs_tcp_ses_lock);
	if (!tcon->dfs_path) {
		/* Save full path in new tcon to do failover when reconnecting tcons */
		tcon->dfs_path = full_path;
		full_path = NULL;
		tcon->remap = cifs_remap(cifs_sb);
	}
	cifs_sb->origin_fullpath = kstrndup(tcon->dfs_path,
					    strlen(tcon->dfs_path),
					    GFP_ATOMIC);
	if (!cifs_sb->origin_fullpath) {
		spin_unlock(&cifs_tcp_ses_lock);
		rc = -ENOMEM;
		goto error;
	}
	spin_unlock(&cifs_tcp_ses_lock);

	rc = dfs_cache_add_vol(origin_mountdata, vol, cifs_sb->origin_fullpath);
	if (rc) {
		kfree(cifs_sb->origin_fullpath);
		goto error;
	}
	/*
	 * After reconnecting to a different server, unique ids won't
	 * match anymore, so we disable serverino. This prevents
	 * dentry revalidation to think the dentry are stale (ESTALE).
	 */
	cifs_autodisable_serverino(cifs_sb);
out:
	free_xid(xid);
	cifs_try_adding_channels(ses);
	return mount_setup_tlink(cifs_sb, ses, tcon);

error:
	kfree(full_path);
	kfree(root_path);
	kfree(origin_mountdata);
	mount_put_conns(cifs_sb, xid, server, ses, tcon);
	return rc;
}
#else
int cifs_mount(struct cifs_sb_info *cifs_sb, struct smb_vol *vol)
{
	int rc = 0;
	unsigned int xid;
	struct cifs_ses *ses;
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;

	rc = mount_get_conns(vol, cifs_sb, &xid, &server, &ses, &tcon);
	if (rc)
		goto error;

	if (tcon) {
		rc = is_path_remote(cifs_sb, vol, xid, server, tcon);
		if (rc == -EREMOTE)
			rc = -EOPNOTSUPP;
		if (rc)
			goto error;
	}

	free_xid(xid);

	return mount_setup_tlink(cifs_sb, ses, tcon);

error:
	mount_put_conns(cifs_sb, xid, server, ses, tcon);
	return rc;
}
#endif

/*
 * Issue a TREE_CONNECT request.
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
	if (tcon->pipe || (ses->server->sec_mode & SECMODE_USER)) {
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
	if (rc == 0) {
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
				tcon->ipc = true;
				tcon->pipe = true;
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

	kfree(cifs_sb->mountdata);
	kfree(cifs_sb->prepath);
#ifdef CONFIG_CIFS_DFS_UPCALL
	dfs_cache_del_vol(cifs_sb->origin_fullpath);
	kfree(cifs_sb->origin_fullpath);
#endif
	call_rcu(&cifs_sb->rcu, delayed_free);
}

int
cifs_negotiate_protocol(const unsigned int xid, struct cifs_ses *ses)
{
	int rc = 0;
	struct TCP_Server_Info *server = cifs_ses_server(ses);

	if (!server->ops->need_neg || !server->ops->negotiate)
		return -ENOSYS;

	/* only send once per connect */
	if (!server->ops->need_neg(server))
		return 0;

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
	struct TCP_Server_Info *server = cifs_ses_server(ses);

	if (!ses->binding) {
		ses->capabilities = server->capabilities;
		if (linuxExtEnabled == 0)
			ses->capabilities &= (~server->vals->cap_unix);

		if (ses->auth_key.response) {
			cifs_dbg(FYI, "Free previous auth_key.response = %p\n",
				 ses->auth_key.response);
			kfree(ses->auth_key.response);
			ses->auth_key.response = NULL;
			ses->auth_key.len = 0;
		}
	}

	cifs_dbg(FYI, "Security Mode: 0x%x Capabilities: 0x%x TimeAdjust: %d\n",
		 server->sec_mode, server->capabilities, server->timeAdj);

	if (server->ops->sess_setup)
		rc = server->ops->sess_setup(xid, ses, nls_info);

	if (rc)
		cifs_server_dbg(VFS, "Send error in SessSetup = %d\n", rc);

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
	vol_info->nohandlecache = master_tcon->nohandlecache;
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
		cifs_put_tcp_session(master_tcon->ses->server, 0);
		goto out;
	}

	tcon = cifs_get_tcon(ses, vol_info);
	if (IS_ERR(tcon)) {
		cifs_put_smb_ses(ses);
		goto out;
	}

	/* if new SMB3.11 POSIX extensions are supported do not remap / and \ */
	if (tcon->posix_extensions)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_POSIX_PATHS;

	if (cap_unix(ses))
		reset_cifs_unix_caps(0, tcon, NULL, vol_info);

out:
	kfree(vol_info->username);
	kzfree(vol_info->password);
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
	struct rb_node *node;
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
