/*
* linux/arch/arm/mach-meson/include/mach/i2c.h
*/
#ifndef AML_MACH_I2C
#define AML_MACH_I2C
#include <mach/pinmux.h>
#include <mach/am_regs.h>
//#include <mach/regs.h>
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON3
//#include <mach/ao_regs.h>
#endif
#define MESON_I2C_MASTER_A_START		CBUS_REG_ADDR(I2C_M_0_CONTROL_REG)
#define MESON_I2C_MASTER_A_END		(CBUS_REG_ADDR(I2C_M_0_RDATA_REG1+1)-1)

#define MESON_I2C_MASTER_B_START		CBUS_REG_ADDR(I2C_M_1_CONTROL_REG)
#define MESON_I2C_MASTER_B_END		(CBUS_REG_ADDR(I2C_M_1_RDATA_REG1+1)-1)

#define MESON_I2C_SLAVE_START			CBUS_REG_ADDR(I2C_S_CONTROL_REG)
#define MESON_I2C_SLAVE_END			(CBUS_REG_ADDR(I2C_S_CNTL1_REG+1)-1)

#define MESON_I2C_MASTER_AO_START			AOBUS_REG_ADDR(AO_I2C_M_0_CONTROL_REG)
#define MESON_I2C_MASTER_AO_END			(AOBUS_REG_ADDR(AO_I2C_M_0_RDATA_REG1+1)-1)

#define AML_I2C_MASTER_AO			0
#define AML_I2C_MASTER_A			1
#define AML_I2C_MASTER_B 			2
#define AML_I2C_MASTER_C 			3
#define AML_I2C_MASTER_D 			4

#define AML_I2C_SLAVE_ADDR			0x6c
#if 0
/*M1 i2c pinmux
 *       I/O			I2C_MASTER_A		I2C_MASTER_B		I2C_SLAVE
 * GPIO_JTAG_TMS	SCK_A REG1[12]							SCK_A REG1[13]
 * GPIO_JTAG_TDI		SDA_A REG1[12]							SDA_A REG1[13]
 * GPIO_JTAG_TCK						SCK_B REG1[16]		SCK_A REG1[17]
 * GPIO_JTAG_TDO						SDA_B REG1[20]		SDA_A REG1[21]
 * GPIOB_0								SCK_B REG2[5]		SCK_A REG2[6]
 * GPIOB_1								SDA_B REG2[2]		SDA_A REG2[3]
 * GPIOB_2			SCK_A REGS[13]							SCK_A REG2[14]
 * GPIOB_3			SDA_A REG2[9]							SDA_A REG2[10]
 * GPIOC_13								SCK_B REG3[28]		SCK_A REG3[29]
 * GPIOC_14								SDA_B REG3[25]		SDA_A REG3[26]
 * GPIOC_21			SCK_A REG7[9]							SCK_A REG7[10]
 * GPIOC_22			SDA_A REG7[6]							SDA_A REG7[7]
 * GPIOE_16								SCK_B REG5[27]		SCK_A REG5[28]
 * GPIOE_17								SDA_B REG5[25]		SDA_A REG5[26]
*/

/*i2c master a*/

#define MESON_I2C_MASTER_GPIOX_26_REG		CBUS_REG_ADDR(PERIPHS_PIN_MUX_5)
#define MESON_I2C_MASTER_GPIOX_26_BIT		(1<<26)
#define MESON_I2C_MASTER_GPIOX_25_REG		CBUS_REG_ADDR(PERIPHS_PIN_MUX_5)
#define MESON_I2C_MASTER_GPIOX_25_BIT		(1<<27)

#define MESON_I2C_MASTER_GPIOX_28_REG  		CBUS_REG_ADDR(PERIPHS_PIN_MUX_5)
#define MESON_I2C_MASTER_GPIOX_28_BIT  		(1<<30)
#define MESON_I2C_MASTER_GPIOX_27_REG  		CBUS_REG_ADDR(PERIPHS_PIN_MUX_5)
#define MESON_I2C_MASTER_GPIOX_27_BIT  		(1<<31)

#define MESON_I2C_MASTER_GPIOAO_4_REG		AOBUS_REG_ADDR(AO_RTI_PIN_MUX_REG)
#define MESON_I2C_MASTER_GPIOAO_4_BIT		(1<<6)
#define MESON_I2C_MASTER_GPIOAO_5_REG		AOBUS_REG_ADDR(AO_RTI_PIN_MUX_REG)
#define MESON_I2C_MASTER_GPIOAO_5_BIT		(1<<5)

#endif

#define AML_I2C_SPPED_50K			50000
#define AML_I2C_SPPED_100K			100000
#define AML_I2C_SPPED_200K			200000
#define AML_I2C_SPPED_300K			300000
#define AML_I2C_SPPED_400K			400000
#if 0
struct aml_pinmux_reg_bit {
	unsigned int	scl_reg;
	unsigned int  			scl_bit;
	unsigned int	sda_reg;
	unsigned int  			sda_bit;
};
#endif

struct aml_i2c_platform{
	unsigned int		slave_addr;/*7bit addr*/
	unsigned int 		wait_count;/*i2c wait ack timeout =
											wait_count * wait_ack_interval */
	unsigned int 		wait_ack_interval;
	unsigned int 		wait_read_interval;
	unsigned int 		wait_xfer_interval;
	unsigned int 		master_no;
	unsigned int		master_i2c_speed;
	unsigned int		master_i2c_speed2;/*the same adapter, different speed*/
    /*reserved*/
	unsigned int		use_pio;/*0: hardware i2c, 1: manual pio i2c*/
#if 0
    struct aml_pinmux_reg_bit master_a_pinmux;
	struct aml_pinmux_reg_bit master_b_pinmux;
    struct aml_pinmux_reg_bit master_pinmux;
#endif
	pinmux_set_t  master_pinmux;
	pinmux_set_t  master_a_pinmux;
	pinmux_set_t  master_b_pinmux;
	const char *master_state_name;
	struct list_head list;
};

/**************i2c software gpio***************/

#define MESON_I2C_PREG_GPIOC_OE			CBUS_REG_ADDR(PREG_FGPIO_EN_N)
#define MESON_I2C_PREG_GPIOC_OUTLVL		CBUS_REG_ADDR(PREG_FGPIO_O)
#define MESON_I2C_PREG_GPIOC_INLVL		CBUS_REG_ADDR(PREG_FGPIO_I)

#define MESON_I2C_PREG_GPIOE_OE			CBUS_REG_ADDR(PREG_HGPIO_EN_N)
#define MESON_I2C_PREG_GPIOE_OUTLVL		CBUS_REG_ADDR(PREG_HGPIO_O)
#define MESON_I2C_PREG_GPIOE_INLVL		CBUS_REG_ADDR(PREG_HGPIO_I)

#define MESON_I2C_PREG_GPIOA_OE			CBUS_REG_ADDR(PREG_EGPIO_EN_N)
#define MESON_I2C_PREG_GPIOA_OUTLVL		CBUS_REG_ADDR(PREG_EGPIO_O)
#define MESON_I2C_PREG_GPIOA_INLVL		CBUS_REG_ADDR(PREG_EGPIO_I)

struct aml_sw_i2c_pins
{
	unsigned int scl_reg_out;
	unsigned int scl_reg_in;
	unsigned int scl_bit;
	unsigned int scl_oe;
	unsigned int sda_reg_out;
	unsigned int sda_reg_in;
	unsigned int sda_bit;
	unsigned int sda_oe;
};


struct aml_sw_i2c_platform {
	struct aml_sw_i2c_pins sw_pins;

	/* local settings */
	int udelay;		/* half clock cycle time in us,
				   minimum 2 us for fast-mode I2C,
				   minimum 5 us for standard-mode I2C and SMBus,
				   maximum 50 us for SMBus */
	int timeout;		/* in jiffies */
};

#endif

