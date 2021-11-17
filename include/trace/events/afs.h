/* SPDX-License-Identifier: GPL-2.0-or-later */
/* AFS tracepoints
 *
 * Copyright (C) 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM afs

#if !defined(_TRACE_AFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_AFS_H

#include <linux/tracepoint.h>

/*
 * Define enums for tracing information.
 */
#ifndef __AFS_DECLARE_TRACE_ENUMS_ONCE_ONLY
#define __AFS_DECLARE_TRACE_ENUMS_ONCE_ONLY

enum afs_call_trace {
	afs_call_trace_alloc,
	afs_call_trace_free,
	afs_call_trace_get,
	afs_call_trace_put,
	afs_call_trace_wake,
	afs_call_trace_work,
};

enum afs_server_trace {
	afs_server_trace_alloc,
	afs_server_trace_callback,
	afs_server_trace_destroy,
	afs_server_trace_free,
	afs_server_trace_gc,
	afs_server_trace_get_by_addr,
	afs_server_trace_get_by_uuid,
	afs_server_trace_get_caps,
	afs_server_trace_get_install,
	afs_server_trace_get_new_cbi,
	afs_server_trace_get_probe,
	afs_server_trace_give_up_cb,
	afs_server_trace_purging,
	afs_server_trace_put_call,
	afs_server_trace_put_cbi,
	afs_server_trace_put_find_rsq,
	afs_server_trace_put_probe,
	afs_server_trace_put_slist,
	afs_server_trace_put_slist_isort,
	afs_server_trace_put_uuid_rsq,
	afs_server_trace_update,
};


enum afs_volume_trace {
	afs_volume_trace_alloc,
	afs_volume_trace_free,
	afs_volume_trace_get_alloc_sbi,
	afs_volume_trace_get_cell_insert,
	afs_volume_trace_get_new_op,
	afs_volume_trace_get_query_alias,
	afs_volume_trace_put_cell_dup,
	afs_volume_trace_put_cell_root,
	afs_volume_trace_put_destroy_sbi,
	afs_volume_trace_put_free_fc,
	afs_volume_trace_put_put_op,
	afs_volume_trace_put_query_alias,
	afs_volume_trace_put_validate_fc,
	afs_volume_trace_remove,
};

enum afs_cell_trace {
	afs_cell_trace_alloc,
	afs_cell_trace_free,
	afs_cell_trace_get_queue_dns,
	afs_cell_trace_get_queue_manage,
	afs_cell_trace_get_queue_new,
	afs_cell_trace_get_vol,
	afs_cell_trace_insert,
	afs_cell_trace_manage,
	afs_cell_trace_put_candidate,
	afs_cell_trace_put_destroy,
	afs_cell_trace_put_queue_fail,
	afs_cell_trace_put_queue_work,
	afs_cell_trace_put_vol,
	afs_cell_trace_see_source,
	afs_cell_trace_see_ws,
	afs_cell_trace_unuse_alias,
	afs_cell_trace_unuse_check_alias,
	afs_cell_trace_unuse_delete,
	afs_cell_trace_unuse_fc,
	afs_cell_trace_unuse_lookup,
	afs_cell_trace_unuse_mntpt,
	afs_cell_trace_unuse_no_pin,
	afs_cell_trace_unuse_parse,
	afs_cell_trace_unuse_pin,
	afs_cell_trace_unuse_probe,
	afs_cell_trace_unuse_sbi,
	afs_cell_trace_unuse_ws,
	afs_cell_trace_use_alias,
	afs_cell_trace_use_check_alias,
	afs_cell_trace_use_fc,
	afs_cell_trace_use_fc_alias,
	afs_cell_trace_use_lookup,
	afs_cell_trace_use_mntpt,
	afs_cell_trace_use_pin,
	afs_cell_trace_use_probe,
	afs_cell_trace_use_sbi,
	afs_cell_trace_wait,
};

enum afs_fs_operation {
	afs_FS_FetchData		= 130,	/* AFS Fetch file data */
	afs_FS_FetchACL			= 131,	/* AFS Fetch file ACL */
	afs_FS_FetchStatus		= 132,	/* AFS Fetch file status */
	afs_FS_StoreData		= 133,	/* AFS Store file data */
	afs_FS_StoreACL			= 134,	/* AFS Store file ACL */
	afs_FS_StoreStatus		= 135,	/* AFS Store file status */
	afs_FS_RemoveFile		= 136,	/* AFS Remove a file */
	afs_FS_CreateFile		= 137,	/* AFS Create a file */
	afs_FS_Rename			= 138,	/* AFS Rename or move a file or directory */
	afs_FS_Symlink			= 139,	/* AFS Create a symbolic link */
	afs_FS_Link			= 140,	/* AFS Create a hard link */
	afs_FS_MakeDir			= 141,	/* AFS Create a directory */
	afs_FS_RemoveDir		= 142,	/* AFS Remove a directory */
	afs_FS_GetVolumeInfo		= 148,	/* AFS Get information about a volume */
	afs_FS_GetVolumeStatus		= 149,	/* AFS Get volume status information */
	afs_FS_GetRootVolume		= 151,	/* AFS Get root volume name */
	afs_FS_SetLock			= 156,	/* AFS Request a file lock */
	afs_FS_ExtendLock		= 157,	/* AFS Extend a file lock */
	afs_FS_ReleaseLock		= 158,	/* AFS Release a file lock */
	afs_FS_Lookup			= 161,	/* AFS lookup file in directory */
	afs_FS_InlineBulkStatus		= 65536, /* AFS Fetch multiple file statuses with errors */
	afs_FS_FetchData64		= 65537, /* AFS Fetch file data */
	afs_FS_StoreData64		= 65538, /* AFS Store file data */
	afs_FS_GiveUpAllCallBacks	= 65539, /* AFS Give up all our callbacks on a server */
	afs_FS_GetCapabilities		= 65540, /* AFS Get FS server capabilities */

	yfs_FS_FetchData		= 130,	 /* YFS Fetch file data */
	yfs_FS_FetchACL			= 64131, /* YFS Fetch file ACL */
	yfs_FS_FetchStatus		= 64132, /* YFS Fetch file status */
	yfs_FS_StoreACL			= 64134, /* YFS Store file ACL */
	yfs_FS_StoreStatus		= 64135, /* YFS Store file status */
	yfs_FS_RemoveFile		= 64136, /* YFS Remove a file */
	yfs_FS_CreateFile		= 64137, /* YFS Create a file */
	yfs_FS_Rename			= 64138, /* YFS Rename or move a file or directory */
	yfs_FS_Symlink			= 64139, /* YFS Create a symbolic link */
	yfs_FS_Link			= 64140, /* YFS Create a hard link */
	yfs_FS_MakeDir			= 64141, /* YFS Create a directory */
	yfs_FS_RemoveDir		= 64142, /* YFS Remove a directory */
	yfs_FS_GetVolumeStatus		= 64149, /* YFS Get volume status information */
	yfs_FS_SetVolumeStatus		= 64150, /* YFS Set volume status information */
	yfs_FS_SetLock			= 64156, /* YFS Request a file lock */
	yfs_FS_ExtendLock		= 64157, /* YFS Extend a file lock */
	yfs_FS_ReleaseLock		= 64158, /* YFS Release a file lock */
	yfs_FS_Lookup			= 64161, /* YFS lookup file in directory */
	yfs_FS_FlushCPS			= 64165,
	yfs_FS_FetchOpaqueACL		= 64168,
	yfs_FS_WhoAmI			= 64170,
	yfs_FS_RemoveACL		= 64171,
	yfs_FS_RemoveFile2		= 64173,
	yfs_FS_StoreOpaqueACL2		= 64174,
	yfs_FS_InlineBulkStatus		= 64536, /* YFS Fetch multiple file statuses with errors */
	yfs_FS_FetchData64		= 64537, /* YFS Fetch file data */
	yfs_FS_StoreData64		= 64538, /* YFS Store file data */
	yfs_FS_UpdateSymlink		= 64540,
};

enum afs_vl_operation {
	afs_VL_GetEntryByNameU	= 527,		/* AFS Get Vol Entry By Name operation ID */
	afs_VL_GetAddrsU	= 533,		/* AFS Get FS server addresses */
	afs_YFSVL_GetEndpoints	= 64002,	/* YFS Get FS & Vol server addresses */
	afs_YFSVL_GetCellName	= 64014,	/* YFS Get actual cell name */
	afs_VL_GetCapabilities	= 65537,	/* AFS Get VL server capabilities */
};

enum afs_cm_operation {
	afs_CB_CallBack			= 204,	/* AFS break callback promises */
	afs_CB_InitCallBackState	= 205,	/* AFS initialise callback state */
	afs_CB_Probe			= 206,	/* AFS probe client */
	afs_CB_GetLock			= 207,	/* AFS get contents of CM lock table */
	afs_CB_GetCE			= 208,	/* AFS get cache file description */
	afs_CB_GetXStatsVersion		= 209,	/* AFS get version of extended statistics */
	afs_CB_GetXStats		= 210,	/* AFS get contents of extended statistics data */
	afs_CB_InitCallBackState3	= 213,	/* AFS initialise callback state, version 3 */
	afs_CB_ProbeUuid		= 214,	/* AFS check the client hasn't rebooted */
};

enum yfs_cm_operation {
	yfs_CB_Probe			= 206,	/* YFS probe client */
	yfs_CB_GetLock			= 207,	/* YFS get contents of CM lock table */
	yfs_CB_XStatsVersion		= 209,	/* YFS get version of extended statistics */
	yfs_CB_GetXStats		= 210,	/* YFS get contents of extended statistics data */
	yfs_CB_InitCallBackState3	= 213,	/* YFS initialise callback state, version 3 */
	yfs_CB_ProbeUuid		= 214,	/* YFS check the client hasn't rebooted */
	yfs_CB_GetServerPrefs		= 215,
	yfs_CB_GetCellServDV		= 216,
	yfs_CB_GetLocalCell		= 217,
	yfs_CB_GetCacheConfig		= 218,
	yfs_CB_GetCellByNum		= 65537,
	yfs_CB_TellMeAboutYourself	= 65538, /* get client capabilities */
	yfs_CB_CallBack			= 64204,
};

enum afs_edit_dir_op {
	afs_edit_dir_create,
	afs_edit_dir_create_error,
	afs_edit_dir_create_inval,
	afs_edit_dir_create_nospc,
	afs_edit_dir_delete,
	afs_edit_dir_delete_error,
	afs_edit_dir_delete_inval,
	afs_edit_dir_delete_noent,
};

enum afs_edit_dir_reason {
	afs_edit_dir_for_create,
	afs_edit_dir_for_link,
	afs_edit_dir_for_mkdir,
	afs_edit_dir_for_rename_0,
	afs_edit_dir_for_rename_1,
	afs_edit_dir_for_rename_2,
	afs_edit_dir_for_rmdir,
	afs_edit_dir_for_silly_0,
	afs_edit_dir_for_silly_1,
	afs_edit_dir_for_symlink,
	afs_edit_dir_for_unlink,
};

enum afs_eproto_cause {
	afs_eproto_bad_status,
	afs_eproto_cb_count,
	afs_eproto_cb_fid_count,
	afs_eproto_cellname_len,
	afs_eproto_file_type,
	afs_eproto_ibulkst_cb_count,
	afs_eproto_ibulkst_count,
	afs_eproto_motd_len,
	afs_eproto_offline_msg_len,
	afs_eproto_volname_len,
	afs_eproto_yvl_fsendpt4_len,
	afs_eproto_yvl_fsendpt6_len,
	afs_eproto_yvl_fsendpt_num,
	afs_eproto_yvl_fsendpt_type,
	afs_eproto_yvl_vlendpt4_len,
	afs_eproto_yvl_vlendpt6_len,
	afs_eproto_yvl_vlendpt_type,
};

enum afs_io_error {
	afs_io_error_cm_reply,
	afs_io_error_extract,
	afs_io_error_fs_probe_fail,
	afs_io_error_vl_lookup_fail,
	afs_io_error_vl_probe_fail,
};

enum afs_file_error {
	afs_file_error_dir_bad_magic,
	afs_file_error_dir_big,
	afs_file_error_dir_missing_page,
	afs_file_error_dir_name_too_long,
	afs_file_error_dir_over_end,
	afs_file_error_dir_small,
	afs_file_error_dir_unmarked_ext,
	afs_file_error_mntpt,
	afs_file_error_writeback_fail,
};

enum afs_flock_event {
	afs_flock_acquired,
	afs_flock_callback_break,
	afs_flock_defer_unlock,
	afs_flock_extend_fail,
	afs_flock_fail_other,
	afs_flock_fail_perm,
	afs_flock_no_lockers,
	afs_flock_release_fail,
	afs_flock_silly_delete,
	afs_flock_timestamp,
	afs_flock_try_to_lock,
	afs_flock_vfs_lock,
	afs_flock_vfs_locking,
	afs_flock_waited,
	afs_flock_waiting,
	afs_flock_work_extending,
	afs_flock_work_retry,
	afs_flock_work_unlocking,
	afs_flock_would_block,
};

enum afs_flock_operation {
	afs_flock_op_copy_lock,
	afs_flock_op_flock,
	afs_flock_op_grant,
	afs_flock_op_lock,
	afs_flock_op_release_lock,
	afs_flock_op_return_ok,
	afs_flock_op_return_eagain,
	afs_flock_op_return_edeadlk,
	afs_flock_op_return_error,
	afs_flock_op_set_lock,
	afs_flock_op_unlock,
	afs_flock_op_wake,
};

enum afs_cb_break_reason {
	afs_cb_break_no_break,
	afs_cb_break_no_promise,
	afs_cb_break_for_callback,
	afs_cb_break_for_deleted,
	afs_cb_break_for_lapsed,
	afs_cb_break_for_s_reinit,
	afs_cb_break_for_unlink,
	afs_cb_break_for_v_break,
	afs_cb_break_for_volume_callback,
	afs_cb_break_for_zap,
};

#endif /* end __AFS_DECLARE_TRACE_ENUMS_ONCE_ONLY */

/*
 * Declare tracing information enums and their string mappings for display.
 */
#define afs_call_traces \
	EM(afs_call_trace_alloc,		"ALLOC") \
	EM(afs_call_trace_free,			"FREE ") \
	EM(afs_call_trace_get,			"GET  ") \
	EM(afs_call_trace_put,			"PUT  ") \
	EM(afs_call_trace_wake,			"WAKE ") \
	E_(afs_call_trace_work,			"QUEUE")

#define afs_server_traces \
	EM(afs_server_trace_alloc,		"ALLOC    ") \
	EM(afs_server_trace_callback,		"CALLBACK ") \
	EM(afs_server_trace_destroy,		"DESTROY  ") \
	EM(afs_server_trace_free,		"FREE     ") \
	EM(afs_server_trace_gc,			"GC       ") \
	EM(afs_server_trace_get_by_addr,	"GET addr ") \
	EM(afs_server_trace_get_by_uuid,	"GET uuid ") \
	EM(afs_server_trace_get_caps,		"GET caps ") \
	EM(afs_server_trace_get_install,	"GET inst ") \
	EM(afs_server_trace_get_new_cbi,	"GET cbi  ") \
	EM(afs_server_trace_get_probe,		"GET probe") \
	EM(afs_server_trace_give_up_cb,		"giveup-cb") \
	EM(afs_server_trace_purging,		"PURGE    ") \
	EM(afs_server_trace_put_call,		"PUT call ") \
	EM(afs_server_trace_put_cbi,		"PUT cbi  ") \
	EM(afs_server_trace_put_find_rsq,	"PUT f-rsq") \
	EM(afs_server_trace_put_probe,		"PUT probe") \
	EM(afs_server_trace_put_slist,		"PUT slist") \
	EM(afs_server_trace_put_slist_isort,	"PUT isort") \
	EM(afs_server_trace_put_uuid_rsq,	"PUT u-req") \
	E_(afs_server_trace_update,		"UPDATE")

#define afs_volume_traces \
	EM(afs_volume_trace_alloc,		"ALLOC         ") \
	EM(afs_volume_trace_free,		"FREE          ") \
	EM(afs_volume_trace_get_alloc_sbi,	"GET sbi-alloc ") \
	EM(afs_volume_trace_get_cell_insert,	"GET cell-insrt") \
	EM(afs_volume_trace_get_new_op,		"GET op-new    ") \
	EM(afs_volume_trace_get_query_alias,	"GET cell-alias") \
	EM(afs_volume_trace_put_cell_dup,	"PUT cell-dup  ") \
	EM(afs_volume_trace_put_cell_root,	"PUT cell-root ") \
	EM(afs_volume_trace_put_destroy_sbi,	"PUT sbi-destry") \
	EM(afs_volume_trace_put_free_fc,	"PUT fc-free   ") \
	EM(afs_volume_trace_put_put_op,		"PUT op-put    ") \
	EM(afs_volume_trace_put_query_alias,	"PUT cell-alias") \
	EM(afs_volume_trace_put_validate_fc,	"PUT fc-validat") \
	E_(afs_volume_trace_remove,		"REMOVE        ")

#define afs_cell_traces \
	EM(afs_cell_trace_alloc,		"ALLOC     ") \
	EM(afs_cell_trace_free,			"FREE      ") \
	EM(afs_cell_trace_get_queue_dns,	"GET q-dns ") \
	EM(afs_cell_trace_get_queue_manage,	"GET q-mng ") \
	EM(afs_cell_trace_get_queue_new,	"GET q-new ") \
	EM(afs_cell_trace_get_vol,		"GET vol   ") \
	EM(afs_cell_trace_insert,		"INSERT    ") \
	EM(afs_cell_trace_manage,		"MANAGE    ") \
	EM(afs_cell_trace_put_candidate,	"PUT candid") \
	EM(afs_cell_trace_put_destroy,		"PUT destry") \
	EM(afs_cell_trace_put_queue_work,	"PUT q-work") \
	EM(afs_cell_trace_put_queue_fail,	"PUT q-fail") \
	EM(afs_cell_trace_put_vol,		"PUT vol   ") \
	EM(afs_cell_trace_see_source,		"SEE source") \
	EM(afs_cell_trace_see_ws,		"SEE ws    ") \
	EM(afs_cell_trace_unuse_alias,		"UNU alias ") \
	EM(afs_cell_trace_unuse_check_alias,	"UNU chk-al") \
	EM(afs_cell_trace_unuse_delete,		"UNU delete") \
	EM(afs_cell_trace_unuse_fc,		"UNU fc    ") \
	EM(afs_cell_trace_unuse_lookup,		"UNU lookup") \
	EM(afs_cell_trace_unuse_mntpt,		"UNU mntpt ") \
	EM(afs_cell_trace_unuse_parse,		"UNU parse ") \
	EM(afs_cell_trace_unuse_pin,		"UNU pin   ") \
	EM(afs_cell_trace_unuse_probe,		"UNU probe ") \
	EM(afs_cell_trace_unuse_sbi,		"UNU sbi   ") \
	EM(afs_cell_trace_unuse_ws,		"UNU ws    ") \
	EM(afs_cell_trace_use_alias,		"USE alias ") \
	EM(afs_cell_trace_use_check_alias,	"USE chk-al") \
	EM(afs_cell_trace_use_fc,		"USE fc    ") \
	EM(afs_cell_trace_use_fc_alias,		"USE fc-al ") \
	EM(afs_cell_trace_use_lookup,		"USE lookup") \
	EM(afs_cell_trace_use_mntpt,		"USE mntpt ") \
	EM(afs_cell_trace_use_pin,		"USE pin   ") \
	EM(afs_cell_trace_use_probe,		"USE probe ") \
	EM(afs_cell_trace_use_sbi,		"USE sbi   ") \
	E_(afs_cell_trace_wait,			"WAIT      ")

#define afs_fs_operations \
	EM(afs_FS_FetchData,			"FS.FetchData") \
	EM(afs_FS_FetchStatus,			"FS.FetchStatus") \
	EM(afs_FS_StoreData,			"FS.StoreData") \
	EM(afs_FS_StoreStatus,			"FS.StoreStatus") \
	EM(afs_FS_RemoveFile,			"FS.RemoveFile") \
	EM(afs_FS_CreateFile,			"FS.CreateFile") \
	EM(afs_FS_Rename,			"FS.Rename") \
	EM(afs_FS_Symlink,			"FS.Symlink") \
	EM(afs_FS_Link,				"FS.Link") \
	EM(afs_FS_MakeDir,			"FS.MakeDir") \
	EM(afs_FS_RemoveDir,			"FS.RemoveDir") \
	EM(afs_FS_GetVolumeInfo,		"FS.GetVolumeInfo") \
	EM(afs_FS_GetVolumeStatus,		"FS.GetVolumeStatus") \
	EM(afs_FS_GetRootVolume,		"FS.GetRootVolume") \
	EM(afs_FS_SetLock,			"FS.SetLock") \
	EM(afs_FS_ExtendLock,			"FS.ExtendLock") \
	EM(afs_FS_ReleaseLock,			"FS.ReleaseLock") \
	EM(afs_FS_Lookup,			"FS.Lookup") \
	EM(afs_FS_InlineBulkStatus,		"FS.InlineBulkStatus") \
	EM(afs_FS_FetchData64,			"FS.FetchData64") \
	EM(afs_FS_StoreData64,			"FS.StoreData64") \
	EM(afs_FS_GiveUpAllCallBacks,		"FS.GiveUpAllCallBacks") \
	EM(afs_FS_GetCapabilities,		"FS.GetCapabilities") \
	EM(yfs_FS_FetchACL,			"YFS.FetchACL") \
	EM(yfs_FS_FetchStatus,			"YFS.FetchStatus") \
	EM(yfs_FS_StoreACL,			"YFS.StoreACL") \
	EM(yfs_FS_StoreStatus,			"YFS.StoreStatus") \
	EM(yfs_FS_RemoveFile,			"YFS.RemoveFile") \
	EM(yfs_FS_CreateFile,			"YFS.CreateFile") \
	EM(yfs_FS_Rename,			"YFS.Rename") \
	EM(yfs_FS_Symlink,			"YFS.Symlink") \
	EM(yfs_FS_Link,				"YFS.Link") \
	EM(yfs_FS_MakeDir,			"YFS.MakeDir") \
	EM(yfs_FS_RemoveDir,			"YFS.RemoveDir") \
	EM(yfs_FS_GetVolumeStatus,		"YFS.GetVolumeStatus") \
	EM(yfs_FS_SetVolumeStatus,		"YFS.SetVolumeStatus") \
	EM(yfs_FS_SetLock,			"YFS.SetLock") \
	EM(yfs_FS_ExtendLock,			"YFS.ExtendLock") \
	EM(yfs_FS_ReleaseLock,			"YFS.ReleaseLock") \
	EM(yfs_FS_Lookup,			"YFS.Lookup") \
	EM(yfs_FS_FlushCPS,			"YFS.FlushCPS") \
	EM(yfs_FS_FetchOpaqueACL,		"YFS.FetchOpaqueACL") \
	EM(yfs_FS_WhoAmI,			"YFS.WhoAmI") \
	EM(yfs_FS_RemoveACL,			"YFS.RemoveACL") \
	EM(yfs_FS_RemoveFile2,			"YFS.RemoveFile2") \
	EM(yfs_FS_StoreOpaqueACL2,		"YFS.StoreOpaqueACL2") \
	EM(yfs_FS_InlineBulkStatus,		"YFS.InlineBulkStatus") \
	EM(yfs_FS_FetchData64,			"YFS.FetchData64") \
	EM(yfs_FS_StoreData64,			"YFS.StoreData64") \
	E_(yfs_FS_UpdateSymlink,		"YFS.UpdateSymlink")

#define afs_vl_operations \
	EM(afs_VL_GetEntryByNameU,		"VL.GetEntryByNameU") \
	EM(afs_VL_GetAddrsU,			"VL.GetAddrsU") \
	EM(afs_YFSVL_GetEndpoints,		"YFSVL.GetEndpoints") \
	EM(afs_YFSVL_GetCellName,		"YFSVL.GetCellName") \
	E_(afs_VL_GetCapabilities,		"VL.GetCapabilities")

#define afs_cm_operations \
	EM(afs_CB_CallBack,			"CB.CallBack") \
	EM(afs_CB_InitCallBackState,		"CB.InitCallBackState") \
	EM(afs_CB_Probe,			"CB.Probe") \
	EM(afs_CB_GetLock,			"CB.GetLock") \
	EM(afs_CB_GetCE,			"CB.GetCE") \
	EM(afs_CB_GetXStatsVersion,		"CB.GetXStatsVersion") \
	EM(afs_CB_GetXStats,			"CB.GetXStats") \
	EM(afs_CB_InitCallBackState3,		"CB.InitCallBackState3") \
	E_(afs_CB_ProbeUuid,			"CB.ProbeUuid")

#define yfs_cm_operations \
	EM(yfs_CB_Probe,			"YFSCB.Probe") \
	EM(yfs_CB_GetLock,			"YFSCB.GetLock") \
	EM(yfs_CB_XStatsVersion,		"YFSCB.XStatsVersion") \
	EM(yfs_CB_GetXStats,			"YFSCB.GetXStats") \
	EM(yfs_CB_InitCallBackState3,		"YFSCB.InitCallBackState3") \
	EM(yfs_CB_ProbeUuid,			"YFSCB.ProbeUuid") \
	EM(yfs_CB_GetServerPrefs,		"YFSCB.GetServerPrefs") \
	EM(yfs_CB_GetCellServDV,		"YFSCB.GetCellServDV") \
	EM(yfs_CB_GetLocalCell,			"YFSCB.GetLocalCell") \
	EM(yfs_CB_GetCacheConfig,		"YFSCB.GetCacheConfig") \
	EM(yfs_CB_GetCellByNum,			"YFSCB.GetCellByNum") \
	EM(yfs_CB_TellMeAboutYourself,		"YFSCB.TellMeAboutYourself") \
	E_(yfs_CB_CallBack,			"YFSCB.CallBack")

#define afs_edit_dir_ops				  \
	EM(afs_edit_dir_create,			"create") \
	EM(afs_edit_dir_create_error,		"c_fail") \
	EM(afs_edit_dir_create_inval,		"c_invl") \
	EM(afs_edit_dir_create_nospc,		"c_nspc") \
	EM(afs_edit_dir_delete,			"delete") \
	EM(afs_edit_dir_delete_error,		"d_err ") \
	EM(afs_edit_dir_delete_inval,		"d_invl") \
	E_(afs_edit_dir_delete_noent,		"d_nent")

#define afs_edit_dir_reasons				  \
	EM(afs_edit_dir_for_create,		"Create") \
	EM(afs_edit_dir_for_link,		"Link  ") \
	EM(afs_edit_dir_for_mkdir,		"MkDir ") \
	EM(afs_edit_dir_for_rename_0,		"Renam0") \
	EM(afs_edit_dir_for_rename_1,		"Renam1") \
	EM(afs_edit_dir_for_rename_2,		"Renam2") \
	EM(afs_edit_dir_for_rmdir,		"RmDir ") \
	EM(afs_edit_dir_for_silly_0,		"S_Ren0") \
	EM(afs_edit_dir_for_silly_1,		"S_Ren1") \
	EM(afs_edit_dir_for_symlink,		"Symlnk") \
	E_(afs_edit_dir_for_unlink,		"Unlink")

#define afs_eproto_causes			\
	EM(afs_eproto_bad_status,	"BadStatus") \
	EM(afs_eproto_cb_count,		"CbCount") \
	EM(afs_eproto_cb_fid_count,	"CbFidCount") \
	EM(afs_eproto_cellname_len,	"CellNameLen") \
	EM(afs_eproto_file_type,	"FileTYpe") \
	EM(afs_eproto_ibulkst_cb_count,	"IBS.CbCount") \
	EM(afs_eproto_ibulkst_count,	"IBS.FidCount") \
	EM(afs_eproto_motd_len,		"MotdLen") \
	EM(afs_eproto_offline_msg_len,	"OfflineMsgLen") \
	EM(afs_eproto_volname_len,	"VolNameLen") \
	EM(afs_eproto_yvl_fsendpt4_len,	"YVL.FsEnd4Len") \
	EM(afs_eproto_yvl_fsendpt6_len,	"YVL.FsEnd6Len") \
	EM(afs_eproto_yvl_fsendpt_num,	"YVL.FsEndCount") \
	EM(afs_eproto_yvl_fsendpt_type,	"YVL.FsEndType") \
	EM(afs_eproto_yvl_vlendpt4_len,	"YVL.VlEnd4Len") \
	EM(afs_eproto_yvl_vlendpt6_len,	"YVL.VlEnd6Len") \
	E_(afs_eproto_yvl_vlendpt_type,	"YVL.VlEndType")

#define afs_io_errors							\
	EM(afs_io_error_cm_reply,		"CM_REPLY")		\
	EM(afs_io_error_extract,		"EXTRACT")		\
	EM(afs_io_error_fs_probe_fail,		"FS_PROBE_FAIL")	\
	EM(afs_io_error_vl_lookup_fail,		"VL_LOOKUP_FAIL")	\
	E_(afs_io_error_vl_probe_fail,		"VL_PROBE_FAIL")

#define afs_file_errors							\
	EM(afs_file_error_dir_bad_magic,	"DIR_BAD_MAGIC")	\
	EM(afs_file_error_dir_big,		"DIR_BIG")		\
	EM(afs_file_error_dir_missing_page,	"DIR_MISSING_PAGE")	\
	EM(afs_file_error_dir_name_too_long,	"DIR_NAME_TOO_LONG")	\
	EM(afs_file_error_dir_over_end,		"DIR_ENT_OVER_END")	\
	EM(afs_file_error_dir_small,		"DIR_SMALL")		\
	EM(afs_file_error_dir_unmarked_ext,	"DIR_UNMARKED_EXT")	\
	EM(afs_file_error_mntpt,		"MNTPT_READ_FAILED")	\
	E_(afs_file_error_writeback_fail,	"WRITEBACK_FAILED")

#define afs_flock_types							\
	EM(F_RDLCK,				"RDLCK")		\
	EM(F_WRLCK,				"WRLCK")		\
	E_(F_UNLCK,				"UNLCK")

#define afs_flock_states						\
	EM(AFS_VNODE_LOCK_NONE,			"NONE")			\
	EM(AFS_VNODE_LOCK_WAITING_FOR_CB,	"WAIT_FOR_CB")		\
	EM(AFS_VNODE_LOCK_SETTING,		"SETTING")		\
	EM(AFS_VNODE_LOCK_GRANTED,		"GRANTED")		\
	EM(AFS_VNODE_LOCK_EXTENDING,		"EXTENDING")		\
	EM(AFS_VNODE_LOCK_NEED_UNLOCK,		"NEED_UNLOCK")		\
	EM(AFS_VNODE_LOCK_UNLOCKING,		"UNLOCKING")		\
	E_(AFS_VNODE_LOCK_DELETED,		"DELETED")

#define afs_flock_events						\
	EM(afs_flock_acquired,			"Acquired")		\
	EM(afs_flock_callback_break,		"Callback")		\
	EM(afs_flock_defer_unlock,		"D-Unlock")		\
	EM(afs_flock_extend_fail,		"Ext_Fail")		\
	EM(afs_flock_fail_other,		"ErrOther")		\
	EM(afs_flock_fail_perm,			"ErrPerm ")		\
	EM(afs_flock_no_lockers,		"NoLocker")		\
	EM(afs_flock_release_fail,		"Rel_Fail")		\
	EM(afs_flock_silly_delete,		"SillyDel")		\
	EM(afs_flock_timestamp,			"Timestmp")		\
	EM(afs_flock_try_to_lock,		"TryToLck")		\
	EM(afs_flock_vfs_lock,			"VFSLock ")		\
	EM(afs_flock_vfs_locking,		"VFSLking")		\
	EM(afs_flock_waited,			"Waited  ")		\
	EM(afs_flock_waiting,			"Waiting ")		\
	EM(afs_flock_work_extending,		"Extendng")		\
	EM(afs_flock_work_retry,		"Retry   ")		\
	EM(afs_flock_work_unlocking,		"Unlcking")		\
	E_(afs_flock_would_block,		"EWOULDBL")

#define afs_flock_operations						\
	EM(afs_flock_op_copy_lock,		"COPY    ")		\
	EM(afs_flock_op_flock,			"->flock ")		\
	EM(afs_flock_op_grant,			"GRANT   ")		\
	EM(afs_flock_op_lock,			"->lock  ")		\
	EM(afs_flock_op_release_lock,		"RELEASE ")		\
	EM(afs_flock_op_return_ok,		"<-OK    ")		\
	EM(afs_flock_op_return_edeadlk,		"<-EDEADL")		\
	EM(afs_flock_op_return_eagain,		"<-EAGAIN")		\
	EM(afs_flock_op_return_error,		"<-ERROR ")		\
	EM(afs_flock_op_set_lock,		"SET     ")		\
	EM(afs_flock_op_unlock,			"UNLOCK  ")		\
	E_(afs_flock_op_wake,			"WAKE    ")

#define afs_cb_break_reasons						\
	EM(afs_cb_break_no_break,		"no-break")		\
	EM(afs_cb_break_no_promise,		"no-promise")		\
	EM(afs_cb_break_for_callback,		"break-cb")		\
	EM(afs_cb_break_for_deleted,		"break-del")		\
	EM(afs_cb_break_for_lapsed,		"break-lapsed")		\
	EM(afs_cb_break_for_s_reinit,		"s-reinit")		\
	EM(afs_cb_break_for_unlink,		"break-unlink")		\
	EM(afs_cb_break_for_v_break,		"break-v")		\
	EM(afs_cb_break_for_volume_callback,	"break-v-cb")		\
	E_(afs_cb_break_for_zap,		"break-zap")

/*
 * Export enum symbols via userspace.
 */
#undef EM
#undef E_
#define EM(a, b) TRACE_DEFINE_ENUM(a);
#define E_(a, b) TRACE_DEFINE_ENUM(a);

afs_call_traces;
afs_server_traces;
afs_cell_traces;
afs_fs_operations;
afs_vl_operations;
afs_cm_operations;
yfs_cm_operations;
afs_edit_dir_ops;
afs_edit_dir_reasons;
afs_eproto_causes;
afs_io_errors;
afs_file_errors;
afs_flock_types;
afs_flock_operations;
afs_cb_break_reasons;

/*
 * Now redefine the EM() and E_() macros to map the enums to the strings that
 * will be printed in the output.
 */
#undef EM
#undef E_
#define EM(a, b)	{ a, b },
#define E_(a, b)	{ a, b }

TRACE_EVENT(afs_receive_data,
	    TP_PROTO(struct afs_call *call, struct iov_iter *iter,
		     bool want_more, int ret),

	    TP_ARGS(call, iter, want_more, ret),

	    TP_STRUCT__entry(
		    __field(loff_t,			remain		)
		    __field(unsigned int,		call		)
		    __field(enum afs_call_state,	state		)
		    __field(unsigned short,		unmarshall	)
		    __field(bool,			want_more	)
		    __field(int,			ret		)
			     ),

	    TP_fast_assign(
		    __entry->call	= call->debug_id;
		    __entry->state	= call->state;
		    __entry->unmarshall	= call->unmarshall;
		    __entry->remain	= iov_iter_count(iter);
		    __entry->want_more	= want_more;
		    __entry->ret	= ret;
			   ),

	    TP_printk("c=%08x r=%llu u=%u w=%u s=%u ret=%d",
		      __entry->call,
		      __entry->remain,
		      __entry->unmarshall,
		      __entry->want_more,
		      __entry->state,
		      __entry->ret)
	    );

TRACE_EVENT(afs_notify_call,
	    TP_PROTO(struct rxrpc_call *rxcall, struct afs_call *call),

	    TP_ARGS(rxcall, call),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call		)
		    __field(enum afs_call_state,	state		)
		    __field(unsigned short,		unmarshall	)
			     ),

	    TP_fast_assign(
		    __entry->call	= call->debug_id;
		    __entry->state	= call->state;
		    __entry->unmarshall	= call->unmarshall;
			   ),

	    TP_printk("c=%08x s=%u u=%u",
		      __entry->call,
		      __entry->state, __entry->unmarshall)
	    );

TRACE_EVENT(afs_cb_call,
	    TP_PROTO(struct afs_call *call),

	    TP_ARGS(call),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call		)
		    __field(u32,			op		)
		    __field(u16,			service_id	)
			     ),

	    TP_fast_assign(
		    __entry->call	= call->debug_id;
		    __entry->op		= call->operation_ID;
		    __entry->service_id	= call->service_id;
			   ),

	    TP_printk("c=%08x %s",
		      __entry->call,
		      __entry->service_id == 2501 ?
		      __print_symbolic(__entry->op, yfs_cm_operations) :
		      __print_symbolic(__entry->op, afs_cm_operations))
	    );

TRACE_EVENT(afs_call,
	    TP_PROTO(struct afs_call *call, enum afs_call_trace op,
		     int usage, int outstanding, const void *where),

	    TP_ARGS(call, op, usage, outstanding, where),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call		)
		    __field(int,			op		)
		    __field(int,			usage		)
		    __field(int,			outstanding	)
		    __field(const void *,		where		)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->op = op;
		    __entry->usage = usage;
		    __entry->outstanding = outstanding;
		    __entry->where = where;
			   ),

	    TP_printk("c=%08x %s u=%d o=%d sp=%pSR",
		      __entry->call,
		      __print_symbolic(__entry->op, afs_call_traces),
		      __entry->usage,
		      __entry->outstanding,
		      __entry->where)
	    );

TRACE_EVENT(afs_make_fs_call,
	    TP_PROTO(struct afs_call *call, const struct afs_fid *fid),

	    TP_ARGS(call, fid),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call		)
		    __field(enum afs_fs_operation,	op		)
		    __field_struct(struct afs_fid,	fid		)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->op = call->operation_ID;
		    if (fid) {
			    __entry->fid = *fid;
		    } else {
			    __entry->fid.vid = 0;
			    __entry->fid.vnode = 0;
			    __entry->fid.unique = 0;
		    }
			   ),

	    TP_printk("c=%08x %06llx:%06llx:%06x %s",
		      __entry->call,
		      __entry->fid.vid,
		      __entry->fid.vnode,
		      __entry->fid.unique,
		      __print_symbolic(__entry->op, afs_fs_operations))
	    );

TRACE_EVENT(afs_make_fs_calli,
	    TP_PROTO(struct afs_call *call, const struct afs_fid *fid,
		     unsigned int i),

	    TP_ARGS(call, fid, i),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call		)
		    __field(unsigned int,		i		)
		    __field(enum afs_fs_operation,	op		)
		    __field_struct(struct afs_fid,	fid		)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->i = i;
		    __entry->op = call->operation_ID;
		    if (fid) {
			    __entry->fid = *fid;
		    } else {
			    __entry->fid.vid = 0;
			    __entry->fid.vnode = 0;
			    __entry->fid.unique = 0;
		    }
			   ),

	    TP_printk("c=%08x %06llx:%06llx:%06x %s i=%u",
		      __entry->call,
		      __entry->fid.vid,
		      __entry->fid.vnode,
		      __entry->fid.unique,
		      __print_symbolic(__entry->op, afs_fs_operations),
		      __entry->i)
	    );

TRACE_EVENT(afs_make_fs_call1,
	    TP_PROTO(struct afs_call *call, const struct afs_fid *fid,
		     const struct qstr *name),

	    TP_ARGS(call, fid, name),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call		)
		    __field(enum afs_fs_operation,	op		)
		    __field_struct(struct afs_fid,	fid		)
		    __array(char,			name, 24	)
			     ),

	    TP_fast_assign(
		    unsigned int __len = min_t(unsigned int, name->len, 23);
		    __entry->call = call->debug_id;
		    __entry->op = call->operation_ID;
		    if (fid) {
			    __entry->fid = *fid;
		    } else {
			    __entry->fid.vid = 0;
			    __entry->fid.vnode = 0;
			    __entry->fid.unique = 0;
		    }
		    memcpy(__entry->name, name->name, __len);
		    __entry->name[__len] = 0;
			   ),

	    TP_printk("c=%08x %06llx:%06llx:%06x %s \"%s\"",
		      __entry->call,
		      __entry->fid.vid,
		      __entry->fid.vnode,
		      __entry->fid.unique,
		      __print_symbolic(__entry->op, afs_fs_operations),
		      __entry->name)
	    );

TRACE_EVENT(afs_make_fs_call2,
	    TP_PROTO(struct afs_call *call, const struct afs_fid *fid,
		     const struct qstr *name, const struct qstr *name2),

	    TP_ARGS(call, fid, name, name2),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call		)
		    __field(enum afs_fs_operation,	op		)
		    __field_struct(struct afs_fid,	fid		)
		    __array(char,			name, 24	)
		    __array(char,			name2, 24	)
			     ),

	    TP_fast_assign(
		    unsigned int __len = min_t(unsigned int, name->len, 23);
		    unsigned int __len2 = min_t(unsigned int, name2->len, 23);
		    __entry->call = call->debug_id;
		    __entry->op = call->operation_ID;
		    if (fid) {
			    __entry->fid = *fid;
		    } else {
			    __entry->fid.vid = 0;
			    __entry->fid.vnode = 0;
			    __entry->fid.unique = 0;
		    }
		    memcpy(__entry->name, name->name, __len);
		    __entry->name[__len] = 0;
		    memcpy(__entry->name2, name2->name, __len2);
		    __entry->name2[__len2] = 0;
			   ),

	    TP_printk("c=%08x %06llx:%06llx:%06x %s \"%s\" \"%s\"",
		      __entry->call,
		      __entry->fid.vid,
		      __entry->fid.vnode,
		      __entry->fid.unique,
		      __print_symbolic(__entry->op, afs_fs_operations),
		      __entry->name,
		      __entry->name2)
	    );

TRACE_EVENT(afs_make_vl_call,
	    TP_PROTO(struct afs_call *call),

	    TP_ARGS(call),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call		)
		    __field(enum afs_vl_operation,	op		)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->op = call->operation_ID;
			   ),

	    TP_printk("c=%08x %s",
		      __entry->call,
		      __print_symbolic(__entry->op, afs_vl_operations))
	    );

TRACE_EVENT(afs_call_done,
	    TP_PROTO(struct afs_call *call),

	    TP_ARGS(call),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call		)
		    __field(struct rxrpc_call *,	rx_call		)
		    __field(int,			ret		)
		    __field(u32,			abort_code	)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->rx_call = call->rxcall;
		    __entry->ret = call->error;
		    __entry->abort_code = call->abort_code;
			   ),

	    TP_printk("   c=%08x ret=%d ab=%d [%p]",
		      __entry->call,
		      __entry->ret,
		      __entry->abort_code,
		      __entry->rx_call)
	    );

TRACE_EVENT(afs_send_data,
	    TP_PROTO(struct afs_call *call, struct msghdr *msg),

	    TP_ARGS(call, msg),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call		)
		    __field(unsigned int,		flags		)
		    __field(loff_t,			offset		)
		    __field(loff_t,			count		)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->flags = msg->msg_flags;
		    __entry->offset = msg->msg_iter.xarray_start + msg->msg_iter.iov_offset;
		    __entry->count = iov_iter_count(&msg->msg_iter);
			   ),

	    TP_printk(" c=%08x o=%llx n=%llx f=%x",
		      __entry->call, __entry->offset, __entry->count,
		      __entry->flags)
	    );

TRACE_EVENT(afs_sent_data,
	    TP_PROTO(struct afs_call *call, struct msghdr *msg, int ret),

	    TP_ARGS(call, msg, ret),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call		)
		    __field(int,			ret		)
		    __field(loff_t,			offset		)
		    __field(loff_t,			count		)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->ret = ret;
		    __entry->offset = msg->msg_iter.xarray_start + msg->msg_iter.iov_offset;
		    __entry->count = iov_iter_count(&msg->msg_iter);
			   ),

	    TP_printk(" c=%08x o=%llx n=%llx r=%x",
		      __entry->call, __entry->offset, __entry->count,
		      __entry->ret)
	    );

TRACE_EVENT(afs_dir_check_failed,
	    TP_PROTO(struct afs_vnode *vnode, loff_t off, loff_t i_size),

	    TP_ARGS(vnode, off, i_size),

	    TP_STRUCT__entry(
		    __field(struct afs_vnode *,		vnode		)
		    __field(loff_t,			off		)
		    __field(loff_t,			i_size		)
			     ),

	    TP_fast_assign(
		    __entry->vnode = vnode;
		    __entry->off = off;
		    __entry->i_size = i_size;
			   ),

	    TP_printk("vn=%p %llx/%llx",
		      __entry->vnode, __entry->off, __entry->i_size)
	    );

TRACE_EVENT(afs_page_dirty,
	    TP_PROTO(struct afs_vnode *vnode, const char *where, struct page *page),

	    TP_ARGS(vnode, where, page),

	    TP_STRUCT__entry(
		    __field(struct afs_vnode *,		vnode		)
		    __field(const char *,		where		)
		    __field(pgoff_t,			page		)
		    __field(unsigned long,		from		)
		    __field(unsigned long,		to		)
			     ),

	    TP_fast_assign(
		    __entry->vnode = vnode;
		    __entry->where = where;
		    __entry->page = page->index;
		    __entry->from = afs_page_dirty_from(page, page->private);
		    __entry->to = afs_page_dirty_to(page, page->private);
		    __entry->to |= (afs_is_page_dirty_mmapped(page->private) ?
				    (1UL << (BITS_PER_LONG - 1)) : 0);
			   ),

	    TP_printk("vn=%p %lx %s %lx-%lx%s",
		      __entry->vnode, __entry->page, __entry->where,
		      __entry->from,
		      __entry->to & ~(1UL << (BITS_PER_LONG - 1)),
		      __entry->to & (1UL << (BITS_PER_LONG - 1)) ? " M" : "")
	    );

TRACE_EVENT(afs_call_state,
	    TP_PROTO(struct afs_call *call,
		     enum afs_call_state from,
		     enum afs_call_state to,
		     int ret, u32 remote_abort),

	    TP_ARGS(call, from, to, ret, remote_abort),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call		)
		    __field(enum afs_call_state,	from		)
		    __field(enum afs_call_state,	to		)
		    __field(int,			ret		)
		    __field(u32,			abort		)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->from = from;
		    __entry->to = to;
		    __entry->ret = ret;
		    __entry->abort = remote_abort;
			   ),

	    TP_printk("c=%08x %u->%u r=%d ab=%d",
		      __entry->call,
		      __entry->from, __entry->to,
		      __entry->ret, __entry->abort)
	    );

TRACE_EVENT(afs_lookup,
	    TP_PROTO(struct afs_vnode *dvnode, const struct qstr *name,
		     struct afs_fid *fid),

	    TP_ARGS(dvnode, name, fid),

	    TP_STRUCT__entry(
		    __field_struct(struct afs_fid,	dfid		)
		    __field_struct(struct afs_fid,	fid		)
		    __array(char,			name, 24	)
			     ),

	    TP_fast_assign(
		    int __len = min_t(int, name->len, 23);
		    __entry->dfid = dvnode->fid;
		    __entry->fid = *fid;
		    memcpy(__entry->name, name->name, __len);
		    __entry->name[__len] = 0;
			   ),

	    TP_printk("d=%llx:%llx:%x \"%s\" f=%llx:%x",
		      __entry->dfid.vid, __entry->dfid.vnode, __entry->dfid.unique,
		      __entry->name,
		      __entry->fid.vnode, __entry->fid.unique)
	    );

TRACE_EVENT(afs_edit_dir,
	    TP_PROTO(struct afs_vnode *dvnode,
		     enum afs_edit_dir_reason why,
		     enum afs_edit_dir_op op,
		     unsigned int block,
		     unsigned int slot,
		     unsigned int f_vnode,
		     unsigned int f_unique,
		     const char *name),

	    TP_ARGS(dvnode, why, op, block, slot, f_vnode, f_unique, name),

	    TP_STRUCT__entry(
		    __field(unsigned int,		vnode		)
		    __field(unsigned int,		unique		)
		    __field(enum afs_edit_dir_reason,	why		)
		    __field(enum afs_edit_dir_op,	op		)
		    __field(unsigned int,		block		)
		    __field(unsigned short,		slot		)
		    __field(unsigned int,		f_vnode		)
		    __field(unsigned int,		f_unique	)
		    __array(char,			name, 24	)
			     ),

	    TP_fast_assign(
		    int __len = strlen(name);
		    __len = min(__len, 23);
		    __entry->vnode	= dvnode->fid.vnode;
		    __entry->unique	= dvnode->fid.unique;
		    __entry->why	= why;
		    __entry->op		= op;
		    __entry->block	= block;
		    __entry->slot	= slot;
		    __entry->f_vnode	= f_vnode;
		    __entry->f_unique	= f_unique;
		    memcpy(__entry->name, name, __len);
		    __entry->name[__len] = 0;
			   ),

	    TP_printk("d=%x:%x %s %s %u[%u] f=%x:%x \"%s\"",
		      __entry->vnode, __entry->unique,
		      __print_symbolic(__entry->why, afs_edit_dir_reasons),
		      __print_symbolic(__entry->op, afs_edit_dir_ops),
		      __entry->block, __entry->slot,
		      __entry->f_vnode, __entry->f_unique,
		      __entry->name)
	    );

TRACE_EVENT(afs_protocol_error,
	    TP_PROTO(struct afs_call *call, enum afs_eproto_cause cause),

	    TP_ARGS(call, cause),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call		)
		    __field(enum afs_eproto_cause,	cause		)
			     ),

	    TP_fast_assign(
		    __entry->call = call ? call->debug_id : 0;
		    __entry->cause = cause;
			   ),

	    TP_printk("c=%08x %s",
		      __entry->call,
		      __print_symbolic(__entry->cause, afs_eproto_causes))
	    );

TRACE_EVENT(afs_io_error,
	    TP_PROTO(unsigned int call, int error, enum afs_io_error where),

	    TP_ARGS(call, error, where),

	    TP_STRUCT__entry(
		    __field(unsigned int,	call		)
		    __field(int,		error		)
		    __field(enum afs_io_error,	where		)
			     ),

	    TP_fast_assign(
		    __entry->call = call;
		    __entry->error = error;
		    __entry->where = where;
			   ),

	    TP_printk("c=%08x r=%d %s",
		      __entry->call, __entry->error,
		      __print_symbolic(__entry->where, afs_io_errors))
	    );

TRACE_EVENT(afs_file_error,
	    TP_PROTO(struct afs_vnode *vnode, int error, enum afs_file_error where),

	    TP_ARGS(vnode, error, where),

	    TP_STRUCT__entry(
		    __field_struct(struct afs_fid,	fid		)
		    __field(int,			error		)
		    __field(enum afs_file_error,	where		)
			     ),

	    TP_fast_assign(
		    __entry->fid = vnode->fid;
		    __entry->error = error;
		    __entry->where = where;
			   ),

	    TP_printk("%llx:%llx:%x r=%d %s",
		      __entry->fid.vid, __entry->fid.vnode, __entry->fid.unique,
		      __entry->error,
		      __print_symbolic(__entry->where, afs_file_errors))
	    );

TRACE_EVENT(afs_cm_no_server,
	    TP_PROTO(struct afs_call *call, struct sockaddr_rxrpc *srx),

	    TP_ARGS(call, srx),

	    TP_STRUCT__entry(
		    __field(unsigned int,			call	)
		    __field(unsigned int,			op_id	)
		    __field_struct(struct sockaddr_rxrpc,	srx	)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->op_id = call->operation_ID;
		    memcpy(&__entry->srx, srx, sizeof(__entry->srx));
			   ),

	    TP_printk("c=%08x op=%u %pISpc",
		      __entry->call, __entry->op_id, &__entry->srx.transport)
	    );

TRACE_EVENT(afs_cm_no_server_u,
	    TP_PROTO(struct afs_call *call, const uuid_t *uuid),

	    TP_ARGS(call, uuid),

	    TP_STRUCT__entry(
		    __field(unsigned int,			call	)
		    __field(unsigned int,			op_id	)
		    __field_struct(uuid_t,			uuid	)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->op_id = call->operation_ID;
		    memcpy(&__entry->uuid, uuid, sizeof(__entry->uuid));
			   ),

	    TP_printk("c=%08x op=%u %pU",
		      __entry->call, __entry->op_id, &__entry->uuid)
	    );

TRACE_EVENT(afs_flock_ev,
	    TP_PROTO(struct afs_vnode *vnode, struct file_lock *fl,
		     enum afs_flock_event event, int error),

	    TP_ARGS(vnode, fl, event, error),

	    TP_STRUCT__entry(
		    __field_struct(struct afs_fid,	fid		)
		    __field(enum afs_flock_event,	event		)
		    __field(enum afs_lock_state,	state		)
		    __field(int,			error		)
		    __field(unsigned int,		debug_id	)
			     ),

	    TP_fast_assign(
		    __entry->fid = vnode->fid;
		    __entry->event = event;
		    __entry->state = vnode->lock_state;
		    __entry->error = error;
		    __entry->debug_id = fl ? fl->fl_u.afs.debug_id : 0;
			   ),

	    TP_printk("%llx:%llx:%x %04x %s s=%s e=%d",
		      __entry->fid.vid, __entry->fid.vnode, __entry->fid.unique,
		      __entry->debug_id,
		      __print_symbolic(__entry->event, afs_flock_events),
		      __print_symbolic(__entry->state, afs_flock_states),
		      __entry->error)
	    );

TRACE_EVENT(afs_flock_op,
	    TP_PROTO(struct afs_vnode *vnode, struct file_lock *fl,
		     enum afs_flock_operation op),

	    TP_ARGS(vnode, fl, op),

	    TP_STRUCT__entry(
		    __field_struct(struct afs_fid,	fid		)
		    __field(loff_t,			from		)
		    __field(loff_t,			len		)
		    __field(enum afs_flock_operation,	op		)
		    __field(unsigned char,		type		)
		    __field(unsigned int,		flags		)
		    __field(unsigned int,		debug_id	)
			     ),

	    TP_fast_assign(
		    __entry->fid = vnode->fid;
		    __entry->from = fl->fl_start;
		    __entry->len = fl->fl_end - fl->fl_start + 1;
		    __entry->op = op;
		    __entry->type = fl->fl_type;
		    __entry->flags = fl->fl_flags;
		    __entry->debug_id = fl->fl_u.afs.debug_id;
			   ),

	    TP_printk("%llx:%llx:%x %04x %s t=%s R=%llx/%llx f=%x",
		      __entry->fid.vid, __entry->fid.vnode, __entry->fid.unique,
		      __entry->debug_id,
		      __print_symbolic(__entry->op, afs_flock_operations),
		      __print_symbolic(__entry->type, afs_flock_types),
		      __entry->from, __entry->len, __entry->flags)
	    );

TRACE_EVENT(afs_reload_dir,
	    TP_PROTO(struct afs_vnode *vnode),

	    TP_ARGS(vnode),

	    TP_STRUCT__entry(
		    __field_struct(struct afs_fid,	fid		)
			     ),

	    TP_fast_assign(
		    __entry->fid = vnode->fid;
			   ),

	    TP_printk("%llx:%llx:%x",
		      __entry->fid.vid, __entry->fid.vnode, __entry->fid.unique)
	    );

TRACE_EVENT(afs_silly_rename,
	    TP_PROTO(struct afs_vnode *vnode, bool done),

	    TP_ARGS(vnode, done),

	    TP_STRUCT__entry(
		    __field_struct(struct afs_fid,	fid		)
		    __field(bool,			done		)
			     ),

	    TP_fast_assign(
		    __entry->fid = vnode->fid;
		    __entry->done = done;
			   ),

	    TP_printk("%llx:%llx:%x done=%u",
		      __entry->fid.vid, __entry->fid.vnode, __entry->fid.unique,
		      __entry->done)
	    );

TRACE_EVENT(afs_get_tree,
	    TP_PROTO(struct afs_cell *cell, struct afs_volume *volume),

	    TP_ARGS(cell, volume),

	    TP_STRUCT__entry(
		    __field(u64,			vid		)
		    __array(char,			cell, 24	)
		    __array(char,			volume, 24	)
			     ),

	    TP_fast_assign(
		    int __len;
		    __entry->vid = volume->vid;
		    __len = min_t(int, cell->name_len, 23);
		    memcpy(__entry->cell, cell->name, __len);
		    __entry->cell[__len] = 0;
		    __len = min_t(int, volume->name_len, 23);
		    memcpy(__entry->volume, volume->name, __len);
		    __entry->volume[__len] = 0;
			   ),

	    TP_printk("--- MOUNT %s:%s %llx",
		      __entry->cell, __entry->volume, __entry->vid)
	    );

TRACE_EVENT(afs_cb_break,
	    TP_PROTO(struct afs_fid *fid, unsigned int cb_break,
		     enum afs_cb_break_reason reason, bool skipped),

	    TP_ARGS(fid, cb_break, reason, skipped),

	    TP_STRUCT__entry(
		    __field_struct(struct afs_fid,	fid		)
		    __field(unsigned int,		cb_break	)
		    __field(enum afs_cb_break_reason,	reason		)
		    __field(bool,			skipped		)
			     ),

	    TP_fast_assign(
		    __entry->fid	= *fid;
		    __entry->cb_break	= cb_break;
		    __entry->reason	= reason;
		    __entry->skipped	= skipped;
			   ),

	    TP_printk("%llx:%llx:%x b=%x s=%u %s",
		      __entry->fid.vid, __entry->fid.vnode, __entry->fid.unique,
		      __entry->cb_break,
		      __entry->skipped,
		      __print_symbolic(__entry->reason, afs_cb_break_reasons))
	    );

TRACE_EVENT(afs_cb_miss,
	    TP_PROTO(struct afs_fid *fid, enum afs_cb_break_reason reason),

	    TP_ARGS(fid, reason),

	    TP_STRUCT__entry(
		    __field_struct(struct afs_fid,	fid		)
		    __field(enum afs_cb_break_reason,	reason		)
			     ),

	    TP_fast_assign(
		    __entry->fid	= *fid;
		    __entry->reason	= reason;
			   ),

	    TP_printk(" %llx:%llx:%x %s",
		      __entry->fid.vid, __entry->fid.vnode, __entry->fid.unique,
		      __print_symbolic(__entry->reason, afs_cb_break_reasons))
	    );

TRACE_EVENT(afs_server,
	    TP_PROTO(struct afs_server *server, int ref, int active,
		     enum afs_server_trace reason),

	    TP_ARGS(server, ref, active, reason),

	    TP_STRUCT__entry(
		    __field(unsigned int,		server		)
		    __field(int,			ref		)
		    __field(int,			active		)
		    __field(int,			reason		)
			     ),

	    TP_fast_assign(
		    __entry->server = server->debug_id;
		    __entry->ref = ref;
		    __entry->active = active;
		    __entry->reason = reason;
			   ),

	    TP_printk("s=%08x %s u=%d a=%d",
		      __entry->server,
		      __print_symbolic(__entry->reason, afs_server_traces),
		      __entry->ref,
		      __entry->active)
	    );

TRACE_EVENT(afs_volume,
	    TP_PROTO(afs_volid_t vid, int ref, enum afs_volume_trace reason),

	    TP_ARGS(vid, ref, reason),

	    TP_STRUCT__entry(
		    __field(afs_volid_t,		vid		)
		    __field(int,			ref		)
		    __field(enum afs_volume_trace,	reason		)
			     ),

	    TP_fast_assign(
		    __entry->vid = vid;
		    __entry->ref = ref;
		    __entry->reason = reason;
			   ),

	    TP_printk("V=%llx %s u=%d",
		      __entry->vid,
		      __print_symbolic(__entry->reason, afs_volume_traces),
		      __entry->ref)
	    );

TRACE_EVENT(afs_cell,
	    TP_PROTO(unsigned int cell_debug_id, int usage, int active,
		     enum afs_cell_trace reason),

	    TP_ARGS(cell_debug_id, usage, active, reason),

	    TP_STRUCT__entry(
		    __field(unsigned int,		cell		)
		    __field(int,			usage		)
		    __field(int,			active		)
		    __field(int,			reason		)
			     ),

	    TP_fast_assign(
		    __entry->cell = cell_debug_id;
		    __entry->usage = usage;
		    __entry->active = active;
		    __entry->reason = reason;
			   ),

	    TP_printk("L=%08x %s u=%d a=%d",
		      __entry->cell,
		      __print_symbolic(__entry->reason, afs_cell_traces),
		      __entry->usage,
		      __entry->active)
	    );

#endif /* _TRACE_AFS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
