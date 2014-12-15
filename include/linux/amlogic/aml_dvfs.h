
#ifndef __AML_DVFS_H__
#define __AML_DVFS_H__
#include <linux/cpufreq.h>

/*
 * right now only support the flowing types of voltage change
 */
#define AML_DVFS_ID_VCCK            (1 << 0)        // voltage for CPU core
#define AML_DVFS_ID_VDDEE           (1 << 1)        // voltage for VDDEE
#define AML_DVFS_ID_DDR             (1 << 2)        // voltage for DDR

#define AML_DVFS_FREQ_PRECHANGE     0
#define AML_DVFS_FREQ_POSTCHANGE    1

struct aml_dvfs {
    unsigned int freq;                      // frequent of clock source in KHz
    unsigned int min_uV;                    // min target voltage of this frequent
    unsigned int max_uV;                    // max target voltage of this frequent
};

struct aml_dvfs_driver {
    char            *name;
    unsigned int    id_mask;                   // which types of voltage support

    int (*set_voltage)(uint32_t id, uint32_t min_uV, uint32_t max_uV);
    int (*get_voltage)(uint32_t id, uint32_t *uV);
};

#ifdef CONFIG_AML_DVFS
extern int aml_dvfs_register_driver(struct aml_dvfs_driver *driver);
extern int aml_dvfs_unregister_driver(struct aml_dvfs_driver *driver);
extern int aml_dvfs_freq_change(uint32_t id, uint32_t new_freq, uint32_t old_freq, uint32_t flags);
extern struct cpufreq_frequency_table *aml_dvfs_get_freq_table(unsigned int id);
#else
inline int aml_dvfs_register_driver(struct aml_dvfs_driver *driver)
{
    return 0;
}

inline int aml_dvfs_unregister_driver(struct aml_dvfs_driver *driver)
{
    return 0;
}

inline int aml_dvfs_freq_change(uint32_t id, uint32_t new_freq, uint32_t old_freq, uint32_t flags)
{
    return 0;    
}

inline struct cpufreq_frequency_table *aml_dvfs_get_freq_table(unsigned int id) 
{
    return NULL;    
}
#endif

#endif  /* __AML_DVFS_H__ */
