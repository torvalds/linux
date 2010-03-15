/*
 *   fs/cifs/cifsproto.h
 *
 *   Copyright (c) International Business Machines  Corp., 2002,2008
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
#ifndef _CIFSPROTO_H
#define _CIFSPROTO_H
#include <linux/nls.h>

struct statfs;
struct smb_vol;

/*
 *****************************************************************
 * All Prototypes
 *****************************************************************
 */

extern struct smb_hdr *cifs_buf_get(void);
extern void cifs_buf_release(void *);
extern struct smb_hdr *cifs_small_buf_get(void);
extern void cifs_small_buf_release(void *);
extern int smb_send(struct TCP_Server_Info *, struct smb_hdr *,
			unsigned int /* length */);
extern unsigned int _GetXid(void);
extern void _FreeXid(unsigned int);
#define GetXid() (int)_GetXid(); cFYI(1,("CIFS VFS: in %s as Xid: %d with uid: %d",__func__, xid,current_fsuid()));
#define FreeXid(curr_xid) {_FreeXid(curr_xid); cFYI(1,("CIFS VFS: leaving %s (xid = %d) rc = %d",__func__,curr_xid,(int)rc));}
extern char *build_path_from_dentry(struct dentry *);
extern char *cifs_build_path_to_root(struct cifs_sb_info *cifs_sb);
extern char *build_wildcard_path_from_dentry(struct dentry *direntry);
extern char *cifs_compose_mount_options(const char *sb_mountdata,
		const char *fullpath, const struct dfs_info3_param *ref,
		char **devname);
/* extern void renew_parental_timestamps(struct dentry *direntry);*/
extern int SendReceive(const unsigned int /* xid */ , struct cifsSesInfo *,
			struct smb_hdr * /* input */ ,
			struct smb_hdr * /* out */ ,
			int * /* bytes returned */ , const int long_op);
extern int SendReceiveNoRsp(const unsigned int xid, struct cifsSesInfo *ses,
			struct smb_hdr *in_buf, int flags);
extern int SendReceive2(const unsigned int /* xid */ , struct cifsSesInfo *,
			struct kvec *, int /* nvec to send */,
			int * /* type of buf returned */ , const int flags);
extern int SendReceiveBlockingLock(const unsigned int xid,
			struct cifsTconInfo *ptcon,
			struct smb_hdr *in_buf ,
			struct smb_hdr *out_buf,
			int *bytes_returned);
extern int checkSMB(struct smb_hdr *smb, __u16 mid, unsigned int length);
extern bool is_valid_oplock_break(struct smb_hdr *smb,
				  struct TCP_Server_Info *);
extern bool is_size_safe_to_change(struct cifsInodeInfo *, __u64 eof);
extern struct cifsFileInfo *find_writable_file(struct cifsInodeInfo *);
#ifdef CONFIG_CIFS_EXPERIMENTAL
extern struct cifsFileInfo *find_readable_file(struct cifsInodeInfo *);
#endif
extern unsigned int smbCalcSize(struct smb_hdr *ptr);
extern unsigned int smbCalcSize_LE(struct smb_hdr *ptr);
extern int decode_negTokenInit(unsigned char *security_blob, int length,
			enum securityEnum *secType);
extern int cifs_convert_address(char *src, void *dst);
extern int map_smb_to_linux_error(struct smb_hdr *smb, int logErr);
extern void header_assemble(struct smb_hdr *, char /* command */ ,
			    const struct cifsTconInfo *, int /* length of
			    fixed section (word count) in two byte units */);
extern int small_smb_init_no_tc(const int smb_cmd, const int wct,
				struct cifsSesInfo *ses,
				void **request_buf);
extern int CIFS_SessSetup(unsigned int xid, struct cifsSesInfo *ses,
			     const int stage,
			     const struct nls_table *nls_cp);
extern __u16 GetNextMid(struct TCP_Server_Info *server);
extern struct timespec cifs_NTtimeToUnix(__le64 utc_nanoseconds_since_1601);
extern u64 cifs_UnixTimeToNT(struct timespec);
extern struct timespec cnvrtDosUnixTm(__le16 le_date, __le16 le_time,
				      int offset);

extern struct cifsFileInfo *cifs_new_fileinfo(struct inode *newinode,
				__u16 fileHandle, struct file *file,
				struct vfsmount *mnt, unsigned int oflags);
extern int cifs_posix_open(char *full_path, struct inode **pinode,
			   struct vfsmount *mnt, int mode, int oflags,
			   __u32 *poplock, __u16 *pnetfid, int xid);
extern void cifs_unix_basic_to_fattr(struct cifs_fattr *fattr,
				     FILE_UNIX_BASIC_INFO *info,
				     struct cifs_sb_info *cifs_sb);
extern void cifs_fattr_to_inode(struct inode *inode, struct cifs_fattr *fattr);
extern struct inode *cifs_iget(struct super_block *sb,
			       struct cifs_fattr *fattr);

extern int cifs_get_inode_info(struct inode **pinode,
			const unsigned char *search_path,
			FILE_ALL_INFO *pfile_info,
			struct super_block *sb, int xid, const __u16 *pfid);
extern int cifs_get_inode_info_unix(struct inode **pinode,
			const unsigned char *search_path,
			struct super_block *sb, int xid);
extern void cifs_acl_to_fattr(struct cifs_sb_info *cifs_sb,
			      struct cifs_fattr *fattr, struct inode *inode,
			      const char *path, const __u16 *pfid);
extern int mode_to_acl(struct inode *inode, const char *path, __u64);

extern int cifs_mount(struct super_block *, struct cifs_sb_info *, char *,
			const char *);
extern int cifs_umount(struct super_block *, struct cifs_sb_info *);
extern void cifs_dfs_release_automount_timer(void);
void cifs_proc_init(void);
void cifs_proc_clean(void);

extern int cifs_setup_session(unsigned int xid, struct cifsSesInfo *pSesInfo,
			struct nls_table *nls_info);
extern int CIFSSMBNegotiate(unsigned int xid, struct cifsSesInfo *ses);

extern int CIFSTCon(unsigned int xid, struct cifsSesInfo *ses,
			const char *tree, struct cifsTconInfo *tcon,
			const struct nls_table *);

extern int CIFSFindFirst(const int xid, struct cifsTconInfo *tcon,
		const char *searchName, const struct nls_table *nls_codepage,
		__u16 *searchHandle, struct cifs_search_info *psrch_inf,
		int map, const char dirsep);

extern int CIFSFindNext(const int xid, struct cifsTconInfo *tcon,
		__u16 searchHandle, struct cifs_search_info *psrch_inf);

extern int CIFSFindClose(const int, struct cifsTconInfo *tcon,
			const __u16 search_handle);

extern int CIFSSMBQPathInfo(const int xid, struct cifsTconInfo *tcon,
			const unsigned char *searchName,
			FILE_ALL_INFO *findData,
			int legacy /* whether to use old info level */,
			const struct nls_table *nls_codepage, int remap);
extern int SMBQueryInformation(const int xid, struct cifsTconInfo *tcon,
			const unsigned char *searchName,
			FILE_ALL_INFO *findData,
			const struct nls_table *nls_codepage, int remap);

extern int CIFSSMBUnixQPathInfo(const int xid,
			struct cifsTconInfo *tcon,
			const unsigned char *searchName,
			FILE_UNIX_BASIC_INFO *pFindData,
			const struct nls_table *nls_codepage, int remap);

extern int CIFSGetDFSRefer(const int xid, struct cifsSesInfo *ses,
			const unsigned char *searchName,
			struct dfs_info3_param **target_nodes,
			unsigned int *number_of_nodes_in_array,
			const struct nls_table *nls_codepage, int remap);

extern int get_dfs_path(int xid, struct cifsSesInfo *pSesInfo,
			const char *old_path,
			const struct nls_table *nls_codepage,
			unsigned int *pnum_referrals,
			struct dfs_info3_param **preferrals,
			int remap);
extern void reset_cifs_unix_caps(int xid, struct cifsTconInfo *tcon,
				 struct super_block *sb, struct smb_vol *vol);
extern int CIFSSMBQFSInfo(const int xid, struct cifsTconInfo *tcon,
			struct kstatfs *FSData);
extern int SMBOldQFSInfo(const int xid, struct cifsTconInfo *tcon,
			struct kstatfs *FSData);
extern int CIFSSMBSetFSUnixInfo(const int xid, struct cifsTconInfo *tcon,
			__u64 cap);

extern int CIFSSMBQFSAttributeInfo(const int xid,
			struct cifsTconInfo *tcon);
extern int CIFSSMBQFSDeviceInfo(const int xid, struct cifsTconInfo *tcon);
extern int CIFSSMBQFSUnixInfo(const int xid, struct cifsTconInfo *tcon);
extern int CIFSSMBQFSPosixInfo(const int xid, struct cifsTconInfo *tcon,
			struct kstatfs *FSData);

extern int CIFSSMBSetPathInfo(const int xid, struct cifsTconInfo *tcon,
			const char *fileName, const FILE_BASIC_INFO *data,
			const struct nls_table *nls_codepage,
			int remap_special_chars);
extern int CIFSSMBSetFileInfo(const int xid, struct cifsTconInfo *tcon,
			const FILE_BASIC_INFO *data, __u16 fid,
			__u32 pid_of_opener);
extern int CIFSSMBSetFileDisposition(const int xid, struct cifsTconInfo *tcon,
			bool delete_file, __u16 fid, __u32 pid_of_opener);
#if 0
extern int CIFSSMBSetAttrLegacy(int xid, struct cifsTconInfo *tcon,
			char *fileName, __u16 dos_attributes,
			const struct nls_table *nls_codepage);
#endif /* possibly unneeded function */
extern int CIFSSMBSetEOF(const int xid, struct cifsTconInfo *tcon,
			const char *fileName, __u64 size,
			bool setAllocationSizeFlag,
			const struct nls_table *nls_codepage,
			int remap_special_chars);
extern int CIFSSMBSetFileSize(const int xid, struct cifsTconInfo *tcon,
			 __u64 size, __u16 fileHandle, __u32 opener_pid,
			bool AllocSizeFlag);

struct cifs_unix_set_info_args {
	__u64	ctime;
	__u64	atime;
	__u64	mtime;
	__u64	mode;
	__u64	uid;
	__u64	gid;
	dev_t	device;
};

extern int CIFSSMBUnixSetFileInfo(const int xid, struct cifsTconInfo *tcon,
				  const struct cifs_unix_set_info_args *args,
				  u16 fid, u32 pid_of_opener);

extern int CIFSSMBUnixSetPathInfo(const int xid, struct cifsTconInfo *pTcon,
			char *fileName,
			const struct cifs_unix_set_info_args *args,
			const struct nls_table *nls_codepage,
			int remap_special_chars);

extern int CIFSSMBMkDir(const int xid, struct cifsTconInfo *tcon,
			const char *newName,
			const struct nls_table *nls_codepage,
			int remap_special_chars);
extern int CIFSSMBRmDir(const int xid, struct cifsTconInfo *tcon,
			const char *name, const struct nls_table *nls_codepage,
			int remap_special_chars);
extern int CIFSPOSIXDelFile(const int xid, struct cifsTconInfo *tcon,
			const char *name, __u16 type,
			const struct nls_table *nls_codepage,
			int remap_special_chars);
extern int CIFSSMBDelFile(const int xid, struct cifsTconInfo *tcon,
			const char *name,
			const struct nls_table *nls_codepage,
			int remap_special_chars);
extern int CIFSSMBRename(const int xid, struct cifsTconInfo *tcon,
			const char *fromName, const char *toName,
			const struct nls_table *nls_codepage,
			int remap_special_chars);
extern int CIFSSMBRenameOpenFile(const int xid, struct cifsTconInfo *pTcon,
			int netfid, const char *target_name,
			const struct nls_table *nls_codepage,
			int remap_special_chars);
extern int CIFSCreateHardLink(const int xid,
			struct cifsTconInfo *tcon,
			const char *fromName, const char *toName,
			const struct nls_table *nls_codepage,
			int remap_special_chars);
extern int CIFSUnixCreateHardLink(const int xid,
			struct cifsTconInfo *tcon,
			const char *fromName, const char *toName,
			const struct nls_table *nls_codepage,
			int remap_special_chars);
extern int CIFSUnixCreateSymLink(const int xid,
			struct cifsTconInfo *tcon,
			const char *fromName, const char *toName,
			const struct nls_table *nls_codepage);
extern int CIFSSMBUnixQuerySymLink(const int xid,
			struct cifsTconInfo *tcon,
			const unsigned char *searchName, char **syminfo,
			const struct nls_table *nls_codepage);
extern int CIFSSMBQueryReparseLinkInfo(const int xid,
			struct cifsTconInfo *tcon,
			const unsigned char *searchName,
			char *symlinkinfo, const int buflen, __u16 fid,
			const struct nls_table *nls_codepage);

extern int CIFSSMBOpen(const int xid, struct cifsTconInfo *tcon,
			const char *fileName, const int disposition,
			const int access_flags, const int omode,
			__u16 *netfid, int *pOplock, FILE_ALL_INFO *,
			const struct nls_table *nls_codepage, int remap);
extern int SMBLegacyOpen(const int xid, struct cifsTconInfo *tcon,
			const char *fileName, const int disposition,
			const int access_flags, const int omode,
			__u16 *netfid, int *pOplock, FILE_ALL_INFO *,
			const struct nls_table *nls_codepage, int remap);
extern int CIFSPOSIXCreate(const int xid, struct cifsTconInfo *tcon,
			u32 posix_flags, __u64 mode, __u16 *netfid,
			FILE_UNIX_BASIC_INFO *pRetData,
			__u32 *pOplock, const char *name,
			const struct nls_table *nls_codepage, int remap);
extern int CIFSSMBClose(const int xid, struct cifsTconInfo *tcon,
			const int smb_file_id);

extern int CIFSSMBFlush(const int xid, struct cifsTconInfo *tcon,
			const int smb_file_id);

extern int CIFSSMBRead(const int xid, struct cifsTconInfo *tcon,
			const int netfid, unsigned int count,
			const __u64 lseek, unsigned int *nbytes, char **buf,
			int *return_buf_type);
extern int CIFSSMBWrite(const int xid, struct cifsTconInfo *tcon,
			const int netfid, const unsigned int count,
			const __u64 lseek, unsigned int *nbytes,
			const char *buf, const char __user *ubuf,
			const int long_op);
extern int CIFSSMBWrite2(const int xid, struct cifsTconInfo *tcon,
			const int netfid, const unsigned int count,
			const __u64 offset, unsigned int *nbytes,
			struct kvec *iov, const int nvec, const int long_op);
extern int CIFSGetSrvInodeNumber(const int xid, struct cifsTconInfo *tcon,
			const unsigned char *searchName, __u64 *inode_number,
			const struct nls_table *nls_codepage,
			int remap_special_chars);
extern int cifsConvertToUCS(__le16 *target, const char *source, int maxlen,
			const struct nls_table *cp, int mapChars);

extern int CIFSSMBLock(const int xid, struct cifsTconInfo *tcon,
			const __u16 netfid, const __u64 len,
			const __u64 offset, const __u32 numUnlock,
			const __u32 numLock, const __u8 lockType,
			const bool waitFlag);
extern int CIFSSMBPosixLock(const int xid, struct cifsTconInfo *tcon,
			const __u16 smb_file_id, const int get_flag,
			const __u64 len, struct file_lock *,
			const __u16 lock_type, const bool waitFlag);
extern int CIFSSMBTDis(const int xid, struct cifsTconInfo *tcon);
extern int CIFSSMBLogoff(const int xid, struct cifsSesInfo *ses);

extern struct cifsSesInfo *sesInfoAlloc(void);
extern void sesInfoFree(struct cifsSesInfo *);
extern struct cifsTconInfo *tconInfoAlloc(void);
extern void tconInfoFree(struct cifsTconInfo *);

extern int cifs_sign_smb(struct smb_hdr *, struct TCP_Server_Info *, __u32 *);
extern int cifs_sign_smb2(struct kvec *iov, int n_vec, struct TCP_Server_Info *,
			  __u32 *);
extern int cifs_verify_signature(struct smb_hdr *,
				 const struct mac_key *mac_key,
				__u32 expected_sequence_number);
extern int cifs_calculate_mac_key(struct mac_key *key, const char *rn,
				 const char *pass);
extern int CalcNTLMv2_partial_mac_key(struct cifsSesInfo *,
			const struct nls_table *);
extern void CalcNTLMv2_response(const struct cifsSesInfo *, char *);
extern void setup_ntlmv2_rsp(struct cifsSesInfo *, char *,
			     const struct nls_table *);
#ifdef CONFIG_CIFS_WEAK_PW_HASH
extern void calc_lanman_hash(const char *password, const char *cryptkey,
				bool encrypt, char *lnm_session_key);
#endif /* CIFS_WEAK_PW_HASH */
extern int CIFSSMBCopy(int xid,
			struct cifsTconInfo *source_tcon,
			const char *fromName,
			const __u16 target_tid,
			const char *toName, const int flags,
			const struct nls_table *nls_codepage,
			int remap_special_chars);
extern int CIFSSMBNotify(const int xid, struct cifsTconInfo *tcon,
			const int notify_subdirs, const __u16 netfid,
			__u32 filter, struct file *file, int multishot,
			const struct nls_table *nls_codepage);
extern ssize_t CIFSSMBQAllEAs(const int xid, struct cifsTconInfo *tcon,
			const unsigned char *searchName,
			const unsigned char *ea_name, char *EAData,
			size_t bufsize, const struct nls_table *nls_codepage,
			int remap_special_chars);
extern int CIFSSMBSetEA(const int xid, struct cifsTconInfo *tcon,
		const char *fileName, const char *ea_name,
		const void *ea_value, const __u16 ea_value_len,
		const struct nls_table *nls_codepage, int remap_special_chars);
extern int CIFSSMBGetCIFSACL(const int xid, struct cifsTconInfo *tcon,
			__u16 fid, struct cifs_ntsd **acl_inf, __u32 *buflen);
extern int CIFSSMBSetCIFSACL(const int, struct cifsTconInfo *, __u16,
			struct cifs_ntsd *, __u32);
extern int CIFSSMBGetPosixACL(const int xid, struct cifsTconInfo *tcon,
		const unsigned char *searchName,
		char *acl_inf, const int buflen, const int acl_type,
		const struct nls_table *nls_codepage, int remap_special_chars);
extern int CIFSSMBSetPosixACL(const int xid, struct cifsTconInfo *tcon,
		const unsigned char *fileName,
		const char *local_acl, const int buflen, const int acl_type,
		const struct nls_table *nls_codepage, int remap_special_chars);
extern int CIFSGetExtAttr(const int xid, struct cifsTconInfo *tcon,
			const int netfid, __u64 *pExtAttrBits, __u64 *pMask);
extern void cifs_autodisable_serverino(struct cifs_sb_info *cifs_sb);
#endif			/* _CIFSPROTO_H */
