/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __NET_PSP_H
#define __NET_PSP_H

#include <linux/mutex.h>
#include <linux/refcount.h>

struct netlink_ext_ack;

#define PSP_DEFAULT_UDP_PORT	1000

struct psphdr {
	u8	nexthdr;
	u8	hdrlen;
	u8	crypt_offset;
	u8	verfl;
	__be32	spi;
	__be64	iv;
	__be64	vc[]; /* optional */
};

#define PSP_ENCAP_HLEN (sizeof(struct udphdr) + sizeof(struct psphdr))

#define PSP_SPI_KEY_ID		GENMASK(30, 0)
#define PSP_SPI_KEY_PHASE	BIT(31)

#define PSPHDR_CRYPT_OFFSET	GENMASK(5, 0)

#define PSPHDR_VERFL_SAMPLE	BIT(7)
#define PSPHDR_VERFL_DROP	BIT(6)
#define PSPHDR_VERFL_VERSION	GENMASK(5, 2)
#define PSPHDR_VERFL_VIRT	BIT(1)
#define PSPHDR_VERFL_ONE	BIT(0)

#define PSP_HDRLEN_NOOPT	((sizeof(struct psphdr) - 8) / 8)

/**
 * struct psp_dev_config - PSP device configuration
 * @versions: PSP versions enabled on the device
 */
struct psp_dev_config {
	u32 versions;
};

/**
 * struct psp_dev - PSP device struct
 * @main_netdev: original netdevice of this PSP device
 * @ops:	driver callbacks
 * @caps:	device capabilities
 * @drv_priv:	driver priv pointer
 * @lock:	instance lock, protects all fields
 * @refcnt:	reference count for the instance
 * @id:		instance id
 * @generation:	current generation of the device key
 * @config:	current device configuration
 * @active_assocs:	list of registered associations
 * @prev_assocs:	associations which use old (but still usable)
 *			device key
 * @stale_assocs:	associations which use a rotated out key
 *
 * @rcu:	RCU head for freeing the structure
 */
struct psp_dev {
	struct net_device *main_netdev;

	struct psp_dev_ops *ops;
	struct psp_dev_caps *caps;
	void *drv_priv;

	struct mutex lock;
	refcount_t refcnt;

	u32 id;

	u8 generation;

	struct psp_dev_config config;

	struct list_head active_assocs;
	struct list_head prev_assocs;
	struct list_head stale_assocs;

	struct rcu_head rcu;
};

#define PSP_GEN_VALID_MASK	0x7f

/**
 * struct psp_dev_caps - PSP device capabilities
 */
struct psp_dev_caps {
	/**
	 * @versions: mask of supported PSP versions
	 * Set this field to 0 to indicate PSP is not supported at all.
	 */
	u32 versions;

	/**
	 * @assoc_drv_spc: size of driver-specific state in Tx assoc
	 * Determines the size of struct psp_assoc::drv_data
	 */
	u32 assoc_drv_spc;
};

#define PSP_MAX_KEY	32

#define PSP_HDR_SIZE	16	/* We don't support optional fields, yet */
#define PSP_TRL_SIZE	16	/* AES-GCM/GMAC trailer size */

struct psp_skb_ext {
	__be32 spi;
	u16 dev_id;
	u8 generation;
	u8 version;
};

struct psp_key_parsed {
	__be32 spi;
	u8 key[PSP_MAX_KEY];
};

struct psp_assoc {
	struct psp_dev *psd;

	u16 dev_id;
	u8 generation;
	u8 version;
	u8 peer_tx;

	u32 upgrade_seq;

	struct psp_key_parsed tx;
	struct psp_key_parsed rx;

	refcount_t refcnt;
	struct rcu_head rcu;
	struct work_struct work;
	struct list_head assocs_list;

	u8 drv_data[] __aligned(8);
};

/**
 * struct psp_dev_ops - netdev driver facing PSP callbacks
 */
struct psp_dev_ops {
	/**
	 * @set_config: set configuration of a PSP device
	 * Driver can inspect @psd->config for the previous configuration.
	 * Core will update @psd->config with @config on success.
	 */
	int (*set_config)(struct psp_dev *psd, struct psp_dev_config *conf,
			  struct netlink_ext_ack *extack);

	/**
	 * @key_rotate: rotate the device key
	 */
	int (*key_rotate)(struct psp_dev *psd, struct netlink_ext_ack *extack);

	/**
	 * @rx_spi_alloc: allocate an Rx SPI+key pair
	 * Allocate an Rx SPI and resulting derived key.
	 * This key should remain valid until key rotation.
	 */
	int (*rx_spi_alloc)(struct psp_dev *psd, u32 version,
			    struct psp_key_parsed *assoc,
			    struct netlink_ext_ack *extack);

	/**
	 * @tx_key_add: add a Tx key to the device
	 * Install an association in the device. Core will allocate space
	 * for the driver to use at drv_data.
	 */
	int (*tx_key_add)(struct psp_dev *psd, struct psp_assoc *pas,
			  struct netlink_ext_ack *extack);
	/**
	 * @tx_key_del: remove a Tx key from the device
	 * Remove an association from the device.
	 */
	void (*tx_key_del)(struct psp_dev *psd, struct psp_assoc *pas);
};

#endif /* __NET_PSP_H */
