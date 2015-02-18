/*
 * Description: EBTables 802.1Q match extension kernelspace module.
 * Authors: Nick Fedchik <nick@fedchik.org.ua>
 *          Bart De Schuymer <bdschuym@pandora.be>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_vlan.h>

#define MODULE_VERS "0.6"

MODULE_AUTHOR("Nick Fedchik <nick@fedchik.org.ua>");
MODULE_DESCRIPTION("Ebtables: 802.1Q VLAN tag match");
MODULE_LICENSE("GPL");

#define GET_BITMASK(_BIT_MASK_) info->bitmask & _BIT_MASK_
#define EXIT_ON_MISMATCH(_MATCH_,_MASK_) {if (!((info->_MATCH_ == _MATCH_)^!!(info->invflags & _MASK_))) return false; }

static bool
ebt_vlan_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct ebt_vlan_info *info = par->matchinfo;

	unsigned short TCI;	/* Whole TCI, given from parsed frame */
	unsigned short id;	/* VLAN ID, given from frame TCI */
	unsigned char prio;	/* user_priority, given from frame TCI */
	/* VLAN encapsulated Type/Length field, given from orig frame */
	__be16 encap;

	if (skb_vlan_tag_present(skb)) {
		TCI = skb_vlan_tag_get(skb);
		encap = skb->protocol;
	} else {
		const struct vlan_hdr *fp;
		struct vlan_hdr _frame;

		fp = skb_header_pointer(skb, 0, sizeof(_frame), &_frame);
		if (fp == NULL)
			return false;

		TCI = ntohs(fp->h_vlan_TCI);
		encap = fp->h_vlan_encapsulated_proto;
	}

	/* Tag Control Information (TCI) consists of the following elements:
	 * - User_priority. The user_priority field is three bits in length,
	 * interpreted as a binary number.
	 * - Canonical Format Indicator (CFI). The Canonical Format Indicator
	 * (CFI) is a single bit flag value. Currently ignored.
	 * - VLAN Identifier (VID). The VID is encoded as
	 * an unsigned binary number. */
	id = TCI & VLAN_VID_MASK;
	prio = (TCI >> 13) & 0x7;

	/* Checking VLAN Identifier (VID) */
	if (GET_BITMASK(EBT_VLAN_ID))
		EXIT_ON_MISMATCH(id, EBT_VLAN_ID);

	/* Checking user_priority */
	if (GET_BITMASK(EBT_VLAN_PRIO))
		EXIT_ON_MISMATCH(prio, EBT_VLAN_PRIO);

	/* Checking Encapsulated Proto (Length/Type) field */
	if (GET_BITMASK(EBT_VLAN_ENCAP))
		EXIT_ON_MISMATCH(encap, EBT_VLAN_ENCAP);

	return true;
}

static int ebt_vlan_mt_check(const struct xt_mtchk_param *par)
{
	struct ebt_vlan_info *info = par->matchinfo;
	const struct ebt_entry *e = par->entryinfo;

	/* Is it 802.1Q frame checked? */
	if (e->ethproto != htons(ETH_P_8021Q)) {
		pr_debug("passed entry proto %2.4X is not 802.1Q (8100)\n",
			 ntohs(e->ethproto));
		return -EINVAL;
	}

	/* Check for bitmask range
	 * True if even one bit is out of mask */
	if (info->bitmask & ~EBT_VLAN_MASK) {
		pr_debug("bitmask %2X is out of mask (%2X)\n",
			 info->bitmask, EBT_VLAN_MASK);
		return -EINVAL;
	}

	/* Check for inversion flags range */
	if (info->invflags & ~EBT_VLAN_MASK) {
		pr_debug("inversion flags %2X is out of mask (%2X)\n",
			 info->invflags, EBT_VLAN_MASK);
		return -EINVAL;
	}

	/* Reserved VLAN ID (VID) values
	 * -----------------------------
	 * 0 - The null VLAN ID.
	 * 1 - The default Port VID (PVID)
	 * 0x0FFF - Reserved for implementation use.
	 * if_vlan.h: VLAN_N_VID 4096. */
	if (GET_BITMASK(EBT_VLAN_ID)) {
		if (!!info->id) { /* if id!=0 => check vid range */
			if (info->id > VLAN_N_VID) {
				pr_debug("id %d is out of range (1-4096)\n",
					 info->id);
				return -EINVAL;
			}
			/* Note: This is valid VLAN-tagged frame point.
			 * Any value of user_priority are acceptable,
			 * but should be ignored according to 802.1Q Std.
			 * So we just drop the prio flag. */
			info->bitmask &= ~EBT_VLAN_PRIO;
		}
		/* Else, id=0 (null VLAN ID)  => user_priority range (any?) */
	}

	if (GET_BITMASK(EBT_VLAN_PRIO)) {
		if ((unsigned char) info->prio > 7) {
			pr_debug("prio %d is out of range (0-7)\n",
				 info->prio);
			return -EINVAL;
		}
	}
	/* Check for encapsulated proto range - it is possible to be
	 * any value for u_short range.
	 * if_ether.h:  ETH_ZLEN        60   -  Min. octets in frame sans FCS */
	if (GET_BITMASK(EBT_VLAN_ENCAP)) {
		if ((unsigned short) ntohs(info->encap) < ETH_ZLEN) {
			pr_debug("encap frame length %d is less than "
				 "minimal\n", ntohs(info->encap));
			return -EINVAL;
		}
	}

	return 0;
}

static struct xt_match ebt_vlan_mt_reg __read_mostly = {
	.name		= "vlan",
	.revision	= 0,
	.family		= NFPROTO_BRIDGE,
	.match		= ebt_vlan_mt,
	.checkentry	= ebt_vlan_mt_check,
	.matchsize	= sizeof(struct ebt_vlan_info),
	.me		= THIS_MODULE,
};

static int __init ebt_vlan_init(void)
{
	pr_debug("ebtables 802.1Q extension module v" MODULE_VERS "\n");
	return xt_register_match(&ebt_vlan_mt_reg);
}

static void __exit ebt_vlan_fini(void)
{
	xt_unregister_match(&ebt_vlan_mt_reg);
}

module_init(ebt_vlan_init);
module_exit(ebt_vlan_fini);
