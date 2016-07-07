#ifndef __NET_NETLINK_H
#define __NET_NETLINK_H

#include <linux/types.h>
#include <linux/netlink.h>
#include <linux/jiffies.h>
#include <linux/in6.h>

/* ========================================================================
 *         Netlink Messages and Attributes Interface (As Seen On TV)
 * ------------------------------------------------------------------------
 *                          Messages Interface
 * ------------------------------------------------------------------------
 *
 * Message Format:
 *    <--- nlmsg_total_size(payload)  --->
 *    <-- nlmsg_msg_size(payload) ->
 *   +----------+- - -+-------------+- - -+-------- - -
 *   | nlmsghdr | Pad |   Payload   | Pad | nlmsghdr
 *   +----------+- - -+-------------+- - -+-------- - -
 *   nlmsg_data(nlh)---^                   ^
 *   nlmsg_next(nlh)-----------------------+
 *
 * Payload Format:
 *    <---------------------- nlmsg_len(nlh) --------------------->
 *    <------ hdrlen ------>       <- nlmsg_attrlen(nlh, hdrlen) ->
 *   +----------------------+- - -+--------------------------------+
 *   |     Family Header    | Pad |           Attributes           |
 *   +----------------------+- - -+--------------------------------+
 *   nlmsg_attrdata(nlh, hdrlen)---^
 *
 * Data Structures:
 *   struct nlmsghdr			netlink message header
 *
 * Message Construction:
 *   nlmsg_new()			create a new netlink message
 *   nlmsg_put()			add a netlink message to an skb
 *   nlmsg_put_answer()			callback based nlmsg_put()
 *   nlmsg_end()			finalize netlink message
 *   nlmsg_get_pos()			return current position in message
 *   nlmsg_trim()			trim part of message
 *   nlmsg_cancel()			cancel message construction
 *   nlmsg_free()			free a netlink message
 *
 * Message Sending:
 *   nlmsg_multicast()			multicast message to several groups
 *   nlmsg_unicast()			unicast a message to a single socket
 *   nlmsg_notify()			send notification message
 *
 * Message Length Calculations:
 *   nlmsg_msg_size(payload)		length of message w/o padding
 *   nlmsg_total_size(payload)		length of message w/ padding
 *   nlmsg_padlen(payload)		length of padding at tail
 *
 * Message Payload Access:
 *   nlmsg_data(nlh)			head of message payload
 *   nlmsg_len(nlh)			length of message payload
 *   nlmsg_attrdata(nlh, hdrlen)	head of attributes data
 *   nlmsg_attrlen(nlh, hdrlen)		length of attributes data
 *
 * Message Parsing:
 *   nlmsg_ok(nlh, remaining)		does nlh fit into remaining bytes?
 *   nlmsg_next(nlh, remaining)		get next netlink message
 *   nlmsg_parse()			parse attributes of a message
 *   nlmsg_find_attr()			find an attribute in a message
 *   nlmsg_for_each_msg()		loop over all messages
 *   nlmsg_validate()			validate netlink message incl. attrs
 *   nlmsg_for_each_attr()		loop over all attributes
 *
 * Misc:
 *   nlmsg_report()			report back to application?
 *
 * ------------------------------------------------------------------------
 *                          Attributes Interface
 * ------------------------------------------------------------------------
 *
 * Attribute Format:
 *    <------- nla_total_size(payload) ------->
 *    <---- nla_attr_size(payload) ----->
 *   +----------+- - -+- - - - - - - - - +- - -+-------- - -
 *   |  Header  | Pad |     Payload      | Pad |  Header
 *   +----------+- - -+- - - - - - - - - +- - -+-------- - -
 *                     <- nla_len(nla) ->      ^
 *   nla_data(nla)----^                        |
 *   nla_next(nla)-----------------------------'
 *
 * Data Structures:
 *   struct nlattr			netlink attribute header
 *
 * Attribute Construction:
 *   nla_reserve(skb, type, len)	reserve room for an attribute
 *   nla_reserve_nohdr(skb, len)	reserve room for an attribute w/o hdr
 *   nla_put(skb, type, len, data)	add attribute to skb
 *   nla_put_nohdr(skb, len, data)	add attribute w/o hdr
 *   nla_append(skb, len, data)		append data to skb
 *
 * Attribute Construction for Basic Types:
 *   nla_put_u8(skb, type, value)	add u8 attribute to skb
 *   nla_put_u16(skb, type, value)	add u16 attribute to skb
 *   nla_put_u32(skb, type, value)	add u32 attribute to skb
 *   nla_put_u64_64bits(skb, type,
 *			value, padattr)	add u64 attribute to skb
 *   nla_put_s8(skb, type, value)	add s8 attribute to skb
 *   nla_put_s16(skb, type, value)	add s16 attribute to skb
 *   nla_put_s32(skb, type, value)	add s32 attribute to skb
 *   nla_put_s64(skb, type, value,
 *               padattr)		add s64 attribute to skb
 *   nla_put_string(skb, type, str)	add string attribute to skb
 *   nla_put_flag(skb, type)		add flag attribute to skb
 *   nla_put_msecs(skb, type, jiffies,
 *                 padattr)		add msecs attribute to skb
 *   nla_put_in_addr(skb, type, addr)	add IPv4 address attribute to skb
 *   nla_put_in6_addr(skb, type, addr)	add IPv6 address attribute to skb
 *
 * Nested Attributes Construction:
 *   nla_nest_start(skb, type)		start a nested attribute
 *   nla_nest_end(skb, nla)		finalize a nested attribute
 *   nla_nest_cancel(skb, nla)		cancel nested attribute construction
 *
 * Attribute Length Calculations:
 *   nla_attr_size(payload)		length of attribute w/o padding
 *   nla_total_size(payload)		length of attribute w/ padding
 *   nla_padlen(payload)		length of padding
 *
 * Attribute Payload Access:
 *   nla_data(nla)			head of attribute payload
 *   nla_len(nla)			length of attribute payload
 *
 * Attribute Payload Access for Basic Types:
 *   nla_get_u8(nla)			get payload for a u8 attribute
 *   nla_get_u16(nla)			get payload for a u16 attribute
 *   nla_get_u32(nla)			get payload for a u32 attribute
 *   nla_get_u64(nla)			get payload for a u64 attribute
 *   nla_get_s8(nla)			get payload for a s8 attribute
 *   nla_get_s16(nla)			get payload for a s16 attribute
 *   nla_get_s32(nla)			get payload for a s32 attribute
 *   nla_get_s64(nla)			get payload for a s64 attribute
 *   nla_get_flag(nla)			return 1 if flag is true
 *   nla_get_msecs(nla)			get payload for a msecs attribute
 *
 * Attribute Misc:
 *   nla_memcpy(dest, nla, count)	copy attribute into memory
 *   nla_memcmp(nla, data, size)	compare attribute with memory area
 *   nla_strlcpy(dst, nla, size)	copy attribute to a sized string
 *   nla_strcmp(nla, str)		compare attribute with string
 *
 * Attribute Parsing:
 *   nla_ok(nla, remaining)		does nla fit into remaining bytes?
 *   nla_next(nla, remaining)		get next netlink attribute
 *   nla_validate()			validate a stream of attributes
 *   nla_validate_nested()		validate a stream of nested attributes
 *   nla_find()				find attribute in stream of attributes
 *   nla_find_nested()			find attribute in nested attributes
 *   nla_parse()			parse and validate stream of attrs
 *   nla_parse_nested()			parse nested attribuets
 *   nla_for_each_attr()		loop over all attributes
 *   nla_for_each_nested()		loop over the nested attributes
 *=========================================================================
 */

 /**
  * Standard attribute types to specify validation policy
  */
enum {
	NLA_UNSPEC,
	NLA_U8,
	NLA_U16,
	NLA_U32,
	NLA_U64,
	NLA_STRING,
	NLA_FLAG,
	NLA_MSECS,
	NLA_NESTED,
	NLA_NESTED_COMPAT,
	NLA_NUL_STRING,
	NLA_BINARY,
	NLA_S8,
	NLA_S16,
	NLA_S32,
	NLA_S64,
	__NLA_TYPE_MAX,
};

#define NLA_TYPE_MAX (__NLA_TYPE_MAX - 1)

/**
 * struct nla_policy - attribute validation policy
 * @type: Type of attribute or NLA_UNSPEC
 * @len: Type specific length of payload
 *
 * Policies are defined as arrays of this struct, the array must be
 * accessible by attribute type up to the highest identifier to be expected.
 *
 * Meaning of `len' field:
 *    NLA_STRING           Maximum length of string
 *    NLA_NUL_STRING       Maximum length of string (excluding NUL)
 *    NLA_FLAG             Unused
 *    NLA_BINARY           Maximum length of attribute payload
 *    NLA_NESTED           Don't use `len' field -- length verification is
 *                         done by checking len of nested header (or empty)
 *    NLA_NESTED_COMPAT    Minimum length of structure payload
 *    NLA_U8, NLA_U16,
 *    NLA_U32, NLA_U64,
 *    NLA_S8, NLA_S16,
 *    NLA_S32, NLA_S64,
 *    NLA_MSECS            Leaving the length field zero will verify the
 *                         given type fits, using it verifies minimum length
 *                         just like "All other"
 *    All other            Minimum length of attribute payload
 *
 * Example:
 * static const struct nla_policy my_policy[ATTR_MAX+1] = {
 * 	[ATTR_FOO] = { .type = NLA_U16 },
 *	[ATTR_BAR] = { .type = NLA_STRING, .len = BARSIZ },
 *	[ATTR_BAZ] = { .len = sizeof(struct mystruct) },
 * };
 */
struct nla_policy {
	u16		type;
	u16		len;
};

/**
 * struct nl_info - netlink source information
 * @nlh: Netlink message header of original request
 * @portid: Netlink PORTID of requesting application
 */
struct nl_info {
	struct nlmsghdr		*nlh;
	struct net		*nl_net;
	u32			portid;
};

int netlink_rcv_skb(struct sk_buff *skb,
		    int (*cb)(struct sk_buff *, struct nlmsghdr *));
int nlmsg_notify(struct sock *sk, struct sk_buff *skb, u32 portid,
		 unsigned int group, int report, gfp_t flags);

int nla_validate(const struct nlattr *head, int len, int maxtype,
		 const struct nla_policy *policy);
int nla_parse(struct nlattr **tb, int maxtype, const struct nlattr *head,
	      int len, const struct nla_policy *policy);
int nla_policy_len(const struct nla_policy *, int);
struct nlattr *nla_find(const struct nlattr *head, int len, int attrtype);
size_t nla_strlcpy(char *dst, const struct nlattr *nla, size_t dstsize);
int nla_memcpy(void *dest, const struct nlattr *src, int count);
int nla_memcmp(const struct nlattr *nla, const void *data, size_t size);
int nla_strcmp(const struct nlattr *nla, const char *str);
struct nlattr *__nla_reserve(struct sk_buff *skb, int attrtype, int attrlen);
struct nlattr *__nla_reserve_64bit(struct sk_buff *skb, int attrtype,
				   int attrlen, int padattr);
void *__nla_reserve_nohdr(struct sk_buff *skb, int attrlen);
struct nlattr *nla_reserve(struct sk_buff *skb, int attrtype, int attrlen);
struct nlattr *nla_reserve_64bit(struct sk_buff *skb, int attrtype,
				 int attrlen, int padattr);
void *nla_reserve_nohdr(struct sk_buff *skb, int attrlen);
void __nla_put(struct sk_buff *skb, int attrtype, int attrlen,
	       const void *data);
void __nla_put_64bit(struct sk_buff *skb, int attrtype, int attrlen,
		     const void *data, int padattr);
void __nla_put_nohdr(struct sk_buff *skb, int attrlen, const void *data);
int nla_put(struct sk_buff *skb, int attrtype, int attrlen, const void *data);
int nla_put_64bit(struct sk_buff *skb, int attrtype, int attrlen,
		  const void *data, int padattr);
int nla_put_nohdr(struct sk_buff *skb, int attrlen, const void *data);
int nla_append(struct sk_buff *skb, int attrlen, const void *data);

/**************************************************************************
 * Netlink Messages
 **************************************************************************/

/**
 * nlmsg_msg_size - length of netlink message not including padding
 * @payload: length of message payload
 */
static inline int nlmsg_msg_size(int payload)
{
	return NLMSG_HDRLEN + payload;
}

/**
 * nlmsg_total_size - length of netlink message including padding
 * @payload: length of message payload
 */
static inline int nlmsg_total_size(int payload)
{
	return NLMSG_ALIGN(nlmsg_msg_size(payload));
}

/**
 * nlmsg_padlen - length of padding at the message's tail
 * @payload: length of message payload
 */
static inline int nlmsg_padlen(int payload)
{
	return nlmsg_total_size(payload) - nlmsg_msg_size(payload);
}

/**
 * nlmsg_data - head of message payload
 * @nlh: netlink message header
 */
static inline void *nlmsg_data(const struct nlmsghdr *nlh)
{
	return (unsigned char *) nlh + NLMSG_HDRLEN;
}

/**
 * nlmsg_len - length of message payload
 * @nlh: netlink message header
 */
static inline int nlmsg_len(const struct nlmsghdr *nlh)
{
	return nlh->nlmsg_len - NLMSG_HDRLEN;
}

/**
 * nlmsg_attrdata - head of attributes data
 * @nlh: netlink message header
 * @hdrlen: length of family specific header
 */
static inline struct nlattr *nlmsg_attrdata(const struct nlmsghdr *nlh,
					    int hdrlen)
{
	unsigned char *data = nlmsg_data(nlh);
	return (struct nlattr *) (data + NLMSG_ALIGN(hdrlen));
}

/**
 * nlmsg_attrlen - length of attributes data
 * @nlh: netlink message header
 * @hdrlen: length of family specific header
 */
static inline int nlmsg_attrlen(const struct nlmsghdr *nlh, int hdrlen)
{
	return nlmsg_len(nlh) - NLMSG_ALIGN(hdrlen);
}

/**
 * nlmsg_ok - check if the netlink message fits into the remaining bytes
 * @nlh: netlink message header
 * @remaining: number of bytes remaining in message stream
 */
static inline int nlmsg_ok(const struct nlmsghdr *nlh, int remaining)
{
	return (remaining >= (int) sizeof(struct nlmsghdr) &&
		nlh->nlmsg_len >= sizeof(struct nlmsghdr) &&
		nlh->nlmsg_len <= remaining);
}

/**
 * nlmsg_next - next netlink message in message stream
 * @nlh: netlink message header
 * @remaining: number of bytes remaining in message stream
 *
 * Returns the next netlink message in the message stream and
 * decrements remaining by the size of the current message.
 */
static inline struct nlmsghdr *
nlmsg_next(const struct nlmsghdr *nlh, int *remaining)
{
	int totlen = NLMSG_ALIGN(nlh->nlmsg_len);

	*remaining -= totlen;

	return (struct nlmsghdr *) ((unsigned char *) nlh + totlen);
}

/**
 * nlmsg_parse - parse attributes of a netlink message
 * @nlh: netlink message header
 * @hdrlen: length of family specific header
 * @tb: destination array with maxtype+1 elements
 * @maxtype: maximum attribute type to be expected
 * @policy: validation policy
 *
 * See nla_parse()
 */
static inline int nlmsg_parse(const struct nlmsghdr *nlh, int hdrlen,
			      struct nlattr *tb[], int maxtype,
			      const struct nla_policy *policy)
{
	if (nlh->nlmsg_len < nlmsg_msg_size(hdrlen))
		return -EINVAL;

	return nla_parse(tb, maxtype, nlmsg_attrdata(nlh, hdrlen),
			 nlmsg_attrlen(nlh, hdrlen), policy);
}

/**
 * nlmsg_find_attr - find a specific attribute in a netlink message
 * @nlh: netlink message header
 * @hdrlen: length of familiy specific header
 * @attrtype: type of attribute to look for
 *
 * Returns the first attribute which matches the specified type.
 */
static inline struct nlattr *nlmsg_find_attr(const struct nlmsghdr *nlh,
					     int hdrlen, int attrtype)
{
	return nla_find(nlmsg_attrdata(nlh, hdrlen),
			nlmsg_attrlen(nlh, hdrlen), attrtype);
}

/**
 * nlmsg_validate - validate a netlink message including attributes
 * @nlh: netlinket message header
 * @hdrlen: length of familiy specific header
 * @maxtype: maximum attribute type to be expected
 * @policy: validation policy
 */
static inline int nlmsg_validate(const struct nlmsghdr *nlh,
				 int hdrlen, int maxtype,
				 const struct nla_policy *policy)
{
	if (nlh->nlmsg_len < nlmsg_msg_size(hdrlen))
		return -EINVAL;

	return nla_validate(nlmsg_attrdata(nlh, hdrlen),
			    nlmsg_attrlen(nlh, hdrlen), maxtype, policy);
}

/**
 * nlmsg_report - need to report back to application?
 * @nlh: netlink message header
 *
 * Returns 1 if a report back to the application is requested.
 */
static inline int nlmsg_report(const struct nlmsghdr *nlh)
{
	return !!(nlh->nlmsg_flags & NLM_F_ECHO);
}

/**
 * nlmsg_for_each_attr - iterate over a stream of attributes
 * @pos: loop counter, set to current attribute
 * @nlh: netlink message header
 * @hdrlen: length of familiy specific header
 * @rem: initialized to len, holds bytes currently remaining in stream
 */
#define nlmsg_for_each_attr(pos, nlh, hdrlen, rem) \
	nla_for_each_attr(pos, nlmsg_attrdata(nlh, hdrlen), \
			  nlmsg_attrlen(nlh, hdrlen), rem)

/**
 * nlmsg_put - Add a new netlink message to an skb
 * @skb: socket buffer to store message in
 * @portid: netlink PORTID of requesting application
 * @seq: sequence number of message
 * @type: message type
 * @payload: length of message payload
 * @flags: message flags
 *
 * Returns NULL if the tailroom of the skb is insufficient to store
 * the message header and payload.
 */
static inline struct nlmsghdr *nlmsg_put(struct sk_buff *skb, u32 portid, u32 seq,
					 int type, int payload, int flags)
{
	if (unlikely(skb_tailroom(skb) < nlmsg_total_size(payload)))
		return NULL;

	return __nlmsg_put(skb, portid, seq, type, payload, flags);
}

/**
 * nlmsg_put_answer - Add a new callback based netlink message to an skb
 * @skb: socket buffer to store message in
 * @cb: netlink callback
 * @type: message type
 * @payload: length of message payload
 * @flags: message flags
 *
 * Returns NULL if the tailroom of the skb is insufficient to store
 * the message header and payload.
 */
static inline struct nlmsghdr *nlmsg_put_answer(struct sk_buff *skb,
						struct netlink_callback *cb,
						int type, int payload,
						int flags)
{
	return nlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			 type, payload, flags);
}

/**
 * nlmsg_new - Allocate a new netlink message
 * @payload: size of the message payload
 * @flags: the type of memory to allocate.
 *
 * Use NLMSG_DEFAULT_SIZE if the size of the payload isn't known
 * and a good default is needed.
 */
static inline struct sk_buff *nlmsg_new(size_t payload, gfp_t flags)
{
	return alloc_skb(nlmsg_total_size(payload), flags);
}

/**
 * nlmsg_end - Finalize a netlink message
 * @skb: socket buffer the message is stored in
 * @nlh: netlink message header
 *
 * Corrects the netlink message header to include the appeneded
 * attributes. Only necessary if attributes have been added to
 * the message.
 */
static inline void nlmsg_end(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	nlh->nlmsg_len = skb_tail_pointer(skb) - (unsigned char *)nlh;
}

/**
 * nlmsg_get_pos - return current position in netlink message
 * @skb: socket buffer the message is stored in
 *
 * Returns a pointer to the current tail of the message.
 */
static inline void *nlmsg_get_pos(struct sk_buff *skb)
{
	return skb_tail_pointer(skb);
}

/**
 * nlmsg_trim - Trim message to a mark
 * @skb: socket buffer the message is stored in
 * @mark: mark to trim to
 *
 * Trims the message to the provided mark.
 */
static inline void nlmsg_trim(struct sk_buff *skb, const void *mark)
{
	if (mark) {
		WARN_ON((unsigned char *) mark < skb->data);
		skb_trim(skb, (unsigned char *) mark - skb->data);
	}
}

/**
 * nlmsg_cancel - Cancel construction of a netlink message
 * @skb: socket buffer the message is stored in
 * @nlh: netlink message header
 *
 * Removes the complete netlink message including all
 * attributes from the socket buffer again.
 */
static inline void nlmsg_cancel(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	nlmsg_trim(skb, nlh);
}

/**
 * nlmsg_free - free a netlink message
 * @skb: socket buffer of netlink message
 */
static inline void nlmsg_free(struct sk_buff *skb)
{
	kfree_skb(skb);
}

/**
 * nlmsg_multicast - multicast a netlink message
 * @sk: netlink socket to spread messages to
 * @skb: netlink message as socket buffer
 * @portid: own netlink portid to avoid sending to yourself
 * @group: multicast group id
 * @flags: allocation flags
 */
static inline int nlmsg_multicast(struct sock *sk, struct sk_buff *skb,
				  u32 portid, unsigned int group, gfp_t flags)
{
	int err;

	NETLINK_CB(skb).dst_group = group;

	err = netlink_broadcast(sk, skb, portid, group, flags);
	if (err > 0)
		err = 0;

	return err;
}

/**
 * nlmsg_unicast - unicast a netlink message
 * @sk: netlink socket to spread message to
 * @skb: netlink message as socket buffer
 * @portid: netlink portid of the destination socket
 */
static inline int nlmsg_unicast(struct sock *sk, struct sk_buff *skb, u32 portid)
{
	int err;

	err = netlink_unicast(sk, skb, portid, MSG_DONTWAIT);
	if (err > 0)
		err = 0;

	return err;
}

/**
 * nlmsg_for_each_msg - iterate over a stream of messages
 * @pos: loop counter, set to current message
 * @head: head of message stream
 * @len: length of message stream
 * @rem: initialized to len, holds bytes currently remaining in stream
 */
#define nlmsg_for_each_msg(pos, head, len, rem) \
	for (pos = head, rem = len; \
	     nlmsg_ok(pos, rem); \
	     pos = nlmsg_next(pos, &(rem)))

/**
 * nl_dump_check_consistent - check if sequence is consistent and advertise if not
 * @cb: netlink callback structure that stores the sequence number
 * @nlh: netlink message header to write the flag to
 *
 * This function checks if the sequence (generation) number changed during dump
 * and if it did, advertises it in the netlink message header.
 *
 * The correct way to use it is to set cb->seq to the generation counter when
 * all locks for dumping have been acquired, and then call this function for
 * each message that is generated.
 *
 * Note that due to initialisation concerns, 0 is an invalid sequence number
 * and must not be used by code that uses this functionality.
 */
static inline void
nl_dump_check_consistent(struct netlink_callback *cb,
			 struct nlmsghdr *nlh)
{
	if (cb->prev_seq && cb->seq != cb->prev_seq)
		nlh->nlmsg_flags |= NLM_F_DUMP_INTR;
	cb->prev_seq = cb->seq;
}

/**************************************************************************
 * Netlink Attributes
 **************************************************************************/

/**
 * nla_attr_size - length of attribute not including padding
 * @payload: length of payload
 */
static inline int nla_attr_size(int payload)
{
	return NLA_HDRLEN + payload;
}

/**
 * nla_total_size - total length of attribute including padding
 * @payload: length of payload
 */
static inline int nla_total_size(int payload)
{
	return NLA_ALIGN(nla_attr_size(payload));
}

/**
 * nla_padlen - length of padding at the tail of attribute
 * @payload: length of payload
 */
static inline int nla_padlen(int payload)
{
	return nla_total_size(payload) - nla_attr_size(payload);
}

/**
 * nla_type - attribute type
 * @nla: netlink attribute
 */
static inline int nla_type(const struct nlattr *nla)
{
	return nla->nla_type & NLA_TYPE_MASK;
}

/**
 * nla_data - head of payload
 * @nla: netlink attribute
 */
static inline void *nla_data(const struct nlattr *nla)
{
	return (char *) nla + NLA_HDRLEN;
}

/**
 * nla_len - length of payload
 * @nla: netlink attribute
 */
static inline int nla_len(const struct nlattr *nla)
{
	return nla->nla_len - NLA_HDRLEN;
}

/**
 * nla_ok - check if the netlink attribute fits into the remaining bytes
 * @nla: netlink attribute
 * @remaining: number of bytes remaining in attribute stream
 */
static inline int nla_ok(const struct nlattr *nla, int remaining)
{
	return remaining >= (int) sizeof(*nla) &&
	       nla->nla_len >= sizeof(*nla) &&
	       nla->nla_len <= remaining;
}

/**
 * nla_next - next netlink attribute in attribute stream
 * @nla: netlink attribute
 * @remaining: number of bytes remaining in attribute stream
 *
 * Returns the next netlink attribute in the attribute stream and
 * decrements remaining by the size of the current attribute.
 */
static inline struct nlattr *nla_next(const struct nlattr *nla, int *remaining)
{
	int totlen = NLA_ALIGN(nla->nla_len);

	*remaining -= totlen;
	return (struct nlattr *) ((char *) nla + totlen);
}

/**
 * nla_find_nested - find attribute in a set of nested attributes
 * @nla: attribute containing the nested attributes
 * @attrtype: type of attribute to look for
 *
 * Returns the first attribute which matches the specified type.
 */
static inline struct nlattr *
nla_find_nested(const struct nlattr *nla, int attrtype)
{
	return nla_find(nla_data(nla), nla_len(nla), attrtype);
}

/**
 * nla_parse_nested - parse nested attributes
 * @tb: destination array with maxtype+1 elements
 * @maxtype: maximum attribute type to be expected
 * @nla: attribute containing the nested attributes
 * @policy: validation policy
 *
 * See nla_parse()
 */
static inline int nla_parse_nested(struct nlattr *tb[], int maxtype,
				   const struct nlattr *nla,
				   const struct nla_policy *policy)
{
	return nla_parse(tb, maxtype, nla_data(nla), nla_len(nla), policy);
}

/**
 * nla_put_u8 - Add a u8 netlink attribute to a socket buffer
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @value: numeric value
 */
static inline int nla_put_u8(struct sk_buff *skb, int attrtype, u8 value)
{
	return nla_put(skb, attrtype, sizeof(u8), &value);
}

/**
 * nla_put_u16 - Add a u16 netlink attribute to a socket buffer
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @value: numeric value
 */
static inline int nla_put_u16(struct sk_buff *skb, int attrtype, u16 value)
{
	return nla_put(skb, attrtype, sizeof(u16), &value);
}

/**
 * nla_put_be16 - Add a __be16 netlink attribute to a socket buffer
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @value: numeric value
 */
static inline int nla_put_be16(struct sk_buff *skb, int attrtype, __be16 value)
{
	return nla_put(skb, attrtype, sizeof(__be16), &value);
}

/**
 * nla_put_net16 - Add 16-bit network byte order netlink attribute to a socket buffer
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @value: numeric value
 */
static inline int nla_put_net16(struct sk_buff *skb, int attrtype, __be16 value)
{
	return nla_put_be16(skb, attrtype | NLA_F_NET_BYTEORDER, value);
}

/**
 * nla_put_le16 - Add a __le16 netlink attribute to a socket buffer
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @value: numeric value
 */
static inline int nla_put_le16(struct sk_buff *skb, int attrtype, __le16 value)
{
	return nla_put(skb, attrtype, sizeof(__le16), &value);
}

/**
 * nla_put_u32 - Add a u32 netlink attribute to a socket buffer
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @value: numeric value
 */
static inline int nla_put_u32(struct sk_buff *skb, int attrtype, u32 value)
{
	return nla_put(skb, attrtype, sizeof(u32), &value);
}

/**
 * nla_put_be32 - Add a __be32 netlink attribute to a socket buffer
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @value: numeric value
 */
static inline int nla_put_be32(struct sk_buff *skb, int attrtype, __be32 value)
{
	return nla_put(skb, attrtype, sizeof(__be32), &value);
}

/**
 * nla_put_net32 - Add 32-bit network byte order netlink attribute to a socket buffer
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @value: numeric value
 */
static inline int nla_put_net32(struct sk_buff *skb, int attrtype, __be32 value)
{
	return nla_put_be32(skb, attrtype | NLA_F_NET_BYTEORDER, value);
}

/**
 * nla_put_le32 - Add a __le32 netlink attribute to a socket buffer
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @value: numeric value
 */
static inline int nla_put_le32(struct sk_buff *skb, int attrtype, __le32 value)
{
	return nla_put(skb, attrtype, sizeof(__le32), &value);
}

/**
 * nla_put_u64_64bit - Add a u64 netlink attribute to a skb and align it
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @value: numeric value
 * @padattr: attribute type for the padding
 */
static inline int nla_put_u64_64bit(struct sk_buff *skb, int attrtype,
				    u64 value, int padattr)
{
	return nla_put_64bit(skb, attrtype, sizeof(u64), &value, padattr);
}

/**
 * nla_put_be64 - Add a __be64 netlink attribute to a socket buffer and align it
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @value: numeric value
 * @padattr: attribute type for the padding
 */
static inline int nla_put_be64(struct sk_buff *skb, int attrtype, __be64 value,
			       int padattr)
{
	return nla_put_64bit(skb, attrtype, sizeof(__be64), &value, padattr);
}

/**
 * nla_put_net64 - Add 64-bit network byte order nlattr to a skb and align it
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @value: numeric value
 * @padattr: attribute type for the padding
 */
static inline int nla_put_net64(struct sk_buff *skb, int attrtype, __be64 value,
				int padattr)
{
	return nla_put_be64(skb, attrtype | NLA_F_NET_BYTEORDER, value,
			    padattr);
}

/**
 * nla_put_le64 - Add a __le64 netlink attribute to a socket buffer and align it
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @value: numeric value
 * @padattr: attribute type for the padding
 */
static inline int nla_put_le64(struct sk_buff *skb, int attrtype, __le64 value,
			       int padattr)
{
	return nla_put_64bit(skb, attrtype, sizeof(__le64), &value, padattr);
}

/**
 * nla_put_s8 - Add a s8 netlink attribute to a socket buffer
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @value: numeric value
 */
static inline int nla_put_s8(struct sk_buff *skb, int attrtype, s8 value)
{
	return nla_put(skb, attrtype, sizeof(s8), &value);
}

/**
 * nla_put_s16 - Add a s16 netlink attribute to a socket buffer
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @value: numeric value
 */
static inline int nla_put_s16(struct sk_buff *skb, int attrtype, s16 value)
{
	return nla_put(skb, attrtype, sizeof(s16), &value);
}

/**
 * nla_put_s32 - Add a s32 netlink attribute to a socket buffer
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @value: numeric value
 */
static inline int nla_put_s32(struct sk_buff *skb, int attrtype, s32 value)
{
	return nla_put(skb, attrtype, sizeof(s32), &value);
}

/**
 * nla_put_s64 - Add a s64 netlink attribute to a socket buffer and align it
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @value: numeric value
 * @padattr: attribute type for the padding
 */
static inline int nla_put_s64(struct sk_buff *skb, int attrtype, s64 value,
			      int padattr)
{
	return nla_put_64bit(skb, attrtype, sizeof(s64), &value, padattr);
}

/**
 * nla_put_string - Add a string netlink attribute to a socket buffer
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @str: NUL terminated string
 */
static inline int nla_put_string(struct sk_buff *skb, int attrtype,
				 const char *str)
{
	return nla_put(skb, attrtype, strlen(str) + 1, str);
}

/**
 * nla_put_flag - Add a flag netlink attribute to a socket buffer
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 */
static inline int nla_put_flag(struct sk_buff *skb, int attrtype)
{
	return nla_put(skb, attrtype, 0, NULL);
}

/**
 * nla_put_msecs - Add a msecs netlink attribute to a skb and align it
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @njiffies: number of jiffies to convert to msecs
 * @padattr: attribute type for the padding
 */
static inline int nla_put_msecs(struct sk_buff *skb, int attrtype,
				unsigned long njiffies, int padattr)
{
	u64 tmp = jiffies_to_msecs(njiffies);

	return nla_put_64bit(skb, attrtype, sizeof(u64), &tmp, padattr);
}

/**
 * nla_put_in_addr - Add an IPv4 address netlink attribute to a socket
 * buffer
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @addr: IPv4 address
 */
static inline int nla_put_in_addr(struct sk_buff *skb, int attrtype,
				  __be32 addr)
{
	return nla_put_be32(skb, attrtype, addr);
}

/**
 * nla_put_in6_addr - Add an IPv6 address netlink attribute to a socket
 * buffer
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @addr: IPv6 address
 */
static inline int nla_put_in6_addr(struct sk_buff *skb, int attrtype,
				   const struct in6_addr *addr)
{
	return nla_put(skb, attrtype, sizeof(*addr), addr);
}

/**
 * nla_get_u32 - return payload of u32 attribute
 * @nla: u32 netlink attribute
 */
static inline u32 nla_get_u32(const struct nlattr *nla)
{
	return *(u32 *) nla_data(nla);
}

/**
 * nla_get_be32 - return payload of __be32 attribute
 * @nla: __be32 netlink attribute
 */
static inline __be32 nla_get_be32(const struct nlattr *nla)
{
	return *(__be32 *) nla_data(nla);
}

/**
 * nla_get_le32 - return payload of __le32 attribute
 * @nla: __le32 netlink attribute
 */
static inline __le32 nla_get_le32(const struct nlattr *nla)
{
	return *(__le32 *) nla_data(nla);
}

/**
 * nla_get_u16 - return payload of u16 attribute
 * @nla: u16 netlink attribute
 */
static inline u16 nla_get_u16(const struct nlattr *nla)
{
	return *(u16 *) nla_data(nla);
}

/**
 * nla_get_be16 - return payload of __be16 attribute
 * @nla: __be16 netlink attribute
 */
static inline __be16 nla_get_be16(const struct nlattr *nla)
{
	return *(__be16 *) nla_data(nla);
}

/**
 * nla_get_le16 - return payload of __le16 attribute
 * @nla: __le16 netlink attribute
 */
static inline __le16 nla_get_le16(const struct nlattr *nla)
{
	return *(__le16 *) nla_data(nla);
}

/**
 * nla_get_u8 - return payload of u8 attribute
 * @nla: u8 netlink attribute
 */
static inline u8 nla_get_u8(const struct nlattr *nla)
{
	return *(u8 *) nla_data(nla);
}

/**
 * nla_get_u64 - return payload of u64 attribute
 * @nla: u64 netlink attribute
 */
static inline u64 nla_get_u64(const struct nlattr *nla)
{
	u64 tmp;

	nla_memcpy(&tmp, nla, sizeof(tmp));

	return tmp;
}

/**
 * nla_get_be64 - return payload of __be64 attribute
 * @nla: __be64 netlink attribute
 */
static inline __be64 nla_get_be64(const struct nlattr *nla)
{
	__be64 tmp;

	nla_memcpy(&tmp, nla, sizeof(tmp));

	return tmp;
}

/**
 * nla_get_le64 - return payload of __le64 attribute
 * @nla: __le64 netlink attribute
 */
static inline __le64 nla_get_le64(const struct nlattr *nla)
{
	return *(__le64 *) nla_data(nla);
}

/**
 * nla_get_s32 - return payload of s32 attribute
 * @nla: s32 netlink attribute
 */
static inline s32 nla_get_s32(const struct nlattr *nla)
{
	return *(s32 *) nla_data(nla);
}

/**
 * nla_get_s16 - return payload of s16 attribute
 * @nla: s16 netlink attribute
 */
static inline s16 nla_get_s16(const struct nlattr *nla)
{
	return *(s16 *) nla_data(nla);
}

/**
 * nla_get_s8 - return payload of s8 attribute
 * @nla: s8 netlink attribute
 */
static inline s8 nla_get_s8(const struct nlattr *nla)
{
	return *(s8 *) nla_data(nla);
}

/**
 * nla_get_s64 - return payload of s64 attribute
 * @nla: s64 netlink attribute
 */
static inline s64 nla_get_s64(const struct nlattr *nla)
{
	s64 tmp;

	nla_memcpy(&tmp, nla, sizeof(tmp));

	return tmp;
}

/**
 * nla_get_flag - return payload of flag attribute
 * @nla: flag netlink attribute
 */
static inline int nla_get_flag(const struct nlattr *nla)
{
	return !!nla;
}

/**
 * nla_get_msecs - return payload of msecs attribute
 * @nla: msecs netlink attribute
 *
 * Returns the number of milliseconds in jiffies.
 */
static inline unsigned long nla_get_msecs(const struct nlattr *nla)
{
	u64 msecs = nla_get_u64(nla);

	return msecs_to_jiffies((unsigned long) msecs);
}

/**
 * nla_get_in_addr - return payload of IPv4 address attribute
 * @nla: IPv4 address netlink attribute
 */
static inline __be32 nla_get_in_addr(const struct nlattr *nla)
{
	return *(__be32 *) nla_data(nla);
}

/**
 * nla_get_in6_addr - return payload of IPv6 address attribute
 * @nla: IPv6 address netlink attribute
 */
static inline struct in6_addr nla_get_in6_addr(const struct nlattr *nla)
{
	struct in6_addr tmp;

	nla_memcpy(&tmp, nla, sizeof(tmp));
	return tmp;
}

/**
 * nla_nest_start - Start a new level of nested attributes
 * @skb: socket buffer to add attributes to
 * @attrtype: attribute type of container
 *
 * Returns the container attribute
 */
static inline struct nlattr *nla_nest_start(struct sk_buff *skb, int attrtype)
{
	struct nlattr *start = (struct nlattr *)skb_tail_pointer(skb);

	if (nla_put(skb, attrtype, 0, NULL) < 0)
		return NULL;

	return start;
}

/**
 * nla_nest_end - Finalize nesting of attributes
 * @skb: socket buffer the attributes are stored in
 * @start: container attribute
 *
 * Corrects the container attribute header to include the all
 * appeneded attributes.
 *
 * Returns the total data length of the skb.
 */
static inline int nla_nest_end(struct sk_buff *skb, struct nlattr *start)
{
	start->nla_len = skb_tail_pointer(skb) - (unsigned char *)start;
	return skb->len;
}

/**
 * nla_nest_cancel - Cancel nesting of attributes
 * @skb: socket buffer the message is stored in
 * @start: container attribute
 *
 * Removes the container attribute and including all nested
 * attributes. Returns -EMSGSIZE
 */
static inline void nla_nest_cancel(struct sk_buff *skb, struct nlattr *start)
{
	nlmsg_trim(skb, start);
}

/**
 * nla_validate_nested - Validate a stream of nested attributes
 * @start: container attribute
 * @maxtype: maximum attribute type to be expected
 * @policy: validation policy
 *
 * Validates all attributes in the nested attribute stream against the
 * specified policy. Attributes with a type exceeding maxtype will be
 * ignored. See documenation of struct nla_policy for more details.
 *
 * Returns 0 on success or a negative error code.
 */
static inline int nla_validate_nested(const struct nlattr *start, int maxtype,
				      const struct nla_policy *policy)
{
	return nla_validate(nla_data(start), nla_len(start), maxtype, policy);
}

/**
 * nla_need_padding_for_64bit - test 64-bit alignment of the next attribute
 * @skb: socket buffer the message is stored in
 *
 * Return true if padding is needed to align the next attribute (nla_data()) to
 * a 64-bit aligned area.
 */
static inline bool nla_need_padding_for_64bit(struct sk_buff *skb)
{
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	/* The nlattr header is 4 bytes in size, that's why we test
	 * if the skb->data _is_ aligned.  A NOP attribute, plus
	 * nlattr header for next attribute, will make nla_data()
	 * 8-byte aligned.
	 */
	if (IS_ALIGNED((unsigned long)skb_tail_pointer(skb), 8))
		return true;
#endif
	return false;
}

/**
 * nla_align_64bit - 64-bit align the nla_data() of next attribute
 * @skb: socket buffer the message is stored in
 * @padattr: attribute type for the padding
 *
 * Conditionally emit a padding netlink attribute in order to make
 * the next attribute we emit have a 64-bit aligned nla_data() area.
 * This will only be done in architectures which do not have
 * CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS defined.
 *
 * Returns zero on success or a negative error code.
 */
static inline int nla_align_64bit(struct sk_buff *skb, int padattr)
{
	if (nla_need_padding_for_64bit(skb) &&
	    !nla_reserve(skb, padattr, 0))
		return -EMSGSIZE;

	return 0;
}

/**
 * nla_total_size_64bit - total length of attribute including padding
 * @payload: length of payload
 */
static inline int nla_total_size_64bit(int payload)
{
	return NLA_ALIGN(nla_attr_size(payload))
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
		+ NLA_ALIGN(nla_attr_size(0))
#endif
		;
}

/**
 * nla_for_each_attr - iterate over a stream of attributes
 * @pos: loop counter, set to current attribute
 * @head: head of attribute stream
 * @len: length of attribute stream
 * @rem: initialized to len, holds bytes currently remaining in stream
 */
#define nla_for_each_attr(pos, head, len, rem) \
	for (pos = head, rem = len; \
	     nla_ok(pos, rem); \
	     pos = nla_next(pos, &(rem)))

/**
 * nla_for_each_nested - iterate over nested attributes
 * @pos: loop counter, set to current attribute
 * @nla: attribute containing the nested attributes
 * @rem: initialized to len, holds bytes currently remaining in stream
 */
#define nla_for_each_nested(pos, nla, rem) \
	nla_for_each_attr(pos, nla_data(nla), nla_len(nla), rem)

/**
 * nla_is_last - Test if attribute is last in stream
 * @nla: attribute to test
 * @rem: bytes remaining in stream
 */
static inline bool nla_is_last(const struct nlattr *nla, int rem)
{
	return nla->nla_len == rem;
}

#endif
