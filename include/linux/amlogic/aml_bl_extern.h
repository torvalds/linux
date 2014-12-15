
#ifndef __AMLOGIC_BL_EXTERN_H_
#define __AMLOGIC_BL_EXTERN_H_

#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/pinctrl/consumer.h>

typedef enum {
	BL_EXTERN_I2C = 0,
	BL_EXTERN_SPI,
	BL_EXTERN_OTHER,//panel, pmu, etc...
	BL_EXTERN_MAX,
} Bl_Extern_Type_t;

//global API
struct aml_bl_extern_driver_t {
	const char *name;
	Bl_Extern_Type_t type;
	int (*power_on) (void);
	int (*power_off)(void);
	int (*set_level)(unsigned int level);
};

struct bl_extern_config_t {
	const char *name;
	Bl_Extern_Type_t type;
	unsigned int gpio_used;
	int gpio;
	int i2c_addr;
	int i2c_bus;
	int spi_cs;
	int spi_clk;
	int spi_data;
	unsigned int dim_min;
	unsigned int dim_max;
	unsigned int level_min;
	unsigned int level_max;
};

#define BL_EXTERN_DRIVER		"bl_extern"

#define bl_extern_gpio_request(gpio)                amlogic_gpio_request(gpio, BL_EXTERN_DRIVER)
#define bl_extern_gpio_free(gpio)                   amlogic_gpio_free(gpio, BL_EXTERN_DRIVER)
#define bl_extern_gpio_direction_input(gpio)        amlogic_gpio_direction_input(gpio, BL_EXTERN_DRIVER)
#define bl_extern_gpio_direction_output(gpio, val)  amlogic_gpio_direction_output(gpio, val, BL_EXTERN_DRIVER)
#define bl_extern_gpio_get_value(gpio)              amlogic_get_value(gpio, BL_EXTERN_DRIVER)
#define bl_extern_gpio_set_value(gpio,val)          amlogic_set_value(gpio, val, BL_EXTERN_DRIVER)

extern struct aml_bl_extern_driver_t* aml_bl_extern_get_driver(void);
extern int bl_extern_driver_check(void);
extern int get_bl_extern_dt_data(struct device_node* of_node, struct bl_extern_config_t *pdata);

extern void get_bl_ext_level(struct bl_extern_config_t *bl_ext_cfg);

#endif

