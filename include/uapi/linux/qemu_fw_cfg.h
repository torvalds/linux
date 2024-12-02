/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef _LINUX_FW_CFG_H
#define _LINUX_FW_CFG_H

#include <linux/types.h>

#define FW_CFG_ACPI_DEVICE_ID	"QEMU0002"

/* selector key values for "well-known" fw_cfg entries */
#define FW_CFG_SIGNATURE	0x00
#define FW_CFG_ID		0x01
#define FW_CFG_UUID		0x02
#define FW_CFG_RAM_SIZE		0x03
#define FW_CFG_NOGRAPHIC	0x04
#define FW_CFG_NB_CPUS		0x05
#define FW_CFG_MACHINE_ID	0x06
#define FW_CFG_KERNEL_ADDR	0x07
#define FW_CFG_KERNEL_SIZE	0x08
#define FW_CFG_KERNEL_CMDLINE	0x09
#define FW_CFG_INITRD_ADDR	0x0a
#define FW_CFG_INITRD_SIZE	0x0b
#define FW_CFG_BOOT_DEVICE	0x0c
#define FW_CFG_NUMA		0x0d
#define FW_CFG_BOOT_MENU	0x0e
#define FW_CFG_MAX_CPUS		0x0f
#define FW_CFG_KERNEL_ENTRY	0x10
#define FW_CFG_KERNEL_DATA	0x11
#define FW_CFG_INITRD_DATA	0x12
#define FW_CFG_CMDLINE_ADDR	0x13
#define FW_CFG_CMDLINE_SIZE	0x14
#define FW_CFG_CMDLINE_DATA	0x15
#define FW_CFG_SETUP_ADDR	0x16
#define FW_CFG_SETUP_SIZE	0x17
#define FW_CFG_SETUP_DATA	0x18
#define FW_CFG_FILE_DIR		0x19

#define FW_CFG_FILE_FIRST	0x20
#define FW_CFG_FILE_SLOTS_MIN	0x10

#define FW_CFG_WRITE_CHANNEL	0x4000
#define FW_CFG_ARCH_LOCAL	0x8000
#define FW_CFG_ENTRY_MASK	(~(FW_CFG_WRITE_CHANNEL | FW_CFG_ARCH_LOCAL))

#define FW_CFG_INVALID		0xffff

/* width in bytes of fw_cfg control register */
#define FW_CFG_CTL_SIZE		0x02

/* fw_cfg "file name" is up to 56 characters (including terminating nul) */
#define FW_CFG_MAX_FILE_PATH	56

/* size in bytes of fw_cfg signature */
#define FW_CFG_SIG_SIZE 4

/* FW_CFG_ID bits */
#define FW_CFG_VERSION		0x01
#define FW_CFG_VERSION_DMA	0x02

/* fw_cfg file directory entry type */
struct fw_cfg_file {
	__be32 size;
	__be16 select;
	__u16 reserved;
	char name[FW_CFG_MAX_FILE_PATH];
};

/* FW_CFG_DMA_CONTROL bits */
#define FW_CFG_DMA_CTL_ERROR	0x01
#define FW_CFG_DMA_CTL_READ	0x02
#define FW_CFG_DMA_CTL_SKIP	0x04
#define FW_CFG_DMA_CTL_SELECT	0x08
#define FW_CFG_DMA_CTL_WRITE	0x10

#define FW_CFG_DMA_SIGNATURE    0x51454d5520434647ULL /* "QEMU CFG" */

/* Control as first field allows for different structures selected by this
 * field, which might be useful in the future
 */
struct fw_cfg_dma_access {
	__be32 control;
	__be32 length;
	__be64 address;
};

#define FW_CFG_VMCOREINFO_FILENAME "etc/vmcoreinfo"

#define FW_CFG_VMCOREINFO_FORMAT_NONE 0x0
#define FW_CFG_VMCOREINFO_FORMAT_ELF 0x1

struct fw_cfg_vmcoreinfo {
	__le16 host_format;
	__le16 guest_format;
	__le32 size;
	__le64 paddr;
};

#endif
