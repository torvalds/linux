// SPDX-License-Identifier: GPL-2.0
/*
 * Witness Service client for CIFS
 *
 * Copyright (c) 2020 Samuel Cabrero <scabrero@suse.de>
 */

#include <linux/kref.h>
#include <net/genetlink.h>
#include <uapi/linux/cifs/cifs_netlink.h>

#include "cifs_swn.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "fscache.h"
#include "cifs_debug.h"
#include "netlink.h"

static DEFINE_IDR(cifs_swnreg_idr);
static DEFINE_MUTEX(cifs_swnreg_idr_mutex);

struct cifs_swn_reg {
	int id;
	struct kref ref_count;

	const char *net_name;
	const char *share_name;
	bool net_name_notify;
	bool share_name_notify;
	bool ip_notify;
};

struct cifs_swn_reg_info {
	int id;
	unsigned int ref_count;
	const char *net_name;
	const char *share_name;
	bool net_name_notify;
	bool share_name_notify;
	bool ip_notify;
};

static void cifs_swn_snapshot_reg(struct cifs_swn_reg *swnreg,
				  struct cifs_swn_reg_info *info)
{
	info->id = swnreg->id;
	info->ref_count = kref_read(&swnreg->ref_count);
	info->net_name = swnreg->net_name;
	info->share_name = swnreg->share_name;
	info->net_name_notify = swnreg->net_name_notify;
	info->share_name_notify = swnreg->share_name_notify;
	info->ip_notify = swnreg->ip_notify;
}

static int cifs_swn_dup_reg(struct cifs_swn_reg *swnreg,
			    struct cifs_swn_reg_info *info)
{
	cifs_swn_snapshot_reg(swnreg, info);

	info->net_name = kstrdup(swnreg->net_name, GFP_KERNEL);
	if (!info->net_name)
		return -ENOMEM;

	info->share_name = kstrdup(swnreg->share_name, GFP_KERNEL);
	if (!info->share_name) {
		kfree(info->net_name);
		return -ENOMEM;
	}

	return 0;
}

static void cifs_swn_free_reg_info(struct cifs_swn_reg_info *info)
{
	kfree(info->net_name);
	kfree(info->share_name);
}

static int cifs_swn_auth_info_krb(struct cifs_tcon *tcon, struct sk_buff *skb)
{
	int ret;

	ret = nla_put_flag(skb, CIFS_GENL_ATTR_SWN_KRB_AUTH);
	if (ret < 0)
		return ret;

	return 0;
}

static int cifs_swn_auth_info_ntlm(struct cifs_tcon *tcon, struct sk_buff *skb)
{
	int ret;

	if (tcon->ses->user_name != NULL) {
		ret = nla_put_string(skb, CIFS_GENL_ATTR_SWN_USER_NAME, tcon->ses->user_name);
		if (ret < 0)
			return ret;
	}

	if (tcon->ses->password != NULL) {
		ret = nla_put_string(skb, CIFS_GENL_ATTR_SWN_PASSWORD, tcon->ses->password);
		if (ret < 0)
			return ret;
	}

	if (tcon->ses->domainName != NULL) {
		ret = nla_put_string(skb, CIFS_GENL_ATTR_SWN_DOMAIN_NAME, tcon->ses->domainName);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/*
 * Sends a register message to the userspace daemon based on the registration.
 * The authentication information to connect to the witness service is bundled
 * into the message.
 */
static int cifs_swn_send_register_message(struct cifs_swn_reg_info *swnreg,
					  struct cifs_tcon *tcon)
{
	struct sk_buff *skb;
	struct genlmsghdr *hdr;
	enum securityEnum authtype;
	struct sockaddr_storage *addr;
	int ret;

	skb = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	hdr = genlmsg_put(skb, 0, 0, &cifs_genl_family, 0, CIFS_GENL_CMD_SWN_REGISTER);
	if (hdr == NULL) {
		ret = -ENOMEM;
		goto nlmsg_fail;
	}

	ret = nla_put_u32(skb, CIFS_GENL_ATTR_SWN_REGISTRATION_ID, swnreg->id);
	if (ret < 0)
		goto nlmsg_fail;

	ret = nla_put_string(skb, CIFS_GENL_ATTR_SWN_NET_NAME, swnreg->net_name);
	if (ret < 0)
		goto nlmsg_fail;

	ret = nla_put_string(skb, CIFS_GENL_ATTR_SWN_SHARE_NAME, swnreg->share_name);
	if (ret < 0)
		goto nlmsg_fail;

	/*
	 * If there is an address stored use it instead of the server address, because we are
	 * in the process of reconnecting to it after a share has been moved or we have been
	 * told to switch to it (client move message). In these cases we unregister from the
	 * server address and register to the new address when we receive the notification.
	 */
	if (tcon->ses->server->use_swn_dstaddr)
		addr = &tcon->ses->server->swn_dstaddr;
	else
		addr = &tcon->ses->server->dstaddr;

	ret = nla_put(skb, CIFS_GENL_ATTR_SWN_IP, sizeof(struct sockaddr_storage), addr);
	if (ret < 0)
		goto nlmsg_fail;

	if (swnreg->net_name_notify) {
		ret = nla_put_flag(skb, CIFS_GENL_ATTR_SWN_NET_NAME_NOTIFY);
		if (ret < 0)
			goto nlmsg_fail;
	}

	if (swnreg->share_name_notify) {
		ret = nla_put_flag(skb, CIFS_GENL_ATTR_SWN_SHARE_NAME_NOTIFY);
		if (ret < 0)
			goto nlmsg_fail;
	}

	if (swnreg->ip_notify) {
		ret = nla_put_flag(skb, CIFS_GENL_ATTR_SWN_IP_NOTIFY);
		if (ret < 0)
			goto nlmsg_fail;
	}

	authtype = cifs_select_sectype(tcon->ses->server, tcon->ses->sectype);
	switch (authtype) {
	case Kerberos:
		ret = cifs_swn_auth_info_krb(tcon, skb);
		if (ret < 0) {
			cifs_dbg(VFS, "%s: Failed to get kerberos auth info: %d\n", __func__, ret);
			goto nlmsg_fail;
		}
		break;
	case NTLMv2:
	case RawNTLMSSP:
		ret = cifs_swn_auth_info_ntlm(tcon, skb);
		if (ret < 0) {
			cifs_dbg(VFS, "%s: Failed to get NTLM auth info: %d\n", __func__, ret);
			goto nlmsg_fail;
		}
		break;
	default:
		cifs_dbg(VFS, "%s: secType %d not supported!\n", __func__, authtype);
		ret = -EINVAL;
		goto nlmsg_fail;
	}

	genlmsg_end(skb, hdr);
	genlmsg_multicast(&cifs_genl_family, skb, 0, CIFS_GENL_MCGRP_SWN, GFP_ATOMIC);

	cifs_dbg(FYI, "%s: Message to register for network name %s with id %d sent\n", __func__,
			swnreg->net_name, swnreg->id);

	return 0;

nlmsg_fail:
	genlmsg_cancel(skb, hdr);
	nlmsg_free(skb);
	return ret;
}

/*
 * Sends an uregister message to the userspace daemon based on the registration
 */
static int cifs_swn_send_unregister_message(struct cifs_swn_reg_info *swnreg,
					    struct cifs_tcon *tcon)
{
	struct sk_buff *skb;
	struct genlmsghdr *hdr;
	int ret;

	skb = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (skb == NULL)
		return -ENOMEM;

	hdr = genlmsg_put(skb, 0, 0, &cifs_genl_family, 0, CIFS_GENL_CMD_SWN_UNREGISTER);
	if (hdr == NULL) {
		ret = -ENOMEM;
		goto nlmsg_fail;
	}

	ret = nla_put_u32(skb, CIFS_GENL_ATTR_SWN_REGISTRATION_ID, swnreg->id);
	if (ret < 0)
		goto nlmsg_fail;

	ret = nla_put_string(skb, CIFS_GENL_ATTR_SWN_NET_NAME, swnreg->net_name);
	if (ret < 0)
		goto nlmsg_fail;

	ret = nla_put_string(skb, CIFS_GENL_ATTR_SWN_SHARE_NAME, swnreg->share_name);
	if (ret < 0)
		goto nlmsg_fail;

	ret = nla_put(skb, CIFS_GENL_ATTR_SWN_IP, sizeof(struct sockaddr_storage),
			&tcon->ses->server->dstaddr);
	if (ret < 0)
		goto nlmsg_fail;

	if (swnreg->net_name_notify) {
		ret = nla_put_flag(skb, CIFS_GENL_ATTR_SWN_NET_NAME_NOTIFY);
		if (ret < 0)
			goto nlmsg_fail;
	}

	if (swnreg->share_name_notify) {
		ret = nla_put_flag(skb, CIFS_GENL_ATTR_SWN_SHARE_NAME_NOTIFY);
		if (ret < 0)
			goto nlmsg_fail;
	}

	if (swnreg->ip_notify) {
		ret = nla_put_flag(skb, CIFS_GENL_ATTR_SWN_IP_NOTIFY);
		if (ret < 0)
			goto nlmsg_fail;
	}

	genlmsg_end(skb, hdr);
	genlmsg_multicast(&cifs_genl_family, skb, 0, CIFS_GENL_MCGRP_SWN, GFP_ATOMIC);

	cifs_dbg(FYI, "%s: Message to unregister for network name %s with id %d sent\n", __func__,
			swnreg->net_name, swnreg->id);

	return 0;

nlmsg_fail:
	genlmsg_cancel(skb, hdr);
	nlmsg_free(skb);
	return ret;
}

/*
 * Allocation-free mirror of extract_hostname() + extract_sharename() from
 * fs/smb/client/unc.c.  Those helpers kmalloc(GFP_KERNEL); this runs under
 * cifs_tcp_ses_lock and tcon->tc_lock, both spinlocks, so we mirror their
 * parsing in place against the caller's stable net_name/share_name strings.
 * Keep in sync with unc.c.
 */
static bool cifs_swn_tcon_matches(struct cifs_tcon *tcon,
				  const char *net_name,
				  const char *share_name)
{
	const char *unc = tcon->tree_name;
	const char *host, *share, *delim;
	size_t host_len, share_len;

	if (!tcon->use_witness)
		return false;

	/* extract_hostname: require strlen(unc) >= 3 */
	if (strnlen(unc, 3) < 3)
		return false;
	/* extract_hostname: skip all leading '\' characters */
	for (host = unc; *host == '\\'; host++)
		;
	if (!*host)
		return false;
	delim = strchr(host, '\\');
	if (!delim)
		return false;
	host_len = delim - host;
	if (strlen(net_name) != host_len ||
	    strncasecmp(host, net_name, host_len))
		return false;

	/* extract_sharename: start at unc + 2, then first '\' onward */
	share = unc + 2;
	delim = strchr(share, '\\');
	if (!delim)
		return false;
	share = delim + 1;
	share_len = strlen(share);

	return strlen(share_name) == share_len &&
	       !strncasecmp(share, share_name, share_len);
}

/*
 * One SWN registration id represents one net/share name pair.  Multiple
 * mounted tcons can therefore share the id.  Pick a live representative at
 * use time instead of caching the first tcon pointer in the registration.
 */
static struct cifs_tcon *cifs_swn_get_tcon(struct cifs_swn_reg_info *swnreg)
{
	struct TCP_Server_Info *server;
	struct cifs_ses *ses;
	struct cifs_tcon *tcon;

	spin_lock(&cifs_tcp_ses_lock);
	list_for_each_entry(server, &cifs_tcp_ses_list, tcp_ses_list) {
		list_for_each_entry(ses, &server->smb_ses_list, smb_ses_list) {
			list_for_each_entry(tcon, &ses->tcon_list, tcon_list) {
				spin_lock(&tcon->tc_lock);
				if (tcon->status == TID_EXITING ||
				    !cifs_swn_tcon_matches(tcon, swnreg->net_name,
							   swnreg->share_name)) {
					spin_unlock(&tcon->tc_lock);
					continue;
				}
				++tcon->tc_count;
				trace_smb3_tcon_ref(tcon->debug_id,
						    tcon->tc_count,
						    netfs_trace_tcon_ref_get_swn_notify);
				spin_unlock(&tcon->tc_lock);
				spin_unlock(&cifs_tcp_ses_lock);
				return tcon;
			}
		}
	}
	spin_unlock(&cifs_tcp_ses_lock);
	return NULL;
}

/*
 * Try to find a matching registration for the tcon's server name and share name.
 * Calls to this function must be protected by cifs_swnreg_idr_mutex.
 * TODO Try to avoid memory allocations
 */
static struct cifs_swn_reg *cifs_find_swn_reg(struct cifs_tcon *tcon)
{
	struct cifs_swn_reg *swnreg;
	int id;
	const char *share_name;
	const char *net_name;

	net_name = extract_hostname(tcon->tree_name);
	if (IS_ERR(net_name)) {
		int ret;

		ret = PTR_ERR(net_name);
		cifs_dbg(VFS, "%s: failed to extract host name from target '%s': %d\n",
				__func__, tcon->tree_name, ret);
		return ERR_PTR(-EINVAL);
	}

	share_name = extract_sharename(tcon->tree_name);
	if (IS_ERR(share_name)) {
		int ret;

		ret = PTR_ERR(share_name);
		cifs_dbg(VFS, "%s: failed to extract share name from target '%s': %d\n",
				__func__, tcon->tree_name, ret);
		kfree(net_name);
		return ERR_PTR(-EINVAL);
	}

	idr_for_each_entry(&cifs_swnreg_idr, swnreg, id) {
		if (strcasecmp(swnreg->net_name, net_name) != 0
		    || strcasecmp(swnreg->share_name, share_name) != 0) {
			continue;
		}

		cifs_dbg(FYI, "Existing swn registration for %s:%s found\n", swnreg->net_name,
				swnreg->share_name);

		kfree(net_name);
		kfree(share_name);

		return swnreg;
	}

	kfree(net_name);
	kfree(share_name);

	return ERR_PTR(-EEXIST);
}

/*
 * Get a registration for the tcon's server and share name, allocating a new one if it does not
 * exists
 */
static struct cifs_swn_reg *cifs_get_swn_reg(struct cifs_tcon *tcon)
{
	struct cifs_swn_reg *reg = NULL;
	int ret;

	mutex_lock(&cifs_swnreg_idr_mutex);

	/* Check if we are already registered for this network and share names */
	reg = cifs_find_swn_reg(tcon);
	if (!IS_ERR(reg)) {
		kref_get(&reg->ref_count);
		goto unlock;
	} else if (PTR_ERR(reg) != -EEXIST) {
		goto unlock;
	}

	reg = kmalloc_obj(struct cifs_swn_reg, GFP_ATOMIC);
	if (reg == NULL) {
		ret = -ENOMEM;
		goto fail_unlock;
	}

	kref_init(&reg->ref_count);

	reg->id = idr_alloc(&cifs_swnreg_idr, reg, 1, 0, GFP_ATOMIC);
	if (reg->id < 0) {
		cifs_dbg(FYI, "%s: failed to allocate registration id\n", __func__);
		ret = reg->id;
		goto fail;
	}

	reg->net_name = extract_hostname(tcon->tree_name);
	if (IS_ERR(reg->net_name)) {
		ret = PTR_ERR(reg->net_name);
		cifs_dbg(VFS, "%s: failed to extract host name from target: %d\n", __func__, ret);
		goto fail_idr;
	}

	reg->share_name = extract_sharename(tcon->tree_name);
	if (IS_ERR(reg->share_name)) {
		ret = PTR_ERR(reg->share_name);
		cifs_dbg(VFS, "%s: failed to extract share name from target: %d\n", __func__, ret);
		goto fail_net_name;
	}

	reg->net_name_notify = true;
	reg->share_name_notify = true;
	reg->ip_notify = (tcon->capabilities & SMB2_SHARE_CAP_SCALEOUT);
unlock:
	mutex_unlock(&cifs_swnreg_idr_mutex);

	return reg;

fail_net_name:
	kfree(reg->net_name);
fail_idr:
	idr_remove(&cifs_swnreg_idr, reg->id);
fail:
	kfree(reg);
fail_unlock:
	mutex_unlock(&cifs_swnreg_idr_mutex);
	return ERR_PTR(ret);
}

static void cifs_swn_reg_release(struct kref *ref)
{
	struct cifs_swn_reg *swnreg = container_of(ref, struct cifs_swn_reg, ref_count);

	idr_remove(&cifs_swnreg_idr, swnreg->id);
	kfree(swnreg->net_name);
	kfree(swnreg->share_name);
	kfree(swnreg);
}

static void cifs_put_swn_reg_locked(struct cifs_swn_reg *swnreg,
				    struct cifs_tcon *tcon)
{
	if (kref_read(&swnreg->ref_count) == 1) {
		struct cifs_swn_reg_info swnreg_info;
		int ret;

		cifs_swn_snapshot_reg(swnreg, &swnreg_info);
		ret = cifs_swn_send_unregister_message(&swnreg_info, tcon);
		if (ret < 0)
			cifs_dbg(VFS, "%s: Failed to send unregister message: %d\n",
				 __func__, ret);
	}

	kref_put(&swnreg->ref_count, cifs_swn_reg_release);
}

static int cifs_swn_resource_state_changed(struct cifs_tcon *tcon, const char *name, int state)
{
	switch (state) {
	case CIFS_SWN_RESOURCE_STATE_UNAVAILABLE:
		cifs_dbg(FYI, "%s: resource name '%s' become unavailable\n", __func__, name);
		cifs_signal_cifsd_for_reconnect(tcon->ses->server, true);
		break;
	case CIFS_SWN_RESOURCE_STATE_AVAILABLE:
		cifs_dbg(FYI, "%s: resource name '%s' become available\n", __func__, name);
		cifs_signal_cifsd_for_reconnect(tcon->ses->server, true);
		break;
	case CIFS_SWN_RESOURCE_STATE_UNKNOWN:
		cifs_dbg(FYI, "%s: resource name '%s' changed to unknown state\n", __func__, name);
		break;
	}
	return 0;
}

static bool cifs_sockaddr_equal(struct sockaddr_storage *addr1, struct sockaddr_storage *addr2)
{
	if (addr1->ss_family != addr2->ss_family)
		return false;

	if (addr1->ss_family == AF_INET) {
		return (memcmp(&((const struct sockaddr_in *)addr1)->sin_addr,
				&((const struct sockaddr_in *)addr2)->sin_addr,
				sizeof(struct in_addr)) == 0);
	}

	if (addr1->ss_family == AF_INET6) {
		return (memcmp(&((const struct sockaddr_in6 *)addr1)->sin6_addr,
				&((const struct sockaddr_in6 *)addr2)->sin6_addr,
				sizeof(struct in6_addr)) == 0);
	}

	return false;
}

static int cifs_swn_store_swn_addr(const struct sockaddr_storage *new,
				   const struct sockaddr_storage *old,
				   struct sockaddr_storage *dst)
{
	__be16 port = cpu_to_be16(CIFS_PORT);

	if (old->ss_family == AF_INET) {
		struct sockaddr_in *ipv4 = (struct sockaddr_in *)old;

		port = ipv4->sin_port;
	} else if (old->ss_family == AF_INET6) {
		struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)old;

		port = ipv6->sin6_port;
	}

	if (new->ss_family == AF_INET) {
		struct sockaddr_in *ipv4 = (struct sockaddr_in *)new;

		ipv4->sin_port = port;
	} else if (new->ss_family == AF_INET6) {
		struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)new;

		ipv6->sin6_port = port;
	}

	*dst = *new;

	return 0;
}

static int cifs_swn_reconnect(struct cifs_tcon *tcon, struct sockaddr_storage *addr)
{
	int ret = 0;

	/* Store the reconnect address */
	cifs_server_lock(tcon->ses->server);
	if (cifs_sockaddr_equal(&tcon->ses->server->dstaddr, addr))
		goto unlock;

	ret = cifs_swn_store_swn_addr(addr, &tcon->ses->server->dstaddr,
				      &tcon->ses->server->swn_dstaddr);
	if (ret < 0) {
		cifs_dbg(VFS, "%s: failed to store address: %d\n", __func__, ret);
		goto unlock;
	}
	tcon->ses->server->use_swn_dstaddr = true;

	/*
	 * Unregister to stop receiving notifications for the old IP address.
	 */
	ret = cifs_swn_unregister(tcon);
	if (ret < 0) {
		cifs_dbg(VFS, "%s: Failed to unregister for witness notifications: %d\n",
			 __func__, ret);
		goto unlock;
	}

	/*
	 * And register to receive notifications for the new IP address now that we have
	 * stored the new address.
	 */
	ret = cifs_swn_register(tcon);
	if (ret < 0) {
		cifs_dbg(VFS, "%s: Failed to register for witness notifications: %d\n",
			 __func__, ret);
		goto unlock;
	}

	cifs_signal_cifsd_for_reconnect(tcon->ses->server, false);

unlock:
	cifs_server_unlock(tcon->ses->server);

	return ret;
}

static int cifs_swn_client_move(struct cifs_tcon *tcon, struct sockaddr_storage *addr)
{
	struct sockaddr_in *ipv4 = (struct sockaddr_in *)addr;
	struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)addr;

	if (addr->ss_family == AF_INET)
		cifs_dbg(FYI, "%s: move to %pI4\n", __func__, &ipv4->sin_addr);
	else if (addr->ss_family == AF_INET6)
		cifs_dbg(FYI, "%s: move to %pI6\n", __func__, &ipv6->sin6_addr);

	return cifs_swn_reconnect(tcon, addr);
}

int cifs_swn_notify(struct sk_buff *skb, struct genl_info *info)
{
	struct cifs_swn_reg *swnreg;
	struct cifs_swn_reg_info swnreg_info;
	struct cifs_tcon *tcon;
	char name[256];
	int type;
	int ret = 0;

	if (info->attrs[CIFS_GENL_ATTR_SWN_REGISTRATION_ID]) {
		int swnreg_id;

		swnreg_id = nla_get_u32(info->attrs[CIFS_GENL_ATTR_SWN_REGISTRATION_ID]);
		mutex_lock(&cifs_swnreg_idr_mutex);
		swnreg = idr_find(&cifs_swnreg_idr, swnreg_id);
		if (swnreg == NULL) {
			mutex_unlock(&cifs_swnreg_idr_mutex);
			cifs_dbg(FYI, "%s: registration id %d not found\n", __func__, swnreg_id);
			return -EINVAL;
		}
		ret = cifs_swn_dup_reg(swnreg, &swnreg_info);
		mutex_unlock(&cifs_swnreg_idr_mutex);
		if (ret)
			return ret;
	} else {
		cifs_dbg(FYI, "%s: missing registration id attribute\n", __func__);
		return -EINVAL;
	}

	tcon = cifs_swn_get_tcon(&swnreg_info);
	if (!tcon) {
		cifs_dbg(FYI, "%s: registration id %d has no live tcon\n",
			 __func__, swnreg_info.id);
		ret = -ENODEV;
		goto free_info;
	}

	if (info->attrs[CIFS_GENL_ATTR_SWN_NOTIFICATION_TYPE]) {
		type = nla_get_u32(info->attrs[CIFS_GENL_ATTR_SWN_NOTIFICATION_TYPE]);
	} else {
		cifs_dbg(FYI, "%s: missing notification type attribute\n", __func__);
		ret = -EINVAL;
		goto out;
	}

	switch (type) {
	case CIFS_SWN_NOTIFICATION_RESOURCE_CHANGE: {
		int state;

		if (info->attrs[CIFS_GENL_ATTR_SWN_RESOURCE_NAME]) {
			nla_strscpy(name, info->attrs[CIFS_GENL_ATTR_SWN_RESOURCE_NAME],
					sizeof(name));
		} else {
			cifs_dbg(FYI, "%s: missing resource name attribute\n", __func__);
			ret = -EINVAL;
			goto out;
		}
		if (info->attrs[CIFS_GENL_ATTR_SWN_RESOURCE_STATE]) {
			state = nla_get_u32(info->attrs[CIFS_GENL_ATTR_SWN_RESOURCE_STATE]);
		} else {
			cifs_dbg(FYI, "%s: missing resource state attribute\n", __func__);
			ret = -EINVAL;
			goto out;
		}
		ret = cifs_swn_resource_state_changed(tcon, name, state);
		break;
	}
	case CIFS_SWN_NOTIFICATION_CLIENT_MOVE: {
		struct sockaddr_storage addr;

		if (info->attrs[CIFS_GENL_ATTR_SWN_IP]) {
			nla_memcpy(&addr, info->attrs[CIFS_GENL_ATTR_SWN_IP], sizeof(addr));
		} else {
			cifs_dbg(FYI, "%s: missing IP address attribute\n", __func__);
			ret = -EINVAL;
			goto out;
		}
		ret = cifs_swn_client_move(tcon, &addr);
		break;
	}
	default:
		cifs_dbg(FYI, "%s: unknown notification type %d\n", __func__, type);
		break;
	}

out:
	cifs_put_tcon(tcon, netfs_trace_tcon_ref_put_swn_notify);
free_info:
	cifs_swn_free_reg_info(&swnreg_info);
	return ret;
}

int cifs_swn_register(struct cifs_tcon *tcon)
{
	struct cifs_swn_reg *swnreg;
	struct cifs_swn_reg_info swnreg_info;
	int ret;

	swnreg = cifs_get_swn_reg(tcon);
	if (IS_ERR(swnreg))
		return PTR_ERR(swnreg);

	cifs_swn_snapshot_reg(swnreg, &swnreg_info);
	ret = cifs_swn_send_register_message(&swnreg_info, tcon);
	if (ret < 0) {
		cifs_dbg(VFS, "%s: Failed to send swn register message: %d\n", __func__, ret);
		/* Do not put the swnreg or return error, the echo task will retry */
	}

	return 0;
}

int cifs_swn_unregister(struct cifs_tcon *tcon)
{
	struct cifs_swn_reg *swnreg;

	mutex_lock(&cifs_swnreg_idr_mutex);

	swnreg = cifs_find_swn_reg(tcon);
	if (IS_ERR(swnreg)) {
		mutex_unlock(&cifs_swnreg_idr_mutex);
		return PTR_ERR(swnreg);
	}

	cifs_put_swn_reg_locked(swnreg, tcon);
	mutex_unlock(&cifs_swnreg_idr_mutex);

	return 0;
}

/*
 * Snapshot one registration under cifs_swnreg_idr_mutex and return.  Callers
 * intentionally do the per-registration network/genlmsg work without the
 * mutex held, both to keep the critical section short and to avoid nesting
 * cifs_swnreg_idr_mutex inside the higher tc_lock when a live tcon is then
 * pinned for the send.
 */
static int cifs_swn_get_next_reg_info(int *id, struct cifs_swn_reg_info *info)
{
	struct cifs_swn_reg *swnreg;
	int ret = 0;

	mutex_lock(&cifs_swnreg_idr_mutex);
	swnreg = idr_get_next(&cifs_swnreg_idr, id);
	if (swnreg) {
		ret = cifs_swn_dup_reg(swnreg, info);
		if (!ret) {
			*id = swnreg->id + 1;
			ret = 1;
		}
	}
	mutex_unlock(&cifs_swnreg_idr_mutex);

	return ret;
}

void cifs_swn_dump(struct seq_file *m)
{
	struct cifs_swn_reg_info swnreg_info;
	struct cifs_tcon *tcon;
	struct sockaddr_in *sa;
	struct sockaddr_in6 *sa6;
	int id = 0;
	int ret;

	seq_puts(m, "Witness registrations:");

	while ((ret = cifs_swn_get_next_reg_info(&id, &swnreg_info)) > 0) {
		seq_printf(m, "\nId: %u Refs: %u Network name: '%s'%s Share name: '%s'%s Ip address: ",
			   swnreg_info.id, swnreg_info.ref_count,
			   swnreg_info.net_name, swnreg_info.net_name_notify ? "(y)" : "(n)",
			   swnreg_info.share_name, swnreg_info.share_name_notify ? "(y)" : "(n)");

		tcon = cifs_swn_get_tcon(&swnreg_info);
		if (!tcon) {
			seq_puts(m, "(no live tcon)");
			goto next;
		}

		switch (tcon->ses->server->dstaddr.ss_family) {
		case AF_INET:
			sa = (struct sockaddr_in *)&tcon->ses->server->dstaddr;
			seq_printf(m, "%pI4", &sa->sin_addr.s_addr);
			break;
		case AF_INET6:
			sa6 = (struct sockaddr_in6 *)&tcon->ses->server->dstaddr;
			seq_printf(m, "%pI6", &sa6->sin6_addr.s6_addr);
			if (sa6->sin6_scope_id)
				seq_printf(m, "%%%u", sa6->sin6_scope_id);
			break;
		default:
			seq_puts(m, "(unknown)");
		}
		cifs_put_tcon(tcon, netfs_trace_tcon_ref_put_swn_notify);
next:
		seq_printf(m, "%s", swnreg_info.ip_notify ? "(y)" : "(n)");
		cifs_swn_free_reg_info(&swnreg_info);
	}
	if (ret < 0)
		seq_printf(m, "\nFailed to snapshot witness registration: %d", ret);
	seq_puts(m, "\n");
}

void cifs_swn_check(void)
{
	struct cifs_swn_reg_info swnreg_info;
	struct cifs_tcon *tcon;
	int id = 0;
	int ret;

	while ((ret = cifs_swn_get_next_reg_info(&id, &swnreg_info)) > 0) {
		tcon = cifs_swn_get_tcon(&swnreg_info);
		if (!tcon) {
			cifs_dbg(FYI, "%s: registration id %d has no live tcon\n",
				 __func__, swnreg_info.id);
			goto free_info;
		}

		ret = cifs_swn_send_register_message(&swnreg_info, tcon);
		if (ret < 0)
			cifs_dbg(FYI, "%s: Failed to send register message: %d\n", __func__, ret);
		cifs_put_tcon(tcon, netfs_trace_tcon_ref_put_swn_notify);
free_info:
		cifs_swn_free_reg_info(&swnreg_info);
	}
	if (ret < 0)
		cifs_dbg(FYI, "%s: Failed to snapshot registration: %d\n", __func__, ret);
}
