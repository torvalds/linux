/*-
 * Copyright (c) 2016 Michal Meloun <mmel@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _AS3722_H_

#include <sys/clock.h>

#define	AS3722_SD0_VOLTAGE			0x00
#define	  AS3722_SD_VSEL_MASK				0x7F /* For all SD */
#define	  AS3722_SD0_VSEL_MIN				0x01
#define	  AS3722_SD0_VSEL_MAX				0x5A
#define	  AS3722_SD0_VSEL_LOW_VOL_MAX			0x6E

#define	AS3722_SD1_VOLTAGE			0x01
#define	AS3722_SD2_VOLTAGE			0x02
#define	  AS3722_SD2_VSEL_MIN				0x01
#define	  AS3722_SD2_VSEL_MAX				0x7F
#define	AS3722_SD3_VOLTAGE			0x03
#define	AS3722_SD4_VOLTAGE			0x04
#define	AS3722_SD5_VOLTAGE			0x05
#define	AS3722_SD6_VOLTAGE			0x06
#define	AS3722_GPIO0_CONTROL			0x08
#define	  AS3722_GPIO_INVERT				0x80
#define	  AS3722_GPIO_IOSF_MASK				0x0F
#define	  AS3722_GPIO_IOSF_SHIFT			3
#define	  AS3722_GPIO_MODE_MASK				0x07
#define	  AS3722_GPIO_MODE_SHIFT			0

#define	AS3722_GPIO1_CONTROL			0x09
#define	AS3722_GPIO2_CONTROL			0x0A
#define	AS3722_GPIO3_CONTROL			0x0B
#define	AS3722_GPIO4_CONTROL			0x0C
#define	AS3722_GPIO5_CONTROL			0x0D
#define	AS3722_GPIO6_CONTROL			0x0E
#define	AS3722_GPIO7_CONTROL			0x0F
#define	AS3722_LDO0_VOLTAGE			0x10
#define	  AS3722_LDO0_VSEL_MASK				0x1F
#define	  AS3722_LDO0_VSEL_MIN				0x01
#define	  AS3722_LDO0_VSEL_MAX				0x12
#define	  AS3722_LDO0_NUM_VOLT				0x12

#define	AS3722_LDO1_VOLTAGE			0x11
#define	  AS3722_LDO_VSEL_MASK				0x7F
#define	  AS3722_LDO_VSEL_MIN				0x01
#define	  AS3722_LDO_VSEL_MAX				0x7F
#define	  AS3722_LDO_VSEL_DNU_MIN			0x25
#define	  AS3722_LDO_VSEL_DNU_MAX			0x3F
#define	  AS3722_LDO_NUM_VOLT				0x80

#define	AS3722_LDO2_VOLTAGE			0x12
#define	AS3722_LDO3_VOLTAGE			0x13
#define	  AS3722_LDO3_VSEL_MASK				0x3F
#define	  AS3722_LDO3_VSEL_MIN				0x01
#define	  AS3722_LDO3_VSEL_MAX				0x2D
#define	  AS3722_LDO3_NUM_VOLT				0x2D
#define	  AS3722_LDO3_MODE_MASK				(0x3 << 6)
#define	  AS3722_LDO3_MODE_GET(x)			(((x) >> 6) & 0x3)
#define	  AS3722_LDO3_MODE(x)				(((x) & 0x3) << 6)
#define	  AS3722_LDO3_MODE_PMOS				AS3722_LDO3_MODE(0)
#define	  AS3722_LDO3_MODE_PMOS_TRACKING		AS3722_LDO3_MODE(1)
#define	  AS3722_LDO3_MODE_NMOS				AS3722_LDO3_MODE(2)
#define	  AS3722_LDO3_MODE_SWITCH			AS3722_LDO3_MODE(3)

#define	AS3722_LDO4_VOLTAGE			0x14
#define	AS3722_LDO5_VOLTAGE			0x15
#define	AS3722_LDO6_VOLTAGE			0x16
#define	  AS3722_LDO6_SEL_BYPASS			0x3F
#define	AS3722_LDO7_VOLTAGE			0x17
#define	AS3722_LDO9_VOLTAGE			0x19
#define	AS3722_LDO10_VOLTAGE			0x1A
#define	AS3722_LDO11_VOLTAGE			0x1B
#define	AS3722_LDO3_SETTINGS			0x1D
#define	AS3722_GPIO_DEB1			0x1E
#define	AS3722_GPIO_DEB2			0x1F
#define	AS3722_GPIO_SIGNAL_OUT			0x20
#define	AS3722_GPIO_SIGNAL_IN			0x21
#define	AS3722_REG_SEQU_MOD1			0x22
#define	AS3722_REG_SEQU_MOD2			0x23
#define	AS3722_REG_SEQU_MOD3			0x24
#define	AS3722_SD_PHSW_CTRL			0x27
#define	AS3722_SD_PHSW_STATUS			0x28

#define	AS3722_SD0_CONTROL			0x29
#define	 AS3722_SD0_MODE_FAST				(1 << 4)

#define	AS3722_SD1_CONTROL			0x2A
#define	 AS3722_SD1_MODE_FAST				(1 << 4)

#define	AS3722_SDMPH_CONTROL			0x2B
#define	AS3722_SD23_CONTROL			0x2C
#define	 AS3722_SD3_MODE_FAST				(1 << 6)
#define	 AS3722_SD2_MODE_FAST				(1 << 2)

#define	AS3722_SD4_CONTROL			0x2D
#define	 AS3722_SD4_MODE_FAST				(1 << 2)

#define	AS3722_SD5_CONTROL			0x2E
#define	 AS3722_SD5_MODE_FAST				(1 << 2)

#define	AS3722_SD6_CONTROL			0x2F
#define	 AS3722_SD6_MODE_FAST				(1 << 4)

#define	AS3722_SD_DVM				0x30
#define	AS3722_RESET_REASON			0x31
#define	AS3722_BATTERY_VOLTAGE_MONITOR		0x32
#define	AS3722_STARTUP_CONTROL			0x33
#define	AS3722_RESET_TIMER			0x34
#define	AS3722_REFERENCE_CONTROL		0x35
#define	AS3722_RESET_CONTROL			0x36
#define	AS3722_OVERTEMPERATURE_CONTROL		0x37
#define	AS3722_WATCHDOG_CONTROL			0x38
#define	AS3722_REG_STANDBY_MOD1			0x39
#define	AS3722_REG_STANDBY_MOD2			0x3A
#define	AS3722_REG_STANDBY_MOD3			0x3B
#define	AS3722_ENABLE_CTRL1			0x3C
#define	 AS3722_SD3_EXT_ENABLE_MASK			0xC0
#define	 AS3722_SD2_EXT_ENABLE_MASK			0x30
#define	 AS3722_SD1_EXT_ENABLE_MASK			0x0C
#define	 AS3722_SD0_EXT_ENABLE_MASK			0x03

#define	AS3722_ENABLE_CTRL2			0x3D
#define	 AS3722_SD6_EXT_ENABLE_MASK			0x30
#define	 AS3722_SD5_EXT_ENABLE_MASK			0x0C
#define	 AS3722_SD4_EXT_ENABLE_MASK			0x03

#define	AS3722_ENABLE_CTRL3			0x3E
#define	 AS3722_LDO3_EXT_ENABLE_MASK			0xC0
#define	 AS3722_LDO2_EXT_ENABLE_MASK			0x30
#define	 AS3722_LDO1_EXT_ENABLE_MASK			0x0C
#define	 AS3722_LDO0_EXT_ENABLE_MASK			0x03

#define	AS3722_ENABLE_CTRL4			0x3F
#define	 AS3722_LDO7_EXT_ENABLE_MASK			0xC0
#define	 AS3722_LDO6_EXT_ENABLE_MASK			0x30
#define	 AS3722_LDO5_EXT_ENABLE_MASK			0x0C
#define	 AS3722_LDO4_EXT_ENABLE_MASK			0x03

#define	AS3722_ENABLE_CTRL5			0x40
#define	 AS3722_LDO11_EXT_ENABLE_MASK			0xC0
#define	 AS3722_LDO10_EXT_ENABLE_MASK			0x30
#define	 AS3722_LDO9_EXT_ENABLE_MASK			0x0C

#define	AS3722_PWM_CONTROL_L			0x41
#define	AS3722_PWM_CONTROL_H			0x42
#define	AS3722_WATCHDOG_TIMER			0x46
#define	AS3722_WATCHDOG_SOFTWARE_SIGNAL		0x48
#define	AS3722_IO_VOLTAGE			0x49
#define	 AS3722_I2C_PULL_UP				(1 << 4)
#define	 AS3722_INT_PULL_UP				(1 << 5)

#define	AS3722_BATTERY_VOLTAGE_MONITOR2		0x4A
#define	AS3722_SD_CONTROL			0x4D
#define	  AS3722_SDN_CTRL(x)				(1 << (x))

#define	AS3722_LDO_CONTROL0			0x4E
#define	 AS3722_LDO7_CTRL				(1 << 7)
#define	 AS3722_LDO6_CTRL				(1 << 6)
#define	 AS3722_LDO5_CTRL				(1 << 5)
#define	 AS3722_LDO4_CTRL				(1 << 4)
#define	 AS3722_LDO3_CTRL				(1 << 3)
#define	 AS3722_LDO2_CTRL				(1 << 2)
#define	 AS3722_LDO1_CTRL				(1 << 1)
#define	 AS3722_LDO0_CTRL				(1 << 0)

#define	AS3722_LDO_CONTROL1			0x4F
#define	 AS3722_LDO11_CTRL				(1 << 3)
#define	 AS3722_LDO10_CTRL				(1 << 2)
#define	 AS3722_LDO9_CTRL				(1 << 1)

#define	AS3722_SD0_PROTECT			0x50
#define	AS3722_SD6_PROTECT			0x51
#define	AS3722_PWM_VCONTROL1			0x52
#define	AS3722_PWM_VCONTROL2			0x53
#define	AS3722_PWM_VCONTROL3			0x54
#define	AS3722_PWM_VCONTROL4			0x55
#define	AS3722_BB_CHARGER			0x57
#define	AS3722_CTRL_SEQU1			0x58
#define	AS3722_CTRL_SEQU2			0x59
#define	AS3722_OV_CURRENT			0x5A
#define	AS3722_OV_CURRENT_DEB			0x5B
#define	AS3722_SDLV_DEB				0x5C
#define	AS3722_OC_PG_CTRL			0x5D
#define	AS3722_OC_PG_CTRL2			0x5E
#define	AS3722_CTRL_STATUS			0x5F
#define	AS3722_RTC_CONTROL			0x60
#define	 AS3722_RTC_AM_PM_MODE				(1 << 7)
#define	 AS3722_RTC_CLK32K_OUT_EN			(1 << 5)
#define	 AS3722_RTC_IRQ_MODE				(1 << 3)
#define	 AS3722_RTC_ON					(1 << 2)
#define	 AS3722_RTC_ALARM_WAKEUP_EN			(1 << 1)
#define	 AS3722_RTC_REP_WAKEUP_EN			(1 << 0)

#define	AS3722_RTC_SECOND			0x61
#define	AS3722_RTC_MINUTE			0x62
#define	AS3722_RTC_HOUR				0x63
#define	AS3722_RTC_DAY				0x64
#define	AS3722_RTC_MONTH			0x65
#define	AS3722_RTC_YEAR				0x66
#define	AS3722_RTC_ALARM_SECOND			0x67
#define	AS3722_RTC_ALARM_MINUTE			0x68
#define	AS3722_RTC_ALARM_HOUR			0x69
#define	AS3722_RTC_ALARM_DAY			0x6A
#define	AS3722_RTC_ALARM_MONTH			0x6B
#define	AS3722_RTC_ALARM_YEAR			0x6C
#define	AS3722_SRAM				0x6D
#define	AS3722_RTC_ACCESS			0x6F
#define	AS3722_REG_STATUS			0x73
#define	AS3722_INTERRUPT_MASK1			0x74
#define	AS3722_INTERRUPT_MASK2			0x75
#define	AS3722_INTERRUPT_MASK3			0x76
#define	AS3722_INTERRUPT_MASK4			0x77
#define	AS3722_INTERRUPT_STATUS1		0x78
#define	AS3722_INTERRUPT_STATUS2		0x79
#define	AS3722_INTERRUPT_STATUS3		0x7A
#define	AS3722_INTERRUPT_STATUS4		0x7B
#define	AS3722_TEMP_STATUS			0x7D
#define	AS3722_ADC0_CONTROL			0x80
#define	AS3722_ADC1_CONTROL			0x81
#define	AS3722_ADC0_MSB_RESULT			0x82
#define	AS3722_ADC0_LSB_RESULT			0x83
#define	AS3722_ADC1_MSB_RESULT			0x84
#define	AS3722_ADC1_LSB_RESULT			0x85
#define	AS3722_ADC1_THRESHOLD_HI_MSB		0x86
#define	AS3722_ADC1_THRESHOLD_HI_LSB		0x87
#define	AS3722_ADC1_THRESHOLD_LO_MSB		0x88
#define	AS3722_ADC1_THRESHOLD_LO_LSB		0x89
#define	AS3722_ADC_CONFIGURATION		0x8A
#define	AS3722_ASIC_ID1				0x90
#define	AS3722_ASIC_ID2				0x91
#define	AS3722_LOCK				0x9E
#define	AS3722_FUSE7				0x9E
#define	  AS3722_FUSE7_SD0_LOW_VOLTAGE			(1 << 4)

struct as3722_reg_sc;
struct as3722_gpio_pin;

struct as3722_softc {
	device_t		dev;
	struct sx		lock;
	int			bus_addr;
	struct resource		*irq_res;
	void			*irq_h;

	uint8_t			chip_rev;
	int			int_pullup;
	int			i2c_pullup;

	/* Regulators. */
	struct as3722_reg_sc	**regs;
	int			nregs;

	/* GPIO */
	device_t		gpio_busdev;
	struct as3722_gpio_pin	**gpio_pins;
	int			gpio_npins;
	struct sx		gpio_lock;

};

#define	RD1(sc, reg, val)	as3722_read(sc, reg, val)
#define	WR1(sc, reg, val)	as3722_write(sc, reg, val)
#define	RM1(sc, reg, clr, set)	as3722_modify(sc, reg, clr, set)

int as3722_read(struct as3722_softc *sc, uint8_t reg, uint8_t *val);
int as3722_write(struct as3722_softc *sc, uint8_t reg, uint8_t val);
int as3722_modify(struct as3722_softc *sc, uint8_t reg, uint8_t clear,
    uint8_t set);
int as3722_read_buf(struct as3722_softc *sc, uint8_t reg, uint8_t *buf,
    size_t size);
int as3722_write_buf(struct as3722_softc *sc, uint8_t reg, uint8_t *buf,
    size_t size);

/* Regulators */
int as3722_regulator_attach(struct as3722_softc *sc, phandle_t node);
int as3722_regulator_map(device_t dev, phandle_t xref, int ncells,
    pcell_t *cells, int *num);

/* RTC */
int as3722_rtc_attach(struct as3722_softc *sc, phandle_t node);
int as3722_rtc_gettime(device_t dev, struct timespec *ts);
int as3722_rtc_settime(device_t dev, struct timespec *ts);

/* GPIO */
device_t as3722_gpio_get_bus(device_t dev);
int as3722_gpio_pin_max(device_t dev, int *maxpin);
int as3722_gpio_pin_getname(device_t dev, uint32_t pin, char *name);
int as3722_gpio_pin_getflags(device_t dev, uint32_t pin, uint32_t *flags);
int as3722_gpio_pin_getcaps(device_t dev, uint32_t pin, uint32_t *caps);
int as3722_gpio_pin_setflags(device_t dev, uint32_t pin, uint32_t flags);
int as3722_gpio_pin_set(device_t dev, uint32_t pin, unsigned int value);
int as3722_gpio_pin_get(device_t dev, uint32_t pin, unsigned int *val);
int as3722_gpio_pin_toggle(device_t dev, uint32_t pin);
int as3722_gpio_map_gpios(device_t dev, phandle_t pdev, phandle_t gparent,
    int gcells, pcell_t *gpios, uint32_t *pin, uint32_t *flags);
int as3722_gpio_attach(struct as3722_softc *sc, phandle_t node);
int as3722_pinmux_configure(device_t dev, phandle_t cfgxref);

#endif /* _AS3722_H_ */
