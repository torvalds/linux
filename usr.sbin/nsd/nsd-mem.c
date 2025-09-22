/*
 * nsd-mem.c -- nsd-mem(8)
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
#include "tsig.h"
#include "options.h"
#include "namedb.h"
#include "difffile.h"
#include "util.h"
#include "ixfr.h"

struct nsd nsd;

/*
 * Print the help text.
 *
 */
static void
usage (void)
{
	fprintf(stderr, "Usage: nsd-mem [-c configfile]\n");
	fprintf(stderr, "Version %s. Report bugs to <%s>.\n",
		PACKAGE_VERSION, PACKAGE_BUGREPORT);
}

/* zone memory structure */
struct zone_mem {
	/* size of data (allocated in db.region) */
	size_t data;
	/* unused space (in db.region) due to alignment */
	size_t data_unused;

	/* count of number of domains */
	size_t domaincount;
};

/* total memory structure */
struct tot_mem {
	/* size of data (allocated in db.region) */
	size_t data;
	/* unused space (in db.region) due to alignment */
	size_t data_unused;

	/* count of number of domains */
	size_t domaincount;

	/* options data */
	size_t opt_data;
	/* unused in options region */
	size_t opt_unused;
	/* dname compression table */
	size_t compresstable;
#ifdef RATELIMIT
	/* size of rrl tables */
	size_t rrl;
#endif

	/* total ram usage */
	size_t ram;
};

static void
account_zone(struct namedb* db, struct zone_mem* zmem)
{
	zmem->data = region_get_mem(db->region);
	zmem->data_unused = region_get_mem_unused(db->region);
	zmem->domaincount = domain_table_count(db->domains);
}

static void
pretty_mem(size_t x, const char* s)
{
	char buf[32];
	memset(buf, 0, sizeof(buf));
	if(snprintf(buf, sizeof(buf), "%12lld", (long long)x) > 12) {
		printf("%12lld %s\n", (long long)x, s);
		return;
	}
	printf("%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c %s\n",
		buf[0], buf[1], buf[2], (buf[2]==' '?' ':'.'),
		buf[3], buf[4], buf[5], (buf[5]==' '?' ':'.'),
		buf[6], buf[7], buf[8], (buf[8]==' '?' ':'.'),
		buf[9], buf[10], buf[11], s);
}

static void
print_zone_mem(struct zone_mem* z)
{
	pretty_mem(z->data, "zone data");
	pretty_mem(z->data_unused, "zone unused space (due to alignment)");
}

static void
account_total(struct nsd_options* opt, struct tot_mem* t)
{
	t->opt_data = region_get_mem(opt->region);
	t->opt_unused = region_get_mem_unused(opt->region);
	t->compresstable = sizeof(uint16_t) *
		(t->domaincount + 1 + EXTRA_DOMAIN_NUMBERS);
	t->compresstable *= opt->server_count;

#ifdef RATELIMIT
#define SIZE_RRL_BUCKET (8 + 4 + 4 + 4 + 4 + 2)
	t->rrl = opt->rrl_size * SIZE_RRL_BUCKET;
	t->rrl *= opt->server_count;
#endif

	t->ram = t->data + t->data_unused + t->opt_data + t->opt_unused +
		t->compresstable;
#ifdef RATELIMIT
	t->ram += t->rrl;
#endif
}

static void
print_tot_mem(struct tot_mem* t)
{
	printf("\ntotal\n");
	pretty_mem(t->data, "data");
	pretty_mem(t->data_unused, "unused space (due to alignment)");
	pretty_mem(t->opt_data, "options");
	pretty_mem(t->opt_unused, "options unused space (due to alignment)");
	pretty_mem(t->compresstable, "name table (depends on servercount)");
#ifdef RATELIMIT
	pretty_mem(t->rrl, "RRL table (depends on servercount)");
#endif
	printf("\nsummary\n");

	pretty_mem(t->ram, "ram usage (excl space for buffers)");
}

static void
add_mem(struct tot_mem* t, struct zone_mem* z)
{
	t->data += z->data;
	t->data_unused += z->data_unused;
	t->domaincount += z->domaincount;
}

static void
check_zone_mem(const char* tf, struct zone_options* zo,
	struct nsd_options* opt, struct tot_mem* totmem)
{
	struct nsd nsd;
	struct namedb* db;
	const dname_type* dname = (const dname_type*)zo->node.key;
	zone_type* zone;
	struct udb_base* taskudb;
	udb_ptr last_task;
	struct zone_mem zmem;

	printf("zone %s\n", zo->name);

	/* init*/
	memset(&zmem, 0, sizeof(zmem));
	memset(&nsd, 0, sizeof(nsd));
	nsd.region = region_create(xalloc, free);
	nsd.db = db = namedb_open(opt);
	if(!db) error("cannot open namedb");
	zone = namedb_zone_create(db, dname, zo);
	taskudb = task_file_create(tf);
	udb_ptr_init(&last_task, taskudb);

	/* read the zone */
	namedb_read_zonefile(&nsd, zone, taskudb, &last_task);

	/* account the memory for this zone */
	account_zone(db, &zmem);

	/* pretty print the memory for this zone */
	print_zone_mem(&zmem);

	/* delete the zone from memory */
	zone_ixfr_free(zone->ixfr);
	namedb_close(db);
	udb_base_free(taskudb);
	unlink(tf);
	region_destroy(nsd.region);

	/* add up totals */
	add_mem(totmem, &zmem);
}

static void
check_mem(struct nsd_options* opt)
{
	struct tot_mem totmem;
	struct zone_options* zo;
	char tf[512];
	memset(&totmem, 0, sizeof(totmem));
	snprintf(tf, sizeof(tf), "./nsd-mem-task-%u.db", (unsigned)getpid());

	/* read all zones and account memory */
	RBTREE_FOR(zo, struct zone_options*, opt->zone_options) {
		check_zone_mem(tf, zo, opt, &totmem);
	}

	/* calculate more total statistics */
	account_total(opt, &totmem);
	/* print statistics */
	print_tot_mem(&totmem);

	nsd_options_destroy(opt);
}

/* dummy functions to link */
struct nsd;
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
	struct nsd nsd;
	const char *configfile = CONFIGFILE;
	memset(&nsd, 0, sizeof(nsd));

	log_init("nsd-mem");

	/* Parse the command line... */
	while ((c = getopt(argc, argv, "c:h"
		)) != -1) {
		switch (c) {
		case 'c':
			configfile = optarg;
			break;
		case 'h':
			usage();
			exit(0);
		case '?':
		default:
			usage();
			exit(1);
		}
	}
	argc -= optind;
	/* argv += optind; move along argv for positional arguments */

	/* Commandline parse error */
	if (argc != 0) {
		usage();
		exit(1);
	}

	/* Read options */
	nsd.options = nsd_options_create(region_create_custom(xalloc, free,
		DEFAULT_CHUNK_SIZE, DEFAULT_LARGE_OBJECT_SIZE,
		DEFAULT_INITIAL_CLEANUP_SIZE, 1));
	tsig_init(nsd.options->region);
	if(!parse_options_file(nsd.options, configfile, NULL, NULL, NULL)) {
		error("could not read config: %s\n", configfile);
	}
	if(!parse_zone_list_file(nsd.options)) {
		error("could not read zonelist file %s\n",
			nsd.options->zonelistfile);
	}
	if (verbosity == 0)
		verbosity = nsd.options->verbosity;

#ifdef HAVE_CHROOT
	if(nsd.chrootdir == 0) nsd.chrootdir = nsd.options->chroot;
#ifdef CHROOTDIR
	/* if still no chrootdir, fallback to default */
	if(nsd.chrootdir == 0) nsd.chrootdir = CHROOTDIR;
#endif /* CHROOTDIR */
#endif /* HAVE_CHROOT */
	if(nsd.options->zonesdir && nsd.options->zonesdir[0]) {
		if(chdir(nsd.options->zonesdir)) {
			error("cannot chdir to '%s': %s",
				nsd.options->zonesdir, strerror(errno));
		}
		DEBUG(DEBUG_IPC,1, (LOG_INFO, "changed directory to %s",
			nsd.options->zonesdir));
	}

	/* Chroot */
#ifdef HAVE_CHROOT
	if (nsd.chrootdir && strlen(nsd.chrootdir)) {
		if(chdir(nsd.chrootdir)) {
			error("unable to chdir to chroot: %s", strerror(errno));
		}
		DEBUG(DEBUG_IPC,1, (LOG_INFO, "changed root directory to %s",
			nsd.chrootdir));
	}
#endif /* HAVE_CHROOT */

	check_mem(nsd.options);

	exit(0);
}
