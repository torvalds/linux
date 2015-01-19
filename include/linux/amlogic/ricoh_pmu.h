/*
 * Head file for PMU RN5T618
 */
#ifndef __RN5T618_H__
#define __RN5T618_H__

#include <linux/power_supply.h>
#include <linux/amlogic/battery_parameter.h>
#include <linux/amlogic/aml_pmu_common.h>
#include <linux/notifier.h>
#define RN5T618_ADDR    0x32

#define RN5T618_DRIVER_VERSION              "v1.0"
#define RN5T618_DRIVER_NAME                 "rn5t618_pmu"
#define RN5T618_IRQ_NUM                     INT_GPIO_2
#define RN5T618_IRQ_NAME                    "rn5t618_irq"
#define RN5T618_WORK_CYCLE                  2000

#define RICOH_DBG(format,args...)                 \
    if (1) printk(KERN_DEBUG "[RN5T618]"format,##args)
#define RICOH_INFO(format,args...)                 \
    if (1) printk(KERN_WARNING "[RN5T618]"format,##args)
#define RICOH_ERR(format,args...)                 \
    if (1) printk(KERN_ERR "[RN5T618]"format,##args)
#define ABS(x)                  ((x) >0 ? (x) : -(x))

/*
 * status of charger
 */
#define RN5T618_CHARGER_CHARGING            1
#define RN5T618_CHARGER_DISCHARGING         2
#define RN5T618_CHARGER_NONE                3 

#define RN5T618_CHARGE_OFF                  0x00
#define RN5T618_CHARGE_ADP_READY            0x01
#define RN5T618_CHARGE_TRICKLE              0x02
#define RN5T618_CHARGE_RAPID                0x03
#define RN5T618_CHARGE_COMPLETE             0x04
#define RN5T618_CHARGE_SUSPEND              0x05
#define RN5T618_CHARGE_OVER_VOLTAGE         0x06
#define RN5T618_BATTERY_ERROR               0x07
#define RN5T618_NO_BATTERY                  0x08
#define RN5T618_BATTERY_OVER_VOLTAGE        0x09
#define RN5T618_BATTERY_TEMPERATURE_ERROR   0x0A
#define RN5T618_DIE_ERROR                   0x0B
#define RN5T618_DIE_SHUTDOWN                0x0C
#define RN5T618_NO_BATTERY2                 0x10
#define RN5T618_CHARGE_USB_READY            0x11


#define RICOH_ATTR(_name)                           \
{                                                   \
    .attr = { .name = #_name,.mode = 0644 },        \
    .show =  _name##_show,                          \
    .store = _name##_store,                         \
}

/*
 * @soft_limit_to99: flag for if we need to restrict battery capacity to 99% when have charge current,
 *                   even battery voltage is over ocv_full;
 * @para:            parameters for call back funtions, user implement;
 * @pmu_call_back:   call back function for axp_charging_monitor, you can add anything you want to do
 *                   in this function, this funtion will be called every 2 seconds by default
 */
struct ricoh_pmu_init_data { 
    int   soft_limit_to99;                          // software limit battery volume to 99% when have charge current
    int   charge_timeout_retry;                     // retry charge count when charge timeout
    int   temp_to_stop_charger;                     // stop charger when temperature is higher than this value
    int   temp_to_power_off;                        // power off system when temperature is higher than this value
    int   vbus_dcin_short_connect;
    int   reset_to_system;                          // flag to indicate if need reset to system
    struct battery_parameter *board_battery;        // battery parameter
};

struct rn5t618_supply {
    struct   aml_charger aml_charger;
	int32_t  interval;                                                  // PMU work cycle
    int32_t  usb_connect_type;                                          // usb is connect to PC or adapter
    int32_t  charge_timeout_retry;                                      // retry charge count when charge timeout
    int32_t  vbus_dcin_short_connect;                                   // indicate VBUS and DCIN are short connected
    int32_t  irq;

	struct power_supply batt;                                           // power supply sysfs
	struct power_supply	ac;
	struct power_supply	usb;
	struct power_supply_info *battery_info;
	struct delayed_work work;                                           // work struct
    struct work_struct  irq_work;                                       // work for IRQ 

	struct device *master;
};

/*
 * functions for otg and usb charger detect
 */
#ifdef CONFIG_AMLOGIC_USB
int rn5t618_otg_change(struct notifier_block *nb, unsigned long value, void *pdata);
int rn5t618_usb_charger(struct notifier_block *nb, unsigned long value, void *pdata);
#endif

/*
 * Global variable declaration
 */
extern struct rn5t618_supply *g_rn5t618_supply;                         // export global charger struct
extern struct i2c_client *g_rn5t618_client;                             // i2c client for register RW

/*
 * API export of pmu base operation
 */
extern int  rn5t618_write   (int add, uint8_t val);
extern int  rn5t618_writes  (int add, uint8_t *buff, int len);
extern int  rn5t618_read    (int add, uint8_t *val);
extern int  rn5t618_reads   (int add, uint8_t *buff, int len);
extern int  rn5t618_set_bits(int addr, uint8_t bits, uint8_t mask);
extern int  rn5t618_get_battery_voltage(void);                          // in mV
extern int  rn5t618_get_battery_temperature(void);                      // in C
extern int  rn5t618_get_battery_current(void);                          // in mA
extern int  rn5t618_get_charge_status(void);                            // see status of charger
extern int  rn5t618_set_gpio(int gpio, int output);                     // gpio 0 ~ 3, output: 1->high, 0->low
extern int  rn5t618_get_gpio(int gpio, int *val);                       // gpio 0 ~ 3, val: 1->high, 0->low
extern int  rn5t618_set_dcin_current_limit(int limit);                  // current limit for DCIN, in mA
extern int  rn5t618_set_usb_current_limit(int limit);                   // current limit of VBUS, in mA
extern int  rn5t618_set_usb_voltage_limit(int voltage);                 // voltage limit of VBUS, in mV
extern int  rn5t618_set_dcdc_voltage(int dcdc, uint32_t voltage);       // dcdc 0 ~ 3, voltage in mV
extern int  rn5t618_get_dcdc_voltage(int dcdc, uint32_t *uV);           // return dcdc voltage
extern int  rn5t618_set_charge_enable(int enable);                      // 0->disable charger, 1->enable charger
extern int  rn5t618_set_charge_current(int curr);                       // current of charge, in uA
extern int  rn5t618_get_battery_percent(void);                          // return volume of battery, in 1%
extern int  rn5t618_cal_ocv(int ibat, int vbat, int dir);               // ibat in mA, vbat in mV
extern int  rn5t618_get_saved_coulomb(void);                            // for coulomb clear issue
extern void rn5t618_power_off(void);                                    // power of system

#endif /* __RN5T618_H__ */
