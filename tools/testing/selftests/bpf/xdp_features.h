/* SPDX-License-Identifier: GPL-2.0 */

/* test commands */
enum test_commands {
	CMD_STOP,		/* CMD */
	CMD_START,		/* CMD */
	CMD_ECHO,		/* CMD */
	CMD_ACK,		/* CMD + data */
	CMD_GET_XDP_CAP,	/* CMD */
	CMD_GET_STATS,		/* CMD */
};

#define DUT_CTRL_PORT	12345
#define DUT_ECHO_PORT	12346

struct tlv_hdr {
	__be16 type;
	__be16 len;
	__u8 data[];
};
