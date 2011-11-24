/*
 * Sigma Control API DUT (wlantest)
 * Copyright (c) 2010, Atheros Communications, Inc.
 */

#include "sigma_dut.h"
#include <sys/un.h>
#include "wlantest_ctrl.h"


typedef unsigned int u32;
typedef unsigned char u8;

#define WPA_GET_BE32(a) ((((u32) (a)[0]) << 24) | (((u32) (a)[1]) << 16) | \
			 (((u32) (a)[2]) << 8) | ((u32) (a)[3]))
#define WPA_PUT_BE32(a, val)					\
	do {							\
		(a)[0] = (u8) ((((u32) (val)) >> 24) & 0xff);	\
		(a)[1] = (u8) ((((u32) (val)) >> 16) & 0xff);	\
		(a)[2] = (u8) ((((u32) (val)) >> 8) & 0xff);	\
		(a)[3] = (u8) (((u32) (val)) & 0xff);		\
	} while (0)

#ifndef ETH_ALEN
#define ETH_ALEN 6
#endif

int hwaddr_aton(const char *txt, unsigned char *addr);


static u8 * attr_get(u8 *buf, size_t buflen, enum wlantest_ctrl_attr attr,
		     size_t *len)
{
	u8 *pos = buf;

	while (pos + 8 <= buf + buflen) {
		enum wlantest_ctrl_attr a;
		size_t alen;
		a = WPA_GET_BE32(pos);
		pos += 4;
		alen = WPA_GET_BE32(pos);
		pos += 4;
		if (pos + alen > buf + buflen)
			return NULL;
		if (a == attr) {
			*len = alen;
			return pos;
		}
		pos += alen;
	}

	return NULL;
}


static u8 * attr_hdr_add(u8 *pos, u8 *end, enum wlantest_ctrl_attr attr,
			 size_t len)
{
	if (pos == NULL || end - pos < 8 + len)
		return NULL;
	WPA_PUT_BE32(pos, attr);
	pos += 4;
	WPA_PUT_BE32(pos, len);
	pos += 4;
	return pos;
}


static u8 * attr_add_str(u8 *pos, u8 *end, enum wlantest_ctrl_attr attr,
			 const char *str)
{
	size_t len = strlen(str);

	if (pos == NULL || end - pos < 8 + len)
		return NULL;
	WPA_PUT_BE32(pos, attr);
	pos += 4;
	WPA_PUT_BE32(pos, len);
	pos += 4;
	memcpy(pos, str, len);
	pos += len;
	return pos;
}


static u8 * attr_add_be32(u8 *pos, u8 *end, enum wlantest_ctrl_attr attr,
			  u32 val)
{
	if (pos == NULL || end - pos < 12)
		return NULL;
	WPA_PUT_BE32(pos, attr);
	pos += 4;
	WPA_PUT_BE32(pos, 4);
	pos += 4;
	WPA_PUT_BE32(pos, val);
	pos += 4;
	return pos;
}


static int open_wlantest(void)
{
	int s;
	struct sockaddr_un addr;

	s = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (s < 0) {
		perror("socket");
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path + 1, WLANTEST_SOCK_NAME,
		sizeof(addr.sun_path) - 1);
	if (connect(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("connect");
		close(s);
		return -1;
	}

	return s;
}


static int cmd_send_and_recv(int s, const u8 *cmd, size_t cmd_len,
			     u8 *resp, size_t max_resp_len)
{
	int res;
	enum wlantest_ctrl_cmd cmd_resp;

	if (send(s, cmd, cmd_len, 0) < 0)
		return -1;
	res = recv(s, resp, max_resp_len, 0);
	if (res < 4)
		return -1;

	cmd_resp = WPA_GET_BE32(resp);
	if (cmd_resp == WLANTEST_CTRL_SUCCESS)
		return res;

	return -1;
}


static int cmd_simple(int s, enum wlantest_ctrl_cmd cmd)
{
	u8 buf[4];
	int res;
	WPA_PUT_BE32(buf, cmd);
	res = cmd_send_and_recv(s, buf, sizeof(buf), buf, sizeof(buf));
	return res < 0 ? -1 : 0;
}


static int run_wlantest_simple(struct sigma_dut *dut, struct sigma_conn *conn,
			       enum wlantest_ctrl_cmd cmd)
{
	int s, ret;

	s = open_wlantest();
	if (s < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,wlantest not "
			  "available");
		return 0;
	}

	ret = cmd_simple(s, cmd);
	close(s);

	return ret < 0 ? -2 : 1;
}


static int cmd_wlantest_version(struct sigma_dut *dut, struct sigma_conn *conn,
				struct sigma_cmd *cmd)
{
	int s;
	u8 resp[WLANTEST_CTRL_MAX_RESP_LEN];
	u8 buf[4];
	char *version;
	size_t len;
	int rlen;
	char *rbuf;

	s = open_wlantest();
	if (s < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,wlantest not "
			  "available");
		return 0;
	}

	WPA_PUT_BE32(buf, WLANTEST_CTRL_VERSION);
	rlen = cmd_send_and_recv(s, buf, sizeof(buf), resp, sizeof(resp));
	close(s);
	if (rlen < 0)
		return -2;

	version = (char *) attr_get(resp + 4, rlen - 4, WLANTEST_ATTR_VERSION,
				    &len);
	if (version == NULL)
		return -2;

	rbuf = malloc(9 + len);
	if (rbuf == NULL)
		return -2;
	memcpy(rbuf, "version,", 8);
	memcpy(rbuf + 8, version, len);
	rbuf[8 + len] = '\0';
	send_resp(dut, conn, SIGMA_COMPLETE, rbuf);
	free(rbuf);
	return 0;
}


static int cmd_wlantest_set_channel(struct sigma_dut *dut,
				    struct sigma_conn *conn,
				    struct sigma_cmd *cmd)
{
	char buf[100];
	const char *chan;

	if (dut->sniffer_ifname == NULL) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Sniffer "
			  "interface not available");
		return 0;
	}

	chan = get_param(cmd, "channel");
	if (chan == NULL)
		return -1;

	snprintf(buf, sizeof(buf), "iw dev %s set type monitor",
		 dut->sniffer_ifname);
	if (system(buf) != 0) {
		snprintf(buf, sizeof(buf), "ifconfig %s down",
			 dut->sniffer_ifname);
		if (system(buf) != 0) {
			sigma_dut_print( DUT_MSG_INFO,
					"Failed to run '%s'", buf);
			return -2;
		}

		snprintf(buf, sizeof(buf), "iw dev %s set type monitor",
			 dut->sniffer_ifname);
		if (system(buf) != 0) {
			sigma_dut_print( DUT_MSG_INFO,
					"Failed to run '%s'", buf);
			return -2;
		}
	}

	snprintf(buf, sizeof(buf), "iw %s set channel %d HT20",
		 dut->sniffer_ifname, atoi(chan));
	if (system(buf) != 0) {
		sigma_dut_print( DUT_MSG_INFO, "Failed to run '%s'", buf);
		return -2;
	}

	snprintf(buf, sizeof(buf), "ifconfig %s up", dut->sniffer_ifname);
	if (system(buf) != 0) {
		sigma_dut_print( DUT_MSG_INFO, "Failed to run '%s'", buf);
		return -2;
	}

	return 1;
}


static int cmd_wlantest_flush(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	return run_wlantest_simple(dut, conn, WLANTEST_CTRL_FLUSH);
}


static int cmd_wlantest_send_frame(struct sigma_dut *dut,
				   struct sigma_conn *conn,
				   struct sigma_cmd *cmd)
{
	u8 resp[WLANTEST_CTRL_MAX_RESP_LEN];
	u8 buf[100], *end, *pos;
	int rlen;
	enum wlantest_inject_frame frame;
	enum wlantest_inject_protection prot;
	const char *val;
	int s;

	/* wlantest_send_frame,PMFFrameType,disassoc,PMFProtected,Unprotected,sender,AP,bssid,00:11:22:33:44:55,stationID,00:66:77:88:99:aa */

	pos = buf;
	end = buf + sizeof(buf);
	WPA_PUT_BE32(pos, WLANTEST_CTRL_INJECT);
	pos += 4;

	val = get_param(cmd, "Type");
	if (val == NULL)
		val = get_param(cmd, "PMFFrameType");
	if (val == NULL)
		return -1;
	if (strcasecmp(val, "disassoc") == 0)
		frame = WLANTEST_FRAME_DISASSOC;
	else if (strcasecmp(val, "deauth") == 0)
		frame = WLANTEST_FRAME_DEAUTH;
	else if (strcasecmp(val, "saquery") == 0)
		frame = WLANTEST_FRAME_SAQUERYREQ;
	else if (strcasecmp(val, "auth") == 0)
		frame = WLANTEST_FRAME_AUTH;
	else if (strcasecmp(val, "assocreq") == 0)
		frame = WLANTEST_FRAME_ASSOCREQ;
	else if (strcasecmp(val, "reassocreq") == 0)
		frame = WLANTEST_FRAME_REASSOCREQ;
	else {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Unsupported "
			  "PMFFrameType");
		return 0;
	}
	pos = attr_add_be32(pos, end, WLANTEST_ATTR_INJECT_FRAME, frame);

	val = get_param(cmd, "Protected");
	if (val == NULL)
		val = get_param(cmd, "PMFProtected");
	if (val == NULL)
		return -1;
	if (strcasecmp(val, "CorrectKey") == 0)
		prot = WLANTEST_INJECT_PROTECTED;
	else if (strcasecmp(val, "IncorrectKey") == 0)
		prot = WLANTEST_INJECT_INCORRECT_KEY;
	else if (strcasecmp(val, "Unprotected") == 0)
		prot = WLANTEST_INJECT_UNPROTECTED;
	else {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Unsupported "
			  "PMFProtected");
		return 0;
	}
	pos = attr_add_be32(pos, end, WLANTEST_ATTR_INJECT_PROTECTION, prot);

	val = get_param(cmd, "sender");
	if (val == NULL)
		return -1;
	if (strcasecmp(val, "ap") == 0) {
		pos = attr_add_be32(pos, end, WLANTEST_ATTR_INJECT_SENDER_AP,
				    1);
	} else if (strcasecmp(val, "sta") == 0) {
		pos = attr_add_be32(pos, end, WLANTEST_ATTR_INJECT_SENDER_AP,
				    0);
	} else {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Unsupported "
			  "sender");
		return 0;
	}

	val = get_param(cmd, "bssid");
	if (val == NULL)
		return -1;
	pos = attr_hdr_add(pos, end, WLANTEST_ATTR_BSSID, ETH_ALEN);
	if (hwaddr_aton(val, pos) < 0) {
		send_resp(dut, conn, SIGMA_INVALID, "errorCode,Invalid bssid");
		return 0;
	}
	pos += ETH_ALEN;

	val = get_param(cmd, "stationID");
	if (val == NULL)
		return -1;
	pos = attr_hdr_add(pos, end, WLANTEST_ATTR_STA_ADDR, ETH_ALEN);
	if (hwaddr_aton(val, pos) < 0) {
		send_resp(dut, conn, SIGMA_INVALID,
			  "errorCode,Invalid stationID");
		return 0;
	}
	pos += ETH_ALEN;

	s = open_wlantest();
	if (s < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,wlantest not "
			  "available");
		return 0;
	}
	rlen = cmd_send_and_recv(s, buf, pos - buf, resp, sizeof(resp));
	close(s);
	if (rlen < 0)
		return -2;
	return 1;
}


static int cmd_wlantest_add_passphrase(struct sigma_dut *dut,
				       struct sigma_conn *conn,
				       struct sigma_cmd *cmd)
{
	u8 resp[WLANTEST_CTRL_MAX_RESP_LEN];
	u8 buf[100], *end, *pos;
	int rlen;
	const char *val;
	int s;

	pos = buf;
	end = buf + sizeof(buf);
	WPA_PUT_BE32(pos, WLANTEST_CTRL_ADD_PASSPHRASE);
	pos += 4;

	val = get_param(cmd, "passphrase");
	if (val) {
		if (strlen(val) < 8 || strlen(val) > 63)
			return -1;
		pos = attr_add_str(pos, end, WLANTEST_ATTR_PASSPHRASE, val);
	} else {
		val = get_param(cmd, "wepkey");
		if (!val)
			return -1;
		pos = attr_add_str(pos, end, WLANTEST_ATTR_WEPKEY, val);
	}
	val = get_param(cmd, "bssid");
	if (val) {
		pos = attr_hdr_add(pos, end, WLANTEST_ATTR_BSSID, ETH_ALEN);
		if (hwaddr_aton(val, pos) < 0) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid bssid");
			return 0;
		}
		pos += ETH_ALEN;
	}

	s = open_wlantest();
	if (s < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,wlantest not "
			  "available");
		return 0;
	}
	rlen = cmd_send_and_recv(s, buf, pos - buf, resp, sizeof(resp));
	close(s);
	if (rlen < 0)
		return -2;
	return 1;
}


static int cmd_wlantest_clear_sta_counters(struct sigma_dut *dut,
					   struct sigma_conn *conn,
					   struct sigma_cmd *cmd)
{
	u8 resp[WLANTEST_CTRL_MAX_RESP_LEN];
	u8 buf[100], *end, *pos;
	int rlen;
	const char *val;
	int s;

	pos = buf;
	end = buf + sizeof(buf);
	WPA_PUT_BE32(pos, WLANTEST_CTRL_CLEAR_STA_COUNTERS);
	pos += 4;

	val = get_param(cmd, "bssid");
	if (val == NULL)
		return -1;
	if (val) {
		pos = attr_hdr_add(pos, end, WLANTEST_ATTR_BSSID, ETH_ALEN);
		if (hwaddr_aton(val, pos) < 0) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid bssid");
			return 0;
		}
		pos += ETH_ALEN;
	}

	val = get_param(cmd, "stationID");
	if (val == NULL)
		return -1;
	if (val) {
		pos = attr_hdr_add(pos, end, WLANTEST_ATTR_STA_ADDR, ETH_ALEN);
		if (hwaddr_aton(val, pos) < 0) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid stationID");
			return 0;
		}
		pos += ETH_ALEN;
	}

	s = open_wlantest();
	if (s < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,wlantest not "
			  "available");
		return 0;
	}
	rlen = cmd_send_and_recv(s, buf, pos - buf, resp, sizeof(resp));
	close(s);
	if (rlen < 0)
		return -2;
	return 1;
}


static int cmd_wlantest_clear_bss_counters(struct sigma_dut *dut,
					   struct sigma_conn *conn,
					   struct sigma_cmd *cmd)
{
	u8 resp[WLANTEST_CTRL_MAX_RESP_LEN];
	u8 buf[100], *end, *pos;
	int rlen;
	const char *val;
	int s;

	pos = buf;
	end = buf + sizeof(buf);
	WPA_PUT_BE32(pos, WLANTEST_CTRL_CLEAR_BSS_COUNTERS);
	pos += 4;

	val = get_param(cmd, "bssid");
	if (val == NULL)
		return -1;
	if (val) {
		pos = attr_hdr_add(pos, end, WLANTEST_ATTR_BSSID, ETH_ALEN);
		if (hwaddr_aton(val, pos) < 0) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid bssid");
			return 0;
		}
		pos += ETH_ALEN;
	}

	s = open_wlantest();
	if (s < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,wlantest not "
			  "available");
		return 0;
	}
	rlen = cmd_send_and_recv(s, buf, pos - buf, resp, sizeof(resp));
	close(s);
	if (rlen < 0)
		return -2;
	return 1;
}


static int cmd_wlantest_clear_tdls_counters(struct sigma_dut *dut,
					    struct sigma_conn *conn,
					    struct sigma_cmd *cmd)
{
	u8 resp[WLANTEST_CTRL_MAX_RESP_LEN];
	u8 buf[100], *end, *pos;
	int rlen;
	const char *val;
	int s;

	pos = buf;
	end = buf + sizeof(buf);
	WPA_PUT_BE32(pos, WLANTEST_CTRL_CLEAR_TDLS_COUNTERS);
	pos += 4;

	val = get_param(cmd, "bssid");
	if (val == NULL)
		return -1;
	if (val) {
		pos = attr_hdr_add(pos, end, WLANTEST_ATTR_BSSID, ETH_ALEN);
		if (hwaddr_aton(val, pos) < 0) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid bssid");
			return 0;
		}
		pos += ETH_ALEN;
	}

	val = get_param(cmd, "stationID");
	if (val == NULL)
		return -1;
	if (val) {
		pos = attr_hdr_add(pos, end, WLANTEST_ATTR_STA_ADDR, ETH_ALEN);
		if (hwaddr_aton(val, pos) < 0) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid stationID");
			return 0;
		}
		pos += ETH_ALEN;
	}

	val = get_param(cmd, "stationID2");
	if (val == NULL)
		return -1;
	if (val) {
		pos = attr_hdr_add(pos, end, WLANTEST_ATTR_STA2_ADDR,
				   ETH_ALEN);
		if (hwaddr_aton(val, pos) < 0) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid stationID2");
			return 0;
		}
		pos += ETH_ALEN;
	}

	s = open_wlantest();
	if (s < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,wlantest not "
			  "available");
		return 0;
	}
	rlen = cmd_send_and_recv(s, buf, pos - buf, resp, sizeof(resp));
	close(s);
	if (rlen < 0)
		return -2;
	return 1;
}


struct sta_counters {
	const char *name;
	enum wlantest_sta_counter num;
};

static const struct sta_counters sta_counters[] = {
	{ "auth_tx", WLANTEST_STA_COUNTER_AUTH_TX },
	{ "auth_rx", WLANTEST_STA_COUNTER_AUTH_RX },
	{ "assocreq_tx", WLANTEST_STA_COUNTER_ASSOCREQ_TX },
	{ "reassocreq_tx", WLANTEST_STA_COUNTER_REASSOCREQ_TX },
	{ "ptk_learned", WLANTEST_STA_COUNTER_PTK_LEARNED },
	{ "valid_deauth_tx", WLANTEST_STA_COUNTER_VALID_DEAUTH_TX },
	{ "valid_deauth_rx", WLANTEST_STA_COUNTER_VALID_DEAUTH_RX },
	{ "invalid_deauth_tx", WLANTEST_STA_COUNTER_INVALID_DEAUTH_TX },
	{ "invalid_deauth_rx", WLANTEST_STA_COUNTER_INVALID_DEAUTH_RX },
	{ "valid_disassoc_tx", WLANTEST_STA_COUNTER_VALID_DISASSOC_TX },
	{ "valid_disassoc_rx", WLANTEST_STA_COUNTER_VALID_DISASSOC_RX },
	{ "invalid_disassoc_tx", WLANTEST_STA_COUNTER_INVALID_DISASSOC_TX },
	{ "invalid_disassoc_rx", WLANTEST_STA_COUNTER_INVALID_DISASSOC_RX },
	{ "valid_saqueryreq_tx", WLANTEST_STA_COUNTER_VALID_SAQUERYREQ_TX },
	{ "valid_saqueryreq_rx", WLANTEST_STA_COUNTER_VALID_SAQUERYREQ_RX },
	{ "invalid_saqueryreq_tx",
	  WLANTEST_STA_COUNTER_INVALID_SAQUERYREQ_TX },
	{ "invalid_saqueryreq_rx",
	  WLANTEST_STA_COUNTER_INVALID_SAQUERYREQ_RX },
	{ "valid_saqueryresp_tx", WLANTEST_STA_COUNTER_VALID_SAQUERYRESP_TX },
	{ "valid_saqueryresp_rx", WLANTEST_STA_COUNTER_VALID_SAQUERYRESP_RX },
	{ "invalid_saqueryresp_tx",
	  WLANTEST_STA_COUNTER_INVALID_SAQUERYRESP_TX },
	{ "invalid_saqueryresp_rx",
	  WLANTEST_STA_COUNTER_INVALID_SAQUERYRESP_RX },
	{ "ping_ok", WLANTEST_STA_COUNTER_PING_OK },
	{ "assocresp_comeback", WLANTEST_STA_COUNTER_ASSOCRESP_COMEBACK },
	{ "reassocresp_comeback", WLANTEST_STA_COUNTER_REASSOCRESP_COMEBACK },
	{ "ping_ok_first_assoc", WLANTEST_STA_COUNTER_PING_OK_FIRST_ASSOC },
	{ "valid_deauth_rx_ack", WLANTEST_STA_COUNTER_VALID_DEAUTH_RX_ACK },
	{ "valid_disassoc_rx_ack",
	  WLANTEST_STA_COUNTER_VALID_DISASSOC_RX_ACK },
	{ "invalid_deauth_rx_ack",
	  WLANTEST_STA_COUNTER_INVALID_DEAUTH_RX_ACK },
	{ "invalid_disassoc_rx_ack",
	  WLANTEST_STA_COUNTER_INVALID_DISASSOC_RX_ACK },
	{ "deauth_rx_asleep", WLANTEST_STA_COUNTER_DEAUTH_RX_ASLEEP },
	{ "deauth_rx_awake", WLANTEST_STA_COUNTER_DEAUTH_RX_AWAKE },
	{ "disassoc_rx_asleep", WLANTEST_STA_COUNTER_DISASSOC_RX_ASLEEP },
	{ "disassoc_rx_awake", WLANTEST_STA_COUNTER_DISASSOC_RX_AWAKE },
	{ "prot_data_tx", WLANTEST_STA_COUNTER_PROT_DATA_TX },
	{ NULL, 0 }
};

static int cmd_wlantest_get_sta_counter(struct sigma_dut *dut,
					struct sigma_conn *conn,
					struct sigma_cmd *cmd)
{
	u8 resp[WLANTEST_CTRL_MAX_RESP_LEN];
	u8 buf[100], *end, *pos;
	int rlen;
	const char *val;
	int s, i;
	char ret[100];
	size_t len;

	pos = buf;
	end = buf + sizeof(buf);
	WPA_PUT_BE32(pos, WLANTEST_CTRL_GET_STA_COUNTER);
	pos += 4;

	val = get_param(cmd, "bssid");
	if (val == NULL)
		return -1;
	if (val) {
		pos = attr_hdr_add(pos, end, WLANTEST_ATTR_BSSID, ETH_ALEN);
		if (hwaddr_aton(val, pos) < 0) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid bssid");
			return 0;
		}
		pos += ETH_ALEN;
	}

	val = get_param(cmd, "stationID");
	if (val == NULL)
		return -1;
	if (val) {
		pos = attr_hdr_add(pos, end, WLANTEST_ATTR_STA_ADDR, ETH_ALEN);
		if (hwaddr_aton(val, pos) < 0) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid stationID");
			return 0;
		}
		pos += ETH_ALEN;
	}

	val = get_param(cmd, "field");
	if (val == NULL)
		return -1;
	for (i = 0; sta_counters[i].name; i++) {
		if (strcasecmp(sta_counters[i].name, val) == 0)
			break;
	}
	if (sta_counters[i].name == NULL) {
		send_resp(dut, conn, SIGMA_INVALID, "errorCode,Invalid field");
		return 0;
	}
	pos = attr_add_be32(pos, end, WLANTEST_ATTR_STA_COUNTER,
			    sta_counters[i].num);

	s = open_wlantest();
	if (s < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,wlantest not "
			  "available");
		return 0;
	}
	rlen = cmd_send_and_recv(s, buf, pos - buf, resp, sizeof(resp));
	close(s);


	pos = attr_get(resp + 4, rlen - 4, WLANTEST_ATTR_COUNTER, &len);
	if (pos == NULL || len != 4)
		return -2;
	snprintf(ret, sizeof(ret), "counter,%u", WPA_GET_BE32(pos));
	send_resp(dut, conn, SIGMA_COMPLETE, ret);
	return 0;
}


struct bss_counters {
	const char *name;
	enum wlantest_bss_counter num;
};

static const struct bss_counters bss_counters[] = {
	{ "valid_bip_mmie", WLANTEST_BSS_COUNTER_VALID_BIP_MMIE },
	{ "invalid_bip_mmie", WLANTEST_BSS_COUNTER_INVALID_BIP_MMIE },
	{ "missing_bip_mmie", WLANTEST_BSS_COUNTER_MISSING_BIP_MMIE },
	{ "bip_deauth", WLANTEST_BSS_COUNTER_BIP_DEAUTH },
	{ "bip_disassoc", WLANTEST_BSS_COUNTER_BIP_DISASSOC },
	{ NULL, 0 }
};

static int cmd_wlantest_get_bss_counter(struct sigma_dut *dut,
					struct sigma_conn *conn,
					struct sigma_cmd *cmd)
{
	u8 resp[WLANTEST_CTRL_MAX_RESP_LEN];
	u8 buf[100], *end, *pos;
	int rlen;
	const char *val;
	int s, i;
	char ret[100];
	size_t len;

	pos = buf;
	end = buf + sizeof(buf);
	WPA_PUT_BE32(pos, WLANTEST_CTRL_GET_BSS_COUNTER);
	pos += 4;

	val = get_param(cmd, "bssid");
	if (val == NULL)
		return -1;
	if (val) {
		pos = attr_hdr_add(pos, end, WLANTEST_ATTR_BSSID, ETH_ALEN);
		if (hwaddr_aton(val, pos) < 0) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid bssid");
			return 0;
		}
		pos += ETH_ALEN;
	}

	val = get_param(cmd, "field");
	if (val == NULL)
		return -1;
	for (i = 0; bss_counters[i].name; i++) {
		if (strcasecmp(bss_counters[i].name, val) == 0)
			break;
	}
	if (bss_counters[i].name == NULL) {
		send_resp(dut, conn, SIGMA_INVALID, "errorCode,Invalid field");
		return 0;
	}
	pos = attr_add_be32(pos, end, WLANTEST_ATTR_BSS_COUNTER,
			    bss_counters[i].num);

	s = open_wlantest();
	if (s < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,wlantest not "
			  "available");
		return 0;
	}
	rlen = cmd_send_and_recv(s, buf, pos - buf, resp, sizeof(resp));
	close(s);

	pos = attr_get(resp + 4, rlen - 4, WLANTEST_ATTR_COUNTER, &len);
	if (pos == NULL || len != 4)
		return -2;
	snprintf(ret, sizeof(ret), "counter,%u", WPA_GET_BE32(pos));
	send_resp(dut, conn, SIGMA_COMPLETE, ret);
	return 0;
}


struct tdls_counters {
	const char *name;
	enum wlantest_tdls_counter num;
};

static const struct tdls_counters tdls_counters[] = {
	{ "valid_direct_link", WLANTEST_TDLS_COUNTER_VALID_DIRECT_LINK },
	{ "invalid_direct_link", WLANTEST_TDLS_COUNTER_INVALID_DIRECT_LINK },
	{ "valid_ap_path", WLANTEST_TDLS_COUNTER_VALID_AP_PATH },
	{ "invalid_ap_path", WLANTEST_TDLS_COUNTER_INVALID_AP_PATH },
	{ "setup_req", WLANTEST_TDLS_COUNTER_SETUP_REQ },
	{ "setup_resp_ok", WLANTEST_TDLS_COUNTER_SETUP_RESP_OK },
	{ "setup_resp_fail", WLANTEST_TDLS_COUNTER_SETUP_RESP_FAIL },
	{ "setup_conf_ok", WLANTEST_TDLS_COUNTER_SETUP_CONF_OK },
	{ "setup_conf_fail", WLANTEST_TDLS_COUNTER_SETUP_CONF_FAIL },
	{ "teardown", WLANTEST_TDLS_COUNTER_TEARDOWN },
	{ NULL, 0 }
};

static int cmd_wlantest_get_tdls_counter(struct sigma_dut *dut,
					 struct sigma_conn *conn,
					 struct sigma_cmd *cmd)
{
	u8 resp[WLANTEST_CTRL_MAX_RESP_LEN];
	u8 buf[100], *end, *pos;
	int rlen;
	const char *val;
	int s, i;
	char ret[100];
	size_t len;

	pos = buf;
	end = buf + sizeof(buf);
	WPA_PUT_BE32(pos, WLANTEST_CTRL_GET_TDLS_COUNTER);
	pos += 4;

	val = get_param(cmd, "bssid");
	if (val == NULL)
		return -1;
	if (val) {
		pos = attr_hdr_add(pos, end, WLANTEST_ATTR_BSSID, ETH_ALEN);
		if (hwaddr_aton(val, pos) < 0) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid bssid");
			return 0;
		}
		pos += ETH_ALEN;
	}

	val = get_param(cmd, "stationID");
	if (val == NULL)
		return -1;
	if (val) {
		pos = attr_hdr_add(pos, end, WLANTEST_ATTR_STA_ADDR, ETH_ALEN);
		if (hwaddr_aton(val, pos) < 0) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid stationID");
			return 0;
		}
		pos += ETH_ALEN;
	}

	val = get_param(cmd, "stationID2");
	if (val == NULL)
		return -1;
	if (val) {
		pos = attr_hdr_add(pos, end, WLANTEST_ATTR_STA2_ADDR,
				   ETH_ALEN);
		if (hwaddr_aton(val, pos) < 0) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid stationID");
			return 0;
		}
		pos += ETH_ALEN;
	}

	val = get_param(cmd, "field");
	if (val == NULL)
		return -1;
	for (i = 0; tdls_counters[i].name; i++) {
		if (strcasecmp(tdls_counters[i].name, val) == 0)
			break;
	}
	if (tdls_counters[i].name == NULL) {
		send_resp(dut, conn, SIGMA_INVALID, "errorCode,Invalid field");
		return 0;
	}
	pos = attr_add_be32(pos, end, WLANTEST_ATTR_TDLS_COUNTER,
			    tdls_counters[i].num);

	s = open_wlantest();
	if (s < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,wlantest not "
			  "available");
		return 0;
	}
	rlen = cmd_send_and_recv(s, buf, pos - buf, resp, sizeof(resp));
	close(s);


	pos = attr_get(resp + 4, rlen - 4, WLANTEST_ATTR_COUNTER, &len);
	if (pos == NULL || len != 4)
		return -2;
	snprintf(ret, sizeof(ret), "counter,%u", WPA_GET_BE32(pos));
	send_resp(dut, conn, SIGMA_COMPLETE, ret);
	return 0;
}


struct sta_infos {
	const char *name;
	enum wlantest_sta_info num;
};

static const struct sta_infos sta_infos[] = {
	{ "proto", WLANTEST_STA_INFO_PROTO },
	{ "pairwise", WLANTEST_STA_INFO_PAIRWISE },
	{ "key_mgmt", WLANTEST_STA_INFO_KEY_MGMT },
	{ "rsn_capab", WLANTEST_STA_INFO_RSN_CAPAB },
	{ "state", WLANTEST_STA_INFO_STATE },
	{ NULL, 0 }
};

static int cmd_wlantest_info_sta(struct sigma_dut *dut,
				 struct sigma_conn *conn,
				 struct sigma_cmd *cmd)
{
	u8 resp[WLANTEST_CTRL_MAX_RESP_LEN];
	u8 buf[100], *end, *pos;
	int rlen;
	const char *val;
	int s, i;
	char ret[120];
	size_t len;
	char info[100];

	pos = buf;
	end = buf + sizeof(buf);
	WPA_PUT_BE32(pos, WLANTEST_CTRL_INFO_STA);
	pos += 4;

	val = get_param(cmd, "bssid");
	if (val == NULL)
		return -1;
	if (val) {
		pos = attr_hdr_add(pos, end, WLANTEST_ATTR_BSSID, ETH_ALEN);
		if (hwaddr_aton(val, pos) < 0) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid bssid");
			return 0;
		}
		pos += ETH_ALEN;
	}

	val = get_param(cmd, "stationID");
	if (val == NULL)
		return -1;
	if (val) {
		pos = attr_hdr_add(pos, end, WLANTEST_ATTR_STA_ADDR, ETH_ALEN);
		if (hwaddr_aton(val, pos) < 0) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid stationID");
			return 0;
		}
		pos += ETH_ALEN;
	}

	val = get_param(cmd, "field");
	if (val == NULL)
		return -1;
	for (i = 0; sta_infos[i].name; i++) {
		if (strcasecmp(sta_infos[i].name, val) == 0)
			break;
	}
	if (sta_infos[i].name == NULL) {
		send_resp(dut, conn, SIGMA_INVALID, "errorCode,Invalid field");
		return 0;
	}
	pos = attr_add_be32(pos, end, WLANTEST_ATTR_STA_INFO,
			    sta_infos[i].num);

	s = open_wlantest();
	if (s < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,wlantest not "
			  "available");
		return 0;
	}
	rlen = cmd_send_and_recv(s, buf, pos - buf, resp, sizeof(resp));
	close(s);


	pos = attr_get(resp + 4, rlen - 4, WLANTEST_ATTR_INFO, &len);
	if (pos == NULL)
		return -2;
	if (len >= sizeof(info))
		len = sizeof(info) - 1;
	memcpy(info, pos, len);
	info[len] = '\0';
	snprintf(ret, sizeof(ret), "info,%s", info);
	send_resp(dut, conn, SIGMA_COMPLETE, ret);
	return 0;
}


struct bss_infos {
	const char *name;
	enum wlantest_bss_info num;
};

static const struct bss_infos bss_infos[] = {
	{ "proto", WLANTEST_BSS_INFO_PROTO },
	{ "pairwise", WLANTEST_BSS_INFO_PAIRWISE },
	{ "group", WLANTEST_BSS_INFO_GROUP },
	{ "group_mgmt", WLANTEST_BSS_INFO_GROUP_MGMT },
	{ "key_mgmt", WLANTEST_BSS_INFO_KEY_MGMT },
	{ "rsn_capab", WLANTEST_BSS_INFO_RSN_CAPAB },
	{ NULL, 0 }
};

static int cmd_wlantest_info_bss(struct sigma_dut *dut,
				 struct sigma_conn *conn,
				 struct sigma_cmd *cmd)
{
	u8 resp[WLANTEST_CTRL_MAX_RESP_LEN];
	u8 buf[100], *end, *pos;
	int rlen;
	const char *val;
	int s, i;
	char ret[120];
	size_t len;
	char info[100];

	pos = buf;
	end = buf + sizeof(buf);
	WPA_PUT_BE32(pos, WLANTEST_CTRL_INFO_BSS);
	pos += 4;

	val = get_param(cmd, "bssid");
	if (val == NULL)
		return -1;
	if (val) {
		pos = attr_hdr_add(pos, end, WLANTEST_ATTR_BSSID, ETH_ALEN);
		if (hwaddr_aton(val, pos) < 0) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid bssid");
			return 0;
		}
		pos += ETH_ALEN;
	}

	val = get_param(cmd, "field");
	if (val == NULL)
		return -1;
	for (i = 0; bss_infos[i].name; i++) {
		if (strcasecmp(bss_infos[i].name, val) == 0)
			break;
	}
	if (bss_infos[i].name == NULL) {
		send_resp(dut, conn, SIGMA_INVALID, "errorCode,Invalid field");
		return 0;
	}
	pos = attr_add_be32(pos, end, WLANTEST_ATTR_BSS_INFO,
			    bss_infos[i].num);

	s = open_wlantest();
	if (s < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,wlantest not "
			  "available");
		return 0;
	}
	rlen = cmd_send_and_recv(s, buf, pos - buf, resp, sizeof(resp));
	close(s);


	pos = attr_get(resp + 4, rlen - 4, WLANTEST_ATTR_INFO, &len);
	if (pos == NULL)
		return -2;
	if (len >= sizeof(info))
		len = sizeof(info) - 1;
	memcpy(info, pos, len);
	info[len] = '\0';
	snprintf(ret, sizeof(ret), "info,%s", info);
	send_resp(dut, conn, SIGMA_COMPLETE, ret);
	return 0;
}


void wlantest_register_cmds(void)
{
	sigma_dut_reg_cmd("wlantest_version", NULL, cmd_wlantest_version);
	sigma_dut_reg_cmd("wlantest_set_channel", NULL,
			  cmd_wlantest_set_channel);
	sigma_dut_reg_cmd("wlantest_flush", NULL, cmd_wlantest_flush);
	sigma_dut_reg_cmd("wlantest_send_frame", NULL,
			  cmd_wlantest_send_frame);
	sigma_dut_reg_cmd("wlantest_add_passphrase", NULL,
			  cmd_wlantest_add_passphrase);
	sigma_dut_reg_cmd("wlantest_clear_sta_counters", NULL,
			  cmd_wlantest_clear_sta_counters);
	sigma_dut_reg_cmd("wlantest_clear_bss_counters", NULL,
			  cmd_wlantest_clear_bss_counters);
	sigma_dut_reg_cmd("wlantest_clear_tdls_counters", NULL,
			  cmd_wlantest_clear_tdls_counters);
	sigma_dut_reg_cmd("wlantest_get_sta_counter", NULL,
			  cmd_wlantest_get_sta_counter);
	sigma_dut_reg_cmd("wlantest_get_bss_counter", NULL,
			  cmd_wlantest_get_bss_counter);
	sigma_dut_reg_cmd("wlantest_get_tdls_counter", NULL,
			  cmd_wlantest_get_tdls_counter);
	sigma_dut_reg_cmd("wlantest_info_sta", NULL, cmd_wlantest_info_sta);
	sigma_dut_reg_cmd("wlantest_info_bss", NULL, cmd_wlantest_info_bss);
}
