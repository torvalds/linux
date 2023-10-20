/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/ethtool.yaml */
/* YNL-GEN user header */
/* YNL-ARG --user-header linux/ethtool_netlink.h --exclude-op stats-get */

#ifndef _LINUX_ETHTOOL_GEN_H
#define _LINUX_ETHTOOL_GEN_H

#include <stdlib.h>
#include <string.h>
#include <linux/types.h>
#include <linux/ethtool.h>

struct ynl_sock;

extern const struct ynl_family ynl_ethtool_family;

/* Enums */
const char *ethtool_op_str(int op);
const char *ethtool_udp_tunnel_type_str(int value);
const char *ethtool_stringset_str(enum ethtool_stringset value);

/* Common nested types */
struct ethtool_header {
	struct {
		__u32 dev_index:1;
		__u32 dev_name_len;
		__u32 flags:1;
	} _present;

	__u32 dev_index;
	char *dev_name;
	__u32 flags;
};

struct ethtool_pause_stat {
	struct {
		__u32 tx_frames:1;
		__u32 rx_frames:1;
	} _present;

	__u64 tx_frames;
	__u64 rx_frames;
};

struct ethtool_cable_test_tdr_cfg {
	struct {
		__u32 first:1;
		__u32 last:1;
		__u32 step:1;
		__u32 pair:1;
	} _present;

	__u32 first;
	__u32 last;
	__u32 step;
	__u8 pair;
};

struct ethtool_fec_stat {
	struct {
		__u32 corrected_len;
		__u32 uncorr_len;
		__u32 corr_bits_len;
	} _present;

	void *corrected;
	void *uncorr;
	void *corr_bits;
};

struct ethtool_mm_stat {
	struct {
		__u32 reassembly_errors:1;
		__u32 smd_errors:1;
		__u32 reassembly_ok:1;
		__u32 rx_frag_count:1;
		__u32 tx_frag_count:1;
		__u32 hold_count:1;
	} _present;

	__u64 reassembly_errors;
	__u64 smd_errors;
	__u64 reassembly_ok;
	__u64 rx_frag_count;
	__u64 tx_frag_count;
	__u64 hold_count;
};

struct ethtool_cable_result {
	struct {
		__u32 pair:1;
		__u32 code:1;
	} _present;

	__u8 pair;
	__u8 code;
};

struct ethtool_cable_fault_length {
	struct {
		__u32 pair:1;
		__u32 cm:1;
	} _present;

	__u8 pair;
	__u32 cm;
};

struct ethtool_bitset_bit {
	struct {
		__u32 index:1;
		__u32 name_len;
		__u32 value:1;
	} _present;

	__u32 index;
	char *name;
};

struct ethtool_tunnel_udp_entry {
	struct {
		__u32 port:1;
		__u32 type:1;
	} _present;

	__u16 port /* big-endian */;
	__u32 type;
};

struct ethtool_string {
	struct {
		__u32 index:1;
		__u32 value_len;
	} _present;

	__u32 index;
	char *value;
};

struct ethtool_cable_nest {
	struct {
		__u32 result:1;
		__u32 fault_length:1;
	} _present;

	struct ethtool_cable_result result;
	struct ethtool_cable_fault_length fault_length;
};

struct ethtool_bitset_bits {
	unsigned int n_bit;
	struct ethtool_bitset_bit *bit;
};

struct ethtool_strings {
	unsigned int n_string;
	struct ethtool_string *string;
};

struct ethtool_bitset {
	struct {
		__u32 nomask:1;
		__u32 size:1;
		__u32 bits:1;
	} _present;

	__u32 size;
	struct ethtool_bitset_bits bits;
};

struct ethtool_stringset_ {
	struct {
		__u32 id:1;
		__u32 count:1;
	} _present;

	__u32 id;
	__u32 count;
	unsigned int n_strings;
	struct ethtool_strings *strings;
};

struct ethtool_tunnel_udp_table {
	struct {
		__u32 size:1;
		__u32 types:1;
	} _present;

	__u32 size;
	struct ethtool_bitset types;
	unsigned int n_entry;
	struct ethtool_tunnel_udp_entry *entry;
};

struct ethtool_stringsets {
	unsigned int n_stringset;
	struct ethtool_stringset_ *stringset;
};

struct ethtool_tunnel_udp {
	struct {
		__u32 table:1;
	} _present;

	struct ethtool_tunnel_udp_table table;
};

/* ============== ETHTOOL_MSG_STRSET_GET ============== */
/* ETHTOOL_MSG_STRSET_GET - do */
struct ethtool_strset_get_req {
	struct {
		__u32 header:1;
		__u32 stringsets:1;
		__u32 counts_only:1;
	} _present;

	struct ethtool_header header;
	struct ethtool_stringsets stringsets;
};

static inline struct ethtool_strset_get_req *ethtool_strset_get_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_strset_get_req));
}
void ethtool_strset_get_req_free(struct ethtool_strset_get_req *req);

static inline void
ethtool_strset_get_req_set_header_dev_index(struct ethtool_strset_get_req *req,
					    __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_strset_get_req_set_header_dev_name(struct ethtool_strset_get_req *req,
					   const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_strset_get_req_set_header_flags(struct ethtool_strset_get_req *req,
					__u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}
static inline void
__ethtool_strset_get_req_set_stringsets_stringset(struct ethtool_strset_get_req *req,
						  struct ethtool_stringset_ *stringset,
						  unsigned int n_stringset)
{
	free(req->stringsets.stringset);
	req->stringsets.stringset = stringset;
	req->stringsets.n_stringset = n_stringset;
}
static inline void
ethtool_strset_get_req_set_counts_only(struct ethtool_strset_get_req *req)
{
	req->_present.counts_only = 1;
}

struct ethtool_strset_get_rsp {
	struct {
		__u32 header:1;
		__u32 stringsets:1;
	} _present;

	struct ethtool_header header;
	struct ethtool_stringsets stringsets;
};

void ethtool_strset_get_rsp_free(struct ethtool_strset_get_rsp *rsp);

/*
 * Get string set from the kernel.
 */
struct ethtool_strset_get_rsp *
ethtool_strset_get(struct ynl_sock *ys, struct ethtool_strset_get_req *req);

/* ETHTOOL_MSG_STRSET_GET - dump */
struct ethtool_strset_get_req_dump {
	struct {
		__u32 header:1;
		__u32 stringsets:1;
		__u32 counts_only:1;
	} _present;

	struct ethtool_header header;
	struct ethtool_stringsets stringsets;
};

static inline struct ethtool_strset_get_req_dump *
ethtool_strset_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_strset_get_req_dump));
}
void ethtool_strset_get_req_dump_free(struct ethtool_strset_get_req_dump *req);

static inline void
ethtool_strset_get_req_dump_set_header_dev_index(struct ethtool_strset_get_req_dump *req,
						 __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_strset_get_req_dump_set_header_dev_name(struct ethtool_strset_get_req_dump *req,
						const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_strset_get_req_dump_set_header_flags(struct ethtool_strset_get_req_dump *req,
					     __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}
static inline void
__ethtool_strset_get_req_dump_set_stringsets_stringset(struct ethtool_strset_get_req_dump *req,
						       struct ethtool_stringset_ *stringset,
						       unsigned int n_stringset)
{
	free(req->stringsets.stringset);
	req->stringsets.stringset = stringset;
	req->stringsets.n_stringset = n_stringset;
}
static inline void
ethtool_strset_get_req_dump_set_counts_only(struct ethtool_strset_get_req_dump *req)
{
	req->_present.counts_only = 1;
}

struct ethtool_strset_get_list {
	struct ethtool_strset_get_list *next;
	struct ethtool_strset_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_strset_get_list_free(struct ethtool_strset_get_list *rsp);

struct ethtool_strset_get_list *
ethtool_strset_get_dump(struct ynl_sock *ys,
			struct ethtool_strset_get_req_dump *req);

/* ============== ETHTOOL_MSG_LINKINFO_GET ============== */
/* ETHTOOL_MSG_LINKINFO_GET - do */
struct ethtool_linkinfo_get_req {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_linkinfo_get_req *
ethtool_linkinfo_get_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_linkinfo_get_req));
}
void ethtool_linkinfo_get_req_free(struct ethtool_linkinfo_get_req *req);

static inline void
ethtool_linkinfo_get_req_set_header_dev_index(struct ethtool_linkinfo_get_req *req,
					      __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_linkinfo_get_req_set_header_dev_name(struct ethtool_linkinfo_get_req *req,
					     const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_linkinfo_get_req_set_header_flags(struct ethtool_linkinfo_get_req *req,
					  __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_linkinfo_get_rsp {
	struct {
		__u32 header:1;
		__u32 port:1;
		__u32 phyaddr:1;
		__u32 tp_mdix:1;
		__u32 tp_mdix_ctrl:1;
		__u32 transceiver:1;
	} _present;

	struct ethtool_header header;
	__u8 port;
	__u8 phyaddr;
	__u8 tp_mdix;
	__u8 tp_mdix_ctrl;
	__u8 transceiver;
};

void ethtool_linkinfo_get_rsp_free(struct ethtool_linkinfo_get_rsp *rsp);

/*
 * Get link info.
 */
struct ethtool_linkinfo_get_rsp *
ethtool_linkinfo_get(struct ynl_sock *ys, struct ethtool_linkinfo_get_req *req);

/* ETHTOOL_MSG_LINKINFO_GET - dump */
struct ethtool_linkinfo_get_req_dump {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_linkinfo_get_req_dump *
ethtool_linkinfo_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_linkinfo_get_req_dump));
}
void
ethtool_linkinfo_get_req_dump_free(struct ethtool_linkinfo_get_req_dump *req);

static inline void
ethtool_linkinfo_get_req_dump_set_header_dev_index(struct ethtool_linkinfo_get_req_dump *req,
						   __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_linkinfo_get_req_dump_set_header_dev_name(struct ethtool_linkinfo_get_req_dump *req,
						  const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_linkinfo_get_req_dump_set_header_flags(struct ethtool_linkinfo_get_req_dump *req,
					       __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_linkinfo_get_list {
	struct ethtool_linkinfo_get_list *next;
	struct ethtool_linkinfo_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_linkinfo_get_list_free(struct ethtool_linkinfo_get_list *rsp);

struct ethtool_linkinfo_get_list *
ethtool_linkinfo_get_dump(struct ynl_sock *ys,
			  struct ethtool_linkinfo_get_req_dump *req);

/* ETHTOOL_MSG_LINKINFO_GET - notify */
struct ethtool_linkinfo_get_ntf {
	__u16 family;
	__u8 cmd;
	struct ynl_ntf_base_type *next;
	void (*free)(struct ethtool_linkinfo_get_ntf *ntf);
	struct ethtool_linkinfo_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_linkinfo_get_ntf_free(struct ethtool_linkinfo_get_ntf *rsp);

/* ============== ETHTOOL_MSG_LINKINFO_SET ============== */
/* ETHTOOL_MSG_LINKINFO_SET - do */
struct ethtool_linkinfo_set_req {
	struct {
		__u32 header:1;
		__u32 port:1;
		__u32 phyaddr:1;
		__u32 tp_mdix:1;
		__u32 tp_mdix_ctrl:1;
		__u32 transceiver:1;
	} _present;

	struct ethtool_header header;
	__u8 port;
	__u8 phyaddr;
	__u8 tp_mdix;
	__u8 tp_mdix_ctrl;
	__u8 transceiver;
};

static inline struct ethtool_linkinfo_set_req *
ethtool_linkinfo_set_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_linkinfo_set_req));
}
void ethtool_linkinfo_set_req_free(struct ethtool_linkinfo_set_req *req);

static inline void
ethtool_linkinfo_set_req_set_header_dev_index(struct ethtool_linkinfo_set_req *req,
					      __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_linkinfo_set_req_set_header_dev_name(struct ethtool_linkinfo_set_req *req,
					     const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_linkinfo_set_req_set_header_flags(struct ethtool_linkinfo_set_req *req,
					  __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}
static inline void
ethtool_linkinfo_set_req_set_port(struct ethtool_linkinfo_set_req *req,
				  __u8 port)
{
	req->_present.port = 1;
	req->port = port;
}
static inline void
ethtool_linkinfo_set_req_set_phyaddr(struct ethtool_linkinfo_set_req *req,
				     __u8 phyaddr)
{
	req->_present.phyaddr = 1;
	req->phyaddr = phyaddr;
}
static inline void
ethtool_linkinfo_set_req_set_tp_mdix(struct ethtool_linkinfo_set_req *req,
				     __u8 tp_mdix)
{
	req->_present.tp_mdix = 1;
	req->tp_mdix = tp_mdix;
}
static inline void
ethtool_linkinfo_set_req_set_tp_mdix_ctrl(struct ethtool_linkinfo_set_req *req,
					  __u8 tp_mdix_ctrl)
{
	req->_present.tp_mdix_ctrl = 1;
	req->tp_mdix_ctrl = tp_mdix_ctrl;
}
static inline void
ethtool_linkinfo_set_req_set_transceiver(struct ethtool_linkinfo_set_req *req,
					 __u8 transceiver)
{
	req->_present.transceiver = 1;
	req->transceiver = transceiver;
}

/*
 * Set link info.
 */
int ethtool_linkinfo_set(struct ynl_sock *ys,
			 struct ethtool_linkinfo_set_req *req);

/* ============== ETHTOOL_MSG_LINKMODES_GET ============== */
/* ETHTOOL_MSG_LINKMODES_GET - do */
struct ethtool_linkmodes_get_req {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_linkmodes_get_req *
ethtool_linkmodes_get_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_linkmodes_get_req));
}
void ethtool_linkmodes_get_req_free(struct ethtool_linkmodes_get_req *req);

static inline void
ethtool_linkmodes_get_req_set_header_dev_index(struct ethtool_linkmodes_get_req *req,
					       __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_linkmodes_get_req_set_header_dev_name(struct ethtool_linkmodes_get_req *req,
					      const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_linkmodes_get_req_set_header_flags(struct ethtool_linkmodes_get_req *req,
					   __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_linkmodes_get_rsp {
	struct {
		__u32 header:1;
		__u32 autoneg:1;
		__u32 ours:1;
		__u32 peer:1;
		__u32 speed:1;
		__u32 duplex:1;
		__u32 master_slave_cfg:1;
		__u32 master_slave_state:1;
		__u32 lanes:1;
		__u32 rate_matching:1;
	} _present;

	struct ethtool_header header;
	__u8 autoneg;
	struct ethtool_bitset ours;
	struct ethtool_bitset peer;
	__u32 speed;
	__u8 duplex;
	__u8 master_slave_cfg;
	__u8 master_slave_state;
	__u32 lanes;
	__u8 rate_matching;
};

void ethtool_linkmodes_get_rsp_free(struct ethtool_linkmodes_get_rsp *rsp);

/*
 * Get link modes.
 */
struct ethtool_linkmodes_get_rsp *
ethtool_linkmodes_get(struct ynl_sock *ys,
		      struct ethtool_linkmodes_get_req *req);

/* ETHTOOL_MSG_LINKMODES_GET - dump */
struct ethtool_linkmodes_get_req_dump {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_linkmodes_get_req_dump *
ethtool_linkmodes_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_linkmodes_get_req_dump));
}
void
ethtool_linkmodes_get_req_dump_free(struct ethtool_linkmodes_get_req_dump *req);

static inline void
ethtool_linkmodes_get_req_dump_set_header_dev_index(struct ethtool_linkmodes_get_req_dump *req,
						    __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_linkmodes_get_req_dump_set_header_dev_name(struct ethtool_linkmodes_get_req_dump *req,
						   const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_linkmodes_get_req_dump_set_header_flags(struct ethtool_linkmodes_get_req_dump *req,
						__u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_linkmodes_get_list {
	struct ethtool_linkmodes_get_list *next;
	struct ethtool_linkmodes_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_linkmodes_get_list_free(struct ethtool_linkmodes_get_list *rsp);

struct ethtool_linkmodes_get_list *
ethtool_linkmodes_get_dump(struct ynl_sock *ys,
			   struct ethtool_linkmodes_get_req_dump *req);

/* ETHTOOL_MSG_LINKMODES_GET - notify */
struct ethtool_linkmodes_get_ntf {
	__u16 family;
	__u8 cmd;
	struct ynl_ntf_base_type *next;
	void (*free)(struct ethtool_linkmodes_get_ntf *ntf);
	struct ethtool_linkmodes_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_linkmodes_get_ntf_free(struct ethtool_linkmodes_get_ntf *rsp);

/* ============== ETHTOOL_MSG_LINKMODES_SET ============== */
/* ETHTOOL_MSG_LINKMODES_SET - do */
struct ethtool_linkmodes_set_req {
	struct {
		__u32 header:1;
		__u32 autoneg:1;
		__u32 ours:1;
		__u32 peer:1;
		__u32 speed:1;
		__u32 duplex:1;
		__u32 master_slave_cfg:1;
		__u32 master_slave_state:1;
		__u32 lanes:1;
		__u32 rate_matching:1;
	} _present;

	struct ethtool_header header;
	__u8 autoneg;
	struct ethtool_bitset ours;
	struct ethtool_bitset peer;
	__u32 speed;
	__u8 duplex;
	__u8 master_slave_cfg;
	__u8 master_slave_state;
	__u32 lanes;
	__u8 rate_matching;
};

static inline struct ethtool_linkmodes_set_req *
ethtool_linkmodes_set_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_linkmodes_set_req));
}
void ethtool_linkmodes_set_req_free(struct ethtool_linkmodes_set_req *req);

static inline void
ethtool_linkmodes_set_req_set_header_dev_index(struct ethtool_linkmodes_set_req *req,
					       __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_linkmodes_set_req_set_header_dev_name(struct ethtool_linkmodes_set_req *req,
					      const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_linkmodes_set_req_set_header_flags(struct ethtool_linkmodes_set_req *req,
					   __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}
static inline void
ethtool_linkmodes_set_req_set_autoneg(struct ethtool_linkmodes_set_req *req,
				      __u8 autoneg)
{
	req->_present.autoneg = 1;
	req->autoneg = autoneg;
}
static inline void
ethtool_linkmodes_set_req_set_ours_nomask(struct ethtool_linkmodes_set_req *req)
{
	req->_present.ours = 1;
	req->ours._present.nomask = 1;
}
static inline void
ethtool_linkmodes_set_req_set_ours_size(struct ethtool_linkmodes_set_req *req,
					__u32 size)
{
	req->_present.ours = 1;
	req->ours._present.size = 1;
	req->ours.size = size;
}
static inline void
__ethtool_linkmodes_set_req_set_ours_bits_bit(struct ethtool_linkmodes_set_req *req,
					      struct ethtool_bitset_bit *bit,
					      unsigned int n_bit)
{
	free(req->ours.bits.bit);
	req->ours.bits.bit = bit;
	req->ours.bits.n_bit = n_bit;
}
static inline void
ethtool_linkmodes_set_req_set_peer_nomask(struct ethtool_linkmodes_set_req *req)
{
	req->_present.peer = 1;
	req->peer._present.nomask = 1;
}
static inline void
ethtool_linkmodes_set_req_set_peer_size(struct ethtool_linkmodes_set_req *req,
					__u32 size)
{
	req->_present.peer = 1;
	req->peer._present.size = 1;
	req->peer.size = size;
}
static inline void
__ethtool_linkmodes_set_req_set_peer_bits_bit(struct ethtool_linkmodes_set_req *req,
					      struct ethtool_bitset_bit *bit,
					      unsigned int n_bit)
{
	free(req->peer.bits.bit);
	req->peer.bits.bit = bit;
	req->peer.bits.n_bit = n_bit;
}
static inline void
ethtool_linkmodes_set_req_set_speed(struct ethtool_linkmodes_set_req *req,
				    __u32 speed)
{
	req->_present.speed = 1;
	req->speed = speed;
}
static inline void
ethtool_linkmodes_set_req_set_duplex(struct ethtool_linkmodes_set_req *req,
				     __u8 duplex)
{
	req->_present.duplex = 1;
	req->duplex = duplex;
}
static inline void
ethtool_linkmodes_set_req_set_master_slave_cfg(struct ethtool_linkmodes_set_req *req,
					       __u8 master_slave_cfg)
{
	req->_present.master_slave_cfg = 1;
	req->master_slave_cfg = master_slave_cfg;
}
static inline void
ethtool_linkmodes_set_req_set_master_slave_state(struct ethtool_linkmodes_set_req *req,
						 __u8 master_slave_state)
{
	req->_present.master_slave_state = 1;
	req->master_slave_state = master_slave_state;
}
static inline void
ethtool_linkmodes_set_req_set_lanes(struct ethtool_linkmodes_set_req *req,
				    __u32 lanes)
{
	req->_present.lanes = 1;
	req->lanes = lanes;
}
static inline void
ethtool_linkmodes_set_req_set_rate_matching(struct ethtool_linkmodes_set_req *req,
					    __u8 rate_matching)
{
	req->_present.rate_matching = 1;
	req->rate_matching = rate_matching;
}

/*
 * Set link modes.
 */
int ethtool_linkmodes_set(struct ynl_sock *ys,
			  struct ethtool_linkmodes_set_req *req);

/* ============== ETHTOOL_MSG_LINKSTATE_GET ============== */
/* ETHTOOL_MSG_LINKSTATE_GET - do */
struct ethtool_linkstate_get_req {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_linkstate_get_req *
ethtool_linkstate_get_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_linkstate_get_req));
}
void ethtool_linkstate_get_req_free(struct ethtool_linkstate_get_req *req);

static inline void
ethtool_linkstate_get_req_set_header_dev_index(struct ethtool_linkstate_get_req *req,
					       __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_linkstate_get_req_set_header_dev_name(struct ethtool_linkstate_get_req *req,
					      const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_linkstate_get_req_set_header_flags(struct ethtool_linkstate_get_req *req,
					   __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_linkstate_get_rsp {
	struct {
		__u32 header:1;
		__u32 link:1;
		__u32 sqi:1;
		__u32 sqi_max:1;
		__u32 ext_state:1;
		__u32 ext_substate:1;
		__u32 ext_down_cnt:1;
	} _present;

	struct ethtool_header header;
	__u8 link;
	__u32 sqi;
	__u32 sqi_max;
	__u8 ext_state;
	__u8 ext_substate;
	__u32 ext_down_cnt;
};

void ethtool_linkstate_get_rsp_free(struct ethtool_linkstate_get_rsp *rsp);

/*
 * Get link state.
 */
struct ethtool_linkstate_get_rsp *
ethtool_linkstate_get(struct ynl_sock *ys,
		      struct ethtool_linkstate_get_req *req);

/* ETHTOOL_MSG_LINKSTATE_GET - dump */
struct ethtool_linkstate_get_req_dump {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_linkstate_get_req_dump *
ethtool_linkstate_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_linkstate_get_req_dump));
}
void
ethtool_linkstate_get_req_dump_free(struct ethtool_linkstate_get_req_dump *req);

static inline void
ethtool_linkstate_get_req_dump_set_header_dev_index(struct ethtool_linkstate_get_req_dump *req,
						    __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_linkstate_get_req_dump_set_header_dev_name(struct ethtool_linkstate_get_req_dump *req,
						   const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_linkstate_get_req_dump_set_header_flags(struct ethtool_linkstate_get_req_dump *req,
						__u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_linkstate_get_list {
	struct ethtool_linkstate_get_list *next;
	struct ethtool_linkstate_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_linkstate_get_list_free(struct ethtool_linkstate_get_list *rsp);

struct ethtool_linkstate_get_list *
ethtool_linkstate_get_dump(struct ynl_sock *ys,
			   struct ethtool_linkstate_get_req_dump *req);

/* ============== ETHTOOL_MSG_DEBUG_GET ============== */
/* ETHTOOL_MSG_DEBUG_GET - do */
struct ethtool_debug_get_req {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_debug_get_req *ethtool_debug_get_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_debug_get_req));
}
void ethtool_debug_get_req_free(struct ethtool_debug_get_req *req);

static inline void
ethtool_debug_get_req_set_header_dev_index(struct ethtool_debug_get_req *req,
					   __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_debug_get_req_set_header_dev_name(struct ethtool_debug_get_req *req,
					  const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_debug_get_req_set_header_flags(struct ethtool_debug_get_req *req,
				       __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_debug_get_rsp {
	struct {
		__u32 header:1;
		__u32 msgmask:1;
	} _present;

	struct ethtool_header header;
	struct ethtool_bitset msgmask;
};

void ethtool_debug_get_rsp_free(struct ethtool_debug_get_rsp *rsp);

/*
 * Get debug message mask.
 */
struct ethtool_debug_get_rsp *
ethtool_debug_get(struct ynl_sock *ys, struct ethtool_debug_get_req *req);

/* ETHTOOL_MSG_DEBUG_GET - dump */
struct ethtool_debug_get_req_dump {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_debug_get_req_dump *
ethtool_debug_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_debug_get_req_dump));
}
void ethtool_debug_get_req_dump_free(struct ethtool_debug_get_req_dump *req);

static inline void
ethtool_debug_get_req_dump_set_header_dev_index(struct ethtool_debug_get_req_dump *req,
						__u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_debug_get_req_dump_set_header_dev_name(struct ethtool_debug_get_req_dump *req,
					       const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_debug_get_req_dump_set_header_flags(struct ethtool_debug_get_req_dump *req,
					    __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_debug_get_list {
	struct ethtool_debug_get_list *next;
	struct ethtool_debug_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_debug_get_list_free(struct ethtool_debug_get_list *rsp);

struct ethtool_debug_get_list *
ethtool_debug_get_dump(struct ynl_sock *ys,
		       struct ethtool_debug_get_req_dump *req);

/* ETHTOOL_MSG_DEBUG_GET - notify */
struct ethtool_debug_get_ntf {
	__u16 family;
	__u8 cmd;
	struct ynl_ntf_base_type *next;
	void (*free)(struct ethtool_debug_get_ntf *ntf);
	struct ethtool_debug_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_debug_get_ntf_free(struct ethtool_debug_get_ntf *rsp);

/* ============== ETHTOOL_MSG_DEBUG_SET ============== */
/* ETHTOOL_MSG_DEBUG_SET - do */
struct ethtool_debug_set_req {
	struct {
		__u32 header:1;
		__u32 msgmask:1;
	} _present;

	struct ethtool_header header;
	struct ethtool_bitset msgmask;
};

static inline struct ethtool_debug_set_req *ethtool_debug_set_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_debug_set_req));
}
void ethtool_debug_set_req_free(struct ethtool_debug_set_req *req);

static inline void
ethtool_debug_set_req_set_header_dev_index(struct ethtool_debug_set_req *req,
					   __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_debug_set_req_set_header_dev_name(struct ethtool_debug_set_req *req,
					  const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_debug_set_req_set_header_flags(struct ethtool_debug_set_req *req,
				       __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}
static inline void
ethtool_debug_set_req_set_msgmask_nomask(struct ethtool_debug_set_req *req)
{
	req->_present.msgmask = 1;
	req->msgmask._present.nomask = 1;
}
static inline void
ethtool_debug_set_req_set_msgmask_size(struct ethtool_debug_set_req *req,
				       __u32 size)
{
	req->_present.msgmask = 1;
	req->msgmask._present.size = 1;
	req->msgmask.size = size;
}
static inline void
__ethtool_debug_set_req_set_msgmask_bits_bit(struct ethtool_debug_set_req *req,
					     struct ethtool_bitset_bit *bit,
					     unsigned int n_bit)
{
	free(req->msgmask.bits.bit);
	req->msgmask.bits.bit = bit;
	req->msgmask.bits.n_bit = n_bit;
}

/*
 * Set debug message mask.
 */
int ethtool_debug_set(struct ynl_sock *ys, struct ethtool_debug_set_req *req);

/* ============== ETHTOOL_MSG_WOL_GET ============== */
/* ETHTOOL_MSG_WOL_GET - do */
struct ethtool_wol_get_req {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_wol_get_req *ethtool_wol_get_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_wol_get_req));
}
void ethtool_wol_get_req_free(struct ethtool_wol_get_req *req);

static inline void
ethtool_wol_get_req_set_header_dev_index(struct ethtool_wol_get_req *req,
					 __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_wol_get_req_set_header_dev_name(struct ethtool_wol_get_req *req,
					const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_wol_get_req_set_header_flags(struct ethtool_wol_get_req *req,
				     __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_wol_get_rsp {
	struct {
		__u32 header:1;
		__u32 modes:1;
		__u32 sopass_len;
	} _present;

	struct ethtool_header header;
	struct ethtool_bitset modes;
	void *sopass;
};

void ethtool_wol_get_rsp_free(struct ethtool_wol_get_rsp *rsp);

/*
 * Get WOL params.
 */
struct ethtool_wol_get_rsp *
ethtool_wol_get(struct ynl_sock *ys, struct ethtool_wol_get_req *req);

/* ETHTOOL_MSG_WOL_GET - dump */
struct ethtool_wol_get_req_dump {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_wol_get_req_dump *
ethtool_wol_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_wol_get_req_dump));
}
void ethtool_wol_get_req_dump_free(struct ethtool_wol_get_req_dump *req);

static inline void
ethtool_wol_get_req_dump_set_header_dev_index(struct ethtool_wol_get_req_dump *req,
					      __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_wol_get_req_dump_set_header_dev_name(struct ethtool_wol_get_req_dump *req,
					     const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_wol_get_req_dump_set_header_flags(struct ethtool_wol_get_req_dump *req,
					  __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_wol_get_list {
	struct ethtool_wol_get_list *next;
	struct ethtool_wol_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_wol_get_list_free(struct ethtool_wol_get_list *rsp);

struct ethtool_wol_get_list *
ethtool_wol_get_dump(struct ynl_sock *ys, struct ethtool_wol_get_req_dump *req);

/* ETHTOOL_MSG_WOL_GET - notify */
struct ethtool_wol_get_ntf {
	__u16 family;
	__u8 cmd;
	struct ynl_ntf_base_type *next;
	void (*free)(struct ethtool_wol_get_ntf *ntf);
	struct ethtool_wol_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_wol_get_ntf_free(struct ethtool_wol_get_ntf *rsp);

/* ============== ETHTOOL_MSG_WOL_SET ============== */
/* ETHTOOL_MSG_WOL_SET - do */
struct ethtool_wol_set_req {
	struct {
		__u32 header:1;
		__u32 modes:1;
		__u32 sopass_len;
	} _present;

	struct ethtool_header header;
	struct ethtool_bitset modes;
	void *sopass;
};

static inline struct ethtool_wol_set_req *ethtool_wol_set_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_wol_set_req));
}
void ethtool_wol_set_req_free(struct ethtool_wol_set_req *req);

static inline void
ethtool_wol_set_req_set_header_dev_index(struct ethtool_wol_set_req *req,
					 __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_wol_set_req_set_header_dev_name(struct ethtool_wol_set_req *req,
					const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_wol_set_req_set_header_flags(struct ethtool_wol_set_req *req,
				     __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}
static inline void
ethtool_wol_set_req_set_modes_nomask(struct ethtool_wol_set_req *req)
{
	req->_present.modes = 1;
	req->modes._present.nomask = 1;
}
static inline void
ethtool_wol_set_req_set_modes_size(struct ethtool_wol_set_req *req, __u32 size)
{
	req->_present.modes = 1;
	req->modes._present.size = 1;
	req->modes.size = size;
}
static inline void
__ethtool_wol_set_req_set_modes_bits_bit(struct ethtool_wol_set_req *req,
					 struct ethtool_bitset_bit *bit,
					 unsigned int n_bit)
{
	free(req->modes.bits.bit);
	req->modes.bits.bit = bit;
	req->modes.bits.n_bit = n_bit;
}
static inline void
ethtool_wol_set_req_set_sopass(struct ethtool_wol_set_req *req,
			       const void *sopass, size_t len)
{
	free(req->sopass);
	req->_present.sopass_len = len;
	req->sopass = malloc(req->_present.sopass_len);
	memcpy(req->sopass, sopass, req->_present.sopass_len);
}

/*
 * Set WOL params.
 */
int ethtool_wol_set(struct ynl_sock *ys, struct ethtool_wol_set_req *req);

/* ============== ETHTOOL_MSG_FEATURES_GET ============== */
/* ETHTOOL_MSG_FEATURES_GET - do */
struct ethtool_features_get_req {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_features_get_req *
ethtool_features_get_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_features_get_req));
}
void ethtool_features_get_req_free(struct ethtool_features_get_req *req);

static inline void
ethtool_features_get_req_set_header_dev_index(struct ethtool_features_get_req *req,
					      __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_features_get_req_set_header_dev_name(struct ethtool_features_get_req *req,
					     const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_features_get_req_set_header_flags(struct ethtool_features_get_req *req,
					  __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_features_get_rsp {
	struct {
		__u32 header:1;
		__u32 hw:1;
		__u32 wanted:1;
		__u32 active:1;
		__u32 nochange:1;
	} _present;

	struct ethtool_header header;
	struct ethtool_bitset hw;
	struct ethtool_bitset wanted;
	struct ethtool_bitset active;
	struct ethtool_bitset nochange;
};

void ethtool_features_get_rsp_free(struct ethtool_features_get_rsp *rsp);

/*
 * Get features.
 */
struct ethtool_features_get_rsp *
ethtool_features_get(struct ynl_sock *ys, struct ethtool_features_get_req *req);

/* ETHTOOL_MSG_FEATURES_GET - dump */
struct ethtool_features_get_req_dump {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_features_get_req_dump *
ethtool_features_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_features_get_req_dump));
}
void
ethtool_features_get_req_dump_free(struct ethtool_features_get_req_dump *req);

static inline void
ethtool_features_get_req_dump_set_header_dev_index(struct ethtool_features_get_req_dump *req,
						   __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_features_get_req_dump_set_header_dev_name(struct ethtool_features_get_req_dump *req,
						  const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_features_get_req_dump_set_header_flags(struct ethtool_features_get_req_dump *req,
					       __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_features_get_list {
	struct ethtool_features_get_list *next;
	struct ethtool_features_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_features_get_list_free(struct ethtool_features_get_list *rsp);

struct ethtool_features_get_list *
ethtool_features_get_dump(struct ynl_sock *ys,
			  struct ethtool_features_get_req_dump *req);

/* ETHTOOL_MSG_FEATURES_GET - notify */
struct ethtool_features_get_ntf {
	__u16 family;
	__u8 cmd;
	struct ynl_ntf_base_type *next;
	void (*free)(struct ethtool_features_get_ntf *ntf);
	struct ethtool_features_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_features_get_ntf_free(struct ethtool_features_get_ntf *rsp);

/* ============== ETHTOOL_MSG_FEATURES_SET ============== */
/* ETHTOOL_MSG_FEATURES_SET - do */
struct ethtool_features_set_req {
	struct {
		__u32 header:1;
		__u32 hw:1;
		__u32 wanted:1;
		__u32 active:1;
		__u32 nochange:1;
	} _present;

	struct ethtool_header header;
	struct ethtool_bitset hw;
	struct ethtool_bitset wanted;
	struct ethtool_bitset active;
	struct ethtool_bitset nochange;
};

static inline struct ethtool_features_set_req *
ethtool_features_set_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_features_set_req));
}
void ethtool_features_set_req_free(struct ethtool_features_set_req *req);

static inline void
ethtool_features_set_req_set_header_dev_index(struct ethtool_features_set_req *req,
					      __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_features_set_req_set_header_dev_name(struct ethtool_features_set_req *req,
					     const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_features_set_req_set_header_flags(struct ethtool_features_set_req *req,
					  __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}
static inline void
ethtool_features_set_req_set_hw_nomask(struct ethtool_features_set_req *req)
{
	req->_present.hw = 1;
	req->hw._present.nomask = 1;
}
static inline void
ethtool_features_set_req_set_hw_size(struct ethtool_features_set_req *req,
				     __u32 size)
{
	req->_present.hw = 1;
	req->hw._present.size = 1;
	req->hw.size = size;
}
static inline void
__ethtool_features_set_req_set_hw_bits_bit(struct ethtool_features_set_req *req,
					   struct ethtool_bitset_bit *bit,
					   unsigned int n_bit)
{
	free(req->hw.bits.bit);
	req->hw.bits.bit = bit;
	req->hw.bits.n_bit = n_bit;
}
static inline void
ethtool_features_set_req_set_wanted_nomask(struct ethtool_features_set_req *req)
{
	req->_present.wanted = 1;
	req->wanted._present.nomask = 1;
}
static inline void
ethtool_features_set_req_set_wanted_size(struct ethtool_features_set_req *req,
					 __u32 size)
{
	req->_present.wanted = 1;
	req->wanted._present.size = 1;
	req->wanted.size = size;
}
static inline void
__ethtool_features_set_req_set_wanted_bits_bit(struct ethtool_features_set_req *req,
					       struct ethtool_bitset_bit *bit,
					       unsigned int n_bit)
{
	free(req->wanted.bits.bit);
	req->wanted.bits.bit = bit;
	req->wanted.bits.n_bit = n_bit;
}
static inline void
ethtool_features_set_req_set_active_nomask(struct ethtool_features_set_req *req)
{
	req->_present.active = 1;
	req->active._present.nomask = 1;
}
static inline void
ethtool_features_set_req_set_active_size(struct ethtool_features_set_req *req,
					 __u32 size)
{
	req->_present.active = 1;
	req->active._present.size = 1;
	req->active.size = size;
}
static inline void
__ethtool_features_set_req_set_active_bits_bit(struct ethtool_features_set_req *req,
					       struct ethtool_bitset_bit *bit,
					       unsigned int n_bit)
{
	free(req->active.bits.bit);
	req->active.bits.bit = bit;
	req->active.bits.n_bit = n_bit;
}
static inline void
ethtool_features_set_req_set_nochange_nomask(struct ethtool_features_set_req *req)
{
	req->_present.nochange = 1;
	req->nochange._present.nomask = 1;
}
static inline void
ethtool_features_set_req_set_nochange_size(struct ethtool_features_set_req *req,
					   __u32 size)
{
	req->_present.nochange = 1;
	req->nochange._present.size = 1;
	req->nochange.size = size;
}
static inline void
__ethtool_features_set_req_set_nochange_bits_bit(struct ethtool_features_set_req *req,
						 struct ethtool_bitset_bit *bit,
						 unsigned int n_bit)
{
	free(req->nochange.bits.bit);
	req->nochange.bits.bit = bit;
	req->nochange.bits.n_bit = n_bit;
}

struct ethtool_features_set_rsp {
	struct {
		__u32 header:1;
		__u32 hw:1;
		__u32 wanted:1;
		__u32 active:1;
		__u32 nochange:1;
	} _present;

	struct ethtool_header header;
	struct ethtool_bitset hw;
	struct ethtool_bitset wanted;
	struct ethtool_bitset active;
	struct ethtool_bitset nochange;
};

void ethtool_features_set_rsp_free(struct ethtool_features_set_rsp *rsp);

/*
 * Set features.
 */
struct ethtool_features_set_rsp *
ethtool_features_set(struct ynl_sock *ys, struct ethtool_features_set_req *req);

/* ============== ETHTOOL_MSG_PRIVFLAGS_GET ============== */
/* ETHTOOL_MSG_PRIVFLAGS_GET - do */
struct ethtool_privflags_get_req {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_privflags_get_req *
ethtool_privflags_get_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_privflags_get_req));
}
void ethtool_privflags_get_req_free(struct ethtool_privflags_get_req *req);

static inline void
ethtool_privflags_get_req_set_header_dev_index(struct ethtool_privflags_get_req *req,
					       __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_privflags_get_req_set_header_dev_name(struct ethtool_privflags_get_req *req,
					      const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_privflags_get_req_set_header_flags(struct ethtool_privflags_get_req *req,
					   __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_privflags_get_rsp {
	struct {
		__u32 header:1;
		__u32 flags:1;
	} _present;

	struct ethtool_header header;
	struct ethtool_bitset flags;
};

void ethtool_privflags_get_rsp_free(struct ethtool_privflags_get_rsp *rsp);

/*
 * Get device private flags.
 */
struct ethtool_privflags_get_rsp *
ethtool_privflags_get(struct ynl_sock *ys,
		      struct ethtool_privflags_get_req *req);

/* ETHTOOL_MSG_PRIVFLAGS_GET - dump */
struct ethtool_privflags_get_req_dump {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_privflags_get_req_dump *
ethtool_privflags_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_privflags_get_req_dump));
}
void
ethtool_privflags_get_req_dump_free(struct ethtool_privflags_get_req_dump *req);

static inline void
ethtool_privflags_get_req_dump_set_header_dev_index(struct ethtool_privflags_get_req_dump *req,
						    __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_privflags_get_req_dump_set_header_dev_name(struct ethtool_privflags_get_req_dump *req,
						   const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_privflags_get_req_dump_set_header_flags(struct ethtool_privflags_get_req_dump *req,
						__u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_privflags_get_list {
	struct ethtool_privflags_get_list *next;
	struct ethtool_privflags_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_privflags_get_list_free(struct ethtool_privflags_get_list *rsp);

struct ethtool_privflags_get_list *
ethtool_privflags_get_dump(struct ynl_sock *ys,
			   struct ethtool_privflags_get_req_dump *req);

/* ETHTOOL_MSG_PRIVFLAGS_GET - notify */
struct ethtool_privflags_get_ntf {
	__u16 family;
	__u8 cmd;
	struct ynl_ntf_base_type *next;
	void (*free)(struct ethtool_privflags_get_ntf *ntf);
	struct ethtool_privflags_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_privflags_get_ntf_free(struct ethtool_privflags_get_ntf *rsp);

/* ============== ETHTOOL_MSG_PRIVFLAGS_SET ============== */
/* ETHTOOL_MSG_PRIVFLAGS_SET - do */
struct ethtool_privflags_set_req {
	struct {
		__u32 header:1;
		__u32 flags:1;
	} _present;

	struct ethtool_header header;
	struct ethtool_bitset flags;
};

static inline struct ethtool_privflags_set_req *
ethtool_privflags_set_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_privflags_set_req));
}
void ethtool_privflags_set_req_free(struct ethtool_privflags_set_req *req);

static inline void
ethtool_privflags_set_req_set_header_dev_index(struct ethtool_privflags_set_req *req,
					       __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_privflags_set_req_set_header_dev_name(struct ethtool_privflags_set_req *req,
					      const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_privflags_set_req_set_header_flags(struct ethtool_privflags_set_req *req,
					   __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}
static inline void
ethtool_privflags_set_req_set_flags_nomask(struct ethtool_privflags_set_req *req)
{
	req->_present.flags = 1;
	req->flags._present.nomask = 1;
}
static inline void
ethtool_privflags_set_req_set_flags_size(struct ethtool_privflags_set_req *req,
					 __u32 size)
{
	req->_present.flags = 1;
	req->flags._present.size = 1;
	req->flags.size = size;
}
static inline void
__ethtool_privflags_set_req_set_flags_bits_bit(struct ethtool_privflags_set_req *req,
					       struct ethtool_bitset_bit *bit,
					       unsigned int n_bit)
{
	free(req->flags.bits.bit);
	req->flags.bits.bit = bit;
	req->flags.bits.n_bit = n_bit;
}

/*
 * Set device private flags.
 */
int ethtool_privflags_set(struct ynl_sock *ys,
			  struct ethtool_privflags_set_req *req);

/* ============== ETHTOOL_MSG_RINGS_GET ============== */
/* ETHTOOL_MSG_RINGS_GET - do */
struct ethtool_rings_get_req {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_rings_get_req *ethtool_rings_get_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_rings_get_req));
}
void ethtool_rings_get_req_free(struct ethtool_rings_get_req *req);

static inline void
ethtool_rings_get_req_set_header_dev_index(struct ethtool_rings_get_req *req,
					   __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_rings_get_req_set_header_dev_name(struct ethtool_rings_get_req *req,
					  const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_rings_get_req_set_header_flags(struct ethtool_rings_get_req *req,
				       __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_rings_get_rsp {
	struct {
		__u32 header:1;
		__u32 rx_max:1;
		__u32 rx_mini_max:1;
		__u32 rx_jumbo_max:1;
		__u32 tx_max:1;
		__u32 rx:1;
		__u32 rx_mini:1;
		__u32 rx_jumbo:1;
		__u32 tx:1;
		__u32 rx_buf_len:1;
		__u32 tcp_data_split:1;
		__u32 cqe_size:1;
		__u32 tx_push:1;
		__u32 rx_push:1;
		__u32 tx_push_buf_len:1;
		__u32 tx_push_buf_len_max:1;
	} _present;

	struct ethtool_header header;
	__u32 rx_max;
	__u32 rx_mini_max;
	__u32 rx_jumbo_max;
	__u32 tx_max;
	__u32 rx;
	__u32 rx_mini;
	__u32 rx_jumbo;
	__u32 tx;
	__u32 rx_buf_len;
	__u8 tcp_data_split;
	__u32 cqe_size;
	__u8 tx_push;
	__u8 rx_push;
	__u32 tx_push_buf_len;
	__u32 tx_push_buf_len_max;
};

void ethtool_rings_get_rsp_free(struct ethtool_rings_get_rsp *rsp);

/*
 * Get ring params.
 */
struct ethtool_rings_get_rsp *
ethtool_rings_get(struct ynl_sock *ys, struct ethtool_rings_get_req *req);

/* ETHTOOL_MSG_RINGS_GET - dump */
struct ethtool_rings_get_req_dump {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_rings_get_req_dump *
ethtool_rings_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_rings_get_req_dump));
}
void ethtool_rings_get_req_dump_free(struct ethtool_rings_get_req_dump *req);

static inline void
ethtool_rings_get_req_dump_set_header_dev_index(struct ethtool_rings_get_req_dump *req,
						__u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_rings_get_req_dump_set_header_dev_name(struct ethtool_rings_get_req_dump *req,
					       const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_rings_get_req_dump_set_header_flags(struct ethtool_rings_get_req_dump *req,
					    __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_rings_get_list {
	struct ethtool_rings_get_list *next;
	struct ethtool_rings_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_rings_get_list_free(struct ethtool_rings_get_list *rsp);

struct ethtool_rings_get_list *
ethtool_rings_get_dump(struct ynl_sock *ys,
		       struct ethtool_rings_get_req_dump *req);

/* ETHTOOL_MSG_RINGS_GET - notify */
struct ethtool_rings_get_ntf {
	__u16 family;
	__u8 cmd;
	struct ynl_ntf_base_type *next;
	void (*free)(struct ethtool_rings_get_ntf *ntf);
	struct ethtool_rings_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_rings_get_ntf_free(struct ethtool_rings_get_ntf *rsp);

/* ============== ETHTOOL_MSG_RINGS_SET ============== */
/* ETHTOOL_MSG_RINGS_SET - do */
struct ethtool_rings_set_req {
	struct {
		__u32 header:1;
		__u32 rx_max:1;
		__u32 rx_mini_max:1;
		__u32 rx_jumbo_max:1;
		__u32 tx_max:1;
		__u32 rx:1;
		__u32 rx_mini:1;
		__u32 rx_jumbo:1;
		__u32 tx:1;
		__u32 rx_buf_len:1;
		__u32 tcp_data_split:1;
		__u32 cqe_size:1;
		__u32 tx_push:1;
		__u32 rx_push:1;
		__u32 tx_push_buf_len:1;
		__u32 tx_push_buf_len_max:1;
	} _present;

	struct ethtool_header header;
	__u32 rx_max;
	__u32 rx_mini_max;
	__u32 rx_jumbo_max;
	__u32 tx_max;
	__u32 rx;
	__u32 rx_mini;
	__u32 rx_jumbo;
	__u32 tx;
	__u32 rx_buf_len;
	__u8 tcp_data_split;
	__u32 cqe_size;
	__u8 tx_push;
	__u8 rx_push;
	__u32 tx_push_buf_len;
	__u32 tx_push_buf_len_max;
};

static inline struct ethtool_rings_set_req *ethtool_rings_set_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_rings_set_req));
}
void ethtool_rings_set_req_free(struct ethtool_rings_set_req *req);

static inline void
ethtool_rings_set_req_set_header_dev_index(struct ethtool_rings_set_req *req,
					   __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_rings_set_req_set_header_dev_name(struct ethtool_rings_set_req *req,
					  const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_rings_set_req_set_header_flags(struct ethtool_rings_set_req *req,
				       __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}
static inline void
ethtool_rings_set_req_set_rx_max(struct ethtool_rings_set_req *req,
				 __u32 rx_max)
{
	req->_present.rx_max = 1;
	req->rx_max = rx_max;
}
static inline void
ethtool_rings_set_req_set_rx_mini_max(struct ethtool_rings_set_req *req,
				      __u32 rx_mini_max)
{
	req->_present.rx_mini_max = 1;
	req->rx_mini_max = rx_mini_max;
}
static inline void
ethtool_rings_set_req_set_rx_jumbo_max(struct ethtool_rings_set_req *req,
				       __u32 rx_jumbo_max)
{
	req->_present.rx_jumbo_max = 1;
	req->rx_jumbo_max = rx_jumbo_max;
}
static inline void
ethtool_rings_set_req_set_tx_max(struct ethtool_rings_set_req *req,
				 __u32 tx_max)
{
	req->_present.tx_max = 1;
	req->tx_max = tx_max;
}
static inline void
ethtool_rings_set_req_set_rx(struct ethtool_rings_set_req *req, __u32 rx)
{
	req->_present.rx = 1;
	req->rx = rx;
}
static inline void
ethtool_rings_set_req_set_rx_mini(struct ethtool_rings_set_req *req,
				  __u32 rx_mini)
{
	req->_present.rx_mini = 1;
	req->rx_mini = rx_mini;
}
static inline void
ethtool_rings_set_req_set_rx_jumbo(struct ethtool_rings_set_req *req,
				   __u32 rx_jumbo)
{
	req->_present.rx_jumbo = 1;
	req->rx_jumbo = rx_jumbo;
}
static inline void
ethtool_rings_set_req_set_tx(struct ethtool_rings_set_req *req, __u32 tx)
{
	req->_present.tx = 1;
	req->tx = tx;
}
static inline void
ethtool_rings_set_req_set_rx_buf_len(struct ethtool_rings_set_req *req,
				     __u32 rx_buf_len)
{
	req->_present.rx_buf_len = 1;
	req->rx_buf_len = rx_buf_len;
}
static inline void
ethtool_rings_set_req_set_tcp_data_split(struct ethtool_rings_set_req *req,
					 __u8 tcp_data_split)
{
	req->_present.tcp_data_split = 1;
	req->tcp_data_split = tcp_data_split;
}
static inline void
ethtool_rings_set_req_set_cqe_size(struct ethtool_rings_set_req *req,
				   __u32 cqe_size)
{
	req->_present.cqe_size = 1;
	req->cqe_size = cqe_size;
}
static inline void
ethtool_rings_set_req_set_tx_push(struct ethtool_rings_set_req *req,
				  __u8 tx_push)
{
	req->_present.tx_push = 1;
	req->tx_push = tx_push;
}
static inline void
ethtool_rings_set_req_set_rx_push(struct ethtool_rings_set_req *req,
				  __u8 rx_push)
{
	req->_present.rx_push = 1;
	req->rx_push = rx_push;
}
static inline void
ethtool_rings_set_req_set_tx_push_buf_len(struct ethtool_rings_set_req *req,
					  __u32 tx_push_buf_len)
{
	req->_present.tx_push_buf_len = 1;
	req->tx_push_buf_len = tx_push_buf_len;
}
static inline void
ethtool_rings_set_req_set_tx_push_buf_len_max(struct ethtool_rings_set_req *req,
					      __u32 tx_push_buf_len_max)
{
	req->_present.tx_push_buf_len_max = 1;
	req->tx_push_buf_len_max = tx_push_buf_len_max;
}

/*
 * Set ring params.
 */
int ethtool_rings_set(struct ynl_sock *ys, struct ethtool_rings_set_req *req);

/* ============== ETHTOOL_MSG_CHANNELS_GET ============== */
/* ETHTOOL_MSG_CHANNELS_GET - do */
struct ethtool_channels_get_req {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_channels_get_req *
ethtool_channels_get_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_channels_get_req));
}
void ethtool_channels_get_req_free(struct ethtool_channels_get_req *req);

static inline void
ethtool_channels_get_req_set_header_dev_index(struct ethtool_channels_get_req *req,
					      __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_channels_get_req_set_header_dev_name(struct ethtool_channels_get_req *req,
					     const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_channels_get_req_set_header_flags(struct ethtool_channels_get_req *req,
					  __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_channels_get_rsp {
	struct {
		__u32 header:1;
		__u32 rx_max:1;
		__u32 tx_max:1;
		__u32 other_max:1;
		__u32 combined_max:1;
		__u32 rx_count:1;
		__u32 tx_count:1;
		__u32 other_count:1;
		__u32 combined_count:1;
	} _present;

	struct ethtool_header header;
	__u32 rx_max;
	__u32 tx_max;
	__u32 other_max;
	__u32 combined_max;
	__u32 rx_count;
	__u32 tx_count;
	__u32 other_count;
	__u32 combined_count;
};

void ethtool_channels_get_rsp_free(struct ethtool_channels_get_rsp *rsp);

/*
 * Get channel params.
 */
struct ethtool_channels_get_rsp *
ethtool_channels_get(struct ynl_sock *ys, struct ethtool_channels_get_req *req);

/* ETHTOOL_MSG_CHANNELS_GET - dump */
struct ethtool_channels_get_req_dump {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_channels_get_req_dump *
ethtool_channels_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_channels_get_req_dump));
}
void
ethtool_channels_get_req_dump_free(struct ethtool_channels_get_req_dump *req);

static inline void
ethtool_channels_get_req_dump_set_header_dev_index(struct ethtool_channels_get_req_dump *req,
						   __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_channels_get_req_dump_set_header_dev_name(struct ethtool_channels_get_req_dump *req,
						  const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_channels_get_req_dump_set_header_flags(struct ethtool_channels_get_req_dump *req,
					       __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_channels_get_list {
	struct ethtool_channels_get_list *next;
	struct ethtool_channels_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_channels_get_list_free(struct ethtool_channels_get_list *rsp);

struct ethtool_channels_get_list *
ethtool_channels_get_dump(struct ynl_sock *ys,
			  struct ethtool_channels_get_req_dump *req);

/* ETHTOOL_MSG_CHANNELS_GET - notify */
struct ethtool_channels_get_ntf {
	__u16 family;
	__u8 cmd;
	struct ynl_ntf_base_type *next;
	void (*free)(struct ethtool_channels_get_ntf *ntf);
	struct ethtool_channels_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_channels_get_ntf_free(struct ethtool_channels_get_ntf *rsp);

/* ============== ETHTOOL_MSG_CHANNELS_SET ============== */
/* ETHTOOL_MSG_CHANNELS_SET - do */
struct ethtool_channels_set_req {
	struct {
		__u32 header:1;
		__u32 rx_max:1;
		__u32 tx_max:1;
		__u32 other_max:1;
		__u32 combined_max:1;
		__u32 rx_count:1;
		__u32 tx_count:1;
		__u32 other_count:1;
		__u32 combined_count:1;
	} _present;

	struct ethtool_header header;
	__u32 rx_max;
	__u32 tx_max;
	__u32 other_max;
	__u32 combined_max;
	__u32 rx_count;
	__u32 tx_count;
	__u32 other_count;
	__u32 combined_count;
};

static inline struct ethtool_channels_set_req *
ethtool_channels_set_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_channels_set_req));
}
void ethtool_channels_set_req_free(struct ethtool_channels_set_req *req);

static inline void
ethtool_channels_set_req_set_header_dev_index(struct ethtool_channels_set_req *req,
					      __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_channels_set_req_set_header_dev_name(struct ethtool_channels_set_req *req,
					     const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_channels_set_req_set_header_flags(struct ethtool_channels_set_req *req,
					  __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}
static inline void
ethtool_channels_set_req_set_rx_max(struct ethtool_channels_set_req *req,
				    __u32 rx_max)
{
	req->_present.rx_max = 1;
	req->rx_max = rx_max;
}
static inline void
ethtool_channels_set_req_set_tx_max(struct ethtool_channels_set_req *req,
				    __u32 tx_max)
{
	req->_present.tx_max = 1;
	req->tx_max = tx_max;
}
static inline void
ethtool_channels_set_req_set_other_max(struct ethtool_channels_set_req *req,
				       __u32 other_max)
{
	req->_present.other_max = 1;
	req->other_max = other_max;
}
static inline void
ethtool_channels_set_req_set_combined_max(struct ethtool_channels_set_req *req,
					  __u32 combined_max)
{
	req->_present.combined_max = 1;
	req->combined_max = combined_max;
}
static inline void
ethtool_channels_set_req_set_rx_count(struct ethtool_channels_set_req *req,
				      __u32 rx_count)
{
	req->_present.rx_count = 1;
	req->rx_count = rx_count;
}
static inline void
ethtool_channels_set_req_set_tx_count(struct ethtool_channels_set_req *req,
				      __u32 tx_count)
{
	req->_present.tx_count = 1;
	req->tx_count = tx_count;
}
static inline void
ethtool_channels_set_req_set_other_count(struct ethtool_channels_set_req *req,
					 __u32 other_count)
{
	req->_present.other_count = 1;
	req->other_count = other_count;
}
static inline void
ethtool_channels_set_req_set_combined_count(struct ethtool_channels_set_req *req,
					    __u32 combined_count)
{
	req->_present.combined_count = 1;
	req->combined_count = combined_count;
}

/*
 * Set channel params.
 */
int ethtool_channels_set(struct ynl_sock *ys,
			 struct ethtool_channels_set_req *req);

/* ============== ETHTOOL_MSG_COALESCE_GET ============== */
/* ETHTOOL_MSG_COALESCE_GET - do */
struct ethtool_coalesce_get_req {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_coalesce_get_req *
ethtool_coalesce_get_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_coalesce_get_req));
}
void ethtool_coalesce_get_req_free(struct ethtool_coalesce_get_req *req);

static inline void
ethtool_coalesce_get_req_set_header_dev_index(struct ethtool_coalesce_get_req *req,
					      __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_coalesce_get_req_set_header_dev_name(struct ethtool_coalesce_get_req *req,
					     const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_coalesce_get_req_set_header_flags(struct ethtool_coalesce_get_req *req,
					  __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_coalesce_get_rsp {
	struct {
		__u32 header:1;
		__u32 rx_usecs:1;
		__u32 rx_max_frames:1;
		__u32 rx_usecs_irq:1;
		__u32 rx_max_frames_irq:1;
		__u32 tx_usecs:1;
		__u32 tx_max_frames:1;
		__u32 tx_usecs_irq:1;
		__u32 tx_max_frames_irq:1;
		__u32 stats_block_usecs:1;
		__u32 use_adaptive_rx:1;
		__u32 use_adaptive_tx:1;
		__u32 pkt_rate_low:1;
		__u32 rx_usecs_low:1;
		__u32 rx_max_frames_low:1;
		__u32 tx_usecs_low:1;
		__u32 tx_max_frames_low:1;
		__u32 pkt_rate_high:1;
		__u32 rx_usecs_high:1;
		__u32 rx_max_frames_high:1;
		__u32 tx_usecs_high:1;
		__u32 tx_max_frames_high:1;
		__u32 rate_sample_interval:1;
		__u32 use_cqe_mode_tx:1;
		__u32 use_cqe_mode_rx:1;
		__u32 tx_aggr_max_bytes:1;
		__u32 tx_aggr_max_frames:1;
		__u32 tx_aggr_time_usecs:1;
	} _present;

	struct ethtool_header header;
	__u32 rx_usecs;
	__u32 rx_max_frames;
	__u32 rx_usecs_irq;
	__u32 rx_max_frames_irq;
	__u32 tx_usecs;
	__u32 tx_max_frames;
	__u32 tx_usecs_irq;
	__u32 tx_max_frames_irq;
	__u32 stats_block_usecs;
	__u8 use_adaptive_rx;
	__u8 use_adaptive_tx;
	__u32 pkt_rate_low;
	__u32 rx_usecs_low;
	__u32 rx_max_frames_low;
	__u32 tx_usecs_low;
	__u32 tx_max_frames_low;
	__u32 pkt_rate_high;
	__u32 rx_usecs_high;
	__u32 rx_max_frames_high;
	__u32 tx_usecs_high;
	__u32 tx_max_frames_high;
	__u32 rate_sample_interval;
	__u8 use_cqe_mode_tx;
	__u8 use_cqe_mode_rx;
	__u32 tx_aggr_max_bytes;
	__u32 tx_aggr_max_frames;
	__u32 tx_aggr_time_usecs;
};

void ethtool_coalesce_get_rsp_free(struct ethtool_coalesce_get_rsp *rsp);

/*
 * Get coalesce params.
 */
struct ethtool_coalesce_get_rsp *
ethtool_coalesce_get(struct ynl_sock *ys, struct ethtool_coalesce_get_req *req);

/* ETHTOOL_MSG_COALESCE_GET - dump */
struct ethtool_coalesce_get_req_dump {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_coalesce_get_req_dump *
ethtool_coalesce_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_coalesce_get_req_dump));
}
void
ethtool_coalesce_get_req_dump_free(struct ethtool_coalesce_get_req_dump *req);

static inline void
ethtool_coalesce_get_req_dump_set_header_dev_index(struct ethtool_coalesce_get_req_dump *req,
						   __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_coalesce_get_req_dump_set_header_dev_name(struct ethtool_coalesce_get_req_dump *req,
						  const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_coalesce_get_req_dump_set_header_flags(struct ethtool_coalesce_get_req_dump *req,
					       __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_coalesce_get_list {
	struct ethtool_coalesce_get_list *next;
	struct ethtool_coalesce_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_coalesce_get_list_free(struct ethtool_coalesce_get_list *rsp);

struct ethtool_coalesce_get_list *
ethtool_coalesce_get_dump(struct ynl_sock *ys,
			  struct ethtool_coalesce_get_req_dump *req);

/* ETHTOOL_MSG_COALESCE_GET - notify */
struct ethtool_coalesce_get_ntf {
	__u16 family;
	__u8 cmd;
	struct ynl_ntf_base_type *next;
	void (*free)(struct ethtool_coalesce_get_ntf *ntf);
	struct ethtool_coalesce_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_coalesce_get_ntf_free(struct ethtool_coalesce_get_ntf *rsp);

/* ============== ETHTOOL_MSG_COALESCE_SET ============== */
/* ETHTOOL_MSG_COALESCE_SET - do */
struct ethtool_coalesce_set_req {
	struct {
		__u32 header:1;
		__u32 rx_usecs:1;
		__u32 rx_max_frames:1;
		__u32 rx_usecs_irq:1;
		__u32 rx_max_frames_irq:1;
		__u32 tx_usecs:1;
		__u32 tx_max_frames:1;
		__u32 tx_usecs_irq:1;
		__u32 tx_max_frames_irq:1;
		__u32 stats_block_usecs:1;
		__u32 use_adaptive_rx:1;
		__u32 use_adaptive_tx:1;
		__u32 pkt_rate_low:1;
		__u32 rx_usecs_low:1;
		__u32 rx_max_frames_low:1;
		__u32 tx_usecs_low:1;
		__u32 tx_max_frames_low:1;
		__u32 pkt_rate_high:1;
		__u32 rx_usecs_high:1;
		__u32 rx_max_frames_high:1;
		__u32 tx_usecs_high:1;
		__u32 tx_max_frames_high:1;
		__u32 rate_sample_interval:1;
		__u32 use_cqe_mode_tx:1;
		__u32 use_cqe_mode_rx:1;
		__u32 tx_aggr_max_bytes:1;
		__u32 tx_aggr_max_frames:1;
		__u32 tx_aggr_time_usecs:1;
	} _present;

	struct ethtool_header header;
	__u32 rx_usecs;
	__u32 rx_max_frames;
	__u32 rx_usecs_irq;
	__u32 rx_max_frames_irq;
	__u32 tx_usecs;
	__u32 tx_max_frames;
	__u32 tx_usecs_irq;
	__u32 tx_max_frames_irq;
	__u32 stats_block_usecs;
	__u8 use_adaptive_rx;
	__u8 use_adaptive_tx;
	__u32 pkt_rate_low;
	__u32 rx_usecs_low;
	__u32 rx_max_frames_low;
	__u32 tx_usecs_low;
	__u32 tx_max_frames_low;
	__u32 pkt_rate_high;
	__u32 rx_usecs_high;
	__u32 rx_max_frames_high;
	__u32 tx_usecs_high;
	__u32 tx_max_frames_high;
	__u32 rate_sample_interval;
	__u8 use_cqe_mode_tx;
	__u8 use_cqe_mode_rx;
	__u32 tx_aggr_max_bytes;
	__u32 tx_aggr_max_frames;
	__u32 tx_aggr_time_usecs;
};

static inline struct ethtool_coalesce_set_req *
ethtool_coalesce_set_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_coalesce_set_req));
}
void ethtool_coalesce_set_req_free(struct ethtool_coalesce_set_req *req);

static inline void
ethtool_coalesce_set_req_set_header_dev_index(struct ethtool_coalesce_set_req *req,
					      __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_coalesce_set_req_set_header_dev_name(struct ethtool_coalesce_set_req *req,
					     const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_coalesce_set_req_set_header_flags(struct ethtool_coalesce_set_req *req,
					  __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}
static inline void
ethtool_coalesce_set_req_set_rx_usecs(struct ethtool_coalesce_set_req *req,
				      __u32 rx_usecs)
{
	req->_present.rx_usecs = 1;
	req->rx_usecs = rx_usecs;
}
static inline void
ethtool_coalesce_set_req_set_rx_max_frames(struct ethtool_coalesce_set_req *req,
					   __u32 rx_max_frames)
{
	req->_present.rx_max_frames = 1;
	req->rx_max_frames = rx_max_frames;
}
static inline void
ethtool_coalesce_set_req_set_rx_usecs_irq(struct ethtool_coalesce_set_req *req,
					  __u32 rx_usecs_irq)
{
	req->_present.rx_usecs_irq = 1;
	req->rx_usecs_irq = rx_usecs_irq;
}
static inline void
ethtool_coalesce_set_req_set_rx_max_frames_irq(struct ethtool_coalesce_set_req *req,
					       __u32 rx_max_frames_irq)
{
	req->_present.rx_max_frames_irq = 1;
	req->rx_max_frames_irq = rx_max_frames_irq;
}
static inline void
ethtool_coalesce_set_req_set_tx_usecs(struct ethtool_coalesce_set_req *req,
				      __u32 tx_usecs)
{
	req->_present.tx_usecs = 1;
	req->tx_usecs = tx_usecs;
}
static inline void
ethtool_coalesce_set_req_set_tx_max_frames(struct ethtool_coalesce_set_req *req,
					   __u32 tx_max_frames)
{
	req->_present.tx_max_frames = 1;
	req->tx_max_frames = tx_max_frames;
}
static inline void
ethtool_coalesce_set_req_set_tx_usecs_irq(struct ethtool_coalesce_set_req *req,
					  __u32 tx_usecs_irq)
{
	req->_present.tx_usecs_irq = 1;
	req->tx_usecs_irq = tx_usecs_irq;
}
static inline void
ethtool_coalesce_set_req_set_tx_max_frames_irq(struct ethtool_coalesce_set_req *req,
					       __u32 tx_max_frames_irq)
{
	req->_present.tx_max_frames_irq = 1;
	req->tx_max_frames_irq = tx_max_frames_irq;
}
static inline void
ethtool_coalesce_set_req_set_stats_block_usecs(struct ethtool_coalesce_set_req *req,
					       __u32 stats_block_usecs)
{
	req->_present.stats_block_usecs = 1;
	req->stats_block_usecs = stats_block_usecs;
}
static inline void
ethtool_coalesce_set_req_set_use_adaptive_rx(struct ethtool_coalesce_set_req *req,
					     __u8 use_adaptive_rx)
{
	req->_present.use_adaptive_rx = 1;
	req->use_adaptive_rx = use_adaptive_rx;
}
static inline void
ethtool_coalesce_set_req_set_use_adaptive_tx(struct ethtool_coalesce_set_req *req,
					     __u8 use_adaptive_tx)
{
	req->_present.use_adaptive_tx = 1;
	req->use_adaptive_tx = use_adaptive_tx;
}
static inline void
ethtool_coalesce_set_req_set_pkt_rate_low(struct ethtool_coalesce_set_req *req,
					  __u32 pkt_rate_low)
{
	req->_present.pkt_rate_low = 1;
	req->pkt_rate_low = pkt_rate_low;
}
static inline void
ethtool_coalesce_set_req_set_rx_usecs_low(struct ethtool_coalesce_set_req *req,
					  __u32 rx_usecs_low)
{
	req->_present.rx_usecs_low = 1;
	req->rx_usecs_low = rx_usecs_low;
}
static inline void
ethtool_coalesce_set_req_set_rx_max_frames_low(struct ethtool_coalesce_set_req *req,
					       __u32 rx_max_frames_low)
{
	req->_present.rx_max_frames_low = 1;
	req->rx_max_frames_low = rx_max_frames_low;
}
static inline void
ethtool_coalesce_set_req_set_tx_usecs_low(struct ethtool_coalesce_set_req *req,
					  __u32 tx_usecs_low)
{
	req->_present.tx_usecs_low = 1;
	req->tx_usecs_low = tx_usecs_low;
}
static inline void
ethtool_coalesce_set_req_set_tx_max_frames_low(struct ethtool_coalesce_set_req *req,
					       __u32 tx_max_frames_low)
{
	req->_present.tx_max_frames_low = 1;
	req->tx_max_frames_low = tx_max_frames_low;
}
static inline void
ethtool_coalesce_set_req_set_pkt_rate_high(struct ethtool_coalesce_set_req *req,
					   __u32 pkt_rate_high)
{
	req->_present.pkt_rate_high = 1;
	req->pkt_rate_high = pkt_rate_high;
}
static inline void
ethtool_coalesce_set_req_set_rx_usecs_high(struct ethtool_coalesce_set_req *req,
					   __u32 rx_usecs_high)
{
	req->_present.rx_usecs_high = 1;
	req->rx_usecs_high = rx_usecs_high;
}
static inline void
ethtool_coalesce_set_req_set_rx_max_frames_high(struct ethtool_coalesce_set_req *req,
						__u32 rx_max_frames_high)
{
	req->_present.rx_max_frames_high = 1;
	req->rx_max_frames_high = rx_max_frames_high;
}
static inline void
ethtool_coalesce_set_req_set_tx_usecs_high(struct ethtool_coalesce_set_req *req,
					   __u32 tx_usecs_high)
{
	req->_present.tx_usecs_high = 1;
	req->tx_usecs_high = tx_usecs_high;
}
static inline void
ethtool_coalesce_set_req_set_tx_max_frames_high(struct ethtool_coalesce_set_req *req,
						__u32 tx_max_frames_high)
{
	req->_present.tx_max_frames_high = 1;
	req->tx_max_frames_high = tx_max_frames_high;
}
static inline void
ethtool_coalesce_set_req_set_rate_sample_interval(struct ethtool_coalesce_set_req *req,
						  __u32 rate_sample_interval)
{
	req->_present.rate_sample_interval = 1;
	req->rate_sample_interval = rate_sample_interval;
}
static inline void
ethtool_coalesce_set_req_set_use_cqe_mode_tx(struct ethtool_coalesce_set_req *req,
					     __u8 use_cqe_mode_tx)
{
	req->_present.use_cqe_mode_tx = 1;
	req->use_cqe_mode_tx = use_cqe_mode_tx;
}
static inline void
ethtool_coalesce_set_req_set_use_cqe_mode_rx(struct ethtool_coalesce_set_req *req,
					     __u8 use_cqe_mode_rx)
{
	req->_present.use_cqe_mode_rx = 1;
	req->use_cqe_mode_rx = use_cqe_mode_rx;
}
static inline void
ethtool_coalesce_set_req_set_tx_aggr_max_bytes(struct ethtool_coalesce_set_req *req,
					       __u32 tx_aggr_max_bytes)
{
	req->_present.tx_aggr_max_bytes = 1;
	req->tx_aggr_max_bytes = tx_aggr_max_bytes;
}
static inline void
ethtool_coalesce_set_req_set_tx_aggr_max_frames(struct ethtool_coalesce_set_req *req,
						__u32 tx_aggr_max_frames)
{
	req->_present.tx_aggr_max_frames = 1;
	req->tx_aggr_max_frames = tx_aggr_max_frames;
}
static inline void
ethtool_coalesce_set_req_set_tx_aggr_time_usecs(struct ethtool_coalesce_set_req *req,
						__u32 tx_aggr_time_usecs)
{
	req->_present.tx_aggr_time_usecs = 1;
	req->tx_aggr_time_usecs = tx_aggr_time_usecs;
}

/*
 * Set coalesce params.
 */
int ethtool_coalesce_set(struct ynl_sock *ys,
			 struct ethtool_coalesce_set_req *req);

/* ============== ETHTOOL_MSG_PAUSE_GET ============== */
/* ETHTOOL_MSG_PAUSE_GET - do */
struct ethtool_pause_get_req {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_pause_get_req *ethtool_pause_get_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_pause_get_req));
}
void ethtool_pause_get_req_free(struct ethtool_pause_get_req *req);

static inline void
ethtool_pause_get_req_set_header_dev_index(struct ethtool_pause_get_req *req,
					   __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_pause_get_req_set_header_dev_name(struct ethtool_pause_get_req *req,
					  const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_pause_get_req_set_header_flags(struct ethtool_pause_get_req *req,
				       __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_pause_get_rsp {
	struct {
		__u32 header:1;
		__u32 autoneg:1;
		__u32 rx:1;
		__u32 tx:1;
		__u32 stats:1;
		__u32 stats_src:1;
	} _present;

	struct ethtool_header header;
	__u8 autoneg;
	__u8 rx;
	__u8 tx;
	struct ethtool_pause_stat stats;
	__u32 stats_src;
};

void ethtool_pause_get_rsp_free(struct ethtool_pause_get_rsp *rsp);

/*
 * Get pause params.
 */
struct ethtool_pause_get_rsp *
ethtool_pause_get(struct ynl_sock *ys, struct ethtool_pause_get_req *req);

/* ETHTOOL_MSG_PAUSE_GET - dump */
struct ethtool_pause_get_req_dump {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_pause_get_req_dump *
ethtool_pause_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_pause_get_req_dump));
}
void ethtool_pause_get_req_dump_free(struct ethtool_pause_get_req_dump *req);

static inline void
ethtool_pause_get_req_dump_set_header_dev_index(struct ethtool_pause_get_req_dump *req,
						__u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_pause_get_req_dump_set_header_dev_name(struct ethtool_pause_get_req_dump *req,
					       const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_pause_get_req_dump_set_header_flags(struct ethtool_pause_get_req_dump *req,
					    __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_pause_get_list {
	struct ethtool_pause_get_list *next;
	struct ethtool_pause_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_pause_get_list_free(struct ethtool_pause_get_list *rsp);

struct ethtool_pause_get_list *
ethtool_pause_get_dump(struct ynl_sock *ys,
		       struct ethtool_pause_get_req_dump *req);

/* ETHTOOL_MSG_PAUSE_GET - notify */
struct ethtool_pause_get_ntf {
	__u16 family;
	__u8 cmd;
	struct ynl_ntf_base_type *next;
	void (*free)(struct ethtool_pause_get_ntf *ntf);
	struct ethtool_pause_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_pause_get_ntf_free(struct ethtool_pause_get_ntf *rsp);

/* ============== ETHTOOL_MSG_PAUSE_SET ============== */
/* ETHTOOL_MSG_PAUSE_SET - do */
struct ethtool_pause_set_req {
	struct {
		__u32 header:1;
		__u32 autoneg:1;
		__u32 rx:1;
		__u32 tx:1;
		__u32 stats:1;
		__u32 stats_src:1;
	} _present;

	struct ethtool_header header;
	__u8 autoneg;
	__u8 rx;
	__u8 tx;
	struct ethtool_pause_stat stats;
	__u32 stats_src;
};

static inline struct ethtool_pause_set_req *ethtool_pause_set_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_pause_set_req));
}
void ethtool_pause_set_req_free(struct ethtool_pause_set_req *req);

static inline void
ethtool_pause_set_req_set_header_dev_index(struct ethtool_pause_set_req *req,
					   __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_pause_set_req_set_header_dev_name(struct ethtool_pause_set_req *req,
					  const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_pause_set_req_set_header_flags(struct ethtool_pause_set_req *req,
				       __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}
static inline void
ethtool_pause_set_req_set_autoneg(struct ethtool_pause_set_req *req,
				  __u8 autoneg)
{
	req->_present.autoneg = 1;
	req->autoneg = autoneg;
}
static inline void
ethtool_pause_set_req_set_rx(struct ethtool_pause_set_req *req, __u8 rx)
{
	req->_present.rx = 1;
	req->rx = rx;
}
static inline void
ethtool_pause_set_req_set_tx(struct ethtool_pause_set_req *req, __u8 tx)
{
	req->_present.tx = 1;
	req->tx = tx;
}
static inline void
ethtool_pause_set_req_set_stats_tx_frames(struct ethtool_pause_set_req *req,
					  __u64 tx_frames)
{
	req->_present.stats = 1;
	req->stats._present.tx_frames = 1;
	req->stats.tx_frames = tx_frames;
}
static inline void
ethtool_pause_set_req_set_stats_rx_frames(struct ethtool_pause_set_req *req,
					  __u64 rx_frames)
{
	req->_present.stats = 1;
	req->stats._present.rx_frames = 1;
	req->stats.rx_frames = rx_frames;
}
static inline void
ethtool_pause_set_req_set_stats_src(struct ethtool_pause_set_req *req,
				    __u32 stats_src)
{
	req->_present.stats_src = 1;
	req->stats_src = stats_src;
}

/*
 * Set pause params.
 */
int ethtool_pause_set(struct ynl_sock *ys, struct ethtool_pause_set_req *req);

/* ============== ETHTOOL_MSG_EEE_GET ============== */
/* ETHTOOL_MSG_EEE_GET - do */
struct ethtool_eee_get_req {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_eee_get_req *ethtool_eee_get_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_eee_get_req));
}
void ethtool_eee_get_req_free(struct ethtool_eee_get_req *req);

static inline void
ethtool_eee_get_req_set_header_dev_index(struct ethtool_eee_get_req *req,
					 __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_eee_get_req_set_header_dev_name(struct ethtool_eee_get_req *req,
					const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_eee_get_req_set_header_flags(struct ethtool_eee_get_req *req,
				     __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_eee_get_rsp {
	struct {
		__u32 header:1;
		__u32 modes_ours:1;
		__u32 modes_peer:1;
		__u32 active:1;
		__u32 enabled:1;
		__u32 tx_lpi_enabled:1;
		__u32 tx_lpi_timer:1;
	} _present;

	struct ethtool_header header;
	struct ethtool_bitset modes_ours;
	struct ethtool_bitset modes_peer;
	__u8 active;
	__u8 enabled;
	__u8 tx_lpi_enabled;
	__u32 tx_lpi_timer;
};

void ethtool_eee_get_rsp_free(struct ethtool_eee_get_rsp *rsp);

/*
 * Get eee params.
 */
struct ethtool_eee_get_rsp *
ethtool_eee_get(struct ynl_sock *ys, struct ethtool_eee_get_req *req);

/* ETHTOOL_MSG_EEE_GET - dump */
struct ethtool_eee_get_req_dump {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_eee_get_req_dump *
ethtool_eee_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_eee_get_req_dump));
}
void ethtool_eee_get_req_dump_free(struct ethtool_eee_get_req_dump *req);

static inline void
ethtool_eee_get_req_dump_set_header_dev_index(struct ethtool_eee_get_req_dump *req,
					      __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_eee_get_req_dump_set_header_dev_name(struct ethtool_eee_get_req_dump *req,
					     const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_eee_get_req_dump_set_header_flags(struct ethtool_eee_get_req_dump *req,
					  __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_eee_get_list {
	struct ethtool_eee_get_list *next;
	struct ethtool_eee_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_eee_get_list_free(struct ethtool_eee_get_list *rsp);

struct ethtool_eee_get_list *
ethtool_eee_get_dump(struct ynl_sock *ys, struct ethtool_eee_get_req_dump *req);

/* ETHTOOL_MSG_EEE_GET - notify */
struct ethtool_eee_get_ntf {
	__u16 family;
	__u8 cmd;
	struct ynl_ntf_base_type *next;
	void (*free)(struct ethtool_eee_get_ntf *ntf);
	struct ethtool_eee_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_eee_get_ntf_free(struct ethtool_eee_get_ntf *rsp);

/* ============== ETHTOOL_MSG_EEE_SET ============== */
/* ETHTOOL_MSG_EEE_SET - do */
struct ethtool_eee_set_req {
	struct {
		__u32 header:1;
		__u32 modes_ours:1;
		__u32 modes_peer:1;
		__u32 active:1;
		__u32 enabled:1;
		__u32 tx_lpi_enabled:1;
		__u32 tx_lpi_timer:1;
	} _present;

	struct ethtool_header header;
	struct ethtool_bitset modes_ours;
	struct ethtool_bitset modes_peer;
	__u8 active;
	__u8 enabled;
	__u8 tx_lpi_enabled;
	__u32 tx_lpi_timer;
};

static inline struct ethtool_eee_set_req *ethtool_eee_set_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_eee_set_req));
}
void ethtool_eee_set_req_free(struct ethtool_eee_set_req *req);

static inline void
ethtool_eee_set_req_set_header_dev_index(struct ethtool_eee_set_req *req,
					 __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_eee_set_req_set_header_dev_name(struct ethtool_eee_set_req *req,
					const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_eee_set_req_set_header_flags(struct ethtool_eee_set_req *req,
				     __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}
static inline void
ethtool_eee_set_req_set_modes_ours_nomask(struct ethtool_eee_set_req *req)
{
	req->_present.modes_ours = 1;
	req->modes_ours._present.nomask = 1;
}
static inline void
ethtool_eee_set_req_set_modes_ours_size(struct ethtool_eee_set_req *req,
					__u32 size)
{
	req->_present.modes_ours = 1;
	req->modes_ours._present.size = 1;
	req->modes_ours.size = size;
}
static inline void
__ethtool_eee_set_req_set_modes_ours_bits_bit(struct ethtool_eee_set_req *req,
					      struct ethtool_bitset_bit *bit,
					      unsigned int n_bit)
{
	free(req->modes_ours.bits.bit);
	req->modes_ours.bits.bit = bit;
	req->modes_ours.bits.n_bit = n_bit;
}
static inline void
ethtool_eee_set_req_set_modes_peer_nomask(struct ethtool_eee_set_req *req)
{
	req->_present.modes_peer = 1;
	req->modes_peer._present.nomask = 1;
}
static inline void
ethtool_eee_set_req_set_modes_peer_size(struct ethtool_eee_set_req *req,
					__u32 size)
{
	req->_present.modes_peer = 1;
	req->modes_peer._present.size = 1;
	req->modes_peer.size = size;
}
static inline void
__ethtool_eee_set_req_set_modes_peer_bits_bit(struct ethtool_eee_set_req *req,
					      struct ethtool_bitset_bit *bit,
					      unsigned int n_bit)
{
	free(req->modes_peer.bits.bit);
	req->modes_peer.bits.bit = bit;
	req->modes_peer.bits.n_bit = n_bit;
}
static inline void
ethtool_eee_set_req_set_active(struct ethtool_eee_set_req *req, __u8 active)
{
	req->_present.active = 1;
	req->active = active;
}
static inline void
ethtool_eee_set_req_set_enabled(struct ethtool_eee_set_req *req, __u8 enabled)
{
	req->_present.enabled = 1;
	req->enabled = enabled;
}
static inline void
ethtool_eee_set_req_set_tx_lpi_enabled(struct ethtool_eee_set_req *req,
				       __u8 tx_lpi_enabled)
{
	req->_present.tx_lpi_enabled = 1;
	req->tx_lpi_enabled = tx_lpi_enabled;
}
static inline void
ethtool_eee_set_req_set_tx_lpi_timer(struct ethtool_eee_set_req *req,
				     __u32 tx_lpi_timer)
{
	req->_present.tx_lpi_timer = 1;
	req->tx_lpi_timer = tx_lpi_timer;
}

/*
 * Set eee params.
 */
int ethtool_eee_set(struct ynl_sock *ys, struct ethtool_eee_set_req *req);

/* ============== ETHTOOL_MSG_TSINFO_GET ============== */
/* ETHTOOL_MSG_TSINFO_GET - do */
struct ethtool_tsinfo_get_req {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_tsinfo_get_req *ethtool_tsinfo_get_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_tsinfo_get_req));
}
void ethtool_tsinfo_get_req_free(struct ethtool_tsinfo_get_req *req);

static inline void
ethtool_tsinfo_get_req_set_header_dev_index(struct ethtool_tsinfo_get_req *req,
					    __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_tsinfo_get_req_set_header_dev_name(struct ethtool_tsinfo_get_req *req,
					   const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_tsinfo_get_req_set_header_flags(struct ethtool_tsinfo_get_req *req,
					__u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_tsinfo_get_rsp {
	struct {
		__u32 header:1;
		__u32 timestamping:1;
		__u32 tx_types:1;
		__u32 rx_filters:1;
		__u32 phc_index:1;
	} _present;

	struct ethtool_header header;
	struct ethtool_bitset timestamping;
	struct ethtool_bitset tx_types;
	struct ethtool_bitset rx_filters;
	__u32 phc_index;
};

void ethtool_tsinfo_get_rsp_free(struct ethtool_tsinfo_get_rsp *rsp);

/*
 * Get tsinfo params.
 */
struct ethtool_tsinfo_get_rsp *
ethtool_tsinfo_get(struct ynl_sock *ys, struct ethtool_tsinfo_get_req *req);

/* ETHTOOL_MSG_TSINFO_GET - dump */
struct ethtool_tsinfo_get_req_dump {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_tsinfo_get_req_dump *
ethtool_tsinfo_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_tsinfo_get_req_dump));
}
void ethtool_tsinfo_get_req_dump_free(struct ethtool_tsinfo_get_req_dump *req);

static inline void
ethtool_tsinfo_get_req_dump_set_header_dev_index(struct ethtool_tsinfo_get_req_dump *req,
						 __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_tsinfo_get_req_dump_set_header_dev_name(struct ethtool_tsinfo_get_req_dump *req,
						const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_tsinfo_get_req_dump_set_header_flags(struct ethtool_tsinfo_get_req_dump *req,
					     __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_tsinfo_get_list {
	struct ethtool_tsinfo_get_list *next;
	struct ethtool_tsinfo_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_tsinfo_get_list_free(struct ethtool_tsinfo_get_list *rsp);

struct ethtool_tsinfo_get_list *
ethtool_tsinfo_get_dump(struct ynl_sock *ys,
			struct ethtool_tsinfo_get_req_dump *req);

/* ============== ETHTOOL_MSG_CABLE_TEST_ACT ============== */
/* ETHTOOL_MSG_CABLE_TEST_ACT - do */
struct ethtool_cable_test_act_req {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_cable_test_act_req *
ethtool_cable_test_act_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_cable_test_act_req));
}
void ethtool_cable_test_act_req_free(struct ethtool_cable_test_act_req *req);

static inline void
ethtool_cable_test_act_req_set_header_dev_index(struct ethtool_cable_test_act_req *req,
						__u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_cable_test_act_req_set_header_dev_name(struct ethtool_cable_test_act_req *req,
					       const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_cable_test_act_req_set_header_flags(struct ethtool_cable_test_act_req *req,
					    __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

/*
 * Cable test.
 */
int ethtool_cable_test_act(struct ynl_sock *ys,
			   struct ethtool_cable_test_act_req *req);

/* ============== ETHTOOL_MSG_CABLE_TEST_TDR_ACT ============== */
/* ETHTOOL_MSG_CABLE_TEST_TDR_ACT - do */
struct ethtool_cable_test_tdr_act_req {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_cable_test_tdr_act_req *
ethtool_cable_test_tdr_act_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_cable_test_tdr_act_req));
}
void
ethtool_cable_test_tdr_act_req_free(struct ethtool_cable_test_tdr_act_req *req);

static inline void
ethtool_cable_test_tdr_act_req_set_header_dev_index(struct ethtool_cable_test_tdr_act_req *req,
						    __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_cable_test_tdr_act_req_set_header_dev_name(struct ethtool_cable_test_tdr_act_req *req,
						   const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_cable_test_tdr_act_req_set_header_flags(struct ethtool_cable_test_tdr_act_req *req,
						__u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

/*
 * Cable test TDR.
 */
int ethtool_cable_test_tdr_act(struct ynl_sock *ys,
			       struct ethtool_cable_test_tdr_act_req *req);

/* ============== ETHTOOL_MSG_TUNNEL_INFO_GET ============== */
/* ETHTOOL_MSG_TUNNEL_INFO_GET - do */
struct ethtool_tunnel_info_get_req {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_tunnel_info_get_req *
ethtool_tunnel_info_get_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_tunnel_info_get_req));
}
void ethtool_tunnel_info_get_req_free(struct ethtool_tunnel_info_get_req *req);

static inline void
ethtool_tunnel_info_get_req_set_header_dev_index(struct ethtool_tunnel_info_get_req *req,
						 __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_tunnel_info_get_req_set_header_dev_name(struct ethtool_tunnel_info_get_req *req,
						const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_tunnel_info_get_req_set_header_flags(struct ethtool_tunnel_info_get_req *req,
					     __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_tunnel_info_get_rsp {
	struct {
		__u32 header:1;
		__u32 udp_ports:1;
	} _present;

	struct ethtool_header header;
	struct ethtool_tunnel_udp udp_ports;
};

void ethtool_tunnel_info_get_rsp_free(struct ethtool_tunnel_info_get_rsp *rsp);

/*
 * Get tsinfo params.
 */
struct ethtool_tunnel_info_get_rsp *
ethtool_tunnel_info_get(struct ynl_sock *ys,
			struct ethtool_tunnel_info_get_req *req);

/* ETHTOOL_MSG_TUNNEL_INFO_GET - dump */
struct ethtool_tunnel_info_get_req_dump {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_tunnel_info_get_req_dump *
ethtool_tunnel_info_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_tunnel_info_get_req_dump));
}
void
ethtool_tunnel_info_get_req_dump_free(struct ethtool_tunnel_info_get_req_dump *req);

static inline void
ethtool_tunnel_info_get_req_dump_set_header_dev_index(struct ethtool_tunnel_info_get_req_dump *req,
						      __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_tunnel_info_get_req_dump_set_header_dev_name(struct ethtool_tunnel_info_get_req_dump *req,
						     const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_tunnel_info_get_req_dump_set_header_flags(struct ethtool_tunnel_info_get_req_dump *req,
						  __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_tunnel_info_get_list {
	struct ethtool_tunnel_info_get_list *next;
	struct ethtool_tunnel_info_get_rsp obj __attribute__((aligned(8)));
};

void
ethtool_tunnel_info_get_list_free(struct ethtool_tunnel_info_get_list *rsp);

struct ethtool_tunnel_info_get_list *
ethtool_tunnel_info_get_dump(struct ynl_sock *ys,
			     struct ethtool_tunnel_info_get_req_dump *req);

/* ============== ETHTOOL_MSG_FEC_GET ============== */
/* ETHTOOL_MSG_FEC_GET - do */
struct ethtool_fec_get_req {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_fec_get_req *ethtool_fec_get_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_fec_get_req));
}
void ethtool_fec_get_req_free(struct ethtool_fec_get_req *req);

static inline void
ethtool_fec_get_req_set_header_dev_index(struct ethtool_fec_get_req *req,
					 __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_fec_get_req_set_header_dev_name(struct ethtool_fec_get_req *req,
					const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_fec_get_req_set_header_flags(struct ethtool_fec_get_req *req,
				     __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_fec_get_rsp {
	struct {
		__u32 header:1;
		__u32 modes:1;
		__u32 auto_:1;
		__u32 active:1;
		__u32 stats:1;
	} _present;

	struct ethtool_header header;
	struct ethtool_bitset modes;
	__u8 auto_;
	__u32 active;
	struct ethtool_fec_stat stats;
};

void ethtool_fec_get_rsp_free(struct ethtool_fec_get_rsp *rsp);

/*
 * Get FEC params.
 */
struct ethtool_fec_get_rsp *
ethtool_fec_get(struct ynl_sock *ys, struct ethtool_fec_get_req *req);

/* ETHTOOL_MSG_FEC_GET - dump */
struct ethtool_fec_get_req_dump {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_fec_get_req_dump *
ethtool_fec_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_fec_get_req_dump));
}
void ethtool_fec_get_req_dump_free(struct ethtool_fec_get_req_dump *req);

static inline void
ethtool_fec_get_req_dump_set_header_dev_index(struct ethtool_fec_get_req_dump *req,
					      __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_fec_get_req_dump_set_header_dev_name(struct ethtool_fec_get_req_dump *req,
					     const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_fec_get_req_dump_set_header_flags(struct ethtool_fec_get_req_dump *req,
					  __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_fec_get_list {
	struct ethtool_fec_get_list *next;
	struct ethtool_fec_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_fec_get_list_free(struct ethtool_fec_get_list *rsp);

struct ethtool_fec_get_list *
ethtool_fec_get_dump(struct ynl_sock *ys, struct ethtool_fec_get_req_dump *req);

/* ETHTOOL_MSG_FEC_GET - notify */
struct ethtool_fec_get_ntf {
	__u16 family;
	__u8 cmd;
	struct ynl_ntf_base_type *next;
	void (*free)(struct ethtool_fec_get_ntf *ntf);
	struct ethtool_fec_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_fec_get_ntf_free(struct ethtool_fec_get_ntf *rsp);

/* ============== ETHTOOL_MSG_FEC_SET ============== */
/* ETHTOOL_MSG_FEC_SET - do */
struct ethtool_fec_set_req {
	struct {
		__u32 header:1;
		__u32 modes:1;
		__u32 auto_:1;
		__u32 active:1;
		__u32 stats:1;
	} _present;

	struct ethtool_header header;
	struct ethtool_bitset modes;
	__u8 auto_;
	__u32 active;
	struct ethtool_fec_stat stats;
};

static inline struct ethtool_fec_set_req *ethtool_fec_set_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_fec_set_req));
}
void ethtool_fec_set_req_free(struct ethtool_fec_set_req *req);

static inline void
ethtool_fec_set_req_set_header_dev_index(struct ethtool_fec_set_req *req,
					 __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_fec_set_req_set_header_dev_name(struct ethtool_fec_set_req *req,
					const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_fec_set_req_set_header_flags(struct ethtool_fec_set_req *req,
				     __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}
static inline void
ethtool_fec_set_req_set_modes_nomask(struct ethtool_fec_set_req *req)
{
	req->_present.modes = 1;
	req->modes._present.nomask = 1;
}
static inline void
ethtool_fec_set_req_set_modes_size(struct ethtool_fec_set_req *req, __u32 size)
{
	req->_present.modes = 1;
	req->modes._present.size = 1;
	req->modes.size = size;
}
static inline void
__ethtool_fec_set_req_set_modes_bits_bit(struct ethtool_fec_set_req *req,
					 struct ethtool_bitset_bit *bit,
					 unsigned int n_bit)
{
	free(req->modes.bits.bit);
	req->modes.bits.bit = bit;
	req->modes.bits.n_bit = n_bit;
}
static inline void
ethtool_fec_set_req_set_auto_(struct ethtool_fec_set_req *req, __u8 auto_)
{
	req->_present.auto_ = 1;
	req->auto_ = auto_;
}
static inline void
ethtool_fec_set_req_set_active(struct ethtool_fec_set_req *req, __u32 active)
{
	req->_present.active = 1;
	req->active = active;
}
static inline void
ethtool_fec_set_req_set_stats_corrected(struct ethtool_fec_set_req *req,
					const void *corrected, size_t len)
{
	free(req->stats.corrected);
	req->stats._present.corrected_len = len;
	req->stats.corrected = malloc(req->stats._present.corrected_len);
	memcpy(req->stats.corrected, corrected, req->stats._present.corrected_len);
}
static inline void
ethtool_fec_set_req_set_stats_uncorr(struct ethtool_fec_set_req *req,
				     const void *uncorr, size_t len)
{
	free(req->stats.uncorr);
	req->stats._present.uncorr_len = len;
	req->stats.uncorr = malloc(req->stats._present.uncorr_len);
	memcpy(req->stats.uncorr, uncorr, req->stats._present.uncorr_len);
}
static inline void
ethtool_fec_set_req_set_stats_corr_bits(struct ethtool_fec_set_req *req,
					const void *corr_bits, size_t len)
{
	free(req->stats.corr_bits);
	req->stats._present.corr_bits_len = len;
	req->stats.corr_bits = malloc(req->stats._present.corr_bits_len);
	memcpy(req->stats.corr_bits, corr_bits, req->stats._present.corr_bits_len);
}

/*
 * Set FEC params.
 */
int ethtool_fec_set(struct ynl_sock *ys, struct ethtool_fec_set_req *req);

/* ============== ETHTOOL_MSG_MODULE_EEPROM_GET ============== */
/* ETHTOOL_MSG_MODULE_EEPROM_GET - do */
struct ethtool_module_eeprom_get_req {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_module_eeprom_get_req *
ethtool_module_eeprom_get_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_module_eeprom_get_req));
}
void
ethtool_module_eeprom_get_req_free(struct ethtool_module_eeprom_get_req *req);

static inline void
ethtool_module_eeprom_get_req_set_header_dev_index(struct ethtool_module_eeprom_get_req *req,
						   __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_module_eeprom_get_req_set_header_dev_name(struct ethtool_module_eeprom_get_req *req,
						  const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_module_eeprom_get_req_set_header_flags(struct ethtool_module_eeprom_get_req *req,
					       __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_module_eeprom_get_rsp {
	struct {
		__u32 header:1;
		__u32 offset:1;
		__u32 length:1;
		__u32 page:1;
		__u32 bank:1;
		__u32 i2c_address:1;
		__u32 data_len;
	} _present;

	struct ethtool_header header;
	__u32 offset;
	__u32 length;
	__u8 page;
	__u8 bank;
	__u8 i2c_address;
	void *data;
};

void
ethtool_module_eeprom_get_rsp_free(struct ethtool_module_eeprom_get_rsp *rsp);

/*
 * Get module EEPROM params.
 */
struct ethtool_module_eeprom_get_rsp *
ethtool_module_eeprom_get(struct ynl_sock *ys,
			  struct ethtool_module_eeprom_get_req *req);

/* ETHTOOL_MSG_MODULE_EEPROM_GET - dump */
struct ethtool_module_eeprom_get_req_dump {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_module_eeprom_get_req_dump *
ethtool_module_eeprom_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_module_eeprom_get_req_dump));
}
void
ethtool_module_eeprom_get_req_dump_free(struct ethtool_module_eeprom_get_req_dump *req);

static inline void
ethtool_module_eeprom_get_req_dump_set_header_dev_index(struct ethtool_module_eeprom_get_req_dump *req,
							__u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_module_eeprom_get_req_dump_set_header_dev_name(struct ethtool_module_eeprom_get_req_dump *req,
						       const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_module_eeprom_get_req_dump_set_header_flags(struct ethtool_module_eeprom_get_req_dump *req,
						    __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_module_eeprom_get_list {
	struct ethtool_module_eeprom_get_list *next;
	struct ethtool_module_eeprom_get_rsp obj __attribute__((aligned(8)));
};

void
ethtool_module_eeprom_get_list_free(struct ethtool_module_eeprom_get_list *rsp);

struct ethtool_module_eeprom_get_list *
ethtool_module_eeprom_get_dump(struct ynl_sock *ys,
			       struct ethtool_module_eeprom_get_req_dump *req);

/* ============== ETHTOOL_MSG_PHC_VCLOCKS_GET ============== */
/* ETHTOOL_MSG_PHC_VCLOCKS_GET - do */
struct ethtool_phc_vclocks_get_req {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_phc_vclocks_get_req *
ethtool_phc_vclocks_get_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_phc_vclocks_get_req));
}
void ethtool_phc_vclocks_get_req_free(struct ethtool_phc_vclocks_get_req *req);

static inline void
ethtool_phc_vclocks_get_req_set_header_dev_index(struct ethtool_phc_vclocks_get_req *req,
						 __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_phc_vclocks_get_req_set_header_dev_name(struct ethtool_phc_vclocks_get_req *req,
						const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_phc_vclocks_get_req_set_header_flags(struct ethtool_phc_vclocks_get_req *req,
					     __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_phc_vclocks_get_rsp {
	struct {
		__u32 header:1;
		__u32 num:1;
	} _present;

	struct ethtool_header header;
	__u32 num;
};

void ethtool_phc_vclocks_get_rsp_free(struct ethtool_phc_vclocks_get_rsp *rsp);

/*
 * Get PHC VCLOCKs.
 */
struct ethtool_phc_vclocks_get_rsp *
ethtool_phc_vclocks_get(struct ynl_sock *ys,
			struct ethtool_phc_vclocks_get_req *req);

/* ETHTOOL_MSG_PHC_VCLOCKS_GET - dump */
struct ethtool_phc_vclocks_get_req_dump {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_phc_vclocks_get_req_dump *
ethtool_phc_vclocks_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_phc_vclocks_get_req_dump));
}
void
ethtool_phc_vclocks_get_req_dump_free(struct ethtool_phc_vclocks_get_req_dump *req);

static inline void
ethtool_phc_vclocks_get_req_dump_set_header_dev_index(struct ethtool_phc_vclocks_get_req_dump *req,
						      __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_phc_vclocks_get_req_dump_set_header_dev_name(struct ethtool_phc_vclocks_get_req_dump *req,
						     const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_phc_vclocks_get_req_dump_set_header_flags(struct ethtool_phc_vclocks_get_req_dump *req,
						  __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_phc_vclocks_get_list {
	struct ethtool_phc_vclocks_get_list *next;
	struct ethtool_phc_vclocks_get_rsp obj __attribute__((aligned(8)));
};

void
ethtool_phc_vclocks_get_list_free(struct ethtool_phc_vclocks_get_list *rsp);

struct ethtool_phc_vclocks_get_list *
ethtool_phc_vclocks_get_dump(struct ynl_sock *ys,
			     struct ethtool_phc_vclocks_get_req_dump *req);

/* ============== ETHTOOL_MSG_MODULE_GET ============== */
/* ETHTOOL_MSG_MODULE_GET - do */
struct ethtool_module_get_req {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_module_get_req *ethtool_module_get_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_module_get_req));
}
void ethtool_module_get_req_free(struct ethtool_module_get_req *req);

static inline void
ethtool_module_get_req_set_header_dev_index(struct ethtool_module_get_req *req,
					    __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_module_get_req_set_header_dev_name(struct ethtool_module_get_req *req,
					   const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_module_get_req_set_header_flags(struct ethtool_module_get_req *req,
					__u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_module_get_rsp {
	struct {
		__u32 header:1;
		__u32 power_mode_policy:1;
		__u32 power_mode:1;
	} _present;

	struct ethtool_header header;
	__u8 power_mode_policy;
	__u8 power_mode;
};

void ethtool_module_get_rsp_free(struct ethtool_module_get_rsp *rsp);

/*
 * Get module params.
 */
struct ethtool_module_get_rsp *
ethtool_module_get(struct ynl_sock *ys, struct ethtool_module_get_req *req);

/* ETHTOOL_MSG_MODULE_GET - dump */
struct ethtool_module_get_req_dump {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_module_get_req_dump *
ethtool_module_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_module_get_req_dump));
}
void ethtool_module_get_req_dump_free(struct ethtool_module_get_req_dump *req);

static inline void
ethtool_module_get_req_dump_set_header_dev_index(struct ethtool_module_get_req_dump *req,
						 __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_module_get_req_dump_set_header_dev_name(struct ethtool_module_get_req_dump *req,
						const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_module_get_req_dump_set_header_flags(struct ethtool_module_get_req_dump *req,
					     __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_module_get_list {
	struct ethtool_module_get_list *next;
	struct ethtool_module_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_module_get_list_free(struct ethtool_module_get_list *rsp);

struct ethtool_module_get_list *
ethtool_module_get_dump(struct ynl_sock *ys,
			struct ethtool_module_get_req_dump *req);

/* ETHTOOL_MSG_MODULE_GET - notify */
struct ethtool_module_get_ntf {
	__u16 family;
	__u8 cmd;
	struct ynl_ntf_base_type *next;
	void (*free)(struct ethtool_module_get_ntf *ntf);
	struct ethtool_module_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_module_get_ntf_free(struct ethtool_module_get_ntf *rsp);

/* ============== ETHTOOL_MSG_MODULE_SET ============== */
/* ETHTOOL_MSG_MODULE_SET - do */
struct ethtool_module_set_req {
	struct {
		__u32 header:1;
		__u32 power_mode_policy:1;
		__u32 power_mode:1;
	} _present;

	struct ethtool_header header;
	__u8 power_mode_policy;
	__u8 power_mode;
};

static inline struct ethtool_module_set_req *ethtool_module_set_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_module_set_req));
}
void ethtool_module_set_req_free(struct ethtool_module_set_req *req);

static inline void
ethtool_module_set_req_set_header_dev_index(struct ethtool_module_set_req *req,
					    __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_module_set_req_set_header_dev_name(struct ethtool_module_set_req *req,
					   const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_module_set_req_set_header_flags(struct ethtool_module_set_req *req,
					__u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}
static inline void
ethtool_module_set_req_set_power_mode_policy(struct ethtool_module_set_req *req,
					     __u8 power_mode_policy)
{
	req->_present.power_mode_policy = 1;
	req->power_mode_policy = power_mode_policy;
}
static inline void
ethtool_module_set_req_set_power_mode(struct ethtool_module_set_req *req,
				      __u8 power_mode)
{
	req->_present.power_mode = 1;
	req->power_mode = power_mode;
}

/*
 * Set module params.
 */
int ethtool_module_set(struct ynl_sock *ys, struct ethtool_module_set_req *req);

/* ============== ETHTOOL_MSG_PSE_GET ============== */
/* ETHTOOL_MSG_PSE_GET - do */
struct ethtool_pse_get_req {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_pse_get_req *ethtool_pse_get_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_pse_get_req));
}
void ethtool_pse_get_req_free(struct ethtool_pse_get_req *req);

static inline void
ethtool_pse_get_req_set_header_dev_index(struct ethtool_pse_get_req *req,
					 __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_pse_get_req_set_header_dev_name(struct ethtool_pse_get_req *req,
					const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_pse_get_req_set_header_flags(struct ethtool_pse_get_req *req,
				     __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_pse_get_rsp {
	struct {
		__u32 header:1;
		__u32 admin_state:1;
		__u32 admin_control:1;
		__u32 pw_d_status:1;
	} _present;

	struct ethtool_header header;
	__u32 admin_state;
	__u32 admin_control;
	__u32 pw_d_status;
};

void ethtool_pse_get_rsp_free(struct ethtool_pse_get_rsp *rsp);

/*
 * Get Power Sourcing Equipment params.
 */
struct ethtool_pse_get_rsp *
ethtool_pse_get(struct ynl_sock *ys, struct ethtool_pse_get_req *req);

/* ETHTOOL_MSG_PSE_GET - dump */
struct ethtool_pse_get_req_dump {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_pse_get_req_dump *
ethtool_pse_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_pse_get_req_dump));
}
void ethtool_pse_get_req_dump_free(struct ethtool_pse_get_req_dump *req);

static inline void
ethtool_pse_get_req_dump_set_header_dev_index(struct ethtool_pse_get_req_dump *req,
					      __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_pse_get_req_dump_set_header_dev_name(struct ethtool_pse_get_req_dump *req,
					     const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_pse_get_req_dump_set_header_flags(struct ethtool_pse_get_req_dump *req,
					  __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_pse_get_list {
	struct ethtool_pse_get_list *next;
	struct ethtool_pse_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_pse_get_list_free(struct ethtool_pse_get_list *rsp);

struct ethtool_pse_get_list *
ethtool_pse_get_dump(struct ynl_sock *ys, struct ethtool_pse_get_req_dump *req);

/* ============== ETHTOOL_MSG_PSE_SET ============== */
/* ETHTOOL_MSG_PSE_SET - do */
struct ethtool_pse_set_req {
	struct {
		__u32 header:1;
		__u32 admin_state:1;
		__u32 admin_control:1;
		__u32 pw_d_status:1;
	} _present;

	struct ethtool_header header;
	__u32 admin_state;
	__u32 admin_control;
	__u32 pw_d_status;
};

static inline struct ethtool_pse_set_req *ethtool_pse_set_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_pse_set_req));
}
void ethtool_pse_set_req_free(struct ethtool_pse_set_req *req);

static inline void
ethtool_pse_set_req_set_header_dev_index(struct ethtool_pse_set_req *req,
					 __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_pse_set_req_set_header_dev_name(struct ethtool_pse_set_req *req,
					const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_pse_set_req_set_header_flags(struct ethtool_pse_set_req *req,
				     __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}
static inline void
ethtool_pse_set_req_set_admin_state(struct ethtool_pse_set_req *req,
				    __u32 admin_state)
{
	req->_present.admin_state = 1;
	req->admin_state = admin_state;
}
static inline void
ethtool_pse_set_req_set_admin_control(struct ethtool_pse_set_req *req,
				      __u32 admin_control)
{
	req->_present.admin_control = 1;
	req->admin_control = admin_control;
}
static inline void
ethtool_pse_set_req_set_pw_d_status(struct ethtool_pse_set_req *req,
				    __u32 pw_d_status)
{
	req->_present.pw_d_status = 1;
	req->pw_d_status = pw_d_status;
}

/*
 * Set Power Sourcing Equipment params.
 */
int ethtool_pse_set(struct ynl_sock *ys, struct ethtool_pse_set_req *req);

/* ============== ETHTOOL_MSG_RSS_GET ============== */
/* ETHTOOL_MSG_RSS_GET - do */
struct ethtool_rss_get_req {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_rss_get_req *ethtool_rss_get_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_rss_get_req));
}
void ethtool_rss_get_req_free(struct ethtool_rss_get_req *req);

static inline void
ethtool_rss_get_req_set_header_dev_index(struct ethtool_rss_get_req *req,
					 __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_rss_get_req_set_header_dev_name(struct ethtool_rss_get_req *req,
					const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_rss_get_req_set_header_flags(struct ethtool_rss_get_req *req,
				     __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_rss_get_rsp {
	struct {
		__u32 header:1;
		__u32 context:1;
		__u32 hfunc:1;
		__u32 indir_len;
		__u32 hkey_len;
	} _present;

	struct ethtool_header header;
	__u32 context;
	__u32 hfunc;
	void *indir;
	void *hkey;
};

void ethtool_rss_get_rsp_free(struct ethtool_rss_get_rsp *rsp);

/*
 * Get RSS params.
 */
struct ethtool_rss_get_rsp *
ethtool_rss_get(struct ynl_sock *ys, struct ethtool_rss_get_req *req);

/* ETHTOOL_MSG_RSS_GET - dump */
struct ethtool_rss_get_req_dump {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_rss_get_req_dump *
ethtool_rss_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_rss_get_req_dump));
}
void ethtool_rss_get_req_dump_free(struct ethtool_rss_get_req_dump *req);

static inline void
ethtool_rss_get_req_dump_set_header_dev_index(struct ethtool_rss_get_req_dump *req,
					      __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_rss_get_req_dump_set_header_dev_name(struct ethtool_rss_get_req_dump *req,
					     const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_rss_get_req_dump_set_header_flags(struct ethtool_rss_get_req_dump *req,
					  __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_rss_get_list {
	struct ethtool_rss_get_list *next;
	struct ethtool_rss_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_rss_get_list_free(struct ethtool_rss_get_list *rsp);

struct ethtool_rss_get_list *
ethtool_rss_get_dump(struct ynl_sock *ys, struct ethtool_rss_get_req_dump *req);

/* ============== ETHTOOL_MSG_PLCA_GET_CFG ============== */
/* ETHTOOL_MSG_PLCA_GET_CFG - do */
struct ethtool_plca_get_cfg_req {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_plca_get_cfg_req *
ethtool_plca_get_cfg_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_plca_get_cfg_req));
}
void ethtool_plca_get_cfg_req_free(struct ethtool_plca_get_cfg_req *req);

static inline void
ethtool_plca_get_cfg_req_set_header_dev_index(struct ethtool_plca_get_cfg_req *req,
					      __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_plca_get_cfg_req_set_header_dev_name(struct ethtool_plca_get_cfg_req *req,
					     const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_plca_get_cfg_req_set_header_flags(struct ethtool_plca_get_cfg_req *req,
					  __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_plca_get_cfg_rsp {
	struct {
		__u32 header:1;
		__u32 version:1;
		__u32 enabled:1;
		__u32 status:1;
		__u32 node_cnt:1;
		__u32 node_id:1;
		__u32 to_tmr:1;
		__u32 burst_cnt:1;
		__u32 burst_tmr:1;
	} _present;

	struct ethtool_header header;
	__u16 version;
	__u8 enabled;
	__u8 status;
	__u32 node_cnt;
	__u32 node_id;
	__u32 to_tmr;
	__u32 burst_cnt;
	__u32 burst_tmr;
};

void ethtool_plca_get_cfg_rsp_free(struct ethtool_plca_get_cfg_rsp *rsp);

/*
 * Get PLCA params.
 */
struct ethtool_plca_get_cfg_rsp *
ethtool_plca_get_cfg(struct ynl_sock *ys, struct ethtool_plca_get_cfg_req *req);

/* ETHTOOL_MSG_PLCA_GET_CFG - dump */
struct ethtool_plca_get_cfg_req_dump {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_plca_get_cfg_req_dump *
ethtool_plca_get_cfg_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_plca_get_cfg_req_dump));
}
void
ethtool_plca_get_cfg_req_dump_free(struct ethtool_plca_get_cfg_req_dump *req);

static inline void
ethtool_plca_get_cfg_req_dump_set_header_dev_index(struct ethtool_plca_get_cfg_req_dump *req,
						   __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_plca_get_cfg_req_dump_set_header_dev_name(struct ethtool_plca_get_cfg_req_dump *req,
						  const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_plca_get_cfg_req_dump_set_header_flags(struct ethtool_plca_get_cfg_req_dump *req,
					       __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_plca_get_cfg_list {
	struct ethtool_plca_get_cfg_list *next;
	struct ethtool_plca_get_cfg_rsp obj __attribute__((aligned(8)));
};

void ethtool_plca_get_cfg_list_free(struct ethtool_plca_get_cfg_list *rsp);

struct ethtool_plca_get_cfg_list *
ethtool_plca_get_cfg_dump(struct ynl_sock *ys,
			  struct ethtool_plca_get_cfg_req_dump *req);

/* ETHTOOL_MSG_PLCA_GET_CFG - notify */
struct ethtool_plca_get_cfg_ntf {
	__u16 family;
	__u8 cmd;
	struct ynl_ntf_base_type *next;
	void (*free)(struct ethtool_plca_get_cfg_ntf *ntf);
	struct ethtool_plca_get_cfg_rsp obj __attribute__((aligned(8)));
};

void ethtool_plca_get_cfg_ntf_free(struct ethtool_plca_get_cfg_ntf *rsp);

/* ============== ETHTOOL_MSG_PLCA_SET_CFG ============== */
/* ETHTOOL_MSG_PLCA_SET_CFG - do */
struct ethtool_plca_set_cfg_req {
	struct {
		__u32 header:1;
		__u32 version:1;
		__u32 enabled:1;
		__u32 status:1;
		__u32 node_cnt:1;
		__u32 node_id:1;
		__u32 to_tmr:1;
		__u32 burst_cnt:1;
		__u32 burst_tmr:1;
	} _present;

	struct ethtool_header header;
	__u16 version;
	__u8 enabled;
	__u8 status;
	__u32 node_cnt;
	__u32 node_id;
	__u32 to_tmr;
	__u32 burst_cnt;
	__u32 burst_tmr;
};

static inline struct ethtool_plca_set_cfg_req *
ethtool_plca_set_cfg_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_plca_set_cfg_req));
}
void ethtool_plca_set_cfg_req_free(struct ethtool_plca_set_cfg_req *req);

static inline void
ethtool_plca_set_cfg_req_set_header_dev_index(struct ethtool_plca_set_cfg_req *req,
					      __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_plca_set_cfg_req_set_header_dev_name(struct ethtool_plca_set_cfg_req *req,
					     const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_plca_set_cfg_req_set_header_flags(struct ethtool_plca_set_cfg_req *req,
					  __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}
static inline void
ethtool_plca_set_cfg_req_set_version(struct ethtool_plca_set_cfg_req *req,
				     __u16 version)
{
	req->_present.version = 1;
	req->version = version;
}
static inline void
ethtool_plca_set_cfg_req_set_enabled(struct ethtool_plca_set_cfg_req *req,
				     __u8 enabled)
{
	req->_present.enabled = 1;
	req->enabled = enabled;
}
static inline void
ethtool_plca_set_cfg_req_set_status(struct ethtool_plca_set_cfg_req *req,
				    __u8 status)
{
	req->_present.status = 1;
	req->status = status;
}
static inline void
ethtool_plca_set_cfg_req_set_node_cnt(struct ethtool_plca_set_cfg_req *req,
				      __u32 node_cnt)
{
	req->_present.node_cnt = 1;
	req->node_cnt = node_cnt;
}
static inline void
ethtool_plca_set_cfg_req_set_node_id(struct ethtool_plca_set_cfg_req *req,
				     __u32 node_id)
{
	req->_present.node_id = 1;
	req->node_id = node_id;
}
static inline void
ethtool_plca_set_cfg_req_set_to_tmr(struct ethtool_plca_set_cfg_req *req,
				    __u32 to_tmr)
{
	req->_present.to_tmr = 1;
	req->to_tmr = to_tmr;
}
static inline void
ethtool_plca_set_cfg_req_set_burst_cnt(struct ethtool_plca_set_cfg_req *req,
				       __u32 burst_cnt)
{
	req->_present.burst_cnt = 1;
	req->burst_cnt = burst_cnt;
}
static inline void
ethtool_plca_set_cfg_req_set_burst_tmr(struct ethtool_plca_set_cfg_req *req,
				       __u32 burst_tmr)
{
	req->_present.burst_tmr = 1;
	req->burst_tmr = burst_tmr;
}

/*
 * Set PLCA params.
 */
int ethtool_plca_set_cfg(struct ynl_sock *ys,
			 struct ethtool_plca_set_cfg_req *req);

/* ============== ETHTOOL_MSG_PLCA_GET_STATUS ============== */
/* ETHTOOL_MSG_PLCA_GET_STATUS - do */
struct ethtool_plca_get_status_req {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_plca_get_status_req *
ethtool_plca_get_status_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_plca_get_status_req));
}
void ethtool_plca_get_status_req_free(struct ethtool_plca_get_status_req *req);

static inline void
ethtool_plca_get_status_req_set_header_dev_index(struct ethtool_plca_get_status_req *req,
						 __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_plca_get_status_req_set_header_dev_name(struct ethtool_plca_get_status_req *req,
						const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_plca_get_status_req_set_header_flags(struct ethtool_plca_get_status_req *req,
					     __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_plca_get_status_rsp {
	struct {
		__u32 header:1;
		__u32 version:1;
		__u32 enabled:1;
		__u32 status:1;
		__u32 node_cnt:1;
		__u32 node_id:1;
		__u32 to_tmr:1;
		__u32 burst_cnt:1;
		__u32 burst_tmr:1;
	} _present;

	struct ethtool_header header;
	__u16 version;
	__u8 enabled;
	__u8 status;
	__u32 node_cnt;
	__u32 node_id;
	__u32 to_tmr;
	__u32 burst_cnt;
	__u32 burst_tmr;
};

void ethtool_plca_get_status_rsp_free(struct ethtool_plca_get_status_rsp *rsp);

/*
 * Get PLCA status params.
 */
struct ethtool_plca_get_status_rsp *
ethtool_plca_get_status(struct ynl_sock *ys,
			struct ethtool_plca_get_status_req *req);

/* ETHTOOL_MSG_PLCA_GET_STATUS - dump */
struct ethtool_plca_get_status_req_dump {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_plca_get_status_req_dump *
ethtool_plca_get_status_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_plca_get_status_req_dump));
}
void
ethtool_plca_get_status_req_dump_free(struct ethtool_plca_get_status_req_dump *req);

static inline void
ethtool_plca_get_status_req_dump_set_header_dev_index(struct ethtool_plca_get_status_req_dump *req,
						      __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_plca_get_status_req_dump_set_header_dev_name(struct ethtool_plca_get_status_req_dump *req,
						     const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_plca_get_status_req_dump_set_header_flags(struct ethtool_plca_get_status_req_dump *req,
						  __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_plca_get_status_list {
	struct ethtool_plca_get_status_list *next;
	struct ethtool_plca_get_status_rsp obj __attribute__((aligned(8)));
};

void
ethtool_plca_get_status_list_free(struct ethtool_plca_get_status_list *rsp);

struct ethtool_plca_get_status_list *
ethtool_plca_get_status_dump(struct ynl_sock *ys,
			     struct ethtool_plca_get_status_req_dump *req);

/* ============== ETHTOOL_MSG_MM_GET ============== */
/* ETHTOOL_MSG_MM_GET - do */
struct ethtool_mm_get_req {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_mm_get_req *ethtool_mm_get_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_mm_get_req));
}
void ethtool_mm_get_req_free(struct ethtool_mm_get_req *req);

static inline void
ethtool_mm_get_req_set_header_dev_index(struct ethtool_mm_get_req *req,
					__u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_mm_get_req_set_header_dev_name(struct ethtool_mm_get_req *req,
				       const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_mm_get_req_set_header_flags(struct ethtool_mm_get_req *req,
				    __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_mm_get_rsp {
	struct {
		__u32 header:1;
		__u32 pmac_enabled:1;
		__u32 tx_enabled:1;
		__u32 tx_active:1;
		__u32 tx_min_frag_size:1;
		__u32 rx_min_frag_size:1;
		__u32 verify_enabled:1;
		__u32 verify_time:1;
		__u32 max_verify_time:1;
		__u32 stats:1;
	} _present;

	struct ethtool_header header;
	__u8 pmac_enabled;
	__u8 tx_enabled;
	__u8 tx_active;
	__u32 tx_min_frag_size;
	__u32 rx_min_frag_size;
	__u8 verify_enabled;
	__u32 verify_time;
	__u32 max_verify_time;
	struct ethtool_mm_stat stats;
};

void ethtool_mm_get_rsp_free(struct ethtool_mm_get_rsp *rsp);

/*
 * Get MAC Merge configuration and state
 */
struct ethtool_mm_get_rsp *
ethtool_mm_get(struct ynl_sock *ys, struct ethtool_mm_get_req *req);

/* ETHTOOL_MSG_MM_GET - dump */
struct ethtool_mm_get_req_dump {
	struct {
		__u32 header:1;
	} _present;

	struct ethtool_header header;
};

static inline struct ethtool_mm_get_req_dump *
ethtool_mm_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_mm_get_req_dump));
}
void ethtool_mm_get_req_dump_free(struct ethtool_mm_get_req_dump *req);

static inline void
ethtool_mm_get_req_dump_set_header_dev_index(struct ethtool_mm_get_req_dump *req,
					     __u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_mm_get_req_dump_set_header_dev_name(struct ethtool_mm_get_req_dump *req,
					    const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_mm_get_req_dump_set_header_flags(struct ethtool_mm_get_req_dump *req,
					 __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}

struct ethtool_mm_get_list {
	struct ethtool_mm_get_list *next;
	struct ethtool_mm_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_mm_get_list_free(struct ethtool_mm_get_list *rsp);

struct ethtool_mm_get_list *
ethtool_mm_get_dump(struct ynl_sock *ys, struct ethtool_mm_get_req_dump *req);

/* ETHTOOL_MSG_MM_GET - notify */
struct ethtool_mm_get_ntf {
	__u16 family;
	__u8 cmd;
	struct ynl_ntf_base_type *next;
	void (*free)(struct ethtool_mm_get_ntf *ntf);
	struct ethtool_mm_get_rsp obj __attribute__((aligned(8)));
};

void ethtool_mm_get_ntf_free(struct ethtool_mm_get_ntf *rsp);

/* ============== ETHTOOL_MSG_MM_SET ============== */
/* ETHTOOL_MSG_MM_SET - do */
struct ethtool_mm_set_req {
	struct {
		__u32 header:1;
		__u32 verify_enabled:1;
		__u32 verify_time:1;
		__u32 tx_enabled:1;
		__u32 pmac_enabled:1;
		__u32 tx_min_frag_size:1;
	} _present;

	struct ethtool_header header;
	__u8 verify_enabled;
	__u32 verify_time;
	__u8 tx_enabled;
	__u8 pmac_enabled;
	__u32 tx_min_frag_size;
};

static inline struct ethtool_mm_set_req *ethtool_mm_set_req_alloc(void)
{
	return calloc(1, sizeof(struct ethtool_mm_set_req));
}
void ethtool_mm_set_req_free(struct ethtool_mm_set_req *req);

static inline void
ethtool_mm_set_req_set_header_dev_index(struct ethtool_mm_set_req *req,
					__u32 dev_index)
{
	req->_present.header = 1;
	req->header._present.dev_index = 1;
	req->header.dev_index = dev_index;
}
static inline void
ethtool_mm_set_req_set_header_dev_name(struct ethtool_mm_set_req *req,
				       const char *dev_name)
{
	free(req->header.dev_name);
	req->header._present.dev_name_len = strlen(dev_name);
	req->header.dev_name = malloc(req->header._present.dev_name_len + 1);
	memcpy(req->header.dev_name, dev_name, req->header._present.dev_name_len);
	req->header.dev_name[req->header._present.dev_name_len] = 0;
}
static inline void
ethtool_mm_set_req_set_header_flags(struct ethtool_mm_set_req *req,
				    __u32 flags)
{
	req->_present.header = 1;
	req->header._present.flags = 1;
	req->header.flags = flags;
}
static inline void
ethtool_mm_set_req_set_verify_enabled(struct ethtool_mm_set_req *req,
				      __u8 verify_enabled)
{
	req->_present.verify_enabled = 1;
	req->verify_enabled = verify_enabled;
}
static inline void
ethtool_mm_set_req_set_verify_time(struct ethtool_mm_set_req *req,
				   __u32 verify_time)
{
	req->_present.verify_time = 1;
	req->verify_time = verify_time;
}
static inline void
ethtool_mm_set_req_set_tx_enabled(struct ethtool_mm_set_req *req,
				  __u8 tx_enabled)
{
	req->_present.tx_enabled = 1;
	req->tx_enabled = tx_enabled;
}
static inline void
ethtool_mm_set_req_set_pmac_enabled(struct ethtool_mm_set_req *req,
				    __u8 pmac_enabled)
{
	req->_present.pmac_enabled = 1;
	req->pmac_enabled = pmac_enabled;
}
static inline void
ethtool_mm_set_req_set_tx_min_frag_size(struct ethtool_mm_set_req *req,
					__u32 tx_min_frag_size)
{
	req->_present.tx_min_frag_size = 1;
	req->tx_min_frag_size = tx_min_frag_size;
}

/*
 * Set MAC Merge configuration
 */
int ethtool_mm_set(struct ynl_sock *ys, struct ethtool_mm_set_req *req);

/* ETHTOOL_MSG_CABLE_TEST_NTF - event */
struct ethtool_cable_test_ntf_rsp {
	struct {
		__u32 header:1;
		__u32 status:1;
	} _present;

	struct ethtool_header header;
	__u8 status;
};

struct ethtool_cable_test_ntf {
	__u16 family;
	__u8 cmd;
	struct ynl_ntf_base_type *next;
	void (*free)(struct ethtool_cable_test_ntf *ntf);
	struct ethtool_cable_test_ntf_rsp obj __attribute__((aligned(8)));
};

void ethtool_cable_test_ntf_free(struct ethtool_cable_test_ntf *rsp);

/* ETHTOOL_MSG_CABLE_TEST_TDR_NTF - event */
struct ethtool_cable_test_tdr_ntf_rsp {
	struct {
		__u32 header:1;
		__u32 status:1;
		__u32 nest:1;
	} _present;

	struct ethtool_header header;
	__u8 status;
	struct ethtool_cable_nest nest;
};

struct ethtool_cable_test_tdr_ntf {
	__u16 family;
	__u8 cmd;
	struct ynl_ntf_base_type *next;
	void (*free)(struct ethtool_cable_test_tdr_ntf *ntf);
	struct ethtool_cable_test_tdr_ntf_rsp obj __attribute__((aligned(8)));
};

void ethtool_cable_test_tdr_ntf_free(struct ethtool_cable_test_tdr_ntf *rsp);

#endif /* _LINUX_ETHTOOL_GEN_H */
