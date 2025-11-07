// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <net/if.h>
#include <math.h>

#include <ynl.h>
#include "netdev-user.h"

#include "main.h"

static enum netdev_qstats_scope scope; /* default - device */

struct queue_balance {
	unsigned int ifindex;
	enum netdev_queue_type type;
	unsigned int queue_count;
	__u64 *rx_packets;
	__u64 *rx_bytes;
	__u64 *tx_packets;
	__u64 *tx_bytes;
};

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

static void compute_stats(__u64 *values, unsigned int count,
			  double *mean, double *stddev, __u64 *min, __u64 *max)
{
	double sum = 0.0, variance = 0.0;
	unsigned int i;

	*min = ~0ULL;
	*max = 0;

	if (count == 0) {
		*mean = 0;
		*stddev = 0;
		*min = 0;
		return;
	}

	for (i = 0; i < count; i++) {
		sum += values[i];
		if (values[i] < *min)
			*min = values[i];
		if (values[i] > *max)
			*max = values[i];
	}

	*mean = sum / count;

	if (count > 1) {
		for (i = 0; i < count; i++) {
			double diff = values[i] - *mean;

			variance += diff * diff;
		}
		*stddev = sqrt(variance / (count - 1));
	} else {
		*stddev = 0;
	}
}

static void print_balance_stats(const char *name, enum netdev_queue_type type,
				__u64 *values, unsigned int count)
{
	double mean, stddev, cv, ns;
	__u64 min, max;

	if ((name[0] == 'r' && type != NETDEV_QUEUE_TYPE_RX) ||
	    (name[0] == 't' && type != NETDEV_QUEUE_TYPE_TX))
		return;

	compute_stats(values, count, &mean, &stddev, &min, &max);

	cv = mean > 0 ? (stddev / mean) * 100.0 : 0.0;
	ns = min + max > 0 ? (double)2 * (max - min) / (max + min) * 100 : 0.0;

	printf("  %-12s: cv=%.1f%% ns=%.1f%% stddev=%.0f\n",
	       name, cv, ns, stddev);
	printf("  %-12s  min=%llu max=%llu mean=%.0f\n",
	       "", min, max, mean);
}

static void
print_balance_stats_json(const char *name, enum netdev_queue_type type,
			 __u64 *values, unsigned int count)
{
	double mean, stddev, cv, ns;
	__u64 min, max;

	if ((name[0] == 'r' && type != NETDEV_QUEUE_TYPE_RX) ||
	    (name[0] == 't' && type != NETDEV_QUEUE_TYPE_TX))
		return;

	compute_stats(values, count, &mean, &stddev, &min, &max);

	cv = mean > 0 ? (stddev / mean) * 100.0 : 0.0;
	ns = min + max > 0 ? (double)2 * (max - min) / (max + min) * 100 : 0.0;

	jsonw_name(json_wtr, name);
	jsonw_start_object(json_wtr);
	jsonw_uint_field(json_wtr, "queue-count", count);
	jsonw_uint_field(json_wtr, "min", min);
	jsonw_uint_field(json_wtr, "max", max);
	jsonw_float_field(json_wtr, "mean", mean);
	jsonw_float_field(json_wtr, "stddev", stddev);
	jsonw_float_field(json_wtr, "coefficient-of-variation", cv);
	jsonw_float_field(json_wtr, "normalized-spread", ns);
	jsonw_end_object(json_wtr);
}

static int cmp_ifindex_type(const void *a, const void *b)
{
	const struct netdev_qstats_get_rsp *qa = a;
	const struct netdev_qstats_get_rsp *qb = b;

	if (qa->ifindex != qb->ifindex)
		return qa->ifindex - qb->ifindex;
	if (qa->queue_type != qb->queue_type)
		return qa->queue_type - qb->queue_type;
	return qa->queue_id - qb->queue_id;
}

static int do_balance(int argc, char **argv __attribute__((unused)))
{
	struct netdev_qstats_get_list *qstats;
	struct netdev_qstats_get_req *req;
	struct netdev_qstats_get_rsp **sorted;
	struct ynl_error yerr;
	struct ynl_sock *ys;
	unsigned int count = 0;
	unsigned int i, j;
	int ret = 0;

	if (argc > 0) {
		p_err("balance command takes no arguments");
		return -1;
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

	/* Always use queue scope for balance analysis */
	netdev_qstats_get_req_set_scope(req, NETDEV_QSTATS_SCOPE_QUEUE);

	qstats = netdev_qstats_get_dump(ys, req);
	netdev_qstats_get_req_free(req);
	if (!qstats) {
		p_err("failed to get queue stats: %s", ys->err.msg);
		ret = -1;
		goto exit_close;
	}

	/* Count and sort queues */
	ynl_dump_foreach(qstats, qs)
		count++;

	if (count == 0) {
		if (json_output)
			jsonw_start_array(json_wtr);
		else
			printf("No queue statistics available\n");
		goto exit_free_qstats;
	}

	sorted = calloc(count, sizeof(*sorted));
	if (!sorted) {
		p_err("failed to allocate sorted array");
		ret = -1;
		goto exit_free_qstats;
	}

	i = 0;
	ynl_dump_foreach(qstats, qs)
		sorted[i++] = qs;

	qsort(sorted, count, sizeof(*sorted), cmp_ifindex_type);

	if (json_output)
		jsonw_start_array(json_wtr);

	/* Process each device/queue-type combination */
	i = 0;
	while (i < count) {
		__u64 *rx_packets, *rx_bytes, *tx_packets, *tx_bytes;
		enum netdev_queue_type type = sorted[i]->queue_type;
		unsigned int ifindex = sorted[i]->ifindex;
		unsigned int queue_count = 0;
		char ifname[IF_NAMESIZE];
		const char *name;

		/* Count queues for this device/type */
		for (j = i; j < count && sorted[j]->ifindex == ifindex &&
		     sorted[j]->queue_type == type; j++)
			queue_count++;

		/* Skip if no packets/bytes (inactive queues) */
		if (!sorted[i]->_present.rx_packets &&
		    !sorted[i]->_present.rx_bytes &&
		    !sorted[i]->_present.tx_packets &&
		    !sorted[i]->_present.tx_bytes)
			goto next_ifc;

		/* Allocate arrays for statistics */
		rx_packets = calloc(queue_count, sizeof(*rx_packets));
		rx_bytes   = calloc(queue_count, sizeof(*rx_bytes));
		tx_packets = calloc(queue_count, sizeof(*tx_packets));
		tx_bytes   = calloc(queue_count, sizeof(*tx_bytes));

		if (!rx_packets || !rx_bytes || !tx_packets || !tx_bytes) {
			p_err("failed to allocate statistics arrays");
			free(rx_packets);
			free(rx_bytes);
			free(tx_packets);
			free(tx_bytes);
			ret = -1;
			goto exit_free_sorted;
		}

		/* Collect statistics */
		for (j = 0; j < queue_count; j++) {
			rx_packets[j] = sorted[i + j]->_present.rx_packets ?
					sorted[i + j]->rx_packets : 0;
			rx_bytes[j] = sorted[i + j]->_present.rx_bytes ?
				      sorted[i + j]->rx_bytes : 0;
			tx_packets[j] = sorted[i + j]->_present.tx_packets ?
					sorted[i + j]->tx_packets : 0;
			tx_bytes[j] = sorted[i + j]->_present.tx_bytes ?
				      sorted[i + j]->tx_bytes : 0;
		}

		name = if_indextoname(ifindex, ifname);

		if (json_output) {
			jsonw_start_object(json_wtr);
			if (name)
				jsonw_string_field(json_wtr, "ifname", name);
			jsonw_uint_field(json_wtr, "ifindex", ifindex);
			jsonw_string_field(json_wtr, "queue-type",
					   netdev_queue_type_str(type));

			print_balance_stats_json("rx-packets", type,
						 rx_packets, queue_count);
			print_balance_stats_json("rx-bytes", type,
						 rx_bytes, queue_count);
			print_balance_stats_json("tx-packets", type,
						 tx_packets, queue_count);
			print_balance_stats_json("tx-bytes", type,
						 tx_bytes, queue_count);

			jsonw_end_object(json_wtr);
		} else {
			if (name)
				printf("%s", name);
			else
				printf("ifindex:%u", ifindex);
			printf(" %s %d queues:\n",
			       netdev_queue_type_str(type), queue_count);

			print_balance_stats("rx-packets", type,
					    rx_packets, queue_count);
			print_balance_stats("rx-bytes", type,
					    rx_bytes, queue_count);
			print_balance_stats("tx-packets", type,
					    tx_packets, queue_count);
			print_balance_stats("tx-bytes", type,
					    tx_bytes, queue_count);
			printf("\n");
		}

		free(rx_packets);
		free(rx_bytes);
		free(tx_packets);
		free(tx_bytes);

next_ifc:
		i += queue_count;
	}

	if (json_output)
		jsonw_end_array(json_wtr);

exit_free_sorted:
	free(sorted);
exit_free_qstats:
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
		"       %s qstats balance\n"
		"\n"
		"       OPTIONS := { scope queue | group-by { device | queue } }\n"
		"\n"
		"       show                  - Display queue statistics (default)\n"
		"                               Statistics are aggregated for the entire device.\n"
		"       show scope queue      - Display per-queue statistics\n"
		"       show group-by device  - Display device-aggregated statistics (default)\n"
		"       show group-by queue   - Display per-queue statistics\n"
		"       balance               - Analyze traffic distribution balance.\n"
		"",
		bin_name, bin_name, bin_name);

	return 0;
}

static const struct cmd qstats_cmds[] = {
	{ "show",	do_show },
	{ "balance",	do_balance },
	{ "help",	do_help },
	{ 0 }
};

int do_qstats(int argc, char **argv)
{
	return cmd_select(qstats_cmds, argc, argv, do_help);
}
