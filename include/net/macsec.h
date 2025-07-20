/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * MACsec netdev header, used for h/w accelerated implementations.
 *
 * Copyright (c) 2015 Sabrina Dubroca <sd@queasysnail.net>
 */
#ifndef _NET_MACSEC_H_
#define _NET_MACSEC_H_

#include <linux/u64_stats_sync.h>
#include <linux/if_vlan.h>
#include <uapi/linux/if_link.h>
#include <uapi/linux/if_macsec.h>

#define MACSEC_DEFAULT_PN_LEN 4
#define MACSEC_XPN_PN_LEN 8

#define MACSEC_NUM_AN 4 /* 2 bits for the association number */

#define MACSEC_SCI_LEN 8
#define MACSEC_PORT_ES (htons(0x0001))

#define MACSEC_TCI_VERSION 0x80
#define MACSEC_TCI_ES      0x40 /* end station */
#define MACSEC_TCI_SC      0x20 /* SCI present */
#define MACSEC_TCI_SCB     0x10 /* epon */
#define MACSEC_TCI_E       0x08 /* encryption */
#define MACSEC_TCI_C       0x04 /* changed text */
#define MACSEC_AN_MASK     0x03 /* association number */
#define MACSEC_TCI_CONFID  (MACSEC_TCI_E | MACSEC_TCI_C)

#define MACSEC_DEFAULT_ICV_LEN 16

typedef u64 __bitwise sci_t;
typedef u32 __bitwise ssci_t;

struct metadata_dst;

typedef union salt {
	struct {
		ssci_t ssci;
		__be64 pn;
	} __packed;
	u8 bytes[MACSEC_SALT_LEN];
} __packed salt_t;

typedef union pn {
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		u32 lower;
		u32 upper;
#elif defined(__BIG_ENDIAN_BITFIELD)
		u32 upper;
		u32 lower;
#else
#error	"Please fix <asm/byteorder.h>"
#endif
	};
	u64 full64;
} pn_t;

/**
 * struct macsec_key - SA key
 * @id: user-provided key identifier
 * @tfm: crypto struct, key storage
 * @salt: salt used to generate IV in XPN cipher suites
 */
struct macsec_key {
	u8 id[MACSEC_KEYID_LEN];
	struct crypto_aead *tfm;
	salt_t salt;
};

struct macsec_rx_sc_stats {
	__u64 InOctetsValidated;
	__u64 InOctetsDecrypted;
	__u64 InPktsUnchecked;
	__u64 InPktsDelayed;
	__u64 InPktsOK;
	__u64 InPktsInvalid;
	__u64 InPktsLate;
	__u64 InPktsNotValid;
	__u64 InPktsNotUsingSA;
	__u64 InPktsUnusedSA;
};

struct macsec_rx_sa_stats {
	__u32 InPktsOK;
	__u32 InPktsInvalid;
	__u32 InPktsNotValid;
	__u32 InPktsNotUsingSA;
	__u32 InPktsUnusedSA;
};

struct macsec_tx_sa_stats {
	__u32 OutPktsProtected;
	__u32 OutPktsEncrypted;
};

struct macsec_tx_sc_stats {
	__u64 OutPktsProtected;
	__u64 OutPktsEncrypted;
	__u64 OutOctetsProtected;
	__u64 OutOctetsEncrypted;
};

struct macsec_dev_stats {
	__u64 OutPktsUntagged;
	__u64 InPktsUntagged;
	__u64 OutPktsTooLong;
	__u64 InPktsNoTag;
	__u64 InPktsBadTag;
	__u64 InPktsUnknownSCI;
	__u64 InPktsNoSCI;
	__u64 InPktsOverrun;
};

/**
 * struct macsec_rx_sa - receive secure association
 * @active:
 * @next_pn: packet number expected for the next packet
 * @lock: protects next_pn manipulations
 * @key: key structure
 * @ssci: short secure channel identifier
 * @stats: per-SA stats
 */
struct macsec_rx_sa {
	struct macsec_key key;
	ssci_t ssci;
	spinlock_t lock;
	union {
		pn_t next_pn_halves;
		u64 next_pn;
	};
	refcount_t refcnt;
	bool active;
	struct macsec_rx_sa_stats __percpu *stats;
	struct macsec_rx_sc *sc;
	struct rcu_head rcu;
};

struct pcpu_rx_sc_stats {
	struct macsec_rx_sc_stats stats;
	struct u64_stats_sync syncp;
};

struct pcpu_tx_sc_stats {
	struct macsec_tx_sc_stats stats;
	struct u64_stats_sync syncp;
};

/**
 * struct macsec_rx_sc - receive secure channel
 * @sci: secure channel identifier for this SC
 * @active: channel is active
 * @sa: array of secure associations
 * @stats: per-SC stats
 */
struct macsec_rx_sc {
	struct macsec_rx_sc __rcu *next;
	sci_t sci;
	bool active;
	struct macsec_rx_sa __rcu *sa[MACSEC_NUM_AN];
	struct pcpu_rx_sc_stats __percpu *stats;
	refcount_t refcnt;
	struct rcu_head rcu_head;
};

/**
 * struct macsec_tx_sa - transmit secure association
 * @active:
 * @next_pn: packet number to use for the next packet
 * @lock: protects next_pn manipulations
 * @key: key structure
 * @ssci: short secure channel identifier
 * @stats: per-SA stats
 */
struct macsec_tx_sa {
	struct macsec_key key;
	ssci_t ssci;
	spinlock_t lock;
	union {
		pn_t next_pn_halves;
		u64 next_pn;
	};
	refcount_t refcnt;
	bool active;
	struct macsec_tx_sa_stats __percpu *stats;
	struct rcu_head rcu;
};

/**
 * struct macsec_tx_sc - transmit secure channel
 * @active:
 * @encoding_sa: association number of the SA currently in use
 * @encrypt: encrypt packets on transmit, or authenticate only
 * @send_sci: always include the SCI in the SecTAG
 * @end_station:
 * @scb: single copy broadcast flag
 * @sa: array of secure associations
 * @stats: stats for this TXSC
 * @md_dst: MACsec offload metadata dst
 */
struct macsec_tx_sc {
	bool active;
	u8 encoding_sa;
	bool encrypt;
	bool send_sci;
	bool end_station;
	bool scb;
	struct macsec_tx_sa __rcu *sa[MACSEC_NUM_AN];
	struct pcpu_tx_sc_stats __percpu *stats;
	struct metadata_dst *md_dst;
};

/**
 * struct macsec_secy - MACsec Security Entity
 * @netdev: netdevice for this SecY
 * @n_rx_sc: number of receive secure channels configured on this SecY
 * @sci: secure channel identifier used for tx
 * @key_len: length of keys used by the cipher suite
 * @icv_len: length of ICV used by the cipher suite
 * @validate_frames: validation mode
 * @xpn: enable XPN for this SecY
 * @operational: MAC_Operational flag
 * @protect_frames: enable protection for this SecY
 * @replay_protect: enable packet number checks on receive
 * @replay_window: size of the replay window
 * @tx_sc: transmit secure channel
 * @rx_sc: linked list of receive secure channels
 */
struct macsec_secy {
	struct net_device *netdev;
	unsigned int n_rx_sc;
	sci_t sci;
	u16 key_len;
	u16 icv_len;
	enum macsec_validation_type validate_frames;
	bool xpn;
	bool operational;
	bool protect_frames;
	bool replay_protect;
	u32 replay_window;
	struct macsec_tx_sc tx_sc;
	struct macsec_rx_sc __rcu *rx_sc;
};

/**
 * struct macsec_context - MACsec context for hardware offloading
 * @netdev: a valid pointer to a struct net_device if @offload ==
 *	MACSEC_OFFLOAD_MAC
 * @phydev: a valid pointer to a struct phy_device if @offload ==
 *	MACSEC_OFFLOAD_PHY
 * @offload: MACsec offload status
 * @secy: pointer to a MACsec SecY
 * @rx_sc: pointer to a RX SC
 * @update_pn: when updating the SA, update the next PN
 * @assoc_num: association number of the target SA
 * @key: key of the target SA
 * @rx_sa: pointer to an RX SA if a RX SA is added/updated/removed
 * @tx_sa: pointer to an TX SA if a TX SA is added/updated/removed
 * @tx_sc_stats: pointer to TX SC stats structure
 * @tx_sa_stats: pointer to TX SA stats structure
 * @rx_sc_stats: pointer to RX SC stats structure
 * @rx_sa_stats: pointer to RX SA stats structure
 * @dev_stats: pointer to dev stats structure
 */
struct macsec_context {
	union {
		struct net_device *netdev;
		struct phy_device *phydev;
	};
	enum macsec_offload offload;

	struct macsec_secy *secy;
	struct macsec_rx_sc *rx_sc;
	struct {
		bool update_pn;
		unsigned char assoc_num;
		u8 key[MACSEC_MAX_KEY_LEN];
		union {
			struct macsec_rx_sa *rx_sa;
			struct macsec_tx_sa *tx_sa;
		};
	} sa;
	union {
		struct macsec_tx_sc_stats *tx_sc_stats;
		struct macsec_tx_sa_stats *tx_sa_stats;
		struct macsec_rx_sc_stats *rx_sc_stats;
		struct macsec_rx_sa_stats *rx_sa_stats;
		struct macsec_dev_stats  *dev_stats;
	} stats;
};

/**
 * struct macsec_ops - MACsec offloading operations
 * @mdo_dev_open: called when the MACsec interface transitions to the up state
 * @mdo_dev_stop: called when the MACsec interface transitions to the down
 *	state
 * @mdo_add_secy: called when a new SecY is added
 * @mdo_upd_secy: called when the SecY flags are changed or the MAC address of
 *	the MACsec interface is changed
 * @mdo_del_secy: called when the hw offload is disabled or the MACsec
 *	interface is removed
 * @mdo_add_rxsc: called when a new RX SC is added
 * @mdo_upd_rxsc: called when a certain RX SC is updated
 * @mdo_del_rxsc: called when a certain RX SC is removed
 * @mdo_add_rxsa: called when a new RX SA is added
 * @mdo_upd_rxsa: called when a certain RX SA is updated
 * @mdo_del_rxsa: called when a certain RX SA is removed
 * @mdo_add_txsa: called when a new TX SA is added
 * @mdo_upd_txsa: called when a certain TX SA is updated
 * @mdo_del_txsa: called when a certain TX SA is removed
 * @mdo_get_dev_stats: called when dev stats are read
 * @mdo_get_tx_sc_stats: called when TX SC stats are read
 * @mdo_get_tx_sa_stats: called when TX SA stats are read
 * @mdo_get_rx_sc_stats: called when RX SC stats are read
 * @mdo_get_rx_sa_stats: called when RX SA stats are read
 * @mdo_insert_tx_tag: called to insert the TX tag
 * @needed_headroom: number of bytes reserved at the beginning of the sk_buff
 *	for the TX tag
 * @needed_tailroom: number of bytes reserved at the end of the sk_buff for the
 *	TX tag
 * @rx_uses_md_dst: whether MACsec device offload supports sk_buff md_dst
 */
struct macsec_ops {
	/* Device wide */
	int (*mdo_dev_open)(struct macsec_context *ctx);
	int (*mdo_dev_stop)(struct macsec_context *ctx);
	/* SecY */
	int (*mdo_add_secy)(struct macsec_context *ctx);
	int (*mdo_upd_secy)(struct macsec_context *ctx);
	int (*mdo_del_secy)(struct macsec_context *ctx);
	/* Security channels */
	int (*mdo_add_rxsc)(struct macsec_context *ctx);
	int (*mdo_upd_rxsc)(struct macsec_context *ctx);
	int (*mdo_del_rxsc)(struct macsec_context *ctx);
	/* Security associations */
	int (*mdo_add_rxsa)(struct macsec_context *ctx);
	int (*mdo_upd_rxsa)(struct macsec_context *ctx);
	int (*mdo_del_rxsa)(struct macsec_context *ctx);
	int (*mdo_add_txsa)(struct macsec_context *ctx);
	int (*mdo_upd_txsa)(struct macsec_context *ctx);
	int (*mdo_del_txsa)(struct macsec_context *ctx);
	/* Statistics */
	int (*mdo_get_dev_stats)(struct macsec_context *ctx);
	int (*mdo_get_tx_sc_stats)(struct macsec_context *ctx);
	int (*mdo_get_tx_sa_stats)(struct macsec_context *ctx);
	int (*mdo_get_rx_sc_stats)(struct macsec_context *ctx);
	int (*mdo_get_rx_sa_stats)(struct macsec_context *ctx);
	/* Offload tag */
	int (*mdo_insert_tx_tag)(struct phy_device *phydev,
				 struct sk_buff *skb);
	unsigned int needed_headroom;
	unsigned int needed_tailroom;
	bool rx_uses_md_dst;
};

void macsec_pn_wrapped(struct macsec_secy *secy, struct macsec_tx_sa *tx_sa);
static inline bool macsec_send_sci(const struct macsec_secy *secy)
{
	const struct macsec_tx_sc *tx_sc = &secy->tx_sc;

	return tx_sc->send_sci ||
		(secy->n_rx_sc > 1 && !tx_sc->end_station && !tx_sc->scb);
}
struct net_device *macsec_get_real_dev(const struct net_device *dev);
bool macsec_netdev_is_offloaded(struct net_device *dev);

static inline void *macsec_netdev_priv(const struct net_device *dev)
{
#if IS_ENABLED(CONFIG_VLAN_8021Q)
	if (is_vlan_dev(dev))
		return netdev_priv(vlan_dev_priv(dev)->real_dev);
#endif
	return netdev_priv(dev);
}

static inline u64 sci_to_cpu(sci_t sci)
{
	return be64_to_cpu((__force __be64)sci);
}

#endif /* _NET_MACSEC_H_ */
