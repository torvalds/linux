/*
 * nsd-checkzone.c -- nsd-checkzone(8) checks zones for syntax errors
 *
 * Copyright (c) 2013, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include "nsd.h"
#include "options.h"
#include "util.h"
#include "zonec.h"
#include "ixfr.h"
#include "ixfrcreate.h"
#include "difffile.h"

struct nsd nsd;

/*
 * Print the help text.
 *
 */
static void
usage (void)
{
	fprintf(stderr, "Usage: nsd-checkzone [-p] <zone name> <zone file>\n");
	fprintf(stderr, "\t-p\tprint the zone if the zone is ok\n");
	fprintf(stderr, "\t-i <old zone file>\tcreate an IXFR from the differences between the\n\t\told zone file and the new zone file. Writes to \n\t\t<zonefile>.ixfr and renames other <zonefile>.ixfr files to\n\t\t<zonefile>.ixfr.num+1.\n");
	fprintf(stderr, "\t-n <ixfr number>\tnumber of IXFR versions to store, at most.\n\t\tdefault %d.\n", (int)IXFR_NUMBER_DEFAULT);
	fprintf(stderr, "\t-s <ixfr size>\tsize of IXFR to store, at most. default %d.\n", (int)IXFR_SIZE_DEFAULT);
	fprintf(stderr, "Version %s. Report bugs to <%s>.\n",
		PACKAGE_VERSION, PACKAGE_BUGREPORT);
}

static void
check_zone(struct nsd* nsd, const char* name, const char* fname, FILE *out,
	const char* oldzone, uint32_t ixfr_number, uint64_t ixfr_size)
{
	const dname_type* dname;
	zone_options_type* zo;
	zone_type* zone;
	unsigned errors;
	struct ixfr_create* ixfrcr = NULL;

	/* init*/
	nsd->db = namedb_open(nsd->options);
	dname = dname_parse(nsd->options->region, name);
	if(!dname) {
		/* parse failure */
		error("cannot parse zone name '%s'", name);
	}
	zo = zone_options_create(nsd->options->region);
	memset(zo, 0, sizeof(*zo));
	zo->node.key = dname;
	zo->name = name;
	zone = namedb_zone_create(nsd->db, dname, zo);

	if(oldzone) {
		errors = zonec_read(nsd->db, nsd->db->domains, name, oldzone, zone);
		if(errors > 0) {
			printf("zone %s file %s has %u errors\n", name, oldzone, errors);
			exit(1);
		}
		ixfrcr = ixfr_create_start(zone, fname, ixfr_size, 1);
		if(!ixfrcr) {
			error("out of memory");
		}
		delete_zone_rrs(nsd->db, zone);
	}

	/* read the zone */
	errors = zonec_read(nsd->db, nsd->db->domains, name, fname, zone);
	if(errors > 0) {
		printf("zone %s file %s has %u errors\n", name, fname, errors);
		ixfr_create_cancel(ixfrcr);
#ifdef MEMCLEAN /* otherwise, the OS collects memory pages */
		namedb_close(nsd->db);
		region_destroy(nsd->options->region);
#endif
		exit(1);
	}
	if(ixfrcr) {
		if(!ixfr_create_perform(ixfrcr, zone, 0, nsd, fname,
			ixfr_number)) {
#ifdef MEMCLEAN /* otherwise, the OS collects memory pages */
			namedb_close(nsd->db);
			region_destroy(nsd->options->region);
			ixfr_create_free(ixfrcr);
#endif
			error("could not create IXFR");
		}
		printf("zone %s created IXFR %s.ixfr\n", name, fname);
		ixfr_create_free(ixfrcr);
	}
	if (out) {
		print_rrs(out, zone);
		printf("; ");
	}
	printf("zone %s is ok\n", name);
	namedb_close(nsd->db);
}

/* dummy functions to link */
int writepid(struct nsd * ATTR_UNUSED(nsd))
{
	        return 0;
}
void unlinkpid(const char * ATTR_UNUSED(file), const char* ATTR_UNUSED(username))
{
}
void bind8_stats(struct nsd * ATTR_UNUSED(nsd))
{
}

void sig_handler(int ATTR_UNUSED(sig))
{
}

extern char *optarg;
extern int optind;

int
main(int argc, char *argv[])
{
	/* Scratch variables... */
	int c;
	int print_zone = 0;
	uint32_t ixfr_number = IXFR_NUMBER_DEFAULT;
	uint64_t ixfr_size = IXFR_SIZE_DEFAULT;
	char* oldzone = NULL;
	struct nsd nsd;
	memset(&nsd, 0, sizeof(nsd));

	log_init("nsd-checkzone");

	/* Parse the command line... */
	while ((c = getopt(argc, argv, "hi:n:ps:")) != -1) {
		switch (c) {
		case 'h':
			usage();
			exit(0);
		case 'i':
			oldzone = optarg;
			break;
		case 'n':
			ixfr_number = (uint32_t)atoi(optarg);
			break;
		case 'p':
			print_zone = 1;
			break;
		case 's':
			ixfr_size = (uint64_t)atoi(optarg);
			break;
		case '?':
		default:
			usage();
			exit(1);
		}
	}
	argc -= optind;
	argv += optind;

	/* Commandline parse error */
	if (argc != 2) {
		fprintf(stderr, "wrong number of arguments.\n");
		usage();
		exit(1);
	}

	nsd.options = nsd_options_create(region_create_custom(xalloc, free,
		DEFAULT_CHUNK_SIZE, DEFAULT_LARGE_OBJECT_SIZE,
		DEFAULT_INITIAL_CLEANUP_SIZE, 1));
	if (verbosity == 0)
		verbosity = nsd.options->verbosity;

	check_zone(&nsd, argv[0], argv[1], print_zone ? stdout : NULL,
		oldzone, ixfr_number, ixfr_size);
	region_destroy(nsd.options->region);
	/* yylex_destroy(); but, not available in all versions of flex */

	exit(0);
}
