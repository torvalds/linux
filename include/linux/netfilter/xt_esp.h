#ifndef _XT_ESP_H
#define _XT_ESP_H

struct xt_esp
{
	u_int32_t spis[2];	/* Security Parameter Index */
	u_int8_t  invflags;	/* Inverse flags */
};

/* Values for "invflags" field in struct xt_esp. */
#define XT_ESP_INV_SPI	0x01	/* Invert the sense of spi. */
#define XT_ESP_INV_MASK	0x01	/* All possible flags. */

#endif /*_XT_ESP_H*/
