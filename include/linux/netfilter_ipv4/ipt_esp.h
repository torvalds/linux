#ifndef _IPT_ESP_H
#define _IPT_ESP_H

struct ipt_esp
{
	u_int32_t spis[2];			/* Security Parameter Index */
	u_int8_t  invflags;			/* Inverse flags */
};



/* Values for "invflags" field in struct ipt_esp. */
#define IPT_ESP_INV_SPI		0x01	/* Invert the sense of spi. */
#define IPT_ESP_INV_MASK	0x01	/* All possible flags. */

#endif /*_IPT_ESP_H*/
