/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2010, Atheros Communications, Inc.
 */

#include "sigma_dut.h"
#include "wpa_helpers.h"


static int cmd_sta_atheros(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd)
{
	char buf[2048], *pos;
	int i;
	const char *intf, *c;
	char resp[200];

	intf = get_param(cmd, "interface");
	c = get_param(cmd, "cmd");
	if (c == NULL)
		return -1;

	buf[0] = '\0';
	if (strncmp(c, "ctrl=", 5) == 0) {
		size_t rlen;
		c += 5;
		if (wpa_command_resp(intf, c, buf, sizeof(buf)) < 0)
			return -2;
		rlen = strlen(buf);
		if (rlen > 0 && buf[rlen - 1] == '\n')
			buf[rlen - 1] = '\0';
	} else if (strncmp(c, "timeout=", 8) == 0) {
		unsigned int timeout;
		timeout = atoi(c + 8);
		if (timeout == 0)
			return -1;
		dut->default_timeout = timeout;
		sigma_dut_print( DUT_MSG_INFO, "Set DUT default timeout "
				"to %u seconds", dut->default_timeout);
		snprintf(buf, sizeof(buf), "OK");
	} else
		return -2;

	i = snprintf(resp, sizeof(resp), "resp,");
	pos = buf;
	while (*pos && i + 1 < sizeof(resp)) {
		char c = *pos++;
		if (c == '\n' || c == '\r' || c == ',')
			c = '^';
		resp[i++] = c;
	}
	resp[i] = '\0';

	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return 0;
}


static int req_intf(struct sigma_cmd *cmd)
{
	return get_param(cmd, "interface") == NULL ? -1 : 0;
}


void atheros_register_cmds(void)
{
	sigma_dut_reg_cmd("sta_atheros", req_intf, cmd_sta_atheros);
}
