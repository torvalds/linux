// SPDX-License-Identifier: GPL-2.0
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include "../../../../../include/linux/compiler.h"
#include "../../../../../include/linux/kernel.h"
#include "aolib.h"

struct netstat_counter {
	uint64_t val;
	char *name;
};

struct netstat {
	char *header_name;
	struct netstat *next;
	size_t counters_nr;
	struct netstat_counter *counters;
};

static struct netstat *lookup_type(struct netstat *ns,
		const char *type, size_t len)
{
	while (ns != NULL) {
		size_t cmp = max(len, strlen(ns->header_name));

		if (!strncmp(ns->header_name, type, cmp))
			return ns;
		ns = ns->next;
	}
	return NULL;
}

static struct netstat *lookup_get(struct netstat *ns,
				  const char *type, const size_t len)
{
	struct netstat *ret;

	ret = lookup_type(ns, type, len);
	if (ret != NULL)
		return ret;

	ret = malloc(sizeof(struct netstat));
	if (!ret)
		test_error("malloc()");

	ret->header_name = strndup(type, len);
	if (ret->header_name == NULL)
		test_error("strndup()");
	ret->next = ns;
	ret->counters_nr = 0;
	ret->counters = NULL;

	return ret;
}

static struct netstat *lookup_get_column(struct netstat *ns, const char *line)
{
	char *column;

	column = strchr(line, ':');
	if (!column)
		test_error("can't parse netstat file");

	return lookup_get(ns, line, column - line);
}

static void netstat_read_type(FILE *fnetstat, struct netstat **dest, char *line)
{
	struct netstat *type = lookup_get_column(*dest, line);
	const char *pos = line;
	size_t i, nr_elems = 0;
	char tmp;

	while ((pos = strchr(pos, ' '))) {
		nr_elems++;
		pos++;
	}

	*dest = type;
	type->counters = reallocarray(type->counters,
				type->counters_nr + nr_elems,
				sizeof(struct netstat_counter));
	if (!type->counters)
		test_error("reallocarray()");

	pos = strchr(line, ' ') + 1;

	if (fscanf(fnetstat, type->header_name) == EOF)
		test_error("fscanf(%s)", type->header_name);
	if (fread(&tmp, 1, 1, fnetstat) != 1 || tmp != ':')
		test_error("Unexpected netstat format (%c)", tmp);

	for (i = type->counters_nr; i < type->counters_nr + nr_elems; i++) {
		struct netstat_counter *nc = &type->counters[i];
		const char *new_pos = strchr(pos, ' ');
		const char *fmt = " %" PRIu64;

		if (new_pos == NULL)
			new_pos = strchr(pos, '\n');

		nc->name = strndup(pos, new_pos - pos);
		if (nc->name == NULL)
			test_error("strndup()");

		if (unlikely(!strcmp(nc->name, "MaxConn")))
			fmt = " %" PRId64; /* MaxConn is signed, RFC 2012 */
		if (fscanf(fnetstat, fmt, &nc->val) != 1)
			test_error("fscanf(%s)", nc->name);
		pos = new_pos + 1;
	}
	type->counters_nr += nr_elems;

	if (fread(&tmp, 1, 1, fnetstat) != 1 || tmp != '\n')
		test_error("Unexpected netstat format");
}

static const char *snmp6_name = "Snmp6";
static void snmp6_read(FILE *fnetstat, struct netstat **dest)
{
	struct netstat *type = lookup_get(*dest, snmp6_name, strlen(snmp6_name));
	char *counter_name;
	size_t i;

	for (i = type->counters_nr;; i++) {
		struct netstat_counter *nc;
		uint64_t counter;

		if (fscanf(fnetstat, "%ms", &counter_name) == EOF)
			break;
		if (fscanf(fnetstat, "%" PRIu64, &counter) == EOF)
			test_error("Unexpected snmp6 format");
		type->counters = reallocarray(type->counters, i + 1,
					sizeof(struct netstat_counter));
		if (!type->counters)
			test_error("reallocarray()");
		nc = &type->counters[i];
		nc->name = counter_name;
		nc->val = counter;
	}
	type->counters_nr = i;
	*dest = type;
}

struct netstat *netstat_read(void)
{
	struct netstat *ret = 0;
	size_t line_sz = 0;
	char *line = NULL;
	FILE *fnetstat;

	/*
	 * Opening thread-self instead of /proc/net/... as the latter
	 * points to /proc/self/net/ which instantiates thread-leader's
	 * net-ns, see:
	 * commit 155134fef2b6 ("Revert "proc: Point /proc/{mounts,net} at..")
	 */
	errno = 0;
	fnetstat = fopen("/proc/thread-self/net/netstat", "r");
	if (fnetstat == NULL)
		test_error("failed to open /proc/net/netstat");

	while (getline(&line, &line_sz, fnetstat) != -1)
		netstat_read_type(fnetstat, &ret, line);
	fclose(fnetstat);

	errno = 0;
	fnetstat = fopen("/proc/thread-self/net/snmp", "r");
	if (fnetstat == NULL)
		test_error("failed to open /proc/net/snmp");

	while (getline(&line, &line_sz, fnetstat) != -1)
		netstat_read_type(fnetstat, &ret, line);
	fclose(fnetstat);

	errno = 0;
	fnetstat = fopen("/proc/thread-self/net/snmp6", "r");
	if (fnetstat == NULL)
		test_error("failed to open /proc/net/snmp6");

	snmp6_read(fnetstat, &ret);
	fclose(fnetstat);

	free(line);
	return ret;
}

void netstat_free(struct netstat *ns)
{
	while (ns != NULL) {
		struct netstat *prev = ns;
		size_t i;

		free(ns->header_name);
		for (i = 0; i < ns->counters_nr; i++)
			free(ns->counters[i].name);
		free(ns->counters);
		ns = ns->next;
		free(prev);
	}
}

static inline void
__netstat_print_diff(uint64_t a, struct netstat *nsb, size_t i)
{
	if (unlikely(!strcmp(nsb->header_name, "MaxConn"))) {
		test_print("%8s %25s: %" PRId64 " => %" PRId64,
				nsb->header_name, nsb->counters[i].name,
				a, nsb->counters[i].val);
		return;
	}

	test_print("%8s %25s: %" PRIu64 " => %" PRIu64, nsb->header_name,
			nsb->counters[i].name, a, nsb->counters[i].val);
}

void netstat_print_diff(struct netstat *nsa, struct netstat *nsb)
{
	size_t i, j;

	while (nsb != NULL) {
		if (unlikely(strcmp(nsb->header_name, nsa->header_name))) {
			for (i = 0; i < nsb->counters_nr; i++)
				__netstat_print_diff(0, nsb, i);
			nsb = nsb->next;
			continue;
		}

		if (nsb->counters_nr < nsa->counters_nr)
			test_error("Unexpected: some counters disappeared!");

		for (j = 0, i = 0; i < nsb->counters_nr; i++) {
			if (strcmp(nsb->counters[i].name, nsa->counters[j].name)) {
				__netstat_print_diff(0, nsb, i);
				continue;
			}

			if (nsa->counters[j].val == nsb->counters[i].val) {
				j++;
				continue;
			}

			__netstat_print_diff(nsa->counters[j].val, nsb, i);
			j++;
		}
		if (j != nsa->counters_nr)
			test_error("Unexpected: some counters disappeared!");

		nsb = nsb->next;
		nsa = nsa->next;
	}
}

uint64_t netstat_get(struct netstat *ns, const char *name, bool *not_found)
{
	if (not_found)
		*not_found = false;

	while (ns != NULL) {
		size_t i;

		for (i = 0; i < ns->counters_nr; i++) {
			if (!strcmp(name, ns->counters[i].name))
				return ns->counters[i].val;
		}

		ns = ns->next;
	}

	if (not_found)
		*not_found = true;
	return 0;
}
