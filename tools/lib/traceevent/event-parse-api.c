// SPDX-License-Identifier: LGPL-2.1
/*
 * Copyright (C) 2009, 2010 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
 *
 */

#include "event-parse.h"
#include "event-parse-local.h"
#include "event-utils.h"

/**
 * tep_get_event - returns the event with the given index
 * @tep: a handle to the tep_handle
 * @index: index of the requested event, in the range 0 .. nr_events
 *
 * This returns pointer to the element of the events array with the given index
 * If @tep is NULL, or @index is not in the range 0 .. nr_events, NULL is returned.
 */
struct tep_event *tep_get_event(struct tep_handle *tep, int index)
{
	if (tep && tep->events && index < tep->nr_events)
		return tep->events[index];

	return NULL;
}

/**
 * tep_get_first_event - returns the first event in the events array
 * @tep: a handle to the tep_handle
 *
 * This returns pointer to the first element of the events array
 * If @tep is NULL, NULL is returned.
 */
struct tep_event *tep_get_first_event(struct tep_handle *tep)
{
	return tep_get_event(tep, 0);
}

/**
 * tep_get_events_count - get the number of defined events
 * @tep: a handle to the tep_handle
 *
 * This returns number of elements in event array
 * If @tep is NULL, 0 is returned.
 */
int tep_get_events_count(struct tep_handle *tep)
{
	if (tep)
		return tep->nr_events;
	return 0;
}

/**
 * tep_set_flag - set event parser flag
 * @tep: a handle to the tep_handle
 * @flag: flag, or combination of flags to be set
 * can be any combination from enum tep_flag
 *
 * This sets a flag or combination of flags from enum tep_flag
 */
void tep_set_flag(struct tep_handle *tep, int flag)
{
	if (tep)
		tep->flags |= flag;
}

/**
 * tep_clear_flag - clear event parser flag
 * @tep: a handle to the tep_handle
 * @flag: flag to be cleared
 *
 * This clears a tep flag
 */
void tep_clear_flag(struct tep_handle *tep, enum tep_flag flag)
{
	if (tep)
		tep->flags &= ~flag;
}

/**
 * tep_test_flag - check the state of event parser flag
 * @tep: a handle to the tep_handle
 * @flag: flag to be checked
 *
 * This returns the state of the requested tep flag.
 * Returns: true if the flag is set, false otherwise.
 */
bool tep_test_flag(struct tep_handle *tep, enum tep_flag flag)
{
	if (tep)
		return tep->flags & flag;
	return false;
}

unsigned short tep_data2host2(struct tep_handle *pevent, unsigned short data)
{
	unsigned short swap;

	if (!pevent || pevent->host_bigendian == pevent->file_bigendian)
		return data;

	swap = ((data & 0xffULL) << 8) |
		((data & (0xffULL << 8)) >> 8);

	return swap;
}

unsigned int tep_data2host4(struct tep_handle *pevent, unsigned int data)
{
	unsigned int swap;

	if (!pevent || pevent->host_bigendian == pevent->file_bigendian)
		return data;

	swap = ((data & 0xffULL) << 24) |
		((data & (0xffULL << 8)) << 8) |
		((data & (0xffULL << 16)) >> 8) |
		((data & (0xffULL << 24)) >> 24);

	return swap;
}

unsigned long long
tep_data2host8(struct tep_handle *pevent, unsigned long long data)
{
	unsigned long long swap;

	if (!pevent || pevent->host_bigendian == pevent->file_bigendian)
		return data;

	swap = ((data & 0xffULL) << 56) |
		((data & (0xffULL << 8)) << 40) |
		((data & (0xffULL << 16)) << 24) |
		((data & (0xffULL << 24)) << 8) |
		((data & (0xffULL << 32)) >> 8) |
		((data & (0xffULL << 40)) >> 24) |
		((data & (0xffULL << 48)) >> 40) |
		((data & (0xffULL << 56)) >> 56);

	return swap;
}

/**
 * tep_get_header_page_size - get size of the header page
 * @pevent: a handle to the tep_handle
 *
 * This returns size of the header page
 * If @pevent is NULL, 0 is returned.
 */
int tep_get_header_page_size(struct tep_handle *pevent)
{
	if (pevent)
		return pevent->header_page_size_size;
	return 0;
}

/**
 * tep_get_header_timestamp_size - get size of the timestamp in the header page
 * @tep: a handle to the tep_handle
 *
 * This returns size of the timestamp in the header page
 * If @tep is NULL, 0 is returned.
 */
int tep_get_header_timestamp_size(struct tep_handle *tep)
{
	if (tep)
		return tep->header_page_ts_size;
	return 0;
}

/**
 * tep_get_cpus - get the number of CPUs
 * @pevent: a handle to the tep_handle
 *
 * This returns the number of CPUs
 * If @pevent is NULL, 0 is returned.
 */
int tep_get_cpus(struct tep_handle *pevent)
{
	if (pevent)
		return pevent->cpus;
	return 0;
}

/**
 * tep_set_cpus - set the number of CPUs
 * @pevent: a handle to the tep_handle
 *
 * This sets the number of CPUs
 */
void tep_set_cpus(struct tep_handle *pevent, int cpus)
{
	if (pevent)
		pevent->cpus = cpus;
}

/**
 * tep_get_long_size - get the size of a long integer on the traced machine
 * @pevent: a handle to the tep_handle
 *
 * This returns the size of a long integer on the traced machine
 * If @pevent is NULL, 0 is returned.
 */
int tep_get_long_size(struct tep_handle *pevent)
{
	if (pevent)
		return pevent->long_size;
	return 0;
}

/**
 * tep_set_long_size - set the size of a long integer on the traced machine
 * @pevent: a handle to the tep_handle
 * @size: size, in bytes, of a long integer
 *
 * This sets the size of a long integer on the traced machine
 */
void tep_set_long_size(struct tep_handle *pevent, int long_size)
{
	if (pevent)
		pevent->long_size = long_size;
}

/**
 * tep_get_page_size - get the size of a memory page on the traced machine
 * @pevent: a handle to the tep_handle
 *
 * This returns the size of a memory page on the traced machine
 * If @pevent is NULL, 0 is returned.
 */
int tep_get_page_size(struct tep_handle *pevent)
{
	if (pevent)
		return pevent->page_size;
	return 0;
}

/**
 * tep_set_page_size - set the size of a memory page on the traced machine
 * @pevent: a handle to the tep_handle
 * @_page_size: size of a memory page, in bytes
 *
 * This sets the size of a memory page on the traced machine
 */
void tep_set_page_size(struct tep_handle *pevent, int _page_size)
{
	if (pevent)
		pevent->page_size = _page_size;
}

/**
 * tep_is_file_bigendian - return the endian of the file
 * @pevent: a handle to the tep_handle
 *
 * This returns true if the file is in big endian order
 * If @pevent is NULL, false is returned.
 */
bool tep_is_file_bigendian(struct tep_handle *pevent)
{
	if (pevent)
		return pevent->file_bigendian == TEP_BIG_ENDIAN;
	return false;
}

/**
 * tep_set_file_bigendian - set if the file is in big endian order
 * @pevent: a handle to the tep_handle
 * @endian: non zero, if the file is in big endian order
 *
 * This sets if the file is in big endian order
 */
void tep_set_file_bigendian(struct tep_handle *pevent, enum tep_endian endian)
{
	if (pevent)
		pevent->file_bigendian = endian;
}

/**
 * tep_is_local_bigendian - return the endian of the saved local machine
 * @pevent: a handle to the tep_handle
 *
 * This returns true if the saved local machine in @pevent is big endian.
 * If @pevent is NULL, false is returned.
 */
bool tep_is_local_bigendian(struct tep_handle *pevent)
{
	if (pevent)
		return pevent->host_bigendian == TEP_BIG_ENDIAN;
	return 0;
}

/**
 * tep_set_local_bigendian - set the stored local machine endian order
 * @pevent: a handle to the tep_handle
 * @endian: non zero, if the local host has big endian order
 *
 * This sets the endian order for the local machine.
 */
void tep_set_local_bigendian(struct tep_handle *pevent, enum tep_endian endian)
{
	if (pevent)
		pevent->host_bigendian = endian;
}

/**
 * tep_is_latency_format - get if the latency output format is configured
 * @pevent: a handle to the tep_handle
 *
 * This returns true if the latency output format is configured
 * If @pevent is NULL, false is returned.
 */
bool tep_is_latency_format(struct tep_handle *pevent)
{
	if (pevent)
		return pevent->latency_format;
	return false;
}

/**
 * tep_set_latency_format - set the latency output format
 * @pevent: a handle to the tep_handle
 * @lat: non zero for latency output format
 *
 * This sets the latency output format
  */
void tep_set_latency_format(struct tep_handle *pevent, int lat)
{
	if (pevent)
		pevent->latency_format = lat;
}

/**
 * tep_is_old_format - get if an old kernel is used
 * @tep: a handle to the tep_handle
 *
 * This returns true, if an old kernel is used to generate the tracing events or
 * false if a new kernel is used. Old kernels did not have header page info.
 * If @tep is NULL, false is returned.
 */
bool tep_is_old_format(struct tep_handle *tep)
{
	if (tep)
		return tep->old_format;
	return false;
}

/**
 * tep_set_print_raw - set a flag to force print in raw format
 * @tep: a handle to the tep_handle
 * @print_raw: the new value of the print_raw flag
 *
 * This sets a flag to force print in raw format
 */
void tep_set_print_raw(struct tep_handle *tep, int print_raw)
{
	if (tep)
		tep->print_raw = print_raw;
}

/**
 * tep_set_test_filters - set a flag to test a filter string
 * @tep: a handle to the tep_handle
 * @test_filters: the new value of the test_filters flag
 *
 * This sets a flag to test a filter string. If this flag is set, when
 * tep_filter_add_filter_str() API as called,it will print the filter string
 * instead of adding it.
 */
void tep_set_test_filters(struct tep_handle *tep, int test_filters)
{
	if (tep)
		tep->test_filters = test_filters;
}
