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
extern unsigned int _get_xid(void);
extern void _free_xid(unsigned int);
#define get_xid()						\
({								\
	unsigned int __xid = _get_xid();				\
	cFYI(1, "CIFS VFS: in %s as Xid: %u with uid: %d",	\
	     __func__, __xid, current_fsuid());			\
	__xid;							\
})

#define free_xid(curr_xid)					\
do {								\
	_free_xid(curr_xid);					\
	cFYI(1, "CIFS VFS: leaving %s (xid = %u) rc = %d",	\
	     __func__, curr_xid, (int)rc);			\
} while (0)
extern int init_cifs_idmap(void);
extern void exit_cifs_idmap(void);
extern void cifs_destroy_idmaptrees(void);
extern char *build_path_from_dentry(struct dentry *);
extern char *build_wildcard_path_from_dentry(struct dentry *direntry);
extern char *cifs_compose_mount_options(const char *sb_mountdata,
		const char *fullpath, const struct dfs_info3_param *ref,
		char **devname);
/* extern void renew_parental_timestamps(struct dentry *direntry);*/
extern struct mid_q_entry *AllocMidQEntry(const struct smb_hdr *smb_buffer,
					struct TCP_Server_Info *server);
extern void DeleteMidQEntry(struct mid_q_entry *midEntry);
extern void cifs_wake_up_task(struct mid_q_entry *mid);
extern int cifs_call_async(struct TCP_Server_Info *server, struct kvec *iov,
			   unsigned int nvec, mid_receive_t *receive,
			   mid_callback_t *callback, void *cbdata,
			   const int flags);
extern int SendReceive(const unsigned int /* xid */ , struct cifs_ses *,
			struct smb_hdr * /* input */ ,
			struct smb_hdr * /* out */ ,
			int * /* bytes returned */ , const int);
extern int SendReceiveNoRsp(const unsigned int xid, struct cifs_ses *ses,
			    char *in_buf, int flags);
extern int cifs_setup_request(struct cifs_ses *, struct kvec *, unsigned int,
			      struct mid_q_entry **);
extern int cifs_setup_async_request(struct TCP_Server_Info *, struct kvec *,
				    unsigned int, struct mid_q_entry **);
extern int cifs_check_receive(struct mid_q_entry *mid,
			struct TCP_Server_Info *server, bool log_error);
extern int SendReceive2(const unsigned int /* xid */ , struct cifs_ses *,
			struct kvec *, int /* nvec to send */,
			int * /* type of buf returned */ , const int flags);
extern int SendReceiveBlockingLock(const unsigned int xid,
			struct cifs_tcon *ptcon,
			struct smb_hdr *in_buf ,
			struct smb_hdr *out_buf,
			int *bytes_returned);
extern int cifs_reconnect(struct TCP_Server_Info *server);
extern int checkSMB(char *buf, unsigned int length);
extern bool is_valid_oplock_break(char *, struct TCP_Server_Info *);
extern bool backup_cred(struct cifs_sb_info *);
extern bool is_size_safe_to_change(struct cifsInodeInfo *, __u64 eof);
extern void cifs_update_eof(struct cifsInodeInfo *cifsi, loff_t offset,
			    unsigned int bytes_written);
extern struct cifsFileInfo *find_writable_file(struct cifsInodeInfo *, bool);
extern struct cifsFileInfo *find_readable_file(struct cifsInodeInfo *, bool);
extern unsigned int smbCalcSize(struct smb_hdr *ptr);
extern int decode_negTokenInit(unsigned char *security_blob, int length,
			struct TCP_Server_Info *server);
extern int cifs_convert_address(struct sockaddr *dst, const char *src, int len);
extern int cifs_set_port(struct sockaddr *addr, const unsigned short int port);
extern int cifs_fill_sockaddr(struct sockaddr *dst, const char *src, int len,
				const unsigned short int port);
extern int map_smb_to_linux_error(char *buf, bool logErr);
extern void header_assemble(struct smb_hdr *, char /* command */ ,
			    const struct cifs_tcon *, int /* length of
			    fixed section (word count) in two byte units */);
extern int small_smb_init_no_tc(const int smb_cmd, const int wct,
				struct cifs_ses *ses,
				void **request_buf);
extern int CIFS_SessSetup(const unsigned int xid, struct cifs_ses *ses,
			  const struct nls_table *nls_cp);
extern struct timespec cifs_NTtimeToUnix(__le64 utc_nanoseconds_since_1601);
extern u64 cifs_UnixTimeToNT(struct timespec);
extern struct timespec cnvrtDosUnixTm(__le16 le_date, __le16 le_time,
				      int offset);
extern void cifs_set_oplock_level(struct cifsInodeInfo *cinode, __u32 oplock);

extern struct cifsFileInfo *cifs_new_fileinfo(__u16 fileHandle,
				struct file *file, struct tcon_link *tlink,
				__u32 oplock);
extern int cifs_posix_open(char *full_path, struct inode **inode,
			   struct super_block *sb, int mode,
			   unsigned int f_flags, __u32 *oplock, __u16 *netfid,
			   unsigned int xid);
void cifs_fill_uniqueid(struct super_block *sb, struct cifs_fattr *fattr);
extern void cifs_unix_basic_to_fattr(struct cifs_fattr *fattr,
				     FILE_UNIX_BASIC_INFO *info,
				     struct cifs_sb_info *cifs_sb);
extern void cifs_fattr_to_inode(struct inode *inode, struct cifs_fattr *fattr);
extern struct inode *cifs_iget(struct super_block *sb,
			       struct cifs_fattr *fattr);

extern int cifs_get_file_info(struct file *filp);
extern int cifs_get_inode_info(struct inode **inode, const char *full_path,
			       FILE_ALL_INFO *data, struct super_block *sb,
			       int xid, const __u16 *fid);
extern int cifs_get_file_info_unix(struct file *filp);
extern int cifs_get_inode_info_unix(struct inode **pinode,
			const unsigned char *search_path,
			struct super_block *sb, unsigned int xid);
extern int cifs_acl_to_fattr(struct cifs_sb_info *cifs_sb,
			      struct cifs_fattr *fattr, struct inode *inode,
			      const char *path, const __u16 *pfid);
extern int id_mode_to_cifs_acl(struct inode *inode, const char *path, __u64,
					uid_t, gid_t);
extern struct cifs_ntsd *get_cifs_acl(struct cifs_sb_info *, struct inode *,
					const char *, u32 *);
extern int set_cifs_acl(struct cifs_ntsd *, __u32, struct inode *,
				const char *, int);

extern void dequeue_mid(struct mid_q_entry *mid, bool malformed);
extern int cifs_read_from_socket(struct TCP_Server_Info *server, char *buf,
		     unsigned int to_read);
extern int cifs_readv_from_socket(struct TCP_Server_Info *server,
		struct kvec *iov_orig, unsigned int nr_segs,
		unsigned int to_read);
extern void cifs_setup_cifs_sb(struct smb_vol *pvolume_info,
			       struct cifs_sb_info *cifs_sb);
extern int cifs_match_super(struct super_block *, void *);
extern void cifs_cleanup_volume_info(struct smb_vol *pvolume_info);
extern struct smb_vol *cifs_get_volume_info(char *mount_data,
					    const char *devname);
extern int cifs_mount(struct cifs_sb_info *, struct smb_vol *);
extern void cifs_umount(struct cifs_sb_info *);
extern void cifs_mark_open_files_invalid(struct cifs_tcon *tcon);

#if IS_ENABLED(CONFIG_CIFS_DFS_UPCALL)
extern void cifs_dfs_release_automount_timer(void);
#else /* ! IS_ENABLED(CONFIG_CIFS_DFS_UPCALL) */
#define cifs_dfs_release_automount_timer()	do { } while (0)
#endif /* ! IS_ENABLED(CONFIG_CIFS_DFS_UPCALL) */

void cifs_proc_init(void);
void cifs_proc_clean(void);

extern int cifs_negotiate_protocol(const unsigned int xid,
				   struct cifs_ses *ses);
extern int cifs_setup_session(const unsigned int xid, struct cifs_ses *ses,
			      struct nls_table *nls_info);
extern int CIFSSMBNegotiate(const unsigned int xid, struct cifs_ses *ses);

extern int CIFSTCon(const unsigned int xid, struct cifs_ses *ses,
		    const char *tree, struct cifs_tcon *tcon,
		    const struct nls_table *);

extern int CIFSFindFirst(const unsigned int xid, struct cifs_tcon *tcon,
		const char *searchName, const struct nls_table *nls_codepage,
		__u16 *searchHandle, __u16 search_flags,
		struct cifs_search_info *psrch_inf,
		int map, const char dirsep);

extern int CIFSFindNext(const unsigned int xid, struct cifs_tcon *tcon,
		__u16 searchHandle, __u16 search_flags,
		struct cifs_search_info *psrch_inf);

extern int CIFSFindClose(const unsigned int xid, struct cifs_tcon *tcon,
			const __u16 search_handle);

extern int CIFSSMBQFileInfo(const unsigned int xid, struct cifs_tcon *tcon,
			u16 netfid, FILE_ALL_INFO *pFindData);
extern int CIFSSMBQPathInfo(const unsigned int xid, struct cifs_tcon *tcon,
			    const char *search_Name, FILE_ALL_INFO *data,
			    int legacy /* whether to use old info level */,
			    const struct nls_table *nls_codepage, int remap);
extern int SMBQueryInformation(const unsigned int xid, struct cifs_tcon *tcon,
			       const char *search_name, FILE_ALL_INFO *data,
			       const struct nls_table *nls_codepage, int remap);

extern int CIFSSMBUnixQFileInfo(const unsigned int xid, struct cifs_tcon *tcon,
			u16 netfid, FILE_UNIX_BASIC_INFO *pFindData);
extern int CIFSSMBUnixQPathInfo(const unsigned int xid,
			struct cifs_tcon *tcon,
			const unsigned char *searchName,
			FILE_UNIX_BASIC_INFO *pFindData,
			const struct nls_table *nls_codepage, int remap);

extern int CIFSGetDFSRefer(const unsigned int xid, struct cifs_ses *ses,
			   const char *search_name,
			   struct dfs_info3_param **target_nodes,
			   unsigned int *num_of_nodes,
			   const struct nls_table *nls_codepage, int remap);

extern int get_dfs_path(const unsigned int xid, struct cifs_ses *ses,
			const char *old_path,
			const struct nls_table *nls_codepage,
			unsigned int *num_referrals,
			struct dfs_info3_param **referrals, int remap);
extern void reset_cifs_unix_caps(unsigned int xid, struct cifs_tcon *tcon,
				 struct cifs_sb_info *cifs_sb,
				 struct smb_vol *vol);
extern int CIFSSMBQFSInfo(const unsigned int xid, struct cifs_tcon *tcon,
			struct kstatfs *FSData);
extern int SMBOldQFSInfo(const unsigned int xid, struct cifs_tcon *tcon,
			struct kstatfs *FSData);
extern int CIFSSMBSetFSUnixInfo(const unsigned int xid, struct cifs_tcon *tcon,
			__u64 cap);

extern int CIFSSMBQFSAttributeInfo(const unsigned int xid,
			struct cifs_tcon *tcon);
extern int CIFSSMBQFSDeviceInfo(const unsigned int xid, struct cifs_tcon *tcon);
extern int CIFSSMBQFSUnixInfo(const unsigned int xid, struct cifs_tcon *tcon);
extern int CIFSSMBQFSPosixInfo(const unsigned int xid, struct cifs_tcon *tcon,
			struct kstatfs *FSData);

extern int CIFSSMBSetPathInfo(const unsigned int xid, struct cifs_tcon *tcon,
			const char *fileName, const FILE_BASIC_INFO *data,
			const struct nls_table *nls_codepage,
			int remap_special_chars);
extern int CIFSSMBSetFileInfo(const unsigned int xid, struct cifs_tcon *tcon,
			const FILE_BASIC_INFO *data, __u16 fid,
			__u32 pid_of_opener);
extern int CIFSSMBSetFileDisposition(const unsigned int xid,
				     struct cifs_tcon *tcon,
				     bool delete_file, __u16 fid,
				     __u32 pid_of_opener);
#if 0
extern int CIFSSMBSetAttrLegacy(unsigned int xid, struct cifs_tcon *tcon,
			char *fileName, __u16 dos_attributes,
			const struct nls_table *nls_codepage);
#endif /* possibly unneeded function */
extern int CIFSSMBSetEOF(const unsigned int xid, struct cifs_tcon *tcon,
			const char *fileName, __u64 size,
			bool setAllocationSizeFlag,
			const struct nls_table *nls_codepage,
			int remap_special_chars);
extern int CIFSSMBSetFileSize(const unsigned int xid, struct cifs_tcon *tcon,
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

extern int CIFSSMBUnixSetFileInfo(const unsigned int xid,
				  struct cifs_tcon *tcon,
				  const struct cifs_unix_set_info_args *args,
				  u16 fid, u32 pid_of_opener);

extern int CIFSSMBUnixSetPathInfo(const unsigned int xid,
				  struct cifs_tcon *tcon, const char *file_name,
				  const struct cifs_unix_set_info_args *args,
				  const struct nls_table *nls_codepage,
				  int remap);

extern int CIFSSMBMkDir(const unsigned int xid, struct cifs_tcon *tcon,
			const char *name, struct cifs_sb_info *cifs_sb);
extern int CIFSSMBRmDir(const unsigned int xid, struct cifs_tcon *tcon,
			const char *name, struct cifs_sb_info *cifs_sb);
extern int CIFSPOSIXDelFile(const unsigned int xid, struct cifs_tcon *tcon,
			const char *name, __u16 type,
			const struct nls_table *nls_codepage,
			int remap_special_chars);
extern int CIFSSMBDelFile(const unsigned int xid, struct cifs_tcon *tcon,
			const char *name,
			const struct nls_table *nls_codepage,
			int remap_special_chars);
extern int CIFSSMBRename(const unsigned int xid, struct cifs_tcon *tcon,
			const char *fromName, const char *toName,
			const struct nls_table *nls_codepage,
			int remap_special_chars);
extern int CIFSSMBRenameOpenFile(const unsigned int xid, struct cifs_tcon *tcon,
				 int netfid, const char *target_name,
				 const struct nls_table *nls_codepage,
				 int remap_special_chars);
extern int CIFSCreateHardLink(const unsigned int xid,
			struct cifs_tcon *tcon,
			const char *fromName, const char *toName,
			const struct nls_table *nls_codepage,
			int remap_special_chars);
extern int CIFSUnixCreateHardLink(const unsigned int xid,
			struct cifs_tcon *tcon,
			const char *fromName, const char *toName,
			const struct nls_table *nls_codepage,
			int remap_special_chars);
extern int CIFSUnixCreateSymLink(const unsigned int xid,
			struct cifs_tcon *tcon,
			const char *fromName, const char *toName,
			const struct nls_table *nls_codepage);
extern int CIFSSMBUnixQuerySymLink(const unsigned int xid,
			struct cifs_tcon *tcon,
			const unsigned char *searchName, char **syminfo,
			const struct nls_table *nls_codepage);
#ifdef CONFIG_CIFS_SYMLINK_EXPERIMENTAL
extern int CIFSSMBQueryReparseLinkInfo(const unsigned int xid,
			struct cifs_tcon *tcon,
			const unsigned char *searchName,
			char *symlinkinfo, const int buflen, __u16 fid,
			const struct nls_table *nls_codepage);
#endif /* temporarily unused until cifs_symlink fixed */
extern int CIFSSMBOpen(const unsigned int xid, struct cifs_tcon *tcon,
			const char *fileName, const int disposition,
			const int access_flags, const int omode,
			__u16 *netfid, int *pOplock, FILE_ALL_INFO *,
			const struct nls_table *nls_codepage, int remap);
extern int SMBLegacyOpen(const unsigned int xid, struct cifs_tcon *tcon,
			const char *fileName, const int disposition,
			const int access_flags, const int omode,
			__u16 *netfid, int *pOplock, FILE_ALL_INFO *,
			const struct nls_table *nls_codepage, int remap);
extern int CIFSPOSIXCreate(const unsigned int xid, struct cifs_tcon *tcon,
			u32 posix_flags, __u64 mode, __u16 *netfid,
			FILE_UNIX_BASIC_INFO *pRetData,
			__u32 *pOplock, const char *name,
			const struct nls_table *nls_codepage, int remap);
extern int CIFSSMBClose(const unsigned int xid, struct cifs_tcon *tcon,
			const int smb_file_id);

extern int CIFSSMBFlush(const unsigned int xid, struct cifs_tcon *tcon,
			const int smb_file_id);

extern int CIFSSMBRead(const unsigned int xid, struct cifs_io_parms *io_parms,
			unsigned int *nbytes, char **buf,
			int *return_buf_type);
extern int CIFSSMBWrite(const unsigned int xid, struct cifs_io_parms *io_parms,
			unsigned int *nbytes, const char *buf,
			const char __user *ubuf, const int long_op);
extern int CIFSSMBWrite2(const unsigned int xid, struct cifs_io_parms *io_parms,
			unsigned int *nbytes, struct kvec *iov, const int nvec,
			const int long_op);
extern int CIFSGetSrvInodeNumber(const unsigned int xid, struct cifs_tcon *tcon,
				 const char *search_name, __u64 *inode_number,
				 const struct nls_table *nls_codepage,
				 int remap);

extern int cifs_lockv(const unsigned int xid, struct cifs_tcon *tcon,
		      const __u16 netfid, const __u8 lock_type,
		      const __u32 num_unlock, const __u32 num_lock,
		      LOCKING_ANDX_RANGE *buf);
extern int CIFSSMBLock(const unsigned int xid, struct cifs_tcon *tcon,
			const __u16 netfid, const __u32 netpid, const __u64 len,
			const __u64 offset, const __u32 numUnlock,
			const __u32 numLock, const __u8 lockType,
			const bool waitFlag, const __u8 oplock_level);
extern int CIFSSMBPosixLock(const unsigned int xid, struct cifs_tcon *tcon,
			const __u16 smb_file_id, const __u32 netpid,
			const loff_t start_offset, const __u64 len,
			struct file_lock *, const __u16 lock_type,
			const bool waitFlag);
extern int CIFSSMBTDis(const unsigned int xid, struct cifs_tcon *tcon);
extern int CIFSSMBEcho(struct TCP_Server_Info *server);
extern int CIFSSMBLogoff(const unsigned int xid, struct cifs_ses *ses);

extern struct cifs_ses *sesInfoAlloc(void);
extern void sesInfoFree(struct cifs_ses *);
extern struct cifs_tcon *tconInfoAlloc(void);
extern void tconInfoFree(struct cifs_tcon *);

extern int cifs_sign_smb(struct smb_hdr *, struct TCP_Server_Info *, __u32 *);
extern int cifs_sign_smbv(struct kvec *iov, int n_vec, struct TCP_Server_Info *,
			  __u32 *);
extern int cifs_verify_signature(struct kvec *iov, unsigned int nr_iov,
				 struct TCP_Server_Info *server,
				__u32 expected_sequence_number);
extern int SMBNTencrypt(unsigned char *, unsigned char *, unsigned char *,
			const struct nls_table *);
extern int setup_ntlm_response(struct cifs_ses *, const struct nls_table *);
extern int setup_ntlmv2_rsp(struct cifs_ses *, const struct nls_table *);
extern int cifs_crypto_shash_allocate(struct TCP_Server_Info *);
extern void cifs_crypto_shash_release(struct TCP_Server_Info *);
extern int calc_seckey(struct cifs_ses *);

#ifdef CONFIG_CIFS_WEAK_PW_HASH
extern int calc_lanman_hash(const char *password, const char *cryptkey,
				bool encrypt, char *lnm_session_key);
#endif /* CIFS_WEAK_PW_HASH */
#ifdef CONFIG_CIFS_DNOTIFY_EXPERIMENTAL /* unused temporarily */
extern int CIFSSMBNotify(const unsigned int xid, struct cifs_tcon *tcon,
			const int notify_subdirs, const __u16 netfid,
			__u32 filter, struct file *file, int multishot,
			const struct nls_table *nls_codepage);
#endif /* was needed for dnotify, and will be needed for inotify when VFS fix */
extern int CIFSSMBCopy(unsigned int xid,
			struct cifs_tcon *source_tcon,
			const char *fromName,
			const __u16 target_tid,
			const char *toName, const int flags,
			const struct nls_table *nls_codepage,
			int remap_special_chars);
extern ssize_t CIFSSMBQAllEAs(const unsigned int xid, struct cifs_tcon *tcon,
			const unsigned char *searchName,
			const unsigned char *ea_name, char *EAData,
			size_t bufsize, const struct nls_table *nls_codepage,
			int remap_special_chars);
extern int CIFSSMBSetEA(const unsigned int xid, struct cifs_tcon *tcon,
		const char *fileName, const char *ea_name,
		const void *ea_value, const __u16 ea_value_len,
		const struct nls_table *nls_codepage, int remap_special_chars);
extern int CIFSSMBGetCIFSACL(const unsigned int xid, struct cifs_tcon *tcon,
			__u16 fid, struct cifs_ntsd **acl_inf, __u32 *buflen);
extern int CIFSSMBSetCIFSACL(const unsigned int, struct cifs_tcon *, __u16,
			struct cifs_ntsd *, __u32, int);
extern int CIFSSMBGetPosixACL(const unsigned int xid, struct cifs_tcon *tcon,
		const unsigned char *searchName,
		char *acl_inf, const int buflen, const int acl_type,
		const struct nls_table *nls_codepage, int remap_special_chars);
extern int CIFSSMBSetPosixACL(const unsigned int xid, struct cifs_tcon *tcon,
		const unsigned char *fileName,
		const char *local_acl, const int buflen, const int acl_type,
		const struct nls_table *nls_codepage, int remap_special_chars);
extern int CIFSGetExtAttr(const unsigned int xid, struct cifs_tcon *tcon,
			const int netfid, __u64 *pExtAttrBits, __u64 *pMask);
extern void cifs_autodisable_serverino(struct cifs_sb_info *cifs_sb);
extern bool CIFSCouldBeMFSymlink(const struct cifs_fattr *fattr);
extern int CIFSCheckMFSymlink(struct cifs_fattr *fattr,
		const unsigned char *path,
		struct cifs_sb_info *cifs_sb, unsigned int xid);
extern int mdfour(unsigned char *, unsigned char *, int);
extern int E_md4hash(const unsigned char *passwd, unsigned char *p16,
			const struct nls_table *codepage);
extern int SMBencrypt(unsigned char *passwd, const unsigned char *c8,
			unsigned char *p24);

/* asynchronous read support */
struct cifs_readdata {
	struct kref			refcount;
	struct list_head		list;
	struct completion		done;
	struct cifsFileInfo		*cfile;
	struct address_space		*mapping;
	__u64				offset;
	unsigned int			bytes;
	pid_t				pid;
	int				result;
	struct list_head		pages;
	struct work_struct		work;
	int (*marshal_iov) (struct cifs_readdata *rdata,
			    unsigned int remaining);
	unsigned int			nr_iov;
	struct kvec			iov[1];
};

void cifs_readdata_release(struct kref *refcount);
int cifs_async_readv(struct cifs_readdata *rdata);

/* asynchronous write support */
struct cifs_writedata {
	struct kref			refcount;
	struct list_head		list;
	struct completion		done;
	enum writeback_sync_modes	sync_mode;
	struct work_struct		work;
	struct cifsFileInfo		*cfile;
	__u64				offset;
	pid_t				pid;
	unsigned int			bytes;
	int				result;
	void (*marshal_iov) (struct kvec *iov,
			     struct cifs_writedata *wdata);
	unsigned int			nr_pages;
	struct page			*pages[1];
};

int cifs_async_writev(struct cifs_writedata *wdata);
void cifs_writev_complete(struct work_struct *work);
struct cifs_writedata *cifs_writedata_alloc(unsigned int nr_pages,
						work_func_t complete);
void cifs_writedata_release(struct kref *refcount);

#endif			/* _CIFSPROTO_H */
