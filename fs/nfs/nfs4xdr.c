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
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/msg_prot.h>
#include <linux/nfs.h>
#include <linux/nfs4.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_idmap.h>
#include "nfs4_fs.h"
#include "internal.h"

#define NFSDBG_FACILITY		NFSDBG_XDR

/* Mapping from NFS error code to "errno" error code. */
#define errno_NFSERR_IO		EIO

static int nfs4_stat_to_errno(int);

/* NFSv4 COMPOUND tags are only wanted for debugging purposes */
#ifdef DEBUG
#define NFS4_MAXTAGLEN		20
#else
#define NFS4_MAXTAGLEN		0
#endif

/* lock,open owner id:
 * we currently use size 2 (u64) out of (NFS4_OPAQUE_LIMIT  >> 2)
 */
#define open_owner_id_maxsz	(1 + 4)
#define lock_owner_id_maxsz	(1 + 4)
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
#define nfs4_fattr_bitmap_maxsz 3
#define encode_getattr_maxsz    (op_encode_hdr_maxsz + nfs4_fattr_bitmap_maxsz)
#define nfs4_name_maxsz		(1 + ((3 + NFS4_MAXNAMLEN) >> 2))
#define nfs4_path_maxsz		(1 + ((3 + NFS4_MAXPATHLEN) >> 2))
#define nfs4_owner_maxsz	(1 + XDR_QUADLEN(IDMAP_NAMESZ))
#define nfs4_group_maxsz	(1 + XDR_QUADLEN(IDMAP_NAMESZ))
/* This is based on getfattr, which uses the most attributes: */
#define nfs4_fattr_value_maxsz	(1 + (1 + 2 + 2 + 4 + 2 + 1 + 1 + 2 + 2 + \
				3 + 3 + 3 + nfs4_owner_maxsz + nfs4_group_maxsz))
#define nfs4_fattr_maxsz	(nfs4_fattr_bitmap_maxsz + \
				nfs4_fattr_value_maxsz)
#define decode_getattr_maxsz    (op_decode_hdr_maxsz + nfs4_fattr_maxsz)
#define encode_attrs_maxsz	(nfs4_fattr_bitmap_maxsz + \
				 1 + 2 + 1 + \
				nfs4_owner_maxsz + \
				nfs4_group_maxsz + \
				4 + 4)
#define encode_savefh_maxsz     (op_encode_hdr_maxsz)
#define decode_savefh_maxsz     (op_decode_hdr_maxsz)
#define encode_restorefh_maxsz  (op_encode_hdr_maxsz)
#define decode_restorefh_maxsz  (op_decode_hdr_maxsz)
#define encode_fsinfo_maxsz	(encode_getattr_maxsz)
#define decode_fsinfo_maxsz	(op_decode_hdr_maxsz + 11)
#define encode_renew_maxsz	(op_encode_hdr_maxsz + 3)
#define decode_renew_maxsz	(op_decode_hdr_maxsz)
#define encode_setclientid_maxsz \
				(op_encode_hdr_maxsz + \
				XDR_QUADLEN(NFS4_VERIFIER_SIZE) + \
				XDR_QUADLEN(NFS4_SETCLIENTID_NAMELEN) + \
				1 /* sc_prog */ + \
				XDR_QUADLEN(RPCBIND_MAXNETIDLEN) + \
				XDR_QUADLEN(RPCBIND_MAXUADDRLEN) + \
				1) /* sc_cb_ident */
#define decode_setclientid_maxsz \
				(op_decode_hdr_maxsz + \
				2 + \
				1024) /* large value for CLID_INUSE */
#define encode_setclientid_confirm_maxsz \
				(op_encode_hdr_maxsz + \
				3 + (NFS4_VERIFIER_SIZE >> 2))
#define decode_setclientid_confirm_maxsz \
				(op_decode_hdr_maxsz)
#define encode_lookup_maxsz	(op_encode_hdr_maxsz + nfs4_name_maxsz)
#define decode_lookup_maxsz	(op_decode_hdr_maxsz)
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
#define decode_ace_maxsz	(3 + nfs4_owner_maxsz)
#define decode_delegation_maxsz	(1 + decode_stateid_maxsz + 1 + \
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
#define decode_read_maxsz	(op_decode_hdr_maxsz + 2)
#define encode_readdir_maxsz	(op_encode_hdr_maxsz + \
				 2 + encode_verifier_maxsz + 5)
#define decode_readdir_maxsz	(op_decode_hdr_maxsz + \
				 decode_verifier_maxsz)
#define encode_readlink_maxsz	(op_encode_hdr_maxsz)
#define decode_readlink_maxsz	(op_decode_hdr_maxsz + 1)
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
#define encode_lock_maxsz	(op_encode_hdr_maxsz + \
				 7 + \
				 1 + encode_stateid_maxsz + 8)
#define decode_lock_denied_maxsz \
				(8 + decode_lockowner_maxsz)
#define decode_lock_maxsz	(op_decode_hdr_maxsz + \
				 decode_lock_denied_maxsz)
#define encode_lockt_maxsz	(op_encode_hdr_maxsz + 12)
#define decode_lockt_maxsz	(op_decode_hdr_maxsz + \
				 decode_lock_denied_maxsz)
#define encode_locku_maxsz	(op_encode_hdr_maxsz + 3 + \
				 encode_stateid_maxsz + \
				 4)
#define decode_locku_maxsz	(op_decode_hdr_maxsz + \
				 decode_stateid_maxsz)
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
				 nfs4_fattr_bitmap_maxsz + 1)
#define encode_setacl_maxsz	(op_encode_hdr_maxsz + \
				 encode_stateid_maxsz + 3)
#define decode_setacl_maxsz	(decode_setattr_maxsz)
#define encode_fs_locations_maxsz \
				(encode_getattr_maxsz)
#define decode_fs_locations_maxsz \
				(0)

#if defined(CONFIG_NFS_V4_1)
#define NFS4_MAX_MACHINE_NAME_LEN (64)

#define encode_exchange_id_maxsz (op_encode_hdr_maxsz + \
				encode_verifier_maxsz + \
				1 /* co_ownerid.len */ + \
				XDR_QUADLEN(NFS4_EXCHANGE_ID_LEN) + \
				1 /* flags */ + \
				1 /* spa_how */ + \
				0 /* SP4_NONE (for now) */ + \
				1 /* zero implemetation id array */)
#define decode_exchange_id_maxsz (op_decode_hdr_maxsz + \
				2 /* eir_clientid */ + \
				1 /* eir_sequenceid */ + \
				1 /* eir_flags */ + \
				1 /* spr_how */ + \
				0 /* SP4_NONE (for now) */ + \
				2 /* eir_server_owner.so_minor_id */ + \
				/* eir_server_owner.so_major_id<> */ \
				XDR_QUADLEN(NFS4_OPAQUE_LIMIT) + 1 + \
				/* eir_server_scope<> */ \
				XDR_QUADLEN(NFS4_OPAQUE_LIMIT) + 1 + \
				1 /* eir_server_impl_id array length */ + \
				0 /* ignored eir_server_impl_id contents */)
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
#define encode_destroy_session_maxsz    (op_encode_hdr_maxsz + 4)
#define decode_destroy_session_maxsz    (op_decode_hdr_maxsz)
#define encode_sequence_maxsz	(op_encode_hdr_maxsz + \
				XDR_QUADLEN(NFS4_MAX_SESSIONID_LEN) + 4)
#define decode_sequence_maxsz	(op_decode_hdr_maxsz + \
				XDR_QUADLEN(NFS4_MAX_SESSIONID_LEN) + 5)
#define encode_reclaim_complete_maxsz	(op_encode_hdr_maxsz + 4)
#define decode_reclaim_complete_maxsz	(op_decode_hdr_maxsz + 4)
#else /* CONFIG_NFS_V4_1 */
#define encode_sequence_maxsz	0
#define decode_sequence_maxsz	0
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
				encode_commit_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_commit_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_commit_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_open_sz        (compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_savefh_maxsz + \
				encode_open_maxsz + \
				encode_getfh_maxsz + \
				encode_getattr_maxsz + \
				encode_restorefh_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_open_sz        (compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_savefh_maxsz + \
				decode_open_maxsz + \
				decode_getfh_maxsz + \
				decode_getattr_maxsz + \
				decode_restorefh_maxsz + \
				decode_getattr_maxsz)
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
					encode_getattr_maxsz)
#define NFS4_dec_open_noattr_sz	(compound_decode_hdr_maxsz + \
					decode_sequence_maxsz + \
					decode_putfh_maxsz + \
					decode_open_maxsz + \
					decode_getattr_maxsz)
#define NFS4_enc_open_downgrade_sz \
				(compound_encode_hdr_maxsz + \
				 encode_sequence_maxsz + \
				 encode_putfh_maxsz + \
				 encode_open_downgrade_maxsz + \
				 encode_getattr_maxsz)
#define NFS4_dec_open_downgrade_sz \
				(compound_decode_hdr_maxsz + \
				 decode_sequence_maxsz + \
				 decode_putfh_maxsz + \
				 decode_open_downgrade_maxsz + \
				 decode_getattr_maxsz)
#define NFS4_enc_close_sz	(compound_encode_hdr_maxsz + \
				 encode_sequence_maxsz + \
				 encode_putfh_maxsz + \
				 encode_close_maxsz + \
				 encode_getattr_maxsz)
#define NFS4_dec_close_sz	(compound_decode_hdr_maxsz + \
				 decode_sequence_maxsz + \
				 decode_putfh_maxsz + \
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
				encode_setclientid_confirm_maxsz + \
				encode_putrootfh_maxsz + \
				encode_fsinfo_maxsz)
#define NFS4_dec_setclientid_confirm_sz \
				(compound_decode_hdr_maxsz + \
				decode_setclientid_confirm_maxsz + \
				decode_putrootfh_maxsz + \
				decode_fsinfo_maxsz)
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
				encode_getattr_maxsz)
#define NFS4_dec_getattr_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_getattr_maxsz)
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
				encode_remove_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_remove_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_remove_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_rename_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_savefh_maxsz + \
				encode_putfh_maxsz + \
				encode_rename_maxsz + \
				encode_getattr_maxsz + \
				encode_restorefh_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_rename_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_savefh_maxsz + \
				decode_putfh_maxsz + \
				decode_rename_maxsz + \
				decode_getattr_maxsz + \
				decode_restorefh_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_link_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_savefh_maxsz + \
				encode_putfh_maxsz + \
				encode_link_maxsz + \
				decode_getattr_maxsz + \
				encode_restorefh_maxsz + \
				decode_getattr_maxsz)
#define NFS4_dec_link_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_savefh_maxsz + \
				decode_putfh_maxsz + \
				decode_link_maxsz + \
				decode_getattr_maxsz + \
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
				encode_savefh_maxsz + \
				encode_create_maxsz + \
				encode_getfh_maxsz + \
				encode_getattr_maxsz + \
				encode_restorefh_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_create_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_savefh_maxsz + \
				decode_create_maxsz + \
				decode_getfh_maxsz + \
				decode_getattr_maxsz + \
				decode_restorefh_maxsz + \
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
				encode_delegreturn_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_delegreturn_sz (compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
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
				 encode_fs_locations_maxsz)
#define NFS4_dec_fs_locations_sz \
				(compound_decode_hdr_maxsz + \
				 decode_sequence_maxsz + \
				 decode_putfh_maxsz + \
				 decode_lookup_maxsz + \
				 decode_fs_locations_maxsz)
#if defined(CONFIG_NFS_V4_1)
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
#define NFS4_enc_sequence_sz \
				(compound_decode_hdr_maxsz + \
				 encode_sequence_maxsz)
#define NFS4_dec_sequence_sz \
				(compound_decode_hdr_maxsz + \
				 decode_sequence_maxsz)
#define NFS4_enc_get_lease_time_sz	(compound_encode_hdr_maxsz + \
					 encode_sequence_maxsz + \
					 encode_putrootfh_maxsz + \
					 encode_fsinfo_maxsz)
#define NFS4_dec_get_lease_time_sz	(compound_decode_hdr_maxsz + \
					 decode_sequence_maxsz + \
					 decode_putrootfh_maxsz + \
					 decode_fsinfo_maxsz)
#define NFS4_enc_reclaim_complete_sz	(compound_encode_hdr_maxsz + \
					 encode_sequence_maxsz + \
					 encode_reclaim_complete_maxsz)
#define NFS4_dec_reclaim_complete_sz	(compound_decode_hdr_maxsz + \
					 decode_sequence_maxsz + \
					 decode_reclaim_complete_maxsz)

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

static void encode_string(struct xdr_stream *xdr, unsigned int len, const char *str)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, 4 + len);
	BUG_ON(p == NULL);
	xdr_encode_opaque(p, str, len);
}

static void encode_compound_hdr(struct xdr_stream *xdr,
				struct rpc_rqst *req,
				struct compound_hdr *hdr)
{
	__be32 *p;
	struct rpc_auth *auth = req->rq_task->tk_msg.rpc_cred->cr_auth;

	/* initialize running count of expected bytes in reply.
	 * NOTE: the replied tag SHOULD be the same is the one sent,
	 * but this is not required as a MUST for the server to do so. */
	hdr->replen = RPC_REPHDRSIZE + auth->au_rslack + 3 + hdr->taglen;

	dprintk("encode_compound: tag=%.*s\n", (int)hdr->taglen, hdr->tag);
	BUG_ON(hdr->taglen > NFS4_MAXTAGLEN);
	p = reserve_space(xdr, 4 + hdr->taglen + 8);
	p = xdr_encode_opaque(p, hdr->tag, hdr->taglen);
	*p++ = cpu_to_be32(hdr->minorversion);
	hdr->nops_p = p;
	*p = cpu_to_be32(hdr->nops);
}

static void encode_nops(struct compound_hdr *hdr)
{
	BUG_ON(hdr->nops > NFS4_MAX_OPS);
	*hdr->nops_p = htonl(hdr->nops);
}

static void encode_nfs4_verifier(struct xdr_stream *xdr, const nfs4_verifier *verf)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, NFS4_VERIFIER_SIZE);
	BUG_ON(p == NULL);
	xdr_encode_opaque_fixed(p, verf->data, NFS4_VERIFIER_SIZE);
}

static void encode_attrs(struct xdr_stream *xdr, const struct iattr *iap, const struct nfs_server *server)
{
	char owner_name[IDMAP_NAMESZ];
	char owner_group[IDMAP_NAMESZ];
	int owner_namelen = 0;
	int owner_grouplen = 0;
	__be32 *p;
	__be32 *q;
	int len;
	uint32_t bmval0 = 0;
	uint32_t bmval1 = 0;

	/*
	 * We reserve enough space to write the entire attribute buffer at once.
	 * In the worst-case, this would be
	 *   12(bitmap) + 4(attrlen) + 8(size) + 4(mode) + 4(atime) + 4(mtime)
	 *          = 36 bytes, plus any contribution from variable-length fields
	 *            such as owner/group.
	 */
	len = 16;

	/* Sigh */
	if (iap->ia_valid & ATTR_SIZE)
		len += 8;
	if (iap->ia_valid & ATTR_MODE)
		len += 4;
	if (iap->ia_valid & ATTR_UID) {
		owner_namelen = nfs_map_uid_to_name(server->nfs_client, iap->ia_uid, owner_name);
		if (owner_namelen < 0) {
			dprintk("nfs: couldn't resolve uid %d to string\n",
					iap->ia_uid);
			/* XXX */
			strcpy(owner_name, "nobody");
			owner_namelen = sizeof("nobody") - 1;
			/* goto out; */
		}
		len += 4 + (XDR_QUADLEN(owner_namelen) << 2);
	}
	if (iap->ia_valid & ATTR_GID) {
		owner_grouplen = nfs_map_gid_to_group(server->nfs_client, iap->ia_gid, owner_group);
		if (owner_grouplen < 0) {
			dprintk("nfs: couldn't resolve gid %d to string\n",
					iap->ia_gid);
			strcpy(owner_group, "nobody");
			owner_grouplen = sizeof("nobody") - 1;
			/* goto out; */
		}
		len += 4 + (XDR_QUADLEN(owner_grouplen) << 2);
	}
	if (iap->ia_valid & ATTR_ATIME_SET)
		len += 16;
	else if (iap->ia_valid & ATTR_ATIME)
		len += 4;
	if (iap->ia_valid & ATTR_MTIME_SET)
		len += 16;
	else if (iap->ia_valid & ATTR_MTIME)
		len += 4;
	p = reserve_space(xdr, len);

	/*
	 * We write the bitmap length now, but leave the bitmap and the attribute
	 * buffer length to be backfilled at the end of this routine.
	 */
	*p++ = cpu_to_be32(2);
	q = p;
	p += 3;

	if (iap->ia_valid & ATTR_SIZE) {
		bmval0 |= FATTR4_WORD0_SIZE;
		p = xdr_encode_hyper(p, iap->ia_size);
	}
	if (iap->ia_valid & ATTR_MODE) {
		bmval1 |= FATTR4_WORD1_MODE;
		*p++ = cpu_to_be32(iap->ia_mode & S_IALLUGO);
	}
	if (iap->ia_valid & ATTR_UID) {
		bmval1 |= FATTR4_WORD1_OWNER;
		p = xdr_encode_opaque(p, owner_name, owner_namelen);
	}
	if (iap->ia_valid & ATTR_GID) {
		bmval1 |= FATTR4_WORD1_OWNER_GROUP;
		p = xdr_encode_opaque(p, owner_group, owner_grouplen);
	}
	if (iap->ia_valid & ATTR_ATIME_SET) {
		bmval1 |= FATTR4_WORD1_TIME_ACCESS_SET;
		*p++ = cpu_to_be32(NFS4_SET_TO_CLIENT_TIME);
		*p++ = cpu_to_be32(0);
		*p++ = cpu_to_be32(iap->ia_atime.tv_sec);
		*p++ = cpu_to_be32(iap->ia_atime.tv_nsec);
	}
	else if (iap->ia_valid & ATTR_ATIME) {
		bmval1 |= FATTR4_WORD1_TIME_ACCESS_SET;
		*p++ = cpu_to_be32(NFS4_SET_TO_SERVER_TIME);
	}
	if (iap->ia_valid & ATTR_MTIME_SET) {
		bmval1 |= FATTR4_WORD1_TIME_MODIFY_SET;
		*p++ = cpu_to_be32(NFS4_SET_TO_CLIENT_TIME);
		*p++ = cpu_to_be32(0);
		*p++ = cpu_to_be32(iap->ia_mtime.tv_sec);
		*p++ = cpu_to_be32(iap->ia_mtime.tv_nsec);
	}
	else if (iap->ia_valid & ATTR_MTIME) {
		bmval1 |= FATTR4_WORD1_TIME_MODIFY_SET;
		*p++ = cpu_to_be32(NFS4_SET_TO_SERVER_TIME);
	}

	/*
	 * Now we backfill the bitmap and the attribute buffer length.
	 */
	if (len != ((char *)p - (char *)q) + 4) {
		printk(KERN_ERR "nfs: Attr length error, %u != %Zu\n",
				len, ((char *)p - (char *)q) + 4);
		BUG();
	}
	len = (char *)p - (char *)q - 12;
	*q++ = htonl(bmval0);
	*q++ = htonl(bmval1);
	*q = htonl(len);

/* out: */
}

static void encode_access(struct xdr_stream *xdr, u32 access, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 8);
	*p++ = cpu_to_be32(OP_ACCESS);
	*p = cpu_to_be32(access);
	hdr->nops++;
	hdr->replen += decode_access_maxsz;
}

static void encode_close(struct xdr_stream *xdr, const struct nfs_closeargs *arg, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 8+NFS4_STATEID_SIZE);
	*p++ = cpu_to_be32(OP_CLOSE);
	*p++ = cpu_to_be32(arg->seqid->sequence->counter);
	xdr_encode_opaque_fixed(p, arg->stateid->data, NFS4_STATEID_SIZE);
	hdr->nops++;
	hdr->replen += decode_close_maxsz;
}

static void encode_commit(struct xdr_stream *xdr, const struct nfs_writeargs *args, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 16);
	*p++ = cpu_to_be32(OP_COMMIT);
	p = xdr_encode_hyper(p, args->offset);
	*p = cpu_to_be32(args->count);
	hdr->nops++;
	hdr->replen += decode_commit_maxsz;
}

static void encode_create(struct xdr_stream *xdr, const struct nfs4_create_arg *create, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 8);
	*p++ = cpu_to_be32(OP_CREATE);
	*p = cpu_to_be32(create->ftype);

	switch (create->ftype) {
	case NF4LNK:
		p = reserve_space(xdr, 4);
		*p = cpu_to_be32(create->u.symlink.len);
		xdr_write_pages(xdr, create->u.symlink.pages, 0, create->u.symlink.len);
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
	hdr->nops++;
	hdr->replen += decode_create_maxsz;

	encode_attrs(xdr, create->attrs, create->server);
}

static void encode_getattr_one(struct xdr_stream *xdr, uint32_t bitmap, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 12);
	*p++ = cpu_to_be32(OP_GETATTR);
	*p++ = cpu_to_be32(1);
	*p = cpu_to_be32(bitmap);
	hdr->nops++;
	hdr->replen += decode_getattr_maxsz;
}

static void encode_getattr_two(struct xdr_stream *xdr, uint32_t bm0, uint32_t bm1, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 16);
	*p++ = cpu_to_be32(OP_GETATTR);
	*p++ = cpu_to_be32(2);
	*p++ = cpu_to_be32(bm0);
	*p = cpu_to_be32(bm1);
	hdr->nops++;
	hdr->replen += decode_getattr_maxsz;
}

static void encode_getfattr(struct xdr_stream *xdr, const u32* bitmask, struct compound_hdr *hdr)
{
	encode_getattr_two(xdr, bitmask[0] & nfs4_fattr_bitmap[0],
			   bitmask[1] & nfs4_fattr_bitmap[1], hdr);
}

static void encode_fsinfo(struct xdr_stream *xdr, const u32* bitmask, struct compound_hdr *hdr)
{
	encode_getattr_two(xdr, bitmask[0] & nfs4_fsinfo_bitmap[0],
			   bitmask[1] & nfs4_fsinfo_bitmap[1], hdr);
}

static void encode_fs_locations(struct xdr_stream *xdr, const u32* bitmask, struct compound_hdr *hdr)
{
	encode_getattr_two(xdr, bitmask[0] & nfs4_fs_locations_bitmap[0],
			   bitmask[1] & nfs4_fs_locations_bitmap[1], hdr);
}

static void encode_getfh(struct xdr_stream *xdr, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(OP_GETFH);
	hdr->nops++;
	hdr->replen += decode_getfh_maxsz;
}

static void encode_link(struct xdr_stream *xdr, const struct qstr *name, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 8 + name->len);
	*p++ = cpu_to_be32(OP_LINK);
	xdr_encode_opaque(p, name->name, name->len);
	hdr->nops++;
	hdr->replen += decode_link_maxsz;
}

static inline int nfs4_lock_type(struct file_lock *fl, int block)
{
	if ((fl->fl_type & (F_RDLCK|F_WRLCK|F_UNLCK)) == F_RDLCK)
		return block ? NFS4_READW_LT : NFS4_READ_LT;
	return block ? NFS4_WRITEW_LT : NFS4_WRITE_LT;
}

static inline uint64_t nfs4_lock_length(struct file_lock *fl)
{
	if (fl->fl_end == OFFSET_MAX)
		return ~(uint64_t)0;
	return fl->fl_end - fl->fl_start + 1;
}

/*
 * opcode,type,reclaim,offset,length,new_lock_owner = 32
 * open_seqid,open_stateid,lock_seqid,lock_owner.clientid, lock_owner.id = 40
 */
static void encode_lock(struct xdr_stream *xdr, const struct nfs_lock_args *args, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 32);
	*p++ = cpu_to_be32(OP_LOCK);
	*p++ = cpu_to_be32(nfs4_lock_type(args->fl, args->block));
	*p++ = cpu_to_be32(args->reclaim);
	p = xdr_encode_hyper(p, args->fl->fl_start);
	p = xdr_encode_hyper(p, nfs4_lock_length(args->fl));
	*p = cpu_to_be32(args->new_lock_owner);
	if (args->new_lock_owner){
		p = reserve_space(xdr, 4+NFS4_STATEID_SIZE+32);
		*p++ = cpu_to_be32(args->open_seqid->sequence->counter);
		p = xdr_encode_opaque_fixed(p, args->open_stateid->data, NFS4_STATEID_SIZE);
		*p++ = cpu_to_be32(args->lock_seqid->sequence->counter);
		p = xdr_encode_hyper(p, args->lock_owner.clientid);
		*p++ = cpu_to_be32(16);
		p = xdr_encode_opaque_fixed(p, "lock id:", 8);
		xdr_encode_hyper(p, args->lock_owner.id);
	}
	else {
		p = reserve_space(xdr, NFS4_STATEID_SIZE+4);
		p = xdr_encode_opaque_fixed(p, args->lock_stateid->data, NFS4_STATEID_SIZE);
		*p = cpu_to_be32(args->lock_seqid->sequence->counter);
	}
	hdr->nops++;
	hdr->replen += decode_lock_maxsz;
}

static void encode_lockt(struct xdr_stream *xdr, const struct nfs_lockt_args *args, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 52);
	*p++ = cpu_to_be32(OP_LOCKT);
	*p++ = cpu_to_be32(nfs4_lock_type(args->fl, 0));
	p = xdr_encode_hyper(p, args->fl->fl_start);
	p = xdr_encode_hyper(p, nfs4_lock_length(args->fl));
	p = xdr_encode_hyper(p, args->lock_owner.clientid);
	*p++ = cpu_to_be32(16);
	p = xdr_encode_opaque_fixed(p, "lock id:", 8);
	xdr_encode_hyper(p, args->lock_owner.id);
	hdr->nops++;
	hdr->replen += decode_lockt_maxsz;
}

static void encode_locku(struct xdr_stream *xdr, const struct nfs_locku_args *args, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 12+NFS4_STATEID_SIZE+16);
	*p++ = cpu_to_be32(OP_LOCKU);
	*p++ = cpu_to_be32(nfs4_lock_type(args->fl, 0));
	*p++ = cpu_to_be32(args->seqid->sequence->counter);
	p = xdr_encode_opaque_fixed(p, args->stateid->data, NFS4_STATEID_SIZE);
	p = xdr_encode_hyper(p, args->fl->fl_start);
	xdr_encode_hyper(p, nfs4_lock_length(args->fl));
	hdr->nops++;
	hdr->replen += decode_locku_maxsz;
}

static void encode_lookup(struct xdr_stream *xdr, const struct qstr *name, struct compound_hdr *hdr)
{
	int len = name->len;
	__be32 *p;

	p = reserve_space(xdr, 8 + len);
	*p++ = cpu_to_be32(OP_LOOKUP);
	xdr_encode_opaque(p, name->name, len);
	hdr->nops++;
	hdr->replen += decode_lookup_maxsz;
}

static void encode_share_access(struct xdr_stream *xdr, fmode_t fmode)
{
	__be32 *p;

	p = reserve_space(xdr, 8);
	switch (fmode & (FMODE_READ|FMODE_WRITE)) {
	case FMODE_READ:
		*p++ = cpu_to_be32(NFS4_SHARE_ACCESS_READ);
		break;
	case FMODE_WRITE:
		*p++ = cpu_to_be32(NFS4_SHARE_ACCESS_WRITE);
		break;
	case FMODE_READ|FMODE_WRITE:
		*p++ = cpu_to_be32(NFS4_SHARE_ACCESS_BOTH);
		break;
	default:
		*p++ = cpu_to_be32(0);
	}
	*p = cpu_to_be32(0);		/* for linux, share_deny = 0 always */
}

static inline void encode_openhdr(struct xdr_stream *xdr, const struct nfs_openargs *arg)
{
	__be32 *p;
 /*
 * opcode 4, seqid 4, share_access 4, share_deny 4, clientid 8, ownerlen 4,
 * owner 4 = 32
 */
	p = reserve_space(xdr, 8);
	*p++ = cpu_to_be32(OP_OPEN);
	*p = cpu_to_be32(arg->seqid->sequence->counter);
	encode_share_access(xdr, arg->fmode);
	p = reserve_space(xdr, 28);
	p = xdr_encode_hyper(p, arg->clientid);
	*p++ = cpu_to_be32(16);
	p = xdr_encode_opaque_fixed(p, "open id:", 8);
	xdr_encode_hyper(p, arg->id);
}

static inline void encode_createmode(struct xdr_stream *xdr, const struct nfs_openargs *arg)
{
	__be32 *p;
	struct nfs_client *clp;

	p = reserve_space(xdr, 4);
	switch(arg->open_flags & O_EXCL) {
	case 0:
		*p = cpu_to_be32(NFS4_CREATE_UNCHECKED);
		encode_attrs(xdr, arg->u.attrs, arg->server);
		break;
	default:
		clp = arg->server->nfs_client;
		if (clp->cl_minorversion > 0) {
			if (nfs4_has_persistent_session(clp)) {
				*p = cpu_to_be32(NFS4_CREATE_GUARDED);
				encode_attrs(xdr, arg->u.attrs, arg->server);
			} else {
				struct iattr dummy;

				*p = cpu_to_be32(NFS4_CREATE_EXCLUSIVE4_1);
				encode_nfs4_verifier(xdr, &arg->u.verifier);
				dummy.ia_valid = 0;
				encode_attrs(xdr, &dummy, arg->server);
			}
		} else {
			*p = cpu_to_be32(NFS4_CREATE_EXCLUSIVE);
			encode_nfs4_verifier(xdr, &arg->u.verifier);
		}
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
		BUG_ON(arg->claim != NFS4_OPEN_CLAIM_NULL);
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

	p = reserve_space(xdr, 4+NFS4_STATEID_SIZE);
	*p++ = cpu_to_be32(NFS4_OPEN_CLAIM_DELEGATE_CUR);
	xdr_encode_opaque_fixed(p, stateid->data, NFS4_STATEID_SIZE);
	encode_string(xdr, name->len, name->name);
}

static void encode_open(struct xdr_stream *xdr, const struct nfs_openargs *arg, struct compound_hdr *hdr)
{
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
	default:
		BUG();
	}
	hdr->nops++;
	hdr->replen += decode_open_maxsz;
}

static void encode_open_confirm(struct xdr_stream *xdr, const struct nfs_open_confirmargs *arg, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 4+NFS4_STATEID_SIZE+4);
	*p++ = cpu_to_be32(OP_OPEN_CONFIRM);
	p = xdr_encode_opaque_fixed(p, arg->stateid->data, NFS4_STATEID_SIZE);
	*p = cpu_to_be32(arg->seqid->sequence->counter);
	hdr->nops++;
	hdr->replen += decode_open_confirm_maxsz;
}

static void encode_open_downgrade(struct xdr_stream *xdr, const struct nfs_closeargs *arg, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 4+NFS4_STATEID_SIZE+4);
	*p++ = cpu_to_be32(OP_OPEN_DOWNGRADE);
	p = xdr_encode_opaque_fixed(p, arg->stateid->data, NFS4_STATEID_SIZE);
	*p = cpu_to_be32(arg->seqid->sequence->counter);
	encode_share_access(xdr, arg->fmode);
	hdr->nops++;
	hdr->replen += decode_open_downgrade_maxsz;
}

static void
encode_putfh(struct xdr_stream *xdr, const struct nfs_fh *fh, struct compound_hdr *hdr)
{
	int len = fh->size;
	__be32 *p;

	p = reserve_space(xdr, 8 + len);
	*p++ = cpu_to_be32(OP_PUTFH);
	xdr_encode_opaque(p, fh->data, len);
	hdr->nops++;
	hdr->replen += decode_putfh_maxsz;
}

static void encode_putrootfh(struct xdr_stream *xdr, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(OP_PUTROOTFH);
	hdr->nops++;
	hdr->replen += decode_putrootfh_maxsz;
}

static void encode_stateid(struct xdr_stream *xdr, const struct nfs_open_context *ctx)
{
	nfs4_stateid stateid;
	__be32 *p;

	p = reserve_space(xdr, NFS4_STATEID_SIZE);
	if (ctx->state != NULL) {
		nfs4_copy_stateid(&stateid, ctx->state, ctx->lockowner);
		xdr_encode_opaque_fixed(p, stateid.data, NFS4_STATEID_SIZE);
	} else
		xdr_encode_opaque_fixed(p, zero_stateid.data, NFS4_STATEID_SIZE);
}

static void encode_read(struct xdr_stream *xdr, const struct nfs_readargs *args, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(OP_READ);

	encode_stateid(xdr, args->context);

	p = reserve_space(xdr, 12);
	p = xdr_encode_hyper(p, args->offset);
	*p = cpu_to_be32(args->count);
	hdr->nops++;
	hdr->replen += decode_read_maxsz;
}

static void encode_readdir(struct xdr_stream *xdr, const struct nfs4_readdir_arg *readdir, struct rpc_rqst *req, struct compound_hdr *hdr)
{
	uint32_t attrs[2] = {
		FATTR4_WORD0_RDATTR_ERROR|FATTR4_WORD0_FILEID,
		FATTR4_WORD1_MOUNTED_ON_FILEID,
	};
	__be32 *p;

	p = reserve_space(xdr, 12+NFS4_VERIFIER_SIZE+20);
	*p++ = cpu_to_be32(OP_READDIR);
	p = xdr_encode_hyper(p, readdir->cookie);
	p = xdr_encode_opaque_fixed(p, readdir->verifier.data, NFS4_VERIFIER_SIZE);
	*p++ = cpu_to_be32(readdir->count >> 1);  /* We're not doing readdirplus */
	*p++ = cpu_to_be32(readdir->count);
	*p++ = cpu_to_be32(2);
	/* Switch to mounted_on_fileid if the server supports it */
	if (readdir->bitmask[1] & FATTR4_WORD1_MOUNTED_ON_FILEID)
		attrs[0] &= ~FATTR4_WORD0_FILEID;
	else
		attrs[1] &= ~FATTR4_WORD1_MOUNTED_ON_FILEID;
	*p++ = cpu_to_be32(attrs[0] & readdir->bitmask[0]);
	*p = cpu_to_be32(attrs[1] & readdir->bitmask[1]);
	hdr->nops++;
	hdr->replen += decode_readdir_maxsz;
	dprintk("%s: cookie = %Lu, verifier = %08x:%08x, bitmap = %08x:%08x\n",
			__func__,
			(unsigned long long)readdir->cookie,
			((u32 *)readdir->verifier.data)[0],
			((u32 *)readdir->verifier.data)[1],
			attrs[0] & readdir->bitmask[0],
			attrs[1] & readdir->bitmask[1]);
}

static void encode_readlink(struct xdr_stream *xdr, const struct nfs4_readlink *readlink, struct rpc_rqst *req, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(OP_READLINK);
	hdr->nops++;
	hdr->replen += decode_readlink_maxsz;
}

static void encode_remove(struct xdr_stream *xdr, const struct qstr *name, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 8 + name->len);
	*p++ = cpu_to_be32(OP_REMOVE);
	xdr_encode_opaque(p, name->name, name->len);
	hdr->nops++;
	hdr->replen += decode_remove_maxsz;
}

static void encode_rename(struct xdr_stream *xdr, const struct qstr *oldname, const struct qstr *newname, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(OP_RENAME);
	encode_string(xdr, oldname->len, oldname->name);
	encode_string(xdr, newname->len, newname->name);
	hdr->nops++;
	hdr->replen += decode_rename_maxsz;
}

static void encode_renew(struct xdr_stream *xdr, const struct nfs_client *client_stateid, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 12);
	*p++ = cpu_to_be32(OP_RENEW);
	xdr_encode_hyper(p, client_stateid->cl_clientid);
	hdr->nops++;
	hdr->replen += decode_renew_maxsz;
}

static void
encode_restorefh(struct xdr_stream *xdr, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(OP_RESTOREFH);
	hdr->nops++;
	hdr->replen += decode_restorefh_maxsz;
}

static int
encode_setacl(struct xdr_stream *xdr, struct nfs_setaclargs *arg, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 4+NFS4_STATEID_SIZE);
	*p++ = cpu_to_be32(OP_SETATTR);
	xdr_encode_opaque_fixed(p, zero_stateid.data, NFS4_STATEID_SIZE);
	p = reserve_space(xdr, 2*4);
	*p++ = cpu_to_be32(1);
	*p = cpu_to_be32(FATTR4_WORD0_ACL);
	if (arg->acl_len % 4)
		return -EINVAL;
	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(arg->acl_len);
	xdr_write_pages(xdr, arg->acl_pages, arg->acl_pgbase, arg->acl_len);
	hdr->nops++;
	hdr->replen += decode_setacl_maxsz;
	return 0;
}

static void
encode_savefh(struct xdr_stream *xdr, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(OP_SAVEFH);
	hdr->nops++;
	hdr->replen += decode_savefh_maxsz;
}

static void encode_setattr(struct xdr_stream *xdr, const struct nfs_setattrargs *arg, const struct nfs_server *server, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 4+NFS4_STATEID_SIZE);
	*p++ = cpu_to_be32(OP_SETATTR);
	xdr_encode_opaque_fixed(p, arg->stateid.data, NFS4_STATEID_SIZE);
	hdr->nops++;
	hdr->replen += decode_setattr_maxsz;
	encode_attrs(xdr, arg->iap, server);
}

static void encode_setclientid(struct xdr_stream *xdr, const struct nfs4_setclientid *setclientid, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 4 + NFS4_VERIFIER_SIZE);
	*p++ = cpu_to_be32(OP_SETCLIENTID);
	xdr_encode_opaque_fixed(p, setclientid->sc_verifier->data, NFS4_VERIFIER_SIZE);

	encode_string(xdr, setclientid->sc_name_len, setclientid->sc_name);
	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(setclientid->sc_prog);
	encode_string(xdr, setclientid->sc_netid_len, setclientid->sc_netid);
	encode_string(xdr, setclientid->sc_uaddr_len, setclientid->sc_uaddr);
	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(setclientid->sc_cb_ident);
	hdr->nops++;
	hdr->replen += decode_setclientid_maxsz;
}

static void encode_setclientid_confirm(struct xdr_stream *xdr, const struct nfs4_setclientid_res *arg, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 12 + NFS4_VERIFIER_SIZE);
	*p++ = cpu_to_be32(OP_SETCLIENTID_CONFIRM);
	p = xdr_encode_hyper(p, arg->clientid);
	xdr_encode_opaque_fixed(p, arg->confirm.data, NFS4_VERIFIER_SIZE);
	hdr->nops++;
	hdr->replen += decode_setclientid_confirm_maxsz;
}

static void encode_write(struct xdr_stream *xdr, const struct nfs_writeargs *args, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(OP_WRITE);

	encode_stateid(xdr, args->context);

	p = reserve_space(xdr, 16);
	p = xdr_encode_hyper(p, args->offset);
	*p++ = cpu_to_be32(args->stable);
	*p = cpu_to_be32(args->count);

	xdr_write_pages(xdr, args->pages, args->pgbase, args->count);
	hdr->nops++;
	hdr->replen += decode_write_maxsz;
}

static void encode_delegreturn(struct xdr_stream *xdr, const nfs4_stateid *stateid, struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 4+NFS4_STATEID_SIZE);

	*p++ = cpu_to_be32(OP_DELEGRETURN);
	xdr_encode_opaque_fixed(p, stateid->data, NFS4_STATEID_SIZE);
	hdr->nops++;
	hdr->replen += decode_delegreturn_maxsz;
}

#if defined(CONFIG_NFS_V4_1)
/* NFSv4.1 operations */
static void encode_exchange_id(struct xdr_stream *xdr,
			       struct nfs41_exchange_id_args *args,
			       struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 4 + sizeof(args->verifier->data));
	*p++ = cpu_to_be32(OP_EXCHANGE_ID);
	xdr_encode_opaque_fixed(p, args->verifier->data, sizeof(args->verifier->data));

	encode_string(xdr, args->id_len, args->id);

	p = reserve_space(xdr, 12);
	*p++ = cpu_to_be32(args->flags);
	*p++ = cpu_to_be32(0);	/* zero length state_protect4_a */
	*p = cpu_to_be32(0);	/* zero length implementation id array */
	hdr->nops++;
	hdr->replen += decode_exchange_id_maxsz;
}

static void encode_create_session(struct xdr_stream *xdr,
				  struct nfs41_create_session_args *args,
				  struct compound_hdr *hdr)
{
	__be32 *p;
	char machine_name[NFS4_MAX_MACHINE_NAME_LEN];
	uint32_t len;
	struct nfs_client *clp = args->client;
	u32 max_resp_sz_cached;

	/*
	 * Assumes OPEN is the biggest non-idempotent compound.
	 * 2 is the verifier.
	 */
	max_resp_sz_cached = (NFS4_dec_open_sz + RPC_REPHDRSIZE +
			      RPC_MAX_AUTH_SIZE + 2) * XDR_UNIT;

	len = scnprintf(machine_name, sizeof(machine_name), "%s",
			clp->cl_ipaddr);

	p = reserve_space(xdr, 20 + 2*28 + 20 + len + 12);
	*p++ = cpu_to_be32(OP_CREATE_SESSION);
	p = xdr_encode_hyper(p, clp->cl_ex_clid);
	*p++ = cpu_to_be32(clp->cl_seqid);			/*Sequence id */
	*p++ = cpu_to_be32(args->flags);			/*flags */

	/* Fore Channel */
	*p++ = cpu_to_be32(args->fc_attrs.headerpadsz);	/* header padding size */
	*p++ = cpu_to_be32(args->fc_attrs.max_rqst_sz);	/* max req size */
	*p++ = cpu_to_be32(args->fc_attrs.max_resp_sz);	/* max resp size */
	*p++ = cpu_to_be32(max_resp_sz_cached);		/* Max resp sz cached */
	*p++ = cpu_to_be32(args->fc_attrs.max_ops);	/* max operations */
	*p++ = cpu_to_be32(args->fc_attrs.max_reqs);	/* max requests */
	*p++ = cpu_to_be32(0);				/* rdmachannel_attrs */

	/* Back Channel */
	*p++ = cpu_to_be32(args->fc_attrs.headerpadsz);	/* header padding size */
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
	*p++ = cpu_to_be32((u32)clp->cl_boot_time.tv_nsec);	/* stamp */
	p = xdr_encode_opaque(p, machine_name, len);
	*p++ = cpu_to_be32(0);				/* UID */
	*p++ = cpu_to_be32(0);				/* GID */
	*p = cpu_to_be32(0);				/* No more gids */
	hdr->nops++;
	hdr->replen += decode_create_session_maxsz;
}

static void encode_destroy_session(struct xdr_stream *xdr,
				   struct nfs4_session *session,
				   struct compound_hdr *hdr)
{
	__be32 *p;
	p = reserve_space(xdr, 4 + NFS4_MAX_SESSIONID_LEN);
	*p++ = cpu_to_be32(OP_DESTROY_SESSION);
	xdr_encode_opaque_fixed(p, session->sess_id.data, NFS4_MAX_SESSIONID_LEN);
	hdr->nops++;
	hdr->replen += decode_destroy_session_maxsz;
}

static void encode_reclaim_complete(struct xdr_stream *xdr,
				    struct nfs41_reclaim_complete_args *args,
				    struct compound_hdr *hdr)
{
	__be32 *p;

	p = reserve_space(xdr, 8);
	*p++ = cpu_to_be32(OP_RECLAIM_COMPLETE);
	*p++ = cpu_to_be32(args->one_fs);
	hdr->nops++;
	hdr->replen += decode_reclaim_complete_maxsz;
}
#endif /* CONFIG_NFS_V4_1 */

static void encode_sequence(struct xdr_stream *xdr,
			    const struct nfs4_sequence_args *args,
			    struct compound_hdr *hdr)
{
#if defined(CONFIG_NFS_V4_1)
	struct nfs4_session *session = args->sa_session;
	struct nfs4_slot_table *tp;
	struct nfs4_slot *slot;
	__be32 *p;

	if (!session)
		return;

	tp = &session->fc_slot_table;

	WARN_ON(args->sa_slotid == NFS4_MAX_SLOT_TABLE);
	slot = tp->slots + args->sa_slotid;

	p = reserve_space(xdr, 4 + NFS4_MAX_SESSIONID_LEN + 16);
	*p++ = cpu_to_be32(OP_SEQUENCE);

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
		slot->seq_nr, args->sa_slotid,
		tp->highest_used_slotid, args->sa_cache_this);
	p = xdr_encode_opaque_fixed(p, session->sess_id.data, NFS4_MAX_SESSIONID_LEN);
	*p++ = cpu_to_be32(slot->seq_nr);
	*p++ = cpu_to_be32(args->sa_slotid);
	*p++ = cpu_to_be32(tp->highest_used_slotid);
	*p = cpu_to_be32(args->sa_cache_this);
	hdr->nops++;
	hdr->replen += decode_sequence_maxsz;
#endif /* CONFIG_NFS_V4_1 */
}

/*
 * END OF "GENERIC" ENCODE ROUTINES.
 */

static u32 nfs4_xdr_minorversion(const struct nfs4_sequence_args *args)
{
#if defined(CONFIG_NFS_V4_1)
	if (args->sa_session)
		return args->sa_session->clp->cl_minorversion;
#endif /* CONFIG_NFS_V4_1 */
	return 0;
}

/*
 * Encode an ACCESS request
 */
static int nfs4_xdr_enc_access(struct rpc_rqst *req, __be32 *p, const struct nfs4_accessargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->fh, &hdr);
	encode_access(&xdr, args->access, &hdr);
	encode_getfattr(&xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * Encode LOOKUP request
 */
static int nfs4_xdr_enc_lookup(struct rpc_rqst *req, __be32 *p, const struct nfs4_lookup_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->dir_fh, &hdr);
	encode_lookup(&xdr, args->name, &hdr);
	encode_getfh(&xdr, &hdr);
	encode_getfattr(&xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * Encode LOOKUP_ROOT request
 */
static int nfs4_xdr_enc_lookup_root(struct rpc_rqst *req, __be32 *p, const struct nfs4_lookup_root_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putrootfh(&xdr, &hdr);
	encode_getfh(&xdr, &hdr);
	encode_getfattr(&xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * Encode REMOVE request
 */
static int nfs4_xdr_enc_remove(struct rpc_rqst *req, __be32 *p, const struct nfs_removeargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->fh, &hdr);
	encode_remove(&xdr, &args->name, &hdr);
	encode_getfattr(&xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * Encode RENAME request
 */
static int nfs4_xdr_enc_rename(struct rpc_rqst *req, __be32 *p, const struct nfs4_rename_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->old_dir, &hdr);
	encode_savefh(&xdr, &hdr);
	encode_putfh(&xdr, args->new_dir, &hdr);
	encode_rename(&xdr, args->old_name, args->new_name, &hdr);
	encode_getfattr(&xdr, args->bitmask, &hdr);
	encode_restorefh(&xdr, &hdr);
	encode_getfattr(&xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * Encode LINK request
 */
static int nfs4_xdr_enc_link(struct rpc_rqst *req, __be32 *p, const struct nfs4_link_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->fh, &hdr);
	encode_savefh(&xdr, &hdr);
	encode_putfh(&xdr, args->dir_fh, &hdr);
	encode_link(&xdr, args->name, &hdr);
	encode_getfattr(&xdr, args->bitmask, &hdr);
	encode_restorefh(&xdr, &hdr);
	encode_getfattr(&xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * Encode CREATE request
 */
static int nfs4_xdr_enc_create(struct rpc_rqst *req, __be32 *p, const struct nfs4_create_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->dir_fh, &hdr);
	encode_savefh(&xdr, &hdr);
	encode_create(&xdr, args, &hdr);
	encode_getfh(&xdr, &hdr);
	encode_getfattr(&xdr, args->bitmask, &hdr);
	encode_restorefh(&xdr, &hdr);
	encode_getfattr(&xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * Encode SYMLINK request
 */
static int nfs4_xdr_enc_symlink(struct rpc_rqst *req, __be32 *p, const struct nfs4_create_arg *args)
{
	return nfs4_xdr_enc_create(req, p, args);
}

/*
 * Encode GETATTR request
 */
static int nfs4_xdr_enc_getattr(struct rpc_rqst *req, __be32 *p, const struct nfs4_getattr_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->fh, &hdr);
	encode_getfattr(&xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * Encode a CLOSE request
 */
static int nfs4_xdr_enc_close(struct rpc_rqst *req, __be32 *p, struct nfs_closeargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->fh, &hdr);
	encode_close(&xdr, args, &hdr);
	encode_getfattr(&xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * Encode an OPEN request
 */
static int nfs4_xdr_enc_open(struct rpc_rqst *req, __be32 *p, struct nfs_openargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->fh, &hdr);
	encode_savefh(&xdr, &hdr);
	encode_open(&xdr, args, &hdr);
	encode_getfh(&xdr, &hdr);
	encode_getfattr(&xdr, args->bitmask, &hdr);
	encode_restorefh(&xdr, &hdr);
	encode_getfattr(&xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * Encode an OPEN_CONFIRM request
 */
static int nfs4_xdr_enc_open_confirm(struct rpc_rqst *req, __be32 *p, struct nfs_open_confirmargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 0,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_putfh(&xdr, args->fh, &hdr);
	encode_open_confirm(&xdr, args, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * Encode an OPEN request with no attributes.
 */
static int nfs4_xdr_enc_open_noattr(struct rpc_rqst *req, __be32 *p, struct nfs_openargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->fh, &hdr);
	encode_open(&xdr, args, &hdr);
	encode_getfattr(&xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * Encode an OPEN_DOWNGRADE request
 */
static int nfs4_xdr_enc_open_downgrade(struct rpc_rqst *req, __be32 *p, struct nfs_closeargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->fh, &hdr);
	encode_open_downgrade(&xdr, args, &hdr);
	encode_getfattr(&xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * Encode a LOCK request
 */
static int nfs4_xdr_enc_lock(struct rpc_rqst *req, __be32 *p, struct nfs_lock_args *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->fh, &hdr);
	encode_lock(&xdr, args, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * Encode a LOCKT request
 */
static int nfs4_xdr_enc_lockt(struct rpc_rqst *req, __be32 *p, struct nfs_lockt_args *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->fh, &hdr);
	encode_lockt(&xdr, args, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * Encode a LOCKU request
 */
static int nfs4_xdr_enc_locku(struct rpc_rqst *req, __be32 *p, struct nfs_locku_args *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->fh, &hdr);
	encode_locku(&xdr, args, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * Encode a READLINK request
 */
static int nfs4_xdr_enc_readlink(struct rpc_rqst *req, __be32 *p, const struct nfs4_readlink *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->fh, &hdr);
	encode_readlink(&xdr, args, req, &hdr);

	xdr_inline_pages(&req->rq_rcv_buf, hdr.replen << 2, args->pages,
			args->pgbase, args->pglen);
	encode_nops(&hdr);
	return 0;
}

/*
 * Encode a READDIR request
 */
static int nfs4_xdr_enc_readdir(struct rpc_rqst *req, __be32 *p, const struct nfs4_readdir_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->fh, &hdr);
	encode_readdir(&xdr, args, req, &hdr);

	xdr_inline_pages(&req->rq_rcv_buf, hdr.replen << 2, args->pages,
			 args->pgbase, args->count);
	dprintk("%s: inlined page args = (%u, %p, %u, %u)\n",
			__func__, hdr.replen << 2, args->pages,
			args->pgbase, args->count);
	encode_nops(&hdr);
	return 0;
}

/*
 * Encode a READ request
 */
static int nfs4_xdr_enc_read(struct rpc_rqst *req, __be32 *p, struct nfs_readargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->fh, &hdr);
	encode_read(&xdr, args, &hdr);

	xdr_inline_pages(&req->rq_rcv_buf, hdr.replen << 2,
			 args->pages, args->pgbase, args->count);
	req->rq_rcv_buf.flags |= XDRBUF_READ;
	encode_nops(&hdr);
	return 0;
}

/*
 * Encode an SETATTR request
 */
static int nfs4_xdr_enc_setattr(struct rpc_rqst *req, __be32 *p, struct nfs_setattrargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->fh, &hdr);
	encode_setattr(&xdr, args, args->server, &hdr);
	encode_getfattr(&xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * Encode a GETACL request
 */
static int
nfs4_xdr_enc_getacl(struct rpc_rqst *req, __be32 *p,
		struct nfs_getaclargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};
	uint32_t replen;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->fh, &hdr);
	replen = hdr.replen + op_decode_hdr_maxsz + nfs4_fattr_bitmap_maxsz + 1;
	encode_getattr_two(&xdr, FATTR4_WORD0_ACL, 0, &hdr);

	xdr_inline_pages(&req->rq_rcv_buf, replen << 2,
		args->acl_pages, args->acl_pgbase, args->acl_len);
	encode_nops(&hdr);
	return 0;
}

/*
 * Encode a WRITE request
 */
static int nfs4_xdr_enc_write(struct rpc_rqst *req, __be32 *p, struct nfs_writeargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->fh, &hdr);
	encode_write(&xdr, args, &hdr);
	req->rq_snd_buf.flags |= XDRBUF_WRITE;
	encode_getfattr(&xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 *  a COMMIT request
 */
static int nfs4_xdr_enc_commit(struct rpc_rqst *req, __be32 *p, struct nfs_writeargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->fh, &hdr);
	encode_commit(&xdr, args, &hdr);
	encode_getfattr(&xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * FSINFO request
 */
static int nfs4_xdr_enc_fsinfo(struct rpc_rqst *req, __be32 *p, struct nfs4_fsinfo_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->fh, &hdr);
	encode_fsinfo(&xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * a PATHCONF request
 */
static int nfs4_xdr_enc_pathconf(struct rpc_rqst *req, __be32 *p, const struct nfs4_pathconf_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->fh, &hdr);
	encode_getattr_one(&xdr, args->bitmask[0] & nfs4_pathconf_bitmap[0],
			   &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * a STATFS request
 */
static int nfs4_xdr_enc_statfs(struct rpc_rqst *req, __be32 *p, const struct nfs4_statfs_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->fh, &hdr);
	encode_getattr_two(&xdr, args->bitmask[0] & nfs4_statfs_bitmap[0],
			   args->bitmask[1] & nfs4_statfs_bitmap[1], &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * GETATTR_BITMAP request
 */
static int nfs4_xdr_enc_server_caps(struct rpc_rqst *req, __be32 *p,
				    struct nfs4_server_caps_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->fhandle, &hdr);
	encode_getattr_one(&xdr, FATTR4_WORD0_SUPPORTED_ATTRS|
			   FATTR4_WORD0_LINK_SUPPORT|
			   FATTR4_WORD0_SYMLINK_SUPPORT|
			   FATTR4_WORD0_ACLSUPPORT, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * a RENEW request
 */
static int nfs4_xdr_enc_renew(struct rpc_rqst *req, __be32 *p, struct nfs_client *clp)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops	= 0,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_renew(&xdr, clp, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * a SETCLIENTID request
 */
static int nfs4_xdr_enc_setclientid(struct rpc_rqst *req, __be32 *p, struct nfs4_setclientid *sc)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops	= 0,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_setclientid(&xdr, sc, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * a SETCLIENTID_CONFIRM request
 */
static int nfs4_xdr_enc_setclientid_confirm(struct rpc_rqst *req, __be32 *p, struct nfs4_setclientid_res *arg)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops	= 0,
	};
	const u32 lease_bitmap[2] = { FATTR4_WORD0_LEASE_TIME, 0 };

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_setclientid_confirm(&xdr, arg, &hdr);
	encode_putrootfh(&xdr, &hdr);
	encode_fsinfo(&xdr, lease_bitmap, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * DELEGRETURN request
 */
static int nfs4_xdr_enc_delegreturn(struct rpc_rqst *req, __be32 *p, const struct nfs4_delegreturnargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->fhandle, &hdr);
	encode_delegreturn(&xdr, args->stateid, &hdr);
	encode_getfattr(&xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * Encode FS_LOCATIONS request
 */
static int nfs4_xdr_enc_fs_locations(struct rpc_rqst *req, __be32 *p, struct nfs4_fs_locations_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};
	uint32_t replen;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->dir_fh, &hdr);
	encode_lookup(&xdr, args->name, &hdr);
	replen = hdr.replen;	/* get the attribute into args->page */
	encode_fs_locations(&xdr, args->bitmask, &hdr);

	xdr_inline_pages(&req->rq_rcv_buf, replen << 2, &args->page,
			0, PAGE_SIZE);
	encode_nops(&hdr);
	return 0;
}

#if defined(CONFIG_NFS_V4_1)
/*
 * EXCHANGE_ID request
 */
static int nfs4_xdr_enc_exchange_id(struct rpc_rqst *req, uint32_t *p,
				    struct nfs41_exchange_id_args *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = args->client->cl_minorversion,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_exchange_id(&xdr, args, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * a CREATE_SESSION request
 */
static int nfs4_xdr_enc_create_session(struct rpc_rqst *req, uint32_t *p,
				       struct nfs41_create_session_args *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = args->client->cl_minorversion,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_create_session(&xdr, args, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * a DESTROY_SESSION request
 */
static int nfs4_xdr_enc_destroy_session(struct rpc_rqst *req, uint32_t *p,
					struct nfs4_session *session)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = session->clp->cl_minorversion,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_destroy_session(&xdr, session, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * a SEQUENCE request
 */
static int nfs4_xdr_enc_sequence(struct rpc_rqst *req, uint32_t *p,
				 struct nfs4_sequence_args *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(args),
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, args, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * a GET_LEASE_TIME request
 */
static int nfs4_xdr_enc_get_lease_time(struct rpc_rqst *req, uint32_t *p,
				       struct nfs4_get_lease_time_args *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->la_seq_args),
	};
	const u32 lease_bitmap[2] = { FATTR4_WORD0_LEASE_TIME, 0 };

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->la_seq_args, &hdr);
	encode_putrootfh(&xdr, &hdr);
	encode_fsinfo(&xdr, lease_bitmap, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 * a RECLAIM_COMPLETE request
 */
static int nfs4_xdr_enc_reclaim_complete(struct rpc_rqst *req, uint32_t *p,
				     struct nfs41_reclaim_complete_args *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args)
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_reclaim_complete(&xdr, args, &hdr);
	encode_nops(&hdr);
	return 0;
}

#endif /* CONFIG_NFS_V4_1 */

static void print_overflow_msg(const char *func, const struct xdr_stream *xdr)
{
	dprintk("nfs: %s: prematurely hit end of receive buffer. "
		"Remaining buffer length is %tu words.\n",
		func, xdr->end - xdr->p);
}

static int decode_opaque_inline(struct xdr_stream *xdr, unsigned int *len, char **string)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	*len = be32_to_cpup(p);
	p = xdr_inline_decode(xdr, *len);
	if (unlikely(!p))
		goto out_overflow;
	*string = (char *)p;
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_compound_hdr(struct xdr_stream *xdr, struct compound_hdr *hdr)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	hdr->status = be32_to_cpup(p++);
	hdr->taglen = be32_to_cpup(p);

	p = xdr_inline_decode(xdr, hdr->taglen + 4);
	if (unlikely(!p))
		goto out_overflow;
	hdr->tag = (char *)p;
	p += XDR_QUADLEN(hdr->taglen);
	hdr->nops = be32_to_cpup(p);
	if (unlikely(hdr->nops < 1))
		return nfs4_stat_to_errno(hdr->status);
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_op_hdr(struct xdr_stream *xdr, enum nfs_opnum4 expected)
{
	__be32 *p;
	uint32_t opnum;
	int32_t nfserr;

	p = xdr_inline_decode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	opnum = be32_to_cpup(p++);
	if (opnum != expected) {
		dprintk("nfs: Server returned operation"
			" %d but we issued a request for %d\n",
				opnum, expected);
		return -EIO;
	}
	nfserr = be32_to_cpup(p);
	if (nfserr != NFS_OK)
		return nfs4_stat_to_errno(nfserr);
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

/* Dummy routine */
static int decode_ace(struct xdr_stream *xdr, void *ace, struct nfs_client *clp)
{
	__be32 *p;
	unsigned int strlen;
	char *str;

	p = xdr_inline_decode(xdr, 12);
	if (likely(p))
		return decode_opaque_inline(xdr, &strlen, &str);
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_attr_bitmap(struct xdr_stream *xdr, uint32_t *bitmap)
{
	uint32_t bmlen;
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	bmlen = be32_to_cpup(p);

	bitmap[0] = bitmap[1] = 0;
	p = xdr_inline_decode(xdr, (bmlen << 2));
	if (unlikely(!p))
		goto out_overflow;
	if (bmlen > 0) {
		bitmap[0] = be32_to_cpup(p++);
		if (bmlen > 1)
			bitmap[1] = be32_to_cpup(p);
	}
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static inline int decode_attr_length(struct xdr_stream *xdr, uint32_t *attrlen, __be32 **savep)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	*attrlen = be32_to_cpup(p);
	*savep = xdr->p;
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_attr_supported(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *bitmask)
{
	if (likely(bitmap[0] & FATTR4_WORD0_SUPPORTED_ATTRS)) {
		decode_attr_bitmap(xdr, bitmask);
		bitmap[0] &= ~FATTR4_WORD0_SUPPORTED_ATTRS;
	} else
		bitmask[0] = bitmask[1] = 0;
	dprintk("%s: bitmask=%08x:%08x\n", __func__, bitmask[0], bitmask[1]);
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
			goto out_overflow;
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
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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
			goto out_overflow;
		xdr_decode_hyper(p, change);
		bitmap[0] &= ~FATTR4_WORD0_CHANGE;
		ret = NFS_ATTR_FATTR_CHANGE;
	}
	dprintk("%s: change attribute=%Lu\n", __func__,
			(unsigned long long)*change);
	return ret;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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
			goto out_overflow;
		xdr_decode_hyper(p, size);
		bitmap[0] &= ~FATTR4_WORD0_SIZE;
		ret = NFS_ATTR_FATTR_SIZE;
	}
	dprintk("%s: file size=%Lu\n", __func__, (unsigned long long)*size);
	return ret;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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
			goto out_overflow;
		*res = be32_to_cpup(p);
		bitmap[0] &= ~FATTR4_WORD0_LINK_SUPPORT;
	}
	dprintk("%s: link support=%s\n", __func__, *res == 0 ? "false" : "true");
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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
			goto out_overflow;
		*res = be32_to_cpup(p);
		bitmap[0] &= ~FATTR4_WORD0_SYMLINK_SUPPORT;
	}
	dprintk("%s: symlink support=%s\n", __func__, *res == 0 ? "false" : "true");
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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
			goto out_overflow;
		p = xdr_decode_hyper(p, &fsid->major);
		xdr_decode_hyper(p, &fsid->minor);
		bitmap[0] &= ~FATTR4_WORD0_FSID;
		ret = NFS_ATTR_FATTR_FSID;
	}
	dprintk("%s: fsid=(0x%Lx/0x%Lx)\n", __func__,
			(unsigned long long)fsid->major,
			(unsigned long long)fsid->minor);
	return ret;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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
			goto out_overflow;
		*res = be32_to_cpup(p);
		bitmap[0] &= ~FATTR4_WORD0_LEASE_TIME;
	}
	dprintk("%s: file size=%u\n", __func__, (unsigned int)*res);
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_attr_aclsupport(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *res)
{
	__be32 *p;

	*res = ACL4_SUPPORT_ALLOW_ACL|ACL4_SUPPORT_DENY_ACL;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_ACLSUPPORT - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_ACLSUPPORT)) {
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			goto out_overflow;
		*res = be32_to_cpup(p);
		bitmap[0] &= ~FATTR4_WORD0_ACLSUPPORT;
	}
	dprintk("%s: ACLs supported=%u\n", __func__, (unsigned int)*res);
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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
			goto out_overflow;
		xdr_decode_hyper(p, fileid);
		bitmap[0] &= ~FATTR4_WORD0_FILEID;
		ret = NFS_ATTR_FATTR_FILEID;
	}
	dprintk("%s: fileid=%Lu\n", __func__, (unsigned long long)*fileid);
	return ret;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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
			goto out_overflow;
		xdr_decode_hyper(p, fileid);
		bitmap[1] &= ~FATTR4_WORD1_MOUNTED_ON_FILEID;
		ret = NFS_ATTR_FATTR_FILEID;
	}
	dprintk("%s: fileid=%Lu\n", __func__, (unsigned long long)*fileid);
	return ret;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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
			goto out_overflow;
		xdr_decode_hyper(p, res);
		bitmap[0] &= ~FATTR4_WORD0_FILES_AVAIL;
	}
	dprintk("%s: files avail=%Lu\n", __func__, (unsigned long long)*res);
	return status;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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
			goto out_overflow;
		xdr_decode_hyper(p, res);
		bitmap[0] &= ~FATTR4_WORD0_FILES_FREE;
	}
	dprintk("%s: files free=%Lu\n", __func__, (unsigned long long)*res);
	return status;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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
			goto out_overflow;
		xdr_decode_hyper(p, res);
		bitmap[0] &= ~FATTR4_WORD0_FILES_TOTAL;
	}
	dprintk("%s: files total=%Lu\n", __func__, (unsigned long long)*res);
	return status;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_pathname(struct xdr_stream *xdr, struct nfs4_pathname *path)
{
	u32 n;
	__be32 *p;
	int status = 0;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	n = be32_to_cpup(p);
	if (n == 0)
		goto root_path;
	dprintk("path ");
	path->ncomponents = 0;
	while (path->ncomponents < n) {
		struct nfs4_string *component = &path->components[path->ncomponents];
		status = decode_opaque_inline(xdr, &component->len, &component->data);
		if (unlikely(status != 0))
			goto out_eio;
		if (path->ncomponents != n)
			dprintk("/");
		dprintk("%s", component->data);
		if (path->ncomponents < NFS4_PATHNAME_MAXCOMPONENTS)
			path->ncomponents++;
		else {
			dprintk("cannot parse %d components in path\n", n);
			goto out_eio;
		}
	}
out:
	dprintk("\n");
	return status;
root_path:
/* a root pathname is sent as a zero component4 */
	path->ncomponents = 1;
	path->components[0].len=0;
	path->components[0].data=NULL;
	dprintk("path /\n");
	goto out;
out_eio:
	dprintk(" status %d", status);
	status = -EIO;
	goto out;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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
	dprintk("%s: fsroot ", __func__);
	status = decode_pathname(xdr, &res->fs_path);
	if (unlikely(status != 0))
		goto out;
	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	n = be32_to_cpup(p);
	if (n <= 0)
		goto out_eio;
	res->nlocations = 0;
	while (res->nlocations < n) {
		u32 m;
		struct nfs4_fs_location *loc = &res->locations[res->nlocations];

		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			goto out_overflow;
		m = be32_to_cpup(p);

		loc->nservers = 0;
		dprintk("%s: servers ", __func__);
		while (loc->nservers < m) {
			struct nfs4_string *server = &loc->servers[loc->nservers];
			status = decode_opaque_inline(xdr, &server->len, &server->data);
			if (unlikely(status != 0))
				goto out_eio;
			dprintk("%s ", server->data);
			if (loc->nservers < NFS4_FS_LOCATION_MAXSERVERS)
				loc->nservers++;
			else {
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
			}
		}
		status = decode_pathname(xdr, &loc->rootpath);
		if (unlikely(status != 0))
			goto out_eio;
		if (res->nlocations < NFS4_FS_LOCATIONS_MAXENTRIES)
			res->nlocations++;
	}
	if (res->nlocations != 0)
		status = NFS_ATTR_FATTR_V4_REFERRAL;
out:
	dprintk("%s: fs_locations done, error = %d\n", __func__, status);
	return status;
out_overflow:
	print_overflow_msg(__func__, xdr);
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
			goto out_overflow;
		xdr_decode_hyper(p, res);
		bitmap[0] &= ~FATTR4_WORD0_MAXFILESIZE;
	}
	dprintk("%s: maxfilesize=%Lu\n", __func__, (unsigned long long)*res);
	return status;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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
			goto out_overflow;
		*maxlink = be32_to_cpup(p);
		bitmap[0] &= ~FATTR4_WORD0_MAXLINK;
	}
	dprintk("%s: maxlink=%u\n", __func__, *maxlink);
	return status;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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
			goto out_overflow;
		*maxname = be32_to_cpup(p);
		bitmap[0] &= ~FATTR4_WORD0_MAXNAME;
	}
	dprintk("%s: maxname=%u\n", __func__, *maxname);
	return status;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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
			goto out_overflow;
		xdr_decode_hyper(p, &maxread);
		if (maxread > 0x7FFFFFFF)
			maxread = 0x7FFFFFFF;
		*res = (uint32_t)maxread;
		bitmap[0] &= ~FATTR4_WORD0_MAXREAD;
	}
	dprintk("%s: maxread=%lu\n", __func__, (unsigned long)*res);
	return status;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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
			goto out_overflow;
		xdr_decode_hyper(p, &maxwrite);
		if (maxwrite > 0x7FFFFFFF)
			maxwrite = 0x7FFFFFFF;
		*res = (uint32_t)maxwrite;
		bitmap[0] &= ~FATTR4_WORD0_MAXWRITE;
	}
	dprintk("%s: maxwrite=%lu\n", __func__, (unsigned long)*res);
	return status;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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
			goto out_overflow;
		tmp = be32_to_cpup(p);
		*mode = tmp & ~S_IFMT;
		bitmap[1] &= ~FATTR4_WORD1_MODE;
		ret = NFS_ATTR_FATTR_MODE;
	}
	dprintk("%s: file mode=0%o\n", __func__, (unsigned int)*mode);
	return ret;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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
			goto out_overflow;
		*nlink = be32_to_cpup(p);
		bitmap[1] &= ~FATTR4_WORD1_NUMLINKS;
		ret = NFS_ATTR_FATTR_NLINK;
	}
	dprintk("%s: nlink=%u\n", __func__, (unsigned int)*nlink);
	return ret;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_attr_owner(struct xdr_stream *xdr, uint32_t *bitmap,
		struct nfs_client *clp, uint32_t *uid, int may_sleep)
{
	uint32_t len;
	__be32 *p;
	int ret = 0;

	*uid = -2;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_OWNER - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_OWNER)) {
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			goto out_overflow;
		len = be32_to_cpup(p);
		p = xdr_inline_decode(xdr, len);
		if (unlikely(!p))
			goto out_overflow;
		if (!may_sleep) {
			/* do nothing */
		} else if (len < XDR_MAX_NETOBJ) {
			if (nfs_map_name_to_uid(clp, (char *)p, len, uid) == 0)
				ret = NFS_ATTR_FATTR_OWNER;
			else
				dprintk("%s: nfs_map_name_to_uid failed!\n",
						__func__);
		} else
			dprintk("%s: name too long (%u)!\n",
					__func__, len);
		bitmap[1] &= ~FATTR4_WORD1_OWNER;
	}
	dprintk("%s: uid=%d\n", __func__, (int)*uid);
	return ret;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_attr_group(struct xdr_stream *xdr, uint32_t *bitmap,
		struct nfs_client *clp, uint32_t *gid, int may_sleep)
{
	uint32_t len;
	__be32 *p;
	int ret = 0;

	*gid = -2;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_OWNER_GROUP - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_OWNER_GROUP)) {
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			goto out_overflow;
		len = be32_to_cpup(p);
		p = xdr_inline_decode(xdr, len);
		if (unlikely(!p))
			goto out_overflow;
		if (!may_sleep) {
			/* do nothing */
		} else if (len < XDR_MAX_NETOBJ) {
			if (nfs_map_group_to_gid(clp, (char *)p, len, gid) == 0)
				ret = NFS_ATTR_FATTR_GROUP;
			else
				dprintk("%s: nfs_map_group_to_gid failed!\n",
						__func__);
		} else
			dprintk("%s: name too long (%u)!\n",
					__func__, len);
		bitmap[1] &= ~FATTR4_WORD1_OWNER_GROUP;
	}
	dprintk("%s: gid=%d\n", __func__, (int)*gid);
	return ret;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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
			goto out_overflow;
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
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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
			goto out_overflow;
		xdr_decode_hyper(p, res);
		bitmap[1] &= ~FATTR4_WORD1_SPACE_AVAIL;
	}
	dprintk("%s: space avail=%Lu\n", __func__, (unsigned long long)*res);
	return status;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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
			goto out_overflow;
		xdr_decode_hyper(p, res);
		bitmap[1] &= ~FATTR4_WORD1_SPACE_FREE;
	}
	dprintk("%s: space free=%Lu\n", __func__, (unsigned long long)*res);
	return status;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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
			goto out_overflow;
		xdr_decode_hyper(p, res);
		bitmap[1] &= ~FATTR4_WORD1_SPACE_TOTAL;
	}
	dprintk("%s: space total=%Lu\n", __func__, (unsigned long long)*res);
	return status;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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
			goto out_overflow;
		xdr_decode_hyper(p, used);
		bitmap[1] &= ~FATTR4_WORD1_SPACE_USED;
		ret = NFS_ATTR_FATTR_SPACE_USED;
	}
	dprintk("%s: space used=%Lu\n", __func__,
			(unsigned long long)*used);
	return ret;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_attr_time(struct xdr_stream *xdr, struct timespec *time)
{
	__be32 *p;
	uint64_t sec;
	uint32_t nsec;

	p = xdr_inline_decode(xdr, 12);
	if (unlikely(!p))
		goto out_overflow;
	p = xdr_decode_hyper(p, &sec);
	nsec = be32_to_cpup(p);
	time->tv_sec = (time_t)sec;
	time->tv_nsec = (long)nsec;
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_attr_time_access(struct xdr_stream *xdr, uint32_t *bitmap, struct timespec *time)
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
	dprintk("%s: atime=%ld\n", __func__, (long)time->tv_sec);
	return status;
}

static int decode_attr_time_metadata(struct xdr_stream *xdr, uint32_t *bitmap, struct timespec *time)
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
	dprintk("%s: ctime=%ld\n", __func__, (long)time->tv_sec);
	return status;
}

static int decode_attr_time_modify(struct xdr_stream *xdr, uint32_t *bitmap, struct timespec *time)
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
	dprintk("%s: mtime=%ld\n", __func__, (long)time->tv_sec);
	return status;
}

static int verify_attr_len(struct xdr_stream *xdr, __be32 *savep, uint32_t attrlen)
{
	unsigned int attrwords = XDR_QUADLEN(attrlen);
	unsigned int nwords = xdr->p - savep;

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
		goto out_overflow;
	cinfo->atomic = be32_to_cpup(p++);
	p = xdr_decode_hyper(p, &cinfo->before);
	xdr_decode_hyper(p, &cinfo->after);
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_access(struct xdr_stream *xdr, struct nfs4_accessres *access)
{
	__be32 *p;
	uint32_t supp, acc;
	int status;

	status = decode_op_hdr(xdr, OP_ACCESS);
	if (status)
		return status;
	p = xdr_inline_decode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	supp = be32_to_cpup(p++);
	acc = be32_to_cpup(p);
	access->supported = supp;
	access->access = acc;
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_opaque_fixed(struct xdr_stream *xdr, void *buf, size_t len)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, len);
	if (likely(p)) {
		memcpy(buf, p, len);
		return 0;
	}
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_stateid(struct xdr_stream *xdr, nfs4_stateid *stateid)
{
	return decode_opaque_fixed(xdr, stateid->data, NFS4_STATEID_SIZE);
}

static int decode_close(struct xdr_stream *xdr, struct nfs_closeres *res)
{
	int status;

	status = decode_op_hdr(xdr, OP_CLOSE);
	if (status != -EIO)
		nfs_increment_open_seqid(status, res->seqid);
	if (!status)
		status = decode_stateid(xdr, &res->stateid);
	return status;
}

static int decode_verifier(struct xdr_stream *xdr, void *verifier)
{
	return decode_opaque_fixed(xdr, verifier, 8);
}

static int decode_commit(struct xdr_stream *xdr, struct nfs_writeres *res)
{
	int status;

	status = decode_op_hdr(xdr, OP_COMMIT);
	if (!status)
		status = decode_verifier(xdr, res->verf->verifier);
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
		goto out_overflow;
	bmlen = be32_to_cpup(p);
	p = xdr_inline_decode(xdr, bmlen << 2);
	if (likely(p))
		return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_server_caps(struct xdr_stream *xdr, struct nfs4_server_caps_res *res)
{
	__be32 *savep;
	uint32_t attrlen, bitmap[2] = {0};
	int status;

	if ((status = decode_op_hdr(xdr, OP_GETATTR)) != 0)
		goto xdr_error;
	if ((status = decode_attr_bitmap(xdr, bitmap)) != 0)
		goto xdr_error;
	if ((status = decode_attr_length(xdr, &attrlen, &savep)) != 0)
		goto xdr_error;
	if ((status = decode_attr_supported(xdr, bitmap, res->attr_bitmask)) != 0)
		goto xdr_error;
	if ((status = decode_attr_link_support(xdr, bitmap, &res->has_links)) != 0)
		goto xdr_error;
	if ((status = decode_attr_symlink_support(xdr, bitmap, &res->has_symlinks)) != 0)
		goto xdr_error;
	if ((status = decode_attr_aclsupport(xdr, bitmap, &res->acl_bitmask)) != 0)
		goto xdr_error;
	status = verify_attr_len(xdr, savep, attrlen);
xdr_error:
	dprintk("%s: xdr returned %d!\n", __func__, -status);
	return status;
}

static int decode_statfs(struct xdr_stream *xdr, struct nfs_fsstat *fsstat)
{
	__be32 *savep;
	uint32_t attrlen, bitmap[2] = {0};
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
	__be32 *savep;
	uint32_t attrlen, bitmap[2] = {0};
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

static int decode_getfattr(struct xdr_stream *xdr, struct nfs_fattr *fattr,
		const struct nfs_server *server, int may_sleep)
{
	__be32 *savep;
	uint32_t attrlen,
		 bitmap[2] = {0},
		 type;
	int status;
	umode_t fmode = 0;
	uint64_t fileid;

	status = decode_op_hdr(xdr, OP_GETATTR);
	if (status < 0)
		goto xdr_error;

	status = decode_attr_bitmap(xdr, bitmap);
	if (status < 0)
		goto xdr_error;

	status = decode_attr_length(xdr, &attrlen, &savep);
	if (status < 0)
		goto xdr_error;


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

	status = decode_attr_fileid(xdr, bitmap, &fattr->fileid);
	if (status < 0)
		goto xdr_error;
	fattr->valid |= status;

	status = decode_attr_fs_locations(xdr, bitmap, container_of(fattr,
						struct nfs4_fs_locations,
						fattr));
	if (status < 0)
		goto xdr_error;
	fattr->valid |= status;

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

	status = decode_attr_owner(xdr, bitmap, server->nfs_client,
			&fattr->uid, may_sleep);
	if (status < 0)
		goto xdr_error;
	fattr->valid |= status;

	status = decode_attr_group(xdr, bitmap, server->nfs_client,
			&fattr->gid, may_sleep);
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

	status = decode_attr_mounted_on_fileid(xdr, bitmap, &fileid);
	if (status < 0)
		goto xdr_error;
	if (status != 0 && !(fattr->valid & status)) {
		fattr->fileid = fileid;
		fattr->valid |= status;
	}

	status = verify_attr_len(xdr, savep, attrlen);
xdr_error:
	dprintk("%s: xdr returned %d\n", __func__, -status);
	return status;
}


static int decode_fsinfo(struct xdr_stream *xdr, struct nfs_fsinfo *fsinfo)
{
	__be32 *savep;
	uint32_t attrlen, bitmap[2];
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
		goto out_overflow;
	len = be32_to_cpup(p);
	if (len > NFS4_FHSIZE)
		return -EIO;
	fh->size = len;
	p = xdr_inline_decode(xdr, len);
	if (unlikely(!p))
		goto out_overflow;
	memcpy(fh->data, p, len);
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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

	p = xdr_inline_decode(xdr, 32);
	if (unlikely(!p))
		goto out_overflow;
	p = xdr_decode_hyper(p, &offset);
	p = xdr_decode_hyper(p, &length);
	type = be32_to_cpup(p++);
	if (fl != NULL) {
		fl->fl_start = (loff_t)offset;
		fl->fl_end = fl->fl_start + (loff_t)length - 1;
		if (length == ~(uint64_t)0)
			fl->fl_end = OFFSET_MAX;
		fl->fl_type = F_WRLCK;
		if (type & 1)
			fl->fl_type = F_RDLCK;
		fl->fl_pid = 0;
	}
	p = xdr_decode_hyper(p, &clientid);
	namelen = be32_to_cpup(p);
	p = xdr_inline_decode(xdr, namelen);
	if (likely(p))
		return -NFS4ERR_DENIED;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_lock(struct xdr_stream *xdr, struct nfs_lock_res *res)
{
	int status;

	status = decode_op_hdr(xdr, OP_LOCK);
	if (status == -EIO)
		goto out;
	if (status == 0) {
		status = decode_stateid(xdr, &res->stateid);
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
		status = decode_stateid(xdr, &res->stateid);
	return status;
}

static int decode_lookup(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_LOOKUP);
}

/* This is too sick! */
static int decode_space_limit(struct xdr_stream *xdr, u64 *maxsize)
{
	__be32 *p;
	uint32_t limit_type, nblocks, blocksize;

	p = xdr_inline_decode(xdr, 12);
	if (unlikely(!p))
		goto out_overflow;
	limit_type = be32_to_cpup(p++);
	switch (limit_type) {
	case 1:
		xdr_decode_hyper(p, maxsize);
		break;
	case 2:
		nblocks = be32_to_cpup(p++);
		blocksize = be32_to_cpup(p);
		*maxsize = (uint64_t)nblocks * (uint64_t)blocksize;
	}
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_delegation(struct xdr_stream *xdr, struct nfs_openres *res)
{
	__be32 *p;
	uint32_t delegation_type;
	int status;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	delegation_type = be32_to_cpup(p);
	if (delegation_type == NFS4_OPEN_DELEGATE_NONE) {
		res->delegation_type = 0;
		return 0;
	}
	status = decode_stateid(xdr, &res->delegation);
	if (unlikely(status))
		return status;
	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	res->do_recall = be32_to_cpup(p);

	switch (delegation_type) {
	case NFS4_OPEN_DELEGATE_READ:
		res->delegation_type = FMODE_READ;
		break;
	case NFS4_OPEN_DELEGATE_WRITE:
		res->delegation_type = FMODE_WRITE|FMODE_READ;
		if (decode_space_limit(xdr, &res->maxsize) < 0)
				return -EIO;
	}
	return decode_ace(xdr, NULL, res->server->nfs_client);
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_open(struct xdr_stream *xdr, struct nfs_openres *res)
{
	__be32 *p;
	uint32_t savewords, bmlen, i;
	int status;

	status = decode_op_hdr(xdr, OP_OPEN);
	if (status != -EIO)
		nfs_increment_open_seqid(status, res->seqid);
	if (!status)
		status = decode_stateid(xdr, &res->stateid);
	if (unlikely(status))
		return status;

	decode_change_info(xdr, &res->cinfo);

	p = xdr_inline_decode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	res->rflags = be32_to_cpup(p++);
	bmlen = be32_to_cpup(p);
	if (bmlen > 10)
		goto xdr_error;

	p = xdr_inline_decode(xdr, bmlen << 2);
	if (unlikely(!p))
		goto out_overflow;
	savewords = min_t(uint32_t, bmlen, NFS4_BITMAP_SIZE);
	for (i = 0; i < savewords; ++i)
		res->attrset[i] = be32_to_cpup(p++);
	for (; i < NFS4_BITMAP_SIZE; i++)
		res->attrset[i] = 0;

	return decode_delegation(xdr, res);
xdr_error:
	dprintk("%s: Bitmap too large! Length = %u\n", __func__, bmlen);
	return -EIO;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_open_confirm(struct xdr_stream *xdr, struct nfs_open_confirmres *res)
{
	int status;

	status = decode_op_hdr(xdr, OP_OPEN_CONFIRM);
	if (status != -EIO)
		nfs_increment_open_seqid(status, res->seqid);
	if (!status)
		status = decode_stateid(xdr, &res->stateid);
	return status;
}

static int decode_open_downgrade(struct xdr_stream *xdr, struct nfs_closeres *res)
{
	int status;

	status = decode_op_hdr(xdr, OP_OPEN_DOWNGRADE);
	if (status != -EIO)
		nfs_increment_open_seqid(status, res->seqid);
	if (!status)
		status = decode_stateid(xdr, &res->stateid);
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

static int decode_read(struct xdr_stream *xdr, struct rpc_rqst *req, struct nfs_readres *res)
{
	struct kvec *iov = req->rq_rcv_buf.head;
	__be32 *p;
	uint32_t count, eof, recvd, hdrlen;
	int status;

	status = decode_op_hdr(xdr, OP_READ);
	if (status)
		return status;
	p = xdr_inline_decode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	eof = be32_to_cpup(p++);
	count = be32_to_cpup(p);
	hdrlen = (u8 *) p - (u8 *) iov->iov_base;
	recvd = req->rq_rcv_buf.len - hdrlen;
	if (count > recvd) {
		dprintk("NFS: server cheating in read reply: "
				"count %u > recvd %u\n", count, recvd);
		count = recvd;
		eof = 0;
	}
	xdr_read_pages(xdr, count);
	res->eof = eof;
	res->count = count;
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_readdir(struct xdr_stream *xdr, struct rpc_rqst *req, struct nfs4_readdir_res *readdir)
{
	struct xdr_buf	*rcvbuf = &req->rq_rcv_buf;
	struct page	*page = *rcvbuf->pages;
	struct kvec	*iov = rcvbuf->head;
	size_t		hdrlen;
	u32		recvd, pglen = rcvbuf->page_len;
	__be32		*end, *entry, *p, *kaddr;
	unsigned int	nr = 0;
	int		status;

	status = decode_op_hdr(xdr, OP_READDIR);
	if (!status)
		status = decode_verifier(xdr, readdir->verifier.data);
	if (unlikely(status))
		return status;
	dprintk("%s: verifier = %08x:%08x\n",
			__func__,
			((u32 *)readdir->verifier.data)[0],
			((u32 *)readdir->verifier.data)[1]);


	hdrlen = (char *) xdr->p - (char *) iov->iov_base;
	recvd = rcvbuf->len - hdrlen;
	if (pglen > recvd)
		pglen = recvd;
	xdr_read_pages(xdr, pglen);

	BUG_ON(pglen + readdir->pgbase > PAGE_CACHE_SIZE);
	kaddr = p = kmap_atomic(page, KM_USER0);
	end = p + ((pglen + readdir->pgbase) >> 2);
	entry = p;

	/* Make sure the packet actually has a value_follows and EOF entry */
	if ((entry + 1) > end)
		goto short_pkt;

	for (; *p++; nr++) {
		u32 len, attrlen, xlen;
		if (end - p < 3)
			goto short_pkt;
		dprintk("cookie = %Lu, ", *((unsigned long long *)p));
		p += 2;			/* cookie */
		len = ntohl(*p++);	/* filename length */
		if (len > NFS4_MAXNAMLEN) {
			dprintk("NFS: giant filename in readdir (len 0x%x)\n",
					len);
			goto err_unmap;
		}
		xlen = XDR_QUADLEN(len);
		if (end - p < xlen + 1)
			goto short_pkt;
		dprintk("filename = %*s\n", len, (char *)p);
		p += xlen;
		len = ntohl(*p++);	/* bitmap length */
		if (end - p < len + 1)
			goto short_pkt;
		p += len;
		attrlen = XDR_QUADLEN(ntohl(*p++));
		if (end - p < attrlen + 2)
			goto short_pkt;
		p += attrlen;		/* attributes */
		entry = p;
	}
	/*
	 * Apparently some server sends responses that are a valid size, but
	 * contain no entries, and have value_follows==0 and EOF==0. For
	 * those, just set the EOF marker.
	 */
	if (!nr && entry[1] == 0) {
		dprintk("NFS: readdir reply truncated!\n");
		entry[1] = 1;
	}
out:
	kunmap_atomic(kaddr, KM_USER0);
	return 0;
short_pkt:
	/*
	 * When we get a short packet there are 2 possibilities. We can
	 * return an error, or fix up the response to look like a valid
	 * response and return what we have so far. If there are no
	 * entries and the packet was short, then return -EIO. If there
	 * are valid entries in the response, return them and pretend that
	 * the call was successful, but incomplete. The caller can retry the
	 * readdir starting at the last cookie.
	 */
	dprintk("%s: short packet at entry %d\n", __func__, nr);
	entry[0] = entry[1] = 0;
	if (nr)
		goto out;
err_unmap:
	kunmap_atomic(kaddr, KM_USER0);
	return -errno_NFSERR_IO;
}

static int decode_readlink(struct xdr_stream *xdr, struct rpc_rqst *req)
{
	struct xdr_buf *rcvbuf = &req->rq_rcv_buf;
	struct kvec *iov = rcvbuf->head;
	size_t hdrlen;
	u32 len, recvd;
	__be32 *p;
	char *kaddr;
	int status;

	status = decode_op_hdr(xdr, OP_READLINK);
	if (status)
		return status;

	/* Convert length of symlink */
	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	len = be32_to_cpup(p);
	if (len >= rcvbuf->page_len || len <= 0) {
		dprintk("nfs: server returned giant symlink!\n");
		return -ENAMETOOLONG;
	}
	hdrlen = (char *) xdr->p - (char *) iov->iov_base;
	recvd = req->rq_rcv_buf.len - hdrlen;
	if (recvd < len) {
		dprintk("NFS: server cheating in readlink reply: "
				"count %u > recvd %u\n", len, recvd);
		return -EIO;
	}
	xdr_read_pages(xdr, len);
	/*
	 * The XDR encode routine has set things up so that
	 * the link text will be copied directly into the
	 * buffer.  We just have to do overflow-checking,
	 * and and null-terminate the text (the VFS expects
	 * null-termination).
	 */
	kaddr = (char *)kmap_atomic(rcvbuf->pages[0], KM_USER0);
	kaddr[len+rcvbuf->page_base] = '\0';
	kunmap_atomic(kaddr, KM_USER0);
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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
		size_t *acl_len)
{
	__be32 *savep;
	uint32_t attrlen,
		 bitmap[2] = {0};
	struct kvec *iov = req->rq_rcv_buf.head;
	int status;

	*acl_len = 0;
	if ((status = decode_op_hdr(xdr, OP_GETATTR)) != 0)
		goto out;
	if ((status = decode_attr_bitmap(xdr, bitmap)) != 0)
		goto out;
	if ((status = decode_attr_length(xdr, &attrlen, &savep)) != 0)
		goto out;

	if (unlikely(bitmap[0] & (FATTR4_WORD0_ACL - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_ACL)) {
		size_t hdrlen;
		u32 recvd;

		/* We ignore &savep and don't do consistency checks on
		 * the attr length.  Let userspace figure it out.... */
		hdrlen = (u8 *)xdr->p - (u8 *)iov->iov_base;
		recvd = req->rq_rcv_buf.len - hdrlen;
		if (attrlen > recvd) {
			dprintk("NFS: server cheating in getattr"
					" acl reply: attrlen %u > recvd %u\n",
					attrlen, recvd);
			return -EINVAL;
		}
		xdr_read_pages(xdr, attrlen);
		*acl_len = attrlen;
	} else
		status = -EOPNOTSUPP;

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
	__be32 *p;
	uint32_t bmlen;
	int status;

	status = decode_op_hdr(xdr, OP_SETATTR);
	if (status)
		return status;
	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	bmlen = be32_to_cpup(p);
	p = xdr_inline_decode(xdr, bmlen << 2);
	if (likely(p))
		return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_setclientid(struct xdr_stream *xdr, struct nfs4_setclientid_res *res)
{
	__be32 *p;
	uint32_t opnum;
	int32_t nfserr;

	p = xdr_inline_decode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
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
			goto out_overflow;
		p = xdr_decode_hyper(p, &res->clientid);
		memcpy(res->confirm.data, p, NFS4_VERIFIER_SIZE);
	} else if (nfserr == NFSERR_CLID_INUSE) {
		uint32_t len;

		/* skip netid string */
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			goto out_overflow;
		len = be32_to_cpup(p);
		p = xdr_inline_decode(xdr, len);
		if (unlikely(!p))
			goto out_overflow;

		/* skip uaddr string */
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			goto out_overflow;
		len = be32_to_cpup(p);
		p = xdr_inline_decode(xdr, len);
		if (unlikely(!p))
			goto out_overflow;
		return -NFSERR_CLID_INUSE;
	} else
		return nfs4_stat_to_errno(nfserr);

	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_setclientid_confirm(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_SETCLIENTID_CONFIRM);
}

static int decode_write(struct xdr_stream *xdr, struct nfs_writeres *res)
{
	__be32 *p;
	int status;

	status = decode_op_hdr(xdr, OP_WRITE);
	if (status)
		return status;

	p = xdr_inline_decode(xdr, 16);
	if (unlikely(!p))
		goto out_overflow;
	res->count = be32_to_cpup(p++);
	res->verf->committed = be32_to_cpup(p++);
	memcpy(res->verf->verifier, p, 8);
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_delegreturn(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_DELEGRETURN);
}

#if defined(CONFIG_NFS_V4_1)
static int decode_exchange_id(struct xdr_stream *xdr,
			      struct nfs41_exchange_id_res *res)
{
	__be32 *p;
	uint32_t dummy;
	char *dummy_str;
	int status;
	struct nfs_client *clp = res->client;

	status = decode_op_hdr(xdr, OP_EXCHANGE_ID);
	if (status)
		return status;

	p = xdr_inline_decode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	xdr_decode_hyper(p, &clp->cl_ex_clid);
	p = xdr_inline_decode(xdr, 12);
	if (unlikely(!p))
		goto out_overflow;
	clp->cl_seqid = be32_to_cpup(p++);
	clp->cl_exchange_flags = be32_to_cpup(p++);

	/* We ask for SP4_NONE */
	dummy = be32_to_cpup(p);
	if (dummy != SP4_NONE)
		return -EIO;

	/* Throw away minor_id */
	p = xdr_inline_decode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;

	/* Throw away Major id */
	status = decode_opaque_inline(xdr, &dummy, &dummy_str);
	if (unlikely(status))
		return status;

	/* Throw away server_scope */
	status = decode_opaque_inline(xdr, &dummy, &dummy_str);
	if (unlikely(status))
		return status;

	/* Throw away Implementation id array */
	status = decode_opaque_inline(xdr, &dummy, &dummy_str);
	if (unlikely(status))
		return status;

	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_chan_attrs(struct xdr_stream *xdr,
			     struct nfs4_channel_attrs *attrs)
{
	__be32 *p;
	u32 nr_attrs;

	p = xdr_inline_decode(xdr, 28);
	if (unlikely(!p))
		goto out_overflow;
	attrs->headerpadsz = be32_to_cpup(p++);
	attrs->max_rqst_sz = be32_to_cpup(p++);
	attrs->max_resp_sz = be32_to_cpup(p++);
	attrs->max_resp_sz_cached = be32_to_cpup(p++);
	attrs->max_ops = be32_to_cpup(p++);
	attrs->max_reqs = be32_to_cpup(p++);
	nr_attrs = be32_to_cpup(p);
	if (unlikely(nr_attrs > 1)) {
		printk(KERN_WARNING "%s: Invalid rdma channel attrs count %u\n",
			__func__, nr_attrs);
		return -EINVAL;
	}
	if (nr_attrs == 1) {
		p = xdr_inline_decode(xdr, 4); /* skip rdma_attrs */
		if (unlikely(!p))
			goto out_overflow;
	}
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_sessionid(struct xdr_stream *xdr, struct nfs4_sessionid *sid)
{
	return decode_opaque_fixed(xdr, sid->data, NFS4_MAX_SESSIONID_LEN);
}

static int decode_create_session(struct xdr_stream *xdr,
				 struct nfs41_create_session_res *res)
{
	__be32 *p;
	int status;
	struct nfs_client *clp = res->client;
	struct nfs4_session *session = clp->cl_session;

	status = decode_op_hdr(xdr, OP_CREATE_SESSION);
	if (!status)
		status = decode_sessionid(xdr, &session->sess_id);
	if (unlikely(status))
		return status;

	/* seqid, flags */
	p = xdr_inline_decode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	clp->cl_seqid = be32_to_cpup(p++);
	session->flags = be32_to_cpup(p);

	/* Channel attributes */
	status = decode_chan_attrs(xdr, &session->fc_attrs);
	if (!status)
		status = decode_chan_attrs(xdr, &session->bc_attrs);
	return status;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_destroy_session(struct xdr_stream *xdr, void *dummy)
{
	return decode_op_hdr(xdr, OP_DESTROY_SESSION);
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
	struct nfs4_slot *slot;
	struct nfs4_sessionid id;
	u32 dummy;
	int status;
	__be32 *p;

	if (!res->sr_session)
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

	if (memcmp(id.data, res->sr_session->sess_id.data,
		   NFS4_MAX_SESSIONID_LEN)) {
		dprintk("%s Invalid session id\n", __func__);
		goto out_err;
	}

	p = xdr_inline_decode(xdr, 20);
	if (unlikely(!p))
		goto out_overflow;

	/* seqid */
	slot = &res->sr_session->fc_slot_table.slots[res->sr_slotid];
	dummy = be32_to_cpup(p++);
	if (dummy != slot->seq_nr) {
		dprintk("%s Invalid sequence number\n", __func__);
		goto out_err;
	}
	/* slot id */
	dummy = be32_to_cpup(p++);
	if (dummy != res->sr_slotid) {
		dprintk("%s Invalid slot id\n", __func__);
		goto out_err;
	}
	/* highest slot id - currently not processed */
	dummy = be32_to_cpup(p++);
	/* target highest slot id - currently not processed */
	dummy = be32_to_cpup(p++);
	/* result flags */
	res->sr_status_flags = be32_to_cpup(p);
	status = 0;
out_err:
	res->sr_status = status;
	return status;
out_overflow:
	print_overflow_msg(__func__, xdr);
	status = -EIO;
	goto out_err;
#else  /* CONFIG_NFS_V4_1 */
	return 0;
#endif /* CONFIG_NFS_V4_1 */
}

/*
 * END OF "GENERIC" DECODE ROUTINES.
 */

/*
 * Decode OPEN_DOWNGRADE response
 */
static int nfs4_xdr_dec_open_downgrade(struct rpc_rqst *rqstp, __be32 *p, struct nfs_closeres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_open_downgrade(&xdr, res);
	if (status != 0)
		goto out;
	decode_getfattr(&xdr, res->fattr, res->server,
			!RPC_IS_ASYNC(rqstp->rq_task));
out:
	return status;
}

/*
 * Decode ACCESS response
 */
static int nfs4_xdr_dec_access(struct rpc_rqst *rqstp, __be32 *p, struct nfs4_accessres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status != 0)
		goto out;
	status = decode_access(&xdr, res);
	if (status != 0)
		goto out;
	decode_getfattr(&xdr, res->fattr, res->server,
			!RPC_IS_ASYNC(rqstp->rq_task));
out:
	return status;
}

/*
 * Decode LOOKUP response
 */
static int nfs4_xdr_dec_lookup(struct rpc_rqst *rqstp, __be32 *p, struct nfs4_lookup_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	if ((status = decode_putfh(&xdr)) != 0)
		goto out;
	if ((status = decode_lookup(&xdr)) != 0)
		goto out;
	if ((status = decode_getfh(&xdr, res->fh)) != 0)
		goto out;
	status = decode_getfattr(&xdr, res->fattr, res->server
			,!RPC_IS_ASYNC(rqstp->rq_task));
out:
	return status;
}

/*
 * Decode LOOKUP_ROOT response
 */
static int nfs4_xdr_dec_lookup_root(struct rpc_rqst *rqstp, __be32 *p, struct nfs4_lookup_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	if ((status = decode_putrootfh(&xdr)) != 0)
		goto out;
	if ((status = decode_getfh(&xdr, res->fh)) == 0)
		status = decode_getfattr(&xdr, res->fattr, res->server,
				!RPC_IS_ASYNC(rqstp->rq_task));
out:
	return status;
}

/*
 * Decode REMOVE response
 */
static int nfs4_xdr_dec_remove(struct rpc_rqst *rqstp, __be32 *p, struct nfs_removeres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	if ((status = decode_putfh(&xdr)) != 0)
		goto out;
	if ((status = decode_remove(&xdr, &res->cinfo)) != 0)
		goto out;
	decode_getfattr(&xdr, res->dir_attr, res->server,
			!RPC_IS_ASYNC(rqstp->rq_task));
out:
	return status;
}

/*
 * Decode RENAME response
 */
static int nfs4_xdr_dec_rename(struct rpc_rqst *rqstp, __be32 *p, struct nfs4_rename_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	if ((status = decode_putfh(&xdr)) != 0)
		goto out;
	if ((status = decode_savefh(&xdr)) != 0)
		goto out;
	if ((status = decode_putfh(&xdr)) != 0)
		goto out;
	if ((status = decode_rename(&xdr, &res->old_cinfo, &res->new_cinfo)) != 0)
		goto out;
	/* Current FH is target directory */
	if (decode_getfattr(&xdr, res->new_fattr, res->server,
				!RPC_IS_ASYNC(rqstp->rq_task)) != 0)
		goto out;
	if ((status = decode_restorefh(&xdr)) != 0)
		goto out;
	decode_getfattr(&xdr, res->old_fattr, res->server,
			!RPC_IS_ASYNC(rqstp->rq_task));
out:
	return status;
}

/*
 * Decode LINK response
 */
static int nfs4_xdr_dec_link(struct rpc_rqst *rqstp, __be32 *p, struct nfs4_link_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	if ((status = decode_putfh(&xdr)) != 0)
		goto out;
	if ((status = decode_savefh(&xdr)) != 0)
		goto out;
	if ((status = decode_putfh(&xdr)) != 0)
		goto out;
	if ((status = decode_link(&xdr, &res->cinfo)) != 0)
		goto out;
	/*
	 * Note order: OP_LINK leaves the directory as the current
	 *             filehandle.
	 */
	if (decode_getfattr(&xdr, res->dir_attr, res->server,
				!RPC_IS_ASYNC(rqstp->rq_task)) != 0)
		goto out;
	if ((status = decode_restorefh(&xdr)) != 0)
		goto out;
	decode_getfattr(&xdr, res->fattr, res->server,
			!RPC_IS_ASYNC(rqstp->rq_task));
out:
	return status;
}

/*
 * Decode CREATE response
 */
static int nfs4_xdr_dec_create(struct rpc_rqst *rqstp, __be32 *p, struct nfs4_create_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	if ((status = decode_putfh(&xdr)) != 0)
		goto out;
	if ((status = decode_savefh(&xdr)) != 0)
		goto out;
	if ((status = decode_create(&xdr,&res->dir_cinfo)) != 0)
		goto out;
	if ((status = decode_getfh(&xdr, res->fh)) != 0)
		goto out;
	if (decode_getfattr(&xdr, res->fattr, res->server,
				!RPC_IS_ASYNC(rqstp->rq_task)) != 0)
		goto out;
	if ((status = decode_restorefh(&xdr)) != 0)
		goto out;
	decode_getfattr(&xdr, res->dir_fattr, res->server,
			!RPC_IS_ASYNC(rqstp->rq_task));
out:
	return status;
}

/*
 * Decode SYMLINK response
 */
static int nfs4_xdr_dec_symlink(struct rpc_rqst *rqstp, __be32 *p, struct nfs4_create_res *res)
{
	return nfs4_xdr_dec_create(rqstp, p, res);
}

/*
 * Decode GETATTR response
 */
static int nfs4_xdr_dec_getattr(struct rpc_rqst *rqstp, __be32 *p, struct nfs4_getattr_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_getfattr(&xdr, res->fattr, res->server,
			!RPC_IS_ASYNC(rqstp->rq_task));
out:
	return status;
}

/*
 * Encode an SETACL request
 */
static int
nfs4_xdr_enc_setacl(struct rpc_rqst *req, __be32 *p, struct nfs_setaclargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, req, &hdr);
	encode_sequence(&xdr, &args->seq_args, &hdr);
	encode_putfh(&xdr, args->fh, &hdr);
	status = encode_setacl(&xdr, args, &hdr);
	encode_nops(&hdr);
	return status;
}

/*
 * Decode SETACL response
 */
static int
nfs4_xdr_dec_setacl(struct rpc_rqst *rqstp, __be32 *p,
		    struct nfs_setaclres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_setattr(&xdr);
out:
	return status;
}

/*
 * Decode GETACL response
 */
static int
nfs4_xdr_dec_getacl(struct rpc_rqst *rqstp, __be32 *p,
		    struct nfs_getaclres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_getacl(&xdr, rqstp, &res->acl_len);

out:
	return status;
}

/*
 * Decode CLOSE response
 */
static int nfs4_xdr_dec_close(struct rpc_rqst *rqstp, __be32 *p, struct nfs_closeres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_close(&xdr, res);
	if (status != 0)
		goto out;
	/*
	 * Note: Server may do delete on close for this file
	 * 	in which case the getattr call will fail with
	 * 	an ESTALE error. Shouldn't be a problem,
	 * 	though, since fattr->valid will remain unset.
	 */
	decode_getfattr(&xdr, res->fattr, res->server,
			!RPC_IS_ASYNC(rqstp->rq_task));
out:
	return status;
}

/*
 * Decode OPEN response
 */
static int nfs4_xdr_dec_open(struct rpc_rqst *rqstp, __be32 *p, struct nfs_openres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_savefh(&xdr);
	if (status)
		goto out;
	status = decode_open(&xdr, res);
	if (status)
		goto out;
	if (decode_getfh(&xdr, &res->fh) != 0)
		goto out;
	if (decode_getfattr(&xdr, res->f_attr, res->server,
				!RPC_IS_ASYNC(rqstp->rq_task)) != 0)
		goto out;
	if (decode_restorefh(&xdr) != 0)
		goto out;
	decode_getfattr(&xdr, res->dir_attr, res->server,
			!RPC_IS_ASYNC(rqstp->rq_task));
out:
	return status;
}

/*
 * Decode OPEN_CONFIRM response
 */
static int nfs4_xdr_dec_open_confirm(struct rpc_rqst *rqstp, __be32 *p, struct nfs_open_confirmres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_open_confirm(&xdr, res);
out:
	return status;
}

/*
 * Decode OPEN response
 */
static int nfs4_xdr_dec_open_noattr(struct rpc_rqst *rqstp, __be32 *p, struct nfs_openres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_open(&xdr, res);
	if (status)
		goto out;
	decode_getfattr(&xdr, res->f_attr, res->server,
			!RPC_IS_ASYNC(rqstp->rq_task));
out:
	return status;
}

/*
 * Decode SETATTR response
 */
static int nfs4_xdr_dec_setattr(struct rpc_rqst *rqstp, __be32 *p, struct nfs_setattrres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_setattr(&xdr);
	if (status)
		goto out;
	decode_getfattr(&xdr, res->fattr, res->server,
			!RPC_IS_ASYNC(rqstp->rq_task));
out:
	return status;
}

/*
 * Decode LOCK response
 */
static int nfs4_xdr_dec_lock(struct rpc_rqst *rqstp, __be32 *p, struct nfs_lock_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_lock(&xdr, res);
out:
	return status;
}

/*
 * Decode LOCKT response
 */
static int nfs4_xdr_dec_lockt(struct rpc_rqst *rqstp, __be32 *p, struct nfs_lockt_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_lockt(&xdr, res);
out:
	return status;
}

/*
 * Decode LOCKU response
 */
static int nfs4_xdr_dec_locku(struct rpc_rqst *rqstp, __be32 *p, struct nfs_locku_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_locku(&xdr, res);
out:
	return status;
}

/*
 * Decode READLINK response
 */
static int nfs4_xdr_dec_readlink(struct rpc_rqst *rqstp, __be32 *p,
				 struct nfs4_readlink_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_readlink(&xdr, rqstp);
out:
	return status;
}

/*
 * Decode READDIR response
 */
static int nfs4_xdr_dec_readdir(struct rpc_rqst *rqstp, __be32 *p, struct nfs4_readdir_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_readdir(&xdr, rqstp, res);
out:
	return status;
}

/*
 * Decode Read response
 */
static int nfs4_xdr_dec_read(struct rpc_rqst *rqstp, __be32 *p, struct nfs_readres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_read(&xdr, rqstp, res);
	if (!status)
		status = res->count;
out:
	return status;
}

/*
 * Decode WRITE response
 */
static int nfs4_xdr_dec_write(struct rpc_rqst *rqstp, __be32 *p, struct nfs_writeres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_write(&xdr, res);
	if (status)
		goto out;
	decode_getfattr(&xdr, res->fattr, res->server,
			!RPC_IS_ASYNC(rqstp->rq_task));
	if (!status)
		status = res->count;
out:
	return status;
}

/*
 * Decode COMMIT response
 */
static int nfs4_xdr_dec_commit(struct rpc_rqst *rqstp, __be32 *p, struct nfs_writeres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_commit(&xdr, res);
	if (status)
		goto out;
	decode_getfattr(&xdr, res->fattr, res->server,
			!RPC_IS_ASYNC(rqstp->rq_task));
out:
	return status;
}

/*
 * Decode FSINFO response
 */
static int nfs4_xdr_dec_fsinfo(struct rpc_rqst *req, __be32 *p,
			       struct nfs4_fsinfo_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &req->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_sequence(&xdr, &res->seq_res, req);
	if (!status)
		status = decode_putfh(&xdr);
	if (!status)
		status = decode_fsinfo(&xdr, res->fsinfo);
	return status;
}

/*
 * Decode PATHCONF response
 */
static int nfs4_xdr_dec_pathconf(struct rpc_rqst *req, __be32 *p,
				 struct nfs4_pathconf_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &req->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_sequence(&xdr, &res->seq_res, req);
	if (!status)
		status = decode_putfh(&xdr);
	if (!status)
		status = decode_pathconf(&xdr, res->pathconf);
	return status;
}

/*
 * Decode STATFS response
 */
static int nfs4_xdr_dec_statfs(struct rpc_rqst *req, __be32 *p,
			       struct nfs4_statfs_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &req->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_sequence(&xdr, &res->seq_res, req);
	if (!status)
		status = decode_putfh(&xdr);
	if (!status)
		status = decode_statfs(&xdr, res->fsstat);
	return status;
}

/*
 * Decode GETATTR_BITMAP response
 */
static int nfs4_xdr_dec_server_caps(struct rpc_rqst *req, __be32 *p, struct nfs4_server_caps_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &req->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res, req);
	if (status)
		goto out;
	if ((status = decode_putfh(&xdr)) != 0)
		goto out;
	status = decode_server_caps(&xdr, res);
out:
	return status;
}

/*
 * Decode RENEW response
 */
static int nfs4_xdr_dec_renew(struct rpc_rqst *rqstp, __be32 *p, void *dummy)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_renew(&xdr);
	return status;
}

/*
 * Decode SETCLIENTID response
 */
static int nfs4_xdr_dec_setclientid(struct rpc_rqst *req, __be32 *p,
		struct nfs4_setclientid_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &req->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_setclientid(&xdr, res);
	return status;
}

/*
 * Decode SETCLIENTID_CONFIRM response
 */
static int nfs4_xdr_dec_setclientid_confirm(struct rpc_rqst *req, __be32 *p, struct nfs_fsinfo *fsinfo)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &req->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_setclientid_confirm(&xdr);
	if (!status)
		status = decode_putrootfh(&xdr);
	if (!status)
		status = decode_fsinfo(&xdr, fsinfo);
	return status;
}

/*
 * Decode DELEGRETURN response
 */
static int nfs4_xdr_dec_delegreturn(struct rpc_rqst *rqstp, __be32 *p, struct nfs4_delegreturnres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status != 0)
		goto out;
	status = decode_delegreturn(&xdr);
	if (status != 0)
		goto out;
	decode_getfattr(&xdr, res->fattr, res->server,
			!RPC_IS_ASYNC(rqstp->rq_task));
out:
	return status;
}

/*
 * Decode FS_LOCATIONS response
 */
static int nfs4_xdr_dec_fs_locations(struct rpc_rqst *req, __be32 *p,
				     struct nfs4_fs_locations_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &req->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res, req);
	if (status)
		goto out;
	if ((status = decode_putfh(&xdr)) != 0)
		goto out;
	if ((status = decode_lookup(&xdr)) != 0)
		goto out;
	xdr_enter_page(&xdr, PAGE_SIZE);
	status = decode_getfattr(&xdr, &res->fs_locations->fattr,
				 res->fs_locations->server,
				 !RPC_IS_ASYNC(req->rq_task));
out:
	return status;
}

#if defined(CONFIG_NFS_V4_1)
/*
 * Decode EXCHANGE_ID response
 */
static int nfs4_xdr_dec_exchange_id(struct rpc_rqst *rqstp, uint32_t *p,
				    void *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_exchange_id(&xdr, res);
	return status;
}

/*
 * Decode CREATE_SESSION response
 */
static int nfs4_xdr_dec_create_session(struct rpc_rqst *rqstp, uint32_t *p,
				       struct nfs41_create_session_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_create_session(&xdr, res);
	return status;
}

/*
 * Decode DESTROY_SESSION response
 */
static int nfs4_xdr_dec_destroy_session(struct rpc_rqst *rqstp, uint32_t *p,
					void *dummy)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_destroy_session(&xdr, dummy);
	return status;
}

/*
 * Decode SEQUENCE response
 */
static int nfs4_xdr_dec_sequence(struct rpc_rqst *rqstp, uint32_t *p,
				 struct nfs4_sequence_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_sequence(&xdr, res, rqstp);
	return status;
}

/*
 * Decode GET_LEASE_TIME response
 */
static int nfs4_xdr_dec_get_lease_time(struct rpc_rqst *rqstp, uint32_t *p,
				       struct nfs4_get_lease_time_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_sequence(&xdr, &res->lr_seq_res, rqstp);
	if (!status)
		status = decode_putrootfh(&xdr);
	if (!status)
		status = decode_fsinfo(&xdr, res->lr_fsinfo);
	return status;
}

/*
 * Decode RECLAIM_COMPLETE response
 */
static int nfs4_xdr_dec_reclaim_complete(struct rpc_rqst *rqstp, uint32_t *p,
					 struct nfs41_reclaim_complete_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_sequence(&xdr, &res->seq_res, rqstp);
	if (!status)
		status = decode_reclaim_complete(&xdr, (void *)NULL);
	return status;
}
#endif /* CONFIG_NFS_V4_1 */

__be32 *nfs4_decode_dirent(__be32 *p, struct nfs_entry *entry, int plus)
{
	uint32_t bitmap[2] = {0};
	uint32_t len;

	if (!*p++) {
		if (!*p)
			return ERR_PTR(-EAGAIN);
		entry->eof = 1;
		return ERR_PTR(-EBADCOOKIE);
	}

	entry->prev_cookie = entry->cookie;
	p = xdr_decode_hyper(p, &entry->cookie);
	entry->len = ntohl(*p++);
	entry->name = (const char *) p;
	p += XDR_QUADLEN(entry->len);

	/*
	 * In case the server doesn't return an inode number,
	 * we fake one here.  (We don't use inode number 0,
	 * since glibc seems to choke on it...)
	 */
	entry->ino = 1;

	len = ntohl(*p++);		/* bitmap length */
	if (len-- > 0) {
		bitmap[0] = ntohl(*p++);
		if (len-- > 0) {
			bitmap[1] = ntohl(*p++);
			p += len;
		}
	}
	len = XDR_QUADLEN(ntohl(*p++));	/* attribute buffer length */
	if (len > 0) {
		if (bitmap[0] & FATTR4_WORD0_RDATTR_ERROR) {
			bitmap[0] &= ~FATTR4_WORD0_RDATTR_ERROR;
			/* Ignore the return value of rdattr_error for now */
			p++;
			len--;
		}
		if (bitmap[0] == 0 && bitmap[1] == FATTR4_WORD1_MOUNTED_ON_FILEID)
			xdr_decode_hyper(p, &entry->ino);
		else if (bitmap[0] == FATTR4_WORD0_FILEID)
			xdr_decode_hyper(p, &entry->ino);
		p += len;
	}

	entry->eof = !p[0] && p[1];
	return p;
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
	{ NFS4ERR_BADOWNER,	-EINVAL		},
	{ NFS4ERR_BADNAME,	-EINVAL		},
	{ NFS4ERR_BAD_COOKIE,	-EBADCOOKIE	},
	{ NFS4ERR_NOTSUPP,	-ENOTSUPP	},
	{ NFS4ERR_TOOSMALL,	-ETOOSMALL	},
	{ NFS4ERR_SERVERFAULT,	-EREMOTEIO	},
	{ NFS4ERR_BADTYPE,	-EBADTYPE	},
	{ NFS4ERR_LOCKED,	-EAGAIN		},
	{ NFS4ERR_SYMLINK,	-ELOOP		},
	{ NFS4ERR_OP_ILLEGAL,	-EOPNOTSUPP	},
	{ NFS4ERR_DEADLOCK,	-EDEADLK	},
	{ NFS4ERR_WRONGSEC,	-EPERM		}, /* FIXME: this needs
						    * to be handled by a
						    * middle-layer.
						    */
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

#define PROC(proc, argtype, restype)				\
[NFSPROC4_CLNT_##proc] = {					\
	.p_proc   = NFSPROC4_COMPOUND,				\
	.p_encode = (kxdrproc_t) nfs4_xdr_##argtype,		\
	.p_decode = (kxdrproc_t) nfs4_xdr_##restype,		\
	.p_arglen = NFS4_##argtype##_sz,			\
	.p_replen = NFS4_##restype##_sz,			\
	.p_statidx = NFSPROC4_CLNT_##proc,			\
	.p_name   = #proc,					\
}

struct rpc_procinfo	nfs4_procedures[] = {
  PROC(READ,		enc_read,	dec_read),
  PROC(WRITE,		enc_write,	dec_write),
  PROC(COMMIT,		enc_commit,	dec_commit),
  PROC(OPEN,		enc_open,	dec_open),
  PROC(OPEN_CONFIRM,	enc_open_confirm,	dec_open_confirm),
  PROC(OPEN_NOATTR,	enc_open_noattr,	dec_open_noattr),
  PROC(OPEN_DOWNGRADE,	enc_open_downgrade,	dec_open_downgrade),
  PROC(CLOSE,		enc_close,	dec_close),
  PROC(SETATTR,		enc_setattr,	dec_setattr),
  PROC(FSINFO,		enc_fsinfo,	dec_fsinfo),
  PROC(RENEW,		enc_renew,	dec_renew),
  PROC(SETCLIENTID,	enc_setclientid,	dec_setclientid),
  PROC(SETCLIENTID_CONFIRM,	enc_setclientid_confirm,	dec_setclientid_confirm),
  PROC(LOCK,            enc_lock,       dec_lock),
  PROC(LOCKT,           enc_lockt,      dec_lockt),
  PROC(LOCKU,           enc_locku,      dec_locku),
  PROC(ACCESS,		enc_access,	dec_access),
  PROC(GETATTR,		enc_getattr,	dec_getattr),
  PROC(LOOKUP,		enc_lookup,	dec_lookup),
  PROC(LOOKUP_ROOT,	enc_lookup_root,	dec_lookup_root),
  PROC(REMOVE,		enc_remove,	dec_remove),
  PROC(RENAME,		enc_rename,	dec_rename),
  PROC(LINK,		enc_link,	dec_link),
  PROC(SYMLINK,		enc_symlink,	dec_symlink),
  PROC(CREATE,		enc_create,	dec_create),
  PROC(PATHCONF,	enc_pathconf,	dec_pathconf),
  PROC(STATFS,		enc_statfs,	dec_statfs),
  PROC(READLINK,	enc_readlink,	dec_readlink),
  PROC(READDIR,		enc_readdir,	dec_readdir),
  PROC(SERVER_CAPS,	enc_server_caps, dec_server_caps),
  PROC(DELEGRETURN,	enc_delegreturn, dec_delegreturn),
  PROC(GETACL,		enc_getacl,	dec_getacl),
  PROC(SETACL,		enc_setacl,	dec_setacl),
  PROC(FS_LOCATIONS,	enc_fs_locations, dec_fs_locations),
#if defined(CONFIG_NFS_V4_1)
  PROC(EXCHANGE_ID,	enc_exchange_id,	dec_exchange_id),
  PROC(CREATE_SESSION,	enc_create_session,	dec_create_session),
  PROC(DESTROY_SESSION,	enc_destroy_session,	dec_destroy_session),
  PROC(SEQUENCE,	enc_sequence,	dec_sequence),
  PROC(GET_LEASE_TIME,	enc_get_lease_time,	dec_get_lease_time),
  PROC(RECLAIM_COMPLETE, enc_reclaim_complete,  dec_reclaim_complete),
#endif /* CONFIG_NFS_V4_1 */
};

struct rpc_version		nfs_version4 = {
	.number			= 4,
	.nrprocs		= ARRAY_SIZE(nfs4_procedures),
	.procs			= nfs4_procedures
};

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */
