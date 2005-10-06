/* net/atm/addr.c - Local ATM address registry */

/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */

#include <linux/atm.h>
#include <linux/atmdev.h>
#include <linux/sched.h>
#include <asm/uaccess.h>

#include "signaling.h"
#include "addr.h"

static int check_addr(struct sockaddr_atmsvc *addr)
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

static int identical(struct sockaddr_atmsvc *a, struct sockaddr_atmsvc *b)
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

static void notify_sigd(struct atm_dev *dev)
{
	struct sockaddr_atmpvc pvc;

	pvc.sap_addr.itf = dev->number;
	sigd_enq(NULL, as_itf_notify, NULL, &pvc, NULL);
}

void atm_reset_addr(struct atm_dev *dev)
{
	unsigned long flags;
	struct atm_dev_addr *this, *p;

	spin_lock_irqsave(&dev->lock, flags);
	list_for_each_entry_safe(this, p, &dev->local, entry) {
		list_del(&this->entry);
		kfree(this);
	}
	spin_unlock_irqrestore(&dev->lock, flags);
	notify_sigd(dev);
}

int atm_add_addr(struct atm_dev *dev, struct sockaddr_atmsvc *addr)
{
	unsigned long flags;
	struct atm_dev_addr *this;
	int error;

	error = check_addr(addr);
	if (error)
		return error;
	spin_lock_irqsave(&dev->lock, flags);
	list_for_each_entry(this, &dev->local, entry) {
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
	list_add(&this->entry, &dev->local);
	spin_unlock_irqrestore(&dev->lock, flags);
	notify_sigd(dev);
	return 0;
}

int atm_del_addr(struct atm_dev *dev, struct sockaddr_atmsvc *addr)
{
	unsigned long flags;
	struct atm_dev_addr *this;
	int error;

	error = check_addr(addr);
	if (error)
		return error;
	spin_lock_irqsave(&dev->lock, flags);
	list_for_each_entry(this, &dev->local, entry) {
		if (identical(&this->addr, addr)) {
			list_del(&this->entry);
			spin_unlock_irqrestore(&dev->lock, flags);
			kfree(this);
			notify_sigd(dev);
			return 0;
		}
	}
	spin_unlock_irqrestore(&dev->lock, flags);
	return -ENOENT;
}

int atm_get_addr(struct atm_dev *dev, struct sockaddr_atmsvc __user * buf,
		 size_t size)
{
	unsigned long flags;
	struct atm_dev_addr *this;
	int total = 0, error;
	struct sockaddr_atmsvc *tmp_buf, *tmp_bufp;

	spin_lock_irqsave(&dev->lock, flags);
	list_for_each_entry(this, &dev->local, entry)
	    total += sizeof(struct sockaddr_atmsvc);
	tmp_buf = tmp_bufp = kmalloc(total, GFP_ATOMIC);
	if (!tmp_buf) {
		spin_unlock_irqrestore(&dev->lock, flags);
		return -ENOMEM;
	}
	list_for_each_entry(this, &dev->local, entry)
	    memcpy(tmp_bufp++, &this->addr, sizeof(struct sockaddr_atmsvc));
	spin_unlock_irqrestore(&dev->lock, flags);
	error = total > size ? -E2BIG : total;
	if (copy_to_user(buf, tmp_buf, total < size ? total : size))
		error = -EFAULT;
	kfree(tmp_buf);
	return error;
}
