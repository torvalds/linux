
#ifndef __AMLOGIC_LCD_EXTERN_H_
#define __AMLOGIC_LCD_EXTERN_H_

#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/pinctrl/consumer.h>

typedef enum {
	LCD_EXTERN_I2C = 0,
	LCD_EXTERN_SPI,
	LCD_EXTERN_MIPI,
	LCD_EXTERN_MAX,
} Lcd_Extern_Type_t;

//global API
struct aml_lcd_extern_driver_t {
	char *name;
	Lcd_Extern_Type_t type;
	int (*reg_read)  (unsigned char reg, unsigned char *buf);
	int (*reg_write) (unsigned char reg, unsigned char value);
	int (*power_on)(void);
	int (*power_off)(void);
	unsigned char *init_on_cmd_8;
	unsigned char *init_off_cmd_8;
	//unsigned short *init_on_cmd_16;
	//unsigned short *init_off_cmd_16;
};

struct lcd_extern_config_t {
	char *name;
	Lcd_Extern_Type_t type;
	int status;
	int i2c_addr;
	int i2c_bus;
	int spi_cs;
	int spi_clk;
	int spi_data;
};

#define LCD_EXTERN_DRIVER		"lcd_extern"

#define lcd_extern_gpio_request(gpio) 				amlogic_gpio_request(gpio, LCD_EXTERN_DRIVER)
#define lcd_extern_gpio_free(gpio) 					amlogic_gpio_free(gpio, LCD_EXTERN_DRIVER)
#define lcd_extern_gpio_direction_input(gpio) 		amlogic_gpio_direction_input(gpio, LCD_EXTERN_DRIVER)
#define lcd_extern_gpio_direction_output(gpio, val) amlogic_gpio_direction_output(gpio, val, LCD_EXTERN_DRIVER)
#define lcd_extern_gpio_get_value(gpio) 			amlogic_get_value(gpio, LCD_EXTERN_DRIVER)
#define lcd_extern_gpio_set_value(gpio,val) 		amlogic_set_value(gpio, val, LCD_EXTERN_DRIVER)

extern struct aml_lcd_extern_driver_t* aml_lcd_extern_get_driver(void);
extern int lcd_extern_driver_check(void);
extern int get_lcd_extern_dt_data(struct device_node* of_node, struct lcd_extern_config_t *pdata);
extern int remove_lcd_extern(struct lcd_extern_config_t *pdata);

#endif

