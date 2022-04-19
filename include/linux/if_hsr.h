/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_IF_HSR_H_
#define _LINUX_IF_HSR_H_

/* used to differentiate various protocols */
enum hsr_version {
	HSR_V0 = 0,
	HSR_V1,
	PRP_V1,
};

/* HSR Tag.
 * As defined in IEC-62439-3:2010, the HSR tag is really { ethertype = 0x88FB,
 * path, LSDU_size, sequence Nr }. But we let eth_header() create { h_dest,
 * h_source, h_proto = 0x88FB }, and add { path, LSDU_size, sequence Nr,
 * encapsulated protocol } instead.
 *
 * Field names as defined in the IEC:2010 standard for HSR.
 */
struct hsr_tag {
	__be16		path_and_LSDU_size;
	__be16		sequence_nr;
	__be16		encap_proto;
} __packed;

#define HSR_HLEN	6

#if IS_ENABLED(CONFIG_HSR)
extern bool is_hsr_master(struct net_device *dev);
extern int hsr_get_version(struct net_device *dev, enum hsr_version *ver);
#else
static inline bool is_hsr_master(struct net_device *dev)
{
	return false;
}
static inline int hsr_get_version(struct net_device *dev,
				  enum hsr_version *ver)
{
	return -EINVAL;
}
#endif /* CONFIG_HSR */

#endif /*_LINUX_IF_HSR_H_*/
