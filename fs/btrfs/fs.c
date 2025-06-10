// SPDX-License-Identifier: GPL-2.0

#include "messages.h"
#include "fs.h"
#include "accessors.h"
#include "volumes.h"

static const struct btrfs_csums {
	u16		size;
	const char	name[10];
	const char	driver[12];
} btrfs_csums[] = {
	[BTRFS_CSUM_TYPE_CRC32] = { .size = 4, .name = "crc32c" },
	[BTRFS_CSUM_TYPE_XXHASH] = { .size = 8, .name = "xxhash64" },
	[BTRFS_CSUM_TYPE_SHA256] = { .size = 32, .name = "sha256" },
	[BTRFS_CSUM_TYPE_BLAKE2] = { .size = 32, .name = "blake2b",
				     .driver = "blake2b-256" },
};

/* This exists for btrfs-progs usages. */
u16 btrfs_csum_type_size(u16 type)
{
	return btrfs_csums[type].size;
}

int btrfs_super_csum_size(const struct btrfs_super_block *s)
{
	u16 t = btrfs_super_csum_type(s);

	/* csum type is validated at mount time. */
	return btrfs_csum_type_size(t);
}

const char *btrfs_super_csum_name(u16 csum_type)
{
	/* csum type is validated at mount time. */
	return btrfs_csums[csum_type].name;
}

/*
 * Return driver name if defined, otherwise the name that's also a valid driver
 * name.
 */
const char *btrfs_super_csum_driver(u16 csum_type)
{
	/* csum type is validated at mount time */
	return btrfs_csums[csum_type].driver[0] ?
		btrfs_csums[csum_type].driver :
		btrfs_csums[csum_type].name;
}

size_t __attribute_const__ btrfs_get_num_csums(void)
{
	return ARRAY_SIZE(btrfs_csums);
}

/*
 * Start exclusive operation @type, return true on success.
 */
bool btrfs_exclop_start(struct btrfs_fs_info *fs_info,
			enum btrfs_exclusive_operation type)
{
	bool ret = false;

	spin_lock(&fs_info->super_lock);
	if (fs_info->exclusive_operation == BTRFS_EXCLOP_NONE) {
		fs_info->exclusive_operation = type;
		ret = true;
	}
	spin_unlock(&fs_info->super_lock);

	return ret;
}

/*
 * Conditionally allow to enter the exclusive operation in case it's compatible
 * with the running one.  This must be paired with btrfs_exclop_start_unlock()
 * and btrfs_exclop_finish().
 *
 * Compatibility:
 * - the same type is already running
 * - when trying to add a device and balance has been paused
 * - not BTRFS_EXCLOP_NONE - this is intentionally incompatible and the caller
 *   must check the condition first that would allow none -> @type
 */
bool btrfs_exclop_start_try_lock(struct btrfs_fs_info *fs_info,
				 enum btrfs_exclusive_operation type)
{
	spin_lock(&fs_info->super_lock);
	if (fs_info->exclusive_operation == type ||
	    (fs_info->exclusive_operation == BTRFS_EXCLOP_BALANCE_PAUSED &&
	     type == BTRFS_EXCLOP_DEV_ADD))
		return true;

	spin_unlock(&fs_info->super_lock);
	return false;
}

void btrfs_exclop_start_unlock(struct btrfs_fs_info *fs_info)
{
	spin_unlock(&fs_info->super_lock);
}

void btrfs_exclop_finish(struct btrfs_fs_info *fs_info)
{
	spin_lock(&fs_info->super_lock);
	WRITE_ONCE(fs_info->exclusive_operation, BTRFS_EXCLOP_NONE);
	spin_unlock(&fs_info->super_lock);
	sysfs_notify(&fs_info->fs_devices->fsid_kobj, NULL, "exclusive_operation");
}

void btrfs_exclop_balance(struct btrfs_fs_info *fs_info,
			  enum btrfs_exclusive_operation op)
{
	switch (op) {
	case BTRFS_EXCLOP_BALANCE_PAUSED:
		spin_lock(&fs_info->super_lock);
		ASSERT(fs_info->exclusive_operation == BTRFS_EXCLOP_BALANCE ||
		       fs_info->exclusive_operation == BTRFS_EXCLOP_DEV_ADD ||
		       fs_info->exclusive_operation == BTRFS_EXCLOP_NONE ||
		       fs_info->exclusive_operation == BTRFS_EXCLOP_BALANCE_PAUSED);
		fs_info->exclusive_operation = BTRFS_EXCLOP_BALANCE_PAUSED;
		spin_unlock(&fs_info->super_lock);
		break;
	case BTRFS_EXCLOP_BALANCE:
		spin_lock(&fs_info->super_lock);
		ASSERT(fs_info->exclusive_operation == BTRFS_EXCLOP_BALANCE_PAUSED);
		fs_info->exclusive_operation = BTRFS_EXCLOP_BALANCE;
		spin_unlock(&fs_info->super_lock);
		break;
	default:
		btrfs_warn(fs_info,
			"invalid exclop balance operation %d requested", op);
	}
}

void __btrfs_set_fs_incompat(struct btrfs_fs_info *fs_info, u64 flag,
			     const char *name)
{
	struct btrfs_super_block *disk_super;
	u64 features;

	disk_super = fs_info->super_copy;
	features = btrfs_super_incompat_flags(disk_super);
	if (!(features & flag)) {
		spin_lock(&fs_info->super_lock);
		features = btrfs_super_incompat_flags(disk_super);
		if (!(features & flag)) {
			features |= flag;
			btrfs_set_super_incompat_flags(disk_super, features);
			btrfs_info(fs_info,
				"setting incompat feature flag for %s (0x%llx)",
				name, flag);
		}
		spin_unlock(&fs_info->super_lock);
		set_bit(BTRFS_FS_FEATURE_CHANGED, &fs_info->flags);
	}
}

void __btrfs_clear_fs_incompat(struct btrfs_fs_info *fs_info, u64 flag,
			       const char *name)
{
	struct btrfs_super_block *disk_super;
	u64 features;

	disk_super = fs_info->super_copy;
	features = btrfs_super_incompat_flags(disk_super);
	if (features & flag) {
		spin_lock(&fs_info->super_lock);
		features = btrfs_super_incompat_flags(disk_super);
		if (features & flag) {
			features &= ~flag;
			btrfs_set_super_incompat_flags(disk_super, features);
			btrfs_info(fs_info,
				"clearing incompat feature flag for %s (0x%llx)",
				name, flag);
		}
		spin_unlock(&fs_info->super_lock);
		set_bit(BTRFS_FS_FEATURE_CHANGED, &fs_info->flags);
	}
}

void __btrfs_set_fs_compat_ro(struct btrfs_fs_info *fs_info, u64 flag,
			      const char *name)
{
	struct btrfs_super_block *disk_super;
	u64 features;

	disk_super = fs_info->super_copy;
	features = btrfs_super_compat_ro_flags(disk_super);
	if (!(features & flag)) {
		spin_lock(&fs_info->super_lock);
		features = btrfs_super_compat_ro_flags(disk_super);
		if (!(features & flag)) {
			features |= flag;
			btrfs_set_super_compat_ro_flags(disk_super, features);
			btrfs_info(fs_info,
				"setting compat-ro feature flag for %s (0x%llx)",
				name, flag);
		}
		spin_unlock(&fs_info->super_lock);
		set_bit(BTRFS_FS_FEATURE_CHANGED, &fs_info->flags);
	}
}

void __btrfs_clear_fs_compat_ro(struct btrfs_fs_info *fs_info, u64 flag,
				const char *name)
{
	struct btrfs_super_block *disk_super;
	u64 features;

	disk_super = fs_info->super_copy;
	features = btrfs_super_compat_ro_flags(disk_super);
	if (features & flag) {
		spin_lock(&fs_info->super_lock);
		features = btrfs_super_compat_ro_flags(disk_super);
		if (features & flag) {
			features &= ~flag;
			btrfs_set_super_compat_ro_flags(disk_super, features);
			btrfs_info(fs_info,
				"clearing compat-ro feature flag for %s (0x%llx)",
				name, flag);
		}
		spin_unlock(&fs_info->super_lock);
		set_bit(BTRFS_FS_FEATURE_CHANGED, &fs_info->flags);
	}
}
