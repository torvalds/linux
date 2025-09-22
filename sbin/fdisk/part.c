/*	$OpenBSD: part.c,v 1.172 2025/06/22 12:23:08 krw Exp $	*/

/*
 * Copyright (c) 1997 Tobias Weingartner
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/disklabel.h>

#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uuid.h>

#include "part.h"
#include "disk.h"
#include "misc.h"
#include "gpt.h"

struct mbr_type {
	int	 mt_type;
	char	*mt_desc;
};

/*
* MBR type sources:
*	OpenBSD Historical usage
* 	https://en.wikipedia.org/wiki/Partition_type#List_of_partition_IDs
*	https://www.win.tue.nl/~aeb/partitions/partition_types-1.html
*/
const struct mbr_type		mbr_types[] = {
	{ 0x00, NULL	},   /* unused */
	{ 0x01, NULL	},   /* Primary DOS with 12 bit FAT */
	{ 0x02, NULL	},   /* XENIX / filesystem */
	{ 0x03, NULL	},   /* XENIX /usr filesystem */
	{ 0x04, NULL	},   /* Primary DOS with 16 bit FAT */
	{ 0x05, NULL	},   /* Extended DOS */
	{ 0x06, NULL	},   /* Primary 'big' DOS (> 32MB) */
	{ 0x07, NULL	},   /* NTFS */
	{ 0x08, NULL	},   /* AIX filesystem */
	{ 0x09, NULL	},   /* AIX boot partition or Coherent */
	{ 0x0A, NULL	},   /* OS/2 Boot Manager or OPUS */
	{ 0x0B, NULL	},   /* Primary Win95 w/ 32-bit FAT */
	{ 0x0C, NULL	},   /* Primary Win95 w/ 32-bit FAT LBA-mapped */
	{ 0x0E, NULL	},   /* Primary DOS w/ 16-bit FAT, CHS-mapped */
	{ 0x0F, NULL	},   /* Extended DOS LBA-mapped */
	{ 0x10, NULL	},   /* OPUS */
	{ 0x11, NULL	},   /* OS/2 BM: hidden DOS 12-bit FAT */
	{ 0x12, NULL	},   /* Compaq Diagnostics */
	{ 0x14, NULL	},   /* OS/2 BM: hidden DOS 16-bit FAT <32M or Novell DOS 7.0 bug */
	{ 0x16, NULL	},   /* OS/2 BM: hidden DOS 16-bit FAT >=32M */
	{ 0x17, NULL	},   /* OS/2 BM: hidden IFS */
	{ 0x18, NULL	},   /* AST Windows swapfile */
	{ 0x19, NULL	},   /* Willowtech Photon coS */
	{ 0x1C, NULL	},   /* IBM ThinkPad recovery partition */
	{ 0x20, NULL	},   /* Willowsoft OFS1 */
	{ 0x24, NULL	},   /* NEC DOS */
	{ 0x27, NULL	},   /* Windows hidden Recovery Partition */
	{ 0x38, NULL	},   /* Theos */
	{ 0x39, NULL	},   /* Plan 9 */
	{ 0x40, NULL	},   /* VENIX 286 or LynxOS */
	{ 0x41, NULL 	},   /* Linux/MINIX (sharing disk with DRDOS) or Personal RISC boot */
	{ 0x42, NULL 	},   /* SFS or Linux swap (sharing disk with DRDOS) */
	{ 0x43, NULL 	},   /* Linux native (sharing disk with DRDOS) */
	{ 0x4D, NULL	},   /* QNX 4.2 Primary */
	{ 0x4E, NULL	},   /* QNX 4.2 Secondary */
	{ 0x4F, NULL	},   /* QNX 4.2 Tertiary */
	{ 0x50, NULL	},   /* DM (disk manager) */
	{ 0x51, NULL	},   /* DM6 Aux1 (or Novell) */
	{ 0x52, NULL	},   /* CP/M or Microport SysV/AT */
	{ 0x53, NULL	},   /* DM6 Aux3 */
	{ 0x54, NULL	},   /* Ontrack */
	{ 0x55, NULL	},   /* EZ-Drive (disk manager) */
	{ 0x56, NULL	},   /* Golden Bow (disk manager) */
	{ 0x5C, NULL	},   /* Priam Edisk (disk manager) */
	{ 0x61, NULL	},   /* SpeedStor */
	{ 0x63, NULL	},   /* ISC, System V/386, GNU HURD or Mach */
	{ 0x64, NULL	},   /* Novell NetWare 2.xx */
	{ 0x65, NULL	},   /* Novell NetWare 3.xx */
	{ 0x66, NULL	},   /* Novell 386 NetWare */
	{ 0x67, NULL	},   /* Novell */
	{ 0x68, NULL	},   /* Novell */
	{ 0x69, NULL	},   /* Novell */
	{ 0x70, NULL	},   /* DiskSecure Multi-Boot */
	{ 0x75, NULL	},   /* PCIX */
	{ 0x80, NULL	},   /* Minix 1.1 ... 1.4a */
	{ 0x81, NULL	},   /* Minix 1.4b ... 1.5.10 */
	{ 0x82, NULL	},   /* Linux swap */
	{ 0x83, NULL	},   /* Linux filesystem */
	{ 0x84, NULL	},   /* OS/2 hidden C: drive */
	{ 0x85, NULL	},   /* Linux extended */
	{ 0x86, NULL	},   /* NT FAT volume set */
	{ 0x87, NULL	},   /* NTFS volume set or HPFS mirrored */
	{ 0x8E, NULL	},   /* Linux LVM */
	{ 0x93, NULL	},   /* Amoeba filesystem */
	{ 0x94, NULL	},   /* Amoeba bad block table */
	{ 0x99, NULL	},   /* Mylex EISA SCSI */
	{ 0x9F, NULL	},   /* BSDI BSD/OS */
	{ 0xA0, NULL	},   /* Phoenix NoteBIOS save-to-disk */
	{ 0xA5, NULL	},   /* FreeBSD */
	{ 0xA6, NULL	},   /* OpenBSD */
	{ 0xA7, NULL	},   /* NEXTSTEP */
	{ 0xA8, NULL	},   /* MacOS X main partition */
	{ 0xA9, NULL	},   /* NetBSD */
	{ 0xAB, NULL	},   /* MacOS X boot partition */
	{ 0xAF, NULL	},   /* MacOS X HFS+ partition */
	{ 0xB7, NULL	},   /* BSDI BSD/386 filesystem */
	{ 0xB8, NULL	},   /* BSDI BSD/386 swap */
	{ 0xBF, NULL	},   /* Solaris */
	{ 0xC0, NULL	},   /* CTOS */
	{ 0xC1, NULL	},   /* DRDOS/sec (FAT-12) */
	{ 0xC4, NULL	},   /* DRDOS/sec (FAT-16, < 32M) */
	{ 0xC6, NULL	},   /* DRDOS/sec (FAT-16, >= 32M) */
	{ 0xC7, NULL	},   /* Syrinx (Cyrnix?) or HPFS disabled */
	{ 0xDB, NULL	},   /* Concurrent CPM or C.DOS or CTOS */
	{ 0xDE, NULL	},   /* Dell maintenance partition */
	{ 0xE1, NULL	},   /* DOS access or SpeedStor 12-bit FAT extended partition */
	{ 0xE3, NULL	},   /* DOS R/O or SpeedStor or Storage Dimensions */
	{ 0xE4, NULL	},   /* SpeedStor 16-bit FAT extended partition < 1024 cyl. */
	{ 0xEB, NULL	},   /* BeOS for Intel */
	{ 0xEE, NULL	},   /* EFI Protective Partition */
	{ 0xEF, NULL	},   /* EFI System Partition */
	{ 0xF1, NULL	},   /* SpeedStor or Storage Dimensions */
	{ 0xF2, NULL	},   /* DOS 3.3+ Secondary */
	{ 0xF4, NULL	},   /* SpeedStor >1024 cyl. or LANstep or IBM PS/2 IML */
	{ 0xFF, NULL	},   /* Xenix Bad Block Table */
};

struct gpt_type {
	int	 gt_attr;
#define	GTATTR_PROTECT		(1 << 0)
#define	GTATTR_PROTECT_EFISYS	(1 << 1)
	char	*gt_desc;	/* NULL == try menu name (a.k.a. mi_name) */
	char	*gt_guid;
};

/*
 * GPT GUID sources:
 *
 * UEFI: UEFI Specification 2.9, March 2021, Section 5.3.3, Table 5.7
 * Wikipedia: https://en.wikipedia.org/wiki/GUID_Partition_Table
 * NetBSD: /usr/src/sys/sys/disklabel_gpt.h
 * FreeBSD: /usr/src/sys/sys/disk/gpt.h.
 * DragonFlyBSD: /usr/src/sys/sys/disk/gpt.h.
 * Systemd:https://uapi-group.org/specifications/specs/discoverable_partitions_specification/
 *         https://www.freedesktop.org/software/systemd/man/systemd-gpt-auto-generator.html
 */

#define UNUSED_GUID		"00000000-0000-0000-0000-000000000000"
#define LEGACY_MBR_GUID		"024dee41-33e7-11d3-9d69-0008c781f39f"
#define LINUX_SWAP_GUID		"0657fd6d-a4ab-43c4-84e5-0933c84b4f4f"
#define LINUX_FILES_GUID	"0fc63daf-8483-4772-8e79-3d69d8477de4"
#define BIOS_BOOT_GUID		"21686148-6449-6e6f-744e-656564454649"
#define HIFIVE_BBL_GUID		"2e54b353-1271-4842-806f-e436d6af6985"
#define BEOS_I386_GUID		"42465331-3ba3-10f1-802a-4861696b7521"
#define MACOS_X_BOOT_GUID	"426f6f74-0000-11aa-aa11-00306543ecac"
#define MACOS_X_HFS_GUID	"48465300-0000-11aa-aa11-00306543ecac"
#define NETBSD_GUID		"49f48d5a-b10e-11dc-b99b-0019d1879648"
#define FREEBSD_GUID		"516e7cb4-6ecf-11d6-8ff8-00022d09712b"
#define APFS_RECOVERY_GUID	"52637672-7900-11aa-aa11-00306543ecac"
#define MACOS_X_GUID		"55465300-0000-11aa-aa11-00306543ecac"
#define HIFIVE_FSBL_GUID	"5b193300-fc78-40cd-8002-e86c45580b47"
#define APFS_ISC_GUID		"69646961-6700-11aa-aa11-00306543ecac"
#define SOLARIS_GUID		"6a85cf4d-1dd2-11b2-99a6-080020736631"
#define APFS_GUID		"7c3457ef-0000-11aa-aa11-00306543ecac"
#define OPENBSD_GUID		"824cc7a0-36a8-11e3-890a-952519ad3f61"
#define LINUXSWAP_DR_GUID	"af9b60a0-1431-4f62-bc68-3311714a69ad"
#define EFI_SYSTEM_PARTITION_GUID "c12a7328-f81f-11d2-ba4b-00a0c93ec93b"
#define WIN_RECOVERY_GUID	"de94bba4-06d1-4d40-a16a-bfd50179d6ac"
#define LINUX_LVM_GUID		"e6d6d379-f507-44c2-a23c-238f2a3df928"
#define MICROSOFT_BASIC_DATA_GUID "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7"
#define CHROME_KERNEL_GUID	"fe3a2a5d-4f32-41a7-b725-accc3285a309"
#if 0
#define	CEPH_MULTIPATH_BLOCK_LOG_GUID	"01b41e1b-002a-453c-9f17-88793989ff8f"
#define	MIDNIGHTBSD_UFS_GUID		"0394ef8b-237e-11e1-b4b3-e89a8f7fc3a7"
#define	FUCHSIA_L_DATA_GUID		"08185f0c-892d-428a-a789-dbeec8f55e6a"
#define	CHROMEOS_MINIOS_GUID		"09845860-705f-4bb5-b16c-8a8a099caf52"
#define	FUCHSIA_RO_FACTORY_BOOT_GUID	"10b8dbaa-d2bf-42a9-98c6-a7c5db3701e7"
#define	ANDROID_IA_BOOT2_GUID		"114eaffe-1552-4022-b26e-9b053604cf84"
#define	CEPH_DMCRYPT_LUKS_BLOCK_DB_GUID	"166418da-c469-4022-adf4-b30afd37f176"
#define	ANDROID_6_0_ARM_EXT_GUID	"193d1ea4-b3ca-11e4-b075-10604b889dcf"
#define	ANDROID_6_0_ARM_META_GUID	"19a710a2-b3ca-11e4-b026-10604b889dcf"
#define	FUCHSIA_L_MISC_GUID		"1d75395d-f2c6-476b-a8b7-45cc1c97b476"
#define	ANDROID_IA_METADATA_GUID	"20ac26be-20b7-11e3-84c5-6cfdb94711e9"
#define	FUCHSIA_L_ZIRCON_BOOTB_GUID	"23cc04df-c278-4ce7-8471-897d1a4bcdf7"
#define	ANDROID_IA_BOOT_GUID		"2568845d-2332-4675-bc39-8fa5a4748d15"
#define	FUCHSIA_L_BLOB_GUID		"2967380e-134c-4cbb-b6da-17e7ce1ca45d"
#define	LINUX_X86_64_ROOT_VERITY_GUID	"2c7357ed-ebd2-46d9-aec1-23d437ec2bf5"
#define	NETBSD_CONCATENATED_GUID	"2db519c4-b10f-11dc-b99b-0019d1879648"
#define	NETBSD_ENCRYPTED_GUID		"2db519ec-b10f-11dc-b99b-0019d1879648"
#define	CHROMEOS_FUTURE_USE_GUID	"2e0a753d-9e48-43b0-8337-b15192cb1b5e"
#define	SOFTRAID_SCRATCH_GUID		"2e313465-19b9-463f-8126-8a7993773801"
#define	CEPH_DMCRYPT_BLOCK_LOG_GUID	"306e8683-4fe2-4330-b7c0-00a917c16966"
#define	CEPH_BLOCK_DB_GUID		"30cd0809-c2b2-499c-8879-2d6b78529876"
#define	WINDOWS_IBM_GPFS_GUID		"37affc90-ef7d-4e96-91c3-2d7ae055b174"
#define	COREOS_RESIZABLE_ROOT_GUID	"3884dd41-8582-4404-b9a8-e9b84f2df50e"
#define	ANDROID_IA_SYSTEM_GUID		"38f428e6-d326-425d-9140-6e0ea133647c"
#define	LINUX_SRV_GUID			"3b8f8425-20e0-4f3b-907f-1a25a76f98e8"
#define	CHROMEOS_ROOTFS_GUID		"3cb8e202-3b7e-47dd-8a3c-7ff2a13cfcec"
#define	U_BOOT_ENVIRONMENT_GUID		"3de21764-95bd-54bd-a5c3-4abe786f38a8"
#define	CHROMEOS_HIBERNATE_GUID		"3f0f8318-f146-4e6b-8222-c28c8f02e0d5"
#define	ANDROID_IA_RECOVERY_GUID	"4177c722-9e92-4aab-8644-43502bfd5506"
#define	FUCHSIA_L_FVM_GUID		"41d0e340-57e3-954e-8c1e-17ecac44cff5"
#define	FUCHSIA_VERIFIED_BOOT_META_GUID	"421a8bfc-85d9-4d85-acda-b64eec0133e9"
#define	LINUX_IA_64_USR_GUID		"4301d2a6-4e3b-4b2a-bb94-9e0b2c4225ea"
#define	LINUX_X86_ROOT_GUID		"44479540-f297-41b2-9af7-d131d5f0458a"
#define	CEPH_MULTIPATH_JOURNAL_GUID	"45b0969e-8ae0-4982-bf9d-5a8d867af560"
#define	CEPH_DMCRYPT_LUKS_JOURNAL_GUID	"45b0969e-9b03-4f30-b4c6-35865ceff106"
#define	CEPH_DMCRYPT_JOURNAL_GUID	"45b0969e-9b03-4f30-b4c6-5ec00ceff106"
#define	CEPH_JOURNAL_GUID		"45b0969e-9b03-4f30-b4c6-b4b80ceff106"
#define	BAREBOX_BAREBOX_STATE_GUID	"4778ed65-bf42-45fa-9c5b-287a1dc4aab1"
#define	FUCHSIA_L_INSTALL_GUID		"48435546-4953-2041-494e-5354414c4c52"
#define	ANDROID_IA_BOOT3_GUID		"49a4d17f-93a3-45c1-a0de-f50b2ebe2599"
#define	NETBSD_SWAP_GUID		"49f48d32-b10e-11dc-b99b-0019d1879648"
#define	NETBSD_LFS_GUID			"49f48d82-b10e-11dc-b99b-0019d1879648"
#define	NETBSD_RAID_GUID		"49f48daa-b10e-11dc-b99b-0019d1879648"
#define	FUCHSIA_VOLUME_MANAGER_GUID	"49fd7cb8-df15-4e73-b9d9-992070127f0f"
#define	MACOS_LABEL_GUID		"4c616265-6c00-11aa-aa11-00306543ecac"
#define	LINUX_VAR_GUID			"4d21b016-b534-45c2-a9fb-5c16e091fd2d"
#define	FUCHSIA_L_SYS_CFG_GUID		"4e5e989e-4c86-11e8-a15b-480fcf35f8e6"
#define	LINUX_X86_64_ROOT_GUID		"4f68bce3-e8cd-4db1-96e7-fbcaf984b709"
#define	CEPH_MULTIPATH_OSD_GUID		"4fbd7e29-8ae0-4982-bf9d-5a8d867af560"
#define	CEPH_OSD_GUID			"4fbd7e29-9d25-41b8-afd0-062c0ceff05d"
#define	CEPH_DMCRYPT_LUKS_OSD_GUID	"4fbd7e29-9d25-41b8-afd0-35865ceff05d"
#define	CEPH_DMCRYPT_OSD_GUID		"4fbd7e29-9d25-41b8-afd0-5ec00ceff05d"
#define	FREEBSD_SWAP_GUID		"516e7cb5-6ecf-11d6-8ff8-00022d09712b"
#define	FREEBSD_UFS_GUID		"516e7cb6-6ecf-11d6-8ff8-00022d09712b"
#define	FREEBSD_VINUM_VOL_MGR_GUID	"516e7cb8-6ecf-11d6-8ff8-00022d09712b"
#define	FREEBSD_ZFS_GUID		"516e7cba-6ecf-11d6-8ff8-00022d09712b"
#define	MACOS_RAID_GUID			"52414944-0000-11aa-aa11-00306543ecac"
#define	MACOS_RAID_OFFLINE_GUID		"52414944-5f4f-11aa-aa11-00306543ecac"
#define	MACOS_TV_RECOVERY_GUID		"5265636f-7665-11aa-aa11-00306543ecac"
#define	MACOS_CORE_STORAGE_GUID		"53746f72-6167-11aa-aa11-00306543ecac"
#define	WINDOWS_STORAGE_REPLICA_GUID	"558d43c5-a1ac-43c0-aac8-d1472b2923d1"
#define	WINDOWS_LDM_METADATA_GUID	"5808c8aa-7e8f-42e0-85d2-e1e90434cfb3"
#define	FUCHSIA_L_FACTORY_CFG_GUID	"5a3a90be-4c86-11e8-a15b-480fcf35f8e6"
#define	CEPH_BLOCK_LOG_GUID		"5ce17fce-4087-4169-b7ff-056cc58473f9"
#define	COREOS_USR_GUID			"5dfbf5f4-2848-4bac-aa5e-0d9a20b745a6"
#define	FUCHSIA_L_BOOT_GUID		"5ece94fe-4c86-11e8-a15b-480fcf35f8e6"
#define	FUCHSIA_L_SYSTEM_GUID		"606b000b-b7c7-4653-a7d5-b737332c899d"
#define	LINUX_ARM32_ROOT_GUID		"69dad710-2ce4-4e3c-b16c-21a1d49abed3"
#define	FUCHSIA_L_VERIFIED_BOOTM_R_GUID	"6a2460c3-cd11-4e8b-80a8-12cce268ed0a"
#define	LINUX_IA_64_USR_VERITY_GUID	"6a491e03-3be7-4545-8e38-83320e0ea880"
#define	SOLARIS_BOOT_GUID		"6a82cb45-1dd2-11b2-99a6-080020736631"
#define	SOLARIS_SWAP_GUID		"6a87c46f-1dd2-11b2-99a6-080020736631"
#define	SOLARIS_USR_MACOS_ZFS_GUID	"6a898cc3-1dd2-11b2-99a6-080020736631"
#define	SOLARIS_BACKUP_GUID		"6a8b642b-1dd2-11b2-99a6-080020736631"
#define	SOLARIS_RESERVED5_GUID		"6a8d2ac7-1dd2-11b2-99a6-080020736631"
#define	SOLARIS_VAR_GUID		"6a8ef2e9-1dd2-11b2-99a6-080020736631"
#define	SOLARIS_HOME_GUID		"6a90ba39-1dd2-11b2-99a6-080020736631"
#define	SOLARIS_ALT_SECTOR_GUID		"6a9283a5-1dd2-11b2-99a6-080020736631"
#define	SOLARIS_RESERVED1_GUID		"6a945a3b-1dd2-11b2-99a6-080020736631"
#define	SOLARIS_RESERVED4_GUID		"6a96237f-1dd2-11b2-99a6-080020736631"
#define	SOLARIS_RESERVED2_GUID		"6a9630d1-1dd2-11b2-99a6-080020736631"
#define	SOLARIS_RESERVED3_GUID		"6a980767-1dd2-11b2-99a6-080020736631"
#define	LINUX_ARM64_USR_VERITY_GUID	"6e11a4e7-fbca-4ded-b9e9-e1a512bb664e"
#define	ATARI_TOS_BASIC_DATA_GUID	"734e5afe-f61a-11e6-bc64-92361f002671"
#define	LINUX_ARM32_ROOT_VERITY_GUID	"7386cdf2-203c-47a9-a498-f2ecce45a2d6"
#define	ONIE_BOOT_GUID			"7412f7d5-a156-4b13-81dc-867174929325"
#define	FREEBSD_NANDFS_GUID		"74ba7dd9-a689-11e1-bd04-00e081286acf"
#define	LINUX_X86_USR_GUID		"75250d76-8cc6-458e-bd66-bd47cc81a812"
#define	HP_UX_DATA_GUID			"75894c1e-3aeb-11d3-b7c1-7b03a0000000"
#define	ANDROID_IA_FASTBOOT_GUID	"767941d0-2085-11e3-ad3b-6cfdb94711e9"
#define	LINUX_USER_HOME_GUID		"773f91ef-66d4-49b5-bd83-d683bf40ad16"
#define	LINUX_X86_64_USR_VERITY_GUID	"77ff5f63-e7b6-4633-acf4-1565b864c0e6"
#define	SPDK_BLOCK_DEVICE_GUID		"7c5222bd-8f5d-4087-9c00-bf9843c7b58c"
#define	LINUX_ARM32_USR_GUID		"7d0359a3-02b3-4f0a-865c-654403e70625"
#define	LINUX_VAR_TMP_GUID		"7ec6f557-3bc5-4aca-b293-16ef5df639d1"
#define	CEPH_MULTIPATH_BLOCK_B_GUID	"7f4a666a-16f3-47a2-8445-152ef4d03f6c"
#define	LINUX_PLAIN_DMCRYPT_GUID	"7ffec5c9-2d00-49b7-8941-3ea10a5586b7"
#define	FREEBSD_BOOT_GUID		"83bd6b9d-7f41-11dc-be0b-001560b84f0f"
#define	LINUX_X86_64_USR_GUID		"8484680c-9521-48c6-9c11-b0720656f69e"
#define	MIDNIGHTBSD_DATA_GUID		"85d5e45a-237c-11e1-b4b3-e89a8f7fc3a7"
#define	MIDNIGHTBSD_SWAP_GUID		"85d5e45b-237c-11e1-b4b3-e89a8f7fc3a7"
#define	MIDNIGHTBSD_VINUM_VOL_MGR_GUID	"85d5e45c-237c-11e1-b4b3-e89a8f7fc3a7"
#define	MIDNIGHTBSD_ZFS_GUID		"85d5e45d-237c-11e1-b4b3-e89a8f7fc3a7"
#define	MIDNIGHTBSD_BOOT_GUID		"85d5e45e-237c-11e1-b4b3-e89a8f7fc3a7"
#define	CEPH_DMCRYPT_LUKS_LOG_GUID	"86a32090-3647-40b9-bbbd-38d8c573aa86"
#define	LINUX_IA_64_ROOT_VERITY_GUID	"86ed10d5-b607-45bb-8957-d350f23d0571"
#define	CEPH_DMCRYPT_CREATNG_DISK_GUID	"89c57f98-2fe5-4dc0-89c1-5ec00ceff2be"
#define	CEPH_DISK_IN_CREATION_GUID	"89c57f98-2fe5-4dc0-89c1-f3ad0ceff2be"
#define	FUCHSIA_L_TEST_GUID		"8b94d043-30be-4871-9dfa-d69556e8c1f3"
#define	VERACRYPT_ENCRYPTED_DATA_GUID	"8c8f8eff-ac95-4770-814a-21994f2dbc8f"
#define	LINUX_RESERVED_GUID		"8da63339-0007-60c0-c436-083ac8230908"
#define	LINUX_X86_USR_VERITY_GUID	"8f461b0d-14ee-4e81-9aa9-049b6fb97abd"
#define	ANDROID_IA_FACTORY_GUID		"8f68cc74-c5e5-48da-be91-a0c8c15e9c80"
#define	FUCHSIA_L_EMMC_BOOT1_GUID	"900b0fc5-90cd-4d4f-84f9-9f8ed579db88"
#define	ARCAOS_TYPE_1_GUID		"90b6ff38-b98f-4358-a21f-48f35b4a8ad3"
#define	VMWARE_RESERVED_GUID		"9198effc-31c0-11db-8f78-000c2911d1b8"
#define	LINUX_HOME_GUID			"933ac7e1-2eb4-4f13-b844-0e14e2aef915"
#define	CEPH_DMCRYPT_BLOCK_DB_GUID	"93b0052d-02d9-4d8a-a43b-33a3ee4dfbc3"
#define	LINUX_IA_64_ROOT_GUID		"993d8d3d-f80e-4225-855a-9daf8ed7ea97"
#define	FUCHSIA_ZIRCON_BOOT_ABR_GUID	"9b37fff6-2e58-466a-983a-f7926d0b04e0"
#define	VMWARE_VMKCORE_GUID		"9d275380-40ad-11db-bf97-000c2911d1b8"
#define	PPC_PREP_BOOT_GUID		"9e1a2d38-c612-4316-aa26-8b49521e5a8b"
#define	ANDROID_IA_FACTORY_ALT_GUID	"9fdaa6ef-4b3f-40d2-ba8d-bff16bfb887b"
#define	FUCHSIA_L_ZIRCON_BOOTR_GUID	"a0e5cf57-2def-46be-a80c-a2067c37cd49"
#define	FUCHSIA_L_VERIFIED_BOOTM_A_GUID	"a13b4d9a-ec5f-11e8-97d8-6c3be52705bf"
#define	LINUX_RAID_GUID			"a19d880f-05fc-4d3b-a006-743f0f84911e"
#define	FUCHSIA_L_VERIFIED_BOOTM_B_GUID	"a288abf2-ec5f-11e8-97d8-6c3be52705bf"
#define	FUCSHIA_BOOT_METADATA_GUID	"a409e16b-78aa-4acc-995c-302352621a41"
#define	ANDROID_IA_CACHE_GUID		"a893ef21-e428-470a-9e55-0668fd91a2d9"
#define	VMWARE_VMFS_FILESYSTEM_GUID	"aa31e02a-400f-11db-9590-000c2911d1b8"
#define	ANDROID_IA_OEM_GUID		"ac6d7924-eb71-4df8-b48d-e267b27148ff"
#define	LINUX_ARM64_USR_GUID		"b0e01050-ee5f-4390-949a-9101b17104e9"
#define	FUCHSIA_L_EMMC_BOOT2_GUID	"b2b2e8d1-7c10-4ebc-a2d0-4614568260ad"
#define	SOFTRAID_STATUS_GUID		"b6fa30da-92d2-4a9a-96f1-871ec6486200"
#define	LINUX_ARM64_ROOT_GUID		"b921b045-1df0-41c3-af44-4c6f280d3fae"
#define	SOFTRAID_CACHE_GUID		"bbba6df5-f46f-4a89-8f59-8765b2727503"
#define	LINUX_BOOT_GUID			"bc13c2ff-59e6-4262-a352-b275fd6f7172"
#define	ANDROID_IA_CFG_GUID		"bd59408b-4514-490d-bf12-9878d963f378"
#define	COREOS_ROOT_RAID_GUID		"be9067b9-ea49-4f15-b4f6-f36f8c9e1818"
#define	LENOVO_BOOT_GUID		"bfbfafe7-a34f-448a-9a5b-6213eb736c22"
#define	LINUX_ARM32_USR_VERITY_GUID	"c215d751-7bcd-4649-be90-6627490a4c05"
#define	ANDROID_IA_VENDOR_GUID		"c5a0aeec-13ea-11e5-a1b1-001e67ca0c3c"
#define	PLAN9_GUID			"c91818f9-8025-47af-89d2-f030d7000c2c"
#define	COREOS_OEM_GUID			"c95dc21a-df0e-4340-8d7b-26cbfa9a03e0"
#define	LINUX_LUKS_GUID			"ca7d7ccb-63ed-4c53-861c-1742536059cc"
#define	CHROMEOS_FIRMWARE_GUID		"cab6e88e-abf3-4102-a07a-d4bb9be3c1d3"
#define	CEPH_MULTIPATH_BLOCK_A_GUID	"cafecafe-8ae0-4982-bf9d-5a8d867af560"
#define	CEPH_DMCRYPT_LUKS_BLOCK_GUID	"cafecafe-9b03-4f30-b4c6-35865ceff106"
#define	CEPH_DMCRYPT_BLOCK_GUID		"cafecafe-9b03-4f30-b4c6-5ec00ceff106"
#define	CEPH_BLOCK_GUID			"cafecafe-9b03-4f30-b4c6-b4b80ceff106"
#define	QNX_POWER_SAFE_FS_GUID		"cef5a9ad-73bc-4601-89f3-cdeeeee321a1"
#define	LINUX_X86_ROOT_VERITY_GUID	"d13c5d3b-b5d1-422a-b29f-9454fdc89d76"
#define	INTEL_FAST_FLASH_GUID		"d3bfe2de-3daf-11df-ba40-e3a556d89593"
#define	ONIE_CFG_GUID			"d4e6e2cd-4469-46f3-b5cb-1bff57afc149"
#define	FUCHSIA_ENCRYPTED_SYS_GUID	"d9fd4535-106c-4cec-8d37-dfc020ca87cb"
#define	ANDROID_IA_DATA_GUID		"dc76dda9-5ac1-491c-af42-a82591580c0d"
#define	FUCHSIA_L_ZIRCON_BOOTA_GUID	"de30cc86-1f4a-4a31-93c4-66f147d33e05"
#define	LINUX_ARM64_ROOT_VERITY_GUID	"df3300ce-d69f-4c92-978c-9bfb0f38d820"
#define	HP_UX_SERVICE_GUID		"e2a1e728-32e3-11d6-a682-7b03a0000000"
#define	WINDOWS_RESERVED_GUID		"e3c9e316-0b5c-4db8-817d-f92df00215ae"
#define	WINDOWS_STORAGE_SPACES_GUID	"e75caf8f-f680-4cee-afa3-b001e56efc2d"
#define	ANDROID_IA_PERSISTENT_GUID	"ebc597d0-2053-4b15-8b64-e0aac75f4db1"
#define	CEPH_MULTIPATH_BLOCK_DB_GUID	"ec6d6385-e346-45dc-be91-da2a7c8b3261"
#define	ANDROID_IA_MISC_GUID		"ef32a33b-a409-486c-9141-9ffb711f6266"
#define	SONY_BOOT_GUID			"f4019732-066e-4e12-8273-346c5641494f"
#define	FUCHSIA_RO_FACTORY_SYS_GUID	"f95d940e-caba-4578-9b93-bb6c90f29d3e"
#define	SOFTRAID_VOLUME_GUID		"fa709c7e-65b1-4593-bfd5-e71d61de9b02"
#define	CEPH_DMCRYPT_KEYS_LOCKBOX_GUID	"fb3aabf9-d25f-47cc-bf5e-721d1816496b"
#define	FUCHSIA_BOOT_GUID		"fe8a2634-5e2e-46ba-99e3-3a192091a350"
#endif

const struct gpt_type		gpt_types[] = {
	{ GTATTR_PROTECT,
	     NULL,	/* BIOS Boot */		BIOS_BOOT_GUID },
	{ GTATTR_PROTECT,
	     NULL,	/* HiFive BBL */	HIFIVE_BBL_GUID },
	{ GTATTR_PROTECT,
	     NULL,	/* HiFive FSBL */	HIFIVE_FSBL_GUID },
	{ GTATTR_PROTECT | GTATTR_PROTECT_EFISYS,
	     NULL,	/* APFS Recovery */	APFS_RECOVERY_GUID },
	{ GTATTR_PROTECT | GTATTR_PROTECT_EFISYS,
	     NULL,	/* APFS ISC */		APFS_ISC_GUID },
	{ GTATTR_PROTECT | GTATTR_PROTECT_EFISYS,
	     NULL,	/* APFS */		APFS_GUID },

	{ 0, NULL,	/* Unused */		UNUSED_GUID },
	{ 0, NULL,	/* Legacy MBR */	LEGACY_MBR_GUID },
	{ 0, NULL,	/* Linux swap */	LINUX_SWAP_GUID },
	{ 0, NULL,	/* Linux files* */	LINUX_FILES_GUID },
	{ 0, NULL,	/* MacOS X */		MACOS_X_GUID },
	{ 0, NULL,	/* Solaris */		SOLARIS_GUID },
	{ 0, NULL,	/* BeOS/i386 */		BEOS_I386_GUID },
	{ 0, NULL,	/* MacOS X boot */	MACOS_X_BOOT_GUID },
	{ 0, NULL,	/* MacOS X HFS+ */	MACOS_X_HFS_GUID },
	{ 0, NULL,	/* NetBSD */		NETBSD_GUID },
	{ 0, NULL,	/* FreeBSD */		FREEBSD_GUID },
	{ 0, NULL,	/* OpenBSD */		OPENBSD_GUID },
	{ 0, NULL,	/* LinuxSwap DR */	LINUXSWAP_DR_GUID },
	{ 0, NULL,	/* EFI Sys */		EFI_SYSTEM_PARTITION_GUID },
	{ 0, NULL,	/* Win Recovery*/	WIN_RECOVERY_GUID },
	{ 0, NULL,	/* Linux VM */		LINUX_LVM_GUID },
	{ 0, "Microsoft basic data",		MICROSOFT_BASIC_DATA_GUID },
	{ 0, NULL,	/* ChromeKernel */	CHROME_KERNEL_GUID },
#if 0
	/* Sorted as in https://en.wikipedia.org/wiki/GUID_Partition_Table */
	{ 0, "Intel Fast Flash (iFFS)",		INTEL_FAST_FLASH_GUID },
	{ 0, "Sony boot",			SONY_BOOT_GUID },
	{ 0, "Lenovo boot",			LENOVO_BOOT_GUID },

	/* Microsoft Windows */
	{ 0, "Windows Reserved (MSR)",		WINDOWS_RESERVED_GUID },
	{ 0, "Windows LDM metadata",		WINDOWS_LDM_METADATA_GUID },
	{ 0, "Windows IBM General Parallel FS",	WINDOWS_IBM_GPFS_GUID },
	{ 0, "Windows Storage Spaces",		WINDOWS_STORAGE_SPACES_GUID },
	{ 0, "Windows Storage Replica",		WINDOWS_STORAGE_REPLICA_GUID },

	/* HP-UX */
	{ 0, "HP-UX Data",			HP_UX_DATA_GUID },
	{ 0, "HP-UX Service",			HP_UX_SERVICE_GUID },

	/* Linux */
	{ 0, "Linux RAID",			LINUX_RAID_GUID },
	{ 0, "Linux x86 Root",			LINUX_X86_ROOT_GUID },
	{ 0, "Linux x86-64 Root",		LINUX_X86_64_ROOT_GUID },
	{ 0, "Linux Arm32 Root",		LINUX_ARM32_ROOT_GUID },
	{ 0, "Linux Arm64 root",		LINUX_ARM64_ROOT_GUID },
	{ 0, "Linux /boot",			LINUX_BOOT_GUID },
	{ 0, "Linux /home",			LINUX_HOME_GUID },
	{ 0, "Linux /srv (server data)",	LINUX_SRV_GUID },
	{ 0, "Linux Plain dm-crypt",		LINUX_PLAIN_DMCRYPT_GUID },
	{ 0, "Linux LUKS",			LINUX_LUKS_GUID },
	{ 0, "Linux Reserved",			LINUX_RESERVED_GUID },

	/* FreeBSD */
	{ 0, "FreeBSD Boot",			FREEBSD_BOOT_GUID },
	{ 0, "FreeBSD Swap",			FREEBSD_SWAP_GUID },
	{ 0, "FreeBSD UFS",			FREEBSD_UFS_GUID },
	{ 0, "FreeBSD Vinum volume manager",	FREEBSD_VINUM_VOL_MGR_GUID },
	{ 0, "FreeBSD ZFS",			FREEBSD_ZFS_GUID },
	{ 0, "FreeBSD nandfs",			FREEBSD_NANDFS_GUID },

	/* MacOS/Darwin */
	{ 0, "MacOS RAID",			MACOS_RAID_GUID },
	{ 0, "MacOS RAID (offline)",		MACOS_RAID_OFFLINE_GUID },
	{ 0, "MacOS Label",			MACOS_LABEL_GUID },
	{ 0, "MacOS TV Recovery",		MACOS_TV_RECOVERY_GUID },
	{ 0, "MacOS Core Storage container",	MACOS_CORE_STORAGE_GUID },

	/* Solaris/illumos */
	{ 0, "Solaris/illumos boot",		SOLARIS_BOOT_GUID },
	{ 0, "Solaris/illumos Swap",		SOLARIS_SWAP_GUID },
	{ 0, "Solaris/illumos Backup",		SOLARIS_BACKUP_GUID },
	{ 0, "Solaris/illumos /usr|MacOS ZFS",	SOLARIS_USR_MACOS_ZFS_GUID },
	{ 0, "Solaris/illumos /var",		SOLARIS_VAR_GUID },
	{ 0, "Solaris/illumos /home",		SOLARIS_HOME_GUID },
	{ 0, "Solaris/illumos Alt sector",	SOLARIS_ALT_SECTOR_GUID },
	{ 0, "Solaris/illumos Reserved",	SOLARIS_RESERVED1_GUID },
	{ 0, "Solaris/illumos Reserved",	SOLARIS_RESERVED2_GUID },
	{ 0, "Solaris/illumos Reserved",	SOLARIS_RESERVED3_GUID },
	{ 0, "Solaris/illumos Reserved",	SOLARIS_RESERVED4_GUID },
	{ 0, "Solaris/illumos Reserved",	SOLARIS_RESERVED5_GUID },

	/* NetBSD */
	{ 0, "NetBSD Swap",			NETBSD_SWAP_GUID },
	{ 0, "NetBSD LFS",			NETBSD_LFS_GUID },
	{ 0, "NetBSD RAID",			NETBSD_RAID_GUID },
	{ 0, "NetBSD Concatenated",		NETBSD_CONCATENATED_GUID },
	{ 0, "NetBSD Encrypted",		NETBSD_ENCRYPTED_GUID },

	/* ChromeOS */
	{ 0, "ChromeOS rootfs",			CHROMEOS_ROOTFS_GUID },
	{ 0, "ChromeOS firmware",		CHROMEOS_FIRMWARE_GUID },
	{ 0, "ChromeOS future use",		CHROMEOS_FUTURE_USE_GUID },
	{ 0, "ChromeOS miniOS",			CHROMEOS_MINIOS_GUID },
	{ 0, "ChromeOS hibernate",		CHROMEOS_HIBERNATE_GUID },

	/* Container Linux by CoreOS */
	{ 0, "CoreOS /usr",			COREOS_USR_GUID },
	{ 0, "CoreOS Resizable root",		COREOS_RESIZABLE_ROOT_GUID },
	{ 0, "CoreOS OEM",			COREOS_OEM_GUID },
	{ 0, "CoreOS root RAID",		COREOS_ROOT_RAID_GUID },

	/* MidnightBSD */
	{ 0, "MidnightBSD Boot",		MIDNIGHTBSD_BOOT_GUID },
	{ 0, "MidnightBSD Data",		MIDNIGHTBSD_DATA_GUID },
	{ 0, "MidnightBSD Swap",		MIDNIGHTBSD_SWAP_GUID },
	{ 0, "MidnightBSD UFS",			MIDNIGHTBSD_UFS_GUID },
	{ 0, "MidnightBSD Vinum vol mgr",	MIDNIGHTBSD_VINUM_VOL_MGR_GUID },
	{ 0, "MidnightBSD ZFS",			MIDNIGHTBSD_ZFS_GUID },

	/* Ceph */
	{ 0, "Ceph Journal",			CEPH_JOURNAL_GUID },
	{ 0, "Ceph dm-crypt journal",		CEPH_DMCRYPT_JOURNAL_GUID },
	{ 0, "Ceph OSD",			CEPH_OSD_GUID },
	{ 0, "Ceph dm-crypt OSD",		CEPH_DMCRYPT_OSD_GUID },
	{ 0, "Ceph Disk in creation",		CEPH_DISK_IN_CREATION_GUID },
	{ 0, "Ceph dm-crypt disk in creation",	CEPH_DMCRYPT_CREATNG_DISK_GUID },
	{ 0, "Ceph Block",			CEPH_BLOCK_GUID },
	{ 0, "Ceph Block DB",			CEPH_BLOCK_DB_GUID },
	{ 0, "Ceph Block write-ahead log",	CEPH_BLOCK_LOG_GUID },
	{ 0, "Ceph Lockbox for dm-crypt keys",	CEPH_DMCRYPT_KEYS_LOCKBOX_GUID },
	{ 0, "Ceph Multipath OSD",		CEPH_MULTIPATH_OSD_GUID },
	{ 0, "Ceph Multipath journal",		CEPH_MULTIPATH_JOURNAL_GUID },
	{ 0, "Ceph Multipath block A",		CEPH_MULTIPATH_BLOCK_A_GUID },
	{ 0, "Ceph Multipath block B",		CEPH_MULTIPATH_BLOCK_B_GUID },
	{ 0, "Ceph Multipath block DB",		CEPH_MULTIPATH_BLOCK_DB_GUID },
	{ 0, "Ceph Multipath block log",	CEPH_MULTIPATH_BLOCK_LOG_GUID },
	{ 0, "Ceph dm-crypt block",		CEPH_DMCRYPT_BLOCK_GUID },
	{ 0, "Ceph dm-crypt block DB",		CEPH_DMCRYPT_BLOCK_DB_GUID },
	{ 0, "Ceph dm-crypt block log",		CEPH_DMCRYPT_BLOCK_LOG_GUID },
	{ 0, "Ceph dm-crypt LUKS journal",	CEPH_DMCRYPT_LUKS_JOURNAL_GUID },
	{ 0, "Ceph dm-crypt LUKS block",	CEPH_DMCRYPT_LUKS_BLOCK_GUID },
	{ 0, "Ceph dm-crypt LUKS block DB",	CEPH_DMCRYPT_LUKS_BLOCK_DB_GUID },
	{ 0, "Ceph dm-crypt LUKS log",		CEPH_DMCRYPT_LUKS_LOG_GUID },
	{ 0, "Ceph dm-crypt LUKS OSD",		CEPH_DMCRYPT_LUKS_OSD_GUID },

	/* QNX */
	{ 0, "QNX (6) Power-safe file system",	QNX_POWER_SAFE_FS_GUID },

	/* Plan 9 */
	{ 0, "Plan9",				PLAN9_GUID },

	/* VMWare ESX */
	{ 0, "VMWare vmkcore (coredump)",	VMWARE_VMKCORE_GUID },
	{ 0, "VMWare VMFS filesystem",		VMWARE_VMFS_FILESYSTEM_GUID },
	{ 0, "VMWare Reserved",			VMWARE_RESERVED_GUID },

	/* Android-IA */
	{ 0, "Android-IA Bootloader",		ANDROID_IA_BOOT_GUID },
	{ 0, "Android-IA Bootloader2",		ANDROID_IA_BOOT2_GUID },
	{ 0, "Android-IA Boot",			ANDROID_IA_BOOT3_GUID },
	{ 0, "Android-IA Recovery",		ANDROID_IA_RECOVERY_GUID },
	{ 0, "Android-IA Misc",			ANDROID_IA_MISC_GUID },
	{ 0, "Android-IA Metadata",		ANDROID_IA_METADATA_GUID },
	{ 0, "Android-IA System",		ANDROID_IA_SYSTEM_GUID },
	{ 0, "Android-IA Cache",		ANDROID_IA_CACHE_GUID },
	{ 0, "Android-IA Data",			ANDROID_IA_DATA_GUID },
	{ 0, "Android-IA Persistent",		ANDROID_IA_PERSISTENT_GUID },
	{ 0, "Android-IA Vendor",		ANDROID_IA_VENDOR_GUID },
	{ 0, "Android-IA Config",		ANDROID_IA_CFG_GUID },
	{ 0, "Android-IA Factory",		ANDROID_IA_FACTORY_GUID },
	{ 0, "Android-IA Factory (alt)",	ANDROID_IA_FACTORY_ALT_GUID },
	{ 0, "Android-IA Fastboot/Tertiary",	ANDROID_IA_FASTBOOT_GUID },
	{ 0, "Android-IA OEM",			ANDROID_IA_OEM_GUID },

	/* Android 6.0+ ARM */
	{ 0, "Android 6.0+ ARM EXT",		ANDROID_6_0_ARM_EXT_GUID },
	{ 0, "Android 6.0+ ARM Meta",		ANDROID_6_0_ARM_META_GUID },

	/* ONIE */
	{ 0, "ONIE Boot",			ONIE_BOOT_GUID },
	{ 0, "ONIE Config",			ONIE_CFG_GUID },

	/* PowerPC */
	{ 0, "PPC PReP boot",			PPC_PREP_BOOT_GUID },

	/* Atari TOS */
	{ 0, "Atari TOS Basic data",		ATARI_TOS_BASIC_DATA_GUID },

	/* VeraCrypt */
	{ 0, "Veracrypt Encrypted data",	VERACRYPT_ENCRYPTED_DATA_GUID },

	/* ArcaOS (a.k.a. OS/2) */
	{ 0, "ArcaOS Type 1 (OS/2)",		ARCAOS_TYPE_1_GUID },

	/* Storage Performance Development Kit (SPDK) */
	{ 0, "SPDK block device",		SPDK_BLOCK_DEVICE_GUID },

	/* barebox bootloader */
	{ 0, "Barebox barebox-state",		BAREBOX_BAREBOX_STATE_GUID },

	/* U-Boot bootloader */
	{ 0, "U-Boot environment",		U_BOOT_ENVIRONMENT_GUID },

	/* SoftRAID (?) */
	{ 0, "Softraid Status",			SOFTRAID_STATUS_GUID },
	{ 0, "Softraid Scratch",		SOFTRAID_SCRATCH_GUID },
	{ 0, "Softraid Volume",			SOFTRAID_VOLUME_GUID },
	{ 0, "Softraid Cache",			SOFTRAID_CACHE_GUID },

	/* Fuchsia standard partitions */
	{ 0, "Fuchsia bootloader (ABR)",	FUCHSIA_BOOT_GUID },
	{ 0, "Fuchsia encrypted system data",	FUCHSIA_ENCRYPTED_SYS_GUID },
	{ 0, "Fucshia boot metadata (ABR)",	FUCSHIA_BOOT_METADATA_GUID },
	{ 0, "Fuchsia RO Factory system data",	FUCHSIA_RO_FACTORY_SYS_GUID },
	{ 0, "Fuchsia RO Factory boot data",	FUCHSIA_RO_FACTORY_BOOT_GUID },
	{ 0, "Fuchsia Volume Manager",		FUCHSIA_VOLUME_MANAGER_GUID },
	{ 0, "Fuchsia Verified boot meta (ABR)",FUCHSIA_VERIFIED_BOOT_META_GUID },
	{ 0, "Fuchsia Zircon boot (ABR)",	FUCHSIA_ZIRCON_BOOT_ABR_GUID },

	/* Fuchsia legacy partitions */
	{ 0, "Fuchsia Legacy system",		FUCHSIA_L_SYSTEM_GUID },
	{ 0, "Fuchsia Legacy data",		FUCHSIA_L_DATA_GUID },
	{ 0, "Fuchsia Legacy install",		FUCHSIA_L_INSTALL_GUID },
	{ 0, "Fuchsia Legacy blob",		FUCHSIA_L_BLOB_GUID },
	{ 0, "Fuchsia Legacy fvm",		FUCHSIA_L_FVM_GUID },
	{ 0, "Fuchsia Legacy Zircon boot (A)",	FUCHSIA_L_ZIRCON_BOOTA_GUID },
	{ 0, "Fuchsia Legacy Zircon boot (B)",	FUCHSIA_L_ZIRCON_BOOTB_GUID },
	{ 0, "Fuchsia Legacy Zircon boot (R)",	FUCHSIA_L_ZIRCON_BOOTR_GUID },
	{ 0, "Fuchsia Legacy sys-config",	FUCHSIA_L_SYS_CFG_GUID },
	{ 0, "Fuchsia Legacy factory-config",	FUCHSIA_L_FACTORY_CFG_GUID },
	{ 0, "Fuchsia Legacy bootloader",	FUCHSIA_L_BOOT_GUID },
	{ 0, "Fuchsia Legacy guid-test",	FUCHSIA_L_TEST_GUID },
	{ 0, "Fuchsia Legacy Verified boot mA", FUCHSIA_L_VERIFIED_BOOTM_A_GUID },
	{ 0, "Fuchsia Legacy Verified boot mB", FUCHSIA_L_VERIFIED_BOOTM_B_GUID },
	{ 0, "Fuchsia Legacy Verified boot mR", FUCHSIA_L_VERIFIED_BOOTM_R_GUID },
	{ 0, "Fuchsia Legacy misc",		FUCHSIA_L_MISC_GUID },
	{ 0, "Fuchsia Legacy emmc-boot1",	FUCHSIA_L_EMMC_BOOT1_GUID },
	{ 0, "Fuchsia Legacy emmc-boot2",	FUCHSIA_L_EMMC_BOOT2_GUID },

	/* Systemd Discoverable Partitions */
	{ 0, "Linux x86-64 root verity",	LINUX_X86_64_ROOT_VERITY_GUID },
	{ 0, "Linux IA-64 /usr",		LINUX_IA_64_USR_GUID },
	{ 0, "Linux /var",			LINUX_VAR_GUID },
	{ 0, "Linux Arm64 /usr verity",		LINUX_ARM64_USR_VERITY_GUID },
	{ 0, "Linux Arm32 root verity",		LINUX_ARM32_ROOT_VERITY_GUID },
	{ 0, "Linux user's home",		LINUX_USER_HOME_GUID },
	{ 0, "Linux x86-64 /usr verity",	LINUX_X86_64_USR_VERITY_GUID },
	{ 0, "Linux Arm32 /usr",		LINUX_ARM32_USR_GUID },
	{ 0, "Linux /var/tmp",			LINUX_VAR_TMP_GUID },
	{ 0, "Linux IA-64 /usr verity",		LINUX_IA_64_USR_VERITY_GUID },
	{ 0, "Linux x86 /usr",			LINUX_X86_USR_GUID },
	{ 0, "Linux x86-64 /usr",		LINUX_X86_64_USR_GUID },
	{ 0, "Linux IA-64 root verity",		LINUX_IA_64_ROOT_VERITY_GUID },
	{ 0, "Linux x86 /usr verity",		LINUX_X86_USR_VERITY_GUID },
	{ 0, "Linux IA-64 root",		LINUX_IA_64_ROOT_GUID },
	{ 0, "Linux Arm64 /usr",		LINUX_ARM64_USR_GUID },
	{ 0, "Linux Arm32 /usr verity",		LINUX_ARM32_USR_VERITY_GUID },
	{ 0, "Linux x86 root verity",		LINUX_X86_ROOT_VERITY_GUID },
	{ 0, "Linux Arm64 root verity",		LINUX_ARM64_ROOT_VERITY_GUID },
#endif
};

struct menu_item {
	int	 mi_menuid;	/* Unique hex octet */
	int	 mi_mbrid;	/* -1 == not on MBR menu */
	char 	*mi_name;	/* Truncated at 14 chars */
	char	*mi_guid;	/* NULL == not on GPT menu */
};

const struct menu_item menu_items[] = {
	{ 0x00,	0x00,	"Unused",	UNUSED_GUID },
	{ 0x01,	0x01,	"FAT12",	MICROSOFT_BASIC_DATA_GUID },
	{ 0x02,	0x02,	"XENIX /",	NULL },
	{ 0x03,	0x03,	"XENIX /usr",	NULL },
	{ 0x04,	0x04,	"FAT16",	MICROSOFT_BASIC_DATA_GUID },
	{ 0x05,	0x05,	"Extended DOS",	NULL },
	{ 0x06,	0x06,	"FAT16B",	MICROSOFT_BASIC_DATA_GUID },
	{ 0x07,	0x07,	"NTFS",		MICROSOFT_BASIC_DATA_GUID },
	{ 0x08,	0x08,	"AIX fs",	NULL },
	{ 0x09,	0x09,	"AIX/Coherent",	NULL },
	{ 0x0A,	0x0A,	"OS/2 Bootmgr",	NULL },
	{ 0x0B,	0x0B,	"FAT32",	MICROSOFT_BASIC_DATA_GUID },
	{ 0x0C,	0x0C,	"FAT32 (LBA)",	MICROSOFT_BASIC_DATA_GUID },
	{ 0x0D,   -1,	"BIOS boot",	BIOS_BOOT_GUID },
	{ 0x0E,	0x0E,	"FAT16B (LBA)",	MICROSOFT_BASIC_DATA_GUID },
	{ 0x0F,	0x0F,	"Extended LBA",	NULL },
	{ 0x10,	0x10,	"OPUS",		NULL },
	{ 0x11,	0x11,	"OS/2 hidden",	MICROSOFT_BASIC_DATA_GUID },
	{ 0x12,	0x12,	"Compaq Diag",	NULL },
	{ 0x14,	0x14,	"OS/2 hidden",	MICROSOFT_BASIC_DATA_GUID },
	{ 0x16,	0x16,	"OS/2 hidden",	MICROSOFT_BASIC_DATA_GUID },
	{ 0x17,	0x17,	"OS/2 hidden",	MICROSOFT_BASIC_DATA_GUID },
	{ 0x18,	0x18,	"AST swap",	NULL },
	{ 0x19,	0x19,	"Willowtech",	NULL },
	{ 0x1C,	0x1C,	"ThinkPad Rec",	MICROSOFT_BASIC_DATA_GUID },
	{ 0x20,	0x20,	"Willowsoft",	NULL },
	{ 0x24,	0x24,	"NEC DOS",	NULL },
	{ 0x27,	0x27,	"Win Recovery",	WIN_RECOVERY_GUID },
	{ 0x38,	0x38,	"Theos",	NULL },
	{ 0x39,	0x39,	"Plan 9",	NULL },
	{ 0x40,	0x40,	"VENIX 286",	NULL },
	{ 0x41,	0x41,	"Lin/Minux DR",	NULL },
	{ 0x42,	0x42,	"LinuxSwap DR",	LINUXSWAP_DR_GUID },
	{ 0x43,	0x43,	"Linux DR",	NULL },
	{ 0x4D,	0x4D,	"QNX 4.2 Pri",	NULL },
	{ 0x4E,	0x4E,	"QNX 4.2 Sec",	NULL },
	{ 0x4F,	0x4F,	"QNX 4.2 Ter",	NULL },
	{ 0x50,	0x50,	"DM",		NULL },
	{ 0x51,	0x51,	"DM",		NULL },
	{ 0x52,	0x52,	"CP/M or SysV",	NULL },
	{ 0x53,	0x53,	"DM",		NULL },
	{ 0x54,	0x54,	"Ontrack",	NULL },
	{ 0x55,	0x55,	"EZ-Drive",	NULL },
	{ 0x56,	0x56,	"Golden Bow",	NULL },
	{ 0x5C,	0x5C,	"Priam"	,	NULL },
	{ 0x61,	0x61,	"SpeedStor",	NULL },
	{ 0x63,	0x63,	"ISC, HURD, *",	NULL },
	{ 0x64,	0x64,	"NetWare 2.xx",	NULL },
	{ 0x65,	0x65,	"NetWare 3.xx",	NULL },
	{ 0x66,	0x66,	"NetWare 386",	NULL },
	{ 0x67,	0x67,	"Novell",	NULL },
	{ 0x68,	0x68,	"Novell",	NULL },
	{ 0x69,	0x69,	"Novell",	NULL },
	{ 0x70,	0x70,	"DiskSecure",	NULL },
	{ 0x75,	0x75,	"PCIX",		NULL },
	{ 0x7F,   -1,	"Chrome Kernel",CHROME_KERNEL_GUID },
	{ 0x80, 0x80,	"Minix (old)",	NULL },
	{ 0x81, 0x81,	"Minix (new)",	NULL },
	{ 0x82,	0x82,	"Linux swap",	LINUX_SWAP_GUID },
	{ 0x83,	0x83,	"Linux files*",	LINUX_FILES_GUID },
	{ 0x84,	0x84,	"OS/2 hidden",	NULL },
	{ 0x85,	0x85,	"Linux ext.",	NULL },
	{ 0x86, 0x86,	"NT FAT VS",	NULL },
	{ 0x87, 0x87,	"NTFS VS",	NULL },
	{ 0x8E,	0x8E,	"Linux LVM",	LINUX_LVM_GUID },
	{ 0x93,	0x93,	"Amoeba FS",	NULL },
	{ 0x94,	0x94,	"Amoeba BBT",	NULL },
	{ 0x99,	0x99,	"Mylex"	,	NULL },
	{ 0x9F,	0x9F,	"BSDI",		NULL },
	{ 0xA0,	0xA0,	"NotebookSave",	NULL },
	{ 0xA5,	0xA5,	"FreeBSD",	FREEBSD_GUID },
	{ 0xA6,	0xA6,	"OpenBSD",	OPENBSD_GUID },
	{ 0xA7,	0xA7,	"NeXTSTEP",	NULL },
	{ 0xA8,	0xA8,	"MacOS X",	MACOS_X_GUID },
	{ 0xA9,	0xA9,	"NetBSD",	NETBSD_GUID },
	{ 0xAB,	0xAB,	"MacOS X boot",	MACOS_X_BOOT_GUID },
	{ 0xAF,	0xAF,	"MacOS X HFS+",	MACOS_X_HFS_GUID },
	{ 0xB0,	  -1,	"APFS",		APFS_GUID },
	{ 0xB1,	  -1,	"APFS ISC",	APFS_ISC_GUID },
	{ 0xB2,	  -1,	"APFS Recovery",APFS_RECOVERY_GUID },
	{ 0xB3,	  -1,	"HiFive FSBL",	HIFIVE_FSBL_GUID },
	{ 0xB4,	  -1,	"HiFive BBL",	HIFIVE_BBL_GUID },
	{ 0xB7,	0xB7,	"BSDI filesy*",	NULL },
	{ 0xB8,	0xB8,	"BSDI swap",	NULL },
	{ 0xBF,	0xBF,	"Solaris",	SOLARIS_GUID },
	{ 0xC0,	0xC0,	"CTOS",		NULL },
	{ 0xC1,	0xC1,	"DRDOSs FAT12",	NULL },
	{ 0xC4,	0xC4,	"DRDOSs < 32M",	NULL },
	{ 0xC6,	0xC6,	"DRDOSs >=32M",	NULL },
	{ 0xC7,	0xC7,	"HPFS Disbled",	NULL },
	{ 0xDB,	0xDB,	"CPM/C.DOS/C*",	NULL },
	{ 0xDE,	0xDE,	"Dell Maint",	NULL },
	{ 0xE1,	0xE1,	"SpeedStor",	NULL },
	{ 0xE3,	0xE3,	"SpeedStor",	NULL },
	{ 0xE4,	0xE4,	"SpeedStor",	NULL },
	{ 0xEB,	0xEB,	"BeOS/i386",	BEOS_I386_GUID },
	{ 0xEC,	  -1,	"Legacy MBR",	LEGACY_MBR_GUID },
	{ 0xEE,	0xEE,	"EFI GPT",	NULL },
	{ 0xEF,	0xEF,	"EFI Sys",	EFI_SYSTEM_PARTITION_GUID },
	{ 0xF1,	0xF1,	"SpeedStor",	NULL },
	{ 0xF2,	0xF2,	"DOS 3.3+ Sec",	NULL },
	{ 0xF4,	0xF4,	"SpeedStor",	NULL },
	{ 0xFF,	0xFF,	"Xenix BBT",	NULL },
};

void			 chs_to_dp(const unsigned char, const struct chs *,
    uint8_t *, uint8_t *, uint8_t *);
const struct gpt_type	*find_gpt_type(const struct uuid *);
const struct menu_item	*find_gpt_menuitem(const char *);
int			 gpt_item(const unsigned int);

const struct mbr_type	*find_mbr_type(const int);
const struct menu_item	*find_mbr_menuitem(const struct mbr_type *);
const char		*find_mbr_desc(const struct mbr_type *);
int			 mbr_item(const unsigned int);

void			 print_menu(int (*)(const unsigned int),
    const unsigned int);
const struct menu_item	*nth_menu_item(int (*)(const unsigned int),
    unsigned int);

void
chs_to_dp(const unsigned char prt_id, const struct chs *chs, uint8_t *dp_cyl,
    uint8_t *dp_hd, uint8_t *dp_sect)
{
	uint64_t		cyl = chs->chs_cyl;
	uint32_t		head = chs->chs_head;
	uint32_t		sect = chs->chs_sect;

	if (head > 254 || sect > 63 || cyl > 1023) {
		/* Set max values to trigger LBA. */
		head = (prt_id == DOSPTYP_EFI) ? 255 : 254;
		sect = 63;
		cyl = 1023;
	}

	*dp_hd = head & 0xFF;
	*dp_sect = (sect & 0x3F) | ((cyl & 0x300) >> 2);
	*dp_cyl = cyl & 0xFF;
}

const struct gpt_type *
find_gpt_type(const struct uuid *uuid)
{
	const struct gpt_type	*gt = NULL;
	char			*guid;
	unsigned int		 i;
	uint32_t		 status;

	uuid_to_string(uuid, &guid, &status);
	if (status == uuid_s_ok) {
		for (i = 0; i < nitems(gpt_types) && gt == NULL; i++) {
			if (strcasecmp(gpt_types[i].gt_guid, guid) == 0)
				gt = &gpt_types[i];
		}
	}
	free(guid);

	return gt;
}

const struct menu_item *
find_gpt_menuitem(const char *guid)
{
	unsigned int		i;

	for (i = 0; i < nitems(menu_items); i++) {
		if (gpt_item(i) == 0 &&
		    strcasecmp(menu_items[i].mi_guid, guid) == 0)
			return &menu_items[i];
	}

	return NULL;
}

int
gpt_item(const unsigned int item)
{
	return menu_items[item].mi_guid == NULL;
}

const struct mbr_type *
find_mbr_type(const int id)
{
	const struct mbr_type	*mt = NULL;
	unsigned int		 i;

	if (id >= 0) {
		for (i = 0; i < nitems(mbr_types); i++) {
			if (mbr_types[i].mt_type == id)
				mt = &mbr_types[i];
		}
	}

	return mt;
}

const struct menu_item *
find_mbr_menuitem(const struct mbr_type *mt)
{
	unsigned int		i;

	if (mt != NULL) {
		for (i = 0; i < nitems(menu_items); i++) {
			if (mbr_item(i) == 0
			    && menu_items[i].mi_mbrid == mt->mt_type)
				return &menu_items[i];
		}
	}

	return NULL;
}

const char *
find_mbr_desc(const struct mbr_type *mt)
{
	const struct menu_item	*mi;

	if (mt != NULL) {
		if (mt->mt_desc != NULL)
			return mt->mt_desc;
		mi = find_mbr_menuitem(mt);
		if (mi)
			return mi->mi_name;
	}

	return NULL;
}

int
mbr_item(const unsigned int item)
{
	return menu_items[item].mi_mbrid == -1;
}

void
print_menu(int (*test)(const unsigned int), const unsigned int columns)
{
	const struct menu_item	 *mi;
	unsigned int		  i, j, rows;

	for (i = 0, j= 0; i < nitems(menu_items); i++)
		j += test(i) == 0;
	rows = (j + columns - 1) / columns;

	for (i = 0; i < rows; i++) {
		for (j = 0; j < columns; j++) {
			mi = nth_menu_item(test, i + j * rows);
			if (mi == NULL)
				break;
			printf("%02X %-15s", mi->mi_menuid, mi->mi_name);
		}
		printf("\n");
	}
}

const struct menu_item *
nth_menu_item(int (*test)(const unsigned int), unsigned int n)
{
	unsigned int			i;

	for (i = 0; i < nitems(menu_items); i++) {
		if (test(i) == 0) {
			if (n == 0)
				return &menu_items[i];
			n--;
		}
	}

	return NULL;
}

int
PRT_protected_uuid(const struct uuid *uuid)
{
	const struct gpt_type	*gt;
	unsigned int		 pn;

	gt = find_gpt_type(uuid);
	if (gt == NULL)
		return 0;	/* We don't know this type, so no protection. */
	if (gt->gt_attr & GTATTR_PROTECT)
		return 1;	/* Protected! */
	if (strcasecmp(gt->gt_guid, EFI_SYSTEM_PARTITION_GUID))
		return 0;	/* Not EFI Sys, so not protected. */

	for (pn = 0; pn < gh.gh_part_num; pn++) {
		gt = find_gpt_type(&gp[pn].gp_type);
		if (gt && (gt->gt_attr & GTATTR_PROTECT_EFISYS))
			return 1;	/* EFI Sys must be protected! */
	}

	return 0;
}

void
PRT_print_mbrmenu(void)
{
#define	MBR_MENU_COLUMNS	4

	printf("Choose from the following Partition id values:\n");
	print_menu(mbr_item,  MBR_MENU_COLUMNS);
}

void
PRT_print_gptmenu(void)
{
#define	GPT_MENU_COLUMNS	4

	printf("Choose from the following Partition id values:\n");
	print_menu(gpt_item, GPT_MENU_COLUMNS);
}

void
PRT_dp_to_prt(const struct dos_partition *dp, const uint64_t lba_self,
    const uint64_t lba_firstembr, struct prt *prt)
{
	off_t			off;
	uint32_t		t;

	prt->prt_flag = dp->dp_flag;
	prt->prt_id = dp->dp_typ;

	if ((prt->prt_id == DOSPTYP_EXTEND) || (prt->prt_id == DOSPTYP_EXTENDL))
		off = lba_firstembr;
	else
		off = lba_self;

	memcpy(&t, &dp->dp_start, sizeof(uint32_t));
	prt->prt_bs = letoh32(t) + off;
	memcpy(&t, &dp->dp_size, sizeof(uint32_t));
	prt->prt_ns = letoh32(t);
	if (prt->prt_id == DOSPTYP_EFI && prt->prt_ns == UINT32_MAX)
		prt->prt_ns = DL_GETDSIZE(&dl) - prt->prt_bs;
}

void
PRT_prt_to_dp(const struct prt *prt, const uint64_t lba_self,
    const uint64_t lba_firstembr, struct dos_partition *dp)
{
	struct chs		start, end;
	uint64_t		off, t;

	if (prt->prt_ns == 0 || prt->prt_id == DOSPTYP_UNUSED) {
		memset(dp, 0, sizeof(*dp));
		return;
	}

	if ((prt->prt_id == DOSPTYP_EXTEND) || (prt->prt_id == DOSPTYP_EXTENDL))
		off = lba_firstembr;
	else
		off = lba_self;

	PRT_lba_to_chs(prt, &start, &end);
	chs_to_dp(prt->prt_id, &start, &dp->dp_scyl, &dp->dp_shd, &dp->dp_ssect);
	chs_to_dp(prt->prt_id, &end, &dp->dp_ecyl, &dp->dp_ehd, &dp->dp_esect);

	dp->dp_flag = prt->prt_flag & 0xFF;
	dp->dp_typ = prt->prt_id & 0xFF;

	t = htole64(prt->prt_bs - off);
	memcpy(&dp->dp_start, &t, sizeof(uint32_t));
	if (prt->prt_id == DOSPTYP_EFI && (prt->prt_bs + prt->prt_ns) >
	    DL_GETDSIZE(&dl))
		t = htole64(UINT32_MAX);
	else
		t = htole64(prt->prt_ns);
	memcpy(&dp->dp_size, &t, sizeof(uint32_t));
}

void
PRT_print_parthdr(void)
{
	printf("            Starting         Ending    "
	    "     LBA Info:\n");
	printf(" #: id      C   H   S -      C   H   S "
	    "[       start:        size ]\n");
	printf("---------------------------------------"
	    "----------------------------------------\n");
}

void
PRT_print_part(const int num, const struct prt *prt, const char *units)
{
	const struct unit_type	*ut;
	const char		*desc;
	struct chs		 start, end;
	double			 size;

	size = units_size(units, prt->prt_ns, &ut);
	PRT_lba_to_chs(prt, &start, &end);
	desc = find_mbr_desc(find_mbr_type(prt->prt_id));

	printf("%c%1d: %.2X %6llu %3u %3u - %6llu %3u %3u [%12llu:%12.0f%s] "
	    "%s\n", (prt->prt_flag == DOSACTIVE) ? '*' : ' ', num,
	    prt->prt_id, start.chs_cyl, start.chs_head, start.chs_sect,
	    end.chs_cyl, end.chs_head, end.chs_sect,
	    prt->prt_bs, size, ut->ut_abbr, desc ? desc : "<Unknown ID>");

	if (prt->prt_bs >= DL_GETDSIZE(&dl))
		printf("partition %d starts beyond the end of %s\n", num,
		    disk.dk_name);
	else if (prt->prt_bs + prt->prt_ns > DL_GETDSIZE(&dl))
		printf("partition %d extends beyond the end of %s\n", num,
		    disk.dk_name);
}

void
PRT_lba_to_chs(const struct prt *prt, struct chs *start, struct chs *end)
{
	uint64_t		lba;

	if (prt->prt_ns == 0 || prt->prt_id == DOSPTYP_UNUSED) {
		memset(start, 0, sizeof(*start));
		memset(end, 0, sizeof(*end));
		return;
	}

	/*
	 * C = LBA ÷ (HPC × SPT)
	 * H = (LBA ÷ SPT) mod HPC
	 * S = (LBA mod SPT) + 1
	 */

	lba = prt->prt_bs;
	start->chs_cyl = lba / (disk.dk_sectors * disk.dk_heads);
	start->chs_head = (lba / disk.dk_sectors) % disk.dk_heads;
	start->chs_sect = (lba % disk.dk_sectors) + 1;

	lba = prt->prt_bs + prt->prt_ns - 1;
	end->chs_cyl = lba / (disk.dk_sectors * disk.dk_heads);
	end->chs_head = (lba / disk.dk_sectors) % disk.dk_heads;
	end->chs_sect = (lba % disk.dk_sectors) + 1;
}

const char *
PRT_uuid_to_desc(const struct uuid *uuid, int menuid)
{
	static char		 id[UUID_STR_LEN + 1];
	char			*guid;
	const struct gpt_type	*gt;
	const struct menu_item	*mi;
	uint32_t		 status;

	gt = find_gpt_type(uuid);
	if (gt) {
		mi = find_gpt_menuitem(gt->gt_guid);
		if (mi && menuid) {
			snprintf(id, sizeof(id), "%02X", mi->mi_menuid);
			return id;
		}
		if (gt->gt_desc)
			return gt->gt_desc;
		else if (mi)
			return mi->mi_name;
		else
			return gt->gt_guid;
	}

	uuid_to_string(uuid, &guid, &status);
	if (status == uuid_s_ok)
		strlcpy(id, guid, sizeof(id));
	free(guid);
	return status == uuid_s_ok ? id : "00";
}

/*
 * Accept gt_desc, gt_guid, mi_name or mi_menuid and return the
 * associated GUID. Or NULL if none found.
 */
const char *
PRT_desc_to_guid(const char *desc)
{
	char			octet[3];
	unsigned int		i;
	int			menuid = -1;

	for (i = 0; i < nitems(gpt_types); i++) {
		if (gpt_types[i].gt_desc &&
		    strcasecmp(gpt_types[i].gt_desc, desc) == 0)
			return gpt_types[i].gt_guid;
		if (strcasecmp(gpt_types[i].gt_guid, desc) == 0)
			return gpt_types[i].gt_guid;
	}

	if (strlcpy(octet, desc, sizeof(octet)) < sizeof(octet))
		menuid = hex_octet(octet);

	for (i = 0; i < nitems(menu_items); i++) {
		if (gpt_item(i) == 0 &&
		    (strcasecmp(menu_items[i].mi_name, desc) == 0 ||
		     menu_items[i].mi_menuid == menuid))
			return menu_items[i].mi_guid;
	}

	return NULL;
}
