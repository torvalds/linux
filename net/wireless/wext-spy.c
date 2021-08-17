/*
 * This file implement the Wireless Extensions spy API.
 *
 * Authors :	Jean Tourrilhes - HPL - <jt@hpl.hp.com>
 * Copyright (c) 1997-2007 Jean Tourrilhes, All Rights Reserved.
 *
 * (As all part of the Linux kernel, this file is GPL)
 */

#include <linux/wireless.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/export.h>
#include <net/iw_handler.h>
#include <net/arp.h>
#include <net/wext.h>

static inline struct iw_spy_data *get_spydata(struct net_device *dev)
{
	/* This is the new way */
	if (dev->wireless_data)
		return dev->wireless_data->spy_data;
	return NULL;
}

int iw_handler_set_spy(struct net_device *	dev,
		       struct iw_request_info *	info,
		       union iwreq_data *	wrqu,
		       char *			extra)
{
	struct iw_spy_data *	spydata = get_spydata(dev);
	struct sockaddr *	address = (struct sockaddr *) extra;

	/* Make sure driver is not buggy or using the old API */
	if (!spydata)
		return -EOPNOTSUPP;

	/* Disable spy collection while we copy the addresses.
	 * While we copy addresses, any call to wireless_spy_update()
	 * will NOP. This is OK, as anyway the addresses are changing. */
	spydata->spy_number = 0;

	/* We want to operate without locking, because wireless_spy_update()
	 * most likely will happen in the interrupt handler, and therefore
	 * have its own locking constraints and needs performance.
	 * The rtnl_lock() make sure we don't race with the other iw_handlers.
	 * This make sure wireless_spy_update() "see" that the spy list
	 * is temporarily disabled. */
	smp_wmb();

	/* Are there are addresses to copy? */
	if (wrqu->data.length > 0) {
		int i;

		/* Copy addresses */
		for (i = 0; i < wrqu->data.length; i++)
			memcpy(spydata->spy_address[i], address[i].sa_data,
			       ETH_ALEN);
		/* Reset stats */
		memset(spydata->spy_stat, 0,
		       sizeof(struct iw_quality) * IW_MAX_SPY);
	}

	/* Make sure above is updated before re-enabling */
	smp_wmb();

	/* Enable addresses */
	spydata->spy_number = wrqu->data.length;

	return 0;
}
EXPORT_SYMBOL(iw_handler_set_spy);

int iw_handler_get_spy(struct net_device *	dev,
		       struct iw_request_info *	info,
		       union iwreq_data *	wrqu,
		       char *			extra)
{
	struct iw_spy_data *	spydata = get_spydata(dev);
	struct sockaddr *	address = (struct sockaddr *) extra;
	int			i;

	/* Make sure driver is not buggy or using the old API */
	if (!spydata)
		return -EOPNOTSUPP;

	wrqu->data.length = spydata->spy_number;

	/* Copy addresses. */
	for (i = 0; i < spydata->spy_number; i++) 	{
		memcpy(address[i].sa_data, spydata->spy_address[i], ETH_ALEN);
		address[i].sa_family = AF_UNIX;
	}
	/* Copy stats to the user buffer (just after). */
	if (spydata->spy_number > 0)
		memcpy(extra  + (sizeof(struct sockaddr) *spydata->spy_number),
		       spydata->spy_stat,
		       sizeof(struct iw_quality) * spydata->spy_number);
	/* Reset updated flags. */
	for (i = 0; i < spydata->spy_number; i++)
		spydata->spy_stat[i].updated &= ~IW_QUAL_ALL_UPDATED;
	return 0;
}
EXPORT_SYMBOL(iw_handler_get_spy);

/*------------------------------------------------------------------*/
/*
 * Standard Wireless Handler : set spy threshold
 */
int iw_handler_set_thrspy(struct net_device *	dev,
			  struct iw_request_info *info,
			  union iwreq_data *	wrqu,
			  char *		extra)
{
	struct iw_spy_data *	spydata = get_spydata(dev);
	struct iw_thrspy *	threshold = (struct iw_thrspy *) extra;

	/* Make sure driver is not buggy or using the old API */
	if (!spydata)
		return -EOPNOTSUPP;

	/* Just do it */
	spydata->spy_thr_low = threshold->low;
	spydata->spy_thr_high = threshold->high;

	/* Clear flag */
	memset(spydata->spy_thr_under, '\0', sizeof(spydata->spy_thr_under));

	return 0;
}
EXPORT_SYMBOL(iw_handler_set_thrspy);

/*------------------------------------------------------------------*/
/*
 * Standard Wireless Handler : get spy threshold
 */
int iw_handler_get_thrspy(struct net_device *	dev,
			  struct iw_request_info *info,
			  union iwreq_data *	wrqu,
			  char *		extra)
{
	struct iw_spy_data *	spydata = get_spydata(dev);
	struct iw_thrspy *	threshold = (struct iw_thrspy *) extra;

	/* Make sure driver is not buggy or using the old API */
	if (!spydata)
		return -EOPNOTSUPP;

	/* Just do it */
	threshold->low = spydata->spy_thr_low;
	threshold->high = spydata->spy_thr_high;

	return 0;
}
EXPORT_SYMBOL(iw_handler_get_thrspy);

/*------------------------------------------------------------------*/
/*
 * Prepare and send a Spy Threshold event
 */
static void iw_send_thrspy_event(struct net_device *	dev,
				 struct iw_spy_data *	spydata,
				 unsigned char *	address,
				 struct iw_quality *	wstats)
{
	union iwreq_data	wrqu;
	struct iw_thrspy	threshold;

	/* Init */
	wrqu.data.length = 1;
	wrqu.data.flags = 0;
	/* Copy address */
	memcpy(threshold.addr.sa_data, address, ETH_ALEN);
	threshold.addr.sa_family = ARPHRD_ETHER;
	/* Copy stats */
	threshold.qual = *wstats;
	/* Copy also thresholds */
	threshold.low = spydata->spy_thr_low;
	threshold.high = spydata->spy_thr_high;

	/* Send event to user space */
	wireless_send_event(dev, SIOCGIWTHRSPY, &wrqu, (char *) &threshold);
}

/* ---------------------------------------------------------------- */
/*
 * Call for the driver to update the spy data.
 * For now, the spy data is a simple array. As the size of the array is
 * small, this is good enough. If we wanted to support larger number of
 * spy addresses, we should use something more efficient...
 */
void wireless_spy_update(struct net_device *	dev,
			 unsigned char *	address,
			 struct iw_quality *	wstats)
{
	struct iw_spy_data *	spydata = get_spydata(dev);
	int			i;
	int			match = -1;

	/* Make sure driver is not buggy or using the old API */
	if (!spydata)
		return;

	/* Update all records that match */
	for (i = 0; i < spydata->spy_number; i++)
		if (ether_addr_equal(address, spydata->spy_address[i])) {
			memcpy(&(spydata->spy_stat[i]), wstats,
			       sizeof(struct iw_quality));
			match = i;
		}

	/* Generate an event if we cross the spy threshold.
	 * To avoid event storms, we have a simple hysteresis : we generate
	 * event only when we go under the low threshold or above the
	 * high threshold. */
	if (match >= 0) {
		if (spydata->spy_thr_under[match]) {
			if (wstats->level > spydata->spy_thr_high.level) {
				spydata->spy_thr_under[match] = 0;
				iw_send_thrspy_event(dev, spydata,
						     address, wstats);
			}
		} else {
			if (wstats->level < spydata->spy_thr_low.level) {
				spydata->spy_thr_under[match] = 1;
				iw_send_thrspy_event(dev, spydata,
						     address, wstats);
			}
		}
	}
}
EXPORT_SYMBOL(wireless_spy_update);
