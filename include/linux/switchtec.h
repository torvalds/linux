/*
 * Microsemi Switchtec PCIe Driver
 * Copyright (c) 2017, Microsemi Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef _SWITCHTEC_H
#define _SWITCHTEC_H

#include <linux/pci.h>
#include <linux/cdev.h>

#define SWITCHTEC_MRPC_PAYLOAD_SIZE 1024
#define SWITCHTEC_MAX_PFF_CSR 48

#define SWITCHTEC_EVENT_OCCURRED BIT(0)
#define SWITCHTEC_EVENT_CLEAR    BIT(0)
#define SWITCHTEC_EVENT_EN_LOG   BIT(1)
#define SWITCHTEC_EVENT_EN_CLI   BIT(2)
#define SWITCHTEC_EVENT_EN_IRQ   BIT(3)
#define SWITCHTEC_EVENT_FATAL    BIT(4)

enum {
	SWITCHTEC_GAS_MRPC_OFFSET       = 0x0000,
	SWITCHTEC_GAS_TOP_CFG_OFFSET    = 0x1000,
	SWITCHTEC_GAS_SW_EVENT_OFFSET   = 0x1800,
	SWITCHTEC_GAS_SYS_INFO_OFFSET   = 0x2000,
	SWITCHTEC_GAS_FLASH_INFO_OFFSET = 0x2200,
	SWITCHTEC_GAS_PART_CFG_OFFSET   = 0x4000,
	SWITCHTEC_GAS_NTB_OFFSET        = 0x10000,
	SWITCHTEC_GAS_PFF_CSR_OFFSET    = 0x134000,
};

struct mrpc_regs {
	u8 input_data[SWITCHTEC_MRPC_PAYLOAD_SIZE];
	u8 output_data[SWITCHTEC_MRPC_PAYLOAD_SIZE];
	u32 cmd;
	u32 status;
	u32 ret_value;
} __packed;

enum mrpc_status {
	SWITCHTEC_MRPC_STATUS_INPROGRESS = 1,
	SWITCHTEC_MRPC_STATUS_DONE = 2,
	SWITCHTEC_MRPC_STATUS_ERROR = 0xFF,
	SWITCHTEC_MRPC_STATUS_INTERRUPTED = 0x100,
};

struct sw_event_regs {
	u64 event_report_ctrl;
	u64 reserved1;
	u64 part_event_bitmap;
	u64 reserved2;
	u32 global_summary;
	u32 reserved3[3];
	u32 stack_error_event_hdr;
	u32 stack_error_event_data;
	u32 reserved4[4];
	u32 ppu_error_event_hdr;
	u32 ppu_error_event_data;
	u32 reserved5[4];
	u32 isp_error_event_hdr;
	u32 isp_error_event_data;
	u32 reserved6[4];
	u32 sys_reset_event_hdr;
	u32 reserved7[5];
	u32 fw_exception_hdr;
	u32 reserved8[5];
	u32 fw_nmi_hdr;
	u32 reserved9[5];
	u32 fw_non_fatal_hdr;
	u32 reserved10[5];
	u32 fw_fatal_hdr;
	u32 reserved11[5];
	u32 twi_mrpc_comp_hdr;
	u32 twi_mrpc_comp_data;
	u32 reserved12[4];
	u32 twi_mrpc_comp_async_hdr;
	u32 twi_mrpc_comp_async_data;
	u32 reserved13[4];
	u32 cli_mrpc_comp_hdr;
	u32 cli_mrpc_comp_data;
	u32 reserved14[4];
	u32 cli_mrpc_comp_async_hdr;
	u32 cli_mrpc_comp_async_data;
	u32 reserved15[4];
	u32 gpio_interrupt_hdr;
	u32 gpio_interrupt_data;
	u32 reserved16[4];
	u32 gfms_event_hdr;
	u32 gfms_event_data;
	u32 reserved17[4];
} __packed;

enum {
	SWITCHTEC_CFG0_RUNNING = 0x04,
	SWITCHTEC_CFG1_RUNNING = 0x05,
	SWITCHTEC_IMG0_RUNNING = 0x03,
	SWITCHTEC_IMG1_RUNNING = 0x07,
};

struct sys_info_regs {
	u32 device_id;
	u32 device_version;
	u32 firmware_version;
	u32 reserved1;
	u32 vendor_table_revision;
	u32 table_format_version;
	u32 partition_id;
	u32 cfg_file_fmt_version;
	u16 cfg_running;
	u16 img_running;
	u32 reserved2[57];
	char vendor_id[8];
	char product_id[16];
	char product_revision[4];
	char component_vendor[8];
	u16 component_id;
	u8 component_revision;
} __packed;

struct flash_info_regs {
	u32 flash_part_map_upd_idx;

	struct active_partition_info {
		u32 address;
		u32 build_version;
		u32 build_string;
	} active_img;

	struct active_partition_info active_cfg;
	struct active_partition_info inactive_img;
	struct active_partition_info inactive_cfg;

	u32 flash_length;

	struct partition_info {
		u32 address;
		u32 length;
	} cfg0;

	struct partition_info cfg1;
	struct partition_info img0;
	struct partition_info img1;
	struct partition_info nvlog;
	struct partition_info vendor[8];
};

enum {
	SWITCHTEC_NTB_REG_INFO_OFFSET   = 0x0000,
	SWITCHTEC_NTB_REG_CTRL_OFFSET   = 0x4000,
	SWITCHTEC_NTB_REG_DBMSG_OFFSET  = 0x64000,
};

struct ntb_info_regs {
	u8  partition_count;
	u8  partition_id;
	u16 reserved1;
	u64 ep_map;
	u16 requester_id;
	u16 reserved2;
	u32 reserved3[4];
	struct nt_partition_info {
		u32 xlink_enabled;
		u32 target_part_low;
		u32 target_part_high;
		u32 reserved;
	} ntp_info[48];
} __packed;

struct part_cfg_regs {
	u32 status;
	u32 state;
	u32 port_cnt;
	u32 usp_port_mode;
	u32 usp_pff_inst_id;
	u32 vep_pff_inst_id;
	u32 dsp_pff_inst_id[47];
	u32 reserved1[11];
	u16 vep_vector_number;
	u16 usp_vector_number;
	u32 port_event_bitmap;
	u32 reserved2[3];
	u32 part_event_summary;
	u32 reserved3[3];
	u32 part_reset_hdr;
	u32 part_reset_data[5];
	u32 mrpc_comp_hdr;
	u32 mrpc_comp_data[5];
	u32 mrpc_comp_async_hdr;
	u32 mrpc_comp_async_data[5];
	u32 dyn_binding_hdr;
	u32 dyn_binding_data[5];
	u32 reserved4[159];
} __packed;

enum {
	NTB_CTRL_PART_OP_LOCK = 0x1,
	NTB_CTRL_PART_OP_CFG = 0x2,
	NTB_CTRL_PART_OP_RESET = 0x3,

	NTB_CTRL_PART_STATUS_NORMAL = 0x1,
	NTB_CTRL_PART_STATUS_LOCKED = 0x2,
	NTB_CTRL_PART_STATUS_LOCKING = 0x3,
	NTB_CTRL_PART_STATUS_CONFIGURING = 0x4,
	NTB_CTRL_PART_STATUS_RESETTING = 0x5,

	NTB_CTRL_BAR_VALID = 1 << 0,
	NTB_CTRL_BAR_DIR_WIN_EN = 1 << 4,
	NTB_CTRL_BAR_LUT_WIN_EN = 1 << 5,

	NTB_CTRL_REQ_ID_EN = 1 << 0,

	NTB_CTRL_LUT_EN = 1 << 0,

	NTB_PART_CTRL_ID_PROT_DIS = 1 << 0,
};

struct ntb_ctrl_regs {
	u32 partition_status;
	u32 partition_op;
	u32 partition_ctrl;
	u32 bar_setup;
	u32 bar_error;
	u16 lut_table_entries;
	u16 lut_table_offset;
	u32 lut_error;
	u16 req_id_table_size;
	u16 req_id_table_offset;
	u32 req_id_error;
	u32 reserved1[7];
	struct {
		u32 ctl;
		u32 win_size;
		u64 xlate_addr;
	} bar_entry[6];
	u32 reserved2[216];
	u32 req_id_table[512];
	u32 reserved3[256];
	u64 lut_entry[512];
} __packed;

#define NTB_DBMSG_IMSG_STATUS BIT_ULL(32)
#define NTB_DBMSG_IMSG_MASK   BIT_ULL(40)

struct ntb_dbmsg_regs {
	u32 reserved1[1024];
	u64 odb;
	u64 odb_mask;
	u64 idb;
	u64 idb_mask;
	u8  idb_vec_map[64];
	u32 msg_map;
	u32 reserved2;
	struct {
		u32 msg;
		u32 status;
	} omsg[4];

	struct {
		u32 msg;
		u8  status;
		u8  mask;
		u8  src;
		u8  reserved;
	} imsg[4];

	u8 reserved3[3928];
	u8 msix_table[1024];
	u8 reserved4[3072];
	u8 pba[24];
	u8 reserved5[4072];
} __packed;

enum {
	SWITCHTEC_PART_CFG_EVENT_RESET = 1 << 0,
	SWITCHTEC_PART_CFG_EVENT_MRPC_CMP = 1 << 1,
	SWITCHTEC_PART_CFG_EVENT_MRPC_ASYNC_CMP = 1 << 2,
	SWITCHTEC_PART_CFG_EVENT_DYN_PART_CMP = 1 << 3,
};

struct pff_csr_regs {
	u16 vendor_id;
	u16 device_id;
	u16 pcicmd;
	u16 pcists;
	u32 pci_class;
	u32 pci_opts;
	union {
		u32 pci_bar[6];
		u64 pci_bar64[3];
	};
	u32 pci_cardbus;
	u32 pci_subsystem_id;
	u32 pci_expansion_rom;
	u32 pci_cap_ptr;
	u32 reserved1;
	u32 pci_irq;
	u32 pci_cap_region[48];
	u32 pcie_cap_region[448];
	u32 indirect_gas_window[128];
	u32 indirect_gas_window_off;
	u32 reserved[127];
	u32 pff_event_summary;
	u32 reserved2[3];
	u32 aer_in_p2p_hdr;
	u32 aer_in_p2p_data[5];
	u32 aer_in_vep_hdr;
	u32 aer_in_vep_data[5];
	u32 dpc_hdr;
	u32 dpc_data[5];
	u32 cts_hdr;
	u32 cts_data[5];
	u32 reserved3[6];
	u32 hotplug_hdr;
	u32 hotplug_data[5];
	u32 ier_hdr;
	u32 ier_data[5];
	u32 threshold_hdr;
	u32 threshold_data[5];
	u32 power_mgmt_hdr;
	u32 power_mgmt_data[5];
	u32 tlp_throttling_hdr;
	u32 tlp_throttling_data[5];
	u32 force_speed_hdr;
	u32 force_speed_data[5];
	u32 credit_timeout_hdr;
	u32 credit_timeout_data[5];
	u32 link_state_hdr;
	u32 link_state_data[5];
	u32 reserved4[174];
} __packed;

struct switchtec_ntb;

struct switchtec_dev {
	struct pci_dev *pdev;
	struct device dev;
	struct cdev cdev;

	int partition;
	int partition_count;
	int pff_csr_count;
	char pff_local[SWITCHTEC_MAX_PFF_CSR];

	void __iomem *mmio;
	struct mrpc_regs __iomem *mmio_mrpc;
	struct sw_event_regs __iomem *mmio_sw_event;
	struct sys_info_regs __iomem *mmio_sys_info;
	struct flash_info_regs __iomem *mmio_flash_info;
	struct ntb_info_regs __iomem *mmio_ntb;
	struct part_cfg_regs __iomem *mmio_part_cfg;
	struct part_cfg_regs __iomem *mmio_part_cfg_all;
	struct pff_csr_regs __iomem *mmio_pff_csr;

	/*
	 * The mrpc mutex must be held when accessing the other
	 * mrpc_ fields, alive flag and stuser->state field
	 */
	struct mutex mrpc_mutex;
	struct list_head mrpc_queue;
	int mrpc_busy;
	struct work_struct mrpc_work;
	struct delayed_work mrpc_timeout;
	bool alive;

	wait_queue_head_t event_wq;
	atomic_t event_cnt;

	struct work_struct link_event_work;
	void (*link_notifier)(struct switchtec_dev *stdev);
	u8 link_event_count[SWITCHTEC_MAX_PFF_CSR];

	struct switchtec_ntb *sndev;
};

static inline struct switchtec_dev *to_stdev(struct device *dev)
{
	return container_of(dev, struct switchtec_dev, dev);
}

extern struct class *switchtec_class;

#endif
