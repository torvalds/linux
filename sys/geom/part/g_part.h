/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006-2008 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _GEOM_PART_H_
#define	_GEOM_PART_H_

#define	G_PART_TRACE(args)	g_trace args

#define G_PART_PROBE_PRI_LOW	-10
#define	G_PART_PROBE_PRI_NORM	-5
#define	G_PART_PROBE_PRI_HIGH	0

enum g_part_alias {
	G_PART_ALIAS_APPLE_APFS,	/* An Apple APFS partition. */
	G_PART_ALIAS_APPLE_BOOT,	/* An Apple boot partition entry. */
	G_PART_ALIAS_APPLE_CORE_STORAGE,/* An Apple Core Storage partition. */
	G_PART_ALIAS_APPLE_HFS,		/* An HFS+ file system entry. */
	G_PART_ALIAS_APPLE_LABEL,	/* An Apple label partition entry. */
	G_PART_ALIAS_APPLE_RAID,	/* An Apple RAID partition entry. */
	G_PART_ALIAS_APPLE_RAID_OFFLINE,/* An Apple RAID (offline) part entry.*/
	G_PART_ALIAS_APPLE_TV_RECOVERY,	/* An Apple TV recovery part entry. */
	G_PART_ALIAS_APPLE_UFS,		/* An Apple UFS partition entry. */
	G_PART_ALIAS_BIOS_BOOT,		/* A GRUB 2 boot partition entry. */
	G_PART_ALIAS_CHROMEOS_FIRMWARE,	/* A ChromeOS firmware part. entry. */
	G_PART_ALIAS_CHROMEOS_KERNEL,	/* A ChromeOS Kernel part. entry. */
	G_PART_ALIAS_CHROMEOS_RESERVED,	/* ChromeOS. Reserved for future use. */
	G_PART_ALIAS_CHROMEOS_ROOT,	/* A ChromeOS root part. entry. */
	G_PART_ALIAS_DFBSD,		/* A DfBSD label32 partition entry */
	G_PART_ALIAS_DFBSD64,		/* A DfBSD label64 partition entry */
	G_PART_ALIAS_DFBSD_CCD,		/* A DfBSD CCD partition entry */
	G_PART_ALIAS_DFBSD_HAMMER,	/* A DfBSD HAMMER FS partition entry */
	G_PART_ALIAS_DFBSD_HAMMER2,	/* A DfBSD HAMMER2 FS partition entry */
	G_PART_ALIAS_DFBSD_LEGACY,	/* A DfBSD legacy partition entry */
	G_PART_ALIAS_DFBSD_SWAP,	/* A DfBSD swap partition entry */
	G_PART_ALIAS_DFBSD_UFS,		/* A DfBSD UFS partition entry */
	G_PART_ALIAS_DFBSD_VINUM,	/* A DfBSD Vinum partition entry */
	G_PART_ALIAS_EBR,		/* A EBR partition entry. */
	G_PART_ALIAS_EFI,		/* A EFI system partition entry. */
	G_PART_ALIAS_FREEBSD,		/* A BSD labeled partition entry. */
	G_PART_ALIAS_FREEBSD_BOOT,	/* A FreeBSD boot partition entry. */
	G_PART_ALIAS_FREEBSD_NANDFS,	/* A FreeBSD nandfs partition entry. */
	G_PART_ALIAS_FREEBSD_SWAP,	/* A swap partition entry. */
	G_PART_ALIAS_FREEBSD_UFS,	/* A UFS/UFS2 file system entry. */
	G_PART_ALIAS_FREEBSD_VINUM,	/* A Vinum partition entry. */
	G_PART_ALIAS_FREEBSD_ZFS,	/* A ZFS file system entry. */
	G_PART_ALIAS_LINUX_DATA,	/* A Linux data partition entry. */
	G_PART_ALIAS_LINUX_LVM,		/* A Linux LVM partition entry. */
	G_PART_ALIAS_LINUX_RAID,	/* A Linux RAID partition entry. */
	G_PART_ALIAS_LINUX_SWAP,	/* A Linux swap partition entry. */
	G_PART_ALIAS_MBR,		/* A MBR (extended) partition entry. */
	G_PART_ALIAS_MS_BASIC_DATA,	/* A Microsoft Data part. entry. */
	G_PART_ALIAS_MS_FAT16,		/* A Microsoft FAT16 partition entry. */
	G_PART_ALIAS_MS_FAT32,		/* A Microsoft FAT32 partition entry. */
	G_PART_ALIAS_MS_FAT32LBA,	/* A Microsoft FAT32 LBA partition entry */
	G_PART_ALIAS_MS_LDM_DATA,	/* A Microsoft LDM Data part. entry. */
	G_PART_ALIAS_MS_LDM_METADATA,	/* A Microsoft LDM Metadata entry. */
	G_PART_ALIAS_MS_NTFS,		/* A Microsoft NTFS partition entry */
	G_PART_ALIAS_MS_RECOVERY,	/* A Microsoft recovery part. entry. */
	G_PART_ALIAS_MS_RESERVED,	/* A Microsoft Reserved part. entry. */
	G_PART_ALIAS_MS_SPACES,		/* A Microsoft Spaces part. entry. */
	G_PART_ALIAS_NETBSD_CCD,	/* A NetBSD CCD partition entry. */
	G_PART_ALIAS_NETBSD_CGD,	/* A NetBSD CGD partition entry. */
	G_PART_ALIAS_NETBSD_FFS,	/* A NetBSD FFS partition entry. */
	G_PART_ALIAS_NETBSD_LFS,	/* A NetBSD LFS partition entry. */
	G_PART_ALIAS_NETBSD_RAID,	/* A NetBSD RAID partition entry. */
	G_PART_ALIAS_NETBSD_SWAP,	/* A NetBSD swap partition entry. */
	G_PART_ALIAS_OPENBSD_DATA,	/* An OpenBSD data partition entry. */
	G_PART_ALIAS_PREP_BOOT,		/* A PREP/CHRP boot partition entry. */
	G_PART_ALIAS_VMFS,		/* A VMware VMFS partition entry */
	G_PART_ALIAS_VMKDIAG,		/* A VMware vmkDiagnostic partition entry */
	G_PART_ALIAS_VMRESERVED,	/* A VMware reserved partition entry */
	G_PART_ALIAS_VMVSANHDR,		/* A VMware vSAN header partition entry */
	/* Keep the following last */
	G_PART_ALIAS_COUNT
};

const char *g_part_alias_name(enum g_part_alias);

/* G_PART scheme (KOBJ class). */
struct g_part_scheme {
	KOBJ_CLASS_FIELDS;
	size_t		gps_entrysz;
	int		gps_minent;
	int		gps_maxent;
	int		gps_bootcodesz;
	TAILQ_ENTRY(g_part_scheme) scheme_list;
};

struct g_part_entry {
	LIST_ENTRY(g_part_entry) gpe_entry;
	struct g_provider *gpe_pp;	/* Corresponding provider. */
	off_t		gpe_offset;	/* Byte offset. */
	quad_t		gpe_start;	/* First LBA of partition. */
	quad_t		gpe_end;	/* Last LBA of partition. */
	int		gpe_index;
	int		gpe_created:1;	/* Entry is newly created. */
	int		gpe_deleted:1;	/* Entry has been deleted. */
	int		gpe_modified:1;	/* Entry has been modified. */
	int		gpe_internal:1;	/* Entry is not a used entry. */
};

/* G_PART table (KOBJ instance). */
struct g_part_table {
	KOBJ_FIELDS;
	struct g_part_scheme *gpt_scheme;
	struct g_geom	*gpt_gp;
	LIST_HEAD(, g_part_entry) gpt_entry;
	quad_t		gpt_first;	/* First allocatable LBA */
	quad_t		gpt_last;	/* Last allocatable LBA */
	int		gpt_entries;
	/*
	 * gpt_smhead and gpt_smtail are bitmaps representing the first
	 * 32 sectors on the disk (gpt_smhead) and the last 32 sectors
	 * on the disk (gpt_smtail). These maps are used by the commit
	 * verb to clear sectors previously used by a scheme after the
	 * partitioning scheme has been destroyed.
	 */
	uint32_t	gpt_smhead;
	uint32_t	gpt_smtail;
	/*
	 * gpt_sectors and gpt_heads are the fixed or synchesized number
	 * of sectors per track and heads (resp) that make up a disks
	 * geometry. This is to support partitioning schemes as well as
	 * file systems that work on a geometry. The MBR scheme and the
	 * MS-DOS (FAT) file system come to mind.
	 * We keep track of whether the geometry is fixed or synchesized
	 * so that a partitioning scheme can correct the synthesized
	 * geometry, based on the on-disk metadata.
	 */
	uint32_t	gpt_sectors;
	uint32_t	gpt_heads;

	int		gpt_depth;	/* Sub-partitioning level. */
	int		gpt_isleaf:1;	/* Cannot be sub-partitioned. */
	int		gpt_created:1;	/* Newly created. */
	int		gpt_modified:1;	/* Table changes have been made. */
	int		gpt_opened:1;	/* Permissions obtained. */
	int		gpt_fixgeom:1;	/* Geometry is fixed. */
	int		gpt_corrupt:1;	/* Table is corrupt. */
};

struct g_part_entry *g_part_new_entry(struct g_part_table *, int, quad_t,
    quad_t);

enum g_part_ctl {
	G_PART_CTL_NONE,
	G_PART_CTL_ADD,
	G_PART_CTL_BOOTCODE,
	G_PART_CTL_COMMIT,
	G_PART_CTL_CREATE,
	G_PART_CTL_DELETE,
	G_PART_CTL_DESTROY,
	G_PART_CTL_MODIFY,
	G_PART_CTL_MOVE,
	G_PART_CTL_RECOVER,
	G_PART_CTL_RESIZE,
	G_PART_CTL_SET,
	G_PART_CTL_UNDO,
	G_PART_CTL_UNSET
};

/* G_PART ctlreq parameters. */
#define	G_PART_PARM_ENTRIES	0x0001
#define	G_PART_PARM_FLAGS	0x0002
#define	G_PART_PARM_GEOM	0x0004
#define	G_PART_PARM_INDEX	0x0008
#define	G_PART_PARM_LABEL	0x0010
#define	G_PART_PARM_OUTPUT	0x0020
#define	G_PART_PARM_PROVIDER	0x0040
#define	G_PART_PARM_SCHEME	0x0080
#define	G_PART_PARM_SIZE	0x0100
#define	G_PART_PARM_START	0x0200
#define	G_PART_PARM_TYPE	0x0400
#define	G_PART_PARM_VERSION	0x0800
#define	G_PART_PARM_BOOTCODE	0x1000
#define	G_PART_PARM_ATTRIB	0x2000
#define	G_PART_PARM_FORCE	0x4000
#define G_PART_PARM_SKIP_DSN	0x8000

struct g_part_parms {
	unsigned int	gpp_parms;
	unsigned int	gpp_entries;
	const char	*gpp_flags;
	struct g_geom	*gpp_geom;
	unsigned int	gpp_index;
	const char	*gpp_label;
	struct g_provider *gpp_provider;
	struct g_part_scheme *gpp_scheme;
	quad_t		gpp_size;
	quad_t		gpp_start;
	const char	*gpp_type;
	unsigned int	gpp_version;
	const void	*gpp_codeptr;
	unsigned int	gpp_codesize;
	const char	*gpp_attrib;
	unsigned int	gpp_force;
	unsigned int	gpp_skip_dsn;
};

void g_part_geometry_heads(off_t, u_int, off_t *, u_int *);

int g_part_modevent(module_t, int, struct g_part_scheme *);

#define	G_PART_SCHEME_DECLARE(name)				\
    static int name##_modevent(module_t mod, int tp, void *d)	\
    {								\
	return (g_part_modevent(mod, tp, d));			\
    }								\
    static moduledata_t name##_mod = {				\
	#name,							\
	name##_modevent,					\
	&name##_scheme						\
    };								\
    DECLARE_MODULE(name, name##_mod, SI_SUB_DRIVERS, SI_ORDER_ANY); \
    MODULE_DEPEND(name, g_part, 0, 0, 0)

#endif /* !_GEOM_PART_H_ */
