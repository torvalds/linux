#ifndef __OF_RESERVED_MEM_H
#define __OF_RESERVED_MEM_H

#ifdef CONFIG_OF_RESERVED_MEM
void of_reserved_mem_device_init(struct device *dev);
void of_reserved_mem_device_release(struct device *dev);
void early_init_dt_scan_reserved_mem(void);
#else
static inline void of_reserved_mem_device_init(struct device *dev) { }
static inline void of_reserved_mem_device_release(struct device *dev) { }
static inline void early_init_dt_scan_reserved_mem(void) { }
#endif

#endif /* __OF_RESERVED_MEM_H */
