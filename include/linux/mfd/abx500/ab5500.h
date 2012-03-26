/*
 * Copyright (C) ST-Ericsson 2011
 *
 * License Terms: GNU General Public License v2
 */
#ifndef MFD_AB5500_H
#define MFD_AB5500_H

struct device;

enum ab5500_devid {
	AB5500_DEVID_ADC,
	AB5500_DEVID_LEDS,
	AB5500_DEVID_POWER,
	AB5500_DEVID_REGULATORS,
	AB5500_DEVID_SIM,
	AB5500_DEVID_RTC,
	AB5500_DEVID_CHARGER,
	AB5500_DEVID_FUELGAUGE,
	AB5500_DEVID_VIBRATOR,
	AB5500_DEVID_CODEC,
	AB5500_DEVID_USB,
	AB5500_DEVID_OTP,
	AB5500_DEVID_VIDEO,
	AB5500_DEVID_DBIECI,
	AB5500_DEVID_ONSWA,
	AB5500_NUM_DEVICES,
};

enum ab5500_banks {
	AB5500_BANK_VIT_IO_I2C_CLK_TST_OTP = 0,
	AB5500_BANK_VDDDIG_IO_I2C_CLK_TST = 1,
	AB5500_BANK_VDENC = 2,
	AB5500_BANK_SIM_USBSIM  = 3,
	AB5500_BANK_LED = 4,
	AB5500_BANK_ADC  = 5,
	AB5500_BANK_RTC  = 6,
	AB5500_BANK_STARTUP  = 7,
	AB5500_BANK_DBI_ECI  = 8,
	AB5500_BANK_CHG  = 9,
	AB5500_BANK_FG_BATTCOM_ACC = 10,
	AB5500_BANK_USB = 11,
	AB5500_BANK_IT = 12,
	AB5500_BANK_VIBRA = 13,
	AB5500_BANK_AUDIO_HEADSETUSB = 14,
	AB5500_NUM_BANKS = 15,
};

enum ab5500_banks_addr {
	AB5500_ADDR_VIT_IO_I2C_CLK_TST_OTP = 0x4A,
	AB5500_ADDR_VDDDIG_IO_I2C_CLK_TST = 0x4B,
	AB5500_ADDR_VDENC = 0x06,
	AB5500_ADDR_SIM_USBSIM  = 0x04,
	AB5500_ADDR_LED = 0x10,
	AB5500_ADDR_ADC  = 0x0A,
	AB5500_ADDR_RTC  = 0x0F,
	AB5500_ADDR_STARTUP  = 0x03,
	AB5500_ADDR_DBI_ECI  = 0x07,
	AB5500_ADDR_CHG  = 0x0B,
	AB5500_ADDR_FG_BATTCOM_ACC = 0x0C,
	AB5500_ADDR_USB = 0x05,
	AB5500_ADDR_IT = 0x0E,
	AB5500_ADDR_VIBRA = 0x02,
	AB5500_ADDR_AUDIO_HEADSETUSB = 0x0D,
};

/*
 * Interrupt register offsets
 * Bank : 0x0E
 */
#define AB5500_IT_SOURCE0_REG		0x20
#define AB5500_IT_SOURCE1_REG		0x21
#define AB5500_IT_SOURCE2_REG		0x22
#define AB5500_IT_SOURCE3_REG		0x23
#define AB5500_IT_SOURCE4_REG		0x24
#define AB5500_IT_SOURCE5_REG		0x25
#define AB5500_IT_SOURCE6_REG		0x26
#define AB5500_IT_SOURCE7_REG		0x27
#define AB5500_IT_SOURCE8_REG		0x28
#define AB5500_IT_SOURCE9_REG		0x29
#define AB5500_IT_SOURCE10_REG		0x2A
#define AB5500_IT_SOURCE11_REG		0x2B
#define AB5500_IT_SOURCE12_REG		0x2C
#define AB5500_IT_SOURCE13_REG		0x2D
#define AB5500_IT_SOURCE14_REG		0x2E
#define AB5500_IT_SOURCE15_REG		0x2F
#define AB5500_IT_SOURCE16_REG		0x30
#define AB5500_IT_SOURCE17_REG		0x31
#define AB5500_IT_SOURCE18_REG		0x32
#define AB5500_IT_SOURCE19_REG		0x33
#define AB5500_IT_SOURCE20_REG		0x34
#define AB5500_IT_SOURCE21_REG		0x35
#define AB5500_IT_SOURCE22_REG		0x36
#define AB5500_IT_SOURCE23_REG		0x37

#define AB5500_NUM_IRQ_REGS		23

/**
 * struct ab5500
 * @access_mutex: lock out concurrent accesses to the AB registers
 * @dev: a pointer to the device struct for this chip driver
 * @ab5500_irq: the analog baseband irq
 * @irq_base: the platform configuration irq base for subdevices
 * @chip_name: name of this chip variant
 * @chip_id: 8 bit chip ID for this chip variant
 * @irq_lock: a lock to protect the mask
 * @abb_events: a local bit mask of the prcmu wakeup events
 * @event_mask: a local copy of the mask event registers
 * @last_event_mask: a copy of the last event_mask written to hardware
 * @startup_events: a copy of the first reading of the event registers
 * @startup_events_read: whether the first events have been read
 */
struct ab5500 {
	struct mutex access_mutex;
	struct device *dev;
	unsigned int ab5500_irq;
	unsigned int irq_base;
	char chip_name[32];
	u8 chip_id;
	struct mutex irq_lock;
	u32 abb_events;
	u8 mask[AB5500_NUM_IRQ_REGS];
	u8 oldmask[AB5500_NUM_IRQ_REGS];
	u8 startup_events[AB5500_NUM_IRQ_REGS];
	bool startup_events_read;
#ifdef CONFIG_DEBUG_FS
	unsigned int debug_bank;
	unsigned int debug_address;
#endif
};

struct ab5500_platform_data {
	struct {unsigned int base; unsigned int count; } irq;
	void *dev_data[AB5500_NUM_DEVICES];
	struct abx500_init_settings *init_settings;
	unsigned int init_settings_sz;
	bool pm_power_off;
};

#endif /* MFD_AB5500_H */
