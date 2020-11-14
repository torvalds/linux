// SPDX-License-Identifier: GPL-2.0

#ifndef _TEST_TCPBPF_H
#define _TEST_TCPBPF_H

struct tcpbpf_globals {
	__u32 event_map;
	__u32 total_retrans;
	__u32 data_segs_in;
	__u32 data_segs_out;
	__u32 bad_cb_test_rv;
	__u32 good_cb_test_rv;
	__u64 bytes_received;
	__u64 bytes_acked;
	__u32 num_listen;
	__u32 num_close_events;
	__u32 tcp_save_syn;
	__u32 tcp_saved_syn;
};
#endif
