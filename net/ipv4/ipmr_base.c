/* Linux multicast routing support
 * Common logic shared by IPv4 [ipmr] and IPv6 [ip6mr] implementation
 */

#include <linux/mroute_base.h>

/* Sets everything common except 'dev', since that is done under locking */
void vif_device_init(struct vif_device *v,
		     struct net_device *dev,
		     unsigned long rate_limit,
		     unsigned char threshold,
		     unsigned short flags,
		     unsigned short get_iflink_mask)
{
	v->dev = NULL;
	v->bytes_in = 0;
	v->bytes_out = 0;
	v->pkt_in = 0;
	v->pkt_out = 0;
	v->rate_limit = rate_limit;
	v->flags = flags;
	v->threshold = threshold;
	if (v->flags & get_iflink_mask)
		v->link = dev_get_iflink(dev);
	else
		v->link = dev->ifindex;
}
EXPORT_SYMBOL(vif_device_init);
