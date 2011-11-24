/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2010, Atheros Communications, Inc.
 */

#include "sigma_dut.h"


static int cmd_power_switch_ctrl(struct sigma_dut *dut,
				 struct sigma_conn *conn,
				 struct sigma_cmd *cmd)
{
	return 1;
}


static int cmd_power_switch_reset(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd)
{
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
	return 1;
}


static int cmd_powerswitch(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd)
{
	return 1;
}


void powerswitch_register_cmds(void)
{
	sigma_dut_reg_cmd("power_switch_ctrl", NULL, cmd_power_switch_ctrl);
	sigma_dut_reg_cmd("power_switch_reset", NULL, cmd_power_switch_reset);
	sigma_dut_reg_cmd("PowerSwitch", NULL, cmd_powerswitch);
}
