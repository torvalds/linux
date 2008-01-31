/*
 * File:         arch/blackfin/kernel/bfin_gpio.h
 * Based on:
 * Author:	 Michael Hennerich (hennerich@blackfin.uclinux.org)
 *
 * Created:
 * Description:
 *
 * Modified:
 *               Copyright 2004-2008 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*
*  Number     BF537/6/4    BF561    BF533/2/1
*             BF527/5/2
*
*  GPIO_0       PF0         PF0        PF0
*  GPIO_1       PF1         PF1        PF1
*  GPIO_2       PF2         PF2        PF2
*  GPIO_3       PF3         PF3        PF3
*  GPIO_4       PF4         PF4        PF4
*  GPIO_5       PF5         PF5        PF5
*  GPIO_6       PF6         PF6        PF6
*  GPIO_7       PF7         PF7        PF7
*  GPIO_8       PF8         PF8        PF8
*  GPIO_9       PF9         PF9        PF9
*  GPIO_10      PF10        PF10       PF10
*  GPIO_11      PF11        PF11       PF11
*  GPIO_12      PF12        PF12       PF12
*  GPIO_13      PF13        PF13       PF13
*  GPIO_14      PF14        PF14       PF14
*  GPIO_15      PF15        PF15       PF15
*  GPIO_16      PG0         PF16
*  GPIO_17      PG1         PF17
*  GPIO_18      PG2         PF18
*  GPIO_19      PG3         PF19
*  GPIO_20      PG4         PF20
*  GPIO_21      PG5         PF21
*  GPIO_22      PG6         PF22
*  GPIO_23      PG7         PF23
*  GPIO_24      PG8         PF24
*  GPIO_25      PG9         PF25
*  GPIO_26      PG10        PF26
*  GPIO_27      PG11        PF27
*  GPIO_28      PG12        PF28
*  GPIO_29      PG13        PF29
*  GPIO_30      PG14        PF30
*  GPIO_31      PG15        PF31
*  GPIO_32      PH0         PF32
*  GPIO_33      PH1         PF33
*  GPIO_34      PH2         PF34
*  GPIO_35      PH3         PF35
*  GPIO_36      PH4         PF36
*  GPIO_37      PH5         PF37
*  GPIO_38      PH6         PF38
*  GPIO_39      PH7         PF39
*  GPIO_40      PH8         PF40
*  GPIO_41      PH9         PF41
*  GPIO_42      PH10        PF42
*  GPIO_43      PH11        PF43
*  GPIO_44      PH12        PF44
*  GPIO_45      PH13        PF45
*  GPIO_46      PH14        PF46
*  GPIO_47      PH15        PF47
*/

#ifndef __ARCH_BLACKFIN_GPIO_H__
#define __ARCH_BLACKFIN_GPIO_H__

#define gpio_bank(x) ((x) >> 4)
#define gpio_bit(x)  (1<<((x) & 0xF))
#define gpio_sub_n(x) ((x) & 0xF)

#define GPIO_BANKSIZE 16

#define	GPIO_0	0
#define	GPIO_1	1
#define	GPIO_2	2
#define	GPIO_3	3
#define	GPIO_4	4
#define	GPIO_5	5
#define	GPIO_6	6
#define	GPIO_7	7
#define	GPIO_8	8
#define	GPIO_9	9
#define	GPIO_10	10
#define	GPIO_11	11
#define	GPIO_12	12
#define	GPIO_13	13
#define	GPIO_14	14
#define	GPIO_15	15
#define	GPIO_16	16
#define	GPIO_17	17
#define	GPIO_18	18
#define	GPIO_19	19
#define	GPIO_20	20
#define	GPIO_21	21
#define	GPIO_22	22
#define	GPIO_23	23
#define	GPIO_24	24
#define	GPIO_25	25
#define	GPIO_26	26
#define	GPIO_27	27
#define	GPIO_28	28
#define	GPIO_29	29
#define	GPIO_30	30
#define	GPIO_31	31
#define	GPIO_32	32
#define	GPIO_33	33
#define	GPIO_34	34
#define	GPIO_35	35
#define	GPIO_36	36
#define	GPIO_37	37
#define	GPIO_38	38
#define	GPIO_39	39
#define	GPIO_40	40
#define	GPIO_41	41
#define	GPIO_42	42
#define	GPIO_43	43
#define	GPIO_44	44
#define	GPIO_45	45
#define	GPIO_46	46
#define	GPIO_47	47


#define PERIPHERAL_USAGE 1
#define GPIO_USAGE 0

#ifdef BF533_FAMILY
#define MAX_BLACKFIN_GPIOS 16

#define	GPIO_PF0	0
#define	GPIO_PF1	1
#define	GPIO_PF2	2
#define	GPIO_PF3	3
#define	GPIO_PF4	4
#define	GPIO_PF5	5
#define	GPIO_PF6	6
#define	GPIO_PF7	7
#define	GPIO_PF8	8
#define	GPIO_PF9	9
#define	GPIO_PF10	10
#define	GPIO_PF11	11
#define	GPIO_PF12	12
#define	GPIO_PF13	13
#define	GPIO_PF14	14
#define	GPIO_PF15	15

#endif

#if defined(BF527_FAMILY) || defined(BF537_FAMILY)
#define MAX_BLACKFIN_GPIOS 48

#define	GPIO_PF0	0
#define	GPIO_PF1	1
#define	GPIO_PF2	2
#define	GPIO_PF3	3
#define	GPIO_PF4	4
#define	GPIO_PF5	5
#define	GPIO_PF6	6
#define	GPIO_PF7	7
#define	GPIO_PF8	8
#define	GPIO_PF9	9
#define	GPIO_PF10	10
#define	GPIO_PF11	11
#define	GPIO_PF12	12
#define	GPIO_PF13	13
#define	GPIO_PF14	14
#define	GPIO_PF15	15
#define	GPIO_PG0	16
#define	GPIO_PG1	17
#define	GPIO_PG2	18
#define	GPIO_PG3	19
#define	GPIO_PG4	20
#define	GPIO_PG5	21
#define	GPIO_PG6	22
#define	GPIO_PG7	23
#define	GPIO_PG8	24
#define	GPIO_PG9	25
#define	GPIO_PG10      	26
#define	GPIO_PG11      	27
#define	GPIO_PG12      	28
#define	GPIO_PG13      	29
#define	GPIO_PG14      	30
#define	GPIO_PG15      	31
#define	GPIO_PH0	32
#define	GPIO_PH1	33
#define	GPIO_PH2	34
#define	GPIO_PH3	35
#define	GPIO_PH4	36
#define	GPIO_PH5	37
#define	GPIO_PH6	38
#define	GPIO_PH7	39
#define	GPIO_PH8	40
#define	GPIO_PH9	41
#define	GPIO_PH10      	42
#define	GPIO_PH11      	43
#define	GPIO_PH12      	44
#define	GPIO_PH13      	45
#define	GPIO_PH14      	46
#define	GPIO_PH15      	47

#define PORT_F GPIO_PF0
#define PORT_G GPIO_PG0
#define PORT_H GPIO_PH0

#endif

#ifdef BF548_FAMILY
#include <asm-blackfin/mach-bf548/gpio.h>
#endif

#ifdef BF561_FAMILY
#define MAX_BLACKFIN_GPIOS 48

#define	GPIO_PF0	0
#define	GPIO_PF1	1
#define	GPIO_PF2	2
#define	GPIO_PF3	3
#define	GPIO_PF4	4
#define	GPIO_PF5	5
#define	GPIO_PF6	6
#define	GPIO_PF7	7
#define	GPIO_PF8	8
#define	GPIO_PF9	9
#define	GPIO_PF10	10
#define	GPIO_PF11	11
#define	GPIO_PF12	12
#define	GPIO_PF13	13
#define	GPIO_PF14	14
#define	GPIO_PF15	15
#define	GPIO_PF16	16
#define	GPIO_PF17	17
#define	GPIO_PF18	18
#define	GPIO_PF19	19
#define	GPIO_PF20	20
#define	GPIO_PF21	21
#define	GPIO_PF22	22
#define	GPIO_PF23	23
#define	GPIO_PF24	24
#define	GPIO_PF25	25
#define	GPIO_PF26	26
#define	GPIO_PF27	27
#define	GPIO_PF28	28
#define	GPIO_PF29	29
#define	GPIO_PF30	30
#define	GPIO_PF31	31
#define	GPIO_PF32	32
#define	GPIO_PF33	33
#define	GPIO_PF34	34
#define	GPIO_PF35	35
#define	GPIO_PF36	36
#define	GPIO_PF37	37
#define	GPIO_PF38	38
#define	GPIO_PF39	39
#define	GPIO_PF40	40
#define	GPIO_PF41	41
#define	GPIO_PF42	42
#define	GPIO_PF43	43
#define	GPIO_PF44	44
#define	GPIO_PF45	45
#define	GPIO_PF46	46
#define	GPIO_PF47	47

#define PORT_FIO0 GPIO_0
#define PORT_FIO1 GPIO_16
#define PORT_FIO2 GPIO_32
#endif

#ifndef __ASSEMBLY__

/***********************************************************
*
* FUNCTIONS: Blackfin General Purpose Ports Access Functions
*
* INPUTS/OUTPUTS:
* gpio - GPIO Number between 0 and MAX_BLACKFIN_GPIOS
*
*
* DESCRIPTION: These functions abstract direct register access
*              to Blackfin processor General Purpose
*              Ports Regsiters
*
* CAUTION: These functions do not belong to the GPIO Driver API
*************************************************************
* MODIFICATION HISTORY :
**************************************************************/

#ifndef BF548_FAMILY
void set_gpio_dir(unsigned, unsigned short);
void set_gpio_inen(unsigned, unsigned short);
void set_gpio_polar(unsigned, unsigned short);
void set_gpio_edge(unsigned, unsigned short);
void set_gpio_both(unsigned, unsigned short);
void set_gpio_data(unsigned, unsigned short);
void set_gpio_maska(unsigned, unsigned short);
void set_gpio_maskb(unsigned, unsigned short);
void set_gpio_toggle(unsigned);
void set_gpiop_dir(unsigned, unsigned short);
void set_gpiop_inen(unsigned, unsigned short);
void set_gpiop_polar(unsigned, unsigned short);
void set_gpiop_edge(unsigned, unsigned short);
void set_gpiop_both(unsigned, unsigned short);
void set_gpiop_data(unsigned, unsigned short);
void set_gpiop_maska(unsigned, unsigned short);
void set_gpiop_maskb(unsigned, unsigned short);
unsigned short get_gpio_dir(unsigned);
unsigned short get_gpio_inen(unsigned);
unsigned short get_gpio_polar(unsigned);
unsigned short get_gpio_edge(unsigned);
unsigned short get_gpio_both(unsigned);
unsigned short get_gpio_maska(unsigned);
unsigned short get_gpio_maskb(unsigned);
unsigned short get_gpio_data(unsigned);
unsigned short get_gpiop_dir(unsigned);
unsigned short get_gpiop_inen(unsigned);
unsigned short get_gpiop_polar(unsigned);
unsigned short get_gpiop_edge(unsigned);
unsigned short get_gpiop_both(unsigned);
unsigned short get_gpiop_maska(unsigned);
unsigned short get_gpiop_maskb(unsigned);
unsigned short get_gpiop_data(unsigned);

struct gpio_port_t {
	unsigned short data;
	unsigned short dummy1;
	unsigned short data_clear;
	unsigned short dummy2;
	unsigned short data_set;
	unsigned short dummy3;
	unsigned short toggle;
	unsigned short dummy4;
	unsigned short maska;
	unsigned short dummy5;
	unsigned short maska_clear;
	unsigned short dummy6;
	unsigned short maska_set;
	unsigned short dummy7;
	unsigned short maska_toggle;
	unsigned short dummy8;
	unsigned short maskb;
	unsigned short dummy9;
	unsigned short maskb_clear;
	unsigned short dummy10;
	unsigned short maskb_set;
	unsigned short dummy11;
	unsigned short maskb_toggle;
	unsigned short dummy12;
	unsigned short dir;
	unsigned short dummy13;
	unsigned short polar;
	unsigned short dummy14;
	unsigned short edge;
	unsigned short dummy15;
	unsigned short both;
	unsigned short dummy16;
	unsigned short inen;
};
#endif

#ifdef CONFIG_PM
#define PM_WAKE_RISING	0x1
#define PM_WAKE_FALLING	0x2
#define PM_WAKE_HIGH	0x4
#define PM_WAKE_LOW	0x8
#define PM_WAKE_BOTH_EDGES	(PM_WAKE_RISING | PM_WAKE_FALLING)

int gpio_pm_wakeup_request(unsigned gpio, unsigned char type);
void gpio_pm_wakeup_free(unsigned gpio);
unsigned int gpio_pm_setup(void);
void gpio_pm_restore(void);

struct gpio_port_s {
	unsigned short data;
	unsigned short data_clear;
	unsigned short data_set;
	unsigned short toggle;
	unsigned short maska;
	unsigned short maska_clear;
	unsigned short maska_set;
	unsigned short maska_toggle;
	unsigned short maskb;
	unsigned short maskb_clear;
	unsigned short maskb_set;
	unsigned short maskb_toggle;
	unsigned short dir;
	unsigned short polar;
	unsigned short edge;
	unsigned short both;
	unsigned short inen;

	unsigned short fer;
	unsigned short reserved;
};
#endif /*CONFIG_PM*/

/***********************************************************
*
* FUNCTIONS: Blackfin GPIO Driver
*
* INPUTS/OUTPUTS:
* gpio - GPIO Number between 0 and MAX_BLACKFIN_GPIOS
*
*
* DESCRIPTION: Blackfin GPIO Driver API
*
* CAUTION:
*************************************************************
* MODIFICATION HISTORY :
**************************************************************/

int gpio_request(unsigned, const char *);
void gpio_free(unsigned);

void gpio_set_value(unsigned gpio, int arg);
int gpio_get_value(unsigned gpio);

#ifndef BF548_FAMILY
#define gpio_get_value(gpio) 		get_gpio_data(gpio)
#define gpio_set_value(gpio, value)	set_gpio_data(gpio, value)
#endif

int gpio_direction_input(unsigned gpio);
int gpio_direction_output(unsigned gpio, int value);

#include <asm-generic/gpio.h>		/* cansleep wrappers */
#include <asm/irq.h>

static inline int gpio_to_irq(unsigned gpio)
{
	return (gpio + GPIO_IRQ_BASE);
}

static inline int irq_to_gpio(unsigned irq)
{
	return (irq - GPIO_IRQ_BASE);
}

#endif /* __ASSEMBLY__ */

#endif /* __ARCH_BLACKFIN_GPIO_H__ */
