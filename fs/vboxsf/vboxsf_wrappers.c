// SPDX-License-Identifier: MIT
/*
 * Wrapper functions for the shfl host calls.
 *
 * Copyright (C) 2006-2018 Oracle Corporation
 */

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vbox_err.h>
#include <linux/vbox_utils.h>
#include "vfsmod.h"

#define SHFL_REQUEST \
	(VMMDEV_REQUESTOR_KERNEL | VMMDEV_REQUESTOR_USR_DRV_OTHER | \
	 VMMDEV_REQUESTOR_CON_DONT_KNOW | VMMDEV_REQUESTOR_TRUST_NOT_GIVEN)

static u32 vboxsf_client_id;

int vboxsf_connect(void)
{
	struct vbg_dev *gdev;
	struct vmmdev_hgcm_service_location loc;
	int err, vbox_status;

	loc.type = VMMDEV_HGCM_LOC_LOCALHOST_EXISTING;
	strcpy(loc.u.localhost.service_name, "VBoxSharedFolders");

	gdev = vbg_get_gdev();
	if (IS_ERR(gdev))
		return -ENODEV;	/* No guest-device */

	err = vbg_hgcm_connect(gdev, SHFL_REQUEST, &loc,
			       &vboxsf_client_id, &vbox_status);
	vbg_put_gdev(gdev);

	return err ? err : vbg_status_code_to_errno(vbox_status);
}

void vboxsf_disconnect(void)
{
	struct vbg_dev *gdev;
	int vbox_status;

	gdev = vbg_get_gdev();
	if (IS_ERR(gdev))
		return;   /* guest-device is gone, already disconnected */

	vbg_hgcm_disconnect(gdev, SHFL_REQUEST, vboxsf_client_id, &vbox_status);
	vbg_put_gdev(gdev);
}

static int vboxsf_call(u32 function, void *parms, u32 parm_count, int *status)
{
	struct vbg_dev *gdev;
	int err, vbox_status;

	gdev = vbg_get_gdev();
	if (IS_ERR(gdev))
		return -ESHUTDOWN; /* guest-dev removed underneath us */

	err = vbg_hgcm_call(gdev, SHFL_REQUEST, vboxsf_client_id, function,
			    U32_MAX, parms, parm_count, &vbox_status);
	vbg_put_gdev(gdev);

	if (err < 0)
		return err;

	if (status)
		*status = vbox_status;

	return vbg_status_code_to_errno(vbox_status);
}

int vboxsf_map_folder(struct shfl_string *folder_name, u32 *root)
{
	struct shfl_map_folder parms;
	int err, status;

	parms.path.type = VMMDEV_HGCM_PARM_TYPE_LINADDR_KERNEL;
	parms.path.u.pointer.size = shfl_string_buf_size(folder_name);
	parms.path.u.pointer.u.linear_addr = (uintptr_t)folder_name;

	parms.root.type = VMMDEV_HGCM_PARM_TYPE_32BIT;
	parms.root.u.value32 = 0;

	parms.delimiter.type = VMMDEV_HGCM_PARM_TYPE_32BIT;
	parms.delimiter.u.value32 = '/';

	parms.case_sensitive.type = VMMDEV_HGCM_PARM_TYPE_32BIT;
	parms.case_sensitive.u.value32 = 1;

	err = vboxsf_call(SHFL_FN_MAP_FOLDER, &parms, SHFL_CPARMS_MAP_FOLDER,
			  &status);
	if (err == -ENOSYS && status == VERR_NOT_IMPLEMENTED)
		vbg_err("%s: Error host is too old\n", __func__);

	*root = parms.root.u.value32;
	return err;
}

int vboxsf_unmap_folder(u32 root)
{
	struct shfl_unmap_folder parms;

	parms.root.type = VMMDEV_HGCM_PARM_TYPE_32BIT;
	parms.root.u.value32 = root;

	return vboxsf_call(SHFL_FN_UNMAP_FOLDER, &parms,
			   SHFL_CPARMS_UNMAP_FOLDER, NULL);
}

/**
 * vboxsf_create - Create a new file or folder
 * @root:         Root of the shared folder in which to create the file
 * @parsed_path:  The path of the file or folder relative to the shared folder
 * @create_parms: Parameters for file/folder creation.
 *
 * Create a new file or folder or open an existing one in a shared folder.
 * Note this function always returns 0 / success unless an exceptional condition
 * occurs - out of memory, invalid arguments, etc. If the file or folder could
 * not be opened or created, create_parms->handle will be set to
 * SHFL_HANDLE_NIL on return.  In this case the value in create_parms->result
 * provides information as to why (e.g. SHFL_FILE_EXISTS), create_parms->result
 * is also set on success as additional information.
 *
 * Returns:
 * 0 or negative errno value.
 */
int vboxsf_create(u32 root, struct shfl_string *parsed_path,
		  struct shfl_createparms *create_parms)
{
	struct shfl_create parms;

	parms.root.type = VMMDEV_HGCM_PARM_TYPE_32BIT;
	parms.root.u.value32 = root;

	parms.path.type = VMMDEV_HGCM_PARM_TYPE_LINADDR_KERNEL;
	parms.path.u.pointer.size = shfl_string_buf_size(parsed_path);
	parms.path.u.pointer.u.linear_addr = (uintptr_t)parsed_path;

	parms.parms.type = VMMDEV_HGCM_PARM_TYPE_LINADDR_KERNEL;
	parms.parms.u.pointer.size = sizeof(struct shfl_createparms);
	parms.parms.u.pointer.u.linear_addr = (uintptr_t)create_parms;

	return vboxsf_call(SHFL_FN_CREATE, &parms, SHFL_CPARMS_CREATE, NULL);
}

int vboxsf_close(u32 root, u64 handle)
{
	struct shfl_close parms;

	parms.root.type = VMMDEV_HGCM_PARM_TYPE_32BIT;
	parms.root.u.value32 = root;

	parms.handle.type = VMMDEV_HGCM_PARM_TYPE_64BIT;
	parms.handle.u.value64 = handle;

	return vboxsf_call(SHFL_FN_CLOSE, &parms, SHFL_CPARMS_CLOSE, NULL);
}

int vboxsf_remove(u32 root, struct shfl_string *parsed_path, u32 flags)
{
	struct shfl_remove parms;

	parms.root.type = VMMDEV_HGCM_PARM_TYPE_32BIT;
	parms.root.u.value32 = root;

	parms.path.type = VMMDEV_HGCM_PARM_TYPE_LINADDR_KERNEL_IN;
	parms.path.u.pointer.size = shfl_string_buf_size(parsed_path);
	parms.path.u.pointer.u.linear_addr = (uintptr_t)parsed_path;

	parms.flags.type = VMMDEV_HGCM_PARM_TYPE_32BIT;
	parms.flags.u.value32 = flags;

	return vboxsf_call(SHFL_FN_REMOVE, &parms, SHFL_CPARMS_REMOVE, NULL);
}

int vboxsf_rename(u32 root, struct shfl_string *src_path,
		  struct shfl_string *dest_path, u32 flags)
{
	struct shfl_rename parms;

	parms.root.type = VMMDEV_HGCM_PARM_TYPE_32BIT;
	parms.root.u.value32 = root;

	parms.src.type = VMMDEV_HGCM_PARM_TYPE_LINADDR_KERNEL_IN;
	parms.src.u.pointer.size = shfl_string_buf_size(src_path);
	parms.src.u.pointer.u.linear_addr = (uintptr_t)src_path;

	parms.dest.type = VMMDEV_HGCM_PARM_TYPE_LINADDR_KERNEL_IN;
	parms.dest.u.pointer.size = shfl_string_buf_size(dest_path);
	parms.dest.u.pointer.u.linear_addr = (uintptr_t)dest_path;

	parms.flags.type = VMMDEV_HGCM_PARM_TYPE_32BIT;
	parms.flags.u.value32 = flags;

	return vboxsf_call(SHFL_FN_RENAME, &parms, SHFL_CPARMS_RENAME, NULL);
}

int vboxsf_read(u32 root, u64 handle, u64 offset, u32 *buf_len, u8 *buf)
{
	struct shfl_read parms;
	int err;

	parms.root.type = VMMDEV_HGCM_PARM_TYPE_32BIT;
	parms.root.u.value32 = root;

	parms.handle.type = VMMDEV_HGCM_PARM_TYPE_64BIT;
	parms.handle.u.value64 = handle;
	parms.offset.type = VMMDEV_HGCM_PARM_TYPE_64BIT;
	parms.offset.u.value64 = offset;
	parms.cb.type = VMMDEV_HGCM_PARM_TYPE_32BIT;
	parms.cb.u.value32 = *buf_len;
	parms.buffer.type = VMMDEV_HGCM_PARM_TYPE_LINADDR_KERNEL_OUT;
	parms.buffer.u.pointer.size = *buf_len;
	parms.buffer.u.pointer.u.linear_addr = (uintptr_t)buf;

	err = vboxsf_call(SHFL_FN_READ, &parms, SHFL_CPARMS_READ, NULL);

	*buf_len = parms.cb.u.value32;
	return err;
}

int vboxsf_write(u32 root, u64 handle, u64 offset, u32 *buf_len, u8 *buf)
{
	struct shfl_write parms;
	int err;

	parms.root.type = VMMDEV_HGCM_PARM_TYPE_32BIT;
	parms.root.u.value32 = root;

	parms.handle.type = VMMDEV_HGCM_PARM_TYPE_64BIT;
	parms.handle.u.value64 = handle;
	parms.offset.type = VMMDEV_HGCM_PARM_TYPE_64BIT;
	parms.offset.u.value64 = offset;
	parms.cb.type = VMMDEV_HGCM_PARM_TYPE_32BIT;
	parms.cb.u.value32 = *buf_len;
	parms.buffer.type = VMMDEV_HGCM_PARM_TYPE_LINADDR_KERNEL_IN;
	parms.buffer.u.pointer.size = *buf_len;
	parms.buffer.u.pointer.u.linear_addr = (uintptr_t)buf;

	err = vboxsf_call(SHFL_FN_WRITE, &parms, SHFL_CPARMS_WRITE, NULL);

	*buf_len = parms.cb.u.value32;
	return err;
}

/* Returns 0 on success, 1 on end-of-dir, negative errno otherwise */
int vboxsf_dirinfo(u32 root, u64 handle,
		   struct shfl_string *parsed_path, u32 flags, u32 index,
		   u32 *buf_len, struct shfl_dirinfo *buf, u32 *file_count)
{
	struct shfl_list parms;
	int err, status;

	parms.root.type = VMMDEV_HGCM_PARM_TYPE_32BIT;
	parms.root.u.value32 = root;

	parms.handle.type = VMMDEV_HGCM_PARM_TYPE_64BIT;
	parms.handle.u.value64 = handle;
	parms.flags.type = VMMDEV_HGCM_PARM_TYPE_32BIT;
	parms.flags.u.value32 = flags;
	parms.cb.type = VMMDEV_HGCM_PARM_TYPE_32BIT;
	parms.cb.u.value32 = *buf_len;
	if (parsed_path) {
		parms.path.type = VMMDEV_HGCM_PARM_TYPE_LINADDR_KERNEL_IN;
		parms.path.u.pointer.size = shfl_string_buf_size(parsed_path);
		parms.path.u.pointer.u.linear_addr = (uintptr_t)parsed_path;
	} else {
		parms.path.type = VMMDEV_HGCM_PARM_TYPE_LINADDR_IN;
		parms.path.u.pointer.size = 0;
		parms.path.u.pointer.u.linear_addr = 0;
	}

	parms.buffer.type = VMMDEV_HGCM_PARM_TYPE_LINADDR_KERNEL_OUT;
	parms.buffer.u.pointer.size = *buf_len;
	parms.buffer.u.pointer.u.linear_addr = (uintptr_t)buf;

	parms.resume_point.type = VMMDEV_HGCM_PARM_TYPE_32BIT;
	parms.resume_point.u.value32 = index;
	parms.file_count.type = VMMDEV_HGCM_PARM_TYPE_32BIT;
	parms.file_count.u.value32 = 0;	/* out parameter only */

	err = vboxsf_call(SHFL_FN_LIST, &parms, SHFL_CPARMS_LIST, &status);
	if (err == -ENODATA && status == VERR_NO_MORE_FILES)
		err = 1;

	*buf_len = parms.cb.u.value32;
	*file_count = parms.file_count.u.value32;
	return err;
}

int vboxsf_fsinfo(u32 root, u64 handle, u32 flags,
		  u32 *buf_len, void *buf)
{
	struct shfl_information parms;
	int err;

	parms.root.type = VMMDEV_HGCM_PARM_TYPE_32BIT;
	parms.root.u.value32 = root;

	parms.handle.type = VMMDEV_HGCM_PARM_TYPE_64BIT;
	parms.handle.u.value64 = handle;
	parms.flags.type = VMMDEV_HGCM_PARM_TYPE_32BIT;
	parms.flags.u.value32 = flags;
	parms.cb.type = VMMDEV_HGCM_PARM_TYPE_32BIT;
	parms.cb.u.value32 = *buf_len;
	parms.info.type = VMMDEV_HGCM_PARM_TYPE_LINADDR_KERNEL;
	parms.info.u.pointer.size = *buf_len;
	parms.info.u.pointer.u.linear_addr = (uintptr_t)buf;

	err = vboxsf_call(SHFL_FN_INFORMATION, &parms, SHFL_CPARMS_INFORMATION,
			  NULL);

	*buf_len = parms.cb.u.value32;
	return err;
}

int vboxsf_readlink(u32 root, struct shfl_string *parsed_path,
		    u32 buf_len, u8 *buf)
{
	struct shfl_readLink parms;

	parms.root.type = VMMDEV_HGCM_PARM_TYPE_32BIT;
	parms.root.u.value32 = root;

	parms.path.type = VMMDEV_HGCM_PARM_TYPE_LINADDR_KERNEL_IN;
	parms.path.u.pointer.size = shfl_string_buf_size(parsed_path);
	parms.path.u.pointer.u.linear_addr = (uintptr_t)parsed_path;

	parms.buffer.type = VMMDEV_HGCM_PARM_TYPE_LINADDR_KERNEL_OUT;
	parms.buffer.u.pointer.size = buf_len;
	parms.buffer.u.pointer.u.linear_addr = (uintptr_t)buf;

	return vboxsf_call(SHFL_FN_READLINK, &parms, SHFL_CPARMS_READLINK,
			   NULL);
}

int vboxsf_symlink(u32 root, struct shfl_string *new_path,
		   struct shfl_string *old_path, struct shfl_fsobjinfo *buf)
{
	struct shfl_symlink parms;

	parms.root.type = VMMDEV_HGCM_PARM_TYPE_32BIT;
	parms.root.u.value32 = root;

	parms.new_path.type = VMMDEV_HGCM_PARM_TYPE_LINADDR_KERNEL_IN;
	parms.new_path.u.pointer.size = shfl_string_buf_size(new_path);
	parms.new_path.u.pointer.u.linear_addr = (uintptr_t)new_path;

	parms.old_path.type = VMMDEV_HGCM_PARM_TYPE_LINADDR_KERNEL_IN;
	parms.old_path.u.pointer.size = shfl_string_buf_size(old_path);
	parms.old_path.u.pointer.u.linear_addr = (uintptr_t)old_path;

	parms.info.type = VMMDEV_HGCM_PARM_TYPE_LINADDR_KERNEL_OUT;
	parms.info.u.pointer.size = sizeof(struct shfl_fsobjinfo);
	parms.info.u.pointer.u.linear_addr = (uintptr_t)buf;

	return vboxsf_call(SHFL_FN_SYMLINK, &parms, SHFL_CPARMS_SYMLINK, NULL);
}

int vboxsf_set_utf8(void)
{
	return vboxsf_call(SHFL_FN_SET_UTF8, NULL, 0, NULL);
}

int vboxsf_set_symlinks(void)
{
	return vboxsf_call(SHFL_FN_SET_SYMLINKS, NULL, 0, NULL);
}
