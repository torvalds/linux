/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_BTF_IDS_H
#define _LINUX_BTF_IDS_H

#ifdef CONFIG_DEBUG_INFO_BTF

#include <linux/compiler.h> /* for __PASTE */

/*
 * Following macros help to define lists of BTF IDs placed
 * in .BTF_ids section. They are initially filled with zeros
 * (during compilation) and resolved later during the
 * linking phase by resolve_btfids tool.
 *
 * Any change in list layout must be reflected in resolve_btfids
 * tool logic.
 */

#define BTF_IDS_SECTION ".BTF_ids"

#define ____BTF_ID(symbol)				\
asm(							\
".pushsection " BTF_IDS_SECTION ",\"a\";       \n"	\
".local " #symbol " ;                          \n"	\
".type  " #symbol ", STT_OBJECT;               \n"	\
".size  " #symbol ", 4;                        \n"	\
#symbol ":                                     \n"	\
".zero 4                                       \n"	\
".popsection;                                  \n");

#define __BTF_ID(symbol) \
	____BTF_ID(symbol)

#define __ID(prefix) \
	__PASTE(prefix, __COUNTER__)

/*
 * The BTF_ID defines unique symbol for each ID pointing
 * to 4 zero bytes.
 */
#define BTF_ID(prefix, name) \
	__BTF_ID(__ID(__BTF_ID__##prefix##__##name##__))

/*
 * The BTF_ID_LIST macro defines pure (unsorted) list
 * of BTF IDs, with following layout:
 *
 * BTF_ID_LIST(list1)
 * BTF_ID(type1, name1)
 * BTF_ID(type2, name2)
 *
 * list1:
 * __BTF_ID__type1__name1__1:
 * .zero 4
 * __BTF_ID__type2__name2__2:
 * .zero 4
 *
 */
#define __BTF_ID_LIST(name, scope)			\
asm(							\
".pushsection " BTF_IDS_SECTION ",\"a\";       \n"	\
"." #scope " " #name ";                        \n"	\
#name ":;                                      \n"	\
".popsection;                                  \n");	\

#define BTF_ID_LIST(name)				\
__BTF_ID_LIST(name, local)				\
extern u32 name[];

#define BTF_ID_LIST_GLOBAL(name)			\
__BTF_ID_LIST(name, globl)

/*
 * The BTF_ID_UNUSED macro defines 4 zero bytes.
 * It's used when we want to define 'unused' entry
 * in BTF_ID_LIST, like:
 *
 *   BTF_ID_LIST(bpf_skb_output_btf_ids)
 *   BTF_ID(struct, sk_buff)
 *   BTF_ID_UNUSED
 *   BTF_ID(struct, task_struct)
 */

#define BTF_ID_UNUSED					\
asm(							\
".pushsection " BTF_IDS_SECTION ",\"a\";       \n"	\
".zero 4                                       \n"	\
".popsection;                                  \n");

#else

#define BTF_ID_LIST(name) static u32 name[5];
#define BTF_ID(prefix, name)
#define BTF_ID_UNUSED
#define BTF_ID_LIST_GLOBAL(name) u32 name[1];

#endif /* CONFIG_DEBUG_INFO_BTF */

#ifdef CONFIG_NET
/* Define a list of socket types which can be the argument for
 * skc_to_*_sock() helpers. All these sockets should have
 * sock_common as the first argument in its memory layout.
 */
#define BTF_SOCK_TYPE_xxx \
	BTF_SOCK_TYPE(BTF_SOCK_TYPE_INET, inet_sock)			\
	BTF_SOCK_TYPE(BTF_SOCK_TYPE_INET_CONN, inet_connection_sock)	\
	BTF_SOCK_TYPE(BTF_SOCK_TYPE_INET_REQ, inet_request_sock)	\
	BTF_SOCK_TYPE(BTF_SOCK_TYPE_INET_TW, inet_timewait_sock)	\
	BTF_SOCK_TYPE(BTF_SOCK_TYPE_REQ, request_sock)			\
	BTF_SOCK_TYPE(BTF_SOCK_TYPE_SOCK, sock)				\
	BTF_SOCK_TYPE(BTF_SOCK_TYPE_SOCK_COMMON, sock_common)		\
	BTF_SOCK_TYPE(BTF_SOCK_TYPE_TCP, tcp_sock)			\
	BTF_SOCK_TYPE(BTF_SOCK_TYPE_TCP_REQ, tcp_request_sock)		\
	BTF_SOCK_TYPE(BTF_SOCK_TYPE_TCP_TW, tcp_timewait_sock)		\
	BTF_SOCK_TYPE(BTF_SOCK_TYPE_TCP6, tcp6_sock)			\
	BTF_SOCK_TYPE(BTF_SOCK_TYPE_UDP, udp_sock)			\
	BTF_SOCK_TYPE(BTF_SOCK_TYPE_UDP6, udp6_sock)

enum {
#define BTF_SOCK_TYPE(name, str) name,
BTF_SOCK_TYPE_xxx
#undef BTF_SOCK_TYPE
MAX_BTF_SOCK_TYPE,
};

extern u32 btf_sock_ids[];
#endif

#endif
