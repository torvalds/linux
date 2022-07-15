// SPDX-License-Identifier: GPL-2.0
/*
 *  Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  CLC (connection layer control) handshake over initial TCP socket to
 *  prepare for RDMA traffic
 *
 *  Copyright IBM Corp. 2016, 2018
 *
 *  Author(s):  Ursula Braun <ubraun@linux.vnet.ibm.com>
 */

#include <linux/in.h>
#include <linux/inetdevice.h>
#include <linux/if_ether.h>
#include <linux/sched/signal.h>
#include <linux/utsname.h>
#include <linux/ctype.h>

#include <net/addrconf.h>
#include <net/sock.h>
#include <net/tcp.h>

#include "smc.h"
#include "smc_core.h"
#include "smc_clc.h"
#include "smc_ib.h"
#include "smc_ism.h"

#define SMCR_CLC_ACCEPT_CONFIRM_LEN 68
#define SMCD_CLC_ACCEPT_CONFIRM_LEN 48
#define SMCD_CLC_ACCEPT_CONFIRM_LEN_V2 78
#define SMC_CLC_RECV_BUF_LEN	100

/* eye catcher "SMCR" EBCDIC for CLC messages */
static const char SMC_EYECATCHER[4] = {'\xe2', '\xd4', '\xc3', '\xd9'};
/* eye catcher "SMCD" EBCDIC for CLC messages */
static const char SMCD_EYECATCHER[4] = {'\xe2', '\xd4', '\xc3', '\xc4'};

static u8 smc_hostname[SMC_MAX_HOSTNAME_LEN];

/* check arriving CLC proposal */
static bool smc_clc_msg_prop_valid(struct smc_clc_msg_proposal *pclc)
{
	struct smc_clc_msg_proposal_prefix *pclc_prfx;
	struct smc_clc_smcd_v2_extension *smcd_v2_ext;
	struct smc_clc_msg_hdr *hdr = &pclc->hdr;
	struct smc_clc_v2_extension *v2_ext;

	v2_ext = smc_get_clc_v2_ext(pclc);
	pclc_prfx = smc_clc_proposal_get_prefix(pclc);
	if (hdr->version == SMC_V1) {
		if (hdr->typev1 == SMC_TYPE_N)
			return false;
		if (ntohs(hdr->length) !=
			sizeof(*pclc) + ntohs(pclc->iparea_offset) +
			sizeof(*pclc_prfx) +
			pclc_prfx->ipv6_prefixes_cnt *
				sizeof(struct smc_clc_ipv6_prefix) +
			sizeof(struct smc_clc_msg_trail))
			return false;
	} else {
		if (ntohs(hdr->length) !=
			sizeof(*pclc) +
			sizeof(struct smc_clc_msg_smcd) +
			(hdr->typev1 != SMC_TYPE_N ?
				sizeof(*pclc_prfx) +
				pclc_prfx->ipv6_prefixes_cnt *
				sizeof(struct smc_clc_ipv6_prefix) : 0) +
			(hdr->typev2 != SMC_TYPE_N ?
				sizeof(*v2_ext) +
				v2_ext->hdr.eid_cnt * SMC_MAX_EID_LEN : 0) +
			(smcd_indicated(hdr->typev2) ?
				sizeof(*smcd_v2_ext) + v2_ext->hdr.ism_gid_cnt *
					sizeof(struct smc_clc_smcd_gid_chid) :
				0) +
			sizeof(struct smc_clc_msg_trail))
			return false;
	}
	return true;
}

/* check arriving CLC accept or confirm */
static bool
smc_clc_msg_acc_conf_valid(struct smc_clc_msg_accept_confirm_v2 *clc_v2)
{
	struct smc_clc_msg_hdr *hdr = &clc_v2->hdr;

	if (hdr->typev1 != SMC_TYPE_R && hdr->typev1 != SMC_TYPE_D)
		return false;
	if (hdr->version == SMC_V1) {
		if ((hdr->typev1 == SMC_TYPE_R &&
		     ntohs(hdr->length) != SMCR_CLC_ACCEPT_CONFIRM_LEN) ||
		    (hdr->typev1 == SMC_TYPE_D &&
		     ntohs(hdr->length) != SMCD_CLC_ACCEPT_CONFIRM_LEN))
			return false;
	} else {
		if (hdr->typev1 == SMC_TYPE_D &&
		    ntohs(hdr->length) != SMCD_CLC_ACCEPT_CONFIRM_LEN_V2 &&
		    (ntohs(hdr->length) != SMCD_CLC_ACCEPT_CONFIRM_LEN_V2 +
				sizeof(struct smc_clc_first_contact_ext)))
			return false;
	}
	return true;
}

static void smc_clc_fill_fce(struct smc_clc_first_contact_ext *fce, int *len)
{
	memset(fce, 0, sizeof(*fce));
	fce->os_type = SMC_CLC_OS_LINUX;
	fce->release = SMC_RELEASE;
	memcpy(fce->hostname, smc_hostname, sizeof(smc_hostname));
	(*len) += sizeof(*fce);
}

/* check if received message has a correct header length and contains valid
 * heading and trailing eyecatchers
 */
static bool smc_clc_msg_hdr_valid(struct smc_clc_msg_hdr *clcm, bool check_trl)
{
	struct smc_clc_msg_accept_confirm_v2 *clc_v2;
	struct smc_clc_msg_proposal *pclc;
	struct smc_clc_msg_decline *dclc;
	struct smc_clc_msg_trail *trl;

	if (memcmp(clcm->eyecatcher, SMC_EYECATCHER, sizeof(SMC_EYECATCHER)) &&
	    memcmp(clcm->eyecatcher, SMCD_EYECATCHER, sizeof(SMCD_EYECATCHER)))
		return false;
	switch (clcm->type) {
	case SMC_CLC_PROPOSAL:
		pclc = (struct smc_clc_msg_proposal *)clcm;
		if (!smc_clc_msg_prop_valid(pclc))
			return false;
		trl = (struct smc_clc_msg_trail *)
			((u8 *)pclc + ntohs(pclc->hdr.length) - sizeof(*trl));
		break;
	case SMC_CLC_ACCEPT:
	case SMC_CLC_CONFIRM:
		clc_v2 = (struct smc_clc_msg_accept_confirm_v2 *)clcm;
		if (!smc_clc_msg_acc_conf_valid(clc_v2))
			return false;
		trl = (struct smc_clc_msg_trail *)
			((u8 *)clc_v2 + ntohs(clc_v2->hdr.length) -
							sizeof(*trl));
		break;
	case SMC_CLC_DECLINE:
		dclc = (struct smc_clc_msg_decline *)clcm;
		if (ntohs(dclc->hdr.length) != sizeof(*dclc))
			return false;
		trl = &dclc->trl;
		break;
	default:
		return false;
	}
	if (check_trl &&
	    memcmp(trl->eyecatcher, SMC_EYECATCHER, sizeof(SMC_EYECATCHER)) &&
	    memcmp(trl->eyecatcher, SMCD_EYECATCHER, sizeof(SMCD_EYECATCHER)))
		return false;
	return true;
}

/* find ipv4 addr on device and get the prefix len, fill CLC proposal msg */
static int smc_clc_prfx_set4_rcu(struct dst_entry *dst, __be32 ipv4,
				 struct smc_clc_msg_proposal_prefix *prop)
{
	struct in_device *in_dev = __in_dev_get_rcu(dst->dev);
	const struct in_ifaddr *ifa;

	if (!in_dev)
		return -ENODEV;

	in_dev_for_each_ifa_rcu(ifa, in_dev) {
		if (!inet_ifa_match(ipv4, ifa))
			continue;
		prop->prefix_len = inet_mask_len(ifa->ifa_mask);
		prop->outgoing_subnet = ifa->ifa_address & ifa->ifa_mask;
		/* prop->ipv6_prefixes_cnt = 0; already done by memset before */
		return 0;
	}
	return -ENOENT;
}

/* fill CLC proposal msg with ipv6 prefixes from device */
static int smc_clc_prfx_set6_rcu(struct dst_entry *dst,
				 struct smc_clc_msg_proposal_prefix *prop,
				 struct smc_clc_ipv6_prefix *ipv6_prfx)
{
#if IS_ENABLED(CONFIG_IPV6)
	struct inet6_dev *in6_dev = __in6_dev_get(dst->dev);
	struct inet6_ifaddr *ifa;
	int cnt = 0;

	if (!in6_dev)
		return -ENODEV;
	/* use a maximum of 8 IPv6 prefixes from device */
	list_for_each_entry(ifa, &in6_dev->addr_list, if_list) {
		if (ipv6_addr_type(&ifa->addr) & IPV6_ADDR_LINKLOCAL)
			continue;
		ipv6_addr_prefix(&ipv6_prfx[cnt].prefix,
				 &ifa->addr, ifa->prefix_len);
		ipv6_prfx[cnt].prefix_len = ifa->prefix_len;
		cnt++;
		if (cnt == SMC_CLC_MAX_V6_PREFIX)
			break;
	}
	prop->ipv6_prefixes_cnt = cnt;
	if (cnt)
		return 0;
#endif
	return -ENOENT;
}

/* retrieve and set prefixes in CLC proposal msg */
static int smc_clc_prfx_set(struct socket *clcsock,
			    struct smc_clc_msg_proposal_prefix *prop,
			    struct smc_clc_ipv6_prefix *ipv6_prfx)
{
	struct dst_entry *dst = sk_dst_get(clcsock->sk);
	struct sockaddr_storage addrs;
	struct sockaddr_in6 *addr6;
	struct sockaddr_in *addr;
	int rc = -ENOENT;

	if (!dst) {
		rc = -ENOTCONN;
		goto out;
	}
	if (!dst->dev) {
		rc = -ENODEV;
		goto out_rel;
	}
	/* get address to which the internal TCP socket is bound */
	if (kernel_getsockname(clcsock, (struct sockaddr *)&addrs) < 0)
		goto out_rel;
	/* analyze IP specific data of net_device belonging to TCP socket */
	addr6 = (struct sockaddr_in6 *)&addrs;
	rcu_read_lock();
	if (addrs.ss_family == PF_INET) {
		/* IPv4 */
		addr = (struct sockaddr_in *)&addrs;
		rc = smc_clc_prfx_set4_rcu(dst, addr->sin_addr.s_addr, prop);
	} else if (ipv6_addr_v4mapped(&addr6->sin6_addr)) {
		/* mapped IPv4 address - peer is IPv4 only */
		rc = smc_clc_prfx_set4_rcu(dst, addr6->sin6_addr.s6_addr32[3],
					   prop);
	} else {
		/* IPv6 */
		rc = smc_clc_prfx_set6_rcu(dst, prop, ipv6_prfx);
	}
	rcu_read_unlock();
out_rel:
	dst_release(dst);
out:
	return rc;
}

/* match ipv4 addrs of dev against addr in CLC proposal */
static int smc_clc_prfx_match4_rcu(struct net_device *dev,
				   struct smc_clc_msg_proposal_prefix *prop)
{
	struct in_device *in_dev = __in_dev_get_rcu(dev);
	const struct in_ifaddr *ifa;

	if (!in_dev)
		return -ENODEV;
	in_dev_for_each_ifa_rcu(ifa, in_dev) {
		if (prop->prefix_len == inet_mask_len(ifa->ifa_mask) &&
		    inet_ifa_match(prop->outgoing_subnet, ifa))
			return 0;
	}

	return -ENOENT;
}

/* match ipv6 addrs of dev against addrs in CLC proposal */
static int smc_clc_prfx_match6_rcu(struct net_device *dev,
				   struct smc_clc_msg_proposal_prefix *prop)
{
#if IS_ENABLED(CONFIG_IPV6)
	struct inet6_dev *in6_dev = __in6_dev_get(dev);
	struct smc_clc_ipv6_prefix *ipv6_prfx;
	struct inet6_ifaddr *ifa;
	int i, max;

	if (!in6_dev)
		return -ENODEV;
	/* ipv6 prefix list starts behind smc_clc_msg_proposal_prefix */
	ipv6_prfx = (struct smc_clc_ipv6_prefix *)((u8 *)prop + sizeof(*prop));
	max = min_t(u8, prop->ipv6_prefixes_cnt, SMC_CLC_MAX_V6_PREFIX);
	list_for_each_entry(ifa, &in6_dev->addr_list, if_list) {
		if (ipv6_addr_type(&ifa->addr) & IPV6_ADDR_LINKLOCAL)
			continue;
		for (i = 0; i < max; i++) {
			if (ifa->prefix_len == ipv6_prfx[i].prefix_len &&
			    ipv6_prefix_equal(&ifa->addr, &ipv6_prfx[i].prefix,
					      ifa->prefix_len))
				return 0;
		}
	}
#endif
	return -ENOENT;
}

/* check if proposed prefixes match one of our device prefixes */
int smc_clc_prfx_match(struct socket *clcsock,
		       struct smc_clc_msg_proposal_prefix *prop)
{
	struct dst_entry *dst = sk_dst_get(clcsock->sk);
	int rc;

	if (!dst) {
		rc = -ENOTCONN;
		goto out;
	}
	if (!dst->dev) {
		rc = -ENODEV;
		goto out_rel;
	}
	rcu_read_lock();
	if (!prop->ipv6_prefixes_cnt)
		rc = smc_clc_prfx_match4_rcu(dst->dev, prop);
	else
		rc = smc_clc_prfx_match6_rcu(dst->dev, prop);
	rcu_read_unlock();
out_rel:
	dst_release(dst);
out:
	return rc;
}

/* Wait for data on the tcp-socket, analyze received data
 * Returns:
 * 0 if success and it was not a decline that we received.
 * SMC_CLC_DECL_REPLY if decline received for fallback w/o another decl send.
 * clcsock error, -EINTR, -ECONNRESET, -EPROTO otherwise.
 */
int smc_clc_wait_msg(struct smc_sock *smc, void *buf, int buflen,
		     u8 expected_type, unsigned long timeout)
{
	long rcvtimeo = smc->clcsock->sk->sk_rcvtimeo;
	struct sock *clc_sk = smc->clcsock->sk;
	struct smc_clc_msg_hdr *clcm = buf;
	struct msghdr msg = {NULL, 0};
	int reason_code = 0;
	struct kvec vec = {buf, buflen};
	int len, datlen, recvlen;
	bool check_trl = true;
	int krflags;

	/* peek the first few bytes to determine length of data to receive
	 * so we don't consume any subsequent CLC message or payload data
	 * in the TCP byte stream
	 */
	/*
	 * Caller must make sure that buflen is no less than
	 * sizeof(struct smc_clc_msg_hdr)
	 */
	krflags = MSG_PEEK | MSG_WAITALL;
	clc_sk->sk_rcvtimeo = timeout;
	iov_iter_kvec(&msg.msg_iter, READ, &vec, 1,
			sizeof(struct smc_clc_msg_hdr));
	len = sock_recvmsg(smc->clcsock, &msg, krflags);
	if (signal_pending(current)) {
		reason_code = -EINTR;
		clc_sk->sk_err = EINTR;
		smc->sk.sk_err = EINTR;
		goto out;
	}
	if (clc_sk->sk_err) {
		reason_code = -clc_sk->sk_err;
		if (clc_sk->sk_err == EAGAIN &&
		    expected_type == SMC_CLC_DECLINE)
			clc_sk->sk_err = 0; /* reset for fallback usage */
		else
			smc->sk.sk_err = clc_sk->sk_err;
		goto out;
	}
	if (!len) { /* peer has performed orderly shutdown */
		smc->sk.sk_err = ECONNRESET;
		reason_code = -ECONNRESET;
		goto out;
	}
	if (len < 0) {
		if (len != -EAGAIN || expected_type != SMC_CLC_DECLINE)
			smc->sk.sk_err = -len;
		reason_code = len;
		goto out;
	}
	datlen = ntohs(clcm->length);
	if ((len < sizeof(struct smc_clc_msg_hdr)) ||
	    (clcm->version < SMC_V1) ||
	    ((clcm->type != SMC_CLC_DECLINE) &&
	     (clcm->type != expected_type))) {
		smc->sk.sk_err = EPROTO;
		reason_code = -EPROTO;
		goto out;
	}

	/* receive the complete CLC message */
	memset(&msg, 0, sizeof(struct msghdr));
	if (datlen > buflen) {
		check_trl = false;
		recvlen = buflen;
	} else {
		recvlen = datlen;
	}
	iov_iter_kvec(&msg.msg_iter, READ, &vec, 1, recvlen);
	krflags = MSG_WAITALL;
	len = sock_recvmsg(smc->clcsock, &msg, krflags);
	if (len < recvlen || !smc_clc_msg_hdr_valid(clcm, check_trl)) {
		smc->sk.sk_err = EPROTO;
		reason_code = -EPROTO;
		goto out;
	}
	datlen -= len;
	while (datlen) {
		u8 tmp[SMC_CLC_RECV_BUF_LEN];

		vec.iov_base = &tmp;
		vec.iov_len = SMC_CLC_RECV_BUF_LEN;
		/* receive remaining proposal message */
		recvlen = datlen > SMC_CLC_RECV_BUF_LEN ?
						SMC_CLC_RECV_BUF_LEN : datlen;
		iov_iter_kvec(&msg.msg_iter, READ, &vec, 1, recvlen);
		len = sock_recvmsg(smc->clcsock, &msg, krflags);
		datlen -= len;
	}
	if (clcm->type == SMC_CLC_DECLINE) {
		struct smc_clc_msg_decline *dclc;

		dclc = (struct smc_clc_msg_decline *)clcm;
		reason_code = SMC_CLC_DECL_PEERDECL;
		smc->peer_diagnosis = ntohl(dclc->peer_diagnosis);
		if (((struct smc_clc_msg_decline *)buf)->hdr.typev2 &
						SMC_FIRST_CONTACT_MASK) {
			smc->conn.lgr->sync_err = 1;
			smc_lgr_terminate_sched(smc->conn.lgr);
		}
	}

out:
	clc_sk->sk_rcvtimeo = rcvtimeo;
	return reason_code;
}

/* send CLC DECLINE message across internal TCP socket */
int smc_clc_send_decline(struct smc_sock *smc, u32 peer_diag_info, u8 version)
{
	struct smc_clc_msg_decline dclc;
	struct msghdr msg;
	struct kvec vec;
	int len;

	memset(&dclc, 0, sizeof(dclc));
	memcpy(dclc.hdr.eyecatcher, SMC_EYECATCHER, sizeof(SMC_EYECATCHER));
	dclc.hdr.type = SMC_CLC_DECLINE;
	dclc.hdr.length = htons(sizeof(struct smc_clc_msg_decline));
	dclc.hdr.version = version;
	dclc.os_type = version == SMC_V1 ? 0 : SMC_CLC_OS_LINUX;
	dclc.hdr.typev2 = (peer_diag_info == SMC_CLC_DECL_SYNCERR) ?
						SMC_FIRST_CONTACT_MASK : 0;
	if ((!smc->conn.lgr || !smc->conn.lgr->is_smcd) &&
	    smc_ib_is_valid_local_systemid())
		memcpy(dclc.id_for_peer, local_systemid,
		       sizeof(local_systemid));
	dclc.peer_diagnosis = htonl(peer_diag_info);
	memcpy(dclc.trl.eyecatcher, SMC_EYECATCHER, sizeof(SMC_EYECATCHER));

	memset(&msg, 0, sizeof(msg));
	vec.iov_base = &dclc;
	vec.iov_len = sizeof(struct smc_clc_msg_decline);
	len = kernel_sendmsg(smc->clcsock, &msg, &vec, 1,
			     sizeof(struct smc_clc_msg_decline));
	if (len < 0 || len < sizeof(struct smc_clc_msg_decline))
		len = -EPROTO;
	return len > 0 ? 0 : len;
}

/* send CLC PROPOSAL message across internal TCP socket */
int smc_clc_send_proposal(struct smc_sock *smc, struct smc_init_info *ini)
{
	struct smc_clc_smcd_v2_extension *smcd_v2_ext;
	struct smc_clc_msg_proposal_prefix *pclc_prfx;
	struct smc_clc_msg_proposal *pclc_base;
	struct smc_clc_smcd_gid_chid *gidchids;
	struct smc_clc_msg_proposal_area *pclc;
	struct smc_clc_ipv6_prefix *ipv6_prfx;
	struct smc_clc_v2_extension *v2_ext;
	struct smc_clc_msg_smcd *pclc_smcd;
	struct smc_clc_msg_trail *trl;
	int len, i, plen, rc;
	int reason_code = 0;
	struct kvec vec[8];
	struct msghdr msg;

	pclc = kzalloc(sizeof(*pclc), GFP_KERNEL);
	if (!pclc)
		return -ENOMEM;

	pclc_base = &pclc->pclc_base;
	pclc_smcd = &pclc->pclc_smcd;
	pclc_prfx = &pclc->pclc_prfx;
	ipv6_prfx = pclc->pclc_prfx_ipv6;
	v2_ext = &pclc->pclc_v2_ext;
	smcd_v2_ext = &pclc->pclc_smcd_v2_ext;
	gidchids = pclc->pclc_gidchids;
	trl = &pclc->pclc_trl;

	pclc_base->hdr.version = SMC_V2;
	pclc_base->hdr.typev1 = ini->smc_type_v1;
	pclc_base->hdr.typev2 = ini->smc_type_v2;
	plen = sizeof(*pclc_base) + sizeof(*pclc_smcd) + sizeof(*trl);

	/* retrieve ip prefixes for CLC proposal msg */
	if (ini->smc_type_v1 != SMC_TYPE_N) {
		rc = smc_clc_prfx_set(smc->clcsock, pclc_prfx, ipv6_prfx);
		if (rc) {
			if (ini->smc_type_v2 == SMC_TYPE_N) {
				kfree(pclc);
				return SMC_CLC_DECL_CNFERR;
			}
			pclc_base->hdr.typev1 = SMC_TYPE_N;
		} else {
			pclc_base->iparea_offset = htons(sizeof(*pclc_smcd));
			plen += sizeof(*pclc_prfx) +
					pclc_prfx->ipv6_prefixes_cnt *
					sizeof(ipv6_prfx[0]);
		}
	}

	/* build SMC Proposal CLC message */
	memcpy(pclc_base->hdr.eyecatcher, SMC_EYECATCHER,
	       sizeof(SMC_EYECATCHER));
	pclc_base->hdr.type = SMC_CLC_PROPOSAL;
	if (smcr_indicated(ini->smc_type_v1)) {
		/* add SMC-R specifics */
		memcpy(pclc_base->lcl.id_for_peer, local_systemid,
		       sizeof(local_systemid));
		memcpy(pclc_base->lcl.gid, ini->ib_gid, SMC_GID_SIZE);
		memcpy(pclc_base->lcl.mac, &ini->ib_dev->mac[ini->ib_port - 1],
		       ETH_ALEN);
	}
	if (smcd_indicated(ini->smc_type_v1)) {
		/* add SMC-D specifics */
		if (ini->ism_dev[0]) {
			pclc_smcd->ism.gid = htonll(ini->ism_dev[0]->local_gid);
			pclc_smcd->ism.chid =
				htons(smc_ism_get_chid(ini->ism_dev[0]));
		}
	}
	if (ini->smc_type_v2 == SMC_TYPE_N) {
		pclc_smcd->v2_ext_offset = 0;
	} else {
		u16 v2_ext_offset;
		u8 *eid = NULL;

		v2_ext_offset = sizeof(*pclc_smcd) -
			offsetofend(struct smc_clc_msg_smcd, v2_ext_offset);
		if (ini->smc_type_v1 != SMC_TYPE_N)
			v2_ext_offset += sizeof(*pclc_prfx) +
						pclc_prfx->ipv6_prefixes_cnt *
						sizeof(ipv6_prfx[0]);
		pclc_smcd->v2_ext_offset = htons(v2_ext_offset);
		v2_ext->hdr.eid_cnt = 0;
		v2_ext->hdr.ism_gid_cnt = ini->ism_offered_cnt;
		v2_ext->hdr.flag.release = SMC_RELEASE;
		v2_ext->hdr.flag.seid = 1;
		v2_ext->hdr.smcd_v2_ext_offset = htons(sizeof(*v2_ext) -
				offsetofend(struct smc_clnt_opts_area_hdr,
					    smcd_v2_ext_offset) +
				v2_ext->hdr.eid_cnt * SMC_MAX_EID_LEN);
		if (ini->ism_dev[0])
			smc_ism_get_system_eid(ini->ism_dev[0], &eid);
		else
			smc_ism_get_system_eid(ini->ism_dev[1], &eid);
		if (eid)
			memcpy(smcd_v2_ext->system_eid, eid, SMC_MAX_EID_LEN);
		plen += sizeof(*v2_ext) + sizeof(*smcd_v2_ext);
		if (ini->ism_offered_cnt) {
			for (i = 1; i <= ini->ism_offered_cnt; i++) {
				gidchids[i - 1].gid =
					htonll(ini->ism_dev[i]->local_gid);
				gidchids[i - 1].chid =
					htons(smc_ism_get_chid(ini->ism_dev[i]));
			}
			plen += ini->ism_offered_cnt *
				sizeof(struct smc_clc_smcd_gid_chid);
		}
	}
	pclc_base->hdr.length = htons(plen);
	memcpy(trl->eyecatcher, SMC_EYECATCHER, sizeof(SMC_EYECATCHER));

	/* send SMC Proposal CLC message */
	memset(&msg, 0, sizeof(msg));
	i = 0;
	vec[i].iov_base = pclc_base;
	vec[i++].iov_len = sizeof(*pclc_base);
	vec[i].iov_base = pclc_smcd;
	vec[i++].iov_len = sizeof(*pclc_smcd);
	if (ini->smc_type_v1 != SMC_TYPE_N) {
		vec[i].iov_base = pclc_prfx;
		vec[i++].iov_len = sizeof(*pclc_prfx);
		if (pclc_prfx->ipv6_prefixes_cnt > 0) {
			vec[i].iov_base = ipv6_prfx;
			vec[i++].iov_len = pclc_prfx->ipv6_prefixes_cnt *
					   sizeof(ipv6_prfx[0]);
		}
	}
	if (ini->smc_type_v2 != SMC_TYPE_N) {
		vec[i].iov_base = v2_ext;
		vec[i++].iov_len = sizeof(*v2_ext);
		vec[i].iov_base = smcd_v2_ext;
		vec[i++].iov_len = sizeof(*smcd_v2_ext);
		if (ini->ism_offered_cnt) {
			vec[i].iov_base = gidchids;
			vec[i++].iov_len = ini->ism_offered_cnt *
					sizeof(struct smc_clc_smcd_gid_chid);
		}
	}
	vec[i].iov_base = trl;
	vec[i++].iov_len = sizeof(*trl);
	/* due to the few bytes needed for clc-handshake this cannot block */
	len = kernel_sendmsg(smc->clcsock, &msg, vec, i, plen);
	if (len < 0) {
		smc->sk.sk_err = smc->clcsock->sk->sk_err;
		reason_code = -smc->sk.sk_err;
	} else if (len < ntohs(pclc_base->hdr.length)) {
		reason_code = -ENETUNREACH;
		smc->sk.sk_err = -reason_code;
	}

	kfree(pclc);
	return reason_code;
}

/* build and send CLC CONFIRM / ACCEPT message */
static int smc_clc_send_confirm_accept(struct smc_sock *smc,
				       struct smc_clc_msg_accept_confirm_v2 *clc_v2,
				       int first_contact, u8 version)
{
	struct smc_connection *conn = &smc->conn;
	struct smc_clc_msg_accept_confirm *clc;
	struct smc_clc_first_contact_ext fce;
	struct smc_clc_msg_trail trl;
	struct kvec vec[3];
	struct msghdr msg;
	int i, len;

	/* send SMC Confirm CLC msg */
	clc = (struct smc_clc_msg_accept_confirm *)clc_v2;
	clc->hdr.version = version;	/* SMC version */
	if (first_contact)
		clc->hdr.typev2 |= SMC_FIRST_CONTACT_MASK;
	if (conn->lgr->is_smcd) {
		/* SMC-D specific settings */
		memcpy(clc->hdr.eyecatcher, SMCD_EYECATCHER,
		       sizeof(SMCD_EYECATCHER));
		clc->hdr.typev1 = SMC_TYPE_D;
		clc->d0.gid = conn->lgr->smcd->local_gid;
		clc->d0.token = conn->rmb_desc->token;
		clc->d0.dmbe_size = conn->rmbe_size_short;
		clc->d0.dmbe_idx = 0;
		memcpy(&clc->d0.linkid, conn->lgr->id, SMC_LGR_ID_SIZE);
		if (version == SMC_V1) {
			clc->hdr.length = htons(SMCD_CLC_ACCEPT_CONFIRM_LEN);
		} else {
			u8 *eid = NULL;

			clc_v2->chid = htons(smc_ism_get_chid(conn->lgr->smcd));
			smc_ism_get_system_eid(conn->lgr->smcd, &eid);
			if (eid)
				memcpy(clc_v2->eid, eid, SMC_MAX_EID_LEN);
			len = SMCD_CLC_ACCEPT_CONFIRM_LEN_V2;
			if (first_contact)
				smc_clc_fill_fce(&fce, &len);
			clc_v2->hdr.length = htons(len);
		}
		memcpy(trl.eyecatcher, SMCD_EYECATCHER,
		       sizeof(SMCD_EYECATCHER));
	} else {
		struct smc_link *link = conn->lnk;

		/* SMC-R specific settings */
		link = conn->lnk;
		memcpy(clc->hdr.eyecatcher, SMC_EYECATCHER,
		       sizeof(SMC_EYECATCHER));
		clc->hdr.typev1 = SMC_TYPE_R;
		clc->hdr.length = htons(SMCR_CLC_ACCEPT_CONFIRM_LEN);
		memcpy(clc->r0.lcl.id_for_peer, local_systemid,
		       sizeof(local_systemid));
		memcpy(&clc->r0.lcl.gid, link->gid, SMC_GID_SIZE);
		memcpy(&clc->r0.lcl.mac, &link->smcibdev->mac[link->ibport - 1],
		       ETH_ALEN);
		hton24(clc->r0.qpn, link->roce_qp->qp_num);
		clc->r0.rmb_rkey =
			htonl(conn->rmb_desc->mr_rx[link->link_idx]->rkey);
		clc->r0.rmbe_idx = 1; /* for now: 1 RMB = 1 RMBE */
		clc->r0.rmbe_alert_token = htonl(conn->alert_token_local);
		switch (clc->hdr.type) {
		case SMC_CLC_ACCEPT:
			clc->r0.qp_mtu = link->path_mtu;
			break;
		case SMC_CLC_CONFIRM:
			clc->r0.qp_mtu = min(link->path_mtu, link->peer_mtu);
			break;
		}
		clc->r0.rmbe_size = conn->rmbe_size_short;
		clc->r0.rmb_dma_addr = cpu_to_be64((u64)sg_dma_address
				(conn->rmb_desc->sgt[link->link_idx].sgl));
		hton24(clc->r0.psn, link->psn_initial);
		memcpy(trl.eyecatcher, SMC_EYECATCHER, sizeof(SMC_EYECATCHER));
	}

	memset(&msg, 0, sizeof(msg));
	i = 0;
	vec[i].iov_base = clc_v2;
	if (version > SMC_V1)
		vec[i++].iov_len = SMCD_CLC_ACCEPT_CONFIRM_LEN_V2 - sizeof(trl);
	else
		vec[i++].iov_len = (clc->hdr.typev1 == SMC_TYPE_D ?
						SMCD_CLC_ACCEPT_CONFIRM_LEN :
						SMCR_CLC_ACCEPT_CONFIRM_LEN) -
				   sizeof(trl);
	if (version > SMC_V1 && first_contact) {
		vec[i].iov_base = &fce;
		vec[i++].iov_len = sizeof(fce);
	}
	vec[i].iov_base = &trl;
	vec[i++].iov_len = sizeof(trl);
	return kernel_sendmsg(smc->clcsock, &msg, vec, 1,
			      ntohs(clc->hdr.length));
}

/* send CLC CONFIRM message across internal TCP socket */
int smc_clc_send_confirm(struct smc_sock *smc, bool clnt_first_contact,
			 u8 version)
{
	struct smc_clc_msg_accept_confirm_v2 cclc_v2;
	int reason_code = 0;
	int len;

	/* send SMC Confirm CLC msg */
	memset(&cclc_v2, 0, sizeof(cclc_v2));
	cclc_v2.hdr.type = SMC_CLC_CONFIRM;
	len = smc_clc_send_confirm_accept(smc, &cclc_v2, clnt_first_contact,
					  version);
	if (len < ntohs(cclc_v2.hdr.length)) {
		if (len >= 0) {
			reason_code = -ENETUNREACH;
			smc->sk.sk_err = -reason_code;
		} else {
			smc->sk.sk_err = smc->clcsock->sk->sk_err;
			reason_code = -smc->sk.sk_err;
		}
	}
	return reason_code;
}

/* send CLC ACCEPT message across internal TCP socket */
int smc_clc_send_accept(struct smc_sock *new_smc, bool srv_first_contact,
			u8 version)
{
	struct smc_clc_msg_accept_confirm_v2 aclc_v2;
	int len;

	memset(&aclc_v2, 0, sizeof(aclc_v2));
	aclc_v2.hdr.type = SMC_CLC_ACCEPT;
	len = smc_clc_send_confirm_accept(new_smc, &aclc_v2, srv_first_contact,
					  version);
	if (len < ntohs(aclc_v2.hdr.length))
		len = len >= 0 ? -EPROTO : -new_smc->clcsock->sk->sk_err;

	return len > 0 ? 0 : len;
}

void __init smc_clc_init(void)
{
	struct new_utsname *u;

	memset(smc_hostname, _S, sizeof(smc_hostname)); /* ASCII blanks */
	u = utsname();
	memcpy(smc_hostname, u->nodename,
	       min_t(size_t, strlen(u->nodename), sizeof(smc_hostname)));
}
