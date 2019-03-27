/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * Copyright (c) 2006 Mellanox Technologies Ltd.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <linux/tcp.h>
#include <asm/ioctls.h>
#include <linux/workqueue.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <net/protocol.h>
#include <net/inet_common.h>
#include <rdma/rdma_cm.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_fmr_pool.h>
#include <rdma/ib_umem.h> 
#include <net/tcp.h> /* for memcpy_toiovec */
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include "sdp.h"

static int sdp_post_srcavail(struct socket *sk, struct tx_srcavail_state *tx_sa)
{
	struct sdp_sock *ssk = sdp_sk(sk);
	struct mbuf *mb;
	int payload_len;
	struct page *payload_pg;
	int off, len;
	struct ib_umem_chunk *chunk;

	WARN_ON(ssk->tx_sa);

	BUG_ON(!tx_sa);
	BUG_ON(!tx_sa->fmr || !tx_sa->fmr->fmr->lkey);
	BUG_ON(!tx_sa->umem);
	BUG_ON(!tx_sa->umem->chunk_list.next);

	chunk = list_entry(tx_sa->umem->chunk_list.next, struct ib_umem_chunk, list);
	BUG_ON(!chunk->nmap);

	off = tx_sa->umem->offset;
	len = tx_sa->umem->length;

	tx_sa->bytes_sent = tx_sa->bytes_acked = 0;

	mb = sdp_alloc_mb_srcavail(sk, len, tx_sa->fmr->fmr->lkey, off, 0);
	if (!mb) {
		return -ENOMEM;
	}
	sdp_dbg_data(sk, "sending SrcAvail\n");
		
	TX_SRCAVAIL_STATE(mb) = tx_sa; /* tx_sa is hanged on the mb 
					 * but continue to live after mb is freed */
	ssk->tx_sa = tx_sa;

	/* must have payload inlined in SrcAvail packet in combined mode */
	payload_len = MIN(tx_sa->umem->page_size - off, len);
	payload_len = MIN(payload_len, ssk->xmit_size_goal - sizeof(struct sdp_srcah));
	payload_pg  = sg_page(&chunk->page_list[0]);
	get_page(payload_pg);

	sdp_dbg_data(sk, "payload: off: 0x%x, pg: %p, len: 0x%x\n",
		off, payload_pg, payload_len);

	mb_fill_page_desc(mb, mb_shinfo(mb)->nr_frags,
			payload_pg, off, payload_len);

	mb->len             += payload_len;
	mb->data_len         = payload_len;
	mb->truesize        += payload_len;
//	sk->sk_wmem_queued   += payload_len;
//	sk->sk_forward_alloc -= payload_len;

	mb_entail(sk, ssk, mb);
	
	ssk->write_seq += payload_len;
	SDP_SKB_CB(mb)->end_seq += payload_len;

	tx_sa->bytes_sent = tx_sa->umem->length;
	tx_sa->bytes_acked = payload_len;

	/* TODO: pushing the mb into the tx_queue should be enough */

	return 0;
}

static int sdp_post_srcavail_cancel(struct socket *sk)
{
	struct sdp_sock *ssk = sdp_sk(sk);
	struct mbuf *mb;

	sdp_dbg_data(ssk->socket, "Posting srcavail cancel\n");

	mb = sdp_alloc_mb_srcavail_cancel(sk, 0);
	mb_entail(sk, ssk, mb);

	sdp_post_sends(ssk, 0);

	schedule_delayed_work(&ssk->srcavail_cancel_work,
			SDP_SRCAVAIL_CANCEL_TIMEOUT);

	return 0;
}

void srcavail_cancel_timeout(struct work_struct *work)
{
	struct sdp_sock *ssk =
		container_of(work, struct sdp_sock, srcavail_cancel_work.work);
	struct socket *sk = ssk->socket;

	lock_sock(sk);

	sdp_dbg_data(sk, "both SrcAvail and SrcAvailCancel timedout."
			" closing connection\n");
	sdp_set_error(sk, -ECONNRESET);
	wake_up(&ssk->wq);

	release_sock(sk);
}

static int sdp_wait_rdmardcompl(struct sdp_sock *ssk, long *timeo_p,
		int ignore_signals)
{
	struct socket *sk = ssk->socket;
	int err = 0;
	long vm_wait = 0;
	long current_timeo = *timeo_p;
	struct tx_srcavail_state *tx_sa = ssk->tx_sa;
	DEFINE_WAIT(wait);

	sdp_dbg_data(sk, "sleep till RdmaRdCompl. timeo = %ld.\n", *timeo_p);
	sdp_prf1(sk, NULL, "Going to sleep");
	while (ssk->qp_active) {
		prepare_to_wait(sk->sk_sleep, &wait, TASK_INTERRUPTIBLE);

		if (unlikely(!*timeo_p)) {
			err = -ETIME;
			tx_sa->abort_flags |= TX_SA_TIMEDOUT;
			sdp_prf1(sk, NULL, "timeout");
			SDPSTATS_COUNTER_INC(zcopy_tx_timeout);
			break;
		}

		else if (tx_sa->bytes_acked > tx_sa->bytes_sent) {
			err = -EINVAL;
			sdp_dbg_data(sk, "acked bytes > sent bytes\n");
			tx_sa->abort_flags |= TX_SA_ERROR;
			break;
		}

		if (tx_sa->abort_flags & TX_SA_SENDSM) {
			sdp_prf1(sk, NULL, "Aborting SrcAvail sending");
			SDPSTATS_COUNTER_INC(zcopy_tx_aborted);
			err = -EAGAIN;
			break ;
		}

		if (!ignore_signals) {
			if (signal_pending(current)) {
				err = -EINTR;
				sdp_prf1(sk, NULL, "signalled");
				tx_sa->abort_flags |= TX_SA_INTRRUPTED;
				break;
			}

			if (ssk->rx_sa && (tx_sa->bytes_acked < tx_sa->bytes_sent)) {
				sdp_dbg_data(sk, "Crossing SrcAvail - aborting this\n");
				tx_sa->abort_flags |= TX_SA_CROSS_SEND;
				SDPSTATS_COUNTER_INC(zcopy_cross_send);
				err = -ETIME;
				break ;
			}
		}

		posts_handler_put(ssk);

		sk_wait_event(sk, &current_timeo,
				tx_sa->abort_flags &&
				ssk->rx_sa &&
				(tx_sa->bytes_acked < tx_sa->bytes_sent) && 
				vm_wait);
		sdp_dbg_data(ssk->socket, "woke up sleepers\n");

		posts_handler_get(ssk);

		if (tx_sa->bytes_acked == tx_sa->bytes_sent)
			break;

		if (vm_wait) {
			vm_wait -= current_timeo;
			current_timeo = *timeo_p;
			if (current_timeo != MAX_SCHEDULE_TIMEOUT &&
			    (current_timeo -= vm_wait) < 0)
				current_timeo = 0;
			vm_wait = 0;
		}
		*timeo_p = current_timeo;
	}

	finish_wait(sk->sk_sleep, &wait);

	sdp_dbg_data(sk, "Finished waiting - RdmaRdCompl: %d/%d bytes, flags: 0x%x\n",
			tx_sa->bytes_acked, tx_sa->bytes_sent, tx_sa->abort_flags);

	if (!ssk->qp_active) {
		sdp_dbg(sk, "QP destroyed while waiting\n");
		return -EINVAL;
	}
	return err;
}

static void sdp_wait_rdma_wr_finished(struct sdp_sock *ssk)
{
	struct socket *sk = ssk->socket;
	long timeo = HZ * 5; /* Timeout for for RDMA read */
	DEFINE_WAIT(wait);

	sdp_dbg_data(sk, "Sleep till RDMA wr finished.\n");
	while (1) {
		prepare_to_wait(sk->sk_sleep, &wait, TASK_UNINTERRUPTIBLE);

		if (!ssk->tx_ring.rdma_inflight->busy) {
			sdp_dbg_data(sk, "got rdma cqe\n");
			break;
		}

		if (!ssk->qp_active) {
			sdp_dbg_data(sk, "QP destroyed\n");
			break;
		}

		if (!timeo) {
			sdp_warn(sk, "Panic: Timed out waiting for RDMA read\n");
			WARN_ON(1);
			break;
		}

		posts_handler_put(ssk);

		sdp_prf1(sk, NULL, "Going to sleep");
		sk_wait_event(sk, &timeo, 
			!ssk->tx_ring.rdma_inflight->busy);
		sdp_prf1(sk, NULL, "Woke up");
		sdp_dbg_data(ssk->socket, "woke up sleepers\n");

		posts_handler_get(ssk);
	}

	finish_wait(sk->sk_sleep, &wait);

	sdp_dbg_data(sk, "Finished waiting\n");
}

int sdp_post_rdma_rd_compl(struct sdp_sock *ssk,
		struct rx_srcavail_state *rx_sa)
{
	struct mbuf *mb;
	int copied = rx_sa->used - rx_sa->reported;

	if (rx_sa->used <= rx_sa->reported)
		return 0;

	mb = sdp_alloc_mb_rdmardcompl(ssk->socket, copied, 0);

	rx_sa->reported += copied;

	/* TODO: What if no tx_credits available? */
	sdp_post_send(ssk, mb);

	return 0;
}

int sdp_post_sendsm(struct socket *sk)
{
	struct mbuf *mb = sdp_alloc_mb_sendsm(sk, 0);

	sdp_post_send(sdp_sk(sk), mb);

	return 0;
}

static int sdp_update_iov_used(struct socket *sk, struct iovec *iov, int len)
{
	sdp_dbg_data(sk, "updating consumed 0x%x bytes from iov\n", len);
	while (len > 0) {
		if (iov->iov_len) {
			int copy = min_t(unsigned int, iov->iov_len, len);
			len -= copy;
			iov->iov_len -= copy;
			iov->iov_base += copy;
		}
		iov++;
	}

	return 0;
}

static inline int sge_bytes(struct ib_sge *sge, int sge_cnt)
{
	int bytes = 0;

	while (sge_cnt > 0) {
		bytes += sge->length;
		sge++;
		sge_cnt--;
	}

	return bytes;
}
void sdp_handle_sendsm(struct sdp_sock *ssk, u32 mseq_ack)
{
	struct socket *sk = ssk->socket;
	unsigned long flags;

	spin_lock_irqsave(&ssk->tx_sa_lock, flags);

	if (!ssk->tx_sa) {
		sdp_prf1(sk, NULL, "SendSM for cancelled/finished SrcAvail");
		goto out;
	}

	if (ssk->tx_sa->mseq > mseq_ack) {
		sdp_dbg_data(sk, "SendSM arrived for old SrcAvail. "
			"SendSM mseq_ack: 0x%x, SrcAvail mseq: 0x%x\n",
			mseq_ack, ssk->tx_sa->mseq);
		goto out;
	}

	sdp_dbg_data(sk, "Got SendSM - aborting SrcAvail\n");

	ssk->tx_sa->abort_flags |= TX_SA_SENDSM;
	cancel_delayed_work(&ssk->srcavail_cancel_work);

	wake_up(sk->sk_sleep);
	sdp_dbg_data(sk, "woke up sleepers\n");

out:
	spin_unlock_irqrestore(&ssk->tx_sa_lock, flags);
}

void sdp_handle_rdma_read_compl(struct sdp_sock *ssk, u32 mseq_ack,
		u32 bytes_completed)
{
	struct socket *sk = ssk->socket;
	unsigned long flags;

	sdp_prf1(sk, NULL, "RdmaRdCompl ssk=%p tx_sa=%p", ssk, ssk->tx_sa);
	sdp_dbg_data(sk, "RdmaRdCompl ssk=%p tx_sa=%p\n", ssk, ssk->tx_sa);

	spin_lock_irqsave(&ssk->tx_sa_lock, flags);

	BUG_ON(!ssk);

	if (!ssk->tx_sa) {
		sdp_dbg_data(sk, "Got RdmaRdCompl for aborted SrcAvail\n");
		goto out;
	}

	if (ssk->tx_sa->mseq > mseq_ack) {
		sdp_dbg_data(sk, "RdmaRdCompl arrived for old SrcAvail. "
			"SendSM mseq_ack: 0x%x, SrcAvail mseq: 0x%x\n",
			mseq_ack, ssk->tx_sa->mseq);
		goto out;
	}

	ssk->tx_sa->bytes_acked += bytes_completed;

	wake_up(sk->sk_sleep);
	sdp_dbg_data(sk, "woke up sleepers\n");

out:
	spin_unlock_irqrestore(&ssk->tx_sa_lock, flags);
	return;
}

static unsigned long sdp_get_max_memlockable_bytes(unsigned long offset)
{
	unsigned long avail;
	unsigned long lock_limit;

	if (capable(CAP_IPC_LOCK))
		return ULONG_MAX;

	lock_limit = current->signal->rlim[RLIMIT_MEMLOCK].rlim_cur;
	avail = lock_limit - (current->mm->locked_vm << PAGE_SHIFT);

	return avail - offset;
}

static int sdp_alloc_fmr(struct socket *sk, void *uaddr, size_t len,
	struct ib_pool_fmr **_fmr, struct ib_umem **_umem)
{
	struct ib_pool_fmr *fmr;
	struct ib_umem *umem;
	struct ib_device *dev;
	u64 *pages;
	struct ib_umem_chunk *chunk;
	int n, j, k;
	int rc = 0;
	unsigned long max_lockable_bytes;

	if (unlikely(len > SDP_MAX_RDMA_READ_LEN)) {
		sdp_dbg_data(sk, "len:0x%lx > FMR_SIZE: 0x%lx\n",
			len, SDP_MAX_RDMA_READ_LEN);
		len = SDP_MAX_RDMA_READ_LEN;
	}

	max_lockable_bytes = sdp_get_max_memlockable_bytes((unsigned long)uaddr & ~PAGE_MASK);
	if (unlikely(len > max_lockable_bytes)) {
		sdp_dbg_data(sk, "len:0x%lx > RLIMIT_MEMLOCK available: 0x%lx\n",
			len, max_lockable_bytes);
		len = max_lockable_bytes;
	}

	sdp_dbg_data(sk, "user buf: %p, len:0x%lx max_lockable_bytes: 0x%lx\n",
			uaddr, len, max_lockable_bytes);

	umem = ib_umem_get(&sdp_sk(sk)->context, (unsigned long)uaddr, len,
		IB_ACCESS_REMOTE_WRITE, 0);

	if (IS_ERR(umem)) {
		rc = PTR_ERR(umem);
		sdp_warn(sk, "Error doing umem_get 0x%lx bytes: %d\n", len, rc);
		sdp_warn(sk, "RLIMIT_MEMLOCK: 0x%lx[cur] 0x%lx[max] CAP_IPC_LOCK: %d\n",
				current->signal->rlim[RLIMIT_MEMLOCK].rlim_cur,
				current->signal->rlim[RLIMIT_MEMLOCK].rlim_max,
				capable(CAP_IPC_LOCK));
		goto err_umem_get;
	}

	sdp_dbg_data(sk, "umem->offset = 0x%x, length = 0x%lx\n",
		umem->offset, umem->length);

	pages = (u64 *) __get_free_page(GFP_KERNEL);
	if (!pages)
		goto err_pages_alloc;

	n = 0;

	dev = sdp_sk(sk)->ib_device;
	list_for_each_entry(chunk, &umem->chunk_list, list) {
		for (j = 0; j < chunk->nmap; ++j) {
			len = ib_sg_dma_len(dev,
					&chunk->page_list[j]) >> PAGE_SHIFT;

			for (k = 0; k < len; ++k) {
				pages[n++] = ib_sg_dma_address(dev,
						&chunk->page_list[j]) +
					umem->page_size * k;

			}
		}
	}

	fmr = ib_fmr_pool_map_phys(sdp_sk(sk)->sdp_dev->fmr_pool, pages, n, 0);
	if (IS_ERR(fmr)) {
		sdp_warn(sk, "Error allocating fmr: %ld\n", PTR_ERR(fmr));
		goto err_fmr_alloc;
	}

	free_page((unsigned long) pages);

	*_umem = umem;
	*_fmr = fmr;

	return 0;

err_fmr_alloc:	
	free_page((unsigned long) pages);

err_pages_alloc:
	ib_umem_release(umem);

err_umem_get:

	return rc;
}

void sdp_free_fmr(struct socket *sk, struct ib_pool_fmr **_fmr, struct ib_umem **_umem)
{
	if (!sdp_sk(sk)->qp_active)
		return;

	ib_fmr_pool_unmap(*_fmr);
	*_fmr = NULL;

	ib_umem_release(*_umem);
	*_umem = NULL;
}

static int sdp_post_rdma_read(struct socket *sk, struct rx_srcavail_state *rx_sa)
{
	struct sdp_sock *ssk = sdp_sk(sk);
	struct ib_send_wr *bad_wr;
	struct ib_send_wr wr = { NULL };
	struct ib_sge sge;

	wr.opcode = IB_WR_RDMA_READ;
	wr.next = NULL;
	wr.wr_id = SDP_OP_RDMA;
	wr.wr.rdma.rkey = rx_sa->rkey;
	wr.send_flags = 0;

	ssk->tx_ring.rdma_inflight = rx_sa;

	sge.addr = rx_sa->umem->offset;
	sge.length = rx_sa->umem->length;
	sge.lkey = rx_sa->fmr->fmr->lkey;

	wr.wr.rdma.remote_addr = rx_sa->vaddr + rx_sa->used;
	wr.num_sge = 1;
	wr.sg_list = &sge;
	rx_sa->busy++;

	wr.send_flags = IB_SEND_SIGNALED;

	return ib_post_send(ssk->qp, &wr, &bad_wr);
}

int sdp_rdma_to_iovec(struct socket *sk, struct iovec *iov, struct mbuf *mb,
		unsigned long *used)
{
	struct sdp_sock *ssk = sdp_sk(sk);
	struct rx_srcavail_state *rx_sa = RX_SRCAVAIL_STATE(mb);
	int got_srcavail_cancel;
	int rc = 0;
	int len = *used;
	int copied;

	sdp_dbg_data(ssk->socket, "preparing RDMA read."
		" len: 0x%x. buffer len: 0x%lx\n", len, iov->iov_len);

	sock_hold(sk, SOCK_REF_RDMA_RD);

	if (len > rx_sa->len) {
		sdp_warn(sk, "len:0x%x > rx_sa->len: 0x%x\n", len, rx_sa->len);
		WARN_ON(1);
		len = rx_sa->len;
	}

	rc = sdp_alloc_fmr(sk, iov->iov_base, len, &rx_sa->fmr, &rx_sa->umem);
	if (rc) {
		sdp_warn(sk, "Error allocating fmr: %d\n", rc);
		goto err_alloc_fmr;
	}

	rc = sdp_post_rdma_read(sk, rx_sa);
	if (unlikely(rc)) {
		sdp_warn(sk, "ib_post_send failed with status %d.\n", rc);
		sdp_set_error(ssk->socket, -ECONNRESET);
		wake_up(&ssk->wq);
		goto err_post_send;
	}

	sdp_prf(sk, mb, "Finished posting(rc=%d), now to wait", rc);

	got_srcavail_cancel = ssk->srcavail_cancel_mseq > rx_sa->mseq;

	sdp_arm_tx_cq(sk);

	sdp_wait_rdma_wr_finished(ssk);

	sdp_prf(sk, mb, "Finished waiting(rc=%d)", rc);
	if (!ssk->qp_active) {
		sdp_dbg_data(sk, "QP destroyed during RDMA read\n");
		rc = -EPIPE;
		goto err_post_send;
	}

	copied = rx_sa->umem->length;

	sdp_update_iov_used(sk, iov, copied);
	rx_sa->used += copied;
	atomic_add(copied, &ssk->rcv_nxt);
	*used = copied;

	ssk->tx_ring.rdma_inflight = NULL;

err_post_send:
	sdp_free_fmr(sk, &rx_sa->fmr, &rx_sa->umem);

err_alloc_fmr:
	if (rc && ssk->qp_active) {
		sdp_warn(sk, "Couldn't do RDMA - post sendsm\n");
		rx_sa->flags |= RX_SA_ABORTED;
	}

	sock_put(sk, SOCK_REF_RDMA_RD);

	return rc;
}

static inline int wait_for_sndbuf(struct socket *sk, long *timeo_p)
{
	struct sdp_sock *ssk = sdp_sk(sk);
	int ret = 0;
	int credits_needed = 1;

	sdp_dbg_data(sk, "Wait for mem\n");

	set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);

	SDPSTATS_COUNTER_INC(send_wait_for_mem);

	sdp_do_posts(ssk);

	sdp_xmit_poll(ssk, 1);

	ret = sdp_tx_wait_memory(ssk, timeo_p, &credits_needed);

	return ret;
}

static int do_sdp_sendmsg_zcopy(struct socket *sk, struct tx_srcavail_state *tx_sa,
		struct iovec *iov, long *timeo)
{
	struct sdp_sock *ssk = sdp_sk(sk);
	int rc = 0;
	unsigned long lock_flags;

	rc = sdp_alloc_fmr(sk, iov->iov_base, iov->iov_len,
			&tx_sa->fmr, &tx_sa->umem);
	if (rc) {
		sdp_warn(sk, "Error allocating fmr: %d\n", rc);
		goto err_alloc_fmr;
	}

	if (tx_slots_free(ssk) == 0) {
		rc = wait_for_sndbuf(sk, timeo);
		if (rc) {
			sdp_warn(sk, "Couldn't get send buffer\n");
			goto err_no_tx_slots;
		}
	}

	rc = sdp_post_srcavail(sk, tx_sa);
	if (rc) {
		sdp_dbg(sk, "Error posting SrcAvail\n");
		goto err_abort_send;
	}

	rc = sdp_wait_rdmardcompl(ssk, timeo, 0);
	if (unlikely(rc)) {
		enum tx_sa_flag f = tx_sa->abort_flags;

		if (f & TX_SA_SENDSM) {
			sdp_dbg_data(sk, "Got SendSM. use SEND verb.\n");
		} else if (f & TX_SA_ERROR) {
			sdp_dbg_data(sk, "SrcAvail error completion\n");
			sdp_reset(sk);
			SDPSTATS_COUNTER_INC(zcopy_tx_error);
		} else if (ssk->qp_active) {
			sdp_post_srcavail_cancel(sk);

			/* Wait for RdmaRdCompl/SendSM to
			 * finish the transaction */
			*timeo = 2 * HZ;
			sdp_dbg_data(sk, "Waiting for SendSM\n");
			sdp_wait_rdmardcompl(ssk, timeo, 1);
			sdp_dbg_data(sk, "finished waiting\n");

			cancel_delayed_work(&ssk->srcavail_cancel_work);
		} else {
			sdp_dbg_data(sk, "QP was destroyed while waiting\n");
		}
	} else {
		sdp_dbg_data(sk, "got RdmaRdCompl\n");
	}

	spin_lock_irqsave(&ssk->tx_sa_lock, lock_flags);
	ssk->tx_sa = NULL;
	spin_unlock_irqrestore(&ssk->tx_sa_lock, lock_flags);

err_abort_send:
	sdp_update_iov_used(sk, iov, tx_sa->bytes_acked);

err_no_tx_slots:
	sdp_free_fmr(sk, &tx_sa->fmr, &tx_sa->umem);

err_alloc_fmr:
	return rc;	
}

int sdp_sendmsg_zcopy(struct kiocb *iocb, struct socket *sk, struct iovec *iov)
{
	struct sdp_sock *ssk = sdp_sk(sk);
	int rc = 0;
	long timeo;
	struct tx_srcavail_state *tx_sa;
	int offset;
	size_t bytes_to_copy = 0;
	int copied = 0;

	sdp_dbg_data(sk, "Sending iov: %p, iov_len: 0x%lx\n",
			iov->iov_base, iov->iov_len);
	sdp_prf1(sk, NULL, "sdp_sendmsg_zcopy start");
	if (ssk->rx_sa) {
		sdp_dbg_data(sk, "Deadlock prevent: crossing SrcAvail\n");
		return 0;
	}

	sock_hold(ssk->socket, SOCK_REF_ZCOPY);

	SDPSTATS_COUNTER_INC(sendmsg_zcopy_segment);

	timeo = SDP_SRCAVAIL_ADV_TIMEOUT ;

	/* Ok commence sending. */
	offset = (unsigned long)iov->iov_base & (PAGE_SIZE - 1);

	tx_sa = kmalloc(sizeof(struct tx_srcavail_state), GFP_KERNEL);
	if (!tx_sa) {
		sdp_warn(sk, "Error allocating zcopy context\n");
		rc = -EAGAIN; /* Buffer too big - fallback to bcopy */
		goto err_alloc_tx_sa;
	}

	bytes_to_copy = iov->iov_len;
	do {
		tx_sa_reset(tx_sa);

		rc = do_sdp_sendmsg_zcopy(sk, tx_sa, iov, &timeo);

		if (iov->iov_len && iov->iov_len < sdp_zcopy_thresh) {
			sdp_dbg_data(sk, "0x%lx bytes left, switching to bcopy\n",
				iov->iov_len);
			break;
		}
	} while (!rc && iov->iov_len > 0 && !tx_sa->abort_flags);

	kfree(tx_sa);
err_alloc_tx_sa:
	copied = bytes_to_copy - iov->iov_len;

	sdp_prf1(sk, NULL, "sdp_sendmsg_zcopy end rc: %d copied: %d", rc, copied);

	sock_put(ssk->socket, SOCK_REF_ZCOPY);

	if (rc < 0 && rc != -EAGAIN && rc != -ETIME)
		return rc;

	return copied;
}

void sdp_abort_srcavail(struct socket *sk)
{
	struct sdp_sock *ssk = sdp_sk(sk);
	struct tx_srcavail_state *tx_sa = ssk->tx_sa;
	unsigned long flags;

	if (!tx_sa)
		return;

	cancel_delayed_work(&ssk->srcavail_cancel_work);
	flush_scheduled_work();

	spin_lock_irqsave(&ssk->tx_sa_lock, flags);

	sdp_free_fmr(sk, &tx_sa->fmr, &tx_sa->umem);

	ssk->tx_sa = NULL;

	spin_unlock_irqrestore(&ssk->tx_sa_lock, flags);
}

void sdp_abort_rdma_read(struct socket *sk)
{
	struct sdp_sock *ssk = sdp_sk(sk);
	struct rx_srcavail_state *rx_sa = ssk->rx_sa;

	if (!rx_sa)
		return;

	sdp_free_fmr(sk, &rx_sa->fmr, &rx_sa->umem);

	ssk->rx_sa = NULL;
}
