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
#define SMC_CLC_RECV_BUF_LEN	100

/* eye catcher "SMCR" EBCDIC for CLC messages */
static const char SMC_EYECATCHER[4] = {'\xe2', '\xd4', '\xc3', '\xd9'};
/* eye catcher "SMCD" EBCDIC for CLC messages */
static const char SMCD_EYECATCHER[4] = {'\xe2', '\xd4', '\xc3', '\xc4'};

/* check if received message has a correct header length and contains valid
 * heading and trailing eyecatchers
 */
static bool smc_clc_msg_hdr_valid(struct smc_clc_msg_hdr *clcm, bool check_trl)
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
		pclc = (struct smc_clc_msg_proposal *)clcm;
		pclc_prfx = smc_clc_proposal_get_prefix(pclc);
		if (ntohs(pclc->hdr.length) <
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
		if (clcm->typev1 != SMC_TYPE_R && clcm->typev1 != SMC_TYPE_D)
			return false;
		clc = (struct smc_clc_msg_accept_confirm *)clcm;
		if ((clcm->typev1 == SMC_TYPE_R &&
		     ntohs(clc->hdr.length) != SMCR_CLC_ACCEPT_CONFIRM_LEN) ||
		    (clcm->typev1 == SMC_TYPE_D &&
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

	if (clcm->type == SMC_CLC_PROPOSAL && clcm->typev1 == SMC_TYPE_N)
		reason_code = SMC_CLC_DECL_VERSMISMAT; /* just V2 offered */

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
	dclc.hdr.version = SMC_V1;
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
int smc_clc_send_proposal(struct smc_sock *smc, int smc_type,
			  struct smc_init_info *ini)
{
	struct smc_clc_msg_proposal_prefix *pclc_prfx;
	struct smc_clc_msg_proposal *pclc_base;
	struct smc_clc_msg_proposal_area *pclc;
	struct smc_clc_ipv6_prefix *ipv6_prfx;
	struct smc_clc_msg_smcd *pclc_smcd;
	struct smc_clc_msg_trail *trl;
	int len, i, plen, rc;
	int reason_code = 0;
	struct kvec vec[5];
	struct msghdr msg;

	pclc = kzalloc(sizeof(*pclc), GFP_KERNEL);
	if (!pclc)
		return -ENOMEM;

	pclc_base = &pclc->pclc_base;
	pclc_smcd = &pclc->pclc_smcd;
	pclc_prfx = &pclc->pclc_prfx;
	ipv6_prfx = pclc->pclc_prfx_ipv6;
	trl = &pclc->pclc_trl;

	/* retrieve ip prefixes for CLC proposal msg */
	rc = smc_clc_prfx_set(smc->clcsock, pclc_prfx, ipv6_prfx);
	if (rc) {
		kfree(pclc);
		return SMC_CLC_DECL_CNFERR; /* configuration error */
	}

	/* send SMC Proposal CLC message */
	plen = sizeof(*pclc_base) + sizeof(*pclc_prfx) +
	       (pclc_prfx->ipv6_prefixes_cnt * sizeof(ipv6_prfx[0])) +
	       sizeof(*trl);
	memcpy(pclc_base->hdr.eyecatcher, SMC_EYECATCHER,
	       sizeof(SMC_EYECATCHER));
	pclc_base->hdr.type = SMC_CLC_PROPOSAL;
	pclc_base->hdr.version = SMC_V1;		/* SMC version */
	pclc_base->hdr.typev1 = smc_type;
	if (smcr_indicated(smc_type)) {
		/* add SMC-R specifics */
		memcpy(pclc_base->lcl.id_for_peer, local_systemid,
		       sizeof(local_systemid));
		memcpy(pclc_base->lcl.gid, ini->ib_gid, SMC_GID_SIZE);
		memcpy(pclc_base->lcl.mac, &ini->ib_dev->mac[ini->ib_port - 1],
		       ETH_ALEN);
		pclc_base->iparea_offset = htons(0);
	}
	if (smcd_indicated(smc_type)) {
		/* add SMC-D specifics */
		plen += sizeof(*pclc_smcd);
		pclc_base->iparea_offset = htons(sizeof(*pclc_smcd));
		pclc_smcd->gid = ini->ism_dev[0]->local_gid;
	}
	pclc_base->hdr.length = htons(plen);

	memcpy(trl->eyecatcher, SMC_EYECATCHER, sizeof(SMC_EYECATCHER));
	memset(&msg, 0, sizeof(msg));
	i = 0;
	vec[i].iov_base = pclc_base;
	vec[i++].iov_len = sizeof(*pclc_base);
	if (smcd_indicated(smc_type)) {
		vec[i].iov_base = pclc_smcd;
		vec[i++].iov_len = sizeof(*pclc_smcd);
	}
	vec[i].iov_base = pclc_prfx;
	vec[i++].iov_len = sizeof(*pclc_prfx);
	if (pclc_prfx->ipv6_prefixes_cnt > 0) {
		vec[i].iov_base = ipv6_prfx;
		vec[i++].iov_len = pclc_prfx->ipv6_prefixes_cnt *
				   sizeof(ipv6_prfx[0]);
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
				       struct smc_clc_msg_accept_confirm *clc,
				       int first_contact)
{
	struct smc_connection *conn = &smc->conn;
	struct smc_clc_msg_trail trl;
	struct kvec vec[2];
	struct msghdr msg;
	int i;

	/* send SMC Confirm CLC msg */
	clc->hdr.version = SMC_V1;		/* SMC version */
	if (first_contact)
		clc->hdr.typev2 |= SMC_FIRST_CONTACT_MASK;
	if (conn->lgr->is_smcd) {
		/* SMC-D specific settings */
		memcpy(clc->hdr.eyecatcher, SMCD_EYECATCHER,
		       sizeof(SMCD_EYECATCHER));
		clc->hdr.typev1 = SMC_TYPE_D;
		clc->hdr.length = htons(SMCD_CLC_ACCEPT_CONFIRM_LEN);
		clc->d0.gid = conn->lgr->smcd->local_gid;
		clc->d0.token = conn->rmb_desc->token;
		clc->d0.dmbe_size = conn->rmbe_size_short;
		clc->d0.dmbe_idx = 0;
		memcpy(&clc->d0.linkid, conn->lgr->id, SMC_LGR_ID_SIZE);
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
	vec[i].iov_base = clc;
	vec[i++].iov_len = (clc->hdr.typev1 == SMC_TYPE_D ?
			    SMCD_CLC_ACCEPT_CONFIRM_LEN :
			    SMCR_CLC_ACCEPT_CONFIRM_LEN) -
			   sizeof(trl);
	vec[i].iov_base = &trl;
	vec[i++].iov_len = sizeof(trl);
	return kernel_sendmsg(smc->clcsock, &msg, vec, 1,
			      ntohs(clc->hdr.length));
}

/* send CLC CONFIRM message across internal TCP socket */
int smc_clc_send_confirm(struct smc_sock *smc)
{
	struct smc_clc_msg_accept_confirm cclc;
	int reason_code = 0;
	int len;

	/* send SMC Confirm CLC msg */
	memset(&cclc, 0, sizeof(cclc));
	cclc.hdr.type = SMC_CLC_CONFIRM;
	len = smc_clc_send_confirm_accept(smc, &cclc, 0);
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
int smc_clc_send_accept(struct smc_sock *new_smc, bool srv_first_contact)
{
	struct smc_clc_msg_accept_confirm aclc;
	int len;

	memset(&aclc, 0, sizeof(aclc));
	aclc.hdr.type = SMC_CLC_ACCEPT;
	len = smc_clc_send_confirm_accept(new_smc, &aclc, srv_first_contact);
	if (len < ntohs(aclc.hdr.length))
		len = len >= 0 ? -EPROTO : -new_smc->clcsock->sk->sk_err;

	return len > 0 ? 0 : len;
}
