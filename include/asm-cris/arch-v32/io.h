#ifndef _ASM_ARCH_CRIS_IO_H
#define _ASM_ARCH_CRIS_IO_H

#include <asm/arch/hwregs/reg_map.h>
#include <asm/arch/hwregs/reg_rdwr.h>
#include <asm/arch/hwregs/gio_defs.h>
#include <linux/config.h>

enum crisv32_io_dir
{
  crisv32_io_dir_in = 0,
  crisv32_io_dir_out = 1
};

struct crisv32_ioport
{
  unsigned long* oe;
  unsigned long* data;
  unsigned long* data_in;
  unsigned int pin_count;
};

struct crisv32_iopin
{
  struct crisv32_ioport* port;
  int bit;
};

extern struct crisv32_ioport crisv32_ioports[];

extern struct crisv32_iopin crisv32_led1_green;
extern struct crisv32_iopin crisv32_led1_red;
extern struct crisv32_iopin crisv32_led2_green;
extern struct crisv32_iopin crisv32_led2_red;
extern struct crisv32_iopin crisv32_led3_green;
extern struct crisv32_iopin crisv32_led3_red;

extern inline void crisv32_io_set(struct crisv32_iopin* iopin,
			   int val)
{
	if (val)
		*iopin->port->data |= iopin->bit;
	else
		*iopin->port->data &= ~iopin->bit;
}

extern inline void crisv32_io_set_dir(struct crisv32_iopin* iopin,
			       enum crisv32_io_dir dir)
{
	if (dir == crisv32_io_dir_in)
		*iopin->port->oe &= ~iopin->bit;
	else
		*iopin->port->oe |= iopin->bit;
}

extern inline int crisv32_io_rd(struct crisv32_iopin* iopin)
{
	return ((*iopin->port->data_in & iopin->bit) ? 1 : 0);
}

int crisv32_io_get(struct crisv32_iopin* iopin,
                   unsigned int port, unsigned int pin);
int crisv32_io_get_name(struct crisv32_iopin* iopin,
                         char* name);

#define LED_OFF    0x00
#define LED_GREEN  0x01
#define LED_RED    0x02
#define LED_ORANGE (LED_GREEN | LED_RED)

#define LED_NETWORK_SET(x)                          \
	do {                                        \
		LED_NETWORK_SET_G((x) & LED_GREEN); \
		LED_NETWORK_SET_R((x) & LED_RED);   \
	} while (0)
#define LED_ACTIVE_SET(x)                           \
	do {                                        \
		LED_ACTIVE_SET_G((x) & LED_GREEN);  \
		LED_ACTIVE_SET_R((x) & LED_RED);    \
	} while (0)

#define LED_NETWORK_SET_G(x) \
	crisv32_io_set(&crisv32_led1_green, !(x));
#define LED_NETWORK_SET_R(x) \
	crisv32_io_set(&crisv32_led1_red, !(x));
#define LED_ACTIVE_SET_G(x) \
	crisv32_io_set(&crisv32_led2_green, !(x));
#define LED_ACTIVE_SET_R(x) \
	crisv32_io_set(&crisv32_led2_red, !(x));
#define LED_DISK_WRITE(x) \
         do{\
		crisv32_io_set(&crisv32_led3_green, !(x)); \
		crisv32_io_set(&crisv32_led3_red, !(x));   \
        }while(0)
#define LED_DISK_READ(x) \
	crisv32_io_set(&crisv32_led3_green, !(x));

#endif
