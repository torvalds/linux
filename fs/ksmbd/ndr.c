// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2021 Samsung Electronics Co., Ltd.
 *   Author(s): Namjae Jeon <linkinjeon@kernel.org>
 */

#include <linux/fs.h>

#include "glob.h"
#include "ndr.h"

static inline char *ndr_get_field(struct ndr *n)
{
	return n->data + n->offset;
}

static int try_to_realloc_ndr_blob(struct ndr *n, size_t sz)
{
	char *data;

	data = krealloc(n->data, n->offset + sz + 1024, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	n->data = data;
	n->length += 1024;
	memset(n->data + n->offset, 0, 1024);
	return 0;
}

static void ndr_write_int16(struct ndr *n, __u16 value)
{
	if (n->length <= n->offset + sizeof(value))
		try_to_realloc_ndr_blob(n, sizeof(value));

	*(__le16 *)ndr_get_field(n) = cpu_to_le16(value);
	n->offset += sizeof(value);
}

static void ndr_write_int32(struct ndr *n, __u32 value)
{
	if (n->length <= n->offset + sizeof(value))
		try_to_realloc_ndr_blob(n, sizeof(value));

	*(__le32 *)ndr_get_field(n) = cpu_to_le32(value);
	n->offset += sizeof(value);
}

static void ndr_write_int64(struct ndr *n, __u64 value)
{
	if (n->length <= n->offset + sizeof(value))
		try_to_realloc_ndr_blob(n, sizeof(value));

	*(__le64 *)ndr_get_field(n) = cpu_to_le64(value);
	n->offset += sizeof(value);
}

static int ndr_write_bytes(struct ndr *n, void *value, size_t sz)
{
	if (n->length <= n->offset + sz)
		try_to_realloc_ndr_blob(n, sz);

	memcpy(ndr_get_field(n), value, sz);
	n->offset += sz;
	return 0;
}

static int ndr_write_string(struct ndr *n, char *value)
{
	size_t sz;

	sz = strlen(value) + 1;
	if (n->length <= n->offset + sz)
		try_to_realloc_ndr_blob(n, sz);

	memcpy(ndr_get_field(n), value, sz);
	n->offset += sz;
	n->offset = ALIGN(n->offset, 2);
	return 0;
}

static int ndr_read_string(struct ndr *n, void *value, size_t sz)
{
	int len = strnlen(ndr_get_field(n), sz);

	memcpy(value, ndr_get_field(n), len);
	len++;
	n->offset += len;
	n->offset = ALIGN(n->offset, 2);
	return 0;
}

static int ndr_read_bytes(struct ndr *n, void *value, size_t sz)
{
	memcpy(value, ndr_get_field(n), sz);
	n->offset += sz;
	return 0;
}

static __u16 ndr_read_int16(struct ndr *n)
{
	__u16 ret;

	ret = le16_to_cpu(*(__le16 *)ndr_get_field(n));
	n->offset += sizeof(__u16);
	return ret;
}

static __u32 ndr_read_int32(struct ndr *n)
{
	__u32 ret;

	ret = le32_to_cpu(*(__le32 *)ndr_get_field(n));
	n->offset += sizeof(__u32);
	return ret;
}

static __u64 ndr_read_int64(struct ndr *n)
{
	__u64 ret;

	ret = le64_to_cpu(*(__le64 *)ndr_get_field(n));
	n->offset += sizeof(__u64);
	return ret;
}

int ndr_encode_dos_attr(struct ndr *n, struct xattr_dos_attrib *da)
{
	char hex_attr[12] = {0};

	n->offset = 0;
	n->length = 1024;
	n->data = kzalloc(n->length, GFP_KERNEL);
	if (!n->data)
		return -ENOMEM;

	if (da->version == 3) {
		snprintf(hex_attr, 10, "0x%x", da->attr);
		ndr_write_string(n, hex_attr);
	} else {
		ndr_write_string(n, "");
	}
	ndr_write_int16(n, da->version);
	ndr_write_int32(n, da->version);

	ndr_write_int32(n, da->flags);
	ndr_write_int32(n, da->attr);
	if (da->version == 3) {
		ndr_write_int32(n, da->ea_size);
		ndr_write_int64(n, da->size);
		ndr_write_int64(n, da->alloc_size);
	} else {
		ndr_write_int64(n, da->itime);
	}
	ndr_write_int64(n, da->create_time);
	if (da->version == 3)
		ndr_write_int64(n, da->change_time);
	return 0;
}

int ndr_decode_dos_attr(struct ndr *n, struct xattr_dos_attrib *da)
{
	char *hex_attr;
	int version2;

	hex_attr = kzalloc(n->length, GFP_KERNEL);
	if (!hex_attr)
		return -ENOMEM;

	n->offset = 0;
	ndr_read_string(n, hex_attr, n->length);
	kfree(hex_attr);
	da->version = ndr_read_int16(n);

	if (da->version != 3 && da->version != 4) {
		pr_err("v%d version is not supported\n", da->version);
		return -EINVAL;
	}

	version2 = ndr_read_int32(n);
	if (da->version != version2) {
		pr_err("ndr version mismatched(version: %d, version2: %d)\n",
		       da->version, version2);
		return -EINVAL;
	}

	ndr_read_int32(n);
	da->attr = ndr_read_int32(n);
	if (da->version == 4) {
		da->itime = ndr_read_int64(n);
		da->create_time = ndr_read_int64(n);
	} else {
		ndr_read_int32(n);
		ndr_read_int64(n);
		ndr_read_int64(n);
		da->create_time = ndr_read_int64(n);
		ndr_read_int64(n);
	}

	return 0;
}

static int ndr_encode_posix_acl_entry(struct ndr *n, struct xattr_smb_acl *acl)
{
	int i;

	ndr_write_int32(n, acl->count);
	n->offset = ALIGN(n->offset, 8);
	ndr_write_int32(n, acl->count);
	ndr_write_int32(n, 0);

	for (i = 0; i < acl->count; i++) {
		n->offset = ALIGN(n->offset, 8);
		ndr_write_int16(n, acl->entries[i].type);
		ndr_write_int16(n, acl->entries[i].type);

		if (acl->entries[i].type == SMB_ACL_USER) {
			n->offset = ALIGN(n->offset, 8);
			ndr_write_int64(n, acl->entries[i].uid);
		} else if (acl->entries[i].type == SMB_ACL_GROUP) {
			n->offset = ALIGN(n->offset, 8);
			ndr_write_int64(n, acl->entries[i].gid);
		}

		/* push permission */
		ndr_write_int32(n, acl->entries[i].perm);
	}

	return 0;
}

int ndr_encode_posix_acl(struct ndr *n,
			 struct user_namespace *user_ns,
			 struct inode *inode,
			 struct xattr_smb_acl *acl,
			 struct xattr_smb_acl *def_acl)
{
	int ref_id = 0x00020000;

	n->offset = 0;
	n->length = 1024;
	n->data = kzalloc(n->length, GFP_KERNEL);
	if (!n->data)
		return -ENOMEM;

	if (acl) {
		/* ACL ACCESS */
		ndr_write_int32(n, ref_id);
		ref_id += 4;
	} else {
		ndr_write_int32(n, 0);
	}

	if (def_acl) {
		/* DEFAULT ACL ACCESS */
		ndr_write_int32(n, ref_id);
		ref_id += 4;
	} else {
		ndr_write_int32(n, 0);
	}

	ndr_write_int64(n, from_kuid(&init_user_ns, i_uid_into_mnt(user_ns, inode)));
	ndr_write_int64(n, from_kgid(&init_user_ns, i_gid_into_mnt(user_ns, inode)));
	ndr_write_int32(n, inode->i_mode);

	if (acl) {
		ndr_encode_posix_acl_entry(n, acl);
		if (def_acl)
			ndr_encode_posix_acl_entry(n, def_acl);
	}
	return 0;
}

int ndr_encode_v4_ntacl(struct ndr *n, struct xattr_ntacl *acl)
{
	int ref_id = 0x00020004;

	n->offset = 0;
	n->length = 2048;
	n->data = kzalloc(n->length, GFP_KERNEL);
	if (!n->data)
		return -ENOMEM;

	ndr_write_int16(n, acl->version);
	ndr_write_int32(n, acl->version);
	ndr_write_int16(n, 2);
	ndr_write_int32(n, ref_id);

	/* push hash type and hash 64bytes */
	ndr_write_int16(n, acl->hash_type);
	ndr_write_bytes(n, acl->hash, XATTR_SD_HASH_SIZE);
	ndr_write_bytes(n, acl->desc, acl->desc_len);
	ndr_write_int64(n, acl->current_time);
	ndr_write_bytes(n, acl->posix_acl_hash, XATTR_SD_HASH_SIZE);

	/* push ndr for security descriptor */
	ndr_write_bytes(n, acl->sd_buf, acl->sd_size);

	return 0;
}

int ndr_decode_v4_ntacl(struct ndr *n, struct xattr_ntacl *acl)
{
	int version2;

	n->offset = 0;
	acl->version = ndr_read_int16(n);
	if (acl->version != 4) {
		pr_err("v%d version is not supported\n", acl->version);
		return -EINVAL;
	}

	version2 = ndr_read_int32(n);
	if (acl->version != version2) {
		pr_err("ndr version mismatched(version: %d, version2: %d)\n",
		       acl->version, version2);
		return -EINVAL;
	}

	/* Read Level */
	ndr_read_int16(n);
	/* Read Ref Id */
	ndr_read_int32(n);
	acl->hash_type = ndr_read_int16(n);
	ndr_read_bytes(n, acl->hash, XATTR_SD_HASH_SIZE);

	ndr_read_bytes(n, acl->desc, 10);
	if (strncmp(acl->desc, "posix_acl", 9)) {
		pr_err("Invalid acl description : %s\n", acl->desc);
		return -EINVAL;
	}

	/* Read Time */
	ndr_read_int64(n);
	/* Read Posix ACL hash */
	ndr_read_bytes(n, acl->posix_acl_hash, XATTR_SD_HASH_SIZE);
	acl->sd_size = n->length - n->offset;
	acl->sd_buf = kzalloc(acl->sd_size, GFP_KERNEL);
	if (!acl->sd_buf)
		return -ENOMEM;

	ndr_read_bytes(n, acl->sd_buf, acl->sd_size);

	return 0;
}
