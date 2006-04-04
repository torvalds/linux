#ifndef _IP6T_ESP_H
#define _IP6T_ESP_H

#include <linux/netfilter/xt_esp.h>

#define ip6t_esp xt_esp
#define IP6T_ESP_INV_SPI	XT_ESP_INV_SPI
#define IP6T_ESP_INV_MASK	XT_ESP_INV_MASK

#endif /*_IP6T_ESP_H*/
