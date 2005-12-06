#ifndef __AU1XXX_GPIO_H
#define __AU1XXX_GPIO_H

void au1xxx_gpio1_set_inputs(void);
void au1xxx_gpio_tristate(int signal);
void au1xxx_gpio_write(int signal, int value);
int  au1xxx_gpio_read(int signal);

typedef volatile struct
{
	u32 dir;
	u32 reserved;
	u32 output;
	u32 pinstate;
	u32 inten;
	u32 enable;

} AU1X00_GPIO2;

#endif //__AU1XXX_GPIO_H
