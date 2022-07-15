/*
 *  fs/nfs/nfs4xdr.c
 *
 *  Client-side XDR for NFSv4.
 *
 *  Copyright (c) 2002 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Kendrick Smith <kmsmith@umich.edu>
 *  Andy Adamson   <andros@umich.edu>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/param.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/pagemap.h>
#include <linux/proc_fs.h>
#include <linux/kdev_t.h>
#include <linux/module.h>
#include <linux/utsname.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/msg_prot.h>
#include <linux/sunrpc/gss_api.h>
#include <linux/nfs.h>
#include <linux/nfs4.h>
#include <linux/nfs_fs.h>

#include "nfs4_fs.h"
#include "nfs4trace.h"
#include "internal.h"
#include "nfs4idmap.h"
#include "nfs4session.h"
#include "pnfs.h"
#include "netns.h"

#define NFSDBG_FACILITY		NFSDBG_XDR

/* Mapping from NFS error code to "errno" error code. */
#define errno_NFSERR_IO		EIO

struct compound_hdr;
static int nfs4_stat_to_errno(int);
static void encode_layoutget(struct xdr_stream *xdr,
			     const struct nfs4_layoutget_args *args,
			     struct compound_hdr *hdr);
static int decode_layoutget(struct xdr_stream *xdr, struct rpc_rqst *req,
			     struct nfs4_layoutget_res *res);

/* NFSv4 COMPOUND tags are only wanted for debugging purposes */
#ifdef DEBUG
#define NFS4_MAXTAGLEN		20
#else
#define NFS4_MAXTAGLEN		0
#endif

/* lock,open owner id:
 * we currently use size 2 (u64) out of (NFS4_OPAQUE_LIMIT  >> 2)
 */
#define pagepad_maxsz		(1)
#define open_owner_id_maxsz	(1 + 2 + 1 + 1 + 2)
#define lock_owner_id_maxsz	(1 + 1 + 4)
#define decode_lockowner_maxsz	(1 + XDR_QUADLEN(IDMAP_NAMESZ))
#define compound_encode_hdr_maxsz	(3 + (NFS4_MAXTAGLEN >> 2))
#define compound_decode_hdr_maxsz	(3 + (NFS4_MAXTAGLEN >> 2))
#define op_encode_hdr_maxsz	(1)
#define op_decode_hdr_maxsz	(2)
#define encode_stateid_maxsz	(XDR_QUADLEN(NFS4_STATEID_SIZE))
#define decode_stateid_maxsz	(XDR_QUADLEN(NFS4_STATEID_SIZE))
#define encode_verifier_maxsz	(XDR_QUADLEN(NFS4_VERIFIER_SIZE))
#define decode_verifier_maxsz	(XDR_QUADLEN(NFS4_VERIFIER_SIZE))
#define encode_putfh_maxsz	(op_encode_hdr_maxsz + 1 + \
				(NFS4_FHSIZE >> 2))
#define decode_putfh_maxsz	(op_decode_hdr_maxsz)
#define encode_putrootfh_maxsz	(op_encode_hdr_maxsz)
#define decode_putrootfh_maxsz	(op_decode_hdr_maxsz)
#define encode_getfh_maxsz      (op_encode_hdr_maxsz)
#define decode_getfh_maxsz      (op_decode_hdr_maxsz + 1 + \
				((3+NFS4_FHSIZE) >> 2))
#define nfs4_fattr_bitmap_maxsz 4
#define encode_getattr_maxsz    (op_encode_hdr_maxsz + nfs4_fattr_bitmap_maxsz)
#define nfstime4_maxsz		(3)
#define nfs4_name_maxsz		(1 + ((3 + NFS4_MAXNAMLEN) >> 2))
#define nfs4_path_maxsz		(1 + ((3 + NFS4_MAXPATHLEN) >> 2))
#define nfs4_owner_maxsz	(1 + XDR_QUADLEN(IDMAP_NAMESZ))
#define nfs4_group_maxsz	(1 + XDR_QUADLEN(IDMAP_NAMESZ))
#ifdef CONFIG_NFS_V4_SECURITY_LABEL
/* PI(4 bytes) + LFS(4 bytes) + 1(for null terminator?) + MAXLABELLEN */
#define	nfs4_label_maxsz	(4 + 4 + 1 + XDR_QUADLEN(NFS4_MAXLABELLEN))
#else
#define	nfs4_label_maxsz	0
#endif
/* We support only one layout type per file system */
#define decode_mdsthreshold_maxsz (1 + 1 + nfs4_fattr_bitmap_maxsz + 1 + 8)
/* This is based on getfattr, which uses the most attributes: */
#define nfs4_fattr_value_maxsz	(1 + (1 + 2 + 2 + 4 + 2 + 1 + 1 + 2 + 2 + \
				3*nfstime4_maxsz + \
				nfs4_owner_maxsz + \
				nfs4_group_maxsz + nfs4_label_maxsz + \
				 decode_mdsthreshold_maxsz))
#define nfs4_fattr_maxsz	(nfs4_fattr_bitmap_maxsz + \
				nfs4_fattr_value_maxsz)
#define decode_getattr_maxsz    (op_decode_hdr_maxsz + nfs4_fattr_maxsz)
#define encode_attrs_maxsz	(nfs4_fattr_bitmap_maxsz + \
				 1 + 2 + 1 + \
				nfs4_owner_maxsz + \
				nfs4_group_maxsz + \
				nfs4_label_maxsz + \
				1 + nfstime4_maxsz + \
				1 + nfstime4_maxsz)
#define encode_savefh_maxsz     (op_encode_hdr_maxsz)
#define decode_savefh_maxsz     (op_decode_hdr_maxsz)
#define encode_restorefh_maxsz  (op_encode_hdr_maxsz)
#define decode_restorefh_maxsz  (op_decode_hdr_maxsz)
#define encode_fsinfo_maxsz	(encode_getattr_maxsz)
/* The 5 accounts for the PNFS attributes, and assumes that at most three
 * layout types will be returned.
 */
#define decode_fsinfo_maxsz	(op_decode_hdr_maxsz + \
				 nfs4_fattr_bitmap_maxsz + 1 + \
				 1 /* lease time */ + \
				 2 /* max filesize */ + \
				 2 /* max read */ + \
				 2 /* max write */ + \
				 nfstime4_maxsz /* time delta */ + \
				 5 /* fs layout types */ + \
				 1 /* layout blksize */ + \
				 1 /* clone blksize */ + \
				 1 /* change attr type */ + \
				 1 /* xattr support */)
#define encode_renew_maxsz	(op_encode_hdr_maxsz + 3)
#define decode_renew_maxsz	(op_decode_hdr_maxsz)
#define encode_setclientid_maxsz \
				(op_encode_hdr_maxsz + \
				XDR_QUADLEN(NFS4_VERIFIER_SIZE) + \
				/* client name */ \
				1 + XDR_QUADLEN(NFS4_OPAQUE_LIMIT) + \
				1 /* sc_prog */ + \
				1 + XDR_QUADLEN(RPCBIND_MAXNETIDLEN) + \
				1 + XDR_QUADLEN(RPCBIND_MAXUADDRLEN) + \
				1) /* sc_cb_ident */
#define decode_setclientid_maxsz \
				(op_decode_hdr_maxsz + \
				2 /* clientid */ + \
				XDR_QUADLEN(NFS4_VERIFIER_SIZE) + \
				1 + XDR_QUADLEN(RPCBIND_MAXNETIDLEN) + \
				1 + XDR_QUADLEN(RPCBIND_MAXUADDRLEN))
#define encode_setclientid_confirm_maxsz \
				(op_encode_hdr_maxsz + \
				3 + (NFS4_VERIFIER_SIZE >> 2))
#define decode_setclientid_confirm_maxsz \
				(op_decode_hdr_maxsz)
#define encode_lookup_maxsz	(op_encode_hdr_maxsz + nfs4_name_maxsz)
#define decode_lookup_maxsz	(op_decode_hdr_maxsz)
#define encode_lookupp_maxsz	(op_encode_hdr_maxsz)
#define decode_lookupp_maxsz	(op_decode_hdr_maxsz)
#define encode_share_access_maxsz \
				(2)
#define encode_createmode_maxsz	(1 + encode_attrs_maxsz + encode_verifier_maxsz)
#define encode_opentype_maxsz	(1 + encode_createmode_maxsz)
#define encode_claim_null_maxsz	(1 + nfs4_name_maxsz)
#define encode_open_maxsz	(op_encode_hdr_maxsz + \
				2 + encode_share_access_maxsz + 2 + \
				open_owner_id_maxsz + \
				encode_opentype_maxsz + \
				encode_claim_null_maxsz)
#define decode_space_limit_maxsz	(3)
#define decode_ace_maxsz	(3 + nfs4_owner_maxsz)
#define decode_delegation_maxsz	(1 + decode_stateid_maxsz + 1 + \
				decode_space_limit_maxsz + \
				decode_ace_maxsz)
#define decode_change_info_maxsz	(5)
#define decode_open_maxsz	(op_decode_hdr_maxsz + \
				decode_stateid_maxsz + \
				decode_change_info_maxsz + 1 + \
				nfs4_fattr_bitmap_maxsz + \
				decode_delegation_maxsz)
#define encode_open_confirm_maxsz \
				(op_encode_hdr_maxsz + \
				 encode_stateid_maxsz + 1)
#define decode_open_confirm_maxsz \
				(op_decode_hdr_maxsz + \
				 decode_stateid_maxsz)
#define encode_open_downgrade_maxsz \
				(op_encode_hdr_maxsz + \
				 encode_stateid_maxsz + 1 + \
				 encode_share_access_maxsz)
#define decode_open_downgrade_maxsz \
				(op_decode_hdr_maxsz + \
				 decode_stateid_maxsz)
#define encode_close_maxsz	(op_encode_hdr_maxsz + \
				 1 + encode_stateid_maxsz)
#define decode_close_maxsz	(op_decode_hdr_maxsz + \
				 decode_stateid_maxsz)
#define encode_setattr_maxsz	(op_encode_hdr_maxsz + \
				 encode_stateid_maxsz + \
				 encode_attrs_maxsz)
#define decode_setattr_maxsz	(op_decode_hdr_maxsz + \
				 nfs4_fattr_bitmap_maxsz)
#define encode_read_maxsz	(op_encode_hdr_maxsz + \
				 encode_stateid_maxsz + 3)
#define decode_read_maxsz	(op_decode_hdr_maxsz + 2 + pagepad_maxsz)
#define encode_readdir_maxsz	(op_encode_hdr_maxsz + \
				 2 + encode_verifier_maxsz + 5 + \
				nfs4_label_maxsz)
#define decode_readdir_maxsz	(op_decode_hdr_maxsz + \
				 decode_verifier_maxsz + pagepad_maxsz)
#define encode_readlink_maxsz	(op_encode_hdr_maxsz)
#define decode_readlink_maxsz	(op_decode_hdr_maxsz + 1 + pagepad_maxsz)
#define encode_write_maxsz	(op_encode_hdr_maxsz + \
				 encode_stateid_maxsz + 4)
#define decode_write_maxsz	(op_decode_hdr_maxsz + \
				 2 + decode_verifier_maxsz)
#define encode_commit_maxsz	(op_encode_hdr_maxsz + 3)
#define decode_commit_maxsz	(op_decode_hdr_maxsz + \
				 decode_verifier_maxsz)
#define encode_remove_maxsz	(op_encode_hdr_maxsz + \
				nfs4_name_maxsz)
#define decode_remove_maxsz	(op_decode_hdr_maxsz + \
				 decode_change_info_maxsz)
#define encode_rename_maxsz	(op_encode_hdr_maxsz + \
				2 * nfs4_name_maxsz)
#define decode_rename_maxsz	(op_decode_hdr_maxsz + \
				 decode_change_info_maxsz + \
				 decode_change_info_maxsz)
#define encode_link_maxsz	(op_encode_hdr_maxsz + \
				nfs4_name_maxsz)
#define decode_link_maxsz	(op_decode_hdr_maxsz + decode_change_info_maxsz)
#define encode_lockowner_maxsz	(7)
#define encode_lock_maxsz	(op_encode_hdr_maxsz + \
				 7 + \
				 1 + encode_stateid_maxsz + 1 + \
				 encode_lockowner_maxsz)
#define decode_lock_denied_maxsz \
				(8 + decode_lockowner_maxsz)
#define decode_lock_maxsz	(op_decode_hdr_maxsz + \
				 decode_lock_denied_maxsz)
#define encode_lockt_maxsz	(op_encode_hdr_maxsz + 5 + \
				encode_lockowner_maxsz)
#define decode_lockt_maxsz	(op_decode_hdr_maxsz + \
				 decode_lock_denied_maxsz)
#define encode_locku_maxsz	(op_encode_hdr_maxsz + 3 + \
				 encode_stateid_maxsz + \
				 4)
#define decode_locku_maxsz	(op_decode_hdr_maxsz + \
				 decode_stateid_maxsz)
#define encode_release_lockowner_maxsz \
				(op_encode_hdr_maxsz + \
				 encode_lockowner_maxsz)
#define decode_release_lockowner_maxsz \
				(op_decode_hdr_maxsz)
#define encode_access_maxsz	(op_encode_hdr_maxsz + 1)
#define decode_access_maxsz	(op_decode_hdr_maxsz + 2)
#define encode_symlink_maxsz	(op_encode_hdr_maxsz + \
				1 + nfs4_name_maxsz + \
				1 + \
				nfs4_fattr_maxsz)
#define decode_symlink_maxsz	(op_decode_hdr_maxsz + 8)
#define encode_create_maxsz	(op_encode_hdr_maxsz + \
				1 + 2 + nfs4_name_maxsz + \
				encode_attrs_maxsz)
#define decode_create_maxsz	(op_decode_hdr_maxsz + \
				decode_change_info_maxsz + \
				nfs4_fattr_bitmap_maxsz)
#define encode_statfs_maxsz	(encode_getattr_maxsz)
#define decode_statfs_maxsz	(decode_getattr_maxsz)
#define encode_delegreturn_maxsz (op_encode_hdr_maxsz + 4)
#define decode_delegreturn_maxsz (op_decode_hdr_maxsz)
#define encode_getacl_maxsz	(encode_getattr_maxsz)
#define decode_getacl_maxsz	(op_decode_hdr_maxsz + \
				 nfs4_fattr_bitmap_maxsz + 1 + pagepad_maxsz)
#define encode_setacl_maxsz	(op_encode_hdr_maxsz + \
				 encode_stateid_maxsz + 3)
#define decode_setacl_maxsz	(decode_setattr_maxsz)
#define encode_fs_locations_maxsz \
				(encode_getattr_maxsz)
#define decode_fs_locations_maxsz \
				(pagepad_maxsz)
#define encode_secinfo_maxsz	(op_encode_hdr_maxsz + nfs4_name_maxsz)
#define decode_secinfo_maxsz	(op_decode_hdr_maxsz + 1 + ((NFS_MAX_SECFLAVORS * (16 + GSS_OID_MAX_LEN)) / 4))

#if defined(CONFIG_NFS_V4_1)
#define NFS4_MAX_MACHINE_NAME_LEN (64)
#define IMPL_NAME_LIMIT (sizeof(utsname()->sysname) + sizeof(utsname()->release) + \
			 sizeof(utsname()->version) + sizeof(utsname()->machine) + 8)

#define encode_exchange_id_maxsz (op_encode_hdr_maxsz + \
				encode_verifier_maxsz + \
				1 /* co_ownerid.len */ + \
				/* eia_clientowner */ \
				1 + XDR_QUADLEN(NFS4_OPAQUE_LIMIT) + \
				1 /* flags */ + \
				1 /* spa_how */ + \
				/* max is SP4_MACH_CRED (for now) */ + \
				1 + NFS4_OP_MAP_NUM_WORDS + \
				1 + NFS4_OP_MAP_NUM_WORDS + \
				1 /* implementation id array of size 1 */ + \
				1 /* nii_domain */ + \
				XDR_QUADLEN(NFS4_OPAQUE_LIMIT) + \
				1 /* nii_name */ + \
				XDR_QUADLEN(IMPL_NAME_LIMIT) + \
				3 /* nii_date */)
#define decode_exchange_id_maxsz (op_decode_hdr_maxsz + \
				2 /* eir_clientid */ + \
				1 /* eir_sequenceid */ + \
				1 /* eir_flags */ + \
				1 /* spr_how */ + \
				  /* max is SP4_MACH_CRED (for now) */ + \
				1 + NFS4_OP_MAP_NUM_WORDS + \
				1 + NFS4_OP_MAP_NUM_WORDS + \
				2 /* eir_server_owner.so_minor_id */ + \
				/* eir_server_owner.so_major_id<> */ \
				XDR_QUADLEN(NFS4_OPAQUE_LIMIT) + 1 + \
				/* eir_server_scope<> */ \
				XDR_QUADLEN(NFS4_OPAQUE_LIMIT) + 1 + \
				1 /* eir_server_impl_id array length */ + \
				1 /* nii_domain */ + \
				XDR_QUADLEN(NFS4_OPAQUE_LIMIT) + \
				1 /* nii_name */ + \
				XDR_QUADLEN(NFS4_OPAQUE_LIMIT) + \
				3 /* nii_date */)
#define encode_channel_attrs_maxsz  (6 + 1 /* ca_rdma_ird.len (0) */)
#define decode_channel_attrs_maxsz  (6 + \
				     1 /* ca_rdma_ird.len */ + \
				     1 /* ca_rdma_ird */)
#define encode_create_session_maxsz  (op_encode_hdr_maxsz + \
				     2 /* csa_clientid */ + \
				     1 /* csa_sequence */ + \
				     1 /* csa_flags */ + \
				     encode_channel_attrs_maxsz + \
				     encode_channel_attrs_maxsz + \
				     1 /* csa_cb_program */ + \
				     1 /* csa_sec_parms.len (1) */ + \
				     1 /* cb_secflavor (AUTH_SYS) */ + \
				     1 /* stamp */ + \
				     1 /* machinename.len */ + \
				     XDR_QUADLEN(NFS4_MAX_MACHINE_NAME_LEN) + \
				     1 /* uid */ + \
				     1 /* gid */ + \
				     1 /* gids.len (0) */)
#define decode_create_session_maxsz  (op_decode_hdr_maxsz +	\
				     XDR_QUADLEN(NFS4_MAX_SESSIONID_LEN) + \
				     1 /* csr_sequence */ + \
				     1 /* csr_flags */ + \
				     decode_channel_attrs_maxsz + \
				     decode_channel_attrs_maxsz)
#define encode_bind_conn_to_session_maxsz  (op_encode_hdr_maxsz + \
				     /* bctsa_sessid */ \
				     XDR_QUADLEN(NFS4_MAX_SESSIONID_LEN) + \
				     1 /* bctsa_dir */ + \
				     1 /* bctsa_use_conn_in_rdma_mode */)
#define decode_bind_conn_to_session_maxsz  (op_decode_hdr_maxsz +	\
				     /* bctsr_sessid */ \
				     XDR_QUADLEN(NFS4_MAX_SESSIONID_LEN) + \
				     1 /* bctsr_dir */ + \
				     1 /* bctsr_use_conn_in_rdma_mode */)
#define encode_destroy_session_maxsz    (op_encode_hdr_maxsz + 4)
#define decode_destroy_session_maxsz    (op_decode_hdr_maxsz)
#define encode_destroy_clientid_maxsz   (op_encode_hdr_maxsz + 2)
#define decode_destroy_clientid_maxsz   (op_decode_hdr_maxsz)
#define encode_sequence_maxsz	(op_encode_hdr_maxsz + \
				XDR_QUADLEN(NFS4_MAX_SESSIONID_LEN) + 4)
#define decode_sequence_maxsz	(op_decode_hdr_maxsz + \
				XDR_QUADLEN(NFS4_MAX_SESSIONID_LEN) + 5)
#define encode_reclaim_complete_maxsz	(op_encode_hdr_maxsz + 4)
#define decode_reclaim_complete_maxsz	(op_decode_hdr_maxsz + 4)
#define encode_getdeviceinfo_maxsz (op_encode_hdr_maxsz + \
				XDR_QUADLEN(NFS4_DEVICEID4_SIZE) + \
				1 /* layout type */ + \
				1 /* maxcount */ + \
				1 /* bitmap size */ + \
				1 /* notification bitmap length */ + \
				1 /* notification bitmap, word 0 */)
#define decode_getdeviceinfo_maxsz (op_decode_hdr_maxsz + \
				1 /* layout type */ + \
				1 /* opaque devaddr4 length */ + \
				  /* devaddr4 payload is read into page */ \
				1 /* notification bitmap length */ + \
				1 /* notification bitmap, word 0 */ + \
				pagepad_maxsz /* possible XDR padding */)
#define encode_layoutget_maxsz	(op_encode_hdr_maxsz + 10 + \
				encode_stateid_maxsz)
#define decode_layoutget_maxsz	(op_decode_hdr_maxsz + 8 + \
				decode_stateid_maxsz + \
				XDR_QUADLEN(PNFS_LAYOUT_MAXSIZE) + \
				pagepad_maxsz)
#define encode_layoutcommit_maxsz (op_encode_hdr_maxsz +          \
				2 /* offset */ + \
				2 /* length */ + \
				1 /* reclaim */ + \
				encode_stateid_maxsz + \
				1 /* new offset (true) */ + \
				2 /* last byte written */ + \
				1 /* nt_timechanged (false) */ + \
				1 /* layoutupdate4 layout type */ + \
				1 /* layoutupdate4 opaqueue len */)
				  /* the actual content of layoutupdate4 should
				     be allocated by drivers and spliced in
				     using xdr_write_pages */
#define decode_layoutcommit_maxsz (op_decode_hdr_maxsz + 3)
#define encode_layoutreturn_maxsz (8 + op_encode_hdr_maxsz + \
				encode_stateid_maxsz + \
				1 + \
				XDR_QUADLEN(NFS4_OPAQUE_LIMIT))
#define decode_layoutreturn_maxsz (op_decode_hdr_maxsz + \
				1 + decode_stateid_maxsz)
#define encode_secinfo_no_name_maxsz (op_encode_hdr_maxsz + 1)
#define decode_secinfo_no_name_maxsz decode_secinfo_maxsz
#define encode_test_stateid_maxsz	(op_encode_hdr_maxsz + 2 + \
					 XDR_QUADLEN(NFS4_STATEID_SIZE))
#define decode_test_stateid_maxsz	(op_decode_hdr_maxsz + 2 + 1)
#define encode_free_stateid_maxsz	(op_encode_hdr_maxsz + 1 + \
					 XDR_QUADLEN(NFS4_STATEID_SIZE))
#define decode_free_stateid_maxsz	(op_decode_hdr_maxsz)
#else /* CONFIG_NFS_V4_1 */
#define encode_sequence_maxsz	0
#define decode_sequence_maxsz	0
#define encode_layoutreturn_maxsz 0
#define decode_layoutreturn_maxsz 0
#define encode_layoutget_maxsz	0
#define decode_layoutget_maxsz	0
#endif /* CONFIG_NFS_V4_1 */

#define NFS4_enc_compound_sz	(1024)  /* XXX: large enough? */
#define NFS4_dec_compound_sz	(1024)  /* XXX: large enough? */
#define NFS4_enc_read_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_read_maxsz)
#define NFS4_dec_read_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_read_maxsz)
#define NFS4_enc_readlink_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_readlink_maxsz)
#define NFS4_dec_readlink_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_readlink_maxsz)
#define NFS4_enc_readdir_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_readdir_maxsz)
#define NFS4_dec_readdir_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_readdir_maxsz)
#define NFS4_enc_write_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_write_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_write_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_write_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_commit_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_commit_maxsz)
#define NFS4_dec_commit_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_commit_maxsz)
#define NFS4_enc_open_sz        (compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_open_maxsz + \
				encode_access_maxsz + \
				encode_getfh_maxsz + \
				encode_getattr_maxsz + \
				encode_layoutget_maxsz)
#define NFS4_dec_open_sz        (compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_open_maxsz + \
				decode_access_maxsz + \
				decode_getfh_maxsz + \
				decode_getattr_maxsz + \
				decode_layoutget_maxsz)
#define NFS4_enc_open_confirm_sz \
				(compound_encode_hdr_maxsz + \
				 encode_putfh_maxsz + \
				 encode_open_confirm_maxsz)
#define NFS4_dec_open_confirm_sz \
				(compound_decode_hdr_maxsz + \
				 decode_putfh_maxsz + \
				 decode_open_confirm_maxsz)
#define NFS4_enc_open_noattr_sz	(compound_encode_hdr_maxsz + \
					encode_sequence_maxsz + \
					encode_putfh_maxsz + \
					encode_open_maxsz + \
					encode_access_maxsz + \
					encode_getattr_maxsz + \
					encode_layoutget_maxsz)
#define NFS4_dec_open_noattr_sz	(compound_decode_hdr_maxsz + \
					decode_sequence_maxsz + \
					decode_putfh_maxsz + \
					decode_open_maxsz + \
					decode_access_maxsz + \
					decode_getattr_maxsz + \
					decode_layoutget_maxsz)
#define NFS4_enc_open_downgrade_sz \
				(compound_encode_hdr_maxsz + \
				 encode_sequence_maxsz + \
				 encode_putfh_maxsz + \
				 encode_layoutreturn_maxsz + \
				 encode_open_downgrade_maxsz)
#define NFS4_dec_open_downgrade_sz \
				(compound_decode_hdr_maxsz + \
				 decode_sequence_maxsz + \
				 decode_putfh_maxsz + \
				 decode_layoutreturn_maxsz + \
				 decode_open_downgrade_maxsz)
#define NFS4_enc_close_sz	(compound_encode_hdr_maxsz + \
				 encode_sequence_maxsz + \
				 encode_putfh_maxsz + \
				 encode_layoutreturn_maxsz + \
				 encode_close_maxsz + \
				 encode_getattr_maxsz)
#define NFS4_dec_close_sz	(compound_decode_hdr_maxsz + \
				 decode_sequence_maxsz + \
				 decode_putfh_maxsz + \
				 decode_layoutreturn_maxsz + \
				 decode_close_maxsz + \
				 decode_getattr_maxsz)
#define NFS4_enc_setattr_sz	(compound_encode_hdr_maxsz + \
				 encode_sequence_maxsz + \
				 encode_putfh_maxsz + \
				 encode_setattr_maxsz + \
				 encode_getattr_maxsz)
#define NFS4_dec_setattr_sz	(compound_decode_hdr_maxsz + \
				 decode_sequence_maxsz + \
				 decode_putfh_maxsz + \
				 decode_setattr_maxsz + \
				 decode_getattr_maxsz)
#define NFS4_enc_fsinfo_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_fsinfo_maxsz)
#define NFS4_dec_fsinfo_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_fsinfo_maxsz)
#define NFS4_enc_renew_sz	(compound_encode_hdr_maxsz + \
				encode_renew_maxsz)
#define NFS4_dec_renew_sz	(compound_decode_hdr_maxsz + \
				decode_renew_maxsz)
#define NFS4_enc_setclientid_sz	(compound_encode_hdr_maxsz + \
				encode_setclientid_maxsz)
#define NFS4_dec_setclientid_sz	(compound_decode_hdr_maxsz + \
				decode_setclientid_maxsz)
#define NFS4_enc_setclientid_confirm_sz \
				(compound_encode_hdr_maxsz + \
				encode_setclientid_confirm_maxsz)
#define NFS4_dec_setclientid_confirm_sz \
				(compound_decode_hdr_maxsz + \
				decode_setclientid_confirm_maxsz)
#define NFS4_enc_lock_sz        (compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_lock_maxsz)
#define NFS4_dec_lock_sz        (compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_lock_maxsz)
#define NFS4_enc_lockt_sz       (compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_lockt_maxsz)
#define NFS4_dec_lockt_sz       (compound_decode_hdr_maxsz + \
				 decode_sequence_maxsz + \
				 decode_putfh_maxsz + \
				 decode_lockt_maxsz)
#define NFS4_enc_locku_sz       (compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_locku_maxsz)
#define NFS4_dec_locku_sz       (compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_locku_maxsz)
#define NFS4_enc_release_lockowner_sz \
				(compound_encode_hdr_maxsz + \
				 encode_lockowner_maxsz)
#define NFS4_dec_release_lockowner_sz \
				(compound_decode_hdr_maxsz + \
				 decode_lockowner_maxsz)
#define NFS4_enc_access_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_access_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_access_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_access_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_getattr_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_getattr_maxsz + \
				encode_renew_maxsz)
#define NFS4_dec_getattr_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_getattr_maxsz + \
				decode_renew_maxsz)
#define NFS4_enc_lookup_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_lookup_maxsz + \
				encode_getattr_maxsz + \
				encode_getfh_maxsz)
#define NFS4_dec_lookup_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_lookup_maxsz + \
				decode_getattr_maxsz + \
				decode_getfh_maxsz)
#define NFS4_enc_lookupp_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_lookupp_maxsz + \
				encode_getattr_maxsz + \
				encode_getfh_maxsz)
#define NFS4_dec_lookupp_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_lookupp_maxsz + \
				decode_getattr_maxsz + \
				decode_getfh_maxsz)
#define NFS4_enc_lookup_root_sz (compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putrootfh_maxsz + \
				encode_getattr_maxsz + \
				encode_getfh_maxsz)
#define NFS4_dec_lookup_root_sz (compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putrootfh_maxsz + \
				decode_getattr_maxsz + \
				decode_getfh_maxsz)
#define NFS4_enc_remove_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_remove_maxsz)
#define NFS4_dec_remove_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_remove_maxsz)
#define NFS4_enc_rename_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_savefh_maxsz + \
				encode_putfh_maxsz + \
				encode_rename_maxsz)
#define NFS4_dec_rename_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_savefh_maxsz + \
				decode_putfh_maxsz + \
				decode_rename_maxsz)
#define NFS4_enc_link_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_savefh_maxsz + \
				encode_putfh_maxsz + \
				encode_link_maxsz + \
				encode_restorefh_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_link_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_savefh_maxsz + \
				decode_putfh_maxsz + \
				decode_link_maxsz + \
				decode_restorefh_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_symlink_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_symlink_maxsz + \
				encode_getattr_maxsz + \
				encode_getfh_maxsz)
#define NFS4_dec_symlink_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_symlink_maxsz + \
				decode_getattr_maxsz + \
				decode_getfh_maxsz)
#define NFS4_enc_create_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_create_maxsz + \
				encode_getfh_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_create_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_create_maxsz + \
				decode_getfh_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_pathconf_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_pathconf_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_statfs_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_statfs_maxsz)
#define NFS4_dec_statfs_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_statfs_maxsz)
#define NFS4_enc_server_caps_sz (compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_server_caps_sz (compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_delegreturn_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_layoutreturn_maxsz + \
				encode_delegreturn_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_delegreturn_sz (compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_layoutreturn_maxsz + \
				decode_delegreturn_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_getacl_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_getacl_maxsz)
#define NFS4_dec_getacl_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_getacl_maxsz)
#define NFS4_enc_setacl_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_setacl_maxsz)
#define NFS4_dec_setacl_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_setacl_maxsz)
#define NFS4_enc_fs_locations_sz \
				(compound_encode_hdr_maxsz + \
				 encode_sequence_maxsz + \
				 encode_putfh_maxsz + \
				 encode_lookup_maxsz + \
				 encode_fs_locations_maxsz + \
				 encode_renew_maxsz)
#define NFS4_dec_fs_locations_sz \
				(compound_decode_hdr_maxsz + \
				 decode_sequence_maxsz + \
				 decode_putfh_maxsz + \
				 decode_lookup_maxsz + \
				 decode_fs_locations_maxsz + \
				 decode_renew_maxsz)
#define NFS4_enc_secinfo_sz 	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_secinfo_maxsz)
#define NFS4_dec_secinfo_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_secinfo_maxsz)
#define NFS4_enc_fsid_present_sz \
				(compound_encode_hdr_maxsz + \
				 encode_sequence_maxsz + \
				 encode_putfh_maxsz + \
				 encode_getfh_maxsz + \
				 encode_renew_maxsz)
#define NFS4_dec_fsid_present_sz \
				(compound_decode_hdr_maxsz + \
				 decode_sequence_maxsz + \
				 decode_putfh_maxsz + \
				 decode_getfh_maxsz + \
				 decode_renew_maxsz)
#if defined(CONFIG_NFS_V4_1)
#define NFS4_enc_bind_conn_to_session_sz \
				(compound_encode_hdr_maxsz + \
				 encode_bind_conn_to_session_maxsz)
#define NFS4_dec_bind_conn_to_session_sz \
				(compound_decode_hdr_maxsz + \
				 decode_bind_conn_to_session_maxsz)
#define NFS4_enc_exchange_id_sz \
				(compound_encode_hdr_maxsz + \
				 encode_exchange_id_maxsz)
#define NFS4_dec_exchange_id_sz \
				(compound_decode_hdr_maxsz + \
				 decode_exchange_id_maxsz)
#define NFS4_enc_create_session_sz \
				(compound_encode_hdr_maxsz + \
				 encode_create_session_maxsz)
#define NFS4_dec_create_session_sz \
				(compound_decode_hdr_maxsz + \
				 decode_create_session_maxsz)
#define NFS4_enc_destroy_session_sz	(compound_encode_hdr_maxsz + \
					 encode_destroy_session_maxsz)
#define NFS4_dec_destroy_session_sz	(compound_decode_hdr_maxsz + \
					 decode_destroy_session_maxsz)
#define NFS4_enc_destroy_clientid_sz	(compound_encode_hdr_maxsz + \
					 encode_destroy_clientid_maxsz)
#define NFS4_dec_destroy_clientid_sz	(compound_decode_hdr_maxsz + \
					 decode_destroy_clientid_maxsz)
#define NFS4_enc_sequence_sz \
				(compound_decode_hdr_maxsz + \
				 encode_sequence_maxsz)
#define NFS4_dec_sequence_sz \
				(compound_decode_hdr_maxsz + \
				 decode_sequence_maxsz)
#endif
#define NFS4_enc_get_lease_time_sz	(compound_encode_hdr_maxsz + \
					 encode_sequence_maxsz + \
					 encode_putrootfh_maxsz + \
					 encode_fsinfo_maxsz)
#define NFS4_dec_get_lease_time_sz	(compound_decode_hdr_maxsz + \
					 decode_sequence_maxsz + \
					 decode_putrootfh_maxsz + \
					 decode_fsinfo_maxsz)
#if defined(CONFIG_NFS_V4_1)
#define NFS4_enc_reclaim_complete_sz	(compound_encode_hdr_maxsz + \
					 encode_sequence_maxsz + \
					 encode_reclaim_complete_maxsz)
#define NFS4_dec_reclaim_complete_sz	(compound_decode_hdr_maxsz + \
					 decode_sequence_maxsz + \
					 decode_reclaim_complete_maxsz)
#define NFS4_enc_getdeviceinfo_sz (compound_encode_hdr_maxsz +    \
				encode_sequence_maxsz +\
				encode_getdeviceinfo_maxsz)
#define NFS4_dec_getdeviceinfo_sz (compound_decode_hdr_maxsz +    \
				decode_sequence_maxsz + \
				decode_getdeviceinfo_maxsz)
#define NFS4_enc_layoutget_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz +        \
				encode_layoutget_maxsz)
#define NFS4_dec_layoutget_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz +        \
				decode_layoutget_maxsz)
#define NFS4_enc_layoutcommit_sz (compound_encode_hdr_maxsz + \
				encode_sequence_maxsz +\
				encode_putfh_maxsz + \
				encode_layoutcommit_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_layoutcommit_sz (compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_layoutcommit_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_layoutreturn_sz (compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_layoutreturn_maxsz)
#define NFS4_dec_layoutreturn_sz (compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_layoutreturn_maxsz)
#define NFS4_enc_secinfo_no_name_sz	(compound_encode_hdr_maxsz + \
					encode_sequence_maxsz + \
					encode_putrootfh_maxsz +\
					encode_secinfo_no_name_maxsz)
#define NFS4_dec_secinfo_no_name_sz	(compound_decode_hdr_maxsz + \
					decode_sequence_maxsz + \
					decode_putrootfh_maxsz + \
					decode_secinfo_no_name_maxsz)
#define NFS4_enc_test_stateid_sz	(compound_encode_hdr_maxsz + \
					 encode_sequence_maxsz + \
					 encode_test_stateid_maxsz)
#define NFS4_dec_test_stateid_sz	(compound_decode_hdr_maxsz + \
					 decode_sequence_maxsz + \
					 decode_test_stateid_maxsz)
#define NFS4_enc_free_stateid_sz	(compound_encode_hdr_maxsz + \
					 encode_sequence_maxsz + \
					 encode_free_stateid_maxsz)
#define NFS4_dec_free_stateid_sz	(compound_decode_hdr_maxsz + \
					 decode_sequence_maxsz + \
					 decode_free_stateid_maxsz)

const u32 nfs41_maxwrite_overhead = ((RPC_MAX_HEADER_WITH_AUTH +
				      compound_encode_hdr_maxsz +
				      encode_sequence_maxsz +
				      encode_putfh_maxsz +
				      encode_getattr_maxsz) *
				     XDR_UNIT);

const u32 nfs41_maxread_overhead = ((RPC_MAX_HEADER_WITH_AUTH +
				     compound_decode_hdr_maxsz +
				     decode_sequence_maxsz +
				     decode_putfh_maxsz) *
				    XDR_UNIT);

const u32 nfs41_maxgetdevinfo_overhead = ((RPC_MAX_REPHEADER_WITH_AUTH +
					   compound_decode_hdr_maxsz +
					   decode_sequence_maxsz) *
					  XDR_UNIT);
EXPORT_SYMBOL_GPL(nfs41_maxgetdevinfo_overhead);
#endif /* CONFIG_NFS_V4_1 */

static const umode_t nfs_type2fmt[] = {
	[NF4BAD] = 0,
	[NF4REG] = S_IFREG,
	[NF4DIR] = S_IFDIR,
	[NF4BLK] = S_IFBLK,
	[NF4CHR] = S_IFCHR,
	[NF4LNK] = S_IFLNK,
	[NF4SOCK] = S_IFSOCK,
	[NF4FIFO] = S_IFIFO,
	[NF4ATTRDIR] = 0,
	[NF4NAMEDATTR] = 0,
};

struct compound_hdr {
	int32_t		status;
	uint32_t	nops;
	__be32 *	nops_p;
	uint32_t	taglen;
	char *		tag;
	uint32_t	replen;		/* expected reply words */
	u32		minorversion;
};

static __be32 *reserve_space(struct xdr_stream *xdr, size_t nbytes)
{
	__be32 *p = xdr_reserve_space(xdr, nbytes);
	BUG_ON(!p);
	return p;
}

static void encode_opaque_fixed(struct xdr_stream *xdr, const void *buf, size_t len)
{
	WARN_ON_ONCE(xdr_stream_encode_opaque_fixed(xdr, buf, len) < 0);
}

static void encode_string(struct xdr_stream *xdr, unsigned int len, const char *str)
{
	WARN_ON_ONCE(xdr_stream_encode_opaque(xdr, str, len) < 0);
}

static void encode_uint32(struct xdr_stream *xdr, u32 n)
{
	WARN_ON_ONCE(xdr_stream_encode_u32(xdr, n) < 0);
}

static void encode_uint64(struct xdr_stream *xdr, u64 n)
{
	WARN_ON_ONCE(xdr_stream_encode_u64(xdr, n) < 0);
}

static ssize_t xdr_encode_bitmap4(struct xdr_stream *xdr,
		const __u32 *bitmap, size_t len)
{
	ssize_t ret;

	/* Trim empty words */
	while (len > 0 && bitmap[len-1] == 0)
		len--;
	ret = xdr_stream_encode_uint32_array(xdr, bitmap, len);
	if (WARN_ON_ONCE(ret < 0))
		return ret;
	return len;
}

static size_t mask_bitmap4(const __u32 *bitmap, const __u32 *mask,
		__u32 *res, size_t len)
{
	size_t i;
	__u32 tmp;

	while (len > 0 && (bitmap[len-1] == 0 || mask[len-1] == 0))
		len--;
	for (i = len; i-- > 0;) {
		tmp = bitmap[i] & mask[i];
		res[i] = tmp;
	}
	return len;
}

static void encode_nfs4_seqid(struct xdr_stream *xdr,
		const struct nfs_seqid *seqid)
{
	if (seqid != NULL)
		encode_uint32(xdr, seqid->sequence->counter);
	else
		encode_uint32(xdr, 0);
}

static void encode_compound_hdr(struct xdr_stream *xdr,
				struct rpc_rqst *req,
				struct compound_hdr *hdr)
{
	__be32 *p;

	/* initialize running count of expected bytes in reply.
	 * NOTE: the replied tag SHOULD be the same is the one sent,
	 * but this is not required as a MUST for the server to do so. */
	hdr->replen = 3 + hdr->taglen;

	WARN_ON_ONCE(hdr->taglen > NFS4_MAXTAGLEN);
	encode_string(xdr, hdr->taglen, hdr->tag);
	p = reserve_space(xdr, 8);
	*p++ = cpu_to_be32(hdr->minorversion);
	hdr->nops_p = p;
	*p = cpu_to_be32(hdr->nops);
}

static void encode_op_hdr(struct xdr_stream *xdr, enum nfs_opnum4 op,
		uint32_t replen,
		struct compound_hdr *hdr)
{
	encode_uint32(xdr, op);
	hdr->nops++;
	hdr->replen += replen;
}

static void encode_nops(struct compound_hdr *hdr)
{
	WARN_ON_ONCE(hdr->nops > NFS4_MAX_OPS);
	*hdr->nops_p = htonl(hdr->nops);
}

static void encode_nfs4_stateid(struct xdr_stream *xdr, const nfs4_stateid *stateid)
{
	encode_opaque_fixed(xdr, stateid, NFS4_STATEID_SIZE);
}

static void encode_nfs4_verifier(struct xdr_stream *xdr, const nfs4_verifier *verf)
{
	encode_opaque_fixed(xdr, verf->data, NFS4_VERIFIER_SIZE);
}

static __be32 *
xdr_encode_nfstime4(__be32 *p, const struct timespec64 *t)
{
	p = xdr_encode_hyper(p, t->tv_sec);
	*p++ = cpu_to_be32(t->tv_nsec);
	return p;
}

static void encode_attrs(struct xdr_stream *xdr, const struct iattr *iap,
				const struct nfs4_label *label,
				const umode_t *umask,
				const struct nfs_server *server,
				const uint32_t attrmask[])
{
	char owner_name[IDMAP_NAMESZ];
	char owner_group[IDMAP_NAMESZ];
	int owner_namelen = 0;
	int owner_grouplen = 0;
	__be32 *p;
	uint32_t len = 0;
	uint32_t bmval[3] = { 0 };

	/*
	 * We reserve enough space to write the entire attribute buffer at once.
	 */
	if ((iap->ia_valid & ATTR_SIZE) && (attrmask[0] & FATTR4_WORD0_SIZE)) {
		bmval[0] |= FATTR4_WORD0_SIZE;
		len += 8;
	}
	if (iap->ia_valid & ATTR_MODE) {
		if (umask && (attrmask[2] & FATTR4_WORD2_MODE_UMASK)) {
			bmval[2] |= FATTR4_WORD2_MODE_UMASK;
			len += 8;
		} else if (attrmask[1] & FATTR4_WORD1_MODE) {
			bmval[1] |= FATTR4_WORD1_MODE;
			len += 4;
		}
	}
	if ((iap->ia_valid & ATTR_UID) && (attrmask[1] & FATTR4_WORD1_OWNER)) {
		owner_namelen = nfs_map_uid_to_name(server, iap->ia_uid, owner_name, IDMAP_NAMESZ);
		if (owner_namelen < 0) {
			dprintk("nfs: couldn't resolve uid %d to string\n",
					from_kuid(&init_user_ns, iap->ia_uid));
			/* XXX */
			strcpy(owner_name, "nobody");
			owner_namelen = sizeof("nobody") - 1;
			/* goto out; */
		}
		bmval[1] |= FATTR4_WORD1_OWNER;
		len += 4 + (XDR_QUADLEN(owner_namelen) << 2);
	}
	if ((iap->ia_valid & ATTR_GID) &&
	   (attrmask[1] & FATTR4_WORD1_OWNER_GROUP)) {
		owner_grouplen = nfs_map_gid_to_group(server, iap->ia_gid, owner_group, IDMAP_NAMESZ);
		if (owner_grouplen < 0) {
			dprintk("nfs: couldn't resolve gid %d to string\n",
					from_kgid(&init_user_ns, iap->ia_gid));
			strcpy(owner_group, "nobody");
			owner_grouplen = sizeof("nobody") - 1;
			/* goto out; */
		}
		bmval[1] |= FATTR4_WORD1_OWNER_GROUP;
		len += 4 + (XDR_QUADLEN(owner_grouplen) << 2);
	}
	if (attrmask[1] & FATTR4_WORD1_TIME_ACCESS_SET) {
		if (iap->ia_valid & ATTR_ATIME_SET) {
			bmval[1] |= FATTR4_WORD1_TIME_ACCESS_SET;
			len += 4 + (nfstime4_maxsz << 2);
		} else if (iap->ia_valid & ATTR_ATIME) {
			bmval[1] |= FATTR4_WORD1_TIME_ACCESS_SET;
			len += 4;
		}
	}
	if (attrmask[1] & FATTR4_WORD1_TIME_MODIFY_SET) {
		if (iap->ia_valid & ATTR_MTIME_SET) {
			bmval[1] |= FATTR4_WORD1_TIME_MODIFY_SET;
			len += 4 + (nfstime4_maxsz << 2);
		} else if (iap->ia_valid & ATTR_MTIME) {
			bmval[1] |= FATTR4_WORD1_TIME_MODIFY_SET;
			len += 4;
		}
	}

	if (label && (attrmask[2] & FATTR4_WORD2_SECURITY_LABEL)) {
		len += 4 + 4 + 4 + (XDR_QUADLEN(label->len) << 2);
		bmval[2] |= FATTR4_WORD2_SECURITY_LABEL;
	}

	xdr_encode_bitmap4(xdr, bmval, ARRAY_SIZE(bmval));
	xdr_stream_encode_opaque_inline(xdr, (void **)&p, len);

	if (bmval[0] & FATTR4_WORD0_SIZE)
		p = xdr_encode_hyper(p, iap->ia_size);
	if (bmval[1] & FATTR4_WORD1_MODE)
		*p++ = cpu_to_be32(iap->ia_mode & S_IALLUGO);
	if (bmval[1] & FATTR4_WORD1_OWNER)
		p = xdr_encode_opaque(p, owner_name, owner_namelen);
	if (bmval[1] & FATTR4_WORD1_OWNER_GROUP)
		p = xdr_encode_opaque(p, owner_group, owner_grouplen);
	if (bmval[1] & FATTR4_WORD1_TIME_ACCESS_SET) {
		if (iap->ia_valid & ATTR_ATIME_SET) {
			*p++ = cpu_to_be32(NFS4_SET_TO_CLIENT_TIME);
			p = xdr_encode_nfstime4(p, &iap->ia_atime);
		} else
			*p++ = cpu_to_be32(NFS4_SET_TO_SERVER_TIME);
	}
	if (bmval[1] & FATTR4_WORD1_TIME_MODIFY_SET) {
		if (iap->ia_valid & ATTR_MTIME_SET) {
			*p++ = cpu_to_be32(NFS4_SET_TO_CLIENT_TIME);
			p = xdr_encode_nfstime4(p, &iap->ia_mtime);
		} else
			*p++ = cpu_to_be32(NFS4_SET_TO_SERVER_TIME);
	}
	if (label && (bmval[2] & FATTR4_WORD2_SECURITY_LABEL)) {
		*p++ = cpu_to_be32(label->lfs);
		*p++ = cpu_to_be32(label->pi);
		*p++ = cpu_to_be32(label->len);
		p = xdr_encode_opaque_fixed(p, label->label, label->len);
	}
	if (bmval[2] & FATTR4_WORD2_MODE_UMASK) {
		*p++ = cpu_to_be32(iap->ia_mode & S_IALLUGO);
		*p++ = cpu_to_be32(*umask);
	}

/* out: */
}

static void encode_access(struct xdr_stream *xdr, u32 access, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_ACCESS, decode_access_maxsz, hdr);
	encode_uint32(xdr, access);
}

static void encode_close(struct xdr_stream *xdr, const struct nfs_closeargs *arg, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_CLOSE, decode_close_maxsz, hdr);
	encode_nfs4_seqid(xdr, arg->seqid);
	encode_nfs4_stateid(xdr, &arg->stateid);
}

static void encode_commit(struct xdr_stream *xdr, const struct nfs_commitargs *args, struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_COMMIT, decode_commit_maxsz, hdr);
	p = reserve_space(xdr, 12);
	p = xdr_encode_hyper(p, args->offset);
	*p = cpu_to_be32(args->count);
}

static void encode_create(struct xdr_stream *xdr, const struct nfs4_create_arg *create, struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_CREATE, decode_create_maxsz, hdr);
	encode_uint32(xdr, create->ftype);

	switch (create->ftype) {
	case NF4LNK:
		p = reserve_space(xdr, 4);
		*p = cpu_to_be32(create->u.symlink.len);
		xdr_write_pages(xdr, create->u.symlink.pages, 0,
				create->u.symlink.len);
		xdr->buf->flags |= XDRBUF_WRITE;
		break;

	case NF4BLK: case NF4CHR:
		p = reserve_space(xdr, 8);
		*p++ = cpu_to_be32(create->u.device.specdata1);
		*p = cpu_to_be32(create->u.device.specdata2);
		break;

	default:
		break;
	}

	encode_string(xdr, create->name->len, create->name->name);
	encode_attrs(xdr, create->attrs, create->label, &create->umask,
			create->server, create->server->attr_bitmask);
}

static void encode_getattr(struct xdr_stream *xdr,
		const __u32 *bitmap, const __u32 *mask, size_t len,
		struct compound_hdr *hdr)
{
	__u32 masked_bitmap[nfs4_fattr_bitmap_maxsz];

	encode_op_hdr(xdr, OP_GETATTR, decode_getattr_maxsz, hdr);
	if (mask) {
		if (WARN_ON_ONCE(len > ARRAY_SIZE(masked_bitmap)))
			len = ARRAY_SIZE(masked_bitmap);
		len = mask_bitmap4(bitmap, mask, masked_bitmap, len);
		bitmap = masked_bitmap;
	}
	xdr_encode_bitmap4(xdr, bitmap, len);
}

static void encode_getfattr(struct xdr_stream *xdr, const u32* bitmask, struct compound_hdr *hdr)
{
	encode_getattr(xdr, nfs4_fattr_bitmap, bitmask,
			ARRAY_SIZE(nfs4_fattr_bitmap), hdr);
}

static void encode_getfattr_open(struct xdr_stream *xdr, const u32 *bitmask,
				 const u32 *open_bitmap,
				 struct compound_hdr *hdr)
{
	encode_getattr(xdr, open_bitmap, bitmask, 3, hdr);
}

static void encode_fsinfo(struct xdr_stream *xdr, const u32* bitmask, struct compound_hdr *hdr)
{
	encode_getattr(xdr, nfs4_fsinfo_bitmap, bitmask,
			ARRAY_SIZE(nfs4_fsinfo_bitmap), hdr);
}

static void encode_fs_locations(struct xdr_stream *xdr, const u32* bitmask, struct compound_hdr *hdr)
{
	encode_getattr(xdr, nfs4_fs_locations_bitmap, bitmask,
			ARRAY_SIZE(nfs4_fs_locations_bitmap), hdr);
}

static void encode_getfh(struct xdr_stream *xdr, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_GETFH, decode_getfh_maxsz, hdr);
}

static void encode_link(struct xdr_stream *xdr, const struct qstr *name, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_LINK, decode_link_maxsz, hdr);
	encode_string(xdr, name->len, name->name);
}

static inline int nfs4_lock_type(struct file_lock *fl, int block)
{
	if (fl->fl_type == F_RDLCK)
		return block ? NFS4_READW_LT : NFS4_READ_LT;
	return block ? NFS4_WRITEW_LT : NFS4_WRITE_LT;
}

static inline uint64_t nfs4_lock_length(struct file_lock *fl)
{
	if (fl->fl_end == OFFSET_MAX)
		return ~(uint64_t)0;
	return fl->fl_end - fl->fl_start + 1;
}

static void encode_lockowner(struct xdr_stream *xdr, const struct nfs_lowner *lowner)
{
	__be32 *p;

	p = reserve_space(xdr, 32);
	p = xdr_encode_hyper(p, lowner->clientid);
	*p++ = cpu_to_be32(20);
	p = xdr_encode_opaque_fixed(p, "lock id:", 8);
	*p++ = cpu_to_be32(lowner->s_dev);
	xdr_encode_hyper(p, lowner->id);
}

/*
 * opcode,type,reclaim,offset,length,new_lock_owner = 32
 * open_seqid,open_stateid,lock_seqid,lock_owner.clientid, lock_owner.id = 40
 */
static void encode_lock(struct xdr_stream *xdr, const struct nfs_lock_args *args, struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_LOCK, decode_lock_maxsz, hdr);
	p = reserve_space(xdr, 28);
	*p++ = cpu_to_be32(nfs4_lock_type(args->fl, args->block));
	*p++ = cpu_to_be32(args->reclaim);
	p = xdr_encode_hyper(p, args->fl->fl_start);
	p = xdr_encode_hyper(p, nfs4_lock_length(args->fl));
	*p = cpu_to_be32(args->new_lock_owner);
	if (args->new_lock_owner){
		encode_nfs4_seqid(xdr, args->open_seqid);
		encode_nfs4_stateid(xdr, &args->open_stateid);
		encode_nfs4_seqid(xdr, args->lock_seqid);
		encode_lockowner(xdr, &args->lock_owner);
	}
	else {
		encode_nfs4_stateid(xdr, &args->lock_stateid);
		encode_nfs4_seqid(xdr, args->lock_seqid);
	}
}

static void encode_lockt(struct xdr_stream *xdr, const struct nfs_lockt_args *args, struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_LOCKT, decode_lockt_maxsz, hdr);
	p = reserve_space(xdr, 20);
	*p++ = cpu_to_be32(nfs4_lock_type(args->fl, 0));
	p = xdr_encode_hyper(p, args->fl->fl_start);
	p = xdr_encode_hyper(p, nfs4_lock_length(args->fl));
	encode_lockowner(xdr, &args->lock_owner);
}

static void encode_locku(struct xdr_stream *xdr, const struct nfs_locku_args *args, struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_LOCKU, decode_locku_maxsz, hdr);
	encode_uint32(xdr, nfs4_lock_type(args->fl, 0));
	encode_nfs4_seqid(xdr, args->seqid);
	encode_nfs4_stateid(xdr, &args->stateid);
	p = reserve_space(xdr, 16);
	p = xdr_encode_hyper(p, args->fl->fl_start);
	xdr_encode_hyper(p, nfs4_lock_length(args->fl));
}

static void encode_release_lockowner(struct xdr_stream *xdr, const struct nfs_lowner *lowner, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_RELEASE_LOCKOWNER, decode_release_lockowner_maxsz, hdr);
	encode_lockowner(xdr, lowner);
}

static void encode_lookup(struct xdr_stream *xdr, const struct qstr *name, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_LOOKUP, decode_lookup_maxsz, hdr);
	encode_string(xdr, name->len, name->name);
}

static void encode_lookupp(struct xdr_stream *xdr, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_LOOKUPP, decode_lookupp_maxsz, hdr);
}

static void encode_share_access(struct xdr_stream *xdr, u32 share_access)
{
	__be32 *p;

	p = reserve_space(xdr, 8);
	*p++ = cpu_to_be32(share_access);
	*p = cpu_to_be32(0);		/* for linux, share_deny = 0 always */
}

static inline void encode_openhdr(struct xdr_stream *xdr, const struct nfs_openargs *arg)
{
	__be32 *p;
 /*
 * opcode 4, seqid 4, share_access 4, share_deny 4, clientid 8, ownerlen 4,
 * owner 4 = 32
 */
	encode_nfs4_seqid(xdr, arg->seqid);
	encode_share_access(xdr, arg->share_access);
	p = reserve_space(xdr, 36);
	p = xdr_encode_hyper(p, arg->clientid);
	*p++ = cpu_to_be32(24);
	p = xdr_encode_opaque_fixed(p, "open id:", 8);
	*p++ = cpu_to_be32(arg->server->s_dev);
	*p++ = cpu_to_be32(arg->id.uniquifier);
	xdr_encode_hyper(p, arg->id.create_time);
}

static inline void encode_createmode(struct xdr_stream *xdr, const struct nfs_openargs *arg)
{
	__be32 *p;

	p = reserve_space(xdr, 4);
	switch(arg->createmode) {
	case NFS4_CREATE_UNCHECKED:
		*p = cpu_to_be32(NFS4_CREATE_UNCHECKED);
		encode_attrs(xdr, arg->u.attrs, arg->label, &arg->umask,
				arg->server, arg->server->attr_bitmask);
		break;
	case NFS4_CREATE_GUARDED:
		*p = cpu_to_be32(NFS4_CREATE_GUARDED);
		encode_attrs(xdr, arg->u.attrs, arg->label, &arg->umask,
				arg->server, arg->server->attr_bitmask);
		break;
	case NFS4_CREATE_EXCLUSIVE:
		*p = cpu_to_be32(NFS4_CREATE_EXCLUSIVE);
		encode_nfs4_verifier(xdr, &arg->u.verifier);
		break;
	case NFS4_CREATE_EXCLUSIVE4_1:
		*p = cpu_to_be32(NFS4_CREATE_EXCLUSIVE4_1);
		encode_nfs4_verifier(xdr, &arg->u.verifier);
		encode_attrs(xdr, arg->u.attrs, arg->label, &arg->umask,
				arg->server, arg->server->exclcreat_bitmask);
	}
}

static void encode_opentype(struct xdr_stream *xdr, const struct nfs_openargs *arg)
{
	__be32 *p;

	p = reserve_space(xdr, 4);
	switch (arg->open_flags & O_CREAT) {
	case 0:
		*p = cpu_to_be32(NFS4_OPEN_NOCREATE);
		break;
	default:
		*p = cpu_to_be32(NFS4_OPEN_CREATE);
		encode_createmode(xdr, arg);
	}
}

static inline void encode_delegation_type(struct xdr_stream *xdr, fmode_t delegation_type)
{
	__be32 *p;

	p = reserve_space(xdr, 4);
	switch (delegation_type) {
	case 0:
		*p = cpu_to_be32(NFS4_OPEN_DELEGATE_NONE);
		break;
	case FMODE_READ:
		*p = cpu_to_be32(NFS4_OPEN_DELEGATE_READ);
		break;
	case FMODE_WRITE|FMODE_READ:
		*p = cpu_to_be32(NFS4_OPEN_DELEGATE_WRITE);
		break;
	default:
		BUG();
	}
}

static inline void encode_claim_null(struct xdr_stream *xdr, const struct qstr *name)
{
	__be32 *p;

	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(NFS4_OPEN_CLAIM_NULL);
	encode_string(xdr, name->len, name->name);
}

static inline void encode_claim_previous(struct xdr_stream *xdr, fmode_t type)
{
	__be32 *p;

	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(NFS4_OPEN_CLAIM_PREVIOUS);
	encode_delegation_type(xdr, type);
}

static inline void encode_claim_delegate_cur(struct xdr_stream *xdr, const struct qstr *name, const nfs4_stateid *stateid)
{
	__be32 *p;

	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(NFS4_OPEN_CLAIM_DELEGATE_CUR);
	encode_nfs4_stateid(xdr, stateid);
	encode_string(xdr, name->len, name->name);
}

static inline void encode_claim_fh(struct xdr_stream *xdr)
{
	__be32 *p;

	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(NFS4_OPEN_CLAIM_FH);
}

static inline void encode_claim_delegate_cur_fh(struct xdr_stream *xdr, const nfs4_stateid *stateid)
{
	__be32 *p;

	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(NFS4_OPEN_CLAIM_DELEG_CUR_FH);
	encode_nfs4_stateid(xdr, stateid);
}

static void encode_open(struct xdr_stream *xdr, const struct nfs_openargs *arg, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_OPEN, decode_open_maxsz, hdr);
	encode_openhdr(xdr, arg);
	encode_opentype(xdr, arg);
	switch (arg->claim) {
	case NFS4_OPEN_CLAIM_NULL:
		encode_claim_null(xdr, arg->name);
		break;
	case NFS4_OPEN_CLAIM_PREVIOUS:
		encode_claim_previous(xdr, arg->u.delegation_type);
		break;
	case NFS4_OPEN_CLAIM_DELEGATE_CUR:
		encode_claim_delegate_cur(xdr, arg->name, &arg->u.delegation);
		break;
	case NFS4_OPEN_CLAIM_FH:
		encode_claim_fh(xdr);
		break;
	case NFS4_OPEN_CLAIM_DELEG_CUR_FH:
		encode_claim_delegate_cur_fh(xdr, &arg->u.delegation);
		break;
	default:
		BUG();
	}
}

static void encode_open_confirm(struct xdr_stream *xdr, const struct nfs_open_confirmargs *arg, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_OPEN_CONFIRM, decode_open_confirm_maxsz, hdr);
	encode_nfs4_stateid(xdr, arg->stateid);
	encode_nfs4_seqid(xdr, arg->seqid);
}

static void encode_open_downgrade(struct xdr_stream *xdr, const struct nfs_closeargs *arg, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_OPEN_DOWNGRADE, decode_open_downgrade_maxsz, hdr);
	encode_nfs4_stateid(xdr, &arg->stateid);
	encode_nfs4_seqid(xdr, arg->seqid);
	encode_share_access(xdr, arg->share_access);
}

static void
encode_putfh(struct xdr_stream *xdr, const struct nfs_fh *fh, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_PUTFH, decode_putfh_maxsz, hdr);
	encode_string(xdr, fh->size, fh->data);
}

static void encode_putrootfh(struct xdr_stream *xdr, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_PUTROOTFH, decode_putrootfh_maxsz, hdr);
}

static void encode_read(struct xdr_stream *xdr, const struct nfs_pgio_args *args,
			struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_READ, decode_read_maxsz, hdr);
	encode_nfs4_stateid(xdr, &args->stateid);

	p = reserve_space(xdr, 12);
	p = xdr_encode_hyper(p, args->offset);
	*p = cpu_to_be32(args->count);
}

static void encode_readdir(struct xdr_stream *xdr, const struct nfs4_readdir_arg *readdir, struct rpc_rqst *req, struct compound_hdr *hdr)
{
	uint32_t attrs[3] = {
		FATTR4_WORD0_RDATTR_ERROR,
		FATTR4_WORD1_MOUNTED_ON_FILEID,
	};
	uint32_t dircount = readdir->count;
	uint32_t maxcount = readdir->count;
	__be32 *p, verf[2];
	uint32_t attrlen = 0;
	unsigned int i;

	if (readdir->plus) {
		attrs[0] |= FATTR4_WORD0_TYPE|FATTR4_WORD0_CHANGE|FATTR4_WORD0_SIZE|
			FATTR4_WORD0_FSID|FATTR4_WORD0_FILEHANDLE|FATTR4_WORD0_FILEID;
		attrs[1] |= FATTR4_WORD1_MODE|FATTR4_WORD1_NUMLINKS|FATTR4_WORD1_OWNER|
			FATTR4_WORD1_OWNER_GROUP|FATTR4_WORD1_RAWDEV|
			FATTR4_WORD1_SPACE_USED|FATTR4_WORD1_TIME_ACCESS|
			FATTR4_WORD1_TIME_METADATA|FATTR4_WORD1_TIME_MODIFY;
		attrs[2] |= FATTR4_WORD2_SECURITY_LABEL;
	}
	/* Use mounted_on_fileid only if the server supports it */
	if (!(readdir->bitmask[1] & FATTR4_WORD1_MOUNTED_ON_FILEID))
		attrs[0] |= FATTR4_WORD0_FILEID;
	for (i = 0; i < ARRAY_SIZE(attrs); i++) {
		attrs[i] &= readdir->bitmask[i];
		if (attrs[i] != 0)
			attrlen = i+1;
	}

	encode_op_hdr(xdr, OP_READDIR, decode_readdir_maxsz, hdr);
	encode_uint64(xdr, readdir->cookie);
	encode_nfs4_verifier(xdr, &readdir->verifier);
	p = reserve_space(xdr, 12 + (attrlen << 2));
	*p++ = cpu_to_be32(dircount);
	*p++ = cpu_to_be32(maxcount);
	*p++ = cpu_to_be32(attrlen);
	for (i = 0; i < attrlen; i++)
		*p++ = cpu_to_be32(attrs[i]);
	memcpy(verf, readdir->verifier.data, sizeof(verf));

	dprintk("%s: cookie = %llu, verifier = %08x:%08x, bitmap = %08x:%08x:%08x\n",
			__func__,
			(unsigned long long)readdir->cookie,
			verf[0], verf[1],
			attrs[0] & readdir->bitmask[0],
			attrs[1] & readdir->bitmask[1],
			attrs[2] & readdir->bitmask[2]);
}

static void encode_readlink(struct xdr_stream *xdr, const struct nfs4_readlink *readlink, struct rpc_rqst *req, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_READLINK, decode_readlink_maxsz, hdr);
}

static void encode_remove(struct xdr_stream *xdr, const struct qstr *name, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_REMOVE, decode_remove_maxsz, hdr);
	encode_string(xdr, name->len, name->name);
}

static void encode_rename(struct xdr_stream *xdr, const struct qstr *oldname, const struct qstr *newname, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_RENAME, decode_rename_maxsz, hdr);
	encode_string(xdr, oldname->len, oldname->name);
	encode_string(xdr, newname->len, newname->name);
}

static void encode_renew(struct xdr_stream *xdr, clientid4 clid,
			 struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_RENEW, decode_renew_maxsz, hdr);
	encode_uint64(xdr, clid);
}

static void
encode_restorefh(struct xdr_stream *xdr, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_RESTOREFH, decode_restorefh_maxsz, hdr);
}

static void nfs4_acltype_to_bitmap(enum nfs4_acl_type type, __u32 bitmap[2])
{
	switch (type) {
	default:
		bitmap[0] = FATTR4_WORD0_ACL;
		bitmap[1] = 0;
		break;
	case NFS4ACL_DACL:
		bitmap[0] = 0;
		bitmap[1] = FATTR4_WORD1_DACL;
		break;
	case NFS4ACL_SACL:
		bitmap[0] = 0;
		bitmap[1] = FATTR4_WORD1_SACL;
	}
}

static void encode_setacl(struct xdr_stream *xdr,
			  const struct nfs_setaclargs *arg,
			  struct compound_hdr *hdr)
{
	__u32 bitmap[2];

	nfs4_acltype_to_bitmap(arg->acl_type, bitmap);

	encode_op_hdr(xdr, OP_SETATTR, decode_setacl_maxsz, hdr);
	encode_nfs4_stateid(xdr, &zero_stateid);
	xdr_encode_bitmap4(xdr, bitmap, ARRAY_SIZE(bitmap));
	encode_uint32(xdr, arg->acl_len);
	xdr_write_pages(xdr, arg->acl_pages, 0, arg->acl_len);
}

static void
encode_savefh(struct xdr_stream *xdr, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_SAVEFH, decode_savefh_maxsz, hdr);
}

static void encode_setattr(struct xdr_stream *xdr, const struct nfs_setattrargs *arg, const struct nfs_server *server, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_SETATTR, decode_setattr_maxsz, hdr);
	encode_nfs4_stateid(xdr, &arg->stateid);
	encode_attrs(xdr, arg->iap, arg->label, NULL, server,
			server->attr_bitmask);
}

static void encode_setclientid(struct xdr_stream *xdr, const struct nfs4_setclientid *setclientid, struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_SETCLIENTID, decode_setclientid_maxsz, hdr);
	encode_nfs4_verifier(xdr, setclientid->sc_verifier);

	encode_string(xdr, strlen(setclientid->sc_clnt->cl_owner_id),
			setclientid->sc_clnt->cl_owner_id);
	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(setclientid->sc_prog);
	encode_string(xdr, setclientid->sc_netid_len, setclientid->sc_netid);
	encode_string(xdr, setclientid->sc_uaddr_len, setclientid->sc_uaddr);
	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(setclientid->sc_clnt->cl_cb_ident);
}

static void encode_setclientid_confirm(struct xdr_stream *xdr, const struct nfs4_setclientid_res *arg, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_SETCLIENTID_CONFIRM,
			decode_setclientid_confirm_maxsz, hdr);
	encode_uint64(xdr, arg->clientid);
	encode_nfs4_verifier(xdr, &arg->confirm);
}

static void encode_write(struct xdr_stream *xdr, const struct nfs_pgio_args *args,
			 struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_WRITE, decode_write_maxsz, hdr);
	encode_nfs4_stateid(xdr, &args->stateid);

	p = reserve_space(xdr, 16);
	p = xdr_encode_hyper(p, args->offset);
	*p++ = cpu_to_be32(args->stable);
	*p = cpu_to_be32(args->count);

	xdr_write_pages(xdr, args->pages, args->pgbase, args->count);
}

static void encode_delegreturn(struct xdr_stream *xdr, const nfs4_stateid *stateid, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_DELEGRETURN, decode_delegreturn_maxsz, hdr);
	encode_nfs4_stateid(xdr, stateid);
}

static void encode_secinfo(struct xdr_stream *xdr, const struct qstr *name, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_SECINFO, decode_secinfo_maxsz, hdr);
	encode_string(xdr, name->len, name->name);
}

#if defined(CONFIG_NFS_V4_1)
/* NFSv4.1 operations */
static void encode_bind_conn_to_session(struct xdr_stream *xdr,
				   const struct nfs41_bind_conn_to_session_args *args,
				   struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_BIND_CONN_TO_SESSION,
		decode_bind_conn_to_session_maxsz, hdr);
	encode_opaque_fixed(xdr, args->sessionid.data, NFS4_MAX_SESSIONID_LEN);
	p = xdr_reserve_space(xdr, 8);
	*p++ = cpu_to_be32(args->dir);
	*p = (args->use_conn_in_rdma_mode) ? cpu_to_be32(1) : cpu_to_be32(0);
}

static void encode_op_map(struct xdr_stream *xdr, const struct nfs4_op_map *op_map)
{
	unsigned int i;
	encode_uint32(xdr, NFS4_OP_MAP_NUM_WORDS);
	for (i = 0; i < NFS4_OP_MAP_NUM_WORDS; i++)
		encode_uint32(xdr, op_map->u.words[i]);
}

static void encode_exchange_id(struct xdr_stream *xdr,
			       const struct nfs41_exchange_id_args *args,
			       struct compound_hdr *hdr)
{
	__be32 *p;
	char impl_name[IMPL_NAME_LIMIT];
	int len = 0;

	encode_op_hdr(xdr, OP_EXCHANGE_ID, decode_exchange_id_maxsz, hdr);
	encode_nfs4_verifier(xdr, &args->verifier);

	encode_string(xdr, strlen(args->client->cl_owner_id),
			args->client->cl_owner_id);

	encode_uint32(xdr, args->flags);
	encode_uint32(xdr, args->state_protect.how);

	switch (args->state_protect.how) {
	case SP4_NONE:
		break;
	case SP4_MACH_CRED:
		encode_op_map(xdr, &args->state_protect.enforce);
		encode_op_map(xdr, &args->state_protect.allow);
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}

	if (send_implementation_id &&
	    sizeof(CONFIG_NFS_V4_1_IMPLEMENTATION_ID_DOMAIN) > 1 &&
	    sizeof(CONFIG_NFS_V4_1_IMPLEMENTATION_ID_DOMAIN)
		<= sizeof(impl_name) + 1)
		len = snprintf(impl_name, sizeof(impl_name), "%s %s %s %s",
			       utsname()->sysname, utsname()->release,
			       utsname()->version, utsname()->machine);

	if (len > 0) {
		encode_uint32(xdr, 1);	/* implementation id array length=1 */

		encode_string(xdr,
			sizeof(CONFIG_NFS_V4_1_IMPLEMENTATION_ID_DOMAIN) - 1,
			CONFIG_NFS_V4_1_IMPLEMENTATION_ID_DOMAIN);
		encode_string(xdr, len, impl_name);
		/* just send zeros for nii_date - the date is in nii_name */
		p = reserve_space(xdr, 12);
		p = xdr_encode_hyper(p, 0);
		*p = cpu_to_be32(0);
	} else
		encode_uint32(xdr, 0);	/* implementation id array length=0 */
}

static void encode_create_session(struct xdr_stream *xdr,
				  const struct nfs41_create_session_args *args,
				  struct compound_hdr *hdr)
{
	__be32 *p;
	struct nfs_client *clp = args->client;
	struct rpc_clnt *clnt = clp->cl_rpcclient;
	struct nfs_net *nn = net_generic(clp->cl_net, nfs_net_id);
	u32 max_resp_sz_cached;

	/*
	 * Assumes OPEN is the biggest non-idempotent compound.
	 * 2 is the verifier.
	 */
	max_resp_sz_cached = (NFS4_dec_open_sz + RPC_REPHDRSIZE + 2)
				* XDR_UNIT + RPC_MAX_AUTH_SIZE;

	encode_op_hdr(xdr, OP_CREATE_SESSION, decode_create_session_maxsz, hdr);
	p = reserve_space(xdr, 16 + 2*28 + 20 + clnt->cl_nodelen + 12);
	p = xdr_encode_hyper(p, args->clientid);
	*p++ = cpu_to_be32(args->seqid);			/*Sequence id */
	*p++ = cpu_to_be32(args->flags);			/*flags */

	/* Fore Channel */
	*p++ = cpu_to_be32(0);				/* header padding size */
	*p++ = cpu_to_be32(args->fc_attrs.max_rqst_sz);	/* max req size */
	*p++ = cpu_to_be32(args->fc_attrs.max_resp_sz);	/* max resp size */
	*p++ = cpu_to_be32(max_resp_sz_cached);		/* Max resp sz cached */
	*p++ = cpu_to_be32(args->fc_attrs.max_ops);	/* max operations */
	*p++ = cpu_to_be32(args->fc_attrs.max_reqs);	/* max requests */
	*p++ = cpu_to_be32(0);				/* rdmachannel_attrs */

	/* Back Channel */
	*p++ = cpu_to_be32(0);				/* header padding size */
	*p++ = cpu_to_be32(args->bc_attrs.max_rqst_sz);	/* max req size */
	*p++ = cpu_to_be32(args->bc_attrs.max_resp_sz);	/* max resp size */
	*p++ = cpu_to_be32(args->bc_attrs.max_resp_sz_cached);	/* Max resp sz cached */
	*p++ = cpu_to_be32(args->bc_attrs.max_ops);	/* max operations */
	*p++ = cpu_to_be32(args->bc_attrs.max_reqs);	/* max requests */
	*p++ = cpu_to_be32(0);				/* rdmachannel_attrs */

	*p++ = cpu_to_be32(args->cb_program);		/* cb_program */
	*p++ = cpu_to_be32(1);
	*p++ = cpu_to_be32(RPC_AUTH_UNIX);			/* auth_sys */

	/* authsys_parms rfc1831 */
	*p++ = cpu_to_be32(ktime_to_ns(nn->boot_time));	/* stamp */
	p = xdr_encode_array(p, clnt->cl_nodename, clnt->cl_nodelen);
	*p++ = cpu_to_be32(0);				/* UID */
	*p++ = cpu_to_be32(0);				/* GID */
	*p = cpu_to_be32(0);				/* No more gids */
}

static void encode_destroy_session(struct xdr_stream *xdr,
				   const struct nfs4_session *session,
				   struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_DESTROY_SESSION, decode_destroy_session_maxsz, hdr);
	encode_opaque_fixed(xdr, session->sess_id.data, NFS4_MAX_SESSIONID_LEN);
}

static void encode_destroy_clientid(struct xdr_stream *xdr,
				   uint64_t clientid,
				   struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_DESTROY_CLIENTID, decode_destroy_clientid_maxsz, hdr);
	encode_uint64(xdr, clientid);
}

static void encode_reclaim_complete(struct xdr_stream *xdr,
				    const struct nfs41_reclaim_complete_args *args,
				    struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_RECLAIM_COMPLETE, decode_reclaim_complete_maxsz, hdr);
	encode_uint32(xdr, args->one_fs);
}
#endif /* CONFIG_NFS_V4_1 */

static void encode_sequence(struct xdr_stream *xdr,
			    const struct nfs4_sequence_args *args,
			    struct compound_hdr *hdr)
{
#if defined(CONFIG_NFS_V4_1)
	struct nfs4_session *session;
	struct nfs4_slot_table *tp;
	struct nfs4_slot *slot = args->sa_slot;
	__be32 *p;

	tp = slot->table;
	session = tp->session;
	if (!session)
		return;

	encode_op_hdr(xdr, OP_SEQUENCE, decode_sequence_maxsz, hdr);

	/*
	 * Sessionid + seqid + slotid + max slotid + cache_this
	 */
	dprintk("%s: sessionid=%u:%u:%u:%u seqid=%d slotid=%d "
		"max_slotid=%d cache_this=%d\n",
		__func__,
		((u32 *)session->sess_id.data)[0],
		((u32 *)session->sess_id.data)[1],
		((u32 *)session->sess_id.data)[2],
		((u32 *)session->sess_id.data)[3],
		slot->seq_nr, slot->slot_nr,
		tp->highest_used_slotid, args->sa_cache_this);
	p = reserve_space(xdr, NFS4_MAX_SESSIONID_LEN + 16);
	p = xdr_encode_opaque_fixed(p, session->sess_id.data, NFS4_MAX_SESSIONID_LEN);
	*p++ = cpu_to_be32(slot->seq_nr);
	*p++ = cpu_to_be32(slot->slot_nr);
	*p++ = cpu_to_be32(tp->highest_used_slotid);
	*p = cpu_to_be32(args->sa_cache_this);
#endif /* CONFIG_NFS_V4_1 */
}

#ifdef CONFIG_NFS_V4_1
static void
encode_getdeviceinfo(struct xdr_stream *xdr,
		     const struct nfs4_getdeviceinfo_args *args,
		     struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_GETDEVICEINFO, decode_getdeviceinfo_maxsz, hdr);
	p = reserve_space(xdr, NFS4_DEVICEID4_SIZE + 4 + 4);
	p = xdr_encode_opaque_fixed(p, args->pdev->dev_id.data,
				    NFS4_DEVICEID4_SIZE);
	*p++ = cpu_to_be32(args->pdev->layout_type);
	*p++ = cpu_to_be32(args->pdev->maxcount);	/* gdia_maxcount */

	p = reserve_space(xdr, 4 + 4);
	*p++ = cpu_to_be32(1);			/* bitmap length */
	*p++ = cpu_to_be32(args->notify_types);
}

static void
encode_layoutget(struct xdr_stream *xdr,
		      const struct nfs4_layoutget_args *args,
		      struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_LAYOUTGET, decode_layoutget_maxsz, hdr);
	p = reserve_space(xdr, 36);
	*p++ = cpu_to_be32(0);     /* Signal layout available */
	*p++ = cpu_to_be32(args->type);
	*p++ = cpu_to_be32(args->range.iomode);
	p = xdr_encode_hyper(p, args->range.offset);
	p = xdr_encode_hyper(p, args->range.length);
	p = xdr_encode_hyper(p, args->minlength);
	encode_nfs4_stateid(xdr, &args->stateid);
	encode_uint32(xdr, args->maxcount);

	dprintk("%s: 1st type:0x%x iomode:%d off:%lu len:%lu mc:%d\n",
		__func__,
		args->type,
		args->range.iomode,
		(unsigned long)args->range.offset,
		(unsigned long)args->range.length,
		args->maxcount);
}

static int
encode_layoutcommit(struct xdr_stream *xdr,
		    struct inode *inode,
		    const struct nfs4_layoutcommit_args *args,
		    struct compound_hdr *hdr)
{
	__be32 *p;

	dprintk("%s: lbw: %llu type: %d\n", __func__, args->lastbytewritten,
		NFS_SERVER(args->inode)->pnfs_curr_ld->id);

	encode_op_hdr(xdr, OP_LAYOUTCOMMIT, decode_layoutcommit_maxsz, hdr);
	p = reserve_space(xdr, 20);
	/* Only whole file layouts */
	p = xdr_encode_hyper(p, 0); /* offset */
	p = xdr_encode_hyper(p, args->lastbytewritten + 1);	/* length */
	*p = cpu_to_be32(0); /* reclaim */
	encode_nfs4_stateid(xdr, &args->stateid);
	if (args->lastbytewritten != U64_MAX) {
		p = reserve_space(xdr, 20);
		*p++ = cpu_to_be32(1); /* newoffset = TRUE */
		p = xdr_encode_hyper(p, args->lastbytewritten);
	} else {
		p = reserve_space(xdr, 12);
		*p++ = cpu_to_be32(0); /* newoffset = FALSE */
	}
	*p++ = cpu_to_be32(0); /* Never send time_modify_changed */
	*p++ = cpu_to_be32(NFS_SERVER(args->inode)->pnfs_curr_ld->id);/* type */

	encode_uint32(xdr, args->layoutupdate_len);
	if (args->layoutupdate_pages)
		xdr_write_pages(xdr, args->layoutupdate_pages, 0,
				args->layoutupdate_len);

	return 0;
}

static void
encode_layoutreturn(struct xdr_stream *xdr,
		    const struct nfs4_layoutreturn_args *args,
		    struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_LAYOUTRETURN, decode_layoutreturn_maxsz, hdr);
	p = reserve_space(xdr, 16);
	*p++ = cpu_to_be32(0);		/* reclaim. always 0 for now */
	*p++ = cpu_to_be32(args->layout_type);
	*p++ = cpu_to_be32(args->range.iomode);
	*p = cpu_to_be32(RETURN_FILE);
	p = reserve_space(xdr, 16);
	p = xdr_encode_hyper(p, args->range.offset);
	p = xdr_encode_hyper(p, args->range.length);
	spin_lock(&args->inode->i_lock);
	encode_nfs4_stateid(xdr, &args->stateid);
	spin_unlock(&args->inode->i_lock);
	if (args->ld_private->ops && args->ld_private->ops->encode)
		args->ld_private->ops->encode(xdr, args, args->ld_private);
	else
		encode_uint32(xdr, 0);
}

static int
encode_secinfo_no_name(struct xdr_stream *xdr,
		       const struct nfs41_secinfo_no_name_args *args,
		       struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_SECINFO_NO_NAME, decode_secinfo_no_name_maxsz, hdr);
	encode_uint32(xdr, args->style);
	return 0;
}

static void encode_test_stateid(struct xdr_stream *xdr,
				const struct nfs41_test_stateid_args *args,
				struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_TEST_STATEID, decode_test_stateid_maxsz, hdr);
	encode_uint32(xdr, 1);
	encode_nfs4_stateid(xdr, args->stateid);
}

static void encode_free_stateid(struct xdr_stream *xdr,
				const struct nfs41_free_stateid_args *args,
				struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_FREE_STATEID, decode_free_stateid_maxsz, hdr);
	encode_nfs4_stateid(xdr, &args->stateid);
}
#else
static inline void
encode_layoutreturn(struct xdr_stream *xdr,
		    const struct nfs4_layoutreturn_args *args,
		    struct compound_hdr *hdr)
{
}

static void
encode_layoutget(struct xdr_stream *xdr,
		      const struct nfs4_layoutget_args *args,
		      struct compound_hdr *hdr)
{
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * END OF "GENERIC" ENCODE ROUTINES.
 */

static u32 nfs4_xdr_minorversion(const struct nfs4_sequence_args *args)
{
#if defined(CONFIG_NFS_V4_1)
	struct nfs4_session *session = args->sa_slot->table->session;
	if (session)
		return session->clp->cl_mvops->minor_version;
#endif /* CONFIG_NFS_V4_1 */
	return 0;
}

/*
 * Encode an ACCESS request
 */
static void nfs4_xdr_enc_access(struct rpc_rqst *req, struct xdr_stream *xdr,
				const void *data)
{
	const struct nfs4_accessargs *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_access(xdr, args->access, &hdr);
	if (args->bitmask)
		encode_getfattr(xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode LOOKUP request
 */
static void nfs4_xdr_enc_lookup(struct rpc_rqst *req, struct xdr_stream *xdr,
				const void *data)
{
	const struct nfs4_lookup_arg *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->dir_fh, &hdr);
	encode_lookup(xdr, args->name, &hdr);
	encode_getfh(xdr, &hdr);
	encode_getfattr(xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode LOOKUPP request
 */
static void nfs4_xdr_enc_lookupp(struct rpc_rqst *req, struct xdr_stream *xdr,
		const void *data)
{
	const struct nfs4_lookupp_arg *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_lookupp(xdr, &hdr);
	encode_getfh(xdr, &hdr);
	encode_getfattr(xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode LOOKUP_ROOT request
 */
static void nfs4_xdr_enc_lookup_root(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     const void *data)
{
	const struct nfs4_lookup_root_arg *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putrootfh(xdr, &hdr);
	encode_getfh(xdr, &hdr);
	encode_getfattr(xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode REMOVE request
 */
static void nfs4_xdr_enc_remove(struct rpc_rqst *req, struct xdr_stream *xdr,
				const void *data)
{
	const struct nfs_removeargs *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_remove(xdr, &args->name, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode RENAME request
 */
static void nfs4_xdr_enc_rename(struct rpc_rqst *req, struct xdr_stream *xdr,
				const void *data)
{
	const struct nfs_renameargs *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->old_dir, &hdr);
	encode_savefh(xdr, &hdr);
	encode_putfh(xdr, args->new_dir, &hdr);
	encode_rename(xdr, args->old_name, args->new_name, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode LINK request
 */
static void nfs4_xdr_enc_link(struct rpc_rqst *req, struct xdr_stream *xdr,
			      const void *data)
{
	const struct nfs4_link_arg *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_savefh(xdr, &hdr);
	encode_putfh(xdr, args->dir_fh, &hdr);
	encode_link(xdr, args->name, &hdr);
	encode_restorefh(xdr, &hdr);
	encode_getfattr(xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode CREATE request
 */
static void nfs4_xdr_enc_create(struct rpc_rqst *req, struct xdr_stream *xdr,
				const void *data)
{
	const struct nfs4_create_arg *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->dir_fh, &hdr);
	encode_create(xdr, args, &hdr);
	encode_getfh(xdr, &hdr);
	encode_getfattr(xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode SYMLINK request
 */
static void nfs4_xdr_enc_symlink(struct rpc_rqst *req, struct xdr_stream *xdr,
				 const void *data)
{
	const struct nfs4_create_arg *args = data;

	nfs4_xdr_enc_create(req, xdr, args);
}

/*
 * Encode GETATTR request
 */
static void nfs4_xdr_enc_getattr(struct rpc_rqst *req, struct xdr_stream *xdr,
				 const void *data)
{
	const struct nfs4_getattr_arg *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_getfattr(xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode a CLOSE request
 */
static void nfs4_xdr_enc_close(struct rpc_rqst *req, struct xdr_stream *xdr,
			       const void *data)
{
	const struct nfs_closeargs *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	if (args->lr_args)
		encode_layoutreturn(xdr, args->lr_args, &hdr);
	if (args->bitmask != NULL)
		encode_getfattr(xdr, args->bitmask, &hdr);
	encode_close(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode an OPEN request
 */
static void nfs4_xdr_enc_open(struct rpc_rqst *req, struct xdr_stream *xdr,
			      const void *data)
{
	const struct nfs_openargs *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_open(xdr, args, &hdr);
	encode_getfh(xdr, &hdr);
	if (args->access)
		encode_access(xdr, args->access, &hdr);
	encode_getfattr_open(xdr, args->bitmask, args->open_bitmap, &hdr);
	if (args->lg_args) {
		encode_layoutget(xdr, args->lg_args, &hdr);
		rpc_prepare_reply_pages(req, args->lg_args->layout.pages, 0,
					args->lg_args->layout.pglen,
					hdr.replen - pagepad_maxsz);
	}
	encode_nops(&hdr);
}

/*
 * Encode an OPEN_CONFIRM request
 */
static void nfs4_xdr_enc_open_confirm(struct rpc_rqst *req,
				      struct xdr_stream *xdr,
				      const void *data)
{
	const struct nfs_open_confirmargs *args = data;
	struct compound_hdr hdr = {
		.nops   = 0,
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_open_confirm(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode an OPEN request with no attributes.
 */
static void nfs4_xdr_enc_open_noattr(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     const void *data)
{
	const struct nfs_openargs *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_open(xdr, args, &hdr);
	if (args->access)
		encode_access(xdr, args->access, &hdr);
	encode_getfattr_open(xdr, args->bitmask, args->open_bitmap, &hdr);
	if (args->lg_args) {
		encode_layoutget(xdr, args->lg_args, &hdr);
		rpc_prepare_reply_pages(req, args->lg_args->layout.pages, 0,
					args->lg_args->layout.pglen,
					hdr.replen - pagepad_maxsz);
	}
	encode_nops(&hdr);
}

/*
 * Encode an OPEN_DOWNGRADE request
 */
static void nfs4_xdr_enc_open_downgrade(struct rpc_rqst *req,
					struct xdr_stream *xdr,
					const void *data)
{
	const struct nfs_closeargs *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	if (args->lr_args)
		encode_layoutreturn(xdr, args->lr_args, &hdr);
	encode_open_downgrade(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode a LOCK request
 */
static void nfs4_xdr_enc_lock(struct rpc_rqst *req, struct xdr_stream *xdr,
			      const void *data)
{
	const struct nfs_lock_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_lock(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode a LOCKT request
 */
static void nfs4_xdr_enc_lockt(struct rpc_rqst *req, struct xdr_stream *xdr,
			       const void *data)
{
	const struct nfs_lockt_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_lockt(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode a LOCKU request
 */
static void nfs4_xdr_enc_locku(struct rpc_rqst *req, struct xdr_stream *xdr,
			       const void *data)
{
	const struct nfs_locku_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_locku(xdr, args, &hdr);
	encode_nops(&hdr);
}

static void nfs4_xdr_enc_release_lockowner(struct rpc_rqst *req,
					   struct xdr_stream *xdr,
					   const void *data)
{
	const struct nfs_release_lockowner_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = 0,
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_release_lockowner(xdr, &args->lock_owner, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode a READLINK request
 */
static void nfs4_xdr_enc_readlink(struct rpc_rqst *req, struct xdr_stream *xdr,
				  const void *data)
{
	const struct nfs4_readlink *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_readlink(xdr, args, req, &hdr);

	rpc_prepare_reply_pages(req, args->pages, args->pgbase,
				args->pglen, hdr.replen - pagepad_maxsz);
	encode_nops(&hdr);
}

/*
 * Encode a READDIR request
 */
static void nfs4_xdr_enc_readdir(struct rpc_rqst *req, struct xdr_stream *xdr,
				 const void *data)
{
	const struct nfs4_readdir_arg *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_readdir(xdr, args, req, &hdr);

	rpc_prepare_reply_pages(req, args->pages, args->pgbase,
				args->count, hdr.replen - pagepad_maxsz);
	encode_nops(&hdr);
}

/*
 * Encode a READ request
 */
static void nfs4_xdr_enc_read(struct rpc_rqst *req, struct xdr_stream *xdr,
			      const void *data)
{
	const struct nfs_pgio_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_read(xdr, args, &hdr);

	rpc_prepare_reply_pages(req, args->pages, args->pgbase,
				args->count, hdr.replen - pagepad_maxsz);
	req->rq_rcv_buf.flags |= XDRBUF_READ;
	encode_nops(&hdr);
}

/*
 * Encode an SETATTR request
 */
static void nfs4_xdr_enc_setattr(struct rpc_rqst *req, struct xdr_stream *xdr,
				 const void *data)
{
	const struct nfs_setattrargs *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_setattr(xdr, args, args->server, &hdr);
	encode_getfattr(xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode a GETACL request
 */
static void nfs4_xdr_enc_getacl(struct rpc_rqst *req, struct xdr_stream *xdr,
				const void *data)
{
	const struct nfs_getaclargs *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};
	__u32 nfs4_acl_bitmap[2];
	uint32_t replen;

	nfs4_acltype_to_bitmap(args->acl_type, nfs4_acl_bitmap);

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	replen = hdr.replen + op_decode_hdr_maxsz;
	encode_getattr(xdr, nfs4_acl_bitmap, NULL,
			ARRAY_SIZE(nfs4_acl_bitmap), &hdr);

	rpc_prepare_reply_pages(req, args->acl_pages, 0,
				args->acl_len, replen);
	encode_nops(&hdr);
}

/*
 * Encode a WRITE request
 */
static void nfs4_xdr_enc_write(struct rpc_rqst *req, struct xdr_stream *xdr,
			       const void *data)
{
	const struct nfs_pgio_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_write(xdr, args, &hdr);
	req->rq_snd_buf.flags |= XDRBUF_WRITE;
	if (args->bitmask)
		encode_getfattr(xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
}

/*
 *  a COMMIT request
 */
static void nfs4_xdr_enc_commit(struct rpc_rqst *req, struct xdr_stream *xdr,
				const void *data)
{
	const struct nfs_commitargs *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_commit(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * FSINFO request
 */
static void nfs4_xdr_enc_fsinfo(struct rpc_rqst *req, struct xdr_stream *xdr,
				const void *data)
{
	const struct nfs4_fsinfo_arg *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_fsinfo(xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
}

/*
 * a PATHCONF request
 */
static void nfs4_xdr_enc_pathconf(struct rpc_rqst *req, struct xdr_stream *xdr,
				  const void *data)
{
	const struct nfs4_pathconf_arg *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_getattr(xdr, nfs4_pathconf_bitmap, args->bitmask,
			ARRAY_SIZE(nfs4_pathconf_bitmap), &hdr);
	encode_nops(&hdr);
}

/*
 * a STATFS request
 */
static void nfs4_xdr_enc_statfs(struct rpc_rqst *req, struct xdr_stream *xdr,
				const void *data)
{
	const struct nfs4_statfs_arg *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_getattr(xdr, nfs4_statfs_bitmap, args->bitmask,
			ARRAY_SIZE(nfs4_statfs_bitmap), &hdr);
	encode_nops(&hdr);
}

/*
 * GETATTR_BITMAP request
 */
static void nfs4_xdr_enc_server_caps(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     const void *data)
{
	const struct nfs4_server_caps_arg *args = data;
	const u32 *bitmask = args->bitmask;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fhandle, &hdr);
	encode_getattr(xdr, bitmask, NULL, 3, &hdr);
	encode_nops(&hdr);
}

/*
 * a RENEW request
 */
static void nfs4_xdr_enc_renew(struct rpc_rqst *req, struct xdr_stream *xdr,
			       const void *data)

{
	const struct nfs_client *clp = data;
	struct compound_hdr hdr = {
		.nops	= 0,
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_renew(xdr, clp->cl_clientid, &hdr);
	encode_nops(&hdr);
}

/*
 * a SETCLIENTID request
 */
static void nfs4_xdr_enc_setclientid(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     const void *data)
{
	const struct nfs4_setclientid *sc = data;
	struct compound_hdr hdr = {
		.nops	= 0,
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_setclientid(xdr, sc, &hdr);
	encode_nops(&hdr);
}

/*
 * a SETCLIENTID_CONFIRM request
 */
static void nfs4_xdr_enc_setclientid_confirm(struct rpc_rqst *req,
					     struct xdr_stream *xdr,
					     const void *data)
{
	const struct nfs4_setclientid_res *arg = data;
	struct compound_hdr hdr = {
		.nops	= 0,
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_setclientid_confirm(xdr, arg, &hdr);
	encode_nops(&hdr);
}

/*
 * DELEGRETURN request
 */
static void nfs4_xdr_enc_delegreturn(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     const void *data)
{
	const struct nfs4_delegreturnargs *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fhandle, &hdr);
	if (args->lr_args)
		encode_layoutreturn(xdr, args->lr_args, &hdr);
	if (args->bitmask)
		encode_getfattr(xdr, args->bitmask, &hdr);
	encode_delegreturn(xdr, args->stateid, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode FS_LOCATIONS request
 */
static void nfs4_xdr_enc_fs_locations(struct rpc_rqst *req,
				      struct xdr_stream *xdr,
				      const void *data)
{
	const struct nfs4_fs_locations_arg *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};
	uint32_t replen;

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	if (args->migration) {
		encode_putfh(xdr, args->fh, &hdr);
		replen = hdr.replen;
		encode_fs_locations(xdr, args->bitmask, &hdr);
		if (args->renew)
			encode_renew(xdr, args->clientid, &hdr);
	} else {
		encode_putfh(xdr, args->dir_fh, &hdr);
		encode_lookup(xdr, args->name, &hdr);
		replen = hdr.replen;
		encode_fs_locations(xdr, args->bitmask, &hdr);
	}

	rpc_prepare_reply_pages(req, (struct page **)&args->page, 0,
				PAGE_SIZE, replen);
	encode_nops(&hdr);
}

/*
 * Encode SECINFO request
 */
static void nfs4_xdr_enc_secinfo(struct rpc_rqst *req,
				struct xdr_stream *xdr,
				const void *data)
{
	const struct nfs4_secinfo_arg *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->dir_fh, &hdr);
	encode_secinfo(xdr, args->name, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode FSID_PRESENT request
 */
static void nfs4_xdr_enc_fsid_present(struct rpc_rqst *req,
				      struct xdr_stream *xdr,
				      const void *data)
{
	const struct nfs4_fsid_present_arg *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_getfh(xdr, &hdr);
	if (args->renew)
		encode_renew(xdr, args->clientid, &hdr);
	encode_nops(&hdr);
}

#if defined(CONFIG_NFS_V4_1)
/*
 * BIND_CONN_TO_SESSION request
 */
static void nfs4_xdr_enc_bind_conn_to_session(struct rpc_rqst *req,
				struct xdr_stream *xdr,
				const void *data)
{
	const struct nfs41_bind_conn_to_session_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = args->client->cl_mvops->minor_version,
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_bind_conn_to_session(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * EXCHANGE_ID request
 */
static void nfs4_xdr_enc_exchange_id(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     const void *data)
{
	const struct nfs41_exchange_id_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = args->client->cl_mvops->minor_version,
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_exchange_id(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * a CREATE_SESSION request
 */
static void nfs4_xdr_enc_create_session(struct rpc_rqst *req,
					struct xdr_stream *xdr,
					const void *data)
{
	const struct nfs41_create_session_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = args->client->cl_mvops->minor_version,
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_create_session(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * a DESTROY_SESSION request
 */
static void nfs4_xdr_enc_destroy_session(struct rpc_rqst *req,
					 struct xdr_stream *xdr,
					 const void *data)
{
	const struct nfs4_session *session = data;
	struct compound_hdr hdr = {
		.minorversion = session->clp->cl_mvops->minor_version,
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_destroy_session(xdr, session, &hdr);
	encode_nops(&hdr);
}

/*
 * a DESTROY_CLIENTID request
 */
static void nfs4_xdr_enc_destroy_clientid(struct rpc_rqst *req,
					 struct xdr_stream *xdr,
					 const void *data)
{
	const struct nfs_client *clp = data;
	struct compound_hdr hdr = {
		.minorversion = clp->cl_mvops->minor_version,
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_destroy_clientid(xdr, clp->cl_clientid, &hdr);
	encode_nops(&hdr);
}

/*
 * a SEQUENCE request
 */
static void nfs4_xdr_enc_sequence(struct rpc_rqst *req, struct xdr_stream *xdr,
				  const void *data)
{
	const struct nfs4_sequence_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, args, &hdr);
	encode_nops(&hdr);
}

#endif

/*
 * a GET_LEASE_TIME request
 */
static void nfs4_xdr_enc_get_lease_time(struct rpc_rqst *req,
					struct xdr_stream *xdr,
					const void *data)
{
	const struct nfs4_get_lease_time_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->la_seq_args),
	};
	const u32 lease_bitmap[3] = { FATTR4_WORD0_LEASE_TIME };

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->la_seq_args, &hdr);
	encode_putrootfh(xdr, &hdr);
	encode_fsinfo(xdr, lease_bitmap, &hdr);
	encode_nops(&hdr);
}

#ifdef CONFIG_NFS_V4_1

/*
 * a RECLAIM_COMPLETE request
 */
static void nfs4_xdr_enc_reclaim_complete(struct rpc_rqst *req,
					  struct xdr_stream *xdr,
					  const void *data)
{
	const struct nfs41_reclaim_complete_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args)
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_reclaim_complete(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode GETDEVICEINFO request
 */
static void nfs4_xdr_enc_getdeviceinfo(struct rpc_rqst *req,
				       struct xdr_stream *xdr,
				       const void *data)
{
	const struct nfs4_getdeviceinfo_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};
	uint32_t replen;

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);

	replen = hdr.replen + op_decode_hdr_maxsz + 2;

	encode_getdeviceinfo(xdr, args, &hdr);

	/* set up reply kvec. device_addr4 opaque data is read into the
	 * pages */
	rpc_prepare_reply_pages(req, args->pdev->pages, args->pdev->pgbase,
				args->pdev->pglen, replen);
	encode_nops(&hdr);
}

/*
 *  Encode LAYOUTGET request
 */
static void nfs4_xdr_enc_layoutget(struct rpc_rqst *req,
				   struct xdr_stream *xdr,
				   const void *data)
{
	const struct nfs4_layoutget_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, NFS_FH(args->inode), &hdr);
	encode_layoutget(xdr, args, &hdr);

	rpc_prepare_reply_pages(req, args->layout.pages, 0,
				args->layout.pglen, hdr.replen - pagepad_maxsz);
	encode_nops(&hdr);
}

/*
 *  Encode LAYOUTCOMMIT request
 */
static void nfs4_xdr_enc_layoutcommit(struct rpc_rqst *req,
				      struct xdr_stream *xdr,
				      const void *priv)
{
	const struct nfs4_layoutcommit_args *args = priv;
	struct nfs4_layoutcommit_data *data =
		container_of(args, struct nfs4_layoutcommit_data, args);
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, NFS_FH(args->inode), &hdr);
	encode_layoutcommit(xdr, data->args.inode, args, &hdr);
	encode_getfattr(xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode LAYOUTRETURN request
 */
static void nfs4_xdr_enc_layoutreturn(struct rpc_rqst *req,
				      struct xdr_stream *xdr,
				      const void *data)
{
	const struct nfs4_layoutreturn_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, NFS_FH(args->inode), &hdr);
	encode_layoutreturn(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode SECINFO_NO_NAME request
 */
static void nfs4_xdr_enc_secinfo_no_name(struct rpc_rqst *req,
					struct xdr_stream *xdr,
					const void *data)
{
	const struct nfs41_secinfo_no_name_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putrootfh(xdr, &hdr);
	encode_secinfo_no_name(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 *  Encode TEST_STATEID request
 */
static void nfs4_xdr_enc_test_stateid(struct rpc_rqst *req,
				      struct xdr_stream *xdr,
				      const void *data)
{
	const struct nfs41_test_stateid_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_test_stateid(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 *  Encode FREE_STATEID request
 */
static void nfs4_xdr_enc_free_stateid(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     const void *data)
{
	const struct nfs41_free_stateid_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_free_stateid(xdr, args, &hdr);
	encode_nops(&hdr);
}
#endif /* CONFIG_NFS_V4_1 */

static int decode_opaque_inline(struct xdr_stream *xdr, unsigned int *len, char **string)
{
	ssize_t ret = xdr_stream_decode_opaque_inline(xdr, (void **)string,
			NFS4_OPAQUE_LIMIT);
	if (unlikely(ret < 0))
		return -EIO;
	*len = ret;
	return 0;
}

static int decode_compound_hdr(struct xdr_stream *xdr, struct compound_hdr *hdr)
{
	ssize_t ret;
	void *ptr;
	u32 tmp;

	if (xdr_stream_decode_u32(xdr, &tmp) < 0)
		return -EIO;
	hdr->status = tmp;

	ret = xdr_stream_decode_opaque_inline(xdr, &ptr, NFS4_OPAQUE_LIMIT);
	if (ret < 0)
		return -EIO;
	hdr->taglen = ret;
	hdr->tag = ptr;

	if (xdr_stream_decode_u32(xdr, &tmp) < 0)
		return -EIO;
	hdr->nops = tmp;
	if (unlikely(hdr->nops < 1))
		return nfs4_stat_to_errno(hdr->status);
	return 0;
}

static bool __decode_op_hdr(struct xdr_stream *xdr, enum nfs_opnum4 expected,
		int *nfs_retval)
{
	__be32 *p;
	uint32_t opnum;
	int32_t nfserr;

	p = xdr_inline_decode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	opnum = be32_to_cpup(p++);
	if (unlikely(opnum != expected))
		goto out_bad_operation;
	if (unlikely(*p != cpu_to_be32(NFS_OK)))
		goto out_status;
	*nfs_retval = 0;
	return true;
out_status:
	nfserr = be32_to_cpup(p);
	trace_nfs4_xdr_status(xdr, opnum, nfserr);
	*nfs_retval = nfs4_stat_to_errno(nfserr);
	return true;
out_bad_operation:
	trace_nfs4_xdr_bad_operation(xdr, opnum, expected);
	*nfs_retval = -EREMOTEIO;
	return false;
out_overflow:
	*nfs_retval = -EIO;
	return false;
}

static int decode_op_hdr(struct xdr_stream *xdr, enum nfs_opnum4 expected)
{
	int retval;

	__decode_op_hdr(xdr, expected, &retval);
	return retval;
}

/* Dummy routine */
static int decode_ace(struct xdr_stream *xdr, void *ace)
{
	__be32 *p;
	unsigned int strlen;
	char *str;

	p = xdr_inline_decode(xdr, 12);
	if (unlikely(!p))
		return -EIO;
	return decode_opaque_inline(xdr, &strlen, &str);
}

static ssize_t
decode_bitmap4(struct xdr_stream *xdr, uint32_t *bitmap, size_t sz)
{
	ssize_t ret;

	ret = xdr_stream_decode_uint32_array(xdr, bitmap, sz);
	if (likely(ret >= 0))
		return ret;
	if (ret != -EMSGSIZE)
		return -EIO;
	return sz;
}

static int decode_attr_bitmap(struct xdr_stream *xdr, uint32_t *bitmap)
{
	ssize_t ret;
	ret = decode_bitmap4(xdr, bitmap, 3);
	return ret < 0 ? ret : 0;
}

static int decode_attr_length(struct xdr_stream *xdr, uint32_t *attrlen, unsigned int *savep)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;
	*attrlen = be32_to_cpup(p);
	*savep = xdr_stream_pos(xdr);
	return 0;
}

static int decode_attr_supported(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *bitmask)
{
	if (likely(bitmap[0] & FATTR4_WORD0_SUPPORTED_ATTRS)) {
		int ret;
		ret = decode_attr_bitmap(xdr, bitmask);
		if (unlikely(ret < 0))
			return ret;
		bitmap[0] &= ~FATTR4_WORD0_SUPPORTED_ATTRS;
	} else
		bitmask[0] = bitmask[1] = bitmask[2] = 0;
	dprintk("%s: bitmask=%08x:%08x:%08x\n", __func__,
		bitmask[0], bitmask[1], bitmask[2]);
	return 0;
}

static int decode_attr_type(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *type)
{
	__be32 *p;
	int ret = 0;

	*type = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_TYPE - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_TYPE)) {
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EIO;
		*type = be32_to_cpup(p);
		if (*type < NF4REG || *type > NF4NAMEDATTR) {
			dprintk("%s: bad type %d\n", __func__, *type);
			return -EIO;
		}
		bitmap[0] &= ~FATTR4_WORD0_TYPE;
		ret = NFS_ATTR_FATTR_TYPE;
	}
	dprintk("%s: type=0%o\n", __func__, nfs_type2fmt[*type]);
	return ret;
}

static int decode_attr_fh_expire_type(struct xdr_stream *xdr,
				      uint32_t *bitmap, uint32_t *type)
{
	__be32 *p;

	*type = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_FH_EXPIRE_TYPE - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_FH_EXPIRE_TYPE)) {
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EIO;
		*type = be32_to_cpup(p);
		bitmap[0] &= ~FATTR4_WORD0_FH_EXPIRE_TYPE;
	}
	dprintk("%s: expire type=0x%x\n", __func__, *type);
	return 0;
}

static int decode_attr_change(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *change)
{
	__be32 *p;
	int ret = 0;

	*change = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_CHANGE - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_CHANGE)) {
		p = xdr_inline_decode(xdr, 8);
		if (unlikely(!p))
			return -EIO;
		xdr_decode_hyper(p, change);
		bitmap[0] &= ~FATTR4_WORD0_CHANGE;
		ret = NFS_ATTR_FATTR_CHANGE;
	}
	dprintk("%s: change attribute=%Lu\n", __func__,
			(unsigned long long)*change);
	return ret;
}

static int decode_attr_size(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *size)
{
	__be32 *p;
	int ret = 0;

	*size = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_SIZE - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_SIZE)) {
		p = xdr_inline_decode(xdr, 8);
		if (unlikely(!p))
			return -EIO;
		xdr_decode_hyper(p, size);
		bitmap[0] &= ~FATTR4_WORD0_SIZE;
		ret = NFS_ATTR_FATTR_SIZE;
	}
	dprintk("%s: file size=%Lu\n", __func__, (unsigned long long)*size);
	return ret;
}

static int decode_attr_link_support(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *res)
{
	__be32 *p;

	*res = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_LINK_SUPPORT - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_LINK_SUPPORT)) {
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EIO;
		*res = be32_to_cpup(p);
		bitmap[0] &= ~FATTR4_WORD0_LINK_SUPPORT;
	}
	dprintk("%s: link support=%s\n", __func__, *res == 0 ? "false" : "true");
	return 0;
}

static int decode_attr_symlink_support(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *res)
{
	__be32 *p;

	*res = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_SYMLINK_SUPPORT - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_SYMLINK_SUPPORT)) {
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EIO;
		*res = be32_to_cpup(p);
		bitmap[0] &= ~FATTR4_WORD0_SYMLINK_SUPPORT;
	}
	dprintk("%s: symlink support=%s\n", __func__, *res == 0 ? "false" : "true");
	return 0;
}

static int decode_attr_fsid(struct xdr_stream *xdr, uint32_t *bitmap, struct nfs_fsid *fsid)
{
	__be32 *p;
	int ret = 0;

	fsid->major = 0;
	fsid->minor = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_FSID - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_FSID)) {
		p = xdr_inline_decode(xdr, 16);
		if (unlikely(!p))
			return -EIO;
		p = xdr_decode_hyper(p, &fsid->major);
		xdr_decode_hyper(p, &fsid->minor);
		bitmap[0] &= ~FATTR4_WORD0_FSID;
		ret = NFS_ATTR_FATTR_FSID;
	}
	dprintk("%s: fsid=(0x%Lx/0x%Lx)\n", __func__,
			(unsigned long long)fsid->major,
			(unsigned long long)fsid->minor);
	return ret;
}

static int decode_attr_lease_time(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *res)
{
	__be32 *p;

	*res = 60;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_LEASE_TIME - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_LEASE_TIME)) {
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EIO;
		*res = be32_to_cpup(p);
		bitmap[0] &= ~FATTR4_WORD0_LEASE_TIME;
	}
	dprintk("%s: lease time=%u\n", __func__, (unsigned int)*res);
	return 0;
}

static int decode_attr_error(struct xdr_stream *xdr, uint32_t *bitmap, int32_t *res)
{
	__be32 *p;

	if (unlikely(bitmap[0] & (FATTR4_WORD0_RDATTR_ERROR - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_RDATTR_ERROR)) {
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EIO;
		bitmap[0] &= ~FATTR4_WORD0_RDATTR_ERROR;
		*res = -be32_to_cpup(p);
	}
	return 0;
}

static int decode_attr_exclcreat_supported(struct xdr_stream *xdr,
				 uint32_t *bitmap, uint32_t *bitmask)
{
	if (likely(bitmap[2] & FATTR4_WORD2_SUPPATTR_EXCLCREAT)) {
		int ret;
		ret = decode_attr_bitmap(xdr, bitmask);
		if (unlikely(ret < 0))
			return ret;
		bitmap[2] &= ~FATTR4_WORD2_SUPPATTR_EXCLCREAT;
	} else
		bitmask[0] = bitmask[1] = bitmask[2] = 0;
	dprintk("%s: bitmask=%08x:%08x:%08x\n", __func__,
		bitmask[0], bitmask[1], bitmask[2]);
	return 0;
}

static int decode_attr_filehandle(struct xdr_stream *xdr, uint32_t *bitmap, struct nfs_fh *fh)
{
	__be32 *p;
	u32 len;

	if (fh != NULL)
		memset(fh, 0, sizeof(*fh));

	if (unlikely(bitmap[0] & (FATTR4_WORD0_FILEHANDLE - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_FILEHANDLE)) {
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EIO;
		len = be32_to_cpup(p);
		if (len > NFS4_FHSIZE || len == 0) {
			trace_nfs4_xdr_bad_filehandle(xdr, OP_READDIR,
						      NFS4ERR_BADHANDLE);
			return -EREMOTEIO;
		}
		p = xdr_inline_decode(xdr, len);
		if (unlikely(!p))
			return -EIO;
		if (fh != NULL) {
			memcpy(fh->data, p, len);
			fh->size = len;
		}
		bitmap[0] &= ~FATTR4_WORD0_FILEHANDLE;
	}
	return 0;
}

static int decode_attr_aclsupport(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *res)
{
	__be32 *p;

	*res = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_ACLSUPPORT - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_ACLSUPPORT)) {
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EIO;
		*res = be32_to_cpup(p);
		bitmap[0] &= ~FATTR4_WORD0_ACLSUPPORT;
	}
	dprintk("%s: ACLs supported=%u\n", __func__, (unsigned int)*res);
	return 0;
}

static int decode_attr_case_insensitive(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *res)
{
	__be32 *p;

	*res = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_CASE_INSENSITIVE - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_CASE_INSENSITIVE)) {
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EIO;
		*res = be32_to_cpup(p);
		bitmap[0] &= ~FATTR4_WORD0_CASE_INSENSITIVE;
	}
	dprintk("%s: case_insensitive=%s\n", __func__, *res == 0 ? "false" : "true");
	return 0;
}

static int decode_attr_case_preserving(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *res)
{
	__be32 *p;

	*res = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_CASE_PRESERVING - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_CASE_PRESERVING)) {
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EIO;
		*res = be32_to_cpup(p);
		bitmap[0] &= ~FATTR4_WORD0_CASE_PRESERVING;
	}
	dprintk("%s: case_preserving=%s\n", __func__, *res == 0 ? "false" : "true");
	return 0;
}

static int decode_attr_fileid(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *fileid)
{
	__be32 *p;
	int ret = 0;

	*fileid = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_FILEID - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_FILEID)) {
		p = xdr_inline_decode(xdr, 8);
		if (unlikely(!p))
			return -EIO;
		xdr_decode_hyper(p, fileid);
		bitmap[0] &= ~FATTR4_WORD0_FILEID;
		ret = NFS_ATTR_FATTR_FILEID;
	}
	dprintk("%s: fileid=%Lu\n", __func__, (unsigned long long)*fileid);
	return ret;
}

static int decode_attr_mounted_on_fileid(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *fileid)
{
	__be32 *p;
	int ret = 0;

	*fileid = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_MOUNTED_ON_FILEID - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_MOUNTED_ON_FILEID)) {
		p = xdr_inline_decode(xdr, 8);
		if (unlikely(!p))
			return -EIO;
		xdr_decode_hyper(p, fileid);
		bitmap[1] &= ~FATTR4_WORD1_MOUNTED_ON_FILEID;
		ret = NFS_ATTR_FATTR_MOUNTED_ON_FILEID;
	}
	dprintk("%s: fileid=%Lu\n", __func__, (unsigned long long)*fileid);
	return ret;
}

static int decode_attr_files_avail(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *res)
{
	__be32 *p;
	int status = 0;

	*res = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_FILES_AVAIL - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_FILES_AVAIL)) {
		p = xdr_inline_decode(xdr, 8);
		if (unlikely(!p))
			return -EIO;
		xdr_decode_hyper(p, res);
		bitmap[0] &= ~FATTR4_WORD0_FILES_AVAIL;
	}
	dprintk("%s: files avail=%Lu\n", __func__, (unsigned long long)*res);
	return status;
}

static int decode_attr_files_free(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *res)
{
	__be32 *p;
	int status = 0;

	*res = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_FILES_FREE - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_FILES_FREE)) {
		p = xdr_inline_decode(xdr, 8);
		if (unlikely(!p))
			return -EIO;
		xdr_decode_hyper(p, res);
		bitmap[0] &= ~FATTR4_WORD0_FILES_FREE;
	}
	dprintk("%s: files free=%Lu\n", __func__, (unsigned long long)*res);
	return status;
}

static int decode_attr_files_total(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *res)
{
	__be32 *p;
	int status = 0;

	*res = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_FILES_TOTAL - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_FILES_TOTAL)) {
		p = xdr_inline_decode(xdr, 8);
		if (unlikely(!p))
			return -EIO;
		xdr_decode_hyper(p, res);
		bitmap[0] &= ~FATTR4_WORD0_FILES_TOTAL;
	}
	dprintk("%s: files total=%Lu\n", __func__, (unsigned long long)*res);
	return status;
}

static int decode_pathname(struct xdr_stream *xdr, struct nfs4_pathname *path)
{
	u32 n;
	__be32 *p;
	int status = 0;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;
	n = be32_to_cpup(p);
	if (n == 0)
		goto root_path;
	dprintk("pathname4: ");
	if (n > NFS4_PATHNAME_MAXCOMPONENTS) {
		dprintk("cannot parse %d components in path\n", n);
		goto out_eio;
	}
	for (path->ncomponents = 0; path->ncomponents < n; path->ncomponents++) {
		struct nfs4_string *component = &path->components[path->ncomponents];
		status = decode_opaque_inline(xdr, &component->len, &component->data);
		if (unlikely(status != 0))
			goto out_eio;
		ifdebug (XDR)
			pr_cont("%s%.*s ",
				(path->ncomponents != n ? "/ " : ""),
				component->len, component->data);
	}
out:
	return status;
root_path:
/* a root pathname is sent as a zero component4 */
	path->ncomponents = 1;
	path->components[0].len=0;
	path->components[0].data=NULL;
	dprintk("pathname4: /\n");
	goto out;
out_eio:
	dprintk(" status %d", status);
	status = -EIO;
	goto out;
}

static int decode_attr_fs_locations(struct xdr_stream *xdr, uint32_t *bitmap, struct nfs4_fs_locations *res)
{
	int n;
	__be32 *p;
	int status = -EIO;

	if (unlikely(bitmap[0] & (FATTR4_WORD0_FS_LOCATIONS -1U)))
		goto out;
	status = 0;
	if (unlikely(!(bitmap[0] & FATTR4_WORD0_FS_LOCATIONS)))
		goto out;
	bitmap[0] &= ~FATTR4_WORD0_FS_LOCATIONS;
	status = -EIO;
	/* Ignore borken servers that return unrequested attrs */
	if (unlikely(res == NULL))
		goto out;
	dprintk("%s: fsroot:\n", __func__);
	status = decode_pathname(xdr, &res->fs_path);
	if (unlikely(status != 0))
		goto out;
	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		goto out_eio;
	n = be32_to_cpup(p);
	for (res->nlocations = 0; res->nlocations < n; res->nlocations++) {
		u32 m;
		struct nfs4_fs_location *loc;

		if (res->nlocations == NFS4_FS_LOCATIONS_MAXENTRIES)
			break;
		loc = &res->locations[res->nlocations];
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			goto out_eio;
		m = be32_to_cpup(p);

		dprintk("%s: servers:\n", __func__);
		for (loc->nservers = 0; loc->nservers < m; loc->nservers++) {
			struct nfs4_string *server;

			if (loc->nservers == NFS4_FS_LOCATION_MAXSERVERS) {
				unsigned int i;
				dprintk("%s: using first %u of %u servers "
					"returned for location %u\n",
						__func__,
						NFS4_FS_LOCATION_MAXSERVERS,
						m, res->nlocations);
				for (i = loc->nservers; i < m; i++) {
					unsigned int len;
					char *data;
					status = decode_opaque_inline(xdr, &len, &data);
					if (unlikely(status != 0))
						goto out_eio;
				}
				break;
			}
			server = &loc->servers[loc->nservers];
			status = decode_opaque_inline(xdr, &server->len, &server->data);
			if (unlikely(status != 0))
				goto out_eio;
			dprintk("%s ", server->data);
		}
		status = decode_pathname(xdr, &loc->rootpath);
		if (unlikely(status != 0))
			goto out_eio;
	}
	if (res->nlocations != 0)
		status = NFS_ATTR_FATTR_V4_LOCATIONS;
out:
	dprintk("%s: fs_locations done, error = %d\n", __func__, status);
	return status;
out_eio:
	status = -EIO;
	goto out;
}

static int decode_attr_maxfilesize(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *res)
{
	__be32 *p;
	int status = 0;

	*res = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_MAXFILESIZE - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_MAXFILESIZE)) {
		p = xdr_inline_decode(xdr, 8);
		if (unlikely(!p))
			return -EIO;
		xdr_decode_hyper(p, res);
		bitmap[0] &= ~FATTR4_WORD0_MAXFILESIZE;
	}
	dprintk("%s: maxfilesize=%Lu\n", __func__, (unsigned long long)*res);
	return status;
}

static int decode_attr_maxlink(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *maxlink)
{
	__be32 *p;
	int status = 0;

	*maxlink = 1;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_MAXLINK - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_MAXLINK)) {
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EIO;
		*maxlink = be32_to_cpup(p);
		bitmap[0] &= ~FATTR4_WORD0_MAXLINK;
	}
	dprintk("%s: maxlink=%u\n", __func__, *maxlink);
	return status;
}

static int decode_attr_maxname(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *maxname)
{
	__be32 *p;
	int status = 0;

	*maxname = 1024;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_MAXNAME - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_MAXNAME)) {
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EIO;
		*maxname = be32_to_cpup(p);
		bitmap[0] &= ~FATTR4_WORD0_MAXNAME;
	}
	dprintk("%s: maxname=%u\n", __func__, *maxname);
	return status;
}

static int decode_attr_maxread(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *res)
{
	__be32 *p;
	int status = 0;

	*res = 1024;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_MAXREAD - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_MAXREAD)) {
		uint64_t maxread;
		p = xdr_inline_decode(xdr, 8);
		if (unlikely(!p))
			return -EIO;
		xdr_decode_hyper(p, &maxread);
		if (maxread > 0x7FFFFFFF)
			maxread = 0x7FFFFFFF;
		*res = (uint32_t)maxread;
		bitmap[0] &= ~FATTR4_WORD0_MAXREAD;
	}
	dprintk("%s: maxread=%lu\n", __func__, (unsigned long)*res);
	return status;
}

static int decode_attr_maxwrite(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *res)
{
	__be32 *p;
	int status = 0;

	*res = 1024;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_MAXWRITE - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_MAXWRITE)) {
		uint64_t maxwrite;
		p = xdr_inline_decode(xdr, 8);
		if (unlikely(!p))
			return -EIO;
		xdr_decode_hyper(p, &maxwrite);
		if (maxwrite > 0x7FFFFFFF)
			maxwrite = 0x7FFFFFFF;
		*res = (uint32_t)maxwrite;
		bitmap[0] &= ~FATTR4_WORD0_MAXWRITE;
	}
	dprintk("%s: maxwrite=%lu\n", __func__, (unsigned long)*res);
	return status;
}

static int decode_attr_mode(struct xdr_stream *xdr, uint32_t *bitmap, umode_t *mode)
{
	uint32_t tmp;
	__be32 *p;
	int ret = 0;

	*mode = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_MODE - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_MODE)) {
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EIO;
		tmp = be32_to_cpup(p);
		*mode = tmp & ~S_IFMT;
		bitmap[1] &= ~FATTR4_WORD1_MODE;
		ret = NFS_ATTR_FATTR_MODE;
	}
	dprintk("%s: file mode=0%o\n", __func__, (unsigned int)*mode);
	return ret;
}

static int decode_attr_nlink(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *nlink)
{
	__be32 *p;
	int ret = 0;

	*nlink = 1;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_NUMLINKS - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_NUMLINKS)) {
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EIO;
		*nlink = be32_to_cpup(p);
		bitmap[1] &= ~FATTR4_WORD1_NUMLINKS;
		ret = NFS_ATTR_FATTR_NLINK;
	}
	dprintk("%s: nlink=%u\n", __func__, (unsigned int)*nlink);
	return ret;
}

static ssize_t decode_nfs4_string(struct xdr_stream *xdr,
		struct nfs4_string *name, gfp_t gfp_flags)
{
	ssize_t ret;

	ret = xdr_stream_decode_string_dup(xdr, &name->data,
			XDR_MAX_NETOBJ, gfp_flags);
	name->len = 0;
	if (ret > 0)
		name->len = ret;
	return ret;
}

static int decode_attr_owner(struct xdr_stream *xdr, uint32_t *bitmap,
		const struct nfs_server *server, kuid_t *uid,
		struct nfs4_string *owner_name)
{
	ssize_t len;
	char *p;

	*uid = make_kuid(&init_user_ns, -2);
	if (unlikely(bitmap[1] & (FATTR4_WORD1_OWNER - 1U)))
		return -EIO;
	if (!(bitmap[1] & FATTR4_WORD1_OWNER))
		return 0;
	bitmap[1] &= ~FATTR4_WORD1_OWNER;

	if (owner_name != NULL) {
		len = decode_nfs4_string(xdr, owner_name, GFP_NOIO);
		if (len <= 0)
			goto out;
		dprintk("%s: name=%s\n", __func__, owner_name->data);
		return NFS_ATTR_FATTR_OWNER_NAME;
	} else {
		len = xdr_stream_decode_opaque_inline(xdr, (void **)&p,
				XDR_MAX_NETOBJ);
		if (len <= 0 || nfs_map_name_to_uid(server, p, len, uid) != 0)
			goto out;
		dprintk("%s: uid=%d\n", __func__, (int)from_kuid(&init_user_ns, *uid));
		return NFS_ATTR_FATTR_OWNER;
	}
out:
	if (len == -EBADMSG)
		return -EIO;
	return 0;
}

static int decode_attr_group(struct xdr_stream *xdr, uint32_t *bitmap,
		const struct nfs_server *server, kgid_t *gid,
		struct nfs4_string *group_name)
{
	ssize_t len;
	char *p;

	*gid = make_kgid(&init_user_ns, -2);
	if (unlikely(bitmap[1] & (FATTR4_WORD1_OWNER_GROUP - 1U)))
		return -EIO;
	if (!(bitmap[1] & FATTR4_WORD1_OWNER_GROUP))
		return 0;
	bitmap[1] &= ~FATTR4_WORD1_OWNER_GROUP;

	if (group_name != NULL) {
		len = decode_nfs4_string(xdr, group_name, GFP_NOIO);
		if (len <= 0)
			goto out;
		dprintk("%s: name=%s\n", __func__, group_name->data);
		return NFS_ATTR_FATTR_GROUP_NAME;
	} else {
		len = xdr_stream_decode_opaque_inline(xdr, (void **)&p,
				XDR_MAX_NETOBJ);
		if (len <= 0 || nfs_map_group_to_gid(server, p, len, gid) != 0)
			goto out;
		dprintk("%s: gid=%d\n", __func__, (int)from_kgid(&init_user_ns, *gid));
		return NFS_ATTR_FATTR_GROUP;
	}
out:
	if (len == -EBADMSG)
		return -EIO;
	return 0;
}

static int decode_attr_rdev(struct xdr_stream *xdr, uint32_t *bitmap, dev_t *rdev)
{
	uint32_t major = 0, minor = 0;
	__be32 *p;
	int ret = 0;

	*rdev = MKDEV(0,0);
	if (unlikely(bitmap[1] & (FATTR4_WORD1_RAWDEV - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_RAWDEV)) {
		dev_t tmp;

		p = xdr_inline_decode(xdr, 8);
		if (unlikely(!p))
			return -EIO;
		major = be32_to_cpup(p++);
		minor = be32_to_cpup(p);
		tmp = MKDEV(major, minor);
		if (MAJOR(tmp) == major && MINOR(tmp) == minor)
			*rdev = tmp;
		bitmap[1] &= ~ FATTR4_WORD1_RAWDEV;
		ret = NFS_ATTR_FATTR_RDEV;
	}
	dprintk("%s: rdev=(0x%x:0x%x)\n", __func__, major, minor);
	return ret;
}

static int decode_attr_space_avail(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *res)
{
	__be32 *p;
	int status = 0;

	*res = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_SPACE_AVAIL - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_SPACE_AVAIL)) {
		p = xdr_inline_decode(xdr, 8);
		if (unlikely(!p))
			return -EIO;
		xdr_decode_hyper(p, res);
		bitmap[1] &= ~FATTR4_WORD1_SPACE_AVAIL;
	}
	dprintk("%s: space avail=%Lu\n", __func__, (unsigned long long)*res);
	return status;
}

static int decode_attr_space_free(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *res)
{
	__be32 *p;
	int status = 0;

	*res = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_SPACE_FREE - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_SPACE_FREE)) {
		p = xdr_inline_decode(xdr, 8);
		if (unlikely(!p))
			return -EIO;
		xdr_decode_hyper(p, res);
		bitmap[1] &= ~FATTR4_WORD1_SPACE_FREE;
	}
	dprintk("%s: space free=%Lu\n", __func__, (unsigned long long)*res);
	return status;
}

static int decode_attr_space_total(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *res)
{
	__be32 *p;
	int status = 0;

	*res = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_SPACE_TOTAL - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_SPACE_TOTAL)) {
		p = xdr_inline_decode(xdr, 8);
		if (unlikely(!p))
			return -EIO;
		xdr_decode_hyper(p, res);
		bitmap[1] &= ~FATTR4_WORD1_SPACE_TOTAL;
	}
	dprintk("%s: space total=%Lu\n", __func__, (unsigned long long)*res);
	return status;
}

static int decode_attr_space_used(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *used)
{
	__be32 *p;
	int ret = 0;

	*used = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_SPACE_USED - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_SPACE_USED)) {
		p = xdr_inline_decode(xdr, 8);
		if (unlikely(!p))
			return -EIO;
		xdr_decode_hyper(p, used);
		bitmap[1] &= ~FATTR4_WORD1_SPACE_USED;
		ret = NFS_ATTR_FATTR_SPACE_USED;
	}
	dprintk("%s: space used=%Lu\n", __func__,
			(unsigned long long)*used);
	return ret;
}

static __be32 *
xdr_decode_nfstime4(__be32 *p, struct timespec64 *t)
{
	__u64 sec;

	p = xdr_decode_hyper(p, &sec);
	t-> tv_sec = sec;
	t->tv_nsec = be32_to_cpup(p++);
	return p;
}

static int decode_attr_time(struct xdr_stream *xdr, struct timespec64 *time)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, nfstime4_maxsz << 2);
	if (unlikely(!p))
		return -EIO;
	xdr_decode_nfstime4(p, time);
	return 0;
}

static int decode_attr_time_access(struct xdr_stream *xdr, uint32_t *bitmap, struct timespec64 *time)
{
	int status = 0;

	time->tv_sec = 0;
	time->tv_nsec = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_TIME_ACCESS - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_TIME_ACCESS)) {
		status = decode_attr_time(xdr, time);
		if (status == 0)
			status = NFS_ATTR_FATTR_ATIME;
		bitmap[1] &= ~FATTR4_WORD1_TIME_ACCESS;
	}
	dprintk("%s: atime=%lld\n", __func__, time->tv_sec);
	return status;
}

static int decode_attr_time_metadata(struct xdr_stream *xdr, uint32_t *bitmap, struct timespec64 *time)
{
	int status = 0;

	time->tv_sec = 0;
	time->tv_nsec = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_TIME_METADATA - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_TIME_METADATA)) {
		status = decode_attr_time(xdr, time);
		if (status == 0)
			status = NFS_ATTR_FATTR_CTIME;
		bitmap[1] &= ~FATTR4_WORD1_TIME_METADATA;
	}
	dprintk("%s: ctime=%lld\n", __func__, time->tv_sec);
	return status;
}

static int decode_attr_time_delta(struct xdr_stream *xdr, uint32_t *bitmap,
				  struct timespec64 *time)
{
	int status = 0;

	time->tv_sec = 0;
	time->tv_nsec = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_TIME_DELTA - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_TIME_DELTA)) {
		status = decode_attr_time(xdr, time);
		bitmap[1] &= ~FATTR4_WORD1_TIME_DELTA;
	}
	dprintk("%s: time_delta=%lld %ld\n", __func__, time->tv_sec,
		time->tv_nsec);
	return status;
}

static int decode_attr_security_label(struct xdr_stream *xdr, uint32_t *bitmap,
					struct nfs4_label *label)
{
	uint32_t pi = 0;
	uint32_t lfs = 0;
	__u32 len;
	__be32 *p;
	int status = 0;

	if (unlikely(bitmap[2] & (FATTR4_WORD2_SECURITY_LABEL - 1U)))
		return -EIO;
	if (likely(bitmap[2] & FATTR4_WORD2_SECURITY_LABEL)) {
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EIO;
		lfs = be32_to_cpup(p++);
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EIO;
		pi = be32_to_cpup(p++);
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EIO;
		len = be32_to_cpup(p++);
		p = xdr_inline_decode(xdr, len);
		if (unlikely(!p))
			return -EIO;
		if (len < NFS4_MAXLABELLEN) {
			if (label) {
				if (label->len) {
					if (label->len < len)
						return -ERANGE;
					memcpy(label->label, p, len);
				}
				label->len = len;
				label->pi = pi;
				label->lfs = lfs;
				status = NFS_ATTR_FATTR_V4_SECURITY_LABEL;
			}
			bitmap[2] &= ~FATTR4_WORD2_SECURITY_LABEL;
		} else
			printk(KERN_WARNING "%s: label too long (%u)!\n",
					__func__, len);
		if (label && label->label)
			dprintk("%s: label=%.*s, len=%d, PI=%d, LFS=%d\n",
				__func__, label->len, (char *)label->label,
				label->len, label->pi, label->lfs);
	}
	return status;
}

static int decode_attr_time_modify(struct xdr_stream *xdr, uint32_t *bitmap, struct timespec64 *time)
{
	int status = 0;

	time->tv_sec = 0;
	time->tv_nsec = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_TIME_MODIFY - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_TIME_MODIFY)) {
		status = decode_attr_time(xdr, time);
		if (status == 0)
			status = NFS_ATTR_FATTR_MTIME;
		bitmap[1] &= ~FATTR4_WORD1_TIME_MODIFY;
	}
	dprintk("%s: mtime=%lld\n", __func__, time->tv_sec);
	return status;
}

static int decode_attr_xattrsupport(struct xdr_stream *xdr, uint32_t *bitmap,
				    uint32_t *res)
{
	__be32 *p;

	*res = 0;
	if (unlikely(bitmap[2] & (FATTR4_WORD2_XATTR_SUPPORT - 1U)))
		return -EIO;
	if (likely(bitmap[2] & FATTR4_WORD2_XATTR_SUPPORT)) {
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EIO;
		*res = be32_to_cpup(p);
		bitmap[2] &= ~FATTR4_WORD2_XATTR_SUPPORT;
	}
	dprintk("%s: XATTR support=%s\n", __func__,
		*res == 0 ? "false" : "true");
	return 0;
}

static int verify_attr_len(struct xdr_stream *xdr, unsigned int savep, uint32_t attrlen)
{
	unsigned int attrwords = XDR_QUADLEN(attrlen);
	unsigned int nwords = (xdr_stream_pos(xdr) - savep) >> 2;

	if (unlikely(attrwords != nwords)) {
		dprintk("%s: server returned incorrect attribute length: "
			"%u %c %u\n",
				__func__,
				attrwords << 2,
				(attrwords < nwords) ? '<' : '>',
				nwords << 2);
		return -EIO;
	}
	return 0;
}

static int decode_change_info(struct xdr_stream *xdr, struct nfs4_change_info *cinfo)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, 20);
	if (unlikely(!p))
		return -EIO;
	cinfo->atomic = be32_to_cpup(p++);
	p = xdr_decode_hyper(p, &cinfo->before);
	xdr_decode_hyper(p, &cinfo->after);
	return 0;
}

static int decode_access(struct xdr_stream *xdr, u32 *supported, u32 *access)
{
	__be32 *p;
	uint32_t supp, acc;
	int status;

	status = decode_op_hdr(xdr, OP_ACCESS);
	if (status)
		return status;
	p = xdr_inline_decode(xdr, 8);
	if (unlikely(!p))
		return -EIO;
	supp = be32_to_cpup(p++);
	acc = be32_to_cpup(p);
	*supported = supp;
	*access = acc;
	return 0;
}

static int decode_opaque_fixed(struct xdr_stream *xdr, void *buf, size_t len)
{
	ssize_t ret = xdr_stream_decode_opaque_fixed(xdr, buf, len);
	if (unlikely(ret < 0))
		return -EIO;
	return 0;
}

static int decode_stateid(struct xdr_stream *xdr, nfs4_stateid *stateid)
{
	return decode_opaque_fixed(xdr, stateid, NFS4_STATEID_SIZE);
}

static int decode_open_stateid(struct xdr_stream *xdr, nfs4_stateid *stateid)
{
	stateid->type = NFS4_OPEN_STATEID_TYPE;
	return decode_stateid(xdr, stateid);
}

static int decode_lock_stateid(struct xdr_stream *xdr, nfs4_stateid *stateid)
{
	stateid->type = NFS4_LOCK_STATEID_TYPE;
	return decode_stateid(xdr, stateid);
}

static int decode_delegation_stateid(struct xdr_stream *xdr, nfs4_stateid *stateid)
{
	stateid->type = NFS4_DELEGATION_STATEID_TYPE;
	return decode_stateid(xdr, stateid);
}

static int decode_invalid_stateid(struct xdr_stream *xdr, nfs4_stateid *stateid)
{
	nfs4_stateid dummy;

	nfs4_stateid_copy(stateid, &invalid_stateid);
	return decode_stateid(xdr, &dummy);
}

static int decode_close(struct xdr_stream *xdr, struct nfs_closeres *res)
{
	int status;

	status = decode_op_hdr(xdr, OP_CLOSE);
	if (status != -EIO)
		nfs_increment_open_seqid(status, res->seqid);
	if (!status)
		status = decode_invalid_stateid(xdr, &res->stateid);
	return status;
}

static int decode_verifier(struct xdr_stream *xdr, void *verifier)
{
	return decode_opaque_fixed(xdr, verifier, NFS4_VERIFIER_SIZE);
}

static int decode_write_verifier(struct xdr_stream *xdr, struct nfs_write_verifier *verifier)
{
	return decode_opaque_fixed(xdr, verifier->data, NFS4_VERIFIER_SIZE);
}

static int decode_commit(struct xdr_stream *xdr, struct nfs_commitres *res)
{
	struct nfs_writeverf *verf = res->verf;
	int status;

	status = decode_op_hdr(xdr, OP_COMMIT);
	if (!status)
		status = decode_write_verifier(xdr, &verf->verifier);
	if (!status)
		verf->committed = NFS_FILE_SYNC;
	return status;
}

static int decode_create(struct xdr_stream *xdr, struct nfs4_change_info *cinfo)
{
	__be32 *p;
	uint32_t bmlen;
	int status;

	status = decode_op_hdr(xdr, OP_CREATE);
	if (status)
		return status;
	if ((status = decode_change_info(xdr, cinfo)))
		return status;
	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;
	bmlen = be32_to_cpup(p);
	p = xdr_inline_decode(xdr, bmlen << 2);
	if (likely(p))
		return 0;
	return -EIO;
}

static int decode_server_caps(struct xdr_stream *xdr, struct nfs4_server_caps_res *res)
{
	unsigned int savep;
	uint32_t attrlen, bitmap[3] = {0};
	int status;

	if ((status = decode_op_hdr(xdr, OP_GETATTR)) != 0)
		goto xdr_error;
	if ((status = decode_attr_bitmap(xdr, bitmap)) != 0)
		goto xdr_error;
	if ((status = decode_attr_length(xdr, &attrlen, &savep)) != 0)
		goto xdr_error;
	if ((status = decode_attr_supported(xdr, bitmap, res->attr_bitmask)) != 0)
		goto xdr_error;
	if ((status = decode_attr_fh_expire_type(xdr, bitmap,
						 &res->fh_expire_type)) != 0)
		goto xdr_error;
	if ((status = decode_attr_link_support(xdr, bitmap, &res->has_links)) != 0)
		goto xdr_error;
	if ((status = decode_attr_symlink_support(xdr, bitmap, &res->has_symlinks)) != 0)
		goto xdr_error;
	if ((status = decode_attr_aclsupport(xdr, bitmap, &res->acl_bitmask)) != 0)
		goto xdr_error;
	if ((status = decode_attr_case_insensitive(xdr, bitmap, &res->case_insensitive)) != 0)
		goto xdr_error;
	if ((status = decode_attr_case_preserving(xdr, bitmap, &res->case_preserving)) != 0)
		goto xdr_error;
	if ((status = decode_attr_exclcreat_supported(xdr, bitmap,
				res->exclcreat_bitmask)) != 0)
		goto xdr_error;
	status = verify_attr_len(xdr, savep, attrlen);
xdr_error:
	dprintk("%s: xdr returned %d!\n", __func__, -status);
	return status;
}

static int decode_statfs(struct xdr_stream *xdr, struct nfs_fsstat *fsstat)
{
	unsigned int savep;
	uint32_t attrlen, bitmap[3] = {0};
	int status;

	if ((status = decode_op_hdr(xdr, OP_GETATTR)) != 0)
		goto xdr_error;
	if ((status = decode_attr_bitmap(xdr, bitmap)) != 0)
		goto xdr_error;
	if ((status = decode_attr_length(xdr, &attrlen, &savep)) != 0)
		goto xdr_error;

	if ((status = decode_attr_files_avail(xdr, bitmap, &fsstat->afiles)) != 0)
		goto xdr_error;
	if ((status = decode_attr_files_free(xdr, bitmap, &fsstat->ffiles)) != 0)
		goto xdr_error;
	if ((status = decode_attr_files_total(xdr, bitmap, &fsstat->tfiles)) != 0)
		goto xdr_error;

	status = -EIO;
	if (unlikely(bitmap[0]))
		goto xdr_error;

	if ((status = decode_attr_space_avail(xdr, bitmap, &fsstat->abytes)) != 0)
		goto xdr_error;
	if ((status = decode_attr_space_free(xdr, bitmap, &fsstat->fbytes)) != 0)
		goto xdr_error;
	if ((status = decode_attr_space_total(xdr, bitmap, &fsstat->tbytes)) != 0)
		goto xdr_error;

	status = verify_attr_len(xdr, savep, attrlen);
xdr_error:
	dprintk("%s: xdr returned %d!\n", __func__, -status);
	return status;
}

static int decode_pathconf(struct xdr_stream *xdr, struct nfs_pathconf *pathconf)
{
	unsigned int savep;
	uint32_t attrlen, bitmap[3] = {0};
	int status;

	if ((status = decode_op_hdr(xdr, OP_GETATTR)) != 0)
		goto xdr_error;
	if ((status = decode_attr_bitmap(xdr, bitmap)) != 0)
		goto xdr_error;
	if ((status = decode_attr_length(xdr, &attrlen, &savep)) != 0)
		goto xdr_error;

	if ((status = decode_attr_maxlink(xdr, bitmap, &pathconf->max_link)) != 0)
		goto xdr_error;
	if ((status = decode_attr_maxname(xdr, bitmap, &pathconf->max_namelen)) != 0)
		goto xdr_error;

	status = verify_attr_len(xdr, savep, attrlen);
xdr_error:
	dprintk("%s: xdr returned %d!\n", __func__, -status);
	return status;
}

static int decode_threshold_hint(struct xdr_stream *xdr,
				  uint32_t *bitmap,
				  uint64_t *res,
				  uint32_t hint_bit)
{
	__be32 *p;

	*res = 0;
	if (likely(bitmap[0] & hint_bit)) {
		p = xdr_inline_decode(xdr, 8);
		if (unlikely(!p))
			return -EIO;
		xdr_decode_hyper(p, res);
	}
	return 0;
}

static int decode_first_threshold_item4(struct xdr_stream *xdr,
					struct nfs4_threshold *res)
{
	__be32 *p;
	unsigned int savep;
	uint32_t bitmap[3] = {0,}, attrlen;
	int status;

	/* layout type */
	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;
	res->l_type = be32_to_cpup(p);

	/* thi_hintset bitmap */
	status = decode_attr_bitmap(xdr, bitmap);
	if (status < 0)
		goto xdr_error;

	/* thi_hintlist length */
	status = decode_attr_length(xdr, &attrlen, &savep);
	if (status < 0)
		goto xdr_error;
	/* thi_hintlist */
	status = decode_threshold_hint(xdr, bitmap, &res->rd_sz, THRESHOLD_RD);
	if (status < 0)
		goto xdr_error;
	status = decode_threshold_hint(xdr, bitmap, &res->wr_sz, THRESHOLD_WR);
	if (status < 0)
		goto xdr_error;
	status = decode_threshold_hint(xdr, bitmap, &res->rd_io_sz,
				       THRESHOLD_RD_IO);
	if (status < 0)
		goto xdr_error;
	status = decode_threshold_hint(xdr, bitmap, &res->wr_io_sz,
				       THRESHOLD_WR_IO);
	if (status < 0)
		goto xdr_error;

	status = verify_attr_len(xdr, savep, attrlen);
	res->bm = bitmap[0];

	dprintk("%s bm=0x%x rd_sz=%llu wr_sz=%llu rd_io=%llu wr_io=%llu\n",
		 __func__, res->bm, res->rd_sz, res->wr_sz, res->rd_io_sz,
		res->wr_io_sz);
xdr_error:
	dprintk("%s ret=%d!\n", __func__, status);
	return status;
}

/*
 * Thresholds on pNFS direct I/O vrs MDS I/O
 */
static int decode_attr_mdsthreshold(struct xdr_stream *xdr,
				    uint32_t *bitmap,
				    struct nfs4_threshold *res)
{
	__be32 *p;
	int status = 0;
	uint32_t num;

	if (unlikely(bitmap[2] & (FATTR4_WORD2_MDSTHRESHOLD - 1U)))
		return -EIO;
	if (bitmap[2] & FATTR4_WORD2_MDSTHRESHOLD) {
		/* Did the server return an unrequested attribute? */
		if (unlikely(res == NULL))
			return -EREMOTEIO;
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EIO;
		num = be32_to_cpup(p);
		if (num == 0)
			return 0;
		if (num > 1)
			printk(KERN_INFO "%s: Warning: Multiple pNFS layout "
				"drivers per filesystem not supported\n",
				__func__);

		status = decode_first_threshold_item4(xdr, res);
		bitmap[2] &= ~FATTR4_WORD2_MDSTHRESHOLD;
	}
	return status;
}

static int decode_getfattr_attrs(struct xdr_stream *xdr, uint32_t *bitmap,
		struct nfs_fattr *fattr, struct nfs_fh *fh,
		struct nfs4_fs_locations *fs_loc, const struct nfs_server *server)
{
	int status;
	umode_t fmode = 0;
	uint32_t type;
	int32_t err;

	status = decode_attr_type(xdr, bitmap, &type);
	if (status < 0)
		goto xdr_error;
	fattr->mode = 0;
	if (status != 0) {
		fattr->mode |= nfs_type2fmt[type];
		fattr->valid |= status;
	}

	status = decode_attr_change(xdr, bitmap, &fattr->change_attr);
	if (status < 0)
		goto xdr_error;
	fattr->valid |= status;

	status = decode_attr_size(xdr, bitmap, &fattr->size);
	if (status < 0)
		goto xdr_error;
	fattr->valid |= status;

	status = decode_attr_fsid(xdr, bitmap, &fattr->fsid);
	if (status < 0)
		goto xdr_error;
	fattr->valid |= status;

	err = 0;
	status = decode_attr_error(xdr, bitmap, &err);
	if (status < 0)
		goto xdr_error;

	status = decode_attr_filehandle(xdr, bitmap, fh);
	if (status < 0)
		goto xdr_error;

	status = decode_attr_fileid(xdr, bitmap, &fattr->fileid);
	if (status < 0)
		goto xdr_error;
	fattr->valid |= status;

	status = decode_attr_fs_locations(xdr, bitmap, fs_loc);
	if (status < 0)
		goto xdr_error;
	fattr->valid |= status;

	status = -EIO;
	if (unlikely(bitmap[0]))
		goto xdr_error;

	status = decode_attr_mode(xdr, bitmap, &fmode);
	if (status < 0)
		goto xdr_error;
	if (status != 0) {
		fattr->mode |= fmode;
		fattr->valid |= status;
	}

	status = decode_attr_nlink(xdr, bitmap, &fattr->nlink);
	if (status < 0)
		goto xdr_error;
	fattr->valid |= status;

	status = decode_attr_owner(xdr, bitmap, server, &fattr->uid, fattr->owner_name);
	if (status < 0)
		goto xdr_error;
	fattr->valid |= status;

	status = decode_attr_group(xdr, bitmap, server, &fattr->gid, fattr->group_name);
	if (status < 0)
		goto xdr_error;
	fattr->valid |= status;

	status = decode_attr_rdev(xdr, bitmap, &fattr->rdev);
	if (status < 0)
		goto xdr_error;
	fattr->valid |= status;

	status = decode_attr_space_used(xdr, bitmap, &fattr->du.nfs3.used);
	if (status < 0)
		goto xdr_error;
	fattr->valid |= status;

	status = decode_attr_time_access(xdr, bitmap, &fattr->atime);
	if (status < 0)
		goto xdr_error;
	fattr->valid |= status;

	status = decode_attr_time_metadata(xdr, bitmap, &fattr->ctime);
	if (status < 0)
		goto xdr_error;
	fattr->valid |= status;

	status = decode_attr_time_modify(xdr, bitmap, &fattr->mtime);
	if (status < 0)
		goto xdr_error;
	fattr->valid |= status;

	status = decode_attr_mounted_on_fileid(xdr, bitmap, &fattr->mounted_on_fileid);
	if (status < 0)
		goto xdr_error;
	fattr->valid |= status;

	status = -EIO;
	if (unlikely(bitmap[1]))
		goto xdr_error;

	status = decode_attr_mdsthreshold(xdr, bitmap, fattr->mdsthreshold);
	if (status < 0)
		goto xdr_error;

	if (fattr->label) {
		status = decode_attr_security_label(xdr, bitmap, fattr->label);
		if (status < 0)
			goto xdr_error;
		fattr->valid |= status;
	}

xdr_error:
	dprintk("%s: xdr returned %d\n", __func__, -status);
	return status;
}

static int decode_getfattr_generic(struct xdr_stream *xdr, struct nfs_fattr *fattr,
		struct nfs_fh *fh, struct nfs4_fs_locations *fs_loc,
		const struct nfs_server *server)
{
	unsigned int savep;
	uint32_t attrlen,
		 bitmap[3] = {0};
	int status;

	status = decode_op_hdr(xdr, OP_GETATTR);
	if (status < 0)
		goto xdr_error;

	status = decode_attr_bitmap(xdr, bitmap);
	if (status < 0)
		goto xdr_error;

	status = decode_attr_length(xdr, &attrlen, &savep);
	if (status < 0)
		goto xdr_error;

	status = decode_getfattr_attrs(xdr, bitmap, fattr, fh, fs_loc, server);
	if (status < 0)
		goto xdr_error;

	status = verify_attr_len(xdr, savep, attrlen);
xdr_error:
	dprintk("%s: xdr returned %d\n", __func__, -status);
	return status;
}

static int decode_getfattr(struct xdr_stream *xdr, struct nfs_fattr *fattr,
		const struct nfs_server *server)
{
	return decode_getfattr_generic(xdr, fattr, NULL, NULL, server);
}

/*
 * Decode potentially multiple layout types.
 */
static int decode_pnfs_layout_types(struct xdr_stream *xdr,
				    struct nfs_fsinfo *fsinfo)
{
	__be32 *p;
	uint32_t i;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;
	fsinfo->nlayouttypes = be32_to_cpup(p);

	/* pNFS is not supported by the underlying file system */
	if (fsinfo->nlayouttypes == 0)
		return 0;

	/* Decode and set first layout type, move xdr->p past unused types */
	p = xdr_inline_decode(xdr, fsinfo->nlayouttypes * 4);
	if (unlikely(!p))
		return -EIO;

	/* If we get too many, then just cap it at the max */
	if (fsinfo->nlayouttypes > NFS_MAX_LAYOUT_TYPES) {
		printk(KERN_INFO "NFS: %s: Warning: Too many (%u) pNFS layout types\n",
			__func__, fsinfo->nlayouttypes);
		fsinfo->nlayouttypes = NFS_MAX_LAYOUT_TYPES;
	}

	for(i = 0; i < fsinfo->nlayouttypes; ++i)
		fsinfo->layouttype[i] = be32_to_cpup(p++);
	return 0;
}

/*
 * The type of file system exported.
 * Note we must ensure that layouttype is set in any non-error case.
 */
static int decode_attr_pnfstype(struct xdr_stream *xdr, uint32_t *bitmap,
				struct nfs_fsinfo *fsinfo)
{
	int status = 0;

	dprintk("%s: bitmap is %x\n", __func__, bitmap[1]);
	if (unlikely(bitmap[1] & (FATTR4_WORD1_FS_LAYOUT_TYPES - 1U)))
		return -EIO;
	if (bitmap[1] & FATTR4_WORD1_FS_LAYOUT_TYPES) {
		status = decode_pnfs_layout_types(xdr, fsinfo);
		bitmap[1] &= ~FATTR4_WORD1_FS_LAYOUT_TYPES;
	}
	return status;
}

/*
 * The prefered block size for layout directed io
 */
static int decode_attr_layout_blksize(struct xdr_stream *xdr, uint32_t *bitmap,
				      uint32_t *res)
{
	__be32 *p;

	dprintk("%s: bitmap is %x\n", __func__, bitmap[2]);
	*res = 0;
	if (bitmap[2] & FATTR4_WORD2_LAYOUT_BLKSIZE) {
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EIO;
		*res = be32_to_cpup(p);
		bitmap[2] &= ~FATTR4_WORD2_LAYOUT_BLKSIZE;
	}
	return 0;
}

/*
 * The granularity of a CLONE operation.
 */
static int decode_attr_clone_blksize(struct xdr_stream *xdr, uint32_t *bitmap,
				     uint32_t *res)
{
	__be32 *p;

	dprintk("%s: bitmap is %x\n", __func__, bitmap[2]);
	*res = 0;
	if (bitmap[2] & FATTR4_WORD2_CLONE_BLKSIZE) {
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EIO;
		*res = be32_to_cpup(p);
		bitmap[2] &= ~FATTR4_WORD2_CLONE_BLKSIZE;
	}
	return 0;
}

static int decode_attr_change_attr_type(struct xdr_stream *xdr,
					uint32_t *bitmap,
					enum nfs4_change_attr_type *res)
{
	u32 tmp = NFS4_CHANGE_TYPE_IS_UNDEFINED;

	dprintk("%s: bitmap is %x\n", __func__, bitmap[2]);
	if (bitmap[2] & FATTR4_WORD2_CHANGE_ATTR_TYPE) {
		if (xdr_stream_decode_u32(xdr, &tmp))
			return -EIO;
		bitmap[2] &= ~FATTR4_WORD2_CHANGE_ATTR_TYPE;
	}

	switch(tmp) {
	case NFS4_CHANGE_TYPE_IS_MONOTONIC_INCR:
	case NFS4_CHANGE_TYPE_IS_VERSION_COUNTER:
	case NFS4_CHANGE_TYPE_IS_VERSION_COUNTER_NOPNFS:
	case NFS4_CHANGE_TYPE_IS_TIME_METADATA:
		*res = tmp;
		break;
	default:
		*res = NFS4_CHANGE_TYPE_IS_UNDEFINED;
	}
	return 0;
}

static int decode_fsinfo(struct xdr_stream *xdr, struct nfs_fsinfo *fsinfo)
{
	unsigned int savep;
	uint32_t attrlen, bitmap[3];
	int status;

	if ((status = decode_op_hdr(xdr, OP_GETATTR)) != 0)
		goto xdr_error;
	if ((status = decode_attr_bitmap(xdr, bitmap)) != 0)
		goto xdr_error;
	if ((status = decode_attr_length(xdr, &attrlen, &savep)) != 0)
		goto xdr_error;

	fsinfo->rtmult = fsinfo->wtmult = 512;	/* ??? */

	if ((status = decode_attr_lease_time(xdr, bitmap, &fsinfo->lease_time)) != 0)
		goto xdr_error;
	if ((status = decode_attr_maxfilesize(xdr, bitmap, &fsinfo->maxfilesize)) != 0)
		goto xdr_error;
	if ((status = decode_attr_maxread(xdr, bitmap, &fsinfo->rtmax)) != 0)
		goto xdr_error;
	fsinfo->rtpref = fsinfo->dtpref = fsinfo->rtmax;
	if ((status = decode_attr_maxwrite(xdr, bitmap, &fsinfo->wtmax)) != 0)
		goto xdr_error;
	fsinfo->wtpref = fsinfo->wtmax;

	status = -EIO;
	if (unlikely(bitmap[0]))
		goto xdr_error;

	status = decode_attr_time_delta(xdr, bitmap, &fsinfo->time_delta);
	if (status != 0)
		goto xdr_error;
	status = decode_attr_pnfstype(xdr, bitmap, fsinfo);
	if (status != 0)
		goto xdr_error;

	status = -EIO;
	if (unlikely(bitmap[1]))
		goto xdr_error;

	status = decode_attr_layout_blksize(xdr, bitmap, &fsinfo->blksize);
	if (status)
		goto xdr_error;
	status = decode_attr_clone_blksize(xdr, bitmap, &fsinfo->clone_blksize);
	if (status)
		goto xdr_error;

	status = decode_attr_change_attr_type(xdr, bitmap,
					      &fsinfo->change_attr_type);
	if (status)
		goto xdr_error;

	status = decode_attr_xattrsupport(xdr, bitmap,
					  &fsinfo->xattr_support);
	if (status)
		goto xdr_error;

	status = verify_attr_len(xdr, savep, attrlen);
xdr_error:
	dprintk("%s: xdr returned %d!\n", __func__, -status);
	return status;
}

static int decode_getfh(struct xdr_stream *xdr, struct nfs_fh *fh)
{
	__be32 *p;
	uint32_t len;
	int status;

	/* Zero handle first to allow comparisons */
	memset(fh, 0, sizeof(*fh));

	status = decode_op_hdr(xdr, OP_GETFH);
	if (status)
		return status;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;
	len = be32_to_cpup(p);
	if (len > NFS4_FHSIZE || len == 0) {
		trace_nfs4_xdr_bad_filehandle(xdr, OP_GETFH, NFS4ERR_BADHANDLE);
		return -EREMOTEIO;
	}
	fh->size = len;
	p = xdr_inline_decode(xdr, len);
	if (unlikely(!p))
		return -EIO;
	memcpy(fh->data, p, len);
	return 0;
}

static int decode_link(struct xdr_stream *xdr, struct nfs4_change_info *cinfo)
{
	int status;

	status = decode_op_hdr(xdr, OP_LINK);
	if (status)
		return status;
	return decode_change_info(xdr, cinfo);
}

/*
 * We create the owner, so we know a proper owner.id length is 4.
 */
static int decode_lock_denied (struct xdr_stream *xdr, struct file_lock *fl)
{
	uint64_t offset, length, clientid;
	__be32 *p;
	uint32_t namelen, type;

	p = xdr_inline_decode(xdr, 32); /* read 32 bytes */
	if (unlikely(!p))
		return -EIO;
	p = xdr_decode_hyper(p, &offset); /* read 2 8-byte long words */
	p = xdr_decode_hyper(p, &length);
	type = be32_to_cpup(p++); /* 4 byte read */
	if (fl != NULL) { /* manipulate file lock */
		fl->fl_start = (loff_t)offset;
		fl->fl_end = fl->fl_start + (loff_t)length - 1;
		if (length == ~(uint64_t)0)
			fl->fl_end = OFFSET_MAX;
		fl->fl_type = F_WRLCK;
		if (type & 1)
			fl->fl_type = F_RDLCK;
		fl->fl_pid = 0;
	}
	p = xdr_decode_hyper(p, &clientid); /* read 8 bytes */
	namelen = be32_to_cpup(p); /* read 4 bytes */  /* have read all 32 bytes now */
	p = xdr_inline_decode(xdr, namelen); /* variable size field */
	if (likely(!p))
		return -EIO;
	return -NFS4ERR_DENIED;
}

static int decode_lock(struct xdr_stream *xdr, struct nfs_lock_res *res)
{
	int status;

	status = decode_op_hdr(xdr, OP_LOCK);
	if (status == -EIO)
		goto out;
	if (status == 0) {
		status = decode_lock_stateid(xdr, &res->stateid);
		if (unlikely(status))
			goto out;
	} else if (status == -NFS4ERR_DENIED)
		status = decode_lock_denied(xdr, NULL);
	if (res->open_seqid != NULL)
		nfs_increment_open_seqid(status, res->open_seqid);
	nfs_increment_lock_seqid(status, res->lock_seqid);
out:
	return status;
}

static int decode_lockt(struct xdr_stream *xdr, struct nfs_lockt_res *res)
{
	int status;
	status = decode_op_hdr(xdr, OP_LOCKT);
	if (status == -NFS4ERR_DENIED)
		return decode_lock_denied(xdr, res->denied);
	return status;
}

static int decode_locku(struct xdr_stream *xdr, struct nfs_locku_res *res)
{
	int status;

	status = decode_op_hdr(xdr, OP_LOCKU);
	if (status != -EIO)
		nfs_increment_lock_seqid(status, res->seqid);
	if (status == 0)
		status = decode_lock_stateid(xdr, &res->stateid);
	return status;
}

static int decode_release_lockowner(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_RELEASE_LOCKOWNER);
}

static int decode_lookup(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_LOOKUP);
}

static int decode_lookupp(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_LOOKUPP);
}

/* This is too sick! */
static int decode_space_limit(struct xdr_stream *xdr,
		unsigned long *pagemod_limit)
{
	__be32 *p;
	uint32_t limit_type, nblocks, blocksize;
	u64 maxsize = 0;

	p = xdr_inline_decode(xdr, 12);
	if (unlikely(!p))
		return -EIO;
	limit_type = be32_to_cpup(p++);
	switch (limit_type) {
	case NFS4_LIMIT_SIZE:
		xdr_decode_hyper(p, &maxsize);
		break;
	case NFS4_LIMIT_BLOCKS:
		nblocks = be32_to_cpup(p++);
		blocksize = be32_to_cpup(p);
		maxsize = (uint64_t)nblocks * (uint64_t)blocksize;
	}
	maxsize >>= PAGE_SHIFT;
	*pagemod_limit = min_t(u64, maxsize, ULONG_MAX);
	return 0;
}

static int decode_rw_delegation(struct xdr_stream *xdr,
		uint32_t delegation_type,
		struct nfs_openres *res)
{
	__be32 *p;
	int status;

	status = decode_delegation_stateid(xdr, &res->delegation);
	if (unlikely(status))
		return status;
	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;
	res->do_recall = be32_to_cpup(p);

	switch (delegation_type) {
	case NFS4_OPEN_DELEGATE_READ:
		res->delegation_type = FMODE_READ;
		break;
	case NFS4_OPEN_DELEGATE_WRITE:
		res->delegation_type = FMODE_WRITE|FMODE_READ;
		if (decode_space_limit(xdr, &res->pagemod_limit) < 0)
				return -EIO;
	}
	return decode_ace(xdr, NULL);
}

static int decode_no_delegation(struct xdr_stream *xdr, struct nfs_openres *res)
{
	__be32 *p;
	uint32_t why_no_delegation;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;
	why_no_delegation = be32_to_cpup(p);
	switch (why_no_delegation) {
		case WND4_CONTENTION:
		case WND4_RESOURCE:
			xdr_inline_decode(xdr, 4);
			/* Ignore for now */
	}
	return 0;
}

static int decode_delegation(struct xdr_stream *xdr, struct nfs_openres *res)
{
	__be32 *p;
	uint32_t delegation_type;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;
	delegation_type = be32_to_cpup(p);
	res->delegation_type = 0;
	switch (delegation_type) {
	case NFS4_OPEN_DELEGATE_NONE:
		return 0;
	case NFS4_OPEN_DELEGATE_READ:
	case NFS4_OPEN_DELEGATE_WRITE:
		return decode_rw_delegation(xdr, delegation_type, res);
	case NFS4_OPEN_DELEGATE_NONE_EXT:
		return decode_no_delegation(xdr, res);
	}
	return -EIO;
}

static int decode_open(struct xdr_stream *xdr, struct nfs_openres *res)
{
	__be32 *p;
	uint32_t savewords, bmlen, i;
	int status;

	if (!__decode_op_hdr(xdr, OP_OPEN, &status))
		return status;
	nfs_increment_open_seqid(status, res->seqid);
	if (status)
		return status;
	status = decode_open_stateid(xdr, &res->stateid);
	if (unlikely(status))
		return status;

	decode_change_info(xdr, &res->cinfo);

	p = xdr_inline_decode(xdr, 8);
	if (unlikely(!p))
		return -EIO;
	res->rflags = be32_to_cpup(p++);
	bmlen = be32_to_cpup(p);
	if (bmlen > 10)
		goto xdr_error;

	p = xdr_inline_decode(xdr, bmlen << 2);
	if (unlikely(!p))
		return -EIO;
	savewords = min_t(uint32_t, bmlen, NFS4_BITMAP_SIZE);
	for (i = 0; i < savewords; ++i)
		res->attrset[i] = be32_to_cpup(p++);
	for (; i < NFS4_BITMAP_SIZE; i++)
		res->attrset[i] = 0;

	return decode_delegation(xdr, res);
xdr_error:
	dprintk("%s: Bitmap too large! Length = %u\n", __func__, bmlen);
	return -EIO;
}

static int decode_open_confirm(struct xdr_stream *xdr, struct nfs_open_confirmres *res)
{
	int status;

	status = decode_op_hdr(xdr, OP_OPEN_CONFIRM);
	if (status != -EIO)
		nfs_increment_open_seqid(status, res->seqid);
	if (!status)
		status = decode_open_stateid(xdr, &res->stateid);
	return status;
}

static int decode_open_downgrade(struct xdr_stream *xdr, struct nfs_closeres *res)
{
	int status;

	status = decode_op_hdr(xdr, OP_OPEN_DOWNGRADE);
	if (status != -EIO)
		nfs_increment_open_seqid(status, res->seqid);
	if (!status)
		status = decode_open_stateid(xdr, &res->stateid);
	return status;
}

static int decode_putfh(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_PUTFH);
}

static int decode_putrootfh(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_PUTROOTFH);
}

static int decode_read(struct xdr_stream *xdr, struct rpc_rqst *req,
		       struct nfs_pgio_res *res)
{
	__be32 *p;
	uint32_t count, eof, recvd;
	int status;

	status = decode_op_hdr(xdr, OP_READ);
	if (status)
		return status;
	p = xdr_inline_decode(xdr, 8);
	if (unlikely(!p))
		return -EIO;
	eof = be32_to_cpup(p++);
	count = be32_to_cpup(p);
	recvd = xdr_read_pages(xdr, count);
	if (count > recvd) {
		dprintk("NFS: server cheating in read reply: "
				"count %u > recvd %u\n", count, recvd);
		count = recvd;
		eof = 0;
	}
	res->eof = eof;
	res->count = count;
	return 0;
}

static int decode_readdir(struct xdr_stream *xdr, struct rpc_rqst *req, struct nfs4_readdir_res *readdir)
{
	int		status;
	__be32		verf[2];

	status = decode_op_hdr(xdr, OP_READDIR);
	if (!status)
		status = decode_verifier(xdr, readdir->verifier.data);
	if (unlikely(status))
		return status;
	memcpy(verf, readdir->verifier.data, sizeof(verf));
	dprintk("%s: verifier = %08x:%08x\n",
			__func__, verf[0], verf[1]);
	return xdr_read_pages(xdr, xdr->buf->page_len);
}

static int decode_readlink(struct xdr_stream *xdr, struct rpc_rqst *req)
{
	struct xdr_buf *rcvbuf = &req->rq_rcv_buf;
	u32 len, recvd;
	__be32 *p;
	int status;

	status = decode_op_hdr(xdr, OP_READLINK);
	if (status)
		return status;

	/* Convert length of symlink */
	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;
	len = be32_to_cpup(p);
	if (len >= rcvbuf->page_len || len <= 0) {
		dprintk("nfs: server returned giant symlink!\n");
		return -ENAMETOOLONG;
	}
	recvd = xdr_read_pages(xdr, len);
	if (recvd < len) {
		dprintk("NFS: server cheating in readlink reply: "
				"count %u > recvd %u\n", len, recvd);
		return -EIO;
	}
	/*
	 * The XDR encode routine has set things up so that
	 * the link text will be copied directly into the
	 * buffer.  We just have to do overflow-checking,
	 * and null-terminate the text (the VFS expects
	 * null-termination).
	 */
	xdr_terminate_string(rcvbuf, len);
	return 0;
}

static int decode_remove(struct xdr_stream *xdr, struct nfs4_change_info *cinfo)
{
	int status;

	status = decode_op_hdr(xdr, OP_REMOVE);
	if (status)
		goto out;
	status = decode_change_info(xdr, cinfo);
out:
	return status;
}

static int decode_rename(struct xdr_stream *xdr, struct nfs4_change_info *old_cinfo,
	      struct nfs4_change_info *new_cinfo)
{
	int status;

	status = decode_op_hdr(xdr, OP_RENAME);
	if (status)
		goto out;
	if ((status = decode_change_info(xdr, old_cinfo)))
		goto out;
	status = decode_change_info(xdr, new_cinfo);
out:
	return status;
}

static int decode_renew(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_RENEW);
}

static int
decode_restorefh(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_RESTOREFH);
}

static int decode_getacl(struct xdr_stream *xdr, struct rpc_rqst *req,
			 struct nfs_getaclres *res, enum nfs4_acl_type type)
{
	unsigned int savep;
	uint32_t attrlen,
		 bitmap[3] = {0};
	int status;

	res->acl_len = 0;
	if ((status = decode_op_hdr(xdr, OP_GETATTR)) != 0)
		goto out;

	xdr_enter_page(xdr, xdr->buf->page_len);

	if ((status = decode_attr_bitmap(xdr, bitmap)) != 0)
		goto out;
	if ((status = decode_attr_length(xdr, &attrlen, &savep)) != 0)
		goto out;

	switch (type) {
	default:
		if (unlikely(bitmap[0] & (FATTR4_WORD0_ACL - 1U)))
			return -EIO;
		if (!(bitmap[0] & FATTR4_WORD0_ACL))
			return -EOPNOTSUPP;
		break;
	case NFS4ACL_DACL:
		if (unlikely(bitmap[0] || bitmap[1] & (FATTR4_WORD1_DACL - 1U)))
			return -EIO;
		if (!(bitmap[1] & FATTR4_WORD1_DACL))
			return -EOPNOTSUPP;
		break;
	case NFS4ACL_SACL:
		if (unlikely(bitmap[0] || bitmap[1] & (FATTR4_WORD1_SACL - 1U)))
			return -EIO;
		if (!(bitmap[1] & FATTR4_WORD1_SACL))
			return -EOPNOTSUPP;
	}

	/* The bitmap (xdr len + bitmaps) and the attr xdr len words
	 * are stored with the acl data to handle the problem of
	 * variable length bitmaps.*/
	res->acl_data_offset = xdr_page_pos(xdr);
	res->acl_len = attrlen;

	/* Check for receive buffer overflow */
	if (res->acl_len > xdr_stream_remaining(xdr) ||
	    res->acl_len + res->acl_data_offset > xdr->buf->page_len) {
		res->acl_flags |= NFS4_ACL_TRUNC;
		dprintk("NFS: acl reply: attrlen %u > page_len %zu\n",
			attrlen, xdr_stream_remaining(xdr));
	}
out:
	return status;
}

static int
decode_savefh(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_SAVEFH);
}

static int decode_setattr(struct xdr_stream *xdr)
{
	int status;

	status = decode_op_hdr(xdr, OP_SETATTR);
	if (status)
		return status;
	if (decode_bitmap4(xdr, NULL, 0) >= 0)
		return 0;
	return -EIO;
}

static int decode_setclientid(struct xdr_stream *xdr, struct nfs4_setclientid_res *res)
{
	__be32 *p;
	uint32_t opnum;
	int32_t nfserr;

	p = xdr_inline_decode(xdr, 8);
	if (unlikely(!p))
		return -EIO;
	opnum = be32_to_cpup(p++);
	if (opnum != OP_SETCLIENTID) {
		dprintk("nfs: decode_setclientid: Server returned operation"
			" %d\n", opnum);
		return -EIO;
	}
	nfserr = be32_to_cpup(p);
	if (nfserr == NFS_OK) {
		p = xdr_inline_decode(xdr, 8 + NFS4_VERIFIER_SIZE);
		if (unlikely(!p))
			return -EIO;
		p = xdr_decode_hyper(p, &res->clientid);
		memcpy(res->confirm.data, p, NFS4_VERIFIER_SIZE);
	} else if (nfserr == NFSERR_CLID_INUSE) {
		uint32_t len;

		/* skip netid string */
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EIO;
		len = be32_to_cpup(p);
		p = xdr_inline_decode(xdr, len);
		if (unlikely(!p))
			return -EIO;

		/* skip uaddr string */
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EIO;
		len = be32_to_cpup(p);
		p = xdr_inline_decode(xdr, len);
		if (unlikely(!p))
			return -EIO;
		return -NFSERR_CLID_INUSE;
	} else
		return nfs4_stat_to_errno(nfserr);

	return 0;
}

static int decode_setclientid_confirm(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_SETCLIENTID_CONFIRM);
}

static int decode_write(struct xdr_stream *xdr, struct nfs_pgio_res *res)
{
	__be32 *p;
	int status;

	status = decode_op_hdr(xdr, OP_WRITE);
	if (status)
		return status;

	p = xdr_inline_decode(xdr, 8);
	if (unlikely(!p))
		return -EIO;
	res->count = be32_to_cpup(p++);
	res->verf->committed = be32_to_cpup(p++);
	return decode_write_verifier(xdr, &res->verf->verifier);
}

static int decode_delegreturn(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_DELEGRETURN);
}

static int decode_secinfo_gss(struct xdr_stream *xdr,
			      struct nfs4_secinfo4 *flavor)
{
	u32 oid_len;
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;
	oid_len = be32_to_cpup(p);
	if (oid_len > GSS_OID_MAX_LEN)
		return -EINVAL;

	p = xdr_inline_decode(xdr, oid_len);
	if (unlikely(!p))
		return -EIO;
	memcpy(flavor->flavor_info.oid.data, p, oid_len);
	flavor->flavor_info.oid.len = oid_len;

	p = xdr_inline_decode(xdr, 8);
	if (unlikely(!p))
		return -EIO;
	flavor->flavor_info.qop = be32_to_cpup(p++);
	flavor->flavor_info.service = be32_to_cpup(p);

	return 0;
}

static int decode_secinfo_common(struct xdr_stream *xdr, struct nfs4_secinfo_res *res)
{
	struct nfs4_secinfo4 *sec_flavor;
	unsigned int i, num_flavors;
	int status;
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;

	res->flavors->num_flavors = 0;
	num_flavors = be32_to_cpup(p);

	for (i = 0; i < num_flavors; i++) {
		sec_flavor = &res->flavors->flavors[i];
		if ((char *)&sec_flavor[1] - (char *)res->flavors > PAGE_SIZE)
			break;

		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EIO;
		sec_flavor->flavor = be32_to_cpup(p);

		if (sec_flavor->flavor == RPC_AUTH_GSS) {
			status = decode_secinfo_gss(xdr, sec_flavor);
			if (status)
				goto out;
		}
		res->flavors->num_flavors++;
	}

	status = 0;
out:
	return status;
}

static int decode_secinfo(struct xdr_stream *xdr, struct nfs4_secinfo_res *res)
{
	int status = decode_op_hdr(xdr, OP_SECINFO);
	if (status)
		return status;
	return decode_secinfo_common(xdr, res);
}

#if defined(CONFIG_NFS_V4_1)
static int decode_secinfo_no_name(struct xdr_stream *xdr, struct nfs4_secinfo_res *res)
{
	int status = decode_op_hdr(xdr, OP_SECINFO_NO_NAME);
	if (status)
		return status;
	return decode_secinfo_common(xdr, res);
}

static int decode_op_map(struct xdr_stream *xdr, struct nfs4_op_map *op_map)
{
	if (xdr_stream_decode_uint32_array(xdr, op_map->u.words,
					   ARRAY_SIZE(op_map->u.words)) < 0)
		return -EIO;
	return 0;
}

static int decode_exchange_id(struct xdr_stream *xdr,
			      struct nfs41_exchange_id_res *res)
{
	__be32 *p;
	uint32_t dummy;
	char *dummy_str;
	int status;
	uint32_t impl_id_count;

	status = decode_op_hdr(xdr, OP_EXCHANGE_ID);
	if (status)
		return status;

	p = xdr_inline_decode(xdr, 8);
	if (unlikely(!p))
		return -EIO;
	xdr_decode_hyper(p, &res->clientid);
	p = xdr_inline_decode(xdr, 12);
	if (unlikely(!p))
		return -EIO;
	res->seqid = be32_to_cpup(p++);
	res->flags = be32_to_cpup(p++);

	res->state_protect.how = be32_to_cpup(p);
	switch (res->state_protect.how) {
	case SP4_NONE:
		break;
	case SP4_MACH_CRED:
		status = decode_op_map(xdr, &res->state_protect.enforce);
		if (status)
			return status;
		status = decode_op_map(xdr, &res->state_protect.allow);
		if (status)
			return status;
		break;
	default:
		WARN_ON_ONCE(1);
		return -EIO;
	}

	/* server_owner4.so_minor_id */
	p = xdr_inline_decode(xdr, 8);
	if (unlikely(!p))
		return -EIO;
	p = xdr_decode_hyper(p, &res->server_owner->minor_id);

	/* server_owner4.so_major_id */
	status = decode_opaque_inline(xdr, &dummy, &dummy_str);
	if (unlikely(status))
		return status;
	memcpy(res->server_owner->major_id, dummy_str, dummy);
	res->server_owner->major_id_sz = dummy;

	/* server_scope4 */
	status = decode_opaque_inline(xdr, &dummy, &dummy_str);
	if (unlikely(status))
		return status;
	memcpy(res->server_scope->server_scope, dummy_str, dummy);
	res->server_scope->server_scope_sz = dummy;

	/* Implementation Id */
	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;
	impl_id_count = be32_to_cpup(p++);

	if (impl_id_count) {
		/* nii_domain */
		status = decode_opaque_inline(xdr, &dummy, &dummy_str);
		if (unlikely(status))
			return status;
		memcpy(res->impl_id->domain, dummy_str, dummy);

		/* nii_name */
		status = decode_opaque_inline(xdr, &dummy, &dummy_str);
		if (unlikely(status))
			return status;
		memcpy(res->impl_id->name, dummy_str, dummy);

		/* nii_date */
		p = xdr_inline_decode(xdr, 12);
		if (unlikely(!p))
			return -EIO;
		p = xdr_decode_hyper(p, &res->impl_id->date.seconds);
		res->impl_id->date.nseconds = be32_to_cpup(p);

		/* if there's more than one entry, ignore the rest */
	}
	return 0;
}

static int decode_chan_attrs(struct xdr_stream *xdr,
			     struct nfs4_channel_attrs *attrs)
{
	__be32 *p;
	u32 nr_attrs, val;

	p = xdr_inline_decode(xdr, 28);
	if (unlikely(!p))
		return -EIO;
	val = be32_to_cpup(p++);	/* headerpadsz */
	if (val)
		return -EINVAL;		/* no support for header padding yet */
	attrs->max_rqst_sz = be32_to_cpup(p++);
	attrs->max_resp_sz = be32_to_cpup(p++);
	attrs->max_resp_sz_cached = be32_to_cpup(p++);
	attrs->max_ops = be32_to_cpup(p++);
	attrs->max_reqs = be32_to_cpup(p++);
	nr_attrs = be32_to_cpup(p);
	if (unlikely(nr_attrs > 1)) {
		printk(KERN_WARNING "NFS: %s: Invalid rdma channel attrs "
			"count %u\n", __func__, nr_attrs);
		return -EINVAL;
	}
	if (nr_attrs == 1) {
		p = xdr_inline_decode(xdr, 4); /* skip rdma_attrs */
		if (unlikely(!p))
			return -EIO;
	}
	return 0;
}

static int decode_sessionid(struct xdr_stream *xdr, struct nfs4_sessionid *sid)
{
	return decode_opaque_fixed(xdr, sid->data, NFS4_MAX_SESSIONID_LEN);
}

static int decode_bind_conn_to_session(struct xdr_stream *xdr,
				struct nfs41_bind_conn_to_session_res *res)
{
	__be32 *p;
	int status;

	status = decode_op_hdr(xdr, OP_BIND_CONN_TO_SESSION);
	if (!status)
		status = decode_sessionid(xdr, &res->sessionid);
	if (unlikely(status))
		return status;

	/* dir flags, rdma mode bool */
	p = xdr_inline_decode(xdr, 8);
	if (unlikely(!p))
		return -EIO;

	res->dir = be32_to_cpup(p++);
	if (res->dir == 0 || res->dir > NFS4_CDFS4_BOTH)
		return -EIO;
	if (be32_to_cpup(p) == 0)
		res->use_conn_in_rdma_mode = false;
	else
		res->use_conn_in_rdma_mode = true;

	return 0;
}

static int decode_create_session(struct xdr_stream *xdr,
				 struct nfs41_create_session_res *res)
{
	__be32 *p;
	int status;

	status = decode_op_hdr(xdr, OP_CREATE_SESSION);
	if (!status)
		status = decode_sessionid(xdr, &res->sessionid);
	if (unlikely(status))
		return status;

	/* seqid, flags */
	p = xdr_inline_decode(xdr, 8);
	if (unlikely(!p))
		return -EIO;
	res->seqid = be32_to_cpup(p++);
	res->flags = be32_to_cpup(p);

	/* Channel attributes */
	status = decode_chan_attrs(xdr, &res->fc_attrs);
	if (!status)
		status = decode_chan_attrs(xdr, &res->bc_attrs);
	return status;
}

static int decode_destroy_session(struct xdr_stream *xdr, void *dummy)
{
	return decode_op_hdr(xdr, OP_DESTROY_SESSION);
}

static int decode_destroy_clientid(struct xdr_stream *xdr, void *dummy)
{
	return decode_op_hdr(xdr, OP_DESTROY_CLIENTID);
}

static int decode_reclaim_complete(struct xdr_stream *xdr, void *dummy)
{
	return decode_op_hdr(xdr, OP_RECLAIM_COMPLETE);
}
#endif /* CONFIG_NFS_V4_1 */

static int decode_sequence(struct xdr_stream *xdr,
			   struct nfs4_sequence_res *res,
			   struct rpc_rqst *rqstp)
{
#if defined(CONFIG_NFS_V4_1)
	struct nfs4_session *session;
	struct nfs4_sessionid id;
	u32 dummy;
	int status;
	__be32 *p;

	if (res->sr_slot == NULL)
		return 0;
	if (!res->sr_slot->table->session)
		return 0;

	status = decode_op_hdr(xdr, OP_SEQUENCE);
	if (!status)
		status = decode_sessionid(xdr, &id);
	if (unlikely(status))
		goto out_err;

	/*
	 * If the server returns different values for sessionID, slotID or
	 * sequence number, the server is looney tunes.
	 */
	status = -EREMOTEIO;
	session = res->sr_slot->table->session;

	if (memcmp(id.data, session->sess_id.data,
		   NFS4_MAX_SESSIONID_LEN)) {
		dprintk("%s Invalid session id\n", __func__);
		goto out_err;
	}

	p = xdr_inline_decode(xdr, 20);
	if (unlikely(!p))
		goto out_overflow;

	/* seqid */
	dummy = be32_to_cpup(p++);
	if (dummy != res->sr_slot->seq_nr) {
		dprintk("%s Invalid sequence number\n", __func__);
		goto out_err;
	}
	/* slot id */
	dummy = be32_to_cpup(p++);
	if (dummy != res->sr_slot->slot_nr) {
		dprintk("%s Invalid slot id\n", __func__);
		goto out_err;
	}
	/* highest slot id */
	res->sr_highest_slotid = be32_to_cpup(p++);
	/* target highest slot id */
	res->sr_target_highest_slotid = be32_to_cpup(p++);
	/* result flags */
	res->sr_status_flags = be32_to_cpup(p);
	status = 0;
out_err:
	res->sr_status = status;
	return status;
out_overflow:
	status = -EIO;
	goto out_err;
#else  /* CONFIG_NFS_V4_1 */
	return 0;
#endif /* CONFIG_NFS_V4_1 */
}

#if defined(CONFIG_NFS_V4_1)
static int decode_layout_stateid(struct xdr_stream *xdr, nfs4_stateid *stateid)
{
	stateid->type = NFS4_LAYOUT_STATEID_TYPE;
	return decode_stateid(xdr, stateid);
}

static int decode_getdeviceinfo(struct xdr_stream *xdr,
				struct nfs4_getdeviceinfo_res *res)
{
	struct pnfs_device *pdev = res->pdev;
	__be32 *p;
	uint32_t len, type;
	int status;

	status = decode_op_hdr(xdr, OP_GETDEVICEINFO);
	if (status) {
		if (status == -ETOOSMALL) {
			p = xdr_inline_decode(xdr, 4);
			if (unlikely(!p))
				return -EIO;
			pdev->mincount = be32_to_cpup(p);
			dprintk("%s: Min count too small. mincnt = %u\n",
				__func__, pdev->mincount);
		}
		return status;
	}

	p = xdr_inline_decode(xdr, 8);
	if (unlikely(!p))
		return -EIO;
	type = be32_to_cpup(p++);
	if (type != pdev->layout_type) {
		dprintk("%s: layout mismatch req: %u pdev: %u\n",
			__func__, pdev->layout_type, type);
		return -EINVAL;
	}
	/*
	 * Get the length of the opaque device_addr4. xdr_read_pages places
	 * the opaque device_addr4 in the xdr_buf->pages (pnfs_device->pages)
	 * and places the remaining xdr data in xdr_buf->tail
	 */
	pdev->mincount = be32_to_cpup(p);
	if (xdr_read_pages(xdr, pdev->mincount) != pdev->mincount)
		return -EIO;

	/* Parse notification bitmap, verifying that it is zero. */
	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;
	len = be32_to_cpup(p);
	if (len) {
		uint32_t i;

		p = xdr_inline_decode(xdr, 4 * len);
		if (unlikely(!p))
			return -EIO;

		res->notification = be32_to_cpup(p++);
		for (i = 1; i < len; i++) {
			if (be32_to_cpup(p++)) {
				dprintk("%s: unsupported notification\n",
					__func__);
				return -EIO;
			}
		}
	}
	return 0;
}

static int decode_layoutget(struct xdr_stream *xdr, struct rpc_rqst *req,
			    struct nfs4_layoutget_res *res)
{
	__be32 *p;
	int status;
	u32 layout_count;
	u32 recvd;

	status = decode_op_hdr(xdr, OP_LAYOUTGET);
	if (status)
		goto out;
	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	res->return_on_close = be32_to_cpup(p);
	decode_layout_stateid(xdr, &res->stateid);
	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	layout_count = be32_to_cpup(p);
	if (!layout_count) {
		dprintk("%s: server responded with empty layout array\n",
			__func__);
		status = -EINVAL;
		goto out;
	}

	p = xdr_inline_decode(xdr, 28);
	if (unlikely(!p))
		goto out_overflow;
	p = xdr_decode_hyper(p, &res->range.offset);
	p = xdr_decode_hyper(p, &res->range.length);
	res->range.iomode = be32_to_cpup(p++);
	res->type = be32_to_cpup(p++);
	res->layoutp->len = be32_to_cpup(p);

	dprintk("%s roff:%lu rlen:%lu riomode:%d, lo_type:0x%x, lo.len:%d\n",
		__func__,
		(unsigned long)res->range.offset,
		(unsigned long)res->range.length,
		res->range.iomode,
		res->type,
		res->layoutp->len);

	recvd = xdr_read_pages(xdr, res->layoutp->len);
	if (res->layoutp->len > recvd) {
		dprintk("NFS: server cheating in layoutget reply: "
				"layout len %u > recvd %u\n",
				res->layoutp->len, recvd);
		status = -EINVAL;
		goto out;
	}

	if (layout_count > 1) {
		/* We only handle a length one array at the moment.  Any
		 * further entries are just ignored.  Note that this means
		 * the client may see a response that is less than the
		 * minimum it requested.
		 */
		dprintk("%s: server responded with %d layouts, dropping tail\n",
			__func__, layout_count);
	}

out:
	res->status = status;
	return status;
out_overflow:
	status = -EIO;
	goto out;
}

static int decode_layoutreturn(struct xdr_stream *xdr,
			       struct nfs4_layoutreturn_res *res)
{
	__be32 *p;
	int status;

	status = decode_op_hdr(xdr, OP_LAYOUTRETURN);
	if (status)
		return status;
	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;
	res->lrs_present = be32_to_cpup(p);
	if (res->lrs_present)
		status = decode_layout_stateid(xdr, &res->stateid);
	else
		nfs4_stateid_copy(&res->stateid, &invalid_stateid);
	return status;
}

static int decode_layoutcommit(struct xdr_stream *xdr,
			       struct rpc_rqst *req,
			       struct nfs4_layoutcommit_res *res)
{
	__be32 *p;
	__u32 sizechanged;
	int status;

	status = decode_op_hdr(xdr, OP_LAYOUTCOMMIT);
	res->status = status;
	if (status)
		return status;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;
	sizechanged = be32_to_cpup(p);

	if (sizechanged) {
		/* throw away new size */
		p = xdr_inline_decode(xdr, 8);
		if (unlikely(!p))
			return -EIO;
	}
	return 0;
}

static int decode_test_stateid(struct xdr_stream *xdr,
			       struct nfs41_test_stateid_res *res)
{
	__be32 *p;
	int status;
	int num_res;

	status = decode_op_hdr(xdr, OP_TEST_STATEID);
	if (status)
		return status;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;
	num_res = be32_to_cpup(p++);
	if (num_res != 1)
		return -EIO;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;
	res->status = be32_to_cpup(p++);

	return status;
}

static int decode_free_stateid(struct xdr_stream *xdr,
			       struct nfs41_free_stateid_res *res)
{
	res->status = decode_op_hdr(xdr, OP_FREE_STATEID);
	return res->status;
}
#else
static inline
int decode_layoutreturn(struct xdr_stream *xdr,
			       struct nfs4_layoutreturn_res *res)
{
	return 0;
}

static int decode_layoutget(struct xdr_stream *xdr, struct rpc_rqst *req,
			    struct nfs4_layoutget_res *res)
{
	return 0;
}

#endif /* CONFIG_NFS_V4_1 */

/*
 * END OF "GENERIC" DECODE ROUTINES.
 */

/*
 * Decode OPEN_DOWNGRADE response
 */
static int nfs4_xdr_dec_open_downgrade(struct rpc_rqst *rqstp,
				       struct xdr_stream *xdr,
				       void *data)
{
	struct nfs_closeres *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	if (res->lr_res) {
		status = decode_layoutreturn(xdr, res->lr_res);
		res->lr_ret = status;
		if (status)
			goto out;
	}
	status = decode_open_downgrade(xdr, res);
out:
	return status;
}

/*
 * Decode ACCESS response
 */
static int nfs4_xdr_dec_access(struct rpc_rqst *rqstp, struct xdr_stream *xdr,
			       void *data)
{
	struct nfs4_accessres *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status != 0)
		goto out;
	status = decode_access(xdr, &res->supported, &res->access);
	if (status != 0)
		goto out;
	if (res->fattr)
		decode_getfattr(xdr, res->fattr, res->server);
out:
	return status;
}

/*
 * Decode LOOKUP response
 */
static int nfs4_xdr_dec_lookup(struct rpc_rqst *rqstp, struct xdr_stream *xdr,
			       void *data)
{
	struct nfs4_lookup_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_lookup(xdr);
	if (status)
		goto out;
	status = decode_getfh(xdr, res->fh);
	if (status)
		goto out;
	status = decode_getfattr(xdr, res->fattr, res->server);
out:
	return status;
}

/*
 * Decode LOOKUPP response
 */
static int nfs4_xdr_dec_lookupp(struct rpc_rqst *rqstp, struct xdr_stream *xdr,
		void *data)
{
	struct nfs4_lookupp_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_lookupp(xdr);
	if (status)
		goto out;
	status = decode_getfh(xdr, res->fh);
	if (status)
		goto out;
	status = decode_getfattr(xdr, res->fattr, res->server);
out:
	return status;
}

/*
 * Decode LOOKUP_ROOT response
 */
static int nfs4_xdr_dec_lookup_root(struct rpc_rqst *rqstp,
				    struct xdr_stream *xdr,
				    void *data)
{
	struct nfs4_lookup_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putrootfh(xdr);
	if (status)
		goto out;
	status = decode_getfh(xdr, res->fh);
	if (status == 0)
		status = decode_getfattr(xdr, res->fattr, res->server);
out:
	return status;
}

/*
 * Decode REMOVE response
 */
static int nfs4_xdr_dec_remove(struct rpc_rqst *rqstp, struct xdr_stream *xdr,
			       void *data)
{
	struct nfs_removeres *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_remove(xdr, &res->cinfo);
out:
	return status;
}

/*
 * Decode RENAME response
 */
static int nfs4_xdr_dec_rename(struct rpc_rqst *rqstp, struct xdr_stream *xdr,
			       void *data)
{
	struct nfs_renameres *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_savefh(xdr);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_rename(xdr, &res->old_cinfo, &res->new_cinfo);
out:
	return status;
}

/*
 * Decode LINK response
 */
static int nfs4_xdr_dec_link(struct rpc_rqst *rqstp, struct xdr_stream *xdr,
			     void *data)
{
	struct nfs4_link_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_savefh(xdr);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_link(xdr, &res->cinfo);
	if (status)
		goto out;
	/*
	 * Note order: OP_LINK leaves the directory as the current
	 *             filehandle.
	 */
	status = decode_restorefh(xdr);
	if (status)
		goto out;
	decode_getfattr(xdr, res->fattr, res->server);
out:
	return status;
}

/*
 * Decode CREATE response
 */
static int nfs4_xdr_dec_create(struct rpc_rqst *rqstp, struct xdr_stream *xdr,
			       void *data)
{
	struct nfs4_create_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_create(xdr, &res->dir_cinfo);
	if (status)
		goto out;
	status = decode_getfh(xdr, res->fh);
	if (status)
		goto out;
	decode_getfattr(xdr, res->fattr, res->server);
out:
	return status;
}

/*
 * Decode SYMLINK response
 */
static int nfs4_xdr_dec_symlink(struct rpc_rqst *rqstp, struct xdr_stream *xdr,
				void *res)
{
	return nfs4_xdr_dec_create(rqstp, xdr, res);
}

/*
 * Decode GETATTR response
 */
static int nfs4_xdr_dec_getattr(struct rpc_rqst *rqstp, struct xdr_stream *xdr,
				void *data)
{
	struct nfs4_getattr_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_getfattr(xdr, res->fattr, res->server);
out:
	return status;
}

/*
 * Encode an SETACL request
 */
static void nfs4_xdr_enc_setacl(struct rpc_rqst *req, struct xdr_stream *xdr,
				const void *data)
{
	const struct nfs_setaclargs *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_setacl(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * Decode SETACL response
 */
static int
nfs4_xdr_dec_setacl(struct rpc_rqst *rqstp, struct xdr_stream *xdr,
		    void *data)
{
	struct nfs_setaclres *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_setattr(xdr);
out:
	return status;
}

/*
 * Decode GETACL response
 */
static int
nfs4_xdr_dec_getacl(struct rpc_rqst *rqstp, struct xdr_stream *xdr,
		    void *data)
{
	struct nfs_getaclres *res = data;
	struct compound_hdr hdr;
	int status;

	if (res->acl_scratch != NULL)
		xdr_set_scratch_page(xdr, res->acl_scratch);
	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_getacl(xdr, rqstp, res, res->acl_type);

out:
	return status;
}

/*
 * Decode CLOSE response
 */
static int nfs4_xdr_dec_close(struct rpc_rqst *rqstp, struct xdr_stream *xdr,
			      void *data)
{
	struct nfs_closeres *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	if (res->lr_res) {
		status = decode_layoutreturn(xdr, res->lr_res);
		res->lr_ret = status;
		if (status)
			goto out;
	}
	if (res->fattr != NULL) {
		status = decode_getfattr(xdr, res->fattr, res->server);
		if (status != 0)
			goto out;
	}
	status = decode_close(xdr, res);
out:
	return status;
}

/*
 * Decode OPEN response
 */
static int nfs4_xdr_dec_open(struct rpc_rqst *rqstp, struct xdr_stream *xdr,
			     void *data)
{
	struct nfs_openres *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_open(xdr, res);
	if (status)
		goto out;
	status = decode_getfh(xdr, &res->fh);
	if (status)
		goto out;
	if (res->access_request)
		decode_access(xdr, &res->access_supported, &res->access_result);
	decode_getfattr(xdr, res->f_attr, res->server);
	if (res->lg_res)
		decode_layoutget(xdr, rqstp, res->lg_res);
out:
	return status;
}

/*
 * Decode OPEN_CONFIRM response
 */
static int nfs4_xdr_dec_open_confirm(struct rpc_rqst *rqstp,
				     struct xdr_stream *xdr,
				     void *data)
{
	struct nfs_open_confirmres *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_open_confirm(xdr, res);
out:
	return status;
}

/*
 * Decode OPEN response
 */
static int nfs4_xdr_dec_open_noattr(struct rpc_rqst *rqstp,
				    struct xdr_stream *xdr,
				    void *data)
{
	struct nfs_openres *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_open(xdr, res);
	if (status)
		goto out;
	if (res->access_request)
		decode_access(xdr, &res->access_supported, &res->access_result);
	decode_getfattr(xdr, res->f_attr, res->server);
	if (res->lg_res)
		decode_layoutget(xdr, rqstp, res->lg_res);
out:
	return status;
}

/*
 * Decode SETATTR response
 */
static int nfs4_xdr_dec_setattr(struct rpc_rqst *rqstp,
				struct xdr_stream *xdr,
				void *data)
{
	struct nfs_setattrres *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_setattr(xdr);
	if (status)
		goto out;
	decode_getfattr(xdr, res->fattr, res->server);
out:
	return status;
}

/*
 * Decode LOCK response
 */
static int nfs4_xdr_dec_lock(struct rpc_rqst *rqstp, struct xdr_stream *xdr,
			     void *data)
{
	struct nfs_lock_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_lock(xdr, res);
out:
	return status;
}

/*
 * Decode LOCKT response
 */
static int nfs4_xdr_dec_lockt(struct rpc_rqst *rqstp, struct xdr_stream *xdr,
			      void *data)
{
	struct nfs_lockt_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_lockt(xdr, res);
out:
	return status;
}

/*
 * Decode LOCKU response
 */
static int nfs4_xdr_dec_locku(struct rpc_rqst *rqstp, struct xdr_stream *xdr,
			      void *data)
{
	struct nfs_locku_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_locku(xdr, res);
out:
	return status;
}

static int nfs4_xdr_dec_release_lockowner(struct rpc_rqst *rqstp,
					  struct xdr_stream *xdr, void *dummy)
{
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (!status)
		status = decode_release_lockowner(xdr);
	return status;
}

/*
 * Decode READLINK response
 */
static int nfs4_xdr_dec_readlink(struct rpc_rqst *rqstp,
				 struct xdr_stream *xdr,
				 void *data)
{
	struct nfs4_readlink_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_readlink(xdr, rqstp);
out:
	return status;
}

/*
 * Decode READDIR response
 */
static int nfs4_xdr_dec_readdir(struct rpc_rqst *rqstp, struct xdr_stream *xdr,
				void *data)
{
	struct nfs4_readdir_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_readdir(xdr, rqstp, res);
out:
	return status;
}

/*
 * Decode Read response
 */
static int nfs4_xdr_dec_read(struct rpc_rqst *rqstp, struct xdr_stream *xdr,
			     void *data)
{
	struct nfs_pgio_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	res->op_status = hdr.status;
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_read(xdr, rqstp, res);
	if (!status)
		status = res->count;
out:
	return status;
}

/*
 * Decode WRITE response
 */
static int nfs4_xdr_dec_write(struct rpc_rqst *rqstp, struct xdr_stream *xdr,
			      void *data)
{
	struct nfs_pgio_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	res->op_status = hdr.status;
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_write(xdr, res);
	if (status)
		goto out;
	if (res->fattr)
		decode_getfattr(xdr, res->fattr, res->server);
	if (!status)
		status = res->count;
out:
	return status;
}

/*
 * Decode COMMIT response
 */
static int nfs4_xdr_dec_commit(struct rpc_rqst *rqstp, struct xdr_stream *xdr,
			       void *data)
{
	struct nfs_commitres *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	res->op_status = hdr.status;
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_commit(xdr, res);
out:
	return status;
}

/*
 * Decode FSINFO response
 */
static int nfs4_xdr_dec_fsinfo(struct rpc_rqst *req, struct xdr_stream *xdr,
			       void *data)
{
	struct nfs4_fsinfo_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (!status)
		status = decode_sequence(xdr, &res->seq_res, req);
	if (!status)
		status = decode_putfh(xdr);
	if (!status)
		status = decode_fsinfo(xdr, res->fsinfo);
	return status;
}

/*
 * Decode PATHCONF response
 */
static int nfs4_xdr_dec_pathconf(struct rpc_rqst *req, struct xdr_stream *xdr,
				 void *data)
{
	struct nfs4_pathconf_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (!status)
		status = decode_sequence(xdr, &res->seq_res, req);
	if (!status)
		status = decode_putfh(xdr);
	if (!status)
		status = decode_pathconf(xdr, res->pathconf);
	return status;
}

/*
 * Decode STATFS response
 */
static int nfs4_xdr_dec_statfs(struct rpc_rqst *req, struct xdr_stream *xdr,
			       void *data)
{
	struct nfs4_statfs_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (!status)
		status = decode_sequence(xdr, &res->seq_res, req);
	if (!status)
		status = decode_putfh(xdr);
	if (!status)
		status = decode_statfs(xdr, res->fsstat);
	return status;
}

/*
 * Decode GETATTR_BITMAP response
 */
static int nfs4_xdr_dec_server_caps(struct rpc_rqst *req,
				    struct xdr_stream *xdr,
				    void *data)
{
	struct nfs4_server_caps_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, req);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_server_caps(xdr, res);
out:
	return status;
}

/*
 * Decode RENEW response
 */
static int nfs4_xdr_dec_renew(struct rpc_rqst *rqstp, struct xdr_stream *xdr,
			      void *__unused)
{
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (!status)
		status = decode_renew(xdr);
	return status;
}

/*
 * Decode SETCLIENTID response
 */
static int nfs4_xdr_dec_setclientid(struct rpc_rqst *req,
				    struct xdr_stream *xdr,
				    void *data)
{
	struct nfs4_setclientid_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (!status)
		status = decode_setclientid(xdr, res);
	return status;
}

/*
 * Decode SETCLIENTID_CONFIRM response
 */
static int nfs4_xdr_dec_setclientid_confirm(struct rpc_rqst *req,
					    struct xdr_stream *xdr,
					    void *data)
{
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (!status)
		status = decode_setclientid_confirm(xdr);
	return status;
}

/*
 * Decode DELEGRETURN response
 */
static int nfs4_xdr_dec_delegreturn(struct rpc_rqst *rqstp,
				    struct xdr_stream *xdr,
				    void *data)
{
	struct nfs4_delegreturnres *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status != 0)
		goto out;
	if (res->lr_res) {
		status = decode_layoutreturn(xdr, res->lr_res);
		res->lr_ret = status;
		if (status)
			goto out;
	}
	if (res->fattr) {
		status = decode_getfattr(xdr, res->fattr, res->server);
		if (status != 0)
			goto out;
	}
	status = decode_delegreturn(xdr);
out:
	return status;
}

/*
 * Decode FS_LOCATIONS response
 */
static int nfs4_xdr_dec_fs_locations(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     void *data)
{
	struct nfs4_fs_locations_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, req);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	if (res->migration) {
		xdr_enter_page(xdr, PAGE_SIZE);
		status = decode_getfattr_generic(xdr,
					res->fs_locations->fattr,
					 NULL, res->fs_locations,
					 res->fs_locations->server);
		if (status)
			goto out;
		if (res->renew)
			status = decode_renew(xdr);
	} else {
		status = decode_lookup(xdr);
		if (status)
			goto out;
		xdr_enter_page(xdr, PAGE_SIZE);
		status = decode_getfattr_generic(xdr,
					res->fs_locations->fattr,
					 NULL, res->fs_locations,
					 res->fs_locations->server);
	}
out:
	return status;
}

/*
 * Decode SECINFO response
 */
static int nfs4_xdr_dec_secinfo(struct rpc_rqst *rqstp,
				struct xdr_stream *xdr,
				void *data)
{
	struct nfs4_secinfo_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_secinfo(xdr, res);
out:
	return status;
}

/*
 * Decode FSID_PRESENT response
 */
static int nfs4_xdr_dec_fsid_present(struct rpc_rqst *rqstp,
				     struct xdr_stream *xdr,
				     void *data)
{
	struct nfs4_fsid_present_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_getfh(xdr, res->fh);
	if (status)
		goto out;
	if (res->renew)
		status = decode_renew(xdr);
out:
	return status;
}

#if defined(CONFIG_NFS_V4_1)
/*
 * Decode BIND_CONN_TO_SESSION response
 */
static int nfs4_xdr_dec_bind_conn_to_session(struct rpc_rqst *rqstp,
					struct xdr_stream *xdr,
					void *res)
{
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (!status)
		status = decode_bind_conn_to_session(xdr, res);
	return status;
}

/*
 * Decode EXCHANGE_ID response
 */
static int nfs4_xdr_dec_exchange_id(struct rpc_rqst *rqstp,
				    struct xdr_stream *xdr,
				    void *res)
{
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (!status)
		status = decode_exchange_id(xdr, res);
	return status;
}

/*
 * Decode CREATE_SESSION response
 */
static int nfs4_xdr_dec_create_session(struct rpc_rqst *rqstp,
				       struct xdr_stream *xdr,
				       void *res)
{
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (!status)
		status = decode_create_session(xdr, res);
	return status;
}

/*
 * Decode DESTROY_SESSION response
 */
static int nfs4_xdr_dec_destroy_session(struct rpc_rqst *rqstp,
					struct xdr_stream *xdr,
					void *res)
{
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (!status)
		status = decode_destroy_session(xdr, res);
	return status;
}

/*
 * Decode DESTROY_CLIENTID response
 */
static int nfs4_xdr_dec_destroy_clientid(struct rpc_rqst *rqstp,
					struct xdr_stream *xdr,
					void *res)
{
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (!status)
		status = decode_destroy_clientid(xdr, res);
	return status;
}

/*
 * Decode SEQUENCE response
 */
static int nfs4_xdr_dec_sequence(struct rpc_rqst *rqstp,
				 struct xdr_stream *xdr,
				 void *res)
{
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (!status)
		status = decode_sequence(xdr, res, rqstp);
	return status;
}

#endif

/*
 * Decode GET_LEASE_TIME response
 */
static int nfs4_xdr_dec_get_lease_time(struct rpc_rqst *rqstp,
				       struct xdr_stream *xdr,
				       void *data)
{
	struct nfs4_get_lease_time_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (!status)
		status = decode_sequence(xdr, &res->lr_seq_res, rqstp);
	if (!status)
		status = decode_putrootfh(xdr);
	if (!status)
		status = decode_fsinfo(xdr, res->lr_fsinfo);
	return status;
}

#ifdef CONFIG_NFS_V4_1

/*
 * Decode RECLAIM_COMPLETE response
 */
static int nfs4_xdr_dec_reclaim_complete(struct rpc_rqst *rqstp,
					 struct xdr_stream *xdr,
					 void *data)
{
	struct nfs41_reclaim_complete_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (!status)
		status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (!status)
		status = decode_reclaim_complete(xdr, NULL);
	return status;
}

/*
 * Decode GETDEVINFO response
 */
static int nfs4_xdr_dec_getdeviceinfo(struct rpc_rqst *rqstp,
				      struct xdr_stream *xdr,
				      void *data)
{
	struct nfs4_getdeviceinfo_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status != 0)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status != 0)
		goto out;
	status = decode_getdeviceinfo(xdr, res);
out:
	return status;
}

/*
 * Decode LAYOUTGET response
 */
static int nfs4_xdr_dec_layoutget(struct rpc_rqst *rqstp,
				  struct xdr_stream *xdr,
				  void *data)
{
	struct nfs4_layoutget_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_layoutget(xdr, rqstp, res);
out:
	return status;
}

/*
 * Decode LAYOUTRETURN response
 */
static int nfs4_xdr_dec_layoutreturn(struct rpc_rqst *rqstp,
				     struct xdr_stream *xdr,
				     void *data)
{
	struct nfs4_layoutreturn_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_layoutreturn(xdr, res);
out:
	return status;
}

/*
 * Decode LAYOUTCOMMIT response
 */
static int nfs4_xdr_dec_layoutcommit(struct rpc_rqst *rqstp,
				     struct xdr_stream *xdr,
				     void *data)
{
	struct nfs4_layoutcommit_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_layoutcommit(xdr, rqstp, res);
	if (status)
		goto out;
	decode_getfattr(xdr, res->fattr, res->server);
out:
	return status;
}

/*
 * Decode SECINFO_NO_NAME response
 */
static int nfs4_xdr_dec_secinfo_no_name(struct rpc_rqst *rqstp,
					struct xdr_stream *xdr,
					void *data)
{
	struct nfs4_secinfo_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putrootfh(xdr);
	if (status)
		goto out;
	status = decode_secinfo_no_name(xdr, res);
out:
	return status;
}

/*
 * Decode TEST_STATEID response
 */
static int nfs4_xdr_dec_test_stateid(struct rpc_rqst *rqstp,
				     struct xdr_stream *xdr,
				     void *data)
{
	struct nfs41_test_stateid_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_test_stateid(xdr, res);
out:
	return status;
}

/*
 * Decode FREE_STATEID response
 */
static int nfs4_xdr_dec_free_stateid(struct rpc_rqst *rqstp,
				     struct xdr_stream *xdr,
				     void *data)
{
	struct nfs41_free_stateid_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_free_stateid(xdr, res);
out:
	return status;
}
#endif /* CONFIG_NFS_V4_1 */

/**
 * nfs4_decode_dirent - Decode a single NFSv4 directory entry stored in
 *                      the local page cache.
 * @xdr: XDR stream where entry resides
 * @entry: buffer to fill in with entry data
 * @plus: boolean indicating whether this should be a readdirplus entry
 *
 * Returns zero if successful, otherwise a negative errno value is
 * returned.
 *
 * This function is not invoked during READDIR reply decoding, but
 * rather whenever an application invokes the getdents(2) system call
 * on a directory already in our cache.
 */
int nfs4_decode_dirent(struct xdr_stream *xdr, struct nfs_entry *entry,
		       bool plus)
{
	unsigned int savep;
	uint32_t bitmap[3] = {0};
	uint32_t len;
	uint64_t new_cookie;
	__be32 *p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EAGAIN;
	if (*p == xdr_zero) {
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EAGAIN;
		if (*p == xdr_zero)
			return -EAGAIN;
		entry->eof = 1;
		return -EBADCOOKIE;
	}

	p = xdr_inline_decode(xdr, 12);
	if (unlikely(!p))
		return -EAGAIN;
	p = xdr_decode_hyper(p, &new_cookie);
	entry->len = be32_to_cpup(p);

	p = xdr_inline_decode(xdr, entry->len);
	if (unlikely(!p))
		return -EAGAIN;
	entry->name = (const char *) p;

	/*
	 * In case the server doesn't return an inode number,
	 * we fake one here.  (We don't use inode number 0,
	 * since glibc seems to choke on it...)
	 */
	entry->ino = 1;
	entry->fattr->valid = 0;

	if (decode_attr_bitmap(xdr, bitmap) < 0)
		return -EAGAIN;

	if (decode_attr_length(xdr, &len, &savep) < 0)
		return -EAGAIN;

	if (decode_getfattr_attrs(xdr, bitmap, entry->fattr, entry->fh,
			NULL, entry->server) < 0)
		return -EAGAIN;
	if (entry->fattr->valid & NFS_ATTR_FATTR_MOUNTED_ON_FILEID)
		entry->ino = entry->fattr->mounted_on_fileid;
	else if (entry->fattr->valid & NFS_ATTR_FATTR_FILEID)
		entry->ino = entry->fattr->fileid;

	entry->d_type = DT_UNKNOWN;
	if (entry->fattr->valid & NFS_ATTR_FATTR_TYPE)
		entry->d_type = nfs_umode_to_dtype(entry->fattr->mode);

	entry->cookie = new_cookie;

	return 0;
}

/*
 * We need to translate between nfs status return values and
 * the local errno values which may not be the same.
 */
static struct {
	int stat;
	int errno;
} nfs_errtbl[] = {
	{ NFS4_OK,		0		},
	{ NFS4ERR_PERM,		-EPERM		},
	{ NFS4ERR_NOENT,	-ENOENT		},
	{ NFS4ERR_IO,		-errno_NFSERR_IO},
	{ NFS4ERR_NXIO,		-ENXIO		},
	{ NFS4ERR_ACCESS,	-EACCES		},
	{ NFS4ERR_EXIST,	-EEXIST		},
	{ NFS4ERR_XDEV,		-EXDEV		},
	{ NFS4ERR_NOTDIR,	-ENOTDIR	},
	{ NFS4ERR_ISDIR,	-EISDIR		},
	{ NFS4ERR_INVAL,	-EINVAL		},
	{ NFS4ERR_FBIG,		-EFBIG		},
	{ NFS4ERR_NOSPC,	-ENOSPC		},
	{ NFS4ERR_ROFS,		-EROFS		},
	{ NFS4ERR_MLINK,	-EMLINK		},
	{ NFS4ERR_NAMETOOLONG,	-ENAMETOOLONG	},
	{ NFS4ERR_NOTEMPTY,	-ENOTEMPTY	},
	{ NFS4ERR_DQUOT,	-EDQUOT		},
	{ NFS4ERR_STALE,	-ESTALE		},
	{ NFS4ERR_BADHANDLE,	-EBADHANDLE	},
	{ NFS4ERR_BAD_COOKIE,	-EBADCOOKIE	},
	{ NFS4ERR_NOTSUPP,	-ENOTSUPP	},
	{ NFS4ERR_TOOSMALL,	-ETOOSMALL	},
	{ NFS4ERR_SERVERFAULT,	-EREMOTEIO	},
	{ NFS4ERR_BADTYPE,	-EBADTYPE	},
	{ NFS4ERR_LOCKED,	-EAGAIN		},
	{ NFS4ERR_SYMLINK,	-ELOOP		},
	{ NFS4ERR_OP_ILLEGAL,	-EOPNOTSUPP	},
	{ NFS4ERR_DEADLOCK,	-EDEADLK	},
	{ NFS4ERR_NOXATTR,	-ENODATA	},
	{ NFS4ERR_XATTR2BIG,	-E2BIG		},
	{ -1,			-EIO		}
};

/*
 * Convert an NFS error code to a local one.
 * This one is used jointly by NFSv2 and NFSv3.
 */
static int
nfs4_stat_to_errno(int stat)
{
	int i;
	for (i = 0; nfs_errtbl[i].stat != -1; i++) {
		if (nfs_errtbl[i].stat == stat)
			return nfs_errtbl[i].errno;
	}
	if (stat <= 10000 || stat > 10100) {
		/* The server is looney tunes. */
		return -EREMOTEIO;
	}
	/* If we cannot translate the error, the recovery routines should
	 * handle it.
	 * Note: remaining NFSv4 error codes have values > 10000, so should
	 * not conflict with native Linux error codes.
	 */
	return -stat;
}

#ifdef CONFIG_NFS_V4_2
#include "nfs42xdr.c"
#endif /* CONFIG_NFS_V4_2 */

#define PROC(proc, argtype, restype)				\
[NFSPROC4_CLNT_##proc] = {					\
	.p_proc   = NFSPROC4_COMPOUND,				\
	.p_encode = nfs4_xdr_##argtype,				\
	.p_decode = nfs4_xdr_##restype,				\
	.p_arglen = NFS4_##argtype##_sz,			\
	.p_replen = NFS4_##restype##_sz,			\
	.p_statidx = NFSPROC4_CLNT_##proc,			\
	.p_name   = #proc,					\
}

#define STUB(proc)		\
[NFSPROC4_CLNT_##proc] = {	\
	.p_name = #proc,	\
}

#if defined(CONFIG_NFS_V4_1)
#define PROC41(proc, argtype, restype)				\
	PROC(proc, argtype, restype)
#else
#define PROC41(proc, argtype, restype)				\
	STUB(proc)
#endif

#if defined(CONFIG_NFS_V4_2)
#define PROC42(proc, argtype, restype)				\
	PROC(proc, argtype, restype)
#else
#define PROC42(proc, argtype, restype)				\
	STUB(proc)
#endif

const struct rpc_procinfo nfs4_procedures[] = {
	PROC(READ,		enc_read,		dec_read),
	PROC(WRITE,		enc_write,		dec_write),
	PROC(COMMIT,		enc_commit,		dec_commit),
	PROC(OPEN,		enc_open,		dec_open),
	PROC(OPEN_CONFIRM,	enc_open_confirm,	dec_open_confirm),
	PROC(OPEN_NOATTR,	enc_open_noattr,	dec_open_noattr),
	PROC(OPEN_DOWNGRADE,	enc_open_downgrade,	dec_open_downgrade),
	PROC(CLOSE,		enc_close,		dec_close),
	PROC(SETATTR,		enc_setattr,		dec_setattr),
	PROC(FSINFO,		enc_fsinfo,		dec_fsinfo),
	PROC(RENEW,		enc_renew,		dec_renew),
	PROC(SETCLIENTID,	enc_setclientid,	dec_setclientid),
	PROC(SETCLIENTID_CONFIRM, enc_setclientid_confirm, dec_setclientid_confirm),
	PROC(LOCK,		enc_lock,		dec_lock),
	PROC(LOCKT,		enc_lockt,		dec_lockt),
	PROC(LOCKU,		enc_locku,		dec_locku),
	PROC(ACCESS,		enc_access,		dec_access),
	PROC(GETATTR,		enc_getattr,		dec_getattr),
	PROC(LOOKUP,		enc_lookup,		dec_lookup),
	PROC(LOOKUP_ROOT,	enc_lookup_root,	dec_lookup_root),
	PROC(REMOVE,		enc_remove,		dec_remove),
	PROC(RENAME,		enc_rename,		dec_rename),
	PROC(LINK,		enc_link,		dec_link),
	PROC(SYMLINK,		enc_symlink,		dec_symlink),
	PROC(CREATE,		enc_create,		dec_create),
	PROC(PATHCONF,		enc_pathconf,		dec_pathconf),
	PROC(STATFS,		enc_statfs,		dec_statfs),
	PROC(READLINK,		enc_readlink,		dec_readlink),
	PROC(READDIR,		enc_readdir,		dec_readdir),
	PROC(SERVER_CAPS,	enc_server_caps,	dec_server_caps),
	PROC(DELEGRETURN,	enc_delegreturn,	dec_delegreturn),
	PROC(GETACL,		enc_getacl,		dec_getacl),
	PROC(SETACL,		enc_setacl,		dec_setacl),
	PROC(FS_LOCATIONS,	enc_fs_locations,	dec_fs_locations),
	PROC(RELEASE_LOCKOWNER,	enc_release_lockowner,	dec_release_lockowner),
	PROC(SECINFO,		enc_secinfo,		dec_secinfo),
	PROC(FSID_PRESENT,	enc_fsid_present,	dec_fsid_present),
	PROC41(EXCHANGE_ID,	enc_exchange_id,	dec_exchange_id),
	PROC41(CREATE_SESSION,	enc_create_session,	dec_create_session),
	PROC41(DESTROY_SESSION,	enc_destroy_session,	dec_destroy_session),
	PROC41(SEQUENCE,	enc_sequence,		dec_sequence),
	PROC(GET_LEASE_TIME,	enc_get_lease_time,	dec_get_lease_time),
	PROC41(RECLAIM_COMPLETE,enc_reclaim_complete,	dec_reclaim_complete),
	PROC41(GETDEVICEINFO,	enc_getdeviceinfo,	dec_getdeviceinfo),
	PROC41(LAYOUTGET,	enc_layoutget,		dec_layoutget),
	PROC41(LAYOUTCOMMIT,	enc_layoutcommit,	dec_layoutcommit),
	PROC41(LAYOUTRETURN,	enc_layoutreturn,	dec_layoutreturn),
	PROC41(SECINFO_NO_NAME,	enc_secinfo_no_name,	dec_secinfo_no_name),
	PROC41(TEST_STATEID,	enc_test_stateid,	dec_test_stateid),
	PROC41(FREE_STATEID,	enc_free_stateid,	dec_free_stateid),
	STUB(GETDEVICELIST),
	PROC41(BIND_CONN_TO_SESSION,
			enc_bind_conn_to_session, dec_bind_conn_to_session),
	PROC41(DESTROY_CLIENTID,enc_destroy_clientid,	dec_destroy_clientid),
	PROC42(SEEK,		enc_seek,		dec_seek),
	PROC42(ALLOCATE,	enc_allocate,		dec_allocate),
	PROC42(DEALLOCATE,	enc_deallocate,		dec_deallocate),
	PROC42(LAYOUTSTATS,	enc_layoutstats,	dec_layoutstats),
	PROC42(CLONE,		enc_clone,		dec_clone),
	PROC42(COPY,		enc_copy,		dec_copy),
	PROC42(OFFLOAD_CANCEL,	enc_offload_cancel,	dec_offload_cancel),
	PROC42(COPY_NOTIFY,	enc_copy_notify,	dec_copy_notify),
	PROC(LOOKUPP,		enc_lookupp,		dec_lookupp),
	PROC42(LAYOUTERROR,	enc_layouterror,	dec_layouterror),
	PROC42(GETXATTR,	enc_getxattr,		dec_getxattr),
	PROC42(SETXATTR,	enc_setxattr,		dec_setxattr),
	PROC42(LISTXATTRS,	enc_listxattrs,		dec_listxattrs),
	PROC42(REMOVEXATTR,	enc_removexattr,	dec_removexattr),
	PROC42(READ_PLUS,	enc_read_plus,		dec_read_plus),
};

static unsigned int nfs_version4_counts[ARRAY_SIZE(nfs4_procedures)];
const struct rpc_version nfs_version4 = {
	.number			= 4,
	.nrprocs		= ARRAY_SIZE(nfs4_procedures),
	.procs			= nfs4_procedures,
	.counts			= nfs_version4_counts,
};
