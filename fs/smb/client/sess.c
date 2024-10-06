// SPDX-License-Identifier: LGPL-2.1
/*
 *
 *   SMB/CIFS session setup handling routines
 *
 *   Copyright (c) International Business Machines  Corp., 2006, 2009
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 */

#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"
#include "ntlmssp.h"
#include "nterr.h"
#include <linux/utsname.h>
#include <linux/slab.h>
#include <linux/version.h>
#include "cifsfs.h"
#include "cifs_spnego.h"
#include "smb2proto.h"
#include "fs_context.h"

static int
cifs_ses_add_channel(struct cifs_ses *ses,
		     struct cifs_server_iface *iface);

bool is_ses_using_iface(struct cifs_ses *ses, struct cifs_server_iface *iface)
{
	int i;

	spin_lock(&ses->chan_lock);
	for (i = 0; i < ses->chan_count; i++) {
		if (ses->chans[i].iface == iface) {
			spin_unlock(&ses->chan_lock);
			return true;
		}
	}
	spin_unlock(&ses->chan_lock);
	return false;
}

/* channel helper functions. assumed that chan_lock is held by caller. */

int
cifs_ses_get_chan_index(struct cifs_ses *ses,
			struct TCP_Server_Info *server)
{
	unsigned int i;

	/* if the channel is waiting for termination */
	if (server && server->terminate)
		return CIFS_INVAL_CHAN_INDEX;

	for (i = 0; i < ses->chan_count; i++) {
		if (ses->chans[i].server == server)
			return i;
	}

	/* If we didn't find the channel, it is likely a bug */
	if (server)
		cifs_dbg(VFS, "unable to get chan index for server: 0x%llx",
			 server->conn_id);
	return CIFS_INVAL_CHAN_INDEX;
}

void
cifs_chan_set_in_reconnect(struct cifs_ses *ses,
			     struct TCP_Server_Info *server)
{
	int chan_index = cifs_ses_get_chan_index(ses, server);

	if (chan_index == CIFS_INVAL_CHAN_INDEX)
		return;

	ses->chans[chan_index].in_reconnect = true;
}

void
cifs_chan_clear_in_reconnect(struct cifs_ses *ses,
			     struct TCP_Server_Info *server)
{
	unsigned int chan_index = cifs_ses_get_chan_index(ses, server);

	if (chan_index == CIFS_INVAL_CHAN_INDEX)
		return;

	ses->chans[chan_index].in_reconnect = false;
}

void
cifs_chan_set_need_reconnect(struct cifs_ses *ses,
			     struct TCP_Server_Info *server)
{
	unsigned int chan_index = cifs_ses_get_chan_index(ses, server);

	if (chan_index == CIFS_INVAL_CHAN_INDEX)
		return;

	set_bit(chan_index, &ses->chans_need_reconnect);
	cifs_dbg(FYI, "Set reconnect bitmask for chan %u; now 0x%lx\n",
		 chan_index, ses->chans_need_reconnect);
}

void
cifs_chan_clear_need_reconnect(struct cifs_ses *ses,
			       struct TCP_Server_Info *server)
{
	unsigned int chan_index = cifs_ses_get_chan_index(ses, server);

	if (chan_index == CIFS_INVAL_CHAN_INDEX)
		return;

	clear_bit(chan_index, &ses->chans_need_reconnect);
	cifs_dbg(FYI, "Cleared reconnect bitmask for chan %u; now 0x%lx\n",
		 chan_index, ses->chans_need_reconnect);
}

bool
cifs_chan_needs_reconnect(struct cifs_ses *ses,
			  struct TCP_Server_Info *server)
{
	unsigned int chan_index = cifs_ses_get_chan_index(ses, server);

	if (chan_index == CIFS_INVAL_CHAN_INDEX)
		return true;	/* err on the safer side */

	return CIFS_CHAN_NEEDS_RECONNECT(ses, chan_index);
}

bool
cifs_chan_is_iface_active(struct cifs_ses *ses,
			  struct TCP_Server_Info *server)
{
	unsigned int chan_index = cifs_ses_get_chan_index(ses, server);

	if (chan_index == CIFS_INVAL_CHAN_INDEX)
		return true;	/* err on the safer side */

	return ses->chans[chan_index].iface &&
		ses->chans[chan_index].iface->is_active;
}

/* returns number of channels added */
int cifs_try_adding_channels(struct cifs_ses *ses)
{
	struct TCP_Server_Info *server = ses->server;
	int old_chan_count, new_chan_count;
	int left;
	int rc = 0;
	int tries = 0;
	size_t iface_weight = 0, iface_min_speed = 0;
	struct cifs_server_iface *iface = NULL, *niface = NULL;
	struct cifs_server_iface *last_iface = NULL;

	spin_lock(&ses->chan_lock);

	new_chan_count = old_chan_count = ses->chan_count;
	left = ses->chan_max - ses->chan_count;

	if (left <= 0) {
		spin_unlock(&ses->chan_lock);
		cifs_dbg(FYI,
			 "ses already at max_channels (%zu), nothing to open\n",
			 ses->chan_max);
		return 0;
	}

	if (server->dialect < SMB30_PROT_ID) {
		spin_unlock(&ses->chan_lock);
		cifs_dbg(VFS, "multichannel is not supported on this protocol version, use 3.0 or above\n");
		return 0;
	}

	if (!(server->capabilities & SMB2_GLOBAL_CAP_MULTI_CHANNEL)) {
		spin_unlock(&ses->chan_lock);
		cifs_server_dbg(VFS, "no multichannel support\n");
		return 0;
	}
	spin_unlock(&ses->chan_lock);

	while (left > 0) {

		tries++;
		if (tries > 3*ses->chan_max) {
			cifs_dbg(VFS, "too many channel open attempts (%d channels left to open)\n",
				 left);
			break;
		}

		spin_lock(&ses->iface_lock);
		if (!ses->iface_count) {
			spin_unlock(&ses->iface_lock);
			cifs_dbg(ONCE, "server %s does not advertise interfaces\n",
				      ses->server->hostname);
			break;
		}

		if (!iface)
			iface = list_first_entry(&ses->iface_list, struct cifs_server_iface,
						 iface_head);
		last_iface = list_last_entry(&ses->iface_list, struct cifs_server_iface,
					     iface_head);
		iface_min_speed = last_iface->speed;

		list_for_each_entry_safe_from(iface, niface, &ses->iface_list,
				    iface_head) {
			/* do not mix rdma and non-rdma interfaces */
			if (iface->rdma_capable != ses->server->rdma)
				continue;

			/* skip ifaces that are unusable */
			if (!iface->is_active ||
			    (is_ses_using_iface(ses, iface) &&
			     !iface->rss_capable))
				continue;

			/* check if we already allocated enough channels */
			iface_weight = iface->speed / iface_min_speed;

			if (iface->weight_fulfilled >= iface_weight)
				continue;

			/* take ref before unlock */
			kref_get(&iface->refcount);

			spin_unlock(&ses->iface_lock);
			rc = cifs_ses_add_channel(ses, iface);
			spin_lock(&ses->iface_lock);

			if (rc) {
				cifs_dbg(VFS, "failed to open extra channel on iface:%pIS rc=%d\n",
					 &iface->sockaddr,
					 rc);
				kref_put(&iface->refcount, release_iface);
				/* failure to add chan should increase weight */
				iface->weight_fulfilled++;
				continue;
			}

			iface->num_channels++;
			iface->weight_fulfilled++;
			cifs_info("successfully opened new channel on iface:%pIS\n",
				 &iface->sockaddr);
			break;
		}

		/* reached end of list. reset weight_fulfilled and start over */
		if (list_entry_is_head(iface, &ses->iface_list, iface_head)) {
			list_for_each_entry(iface, &ses->iface_list, iface_head)
				iface->weight_fulfilled = 0;
			spin_unlock(&ses->iface_lock);
			iface = NULL;
			continue;
		}
		spin_unlock(&ses->iface_lock);

		left--;
		new_chan_count++;
	}

	return new_chan_count - old_chan_count;
}

/*
 * called when multichannel is disabled by the server.
 * this always gets called from smb2_reconnect
 * and cannot get called in parallel threads.
 */
void
cifs_disable_secondary_channels(struct cifs_ses *ses)
{
	int i, chan_count;
	struct TCP_Server_Info *server;
	struct cifs_server_iface *iface;

	spin_lock(&ses->chan_lock);
	chan_count = ses->chan_count;
	if (chan_count == 1)
		goto done;

	ses->chan_count = 1;

	/* for all secondary channels reset the need reconnect bit */
	ses->chans_need_reconnect &= 1;

	for (i = 1; i < chan_count; i++) {
		iface = ses->chans[i].iface;
		server = ses->chans[i].server;

		/*
		 * remove these references first, since we need to unlock
		 * the chan_lock here, since iface_lock is a higher lock
		 */
		ses->chans[i].iface = NULL;
		ses->chans[i].server = NULL;
		spin_unlock(&ses->chan_lock);

		if (iface) {
			spin_lock(&ses->iface_lock);
			iface->num_channels--;
			if (iface->weight_fulfilled)
				iface->weight_fulfilled--;
			kref_put(&iface->refcount, release_iface);
			spin_unlock(&ses->iface_lock);
		}

		if (server) {
			if (!server->terminate) {
				server->terminate = true;
				cifs_signal_cifsd_for_reconnect(server, false);
			}
			cifs_put_tcp_session(server, false);
		}

		spin_lock(&ses->chan_lock);
	}

done:
	spin_unlock(&ses->chan_lock);
}

/* update the iface for the channel if necessary. */
void
cifs_chan_update_iface(struct cifs_ses *ses, struct TCP_Server_Info *server)
{
	unsigned int chan_index;
	size_t iface_weight = 0, iface_min_speed = 0;
	struct cifs_server_iface *iface = NULL;
	struct cifs_server_iface *old_iface = NULL;
	struct cifs_server_iface *last_iface = NULL;
	struct sockaddr_storage ss;

	spin_lock(&ses->chan_lock);
	chan_index = cifs_ses_get_chan_index(ses, server);
	if (chan_index == CIFS_INVAL_CHAN_INDEX) {
		spin_unlock(&ses->chan_lock);
		return;
	}

	if (ses->chans[chan_index].iface) {
		old_iface = ses->chans[chan_index].iface;
		if (old_iface->is_active) {
			spin_unlock(&ses->chan_lock);
			return;
		}
	}
	spin_unlock(&ses->chan_lock);

	spin_lock(&server->srv_lock);
	ss = server->dstaddr;
	spin_unlock(&server->srv_lock);

	spin_lock(&ses->iface_lock);
	if (!ses->iface_count) {
		spin_unlock(&ses->iface_lock);
		cifs_dbg(ONCE, "server %s does not advertise interfaces\n", ses->server->hostname);
		return;
	}

	last_iface = list_last_entry(&ses->iface_list, struct cifs_server_iface,
				     iface_head);
	iface_min_speed = last_iface->speed;

	/* then look for a new one */
	list_for_each_entry(iface, &ses->iface_list, iface_head) {
		if (!chan_index) {
			/* if we're trying to get the updated iface for primary channel */
			if (!cifs_match_ipaddr((struct sockaddr *) &ss,
					       (struct sockaddr *) &iface->sockaddr))
				continue;

			kref_get(&iface->refcount);
			break;
		}

		/* do not mix rdma and non-rdma interfaces */
		if (iface->rdma_capable != server->rdma)
			continue;

		if (!iface->is_active ||
		    (is_ses_using_iface(ses, iface) &&
		     !iface->rss_capable)) {
			continue;
		}

		/* check if we already allocated enough channels */
		iface_weight = iface->speed / iface_min_speed;

		if (iface->weight_fulfilled >= iface_weight)
			continue;

		kref_get(&iface->refcount);
		break;
	}

	if (list_entry_is_head(iface, &ses->iface_list, iface_head)) {
		iface = NULL;
		cifs_dbg(FYI, "unable to find a suitable iface\n");
	}

	if (!iface) {
		if (!chan_index)
			cifs_dbg(FYI, "unable to get the interface matching: %pIS\n",
				 &ss);
		else {
			cifs_dbg(FYI, "unable to find another interface to replace: %pIS\n",
				 &old_iface->sockaddr);
		}

		spin_unlock(&ses->iface_lock);
		return;
	}

	/* now drop the ref to the current iface */
	if (old_iface) {
		cifs_dbg(FYI, "replacing iface: %pIS with %pIS\n",
			 &old_iface->sockaddr,
			 &iface->sockaddr);

		old_iface->num_channels--;
		if (old_iface->weight_fulfilled)
			old_iface->weight_fulfilled--;
		iface->num_channels++;
		iface->weight_fulfilled++;

		kref_put(&old_iface->refcount, release_iface);
	} else if (!chan_index) {
		/* special case: update interface for primary channel */
		cifs_dbg(FYI, "referencing primary channel iface: %pIS\n",
			 &iface->sockaddr);
		iface->num_channels++;
		iface->weight_fulfilled++;
	}
	spin_unlock(&ses->iface_lock);

	spin_lock(&ses->chan_lock);
	chan_index = cifs_ses_get_chan_index(ses, server);
	if (chan_index == CIFS_INVAL_CHAN_INDEX) {
		spin_unlock(&ses->chan_lock);
		return;
	}

	ses->chans[chan_index].iface = iface;
	spin_unlock(&ses->chan_lock);
}

static int
cifs_ses_add_channel(struct cifs_ses *ses,
		     struct cifs_server_iface *iface)
{
	struct TCP_Server_Info *chan_server;
	struct cifs_chan *chan;
	struct smb3_fs_context *ctx;
	static const char unc_fmt[] = "\\%s\\foo";
	struct sockaddr_in *ipv4 = (struct sockaddr_in *)&iface->sockaddr;
	struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)&iface->sockaddr;
	size_t len;
	int rc;
	unsigned int xid = get_xid();

	if (iface->sockaddr.ss_family == AF_INET)
		cifs_dbg(FYI, "adding channel to ses %p (speed:%zu bps rdma:%s ip:%pI4)\n",
			 ses, iface->speed, str_yes_no(iface->rdma_capable),
			 &ipv4->sin_addr);
	else
		cifs_dbg(FYI, "adding channel to ses %p (speed:%zu bps rdma:%s ip:%pI6)\n",
			 ses, iface->speed, str_yes_no(iface->rdma_capable),
			 &ipv6->sin6_addr);

	/*
	 * Setup a ctx with mostly the same info as the existing
	 * session and overwrite it with the requested iface data.
	 *
	 * We need to setup at least the fields used for negprot and
	 * sesssetup.
	 *
	 * We only need the ctx here, so we can reuse memory from
	 * the session and server without caring about memory
	 * management.
	 */
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		rc = -ENOMEM;
		goto out_free_xid;
	}

	/* Always make new connection for now (TODO?) */
	ctx->nosharesock = true;

	/* Auth */
	ctx->domainauto = ses->domainAuto;
	ctx->domainname = ses->domainName;

	/* no hostname for extra channels */
	ctx->server_hostname = "";

	ctx->username = ses->user_name;
	ctx->password = ses->password;
	ctx->sectype = ses->sectype;
	ctx->sign = ses->sign;
	ctx->unicode = ses->unicode;

	/* UNC and paths */
	/* XXX: Use ses->server->hostname? */
	len = sizeof(unc_fmt) + SERVER_NAME_LEN_WITH_NULL;
	ctx->UNC = kzalloc(len, GFP_KERNEL);
	if (!ctx->UNC) {
		rc = -ENOMEM;
		goto out_free_ctx;
	}
	scnprintf(ctx->UNC, len, unc_fmt, ses->ip_addr);
	ctx->prepath = "";

	/* Reuse same version as master connection */
	ctx->vals = ses->server->vals;
	ctx->ops = ses->server->ops;

	ctx->noblocksnd = ses->server->noblocksnd;
	ctx->noautotune = ses->server->noautotune;
	ctx->sockopt_tcp_nodelay = ses->server->tcp_nodelay;
	ctx->echo_interval = ses->server->echo_interval / HZ;
	ctx->max_credits = ses->server->max_credits;
	ctx->min_offload = ses->server->min_offload;
	ctx->compress = ses->server->compression.requested;
	ctx->dfs_conn = ses->server->dfs_conn;
	ctx->ignore_signature = ses->server->ignore_signature;
	ctx->leaf_fullpath = ses->server->leaf_fullpath;
	ctx->rootfs = ses->server->noblockcnt;
	ctx->retrans = ses->server->retrans;

	/*
	 * This will be used for encoding/decoding user/domain/pw
	 * during sess setup auth.
	 */
	ctx->local_nls = ses->local_nls;

	/* Use RDMA if possible */
	ctx->rdma = iface->rdma_capable;
	memcpy(&ctx->dstaddr, &iface->sockaddr, sizeof(ctx->dstaddr));

	/* reuse master con client guid */
	memcpy(&ctx->client_guid, ses->server->client_guid,
	       sizeof(ctx->client_guid));
	ctx->use_client_guid = true;

	chan_server = cifs_get_tcp_session(ctx, ses->server);

	spin_lock(&ses->chan_lock);
	chan = &ses->chans[ses->chan_count];
	chan->server = chan_server;
	if (IS_ERR(chan->server)) {
		rc = PTR_ERR(chan->server);
		chan->server = NULL;
		spin_unlock(&ses->chan_lock);
		goto out;
	}
	chan->iface = iface;
	ses->chan_count++;
	atomic_set(&ses->chan_seq, 0);

	/* Mark this channel as needing connect/setup */
	cifs_chan_set_need_reconnect(ses, chan->server);

	spin_unlock(&ses->chan_lock);

	mutex_lock(&ses->session_mutex);
	/*
	 * We need to allocate the server crypto now as we will need
	 * to sign packets before we generate the channel signing key
	 * (we sign with the session key)
	 */
	rc = smb311_crypto_shash_allocate(chan->server);
	if (rc) {
		cifs_dbg(VFS, "%s: crypto alloc failed\n", __func__);
		mutex_unlock(&ses->session_mutex);
		goto out;
	}

	rc = cifs_negotiate_protocol(xid, ses, chan->server);
	if (!rc)
		rc = cifs_setup_session(xid, ses, chan->server, ses->local_nls);

	mutex_unlock(&ses->session_mutex);

out:
	if (rc && chan->server) {
		cifs_put_tcp_session(chan->server, 0);

		spin_lock(&ses->chan_lock);

		/* we rely on all bits beyond chan_count to be clear */
		cifs_chan_clear_need_reconnect(ses, chan->server);
		ses->chan_count--;
		/*
		 * chan_count should never reach 0 as at least the primary
		 * channel is always allocated
		 */
		WARN_ON(ses->chan_count < 1);
		spin_unlock(&ses->chan_lock);
	}

	kfree(ctx->UNC);
out_free_ctx:
	kfree(ctx);
out_free_xid:
	free_xid(xid);
	return rc;
}

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
static __u32 cifs_ssetup_hdr(struct cifs_ses *ses,
			     struct TCP_Server_Info *server,
			     SESSION_SETUP_ANDX *pSMB)
{
	__u32 capabilities = 0;

	/* init fields common to all four types of SessSetup */
	/* Note that offsets for first seven fields in req struct are same  */
	/*	in CIFS Specs so does not matter which of 3 forms of struct */
	/*	that we use in next few lines                               */
	/* Note that header is initialized to zero in header_assemble */
	pSMB->req.AndXCommand = 0xFF;
	pSMB->req.MaxBufferSize = cpu_to_le16(min_t(u32,
					CIFSMaxBufSize + MAX_CIFS_HDR_SIZE - 4,
					USHRT_MAX));
	pSMB->req.MaxMpxCount = cpu_to_le16(server->maxReq);
	pSMB->req.VcNumber = cpu_to_le16(1);

	/* Now no need to set SMBFLG_CASELESS or obsolete CANONICAL PATH */

	/* BB verify whether signing required on neg or just auth frame (and NTLM case) */

	capabilities = CAP_LARGE_FILES | CAP_NT_SMBS | CAP_LEVEL_II_OPLOCKS |
			CAP_LARGE_WRITE_X | CAP_LARGE_READ_X;

	if (server->sign)
		pSMB->req.hdr.Flags2 |= SMBFLG2_SECURITY_SIGNATURE;

	if (ses->capabilities & CAP_UNICODE) {
		pSMB->req.hdr.Flags2 |= SMBFLG2_UNICODE;
		capabilities |= CAP_UNICODE;
	}
	if (ses->capabilities & CAP_STATUS32) {
		pSMB->req.hdr.Flags2 |= SMBFLG2_ERR_STATUS;
		capabilities |= CAP_STATUS32;
	}
	if (ses->capabilities & CAP_DFS) {
		pSMB->req.hdr.Flags2 |= SMBFLG2_DFS;
		capabilities |= CAP_DFS;
	}
	if (ses->capabilities & CAP_UNIX)
		capabilities |= CAP_UNIX;

	return capabilities;
}

static void
unicode_oslm_strings(char **pbcc_area, const struct nls_table *nls_cp)
{
	char *bcc_ptr = *pbcc_area;
	int bytes_ret = 0;

	/* Copy OS version */
	bytes_ret = cifs_strtoUTF16((__le16 *)bcc_ptr, "Linux version ", 32,
				    nls_cp);
	bcc_ptr += 2 * bytes_ret;
	bytes_ret = cifs_strtoUTF16((__le16 *) bcc_ptr, init_utsname()->release,
				    32, nls_cp);
	bcc_ptr += 2 * bytes_ret;
	bcc_ptr += 2; /* trailing null */

	bytes_ret = cifs_strtoUTF16((__le16 *) bcc_ptr, CIFS_NETWORK_OPSYS,
				    32, nls_cp);
	bcc_ptr += 2 * bytes_ret;
	bcc_ptr += 2; /* trailing null */

	*pbcc_area = bcc_ptr;
}

static void unicode_domain_string(char **pbcc_area, struct cifs_ses *ses,
				   const struct nls_table *nls_cp)
{
	char *bcc_ptr = *pbcc_area;
	int bytes_ret = 0;

	/* copy domain */
	if (ses->domainName == NULL) {
		/*
		 * Sending null domain better than using a bogus domain name (as
		 * we did briefly in 2.6.18) since server will use its default
		 */
		*bcc_ptr = 0;
		*(bcc_ptr+1) = 0;
		bytes_ret = 0;
	} else
		bytes_ret = cifs_strtoUTF16((__le16 *) bcc_ptr, ses->domainName,
					    CIFS_MAX_DOMAINNAME_LEN, nls_cp);
	bcc_ptr += 2 * bytes_ret;
	bcc_ptr += 2;  /* account for null terminator */

	*pbcc_area = bcc_ptr;
}

static void unicode_ssetup_strings(char **pbcc_area, struct cifs_ses *ses,
				   const struct nls_table *nls_cp)
{
	char *bcc_ptr = *pbcc_area;
	int bytes_ret = 0;

	/* BB FIXME add check that strings less than 335 or will need to send as arrays */

	/* copy user */
	if (ses->user_name == NULL) {
		/* null user mount */
		*bcc_ptr = 0;
		*(bcc_ptr+1) = 0;
	} else {
		bytes_ret = cifs_strtoUTF16((__le16 *) bcc_ptr, ses->user_name,
					    CIFS_MAX_USERNAME_LEN, nls_cp);
	}
	bcc_ptr += 2 * bytes_ret;
	bcc_ptr += 2; /* account for null termination */

	unicode_domain_string(&bcc_ptr, ses, nls_cp);
	unicode_oslm_strings(&bcc_ptr, nls_cp);

	*pbcc_area = bcc_ptr;
}

static void ascii_ssetup_strings(char **pbcc_area, struct cifs_ses *ses,
				 const struct nls_table *nls_cp)
{
	char *bcc_ptr = *pbcc_area;
	int len;

	/* copy user */
	/* BB what about null user mounts - check that we do this BB */
	/* copy user */
	if (ses->user_name != NULL) {
		len = strscpy(bcc_ptr, ses->user_name, CIFS_MAX_USERNAME_LEN);
		if (WARN_ON_ONCE(len < 0))
			len = CIFS_MAX_USERNAME_LEN - 1;
		bcc_ptr += len;
	}
	/* else null user mount */
	*bcc_ptr = 0;
	bcc_ptr++; /* account for null termination */

	/* copy domain */
	if (ses->domainName != NULL) {
		len = strscpy(bcc_ptr, ses->domainName, CIFS_MAX_DOMAINNAME_LEN);
		if (WARN_ON_ONCE(len < 0))
			len = CIFS_MAX_DOMAINNAME_LEN - 1;
		bcc_ptr += len;
	} /* else we send a null domain name so server will default to its own domain */
	*bcc_ptr = 0;
	bcc_ptr++;

	/* BB check for overflow here */

	strcpy(bcc_ptr, "Linux version ");
	bcc_ptr += strlen("Linux version ");
	strcpy(bcc_ptr, init_utsname()->release);
	bcc_ptr += strlen(init_utsname()->release) + 1;

	strcpy(bcc_ptr, CIFS_NETWORK_OPSYS);
	bcc_ptr += strlen(CIFS_NETWORK_OPSYS) + 1;

	*pbcc_area = bcc_ptr;
}

static void
decode_unicode_ssetup(char **pbcc_area, int bleft, struct cifs_ses *ses,
		      const struct nls_table *nls_cp)
{
	int len;
	char *data = *pbcc_area;

	cifs_dbg(FYI, "bleft %d\n", bleft);

	kfree(ses->serverOS);
	ses->serverOS = cifs_strndup_from_utf16(data, bleft, true, nls_cp);
	cifs_dbg(FYI, "serverOS=%s\n", ses->serverOS);
	len = (UniStrnlen((wchar_t *) data, bleft / 2) * 2) + 2;
	data += len;
	bleft -= len;
	if (bleft <= 0)
		return;

	kfree(ses->serverNOS);
	ses->serverNOS = cifs_strndup_from_utf16(data, bleft, true, nls_cp);
	cifs_dbg(FYI, "serverNOS=%s\n", ses->serverNOS);
	len = (UniStrnlen((wchar_t *) data, bleft / 2) * 2) + 2;
	data += len;
	bleft -= len;
	if (bleft <= 0)
		return;

	kfree(ses->serverDomain);
	ses->serverDomain = cifs_strndup_from_utf16(data, bleft, true, nls_cp);
	cifs_dbg(FYI, "serverDomain=%s\n", ses->serverDomain);

	return;
}

static void decode_ascii_ssetup(char **pbcc_area, __u16 bleft,
				struct cifs_ses *ses,
				const struct nls_table *nls_cp)
{
	int len;
	char *bcc_ptr = *pbcc_area;

	cifs_dbg(FYI, "decode sessetup ascii. bleft %d\n", bleft);

	len = strnlen(bcc_ptr, bleft);
	if (len >= bleft)
		return;

	kfree(ses->serverOS);

	ses->serverOS = kmalloc(len + 1, GFP_KERNEL);
	if (ses->serverOS) {
		memcpy(ses->serverOS, bcc_ptr, len);
		ses->serverOS[len] = 0;
		if (strncmp(ses->serverOS, "OS/2", 4) == 0)
			cifs_dbg(FYI, "OS/2 server\n");
	}

	bcc_ptr += len + 1;
	bleft -= len + 1;

	len = strnlen(bcc_ptr, bleft);
	if (len >= bleft)
		return;

	kfree(ses->serverNOS);

	ses->serverNOS = kmalloc(len + 1, GFP_KERNEL);
	if (ses->serverNOS) {
		memcpy(ses->serverNOS, bcc_ptr, len);
		ses->serverNOS[len] = 0;
	}

	bcc_ptr += len + 1;
	bleft -= len + 1;

	len = strnlen(bcc_ptr, bleft);
	if (len > bleft)
		return;

	/*
	 * No domain field in LANMAN case. Domain is
	 * returned by old servers in the SMB negprot response
	 *
	 * BB For newer servers which do not support Unicode,
	 * but thus do return domain here, we could add parsing
	 * for it later, but it is not very important
	 */
	cifs_dbg(FYI, "ascii: bytes left %d\n", bleft);
}
#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */

int decode_ntlmssp_challenge(char *bcc_ptr, int blob_len,
				    struct cifs_ses *ses)
{
	unsigned int tioffset; /* challenge message target info area */
	unsigned int tilen; /* challenge message target info area length  */
	CHALLENGE_MESSAGE *pblob = (CHALLENGE_MESSAGE *)bcc_ptr;
	__u32 server_flags;

	if (blob_len < sizeof(CHALLENGE_MESSAGE)) {
		cifs_dbg(VFS, "challenge blob len %d too small\n", blob_len);
		return -EINVAL;
	}

	if (memcmp(pblob->Signature, "NTLMSSP", 8)) {
		cifs_dbg(VFS, "blob signature incorrect %s\n",
			 pblob->Signature);
		return -EINVAL;
	}
	if (pblob->MessageType != NtLmChallenge) {
		cifs_dbg(VFS, "Incorrect message type %d\n",
			 pblob->MessageType);
		return -EINVAL;
	}

	server_flags = le32_to_cpu(pblob->NegotiateFlags);
	cifs_dbg(FYI, "%s: negotiate=0x%08x challenge=0x%08x\n", __func__,
		 ses->ntlmssp->client_flags, server_flags);

	if ((ses->ntlmssp->client_flags & (NTLMSSP_NEGOTIATE_SEAL | NTLMSSP_NEGOTIATE_SIGN)) &&
	    (!(server_flags & NTLMSSP_NEGOTIATE_56) && !(server_flags & NTLMSSP_NEGOTIATE_128))) {
		cifs_dbg(VFS, "%s: requested signing/encryption but server did not return either 56-bit or 128-bit session key size\n",
			 __func__);
		return -EINVAL;
	}
	if (!(server_flags & NTLMSSP_NEGOTIATE_NTLM) && !(server_flags & NTLMSSP_NEGOTIATE_EXTENDED_SEC)) {
		cifs_dbg(VFS, "%s: server does not seem to support either NTLMv1 or NTLMv2\n", __func__);
		return -EINVAL;
	}
	if (ses->server->sign && !(server_flags & NTLMSSP_NEGOTIATE_SIGN)) {
		cifs_dbg(VFS, "%s: forced packet signing but server does not seem to support it\n",
			 __func__);
		return -EOPNOTSUPP;
	}
	if ((ses->ntlmssp->client_flags & NTLMSSP_NEGOTIATE_KEY_XCH) &&
	    !(server_flags & NTLMSSP_NEGOTIATE_KEY_XCH))
		pr_warn_once("%s: authentication has been weakened as server does not support key exchange\n",
			     __func__);

	ses->ntlmssp->server_flags = server_flags;

	memcpy(ses->ntlmssp->cryptkey, pblob->Challenge, CIFS_CRYPTO_KEY_SIZE);
	/*
	 * In particular we can examine sign flags
	 *
	 * BB spec says that if AvId field of MsvAvTimestamp is populated then
	 * we must set the MIC field of the AUTHENTICATE_MESSAGE
	 */

	tioffset = le32_to_cpu(pblob->TargetInfoArray.BufferOffset);
	tilen = le16_to_cpu(pblob->TargetInfoArray.Length);
	if (tioffset > blob_len || tioffset + tilen > blob_len) {
		cifs_dbg(VFS, "tioffset + tilen too high %u + %u\n",
			 tioffset, tilen);
		return -EINVAL;
	}
	if (tilen) {
		kfree_sensitive(ses->auth_key.response);
		ses->auth_key.response = kmemdup(bcc_ptr + tioffset, tilen,
						 GFP_KERNEL);
		if (!ses->auth_key.response) {
			cifs_dbg(VFS, "Challenge target info alloc failure\n");
			return -ENOMEM;
		}
		ses->auth_key.len = tilen;
	}

	return 0;
}

static int size_of_ntlmssp_blob(struct cifs_ses *ses, int base_size)
{
	int sz = base_size + ses->auth_key.len
		- CIFS_SESS_KEY_SIZE + CIFS_CPHTXT_SIZE + 2;

	if (ses->domainName)
		sz += sizeof(__le16) * strnlen(ses->domainName, CIFS_MAX_DOMAINNAME_LEN);
	else
		sz += sizeof(__le16);

	if (ses->user_name)
		sz += sizeof(__le16) * strnlen(ses->user_name, CIFS_MAX_USERNAME_LEN);
	else
		sz += sizeof(__le16);

	if (ses->workstation_name[0])
		sz += sizeof(__le16) * strnlen(ses->workstation_name,
					       ntlmssp_workstation_name_size(ses));
	else
		sz += sizeof(__le16);

	return sz;
}

static inline void cifs_security_buffer_from_str(SECURITY_BUFFER *pbuf,
						 char *str_value,
						 int str_length,
						 unsigned char *pstart,
						 unsigned char **pcur,
						 const struct nls_table *nls_cp)
{
	unsigned char *tmp = pstart;
	int len;

	if (!pbuf)
		return;

	if (!pcur)
		pcur = &tmp;

	if (!str_value) {
		pbuf->BufferOffset = cpu_to_le32(*pcur - pstart);
		pbuf->Length = 0;
		pbuf->MaximumLength = 0;
		*pcur += sizeof(__le16);
	} else {
		len = cifs_strtoUTF16((__le16 *)*pcur,
				      str_value,
				      str_length,
				      nls_cp);
		len *= sizeof(__le16);
		pbuf->BufferOffset = cpu_to_le32(*pcur - pstart);
		pbuf->Length = cpu_to_le16(len);
		pbuf->MaximumLength = cpu_to_le16(len);
		*pcur += len;
	}
}

/* BB Move to ntlmssp.c eventually */

int build_ntlmssp_negotiate_blob(unsigned char **pbuffer,
				 u16 *buflen,
				 struct cifs_ses *ses,
				 struct TCP_Server_Info *server,
				 const struct nls_table *nls_cp)
{
	int rc = 0;
	NEGOTIATE_MESSAGE *sec_blob;
	__u32 flags;
	unsigned char *tmp;
	int len;

	len = size_of_ntlmssp_blob(ses, sizeof(NEGOTIATE_MESSAGE));
	*pbuffer = kmalloc(len, GFP_KERNEL);
	if (!*pbuffer) {
		rc = -ENOMEM;
		cifs_dbg(VFS, "Error %d during NTLMSSP allocation\n", rc);
		*buflen = 0;
		goto setup_ntlm_neg_ret;
	}
	sec_blob = (NEGOTIATE_MESSAGE *)*pbuffer;

	memset(*pbuffer, 0, sizeof(NEGOTIATE_MESSAGE));
	memcpy(sec_blob->Signature, NTLMSSP_SIGNATURE, 8);
	sec_blob->MessageType = NtLmNegotiate;

	/* BB is NTLMV2 session security format easier to use here? */
	flags = NTLMSSP_NEGOTIATE_56 |	NTLMSSP_REQUEST_TARGET |
		NTLMSSP_NEGOTIATE_128 | NTLMSSP_NEGOTIATE_UNICODE |
		NTLMSSP_NEGOTIATE_NTLM | NTLMSSP_NEGOTIATE_EXTENDED_SEC |
		NTLMSSP_NEGOTIATE_ALWAYS_SIGN | NTLMSSP_NEGOTIATE_SEAL |
		NTLMSSP_NEGOTIATE_SIGN;
	if (!server->session_estab || ses->ntlmssp->sesskey_per_smbsess)
		flags |= NTLMSSP_NEGOTIATE_KEY_XCH;

	tmp = *pbuffer + sizeof(NEGOTIATE_MESSAGE);
	ses->ntlmssp->client_flags = flags;
	sec_blob->NegotiateFlags = cpu_to_le32(flags);

	/* these fields should be null in negotiate phase MS-NLMP 3.1.5.1.1 */
	cifs_security_buffer_from_str(&sec_blob->DomainName,
				      NULL,
				      CIFS_MAX_DOMAINNAME_LEN,
				      *pbuffer, &tmp,
				      nls_cp);

	cifs_security_buffer_from_str(&sec_blob->WorkstationName,
				      NULL,
				      CIFS_MAX_WORKSTATION_LEN,
				      *pbuffer, &tmp,
				      nls_cp);

	*buflen = tmp - *pbuffer;
setup_ntlm_neg_ret:
	return rc;
}

/*
 * Build ntlmssp blob with additional fields, such as version,
 * supported by modern servers. For safety limit to SMB3 or later
 * See notes in MS-NLMP Section 2.2.2.1 e.g.
 */
int build_ntlmssp_smb3_negotiate_blob(unsigned char **pbuffer,
				 u16 *buflen,
				 struct cifs_ses *ses,
				 struct TCP_Server_Info *server,
				 const struct nls_table *nls_cp)
{
	int rc = 0;
	struct negotiate_message *sec_blob;
	__u32 flags;
	unsigned char *tmp;
	int len;

	len = size_of_ntlmssp_blob(ses, sizeof(struct negotiate_message));
	*pbuffer = kmalloc(len, GFP_KERNEL);
	if (!*pbuffer) {
		rc = -ENOMEM;
		cifs_dbg(VFS, "Error %d during NTLMSSP allocation\n", rc);
		*buflen = 0;
		goto setup_ntlm_smb3_neg_ret;
	}
	sec_blob = (struct negotiate_message *)*pbuffer;

	memset(*pbuffer, 0, sizeof(struct negotiate_message));
	memcpy(sec_blob->Signature, NTLMSSP_SIGNATURE, 8);
	sec_blob->MessageType = NtLmNegotiate;

	/* BB is NTLMV2 session security format easier to use here? */
	flags = NTLMSSP_NEGOTIATE_56 |	NTLMSSP_REQUEST_TARGET |
		NTLMSSP_NEGOTIATE_128 | NTLMSSP_NEGOTIATE_UNICODE |
		NTLMSSP_NEGOTIATE_NTLM | NTLMSSP_NEGOTIATE_EXTENDED_SEC |
		NTLMSSP_NEGOTIATE_ALWAYS_SIGN | NTLMSSP_NEGOTIATE_SEAL |
		NTLMSSP_NEGOTIATE_SIGN | NTLMSSP_NEGOTIATE_VERSION;
	if (!server->session_estab || ses->ntlmssp->sesskey_per_smbsess)
		flags |= NTLMSSP_NEGOTIATE_KEY_XCH;

	sec_blob->Version.ProductMajorVersion = LINUX_VERSION_MAJOR;
	sec_blob->Version.ProductMinorVersion = LINUX_VERSION_PATCHLEVEL;
	sec_blob->Version.ProductBuild = cpu_to_le16(SMB3_PRODUCT_BUILD);
	sec_blob->Version.NTLMRevisionCurrent = NTLMSSP_REVISION_W2K3;

	tmp = *pbuffer + sizeof(struct negotiate_message);
	ses->ntlmssp->client_flags = flags;
	sec_blob->NegotiateFlags = cpu_to_le32(flags);

	/* these fields should be null in negotiate phase MS-NLMP 3.1.5.1.1 */
	cifs_security_buffer_from_str(&sec_blob->DomainName,
				      NULL,
				      CIFS_MAX_DOMAINNAME_LEN,
				      *pbuffer, &tmp,
				      nls_cp);

	cifs_security_buffer_from_str(&sec_blob->WorkstationName,
				      NULL,
				      CIFS_MAX_WORKSTATION_LEN,
				      *pbuffer, &tmp,
				      nls_cp);

	*buflen = tmp - *pbuffer;
setup_ntlm_smb3_neg_ret:
	return rc;
}


/* See MS-NLMP 2.2.1.3 */
int build_ntlmssp_auth_blob(unsigned char **pbuffer,
					u16 *buflen,
				   struct cifs_ses *ses,
				   struct TCP_Server_Info *server,
				   const struct nls_table *nls_cp)
{
	int rc;
	AUTHENTICATE_MESSAGE *sec_blob;
	__u32 flags;
	unsigned char *tmp;
	int len;

	rc = setup_ntlmv2_rsp(ses, nls_cp);
	if (rc) {
		cifs_dbg(VFS, "Error %d during NTLMSSP authentication\n", rc);
		*buflen = 0;
		goto setup_ntlmv2_ret;
	}

	len = size_of_ntlmssp_blob(ses, sizeof(AUTHENTICATE_MESSAGE));
	*pbuffer = kmalloc(len, GFP_KERNEL);
	if (!*pbuffer) {
		rc = -ENOMEM;
		cifs_dbg(VFS, "Error %d during NTLMSSP allocation\n", rc);
		*buflen = 0;
		goto setup_ntlmv2_ret;
	}
	sec_blob = (AUTHENTICATE_MESSAGE *)*pbuffer;

	memcpy(sec_blob->Signature, NTLMSSP_SIGNATURE, 8);
	sec_blob->MessageType = NtLmAuthenticate;

	/* send version information in ntlmssp authenticate also */
	flags = ses->ntlmssp->server_flags | NTLMSSP_REQUEST_TARGET |
		NTLMSSP_NEGOTIATE_TARGET_INFO | NTLMSSP_NEGOTIATE_VERSION |
		NTLMSSP_NEGOTIATE_WORKSTATION_SUPPLIED;

	sec_blob->Version.ProductMajorVersion = LINUX_VERSION_MAJOR;
	sec_blob->Version.ProductMinorVersion = LINUX_VERSION_PATCHLEVEL;
	sec_blob->Version.ProductBuild = cpu_to_le16(SMB3_PRODUCT_BUILD);
	sec_blob->Version.NTLMRevisionCurrent = NTLMSSP_REVISION_W2K3;

	tmp = *pbuffer + sizeof(AUTHENTICATE_MESSAGE);
	sec_blob->NegotiateFlags = cpu_to_le32(flags);

	sec_blob->LmChallengeResponse.BufferOffset =
				cpu_to_le32(sizeof(AUTHENTICATE_MESSAGE));
	sec_blob->LmChallengeResponse.Length = 0;
	sec_blob->LmChallengeResponse.MaximumLength = 0;

	sec_blob->NtChallengeResponse.BufferOffset =
				cpu_to_le32(tmp - *pbuffer);
	if (ses->user_name != NULL) {
		memcpy(tmp, ses->auth_key.response + CIFS_SESS_KEY_SIZE,
				ses->auth_key.len - CIFS_SESS_KEY_SIZE);
		tmp += ses->auth_key.len - CIFS_SESS_KEY_SIZE;

		sec_blob->NtChallengeResponse.Length =
				cpu_to_le16(ses->auth_key.len - CIFS_SESS_KEY_SIZE);
		sec_blob->NtChallengeResponse.MaximumLength =
				cpu_to_le16(ses->auth_key.len - CIFS_SESS_KEY_SIZE);
	} else {
		/*
		 * don't send an NT Response for anonymous access
		 */
		sec_blob->NtChallengeResponse.Length = 0;
		sec_blob->NtChallengeResponse.MaximumLength = 0;
	}

	cifs_security_buffer_from_str(&sec_blob->DomainName,
				      ses->domainName,
				      CIFS_MAX_DOMAINNAME_LEN,
				      *pbuffer, &tmp,
				      nls_cp);

	cifs_security_buffer_from_str(&sec_blob->UserName,
				      ses->user_name,
				      CIFS_MAX_USERNAME_LEN,
				      *pbuffer, &tmp,
				      nls_cp);

	cifs_security_buffer_from_str(&sec_blob->WorkstationName,
				      ses->workstation_name,
				      ntlmssp_workstation_name_size(ses),
				      *pbuffer, &tmp,
				      nls_cp);

	if ((ses->ntlmssp->server_flags & NTLMSSP_NEGOTIATE_KEY_XCH) &&
	    (!ses->server->session_estab || ses->ntlmssp->sesskey_per_smbsess) &&
	    !calc_seckey(ses)) {
		memcpy(tmp, ses->ntlmssp->ciphertext, CIFS_CPHTXT_SIZE);
		sec_blob->SessionKey.BufferOffset = cpu_to_le32(tmp - *pbuffer);
		sec_blob->SessionKey.Length = cpu_to_le16(CIFS_CPHTXT_SIZE);
		sec_blob->SessionKey.MaximumLength =
				cpu_to_le16(CIFS_CPHTXT_SIZE);
		tmp += CIFS_CPHTXT_SIZE;
	} else {
		sec_blob->SessionKey.BufferOffset = cpu_to_le32(tmp - *pbuffer);
		sec_blob->SessionKey.Length = 0;
		sec_blob->SessionKey.MaximumLength = 0;
	}

	*buflen = tmp - *pbuffer;
setup_ntlmv2_ret:
	return rc;
}

enum securityEnum
cifs_select_sectype(struct TCP_Server_Info *server, enum securityEnum requested)
{
	switch (server->negflavor) {
	case CIFS_NEGFLAVOR_EXTENDED:
		switch (requested) {
		case Kerberos:
		case RawNTLMSSP:
		case IAKerb:
			return requested;
		case Unspecified:
			if (server->sec_ntlmssp &&
			    (global_secflags & CIFSSEC_MAY_NTLMSSP))
				return RawNTLMSSP;
			if ((server->sec_kerberos || server->sec_mskerberos || server->sec_iakerb) &&
			    (global_secflags & CIFSSEC_MAY_KRB5))
				return Kerberos;
			fallthrough;
		default:
			return Unspecified;
		}
	case CIFS_NEGFLAVOR_UNENCAP:
		switch (requested) {
		case NTLMv2:
			return requested;
		case Unspecified:
			if (global_secflags & CIFSSEC_MAY_NTLMV2)
				return NTLMv2;
			break;
		default:
			break;
		}
		fallthrough;
	default:
		return Unspecified;
	}
}

struct sess_data {
	unsigned int xid;
	struct cifs_ses *ses;
	struct TCP_Server_Info *server;
	struct nls_table *nls_cp;
	void (*func)(struct sess_data *);
	int result;

	/* we will send the SMB in three pieces:
	 * a fixed length beginning part, an optional
	 * SPNEGO blob (which can be zero length), and a
	 * last part which will include the strings
	 * and rest of bcc area. This allows us to avoid
	 * a large buffer 17K allocation
	 */
	int buf0_type;
	struct kvec iov[3];
};

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
static int
sess_alloc_buffer(struct sess_data *sess_data, int wct)
{
	int rc;
	struct cifs_ses *ses = sess_data->ses;
	struct smb_hdr *smb_buf;

	rc = small_smb_init_no_tc(SMB_COM_SESSION_SETUP_ANDX, wct, ses,
				  (void **)&smb_buf);

	if (rc)
		return rc;

	sess_data->iov[0].iov_base = (char *)smb_buf;
	sess_data->iov[0].iov_len = be32_to_cpu(smb_buf->smb_buf_length) + 4;
	/*
	 * This variable will be used to clear the buffer
	 * allocated above in case of any error in the calling function.
	 */
	sess_data->buf0_type = CIFS_SMALL_BUFFER;

	/* 2000 big enough to fit max user, domain, NOS name etc. */
	sess_data->iov[2].iov_base = kmalloc(2000, GFP_KERNEL);
	if (!sess_data->iov[2].iov_base) {
		rc = -ENOMEM;
		goto out_free_smb_buf;
	}

	return 0;

out_free_smb_buf:
	cifs_small_buf_release(smb_buf);
	sess_data->iov[0].iov_base = NULL;
	sess_data->iov[0].iov_len = 0;
	sess_data->buf0_type = CIFS_NO_BUFFER;
	return rc;
}

static void
sess_free_buffer(struct sess_data *sess_data)
{
	struct kvec *iov = sess_data->iov;

	/*
	 * Zero the session data before freeing, as it might contain sensitive info (keys, etc).
	 * Note that iov[1] is already freed by caller.
	 */
	if (sess_data->buf0_type != CIFS_NO_BUFFER && iov[0].iov_base)
		memzero_explicit(iov[0].iov_base, iov[0].iov_len);

	free_rsp_buf(sess_data->buf0_type, iov[0].iov_base);
	sess_data->buf0_type = CIFS_NO_BUFFER;
	kfree_sensitive(iov[2].iov_base);
}

static int
sess_establish_session(struct sess_data *sess_data)
{
	struct cifs_ses *ses = sess_data->ses;
	struct TCP_Server_Info *server = sess_data->server;

	cifs_server_lock(server);
	if (!server->session_estab) {
		if (server->sign) {
			server->session_key.response =
				kmemdup(ses->auth_key.response,
				ses->auth_key.len, GFP_KERNEL);
			if (!server->session_key.response) {
				cifs_server_unlock(server);
				return -ENOMEM;
			}
			server->session_key.len =
						ses->auth_key.len;
		}
		server->sequence_number = 0x2;
		server->session_estab = true;
	}
	cifs_server_unlock(server);

	cifs_dbg(FYI, "CIFS session established successfully\n");
	return 0;
}

static int
sess_sendreceive(struct sess_data *sess_data)
{
	int rc;
	struct smb_hdr *smb_buf = (struct smb_hdr *) sess_data->iov[0].iov_base;
	__u16 count;
	struct kvec rsp_iov = { NULL, 0 };

	count = sess_data->iov[1].iov_len + sess_data->iov[2].iov_len;
	be32_add_cpu(&smb_buf->smb_buf_length, count);
	put_bcc(count, smb_buf);

	rc = SendReceive2(sess_data->xid, sess_data->ses,
			  sess_data->iov, 3 /* num_iovecs */,
			  &sess_data->buf0_type,
			  CIFS_LOG_ERROR, &rsp_iov);
	cifs_small_buf_release(sess_data->iov[0].iov_base);
	memcpy(&sess_data->iov[0], &rsp_iov, sizeof(struct kvec));

	return rc;
}

static void
sess_auth_ntlmv2(struct sess_data *sess_data)
{
	int rc = 0;
	struct smb_hdr *smb_buf;
	SESSION_SETUP_ANDX *pSMB;
	char *bcc_ptr;
	struct cifs_ses *ses = sess_data->ses;
	struct TCP_Server_Info *server = sess_data->server;
	__u32 capabilities;
	__u16 bytes_remaining;

	/* old style NTLM sessionsetup */
	/* wct = 13 */
	rc = sess_alloc_buffer(sess_data, 13);
	if (rc)
		goto out;

	pSMB = (SESSION_SETUP_ANDX *)sess_data->iov[0].iov_base;
	bcc_ptr = sess_data->iov[2].iov_base;
	capabilities = cifs_ssetup_hdr(ses, server, pSMB);

	pSMB->req_no_secext.Capabilities = cpu_to_le32(capabilities);

	/* LM2 password would be here if we supported it */
	pSMB->req_no_secext.CaseInsensitivePasswordLength = 0;

	if (ses->user_name != NULL) {
		/* calculate nlmv2 response and session key */
		rc = setup_ntlmv2_rsp(ses, sess_data->nls_cp);
		if (rc) {
			cifs_dbg(VFS, "Error %d during NTLMv2 authentication\n", rc);
			goto out;
		}

		memcpy(bcc_ptr, ses->auth_key.response + CIFS_SESS_KEY_SIZE,
				ses->auth_key.len - CIFS_SESS_KEY_SIZE);
		bcc_ptr += ses->auth_key.len - CIFS_SESS_KEY_SIZE;

		/* set case sensitive password length after tilen may get
		 * assigned, tilen is 0 otherwise.
		 */
		pSMB->req_no_secext.CaseSensitivePasswordLength =
			cpu_to_le16(ses->auth_key.len - CIFS_SESS_KEY_SIZE);
	} else {
		pSMB->req_no_secext.CaseSensitivePasswordLength = 0;
	}

	if (ses->capabilities & CAP_UNICODE) {
		if (!IS_ALIGNED(sess_data->iov[0].iov_len, 2)) {
			*bcc_ptr = 0;
			bcc_ptr++;
		}
		unicode_ssetup_strings(&bcc_ptr, ses, sess_data->nls_cp);
	} else {
		ascii_ssetup_strings(&bcc_ptr, ses, sess_data->nls_cp);
	}


	sess_data->iov[2].iov_len = (long) bcc_ptr -
			(long) sess_data->iov[2].iov_base;

	rc = sess_sendreceive(sess_data);
	if (rc)
		goto out;

	pSMB = (SESSION_SETUP_ANDX *)sess_data->iov[0].iov_base;
	smb_buf = (struct smb_hdr *)sess_data->iov[0].iov_base;

	if (smb_buf->WordCount != 3) {
		rc = -EIO;
		cifs_dbg(VFS, "bad word count %d\n", smb_buf->WordCount);
		goto out;
	}

	if (le16_to_cpu(pSMB->resp.Action) & GUEST_LOGIN)
		cifs_dbg(FYI, "Guest login\n"); /* BB mark SesInfo struct? */

	ses->Suid = smb_buf->Uid;   /* UID left in wire format (le) */
	cifs_dbg(FYI, "UID = %llu\n", ses->Suid);

	bytes_remaining = get_bcc(smb_buf);
	bcc_ptr = pByteArea(smb_buf);

	/* BB check if Unicode and decode strings */
	if (bytes_remaining == 0) {
		/* no string area to decode, do nothing */
	} else if (smb_buf->Flags2 & SMBFLG2_UNICODE) {
		/* unicode string area must be word-aligned */
		if (!IS_ALIGNED((unsigned long)bcc_ptr - (unsigned long)smb_buf, 2)) {
			++bcc_ptr;
			--bytes_remaining;
		}
		decode_unicode_ssetup(&bcc_ptr, bytes_remaining, ses,
				      sess_data->nls_cp);
	} else {
		decode_ascii_ssetup(&bcc_ptr, bytes_remaining, ses,
				    sess_data->nls_cp);
	}

	rc = sess_establish_session(sess_data);
out:
	sess_data->result = rc;
	sess_data->func = NULL;
	sess_free_buffer(sess_data);
	kfree_sensitive(ses->auth_key.response);
	ses->auth_key.response = NULL;
}

#ifdef CONFIG_CIFS_UPCALL
static void
sess_auth_kerberos(struct sess_data *sess_data)
{
	int rc = 0;
	struct smb_hdr *smb_buf;
	SESSION_SETUP_ANDX *pSMB;
	char *bcc_ptr;
	struct cifs_ses *ses = sess_data->ses;
	struct TCP_Server_Info *server = sess_data->server;
	__u32 capabilities;
	__u16 bytes_remaining;
	struct key *spnego_key = NULL;
	struct cifs_spnego_msg *msg;
	u16 blob_len;

	/* extended security */
	/* wct = 12 */
	rc = sess_alloc_buffer(sess_data, 12);
	if (rc)
		goto out;

	pSMB = (SESSION_SETUP_ANDX *)sess_data->iov[0].iov_base;
	bcc_ptr = sess_data->iov[2].iov_base;
	capabilities = cifs_ssetup_hdr(ses, server, pSMB);

	spnego_key = cifs_get_spnego_key(ses, server);
	if (IS_ERR(spnego_key)) {
		rc = PTR_ERR(spnego_key);
		spnego_key = NULL;
		goto out;
	}

	msg = spnego_key->payload.data[0];
	/*
	 * check version field to make sure that cifs.upcall is
	 * sending us a response in an expected form
	 */
	if (msg->version != CIFS_SPNEGO_UPCALL_VERSION) {
		cifs_dbg(VFS, "incorrect version of cifs.upcall (expected %d but got %d)\n",
			 CIFS_SPNEGO_UPCALL_VERSION, msg->version);
		rc = -EKEYREJECTED;
		goto out_put_spnego_key;
	}

	kfree_sensitive(ses->auth_key.response);
	ses->auth_key.response = kmemdup(msg->data, msg->sesskey_len,
					 GFP_KERNEL);
	if (!ses->auth_key.response) {
		cifs_dbg(VFS, "Kerberos can't allocate (%u bytes) memory\n",
			 msg->sesskey_len);
		rc = -ENOMEM;
		goto out_put_spnego_key;
	}
	ses->auth_key.len = msg->sesskey_len;

	pSMB->req.hdr.Flags2 |= SMBFLG2_EXT_SEC;
	capabilities |= CAP_EXTENDED_SECURITY;
	pSMB->req.Capabilities = cpu_to_le32(capabilities);
	sess_data->iov[1].iov_base = msg->data + msg->sesskey_len;
	sess_data->iov[1].iov_len = msg->secblob_len;
	pSMB->req.SecurityBlobLength = cpu_to_le16(sess_data->iov[1].iov_len);

	if (ses->capabilities & CAP_UNICODE) {
		/* unicode strings must be word aligned */
		if (!IS_ALIGNED(sess_data->iov[0].iov_len + sess_data->iov[1].iov_len, 2)) {
			*bcc_ptr = 0;
			bcc_ptr++;
		}
		unicode_oslm_strings(&bcc_ptr, sess_data->nls_cp);
		unicode_domain_string(&bcc_ptr, ses, sess_data->nls_cp);
	} else {
		/* BB: is this right? */
		ascii_ssetup_strings(&bcc_ptr, ses, sess_data->nls_cp);
	}

	sess_data->iov[2].iov_len = (long) bcc_ptr -
			(long) sess_data->iov[2].iov_base;

	rc = sess_sendreceive(sess_data);
	if (rc)
		goto out_put_spnego_key;

	pSMB = (SESSION_SETUP_ANDX *)sess_data->iov[0].iov_base;
	smb_buf = (struct smb_hdr *)sess_data->iov[0].iov_base;

	if (smb_buf->WordCount != 4) {
		rc = -EIO;
		cifs_dbg(VFS, "bad word count %d\n", smb_buf->WordCount);
		goto out_put_spnego_key;
	}

	if (le16_to_cpu(pSMB->resp.Action) & GUEST_LOGIN)
		cifs_dbg(FYI, "Guest login\n"); /* BB mark SesInfo struct? */

	ses->Suid = smb_buf->Uid;   /* UID left in wire format (le) */
	cifs_dbg(FYI, "UID = %llu\n", ses->Suid);

	bytes_remaining = get_bcc(smb_buf);
	bcc_ptr = pByteArea(smb_buf);

	blob_len = le16_to_cpu(pSMB->resp.SecurityBlobLength);
	if (blob_len > bytes_remaining) {
		cifs_dbg(VFS, "bad security blob length %d\n",
				blob_len);
		rc = -EINVAL;
		goto out_put_spnego_key;
	}
	bcc_ptr += blob_len;
	bytes_remaining -= blob_len;

	/* BB check if Unicode and decode strings */
	if (bytes_remaining == 0) {
		/* no string area to decode, do nothing */
	} else if (smb_buf->Flags2 & SMBFLG2_UNICODE) {
		/* unicode string area must be word-aligned */
		if (!IS_ALIGNED((unsigned long)bcc_ptr - (unsigned long)smb_buf, 2)) {
			++bcc_ptr;
			--bytes_remaining;
		}
		decode_unicode_ssetup(&bcc_ptr, bytes_remaining, ses,
				      sess_data->nls_cp);
	} else {
		decode_ascii_ssetup(&bcc_ptr, bytes_remaining, ses,
				    sess_data->nls_cp);
	}

	rc = sess_establish_session(sess_data);
out_put_spnego_key:
	key_invalidate(spnego_key);
	key_put(spnego_key);
out:
	sess_data->result = rc;
	sess_data->func = NULL;
	sess_free_buffer(sess_data);
	kfree_sensitive(ses->auth_key.response);
	ses->auth_key.response = NULL;
}

#endif /* ! CONFIG_CIFS_UPCALL */

/*
 * The required kvec buffers have to be allocated before calling this
 * function.
 */
static int
_sess_auth_rawntlmssp_assemble_req(struct sess_data *sess_data)
{
	SESSION_SETUP_ANDX *pSMB;
	struct cifs_ses *ses = sess_data->ses;
	struct TCP_Server_Info *server = sess_data->server;
	__u32 capabilities;
	char *bcc_ptr;

	pSMB = (SESSION_SETUP_ANDX *)sess_data->iov[0].iov_base;

	capabilities = cifs_ssetup_hdr(ses, server, pSMB);
	if ((pSMB->req.hdr.Flags2 & SMBFLG2_UNICODE) == 0) {
		cifs_dbg(VFS, "NTLMSSP requires Unicode support\n");
		return -ENOSYS;
	}

	pSMB->req.hdr.Flags2 |= SMBFLG2_EXT_SEC;
	capabilities |= CAP_EXTENDED_SECURITY;
	pSMB->req.Capabilities |= cpu_to_le32(capabilities);

	bcc_ptr = sess_data->iov[2].iov_base;
	/* unicode strings must be word aligned */
	if (!IS_ALIGNED(sess_data->iov[0].iov_len + sess_data->iov[1].iov_len, 2)) {
		*bcc_ptr = 0;
		bcc_ptr++;
	}
	unicode_oslm_strings(&bcc_ptr, sess_data->nls_cp);

	sess_data->iov[2].iov_len = (long) bcc_ptr -
					(long) sess_data->iov[2].iov_base;

	return 0;
}

static void
sess_auth_rawntlmssp_authenticate(struct sess_data *sess_data);

static void
sess_auth_rawntlmssp_negotiate(struct sess_data *sess_data)
{
	int rc;
	struct smb_hdr *smb_buf;
	SESSION_SETUP_ANDX *pSMB;
	struct cifs_ses *ses = sess_data->ses;
	struct TCP_Server_Info *server = sess_data->server;
	__u16 bytes_remaining;
	char *bcc_ptr;
	unsigned char *ntlmsspblob = NULL;
	u16 blob_len;

	cifs_dbg(FYI, "rawntlmssp session setup negotiate phase\n");

	/*
	 * if memory allocation is successful, caller of this function
	 * frees it.
	 */
	ses->ntlmssp = kmalloc(sizeof(struct ntlmssp_auth), GFP_KERNEL);
	if (!ses->ntlmssp) {
		rc = -ENOMEM;
		goto out;
	}
	ses->ntlmssp->sesskey_per_smbsess = false;

	/* wct = 12 */
	rc = sess_alloc_buffer(sess_data, 12);
	if (rc)
		goto out;

	pSMB = (SESSION_SETUP_ANDX *)sess_data->iov[0].iov_base;

	/* Build security blob before we assemble the request */
	rc = build_ntlmssp_negotiate_blob(&ntlmsspblob,
				     &blob_len, ses, server,
				     sess_data->nls_cp);
	if (rc)
		goto out_free_ntlmsspblob;

	sess_data->iov[1].iov_len = blob_len;
	sess_data->iov[1].iov_base = ntlmsspblob;
	pSMB->req.SecurityBlobLength = cpu_to_le16(blob_len);

	rc = _sess_auth_rawntlmssp_assemble_req(sess_data);
	if (rc)
		goto out_free_ntlmsspblob;

	rc = sess_sendreceive(sess_data);

	pSMB = (SESSION_SETUP_ANDX *)sess_data->iov[0].iov_base;
	smb_buf = (struct smb_hdr *)sess_data->iov[0].iov_base;

	/* If true, rc here is expected and not an error */
	if (sess_data->buf0_type != CIFS_NO_BUFFER &&
	    smb_buf->Status.CifsError ==
			cpu_to_le32(NT_STATUS_MORE_PROCESSING_REQUIRED))
		rc = 0;

	if (rc)
		goto out_free_ntlmsspblob;

	cifs_dbg(FYI, "rawntlmssp session setup challenge phase\n");

	if (smb_buf->WordCount != 4) {
		rc = -EIO;
		cifs_dbg(VFS, "bad word count %d\n", smb_buf->WordCount);
		goto out_free_ntlmsspblob;
	}

	ses->Suid = smb_buf->Uid;   /* UID left in wire format (le) */
	cifs_dbg(FYI, "UID = %llu\n", ses->Suid);

	bytes_remaining = get_bcc(smb_buf);
	bcc_ptr = pByteArea(smb_buf);

	blob_len = le16_to_cpu(pSMB->resp.SecurityBlobLength);
	if (blob_len > bytes_remaining) {
		cifs_dbg(VFS, "bad security blob length %d\n",
				blob_len);
		rc = -EINVAL;
		goto out_free_ntlmsspblob;
	}

	rc = decode_ntlmssp_challenge(bcc_ptr, blob_len, ses);

out_free_ntlmsspblob:
	kfree_sensitive(ntlmsspblob);
out:
	sess_free_buffer(sess_data);

	if (!rc) {
		sess_data->func = sess_auth_rawntlmssp_authenticate;
		return;
	}

	/* Else error. Cleanup */
	kfree_sensitive(ses->auth_key.response);
	ses->auth_key.response = NULL;
	kfree_sensitive(ses->ntlmssp);
	ses->ntlmssp = NULL;

	sess_data->func = NULL;
	sess_data->result = rc;
}

static void
sess_auth_rawntlmssp_authenticate(struct sess_data *sess_data)
{
	int rc;
	struct smb_hdr *smb_buf;
	SESSION_SETUP_ANDX *pSMB;
	struct cifs_ses *ses = sess_data->ses;
	struct TCP_Server_Info *server = sess_data->server;
	__u16 bytes_remaining;
	char *bcc_ptr;
	unsigned char *ntlmsspblob = NULL;
	u16 blob_len;

	cifs_dbg(FYI, "rawntlmssp session setup authenticate phase\n");

	/* wct = 12 */
	rc = sess_alloc_buffer(sess_data, 12);
	if (rc)
		goto out;

	/* Build security blob before we assemble the request */
	pSMB = (SESSION_SETUP_ANDX *)sess_data->iov[0].iov_base;
	smb_buf = (struct smb_hdr *)pSMB;
	rc = build_ntlmssp_auth_blob(&ntlmsspblob,
					&blob_len, ses, server,
					sess_data->nls_cp);
	if (rc)
		goto out_free_ntlmsspblob;
	sess_data->iov[1].iov_len = blob_len;
	sess_data->iov[1].iov_base = ntlmsspblob;
	pSMB->req.SecurityBlobLength = cpu_to_le16(blob_len);
	/*
	 * Make sure that we tell the server that we are using
	 * the uid that it just gave us back on the response
	 * (challenge)
	 */
	smb_buf->Uid = ses->Suid;

	rc = _sess_auth_rawntlmssp_assemble_req(sess_data);
	if (rc)
		goto out_free_ntlmsspblob;

	rc = sess_sendreceive(sess_data);
	if (rc)
		goto out_free_ntlmsspblob;

	pSMB = (SESSION_SETUP_ANDX *)sess_data->iov[0].iov_base;
	smb_buf = (struct smb_hdr *)sess_data->iov[0].iov_base;
	if (smb_buf->WordCount != 4) {
		rc = -EIO;
		cifs_dbg(VFS, "bad word count %d\n", smb_buf->WordCount);
		goto out_free_ntlmsspblob;
	}

	if (le16_to_cpu(pSMB->resp.Action) & GUEST_LOGIN)
		cifs_dbg(FYI, "Guest login\n"); /* BB mark SesInfo struct? */

	if (ses->Suid != smb_buf->Uid) {
		ses->Suid = smb_buf->Uid;
		cifs_dbg(FYI, "UID changed! new UID = %llu\n", ses->Suid);
	}

	bytes_remaining = get_bcc(smb_buf);
	bcc_ptr = pByteArea(smb_buf);
	blob_len = le16_to_cpu(pSMB->resp.SecurityBlobLength);
	if (blob_len > bytes_remaining) {
		cifs_dbg(VFS, "bad security blob length %d\n",
				blob_len);
		rc = -EINVAL;
		goto out_free_ntlmsspblob;
	}
	bcc_ptr += blob_len;
	bytes_remaining -= blob_len;


	/* BB check if Unicode and decode strings */
	if (bytes_remaining == 0) {
		/* no string area to decode, do nothing */
	} else if (smb_buf->Flags2 & SMBFLG2_UNICODE) {
		/* unicode string area must be word-aligned */
		if (!IS_ALIGNED((unsigned long)bcc_ptr - (unsigned long)smb_buf, 2)) {
			++bcc_ptr;
			--bytes_remaining;
		}
		decode_unicode_ssetup(&bcc_ptr, bytes_remaining, ses,
				      sess_data->nls_cp);
	} else {
		decode_ascii_ssetup(&bcc_ptr, bytes_remaining, ses,
				    sess_data->nls_cp);
	}

out_free_ntlmsspblob:
	kfree_sensitive(ntlmsspblob);
out:
	sess_free_buffer(sess_data);

	if (!rc)
		rc = sess_establish_session(sess_data);

	/* Cleanup */
	kfree_sensitive(ses->auth_key.response);
	ses->auth_key.response = NULL;
	kfree_sensitive(ses->ntlmssp);
	ses->ntlmssp = NULL;

	sess_data->func = NULL;
	sess_data->result = rc;
}

static int select_sec(struct sess_data *sess_data)
{
	int type;
	struct cifs_ses *ses = sess_data->ses;
	struct TCP_Server_Info *server = sess_data->server;

	type = cifs_select_sectype(server, ses->sectype);
	cifs_dbg(FYI, "sess setup type %d\n", type);
	if (type == Unspecified) {
		cifs_dbg(VFS, "Unable to select appropriate authentication method!\n");
		return -EINVAL;
	}

	switch (type) {
	case NTLMv2:
		sess_data->func = sess_auth_ntlmv2;
		break;
	case Kerberos:
#ifdef CONFIG_CIFS_UPCALL
		sess_data->func = sess_auth_kerberos;
		break;
#else
		cifs_dbg(VFS, "Kerberos negotiated but upcall support disabled!\n");
		return -ENOSYS;
#endif /* CONFIG_CIFS_UPCALL */
	case RawNTLMSSP:
		sess_data->func = sess_auth_rawntlmssp_negotiate;
		break;
	default:
		cifs_dbg(VFS, "secType %d not supported!\n", type);
		return -ENOSYS;
	}

	return 0;
}

int CIFS_SessSetup(const unsigned int xid, struct cifs_ses *ses,
		   struct TCP_Server_Info *server,
		   const struct nls_table *nls_cp)
{
	int rc = 0;
	struct sess_data *sess_data;

	if (ses == NULL) {
		WARN(1, "%s: ses == NULL!", __func__);
		return -EINVAL;
	}

	sess_data = kzalloc(sizeof(struct sess_data), GFP_KERNEL);
	if (!sess_data)
		return -ENOMEM;

	sess_data->xid = xid;
	sess_data->ses = ses;
	sess_data->server = server;
	sess_data->buf0_type = CIFS_NO_BUFFER;
	sess_data->nls_cp = (struct nls_table *) nls_cp;

	rc = select_sec(sess_data);
	if (rc)
		goto out;

	while (sess_data->func)
		sess_data->func(sess_data);

	/* Store result before we free sess_data */
	rc = sess_data->result;

out:
	kfree_sensitive(sess_data);
	return rc;
}
#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */
