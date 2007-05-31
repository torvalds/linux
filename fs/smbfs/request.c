/*
 *  request.c
 *
 *  Copyright (C) 2001 by Urban Widmark
 *
 *  Please add a note about your changes to smbfs in the ChangeLog file.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/net.h>
#include <linux/sched.h>

#include <linux/smb_fs.h>
#include <linux/smbno.h>
#include <linux/smb_mount.h>

#include "smb_debug.h"
#include "request.h"
#include "proto.h"

/* #define SMB_SLAB_DEBUG	(SLAB_RED_ZONE | SLAB_POISON) */
#define SMB_SLAB_DEBUG	0

/* cache for request structures */
static struct kmem_cache *req_cachep;

static int smb_request_send_req(struct smb_request *req);

/*
  /proc/slabinfo:
  name, active, num, objsize, active_slabs, num_slaps, #pages
*/


int smb_init_request_cache(void)
{
	req_cachep = kmem_cache_create("smb_request",
				       sizeof(struct smb_request), 0,
				       SMB_SLAB_DEBUG | SLAB_HWCACHE_ALIGN,
				       NULL, NULL);
	if (req_cachep == NULL)
		return -ENOMEM;

	return 0;
}

void smb_destroy_request_cache(void)
{
	kmem_cache_destroy(req_cachep);
}

/*
 * Allocate and initialise a request structure
 */
static struct smb_request *smb_do_alloc_request(struct smb_sb_info *server,
						int bufsize)
{
	struct smb_request *req;
	unsigned char *buf = NULL;

	req = kmem_cache_zalloc(req_cachep, GFP_KERNEL);
	VERBOSE("allocating request: %p\n", req);
	if (!req)
		goto out;

	if (bufsize > 0) {
		buf = kmalloc(bufsize, GFP_NOFS);
		if (!buf) {
			kmem_cache_free(req_cachep, req);
			return NULL;
		}
	}

	req->rq_buffer = buf;
	req->rq_bufsize = bufsize;
	req->rq_server = server;
	init_waitqueue_head(&req->rq_wait);
	INIT_LIST_HEAD(&req->rq_queue);
	atomic_set(&req->rq_count, 1);

out:
	return req;
}

struct smb_request *smb_alloc_request(struct smb_sb_info *server, int bufsize)
{
	struct smb_request *req = NULL;

	for (;;) {
		atomic_inc(&server->nr_requests);
		if (atomic_read(&server->nr_requests) <= MAX_REQUEST_HARD) {
			req = smb_do_alloc_request(server, bufsize);
			if (req != NULL)
				break;
		}

#if 0
		/*
		 * Try to free up at least one request in order to stay
		 * below the hard limit
		 */
                if (nfs_try_to_free_pages(server))
			continue;

		if (signalled() && (server->flags & NFS_MOUNT_INTR))
			return ERR_PTR(-ERESTARTSYS);
		current->policy = SCHED_YIELD;
		schedule();
#else
		/* FIXME: we want something like nfs does above, but that
		   requires changes to all callers and can wait. */
		break;
#endif
	}
	return req;
}

static void smb_free_request(struct smb_request *req)
{
	atomic_dec(&req->rq_server->nr_requests);
	if (req->rq_buffer && !(req->rq_flags & SMB_REQ_STATIC))
		kfree(req->rq_buffer);
	kfree(req->rq_trans2buffer);
	kmem_cache_free(req_cachep, req);
}

/*
 * What prevents a rget to race with a rput? The count must never drop to zero
 * while it is in use. Only rput if it is ok that it is free'd.
 */
static void smb_rget(struct smb_request *req)
{
	atomic_inc(&req->rq_count);
}
void smb_rput(struct smb_request *req)
{
	if (atomic_dec_and_test(&req->rq_count)) {
		list_del_init(&req->rq_queue);
		smb_free_request(req);
	}
}

/* setup to receive the data part of the SMB */
static int smb_setup_bcc(struct smb_request *req)
{
	int result = 0;
	req->rq_rlen = smb_len(req->rq_header) + 4 - req->rq_bytes_recvd;

	if (req->rq_rlen > req->rq_bufsize) {
		PARANOIA("Packet too large %d > %d\n",
			 req->rq_rlen, req->rq_bufsize);
		return -ENOBUFS;
	}

	req->rq_iov[0].iov_base = req->rq_buffer;
	req->rq_iov[0].iov_len  = req->rq_rlen;
	req->rq_iovlen = 1;

	return result;
}

/*
 * Prepare a "normal" request structure.
 */
static int smb_setup_request(struct smb_request *req)
{
	int len = smb_len(req->rq_header) + 4;
	req->rq_slen = len;

	/* if we expect a data part in the reply we set the iov's to read it */
	if (req->rq_resp_bcc)
		req->rq_setup_read = smb_setup_bcc;

	/* This tries to support re-using the same request */
	req->rq_bytes_sent = 0;
	req->rq_rcls = 0;
	req->rq_err = 0;
	req->rq_errno = 0;
	req->rq_fragment = 0;
	kfree(req->rq_trans2buffer);
	req->rq_trans2buffer = NULL;

	return 0;
}

/*
 * Prepare a transaction2 request structure
 */
static int smb_setup_trans2request(struct smb_request *req)
{
	struct smb_sb_info *server = req->rq_server;
	int mparam, mdata;
	static unsigned char padding[4];

	/* I know the following is very ugly, but I want to build the
	   smb packet as efficiently as possible. */

	const int smb_parameters = 15;
	const int header = SMB_HEADER_LEN + 2 * smb_parameters + 2;
	const int oparam = ALIGN(header + 3, sizeof(u32));
	const int odata  = ALIGN(oparam + req->rq_lparm, sizeof(u32));
	const int bcc = (req->rq_data ? odata + req->rq_ldata :
					oparam + req->rq_lparm) - header;

	if ((bcc + oparam) > server->opt.max_xmit)
		return -ENOMEM;
	smb_setup_header(req, SMBtrans2, smb_parameters, bcc);

	/*
	 * max parameters + max data + max setup == bufsize to make NT4 happy
	 * and not abort the transfer or split into multiple responses. It also
	 * makes smbfs happy as handling packets larger than the buffer size
	 * is extra work.
	 *
	 * OS/2 is probably going to hate me for this ...
	 */
	mparam = SMB_TRANS2_MAX_PARAM;
	mdata = req->rq_bufsize - mparam;

	mdata = server->opt.max_xmit - mparam - 100;
	if (mdata < 1024) {
		mdata = 1024;
		mparam = 20;
	}

#if 0
	/* NT/win2k has ~4k max_xmit, so with this we request more than it wants
	   to return as one SMB. Useful for testing the fragmented trans2
	   handling. */
	mdata = 8192;
#endif

	WSET(req->rq_header, smb_tpscnt, req->rq_lparm);
	WSET(req->rq_header, smb_tdscnt, req->rq_ldata);
	WSET(req->rq_header, smb_mprcnt, mparam);
	WSET(req->rq_header, smb_mdrcnt, mdata);
	WSET(req->rq_header, smb_msrcnt, 0);    /* max setup always 0 ? */
	WSET(req->rq_header, smb_flags, 0);
	DSET(req->rq_header, smb_timeout, 0);
	WSET(req->rq_header, smb_pscnt, req->rq_lparm);
	WSET(req->rq_header, smb_psoff, oparam - 4);
	WSET(req->rq_header, smb_dscnt, req->rq_ldata);
	WSET(req->rq_header, smb_dsoff, req->rq_data ? odata - 4 : 0);
	*(req->rq_header + smb_suwcnt) = 0x01;          /* setup count */
	*(req->rq_header + smb_suwcnt + 1) = 0x00;      /* reserved */
	WSET(req->rq_header, smb_setup0, req->rq_trans2_command);

	req->rq_iovlen = 2;
	req->rq_iov[0].iov_base = (void *) req->rq_header;
	req->rq_iov[0].iov_len = oparam;
	req->rq_iov[1].iov_base = (req->rq_parm==NULL) ? padding : req->rq_parm;
	req->rq_iov[1].iov_len = req->rq_lparm;
	req->rq_slen = oparam + req->rq_lparm;

	if (req->rq_data) {
		req->rq_iovlen += 2;
		req->rq_iov[2].iov_base = padding;
		req->rq_iov[2].iov_len = odata - oparam - req->rq_lparm;
		req->rq_iov[3].iov_base = req->rq_data;
		req->rq_iov[3].iov_len = req->rq_ldata;
		req->rq_slen = odata + req->rq_ldata;
	}

	/* always a data part for trans2 replies */
	req->rq_setup_read = smb_setup_bcc;

	return 0;
}

/*
 * Add a request and tell smbiod to process it
 */
int smb_add_request(struct smb_request *req)
{
	long timeleft;
	struct smb_sb_info *server = req->rq_server;
	int result = 0;

	smb_setup_request(req);
	if (req->rq_trans2_command) {
		if (req->rq_buffer == NULL) {
			PARANOIA("trans2 attempted without response buffer!\n");
			return -EIO;
		}
		result = smb_setup_trans2request(req);
	}
	if (result < 0)
		return result;

#ifdef SMB_DEBUG_PACKET_SIZE
	add_xmit_stats(req);
#endif

	/* add 'req' to the queue of requests */
	if (smb_lock_server_interruptible(server))
		return -EINTR;

	/*
	 * Try to send the request as the process. If that fails we queue the
	 * request and let smbiod send it later.
	 */

	/* FIXME: each server has a number on the maximum number of parallel
	   requests. 10, 50 or so. We should not allow more requests to be
	   active. */
	if (server->mid > 0xf000)
		server->mid = 0;
	req->rq_mid = server->mid++;
	WSET(req->rq_header, smb_mid, req->rq_mid);

	result = 0;
	if (server->state == CONN_VALID) {
		if (list_empty(&server->xmitq))
			result = smb_request_send_req(req);
		if (result < 0) {
			/* Connection lost? */
			server->conn_error = result;
			server->state = CONN_INVALID;
		}
	}
	if (result != 1)
		list_add_tail(&req->rq_queue, &server->xmitq);
	smb_rget(req);

	if (server->state != CONN_VALID)
		smbiod_retry(server);

	smb_unlock_server(server);

	smbiod_wake_up();

	timeleft = wait_event_interruptible_timeout(req->rq_wait,
				    req->rq_flags & SMB_REQ_RECEIVED, 30*HZ);
	if (!timeleft || signal_pending(current)) {
		/*
		 * On timeout or on interrupt we want to try and remove the
		 * request from the recvq/xmitq.
		 * First check if the request is still part of a queue. (May
		 * have been removed by some error condition)
		 */
		smb_lock_server(server);
		if (!list_empty(&req->rq_queue)) {
			list_del_init(&req->rq_queue);
			smb_rput(req);
		}
		smb_unlock_server(server);
	}

	if (!timeleft) {
		PARANOIA("request [%p, mid=%d] timed out!\n",
			 req, req->rq_mid);
		VERBOSE("smb_com:  %02x\n", *(req->rq_header + smb_com));
		VERBOSE("smb_rcls: %02x\n", *(req->rq_header + smb_rcls));
		VERBOSE("smb_flg:  %02x\n", *(req->rq_header + smb_flg));
		VERBOSE("smb_tid:  %04x\n", WVAL(req->rq_header, smb_tid));
		VERBOSE("smb_pid:  %04x\n", WVAL(req->rq_header, smb_pid));
		VERBOSE("smb_uid:  %04x\n", WVAL(req->rq_header, smb_uid));
		VERBOSE("smb_mid:  %04x\n", WVAL(req->rq_header, smb_mid));
		VERBOSE("smb_wct:  %02x\n", *(req->rq_header + smb_wct));

		req->rq_rcls = ERRSRV;
		req->rq_err  = ERRtimeout;

		/* Just in case it was "stuck" */
		smbiod_wake_up();
	}
	VERBOSE("woke up, rcls=%d\n", req->rq_rcls);

	if (req->rq_rcls != 0)
		req->rq_errno = smb_errno(req);
	if (signal_pending(current))
		req->rq_errno = -ERESTARTSYS;
	return req->rq_errno;
}

/*
 * Send a request and place it on the recvq if successfully sent.
 * Must be called with the server lock held.
 */
static int smb_request_send_req(struct smb_request *req)
{
	struct smb_sb_info *server = req->rq_server;
	int result;

	if (req->rq_bytes_sent == 0) {
		WSET(req->rq_header, smb_tid, server->opt.tid);
		WSET(req->rq_header, smb_pid, 1);
		WSET(req->rq_header, smb_uid, server->opt.server_uid);
	}

	result = smb_send_request(req);
	if (result < 0 && result != -EAGAIN)
		goto out;

	result = 0;
	if (!(req->rq_flags & SMB_REQ_TRANSMITTED))
		goto out;

	list_move_tail(&req->rq_queue, &server->recvq);
	result = 1;
out:
	return result;
}

/*
 * Sends one request for this server. (smbiod)
 * Must be called with the server lock held.
 * Returns: <0 on error
 *           0 if no request could be completely sent
 *           1 if all data for one request was sent
 */
int smb_request_send_server(struct smb_sb_info *server)
{
	struct list_head *head;
	struct smb_request *req;
	int result;

	if (server->state != CONN_VALID)
		return 0;

	/* dequeue first request, if any */
	req = NULL;
	head = server->xmitq.next;
	if (head != &server->xmitq) {
		req = list_entry(head, struct smb_request, rq_queue);
	}
	if (!req)
		return 0;

	result = smb_request_send_req(req);
	if (result < 0) {
		server->conn_error = result;
		list_move(&req->rq_queue, &server->xmitq);
		result = -EIO;
		goto out;
	}

out:
	return result;
}

/*
 * Try to find a request matching this "mid". Typically the first entry will
 * be the matching one.
 */
static struct smb_request *find_request(struct smb_sb_info *server, int mid)
{
	struct list_head *tmp;
	struct smb_request *req = NULL;

	list_for_each(tmp, &server->recvq) {
		req = list_entry(tmp, struct smb_request, rq_queue);
		if (req->rq_mid == mid) {
			break;
		}
		req = NULL;
	}

	if (!req) {
		VERBOSE("received reply with mid %d but no request!\n",
			WVAL(server->header, smb_mid));
		server->rstate = SMB_RECV_DROP;
	}

	return req;
}

/*
 * Called when we have read the smb header and believe this is a response.
 */
static int smb_init_request(struct smb_sb_info *server, struct smb_request *req)
{
	int hdrlen, wct;

	memcpy(req->rq_header, server->header, SMB_HEADER_LEN);

	wct = *(req->rq_header + smb_wct);
	if (wct > 20) {	
		PARANOIA("wct too large, %d > 20\n", wct);
		server->rstate = SMB_RECV_DROP;
		return 0;
	}

	req->rq_resp_wct = wct;
	hdrlen = SMB_HEADER_LEN + wct*2 + 2;
	VERBOSE("header length: %d   smb_wct: %2d\n", hdrlen, wct);

	req->rq_bytes_recvd = SMB_HEADER_LEN;
	req->rq_rlen = hdrlen;
	req->rq_iov[0].iov_base = req->rq_header;
	req->rq_iov[0].iov_len  = hdrlen;
	req->rq_iovlen = 1;
	server->rstate = SMB_RECV_PARAM;

#ifdef SMB_DEBUG_PACKET_SIZE
	add_recv_stats(smb_len(server->header));
#endif
	return 0;
}

/*
 * Reads the SMB parameters
 */
static int smb_recv_param(struct smb_sb_info *server, struct smb_request *req)
{
	int result;

	result = smb_receive(server, req);
	if (result < 0)
		return result;
	if (req->rq_bytes_recvd < req->rq_rlen)
		return 0;

	VERBOSE("result: %d   smb_bcc:  %04x\n", result,
		WVAL(req->rq_header, SMB_HEADER_LEN +
		     (*(req->rq_header + smb_wct) * 2)));

	result = 0;
	req->rq_iov[0].iov_base = NULL;
	req->rq_rlen = 0;
	if (req->rq_callback)
		req->rq_callback(req);
	else if (req->rq_setup_read)
		result = req->rq_setup_read(req);
	if (result < 0) {
		server->rstate = SMB_RECV_DROP;
		return result;
	}

	server->rstate = req->rq_rlen > 0 ? SMB_RECV_DATA : SMB_RECV_END;

	req->rq_bytes_recvd = 0;	// recvd out of the iov

	VERBOSE("rlen: %d\n", req->rq_rlen);
	if (req->rq_rlen < 0) {
		PARANOIA("Parameters read beyond end of packet!\n");
		server->rstate = SMB_RECV_END;
		return -EIO;
	}
	return 0;
}

/*
 * Reads the SMB data
 */
static int smb_recv_data(struct smb_sb_info *server, struct smb_request *req)
{
	int result;

	result = smb_receive(server, req);
	if (result < 0)
		goto out;
	if (req->rq_bytes_recvd < req->rq_rlen)
		goto out;
	server->rstate = SMB_RECV_END;
out:
	VERBOSE("result: %d\n", result);
	return result;
}

/*
 * Receive a transaction2 response
 * Return: 0 if the response has been fully read
 *         1 if there are further "fragments" to read
 *        <0 if there is an error
 */
static int smb_recv_trans2(struct smb_sb_info *server, struct smb_request *req)
{
	unsigned char *inbuf;
	unsigned int parm_disp, parm_offset, parm_count, parm_tot;
	unsigned int data_disp, data_offset, data_count, data_tot;
	int hdrlen = SMB_HEADER_LEN + req->rq_resp_wct*2 - 2;

	VERBOSE("handling trans2\n");

	inbuf = req->rq_header;
	data_tot    = WVAL(inbuf, smb_tdrcnt);
	parm_tot    = WVAL(inbuf, smb_tprcnt);
	parm_disp   = WVAL(inbuf, smb_prdisp);
	parm_offset = WVAL(inbuf, smb_proff);
	parm_count  = WVAL(inbuf, smb_prcnt);
	data_disp   = WVAL(inbuf, smb_drdisp);
	data_offset = WVAL(inbuf, smb_droff);
	data_count  = WVAL(inbuf, smb_drcnt);

	/* Modify offset for the split header/buffer we use */
	if (data_count || data_offset) {
		if (unlikely(data_offset < hdrlen))
			goto out_bad_data;
		else
			data_offset -= hdrlen;
	}
	if (parm_count || parm_offset) {
		if (unlikely(parm_offset < hdrlen))
			goto out_bad_parm;
		else
			parm_offset -= hdrlen;
	}

	if (parm_count == parm_tot && data_count == data_tot) {
		/*
		 * This packet has all the trans2 data.
		 *
		 * We setup the request so that this will be the common
		 * case. It may be a server error to not return a
		 * response that fits.
		 */
		VERBOSE("single trans2 response  "
			"dcnt=%u, pcnt=%u, doff=%u, poff=%u\n",
			data_count, parm_count,
			data_offset, parm_offset);
		req->rq_ldata = data_count;
		req->rq_lparm = parm_count;
		req->rq_data = req->rq_buffer + data_offset;
		req->rq_parm = req->rq_buffer + parm_offset;
		if (unlikely(parm_offset + parm_count > req->rq_rlen))
			goto out_bad_parm;
		if (unlikely(data_offset + data_count > req->rq_rlen))
			goto out_bad_data;
		return 0;
	}

	VERBOSE("multi trans2 response  "
		"frag=%d, dcnt=%u, pcnt=%u, doff=%u, poff=%u\n",
		req->rq_fragment,
		data_count, parm_count,
		data_offset, parm_offset);

	if (!req->rq_fragment) {
		int buf_len;

		/* We got the first trans2 fragment */
		req->rq_fragment = 1;
		req->rq_total_data = data_tot;
		req->rq_total_parm = parm_tot;
		req->rq_ldata = 0;
		req->rq_lparm = 0;

		buf_len = data_tot + parm_tot;
		if (buf_len > SMB_MAX_PACKET_SIZE)
			goto out_too_long;

		req->rq_trans2bufsize = buf_len;
		req->rq_trans2buffer = kzalloc(buf_len, GFP_NOFS);
		if (!req->rq_trans2buffer)
			goto out_no_mem;

		req->rq_parm = req->rq_trans2buffer;
		req->rq_data = req->rq_trans2buffer + parm_tot;
	} else if (unlikely(req->rq_total_data < data_tot ||
			    req->rq_total_parm < parm_tot))
		goto out_data_grew;

	if (unlikely(parm_disp + parm_count > req->rq_total_parm ||
		     parm_offset + parm_count > req->rq_rlen))
		goto out_bad_parm;
	if (unlikely(data_disp + data_count > req->rq_total_data ||
		     data_offset + data_count > req->rq_rlen))
		goto out_bad_data;

	inbuf = req->rq_buffer;
	memcpy(req->rq_parm + parm_disp, inbuf + parm_offset, parm_count);
	memcpy(req->rq_data + data_disp, inbuf + data_offset, data_count);

	req->rq_ldata += data_count;
	req->rq_lparm += parm_count;

	/*
	 * Check whether we've received all of the data. Note that
	 * we use the packet totals -- total lengths might shrink!
	 */
	if (req->rq_ldata >= data_tot && req->rq_lparm >= parm_tot) {
		req->rq_ldata = data_tot;
		req->rq_lparm = parm_tot;
		return 0;
	}
	return 1;

out_too_long:
	printk(KERN_ERR "smb_trans2: data/param too long, data=%u, parm=%u\n",
		data_tot, parm_tot);
	goto out_EIO;
out_no_mem:
	printk(KERN_ERR "smb_trans2: couldn't allocate data area of %d bytes\n",
	       req->rq_trans2bufsize);
	req->rq_errno = -ENOMEM;
	goto out;
out_data_grew:
	printk(KERN_ERR "smb_trans2: data/params grew!\n");
	goto out_EIO;
out_bad_parm:
	printk(KERN_ERR "smb_trans2: invalid parms, disp=%u, cnt=%u, tot=%u, ofs=%u\n",
	       parm_disp, parm_count, parm_tot, parm_offset);
	goto out_EIO;
out_bad_data:
	printk(KERN_ERR "smb_trans2: invalid data, disp=%u, cnt=%u, tot=%u, ofs=%u\n",
	       data_disp, data_count, data_tot, data_offset);
out_EIO:
	req->rq_errno = -EIO;
out:
	return req->rq_errno;
}

/*
 * State machine for receiving responses. We handle the fact that we can't
 * read the full response in one try by having states telling us how much we
 * have read.
 *
 * Must be called with the server lock held (only called from smbiod).
 *
 * Return: <0 on error
 */
int smb_request_recv(struct smb_sb_info *server)
{
	struct smb_request *req = NULL;
	int result = 0;

	if (smb_recv_available(server) <= 0)
		return 0;

	VERBOSE("state: %d\n", server->rstate);
	switch (server->rstate) {
	case SMB_RECV_DROP:
		result = smb_receive_drop(server);
		if (result < 0)
			break;
		if (server->rstate == SMB_RECV_DROP)
			break;
		server->rstate = SMB_RECV_START;
		/* fallthrough */
	case SMB_RECV_START:
		server->smb_read = 0;
		server->rstate = SMB_RECV_HEADER;
		/* fallthrough */
	case SMB_RECV_HEADER:
		result = smb_receive_header(server);
		if (result < 0)
			break;
		if (server->rstate == SMB_RECV_HEADER)
			break;
		if (! (*(server->header + smb_flg) & SMB_FLAGS_REPLY) ) {
			server->rstate = SMB_RECV_REQUEST;
			break;
		}
		if (server->rstate != SMB_RECV_HCOMPLETE)
			break;
		/* fallthrough */
	case SMB_RECV_HCOMPLETE:
		req = find_request(server, WVAL(server->header, smb_mid));
		if (!req)
			break;
		smb_init_request(server, req);
		req->rq_rcls = *(req->rq_header + smb_rcls);
		req->rq_err  = WVAL(req->rq_header, smb_err);
		if (server->rstate != SMB_RECV_PARAM)
			break;
		/* fallthrough */
	case SMB_RECV_PARAM:
		if (!req)
			req = find_request(server,WVAL(server->header,smb_mid));
		if (!req)
			break;
		result = smb_recv_param(server, req);
		if (result < 0)
			break;
		if (server->rstate != SMB_RECV_DATA)
			break;
		/* fallthrough */
	case SMB_RECV_DATA:
		if (!req)
			req = find_request(server,WVAL(server->header,smb_mid));
		if (!req)
			break;
		result = smb_recv_data(server, req);
		if (result < 0)
			break;
		break;

		/* We should never be called with any of these states */
	case SMB_RECV_END:
	case SMB_RECV_REQUEST:
		BUG();
	}

	if (result < 0) {
		/* We saw an error */
		return result;
	}

	if (server->rstate != SMB_RECV_END)
		return 0;

	result = 0;
	if (req->rq_trans2_command && req->rq_rcls == SUCCESS)
		result = smb_recv_trans2(server, req);

	/*
	 * Response completely read. Drop any extra bytes sent by the server.
	 * (Yes, servers sometimes add extra bytes to responses)
	 */
	VERBOSE("smb_len: %d   smb_read: %d\n",
		server->smb_len, server->smb_read);
	if (server->smb_read < server->smb_len)
		smb_receive_drop(server);

	server->rstate = SMB_RECV_START;

	if (!result) {
		list_del_init(&req->rq_queue);
		req->rq_flags |= SMB_REQ_RECEIVED;
		smb_rput(req);
		wake_up_interruptible(&req->rq_wait);
	}
	return 0;
}
