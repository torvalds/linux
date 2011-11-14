#ifndef TARGET_CORE_DEVICE_H
#define TARGET_CORE_DEVICE_H


// external
extern int transport_lookup_cmd_lun(struct se_cmd *, u32);
extern int transport_lookup_tmr_lun(struct se_cmd *, u32);

#endif /* TARGET_CORE_DEVICE_H */
