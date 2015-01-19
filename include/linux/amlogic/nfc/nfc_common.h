#ifndef NFC_COMMON_H
#define NFC_COMMON_H

#include <linux/i2c.h>

struct nfc_pdata{
    unsigned int irq_gpio;
	unsigned int en_gpio;
	unsigned int wake_gpio;
	unsigned int irq;
	unsigned int bus_type;
    unsigned int addr;
    char *owner;
};

#endif
