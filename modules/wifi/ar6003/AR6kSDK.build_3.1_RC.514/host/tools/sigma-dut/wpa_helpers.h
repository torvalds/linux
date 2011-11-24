/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2010, Atheros Communications, Inc.
 */

#ifndef WPA_HELPERS_H
#define WPA_HELPERS_H

char * get_main_ifname(void);
char * get_station_ifname(void);
int wpa_command(const char *ifname, const char *cmd);
int wpa_command_resp(const char *ifname, const char *cmd,
		     char *resp, size_t resp_size);
int get_wpa_status(const char *ifname, const char *field, char *obuf,
		   size_t obuf_size);
void remove_wpa_networks(const char *ifname);

struct wpa_ctrl * open_wpa_mon(const char *ifname);
int wait_ip_addr(struct sigma_dut *dut, const char *ifname);
int get_wpa_cli_event(struct sigma_dut *dut, struct wpa_ctrl *mon,
		      const char *event, char *buf, size_t buf_size);
int get_wpa_cli_event2(struct sigma_dut *dut, struct wpa_ctrl *mon,
		       const char *event, const char *event2,
		       char *buf, size_t buf_size);

int add_network(const char *ifname);
int set_network(const char *ifname, int id, const char *field,
		const char *value);
int set_network_quoted(const char *ifname, int id, const char *field,
		       const char *value);

int start_sta_mode(struct sigma_dut *dut);
void stop_sta_mode(struct sigma_dut *dut);

#endif /* WPA_HELPERS_H */
