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
#include "cifsglob.h"
#include "trace.h"
#ifdef CONFIG_CIFS_DFS_UPCALL
#include "dfs_cache.h"
#endif
#include "smb1proto.h"

struct statfs;
struct smb_rqst;
struct smb3_fs_context;

/*
 *****************************************************************
 * All Prototypes
 *****************************************************************
 */

struct smb_hdr *cifs_buf_get(void);
void cifs_buf_release(void *buf_to_free);
struct smb_hdr *cifs_small_buf_get(void);
void cifs_small_buf_release(void *buf_to_free);
void free_rsp_buf(int resp_buftype, void *rsp);
int smb_send_kvec(struct TCP_Server_Info *server, struct msghdr *smb_msg,
		  size_t *sent);
unsigned int _get_xid(void);
void _free_xid(unsigned int xid);
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
int init_cifs_idmap(void);
void exit_cifs_idmap(void);
int init_cifs_spnego(void);
void exit_cifs_spnego(void);
const char *build_path_from_dentry(struct dentry *direntry, void *page);
char *__build_path_from_dentry_optional_prefix(struct dentry *direntry,
					       void *page, const char *tree,
					       int tree_len, bool prefix);
char *build_path_from_dentry_optional_prefix(struct dentry *direntry,
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

char *cifs_build_path_to_root(struct smb3_fs_context *ctx,
			      struct cifs_sb_info *cifs_sb,
			      struct cifs_tcon *tcon, int add_treename);
char *cifs_build_devname(char *nodename, const char *prepath);
void delete_mid(struct TCP_Server_Info *server, struct mid_q_entry *mid);
void __release_mid(struct TCP_Server_Info *server,
		   struct mid_q_entry *midEntry);
void cifs_wake_up_task(struct TCP_Server_Info *server,
		       struct mid_q_entry *mid);
int cifs_handle_standard(struct TCP_Server_Info *server,
			 struct mid_q_entry *mid);
char *smb3_fs_context_fullpath(const struct smb3_fs_context *ctx, char dirsep);
int smb3_parse_devname(const char *devname, struct smb3_fs_context *ctx);
int smb3_parse_opt(const char *options, const char *key, char **val);
int cifs_ipaddr_cmp(struct sockaddr *srcaddr, struct sockaddr *rhs);
bool cifs_match_ipaddr(struct sockaddr *srcaddr, struct sockaddr *rhs);
int cifs_discard_remaining_data(struct TCP_Server_Info *server);
int cifs_call_async(struct TCP_Server_Info *server, struct smb_rqst *rqst,
		    mid_receive_t receive, mid_callback_t callback,
		    mid_handle_t handle, void *cbdata, const int flags,
		    const struct cifs_credits *exist_credits);
struct TCP_Server_Info *cifs_pick_channel(struct cifs_ses *ses);
int cifs_send_recv(const unsigned int xid, struct cifs_ses *ses,
		   struct TCP_Server_Info *server, struct smb_rqst *rqst,
		   int *resp_buf_type, const int flags, struct kvec *resp_iov);
int compound_send_recv(const unsigned int xid, struct cifs_ses *ses,
		       struct TCP_Server_Info *server, const int flags,
		       const int num_rqst, struct smb_rqst *rqst,
		       int *resp_buf_type, struct kvec *resp_iov);
int SendReceive(const unsigned int xid, struct cifs_ses *ses,
		struct smb_hdr *in_buf, unsigned int in_len,
		struct smb_hdr *out_buf, int *pbytes_returned,
		const int flags);
int SendReceiveNoRsp(const unsigned int xid, struct cifs_ses *ses,
		     char *in_buf, unsigned int in_len, int flags);
int cifs_sync_mid_result(struct mid_q_entry *mid,
			 struct TCP_Server_Info *server);
struct mid_q_entry *cifs_setup_request(struct cifs_ses *ses,
				       struct TCP_Server_Info *server,
				       struct smb_rqst *rqst);
struct mid_q_entry *cifs_setup_async_request(struct TCP_Server_Info *server,
					     struct smb_rqst *rqst);
int __smb_send_rqst(struct TCP_Server_Info *server, int num_rqst,
		    struct smb_rqst *rqst);
int cifs_check_receive(struct mid_q_entry *mid, struct TCP_Server_Info *server,
		       bool log_error);
int wait_for_free_request(struct TCP_Server_Info *server, const int flags,
			  unsigned int *instance);
int cifs_wait_mtu_credits(struct TCP_Server_Info *server, size_t size,
			  size_t *num, struct cifs_credits *credits);

static inline int
send_cancel(struct cifs_ses *ses, struct TCP_Server_Info *server,
	    struct smb_rqst *rqst, struct mid_q_entry *mid,
	    unsigned int xid)
{
	return server->ops->send_cancel ?
		server->ops->send_cancel(ses, server, rqst, mid, xid) : 0;
}

int wait_for_response(struct TCP_Server_Info *server, struct mid_q_entry *mid);
int SendReceive2(const unsigned int xid, struct cifs_ses *ses,
		 struct kvec *iov, int n_vec, int *resp_buf_type /* ret */,
		 const int flags, struct kvec *resp_iov);

void smb2_query_server_interfaces(struct work_struct *work);
void cifs_signal_cifsd_for_reconnect(struct TCP_Server_Info *server,
				     bool all_channels);
void cifs_mark_tcp_ses_conns_for_reconnect(struct TCP_Server_Info *server,
					   bool mark_smb_session);
int cifs_reconnect(struct TCP_Server_Info *server, bool mark_smb_session);
int checkSMB(char *buf, unsigned int pdu_len, unsigned int total_read,
	     struct TCP_Server_Info *server);
bool is_valid_oplock_break(char *buffer, struct TCP_Server_Info *srv);
bool backup_cred(struct cifs_sb_info *cifs_sb);
bool is_size_safe_to_change(struct cifsInodeInfo *cifsInode, __u64 end_of_file,
			    bool from_readdir);
void cifs_write_subrequest_terminated(struct cifs_io_subrequest *wdata,
				      ssize_t result);
struct cifsFileInfo *find_writable_file(struct cifsInodeInfo *cifs_inode,
					int flags);
int cifs_get_writable_file(struct cifsInodeInfo *cifs_inode, int flags,
			   struct cifsFileInfo **ret_file);
int cifs_get_writable_path(struct cifs_tcon *tcon, const char *name, int flags,
			   struct cifsFileInfo **ret_file);
struct cifsFileInfo *find_readable_file(struct cifsInodeInfo *cifs_inode,
					bool fsuid_only);
int cifs_get_readable_path(struct cifs_tcon *tcon, const char *name,
			   struct cifsFileInfo **ret_file);
int cifs_get_hardlink_path(struct cifs_tcon *tcon, struct inode *inode,
			   struct file *file);
unsigned int smbCalcSize(void *buf);
int decode_negTokenInit(unsigned char *security_blob, int length,
			struct TCP_Server_Info *server);
int cifs_convert_address(struct sockaddr *dst, const char *src, int len);
void cifs_set_port(struct sockaddr *addr, const unsigned short int port);
int map_smb_to_linux_error(char *buf, bool logErr);
int map_and_check_smb_error(struct TCP_Server_Info *server,
			    struct mid_q_entry *mid, bool logErr);
unsigned int header_assemble(struct smb_hdr *buffer, char smb_command,
			     const struct cifs_tcon *treeCon, int word_count
			     /* length of fixed section (word count) in two byte units  */);
int small_smb_init_no_tc(const int smb_command, const int wct,
			 struct cifs_ses *ses, void **request_buf);
int CIFS_SessSetup(const unsigned int xid, struct cifs_ses *ses,
		   struct TCP_Server_Info *server,
		   const struct nls_table *nls_cp);
struct timespec64 cifs_NTtimeToUnix(__le64 ntutc);
u64 cifs_UnixTimeToNT(struct timespec64 t);
struct timespec64 cnvrtDosUnixTm(__le16 le_date, __le16 le_time, int offset);
void cifs_set_oplock_level(struct cifsInodeInfo *cinode, __u32 oplock);
int cifs_get_writer(struct cifsInodeInfo *cinode);
void cifs_put_writer(struct cifsInodeInfo *cinode);
void cifs_done_oplock_break(struct cifsInodeInfo *cinode);
int cifs_unlock_range(struct cifsFileInfo *cfile, struct file_lock *flock,
		      unsigned int xid);
int cifs_push_mandatory_locks(struct cifsFileInfo *cfile);

void cifs_down_write(struct rw_semaphore *sem);
struct cifsFileInfo *cifs_new_fileinfo(struct cifs_fid *fid, struct file *file,
				       struct tcon_link *tlink, __u32 oplock,
				       const char *symlink_target);
int cifs_posix_open(const char *full_path, struct inode **pinode,
		    struct super_block *sb, int mode, unsigned int f_flags,
		    __u32 *poplock, __u16 *pnetfid, unsigned int xid);
void cifs_fill_uniqueid(struct super_block *sb, struct cifs_fattr *fattr);
void cifs_unix_basic_to_fattr(struct cifs_fattr *fattr,
			      FILE_UNIX_BASIC_INFO *info,
			      struct cifs_sb_info *cifs_sb);
void cifs_dir_info_to_fattr(struct cifs_fattr *fattr,
			    FILE_DIRECTORY_INFO *info,
			    struct cifs_sb_info *cifs_sb);
int cifs_fattr_to_inode(struct inode *inode, struct cifs_fattr *fattr,
			bool from_readdir);
struct inode *cifs_iget(struct super_block *sb, struct cifs_fattr *fattr);

int cifs_get_inode_info(struct inode **inode, const char *full_path,
			struct cifs_open_info_data *data,
			struct super_block *sb, int xid,
			const struct cifs_fid *fid);
int smb311_posix_get_inode_info(struct inode **inode, const char *full_path,
				struct cifs_open_info_data *data,
				struct super_block *sb,
				const unsigned int xid);
int cifs_get_inode_info_unix(struct inode **pinode,
			     const unsigned char *full_path,
			     struct super_block *sb, unsigned int xid);
int cifs_set_file_info(struct inode *inode, struct iattr *attrs,
		       unsigned int xid, const char *full_path, __u32 dosattr);
int cifs_rename_pending_delete(const char *full_path, struct dentry *dentry,
			       const unsigned int xid);
int sid_to_id(struct cifs_sb_info *cifs_sb, struct smb_sid *psid,
	      struct cifs_fattr *fattr, uint sidtype);
int cifs_acl_to_fattr(struct cifs_sb_info *cifs_sb, struct cifs_fattr *fattr,
		      struct inode *inode, bool mode_from_special_sid,
		      const char *path, const struct cifs_fid *pfid);
int id_mode_to_cifs_acl(struct inode *inode, const char *path, __u64 *pnmode,
			kuid_t uid, kgid_t gid);
struct smb_ntsd *get_cifs_acl(struct cifs_sb_info *cifs_sb,
			      struct inode *inode, const char *path,
			      u32 *pacllen, u32 info);
struct smb_ntsd *get_cifs_acl_by_fid(struct cifs_sb_info *cifs_sb,
				     const struct cifs_fid *cifsfid,
				     u32 *pacllen, u32 info);
struct posix_acl *cifs_get_acl(struct mnt_idmap *idmap, struct dentry *dentry,
			       int type);
int cifs_set_acl(struct mnt_idmap *idmap, struct dentry *dentry,
		 struct posix_acl *acl, int type);
int set_cifs_acl(struct smb_ntsd *pnntsd, __u32 acllen, struct inode *inode,
		 const char *path, int aclflag);
unsigned int setup_authusers_ACE(struct smb_ace *pntace);
unsigned int setup_special_mode_ACE(struct smb_ace *pntace, bool posix,
				    __u64 nmode);
unsigned int setup_special_user_owner_ACE(struct smb_ace *pntace);

void dequeue_mid(struct TCP_Server_Info *server, struct mid_q_entry *mid,
		 bool malformed);
int cifs_read_from_socket(struct TCP_Server_Info *server, char *buf,
			  unsigned int to_read);
ssize_t cifs_discard_from_socket(struct TCP_Server_Info *server,
				 size_t to_read);
int cifs_read_iter_from_socket(struct TCP_Server_Info *server,
			       struct iov_iter *iter, unsigned int to_read);
int cifs_setup_cifs_sb(struct cifs_sb_info *cifs_sb);
void cifs_mount_put_conns(struct cifs_mount_ctx *mnt_ctx);
int cifs_mount_get_session(struct cifs_mount_ctx *mnt_ctx);
int cifs_is_path_remote(struct cifs_mount_ctx *mnt_ctx);
int cifs_mount_get_tcon(struct cifs_mount_ctx *mnt_ctx);
int cifs_match_super(struct super_block *sb, void *data);
int cifs_mount(struct cifs_sb_info *cifs_sb, struct smb3_fs_context *ctx);
void cifs_umount(struct cifs_sb_info *cifs_sb);
void cifs_mark_open_files_invalid(struct cifs_tcon *tcon);
void cifs_reopen_persistent_handles(struct cifs_tcon *tcon);

bool cifs_find_lock_conflict(struct cifsFileInfo *cfile, __u64 offset,
			     __u64 length, __u8 type, __u16 flags,
			     struct cifsLockInfo **conf_lock, int rw_check);
void cifs_add_pending_open(struct cifs_fid *fid, struct tcon_link *tlink,
			   struct cifs_pending_open *open);
void cifs_add_pending_open_locked(struct cifs_fid *fid,
				  struct tcon_link *tlink,
				  struct cifs_pending_open *open);
void cifs_del_pending_open(struct cifs_pending_open *open);

bool cifs_is_deferred_close(struct cifsFileInfo *cfile,
			    struct cifs_deferred_close **pdclose);

void cifs_add_deferred_close(struct cifsFileInfo *cfile,
			     struct cifs_deferred_close *dclose);

void cifs_del_deferred_close(struct cifsFileInfo *cfile);

void cifs_close_deferred_file(struct cifsInodeInfo *cifs_inode);

void cifs_close_all_deferred_files(struct cifs_tcon *tcon);

void cifs_close_deferred_file_under_dentry(struct cifs_tcon *tcon,
					   struct dentry *dentry);

void cifs_mark_open_handles_for_deleted_file(struct inode *inode,
					     const char *path);

struct TCP_Server_Info *cifs_get_tcp_session(struct smb3_fs_context *ctx,
					     struct TCP_Server_Info *primary_server);
void cifs_put_tcp_session(struct TCP_Server_Info *server, int from_reconnect);
void cifs_put_tcon(struct cifs_tcon *tcon, enum smb3_tcon_ref_trace trace);

void cifs_release_automount_timer(void);

void cifs_proc_init(void);
void cifs_proc_clean(void);

void cifs_move_llist(struct list_head *source, struct list_head *dest);
void cifs_free_llist(struct list_head *llist);
void cifs_del_lock_waiters(struct cifsLockInfo *lock);

int cifs_tree_connect(const unsigned int xid, struct cifs_tcon *tcon);

int cifs_negotiate_protocol(const unsigned int xid, struct cifs_ses *ses,
			    struct TCP_Server_Info *server);
int cifs_setup_session(const unsigned int xid, struct cifs_ses *ses,
		       struct TCP_Server_Info *server,
		       struct nls_table *nls_info);
int cifs_enable_signing(struct TCP_Server_Info *server,
			bool mnt_sign_required);
int CIFSSMBNegotiate(const unsigned int xid, struct cifs_ses *ses,
		     struct TCP_Server_Info *server);

int CIFSTCon(const unsigned int xid, struct cifs_ses *ses, const char *tree,
	     struct cifs_tcon *tcon, const struct nls_table *nls_codepage);

int CIFSFindFirst(const unsigned int xid, struct cifs_tcon *tcon,
		  const char *searchName, struct cifs_sb_info *cifs_sb,
		  __u16 *pnetfid, __u16 search_flags,
		  struct cifs_search_info *psrch_inf, bool msearch);

int CIFSFindNext(const unsigned int xid, struct cifs_tcon *tcon,
		 __u16 searchHandle, __u16 search_flags,
		 struct cifs_search_info *psrch_inf);

int CIFSFindClose(const unsigned int xid, struct cifs_tcon *tcon,
		  const __u16 searchHandle);

int CIFSSMBQFileInfo(const unsigned int xid, struct cifs_tcon *tcon,
		     u16 netfid, FILE_ALL_INFO *pFindData);
int CIFSSMBQPathInfo(const unsigned int xid, struct cifs_tcon *tcon,
		     const char *search_name, FILE_ALL_INFO *data,
		     int legacy /* old style infolevel */,
		     const struct nls_table *nls_codepage, int remap);
int SMBQueryInformation(const unsigned int xid, struct cifs_tcon *tcon,
			const char *search_name, FILE_ALL_INFO *data,
			const struct nls_table *nls_codepage, int remap);

int CIFSSMBUnixQFileInfo(const unsigned int xid, struct cifs_tcon *tcon,
			 u16 netfid, FILE_UNIX_BASIC_INFO *pFindData);
int CIFSSMBUnixQPathInfo(const unsigned int xid, struct cifs_tcon *tcon,
			 const unsigned char *searchName,
			 FILE_UNIX_BASIC_INFO *pFindData,
			 const struct nls_table *nls_codepage, int remap);

int CIFSGetDFSRefer(const unsigned int xid, struct cifs_ses *ses,
		    const char *search_name,
		    struct dfs_info3_param **target_nodes,
		    unsigned int *num_of_nodes,
		    const struct nls_table *nls_codepage, int remap);

int parse_dfs_referrals(struct get_dfs_referral_rsp *rsp, u32 rsp_size,
			unsigned int *num_of_nodes,
			struct dfs_info3_param **target_nodes,
			const struct nls_table *nls_codepage, int remap,
			const char *searchName, bool is_unicode);
void reset_cifs_unix_caps(unsigned int xid, struct cifs_tcon *tcon,
			  struct cifs_sb_info *cifs_sb,
			  struct smb3_fs_context *ctx);
int CIFSSMBQFSInfo(const unsigned int xid, struct cifs_tcon *tcon,
		   struct kstatfs *FSData);
int SMBOldQFSInfo(const unsigned int xid, struct cifs_tcon *tcon,
		  struct kstatfs *FSData);
int CIFSSMBSetFSUnixInfo(const unsigned int xid, struct cifs_tcon *tcon,
			 __u64 cap);

int CIFSSMBQFSAttributeInfo(const unsigned int xid, struct cifs_tcon *tcon);
int CIFSSMBQFSDeviceInfo(const unsigned int xid, struct cifs_tcon *tcon);
int CIFSSMBQFSUnixInfo(const unsigned int xid, struct cifs_tcon *tcon);
int CIFSSMBQFSPosixInfo(const unsigned int xid, struct cifs_tcon *tcon,
			struct kstatfs *FSData);

int SMBSetInformation(const unsigned int xid, struct cifs_tcon *tcon,
		      const char *fileName, __le32 attributes,
		      __le64 write_time, const struct nls_table *nls_codepage,
		      struct cifs_sb_info *cifs_sb);
int CIFSSMBSetPathInfo(const unsigned int xid, struct cifs_tcon *tcon,
		       const char *fileName, const FILE_BASIC_INFO *data,
		       const struct nls_table *nls_codepage,
		       struct cifs_sb_info *cifs_sb);
int CIFSSMBSetFileInfo(const unsigned int xid, struct cifs_tcon *tcon,
		       const FILE_BASIC_INFO *data, __u16 fid,
		       __u32 pid_of_opener);
int CIFSSMBSetFileDisposition(const unsigned int xid, struct cifs_tcon *tcon,
			      bool delete_file, __u16 fid,
			      __u32 pid_of_opener);
int CIFSSMBSetEOF(const unsigned int xid, struct cifs_tcon *tcon,
		  const char *file_name, __u64 size,
		  struct cifs_sb_info *cifs_sb, bool set_allocation,
		  struct dentry *dentry);
int CIFSSMBSetFileSize(const unsigned int xid, struct cifs_tcon *tcon,
		       struct cifsFileInfo *cfile, __u64 size,
		       bool set_allocation);

int CIFSSMBUnixSetFileInfo(const unsigned int xid, struct cifs_tcon *tcon,
			   const struct cifs_unix_set_info_args *args, u16 fid,
			   u32 pid_of_opener);

int CIFSSMBUnixSetPathInfo(const unsigned int xid, struct cifs_tcon *tcon,
			   const char *file_name,
			   const struct cifs_unix_set_info_args *args,
			   const struct nls_table *nls_codepage, int remap);

int CIFSSMBMkDir(const unsigned int xid, struct inode *inode, umode_t mode,
		 struct cifs_tcon *tcon, const char *name,
		 struct cifs_sb_info *cifs_sb);
int CIFSSMBRmDir(const unsigned int xid, struct cifs_tcon *tcon,
		 const char *name, struct cifs_sb_info *cifs_sb);
int CIFSPOSIXDelFile(const unsigned int xid, struct cifs_tcon *tcon,
		     const char *fileName, __u16 type,
		     const struct nls_table *nls_codepage, int remap);
int CIFSSMBDelFile(const unsigned int xid, struct cifs_tcon *tcon,
		   const char *name, struct cifs_sb_info *cifs_sb,
		   struct dentry *dentry);
int CIFSSMBRename(const unsigned int xid, struct cifs_tcon *tcon,
		  struct dentry *source_dentry, const char *from_name,
		  const char *to_name, struct cifs_sb_info *cifs_sb);
int CIFSSMBRenameOpenFile(const unsigned int xid, struct cifs_tcon *pTcon,
			  int netfid, const char *target_name,
			  const struct nls_table *nls_codepage, int remap);
int CIFSCreateHardLink(const unsigned int xid, struct cifs_tcon *tcon,
		       struct dentry *source_dentry, const char *from_name,
		       const char *to_name, struct cifs_sb_info *cifs_sb);
int CIFSUnixCreateHardLink(const unsigned int xid, struct cifs_tcon *tcon,
			   const char *fromName, const char *toName,
			   const struct nls_table *nls_codepage, int remap);
int CIFSUnixCreateSymLink(const unsigned int xid, struct cifs_tcon *tcon,
			  const char *fromName, const char *toName,
			  const struct nls_table *nls_codepage, int remap);
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
int CIFS_open(const unsigned int xid, struct cifs_open_parms *oparms,
	      int *oplock, FILE_ALL_INFO *buf);
int SMBLegacyOpen(const unsigned int xid, struct cifs_tcon *tcon,
		  const char *fileName, const int openDisposition,
		  const int access_flags, const int create_options,
		  __u16 *netfid, int *pOplock, FILE_ALL_INFO *pfile_info,
		  const struct nls_table *nls_codepage, int remap);
int CIFSPOSIXCreate(const unsigned int xid, struct cifs_tcon *tcon,
		    __u32 posix_flags, __u64 mode, __u16 *netfid,
		    FILE_UNIX_BASIC_INFO *pRetData, __u32 *pOplock,
		    const char *name, const struct nls_table *nls_codepage,
		    int remap);
int CIFSSMBClose(const unsigned int xid, struct cifs_tcon *tcon,
		 int smb_file_id);

int CIFSSMBFlush(const unsigned int xid, struct cifs_tcon *tcon,
		 int smb_file_id);

int CIFSSMBRead(const unsigned int xid, struct cifs_io_parms *io_parms,
		unsigned int *nbytes, char **buf, int *pbuf_type);
int CIFSSMBWrite(const unsigned int xid, struct cifs_io_parms *io_parms,
		 unsigned int *nbytes, const char *buf);
int CIFSSMBWrite2(const unsigned int xid, struct cifs_io_parms *io_parms,
		  unsigned int *nbytes, struct kvec *iov, int n_vec);
int CIFSGetSrvInodeNumber(const unsigned int xid, struct cifs_tcon *tcon,
			  const char *search_name, __u64 *inode_number,
			  const struct nls_table *nls_codepage, int remap);

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
int CIFSSMBTDis(const unsigned int xid, struct cifs_tcon *tcon);
int CIFSSMBEcho(struct TCP_Server_Info *server);
int CIFSSMBLogoff(const unsigned int xid, struct cifs_ses *ses);

struct cifs_ses *sesInfoAlloc(void);
void sesInfoFree(struct cifs_ses *buf_to_free);
struct cifs_tcon *tcon_info_alloc(bool dir_leases_enabled,
				  enum smb3_tcon_ref_trace trace);
void tconInfoFree(struct cifs_tcon *tcon, enum smb3_tcon_ref_trace trace);

int cifs_sign_rqst(struct smb_rqst *rqst, struct TCP_Server_Info *server,
		   __u32 *pexpected_response_sequence_number);
int cifs_verify_signature(struct smb_rqst *rqst,
			  struct TCP_Server_Info *server,
			  __u32 expected_sequence_number);
int setup_ntlmv2_rsp(struct cifs_ses *ses, const struct nls_table *nls_cp);
void cifs_crypto_secmech_release(struct TCP_Server_Info *server);
int calc_seckey(struct cifs_ses *ses);
int generate_smb30signingkey(struct cifs_ses *ses,
			     struct TCP_Server_Info *server);
int generate_smb311signingkey(struct cifs_ses *ses,
			      struct TCP_Server_Info *server);

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
ssize_t CIFSSMBQAllEAs(const unsigned int xid, struct cifs_tcon *tcon,
		       const unsigned char *searchName,
		       const unsigned char *ea_name, char *EAData,
		       size_t buf_size, struct cifs_sb_info *cifs_sb);
int CIFSSMBSetEA(const unsigned int xid, struct cifs_tcon *tcon,
		 const char *fileName, const char *ea_name,
		 const void *ea_value, const __u16 ea_value_len,
		 const struct nls_table *nls_codepage,
		 struct cifs_sb_info *cifs_sb);
int CIFSSMBGetCIFSACL(const unsigned int xid, struct cifs_tcon *tcon,
		      __u16 fid, struct smb_ntsd **acl_inf, __u32 *pbuflen,
		      __u32 info);
int CIFSSMBSetCIFSACL(const unsigned int xid, struct cifs_tcon *tcon,
		      __u16 fid, struct smb_ntsd *pntsd, __u32 acllen,
		      int aclflag);
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
#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */
void cifs_autodisable_serverino(struct cifs_sb_info *cifs_sb);
bool couldbe_mf_symlink(const struct cifs_fattr *fattr);
int check_mf_symlink(unsigned int xid, struct cifs_tcon *tcon,
		     struct cifs_sb_info *cifs_sb, struct cifs_fattr *fattr,
		     const unsigned char *path);
int E_md4hash(const unsigned char *passwd, unsigned char *p16,
	      const struct nls_table *codepage);

struct TCP_Server_Info *cifs_find_tcp_session(struct smb3_fs_context *ctx);

struct cifs_tcon *cifs_setup_ipc(struct cifs_ses *ses, bool seal);

void __cifs_put_smb_ses(struct cifs_ses *ses);

struct cifs_ses *cifs_get_smb_ses(struct TCP_Server_Info *server,
				  struct smb3_fs_context *ctx);

int cifs_async_readv(struct cifs_io_subrequest *rdata);
int cifs_readv_receive(struct TCP_Server_Info *server,
		       struct mid_q_entry *mid);

void cifs_async_writev(struct cifs_io_subrequest *wdata);
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
			  struct cifs_calc_sig_ctx *ctx);
enum securityEnum cifs_select_sectype(struct TCP_Server_Info *server,
				      enum securityEnum requested);

int cifs_alloc_hash(const char *name, struct shash_desc **sdesc);
void cifs_free_hash(struct shash_desc **sdesc);

int cifs_try_adding_channels(struct cifs_ses *ses);
int smb3_update_ses_channels(struct cifs_ses *ses,
			     struct TCP_Server_Info *server,
			     bool from_reconnect, bool disable_mchan);
bool is_ses_using_iface(struct cifs_ses *ses, struct cifs_server_iface *iface);

int cifs_ses_get_chan_index(struct cifs_ses *ses,
			    struct TCP_Server_Info *server);
void cifs_chan_set_in_reconnect(struct cifs_ses *ses,
				struct TCP_Server_Info *server);
void cifs_chan_clear_in_reconnect(struct cifs_ses *ses,
				  struct TCP_Server_Info *server);
void cifs_chan_set_need_reconnect(struct cifs_ses *ses,
				  struct TCP_Server_Info *server);
void cifs_chan_clear_need_reconnect(struct cifs_ses *ses,
				    struct TCP_Server_Info *server);
bool cifs_chan_needs_reconnect(struct cifs_ses *ses,
			       struct TCP_Server_Info *server);
bool cifs_chan_is_iface_active(struct cifs_ses *ses,
			       struct TCP_Server_Info *server);
void cifs_decrease_secondary_channels(struct cifs_ses *ses,
				      bool disable_mchan);
void cifs_chan_update_iface(struct cifs_ses *ses,
			    struct TCP_Server_Info *server);
int SMB3_request_interfaces(const unsigned int xid, struct cifs_tcon *tcon,
			    bool in_mount);

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
int parse_reparse_point(struct reparse_data_buffer *buf, u32 plen,
			struct cifs_sb_info *cifs_sb, const char *full_path,
			struct cifs_open_info_data *data);
int __cifs_sfu_make_node(unsigned int xid, struct inode *inode,
			 struct dentry *dentry, struct cifs_tcon *tcon,
			 const char *full_path, umode_t mode, dev_t dev,
			 const char *symname);
int cifs_sfu_make_node(unsigned int xid, struct inode *inode,
		       struct dentry *dentry, struct cifs_tcon *tcon,
		       const char *full_path, umode_t mode, dev_t dev);
umode_t wire_mode_to_posix(u32 wire, bool is_dir);

#ifdef CONFIG_CIFS_DFS_UPCALL
static inline int get_dfs_path(const unsigned int xid, struct cifs_ses *ses,
			       const char *old_path,
			       const struct nls_table *nls_codepage,
			       struct dfs_info3_param *referral, int remap)
{
	return dfs_cache_find(xid, ses, nls_codepage, remap, old_path,
			      referral, NULL);
}

int match_target_ip(struct TCP_Server_Info *server, const char *host,
		    size_t hostlen, bool *result);
int cifs_inval_name_dfs_link_error(const unsigned int xid,
				   struct cifs_tcon *tcon,
				   struct cifs_sb_info *cifs_sb,
				   const char *full_path, bool *islink);
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

static inline void cifs_put_smb_ses(struct cifs_ses *ses)
{
	__cifs_put_smb_ses(ses);
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

static inline void smb_get_mid(struct mid_q_entry *mid)
{
	refcount_inc(&mid->refcount);
}

static inline void release_mid(struct TCP_Server_Info *server, struct mid_q_entry *mid)
{
	if (refcount_dec_and_test(&mid->refcount))
		__release_mid(server, mid);
}

static inline void cifs_free_open_info(struct cifs_open_info_data *data)
{
	kfree(data->symlink_target);
	free_rsp_buf(data->reparse.io.buftype, data->reparse.io.iov.iov_base);
	memset(data, 0, sizeof(*data));
}

static inline int smb_EIO(enum smb_eio_trace trace)
{
	trace_smb3_eio(trace, 0, 0);
	return -EIO;
}

static inline int smb_EIO1(enum smb_eio_trace trace, unsigned long info)
{
	trace_smb3_eio(trace, info, 0);
	return -EIO;
}

static inline int smb_EIO2(enum smb_eio_trace trace, unsigned long info, unsigned long info2)
{
	trace_smb3_eio(trace, info, info2);
	return -EIO;
}

static inline int cifs_get_num_sgs(const struct smb_rqst *rqst,
				   int num_rqst,
				   const u8 *sig)
{
	unsigned int len, skip;
	unsigned int nents = 0;
	unsigned long addr;
	size_t data_size;
	int i, j;

	/*
	 * The first rqst has a transform header where the first 20 bytes are
	 * not part of the encrypted blob.
	 */
	skip = 20;

	/* Assumes the first rqst has a transform header as the first iov.
	 * I.e.
	 * rqst[0].rq_iov[0]  is transform header
	 * rqst[0].rq_iov[1+] data to be encrypted/decrypted
	 * rqst[1+].rq_iov[0+] data to be encrypted/decrypted
	 */
	for (i = 0; i < num_rqst; i++) {
		data_size = iov_iter_count(&rqst[i].rq_iter);

		/* We really don't want a mixture of pinned and unpinned pages
		 * in the sglist.  It's hard to keep track of which is what.
		 * Instead, we convert to a BVEC-type iterator higher up.
		 */
		if (data_size &&
		    WARN_ON_ONCE(user_backed_iter(&rqst[i].rq_iter)))
			return smb_EIO(smb_eio_trace_user_iter);

		/* We also don't want to have any extra refs or pins to clean
		 * up in the sglist.
		 */
		if (data_size &&
		    WARN_ON_ONCE(iov_iter_extract_will_pin(&rqst[i].rq_iter)))
			return smb_EIO(smb_eio_trace_extract_will_pin);

		for (j = 0; j < rqst[i].rq_nvec; j++) {
			struct kvec *iov = &rqst[i].rq_iov[j];

			addr = (unsigned long)iov->iov_base + skip;
			if (is_vmalloc_or_module_addr((void *)addr)) {
				len = iov->iov_len - skip;
				nents += DIV_ROUND_UP(offset_in_page(addr) + len,
						      PAGE_SIZE);
			} else {
				nents++;
			}
			skip = 0;
		}
		if (data_size)
			nents += iov_iter_npages(&rqst[i].rq_iter, INT_MAX);
	}
	nents += DIV_ROUND_UP(offset_in_page(sig) + SMB2_SIGNATURE_SIZE, PAGE_SIZE);
	return nents;
}

/* We can not use the normal sg_set_buf() as we will sometimes pass a
 * stack object as buf.
 */
static inline void cifs_sg_set_buf(struct sg_table *sgtable,
				   const void *buf,
				   unsigned int buflen)
{
	unsigned long addr = (unsigned long)buf;
	unsigned int off = offset_in_page(addr);

	addr &= PAGE_MASK;
	if (is_vmalloc_or_module_addr((void *)addr)) {
		do {
			unsigned int len = min_t(unsigned int, buflen, PAGE_SIZE - off);

			sg_set_page(&sgtable->sgl[sgtable->nents++],
				    vmalloc_to_page((void *)addr), len, off);

			off = 0;
			addr += PAGE_SIZE;
			buflen -= len;
		} while (buflen);
	} else {
		sg_set_page(&sgtable->sgl[sgtable->nents++],
			    virt_to_page((void *)addr), buflen, off);
	}
}

#endif			/* _CIFSPROTO_H */
