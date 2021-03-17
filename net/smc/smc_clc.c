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

/* eye catcher "SMCR" EBCDIC for CLC messages */
static const char SMC_EYECATCHER[4] = {'\xe2', '\xd4', '\xc3', '\xd9'};
/* eye catcher "SMCD" EBCDIC for CLC messages */
static const char SMCD_EYECATCHER[4] = {'\xe2', '\xd4', '\xc3', '\xc4'};

/* check if received message has a correct header length and contains valid
 * heading and trailing eyecatchers
 */
static bool smc_clc_msg_hdr_valid(struct smc_clc_msg_hdr *clcm)
{
	struct smc_clc_msg_proposal_prefix *pclc_prfx;
	struct smc_clc_msg_accept_confirm *clc;
	struct smc_clc_msg_proposal *pclc;
	struct smc_clc_msg_decline *dclc;
	struct smc_clc_msg_trail *trl;

	if (memcmp(clcm->eyecatcher, SMC_EYECATCHER, sizeof(SMC_EYECATCHER)) &&
	    memcmp(clcm->eyecatcher, SMCD_EYECATCHER, sizeof(SMCD_EYECATCHER)))
		return false;
	switch (clcm->type) {
	case SMC_CLC_PROPOSAL:
		if (clcm->path != SMC_TYPE_R && clcm->path != SMC_TYPE_D &&
		    clcm->path != SMC_TYPE_B)
			return false;
		pclc = (struct smc_clc_msg_proposal *)clcm;
		pclc_prfx = smc_clc_proposal_get_prefix(pclc);
		if (ntohs(pclc->hdr.length) !=
			sizeof(*pclc) + ntohs(pclc->iparea_offset) +
			sizeof(*pclc_prfx) +
			pclc_prfx->ipv6_prefixes_cnt *
				sizeof(struct smc_clc_ipv6_prefix) +
			sizeof(*trl))
			return false;
		trl = (struct smc_clc_msg_trail *)
			((u8 *)pclc + ntohs(pclc->hdr.length) - sizeof(*trl));
		break;
	case SMC_CLC_ACCEPT:
	case SMC_CLC_CONFIRM:
		if (clcm->path != SMC_TYPE_R && clcm->path != SMC_TYPE_D)
			return false;
		clc = (struct smc_clc_msg_accept_confirm *)clcm;
		if ((clcm->path == SMC_TYPE_R &&
		     ntohs(clc->hdr.length) != SMCR_CLC_ACCEPT_CONFIRM_LEN) ||
		    (clcm->path == SMC_TYPE_D &&
		     ntohs(clc->hdr.length) != SMCD_CLC_ACCEPT_CONFIRM_LEN))
			return false;
		trl = (struct smc_clc_msg_trail *)
			((u8 *)clc + ntohs(clc->hdr.length) - sizeof(*trl));
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
	if (memcmp(trl->eyecatcher, SMC_EYECATCHER, sizeof(SMC_EYECATCHER)) &&
	    memcmp(trl->eyecatcher, SMCD_EYECATCHER, sizeof(SMCD_EYECATCHER)))
		return false;
	return true;
}

/* find ipv4 addr on device and get the prefix len, fill CLC proposal msg */
static int smc_clc_prfx_set4_rcu(struct dst_entry *dst, __be32 ipv4,
				 struct smc_clc_msg_proposal_prefix *prop)
{
	struct in_device *in_dev = __in_dev_get_rcu(dst->dev);

	if (!in_dev)
		return -ENODEV;
	for_ifa(in_dev) {
		if (!inet_ifa_match(ipv4, ifa))
			continue;
		prop->prefix_len = inet_mask_len(ifa->ifa_mask);
		prop->outgoing_subnet = ifa->ifa_address & ifa->ifa_mask;
		/* prop->ipv6_prefixes_cnt = 0; already done by memset before */
		return 0;
	} endfor_ifa(in_dev);
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

	memset(prop, 0, sizeof(*prop));
	if (!dst) {
		rc = -ENOTCONN;
		goto out;
	}
	if (!dst->dev) {
		rc = -ENODEV;
		goto out_rel;
	}
	/* get address to which the internal TCP socket is bound */
	kernel_getsockname(clcsock, (struct sockaddr *)&addrs);
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

	if (!in_dev)
		return -ENODEV;
	for_ifa(in_dev) {
		if (prop->prefix_len == inet_mask_len(ifa->ifa_mask) &&
		    inet_ifa_match(prop->outgoing_subnet, ifa))
			return 0;
	} endfor_ifa(in_dev);

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
		     u8 expected_type)
{
	long rcvtimeo = smc->clcsock->sk->sk_rcvtimeo;
	struct sock *clc_sk = smc->clcsock->sk;
	struct smc_clc_msg_hdr *clcm = buf;
	struct msghdr msg = {NULL, 0};
	int reason_code = 0;
	struct kvec vec = {buf, buflen};
	int len, datlen;
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
	smc->clcsock->sk->sk_rcvtimeo = CLC_WAIT_TIME;
	iov_iter_kvec(&msg.msg_iter, READ | ITER_KVEC, &vec, 1,
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
		smc->sk.sk_err = clc_sk->sk_err;
		goto out;
	}
	if (!len) { /* peer has performed orderly shutdown */
		smc->sk.sk_err = ECONNRESET;
		reason_code = -ECONNRESET;
		goto out;
	}
	if (len < 0) {
		smc->sk.sk_err = -len;
		reason_code = len;
		goto out;
	}
	datlen = ntohs(clcm->length);
	if ((len < sizeof(struct smc_clc_msg_hdr)) ||
	    (datlen > buflen) ||
	    (clcm->version != SMC_CLC_V1) ||
	    (clcm->path != SMC_TYPE_R && clcm->path != SMC_TYPE_D &&
	     clcm->path != SMC_TYPE_B) ||
	    ((clcm->type != SMC_CLC_DECLINE) &&
	     (clcm->type != expected_type))) {
		smc->sk.sk_err = EPROTO;
		reason_code = -EPROTO;
		goto out;
	}

	/* receive the complete CLC message */
	memset(&msg, 0, sizeof(struct msghdr));
	iov_iter_kvec(&msg.msg_iter, READ | ITER_KVEC, &vec, 1, datlen);
	krflags = MSG_WAITALL;
	len = sock_recvmsg(smc->clcsock, &msg, krflags);
	if (len < datlen || !smc_clc_msg_hdr_valid(clcm)) {
		smc->sk.sk_err = EPROTO;
		reason_code = -EPROTO;
		goto out;
	}
	if (clcm->type == SMC_CLC_DECLINE) {
		struct smc_clc_msg_decline *dclc;

		dclc = (struct smc_clc_msg_decline *)clcm;
		reason_code = SMC_CLC_DECL_PEERDECL;
		smc->peer_diagnosis = ntohl(dclc->peer_diagnosis);
		if (((struct smc_clc_msg_decline *)buf)->hdr.flag) {
			smc->conn.lgr->sync_err = 1;
			smc_lgr_terminate(smc->conn.lgr);
		}
	}

out:
	smc->clcsock->sk->sk_rcvtimeo = rcvtimeo;
	return reason_code;
}

/* send CLC DECLINE message across internal TCP socket */
int smc_clc_send_decline(struct smc_sock *smc, u32 peer_diag_info)
{
	struct smc_clc_msg_decline dclc;
	struct msghdr msg;
	struct kvec vec;
	int len;

	memset(&dclc, 0, sizeof(dclc));
	memcpy(dclc.hdr.eyecatcher, SMC_EYECATCHER, sizeof(SMC_EYECATCHER));
	dclc.hdr.type = SMC_CLC_DECLINE;
	dclc.hdr.length = htons(sizeof(struct smc_clc_msg_decline));
	dclc.hdr.version = SMC_CLC_V1;
	dclc.hdr.flag = (peer_diag_info == SMC_CLC_DECL_SYNCERR) ? 1 : 0;
	memcpy(dclc.id_for_peer, local_systemid, sizeof(local_systemid));
	dclc.peer_diagnosis = htonl(peer_diag_info);
	memcpy(dclc.trl.eyecatcher, SMC_EYECATCHER, sizeof(SMC_EYECATCHER));

	memset(&msg, 0, sizeof(msg));
	vec.iov_base = &dclc;
	vec.iov_len = sizeof(struct smc_clc_msg_decline);
	len = kernel_sendmsg(smc->clcsock, &msg, &vec, 1,
			     sizeof(struct smc_clc_msg_decline));
	if (len < sizeof(struct smc_clc_msg_decline))
		smc->sk.sk_err = EPROTO;
	if (len < 0)
		smc->sk.sk_err = -len;
	return sock_error(&smc->sk);
}

/* send CLC PROPOSAL message across internal TCP socket */
int smc_clc_send_proposal(struct smc_sock *smc, int smc_type,
			  struct smc_ib_device *ibdev, u8 ibport, u8 gid[],
			  struct smcd_dev *ismdev)
{
	struct smc_clc_ipv6_prefix ipv6_prfx[SMC_CLC_MAX_V6_PREFIX];
	struct smc_clc_msg_proposal_prefix pclc_prfx;
	struct smc_clc_msg_smcd pclc_smcd;
	struct smc_clc_msg_proposal pclc;
	struct smc_clc_msg_trail trl;
	int len, i, plen, rc;
	int reason_code = 0;
	struct kvec vec[5];
	struct msghdr msg;

	/* retrieve ip prefixes for CLC proposal msg */
	rc = smc_clc_prfx_set(smc->clcsock, &pclc_prfx, ipv6_prfx);
	if (rc)
		return SMC_CLC_DECL_CNFERR; /* configuration error */

	/* send SMC Proposal CLC message */
	plen = sizeof(pclc) + sizeof(pclc_prfx) +
	       (pclc_prfx.ipv6_prefixes_cnt * sizeof(ipv6_prfx[0])) +
	       sizeof(trl);
	memset(&pclc, 0, sizeof(pclc));
	memcpy(pclc.hdr.eyecatcher, SMC_EYECATCHER, sizeof(SMC_EYECATCHER));
	pclc.hdr.type = SMC_CLC_PROPOSAL;
	pclc.hdr.version = SMC_CLC_V1;		/* SMC version */
	pclc.hdr.path = smc_type;
	if (smc_type == SMC_TYPE_R || smc_type == SMC_TYPE_B) {
		/* add SMC-R specifics */
		memcpy(pclc.lcl.id_for_peer, local_systemid,
		       sizeof(local_systemid));
		memcpy(&pclc.lcl.gid, gid, SMC_GID_SIZE);
		memcpy(&pclc.lcl.mac, &ibdev->mac[ibport - 1], ETH_ALEN);
		pclc.iparea_offset = htons(0);
	}
	if (smc_type == SMC_TYPE_D || smc_type == SMC_TYPE_B) {
		/* add SMC-D specifics */
		memset(&pclc_smcd, 0, sizeof(pclc_smcd));
		plen += sizeof(pclc_smcd);
		pclc.iparea_offset = htons(SMC_CLC_PROPOSAL_MAX_OFFSET);
		pclc_smcd.gid = ismdev->local_gid;
	}
	pclc.hdr.length = htons(plen);

	memcpy(trl.eyecatcher, SMC_EYECATCHER, sizeof(SMC_EYECATCHER));
	memset(&msg, 0, sizeof(msg));
	i = 0;
	vec[i].iov_base = &pclc;
	vec[i++].iov_len = sizeof(pclc);
	if (smc_type == SMC_TYPE_D || smc_type == SMC_TYPE_B) {
		vec[i].iov_base = &pclc_smcd;
		vec[i++].iov_len = sizeof(pclc_smcd);
	}
	vec[i].iov_base = &pclc_prfx;
	vec[i++].iov_len = sizeof(pclc_prfx);
	if (pclc_prfx.ipv6_prefixes_cnt > 0) {
		vec[i].iov_base = &ipv6_prfx[0];
		vec[i++].iov_len = pclc_prfx.ipv6_prefixes_cnt *
				   sizeof(ipv6_prfx[0]);
	}
	vec[i].iov_base = &trl;
	vec[i++].iov_len = sizeof(trl);
	/* due to the few bytes needed for clc-handshake this cannot block */
	len = kernel_sendmsg(smc->clcsock, &msg, vec, i, plen);
	if (len < 0) {
		smc->sk.sk_err = smc->clcsock->sk->sk_err;
		reason_code = -smc->sk.sk_err;
	} else if (len < (int)sizeof(pclc)) {
		reason_code = -ENETUNREACH;
		smc->sk.sk_err = -reason_code;
	}

	return reason_code;
}

/* send CLC CONFIRM message across internal TCP socket */
int smc_clc_send_confirm(struct smc_sock *smc)
{
	struct smc_connection *conn = &smc->conn;
	struct smc_clc_msg_accept_confirm cclc;
	struct smc_link *link;
	int reason_code = 0;
	struct msghdr msg;
	struct kvec vec;
	int len;

	/* send SMC Confirm CLC msg */
	memset(&cclc, 0, sizeof(cclc));
	cclc.hdr.type = SMC_CLC_CONFIRM;
	cclc.hdr.version = SMC_CLC_V1;		/* SMC version */
	if (smc->conn.lgr->is_smcd) {
		/* SMC-D specific settings */
		memcpy(cclc.hdr.eyecatcher, SMCD_EYECATCHER,
		       sizeof(SMCD_EYECATCHER));
		cclc.hdr.path = SMC_TYPE_D;
		cclc.hdr.length = htons(SMCD_CLC_ACCEPT_CONFIRM_LEN);
		cclc.gid = conn->lgr->smcd->local_gid;
		cclc.token = conn->rmb_desc->token;
		cclc.dmbe_size = conn->rmbe_size_short;
		cclc.dmbe_idx = 0;
		memcpy(&cclc.linkid, conn->lgr->id, SMC_LGR_ID_SIZE);
		memcpy(cclc.smcd_trl.eyecatcher, SMCD_EYECATCHER,
		       sizeof(SMCD_EYECATCHER));
	} else {
		/* SMC-R specific settings */
		link = &conn->lgr->lnk[SMC_SINGLE_LINK];
		memcpy(cclc.hdr.eyecatcher, SMC_EYECATCHER,
		       sizeof(SMC_EYECATCHER));
		cclc.hdr.path = SMC_TYPE_R;
		cclc.hdr.length = htons(SMCR_CLC_ACCEPT_CONFIRM_LEN);
		memcpy(cclc.lcl.id_for_peer, local_systemid,
		       sizeof(local_systemid));
		memcpy(&cclc.lcl.gid, link->gid, SMC_GID_SIZE);
		memcpy(&cclc.lcl.mac, &link->smcibdev->mac[link->ibport - 1],
		       ETH_ALEN);
		hton24(cclc.qpn, link->roce_qp->qp_num);
		cclc.rmb_rkey =
			htonl(conn->rmb_desc->mr_rx[SMC_SINGLE_LINK]->rkey);
		cclc.rmbe_idx = 1; /* for now: 1 RMB = 1 RMBE */
		cclc.rmbe_alert_token = htonl(conn->alert_token_local);
		cclc.qp_mtu = min(link->path_mtu, link->peer_mtu);
		cclc.rmbe_size = conn->rmbe_size_short;
		cclc.rmb_dma_addr = cpu_to_be64((u64)sg_dma_address
				(conn->rmb_desc->sgt[SMC_SINGLE_LINK].sgl));
		hton24(cclc.psn, link->psn_initial);
		memcpy(cclc.smcr_trl.eyecatcher, SMC_EYECATCHER,
		       sizeof(SMC_EYECATCHER));
	}

	memset(&msg, 0, sizeof(msg));
	vec.iov_base = &cclc;
	vec.iov_len = ntohs(cclc.hdr.length);
	len = kernel_sendmsg(smc->clcsock, &msg, &vec, 1,
			     ntohs(cclc.hdr.length));
	if (len < ntohs(cclc.hdr.length)) {
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
int smc_clc_send_accept(struct smc_sock *new_smc, int srv_first_contact)
{
	struct smc_connection *conn = &new_smc->conn;
	struct smc_clc_msg_accept_confirm aclc;
	struct smc_link *link;
	struct msghdr msg;
	struct kvec vec;
	int rc = 0;
	int len;

	memset(&aclc, 0, sizeof(aclc));
	aclc.hdr.type = SMC_CLC_ACCEPT;
	aclc.hdr.version = SMC_CLC_V1;		/* SMC version */
	if (srv_first_contact)
		aclc.hdr.flag = 1;

	if (new_smc->conn.lgr->is_smcd) {
		/* SMC-D specific settings */
		aclc.hdr.length = htons(SMCD_CLC_ACCEPT_CONFIRM_LEN);
		memcpy(aclc.hdr.eyecatcher, SMCD_EYECATCHER,
		       sizeof(SMCD_EYECATCHER));
		aclc.hdr.path = SMC_TYPE_D;
		aclc.gid = conn->lgr->smcd->local_gid;
		aclc.token = conn->rmb_desc->token;
		aclc.dmbe_size = conn->rmbe_size_short;
		aclc.dmbe_idx = 0;
		memcpy(&aclc.linkid, conn->lgr->id, SMC_LGR_ID_SIZE);
		memcpy(aclc.smcd_trl.eyecatcher, SMCD_EYECATCHER,
		       sizeof(SMCD_EYECATCHER));
	} else {
		/* SMC-R specific settings */
		aclc.hdr.length = htons(SMCR_CLC_ACCEPT_CONFIRM_LEN);
		memcpy(aclc.hdr.eyecatcher, SMC_EYECATCHER,
		       sizeof(SMC_EYECATCHER));
		aclc.hdr.path = SMC_TYPE_R;
		link = &conn->lgr->lnk[SMC_SINGLE_LINK];
		memcpy(aclc.lcl.id_for_peer, local_systemid,
		       sizeof(local_systemid));
		memcpy(&aclc.lcl.gid, link->gid, SMC_GID_SIZE);
		memcpy(&aclc.lcl.mac, link->smcibdev->mac[link->ibport - 1],
		       ETH_ALEN);
		hton24(aclc.qpn, link->roce_qp->qp_num);
		aclc.rmb_rkey =
			htonl(conn->rmb_desc->mr_rx[SMC_SINGLE_LINK]->rkey);
		aclc.rmbe_idx = 1;		/* as long as 1 RMB = 1 RMBE */
		aclc.rmbe_alert_token = htonl(conn->alert_token_local);
		aclc.qp_mtu = link->path_mtu;
		aclc.rmbe_size = conn->rmbe_size_short,
		aclc.rmb_dma_addr = cpu_to_be64((u64)sg_dma_address
				(conn->rmb_desc->sgt[SMC_SINGLE_LINK].sgl));
		hton24(aclc.psn, link->psn_initial);
		memcpy(aclc.smcr_trl.eyecatcher, SMC_EYECATCHER,
		       sizeof(SMC_EYECATCHER));
	}

	memset(&msg, 0, sizeof(msg));
	vec.iov_base = &aclc;
	vec.iov_len = ntohs(aclc.hdr.length);
	len = kernel_sendmsg(new_smc->clcsock, &msg, &vec, 1,
			     ntohs(aclc.hdr.length));
	if (len < ntohs(aclc.hdr.length)) {
		if (len >= 0)
			new_smc->sk.sk_err = EPROTO;
		else
			new_smc->sk.sk_err = new_smc->clcsock->sk->sk_err;
		rc = sock_error(&new_smc->sk);
	}

	return rc;
}
