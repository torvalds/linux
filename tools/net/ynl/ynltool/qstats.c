// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <net/if.h>

#include <ynl.h>
#include "netdev-user.h"

#include "main.h"

static enum netdev_qstats_scope scope; /* default - device */

static void print_json_qstats(struct netdev_qstats_get_list *qstats)
{
	jsonw_start_array(json_wtr);

	ynl_dump_foreach(qstats, qs) {
		char ifname[IF_NAMESIZE];
		const char *name;

		jsonw_start_object(json_wtr);

		name = if_indextoname(qs->ifindex, ifname);
		if (name)
			jsonw_string_field(json_wtr, "ifname", name);
		jsonw_uint_field(json_wtr, "ifindex", qs->ifindex);

		if (qs->_present.queue_type)
			jsonw_string_field(json_wtr, "queue-type",
					   netdev_queue_type_str(qs->queue_type));
		if (qs->_present.queue_id)
			jsonw_uint_field(json_wtr, "queue-id", qs->queue_id);

		if (qs->_present.rx_packets || qs->_present.rx_bytes ||
		    qs->_present.rx_alloc_fail || qs->_present.rx_hw_drops ||
		    qs->_present.rx_csum_complete || qs->_present.rx_hw_gro_packets) {
			jsonw_name(json_wtr, "rx");
			jsonw_start_object(json_wtr);
			if (qs->_present.rx_packets)
				jsonw_uint_field(json_wtr, "packets", qs->rx_packets);
			if (qs->_present.rx_bytes)
				jsonw_uint_field(json_wtr, "bytes", qs->rx_bytes);
			if (qs->_present.rx_alloc_fail)
				jsonw_uint_field(json_wtr, "alloc-fail", qs->rx_alloc_fail);
			if (qs->_present.rx_hw_drops)
				jsonw_uint_field(json_wtr, "hw-drops", qs->rx_hw_drops);
			if (qs->_present.rx_hw_drop_overruns)
				jsonw_uint_field(json_wtr, "hw-drop-overruns", qs->rx_hw_drop_overruns);
			if (qs->_present.rx_hw_drop_ratelimits)
				jsonw_uint_field(json_wtr, "hw-drop-ratelimits", qs->rx_hw_drop_ratelimits);
			if (qs->_present.rx_csum_complete)
				jsonw_uint_field(json_wtr, "csum-complete", qs->rx_csum_complete);
			if (qs->_present.rx_csum_unnecessary)
				jsonw_uint_field(json_wtr, "csum-unnecessary", qs->rx_csum_unnecessary);
			if (qs->_present.rx_csum_none)
				jsonw_uint_field(json_wtr, "csum-none", qs->rx_csum_none);
			if (qs->_present.rx_csum_bad)
				jsonw_uint_field(json_wtr, "csum-bad", qs->rx_csum_bad);
			if (qs->_present.rx_hw_gro_packets)
				jsonw_uint_field(json_wtr, "hw-gro-packets", qs->rx_hw_gro_packets);
			if (qs->_present.rx_hw_gro_bytes)
				jsonw_uint_field(json_wtr, "hw-gro-bytes", qs->rx_hw_gro_bytes);
			if (qs->_present.rx_hw_gro_wire_packets)
				jsonw_uint_field(json_wtr, "hw-gro-wire-packets", qs->rx_hw_gro_wire_packets);
			if (qs->_present.rx_hw_gro_wire_bytes)
				jsonw_uint_field(json_wtr, "hw-gro-wire-bytes", qs->rx_hw_gro_wire_bytes);
			jsonw_end_object(json_wtr);
		}

		if (qs->_present.tx_packets || qs->_present.tx_bytes ||
		    qs->_present.tx_hw_drops || qs->_present.tx_csum_none ||
		    qs->_present.tx_hw_gso_packets) {
			jsonw_name(json_wtr, "tx");
			jsonw_start_object(json_wtr);
			if (qs->_present.tx_packets)
				jsonw_uint_field(json_wtr, "packets", qs->tx_packets);
			if (qs->_present.tx_bytes)
				jsonw_uint_field(json_wtr, "bytes", qs->tx_bytes);
			if (qs->_present.tx_hw_drops)
				jsonw_uint_field(json_wtr, "hw-drops", qs->tx_hw_drops);
			if (qs->_present.tx_hw_drop_errors)
				jsonw_uint_field(json_wtr, "hw-drop-errors", qs->tx_hw_drop_errors);
			if (qs->_present.tx_hw_drop_ratelimits)
				jsonw_uint_field(json_wtr, "hw-drop-ratelimits", qs->tx_hw_drop_ratelimits);
			if (qs->_present.tx_csum_none)
				jsonw_uint_field(json_wtr, "csum-none", qs->tx_csum_none);
			if (qs->_present.tx_needs_csum)
				jsonw_uint_field(json_wtr, "needs-csum", qs->tx_needs_csum);
			if (qs->_present.tx_hw_gso_packets)
				jsonw_uint_field(json_wtr, "hw-gso-packets", qs->tx_hw_gso_packets);
			if (qs->_present.tx_hw_gso_bytes)
				jsonw_uint_field(json_wtr, "hw-gso-bytes", qs->tx_hw_gso_bytes);
			if (qs->_present.tx_hw_gso_wire_packets)
				jsonw_uint_field(json_wtr, "hw-gso-wire-packets", qs->tx_hw_gso_wire_packets);
			if (qs->_present.tx_hw_gso_wire_bytes)
				jsonw_uint_field(json_wtr, "hw-gso-wire-bytes", qs->tx_hw_gso_wire_bytes);
			if (qs->_present.tx_stop)
				jsonw_uint_field(json_wtr, "stop", qs->tx_stop);
			if (qs->_present.tx_wake)
				jsonw_uint_field(json_wtr, "wake", qs->tx_wake);
			jsonw_end_object(json_wtr);
		}

		jsonw_end_object(json_wtr);
	}

	jsonw_end_array(json_wtr);
}

static void print_one(bool present, const char *name, unsigned long long val,
		      int *line)
{
	if (!present)
		return;

	if (!*line) {
		printf("              ");
		++(*line);
	}

	/* Don't waste space on tx- and rx- prefix, its implied by queue type */
	if (scope == NETDEV_QSTATS_SCOPE_QUEUE &&
	    (name[0] == 'r' || name[0] == 't') &&
	    name[1] == 'x' && name[2] == '-')
		name += 3;

	printf(" %15s: %15llu", name, val);

	if (++(*line) == 3) {
		printf("\n");
		*line = 0;
	}
}

static void print_plain_qstats(struct netdev_qstats_get_list *qstats)
{
	ynl_dump_foreach(qstats, qs) {
		char ifname[IF_NAMESIZE];
		const char *name;
		int n;

		name = if_indextoname(qs->ifindex, ifname);
		if (name)
			printf("%s", name);
		else
			printf("ifindex:%u", qs->ifindex);

		if (qs->_present.queue_type && qs->_present.queue_id)
			printf("\t%s-%-3u",
			       netdev_queue_type_str(qs->queue_type),
			       qs->queue_id);
		else
			printf("\t      ");

		n = 1;

		/* Basic counters */
		print_one(qs->_present.rx_packets, "rx-packets", qs->rx_packets, &n);
		print_one(qs->_present.rx_bytes, "rx-bytes", qs->rx_bytes, &n);
		print_one(qs->_present.tx_packets, "tx-packets", qs->tx_packets, &n);
		print_one(qs->_present.tx_bytes, "tx-bytes", qs->tx_bytes, &n);

		/* RX error/drop counters */
		print_one(qs->_present.rx_alloc_fail, "rx-alloc-fail",
			  qs->rx_alloc_fail, &n);
		print_one(qs->_present.rx_hw_drops, "rx-hw-drops",
			  qs->rx_hw_drops, &n);
		print_one(qs->_present.rx_hw_drop_overruns, "rx-hw-drop-overruns",
			  qs->rx_hw_drop_overruns, &n);
		print_one(qs->_present.rx_hw_drop_ratelimits, "rx-hw-drop-ratelimits",
			  qs->rx_hw_drop_ratelimits, &n);

		/* RX checksum counters */
		print_one(qs->_present.rx_csum_complete, "rx-csum-complete",
			  qs->rx_csum_complete, &n);
		print_one(qs->_present.rx_csum_unnecessary, "rx-csum-unnecessary",
			  qs->rx_csum_unnecessary, &n);
		print_one(qs->_present.rx_csum_none, "rx-csum-none",
			  qs->rx_csum_none, &n);
		print_one(qs->_present.rx_csum_bad, "rx-csum-bad",
			  qs->rx_csum_bad, &n);

		/* RX GRO counters */
		print_one(qs->_present.rx_hw_gro_packets, "rx-hw-gro-packets",
			  qs->rx_hw_gro_packets, &n);
		print_one(qs->_present.rx_hw_gro_bytes, "rx-hw-gro-bytes",
			  qs->rx_hw_gro_bytes, &n);
		print_one(qs->_present.rx_hw_gro_wire_packets, "rx-hw-gro-wire-packets",
			  qs->rx_hw_gro_wire_packets, &n);
		print_one(qs->_present.rx_hw_gro_wire_bytes, "rx-hw-gro-wire-bytes",
			  qs->rx_hw_gro_wire_bytes, &n);

		/* TX error/drop counters */
		print_one(qs->_present.tx_hw_drops, "tx-hw-drops",
			  qs->tx_hw_drops, &n);
		print_one(qs->_present.tx_hw_drop_errors, "tx-hw-drop-errors",
			  qs->tx_hw_drop_errors, &n);
		print_one(qs->_present.tx_hw_drop_ratelimits, "tx-hw-drop-ratelimits",
			  qs->tx_hw_drop_ratelimits, &n);

		/* TX checksum counters */
		print_one(qs->_present.tx_csum_none, "tx-csum-none",
			  qs->tx_csum_none, &n);
		print_one(qs->_present.tx_needs_csum, "tx-needs-csum",
			  qs->tx_needs_csum, &n);

		/* TX GSO counters */
		print_one(qs->_present.tx_hw_gso_packets, "tx-hw-gso-packets",
			  qs->tx_hw_gso_packets, &n);
		print_one(qs->_present.tx_hw_gso_bytes, "tx-hw-gso-bytes",
			  qs->tx_hw_gso_bytes, &n);
		print_one(qs->_present.tx_hw_gso_wire_packets, "tx-hw-gso-wire-packets",
			  qs->tx_hw_gso_wire_packets, &n);
		print_one(qs->_present.tx_hw_gso_wire_bytes, "tx-hw-gso-wire-bytes",
			  qs->tx_hw_gso_wire_bytes, &n);

		/* TX queue control */
		print_one(qs->_present.tx_stop, "tx-stop", qs->tx_stop, &n);
		print_one(qs->_present.tx_wake, "tx-wake", qs->tx_wake, &n);

		if (n)
			printf("\n");
	}
}

static int do_show(int argc, char **argv)
{
	struct netdev_qstats_get_list *qstats;
	struct netdev_qstats_get_req *req;
	struct ynl_error yerr;
	struct ynl_sock *ys;
	int ret = 0;

	/* Parse options */
	while (argc > 0) {
		if (is_prefix(*argv, "scope") || is_prefix(*argv, "group-by")) {
			NEXT_ARG();

			if (!REQ_ARGS(1))
				return -1;

			if (is_prefix(*argv, "queue")) {
				scope = NETDEV_QSTATS_SCOPE_QUEUE;
			} else if (is_prefix(*argv, "device")) {
				scope = 0;
			} else {
				p_err("invalid scope value '%s'", *argv);
				return -1;
			}
			NEXT_ARG();
		} else {
			p_err("unknown option '%s'", *argv);
			return -1;
		}
	}

	ys = ynl_sock_create(&ynl_netdev_family, &yerr);
	if (!ys) {
		p_err("YNL: %s", yerr.msg);
		return -1;
	}

	req = netdev_qstats_get_req_alloc();
	if (!req) {
		p_err("failed to allocate qstats request");
		ret = -1;
		goto exit_close;
	}

	if (scope)
		netdev_qstats_get_req_set_scope(req, scope);

	qstats = netdev_qstats_get_dump(ys, req);
	netdev_qstats_get_req_free(req);
	if (!qstats) {
		p_err("failed to get queue stats: %s", ys->err.msg);
		ret = -1;
		goto exit_close;
	}

	/* Print the stats as returned by the kernel */
	if (json_output)
		print_json_qstats(qstats);
	else
		print_plain_qstats(qstats);

	netdev_qstats_get_list_free(qstats);
exit_close:
	ynl_sock_destroy(ys);
	return ret;
}

static int do_help(int argc __attribute__((unused)),
		   char **argv __attribute__((unused)))
{
	if (json_output) {
		jsonw_null(json_wtr);
		return 0;
	}

	fprintf(stderr,
		"Usage: %s qstats { COMMAND | help }\n"
		"       %s qstats [ show ] [ OPTIONS ]\n"
		"\n"
		"       OPTIONS := { scope queue | group-by { device | queue } }\n"
		"\n"
		"       show                  - Display queue statistics (default)\n"
		"                               Statistics are aggregated for the entire device.\n"
		"       show scope queue      - Display per-queue statistics\n"
		"       show group-by device  - Display device-aggregated statistics (default)\n"
		"       show group-by queue   - Display per-queue statistics\n"
		"",
		bin_name, bin_name);

	return 0;
}

static const struct cmd qstats_cmds[] = {
	{ "show",	do_show },
	{ "help",	do_help },
	{ 0 }
};

int do_qstats(int argc, char **argv)
{
	return cmd_select(qstats_cmds, argc, argv, do_help);
}
