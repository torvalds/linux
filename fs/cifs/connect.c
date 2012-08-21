/*
 *   fs/cifs/connect.c
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2009
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
#include <linux/fs.h>
#include <linux/net.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/ctype.h>
#include <linux/utsname.h>
#include <linux/mempool.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/pagevec.h>
#include <linux/freezer.h>
#include <linux/namei.h>
#include <asm/uaccess.h>
#include <asm/processor.h>
#include <linux/inet.h>
#include <net/ipv6.h>
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"
#include "ntlmssp.h"
#include "nterr.h"
#include "rfc1002pdu.h"
#include "fscache.h"

#define CIFS_PORT 445
#define RFC1001_PORT 139

/* SMB echo "timeout" -- FIXME: tunable? */
#define SMB_ECHO_INTERVAL (60 * HZ)

extern mempool_t *cifs_req_poolp;

/* FIXME: should these be tunable? */
#define TLINK_ERROR_EXPIRE	(1 * HZ)
#define TLINK_IDLE_EXPIRE	(600 * HZ)

static int ip_connect(struct TCP_Server_Info *server);
static int generic_ip_connect(struct TCP_Server_Info *server);
static void tlink_rb_insert(struct rb_root *root, struct tcon_link *new_tlink);
static void cifs_prune_tlinks(struct work_struct *work);
static int cifs_setup_volume_info(struct smb_vol *volume_info, char *mount_data,
					const char *devname);

/*
 * cifs tcp session reconnection
 *
 * mark tcp session as reconnecting so temporarily locked
 * mark all smb sessions as reconnecting for tcp session
 * reconnect tcp session
 * wake up waiters on reconnection? - (not needed currently)
 */
static int
cifs_reconnect(struct TCP_Server_Info *server)
{
	int rc = 0;
	struct list_head *tmp, *tmp2;
	struct cifs_ses *ses;
	struct cifs_tcon *tcon;
	struct mid_q_entry *mid_entry;
	struct list_head retry_list;

	spin_lock(&GlobalMid_Lock);
	if (server->tcpStatus == CifsExiting) {
		/* the demux thread will exit normally
		next time through the loop */
		spin_unlock(&GlobalMid_Lock);
		return rc;
	} else
		server->tcpStatus = CifsNeedReconnect;
	spin_unlock(&GlobalMid_Lock);
	server->maxBuf = 0;

	cFYI(1, "Reconnecting tcp session");

	/* before reconnecting the tcp session, mark the smb session (uid)
		and the tid bad so they are not used until reconnected */
	cFYI(1, "%s: marking sessions and tcons for reconnect", __func__);
	spin_lock(&cifs_tcp_ses_lock);
	list_for_each(tmp, &server->smb_ses_list) {
		ses = list_entry(tmp, struct cifs_ses, smb_ses_list);
		ses->need_reconnect = true;
		ses->ipc_tid = 0;
		list_for_each(tmp2, &ses->tcon_list) {
			tcon = list_entry(tmp2, struct cifs_tcon, tcon_list);
			tcon->need_reconnect = true;
		}
	}
	spin_unlock(&cifs_tcp_ses_lock);

	/* do not want to be sending data on a socket we are freeing */
	cFYI(1, "%s: tearing down socket", __func__);
	mutex_lock(&server->srv_mutex);
	if (server->ssocket) {
		cFYI(1, "State: 0x%x Flags: 0x%lx", server->ssocket->state,
			server->ssocket->flags);
		kernel_sock_shutdown(server->ssocket, SHUT_WR);
		cFYI(1, "Post shutdown state: 0x%x Flags: 0x%lx",
			server->ssocket->state,
			server->ssocket->flags);
		sock_release(server->ssocket);
		server->ssocket = NULL;
	}
	server->sequence_number = 0;
	server->session_estab = false;
	kfree(server->session_key.response);
	server->session_key.response = NULL;
	server->session_key.len = 0;
	server->lstrp = jiffies;
	mutex_unlock(&server->srv_mutex);

	/* mark submitted MIDs for retry and issue callback */
	INIT_LIST_HEAD(&retry_list);
	cFYI(1, "%s: moving mids to private list", __func__);
	spin_lock(&GlobalMid_Lock);
	list_for_each_safe(tmp, tmp2, &server->pending_mid_q) {
		mid_entry = list_entry(tmp, struct mid_q_entry, qhead);
		if (mid_entry->midState == MID_REQUEST_SUBMITTED)
			mid_entry->midState = MID_RETRY_NEEDED;
		list_move(&mid_entry->qhead, &retry_list);
	}
	spin_unlock(&GlobalMid_Lock);

	cFYI(1, "%s: issuing mid callbacks", __func__);
	list_for_each_safe(tmp, tmp2, &retry_list) {
		mid_entry = list_entry(tmp, struct mid_q_entry, qhead);
		list_del_init(&mid_entry->qhead);
		mid_entry->callback(mid_entry);
	}

	do {
		try_to_freeze();

		/* we should try only the port we connected to before */
		rc = generic_ip_connect(server);
		if (rc) {
			cFYI(1, "reconnect error %d", rc);
			msleep(3000);
		} else {
			atomic_inc(&tcpSesReconnectCount);
			spin_lock(&GlobalMid_Lock);
			if (server->tcpStatus != CifsExiting)
				server->tcpStatus = CifsNeedNegotiate;
			spin_unlock(&GlobalMid_Lock);
		}
	} while (server->tcpStatus == CifsNeedReconnect);

	return rc;
}

/*
	return codes:
		0 	not a transact2, or all data present
		>0 	transact2 with that much data missing
		-EINVAL = invalid transact2

 */
static int check2ndT2(struct smb_hdr *pSMB, unsigned int maxBufSize)
{
	struct smb_t2_rsp *pSMBt;
	int remaining;
	__u16 total_data_size, data_in_this_rsp;

	if (pSMB->Command != SMB_COM_TRANSACTION2)
		return 0;

	/* check for plausible wct, bcc and t2 data and parm sizes */
	/* check for parm and data offset going beyond end of smb */
	if (pSMB->WordCount != 10) { /* coalesce_t2 depends on this */
		cFYI(1, "invalid transact2 word count");
		return -EINVAL;
	}

	pSMBt = (struct smb_t2_rsp *)pSMB;

	total_data_size = get_unaligned_le16(&pSMBt->t2_rsp.TotalDataCount);
	data_in_this_rsp = get_unaligned_le16(&pSMBt->t2_rsp.DataCount);

	if (total_data_size == data_in_this_rsp)
		return 0;
	else if (total_data_size < data_in_this_rsp) {
		cFYI(1, "total data %d smaller than data in frame %d",
			total_data_size, data_in_this_rsp);
		return -EINVAL;
	}

	remaining = total_data_size - data_in_this_rsp;

	cFYI(1, "missing %d bytes from transact2, check next response",
		remaining);
	if (total_data_size > maxBufSize) {
		cERROR(1, "TotalDataSize %d is over maximum buffer %d",
			total_data_size, maxBufSize);
		return -EINVAL;
	}
	return remaining;
}

static int coalesce_t2(struct smb_hdr *psecond, struct smb_hdr *pTargetSMB)
{
	struct smb_t2_rsp *pSMB2 = (struct smb_t2_rsp *)psecond;
	struct smb_t2_rsp *pSMBt  = (struct smb_t2_rsp *)pTargetSMB;
	char *data_area_of_target;
	char *data_area_of_buf2;
	int remaining;
	unsigned int byte_count, total_in_buf;
	__u16 total_data_size, total_in_buf2;

	total_data_size = get_unaligned_le16(&pSMBt->t2_rsp.TotalDataCount);

	if (total_data_size !=
	    get_unaligned_le16(&pSMB2->t2_rsp.TotalDataCount))
		cFYI(1, "total data size of primary and secondary t2 differ");

	total_in_buf = get_unaligned_le16(&pSMBt->t2_rsp.DataCount);

	remaining = total_data_size - total_in_buf;

	if (remaining < 0)
		return -EPROTO;

	if (remaining == 0) /* nothing to do, ignore */
		return 0;

	total_in_buf2 = get_unaligned_le16(&pSMB2->t2_rsp.DataCount);
	if (remaining < total_in_buf2) {
		cFYI(1, "transact2 2nd response contains too much data");
	}

	/* find end of first SMB data area */
	data_area_of_target = (char *)&pSMBt->hdr.Protocol +
				get_unaligned_le16(&pSMBt->t2_rsp.DataOffset);
	/* validate target area */

	data_area_of_buf2 = (char *)&pSMB2->hdr.Protocol +
				get_unaligned_le16(&pSMB2->t2_rsp.DataOffset);

	data_area_of_target += total_in_buf;

	/* copy second buffer into end of first buffer */
	total_in_buf += total_in_buf2;
	/* is the result too big for the field? */
	if (total_in_buf > USHRT_MAX)
		return -EPROTO;
	put_unaligned_le16(total_in_buf, &pSMBt->t2_rsp.DataCount);

	/* fix up the BCC */
	byte_count = get_bcc(pTargetSMB);
	byte_count += total_in_buf2;
	/* is the result too big for the field? */
	if (byte_count > USHRT_MAX)
		return -EPROTO;
	put_bcc(byte_count, pTargetSMB);

	byte_count = be32_to_cpu(pTargetSMB->smb_buf_length);
	byte_count += total_in_buf2;
	/* don't allow buffer to overflow */
	if (byte_count > CIFSMaxBufSize)
		return -ENOBUFS;
	pTargetSMB->smb_buf_length = cpu_to_be32(byte_count);

	memcpy(data_area_of_target, data_area_of_buf2, total_in_buf2);

	if (remaining == total_in_buf2) {
		cFYI(1, "found the last secondary response");
		return 0; /* we are done */
	} else /* more responses to go */
		return 1;
}

static void
cifs_echo_request(struct work_struct *work)
{
	int rc;
	struct TCP_Server_Info *server = container_of(work,
					struct TCP_Server_Info, echo.work);

	/*
	 * We cannot send an echo until the NEGOTIATE_PROTOCOL request is
	 * done, which is indicated by maxBuf != 0. Also, no need to ping if
	 * we got a response recently
	 */
	if (server->maxBuf == 0 ||
	    time_before(jiffies, server->lstrp + SMB_ECHO_INTERVAL - HZ))
		goto requeue_echo;

	rc = CIFSSMBEcho(server);
	if (rc)
		cFYI(1, "Unable to send echo request to server: %s",
			server->hostname);

requeue_echo:
	queue_delayed_work(system_nrt_wq, &server->echo, SMB_ECHO_INTERVAL);
}

static int
cifs_demultiplex_thread(struct TCP_Server_Info *server)
{
	int length;
	unsigned int pdu_length, total_read;
	struct smb_hdr *smb_buffer = NULL;
	struct smb_hdr *bigbuf = NULL;
	struct smb_hdr *smallbuf = NULL;
	struct msghdr smb_msg;
	struct kvec iov;
	struct socket *csocket = server->ssocket;
	struct list_head *tmp, *tmp2;
	struct task_struct *task_to_wake = NULL;
	struct mid_q_entry *mid_entry;
	char temp;
	bool isLargeBuf = false;
	bool isMultiRsp;
	int reconnect;

	current->flags |= PF_MEMALLOC;
	cFYI(1, "Demultiplex PID: %d", task_pid_nr(current));

	length = atomic_inc_return(&tcpSesAllocCount);
	if (length > 1)
		mempool_resize(cifs_req_poolp, length + cifs_min_rcv,
				GFP_KERNEL);

	set_freezable();
	while (server->tcpStatus != CifsExiting) {
		if (try_to_freeze())
			continue;
		if (bigbuf == NULL) {
			bigbuf = cifs_buf_get();
			if (!bigbuf) {
				cERROR(1, "No memory for large SMB response");
				msleep(3000);
				/* retry will check if exiting */
				continue;
			}
		} else if (isLargeBuf) {
			/* we are reusing a dirty large buf, clear its start */
			memset(bigbuf, 0, sizeof(struct smb_hdr));
		}

		if (smallbuf == NULL) {
			smallbuf = cifs_small_buf_get();
			if (!smallbuf) {
				cERROR(1, "No memory for SMB response");
				msleep(1000);
				/* retry will check if exiting */
				continue;
			}
			/* beginning of smb buffer is cleared in our buf_get */
		} else /* if existing small buf clear beginning */
			memset(smallbuf, 0, sizeof(struct smb_hdr));

		isLargeBuf = false;
		isMultiRsp = false;
		smb_buffer = smallbuf;
		iov.iov_base = smb_buffer;
		iov.iov_len = 4;
		smb_msg.msg_control = NULL;
		smb_msg.msg_controllen = 0;
		pdu_length = 4; /* enough to get RFC1001 header */

incomplete_rcv:
		if (echo_retries > 0 && server->tcpStatus == CifsGood &&
		    time_after(jiffies, server->lstrp +
					(echo_retries * SMB_ECHO_INTERVAL))) {
			cERROR(1, "Server %s has not responded in %d seconds. "
				  "Reconnecting...", server->hostname,
				  (echo_retries * SMB_ECHO_INTERVAL / HZ));
			cifs_reconnect(server);
			csocket = server->ssocket;
			wake_up(&server->response_q);
			continue;
		}

		length =
		    kernel_recvmsg(csocket, &smb_msg,
				&iov, 1, pdu_length, 0 /* BB other flags? */);

		if (server->tcpStatus == CifsExiting) {
			break;
		} else if (server->tcpStatus == CifsNeedReconnect) {
			cFYI(1, "Reconnect after server stopped responding");
			cifs_reconnect(server);
			cFYI(1, "call to reconnect done");
			csocket = server->ssocket;
			continue;
		} else if (length == -ERESTARTSYS ||
			   length == -EAGAIN ||
			   length == -EINTR) {
			msleep(1); /* minimum sleep to prevent looping
				allowing socket to clear and app threads to set
				tcpStatus CifsNeedReconnect if server hung */
			if (pdu_length < 4) {
				iov.iov_base = (4 - pdu_length) +
							(char *)smb_buffer;
				iov.iov_len = pdu_length;
				smb_msg.msg_control = NULL;
				smb_msg.msg_controllen = 0;
				goto incomplete_rcv;
			} else
				continue;
		} else if (length <= 0) {
			cFYI(1, "Reconnect after unexpected peek error %d",
				length);
			cifs_reconnect(server);
			csocket = server->ssocket;
			wake_up(&server->response_q);
			continue;
		} else if (length < pdu_length) {
			cFYI(1, "requested %d bytes but only got %d bytes",
				  pdu_length, length);
			pdu_length -= length;
			msleep(1);
			goto incomplete_rcv;
		}

		/* The right amount was read from socket - 4 bytes */
		/* so we can now interpret the length field */

		/* the first byte big endian of the length field,
		is actually not part of the length but the type
		with the most common, zero, as regular data */
		temp = *((char *) smb_buffer);

		/* Note that FC 1001 length is big endian on the wire,
		but we convert it here so it is always manipulated
		as host byte order */
		pdu_length = be32_to_cpu(smb_buffer->smb_buf_length);

		cFYI(1, "rfc1002 length 0x%x", pdu_length+4);

		if (temp == (char) RFC1002_SESSION_KEEP_ALIVE) {
			continue;
		} else if (temp == (char)RFC1002_POSITIVE_SESSION_RESPONSE) {
			cFYI(1, "Good RFC 1002 session rsp");
			continue;
		} else if (temp == (char)RFC1002_NEGATIVE_SESSION_RESPONSE) {
			/* we get this from Windows 98 instead of
			   an error on SMB negprot response */
			cFYI(1, "Negative RFC1002 Session Response Error 0x%x)",
				pdu_length);
			/* give server a second to clean up  */
			msleep(1000);
			/* always try 445 first on reconnect since we get NACK
			 * on some if we ever connected to port 139 (the NACK
			 * is since we do not begin with RFC1001 session
			 * initialize frame)
			 */
			cifs_set_port((struct sockaddr *)
					&server->dstaddr, CIFS_PORT);
			cifs_reconnect(server);
			csocket = server->ssocket;
			wake_up(&server->response_q);
			continue;
		} else if (temp != (char) 0) {
			cERROR(1, "Unknown RFC 1002 frame");
			cifs_dump_mem(" Received Data: ", (char *)smb_buffer,
				      length);
			cifs_reconnect(server);
			csocket = server->ssocket;
			continue;
		}

		/* else we have an SMB response */
		if ((pdu_length > CIFSMaxBufSize + MAX_CIFS_HDR_SIZE - 4) ||
			    (pdu_length < sizeof(struct smb_hdr) - 1 - 4)) {
			cERROR(1, "Invalid size SMB length %d pdu_length %d",
					length, pdu_length+4);
			cifs_reconnect(server);
			csocket = server->ssocket;
			wake_up(&server->response_q);
			continue;
		}

		/* else length ok */
		reconnect = 0;

		if (pdu_length > MAX_CIFS_SMALL_BUFFER_SIZE - 4) {
			isLargeBuf = true;
			memcpy(bigbuf, smallbuf, 4);
			smb_buffer = bigbuf;
		}
		length = 0;
		iov.iov_base = 4 + (char *)smb_buffer;
		iov.iov_len = pdu_length;
		for (total_read = 0; total_read < pdu_length;
		     total_read += length) {
			length = kernel_recvmsg(csocket, &smb_msg, &iov, 1,
						pdu_length - total_read, 0);
			if (server->tcpStatus == CifsExiting) {
				/* then will exit */
				reconnect = 2;
				break;
			} else if (server->tcpStatus == CifsNeedReconnect) {
				cifs_reconnect(server);
				csocket = server->ssocket;
				/* Reconnect wakes up rspns q */
				/* Now we will reread sock */
				reconnect = 1;
				break;
			} else if (length == -ERESTARTSYS ||
				   length == -EAGAIN ||
				   length == -EINTR) {
				msleep(1); /* minimum sleep to prevent looping,
					      allowing socket to clear and app
					      threads to set tcpStatus
					      CifsNeedReconnect if server hung*/
				length = 0;
				continue;
			} else if (length <= 0) {
				cERROR(1, "Received no data, expecting %d",
					      pdu_length - total_read);
				cifs_reconnect(server);
				csocket = server->ssocket;
				reconnect = 1;
				break;
			}
		}
		if (reconnect == 2)
			break;
		else if (reconnect == 1)
			continue;

		total_read += 4; /* account for rfc1002 hdr */

		dump_smb(smb_buffer, total_read);

		/*
		 * We know that we received enough to get to the MID as we
		 * checked the pdu_length earlier. Now check to see
		 * if the rest of the header is OK. We borrow the length
		 * var for the rest of the loop to avoid a new stack var.
		 *
		 * 48 bytes is enough to display the header and a little bit
		 * into the payload for debugging purposes.
		 */
		length = checkSMB(smb_buffer, smb_buffer->Mid, total_read);
		if (length != 0)
			cifs_dump_mem("Bad SMB: ", smb_buffer,
					min_t(unsigned int, total_read, 48));

		mid_entry = NULL;
		server->lstrp = jiffies;

		spin_lock(&GlobalMid_Lock);
		list_for_each_safe(tmp, tmp2, &server->pending_mid_q) {
			mid_entry = list_entry(tmp, struct mid_q_entry, qhead);

			if (mid_entry->mid != smb_buffer->Mid ||
			    mid_entry->midState != MID_REQUEST_SUBMITTED ||
			    mid_entry->command != smb_buffer->Command) {
				mid_entry = NULL;
				continue;
			}

			if (length == 0 &&
			    check2ndT2(smb_buffer, server->maxBuf) > 0) {
				/* We have a multipart transact2 resp */
				isMultiRsp = true;
				if (mid_entry->resp_buf) {
					/* merge response - fix up 1st*/
					length = coalesce_t2(smb_buffer,
							mid_entry->resp_buf);
					if (length > 0) {
						length = 0;
						mid_entry->multiRsp = true;
						break;
					} else {
						/* all parts received or
						 * packet is malformed
						 */
						mid_entry->multiEnd = true;
						goto multi_t2_fnd;
					}
				} else {
					if (!isLargeBuf) {
						/*
						 * FIXME: switch to already
						 *        allocated largebuf?
						 */
						cERROR(1, "1st trans2 resp "
							  "needs bigbuf");
					} else {
						/* Have first buffer */
						mid_entry->resp_buf =
							 smb_buffer;
						mid_entry->largeBuf = true;
						bigbuf = NULL;
					}
				}
				break;
			}
			mid_entry->resp_buf = smb_buffer;
			mid_entry->largeBuf = isLargeBuf;
multi_t2_fnd:
			if (length == 0)
				mid_entry->midState = MID_RESPONSE_RECEIVED;
			else
				mid_entry->midState = MID_RESPONSE_MALFORMED;
#ifdef CONFIG_CIFS_STATS2
			mid_entry->when_received = jiffies;
#endif
			list_del_init(&mid_entry->qhead);
			break;
		}
		spin_unlock(&GlobalMid_Lock);

		if (mid_entry != NULL) {
			mid_entry->callback(mid_entry);
			/* Was previous buf put in mpx struct for multi-rsp? */
			if (!isMultiRsp) {
				/* smb buffer will be freed by user thread */
				if (isLargeBuf)
					bigbuf = NULL;
				else
					smallbuf = NULL;
			}
		} else if (length != 0) {
			/* response sanity checks failed */
			continue;
		} else if (!is_valid_oplock_break(smb_buffer, server) &&
			   !isMultiRsp) {
			cERROR(1, "No task to wake, unknown frame received! "
				   "NumMids %d", atomic_read(&midCount));
			cifs_dump_mem("Received Data is: ", (char *)smb_buffer,
				      sizeof(struct smb_hdr));
#ifdef CONFIG_CIFS_DEBUG2
			cifs_dump_detail(smb_buffer);
			cifs_dump_mids(server);
#endif /* CIFS_DEBUG2 */

		}
	} /* end while !EXITING */

	/* take it off the list, if it's not already */
	spin_lock(&cifs_tcp_ses_lock);
	list_del_init(&server->tcp_ses_list);
	spin_unlock(&cifs_tcp_ses_lock);

	spin_lock(&GlobalMid_Lock);
	server->tcpStatus = CifsExiting;
	spin_unlock(&GlobalMid_Lock);
	wake_up_all(&server->response_q);

	/* check if we have blocked requests that need to free */
	/* Note that cifs_max_pending is normally 50, but
	can be set at module install time to as little as two */
	spin_lock(&GlobalMid_Lock);
	if (atomic_read(&server->inFlight) >= cifs_max_pending)
		atomic_set(&server->inFlight, cifs_max_pending - 1);
	/* We do not want to set the max_pending too low or we
	could end up with the counter going negative */
	spin_unlock(&GlobalMid_Lock);
	/* Although there should not be any requests blocked on
	this queue it can not hurt to be paranoid and try to wake up requests
	that may haven been blocked when more than 50 at time were on the wire
	to the same server - they now will see the session is in exit state
	and get out of SendReceive.  */
	wake_up_all(&server->request_q);
	/* give those requests time to exit */
	msleep(125);

	if (server->ssocket) {
		sock_release(csocket);
		server->ssocket = NULL;
	}
	/* buffer usually freed in free_mid - need to free it here on exit */
	cifs_buf_release(bigbuf);
	if (smallbuf) /* no sense logging a debug message if NULL */
		cifs_small_buf_release(smallbuf);

	if (!list_empty(&server->pending_mid_q)) {
		struct list_head dispose_list;

		INIT_LIST_HEAD(&dispose_list);
		spin_lock(&GlobalMid_Lock);
		list_for_each_safe(tmp, tmp2, &server->pending_mid_q) {
			mid_entry = list_entry(tmp, struct mid_q_entry, qhead);
			cFYI(1, "Clearing mid 0x%x", mid_entry->mid);
			mid_entry->midState = MID_SHUTDOWN;
			list_move(&mid_entry->qhead, &dispose_list);
		}
		spin_unlock(&GlobalMid_Lock);

		/* now walk dispose list and issue callbacks */
		list_for_each_safe(tmp, tmp2, &dispose_list) {
			mid_entry = list_entry(tmp, struct mid_q_entry, qhead);
			cFYI(1, "Callback mid 0x%x", mid_entry->mid);
			list_del_init(&mid_entry->qhead);
			mid_entry->callback(mid_entry);
		}
		/* 1/8th of sec is more than enough time for them to exit */
		msleep(125);
	}

	if (!list_empty(&server->pending_mid_q)) {
		/* mpx threads have not exited yet give them
		at least the smb send timeout time for long ops */
		/* due to delays on oplock break requests, we need
		to wait at least 45 seconds before giving up
		on a request getting a response and going ahead
		and killing cifsd */
		cFYI(1, "Wait for exit from demultiplex thread");
		msleep(46000);
		/* if threads still have not exited they are probably never
		coming home not much else we can do but free the memory */
	}

	kfree(server->hostname);
	task_to_wake = xchg(&server->tsk, NULL);
	kfree(server);

	length = atomic_dec_return(&tcpSesAllocCount);
	if (length  > 0)
		mempool_resize(cifs_req_poolp, length + cifs_min_rcv,
				GFP_KERNEL);

	/* if server->tsk was NULL then wait for a signal before exiting */
	if (!task_to_wake) {
		set_current_state(TASK_INTERRUPTIBLE);
		while (!signal_pending(current)) {
			schedule();
			set_current_state(TASK_INTERRUPTIBLE);
		}
		set_current_state(TASK_RUNNING);
	}

	module_put_and_exit(0);
}

/* extract the host portion of the UNC string */
static char *
extract_hostname(const char *unc)
{
	const char *src;
	char *dst, *delim;
	unsigned int len;

	/* skip double chars at beginning of string */
	/* BB: check validity of these bytes? */
	src = unc + 2;

	/* delimiter between hostname and sharename is always '\\' now */
	delim = strchr(src, '\\');
	if (!delim)
		return ERR_PTR(-EINVAL);

	len = delim - src;
	dst = kmalloc((len + 1), GFP_KERNEL);
	if (dst == NULL)
		return ERR_PTR(-ENOMEM);

	memcpy(dst, src, len);
	dst[len] = '\0';

	return dst;
}

static int
cifs_parse_mount_options(const char *mountdata, const char *devname,
			 struct smb_vol *vol)
{
	char *value, *data, *end;
	char *mountdata_copy = NULL, *options;
	unsigned int  temp_len, i, j;
	char separator[2];
	short int override_uid = -1;
	short int override_gid = -1;
	bool uid_specified = false;
	bool gid_specified = false;
	char *nodename = utsname()->nodename;

	separator[0] = ',';
	separator[1] = 0;

	/*
	 * does not have to be perfect mapping since field is
	 * informational, only used for servers that do not support
	 * port 445 and it can be overridden at mount time
	 */
	memset(vol->source_rfc1001_name, 0x20, RFC1001_NAME_LEN);
	for (i = 0; i < strnlen(nodename, RFC1001_NAME_LEN); i++)
		vol->source_rfc1001_name[i] = toupper(nodename[i]);

	vol->source_rfc1001_name[RFC1001_NAME_LEN] = 0;
	/* null target name indicates to use *SMBSERVR default called name
	   if we end up sending RFC1001 session initialize */
	vol->target_rfc1001_name[0] = 0;
	vol->cred_uid = current_uid();
	vol->linux_uid = current_uid();
	vol->linux_gid = current_gid();

	/* default to only allowing write access to owner of the mount */
	vol->dir_mode = vol->file_mode = S_IRUGO | S_IXUGO | S_IWUSR;

	/* vol->retry default is 0 (i.e. "soft" limited retry not hard retry) */
	/* default is always to request posix paths. */
	vol->posix_paths = 1;
	/* default to using server inode numbers where available */
	vol->server_ino = 1;

	vol->actimeo = CIFS_DEF_ACTIMEO;

	if (!mountdata)
		goto cifs_parse_mount_err;

	mountdata_copy = kstrndup(mountdata, PAGE_SIZE, GFP_KERNEL);
	if (!mountdata_copy)
		goto cifs_parse_mount_err;

	options = mountdata_copy;
	end = options + strlen(options);
	if (strncmp(options, "sep=", 4) == 0) {
		if (options[4] != 0) {
			separator[0] = options[4];
			options += 5;
		} else {
			cFYI(1, "Null separator not allowed");
		}
	}

	while ((data = strsep(&options, separator)) != NULL) {
		if (!*data)
			continue;
		if ((value = strchr(data, '=')) != NULL)
			*value++ = '\0';

		/* Have to parse this before we parse for "user" */
		if (strnicmp(data, "user_xattr", 10) == 0) {
			vol->no_xattr = 0;
		} else if (strnicmp(data, "nouser_xattr", 12) == 0) {
			vol->no_xattr = 1;
		} else if (strnicmp(data, "user", 4) == 0) {
			if (!value) {
				printk(KERN_WARNING
				       "CIFS: invalid or missing username\n");
				goto cifs_parse_mount_err;
			} else if (!*value) {
				/* null user, ie anonymous, authentication */
				vol->nullauth = 1;
			}
			if (strnlen(value, MAX_USERNAME_SIZE) <
						MAX_USERNAME_SIZE) {
				vol->username = kstrdup(value, GFP_KERNEL);
				if (!vol->username) {
					printk(KERN_WARNING "CIFS: no memory "
							    "for username\n");
					goto cifs_parse_mount_err;
				}
			} else {
				printk(KERN_WARNING "CIFS: username too long\n");
				goto cifs_parse_mount_err;
			}
		} else if (strnicmp(data, "pass", 4) == 0) {
			if (!value) {
				vol->password = NULL;
				continue;
			} else if (value[0] == 0) {
				/* check if string begins with double comma
				   since that would mean the password really
				   does start with a comma, and would not
				   indicate an empty string */
				if (value[1] != separator[0]) {
					vol->password = NULL;
					continue;
				}
			}
			temp_len = strlen(value);
			/* removed password length check, NTLM passwords
				can be arbitrarily long */

			/* if comma in password, the string will be
			prematurely null terminated.  Commas in password are
			specified across the cifs mount interface by a double
			comma ie ,, and a comma used as in other cases ie ','
			as a parameter delimiter/separator is single and due
			to the strsep above is temporarily zeroed. */

			/* NB: password legally can have multiple commas and
			the only illegal character in a password is null */

			if ((value[temp_len] == 0) &&
			    (value + temp_len < end) &&
			    (value[temp_len+1] == separator[0])) {
				/* reinsert comma */
				value[temp_len] = separator[0];
				temp_len += 2;  /* move after second comma */
				while (value[temp_len] != 0)  {
					if (value[temp_len] == separator[0]) {
						if (value[temp_len+1] ==
						     separator[0]) {
						/* skip second comma */
							temp_len++;
						} else {
						/* single comma indicating start
							 of next parm */
							break;
						}
					}
					temp_len++;
				}
				if (value[temp_len] == 0) {
					options = NULL;
				} else {
					value[temp_len] = 0;
					/* point option to start of next parm */
					options = value + temp_len + 1;
				}
				/* go from value to value + temp_len condensing
				double commas to singles. Note that this ends up
				allocating a few bytes too many, which is ok */
				vol->password = kzalloc(temp_len, GFP_KERNEL);
				if (vol->password == NULL) {
					printk(KERN_WARNING "CIFS: no memory "
							    "for password\n");
					goto cifs_parse_mount_err;
				}
				for (i = 0, j = 0; i < temp_len; i++, j++) {
					vol->password[j] = value[i];
					if (value[i] == separator[0]
						&& value[i+1] == separator[0]) {
						/* skip second comma */
						i++;
					}
				}
				vol->password[j] = 0;
			} else {
				vol->password = kzalloc(temp_len+1, GFP_KERNEL);
				if (vol->password == NULL) {
					printk(KERN_WARNING "CIFS: no memory "
							    "for password\n");
					goto cifs_parse_mount_err;
				}
				strcpy(vol->password, value);
			}
		} else if (!strnicmp(data, "ip", 2) ||
			   !strnicmp(data, "addr", 4)) {
			if (!value || !*value) {
				vol->UNCip = NULL;
			} else if (strnlen(value, INET6_ADDRSTRLEN) <
							INET6_ADDRSTRLEN) {
				vol->UNCip = kstrdup(value, GFP_KERNEL);
				if (!vol->UNCip) {
					printk(KERN_WARNING "CIFS: no memory "
							    "for UNC IP\n");
					goto cifs_parse_mount_err;
				}
			} else {
				printk(KERN_WARNING "CIFS: ip address "
						    "too long\n");
				goto cifs_parse_mount_err;
			}
		} else if (strnicmp(data, "sec", 3) == 0) {
			if (!value || !*value) {
				cERROR(1, "no security value specified");
				continue;
			} else if (strnicmp(value, "krb5i", 5) == 0) {
				vol->secFlg |= CIFSSEC_MAY_KRB5 |
					CIFSSEC_MUST_SIGN;
			} else if (strnicmp(value, "krb5p", 5) == 0) {
				/* vol->secFlg |= CIFSSEC_MUST_SEAL |
					CIFSSEC_MAY_KRB5; */
				cERROR(1, "Krb5 cifs privacy not supported");
				goto cifs_parse_mount_err;
			} else if (strnicmp(value, "krb5", 4) == 0) {
				vol->secFlg |= CIFSSEC_MAY_KRB5;
			} else if (strnicmp(value, "ntlmsspi", 8) == 0) {
				vol->secFlg |= CIFSSEC_MAY_NTLMSSP |
					CIFSSEC_MUST_SIGN;
			} else if (strnicmp(value, "ntlmssp", 7) == 0) {
				vol->secFlg |= CIFSSEC_MAY_NTLMSSP;
			} else if (strnicmp(value, "ntlmv2i", 7) == 0) {
				vol->secFlg |= CIFSSEC_MAY_NTLMV2 |
					CIFSSEC_MUST_SIGN;
			} else if (strnicmp(value, "ntlmv2", 6) == 0) {
				vol->secFlg |= CIFSSEC_MAY_NTLMV2;
			} else if (strnicmp(value, "ntlmi", 5) == 0) {
				vol->secFlg |= CIFSSEC_MAY_NTLM |
					CIFSSEC_MUST_SIGN;
			} else if (strnicmp(value, "ntlm", 4) == 0) {
				/* ntlm is default so can be turned off too */
				vol->secFlg |= CIFSSEC_MAY_NTLM;
			} else if (strnicmp(value, "nontlm", 6) == 0) {
				/* BB is there a better way to do this? */
				vol->secFlg |= CIFSSEC_MAY_NTLMV2;
#ifdef CONFIG_CIFS_WEAK_PW_HASH
			} else if (strnicmp(value, "lanman", 6) == 0) {
				vol->secFlg |= CIFSSEC_MAY_LANMAN;
#endif
			} else if (strnicmp(value, "none", 4) == 0) {
				vol->nullauth = 1;
			} else {
				cERROR(1, "bad security option: %s", value);
				goto cifs_parse_mount_err;
			}
		} else if (strnicmp(data, "vers", 3) == 0) {
			if (!value || !*value) {
				cERROR(1, "no protocol version specified"
					  " after vers= mount option");
			} else if ((strnicmp(value, "cifs", 4) == 0) ||
				   (strnicmp(value, "1", 1) == 0)) {
				/* this is the default */
				continue;
			}
		} else if ((strnicmp(data, "unc", 3) == 0)
			   || (strnicmp(data, "target", 6) == 0)
			   || (strnicmp(data, "path", 4) == 0)) {
			if (!value || !*value) {
				printk(KERN_WARNING "CIFS: invalid path to "
						    "network resource\n");
				goto cifs_parse_mount_err;
			}
			if ((temp_len = strnlen(value, 300)) < 300) {
				vol->UNC = kmalloc(temp_len+1, GFP_KERNEL);
				if (vol->UNC == NULL)
					goto cifs_parse_mount_err;
				strcpy(vol->UNC, value);
				if (strncmp(vol->UNC, "//", 2) == 0) {
					vol->UNC[0] = '\\';
					vol->UNC[1] = '\\';
				} else if (strncmp(vol->UNC, "\\\\", 2) != 0) {
					printk(KERN_WARNING
					       "CIFS: UNC Path does not begin "
					       "with // or \\\\ \n");
					goto cifs_parse_mount_err;
				}
			} else {
				printk(KERN_WARNING "CIFS: UNC name too long\n");
				goto cifs_parse_mount_err;
			}
		} else if ((strnicmp(data, "domain", 3) == 0)
			   || (strnicmp(data, "workgroup", 5) == 0)) {
			if (!value || !*value) {
				printk(KERN_WARNING "CIFS: invalid domain name\n");
				goto cifs_parse_mount_err;
			}
			/* BB are there cases in which a comma can be valid in
			a domain name and need special handling? */
			if (strnlen(value, 256) < 256) {
				vol->domainname = kstrdup(value, GFP_KERNEL);
				if (!vol->domainname) {
					printk(KERN_WARNING "CIFS: no memory "
							    "for domainname\n");
					goto cifs_parse_mount_err;
				}
				cFYI(1, "Domain name set");
			} else {
				printk(KERN_WARNING "CIFS: domain name too "
						    "long\n");
				goto cifs_parse_mount_err;
			}
		} else if (strnicmp(data, "srcaddr", 7) == 0) {
			vol->srcaddr.ss_family = AF_UNSPEC;

			if (!value || !*value) {
				printk(KERN_WARNING "CIFS: srcaddr value"
				       " not specified.\n");
				goto cifs_parse_mount_err;
			}
			i = cifs_convert_address((struct sockaddr *)&vol->srcaddr,
						 value, strlen(value));
			if (i == 0) {
				printk(KERN_WARNING "CIFS:  Could not parse"
				       " srcaddr: %s\n",
				       value);
				goto cifs_parse_mount_err;
			}
		} else if (strnicmp(data, "prefixpath", 10) == 0) {
			if (!value || !*value) {
				printk(KERN_WARNING
					"CIFS: invalid path prefix\n");
				goto cifs_parse_mount_err;
			}
			if ((temp_len = strnlen(value, 1024)) < 1024) {
				if (value[0] != '/')
					temp_len++;  /* missing leading slash */
				vol->prepath = kmalloc(temp_len+1, GFP_KERNEL);
				if (vol->prepath == NULL)
					goto cifs_parse_mount_err;
				if (value[0] != '/') {
					vol->prepath[0] = '/';
					strcpy(vol->prepath+1, value);
				} else
					strcpy(vol->prepath, value);
				cFYI(1, "prefix path %s", vol->prepath);
			} else {
				printk(KERN_WARNING "CIFS: prefix too long\n");
				goto cifs_parse_mount_err;
			}
		} else if (strnicmp(data, "iocharset", 9) == 0) {
			if (!value || !*value) {
				printk(KERN_WARNING "CIFS: invalid iocharset "
						    "specified\n");
				goto cifs_parse_mount_err;
			}
			if (strnlen(value, 65) < 65) {
				if (strnicmp(value, "default", 7)) {
					vol->iocharset = kstrdup(value,
								 GFP_KERNEL);

					if (!vol->iocharset) {
						printk(KERN_WARNING "CIFS: no "
								   "memory for"
								   "charset\n");
						goto cifs_parse_mount_err;
					}
				}
				/* if iocharset not set then load_nls_default
				   is used by caller */
				cFYI(1, "iocharset set to %s", value);
			} else {
				printk(KERN_WARNING "CIFS: iocharset name "
						    "too long.\n");
				goto cifs_parse_mount_err;
			}
		} else if (!strnicmp(data, "uid", 3) && value && *value) {
			vol->linux_uid = simple_strtoul(value, &value, 0);
			uid_specified = true;
		} else if (!strnicmp(data, "cruid", 5) && value && *value) {
			vol->cred_uid = simple_strtoul(value, &value, 0);
		} else if (!strnicmp(data, "forceuid", 8)) {
			override_uid = 1;
		} else if (!strnicmp(data, "noforceuid", 10)) {
			override_uid = 0;
		} else if (!strnicmp(data, "gid", 3) && value && *value) {
			vol->linux_gid = simple_strtoul(value, &value, 0);
			gid_specified = true;
		} else if (!strnicmp(data, "forcegid", 8)) {
			override_gid = 1;
		} else if (!strnicmp(data, "noforcegid", 10)) {
			override_gid = 0;
		} else if (strnicmp(data, "file_mode", 4) == 0) {
			if (value && *value) {
				vol->file_mode =
					simple_strtoul(value, &value, 0);
			}
		} else if (strnicmp(data, "dir_mode", 4) == 0) {
			if (value && *value) {
				vol->dir_mode =
					simple_strtoul(value, &value, 0);
			}
		} else if (strnicmp(data, "dirmode", 4) == 0) {
			if (value && *value) {
				vol->dir_mode =
					simple_strtoul(value, &value, 0);
			}
		} else if (strnicmp(data, "port", 4) == 0) {
			if (value && *value) {
				vol->port =
					simple_strtoul(value, &value, 0);
			}
		} else if (strnicmp(data, "rsize", 5) == 0) {
			if (value && *value) {
				vol->rsize =
					simple_strtoul(value, &value, 0);
			}
		} else if (strnicmp(data, "wsize", 5) == 0) {
			if (value && *value) {
				vol->wsize =
					simple_strtoul(value, &value, 0);
			}
		} else if (strnicmp(data, "sockopt", 5) == 0) {
			if (!value || !*value) {
				cERROR(1, "no socket option specified");
				continue;
			} else if (strnicmp(value, "TCP_NODELAY", 11) == 0) {
				vol->sockopt_tcp_nodelay = 1;
			}
		} else if (strnicmp(data, "netbiosname", 4) == 0) {
			if (!value || !*value || (*value == ' ')) {
				cFYI(1, "invalid (empty) netbiosname");
			} else {
				memset(vol->source_rfc1001_name, 0x20,
					RFC1001_NAME_LEN);
				/*
				 * FIXME: are there cases in which a comma can
				 * be valid in workstation netbios name (and
				 * need special handling)?
				 */
				for (i = 0; i < RFC1001_NAME_LEN; i++) {
					/* don't ucase netbiosname for user */
					if (value[i] == 0)
						break;
					vol->source_rfc1001_name[i] = value[i];
				}
				/* The string has 16th byte zero still from
				set at top of the function  */
				if (i == RFC1001_NAME_LEN && value[i] != 0)
					printk(KERN_WARNING "CIFS: netbiosname"
						" longer than 15 truncated.\n");
			}
		} else if (strnicmp(data, "servern", 7) == 0) {
			/* servernetbiosname specified override *SMBSERVER */
			if (!value || !*value || (*value == ' ')) {
				cFYI(1, "empty server netbiosname specified");
			} else {
				/* last byte, type, is 0x20 for servr type */
				memset(vol->target_rfc1001_name, 0x20,
					RFC1001_NAME_LEN_WITH_NULL);

				for (i = 0; i < 15; i++) {
				/* BB are there cases in which a comma can be
				   valid in this workstation netbios name
				   (and need special handling)? */

				/* user or mount helper must uppercase
				   the netbiosname */
					if (value[i] == 0)
						break;
					else
						vol->target_rfc1001_name[i] =
								value[i];
				}
				/* The string has 16th byte zero still from
				   set at top of the function  */
				if (i == RFC1001_NAME_LEN && value[i] != 0)
					printk(KERN_WARNING "CIFS: server net"
					"biosname longer than 15 truncated.\n");
			}
		} else if (strnicmp(data, "actimeo", 7) == 0) {
			if (value && *value) {
				vol->actimeo = HZ * simple_strtoul(value,
								   &value, 0);
				if (vol->actimeo > CIFS_MAX_ACTIMEO) {
					cERROR(1, "CIFS: attribute cache"
							"timeout too large");
					goto cifs_parse_mount_err;
				}
			}
		} else if (strnicmp(data, "credentials", 4) == 0) {
			/* ignore */
		} else if (strnicmp(data, "version", 3) == 0) {
			/* ignore */
		} else if (strnicmp(data, "guest", 5) == 0) {
			/* ignore */
		} else if (strnicmp(data, "rw", 2) == 0 && strlen(data) == 2) {
			/* ignore */
		} else if (strnicmp(data, "ro", 2) == 0) {
			/* ignore */
		} else if (strnicmp(data, "noblocksend", 11) == 0) {
			vol->noblocksnd = 1;
		} else if (strnicmp(data, "noautotune", 10) == 0) {
			vol->noautotune = 1;
		} else if ((strnicmp(data, "suid", 4) == 0) ||
				   (strnicmp(data, "nosuid", 6) == 0) ||
				   (strnicmp(data, "exec", 4) == 0) ||
				   (strnicmp(data, "noexec", 6) == 0) ||
				   (strnicmp(data, "nodev", 5) == 0) ||
				   (strnicmp(data, "noauto", 6) == 0) ||
				   (strnicmp(data, "dev", 3) == 0)) {
			/*  The mount tool or mount.cifs helper (if present)
			    uses these opts to set flags, and the flags are read
			    by the kernel vfs layer before we get here (ie
			    before read super) so there is no point trying to
			    parse these options again and set anything and it
			    is ok to just ignore them */
			continue;
		} else if (strnicmp(data, "hard", 4) == 0) {
			vol->retry = 1;
		} else if (strnicmp(data, "soft", 4) == 0) {
			vol->retry = 0;
		} else if (strnicmp(data, "perm", 4) == 0) {
			vol->noperm = 0;
		} else if (strnicmp(data, "noperm", 6) == 0) {
			vol->noperm = 1;
		} else if (strnicmp(data, "mapchars", 8) == 0) {
			vol->remap = 1;
		} else if (strnicmp(data, "nomapchars", 10) == 0) {
			vol->remap = 0;
		} else if (strnicmp(data, "sfu", 3) == 0) {
			vol->sfu_emul = 1;
		} else if (strnicmp(data, "nosfu", 5) == 0) {
			vol->sfu_emul = 0;
		} else if (strnicmp(data, "nodfs", 5) == 0) {
			vol->nodfs = 1;
		} else if (strnicmp(data, "posixpaths", 10) == 0) {
			vol->posix_paths = 1;
		} else if (strnicmp(data, "noposixpaths", 12) == 0) {
			vol->posix_paths = 0;
		} else if (strnicmp(data, "nounix", 6) == 0) {
			vol->no_linux_ext = 1;
		} else if (strnicmp(data, "nolinux", 7) == 0) {
			vol->no_linux_ext = 1;
		} else if ((strnicmp(data, "nocase", 6) == 0) ||
			   (strnicmp(data, "ignorecase", 10)  == 0)) {
			vol->nocase = 1;
		} else if (strnicmp(data, "mand", 4) == 0) {
			/* ignore */
		} else if (strnicmp(data, "nomand", 6) == 0) {
			/* ignore */
		} else if (strnicmp(data, "_netdev", 7) == 0) {
			/* ignore */
		} else if (strnicmp(data, "brl", 3) == 0) {
			vol->nobrl =  0;
		} else if ((strnicmp(data, "nobrl", 5) == 0) ||
			   (strnicmp(data, "nolock", 6) == 0)) {
			vol->nobrl =  1;
			/* turn off mandatory locking in mode
			if remote locking is turned off since the
			local vfs will do advisory */
			if (vol->file_mode ==
				(S_IALLUGO & ~(S_ISUID | S_IXGRP)))
				vol->file_mode = S_IALLUGO;
		} else if (strnicmp(data, "forcemandatorylock", 9) == 0) {
			/* will take the shorter form "forcemand" as well */
			/* This mount option will force use of mandatory
			  (DOS/Windows style) byte range locks, instead of
			  using posix advisory byte range locks, even if the
			  Unix extensions are available and posix locks would
			  be supported otherwise. If Unix extensions are not
			  negotiated this has no effect since mandatory locks
			  would be used (mandatory locks is all that those
			  those servers support) */
			vol->mand_lock = 1;
		} else if (strnicmp(data, "setuids", 7) == 0) {
			vol->setuids = 1;
		} else if (strnicmp(data, "nosetuids", 9) == 0) {
			vol->setuids = 0;
		} else if (strnicmp(data, "dynperm", 7) == 0) {
			vol->dynperm = true;
		} else if (strnicmp(data, "nodynperm", 9) == 0) {
			vol->dynperm = false;
		} else if (strnicmp(data, "nohard", 6) == 0) {
			vol->retry = 0;
		} else if (strnicmp(data, "nosoft", 6) == 0) {
			vol->retry = 1;
		} else if (strnicmp(data, "nointr", 6) == 0) {
			vol->intr = 0;
		} else if (strnicmp(data, "intr", 4) == 0) {
			vol->intr = 1;
		} else if (strnicmp(data, "nostrictsync", 12) == 0) {
			vol->nostrictsync = 1;
		} else if (strnicmp(data, "strictsync", 10) == 0) {
			vol->nostrictsync = 0;
		} else if (strnicmp(data, "serverino", 7) == 0) {
			vol->server_ino = 1;
		} else if (strnicmp(data, "noserverino", 9) == 0) {
			vol->server_ino = 0;
		} else if (strnicmp(data, "rwpidforward", 12) == 0) {
			vol->rwpidforward = 1;
		} else if (strnicmp(data, "cifsacl", 7) == 0) {
			vol->cifs_acl = 1;
		} else if (strnicmp(data, "nocifsacl", 9) == 0) {
			vol->cifs_acl = 0;
		} else if (strnicmp(data, "acl", 3) == 0) {
			vol->no_psx_acl = 0;
		} else if (strnicmp(data, "noacl", 5) == 0) {
			vol->no_psx_acl = 1;
		} else if (strnicmp(data, "locallease", 6) == 0) {
			vol->local_lease = 1;
		} else if (strnicmp(data, "sign", 4) == 0) {
			vol->secFlg |= CIFSSEC_MUST_SIGN;
		} else if (strnicmp(data, "seal", 4) == 0) {
			/* we do not do the following in secFlags because seal
			   is a per tree connection (mount) not a per socket
			   or per-smb connection option in the protocol */
			/* vol->secFlg |= CIFSSEC_MUST_SEAL; */
			vol->seal = 1;
		} else if (strnicmp(data, "direct", 6) == 0) {
			vol->direct_io = 1;
		} else if (strnicmp(data, "forcedirectio", 13) == 0) {
			vol->direct_io = 1;
		} else if (strnicmp(data, "strictcache", 11) == 0) {
			vol->strict_io = 1;
		} else if (strnicmp(data, "noac", 4) == 0) {
			printk(KERN_WARNING "CIFS: Mount option noac not "
				"supported. Instead set "
				"/proc/fs/cifs/LookupCacheEnabled to 0\n");
		} else if (strnicmp(data, "fsc", 3) == 0) {
#ifndef CONFIG_CIFS_FSCACHE
			cERROR(1, "FS-Cache support needs CONFIG_CIFS_FSCACHE "
				  "kernel config option set");
			goto cifs_parse_mount_err;
#endif
			vol->fsc = true;
		} else if (strnicmp(data, "mfsymlinks", 10) == 0) {
			vol->mfsymlinks = true;
		} else if (strnicmp(data, "multiuser", 8) == 0) {
			vol->multiuser = true;
		} else
			printk(KERN_WARNING "CIFS: Unknown mount option %s\n",
						data);
	}
	if (vol->UNC == NULL) {
		if (devname == NULL) {
			printk(KERN_WARNING "CIFS: Missing UNC name for mount "
						"target\n");
			goto cifs_parse_mount_err;
		}
		if ((temp_len = strnlen(devname, 300)) < 300) {
			vol->UNC = kmalloc(temp_len+1, GFP_KERNEL);
			if (vol->UNC == NULL)
				goto cifs_parse_mount_err;
			strcpy(vol->UNC, devname);
			if (strncmp(vol->UNC, "//", 2) == 0) {
				vol->UNC[0] = '\\';
				vol->UNC[1] = '\\';
			} else if (strncmp(vol->UNC, "\\\\", 2) != 0) {
				printk(KERN_WARNING "CIFS: UNC Path does not "
						    "begin with // or \\\\ \n");
				goto cifs_parse_mount_err;
			}
			value = strpbrk(vol->UNC+2, "/\\");
			if (value)
				*value = '\\';
		} else {
			printk(KERN_WARNING "CIFS: UNC name too long\n");
			goto cifs_parse_mount_err;
		}
	}

	if (vol->multiuser && !(vol->secFlg & CIFSSEC_MAY_KRB5)) {
		cERROR(1, "Multiuser mounts currently require krb5 "
			  "authentication!");
		goto cifs_parse_mount_err;
	}

	if (vol->UNCip == NULL)
		vol->UNCip = &vol->UNC[2];

	if (uid_specified)
		vol->override_uid = override_uid;
	else if (override_uid == 1)
		printk(KERN_NOTICE "CIFS: ignoring forceuid mount option "
				   "specified with no uid= option.\n");

	if (gid_specified)
		vol->override_gid = override_gid;
	else if (override_gid == 1)
		printk(KERN_NOTICE "CIFS: ignoring forcegid mount option "
				   "specified with no gid= option.\n");

	kfree(mountdata_copy);
	return 0;

cifs_parse_mount_err:
	kfree(mountdata_copy);
	return 1;
}

/** Returns true if srcaddr isn't specified and rhs isn't
 * specified, or if srcaddr is specified and
 * matches the IP address of the rhs argument.
 */
static bool
srcip_matches(struct sockaddr *srcaddr, struct sockaddr *rhs)
{
	switch (srcaddr->sa_family) {
	case AF_UNSPEC:
		return (rhs->sa_family == AF_UNSPEC);
	case AF_INET: {
		struct sockaddr_in *saddr4 = (struct sockaddr_in *)srcaddr;
		struct sockaddr_in *vaddr4 = (struct sockaddr_in *)rhs;
		return (saddr4->sin_addr.s_addr == vaddr4->sin_addr.s_addr);
	}
	case AF_INET6: {
		struct sockaddr_in6 *saddr6 = (struct sockaddr_in6 *)srcaddr;
		struct sockaddr_in6 *vaddr6 = (struct sockaddr_in6 *)&rhs;
		return ipv6_addr_equal(&saddr6->sin6_addr, &vaddr6->sin6_addr);
	}
	default:
		WARN_ON(1);
		return false; /* don't expect to be here */
	}
}

/*
 * If no port is specified in addr structure, we try to match with 445 port
 * and if it fails - with 139 ports. It should be called only if address
 * families of server and addr are equal.
 */
static bool
match_port(struct TCP_Server_Info *server, struct sockaddr *addr)
{
	__be16 port, *sport;

	switch (addr->sa_family) {
	case AF_INET:
		sport = &((struct sockaddr_in *) &server->dstaddr)->sin_port;
		port = ((struct sockaddr_in *) addr)->sin_port;
		break;
	case AF_INET6:
		sport = &((struct sockaddr_in6 *) &server->dstaddr)->sin6_port;
		port = ((struct sockaddr_in6 *) addr)->sin6_port;
		break;
	default:
		WARN_ON(1);
		return false;
	}

	if (!port) {
		port = htons(CIFS_PORT);
		if (port == *sport)
			return true;

		port = htons(RFC1001_PORT);
	}

	return port == *sport;
}

static bool
match_address(struct TCP_Server_Info *server, struct sockaddr *addr,
	      struct sockaddr *srcaddr)
{
	switch (addr->sa_family) {
	case AF_INET: {
		struct sockaddr_in *addr4 = (struct sockaddr_in *)addr;
		struct sockaddr_in *srv_addr4 =
					(struct sockaddr_in *)&server->dstaddr;

		if (addr4->sin_addr.s_addr != srv_addr4->sin_addr.s_addr)
			return false;
		break;
	}
	case AF_INET6: {
		struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)addr;
		struct sockaddr_in6 *srv_addr6 =
					(struct sockaddr_in6 *)&server->dstaddr;

		if (!ipv6_addr_equal(&addr6->sin6_addr,
				     &srv_addr6->sin6_addr))
			return false;
		if (addr6->sin6_scope_id != srv_addr6->sin6_scope_id)
			return false;
		break;
	}
	default:
		WARN_ON(1);
		return false; /* don't expect to be here */
	}

	if (!srcip_matches(srcaddr, (struct sockaddr *)&server->srcaddr))
		return false;

	return true;
}

static bool
match_security(struct TCP_Server_Info *server, struct smb_vol *vol)
{
	unsigned int secFlags;

	if (vol->secFlg & (~(CIFSSEC_MUST_SIGN | CIFSSEC_MUST_SEAL)))
		secFlags = vol->secFlg;
	else
		secFlags = global_secflags | vol->secFlg;

	switch (server->secType) {
	case LANMAN:
		if (!(secFlags & (CIFSSEC_MAY_LANMAN|CIFSSEC_MAY_PLNTXT)))
			return false;
		break;
	case NTLMv2:
		if (!(secFlags & CIFSSEC_MAY_NTLMV2))
			return false;
		break;
	case NTLM:
		if (!(secFlags & CIFSSEC_MAY_NTLM))
			return false;
		break;
	case Kerberos:
		if (!(secFlags & CIFSSEC_MAY_KRB5))
			return false;
		break;
	case RawNTLMSSP:
		if (!(secFlags & CIFSSEC_MAY_NTLMSSP))
			return false;
		break;
	default:
		/* shouldn't happen */
		return false;
	}

	/* now check if signing mode is acceptable */
	if ((secFlags & CIFSSEC_MAY_SIGN) == 0 &&
	    (server->sec_mode & SECMODE_SIGN_REQUIRED))
			return false;
	else if (((secFlags & CIFSSEC_MUST_SIGN) == CIFSSEC_MUST_SIGN) &&
		 (server->sec_mode &
		  (SECMODE_SIGN_ENABLED|SECMODE_SIGN_REQUIRED)) == 0)
			return false;

	return true;
}

static int match_server(struct TCP_Server_Info *server, struct sockaddr *addr,
			 struct smb_vol *vol)
{
	if (!net_eq(cifs_net_ns(server), current->nsproxy->net_ns))
		return 0;

	if (!match_address(server, addr,
			   (struct sockaddr *)&vol->srcaddr))
		return 0;

	if (!match_port(server, addr))
		return 0;

	if (!match_security(server, vol))
		return 0;

	return 1;
}

static struct TCP_Server_Info *
cifs_find_tcp_session(struct sockaddr *addr, struct smb_vol *vol)
{
	struct TCP_Server_Info *server;

	spin_lock(&cifs_tcp_ses_lock);
	list_for_each_entry(server, &cifs_tcp_ses_list, tcp_ses_list) {
		if (!match_server(server, addr, vol))
			continue;

		++server->srv_count;
		spin_unlock(&cifs_tcp_ses_lock);
		cFYI(1, "Existing tcp session with server found");
		return server;
	}
	spin_unlock(&cifs_tcp_ses_lock);
	return NULL;
}

static void
cifs_put_tcp_session(struct TCP_Server_Info *server)
{
	struct task_struct *task;

	spin_lock(&cifs_tcp_ses_lock);
	if (--server->srv_count > 0) {
		spin_unlock(&cifs_tcp_ses_lock);
		return;
	}

	put_net(cifs_net_ns(server));

	list_del_init(&server->tcp_ses_list);
	spin_unlock(&cifs_tcp_ses_lock);

	cancel_delayed_work_sync(&server->echo);

	spin_lock(&GlobalMid_Lock);
	server->tcpStatus = CifsExiting;
	spin_unlock(&GlobalMid_Lock);

	cifs_crypto_shash_release(server);
	cifs_fscache_release_client_cookie(server);

	kfree(server->session_key.response);
	server->session_key.response = NULL;
	server->session_key.len = 0;

	task = xchg(&server->tsk, NULL);
	if (task)
		force_sig(SIGKILL, task);
}

static struct TCP_Server_Info *
cifs_get_tcp_session(struct smb_vol *volume_info)
{
	struct TCP_Server_Info *tcp_ses = NULL;
	struct sockaddr_storage addr;
	struct sockaddr_in *sin_server = (struct sockaddr_in *) &addr;
	struct sockaddr_in6 *sin_server6 = (struct sockaddr_in6 *) &addr;
	int rc;

	memset(&addr, 0, sizeof(struct sockaddr_storage));

	cFYI(1, "UNC: %s ip: %s", volume_info->UNC, volume_info->UNCip);

	if (volume_info->UNCip && volume_info->UNC) {
		rc = cifs_fill_sockaddr((struct sockaddr *)&addr,
					volume_info->UNCip,
					strlen(volume_info->UNCip),
					volume_info->port);
		if (!rc) {
			/* we failed translating address */
			rc = -EINVAL;
			goto out_err;
		}
	} else if (volume_info->UNCip) {
		/* BB using ip addr as tcp_ses name to connect to the
		   DFS root below */
		cERROR(1, "Connecting to DFS root not implemented yet");
		rc = -EINVAL;
		goto out_err;
	} else /* which tcp_sess DFS root would we conect to */ {
		cERROR(1, "CIFS mount error: No UNC path (e.g. -o "
			"unc=//192.168.1.100/public) specified");
		rc = -EINVAL;
		goto out_err;
	}

	/* see if we already have a matching tcp_ses */
	tcp_ses = cifs_find_tcp_session((struct sockaddr *)&addr, volume_info);
	if (tcp_ses)
		return tcp_ses;

	tcp_ses = kzalloc(sizeof(struct TCP_Server_Info), GFP_KERNEL);
	if (!tcp_ses) {
		rc = -ENOMEM;
		goto out_err;
	}

	rc = cifs_crypto_shash_allocate(tcp_ses);
	if (rc) {
		cERROR(1, "could not setup hash structures rc %d", rc);
		goto out_err;
	}

	cifs_set_net_ns(tcp_ses, get_net(current->nsproxy->net_ns));
	tcp_ses->hostname = extract_hostname(volume_info->UNC);
	if (IS_ERR(tcp_ses->hostname)) {
		rc = PTR_ERR(tcp_ses->hostname);
		goto out_err_crypto_release;
	}

	tcp_ses->noblocksnd = volume_info->noblocksnd;
	tcp_ses->noautotune = volume_info->noautotune;
	tcp_ses->tcp_nodelay = volume_info->sockopt_tcp_nodelay;
	atomic_set(&tcp_ses->inFlight, 0);
	init_waitqueue_head(&tcp_ses->response_q);
	init_waitqueue_head(&tcp_ses->request_q);
	INIT_LIST_HEAD(&tcp_ses->pending_mid_q);
	mutex_init(&tcp_ses->srv_mutex);
	memcpy(tcp_ses->workstation_RFC1001_name,
		volume_info->source_rfc1001_name, RFC1001_NAME_LEN_WITH_NULL);
	memcpy(tcp_ses->server_RFC1001_name,
		volume_info->target_rfc1001_name, RFC1001_NAME_LEN_WITH_NULL);
	tcp_ses->session_estab = false;
	tcp_ses->sequence_number = 0;
	tcp_ses->lstrp = jiffies;
	INIT_LIST_HEAD(&tcp_ses->tcp_ses_list);
	INIT_LIST_HEAD(&tcp_ses->smb_ses_list);
	INIT_DELAYED_WORK(&tcp_ses->echo, cifs_echo_request);

	/*
	 * at this point we are the only ones with the pointer
	 * to the struct since the kernel thread not created yet
	 * no need to spinlock this init of tcpStatus or srv_count
	 */
	tcp_ses->tcpStatus = CifsNew;
	memcpy(&tcp_ses->srcaddr, &volume_info->srcaddr,
	       sizeof(tcp_ses->srcaddr));
	++tcp_ses->srv_count;

	if (addr.ss_family == AF_INET6) {
		cFYI(1, "attempting ipv6 connect");
		/* BB should we allow ipv6 on port 139? */
		/* other OS never observed in Wild doing 139 with v6 */
		memcpy(&tcp_ses->dstaddr, sin_server6,
		       sizeof(struct sockaddr_in6));
	} else
		memcpy(&tcp_ses->dstaddr, sin_server,
		       sizeof(struct sockaddr_in));

	rc = ip_connect(tcp_ses);
	if (rc < 0) {
		cERROR(1, "Error connecting to socket. Aborting operation");
		goto out_err_crypto_release;
	}

	/*
	 * since we're in a cifs function already, we know that
	 * this will succeed. No need for try_module_get().
	 */
	__module_get(THIS_MODULE);
	tcp_ses->tsk = kthread_run((void *)(void *)cifs_demultiplex_thread,
				  tcp_ses, "cifsd");
	if (IS_ERR(tcp_ses->tsk)) {
		rc = PTR_ERR(tcp_ses->tsk);
		cERROR(1, "error %d create cifsd thread", rc);
		module_put(THIS_MODULE);
		goto out_err_crypto_release;
	}
	tcp_ses->tcpStatus = CifsNeedNegotiate;

	/* thread spawned, put it on the list */
	spin_lock(&cifs_tcp_ses_lock);
	list_add(&tcp_ses->tcp_ses_list, &cifs_tcp_ses_list);
	spin_unlock(&cifs_tcp_ses_lock);

	cifs_fscache_get_client_cookie(tcp_ses);

	/* queue echo request delayed work */
	queue_delayed_work(system_nrt_wq, &tcp_ses->echo, SMB_ECHO_INTERVAL);

	return tcp_ses;

out_err_crypto_release:
	cifs_crypto_shash_release(tcp_ses);

	put_net(cifs_net_ns(tcp_ses));

out_err:
	if (tcp_ses) {
		if (!IS_ERR(tcp_ses->hostname))
			kfree(tcp_ses->hostname);
		if (tcp_ses->ssocket)
			sock_release(tcp_ses->ssocket);
		kfree(tcp_ses);
	}
	return ERR_PTR(rc);
}

static int match_session(struct cifs_ses *ses, struct smb_vol *vol)
{
	switch (ses->server->secType) {
	case Kerberos:
		if (vol->cred_uid != ses->cred_uid)
			return 0;
		break;
	default:
		/* anything else takes username/password */
		if (ses->user_name == NULL)
			return 0;
		if (strncmp(ses->user_name, vol->username,
			    MAX_USERNAME_SIZE))
			return 0;
		if (strlen(vol->username) != 0 &&
		    ses->password != NULL &&
		    strncmp(ses->password,
			    vol->password ? vol->password : "",
			    MAX_PASSWORD_SIZE))
			return 0;
	}
	return 1;
}

static struct cifs_ses *
cifs_find_smb_ses(struct TCP_Server_Info *server, struct smb_vol *vol)
{
	struct cifs_ses *ses;

	spin_lock(&cifs_tcp_ses_lock);
	list_for_each_entry(ses, &server->smb_ses_list, smb_ses_list) {
		if (!match_session(ses, vol))
			continue;
		++ses->ses_count;
		spin_unlock(&cifs_tcp_ses_lock);
		return ses;
	}
	spin_unlock(&cifs_tcp_ses_lock);
	return NULL;
}

static void
cifs_put_smb_ses(struct cifs_ses *ses)
{
	int xid;
	struct TCP_Server_Info *server = ses->server;

	cFYI(1, "%s: ses_count=%d\n", __func__, ses->ses_count);
	spin_lock(&cifs_tcp_ses_lock);
	if (--ses->ses_count > 0) {
		spin_unlock(&cifs_tcp_ses_lock);
		return;
	}

	list_del_init(&ses->smb_ses_list);
	spin_unlock(&cifs_tcp_ses_lock);

	if (ses->status == CifsGood) {
		xid = GetXid();
		CIFSSMBLogoff(xid, ses);
		_FreeXid(xid);
	}
	sesInfoFree(ses);
	cifs_put_tcp_session(server);
}

static bool warned_on_ntlm;  /* globals init to false automatically */

static struct cifs_ses *
cifs_get_smb_ses(struct TCP_Server_Info *server, struct smb_vol *volume_info)
{
	int rc = -ENOMEM, xid;
	struct cifs_ses *ses;
	struct sockaddr_in *addr = (struct sockaddr_in *)&server->dstaddr;
	struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&server->dstaddr;

	xid = GetXid();

	ses = cifs_find_smb_ses(server, volume_info);
	if (ses) {
		cFYI(1, "Existing smb sess found (status=%d)", ses->status);

		mutex_lock(&ses->session_mutex);
		rc = cifs_negotiate_protocol(xid, ses);
		if (rc) {
			mutex_unlock(&ses->session_mutex);
			/* problem -- put our ses reference */
			cifs_put_smb_ses(ses);
			FreeXid(xid);
			return ERR_PTR(rc);
		}
		if (ses->need_reconnect) {
			cFYI(1, "Session needs reconnect");
			rc = cifs_setup_session(xid, ses,
						volume_info->local_nls);
			if (rc) {
				mutex_unlock(&ses->session_mutex);
				/* problem -- put our reference */
				cifs_put_smb_ses(ses);
				FreeXid(xid);
				return ERR_PTR(rc);
			}
		}
		mutex_unlock(&ses->session_mutex);

		/* existing SMB ses has a server reference already */
		cifs_put_tcp_session(server);
		FreeXid(xid);
		return ses;
	}

	cFYI(1, "Existing smb sess not found");
	ses = sesInfoAlloc();
	if (ses == NULL)
		goto get_ses_fail;

	/* new SMB session uses our server ref */
	ses->server = server;
	if (server->dstaddr.ss_family == AF_INET6)
		sprintf(ses->serverName, "%pI6", &addr6->sin6_addr);
	else
		sprintf(ses->serverName, "%pI4", &addr->sin_addr);

	if (volume_info->username) {
		ses->user_name = kstrdup(volume_info->username, GFP_KERNEL);
		if (!ses->user_name)
			goto get_ses_fail;
	}

	/* volume_info->password freed at unmount */
	if (volume_info->password) {
		ses->password = kstrdup(volume_info->password, GFP_KERNEL);
		if (!ses->password)
			goto get_ses_fail;
	}
	if (volume_info->domainname) {
		ses->domainName = kstrdup(volume_info->domainname, GFP_KERNEL);
		if (!ses->domainName)
			goto get_ses_fail;
	}
	ses->cred_uid = volume_info->cred_uid;
	ses->linux_uid = volume_info->linux_uid;

	/* ntlmv2 is much stronger than ntlm security, and has been broadly
	supported for many years, time to update default security mechanism */
	if ((volume_info->secFlg == 0) && warned_on_ntlm == false) {
		warned_on_ntlm = true;
		cERROR(1, "default security mechanism requested.  The default "
			"security mechanism will be upgraded from ntlm to "
			"ntlmv2 in kernel release 3.1");
	}
	ses->overrideSecFlg = volume_info->secFlg;

	mutex_lock(&ses->session_mutex);
	rc = cifs_negotiate_protocol(xid, ses);
	if (!rc)
		rc = cifs_setup_session(xid, ses, volume_info->local_nls);
	mutex_unlock(&ses->session_mutex);
	if (rc)
		goto get_ses_fail;

	/* success, put it on the list */
	spin_lock(&cifs_tcp_ses_lock);
	list_add(&ses->smb_ses_list, &server->smb_ses_list);
	spin_unlock(&cifs_tcp_ses_lock);

	FreeXid(xid);
	return ses;

get_ses_fail:
	sesInfoFree(ses);
	FreeXid(xid);
	return ERR_PTR(rc);
}

static int match_tcon(struct cifs_tcon *tcon, const char *unc)
{
	if (tcon->tidStatus == CifsExiting)
		return 0;
	if (strncmp(tcon->treeName, unc, MAX_TREE_SIZE))
		return 0;
	return 1;
}

static struct cifs_tcon *
cifs_find_tcon(struct cifs_ses *ses, const char *unc)
{
	struct list_head *tmp;
	struct cifs_tcon *tcon;

	spin_lock(&cifs_tcp_ses_lock);
	list_for_each(tmp, &ses->tcon_list) {
		tcon = list_entry(tmp, struct cifs_tcon, tcon_list);
		if (!match_tcon(tcon, unc))
			continue;
		++tcon->tc_count;
		spin_unlock(&cifs_tcp_ses_lock);
		return tcon;
	}
	spin_unlock(&cifs_tcp_ses_lock);
	return NULL;
}

static void
cifs_put_tcon(struct cifs_tcon *tcon)
{
	int xid;
	struct cifs_ses *ses = tcon->ses;

	cFYI(1, "%s: tc_count=%d\n", __func__, tcon->tc_count);
	spin_lock(&cifs_tcp_ses_lock);
	if (--tcon->tc_count > 0) {
		spin_unlock(&cifs_tcp_ses_lock);
		return;
	}

	list_del_init(&tcon->tcon_list);
	spin_unlock(&cifs_tcp_ses_lock);

	xid = GetXid();
	CIFSSMBTDis(xid, tcon);
	_FreeXid(xid);

	cifs_fscache_release_super_cookie(tcon);
	tconInfoFree(tcon);
	cifs_put_smb_ses(ses);
}

static struct cifs_tcon *
cifs_get_tcon(struct cifs_ses *ses, struct smb_vol *volume_info)
{
	int rc, xid;
	struct cifs_tcon *tcon;

	tcon = cifs_find_tcon(ses, volume_info->UNC);
	if (tcon) {
		cFYI(1, "Found match on UNC path");
		/* existing tcon already has a reference */
		cifs_put_smb_ses(ses);
		if (tcon->seal != volume_info->seal)
			cERROR(1, "transport encryption setting "
				   "conflicts with existing tid");
		return tcon;
	}

	tcon = tconInfoAlloc();
	if (tcon == NULL) {
		rc = -ENOMEM;
		goto out_fail;
	}

	tcon->ses = ses;
	if (volume_info->password) {
		tcon->password = kstrdup(volume_info->password, GFP_KERNEL);
		if (!tcon->password) {
			rc = -ENOMEM;
			goto out_fail;
		}
	}

	if (strchr(volume_info->UNC + 3, '\\') == NULL
	    && strchr(volume_info->UNC + 3, '/') == NULL) {
		cERROR(1, "Missing share name");
		rc = -ENODEV;
		goto out_fail;
	}

	/* BB Do we need to wrap session_mutex around
	 * this TCon call and Unix SetFS as
	 * we do on SessSetup and reconnect? */
	xid = GetXid();
	rc = CIFSTCon(xid, ses, volume_info->UNC, tcon, volume_info->local_nls);
	FreeXid(xid);
	cFYI(1, "CIFS Tcon rc = %d", rc);
	if (rc)
		goto out_fail;

	if (volume_info->nodfs) {
		tcon->Flags &= ~SMB_SHARE_IS_IN_DFS;
		cFYI(1, "DFS disabled (%d)", tcon->Flags);
	}
	tcon->seal = volume_info->seal;
	/* we can have only one retry value for a connection
	   to a share so for resources mounted more than once
	   to the same server share the last value passed in
	   for the retry flag is used */
	tcon->retry = volume_info->retry;
	tcon->nocase = volume_info->nocase;
	tcon->local_lease = volume_info->local_lease;

	spin_lock(&cifs_tcp_ses_lock);
	list_add(&tcon->tcon_list, &ses->tcon_list);
	spin_unlock(&cifs_tcp_ses_lock);

	cifs_fscache_get_super_cookie(tcon);

	return tcon;

out_fail:
	tconInfoFree(tcon);
	return ERR_PTR(rc);
}

void
cifs_put_tlink(struct tcon_link *tlink)
{
	if (!tlink || IS_ERR(tlink))
		return;

	if (!atomic_dec_and_test(&tlink->tl_count) ||
	    test_bit(TCON_LINK_IN_TREE, &tlink->tl_flags)) {
		tlink->tl_time = jiffies;
		return;
	}

	if (!IS_ERR(tlink_tcon(tlink)))
		cifs_put_tcon(tlink_tcon(tlink));
	kfree(tlink);
	return;
}

static inline struct tcon_link *
cifs_sb_master_tlink(struct cifs_sb_info *cifs_sb)
{
	return cifs_sb->master_tlink;
}

static int
compare_mount_options(struct super_block *sb, struct cifs_mnt_data *mnt_data)
{
	struct cifs_sb_info *old = CIFS_SB(sb);
	struct cifs_sb_info *new = mnt_data->cifs_sb;

	if ((sb->s_flags & CIFS_MS_MASK) != (mnt_data->flags & CIFS_MS_MASK))
		return 0;

	if ((old->mnt_cifs_flags & CIFS_MOUNT_MASK) !=
	    (new->mnt_cifs_flags & CIFS_MOUNT_MASK))
		return 0;

	if (old->rsize != new->rsize)
		return 0;

	/*
	 * We want to share sb only if we don't specify wsize or specified wsize
	 * is greater or equal than existing one.
	 */
	if (new->wsize && new->wsize < old->wsize)
		return 0;

	if (old->mnt_uid != new->mnt_uid || old->mnt_gid != new->mnt_gid)
		return 0;

	if (old->mnt_file_mode != new->mnt_file_mode ||
	    old->mnt_dir_mode != new->mnt_dir_mode)
		return 0;

	if (strcmp(old->local_nls->charset, new->local_nls->charset))
		return 0;

	if (old->actimeo != new->actimeo)
		return 0;

	return 1;
}

int
cifs_match_super(struct super_block *sb, void *data)
{
	struct cifs_mnt_data *mnt_data = (struct cifs_mnt_data *)data;
	struct smb_vol *volume_info;
	struct cifs_sb_info *cifs_sb;
	struct TCP_Server_Info *tcp_srv;
	struct cifs_ses *ses;
	struct cifs_tcon *tcon;
	struct tcon_link *tlink;
	struct sockaddr_storage addr;
	int rc = 0;

	memset(&addr, 0, sizeof(struct sockaddr_storage));

	spin_lock(&cifs_tcp_ses_lock);
	cifs_sb = CIFS_SB(sb);
	tlink = cifs_get_tlink(cifs_sb_master_tlink(cifs_sb));
	if (IS_ERR(tlink)) {
		spin_unlock(&cifs_tcp_ses_lock);
		return rc;
	}
	tcon = tlink_tcon(tlink);
	ses = tcon->ses;
	tcp_srv = ses->server;

	volume_info = mnt_data->vol;

	if (!volume_info->UNCip || !volume_info->UNC)
		goto out;

	rc = cifs_fill_sockaddr((struct sockaddr *)&addr,
				volume_info->UNCip,
				strlen(volume_info->UNCip),
				volume_info->port);
	if (!rc)
		goto out;

	if (!match_server(tcp_srv, (struct sockaddr *)&addr, volume_info) ||
	    !match_session(ses, volume_info) ||
	    !match_tcon(tcon, volume_info->UNC)) {
		rc = 0;
		goto out;
	}

	rc = compare_mount_options(sb, mnt_data);
out:
	spin_unlock(&cifs_tcp_ses_lock);
	cifs_put_tlink(tlink);
	return rc;
}

int
get_dfs_path(int xid, struct cifs_ses *pSesInfo, const char *old_path,
	     const struct nls_table *nls_codepage, unsigned int *pnum_referrals,
	     struct dfs_info3_param **preferrals, int remap)
{
	char *temp_unc;
	int rc = 0;

	*pnum_referrals = 0;
	*preferrals = NULL;

	if (pSesInfo->ipc_tid == 0) {
		temp_unc = kmalloc(2 /* for slashes */ +
			strnlen(pSesInfo->serverName,
				SERVER_NAME_LEN_WITH_NULL * 2)
				 + 1 + 4 /* slash IPC$ */  + 2,
				GFP_KERNEL);
		if (temp_unc == NULL)
			return -ENOMEM;
		temp_unc[0] = '\\';
		temp_unc[1] = '\\';
		strcpy(temp_unc + 2, pSesInfo->serverName);
		strcpy(temp_unc + 2 + strlen(pSesInfo->serverName), "\\IPC$");
		rc = CIFSTCon(xid, pSesInfo, temp_unc, NULL, nls_codepage);
		cFYI(1, "CIFS Tcon rc = %d ipc_tid = %d", rc, pSesInfo->ipc_tid);
		kfree(temp_unc);
	}
	if (rc == 0)
		rc = CIFSGetDFSRefer(xid, pSesInfo, old_path, preferrals,
				     pnum_referrals, nls_codepage, remap);
	/* BB map targetUNCs to dfs_info3 structures, here or
		in CIFSGetDFSRefer BB */

	return rc;
}

#ifdef CONFIG_DEBUG_LOCK_ALLOC
static struct lock_class_key cifs_key[2];
static struct lock_class_key cifs_slock_key[2];

static inline void
cifs_reclassify_socket4(struct socket *sock)
{
	struct sock *sk = sock->sk;
	BUG_ON(sock_owned_by_user(sk));
	sock_lock_init_class_and_name(sk, "slock-AF_INET-CIFS",
		&cifs_slock_key[0], "sk_lock-AF_INET-CIFS", &cifs_key[0]);
}

static inline void
cifs_reclassify_socket6(struct socket *sock)
{
	struct sock *sk = sock->sk;
	BUG_ON(sock_owned_by_user(sk));
	sock_lock_init_class_and_name(sk, "slock-AF_INET6-CIFS",
		&cifs_slock_key[1], "sk_lock-AF_INET6-CIFS", &cifs_key[1]);
}
#else
static inline void
cifs_reclassify_socket4(struct socket *sock)
{
}

static inline void
cifs_reclassify_socket6(struct socket *sock)
{
}
#endif

/* See RFC1001 section 14 on representation of Netbios names */
static void rfc1002mangle(char *target, char *source, unsigned int length)
{
	unsigned int i, j;

	for (i = 0, j = 0; i < (length); i++) {
		/* mask a nibble at a time and encode */
		target[j] = 'A' + (0x0F & (source[i] >> 4));
		target[j+1] = 'A' + (0x0F & source[i]);
		j += 2;
	}

}

static int
bind_socket(struct TCP_Server_Info *server)
{
	int rc = 0;
	if (server->srcaddr.ss_family != AF_UNSPEC) {
		/* Bind to the specified local IP address */
		struct socket *socket = server->ssocket;
		rc = socket->ops->bind(socket,
				       (struct sockaddr *) &server->srcaddr,
				       sizeof(server->srcaddr));
		if (rc < 0) {
			struct sockaddr_in *saddr4;
			struct sockaddr_in6 *saddr6;
			saddr4 = (struct sockaddr_in *)&server->srcaddr;
			saddr6 = (struct sockaddr_in6 *)&server->srcaddr;
			if (saddr6->sin6_family == AF_INET6)
				cERROR(1, "cifs: "
				       "Failed to bind to: %pI6c, error: %d\n",
				       &saddr6->sin6_addr, rc);
			else
				cERROR(1, "cifs: "
				       "Failed to bind to: %pI4, error: %d\n",
				       &saddr4->sin_addr.s_addr, rc);
		}
	}
	return rc;
}

static int
ip_rfc1001_connect(struct TCP_Server_Info *server)
{
	int rc = 0;
	/*
	 * some servers require RFC1001 sessinit before sending
	 * negprot - BB check reconnection in case where second
	 * sessinit is sent but no second negprot
	 */
	struct rfc1002_session_packet *ses_init_buf;
	struct smb_hdr *smb_buf;
	ses_init_buf = kzalloc(sizeof(struct rfc1002_session_packet),
			       GFP_KERNEL);
	if (ses_init_buf) {
		ses_init_buf->trailer.session_req.called_len = 32;

		if (server->server_RFC1001_name &&
		    server->server_RFC1001_name[0] != 0)
			rfc1002mangle(ses_init_buf->trailer.
				      session_req.called_name,
				      server->server_RFC1001_name,
				      RFC1001_NAME_LEN_WITH_NULL);
		else
			rfc1002mangle(ses_init_buf->trailer.
				      session_req.called_name,
				      DEFAULT_CIFS_CALLED_NAME,
				      RFC1001_NAME_LEN_WITH_NULL);

		ses_init_buf->trailer.session_req.calling_len = 32;

		/*
		 * calling name ends in null (byte 16) from old smb
		 * convention.
		 */
		if (server->workstation_RFC1001_name &&
		    server->workstation_RFC1001_name[0] != 0)
			rfc1002mangle(ses_init_buf->trailer.
				      session_req.calling_name,
				      server->workstation_RFC1001_name,
				      RFC1001_NAME_LEN_WITH_NULL);
		else
			rfc1002mangle(ses_init_buf->trailer.
				      session_req.calling_name,
				      "LINUX_CIFS_CLNT",
				      RFC1001_NAME_LEN_WITH_NULL);

		ses_init_buf->trailer.session_req.scope1 = 0;
		ses_init_buf->trailer.session_req.scope2 = 0;
		smb_buf = (struct smb_hdr *)ses_init_buf;

		/* sizeof RFC1002_SESSION_REQUEST with no scope */
		smb_buf->smb_buf_length = cpu_to_be32(0x81000044);
		rc = smb_send(server, smb_buf, 0x44);
		kfree(ses_init_buf);
		/*
		 * RFC1001 layer in at least one server
		 * requires very short break before negprot
		 * presumably because not expecting negprot
		 * to follow so fast.  This is a simple
		 * solution that works without
		 * complicating the code and causes no
		 * significant slowing down on mount
		 * for everyone else
		 */
		usleep_range(1000, 2000);
	}
	/*
	 * else the negprot may still work without this
	 * even though malloc failed
	 */

	return rc;
}

static int
generic_ip_connect(struct TCP_Server_Info *server)
{
	int rc = 0;
	__be16 sport;
	int slen, sfamily;
	struct socket *socket = server->ssocket;
	struct sockaddr *saddr;

	saddr = (struct sockaddr *) &server->dstaddr;

	if (server->dstaddr.ss_family == AF_INET6) {
		sport = ((struct sockaddr_in6 *) saddr)->sin6_port;
		slen = sizeof(struct sockaddr_in6);
		sfamily = AF_INET6;
	} else {
		sport = ((struct sockaddr_in *) saddr)->sin_port;
		slen = sizeof(struct sockaddr_in);
		sfamily = AF_INET;
	}

	if (socket == NULL) {
		rc = __sock_create(cifs_net_ns(server), sfamily, SOCK_STREAM,
				   IPPROTO_TCP, &socket, 1);
		if (rc < 0) {
			cERROR(1, "Error %d creating socket", rc);
			server->ssocket = NULL;
			return rc;
		}

		/* BB other socket options to set KEEPALIVE, NODELAY? */
		cFYI(1, "Socket created");
		server->ssocket = socket;
		socket->sk->sk_allocation = GFP_NOFS;
		if (sfamily == AF_INET6)
			cifs_reclassify_socket6(socket);
		else
			cifs_reclassify_socket4(socket);
	}

	rc = bind_socket(server);
	if (rc < 0)
		return rc;

	/*
	 * Eventually check for other socket options to change from
	 * the default. sock_setsockopt not used because it expects
	 * user space buffer
	 */
	socket->sk->sk_rcvtimeo = 7 * HZ;
	socket->sk->sk_sndtimeo = 5 * HZ;

	/* make the bufsizes depend on wsize/rsize and max requests */
	if (server->noautotune) {
		if (socket->sk->sk_sndbuf < (200 * 1024))
			socket->sk->sk_sndbuf = 200 * 1024;
		if (socket->sk->sk_rcvbuf < (140 * 1024))
			socket->sk->sk_rcvbuf = 140 * 1024;
	}

	if (server->tcp_nodelay) {
		int val = 1;
		rc = kernel_setsockopt(socket, SOL_TCP, TCP_NODELAY,
				(char *)&val, sizeof(val));
		if (rc)
			cFYI(1, "set TCP_NODELAY socket option error %d", rc);
	}

	 cFYI(1, "sndbuf %d rcvbuf %d rcvtimeo 0x%lx",
		 socket->sk->sk_sndbuf,
		 socket->sk->sk_rcvbuf, socket->sk->sk_rcvtimeo);

	rc = socket->ops->connect(socket, saddr, slen, 0);
	if (rc < 0) {
		cFYI(1, "Error %d connecting to server", rc);
		sock_release(socket);
		server->ssocket = NULL;
		return rc;
	}

	if (sport == htons(RFC1001_PORT))
		rc = ip_rfc1001_connect(server);

	return rc;
}

static int
ip_connect(struct TCP_Server_Info *server)
{
	__be16 *sport;
	struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&server->dstaddr;
	struct sockaddr_in *addr = (struct sockaddr_in *)&server->dstaddr;

	if (server->dstaddr.ss_family == AF_INET6)
		sport = &addr6->sin6_port;
	else
		sport = &addr->sin_port;

	if (*sport == 0) {
		int rc;

		/* try with 445 port at first */
		*sport = htons(CIFS_PORT);

		rc = generic_ip_connect(server);
		if (rc >= 0)
			return rc;

		/* if it failed, try with 139 port */
		*sport = htons(RFC1001_PORT);
	}

	return generic_ip_connect(server);
}

void reset_cifs_unix_caps(int xid, struct cifs_tcon *tcon,
			  struct cifs_sb_info *cifs_sb, struct smb_vol *vol_info)
{
	/* if we are reconnecting then should we check to see if
	 * any requested capabilities changed locally e.g. via
	 * remount but we can not do much about it here
	 * if they have (even if we could detect it by the following)
	 * Perhaps we could add a backpointer to array of sb from tcon
	 * or if we change to make all sb to same share the same
	 * sb as NFS - then we only have one backpointer to sb.
	 * What if we wanted to mount the server share twice once with
	 * and once without posixacls or posix paths? */
	__u64 saved_cap = le64_to_cpu(tcon->fsUnixInfo.Capability);

	if (vol_info && vol_info->no_linux_ext) {
		tcon->fsUnixInfo.Capability = 0;
		tcon->unix_ext = 0; /* Unix Extensions disabled */
		cFYI(1, "Linux protocol extensions disabled");
		return;
	} else if (vol_info)
		tcon->unix_ext = 1; /* Unix Extensions supported */

	if (tcon->unix_ext == 0) {
		cFYI(1, "Unix extensions disabled so not set on reconnect");
		return;
	}

	if (!CIFSSMBQFSUnixInfo(xid, tcon)) {
		__u64 cap = le64_to_cpu(tcon->fsUnixInfo.Capability);
		cFYI(1, "unix caps which server supports %lld", cap);
		/* check for reconnect case in which we do not
		   want to change the mount behavior if we can avoid it */
		if (vol_info == NULL) {
			/* turn off POSIX ACL and PATHNAMES if not set
			   originally at mount time */
			if ((saved_cap & CIFS_UNIX_POSIX_ACL_CAP) == 0)
				cap &= ~CIFS_UNIX_POSIX_ACL_CAP;
			if ((saved_cap & CIFS_UNIX_POSIX_PATHNAMES_CAP) == 0) {
				if (cap & CIFS_UNIX_POSIX_PATHNAMES_CAP)
					cERROR(1, "POSIXPATH support change");
				cap &= ~CIFS_UNIX_POSIX_PATHNAMES_CAP;
			} else if ((cap & CIFS_UNIX_POSIX_PATHNAMES_CAP) == 0) {
				cERROR(1, "possible reconnect error");
				cERROR(1, "server disabled POSIX path support");
			}
		}

		if (cap & CIFS_UNIX_TRANSPORT_ENCRYPTION_MANDATORY_CAP)
			cERROR(1, "per-share encryption not supported yet");

		cap &= CIFS_UNIX_CAP_MASK;
		if (vol_info && vol_info->no_psx_acl)
			cap &= ~CIFS_UNIX_POSIX_ACL_CAP;
		else if (CIFS_UNIX_POSIX_ACL_CAP & cap) {
			cFYI(1, "negotiated posix acl support");
			if (cifs_sb)
				cifs_sb->mnt_cifs_flags |=
					CIFS_MOUNT_POSIXACL;
		}

		if (vol_info && vol_info->posix_paths == 0)
			cap &= ~CIFS_UNIX_POSIX_PATHNAMES_CAP;
		else if (cap & CIFS_UNIX_POSIX_PATHNAMES_CAP) {
			cFYI(1, "negotiate posix pathnames");
			if (cifs_sb)
				cifs_sb->mnt_cifs_flags |=
					CIFS_MOUNT_POSIX_PATHS;
		}

		if (cifs_sb && (cifs_sb->rsize > 127 * 1024)) {
			if ((cap & CIFS_UNIX_LARGE_READ_CAP) == 0) {
				cifs_sb->rsize = 127 * 1024;
				cFYI(DBG2, "larger reads not supported by srv");
			}
		}


		cFYI(1, "Negotiate caps 0x%x", (int)cap);
#ifdef CONFIG_CIFS_DEBUG2
		if (cap & CIFS_UNIX_FCNTL_CAP)
			cFYI(1, "FCNTL cap");
		if (cap & CIFS_UNIX_EXTATTR_CAP)
			cFYI(1, "EXTATTR cap");
		if (cap & CIFS_UNIX_POSIX_PATHNAMES_CAP)
			cFYI(1, "POSIX path cap");
		if (cap & CIFS_UNIX_XATTR_CAP)
			cFYI(1, "XATTR cap");
		if (cap & CIFS_UNIX_POSIX_ACL_CAP)
			cFYI(1, "POSIX ACL cap");
		if (cap & CIFS_UNIX_LARGE_READ_CAP)
			cFYI(1, "very large read cap");
		if (cap & CIFS_UNIX_LARGE_WRITE_CAP)
			cFYI(1, "very large write cap");
		if (cap & CIFS_UNIX_TRANSPORT_ENCRYPTION_CAP)
			cFYI(1, "transport encryption cap");
		if (cap & CIFS_UNIX_TRANSPORT_ENCRYPTION_MANDATORY_CAP)
			cFYI(1, "mandatory transport encryption cap");
#endif /* CIFS_DEBUG2 */
		if (CIFSSMBSetFSUnixInfo(xid, tcon, cap)) {
			if (vol_info == NULL) {
				cFYI(1, "resetting capabilities failed");
			} else
				cERROR(1, "Negotiating Unix capabilities "
					   "with the server failed.  Consider "
					   "mounting with the Unix Extensions\n"
					   "disabled, if problems are found, "
					   "by specifying the nounix mount "
					   "option.");

		}
	}
}

void cifs_setup_cifs_sb(struct smb_vol *pvolume_info,
			struct cifs_sb_info *cifs_sb)
{
	INIT_DELAYED_WORK(&cifs_sb->prune_tlinks, cifs_prune_tlinks);

	spin_lock_init(&cifs_sb->tlink_tree_lock);
	cifs_sb->tlink_tree = RB_ROOT;

	if (pvolume_info->rsize > CIFSMaxBufSize) {
		cERROR(1, "rsize %d too large, using MaxBufSize",
			pvolume_info->rsize);
		cifs_sb->rsize = CIFSMaxBufSize;
	} else if ((pvolume_info->rsize) &&
			(pvolume_info->rsize <= CIFSMaxBufSize))
		cifs_sb->rsize = pvolume_info->rsize;
	else /* default */
		cifs_sb->rsize = CIFSMaxBufSize;

	if (cifs_sb->rsize < 2048) {
		cifs_sb->rsize = 2048;
		/* Windows ME may prefer this */
		cFYI(1, "readsize set to minimum: 2048");
	}

	/*
	 * Temporarily set wsize for matching superblock. If we end up using
	 * new sb then cifs_negotiate_wsize will later negotiate it downward
	 * if needed.
	 */
	cifs_sb->wsize = pvolume_info->wsize;

	cifs_sb->mnt_uid = pvolume_info->linux_uid;
	cifs_sb->mnt_gid = pvolume_info->linux_gid;
	cifs_sb->mnt_file_mode = pvolume_info->file_mode;
	cifs_sb->mnt_dir_mode = pvolume_info->dir_mode;
	cFYI(1, "file mode: 0x%x  dir mode: 0x%x",
		cifs_sb->mnt_file_mode, cifs_sb->mnt_dir_mode);

	cifs_sb->actimeo = pvolume_info->actimeo;
	cifs_sb->local_nls = pvolume_info->local_nls;

	if (pvolume_info->noperm)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NO_PERM;
	if (pvolume_info->setuids)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_SET_UID;
	if (pvolume_info->server_ino)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_SERVER_INUM;
	if (pvolume_info->remap)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_MAP_SPECIAL_CHR;
	if (pvolume_info->no_xattr)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NO_XATTR;
	if (pvolume_info->sfu_emul)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_UNX_EMUL;
	if (pvolume_info->nobrl)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NO_BRL;
	if (pvolume_info->nostrictsync)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NOSSYNC;
	if (pvolume_info->mand_lock)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NOPOSIXBRL;
	if (pvolume_info->rwpidforward)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_RWPIDFORWARD;
	if (pvolume_info->cifs_acl)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_CIFS_ACL;
	if (pvolume_info->override_uid)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_OVERR_UID;
	if (pvolume_info->override_gid)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_OVERR_GID;
	if (pvolume_info->dynperm)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_DYNPERM;
	if (pvolume_info->fsc)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_FSCACHE;
	if (pvolume_info->multiuser)
		cifs_sb->mnt_cifs_flags |= (CIFS_MOUNT_MULTIUSER |
					    CIFS_MOUNT_NO_PERM);
	if (pvolume_info->strict_io)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_STRICT_IO;
	if (pvolume_info->direct_io) {
		cFYI(1, "mounting share using direct i/o");
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_DIRECT_IO;
	}
	if (pvolume_info->mfsymlinks) {
		if (pvolume_info->sfu_emul) {
			cERROR(1,  "mount option mfsymlinks ignored if sfu "
				   "mount option is used");
		} else {
			cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_MF_SYMLINKS;
		}
	}

	if ((pvolume_info->cifs_acl) && (pvolume_info->dynperm))
		cERROR(1, "mount option dynperm ignored if cifsacl "
			   "mount option supported");
}

/*
 * When the server supports very large writes via POSIX extensions, we can
 * allow up to 2^24-1, minus the size of a WRITE_AND_X header, not including
 * the RFC1001 length.
 *
 * Note that this might make for "interesting" allocation problems during
 * writeback however as we have to allocate an array of pointers for the
 * pages. A 16M write means ~32kb page array with PAGE_CACHE_SIZE == 4096.
 */
#define CIFS_MAX_WSIZE ((1<<24) - 1 - sizeof(WRITE_REQ) + 4)

/*
 * When the server doesn't allow large posix writes, only allow a wsize of
 * 2^17-1 minus the size of the WRITE_AND_X header. That allows for a write up
 * to the maximum size described by RFC1002.
 */
#define CIFS_MAX_RFC1002_WSIZE ((1<<17) - 1 - sizeof(WRITE_REQ) + 4)

/*
 * The default wsize is 1M. find_get_pages seems to return a maximum of 256
 * pages in a single call. With PAGE_CACHE_SIZE == 4k, this means we can fill
 * a single wsize request with a single call.
 */
#define CIFS_DEFAULT_WSIZE (1024 * 1024)

static unsigned int
cifs_negotiate_wsize(struct cifs_tcon *tcon, struct smb_vol *pvolume_info)
{
	__u64 unix_cap = le64_to_cpu(tcon->fsUnixInfo.Capability);
	struct TCP_Server_Info *server = tcon->ses->server;
	unsigned int wsize = pvolume_info->wsize ? pvolume_info->wsize :
				CIFS_DEFAULT_WSIZE;

	/* can server support 24-bit write sizes? (via UNIX extensions) */
	if (!tcon->unix_ext || !(unix_cap & CIFS_UNIX_LARGE_WRITE_CAP))
		wsize = min_t(unsigned int, wsize, CIFS_MAX_RFC1002_WSIZE);

	/*
	 * no CAP_LARGE_WRITE_X or is signing enabled without CAP_UNIX set?
	 * Limit it to max buffer offered by the server, minus the size of the
	 * WRITEX header, not including the 4 byte RFC1001 length.
	 */
	if (!(server->capabilities & CAP_LARGE_WRITE_X) ||
	    (!(server->capabilities & CAP_UNIX) &&
	     (server->sec_mode & (SECMODE_SIGN_ENABLED|SECMODE_SIGN_REQUIRED))))
		wsize = min_t(unsigned int, wsize,
				server->maxBuf - sizeof(WRITE_REQ) + 4);

	/* hard limit of CIFS_MAX_WSIZE */
	wsize = min_t(unsigned int, wsize, CIFS_MAX_WSIZE);

	return wsize;
}

static int
is_path_accessible(int xid, struct cifs_tcon *tcon,
		   struct cifs_sb_info *cifs_sb, const char *full_path)
{
	int rc;
	FILE_ALL_INFO *pfile_info;

	pfile_info = kmalloc(sizeof(FILE_ALL_INFO), GFP_KERNEL);
	if (pfile_info == NULL)
		return -ENOMEM;

	rc = CIFSSMBQPathInfo(xid, tcon, full_path, pfile_info,
			      0 /* not legacy */, cifs_sb->local_nls,
			      cifs_sb->mnt_cifs_flags &
				CIFS_MOUNT_MAP_SPECIAL_CHR);

	if (rc == -EOPNOTSUPP || rc == -EINVAL)
		rc = SMBQueryInformation(xid, tcon, full_path, pfile_info,
				cifs_sb->local_nls, cifs_sb->mnt_cifs_flags &
				  CIFS_MOUNT_MAP_SPECIAL_CHR);
	kfree(pfile_info);
	return rc;
}

static void
cleanup_volume_info_contents(struct smb_vol *volume_info)
{
	kfree(volume_info->username);
	kzfree(volume_info->password);
	kfree(volume_info->UNC);
	if (volume_info->UNCip != volume_info->UNC + 2)
		kfree(volume_info->UNCip);
	kfree(volume_info->domainname);
	kfree(volume_info->iocharset);
	kfree(volume_info->prepath);
}

void
cifs_cleanup_volume_info(struct smb_vol *volume_info)
{
	if (!volume_info)
		return;
	cleanup_volume_info_contents(volume_info);
	kfree(volume_info);
}


#ifdef CONFIG_CIFS_DFS_UPCALL
/* build_path_to_root returns full path to root when
 * we do not have an exiting connection (tcon) */
static char *
build_unc_path_to_root(const struct smb_vol *vol,
		const struct cifs_sb_info *cifs_sb)
{
	char *full_path, *pos;
	unsigned int pplen = vol->prepath ? strlen(vol->prepath) : 0;
	unsigned int unc_len = strnlen(vol->UNC, MAX_TREE_SIZE + 1);

	full_path = kmalloc(unc_len + pplen + 1, GFP_KERNEL);
	if (full_path == NULL)
		return ERR_PTR(-ENOMEM);

	strncpy(full_path, vol->UNC, unc_len);
	pos = full_path + unc_len;

	if (pplen) {
		strncpy(pos, vol->prepath, pplen);
		pos += pplen;
	}

	*pos = '\0'; /* add trailing null */
	convert_delimiter(full_path, CIFS_DIR_SEP(cifs_sb));
	cFYI(1, "%s: full_path=%s", __func__, full_path);
	return full_path;
}

/*
 * Perform a dfs referral query for a share and (optionally) prefix
 *
 * If a referral is found, cifs_sb->mountdata will be (re-)allocated
 * to a string containing updated options for the submount.  Otherwise it
 * will be left untouched.
 *
 * Returns the rc from get_dfs_path to the caller, which can be used to
 * determine whether there were referrals.
 */
static int
expand_dfs_referral(int xid, struct cifs_ses *pSesInfo,
		    struct smb_vol *volume_info, struct cifs_sb_info *cifs_sb,
		    int check_prefix)
{
	int rc;
	unsigned int num_referrals = 0;
	struct dfs_info3_param *referrals = NULL;
	char *full_path = NULL, *ref_path = NULL, *mdata = NULL;

	full_path = build_unc_path_to_root(volume_info, cifs_sb);
	if (IS_ERR(full_path))
		return PTR_ERR(full_path);

	/* For DFS paths, skip the first '\' of the UNC */
	ref_path = check_prefix ? full_path + 1 : volume_info->UNC + 1;

	rc = get_dfs_path(xid, pSesInfo , ref_path, cifs_sb->local_nls,
			  &num_referrals, &referrals,
			  cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);

	if (!rc && num_referrals > 0) {
		char *fake_devname = NULL;

		mdata = cifs_compose_mount_options(cifs_sb->mountdata,
						   full_path + 1, referrals,
						   &fake_devname);

		free_dfs_info_array(referrals, num_referrals);

		if (IS_ERR(mdata)) {
			rc = PTR_ERR(mdata);
			mdata = NULL;
		} else {
			cleanup_volume_info_contents(volume_info);
			memset(volume_info, '\0', sizeof(*volume_info));
			rc = cifs_setup_volume_info(volume_info, mdata,
							fake_devname);
		}
		kfree(fake_devname);
		kfree(cifs_sb->mountdata);
		cifs_sb->mountdata = mdata;
	}
	kfree(full_path);
	return rc;
}
#endif

static int
cifs_setup_volume_info(struct smb_vol *volume_info, char *mount_data,
			const char *devname)
{
	int rc = 0;

	if (cifs_parse_mount_options(mount_data, devname, volume_info))
		return -EINVAL;

	if (volume_info->nullauth) {
		cFYI(1, "null user");
		volume_info->username = kzalloc(1, GFP_KERNEL);
		if (volume_info->username == NULL)
			return -ENOMEM;
	} else if (volume_info->username) {
		/* BB fixme parse for domain name here */
		cFYI(1, "Username: %s", volume_info->username);
	} else {
		cifserror("No username specified");
	/* In userspace mount helper we can get user name from alternate
	   locations such as env variables and files on disk */
		return -EINVAL;
	}

	/* this is needed for ASCII cp to Unicode converts */
	if (volume_info->iocharset == NULL) {
		/* load_nls_default cannot return null */
		volume_info->local_nls = load_nls_default();
	} else {
		volume_info->local_nls = load_nls(volume_info->iocharset);
		if (volume_info->local_nls == NULL) {
			cERROR(1, "CIFS mount error: iocharset %s not found",
				 volume_info->iocharset);
			return -ELIBACC;
		}
	}

	return rc;
}

struct smb_vol *
cifs_get_volume_info(char *mount_data, const char *devname)
{
	int rc;
	struct smb_vol *volume_info;

	volume_info = kzalloc(sizeof(struct smb_vol), GFP_KERNEL);
	if (!volume_info)
		return ERR_PTR(-ENOMEM);

	rc = cifs_setup_volume_info(volume_info, mount_data, devname);
	if (rc) {
		cifs_cleanup_volume_info(volume_info);
		volume_info = ERR_PTR(rc);
	}

	return volume_info;
}

int
cifs_mount(struct cifs_sb_info *cifs_sb, struct smb_vol *volume_info)
{
	int rc;
	int xid;
	struct cifs_ses *pSesInfo;
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *srvTcp;
	char   *full_path;
	struct tcon_link *tlink;
#ifdef CONFIG_CIFS_DFS_UPCALL
	int referral_walks_count = 0;
#endif

	rc = bdi_setup_and_register(&cifs_sb->bdi, "cifs", BDI_CAP_MAP_COPY);
	if (rc)
		return rc;

	cifs_sb->bdi.ra_pages = default_backing_dev_info.ra_pages;

#ifdef CONFIG_CIFS_DFS_UPCALL
try_mount_again:
	/* cleanup activities if we're chasing a referral */
	if (referral_walks_count) {
		if (tcon)
			cifs_put_tcon(tcon);
		else if (pSesInfo)
			cifs_put_smb_ses(pSesInfo);

		FreeXid(xid);
	}
#endif
	rc = 0;
	tcon = NULL;
	pSesInfo = NULL;
	srvTcp = NULL;
	full_path = NULL;
	tlink = NULL;

	xid = GetXid();

	/* get a reference to a tcp session */
	srvTcp = cifs_get_tcp_session(volume_info);
	if (IS_ERR(srvTcp)) {
		rc = PTR_ERR(srvTcp);
		bdi_destroy(&cifs_sb->bdi);
		goto out;
	}

	/* get a reference to a SMB session */
	pSesInfo = cifs_get_smb_ses(srvTcp, volume_info);
	if (IS_ERR(pSesInfo)) {
		rc = PTR_ERR(pSesInfo);
		pSesInfo = NULL;
		goto mount_fail_check;
	}

	/* search for existing tcon to this server share */
	tcon = cifs_get_tcon(pSesInfo, volume_info);
	if (IS_ERR(tcon)) {
		rc = PTR_ERR(tcon);
		tcon = NULL;
		goto remote_path_check;
	}

	/* tell server which Unix caps we support */
	if (tcon->ses->capabilities & CAP_UNIX) {
		/* reset of caps checks mount to see if unix extensions
		   disabled for just this mount */
		reset_cifs_unix_caps(xid, tcon, cifs_sb, volume_info);
		if ((tcon->ses->server->tcpStatus == CifsNeedReconnect) &&
		    (le64_to_cpu(tcon->fsUnixInfo.Capability) &
		     CIFS_UNIX_TRANSPORT_ENCRYPTION_MANDATORY_CAP)) {
			rc = -EACCES;
			goto mount_fail_check;
		}
	} else
		tcon->unix_ext = 0; /* server does not support them */

	/* do not care if following two calls succeed - informational */
	if (!tcon->ipc) {
		CIFSSMBQFSDeviceInfo(xid, tcon);
		CIFSSMBQFSAttributeInfo(xid, tcon);
	}

	if ((tcon->unix_ext == 0) && (cifs_sb->rsize > (1024 * 127))) {
		cifs_sb->rsize = 1024 * 127;
		cFYI(DBG2, "no very large read support, rsize now 127K");
	}
	if (!(tcon->ses->capabilities & CAP_LARGE_READ_X))
		cifs_sb->rsize = min(cifs_sb->rsize,
			       (tcon->ses->server->maxBuf - MAX_CIFS_HDR_SIZE));

	cifs_sb->wsize = cifs_negotiate_wsize(tcon, volume_info);

remote_path_check:
#ifdef CONFIG_CIFS_DFS_UPCALL
	/*
	 * Perform an unconditional check for whether there are DFS
	 * referrals for this path without prefix, to provide support
	 * for DFS referrals from w2k8 servers which don't seem to respond
	 * with PATH_NOT_COVERED to requests that include the prefix.
	 * Chase the referral if found, otherwise continue normally.
	 */
	if (referral_walks_count == 0) {
		int refrc = expand_dfs_referral(xid, pSesInfo, volume_info,
						cifs_sb, false);
		if (!refrc) {
			referral_walks_count++;
			goto try_mount_again;
		}
	}
#endif

	/* check if a whole path is not remote */
	if (!rc && tcon) {
		/* build_path_to_root works only when we have a valid tcon */
		full_path = cifs_build_path_to_root(volume_info, cifs_sb, tcon);
		if (full_path == NULL) {
			rc = -ENOMEM;
			goto mount_fail_check;
		}
		rc = is_path_accessible(xid, tcon, cifs_sb, full_path);
		if (rc != 0 && rc != -EREMOTE) {
			kfree(full_path);
			goto mount_fail_check;
		}
		kfree(full_path);
	}

	/* get referral if needed */
	if (rc == -EREMOTE) {
#ifdef CONFIG_CIFS_DFS_UPCALL
		if (referral_walks_count > MAX_NESTED_LINKS) {
			/*
			 * BB: when we implement proper loop detection,
			 *     we will remove this check. But now we need it
			 *     to prevent an indefinite loop if 'DFS tree' is
			 *     misconfigured (i.e. has loops).
			 */
			rc = -ELOOP;
			goto mount_fail_check;
		}

		rc = expand_dfs_referral(xid, pSesInfo, volume_info, cifs_sb,
					 true);

		if (!rc) {
			referral_walks_count++;
			goto try_mount_again;
		}
		goto mount_fail_check;
#else /* No DFS support, return error on mount */
		rc = -EOPNOTSUPP;
#endif
	}

	if (rc)
		goto mount_fail_check;

	/* now, hang the tcon off of the superblock */
	tlink = kzalloc(sizeof *tlink, GFP_KERNEL);
	if (tlink == NULL) {
		rc = -ENOMEM;
		goto mount_fail_check;
	}

	tlink->tl_uid = pSesInfo->linux_uid;
	tlink->tl_tcon = tcon;
	tlink->tl_time = jiffies;
	set_bit(TCON_LINK_MASTER, &tlink->tl_flags);
	set_bit(TCON_LINK_IN_TREE, &tlink->tl_flags);

	cifs_sb->master_tlink = tlink;
	spin_lock(&cifs_sb->tlink_tree_lock);
	tlink_rb_insert(&cifs_sb->tlink_tree, tlink);
	spin_unlock(&cifs_sb->tlink_tree_lock);

	queue_delayed_work(system_nrt_wq, &cifs_sb->prune_tlinks,
				TLINK_IDLE_EXPIRE);

mount_fail_check:
	/* on error free sesinfo and tcon struct if needed */
	if (rc) {
		/* If find_unc succeeded then rc == 0 so we can not end */
		/* up accidentally freeing someone elses tcon struct */
		if (tcon)
			cifs_put_tcon(tcon);
		else if (pSesInfo)
			cifs_put_smb_ses(pSesInfo);
		else
			cifs_put_tcp_session(srvTcp);
		bdi_destroy(&cifs_sb->bdi);
		goto out;
	}

	/* volume_info->password is freed above when existing session found
	(in which case it is not needed anymore) but when new sesion is created
	the password ptr is put in the new session structure (in which case the
	password will be freed at unmount time) */
out:
	/* zero out password before freeing */
	FreeXid(xid);
	return rc;
}

/*
 * Issue a TREE_CONNECT request. Note that for IPC$ shares, that the tcon
 * pointer may be NULL.
 */
int
CIFSTCon(unsigned int xid, struct cifs_ses *ses,
	 const char *tree, struct cifs_tcon *tcon,
	 const struct nls_table *nls_codepage)
{
	struct smb_hdr *smb_buffer;
	struct smb_hdr *smb_buffer_response;
	TCONX_REQ *pSMB;
	TCONX_RSP *pSMBr;
	unsigned char *bcc_ptr;
	int rc = 0;
	int length;
	__u16 bytes_left, count;

	if (ses == NULL)
		return -EIO;

	smb_buffer = cifs_buf_get();
	if (smb_buffer == NULL)
		return -ENOMEM;

	smb_buffer_response = smb_buffer;

	header_assemble(smb_buffer, SMB_COM_TREE_CONNECT_ANDX,
			NULL /*no tid */ , 4 /*wct */ );

	smb_buffer->Mid = GetNextMid(ses->server);
	smb_buffer->Uid = ses->Suid;
	pSMB = (TCONX_REQ *) smb_buffer;
	pSMBr = (TCONX_RSP *) smb_buffer_response;

	pSMB->AndXCommand = 0xFF;
	pSMB->Flags = cpu_to_le16(TCON_EXTENDED_SECINFO);
	bcc_ptr = &pSMB->Password[0];
	if (!tcon || (ses->server->sec_mode & SECMODE_USER)) {
		pSMB->PasswordLength = cpu_to_le16(1);	/* minimum */
		*bcc_ptr = 0; /* password is null byte */
		bcc_ptr++;              /* skip password */
		/* already aligned so no need to do it below */
	} else {
		pSMB->PasswordLength = cpu_to_le16(CIFS_AUTH_RESP_SIZE);
		/* BB FIXME add code to fail this if NTLMv2 or Kerberos
		   specified as required (when that support is added to
		   the vfs in the future) as only NTLM or the much
		   weaker LANMAN (which we do not send by default) is accepted
		   by Samba (not sure whether other servers allow
		   NTLMv2 password here) */
#ifdef CONFIG_CIFS_WEAK_PW_HASH
		if ((global_secflags & CIFSSEC_MAY_LANMAN) &&
		    (ses->server->secType == LANMAN))
			calc_lanman_hash(tcon->password, ses->server->cryptkey,
					 ses->server->sec_mode &
					    SECMODE_PW_ENCRYPT ? true : false,
					 bcc_ptr);
		else
#endif /* CIFS_WEAK_PW_HASH */
		rc = SMBNTencrypt(tcon->password, ses->server->cryptkey,
					bcc_ptr);

		bcc_ptr += CIFS_AUTH_RESP_SIZE;
		if (ses->capabilities & CAP_UNICODE) {
			/* must align unicode strings */
			*bcc_ptr = 0; /* null byte password */
			bcc_ptr++;
		}
	}

	if (ses->server->sec_mode &
			(SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
		smb_buffer->Flags2 |= SMBFLG2_SECURITY_SIGNATURE;

	if (ses->capabilities & CAP_STATUS32) {
		smb_buffer->Flags2 |= SMBFLG2_ERR_STATUS;
	}
	if (ses->capabilities & CAP_DFS) {
		smb_buffer->Flags2 |= SMBFLG2_DFS;
	}
	if (ses->capabilities & CAP_UNICODE) {
		smb_buffer->Flags2 |= SMBFLG2_UNICODE;
		length =
		    cifs_strtoUCS((__le16 *) bcc_ptr, tree,
			6 /* max utf8 char length in bytes */ *
			(/* server len*/ + 256 /* share len */), nls_codepage);
		bcc_ptr += 2 * length;	/* convert num 16 bit words to bytes */
		bcc_ptr += 2;	/* skip trailing null */
	} else {		/* ASCII */
		strcpy(bcc_ptr, tree);
		bcc_ptr += strlen(tree) + 1;
	}
	strcpy(bcc_ptr, "?????");
	bcc_ptr += strlen("?????");
	bcc_ptr += 1;
	count = bcc_ptr - &pSMB->Password[0];
	pSMB->hdr.smb_buf_length = cpu_to_be32(be32_to_cpu(
					pSMB->hdr.smb_buf_length) + count);
	pSMB->ByteCount = cpu_to_le16(count);

	rc = SendReceive(xid, ses, smb_buffer, smb_buffer_response, &length,
			 0);

	/* above now done in SendReceive */
	if ((rc == 0) && (tcon != NULL)) {
		bool is_unicode;

		tcon->tidStatus = CifsGood;
		tcon->need_reconnect = false;
		tcon->tid = smb_buffer_response->Tid;
		bcc_ptr = pByteArea(smb_buffer_response);
		bytes_left = get_bcc(smb_buffer_response);
		length = strnlen(bcc_ptr, bytes_left - 2);
		if (smb_buffer->Flags2 & SMBFLG2_UNICODE)
			is_unicode = true;
		else
			is_unicode = false;


		/* skip service field (NB: this field is always ASCII) */
		if (length == 3) {
			if ((bcc_ptr[0] == 'I') && (bcc_ptr[1] == 'P') &&
			    (bcc_ptr[2] == 'C')) {
				cFYI(1, "IPC connection");
				tcon->ipc = 1;
			}
		} else if (length == 2) {
			if ((bcc_ptr[0] == 'A') && (bcc_ptr[1] == ':')) {
				/* the most common case */
				cFYI(1, "disk share connection");
			}
		}
		bcc_ptr += length + 1;
		bytes_left -= (length + 1);
		strncpy(tcon->treeName, tree, MAX_TREE_SIZE);

		/* mostly informational -- no need to fail on error here */
		kfree(tcon->nativeFileSystem);
		tcon->nativeFileSystem = cifs_strndup_from_ucs(bcc_ptr,
						      bytes_left, is_unicode,
						      nls_codepage);

		cFYI(1, "nativeFileSystem=%s", tcon->nativeFileSystem);

		if ((smb_buffer_response->WordCount == 3) ||
			 (smb_buffer_response->WordCount == 7))
			/* field is in same location */
			tcon->Flags = le16_to_cpu(pSMBr->OptionalSupport);
		else
			tcon->Flags = 0;
		cFYI(1, "Tcon flags: 0x%x ", tcon->Flags);
	} else if ((rc == 0) && tcon == NULL) {
		/* all we need to save for IPC$ connection */
		ses->ipc_tid = smb_buffer_response->Tid;
	}

	cifs_buf_release(smb_buffer);
	return rc;
}

void
cifs_umount(struct cifs_sb_info *cifs_sb)
{
	struct rb_root *root = &cifs_sb->tlink_tree;
	struct rb_node *node;
	struct tcon_link *tlink;

	cancel_delayed_work_sync(&cifs_sb->prune_tlinks);

	spin_lock(&cifs_sb->tlink_tree_lock);
	while ((node = rb_first(root))) {
		tlink = rb_entry(node, struct tcon_link, tl_rbnode);
		cifs_get_tlink(tlink);
		clear_bit(TCON_LINK_IN_TREE, &tlink->tl_flags);
		rb_erase(node, root);

		spin_unlock(&cifs_sb->tlink_tree_lock);
		cifs_put_tlink(tlink);
		spin_lock(&cifs_sb->tlink_tree_lock);
	}
	spin_unlock(&cifs_sb->tlink_tree_lock);

	bdi_destroy(&cifs_sb->bdi);
	kfree(cifs_sb->mountdata);
	unload_nls(cifs_sb->local_nls);
	kfree(cifs_sb);
}

int cifs_negotiate_protocol(unsigned int xid, struct cifs_ses *ses)
{
	int rc = 0;
	struct TCP_Server_Info *server = ses->server;

	/* only send once per connect */
	if (server->maxBuf != 0)
		return 0;

	rc = CIFSSMBNegotiate(xid, ses);
	if (rc == -EAGAIN) {
		/* retry only once on 1st time connection */
		rc = CIFSSMBNegotiate(xid, ses);
		if (rc == -EAGAIN)
			rc = -EHOSTDOWN;
	}
	if (rc == 0) {
		spin_lock(&GlobalMid_Lock);
		if (server->tcpStatus == CifsNeedNegotiate)
			server->tcpStatus = CifsGood;
		else
			rc = -EHOSTDOWN;
		spin_unlock(&GlobalMid_Lock);

	}

	return rc;
}


int cifs_setup_session(unsigned int xid, struct cifs_ses *ses,
			struct nls_table *nls_info)
{
	int rc = 0;
	struct TCP_Server_Info *server = ses->server;

	ses->flags = 0;
	ses->capabilities = server->capabilities;
	if (linuxExtEnabled == 0)
		ses->capabilities &= (~CAP_UNIX);

	cFYI(1, "Security Mode: 0x%x Capabilities: 0x%x TimeAdjust: %d",
		 server->sec_mode, server->capabilities, server->timeAdj);

	rc = CIFS_SessSetup(xid, ses, nls_info);
	if (rc) {
		cERROR(1, "Send error in SessSetup = %d", rc);
	} else {
		mutex_lock(&ses->server->srv_mutex);
		if (!server->session_estab) {
			server->session_key.response = ses->auth_key.response;
			server->session_key.len = ses->auth_key.len;
			server->sequence_number = 0x2;
			server->session_estab = true;
			ses->auth_key.response = NULL;
		}
		mutex_unlock(&server->srv_mutex);

		cFYI(1, "CIFS Session Established successfully");
		spin_lock(&GlobalMid_Lock);
		ses->status = CifsGood;
		ses->need_reconnect = false;
		spin_unlock(&GlobalMid_Lock);
	}

	kfree(ses->auth_key.response);
	ses->auth_key.response = NULL;
	ses->auth_key.len = 0;
	kfree(ses->ntlmssp);
	ses->ntlmssp = NULL;

	return rc;
}

static struct cifs_tcon *
cifs_construct_tcon(struct cifs_sb_info *cifs_sb, uid_t fsuid)
{
	struct cifs_tcon *master_tcon = cifs_sb_master_tcon(cifs_sb);
	struct cifs_ses *ses;
	struct cifs_tcon *tcon = NULL;
	struct smb_vol *vol_info;
	char username[28]; /* big enough for "krb50x" + hex of ULONG_MAX 6+16 */
			   /* We used to have this as MAX_USERNAME which is   */
			   /* way too big now (256 instead of 32) */

	vol_info = kzalloc(sizeof(*vol_info), GFP_KERNEL);
	if (vol_info == NULL) {
		tcon = ERR_PTR(-ENOMEM);
		goto out;
	}

	snprintf(username, sizeof(username), "krb50x%x", fsuid);
	vol_info->username = username;
	vol_info->local_nls = cifs_sb->local_nls;
	vol_info->linux_uid = fsuid;
	vol_info->cred_uid = fsuid;
	vol_info->UNC = master_tcon->treeName;
	vol_info->retry = master_tcon->retry;
	vol_info->nocase = master_tcon->nocase;
	vol_info->local_lease = master_tcon->local_lease;
	vol_info->no_linux_ext = !master_tcon->unix_ext;

	/* FIXME: allow for other secFlg settings */
	vol_info->secFlg = CIFSSEC_MUST_KRB5;

	/* get a reference for the same TCP session */
	spin_lock(&cifs_tcp_ses_lock);
	++master_tcon->ses->server->srv_count;
	spin_unlock(&cifs_tcp_ses_lock);

	ses = cifs_get_smb_ses(master_tcon->ses->server, vol_info);
	if (IS_ERR(ses)) {
		tcon = (struct cifs_tcon *)ses;
		cifs_put_tcp_session(master_tcon->ses->server);
		goto out;
	}

	tcon = cifs_get_tcon(ses, vol_info);
	if (IS_ERR(tcon)) {
		cifs_put_smb_ses(ses);
		goto out;
	}

	if (ses->capabilities & CAP_UNIX)
		reset_cifs_unix_caps(0, tcon, NULL, vol_info);
out:
	kfree(vol_info);

	return tcon;
}

struct cifs_tcon *
cifs_sb_master_tcon(struct cifs_sb_info *cifs_sb)
{
	return tlink_tcon(cifs_sb_master_tlink(cifs_sb));
}

static int
cifs_sb_tcon_pending_wait(void *unused)
{
	schedule();
	return signal_pending(current) ? -ERESTARTSYS : 0;
}

/* find and return a tlink with given uid */
static struct tcon_link *
tlink_rb_search(struct rb_root *root, uid_t uid)
{
	struct rb_node *node = root->rb_node;
	struct tcon_link *tlink;

	while (node) {
		tlink = rb_entry(node, struct tcon_link, tl_rbnode);

		if (tlink->tl_uid > uid)
			node = node->rb_left;
		else if (tlink->tl_uid < uid)
			node = node->rb_right;
		else
			return tlink;
	}
	return NULL;
}

/* insert a tcon_link into the tree */
static void
tlink_rb_insert(struct rb_root *root, struct tcon_link *new_tlink)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;
	struct tcon_link *tlink;

	while (*new) {
		tlink = rb_entry(*new, struct tcon_link, tl_rbnode);
		parent = *new;

		if (tlink->tl_uid > new_tlink->tl_uid)
			new = &((*new)->rb_left);
		else
			new = &((*new)->rb_right);
	}

	rb_link_node(&new_tlink->tl_rbnode, parent, new);
	rb_insert_color(&new_tlink->tl_rbnode, root);
}

/*
 * Find or construct an appropriate tcon given a cifs_sb and the fsuid of the
 * current task.
 *
 * If the superblock doesn't refer to a multiuser mount, then just return
 * the master tcon for the mount.
 *
 * First, search the rbtree for an existing tcon for this fsuid. If one
 * exists, then check to see if it's pending construction. If it is then wait
 * for construction to complete. Once it's no longer pending, check to see if
 * it failed and either return an error or retry construction, depending on
 * the timeout.
 *
 * If one doesn't exist then insert a new tcon_link struct into the tree and
 * try to construct a new one.
 */
struct tcon_link *
cifs_sb_tlink(struct cifs_sb_info *cifs_sb)
{
	int ret;
	uid_t fsuid = current_fsuid();
	struct tcon_link *tlink, *newtlink;

	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MULTIUSER))
		return cifs_get_tlink(cifs_sb_master_tlink(cifs_sb));

	spin_lock(&cifs_sb->tlink_tree_lock);
	tlink = tlink_rb_search(&cifs_sb->tlink_tree, fsuid);
	if (tlink)
		cifs_get_tlink(tlink);
	spin_unlock(&cifs_sb->tlink_tree_lock);

	if (tlink == NULL) {
		newtlink = kzalloc(sizeof(*tlink), GFP_KERNEL);
		if (newtlink == NULL)
			return ERR_PTR(-ENOMEM);
		newtlink->tl_uid = fsuid;
		newtlink->tl_tcon = ERR_PTR(-EACCES);
		set_bit(TCON_LINK_PENDING, &newtlink->tl_flags);
		set_bit(TCON_LINK_IN_TREE, &newtlink->tl_flags);
		cifs_get_tlink(newtlink);

		spin_lock(&cifs_sb->tlink_tree_lock);
		/* was one inserted after previous search? */
		tlink = tlink_rb_search(&cifs_sb->tlink_tree, fsuid);
		if (tlink) {
			cifs_get_tlink(tlink);
			spin_unlock(&cifs_sb->tlink_tree_lock);
			kfree(newtlink);
			goto wait_for_construction;
		}
		tlink = newtlink;
		tlink_rb_insert(&cifs_sb->tlink_tree, tlink);
		spin_unlock(&cifs_sb->tlink_tree_lock);
	} else {
wait_for_construction:
		ret = wait_on_bit(&tlink->tl_flags, TCON_LINK_PENDING,
				  cifs_sb_tcon_pending_wait,
				  TASK_INTERRUPTIBLE);
		if (ret) {
			cifs_put_tlink(tlink);
			return ERR_PTR(ret);
		}

		/* if it's good, return it */
		if (!IS_ERR(tlink->tl_tcon))
			return tlink;

		/* return error if we tried this already recently */
		if (time_before(jiffies, tlink->tl_time + TLINK_ERROR_EXPIRE)) {
			cifs_put_tlink(tlink);
			return ERR_PTR(-EACCES);
		}

		if (test_and_set_bit(TCON_LINK_PENDING, &tlink->tl_flags))
			goto wait_for_construction;
	}

	tlink->tl_tcon = cifs_construct_tcon(cifs_sb, fsuid);
	clear_bit(TCON_LINK_PENDING, &tlink->tl_flags);
	wake_up_bit(&tlink->tl_flags, TCON_LINK_PENDING);

	if (IS_ERR(tlink->tl_tcon)) {
		cifs_put_tlink(tlink);
		return ERR_PTR(-EACCES);
	}

	return tlink;
}

/*
 * periodic workqueue job that scans tcon_tree for a superblock and closes
 * out tcons.
 */
static void
cifs_prune_tlinks(struct work_struct *work)
{
	struct cifs_sb_info *cifs_sb = container_of(work, struct cifs_sb_info,
						    prune_tlinks.work);
	struct rb_root *root = &cifs_sb->tlink_tree;
	struct rb_node *node = rb_first(root);
	struct rb_node *tmp;
	struct tcon_link *tlink;

	/*
	 * Because we drop the spinlock in the loop in order to put the tlink
	 * it's not guarded against removal of links from the tree. The only
	 * places that remove entries from the tree are this function and
	 * umounts. Because this function is non-reentrant and is canceled
	 * before umount can proceed, this is safe.
	 */
	spin_lock(&cifs_sb->tlink_tree_lock);
	node = rb_first(root);
	while (node != NULL) {
		tmp = node;
		node = rb_next(tmp);
		tlink = rb_entry(tmp, struct tcon_link, tl_rbnode);

		if (test_bit(TCON_LINK_MASTER, &tlink->tl_flags) ||
		    atomic_read(&tlink->tl_count) != 0 ||
		    time_after(tlink->tl_time + TLINK_IDLE_EXPIRE, jiffies))
			continue;

		cifs_get_tlink(tlink);
		clear_bit(TCON_LINK_IN_TREE, &tlink->tl_flags);
		rb_erase(tmp, root);

		spin_unlock(&cifs_sb->tlink_tree_lock);
		cifs_put_tlink(tlink);
		spin_lock(&cifs_sb->tlink_tree_lock);
	}
	spin_unlock(&cifs_sb->tlink_tree_lock);

	queue_delayed_work(system_nrt_wq, &cifs_sb->prune_tlinks,
				TLINK_IDLE_EXPIRE);
}
