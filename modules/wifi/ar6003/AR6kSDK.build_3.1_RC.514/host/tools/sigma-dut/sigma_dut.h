/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2010, Atheros Communications, Inc.
 */

#ifndef SIGMA_DUT_H
#define SIGMA_DUT_H

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef CONFIG_TRAFFIC_AGENT
#include <pthread.h>
#endif /* CONFIG_TRAFFIC_AGENT */

#if 0 // by bbelief
#include "wfa_portall.h"
#include "wfa_debug.h"
#include "wfa_ver.h"
#include "wfa_main.h"
#include "wfa_types.h"
#include "wfa_ca.h"
#include "wfa_tlv.h"
#include "wfa_sock.h"
#include "wfa_tg.h"
#include "wfa_cmds.h"
#include "wfa_rsp.h"
#endif


#ifdef __GNUC__
#define PRINTF_FORMAT(a,b) __attribute__ ((format (printf, (a), (b))))
#else
#define PRINTF_FORMAT(a,b)
#endif


struct sigma_dut;

#define MAX_PARAMS 50

struct sigma_cmd {
	char *params[MAX_PARAMS];
	char *values[MAX_PARAMS];
	int count;
};

#define MAX_CMD_LEN 2048

struct sigma_conn {
	int s;
	struct sockaddr_in addr;
	socklen_t addrlen;
	char buf[MAX_CMD_LEN + 5];
	int pos;
};

struct sigma_cmd_handler {
	struct sigma_cmd_handler *next;
	char *cmd;
	int (*validate)(struct sigma_cmd *cmd);
	/* process return value:
	 * -2 = failed, caller will send status,ERROR
	 * -1 = failed, caller will send status,INVALID
	 * 0 = success, response already sent
	 * 1 = success, caller will send status,COMPLETE
	 */
	int (*process)(struct sigma_dut *dut, struct sigma_conn *conn,
		       struct sigma_cmd *cmd);
};

#define P2P_GRP_ID_LEN 128
#define IP_ADDR_STR_LEN 16

struct wfa_cs_p2p_group {
	struct wfa_cs_p2p_group *next;
	char ifname[IFNAMSIZ];
	int go;
	char grpid[P2P_GRP_ID_LEN];
	char ssid[33];
};

#ifdef CONFIG_TRAFFIC_AGENT

#define MAX_SIGMA_STREAMS 16

struct sigma_stream {
	enum sigma_stream_profile {
		SIGMA_PROFILE_FILE_TRANSFER,
		SIGMA_PROFILE_MULTICAST,
		SIGMA_PROFILE_IPTV,
		SIGMA_PROFILE_TRANSACTION,
		SIGMA_PROFILE_START_SYNC
	} profile;
	int sender;
	struct in_addr dst;
	int dst_port;
	struct in_addr src;
	int src_port;
	int frame_rate;
	int duration;
	int payload_size;
	int start_delay;
	int max_cnt;
	enum sigma_traffic_class {
		SIGMA_TC_VOICE,
		SIGMA_TC_VIDEO,
		SIGMA_TC_BACKGROUND,
		SIGMA_TC_BEST_EFFORT
	} tc;
	int started;

	int sock;
	pthread_t thr;
	int stop;

	/* Statistics */
	int tx_frames;
	int rx_frames;
	int tx_payload_bytes;
	int rx_payload_bytes;
	int out_of_seq_frames;
};

#endif /* CONFIG_TRAFFIC_AGENT */


struct sigma_dut {
	int s; /* server TCP socket */
        //int debug_level;  // by bbelief
	struct sigma_cmd_handler *cmds;

	/* Default timeout value (seconds) for commands */
	unsigned int default_timeout;

	int next_streamid;

	const char *bridge; /* bridge interface to use in AP mode */

	enum sigma_mode {
		SIGMA_MODE_UNKNOWN,
		SIGMA_MODE_STATION,
		SIGMA_MODE_AP,
		SIGMA_MODE_SNIFFER
	} mode;

	/*
	 * Local cached values to handle API that does not provide all the
	 * needed information with commands that actually trigger some
	 * operations.
	 */
	int listen_chn;
	int persistent;
	int intra_bss;
	int noa_duration;
	int noa_interval;
	int noa_count;
	enum wfa_cs_wps_method {
		WFA_CS_WPS_NOT_READY,
		WFA_CS_WPS_PBC,
		WFA_CS_WPS_PIN_DISPLAY,
		WFA_CS_WPS_PIN_LABEL,
		WFA_CS_WPS_PIN_KEYPAD
	} wps_method;
	char wps_pin[9];

	struct wfa_cs_p2p_group *groups;

	char infra_ssid[33];
	int infra_network_id;

	enum p2p_mode {
		P2P_IDLE, P2P_DISCOVER, P2P_LISTEN, P2P_DISABLE
	} p2p_mode;

	int client_uapsd;

	char arp_ipaddr[IP_ADDR_STR_LEN];
	char arp_ifname[IFNAMSIZ + 1];

	enum sta_pmf {
		STA_PMF_DISABLED,
		STA_PMF_OPTIONAL,
		STA_PMF_REQUIRED
	} sta_pmf;

	/* AP configuration */
	char ap_ssid[33];
	enum ap_mode {
		AP_11a,
		AP_11g,
		AP_11b,
		AP_11na,
		AP_11ng
	} ap_mode;
	int ap_channel;
	int ap_rts;
	int ap_frgmnt;
	int ap_bcnint;
	int ap_p2p_mgmt;
	enum ap_key_mgmt {
		AP_OPEN,
		AP_WPA2_PSK,
		AP_WPA_PSK,
		AP_WPA2_EAP,
		AP_WPA_EAP,
		AP_WPA2_EAP_MIXED,
		AP_WPA2_PSK_MIXED
	} ap_key_mgmt;
	enum ap_pmf {
		AP_PMF_DISABLED,
		AP_PMF_OPTIONAL,
		AP_PMF_REQUIRED
	} ap_pmf;
	enum ap_cipher {
		AP_CCMP,
		AP_TKIP,
		AP_WEP,
		AP_PLAIN,
		AP_CCMP_TKIP
	} ap_cipher;
	char ap_passphrase[65];
	char ap_wepkey[27];
	char ap_radius_ipaddr[20];
	int ap_radius_port;
	char ap_radius_password[200];
	int ap_tdls_prohibit;
	int ap_tdls_prohibit_chswitch;

#ifdef CONFIG_TRAFFIC_AGENT
	/* Traffic Agent */
	struct sigma_stream streams[MAX_SIGMA_STREAMS];
	int num_streams;
#endif /* CONFIG_TRAFFIC_AGENT */

	const char *sniffer_ifname;
        int Concurrency;
};


enum sigma_dut_print_level {
	DUT_MSG_DEBUG, DUT_MSG_INFO, DUT_MSG_ERROR
};

void sigma_dut_hexdump(int level, const char *title, const unsigned char *buf, size_t len, int show);

#if 1 //  by bbelief
void sigma_dut_print(int level, const char *fmt, ...)
PRINTF_FORMAT(2, 3);
#else
void sigma_dut_print(struct sigma_dut *dut, int level, const char *fmt, ...)
PRINTF_FORMAT(3, 4);
#endif

enum sigma_status {
	SIGMA_RUNNING, SIGMA_INVALID, SIGMA_ERROR, SIGMA_COMPLETE
};

// by bbelief
void send_resp_debug(enum sigma_status status, char *buf);
#define send_resp(dummy1, dummy2, status, str)  send_resp_debug(status, str)


struct sigma_dut *sigma_dut_ptr(void);

#if 0
void send_resp(struct sigma_dut *dut, struct sigma_conn *conn,
	       enum sigma_status status, char *buf);
#endif

const char * get_param(struct sigma_cmd *cmd, const char *name);

int sigma_dut_reg_cmd(const char *cmd,
		      int (*validate)(struct sigma_cmd *cmd),
		      int (*process)(struct sigma_dut *dut,
				     struct sigma_conn *conn,
				     struct sigma_cmd *cmd));

void sigma_dut_register_cmds(void);


#endif /* SIGMA_DUT_H */
