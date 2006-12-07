/*
 *   fs/cifs/transport.c
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2005
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *   Jeremy Allison (jra@samba.org) 2006.
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
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/net.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <asm/processor.h>
#include <linux/mempool.h>
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
  
extern mempool_t *cifs_mid_poolp;
extern kmem_cache_t *cifs_oplock_cachep;

static struct mid_q_entry *
AllocMidQEntry(const struct smb_hdr *smb_buffer, struct cifsSesInfo *ses)
{
	struct mid_q_entry *temp;

	if (ses == NULL) {
		cERROR(1, ("Null session passed in to AllocMidQEntry"));
		return NULL;
	}
	if (ses->server == NULL) {
		cERROR(1, ("Null TCP session in AllocMidQEntry"));
		return NULL;
	}
	
	temp = (struct mid_q_entry *) mempool_alloc(cifs_mid_poolp,
						    SLAB_KERNEL | GFP_NOFS);
	if (temp == NULL)
		return temp;
	else {
		memset(temp, 0, sizeof (struct mid_q_entry));
		temp->mid = smb_buffer->Mid;	/* always LE */
		temp->pid = current->pid;
		temp->command = smb_buffer->Command;
		cFYI(1, ("For smb_command %d", temp->command));
	/*	do_gettimeofday(&temp->when_sent);*/ /* easier to use jiffies */
		/* when mid allocated can be before when sent */
		temp->when_alloc = jiffies;
		temp->ses = ses;
		temp->tsk = current;
	}

	spin_lock(&GlobalMid_Lock);
	list_add_tail(&temp->qhead, &ses->server->pending_mid_q);
	atomic_inc(&midCount);
	temp->midState = MID_REQUEST_ALLOCATED;
	spin_unlock(&GlobalMid_Lock);
	return temp;
}

static void
DeleteMidQEntry(struct mid_q_entry *midEntry)
{
#ifdef CONFIG_CIFS_STATS2
	unsigned long now;
#endif
	spin_lock(&GlobalMid_Lock);
	midEntry->midState = MID_FREE;
	list_del(&midEntry->qhead);
	atomic_dec(&midCount);
	spin_unlock(&GlobalMid_Lock);
	if(midEntry->largeBuf)
		cifs_buf_release(midEntry->resp_buf);
	else
		cifs_small_buf_release(midEntry->resp_buf);
#ifdef CONFIG_CIFS_STATS2
	now = jiffies;
	/* commands taking longer than one second are indications that
	   something is wrong, unless it is quite a slow link or server */
	if((now - midEntry->when_alloc) > HZ) {
		if((cifsFYI & CIFS_TIMER) && 
		   (midEntry->command != SMB_COM_LOCKING_ANDX)) {
			printk(KERN_DEBUG " CIFS slow rsp: cmd %d mid %d",
			       midEntry->command, midEntry->mid);
			printk(" A: 0x%lx S: 0x%lx R: 0x%lx\n",
			       now - midEntry->when_alloc,
			       now - midEntry->when_sent,
			       now - midEntry->when_received);
		}
	}
#endif
	mempool_free(midEntry, cifs_mid_poolp);
}

struct oplock_q_entry *
AllocOplockQEntry(struct inode * pinode, __u16 fid, struct cifsTconInfo * tcon)
{
	struct oplock_q_entry *temp;
	if ((pinode== NULL) || (tcon == NULL)) {
		cERROR(1, ("Null parms passed to AllocOplockQEntry"));
		return NULL;
	}
	temp = (struct oplock_q_entry *) kmem_cache_alloc(cifs_oplock_cachep,
						       SLAB_KERNEL);
	if (temp == NULL)
		return temp;
	else {
		temp->pinode = pinode;
		temp->tcon = tcon;
		temp->netfid = fid;
		spin_lock(&GlobalMid_Lock);
		list_add_tail(&temp->qhead, &GlobalOplock_Q);
		spin_unlock(&GlobalMid_Lock);
	}
	return temp;

}

void DeleteOplockQEntry(struct oplock_q_entry * oplockEntry)
{
	spin_lock(&GlobalMid_Lock); 
    /* should we check if list empty first? */
	list_del(&oplockEntry->qhead);
	spin_unlock(&GlobalMid_Lock);
	kmem_cache_free(cifs_oplock_cachep, oplockEntry);
}

int
smb_send(struct socket *ssocket, struct smb_hdr *smb_buffer,
	 unsigned int smb_buf_length, struct sockaddr *sin)
{
	int rc = 0;
	int i = 0;
	struct msghdr smb_msg;
	struct kvec iov;
	unsigned len = smb_buf_length + 4;

	if(ssocket == NULL)
		return -ENOTSOCK; /* BB eventually add reconnect code here */
	iov.iov_base = smb_buffer;
	iov.iov_len = len;

	smb_msg.msg_name = sin;
	smb_msg.msg_namelen = sizeof (struct sockaddr);
	smb_msg.msg_control = NULL;
	smb_msg.msg_controllen = 0;
	smb_msg.msg_flags = MSG_DONTWAIT + MSG_NOSIGNAL; /* BB add more flags?*/

	/* smb header is converted in header_assemble. bcc and rest of SMB word
	   area, and byte area if necessary, is converted to littleendian in 
	   cifssmb.c and RFC1001 len is converted to bigendian in smb_send 
	   Flags2 is converted in SendReceive */

	smb_buffer->smb_buf_length = cpu_to_be32(smb_buffer->smb_buf_length);
	cFYI(1, ("Sending smb of length %d", smb_buf_length));
	dump_smb(smb_buffer, len);

	while (len > 0) {
		rc = kernel_sendmsg(ssocket, &smb_msg, &iov, 1, len);
		if ((rc == -ENOSPC) || (rc == -EAGAIN)) {
			i++;
		/* smaller timeout here than send2 since smaller size */
		/* Although it may not be required, this also is smaller 
		   oplock break time */  
			if(i > 12) {
				cERROR(1,
				   ("sends on sock %p stuck for 7 seconds",
				    ssocket));
				rc = -EAGAIN;
				break;
			}
			msleep(1 << i);
			continue;
		}
		if (rc < 0) 
			break;
		else
			i = 0; /* reset i after each successful send */
		iov.iov_base += rc;
		iov.iov_len -= rc;
		len -= rc;
	}

	if (rc < 0) {
		cERROR(1,("Error %d sending data on socket to server", rc));
	} else {
		rc = 0;
	}

	/* Don't want to modify the buffer as a
	   side effect of this call. */
	smb_buffer->smb_buf_length = smb_buf_length;

	return rc;
}

static int
smb_send2(struct socket *ssocket, struct kvec *iov, int n_vec,
	  struct sockaddr *sin)
{
	int rc = 0;
	int i = 0;
	struct msghdr smb_msg;
	struct smb_hdr *smb_buffer = iov[0].iov_base;
	unsigned int len = iov[0].iov_len;
	unsigned int total_len;
	int first_vec = 0;
	unsigned int smb_buf_length = smb_buffer->smb_buf_length;
	
	if(ssocket == NULL)
		return -ENOTSOCK; /* BB eventually add reconnect code here */

	smb_msg.msg_name = sin;
	smb_msg.msg_namelen = sizeof (struct sockaddr);
	smb_msg.msg_control = NULL;
	smb_msg.msg_controllen = 0;
	smb_msg.msg_flags = MSG_DONTWAIT + MSG_NOSIGNAL; /* BB add more flags?*/

	/* smb header is converted in header_assemble. bcc and rest of SMB word
	   area, and byte area if necessary, is converted to littleendian in 
	   cifssmb.c and RFC1001 len is converted to bigendian in smb_send 
	   Flags2 is converted in SendReceive */


	total_len = 0;
	for (i = 0; i < n_vec; i++)
		total_len += iov[i].iov_len;

	smb_buffer->smb_buf_length = cpu_to_be32(smb_buffer->smb_buf_length);
	cFYI(1, ("Sending smb:  total_len %d", total_len));
	dump_smb(smb_buffer, len);

	while (total_len) {
		rc = kernel_sendmsg(ssocket, &smb_msg, &iov[first_vec],
				    n_vec - first_vec, total_len);
		if ((rc == -ENOSPC) || (rc == -EAGAIN)) {
			i++;
			if(i >= 14) {
				cERROR(1,
				   ("sends on sock %p stuck for 15 seconds",
				    ssocket));
				rc = -EAGAIN;
				break;
			}
			msleep(1 << i);
			continue;
		}
		if (rc < 0) 
			break;

		if (rc >= total_len) {
			WARN_ON(rc > total_len);
			break;
		}
		if(rc == 0) {
			/* should never happen, letting socket clear before
			   retrying is our only obvious option here */
			cERROR(1,("tcp sent no data"));
			msleep(500);
			continue;
		}
		total_len -= rc;
		/* the line below resets i */
		for (i = first_vec; i < n_vec; i++) {
			if (iov[i].iov_len) {
				if (rc > iov[i].iov_len) {
					rc -= iov[i].iov_len;
					iov[i].iov_len = 0;
				} else {
					iov[i].iov_base += rc;
					iov[i].iov_len -= rc;
					first_vec = i;
					break;
				}
			}
		}
		i = 0; /* in case we get ENOSPC on the next send */
	}

	if (rc < 0) {
		cERROR(1,("Error %d sending data on socket to server", rc));
	} else
		rc = 0;

	/* Don't want to modify the buffer as a
	   side effect of this call. */
	smb_buffer->smb_buf_length = smb_buf_length;

	return rc;
}

static int wait_for_free_request(struct cifsSesInfo *ses, const int long_op)
{
	if(long_op == -1) {
		/* oplock breaks must not be held up */
		atomic_inc(&ses->server->inFlight);
	} else {
		spin_lock(&GlobalMid_Lock); 
		while(1) {        
			if(atomic_read(&ses->server->inFlight) >= 
					cifs_max_pending){
				spin_unlock(&GlobalMid_Lock);
#ifdef CONFIG_CIFS_STATS2
				atomic_inc(&ses->server->num_waiters);
#endif
				wait_event(ses->server->request_q,
					atomic_read(&ses->server->inFlight)
					 < cifs_max_pending);
#ifdef CONFIG_CIFS_STATS2
				atomic_dec(&ses->server->num_waiters);
#endif
				spin_lock(&GlobalMid_Lock);
			} else {
				if(ses->server->tcpStatus == CifsExiting) {
					spin_unlock(&GlobalMid_Lock);
					return -ENOENT;
				}

				/* can not count locking commands against total since
				   they are allowed to block on server */
					
				/* update # of requests on the wire to server */
				if (long_op < 3)
					atomic_inc(&ses->server->inFlight);
				spin_unlock(&GlobalMid_Lock);
				break;
			}
		}
	}
	return 0;
}

static int allocate_mid(struct cifsSesInfo *ses, struct smb_hdr *in_buf,
			struct mid_q_entry **ppmidQ)
{
	if (ses->server->tcpStatus == CifsExiting) {
		return -ENOENT;
	} else if (ses->server->tcpStatus == CifsNeedReconnect) {
		cFYI(1,("tcp session dead - return to caller to retry"));
		return -EAGAIN;
	} else if (ses->status != CifsGood) {
		/* check if SMB session is bad because we are setting it up */
		if((in_buf->Command != SMB_COM_SESSION_SETUP_ANDX) && 
			(in_buf->Command != SMB_COM_NEGOTIATE)) {
			return -EAGAIN;
		} /* else ok - we are setting up session */
	}
	*ppmidQ = AllocMidQEntry(in_buf, ses);
	if (*ppmidQ == NULL) {
		return -ENOMEM;
	}
	return 0;
}

static int wait_for_response(struct cifsSesInfo *ses, 
			struct mid_q_entry *midQ,
			unsigned long timeout,
			unsigned long time_to_wait)
{
	unsigned long curr_timeout;

	for (;;) {
		curr_timeout = timeout + jiffies;
		wait_event(ses->server->response_q,
			(!(midQ->midState == MID_REQUEST_SUBMITTED)) || 
			time_after(jiffies, curr_timeout) || 
			((ses->server->tcpStatus != CifsGood) &&
			 (ses->server->tcpStatus != CifsNew)));

		if (time_after(jiffies, curr_timeout) &&
			(midQ->midState == MID_REQUEST_SUBMITTED) &&
			((ses->server->tcpStatus == CifsGood) ||
			 (ses->server->tcpStatus == CifsNew))) {

			unsigned long lrt;

			/* We timed out. Is the server still
			   sending replies ? */
			spin_lock(&GlobalMid_Lock);
			lrt = ses->server->lstrp;
			spin_unlock(&GlobalMid_Lock);

			/* Calculate time_to_wait past last receive time.
			 Although we prefer not to time out if the 
			 server is still responding - we will time
			 out if the server takes more than 15 (or 45 
			 or 180) seconds to respond to this request
			 and has not responded to any request from 
			 other threads on the client within 10 seconds */
			lrt += time_to_wait;
			if (time_after(jiffies, lrt)) {
				/* No replies for time_to_wait. */
				cERROR(1,("server not responding"));
				return -1;
			}
		} else {
			return 0;
		}
	}
}

int
SendReceive2(const unsigned int xid, struct cifsSesInfo *ses, 
	     struct kvec *iov, int n_vec, int * pRespBufType /* ret */, 
	     const int long_op)
{
	int rc = 0;
	unsigned int receive_len;
	unsigned long timeout;
	struct mid_q_entry *midQ;
	struct smb_hdr *in_buf = iov[0].iov_base;
	
	*pRespBufType = CIFS_NO_BUFFER;  /* no response buf yet */

	if ((ses == NULL) || (ses->server == NULL)) {
		cifs_small_buf_release(in_buf);
		cERROR(1,("Null session"));
		return -EIO;
	}

	if(ses->server->tcpStatus == CifsExiting) {
		cifs_small_buf_release(in_buf);
		return -ENOENT;
	}

	/* Ensure that we do not send more than 50 overlapping requests 
	   to the same server. We may make this configurable later or
	   use ses->maxReq */

	rc = wait_for_free_request(ses, long_op);
	if (rc) {
		cifs_small_buf_release(in_buf);
		return rc;
	}

	/* make sure that we sign in the same order that we send on this socket 
	   and avoid races inside tcp sendmsg code that could cause corruption
	   of smb data */

	down(&ses->server->tcpSem); 

	rc = allocate_mid(ses, in_buf, &midQ);
	if (rc) {
		up(&ses->server->tcpSem);
		cifs_small_buf_release(in_buf);
		/* Update # of requests on wire to server */
		atomic_dec(&ses->server->inFlight); 
		wake_up(&ses->server->request_q);
		return rc;
	}

 	rc = cifs_sign_smb2(iov, n_vec, ses->server, &midQ->sequence_number);

	midQ->midState = MID_REQUEST_SUBMITTED;
#ifdef CONFIG_CIFS_STATS2
	atomic_inc(&ses->server->inSend);
#endif
	rc = smb_send2(ses->server->ssocket, iov, n_vec,
		      (struct sockaddr *) &(ses->server->addr.sockAddr));
#ifdef CONFIG_CIFS_STATS2
	atomic_dec(&ses->server->inSend);
	midQ->when_sent = jiffies;
#endif

	up(&ses->server->tcpSem);
	cifs_small_buf_release(in_buf);

	if(rc < 0)
		goto out;

	if (long_op == -1)
		goto out;
	else if (long_op == 2) /* writes past end of file can take loong time */
		timeout = 180 * HZ;
	else if (long_op == 1)
		timeout = 45 * HZ; /* should be greater than 
			servers oplock break timeout (about 43 seconds) */
	else
		timeout = 15 * HZ;

	/* wait for 15 seconds or until woken up due to response arriving or 
	   due to last connection to this server being unmounted */
	if (signal_pending(current)) {
		/* if signal pending do not hold up user for full smb timeout
		but we still give response a change to complete */
		timeout = 2 * HZ;
	}   

	/* No user interrupts in wait - wreaks havoc with performance */
	wait_for_response(ses, midQ, timeout, 10 * HZ);

	spin_lock(&GlobalMid_Lock);
	if (midQ->resp_buf) {
		spin_unlock(&GlobalMid_Lock);
		receive_len = midQ->resp_buf->smb_buf_length;
	} else {
		cERROR(1,("No response to cmd %d mid %d",
			midQ->command, midQ->mid));
		if(midQ->midState == MID_REQUEST_SUBMITTED) {
			if(ses->server->tcpStatus == CifsExiting)
				rc = -EHOSTDOWN;
			else {
				ses->server->tcpStatus = CifsNeedReconnect;
				midQ->midState = MID_RETRY_NEEDED;
			}
		}

		if (rc != -EHOSTDOWN) {
			if(midQ->midState == MID_RETRY_NEEDED) {
				rc = -EAGAIN;
				cFYI(1,("marking request for retry"));
			} else {
				rc = -EIO;
			}
		}
		spin_unlock(&GlobalMid_Lock);
		DeleteMidQEntry(midQ);
		/* Update # of requests on wire to server */
		atomic_dec(&ses->server->inFlight); 
		wake_up(&ses->server->request_q);
		return rc;
	}
  
	if (receive_len > CIFSMaxBufSize + MAX_CIFS_HDR_SIZE) {
		cERROR(1, ("Frame too large received.  Length: %d  Xid: %d",
			receive_len, xid));
		rc = -EIO;
	} else {		/* rcvd frame is ok */
		if (midQ->resp_buf && 
			(midQ->midState == MID_RESPONSE_RECEIVED)) {

			iov[0].iov_base = (char *)midQ->resp_buf;
			if(midQ->largeBuf)
				*pRespBufType = CIFS_LARGE_BUFFER;
			else
				*pRespBufType = CIFS_SMALL_BUFFER;
			iov[0].iov_len = receive_len + 4;

			dump_smb(midQ->resp_buf, 80);
			/* convert the length into a more usable form */
			if((receive_len > 24) &&
			   (ses->server->secMode & (SECMODE_SIGN_REQUIRED |
					SECMODE_SIGN_ENABLED))) {
				rc = cifs_verify_signature(midQ->resp_buf,
						ses->server->mac_signing_key,
						midQ->sequence_number+1);
				if(rc) {
					cERROR(1,("Unexpected SMB signature"));
					/* BB FIXME add code to kill session */
				}
			}

			/* BB special case reconnect tid and uid here? */
			/* BB special case Errbadpassword and pwdexpired here */
			rc = map_smb_to_linux_error(midQ->resp_buf);

			/* convert ByteCount if necessary */
			if (receive_len >=
			    sizeof (struct smb_hdr) -
			    4 /* do not count RFC1001 header */  +
			    (2 * midQ->resp_buf->WordCount) + 2 /* bcc */ )
				BCC(midQ->resp_buf) = 
					le16_to_cpu(BCC_LE(midQ->resp_buf));
			midQ->resp_buf = NULL;  /* mark it so will not be freed
						by DeleteMidQEntry */
		} else {
			rc = -EIO;
			cFYI(1,("Bad MID state?"));
		}
	}

out:

	DeleteMidQEntry(midQ);
	atomic_dec(&ses->server->inFlight); 
	wake_up(&ses->server->request_q);

	return rc;
}

int
SendReceive(const unsigned int xid, struct cifsSesInfo *ses,
	    struct smb_hdr *in_buf, struct smb_hdr *out_buf,
	    int *pbytes_returned, const int long_op)
{
	int rc = 0;
	unsigned int receive_len;
	unsigned long timeout;
	struct mid_q_entry *midQ;

	if (ses == NULL) {
		cERROR(1,("Null smb session"));
		return -EIO;
	}
	if(ses->server == NULL) {
		cERROR(1,("Null tcp session"));
		return -EIO;
	}

	if(ses->server->tcpStatus == CifsExiting)
		return -ENOENT;

	/* Ensure that we do not send more than 50 overlapping requests 
	   to the same server. We may make this configurable later or
	   use ses->maxReq */

	rc = wait_for_free_request(ses, long_op);
	if (rc)
		return rc;

	/* make sure that we sign in the same order that we send on this socket 
	   and avoid races inside tcp sendmsg code that could cause corruption
	   of smb data */

	down(&ses->server->tcpSem); 

	rc = allocate_mid(ses, in_buf, &midQ);
	if (rc) {
		up(&ses->server->tcpSem);
		/* Update # of requests on wire to server */
		atomic_dec(&ses->server->inFlight); 
		wake_up(&ses->server->request_q);
		return rc;
	}

	if (in_buf->smb_buf_length > CIFSMaxBufSize + MAX_CIFS_HDR_SIZE - 4) {
		cERROR(1, ("Illegal length, greater than maximum frame, %d",
			in_buf->smb_buf_length));
		DeleteMidQEntry(midQ);
		up(&ses->server->tcpSem);
		/* Update # of requests on wire to server */
		atomic_dec(&ses->server->inFlight); 
		wake_up(&ses->server->request_q);
		return -EIO;
	}

	rc = cifs_sign_smb(in_buf, ses->server, &midQ->sequence_number);

	midQ->midState = MID_REQUEST_SUBMITTED;
#ifdef CONFIG_CIFS_STATS2
	atomic_inc(&ses->server->inSend);
#endif
	rc = smb_send(ses->server->ssocket, in_buf, in_buf->smb_buf_length,
		      (struct sockaddr *) &(ses->server->addr.sockAddr));
#ifdef CONFIG_CIFS_STATS2
	atomic_dec(&ses->server->inSend);
	midQ->when_sent = jiffies;
#endif
	up(&ses->server->tcpSem);

	if(rc < 0)
		goto out;

	if (long_op == -1)
		goto out;
	else if (long_op == 2) /* writes past end of file can take loong time */
		timeout = 180 * HZ;
	else if (long_op == 1)
		timeout = 45 * HZ; /* should be greater than 
			servers oplock break timeout (about 43 seconds) */
	else
		timeout = 15 * HZ;
	/* wait for 15 seconds or until woken up due to response arriving or 
	   due to last connection to this server being unmounted */
	if (signal_pending(current)) {
		/* if signal pending do not hold up user for full smb timeout
		but we still give response a change to complete */
		timeout = 2 * HZ;
	}   

	/* No user interrupts in wait - wreaks havoc with performance */
	wait_for_response(ses, midQ, timeout, 10 * HZ);

	spin_lock(&GlobalMid_Lock);
	if (midQ->resp_buf) {
		spin_unlock(&GlobalMid_Lock);
		receive_len = midQ->resp_buf->smb_buf_length;
	} else {
		cERROR(1,("No response for cmd %d mid %d",
			  midQ->command, midQ->mid));
		if(midQ->midState == MID_REQUEST_SUBMITTED) {
			if(ses->server->tcpStatus == CifsExiting)
				rc = -EHOSTDOWN;
			else {
				ses->server->tcpStatus = CifsNeedReconnect;
				midQ->midState = MID_RETRY_NEEDED;
			}
		}

		if (rc != -EHOSTDOWN) {
			if(midQ->midState == MID_RETRY_NEEDED) {
				rc = -EAGAIN;
				cFYI(1,("marking request for retry"));
			} else {
				rc = -EIO;
			}
		}
		spin_unlock(&GlobalMid_Lock);
		DeleteMidQEntry(midQ);
		/* Update # of requests on wire to server */
		atomic_dec(&ses->server->inFlight); 
		wake_up(&ses->server->request_q);
		return rc;
	}
  
	if (receive_len > CIFSMaxBufSize + MAX_CIFS_HDR_SIZE) {
		cERROR(1, ("Frame too large received.  Length: %d  Xid: %d",
			receive_len, xid));
		rc = -EIO;
	} else {		/* rcvd frame is ok */

		if (midQ->resp_buf && out_buf
		    && (midQ->midState == MID_RESPONSE_RECEIVED)) {
			out_buf->smb_buf_length = receive_len;
			memcpy((char *)out_buf + 4,
			       (char *)midQ->resp_buf + 4,
			       receive_len);

			dump_smb(out_buf, 92);
			/* convert the length into a more usable form */
			if((receive_len > 24) &&
			   (ses->server->secMode & (SECMODE_SIGN_REQUIRED |
					SECMODE_SIGN_ENABLED))) {
				rc = cifs_verify_signature(out_buf,
						ses->server->mac_signing_key,
						midQ->sequence_number+1);
				if(rc) {
					cERROR(1,("Unexpected SMB signature"));
					/* BB FIXME add code to kill session */
				}
			}

			*pbytes_returned = out_buf->smb_buf_length;

			/* BB special case reconnect tid and uid here? */
			rc = map_smb_to_linux_error(out_buf);

			/* convert ByteCount if necessary */
			if (receive_len >=
			    sizeof (struct smb_hdr) -
			    4 /* do not count RFC1001 header */  +
			    (2 * out_buf->WordCount) + 2 /* bcc */ )
				BCC(out_buf) = le16_to_cpu(BCC_LE(out_buf));
		} else {
			rc = -EIO;
			cERROR(1,("Bad MID state?"));
		}
	}

out:

	DeleteMidQEntry(midQ);
	atomic_dec(&ses->server->inFlight); 
	wake_up(&ses->server->request_q);

	return rc;
}

/* Send an NT_CANCEL SMB to cause the POSIX blocking lock to return. */

static int
send_nt_cancel(struct cifsTconInfo *tcon, struct smb_hdr *in_buf,
		struct mid_q_entry *midQ)
{
	int rc = 0;
	struct cifsSesInfo *ses = tcon->ses;
	__u16 mid = in_buf->Mid;

	header_assemble(in_buf, SMB_COM_NT_CANCEL, tcon, 0);
	in_buf->Mid = mid;
	down(&ses->server->tcpSem); 
	rc = cifs_sign_smb(in_buf, ses->server, &midQ->sequence_number);
	if (rc) {
		up(&ses->server->tcpSem);
		return rc;
	}
	rc = smb_send(ses->server->ssocket, in_buf, in_buf->smb_buf_length,
	      (struct sockaddr *) &(ses->server->addr.sockAddr));
	up(&ses->server->tcpSem);
	return rc;
}

/* We send a LOCKINGX_CANCEL_LOCK to cause the Windows
   blocking lock to return. */

static int
send_lock_cancel(const unsigned int xid, struct cifsTconInfo *tcon,
			struct smb_hdr *in_buf,
			struct smb_hdr *out_buf)
{
	int bytes_returned;
	struct cifsSesInfo *ses = tcon->ses;
	LOCK_REQ *pSMB = (LOCK_REQ *)in_buf;

	/* We just modify the current in_buf to change
	   the type of lock from LOCKING_ANDX_SHARED_LOCK
	   or LOCKING_ANDX_EXCLUSIVE_LOCK to
	   LOCKING_ANDX_CANCEL_LOCK. */

	pSMB->LockType = LOCKING_ANDX_CANCEL_LOCK|LOCKING_ANDX_LARGE_FILES;
	pSMB->Timeout = 0;
	pSMB->hdr.Mid = GetNextMid(ses->server);

	return SendReceive(xid, ses, in_buf, out_buf,
			&bytes_returned, 0);
}

int
SendReceiveBlockingLock(const unsigned int xid, struct cifsTconInfo *tcon,
	    struct smb_hdr *in_buf, struct smb_hdr *out_buf,
	    int *pbytes_returned)
{
	int rc = 0;
	int rstart = 0;
	unsigned int receive_len;
	struct mid_q_entry *midQ;
	struct cifsSesInfo *ses;

	if (tcon == NULL || tcon->ses == NULL) {
		cERROR(1,("Null smb session"));
		return -EIO;
	}
	ses = tcon->ses;

	if(ses->server == NULL) {
		cERROR(1,("Null tcp session"));
		return -EIO;
	}

	if(ses->server->tcpStatus == CifsExiting)
		return -ENOENT;

	/* Ensure that we do not send more than 50 overlapping requests 
	   to the same server. We may make this configurable later or
	   use ses->maxReq */

	rc = wait_for_free_request(ses, 3);
	if (rc)
		return rc;

	/* make sure that we sign in the same order that we send on this socket 
	   and avoid races inside tcp sendmsg code that could cause corruption
	   of smb data */

	down(&ses->server->tcpSem); 

	rc = allocate_mid(ses, in_buf, &midQ);
	if (rc) {
		up(&ses->server->tcpSem);
		return rc;
	}

	if (in_buf->smb_buf_length > CIFSMaxBufSize + MAX_CIFS_HDR_SIZE - 4) {
		up(&ses->server->tcpSem);
		cERROR(1, ("Illegal length, greater than maximum frame, %d",
			in_buf->smb_buf_length));
		DeleteMidQEntry(midQ);
		return -EIO;
	}

	rc = cifs_sign_smb(in_buf, ses->server, &midQ->sequence_number);

	midQ->midState = MID_REQUEST_SUBMITTED;
#ifdef CONFIG_CIFS_STATS2
	atomic_inc(&ses->server->inSend);
#endif
	rc = smb_send(ses->server->ssocket, in_buf, in_buf->smb_buf_length,
		      (struct sockaddr *) &(ses->server->addr.sockAddr));
#ifdef CONFIG_CIFS_STATS2
	atomic_dec(&ses->server->inSend);
	midQ->when_sent = jiffies;
#endif
	up(&ses->server->tcpSem);

	if(rc < 0) {
		DeleteMidQEntry(midQ);
		return rc;
	}

	/* Wait for a reply - allow signals to interrupt. */
	rc = wait_event_interruptible(ses->server->response_q,
		(!(midQ->midState == MID_REQUEST_SUBMITTED)) || 
		((ses->server->tcpStatus != CifsGood) &&
		 (ses->server->tcpStatus != CifsNew)));

	/* Were we interrupted by a signal ? */
	if ((rc == -ERESTARTSYS) &&
		(midQ->midState == MID_REQUEST_SUBMITTED) &&
		((ses->server->tcpStatus == CifsGood) ||
		 (ses->server->tcpStatus == CifsNew))) {

		if (in_buf->Command == SMB_COM_TRANSACTION2) {
			/* POSIX lock. We send a NT_CANCEL SMB to cause the
			   blocking lock to return. */

			rc = send_nt_cancel(tcon, in_buf, midQ);
			if (rc) {
				DeleteMidQEntry(midQ);
				return rc;
			}
		} else {
			/* Windows lock. We send a LOCKINGX_CANCEL_LOCK
			   to cause the blocking lock to return. */

			rc = send_lock_cancel(xid, tcon, in_buf, out_buf);

			/* If we get -ENOLCK back the lock may have
			   already been removed. Don't exit in this case. */
			if (rc && rc != -ENOLCK) {
				DeleteMidQEntry(midQ);
				return rc;
			}
		}

		/* Wait 5 seconds for the response. */
		if (wait_for_response(ses, midQ, 5 * HZ, 5 * HZ)==0) {
			/* We got the response - restart system call. */
			rstart = 1;
		}
	}

	spin_lock(&GlobalMid_Lock);
	if (midQ->resp_buf) {
		spin_unlock(&GlobalMid_Lock);
		receive_len = midQ->resp_buf->smb_buf_length;
	} else {
		cERROR(1,("No response for cmd %d mid %d",
			  midQ->command, midQ->mid));
		if(midQ->midState == MID_REQUEST_SUBMITTED) {
			if(ses->server->tcpStatus == CifsExiting)
				rc = -EHOSTDOWN;
			else {
				ses->server->tcpStatus = CifsNeedReconnect;
				midQ->midState = MID_RETRY_NEEDED;
			}
		}

		if (rc != -EHOSTDOWN) {
			if(midQ->midState == MID_RETRY_NEEDED) {
				rc = -EAGAIN;
				cFYI(1,("marking request for retry"));
			} else {
				rc = -EIO;
			}
		}
		spin_unlock(&GlobalMid_Lock);
		DeleteMidQEntry(midQ);
		return rc;
	}
  
	if (receive_len > CIFSMaxBufSize + MAX_CIFS_HDR_SIZE) {
		cERROR(1, ("Frame too large received.  Length: %d  Xid: %d",
			receive_len, xid));
		rc = -EIO;
	} else {		/* rcvd frame is ok */

		if (midQ->resp_buf && out_buf
		    && (midQ->midState == MID_RESPONSE_RECEIVED)) {
			out_buf->smb_buf_length = receive_len;
			memcpy((char *)out_buf + 4,
			       (char *)midQ->resp_buf + 4,
			       receive_len);

			dump_smb(out_buf, 92);
			/* convert the length into a more usable form */
			if((receive_len > 24) &&
			   (ses->server->secMode & (SECMODE_SIGN_REQUIRED |
					SECMODE_SIGN_ENABLED))) {
				rc = cifs_verify_signature(out_buf,
						ses->server->mac_signing_key,
						midQ->sequence_number+1);
				if(rc) {
					cERROR(1,("Unexpected SMB signature"));
					/* BB FIXME add code to kill session */
				}
			}

			*pbytes_returned = out_buf->smb_buf_length;

			/* BB special case reconnect tid and uid here? */
			rc = map_smb_to_linux_error(out_buf);

			/* convert ByteCount if necessary */
			if (receive_len >=
			    sizeof (struct smb_hdr) -
			    4 /* do not count RFC1001 header */  +
			    (2 * out_buf->WordCount) + 2 /* bcc */ )
				BCC(out_buf) = le16_to_cpu(BCC_LE(out_buf));
		} else {
			rc = -EIO;
			cERROR(1,("Bad MID state?"));
		}
	}
	DeleteMidQEntry(midQ);
	if (rstart && rc == -EACCES)
		return -ERESTARTSYS;
	return rc;
}
