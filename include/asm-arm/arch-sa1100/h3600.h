/*
 *
 * Definitions for H3600 Handheld Computer
 *
 * Copyright 2000 Compaq Computer Corporation.
 *
 * Use consistent with the GNU GPL is permitted,
 * provided that this copyright notice is
 * preserved in its entirety in all copies and derived works.
 *
 * COMPAQ COMPUTER CORPORATION MAKES NO WARRANTIES, EXPRESSED OR IMPLIED,
 * AS TO THE USEFULNESS OR CORRECTNESS OF THIS CODE OR ITS
 * FITNESS FOR ANY PARTICULAR PURPOSE.
 *
 * Author: Jamey Hicks.
 *
 * History:
 *
 * 2001-10-??	Andrew Christian   Added support for iPAQ H3800
 *
 */

#ifndef _INCLUDE_H3600_H_
#define _INCLUDE_H3600_H_

/* generalized support for H3xxx series Compaq Pocket PC's */
#define machine_is_h3xxx() (machine_is_h3100() || machine_is_h3600() || machine_is_h3800())

/* Physical memory regions corresponding to chip selects */
#define H3600_EGPIO_PHYS     (SA1100_CS5_PHYS + 0x01000000)
#define H3600_BANK_2_PHYS    SA1100_CS2_PHYS
#define H3600_BANK_4_PHYS    SA1100_CS4_PHYS

/* Virtual memory regions corresponding to chip selects 2 & 4 (used on sleeves) */
#define H3600_EGPIO_VIRT     0xf0000000
#define H3600_BANK_2_VIRT    0xf1000000
#define H3600_BANK_4_VIRT    0xf3800000

/*
   Machine-independent GPIO definitions
   --- these are common across all current iPAQ platforms
*/

#define GPIO_H3600_NPOWER_BUTTON	GPIO_GPIO (0)	/* Also known as the "off button"  */

#define GPIO_H3600_PCMCIA_CD1		GPIO_GPIO (10)
#define GPIO_H3600_PCMCIA_IRQ1		GPIO_GPIO (11)

/* UDA1341 L3 Interface */
#define GPIO_H3600_L3_DATA		GPIO_GPIO (14)
#define GPIO_H3600_L3_MODE		GPIO_GPIO (15)
#define GPIO_H3600_L3_CLOCK		GPIO_GPIO (16)

#define GPIO_H3600_PCMCIA_CD0		GPIO_GPIO (17)
#define GPIO_H3600_SYS_CLK		GPIO_GPIO (19)
#define GPIO_H3600_PCMCIA_IRQ0		GPIO_GPIO (21)

#define GPIO_H3600_COM_DCD		GPIO_GPIO (23)
#define GPIO_H3600_OPT_IRQ		GPIO_GPIO (24)
#define GPIO_H3600_COM_CTS		GPIO_GPIO (25)
#define GPIO_H3600_COM_RTS		GPIO_GPIO (26)

#define IRQ_GPIO_H3600_NPOWER_BUTTON	IRQ_GPIO0
#define IRQ_GPIO_H3600_PCMCIA_CD1	IRQ_GPIO10
#define IRQ_GPIO_H3600_PCMCIA_IRQ1	IRQ_GPIO11
#define IRQ_GPIO_H3600_PCMCIA_CD0	IRQ_GPIO17
#define IRQ_GPIO_H3600_PCMCIA_IRQ0	IRQ_GPIO21
#define IRQ_GPIO_H3600_COM_DCD		IRQ_GPIO23
#define IRQ_GPIO_H3600_OPT_IRQ		IRQ_GPIO24
#define IRQ_GPIO_H3600_COM_CTS		IRQ_GPIO25


#ifndef __ASSEMBLY__

enum ipaq_egpio_type {
	IPAQ_EGPIO_LCD_POWER,	  /* Power to the LCD panel */
	IPAQ_EGPIO_CODEC_NRESET,  /* Clear to reset the audio codec (remember to return high) */
	IPAQ_EGPIO_AUDIO_ON,	  /* Audio power */
	IPAQ_EGPIO_QMUTE,	  /* Audio muting */
	IPAQ_EGPIO_OPT_NVRAM_ON,  /* Non-volatile RAM on extension sleeves (SPI interface) */
	IPAQ_EGPIO_OPT_ON,	  /* Power to extension sleeves */
	IPAQ_EGPIO_CARD_RESET,	  /* Reset PCMCIA cards on extension sleeve (???) */
	IPAQ_EGPIO_OPT_RESET,	  /* Reset option pack (???) */
	IPAQ_EGPIO_IR_ON,	  /* IR sensor/emitter power */
	IPAQ_EGPIO_IR_FSEL,	  /* IR speed selection 1->fast, 0->slow */
	IPAQ_EGPIO_RS232_ON,	  /* Maxim RS232 chip power */
	IPAQ_EGPIO_VPP_ON,	  /* Turn on power to flash programming */
	IPAQ_EGPIO_LCD_ENABLE,	  /* Enable/disable LCD controller */
};

struct ipaq_model_ops {
	const char     *generic_name;
	void	      (*control)(enum ipaq_egpio_type, int);
	unsigned long (*read)(void);
	void	      (*blank_callback)(int blank);
	int	      (*pm_callback)(int req);	    /* Primary model callback */
	int	      (*pm_callback_aux)(int req);  /* Secondary callback (used by HAL modules) */
};

extern struct ipaq_model_ops ipaq_model_ops;

static __inline__ const char * h3600_generic_name(void)
{
	return ipaq_model_ops.generic_name;
}

static __inline__ void assign_h3600_egpio(enum ipaq_egpio_type x, int level)
{
	if (ipaq_model_ops.control)
		ipaq_model_ops.control(x,level);
}

static __inline__ void clr_h3600_egpio(enum ipaq_egpio_type x)
{
	if (ipaq_model_ops.control)
		ipaq_model_ops.control(x,0);
}

static __inline__ void set_h3600_egpio(enum ipaq_egpio_type x)
{
	if (ipaq_model_ops.control)
		ipaq_model_ops.control(x,1);
}

static __inline__ unsigned long read_h3600_egpio(void)
{
	if (ipaq_model_ops.read)
		return ipaq_model_ops.read();
	return 0;
}

static __inline__ int  h3600_register_blank_callback(void (*f)(int))
{
	ipaq_model_ops.blank_callback = f;
	return 0;
}

static __inline__ void h3600_unregister_blank_callback(void (*f)(int))
{
	ipaq_model_ops.blank_callback = NULL;
}


static __inline__ int  h3600_register_pm_callback(int (*f)(int))
{
	ipaq_model_ops.pm_callback_aux = f;
	return 0;
}

static __inline__ void h3600_unregister_pm_callback(int (*f)(int))
{
	ipaq_model_ops.pm_callback_aux = NULL;
}

static __inline__ int h3600_power_management(int req)
{
	if (ipaq_model_ops.pm_callback)
		return ipaq_model_ops.pm_callback(req);
	return 0;
}

#endif /* ASSEMBLY */

#endif /* _INCLUDE_H3600_H_ */
