/*
 * ixfrcreate.h -- generating IXFR differences from zonefiles.
 *
 * Copyright (c) 2021, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#ifndef IXFRCREATE_H
#define IXFRCREATE_H
#include "dns.h"
struct zone;
struct nsd;

/* the ixfr create data structure while the ixfr difference from zone files
 * is created. */
struct ixfr_create {
	/* the old serial and new serial */
	uint32_t old_serial, new_serial;
	/* the file with the spooled old zone data */
	char* file_name;
	/* zone name in uncompressed wireformat */
	uint8_t* zone_name;
	/* length of zone name */
	size_t zone_name_len;
	/* max size of ixfr in bytes */
	size_t max_size;
	/* we are in checkzone, errors should go to the console, not to the
	 * serverlog */
	int errorcmdline;
};

/* start ixfr creation */
struct ixfr_create* ixfr_create_start(struct zone* zone, const char* zfile,
	uint64_t ixfr_size, int errorcmdline);

/* free ixfr create */
void ixfr_create_free(struct ixfr_create* ixfrcr);

/* create the IXFR from differences. The old zone is spooled to file
 * and the new zone is in memory now.
 * With append_mem it does not only write to file but sticks it into the
 * memory lookup structure for IXFRs used by the server. */
int ixfr_create_perform(struct ixfr_create* ixfrcr, struct zone* zone,
	int append_mem, struct nsd* nsd, const char* zfile,
	uint32_t ixfr_number);

/* cancel ixfrcreation, that was started, but not performed yet.
 * It removes the temporary file. */
void ixfr_create_cancel(struct ixfr_create* ixfrcr);

/* returns true if ixfr should be created by taking difference between
 * zone file contents. Also checks if ixfr is enabled for the zone. */
int ixfr_create_from_difference(struct zone* zone, const char* zfile,
	int* ixfr_create_already_done_flag);

/* readup existing file if it already exists */
void ixfr_readup_exist(struct zone* zone, struct nsd* nsd, const char* zfile);

/*
 * Structure to keep track of spool domain name iterator.
 * This reads from the spool file and steps over the domain name
 * elements one by one. It keeps track of: is the first one read yet,
 * are we at end nothing more, is the element processed yet that is
 * current read into the buffer?
 */
struct spool_dname_iterator {
	/* the domain name that has recently been read, but can be none
	 * if before first or after last. */
	uint8_t dname[MAXDOMAINLEN+1];
	/* length of the dname, if one is read, otherwise 0 */
	size_t dname_len;
	/* if we are before the first element, hence nothing is read yet */
	int read_first;
	/* if we are after the last element, nothing to read, end of file */
	int eof;
	/* is the element processed that is currently in dname? */
	int is_processed;
	/* the file to read from */
	FILE* spool;
	/* filename for error printout */
	char* file_name;
};

#endif /* IXFRCREATE_H */
