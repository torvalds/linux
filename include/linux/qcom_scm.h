/* Copyright (c) 2010-2015, 2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2015 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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

struct qcom_scm_hdcp_req {
	u32 addr;
	u32 val;
};

struct qcom_scm_vmperm {
	int vmid;
	int perm;
};

#define QCOM_SCM_VMID_HLOS       0x3
#define QCOM_SCM_VMID_MSS_MSA    0xF
#define QCOM_SCM_VMID_WLAN       0x18
#define QCOM_SCM_VMID_WLAN_CE    0x19
#define QCOM_SCM_PERM_READ       0x4
#define QCOM_SCM_PERM_WRITE      0x2
#define QCOM_SCM_PERM_EXEC       0x1
#define QCOM_SCM_PERM_RW (QCOM_SCM_PERM_READ | QCOM_SCM_PERM_WRITE)
#define QCOM_SCM_PERM_RWX (QCOM_SCM_PERM_RW | QCOM_SCM_PERM_EXEC)

#if IS_ENABLED(CONFIG_QCOM_SCM)
extern int qcom_scm_set_cold_boot_addr(void *entry, const cpumask_t *cpus);
extern int qcom_scm_set_warm_boot_addr(void *entry, const cpumask_t *cpus);
extern bool qcom_scm_is_available(void);
extern bool qcom_scm_hdcp_available(void);
extern int qcom_scm_hdcp_req(struct qcom_scm_hdcp_req *req, u32 req_cnt,
			     u32 *resp);
extern bool qcom_scm_pas_supported(u32 peripheral);
extern int qcom_scm_pas_init_image(u32 peripheral, const void *metadata,
				   size_t size);
extern int qcom_scm_pas_mem_setup(u32 peripheral, phys_addr_t addr,
				  phys_addr_t size);
extern int qcom_scm_pas_auth_and_reset(u32 peripheral);
extern int qcom_scm_pas_shutdown(u32 peripheral);
extern int qcom_scm_assign_mem(phys_addr_t mem_addr, size_t mem_sz,
			       unsigned int *src, struct qcom_scm_vmperm *newvm,
			       int dest_cnt);
extern void qcom_scm_cpu_power_down(u32 flags);
extern u32 qcom_scm_get_version(void);
extern int qcom_scm_set_remote_state(u32 state, u32 id);
extern int qcom_scm_restore_sec_cfg(u32 device_id, u32 spare);
extern int qcom_scm_iommu_secure_ptbl_size(u32 spare, size_t *size);
extern int qcom_scm_iommu_secure_ptbl_init(u64 addr, u32 size, u32 spare);
extern int qcom_scm_io_readl(phys_addr_t addr, unsigned int *val);
extern int qcom_scm_io_writel(phys_addr_t addr, unsigned int val);
#else

#include <linux/errno.h>

static inline
int qcom_scm_set_cold_boot_addr(void *entry, const cpumask_t *cpus)
{
	return -ENODEV;
}
static inline
int qcom_scm_set_warm_boot_addr(void *entry, const cpumask_t *cpus)
{
	return -ENODEV;
}
static inline bool qcom_scm_is_available(void) { return false; }
static inline bool qcom_scm_hdcp_available(void) { return false; }
static inline int qcom_scm_hdcp_req(struct qcom_scm_hdcp_req *req, u32 req_cnt,
				    u32 *resp) { return -ENODEV; }
static inline bool qcom_scm_pas_supported(u32 peripheral) { return false; }
static inline int qcom_scm_pas_init_image(u32 peripheral, const void *metadata,
					  size_t size) { return -ENODEV; }
static inline int qcom_scm_pas_mem_setup(u32 peripheral, phys_addr_t addr,
					 phys_addr_t size) { return -ENODEV; }
static inline int
qcom_scm_pas_auth_and_reset(u32 peripheral) { return -ENODEV; }
static inline int qcom_scm_pas_shutdown(u32 peripheral) { return -ENODEV; }
static inline int qcom_scm_assign_mem(phys_addr_t mem_addr, size_t mem_sz,
				      unsigned int *src,
				      struct qcom_scm_vmperm *newvm,
				      int dest_cnt) { return -ENODEV; }
static inline void qcom_scm_cpu_power_down(u32 flags) {}
static inline u32 qcom_scm_get_version(void) { return 0; }
static inline u32
qcom_scm_set_remote_state(u32 state,u32 id) { return -ENODEV; }
static inline int qcom_scm_restore_sec_cfg(u32 device_id, u32 spare) { return -ENODEV; }
static inline int qcom_scm_iommu_secure_ptbl_size(u32 spare, size_t *size) { return -ENODEV; }
static inline int qcom_scm_iommu_secure_ptbl_init(u64 addr, u32 size, u32 spare) { return -ENODEV; }
static inline int qcom_scm_io_readl(phys_addr_t addr, unsigned int *val) { return -ENODEV; }
static inline int qcom_scm_io_writel(phys_addr_t addr, unsigned int val) { return -ENODEV; }
#endif
#endif
