// SPDX-License-Identifier: LGPL-2.1
/*
 * Copyright (C) 2009, 2010 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
 *
 */

#include "event-parse.h"
#include "event-parse-local.h"
#include "event-utils.h"

/**
 * tep_get_first_event - returns the first event in the events array
 * @tep: a handle to the tep_handle
 *
 * This returns pointer to the first element of the events array
 * If @tep is NULL, NULL is returned.
 */
struct tep_event *tep_get_first_event(struct tep_handle *tep)
{
	if (tep && tep->events)
		return tep->events[0];

	return NULL;
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
	if(tep)
		return tep->nr_events;
	return 0;
}

/**
 * tep_set_flag - set event parser flag
 * @tep: a handle to the tep_handle
 * @flag: flag, or combination of flags to be set
 * can be any combination from enum tep_flag
 *
 * This sets a flag or mbination of flags  from enum tep_flag
  */
void tep_set_flag(struct tep_handle *tep, int flag)
{
	if(tep)
		tep->flags |= flag;
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
	if(pevent)
		return pevent->header_page_size_size;
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
	if(pevent)
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
	if(pevent)
		pevent->cpus = cpus;
}

/**
 * tep_get_long_size - get the size of a long integer on the current machine
 * @pevent: a handle to the tep_handle
 *
 * This returns the size of a long integer on the current machine
 * If @pevent is NULL, 0 is returned.
 */
int tep_get_long_size(struct tep_handle *pevent)
{
	if(pevent)
		return pevent->long_size;
	return 0;
}

/**
 * tep_set_long_size - set the size of a long integer on the current machine
 * @pevent: a handle to the tep_handle
 * @size: size, in bytes, of a long integer
 *
 * This sets the size of a long integer on the current machine
 */
void tep_set_long_size(struct tep_handle *pevent, int long_size)
{
	if(pevent)
		pevent->long_size = long_size;
}

/**
 * tep_get_page_size - get the size of a memory page on the current machine
 * @pevent: a handle to the tep_handle
 *
 * This returns the size of a memory page on the current machine
 * If @pevent is NULL, 0 is returned.
 */
int tep_get_page_size(struct tep_handle *pevent)
{
	if(pevent)
		return pevent->page_size;
	return 0;
}

/**
 * tep_set_page_size - set the size of a memory page on the current machine
 * @pevent: a handle to the tep_handle
 * @_page_size: size of a memory page, in bytes
 *
 * This sets the size of a memory page on the current machine
 */
void tep_set_page_size(struct tep_handle *pevent, int _page_size)
{
	if(pevent)
		pevent->page_size = _page_size;
}

/**
 * tep_file_bigendian - get if the file is in big endian order
 * @pevent: a handle to the tep_handle
 *
 * This returns if the file is in big endian order
 * If @pevent is NULL, 0 is returned.
 */
int tep_file_bigendian(struct tep_handle *pevent)
{
	if(pevent)
		return pevent->file_bigendian;
	return 0;
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
	if(pevent)
		pevent->file_bigendian = endian;
}

/**
 * tep_is_host_bigendian - get if the order of the current host is big endian
 * @pevent: a handle to the tep_handle
 *
 * This gets if the order of the current host is big endian
 * If @pevent is NULL, 0 is returned.
 */
int tep_is_host_bigendian(struct tep_handle *pevent)
{
	if(pevent)
		return pevent->host_bigendian;
	return 0;
}

/**
 * tep_set_host_bigendian - set the order of the local host
 * @pevent: a handle to the tep_handle
 * @endian: non zero, if the local host has big endian order
 *
 * This sets the order of the local host
 */
void tep_set_host_bigendian(struct tep_handle *pevent, enum tep_endian endian)
{
	if(pevent)
		pevent->host_bigendian = endian;
}

/**
 * tep_is_latency_format - get if the latency output format is configured
 * @pevent: a handle to the tep_handle
 *
 * This gets if the latency output format is configured
 * If @pevent is NULL, 0 is returned.
 */
int tep_is_latency_format(struct tep_handle *pevent)
{
	if(pevent)
		return pevent->latency_format;
	return 0;
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
	if(pevent)
		pevent->latency_format = lat;
}
