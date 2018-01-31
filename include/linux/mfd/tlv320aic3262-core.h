/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MFD_AIC3262_CORE_H__
#define __MFD_AIC3262_CORE_H__

#include <linux/interrupt.h>
#include <linux/mfd/core.h>
enum aic3262_type {
	TLV320AIC3262 = 0,
};


#define AIC3262_IRQ_HEADSET_DETECT	0
#define AIC3262_IRQ_BUTTON_PRESS	1
#define AIC3262_IRQ_DAC_DRC		2
#define AIC3262_IRQ_AGC_NOISE		3
#define AIC3262_IRQ_OVER_CURRENT	4
#define AIC3262_IRQ_OVERFLOW_EVENT	5
#define AIC3262_IRQ_SPEAKER_OVER_TEMP 	6

#define AIC3262_GPIO1			7
#define AIC3262_GPIO2			8
#define AIC3262_GPI1			9
#define AIC3262_GPI2			10
#define AIC3262_GPO1			11

typedef union aic326x_reg_union {
	struct aic326x_reg{
	u8 offset;
	u8 page;
	u8 book;
	u8 reserved;
	}aic326x_register;
	unsigned int aic326x_register_int;
}aic326x_reg_union;


/****************************             ************************************/

/*
 *****************************************************************************
 * Structures Definitions
 *****************************************************************************
 */
/*
 *----------------------------------------------------------------------------
 * @struct  aic3262_setup_data |
 *          i2c specific data setup for AIC3262.
 * @field   unsigned short |i2c_address |
 *          Unsigned short for i2c address.
 *----------------------------------------------------------------------------
 */
struct aic3262_setup_data {
	unsigned short i2c_address;
};

/* GPIO API */                                                                                                                                              
#define AIC3262_NUM_GPIO 5 // include 2 GPI and 1 GPO pins
enum {                                                                                                                                                      
        AIC3262_GPIO1_FUNC_DISABLED             = 0,                                                                                                        
        AIC3262_GPIO1_FUNC_INPUT		= 1,                                                                                                        
        AIC3262_GPIO1_FUNC_OUTPUT	        = 3,                                                                                                        
        AIC3262_GPIO1_FUNC_CLOCK_OUTPUT	        = 4,
        AIC3262_GPIO1_FUNC_INT1_OUTPUT		= 5,
        AIC3262_GPIO1_FUNC_INT2_OUTPUT	        = 6,
        AIC3262_GPIO1_FUNC_ADC_MOD_CLK_OUTPUT   = 10,
        AIC3262_GPIO1_FUNC_SAR_ADC_INTERRUPT    = 12,
        AIC3262_GPIO1_FUNC_ASI1_DATA_OUTPUT     = 15,
        AIC3262_GPIO1_FUNC_ASI1_WCLK     	= 16,
        AIC3262_GPIO1_FUNC_ASI1_BCLK         	= 17,
        AIC3262_GPIO1_FUNC_ASI2_WCLK     	= 18,
        AIC3262_GPIO1_FUNC_ASI2_BCLK         	= 19,
        AIC3262_GPIO1_FUNC_ASI3_WCLK     	= 20,
        AIC3262_GPIO1_FUNC_ASI3_BCLK         	= 21

};                                                                                                                                                          
                                                                                                                                                            
enum {                                                                                                                                                      
        AIC3262_GPIO2_FUNC_DISABLED             = 0,                                                                                                        
        AIC3262_GPIO2_FUNC_INPUT		= 1,                                                                                                        
        AIC3262_GPIO2_FUNC_OUTPUT	        = 3,                                                                                                        
        AIC3262_GPIO2_FUNC_CLOCK_OUTPUT	        = 4,
        AIC3262_GPIO2_FUNC_INT1_OUTPUT		= 5,
        AIC3262_GPIO2_FUNC_INT2_OUTPUT	        = 6,
        AIC3262_GPIO2_FUNC_ADC_MOD_CLK_OUTPUT   = 10,
        AIC3262_GPIO2_FUNC_SAR_ADC_INTERRUPT    = 12,
        AIC3262_GPIO2_FUNC_ASI1_DATA_OUTPUT     = 15,
        AIC3262_GPIO2_FUNC_ASI1_WCLK     	= 16,
        AIC3262_GPIO2_FUNC_ASI1_BCLK         	= 17,
        AIC3262_GPIO2_FUNC_ASI2_WCLK     	= 18,
        AIC3262_GPIO2_FUNC_ASI2_BCLK         	= 19,
        AIC3262_GPIO2_FUNC_ASI3_WCLK     	= 20,
        AIC3262_GPIO2_FUNC_ASI3_BCLK         	= 21
};                                   
enum {                                                                                                                                                      
        AIC3262_GPO1_FUNC_DISABLED             	= 0,                                                                                                        
        AIC3262_GPO1_FUNC_MSO_OUTPUT_FOR_SPI	= 1,                                                                                                        
        AIC3262_GPO1_FUNC_GENERAL_PURPOSE_OUTPUT= 2,
        AIC3262_GPO1_FUNC_CLOCK_OUTPUT	        = 3,
        AIC3262_GPO1_FUNC_INT1_OUTPUT		= 4,
        AIC3262_GPO1_FUNC_INT2_OUTPUT	        = 5,
       	AIC3262_GPO1_FUNC_ADC_MOD_CLK_OUTPUT   = 7,
        AIC3262_GPO1_FUNC_SAR_ADC_INTERRUPT    = 12,
        AIC3262_GPO1_FUNC_ASI1_DATA_OUTPUT     = 15,
};                                   
/*
 *----------------------------------------------------------------------------
 * @struct  aic3262_configs |
 *          AIC3262 initialization data which has register offset and register
 *          value.
 * @field   u8 | book_no |
 *          AIC3262 Book Number Offsets required for initialization..
 * @field   u16 | reg_offset |
 *          AIC3262 Register offsets required for initialization..
 * @field   u8 | reg_val |
 *          value to set the AIC3262 register to initialize the AIC3262.
 *----------------------------------------------------------------------------
 */
struct aic3262_configs {
	u8 book_no;
    	u16 reg_offset;
	u8  reg_val;
};

/*
 *----------------------------------------------------------------------------
 * @struct  aic3262_rate_divs |
 *          Setting up the values to get different freqencies
 *
 * @field   u32 | mclk |
 *          Master clock
 * @field   u32 | rate |
 *          sample rate
 * @field   u8 | p_val |
 *          value of p in PLL
 * @field   u32 | pll_j |
 *          value for pll_j
 * @field   u32 | pll_d |
 *          value for pll_d
 * @field   u32 | dosr |
 *          value to store dosr
 * @field   u32 | ndac |
 *          value for ndac
 * @field   u32 | mdac |
 *          value for mdac
 * @field   u32 | aosr |
 *          value for aosr
 * @field   u32 | nadc |
 *          value for nadc
 * @field   u32 | madc |
 *          value for madc
 * @field   u32 | blck_N |
 *          value for block N
 */
struct aic3262 {
	struct mutex io_lock;
	struct mutex irq_lock;

	enum aic3262_type type;

	struct device *dev;
	int (*read_dev)(struct aic3262 *aic3262, unsigned int reg,
			int bytes, void *dest);
	int (*write_dev)(struct aic3262 *aic3262, unsigned int reg,
			 int bytes, const void *src);

	void *control_data;

//	int gpio_base;
	
	unsigned int irq;
	unsigned int irq_base;

	u8 irq_masks_cur;
	u8 irq_masks_cache;

	/* Used over suspend/resume */
	bool suspended;
		
    	u8 book_no;
	u8 page_no;
};

struct aic3262_gpio_setup 
{
	u8 used; // GPIO, GPI and GPO is used in the board, used = 1 else 0
	u8 in; // GPIO is used as input, in = 1 else in = 0. GPI in = 1, GPO in = 0
	unsigned int in_reg; // if GPIO is input, register to write the mask to.
	u8 in_reg_bitmask; // bitmask for 'value' to be written into in_reg
	u8 in_reg_shift; // bits to shift to write 'value' into in_reg
	u8 value; // value to be written gpio_control_reg if GPIO is output, in_reg if its input	
}; 
struct aic3262_pdata {
	unsigned int audio_mclk1; 
	unsigned int audio_mclk2; 
	unsigned int gpio_irq; /* whether AIC3262 interrupts the host AP on a GPIO pin of AP */ 
	unsigned int gpio_reset; /* is the codec being reset by a gpio [host] pin, if yes provide the number. */
	struct aic3262_gpio_setup *gpio;/* all gpio configuration */
	int naudint_irq; /* audio interrupt */
	unsigned int irq_base;
};
		


static inline int aic3262_request_irq(struct aic3262 *aic3262, int irq,
				      irq_handler_t handler, unsigned long irqflags,const char *name,
				      void *data)
{
	if (!aic3262->irq_base)
		return -EINVAL;

	return request_threaded_irq(aic3262->irq_base + irq, NULL, handler,
				    irqflags, name, data);
}

static inline void aic3262_free_irq(struct aic3262 *aic3262, int irq,
				    void *data)
{
	if (!aic3262->irq_base)
		return;

	free_irq(aic3262->irq_base + irq, data);
}

/* Device I/O API */
int aic3262_reg_read(struct aic3262 *aic3262, unsigned int reg);
int aic3262_reg_write(struct aic3262 *aic3262, unsigned int reg,
		 unsigned char val);
int aic3262_set_bits(struct aic3262 *aic3262, unsigned int reg,
		    unsigned char mask, unsigned char val);
int aic3262_bulk_read(struct aic3262 *aic3262, unsigned int reg,
		     int count, u8 *buf);
int aic3262_bulk_write(struct aic3262 *aic3262, unsigned int reg,
		     int count, const u8 *buf);


/* Helper to save on boilerplate */
/*static inline int aic3262_request_irq(struct aic3262 *aic3262, int irq,
				     irq_handler_t handler, const char *name,
				     void *data)
{
	if (!aic3262->irq_base)
		return -EINVAL;
	return request_threaded_irq(aic3262->irq_base + irq, NULL, handler,
				    IRQF_TRIGGER_RISING, name,
				    data);
}
static inline void aic3262_free_irq(struct aic3262 *aic3262, int irq, void *data)
{
	if (!aic3262->irq_base)
		return;
	free_irq(aic3262->irq_base + irq, data);
}
*/
int aic3262_irq_init(struct aic3262 *aic3262);
void aic3262_irq_exit(struct aic3262 *aic3262);

#endif
