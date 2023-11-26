/* SPDX-License-Identifier: LGPL-2.1 */
/*
 *
 *   Copyright (c) International Business Machines  Corp., 2002, 2011
 *                 Etersoft, 2012
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *              Pavel Shilovsky (pshilovsky@samba.org) 2012
 *
 */
#ifndef _SMB2PROTO_H
#define _SMB2PROTO_H
#include <linux/nls.h>
#include <linux/key-type.h>

struct statfs;
struct smb_rqst;

/*
 *****************************************************************
 * All Prototypes
 *****************************************************************
 */
extern int map_smb2_to_linux_error(char *buf, bool log_err);
extern int smb2_check_message(char *buf, unsigned int length,
			      struct TCP_Server_Info *server);
extern unsigned int smb2_calc_size(void *buf);
extern char *smb2_get_data_area_len(int *off, int *len,
				    struct smb2_hdr *shdr);
extern __le16 *cifs_convert_path_to_utf16(const char *from,
					  struct cifs_sb_info *cifs_sb);

extern int smb2_verify_signature(struct smb_rqst *, struct TCP_Server_Info *);
extern int smb2_check_receive(struct mid_q_entry *mid,
			      struct TCP_Server_Info *server, bool log_error);
extern struct mid_q_entry *smb2_setup_request(struct cifs_ses *ses,
					      struct TCP_Server_Info *,
					      struct smb_rqst *rqst);
extern struct mid_q_entry *smb2_setup_async_request(
			struct TCP_Server_Info *server, struct smb_rqst *rqst);
extern struct cifs_ses *smb2_find_smb_ses(struct TCP_Server_Info *server,
					   __u64 ses_id);
extern struct cifs_tcon *smb2_find_smb_tcon(struct TCP_Server_Info *server,
						__u64 ses_id, __u32  tid);
extern int smb2_calc_signature(struct smb_rqst *rqst,
				struct TCP_Server_Info *server,
				bool allocate_crypto);
extern int smb3_calc_signature(struct smb_rqst *rqst,
				struct TCP_Server_Info *server,
				bool allocate_crypto);
extern void smb2_echo_request(struct work_struct *work);
extern __le32 smb2_get_lease_state(struct cifsInodeInfo *cinode);
extern bool smb2_is_valid_oplock_break(char *buffer,
				       struct TCP_Server_Info *srv);
extern int smb3_handle_read_data(struct TCP_Server_Info *server,
				 struct mid_q_entry *mid);
extern int smb2_query_reparse_tag(const unsigned int xid, struct cifs_tcon *tcon,
				struct cifs_sb_info *cifs_sb, const char *path,
				__u32 *reparse_tag);
struct inode *smb2_get_reparse_inode(struct cifs_open_info_data *data,
				     struct super_block *sb,
				     const unsigned int xid,
				     struct cifs_tcon *tcon,
				     const char *full_path,
				     struct kvec *iov);
int smb2_query_path_info(const unsigned int xid,
			 struct cifs_tcon *tcon,
			 struct cifs_sb_info *cifs_sb,
			 const char *full_path,
			 struct cifs_open_info_data *data);
extern int smb2_set_path_size(const unsigned int xid, struct cifs_tcon *tcon,
			      const char *full_path, __u64 size,
			      struct cifs_sb_info *cifs_sb, bool set_alloc);
extern int smb2_set_file_info(struct inode *inode, const char *full_path,
			      FILE_BASIC_INFO *buf, const unsigned int xid);
extern int smb311_posix_mkdir(const unsigned int xid, struct inode *inode,
			       umode_t mode, struct cifs_tcon *tcon,
			       const char *full_path,
			       struct cifs_sb_info *cifs_sb);
extern int smb2_mkdir(const unsigned int xid, struct inode *inode,
		      umode_t mode, struct cifs_tcon *tcon,
		      const char *name, struct cifs_sb_info *cifs_sb);
extern void smb2_mkdir_setinfo(struct inode *inode, const char *full_path,
			       struct cifs_sb_info *cifs_sb,
			       struct cifs_tcon *tcon, const unsigned int xid);
extern int smb2_rmdir(const unsigned int xid, struct cifs_tcon *tcon,
		      const char *name, struct cifs_sb_info *cifs_sb);
extern int smb2_unlink(const unsigned int xid, struct cifs_tcon *tcon,
		       const char *name, struct cifs_sb_info *cifs_sb);
int smb2_rename_path(const unsigned int xid,
		     struct cifs_tcon *tcon,
		     struct dentry *source_dentry,
		     const char *from_name, const char *to_name,
		     struct cifs_sb_info *cifs_sb);
int smb2_create_hardlink(const unsigned int xid,
			 struct cifs_tcon *tcon,
			 struct dentry *source_dentry,
			 const char *from_name, const char *to_name,
			 struct cifs_sb_info *cifs_sb);
extern int smb3_create_mf_symlink(unsigned int xid, struct cifs_tcon *tcon,
			struct cifs_sb_info *cifs_sb, const unsigned char *path,
			char *pbuf, unsigned int *pbytes_written);
extern int smb3_query_mf_symlink(unsigned int xid, struct cifs_tcon *tcon,
			  struct cifs_sb_info *cifs_sb,
			  const unsigned char *path, char *pbuf,
			  unsigned int *pbytes_read);
int smb2_parse_symlink_response(struct cifs_sb_info *cifs_sb, const struct kvec *iov, char **path);
int smb2_open_file(const unsigned int xid, struct cifs_open_parms *oparms, __u32 *oplock,
		   void *buf);
extern int smb2_unlock_range(struct cifsFileInfo *cfile,
			     struct file_lock *flock, const unsigned int xid);
extern int smb2_push_mandatory_locks(struct cifsFileInfo *cfile);
extern void smb2_reconnect_server(struct work_struct *work);
extern int smb3_crypto_aead_allocate(struct TCP_Server_Info *server);
extern unsigned long smb_rqst_len(struct TCP_Server_Info *server,
				  struct smb_rqst *rqst);
extern void smb2_set_next_command(struct cifs_tcon *tcon,
				  struct smb_rqst *rqst);
extern void smb2_set_related(struct smb_rqst *rqst);

/*
 * SMB2 Worker functions - most of protocol specific implementation details
 * are contained within these calls.
 */
extern int SMB2_negotiate(const unsigned int xid,
			  struct cifs_ses *ses,
			  struct TCP_Server_Info *server);
extern int SMB2_sess_setup(const unsigned int xid, struct cifs_ses *ses,
			   struct TCP_Server_Info *server,
			   const struct nls_table *nls_cp);
extern int SMB2_logoff(const unsigned int xid, struct cifs_ses *ses);
extern int SMB2_tcon(const unsigned int xid, struct cifs_ses *ses,
		     const char *tree, struct cifs_tcon *tcon,
		     const struct nls_table *);
extern int SMB2_tdis(const unsigned int xid, struct cifs_tcon *tcon);
extern int SMB2_open(const unsigned int xid, struct cifs_open_parms *oparms,
		     __le16 *path, __u8 *oplock,
		     struct smb2_file_all_info *buf,
		     struct create_posix_rsp *posix,
		     struct kvec *err_iov, int *resp_buftype);
extern int SMB2_open_init(struct cifs_tcon *tcon,
			  struct TCP_Server_Info *server,
			  struct smb_rqst *rqst,
			  __u8 *oplock, struct cifs_open_parms *oparms,
			  __le16 *path);
extern void SMB2_open_free(struct smb_rqst *rqst);
extern int SMB2_ioctl(const unsigned int xid, struct cifs_tcon *tcon,
		     u64 persistent_fid, u64 volatile_fid, u32 opcode,
		     char *in_data, u32 indatalen, u32 maxoutlen,
		     char **out_data, u32 *plen /* returned data len */);
extern int SMB2_ioctl_init(struct cifs_tcon *tcon,
			   struct TCP_Server_Info *server,
			   struct smb_rqst *rqst,
			   u64 persistent_fid, u64 volatile_fid, u32 opcode,
			   char *in_data, u32 indatalen,
			   __u32 max_response_size);
extern void SMB2_ioctl_free(struct smb_rqst *rqst);
extern int SMB2_change_notify(const unsigned int xid, struct cifs_tcon *tcon,
			u64 persistent_fid, u64 volatile_fid, bool watch_tree,
			u32 completion_filter, u32 max_out_data_len,
			char **out_data, u32 *plen /* returned data len */);

extern int __SMB2_close(const unsigned int xid, struct cifs_tcon *tcon,
			u64 persistent_fid, u64 volatile_fid,
			struct smb2_file_network_open_info *pbuf);
extern int SMB2_close(const unsigned int xid, struct cifs_tcon *tcon,
		      u64 persistent_file_id, u64 volatile_file_id);
extern int SMB2_close_init(struct cifs_tcon *tcon,
			   struct TCP_Server_Info *server,
			   struct smb_rqst *rqst,
			   u64 persistent_fid, u64 volatile_fid,
			   bool query_attrs);
extern void SMB2_close_free(struct smb_rqst *rqst);
extern int SMB2_flush(const unsigned int xid, struct cifs_tcon *tcon,
		      u64 persistent_file_id, u64 volatile_file_id);
extern int SMB2_flush_init(const unsigned int xid, struct smb_rqst *rqst,
			   struct cifs_tcon *tcon,
			   struct TCP_Server_Info *server,
			   u64 persistent_file_id, u64 volatile_file_id);
extern void SMB2_flush_free(struct smb_rqst *rqst);
extern int SMB311_posix_query_info(const unsigned int xid, struct cifs_tcon *tcon,
		u64 persistent_fid, u64 volatile_fid, struct smb311_posix_qinfo *data, u32 *plen);
extern int SMB2_query_info(const unsigned int xid, struct cifs_tcon *tcon,
			   u64 persistent_file_id, u64 volatile_file_id,
			   struct smb2_file_all_info *data);
extern int SMB2_query_info_init(struct cifs_tcon *tcon,
				struct TCP_Server_Info *server,
				struct smb_rqst *rqst,
				u64 persistent_fid, u64 volatile_fid,
				u8 info_class, u8 info_type,
				u32 additional_info, size_t output_len,
				size_t input_len, void *input);
extern void SMB2_query_info_free(struct smb_rqst *rqst);
extern int SMB2_query_acl(const unsigned int xid, struct cifs_tcon *tcon,
			  u64 persistent_file_id, u64 volatile_file_id,
			  void **data, unsigned int *plen, u32 info);
extern int SMB2_get_srv_num(const unsigned int xid, struct cifs_tcon *tcon,
			    u64 persistent_fid, u64 volatile_fid,
			    __le64 *uniqueid);
extern int smb2_async_readv(struct cifs_readdata *rdata);
extern int SMB2_read(const unsigned int xid, struct cifs_io_parms *io_parms,
		     unsigned int *nbytes, char **buf, int *buf_type);
extern int smb2_async_writev(struct cifs_writedata *wdata,
			     void (*release)(struct kref *kref));
extern int SMB2_write(const unsigned int xid, struct cifs_io_parms *io_parms,
		      unsigned int *nbytes, struct kvec *iov, int n_vec);
extern int SMB2_echo(struct TCP_Server_Info *server);
extern int SMB2_query_directory(const unsigned int xid, struct cifs_tcon *tcon,
				u64 persistent_fid, u64 volatile_fid, int index,
				struct cifs_search_info *srch_inf);
extern int SMB2_query_directory_init(unsigned int xid, struct cifs_tcon *tcon,
				     struct TCP_Server_Info *server,
				     struct smb_rqst *rqst,
				     u64 persistent_fid, u64 volatile_fid,
				     int index, int info_level);
extern void SMB2_query_directory_free(struct smb_rqst *rqst);
extern int SMB2_set_eof(const unsigned int xid, struct cifs_tcon *tcon,
			u64 persistent_fid, u64 volatile_fid, u32 pid,
			__le64 *eof);
extern int SMB2_set_info_init(struct cifs_tcon *tcon,
			      struct TCP_Server_Info *server,
			      struct smb_rqst *rqst,
			      u64 persistent_fid, u64 volatile_fid, u32 pid,
			      u8 info_class, u8 info_type, u32 additional_info,
			      void **data, unsigned int *size);
extern void SMB2_set_info_free(struct smb_rqst *rqst);
extern int SMB2_set_acl(const unsigned int xid, struct cifs_tcon *tcon,
			u64 persistent_fid, u64 volatile_fid,
			struct cifs_ntsd *pnntsd, int pacllen, int aclflag);
extern int SMB2_set_ea(const unsigned int xid, struct cifs_tcon *tcon,
		       u64 persistent_fid, u64 volatile_fid,
		       struct smb2_file_full_ea_info *buf, int len);
extern int SMB2_set_compression(const unsigned int xid, struct cifs_tcon *tcon,
				u64 persistent_fid, u64 volatile_fid);
extern int SMB2_oplock_break(const unsigned int xid, struct cifs_tcon *tcon,
			     const u64 persistent_fid, const u64 volatile_fid,
			     const __u8 oplock_level);
extern int smb2_handle_cancelled_close(struct cifs_tcon *tcon,
				       __u64 persistent_fid,
				       __u64 volatile_fid);
extern int smb2_handle_cancelled_mid(struct mid_q_entry *mid, struct TCP_Server_Info *server);
void smb2_cancelled_close_fid(struct work_struct *work);
extern int SMB2_QFS_info(const unsigned int xid, struct cifs_tcon *tcon,
			 u64 persistent_file_id, u64 volatile_file_id,
			 struct kstatfs *FSData);
extern int SMB311_posix_qfs_info(const unsigned int xid, struct cifs_tcon *tcon,
			 u64 persistent_file_id, u64 volatile_file_id,
			 struct kstatfs *FSData);
extern int SMB2_QFS_attr(const unsigned int xid, struct cifs_tcon *tcon,
			 u64 persistent_file_id, u64 volatile_file_id, int lvl);
extern int SMB2_lock(const unsigned int xid, struct cifs_tcon *tcon,
		     const __u64 persist_fid, const __u64 volatile_fid,
		     const __u32 pid, const __u64 length, const __u64 offset,
		     const __u32 lockFlags, const bool wait);
extern int smb2_lockv(const unsigned int xid, struct cifs_tcon *tcon,
		      const __u64 persist_fid, const __u64 volatile_fid,
		      const __u32 pid, const __u32 num_lock,
		      struct smb2_lock_element *buf);
extern int SMB2_lease_break(const unsigned int xid, struct cifs_tcon *tcon,
			    __u8 *lease_key, const __le32 lease_state);
extern int smb3_validate_negotiate(const unsigned int, struct cifs_tcon *);

extern enum securityEnum smb2_select_sectype(struct TCP_Server_Info *,
					enum securityEnum);
int smb2_parse_contexts(struct TCP_Server_Info *server,
			struct kvec *rsp_iov,
			unsigned int *epoch,
			char *lease_key, __u8 *oplock,
			struct smb2_file_all_info *buf,
			struct create_posix_rsp *posix);

extern int smb3_encryption_required(const struct cifs_tcon *tcon);
extern int smb2_validate_iov(unsigned int offset, unsigned int buffer_length,
			     struct kvec *iov, unsigned int min_buf_size);
extern int smb2_validate_and_copy_iov(unsigned int offset,
				      unsigned int buffer_length,
				      struct kvec *iov,
				      unsigned int minbufsize, char *data);
extern void smb2_copy_fs_info_to_kstatfs(
	 struct smb2_fs_full_size_info *pfs_inf,
	 struct kstatfs *kst);
extern int smb311_crypto_shash_allocate(struct TCP_Server_Info *server);
extern int smb311_update_preauth_hash(struct cifs_ses *ses,
				      struct TCP_Server_Info *server,
				      struct kvec *iov, int nvec);
extern int smb2_query_info_compound(const unsigned int xid,
				    struct cifs_tcon *tcon,
				    const char *path, u32 desired_access,
				    u32 class, u32 type, u32 output_len,
				    struct kvec *rsp, int *buftype,
				    struct cifs_sb_info *cifs_sb);
/* query path info from the server using SMB311 POSIX extensions*/
int smb311_posix_query_path_info(const unsigned int xid,
				 struct cifs_tcon *tcon,
				 struct cifs_sb_info *cifs_sb,
				 const char *full_path,
				 struct cifs_open_info_data *data,
				 struct cifs_sid *owner,
				 struct cifs_sid *group);
int posix_info_parse(const void *beg, const void *end,
		     struct smb2_posix_info_parsed *out);
int posix_info_sid_size(const void *beg, const void *end);

#endif			/* _SMB2PROTO_H */
