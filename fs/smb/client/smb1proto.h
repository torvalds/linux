/* SPDX-License-Identifier: LGPL-2.1 */
/*
 *
 *   Copyright (c) International Business Machines  Corp., 2002,2008
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 */
#ifndef _SMB1PROTO_H
#define _SMB1PROTO_H

#include <linux/uidgid_types.h>
#include <linux/unaligned.h>
#include "../common/smb2pdu.h"
#include "cifsglob.h"

struct cifs_unix_set_info_args {
	__u64	ctime;
	__u64	atime;
	__u64	mtime;
	__u64	mode;
	kuid_t	uid;
	kgid_t	gid;
	dev_t	device;
};

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY

/*
 * cifssmb.c
 */
int small_smb_init_no_tc(const int smb_command, const int wct,
			 struct cifs_ses *ses, void **request_buf);
int CIFSSMBNegotiate(const unsigned int xid, struct cifs_ses *ses,
		     struct TCP_Server_Info *server);
int CIFSTCon(const unsigned int xid, struct cifs_ses *ses, const char *tree,
	     struct cifs_tcon *tcon, const struct nls_table *nls_codepage);
int CIFSSMBTDis(const unsigned int xid, struct cifs_tcon *tcon);
int CIFSSMBEcho(struct TCP_Server_Info *server);
int CIFSSMBLogoff(const unsigned int xid, struct cifs_ses *ses);
int CIFSPOSIXDelFile(const unsigned int xid, struct cifs_tcon *tcon,
		     const char *fileName, __u16 type,
		     const struct nls_table *nls_codepage, int remap);
int CIFSSMBDelFile(const unsigned int xid, struct cifs_tcon *tcon,
		   const char *name, struct cifs_sb_info *cifs_sb,
		   struct dentry *dentry);
int CIFSSMBRmDir(const unsigned int xid, struct cifs_tcon *tcon,
		 const char *name, struct cifs_sb_info *cifs_sb);
int CIFSSMBMkDir(const unsigned int xid, struct inode *inode, umode_t mode,
		 struct cifs_tcon *tcon, const char *name,
		 struct cifs_sb_info *cifs_sb);
int CIFSPOSIXCreate(const unsigned int xid, struct cifs_tcon *tcon,
		    __u32 posix_flags, __u64 mode, __u16 *netfid,
		    FILE_UNIX_BASIC_INFO *pRetData, __u32 *pOplock,
		    const char *name, const struct nls_table *nls_codepage,
		    int remap);
int SMBLegacyOpen(const unsigned int xid, struct cifs_tcon *tcon,
		  const char *fileName, const int openDisposition,
		  const int access_flags, const int create_options,
		  __u16 *netfid, int *pOplock, FILE_ALL_INFO *pfile_info,
		  const struct nls_table *nls_codepage, int remap);
int CIFS_open(const unsigned int xid, struct cifs_open_parms *oparms,
	      int *oplock, FILE_ALL_INFO *buf);
int cifs_async_readv(struct cifs_io_subrequest *rdata);
int CIFSSMBRead(const unsigned int xid, struct cifs_io_parms *io_parms,
		unsigned int *nbytes, char **buf, int *pbuf_type);
int CIFSSMBWrite(const unsigned int xid, struct cifs_io_parms *io_parms,
		 unsigned int *nbytes, const char *buf);
void cifs_async_writev(struct cifs_io_subrequest *wdata);
int CIFSSMBWrite2(const unsigned int xid, struct cifs_io_parms *io_parms,
		  unsigned int *nbytes, struct kvec *iov, int n_vec);
int cifs_lockv(const unsigned int xid, struct cifs_tcon *tcon,
	       const __u16 netfid, const __u8 lock_type,
	       const __u32 num_unlock, const __u32 num_lock,
	       LOCKING_ANDX_RANGE *buf);
int CIFSSMBLock(const unsigned int xid, struct cifs_tcon *tcon,
		const __u16 smb_file_id, const __u32 netpid, const __u64 len,
		const __u64 offset, const __u32 numUnlock, const __u32 numLock,
		const __u8 lockType, const bool waitFlag,
		const __u8 oplock_level);
int CIFSSMBPosixLock(const unsigned int xid, struct cifs_tcon *tcon,
		     const __u16 smb_file_id, const __u32 netpid,
		     const loff_t start_offset, const __u64 len,
		     struct file_lock *pLockData, const __u16 lock_type,
		     const bool waitFlag);
int CIFSSMBClose(const unsigned int xid, struct cifs_tcon *tcon,
		 int smb_file_id);
int CIFSSMBFlush(const unsigned int xid, struct cifs_tcon *tcon,
		 int smb_file_id);
int CIFSSMBRename(const unsigned int xid, struct cifs_tcon *tcon,
		  struct dentry *source_dentry, const char *from_name,
		  const char *to_name, struct cifs_sb_info *cifs_sb);
int CIFSSMBRenameOpenFile(const unsigned int xid, struct cifs_tcon *pTcon,
			  int netfid, const char *target_name,
			  const struct nls_table *nls_codepage, int remap);
int CIFSUnixCreateSymLink(const unsigned int xid, struct cifs_tcon *tcon,
			  const char *fromName, const char *toName,
			  const struct nls_table *nls_codepage, int remap);
int CIFSUnixCreateHardLink(const unsigned int xid, struct cifs_tcon *tcon,
			   const char *fromName, const char *toName,
			   const struct nls_table *nls_codepage, int remap);
int CIFSCreateHardLink(const unsigned int xid, struct cifs_tcon *tcon,
		       struct dentry *source_dentry, const char *from_name,
		       const char *to_name, struct cifs_sb_info *cifs_sb);
int CIFSSMBUnixQuerySymLink(const unsigned int xid, struct cifs_tcon *tcon,
			    const unsigned char *searchName,
			    char **symlinkinfo,
			    const struct nls_table *nls_codepage, int remap);
int cifs_query_reparse_point(const unsigned int xid, struct cifs_tcon *tcon,
			     struct cifs_sb_info *cifs_sb,
			     const char *full_path, u32 *tag, struct kvec *rsp,
			     int *rsp_buftype);
struct inode *cifs_create_reparse_inode(struct cifs_open_info_data *data,
					struct super_block *sb,
					const unsigned int xid,
					struct cifs_tcon *tcon,
					const char *full_path, bool directory,
					struct kvec *reparse_iov,
					struct kvec *xattr_iov);
int CIFSSMB_set_compression(const unsigned int xid, struct cifs_tcon *tcon,
			    __u16 fid);
int cifs_do_get_acl(const unsigned int xid, struct cifs_tcon *tcon,
		    const unsigned char *searchName, struct posix_acl **acl,
		    const int acl_type, const struct nls_table *nls_codepage,
		    int remap);
int cifs_do_set_acl(const unsigned int xid, struct cifs_tcon *tcon,
		    const unsigned char *fileName, const struct posix_acl *acl,
		    const int acl_type, const struct nls_table *nls_codepage,
		    int remap);
int CIFSGetExtAttr(const unsigned int xid, struct cifs_tcon *tcon,
		   const int netfid, __u64 *pExtAttrBits, __u64 *pMask);
int CIFSSMBGetCIFSACL(const unsigned int xid, struct cifs_tcon *tcon,
		      __u16 fid, struct smb_ntsd **acl_inf, __u32 *pbuflen,
		      __u32 info);
int CIFSSMBSetCIFSACL(const unsigned int xid, struct cifs_tcon *tcon,
		      __u16 fid, struct smb_ntsd *pntsd, __u32 acllen,
		      int aclflag);
int SMBQueryInformation(const unsigned int xid, struct cifs_tcon *tcon,
			const char *search_name, FILE_ALL_INFO *data,
			const struct nls_table *nls_codepage, int remap);
int CIFSSMBQFileInfo(const unsigned int xid, struct cifs_tcon *tcon,
		     u16 netfid, FILE_ALL_INFO *pFindData);
int CIFSSMBQPathInfo(const unsigned int xid, struct cifs_tcon *tcon,
		     const char *search_name, FILE_ALL_INFO *data,
		     int legacy /* old style infolevel */,
		     const struct nls_table *nls_codepage, int remap);
int CIFSSMBUnixQFileInfo(const unsigned int xid, struct cifs_tcon *tcon,
			 u16 netfid, FILE_UNIX_BASIC_INFO *pFindData);
int CIFSSMBUnixQPathInfo(const unsigned int xid, struct cifs_tcon *tcon,
			 const unsigned char *searchName,
			 FILE_UNIX_BASIC_INFO *pFindData,
			 const struct nls_table *nls_codepage, int remap);
int CIFSFindFirst(const unsigned int xid, struct cifs_tcon *tcon,
		  const char *searchName, struct cifs_sb_info *cifs_sb,
		  __u16 *pnetfid, __u16 search_flags,
		  struct cifs_search_info *psrch_inf, bool msearch);
int CIFSFindNext(const unsigned int xid, struct cifs_tcon *tcon,
		 __u16 searchHandle, __u16 search_flags,
		 struct cifs_search_info *psrch_inf);
int CIFSFindClose(const unsigned int xid, struct cifs_tcon *tcon,
		  const __u16 searchHandle);
int CIFSGetSrvInodeNumber(const unsigned int xid, struct cifs_tcon *tcon,
			  const char *search_name, __u64 *inode_number,
			  const struct nls_table *nls_codepage, int remap);
int CIFSGetDFSRefer(const unsigned int xid, struct cifs_ses *ses,
		    const char *search_name,
		    struct dfs_info3_param **target_nodes,
		    unsigned int *num_of_nodes,
		    const struct nls_table *nls_codepage, int remap);
int SMBOldQFSInfo(const unsigned int xid, struct cifs_tcon *tcon,
		  struct kstatfs *FSData);
int CIFSSMBQFSInfo(const unsigned int xid, struct cifs_tcon *tcon,
		   struct kstatfs *FSData);
int CIFSSMBQFSAttributeInfo(const unsigned int xid, struct cifs_tcon *tcon);
int CIFSSMBQFSDeviceInfo(const unsigned int xid, struct cifs_tcon *tcon);
int CIFSSMBQFSUnixInfo(const unsigned int xid, struct cifs_tcon *tcon);
int CIFSSMBSetFSUnixInfo(const unsigned int xid, struct cifs_tcon *tcon,
			 __u64 cap);
int CIFSSMBQFSPosixInfo(const unsigned int xid, struct cifs_tcon *tcon,
			struct kstatfs *FSData);
int CIFSSMBSetEOF(const unsigned int xid, struct cifs_tcon *tcon,
		  const char *file_name, __u64 size,
		  struct cifs_sb_info *cifs_sb, bool set_allocation,
		  struct dentry *dentry);
int CIFSSMBSetFileSize(const unsigned int xid, struct cifs_tcon *tcon,
		       struct cifsFileInfo *cfile, __u64 size,
		       bool set_allocation);
int SMBSetInformation(const unsigned int xid, struct cifs_tcon *tcon,
		      const char *fileName, __le32 attributes,
		      __le64 write_time, const struct nls_table *nls_codepage,
		      struct cifs_sb_info *cifs_sb);
int CIFSSMBSetFileInfo(const unsigned int xid, struct cifs_tcon *tcon,
		       const FILE_BASIC_INFO *data, __u16 fid,
		       __u32 pid_of_opener);
int CIFSSMBSetFileDisposition(const unsigned int xid, struct cifs_tcon *tcon,
			      bool delete_file, __u16 fid,
			      __u32 pid_of_opener);
int CIFSSMBSetPathInfo(const unsigned int xid, struct cifs_tcon *tcon,
		       const char *fileName, const FILE_BASIC_INFO *data,
		       const struct nls_table *nls_codepage,
		       struct cifs_sb_info *cifs_sb);
int CIFSSMBUnixSetFileInfo(const unsigned int xid, struct cifs_tcon *tcon,
			   const struct cifs_unix_set_info_args *args, u16 fid,
			   u32 pid_of_opener);
int CIFSSMBUnixSetPathInfo(const unsigned int xid, struct cifs_tcon *tcon,
			   const char *file_name,
			   const struct cifs_unix_set_info_args *args,
			   const struct nls_table *nls_codepage, int remap);
ssize_t CIFSSMBQAllEAs(const unsigned int xid, struct cifs_tcon *tcon,
		       const unsigned char *searchName,
		       const unsigned char *ea_name, char *EAData,
		       size_t buf_size, struct cifs_sb_info *cifs_sb);
int CIFSSMBSetEA(const unsigned int xid, struct cifs_tcon *tcon,
		 const char *fileName, const char *ea_name,
		 const void *ea_value, const __u16 ea_value_len,
		 const struct nls_table *nls_codepage,
		 struct cifs_sb_info *cifs_sb);

/*
 * smb1debug.c
 */
void cifs_dump_detail(void *buf, size_t buf_len,
		      struct TCP_Server_Info *server);

/*
 * smb1encrypt.c
 */
int cifs_sign_rqst(struct smb_rqst *rqst, struct TCP_Server_Info *server,
		   __u32 *pexpected_response_sequence_number);
int cifs_verify_signature(struct smb_rqst *rqst,
			  struct TCP_Server_Info *server,
			  __u32 expected_sequence_number);

/*
 * smb1maperror.c
 */
int map_smb_to_linux_error(char *buf, bool logErr);
int map_and_check_smb_error(struct TCP_Server_Info *server,
			    struct mid_q_entry *mid, bool logErr);

/*
 * smb1misc.c
 */
unsigned int header_assemble(struct smb_hdr *buffer, char smb_command,
			     const struct cifs_tcon *treeCon, int word_count);
bool is_valid_oplock_break(char *buffer, struct TCP_Server_Info *srv);
unsigned int smbCalcSize(void *buf);

/*
 * smb1ops.c
 */
extern struct smb_version_operations smb1_operations;
extern struct smb_version_values smb1_values;

void reset_cifs_unix_caps(unsigned int xid, struct cifs_tcon *tcon,
			  struct cifs_sb_info *cifs_sb,
			  struct smb3_fs_context *ctx);

/*
 * smb1session.c
 */
int CIFS_SessSetup(const unsigned int xid, struct cifs_ses *ses,
		   struct TCP_Server_Info *server,
		   const struct nls_table *nls_cp);

/*
 * smb1transport.c
 */
struct mid_q_entry *cifs_setup_async_request(struct TCP_Server_Info *server,
					     struct smb_rqst *rqst);
int SendReceiveNoRsp(const unsigned int xid, struct cifs_ses *ses,
		     char *in_buf, unsigned int in_len, int flags);
int cifs_check_receive(struct mid_q_entry *mid, struct TCP_Server_Info *server,
		       bool log_error);
struct mid_q_entry *cifs_setup_request(struct cifs_ses *ses,
				       struct TCP_Server_Info *server,
				       struct smb_rqst *rqst);
int SendReceive2(const unsigned int xid, struct cifs_ses *ses,
		 struct kvec *iov, int n_vec, int *resp_buf_type /* ret */,
		 const int flags, struct kvec *resp_iov);
int SendReceive(const unsigned int xid, struct cifs_ses *ses,
		struct smb_hdr *in_buf, unsigned int in_len,
		struct smb_hdr *out_buf, int *pbytes_returned,
		const int flags);
bool cifs_check_trans2(struct mid_q_entry *mid, struct TCP_Server_Info *server,
		       char *buf, int malformed);
int checkSMB(char *buf, unsigned int pdu_len, unsigned int total_read,
	     struct TCP_Server_Info *server);


static inline __u16
get_mid(const struct smb_hdr *smb)
{
	return le16_to_cpu(smb->Mid);
}

static inline bool
compare_mid(__u16 mid, const struct smb_hdr *smb)
{
	return mid == le16_to_cpu(smb->Mid);
}

#define GETU16(var)  (*((__u16 *)var))	/* BB check for endian issues */
#define GETU32(var)  (*((__u32 *)var))	/* BB check for endian issues */

/* given a pointer to an smb_hdr, retrieve a void pointer to the ByteCount */
static inline void *
BCC(struct smb_hdr *smb)
{
	return (void *)smb + sizeof(*smb) + 2 * smb->WordCount;
}

/* given a pointer to an smb_hdr retrieve the pointer to the byte area */
#define pByteArea(smb_var) (BCC(smb_var) + 2)

/* get the unconverted ByteCount for a SMB packet and return it */
static inline __u16
get_bcc(struct smb_hdr *hdr)
{
	__le16 *bc_ptr = (__le16 *)BCC(hdr);

	return get_unaligned_le16(bc_ptr);
}

/* set the ByteCount for a SMB packet in little-endian */
static inline void
put_bcc(__u16 count, struct smb_hdr *hdr)
{
	__le16 *bc_ptr = (__le16 *)BCC(hdr);

	put_unaligned_le16(count, bc_ptr);
}

#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */

#endif /* _SMB1PROTO_H */
