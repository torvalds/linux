/* linux/i2c/tps65910.h
 *
 *  TPS65910 Power Management Device Definitions.
 *
 * Based on include/linux/i2c/twl.h
 *
 * Copyright (C) 2010 Mistral Solutions Pvt Ltd <www.mistralsolutions.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __LINUX_I2C_TPS65910_H
#define __LINUX_I2C_TPS65910_H

#define TPS65910_NUM_SLAVES	1
/* I2C Slave Address 7-bit */
#define	TPS65910_I2C_ID0	0x2D /* general-purpose */
#define	TPS65910_I2C_ID1	0x12 /* Smart Reflex */

/* TPS65910 to host IRQ */
#define TPS65910_HOST_IRQ 	RK29_PIN4_PD3

/* TPS65910 MAX GPIOs */
#define TPS65910_GPIO_MAX	1

/*
 * ----------------------------------------------------------------------------
 * Registers, all 8 bits
 * ----------------------------------------------------------------------------
 */
#define	TPS65910_REG_SECONDS		0x00
#define TPS65910_REG_MINUTES		0x01
#define TPS65910_REG_HOURS		0x02
#define TPS65910_REG_DAYS		0x03
#define TPS65910_REG_MONTHS		0x04
#define TPS65910_REG_YEARS		0x05
#define TPS65910_REG_WEEKS		0x06
#define TPS65910_REG_ALARM_SECONDS	0x08
#define TPS65910_REG_ALARM_MINUTES	0x09
#define TPS65910_REG_ALARM_HOURS	0x0A
#define TPS65910_REG_ALARM_DAYS		0x0B
#define TPS65910_REG_ALARM_MONTHS	0x0C
#define TPS65910_REG_ALARM_YEARS	0x0D

#define TPS65910_REG_RTC_CTRL		0x10
#define TPS65910_REG_RTC_STATUS		0x11
#define TPS65910_REG_RTC_INTERRUPTS	0x12
#define TPS65910_REG_RTC_COMP_LSB	0x13
#define TPS65910_REG_RTC_COMP_MSB	0x14
#define TPS65910_REG_RTC_RES_PROG	0x15
#define TPS65910_REG_RTC_RESET_STATUS	0x16
#define TPS65910_REG_BCK1		0x17
#define TPS65910_REG_BCK2		0x18
#define TPS65910_REG_BCK3		0x19
#define TPS65910_REG_BCK4		0x1A
#define TPS65910_REG_BCK5		0x1B
#define TPS65910_REG_PUADEN		0x1C
#define TPS65910_REG_REF		0x1D
#define TPS65910_REG_VRTC		0x1E

#define TPS65910_REG_VIO		0x20
#define TPS65910_REG_VDD1		0x21
#define TPS65910_REG_VDD1_OP		0x22
#define TPS65910_REG_VDD1_SR		0x23
#define TPS65910_REG_VDD2		0x24
#define TPS65910_REG_VDD2_OP		0x25
#define TPS65910_REG_VDD2_SR		0x26
#define TPS65910_REG_VDD3		0x27

#define TPS65910_REG_VDIG1		0x30
#define TPS65910_REG_VDIG2		0x31
#define TPS65910_REG_VAUX1		0x32
#define TPS65910_REG_VAUX2		0x33
#define TPS65910_REG_VAUX33		0x34
#define TPS65910_REG_VMMC		0x35
#define TPS65910_REG_VPLL		0x36
#define TPS65910_REG_VDAC		0x37
#define TPS65910_REG_THERM		0x38
#define TPS65910_REG_BBCH		0x39

#define TPS65910_REG_DCDCCTRL		0x3E
#define TPS65910_REG_DEVCTRL		0x3F
#define TPS65910_REG_DEVCTRL2		0x40
#define TPS65910_REG_SLEEP_KEEP_LDO_ON	0x41
#define TPS65910_REG_SLEEP_KEEP_RES_ON	0x42
#define TPS65910_REG_SLEEP_SET_LDO_OFF	0x43
#define TPS65910_REG_SLEEP_SET_RES_OFF	0x44
#define TPS65910_REG_EN1_LDO_ASS	0x45
#define TPS65910_REG_EN1_SMPS_ASS	0x46
#define TPS65910_REG_EN2_LDO_ASS	0x47
#define TPS65910_REG_EN2_SMPS_ASS	0x48
#define TPS65910_REG_EN3_LDO_ASS	0x49
#define TPS65910_REG_SPARE		0x4A

#define TPS65910_REG_INT_STS		0x50
#define TPS65910_REG_INT_MSK		0x51
#define TPS65910_REG_INT_STS2		0x52
#define TPS65910_REG_INT_MSK2		0x53
#define TPS65910_REG_INT_STS3		0x54
#define TPS65910_REG_INT_MSK3		0x55

#define TPS65910_REG_GPIO0		0x60

#define TPS65910_REG_JTAGVERNUM		0x80

/* TPS65910 GPIO Specific flags */
#define TPS65910_GPIO_INT_FALLING	0
#define TPS65910_GPIO_INT_RISING	1

#define TPS65910_DEBOUNCE_91_5_MS	0
#define TPS65910_DEBOUNCE_150_MS	1

#define TPS65910_GPIO_PUDIS		(1 << 3)
#define TPS65910_GPIO_CFG_OUTPUT	(1 << 2)



/* TPS65910 Interrupt events */

/* RTC Driver */
#define TPS65910_RTC_ALARM_IT		0x80
#define TPS65910_RTC_PERIOD_IT		0x40

/*Core Driver */
#define TPS65910_HOT_DIE_IT		0x20
#define TPS65910_PWRHOLD_IT		0x10
#define TPS65910_PWRON_LP_IT		0x08
#define TPS65910_PWRON_IT		0x04
#define TPS65910_VMBHI_IT		0x02
#define TPS65910_VMBGCH_IT		0x01

/* GPIO driver */
#define TPS65910_GPIO_F_IT		0x02
#define TPS65910_GPIO_R_IT		0x01


#define TPS65910_VRTC_OFFMASK		(1<<3)

/* Back-up battery charger control */
#define TPS65910_BBCHEN			0x01

/* Back-up battery charger voltage */
#define TPS65910_BBSEL_3P0		0x00
#define TPS65910_BBSEL_2P52		0x02
#define TPS65910_BBSEL_3P15		0x04
#define TPS65910_BBSEL_VBAT		0x06

/* DEVCTRL_REG flags */
#define TPS65910_RTC_PWDNN		0x40
#define TPS65910_CK32K_CTRL		0x20
#define TPS65910_SR_CTL_I2C_SEL 	0x10
#define TPS65910_DEV_OFF_RST		0x08
#define TPS65910_DEV_ON			0x04
#define TPS65910_DEV_SLP		0x02
#define TPS65910_DEV_OFF		0x01

/* DEVCTRL2_REG flags */
#define TPS65910_DEV2_TSLOT_LENGTH	0x30
#define TPS65910_DEV2_SLEEPSIG_POL	0x08
#define TPS65910_DEV2_PWON_LP_OFF	0x04
#define TPS65910_DEV2_PWON_LP_RST	0x02
#define TPS65910_DEV2_IT_POL		0x01

/* TPS65910 SMPS/LDO's */
#define TPS65910_VIO			0
#define TPS65910_VDD1			1
#define TPS65910_VDD2			2
#define TPS65910_VDD3			3
/* LDOs */
#define TPS65910_VDIG1			4
#define TPS65910_VDIG2			5
#define TPS65910_VAUX33			6
#define TPS65910_VMMC			7
#define TPS65910_VAUX1			8
#define TPS65910_VAUX2			9
#define TPS65910_VDAC			10
#define TPS65910_VPLL			11
/* Internal LDO */
#define TPS65910_VRTC			12

/* Number of step-down/up converters available */
#define TPS65910_NUM_DCDC		4

/* Number of LDO voltage regulators  available */
#define TPS65910_NUM_LDO		9

/* Number of total regulators available */
#define TPS65910_NUM_REGULATOR  (TPS65910_NUM_DCDC + TPS65910_NUM_LDO)


/* Regulator Supply state */
#define SUPPLY_STATE_FLAG		0x03
/* OFF States */
#define TPS65910_REG_OFF_00		0x00
#define TPS65910_REG_OFF_10		0x02
/* OHP - on High Power */
#define TPS65910_REG_OHP		0x01
/* OLP - on Low Power */
#define TPS65910_REG_OLP		0x03

#define TPS65910_MAX_IRQS		10
#define TPS65910_VMBDCH_IRQ     	0
#define TPS65910_VMBHI_IRQ      	1
#define TPS65910_PWRON_IRQ      	2
#define TPS65910_PWRON_LP_IRQ   	3
#define TPS65910_PWRHOLD_IRQ    	4
#define TPS65910_HOTDIE_IRQ     	5
#define TPS65910_RTC_ALARM_IRQ  	6
#define TPS65910_RTC_PERIOD_IRQ 	7
#define TPS65910_GPIO0_R_IRQ    	8
#define TPS65910_GPIO0_F_IRQ    	9

/* TPS65910 has 1 GPIO  */
struct tps65910_gpio {
	u8      debounce;
	u8      pullup_pulldown;
	u8 	gpio_config;  /* Input or output */
	u8 	gpio_val;    /* Output value */
	int (*gpio_setup)(struct tps65910_gpio *pdata);
	int (*gpio_taredown)(struct tps65910_gpio *pdata);
};

struct tps65910_platform_data {

	unsigned irq_num;  /* TPS65910 to Host IRQ Number */
	struct tps65910_gpio	*gpio;

	/* plaform specific data to be initialised in board file */
	struct regulator_init_data *vio;
	struct regulator_init_data *vdd1;
	struct regulator_init_data *vdd2;
	struct regulator_init_data *vdd3;
	struct regulator_init_data *vdig1;
	struct regulator_init_data *vdig2;
	struct regulator_init_data *vaux33;
	struct regulator_init_data *vmmc;
	struct regulator_init_data *vaux1;
	struct regulator_init_data *vaux2;
	struct regulator_init_data *vdac;
	struct regulator_init_data *vpll;

	void  (*handlers[TPS65910_MAX_IRQS]) (void  *data);
	/* Configure TP65910 to board specific usage*/
	int (*board_tps65910_config)(struct tps65910_platform_data *pdata);
};

int tps65910_enable_bbch(u8 voltage);
int tps65910_disable_bbch(void);

int tps65910_remove_irq_work(int irq);
int tps65910_add_irq_work(int irq, void (*handler)(void *data));

int tps65910_i2c_write_u8(u8 slave_addr, u8 val, u8 reg);
int tps65910_i2c_read_u8(u8 slave_addr, u8 *val, u8 reg);

#endif /*  __LINUX_I2C_TPS65910_H */

