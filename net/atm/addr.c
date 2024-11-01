// SPDX-License-Identifier: GPL-2.0
/* net/atm/addr.c - Local ATM address registry */

/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */

#include <linux/atm.h>
#include <linux/atmdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "signaling.h"
#include "addr.h"

static int check_addr(const struct sockaddr_atmsvc *addr)
{
	int i;

	if (addr->sas_family != AF_ATMSVC)
		return -EAFNOSUPPORT;
	if (!*addr->sas_addr.pub)
		return *addr->sas_addr.prv ? 0 : -EINVAL;
	for (i = 1; i < ATM_E164_LEN + 1; i++)	/* make sure it's \0-terminated */
		if (!addr->sas_addr.pub[i])
			return 0;
	return -EINVAL;
}

static int identical(const struct sockaddr_atmsvc *a, const struct sockaddr_atmsvc *b)
{
	if (*a->sas_addr.prv)
		if (memcmp(a->sas_addr.prv, b->sas_addr.prv, ATM_ESA_LEN))
			return 0;
	if (!*a->sas_addr.pub)
		return !*b->sas_addr.pub;
	if (!*b->sas_addr.pub)
		return 0;
	return !strcmp(a->sas_addr.pub, b->sas_addr.pub);
}

static void notify_sigd(const struct atm_dev *dev)
{
	struct sockaddr_atmpvc pvc;

	pvc.sap_addr.itf = dev->number;
	sigd_enq(NULL, as_itf_notify, NULL, &pvc, NULL);
}

void atm_reset_addr(struct atm_dev *dev, enum atm_addr_type_t atype)
{
	unsigned long flags;
	struct atm_dev_addr *this, *p;
	struct list_head *head;

	spin_lock_irqsave(&dev->lock, flags);
	if (atype == ATM_ADDR_LECS)
		head = &dev->lecs;
	else
		head = &dev->local;
	list_for_each_entry_safe(this, p, head, entry) {
		list_del(&this->entry);
		kfree(this);
	}
	spin_unlock_irqrestore(&dev->lock, flags);
	if (head == &dev->local)
		notify_sigd(dev);
}

int atm_add_addr(struct atm_dev *dev, const struct sockaddr_atmsvc *addr,
		 enum atm_addr_type_t atype)
{
	unsigned long flags;
	struct atm_dev_addr *this;
	struct list_head *head;
	int error;

	error = check_addr(addr);
	if (error)
		return error;
	spin_lock_irqsave(&dev->lock, flags);
	if (atype == ATM_ADDR_LECS)
		head = &dev->lecs;
	else
		head = &dev->local;
	list_for_each_entry(this, head, entry) {
		if (identical(&this->addr, addr)) {
			spin_unlock_irqrestore(&dev->lock, flags);
			return -EEXIST;
		}
	}
	this = kmalloc(sizeof(struct atm_dev_addr), GFP_ATOMIC);
	if (!this) {
		spin_unlock_irqrestore(&dev->lock, flags);
		return -ENOMEM;
	}
	this->addr = *addr;
	list_add(&this->entry, head);
	spin_unlock_irqrestore(&dev->lock, flags);
	if (head == &dev->local)
		notify_sigd(dev);
	return 0;
}

int atm_del_addr(struct atm_dev *dev, const struct sockaddr_atmsvc *addr,
		 enum atm_addr_type_t atype)
{
	unsigned long flags;
	struct atm_dev_addr *this;
	struct list_head *head;
	int error;

	error = check_addr(addr);
	if (error)
		return error;
	spin_lock_irqsave(&dev->lock, flags);
	if (atype == ATM_ADDR_LECS)
		head = &dev->lecs;
	else
		head = &dev->local;
	list_for_each_entry(this, head, entry) {
		if (identical(&this->addr, addr)) {
			list_del(&this->entry);
			spin_unlock_irqrestore(&dev->lock, flags);
			kfree(this);
			if (head == &dev->local)
				notify_sigd(dev);
			return 0;
		}
	}
	spin_unlock_irqrestore(&dev->lock, flags);
	return -ENOENT;
}

int atm_get_addr(struct atm_dev *dev, struct sockaddr_atmsvc __user * buf,
		 size_t size, enum atm_addr_type_t atype)
{
	unsigned long flags;
	struct atm_dev_addr *this;
	struct list_head *head;
	int total = 0, error;
	struct sockaddr_atmsvc *tmp_buf, *tmp_bufp;

	spin_lock_irqsave(&dev->lock, flags);
	if (atype == ATM_ADDR_LECS)
		head = &dev->lecs;
	else
		head = &dev->local;
	list_for_each_entry(this, head, entry)
	    total += sizeof(struct sockaddr_atmsvc);
	tmp_buf = tmp_bufp = kmalloc(total, GFP_ATOMIC);
	if (!tmp_buf) {
		spin_unlock_irqrestore(&dev->lock, flags);
		return -ENOMEM;
	}
	list_for_each_entry(this, head, entry)
	    memcpy(tmp_bufp++, &this->addr, sizeof(struct sockaddr_atmsvc));
	spin_unlock_irqrestore(&dev->lock, flags);
	error = total > size ? -E2BIG : total;
	if (copy_to_user(buf, tmp_buf, total < size ? total : size))
		error = -EFAULT;
	kfree(tmp_buf);
	return error;
}
