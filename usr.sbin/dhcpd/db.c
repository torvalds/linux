/*	$OpenBSD: db.c,v 1.18 2017/02/13 23:04:05 krw Exp $	*/

/*
 * Persistent database management routines for DHCPD.
 */

/*
 * Copyright (c) 1995, 1996 The Internet Software Consortium.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of The Internet Software Consortium nor the names
 *    of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INTERNET SOFTWARE CONSORTIUM AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE INTERNET SOFTWARE CONSORTIUM OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This software has been written for the Internet Software Consortium
 * by Ted Lemon <mellon@fugue.com> in cooperation with Vixie
 * Enterprises.  To learn more about the Internet Software Consortium,
 * see ``http://www.vix.com/isc''.  To learn more about Vixie
 * Enterprises, see ``http://www.vix.com''.
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <net/if.h>

#include <netinet/in.h>

#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "dhcp.h"
#include "tree.h"
#include "dhcpd.h"
#include "log.h"

FILE *db_file;

static int counting = 0;
static int count = 0;
time_t write_time;

/*
 * Write the specified lease to the current lease database file.
 */
int
write_lease(struct lease *lease)
{
	char tbuf[26];	/* "w yyyy/mm/dd hh:mm:ss UTC" */
	size_t rsltsz;
	int errors = 0;
	int i;

	if (counting)
		++count;
	if (fprintf(db_file, "lease %s {\n", piaddr(lease->ip_addr)) == -1)
		++errors;

	rsltsz = strftime(tbuf, sizeof(tbuf), DB_TIMEFMT,
	    gmtime(&lease->starts));
	if (rsltsz == 0 || fprintf(db_file, "\tstarts %s;\n", tbuf) == -1)
		errors++;

	rsltsz = strftime(tbuf, sizeof(tbuf), DB_TIMEFMT,
	    gmtime(&lease->ends));
	if (rsltsz == 0 || fprintf(db_file, "\tends %s;\n", tbuf) == -1)
		errors++;

	if (lease->hardware_addr.hlen) {
		if (fprintf(db_file, "\thardware %s %s;",
		    hardware_types[lease->hardware_addr.htype],
		    print_hw_addr(lease->hardware_addr.htype,
		    lease->hardware_addr.hlen,
		    lease->hardware_addr.haddr)) == -1)
			++errors;
	}

	if (lease->uid_len) {
		int j;

		if (fprintf(db_file, "\n\tuid %2.2x", lease->uid[0]) == -1)
			++errors;

		for (j = 1; j < lease->uid_len; j++) {
			if (fprintf(db_file, ":%2.2x", lease->uid[j]) == -1)
				++errors;
		}
		if (fputc(';', db_file) == EOF)
			++errors;
	}

	if (lease->flags & BOOTP_LEASE) {
		if (fprintf(db_file, "\n\tdynamic-bootp;") == -1)
			++errors;
	}

	if (lease->flags & ABANDONED_LEASE) {
		if (fprintf(db_file, "\n\tabandoned;") == -1)
			++errors;
	}

	if (lease->client_hostname) {
		for (i = 0; lease->client_hostname[i]; i++)
			if (lease->client_hostname[i] < 33 ||
			    lease->client_hostname[i] > 126)
				goto bad_client_hostname;
		if (fprintf(db_file, "\n\tclient-hostname \"%s\";",
		    lease->client_hostname) == -1)
			++errors;
	}

bad_client_hostname:
	if (lease->hostname) {
		for (i = 0; lease->hostname[i]; i++)
			if (lease->hostname[i] < 33 ||
			    lease->hostname[i] > 126)
				goto bad_hostname;
		if (fprintf(db_file, "\n\thostname \"%s\";",
		    lease->hostname) == -1)
			++errors;
	}

bad_hostname:
	if (fputs("\n}\n", db_file) == EOF)
		++errors;

	if (errors)
		log_info("write_lease: unable to write lease %s",
		    piaddr(lease->ip_addr));

	return (!errors);
}

/*
 * Commit any leases that have been written out...
 */
int
commit_leases(void)
{
	/*
	 * Commit any outstanding writes to the lease database file. We need to
	 * do this even if we're rewriting the file below, just in case the
	 * rewrite fails.
	 */
	if (fflush(db_file) == EOF) {
		log_warn("commit_leases: unable to commit");
		return (0);
	}

	if (fsync(fileno(db_file)) == -1) {
		log_warn("commit_leases: unable to commit");
		return (0);
	}

	/*
	 * If we've written more than a thousand leases or if we haven't
	 * rewritten the lease database in over an hour, rewrite it now.
	 */
	if (count > 1000 || (count && cur_time - write_time > 3600)) {
		count = 0;
		write_time = cur_time;
		new_lease_file();
	}

	return (1);
}

void
db_startup(void)
{
	int db_fd;

	/* open lease file. once we dropped privs it has to stay open */
	db_fd = open(path_dhcpd_db, O_WRONLY|O_CREAT, 0640);
	if (db_fd == -1)
		fatal("Can't create new lease file");
	if ((db_file = fdopen(db_fd, "w")) == NULL)
		fatalx("Can't fdopen new lease file!");

	/* Read in the existing lease file... */
	read_leases();
	time(&write_time);

	new_lease_file();
}

void
new_lease_file(void)
{
	fflush(db_file);
	rewind(db_file);

	/* Write out all the leases that we know of... */
	counting = 0;
	write_leases();

	fflush(db_file);
	ftruncate(fileno(db_file), ftello(db_file));
	fsync(fileno(db_file));

	counting = 1;
}
