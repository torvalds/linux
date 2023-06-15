/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _NFNL_HOOK_H_
#define _NFNL_HOOK_H_

enum nfnl_hook_msg_types {
	NFNL_MSG_HOOK_GET,
	NFNL_MSG_HOOK_MAX,
};

/**
 * enum nfnl_hook_attributes - netfilter hook netlink attributes
 *
 * @NFNLA_HOOK_HOOKNUM: netfilter hook number (NLA_U32)
 * @NFNLA_HOOK_PRIORITY: netfilter hook priority (NLA_U32)
 * @NFNLA_HOOK_DEV: netdevice name (NLA_STRING)
 * @NFNLA_HOOK_FUNCTION_NAME: hook function name (NLA_STRING)
 * @NFNLA_HOOK_MODULE_NAME: kernel module that registered this hook (NLA_STRING)
 * @NFNLA_HOOK_CHAIN_INFO: basechain hook metadata (NLA_NESTED)
 */
enum nfnl_hook_attributes {
	NFNLA_HOOK_UNSPEC,
	NFNLA_HOOK_HOOKNUM,
	NFNLA_HOOK_PRIORITY,
	NFNLA_HOOK_DEV,
	NFNLA_HOOK_FUNCTION_NAME,
	NFNLA_HOOK_MODULE_NAME,
	NFNLA_HOOK_CHAIN_INFO,
	__NFNLA_HOOK_MAX
};
#define NFNLA_HOOK_MAX		(__NFNLA_HOOK_MAX - 1)

/**
 * enum nfnl_hook_chain_info_attributes - chain description
 *
 * @NFNLA_HOOK_INFO_DESC: nft chain and table name (NLA_NESTED)
 * @NFNLA_HOOK_INFO_TYPE: chain type (enum nfnl_hook_chaintype) (NLA_U32)
 *
 * NFNLA_HOOK_INFO_DESC depends on NFNLA_HOOK_INFO_TYPE value:
 *   NFNL_HOOK_TYPE_NFTABLES: enum nft_table_attributes
 *   NFNL_HOOK_TYPE_BPF: enum nfnl_hook_bpf_attributes
 */
enum nfnl_hook_chain_info_attributes {
	NFNLA_HOOK_INFO_UNSPEC,
	NFNLA_HOOK_INFO_DESC,
	NFNLA_HOOK_INFO_TYPE,
	__NFNLA_HOOK_INFO_MAX,
};
#define NFNLA_HOOK_INFO_MAX (__NFNLA_HOOK_INFO_MAX - 1)

enum nfnl_hook_chain_desc_attributes {
	NFNLA_CHAIN_UNSPEC,
	NFNLA_CHAIN_TABLE,
	NFNLA_CHAIN_FAMILY,
	NFNLA_CHAIN_NAME,
	__NFNLA_CHAIN_MAX,
};
#define NFNLA_CHAIN_MAX (__NFNLA_CHAIN_MAX - 1)

/**
 * enum nfnl_hook_chaintype - chain type
 *
 * @NFNL_HOOK_TYPE_NFTABLES: nf_tables base chain
 * @NFNL_HOOK_TYPE_BPF: bpf program
 */
enum nfnl_hook_chaintype {
	NFNL_HOOK_TYPE_NFTABLES = 0x1,
	NFNL_HOOK_TYPE_BPF,
};

/**
 * enum nfnl_hook_bpf_attributes - bpf prog description
 *
 * @NFNLA_HOOK_BPF_ID: bpf program id (NLA_U32)
 */
enum nfnl_hook_bpf_attributes {
	NFNLA_HOOK_BPF_UNSPEC,
	NFNLA_HOOK_BPF_ID,
	__NFNLA_HOOK_BPF_MAX,
};
#define NFNLA_HOOK_BPF_MAX (__NFNLA_HOOK_BPF_MAX - 1)

#endif /* _NFNL_HOOK_H */
