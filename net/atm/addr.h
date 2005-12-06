/* net/atm/addr.h - Local ATM address registry */

/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */


#ifndef NET_ATM_ADDR_H
#define NET_ATM_ADDR_H

#include <linux/atm.h>
#include <linux/atmdev.h>

void atm_reset_addr(struct atm_dev *dev, enum atm_addr_type_t type);
int atm_add_addr(struct atm_dev *dev, struct sockaddr_atmsvc *addr,
		 enum atm_addr_type_t type);
int atm_del_addr(struct atm_dev *dev, struct sockaddr_atmsvc *addr,
		 enum atm_addr_type_t type);
int atm_get_addr(struct atm_dev *dev, struct sockaddr_atmsvc __user *buf,
		 size_t size, enum atm_addr_type_t type);

#endif
