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

static int ndr_write_int16(struct ndr *n, __u16 value)
{
	if (n->length <= n->offset + sizeof(value)) {
		int ret;

		ret = try_to_realloc_ndr_blob(n, sizeof(value));
		if (ret)
			return ret;
	}

	*(__le16 *)ndr_get_field(n) = cpu_to_le16(value);
	n->offset += sizeof(value);
	return 0;
}

static int ndr_write_int32(struct ndr *n, __u32 value)
{
	if (n->length <= n->offset + sizeof(value)) {
		int ret;

		ret = try_to_realloc_ndr_blob(n, sizeof(value));
		if (ret)
			return ret;
	}

	*(__le32 *)ndr_get_field(n) = cpu_to_le32(value);
	n->offset += sizeof(value);
	return 0;
}

static int ndr_write_int64(struct ndr *n, __u64 value)
{
	if (n->length <= n->offset + sizeof(value)) {
		int ret;

		ret = try_to_realloc_ndr_blob(n, sizeof(value));
		if (ret)
			return ret;
	}

	*(__le64 *)ndr_get_field(n) = cpu_to_le64(value);
	n->offset += sizeof(value);
	return 0;
}

static int ndr_write_bytes(struct ndr *n, void *value, size_t sz)
{
	if (n->length <= n->offset + sz) {
		int ret;

		ret = try_to_realloc_ndr_blob(n, sz);
		if (ret)
			return ret;
	}

	memcpy(ndr_get_field(n), value, sz);
	n->offset += sz;
	return 0;
}

static int ndr_write_string(struct ndr *n, char *value)
{
	size_t sz;

	sz = strlen(value) + 1;
	if (n->length <= n->offset + sz) {
		int ret;

		ret = try_to_realloc_ndr_blob(n, sz);
		if (ret)
			return ret;
	}

	memcpy(ndr_get_field(n), value, sz);
	n->offset += sz;
	n->offset = ALIGN(n->offset, 2);
	return 0;
}

static int ndr_read_string(struct ndr *n, void *value, size_t sz)
{
	int len;

	if (n->offset + sz > n->length)
		return -EINVAL;

	len = strnlen(ndr_get_field(n), sz);
	if (value)
		memcpy(value, ndr_get_field(n), len);
	len++;
	n->offset += len;
	n->offset = ALIGN(n->offset, 2);
	return 0;
}

static int ndr_read_bytes(struct ndr *n, void *value, size_t sz)
{
	if (n->offset + sz > n->length)
		return -EINVAL;

	if (value)
		memcpy(value, ndr_get_field(n), sz);
	n->offset += sz;
	return 0;
}

static int ndr_read_int16(struct ndr *n, __u16 *value)
{
	if (n->offset + sizeof(__u16) > n->length)
		return -EINVAL;

	if (value)
		*value = le16_to_cpu(*(__le16 *)ndr_get_field(n));
	n->offset += sizeof(__u16);
	return 0;
}

static int ndr_read_int32(struct ndr *n, __u32 *value)
{
	if (n->offset + sizeof(__u32) > n->length)
		return 0;

	if (value)
		*value = le32_to_cpu(*(__le32 *)ndr_get_field(n));
	n->offset += sizeof(__u32);
	return 0;
}

static int ndr_read_int64(struct ndr *n, __u64 *value)
{
	if (n->offset + sizeof(__u64) > n->length)
		return -EINVAL;

	if (value)
		*value = le64_to_cpu(*(__le64 *)ndr_get_field(n));
	n->offset += sizeof(__u64);
	return 0;
}

int ndr_encode_dos_attr(struct ndr *n, struct xattr_dos_attrib *da)
{
	char hex_attr[12] = {0};
	int ret;

	n->offset = 0;
	n->length = 1024;
	n->data = kzalloc(n->length, GFP_KERNEL);
	if (!n->data)
		return -ENOMEM;

	if (da->version == 3) {
		snprintf(hex_attr, 10, "0x%x", da->attr);
		ret = ndr_write_string(n, hex_attr);
	} else {
		ret = ndr_write_string(n, "");
	}
	if (ret)
		return ret;

	ret = ndr_write_int16(n, da->version);
	if (ret)
		return ret;

	ret = ndr_write_int32(n, da->version);
	if (ret)
		return ret;

	ret = ndr_write_int32(n, da->flags);
	if (ret)
		return ret;

	ret = ndr_write_int32(n, da->attr);
	if (ret)
		return ret;

	if (da->version == 3) {
		ret = ndr_write_int32(n, da->ea_size);
		if (ret)
			return ret;
		ret = ndr_write_int64(n, da->size);
		if (ret)
			return ret;
		ret = ndr_write_int64(n, da->alloc_size);
	} else {
		ret = ndr_write_int64(n, da->itime);
	}
	if (ret)
		return ret;

	ret = ndr_write_int64(n, da->create_time);
	if (ret)
		return ret;

	if (da->version == 3)
		ret = ndr_write_int64(n, da->change_time);
	return ret;
}

int ndr_decode_dos_attr(struct ndr *n, struct xattr_dos_attrib *da)
{
	char hex_attr[12];
	unsigned int version2;
	int ret;

	n->offset = 0;
	ret = ndr_read_string(n, hex_attr, sizeof(hex_attr));
	if (ret)
		return ret;

	ret = ndr_read_int16(n, &da->version);
	if (ret)
		return ret;

	if (da->version != 3 && da->version != 4) {
		pr_err("v%d version is not supported\n", da->version);
		return -EINVAL;
	}

	ret = ndr_read_int32(n, &version2);
	if (ret)
		return ret;

	if (da->version != version2) {
		pr_err("ndr version mismatched(version: %d, version2: %d)\n",
		       da->version, version2);
		return -EINVAL;
	}

	ret = ndr_read_int32(n, NULL);
	if (ret)
		return ret;

	ret = ndr_read_int32(n, &da->attr);
	if (ret)
		return ret;

	if (da->version == 4) {
		ret = ndr_read_int64(n, &da->itime);
		if (ret)
			return ret;

		ret = ndr_read_int64(n, &da->create_time);
	} else {
		ret = ndr_read_int32(n, NULL);
		if (ret)
			return ret;

		ndr_read_int64(n, NULL);
		if (ret)
			return ret;

		ndr_read_int64(n, NULL);
		if (ret)
			return ret;

		ret = ndr_read_int64(n, &da->create_time);
		if (ret)
			return ret;

		ret = ndr_read_int64(n, NULL);
	}

	return ret;
}

static int ndr_encode_posix_acl_entry(struct ndr *n, struct xattr_smb_acl *acl)
{
	int i, ret;

	ret = ndr_write_int32(n, acl->count);
	if (ret)
		return ret;

	n->offset = ALIGN(n->offset, 8);
	ret = ndr_write_int32(n, acl->count);
	if (ret)
		return ret;

	ret = ndr_write_int32(n, 0);
	if (ret)
		return ret;

	for (i = 0; i < acl->count; i++) {
		n->offset = ALIGN(n->offset, 8);
		ret = ndr_write_int16(n, acl->entries[i].type);
		if (ret)
			return ret;

		ret = ndr_write_int16(n, acl->entries[i].type);
		if (ret)
			return ret;

		if (acl->entries[i].type == SMB_ACL_USER) {
			n->offset = ALIGN(n->offset, 8);
			ret = ndr_write_int64(n, acl->entries[i].uid);
		} else if (acl->entries[i].type == SMB_ACL_GROUP) {
			n->offset = ALIGN(n->offset, 8);
			ret = ndr_write_int64(n, acl->entries[i].gid);
		}
		if (ret)
			return ret;

		/* push permission */
		ret = ndr_write_int32(n, acl->entries[i].perm);
	}

	return ret;
}

int ndr_encode_posix_acl(struct ndr *n,
			 struct user_namespace *user_ns,
			 struct inode *inode,
			 struct xattr_smb_acl *acl,
			 struct xattr_smb_acl *def_acl)
{
	unsigned int ref_id = 0x00020000;
	int ret;

	n->offset = 0;
	n->length = 1024;
	n->data = kzalloc(n->length, GFP_KERNEL);
	if (!n->data)
		return -ENOMEM;

	if (acl) {
		/* ACL ACCESS */
		ret = ndr_write_int32(n, ref_id);
		ref_id += 4;
	} else {
		ret = ndr_write_int32(n, 0);
	}
	if (ret)
		return ret;

	if (def_acl) {
		/* DEFAULT ACL ACCESS */
		ret = ndr_write_int32(n, ref_id);
		ref_id += 4;
	} else {
		ret = ndr_write_int32(n, 0);
	}
	if (ret)
		return ret;

	ret = ndr_write_int64(n, from_kuid(&init_user_ns, i_uid_into_mnt(user_ns, inode)));
	if (ret)
		return ret;
	ret = ndr_write_int64(n, from_kgid(&init_user_ns, i_gid_into_mnt(user_ns, inode)));
	if (ret)
		return ret;
	ret = ndr_write_int32(n, inode->i_mode);
	if (ret)
		return ret;

	if (acl) {
		ret = ndr_encode_posix_acl_entry(n, acl);
		if (def_acl && !ret)
			ret = ndr_encode_posix_acl_entry(n, def_acl);
	}
	return ret;
}

int ndr_encode_v4_ntacl(struct ndr *n, struct xattr_ntacl *acl)
{
	unsigned int ref_id = 0x00020004;
	int ret;

	n->offset = 0;
	n->length = 2048;
	n->data = kzalloc(n->length, GFP_KERNEL);
	if (!n->data)
		return -ENOMEM;

	ret = ndr_write_int16(n, acl->version);
	if (ret)
		return ret;

	ret = ndr_write_int32(n, acl->version);
	if (ret)
		return ret;

	ret = ndr_write_int16(n, 2);
	if (ret)
		return ret;

	ret = ndr_write_int32(n, ref_id);
	if (ret)
		return ret;

	/* push hash type and hash 64bytes */
	ret = ndr_write_int16(n, acl->hash_type);
	if (ret)
		return ret;

	ret = ndr_write_bytes(n, acl->hash, XATTR_SD_HASH_SIZE);
	if (ret)
		return ret;

	ret = ndr_write_bytes(n, acl->desc, acl->desc_len);
	if (ret)
		return ret;

	ret = ndr_write_int64(n, acl->current_time);
	if (ret)
		return ret;

	ret = ndr_write_bytes(n, acl->posix_acl_hash, XATTR_SD_HASH_SIZE);
	if (ret)
		return ret;

	/* push ndr for security descriptor */
	ret = ndr_write_bytes(n, acl->sd_buf, acl->sd_size);
	return ret;
}

int ndr_decode_v4_ntacl(struct ndr *n, struct xattr_ntacl *acl)
{
	unsigned int version2;
	int ret;

	n->offset = 0;
	ret = ndr_read_int16(n, &acl->version);
	if (ret)
		return ret;
	if (acl->version != 4) {
		pr_err("v%d version is not supported\n", acl->version);
		return -EINVAL;
	}

	ret = ndr_read_int32(n, &version2);
	if (ret)
		return ret;
	if (acl->version != version2) {
		pr_err("ndr version mismatched(version: %d, version2: %d)\n",
		       acl->version, version2);
		return -EINVAL;
	}

	/* Read Level */
	ret = ndr_read_int16(n, NULL);
	if (ret)
		return ret;

	/* Read Ref Id */
	ret = ndr_read_int32(n, NULL);
	if (ret)
		return ret;

	ret = ndr_read_int16(n, &acl->hash_type);
	if (ret)
		return ret;

	ret = ndr_read_bytes(n, acl->hash, XATTR_SD_HASH_SIZE);
	if (ret)
		return ret;

	ndr_read_bytes(n, acl->desc, 10);
	if (strncmp(acl->desc, "posix_acl", 9)) {
		pr_err("Invalid acl description : %s\n", acl->desc);
		return -EINVAL;
	}

	/* Read Time */
	ret = ndr_read_int64(n, NULL);
	if (ret)
		return ret;

	/* Read Posix ACL hash */
	ret = ndr_read_bytes(n, acl->posix_acl_hash, XATTR_SD_HASH_SIZE);
	if (ret)
		return ret;

	acl->sd_size = n->length - n->offset;
	acl->sd_buf = kzalloc(acl->sd_size, GFP_KERNEL);
	if (!acl->sd_buf)
		return -ENOMEM;

	ret = ndr_read_bytes(n, acl->sd_buf, acl->sd_size);
	return ret;
}
