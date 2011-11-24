/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2010, Atheros Communications, Inc.
 */

#include "sigma_dut.h"
#include <sys/ioctl.h>
#include <sys/stat.h>
#ifdef __linux__
#include <netpacket/packet.h>
#include <linux/if_ether.h>
#endif /* __linux__ */
#include "wpa_helpers.h"

#define CERTIFICATES_PATH    "/etc/wpa_supplicant"


void disconnect_station(struct sigma_dut *dut);


int is_ip_addr(const char *str)
{
	const char *pos = str;
	struct in_addr addr;

	while (*pos) {
		if (*pos != '.' && (*pos < '0' || *pos > '9'))
			return 0;
		pos++;
	}

	return inet_aton(str, &addr);
}


int get_ip_config(struct sigma_dut *dut, const char *ifname, char *buf,
		  size_t buf_len)
{
	char tmp[256], *pos, *pos2;
	FILE *f;
	char ip[16], mask[15], dns[16], sec_dns[16];
	int is_dhcp = 0;
	int s;

	ip[0] = '\0';
	mask[0] = '\0';
	dns[0] = '\0';
	sec_dns[0] = '\0';

	s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s >= 0) {
		struct ifreq ifr;
		struct sockaddr_in saddr;

		memset(&ifr, 0, sizeof(ifr));
		strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
		if (ioctl(s, SIOCGIFADDR, &ifr) < 0) {
			sigma_dut_print( DUT_MSG_INFO, "Failed to get "
					"%s IP address: %s",
					ifname, strerror(errno));
		} else {
			memcpy(&saddr, &ifr.ifr_addr,
			       sizeof(struct sockaddr_in));
			strncpy(ip, inet_ntoa(saddr.sin_addr), sizeof(ip));
		}

		if (ioctl(s, SIOCGIFNETMASK, &ifr) == 0) {
			memcpy(&saddr, &ifr.ifr_addr,
			       sizeof(struct sockaddr_in));
			strncpy(mask, inet_ntoa(saddr.sin_addr), sizeof(mask));
		}
	}

#ifdef __linux__
	snprintf(tmp, sizeof(tmp), "ps ax | grep dhclient | grep -v grep | "
		 "grep -q %s", ifname);
	if (system(tmp) == 0)
		is_dhcp = 1;
	else {
		snprintf(tmp, sizeof(tmp), "ps ax | grep udhcpc | "
			 "grep -v grep | grep -q %s", ifname);
		if (system(tmp) == 0)
			is_dhcp = 1;
	}
#endif /* __linux__ */

	f = fopen("/etc/resolv.conf", "r");
	if (f) {
		while (fgets(tmp, sizeof(tmp), f)) {
			if (strncmp(tmp, "nameserver", 10) != 0)
				continue;
			pos = tmp + 10;
			while (*pos == ' ' || *pos == '\t')
				pos++;
			pos2 = pos;
			while (*pos2) {
				if (*pos2 == '\n' || *pos2 == '\r') {
					*pos2 = '\0';
					break;
				}
				pos2++;
			}
			if (!dns[0]) {
				strncpy(dns, pos, sizeof(dns));
				dns[sizeof(dns) - 1] = '\0';
			} else if (!sec_dns[0]) {
				strncpy(sec_dns, pos, sizeof(sec_dns));
				sec_dns[sizeof(sec_dns) - 1] = '\0';
			}
		}
		fclose(f);
	}

	snprintf(buf, buf_len, "dhcp,%d,ip,%s,mask,%s,primary-dns,%s",
		 is_dhcp, ip, mask, dns);
	buf[buf_len - 1] = '\0';
	if (sec_dns[0]) {
		snprintf(buf + strlen(buf), sizeof(buf_len) - strlen(buf),
			 ",secondary-dns,%s", sec_dns);
		buf[buf_len - 1] = '\0';
	}

	return 0;
}


static int cmd_sta_get_ip_config(struct sigma_dut *dut,
				 struct sigma_conn *conn,
				 struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname;
	char buf[200];
#if 0
	if (strcmp(intf, get_main_ifname()) == 0)
		ifname = get_station_ifname();
	else
		ifname = intf;
#else
        ifname = get_station_ifname();
#endif

	if (get_ip_config(dut, ifname, buf, sizeof(buf)) < 0)
		return -2;

	send_resp(dut, conn, SIGMA_COMPLETE, buf);
	return 0;
}


static int cmd_sta_set_ip_config(struct sigma_dut *dut,
				 struct sigma_conn *conn,
				 struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname;
	char buf[200];
	const char *val, *ip, *mask;
#if 0
	if (strcmp(intf, get_main_ifname()) == 0)
		ifname = get_station_ifname();
	else
		ifname = intf;
#else
        ifname = get_station_ifname();
#endif

	if (if_nametoindex(ifname) == 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Unknown interface");
		return 0;
	}

	val = get_param(cmd, "dhcp");
	if (val && (strcmp(val, "1") == 0 || strcasecmp(val, "true") == 0)) {
#ifdef __linux__
		char path[128];
		struct stat s;
		snprintf(path, sizeof(path), "/var/run/dhclient-%s.pid",
			 ifname);
		if (stat(path, &s) == 0) {
			snprintf(buf, sizeof(buf), "kill `cat %s`", path);
			sigma_dut_print( DUT_MSG_INFO,
					"Kill previous DHCP client: %s", buf);
			if (system(buf) != 0)
				sigma_dut_print( DUT_MSG_INFO,
						"Failed to kill DHCP client");
		}
		snprintf(buf, sizeof(buf),
			 "dhclient -nw -pf /var/run/dhclient-%s.pid %s",
			 ifname, ifname);
		sigma_dut_print( DUT_MSG_INFO, "Start DHCP client: %s",
				buf);
		if (system(buf) != 0) {
			sigma_dut_print( DUT_MSG_INFO,
					"Failed to start DHCP client");
			return -2;
		}
		return 1;
#endif /* __linux__ */
		return -2;
	}

	ip = get_param(cmd, "ip");
	mask = get_param(cmd, "mask");
	if (ip == NULL || !is_ip_addr(ip) ||
	    mask == NULL || !is_ip_addr(mask))
		return -1;

	snprintf(buf, sizeof(buf), "ifconfig %s %s netmask %s",
		 ifname, ip, mask);
	if (system(buf) != 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Failed to set IP "
			  "address");
		return 0;
	}

	val = get_param(cmd, "defaultGateway");
	if (val) {
		if (!is_ip_addr(val))
			return -1;
		snprintf(buf, sizeof(buf), "route add default gw %s", val);
		if (system(buf) != 0) {
			send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Failed "
				  "to set default gateway");
			return 0;
		}
	}

	val = get_param(cmd, "primary-dns");
	if (val) {
		/* TODO */
		sigma_dut_print( DUT_MSG_INFO, "Ignored primary-dns %s "
				"setting", val);
	}

	val = get_param(cmd, "secondary-dns");
	if (val) {
		/* TODO */
		sigma_dut_print( DUT_MSG_INFO, "Ignored secondary-dns %s "
				"setting", val);
	}

	return 1;
}


static int cmd_sta_get_info(struct sigma_dut *dut, struct sigma_conn *conn,
			    struct sigma_cmd *cmd)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	/* TODO: could report more details here */
	send_resp(dut, conn, SIGMA_COMPLETE, "vendor,Atheros");
	return 0;
}


static int cmd_sta_get_mac_address(struct sigma_dut *dut,
				   struct sigma_conn *conn,
				   struct sigma_cmd *cmd)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	char addr[20], resp[50];

	if (get_wpa_status(get_station_ifname(), "address", addr, sizeof(addr))
	    < 0)
		return -2;

	snprintf(resp, sizeof(resp), "mac,%s", addr);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return 0;
}


static int cmd_sta_is_connected(struct sigma_dut *dut, struct sigma_conn *conn,
				struct sigma_cmd *cmd)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	int connected = 0;
#ifdef __APPLE__
	int res;
	char buf[100];

	snprintf(buf, sizeof(buf), "apple80211 en1 --assoc_status | "
		 "grep \"last association status: 0 \"");
	sigma_dut_print( DUT_MSG_DEBUG, "%s: Running '%s'",
			__func__, buf);
	res = system(buf);
	sigma_dut_print( DUT_MSG_DEBUG, "system -> %d\n", res);
	if (res == 0)
		connected = 1;
#else /* __APPLE__ */
	char result[32];
	if (get_wpa_status(get_station_ifname(), "wpa_state", result,
			   sizeof(result)) < 0) {
		sigma_dut_print( DUT_MSG_INFO, "Could not get interface "
				"%s status", get_station_ifname());
		return -2;
	}

	sigma_dut_print( DUT_MSG_DEBUG, "wpa_state=%s", result);
	if (strncmp(result, "COMPLETED", 9) == 0)
		connected = 1;
#endif /* __APPLE__ */

	if (connected)
		send_resp(dut, conn, SIGMA_COMPLETE, "connected,1");
	else
		send_resp(dut, conn, SIGMA_COMPLETE, "connected,0");

	return 0;
}


static int cmd_sta_verify_ip_connection(struct sigma_dut *dut,
					struct sigma_conn *conn,
					struct sigma_cmd *cmd)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	const char *dst, *timeout;
	int wait_time = 90;
	char buf[100];
	int res;

	dst = get_param(cmd, "destination");
	if (dst == NULL || !is_ip_addr(dst))
		return -1;

	timeout = get_param(cmd, "timeout");
	if (timeout) {
		wait_time = atoi(timeout);
		if (wait_time < 1)
			wait_time = 1;
	}

	/* TODO: force renewal of IP lease if DHCP is enabled */

	snprintf(buf, sizeof(buf), "ping %s -c 3 -W %d", dst, wait_time);
	res = system(buf);
	sigma_dut_print( DUT_MSG_DEBUG, "ping returned: %d", res);
	if (res == 0)
		send_resp(dut, conn, SIGMA_COMPLETE, "connected,1");
	else if (res == 256)
		send_resp(dut, conn, SIGMA_COMPLETE, "connected,0");
	else
		return -2;

	return 0;
}


static int cmd_sta_get_bssid(struct sigma_dut *dut, struct sigma_conn *conn,
			     struct sigma_cmd *cmd)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	char bssid[20], resp[50];

	if (get_wpa_status(get_station_ifname(), "bssid", bssid, sizeof(bssid))
	    < 0)
		strncpy(bssid, "00:00:00:00:00:00", sizeof(bssid));

	snprintf(resp, sizeof(resp), "bssid,%s", bssid);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return 0;
}


static int add_network_common(struct sigma_dut *dut, struct sigma_conn *conn,
			      const char *ifname, struct sigma_cmd *cmd)
{
	const char *ssid = get_param(cmd, "ssid");
	int id;

	if (ssid == NULL)
		return -1;

	start_sta_mode(dut);

	id = add_network(ifname);
	if (id < 0)
		return -2;
	sigma_dut_print( DUT_MSG_DEBUG, "Adding network %d", id);

	if (set_network_quoted(ifname, id, "ssid", ssid) < 0)
		return -2;

	dut->infra_network_id = id;
	snprintf(dut->infra_ssid, sizeof(dut->infra_ssid), "%s", ssid);

	return id;
}


static int cmd_sta_set_encryption(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ssid = get_param(cmd, "ssid");
	const char *type = get_param(cmd, "encpType");
	const char *ifname;
	char buf[200];
	int id;

	if (ssid == NULL)
		return -1;

#if 0
	if (strcmp(intf, get_main_ifname()) == 0)
		ifname = get_station_ifname();
	else
		ifname = intf;
#else
        ifname = get_station_ifname();
#endif

	id = add_network_common(dut, conn, ifname, cmd);
	if (id < 0)
		return id;

	if (set_network(ifname, id, "key_mgmt", "NONE") < 0)
		return -2;

	if (type && strcasecmp(type, "wep") == 0) {
		const char *val;
		int i;

		val = get_param(cmd, "activeKey");
		if (val) {
			int keyid;
			keyid = atoi(val);
			if (keyid < 1 || keyid > 4)
				return -1;
			snprintf(buf, sizeof(buf), "%d", keyid - 1);
			if (set_network(ifname, id, "wep_tx_keyidx", buf) < 0)
				return -2;
		}

		for (i = 0; i < 4; i++) {
			snprintf(buf, sizeof(buf), "key%d", i + 1);
			val = get_param(cmd, buf);
			if (val == NULL)
				continue;
			snprintf(buf, sizeof(buf), "wep_key%d", i);
			if (set_network(ifname, id, buf, val) < 0)
				return -2;
		}
	}

	return 1;
}


static int set_wpa_common(struct sigma_dut *dut, struct sigma_conn *conn,
			  const char *ifname, struct sigma_cmd *cmd)
{
	const char *val;
	int id;

	id = add_network_common(dut, conn, ifname, cmd);
	if (id < 0)
		return id;

#ifdef __APPLE__
	/* TODO: Figure out how to set passphrase for native supplicant */
	send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Command not supported");
	return -2;
#endif /* __APPLE__ */

	val = get_param(cmd, "keyMgmtType");
	if (val == NULL)
		return -1;
	if (strcasecmp(val, "wpa") == 0) {
		if (set_network(ifname, id, "proto", "WPA") < 0)
			return -2;
	} else if (strcasecmp(val, "wpa2") == 0 ||
		   strcasecmp(val, "wpa2-sha256") == 0) {
		if (set_network(ifname, id, "proto", "WPA2") < 0)
			return -2;
	} else
		return -1;

	val = get_param(cmd, "encpType");
	if (val == NULL)
		return -1;
	if (strcasecmp(val, "tkip") == 0) {
		if (set_network(ifname, id, "pairwise", "TKIP") < 0)
			return -2;
	} else if (strcasecmp(val, "aes-ccmp") == 0) {
		if (set_network(ifname, id, "pairwise", "CCMP") < 0)
			return -2;
	} else
		return -1;

	dut->sta_pmf = STA_PMF_DISABLED;
	val = get_param(cmd, "PMF");
	if (val) {
		if (strcasecmp(val, "Required") == 0) {
			dut->sta_pmf = STA_PMF_REQUIRED;
			if (set_network(ifname, id, "ieee80211w", "2") < 0)
				return -2;
		} else if (strcasecmp(val, "Optional") == 0) {
			dut->sta_pmf = STA_PMF_OPTIONAL;
			if (set_network(ifname, id, "ieee80211w", "1") < 0)
				return -2;
		} else if (strcasecmp(val, "Disabled") == 0) {
			dut->sta_pmf = STA_PMF_DISABLED;
		} else
			return -1;
	}

	return id;
}


static int cmd_sta_set_psk(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname, *val;
	int id;

#if 0
	if (strcmp(intf, get_main_ifname()) == 0)
		ifname = get_station_ifname();
	else
		ifname = intf;
#else
        ifname = get_station_ifname();
#endif

	id = set_wpa_common(dut, conn, ifname, cmd);
	if (id < 0)
		return id;

	val = get_param(cmd, "keyMgmtType");
	if ((val && strcasecmp(val, "wpa2-sha256") == 0) ||
	    dut->sta_pmf == STA_PMF_REQUIRED) {
		if (set_network(ifname, id, "key_mgmt", "WPA-PSK-SHA256") < 0)
			return -2;
	} else if (dut->sta_pmf == STA_PMF_OPTIONAL) {
		if (set_network(ifname, id, "key_mgmt",
				"WPA-PSK WPA-PSK-SHA256") < 0)
			return -2;
	} else {
		if (set_network(ifname, id, "key_mgmt", "WPA-PSK") < 0)
			return -2;
	}

	val = get_param(cmd, "passPhrase");
	if (val == NULL)
		return -1;
	if (set_network_quoted(ifname, id, "psk", val) < 0)
		return -2;

	return 1;
}


static int set_eap_common(struct sigma_dut *dut, struct sigma_conn *conn,
			  const char *ifname, struct sigma_cmd *cmd)
{
	const char *val;
	int id;
	char buf[200];

	id = set_wpa_common(dut, conn, ifname, cmd);
	if (id < 0)
		return id;

	val = get_param(cmd, "keyMgmtType");
	if ((val && strcasecmp(val, "wpa2-sha256") == 0) ||
	    dut->sta_pmf == STA_PMF_REQUIRED) {
		if (set_network(ifname, id, "key_mgmt", "WPA-EAP-SHA256") < 0)
			return -2;
	} else if (dut->sta_pmf == STA_PMF_OPTIONAL) {
		if (set_network(ifname, id, "key_mgmt",
				"WPA-EAP WPA-EAP-SHA256") < 0)
			return -2;
	} else {
		if (set_network(ifname, id, "key_mgmt", "WPA-EAP") < 0)
			return -2;
	}

	val = get_param(cmd, "trustedRootCA");
	if (val) {
		snprintf(buf, sizeof(buf), CERTIFICATES_PATH "/%s", val);
		if (set_network_quoted(ifname, id, "ca_cert", buf) < 0)
			return -2;
	}

	val = get_param(cmd, "username");
	if (val) {
		if (set_network_quoted(ifname, id, "identity", val) < 0)
			return -2;
	}

	val = get_param(cmd, "password");
	if (val) {
		if (set_network_quoted(ifname, id, "password", val) < 0)
			return -2;
	}

	return id;
}


static int cmd_sta_set_eaptls(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname, *val;
	int id;
	char buf[200];

#if 0
	if (strcmp(intf, get_main_ifname()) == 0)
		ifname = get_station_ifname();
	else
		ifname = intf;
#else
        ifname = get_station_ifname();
#endif

	id = set_eap_common(dut, conn, ifname, cmd);
	if (id < 0)
		return id;

	if (set_network(ifname, id, "eap", "TLS") < 0)
		return -2;

	if (set_network_quoted(ifname, id, "identity",
			       "wifi-user@wifilabs.local") < 0)
		return -2;

	val = get_param(cmd, "clientCertificate");
	if (val == NULL)
		return -1;
	snprintf(buf, sizeof(buf), CERTIFICATES_PATH "/%s", val);
	if (set_network_quoted(ifname, id, "private_key", buf) < 0)
		return -2;

	if (set_network_quoted(ifname, id, "private_key_passwd", "wifi") < 0)
		return -2;

	return 1;
}


static int cmd_sta_set_eapttls(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname;
	int id;
#if 0
	if (strcmp(intf, get_main_ifname()) == 0)
		ifname = get_station_ifname();
	else
		ifname = intf;
#else
        ifname = get_station_ifname();
#endif


	id = set_eap_common(dut, conn, ifname, cmd);
	if (id < 0)
		return id;

	if (set_network(ifname, id, "eap", "TTLS") < 0)
		return -2;

	if (set_network_quoted(ifname, id, "phase2", "auth=MSCHAPV2") < 0)
		return -2;

	return 1;
}


static int cmd_sta_set_eapsim(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname;
	int id;
#if 0
	if (strcmp(intf, get_main_ifname()) == 0)
		ifname = get_station_ifname();
	else
		ifname = intf;
#else
        ifname = get_station_ifname();
#endif


	id = set_eap_common(dut, conn, ifname, cmd);
	if (id < 0)
		return id;

	if (set_network(ifname, id, "eap", "SIM") < 0)
		return -2;

	return 1;
}


static int cmd_sta_set_eappeap(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname, *val;
	int id;
	char buf[100];

#if 0
	if (strcmp(intf, get_main_ifname()) == 0)
		ifname = get_station_ifname();
	else
		ifname = intf;
#else
        ifname = get_station_ifname();
#endif

	id = set_eap_common(dut, conn, ifname, cmd);
	if (id < 0)
		return id;

	if (set_network(ifname, id, "eap", "PEAP") < 0)
		return -2;

	if (set_network_quoted(ifname, id, "anonymous_identity", "anonymous") <
	    0)
		return -2;

	val = get_param(cmd, "innerEAP");
	if (val) {
		if (strcasecmp(val, "MSCHAPv2") == 0) {
			if (set_network_quoted(ifname, id, "phase2",
					       "auth=MSCHAPV2") < 0)
				return -2;
		} else if (strcasecmp(val, "GTC") == 0) {
			if (set_network_quoted(ifname, id, "phase2",
					       "auth=GTC") < 0)
				return -2;
		} else
			return -1;
	}

	val = get_param(cmd, "peapVersion");
	if (val) {
		int ver = atoi(val);
		if (ver < 0 || ver > 1)
			return -1;
		snprintf(buf, sizeof(buf), "peapver=%d", ver);
		if (set_network_quoted(ifname, id, "phase1", buf) < 0)
			return -2;
	}

	return 1;
}


static int cmd_sta_set_eapfast(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname, *val;
	int id;
	char buf[100];

#if 0
	if (strcmp(intf, get_main_ifname()) == 0)
		ifname = get_station_ifname();
	else
		ifname = intf;
#else
        ifname = get_station_ifname();
#endif

	id = set_eap_common(dut, conn, ifname, cmd);
	if (id < 0)
		return id;

	if (set_network(ifname, id, "eap", "FAST") < 0)
		return -2;

	val = get_param(cmd, "innerEAP");
	if (val) {
		if (strcasecmp(val, "MSCHAPV2") == 0) {
			if (set_network_quoted(ifname, id, "phase2",
					       "auth=MSCHAPV2") < 0)
				return -2;
		} else if (strcasecmp(val, "GTC") == 0) {
			if (set_network_quoted(ifname, id, "phase2",
					       "auth=GTC") < 0)
				return -2;
		} else
			return -1;
	}

	val = get_param(cmd, "validateServer");
	if (val) {
		/* TODO */
		sigma_dut_print( DUT_MSG_INFO, "Ignored EAP-FAST "
				"validateServer=%s", val);
	}

	val = get_param(cmd, "pacFile");
	if (val) {
		snprintf(buf, sizeof(buf), "blob://%s", val);
		if (set_network_quoted(ifname, id, "pac_file", buf) < 0)
			return -2;
	}

	return 1;
}


static int cmd_sta_set_eapaka(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname;
	int id;


#if 0
	if (strcmp(intf, get_main_ifname()) == 0)
		ifname = get_station_ifname();
	else
		ifname = intf;
#else
        ifname = get_station_ifname();
#endif

	id = set_eap_common(dut, conn, ifname, cmd);
	if (id < 0)
		return id;

	if (set_network(ifname, id, "eap", "AKA") < 0)
		return -2;

	return 1;
}


static int cmd_sta_set_uapsd(struct sigma_dut *dut, struct sigma_conn *conn,
			     struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	/* const char *ssid = get_param(cmd, "ssid"); */
	const char *val;
	int max_sp_len = 4;
	int ac_be = 1, ac_bk = 1, ac_vi = 1, ac_vo = 1;
	char buf[100];

	val = get_param(cmd, "maxSPLength");
	if (val) {
		max_sp_len = atoi(val);
		if (max_sp_len != 0 && max_sp_len != 1 && max_sp_len != 2 &&
		    max_sp_len != 4)
			return -1;
	}

	val = get_param(cmd, "acBE");
	if (val)
		ac_be = atoi(val);

	val = get_param(cmd, "acBK");
	if (val)
		ac_bk = atoi(val);

	val = get_param(cmd, "acVI");
	if (val)
		ac_vi = atoi(val);

	val = get_param(cmd, "acVO");
	if (val)
		ac_vo = atoi(val);

	dut->client_uapsd = ac_be || ac_bk || ac_vi || ac_vo;

	snprintf(buf, sizeof(buf), "P2P_SET client_apsd %d,%d,%d,%d;%d",
		 ac_be, ac_bk, ac_vi, ac_vo, max_sp_len);
	if (wpa_command(intf, buf)) {
		sigma_dut_print( DUT_MSG_INFO, "Failed to set client mode "
				"UAPSD parameters.");
		return -2;
	}

	return 1;
}


static int cmd_sta_associate(struct sigma_dut *dut, struct sigma_conn *conn,
			     struct sigma_cmd *cmd)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	const char *ssid = get_param(cmd, "ssid");
	const char *wps_param = get_param(cmd, "WPS");
	int wps = 0;
	char buf[100];

	if (ssid == NULL)
		return -1;

	if (wps_param &&
	    (strcmp(wps_param, "1") == 0 || strcasecmp(wps_param, "On") == 0))
		wps = 1;

#ifdef __APPLE__
	if (wps) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,WPS connection "
			  "not yet supported");
		return 0;
	}

	snprintf(buf, sizeof(buf), "apple80211 en1 --ssid=\"%s\"", ssid);
	sigma_dut_print( DUT_MSG_DEBUG, "%s: Running '%s'",
			__func__, buf);
	system(buf);
#else /* __APPLE__ */
	if (wps) {
		if (dut->wps_method == WFA_CS_WPS_NOT_READY) {
			send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,WPS "
				  "parameters not yet set");
			return 0;
		}
		if (dut->wps_method == WFA_CS_WPS_PBC) {
			if (wpa_command(get_station_ifname(), "WPS_PBC") < 0)
				return -2;
		} else {
			snprintf(buf, sizeof(buf), "WPS_PIN any %s",
				 dut->wps_pin);
			if (wpa_command(get_station_ifname(), buf) < 0)
				return -2;
		}
	} else {
		if (strcmp(ssid, dut->infra_ssid) != 0) {
			printf("No network parameters known for network "
			       "(ssid='%s')", ssid);
			send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,"
				  "No network parameters known for network");
			return 0;
		}

		snprintf(buf, sizeof(buf), "SELECT_NETWORK %d",
			 dut->infra_network_id);
		if (wpa_command(get_station_ifname(), buf) < 0) {
			sigma_dut_print( DUT_MSG_INFO, "Failed to select "
					"network id %d on %s",
					dut->infra_network_id,
					get_station_ifname());
			return -2;
		}
	}
#endif /* __APPLE__ */

	return 1;
}


static int cmd_sta_preset_testparameters(struct sigma_dut *dut,
					 struct sigma_conn *conn,
					 struct sigma_cmd *cmd)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	const char *val;

#if 0
	val = get_param(cmd, "Supplicant");
	if (val && strcasecmp(val, "Default") != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Only default(Vendor) supplicant "
			  "supported");
		return 0;
	}
#endif

#if 0
	val = get_param(cmd, "RTS");
	if (val) {
		/* TODO */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Setting RTS not supported");
		return 0;
	}
#endif

#if 0
	val = get_param(cmd, "FRGMNT");
	if (val) {
		/* TODO */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Setting FRGMNT not supported");
		return 0;
	}
#endif

#if 0
	val = get_param(cmd, "Preamble");
	if (val) {
		/* TODO: Long/Short */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Setting Preamble not supported");
		return 0;
	}
#endif

#if 0
	val = get_param(cmd, "Mode");
	if (val) {
		/* TODO */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Setting Mode not supported");
		return 0;
	}
#endif

	val = get_param(cmd, "Powersave");
	if (val) {
		if (strcmp(val, "0") == 0 || strcasecmp(val, "off") == 0) {
			if (wpa_command(get_station_ifname(),
					"P2P_SET ps 0") < 0)
				return -2;
			/* Make sure test modes are disabled */
			wpa_command(get_station_ifname(), "P2P_SET ps 98");
			wpa_command(get_station_ifname(), "P2P_SET ps 96");
		} else if (strcmp(val, "1") == 0 ||
			   strcasecmp(val, "PSPoll") == 0 ||
			   strcasecmp(val, "on") == 0) {
			/* Disable default power save mode */
			wpa_command(get_station_ifname(), "P2P_SET ps 0");
			/* Enable PS-Poll test mode */
			if (wpa_command(get_station_ifname(),
					"P2P_SET ps 97") < 0 ||
			    wpa_command(get_station_ifname(),
					"P2P_SET ps 99") < 0)
				return -2;
		} else if (strcmp(val, "2") == 0 ||
			   strcasecmp(val, "Fast") == 0) {
			/* TODO */
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Powersave=Fast not supported");
			return 0;
		} else if (strcmp(val, "3") == 0 ||
			   strcasecmp(val, "PSNonPoll") == 0) {
			/* Make sure test modes are disabled */
			wpa_command(get_station_ifname(), "P2P_SET ps 98");
			wpa_command(get_station_ifname(), "P2P_SET ps 96");

			/* Enable default power save mode */
			if (wpa_command(get_station_ifname(),
					"P2P_SET ps 1") < 0)
				return -2;
		} else
			return -1;
	}

	val = get_param(cmd, "NoAck");
	if (val) {
		/* TODO */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Setting NoAck not supported");
		return 0;
	}

	return 1;
}


static int cmd_sta_set_11n(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *val, *mcs32, *rate;
	int ampdu = -1;
	char buf[30];

	val = get_param(cmd, "40_INTOLERANT");
	if (val) {
		if (strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0) {
			/* TODO: iwpriv ht40intol through wpa_supplicant */
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,40_INTOLERANT not supported");
			return 0;
		}
	}

	val = get_param(cmd, "ADDBA_REJECT");
	if (val) {
		if (strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0) {
			/* reject any ADDBA with status "decline" */
			ampdu = 0;
		} else {
			/* accept ADDBA */
			ampdu = 1;
		}
	}

	val = get_param(cmd, "AMPDU");
	if (val) {
		if (strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0) {
			/* enable AMPDU Aggregation */
			if (ampdu == 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Mismatch in "
					  "addba_reject/ampdu - "
					  "not supported");
				return 0;
			} else
				ampdu = 1;
		} else {
			/* disable AMPDU Aggregation */
			if (ampdu == 1) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Mismatch in "
					  "addba_reject/ampdu - "
					  "not supported");
				return 0;
			} else
				ampdu = 0;
		}
	}

	if (ampdu >= 0) {
		sigma_dut_print( DUT_MSG_DEBUG, "%s A-MPDU aggregation",
				ampdu ? "Enabling" : "Disabling");
		snprintf(buf, sizeof(buf), "SET ampdu %d", ampdu);
		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,set aggr failed");
			return 0;
		}
	}

	val = get_param(cmd, "AMSDU");
	if (val) {
		if (strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0) {
			/* Enable AMSDU Aggregation */
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,AMSDU aggregation not supported");
			return 0;
		}
	}

	val = get_param(cmd, "GREENFIELD");
	if (val) {
		if (strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0) {
			/* Enable GD */
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,GF not supported");
			return 0;
		}
	}

	val = get_param(cmd, "SGI20");
	if (val) {
		/* TODO: Enable/disable SGI20 */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,SGI20 not supported");
		return 0;
	}

	val = get_param(cmd, "STBC_RX");
	if (val) {
		/* TODO */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,STBC_RX not supported");
		return 0;
	}

	val = get_param(cmd, "WIDTH");
	if (val) {
		if (strcasecmp(val, "Auto") != 0) {
			/* TODO: 20/40/Auto */
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,WIDTH not supported");
			return 0;
		}
	}

	mcs32 = get_param(cmd, "MCS32"); /* HT Duplicate Mode Enable/Disable */
	rate = get_param(cmd, "MCS_FIXEDRATE"); /* Fixed MCS rate (0..31) */
	
	if (mcs32 && rate) {
		/* TODO */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,MCS32,MCS_FIXEDRATE not supported");
		return 0;
	} else if (mcs32 && !rate) {
		/* TODO */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,MCS32 not supported");
		return 0;
	} else if (!mcs32 && rate) {
		/* TODO */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,MCS32_FIXEDRATE not supported");
		return 0;
	}

	val = get_param(cmd, "SMPS");
	if (val) {
		/* TODO: Dynamic/0, Static/1, No Limit/2 */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,SMPS not supported");
		return 0;
	}

	val = get_param(cmd, "TXSP_STREAM");
	if (val) {
		/* TODO: Tx Spacial Stream 1/2/3 */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,TXSP_STREAM not supported");
		return 0;
	}

	val = get_param(cmd, "RXSP_STREAM");
	if (val) {
		/* TODO: Rx Spacial Stream 1/2/3 */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,RXSP_STREAM not supported");
		return 0;
	}

	return 1;
}


static int cmd_sta_disconnect(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	disconnect_station(dut);
	return 1;
}


static int cmd_sta_reset_default(struct sigma_dut *dut,
				 struct sigma_conn *conn,
				 struct sigma_cmd *cmd)
{
	int cmd_sta_p2p_reset(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd);
	/* const char *intf = get_param(cmd, "Interface"); */
	/* const char *prog = get_param(cmd, "prog"); */
	return cmd_sta_p2p_reset(dut, conn, cmd);
}


#ifdef __linux__

int inject_frame(int s, const void *data, size_t len, int encrypt)
{
#define	IEEE80211_RADIOTAP_F_WEP	0x04
#define	IEEE80211_RADIOTAP_F_FRAG	0x08
	unsigned char rtap_hdr[] = {
		0x00, 0x00, /* radiotap version */
		0x0e, 0x00, /* radiotap length */
		0x02, 0xc0, 0x00, 0x00, /* bmap: flags, tx and rx flags */
		IEEE80211_RADIOTAP_F_FRAG, /* F_FRAG (fragment if required) */
		0x00,       /* padding */
		0x00, 0x00, /* RX and TX flags to indicate that */
		0x00, 0x00, /* this is the injected frame directly */
	};
	struct iovec iov[2] = {
		{
			.iov_base = &rtap_hdr,
			.iov_len = sizeof(rtap_hdr),
		},
		{
			.iov_base = (void *) data,
			.iov_len = len,
		}
	};
	struct msghdr msg = {
		.msg_name = NULL,
		.msg_namelen = 0,
		.msg_iov = iov,
		.msg_iovlen = 2,
		.msg_control = NULL,
		.msg_controllen = 0,
		.msg_flags = 0,
	};

	if (encrypt)
		rtap_hdr[8] |= IEEE80211_RADIOTAP_F_WEP;

	return sendmsg(s, &msg, 0);
}


int open_monitor(const char *ifname)
{
	struct sockaddr_ll ll;
	int s;

	memset(&ll, 0, sizeof(ll));
	ll.sll_family = AF_PACKET;
	ll.sll_ifindex = if_nametoindex(ifname);
	if (ll.sll_ifindex == 0) {
		perror("if_nametoindex");
		return -1;
	}
	s = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (s < 0) {
		perror("socket[PF_PACKET,SOCK_RAW]");
		return -1;
	}

	if (bind(s, (struct sockaddr *) &ll, sizeof(ll)) < 0) {
		perror("monitor socket bind");
		close(s);
		return -1;
	}

	return s;
}


static int hex2num(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}


int hwaddr_aton(const char *txt, unsigned char *addr)
{
	int i;

	for (i = 0; i < 6; i++) {
		int a, b;

		a = hex2num(*txt++);
		if (a < 0)
			return -1;
		b = hex2num(*txt++);
		if (b < 0)
			return -1;
		*addr++ = (a << 4) | b;
		if (i < 5 && *txt++ != ':')
			return -1;
	}

	return 0;
}

#endif /* __linux__ */

enum send_frame_type {
		DISASSOC, DEAUTH, SAQUERY, AUTH, ASSOCREQ, REASSOCREQ
};
enum send_frame_protection {
	CORRECT_KEY, INCORRECT_KEY, UNPROTECTED
};


static int sta_inject_frame(struct sigma_dut *dut, struct sigma_conn *conn,
			    enum send_frame_type frame,
			    enum send_frame_protection protected)
{
#ifdef __linux__
	unsigned char buf[1000], *pos;
	int s, res;
	char bssid[20], addr[20];
	char result[32], ssid[100];
	size_t ssid_len;

	if (get_wpa_status(get_station_ifname(), "wpa_state", result,
			   sizeof(result)) < 0 ||
	    strncmp(result, "COMPLETED", 9) != 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Not connected");
		return 0;
	}

	if (get_wpa_status(get_station_ifname(), "bssid", bssid, sizeof(bssid))
	    < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could not get "
			  "current BSSID");
		return 0;
	}

	if (get_wpa_status(get_station_ifname(), "address", addr, sizeof(addr))
	    < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could not get "
			  "own MAC address");
		return 0;
	}

	if (get_wpa_status(get_station_ifname(), "ssid", ssid, sizeof(ssid))
	    < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could not get "
			  "current SSID");
		return 0;
	}
	ssid_len = strlen(ssid);

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
	case AUTH:
		*pos++ = 0xb0;
		break;
	case ASSOCREQ:
		*pos++ = 0x00;
		break;
	case REASSOCREQ:
		*pos++ = 0x20;
		break;
	}

	if (protected == INCORRECT_KEY)
		*pos++ = 0x40; /* Set Protected field to 1 */
	else
		*pos++ = 0x00;

	/* Duration */
	*pos++ = 0x00;
	*pos++ = 0x00;

	/* addr1 = DA (current AP) */
	hwaddr_aton(bssid, pos);
	pos += 6;
	/* addr2 = SA (own address) */
	hwaddr_aton(addr, pos);
	pos += 6;
	/* addr3 = BSSID (current AP) */
	hwaddr_aton(bssid, pos);
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
		case AUTH:
			/* Auth Alg (Open) */
			*pos++ = 0x00;
			*pos++ = 0x00;
			/* Seq# */
			*pos++ = 0x01;
			*pos++ = 0x00;
			/* Status code */
			*pos++ = 0x00;
			*pos++ = 0x00;
			break;
		case ASSOCREQ:
			/* Capability Information */
			*pos++ = 0x31;
			*pos++ = 0x04;
			/* Listen Interval */
			*pos++ = 0x0a;
			*pos++ = 0x00;
			/* SSID */
			*pos++ = 0x00;
			*pos++ = ssid_len;
			memcpy(pos, ssid, ssid_len);
			pos += ssid_len;
			/* Supported Rates */
			memcpy(pos, "\x01\x08\x02\x04\x0b\x16\x0c\x12\x18\x24",
			       10);
			pos += 10;
			/* Extended Supported Rates */
			memcpy(pos, "\x32\x04\x30\x48\x60\x6c", 6);
			pos += 6;
			/* RSN */
			memcpy(pos, "\x30\x1a\x01\x00\x00\x0f\xac\x04\x01\x00"
			       "\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x06\xc0"
			       "\x00\x00\x00\x00\x0f\xac\x06", 28);
			pos += 28;
			break;
		case REASSOCREQ:
			/* Capability Information */
			*pos++ = 0x31;
			*pos++ = 0x04;
			/* Listen Interval */
			*pos++ = 0x0a;
			*pos++ = 0x00;
			/* Current AP */
			hwaddr_aton(bssid, pos);
			pos += 6;
			/* SSID */
			*pos++ = 0x00;
			*pos++ = ssid_len;
			memcpy(pos, ssid, ssid_len);
			pos += ssid_len;
			/* Supported Rates */
			memcpy(pos, "\x01\x08\x02\x04\x0b\x16\x0c\x12\x18\x24",
			       10);
			pos += 10;
			/* Extended Supported Rates */
			memcpy(pos, "\x32\x04\x30\x48\x60\x6c", 6);
			pos += 6;
			/* RSN */
			memcpy(pos, "\x30\x1a\x01\x00\x00\x0f\xac\x04\x01\x00"
			       "\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x06\xc0"
			       "\x00\x00\x00\x00\x0f\xac\x06", 28);
			pos += 28;
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
	send_resp(dut, conn, SIGMA_ERROR, "errorCode,sta_send_frame not "
		  "yet supported");
	return 0;
#endif /* __linux__ */
}


static int cmd_sta_send_frame_tdls(struct sigma_dut *dut,
				   struct sigma_conn *conn,
				   struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *sta, *val;
	unsigned char addr[ETH_ALEN];
	char buf[100];

	sta = get_param(cmd, "peer");
	if (sta == NULL)
		sta = get_param(cmd, "station");
	if (sta == NULL)
		return -1;
	if (hwaddr_aton(sta, addr) < 0)
		return -1;

	val = get_param(cmd, "type");
	if (val == NULL)
		return -1;

	if (strcasecmp(val, "DISCOVERY") == 0) {
		snprintf(buf, sizeof(buf), "TDLS_DISCOVER %s", sta);
		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to send TDLS discovery");
			return 0;
		}
		return 1;
	}

	if (strcasecmp(val, "SETUP") == 0) {
		snprintf(buf, sizeof(buf), "TDLS_SETUP %s", sta);
		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to send TDLS setup");
			return 0;
		}
		return 1;
	}

	if (strcasecmp(val, "TEARDOWN") == 0) {
		snprintf(buf, sizeof(buf), "TDLS_TEARDOWN %s", sta);
		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to send TDLS teardown");
			return 0;
		}
		return 1;
	}

	send_resp(dut, conn, SIGMA_ERROR,
		  "ErrorCode,Unsupported TDLS frame");
	return 0;
}


static int cmd_sta_send_frame(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *val;
	enum send_frame_type frame;
	enum send_frame_protection protected;
	char buf[100];
	unsigned char addr[ETH_ALEN];

	val = get_param(cmd, "frame");
	if (val && strcasecmp(val, "TDLS") == 0)
		return cmd_sta_send_frame_tdls(dut, conn, cmd);

	val = get_param(cmd, "TD_DISC");
	if (val) {
		if (hwaddr_aton(val, addr) < 0)
			return -1;
		snprintf(buf, sizeof(buf), "TDLS_DISCOVER %s", val);
		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to send TDLS discovery");
			return 0;
		}
		return 1;
	}

	val = get_param(cmd, "TD_Setup");
	if (val) {
		if (hwaddr_aton(val, addr) < 0)
			return -1;
		snprintf(buf, sizeof(buf), "TDLS_SETUP %s", val);
		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to start TDLS setup");
			return 0;
		}
		return 1;
	}

	val = get_param(cmd, "TD_TearDown");
	if (val) {
		if (hwaddr_aton(val, addr) < 0)
			return -1;
		snprintf(buf, sizeof(buf), "TDLS_TEARDOWN %s", val);
		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to tear down TDLS link");
			return 0;
		}
		return 1;
	}

	val = get_param(cmd, "TD_ChannelSwitch");
	if (val) {
		/* TODO */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,TD_ChannelSwitch not yet supported");
		return 0;
	}

	val = get_param(cmd, "TD_NF");
	if (val) {
		/* TODO */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,TD_NF not yet supported");
		return 0;
	}

	val = get_param(cmd, "PMFFrameType");
	if (val == NULL)
		val = get_param(cmd, "Type");
	if (val == NULL)
		return -1;
	if (strcasecmp(val, "disassoc") == 0)
		frame = DISASSOC;
	else if (strcasecmp(val, "deauth") == 0)
		frame = DEAUTH;
	else if (strcasecmp(val, "saquery") == 0)
		frame = SAQUERY;
	else if (strcasecmp(val, "auth") == 0)
		frame = AUTH;
	else if (strcasecmp(val, "assocreq") == 0)
		frame = ASSOCREQ;
	else if (strcasecmp(val, "reassocreq") == 0)
		frame = REASSOCREQ;
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

	if (protected != UNPROTECTED &&
	    (frame == AUTH || frame == ASSOCREQ || frame == REASSOCREQ)) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Impossible "
			  "PMFProtected for auth/assocreq/reassocreq");
		return 0;
	}

	if (if_nametoindex("sigmadut") == 0) {
		snprintf(buf, sizeof(buf),
			 "iw dev %s interface add sigmadut type monitor",
			 get_station_ifname());
		if (system(buf) != 0 ||
		    if_nametoindex("sigmadut") == 0) {
			sigma_dut_print( DUT_MSG_ERROR, "Failed to add "
					"monitor interface with '%s'", buf);
			return -2;
		}
	}

	if (system("ifconfig sigmadut up") != 0) {
		sigma_dut_print( DUT_MSG_ERROR, "Failed to set "
				"monitor interface up");
		return -2;
	}

	return sta_inject_frame(dut, conn, frame, protected);
}


static int cmd_sta_set_macaddr(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *mac = get_param(cmd, "MAC");

	if (intf == NULL || mac == NULL)
		return -1;

	sigma_dut_print( DUT_MSG_INFO, "Change local MAC address for "
			"interface %s to %s", intf, mac);

	/* TODO: add support for changing own MAC address */
	send_resp(dut, conn, SIGMA_ERROR, "errorCode,Unsupported "
		  "command");
	return 0;
}


static int req_intf(struct sigma_cmd *cmd)
{
	return get_param(cmd, "interface") == NULL ? -1 : 0;
}


void sta_register_cmds(void)
{
	sigma_dut_reg_cmd("sta_get_ip_config", req_intf,
			  cmd_sta_get_ip_config);
	sigma_dut_reg_cmd("sta_set_ip_config", req_intf,
			  cmd_sta_set_ip_config);
	sigma_dut_reg_cmd("sta_get_info", req_intf, cmd_sta_get_info);
	sigma_dut_reg_cmd("sta_get_mac_address", req_intf,
			  cmd_sta_get_mac_address);
	sigma_dut_reg_cmd("sta_is_connected", req_intf, cmd_sta_is_connected);
	sigma_dut_reg_cmd("sta_verify_ip_connection", req_intf,
			  cmd_sta_verify_ip_connection);
	sigma_dut_reg_cmd("sta_get_bssid", req_intf, cmd_sta_get_bssid);
	sigma_dut_reg_cmd("sta_set_encryption", req_intf,
			  cmd_sta_set_encryption);
	sigma_dut_reg_cmd("sta_set_psk", req_intf, cmd_sta_set_psk);
	sigma_dut_reg_cmd("sta_set_eaptls", req_intf, cmd_sta_set_eaptls);
	sigma_dut_reg_cmd("sta_set_eapttls", req_intf, cmd_sta_set_eapttls);
	sigma_dut_reg_cmd("sta_set_eapsim", req_intf, cmd_sta_set_eapsim);
	sigma_dut_reg_cmd("sta_set_eappeap", req_intf, cmd_sta_set_eappeap);
	sigma_dut_reg_cmd("sta_set_eapfast", req_intf, cmd_sta_set_eapfast);
	sigma_dut_reg_cmd("sta_set_eapaka", req_intf, cmd_sta_set_eapaka);
	sigma_dut_reg_cmd("sta_set_uapsd", req_intf, cmd_sta_set_uapsd);
	/* TODO: sta_set_ibss */
	/* TODO: sta_set_mode */
	/* TODO: sta_set_wmm */
	sigma_dut_reg_cmd("sta_associate", req_intf, cmd_sta_associate);
	/* TODO: sta_up_load */
	sigma_dut_reg_cmd("sta_preset_testparameters", req_intf,
			  cmd_sta_preset_testparameters);
	/* TODO: sta_set_system */
	sigma_dut_reg_cmd("sta_set_11n", req_intf, cmd_sta_set_11n);
	/* TODO: sta_set_rifs_test */
	/* TODO: sta_set_wireless */
	/* TODO: sta_send_addba */
	/* TODO: sta_send_coexist_mgmt */
	sigma_dut_reg_cmd("sta_disconnect", req_intf, cmd_sta_disconnect);
	/* TODO: sta_reassoc */
	sigma_dut_reg_cmd("sta_reset_default", req_intf,
			  cmd_sta_reset_default);
	sigma_dut_reg_cmd("sta_send_frame", req_intf, cmd_sta_send_frame);
	sigma_dut_reg_cmd("sta_set_macaddr", req_intf, cmd_sta_set_macaddr);
}
