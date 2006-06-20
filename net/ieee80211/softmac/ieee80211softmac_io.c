/* 
 * Some parts based on code from net80211
 * Copyright (c) 2001 Atsushi Onoe
 * Copyright (c) 2002-2005 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 */

#include "ieee80211softmac_priv.h"

/* Helper functions for inserting data into the frames */

/* 
 * Adds an ESSID element to the frame
 *
 */
static u8 *
ieee80211softmac_add_essid(u8 *dst, struct ieee80211softmac_essid *essid)
{
	if (essid) {
		*dst++ = MFIE_TYPE_SSID;
		*dst++ = essid->len;
		memcpy(dst, essid->data, essid->len);
		return dst+essid->len;
	} else {
		*dst++ = MFIE_TYPE_SSID;
		*dst++ = 0;
		return dst;
	}
}     

/* Adds Supported Rates and if required Extended Rates Information Element
 * to the frame, ASSUMES WE HAVE A SORTED LIST OF RATES */
static u8 *
ieee80211softmac_frame_add_rates(u8 *dst, const struct ieee80211softmac_ratesinfo *r)
{
	int cck_len, ofdm_len;
	*dst++ = MFIE_TYPE_RATES;

	for(cck_len=0; ieee80211_is_cck_rate(r->rates[cck_len]) && (cck_len < r->count);cck_len++);

	if(cck_len > IEEE80211SOFTMAC_MAX_RATES_LEN)
		cck_len = IEEE80211SOFTMAC_MAX_RATES_LEN;
	*dst++ = cck_len;
	memcpy(dst, r->rates, cck_len);
	dst += cck_len;

	if(cck_len < r->count){
		for (ofdm_len=0; ieee80211_is_ofdm_rate(r->rates[ofdm_len + cck_len]) && (ofdm_len + cck_len < r->count); ofdm_len++);
		if (ofdm_len > 0) {
			if (ofdm_len > IEEE80211SOFTMAC_MAX_EX_RATES_LEN)
				ofdm_len = IEEE80211SOFTMAC_MAX_EX_RATES_LEN;
			*dst++ = MFIE_TYPE_RATES_EX;
			*dst++ = ofdm_len;
			memcpy(dst, r->rates + cck_len, ofdm_len);
			dst += ofdm_len;
		}
	}	
	return dst;
}

/* Allocate a management frame */
static u8 * 
ieee80211softmac_alloc_mgt(u32 size)
{
	u8 * data;
	
	/* Add the header and FCS to the size */
	size = size + IEEE80211_3ADDR_LEN;	
	if(size > IEEE80211_DATA_LEN)
		return NULL;
	/* Allocate the frame */
	data = kmalloc(size, GFP_ATOMIC);
	memset(data, 0, size);
	return data;
}

/*
 * Add a 2 Address Header
 */
static void 
ieee80211softmac_hdr_2addr(struct ieee80211softmac_device *mac,
	struct ieee80211_hdr_2addr *header, u32 type, u8 *dest)
{
	/* Fill in the frame control flags */
	header->frame_ctl = cpu_to_le16(type);
	/* Control packets always have WEP turned off */	
	if(type > IEEE80211_STYPE_CFENDACK && type < IEEE80211_STYPE_PSPOLL)
		header->frame_ctl |= mac->ieee->sec.level ? cpu_to_le16(IEEE80211_FCTL_PROTECTED) : 0;

	/* Fill in the duration */
	header->duration_id = 0;
	/* FIXME: How do I find this?
	 * calculate. But most drivers just fill in 0 (except if it's a station id of course) */

	/* Fill in the Destination Address */
	if(dest == NULL)
		memset(header->addr1, 0xFF, ETH_ALEN);
	else
		memcpy(header->addr1, dest, ETH_ALEN);
	/* Fill in the Source Address */
	memcpy(header->addr2, mac->ieee->dev->dev_addr, ETH_ALEN);

}


/* Add a 3 Address Header */
static void 
ieee80211softmac_hdr_3addr(struct ieee80211softmac_device *mac,
	struct ieee80211_hdr_3addr *header, u32 type, u8 *dest, u8 *bssid)
{
	/* This is common with 2addr, so use that instead */
	ieee80211softmac_hdr_2addr(mac, (struct ieee80211_hdr_2addr *)header, type, dest);	
	
	/* Fill in the BSS ID */
	if(bssid == NULL)
		memset(header->addr3, 0xFF, ETH_ALEN);
	else
		memcpy(header->addr3, bssid, ETH_ALEN);

	/* Fill in the sequence # */
	/* FIXME: I need to add this to the softmac struct
	 * shouldn't the sequence number be in ieee80211? */
}

static u16
ieee80211softmac_capabilities(struct ieee80211softmac_device *mac,
	struct ieee80211softmac_network *net)
{
	u16 capability = 0;

	/* ESS and IBSS bits are set according to the current mode */
	switch (mac->ieee->iw_mode) {
	case IW_MODE_INFRA:
		capability = cpu_to_le16(WLAN_CAPABILITY_ESS);
		break;
	case IW_MODE_ADHOC:
		capability = cpu_to_le16(WLAN_CAPABILITY_IBSS);
		break;
	case IW_MODE_AUTO:
		capability = net->capabilities &
			(WLAN_CAPABILITY_ESS|WLAN_CAPABILITY_IBSS);
		break;
	default:
		/* bleh. we don't ever go to these modes */
		printk(KERN_ERR PFX "invalid iw_mode!\n");
		break;
	}

	/* CF Pollable / CF Poll Request */
	/* Needs to be implemented, for now, the 0's == not supported */

	/* Privacy Bit */
	capability |= mac->ieee->sec.level ?
		cpu_to_le16(WLAN_CAPABILITY_PRIVACY) : 0;

	/* Short Preamble */
	/* Always supported: we probably won't ever be powering devices which
	 * dont support this... */
	capability |= WLAN_CAPABILITY_SHORT_PREAMBLE;

	/* PBCC */
	/* Not widely used */

	/* Channel Agility */
	/* Not widely used */

	/* Short Slot */
	/* Will be implemented later */

	/* DSSS-OFDM */
	/* Not widely used */

	return capability;
}

/*****************************************************************************
 * Create Management packets
 *****************************************************************************/ 

/* Creates an association request packet */
static u32
ieee80211softmac_assoc_req(struct ieee80211_assoc_request **pkt, 
	struct ieee80211softmac_device *mac, struct ieee80211softmac_network *net)
{
	u8 *data;
	(*pkt) = (struct ieee80211_assoc_request *)ieee80211softmac_alloc_mgt(
		2 +		/* Capability Info */
		2 +	 	/* Listen Interval */
		/* SSID IE */
		1 + 1 + IW_ESSID_MAX_SIZE +
		/* Rates IE */
		1 + 1 + IEEE80211SOFTMAC_MAX_RATES_LEN +
		/* Extended Rates IE */
		1 + 1 + IEEE80211SOFTMAC_MAX_EX_RATES_LEN +
		/* WPA IE if present */
		mac->wpa.IElen
		/* Other IE's?  Optional?
		 * Yeah, probably need an extra IE parameter -- lots of vendors like to
		 * fill in their own IEs */
	);
	if (unlikely((*pkt) == NULL))
		return 0;
	ieee80211softmac_hdr_3addr(mac, &((*pkt)->header), IEEE80211_STYPE_ASSOC_REQ, net->bssid, net->bssid);

	/* Fill in Listen Interval (?) */
	(*pkt)->listen_interval = cpu_to_le16(10);
	
	data = (u8 *)(*pkt)->info_element;
	/* Add SSID */
	data = ieee80211softmac_add_essid(data, &net->essid);
	/* Add Rates */
	data = ieee80211softmac_frame_add_rates(data, &mac->ratesinfo);
	/* Add WPA IE */
	if (mac->wpa.IElen && mac->wpa.IE) {
		memcpy(data, mac->wpa.IE, mac->wpa.IElen);
		data += mac->wpa.IElen;
	}
	/* Return the number of used bytes */
	return (data - (u8*)(*pkt));
}

/* Create a reassociation request packet */
static u32
ieee80211softmac_reassoc_req(struct ieee80211_reassoc_request **pkt, 
	struct ieee80211softmac_device *mac, struct ieee80211softmac_network *net)
{
	u8 *data;
	(*pkt) = (struct ieee80211_reassoc_request *)ieee80211softmac_alloc_mgt(
		2 +		/* Capability Info */
		2 +	 	/* Listen Interval */
		ETH_ALEN +	/* AP MAC */
		/* SSID IE */
		1 + 1 + IW_ESSID_MAX_SIZE +
		/* Rates IE */
		1 + 1 + IEEE80211SOFTMAC_MAX_RATES_LEN +
		/* Extended Rates IE */
		1 + 1 + IEEE80211SOFTMAC_MAX_EX_RATES_LEN 
		/* Other IE's? */
	);				
	if (unlikely((*pkt) == NULL))
		return 0;
	ieee80211softmac_hdr_3addr(mac, &((*pkt)->header), IEEE80211_STYPE_REASSOC_REQ, net->bssid, net->bssid);

	/* Fill in the capabilities */
	(*pkt)->capability = ieee80211softmac_capabilities(mac, net);

	/* Fill in Listen Interval (?) */
	(*pkt)->listen_interval = cpu_to_le16(10);
	/* Fill in the current AP MAC */
	memcpy((*pkt)->current_ap, mac->ieee->bssid, ETH_ALEN);
	
	data = (u8 *)(*pkt)->info_element;
	/* Add SSID */
	data = ieee80211softmac_add_essid(data, &net->essid); 
	/* Add Rates */
	data = ieee80211softmac_frame_add_rates(data, &mac->ratesinfo);
	/* Return packet size */
	return (data - (u8 *)(*pkt));
}

/* Create an authentication packet */
static u32
ieee80211softmac_auth(struct ieee80211_auth **pkt, 
	struct ieee80211softmac_device *mac, struct ieee80211softmac_network *net,
	u16 transaction, u16 status, int *encrypt_mpdu)
{
	u8 *data;
	int auth_mode = mac->ieee->sec.auth_mode;
	int is_shared_response = (auth_mode == WLAN_AUTH_SHARED_KEY
		&& transaction == IEEE80211SOFTMAC_AUTH_SHARED_RESPONSE);

	/* Allocate Packet */
	(*pkt) = (struct ieee80211_auth *)ieee80211softmac_alloc_mgt(
		2 +		/* Auth Algorithm */
		2 +		/* Auth Transaction Seq */
		2 +		/* Status Code */
		 /* Challenge Text IE */
		is_shared_response ? 0 : 1 + 1 + net->challenge_len
	);
	if (unlikely((*pkt) == NULL))
		return 0;
	ieee80211softmac_hdr_3addr(mac, &((*pkt)->header), IEEE80211_STYPE_AUTH, net->bssid, net->bssid);
		
	/* Algorithm */
	(*pkt)->algorithm = cpu_to_le16(auth_mode);
	/* Transaction */
	(*pkt)->transaction = cpu_to_le16(transaction);
	/* Status */
	(*pkt)->status = cpu_to_le16(status);
	
	data = (u8 *)(*pkt)->info_element;
	/* Challenge Text */
	if (is_shared_response) {
		*data = MFIE_TYPE_CHALLENGE;
		data++;
		
		/* Copy the challenge in */
		*data = net->challenge_len;
		data++;
		memcpy(data, net->challenge, net->challenge_len);
		data += net->challenge_len;

		/* Make sure this frame gets encrypted with the shared key */
		*encrypt_mpdu = 1;
	} else
		*encrypt_mpdu = 0;

	/* Return the packet size */
	return (data - (u8 *)(*pkt));
}

/* Create a disassocation or deauthentication packet */
static u32
ieee80211softmac_disassoc_deauth(struct ieee80211_disassoc **pkt,
	struct ieee80211softmac_device *mac, struct ieee80211softmac_network *net,
	u16 type, u16 reason)
{
	/* Allocate Packet */
	(*pkt) = (struct ieee80211_disassoc *)ieee80211softmac_alloc_mgt(2);
	if (unlikely((*pkt) == NULL))
		return 0;
	ieee80211softmac_hdr_3addr(mac, &((*pkt)->header), type, net->bssid, net->bssid);
	/* Reason */
	(*pkt)->reason = cpu_to_le16(reason);
	/* Return the packet size */
	return (2 + IEEE80211_3ADDR_LEN);
}

/* Create a probe request packet */
static u32
ieee80211softmac_probe_req(struct ieee80211_probe_request **pkt,
	struct ieee80211softmac_device *mac, struct ieee80211softmac_essid *essid)
{
	u8 *data;	
	/* Allocate Packet */
	(*pkt) = (struct ieee80211_probe_request *)ieee80211softmac_alloc_mgt(
		/* SSID of requested network */
		1 + 1 + IW_ESSID_MAX_SIZE +
		/* Rates IE */
		1 + 1 + IEEE80211SOFTMAC_MAX_RATES_LEN +
		/* Extended Rates IE */
		1 + 1 + IEEE80211SOFTMAC_MAX_EX_RATES_LEN 
	);
	if (unlikely((*pkt) == NULL))
		return 0;
	ieee80211softmac_hdr_3addr(mac, &((*pkt)->header), IEEE80211_STYPE_PROBE_REQ, NULL, NULL);
		
	data = (u8 *)(*pkt)->info_element;
	/* Add ESSID (can be NULL) */
	data = ieee80211softmac_add_essid(data, essid);
	/* Add Rates */
	data = ieee80211softmac_frame_add_rates(data, &mac->ratesinfo);
	/* Return packet size */
	return (data - (u8 *)(*pkt));
}

/* Create a probe response packet */
/* FIXME: Not complete */
static u32
ieee80211softmac_probe_resp(struct ieee80211_probe_response **pkt,
	struct ieee80211softmac_device *mac, struct ieee80211softmac_network *net)
{
	u8 *data;
	/* Allocate Packet */
	(*pkt) = (struct ieee80211_probe_response *)ieee80211softmac_alloc_mgt(
		8 +		/* Timestamp */
		2 +		/* Beacon Interval */
		2 +		/* Capability Info */
				/* SSID IE */
		1 + 1 + IW_ESSID_MAX_SIZE +
		7 + 		/* FH Parameter Set */
		2 +		/* DS Parameter Set */
		8 +		/* CF Parameter Set */
		4 		/* IBSS Parameter Set */
	);	
	if (unlikely((*pkt) == NULL))
		return 0;
	ieee80211softmac_hdr_3addr(mac, &((*pkt)->header), IEEE80211_STYPE_PROBE_RESP, net->bssid, net->bssid);
	data = (u8 *)(*pkt)->info_element;

	/* Return the packet size */
	return (data - (u8 *)(*pkt));
}


/* Sends a manangement packet
 * FIXME: document the use of the arg parameter
 * for _AUTH: (transaction #) | (status << 16)
 */
int
ieee80211softmac_send_mgt_frame(struct ieee80211softmac_device *mac,
	void *ptrarg, u32 type, u32 arg)
{
	void *pkt = NULL;
	u32 pkt_size = 0;
	int encrypt_mpdu = 0;

	switch(type) {
	case IEEE80211_STYPE_ASSOC_REQ:
		pkt_size = ieee80211softmac_assoc_req((struct ieee80211_assoc_request **)(&pkt), mac, (struct ieee80211softmac_network *)ptrarg);
		break;
	case IEEE80211_STYPE_REASSOC_REQ:
		pkt_size = ieee80211softmac_reassoc_req((struct ieee80211_reassoc_request **)(&pkt), mac, (struct ieee80211softmac_network *)ptrarg);
		break;
	case IEEE80211_STYPE_AUTH:
		pkt_size = ieee80211softmac_auth((struct ieee80211_auth **)(&pkt), mac, (struct ieee80211softmac_network *)ptrarg, (u16)(arg & 0xFFFF), (u16) (arg >> 16), &encrypt_mpdu);
		break;
	case IEEE80211_STYPE_DISASSOC:
	case IEEE80211_STYPE_DEAUTH:
		pkt_size = ieee80211softmac_disassoc_deauth((struct ieee80211_disassoc **)(&pkt), mac, (struct ieee80211softmac_network *)ptrarg, type, (u16)(arg & 0xFFFF));
		break;
	case IEEE80211_STYPE_PROBE_REQ:
		pkt_size = ieee80211softmac_probe_req((struct ieee80211_probe_request **)(&pkt), mac, (struct ieee80211softmac_essid *)ptrarg);
		break;
	case IEEE80211_STYPE_PROBE_RESP:
		pkt_size = ieee80211softmac_probe_resp((struct ieee80211_probe_response **)(&pkt), mac, (struct ieee80211softmac_network *)ptrarg);
		break;
	default:
                printkl(KERN_DEBUG PFX "Unsupported Management Frame type: %i\n", type);
                return -EINVAL;
	};

	if(pkt_size == 0 || pkt == NULL) {
		printkl(KERN_DEBUG PFX "Error, packet is nonexistant or 0 length\n");
		return -ENOMEM;
	}
	
	/* Send the packet to the ieee80211 layer for tx */
	/* we defined softmac->mgmt_xmit for this. Should we keep it
	 * as it is (that means we'd need to wrap this into a txb),
	 * modify the prototype (so it matches this function),
	 * or get rid of it alltogether?
	 * Does this work for you now?
	 */
	ieee80211_tx_frame(mac->ieee, (struct ieee80211_hdr *)pkt,
		IEEE80211_3ADDR_LEN, pkt_size, encrypt_mpdu);

	kfree(pkt);
	return 0;
}
