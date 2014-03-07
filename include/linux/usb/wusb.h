/*
 * Wireless USB Standard Definitions
 * Event Size Tables
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * FIXME: docs
 * FIXME: organize properly, group logically
 *
 * All the event structures are defined in uwb/spec.h, as they are
 * common to the WHCI and WUSB radio control interfaces.
 */

#ifndef __WUSB_H__
#define __WUSB_H__

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/uwb/spec.h>
#include <linux/usb/ch9.h>
#include <linux/param.h>

/**
 * WUSB Information Element header
 *
 * I don't know why, they decided to make it different to the MBOA MAC
 * IE Header; beats me.
 */
struct wuie_hdr {
	u8 bLength;
	u8 bIEIdentifier;
} __attribute__((packed));

enum {
	WUIE_ID_WCTA = 0x80,
	WUIE_ID_CONNECTACK,
	WUIE_ID_HOST_INFO,
	WUIE_ID_CHANGE_ANNOUNCE,
	WUIE_ID_DEVICE_DISCONNECT,
	WUIE_ID_HOST_DISCONNECT,
	WUIE_ID_KEEP_ALIVE = 0x89,
	WUIE_ID_ISOCH_DISCARD,
	WUIE_ID_RESET_DEVICE,
};

/**
 * Maximum number of array elements in a WUSB IE.
 *
 * WUSB1.0[7.5 before table 7-38] says that in WUSB IEs that
 * are "arrays" have to limited to 4 elements. So we define it
 * like that to ease up and submit only the neeed size.
 */
#define WUIE_ELT_MAX 4

/**
 * Wrapper for the data that defines a CHID, a CDID or a CK
 *
 * WUSB defines that CHIDs, CDIDs and CKs are a 16 byte string of
 * data. In order to avoid confusion and enforce types, we wrap it.
 *
 * Make it packed, as we use it in some hw definitions.
 */
struct wusb_ckhdid {
	u8 data[16];
} __attribute__((packed));

static const struct wusb_ckhdid wusb_ckhdid_zero = { .data = { 0 } };

#define WUSB_CKHDID_STRSIZE (3 * sizeof(struct wusb_ckhdid) + 1)

/**
 * WUSB IE: Host Information (WUSB1.0[7.5.2])
 *
 * Used to provide information about the host to the Wireless USB
 * devices in range (CHID can be used as an ASCII string).
 */
struct wuie_host_info {
	struct wuie_hdr hdr;
	__le16 attributes;
	struct wusb_ckhdid CHID;
} __attribute__((packed));

/**
 * WUSB IE: Connect Ack (WUSB1.0[7.5.1])
 *
 * Used to acknowledge device connect requests. See note for
 * WUIE_ELT_MAX.
 */
struct wuie_connect_ack {
	struct wuie_hdr hdr;
	struct {
		struct wusb_ckhdid CDID;
		u8 bDeviceAddress;	/* 0 means unused */
		u8 bReserved;
	} blk[WUIE_ELT_MAX];
} __attribute__((packed));

/**
 * WUSB IE Host Information Element, Connect Availability
 *
 * WUSB1.0[7.5.2], bmAttributes description
 */
enum {
	WUIE_HI_CAP_RECONNECT = 0,
	WUIE_HI_CAP_LIMITED,
	WUIE_HI_CAP_RESERVED,
	WUIE_HI_CAP_ALL,
};

/**
 * WUSB IE: Channel Stop (WUSB1.0[7.5.8])
 *
 * Tells devices the host is going to stop sending MMCs and will disappear.
 */
struct wuie_channel_stop {
	struct wuie_hdr hdr;
	u8 attributes;
	u8 timestamp[3];
} __attribute__((packed));

/**
 * WUSB IE: Keepalive (WUSB1.0[7.5.9])
 *
 * Ask device(s) to send keepalives.
 */
struct wuie_keep_alive {
	struct wuie_hdr hdr;
	u8 bDeviceAddress[WUIE_ELT_MAX];
} __attribute__((packed));

/**
 * WUSB IE: Reset device (WUSB1.0[7.5.11])
 *
 * Tell device to reset; in all truth, we can fit 4 CDIDs, but we only
 * use it for one at the time...
 *
 * In any case, this request is a wee bit silly: why don't they target
 * by address??
 */
struct wuie_reset {
	struct wuie_hdr hdr;
	struct wusb_ckhdid CDID;
} __attribute__((packed));

/**
 * WUSB IE: Disconnect device (WUSB1.0[7.5.11])
 *
 * Tell device to disconnect; we can fit 4 addresses, but we only use
 * it for one at the time...
 */
struct wuie_disconnect {
	struct wuie_hdr hdr;
	u8 bDeviceAddress;
	u8 padding;
} __attribute__((packed));

/**
 * WUSB IE: Host disconnect ([WUSB] section 7.5.5)
 *
 * Tells all connected devices to disconnect.
 */
struct wuie_host_disconnect {
	struct wuie_hdr hdr;
} __attribute__((packed));

/**
 * WUSB Device Notification header (WUSB1.0[7.6])
 */
struct wusb_dn_hdr {
	u8 bType;
	u8 notifdata[];
} __attribute__((packed));

/** Device Notification codes (WUSB1.0[Table 7-54]) */
enum WUSB_DN {
	WUSB_DN_CONNECT = 0x01,
	WUSB_DN_DISCONNECT = 0x02,
	WUSB_DN_EPRDY = 0x03,
	WUSB_DN_MASAVAILCHANGED = 0x04,
	WUSB_DN_RWAKE = 0x05,
	WUSB_DN_SLEEP = 0x06,
	WUSB_DN_ALIVE = 0x07,
};

/** WUSB Device Notification Connect */
struct wusb_dn_connect {
	struct wusb_dn_hdr hdr;
	__le16 attributes;
	struct wusb_ckhdid CDID;
} __attribute__((packed));

static inline int wusb_dn_connect_prev_dev_addr(const struct wusb_dn_connect *dn)
{
	return le16_to_cpu(dn->attributes) & 0xff;
}

static inline int wusb_dn_connect_new_connection(const struct wusb_dn_connect *dn)
{
	return (le16_to_cpu(dn->attributes) >> 8) & 0x1;
}

static inline int wusb_dn_connect_beacon_behavior(const struct wusb_dn_connect *dn)
{
	return (le16_to_cpu(dn->attributes) >> 9) & 0x03;
}

/** Device is alive (aka: pong) (WUSB1.0[7.6.7]) */
struct wusb_dn_alive {
	struct wusb_dn_hdr hdr;
} __attribute__((packed));

/** Device is disconnecting (WUSB1.0[7.6.2]) */
struct wusb_dn_disconnect {
	struct wusb_dn_hdr hdr;
} __attribute__((packed));

/* General constants */
enum {
	WUSB_TRUST_TIMEOUT_MS = 4000,	/* [WUSB] section 4.15.1 */
};

static inline size_t ckhdid_printf(char *pr_ckhdid, size_t size,
				   const struct wusb_ckhdid *ckhdid)
{
	return scnprintf(pr_ckhdid, size,
			 "%02hx %02hx %02hx %02hx %02hx %02hx %02hx %02hx "
			 "%02hx %02hx %02hx %02hx %02hx %02hx %02hx %02hx",
			 ckhdid->data[0],  ckhdid->data[1],
			 ckhdid->data[2],  ckhdid->data[3],
			 ckhdid->data[4],  ckhdid->data[5],
			 ckhdid->data[6],  ckhdid->data[7],
			 ckhdid->data[8],  ckhdid->data[9],
			 ckhdid->data[10], ckhdid->data[11],
			 ckhdid->data[12], ckhdid->data[13],
			 ckhdid->data[14], ckhdid->data[15]);
}

/*
 * WUSB Crypto stuff (WUSB1.0[6])
 */

extern const char *wusb_et_name(u8);

/**
 * WUSB key index WUSB1.0[7.3.2.4], for usage when setting keys for
 * the host or the device.
 */
static inline u8 wusb_key_index(int index, int type, int originator)
{
	return (originator << 6) | (type << 4) | index;
}

#define WUSB_KEY_INDEX_TYPE_PTK			0 /* for HWA only */
#define WUSB_KEY_INDEX_TYPE_ASSOC		1
#define WUSB_KEY_INDEX_TYPE_GTK			2
#define WUSB_KEY_INDEX_ORIGINATOR_HOST		0
#define WUSB_KEY_INDEX_ORIGINATOR_DEVICE	1
/* bits 0-3 used for the key index. */
#define WUSB_KEY_INDEX_MAX			15

/* A CCM Nonce, defined in WUSB1.0[6.4.1] */
struct aes_ccm_nonce {
	u8 sfn[6];              /* Little Endian */
	u8 tkid[3];             /* LE */
	struct uwb_dev_addr dest_addr;
	struct uwb_dev_addr src_addr;
} __attribute__((packed));

/* A CCM operation label, defined on WUSB1.0[6.5.x] */
struct aes_ccm_label {
	u8 data[14];
} __attribute__((packed));

/*
 * Input to the key derivation sequence defined in
 * WUSB1.0[6.5.1]. Rest of the data is in the CCM Nonce passed to the
 * PRF function.
 */
struct wusb_keydvt_in {
	u8 hnonce[16];
	u8 dnonce[16];
} __attribute__((packed));

/*
 * Output from the key derivation sequence defined in
 * WUSB1.0[6.5.1].
 */
struct wusb_keydvt_out {
	u8 kck[16];
	u8 ptk[16];
} __attribute__((packed));

/* Pseudo Random Function WUSB1.0[6.5] */
extern int wusb_crypto_init(void);
extern void wusb_crypto_exit(void);
extern ssize_t wusb_prf(void *out, size_t out_size,
			const u8 key[16], const struct aes_ccm_nonce *_n,
			const struct aes_ccm_label *a,
			const void *b, size_t blen, size_t len);

static inline int wusb_prf_64(void *out, size_t out_size, const u8 key[16],
			      const struct aes_ccm_nonce *n,
			      const struct aes_ccm_label *a,
			      const void *b, size_t blen)
{
	return wusb_prf(out, out_size, key, n, a, b, blen, 64);
}

static inline int wusb_prf_128(void *out, size_t out_size, const u8 key[16],
			       const struct aes_ccm_nonce *n,
			       const struct aes_ccm_label *a,
			       const void *b, size_t blen)
{
	return wusb_prf(out, out_size, key, n, a, b, blen, 128);
}

static inline int wusb_prf_256(void *out, size_t out_size, const u8 key[16],
			       const struct aes_ccm_nonce *n,
			       const struct aes_ccm_label *a,
			       const void *b, size_t blen)
{
	return wusb_prf(out, out_size, key, n, a, b, blen, 256);
}

/* Key derivation WUSB1.0[6.5.1] */
static inline int wusb_key_derive(struct wusb_keydvt_out *keydvt_out,
				  const u8 key[16],
				  const struct aes_ccm_nonce *n,
				  const struct wusb_keydvt_in *keydvt_in)
{
	const struct aes_ccm_label a = { .data = "Pair-wise keys" };
	return wusb_prf_256(keydvt_out, sizeof(*keydvt_out), key, n, &a,
			    keydvt_in, sizeof(*keydvt_in));
}

/*
 * Out-of-band MIC Generation WUSB1.0[6.5.2]
 *
 * Compute the MIC over @key, @n and @hs and place it in @mic_out.
 *
 * @mic_out:  Where to place the 8 byte MIC tag
 * @key:      KCK from the derivation process
 * @n:        CCM nonce, n->sfn == 0, TKID as established in the
 *            process.
 * @hs:       Handshake struct for phase 2 of the 4-way.
 *            hs->bStatus and hs->bReserved are zero.
 *            hs->bMessageNumber is 2 (WUSB1.0[7.3.2.5.2]
 *            hs->dest_addr is the device's USB address padded with 0
 *            hs->src_addr is the hosts's UWB device address
 *            hs->mic is ignored (as we compute that value).
 */
static inline int wusb_oob_mic(u8 mic_out[8], const u8 key[16],
			       const struct aes_ccm_nonce *n,
			       const struct usb_handshake *hs)
{
	const struct aes_ccm_label a = { .data = "out-of-bandMIC" };
	return wusb_prf_64(mic_out, 8, key, n, &a,
			   hs, sizeof(*hs) - sizeof(hs->MIC));
}

#endif /* #ifndef __WUSB_H__ */
