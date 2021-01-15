/*
 *   fs/cifs/sess.c
 *
 *   SMB/CIFS session setup handling routines
 *
 *   Copyright (c) International Business Machines  Corp., 2006, 2009
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

#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"
#include "ntlmssp.h"
#include "nterr.h"
#include <linux/utsname.h>
#include <linux/slab.h>
#include "cifs_spnego.h"
#include "smb2proto.h"
#include "fs_context.h"

static int
cifs_ses_add_channel(struct cifs_sb_info *cifs_sb, struct cifs_ses *ses,
		     struct cifs_server_iface *iface);

bool
is_server_using_iface(struct TCP_Server_Info *server,
		      struct cifs_server_iface *iface)
{
	struct sockaddr_in *i4 = (struct sockaddr_in *)&iface->sockaddr;
	struct sockaddr_in6 *i6 = (struct sockaddr_in6 *)&iface->sockaddr;
	struct sockaddr_in *s4 = (struct sockaddr_in *)&server->dstaddr;
	struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)&server->dstaddr;

	if (server->dstaddr.ss_family != iface->sockaddr.ss_family)
		return false;
	if (server->dstaddr.ss_family == AF_INET) {
		if (s4->sin_addr.s_addr != i4->sin_addr.s_addr)
			return false;
	} else if (server->dstaddr.ss_family == AF_INET6) {
		if (memcmp(&s6->sin6_addr, &i6->sin6_addr,
			   sizeof(i6->sin6_addr)) != 0)
			return false;
	} else {
		/* unknown family.. */
		return false;
	}
	return true;
}

bool is_ses_using_iface(struct cifs_ses *ses, struct cifs_server_iface *iface)
{
	int i;

	for (i = 0; i < ses->chan_count; i++) {
		if (is_server_using_iface(ses->chans[i].server, iface))
			return true;
	}
	return false;
}

/* returns number of channels added */
int cifs_try_adding_channels(struct cifs_sb_info *cifs_sb, struct cifs_ses *ses)
{
	int old_chan_count = ses->chan_count;
	int left = ses->chan_max - ses->chan_count;
	int i = 0;
	int rc = 0;
	int tries = 0;
	struct cifs_server_iface *ifaces = NULL;
	size_t iface_count;

	if (left <= 0) {
		cifs_dbg(FYI,
			 "ses already at max_channels (%zu), nothing to open\n",
			 ses->chan_max);
		return 0;
	}

	if (ses->server->dialect < SMB30_PROT_ID) {
		cifs_dbg(VFS, "multichannel is not supported on this protocol version, use 3.0 or above\n");
		return 0;
	}

	/*
	 * Make a copy of the iface list at the time and use that
	 * instead so as to not hold the iface spinlock for opening
	 * channels
	 */
	spin_lock(&ses->iface_lock);
	iface_count = ses->iface_count;
	if (iface_count <= 0) {
		spin_unlock(&ses->iface_lock);
		cifs_dbg(VFS, "no iface list available to open channels\n");
		return 0;
	}
	ifaces = kmemdup(ses->iface_list, iface_count*sizeof(*ifaces),
			 GFP_ATOMIC);
	if (!ifaces) {
		spin_unlock(&ses->iface_lock);
		return 0;
	}
	spin_unlock(&ses->iface_lock);

	/*
	 * Keep connecting to same, fastest, iface for all channels as
	 * long as its RSS. Try next fastest one if not RSS or channel
	 * creation fails.
	 */
	while (left > 0) {
		struct cifs_server_iface *iface;

		tries++;
		if (tries > 3*ses->chan_max) {
			cifs_dbg(FYI, "too many channel open attempts (%d channels left to open)\n",
				 left);
			break;
		}

		iface = &ifaces[i];
		if (is_ses_using_iface(ses, iface) && !iface->rss_capable) {
			i = (i+1) % iface_count;
			continue;
		}

		rc = cifs_ses_add_channel(cifs_sb, ses, iface);
		if (rc) {
			cifs_dbg(FYI, "failed to open extra channel on iface#%d rc=%d\n",
				 i, rc);
			i = (i+1) % iface_count;
			continue;
		}

		cifs_dbg(FYI, "successfully opened new channel on iface#%d\n",
			 i);
		left--;
	}

	kfree(ifaces);
	return ses->chan_count - old_chan_count;
}

/*
 * If server is a channel of ses, return the corresponding enclosing
 * cifs_chan otherwise return NULL.
 */
struct cifs_chan *
cifs_ses_find_chan(struct cifs_ses *ses, struct TCP_Server_Info *server)
{
	int i;

	for (i = 0; i < ses->chan_count; i++) {
		if (ses->chans[i].server == server)
			return &ses->chans[i];
	}
	return NULL;
}

static int
cifs_ses_add_channel(struct cifs_sb_info *cifs_sb, struct cifs_ses *ses,
		     struct cifs_server_iface *iface)
{
	struct cifs_chan *chan;
	struct smb3_fs_context ctx = {NULL};
	static const char unc_fmt[] = "\\%s\\foo";
	char unc[sizeof(unc_fmt)+SERVER_NAME_LEN_WITH_NULL] = {0};
	struct sockaddr_in *ipv4 = (struct sockaddr_in *)&iface->sockaddr;
	struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)&iface->sockaddr;
	int rc;
	unsigned int xid = get_xid();

	if (iface->sockaddr.ss_family == AF_INET)
		cifs_dbg(FYI, "adding channel to ses %p (speed:%zu bps rdma:%s ip:%pI4)\n",
			 ses, iface->speed, iface->rdma_capable ? "yes" : "no",
			 &ipv4->sin_addr);
	else
		cifs_dbg(FYI, "adding channel to ses %p (speed:%zu bps rdma:%s ip:%pI4)\n",
			 ses, iface->speed, iface->rdma_capable ? "yes" : "no",
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

	/* Always make new connection for now (TODO?) */
	ctx.nosharesock = true;

	/* Auth */
	ctx.domainauto = ses->domainAuto;
	ctx.domainname = ses->domainName;
	ctx.username = ses->user_name;
	ctx.password = ses->password;
	ctx.sectype = ses->sectype;
	ctx.sign = ses->sign;

	/* UNC and paths */
	/* XXX: Use ses->server->hostname? */
	sprintf(unc, unc_fmt, ses->serverName);
	ctx.UNC = unc;
	ctx.prepath = "";

	/* Reuse same version as master connection */
	ctx.vals = ses->server->vals;
	ctx.ops = ses->server->ops;

	ctx.noblocksnd = ses->server->noblocksnd;
	ctx.noautotune = ses->server->noautotune;
	ctx.sockopt_tcp_nodelay = ses->server->tcp_nodelay;
	ctx.echo_interval = ses->server->echo_interval / HZ;

	/*
	 * This will be used for encoding/decoding user/domain/pw
	 * during sess setup auth.
	 */
	ctx.local_nls = cifs_sb->local_nls;

	/* Use RDMA if possible */
	ctx.rdma = iface->rdma_capable;
	memcpy(&ctx.dstaddr, &iface->sockaddr, sizeof(struct sockaddr_storage));

	/* reuse master con client guid */
	memcpy(&ctx.client_guid, ses->server->client_guid,
	       SMB2_CLIENT_GUID_SIZE);
	ctx.use_client_guid = true;

	mutex_lock(&ses->session_mutex);

	chan = ses->binding_chan = &ses->chans[ses->chan_count];
	chan->server = cifs_get_tcp_session(&ctx);
	if (IS_ERR(chan->server)) {
		rc = PTR_ERR(chan->server);
		chan->server = NULL;
		goto out;
	}
	spin_lock(&cifs_tcp_ses_lock);
	chan->server->is_channel = true;
	spin_unlock(&cifs_tcp_ses_lock);

	/*
	 * We need to allocate the server crypto now as we will need
	 * to sign packets before we generate the channel signing key
	 * (we sign with the session key)
	 */
	rc = smb311_crypto_shash_allocate(chan->server);
	if (rc) {
		cifs_dbg(VFS, "%s: crypto alloc failed\n", __func__);
		goto out;
	}

	ses->binding = true;
	rc = cifs_negotiate_protocol(xid, ses);
	if (rc)
		goto out;

	rc = cifs_setup_session(xid, ses, cifs_sb->local_nls);
	if (rc)
		goto out;

	/* success, put it on the list
	 * XXX: sharing ses between 2 tcp servers is not possible, the
	 * way "internal" linked lists works in linux makes element
	 * only able to belong to one list
	 *
	 * the binding session is already established so the rest of
	 * the code should be able to look it up, no need to add the
	 * ses to the new server.
	 */

	ses->chan_count++;
	atomic_set(&ses->chan_seq, 0);
out:
	ses->binding = false;
	ses->binding_chan = NULL;
	mutex_unlock(&ses->session_mutex);

	if (rc && chan->server)
		cifs_put_tcp_session(chan->server, 0);

	return rc;
}

static __u32 cifs_ssetup_hdr(struct cifs_ses *ses, SESSION_SETUP_ANDX *pSMB)
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
	pSMB->req.MaxMpxCount = cpu_to_le16(ses->server->maxReq);
	pSMB->req.VcNumber = cpu_to_le16(1);

	/* Now no need to set SMBFLG_CASELESS or obsolete CANONICAL PATH */

	/* BB verify whether signing required on neg or just on auth frame
	   (and NTLM case) */

	capabilities = CAP_LARGE_FILES | CAP_NT_SMBS | CAP_LEVEL_II_OPLOCKS |
			CAP_LARGE_WRITE_X | CAP_LARGE_READ_X;

	if (ses->server->sign)
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
		/* Sending null domain better than using a bogus domain name (as
		we did briefly in 2.6.18) since server will use its default */
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

	/* BB FIXME add check that strings total less
	than 335 or will need to send them as arrays */

	/* unicode strings, must be word aligned before the call */
/*	if ((long) bcc_ptr % 2)	{
		*bcc_ptr = 0;
		bcc_ptr++;
	} */
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
	} /* else we will send a null domain name
	     so the server will default to its own domain */
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

	/* No domain field in LANMAN case. Domain is
	   returned by old servers in the SMB negprot response */
	/* BB For newer servers which do not support Unicode,
	   but thus do return domain here we could add parsing
	   for it later, but it is not very important */
	cifs_dbg(FYI, "ascii: bytes left %d\n", bleft);
}

int decode_ntlmssp_challenge(char *bcc_ptr, int blob_len,
				    struct cifs_ses *ses)
{
	unsigned int tioffset; /* challenge message target info area */
	unsigned int tilen; /* challenge message target info area length  */

	CHALLENGE_MESSAGE *pblob = (CHALLENGE_MESSAGE *)bcc_ptr;

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

	memcpy(ses->ntlmssp->cryptkey, pblob->Challenge, CIFS_CRYPTO_KEY_SIZE);
	/* BB we could decode pblob->NegotiateFlags; some may be useful */
	/* In particular we can examine sign flags */
	/* BB spec says that if AvId field of MsvAvTimestamp is populated then
		we must set the MIC field of the AUTHENTICATE_MESSAGE */
	ses->ntlmssp->server_flags = le32_to_cpu(pblob->NegotiateFlags);
	tioffset = le32_to_cpu(pblob->TargetInfoArray.BufferOffset);
	tilen = le16_to_cpu(pblob->TargetInfoArray.Length);
	if (tioffset > blob_len || tioffset + tilen > blob_len) {
		cifs_dbg(VFS, "tioffset + tilen too high %u + %u\n",
			 tioffset, tilen);
		return -EINVAL;
	}
	if (tilen) {
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

/* BB Move to ntlmssp.c eventually */

/* We do not malloc the blob, it is passed in pbuffer, because
   it is fixed size, and small, making this approach cleaner */
void build_ntlmssp_negotiate_blob(unsigned char *pbuffer,
					 struct cifs_ses *ses)
{
	struct TCP_Server_Info *server = cifs_ses_server(ses);
	NEGOTIATE_MESSAGE *sec_blob = (NEGOTIATE_MESSAGE *)pbuffer;
	__u32 flags;

	memset(pbuffer, 0, sizeof(NEGOTIATE_MESSAGE));
	memcpy(sec_blob->Signature, NTLMSSP_SIGNATURE, 8);
	sec_blob->MessageType = NtLmNegotiate;

	/* BB is NTLMV2 session security format easier to use here? */
	flags = NTLMSSP_NEGOTIATE_56 |	NTLMSSP_REQUEST_TARGET |
		NTLMSSP_NEGOTIATE_128 | NTLMSSP_NEGOTIATE_UNICODE |
		NTLMSSP_NEGOTIATE_NTLM | NTLMSSP_NEGOTIATE_EXTENDED_SEC |
		NTLMSSP_NEGOTIATE_SEAL;
	if (server->sign)
		flags |= NTLMSSP_NEGOTIATE_SIGN;
	if (!server->session_estab || ses->ntlmssp->sesskey_per_smbsess)
		flags |= NTLMSSP_NEGOTIATE_KEY_XCH;

	sec_blob->NegotiateFlags = cpu_to_le32(flags);

	sec_blob->WorkstationName.BufferOffset = 0;
	sec_blob->WorkstationName.Length = 0;
	sec_blob->WorkstationName.MaximumLength = 0;

	/* Domain name is sent on the Challenge not Negotiate NTLMSSP request */
	sec_blob->DomainName.BufferOffset = 0;
	sec_blob->DomainName.Length = 0;
	sec_blob->DomainName.MaximumLength = 0;
}

static int size_of_ntlmssp_blob(struct cifs_ses *ses)
{
	int sz = sizeof(AUTHENTICATE_MESSAGE) + ses->auth_key.len
		- CIFS_SESS_KEY_SIZE + CIFS_CPHTXT_SIZE + 2;

	if (ses->domainName)
		sz += 2 * strnlen(ses->domainName, CIFS_MAX_DOMAINNAME_LEN);
	else
		sz += 2;

	if (ses->user_name)
		sz += 2 * strnlen(ses->user_name, CIFS_MAX_USERNAME_LEN);
	else
		sz += 2;

	return sz;
}

int build_ntlmssp_auth_blob(unsigned char **pbuffer,
					u16 *buflen,
				   struct cifs_ses *ses,
				   const struct nls_table *nls_cp)
{
	int rc;
	AUTHENTICATE_MESSAGE *sec_blob;
	__u32 flags;
	unsigned char *tmp;

	rc = setup_ntlmv2_rsp(ses, nls_cp);
	if (rc) {
		cifs_dbg(VFS, "Error %d during NTLMSSP authentication\n", rc);
		*buflen = 0;
		goto setup_ntlmv2_ret;
	}
	*pbuffer = kmalloc(size_of_ntlmssp_blob(ses), GFP_KERNEL);
	if (!*pbuffer) {
		rc = -ENOMEM;
		cifs_dbg(VFS, "Error %d during NTLMSSP allocation\n", rc);
		*buflen = 0;
		goto setup_ntlmv2_ret;
	}
	sec_blob = (AUTHENTICATE_MESSAGE *)*pbuffer;

	memcpy(sec_blob->Signature, NTLMSSP_SIGNATURE, 8);
	sec_blob->MessageType = NtLmAuthenticate;

	flags = NTLMSSP_NEGOTIATE_56 |
		NTLMSSP_REQUEST_TARGET | NTLMSSP_NEGOTIATE_TARGET_INFO |
		NTLMSSP_NEGOTIATE_128 | NTLMSSP_NEGOTIATE_UNICODE |
		NTLMSSP_NEGOTIATE_NTLM | NTLMSSP_NEGOTIATE_EXTENDED_SEC |
		NTLMSSP_NEGOTIATE_SEAL;
	if (ses->server->sign)
		flags |= NTLMSSP_NEGOTIATE_SIGN;
	if (!ses->server->session_estab || ses->ntlmssp->sesskey_per_smbsess)
		flags |= NTLMSSP_NEGOTIATE_KEY_XCH;

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

	if (ses->domainName == NULL) {
		sec_blob->DomainName.BufferOffset = cpu_to_le32(tmp - *pbuffer);
		sec_blob->DomainName.Length = 0;
		sec_blob->DomainName.MaximumLength = 0;
		tmp += 2;
	} else {
		int len;
		len = cifs_strtoUTF16((__le16 *)tmp, ses->domainName,
				      CIFS_MAX_DOMAINNAME_LEN, nls_cp);
		len *= 2; /* unicode is 2 bytes each */
		sec_blob->DomainName.BufferOffset = cpu_to_le32(tmp - *pbuffer);
		sec_blob->DomainName.Length = cpu_to_le16(len);
		sec_blob->DomainName.MaximumLength = cpu_to_le16(len);
		tmp += len;
	}

	if (ses->user_name == NULL) {
		sec_blob->UserName.BufferOffset = cpu_to_le32(tmp - *pbuffer);
		sec_blob->UserName.Length = 0;
		sec_blob->UserName.MaximumLength = 0;
		tmp += 2;
	} else {
		int len;
		len = cifs_strtoUTF16((__le16 *)tmp, ses->user_name,
				      CIFS_MAX_USERNAME_LEN, nls_cp);
		len *= 2; /* unicode is 2 bytes each */
		sec_blob->UserName.BufferOffset = cpu_to_le32(tmp - *pbuffer);
		sec_blob->UserName.Length = cpu_to_le16(len);
		sec_blob->UserName.MaximumLength = cpu_to_le16(len);
		tmp += len;
	}

	sec_blob->WorkstationName.BufferOffset = cpu_to_le32(tmp - *pbuffer);
	sec_blob->WorkstationName.Length = 0;
	sec_blob->WorkstationName.MaximumLength = 0;
	tmp += 2;

	if (((ses->ntlmssp->server_flags & NTLMSSP_NEGOTIATE_KEY_XCH) ||
		(ses->ntlmssp->server_flags & NTLMSSP_NEGOTIATE_EXTENDED_SEC))
			&& !calc_seckey(ses)) {
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
			return requested;
		case Unspecified:
			if (server->sec_ntlmssp &&
			    (global_secflags & CIFSSEC_MAY_NTLMSSP))
				return RawNTLMSSP;
			if ((server->sec_kerberos || server->sec_mskerberos) &&
			    (global_secflags & CIFSSEC_MAY_KRB5))
				return Kerberos;
			fallthrough;
		default:
			return Unspecified;
		}
	case CIFS_NEGFLAVOR_UNENCAP:
		switch (requested) {
		case NTLM:
		case NTLMv2:
			return requested;
		case Unspecified:
			if (global_secflags & CIFSSEC_MAY_NTLMV2)
				return NTLMv2;
			if (global_secflags & CIFSSEC_MAY_NTLM)
				return NTLM;
			break;
		default:
			break;
		}
		fallthrough;	/* to attempt LANMAN authentication next */
	case CIFS_NEGFLAVOR_LANMAN:
		switch (requested) {
		case LANMAN:
			return requested;
		case Unspecified:
			if (global_secflags & CIFSSEC_MAY_LANMAN)
				return LANMAN;
			fallthrough;
		default:
			return Unspecified;
		}
	default:
		return Unspecified;
	}
}

struct sess_data {
	unsigned int xid;
	struct cifs_ses *ses;
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
	kfree(smb_buf);
	sess_data->iov[0].iov_base = NULL;
	sess_data->iov[0].iov_len = 0;
	sess_data->buf0_type = CIFS_NO_BUFFER;
	return rc;
}

static void
sess_free_buffer(struct sess_data *sess_data)
{

	free_rsp_buf(sess_data->buf0_type, sess_data->iov[0].iov_base);
	sess_data->buf0_type = CIFS_NO_BUFFER;
	kfree(sess_data->iov[2].iov_base);
}

static int
sess_establish_session(struct sess_data *sess_data)
{
	struct cifs_ses *ses = sess_data->ses;

	mutex_lock(&ses->server->srv_mutex);
	if (!ses->server->session_estab) {
		if (ses->server->sign) {
			ses->server->session_key.response =
				kmemdup(ses->auth_key.response,
				ses->auth_key.len, GFP_KERNEL);
			if (!ses->server->session_key.response) {
				mutex_unlock(&ses->server->srv_mutex);
				return -ENOMEM;
			}
			ses->server->session_key.len =
						ses->auth_key.len;
		}
		ses->server->sequence_number = 0x2;
		ses->server->session_estab = true;
	}
	mutex_unlock(&ses->server->srv_mutex);

	cifs_dbg(FYI, "CIFS session established successfully\n");
	spin_lock(&GlobalMid_Lock);
	ses->status = CifsGood;
	ses->need_reconnect = false;
	spin_unlock(&GlobalMid_Lock);

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

/*
 * LANMAN and plaintext are less secure and off by default.
 * So we make this explicitly be turned on in kconfig (in the
 * build) and turned on at runtime (changed from the default)
 * in proc/fs/cifs or via mount parm.  Unfortunately this is
 * needed for old Win (e.g. Win95), some obscure NAS and OS/2
 */
#ifdef CONFIG_CIFS_WEAK_PW_HASH
static void
sess_auth_lanman(struct sess_data *sess_data)
{
	int rc = 0;
	struct smb_hdr *smb_buf;
	SESSION_SETUP_ANDX *pSMB;
	char *bcc_ptr;
	struct cifs_ses *ses = sess_data->ses;
	char lnm_session_key[CIFS_AUTH_RESP_SIZE];
	__u16 bytes_remaining;

	/* lanman 2 style sessionsetup */
	/* wct = 10 */
	rc = sess_alloc_buffer(sess_data, 10);
	if (rc)
		goto out;

	pSMB = (SESSION_SETUP_ANDX *)sess_data->iov[0].iov_base;
	bcc_ptr = sess_data->iov[2].iov_base;
	(void)cifs_ssetup_hdr(ses, pSMB);

	pSMB->req.hdr.Flags2 &= ~SMBFLG2_UNICODE;

	if (ses->user_name != NULL) {
		/* no capabilities flags in old lanman negotiation */
		pSMB->old_req.PasswordLength = cpu_to_le16(CIFS_AUTH_RESP_SIZE);

		/* Calculate hash with password and copy into bcc_ptr.
		 * Encryption Key (stored as in cryptkey) gets used if the
		 * security mode bit in Negotiate Protocol response states
		 * to use challenge/response method (i.e. Password bit is 1).
		 */
		rc = calc_lanman_hash(ses->password, ses->server->cryptkey,
				      ses->server->sec_mode & SECMODE_PW_ENCRYPT ?
				      true : false, lnm_session_key);
		if (rc)
			goto out;

		memcpy(bcc_ptr, (char *)lnm_session_key, CIFS_AUTH_RESP_SIZE);
		bcc_ptr += CIFS_AUTH_RESP_SIZE;
	} else {
		pSMB->old_req.PasswordLength = 0;
	}

	/*
	 * can not sign if LANMAN negotiated so no need
	 * to calculate signing key? but what if server
	 * changed to do higher than lanman dialect and
	 * we reconnected would we ever calc signing_key?
	 */

	cifs_dbg(FYI, "Negotiating LANMAN setting up strings\n");
	/* Unicode not allowed for LANMAN dialects */
	ascii_ssetup_strings(&bcc_ptr, ses, sess_data->nls_cp);

	sess_data->iov[2].iov_len = (long) bcc_ptr -
			(long) sess_data->iov[2].iov_base;

	rc = sess_sendreceive(sess_data);
	if (rc)
		goto out;

	pSMB = (SESSION_SETUP_ANDX *)sess_data->iov[0].iov_base;
	smb_buf = (struct smb_hdr *)sess_data->iov[0].iov_base;

	/* lanman response has a word count of 3 */
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
		if (((unsigned long) bcc_ptr - (unsigned long) smb_buf) % 2) {
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
}

#endif

static void
sess_auth_ntlm(struct sess_data *sess_data)
{
	int rc = 0;
	struct smb_hdr *smb_buf;
	SESSION_SETUP_ANDX *pSMB;
	char *bcc_ptr;
	struct cifs_ses *ses = sess_data->ses;
	__u32 capabilities;
	__u16 bytes_remaining;

	/* old style NTLM sessionsetup */
	/* wct = 13 */
	rc = sess_alloc_buffer(sess_data, 13);
	if (rc)
		goto out;

	pSMB = (SESSION_SETUP_ANDX *)sess_data->iov[0].iov_base;
	bcc_ptr = sess_data->iov[2].iov_base;
	capabilities = cifs_ssetup_hdr(ses, pSMB);

	pSMB->req_no_secext.Capabilities = cpu_to_le32(capabilities);
	if (ses->user_name != NULL) {
		pSMB->req_no_secext.CaseInsensitivePasswordLength =
				cpu_to_le16(CIFS_AUTH_RESP_SIZE);
		pSMB->req_no_secext.CaseSensitivePasswordLength =
				cpu_to_le16(CIFS_AUTH_RESP_SIZE);

		/* calculate ntlm response and session key */
		rc = setup_ntlm_response(ses, sess_data->nls_cp);
		if (rc) {
			cifs_dbg(VFS, "Error %d during NTLM authentication\n",
					 rc);
			goto out;
		}

		/* copy ntlm response */
		memcpy(bcc_ptr, ses->auth_key.response + CIFS_SESS_KEY_SIZE,
				CIFS_AUTH_RESP_SIZE);
		bcc_ptr += CIFS_AUTH_RESP_SIZE;
		memcpy(bcc_ptr, ses->auth_key.response + CIFS_SESS_KEY_SIZE,
				CIFS_AUTH_RESP_SIZE);
		bcc_ptr += CIFS_AUTH_RESP_SIZE;
	} else {
		pSMB->req_no_secext.CaseInsensitivePasswordLength = 0;
		pSMB->req_no_secext.CaseSensitivePasswordLength = 0;
	}

	if (ses->capabilities & CAP_UNICODE) {
		/* unicode strings must be word aligned */
		if (sess_data->iov[0].iov_len % 2) {
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
		if (((unsigned long) bcc_ptr - (unsigned long) smb_buf) % 2) {
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
	kfree(ses->auth_key.response);
	ses->auth_key.response = NULL;
}

static void
sess_auth_ntlmv2(struct sess_data *sess_data)
{
	int rc = 0;
	struct smb_hdr *smb_buf;
	SESSION_SETUP_ANDX *pSMB;
	char *bcc_ptr;
	struct cifs_ses *ses = sess_data->ses;
	__u32 capabilities;
	__u16 bytes_remaining;

	/* old style NTLM sessionsetup */
	/* wct = 13 */
	rc = sess_alloc_buffer(sess_data, 13);
	if (rc)
		goto out;

	pSMB = (SESSION_SETUP_ANDX *)sess_data->iov[0].iov_base;
	bcc_ptr = sess_data->iov[2].iov_base;
	capabilities = cifs_ssetup_hdr(ses, pSMB);

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
		if (sess_data->iov[0].iov_len % 2) {
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
		if (((unsigned long) bcc_ptr - (unsigned long) smb_buf) % 2) {
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
	kfree(ses->auth_key.response);
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
	capabilities = cifs_ssetup_hdr(ses, pSMB);

	spnego_key = cifs_get_spnego_key(ses);
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
		if ((sess_data->iov[0].iov_len
			+ sess_data->iov[1].iov_len) % 2) {
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
		if (((unsigned long) bcc_ptr - (unsigned long) smb_buf) % 2) {
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
	kfree(ses->auth_key.response);
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
	__u32 capabilities;
	char *bcc_ptr;

	pSMB = (SESSION_SETUP_ANDX *)sess_data->iov[0].iov_base;

	capabilities = cifs_ssetup_hdr(ses, pSMB);
	if ((pSMB->req.hdr.Flags2 & SMBFLG2_UNICODE) == 0) {
		cifs_dbg(VFS, "NTLMSSP requires Unicode support\n");
		return -ENOSYS;
	}

	pSMB->req.hdr.Flags2 |= SMBFLG2_EXT_SEC;
	capabilities |= CAP_EXTENDED_SECURITY;
	pSMB->req.Capabilities |= cpu_to_le32(capabilities);

	bcc_ptr = sess_data->iov[2].iov_base;
	/* unicode strings must be word aligned */
	if ((sess_data->iov[0].iov_len + sess_data->iov[1].iov_len) % 2) {
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
	__u16 bytes_remaining;
	char *bcc_ptr;
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
	build_ntlmssp_negotiate_blob(pSMB->req.SecurityBlob, ses);
	sess_data->iov[1].iov_len = sizeof(NEGOTIATE_MESSAGE);
	sess_data->iov[1].iov_base = pSMB->req.SecurityBlob;
	pSMB->req.SecurityBlobLength = cpu_to_le16(sizeof(NEGOTIATE_MESSAGE));

	rc = _sess_auth_rawntlmssp_assemble_req(sess_data);
	if (rc)
		goto out;

	rc = sess_sendreceive(sess_data);

	pSMB = (SESSION_SETUP_ANDX *)sess_data->iov[0].iov_base;
	smb_buf = (struct smb_hdr *)sess_data->iov[0].iov_base;

	/* If true, rc here is expected and not an error */
	if (sess_data->buf0_type != CIFS_NO_BUFFER &&
	    smb_buf->Status.CifsError ==
			cpu_to_le32(NT_STATUS_MORE_PROCESSING_REQUIRED))
		rc = 0;

	if (rc)
		goto out;

	cifs_dbg(FYI, "rawntlmssp session setup challenge phase\n");

	if (smb_buf->WordCount != 4) {
		rc = -EIO;
		cifs_dbg(VFS, "bad word count %d\n", smb_buf->WordCount);
		goto out;
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
		goto out;
	}

	rc = decode_ntlmssp_challenge(bcc_ptr, blob_len, ses);
out:
	sess_free_buffer(sess_data);

	if (!rc) {
		sess_data->func = sess_auth_rawntlmssp_authenticate;
		return;
	}

	/* Else error. Cleanup */
	kfree(ses->auth_key.response);
	ses->auth_key.response = NULL;
	kfree(ses->ntlmssp);
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
					&blob_len, ses, sess_data->nls_cp);
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
		if (((unsigned long) bcc_ptr - (unsigned long) smb_buf) % 2) {
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
	kfree(ntlmsspblob);
out:
	sess_free_buffer(sess_data);

	 if (!rc)
		rc = sess_establish_session(sess_data);

	/* Cleanup */
	kfree(ses->auth_key.response);
	ses->auth_key.response = NULL;
	kfree(ses->ntlmssp);
	ses->ntlmssp = NULL;

	sess_data->func = NULL;
	sess_data->result = rc;
}

static int select_sec(struct cifs_ses *ses, struct sess_data *sess_data)
{
	int type;

	type = cifs_select_sectype(ses->server, ses->sectype);
	cifs_dbg(FYI, "sess setup type %d\n", type);
	if (type == Unspecified) {
		cifs_dbg(VFS, "Unable to select appropriate authentication method!\n");
		return -EINVAL;
	}

	switch (type) {
	case LANMAN:
		/* LANMAN and plaintext are less secure and off by default.
		 * So we make this explicitly be turned on in kconfig (in the
		 * build) and turned on at runtime (changed from the default)
		 * in proc/fs/cifs or via mount parm.  Unfortunately this is
		 * needed for old Win (e.g. Win95), some obscure NAS and OS/2 */
#ifdef CONFIG_CIFS_WEAK_PW_HASH
		sess_data->func = sess_auth_lanman;
		break;
#else
		return -EOPNOTSUPP;
#endif
	case NTLM:
		sess_data->func = sess_auth_ntlm;
		break;
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

	rc = select_sec(ses, sess_data);
	if (rc)
		goto out;

	sess_data->xid = xid;
	sess_data->ses = ses;
	sess_data->buf0_type = CIFS_NO_BUFFER;
	sess_data->nls_cp = (struct nls_table *) nls_cp;

	while (sess_data->func)
		sess_data->func(sess_data);

	/* Store result before we free sess_data */
	rc = sess_data->result;

out:
	kfree(sess_data);
	return rc;
}
