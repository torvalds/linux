/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Red Hat, Inc.
 * Copyright (C) 2012 Jeremy Kerr <jeremy.kerr@canonical.com>
 */
#ifndef EFIVAR_FS_INTERNAL_H
#define EFIVAR_FS_INTERNAL_H

#include <linux/list.h>
#include <linux/efi.h>

struct efivarfs_mount_opts {
	kuid_t uid;
	kgid_t gid;
};

struct efivarfs_fs_info {
	struct efivarfs_mount_opts mount_opts;
	struct list_head efivarfs_list;
	struct super_block *sb;
	struct notifier_block nb;
};

struct efi_variable {
	efi_char16_t  VariableName[EFI_VAR_NAME_LEN/sizeof(efi_char16_t)];
	efi_guid_t    VendorGuid;
	unsigned long DataSize;
	__u8          Data[1024];
	efi_status_t  Status;
	__u32         Attributes;
} __attribute__((packed));

struct efivar_entry {
	struct efi_variable var;
	struct list_head list;
	struct kobject kobj;
};

int efivar_init(int (*func)(efi_char16_t *, efi_guid_t, unsigned long, void *,
			    struct list_head *),
		void *data, struct list_head *head);

int efivar_entry_add(struct efivar_entry *entry, struct list_head *head);
void __efivar_entry_add(struct efivar_entry *entry, struct list_head *head);
void efivar_entry_remove(struct efivar_entry *entry);
int efivar_entry_delete(struct efivar_entry *entry);

int efivar_entry_size(struct efivar_entry *entry, unsigned long *size);
int __efivar_entry_get(struct efivar_entry *entry, u32 *attributes,
		       unsigned long *size, void *data);
int efivar_entry_get(struct efivar_entry *entry, u32 *attributes,
		     unsigned long *size, void *data);
int efivar_entry_set_get_size(struct efivar_entry *entry, u32 attributes,
			      unsigned long *size, void *data, bool *set);

int efivar_entry_iter(int (*func)(struct efivar_entry *, void *),
		      struct list_head *head, void *data);

bool efivar_validate(efi_guid_t vendor, efi_char16_t *var_name, u8 *data,
		     unsigned long data_size);
bool efivar_variable_is_removable(efi_guid_t vendor, const char *name,
				  size_t len);

extern const struct file_operations efivarfs_file_operations;
extern const struct inode_operations efivarfs_dir_inode_operations;
extern bool efivarfs_valid_name(const char *str, int len);
extern struct inode *efivarfs_get_inode(struct super_block *sb,
			const struct inode *dir, int mode, dev_t dev,
			bool is_removable);

#endif /* EFIVAR_FS_INTERNAL_H */
