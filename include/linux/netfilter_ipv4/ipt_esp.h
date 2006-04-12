#ifndef _IPT_ESP_H
#define _IPT_ESP_H

#include <linux/netfilter/xt_esp.h>

#define ipt_esp xt_esp
#define IPT_ESP_INV_SPI		XT_ESP_INV_SPI
#define IPT_ESP_INV_MASK	XT_ESP_INV_MASK

#endif /*_IPT_ESP_H*/
