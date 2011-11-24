/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2010, Atheros Communications, Inc.
 */

#include "sigma_dut.h"
#ifdef __linux__
#include <sys/stat.h>
#endif /* __linux__ */
#include "wpa_helpers.h"


#if 1 // by bbelief
#include "wfa_p2p.h"
#endif


static int cmd_ca_get_version(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	const char *info;

	info = get_param(cmd, "TestInfo");
	if (info) {
		char buf[200];
		snprintf(buf, sizeof(buf), "NOTE CAPI:TestInfo:%s", info);
		wpa_command(get_main_ifname(), buf);
	}

	send_resp(dut, conn, SIGMA_COMPLETE, "version,1.0");
	return 0;
}


int cmd_device_get_info(struct sigma_dut *dut, dutCommand_t *command,
                        		      dutCmdResponse_t *resp)
{
	const char *model = "N/A";
	char local_resp[200];

#ifdef __APPLE__
	model = "P2P (Mac)";
#endif /* __APPLE__ */
#ifdef __linux__
	{
		char path[128];
		struct stat s;
		snprintf(path, sizeof(path), "/sys/class/net/%s/phy80211",
			 get_main_ifname());
		if (stat(path, &s) == 0)
			model = "P2P (Linux/mac80211)";
		else
			model = "P2P (Linux)";
	}
#endif /* __linux__ */

	/* TODO: get version from wpa_supplicant (+ driver via wpa_s) */
	snprintf(local_resp, sizeof(local_resp), "vendor,Atheros Communications,"
		 "model,%s,version,TODO", model);


       strcpy(resp->cmdru.info/*[0]*/, local_resp);
	//send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return 0;
}


static int check_device_list_interfaces(struct sigma_cmd *cmd)
{
	if (get_param(cmd, "interfaceType") == NULL)
		return -1;
	return 0;
}


static int cmd_device_list_interfaces(struct sigma_dut *dut,
				      struct sigma_conn *conn,
				      struct sigma_cmd *cmd)
{
	const char *type;
	char resp[200];

	type = get_param(cmd, "interfaceType");
	sigma_dut_print( DUT_MSG_DEBUG, "device_list_interfaces - "
			"interfaceType=%s", type);
	if (strcmp(type, "802.11") != 0)
		return -2;

	snprintf(resp, sizeof(resp), "interfaceType,802.11,"
		 "interfaceID,%s", get_main_ifname());
	send_resp(dut, conn, SIGMA_COMPLETE, resp);

	return 0;
}


void basic_register_cmds(void)
{
#if 0
	sigma_dut_reg_cmd("ca_get_version", NULL, cmd_ca_get_version);
	sigma_dut_reg_cmd("device_get_info", NULL, cmd_device_get_info);
	sigma_dut_reg_cmd("device_list_interfaces",
			  check_device_list_interfaces,
			  cmd_device_list_interfaces);
#endif    
}
