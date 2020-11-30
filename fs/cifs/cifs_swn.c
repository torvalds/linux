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

	struct cifs_tcon *tcon;
};

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
static int cifs_swn_send_register_message(struct cifs_swn_reg *swnreg)
{
	struct sk_buff *skb;
	struct genlmsghdr *hdr;
	enum securityEnum authtype;
	int ret;

	skb = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (skb == NULL) {
		ret = -ENOMEM;
		goto fail;
	}

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

	ret = nla_put(skb, CIFS_GENL_ATTR_SWN_IP, sizeof(struct sockaddr_storage),
			&swnreg->tcon->ses->server->dstaddr);
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

	authtype = cifs_select_sectype(swnreg->tcon->ses->server, swnreg->tcon->ses->sectype);
	switch (authtype) {
	case Kerberos:
		ret = cifs_swn_auth_info_krb(swnreg->tcon, skb);
		if (ret < 0) {
			cifs_dbg(VFS, "%s: Failed to get kerberos auth info: %d\n", __func__, ret);
			goto nlmsg_fail;
		}
		break;
	case LANMAN:
	case NTLM:
	case NTLMv2:
	case RawNTLMSSP:
		ret = cifs_swn_auth_info_ntlm(swnreg->tcon, skb);
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
fail:
	return ret;
}

/*
 * Sends an uregister message to the userspace daemon based on the registration
 */
static int cifs_swn_send_unregister_message(struct cifs_swn_reg *swnreg)
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
			&swnreg->tcon->ses->server->dstaddr);
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
 * Try to find a matching registration for the tcon's server name and share name.
 * Calls to this funciton must be protected by cifs_swnreg_idr_mutex.
 * TODO Try to avoid memory allocations
 */
static struct cifs_swn_reg *cifs_find_swn_reg(struct cifs_tcon *tcon)
{
	struct cifs_swn_reg *swnreg;
	int id;
	const char *share_name;
	const char *net_name;

	net_name = extract_hostname(tcon->treeName);
	if (IS_ERR_OR_NULL(net_name)) {
		int ret;

		ret = PTR_ERR(net_name);
		cifs_dbg(VFS, "%s: failed to extract host name from target '%s': %d\n",
				__func__, tcon->treeName, ret);
		return NULL;
	}

	share_name = extract_sharename(tcon->treeName);
	if (IS_ERR_OR_NULL(share_name)) {
		int ret;

		ret = PTR_ERR(net_name);
		cifs_dbg(VFS, "%s: failed to extract share name from target '%s': %d\n",
				__func__, tcon->treeName, ret);
		kfree(net_name);
		return NULL;
	}

	idr_for_each_entry(&cifs_swnreg_idr, swnreg, id) {
		if (strcasecmp(swnreg->net_name, net_name) != 0
		    || strcasecmp(swnreg->share_name, share_name) != 0) {
			continue;
		}

		mutex_unlock(&cifs_swnreg_idr_mutex);

		cifs_dbg(FYI, "Existing swn registration for %s:%s found\n", swnreg->net_name,
				swnreg->share_name);

		kfree(net_name);
		kfree(share_name);

		return swnreg;
	}

	kfree(net_name);
	kfree(share_name);

	return NULL;
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
	if (IS_ERR(reg)) {
		return reg;
	} else if (reg != NULL) {
		kref_get(&reg->ref_count);
		mutex_unlock(&cifs_swnreg_idr_mutex);
		return reg;
	}

	reg = kmalloc(sizeof(struct cifs_swn_reg), GFP_ATOMIC);
	if (reg == NULL) {
		mutex_unlock(&cifs_swnreg_idr_mutex);
		return ERR_PTR(-ENOMEM);
	}

	kref_init(&reg->ref_count);

	reg->id = idr_alloc(&cifs_swnreg_idr, reg, 1, 0, GFP_ATOMIC);
	if (reg->id < 0) {
		cifs_dbg(FYI, "%s: failed to allocate registration id\n", __func__);
		ret = reg->id;
		goto fail;
	}

	reg->net_name = extract_hostname(tcon->treeName);
	if (IS_ERR(reg->net_name)) {
		ret = PTR_ERR(reg->net_name);
		cifs_dbg(VFS, "%s: failed to extract host name from target: %d\n", __func__, ret);
		goto fail_idr;
	}

	reg->share_name = extract_sharename(tcon->treeName);
	if (IS_ERR(reg->share_name)) {
		ret = PTR_ERR(reg->share_name);
		cifs_dbg(VFS, "%s: failed to extract share name from target: %d\n", __func__, ret);
		goto fail_net_name;
	}

	reg->net_name_notify = true;
	reg->share_name_notify = true;
	reg->ip_notify = (tcon->capabilities & SMB2_SHARE_CAP_SCALEOUT);

	reg->tcon = tcon;

	mutex_unlock(&cifs_swnreg_idr_mutex);

	return reg;

fail_net_name:
	kfree(reg->net_name);
fail_idr:
	idr_remove(&cifs_swnreg_idr, reg->id);
fail:
	kfree(reg);
	mutex_unlock(&cifs_swnreg_idr_mutex);
	return ERR_PTR(ret);
}

static void cifs_swn_reg_release(struct kref *ref)
{
	struct cifs_swn_reg *swnreg = container_of(ref, struct cifs_swn_reg, ref_count);
	int ret;

	ret = cifs_swn_send_unregister_message(swnreg);
	if (ret < 0)
		cifs_dbg(VFS, "%s: Failed to send unregister message: %d\n", __func__, ret);

	idr_remove(&cifs_swnreg_idr, swnreg->id);
	kfree(swnreg->net_name);
	kfree(swnreg->share_name);
	kfree(swnreg);
}

static void cifs_put_swn_reg(struct cifs_swn_reg *swnreg)
{
	mutex_lock(&cifs_swnreg_idr_mutex);
	kref_put(&swnreg->ref_count, cifs_swn_reg_release);
	mutex_unlock(&cifs_swnreg_idr_mutex);
}

static int cifs_swn_resource_state_changed(struct cifs_swn_reg *swnreg, const char *name, int state)
{
	int i;

	switch (state) {
	case CIFS_SWN_RESOURCE_STATE_UNAVAILABLE:
		cifs_dbg(FYI, "%s: resource name '%s' become unavailable\n", __func__, name);
		for (i = 0; i < swnreg->tcon->ses->chan_count; i++) {
			spin_lock(&GlobalMid_Lock);
			if (swnreg->tcon->ses->chans[i].server->tcpStatus != CifsExiting)
				swnreg->tcon->ses->chans[i].server->tcpStatus = CifsNeedReconnect;
			spin_unlock(&GlobalMid_Lock);
		}
		break;
	case CIFS_SWN_RESOURCE_STATE_AVAILABLE:
		cifs_dbg(FYI, "%s: resource name '%s' become available\n", __func__, name);
		for (i = 0; i < swnreg->tcon->ses->chan_count; i++) {
			spin_lock(&GlobalMid_Lock);
			if (swnreg->tcon->ses->chans[i].server->tcpStatus != CifsExiting)
				swnreg->tcon->ses->chans[i].server->tcpStatus = CifsNeedReconnect;
			spin_unlock(&GlobalMid_Lock);
		}
		break;
	case CIFS_SWN_RESOURCE_STATE_UNKNOWN:
		cifs_dbg(FYI, "%s: resource name '%s' changed to unknown state\n", __func__, name);
		break;
	}
	return 0;
}

int cifs_swn_notify(struct sk_buff *skb, struct genl_info *info)
{
	struct cifs_swn_reg *swnreg;
	char name[256];
	int type;

	if (info->attrs[CIFS_GENL_ATTR_SWN_REGISTRATION_ID]) {
		int swnreg_id;

		swnreg_id = nla_get_u32(info->attrs[CIFS_GENL_ATTR_SWN_REGISTRATION_ID]);
		mutex_lock(&cifs_swnreg_idr_mutex);
		swnreg = idr_find(&cifs_swnreg_idr, swnreg_id);
		mutex_unlock(&cifs_swnreg_idr_mutex);
		if (swnreg == NULL) {
			cifs_dbg(FYI, "%s: registration id %d not found\n", __func__, swnreg_id);
			return -EINVAL;
		}
	} else {
		cifs_dbg(FYI, "%s: missing registration id attribute\n", __func__);
		return -EINVAL;
	}

	if (info->attrs[CIFS_GENL_ATTR_SWN_NOTIFICATION_TYPE]) {
		type = nla_get_u32(info->attrs[CIFS_GENL_ATTR_SWN_NOTIFICATION_TYPE]);
	} else {
		cifs_dbg(FYI, "%s: missing notification type attribute\n", __func__);
		return -EINVAL;
	}

	switch (type) {
	case CIFS_SWN_NOTIFICATION_RESOURCE_CHANGE: {
		int state;

		if (info->attrs[CIFS_GENL_ATTR_SWN_RESOURCE_NAME]) {
			nla_strlcpy(name, info->attrs[CIFS_GENL_ATTR_SWN_RESOURCE_NAME],
					sizeof(name));
		} else {
			cifs_dbg(FYI, "%s: missing resource name attribute\n", __func__);
			return -EINVAL;
		}
		if (info->attrs[CIFS_GENL_ATTR_SWN_RESOURCE_STATE]) {
			state = nla_get_u32(info->attrs[CIFS_GENL_ATTR_SWN_RESOURCE_STATE]);
		} else {
			cifs_dbg(FYI, "%s: missing resource state attribute\n", __func__);
			return -EINVAL;
		}
		return cifs_swn_resource_state_changed(swnreg, name, state);
	}
	default:
		cifs_dbg(FYI, "%s: unknown notification type %d\n", __func__, type);
		break;
	}

	return 0;
}

int cifs_swn_register(struct cifs_tcon *tcon)
{
	struct cifs_swn_reg *swnreg;
	int ret;

	swnreg = cifs_get_swn_reg(tcon);
	if (IS_ERR(swnreg))
		return PTR_ERR(swnreg);

	ret = cifs_swn_send_register_message(swnreg);
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
	if (swnreg == NULL) {
		mutex_unlock(&cifs_swnreg_idr_mutex);
		return -EEXIST;
	}

	mutex_unlock(&cifs_swnreg_idr_mutex);

	cifs_put_swn_reg(swnreg);

	return 0;
}
