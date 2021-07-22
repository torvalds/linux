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

__hidden unsigned short data2host2(struct tep_handle *tep, unsigned short data)
{
	unsigned short swap;

	if (!tep || tep->host_bigendian == tep->file_bigendian)
		return data;

	swap = ((data & 0xffULL) << 8) |
		((data & (0xffULL << 8)) >> 8);

	return swap;
}

__hidden unsigned int data2host4(struct tep_handle *tep, unsigned int data)
{
	unsigned int swap;

	if (!tep || tep->host_bigendian == tep->file_bigendian)
		return data;

	swap = ((data & 0xffULL) << 24) |
		((data & (0xffULL << 8)) << 8) |
		((data & (0xffULL << 16)) >> 8) |
		((data & (0xffULL << 24)) >> 24);

	return swap;
}

__hidden  unsigned long long
data2host8(struct tep_handle *tep, unsigned long long data)
{
	unsigned long long swap;

	if (!tep || tep->host_bigendian == tep->file_bigendian)
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
 * @tep: a handle to the tep_handle
 *
 * This returns size of the header page
 * If @tep is NULL, 0 is returned.
 */
int tep_get_header_page_size(struct tep_handle *tep)
{
	if (tep)
		return tep->header_page_size_size;
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
 * @tep: a handle to the tep_handle
 *
 * This returns the number of CPUs
 * If @tep is NULL, 0 is returned.
 */
int tep_get_cpus(struct tep_handle *tep)
{
	if (tep)
		return tep->cpus;
	return 0;
}

/**
 * tep_set_cpus - set the number of CPUs
 * @tep: a handle to the tep_handle
 *
 * This sets the number of CPUs
 */
void tep_set_cpus(struct tep_handle *tep, int cpus)
{
	if (tep)
		tep->cpus = cpus;
}

/**
 * tep_get_long_size - get the size of a long integer on the traced machine
 * @tep: a handle to the tep_handle
 *
 * This returns the size of a long integer on the traced machine
 * If @tep is NULL, 0 is returned.
 */
int tep_get_long_size(struct tep_handle *tep)
{
	if (tep)
		return tep->long_size;
	return 0;
}

/**
 * tep_set_long_size - set the size of a long integer on the traced machine
 * @tep: a handle to the tep_handle
 * @size: size, in bytes, of a long integer
 *
 * This sets the size of a long integer on the traced machine
 */
void tep_set_long_size(struct tep_handle *tep, int long_size)
{
	if (tep)
		tep->long_size = long_size;
}

/**
 * tep_get_page_size - get the size of a memory page on the traced machine
 * @tep: a handle to the tep_handle
 *
 * This returns the size of a memory page on the traced machine
 * If @tep is NULL, 0 is returned.
 */
int tep_get_page_size(struct tep_handle *tep)
{
	if (tep)
		return tep->page_size;
	return 0;
}

/**
 * tep_set_page_size - set the size of a memory page on the traced machine
 * @tep: a handle to the tep_handle
 * @_page_size: size of a memory page, in bytes
 *
 * This sets the size of a memory page on the traced machine
 */
void tep_set_page_size(struct tep_handle *tep, int _page_size)
{
	if (tep)
		tep->page_size = _page_size;
}

/**
 * tep_is_file_bigendian - return the endian of the file
 * @tep: a handle to the tep_handle
 *
 * This returns true if the file is in big endian order
 * If @tep is NULL, false is returned.
 */
bool tep_is_file_bigendian(struct tep_handle *tep)
{
	if (tep)
		return (tep->file_bigendian == TEP_BIG_ENDIAN);
	return false;
}

/**
 * tep_set_file_bigendian - set if the file is in big endian order
 * @tep: a handle to the tep_handle
 * @endian: non zero, if the file is in big endian order
 *
 * This sets if the file is in big endian order
 */
void tep_set_file_bigendian(struct tep_handle *tep, enum tep_endian endian)
{
	if (tep)
		tep->file_bigendian = endian;
}

/**
 * tep_is_local_bigendian - return the endian of the saved local machine
 * @tep: a handle to the tep_handle
 *
 * This returns true if the saved local machine in @tep is big endian.
 * If @tep is NULL, false is returned.
 */
bool tep_is_local_bigendian(struct tep_handle *tep)
{
	if (tep)
		return (tep->host_bigendian == TEP_BIG_ENDIAN);
	return 0;
}

/**
 * tep_set_local_bigendian - set the stored local machine endian order
 * @tep: a handle to the tep_handle
 * @endian: non zero, if the local host has big endian order
 *
 * This sets the endian order for the local machine.
 */
void tep_set_local_bigendian(struct tep_handle *tep, enum tep_endian endian)
{
	if (tep)
		tep->host_bigendian = endian;
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
