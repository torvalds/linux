/*
 *   fs/cifs/cifssmb.c
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2005
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   Contains the routines for constructing the SMB PDUs themselves
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

 /* SMB/CIFS PDU handling routines here - except for leftovers in connect.c   */
 /* These are mostly routines that operate on a pathname, or on a tree id     */
 /* (mounted volume), but there are eight handle based routines which must be */
 /* treated slightly different for reconnection purposes since we never want  */
 /* to reuse a stale file handle and the caller knows the file handle */

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/vfs.h>
#include <linux/posix_acl_xattr.h>
#include <asm/uaccess.h>
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"

#ifdef CONFIG_CIFS_POSIX
static struct {
	int index;
	char *name;
} protocols[] = {
	{CIFS_PROT, "\2NT LM 0.12"}, 
	{CIFS_PROT, "\2POSIX 2"},
	{BAD_PROT, "\2"}
};
#else
static struct {
	int index;
	char *name;
} protocols[] = {
	{CIFS_PROT, "\2NT LM 0.12"}, 
	{BAD_PROT, "\2"}
};
#endif


/* Mark as invalid, all open files on tree connections since they
   were closed when session to server was lost */
static void mark_open_files_invalid(struct cifsTconInfo * pTcon)
{
	struct cifsFileInfo *open_file = NULL;
	struct list_head * tmp;
	struct list_head * tmp1;

/* list all files open on tree connection and mark them invalid */
	write_lock(&GlobalSMBSeslock);
	list_for_each_safe(tmp, tmp1, &pTcon->openFileList) {
		open_file = list_entry(tmp,struct cifsFileInfo, tlist);
		if(open_file) {
			open_file->invalidHandle = TRUE;
		}
	}
	write_unlock(&GlobalSMBSeslock);
	/* BB Add call to invalidate_inodes(sb) for all superblocks mounted
	   to this tcon */
}

/* If the return code is zero, this function must fill in request_buf pointer */
static int
small_smb_init(int smb_command, int wct, struct cifsTconInfo *tcon,
	 void **request_buf /* returned */)
{
	int rc = 0;

	/* SMBs NegProt, SessSetup, uLogoff do not have tcon yet so
	   check for tcp and smb session status done differently
	   for those three - in the calling routine */
	if(tcon) {
		if((tcon->ses) && (tcon->ses->status != CifsExiting) &&
				  (tcon->ses->server)){
			struct nls_table *nls_codepage;
				/* Give Demultiplex thread up to 10 seconds to 
				   reconnect, should be greater than cifs socket
				   timeout which is 7 seconds */
			while(tcon->ses->server->tcpStatus == CifsNeedReconnect) {
				wait_event_interruptible_timeout(tcon->ses->server->response_q,
					(tcon->ses->server->tcpStatus == CifsGood), 10 * HZ);
				if(tcon->ses->server->tcpStatus == CifsNeedReconnect) {
					/* on "soft" mounts we wait once */
					if((tcon->retry == FALSE) || 
					   (tcon->ses->status == CifsExiting)) {
						cFYI(1,("gave up waiting on reconnect in smb_init"));
						return -EHOSTDOWN;
					} /* else "hard" mount - keep retrying
					     until process is killed or server
					     comes back on-line */
				} else /* TCP session is reestablished now */
					break;
				 
			}
			
			nls_codepage = load_nls_default();
		/* need to prevent multiple threads trying to
		simultaneously reconnect the same SMB session */
			down(&tcon->ses->sesSem);
			if(tcon->ses->status == CifsNeedReconnect)
				rc = cifs_setup_session(0, tcon->ses, 
							nls_codepage);
			if(!rc && (tcon->tidStatus == CifsNeedReconnect)) {
				mark_open_files_invalid(tcon);
				rc = CIFSTCon(0, tcon->ses, tcon->treeName, tcon
					, nls_codepage);
				up(&tcon->ses->sesSem);
				/* BB FIXME add code to check if wsize needs
				   update due to negotiated smb buffer size
				   shrinking */
				if(rc == 0)
					atomic_inc(&tconInfoReconnectCount);

				cFYI(1, ("reconnect tcon rc = %d", rc));
				/* Removed call to reopen open files here - 
				   it is safer (and faster) to reopen files
				   one at a time as needed in read and write */

				/* Check if handle based operation so we 
				   know whether we can continue or not without
				   returning to caller to reset file handle */
				switch(smb_command) {
					case SMB_COM_READ_ANDX:
					case SMB_COM_WRITE_ANDX:
					case SMB_COM_CLOSE:
					case SMB_COM_FIND_CLOSE2:
					case SMB_COM_LOCKING_ANDX: {
						unload_nls(nls_codepage);
						return -EAGAIN;
					}
				}
			} else {
				up(&tcon->ses->sesSem);
			}
			unload_nls(nls_codepage);

		} else {
			return -EIO;
		}
	}
	if(rc)
		return rc;

	*request_buf = cifs_small_buf_get();
	if (*request_buf == NULL) {
		/* BB should we add a retry in here if not a writepage? */
		return -ENOMEM;
	}

	header_assemble((struct smb_hdr *) *request_buf, smb_command, tcon,wct);

        if(tcon != NULL)
                cifs_stats_inc(&tcon->num_smbs_sent);

	return rc;
}  

/* If the return code is zero, this function must fill in request_buf pointer */
static int
smb_init(int smb_command, int wct, struct cifsTconInfo *tcon,
	 void **request_buf /* returned */ ,
	 void **response_buf /* returned */ )
{
	int rc = 0;

	/* SMBs NegProt, SessSetup, uLogoff do not have tcon yet so
	   check for tcp and smb session status done differently
	   for those three - in the calling routine */
	if(tcon) {
		if((tcon->ses) && (tcon->ses->status != CifsExiting) && 
				  (tcon->ses->server)){
			struct nls_table *nls_codepage;
				/* Give Demultiplex thread up to 10 seconds to
				   reconnect, should be greater than cifs socket
				   timeout which is 7 seconds */
			while(tcon->ses->server->tcpStatus == CifsNeedReconnect) {
				wait_event_interruptible_timeout(tcon->ses->server->response_q,
					(tcon->ses->server->tcpStatus == CifsGood), 10 * HZ);
				if(tcon->ses->server->tcpStatus == 
						CifsNeedReconnect) {
					/* on "soft" mounts we wait once */
					if((tcon->retry == FALSE) || 
					   (tcon->ses->status == CifsExiting)) {
						cFYI(1,("gave up waiting on reconnect in smb_init"));
						return -EHOSTDOWN;
					} /* else "hard" mount - keep retrying
					     until process is killed or server
					     comes on-line */
				} else /* TCP session is reestablished now */
					break;
				 
			}
			
			nls_codepage = load_nls_default();
		/* need to prevent multiple threads trying to
		simultaneously reconnect the same SMB session */
			down(&tcon->ses->sesSem);
			if(tcon->ses->status == CifsNeedReconnect)
				rc = cifs_setup_session(0, tcon->ses, 
							nls_codepage);
			if(!rc && (tcon->tidStatus == CifsNeedReconnect)) {
				mark_open_files_invalid(tcon);
				rc = CIFSTCon(0, tcon->ses, tcon->treeName,
					      tcon, nls_codepage);
				up(&tcon->ses->sesSem);
				/* BB FIXME add code to check if wsize needs
				update due to negotiated smb buffer size
				shrinking */
				if(rc == 0)
					atomic_inc(&tconInfoReconnectCount);

				cFYI(1, ("reconnect tcon rc = %d", rc));
				/* Removed call to reopen open files here - 
				   it is safer (and faster) to reopen files
				   one at a time as needed in read and write */

				/* Check if handle based operation so we 
				   know whether we can continue or not without
				   returning to caller to reset file handle */
				switch(smb_command) {
					case SMB_COM_READ_ANDX:
					case SMB_COM_WRITE_ANDX:
					case SMB_COM_CLOSE:
					case SMB_COM_FIND_CLOSE2:
					case SMB_COM_LOCKING_ANDX: {
						unload_nls(nls_codepage);
						return -EAGAIN;
					}
				}
			} else {
				up(&tcon->ses->sesSem);
			}
			unload_nls(nls_codepage);

		} else {
			return -EIO;
		}
	}
	if(rc)
		return rc;

	*request_buf = cifs_buf_get();
	if (*request_buf == NULL) {
		/* BB should we add a retry in here if not a writepage? */
		return -ENOMEM;
	}
    /* Although the original thought was we needed the response buf for  */
    /* potential retries of smb operations it turns out we can determine */
    /* from the mid flags when the request buffer can be resent without  */
    /* having to use a second distinct buffer for the response */
	*response_buf = *request_buf; 

	header_assemble((struct smb_hdr *) *request_buf, smb_command, tcon,
			wct /*wct */ );

        if(tcon != NULL)
                cifs_stats_inc(&tcon->num_smbs_sent);

	return rc;
}

static int validate_t2(struct smb_t2_rsp * pSMB) 
{
	int rc = -EINVAL;
	int total_size;
	char * pBCC;

	/* check for plausible wct, bcc and t2 data and parm sizes */
	/* check for parm and data offset going beyond end of smb */
	if(pSMB->hdr.WordCount >= 10) {
		if((le16_to_cpu(pSMB->t2_rsp.ParameterOffset) <= 1024) &&
		   (le16_to_cpu(pSMB->t2_rsp.DataOffset) <= 1024)) {
			/* check that bcc is at least as big as parms + data */
			/* check that bcc is less than negotiated smb buffer */
			total_size = le16_to_cpu(pSMB->t2_rsp.ParameterCount);
			if(total_size < 512) {
				total_size+=le16_to_cpu(pSMB->t2_rsp.DataCount);
				/* BCC le converted in SendReceive */
				pBCC = (pSMB->hdr.WordCount * 2) + 
					sizeof(struct smb_hdr) +
					(char *)pSMB;
				if((total_size <= (*(u16 *)pBCC)) && 
				   (total_size < 
					CIFSMaxBufSize+MAX_CIFS_HDR_SIZE)) {
					return 0;
				}
				
			}
		}
	}
	cifs_dump_mem("Invalid transact2 SMB: ",(char *)pSMB,
		sizeof(struct smb_t2_rsp) + 16);
	return rc;
}
int
CIFSSMBNegotiate(unsigned int xid, struct cifsSesInfo *ses)
{
	NEGOTIATE_REQ *pSMB;
	NEGOTIATE_RSP *pSMBr;
	int rc = 0;
	int bytes_returned;
	struct TCP_Server_Info * server;
	u16 count;

	if(ses->server)
		server = ses->server;
	else {
		rc = -EIO;
		return rc;
	}
	rc = smb_init(SMB_COM_NEGOTIATE, 0, NULL /* no tcon yet */ ,
		      (void **) &pSMB, (void **) &pSMBr);
	if (rc)
		return rc;
	pSMB->hdr.Mid = GetNextMid(server);
	pSMB->hdr.Flags2 |= SMBFLG2_UNICODE;
	if (extended_security)
		pSMB->hdr.Flags2 |= SMBFLG2_EXT_SEC;

	count = strlen(protocols[0].name) + 1;
	strncpy(pSMB->DialectsArray, protocols[0].name, 30);	
    /* null guaranteed to be at end of source and target buffers anyway */

	pSMB->hdr.smb_buf_length += count;
	pSMB->ByteCount = cpu_to_le16(count);

	rc = SendReceive(xid, ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc == 0) {
		server->secMode = pSMBr->SecurityMode;	
		server->secType = NTLM; /* BB override default for 
					   NTLMv2 or kerberos v5 */
		/* one byte - no need to convert this or EncryptionKeyLen
		   from little endian */
		server->maxReq = le16_to_cpu(pSMBr->MaxMpxCount);
		/* probably no need to store and check maxvcs */
		server->maxBuf =
			min(le32_to_cpu(pSMBr->MaxBufferSize),
			(__u32) CIFSMaxBufSize + MAX_CIFS_HDR_SIZE);
		server->maxRw = le32_to_cpu(pSMBr->MaxRawSize);
		cFYI(0, ("Max buf = %d ", ses->server->maxBuf));
		GETU32(ses->server->sessid) = le32_to_cpu(pSMBr->SessionKey);
		server->capabilities = le32_to_cpu(pSMBr->Capabilities);
		server->timeZone = le16_to_cpu(pSMBr->ServerTimeZone);	
        /* BB with UTC do we ever need to be using srvr timezone? */
		if (pSMBr->EncryptionKeyLength == CIFS_CRYPTO_KEY_SIZE) {
			memcpy(server->cryptKey, pSMBr->u.EncryptionKey,
			       CIFS_CRYPTO_KEY_SIZE);
		} else if ((pSMBr->hdr.Flags2 & SMBFLG2_EXT_SEC)
			   && (pSMBr->EncryptionKeyLength == 0)) {
			/* decode security blob */
		} else
			rc = -EIO;

		/* BB might be helpful to save off the domain of server here */

		if ((pSMBr->hdr.Flags2 & SMBFLG2_EXT_SEC) && 
			(server->capabilities & CAP_EXTENDED_SECURITY)) {
			count = pSMBr->ByteCount;
			if (count < 16)
				rc = -EIO;
			else if (count == 16) {
				server->secType = RawNTLMSSP;
				if (server->socketUseCount.counter > 1) {
					if (memcmp
						(server->server_GUID,
						pSMBr->u.extended_response.
						GUID, 16) != 0) {
						cFYI(1,
						     ("UID of server does not match previous connection to same ip address"));
						memcpy(server->
							server_GUID,
							pSMBr->u.
							extended_response.
							GUID, 16);
					}
				} else
					memcpy(server->server_GUID,
					       pSMBr->u.extended_response.
					       GUID, 16);
			} else {
				rc = decode_negTokenInit(pSMBr->u.
							 extended_response.
							 SecurityBlob,
							 count - 16,
							 &server->secType);
				if(rc == 1) {
				/* BB Need to fill struct for sessetup here */
					rc = -EOPNOTSUPP;
				} else {
					rc = -EINVAL;
				}
			}
		} else
			server->capabilities &= ~CAP_EXTENDED_SECURITY;
		if(sign_CIFS_PDUs == FALSE) {        
			if(server->secMode & SECMODE_SIGN_REQUIRED)
				cERROR(1,
				 ("Server requires /proc/fs/cifs/PacketSigningEnabled"));
			server->secMode &= ~(SECMODE_SIGN_ENABLED | SECMODE_SIGN_REQUIRED);
		} else if(sign_CIFS_PDUs == 1) {
			if((server->secMode & SECMODE_SIGN_REQUIRED) == 0)
				server->secMode &= ~(SECMODE_SIGN_ENABLED | SECMODE_SIGN_REQUIRED);
		}
				
	}
	
	cifs_buf_release(pSMB);
	return rc;
}

int
CIFSSMBTDis(const int xid, struct cifsTconInfo *tcon)
{
	struct smb_hdr *smb_buffer;
	struct smb_hdr *smb_buffer_response; /* BB removeme BB */
	int rc = 0;
	int length;

	cFYI(1, ("In tree disconnect"));
	/*
	 *  If last user of the connection and
	 *  connection alive - disconnect it
	 *  If this is the last connection on the server session disconnect it
	 *  (and inside session disconnect we should check if tcp socket needs 
	 *  to be freed and kernel thread woken up).
	 */
	if (tcon)
		down(&tcon->tconSem);
	else
		return -EIO;

	atomic_dec(&tcon->useCount);
	if (atomic_read(&tcon->useCount) > 0) {
		up(&tcon->tconSem);
		return -EBUSY;
	}

	/* No need to return error on this operation if tid invalidated and 
	closed on server already e.g. due to tcp session crashing */
	if(tcon->tidStatus == CifsNeedReconnect) {
		up(&tcon->tconSem);
		return 0;  
	}

	if((tcon->ses == NULL) || (tcon->ses->server == NULL)) {    
		up(&tcon->tconSem);
		return -EIO;
	}
	rc = small_smb_init(SMB_COM_TREE_DISCONNECT, 0, tcon, 
			    (void **)&smb_buffer);
	if (rc) {
		up(&tcon->tconSem);
		return rc;
	} else {
		smb_buffer_response = smb_buffer; /* BB removeme BB */
	}
	rc = SendReceive(xid, tcon->ses, smb_buffer, smb_buffer_response,
			 &length, 0);
	if (rc)
		cFYI(1, ("Tree disconnect failed %d", rc));

	if (smb_buffer)
		cifs_small_buf_release(smb_buffer);
	up(&tcon->tconSem);

	/* No need to return error on this operation if tid invalidated and 
	closed on server already e.g. due to tcp session crashing */
	if (rc == -EAGAIN)
		rc = 0;

	return rc;
}

int
CIFSSMBLogoff(const int xid, struct cifsSesInfo *ses)
{
	struct smb_hdr *smb_buffer_response;
	LOGOFF_ANDX_REQ *pSMB;
	int rc = 0;
	int length;

	cFYI(1, ("In SMBLogoff for session disconnect"));
	if (ses)
		down(&ses->sesSem);
	else
		return -EIO;

	atomic_dec(&ses->inUse);
	if (atomic_read(&ses->inUse) > 0) {
		up(&ses->sesSem);
		return -EBUSY;
	}
	rc = small_smb_init(SMB_COM_LOGOFF_ANDX, 2, NULL, (void **)&pSMB);
	if (rc) {
		up(&ses->sesSem);
		return rc;
	}

	smb_buffer_response = (struct smb_hdr *)pSMB; /* BB removeme BB */
	
	if(ses->server) {
		pSMB->hdr.Mid = GetNextMid(ses->server);

		if(ses->server->secMode & 
		   (SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
			pSMB->hdr.Flags2 |= SMBFLG2_SECURITY_SIGNATURE;
	}

	pSMB->hdr.Uid = ses->Suid;

	pSMB->AndXCommand = 0xFF;
	rc = SendReceive(xid, ses, (struct smb_hdr *) pSMB,
			 smb_buffer_response, &length, 0);
	if (ses->server) {
		atomic_dec(&ses->server->socketUseCount);
		if (atomic_read(&ses->server->socketUseCount) == 0) {
			spin_lock(&GlobalMid_Lock);
			ses->server->tcpStatus = CifsExiting;
			spin_unlock(&GlobalMid_Lock);
			rc = -ESHUTDOWN;
		}
	}
	up(&ses->sesSem);
	cifs_small_buf_release(pSMB);

	/* if session dead then we do not need to do ulogoff,
		since server closed smb session, no sense reporting 
		error */
	if (rc == -EAGAIN)
		rc = 0;
	return rc;
}

int
CIFSSMBDelFile(const int xid, struct cifsTconInfo *tcon, const char *fileName,
	       const struct nls_table *nls_codepage, int remap)
{
	DELETE_FILE_REQ *pSMB = NULL;
	DELETE_FILE_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned;
	int name_len;

DelFileRetry:
	rc = smb_init(SMB_COM_DELETE, 1, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifsConvertToUCS((__le16 *) pSMB->fileName, fileName, 
				     PATH_MAX, nls_codepage, remap);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {		/* BB improve check for buffer overruns BB */
		name_len = strnlen(fileName, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->fileName, fileName, name_len);
	}
	pSMB->SearchAttributes =
	    cpu_to_le16(ATTR_READONLY | ATTR_HIDDEN | ATTR_SYSTEM);
	pSMB->BufferFormat = 0x04;
	pSMB->hdr.smb_buf_length += name_len + 1;
	pSMB->ByteCount = cpu_to_le16(name_len + 1);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	cifs_stats_inc(&tcon->num_deletes);
	if (rc) {
		cFYI(1, ("Error in RMFile = %d", rc));
	} 

	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto DelFileRetry;

	return rc;
}

int
CIFSSMBRmDir(const int xid, struct cifsTconInfo *tcon, const char *dirName, 
	     const struct nls_table *nls_codepage, int remap)
{
	DELETE_DIRECTORY_REQ *pSMB = NULL;
	DELETE_DIRECTORY_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned;
	int name_len;

	cFYI(1, ("In CIFSSMBRmDir"));
RmDirRetry:
	rc = smb_init(SMB_COM_DELETE_DIRECTORY, 0, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len = cifsConvertToUCS((__le16 *) pSMB->DirName, dirName,
					 PATH_MAX, nls_codepage, remap);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {		/* BB improve check for buffer overruns BB */
		name_len = strnlen(dirName, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->DirName, dirName, name_len);
	}

	pSMB->BufferFormat = 0x04;
	pSMB->hdr.smb_buf_length += name_len + 1;
	pSMB->ByteCount = cpu_to_le16(name_len + 1);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	cifs_stats_inc(&tcon->num_rmdirs);
	if (rc) {
		cFYI(1, ("Error in RMDir = %d", rc));
	}

	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto RmDirRetry;
	return rc;
}

int
CIFSSMBMkDir(const int xid, struct cifsTconInfo *tcon,
	     const char *name, const struct nls_table *nls_codepage, int remap)
{
	int rc = 0;
	CREATE_DIRECTORY_REQ *pSMB = NULL;
	CREATE_DIRECTORY_RSP *pSMBr = NULL;
	int bytes_returned;
	int name_len;

	cFYI(1, ("In CIFSSMBMkDir"));
MkDirRetry:
	rc = smb_init(SMB_COM_CREATE_DIRECTORY, 0, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len = cifsConvertToUCS((__le16 *) pSMB->DirName, name, 
					    PATH_MAX, nls_codepage, remap);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {		/* BB improve check for buffer overruns BB */
		name_len = strnlen(name, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->DirName, name, name_len);
	}

	pSMB->BufferFormat = 0x04;
	pSMB->hdr.smb_buf_length += name_len + 1;
	pSMB->ByteCount = cpu_to_le16(name_len + 1);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	cifs_stats_inc(&tcon->num_mkdirs);
	if (rc) {
		cFYI(1, ("Error in Mkdir = %d", rc));
	}

	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto MkDirRetry;
	return rc;
}

static __u16 convert_disposition(int disposition)
{
	__u16 ofun = 0;

	switch (disposition) {
		case FILE_SUPERSEDE:
			ofun = SMBOPEN_OCREATE | SMBOPEN_OTRUNC;
			break;
		case FILE_OPEN:
			ofun = SMBOPEN_OAPPEND;
			break;
		case FILE_CREATE:
			ofun = SMBOPEN_OCREATE;
			break;
		case FILE_OPEN_IF:
			ofun = SMBOPEN_OCREATE | SMBOPEN_OAPPEND;
			break;
		case FILE_OVERWRITE:
			ofun = SMBOPEN_OTRUNC;
			break;
		case FILE_OVERWRITE_IF:
			ofun = SMBOPEN_OCREATE | SMBOPEN_OTRUNC;
			break;
		default:
			cFYI(1,("unknown disposition %d",disposition));
			ofun =  SMBOPEN_OAPPEND; /* regular open */
	}
	return ofun;
}

int
SMBLegacyOpen(const int xid, struct cifsTconInfo *tcon,
	    const char *fileName, const int openDisposition,
	    const int access_flags, const int create_options, __u16 * netfid,
            int *pOplock, FILE_ALL_INFO * pfile_info,
	    const struct nls_table *nls_codepage, int remap)
{
	int rc = -EACCES;
	OPENX_REQ *pSMB = NULL;
	OPENX_RSP *pSMBr = NULL;
	int bytes_returned;
	int name_len;
	__u16 count;

OldOpenRetry:
	rc = smb_init(SMB_COM_OPEN_ANDX, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->AndXCommand = 0xFF;       /* none */

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		count = 1;      /* account for one byte pad to word boundary */
		name_len =
		   cifsConvertToUCS((__le16 *) (pSMB->fileName + 1),
				    fileName, PATH_MAX, nls_codepage, remap);
		name_len++;     /* trailing null */
		name_len *= 2;
	} else {                /* BB improve check for buffer overruns BB */
		count = 0;      /* no pad */
		name_len = strnlen(fileName, PATH_MAX);
		name_len++;     /* trailing null */
		strncpy(pSMB->fileName, fileName, name_len);
	}
	if (*pOplock & REQ_OPLOCK)
		pSMB->OpenFlags = cpu_to_le16(REQ_OPLOCK);
	else if (*pOplock & REQ_BATCHOPLOCK) {
		pSMB->OpenFlags = cpu_to_le16(REQ_BATCHOPLOCK);
	}
	pSMB->OpenFlags |= cpu_to_le16(REQ_MORE_INFO);
	/* BB fixme add conversion for access_flags to bits 0 - 2 of mode */
	/* 0 = read
	   1 = write
	   2 = rw
	   3 = execute
        */
	pSMB->Mode = cpu_to_le16(2);
	pSMB->Mode |= cpu_to_le16(0x40); /* deny none */
	/* set file as system file if special file such
	   as fifo and server expecting SFU style and
	   no Unix extensions */

        if(create_options & CREATE_OPTION_SPECIAL)
                pSMB->FileAttributes = cpu_to_le16(ATTR_SYSTEM);
        else
                pSMB->FileAttributes = cpu_to_le16(0/*ATTR_NORMAL*/); /* BB FIXME */

	/* if ((omode & S_IWUGO) == 0)
		pSMB->FileAttributes |= cpu_to_le32(ATTR_READONLY);*/
	/*  Above line causes problems due to vfs splitting create into two
	    pieces - need to set mode after file created not while it is
	    being created */

	/* BB FIXME BB */
/*	pSMB->CreateOptions = cpu_to_le32(create_options & CREATE_OPTIONS_MASK); */
	/* BB FIXME END BB */

	pSMB->Sattr = cpu_to_le16(ATTR_HIDDEN | ATTR_SYSTEM | ATTR_DIRECTORY);
	pSMB->OpenFunction = cpu_to_le16(convert_disposition(openDisposition));
	count += name_len;
	pSMB->hdr.smb_buf_length += count;

	pSMB->ByteCount = cpu_to_le16(count);
	/* long_op set to 1 to allow for oplock break timeouts */
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
		         (struct smb_hdr *) pSMBr, &bytes_returned, 1);
	cifs_stats_inc(&tcon->num_opens);
	if (rc) {
		cFYI(1, ("Error in Open = %d", rc));
	} else {
	/* BB verify if wct == 15 */

/*		*pOplock = pSMBr->OplockLevel; */  /* BB take from action field BB */

		*netfid = pSMBr->Fid;   /* cifs fid stays in le */
		/* Let caller know file was created so we can set the mode. */
		/* Do we care about the CreateAction in any other cases? */
	/* BB FIXME BB */
/*		if(cpu_to_le32(FILE_CREATE) == pSMBr->CreateAction)
			*pOplock |= CIFS_CREATE_ACTION; */
	/* BB FIXME END */

		if(pfile_info) {
			pfile_info->CreationTime = 0; /* BB convert CreateTime*/
			pfile_info->LastAccessTime = 0; /* BB fixme */
			pfile_info->LastWriteTime = 0; /* BB fixme */
			pfile_info->ChangeTime = 0;  /* BB fixme */
			pfile_info->Attributes =
				cpu_to_le32(le16_to_cpu(pSMBr->FileAttributes)); 
			/* the file_info buf is endian converted by caller */
			pfile_info->AllocationSize =
				cpu_to_le64(le32_to_cpu(pSMBr->EndOfFile));
			pfile_info->EndOfFile = pfile_info->AllocationSize;
			pfile_info->NumberOfLinks = cpu_to_le32(1);
		}
	}

	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto OldOpenRetry;
	return rc;
}

int
CIFSSMBOpen(const int xid, struct cifsTconInfo *tcon,
	    const char *fileName, const int openDisposition,
	    const int access_flags, const int create_options, __u16 * netfid,
	    int *pOplock, FILE_ALL_INFO * pfile_info, 
	    const struct nls_table *nls_codepage, int remap)
{
	int rc = -EACCES;
	OPEN_REQ *pSMB = NULL;
	OPEN_RSP *pSMBr = NULL;
	int bytes_returned;
	int name_len;
	__u16 count;

openRetry:
	rc = smb_init(SMB_COM_NT_CREATE_ANDX, 24, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->AndXCommand = 0xFF;	/* none */

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		count = 1;	/* account for one byte pad to word boundary */
		name_len =
		    cifsConvertToUCS((__le16 *) (pSMB->fileName + 1),
				     fileName, PATH_MAX, nls_codepage, remap);
		name_len++;	/* trailing null */
		name_len *= 2;
		pSMB->NameLength = cpu_to_le16(name_len);
	} else {		/* BB improve check for buffer overruns BB */
		count = 0;	/* no pad */
		name_len = strnlen(fileName, PATH_MAX);
		name_len++;	/* trailing null */
		pSMB->NameLength = cpu_to_le16(name_len);
		strncpy(pSMB->fileName, fileName, name_len);
	}
	if (*pOplock & REQ_OPLOCK)
		pSMB->OpenFlags = cpu_to_le32(REQ_OPLOCK);
	else if (*pOplock & REQ_BATCHOPLOCK) {
		pSMB->OpenFlags = cpu_to_le32(REQ_BATCHOPLOCK);
	}
	pSMB->DesiredAccess = cpu_to_le32(access_flags);
	pSMB->AllocationSize = 0;
	/* set file as system file if special file such
	   as fifo and server expecting SFU style and
	   no Unix extensions */
	if(create_options & CREATE_OPTION_SPECIAL)
		pSMB->FileAttributes = cpu_to_le32(ATTR_SYSTEM);
	else
		pSMB->FileAttributes = cpu_to_le32(ATTR_NORMAL);
	/* XP does not handle ATTR_POSIX_SEMANTICS */
	/* but it helps speed up case sensitive checks for other
	servers such as Samba */
	if (tcon->ses->capabilities & CAP_UNIX)
		pSMB->FileAttributes |= cpu_to_le32(ATTR_POSIX_SEMANTICS);

	/* if ((omode & S_IWUGO) == 0)
		pSMB->FileAttributes |= cpu_to_le32(ATTR_READONLY);*/
	/*  Above line causes problems due to vfs splitting create into two
		pieces - need to set mode after file created not while it is
		being created */
	pSMB->ShareAccess = cpu_to_le32(FILE_SHARE_ALL);
	pSMB->CreateDisposition = cpu_to_le32(openDisposition);
	pSMB->CreateOptions = cpu_to_le32(create_options & CREATE_OPTIONS_MASK);
	/* BB Expirement with various impersonation levels and verify */
	pSMB->ImpersonationLevel = cpu_to_le32(SECURITY_IMPERSONATION);
	pSMB->SecurityFlags =
	    SECURITY_CONTEXT_TRACKING | SECURITY_EFFECTIVE_ONLY;

	count += name_len;
	pSMB->hdr.smb_buf_length += count;

	pSMB->ByteCount = cpu_to_le16(count);
	/* long_op set to 1 to allow for oplock break timeouts */
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 1);
	cifs_stats_inc(&tcon->num_opens);
	if (rc) {
		cFYI(1, ("Error in Open = %d", rc));
	} else {
		*pOplock = pSMBr->OplockLevel; /* 1 byte no need to le_to_cpu */
		*netfid = pSMBr->Fid;	/* cifs fid stays in le */
		/* Let caller know file was created so we can set the mode. */
		/* Do we care about the CreateAction in any other cases? */
		if(cpu_to_le32(FILE_CREATE) == pSMBr->CreateAction)
			*pOplock |= CIFS_CREATE_ACTION; 
		if(pfile_info) {
		    memcpy((char *)pfile_info,(char *)&pSMBr->CreationTime,
			36 /* CreationTime to Attributes */);
		    /* the file_info buf is endian converted by caller */
		    pfile_info->AllocationSize = pSMBr->AllocationSize;
		    pfile_info->EndOfFile = pSMBr->EndOfFile;
		    pfile_info->NumberOfLinks = cpu_to_le32(1);
		}
	}

	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto openRetry;
	return rc;
}

/* If no buffer passed in, then caller wants to do the copy
	as in the case of readpages so the SMB buffer must be
	freed by the caller */

int
CIFSSMBRead(const int xid, struct cifsTconInfo *tcon,
	    const int netfid, const unsigned int count,
	    const __u64 lseek, unsigned int *nbytes, char **buf)
{
	int rc = -EACCES;
	READ_REQ *pSMB = NULL;
	READ_RSP *pSMBr = NULL;
	char *pReadData = NULL;
	int bytes_returned;
	int wct;

	cFYI(1,("Reading %d bytes on fid %d",count,netfid));
	if(tcon->ses->capabilities & CAP_LARGE_FILES)
		wct = 12;
	else
		wct = 10; /* old style read */

	*nbytes = 0;
	rc = smb_init(SMB_COM_READ_ANDX, wct, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	/* tcon and ses pointer are checked in smb_init */
	if (tcon->ses->server == NULL)
		return -ECONNABORTED;

	pSMB->AndXCommand = 0xFF;	/* none */
	pSMB->Fid = netfid;
	pSMB->OffsetLow = cpu_to_le32(lseek & 0xFFFFFFFF);
	if(wct == 12)
		pSMB->OffsetHigh = cpu_to_le32(lseek >> 32);
        else if((lseek >> 32) > 0) /* can not handle this big offset for old */
                return -EIO;

	pSMB->Remaining = 0;
	pSMB->MaxCount = cpu_to_le16(count & 0xFFFF);
	pSMB->MaxCountHigh = cpu_to_le32(count >> 16);
	if(wct == 12)
		pSMB->ByteCount = 0;  /* no need to do le conversion since 0 */
	else {
		/* old style read */
		struct smb_com_readx_req * pSMBW = 
			(struct smb_com_readx_req *)pSMB;
		pSMBW->ByteCount = 0;	
	}
	
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	cifs_stats_inc(&tcon->num_reads);
	if (rc) {
		cERROR(1, ("Send error in read = %d", rc));
	} else {
		int data_length = le16_to_cpu(pSMBr->DataLengthHigh);
		data_length = data_length << 16;
		data_length += le16_to_cpu(pSMBr->DataLength);
		*nbytes = data_length;

		/*check that DataLength would not go beyond end of SMB */
		if ((data_length > CIFSMaxBufSize) 
				|| (data_length > count)) {
			cFYI(1,("bad length %d for count %d",data_length,count));
			rc = -EIO;
			*nbytes = 0;
		} else {
			pReadData =
			    (char *) (&pSMBr->hdr.Protocol) +
			    le16_to_cpu(pSMBr->DataOffset);
/*			if(rc = copy_to_user(buf, pReadData, data_length)) {
				cERROR(1,("Faulting on read rc = %d",rc));
				rc = -EFAULT;
			}*/ /* can not use copy_to_user when using page cache*/
			if(*buf)
			    memcpy(*buf,pReadData,data_length);
		}
	}
	if(*buf)
		cifs_buf_release(pSMB);
	else
		*buf = (char *)pSMB;

	/* Note: On -EAGAIN error only caller can retry on handle based calls 
		since file handle passed in no longer valid */
	return rc;
}

int
CIFSSMBWrite(const int xid, struct cifsTconInfo *tcon,
	     const int netfid, const unsigned int count,
	     const __u64 offset, unsigned int *nbytes, const char *buf,
	     const char __user * ubuf, const int long_op)
{
	int rc = -EACCES;
	WRITE_REQ *pSMB = NULL;
	WRITE_RSP *pSMBr = NULL;
	int bytes_returned, wct;
	__u32 bytes_sent;
	__u16 byte_count;

	/* cFYI(1,("write at %lld %d bytes",offset,count));*/
	if(tcon->ses == NULL)
		return -ECONNABORTED;

	if(tcon->ses->capabilities & CAP_LARGE_FILES)
		wct = 14;
	else
		wct = 12;

	rc = smb_init(SMB_COM_WRITE_ANDX, wct, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;
	/* tcon and ses pointer are checked in smb_init */
	if (tcon->ses->server == NULL)
		return -ECONNABORTED;

	pSMB->AndXCommand = 0xFF;	/* none */
	pSMB->Fid = netfid;
	pSMB->OffsetLow = cpu_to_le32(offset & 0xFFFFFFFF);
	if(wct == 14) 
		pSMB->OffsetHigh = cpu_to_le32(offset >> 32);
	else if((offset >> 32) > 0) /* can not handle this big offset for old */
		return -EIO;
	
	pSMB->Reserved = 0xFFFFFFFF;
	pSMB->WriteMode = 0;
	pSMB->Remaining = 0;

	/* Can increase buffer size if buffer is big enough in some cases - ie we 
	can send more if LARGE_WRITE_X capability returned by the server and if
	our buffer is big enough or if we convert to iovecs on socket writes
	and eliminate the copy to the CIFS buffer */
	if(tcon->ses->capabilities & CAP_LARGE_WRITE_X) {
		bytes_sent = min_t(const unsigned int, CIFSMaxBufSize, count);
	} else {
		bytes_sent = (tcon->ses->server->maxBuf - MAX_CIFS_HDR_SIZE)
			 & ~0xFF;
	}

	if (bytes_sent > count)
		bytes_sent = count;
	pSMB->DataOffset =
		cpu_to_le16(offsetof(struct smb_com_write_req,Data) - 4);
	if(buf)
	    memcpy(pSMB->Data,buf,bytes_sent);
	else if(ubuf) {
		if(copy_from_user(pSMB->Data,ubuf,bytes_sent)) {
			cifs_buf_release(pSMB);
			return -EFAULT;
		}
	} else if (count != 0) {
		/* No buffer */
		cifs_buf_release(pSMB);
		return -EINVAL;
	} /* else setting file size with write of zero bytes */
	if(wct == 14)
		byte_count = bytes_sent + 1; /* pad */
	else /* wct == 12 */ {
		byte_count = bytes_sent + 5; /* bigger pad, smaller smb hdr */
	}
	pSMB->DataLengthLow = cpu_to_le16(bytes_sent & 0xFFFF);
	pSMB->DataLengthHigh = cpu_to_le16(bytes_sent >> 16);
	pSMB->hdr.smb_buf_length += byte_count;

	if(wct == 14)
		pSMB->ByteCount = cpu_to_le16(byte_count);
	else { /* old style write has byte count 4 bytes earlier so 4 bytes pad  */
		struct smb_com_writex_req * pSMBW = 
			(struct smb_com_writex_req *)pSMB;
		pSMBW->ByteCount = cpu_to_le16(byte_count);
	}

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, long_op);
	cifs_stats_inc(&tcon->num_writes);
	if (rc) {
		cFYI(1, ("Send error in write = %d", rc));
		*nbytes = 0;
	} else {
		*nbytes = le16_to_cpu(pSMBr->CountHigh);
		*nbytes = (*nbytes) << 16;
		*nbytes += le16_to_cpu(pSMBr->Count);
	}

	cifs_buf_release(pSMB);

	/* Note: On -EAGAIN error only caller can retry on handle based calls 
		since file handle passed in no longer valid */

	return rc;
}

#ifdef CONFIG_CIFS_EXPERIMENTAL
int
CIFSSMBWrite2(const int xid, struct cifsTconInfo *tcon,
	     const int netfid, const unsigned int count,
	     const __u64 offset, unsigned int *nbytes, struct kvec *iov,
	     int n_vec, const int long_op)
{
	int rc = -EACCES;
	WRITE_REQ *pSMB = NULL;
	int bytes_returned, wct;
	int smb_hdr_len;

	/* BB removeme BB */
	cFYI(1,("write2 at %lld %d bytes", (long long)offset, count));

	if(tcon->ses->capabilities & CAP_LARGE_FILES)
		wct = 14;
	else
		wct = 12;
	rc = small_smb_init(SMB_COM_WRITE_ANDX, wct, tcon, (void **) &pSMB);
	if (rc)
		return rc;
	/* tcon and ses pointer are checked in smb_init */
	if (tcon->ses->server == NULL)
		return -ECONNABORTED;

	pSMB->AndXCommand = 0xFF;	/* none */
	pSMB->Fid = netfid;
	pSMB->OffsetLow = cpu_to_le32(offset & 0xFFFFFFFF);
	if(wct == 14)
		pSMB->OffsetHigh = cpu_to_le32(offset >> 32);
	else if((offset >> 32) > 0) /* can not handle this big offset for old */
		return -EIO;
	pSMB->Reserved = 0xFFFFFFFF;
	pSMB->WriteMode = 0;
	pSMB->Remaining = 0;

	pSMB->DataOffset =
	    cpu_to_le16(offsetof(struct smb_com_write_req,Data) - 4);

	pSMB->DataLengthLow = cpu_to_le16(count & 0xFFFF);
	pSMB->DataLengthHigh = cpu_to_le16(count >> 16);
	smb_hdr_len = pSMB->hdr.smb_buf_length + 1; /* hdr + 1 byte pad */
	if(wct == 14)
		pSMB->hdr.smb_buf_length += count+1;
	else /* wct == 12 */
		pSMB->hdr.smb_buf_length += count+5; /* smb data starts later */ 
	if(wct == 14)
		pSMB->ByteCount = cpu_to_le16(count + 1);
	else /* wct == 12 */ /* bigger pad, smaller smb hdr, keep offset ok */ {
		struct smb_com_writex_req * pSMBW =
				(struct smb_com_writex_req *)pSMB;
		pSMBW->ByteCount = cpu_to_le16(count + 5);
	}
	iov[0].iov_base = pSMB;
	iov[0].iov_len = smb_hdr_len + 4;

	rc = SendReceive2(xid, tcon->ses, iov, n_vec + 1, &bytes_returned,
			  long_op);
	cifs_stats_inc(&tcon->num_writes);
	if (rc) {
		cFYI(1, ("Send error Write2 = %d", rc));
		*nbytes = 0;
	} else {
		WRITE_RSP * pSMBr = (WRITE_RSP *)pSMB;
		*nbytes = le16_to_cpu(pSMBr->CountHigh);
		*nbytes = (*nbytes) << 16;
		*nbytes += le16_to_cpu(pSMBr->Count);
	}

	cifs_small_buf_release(pSMB);

	/* Note: On -EAGAIN error only caller can retry on handle based calls 
		since file handle passed in no longer valid */

	return rc;
}


#endif /* CIFS_EXPERIMENTAL */

int
CIFSSMBLock(const int xid, struct cifsTconInfo *tcon,
	    const __u16 smb_file_id, const __u64 len,
	    const __u64 offset, const __u32 numUnlock,
	    const __u32 numLock, const __u8 lockType, const int waitFlag)
{
	int rc = 0;
	LOCK_REQ *pSMB = NULL;
	LOCK_RSP *pSMBr = NULL;
	int bytes_returned;
	int timeout = 0;
	__u16 count;

	cFYI(1, ("In CIFSSMBLock - timeout %d numLock %d",waitFlag,numLock));
	rc = small_smb_init(SMB_COM_LOCKING_ANDX, 8, tcon, (void **) &pSMB);

	if (rc)
		return rc;

	pSMBr = (LOCK_RSP *)pSMB; /* BB removeme BB */

	if(lockType == LOCKING_ANDX_OPLOCK_RELEASE) {
		timeout = -1; /* no response expected */
		pSMB->Timeout = 0;
	} else if (waitFlag == TRUE) {
		timeout = 3;  /* blocking operation, no timeout */
		pSMB->Timeout = cpu_to_le32(-1);/* blocking - do not time out */
	} else {
		pSMB->Timeout = 0;
	}

	pSMB->NumberOfLocks = cpu_to_le16(numLock);
	pSMB->NumberOfUnlocks = cpu_to_le16(numUnlock);
	pSMB->LockType = lockType;
	pSMB->AndXCommand = 0xFF;	/* none */
	pSMB->Fid = smb_file_id; /* netfid stays le */

	if((numLock != 0) || (numUnlock != 0)) {
		pSMB->Locks[0].Pid = cpu_to_le16(current->tgid);
		/* BB where to store pid high? */
		pSMB->Locks[0].LengthLow = cpu_to_le32((u32)len);
		pSMB->Locks[0].LengthHigh = cpu_to_le32((u32)(len>>32));
		pSMB->Locks[0].OffsetLow = cpu_to_le32((u32)offset);
		pSMB->Locks[0].OffsetHigh = cpu_to_le32((u32)(offset>>32));
		count = sizeof(LOCKING_ANDX_RANGE);
	} else {
		/* oplock break */
		count = 0;
	}
	pSMB->hdr.smb_buf_length += count;
	pSMB->ByteCount = cpu_to_le16(count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, timeout);
	cifs_stats_inc(&tcon->num_locks);
	if (rc) {
		cFYI(1, ("Send error in Lock = %d", rc));
	}
	cifs_small_buf_release(pSMB);

	/* Note: On -EAGAIN error only caller can retry on handle based calls 
	since file handle passed in no longer valid */
	return rc;
}

int
CIFSSMBClose(const int xid, struct cifsTconInfo *tcon, int smb_file_id)
{
	int rc = 0;
	CLOSE_REQ *pSMB = NULL;
	CLOSE_RSP *pSMBr = NULL;
	int bytes_returned;
	cFYI(1, ("In CIFSSMBClose"));

/* do not retry on dead session on close */
	rc = small_smb_init(SMB_COM_CLOSE, 3, tcon, (void **) &pSMB);
	if(rc == -EAGAIN)
		return 0;
	if (rc)
		return rc;

	pSMBr = (CLOSE_RSP *)pSMB; /* BB removeme BB */

	pSMB->FileID = (__u16) smb_file_id;
	pSMB->LastWriteTime = 0;
	pSMB->ByteCount = 0;
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	cifs_stats_inc(&tcon->num_closes);
	if (rc) {
		if(rc!=-EINTR) {
			/* EINTR is expected when user ctl-c to kill app */
			cERROR(1, ("Send error in Close = %d", rc));
		}
	}

	cifs_small_buf_release(pSMB);

	/* Since session is dead, file will be closed on server already */
	if(rc == -EAGAIN)
		rc = 0;

	return rc;
}

int
CIFSSMBRename(const int xid, struct cifsTconInfo *tcon,
	      const char *fromName, const char *toName,
	      const struct nls_table *nls_codepage, int remap)
{
	int rc = 0;
	RENAME_REQ *pSMB = NULL;
	RENAME_RSP *pSMBr = NULL;
	int bytes_returned;
	int name_len, name_len2;
	__u16 count;

	cFYI(1, ("In CIFSSMBRename"));
renameRetry:
	rc = smb_init(SMB_COM_RENAME, 1, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->BufferFormat = 0x04;
	pSMB->SearchAttributes =
	    cpu_to_le16(ATTR_READONLY | ATTR_HIDDEN | ATTR_SYSTEM |
			ATTR_DIRECTORY);

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifsConvertToUCS((__le16 *) pSMB->OldFileName, fromName, 
				     PATH_MAX, nls_codepage, remap);
		name_len++;	/* trailing null */
		name_len *= 2;
		pSMB->OldFileName[name_len] = 0x04;	/* pad */
	/* protocol requires ASCII signature byte on Unicode string */
		pSMB->OldFileName[name_len + 1] = 0x00;
		name_len2 =
		    cifsConvertToUCS((__le16 *) &pSMB->OldFileName[name_len + 2],
				     toName, PATH_MAX, nls_codepage, remap);
		name_len2 += 1 /* trailing null */  + 1 /* Signature word */ ;
		name_len2 *= 2;	/* convert to bytes */
	} else {		/* BB improve the check for buffer overruns BB */
		name_len = strnlen(fromName, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->OldFileName, fromName, name_len);
		name_len2 = strnlen(toName, PATH_MAX);
		name_len2++;	/* trailing null */
		pSMB->OldFileName[name_len] = 0x04;  /* 2nd buffer format */
		strncpy(&pSMB->OldFileName[name_len + 1], toName, name_len2);
		name_len2++;	/* trailing null */
		name_len2++;	/* signature byte */
	}

	count = 1 /* 1st signature byte */  + name_len + name_len2;
	pSMB->hdr.smb_buf_length += count;
	pSMB->ByteCount = cpu_to_le16(count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	cifs_stats_inc(&tcon->num_renames);
	if (rc) {
		cFYI(1, ("Send error in rename = %d", rc));
	} 

	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto renameRetry;

	return rc;
}

int CIFSSMBRenameOpenFile(const int xid,struct cifsTconInfo *pTcon, 
		int netfid, char * target_name, 
		const struct nls_table * nls_codepage, int remap)
{
	struct smb_com_transaction2_sfi_req *pSMB  = NULL;
	struct smb_com_transaction2_sfi_rsp *pSMBr = NULL;
	struct set_file_rename * rename_info;
	char *data_offset;
	char dummy_string[30];
	int rc = 0;
	int bytes_returned = 0;
	int len_of_str;
	__u16 params, param_offset, offset, count, byte_count;

	cFYI(1, ("Rename to File by handle"));
	rc = smb_init(SMB_COM_TRANSACTION2, 15, pTcon, (void **) &pSMB,
			(void **) &pSMBr);
	if (rc)
		return rc;

	params = 6;
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_sfi_req, Fid) - 4;
	offset = param_offset + params;

	data_offset = (char *) (&pSMB->hdr.Protocol) + offset;
	rename_info = (struct set_file_rename *) data_offset;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(1000); /* BB find max SMB PDU from sess */
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_FILE_INFORMATION);
	byte_count = 3 /* pad */  + params;
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(param_offset);
	pSMB->DataOffset = cpu_to_le16(offset);
	/* construct random name ".cifs_tmp<inodenum><mid>" */
	rename_info->overwrite = cpu_to_le32(1);
	rename_info->root_fid  = 0;
	/* unicode only call */
	if(target_name == NULL) {
		sprintf(dummy_string,"cifs%x",pSMB->hdr.Mid);
	        len_of_str = cifsConvertToUCS((__le16 *)rename_info->target_name,
					dummy_string, 24, nls_codepage, remap);
	} else {
		len_of_str = cifsConvertToUCS((__le16 *)rename_info->target_name,
					target_name, PATH_MAX, nls_codepage, remap);
	}
	rename_info->target_name_len = cpu_to_le32(2 * len_of_str);
	count = 12 /* sizeof(struct set_file_rename) */ + (2 * len_of_str) + 2;
	byte_count += count;
	pSMB->DataCount = cpu_to_le16(count);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->Fid = netfid;
	pSMB->InformationLevel =
		cpu_to_le16(SMB_SET_FILE_RENAME_INFORMATION);
	pSMB->Reserved4 = 0;
	pSMB->hdr.smb_buf_length += byte_count;
	pSMB->ByteCount = cpu_to_le16(byte_count);
	rc = SendReceive(xid, pTcon->ses, (struct smb_hdr *) pSMB,
                         (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	cifs_stats_inc(&pTcon->num_t2renames);
	if (rc) {
		cFYI(1,("Send error in Rename (by file handle) = %d", rc));
	}

	cifs_buf_release(pSMB);

	/* Note: On -EAGAIN error only caller can retry on handle based calls
		since file handle passed in no longer valid */

	return rc;
}

int
CIFSSMBCopy(const int xid, struct cifsTconInfo *tcon, const char * fromName, 
            const __u16 target_tid, const char *toName, const int flags,
            const struct nls_table *nls_codepage, int remap)
{
	int rc = 0;
	COPY_REQ *pSMB = NULL;
	COPY_RSP *pSMBr = NULL;
	int bytes_returned;
	int name_len, name_len2;
	__u16 count;

	cFYI(1, ("In CIFSSMBCopy"));
copyRetry:
	rc = smb_init(SMB_COM_COPY, 1, tcon, (void **) &pSMB,
			(void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->BufferFormat = 0x04;
	pSMB->Tid2 = target_tid;

	pSMB->Flags = cpu_to_le16(flags & COPY_TREE);

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len = cifsConvertToUCS((__le16 *) pSMB->OldFileName, 
					    fromName, PATH_MAX, nls_codepage,
					    remap);
		name_len++;     /* trailing null */
		name_len *= 2;
		pSMB->OldFileName[name_len] = 0x04;     /* pad */
		/* protocol requires ASCII signature byte on Unicode string */
		pSMB->OldFileName[name_len + 1] = 0x00;
		name_len2 = cifsConvertToUCS((__le16 *)&pSMB->OldFileName[name_len + 2], 
				toName, PATH_MAX, nls_codepage, remap);
		name_len2 += 1 /* trailing null */  + 1 /* Signature word */ ;
		name_len2 *= 2; /* convert to bytes */
	} else {                /* BB improve the check for buffer overruns BB */
		name_len = strnlen(fromName, PATH_MAX);
		name_len++;     /* trailing null */
		strncpy(pSMB->OldFileName, fromName, name_len);
		name_len2 = strnlen(toName, PATH_MAX);
		name_len2++;    /* trailing null */
		pSMB->OldFileName[name_len] = 0x04;  /* 2nd buffer format */
		strncpy(&pSMB->OldFileName[name_len + 1], toName, name_len2);
		name_len2++;    /* trailing null */
		name_len2++;    /* signature byte */
	}

	count = 1 /* 1st signature byte */  + name_len + name_len2;
	pSMB->hdr.smb_buf_length += count;
	pSMB->ByteCount = cpu_to_le16(count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
		(struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("Send error in copy = %d with %d files copied",
			rc, le16_to_cpu(pSMBr->CopyCount)));
	}
	if (pSMB)
		cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto copyRetry;

	return rc;
}

int
CIFSUnixCreateSymLink(const int xid, struct cifsTconInfo *tcon,
		      const char *fromName, const char *toName,
		      const struct nls_table *nls_codepage)
{
	TRANSACTION2_SPI_REQ *pSMB = NULL;
	TRANSACTION2_SPI_RSP *pSMBr = NULL;
	char *data_offset;
	int name_len;
	int name_len_target;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, param_offset, offset, byte_count;

	cFYI(1, ("In Symlink Unix style"));
createSymLinkRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifs_strtoUCS((__le16 *) pSMB->FileName, fromName, PATH_MAX
				  /* find define for this maxpathcomponent */
				  , nls_codepage);
		name_len++;	/* trailing null */
		name_len *= 2;

	} else {		/* BB improve the check for buffer overruns BB */
		name_len = strnlen(fromName, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, fromName, name_len);
	}
	params = 6 + name_len;
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_spi_req,
                                     InformationLevel) - 4;
	offset = param_offset + params;

	data_offset = (char *) (&pSMB->hdr.Protocol) + offset;
	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len_target =
		    cifs_strtoUCS((__le16 *) data_offset, toName, PATH_MAX
				  /* find define for this maxpathcomponent */
				  , nls_codepage);
		name_len_target++;	/* trailing null */
		name_len_target *= 2;
	} else {		/* BB improve the check for buffer overruns BB */
		name_len_target = strnlen(toName, PATH_MAX);
		name_len_target++;	/* trailing null */
		strncpy(data_offset, toName, name_len_target);
	}

	pSMB->MaxParameterCount = cpu_to_le16(2);
	/* BB find exact max on data count below from sess */
	pSMB->MaxDataCount = cpu_to_le16(1000);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_PATH_INFORMATION);
	byte_count = 3 /* pad */  + params + name_len_target;
	pSMB->DataCount = cpu_to_le16(name_len_target);
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(param_offset);
	pSMB->DataOffset = cpu_to_le16(offset);
	pSMB->InformationLevel = cpu_to_le16(SMB_SET_FILE_UNIX_LINK);
	pSMB->Reserved4 = 0;
	pSMB->hdr.smb_buf_length += byte_count;
	pSMB->ByteCount = cpu_to_le16(byte_count);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	cifs_stats_inc(&tcon->num_symlinks);
	if (rc) {
		cFYI(1,
		     ("Send error in SetPathInfo (create symlink) = %d",
		      rc));
	}

	if (pSMB)
		cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto createSymLinkRetry;

	return rc;
}

int
CIFSUnixCreateHardLink(const int xid, struct cifsTconInfo *tcon,
		       const char *fromName, const char *toName,
		       const struct nls_table *nls_codepage, int remap)
{
	TRANSACTION2_SPI_REQ *pSMB = NULL;
	TRANSACTION2_SPI_RSP *pSMBr = NULL;
	char *data_offset;
	int name_len;
	int name_len_target;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, param_offset, offset, byte_count;

	cFYI(1, ("In Create Hard link Unix style"));
createHardLinkRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len = cifsConvertToUCS((__le16 *) pSMB->FileName, toName,
					    PATH_MAX, nls_codepage, remap);
		name_len++;	/* trailing null */
		name_len *= 2;

	} else {		/* BB improve the check for buffer overruns BB */
		name_len = strnlen(toName, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, toName, name_len);
	}
	params = 6 + name_len;
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_spi_req,
                                     InformationLevel) - 4;
	offset = param_offset + params;

	data_offset = (char *) (&pSMB->hdr.Protocol) + offset;
	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len_target =
		    cifsConvertToUCS((__le16 *) data_offset, fromName, PATH_MAX,
				     nls_codepage, remap);
		name_len_target++;	/* trailing null */
		name_len_target *= 2;
	} else {		/* BB improve the check for buffer overruns BB */
		name_len_target = strnlen(fromName, PATH_MAX);
		name_len_target++;	/* trailing null */
		strncpy(data_offset, fromName, name_len_target);
	}

	pSMB->MaxParameterCount = cpu_to_le16(2);
	/* BB find exact max on data count below from sess*/
	pSMB->MaxDataCount = cpu_to_le16(1000);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_PATH_INFORMATION);
	byte_count = 3 /* pad */  + params + name_len_target;
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->DataCount = cpu_to_le16(name_len_target);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->ParameterOffset = cpu_to_le16(param_offset);
	pSMB->DataOffset = cpu_to_le16(offset);
	pSMB->InformationLevel = cpu_to_le16(SMB_SET_FILE_UNIX_HLINK);
	pSMB->Reserved4 = 0;
	pSMB->hdr.smb_buf_length += byte_count;
	pSMB->ByteCount = cpu_to_le16(byte_count);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	cifs_stats_inc(&tcon->num_hardlinks);
	if (rc) {
		cFYI(1, ("Send error in SetPathInfo (hard link) = %d", rc));
	}

	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto createHardLinkRetry;

	return rc;
}

int
CIFSCreateHardLink(const int xid, struct cifsTconInfo *tcon,
		   const char *fromName, const char *toName,
		   const struct nls_table *nls_codepage, int remap)
{
	int rc = 0;
	NT_RENAME_REQ *pSMB = NULL;
	RENAME_RSP *pSMBr = NULL;
	int bytes_returned;
	int name_len, name_len2;
	__u16 count;

	cFYI(1, ("In CIFSCreateHardLink"));
winCreateHardLinkRetry:

	rc = smb_init(SMB_COM_NT_RENAME, 4, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->SearchAttributes =
	    cpu_to_le16(ATTR_READONLY | ATTR_HIDDEN | ATTR_SYSTEM |
			ATTR_DIRECTORY);
	pSMB->Flags = cpu_to_le16(CREATE_HARD_LINK);
	pSMB->ClusterCount = 0;

	pSMB->BufferFormat = 0x04;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifsConvertToUCS((__le16 *) pSMB->OldFileName, fromName,
				     PATH_MAX, nls_codepage, remap);
		name_len++;	/* trailing null */
		name_len *= 2;
		pSMB->OldFileName[name_len] = 0;	/* pad */
		pSMB->OldFileName[name_len + 1] = 0x04; 
		name_len2 =
		    cifsConvertToUCS((__le16 *)&pSMB->OldFileName[name_len + 2], 
				     toName, PATH_MAX, nls_codepage, remap);
		name_len2 += 1 /* trailing null */  + 1 /* Signature word */ ;
		name_len2 *= 2;	/* convert to bytes */
	} else {		/* BB improve the check for buffer overruns BB */
		name_len = strnlen(fromName, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->OldFileName, fromName, name_len);
		name_len2 = strnlen(toName, PATH_MAX);
		name_len2++;	/* trailing null */
		pSMB->OldFileName[name_len] = 0x04;	/* 2nd buffer format */
		strncpy(&pSMB->OldFileName[name_len + 1], toName, name_len2);
		name_len2++;	/* trailing null */
		name_len2++;	/* signature byte */
	}

	count = 1 /* string type byte */  + name_len + name_len2;
	pSMB->hdr.smb_buf_length += count;
	pSMB->ByteCount = cpu_to_le16(count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	cifs_stats_inc(&tcon->num_hardlinks);
	if (rc) {
		cFYI(1, ("Send error in hard link (NT rename) = %d", rc));
	}
	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto winCreateHardLinkRetry;

	return rc;
}

int
CIFSSMBUnixQuerySymLink(const int xid, struct cifsTconInfo *tcon,
			const unsigned char *searchName,
			char *symlinkinfo, const int buflen,
			const struct nls_table *nls_codepage)
{
/* SMB_QUERY_FILE_UNIX_LINK */
	TRANSACTION2_QPI_REQ *pSMB = NULL;
	TRANSACTION2_QPI_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned;
	int name_len;
	__u16 params, byte_count;

	cFYI(1, ("In QPathSymLinkInfo (Unix) for path %s", searchName));

querySymLinkRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifs_strtoUCS((__le16 *) pSMB->FileName, searchName, PATH_MAX
				  /* find define for this maxpathcomponent */
				  , nls_codepage);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {		/* BB improve the check for buffer overruns BB */
		name_len = strnlen(searchName, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, searchName, name_len);
	}

	params = 2 /* level */  + 4 /* rsrvd */  + name_len /* incl null */ ;
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	/* BB find exact max data count below from sess structure BB */
	pSMB->MaxDataCount = cpu_to_le16(4000);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset = cpu_to_le16(offsetof(
        struct smb_com_transaction2_qpi_req ,InformationLevel) - 4);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_PATH_INFORMATION);
	byte_count = params + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(params);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_FILE_UNIX_LINK);
	pSMB->Reserved4 = 0;
	pSMB->hdr.smb_buf_length += byte_count;
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("Send error in QuerySymLinkInfo = %d", rc));
	} else {
		/* decode response */

		rc = validate_t2((struct smb_t2_rsp *)pSMBr);
		if (rc || (pSMBr->ByteCount < 2))
		/* BB also check enough total bytes returned */
			rc = -EIO;	/* bad smb */
		else {
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);
			__u16 count = le16_to_cpu(pSMBr->t2.DataCount);

			if (pSMBr->hdr.Flags2 & SMBFLG2_UNICODE) {
				name_len = UniStrnlen((wchar_t *) ((char *)
					&pSMBr->hdr.Protocol +data_offset),
					min_t(const int, buflen,count) / 2);
			/* BB FIXME investigate remapping reserved chars here */
				cifs_strfromUCS_le(symlinkinfo,
					(__le16 *) ((char *)&pSMBr->hdr.Protocol +
						data_offset),
					name_len, nls_codepage);
			} else {
				strncpy(symlinkinfo,
					(char *) &pSMBr->hdr.Protocol + 
						data_offset,
					min_t(const int, buflen, count));
			}
			symlinkinfo[buflen] = 0;
	/* just in case so calling code does not go off the end of buffer */
		}
	}
	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto querySymLinkRetry;
	return rc;
}

int
CIFSSMBQueryReparseLinkInfo(const int xid, struct cifsTconInfo *tcon,
			const unsigned char *searchName,
			char *symlinkinfo, const int buflen,__u16 fid,
			const struct nls_table *nls_codepage)
{
	int rc = 0;
	int bytes_returned;
	int name_len;
	struct smb_com_transaction_ioctl_req * pSMB;
	struct smb_com_transaction_ioctl_rsp * pSMBr;

	cFYI(1, ("In Windows reparse style QueryLink for path %s", searchName));
	rc = smb_init(SMB_COM_NT_TRANSACT, 23, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->TotalParameterCount = 0 ;
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le32(2);
	/* BB find exact data count max from sess structure BB */
	pSMB->MaxDataCount = cpu_to_le32(4000);
	pSMB->MaxSetupCount = 4;
	pSMB->Reserved = 0;
	pSMB->ParameterOffset = 0;
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 4;
	pSMB->SubCommand = cpu_to_le16(NT_TRANSACT_IOCTL);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->FunctionCode = cpu_to_le32(FSCTL_GET_REPARSE_POINT);
	pSMB->IsFsctl = 1; /* FSCTL */
	pSMB->IsRootFlag = 0;
	pSMB->Fid = fid; /* file handle always le */
	pSMB->ByteCount = 0;

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("Send error in QueryReparseLinkInfo = %d", rc));
	} else {		/* decode response */
		__u32 data_offset = le32_to_cpu(pSMBr->DataOffset);
		__u32 data_count = le32_to_cpu(pSMBr->DataCount);
		if ((pSMBr->ByteCount < 2) || (data_offset > 512))
		/* BB also check enough total bytes returned */
			rc = -EIO;	/* bad smb */
		else {
			if(data_count && (data_count < 2048)) {
				char * end_of_smb = pSMBr->ByteCount + (char *)&pSMBr->ByteCount;

				struct reparse_data * reparse_buf = (struct reparse_data *)
					((char *)&pSMBr->hdr.Protocol + data_offset);
				if((char*)reparse_buf >= end_of_smb) {
					rc = -EIO;
					goto qreparse_out;
				}
				if((reparse_buf->LinkNamesBuf + 
					reparse_buf->TargetNameOffset +
					reparse_buf->TargetNameLen) >
						end_of_smb) {
					cFYI(1,("reparse buf extended beyond SMB"));
					rc = -EIO;
					goto qreparse_out;
				}
				
				if (pSMBr->hdr.Flags2 & SMBFLG2_UNICODE) {
					name_len = UniStrnlen((wchar_t *)
							(reparse_buf->LinkNamesBuf + 
							reparse_buf->TargetNameOffset),
							min(buflen/2, reparse_buf->TargetNameLen / 2)); 
					cifs_strfromUCS_le(symlinkinfo,
						(__le16 *) (reparse_buf->LinkNamesBuf + 
						reparse_buf->TargetNameOffset),
						name_len, nls_codepage);
				} else { /* ASCII names */
					strncpy(symlinkinfo,reparse_buf->LinkNamesBuf + 
						reparse_buf->TargetNameOffset, 
						min_t(const int, buflen, reparse_buf->TargetNameLen));
				}
			} else {
				rc = -EIO;
				cFYI(1,("Invalid return data count on get reparse info ioctl"));
			}
			symlinkinfo[buflen] = 0; /* just in case so the caller
					does not go off the end of the buffer */
			cFYI(1,("readlink result - %s ",symlinkinfo));
		}
	}
qreparse_out:
	cifs_buf_release(pSMB);

	/* Note: On -EAGAIN error only caller can retry on handle based calls
		since file handle passed in no longer valid */

	return rc;
}

#ifdef CONFIG_CIFS_POSIX

/*Convert an Access Control Entry from wire format to local POSIX xattr format*/
static void cifs_convert_ace(posix_acl_xattr_entry * ace, struct cifs_posix_ace * cifs_ace)
{
	/* u8 cifs fields do not need le conversion */
	ace->e_perm = cpu_to_le16(cifs_ace->cifs_e_perm);
	ace->e_tag  = cpu_to_le16(cifs_ace->cifs_e_tag);
	ace->e_id   = cpu_to_le32(le64_to_cpu(cifs_ace->cifs_uid));
	/* cFYI(1,("perm %d tag %d id %d",ace->e_perm,ace->e_tag,ace->e_id)); */

	return;
}

/* Convert ACL from CIFS POSIX wire format to local Linux POSIX ACL xattr */
static int cifs_copy_posix_acl(char * trgt,char * src, const int buflen,
				const int acl_type,const int size_of_data_area)
{
	int size =  0;
	int i;
	__u16 count;
	struct cifs_posix_ace * pACE;
	struct cifs_posix_acl * cifs_acl = (struct cifs_posix_acl *)src;
	posix_acl_xattr_header * local_acl = (posix_acl_xattr_header *)trgt;

	if (le16_to_cpu(cifs_acl->version) != CIFS_ACL_VERSION)
		return -EOPNOTSUPP;

	if(acl_type & ACL_TYPE_ACCESS) {
		count = le16_to_cpu(cifs_acl->access_entry_count);
		pACE = &cifs_acl->ace_array[0];
		size = sizeof(struct cifs_posix_acl);
		size += sizeof(struct cifs_posix_ace) * count;
		/* check if we would go beyond end of SMB */
		if(size_of_data_area < size) {
			cFYI(1,("bad CIFS POSIX ACL size %d vs. %d",size_of_data_area,size));
			return -EINVAL;
		}
	} else if(acl_type & ACL_TYPE_DEFAULT) {
		count = le16_to_cpu(cifs_acl->access_entry_count);
		size = sizeof(struct cifs_posix_acl);
		size += sizeof(struct cifs_posix_ace) * count;
/* skip past access ACEs to get to default ACEs */
		pACE = &cifs_acl->ace_array[count];
		count = le16_to_cpu(cifs_acl->default_entry_count);
		size += sizeof(struct cifs_posix_ace) * count;
		/* check if we would go beyond end of SMB */
		if(size_of_data_area < size)
			return -EINVAL;
	} else {
		/* illegal type */
		return -EINVAL;
	}

	size = posix_acl_xattr_size(count);
	if((buflen == 0) || (local_acl == NULL)) {
		/* used to query ACL EA size */				
	} else if(size > buflen) {
		return -ERANGE;
	} else /* buffer big enough */ {
		local_acl->a_version = cpu_to_le32(POSIX_ACL_XATTR_VERSION);
		for(i = 0;i < count ;i++) {
			cifs_convert_ace(&local_acl->a_entries[i],pACE);
			pACE ++;
		}
	}
	return size;
}

static __u16 convert_ace_to_cifs_ace(struct cifs_posix_ace * cifs_ace,
			const posix_acl_xattr_entry * local_ace)
{
	__u16 rc = 0; /* 0 = ACL converted ok */

	cifs_ace->cifs_e_perm = le16_to_cpu(local_ace->e_perm);
	cifs_ace->cifs_e_tag =  le16_to_cpu(local_ace->e_tag);
	/* BB is there a better way to handle the large uid? */
	if(local_ace->e_id == cpu_to_le32(-1)) {
	/* Probably no need to le convert -1 on any arch but can not hurt */
		cifs_ace->cifs_uid = cpu_to_le64(-1);
	} else 
		cifs_ace->cifs_uid = cpu_to_le64(le32_to_cpu(local_ace->e_id));
        /*cFYI(1,("perm %d tag %d id %d",ace->e_perm,ace->e_tag,ace->e_id));*/
	return rc;
}

/* Convert ACL from local Linux POSIX xattr to CIFS POSIX ACL wire format */
static __u16 ACL_to_cifs_posix(char * parm_data,const char * pACL,const int buflen,
		const int acl_type)
{
	__u16 rc = 0;
        struct cifs_posix_acl * cifs_acl = (struct cifs_posix_acl *)parm_data;
        posix_acl_xattr_header * local_acl = (posix_acl_xattr_header *)pACL;
	int count;
	int i;

	if((buflen == 0) || (pACL == NULL) || (cifs_acl == NULL))
		return 0;

	count = posix_acl_xattr_count((size_t)buflen);
	cFYI(1,("setting acl with %d entries from buf of length %d and version of %d",
		count, buflen, le32_to_cpu(local_acl->a_version)));
	if(le32_to_cpu(local_acl->a_version) != 2) {
		cFYI(1,("unknown POSIX ACL version %d",
		     le32_to_cpu(local_acl->a_version)));
		return 0;
	}
	cifs_acl->version = cpu_to_le16(1);
	if(acl_type == ACL_TYPE_ACCESS) 
		cifs_acl->access_entry_count = cpu_to_le16(count);
	else if(acl_type == ACL_TYPE_DEFAULT)
		cifs_acl->default_entry_count = cpu_to_le16(count);
	else {
		cFYI(1,("unknown ACL type %d",acl_type));
		return 0;
	}
	for(i=0;i<count;i++) {
		rc = convert_ace_to_cifs_ace(&cifs_acl->ace_array[i],
					&local_acl->a_entries[i]);
		if(rc != 0) {
			/* ACE not converted */
			break;
		}
	}
	if(rc == 0) {
		rc = (__u16)(count * sizeof(struct cifs_posix_ace));
		rc += sizeof(struct cifs_posix_acl);
		/* BB add check to make sure ACL does not overflow SMB */
	}
	return rc;
}

int
CIFSSMBGetPosixACL(const int xid, struct cifsTconInfo *tcon,
                        const unsigned char *searchName,
                        char *acl_inf, const int buflen, const int acl_type,
                        const struct nls_table *nls_codepage, int remap)
{
/* SMB_QUERY_POSIX_ACL */
	TRANSACTION2_QPI_REQ *pSMB = NULL;
	TRANSACTION2_QPI_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned;
	int name_len;
	__u16 params, byte_count;
                                                                                                                                             
	cFYI(1, ("In GetPosixACL (Unix) for path %s", searchName));

queryAclRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		(void **) &pSMBr);
	if (rc)
		return rc;
                                                                                                                                             
	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
			cifsConvertToUCS((__le16 *) pSMB->FileName, searchName, 
					 PATH_MAX, nls_codepage, remap);
		name_len++;     /* trailing null */
		name_len *= 2;
		pSMB->FileName[name_len] = 0;
		pSMB->FileName[name_len+1] = 0;
	} else {                /* BB improve the check for buffer overruns BB */
		name_len = strnlen(searchName, PATH_MAX);
		name_len++;     /* trailing null */
		strncpy(pSMB->FileName, searchName, name_len);
	}

	params = 2 /* level */  + 4 /* rsrvd */  + name_len /* incl null */ ;
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
        /* BB find exact max data count below from sess structure BB */
	pSMB->MaxDataCount = cpu_to_le16(4000);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset = cpu_to_le16(
		offsetof(struct smb_com_transaction2_qpi_req ,InformationLevel) - 4);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_PATH_INFORMATION);
	byte_count = params + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(params);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_POSIX_ACL);
	pSMB->Reserved4 = 0;
	pSMB->hdr.smb_buf_length += byte_count;
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
		(struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("Send error in Query POSIX ACL = %d", rc));
	} else {
		/* decode response */
 
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);
		if (rc || (pSMBr->ByteCount < 2))
		/* BB also check enough total bytes returned */
			rc = -EIO;      /* bad smb */
		else {
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);
			__u16 count = le16_to_cpu(pSMBr->t2.DataCount);
			rc = cifs_copy_posix_acl(acl_inf,
				(char *)&pSMBr->hdr.Protocol+data_offset,
				buflen,acl_type,count);
		}
	}
	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto queryAclRetry;
	return rc;
}

int
CIFSSMBSetPosixACL(const int xid, struct cifsTconInfo *tcon,
                        const unsigned char *fileName,
                        const char *local_acl, const int buflen, 
			const int acl_type,
                        const struct nls_table *nls_codepage, int remap)
{
	struct smb_com_transaction2_spi_req *pSMB = NULL;
	struct smb_com_transaction2_spi_rsp *pSMBr = NULL;
	char *parm_data;
	int name_len;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, byte_count, data_count, param_offset, offset;

	cFYI(1, ("In SetPosixACL (Unix) for path %s", fileName));
setAclRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
                      (void **) &pSMBr);
	if (rc)
		return rc;
	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
			cifsConvertToUCS((__le16 *) pSMB->FileName, fileName, 
				      PATH_MAX, nls_codepage, remap);
		name_len++;     /* trailing null */
		name_len *= 2;
	} else {                /* BB improve the check for buffer overruns BB */
		name_len = strnlen(fileName, PATH_MAX);
		name_len++;     /* trailing null */
		strncpy(pSMB->FileName, fileName, name_len);
	}
	params = 6 + name_len;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(1000); /* BB find max SMB size from sess */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_spi_req,
                                     InformationLevel) - 4;
	offset = param_offset + params;
	parm_data = ((char *) &pSMB->hdr.Protocol) + offset;
	pSMB->ParameterOffset = cpu_to_le16(param_offset);

	/* convert to on the wire format for POSIX ACL */
	data_count = ACL_to_cifs_posix(parm_data,local_acl,buflen,acl_type);

	if(data_count == 0) {
		rc = -EOPNOTSUPP;
		goto setACLerrorExit;
	}
	pSMB->DataOffset = cpu_to_le16(offset);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_PATH_INFORMATION);
	pSMB->InformationLevel = cpu_to_le16(SMB_SET_POSIX_ACL);
	byte_count = 3 /* pad */  + params + data_count;
	pSMB->DataCount = cpu_to_le16(data_count);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->Reserved4 = 0;
	pSMB->hdr.smb_buf_length += byte_count;
	pSMB->ByteCount = cpu_to_le16(byte_count);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
                         (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("Set POSIX ACL returned %d", rc));
	}

setACLerrorExit:
	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto setAclRetry;
	return rc;
}

/* BB fix tabs in this function FIXME BB */
int
CIFSGetExtAttr(const int xid, struct cifsTconInfo *tcon,
                const int netfid, __u64 * pExtAttrBits, __u64 *pMask)
{
        int rc = 0;
        struct smb_t2_qfi_req *pSMB = NULL;
        struct smb_t2_qfi_rsp *pSMBr = NULL;
        int bytes_returned;
        __u16 params, byte_count;

        cFYI(1,("In GetExtAttr"));
        if(tcon == NULL)
                return -ENODEV;

GetExtAttrRetry:
        rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
                      (void **) &pSMBr);
        if (rc)
                return rc;

        params = 2 /* level */ +2 /* fid */;
        pSMB->t2.TotalDataCount = 0;
        pSMB->t2.MaxParameterCount = cpu_to_le16(4);
        /* BB find exact max data count below from sess structure BB */
        pSMB->t2.MaxDataCount = cpu_to_le16(4000);
        pSMB->t2.MaxSetupCount = 0;
        pSMB->t2.Reserved = 0;
        pSMB->t2.Flags = 0;
        pSMB->t2.Timeout = 0;
        pSMB->t2.Reserved2 = 0;
        pSMB->t2.ParameterOffset = cpu_to_le16(offsetof(struct smb_t2_qfi_req,
			Fid) - 4);
        pSMB->t2.DataCount = 0;
        pSMB->t2.DataOffset = 0;
        pSMB->t2.SetupCount = 1;
        pSMB->t2.Reserved3 = 0;
        pSMB->t2.SubCommand = cpu_to_le16(TRANS2_QUERY_FILE_INFORMATION);
        byte_count = params + 1 /* pad */ ;
        pSMB->t2.TotalParameterCount = cpu_to_le16(params);
        pSMB->t2.ParameterCount = pSMB->t2.TotalParameterCount;
        pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_ATTR_FLAGS);
        pSMB->Pad = 0;
	pSMB->Fid = netfid;
        pSMB->hdr.smb_buf_length += byte_count;
        pSMB->t2.ByteCount = cpu_to_le16(byte_count);

        rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
                (struct smb_hdr *) pSMBr, &bytes_returned, 0);
        if (rc) {
                cFYI(1, ("error %d in GetExtAttr", rc));
        } else {
                /* decode response */
                rc = validate_t2((struct smb_t2_rsp *)pSMBr);
                if (rc || (pSMBr->ByteCount < 2))
                /* BB also check enough total bytes returned */
                        /* If rc should we check for EOPNOSUPP and
                        disable the srvino flag? or in caller? */
                        rc = -EIO;      /* bad smb */
                else {
                        __u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);
                        __u16 count = le16_to_cpu(pSMBr->t2.DataCount);
                        struct file_chattr_info * pfinfo;
                        /* BB Do we need a cast or hash here ? */
                        if(count != 16) {
                                cFYI(1, ("Illegal size ret in GetExtAttr"));
                                rc = -EIO;
                                goto GetExtAttrOut;
                        }
                        pfinfo = (struct file_chattr_info *)
                                (data_offset + (char *) &pSMBr->hdr.Protocol);
                        *pExtAttrBits = le64_to_cpu(pfinfo->mode);
			*pMask = le64_to_cpu(pfinfo->mask);
                }
        }
GetExtAttrOut:
        cifs_buf_release(pSMB);
        if (rc == -EAGAIN)
                goto GetExtAttrRetry;
        return rc;
}


#endif /* CONFIG_POSIX */

/* Legacy Query Path Information call for lookup to old servers such
   as Win9x/WinME */
int SMBQueryInformation(const int xid, struct cifsTconInfo *tcon,
                 const unsigned char *searchName,
                 FILE_ALL_INFO * pFinfo,
                 const struct nls_table *nls_codepage, int remap)
{
	QUERY_INFORMATION_REQ * pSMB;
	QUERY_INFORMATION_RSP * pSMBr;
	int rc = 0;
	int bytes_returned;
	int name_len;

	cFYI(1, ("In SMBQPath path %s", searchName)); 
QInfRetry:
	rc = smb_init(SMB_COM_QUERY_INFORMATION, 0, tcon, (void **) &pSMB,
                      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
                    cifsConvertToUCS((__le16 *) pSMB->FileName, searchName,
                                     PATH_MAX, nls_codepage, remap);
		name_len++;     /* trailing null */
		name_len *= 2;
	} else {               
		name_len = strnlen(searchName, PATH_MAX);
		name_len++;     /* trailing null */
		strncpy(pSMB->FileName, searchName, name_len);
	}
	pSMB->BufferFormat = 0x04;
	name_len++; /* account for buffer type byte */	
	pSMB->hdr.smb_buf_length += (__u16) name_len;
	pSMB->ByteCount = cpu_to_le16(name_len);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
                         (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("Send error in QueryInfo = %d", rc));
	} else if (pFinfo) {            /* decode response */
		memset(pFinfo, 0, sizeof(FILE_ALL_INFO));
		pFinfo->AllocationSize =
			cpu_to_le64(le32_to_cpu(pSMBr->size));
		pFinfo->EndOfFile = pFinfo->AllocationSize;
		pFinfo->Attributes =
			cpu_to_le32(le16_to_cpu(pSMBr->attr));
	} else
		rc = -EIO; /* bad buffer passed in */

	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto QInfRetry;

	return rc;
}




int
CIFSSMBQPathInfo(const int xid, struct cifsTconInfo *tcon,
		 const unsigned char *searchName,
		 FILE_ALL_INFO * pFindData,
		 const struct nls_table *nls_codepage, int remap)
{
/* level 263 SMB_QUERY_FILE_ALL_INFO */
	TRANSACTION2_QPI_REQ *pSMB = NULL;
	TRANSACTION2_QPI_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned;
	int name_len;
	__u16 params, byte_count;

/* cFYI(1, ("In QPathInfo path %s", searchName)); */
QPathInfoRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifsConvertToUCS((__le16 *) pSMB->FileName, searchName, 
				     PATH_MAX, nls_codepage, remap);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {		/* BB improve the check for buffer overruns BB */
		name_len = strnlen(searchName, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, searchName, name_len);
	}

	params = 2 /* level */ + 4 /* reserved */ + name_len /* includes NUL */ ;
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(4000);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset = cpu_to_le16(offsetof(
        struct smb_com_transaction2_qpi_req ,InformationLevel) - 4);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_PATH_INFORMATION);
	byte_count = params + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(params);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_FILE_ALL_INFO);
	pSMB->Reserved4 = 0;
	pSMB->hdr.smb_buf_length += byte_count;
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("Send error in QPathInfo = %d", rc));
	} else {		/* decode response */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);

		if (rc || (pSMBr->ByteCount < 40)) 
			rc = -EIO;	/* bad smb */
		else if (pFindData){
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);
			memcpy((char *) pFindData,
			       (char *) &pSMBr->hdr.Protocol +
			       data_offset, sizeof (FILE_ALL_INFO));
		} else
		    rc = -ENOMEM;
	}
	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto QPathInfoRetry;

	return rc;
}

int
CIFSSMBUnixQPathInfo(const int xid, struct cifsTconInfo *tcon,
		     const unsigned char *searchName,
		     FILE_UNIX_BASIC_INFO * pFindData,
		     const struct nls_table *nls_codepage, int remap)
{
/* SMB_QUERY_FILE_UNIX_BASIC */
	TRANSACTION2_QPI_REQ *pSMB = NULL;
	TRANSACTION2_QPI_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned = 0;
	int name_len;
	__u16 params, byte_count;

	cFYI(1, ("In QPathInfo (Unix) the path %s", searchName));
UnixQPathInfoRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifsConvertToUCS((__le16 *) pSMB->FileName, searchName,
				  PATH_MAX, nls_codepage, remap);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {		/* BB improve the check for buffer overruns BB */
		name_len = strnlen(searchName, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, searchName, name_len);
	}

	params = 2 /* level */ + 4 /* reserved */ + name_len /* includes NUL */ ;
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxDataCount = cpu_to_le16(4000); 
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset = cpu_to_le16(offsetof(
        struct smb_com_transaction2_qpi_req ,InformationLevel) - 4);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_PATH_INFORMATION);
	byte_count = params + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(params);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_FILE_UNIX_BASIC);
	pSMB->Reserved4 = 0;
	pSMB->hdr.smb_buf_length += byte_count;
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("Send error in QPathInfo = %d", rc));
	} else {		/* decode response */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);

		if (rc || (pSMBr->ByteCount < sizeof(FILE_UNIX_BASIC_INFO))) {
			rc = -EIO;	/* bad smb */
		} else {
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);
			memcpy((char *) pFindData,
			       (char *) &pSMBr->hdr.Protocol +
			       data_offset,
			       sizeof (FILE_UNIX_BASIC_INFO));
		}
	}
	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto UnixQPathInfoRetry;

	return rc;
}

#if 0  /* function unused at present */
int CIFSFindSingle(const int xid, struct cifsTconInfo *tcon,
	       const char *searchName, FILE_ALL_INFO * findData,
	       const struct nls_table *nls_codepage)
{
/* level 257 SMB_ */
	TRANSACTION2_FFIRST_REQ *pSMB = NULL;
	TRANSACTION2_FFIRST_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned;
	int name_len;
	__u16 params, byte_count;

	cFYI(1, ("In FindUnique"));
findUniqueRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifsConvertToUCS((__le16 *) pSMB->FileName, searchName, PATH_MAX
				  /* find define for this maxpathcomponent */
				  , nls_codepage);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {		/* BB improve the check for buffer overruns BB */
		name_len = strnlen(searchName, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, searchName, name_len);
	}

	params = 12 + name_len /* includes null */ ;
	pSMB->TotalDataCount = 0;	/* no EAs */
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(4000);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset = cpu_to_le16(
         offsetof(struct smb_com_transaction2_ffirst_req,InformationLevel) - 4);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;	/* one byte, no need to le convert */
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_FIND_FIRST);
	byte_count = params + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(params);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->SearchAttributes =
	    cpu_to_le16(ATTR_READONLY | ATTR_HIDDEN | ATTR_SYSTEM |
			ATTR_DIRECTORY);
	pSMB->SearchCount = cpu_to_le16(16);	/* BB increase */
	pSMB->SearchFlags = cpu_to_le16(1);
	pSMB->InformationLevel = cpu_to_le16(SMB_FIND_FILE_DIRECTORY_INFO);
	pSMB->SearchStorageType = 0;	/* BB what should we set this to? BB */
	pSMB->hdr.smb_buf_length += byte_count;
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);

	if (rc) {
		cFYI(1, ("Send error in FindFileDirInfo = %d", rc));
	} else {		/* decode response */
		cifs_stats_inc(&tcon->num_ffirst);
		/* BB fill in */
	}

	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto findUniqueRetry;

	return rc;
}
#endif /* end unused (temporarily) function */

/* xid, tcon, searchName and codepage are input parms, rest are returned */
int
CIFSFindFirst(const int xid, struct cifsTconInfo *tcon,
	      const char *searchName, 
	      const struct nls_table *nls_codepage,
	      __u16 *	pnetfid,
	      struct cifs_search_info * psrch_inf, int remap, const char dirsep)
{
/* level 257 SMB_ */
	TRANSACTION2_FFIRST_REQ *pSMB = NULL;
	TRANSACTION2_FFIRST_RSP *pSMBr = NULL;
	T2_FFIRST_RSP_PARMS * parms;
	int rc = 0;
	int bytes_returned = 0;
	int name_len;
	__u16 params, byte_count;

	cFYI(1, ("In FindFirst for %s",searchName));

findFirstRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifsConvertToUCS((__le16 *) pSMB->FileName,searchName,
				 PATH_MAX, nls_codepage, remap);
		/* We can not add the asterik earlier in case
		it got remapped to 0xF03A as if it were part of the
		directory name instead of a wildcard */
		name_len *= 2;
		pSMB->FileName[name_len] = dirsep;
		pSMB->FileName[name_len+1] = 0;
		pSMB->FileName[name_len+2] = '*';
		pSMB->FileName[name_len+3] = 0;
		name_len += 4; /* now the trailing null */
		pSMB->FileName[name_len] = 0; /* null terminate just in case */
		pSMB->FileName[name_len+1] = 0;
		name_len += 2;
	} else {	/* BB add check for overrun of SMB buf BB */
		name_len = strnlen(searchName, PATH_MAX);
/* BB fix here and in unicode clause above ie
		if(name_len > buffersize-header)
			free buffer exit; BB */
		strncpy(pSMB->FileName, searchName, name_len);
		pSMB->FileName[name_len] = dirsep;
		pSMB->FileName[name_len+1] = '*';
		pSMB->FileName[name_len+2] = 0;
		name_len += 3;
	}

	params = 12 + name_len /* includes null */ ;
	pSMB->TotalDataCount = 0;	/* no EAs */
	pSMB->MaxParameterCount = cpu_to_le16(10);
	pSMB->MaxDataCount = cpu_to_le16((tcon->ses->server->maxBuf -
					  MAX_CIFS_HDR_SIZE) & 0xFFFFFF00);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	byte_count = params + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(params);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(
	  offsetof(struct smb_com_transaction2_ffirst_req, SearchAttributes) - 4);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;	/* one byte, no need to make endian neutral */
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_FIND_FIRST);
	pSMB->SearchAttributes =
	    cpu_to_le16(ATTR_READONLY | ATTR_HIDDEN | ATTR_SYSTEM |
			ATTR_DIRECTORY);
	pSMB->SearchCount= cpu_to_le16(CIFSMaxBufSize/sizeof(FILE_UNIX_INFO));
	pSMB->SearchFlags = cpu_to_le16(CIFS_SEARCH_CLOSE_AT_END | 
		CIFS_SEARCH_RETURN_RESUME);
	pSMB->InformationLevel = cpu_to_le16(psrch_inf->info_level);

	/* BB what should we set StorageType to? Does it matter? BB */
	pSMB->SearchStorageType = 0;
	pSMB->hdr.smb_buf_length += byte_count;
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	cifs_stats_inc(&tcon->num_ffirst);

	if (rc) {/* BB add logic to retry regular search if Unix search rejected unexpectedly by server */
		/* BB Add code to handle unsupported level rc */
		cFYI(1, ("Error in FindFirst = %d", rc));

		if (pSMB)
			cifs_buf_release(pSMB);

		/* BB eventually could optimize out free and realloc of buf */
		/*    for this case */
		if (rc == -EAGAIN)
			goto findFirstRetry;
	} else { /* decode response */
		/* BB remember to free buffer if error BB */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);
		if(rc == 0) {
			if (pSMBr->hdr.Flags2 & SMBFLG2_UNICODE)
				psrch_inf->unicode = TRUE;
			else
				psrch_inf->unicode = FALSE;

			psrch_inf->ntwrk_buf_start = (char *)pSMBr;
			psrch_inf->srch_entries_start = 
				(char *) &pSMBr->hdr.Protocol + 
					le16_to_cpu(pSMBr->t2.DataOffset);
			parms = (T2_FFIRST_RSP_PARMS *)((char *) &pSMBr->hdr.Protocol +
			       le16_to_cpu(pSMBr->t2.ParameterOffset));

			if(parms->EndofSearch)
				psrch_inf->endOfSearch = TRUE;
			else
				psrch_inf->endOfSearch = FALSE;

			psrch_inf->entries_in_buffer  = le16_to_cpu(parms->SearchCount);
			psrch_inf->index_of_last_entry = 
				psrch_inf->entries_in_buffer;
			*pnetfid = parms->SearchHandle;
		} else {
			cifs_buf_release(pSMB);
		}
	}

	return rc;
}

int CIFSFindNext(const int xid, struct cifsTconInfo *tcon,
            __u16 searchHandle, struct cifs_search_info * psrch_inf)
{
	TRANSACTION2_FNEXT_REQ *pSMB = NULL;
	TRANSACTION2_FNEXT_RSP *pSMBr = NULL;
	T2_FNEXT_RSP_PARMS * parms;
	char *response_data;
	int rc = 0;
	int bytes_returned, name_len;
	__u16 params, byte_count;

	cFYI(1, ("In FindNext"));

	if(psrch_inf->endOfSearch == TRUE)
		return -ENOENT;

	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		(void **) &pSMBr);
	if (rc)
		return rc;

	params = 14;    /* includes 2 bytes of null string, converted to LE below */
	byte_count = 0;
	pSMB->TotalDataCount = 0;       /* no EAs */
	pSMB->MaxParameterCount = cpu_to_le16(8);
	pSMB->MaxDataCount =
            cpu_to_le16((tcon->ses->server->maxBuf - MAX_CIFS_HDR_SIZE) & 0xFFFFFF00);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset =  cpu_to_le16(
	      offsetof(struct smb_com_transaction2_fnext_req,SearchHandle) - 4);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_FIND_NEXT);
	pSMB->SearchHandle = searchHandle;      /* always kept as le */
	pSMB->SearchCount =
		cpu_to_le16(CIFSMaxBufSize / sizeof (FILE_UNIX_INFO));
	/* test for Unix extensions */
/*	if (tcon->ses->capabilities & CAP_UNIX) {
		pSMB->InformationLevel = cpu_to_le16(SMB_FIND_FILE_UNIX);
		psrch_inf->info_level = SMB_FIND_FILE_UNIX;
	} else {
		pSMB->InformationLevel =
		   cpu_to_le16(SMB_FIND_FILE_DIRECTORY_INFO);
		psrch_inf->info_level = SMB_FIND_FILE_DIRECTORY_INFO;
	} */
	pSMB->InformationLevel = cpu_to_le16(psrch_inf->info_level);
	pSMB->ResumeKey = psrch_inf->resume_key;
	pSMB->SearchFlags =
	      cpu_to_le16(CIFS_SEARCH_CLOSE_AT_END | CIFS_SEARCH_RETURN_RESUME);

	name_len = psrch_inf->resume_name_len;
	params += name_len;
	if(name_len < PATH_MAX) {
		memcpy(pSMB->ResumeFileName, psrch_inf->presume_name, name_len);
		byte_count += name_len;
		/* 14 byte parm len above enough for 2 byte null terminator */
		pSMB->ResumeFileName[name_len] = 0;
		pSMB->ResumeFileName[name_len+1] = 0;
	} else {
		rc = -EINVAL;
		goto FNext2_err_exit;
	}
	byte_count = params + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(params);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->hdr.smb_buf_length += byte_count;
	pSMB->ByteCount = cpu_to_le16(byte_count);
                                                                                              
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			(struct smb_hdr *) pSMBr, &bytes_returned, 0);
	cifs_stats_inc(&tcon->num_fnext);
	if (rc) {
		if (rc == -EBADF) {
			psrch_inf->endOfSearch = TRUE;
			rc = 0; /* search probably was closed at end of search above */
		} else
			cFYI(1, ("FindNext returned = %d", rc));
	} else {                /* decode response */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);
		
		if(rc == 0) {
			/* BB fixme add lock for file (srch_info) struct here */
			if (pSMBr->hdr.Flags2 & SMBFLG2_UNICODE)
				psrch_inf->unicode = TRUE;
			else
				psrch_inf->unicode = FALSE;
			response_data = (char *) &pSMBr->hdr.Protocol +
			       le16_to_cpu(pSMBr->t2.ParameterOffset);
			parms = (T2_FNEXT_RSP_PARMS *)response_data;
			response_data = (char *)&pSMBr->hdr.Protocol +
				le16_to_cpu(pSMBr->t2.DataOffset);
			cifs_buf_release(psrch_inf->ntwrk_buf_start);
			psrch_inf->srch_entries_start = response_data;
			psrch_inf->ntwrk_buf_start = (char *)pSMB;
			if(parms->EndofSearch)
				psrch_inf->endOfSearch = TRUE;
			else
				psrch_inf->endOfSearch = FALSE;
                                                                                              
			psrch_inf->entries_in_buffer  = le16_to_cpu(parms->SearchCount);
			psrch_inf->index_of_last_entry +=
				psrch_inf->entries_in_buffer;
/*  cFYI(1,("fnxt2 entries in buf %d index_of_last %d",psrch_inf->entries_in_buffer,psrch_inf->index_of_last_entry)); */

			/* BB fixme add unlock here */
		}

	}

	/* BB On error, should we leave previous search buf (and count and
	last entry fields) intact or free the previous one? */

	/* Note: On -EAGAIN error only caller can retry on handle based calls
	since file handle passed in no longer valid */
FNext2_err_exit:
	if (rc != 0)
		cifs_buf_release(pSMB);
                                                                                              
	return rc;
}

int
CIFSFindClose(const int xid, struct cifsTconInfo *tcon, const __u16 searchHandle)
{
	int rc = 0;
	FINDCLOSE_REQ *pSMB = NULL;
	CLOSE_RSP *pSMBr = NULL; /* BB removeme BB */
	int bytes_returned;

	cFYI(1, ("In CIFSSMBFindClose"));
	rc = small_smb_init(SMB_COM_FIND_CLOSE2, 1, tcon, (void **)&pSMB);

	/* no sense returning error if session restarted
		as file handle has been closed */
	if(rc == -EAGAIN)
		return 0;
	if (rc)
		return rc;

	pSMBr = (CLOSE_RSP *)pSMB;  /* BB removeme BB */
	pSMB->FileID = searchHandle;
	pSMB->ByteCount = 0;
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cERROR(1, ("Send error in FindClose = %d", rc));
	}
	cifs_stats_inc(&tcon->num_fclose);
	cifs_small_buf_release(pSMB);

	/* Since session is dead, search handle closed on server already */
	if (rc == -EAGAIN)
		rc = 0;

	return rc;
}

int
CIFSGetSrvInodeNumber(const int xid, struct cifsTconInfo *tcon,
                const unsigned char *searchName,
                __u64 * inode_number,
                const struct nls_table *nls_codepage, int remap)
{
	int rc = 0;
	TRANSACTION2_QPI_REQ *pSMB = NULL;
	TRANSACTION2_QPI_RSP *pSMBr = NULL;
	int name_len, bytes_returned;
	__u16 params, byte_count;

	cFYI(1,("In GetSrvInodeNum for %s",searchName));
	if(tcon == NULL)
		return -ENODEV; 

GetInodeNumberRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
                      (void **) &pSMBr);
	if (rc)
		return rc;


	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
			cifsConvertToUCS((__le16 *) pSMB->FileName, searchName,
				PATH_MAX,nls_codepage, remap);
		name_len++;     /* trailing null */
		name_len *= 2;
	} else {                /* BB improve the check for buffer overruns BB */
		name_len = strnlen(searchName, PATH_MAX);
		name_len++;     /* trailing null */
		strncpy(pSMB->FileName, searchName, name_len);
	}

	params = 2 /* level */  + 4 /* rsrvd */  + name_len /* incl null */ ;
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	/* BB find exact max data count below from sess structure BB */
	pSMB->MaxDataCount = cpu_to_le16(4000);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset = cpu_to_le16(offsetof(
		struct smb_com_transaction2_qpi_req ,InformationLevel) - 4);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_PATH_INFORMATION);
	byte_count = params + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(params);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_FILE_INTERNAL_INFO);
	pSMB->Reserved4 = 0;
	pSMB->hdr.smb_buf_length += byte_count;
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
		(struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("error %d in QueryInternalInfo", rc));
	} else {
		/* decode response */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);
		if (rc || (pSMBr->ByteCount < 2))
		/* BB also check enough total bytes returned */
			/* If rc should we check for EOPNOSUPP and
			disable the srvino flag? or in caller? */
			rc = -EIO;      /* bad smb */
                else {
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);
			__u16 count = le16_to_cpu(pSMBr->t2.DataCount);
			struct file_internal_info * pfinfo;
			/* BB Do we need a cast or hash here ? */
			if(count < 8) {
				cFYI(1, ("Illegal size ret in QryIntrnlInf"));
				rc = -EIO;
				goto GetInodeNumOut;
			}
			pfinfo = (struct file_internal_info *)
				(data_offset + (char *) &pSMBr->hdr.Protocol);
			*inode_number = pfinfo->UniqueId;
		}
	}
GetInodeNumOut:
	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto GetInodeNumberRetry;
	return rc;
}

int
CIFSGetDFSRefer(const int xid, struct cifsSesInfo *ses,
		const unsigned char *searchName,
		unsigned char **targetUNCs,
		unsigned int *number_of_UNC_in_array,
		const struct nls_table *nls_codepage, int remap)
{
/* TRANS2_GET_DFS_REFERRAL */
	TRANSACTION2_GET_DFS_REFER_REQ *pSMB = NULL;
	TRANSACTION2_GET_DFS_REFER_RSP *pSMBr = NULL;
	struct dfs_referral_level_3 * referrals = NULL;
	int rc = 0;
	int bytes_returned;
	int name_len;
	unsigned int i;
	char * temp;
	__u16 params, byte_count;
	*number_of_UNC_in_array = 0;
	*targetUNCs = NULL;

	cFYI(1, ("In GetDFSRefer the path %s", searchName));
	if (ses == NULL)
		return -ENODEV;
getDFSRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, NULL, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;
	
	/* server pointer checked in called function, 
	but should never be null here anyway */
	pSMB->hdr.Mid = GetNextMid(ses->server);
	pSMB->hdr.Tid = ses->ipc_tid;
	pSMB->hdr.Uid = ses->Suid;
	if (ses->capabilities & CAP_STATUS32) {
		pSMB->hdr.Flags2 |= SMBFLG2_ERR_STATUS;
	}
	if (ses->capabilities & CAP_DFS) {
		pSMB->hdr.Flags2 |= SMBFLG2_DFS;
	}

	if (ses->capabilities & CAP_UNICODE) {
		pSMB->hdr.Flags2 |= SMBFLG2_UNICODE;
		name_len =
		    cifsConvertToUCS((__le16 *) pSMB->RequestFileName,
				     searchName, PATH_MAX, nls_codepage, remap);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {		/* BB improve the check for buffer overruns BB */
		name_len = strnlen(searchName, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->RequestFileName, searchName, name_len);
	}

	params = 2 /* level */  + name_len /*includes null */ ;
	pSMB->TotalDataCount = 0;
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->MaxParameterCount = 0;
	pSMB->MaxDataCount = cpu_to_le16(4000);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset = cpu_to_le16(offsetof(
        struct smb_com_transaction2_get_dfs_refer_req, MaxReferralLevel) - 4);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_GET_DFS_REFERRAL);
	byte_count = params + 3 /* pad */ ;
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->MaxReferralLevel = cpu_to_le16(3);
	pSMB->hdr.smb_buf_length += byte_count;
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("Send error in GetDFSRefer = %d", rc));
	} else {		/* decode response */
/* BB Add logic to parse referrals here */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);

		if (rc || (pSMBr->ByteCount < 17))      /* BB also check enough total bytes returned */
			rc = -EIO;      /* bad smb */
		else {
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset); 
			__u16 data_count = le16_to_cpu(pSMBr->t2.DataCount);

			cFYI(1,
			     ("Decoding GetDFSRefer response.  BCC: %d  Offset %d",
			      pSMBr->ByteCount, data_offset));
			referrals = 
			    (struct dfs_referral_level_3 *) 
					(8 /* sizeof start of data block */ +
					data_offset +
					(char *) &pSMBr->hdr.Protocol); 
			cFYI(1,("num_referrals: %d dfs flags: 0x%x ... \nfor referral one refer size: 0x%x srv type: 0x%x refer flags: 0x%x ttl: 0x%x",
				le16_to_cpu(pSMBr->NumberOfReferrals),le16_to_cpu(pSMBr->DFSFlags), le16_to_cpu(referrals->ReferralSize),le16_to_cpu(referrals->ServerType),le16_to_cpu(referrals->ReferralFlags),le16_to_cpu(referrals->TimeToLive)));
			/* BB This field is actually two bytes in from start of
			   data block so we could do safety check that DataBlock
			   begins at address of pSMBr->NumberOfReferrals */
			*number_of_UNC_in_array = le16_to_cpu(pSMBr->NumberOfReferrals);

			/* BB Fix below so can return more than one referral */
			if(*number_of_UNC_in_array > 1)
				*number_of_UNC_in_array = 1;

			/* get the length of the strings describing refs */
			name_len = 0;
			for(i=0;i<*number_of_UNC_in_array;i++) {
				/* make sure that DfsPathOffset not past end */
				__u16 offset = le16_to_cpu(referrals->DfsPathOffset);
				if (offset > data_count) {
					/* if invalid referral, stop here and do 
					not try to copy any more */
					*number_of_UNC_in_array = i;
					break;
				} 
				temp = ((char *)referrals) + offset;

				if (pSMBr->hdr.Flags2 & SMBFLG2_UNICODE) {
					name_len += UniStrnlen((wchar_t *)temp,data_count);
				} else {
					name_len += strnlen(temp,data_count);
				}
				referrals++;
				/* BB add check that referral pointer does not fall off end PDU */
				
			}
			/* BB add check for name_len bigger than bcc */
			*targetUNCs = 
				kmalloc(name_len+1+ (*number_of_UNC_in_array),GFP_KERNEL);
			if(*targetUNCs == NULL) {
				rc = -ENOMEM;
				goto GetDFSRefExit;
			}
			/* copy the ref strings */
			referrals =  
			    (struct dfs_referral_level_3 *) 
					(8 /* sizeof data hdr */ +
					data_offset + 
					(char *) &pSMBr->hdr.Protocol);

			for(i=0;i<*number_of_UNC_in_array;i++) {
				temp = ((char *)referrals) + le16_to_cpu(referrals->DfsPathOffset);
				if (pSMBr->hdr.Flags2 & SMBFLG2_UNICODE) {
					cifs_strfromUCS_le(*targetUNCs,
						(__le16 *) temp, name_len, nls_codepage);
				} else {
					strncpy(*targetUNCs,temp,name_len);
				}
				/*  BB update target_uncs pointers */
				referrals++;
			}
			temp = *targetUNCs;
			temp[name_len] = 0;
		}

	}
GetDFSRefExit:
	if (pSMB)
		cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto getDFSRetry;

	return rc;
}

/* Query File System Info such as free space to old servers such as Win 9x */
int
SMBOldQFSInfo(const int xid, struct cifsTconInfo *tcon, struct kstatfs *FSData)
{
/* level 0x01 SMB_QUERY_FILE_SYSTEM_INFO */
	TRANSACTION2_QFSI_REQ *pSMB = NULL;
	TRANSACTION2_QFSI_RSP *pSMBr = NULL;
	FILE_SYSTEM_ALLOC_INFO *response_data;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, byte_count;

	cFYI(1, ("OldQFSInfo"));
oldQFSInfoRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		(void **) &pSMBr);
	if (rc)
		return rc;
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	params = 2;     /* level */
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(1000);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	byte_count = params + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(params);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(offsetof(
	struct smb_com_transaction2_qfsi_req, InformationLevel) - 4);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_FS_INFORMATION);
	pSMB->InformationLevel = cpu_to_le16(SMB_INFO_ALLOCATION);
	pSMB->hdr.smb_buf_length += byte_count;
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
		(struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("Send error in QFSInfo = %d", rc));
	} else {                /* decode response */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);

		if (rc || (pSMBr->ByteCount < 18))
			rc = -EIO;      /* bad smb */
		else {
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);
			cFYI(1,("qfsinf resp BCC: %d  Offset %d",
				 pSMBr->ByteCount, data_offset));

			response_data =
				(FILE_SYSTEM_ALLOC_INFO *) 
				(((char *) &pSMBr->hdr.Protocol) + data_offset);
			FSData->f_bsize =
				le16_to_cpu(response_data->BytesPerSector) *
				le32_to_cpu(response_data->
					SectorsPerAllocationUnit);
			FSData->f_blocks =
				le32_to_cpu(response_data->TotalAllocationUnits);
			FSData->f_bfree = FSData->f_bavail =
				le32_to_cpu(response_data->FreeAllocationUnits);
			cFYI(1,
			     ("Blocks: %lld  Free: %lld Block size %ld",
			      (unsigned long long)FSData->f_blocks,
			      (unsigned long long)FSData->f_bfree,
			      FSData->f_bsize));
		}
	}
	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto oldQFSInfoRetry;

	return rc;
}

int
CIFSSMBQFSInfo(const int xid, struct cifsTconInfo *tcon, struct kstatfs *FSData)
{
/* level 0x103 SMB_QUERY_FILE_SYSTEM_INFO */
	TRANSACTION2_QFSI_REQ *pSMB = NULL;
	TRANSACTION2_QFSI_RSP *pSMBr = NULL;
	FILE_SYSTEM_INFO *response_data;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, byte_count;

	cFYI(1, ("In QFSInfo"));
QFSInfoRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	params = 2;	/* level */
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(1000);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	byte_count = params + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(params);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(offsetof(
        struct smb_com_transaction2_qfsi_req, InformationLevel) - 4);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_FS_INFORMATION);
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_FS_SIZE_INFO);
	pSMB->hdr.smb_buf_length += byte_count;
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("Send error in QFSInfo = %d", rc));
	} else {		/* decode response */
                rc = validate_t2((struct smb_t2_rsp *)pSMBr);

		if (rc || (pSMBr->ByteCount < 24))
			rc = -EIO;	/* bad smb */
		else {
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);

			response_data =
			    (FILE_SYSTEM_INFO
			     *) (((char *) &pSMBr->hdr.Protocol) +
				 data_offset);
			FSData->f_bsize =
			    le32_to_cpu(response_data->BytesPerSector) *
			    le32_to_cpu(response_data->
					SectorsPerAllocationUnit);
			FSData->f_blocks =
			    le64_to_cpu(response_data->TotalAllocationUnits);
			FSData->f_bfree = FSData->f_bavail =
			    le64_to_cpu(response_data->FreeAllocationUnits);
			cFYI(1,
			     ("Blocks: %lld  Free: %lld Block size %ld",
			      (unsigned long long)FSData->f_blocks,
			      (unsigned long long)FSData->f_bfree,
			      FSData->f_bsize));
		}
	}
	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto QFSInfoRetry;

	return rc;
}

int
CIFSSMBQFSAttributeInfo(const int xid, struct cifsTconInfo *tcon)
{
/* level 0x105  SMB_QUERY_FILE_SYSTEM_INFO */
	TRANSACTION2_QFSI_REQ *pSMB = NULL;
	TRANSACTION2_QFSI_RSP *pSMBr = NULL;
	FILE_SYSTEM_ATTRIBUTE_INFO *response_data;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, byte_count;

	cFYI(1, ("In QFSAttributeInfo"));
QFSAttributeRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	params = 2;	/* level */
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(1000);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	byte_count = params + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(params);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(offsetof(
        struct smb_com_transaction2_qfsi_req, InformationLevel) - 4);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_FS_INFORMATION);
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_FS_ATTRIBUTE_INFO);
	pSMB->hdr.smb_buf_length += byte_count;
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cERROR(1, ("Send error in QFSAttributeInfo = %d", rc));
	} else {		/* decode response */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);

		if (rc || (pSMBr->ByteCount < 13)) {	/* BB also check enough bytes returned */
			rc = -EIO;	/* bad smb */
		} else {
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);
			response_data =
			    (FILE_SYSTEM_ATTRIBUTE_INFO
			     *) (((char *) &pSMBr->hdr.Protocol) +
				 data_offset);
			memcpy(&tcon->fsAttrInfo, response_data,
			       sizeof (FILE_SYSTEM_ATTRIBUTE_INFO));
		}
	}
	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto QFSAttributeRetry;

	return rc;
}

int
CIFSSMBQFSDeviceInfo(const int xid, struct cifsTconInfo *tcon)
{
/* level 0x104 SMB_QUERY_FILE_SYSTEM_INFO */
	TRANSACTION2_QFSI_REQ *pSMB = NULL;
	TRANSACTION2_QFSI_RSP *pSMBr = NULL;
	FILE_SYSTEM_DEVICE_INFO *response_data;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, byte_count;

	cFYI(1, ("In QFSDeviceInfo"));
QFSDeviceRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	params = 2;	/* level */
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(1000);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	byte_count = params + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(params);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(offsetof(
        struct smb_com_transaction2_qfsi_req, InformationLevel) - 4);

	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_FS_INFORMATION);
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_FS_DEVICE_INFO);
	pSMB->hdr.smb_buf_length += byte_count;
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("Send error in QFSDeviceInfo = %d", rc));
	} else {		/* decode response */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);

		if (rc || (pSMBr->ByteCount < sizeof (FILE_SYSTEM_DEVICE_INFO)))
			rc = -EIO;	/* bad smb */
		else {
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);
			response_data =
			    (FILE_SYSTEM_DEVICE_INFO *)
				(((char *) &pSMBr->hdr.Protocol) +
				 data_offset);
			memcpy(&tcon->fsDevInfo, response_data,
			       sizeof (FILE_SYSTEM_DEVICE_INFO));
		}
	}
	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto QFSDeviceRetry;

	return rc;
}

int
CIFSSMBQFSUnixInfo(const int xid, struct cifsTconInfo *tcon)
{
/* level 0x200  SMB_QUERY_CIFS_UNIX_INFO */
	TRANSACTION2_QFSI_REQ *pSMB = NULL;
	TRANSACTION2_QFSI_RSP *pSMBr = NULL;
	FILE_SYSTEM_UNIX_INFO *response_data;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, byte_count;

	cFYI(1, ("In QFSUnixInfo"));
QFSUnixRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	params = 2;	/* level */
	pSMB->TotalDataCount = 0;
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(100);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	byte_count = params + 1 /* pad */ ;
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(offsetof(struct 
        smb_com_transaction2_qfsi_req, InformationLevel) - 4);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_FS_INFORMATION);
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_CIFS_UNIX_INFO);
	pSMB->hdr.smb_buf_length += byte_count;
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cERROR(1, ("Send error in QFSUnixInfo = %d", rc));
	} else {		/* decode response */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);

		if (rc || (pSMBr->ByteCount < 13)) {
			rc = -EIO;	/* bad smb */
		} else {
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);
			response_data =
			    (FILE_SYSTEM_UNIX_INFO
			     *) (((char *) &pSMBr->hdr.Protocol) +
				 data_offset);
			memcpy(&tcon->fsUnixInfo, response_data,
			       sizeof (FILE_SYSTEM_UNIX_INFO));
		}
	}
	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto QFSUnixRetry;


	return rc;
}

int
CIFSSMBSetFSUnixInfo(const int xid, struct cifsTconInfo *tcon, __u64 cap)
{
/* level 0x200  SMB_SET_CIFS_UNIX_INFO */
	TRANSACTION2_SETFSI_REQ *pSMB = NULL;
	TRANSACTION2_SETFSI_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, param_offset, offset, byte_count;

	cFYI(1, ("In SETFSUnixInfo"));
SETFSUnixRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	params = 4;	/* 2 bytes zero followed by info level. */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_setfsi_req, FileNum) - 4;
	offset = param_offset + params;

	pSMB->MaxParameterCount = cpu_to_le16(4);
	pSMB->MaxDataCount = cpu_to_le16(100);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_FS_INFORMATION);
	byte_count = 1 /* pad */ + params + 12;

	pSMB->DataCount = cpu_to_le16(12);
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(param_offset);
	pSMB->DataOffset = cpu_to_le16(offset);

	/* Params. */
	pSMB->FileNum = 0;
	pSMB->InformationLevel = cpu_to_le16(SMB_SET_CIFS_UNIX_INFO);

	/* Data. */
	pSMB->ClientUnixMajor = cpu_to_le16(CIFS_UNIX_MAJOR_VERSION);
	pSMB->ClientUnixMinor = cpu_to_le16(CIFS_UNIX_MINOR_VERSION);
	pSMB->ClientUnixCap = cpu_to_le64(cap);

	pSMB->hdr.smb_buf_length += byte_count;
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cERROR(1, ("Send error in SETFSUnixInfo = %d", rc));
	} else {		/* decode response */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);
		if (rc) {
			rc = -EIO;	/* bad smb */
		}
	}
	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto SETFSUnixRetry;

	return rc;
}



int
CIFSSMBQFSPosixInfo(const int xid, struct cifsTconInfo *tcon,
		   struct kstatfs *FSData)
{
/* level 0x201  SMB_QUERY_CIFS_POSIX_INFO */
	TRANSACTION2_QFSI_REQ *pSMB = NULL;
	TRANSACTION2_QFSI_RSP *pSMBr = NULL;
	FILE_SYSTEM_POSIX_INFO *response_data;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, byte_count;

	cFYI(1, ("In QFSPosixInfo"));
QFSPosixRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	params = 2;	/* level */
	pSMB->TotalDataCount = 0;
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(100);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	byte_count = params + 1 /* pad */ ;
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(offsetof(struct 
        smb_com_transaction2_qfsi_req, InformationLevel) - 4);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_FS_INFORMATION);
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_POSIX_FS_INFO);
	pSMB->hdr.smb_buf_length += byte_count;
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("Send error in QFSUnixInfo = %d", rc));
	} else {		/* decode response */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);

		if (rc || (pSMBr->ByteCount < 13)) {
			rc = -EIO;	/* bad smb */
		} else {
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);
			response_data =
			    (FILE_SYSTEM_POSIX_INFO
			     *) (((char *) &pSMBr->hdr.Protocol) +
				 data_offset);
			FSData->f_bsize =
					le32_to_cpu(response_data->BlockSize);
			FSData->f_blocks =
					le64_to_cpu(response_data->TotalBlocks);
			FSData->f_bfree =
			    le64_to_cpu(response_data->BlocksAvail);
			if(response_data->UserBlocksAvail == cpu_to_le64(-1)) {
				FSData->f_bavail = FSData->f_bfree;
			} else {
				FSData->f_bavail =
					le64_to_cpu(response_data->UserBlocksAvail);
			}
			if(response_data->TotalFileNodes != cpu_to_le64(-1))
				FSData->f_files =
					le64_to_cpu(response_data->TotalFileNodes);
			if(response_data->FreeFileNodes != cpu_to_le64(-1))
				FSData->f_ffree =
					le64_to_cpu(response_data->FreeFileNodes);
		}
	}
	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto QFSPosixRetry;

	return rc;
}


/* We can not use write of zero bytes trick to 
   set file size due to need for large file support.  Also note that 
   this SetPathInfo is preferred to SetFileInfo based method in next 
   routine which is only needed to work around a sharing violation bug
   in Samba which this routine can run into */

int
CIFSSMBSetEOF(const int xid, struct cifsTconInfo *tcon, const char *fileName,
	      __u64 size, int SetAllocation, 
	      const struct nls_table *nls_codepage, int remap)
{
	struct smb_com_transaction2_spi_req *pSMB = NULL;
	struct smb_com_transaction2_spi_rsp *pSMBr = NULL;
	struct file_end_of_file_info *parm_data;
	int name_len;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, byte_count, data_count, param_offset, offset;

	cFYI(1, ("In SetEOF"));
SetEOFRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifsConvertToUCS((__le16 *) pSMB->FileName, fileName,
				     PATH_MAX, nls_codepage, remap);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {	/* BB improve the check for buffer overruns BB */
		name_len = strnlen(fileName, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, fileName, name_len);
	}
	params = 6 + name_len;
	data_count = sizeof (struct file_end_of_file_info);
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(4100);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_spi_req,
                                     InformationLevel) - 4;
	offset = param_offset + params;
	if(SetAllocation) {
        	if (tcon->ses->capabilities & CAP_INFOLEVEL_PASSTHRU)
	            pSMB->InformationLevel =
                	cpu_to_le16(SMB_SET_FILE_ALLOCATION_INFO2);
        	else
	            pSMB->InformationLevel =
        	        cpu_to_le16(SMB_SET_FILE_ALLOCATION_INFO);
	} else /* Set File Size */  {    
	    if (tcon->ses->capabilities & CAP_INFOLEVEL_PASSTHRU)
		    pSMB->InformationLevel =
		        cpu_to_le16(SMB_SET_FILE_END_OF_FILE_INFO2);
	    else
		    pSMB->InformationLevel =
		        cpu_to_le16(SMB_SET_FILE_END_OF_FILE_INFO);
	}

	parm_data =
	    (struct file_end_of_file_info *) (((char *) &pSMB->hdr.Protocol) +
				       offset);
	pSMB->ParameterOffset = cpu_to_le16(param_offset);
	pSMB->DataOffset = cpu_to_le16(offset);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_PATH_INFORMATION);
	byte_count = 3 /* pad */  + params + data_count;
	pSMB->DataCount = cpu_to_le16(data_count);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->Reserved4 = 0;
	pSMB->hdr.smb_buf_length += byte_count;
	parm_data->FileSize = cpu_to_le64(size);
	pSMB->ByteCount = cpu_to_le16(byte_count);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("SetPathInfo (file size) returned %d", rc));
	}

	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto SetEOFRetry;

	return rc;
}

int
CIFSSMBSetFileSize(const int xid, struct cifsTconInfo *tcon, __u64 size, 
                   __u16 fid, __u32 pid_of_opener, int SetAllocation)
{
	struct smb_com_transaction2_sfi_req *pSMB  = NULL;
	struct smb_com_transaction2_sfi_rsp *pSMBr = NULL;
	char *data_offset;
	struct file_end_of_file_info *parm_data;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, param_offset, offset, byte_count, count;

	cFYI(1, ("SetFileSize (via SetFileInfo) %lld",
			(long long)size));
	rc = small_smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB);

	if (rc)
		return rc;

	pSMBr = (struct smb_com_transaction2_sfi_rsp *)pSMB;

	pSMB->hdr.Pid = cpu_to_le16((__u16)pid_of_opener);
	pSMB->hdr.PidHigh = cpu_to_le16((__u16)(pid_of_opener >> 16));
    
	params = 6;
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_sfi_req, Fid) - 4;
	offset = param_offset + params;

	data_offset = (char *) (&pSMB->hdr.Protocol) + offset;	

	count = sizeof(struct file_end_of_file_info);
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(1000);	/* BB find max SMB PDU from sess */
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_FILE_INFORMATION);
	byte_count = 3 /* pad */  + params + count;
	pSMB->DataCount = cpu_to_le16(count);
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(param_offset);
	parm_data =
		(struct file_end_of_file_info *) (((char *) &pSMB->hdr.Protocol) +
			offset);
	pSMB->DataOffset = cpu_to_le16(offset);
	parm_data->FileSize = cpu_to_le64(size);
	pSMB->Fid = fid;
	if(SetAllocation) {
		if (tcon->ses->capabilities & CAP_INFOLEVEL_PASSTHRU)
			pSMB->InformationLevel =
				cpu_to_le16(SMB_SET_FILE_ALLOCATION_INFO2);
		else
			pSMB->InformationLevel =
				cpu_to_le16(SMB_SET_FILE_ALLOCATION_INFO);
	} else /* Set File Size */  {    
	    if (tcon->ses->capabilities & CAP_INFOLEVEL_PASSTHRU)
		    pSMB->InformationLevel =
		        cpu_to_le16(SMB_SET_FILE_END_OF_FILE_INFO2);
	    else
		    pSMB->InformationLevel =
		        cpu_to_le16(SMB_SET_FILE_END_OF_FILE_INFO);
	}
	pSMB->Reserved4 = 0;
	pSMB->hdr.smb_buf_length += byte_count;
	pSMB->ByteCount = cpu_to_le16(byte_count);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1,
		     ("Send error in SetFileInfo (SetFileSize) = %d",
		      rc));
	}

	if (pSMB)
		cifs_small_buf_release(pSMB);

	/* Note: On -EAGAIN error only caller can retry on handle based calls 
		since file handle passed in no longer valid */

	return rc;
}

/* Some legacy servers such as NT4 require that the file times be set on 
   an open handle, rather than by pathname - this is awkward due to
   potential access conflicts on the open, but it is unavoidable for these
   old servers since the only other choice is to go from 100 nanosecond DCE
   time and resort to the original setpathinfo level which takes the ancient
   DOS time format with 2 second granularity */
int
CIFSSMBSetFileTimes(const int xid, struct cifsTconInfo *tcon, const FILE_BASIC_INFO * data, 
                   __u16 fid)
{
	struct smb_com_transaction2_sfi_req *pSMB  = NULL;
	struct smb_com_transaction2_sfi_rsp *pSMBr = NULL;
	char *data_offset;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, param_offset, offset, byte_count, count;

	cFYI(1, ("Set Times (via SetFileInfo)"));
	rc = small_smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB);

	if (rc)
		return rc;

	pSMBr = (struct smb_com_transaction2_sfi_rsp *)pSMB;

	/* At this point there is no need to override the current pid
	with the pid of the opener, but that could change if we someday
	use an existing handle (rather than opening one on the fly) */
	/* pSMB->hdr.Pid = cpu_to_le16((__u16)pid_of_opener);
	pSMB->hdr.PidHigh = cpu_to_le16((__u16)(pid_of_opener >> 16));*/
    
	params = 6;
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_sfi_req, Fid) - 4;
	offset = param_offset + params;

	data_offset = (char *) (&pSMB->hdr.Protocol) + offset; 

	count = sizeof (FILE_BASIC_INFO);
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(1000);	/* BB find max SMB PDU from sess */
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_FILE_INFORMATION);
	byte_count = 3 /* pad */  + params + count;
	pSMB->DataCount = cpu_to_le16(count);
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(param_offset);
	pSMB->DataOffset = cpu_to_le16(offset);
	pSMB->Fid = fid;
	if (tcon->ses->capabilities & CAP_INFOLEVEL_PASSTHRU)
		pSMB->InformationLevel = cpu_to_le16(SMB_SET_FILE_BASIC_INFO2);
	else
		pSMB->InformationLevel = cpu_to_le16(SMB_SET_FILE_BASIC_INFO);
	pSMB->Reserved4 = 0;
	pSMB->hdr.smb_buf_length += byte_count;
	pSMB->ByteCount = cpu_to_le16(byte_count);
	memcpy(data_offset,data,sizeof(FILE_BASIC_INFO));
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1,("Send error in Set Time (SetFileInfo) = %d",rc));
	}

	cifs_small_buf_release(pSMB);

	/* Note: On -EAGAIN error only caller can retry on handle based calls 
		since file handle passed in no longer valid */

	return rc;
}


int
CIFSSMBSetTimes(const int xid, struct cifsTconInfo *tcon, const char *fileName,
		const FILE_BASIC_INFO * data, 
		const struct nls_table *nls_codepage, int remap)
{
	TRANSACTION2_SPI_REQ *pSMB = NULL;
	TRANSACTION2_SPI_RSP *pSMBr = NULL;
	int name_len;
	int rc = 0;
	int bytes_returned = 0;
	char *data_offset;
	__u16 params, param_offset, offset, byte_count, count;

	cFYI(1, ("In SetTimes"));

SetTimesRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifsConvertToUCS((__le16 *) pSMB->FileName, fileName,
				     PATH_MAX, nls_codepage, remap);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {		/* BB improve the check for buffer overruns BB */
		name_len = strnlen(fileName, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, fileName, name_len);
	}

	params = 6 + name_len;
	count = sizeof (FILE_BASIC_INFO);
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(1000);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_spi_req,
                                     InformationLevel) - 4;
	offset = param_offset + params;
	data_offset = (char *) (&pSMB->hdr.Protocol) + offset;
	pSMB->ParameterOffset = cpu_to_le16(param_offset);
	pSMB->DataOffset = cpu_to_le16(offset);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_PATH_INFORMATION);
	byte_count = 3 /* pad */  + params + count;

	pSMB->DataCount = cpu_to_le16(count);
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	if (tcon->ses->capabilities & CAP_INFOLEVEL_PASSTHRU)
		pSMB->InformationLevel = cpu_to_le16(SMB_SET_FILE_BASIC_INFO2);
	else
		pSMB->InformationLevel = cpu_to_le16(SMB_SET_FILE_BASIC_INFO);
	pSMB->Reserved4 = 0;
	pSMB->hdr.smb_buf_length += byte_count;
	memcpy(data_offset, data, sizeof (FILE_BASIC_INFO));
	pSMB->ByteCount = cpu_to_le16(byte_count);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("SetPathInfo (times) returned %d", rc));
	}

	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto SetTimesRetry;

	return rc;
}

/* Can not be used to set time stamps yet (due to old DOS time format) */
/* Can be used to set attributes */
#if 0  /* Possibly not needed - since it turns out that strangely NT4 has a bug
	  handling it anyway and NT4 was what we thought it would be needed for
	  Do not delete it until we prove whether needed for Win9x though */
int
CIFSSMBSetAttrLegacy(int xid, struct cifsTconInfo *tcon, char *fileName,
		__u16 dos_attrs, const struct nls_table *nls_codepage)
{
	SETATTR_REQ *pSMB = NULL;
	SETATTR_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned;
	int name_len;

	cFYI(1, ("In SetAttrLegacy"));

SetAttrLgcyRetry:
	rc = smb_init(SMB_COM_SETATTR, 8, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
			ConvertToUCS((__le16 *) pSMB->fileName, fileName, 
				PATH_MAX, nls_codepage);
		name_len++;     /* trailing null */
		name_len *= 2;
	} else {                /* BB improve the check for buffer overruns BB */
		name_len = strnlen(fileName, PATH_MAX);
		name_len++;     /* trailing null */
		strncpy(pSMB->fileName, fileName, name_len);
	}
	pSMB->attr = cpu_to_le16(dos_attrs);
	pSMB->BufferFormat = 0x04;
	pSMB->hdr.smb_buf_length += name_len + 1;
	pSMB->ByteCount = cpu_to_le16(name_len + 1);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("Error in LegacySetAttr = %d", rc));
	}

	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto SetAttrLgcyRetry;

	return rc;
}
#endif /* temporarily unneeded SetAttr legacy function */

int
CIFSSMBUnixSetPerms(const int xid, struct cifsTconInfo *tcon,
		    char *fileName, __u64 mode, __u64 uid, __u64 gid, 
		    dev_t device, const struct nls_table *nls_codepage, 
		    int remap)
{
	TRANSACTION2_SPI_REQ *pSMB = NULL;
	TRANSACTION2_SPI_RSP *pSMBr = NULL;
	int name_len;
	int rc = 0;
	int bytes_returned = 0;
	FILE_UNIX_BASIC_INFO *data_offset;
	__u16 params, param_offset, offset, count, byte_count;

	cFYI(1, ("In SetUID/GID/Mode"));
setPermsRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifsConvertToUCS((__le16 *) pSMB->FileName, fileName, 
				     PATH_MAX, nls_codepage, remap);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {	/* BB improve the check for buffer overruns BB */
		name_len = strnlen(fileName, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, fileName, name_len);
	}

	params = 6 + name_len;
	count = sizeof (FILE_UNIX_BASIC_INFO);
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(1000);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_spi_req,
                                     InformationLevel) - 4;
	offset = param_offset + params;
	data_offset =
	    (FILE_UNIX_BASIC_INFO *) ((char *) &pSMB->hdr.Protocol +
				      offset);
	memset(data_offset, 0, count);
	pSMB->DataOffset = cpu_to_le16(offset);
	pSMB->ParameterOffset = cpu_to_le16(param_offset);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_PATH_INFORMATION);
	byte_count = 3 /* pad */  + params + count;
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->DataCount = cpu_to_le16(count);
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->InformationLevel = cpu_to_le16(SMB_SET_FILE_UNIX_BASIC);
	pSMB->Reserved4 = 0;
	pSMB->hdr.smb_buf_length += byte_count;
	data_offset->Uid = cpu_to_le64(uid);
	data_offset->Gid = cpu_to_le64(gid);
	/* better to leave device as zero when it is  */
	data_offset->DevMajor = cpu_to_le64(MAJOR(device));
	data_offset->DevMinor = cpu_to_le64(MINOR(device));
	data_offset->Permissions = cpu_to_le64(mode);
    
	if(S_ISREG(mode))
		data_offset->Type = cpu_to_le32(UNIX_FILE);
	else if(S_ISDIR(mode))
		data_offset->Type = cpu_to_le32(UNIX_DIR);
	else if(S_ISLNK(mode))
		data_offset->Type = cpu_to_le32(UNIX_SYMLINK);
	else if(S_ISCHR(mode))
		data_offset->Type = cpu_to_le32(UNIX_CHARDEV);
	else if(S_ISBLK(mode))
		data_offset->Type = cpu_to_le32(UNIX_BLOCKDEV);
	else if(S_ISFIFO(mode))
		data_offset->Type = cpu_to_le32(UNIX_FIFO);
	else if(S_ISSOCK(mode))
		data_offset->Type = cpu_to_le32(UNIX_SOCKET);


	pSMB->ByteCount = cpu_to_le16(byte_count);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("SetPathInfo (perms) returned %d", rc));
	}

	if (pSMB)
		cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto setPermsRetry;
	return rc;
}

int CIFSSMBNotify(const int xid, struct cifsTconInfo *tcon, 
		  const int notify_subdirs, const __u16 netfid,
		  __u32 filter, struct file * pfile, int multishot, 
		  const struct nls_table *nls_codepage)
{
	int rc = 0;
	struct smb_com_transaction_change_notify_req * pSMB = NULL;
	struct smb_com_transaction_change_notify_rsp * pSMBr = NULL;
	struct dir_notify_req *dnotify_req;
	int bytes_returned;

	cFYI(1, ("In CIFSSMBNotify for file handle %d",(int)netfid));
	rc = smb_init(SMB_COM_NT_TRANSACT, 23, tcon, (void **) &pSMB,
                      (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->TotalParameterCount = 0 ;
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le32(2);
	/* BB find exact data count max from sess structure BB */
	pSMB->MaxDataCount = 0; /* same in little endian or be */
	pSMB->MaxSetupCount = 4;
	pSMB->Reserved = 0;
	pSMB->ParameterOffset = 0;
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 4; /* single byte does not need le conversion */
	pSMB->SubCommand = cpu_to_le16(NT_TRANSACT_NOTIFY_CHANGE);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	if(notify_subdirs)
		pSMB->WatchTree = 1; /* one byte - no le conversion needed */
	pSMB->Reserved2 = 0;
	pSMB->CompletionFilter = cpu_to_le32(filter);
	pSMB->Fid = netfid; /* file handle always le */
	pSMB->ByteCount = 0;

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			(struct smb_hdr *) pSMBr, &bytes_returned, -1);
	if (rc) {
		cFYI(1, ("Error in Notify = %d", rc));
	} else {
		/* Add file to outstanding requests */
		/* BB change to kmem cache alloc */	
		dnotify_req = (struct dir_notify_req *) kmalloc(
						sizeof(struct dir_notify_req),
						 GFP_KERNEL);
		if(dnotify_req) {
			dnotify_req->Pid = pSMB->hdr.Pid;
			dnotify_req->PidHigh = pSMB->hdr.PidHigh;
			dnotify_req->Mid = pSMB->hdr.Mid;
			dnotify_req->Tid = pSMB->hdr.Tid;
			dnotify_req->Uid = pSMB->hdr.Uid;
			dnotify_req->netfid = netfid;
			dnotify_req->pfile = pfile;
			dnotify_req->filter = filter;
			dnotify_req->multishot = multishot;
			spin_lock(&GlobalMid_Lock);
			list_add_tail(&dnotify_req->lhead, 
					&GlobalDnotifyReqList);
			spin_unlock(&GlobalMid_Lock);
		} else 
			rc = -ENOMEM;
	}
	cifs_buf_release(pSMB);
	return rc;	
}
#ifdef CONFIG_CIFS_XATTR
ssize_t
CIFSSMBQAllEAs(const int xid, struct cifsTconInfo *tcon,
		 const unsigned char *searchName,
		 char * EAData, size_t buf_size,
		 const struct nls_table *nls_codepage, int remap)
{
		/* BB assumes one setup word */
	TRANSACTION2_QPI_REQ *pSMB = NULL;
	TRANSACTION2_QPI_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned;
	int name_len;
	struct fea * temp_fea;
	char * temp_ptr;
	__u16 params, byte_count;

	cFYI(1, ("In Query All EAs path %s", searchName));
QAllEAsRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifsConvertToUCS((__le16 *) pSMB->FileName, searchName, 
				     PATH_MAX, nls_codepage, remap);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {	/* BB improve the check for buffer overruns BB */
		name_len = strnlen(searchName, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, searchName, name_len);
	}

	params = 2 /* level */ + 4 /* reserved */ + name_len /* includes NUL */ ;
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(4000);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset = cpu_to_le16(offsetof(
        struct smb_com_transaction2_qpi_req ,InformationLevel) - 4);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_PATH_INFORMATION);
	byte_count = params + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(params);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->InformationLevel = cpu_to_le16(SMB_INFO_QUERY_ALL_EAS);
	pSMB->Reserved4 = 0;
	pSMB->hdr.smb_buf_length += byte_count;
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("Send error in QueryAllEAs = %d", rc));
	} else {		/* decode response */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);

		/* BB also check enough total bytes returned */
		/* BB we need to improve the validity checking
		of these trans2 responses */
		if (rc || (pSMBr->ByteCount < 4)) 
			rc = -EIO;	/* bad smb */
	   /* else if (pFindData){
			memcpy((char *) pFindData,
			       (char *) &pSMBr->hdr.Protocol +
			       data_offset, kl);
		}*/ else {
			/* check that length of list is not more than bcc */
			/* check that each entry does not go beyond length
			   of list */
			/* check that each element of each entry does not
			   go beyond end of list */
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);
			struct fealist * ea_response_data;
			rc = 0;
			/* validate_trans2_offsets() */
			/* BB to check if(start of smb + data_offset > &bcc+ bcc)*/
			ea_response_data = (struct fealist *)
				(((char *) &pSMBr->hdr.Protocol) +
				data_offset);
			name_len = le32_to_cpu(ea_response_data->list_len);
			cFYI(1,("ea length %d", name_len));
			if(name_len <= 8) {
			/* returned EA size zeroed at top of function */
				cFYI(1,("empty EA list returned from server"));
			} else {
				/* account for ea list len */
				name_len -= 4;
				temp_fea = ea_response_data->list;
				temp_ptr = (char *)temp_fea;
				while(name_len > 0) {
					__u16 value_len;
					name_len -= 4;
					temp_ptr += 4;
					rc += temp_fea->name_len;
				/* account for prefix user. and trailing null */
					rc = rc + 5 + 1; 
					if(rc<(int)buf_size) {
						memcpy(EAData,"user.",5);
						EAData+=5;
						memcpy(EAData,temp_ptr,temp_fea->name_len);
						EAData+=temp_fea->name_len;
						/* null terminate name */
						*EAData = 0;
						EAData = EAData + 1;
					} else if(buf_size == 0) {
						/* skip copy - calc size only */
					} else {
						/* stop before overrun buffer */
						rc = -ERANGE;
						break;
					}
					name_len -= temp_fea->name_len;
					temp_ptr += temp_fea->name_len;
					/* account for trailing null */
					name_len--;
					temp_ptr++;
					value_len = le16_to_cpu(temp_fea->value_len);
					name_len -= value_len;
					temp_ptr += value_len;
					/* BB check that temp_ptr is still within smb BB*/
				/* no trailing null to account for in value len */
					/* go on to next EA */
					temp_fea = (struct fea *)temp_ptr;
				}
			}
		}
	}
	if (pSMB)
		cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto QAllEAsRetry;

	return (ssize_t)rc;
}

ssize_t CIFSSMBQueryEA(const int xid,struct cifsTconInfo * tcon,
		const unsigned char * searchName,const unsigned char * ea_name,
		unsigned char * ea_value, size_t buf_size, 
		const struct nls_table *nls_codepage, int remap)
{
	TRANSACTION2_QPI_REQ *pSMB = NULL;
	TRANSACTION2_QPI_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned;
	int name_len;
	struct fea * temp_fea;
	char * temp_ptr;
	__u16 params, byte_count;

	cFYI(1, ("In Query EA path %s", searchName));
QEARetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifsConvertToUCS((__le16 *) pSMB->FileName, searchName, 
				     PATH_MAX, nls_codepage, remap);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {	/* BB improve the check for buffer overruns BB */
		name_len = strnlen(searchName, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, searchName, name_len);
	}

	params = 2 /* level */ + 4 /* reserved */ + name_len /* includes NUL */ ;
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(4000);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset = cpu_to_le16(offsetof(
        struct smb_com_transaction2_qpi_req ,InformationLevel) - 4);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_PATH_INFORMATION);
	byte_count = params + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(params);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->InformationLevel = cpu_to_le16(SMB_INFO_QUERY_ALL_EAS);
	pSMB->Reserved4 = 0;
	pSMB->hdr.smb_buf_length += byte_count;
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("Send error in Query EA = %d", rc));
	} else {		/* decode response */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);

		/* BB also check enough total bytes returned */
		/* BB we need to improve the validity checking
		of these trans2 responses */
		if (rc || (pSMBr->ByteCount < 4)) 
			rc = -EIO;	/* bad smb */
	   /* else if (pFindData){
			memcpy((char *) pFindData,
			       (char *) &pSMBr->hdr.Protocol +
			       data_offset, kl);
		}*/ else {
			/* check that length of list is not more than bcc */
			/* check that each entry does not go beyond length
			   of list */
			/* check that each element of each entry does not
			   go beyond end of list */
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);
			struct fealist * ea_response_data;
			rc = -ENODATA;
			/* validate_trans2_offsets() */
			/* BB to check if(start of smb + data_offset > &bcc+ bcc)*/
			ea_response_data = (struct fealist *)
				(((char *) &pSMBr->hdr.Protocol) +
				data_offset);
			name_len = le32_to_cpu(ea_response_data->list_len);
			cFYI(1,("ea length %d", name_len));
			if(name_len <= 8) {
			/* returned EA size zeroed at top of function */
				cFYI(1,("empty EA list returned from server"));
			} else {
				/* account for ea list len */
				name_len -= 4;
				temp_fea = ea_response_data->list;
				temp_ptr = (char *)temp_fea;
				/* loop through checking if we have a matching
				name and then return the associated value */
				while(name_len > 0) {
					__u16 value_len;
					name_len -= 4;
					temp_ptr += 4;
					value_len = le16_to_cpu(temp_fea->value_len);
				/* BB validate that value_len falls within SMB, 
				even though maximum for name_len is 255 */ 
					if(memcmp(temp_fea->name,ea_name,
						  temp_fea->name_len) == 0) {
						/* found a match */
						rc = value_len;
				/* account for prefix user. and trailing null */
						if(rc<=(int)buf_size) {
							memcpy(ea_value,
								temp_fea->name+temp_fea->name_len+1,
								rc);
							/* ea values, unlike ea names,
							are not null terminated */
						} else if(buf_size == 0) {
						/* skip copy - calc size only */
						} else {
							/* stop before overrun buffer */
							rc = -ERANGE;
						}
						break;
					}
					name_len -= temp_fea->name_len;
					temp_ptr += temp_fea->name_len;
					/* account for trailing null */
					name_len--;
					temp_ptr++;
					name_len -= value_len;
					temp_ptr += value_len;
				/* no trailing null to account for in value len */
					/* go on to next EA */
					temp_fea = (struct fea *)temp_ptr;
				}
			} 
		}
	}
	if (pSMB)
		cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto QEARetry;

	return (ssize_t)rc;
}

int
CIFSSMBSetEA(const int xid, struct cifsTconInfo *tcon, const char *fileName,
		const char * ea_name, const void * ea_value, 
		const __u16 ea_value_len, const struct nls_table *nls_codepage,
		int remap)
{
	struct smb_com_transaction2_spi_req *pSMB = NULL;
	struct smb_com_transaction2_spi_rsp *pSMBr = NULL;
	struct fealist *parm_data;
	int name_len;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, param_offset, byte_count, offset, count;

	cFYI(1, ("In SetEA"));
SetEARetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifsConvertToUCS((__le16 *) pSMB->FileName, fileName, 
				     PATH_MAX, nls_codepage, remap);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {		/* BB improve the check for buffer overruns BB */
		name_len = strnlen(fileName, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, fileName, name_len);
	}

	params = 6 + name_len;

	/* done calculating parms using name_len of file name,
	now use name_len to calculate length of ea name
	we are going to create in the inode xattrs */
	if(ea_name == NULL)
		name_len = 0;
	else
		name_len = strnlen(ea_name,255);

	count = sizeof(*parm_data) + ea_value_len + name_len + 1;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(1000);	/* BB find max SMB size from sess */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_spi_req,
                                     InformationLevel) - 4;
	offset = param_offset + params;
	pSMB->InformationLevel =
		cpu_to_le16(SMB_SET_FILE_EA);

	parm_data =
		(struct fealist *) (((char *) &pSMB->hdr.Protocol) +
				       offset);
	pSMB->ParameterOffset = cpu_to_le16(param_offset);
	pSMB->DataOffset = cpu_to_le16(offset);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_PATH_INFORMATION);
	byte_count = 3 /* pad */  + params + count;
	pSMB->DataCount = cpu_to_le16(count);
	parm_data->list_len = cpu_to_le32(count);
	parm_data->list[0].EA_flags = 0;
	/* we checked above that name len is less than 255 */
	parm_data->list[0].name_len = (__u8)name_len;;
	/* EA names are always ASCII */
	if(ea_name)
		strncpy(parm_data->list[0].name,ea_name,name_len);
	parm_data->list[0].name[name_len] = 0;
	parm_data->list[0].value_len = cpu_to_le16(ea_value_len);
	/* caller ensures that ea_value_len is less than 64K but
	we need to ensure that it fits within the smb */

	/*BB add length check that it would fit in negotiated SMB buffer size BB */
	/* if(ea_value_len > buffer_size - 512 (enough for header)) */
	if(ea_value_len)
		memcpy(parm_data->list[0].name+name_len+1,ea_value,ea_value_len);

	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->Reserved4 = 0;
	pSMB->hdr.smb_buf_length += byte_count;
	pSMB->ByteCount = cpu_to_le16(byte_count);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("SetPathInfo (EA) returned %d", rc));
	}

	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto SetEARetry;

	return rc;
}

#endif
