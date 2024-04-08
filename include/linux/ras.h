/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __RAS_H__
#define __RAS_H__

#include <asm/errno.h>
#include <linux/uuid.h>
#include <linux/cper.h>

#ifdef CONFIG_DEBUG_FS
int ras_userspace_consumers(void);
void ras_debugfs_init(void);
int ras_add_daemon_trace(void);
#else
static inline int ras_userspace_consumers(void) { return 0; }
static inline void ras_debugfs_init(void) { }
static inline int ras_add_daemon_trace(void) { return 0; }
#endif

#ifdef CONFIG_RAS_CEC
int __init parse_cec_param(char *str);
#endif

#ifdef CONFIG_RAS
void log_non_standard_event(const guid_t *sec_type,
			    const guid_t *fru_id, const char *fru_text,
			    const u8 sev, const u8 *err, const u32 len);
void log_arm_hw_error(struct cper_sec_proc_arm *err);

#else
static inline void
log_non_standard_event(const guid_t *sec_type,
		       const guid_t *fru_id, const char *fru_text,
		       const u8 sev, const u8 *err, const u32 len)
{ return; }
static inline void
log_arm_hw_error(struct cper_sec_proc_arm *err) { return; }
#endif

struct atl_err {
	u64 addr;
	u64 ipid;
	u32 cpu;
};

#if IS_ENABLED(CONFIG_AMD_ATL)
void amd_atl_register_decoder(unsigned long (*f)(struct atl_err *));
void amd_atl_unregister_decoder(void);
void amd_retire_dram_row(struct atl_err *err);
unsigned long amd_convert_umc_mca_addr_to_sys_addr(struct atl_err *err);
#else
static inline void amd_retire_dram_row(struct atl_err *err) { }
static inline unsigned long
amd_convert_umc_mca_addr_to_sys_addr(struct atl_err *err) { return -EINVAL; }
#endif /* CONFIG_AMD_ATL */

#endif /* __RAS_H__ */
