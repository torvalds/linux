#ifndef _IP6T_ESP_H
#define _IP6T_ESP_H

struct ip6t_esp
{
	u_int32_t spis[2];			/* Security Parameter Index */
	u_int8_t  invflags;			/* Inverse flags */
};

/* Values for "invflags" field in struct ip6t_esp. */
#define IP6T_ESP_INV_SPI		0x01	/* Invert the sense of spi. */
#define IP6T_ESP_INV_MASK	0x01	/* All possible flags. */

#endif /*_IP6T_ESP_H*/
