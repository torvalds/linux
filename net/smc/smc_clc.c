/*
 *  Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  CLC (connection layer control) handshake over initial TCP socket to
 *  prepare for RDMA traffic
 *
 *  Copyright IBM Corp. 2016
 *
 *  Author(s):  Ursula Braun <ubraun@linux.vnet.ibm.com>
 */

#include <linux/in.h>
#include <linux/if_ether.h>
#include <linux/sched/signal.h>

#include <net/sock.h>
#include <net/tcp.h>

#include "smc.h"
#include "smc_core.h"
#include "smc_clc.h"
#include "smc_ib.h"

/* Wait for data on the tcp-socket, analyze received data
 * Returns:
 * 0 if success and it was not a decline that we received.
 * SMC_CLC_DECL_REPLY if decline received for fallback w/o another decl send.
 * clcsock error, -EINTR, -ECONNRESET, -EPROTO otherwise.
 */
int smc_clc_wait_msg(struct smc_sock *smc, void *buf, int buflen,
		     u8 expected_type)
{
	struct sock *clc_sk = smc->clcsock->sk;
	struct smc_clc_msg_hdr *clcm = buf;
	struct msghdr msg = {NULL, 0};
	int reason_code = 0;
	struct kvec vec;
	int len, datlen;
	int krflags;

	/* peek the first few bytes to determine length of data to receive
	 * so we don't consume any subsequent CLC message or payload data
	 * in the TCP byte stream
	 */
	vec.iov_base = buf;
	vec.iov_len = buflen;
	krflags = MSG_PEEK | MSG_WAITALL;
	smc->clcsock->sk->sk_rcvtimeo = CLC_WAIT_TIME;
	len = kernel_recvmsg(smc->clcsock, &msg, &vec, 1,
			     sizeof(struct smc_clc_msg_hdr), krflags);
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
	    (datlen < sizeof(struct smc_clc_msg_decline)) ||
	    (datlen > sizeof(struct smc_clc_msg_accept_confirm)) ||
	    memcmp(clcm->eyecatcher, SMC_EYECATCHER, sizeof(SMC_EYECATCHER)) ||
	    ((clcm->type != SMC_CLC_DECLINE) &&
	     (clcm->type != expected_type))) {
		smc->sk.sk_err = EPROTO;
		reason_code = -EPROTO;
		goto out;
	}

	/* receive the complete CLC message */
	vec.iov_base = buf;
	vec.iov_len = buflen;
	memset(&msg, 0, sizeof(struct msghdr));
	krflags = MSG_WAITALL;
	smc->clcsock->sk->sk_rcvtimeo = CLC_WAIT_TIME;
	len = kernel_recvmsg(smc->clcsock, &msg, &vec, 1, datlen, krflags);
	if (len < datlen) {
		smc->sk.sk_err = EPROTO;
		reason_code = -EPROTO;
		goto out;
	}
	if (clcm->type == SMC_CLC_DECLINE) {
		reason_code = SMC_CLC_DECL_REPLY;
		if (((struct smc_clc_msg_decline *)buf)->hdr.flag) {
			smc->conn.lgr->sync_err = true;
			smc_lgr_terminate(smc->conn.lgr);
		}
	}

out:
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
	return len;
}

/* send CLC PROPOSAL message across internal TCP socket */
int smc_clc_send_proposal(struct smc_sock *smc,
			  struct smc_ib_device *smcibdev,
			  u8 ibport)
{
	struct smc_clc_msg_proposal pclc;
	int reason_code = 0;
	struct msghdr msg;
	struct kvec vec;
	int len, rc;

	/* send SMC Proposal CLC message */
	memset(&pclc, 0, sizeof(pclc));
	memcpy(pclc.hdr.eyecatcher, SMC_EYECATCHER, sizeof(SMC_EYECATCHER));
	pclc.hdr.type = SMC_CLC_PROPOSAL;
	pclc.hdr.length = htons(sizeof(pclc));
	pclc.hdr.version = SMC_CLC_V1;		/* SMC version */
	memcpy(pclc.lcl.id_for_peer, local_systemid, sizeof(local_systemid));
	memcpy(&pclc.lcl.gid, &smcibdev->gid[ibport - 1], SMC_GID_SIZE);
	memcpy(&pclc.lcl.mac, &smcibdev->mac[ibport - 1], ETH_ALEN);

	/* determine subnet and mask from internal TCP socket */
	rc = smc_netinfo_by_tcpsk(smc->clcsock, &pclc.outgoing_subnet,
				  &pclc.prefix_len);
	if (rc)
		return SMC_CLC_DECL_CNFERR; /* configuration error */
	memcpy(pclc.trl.eyecatcher, SMC_EYECATCHER, sizeof(SMC_EYECATCHER));
	memset(&msg, 0, sizeof(msg));
	vec.iov_base = &pclc;
	vec.iov_len = sizeof(pclc);
	/* due to the few bytes needed for clc-handshake this cannot block */
	len = kernel_sendmsg(smc->clcsock, &msg, &vec, 1, sizeof(pclc));
	if (len < sizeof(pclc)) {
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

	link = &conn->lgr->lnk[SMC_SINGLE_LINK];
	/* send SMC Confirm CLC msg */
	memset(&cclc, 0, sizeof(cclc));
	memcpy(cclc.hdr.eyecatcher, SMC_EYECATCHER, sizeof(SMC_EYECATCHER));
	cclc.hdr.type = SMC_CLC_CONFIRM;
	cclc.hdr.length = htons(sizeof(cclc));
	cclc.hdr.version = SMC_CLC_V1;		/* SMC version */
	memcpy(cclc.lcl.id_for_peer, local_systemid, sizeof(local_systemid));
	memcpy(&cclc.lcl.gid, &link->smcibdev->gid[link->ibport - 1],
	       SMC_GID_SIZE);
	memcpy(&cclc.lcl.mac, &link->smcibdev->mac[link->ibport - 1], ETH_ALEN);
	hton24(cclc.qpn, link->roce_qp->qp_num);
	cclc.rmb_rkey =
		htonl(conn->rmb_desc->mr_rx[SMC_SINGLE_LINK]->rkey);
	cclc.conn_idx = 1; /* for now: 1 RMB = 1 RMBE */
	cclc.rmbe_alert_token = htonl(conn->alert_token_local);
	cclc.qp_mtu = min(link->path_mtu, link->peer_mtu);
	cclc.rmbe_size = conn->rmbe_size_short;
	cclc.rmb_dma_addr = cpu_to_be64(
		(u64)sg_dma_address(conn->rmb_desc->sgt[SMC_SINGLE_LINK].sgl));
	hton24(cclc.psn, link->psn_initial);

	memcpy(cclc.trl.eyecatcher, SMC_EYECATCHER, sizeof(SMC_EYECATCHER));

	memset(&msg, 0, sizeof(msg));
	vec.iov_base = &cclc;
	vec.iov_len = sizeof(cclc);
	len = kernel_sendmsg(smc->clcsock, &msg, &vec, 1, sizeof(cclc));
	if (len < sizeof(cclc)) {
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

	link = &conn->lgr->lnk[SMC_SINGLE_LINK];
	memset(&aclc, 0, sizeof(aclc));
	memcpy(aclc.hdr.eyecatcher, SMC_EYECATCHER, sizeof(SMC_EYECATCHER));
	aclc.hdr.type = SMC_CLC_ACCEPT;
	aclc.hdr.length = htons(sizeof(aclc));
	aclc.hdr.version = SMC_CLC_V1;		/* SMC version */
	if (srv_first_contact)
		aclc.hdr.flag = 1;
	memcpy(aclc.lcl.id_for_peer, local_systemid, sizeof(local_systemid));
	memcpy(&aclc.lcl.gid, &link->smcibdev->gid[link->ibport - 1],
	       SMC_GID_SIZE);
	memcpy(&aclc.lcl.mac, link->smcibdev->mac[link->ibport - 1], ETH_ALEN);
	hton24(aclc.qpn, link->roce_qp->qp_num);
	aclc.rmb_rkey =
		htonl(conn->rmb_desc->mr_rx[SMC_SINGLE_LINK]->rkey);
	aclc.conn_idx = 1;			/* as long as 1 RMB = 1 RMBE */
	aclc.rmbe_alert_token = htonl(conn->alert_token_local);
	aclc.qp_mtu = link->path_mtu;
	aclc.rmbe_size = conn->rmbe_size_short,
	aclc.rmb_dma_addr = cpu_to_be64(
		(u64)sg_dma_address(conn->rmb_desc->sgt[SMC_SINGLE_LINK].sgl));
	hton24(aclc.psn, link->psn_initial);
	memcpy(aclc.trl.eyecatcher, SMC_EYECATCHER, sizeof(SMC_EYECATCHER));

	memset(&msg, 0, sizeof(msg));
	vec.iov_base = &aclc;
	vec.iov_len = sizeof(aclc);
	len = kernel_sendmsg(new_smc->clcsock, &msg, &vec, 1, sizeof(aclc));
	if (len < sizeof(aclc)) {
		if (len >= 0)
			new_smc->sk.sk_err = EPROTO;
		else
			new_smc->sk.sk_err = new_smc->clcsock->sk->sk_err;
		rc = sock_error(&new_smc->sk);
	}

	return rc;
}
