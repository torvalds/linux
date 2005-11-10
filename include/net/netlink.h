#ifndef __NET_NETLINK_H
#define __NET_NETLINK_H

#include <linux/types.h>
#include <linux/netlink.h>

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
 *   nlmsg_end()			finanlize netlink message
 *   nlmsg_cancel()			cancel message construction
 *   nlmsg_free()			free a netlink message
 *
 * Message Sending:
 *   nlmsg_multicast()			multicast message to several groups
 *   nlmsg_unicast()			unicast a message to a single socket
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
 *   struct nlattr			netlink attribtue header
 *
 * Attribute Construction:
 *   nla_reserve(skb, type, len)	reserve skb tailroom for an attribute
 *   nla_put(skb, type, len, data)	add attribute to skb
 *
 * Attribute Construction for Basic Types:
 *   nla_put_u8(skb, type, value)	add u8 attribute to skb
 *   nla_put_u16(skb, type, value)	add u16 attribute to skb
 *   nla_put_u32(skb, type, value)	add u32 attribute to skb
 *   nla_put_u64(skb, type, value)	add u64 attribute to skb
 *   nla_put_string(skb, type, str)	add string attribute to skb
 *   nla_put_flag(skb, type)		add flag attribute to skb
 *   nla_put_msecs(skb, type, jiffies)	add msecs attribute to skb
 *
 * Exceptions Based Attribute Construction:
 *   NLA_PUT(skb, type, len, data)	add attribute to skb
 *   NLA_PUT_U8(skb, type, value)	add u8 attribute to skb
 *   NLA_PUT_U16(skb, type, value)	add u16 attribute to skb
 *   NLA_PUT_U32(skb, type, value)	add u32 attribute to skb
 *   NLA_PUT_U64(skb, type, value)	add u64 attribute to skb
 *   NLA_PUT_STRING(skb, type, str)	add string attribute to skb
 *   NLA_PUT_FLAG(skb, type)		add flag attribute to skb
 *   NLA_PUT_MSECS(skb, type, jiffies)	add msecs attribute to skb
 *
 *   The meaning of these functions is equal to their lower case
 *   variants but they jump to the label nla_put_failure in case
 *   of a failure.
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
 *   nla_find()				find attribute in stream of attributes
 *   nla_parse()			parse and validate stream of attrs
 *   nla_parse_nested()			parse nested attribuets
 *   nla_for_each_attr()		loop over all attributes
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
	__NLA_TYPE_MAX,
};

#define NLA_TYPE_MAX (__NLA_TYPE_MAX - 1)

/**
 * struct nla_policy - attribute validation policy
 * @type: Type of attribute or NLA_UNSPEC
 * @minlen: Minimal length of payload required to be available
 *
 * Policies are defined as arrays of this struct, the array must be
 * accessible by attribute type up to the highest identifier to be expected.
 *
 * Example:
 * static struct nla_policy my_policy[ATTR_MAX+1] __read_mostly = {
 * 	[ATTR_FOO] = { .type = NLA_U16 },
 *	[ATTR_BAR] = { .type = NLA_STRING },
 *	[ATTR_BAZ] = { .minlen = sizeof(struct mystruct) },
 * };
 */
struct nla_policy {
	u16		type;
	u16		minlen;
};

extern void		netlink_run_queue(struct sock *sk, unsigned int *qlen,
					  int (*cb)(struct sk_buff *,
						    struct nlmsghdr *, int *));
extern void		netlink_queue_skip(struct nlmsghdr *nlh,
					   struct sk_buff *skb);

extern int		nla_validate(struct nlattr *head, int len, int maxtype,
				     struct nla_policy *policy);
extern int		nla_parse(struct nlattr *tb[], int maxtype,
				  struct nlattr *head, int len,
				  struct nla_policy *policy);
extern struct nlattr *	nla_find(struct nlattr *head, int len, int attrtype);
extern size_t		nla_strlcpy(char *dst, const struct nlattr *nla,
				    size_t dstsize);
extern int		nla_memcpy(void *dest, struct nlattr *src, int count);
extern int		nla_memcmp(const struct nlattr *nla, const void *data,
				   size_t size);
extern int		nla_strcmp(const struct nlattr *nla, const char *str);
extern struct nlattr *	__nla_reserve(struct sk_buff *skb, int attrtype,
				      int attrlen);
extern struct nlattr *	nla_reserve(struct sk_buff *skb, int attrtype,
				    int attrlen);
extern void		__nla_put(struct sk_buff *skb, int attrtype,
				  int attrlen, const void *data);
extern int		nla_put(struct sk_buff *skb, int attrtype,
				int attrlen, const void *data);

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
 * @nlh: netlink messsage header
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
	return (remaining >= sizeof(struct nlmsghdr) &&
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
static inline struct nlmsghdr *nlmsg_next(struct nlmsghdr *nlh, int *remaining)
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
static inline int nlmsg_parse(struct nlmsghdr *nlh, int hdrlen,
			      struct nlattr *tb[], int maxtype,
			      struct nla_policy *policy)
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
static inline struct nlattr *nlmsg_find_attr(struct nlmsghdr *nlh,
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
static inline int nlmsg_validate(struct nlmsghdr *nlh, int hdrlen, int maxtype,
				 struct nla_policy *policy)
{
	if (nlh->nlmsg_len < nlmsg_msg_size(hdrlen))
		return -EINVAL;

	return nla_validate(nlmsg_attrdata(nlh, hdrlen),
			    nlmsg_attrlen(nlh, hdrlen), maxtype, policy);
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

#if 0
/* FIXME: Enable once all users have been converted */

/**
 * __nlmsg_put - Add a new netlink message to an skb
 * @skb: socket buffer to store message in
 * @pid: netlink process id
 * @seq: sequence number of message
 * @type: message type
 * @payload: length of message payload
 * @flags: message flags
 *
 * The caller is responsible to ensure that the skb provides enough
 * tailroom for both the netlink header and payload.
 */
static inline struct nlmsghdr *__nlmsg_put(struct sk_buff *skb, u32 pid,
					   u32 seq, int type, int payload,
					   int flags)
{
	struct nlmsghdr *nlh;

	nlh = (struct nlmsghdr *) skb_put(skb, nlmsg_total_size(payload));
	nlh->nlmsg_type = type;
	nlh->nlmsg_len = nlmsg_msg_size(payload);
	nlh->nlmsg_flags = flags;
	nlh->nlmsg_pid = pid;
	nlh->nlmsg_seq = seq;

	memset((unsigned char *) nlmsg_data(nlh) + payload, 0,
	       nlmsg_padlen(payload));

	return nlh;
}
#endif

/**
 * nlmsg_put - Add a new netlink message to an skb
 * @skb: socket buffer to store message in
 * @pid: netlink process id
 * @seq: sequence number of message
 * @type: message type
 * @payload: length of message payload
 * @flags: message flags
 *
 * Returns NULL if the tailroom of the skb is insufficient to store
 * the message header and payload.
 */
static inline struct nlmsghdr *nlmsg_put(struct sk_buff *skb, u32 pid, u32 seq,
					 int type, int payload, int flags)
{
	if (unlikely(skb_tailroom(skb) < nlmsg_total_size(payload)))
		return NULL;

	return __nlmsg_put(skb, pid, seq, type, payload, flags);
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
	return nlmsg_put(skb, NETLINK_CB(cb->skb).pid, cb->nlh->nlmsg_seq,
			 type, payload, flags);
}

/**
 * nlmsg_new - Allocate a new netlink message
 * @size: maximum size of message
 *
 * Use NLMSG_GOODSIZE if size isn't know and you need a good default size.
 */
static inline struct sk_buff *nlmsg_new(int size)
{
	return alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
}

/**
 * nlmsg_end - Finalize a netlink message
 * @skb: socket buffer the message is stored in
 * @nlh: netlink message header
 *
 * Corrects the netlink message header to include the appeneded
 * attributes. Only necessary if attributes have been added to
 * the message.
 *
 * Returns the total data length of the skb.
 */
static inline int nlmsg_end(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	nlh->nlmsg_len = skb->tail - (unsigned char *) nlh;

	return skb->len;
}

/**
 * nlmsg_cancel - Cancel construction of a netlink message
 * @skb: socket buffer the message is stored in
 * @nlh: netlink message header
 *
 * Removes the complete netlink message including all
 * attributes from the socket buffer again. Returns -1.
 */
static inline int nlmsg_cancel(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	skb_trim(skb, (unsigned char *) nlh - skb->data);

	return -1;
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
 * @pid: own netlink pid to avoid sending to yourself
 * @group: multicast group id
 */
static inline int nlmsg_multicast(struct sock *sk, struct sk_buff *skb,
				  u32 pid, unsigned int group)
{
	int err;

	NETLINK_CB(skb).dst_group = group;

	err = netlink_broadcast(sk, skb, pid, group, GFP_KERNEL);
	if (err > 0)
		err = 0;

	return err;
}

/**
 * nlmsg_unicast - unicast a netlink message
 * @sk: netlink socket to spread message to
 * @skb: netlink message as socket buffer
 * @pid: netlink pid of the destination socket
 */
static inline int nlmsg_unicast(struct sock *sk, struct sk_buff *skb, u32 pid)
{
	int err;

	err = netlink_unicast(sk, skb, pid, MSG_DONTWAIT);
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
	return remaining >= sizeof(*nla) &&
	       nla->nla_len >= sizeof(*nla) &&
	       nla->nla_len <= remaining;
}

/**
 * nla_next - next netlink attribte in attribute stream
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
 * nla_parse_nested - parse nested attributes
 * @tb: destination array with maxtype+1 elements
 * @maxtype: maximum attribute type to be expected
 * @nla: attribute containing the nested attributes
 * @policy: validation policy
 *
 * See nla_parse()
 */
static inline int nla_parse_nested(struct nlattr *tb[], int maxtype,
				   struct nlattr *nla,
				   struct nla_policy *policy)
{
	return nla_parse(tb, maxtype, nla_data(nla), nla_len(nla), policy);
}
/**
 * nla_put_u8 - Add a u16 netlink attribute to a socket buffer
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
 * nla_put_64 - Add a u64 netlink attribute to a socket buffer
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @value: numeric value
 */
static inline int nla_put_u64(struct sk_buff *skb, int attrtype, u64 value)
{
	return nla_put(skb, attrtype, sizeof(u64), &value);
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
 * nla_put_msecs - Add a msecs netlink attribute to a socket buffer
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @jiffies: number of msecs in jiffies
 */
static inline int nla_put_msecs(struct sk_buff *skb, int attrtype,
				unsigned long jiffies)
{
	u64 tmp = jiffies_to_msecs(jiffies);
	return nla_put(skb, attrtype, sizeof(u64), &tmp);
}

#define NLA_PUT(skb, attrtype, attrlen, data) \
	do { \
		if (nla_put(skb, attrtype, attrlen, data) < 0) \
			goto nla_put_failure; \
	} while(0)

#define NLA_PUT_TYPE(skb, type, attrtype, value) \
	do { \
		type __tmp = value; \
		NLA_PUT(skb, attrtype, sizeof(type), &__tmp); \
	} while(0)

#define NLA_PUT_U8(skb, attrtype, value) \
	NLA_PUT_TYPE(skb, u8, attrtype, value)

#define NLA_PUT_U16(skb, attrtype, value) \
	NLA_PUT_TYPE(skb, u16, attrtype, value)

#define NLA_PUT_U32(skb, attrtype, value) \
	NLA_PUT_TYPE(skb, u32, attrtype, value)

#define NLA_PUT_U64(skb, attrtype, value) \
	NLA_PUT_TYPE(skb, u64, attrtype, value)

#define NLA_PUT_STRING(skb, attrtype, value) \
	NLA_PUT(skb, attrtype, strlen(value) + 1, value)

#define NLA_PUT_FLAG(skb, attrtype, value) \
	NLA_PUT(skb, attrtype, 0, NULL)

#define NLA_PUT_MSECS(skb, attrtype, jiffies) \
	NLA_PUT_U64(skb, attrtype, jiffies_to_msecs(jiffies))

/**
 * nla_get_u32 - return payload of u32 attribute
 * @nla: u32 netlink attribute
 */
static inline u32 nla_get_u32(struct nlattr *nla)
{
	return *(u32 *) nla_data(nla);
}

/**
 * nla_get_u16 - return payload of u16 attribute
 * @nla: u16 netlink attribute
 */
static inline u16 nla_get_u16(struct nlattr *nla)
{
	return *(u16 *) nla_data(nla);
}

/**
 * nla_get_u8 - return payload of u8 attribute
 * @nla: u8 netlink attribute
 */
static inline u8 nla_get_u8(struct nlattr *nla)
{
	return *(u8 *) nla_data(nla);
}

/**
 * nla_get_u64 - return payload of u64 attribute
 * @nla: u64 netlink attribute
 */
static inline u64 nla_get_u64(struct nlattr *nla)
{
	u64 tmp;

	nla_memcpy(&tmp, nla, sizeof(tmp));

	return tmp;
}

/**
 * nla_get_flag - return payload of flag attribute
 * @nla: flag netlink attribute
 */
static inline int nla_get_flag(struct nlattr *nla)
{
	return !!nla;
}

/**
 * nla_get_msecs - return payload of msecs attribute
 * @nla: msecs netlink attribute
 *
 * Returns the number of milliseconds in jiffies.
 */
static inline unsigned long nla_get_msecs(struct nlattr *nla)
{
	u64 msecs = nla_get_u64(nla);

	return msecs_to_jiffies((unsigned long) msecs);
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
	struct nlattr *start = (struct nlattr *) skb->tail;

	if (nla_put(skb, attrtype, 0, NULL) < 0)
		return NULL;

	return start;
}

/**
 * nla_nest_end - Finalize nesting of attributes
 * @skb: socket buffer the attribtues are stored in
 * @start: container attribute
 *
 * Corrects the container attribute header to include the all
 * appeneded attributes.
 *
 * Returns the total data length of the skb.
 */
static inline int nla_nest_end(struct sk_buff *skb, struct nlattr *start)
{
	start->nla_len = skb->tail - (unsigned char *) start;
	return skb->len;
}

/**
 * nla_nest_cancel - Cancel nesting of attributes
 * @skb: socket buffer the message is stored in
 * @start: container attribute
 *
 * Removes the container attribute and including all nested
 * attributes. Returns -1.
 */
static inline int nla_nest_cancel(struct sk_buff *skb, struct nlattr *start)
{
	if (start)
		skb_trim(skb, (unsigned char *) start - skb->data);

	return -1;
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

#endif
