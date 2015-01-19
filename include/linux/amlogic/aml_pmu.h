#ifndef __AML_PMU_H__
#define __AML_PMU_H__

#include <linux/power_supply.h>
#include <linux/amlogic/aml_pmu_common.h>
#include <linux/notifier.h>
#define BATCAPCORRATE           5                                       // battery capability is very low
#define ABS(x)                  ((x) >0 ? (x) : -(x))
#define AML_PMU_WORK_CYCLE      2000                                    // PMU work cycle

/*
 * debug message control
 */
#define AML_PMU_DBG(format,args...)                 \
    if (1) printk(KERN_DEBUG "[AML_PMU]"format,##args)
#define AML_PMU_INFO(format,args...)                 \
    if (1) printk(KERN_WARNING "[AML_PMU]"format,##args)
#define AML_PMU_ERR(format,args...)                 \
    if (1) printk(KERN_ERR "[AML_PMU]"format,##args)

#define AML_PMU_CHG_ATTR(_name)                     \
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
struct amlogic_pmu_init {
    int   soft_limit_to99;                          // software limit battery volume to 99% when have charge current
    int   charge_timeout_retry;                     // retry charge count when charge timeout
    int   vbus_dcin_short_connect;                  // if VBUS and DCIN are short connected
    struct battery_parameter *board_battery;        // battery parameter
};

#ifdef CONFIG_AML1212
#define AML1212_ADDR                    0x35
#define AML1212_SUPPLY_NAME             "aml1212"
#define AML1212_IRQ_NAME                "aml1212-irq"
#define AML_PMU_IRQ_NUM                 INT_GPIO_2
#define AML1212_SUPPLY_ID               0
#define AML1212_DRIVER_VERSION          "v0.2"

#define AML1212_OTP_GEN_CONTROL0        0x17

#define AML1212_CHG_CTRL0               0x29
#define AML1212_CHG_CTRL1               0x2A
#define AML1212_CHG_CTRL2               0x2B
#define AML1212_CHG_CTRL3               0x2C
#define AML1212_CHG_CTRL4               0x2D
#define AML1212_CHG_CTRL5               0x2E
#define AML1212_SAR_ADJ                 0x73

#define AML1212_GEN_CNTL0               0x80
#define AML1212_GEN_CNTL1               0x81
#define AML1212_PWR_UP_SW_ENABLE        0x82        // software power up
#define AML1212_PWR_DN_SW_ENABLE        0x84        // software power down
#define AML1212_GEN_STATUS0             0x86
#define AML1212_GEN_STATUS1             0x87
#define AML1212_GEN_STATUS2             0x88
#define AML1212_GEN_STATUS3             0x89
#define AML1212_GEN_STATUS4             0x8A
#define AML1212_WATCH_DOG               0x8F
#define AML1212_PWR_KEY_ADDR            0x90
#define AML1212_SAR_SW_EN_FIELD         0x9A
#define AML1212_SAR_CNTL_REG0           0x9B
#define AML1212_SAR_CNTL_REG2           0x9D
#define AML1212_SAR_CNTL_REG3           0x9E
#define AML1212_SAR_CNTL_REG5           0xA0
#define AML1212_SAR_RD_IBAT_LAST        0xAB        // battery current measure
#define AML1212_SAR_RD_VBAT_ACTIVE      0xAF        // battery voltage measure
#define AML1212_SAR_RD_MANUAL           0xB1        // manual measure
#define AML1212_SAR_RD_IBAT_ACC         0xB5        // IBAT accumulated result, coulomb
#define AML1212_SAR_RD_IBAT_CNT         0xB9        // IBAT measure count
#define AML1212_GPIO_OUTPUT_CTRL        0xC3        // GPIO output control
#define AML1212_GPIO_INPUT_STATUS       0xC4        // GPIO input status
#define AML1212_IRQ_MASK_0              0xC8        // IRQ Mask base address
#define AML1212_IRQ_STATUS_CLR_0        0xCF        // IRQ status base address
#define AML1212_SP_CHARGER_STATUS0      0xDE        // charge status0
#define AML1212_SP_CHARGER_STATUS1      0xDF        // charge status1
#define AML1212_SP_CHARGER_STATUS2      0xE0        // charge status2
#define AML1212_SP_CHARGER_STATUS3      0xE1        // charge status3
#define AML1212_SP_CHARGER_STATUS4      0xE2        // charge status4
#define AML1212_PIN_MUX4                0xF4        // pin mux select 4

#define AML_PMU_DCDC1                   0
#define AML_PMU_DCDC2                   1
#define AML_PMU_DCDC3                   2
#define AML_PMU_BOOST                   3
#define AML_PMU_LDO1                    4
#define AML_PMU_LDO2                    5
#define AML_PMU_LDO3                    6
#define AML_PMU_LDO4                    7
#define AML_PMU_LDO5                    8

struct aml1212_supply {
    struct aml_charger aml_charger;
	int32_t  interval;                                                  // PMU work cycle
    int32_t  usb_connect_type;
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
 * function declaration here
 */
int aml_pmu_write  (int add, uint8_t  val);                             // single byte write
int aml_pmu_write16(int add, uint16_t val);                             // 16 bits register write
int aml_pmu_writes (int add, uint8_t *buff, int len);                   // block write
int aml_pmu_read   (int add, uint8_t  *val);
int aml_pmu_read16 (int add, uint16_t *val);
int aml_pmu_reads  (int add, uint8_t *buff, int len);
int aml_pmu_set_bits(int32_t addr, uint8_t bits, uint8_t mask);         // set bis in mask

int aml_pmu_set_dcin(int enable);                                       // enable / disable dcin power path
int aml_pmu_set_gpio(int pin, int val);                                 // set gpio value
int aml_pmu_get_gpio(int pin, int *val);                                // get gpio value

int aml_pmu_get_voltage(void);                                          // return battery voltage
int aml_pmu_get_current(void);                                          // return battery current
int aml_pmu_get_battery_percent(void);                                  // return battery capacity now, -1 means error

// bc_mode, indicate usb is conneted to PC or adatper, please see <mach/usbclock.h>
int aml_pmu_set_usb_current_limit(int curr, int bc_mode);               // set usb current limit, in mA
int aml_pmu_set_usb_voltage_limit(int voltage);                         // set usb voltage limit, in mV
int aml_pmu_set_charge_current(int chg_cur);                            // set charge current, in uA
int aml_pmu_set_charge_voltage(int voltage);                            // set charge target voltage, in uA
int aml_pmu_set_charge_end_rate(int rate);                              // set charge end rate, 10% or 20%
int aml_pmu_set_adc_freq(int freq);                                     // SAR ADC auto-sample frequent, for coulomb
int aml_pmu_set_precharge_time(int minute);                             // set pre-charge time when battery voltage is very low
int aml_pmu_set_fastcharge_time(int minute);                            // set fast charge time when in CC period
int aml_pmu_set_charge_enable(int en);                                  // enable or disable charge 

void aml_pmu_set_voltage(int dcdc, int voltage);                        // set dcdc voltage, in mV
void aml_pmu_poweroff(void);                                            // power off PMU

void aml_pmu_clear_coulomb(void);                                       // clear coulomb register
extern struct i2c_client *g_aml1212_client;                              // i2c client for register RW
extern struct aml_pmu_driver aml1212_driver;
extern int aml1212_usb_charger(struct notifier_block *nb, unsigned long value, void *pdata);
extern int aml1212_otg_change(struct notifier_block *nb, unsigned long value, void *pdata);
#endif  /* CONFIG_AML1212 */

#ifdef CONFIG_AML1216
#define AML1216_ADDR    0x35

#define AML1216_DRIVER_VERSION              "v1.0"
#define AML1216_DRIVER_NAME                 "aml1216_pmu"
#define AML1216_IRQ_NUM                     INT_GPIO_2
#define AML1216_IRQ_NAME                    "aml1216_irq"
#define AML1216_WORK_CYCLE                  2000

//add for AML1216 ctroller.
#define AML1216_OTP_GEN_CONTROL0        0x17

#define AML1216_CHG_CTRL0               0x29
#define AML1216_CHG_CTRL1               0x2A
#define AML1216_CHG_CTRL2               0x2B
#define AML1216_CHG_CTRL3               0x2C
#define AML1216_CHG_CTRL4               0x2D
#define AML1216_CHG_CTRL5               0x2E
#define AML1216_CHG_CTRL6               0x129
#define AML1216_SAR_ADJ                 0x73


#define AML1216_GEN_CNTL0               0x80
#define AML1216_GEN_CNTL1               0x81
#define AML1216_PWR_UP_SW_ENABLE        0x82        // software power up
#define AML1216_PWR_DN_SW_ENABLE        0x84        // software power down
#define AML1216_GEN_STATUS0             0x86
#define AML1216_GEN_STATUS1             0x87
#define AML1216_GEN_STATUS2             0x88
#define AML1216_GEN_STATUS3             0x89
#define AML1216_GEN_STATUS4             0x8A
#define AML1216_WATCH_DOG               0x8F
#define AML1216_PWR_KEY_ADDR            0x90
#define AML1216_SAR_SW_EN_FIELD         0x9A
#define AML1216_SAR_CNTL_REG0           0x9B
#define AML1216_SAR_CNTL_REG2           0x9D
#define AML1216_SAR_CNTL_REG3           0x9E
#define AML1216_SAR_CNTL_REG5           0xA0
#define AML1216_SAR_RD_IBAT_LAST        0xAB        // battery current measure
#define AML1216_SAR_RD_VBAT_ACTIVE      0xAF        // battery voltage measure
#define AML1216_SAR_RD_MANUAL           0xB1        // manual measure
#define AML1216_SAR_RD_IBAT_ACC         0xB5        // IBAT accumulated result, coulomb
#define AML1216_SAR_RD_IBAT_CNT         0xB9        // IBAT measure count
#define AML1216_GPIO_OUTPUT_CTRL        0xC3        // GPIO output control
#define AML1216_GPIO_INPUT_STATUS       0xC4        // GPIO input status
#define AML1216_IRQ_MASK_0              0xC8        // IRQ Mask base address
#define AML1216_IRQ_STATUS_CLR_0        0xCF        // IRQ status base address
#define AML1216_SP_CHARGER_STATUS0      0xDE        // charge status0
#define AML1216_SP_CHARGER_STATUS1      0xDF        // charge status1
#define AML1216_SP_CHARGER_STATUS2      0xE0        // charge status2
#define AML1216_SP_CHARGER_STATUS3      0xE1        // charge status3
#define AML1216_SP_CHARGER_STATUS4      0xE2        // charge status4
#define AML1216_PIN_MUX4                0xF4        // pin mux select 4

#define AML1216_DCDC1                   0
#define AML1216_DCDC2                   1
#define AML1216_DCDC3                   2
#define AML1216_BOOST                   3
#define AML1216_LDO1                    4
#define AML1216_LDO2                    5
#define AML1216_LDO3                    6
#define AML1216_LDO4                    7
#define AML1216_LDO5                    8

#define AML1216_CHARGER_CHARGING            1
#define AML1216_CHARGER_DISCHARGING         2
#define AML1216_CHARGER_NONE                3 

#define AML1216_DBG(format,args...)                 \
    if (1) printk(KERN_DEBUG "[AML1216]"format,##args)
#define AML1216_INFO(format,args...)                 \
    if (1) printk(KERN_WARNING "[AML1216]"format,##args)
#define AML1216_ERR(format,args...)                 \
    if (1) printk(KERN_ERR "[AML1216]"format,##args)
#define ABS(x)                  ((x) >0 ? (x) : -(x))

#define AML_ATTR(_name)                           \
{                                                   \
    .attr = { .name = #_name,.mode = 0644 },        \
    .show =  _name##_show,                          \
    .store = _name##_store,                         \
}

struct aml1216_supply {
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
    struct notifier_block nb;
    struct device *master;
};

/*
 * API export of pmu base operation
 */
extern int  aml1216_write(int32_t add, uint8_t val);
extern int  aml1216_write16(int32_t add, uint16_t val);
extern int  aml1216_writes(int32_t add, uint8_t *buff, int len);
extern int  aml1216_read    (int add, uint8_t *val);
extern int  aml1216_read16(int add, uint16_t *val);
extern int  aml1216_reads   (int add, uint8_t *buff, int len);
extern int  aml1216_set_bits(int addr, uint8_t bits, uint8_t mask);
extern int  aml1216_get_battery_voltage(void);                           // in mV
extern int  aml1216_get_battery_temperature(void);                       // in C
extern int  aml1216_get_battery_current(void);                           // in mA
extern int  aml1216_get_charge_status(void);                             // see status of charger
extern int  aml1216_set_gpio(int gpio, int output);                      // gpio 0 ~ 3, output: 1->high, 0->low
extern int  aml1216_get_gpio(int gpio, int *val);                        // gpio 0 ~ 3, val: 1->high, 0->low
extern int  aml1216_set_dcin_current_limit(int limit);                   // current limit for DCIN, in mA
extern int  aml1216_set_usb_current_limit(int limit);                    // current limit of VBUS, in mA
extern int  aml1216_set_usb_voltage_limit(int voltage);                  // voltage limit of VBUS, in mV
extern int  aml1216_set_dcdc_voltage(int dcdc, uint32_t voltage);        // dcdc 0 ~ 3, voltage in mV
extern int  aml1216_get_dcdc_voltage(int dcdc, uint32_t *uV);            // return dcdc voltage
extern int  aml1216_set_charge_enable(int enable);                       // 0->disable charger, 1->enable charger
extern int  aml1216_set_charge_current(int curr);                        // current of charge, in uA
extern int  aml1216_get_battery_percent(void);                           // return volume of battery, in 1%
extern int  aml1216_cal_ocv(int ibat, int vbat, int dir);                // ibat in mA, vbat in mV
extern void aml1216_power_off(void);                                     // power of system
/*
 * Global variable declaration
 */
extern struct aml1216_supply *g_aml1216_supply;                           // export global charger struct
extern struct i2c_client *g_aml1216_client;                              // i2c client for register RW
extern int aml1216_otg_change(struct notifier_block *nb, unsigned long value, void *pdata);
extern int aml1216_usb_charger(struct notifier_block *nb, unsigned long value, void *pdata);
#endif      /* CONFIG_AML1216 */

#ifdef CONFIG_AML1218
#define AML1218_ADDR    0x35

#define AML1218_DRIVER_VERSION              "v1.0"
#define AML1218_DRIVER_NAME                 "aml1218_pmu"
#define AML1218_IRQ_NUM                     INT_GPIO_2
#define AML1218_IRQ_NAME                    "aml1218_irq"
#define AML1218_WORK_CYCLE                  2000

//add for AML1218 ctroller.
#define AML1218_OTP_GEN_CONTROL0        0x17

#define AML1218_CHG_CTRL0               0x29
#define AML1218_CHG_CTRL1               0x2A
#define AML1218_CHG_CTRL2               0x2B
#define AML1218_CHG_CTRL3               0x2C
#define AML1218_CHG_CTRL4               0x2D
#define AML1218_CHG_CTRL5               0x2E
#define AML1218_CHG_CTRL6               0x129
#define AML1218_SAR_ADJ                 0x73


#define AML1218_GEN_CNTL0               0x80
#define AML1218_GEN_CNTL1               0x81
#define AML1218_PWR_UP_SW_ENABLE        0x82        // software power up
#define AML1218_PWR_DN_SW_ENABLE        0x84        // software power down
#define AML1218_GEN_STATUS0             0x86
#define AML1218_GEN_STATUS1             0x87
#define AML1218_GEN_STATUS2             0x88
#define AML1218_GEN_STATUS3             0x89
#define AML1218_GEN_STATUS4             0x8A
#define AML1218_WATCH_DOG               0x8F
#define AML1218_PWR_KEY_ADDR            0x90
#define AML1218_SAR_SW_EN_FIELD         0x9A
#define AML1218_SAR_CNTL_REG0           0x9B
#define AML1218_SAR_CNTL_REG2           0x9D
#define AML1218_SAR_CNTL_REG3           0x9E
#define AML1218_SAR_CNTL_REG5           0xA0
#define AML1218_SAR_RD_IBAT_LAST        0xAB        // battery current measure
#define AML1218_SAR_RD_VBAT_ACTIVE      0xAF        // battery voltage measure
#define AML1218_SAR_RD_MANUAL           0xB1        // manual measure
#define AML1218_SAR_RD_IBAT_ACC         0xB5        // IBAT accumulated result, coulomb
#define AML1218_SAR_RD_IBAT_CNT         0xB9        // IBAT measure count
#define AML1218_GPIO_OUTPUT_CTRL        0xC3        // GPIO output control
#define AML1218_GPIO_INPUT_STATUS       0xC4        // GPIO input status
#define AML1218_IRQ_MASK_0              0xC8        // IRQ Mask base address
#define AML1218_IRQ_STATUS_CLR_0        0xCF        // IRQ status base address
#define AML1218_SP_CHARGER_STATUS0      0xDE        // charge status0
#define AML1218_SP_CHARGER_STATUS1      0xDF        // charge status1
#define AML1218_SP_CHARGER_STATUS2      0xE0        // charge status2
#define AML1218_SP_CHARGER_STATUS3      0xE1        // charge status3
#define AML1218_SP_CHARGER_STATUS4      0xE2        // charge status4
#define AML1218_PIN_MUX4                0xF4        // pin mux select 4

#define AML1218_DCDC1                   0
#define AML1218_DCDC2                   1
#define AML1218_DCDC3                   2
#define AML1218_BOOST                   3
#define AML1218_LDO1                    4
#define AML1218_LDO2                    5
#define AML1218_LDO3                    6
#define AML1218_LDO4                    7
#define AML1218_LDO5                    8

#define AML1218_CHARGER_CHARGING            1
#define AML1218_CHARGER_DISCHARGING         2
#define AML1218_CHARGER_NONE                3 

#define AML1218_DBG(format,args...)                 \
    if (1) printk(KERN_DEBUG "[AML1218]"format,##args)
#define AML1218_INFO(format,args...)                 \
    if (1) printk(KERN_WARNING "[AML1218]"format,##args)
#define AML1218_ERR(format,args...)                 \
    if (1) printk(KERN_ERR "[AML1218]"format,##args)
#define ABS(x)                  ((x) >0 ? (x) : -(x))

#define AML_ATTR(_name)                           \
{                                                   \
    .attr = { .name = #_name,.mode = 0644 },        \
    .show =  _name##_show,                          \
    .store = _name##_store,                         \
}

struct aml1218_supply {
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
    struct notifier_block nb;
    struct device *master;
};

/*
 * API export of pmu base operation
 */
extern int  aml1218_write(int32_t add, uint8_t val);
extern int  aml1218_write16(int32_t add, uint16_t val);
extern int  aml1218_writes(int32_t add, uint8_t *buff, int len);
extern int  aml1218_read    (int add, uint8_t *val);
extern int  aml1218_read16(int add, uint16_t *val);
extern int  aml1218_reads   (int add, uint8_t *buff, int len);
extern int  aml1218_set_bits(int addr, uint8_t bits, uint8_t mask);
extern int  aml1218_get_battery_voltage(void);                           // in mV
extern int  aml1218_get_battery_temperature(void);                       // in C
extern int  aml1218_get_battery_current(void);                           // in mA
extern int  aml1218_get_charge_status(void);                             // see status of charger
extern int  aml1218_set_gpio(int gpio, int output);                      // gpio 0 ~ 3, output: 1->high, 0->low
extern int  aml1218_get_gpio(int gpio, int *val);                        // gpio 0 ~ 3, val: 1->high, 0->low
extern int  aml1218_set_dcin_current_limit(int limit);                   // current limit for DCIN, in mA
extern int  aml1218_set_usb_current_limit(int limit);                    // current limit of VBUS, in mA
extern int  aml1218_set_usb_voltage_limit(int voltage);                  // voltage limit of VBUS, in mV
extern int  aml1218_set_dcdc_voltage(int dcdc, uint32_t voltage);        // dcdc 0 ~ 3, voltage in mV
extern int  aml1218_get_dcdc_voltage(int dcdc, uint32_t *uV);            // return dcdc voltage
extern int  aml1218_set_charge_enable(int enable);                       // 0->disable charger, 1->enable charger
extern int  aml1218_set_charge_current(int curr);                        // current of charge, in uA
extern int  aml1218_get_battery_percent(void);                           // return volume of battery, in 1%
extern int  aml1218_cal_ocv(int ibat, int vbat, int dir);                // ibat in mA, vbat in mV
extern void aml1218_power_off(void);                                     // power of system
/*
 * Global variable declaration
 */
extern struct aml1218_supply *g_aml1218_supply;                           // export global charger struct
extern struct i2c_client *g_aml1218_client;                              // i2c client for register RW
extern int aml1218_otg_change(struct notifier_block *nb, unsigned long value, void *pdata);
extern int aml1218_usb_charger(struct notifier_block *nb, unsigned long value, void *pdata);
#endif      /* CONFIG_AML1218 */

#endif /* __AML_PMU_H__ */

