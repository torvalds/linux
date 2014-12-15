#include <linux/gpio.h>
#include <mach/gpio.h>
#ifndef __AML_GPIO_CONSUMER_H__
#define __AML_GPIO_CONSUMER_H__
extern int amlogic_gpio_request(unsigned int  pin,const char *label);
extern int amlogic_gpio_request_one(unsigned pin, unsigned long flags, const char *label);
extern int amlogic_gpio_request_array(const struct gpio *array, size_t num);
extern int amlogic_gpio_free_array(const struct gpio *array, size_t num);
extern int amlogic_gpio_direction_input(unsigned int pin,const char *owner);
extern int amlogic_gpio_direction_output(unsigned int pin,int value,const char *owner);
extern int amlogic_gpio_free(unsigned int  pin,const char * label);
extern int amlogic_request_gpio_to_irq(unsigned int  pin,const char *label,unsigned int flag);
extern int amlogic_gpio_to_irq(unsigned int  pin,const char *owner,unsigned int flag);
extern int amlogic_get_value(unsigned int pin,const char *owner);
extern int amlogic_set_value(unsigned int pin,int value,const char *owner);
extern int amlogic_gpio_name_map_num(const char *name);
extern int amlogic_set_pull_up_down(unsigned int pin,unsigned int val,const char *owner);
extern int amlogic_disable_pullup(unsigned int pin,const char *owner);
#define AML_GPIO_IRQ(irq_bank,filter,type) ((irq_bank&0x7)|(filter&0x7)<<8|(type&0x3)<<16)
#endif

