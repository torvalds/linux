// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bitfield.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/xarray.h>
#include <net/net_namespace.h>
#include <net/psp.h>
#include <net/udp.h>

#include "psp.h"
#include "psp-nl-gen.h"

DEFINE_XARRAY_ALLOC1(psp_devs);
struct mutex psp_devs_lock;

/**
 * DOC: PSP locking
 *
 * psp_devs_lock protects the psp_devs xarray.
 * Ordering is take the psp_devs_lock and then the instance lock.
 * Each instance is protected by RCU, and has a refcount.
 * When driver unregisters the instance gets flushed, but struct sticks around.
 */

/**
 * psp_dev_check_access() - check if user in a given net ns can access PSP dev
 * @psd:	PSP device structure user is trying to access
 * @net:	net namespace user is in
 *
 * Return: 0 if PSP device should be visible in @net, errno otherwise.
 */
int psp_dev_check_access(struct psp_dev *psd, struct net *net)
{
	if (dev_net(psd->main_netdev) == net)
		return 0;
	return -ENOENT;
}

/**
 * psp_dev_create() - create and register PSP device
 * @netdev:	main netdevice
 * @psd_ops:	driver callbacks
 * @psd_caps:	device capabilities
 * @priv_ptr:	back-pointer to driver private data
 *
 * Return: pointer to allocated PSP device, or ERR_PTR.
 */
struct psp_dev *
psp_dev_create(struct net_device *netdev,
	       struct psp_dev_ops *psd_ops, struct psp_dev_caps *psd_caps,
	       void *priv_ptr)
{
	struct psp_dev *psd;
	static u32 last_id;
	int err;

	if (WARN_ON(!psd_caps->versions ||
		    !psd_ops->set_config ||
		    !psd_ops->key_rotate ||
		    !psd_ops->rx_spi_alloc ||
		    !psd_ops->tx_key_add ||
		    !psd_ops->tx_key_del))
		return ERR_PTR(-EINVAL);

	psd = kzalloc(sizeof(*psd), GFP_KERNEL);
	if (!psd)
		return ERR_PTR(-ENOMEM);

	psd->main_netdev = netdev;
	psd->ops = psd_ops;
	psd->caps = psd_caps;
	psd->drv_priv = priv_ptr;

	mutex_init(&psd->lock);
	INIT_LIST_HEAD(&psd->active_assocs);
	INIT_LIST_HEAD(&psd->prev_assocs);
	INIT_LIST_HEAD(&psd->stale_assocs);
	refcount_set(&psd->refcnt, 1);

	mutex_lock(&psp_devs_lock);
	err = xa_alloc_cyclic(&psp_devs, &psd->id, psd, xa_limit_16b,
			      &last_id, GFP_KERNEL);
	if (err) {
		mutex_unlock(&psp_devs_lock);
		kfree(psd);
		return ERR_PTR(err);
	}
	mutex_lock(&psd->lock);
	mutex_unlock(&psp_devs_lock);

	psp_nl_notify_dev(psd, PSP_CMD_DEV_ADD_NTF);

	rcu_assign_pointer(netdev->psp_dev, psd);

	mutex_unlock(&psd->lock);

	return psd;
}
EXPORT_SYMBOL(psp_dev_create);

void psp_dev_destroy(struct psp_dev *psd)
{
	mutex_lock(&psp_devs_lock);
	xa_erase(&psp_devs, psd->id);
	mutex_unlock(&psp_devs_lock);

	mutex_destroy(&psd->lock);
	kfree_rcu(psd, rcu);
}

/**
 * psp_dev_unregister() - unregister PSP device
 * @psd:	PSP device structure
 */
void psp_dev_unregister(struct psp_dev *psd)
{
	struct psp_assoc *pas, *next;

	mutex_lock(&psp_devs_lock);
	mutex_lock(&psd->lock);

	psp_nl_notify_dev(psd, PSP_CMD_DEV_DEL_NTF);

	/* Wait until psp_dev_destroy() to call xa_erase() to prevent a
	 * different psd from being added to the xarray with this id, while
	 * there are still references to this psd being held.
	 */
	xa_store(&psp_devs, psd->id, NULL, GFP_KERNEL);
	mutex_unlock(&psp_devs_lock);

	list_splice_init(&psd->active_assocs, &psd->prev_assocs);
	list_splice_init(&psd->prev_assocs, &psd->stale_assocs);
	list_for_each_entry_safe(pas, next, &psd->stale_assocs, assocs_list)
		psp_dev_tx_key_del(psd, pas);

	rcu_assign_pointer(psd->main_netdev->psp_dev, NULL);

	psd->ops = NULL;
	psd->drv_priv = NULL;

	mutex_unlock(&psd->lock);

	psp_dev_put(psd);
}
EXPORT_SYMBOL(psp_dev_unregister);

unsigned int psp_key_size(u32 version)
{
	switch (version) {
	case PSP_VERSION_HDR0_AES_GCM_128:
	case PSP_VERSION_HDR0_AES_GMAC_128:
		return 16;
	case PSP_VERSION_HDR0_AES_GCM_256:
	case PSP_VERSION_HDR0_AES_GMAC_256:
		return 32;
	default:
		return 0;
	}
}
EXPORT_SYMBOL(psp_key_size);

static void psp_write_headers(struct net *net, struct sk_buff *skb, __be32 spi,
			      u8 ver, unsigned int udp_len, __be16 sport)
{
	struct udphdr *uh = udp_hdr(skb);
	struct psphdr *psph = (struct psphdr *)(uh + 1);

	uh->dest = htons(PSP_DEFAULT_UDP_PORT);
	uh->source = udp_flow_src_port(net, skb, 0, 0, false);
	uh->check = 0;
	uh->len = htons(udp_len);

	psph->nexthdr = IPPROTO_TCP;
	psph->hdrlen = PSP_HDRLEN_NOOPT;
	psph->crypt_offset = 0;
	psph->verfl = FIELD_PREP(PSPHDR_VERFL_VERSION, ver) |
		      FIELD_PREP(PSPHDR_VERFL_ONE, 1);
	psph->spi = spi;
	memset(&psph->iv, 0, sizeof(psph->iv));
}

/* Encapsulate a TCP packet with PSP by adding the UDP+PSP headers and filling
 * them in.
 */
bool psp_dev_encapsulate(struct net *net, struct sk_buff *skb, __be32 spi,
			 u8 ver, __be16 sport)
{
	u32 network_len = skb_network_header_len(skb);
	u32 ethr_len = skb_mac_header_len(skb);
	u32 bufflen = ethr_len + network_len;

	if (skb_cow_head(skb, PSP_ENCAP_HLEN))
		return false;

	skb_push(skb, PSP_ENCAP_HLEN);
	skb->mac_header		-= PSP_ENCAP_HLEN;
	skb->network_header	-= PSP_ENCAP_HLEN;
	skb->transport_header	-= PSP_ENCAP_HLEN;
	memmove(skb->data, skb->data + PSP_ENCAP_HLEN, bufflen);

	if (skb->protocol == htons(ETH_P_IP)) {
		ip_hdr(skb)->protocol = IPPROTO_UDP;
		be16_add_cpu(&ip_hdr(skb)->tot_len, PSP_ENCAP_HLEN);
		ip_hdr(skb)->check = 0;
		ip_hdr(skb)->check =
			ip_fast_csum((u8 *)ip_hdr(skb), ip_hdr(skb)->ihl);
	} else if (skb->protocol == htons(ETH_P_IPV6)) {
		ipv6_hdr(skb)->nexthdr = IPPROTO_UDP;
		be16_add_cpu(&ipv6_hdr(skb)->payload_len, PSP_ENCAP_HLEN);
	} else {
		return false;
	}

	skb_set_inner_ipproto(skb, IPPROTO_TCP);
	skb_set_inner_transport_header(skb, skb_transport_offset(skb) +
						    PSP_ENCAP_HLEN);
	skb->encapsulation = 1;
	psp_write_headers(net, skb, spi, ver,
			  skb->len - skb_transport_offset(skb), sport);

	return true;
}
EXPORT_SYMBOL(psp_dev_encapsulate);

static int __init psp_init(void)
{
	mutex_init(&psp_devs_lock);

	return genl_register_family(&psp_nl_family);
}

subsys_initcall(psp_init);
