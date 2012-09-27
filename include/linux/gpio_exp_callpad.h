#ifndef GPIO_EXP_CALLPAD_H
#define GPIO_EXP_CALLPAD_H

#define GPIOEXP_INT_TRIGGER_FALLING		0x01
#define GPIOEXP_INT_TRIGGER_RISING		0x02
#define GPIOEXP_INT_TRIGGER_MASK			(GPIOEXP_INT_TRIGGER_FALLING | GPIOEXP_INT_TRIGGER_RISING)

enum gpioexp_port_pin_num
{
	//P0
	GPIOEXP_P0_0 = 0,
	GPIOEXP_P0_1,
	GPIOEXP_P0_2,
	GPIOEXP_P0_3,
	GPIOEXP_P0_4,
	GPIOEXP_P0_5,
	GPIOEXP_P0_6,
	GPIOEXP_P0_7,
	
	//P1
	GPIOEXP_P1_0,
	GPIOEXP_P1_1,
	GPIOEXP_P1_2,
	GPIOEXP_P1_3,
	GPIOEXP_P1_4,
	GPIOEXP_P1_5,
	GPIOEXP_P1_6,
	GPIOEXP_P1_7,
	GPIOEXP_PIN_MAX,
};

typedef void (*gpioexp_int_handler_t)(void *data);


extern int gpioexp_set_direction(unsigned gpio, int is_in);
extern int gpioexp_set_output_level(unsigned gpio, int value);
extern int gpioexp_read_input_level(unsigned gpio);
extern int gpioexp_request_irq(unsigned int irq, gpioexp_int_handler_t handler, unsigned long flags);
extern int gpioexp_free_irq(unsigned int irq);
extern int gpioexp_enable_irq(unsigned int irq);
extern int gpioexp_disable_irq(unsigned int irq);
	
#endif
