// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NTFS kernel super block handling.
 *
 * Copyright (c) 2001-2012 Anton Altaparmakov and Tuxera Inc.
 * Copyright (c) 2001,2002 Richard Russon
 * Copyright (c) 2025 LG Electronics Co., Ltd.
 */

#include <linux/blkdev.h>	/* For bdev_logical_block_size(). */
#include <linux/backing-dev.h>
#include <linux/vfs.h>
#include <linux/fs_struct.h>
#include <linux/sched/mm.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>

#include "sysctl.h"
#include "logfile.h"
#include "quota.h"
#include "index.h"
#include "ntfs.h"
#include "ea.h"
#include "volume.h"

/* A global default upcase table and a corresponding reference count. */
static __le16 *default_upcase;
static unsigned long ntfs_nr_upcase_users;

static struct workqueue_struct *ntfs_wq;

/* Error constants/strings used in inode.c::ntfs_show_options(). */
enum {
	/* One of these must be present, default is ON_ERRORS_CONTINUE. */
	ON_ERRORS_PANIC = 0x01,
	ON_ERRORS_REMOUNT_RO = 0x02,
	ON_ERRORS_CONTINUE = 0x04,
};

static const struct constant_table ntfs_param_enums[] = {
	{ "panic",		ON_ERRORS_PANIC },
	{ "remount-ro",		ON_ERRORS_REMOUNT_RO },
	{ "continue",		ON_ERRORS_CONTINUE },
	{}
};

enum {
	Opt_uid,
	Opt_gid,
	Opt_umask,
	Opt_dmask,
	Opt_fmask,
	Opt_errors,
	Opt_nls,
	Opt_charset,
	Opt_show_sys_files,
	Opt_show_meta,
	Opt_case_sensitive,
	Opt_disable_sparse,
	Opt_sparse,
	Opt_mft_zone_multiplier,
	Opt_preallocated_size,
	Opt_sys_immutable,
	Opt_nohidden,
	Opt_hide_dot_files,
	Opt_check_windows_names,
	Opt_acl,
	Opt_discard,
	Opt_nocase,
};

static const struct fs_parameter_spec ntfs_parameters[] = {
	fsparam_u32("uid",			Opt_uid),
	fsparam_u32("gid",			Opt_gid),
	fsparam_u32oct("umask",			Opt_umask),
	fsparam_u32oct("dmask",			Opt_dmask),
	fsparam_u32oct("fmask",			Opt_fmask),
	fsparam_string("nls",			Opt_nls),
	fsparam_string("iocharset",		Opt_charset),
	fsparam_enum("errors",			Opt_errors, ntfs_param_enums),
	fsparam_flag("show_sys_files",		Opt_show_sys_files),
	fsparam_flag("showmeta",		Opt_show_meta),
	fsparam_flag("case_sensitive",		Opt_case_sensitive),
	fsparam_flag("disable_sparse",		Opt_disable_sparse),
	fsparam_s32("mft_zone_multiplier",	Opt_mft_zone_multiplier),
	fsparam_u64("preallocated_size",	Opt_preallocated_size),
	fsparam_flag("sys_immutable",		Opt_sys_immutable),
	fsparam_flag("nohidden",		Opt_nohidden),
	fsparam_flag("hide_dot_files",		Opt_hide_dot_files),
	fsparam_flag("windows_names",		Opt_check_windows_names),
	fsparam_flag("acl",			Opt_acl),
	fsparam_flag("discard",			Opt_discard),
	fsparam_flag("sparse",			Opt_sparse),
	fsparam_flag("nocase",			Opt_nocase),
	{}
};

static int ntfs_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct ntfs_volume *vol = fc->s_fs_info;
	struct fs_parse_result result;
	int opt;

	opt = fs_parse(fc, ntfs_parameters, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_uid:
		vol->uid = make_kuid(current_user_ns(), result.uint_32);
		break;
	case Opt_gid:
		vol->gid = make_kgid(current_user_ns(), result.uint_32);
		break;
	case Opt_umask:
		vol->fmask = vol->dmask = result.uint_32;
		break;
	case Opt_dmask:
		vol->dmask = result.uint_32;
		break;
	case Opt_fmask:
		vol->fmask = result.uint_32;
		break;
	case Opt_errors:
		vol->on_errors = result.uint_32;
		break;
	case Opt_nls:
	case Opt_charset:
		if (vol->nls_map)
			unload_nls(vol->nls_map);
		vol->nls_map = load_nls(param->string);
		if (!vol->nls_map) {
			ntfs_error(vol->sb, "Failed to load NLS table '%s'.",
				   param->string);
			return -EINVAL;
		}
		break;
	case Opt_mft_zone_multiplier:
		if (vol->mft_zone_multiplier && vol->mft_zone_multiplier !=
				result.int_32) {
			ntfs_error(vol->sb, "Cannot change mft_zone_multiplier on remount.");
			return -EINVAL;
		}
		if (result.int_32 < 1 || result.int_32 > 4) {
			ntfs_error(vol->sb,
				"Invalid mft_zone_multiplier. Using default value, i.e. 1.");
			vol->mft_zone_multiplier = 1;
		} else
			vol->mft_zone_multiplier = result.int_32;
		break;
	case Opt_show_sys_files:
	case Opt_show_meta:
		if (result.boolean)
			NVolSetShowSystemFiles(vol);
		else
			NVolClearShowSystemFiles(vol);
		break;
	case Opt_case_sensitive:
		if (result.boolean)
			NVolSetCaseSensitive(vol);
		else
			NVolClearCaseSensitive(vol);
		break;
	case Opt_nocase:
		if (result.boolean)
			NVolClearCaseSensitive(vol);
		else
			NVolSetCaseSensitive(vol);
		break;
	case Opt_preallocated_size:
		vol->preallocated_size = (loff_t)result.uint_64;
		break;
	case Opt_sys_immutable:
		if (result.boolean)
			NVolSetSysImmutable(vol);
		else
			NVolClearSysImmutable(vol);
		break;
	case Opt_nohidden:
		if (result.boolean)
			NVolClearShowHiddenFiles(vol);
		else
			NVolSetShowHiddenFiles(vol);
		break;
	case Opt_hide_dot_files:
		if (result.boolean)
			NVolSetHideDotFiles(vol);
		else
			NVolClearHideDotFiles(vol);
		break;
	case Opt_check_windows_names:
		if (result.boolean)
			NVolSetCheckWindowsNames(vol);
		else
			NVolClearCheckWindowsNames(vol);
		break;
	case Opt_acl:
#ifdef CONFIG_NTFS_FS_POSIX_ACL
		if (result.boolean)
			fc->sb_flags |= SB_POSIXACL;
		else
			fc->sb_flags &= ~SB_POSIXACL;
		break;
#else
		return -EINVAL;
#endif
	case Opt_discard:
		if (result.boolean)
			NVolSetDiscard(vol);
		else
			NVolClearDiscard(vol);
		break;
	case Opt_disable_sparse:
		if (result.boolean)
			NVolSetDisableSparse(vol);
		else
			NVolClearDisableSparse(vol);
		break;
	case Opt_sparse:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ntfs_reconfigure(struct fs_context *fc)
{
	struct super_block *sb = fc->root->d_sb;
	struct ntfs_volume *vol = NTFS_SB(sb);

	ntfs_debug("Entering with remount");

	sync_filesystem(sb);

	/*
	 * For the read-write compiled driver, if we are remounting read-write,
	 * make sure there are no volume errors and that no unsupported volume
	 * flags are set.  Also, empty the logfile journal as it would become
	 * stale as soon as something is written to the volume and mark the
	 * volume dirty so that chkdsk is run if the volume is not umounted
	 * cleanly.  Finally, mark the quotas out of date so Windows rescans
	 * the volume on boot and updates them.
	 *
	 * When remounting read-only, mark the volume clean if no volume errors
	 * have occurred.
	 */
	if (sb_rdonly(sb) && !(fc->sb_flags & SB_RDONLY)) {
		static const char *es = ".  Cannot remount read-write.";

		/* Remounting read-write. */
		if (NVolErrors(vol)) {
			ntfs_error(sb, "Volume has errors and is read-only%s",
					es);
			return -EROFS;
		}
		if (vol->vol_flags & VOLUME_IS_DIRTY) {
			ntfs_error(sb, "Volume is dirty and read-only%s", es);
			return -EROFS;
		}
		if (vol->vol_flags & VOLUME_MODIFIED_BY_CHKDSK) {
			ntfs_error(sb, "Volume has been modified by chkdsk and is read-only%s", es);
			return -EROFS;
		}
		if (vol->vol_flags & VOLUME_MUST_MOUNT_RO_MASK) {
			ntfs_error(sb, "Volume has unsupported flags set (0x%x) and is read-only%s",
					le16_to_cpu(vol->vol_flags), es);
			return -EROFS;
		}
		if (vol->logfile_ino && !ntfs_empty_logfile(vol->logfile_ino)) {
			ntfs_error(sb, "Failed to empty journal LogFile%s",
					es);
			NVolSetErrors(vol);
			return -EROFS;
		}
		if (!ntfs_mark_quotas_out_of_date(vol)) {
			ntfs_error(sb, "Failed to mark quotas out of date%s",
					es);
			NVolSetErrors(vol);
			return -EROFS;
		}
	} else if (!sb_rdonly(sb) && (fc->sb_flags & SB_RDONLY)) {
		/* Remounting read-only. */
		if (!NVolErrors(vol)) {
			if (ntfs_clear_volume_flags(vol, VOLUME_IS_DIRTY))
				ntfs_warning(sb,
					"Failed to clear dirty bit in volume information flags.  Run chkdsk.");
		}
	}

	ntfs_debug("Done.");
	return 0;
}

const struct option_t on_errors_arr[] = {
	{ ON_ERRORS_PANIC,	"panic" },
	{ ON_ERRORS_REMOUNT_RO,	"remount-ro", },
	{ ON_ERRORS_CONTINUE,	"continue", },
	{ 0,			NULL }
};

void ntfs_handle_error(struct super_block *sb)
{
	struct ntfs_volume *vol = NTFS_SB(sb);

	if (sb_rdonly(sb))
		return;

	if (vol->on_errors == ON_ERRORS_REMOUNT_RO) {
		sb->s_flags |= SB_RDONLY;
		pr_crit("(device %s): Filesystem has been set read-only\n",
			sb->s_id);
	} else if (vol->on_errors == ON_ERRORS_PANIC) {
		panic("ntfs: (device %s): panic from previous error\n",
		      sb->s_id);
	} else if (vol->on_errors == ON_ERRORS_CONTINUE) {
		if (errseq_check(&sb->s_wb_err, vol->wb_err) == -ENODEV) {
			NVolSetShutdown(vol);
			vol->wb_err = sb->s_wb_err;
		}
	}
}

/*
 * ntfs_write_volume_flags - write new flags to the volume information flags
 * @vol:	ntfs volume on which to modify the flags
 * @flags:	new flags value for the volume information flags
 *
 * Internal function.  You probably want to use ntfs_{set,clear}_volume_flags()
 * instead (see below).
 *
 * Replace the volume information flags on the volume @vol with the value
 * supplied in @flags.  Note, this overwrites the volume information flags, so
 * make sure to combine the flags you want to modify with the old flags and use
 * the result when calling ntfs_write_volume_flags().
 *
 * Return 0 on success and -errno on error.
 */
static int ntfs_write_volume_flags(struct ntfs_volume *vol, const __le16 flags)
{
	struct ntfs_inode *ni = NTFS_I(vol->vol_ino);
	struct volume_information *vi;
	struct ntfs_attr_search_ctx *ctx;
	int err;

	ntfs_debug("Entering, old flags = 0x%x, new flags = 0x%x.",
			le16_to_cpu(vol->vol_flags), le16_to_cpu(flags));
	mutex_lock(&ni->mrec_lock);
	if (vol->vol_flags == flags)
		goto done;

	ctx = ntfs_attr_get_search_ctx(ni, NULL);
	if (!ctx) {
		err = -ENOMEM;
		goto put_unm_err_out;
	}

	err = ntfs_attr_lookup(AT_VOLUME_INFORMATION, NULL, 0, 0, 0, NULL, 0,
			ctx);
	if (err)
		goto put_unm_err_out;

	vi = (struct volume_information *)((u8 *)ctx->attr +
			le16_to_cpu(ctx->attr->data.resident.value_offset));
	vol->vol_flags = vi->flags = flags;
	mark_mft_record_dirty(ctx->ntfs_ino);
	ntfs_attr_put_search_ctx(ctx);
done:
	mutex_unlock(&ni->mrec_lock);
	ntfs_debug("Done.");
	return 0;
put_unm_err_out:
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	mutex_unlock(&ni->mrec_lock);
	ntfs_error(vol->sb, "Failed with error code %i.", -err);
	return err;
}

/*
 * ntfs_set_volume_flags - set bits in the volume information flags
 * @vol:	ntfs volume on which to modify the flags
 * @flags:	flags to set on the volume
 *
 * Set the bits in @flags in the volume information flags on the volume @vol.
 *
 * Return 0 on success and -errno on error.
 */
int ntfs_set_volume_flags(struct ntfs_volume *vol, __le16 flags)
{
	flags &= VOLUME_FLAGS_MASK;
	return ntfs_write_volume_flags(vol, vol->vol_flags | flags);
}

/*
 * ntfs_clear_volume_flags - clear bits in the volume information flags
 * @vol:	ntfs volume on which to modify the flags
 * @flags:	flags to clear on the volume
 *
 * Clear the bits in @flags in the volume information flags on the volume @vol.
 *
 * Return 0 on success and -errno on error.
 */
int ntfs_clear_volume_flags(struct ntfs_volume *vol, __le16 flags)
{
	flags &= VOLUME_FLAGS_MASK;
	flags = vol->vol_flags & cpu_to_le16(~le16_to_cpu(flags));
	return ntfs_write_volume_flags(vol, flags);
}

int ntfs_write_volume_label(struct ntfs_volume *vol, char *label)
{
	struct ntfs_inode *vol_ni = NTFS_I(vol->vol_ino);
	struct ntfs_attr_search_ctx *ctx;
	__le16 *uname;
	int uname_len, ret;

	uname_len = ntfs_nlstoucs(vol, label, strlen(label),
				  &uname, FSLABEL_MAX);
	if (uname_len < 0) {
		ntfs_error(vol->sb,
			"Failed to convert volume label '%s' to Unicode.",
			label);
		return uname_len;
	}

	if (uname_len  > NTFS_MAX_LABEL_LEN) {
		ntfs_error(vol->sb,
			   "Volume label is too long (max %d characters).",
			   NTFS_MAX_LABEL_LEN);
		kvfree(uname);
		return -EINVAL;
	}

	mutex_lock(&vol_ni->mrec_lock);
	ctx = ntfs_attr_get_search_ctx(vol_ni, NULL);
	if (!ctx) {
		ret = -ENOMEM;
		goto  out;
	}

	if (!ntfs_attr_lookup(AT_VOLUME_NAME, NULL, 0, 0, 0, NULL, 0,
			     ctx))
		ntfs_attr_record_rm(ctx);
	ntfs_attr_put_search_ctx(ctx);

	ret = ntfs_resident_attr_record_add(vol_ni, AT_VOLUME_NAME, AT_UNNAMED, 0,
					    (u8 *)uname, uname_len * sizeof(__le16), 0);
out:
	mutex_unlock(&vol_ni->mrec_lock);
	kvfree(uname);
	mark_inode_dirty_sync(vol->vol_ino);

	if (ret >= 0) {
		kfree(vol->volume_label);
		vol->volume_label = kstrdup(label, GFP_KERNEL);
		ret = 0;
	}
	return ret;
}

/*
 * is_boot_sector_ntfs - check whether a boot sector is a valid NTFS boot sector
 * @sb:		Super block of the device to which @b belongs.
 * @b:		Boot sector of device @sb to check.
 * @silent:	If 'true', all output will be silenced.
 *
 * is_boot_sector_ntfs() checks whether the boot sector @b is a valid NTFS boot
 * sector. Returns 'true' if it is valid and 'false' if not.
 *
 * @sb is only needed for warning/error output, i.e. it can be NULL when silent
 * is 'true'.
 */
static bool is_boot_sector_ntfs(const struct super_block *sb,
		const struct ntfs_boot_sector *b, const bool silent)
{
	/*
	 * Check that checksum == sum of u32 values from b to the checksum
	 * field.  If checksum is zero, no checking is done.  We will work when
	 * the checksum test fails, since some utilities update the boot sector
	 * ignoring the checksum which leaves the checksum out-of-date.  We
	 * report a warning if this is the case.
	 */
	if ((void *)b < (void *)&b->checksum && b->checksum && !silent) {
		__le32 *u;
		u32 i;

		for (i = 0, u = (__le32 *)b; u < (__le32 *)(&b->checksum); ++u)
			i += le32_to_cpup(u);
		if (le32_to_cpu(b->checksum) != i)
			ntfs_warning(sb, "Invalid boot sector checksum.");
	}
	/* Check OEMidentifier is "NTFS    " */
	if (b->oem_id != magicNTFS)
		goto not_ntfs;
	/* Check bytes per sector value is between 256 and 4096. */
	if (le16_to_cpu(b->bpb.bytes_per_sector) < 0x100 ||
	    le16_to_cpu(b->bpb.bytes_per_sector) > 0x1000)
		goto not_ntfs;
	/*
	 * Check sectors per cluster value is valid and the cluster size
	 * is not above the maximum (2MB).
	 */
	if (b->bpb.sectors_per_cluster > 0x80 &&
	    b->bpb.sectors_per_cluster < 0xf4)
		goto not_ntfs;

	/* Check reserved/unused fields are really zero. */
	if (le16_to_cpu(b->bpb.reserved_sectors) ||
			le16_to_cpu(b->bpb.root_entries) ||
			le16_to_cpu(b->bpb.sectors) ||
			le16_to_cpu(b->bpb.sectors_per_fat) ||
			le32_to_cpu(b->bpb.large_sectors) || b->bpb.fats)
		goto not_ntfs;
	/* Check clusters per file mft record value is valid. */
	if ((u8)b->clusters_per_mft_record < 0xe1 ||
			(u8)b->clusters_per_mft_record > 0xf7)
		switch (b->clusters_per_mft_record) {
		case 1: case 2: case 4: case 8: case 16: case 32: case 64:
			break;
		default:
			goto not_ntfs;
		}
	/* Check clusters per index block value is valid. */
	if ((u8)b->clusters_per_index_record < 0xe1 ||
			(u8)b->clusters_per_index_record > 0xf7)
		switch (b->clusters_per_index_record) {
		case 1: case 2: case 4: case 8: case 16: case 32: case 64:
			break;
		default:
			goto not_ntfs;
		}
	/*
	 * Check for valid end of sector marker. We will work without it, but
	 * many BIOSes will refuse to boot from a bootsector if the magic is
	 * incorrect, so we emit a warning.
	 */
	if (!silent && b->end_of_sector_marker != cpu_to_le16(0xaa55))
		ntfs_warning(sb, "Invalid end of sector marker.");
	return true;
not_ntfs:
	return false;
}

/*
 * read_ntfs_boot_sector - read the NTFS boot sector of a device
 * @sb:		super block of device to read the boot sector from
 * @silent:	if true, suppress all output
 *
 * Reads the boot sector from the device and validates it.
 */
static char *read_ntfs_boot_sector(struct super_block *sb,
		const int silent)
{
	char *boot_sector;

	boot_sector = kzalloc(PAGE_SIZE, GFP_NOFS);
	if (!boot_sector)
		return NULL;

	if (ntfs_bdev_read(sb->s_bdev, boot_sector, 0, PAGE_SIZE)) {
		if (!silent)
			ntfs_error(sb, "Unable to read primary boot sector.");
		kfree(boot_sector);
		return NULL;
	}

	if (!is_boot_sector_ntfs(sb, (struct ntfs_boot_sector *)boot_sector,
				 silent)) {
		if (!silent)
			ntfs_error(sb, "Primary boot sector is invalid.");
		kfree(boot_sector);
		return NULL;
	}

	return boot_sector;
}

/*
 * parse_ntfs_boot_sector - parse the boot sector and store the data in @vol
 * @vol:	volume structure to initialise with data from boot sector
 * @b:		boot sector to parse
 *
 * Parse the ntfs boot sector @b and store all imporant information therein in
 * the ntfs super block @vol.  Return 'true' on success and 'false' on error.
 */
static bool parse_ntfs_boot_sector(struct ntfs_volume *vol,
		const struct ntfs_boot_sector *b)
{
	unsigned int sectors_per_cluster, sectors_per_cluster_bits, nr_hidden_sects;
	int clusters_per_mft_record, clusters_per_index_record;
	s64 ll;

	vol->sector_size = le16_to_cpu(b->bpb.bytes_per_sector);
	vol->sector_size_bits = ffs(vol->sector_size) - 1;
	ntfs_debug("vol->sector_size = %i (0x%x)", vol->sector_size,
			vol->sector_size);
	ntfs_debug("vol->sector_size_bits = %i (0x%x)", vol->sector_size_bits,
			vol->sector_size_bits);
	if (vol->sector_size < vol->sb->s_blocksize) {
		ntfs_error(vol->sb,
			"Sector size (%i) is smaller than the device block size (%lu).  This is not supported.",
			vol->sector_size, vol->sb->s_blocksize);
		return false;
	}

	if (b->bpb.sectors_per_cluster >= 0xf4)
		sectors_per_cluster = 1U << -(s8)b->bpb.sectors_per_cluster;
	else
		sectors_per_cluster = b->bpb.sectors_per_cluster;
	ntfs_debug("sectors_per_cluster = 0x%x", b->bpb.sectors_per_cluster);
	sectors_per_cluster_bits = ffs(sectors_per_cluster) - 1;
	ntfs_debug("sectors_per_cluster_bits = 0x%x",
			sectors_per_cluster_bits);
	nr_hidden_sects = le32_to_cpu(b->bpb.hidden_sectors);
	ntfs_debug("number of hidden sectors = 0x%x", nr_hidden_sects);
	vol->cluster_size = vol->sector_size << sectors_per_cluster_bits;
	vol->cluster_size_mask = vol->cluster_size - 1;
	vol->cluster_size_bits = ffs(vol->cluster_size) - 1;
	ntfs_debug("vol->cluster_size = %i (0x%x)", vol->cluster_size,
			vol->cluster_size);
	ntfs_debug("vol->cluster_size_mask = 0x%x", vol->cluster_size_mask);
	ntfs_debug("vol->cluster_size_bits = %i", vol->cluster_size_bits);
	if (vol->cluster_size < vol->sector_size) {
		ntfs_error(vol->sb,
			"Cluster size (%i) is smaller than the sector size (%i).  This is not supported.",
			vol->cluster_size, vol->sector_size);
		return false;
	}
	clusters_per_mft_record = b->clusters_per_mft_record;
	ntfs_debug("clusters_per_mft_record = %i (0x%x)",
			clusters_per_mft_record, clusters_per_mft_record);
	if (clusters_per_mft_record > 0)
		vol->mft_record_size = vol->cluster_size <<
				(ffs(clusters_per_mft_record) - 1);
	else
		/*
		 * When mft_record_size < cluster_size, clusters_per_mft_record
		 * = -log2(mft_record_size) bytes. mft_record_size normaly is
		 * 1024 bytes, which is encoded as 0xF6 (-10 in decimal).
		 */
		vol->mft_record_size = 1 << -clusters_per_mft_record;
	vol->mft_record_size_mask = vol->mft_record_size - 1;
	vol->mft_record_size_bits = ffs(vol->mft_record_size) - 1;
	ntfs_debug("vol->mft_record_size = %i (0x%x)", vol->mft_record_size,
			vol->mft_record_size);
	ntfs_debug("vol->mft_record_size_mask = 0x%x",
			vol->mft_record_size_mask);
	ntfs_debug("vol->mft_record_size_bits = %i (0x%x)",
			vol->mft_record_size_bits, vol->mft_record_size_bits);
	/*
	 * We cannot support mft record sizes above the PAGE_SIZE since
	 * we store $MFT/$DATA, the table of mft records in the page cache.
	 */
	if (vol->mft_record_size > PAGE_SIZE) {
		ntfs_error(vol->sb,
			"Mft record size (%i) exceeds the PAGE_SIZE on your system (%lu).  This is not supported.",
			vol->mft_record_size, PAGE_SIZE);
		return false;
	}
	/* We cannot support mft record sizes below the sector size. */
	if (vol->mft_record_size < vol->sector_size) {
		ntfs_warning(vol->sb, "Mft record size (%i) is smaller than the sector size (%i).",
				vol->mft_record_size, vol->sector_size);
	}
	clusters_per_index_record = b->clusters_per_index_record;
	ntfs_debug("clusters_per_index_record = %i (0x%x)",
			clusters_per_index_record, clusters_per_index_record);
	if (clusters_per_index_record > 0)
		vol->index_record_size = vol->cluster_size <<
				(ffs(clusters_per_index_record) - 1);
	else
		/*
		 * When index_record_size < cluster_size,
		 * clusters_per_index_record = -log2(index_record_size) bytes.
		 * index_record_size normaly equals 4096 bytes, which is
		 * encoded as 0xF4 (-12 in decimal).
		 */
		vol->index_record_size = 1 << -clusters_per_index_record;
	vol->index_record_size_mask = vol->index_record_size - 1;
	vol->index_record_size_bits = ffs(vol->index_record_size) - 1;
	ntfs_debug("vol->index_record_size = %i (0x%x)",
			vol->index_record_size, vol->index_record_size);
	ntfs_debug("vol->index_record_size_mask = 0x%x",
			vol->index_record_size_mask);
	ntfs_debug("vol->index_record_size_bits = %i (0x%x)",
			vol->index_record_size_bits,
			vol->index_record_size_bits);
	/* We cannot support index record sizes below the sector size. */
	if (vol->index_record_size < vol->sector_size) {
		ntfs_error(vol->sb,
			   "Index record size (%i) is smaller than the sector size (%i).  This is not supported.",
			   vol->index_record_size, vol->sector_size);
		return false;
	}
	/*
	 * Get the size of the volume in clusters and check for 64-bit-ness.
	 * Windows currently only uses 32 bits to save the clusters so we do
	 * the same as it is much faster on 32-bit CPUs.
	 */
	ll = le64_to_cpu(b->number_of_sectors) >> sectors_per_cluster_bits;
	if ((u64)ll >= 1ULL << 32) {
		ntfs_error(vol->sb, "Cannot handle 64-bit clusters.");
		return false;
	}
	vol->nr_clusters = ll;
	ntfs_debug("vol->nr_clusters = 0x%llx", vol->nr_clusters);
	ll = le64_to_cpu(b->mft_lcn);
	if (ll >= vol->nr_clusters) {
		ntfs_error(vol->sb, "MFT LCN (%lli, 0x%llx) is beyond end of volume.  Weird.",
				ll, ll);
		return false;
	}
	vol->mft_lcn = ll;
	ntfs_debug("vol->mft_lcn = 0x%llx", vol->mft_lcn);
	ll = le64_to_cpu(b->mftmirr_lcn);
	if (ll >= vol->nr_clusters) {
		ntfs_error(vol->sb, "MFTMirr LCN (%lli, 0x%llx) is beyond end of volume.  Weird.",
				ll, ll);
		return false;
	}
	vol->mftmirr_lcn = ll;
	ntfs_debug("vol->mftmirr_lcn = 0x%llx", vol->mftmirr_lcn);
	/*
	 * Work out the size of the mft mirror in number of mft records. If the
	 * cluster size is less than or equal to the size taken by four mft
	 * records, the mft mirror stores the first four mft records. If the
	 * cluster size is bigger than the size taken by four mft records, the
	 * mft mirror contains as many mft records as will fit into one
	 * cluster.
	 */
	if (vol->cluster_size <= (4 << vol->mft_record_size_bits))
		vol->mftmirr_size = 4;
	else
		vol->mftmirr_size = vol->cluster_size >>
				vol->mft_record_size_bits;
	ntfs_debug("vol->mftmirr_size = %i", vol->mftmirr_size);
	vol->serial_no = le64_to_cpu(b->volume_serial_number);
	ntfs_debug("vol->serial_no = 0x%llx", vol->serial_no);

	vol->sparse_compression_unit = 4;
	if (vol->cluster_size > 4096) {
		switch (vol->cluster_size) {
		case 65536:
			vol->sparse_compression_unit = 0;
			break;
		case 32768:
			vol->sparse_compression_unit = 1;
			break;
		case 16384:
			vol->sparse_compression_unit = 2;
			break;
		case 8192:
			vol->sparse_compression_unit = 3;
			break;
		}
	}

	return true;
}

/*
 * ntfs_setup_allocators - initialize the cluster and mft allocators
 * @vol:	volume structure for which to setup the allocators
 *
 * Setup the cluster (lcn) and mft allocators to the starting values.
 */
static void ntfs_setup_allocators(struct ntfs_volume *vol)
{
	s64 mft_zone_size, mft_lcn;

	ntfs_debug("vol->mft_zone_multiplier = 0x%x",
			vol->mft_zone_multiplier);
	/* Determine the size of the MFT zone. */
	mft_zone_size = vol->nr_clusters;
	switch (vol->mft_zone_multiplier) {  /* % of volume size in clusters */
	case 4:
		mft_zone_size >>= 1;			/* 50%   */
		break;
	case 3:
		mft_zone_size = (mft_zone_size +
				(mft_zone_size >> 1)) >> 2;	/* 37.5% */
		break;
	case 2:
		mft_zone_size >>= 2;			/* 25%   */
		break;
	/* case 1: */
	default:
		mft_zone_size >>= 3;			/* 12.5% */
		break;
	}
	/* Setup the mft zone. */
	vol->mft_zone_start = vol->mft_zone_pos = vol->mft_lcn;
	ntfs_debug("vol->mft_zone_pos = 0x%llx", vol->mft_zone_pos);
	/*
	 * Calculate the mft_lcn for an unmodified NTFS volume (see mkntfs
	 * source) and if the actual mft_lcn is in the expected place or even
	 * further to the front of the volume, extend the mft_zone to cover the
	 * beginning of the volume as well.  This is in order to protect the
	 * area reserved for the mft bitmap as well within the mft_zone itself.
	 * On non-standard volumes we do not protect it as the overhead would
	 * be higher than the speed increase we would get by doing it.
	 */
	mft_lcn = NTFS_B_TO_CLU(vol, 8192 + 2 * vol->cluster_size - 1);
	if (mft_lcn * vol->cluster_size < 16 * 1024)
		mft_lcn = (16 * 1024 + vol->cluster_size - 1) >>
				vol->cluster_size_bits;
	if (vol->mft_zone_start <= mft_lcn)
		vol->mft_zone_start = 0;
	ntfs_debug("vol->mft_zone_start = 0x%llx", vol->mft_zone_start);
	/*
	 * Need to cap the mft zone on non-standard volumes so that it does
	 * not point outside the boundaries of the volume.  We do this by
	 * halving the zone size until we are inside the volume.
	 */
	vol->mft_zone_end = vol->mft_lcn + mft_zone_size;
	while (vol->mft_zone_end >= vol->nr_clusters) {
		mft_zone_size >>= 1;
		vol->mft_zone_end = vol->mft_lcn + mft_zone_size;
	}
	ntfs_debug("vol->mft_zone_end = 0x%llx", vol->mft_zone_end);
	/*
	 * Set the current position within each data zone to the start of the
	 * respective zone.
	 */
	vol->data1_zone_pos = vol->mft_zone_end;
	ntfs_debug("vol->data1_zone_pos = 0x%llx", vol->data1_zone_pos);
	vol->data2_zone_pos = 0;
	ntfs_debug("vol->data2_zone_pos = 0x%llx", vol->data2_zone_pos);

	/* Set the mft data allocation position to mft record 24. */
	vol->mft_data_pos = 24;
	ntfs_debug("vol->mft_data_pos = 0x%llx", vol->mft_data_pos);
}

static struct lock_class_key mftmirr_runlist_lock_key,
			     mftmirr_mrec_lock_key;
/*
 * load_and_init_mft_mirror - load and setup the mft mirror inode for a volume
 * @vol:	ntfs super block describing device whose mft mirror to load
 *
 * Return 'true' on success or 'false' on error.
 */
static bool load_and_init_mft_mirror(struct ntfs_volume *vol)
{
	struct inode *tmp_ino;
	struct ntfs_inode *tmp_ni;

	ntfs_debug("Entering.");
	/* Get mft mirror inode. */
	tmp_ino = ntfs_iget(vol->sb, FILE_MFTMirr);
	if (IS_ERR(tmp_ino)) {
		if (!IS_ERR(tmp_ino))
			iput(tmp_ino);
		/* Caller will display error message. */
		return false;
	}
	lockdep_set_class(&NTFS_I(tmp_ino)->runlist.lock,
			  &mftmirr_runlist_lock_key);
	lockdep_set_class(&NTFS_I(tmp_ino)->mrec_lock,
			  &mftmirr_mrec_lock_key);
	/*
	 * Re-initialize some specifics about $MFTMirr's inode as
	 * ntfs_read_inode() will have set up the default ones.
	 */
	/* Set uid and gid to root. */
	tmp_ino->i_uid = GLOBAL_ROOT_UID;
	tmp_ino->i_gid = GLOBAL_ROOT_GID;
	/* Regular file.  No access for anyone. */
	tmp_ino->i_mode = S_IFREG;
	/* No VFS initiated operations allowed for $MFTMirr. */
	tmp_ino->i_op = &ntfs_empty_inode_ops;
	tmp_ino->i_fop = &ntfs_empty_file_ops;
	/* Put in our special address space operations. */
	tmp_ino->i_mapping->a_ops = &ntfs_aops;
	tmp_ni = NTFS_I(tmp_ino);
	/* The $MFTMirr, like the $MFT is multi sector transfer protected. */
	NInoSetMstProtected(tmp_ni);
	NInoSetSparseDisabled(tmp_ni);
	/*
	 * Set up our little cheat allowing us to reuse the async read io
	 * completion handler for directories.
	 */
	tmp_ni->itype.index.block_size = vol->mft_record_size;
	tmp_ni->itype.index.block_size_bits = vol->mft_record_size_bits;
	vol->mftmirr_ino = tmp_ino;
	ntfs_debug("Done.");
	return true;
}

/*
 * check_mft_mirror - compare contents of the mft mirror with the mft
 * @vol:	ntfs super block describing device whose mft mirror to check
 *
 * Return 'true' on success or 'false' on error.
 *
 * Note, this function also results in the mft mirror runlist being completely
 * mapped into memory.  The mft mirror write code requires this and will BUG()
 * should it find an unmapped runlist element.
 */
static bool check_mft_mirror(struct ntfs_volume *vol)
{
	struct super_block *sb = vol->sb;
	struct ntfs_inode *mirr_ni;
	struct folio *mft_folio = NULL, *mirr_folio = NULL;
	u8 *kmft = NULL, *kmirr = NULL;
	struct runlist_element *rl, rl2[2];
	pgoff_t index;
	int mrecs_per_page, i;

	ntfs_debug("Entering.");
	/* Compare contents of $MFT and $MFTMirr. */
	mrecs_per_page = PAGE_SIZE / vol->mft_record_size;
	index = i = 0;
	do {
		u32 bytes;

		/* Switch pages if necessary. */
		if (!(i % mrecs_per_page)) {
			if (index) {
				kunmap_local(kmirr);
				folio_put(mirr_folio);
				kunmap_local(kmft);
				folio_put(mft_folio);
			}
			/* Get the $MFT page. */
			mft_folio = read_mapping_folio(vol->mft_ino->i_mapping,
					index, NULL);
			if (IS_ERR(mft_folio)) {
				ntfs_error(sb, "Failed to read $MFT.");
				return false;
			}
			kmft = kmap_local_folio(mft_folio, 0);
			/* Get the $MFTMirr page. */
			mirr_folio = read_mapping_folio(vol->mftmirr_ino->i_mapping,
					index, NULL);
			if (IS_ERR(mirr_folio)) {
				ntfs_error(sb, "Failed to read $MFTMirr.");
				goto mft_unmap_out;
			}
			kmirr = kmap_local_folio(mirr_folio, 0);
			++index;
		}

		/* Do not check the record if it is not in use. */
		if (((struct mft_record *)kmft)->flags & MFT_RECORD_IN_USE) {
			/* Make sure the record is ok. */
			if (ntfs_is_baad_recordp((__le32 *)kmft)) {
				ntfs_error(sb,
					"Incomplete multi sector transfer detected in mft record %i.",
					i);
mm_unmap_out:
				kunmap_local(kmirr);
				folio_put(mirr_folio);
mft_unmap_out:
				kunmap_local(kmft);
				folio_put(mft_folio);
				return false;
			}
		}
		/* Do not check the mirror record if it is not in use. */
		if (((struct mft_record *)kmirr)->flags & MFT_RECORD_IN_USE) {
			if (ntfs_is_baad_recordp((__le32 *)kmirr)) {
				ntfs_error(sb,
					"Incomplete multi sector transfer detected in mft mirror record %i.",
					i);
				goto mm_unmap_out;
			}
		}
		/* Get the amount of data in the current record. */
		bytes = le32_to_cpu(((struct mft_record *)kmft)->bytes_in_use);
		if (bytes < sizeof(struct mft_record_old) ||
		    bytes > vol->mft_record_size ||
		    ntfs_is_baad_recordp((__le32 *)kmft)) {
			bytes = le32_to_cpu(((struct mft_record *)kmirr)->bytes_in_use);
			if (bytes < sizeof(struct mft_record_old) ||
			    bytes > vol->mft_record_size ||
			    ntfs_is_baad_recordp((__le32 *)kmirr))
				bytes = vol->mft_record_size;
		}
		kmft += vol->mft_record_size;
		kmirr += vol->mft_record_size;
	} while (++i < vol->mftmirr_size);
	/* Release the last folios. */
	kunmap_local(kmirr);
	folio_put(mirr_folio);
	kunmap_local(kmft);
	folio_put(mft_folio);

	/* Construct the mft mirror runlist by hand. */
	rl2[0].vcn = 0;
	rl2[0].lcn = vol->mftmirr_lcn;
	rl2[0].length = NTFS_B_TO_CLU(vol, vol->mftmirr_size * vol->mft_record_size +
				vol->cluster_size - 1);
	rl2[1].vcn = rl2[0].length;
	rl2[1].lcn = LCN_ENOENT;
	rl2[1].length = 0;
	/*
	 * Because we have just read all of the mft mirror, we know we have
	 * mapped the full runlist for it.
	 */
	mirr_ni = NTFS_I(vol->mftmirr_ino);
	down_read(&mirr_ni->runlist.lock);
	rl = mirr_ni->runlist.rl;
	/* Compare the two runlists.  They must be identical. */
	i = 0;
	do {
		if (rl2[i].vcn != rl[i].vcn || rl2[i].lcn != rl[i].lcn ||
				rl2[i].length != rl[i].length) {
			ntfs_error(sb, "$MFTMirr location mismatch.  Run chkdsk.");
			up_read(&mirr_ni->runlist.lock);
			return false;
		}
	} while (rl2[i++].length);
	up_read(&mirr_ni->runlist.lock);
	ntfs_debug("Done.");
	return true;
}

/*
 * load_and_check_logfile - load and check the logfile inode for a volume
 * @vol: ntfs volume to load the logfile for
 * @rp: on success, set to the restart page header
 *
 * Return 0 on success or errno on error.
 */
static int load_and_check_logfile(struct ntfs_volume *vol,
				  struct restart_page_header **rp)
{
	struct inode *tmp_ino;
	int err = 0;

	ntfs_debug("Entering.");
	tmp_ino = ntfs_iget(vol->sb, FILE_LogFile);
	if (IS_ERR(tmp_ino)) {
		if (!IS_ERR(tmp_ino))
			iput(tmp_ino);
		/* Caller will display error message. */
		return -ENOENT;
	}
	if (!ntfs_check_logfile(tmp_ino, rp))
		err = -EINVAL;
	NInoSetSparseDisabled(NTFS_I(tmp_ino));
	vol->logfile_ino = tmp_ino;
	ntfs_debug("Done.");
	return err;
}

#define NTFS_HIBERFIL_HEADER_SIZE	4096

/*
 * check_windows_hibernation_status - check if Windows is suspended on a volume
 * @vol:	ntfs super block of device to check
 *
 * Check if Windows is hibernated on the ntfs volume @vol.  This is done by
 * looking for the file hiberfil.sys in the root directory of the volume.  If
 * the file is not present Windows is definitely not suspended.
 *
 * If hiberfil.sys exists and is less than 4kiB in size it means Windows is
 * definitely suspended (this volume is not the system volume).  Caveat:  on a
 * system with many volumes it is possible that the < 4kiB check is bogus but
 * for now this should do fine.
 *
 * If hiberfil.sys exists and is larger than 4kiB in size, we need to read the
 * hiberfil header (which is the first 4kiB).  If this begins with "hibr",
 * Windows is definitely suspended.  If it is completely full of zeroes,
 * Windows is definitely not hibernated.  Any other case is treated as if
 * Windows is suspended.  This caters for the above mentioned caveat of a
 * system with many volumes where no "hibr" magic would be present and there is
 * no zero header.
 *
 * Return 0 if Windows is not hibernated on the volume, >0 if Windows is
 * hibernated on the volume, and -errno on error.
 */
static int check_windows_hibernation_status(struct ntfs_volume *vol)
{
	static const __le16 hiberfil[13] = { cpu_to_le16('h'),
			cpu_to_le16('i'), cpu_to_le16('b'),
			cpu_to_le16('e'), cpu_to_le16('r'),
			cpu_to_le16('f'), cpu_to_le16('i'),
			cpu_to_le16('l'), cpu_to_le16('.'),
			cpu_to_le16('s'), cpu_to_le16('y'),
			cpu_to_le16('s'), 0 };
	u64 mref;
	struct inode *vi;
	struct folio *folio;
	u32 *kaddr, *kend, *start_addr = NULL;
	struct ntfs_name *name = NULL;
	int ret = 1;

	ntfs_debug("Entering.");
	/*
	 * Find the inode number for the hibernation file by looking up the
	 * filename hiberfil.sys in the root directory.
	 */
	inode_lock(vol->root_ino);
	mref = ntfs_lookup_inode_by_name(NTFS_I(vol->root_ino), hiberfil, 12,
			&name);
	inode_unlock(vol->root_ino);
	kfree(name);
	if (IS_ERR_MREF(mref)) {
		ret = MREF_ERR(mref);
		/* If the file does not exist, Windows is not hibernated. */
		if (ret == -ENOENT) {
			ntfs_debug("hiberfil.sys not present.  Windows is not hibernated on the volume.");
			return 0;
		}
		/* A real error occurred. */
		ntfs_error(vol->sb, "Failed to find inode number for hiberfil.sys.");
		return ret;
	}
	/* Get the inode. */
	vi = ntfs_iget(vol->sb, MREF(mref));
	if (IS_ERR(vi)) {
		if (!IS_ERR(vi))
			iput(vi);
		ntfs_error(vol->sb, "Failed to load hiberfil.sys.");
		return IS_ERR(vi) ? PTR_ERR(vi) : -EIO;
	}
	if (unlikely(i_size_read(vi) < NTFS_HIBERFIL_HEADER_SIZE)) {
		ntfs_debug("hiberfil.sys is smaller than 4kiB (0x%llx).  Windows is hibernated on the volume.  This is not the system volume.",
				i_size_read(vi));
		goto iput_out;
	}

	folio = read_mapping_folio(vi->i_mapping, 0, NULL);
	if (IS_ERR(folio)) {
		ntfs_error(vol->sb, "Failed to read from hiberfil.sys.");
		ret = PTR_ERR(folio);
		goto iput_out;
	}
	start_addr = (u32 *)kmap_local_folio(folio, 0);
	kaddr = start_addr;
	if (*(__le32 *)kaddr == cpu_to_le32(0x72626968)/*'hibr'*/) {
		ntfs_debug("Magic \"hibr\" found in hiberfil.sys.  Windows is hibernated on the volume.  This is the system volume.");
		goto unm_iput_out;
	}
	kend = kaddr + NTFS_HIBERFIL_HEADER_SIZE/sizeof(*kaddr);
	do {
		if (unlikely(*kaddr)) {
			ntfs_debug("hiberfil.sys is larger than 4kiB (0x%llx), does not contain the \"hibr\" magic, and does not have a zero header.  Windows is hibernated on the volume.  This is not the system volume.",
					i_size_read(vi));
			goto unm_iput_out;
		}
	} while (++kaddr < kend);
	ntfs_debug("hiberfil.sys contains a zero header.  Windows is not hibernated on the volume.  This is the system volume.");
	ret = 0;
unm_iput_out:
	kunmap_local(start_addr);
	folio_put(folio);
iput_out:
	iput(vi);
	return ret;
}

/*
 * load_and_init_quota - load and setup the quota file for a volume if present
 * @vol:	ntfs super block describing device whose quota file to load
 *
 * Return 'true' on success or 'false' on error.  If $Quota is not present, we
 * leave vol->quota_ino as NULL and return success.
 */
static bool load_and_init_quota(struct ntfs_volume *vol)
{
	static const __le16 Quota[7] = { cpu_to_le16('$'),
			cpu_to_le16('Q'), cpu_to_le16('u'),
			cpu_to_le16('o'), cpu_to_le16('t'),
			cpu_to_le16('a'), 0 };
	static __le16 Q[3] = { cpu_to_le16('$'),
			cpu_to_le16('Q'), 0 };
	struct ntfs_name *name = NULL;
	u64 mref;
	struct inode *tmp_ino;

	ntfs_debug("Entering.");
	/*
	 * Find the inode number for the quota file by looking up the filename
	 * $Quota in the extended system files directory $Extend.
	 */
	inode_lock(vol->extend_ino);
	mref = ntfs_lookup_inode_by_name(NTFS_I(vol->extend_ino), Quota, 6,
			&name);
	inode_unlock(vol->extend_ino);
	kfree(name);
	if (IS_ERR_MREF(mref)) {
		/*
		 * If the file does not exist, quotas are disabled and have
		 * never been enabled on this volume, just return success.
		 */
		if (MREF_ERR(mref) == -ENOENT) {
			ntfs_debug("$Quota not present.  Volume does not have quotas enabled.");
			/*
			 * No need to try to set quotas out of date if they are
			 * not enabled.
			 */
			NVolSetQuotaOutOfDate(vol);
			return true;
		}
		/* A real error occurred. */
		ntfs_error(vol->sb, "Failed to find inode number for $Quota.");
		return false;
	}
	/* Get the inode. */
	tmp_ino = ntfs_iget(vol->sb, MREF(mref));
	if (IS_ERR(tmp_ino)) {
		if (!IS_ERR(tmp_ino))
			iput(tmp_ino);
		ntfs_error(vol->sb, "Failed to load $Quota.");
		return false;
	}
	vol->quota_ino = tmp_ino;
	/* Get the $Q index allocation attribute. */
	tmp_ino = ntfs_index_iget(vol->quota_ino, Q, 2);
	if (IS_ERR(tmp_ino)) {
		ntfs_error(vol->sb, "Failed to load $Quota/$Q index.");
		return false;
	}
	vol->quota_q_ino = tmp_ino;
	ntfs_debug("Done.");
	return true;
}

/*
 * load_and_init_attrdef - load the attribute definitions table for a volume
 * @vol:	ntfs super block describing device whose attrdef to load
 *
 * Return 'true' on success or 'false' on error.
 */
static bool load_and_init_attrdef(struct ntfs_volume *vol)
{
	loff_t i_size;
	struct super_block *sb = vol->sb;
	struct inode *ino;
	struct folio *folio;
	u8 *addr;
	pgoff_t index, max_index;
	unsigned int size;

	ntfs_debug("Entering.");
	/* Read attrdef table and setup vol->attrdef and vol->attrdef_size. */
	ino = ntfs_iget(sb, FILE_AttrDef);
	if (IS_ERR(ino)) {
		if (!IS_ERR(ino))
			iput(ino);
		goto failed;
	}
	NInoSetSparseDisabled(NTFS_I(ino));
	/* The size of FILE_AttrDef must be above 0 and fit inside 31 bits. */
	i_size = i_size_read(ino);
	if (i_size <= 0 || i_size > 0x7fffffff)
		goto iput_failed;
	vol->attrdef = kvzalloc(i_size, GFP_NOFS);
	if (!vol->attrdef)
		goto iput_failed;
	index = 0;
	max_index = i_size >> PAGE_SHIFT;
	size = PAGE_SIZE;
	while (index < max_index) {
		/* Read the attrdef table and copy it into the linear buffer. */
read_partial_attrdef_page:
		folio = read_mapping_folio(ino->i_mapping, index, NULL);
		if (IS_ERR(folio))
			goto free_iput_failed;
		addr = kmap_local_folio(folio, 0);
		memcpy((u8 *)vol->attrdef + (index++ << PAGE_SHIFT),
				addr, size);
		kunmap_local(addr);
		folio_put(folio);
	}
	if (size == PAGE_SIZE) {
		size = i_size & ~PAGE_MASK;
		if (size)
			goto read_partial_attrdef_page;
	}
	vol->attrdef_size = i_size;
	ntfs_debug("Read %llu bytes from $AttrDef.", i_size);
	iput(ino);
	return true;
free_iput_failed:
	kvfree(vol->attrdef);
	vol->attrdef = NULL;
iput_failed:
	iput(ino);
failed:
	ntfs_error(sb, "Failed to initialize attribute definition table.");
	return false;
}

/*
 * load_and_init_upcase - load the upcase table for an ntfs volume
 * @vol:	ntfs super block describing device whose upcase to load
 *
 * Return 'true' on success or 'false' on error.
 */
static bool load_and_init_upcase(struct ntfs_volume *vol)
{
	loff_t i_size;
	struct super_block *sb = vol->sb;
	struct inode *ino;
	struct folio *folio;
	u8 *addr;
	pgoff_t index, max_index;
	unsigned int size;
	int i, max;

	ntfs_debug("Entering.");
	/* Read upcase table and setup vol->upcase and vol->upcase_len. */
	ino = ntfs_iget(sb, FILE_UpCase);
	if (IS_ERR(ino)) {
		if (!IS_ERR(ino))
			iput(ino);
		goto upcase_failed;
	}
	/*
	 * The upcase size must not be above 64k Unicode characters, must not
	 * be zero and must be a multiple of sizeof(__le16).
	 */
	i_size = i_size_read(ino);
	if (!i_size || i_size & (sizeof(__le16) - 1) ||
			i_size > 64ULL * 1024 * sizeof(__le16))
		goto iput_upcase_failed;
	vol->upcase = kvzalloc(i_size, GFP_NOFS);
	if (!vol->upcase)
		goto iput_upcase_failed;
	index = 0;
	max_index = i_size >> PAGE_SHIFT;
	size = PAGE_SIZE;
	while (index < max_index) {
		/* Read the upcase table and copy it into the linear buffer. */
read_partial_upcase_page:
		folio = read_mapping_folio(ino->i_mapping, index, NULL);
		if (IS_ERR(folio))
			goto iput_upcase_failed;
		addr = kmap_local_folio(folio, 0);
		memcpy((char *)vol->upcase + (index++ << PAGE_SHIFT),
				addr, size);
		kunmap_local(addr);
		folio_put(folio);
	}
	if (size == PAGE_SIZE) {
		size = i_size & ~PAGE_MASK;
		if (size)
			goto read_partial_upcase_page;
	}
	vol->upcase_len = i_size >> sizeof(unsigned char);
	ntfs_debug("Read %llu bytes from $UpCase (expected %zu bytes).",
			i_size, 64 * 1024 * sizeof(__le16));
	iput(ino);
	mutex_lock(&ntfs_lock);
	if (!default_upcase) {
		ntfs_debug("Using volume specified $UpCase since default is not present.");
		mutex_unlock(&ntfs_lock);
		return true;
	}
	max = default_upcase_len;
	if (max > vol->upcase_len)
		max = vol->upcase_len;
	for (i = 0; i < max; i++)
		if (vol->upcase[i] != default_upcase[i])
			break;
	if (i == max) {
		kvfree(vol->upcase);
		vol->upcase = default_upcase;
		vol->upcase_len = max;
		ntfs_nr_upcase_users++;
		mutex_unlock(&ntfs_lock);
		ntfs_debug("Volume specified $UpCase matches default. Using default.");
		return true;
	}
	mutex_unlock(&ntfs_lock);
	ntfs_debug("Using volume specified $UpCase since it does not match the default.");
	return true;
iput_upcase_failed:
	iput(ino);
	kvfree(vol->upcase);
	vol->upcase = NULL;
upcase_failed:
	mutex_lock(&ntfs_lock);
	if (default_upcase) {
		vol->upcase = default_upcase;
		vol->upcase_len = default_upcase_len;
		ntfs_nr_upcase_users++;
		mutex_unlock(&ntfs_lock);
		ntfs_error(sb, "Failed to load $UpCase from the volume. Using default.");
		return true;
	}
	mutex_unlock(&ntfs_lock);
	ntfs_error(sb, "Failed to initialize upcase table.");
	return false;
}

/*
 * The lcn and mft bitmap inodes are NTFS-internal inodes with
 * their own special locking rules:
 */
static struct lock_class_key
	lcnbmp_runlist_lock_key, lcnbmp_mrec_lock_key,
	mftbmp_runlist_lock_key, mftbmp_mrec_lock_key;

/*
 * load_system_files - open the system files using normal functions
 * @vol:	ntfs super block describing device whose system files to load
 *
 * Open the system files with normal access functions and complete setting up
 * the ntfs super block @vol.
 *
 * Return 'true' on success or 'false' on error.
 */
static bool load_system_files(struct ntfs_volume *vol)
{
	struct super_block *sb = vol->sb;
	struct mft_record *m;
	struct volume_information *vi;
	struct ntfs_attr_search_ctx *ctx;
	struct restart_page_header *rp;
	int err;

	ntfs_debug("Entering.");
	/* Get mft mirror inode compare the contents of $MFT and $MFTMirr. */
	if (!load_and_init_mft_mirror(vol) || !check_mft_mirror(vol)) {
		/* If a read-write mount, convert it to a read-only mount. */
		if (!sb_rdonly(sb) && vol->on_errors == ON_ERRORS_REMOUNT_RO) {
			static const char *es1 = "Failed to load $MFTMirr";
			static const char *es2 = "$MFTMirr does not match $MFT";
			static const char *es3 = ".  Run ntfsck and/or chkdsk.";

			sb->s_flags |= SB_RDONLY;
			ntfs_error(sb, "%s.  Mounting read-only%s",
					!vol->mftmirr_ino ? es1 : es2, es3);
		}
		NVolSetErrors(vol);
	}
	/* Get mft bitmap attribute inode. */
	vol->mftbmp_ino = ntfs_attr_iget(vol->mft_ino, AT_BITMAP, NULL, 0);
	if (IS_ERR(vol->mftbmp_ino)) {
		ntfs_error(sb, "Failed to load $MFT/$BITMAP attribute.");
		goto iput_mirr_err_out;
	}
	lockdep_set_class(&NTFS_I(vol->mftbmp_ino)->runlist.lock,
			   &mftbmp_runlist_lock_key);
	lockdep_set_class(&NTFS_I(vol->mftbmp_ino)->mrec_lock,
			   &mftbmp_mrec_lock_key);
	/* Read upcase table and setup @vol->upcase and @vol->upcase_len. */
	if (!load_and_init_upcase(vol))
		goto iput_mftbmp_err_out;
	/*
	 * Read attribute definitions table and setup @vol->attrdef and
	 * @vol->attrdef_size.
	 */
	if (!load_and_init_attrdef(vol))
		goto iput_upcase_err_out;
	/*
	 * Get the cluster allocation bitmap inode and verify the size, no
	 * need for any locking at this stage as we are already running
	 * exclusively as we are mount in progress task.
	 */
	vol->lcnbmp_ino = ntfs_iget(sb, FILE_Bitmap);
	if (IS_ERR(vol->lcnbmp_ino)) {
		if (!IS_ERR(vol->lcnbmp_ino))
			iput(vol->lcnbmp_ino);
		goto bitmap_failed;
	}
	lockdep_set_class(&NTFS_I(vol->lcnbmp_ino)->runlist.lock,
			   &lcnbmp_runlist_lock_key);
	lockdep_set_class(&NTFS_I(vol->lcnbmp_ino)->mrec_lock,
			   &lcnbmp_mrec_lock_key);

	NInoSetSparseDisabled(NTFS_I(vol->lcnbmp_ino));
	if ((vol->nr_clusters + 7) >> 3 > i_size_read(vol->lcnbmp_ino)) {
		iput(vol->lcnbmp_ino);
bitmap_failed:
		ntfs_error(sb, "Failed to load $Bitmap.");
		goto iput_attrdef_err_out;
	}
	/*
	 * Get the volume inode and setup our cache of the volume flags and
	 * version.
	 */
	vol->vol_ino = ntfs_iget(sb, FILE_Volume);
	if (IS_ERR(vol->vol_ino)) {
		if (!IS_ERR(vol->vol_ino))
			iput(vol->vol_ino);
volume_failed:
		ntfs_error(sb, "Failed to load $Volume.");
		goto iput_lcnbmp_err_out;
	}
	m = map_mft_record(NTFS_I(vol->vol_ino));
	if (IS_ERR(m)) {
iput_volume_failed:
		iput(vol->vol_ino);
		goto volume_failed;
	}

	ctx = ntfs_attr_get_search_ctx(NTFS_I(vol->vol_ino), m);
	if (!ctx) {
		ntfs_error(sb, "Failed to get attribute search context.");
		goto get_ctx_vol_failed;
	}

	if (!ntfs_attr_lookup(AT_VOLUME_NAME, NULL, 0, 0, 0, NULL, 0, ctx) &&
	    !ctx->attr->non_resident &&
	    !(ctx->attr->flags & (ATTR_IS_SPARSE | ATTR_IS_COMPRESSED)) &&
	    le32_to_cpu(ctx->attr->data.resident.value_length) > 0) {
		err = ntfs_ucstonls(vol, (__le16 *)((u8 *)ctx->attr +
				    le16_to_cpu(ctx->attr->data.resident.value_offset)),
				    le32_to_cpu(ctx->attr->data.resident.value_length) / 2,
				    &vol->volume_label, NTFS_MAX_LABEL_LEN);
		if (err < 0)
			vol->volume_label = NULL;
	}

	if (ntfs_attr_lookup(AT_VOLUME_INFORMATION, NULL, 0, 0, 0, NULL, 0,
			ctx) || ctx->attr->non_resident || ctx->attr->flags) {
err_put_vol:
		ntfs_attr_put_search_ctx(ctx);
get_ctx_vol_failed:
		unmap_mft_record(NTFS_I(vol->vol_ino));
		goto iput_volume_failed;
	}
	vi = (struct volume_information *)((char *)ctx->attr +
			le16_to_cpu(ctx->attr->data.resident.value_offset));
	/* Some bounds checks. */
	if ((u8 *)vi < (u8 *)ctx->attr || (u8 *)vi +
			le32_to_cpu(ctx->attr->data.resident.value_length) >
			(u8 *)ctx->attr + le32_to_cpu(ctx->attr->length))
		goto err_put_vol;
	/* Copy the volume flags and version to the struct ntfs_volume structure. */
	vol->vol_flags = vi->flags;
	vol->major_ver = vi->major_ver;
	vol->minor_ver = vi->minor_ver;
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(NTFS_I(vol->vol_ino));
	pr_info("volume version %i.%i, dev %s, cluster size %d\n",
		vol->major_ver, vol->minor_ver, sb->s_id, vol->cluster_size);

	/* Make sure that no unsupported volume flags are set. */
	if (vol->vol_flags & VOLUME_MUST_MOUNT_RO_MASK) {
		static const char *es1a = "Volume is dirty";
		static const char *es1b = "Volume has been modified by chkdsk";
		static const char *es1c = "Volume has unsupported flags set";
		static const char *es2a = ".  Run chkdsk and mount in Windows.";
		static const char *es2b = ".  Mount in Windows.";
		const char *es1, *es2;

		es2 = es2a;
		if (vol->vol_flags & VOLUME_IS_DIRTY)
			es1 = es1a;
		else if (vol->vol_flags & VOLUME_MODIFIED_BY_CHKDSK) {
			es1 = es1b;
			es2 = es2b;
		} else {
			es1 = es1c;
			ntfs_warning(sb, "Unsupported volume flags 0x%x encountered.",
					(unsigned int)le16_to_cpu(vol->vol_flags));
		}
		/* If a read-write mount, convert it to a read-only mount. */
		if (!sb_rdonly(sb) && vol->on_errors == ON_ERRORS_REMOUNT_RO) {
			sb->s_flags |= SB_RDONLY;
			ntfs_error(sb, "%s.  Mounting read-only%s", es1, es2);
		}
		/*
		 * Do not set NVolErrors() because ntfs_remount() re-checks the
		 * flags which we need to do in case any flags have changed.
		 */
	}
	/*
	 * Get the inode for the logfile, check it and determine if the volume
	 * was shutdown cleanly.
	 */
	rp = NULL;
	err = load_and_check_logfile(vol, &rp);
	if (err) {
		/* If a read-write mount, convert it to a read-only mount. */
		if (!sb_rdonly(sb) && vol->on_errors == ON_ERRORS_REMOUNT_RO) {
			sb->s_flags |= SB_RDONLY;
			ntfs_error(sb, "Failed to load LogFile. Mounting read-only.");
		}
		NVolSetErrors(vol);
	}

	kvfree(rp);
	/* Get the root directory inode so we can do path lookups. */
	vol->root_ino = ntfs_iget(sb, FILE_root);
	if (IS_ERR(vol->root_ino)) {
		if (!IS_ERR(vol->root_ino))
			iput(vol->root_ino);
		ntfs_error(sb, "Failed to load root directory.");
		goto iput_logfile_err_out;
	}
	/*
	 * Check if Windows is suspended to disk on the target volume.  If it
	 * is hibernated, we must not write *anything* to the disk so set
	 * NVolErrors() without setting the dirty volume flag and mount
	 * read-only.  This will prevent read-write remounting and it will also
	 * prevent all writes.
	 */
	err = check_windows_hibernation_status(vol);
	if (unlikely(err)) {
		static const char *es1a = "Failed to determine if Windows is hibernated";
		static const char *es1b = "Windows is hibernated";
		static const char *es2 = ".  Run chkdsk.";
		const char *es1;

		es1 = err < 0 ? es1a : es1b;
		/* If a read-write mount, convert it to a read-only mount. */
		if (!sb_rdonly(sb) && vol->on_errors == ON_ERRORS_REMOUNT_RO) {
			sb->s_flags |= SB_RDONLY;
			ntfs_error(sb, "%s.  Mounting read-only%s", es1, es2);
		}
		NVolSetErrors(vol);
	}

	/* If (still) a read-write mount, empty the logfile. */
	if (!sb_rdonly(sb) &&
	    vol->logfile_ino && !ntfs_empty_logfile(vol->logfile_ino) &&
	    vol->on_errors == ON_ERRORS_REMOUNT_RO) {
		static const char *es1 = "Failed to empty LogFile";
		static const char *es2 = ".  Mount in Windows.";

		/* Convert to a read-only mount. */
		ntfs_error(sb, "%s.  Mounting read-only%s", es1, es2);
		sb->s_flags |= SB_RDONLY;
		NVolSetErrors(vol);
	}
	/* If on NTFS versions before 3.0, we are done. */
	if (unlikely(vol->major_ver < 3))
		return true;
	/* NTFS 3.0+ specific initialization. */
	/* Get the security descriptors inode. */
	vol->secure_ino = ntfs_iget(sb, FILE_Secure);
	if (IS_ERR(vol->secure_ino)) {
		if (!IS_ERR(vol->secure_ino))
			iput(vol->secure_ino);
		ntfs_error(sb, "Failed to load $Secure.");
		goto iput_root_err_out;
	}
	/* Get the extended system files' directory inode. */
	vol->extend_ino = ntfs_iget(sb, FILE_Extend);
	if (IS_ERR(vol->extend_ino) ||
	    !S_ISDIR(vol->extend_ino->i_mode)) {
		if (!IS_ERR(vol->extend_ino))
			iput(vol->extend_ino);
		ntfs_error(sb, "Failed to load $Extend.");
		goto iput_sec_err_out;
	}
	/* Find the quota file, load it if present, and set it up. */
	if (!load_and_init_quota(vol) &&
	    vol->on_errors == ON_ERRORS_REMOUNT_RO) {
		static const char *es1 = "Failed to load $Quota";
		static const char *es2 = ".  Run chkdsk.";

		sb->s_flags |= SB_RDONLY;
		ntfs_error(sb, "%s.  Mounting read-only%s", es1, es2);
		/* This will prevent a read-write remount. */
		NVolSetErrors(vol);
	}

	return true;

iput_sec_err_out:
	iput(vol->secure_ino);
iput_root_err_out:
	iput(vol->root_ino);
iput_logfile_err_out:
	if (vol->logfile_ino)
		iput(vol->logfile_ino);
	iput(vol->vol_ino);
iput_lcnbmp_err_out:
	iput(vol->lcnbmp_ino);
iput_attrdef_err_out:
	vol->attrdef_size = 0;
	if (vol->attrdef) {
		kvfree(vol->attrdef);
		vol->attrdef = NULL;
	}
iput_upcase_err_out:
	vol->upcase_len = 0;
	mutex_lock(&ntfs_lock);
	if (vol->upcase == default_upcase) {
		ntfs_nr_upcase_users--;
		vol->upcase = NULL;
	}
	mutex_unlock(&ntfs_lock);
	if (vol->upcase) {
		kvfree(vol->upcase);
		vol->upcase = NULL;
	}
iput_mftbmp_err_out:
	iput(vol->mftbmp_ino);
iput_mirr_err_out:
	iput(vol->mftmirr_ino);
	return false;
}

static void ntfs_volume_free(struct ntfs_volume *vol)
{
	/* Throw away the table of attribute definitions. */
	vol->attrdef_size = 0;
	if (vol->attrdef) {
		kvfree(vol->attrdef);
		vol->attrdef = NULL;
	}
	vol->upcase_len = 0;
	/*
	 * Destroy the global default upcase table if necessary.  Also decrease
	 * the number of upcase users if we are a user.
	 */
	mutex_lock(&ntfs_lock);
	if (vol->upcase == default_upcase) {
		ntfs_nr_upcase_users--;
		vol->upcase = NULL;
	}

	if (!ntfs_nr_upcase_users && default_upcase) {
		kvfree(default_upcase);
		default_upcase = NULL;
	}

	free_compression_buffers();

	mutex_unlock(&ntfs_lock);
	if (vol->upcase) {
		kvfree(vol->upcase);
		vol->upcase = NULL;
	}

	unload_nls(vol->nls_map);

	if (vol->lcn_empty_bits_per_page)
		kvfree(vol->lcn_empty_bits_per_page);
	kfree(vol->volume_label);
	kfree(vol);
}

/*
 * ntfs_put_super - called by the vfs to unmount a volume
 * @sb:		vfs superblock of volume to unmount
 */
static void ntfs_put_super(struct super_block *sb)
{
	struct ntfs_volume *vol = NTFS_SB(sb);

	pr_info("Entering %s, dev %s\n", __func__, sb->s_id);

	cancel_work_sync(&vol->precalc_work);

	/*
	 * Commit all inodes while they are still open in case some of them
	 * cause others to be dirtied.
	 */
	ntfs_commit_inode(vol->vol_ino);

	/* NTFS 3.0+ specific. */
	if (vol->major_ver >= 3) {
		if (vol->quota_q_ino)
			ntfs_commit_inode(vol->quota_q_ino);
		if (vol->quota_ino)
			ntfs_commit_inode(vol->quota_ino);
		if (vol->extend_ino)
			ntfs_commit_inode(vol->extend_ino);
		if (vol->secure_ino)
			ntfs_commit_inode(vol->secure_ino);
	}

	ntfs_commit_inode(vol->root_ino);

	ntfs_commit_inode(vol->lcnbmp_ino);

	/*
	 * the GFP_NOFS scope is not needed because ntfs_commit_inode
	 * does nothing
	 */
	ntfs_commit_inode(vol->mftbmp_ino);

	if (vol->logfile_ino)
		ntfs_commit_inode(vol->logfile_ino);

	if (vol->mftmirr_ino)
		ntfs_commit_inode(vol->mftmirr_ino);
	ntfs_commit_inode(vol->mft_ino);

	/*
	 * If a read-write mount and no volume errors have occurred, mark the
	 * volume clean.  Also, re-commit all affected inodes.
	 */
	if (!sb_rdonly(sb)) {
		if (!NVolErrors(vol)) {
			if (ntfs_clear_volume_flags(vol, VOLUME_IS_DIRTY))
				ntfs_warning(sb,
					"Failed to clear dirty bit in volume information flags.  Run chkdsk.");
			ntfs_commit_inode(vol->vol_ino);
			ntfs_commit_inode(vol->root_ino);
			if (vol->mftmirr_ino)
				ntfs_commit_inode(vol->mftmirr_ino);
			ntfs_commit_inode(vol->mft_ino);
		} else {
			ntfs_warning(sb,
				"Volume has errors.  Leaving volume marked dirty.  Run chkdsk.");
		}
	}

	iput(vol->vol_ino);
	vol->vol_ino = NULL;

	/* NTFS 3.0+ specific clean up. */
	if (vol->major_ver >= 3) {
		if (vol->quota_q_ino) {
			iput(vol->quota_q_ino);
			vol->quota_q_ino = NULL;
		}
		if (vol->quota_ino) {
			iput(vol->quota_ino);
			vol->quota_ino = NULL;
		}
		if (vol->extend_ino) {
			iput(vol->extend_ino);
			vol->extend_ino = NULL;
		}
		if (vol->secure_ino) {
			iput(vol->secure_ino);
			vol->secure_ino = NULL;
		}
	}

	iput(vol->root_ino);
	vol->root_ino = NULL;

	iput(vol->lcnbmp_ino);
	vol->lcnbmp_ino = NULL;

	iput(vol->mftbmp_ino);
	vol->mftbmp_ino = NULL;

	if (vol->logfile_ino) {
		iput(vol->logfile_ino);
		vol->logfile_ino = NULL;
	}
	if (vol->mftmirr_ino) {
		/* Re-commit the mft mirror and mft just in case. */
		ntfs_commit_inode(vol->mftmirr_ino);
		ntfs_commit_inode(vol->mft_ino);
		iput(vol->mftmirr_ino);
		vol->mftmirr_ino = NULL;
	}
	/*
	 * We should have no dirty inodes left, due to
	 * mft.c::ntfs_mft_writepage() cleaning all the dirty pages as
	 * the underlying mft records are written out and cleaned.
	 */
	ntfs_commit_inode(vol->mft_ino);
	write_inode_now(vol->mft_ino, 1);

	iput(vol->mft_ino);
	vol->mft_ino = NULL;
	blkdev_issue_flush(sb->s_bdev);

	ntfs_volume_free(vol);
}

int ntfs_force_shutdown(struct super_block *sb, u32 flags)
{
	struct ntfs_volume *vol = NTFS_SB(sb);
	int ret;

	if (NVolShutdown(vol))
		return 0;

	switch (flags) {
	case FS_SHUTDOWN_FLAGS_DEFAULT:
	case FS_SHUTDOWN_FLAGS_LOGFLUSH:
		ret = bdev_freeze(sb->s_bdev);
		if (ret)
			return ret;
		bdev_thaw(sb->s_bdev);
		NVolSetShutdown(vol);
		break;
	case FS_SHUTDOWN_FLAGS_NOLOGFLUSH:
		NVolSetShutdown(vol);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void ntfs_shutdown(struct super_block *sb)
{
	ntfs_force_shutdown(sb, FS_SHUTDOWN_FLAGS_NOLOGFLUSH);

}

static int ntfs_sync_fs(struct super_block *sb, int wait)
{
	struct ntfs_volume *vol = NTFS_SB(sb);
	int err = 0;

	if (NVolShutdown(vol))
		return -EIO;

	if (!wait)
		return 0;

	/* If there are some dirty buffers in the bdev inode */
	if (ntfs_clear_volume_flags(vol, VOLUME_IS_DIRTY)) {
		ntfs_warning(sb, "Failed to clear dirty bit in volume information flags.  Run chkdsk.");
		err = -EIO;
	}
	sync_inodes_sb(sb);
	sync_blockdev(sb->s_bdev);
	blkdev_issue_flush(sb->s_bdev);
	return err;
}

/*
 * get_nr_free_clusters - return the number of free clusters on a volume
 * @vol:	ntfs volume for which to obtain free cluster count
 *
 * Calculate the number of free clusters on the mounted NTFS volume @vol. We
 * actually calculate the number of clusters in use instead because this
 * allows us to not care about partial pages as these will be just zero filled
 * and hence not be counted as allocated clusters.
 *
 * The only particularity is that clusters beyond the end of the logical ntfs
 * volume will be marked as allocated to prevent errors which means we have to
 * discount those at the end. This is important as the cluster bitmap always
 * has a size in multiples of 8 bytes, i.e. up to 63 clusters could be outside
 * the logical volume and marked in use when they are not as they do not exist.
 *
 * If any pages cannot be read we assume all clusters in the erroring pages are
 * in use. This means we return an underestimate on errors which is better than
 * an overestimate.
 */
s64 get_nr_free_clusters(struct ntfs_volume *vol)
{
	s64 nr_free = vol->nr_clusters;
	u32 nr_used;
	struct address_space *mapping = vol->lcnbmp_ino->i_mapping;
	struct folio *folio;
	pgoff_t index, max_index;
	struct file_ra_state *ra;

	ntfs_debug("Entering.");
	/* Serialize accesses to the cluster bitmap. */

	if (NVolFreeClusterKnown(vol))
		return atomic64_read(&vol->free_clusters);

	ra = kzalloc(sizeof(*ra), GFP_NOFS);
	if (!ra)
		return 0;

	file_ra_state_init(ra, mapping);

	/*
	 * Convert the number of bits into bytes rounded up, then convert into
	 * multiples of PAGE_SIZE, rounding up so that if we have one
	 * full and one partial page max_index = 2.
	 */
	max_index = (((vol->nr_clusters + 7) >> 3) + PAGE_SIZE - 1) >>
			PAGE_SHIFT;
	/* Use multiples of 4 bytes, thus max_size is PAGE_SIZE / 4. */
	ntfs_debug("Reading $Bitmap, max_index = 0x%lx, max_size = 0x%lx.",
			max_index, PAGE_SIZE / 4);
	for (index = 0; index < max_index; index++) {
		unsigned long *kaddr;

		/*
		 * Get folio from page cache, getting it from backing store
		 * if necessary, and increment the use count.
		 */
		folio = ntfs_get_locked_folio(mapping, index, max_index, ra);

		/* Ignore pages which errored synchronously. */
		if (IS_ERR(folio)) {
			ntfs_debug("Skipping page (index 0x%lx).", index);
			nr_free -= PAGE_SIZE * 8;
			vol->lcn_empty_bits_per_page[index] = 0;
			continue;
		}

		kaddr = kmap_local_folio(folio, 0);
		/*
		 * Subtract the number of set bits. If this
		 * is the last page and it is partial we don't really care as
		 * it just means we do a little extra work but it won't affect
		 * the result as all out of range bytes are set to zero by
		 * ntfs_readpage().
		 */
		nr_used = bitmap_weight(kaddr, PAGE_SIZE * BITS_PER_BYTE);
		nr_free -= nr_used;
		vol->lcn_empty_bits_per_page[index] = PAGE_SIZE * BITS_PER_BYTE - nr_used;
		kunmap_local(kaddr);
		folio_unlock(folio);
		folio_put(folio);
	}
	ntfs_debug("Finished reading $Bitmap, last index = 0x%lx.", index - 1);
	/*
	 * Fixup for eventual bits outside logical ntfs volume (see function
	 * description above).
	 */
	if (vol->nr_clusters & 63)
		nr_free += 64 - (vol->nr_clusters & 63);

	/* If errors occurred we may well have gone below zero, fix this. */
	if (nr_free < 0)
		nr_free = 0;
	else
		atomic64_set(&vol->free_clusters, nr_free);

	kfree(ra);
	NVolSetFreeClusterKnown(vol);
	wake_up_all(&vol->free_waitq);
	ntfs_debug("Exiting.");
	return nr_free;
}

/*
 * @nr_clusters is the number of clusters requested for allocation.
 *
 * Return the number of clusters available for allocation within
 * the range of @nr_clusters, which is counts that considered
 * for delayed allocation.
 */
s64 ntfs_available_clusters_count(struct ntfs_volume *vol, s64 nr_clusters)
{
	s64 free_clusters;

	/* wait event */
	if (!NVolFreeClusterKnown(vol))
		wait_event(vol->free_waitq, NVolFreeClusterKnown(vol));

	free_clusters = atomic64_read(&vol->free_clusters) -
		atomic64_read(&vol->dirty_clusters);
	if (free_clusters <= 0)
		return -ENOSPC;
	else if (free_clusters < nr_clusters)
		nr_clusters = free_clusters;

	return nr_clusters;
}

/*
 * __get_nr_free_mft_records - return the number of free inodes on a volume
 * @vol:	ntfs volume for which to obtain free inode count
 * @nr_free:	number of mft records in filesystem
 * @max_index:	maximum number of pages containing set bits
 *
 * Calculate the number of free mft records (inodes) on the mounted NTFS
 * volume @vol. We actually calculate the number of mft records in use instead
 * because this allows us to not care about partial pages as these will be just
 * zero filled and hence not be counted as allocated mft record.
 *
 * If any pages cannot be read we assume all mft records in the erroring pages
 * are in use. This means we return an underestimate on errors which is better
 * than an overestimate.
 *
 * NOTE: Caller must hold mftbmp_lock rw_semaphore for reading or writing.
 */
static unsigned long __get_nr_free_mft_records(struct ntfs_volume *vol,
		s64 nr_free, const pgoff_t max_index)
{
	struct address_space *mapping = vol->mftbmp_ino->i_mapping;
	struct folio *folio;
	pgoff_t index;
	struct file_ra_state *ra;

	ntfs_debug("Entering.");

	ra = kzalloc(sizeof(*ra), GFP_NOFS);
	if (!ra)
		return 0;

	file_ra_state_init(ra, mapping);

	/* Use multiples of 4 bytes, thus max_size is PAGE_SIZE / 4. */
	ntfs_debug("Reading $MFT/$BITMAP, max_index = 0x%lx, max_size = 0x%lx.",
			max_index, PAGE_SIZE / 4);
	for (index = 0; index < max_index; index++) {
		unsigned long *kaddr;

		/*
		 * Get folio from page cache, getting it from backing store
		 * if necessary, and increment the use count.
		 */
		folio = ntfs_get_locked_folio(mapping, index, max_index, ra);

		/* Ignore pages which errored synchronously. */
		if (IS_ERR(folio)) {
			ntfs_debug("read_mapping_page() error. Skipping page (index 0x%lx).",
					index);
			nr_free -= PAGE_SIZE * 8;
			continue;
		}

		kaddr = kmap_local_folio(folio, 0);
		/*
		 * Subtract the number of set bits. If this
		 * is the last page and it is partial we don't really care as
		 * it just means we do a little extra work but it won't affect
		 * the result as all out of range bytes are set to zero by
		 * ntfs_readpage().
		 */
		nr_free -= bitmap_weight(kaddr,
					PAGE_SIZE * BITS_PER_BYTE);
		kunmap_local(kaddr);
		folio_unlock(folio);
		folio_put(folio);
	}
	ntfs_debug("Finished reading $MFT/$BITMAP, last index = 0x%lx.",
			index - 1);
	/* If errors occurred we may well have gone below zero, fix this. */
	if (nr_free < 0)
		nr_free = 0;
	else
		atomic64_set(&vol->free_mft_records, nr_free);

	kfree(ra);
	ntfs_debug("Exiting.");
	return nr_free;
}

/*
 * ntfs_statfs - return information about mounted NTFS volume
 * @dentry:	dentry from mounted volume
 * @sfs:	statfs structure in which to return the information
 *
 * Return information about the mounted NTFS volume @dentry in the statfs structure
 * pointed to by @sfs (this is initialized with zeros before ntfs_statfs is
 * called). We interpret the values to be correct of the moment in time at
 * which we are called. Most values are variable otherwise and this isn't just
 * the free values but the totals as well. For example we can increase the
 * total number of file nodes if we run out and we can keep doing this until
 * there is no more space on the volume left at all.
 *
 * Called from vfs_statfs which is used to handle the statfs, fstatfs, and
 * ustat system calls.
 *
 * Return 0 on success or -errno on error.
 */
static int ntfs_statfs(struct dentry *dentry, struct kstatfs *sfs)
{
	struct super_block *sb = dentry->d_sb;
	s64 size;
	struct ntfs_volume *vol = NTFS_SB(sb);
	struct ntfs_inode *mft_ni = NTFS_I(vol->mft_ino);
	unsigned long flags;

	ntfs_debug("Entering.");
	/* Type of filesystem. */
	sfs->f_type   = NTFS_SB_MAGIC;
	/* Optimal transfer block size. */
	sfs->f_bsize = vol->cluster_size;
	/* Fundamental file system block size, used as the unit. */
	sfs->f_frsize = vol->cluster_size;

	/*
	 * Total data blocks in filesystem in units of f_bsize and since
	 * inodes are also stored in data blocs ($MFT is a file) this is just
	 * the total clusters.
	 */
	sfs->f_blocks = vol->nr_clusters;

	/* wait event */
	if (!NVolFreeClusterKnown(vol))
		wait_event(vol->free_waitq, NVolFreeClusterKnown(vol));

	/* Free data blocks in filesystem in units of f_bsize. */
	size = atomic64_read(&vol->free_clusters) -
		atomic64_read(&vol->dirty_clusters);
	if (size < 0LL)
		size = 0LL;

	/* Free blocks avail to non-superuser, same as above on NTFS. */
	sfs->f_bavail = sfs->f_bfree = size;

	/* Number of inodes in filesystem (at this point in time). */
	read_lock_irqsave(&mft_ni->size_lock, flags);
	sfs->f_files = i_size_read(vol->mft_ino) >> vol->mft_record_size_bits;
	read_unlock_irqrestore(&mft_ni->size_lock, flags);

	/* Free inodes in fs (based on current total count). */
	sfs->f_ffree = atomic64_read(&vol->free_mft_records);

	/*
	 * File system id. This is extremely *nix flavour dependent and even
	 * within Linux itself all fs do their own thing. I interpret this to
	 * mean a unique id associated with the mounted fs and not the id
	 * associated with the filesystem driver, the latter is already given
	 * by the filesystem type in sfs->f_type. Thus we use the 64-bit
	 * volume serial number splitting it into two 32-bit parts. We enter
	 * the least significant 32-bits in f_fsid[0] and the most significant
	 * 32-bits in f_fsid[1].
	 */
	sfs->f_fsid = u64_to_fsid(vol->serial_no);
	/* Maximum length of filenames. */
	sfs->f_namelen	   = NTFS_MAX_NAME_LEN;

	return 0;
}

static int ntfs_write_inode(struct inode *vi, struct writeback_control *wbc)
{
	return __ntfs_write_inode(vi, wbc->sync_mode == WB_SYNC_ALL);
}

/*
 * The complete super operations.
 */
static const struct super_operations ntfs_sops = {
	.alloc_inode	= ntfs_alloc_big_inode,	  /* VFS: Allocate new inode. */
	.free_inode	= ntfs_free_big_inode, /* VFS: Deallocate inode. */
	.drop_inode	= ntfs_drop_big_inode,
	.write_inode	= ntfs_write_inode,	/* VFS: Write dirty inode to disk. */
	.put_super	= ntfs_put_super,	/* Syscall: umount. */
	.shutdown	= ntfs_shutdown,
	.sync_fs	= ntfs_sync_fs,		/* Syscall: sync. */
	.statfs		= ntfs_statfs,		/* Syscall: statfs */
	.evict_inode	= ntfs_evict_big_inode,
	.show_options	= ntfs_show_options,	/* Show mount options in proc. */
};

static void precalc_free_clusters(struct work_struct *work)
{
	struct ntfs_volume *vol = container_of(work, struct ntfs_volume, precalc_work);
	s64 nr_free;

	nr_free = get_nr_free_clusters(vol);

	ntfs_debug("pre-calculate free clusters(%lld) using workqueue",
			nr_free);
}

static struct lock_class_key ntfs_mft_inval_lock_key;

/*
 * ntfs_fill_super - mount an ntfs filesystem
 * @sb: super block of the device to mount
 * @fc: filesystem context containing mount options
 *
 * ntfs_fill_super() is called by the VFS to mount the device described by @sb
 * with the mount otions in @data with the NTFS filesystem.
 *
 * If @silent is true, remain silent even if errors are detected. This is used
 * during bootup, when the kernel tries to mount the root filesystem with all
 * registered filesystems one after the other until one succeeds. This implies
 * that all filesystems except the correct one will quite correctly and
 * expectedly return an error, but nobody wants to see error messages when in
 * fact this is what is supposed to happen.
 */
static int ntfs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	char *boot;
	struct inode *tmp_ino;
	int blocksize, result;
	pgoff_t lcn_bit_pages;
	struct ntfs_volume *vol = NTFS_SB(sb);
	int silent = fc->sb_flags & SB_SILENT;

	vol->sb = sb;

	/*
	 * We do a pretty difficult piece of bootstrap by reading the
	 * MFT (and other metadata) from disk into memory. We'll only
	 * release this metadata during umount, so the locking patterns
	 * observed during bootstrap do not count. So turn off the
	 * observation of locking patterns (strictly for this context
	 * only) while mounting NTFS. [The validator is still active
	 * otherwise, even for this context: it will for example record
	 * lock class registrations.]
	 */
	lockdep_off();
	ntfs_debug("Entering.");

	if (vol->nls_map && !strcmp(vol->nls_map->charset, "utf8"))
		vol->nls_utf8 = true;
	if (NVolDisableSparse(vol))
		vol->preallocated_size = 0;

	if (NVolDiscard(vol) && !bdev_max_discard_sectors(sb->s_bdev)) {
		ntfs_warning(
			sb,
			"Discard requested but device does not support discard.  Discard disabled.");
		NVolClearDiscard(vol);
	}

	/* We support sector sizes up to the PAGE_SIZE. */
	if (bdev_logical_block_size(sb->s_bdev) > PAGE_SIZE) {
		if (!silent)
			ntfs_error(sb,
				"Device has unsupported sector size (%i).  The maximum supported sector size on this architecture is %lu bytes.",
				bdev_logical_block_size(sb->s_bdev),
				PAGE_SIZE);
		goto err_out_now;
	}

	/*
	 * Setup the device access block size to NTFS_BLOCK_SIZE or the hard
	 * sector size, whichever is bigger.
	 */
	blocksize = sb_min_blocksize(sb, NTFS_BLOCK_SIZE);
	if (blocksize < NTFS_BLOCK_SIZE) {
		if (!silent)
			ntfs_error(sb, "Unable to set device block size.");
		goto err_out_now;
	}

	ntfs_debug("Set device block size to %i bytes (block size bits %i).",
			blocksize, sb->s_blocksize_bits);
	/* Determine the size of the device in units of block_size bytes. */
	if (!bdev_nr_bytes(sb->s_bdev)) {
		if (!silent)
			ntfs_error(sb, "Unable to determine device size.");
		goto err_out_now;
	}
	vol->nr_blocks = bdev_nr_bytes(sb->s_bdev) >>
			sb->s_blocksize_bits;
	/* Read the boot sector and return unlocked buffer head to it. */
	boot = read_ntfs_boot_sector(sb, silent);
	if (!boot) {
		if (!silent)
			ntfs_error(sb, "Not an NTFS volume.");
		goto err_out_now;
	}
	/*
	 * Extract the data from the boot sector and setup the ntfs volume
	 * using it.
	 */
	result = parse_ntfs_boot_sector(vol, (struct ntfs_boot_sector *)boot);
	kfree(boot);
	if (!result) {
		if (!silent)
			ntfs_error(sb, "Unsupported NTFS filesystem.");
		goto err_out_now;
	}

	if (vol->sector_size > blocksize) {
		blocksize = sb_set_blocksize(sb, vol->sector_size);
		if (blocksize != vol->sector_size) {
			if (!silent)
				ntfs_error(sb,
					   "Unable to set device block size to sector size (%i).",
					   vol->sector_size);
			goto err_out_now;
		}
		vol->nr_blocks = bdev_nr_bytes(sb->s_bdev) >>
				sb->s_blocksize_bits;
		ntfs_debug("Changed device block size to %i bytes (block size bits %i) to match volume sector size.",
				blocksize, sb->s_blocksize_bits);
	}
	/* Initialize the cluster and mft allocators. */
	ntfs_setup_allocators(vol);
	/* Setup remaining fields in the super block. */
	sb->s_magic = NTFS_SB_MAGIC;
	/*
	 * Ntfs allows 63 bits for the file size, i.e. correct would be:
	 *	sb->s_maxbytes = ~0ULL >> 1;
	 * But the kernel uses a long as the page cache page index which on
	 * 32-bit architectures is only 32-bits. MAX_LFS_FILESIZE is kernel
	 * defined to the maximum the page cache page index can cope with
	 * without overflowing the index or to 2^63 - 1, whichever is smaller.
	 */
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	/* Ntfs measures time in 100ns intervals. */
	sb->s_time_gran = 100;

	sb->s_xattr = ntfs_xattr_handlers;
	/*
	 * Now load the metadata required for the page cache and our address
	 * space operations to function. We do this by setting up a specialised
	 * read_inode method and then just calling the normal iget() to obtain
	 * the inode for $MFT which is sufficient to allow our normal inode
	 * operations and associated address space operations to function.
	 */
	sb->s_op = &ntfs_sops;
	tmp_ino = new_inode(sb);
	if (!tmp_ino) {
		if (!silent)
			ntfs_error(sb, "Failed to load essential metadata.");
		goto err_out_now;
	}

	tmp_ino->i_ino = FILE_MFT;
	insert_inode_hash(tmp_ino);
	if (ntfs_read_inode_mount(tmp_ino) < 0) {
		if (!silent)
			ntfs_error(sb, "Failed to load essential metadata.");
		goto iput_tmp_ino_err_out_now;
	}
	lockdep_set_class(&tmp_ino->i_mapping->invalidate_lock,
			  &ntfs_mft_inval_lock_key);

	mutex_lock(&ntfs_lock);

	/*
	 * Generate the global default upcase table if necessary.  Also
	 * temporarily increment the number of upcase users to avoid race
	 * conditions with concurrent (u)mounts.
	 */
	if (!default_upcase)
		default_upcase = generate_default_upcase();
	ntfs_nr_upcase_users++;
	mutex_unlock(&ntfs_lock);

	lcn_bit_pages = (((vol->nr_clusters + 7) >> 3) + PAGE_SIZE - 1) >> PAGE_SHIFT;
	vol->lcn_empty_bits_per_page = kvmalloc_array(lcn_bit_pages, sizeof(unsigned int),
						      GFP_KERNEL);
	if (!vol->lcn_empty_bits_per_page) {
		ntfs_error(sb,
			   "Unable to allocate pages for storing LCN empty bit counts\n");
		goto unl_upcase_iput_tmp_ino_err_out_now;
	}

	/*
	 * From now on, ignore @silent parameter. If we fail below this line,
	 * it will be due to a corrupt fs or a system error, so we report it.
	 */
	/*
	 * Open the system files with normal access functions and complete
	 * setting up the ntfs super block.
	 */
	if (!load_system_files(vol)) {
		ntfs_error(sb, "Failed to load system files.");
		goto unl_upcase_iput_tmp_ino_err_out_now;
	}

	/* We grab a reference, simulating an ntfs_iget(). */
	ihold(vol->root_ino);
	sb->s_root = d_make_root(vol->root_ino);
	if (sb->s_root) {
		s64 nr_records;

		ntfs_debug("Exiting, status successful.");

		/* Release the default upcase if it has no users. */
		mutex_lock(&ntfs_lock);
		if (!--ntfs_nr_upcase_users && default_upcase) {
			kvfree(default_upcase);
			default_upcase = NULL;
		}
		mutex_unlock(&ntfs_lock);
		sb->s_export_op = &ntfs_export_ops;
		lockdep_on();

		nr_records = __get_nr_free_mft_records(vol,
				i_size_read(vol->mft_ino) >> vol->mft_record_size_bits,
				((((NTFS_I(vol->mft_ino)->initialized_size >>
				    vol->mft_record_size_bits) +
				   7) >> 3) + PAGE_SIZE - 1) >> PAGE_SHIFT);
		ntfs_debug("Free mft records(%lld)", nr_records);

		init_waitqueue_head(&vol->free_waitq);
		INIT_WORK(&vol->precalc_work, precalc_free_clusters);
		queue_work(ntfs_wq, &vol->precalc_work);
		return 0;
	}
	ntfs_error(sb, "Failed to allocate root directory.");
	/* Clean up after the successful load_system_files() call from above. */
	iput(vol->vol_ino);
	vol->vol_ino = NULL;
	/* NTFS 3.0+ specific clean up. */
	if (vol->major_ver >= 3) {
		if (vol->quota_q_ino) {
			iput(vol->quota_q_ino);
			vol->quota_q_ino = NULL;
		}
		if (vol->quota_ino) {
			iput(vol->quota_ino);
			vol->quota_ino = NULL;
		}
		if (vol->extend_ino) {
			iput(vol->extend_ino);
			vol->extend_ino = NULL;
		}
		if (vol->secure_ino) {
			iput(vol->secure_ino);
			vol->secure_ino = NULL;
		}
	}
	iput(vol->root_ino);
	vol->root_ino = NULL;
	iput(vol->lcnbmp_ino);
	vol->lcnbmp_ino = NULL;
	iput(vol->mftbmp_ino);
	vol->mftbmp_ino = NULL;
	if (vol->logfile_ino) {
		iput(vol->logfile_ino);
		vol->logfile_ino = NULL;
	}
	if (vol->mftmirr_ino) {
		iput(vol->mftmirr_ino);
		vol->mftmirr_ino = NULL;
	}
	/* Throw away the table of attribute definitions. */
	vol->attrdef_size = 0;
	if (vol->attrdef) {
		kvfree(vol->attrdef);
		vol->attrdef = NULL;
	}
	vol->upcase_len = 0;
	mutex_lock(&ntfs_lock);
	if (vol->upcase == default_upcase) {
		ntfs_nr_upcase_users--;
		vol->upcase = NULL;
	}
	mutex_unlock(&ntfs_lock);
	if (vol->upcase) {
		kvfree(vol->upcase);
		vol->upcase = NULL;
	}
	if (vol->nls_map) {
		unload_nls(vol->nls_map);
		vol->nls_map = NULL;
	}
	/* Error exit code path. */
unl_upcase_iput_tmp_ino_err_out_now:
	if (vol->lcn_empty_bits_per_page)
		kvfree(vol->lcn_empty_bits_per_page);
	/*
	 * Decrease the number of upcase users and destroy the global default
	 * upcase table if necessary.
	 */
	mutex_lock(&ntfs_lock);
	if (!--ntfs_nr_upcase_users && default_upcase) {
		kvfree(default_upcase);
		default_upcase = NULL;
	}

	mutex_unlock(&ntfs_lock);
iput_tmp_ino_err_out_now:
	iput(tmp_ino);
	if (vol->mft_ino && vol->mft_ino != tmp_ino)
		iput(vol->mft_ino);
	vol->mft_ino = NULL;
	/* Errors at this stage are irrelevant. */
err_out_now:
	sb->s_fs_info = NULL;
	kfree(vol);
	ntfs_debug("Failed, returning -EINVAL.");
	lockdep_on();
	return -EINVAL;
}

/*
 * This is a slab cache to optimize allocations and deallocations of Unicode
 * strings of the maximum length allowed by NTFS, which is NTFS_MAX_NAME_LEN
 * (255) Unicode characters + a terminating NULL Unicode character.
 */
struct kmem_cache *ntfs_name_cache;

/* Slab caches for efficient allocation/deallocation of inodes. */
struct kmem_cache *ntfs_inode_cache;
struct kmem_cache *ntfs_big_inode_cache;

/* Init once constructor for the inode slab cache. */
static void ntfs_big_inode_init_once(void *foo)
{
	struct ntfs_inode *ni = foo;

	inode_init_once(VFS_I(ni));
}

/*
 * Slab caches to optimize allocations and deallocations of attribute search
 * contexts and index contexts, respectively.
 */
struct kmem_cache *ntfs_attr_ctx_cache;
struct kmem_cache *ntfs_index_ctx_cache;

/* Driver wide mutex. */
DEFINE_MUTEX(ntfs_lock);

static int ntfs_get_tree(struct fs_context *fc)
{
	return get_tree_bdev(fc, ntfs_fill_super);
}

static void ntfs_free_fs_context(struct fs_context *fc)
{
	struct ntfs_volume *vol = fc->s_fs_info;

	if (vol)
		ntfs_volume_free(vol);
}

static const struct fs_context_operations ntfs_context_ops = {
	.parse_param	= ntfs_parse_param,
	.get_tree	= ntfs_get_tree,
	.free		= ntfs_free_fs_context,
	.reconfigure	= ntfs_reconfigure,
};

static int ntfs_init_fs_context(struct fs_context *fc)
{
	struct ntfs_volume *vol;

	/* Allocate a new struct ntfs_volume and place it in sb->s_fs_info. */
	vol = kmalloc(sizeof(struct ntfs_volume), GFP_NOFS);
	if (!vol)
		return -ENOMEM;

	/* Initialize struct ntfs_volume structure. */
	*vol = (struct ntfs_volume) {
		.uid = INVALID_UID,
		.gid = INVALID_GID,
		.fmask = 0,
		.dmask = 0,
		.mft_zone_multiplier = 1,
		.on_errors = ON_ERRORS_CONTINUE,
		.nls_map = load_nls_default(),
		.preallocated_size = NTFS_DEF_PREALLOC_SIZE,
	};

	NVolSetShowHiddenFiles(vol);
	NVolSetCaseSensitive(vol);
	init_rwsem(&vol->mftbmp_lock);
	init_rwsem(&vol->lcnbmp_lock);

	fc->s_fs_info = vol;
	fc->ops = &ntfs_context_ops;
	return 0;
}

static struct file_system_type ntfs_fs_type = {
	.owner                  = THIS_MODULE,
	.name                   = "ntfs",
	.init_fs_context        = ntfs_init_fs_context,
	.parameters             = ntfs_parameters,
	.kill_sb                = kill_block_super,
	.fs_flags               = FS_REQUIRES_DEV | FS_ALLOW_IDMAP,
};
MODULE_ALIAS_FS("ntfs");

static int ntfs_workqueue_init(void)
{
	ntfs_wq = alloc_workqueue("ntfs-bg-io", 0, 0);
	if (!ntfs_wq)
		return -ENOMEM;
	return 0;
}

static void ntfs_workqueue_destroy(void)
{
	destroy_workqueue(ntfs_wq);
	ntfs_wq = NULL;
}

/* Stable names for the slab caches. */
static const char ntfs_index_ctx_cache_name[] = "ntfs_index_ctx_cache";
static const char ntfs_attr_ctx_cache_name[] = "ntfs_attr_ctx_cache";
static const char ntfs_name_cache_name[] = "ntfs_name_cache";
static const char ntfs_inode_cache_name[] = "ntfs_inode_cache";
static const char ntfs_big_inode_cache_name[] = "ntfs_big_inode_cache";

static int __init init_ntfs_fs(void)
{
	int err = 0;

	err = ntfs_workqueue_init();
	if (err) {
		pr_crit("Failed to register workqueue!\n");
		return err;
	}

	ntfs_index_ctx_cache = kmem_cache_create(ntfs_index_ctx_cache_name,
			sizeof(struct ntfs_index_context), 0 /* offset */,
			SLAB_HWCACHE_ALIGN, NULL /* ctor */);
	if (!ntfs_index_ctx_cache) {
		pr_crit("Failed to create %s!\n", ntfs_index_ctx_cache_name);
		goto ictx_err_out;
	}
	ntfs_attr_ctx_cache = kmem_cache_create(ntfs_attr_ctx_cache_name,
			sizeof(struct ntfs_attr_search_ctx), 0 /* offset */,
			SLAB_HWCACHE_ALIGN, NULL /* ctor */);
	if (!ntfs_attr_ctx_cache) {
		pr_crit("NTFS: Failed to create %s!\n",
			ntfs_attr_ctx_cache_name);
		goto actx_err_out;
	}

	ntfs_name_cache = kmem_cache_create(ntfs_name_cache_name,
			(NTFS_MAX_NAME_LEN+2) * sizeof(__le16), 0,
			SLAB_HWCACHE_ALIGN, NULL);
	if (!ntfs_name_cache) {
		pr_crit("Failed to create %s!\n", ntfs_name_cache_name);
		goto name_err_out;
	}

	ntfs_inode_cache = kmem_cache_create(ntfs_inode_cache_name,
			sizeof(struct ntfs_inode), 0, SLAB_RECLAIM_ACCOUNT, NULL);
	if (!ntfs_inode_cache) {
		pr_crit("Failed to create %s!\n", ntfs_inode_cache_name);
		goto inode_err_out;
	}

	ntfs_big_inode_cache = kmem_cache_create(ntfs_big_inode_cache_name,
			sizeof(struct big_ntfs_inode), 0, SLAB_HWCACHE_ALIGN |
			SLAB_RECLAIM_ACCOUNT | SLAB_ACCOUNT,
			ntfs_big_inode_init_once);
	if (!ntfs_big_inode_cache) {
		pr_crit("Failed to create %s!\n", ntfs_big_inode_cache_name);
		goto big_inode_err_out;
	}

	/* Register the ntfs sysctls. */
	err = ntfs_sysctl(1);
	if (err) {
		pr_crit("Failed to register NTFS sysctls!\n");
		goto sysctl_err_out;
	}

	err = register_filesystem(&ntfs_fs_type);
	if (!err) {
		ntfs_debug("NTFS driver registered successfully.");
		return 0; /* Success! */
	}
	pr_crit("Failed to register NTFS filesystem driver!\n");

	/* Unregister the ntfs sysctls. */
	ntfs_sysctl(0);
sysctl_err_out:
	kmem_cache_destroy(ntfs_big_inode_cache);
big_inode_err_out:
	kmem_cache_destroy(ntfs_inode_cache);
inode_err_out:
	kmem_cache_destroy(ntfs_name_cache);
name_err_out:
	kmem_cache_destroy(ntfs_attr_ctx_cache);
actx_err_out:
	kmem_cache_destroy(ntfs_index_ctx_cache);
ictx_err_out:
	if (!err) {
		pr_crit("Aborting NTFS filesystem driver registration...\n");
		err = -ENOMEM;
	}
	return err;
}

static void __exit exit_ntfs_fs(void)
{
	ntfs_debug("Unregistering NTFS driver.");

	unregister_filesystem(&ntfs_fs_type);

	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(ntfs_big_inode_cache);
	kmem_cache_destroy(ntfs_inode_cache);
	kmem_cache_destroy(ntfs_name_cache);
	kmem_cache_destroy(ntfs_attr_ctx_cache);
	kmem_cache_destroy(ntfs_index_ctx_cache);
	ntfs_workqueue_destroy();
	/* Unregister the ntfs sysctls. */
	ntfs_sysctl(0);
}

module_init(init_ntfs_fs);
module_exit(exit_ntfs_fs);

MODULE_AUTHOR("Anton Altaparmakov <anton@tuxera.com>"); /* Original read-only NTFS driver */
MODULE_AUTHOR("Namjae Jeon <linkinjeon@kernel.org>"); /* Add write, iomap and various features */
MODULE_DESCRIPTION("NTFS read-write filesystem driver");
MODULE_LICENSE("GPL");
#ifdef DEBUG
module_param(debug_msgs, uint, 0);
MODULE_PARM_DESC(debug_msgs, "Enable debug messages.");
#endif
