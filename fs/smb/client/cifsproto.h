/* SPDX-License-Identifier: LGPL-2.1 */
/*
 *
 *   Copyright (c) International Business Machines  Corp., 2002,2008
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 */
#ifndef _CIFSPROTO_H
#define _CIFSPROTO_H
#include <linux/nls.h>
#include <linux/ctype.h>
#include "trace.h"
#ifdef CONFIG_CIFS_DFS_UPCALL
#include "dfs_cache.h"
#endif

struct statfs;
struct smb_rqst;
struct smb3_fs_context;

/*
 *****************************************************************
 * All Prototypes
 *****************************************************************
 */

extern struct smb_hdr *cifs_buf_get(void);
extern void cifs_buf_release(void *);
extern struct smb_hdr *cifs_small_buf_get(void);
extern void cifs_small_buf_release(void *);
extern void free_rsp_buf(int, void *);
extern int smb_send(struct TCP_Server_Info *, struct smb_hdr *,
			unsigned int /* length */);
extern unsigned int _get_xid(void);
extern void _free_xid(unsigned int);
#define get_xid()							\
({									\
	unsigned int __xid = _get_xid();				\
	cifs_dbg(FYI, "VFS: in %s as Xid: %u with uid: %d\n",		\
		 __func__, __xid,					\
		 from_kuid(&init_user_ns, current_fsuid()));		\
	trace_smb3_enter(__xid, __func__);				\
	__xid;								\
})

#define free_xid(curr_xid)						\
do {									\
	_free_xid(curr_xid);						\
	cifs_dbg(FYI, "VFS: leaving %s (xid = %u) rc = %d\n",		\
		 __func__, curr_xid, (int)rc);				\
	if (rc)								\
		trace_smb3_exit_err(curr_xid, __func__, (int)rc);	\
	else								\
		trace_smb3_exit_done(curr_xid, __func__);		\
} while (0)
extern int init_cifs_idmap(void);
extern void exit_cifs_idmap(void);
extern int init_cifs_spnego(void);
extern void exit_cifs_spnego(void);
extern const char *build_path_from_dentry(struct dentry *, void *);
char *__build_path_from_dentry_optional_prefix(struct dentry *direntry, void *page,
					       const char *tree, int tree_len,
					       bool prefix);
extern char *build_path_from_dentry_optional_prefix(struct dentry *direntry,
						    void *page, bool prefix);
static inline void *alloc_dentry_path(void)
{
	return __getname();
}

static inline void free_dentry_path(void *page)
{
	if (page)
		__putname(page);
}

extern char *cifs_build_path_to_root(struct smb3_fs_context *ctx,
				     struct cifs_sb_info *cifs_sb,
				     struct cifs_tcon *tcon,
				     int add_treename);
extern char *build_wildcard_path_from_dentry(struct dentry *direntry);
char *cifs_build_devname(char *nodename, const char *prepath);
extern void delete_mid(struct mid_q_entry *mid);
void __release_mid(struct kref *refcount);
extern void cifs_wake_up_task(struct mid_q_entry *mid);
extern int cifs_handle_standard(struct TCP_Server_Info *server,
				struct mid_q_entry *mid);
extern char *smb3_fs_context_fullpath(const struct smb3_fs_context *ctx,
				      char dirsep);
extern int smb3_parse_devname(const char *devname, struct smb3_fs_context *ctx);
extern int smb3_parse_opt(const char *options, const char *key, char **val);
extern int cifs_ipaddr_cmp(struct sockaddr *srcaddr, struct sockaddr *rhs);
extern bool cifs_match_ipaddr(struct sockaddr *srcaddr, struct sockaddr *rhs);
extern int cifs_discard_remaining_data(struct TCP_Server_Info *server);
extern int cifs_call_async(struct TCP_Server_Info *server,
			struct smb_rqst *rqst,
			mid_receive_t *receive, mid_callback_t *callback,
			mid_handle_t *handle, void *cbdata, const int flags,
			const struct cifs_credits *exist_credits);
extern struct TCP_Server_Info *cifs_pick_channel(struct cifs_ses *ses);
extern int cifs_send_recv(const unsigned int xid, struct cifs_ses *ses,
			  struct TCP_Server_Info *server,
			  struct smb_rqst *rqst, int *resp_buf_type,
			  const int flags, struct kvec *resp_iov);
extern int compound_send_recv(const unsigned int xid, struct cifs_ses *ses,
			      struct TCP_Server_Info *server,
			      const int flags, const int num_rqst,
			      struct smb_rqst *rqst, int *resp_buf_type,
			      struct kvec *resp_iov);
extern int SendReceive(const unsigned int /* xid */ , struct cifs_ses *,
			struct smb_hdr * /* input */ ,
			struct smb_hdr * /* out */ ,
			int * /* bytes returned */ , const int);
extern int SendReceiveNoRsp(const unsigned int xid, struct cifs_ses *ses,
			    char *in_buf, int flags);
extern struct mid_q_entry *cifs_setup_request(struct cifs_ses *,
				struct TCP_Server_Info *,
				struct smb_rqst *);
extern struct mid_q_entry *cifs_setup_async_request(struct TCP_Server_Info *,
						struct smb_rqst *);
extern int cifs_check_receive(struct mid_q_entry *mid,
			struct TCP_Server_Info *server, bool log_error);
extern int cifs_wait_mtu_credits(struct TCP_Server_Info *server,
				 unsigned int size, unsigned int *num,
				 struct cifs_credits *credits);
extern int SendReceive2(const unsigned int /* xid */ , struct cifs_ses *,
			struct kvec *, int /* nvec to send */,
			int * /* type of buf returned */, const int flags,
			struct kvec * /* resp vec */);
extern int SendReceiveBlockingLock(const unsigned int xid,
			struct cifs_tcon *ptcon,
			struct smb_hdr *in_buf,
			struct smb_hdr *out_buf,
			int *bytes_returned);

void
cifs_signal_cifsd_for_reconnect(struct TCP_Server_Info *server,
				      bool all_channels);
void
cifs_mark_tcp_ses_conns_for_reconnect(struct TCP_Server_Info *server,
				      bool mark_smb_session);
extern int cifs_reconnect(struct TCP_Server_Info *server,
			  bool mark_smb_session);
extern int checkSMB(char *buf, unsigned int len, struct TCP_Server_Info *srvr);
extern bool is_valid_oplock_break(char *, struct TCP_Server_Info *);
extern bool backup_cred(struct cifs_sb_info *);
extern bool is_size_safe_to_change(struct cifsInodeInfo *cifsInode, __u64 eof,
				   bool from_readdir);
extern void cifs_update_eof(struct cifsInodeInfo *cifsi, loff_t offset,
			    unsigned int bytes_written);
extern struct cifsFileInfo *find_writable_file(struct cifsInodeInfo *, int);
extern int cifs_get_writable_file(struct cifsInodeInfo *cifs_inode,
				  int flags,
				  struct cifsFileInfo **ret_file);
extern int cifs_get_writable_path(struct cifs_tcon *tcon, const char *name,
				  int flags,
				  struct cifsFileInfo **ret_file);
extern struct cifsFileInfo *find_readable_file(struct cifsInodeInfo *, bool);
extern int cifs_get_readable_path(struct cifs_tcon *tcon, const char *name,
				  struct cifsFileInfo **ret_file);
extern unsigned int smbCalcSize(void *buf);
extern int decode_negTokenInit(unsigned char *security_blob, int length,
			struct TCP_Server_Info *server);
extern int cifs_convert_address(struct sockaddr *dst, const char *src, int len);
extern void cifs_set_port(struct sockaddr *addr, const unsigned short int port);
extern int map_smb_to_linux_error(char *buf, bool logErr);
extern int map_and_check_smb_error(struct mid_q_entry *mid, bool logErr);
extern void header_assemble(struct smb_hdr *, char /* command */ ,
			    const struct cifs_tcon *, int /* length of
			    fixed section (word count) in two byte units */);
extern int small_smb_init_no_tc(const int smb_cmd, const int wct,
				struct cifs_ses *ses,
				void **request_buf);
extern enum securityEnum select_sectype(struct TCP_Server_Info *server,
				enum securityEnum requested);
extern int CIFS_SessSetup(const unsigned int xid, struct cifs_ses *ses,
			  struct TCP_Server_Info *server,
			  const struct nls_table *nls_cp);
extern struct timespec64 cifs_NTtimeToUnix(__le64 utc_nanoseconds_since_1601);
extern u64 cifs_UnixTimeToNT(struct timespec64);
extern struct timespec64 cnvrtDosUnixTm(__le16 le_date, __le16 le_time,
				      int offset);
extern void cifs_set_oplock_level(struct cifsInodeInfo *cinode, __u32 oplock);
extern int cifs_get_writer(struct cifsInodeInfo *cinode);
extern void cifs_put_writer(struct cifsInodeInfo *cinode);
extern void cifs_done_oplock_break(struct cifsInodeInfo *cinode);
extern int cifs_unlock_range(struct cifsFileInfo *cfile,
			     struct file_lock *flock, const unsigned int xid);
extern int cifs_push_mandatory_locks(struct cifsFileInfo *cfile);

extern void cifs_down_write(struct rw_semaphore *sem);
struct cifsFileInfo *cifs_new_fileinfo(struct cifs_fid *fid, struct file *file,
				       struct tcon_link *tlink, __u32 oplock,
				       const char *symlink_target);
extern int cifs_posix_open(const char *full_path, struct inode **inode,
			   struct super_block *sb, int mode,
			   unsigned int f_flags, __u32 *oplock, __u16 *netfid,
			   unsigned int xid);
void cifs_fill_uniqueid(struct super_block *sb, struct cifs_fattr *fattr);
extern void cifs_unix_basic_to_fattr(struct cifs_fattr *fattr,
				     FILE_UNIX_BASIC_INFO *info,
				     struct cifs_sb_info *cifs_sb);
extern void cifs_dir_info_to_fattr(struct cifs_fattr *, FILE_DIRECTORY_INFO *,
					struct cifs_sb_info *);
extern int cifs_fattr_to_inode(struct inode *inode, struct cifs_fattr *fattr,
			       bool from_readdir);
extern struct inode *cifs_iget(struct super_block *sb,
			       struct cifs_fattr *fattr);

int cifs_get_inode_info(struct inode **inode, const char *full_path,
			struct cifs_open_info_data *data, struct super_block *sb, int xid,
			const struct cifs_fid *fid);
extern int smb311_posix_get_inode_info(struct inode **inode,
				       const char *full_path,
				       struct cifs_open_info_data *data,
				       struct super_block *sb,
				       const unsigned int xid);
extern int cifs_get_inode_info_unix(struct inode **pinode,
			const unsigned char *search_path,
			struct super_block *sb, unsigned int xid);
extern int cifs_set_file_info(struct inode *inode, struct iattr *attrs,
			      unsigned int xid, const char *full_path, __u32 dosattr);
extern int cifs_rename_pending_delete(const char *full_path,
				      struct dentry *dentry,
				      const unsigned int xid);
extern int sid_to_id(struct cifs_sb_info *cifs_sb, struct cifs_sid *psid,
				struct cifs_fattr *fattr, uint sidtype);
extern int cifs_acl_to_fattr(struct cifs_sb_info *cifs_sb,
			      struct cifs_fattr *fattr, struct inode *inode,
			      bool get_mode_from_special_sid,
			      const char *path, const struct cifs_fid *pfid);
extern int id_mode_to_cifs_acl(struct inode *inode, const char *path, __u64 *pnmode,
					kuid_t uid, kgid_t gid);
extern struct cifs_ntsd *get_cifs_acl(struct cifs_sb_info *, struct inode *,
				      const char *, u32 *, u32);
extern struct cifs_ntsd *get_cifs_acl_by_fid(struct cifs_sb_info *,
				const struct cifs_fid *, u32 *, u32);
extern struct posix_acl *cifs_get_acl(struct mnt_idmap *idmap,
				      struct dentry *dentry, int type);
extern int cifs_set_acl(struct mnt_idmap *idmap,
			struct dentry *dentry, struct posix_acl *acl, int type);
extern int set_cifs_acl(struct cifs_ntsd *, __u32, struct inode *,
				const char *, int);
extern unsigned int setup_authusers_ACE(struct cifs_ace *pace);
extern unsigned int setup_special_mode_ACE(struct cifs_ace *pace, __u64 nmode);
extern unsigned int setup_special_user_owner_ACE(struct cifs_ace *pace);

extern void dequeue_mid(struct mid_q_entry *mid, bool malformed);
extern int cifs_read_from_socket(struct TCP_Server_Info *server, char *buf,
			         unsigned int to_read);
extern ssize_t cifs_discard_from_socket(struct TCP_Server_Info *server,
					size_t to_read);
extern int cifs_read_page_from_socket(struct TCP_Server_Info *server,
					struct page *page,
					unsigned int page_offset,
					unsigned int to_read);
int cifs_read_iter_from_socket(struct TCP_Server_Info *server,
			       struct iov_iter *iter,
			       unsigned int to_read);
extern int cifs_setup_cifs_sb(struct cifs_sb_info *cifs_sb);
void cifs_mount_put_conns(struct cifs_mount_ctx *mnt_ctx);
int cifs_mount_get_session(struct cifs_mount_ctx *mnt_ctx);
int cifs_is_path_remote(struct cifs_mount_ctx *mnt_ctx);
int cifs_mount_get_tcon(struct cifs_mount_ctx *mnt_ctx);
extern int cifs_match_super(struct super_block *, void *);
extern int cifs_mount(struct cifs_sb_info *cifs_sb, struct smb3_fs_context *ctx);
extern void cifs_umount(struct cifs_sb_info *);
extern void cifs_mark_open_files_invalid(struct cifs_tcon *tcon);
extern void cifs_reopen_persistent_handles(struct cifs_tcon *tcon);

extern bool cifs_find_lock_conflict(struct cifsFileInfo *cfile, __u64 offset,
				    __u64 length, __u8 type, __u16 flags,
				    struct cifsLockInfo **conf_lock,
				    int rw_check);
extern void cifs_add_pending_open(struct cifs_fid *fid,
				  struct tcon_link *tlink,
				  struct cifs_pending_open *open);
extern void cifs_add_pending_open_locked(struct cifs_fid *fid,
					 struct tcon_link *tlink,
					 struct cifs_pending_open *open);
extern void cifs_del_pending_open(struct cifs_pending_open *open);

extern bool cifs_is_deferred_close(struct cifsFileInfo *cfile,
				struct cifs_deferred_close **dclose);

extern void cifs_add_deferred_close(struct cifsFileInfo *cfile,
				struct cifs_deferred_close *dclose);

extern void cifs_del_deferred_close(struct cifsFileInfo *cfile);

extern void cifs_close_deferred_file(struct cifsInodeInfo *cifs_inode);

extern void cifs_close_all_deferred_files(struct cifs_tcon *cifs_tcon);

extern void cifs_close_deferred_file_under_dentry(struct cifs_tcon *cifs_tcon,
				const char *path);

extern void cifs_mark_open_handles_for_deleted_file(struct inode *inode,
				const char *path);

extern struct TCP_Server_Info *
cifs_get_tcp_session(struct smb3_fs_context *ctx,
		     struct TCP_Server_Info *primary_server);
extern void cifs_put_tcp_session(struct TCP_Server_Info *server,
				 int from_reconnect);
extern void cifs_put_tcon(struct cifs_tcon *tcon, enum smb3_tcon_ref_trace trace);

extern void cifs_release_automount_timer(void);

void cifs_proc_init(void);
void cifs_proc_clean(void);

extern void cifs_move_llist(struct list_head *source, struct list_head *dest);
extern void cifs_free_llist(struct list_head *llist);
extern void cifs_del_lock_waiters(struct cifsLockInfo *lock);

extern int cifs_tree_connect(const unsigned int xid, struct cifs_tcon *tcon,
			     const struct nls_table *nlsc);

extern int cifs_negotiate_protocol(const unsigned int xid,
				   struct cifs_ses *ses,
				   struct TCP_Server_Info *server);
extern int cifs_setup_session(const unsigned int xid, struct cifs_ses *ses,
			      struct TCP_Server_Info *server,
			      struct nls_table *nls_info);
extern int cifs_enable_signing(struct TCP_Server_Info *server, bool mnt_sign_required);
extern int CIFSSMBNegotiate(const unsigned int xid,
			    struct cifs_ses *ses,
			    struct TCP_Server_Info *server);

extern int CIFSTCon(const unsigned int xid, struct cifs_ses *ses,
		    const char *tree, struct cifs_tcon *tcon,
		    const struct nls_table *);

extern int CIFSFindFirst(const unsigned int xid, struct cifs_tcon *tcon,
		const char *searchName, struct cifs_sb_info *cifs_sb,
		__u16 *searchHandle, __u16 search_flags,
		struct cifs_search_info *psrch_inf,
		bool msearch);

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

extern int parse_dfs_referrals(struct get_dfs_referral_rsp *rsp, u32 rsp_size,
			       unsigned int *num_of_nodes,
			       struct dfs_info3_param **target_nodes,
			       const struct nls_table *nls_codepage, int remap,
			       const char *searchName, bool is_unicode);
extern void reset_cifs_unix_caps(unsigned int xid, struct cifs_tcon *tcon,
				 struct cifs_sb_info *cifs_sb,
				 struct smb3_fs_context *ctx);
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
			struct cifs_sb_info *cifs_sb);
extern int CIFSSMBSetFileInfo(const unsigned int xid, struct cifs_tcon *tcon,
			const FILE_BASIC_INFO *data, __u16 fid,
			__u32 pid_of_opener);
extern int CIFSSMBSetFileDisposition(const unsigned int xid,
				     struct cifs_tcon *tcon,
				     bool delete_file, __u16 fid,
				     __u32 pid_of_opener);
extern int CIFSSMBSetEOF(const unsigned int xid, struct cifs_tcon *tcon,
			 const char *file_name, __u64 size,
			 struct cifs_sb_info *cifs_sb, bool set_allocation,
			 struct dentry *dentry);
extern int CIFSSMBSetFileSize(const unsigned int xid, struct cifs_tcon *tcon,
			      struct cifsFileInfo *cfile, __u64 size,
			      bool set_allocation);

struct cifs_unix_set_info_args {
	__u64	ctime;
	__u64	atime;
	__u64	mtime;
	__u64	mode;
	kuid_t	uid;
	kgid_t	gid;
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

extern int CIFSSMBMkDir(const unsigned int xid, struct inode *inode,
			umode_t mode, struct cifs_tcon *tcon,
			const char *name, struct cifs_sb_info *cifs_sb);
extern int CIFSSMBRmDir(const unsigned int xid, struct cifs_tcon *tcon,
			const char *name, struct cifs_sb_info *cifs_sb);
extern int CIFSPOSIXDelFile(const unsigned int xid, struct cifs_tcon *tcon,
			const char *name, __u16 type,
			const struct nls_table *nls_codepage,
			int remap_special_chars);
extern int CIFSSMBDelFile(const unsigned int xid, struct cifs_tcon *tcon,
			  const char *name, struct cifs_sb_info *cifs_sb,
			  struct dentry *dentry);
int CIFSSMBRename(const unsigned int xid, struct cifs_tcon *tcon,
		  struct dentry *source_dentry,
		  const char *from_name, const char *to_name,
		  struct cifs_sb_info *cifs_sb);
extern int CIFSSMBRenameOpenFile(const unsigned int xid, struct cifs_tcon *tcon,
				 int netfid, const char *target_name,
				 const struct nls_table *nls_codepage,
				 int remap_special_chars);
int CIFSCreateHardLink(const unsigned int xid,
		       struct cifs_tcon *tcon,
		       struct dentry *source_dentry,
		       const char *from_name, const char *to_name,
		       struct cifs_sb_info *cifs_sb);
extern int CIFSUnixCreateHardLink(const unsigned int xid,
			struct cifs_tcon *tcon,
			const char *fromName, const char *toName,
			const struct nls_table *nls_codepage,
			int remap_special_chars);
extern int CIFSUnixCreateSymLink(const unsigned int xid,
			struct cifs_tcon *tcon,
			const char *fromName, const char *toName,
			const struct nls_table *nls_codepage, int remap);
extern int CIFSSMBUnixQuerySymLink(const unsigned int xid,
			struct cifs_tcon *tcon,
			const unsigned char *searchName, char **syminfo,
			const struct nls_table *nls_codepage, int remap);
extern int cifs_query_reparse_point(const unsigned int xid,
				    struct cifs_tcon *tcon,
				    struct cifs_sb_info *cifs_sb,
				    const char *full_path,
				    u32 *tag, struct kvec *rsp,
				    int *rsp_buftype);
extern int CIFSSMBQuerySymLink(const unsigned int xid, struct cifs_tcon *tcon,
			       __u16 fid, char **symlinkinfo,
			       const struct nls_table *nls_codepage);
extern int CIFSSMB_set_compression(const unsigned int xid,
				   struct cifs_tcon *tcon, __u16 fid);
extern int CIFS_open(const unsigned int xid, struct cifs_open_parms *oparms,
		     int *oplock, FILE_ALL_INFO *buf);
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
			unsigned int *nbytes, const char *buf);
extern int CIFSSMBWrite2(const unsigned int xid, struct cifs_io_parms *io_parms,
			unsigned int *nbytes, struct kvec *iov, const int nvec);
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
extern struct cifs_tcon *tcon_info_alloc(bool dir_leases_enabled,
					 enum smb3_tcon_ref_trace trace);
extern void tconInfoFree(struct cifs_tcon *tcon, enum smb3_tcon_ref_trace trace);

extern int cifs_sign_rqst(struct smb_rqst *rqst, struct TCP_Server_Info *server,
		   __u32 *pexpected_response_sequence_number);
extern int cifs_sign_smbv(struct kvec *iov, int n_vec, struct TCP_Server_Info *,
			  __u32 *);
extern int cifs_sign_smb(struct smb_hdr *, struct TCP_Server_Info *, __u32 *);
extern int cifs_verify_signature(struct smb_rqst *rqst,
				 struct TCP_Server_Info *server,
				__u32 expected_sequence_number);
extern int setup_ntlmv2_rsp(struct cifs_ses *, const struct nls_table *);
extern void cifs_crypto_secmech_release(struct TCP_Server_Info *server);
extern int calc_seckey(struct cifs_ses *);
extern int generate_smb30signingkey(struct cifs_ses *ses,
				    struct TCP_Server_Info *server);
extern int generate_smb311signingkey(struct cifs_ses *ses,
				     struct TCP_Server_Info *server);

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
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
			size_t bufsize, struct cifs_sb_info *cifs_sb);
extern int CIFSSMBSetEA(const unsigned int xid, struct cifs_tcon *tcon,
		const char *fileName, const char *ea_name,
		const void *ea_value, const __u16 ea_value_len,
		const struct nls_table *nls_codepage,
		struct cifs_sb_info *cifs_sb);
extern int CIFSSMBGetCIFSACL(const unsigned int xid, struct cifs_tcon *tcon,
			__u16 fid, struct cifs_ntsd **acl_inf, __u32 *buflen);
extern int CIFSSMBSetCIFSACL(const unsigned int, struct cifs_tcon *, __u16,
			struct cifs_ntsd *, __u32, int);
extern int cifs_do_get_acl(const unsigned int xid, struct cifs_tcon *tcon,
			   const unsigned char *searchName,
			   struct posix_acl **acl, const int acl_type,
			   const struct nls_table *nls_codepage, int remap);
extern int cifs_do_set_acl(const unsigned int xid, struct cifs_tcon *tcon,
			   const unsigned char *fileName,
			   const struct posix_acl *acl, const int acl_type,
			   const struct nls_table *nls_codepage, int remap);
extern int CIFSGetExtAttr(const unsigned int xid, struct cifs_tcon *tcon,
			const int netfid, __u64 *pExtAttrBits, __u64 *pMask);
#endif /* CIFS_ALLOW_INSECURE_LEGACY */
extern void cifs_autodisable_serverino(struct cifs_sb_info *cifs_sb);
extern bool couldbe_mf_symlink(const struct cifs_fattr *fattr);
extern int check_mf_symlink(unsigned int xid, struct cifs_tcon *tcon,
			      struct cifs_sb_info *cifs_sb,
			      struct cifs_fattr *fattr,
			      const unsigned char *path);
extern int E_md4hash(const unsigned char *passwd, unsigned char *p16,
			const struct nls_table *codepage);

extern struct TCP_Server_Info *
cifs_find_tcp_session(struct smb3_fs_context *ctx);

void __cifs_put_smb_ses(struct cifs_ses *ses);

extern struct cifs_ses *
cifs_get_smb_ses(struct TCP_Server_Info *server, struct smb3_fs_context *ctx);

void cifs_readdata_release(struct kref *refcount);
int cifs_async_readv(struct cifs_readdata *rdata);
int cifs_readv_receive(struct TCP_Server_Info *server, struct mid_q_entry *mid);

int cifs_async_writev(struct cifs_writedata *wdata,
		      void (*release)(struct kref *kref));
void cifs_writev_complete(struct work_struct *work);
struct cifs_writedata *cifs_writedata_alloc(work_func_t complete);
void cifs_writedata_release(struct kref *refcount);
int cifs_query_mf_symlink(unsigned int xid, struct cifs_tcon *tcon,
			  struct cifs_sb_info *cifs_sb,
			  const unsigned char *path, char *pbuf,
			  unsigned int *pbytes_read);
int cifs_create_mf_symlink(unsigned int xid, struct cifs_tcon *tcon,
			   struct cifs_sb_info *cifs_sb,
			   const unsigned char *path, char *pbuf,
			   unsigned int *pbytes_written);
int __cifs_calc_signature(struct smb_rqst *rqst,
			struct TCP_Server_Info *server, char *signature,
			struct shash_desc *shash);
enum securityEnum cifs_select_sectype(struct TCP_Server_Info *,
					enum securityEnum);
struct cifs_aio_ctx *cifs_aio_ctx_alloc(void);
void cifs_aio_ctx_release(struct kref *refcount);

int cifs_alloc_hash(const char *name, struct shash_desc **sdesc);
void cifs_free_hash(struct shash_desc **sdesc);

struct cifs_chan *
cifs_ses_find_chan(struct cifs_ses *ses, struct TCP_Server_Info *server);
int cifs_try_adding_channels(struct cifs_ses *ses);
bool is_server_using_iface(struct TCP_Server_Info *server,
			   struct cifs_server_iface *iface);
bool is_ses_using_iface(struct cifs_ses *ses, struct cifs_server_iface *iface);
void cifs_ses_mark_for_reconnect(struct cifs_ses *ses);

int
cifs_ses_get_chan_index(struct cifs_ses *ses,
			struct TCP_Server_Info *server);
void
cifs_chan_set_in_reconnect(struct cifs_ses *ses,
			     struct TCP_Server_Info *server);
void
cifs_chan_clear_in_reconnect(struct cifs_ses *ses,
			       struct TCP_Server_Info *server);
bool
cifs_chan_in_reconnect(struct cifs_ses *ses,
			  struct TCP_Server_Info *server);
void
cifs_chan_set_need_reconnect(struct cifs_ses *ses,
			     struct TCP_Server_Info *server);
void
cifs_chan_clear_need_reconnect(struct cifs_ses *ses,
			       struct TCP_Server_Info *server);
bool
cifs_chan_needs_reconnect(struct cifs_ses *ses,
			  struct TCP_Server_Info *server);
bool
cifs_chan_is_iface_active(struct cifs_ses *ses,
			  struct TCP_Server_Info *server);
void
cifs_disable_secondary_channels(struct cifs_ses *ses);
void
cifs_chan_update_iface(struct cifs_ses *ses, struct TCP_Server_Info *server);
int
SMB3_request_interfaces(const unsigned int xid, struct cifs_tcon *tcon, bool in_mount);

void extract_unc_hostname(const char *unc, const char **h, size_t *len);
int copy_path_name(char *dst, const char *src);
int smb2_parse_query_directory(struct cifs_tcon *tcon, struct kvec *rsp_iov,
			       int resp_buftype,
			       struct cifs_search_info *srch_inf);

struct super_block *cifs_get_dfs_tcon_super(struct cifs_tcon *tcon);
void cifs_put_tcp_super(struct super_block *sb);
int cifs_update_super_prepath(struct cifs_sb_info *cifs_sb, char *prefix);
char *extract_hostname(const char *unc);
char *extract_sharename(const char *unc);
int parse_reparse_point(struct reparse_data_buffer *buf,
			u32 plen, struct cifs_sb_info *cifs_sb,
			bool unicode, struct cifs_open_info_data *data);
int cifs_sfu_make_node(unsigned int xid, struct inode *inode,
		       struct dentry *dentry, struct cifs_tcon *tcon,
		       const char *full_path, umode_t mode, dev_t dev);

#ifdef CONFIG_CIFS_DFS_UPCALL
static inline int get_dfs_path(const unsigned int xid, struct cifs_ses *ses,
			       const char *old_path,
			       const struct nls_table *nls_codepage,
			       struct dfs_info3_param *referral, int remap)
{
	return dfs_cache_find(xid, ses, nls_codepage, remap, old_path,
			      referral, NULL);
}

int match_target_ip(struct TCP_Server_Info *server,
		    const char *share, size_t share_len,
		    bool *result);
int cifs_inval_name_dfs_link_error(const unsigned int xid,
				   struct cifs_tcon *tcon,
				   struct cifs_sb_info *cifs_sb,
				   const char *full_path,
				   bool *islink);
#else
static inline int cifs_inval_name_dfs_link_error(const unsigned int xid,
				   struct cifs_tcon *tcon,
				   struct cifs_sb_info *cifs_sb,
				   const char *full_path,
				   bool *islink)
{
	*islink = false;
	return 0;
}
#endif

static inline int cifs_create_options(struct cifs_sb_info *cifs_sb, int options)
{
	if (cifs_sb && (backup_cred(cifs_sb)))
		return options | CREATE_OPEN_BACKUP_INTENT;
	else
		return options;
}

int cifs_wait_for_server_reconnect(struct TCP_Server_Info *server, bool retry);

/* Put references of @ses and its children */
static inline void cifs_put_smb_ses(struct cifs_ses *ses)
{
	struct cifs_ses *next;

	do {
		next = ses->dfs_root_ses;
		__cifs_put_smb_ses(ses);
	} while ((ses = next));
}

/* Get an active reference of @ses and its children.
 *
 * NOTE: make sure to call this function when incrementing reference count of
 * @ses to ensure that any DFS root session attached to it (@ses->dfs_root_ses)
 * will also get its reference count incremented.
 *
 * cifs_put_smb_ses() will put all references, so call it when you're done.
 */
static inline void cifs_smb_ses_inc_refcount(struct cifs_ses *ses)
{
	lockdep_assert_held(&cifs_tcp_ses_lock);

	for (; ses; ses = ses->dfs_root_ses)
		ses->ses_count++;
}

static inline bool dfs_src_pathname_equal(const char *s1, const char *s2)
{
	if (strlen(s1) != strlen(s2))
		return false;
	for (; *s1; s1++, s2++) {
		if (*s1 == '/' || *s1 == '\\') {
			if (*s2 != '/' && *s2 != '\\')
				return false;
		} else if (tolower(*s1) != tolower(*s2))
			return false;
	}
	return true;
}

static inline void release_mid(struct mid_q_entry *mid)
{
	kref_put(&mid->refcount, __release_mid);
}

static inline void cifs_free_open_info(struct cifs_open_info_data *data)
{
	kfree(data->symlink_target);
	free_rsp_buf(data->reparse.io.buftype, data->reparse.io.iov.iov_base);
	memset(data, 0, sizeof(*data));
}

#endif			/* _CIFSPROTO_H */
