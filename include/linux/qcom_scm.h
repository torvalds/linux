/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2010-2015, 2018-2019, 2021 The Linux Foundation. All rights reserved.
 * Copyright (C) 2015 Linaro Ltd.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __QCOM_SCM_H
#define __QCOM_SCM_H

#include <linux/err.h>
#include <linux/types.h>
#include <linux/cpumask.h>

#define QCOM_SCM_VERSION(major, minor)	(((major) << 16) | ((minor) & 0xFF))
#define QCOM_SCM_CPU_PWR_DOWN_L2_ON	0x0
#define QCOM_SCM_CPU_PWR_DOWN_L2_OFF	0x1
#define QCOM_SCM_HDCP_MAX_REQ_CNT	5
#define QCOM_SCM_CAMERA_MAX_QOS_CNT	2

struct qcom_scm_camera_qos {
	u32 offset;
	u32 val;
};

enum qcom_download_mode {
	QCOM_DOWNLOAD_NODUMP    = 0x00,
	QCOM_DOWNLOAD_EDL       = 0x01,
	QCOM_DOWNLOAD_FULLDUMP  = 0x10,
	QCOM_DOWNLOAD_MINIDUMP  = 0x20,
};

struct qcom_scm_hdcp_req {
	u32 addr;
	u32 val;
};

struct qcom_scm_vmperm {
	int vmid;
	int perm;
};

enum qcom_scm_ocmem_client {
	QCOM_SCM_OCMEM_UNUSED_ID = 0x0,
	QCOM_SCM_OCMEM_GRAPHICS_ID,
	QCOM_SCM_OCMEM_VIDEO_ID,
	QCOM_SCM_OCMEM_LP_AUDIO_ID,
	QCOM_SCM_OCMEM_SENSORS_ID,
	QCOM_SCM_OCMEM_OTHER_OS_ID,
	QCOM_SCM_OCMEM_DEBUG_ID,
};

enum qcom_scm_sec_dev_id {
	QCOM_SCM_MDSS_DEV_ID    = 1,
	QCOM_SCM_OCMEM_DEV_ID   = 5,
	QCOM_SCM_PCIE0_DEV_ID   = 11,
	QCOM_SCM_PCIE1_DEV_ID   = 12,
	QCOM_SCM_GFX_DEV_ID     = 18,
	QCOM_SCM_UFS_DEV_ID     = 19,
	QCOM_SCM_ICE_DEV_ID     = 20,
};

struct qcom_scm_current_perm_info {
	__le32 vmid;
	__le32 perm;
	__le64 ctx;
	__le32 ctx_size;
	__le32 unused;
};

struct qcom_scm_mem_map_info {
	__le64 mem_addr;
	__le64 mem_size;
};

/**
 * struct arm_smccc_args
 * @args: The array of values used in registers in smc instruction
 */
struct arm_smccc_args {
	unsigned long args[8];
};

enum qcom_scm_ice_cipher {
	QCOM_SCM_ICE_CIPHER_AES_128_XTS = 0,
	QCOM_SCM_ICE_CIPHER_AES_128_CBC = 1,
	QCOM_SCM_ICE_CIPHER_AES_256_XTS = 3,
	QCOM_SCM_ICE_CIPHER_AES_256_CBC = 4,
};

#define QCOM_SCM_PERM_READ       0x4
#define QCOM_SCM_PERM_WRITE      0x2
#define QCOM_SCM_PERM_EXEC       0x1

#define QCOM_SCM_VMID_TZ			0x1
#define QCOM_SCM_VMID_HLOS			0x3
#define QCOM_SCM_VMID_CP_TOUCH			0x8
#define QCOM_SCM_VMID_CP_BITSTREAM		0x9
#define QCOM_SCM_VMID_CP_PIXEL			0xA
#define QCOM_SCM_VMID_CP_NON_PIXEL		0xB
#define QCOM_SCM_VMID_CP_CAMERA			0xD
#define QCOM_SCM_VMID_HLOS_FREE			0xE
#define QCOM_SCM_VMID_MSS_MSA			0xF
#define QCOM_SCM_VMID_MSS_NONMSA		0x10
#define QCOM_SCM_VMID_CP_SEC_DISPLAY		0x11
#define QCOM_SCM_VMID_CP_APP			0x12
#define QCOM_SCM_VMID_LPASS			0x16
#define QCOM_SCM_VMID_WLAN			0x18
#define QCOM_SCM_VMID_WLAN_CE			0x19
#define QCOM_SCM_VMID_CP_SPSS_SP		0x1A
#define QCOM_SCM_VMID_CP_CAMERA_PREVIEW		0x1D
#define QCOM_SCM_VMID_CDSP			0x1E
#define QCOM_SCM_VMID_CP_SPSS_SP_SHARED		0x22
#define QCOM_SCM_VMID_CP_SPSS_HLOS_SHARED	0x24
#define QCOM_SCM_VMID_ADSP_HEAP			0x25
#define QCOM_SCM_VMID_CP_CDSP			0x2A
#define QCOM_SCM_VMID_NAV			0x2B
#define QCOM_SCM_VMID_TVM			0x2D
#define QCOM_SCM_VMID_OEMVM			0x31

#define QCOM_SCM_PERM_RW (QCOM_SCM_PERM_READ | QCOM_SCM_PERM_WRITE)
#define QCOM_SCM_PERM_RWX (QCOM_SCM_PERM_RW | QCOM_SCM_PERM_EXEC)

static inline void qcom_scm_populate_vmperm_info(
		struct qcom_scm_current_perm_info *destvm, int vmid, int perm)
{
	if (!destvm)
		return;

	destvm->vmid = cpu_to_le32(vmid);
	destvm->perm = cpu_to_le32(perm);
	destvm->ctx = 0;
	destvm->ctx_size = 0;
}

static inline void qcom_scm_populate_mem_map_info(
		struct qcom_scm_mem_map_info *mem_to_map,
		phys_addr_t mem_addr, size_t mem_size)
{
	if (!mem_to_map)
		return;

	mem_to_map->mem_addr = cpu_to_le64(mem_addr);
	mem_to_map->mem_size = cpu_to_le64(mem_size);
}

extern bool qcom_scm_is_available(void);
extern void *qcom_get_scm_device(void);

extern int qcom_scm_set_warm_boot_addr_mc(void *entry, u32 aff0, u32 aff1,
					  u32 aff2, u32 flags);
extern int qcom_scm_set_cold_boot_addr(void *entry, const cpumask_t *cpus);
extern int qcom_scm_set_warm_boot_addr(void *entry, const cpumask_t *cpus);
extern void qcom_scm_cpu_power_down(u32 flags);
extern int qcom_scm_sec_wdog_deactivate(void);
extern int qcom_scm_sec_wdog_trigger(void);
extern void qcom_scm_disable_sdi(void);
extern int qcom_scm_set_remote_state(u32 state, u32 id);
extern int qcom_scm_spin_cpu(void);
extern void qcom_scm_set_download_mode(enum qcom_download_mode mode, phys_addr_t tcsr_boot_misc);
extern int qcom_scm_get_download_mode(unsigned int *mode, phys_addr_t tcsr_boot_misc);
extern int qcom_scm_config_cpu_errata(void);
extern void qcom_scm_phy_update_scm_level_shifter(u32 val);

extern int qcom_scm_pas_init_image(u32 peripheral, dma_addr_t metadata);
extern int qcom_scm_pas_mem_setup(u32 peripheral, phys_addr_t addr,
				  phys_addr_t size);
extern int qcom_scm_pas_auth_and_reset(u32 peripheral);
extern int qcom_scm_pas_shutdown(u32 peripheral);
extern int qcom_scm_pas_shutdown_retry(u32 peripheral);
extern bool qcom_scm_pas_supported(u32 peripheral);

extern int qcom_scm_get_sec_dump_state(u32 *dump_state);
extern int qcom_scm_get_llcc_missrate(phys_addr_t in_buf, size_t in_buf_size,
				phys_addr_t out_buf, size_t out_buf_size);
extern int qcom_scm_assign_dump_table_region(bool is_assign, phys_addr_t  addr, size_t size);

extern int qcom_scm_tz_blsp_modify_owner(int food, u64 subsystem, int *out);

extern int qcom_scm_io_readl(phys_addr_t addr, unsigned int *val);
extern int qcom_scm_io_writel(phys_addr_t addr, unsigned int val);
extern int qcom_scm_io_reset(void);

extern bool qcom_scm_is_secure_wdog_trigger_available(void);
extern bool qcom_scm_is_mode_switch_available(void);
extern int qcom_scm_get_jtag_etm_feat_id(u64 *version);

extern void qcom_scm_halt_spmi_pmic_arbiter(void);
extern void qcom_scm_deassert_ps_hold(void);
extern void qcom_scm_mmu_sync(bool sync);

extern bool qcom_scm_restore_sec_cfg_available(void);
extern int qcom_scm_restore_sec_cfg(u32 device_id, u32 spare);
extern int qcom_scm_iommu_secure_ptbl_size(u32 spare, size_t *size);
extern int qcom_scm_iommu_secure_ptbl_init(u64 addr, u32 size, u32 spare);
extern int qcom_scm_mem_protect_video_var(u32 cp_start, u32 cp_size,
					  u32 cp_nonpixel_start,
					  u32 cp_nonpixel_size);
extern int qcom_scm_mem_protect_region_id(phys_addr_t paddr, size_t size);
extern int qcom_scm_mem_protect_lock_id2_flat(phys_addr_t list_addr,
				size_t list_size, size_t chunk_size,
				size_t memory_usage, int lock);
extern int qcom_scm_iommu_secure_map(phys_addr_t sg_list_addr, size_t num_sg,
				size_t sg_block_size, u64 sec_id, int cbndx,
				unsigned long iova, size_t total_len);
extern int qcom_scm_iommu_secure_unmap(u64 sec_id, int cbndx,
				unsigned long iova, size_t total_len);

extern int qcom_scm_paravirt_smmu_attach(u64 sid, u64 asid, u64 ste_pa,
				u64 ste_size, u64 cd_pa, u64 cd_size);
extern int qcom_scm_paravirt_tlb_inv(u64 asid, u64 sid);
extern int qcom_scm_paravirt_smmu_detach(u64 sid);
extern int
qcom_scm_assign_mem_regions(struct qcom_scm_mem_map_info *mem_regions,
			    size_t mem_regions_sz, u32 *srcvms, size_t src_sz,
			    struct qcom_scm_current_perm_info *newvms,
			    size_t newvms_sz);
extern int qcom_scm_assign_mem(phys_addr_t mem_addr, size_t mem_sz,
			       u64 *src,
			       const struct qcom_scm_vmperm *newvm,
			       unsigned int dest_cnt);
extern int qcom_scm_mem_protect_sd_ctrl(u32 devid, phys_addr_t mem_addr,
					u64 mem_size, u32 vmid);
extern int qcom_scm_get_feat_version_cp(u64 *version);
extern bool qcom_scm_kgsl_set_smmu_aperture_available(void);
extern int qcom_scm_kgsl_set_smmu_aperture(
				unsigned int num_context_bank);
extern int qcom_scm_kgsl_set_smmu_lpac_aperture(
				unsigned int num_context_bank);
extern int qcom_scm_kgsl_init_regs(u32 gpu_req);
extern int qcom_scm_enable_shm_bridge(void);
extern int qcom_scm_delete_shm_bridge(u64 handle);
extern int qcom_scm_create_shm_bridge(u64 pfn_and_ns_perm_flags,
			u64 ipfn_and_s_perm_flags, u64 size_and_flags,
			u64 ns_vmids, u64 *handle);
extern int qcom_scm_smmu_prepare_atos_id(u64 dev_id, int cb_num, int operation);
extern int qcom_mdf_assign_memory_to_subsys(u64 start_addr,
				u64 end_addr, phys_addr_t paddr, u64 size);

extern bool qcom_scm_dcvs_core_available(void);
extern bool qcom_scm_dcvs_ca_available(void);
extern int qcom_scm_dcvs_reset(void);
extern int qcom_scm_dcvs_init_v2(phys_addr_t addr, size_t size, int *version);
extern int qcom_scm_dcvs_init_ca_v2(phys_addr_t addr, size_t size);
extern int qcom_scm_dcvs_update(int level, s64 total_time, s64 busy_time);
extern int qcom_scm_dcvs_update_v2(int level, s64 total_time, s64 busy_time);
extern int qcom_scm_dcvs_update_ca_v2(int level, s64 total_time, s64 busy_time,
				      int context_count);

extern bool qcom_scm_ocmem_lock_available(void);
extern int qcom_scm_ocmem_lock(enum qcom_scm_ocmem_client id, u32 offset,
			       u32 size, u32 mode);
extern int qcom_scm_ocmem_unlock(enum qcom_scm_ocmem_client id, u32 offset,
				 u32 size);

extern int qcom_scm_config_set_ice_key(uint32_t index, phys_addr_t paddr,
				       size_t size, uint32_t cipher,
				       unsigned int data_unit,
				       unsigned int ce);
extern int qcom_scm_clear_ice_key(uint32_t index, unsigned int ce);
extern int qcom_scm_derive_raw_secret(phys_addr_t paddr_key, size_t size_key,
		phys_addr_t paddr_secret, size_t size_secret);
extern bool qcom_scm_ice_available(void);
extern int qcom_scm_ice_invalidate_key(u32 index);
extern int qcom_scm_ice_set_key(u32 index, const u8 *key, u32 key_size,
				enum qcom_scm_ice_cipher cipher,
				u32 data_unit_size);

extern bool qcom_scm_hdcp_available(void);
extern int qcom_scm_hdcp_req(struct qcom_scm_hdcp_req *req, u32 req_cnt,
			     u32 *resp);

extern bool qcom_scm_is_lmh_debug_set_available(void);
extern bool qcom_scm_is_lmh_debug_read_buf_size_available(void);
extern bool qcom_scm_is_lmh_debug_read_buf_available(void);
extern bool qcom_scm_is_lmh_debug_get_type_available(void);
extern int qcom_scm_lmh_read_buf_size(int *size);
extern int qcom_scm_lmh_limit_dcvsh(phys_addr_t payload, uint32_t payload_size,
			u64 limit_node, uint32_t node_id, u64 version);
extern int qcom_scm_lmh_debug_read(phys_addr_t payload, uint32_t size);
extern int qcom_scm_lmh_debug_set_config_write(phys_addr_t payload,
			int payload_size, uint32_t *buf, int buf_size);
extern int qcom_scm_lmh_get_type(phys_addr_t payload, u64 payload_size,
			u64 debug_type, uint32_t get_from, uint32_t *size);
extern int qcom_scm_lmh_fetch_data(u32 node_id, u32 debug_type, uint32_t *peak,
		uint32_t *avg);

extern int qcom_scm_smmu_change_pgtbl_format(u64 dev_id, int cbndx);
extern int qcom_scm_qsmmu500_wait_safe_toggle(bool en);
extern int qcom_scm_smmu_notify_secure_lut(u64 dev_id, bool secure);

extern int qcom_scm_qdss_invoke(phys_addr_t addr, size_t size, u64 *out);

extern int qcom_scm_camera_update_camnoc_qos(uint32_t use_case_id,
		uint32_t qos_cnt, struct qcom_scm_camera_qos *scm_buf);
extern int qcom_scm_camera_protect_all(uint32_t protect, uint32_t param);
extern int qcom_scm_camera_protect_phy_lanes(bool protect, u64 regmask);

extern int qcom_scm_tsens_reinit(int *tsens_ret);


extern int qcom_scm_get_tz_log_feat_id(u64 *version);
extern int qcom_scm_get_tz_feat_id_version(u64 feat_id, u64 *version);
extern int qcom_scm_register_qsee_log_buf(phys_addr_t buf, size_t len);
extern int qcom_scm_query_encrypted_log_feature(u64 *enabled);
extern int qcom_scm_request_encrypted_log(phys_addr_t buf, size_t len,
		uint32_t log_id, bool is_full_encrypted_tz_logs_supported,
		bool is_full_encrypted_tz_logs_enabled);

extern int qcom_scm_ice_restore_cfg(void);

extern int qcom_scm_invoke_smc(phys_addr_t in_buf, size_t in_buf_size,
		phys_addr_t out_buf, size_t out_buf_size, int32_t *result,
		u64 *response_type, unsigned int *data);
extern int qcom_scm_invoke_smc_legacy(phys_addr_t in_buf, size_t in_buf_size,
		phys_addr_t out_buf, size_t out_buf_size, int32_t *result,
		u64 *response_type, unsigned int *data);
extern int qcom_scm_invoke_callback_response(phys_addr_t out_buf,
		size_t out_buf_size, int32_t *result, u64 *response_type,
		unsigned int *data);

extern int qcom_scm_lmh_dcvsh(u32 payload_fn, u32 payload_reg, u32 payload_val,
			      u64 limit_node, u32 node_id, u64 version);
extern int qcom_scm_lmh_profile_change(u32 profile_id);
extern bool qcom_scm_lmh_dcvsh_available(void);

extern int qcom_scm_prefetch_tgt_ctrl(bool en);
#endif
