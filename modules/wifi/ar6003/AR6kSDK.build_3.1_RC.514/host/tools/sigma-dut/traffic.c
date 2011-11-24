/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2010, Atheros Communications, Inc.
 */

#include "sigma_dut.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <ctype.h>

#if 1 // by bbelief
#include "wfa_p2p.h"
#endif



int is_ip_addr(const char *str);


int cmd_traffic_send_ping(struct sigma_dut *dut,
                                        tgPingStart_t *staPing, 
                                        dutCmdResponse_t *spresp)
{
	const char *dst;
	int val, size, dur, pkts;
	int id;
	char resp[100];
	float interval, rate;
	FILE *f;
	char buf[100];

	dst = staPing->dipaddr;
	if (strcmp(dst, "")  == 0 || !is_ip_addr(dst))
		return -1;


	//val = get_param(cmd, "frameSize");
	val = staPing->frameSize;
	if (val == 0)
		return -1;
	size = val; //atoi(val);


	val = staPing->frameRate;
	if (val == 0)
		return -1;

	rate = val; // atof(val);

#if 0
	if (rate < 1) {
        return -1;
    }
#endif

	val = staPing->duration;
	if (val == 0)
		return -1;
	dur = val; // atoi(val);
	if (dur <= 0)
		dur = 3600;

	pkts = dur * rate;
	interval = (float) 1 / rate;

	id = dut->next_streamid++;
	snprintf(buf, sizeof(buf), "/tmp/sigma_dut-ping.%d", id);
	unlink(buf);
	snprintf(buf, sizeof(buf), "/tmp/sigma_dut-ping-pid.%d", id);
	unlink(buf);

	sigma_dut_print( DUT_MSG_DEBUG, "Send ping: pkts=%d interval=%f "
			"streamid=%d",
			pkts, interval, id);


	f = fopen("/tmp/sigma_dut-ping.sh", "w");
	if (f == NULL)
		return -2;

	fprintf(f, "#!/bin/sh\n"
		"ping -c %d -i %f -s %d -q %s > /tmp/sigma_dut-ping.%d &\n"
		"echo $! > /tmp/sigma_dut-ping-pid.%d\n",
		pkts, interval, size, dst, id, id);
	fclose(f);
	if (chmod("/tmp/sigma_dut-ping.sh", S_IRUSR | S_IWUSR | S_IXUSR) < 0)
		return -2;

	if (system("/tmp/sigma_dut-ping.sh") != 0) {
		sigma_dut_print( DUT_MSG_ERROR, "Failed to start ping");
		return -2;
	}

	unlink("/tmp/sigma_dut-ping.sh");

	snprintf(resp, sizeof(resp), "streamID,%d", id);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return 0;
}


int cmd_traffic_stop_ping(struct sigma_dut *dut,
				 int      streamID,
				 dutCmdResponse_t *spresp)
{
	int val;
	int id, pid;
	FILE *f;
	char buf[100];
	int res_found = 0, sent = 0, received = 0;

	val = streamID;
	if (val < 0)
		return -1;
	id = val;

	snprintf(buf, sizeof(buf), "/tmp/sigma_dut-ping-pid.%d", id);
	f = fopen(buf, "r");
	if (f == NULL) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Unknown streamID");
		return -1;
	}
	if (fscanf(f, "%d", &pid) != 1 || pid <= 0) {
		sigma_dut_print( DUT_MSG_ERROR, "No PID for ping process");
		fclose(f);
		unlink(buf);
		return -2;
	}

	fclose(f);
	unlink(buf);

	sigma_dut_print( DUT_MSG_DEBUG, "Ping process pid %d", pid);
	if (kill(pid, SIGINT) < 0 && errno != ESRCH) {
		sigma_dut_print( DUT_MSG_DEBUG, "kill failed: %s",
				strerror(errno));
	}
	usleep(250000);

	snprintf(buf, sizeof(buf), "/tmp/sigma_dut-ping.%d", id);
	f = fopen(buf, "r");
	if (f == NULL) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,No ping result file found");
		return -1;
	}

	while (fgets(buf, sizeof(buf), f)) {
		char *pos;

		pos = strstr(buf, " packets transmitted");
		if (pos) {
			pos--;
			while (pos > buf && isdigit(pos[-1]))
				pos--;
			sent = atoi(pos);
			res_found = 1;
		}

		pos = strstr(buf, " received");
		if (pos) {
			pos--;
			while (pos > buf && isdigit(pos[-1]))
				pos--;
			received = atoi(pos);
			res_found = 1;
		}
	}
	fclose(f);
	snprintf(buf, sizeof(buf), "/tmp/sigma_dut-ping.%d", id);
	unlink(buf);

	if (!res_found) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,No ping results found");
		return -1;
	}

	snprintf(buf, sizeof(buf), "sent,%d,replies,%d", sent, received);
	send_resp(dut, conn, SIGMA_COMPLETE, buf);
	return 0;
}


void traffic_register_cmds(void)
{
#if 0
	sigma_dut_reg_cmd("traffic_send_ping", NULL, cmd_traffic_send_ping);
	sigma_dut_reg_cmd("traffic_stop_ping", NULL, cmd_traffic_stop_ping);
#endif    
}
