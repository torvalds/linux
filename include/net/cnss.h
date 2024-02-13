/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2013-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _NET_CNSS_H_
#define _NET_CNSS_H_

#include <linux/device.h>
#include <linux/skbuff.h>
#include <linux/pci.h>
#include <linux/mmc/sdio_func.h>
#include <linux/interrupt.h>

#ifdef CONFIG_CNSS
#define MAX_FIRMWARE_SIZE (1 * 1024 * 1024)
#define CNSS_MAX_FILE_NAME	20
#define PINCTRL_SLEEP  0
#define PINCTRL_ACTIVE 1

enum cnss_bus_width_type {
	CNSS_BUS_WIDTH_NONE,
	CNSS_BUS_WIDTH_LOW,
	CNSS_BUS_WIDTH_MEDIUM,
	CNSS_BUS_WIDTH_HIGH
};

enum cnss_cc_src {
	CNSS_SOURCE_CORE,
	CNSS_SOURCE_11D,
	CNSS_SOURCE_USER
};

/* FW image files */
struct cnss_fw_files {
	char image_file[CNSS_MAX_FILE_NAME];
	char board_data[CNSS_MAX_FILE_NAME];
	char otp_data[CNSS_MAX_FILE_NAME];
	char utf_file[CNSS_MAX_FILE_NAME];
	char utf_board_data[CNSS_MAX_FILE_NAME];
	char epping_file[CNSS_MAX_FILE_NAME];
	char evicted_data[CNSS_MAX_FILE_NAME];
};

struct cnss_wlan_runtime_ops {
	int (*runtime_suspend)(struct pci_dev *pdev);
	int (*runtime_resume)(struct pci_dev *pdev);
};

struct cnss_wlan_driver {
	char *name;
	int  (*probe)(struct pci_dev *pdev, const struct pci_device_id *id);
	void (*remove)(struct pci_dev *pdev);
	int  (*reinit)(struct pci_dev *pdev, const struct pci_device_id *id);
	void (*shutdown)(struct pci_dev *pdev);
	void (*crash_shutdown)(struct pci_dev *pdev);
	int  (*suspend)(struct pci_dev *pdev, pm_message_t state);
	int  (*resume)(struct pci_dev *pdev);
	void (*modem_status)(struct pci_dev *pdev, int state);
	void (*update_status)(struct pci_dev *pdev, uint32_t status);
	struct cnss_wlan_runtime_ops *runtime_ops;
	const struct pci_device_id *id_table;
};

/*
 * codeseg_total_bytes: Total bytes across all the codesegment blocks
 * num_codesegs: No of Pages used
 * codeseg_size: Size of each segment. Should be power of 2 and multiple of 4K
 * codeseg_size_log2: log2(codeseg_size)
 * codeseg_busaddr: Physical address of the DMAble memory;4K aligned
 */

#define CODESWAP_MAX_CODESEGS 16
struct codeswap_codeseg_info {
	u32   codeseg_total_bytes;
	u32   num_codesegs;
	u32   codeseg_size;
	u32   codeseg_size_log2;
	void *codeseg_busaddr[CODESWAP_MAX_CODESEGS];
};

struct image_desc_info {
	dma_addr_t fw_addr;
	u32 fw_size;
	dma_addr_t bdata_addr;
	u32 bdata_size;
};

/* platform capabilities */
enum cnss_platform_cap_flag {
	CNSS_HAS_EXTERNAL_SWREG = 0x01,
	CNSS_HAS_UART_ACCESS = 0x02,
};

struct cnss_platform_cap {
	u32 cap_flag;
};

/* WLAN driver status, keep it aligned with cnss2 */
enum cnss_driver_status {
	CNSS_UNINITIALIZED,
	CNSS_INITIALIZED,
	CNSS_LOAD_UNLOAD,
	CNSS_RECOVERY,
	CNSS_FW_DOWN,
	CNSS_SSR_FAIL,
};

enum cnss_runtime_request {
	CNSS_PM_RUNTIME_GET,
	CNSS_PM_RUNTIME_PUT,
	CNSS_PM_RUNTIME_MARK_LAST_BUSY,
	CNSS_PM_RUNTIME_RESUME,
	CNSS_PM_RUNTIME_PUT_NOIDLE,
	CNSS_PM_REQUEST_RESUME,
	CNSS_PM_RUNTIME_PUT_AUTO,
	CNSS_PM_GET_NORESUME,
};

struct dma_iommu_mapping *cnss_smmu_get_mapping(void);
int cnss_smmu_map(phys_addr_t paddr, uint32_t *iova_addr, size_t size);
int cnss_get_fw_image(struct image_desc_info *image_desc_info);
void cnss_runtime_init(struct device *dev, int auto_delay);
void cnss_runtime_exit(struct device *dev);
void cnss_wlan_pci_link_down(void);
int cnss_pcie_shadow_control(struct pci_dev *dev, bool enable);
int cnss_wlan_register_driver(struct cnss_wlan_driver *driver);
void cnss_wlan_unregister_driver(struct cnss_wlan_driver *driver);
int cnss_get_fw_files(struct cnss_fw_files *pfw_files);
int cnss_get_fw_files_for_target(struct cnss_fw_files *pfw_files,
				 u32 target_type, u32 target_version);
void cnss_get_qca9377_fw_files(struct cnss_fw_files *pfw_files,
			       u32 size, u32 tufello_dual_fw);

int cnss_request_bus_bandwidth(int bandwidth);

#ifdef CONFIG_CNSS_SECURE_FW
int cnss_get_sha_hash(const u8 *data, u32 data_len,
		      u8 *hash_idx, u8 *out);
void *cnss_get_fw_ptr(void);
#endif

int cnss_get_codeswap_struct(struct codeswap_codeseg_info *swap_seg);
int cnss_get_bmi_setup(void);

#ifdef CONFIG_PCI_MSM
int cnss_wlan_pm_control(bool vote);
#endif
void cnss_lock_pm_sem(void);
void cnss_release_pm_sem(void);

void cnss_request_pm_qos_type(int latency_type, u32 qos_val);
void cnss_request_pm_qos(u32 qos_val);
void cnss_remove_pm_qos(void);

void cnss_pci_request_pm_qos_type(int latency_type, u32 qos_val);
void cnss_pci_request_pm_qos(u32 qos_val);
void cnss_pci_remove_pm_qos(void);

void cnss_sdio_request_pm_qos_type(int latency_type, u32 qos_val);
void cnss_sdio_request_pm_qos(u32 qos_val);
void cnss_sdio_remove_pm_qos(void);

int cnss_get_platform_cap(struct cnss_platform_cap *cap);
void cnss_set_driver_status(enum cnss_driver_status driver_status);

#ifndef CONFIG_WCNSS_MEM_PRE_ALLOC
static inline int wcnss_pre_alloc_reset(void) { return 0; }
#endif

int msm_pcie_enumerate(u32 rc_idx);
int cnss_auto_suspend(void);
int cnss_auto_resume(void);
int cnss_prevent_auto_suspend(const char *caller_func);
int cnss_allow_auto_suspend(const char *caller_func);
int cnss_is_auto_suspend_allowed(const char *caller_func);

int cnss_pm_runtime_request(struct device *dev, enum
			    cnss_runtime_request request);
void cnss_set_cc_source(enum cnss_cc_src cc_source);
enum cnss_cc_src cnss_get_cc_source(void);
#endif

void cnss_pm_wake_lock_init(struct wakeup_source **ws, const char *name);
void cnss_pm_wake_lock(struct wakeup_source *ws);

void cnss_device_crashed(void);
void cnss_device_self_recovery(void);
void *cnss_get_virt_ramdump_mem(unsigned long *size);

void cnss_schedule_recovery_work(void);
int cnss_pcie_set_wlan_mac_address(const u8 *in, uint32_t len);
u8 *cnss_get_wlan_mac_address(struct device *dev, uint32_t *num);
int cnss_sdio_set_wlan_mac_address(const u8 *in, uint32_t len);

enum {
	CNSS_RESET_SOC = 0,
	CNSS_RESET_SUBSYS_COUPLED,
	CNSS_RESET_LEVEL_MAX
};

int cnss_get_restart_level(void);

struct cnss_sdio_wlan_driver {
	const char *name;
	const struct sdio_device_id *id_table;
	int (*probe)(struct sdio_func *func, const struct sdio_device_id *id);
	void (*remove)(struct sdio_func *func);
	int (*reinit)(struct sdio_func *func, const struct sdio_device_id *id);
	void (*shutdown)(struct sdio_func *func);
	void (*crash_shutdown)(struct sdio_func *func);
	int (*suspend)(struct device *dev);
	int (*resume)(struct device *dev);
};

int cnss_sdio_wlan_register_driver(struct cnss_sdio_wlan_driver *driver);
void cnss_sdio_wlan_unregister_driver(struct cnss_sdio_wlan_driver *driver);

typedef void (*oob_irq_handler_t)(void *dev_para);
int cnss_wlan_query_oob_status(void);
int cnss_wlan_register_oob_irq_handler(oob_irq_handler_t handler, void *pm_oob);
int cnss_wlan_unregister_oob_irq_handler(void *pm_oob);

void cnss_dump_stack(struct task_struct *task);
u8 *cnss_common_get_wlan_mac_address(struct device *dev, uint32_t *num);
void cnss_init_work(struct work_struct *work, work_func_t func);
void cnss_flush_delayed_work(void *dwork);
void cnss_flush_work(void *work);
void cnss_pm_wake_lock_timeout(struct wakeup_source *ws, ulong msec);
void cnss_pm_wake_lock_release(struct wakeup_source *ws);
void cnss_pm_wake_lock_destroy(struct wakeup_source *ws);
void cnss_get_monotonic_boottime(struct timespec64 *ts);
void cnss_get_boottime(struct timespec64 *ts);
void cnss_init_delayed_work(struct delayed_work *work, work_func_t func);
int cnss_vendor_cmd_reply(struct sk_buff *skb);
int cnss_set_cpus_allowed_ptr(struct task_struct *task, ulong cpu);
int cnss_set_wlan_unsafe_channel(u16 *unsafe_ch_list, u16 ch_count);
int cnss_get_wlan_unsafe_channel(u16 *unsafe_ch_list, u16 *ch_count, u16 buf_len);
int cnss_wlan_set_dfs_nol(const void *info, u16 info_len);
int cnss_wlan_get_dfs_nol(void *info, u16 info_len);
int cnss_common_request_bus_bandwidth(struct device *dev, int bandwidth);
void cnss_common_device_crashed(struct device *dev);
void cnss_common_device_self_recovery(struct device *dev);
void *cnss_common_get_virt_ramdump_mem(struct device *dev, unsigned long *size);
void cnss_common_schedule_recovery_work(struct device *dev);
int cnss_common_set_wlan_mac_address(struct device *dev, const u8 *in, uint32_t len);
u8 *cnss_common_get_wlan_mac_address(struct device *dev, uint32_t *num);
int cnss_power_up(struct device *dev);
int cnss_power_down(struct device *dev);
int cnss_sdio_configure_spdt(bool state);

int cnss_common_register_tsf_captured_handler(struct device *dev, irq_handler_t handler,
					      void *ctx);
int cnss_common_unregister_tsf_captured_handler(struct device *dev, void *ctx);
#endif /* _NET_CNSS_H_ */
