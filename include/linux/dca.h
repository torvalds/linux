/*
 * Copyright(c) 2007 - 2009 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING.
 */
#ifndef DCA_H
#define DCA_H
/* DCA Provider API */

/* DCA Notifier Interface */
void dca_register_notify(struct notifier_block *nb);
void dca_unregister_notify(struct notifier_block *nb);

#define DCA_PROVIDER_ADD     0x0001
#define DCA_PROVIDER_REMOVE  0x0002

struct dca_provider {
	struct list_head	node;
	struct dca_ops		*ops;
	struct device 		*cd;
	int			 id;
};

struct dca_ops {
	int	(*add_requester)    (struct dca_provider *, struct device *);
	int	(*remove_requester) (struct dca_provider *, struct device *);
	u8	(*get_tag)	    (struct dca_provider *, struct device *,
				     int cpu);
	int	(*dev_managed)      (struct dca_provider *, struct device *);
};

struct dca_provider *alloc_dca_provider(struct dca_ops *ops, int priv_size);
void free_dca_provider(struct dca_provider *dca);
int register_dca_provider(struct dca_provider *dca, struct device *dev);
void unregister_dca_provider(struct dca_provider *dca);

static inline void *dca_priv(struct dca_provider *dca)
{
	return (void *)dca + sizeof(struct dca_provider);
}

/* Requester API */
#define DCA_GET_TAG_TWO_ARGS
int dca_add_requester(struct device *dev);
int dca_remove_requester(struct device *dev);
u8 dca_get_tag(int cpu);
u8 dca3_get_tag(struct device *dev, int cpu);

/* internal stuff */
int __init dca_sysfs_init(void);
void __exit dca_sysfs_exit(void);
int dca_sysfs_add_provider(struct dca_provider *dca, struct device *dev);
void dca_sysfs_remove_provider(struct dca_provider *dca);
int dca_sysfs_add_req(struct dca_provider *dca, struct device *dev, int slot);
void dca_sysfs_remove_req(struct dca_provider *dca, int slot);

#endif /* DCA_H */
