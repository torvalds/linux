/*
 *   fs/cifs/connect.c
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2008
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
#include <linux/pagemap.h>
#include <linux/ctype.h>
#include <linux/utsname.h>
#include <linux/mempool.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/pagevec.h>
#include <linux/freezer.h>
#include <asm/uaccess.h>
#include <asm/processor.h>
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
#include "cn_cifs.h"

#define CIFS_PORT 445
#define RFC1001_PORT 139

extern void SMBNTencrypt(unsigned char *passwd, unsigned char *c8,
			 unsigned char *p24);

extern mempool_t *cifs_req_poolp;

struct smb_vol {
	char *username;
	char *password;
	char *domainname;
	char *UNC;
	char *UNCip;
	char *in6_addr;   /* ipv6 address as human readable form of in6_addr */
	char *iocharset;  /* local code page for mapping to and from Unicode */
	char source_rfc1001_name[16]; /* netbios name of client */
	char target_rfc1001_name[16]; /* netbios name of server for Win9x/ME */
	uid_t linux_uid;
	gid_t linux_gid;
	mode_t file_mode;
	mode_t dir_mode;
	unsigned secFlg;
	bool rw:1;
	bool retry:1;
	bool intr:1;
	bool setuids:1;
	bool override_uid:1;
	bool override_gid:1;
	bool dynperm:1;
	bool noperm:1;
	bool no_psx_acl:1; /* set if posix acl support should be disabled */
	bool cifs_acl:1;
	bool no_xattr:1;   /* set if xattr (EA) support should be disabled*/
	bool server_ino:1; /* use inode numbers from server ie UniqueId */
	bool direct_io:1;
	bool remap:1;      /* set to remap seven reserved chars in filenames */
	bool posix_paths:1; /* unset to not ask for posix pathnames. */
	bool no_linux_ext:1;
	bool sfu_emul:1;
	bool nullauth:1;   /* attempt to authenticate with null user */
	bool nocase:1;     /* request case insensitive filenames */
	bool nobrl:1;      /* disable sending byte range locks to srv */
	bool mand_lock:1;  /* send mandatory not posix byte range lock reqs */
	bool seal:1;       /* request transport encryption on share */
	bool nodfs:1;      /* Do not request DFS, even if available */
	bool local_lease:1; /* check leases only on local system, not remote */
	bool noblocksnd:1;
	bool noautotune:1;
	bool nostrictsync:1; /* do not force expensive SMBflush on every sync */
	unsigned int rsize;
	unsigned int wsize;
	unsigned int sockopt;
	unsigned short int port;
	char *prepath;
};

static int ipv4_connect(struct TCP_Server_Info *server);
static int ipv6_connect(struct TCP_Server_Info *server);

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
	struct cifsSesInfo *ses;
	struct cifsTconInfo *tcon;
	struct mid_q_entry *mid_entry;

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

	cFYI(1, ("Reconnecting tcp session"));

	/* before reconnecting the tcp session, mark the smb session (uid)
		and the tid bad so they are not used until reconnected */
	read_lock(&cifs_tcp_ses_lock);
	list_for_each(tmp, &server->smb_ses_list) {
		ses = list_entry(tmp, struct cifsSesInfo, smb_ses_list);
		ses->need_reconnect = true;
		ses->ipc_tid = 0;
		list_for_each(tmp2, &ses->tcon_list) {
			tcon = list_entry(tmp2, struct cifsTconInfo, tcon_list);
			tcon->need_reconnect = true;
		}
	}
	read_unlock(&cifs_tcp_ses_lock);
	/* do not want to be sending data on a socket we are freeing */
	mutex_lock(&server->srv_mutex);
	if (server->ssocket) {
		cFYI(1, ("State: 0x%x Flags: 0x%lx", server->ssocket->state,
			server->ssocket->flags));
		kernel_sock_shutdown(server->ssocket, SHUT_WR);
		cFYI(1, ("Post shutdown state: 0x%x Flags: 0x%lx",
			server->ssocket->state,
			server->ssocket->flags));
		sock_release(server->ssocket);
		server->ssocket = NULL;
	}

	spin_lock(&GlobalMid_Lock);
	list_for_each(tmp, &server->pending_mid_q) {
		mid_entry = list_entry(tmp, struct
					mid_q_entry,
					qhead);
		if (mid_entry->midState == MID_REQUEST_SUBMITTED) {
				/* Mark other intransit requests as needing
				   retry so we do not immediately mark the
				   session bad again (ie after we reconnect
				   below) as they timeout too */
			mid_entry->midState = MID_RETRY_NEEDED;
		}
	}
	spin_unlock(&GlobalMid_Lock);
	mutex_unlock(&server->srv_mutex);

	while ((server->tcpStatus != CifsExiting) &&
	       (server->tcpStatus != CifsGood)) {
		try_to_freeze();
		if (server->addr.sockAddr6.sin6_family == AF_INET6)
			rc = ipv6_connect(server);
		else
			rc = ipv4_connect(server);
		if (rc) {
			cFYI(1, ("reconnect error %d", rc));
			msleep(3000);
		} else {
			atomic_inc(&tcpSesReconnectCount);
			spin_lock(&GlobalMid_Lock);
			if (server->tcpStatus != CifsExiting)
				server->tcpStatus = CifsGood;
			server->sequence_number = 0;
			spin_unlock(&GlobalMid_Lock);
	/*		atomic_set(&server->inFlight,0);*/
			wake_up(&server->response_q);
		}
	}
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
	int total_data_size;
	int data_in_this_rsp;
	int remaining;

	if (pSMB->Command != SMB_COM_TRANSACTION2)
		return 0;

	/* check for plausible wct, bcc and t2 data and parm sizes */
	/* check for parm and data offset going beyond end of smb */
	if (pSMB->WordCount != 10) { /* coalesce_t2 depends on this */
		cFYI(1, ("invalid transact2 word count"));
		return -EINVAL;
	}

	pSMBt = (struct smb_t2_rsp *)pSMB;

	total_data_size = le16_to_cpu(pSMBt->t2_rsp.TotalDataCount);
	data_in_this_rsp = le16_to_cpu(pSMBt->t2_rsp.DataCount);

	remaining = total_data_size - data_in_this_rsp;

	if (remaining == 0)
		return 0;
	else if (remaining < 0) {
		cFYI(1, ("total data %d smaller than data in frame %d",
			total_data_size, data_in_this_rsp));
		return -EINVAL;
	} else {
		cFYI(1, ("missing %d bytes from transact2, check next response",
			remaining));
		if (total_data_size > maxBufSize) {
			cERROR(1, ("TotalDataSize %d is over maximum buffer %d",
				total_data_size, maxBufSize));
			return -EINVAL;
		}
		return remaining;
	}
}

static int coalesce_t2(struct smb_hdr *psecond, struct smb_hdr *pTargetSMB)
{
	struct smb_t2_rsp *pSMB2 = (struct smb_t2_rsp *)psecond;
	struct smb_t2_rsp *pSMBt  = (struct smb_t2_rsp *)pTargetSMB;
	int total_data_size;
	int total_in_buf;
	int remaining;
	int total_in_buf2;
	char *data_area_of_target;
	char *data_area_of_buf2;
	__u16 byte_count;

	total_data_size = le16_to_cpu(pSMBt->t2_rsp.TotalDataCount);

	if (total_data_size != le16_to_cpu(pSMB2->t2_rsp.TotalDataCount)) {
		cFYI(1, ("total data size of primary and secondary t2 differ"));
	}

	total_in_buf = le16_to_cpu(pSMBt->t2_rsp.DataCount);

	remaining = total_data_size - total_in_buf;

	if (remaining < 0)
		return -EINVAL;

	if (remaining == 0) /* nothing to do, ignore */
		return 0;

	total_in_buf2 = le16_to_cpu(pSMB2->t2_rsp.DataCount);
	if (remaining < total_in_buf2) {
		cFYI(1, ("transact2 2nd response contains too much data"));
	}

	/* find end of first SMB data area */
	data_area_of_target = (char *)&pSMBt->hdr.Protocol +
				le16_to_cpu(pSMBt->t2_rsp.DataOffset);
	/* validate target area */

	data_area_of_buf2 = (char *) &pSMB2->hdr.Protocol +
					le16_to_cpu(pSMB2->t2_rsp.DataOffset);

	data_area_of_target += total_in_buf;

	/* copy second buffer into end of first buffer */
	memcpy(data_area_of_target, data_area_of_buf2, total_in_buf2);
	total_in_buf += total_in_buf2;
	pSMBt->t2_rsp.DataCount = cpu_to_le16(total_in_buf);
	byte_count = le16_to_cpu(BCC_LE(pTargetSMB));
	byte_count += total_in_buf2;
	BCC_LE(pTargetSMB) = cpu_to_le16(byte_count);

	byte_count = pTargetSMB->smb_buf_length;
	byte_count += total_in_buf2;

	/* BB also add check that we are not beyond maximum buffer size */

	pTargetSMB->smb_buf_length = byte_count;

	if (remaining == total_in_buf2) {
		cFYI(1, ("found the last secondary response"));
		return 0; /* we are done */
	} else /* more responses to go */
		return 1;

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
	struct list_head *tmp;
	struct cifsSesInfo *ses;
	struct task_struct *task_to_wake = NULL;
	struct mid_q_entry *mid_entry;
	char temp;
	bool isLargeBuf = false;
	bool isMultiRsp;
	int reconnect;

	current->flags |= PF_MEMALLOC;
	cFYI(1, ("Demultiplex PID: %d", task_pid_nr(current)));

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
				cERROR(1, ("No memory for large SMB response"));
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
				cERROR(1, ("No memory for SMB response"));
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
		length =
		    kernel_recvmsg(csocket, &smb_msg,
				&iov, 1, pdu_length, 0 /* BB other flags? */);

		if (server->tcpStatus == CifsExiting) {
			break;
		} else if (server->tcpStatus == CifsNeedReconnect) {
			cFYI(1, ("Reconnect after server stopped responding"));
			cifs_reconnect(server);
			cFYI(1, ("call to reconnect done"));
			csocket = server->ssocket;
			continue;
		} else if ((length == -ERESTARTSYS) || (length == -EAGAIN)) {
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
			if (server->tcpStatus == CifsNew) {
				cFYI(1, ("tcp session abend after SMBnegprot"));
				/* some servers kill the TCP session rather than
				   returning an SMB negprot error, in which
				   case reconnecting here is not going to help,
				   and so simply return error to mount */
				break;
			}
			if (!try_to_freeze() && (length == -EINTR)) {
				cFYI(1, ("cifsd thread killed"));
				break;
			}
			cFYI(1, ("Reconnect after unexpected peek error %d",
				length));
			cifs_reconnect(server);
			csocket = server->ssocket;
			wake_up(&server->response_q);
			continue;
		} else if (length < pdu_length) {
			cFYI(1, ("requested %d bytes but only got %d bytes",
				  pdu_length, length));
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
		pdu_length = be32_to_cpu((__force __be32)smb_buffer->smb_buf_length);
		smb_buffer->smb_buf_length = pdu_length;

		cFYI(1, ("rfc1002 length 0x%x", pdu_length+4));

		if (temp == (char) RFC1002_SESSION_KEEP_ALIVE) {
			continue;
		} else if (temp == (char)RFC1002_POSITIVE_SESSION_RESPONSE) {
			cFYI(1, ("Good RFC 1002 session rsp"));
			continue;
		} else if (temp == (char)RFC1002_NEGATIVE_SESSION_RESPONSE) {
			/* we get this from Windows 98 instead of
			   an error on SMB negprot response */
			cFYI(1, ("Negative RFC1002 Session Response Error 0x%x)",
				pdu_length));
			if (server->tcpStatus == CifsNew) {
				/* if nack on negprot (rather than
				ret of smb negprot error) reconnecting
				not going to help, ret error to mount */
				break;
			} else {
				/* give server a second to
				clean up before reconnect attempt */
				msleep(1000);
				/* always try 445 first on reconnect
				since we get NACK on some if we ever
				connected to port 139 (the NACK is
				since we do not begin with RFC1001
				session initialize frame) */
				server->addr.sockAddr.sin_port =
					htons(CIFS_PORT);
				cifs_reconnect(server);
				csocket = server->ssocket;
				wake_up(&server->response_q);
				continue;
			}
		} else if (temp != (char) 0) {
			cERROR(1, ("Unknown RFC 1002 frame"));
			cifs_dump_mem(" Received Data: ", (char *)smb_buffer,
				      length);
			cifs_reconnect(server);
			csocket = server->ssocket;
			continue;
		}

		/* else we have an SMB response */
		if ((pdu_length > CIFSMaxBufSize + MAX_CIFS_HDR_SIZE - 4) ||
			    (pdu_length < sizeof(struct smb_hdr) - 1 - 4)) {
			cERROR(1, ("Invalid size SMB length %d pdu_length %d",
					length, pdu_length+4));
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
			if ((server->tcpStatus == CifsExiting) ||
			    (length == -EINTR)) {
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
			} else if ((length == -ERESTARTSYS) ||
				   (length == -EAGAIN)) {
				msleep(1); /* minimum sleep to prevent looping,
					      allowing socket to clear and app
					      threads to set tcpStatus
					      CifsNeedReconnect if server hung*/
				length = 0;
				continue;
			} else if (length <= 0) {
				cERROR(1, ("Received no data, expecting %d",
					      pdu_length - total_read));
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

		length += 4; /* account for rfc1002 hdr */


		dump_smb(smb_buffer, length);
		if (checkSMB(smb_buffer, smb_buffer->Mid, total_read+4)) {
			cifs_dump_mem("Bad SMB: ", smb_buffer, 48);
			continue;
		}


		task_to_wake = NULL;
		spin_lock(&GlobalMid_Lock);
		list_for_each(tmp, &server->pending_mid_q) {
			mid_entry = list_entry(tmp, struct mid_q_entry, qhead);

			if ((mid_entry->mid == smb_buffer->Mid) &&
			    (mid_entry->midState == MID_REQUEST_SUBMITTED) &&
			    (mid_entry->command == smb_buffer->Command)) {
				if (check2ndT2(smb_buffer,server->maxBuf) > 0) {
					/* We have a multipart transact2 resp */
					isMultiRsp = true;
					if (mid_entry->resp_buf) {
						/* merge response - fix up 1st*/
						if (coalesce_t2(smb_buffer,
							mid_entry->resp_buf)) {
							mid_entry->multiRsp =
								 true;
							break;
						} else {
							/* all parts received */
							mid_entry->multiEnd =
								 true;
							goto multi_t2_fnd;
						}
					} else {
						if (!isLargeBuf) {
							cERROR(1,("1st trans2 resp needs bigbuf"));
					/* BB maybe we can fix this up,  switch
					   to already allocated large buffer? */
						} else {
							/* Have first buffer */
							mid_entry->resp_buf =
								 smb_buffer;
							mid_entry->largeBuf =
								 true;
							bigbuf = NULL;
						}
					}
					break;
				}
				mid_entry->resp_buf = smb_buffer;
				mid_entry->largeBuf = isLargeBuf;
multi_t2_fnd:
				task_to_wake = mid_entry->tsk;
				mid_entry->midState = MID_RESPONSE_RECEIVED;
#ifdef CONFIG_CIFS_STATS2
				mid_entry->when_received = jiffies;
#endif
				/* so we do not time out requests to  server
				which is still responding (since server could
				be busy but not dead) */
				server->lstrp = jiffies;
				break;
			}
		}
		spin_unlock(&GlobalMid_Lock);
		if (task_to_wake) {
			/* Was previous buf put in mpx struct for multi-rsp? */
			if (!isMultiRsp) {
				/* smb buffer will be freed by user thread */
				if (isLargeBuf)
					bigbuf = NULL;
				else
					smallbuf = NULL;
			}
			wake_up_process(task_to_wake);
		} else if (!is_valid_oplock_break(smb_buffer, server) &&
			   !isMultiRsp) {
			cERROR(1, ("No task to wake, unknown frame received! "
				   "NumMids %d", midCount.counter));
			cifs_dump_mem("Received Data is: ", (char *)smb_buffer,
				      sizeof(struct smb_hdr));
#ifdef CONFIG_CIFS_DEBUG2
			cifs_dump_detail(smb_buffer);
			cifs_dump_mids(server);
#endif /* CIFS_DEBUG2 */

		}
	} /* end while !EXITING */

	/* take it off the list, if it's not already */
	write_lock(&cifs_tcp_ses_lock);
	list_del_init(&server->tcp_ses_list);
	write_unlock(&cifs_tcp_ses_lock);

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
	/* buffer usuallly freed in free_mid - need to free it here on exit */
	cifs_buf_release(bigbuf);
	if (smallbuf) /* no sense logging a debug message if NULL */
		cifs_small_buf_release(smallbuf);

	/*
	 * BB: we shouldn't have to do any of this. It shouldn't be
	 * possible to exit from the thread with active SMB sessions
	 */
	read_lock(&cifs_tcp_ses_lock);
	if (list_empty(&server->pending_mid_q)) {
		/* loop through server session structures attached to this and
		    mark them dead */
		list_for_each(tmp, &server->smb_ses_list) {
			ses = list_entry(tmp, struct cifsSesInfo,
					 smb_ses_list);
			ses->status = CifsExiting;
			ses->server = NULL;
		}
		read_unlock(&cifs_tcp_ses_lock);
	} else {
		/* although we can not zero the server struct pointer yet,
		since there are active requests which may depnd on them,
		mark the corresponding SMB sessions as exiting too */
		list_for_each(tmp, &server->smb_ses_list) {
			ses = list_entry(tmp, struct cifsSesInfo,
					 smb_ses_list);
			ses->status = CifsExiting;
		}

		spin_lock(&GlobalMid_Lock);
		list_for_each(tmp, &server->pending_mid_q) {
		mid_entry = list_entry(tmp, struct mid_q_entry, qhead);
			if (mid_entry->midState == MID_REQUEST_SUBMITTED) {
				cFYI(1, ("Clearing Mid 0x%x - waking up ",
					 mid_entry->mid));
				task_to_wake = mid_entry->tsk;
				if (task_to_wake)
					wake_up_process(task_to_wake);
			}
		}
		spin_unlock(&GlobalMid_Lock);
		read_unlock(&cifs_tcp_ses_lock);
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
		cFYI(1, ("Wait for exit from demultiplex thread"));
		msleep(46000);
		/* if threads still have not exited they are probably never
		coming home not much else we can do but free the memory */
	}

	/* last chance to mark ses pointers invalid
	if there are any pointing to this (e.g
	if a crazy root user tried to kill cifsd
	kernel thread explicitly this might happen) */
	/* BB: This shouldn't be necessary, see above */
	read_lock(&cifs_tcp_ses_lock);
	list_for_each(tmp, &server->smb_ses_list) {
		ses = list_entry(tmp, struct cifsSesInfo, smb_ses_list);
		ses->server = NULL;
	}
	read_unlock(&cifs_tcp_ses_lock);

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
cifs_parse_mount_options(char *options, const char *devname,
			 struct smb_vol *vol)
{
	char *value;
	char *data;
	unsigned int  temp_len, i, j;
	char separator[2];

	separator[0] = ',';
	separator[1] = 0;

	if (Local_System_Name[0] != 0)
		memcpy(vol->source_rfc1001_name, Local_System_Name, 15);
	else {
		char *nodename = utsname()->nodename;
		int n = strnlen(nodename, 15);
		memset(vol->source_rfc1001_name, 0x20, 15);
		for (i = 0; i < n; i++) {
			/* does not have to be perfect mapping since field is
			informational, only used for servers that do not support
			port 445 and it can be overridden at mount time */
			vol->source_rfc1001_name[i] = toupper(nodename[i]);
		}
	}
	vol->source_rfc1001_name[15] = 0;
	/* null target name indicates to use *SMBSERVR default called name
	   if we end up sending RFC1001 session initialize */
	vol->target_rfc1001_name[0] = 0;
	vol->linux_uid = current_uid();  /* use current_euid() instead? */
	vol->linux_gid = current_gid();
	vol->dir_mode = S_IRWXUGO;
	/* 2767 perms indicate mandatory locking support */
	vol->file_mode = (S_IRWXUGO | S_ISGID) & (~S_IXGRP);

	/* vol->retry default is 0 (i.e. "soft" limited retry not hard retry) */
	vol->rw = true;
	/* default is always to request posix paths. */
	vol->posix_paths = 1;

	if (!options)
		return 1;

	if (strncmp(options, "sep=", 4) == 0) {
		if (options[4] != 0) {
			separator[0] = options[4];
			options += 5;
		} else {
			cFYI(1, ("Null separator not allowed"));
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
				return 1;	/* needs_arg; */
			} else if (!*value) {
				/* null user, ie anonymous, authentication */
				vol->nullauth = 1;
			}
			if (strnlen(value, 200) < 200) {
				vol->username = value;
			} else {
				printk(KERN_WARNING "CIFS: username too long\n");
				return 1;
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
					return 1;
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
					return 1;
				}
				strcpy(vol->password, value);
			}
		} else if (strnicmp(data, "ip", 2) == 0) {
			if (!value || !*value) {
				vol->UNCip = NULL;
			} else if (strnlen(value, 35) < 35) {
				vol->UNCip = value;
			} else {
				printk(KERN_WARNING "CIFS: ip address "
						    "too long\n");
				return 1;
			}
		} else if (strnicmp(data, "sec", 3) == 0) {
			if (!value || !*value) {
				cERROR(1, ("no security value specified"));
				continue;
			} else if (strnicmp(value, "krb5i", 5) == 0) {
				vol->secFlg |= CIFSSEC_MAY_KRB5 |
					CIFSSEC_MUST_SIGN;
			} else if (strnicmp(value, "krb5p", 5) == 0) {
				/* vol->secFlg |= CIFSSEC_MUST_SEAL |
					CIFSSEC_MAY_KRB5; */
				cERROR(1, ("Krb5 cifs privacy not supported"));
				return 1;
			} else if (strnicmp(value, "krb5", 4) == 0) {
				vol->secFlg |= CIFSSEC_MAY_KRB5;
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
				cERROR(1, ("bad security option: %s", value));
				return 1;
			}
		} else if ((strnicmp(data, "unc", 3) == 0)
			   || (strnicmp(data, "target", 6) == 0)
			   || (strnicmp(data, "path", 4) == 0)) {
			if (!value || !*value) {
				printk(KERN_WARNING "CIFS: invalid path to "
						    "network resource\n");
				return 1;	/* needs_arg; */
			}
			if ((temp_len = strnlen(value, 300)) < 300) {
				vol->UNC = kmalloc(temp_len+1, GFP_KERNEL);
				if (vol->UNC == NULL)
					return 1;
				strcpy(vol->UNC, value);
				if (strncmp(vol->UNC, "//", 2) == 0) {
					vol->UNC[0] = '\\';
					vol->UNC[1] = '\\';
				} else if (strncmp(vol->UNC, "\\\\", 2) != 0) {
					printk(KERN_WARNING
					       "CIFS: UNC Path does not begin "
					       "with // or \\\\ \n");
					return 1;
				}
			} else {
				printk(KERN_WARNING "CIFS: UNC name too long\n");
				return 1;
			}
		} else if ((strnicmp(data, "domain", 3) == 0)
			   || (strnicmp(data, "workgroup", 5) == 0)) {
			if (!value || !*value) {
				printk(KERN_WARNING "CIFS: invalid domain name\n");
				return 1;	/* needs_arg; */
			}
			/* BB are there cases in which a comma can be valid in
			a domain name and need special handling? */
			if (strnlen(value, 256) < 256) {
				vol->domainname = value;
				cFYI(1, ("Domain name set"));
			} else {
				printk(KERN_WARNING "CIFS: domain name too "
						    "long\n");
				return 1;
			}
		} else if (strnicmp(data, "prefixpath", 10) == 0) {
			if (!value || !*value) {
				printk(KERN_WARNING
					"CIFS: invalid path prefix\n");
				return 1;       /* needs_argument */
			}
			if ((temp_len = strnlen(value, 1024)) < 1024) {
				if (value[0] != '/')
					temp_len++;  /* missing leading slash */
				vol->prepath = kmalloc(temp_len+1, GFP_KERNEL);
				if (vol->prepath == NULL)
					return 1;
				if (value[0] != '/') {
					vol->prepath[0] = '/';
					strcpy(vol->prepath+1, value);
				} else
					strcpy(vol->prepath, value);
				cFYI(1, ("prefix path %s", vol->prepath));
			} else {
				printk(KERN_WARNING "CIFS: prefix too long\n");
				return 1;
			}
		} else if (strnicmp(data, "iocharset", 9) == 0) {
			if (!value || !*value) {
				printk(KERN_WARNING "CIFS: invalid iocharset "
						    "specified\n");
				return 1;	/* needs_arg; */
			}
			if (strnlen(value, 65) < 65) {
				if (strnicmp(value, "default", 7))
					vol->iocharset = value;
				/* if iocharset not set then load_nls_default
				   is used by caller */
				cFYI(1, ("iocharset set to %s", value));
			} else {
				printk(KERN_WARNING "CIFS: iocharset name "
						    "too long.\n");
				return 1;
			}
		} else if (strnicmp(data, "uid", 3) == 0) {
			if (value && *value) {
				vol->linux_uid =
					simple_strtoul(value, &value, 0);
				vol->override_uid = 1;
			}
		} else if (strnicmp(data, "gid", 3) == 0) {
			if (value && *value) {
				vol->linux_gid =
					simple_strtoul(value, &value, 0);
				vol->override_gid = 1;
			}
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
			if (value && *value) {
				vol->sockopt =
					simple_strtoul(value, &value, 0);
			}
		} else if (strnicmp(data, "netbiosname", 4) == 0) {
			if (!value || !*value || (*value == ' ')) {
				cFYI(1, ("invalid (empty) netbiosname"));
			} else {
				memset(vol->source_rfc1001_name, 0x20, 15);
				for (i = 0; i < 15; i++) {
				/* BB are there cases in which a comma can be
				valid in this workstation netbios name (and need
				special handling)? */

				/* We do not uppercase netbiosname for user */
					if (value[i] == 0)
						break;
					else
						vol->source_rfc1001_name[i] =
								value[i];
				}
				/* The string has 16th byte zero still from
				set at top of the function  */
				if ((i == 15) && (value[i] != 0))
					printk(KERN_WARNING "CIFS: netbiosname"
						" longer than 15 truncated.\n");
			}
		} else if (strnicmp(data, "servern", 7) == 0) {
			/* servernetbiosname specified override *SMBSERVER */
			if (!value || !*value || (*value == ' ')) {
				cFYI(1, ("empty server netbiosname specified"));
			} else {
				/* last byte, type, is 0x20 for servr type */
				memset(vol->target_rfc1001_name, 0x20, 16);

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
				if ((i == 15) && (value[i] != 0))
					printk(KERN_WARNING "CIFS: server net"
					"biosname longer than 15 truncated.\n");
			}
		} else if (strnicmp(data, "credentials", 4) == 0) {
			/* ignore */
		} else if (strnicmp(data, "version", 3) == 0) {
			/* ignore */
		} else if (strnicmp(data, "guest", 5) == 0) {
			/* ignore */
		} else if (strnicmp(data, "rw", 2) == 0) {
			vol->rw = true;
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
		} else if (strnicmp(data, "ro", 2) == 0) {
			vol->rw = false;
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
		} else if (strnicmp(data, "cifsacl", 7) == 0) {
			vol->cifs_acl = 1;
		} else if (strnicmp(data, "nocifsacl", 9) == 0) {
			vol->cifs_acl = 0;
		} else if (strnicmp(data, "acl", 3) == 0) {
			vol->no_psx_acl = 0;
		} else if (strnicmp(data, "noacl", 5) == 0) {
			vol->no_psx_acl = 1;
#ifdef CONFIG_CIFS_EXPERIMENTAL
		} else if (strnicmp(data, "locallease", 6) == 0) {
			vol->local_lease = 1;
#endif
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
		} else if (strnicmp(data, "in6_addr", 8) == 0) {
			if (!value || !*value) {
				vol->in6_addr = NULL;
			} else if (strnlen(value, 49) == 48) {
				vol->in6_addr = value;
			} else {
				printk(KERN_WARNING "CIFS: ip v6 address not "
						    "48 characters long\n");
				return 1;
			}
		} else if (strnicmp(data, "noac", 4) == 0) {
			printk(KERN_WARNING "CIFS: Mount option noac not "
				"supported. Instead set "
				"/proc/fs/cifs/LookupCacheEnabled to 0\n");
		} else
			printk(KERN_WARNING "CIFS: Unknown mount option %s\n",
						data);
	}
	if (vol->UNC == NULL) {
		if (devname == NULL) {
			printk(KERN_WARNING "CIFS: Missing UNC name for mount "
						"target\n");
			return 1;
		}
		if ((temp_len = strnlen(devname, 300)) < 300) {
			vol->UNC = kmalloc(temp_len+1, GFP_KERNEL);
			if (vol->UNC == NULL)
				return 1;
			strcpy(vol->UNC, devname);
			if (strncmp(vol->UNC, "//", 2) == 0) {
				vol->UNC[0] = '\\';
				vol->UNC[1] = '\\';
			} else if (strncmp(vol->UNC, "\\\\", 2) != 0) {
				printk(KERN_WARNING "CIFS: UNC Path does not "
						    "begin with // or \\\\ \n");
				return 1;
			}
			value = strpbrk(vol->UNC+2, "/\\");
			if (value)
				*value = '\\';
		} else {
			printk(KERN_WARNING "CIFS: UNC name too long\n");
			return 1;
		}
	}
	if (vol->UNCip == NULL)
		vol->UNCip = &vol->UNC[2];

	return 0;
}

static struct TCP_Server_Info *
cifs_find_tcp_session(struct sockaddr_storage *addr)
{
	struct list_head *tmp;
	struct TCP_Server_Info *server;
	struct sockaddr_in *addr4 = (struct sockaddr_in *) addr;
	struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *) addr;

	write_lock(&cifs_tcp_ses_lock);
	list_for_each(tmp, &cifs_tcp_ses_list) {
		server = list_entry(tmp, struct TCP_Server_Info,
				    tcp_ses_list);
		/*
		 * the demux thread can exit on its own while still in CifsNew
		 * so don't accept any sockets in that state. Since the
		 * tcpStatus never changes back to CifsNew it's safe to check
		 * for this without a lock.
		 */
		if (server->tcpStatus == CifsNew)
			continue;

		if (addr->ss_family == AF_INET &&
		    (addr4->sin_addr.s_addr !=
		     server->addr.sockAddr.sin_addr.s_addr))
			continue;
		else if (addr->ss_family == AF_INET6 &&
			 !ipv6_addr_equal(&server->addr.sockAddr6.sin6_addr,
					  &addr6->sin6_addr))
			continue;

		++server->srv_count;
		write_unlock(&cifs_tcp_ses_lock);
		cFYI(1, ("Existing tcp session with server found"));
		return server;
	}
	write_unlock(&cifs_tcp_ses_lock);
	return NULL;
}

static void
cifs_put_tcp_session(struct TCP_Server_Info *server)
{
	struct task_struct *task;

	write_lock(&cifs_tcp_ses_lock);
	if (--server->srv_count > 0) {
		write_unlock(&cifs_tcp_ses_lock);
		return;
	}

	list_del_init(&server->tcp_ses_list);
	write_unlock(&cifs_tcp_ses_lock);

	spin_lock(&GlobalMid_Lock);
	server->tcpStatus = CifsExiting;
	spin_unlock(&GlobalMid_Lock);

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

	if (volume_info->UNCip && volume_info->UNC) {
		rc = cifs_inet_pton(AF_INET, volume_info->UNCip,
				    &sin_server->sin_addr.s_addr);

		if (rc <= 0) {
			/* not ipv4 address, try ipv6 */
			rc = cifs_inet_pton(AF_INET6, volume_info->UNCip,
					    &sin_server6->sin6_addr.in6_u);
			if (rc > 0)
				addr.ss_family = AF_INET6;
		} else {
			addr.ss_family = AF_INET;
		}

		if (rc <= 0) {
			/* we failed translating address */
			rc = -EINVAL;
			goto out_err;
		}

		cFYI(1, ("UNC: %s ip: %s", volume_info->UNC,
			 volume_info->UNCip));
	} else if (volume_info->UNCip) {
		/* BB using ip addr as tcp_ses name to connect to the
		   DFS root below */
		cERROR(1, ("Connecting to DFS root not implemented yet"));
		rc = -EINVAL;
		goto out_err;
	} else /* which tcp_sess DFS root would we conect to */ {
		cERROR(1,
		       ("CIFS mount error: No UNC path (e.g. -o "
			"unc=//192.168.1.100/public) specified"));
		rc = -EINVAL;
		goto out_err;
	}

	/* see if we already have a matching tcp_ses */
	tcp_ses = cifs_find_tcp_session(&addr);
	if (tcp_ses)
		return tcp_ses;

	tcp_ses = kzalloc(sizeof(struct TCP_Server_Info), GFP_KERNEL);
	if (!tcp_ses) {
		rc = -ENOMEM;
		goto out_err;
	}

	tcp_ses->hostname = extract_hostname(volume_info->UNC);
	if (IS_ERR(tcp_ses->hostname)) {
		rc = PTR_ERR(tcp_ses->hostname);
		goto out_err;
	}

	tcp_ses->noblocksnd = volume_info->noblocksnd;
	tcp_ses->noautotune = volume_info->noautotune;
	atomic_set(&tcp_ses->inFlight, 0);
	init_waitqueue_head(&tcp_ses->response_q);
	init_waitqueue_head(&tcp_ses->request_q);
	INIT_LIST_HEAD(&tcp_ses->pending_mid_q);
	mutex_init(&tcp_ses->srv_mutex);
	memcpy(tcp_ses->workstation_RFC1001_name,
		volume_info->source_rfc1001_name, RFC1001_NAME_LEN_WITH_NULL);
	memcpy(tcp_ses->server_RFC1001_name,
		volume_info->target_rfc1001_name, RFC1001_NAME_LEN_WITH_NULL);
	tcp_ses->sequence_number = 0;
	INIT_LIST_HEAD(&tcp_ses->tcp_ses_list);
	INIT_LIST_HEAD(&tcp_ses->smb_ses_list);

	/*
	 * at this point we are the only ones with the pointer
	 * to the struct since the kernel thread not created yet
	 * no need to spinlock this init of tcpStatus or srv_count
	 */
	tcp_ses->tcpStatus = CifsNew;
	++tcp_ses->srv_count;

	if (addr.ss_family == AF_INET6) {
		cFYI(1, ("attempting ipv6 connect"));
		/* BB should we allow ipv6 on port 139? */
		/* other OS never observed in Wild doing 139 with v6 */
		memcpy(&tcp_ses->addr.sockAddr6, sin_server6,
			sizeof(struct sockaddr_in6));
		sin_server6->sin6_port = htons(volume_info->port);
		rc = ipv6_connect(tcp_ses);
	} else {
		memcpy(&tcp_ses->addr.sockAddr, sin_server,
			sizeof(struct sockaddr_in));
		sin_server->sin_port = htons(volume_info->port);
		rc = ipv4_connect(tcp_ses);
	}
	if (rc < 0) {
		cERROR(1, ("Error connecting to socket. Aborting operation"));
		goto out_err;
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
		cERROR(1, ("error %d create cifsd thread", rc));
		module_put(THIS_MODULE);
		goto out_err;
	}

	/* thread spawned, put it on the list */
	write_lock(&cifs_tcp_ses_lock);
	list_add(&tcp_ses->tcp_ses_list, &cifs_tcp_ses_list);
	write_unlock(&cifs_tcp_ses_lock);

	return tcp_ses;

out_err:
	if (tcp_ses) {
		kfree(tcp_ses->hostname);
		if (tcp_ses->ssocket)
			sock_release(tcp_ses->ssocket);
		kfree(tcp_ses);
	}
	return ERR_PTR(rc);
}

static struct cifsSesInfo *
cifs_find_smb_ses(struct TCP_Server_Info *server, char *username)
{
	struct list_head *tmp;
	struct cifsSesInfo *ses;

	write_lock(&cifs_tcp_ses_lock);
	list_for_each(tmp, &server->smb_ses_list) {
		ses = list_entry(tmp, struct cifsSesInfo, smb_ses_list);
		if (strncmp(ses->userName, username, MAX_USERNAME_SIZE))
			continue;

		++ses->ses_count;
		write_unlock(&cifs_tcp_ses_lock);
		return ses;
	}
	write_unlock(&cifs_tcp_ses_lock);
	return NULL;
}

static void
cifs_put_smb_ses(struct cifsSesInfo *ses)
{
	int xid;
	struct TCP_Server_Info *server = ses->server;

	write_lock(&cifs_tcp_ses_lock);
	if (--ses->ses_count > 0) {
		write_unlock(&cifs_tcp_ses_lock);
		return;
	}

	list_del_init(&ses->smb_ses_list);
	write_unlock(&cifs_tcp_ses_lock);

	if (ses->status == CifsGood) {
		xid = GetXid();
		CIFSSMBLogoff(xid, ses);
		_FreeXid(xid);
	}
	sesInfoFree(ses);
	cifs_put_tcp_session(server);
}

static struct cifsTconInfo *
cifs_find_tcon(struct cifsSesInfo *ses, const char *unc)
{
	struct list_head *tmp;
	struct cifsTconInfo *tcon;

	write_lock(&cifs_tcp_ses_lock);
	list_for_each(tmp, &ses->tcon_list) {
		tcon = list_entry(tmp, struct cifsTconInfo, tcon_list);
		if (tcon->tidStatus == CifsExiting)
			continue;
		if (strncmp(tcon->treeName, unc, MAX_TREE_SIZE))
			continue;

		++tcon->tc_count;
		write_unlock(&cifs_tcp_ses_lock);
		return tcon;
	}
	write_unlock(&cifs_tcp_ses_lock);
	return NULL;
}

static void
cifs_put_tcon(struct cifsTconInfo *tcon)
{
	int xid;
	struct cifsSesInfo *ses = tcon->ses;

	write_lock(&cifs_tcp_ses_lock);
	if (--tcon->tc_count > 0) {
		write_unlock(&cifs_tcp_ses_lock);
		return;
	}

	list_del_init(&tcon->tcon_list);
	write_unlock(&cifs_tcp_ses_lock);

	xid = GetXid();
	CIFSSMBTDis(xid, tcon);
	_FreeXid(xid);

	DeleteTconOplockQEntries(tcon);
	tconInfoFree(tcon);
	cifs_put_smb_ses(ses);
}

int
get_dfs_path(int xid, struct cifsSesInfo *pSesInfo, const char *old_path,
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
		cFYI(1,
		     ("CIFS Tcon rc = %d ipc_tid = %d", rc, pSesInfo->ipc_tid));
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
ipv4_connect(struct TCP_Server_Info *server)
{
	int rc = 0;
	bool connected = false;
	__be16 orig_port = 0;
	struct socket *socket = server->ssocket;

	if (socket == NULL) {
		rc = sock_create_kern(PF_INET, SOCK_STREAM,
				      IPPROTO_TCP, &socket);
		if (rc < 0) {
			cERROR(1, ("Error %d creating socket", rc));
			return rc;
		}

		/* BB other socket options to set KEEPALIVE, NODELAY? */
		cFYI(1, ("Socket created"));
		server->ssocket = socket;
		socket->sk->sk_allocation = GFP_NOFS;
		cifs_reclassify_socket4(socket);
	}

	/* user overrode default port */
	if (server->addr.sockAddr.sin_port) {
		rc = socket->ops->connect(socket, (struct sockaddr *)
					  &server->addr.sockAddr,
					  sizeof(struct sockaddr_in), 0);
		if (rc >= 0)
			connected = true;
	}

	if (!connected) {
		/* save original port so we can retry user specified port
			later if fall back ports fail this time  */
		orig_port = server->addr.sockAddr.sin_port;

		/* do not retry on the same port we just failed on */
		if (server->addr.sockAddr.sin_port != htons(CIFS_PORT)) {
			server->addr.sockAddr.sin_port = htons(CIFS_PORT);
			rc = socket->ops->connect(socket,
						(struct sockaddr *)
						&server->addr.sockAddr,
						sizeof(struct sockaddr_in), 0);
			if (rc >= 0)
				connected = true;
		}
	}
	if (!connected) {
		server->addr.sockAddr.sin_port = htons(RFC1001_PORT);
		rc = socket->ops->connect(socket, (struct sockaddr *)
					      &server->addr.sockAddr,
					      sizeof(struct sockaddr_in), 0);
		if (rc >= 0)
			connected = true;
	}

	/* give up here - unless we want to retry on different
		protocol families some day */
	if (!connected) {
		if (orig_port)
			server->addr.sockAddr.sin_port = orig_port;
		cFYI(1, ("Error %d connecting to server via ipv4", rc));
		sock_release(socket);
		server->ssocket = NULL;
		return rc;
	}


	/*
	 * Eventually check for other socket options to change from
	 *  the default. sock_setsockopt not used because it expects
	 *  user space buffer
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

	 cFYI(1, ("sndbuf %d rcvbuf %d rcvtimeo 0x%lx",
		 socket->sk->sk_sndbuf,
		 socket->sk->sk_rcvbuf, socket->sk->sk_rcvtimeo));

	/* send RFC1001 sessinit */
	if (server->addr.sockAddr.sin_port == htons(RFC1001_PORT)) {
		/* some servers require RFC1001 sessinit before sending
		negprot - BB check reconnection in case where second
		sessinit is sent but no second negprot */
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

			/* calling name ends in null (byte 16) from old smb
			convention. */
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
			smb_buf->smb_buf_length = 0x81000044;
			rc = smb_send(server, smb_buf, 0x44);
			kfree(ses_init_buf);
			msleep(1); /* RFC1001 layer in at least one server
				      requires very short break before negprot
				      presumably because not expecting negprot
				      to follow so fast.  This is a simple
				      solution that works without
				      complicating the code and causes no
				      significant slowing down on mount
				      for everyone else */
		}
		/* else the negprot may still work without this
		even though malloc failed */

	}

	return rc;
}

static int
ipv6_connect(struct TCP_Server_Info *server)
{
	int rc = 0;
	bool connected = false;
	__be16 orig_port = 0;
	struct socket *socket = server->ssocket;

	if (socket == NULL) {
		rc = sock_create_kern(PF_INET6, SOCK_STREAM,
				      IPPROTO_TCP, &socket);
		if (rc < 0) {
			cERROR(1, ("Error %d creating ipv6 socket", rc));
			socket = NULL;
			return rc;
		}

		/* BB other socket options to set KEEPALIVE, NODELAY? */
		cFYI(1, ("ipv6 Socket created"));
		server->ssocket = socket;
		socket->sk->sk_allocation = GFP_NOFS;
		cifs_reclassify_socket6(socket);
	}

	/* user overrode default port */
	if (server->addr.sockAddr6.sin6_port) {
		rc = socket->ops->connect(socket,
				(struct sockaddr *) &server->addr.sockAddr6,
				sizeof(struct sockaddr_in6), 0);
		if (rc >= 0)
			connected = true;
	}

	if (!connected) {
		/* save original port so we can retry user specified port
			later if fall back ports fail this time  */

		orig_port = server->addr.sockAddr6.sin6_port;
		/* do not retry on the same port we just failed on */
		if (server->addr.sockAddr6.sin6_port != htons(CIFS_PORT)) {
			server->addr.sockAddr6.sin6_port = htons(CIFS_PORT);
			rc = socket->ops->connect(socket, (struct sockaddr *)
					&server->addr.sockAddr6,
					sizeof(struct sockaddr_in6), 0);
			if (rc >= 0)
				connected = true;
		}
	}
	if (!connected) {
		server->addr.sockAddr6.sin6_port = htons(RFC1001_PORT);
		rc = socket->ops->connect(socket, (struct sockaddr *)
				&server->addr.sockAddr6,
				sizeof(struct sockaddr_in6), 0);
		if (rc >= 0)
			connected = true;
	}

	/* give up here - unless we want to retry on different
		protocol families some day */
	if (!connected) {
		if (orig_port)
			server->addr.sockAddr6.sin6_port = orig_port;
		cFYI(1, ("Error %d connecting to server via ipv6", rc));
		sock_release(socket);
		server->ssocket = NULL;
		return rc;
	}

	/*
	 * Eventually check for other socket options to change from
	 * the default. sock_setsockopt not used because it expects
	 * user space buffer
	 */
	socket->sk->sk_rcvtimeo = 7 * HZ;
	socket->sk->sk_sndtimeo = 5 * HZ;
	server->ssocket = socket;

	return rc;
}

void reset_cifs_unix_caps(int xid, struct cifsTconInfo *tcon,
			  struct super_block *sb, struct smb_vol *vol_info)
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
		cFYI(1, ("Linux protocol extensions disabled"));
		return;
	} else if (vol_info)
		tcon->unix_ext = 1; /* Unix Extensions supported */

	if (tcon->unix_ext == 0) {
		cFYI(1, ("Unix extensions disabled so not set on reconnect"));
		return;
	}

	if (!CIFSSMBQFSUnixInfo(xid, tcon)) {
		__u64 cap = le64_to_cpu(tcon->fsUnixInfo.Capability);

		/* check for reconnect case in which we do not
		   want to change the mount behavior if we can avoid it */
		if (vol_info == NULL) {
			/* turn off POSIX ACL and PATHNAMES if not set
			   originally at mount time */
			if ((saved_cap & CIFS_UNIX_POSIX_ACL_CAP) == 0)
				cap &= ~CIFS_UNIX_POSIX_ACL_CAP;
			if ((saved_cap & CIFS_UNIX_POSIX_PATHNAMES_CAP) == 0) {
				if (cap & CIFS_UNIX_POSIX_PATHNAMES_CAP)
					cERROR(1, ("POSIXPATH support change"));
				cap &= ~CIFS_UNIX_POSIX_PATHNAMES_CAP;
			} else if ((cap & CIFS_UNIX_POSIX_PATHNAMES_CAP) == 0) {
				cERROR(1, ("possible reconnect error"));
				cERROR(1,
					("server disabled POSIX path support"));
			}
		}

		cap &= CIFS_UNIX_CAP_MASK;
		if (vol_info && vol_info->no_psx_acl)
			cap &= ~CIFS_UNIX_POSIX_ACL_CAP;
		else if (CIFS_UNIX_POSIX_ACL_CAP & cap) {
			cFYI(1, ("negotiated posix acl support"));
			if (sb)
				sb->s_flags |= MS_POSIXACL;
		}

		if (vol_info && vol_info->posix_paths == 0)
			cap &= ~CIFS_UNIX_POSIX_PATHNAMES_CAP;
		else if (cap & CIFS_UNIX_POSIX_PATHNAMES_CAP) {
			cFYI(1, ("negotiate posix pathnames"));
			if (sb)
				CIFS_SB(sb)->mnt_cifs_flags |=
					CIFS_MOUNT_POSIX_PATHS;
		}

		/* We might be setting the path sep back to a different
		form if we are reconnecting and the server switched its
		posix path capability for this share */
		if (sb && (CIFS_SB(sb)->prepathlen > 0))
			CIFS_SB(sb)->prepath[0] = CIFS_DIR_SEP(CIFS_SB(sb));

		if (sb && (CIFS_SB(sb)->rsize > 127 * 1024)) {
			if ((cap & CIFS_UNIX_LARGE_READ_CAP) == 0) {
				CIFS_SB(sb)->rsize = 127 * 1024;
				cFYI(DBG2,
					("larger reads not supported by srv"));
			}
		}


		cFYI(1, ("Negotiate caps 0x%x", (int)cap));
#ifdef CONFIG_CIFS_DEBUG2
		if (cap & CIFS_UNIX_FCNTL_CAP)
			cFYI(1, ("FCNTL cap"));
		if (cap & CIFS_UNIX_EXTATTR_CAP)
			cFYI(1, ("EXTATTR cap"));
		if (cap & CIFS_UNIX_POSIX_PATHNAMES_CAP)
			cFYI(1, ("POSIX path cap"));
		if (cap & CIFS_UNIX_XATTR_CAP)
			cFYI(1, ("XATTR cap"));
		if (cap & CIFS_UNIX_POSIX_ACL_CAP)
			cFYI(1, ("POSIX ACL cap"));
		if (cap & CIFS_UNIX_LARGE_READ_CAP)
			cFYI(1, ("very large read cap"));
		if (cap & CIFS_UNIX_LARGE_WRITE_CAP)
			cFYI(1, ("very large write cap"));
#endif /* CIFS_DEBUG2 */
		if (CIFSSMBSetFSUnixInfo(xid, tcon, cap)) {
			if (vol_info == NULL) {
				cFYI(1, ("resetting capabilities failed"));
			} else
				cERROR(1, ("Negotiating Unix capabilities "
					   "with the server failed.  Consider "
					   "mounting with the Unix Extensions\n"
					   "disabled, if problems are found, "
					   "by specifying the nounix mount "
					   "option."));

		}
	}
}

static void
convert_delimiter(char *path, char delim)
{
	int i;
	char old_delim;

	if (path == NULL)
		return;

	if (delim == '/')
		old_delim = '\\';
	else
		old_delim = '/';

	for (i = 0; path[i] != '\0'; i++) {
		if (path[i] == old_delim)
			path[i] = delim;
	}
}

static void setup_cifs_sb(struct smb_vol *pvolume_info,
			  struct cifs_sb_info *cifs_sb)
{
	if (pvolume_info->rsize > CIFSMaxBufSize) {
		cERROR(1, ("rsize %d too large, using MaxBufSize",
			pvolume_info->rsize));
		cifs_sb->rsize = CIFSMaxBufSize;
	} else if ((pvolume_info->rsize) &&
			(pvolume_info->rsize <= CIFSMaxBufSize))
		cifs_sb->rsize = pvolume_info->rsize;
	else /* default */
		cifs_sb->rsize = CIFSMaxBufSize;

	if (pvolume_info->wsize > PAGEVEC_SIZE * PAGE_CACHE_SIZE) {
		cERROR(1, ("wsize %d too large, using 4096 instead",
			  pvolume_info->wsize));
		cifs_sb->wsize = 4096;
	} else if (pvolume_info->wsize)
		cifs_sb->wsize = pvolume_info->wsize;
	else
		cifs_sb->wsize = min_t(const int,
					PAGEVEC_SIZE * PAGE_CACHE_SIZE,
					127*1024);
		/* old default of CIFSMaxBufSize was too small now
		   that SMB Write2 can send multiple pages in kvec.
		   RFC1001 does not describe what happens when frame
		   bigger than 128K is sent so use that as max in
		   conjunction with 52K kvec constraint on arch with 4K
		   page size  */

	if (cifs_sb->rsize < 2048) {
		cifs_sb->rsize = 2048;
		/* Windows ME may prefer this */
		cFYI(1, ("readsize set to minimum: 2048"));
	}
	/* calculate prepath */
	cifs_sb->prepath = pvolume_info->prepath;
	if (cifs_sb->prepath) {
		cifs_sb->prepathlen = strlen(cifs_sb->prepath);
		/* we can not convert the / to \ in the path
		separators in the prefixpath yet because we do not
		know (until reset_cifs_unix_caps is called later)
		whether POSIX PATH CAP is available. We normalize
		the / to \ after reset_cifs_unix_caps is called */
		pvolume_info->prepath = NULL;
	} else
		cifs_sb->prepathlen = 0;
	cifs_sb->mnt_uid = pvolume_info->linux_uid;
	cifs_sb->mnt_gid = pvolume_info->linux_gid;
	cifs_sb->mnt_file_mode = pvolume_info->file_mode;
	cifs_sb->mnt_dir_mode = pvolume_info->dir_mode;
	cFYI(1, ("file mode: 0x%x  dir mode: 0x%x",
		cifs_sb->mnt_file_mode, cifs_sb->mnt_dir_mode));

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
	if (pvolume_info->cifs_acl)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_CIFS_ACL;
	if (pvolume_info->override_uid)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_OVERR_UID;
	if (pvolume_info->override_gid)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_OVERR_GID;
	if (pvolume_info->dynperm)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_DYNPERM;
	if (pvolume_info->direct_io) {
		cFYI(1, ("mounting share using direct i/o"));
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_DIRECT_IO;
	}

	if ((pvolume_info->cifs_acl) && (pvolume_info->dynperm))
		cERROR(1, ("mount option dynperm ignored if cifsacl "
			   "mount option supported"));
}

static int
is_path_accessible(int xid, struct cifsTconInfo *tcon,
		   struct cifs_sb_info *cifs_sb, const char *full_path)
{
	int rc;
	__u64 inode_num;
	FILE_ALL_INFO *pfile_info;

	rc = CIFSGetSrvInodeNumber(xid, tcon, full_path, &inode_num,
				   cifs_sb->local_nls,
				   cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
	if (rc != -EOPNOTSUPP)
		return rc;

	pfile_info = kmalloc(sizeof(FILE_ALL_INFO), GFP_KERNEL);
	if (pfile_info == NULL)
		return -ENOMEM;

	rc = CIFSSMBQPathInfo(xid, tcon, full_path, pfile_info,
			      0 /* not legacy */, cifs_sb->local_nls,
			      cifs_sb->mnt_cifs_flags &
				CIFS_MOUNT_MAP_SPECIAL_CHR);
	kfree(pfile_info);
	return rc;
}

static void
cleanup_volume_info(struct smb_vol **pvolume_info)
{
	struct smb_vol *volume_info;

	if (!pvolume_info && !*pvolume_info)
		return;

	volume_info = *pvolume_info;
	kzfree(volume_info->password);
	kfree(volume_info->UNC);
	kfree(volume_info->prepath);
	kfree(volume_info);
	*pvolume_info = NULL;
	return;
}

/* build_path_to_root returns full path to root when
 * we do not have an exiting connection (tcon) */
static char *
build_unc_path_to_root(const struct smb_vol *volume_info,
		const struct cifs_sb_info *cifs_sb)
{
	char *full_path;

	int unc_len = strnlen(volume_info->UNC, MAX_TREE_SIZE + 1);
	full_path = kmalloc(unc_len + cifs_sb->prepathlen + 1, GFP_KERNEL);
	if (full_path == NULL)
		return ERR_PTR(-ENOMEM);

	strncpy(full_path, volume_info->UNC, unc_len);
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_POSIX_PATHS) {
		int i;
		for (i = 0; i < unc_len; i++) {
			if (full_path[i] == '\\')
				full_path[i] = '/';
		}
	}

	if (cifs_sb->prepathlen)
		strncpy(full_path + unc_len, cifs_sb->prepath,
				cifs_sb->prepathlen);

	full_path[unc_len + cifs_sb->prepathlen] = 0; /* add trailing null */
	return full_path;
}

int
cifs_mount(struct super_block *sb, struct cifs_sb_info *cifs_sb,
		char *mount_data_global, const char *devname)
{
	int rc = 0;
	int xid;
	struct smb_vol *volume_info;
	struct cifsSesInfo *pSesInfo = NULL;
	struct cifsTconInfo *tcon = NULL;
	struct TCP_Server_Info *srvTcp = NULL;
	char   *full_path;
	struct dfs_info3_param *referrals = NULL;
	unsigned int num_referrals = 0;

	char *mount_data = mount_data_global;

try_mount_again:
	full_path = NULL;

	xid = GetXid();

	volume_info = kzalloc(sizeof(struct smb_vol), GFP_KERNEL);
	if (!volume_info) {
		rc = -ENOMEM;
		goto out;
	}

	if (cifs_parse_mount_options(mount_data, devname, volume_info)) {
		rc = -EINVAL;
		goto out;
	}

	if (volume_info->nullauth) {
		cFYI(1, ("null user"));
		volume_info->username = "";
	} else if (volume_info->username) {
		/* BB fixme parse for domain name here */
		cFYI(1, ("Username: %s", volume_info->username));
	} else {
		cifserror("No username specified");
	/* In userspace mount helper we can get user name from alternate
	   locations such as env variables and files on disk */
		rc = -EINVAL;
		goto out;
	}


	/* this is needed for ASCII cp to Unicode converts */
	if (volume_info->iocharset == NULL) {
		cifs_sb->local_nls = load_nls_default();
	/* load_nls_default can not return null */
	} else {
		cifs_sb->local_nls = load_nls(volume_info->iocharset);
		if (cifs_sb->local_nls == NULL) {
			cERROR(1, ("CIFS mount error: iocharset %s not found",
				 volume_info->iocharset));
			rc = -ELIBACC;
			goto out;
		}
	}

	/* get a reference to a tcp session */
	srvTcp = cifs_get_tcp_session(volume_info);
	if (IS_ERR(srvTcp)) {
		rc = PTR_ERR(srvTcp);
		goto out;
	}

	pSesInfo = cifs_find_smb_ses(srvTcp, volume_info->username);
	if (pSesInfo) {
		cFYI(1, ("Existing smb sess found (status=%d)",
			pSesInfo->status));
		/*
		 * The existing SMB session already has a reference to srvTcp,
		 * so we can put back the extra one we got before
		 */
		cifs_put_tcp_session(srvTcp);

		down(&pSesInfo->sesSem);
		if (pSesInfo->need_reconnect) {
			cFYI(1, ("Session needs reconnect"));
			rc = cifs_setup_session(xid, pSesInfo,
						cifs_sb->local_nls);
		}
		up(&pSesInfo->sesSem);
	} else if (!rc) {
		cFYI(1, ("Existing smb sess not found"));
		pSesInfo = sesInfoAlloc();
		if (pSesInfo == NULL) {
			rc = -ENOMEM;
			goto mount_fail_check;
		}

		/* new SMB session uses our srvTcp ref */
		pSesInfo->server = srvTcp;
		if (srvTcp->addr.sockAddr6.sin6_family == AF_INET6)
			sprintf(pSesInfo->serverName, "%pI6",
				&srvTcp->addr.sockAddr6.sin6_addr);
		else
			sprintf(pSesInfo->serverName, "%pI4",
				&srvTcp->addr.sockAddr.sin_addr.s_addr);

		write_lock(&cifs_tcp_ses_lock);
		list_add(&pSesInfo->smb_ses_list, &srvTcp->smb_ses_list);
		write_unlock(&cifs_tcp_ses_lock);

		/* volume_info->password freed at unmount */
		if (volume_info->password) {
			pSesInfo->password = kstrdup(volume_info->password,
						     GFP_KERNEL);
			if (!pSesInfo->password) {
				rc = -ENOMEM;
				goto mount_fail_check;
			}
		}
		if (volume_info->username)
			strncpy(pSesInfo->userName, volume_info->username,
				MAX_USERNAME_SIZE);
		if (volume_info->domainname) {
			int len = strlen(volume_info->domainname);
			pSesInfo->domainName = kmalloc(len + 1, GFP_KERNEL);
			if (pSesInfo->domainName)
				strcpy(pSesInfo->domainName,
					volume_info->domainname);
		}
		pSesInfo->linux_uid = volume_info->linux_uid;
		pSesInfo->overrideSecFlg = volume_info->secFlg;
		down(&pSesInfo->sesSem);

		/* BB FIXME need to pass vol->secFlgs BB */
		rc = cifs_setup_session(xid, pSesInfo,
					cifs_sb->local_nls);
		up(&pSesInfo->sesSem);
	}

	/* search for existing tcon to this server share */
	if (!rc) {
		setup_cifs_sb(volume_info, cifs_sb);

		tcon = cifs_find_tcon(pSesInfo, volume_info->UNC);
		if (tcon) {
			cFYI(1, ("Found match on UNC path"));
			/* existing tcon already has a reference */
			cifs_put_smb_ses(pSesInfo);
			if (tcon->seal != volume_info->seal)
				cERROR(1, ("transport encryption setting "
					   "conflicts with existing tid"));
		} else {
			tcon = tconInfoAlloc();
			if (tcon == NULL) {
				rc = -ENOMEM;
				goto mount_fail_check;
			}

			tcon->ses = pSesInfo;
			if (volume_info->password) {
				tcon->password = kstrdup(volume_info->password,
							 GFP_KERNEL);
				if (!tcon->password) {
					rc = -ENOMEM;
					goto mount_fail_check;
				}
			}

			if ((strchr(volume_info->UNC + 3, '\\') == NULL)
			    && (strchr(volume_info->UNC + 3, '/') == NULL)) {
				cERROR(1, ("Missing share name"));
				rc = -ENODEV;
				goto mount_fail_check;
			} else {
				/* BB Do we need to wrap sesSem around
				 * this TCon call and Unix SetFS as
				 * we do on SessSetup and reconnect? */
				rc = CIFSTCon(xid, pSesInfo, volume_info->UNC,
					      tcon, cifs_sb->local_nls);
				cFYI(1, ("CIFS Tcon rc = %d", rc));
				if (volume_info->nodfs) {
					tcon->Flags &= ~SMB_SHARE_IS_IN_DFS;
					cFYI(1, ("DFS disabled (%d)",
						tcon->Flags));
				}
			}
			if (rc)
				goto remote_path_check;
			tcon->seal = volume_info->seal;
			write_lock(&cifs_tcp_ses_lock);
			list_add(&tcon->tcon_list, &pSesInfo->tcon_list);
			write_unlock(&cifs_tcp_ses_lock);
		}

		/* we can have only one retry value for a connection
		   to a share so for resources mounted more than once
		   to the same server share the last value passed in
		   for the retry flag is used */
		tcon->retry = volume_info->retry;
		tcon->nocase = volume_info->nocase;
		tcon->local_lease = volume_info->local_lease;
	}
	if (pSesInfo) {
		if (pSesInfo->capabilities & CAP_LARGE_FILES) {
			sb->s_maxbytes = (u64) 1 << 63;
		} else
			sb->s_maxbytes = (u64) 1 << 31;	/* 2 GB */
	}

	/* BB FIXME fix time_gran to be larger for LANMAN sessions */
	sb->s_time_gran = 100;

	if (rc)
		goto remote_path_check;

	cifs_sb->tcon = tcon;

	/* do not care if following two calls succeed - informational */
	if (!tcon->ipc) {
		CIFSSMBQFSDeviceInfo(xid, tcon);
		CIFSSMBQFSAttributeInfo(xid, tcon);
	}

	/* tell server which Unix caps we support */
	if (tcon->ses->capabilities & CAP_UNIX)
		/* reset of caps checks mount to see if unix extensions
		   disabled for just this mount */
		reset_cifs_unix_caps(xid, tcon, sb, volume_info);
	else
		tcon->unix_ext = 0; /* server does not support them */

	/* convert forward to back slashes in prepath here if needed */
	if ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_POSIX_PATHS) == 0)
		convert_delimiter(cifs_sb->prepath, CIFS_DIR_SEP(cifs_sb));

	if ((tcon->unix_ext == 0) && (cifs_sb->rsize > (1024 * 127))) {
		cifs_sb->rsize = 1024 * 127;
		cFYI(DBG2, ("no very large read support, rsize now 127K"));
	}
	if (!(tcon->ses->capabilities & CAP_LARGE_WRITE_X))
		cifs_sb->wsize = min(cifs_sb->wsize,
			       (tcon->ses->server->maxBuf - MAX_CIFS_HDR_SIZE));
	if (!(tcon->ses->capabilities & CAP_LARGE_READ_X))
		cifs_sb->rsize = min(cifs_sb->rsize,
			       (tcon->ses->server->maxBuf - MAX_CIFS_HDR_SIZE));

remote_path_check:
	/* check if a whole path (including prepath) is not remote */
	if (!rc && cifs_sb->prepathlen && tcon) {
		/* build_path_to_root works only when we have a valid tcon */
		full_path = cifs_build_path_to_root(cifs_sb);
		if (full_path == NULL) {
			rc = -ENOMEM;
			goto mount_fail_check;
		}
		rc = is_path_accessible(xid, tcon, cifs_sb, full_path);
		if (rc != -EREMOTE) {
			kfree(full_path);
			goto mount_fail_check;
		}
		kfree(full_path);
	}

	/* get referral if needed */
	if (rc == -EREMOTE) {
#ifdef CONFIG_CIFS_DFS_UPCALL
		/* convert forward to back slashes in prepath here if needed */
		if ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_POSIX_PATHS) == 0)
			convert_delimiter(cifs_sb->prepath,
					CIFS_DIR_SEP(cifs_sb));
		full_path = build_unc_path_to_root(volume_info, cifs_sb);
		if (IS_ERR(full_path)) {
			rc = PTR_ERR(full_path);
			goto mount_fail_check;
		}

		cFYI(1, ("Getting referral for: %s", full_path));
		rc = get_dfs_path(xid, pSesInfo , full_path + 1,
			cifs_sb->local_nls, &num_referrals, &referrals,
			cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);
		if (!rc && num_referrals > 0) {
			char *fake_devname = NULL;

			if (mount_data != mount_data_global)
				kfree(mount_data);
			mount_data = cifs_compose_mount_options(
					cifs_sb->mountdata, full_path + 1,
					referrals, &fake_devname);
			kfree(fake_devname);
			free_dfs_info_array(referrals, num_referrals);

			if (tcon)
				cifs_put_tcon(tcon);
			else if (pSesInfo)
				cifs_put_smb_ses(pSesInfo);

			cleanup_volume_info(&volume_info);
			FreeXid(xid);
			kfree(full_path);
			goto try_mount_again;
		}
#else /* No DFS support, return error on mount */
		rc = -EOPNOTSUPP;
#endif
	}

mount_fail_check:
	/* on error free sesinfo and tcon struct if needed */
	if (rc) {
		if (mount_data != mount_data_global)
			kfree(mount_data);
		/* If find_unc succeeded then rc == 0 so we can not end */
		/* up accidently freeing someone elses tcon struct */
		if (tcon)
			cifs_put_tcon(tcon);
		else if (pSesInfo)
			cifs_put_smb_ses(pSesInfo);
		else
			cifs_put_tcp_session(srvTcp);
		goto out;
	}

	/* volume_info->password is freed above when existing session found
	(in which case it is not needed anymore) but when new sesion is created
	the password ptr is put in the new session structure (in which case the
	password will be freed at unmount time) */
out:
	/* zero out password before freeing */
	cleanup_volume_info(&volume_info);
	FreeXid(xid);
	return rc;
}

static int
CIFSSessSetup(unsigned int xid, struct cifsSesInfo *ses,
	      char session_key[CIFS_SESS_KEY_SIZE],
	      const struct nls_table *nls_codepage)
{
	struct smb_hdr *smb_buffer;
	struct smb_hdr *smb_buffer_response;
	SESSION_SETUP_ANDX *pSMB;
	SESSION_SETUP_ANDX *pSMBr;
	char *bcc_ptr;
	char *user;
	char *domain;
	int rc = 0;
	int remaining_words = 0;
	int bytes_returned = 0;
	int len;
	__u32 capabilities;
	__u16 count;

	cFYI(1, ("In sesssetup"));
	if (ses == NULL)
		return -EINVAL;
	user = ses->userName;
	domain = ses->domainName;
	smb_buffer = cifs_buf_get();

	if (smb_buffer == NULL)
		return -ENOMEM;

	smb_buffer_response = smb_buffer;
	pSMBr = pSMB = (SESSION_SETUP_ANDX *) smb_buffer;

	/* send SMBsessionSetup here */
	header_assemble(smb_buffer, SMB_COM_SESSION_SETUP_ANDX,
			NULL /* no tCon exists yet */ , 13 /* wct */ );

	smb_buffer->Mid = GetNextMid(ses->server);
	pSMB->req_no_secext.AndXCommand = 0xFF;
	pSMB->req_no_secext.MaxBufferSize = cpu_to_le16(ses->server->maxBuf);
	pSMB->req_no_secext.MaxMpxCount = cpu_to_le16(ses->server->maxReq);

	if (ses->server->secMode &
			(SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
		smb_buffer->Flags2 |= SMBFLG2_SECURITY_SIGNATURE;

	capabilities = CAP_LARGE_FILES | CAP_NT_SMBS | CAP_LEVEL_II_OPLOCKS |
		CAP_LARGE_WRITE_X | CAP_LARGE_READ_X;
	if (ses->capabilities & CAP_UNICODE) {
		smb_buffer->Flags2 |= SMBFLG2_UNICODE;
		capabilities |= CAP_UNICODE;
	}
	if (ses->capabilities & CAP_STATUS32) {
		smb_buffer->Flags2 |= SMBFLG2_ERR_STATUS;
		capabilities |= CAP_STATUS32;
	}
	if (ses->capabilities & CAP_DFS) {
		smb_buffer->Flags2 |= SMBFLG2_DFS;
		capabilities |= CAP_DFS;
	}
	pSMB->req_no_secext.Capabilities = cpu_to_le32(capabilities);

	pSMB->req_no_secext.CaseInsensitivePasswordLength =
		cpu_to_le16(CIFS_SESS_KEY_SIZE);

	pSMB->req_no_secext.CaseSensitivePasswordLength =
	    cpu_to_le16(CIFS_SESS_KEY_SIZE);
	bcc_ptr = pByteArea(smb_buffer);
	memcpy(bcc_ptr, (char *) session_key, CIFS_SESS_KEY_SIZE);
	bcc_ptr += CIFS_SESS_KEY_SIZE;
	memcpy(bcc_ptr, (char *) session_key, CIFS_SESS_KEY_SIZE);
	bcc_ptr += CIFS_SESS_KEY_SIZE;

	if (ses->capabilities & CAP_UNICODE) {
		if ((long) bcc_ptr % 2) { /* must be word aligned for Unicode */
			*bcc_ptr = 0;
			bcc_ptr++;
		}
		if (user == NULL)
			bytes_returned = 0; /* skip null user */
		else
			bytes_returned =
				cifs_strtoUCS((__le16 *) bcc_ptr, user, 100,
					nls_codepage);
		/* convert number of 16 bit words to bytes */
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;	/* trailing null */
		if (domain == NULL)
			bytes_returned =
			    cifs_strtoUCS((__le16 *) bcc_ptr,
					  "CIFS_LINUX_DOM", 32, nls_codepage);
		else
			bytes_returned =
			    cifs_strtoUCS((__le16 *) bcc_ptr, domain, 64,
					  nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;
		bytes_returned =
		    cifs_strtoUCS((__le16 *) bcc_ptr, "Linux version ",
				  32, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bytes_returned =
		    cifs_strtoUCS((__le16 *) bcc_ptr, utsname()->release,
				  32, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;
		bytes_returned =
		    cifs_strtoUCS((__le16 *) bcc_ptr, CIFS_NETWORK_OPSYS,
				  64, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;
	} else {
		if (user != NULL) {
		    strncpy(bcc_ptr, user, 200);
		    bcc_ptr += strnlen(user, 200);
		}
		*bcc_ptr = 0;
		bcc_ptr++;
		if (domain == NULL) {
			strcpy(bcc_ptr, "CIFS_LINUX_DOM");
			bcc_ptr += strlen("CIFS_LINUX_DOM") + 1;
		} else {
			strncpy(bcc_ptr, domain, 64);
			bcc_ptr += strnlen(domain, 64);
			*bcc_ptr = 0;
			bcc_ptr++;
		}
		strcpy(bcc_ptr, "Linux version ");
		bcc_ptr += strlen("Linux version ");
		strcpy(bcc_ptr, utsname()->release);
		bcc_ptr += strlen(utsname()->release) + 1;
		strcpy(bcc_ptr, CIFS_NETWORK_OPSYS);
		bcc_ptr += strlen(CIFS_NETWORK_OPSYS) + 1;
	}
	count = (long) bcc_ptr - (long) pByteArea(smb_buffer);
	smb_buffer->smb_buf_length += count;
	pSMB->req_no_secext.ByteCount = cpu_to_le16(count);

	rc = SendReceive(xid, ses, smb_buffer, smb_buffer_response,
			 &bytes_returned, CIFS_LONG_OP);
	if (rc) {
/* rc = map_smb_to_linux_error(smb_buffer_response); now done in SendReceive */
	} else if ((smb_buffer_response->WordCount == 3)
		   || (smb_buffer_response->WordCount == 4)) {
		__u16 action = le16_to_cpu(pSMBr->resp.Action);
		__u16 blob_len = le16_to_cpu(pSMBr->resp.SecurityBlobLength);
		if (action & GUEST_LOGIN)
			cFYI(1, ("Guest login")); /* BB mark SesInfo struct? */
		ses->Suid = smb_buffer_response->Uid; /* UID left in wire format
							 (little endian) */
		cFYI(1, ("UID = %d ", ses->Suid));
	/* response can have either 3 or 4 word count - Samba sends 3 */
		bcc_ptr = pByteArea(smb_buffer_response);
		if ((pSMBr->resp.hdr.WordCount == 3)
		    || ((pSMBr->resp.hdr.WordCount == 4)
			&& (blob_len < pSMBr->resp.ByteCount))) {
			if (pSMBr->resp.hdr.WordCount == 4)
				bcc_ptr += blob_len;

			if (smb_buffer->Flags2 & SMBFLG2_UNICODE) {
				if ((long) (bcc_ptr) % 2) {
					remaining_words =
					    (BCC(smb_buffer_response) - 1) / 2;
					/* Unicode strings must be word
					   aligned */
					bcc_ptr++;
				} else {
					remaining_words =
						BCC(smb_buffer_response) / 2;
				}
				len =
				    UniStrnlen((wchar_t *) bcc_ptr,
					       remaining_words - 1);
/* We look for obvious messed up bcc or strings in response so we do not go off
   the end since (at least) WIN2K and Windows XP have a major bug in not null
   terminating last Unicode string in response  */
				kfree(ses->serverOS);
				ses->serverOS = kzalloc(2 * (len + 1),
							GFP_KERNEL);
				if (ses->serverOS == NULL)
					goto sesssetup_nomem;
				cifs_strfromUCS_le(ses->serverOS,
						   (__le16 *)bcc_ptr,
						   len, nls_codepage);
				bcc_ptr += 2 * (len + 1);
				remaining_words -= len + 1;
				ses->serverOS[2 * len] = 0;
				ses->serverOS[1 + (2 * len)] = 0;
				if (remaining_words > 0) {
					len = UniStrnlen((wchar_t *)bcc_ptr,
							 remaining_words-1);
					kfree(ses->serverNOS);
					ses->serverNOS = kzalloc(2 * (len + 1),
								 GFP_KERNEL);
					if (ses->serverNOS == NULL)
						goto sesssetup_nomem;
					cifs_strfromUCS_le(ses->serverNOS,
							   (__le16 *)bcc_ptr,
							   len, nls_codepage);
					bcc_ptr += 2 * (len + 1);
					ses->serverNOS[2 * len] = 0;
					ses->serverNOS[1 + (2 * len)] = 0;
					if (strncmp(ses->serverNOS,
						"NT LAN Manager 4", 16) == 0) {
						cFYI(1, ("NT4 server"));
						ses->flags |= CIFS_SES_NT4;
					}
					remaining_words -= len + 1;
					if (remaining_words > 0) {
						len = UniStrnlen((wchar_t *) bcc_ptr, remaining_words);
				/* last string is not always null terminated
				   (for e.g. for Windows XP & 2000) */
						kfree(ses->serverDomain);
						ses->serverDomain =
						    kzalloc(2*(len+1),
							    GFP_KERNEL);
						if (ses->serverDomain == NULL)
							goto sesssetup_nomem;
						cifs_strfromUCS_le(ses->serverDomain,
							(__le16 *)bcc_ptr,
							len, nls_codepage);
						bcc_ptr += 2 * (len + 1);
						ses->serverDomain[2*len] = 0;
						ses->serverDomain[1+(2*len)] = 0;
					} else { /* else no more room so create
						  dummy domain string */
						kfree(ses->serverDomain);
						ses->serverDomain =
							kzalloc(2, GFP_KERNEL);
					}
				} else { /* no room so create dummy domain
					    and NOS string */

					/* if these kcallocs fail not much we
					   can do, but better to not fail the
					   sesssetup itself */
					kfree(ses->serverDomain);
					ses->serverDomain =
					    kzalloc(2, GFP_KERNEL);
					kfree(ses->serverNOS);
					ses->serverNOS =
					    kzalloc(2, GFP_KERNEL);
				}
			} else {	/* ASCII */
				len = strnlen(bcc_ptr, 1024);
				if (((long) bcc_ptr + len) - (long)
				    pByteArea(smb_buffer_response)
					    <= BCC(smb_buffer_response)) {
					kfree(ses->serverOS);
					ses->serverOS = kzalloc(len + 1,
								GFP_KERNEL);
					if (ses->serverOS == NULL)
						goto sesssetup_nomem;
					strncpy(ses->serverOS, bcc_ptr, len);

					bcc_ptr += len;
					/* null terminate the string */
					bcc_ptr[0] = 0;
					bcc_ptr++;

					len = strnlen(bcc_ptr, 1024);
					kfree(ses->serverNOS);
					ses->serverNOS = kzalloc(len + 1,
								 GFP_KERNEL);
					if (ses->serverNOS == NULL)
						goto sesssetup_nomem;
					strncpy(ses->serverNOS, bcc_ptr, len);
					bcc_ptr += len;
					bcc_ptr[0] = 0;
					bcc_ptr++;

					len = strnlen(bcc_ptr, 1024);
					kfree(ses->serverDomain);
					ses->serverDomain = kzalloc(len + 1,
								    GFP_KERNEL);
					if (ses->serverDomain == NULL)
						goto sesssetup_nomem;
					strncpy(ses->serverDomain, bcc_ptr,
						len);
					bcc_ptr += len;
					bcc_ptr[0] = 0;
					bcc_ptr++;
				} else
					cFYI(1,
					     ("Variable field of length %d "
						"extends beyond end of smb ",
					      len));
			}
		} else {
			cERROR(1, ("Security Blob Length extends beyond "
				"end of SMB"));
		}
	} else {
		cERROR(1, ("Invalid Word count %d: ",
			smb_buffer_response->WordCount));
		rc = -EIO;
	}
sesssetup_nomem:	/* do not return an error on nomem for the info strings,
			   since that could make reconnection harder, and
			   reconnection might be needed to free memory */
	cifs_buf_release(smb_buffer);

	return rc;
}

static int
CIFSNTLMSSPNegotiateSessSetup(unsigned int xid,
			      struct cifsSesInfo *ses, bool *pNTLMv2_flag,
			      const struct nls_table *nls_codepage)
{
	struct smb_hdr *smb_buffer;
	struct smb_hdr *smb_buffer_response;
	SESSION_SETUP_ANDX *pSMB;
	SESSION_SETUP_ANDX *pSMBr;
	char *bcc_ptr;
	char *domain;
	int rc = 0;
	int remaining_words = 0;
	int bytes_returned = 0;
	int len;
	int SecurityBlobLength = sizeof(NEGOTIATE_MESSAGE);
	PNEGOTIATE_MESSAGE SecurityBlob;
	PCHALLENGE_MESSAGE SecurityBlob2;
	__u32 negotiate_flags, capabilities;
	__u16 count;

	cFYI(1, ("In NTLMSSP sesssetup (negotiate)"));
	if (ses == NULL)
		return -EINVAL;
	domain = ses->domainName;
	*pNTLMv2_flag = false;
	smb_buffer = cifs_buf_get();
	if (smb_buffer == NULL) {
		return -ENOMEM;
	}
	smb_buffer_response = smb_buffer;
	pSMB = (SESSION_SETUP_ANDX *) smb_buffer;
	pSMBr = (SESSION_SETUP_ANDX *) smb_buffer_response;

	/* send SMBsessionSetup here */
	header_assemble(smb_buffer, SMB_COM_SESSION_SETUP_ANDX,
			NULL /* no tCon exists yet */ , 12 /* wct */ );

	smb_buffer->Mid = GetNextMid(ses->server);
	pSMB->req.hdr.Flags2 |= SMBFLG2_EXT_SEC;
	pSMB->req.hdr.Flags |= (SMBFLG_CASELESS | SMBFLG_CANONICAL_PATH_FORMAT);

	pSMB->req.AndXCommand = 0xFF;
	pSMB->req.MaxBufferSize = cpu_to_le16(ses->server->maxBuf);
	pSMB->req.MaxMpxCount = cpu_to_le16(ses->server->maxReq);

	if (ses->server->secMode & (SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
		smb_buffer->Flags2 |= SMBFLG2_SECURITY_SIGNATURE;

	capabilities = CAP_LARGE_FILES | CAP_NT_SMBS | CAP_LEVEL_II_OPLOCKS |
	    CAP_EXTENDED_SECURITY;
	if (ses->capabilities & CAP_UNICODE) {
		smb_buffer->Flags2 |= SMBFLG2_UNICODE;
		capabilities |= CAP_UNICODE;
	}
	if (ses->capabilities & CAP_STATUS32) {
		smb_buffer->Flags2 |= SMBFLG2_ERR_STATUS;
		capabilities |= CAP_STATUS32;
	}
	if (ses->capabilities & CAP_DFS) {
		smb_buffer->Flags2 |= SMBFLG2_DFS;
		capabilities |= CAP_DFS;
	}
	pSMB->req.Capabilities = cpu_to_le32(capabilities);

	bcc_ptr = (char *) &pSMB->req.SecurityBlob;
	SecurityBlob = (PNEGOTIATE_MESSAGE) bcc_ptr;
	strncpy(SecurityBlob->Signature, NTLMSSP_SIGNATURE, 8);
	SecurityBlob->MessageType = NtLmNegotiate;
	negotiate_flags =
	    NTLMSSP_NEGOTIATE_UNICODE | NTLMSSP_NEGOTIATE_OEM |
	    NTLMSSP_REQUEST_TARGET | NTLMSSP_NEGOTIATE_NTLM |
	    NTLMSSP_NEGOTIATE_56 |
	    /* NTLMSSP_NEGOTIATE_ALWAYS_SIGN | */ NTLMSSP_NEGOTIATE_128;
	if (sign_CIFS_PDUs)
		negotiate_flags |= NTLMSSP_NEGOTIATE_SIGN;
/*	if (ntlmv2_support)
		negotiate_flags |= NTLMSSP_NEGOTIATE_NTLMV2;*/
	/* setup pointers to domain name and workstation name */
	bcc_ptr += SecurityBlobLength;

	SecurityBlob->WorkstationName.Buffer = 0;
	SecurityBlob->WorkstationName.Length = 0;
	SecurityBlob->WorkstationName.MaximumLength = 0;

	/* Domain not sent on first Sesssetup in NTLMSSP, instead it is sent
	along with username on auth request (ie the response to challenge) */
	SecurityBlob->DomainName.Buffer = 0;
	SecurityBlob->DomainName.Length = 0;
	SecurityBlob->DomainName.MaximumLength = 0;
	if (ses->capabilities & CAP_UNICODE) {
		if ((long) bcc_ptr % 2) {
			*bcc_ptr = 0;
			bcc_ptr++;
		}

		bytes_returned =
		    cifs_strtoUCS((__le16 *) bcc_ptr, "Linux version ",
				  32, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bytes_returned =
		    cifs_strtoUCS((__le16 *) bcc_ptr, utsname()->release, 32,
				  nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;	/* null terminate Linux version */
		bytes_returned =
		    cifs_strtoUCS((__le16 *) bcc_ptr, CIFS_NETWORK_OPSYS,
				  64, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		*(bcc_ptr + 1) = 0;
		*(bcc_ptr + 2) = 0;
		bcc_ptr += 2;	/* null terminate network opsys string */
		*(bcc_ptr + 1) = 0;
		*(bcc_ptr + 2) = 0;
		bcc_ptr += 2;	/* null domain */
	} else {		/* ASCII */
		strcpy(bcc_ptr, "Linux version ");
		bcc_ptr += strlen("Linux version ");
		strcpy(bcc_ptr, utsname()->release);
		bcc_ptr += strlen(utsname()->release) + 1;
		strcpy(bcc_ptr, CIFS_NETWORK_OPSYS);
		bcc_ptr += strlen(CIFS_NETWORK_OPSYS) + 1;
		bcc_ptr++;	/* empty domain field */
		*bcc_ptr = 0;
	}
	SecurityBlob->NegotiateFlags = cpu_to_le32(negotiate_flags);
	pSMB->req.SecurityBlobLength = cpu_to_le16(SecurityBlobLength);
	count = (long) bcc_ptr - (long) pByteArea(smb_buffer);
	smb_buffer->smb_buf_length += count;
	pSMB->req.ByteCount = cpu_to_le16(count);

	rc = SendReceive(xid, ses, smb_buffer, smb_buffer_response,
			 &bytes_returned, CIFS_LONG_OP);

	if (smb_buffer_response->Status.CifsError ==
	    cpu_to_le32(NT_STATUS_MORE_PROCESSING_REQUIRED))
		rc = 0;

	if (rc) {
/*    rc = map_smb_to_linux_error(smb_buffer_response);  *//* done in SendReceive now */
	} else if ((smb_buffer_response->WordCount == 3)
		   || (smb_buffer_response->WordCount == 4)) {
		__u16 action = le16_to_cpu(pSMBr->resp.Action);
		__u16 blob_len = le16_to_cpu(pSMBr->resp.SecurityBlobLength);

		if (action & GUEST_LOGIN)
			cFYI(1, ("Guest login"));
	/* Do we want to set anything in SesInfo struct when guest login? */

		bcc_ptr = pByteArea(smb_buffer_response);
	/* response can have either 3 or 4 word count - Samba sends 3 */

		SecurityBlob2 = (PCHALLENGE_MESSAGE) bcc_ptr;
		if (SecurityBlob2->MessageType != NtLmChallenge) {
			cFYI(1, ("Unexpected NTLMSSP message type received %d",
			      SecurityBlob2->MessageType));
		} else if (ses) {
			ses->Suid = smb_buffer_response->Uid; /* UID left in le format */
			cFYI(1, ("UID = %d", ses->Suid));
			if ((pSMBr->resp.hdr.WordCount == 3)
			    || ((pSMBr->resp.hdr.WordCount == 4)
				&& (blob_len <
				    pSMBr->resp.ByteCount))) {

				if (pSMBr->resp.hdr.WordCount == 4) {
					bcc_ptr += blob_len;
					cFYI(1, ("Security Blob Length %d",
					      blob_len));
				}

				cFYI(1, ("NTLMSSP Challenge rcvd"));

				memcpy(ses->server->cryptKey,
				       SecurityBlob2->Challenge,
				       CIFS_CRYPTO_KEY_SIZE);
				if (SecurityBlob2->NegotiateFlags &
					cpu_to_le32(NTLMSSP_NEGOTIATE_NTLMV2))
					*pNTLMv2_flag = true;

				if ((SecurityBlob2->NegotiateFlags &
					cpu_to_le32(NTLMSSP_NEGOTIATE_ALWAYS_SIGN))
					|| (sign_CIFS_PDUs > 1))
						ses->server->secMode |=
							SECMODE_SIGN_REQUIRED;
				if ((SecurityBlob2->NegotiateFlags &
					cpu_to_le32(NTLMSSP_NEGOTIATE_SIGN)) && (sign_CIFS_PDUs))
						ses->server->secMode |=
							SECMODE_SIGN_ENABLED;

				if (smb_buffer->Flags2 & SMBFLG2_UNICODE) {
					if ((long) (bcc_ptr) % 2) {
						remaining_words =
						    (BCC(smb_buffer_response)
						     - 1) / 2;
					 /* Must word align unicode strings */
						bcc_ptr++;
					} else {
						remaining_words =
						    BCC
						    (smb_buffer_response) / 2;
					}
					len =
					    UniStrnlen((wchar_t *) bcc_ptr,
						       remaining_words - 1);
/* We look for obvious messed up bcc or strings in response so we do not go off
   the end since (at least) WIN2K and Windows XP have a major bug in not null
   terminating last Unicode string in response  */
					kfree(ses->serverOS);
					ses->serverOS =
					    kzalloc(2 * (len + 1), GFP_KERNEL);
					cifs_strfromUCS_le(ses->serverOS,
							   (__le16 *)
							   bcc_ptr, len,
							   nls_codepage);
					bcc_ptr += 2 * (len + 1);
					remaining_words -= len + 1;
					ses->serverOS[2 * len] = 0;
					ses->serverOS[1 + (2 * len)] = 0;
					if (remaining_words > 0) {
						len = UniStrnlen((wchar_t *)
								 bcc_ptr,
								 remaining_words
								 - 1);
						kfree(ses->serverNOS);
						ses->serverNOS =
						    kzalloc(2 * (len + 1),
							    GFP_KERNEL);
						cifs_strfromUCS_le(ses->
								   serverNOS,
								   (__le16 *)
								   bcc_ptr,
								   len,
								   nls_codepage);
						bcc_ptr += 2 * (len + 1);
						ses->serverNOS[2 * len] = 0;
						ses->serverNOS[1 +
							       (2 * len)] = 0;
						remaining_words -= len + 1;
						if (remaining_words > 0) {
							len = UniStrnlen((wchar_t *) bcc_ptr, remaining_words);
				/* last string not always null terminated
				   (for e.g. for Windows XP & 2000) */
							kfree(ses->serverDomain);
							ses->serverDomain =
							    kzalloc(2 *
								    (len +
								     1),
								    GFP_KERNEL);
							cifs_strfromUCS_le
							    (ses->serverDomain,
							     (__le16 *)bcc_ptr,
							     len, nls_codepage);
							bcc_ptr +=
							    2 * (len + 1);
							ses->serverDomain[2*len]
							    = 0;
							ses->serverDomain
								[1 + (2 * len)]
							    = 0;
						} /* else no more room so create dummy domain string */
						else {
							kfree(ses->serverDomain);
							ses->serverDomain =
							    kzalloc(2,
								    GFP_KERNEL);
						}
					} else {	/* no room so create dummy domain and NOS string */
						kfree(ses->serverDomain);
						ses->serverDomain =
						    kzalloc(2, GFP_KERNEL);
						kfree(ses->serverNOS);
						ses->serverNOS =
						    kzalloc(2, GFP_KERNEL);
					}
				} else {	/* ASCII */
					len = strnlen(bcc_ptr, 1024);
					if (((long) bcc_ptr + len) - (long)
					    pByteArea(smb_buffer_response)
					    <= BCC(smb_buffer_response)) {
						kfree(ses->serverOS);
						ses->serverOS =
						    kzalloc(len + 1,
							    GFP_KERNEL);
						strncpy(ses->serverOS,
							bcc_ptr, len);

						bcc_ptr += len;
						bcc_ptr[0] = 0;	/* null terminate string */
						bcc_ptr++;

						len = strnlen(bcc_ptr, 1024);
						kfree(ses->serverNOS);
						ses->serverNOS =
						    kzalloc(len + 1,
							    GFP_KERNEL);
						strncpy(ses->serverNOS, bcc_ptr, len);
						bcc_ptr += len;
						bcc_ptr[0] = 0;
						bcc_ptr++;

						len = strnlen(bcc_ptr, 1024);
						kfree(ses->serverDomain);
						ses->serverDomain =
						    kzalloc(len + 1,
							    GFP_KERNEL);
						strncpy(ses->serverDomain,
							bcc_ptr, len);
						bcc_ptr += len;
						bcc_ptr[0] = 0;
						bcc_ptr++;
					} else
						cFYI(1,
						     ("field of length %d "
						    "extends beyond end of smb",
						      len));
				}
			} else {
				cERROR(1, ("Security Blob Length extends beyond"
					   " end of SMB"));
			}
		} else {
			cERROR(1, ("No session structure passed in."));
		}
	} else {
		cERROR(1, ("Invalid Word count %d:",
			smb_buffer_response->WordCount));
		rc = -EIO;
	}

	cifs_buf_release(smb_buffer);

	return rc;
}
static int
CIFSNTLMSSPAuthSessSetup(unsigned int xid, struct cifsSesInfo *ses,
			char *ntlm_session_key, bool ntlmv2_flag,
			const struct nls_table *nls_codepage)
{
	struct smb_hdr *smb_buffer;
	struct smb_hdr *smb_buffer_response;
	SESSION_SETUP_ANDX *pSMB;
	SESSION_SETUP_ANDX *pSMBr;
	char *bcc_ptr;
	char *user;
	char *domain;
	int rc = 0;
	int remaining_words = 0;
	int bytes_returned = 0;
	int len;
	int SecurityBlobLength = sizeof(AUTHENTICATE_MESSAGE);
	PAUTHENTICATE_MESSAGE SecurityBlob;
	__u32 negotiate_flags, capabilities;
	__u16 count;

	cFYI(1, ("In NTLMSSPSessSetup (Authenticate)"));
	if (ses == NULL)
		return -EINVAL;
	user = ses->userName;
	domain = ses->domainName;
	smb_buffer = cifs_buf_get();
	if (smb_buffer == NULL) {
		return -ENOMEM;
	}
	smb_buffer_response = smb_buffer;
	pSMB = (SESSION_SETUP_ANDX *)smb_buffer;
	pSMBr = (SESSION_SETUP_ANDX *)smb_buffer_response;

	/* send SMBsessionSetup here */
	header_assemble(smb_buffer, SMB_COM_SESSION_SETUP_ANDX,
			NULL /* no tCon exists yet */ , 12 /* wct */ );

	smb_buffer->Mid = GetNextMid(ses->server);
	pSMB->req.hdr.Flags |= (SMBFLG_CASELESS | SMBFLG_CANONICAL_PATH_FORMAT);
	pSMB->req.hdr.Flags2 |= SMBFLG2_EXT_SEC;
	pSMB->req.AndXCommand = 0xFF;
	pSMB->req.MaxBufferSize = cpu_to_le16(ses->server->maxBuf);
	pSMB->req.MaxMpxCount = cpu_to_le16(ses->server->maxReq);

	pSMB->req.hdr.Uid = ses->Suid;

	if (ses->server->secMode & (SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
		smb_buffer->Flags2 |= SMBFLG2_SECURITY_SIGNATURE;

	capabilities = CAP_LARGE_FILES | CAP_NT_SMBS | CAP_LEVEL_II_OPLOCKS |
			CAP_EXTENDED_SECURITY;
	if (ses->capabilities & CAP_UNICODE) {
		smb_buffer->Flags2 |= SMBFLG2_UNICODE;
		capabilities |= CAP_UNICODE;
	}
	if (ses->capabilities & CAP_STATUS32) {
		smb_buffer->Flags2 |= SMBFLG2_ERR_STATUS;
		capabilities |= CAP_STATUS32;
	}
	if (ses->capabilities & CAP_DFS) {
		smb_buffer->Flags2 |= SMBFLG2_DFS;
		capabilities |= CAP_DFS;
	}
	pSMB->req.Capabilities = cpu_to_le32(capabilities);

	bcc_ptr = (char *)&pSMB->req.SecurityBlob;
	SecurityBlob = (PAUTHENTICATE_MESSAGE)bcc_ptr;
	strncpy(SecurityBlob->Signature, NTLMSSP_SIGNATURE, 8);
	SecurityBlob->MessageType = NtLmAuthenticate;
	bcc_ptr += SecurityBlobLength;
	negotiate_flags = NTLMSSP_NEGOTIATE_UNICODE | NTLMSSP_REQUEST_TARGET |
			NTLMSSP_NEGOTIATE_NTLM | NTLMSSP_NEGOTIATE_TARGET_INFO |
			0x80000000 | NTLMSSP_NEGOTIATE_128;
	if (sign_CIFS_PDUs)
		negotiate_flags |= /* NTLMSSP_NEGOTIATE_ALWAYS_SIGN |*/ NTLMSSP_NEGOTIATE_SIGN;
	if (ntlmv2_flag)
		negotiate_flags |= NTLMSSP_NEGOTIATE_NTLMV2;

/* setup pointers to domain name and workstation name */

	SecurityBlob->WorkstationName.Buffer = 0;
	SecurityBlob->WorkstationName.Length = 0;
	SecurityBlob->WorkstationName.MaximumLength = 0;
	SecurityBlob->SessionKey.Length = 0;
	SecurityBlob->SessionKey.MaximumLength = 0;
	SecurityBlob->SessionKey.Buffer = 0;

	SecurityBlob->LmChallengeResponse.Length = 0;
	SecurityBlob->LmChallengeResponse.MaximumLength = 0;
	SecurityBlob->LmChallengeResponse.Buffer = 0;

	SecurityBlob->NtChallengeResponse.Length =
	    cpu_to_le16(CIFS_SESS_KEY_SIZE);
	SecurityBlob->NtChallengeResponse.MaximumLength =
	    cpu_to_le16(CIFS_SESS_KEY_SIZE);
	memcpy(bcc_ptr, ntlm_session_key, CIFS_SESS_KEY_SIZE);
	SecurityBlob->NtChallengeResponse.Buffer =
	    cpu_to_le32(SecurityBlobLength);
	SecurityBlobLength += CIFS_SESS_KEY_SIZE;
	bcc_ptr += CIFS_SESS_KEY_SIZE;

	if (ses->capabilities & CAP_UNICODE) {
		if (domain == NULL) {
			SecurityBlob->DomainName.Buffer = 0;
			SecurityBlob->DomainName.Length = 0;
			SecurityBlob->DomainName.MaximumLength = 0;
		} else {
			__u16 ln = cifs_strtoUCS((__le16 *) bcc_ptr, domain, 64,
					  nls_codepage);
			ln *= 2;
			SecurityBlob->DomainName.MaximumLength =
			    cpu_to_le16(ln);
			SecurityBlob->DomainName.Buffer =
			    cpu_to_le32(SecurityBlobLength);
			bcc_ptr += ln;
			SecurityBlobLength += ln;
			SecurityBlob->DomainName.Length = cpu_to_le16(ln);
		}
		if (user == NULL) {
			SecurityBlob->UserName.Buffer = 0;
			SecurityBlob->UserName.Length = 0;
			SecurityBlob->UserName.MaximumLength = 0;
		} else {
			__u16 ln = cifs_strtoUCS((__le16 *) bcc_ptr, user, 64,
					  nls_codepage);
			ln *= 2;
			SecurityBlob->UserName.MaximumLength =
			    cpu_to_le16(ln);
			SecurityBlob->UserName.Buffer =
			    cpu_to_le32(SecurityBlobLength);
			bcc_ptr += ln;
			SecurityBlobLength += ln;
			SecurityBlob->UserName.Length = cpu_to_le16(ln);
		}

		/* SecurityBlob->WorkstationName.Length =
		 cifs_strtoUCS((__le16 *) bcc_ptr, "AMACHINE",64, nls_codepage);
		   SecurityBlob->WorkstationName.Length *= 2;
		   SecurityBlob->WorkstationName.MaximumLength =
			cpu_to_le16(SecurityBlob->WorkstationName.Length);
		   SecurityBlob->WorkstationName.Buffer =
				 cpu_to_le32(SecurityBlobLength);
		   bcc_ptr += SecurityBlob->WorkstationName.Length;
		   SecurityBlobLength += SecurityBlob->WorkstationName.Length;
		   SecurityBlob->WorkstationName.Length =
			cpu_to_le16(SecurityBlob->WorkstationName.Length);  */

		if ((long) bcc_ptr % 2) {
			*bcc_ptr = 0;
			bcc_ptr++;
		}
		bytes_returned =
		    cifs_strtoUCS((__le16 *) bcc_ptr, "Linux version ",
				  32, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bytes_returned =
		    cifs_strtoUCS((__le16 *) bcc_ptr, utsname()->release, 32,
				  nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;	/* null term version string */
		bytes_returned =
		    cifs_strtoUCS((__le16 *) bcc_ptr, CIFS_NETWORK_OPSYS,
				  64, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		*(bcc_ptr + 1) = 0;
		*(bcc_ptr + 2) = 0;
		bcc_ptr += 2;	/* null terminate network opsys string */
		*(bcc_ptr + 1) = 0;
		*(bcc_ptr + 2) = 0;
		bcc_ptr += 2;	/* null domain */
	} else {		/* ASCII */
		if (domain == NULL) {
			SecurityBlob->DomainName.Buffer = 0;
			SecurityBlob->DomainName.Length = 0;
			SecurityBlob->DomainName.MaximumLength = 0;
		} else {
			__u16 ln;
			negotiate_flags |= NTLMSSP_NEGOTIATE_DOMAIN_SUPPLIED;
			strncpy(bcc_ptr, domain, 63);
			ln = strnlen(domain, 64);
			SecurityBlob->DomainName.MaximumLength =
			    cpu_to_le16(ln);
			SecurityBlob->DomainName.Buffer =
			    cpu_to_le32(SecurityBlobLength);
			bcc_ptr += ln;
			SecurityBlobLength += ln;
			SecurityBlob->DomainName.Length = cpu_to_le16(ln);
		}
		if (user == NULL) {
			SecurityBlob->UserName.Buffer = 0;
			SecurityBlob->UserName.Length = 0;
			SecurityBlob->UserName.MaximumLength = 0;
		} else {
			__u16 ln;
			strncpy(bcc_ptr, user, 63);
			ln = strnlen(user, 64);
			SecurityBlob->UserName.MaximumLength = cpu_to_le16(ln);
			SecurityBlob->UserName.Buffer =
						cpu_to_le32(SecurityBlobLength);
			bcc_ptr += ln;
			SecurityBlobLength += ln;
			SecurityBlob->UserName.Length = cpu_to_le16(ln);
		}
		/* BB fill in our workstation name if known BB */

		strcpy(bcc_ptr, "Linux version ");
		bcc_ptr += strlen("Linux version ");
		strcpy(bcc_ptr, utsname()->release);
		bcc_ptr += strlen(utsname()->release) + 1;
		strcpy(bcc_ptr, CIFS_NETWORK_OPSYS);
		bcc_ptr += strlen(CIFS_NETWORK_OPSYS) + 1;
		bcc_ptr++;	/* null domain */
		*bcc_ptr = 0;
	}
	SecurityBlob->NegotiateFlags = cpu_to_le32(negotiate_flags);
	pSMB->req.SecurityBlobLength = cpu_to_le16(SecurityBlobLength);
	count = (long) bcc_ptr - (long) pByteArea(smb_buffer);
	smb_buffer->smb_buf_length += count;
	pSMB->req.ByteCount = cpu_to_le16(count);

	rc = SendReceive(xid, ses, smb_buffer, smb_buffer_response,
			 &bytes_returned, CIFS_LONG_OP);
	if (rc) {
/*   rc = map_smb_to_linux_error(smb_buffer_response) done in SendReceive now */
	} else if ((smb_buffer_response->WordCount == 3) ||
		   (smb_buffer_response->WordCount == 4)) {
		__u16 action = le16_to_cpu(pSMBr->resp.Action);
		__u16 blob_len = le16_to_cpu(pSMBr->resp.SecurityBlobLength);
		if (action & GUEST_LOGIN)
			cFYI(1, ("Guest login")); /* BB Should we set anything
							 in SesInfo struct ? */
/*		if (SecurityBlob2->MessageType != NtLm??) {
			cFYI("Unexpected message type on auth response is %d"));
		} */

		if (ses) {
			cFYI(1,
			     ("Check challenge UID %d vs auth response UID %d",
			      ses->Suid, smb_buffer_response->Uid));
			/* UID left in wire format */
			ses->Suid = smb_buffer_response->Uid;
			bcc_ptr = pByteArea(smb_buffer_response);
		/* response can have either 3 or 4 word count - Samba sends 3 */
			if ((pSMBr->resp.hdr.WordCount == 3)
			    || ((pSMBr->resp.hdr.WordCount == 4)
				&& (blob_len <
				    pSMBr->resp.ByteCount))) {
				if (pSMBr->resp.hdr.WordCount == 4) {
					bcc_ptr +=
					    blob_len;
					cFYI(1,
					     ("Security Blob Length %d ",
					      blob_len));
				}

				cFYI(1,
				     ("NTLMSSP response to Authenticate "));

				if (smb_buffer->Flags2 & SMBFLG2_UNICODE) {
					if ((long) (bcc_ptr) % 2) {
						remaining_words =
						    (BCC(smb_buffer_response)
						     - 1) / 2;
						bcc_ptr++;	/* Unicode strings must be word aligned */
					} else {
						remaining_words = BCC(smb_buffer_response) / 2;
					}
					len = UniStrnlen((wchar_t *) bcc_ptr,
							remaining_words - 1);
/* We look for obvious messed up bcc or strings in response so we do not go off
  the end since (at least) WIN2K and Windows XP have a major bug in not null
  terminating last Unicode string in response  */
					kfree(ses->serverOS);
					ses->serverOS =
					    kzalloc(2 * (len + 1), GFP_KERNEL);
					cifs_strfromUCS_le(ses->serverOS,
							   (__le16 *)
							   bcc_ptr, len,
							   nls_codepage);
					bcc_ptr += 2 * (len + 1);
					remaining_words -= len + 1;
					ses->serverOS[2 * len] = 0;
					ses->serverOS[1 + (2 * len)] = 0;
					if (remaining_words > 0) {
						len = UniStrnlen((wchar_t *)
								 bcc_ptr,
								 remaining_words
								 - 1);
						kfree(ses->serverNOS);
						ses->serverNOS =
						    kzalloc(2 * (len + 1),
							    GFP_KERNEL);
						cifs_strfromUCS_le(ses->
								   serverNOS,
								   (__le16 *)
								   bcc_ptr,
								   len,
								   nls_codepage);
						bcc_ptr += 2 * (len + 1);
						ses->serverNOS[2 * len] = 0;
						ses->serverNOS[1+(2*len)] = 0;
						remaining_words -= len + 1;
						if (remaining_words > 0) {
							len = UniStrnlen((wchar_t *) bcc_ptr, remaining_words);
     /* last string not always null terminated (e.g. for Windows XP & 2000) */
							kfree(ses->serverDomain);
							ses->serverDomain =
							    kzalloc(2 *
								    (len +
								     1),
								    GFP_KERNEL);
							cifs_strfromUCS_le
							    (ses->
							     serverDomain,
							     (__le16 *)
							     bcc_ptr, len,
							     nls_codepage);
							bcc_ptr +=
							    2 * (len + 1);
							ses->
							    serverDomain[2
									 * len]
							    = 0;
							ses->
							    serverDomain[1
									 +
									 (2
									  *
									  len)]
							    = 0;
						} /* else no more room so create dummy domain string */
						else {
							kfree(ses->serverDomain);
							ses->serverDomain = kzalloc(2,GFP_KERNEL);
						}
					} else {  /* no room so create dummy domain and NOS string */
						kfree(ses->serverDomain);
						ses->serverDomain = kzalloc(2, GFP_KERNEL);
						kfree(ses->serverNOS);
						ses->serverNOS = kzalloc(2, GFP_KERNEL);
					}
				} else {	/* ASCII */
					len = strnlen(bcc_ptr, 1024);
					if (((long) bcc_ptr + len) -
					   (long) pByteArea(smb_buffer_response)
						<= BCC(smb_buffer_response)) {
						kfree(ses->serverOS);
						ses->serverOS = kzalloc(len + 1, GFP_KERNEL);
						strncpy(ses->serverOS,bcc_ptr, len);

						bcc_ptr += len;
						bcc_ptr[0] = 0;	/* null terminate the string */
						bcc_ptr++;

						len = strnlen(bcc_ptr, 1024);
						kfree(ses->serverNOS);
						ses->serverNOS = kzalloc(len+1,
								    GFP_KERNEL);
						strncpy(ses->serverNOS,
							bcc_ptr, len);
						bcc_ptr += len;
						bcc_ptr[0] = 0;
						bcc_ptr++;

						len = strnlen(bcc_ptr, 1024);
						kfree(ses->serverDomain);
						ses->serverDomain =
								kzalloc(len+1,
								    GFP_KERNEL);
						strncpy(ses->serverDomain,
							bcc_ptr, len);
						bcc_ptr += len;
						bcc_ptr[0] = 0;
						bcc_ptr++;
					} else
						cFYI(1, ("field of length %d "
						   "extends beyond end of smb ",
						      len));
				}
			} else {
				cERROR(1, ("Security Blob extends beyond end "
					"of SMB"));
			}
		} else {
			cERROR(1, ("No session structure passed in."));
		}
	} else {
		cERROR(1, ("Invalid Word count %d: ",
			smb_buffer_response->WordCount));
		rc = -EIO;
	}

	cifs_buf_release(smb_buffer);

	return rc;
}

int
CIFSTCon(unsigned int xid, struct cifsSesInfo *ses,
	 const char *tree, struct cifsTconInfo *tcon,
	 const struct nls_table *nls_codepage)
{
	struct smb_hdr *smb_buffer;
	struct smb_hdr *smb_buffer_response;
	TCONX_REQ *pSMB;
	TCONX_RSP *pSMBr;
	unsigned char *bcc_ptr;
	int rc = 0;
	int length;
	__u16 count;

	if (ses == NULL)
		return -EIO;

	smb_buffer = cifs_buf_get();
	if (smb_buffer == NULL) {
		return -ENOMEM;
	}
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
	if ((ses->server->secMode) & SECMODE_USER) {
		pSMB->PasswordLength = cpu_to_le16(1);	/* minimum */
		*bcc_ptr = 0; /* password is null byte */
		bcc_ptr++;              /* skip password */
		/* already aligned so no need to do it below */
	} else {
		pSMB->PasswordLength = cpu_to_le16(CIFS_SESS_KEY_SIZE);
		/* BB FIXME add code to fail this if NTLMv2 or Kerberos
		   specified as required (when that support is added to
		   the vfs in the future) as only NTLM or the much
		   weaker LANMAN (which we do not send by default) is accepted
		   by Samba (not sure whether other servers allow
		   NTLMv2 password here) */
#ifdef CONFIG_CIFS_WEAK_PW_HASH
		if ((extended_security & CIFSSEC_MAY_LANMAN) &&
		    (ses->server->secType == LANMAN))
			calc_lanman_hash(tcon->password, ses->server->cryptKey,
					 ses->server->secMode &
					    SECMODE_PW_ENCRYPT ? true : false,
					 bcc_ptr);
		else
#endif /* CIFS_WEAK_PW_HASH */
		SMBNTencrypt(tcon->password, ses->server->cryptKey,
			     bcc_ptr);

		bcc_ptr += CIFS_SESS_KEY_SIZE;
		if (ses->capabilities & CAP_UNICODE) {
			/* must align unicode strings */
			*bcc_ptr = 0; /* null byte password */
			bcc_ptr++;
		}
	}

	if (ses->server->secMode &
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
	pSMB->hdr.smb_buf_length += count;
	pSMB->ByteCount = cpu_to_le16(count);

	rc = SendReceive(xid, ses, smb_buffer, smb_buffer_response, &length,
			 CIFS_STD_OP);

	/* if (rc) rc = map_smb_to_linux_error(smb_buffer_response); */
	/* above now done in SendReceive */
	if ((rc == 0) && (tcon != NULL)) {
		tcon->tidStatus = CifsGood;
		tcon->need_reconnect = false;
		tcon->tid = smb_buffer_response->Tid;
		bcc_ptr = pByteArea(smb_buffer_response);
		length = strnlen(bcc_ptr, BCC(smb_buffer_response) - 2);
		/* skip service field (NB: this field is always ASCII) */
		if (length == 3) {
			if ((bcc_ptr[0] == 'I') && (bcc_ptr[1] == 'P') &&
			    (bcc_ptr[2] == 'C')) {
				cFYI(1, ("IPC connection"));
				tcon->ipc = 1;
			}
		} else if (length == 2) {
			if ((bcc_ptr[0] == 'A') && (bcc_ptr[1] == ':')) {
				/* the most common case */
				cFYI(1, ("disk share connection"));
			}
		}
		bcc_ptr += length + 1;
		strncpy(tcon->treeName, tree, MAX_TREE_SIZE);
		if (smb_buffer->Flags2 & SMBFLG2_UNICODE) {
			length = UniStrnlen((wchar_t *) bcc_ptr, 512);
			if ((bcc_ptr + (2 * length)) -
			     pByteArea(smb_buffer_response) <=
			    BCC(smb_buffer_response)) {
				kfree(tcon->nativeFileSystem);
				tcon->nativeFileSystem =
				    kzalloc(2*(length + 1), GFP_KERNEL);
				if (tcon->nativeFileSystem)
					cifs_strfromUCS_le(
						tcon->nativeFileSystem,
						(__le16 *) bcc_ptr,
						length, nls_codepage);
				bcc_ptr += 2 * length;
				bcc_ptr[0] = 0;	/* null terminate the string */
				bcc_ptr[1] = 0;
				bcc_ptr += 2;
			}
			/* else do not bother copying these information fields*/
		} else {
			length = strnlen(bcc_ptr, 1024);
			if ((bcc_ptr + length) -
			    pByteArea(smb_buffer_response) <=
			    BCC(smb_buffer_response)) {
				kfree(tcon->nativeFileSystem);
				tcon->nativeFileSystem =
				    kzalloc(length + 1, GFP_KERNEL);
				if (tcon->nativeFileSystem)
					strncpy(tcon->nativeFileSystem, bcc_ptr,
						length);
			}
			/* else do not bother copying these information fields*/
		}
		if ((smb_buffer_response->WordCount == 3) ||
			 (smb_buffer_response->WordCount == 7))
			/* field is in same location */
			tcon->Flags = le16_to_cpu(pSMBr->OptionalSupport);
		else
			tcon->Flags = 0;
		cFYI(1, ("Tcon flags: 0x%x ", tcon->Flags));
	} else if ((rc == 0) && tcon == NULL) {
		/* all we need to save for IPC$ connection */
		ses->ipc_tid = smb_buffer_response->Tid;
	}

	cifs_buf_release(smb_buffer);
	return rc;
}

int
cifs_umount(struct super_block *sb, struct cifs_sb_info *cifs_sb)
{
	int rc = 0;
	char *tmp;

	if (cifs_sb->tcon)
		cifs_put_tcon(cifs_sb->tcon);

	cifs_sb->tcon = NULL;
	tmp = cifs_sb->prepath;
	cifs_sb->prepathlen = 0;
	cifs_sb->prepath = NULL;
	kfree(tmp);

	return rc;
}

int cifs_setup_session(unsigned int xid, struct cifsSesInfo *pSesInfo,
					   struct nls_table *nls_info)
{
	int rc = 0;
	char ntlm_session_key[CIFS_SESS_KEY_SIZE];
	bool ntlmv2_flag = false;
	int first_time = 0;
	struct TCP_Server_Info *server = pSesInfo->server;

	/* what if server changes its buffer size after dropping the session? */
	if (server->maxBuf == 0) /* no need to send on reconnect */ {
		rc = CIFSSMBNegotiate(xid, pSesInfo);
		if (rc == -EAGAIN) {
			/* retry only once on 1st time connection */
			rc = CIFSSMBNegotiate(xid, pSesInfo);
			if (rc == -EAGAIN)
				rc = -EHOSTDOWN;
		}
		if (rc == 0) {
			spin_lock(&GlobalMid_Lock);
			if (server->tcpStatus != CifsExiting)
				server->tcpStatus = CifsGood;
			else
				rc = -EHOSTDOWN;
			spin_unlock(&GlobalMid_Lock);

		}
		first_time = 1;
	}

	if (rc)
		goto ss_err_exit;

	pSesInfo->flags = 0;
	pSesInfo->capabilities = server->capabilities;
	if (linuxExtEnabled == 0)
		pSesInfo->capabilities &= (~CAP_UNIX);
	/*	pSesInfo->sequence_number = 0;*/
	cFYI(1, ("Security Mode: 0x%x Capabilities: 0x%x TimeAdjust: %d",
		 server->secMode, server->capabilities, server->timeAdj));

	if (experimEnabled < 2)
		rc = CIFS_SessSetup(xid, pSesInfo, first_time, nls_info);
	else if (extended_security
			&& (pSesInfo->capabilities & CAP_EXTENDED_SECURITY)
			&& (server->secType == NTLMSSP)) {
		rc = -EOPNOTSUPP;
	} else if (extended_security
			&& (pSesInfo->capabilities & CAP_EXTENDED_SECURITY)
			&& (server->secType == RawNTLMSSP)) {
		cFYI(1, ("NTLMSSP sesssetup"));
		rc = CIFSNTLMSSPNegotiateSessSetup(xid, pSesInfo, &ntlmv2_flag,
						   nls_info);
		if (!rc) {
			if (ntlmv2_flag) {
				char *v2_response;
				cFYI(1, ("more secure NTLM ver2 hash"));
				if (CalcNTLMv2_partial_mac_key(pSesInfo,
								nls_info)) {
					rc = -ENOMEM;
					goto ss_err_exit;
				} else
					v2_response = kmalloc(16 + 64 /* blob*/,
								GFP_KERNEL);
				if (v2_response) {
					CalcNTLMv2_response(pSesInfo,
								v2_response);
				/*	if (first_time)
						cifs_calculate_ntlmv2_mac_key */
					kfree(v2_response);
					/* BB Put dummy sig in SessSetup PDU? */
				} else {
					rc = -ENOMEM;
					goto ss_err_exit;
				}

			} else {
				SMBNTencrypt(pSesInfo->password,
					     server->cryptKey,
					     ntlm_session_key);

				if (first_time)
					cifs_calculate_mac_key(
					     &server->mac_signing_key,
					     ntlm_session_key,
					     pSesInfo->password);
			}
			/* for better security the weaker lanman hash not sent
			   in AuthSessSetup so we no longer calculate it */

			rc = CIFSNTLMSSPAuthSessSetup(xid, pSesInfo,
						      ntlm_session_key,
						      ntlmv2_flag,
						      nls_info);
		}
	} else { /* old style NTLM 0.12 session setup */
		SMBNTencrypt(pSesInfo->password, server->cryptKey,
			     ntlm_session_key);

		if (first_time)
			cifs_calculate_mac_key(&server->mac_signing_key,
						ntlm_session_key,
						pSesInfo->password);

		rc = CIFSSessSetup(xid, pSesInfo, ntlm_session_key, nls_info);
	}
	if (rc) {
		cERROR(1, ("Send error in SessSetup = %d", rc));
	} else {
		cFYI(1, ("CIFS Session Established successfully"));
			spin_lock(&GlobalMid_Lock);
			pSesInfo->status = CifsGood;
			pSesInfo->need_reconnect = false;
			spin_unlock(&GlobalMid_Lock);
	}

ss_err_exit:
	return rc;
}

