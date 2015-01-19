/*
 * Amlogic COMMON PMU driver head file
 */
#ifndef __AML_PMU_COMMON_H__ 
#define __AML_PMU_COMMON_H__ 

#include <linux/amlogic/battery_parameter.h>
#define CHARGER_CHARGING            1               // battery is charging
#define CHARGER_DISCHARGING         2               // battery is discharging
#define CHARGER_NONE                3               // battery is in rest, this status can be seen when battery is full charged

#define COULOMB_BOTH                0               // PMU has inspective coulomb counter for charge and discharge
#define COULOMB_SINGLE_CHG_INC      1               // PMU has only one coulomb counter, value increase when charging
#define COULOMB_SINGLE_CHG_DEC      2               // PMU has only one coulomb counter, value decrease when charging
#define COULOMB_ALWAYS_DEC          3               // PMU has only one coulomb counter, value decrease always

#define PMU_GPIO_OUTPUT_LOW         0
#define PMU_GPIO_OUTPUT_HIGH        1
#define PMU_GPIO_OUTPUT_HIGHZ       2
#define PMU_GPIO_INPUT              3

struct aml_charger {
    int rest_vol;                                   // current volume of battery
    int ocv;                                        // open-circuit voltage of battery
    int ibat;                                       // battery current 
    int vbat;                                       // battery voltage
    int ocv_full;                                   // ocv when battery is charged full
    int ocv_empty;                                  // ocv when battery is disharged to empty
    int ocv_rest_vol;                               // battery volume @ocv accroding battery curve
    int charge_cc;                                  // charging coulomb counter
    int discharge_cc;                               // discharging coulomb counter
    int v_usb;                                      // voltage of USB
    int i_usb;                                      // current of USB
    int v_dcin;                                     // voltage of DCIN
    int i_dcin;                                     // current of DCIN
    uint32_t fault;                                 // indicate charge status register 

    uint8_t  charge_status;                         // charge status
    uint8_t  resume;                                // resume after long suspend
    uint8_t  soft_limit_to99;                       // limit volume to 99% when still have charge current
    uint8_t  ext_valid;                             // extern power valid, including USB or DCIN
    uint8_t  usb_valid;                             // status for usb is valid
    uint8_t  dcin_valid;                            // status for dcin is valid
    uint8_t  coulomb_type;                          // type of coulomb 
    uint8_t  bat_det;                               // battery detected 
    uint8_t  charge_timeout;                        // indicate charging timeout
    uint8_t  serial_batteries;                      // indicate how many batteries serialed, 
                                                    // 0 -> 1 battery, 1 -> 2 batteries
};

typedef int (*pmu_callback)(struct aml_charger *charger, void *pdata);

struct aml_pmu_driver {
    char  *name;
    int  (*pmu_get_coulomb)(struct aml_charger *charger);               // get coulomb counter
    int  (*pmu_clear_coulomb)(struct aml_charger *charger);             // clear coulomb counter
    int  (*pmu_update_status)(struct aml_charger *charger);             // update status
    int  (*pmu_set_rdc)(int rdc);                                       // update RDC for calculate
    int  (*pmu_set_gpio)(int gpio, int value);                          // export for other driver 
    int  (*pmu_get_gpio)(int gpio, int *value);                         // export for other driver
    int  (*pmu_reg_read)(int addr, uint8_t *buf);                       // single register read
    int  (*pmu_reg_write)(int addr, uint8_t value);                     // single register write
    int  (*pmu_reg_reads)(int addr, uint8_t *buf, int count);           // large amount registers reads
    int  (*pmu_reg_writes)(int addr, uint8_t *buf, int count);          // large amount registers writes
    int  (*pmu_set_bits)(int addr, uint8_t bits, uint8_t mask);         // set bits in mask
    int  (*pmu_set_usb_current_limit)(int curr);                        // set usb current limit
    int  (*pmu_set_charge_current)(int curr);                           // set charge current
    void (*pmu_power_off)(void);                                        // power off system
};

extern void *pmu_alloc_mutex(void);
extern void pmu_mutex_lock(void *mutex);
extern void pmu_mutex_unlock(void *mutex);
extern int  pmu_rtc_device_init(void);
extern int  pmu_rtc_set_alarm(unsigned long seconds);
   
struct aml_pmu_api {
    int     (*pmu_get_ocv_filter)(void);
    int     (*pmu_get_report_delay)(void);
    int     (*pmu_set_report_delay)(int);
    void    (*pmu_update_battery_capacity)(struct aml_charger *, struct battery_parameter *);
    void    (*pmu_probe_process)(struct aml_charger *, struct battery_parameter *);
    void    (*pmu_suspend_process)(struct aml_charger *);
    void    (*pmu_resume_process)(struct aml_charger *, struct battery_parameter *);
    void    (*pmu_update_battery_by_ocv)(struct aml_charger *, struct battery_parameter *);
    void    (*pmu_calibrate_probe_process)(struct aml_charger *);
    ssize_t (*pmu_format_dbg_buffer)(struct aml_charger *, char *);
};

extern int    aml_pmu_register_callback(pmu_callback callback, void *pdata, char *name);
extern int    aml_pmu_unregister_callback(char *name);
extern int    aml_pmu_register_driver(struct aml_pmu_driver *driver);
extern int    aml_pmu_register_api(struct aml_pmu_api *);
extern void   aml_pmu_clear_api(void);
extern struct aml_pmu_api *aml_pmu_get_api(void);
extern void   aml_pmu_clear_driver(void);
extern void   aml_pmu_do_callbacks(struct aml_charger *charger);
extern struct aml_pmu_driver* aml_pmu_get_driver(void);

extern struct aml_pmu_api *aml_pmu_get_api(void);
#endif /* __AML_PMU_COMMON_H__ */
