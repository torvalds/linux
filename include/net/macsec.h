/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * MACsec netdev header, used for h/w accelerated implementations.
 *
 * Copyright (c) 2015 Sabrina Dubroca <sd@queasysnail.net>
 */
#ifndef _NET_MACSEC_H_
#define _NET_MACSEC_H_

#include <linux/u64_stats_sync.h>
#include <uapi/linux/if_link.h>
#include <uapi/linux/if_macsec.h>

typedef u64 __bitwise sci_t;

#define MACSEC_NUM_AN 4 /* 2 bits for the association number */

/**
 * struct macsec_key - SA key
 * @id: user-provided key identifier
 * @tfm: crypto struct, key storage
 */
struct macsec_key {
	u8 id[MACSEC_KEYID_LEN];
	struct crypto_aead *tfm;
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

/**
 * struct macsec_rx_sa - receive secure association
 * @active:
 * @next_pn: packet number expected for the next packet
 * @lock: protects next_pn manipulations
 * @key: key structure
 * @stats: per-SA stats
 */
struct macsec_rx_sa {
	struct macsec_key key;
	spinlock_t lock;
	u32 next_pn;
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
 * @stats: per-SA stats
 */
struct macsec_tx_sa {
	struct macsec_key key;
	spinlock_t lock;
	u32 next_pn;
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
};

/**
 * struct macsec_secy - MACsec Security Entity
 * @netdev: netdevice for this SecY
 * @n_rx_sc: number of receive secure channels configured on this SecY
 * @sci: secure channel identifier used for tx
 * @key_len: length of keys used by the cipher suite
 * @icv_len: length of ICV used by the cipher suite
 * @validate_frames: validation mode
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
	bool operational;
	bool protect_frames;
	bool replay_protect;
	u32 replay_window;
	struct macsec_tx_sc tx_sc;
	struct macsec_rx_sc __rcu *rx_sc;
};

/**
 * struct macsec_context - MACsec context for hardware offloading
 */
struct macsec_context {
	struct phy_device *phydev;
	enum macsec_offload offload;

	struct macsec_secy *secy;
	struct macsec_rx_sc *rx_sc;
	struct {
		unsigned char assoc_num;
		u8 key[MACSEC_KEYID_LEN];
		union {
			struct macsec_rx_sa *rx_sa;
			struct macsec_tx_sa *tx_sa;
		};
	} sa;

	u8 prepare:1;
};

/**
 * struct macsec_ops - MACsec offloading operations
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
};

void macsec_pn_wrapped(struct macsec_secy *secy, struct macsec_tx_sa *tx_sa);

#endif /* _NET_MACSEC_H_ */
