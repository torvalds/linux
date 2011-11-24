/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2010, Atheros Communications, Inc.
 */

#include "sigma_dut.h"
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/ioctl.h>
#include "wpa_helpers.h"


enum driver_type {
	DRIVER_ATHEROS,
	DRIVER_MAC80211
};


static enum driver_type get_driver_type(void)
{
	struct stat s;
	if (stat("/sys/module/mac80211", &s) == 0)
		return DRIVER_MAC80211;
	return DRIVER_ATHEROS;
}


static int cmd_ap_ca_version(struct sigma_dut *dut, struct sigma_conn *conn,
			     struct sigma_cmd *cmd)
{
	/* const char *name = get_param(cmd, "NAME"); */
	send_resp(dut, conn, SIGMA_COMPLETE, "version,1.0");
	return 0;
}


static int cmd_ap_set_wireless(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	/* const char *name = get_param(cmd, "NAME"); */
	/* const char *ifname = get_param(cmd, "INTERFACE"); */
	const char *val;

	val = get_param(cmd, "SSID");
	if (val) {
		if (strlen(val) > sizeof(dut->ap_ssid) - 1)
			return -1;
		snprintf(dut->ap_ssid, sizeof(dut->ap_ssid), "%s", val);
	}

	val = get_param(cmd, "CHANNEL");
	if (val)
		dut->ap_channel = atoi(val);

	val = get_param(cmd, "MODE");
	if (val) {
		if (strcasecmp(val, "11a") == 0)
			dut->ap_mode = AP_11a;
		else if (strcasecmp(val, "11g") == 0)
			dut->ap_mode = AP_11g;
		else if (strcasecmp(val, "11b") == 0)
			dut->ap_mode = AP_11b;
		else if (strcasecmp(val, "11na") == 0)
			dut->ap_mode = AP_11na;
		else if (strcasecmp(val, "11ng") == 0)
			dut->ap_mode = AP_11ng;
		else {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Unsupported MODE");
			return 0;
		}
	}

	/* TODO: WME */

	/* TODO: WMMPS */

	val = get_param(cmd, "RTS");
	if (val)
		dut->ap_rts = atoi(val);

	val = get_param(cmd, "FRGMNT");
	if (val)
		dut->ap_frgmnt = atoi(val);

	/* TODO: PWRSAVE */

	val = get_param(cmd, "BCNINT");
	if (val)
		dut->ap_bcnint = atoi(val);

	/* TODO: RADIO */

	val = get_param(cmd, "P2PMgmtBit");
	if (val)
		dut->ap_p2p_mgmt = atoi(val);

	/* TODO: ChannelUsage */

	/* TODO: 40_INTOLERANT */
	/* TODO: ADDBA_REJECT */
	/* TODO: AMPDU */
	/* TODO: AMPDU_EXP */
	/* TODO: AMSDU */
	/* TODO: GREENFIELD */
	/* TODO: OFFSET */
	/* TODO: MCS_32 */
	/* TODO: MCS_FIXEDRATE */
	/* TODO: SPATIAL_RX_STREAM */
	/* TODO: SPATIAL_TX_STREAM */
	/* TODO: MPDU_MIN_START_SPACING */
	/* TODO: RIFS_TEST */
	/* TODO: SGI20 */
	/* TODO: STBC_TX */
	/* TODO: WIDTH */
	/* TODO: WIDTH_SCAN */

	val = get_param(cmd, "TDLSProhibit");
	dut->ap_tdls_prohibit = val && strcasecmp(val, "Enabled") == 0;
	val = get_param(cmd, "TDLSChswitchProhibit");
	dut->ap_tdls_prohibit_chswitch =
		val && strcasecmp(val, "Enabled") == 0;

	return 1;
}


static int cmd_ap_set_security(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	/* const char *name = get_param(cmd, "NAME"); */
	const char *val;

	val = get_param(cmd, "KEYMGNT");
	if (val) {
		if (strcasecmp(val, "WPA2-PSK") == 0) {
			dut->ap_key_mgmt = AP_WPA2_PSK;
			dut->ap_cipher = AP_CCMP;
		} else if (strcasecmp(val, "WPA2-EAP") == 0 ||
			   strcasecmp(val, "WPA2-Ent") == 0) {
			dut->ap_key_mgmt = AP_WPA2_EAP;
			dut->ap_cipher = AP_CCMP;
		} else if (strcasecmp(val, "WPA-PSK") == 0) {
			dut->ap_key_mgmt = AP_WPA_PSK;
			dut->ap_cipher = AP_TKIP;
		} else if (strcasecmp(val, "WPA-EAP") == 0 ||
			   strcasecmp(val, "WPA-Ent") == 0) {
			dut->ap_key_mgmt = AP_WPA_EAP;
			dut->ap_cipher = AP_TKIP;
		} else if (strcasecmp(val, "WPA2-Mixed") == 0) {
			dut->ap_key_mgmt = AP_WPA2_EAP_MIXED;
			dut->ap_cipher = AP_CCMP_TKIP;
		} else if (strcasecmp(val, "WPA2-PSK-Mixed") == 0) {
			dut->ap_key_mgmt = AP_WPA2_PSK_MIXED;
			dut->ap_cipher = AP_CCMP_TKIP;
		} else if (strcasecmp(val, "NONE") == 0) {
			dut->ap_key_mgmt = AP_OPEN;
			dut->ap_cipher = AP_PLAIN;
		} else {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Unsupported KEYMGNT");
			return 0;
		}
	}

	val = get_param(cmd, "ENCRYPT");
	if (val) {
		if (strcasecmp(val, "WEP") == 0) {
			dut->ap_cipher = AP_WEP;
		} else if (strcasecmp(val, "TKIP") == 0) {
			dut->ap_cipher = AP_TKIP;
		} else if (strcasecmp(val, "AES") == 0) {
			dut->ap_cipher = AP_CCMP;
		} else {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Unsupported ENCRYPT");
			return 0;
		}
	}

	val = get_param(cmd, "WEPKEY");
	if (val) {
		size_t len;
		if (dut->ap_cipher != AP_WEP) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Unexpected WEPKEY without WEP "
				  "configuration");
			return 0;
		}
		len = strlen(val);
		if (len != 10 && len != 26) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Unexpected WEPKEY length");
			return 0;
		}
		snprintf(dut->ap_wepkey, sizeof(dut->ap_wepkey), "%s", val);
	}

	val = get_param(cmd, "PSK");
	if (val) {
		if (strlen(val) > sizeof(dut->ap_passphrase) - 1)
			return -1;
		snprintf(dut->ap_passphrase, sizeof(dut->ap_passphrase),
			 "%s", val);
	}

	val = get_param(cmd, "PMF");
	if (val) {
		if (strcasecmp(val, "Disabled") == 0) {
			dut->ap_pmf = AP_PMF_DISABLED;
		} else if (strcasecmp(val, "Optional") == 0) {
			dut->ap_pmf = AP_PMF_OPTIONAL;
		} else if (strcasecmp(val, "Required") == 0) {
			dut->ap_pmf = AP_PMF_REQUIRED;
		} else {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Unsupported PMF");
			return 0;
		}
	}

	return 1;
}


static int cmd_ap_set_radius(struct sigma_dut *dut, struct sigma_conn *conn,
			     struct sigma_cmd *cmd)
{
	/* const char *name = get_param(cmd, "NAME"); */
	const char *val;

	val = get_param(cmd, "IPADDR");
	if (val) {
		if (strlen(val) > sizeof(dut->ap_radius_ipaddr) - 1)
			return -1;
		snprintf(dut->ap_radius_ipaddr, sizeof(dut->ap_radius_ipaddr),
			 "%s", val);
	}

	val = get_param(cmd, "PORT");
	if (val)
		dut->ap_radius_port = atoi(val);

	val = get_param(cmd, "PASSWORD");
	if (val) {
		if (strlen(val) > sizeof(dut->ap_radius_password) - 1)
			return -1;
		snprintf(dut->ap_radius_password,
			 sizeof(dut->ap_radius_password), "%s", val);
	}

	return 1;
}


static int cmd_ap_config_commit(struct sigma_dut *dut, struct sigma_conn *conn,
				struct sigma_cmd *cmd)
{
	/* const char *name = get_param(cmd, "NAME"); */
	FILE *f;
	const char *ifname;
	char buf[100];
	enum driver_type drv;

	drv = get_driver_type();

	if (dut->mode == SIGMA_MODE_STATION) {
		stop_sta_mode(dut);
		sleep(1);
	}

	if (dut->mode == SIGMA_MODE_SNIFFER && dut->sniffer_ifname) {
		snprintf(buf, sizeof(buf), "ifconfig %s down",
			 dut->sniffer_ifname);
		if (system(buf) != 0) {
			sigma_dut_print( DUT_MSG_INFO,
					"Failed to run '%s'", buf);
		}
		snprintf(buf, sizeof(buf), "iw dev %s set type station",
			 dut->sniffer_ifname);
		if (system(buf) != 0) {
			sigma_dut_print( DUT_MSG_INFO,
					"Failed to run '%s'", buf);
		}
	}

	dut->mode = SIGMA_MODE_AP;

	f = fopen("/tmp/sigma_dut-ap.conf", "w");
	if (f == NULL)
		return -2;

	switch (dut->ap_mode) {
	case AP_11g:
	case AP_11b:
	case AP_11ng:
		ifname = drv == DRIVER_MAC80211 ? "wlan0" : "ath0";
		fprintf(f, "hw_mode=g\n");
		break;
	case AP_11a:
	case AP_11na:
		if (drv == DRIVER_MAC80211) {
			if (if_nametoindex("wlan1") >= 0)
				ifname = "wlan1";
			else
				ifname = "wlan0";
		} else {
			if (if_nametoindex("ath1") >= 0)
				ifname = "ath1";
			else
				ifname = "ath0";
		}
		fprintf(f, "hw_mode=a\n");
		break;
	default:
		fclose(f);
		return -1;
	}

	if (drv == DRIVER_MAC80211)
		fprintf(f, "driver=nl80211\n");

	if (drv == DRIVER_MAC80211 &&
	    (dut->ap_mode == AP_11ng || dut->ap_mode == AP_11na))
		fprintf(f, "ieee80211n=1\n");

	if (drv == DRIVER_ATHEROS && if_nametoindex(ifname) <= 0) {
		sigma_dut_print( DUT_MSG_INFO, "Starting AP");
		if (system("apup") != 0) {
			sigma_dut_print( DUT_MSG_INFO, "apup failed");
		}
	}

	fprintf(f, "interface=%s\n", ifname);
	if (drv == DRIVER_ATHEROS)
		fprintf(f, "bridge=%s\n", dut->bridge ? dut->bridge : "br0");
	else if (dut->bridge)
		fprintf(f, "bridge=%s\n", dut->bridge);
	fprintf(f, "channel=%d\n", dut->ap_channel);

	fprintf(f, "ctrl_interface=/var/run/hostapd\n");

	fprintf(f, "ssid=%s\n", dut->ap_ssid);
	if (dut->ap_bcnint)
		fprintf(f, "beacon_int=%d\n", dut->ap_bcnint);

	switch (dut->ap_key_mgmt) {
	case AP_OPEN:
		if (dut->ap_cipher == AP_WEP)
			fprintf(f, "wep_key0=%s\n", dut->ap_wepkey);
		break;
	case AP_WPA2_PSK:
	case AP_WPA2_PSK_MIXED:
	case AP_WPA_PSK:
		if (dut->ap_key_mgmt == AP_WPA2_PSK)
			fprintf(f, "wpa=2\n");
		else if (dut->ap_key_mgmt == AP_WPA2_PSK_MIXED)
			fprintf(f, "wpa=3\n");
		else
			fprintf(f, "wpa=1\n");
		fprintf(f, "wpa_key_mgmt=WPA-PSK\n");
		switch (dut->ap_pmf) {
		case AP_PMF_DISABLED:
			fprintf(f, "wpa_key_mgmt=WPA-PSK\n");
			break;
		case AP_PMF_OPTIONAL:
			fprintf(f, "wpa_key_mgmt=WPA-PSK WPA-PSK-SHA256\n");
			break;
		case AP_PMF_REQUIRED:
			fprintf(f, "wpa_key_mgmt=WPA-PSK-SHA256\n");
			break;
		}
		if (dut->ap_cipher == AP_CCMP_TKIP)
			fprintf(f, "wpa_pairwise=CCMP TKIP\n");
		else if (dut->ap_cipher == AP_TKIP)
			fprintf(f, "wpa_pairwise=TKIP\n");
		else
			fprintf(f, "wpa_pairwise=CCMP\n");
		fprintf(f, "wpa_passphrase=%s\n", dut->ap_passphrase);
		break;
	case AP_WPA2_EAP:
	case AP_WPA2_EAP_MIXED:
	case AP_WPA_EAP:
		if (dut->ap_key_mgmt == AP_WPA2_EAP)
			fprintf(f, "wpa=2\n");
		else if (dut->ap_key_mgmt == AP_WPA2_EAP_MIXED)
			fprintf(f, "wpa=3\n");
		else
			fprintf(f, "wpa=1\n");
		switch (dut->ap_pmf) {
		case AP_PMF_DISABLED:
			fprintf(f, "wpa_key_mgmt=WPA-EAP\n");
			break;
		case AP_PMF_OPTIONAL:
			fprintf(f, "wpa_key_mgmt=WPA-EAP WPA-EAP-SHA256\n");
			break;
		case AP_PMF_REQUIRED:
			fprintf(f, "wpa_key_mgmt=WPA-EAP-SHA256\n");
			break;
		}
		if (dut->ap_cipher == AP_CCMP_TKIP)
			fprintf(f, "wpa_pairwise=CCMP TKIP\n");
		else if (dut->ap_cipher == AP_TKIP)
			fprintf(f, "wpa_pairwise=TKIP\n");
		else
			fprintf(f, "wpa_pairwise=CCMP\n");
		fprintf(f, "auth_server_addr=%s\n", dut->ap_radius_ipaddr);
		if (dut->ap_radius_port)
			fprintf(f, "auth_server_port=%d\n",
				dut->ap_radius_port);
		fprintf(f, "auth_server_shared_secret=%s\n",
			dut->ap_radius_password);
		break;
	}

	switch (dut->ap_pmf) {
	case AP_PMF_DISABLED:
		break;
	case AP_PMF_OPTIONAL:
		fprintf(f, "ieee80211w=1\n");
		break;
	case AP_PMF_REQUIRED:
		fprintf(f, "ieee80211w=2\n");
		break;
	}

	if (dut->ap_p2p_mgmt)
		fprintf(f, "manage_p2p=1\n");

	if (dut->ap_tdls_prohibit)
		fprintf(f, "tdls_prohibit=1\n");
	if (dut->ap_tdls_prohibit_chswitch)
		fprintf(f, "tdls_prohibit_chan_switch=1\n");

	fclose(f);

	if (system("killall hostapd") == 0) {
		int i;

		/* Wait some time to allow hostapd to complete cleanup before
		 * starting a new process */
		for (i = 0; i < 10; i++) {
			usleep(500000);
			if (system("pidof hostapd") != 0)
				break;
		}
	}

	if (drv == DRIVER_ATHEROS) {
		snprintf(buf, sizeof(buf), "iwconfig %s channel %d",
			 ifname, dut->ap_channel);
		if (system(buf) != 0) {
			sigma_dut_print( DUT_MSG_ERROR, "iwconfig failed");
		}

		if (dut->ap_bcnint) {
			snprintf(buf, sizeof(buf), "iwpriv %s bintval %d",
				 ifname, dut->ap_bcnint);
			if (system(buf) != 0) {
				sigma_dut_print( DUT_MSG_ERROR,
						"iwpriv bintval failed");
			}
		}
	}

	if (system("hostapd -B /tmp/sigma_dut-ap.conf") != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to start hostapd");
		return 0;
	}

	return 1;
}


static int cmd_ap_get_info(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd)
{
	/* const char *name = get_param(cmd, "NAME"); */
	struct stat s;
	char resp[200];
	FILE *f;
	enum driver_type drv = get_driver_type();

	if (drv == DRIVER_ATHEROS) {
		/* Atheros AP */
		struct utsname uts;
		char *version, athver[100];

		if (stat("/proc/athversion", &s) != 0) {
			if (system("/etc/rc.d/rc.wlan up") != 0) {
			}
		}

		athver[0] = '\0';
		f = fopen("/proc/athversion", "r");
		if (f) {
			if (fgets(athver, sizeof(athver), f)) {
				char *pos = strchr(athver, '\n');
				if (pos)
					*pos = '\0';
			}
			fclose(f);
		}

		if (uname(&uts) == 0)
			version = uts.release;
		else
			version = "Unknown";

		if (if_nametoindex("ath1") >= 0)
			snprintf(resp, sizeof(resp), "interface,ath0_24G "
				 "ath1_5G,agent,1.0,version,%s/drv:%s",
				 version, athver);
		else
			snprintf(resp, sizeof(resp), "interface,ath0_24G,"
				 "agent,1.0,version,%s/drv:%s",
				 version, athver);

		send_resp(dut, conn, SIGMA_COMPLETE, resp);
		return 0;
	}

	if (drv == DRIVER_MAC80211) {
		struct utsname uts;
		char *version;

		if (uname(&uts) == 0)
			version = uts.release;
		else
			version = "Unknown";

		if (if_nametoindex("wlan1") >= 0)
			snprintf(resp, sizeof(resp), "interface,wlan0_24G "
				 "wlan1_5G,agent,1.0,version,%s", version);
		else
			snprintf(resp, sizeof(resp), "interface,wlan0_24G,"
				 "agent,1.0,version,%s", version);

		send_resp(dut, conn, SIGMA_COMPLETE, resp);
		return 0;
	}

	/* Unknown AP */
	return 1;
}


static int cmd_ap_deauth_sta(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd)
{
	/* const char *name = get_param(cmd, "NAME"); */
	/* const char *ifname = get_param(cmd, "INTERFACE"); */
	const char *val;
	char buf[100];

	val = get_param(cmd, "MinorCode");
	if (val) {
		/* TODO: add support for P2P minor code */
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,MinorCode not "
			  "yet supported");
		return 0;
	}

	val = get_param(cmd, "STA_MAC_ADDRESS");
	if (val == NULL)
		return -1;
	snprintf(buf, sizeof(buf), "hostapd_cli deauth %s", val);
	if (system(buf) != 0)
		return -2;

	return 1;
}


#ifdef __linux__
int inject_frame(int s, const void *data, size_t len, int encrypt);
int open_monitor(const char *ifname);
int hwaddr_aton(const char *txt, unsigned char *addr);
#endif /* __linux__ */

enum send_frame_type {
		DISASSOC, DEAUTH, SAQUERY
};
enum send_frame_protection {
	CORRECT_KEY, INCORRECT_KEY, UNPROTECTED
};


static int ap_inject_frame(struct sigma_dut *dut, struct sigma_conn *conn,
			   enum send_frame_type frame,
			   enum send_frame_protection protected,
			   const char *sta_addr)
{
#ifdef __linux__
	unsigned char buf[1000], *pos;
	int s, res;
	unsigned char addr_sta[6], addr_own[6];
	char *ifname;
	char cbuf[100];
	struct ifreq ifr;

	if ((dut->ap_mode == AP_11a || dut->ap_mode == AP_11na) &&
	    if_nametoindex("wlan1") >= 0)
		ifname = "wlan1";
	else
		ifname = "wlan0";

	if (hwaddr_aton(sta_addr, addr_sta) < 0)
		return -1;

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		return -1;
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(s, SIOCGIFHWADDR, &ifr) < 0) {
		perror("ioctl");
		close(s);
		return -1;
	}
	close(s);
	memcpy(addr_own, ifr.ifr_hwaddr.sa_data, 6);

	if (if_nametoindex("sigmadut") == 0) {
		snprintf(cbuf, sizeof(cbuf),
			 "iw dev %s interface add sigmadut type monitor",
			 ifname);
		if (system(cbuf) != 0 ||
		    if_nametoindex("sigmadut") == 0) {
			sigma_dut_print( DUT_MSG_ERROR, "Failed to add "
					"monitor interface with '%s'", cbuf);
			return -2;
		}
	}

	if (system("ifconfig sigmadut up") != 0) {
		sigma_dut_print( DUT_MSG_ERROR, "Failed to set "
				"monitor interface up");
		return -2;
	}

	pos = buf;

	/* Frame Control */
	switch (frame) {
	case DISASSOC:
		*pos++ = 0xa0;
		break;
	case DEAUTH:
		*pos++ = 0xc0;
		break;
	case SAQUERY:
		*pos++ = 0xd0;
		break;
	}

	if (protected == INCORRECT_KEY)
		*pos++ = 0x40; /* Set Protected field to 1 */
	else
		*pos++ = 0x00;

	/* Duration */
	*pos++ = 0x00;
	*pos++ = 0x00;

	/* addr1 = DA (station) */
	memcpy(pos, addr_sta, 6);
	pos += 6;
	/* addr2 = SA (own address) */
	memcpy(pos, addr_own, 6);
	pos += 6;
	/* addr3 = BSSID (own address) */
	memcpy(pos, addr_own, 6);
	pos += 6;

	/* Seq# (to be filled by driver/mac80211) */
	*pos++ = 0x00;
	*pos++ = 0x00;

	if (protected == INCORRECT_KEY) {
		/* CCMP parameters */
		memcpy(pos, "\x61\x01\x00\x20\x00\x10\x00\x00", 8);
		pos += 8;
	}

	if (protected == INCORRECT_KEY) {
		switch (frame) {
		case DEAUTH:
			/* Reason code (encrypted) */
			memcpy(pos, "\xa7\x39", 2);
			pos += 2;
			break;
		case DISASSOC:
			/* Reason code (encrypted) */
			memcpy(pos, "\xa7\x39", 2);
			pos += 2;
			break;
		case SAQUERY:
			/* Category|Action|TransID (encrypted) */
			memcpy(pos, "\x6f\xbd\xe9\x4d", 4);
			pos += 4;
			break;
		default:
			return -1;
		}

		/* CCMP MIC */
		memcpy(pos, "\xc8\xd8\x3b\x06\x5d\xb7\x25\x68", 8);
		pos += 8;
	} else {
		switch (frame) {
		case DEAUTH:
			/* reason code = 8 */
			*pos++ = 0x08;
			*pos++ = 0x00;
			break;
		case DISASSOC:
			/* reason code = 8 */
			*pos++ = 0x08;
			*pos++ = 0x00;
			break;
		case SAQUERY:
			/* Category - SA Query */
			*pos++ = 0x08;
			/* SA query Action - Request */
			*pos++ = 0x00;
			/* Transaction ID */
			*pos++ = 0x12;
			*pos++ = 0x34;
			break;
		}
	}

	s = open_monitor("sigmadut");
	if (s < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Failed to open "
			  "monitor socket");
		return 0;
	}

	res = inject_frame(s, buf, pos - buf, protected == CORRECT_KEY);
	if (res < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Failed to "
			  "inject frame");
		return 0;
	}
	if (res < pos - buf) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Only partial "
			  "frame sent");
		return 0;
	}

	close(s);

	return 1;
#else /* __linux__ */
	send_resp(dut, conn, SIGMA_ERROR, "errorCode,ap_send_frame not "
		  "yet supported");
	return 0;
#endif /* __linux__ */
}


static int cmd_ap_send_frame(struct sigma_dut *dut, struct sigma_conn *conn,
			     struct sigma_cmd *cmd)
{
	/* const char *name = get_param(cmd, "NAME"); */
	/* const char *ifname = get_param(cmd, "INTERFACE"); */
	const char *val;
	enum send_frame_type frame;
	enum send_frame_protection protected;
	char buf[100];

	val = get_param(cmd, "PMFFrameType");
	if (val == NULL)
		return -1;
	if (strcasecmp(val, "disassoc") == 0)
		frame = DISASSOC;
	else if (strcasecmp(val, "deauth") == 0)
		frame = DEAUTH;
	else if (strcasecmp(val, "saquery") == 0)
		frame = SAQUERY;
	else {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Unsupported "
			  "PMFFrameType");
		return 0;
	}

	val = get_param(cmd, "PMFProtected");
	if (val == NULL)
		val = get_param(cmd, "Protected");
	if (val == NULL)
		return -1;
	if (strcasecmp(val, "Correct-key") == 0 ||
	    strcasecmp(val, "CorrectKey") == 0)
		protected = CORRECT_KEY;
	else if (strcasecmp(val, "IncorrectKey") == 0)
		protected = INCORRECT_KEY;
	else if (strcasecmp(val, "Unprotected") == 0)
		protected = UNPROTECTED;
	else {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Unsupported "
			  "PMFProtected");
		return 0;
	}

	val = get_param(cmd, "stationID");
	if (val == NULL)
		return -1;

	if (protected == INCORRECT_KEY ||
	    (protected == UNPROTECTED && frame == SAQUERY))
		return ap_inject_frame(dut, conn, frame, protected, val);

	switch (frame) {
	case DISASSOC:
		snprintf(buf, sizeof(buf), "hostapd_cli disassoc %s test=%d",
			 val, protected == CORRECT_KEY);
		break;
	case DEAUTH:
		snprintf(buf, sizeof(buf), "hostapd_cli deauth %s test=%d",
			 val, protected == CORRECT_KEY);
		break;
	case SAQUERY:
		snprintf(buf, sizeof(buf), "hostapd_cli sa_query %s", val);
		break;
	}

	if (system(buf) != 0)
		return -2;

	return 1;
}


static int cmd_ap_get_mac_address(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd)
{
#ifdef __linux__
	/* const char *name = get_param(cmd, "NAME"); */
	/* const char *ifname = get_param(cmd, "INTERFACE"); */
	char resp[50];
	unsigned char addr[6];
	char *ifname;
	struct ifreq ifr;
	int s;

	if ((dut->ap_mode == AP_11a || dut->ap_mode == AP_11na) &&
	    if_nametoindex("wlan1") >= 0)
		ifname = "wlan1";
	else
		ifname = "wlan0";

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		return -1;
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(s, SIOCGIFHWADDR, &ifr) < 0) {
		perror("ioctl");
		close(s);
		return -1;
	}
	close(s);
	memcpy(addr, ifr.ifr_hwaddr.sa_data, 6);

	snprintf(resp, sizeof(resp), "mac,%02x:%02x:%02x:%02x:%02x:%02x",
		 addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return 0;
#else /* __linux__ */
	send_resp(dut, conn, SIGMA_ERROR, "errorCode,ap_get_mac_address not "
		  "yet supported");
	return 0;
#endif /* __linux__ */
}


static int cmd_accesspoint(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd)
{
	/* const char *name = get_param(cmd, "NAME"); */
	return 1;
}


void ap_register_cmds(void)
{
	sigma_dut_reg_cmd("ap_ca_version", NULL, cmd_ap_ca_version);
	sigma_dut_reg_cmd("ap_set_wireless", NULL, cmd_ap_set_wireless);
	sigma_dut_reg_cmd("ap_set_11n_wireless", NULL, cmd_ap_set_wireless);
	sigma_dut_reg_cmd("ap_set_11n", NULL, cmd_ap_set_wireless);
	sigma_dut_reg_cmd("ap_set_security", NULL, cmd_ap_set_security);
	/* TODO: ap_set_apqos */
	/* TODO: ap_set_staqos */
	sigma_dut_reg_cmd("ap_set_radius", NULL, cmd_ap_set_radius);
	/* TODO: ap_reboot */
	sigma_dut_reg_cmd("ap_config_commit", NULL, cmd_ap_config_commit);
	/* TODO: ap_reset_default */
	sigma_dut_reg_cmd("ap_get_info", NULL, cmd_ap_get_info);
	sigma_dut_reg_cmd("ap_deauth_sta", NULL, cmd_ap_deauth_sta);
	sigma_dut_reg_cmd("ap_send_frame", NULL, cmd_ap_send_frame);
	sigma_dut_reg_cmd("ap_get_mac_address", NULL, cmd_ap_get_mac_address);
	sigma_dut_reg_cmd("AccessPoint", NULL, cmd_accesspoint);
}
