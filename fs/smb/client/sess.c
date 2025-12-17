// SPDX-License-Identifier: LGPL-2.1
/*
 *
 *   SMB/CIFS session setup handling routines
 *
 *   Copyright (c) International Business Machines  Corp., 2006, 2009
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 */

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
 * cifs_decrease_secondary_channels - Reduce the number of active secondary channels
 * @ses: pointer to the CIFS session structure
 * @disable_mchan: if true, reduce to a single channel; if false, reduce to chan_max
 *
 * This function disables and cleans up extra secondary channels for a CIFS session.
 * If called during reconfiguration, it reduces the channel count to the new maximum (chan_max).
 * Otherwise, it disables all but the primary channel.
 */
void
cifs_decrease_secondary_channels(struct cifs_ses *ses, bool disable_mchan)
{
	int i, chan_count;
	struct TCP_Server_Info *server;
	struct cifs_server_iface *iface;

	spin_lock(&ses->chan_lock);
	chan_count = ses->chan_count;
	if (chan_count == 1)
		goto done;

	/* Update the chan_count to the new maximum */
	if (disable_mchan) {
		cifs_dbg(FYI, "server does not support multichannel anymore.\n");
		ses->chan_count = 1;
	} else {
		ses->chan_count = ses->chan_max;
	}

	/* Disable all secondary channels beyond the new chan_count */
	for (i = ses->chan_count ; i < chan_count; i++) {
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

	/* For extra secondary channels, reset the need reconnect bit */
	if (ses->chan_count == 1) {
		cifs_dbg(VFS, "Disable all secondary channels\n");
		ses->chans_need_reconnect &= 1;
	} else {
		cifs_dbg(VFS, "Disable extra secondary channels\n");
		ses->chans_need_reconnect &= ((1UL << ses->chan_max) - 1);
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
	int retry = 0;

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

try_again:
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
		list_for_each_entry(iface, &ses->iface_list, iface_head)
			iface->weight_fulfilled = 0;

		/* see if it can be satisfied in second attempt */
		if (!retry++)
			goto try_again;

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

	spin_lock(&server->srv_lock);
	memcpy(&server->dstaddr, &iface->sockaddr, sizeof(server->dstaddr));
	spin_unlock(&server->srv_lock);
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

	ctx->server_hostname = ses->server->hostname;

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
	rc = smb3_crypto_shash_allocate(chan->server);
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
