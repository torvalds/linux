/*
 * difffile.h - nsd.diff file handling header file. Read/write diff files.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */
#ifndef DIFFFILE_H
#define DIFFFILE_H

#include "rbtree.h"
#include "namedb.h"
#include "options.h"
#include "udb.h"
struct nsd;
struct nsdst;

#define DIFF_PART_XXFR ('X'<<24 | 'X'<<16 | 'F'<<8 | 'R')
#define DIFF_PART_XFRF ('X'<<24 | 'F'<<16 | 'R'<<8 | 'F')

#define DIFF_NOT_COMMITTED (0u) /* XFR not (yet) committed to disk */
#define DIFF_COMMITTED (1u<<0) /* XFR committed to disk */
#define DIFF_CORRUPT (1u<<1) /* XFR corrupt */
#define DIFF_INCONSISTENT (1u<<2) /* IXFR cannot be applied */
#define DIFF_VERIFIED (1u<<3) /* XFR already verified */

/* write an xfr packet data to the diff file, type=IXFR.
   The diff file is created if necessary, with initial header(notcommitted). */
void diff_write_packet(const char* zone, const char* pat, uint32_t old_serial,
	uint32_t new_serial, uint32_t seq_nr, uint8_t* data, size_t len,
	struct nsd* nsd, uint64_t filenumber);

/*
 * Overwrite header of diff file with committed vale and other data.
 * append log string.
 */
void diff_write_commit(const char* zone, uint32_t old_serial,
	uint32_t new_serial, uint32_t num_parts, uint8_t commit,
	const char* log_msg, struct nsd* nsd, uint64_t filenumber);

/*
 * Overwrite committed value of diff file with discarded to ensure diff
 * file is not reapplied on reload.
 */
void diff_update_commit(const char* zone,
	uint8_t commit, struct nsd* nsd, uint64_t filenumber);

/*
 * These functions read parts of the diff file.
 */
int diff_read_32(FILE *in, uint32_t* result);
int diff_read_8(FILE *in, uint8_t* result);
int diff_read_str(FILE* in, char* buf, size_t len);

/* delete the RRs for a zone from memory */
void delete_zone_rrs(namedb_type* db, zone_type* zone);
/* delete an RR */
int delete_RR(namedb_type* db, const dname_type* dname,
	uint16_t type, uint16_t klass,
	buffer_type* packet, size_t rdatalen, zone_type *zone,
	region_type* temp_region, int* softfail);
/* add an RR */
int add_RR(namedb_type* db, const dname_type* dname,
	uint16_t type, uint16_t klass, uint32_t ttl,
	buffer_type* packet, size_t rdatalen, zone_type *zone,
	int* softfail);

/* apply the xfr file identified by xfrfilenr to zone */
int apply_ixfr_for_zone(struct nsd* nsd, zone_type* zone, FILE* in,
        struct nsd_options* opt, udb_base* taskudb, uint32_t xfrfilenr);

enum soainfo_hint {
	soainfo_ok,
	soainfo_gone,
	soainfo_bad
};

/* task udb structure */
struct task_list_d {
	/** next task in list */
	udb_rel_ptr next;
	/** task type */
	enum {
		/** expire or un-expire a zone */
		task_expire,
		/** apply an ixfr or axfr to a zone */
		task_apply_xfr,
		/** soa info for zone */
		task_soa_info,
		/** check mtime of zonefiles and read them, done on SIGHUP */
		task_check_zonefiles,
		/** write zonefiles (if changed) */
		task_write_zonefiles,
		/** set verbosity */
		task_set_verbosity,
		/** add a zone */
		task_add_zone,
		/** delete zone */
		task_del_zone,
		/** add TSIG key */
		task_add_key,
		/** delete TSIG key */
		task_del_key,
		/** add pattern */
		task_add_pattern,
		/** delete pattern */
		task_del_pattern,
		/** options change */
		task_opt_change,
		/** zonestat increment */
		task_zonestat_inc,
		/** cookies */
		task_cookies,
	} task_type;
	uint32_t size; /* size of this struct */

	/** soainfo: zonename dname, soaRR wireform, yesno is soainfo_hint */
	/** expire: zonename, boolyesno */
	/** apply_xfr: zonename, serials, yesno is filenamecounter */
	uint32_t oldserial, newserial;
	/** general variable.  for some used to see if zname is present. */
	uint64_t yesno;
	struct dname zname[0];
};
#define TASKLIST(ptr) ((struct task_list_d*)UDB_PTR(ptr))
/** create udb for tasks */
struct udb_base* task_file_create(const char* file);
void task_remap(udb_base* udb);
void task_process_sync(udb_base* udb);
void task_clear(udb_base* udb);
void task_new_soainfo(udb_base* udb, udb_ptr* last, struct zone* z, enum soainfo_hint hint);
void task_new_expire(udb_base* udb, udb_ptr* last,
	const struct dname* z, int expired);
void task_new_check_zonefiles(udb_base* udb, udb_ptr* last,
	const dname_type* zone);
void task_new_write_zonefiles(udb_base* udb, udb_ptr* last,
	const dname_type* zone);
void task_new_set_verbosity(udb_base* udb, udb_ptr* last, int v);
void task_new_add_zone(udb_base* udb, udb_ptr* last, const char* zone,
	const char* pattern, unsigned zonestatid);
void task_new_del_zone(udb_base* udb, udb_ptr* last, const dname_type* dname);
void task_new_add_key(udb_base* udb, udb_ptr* last, struct key_options* key);
void task_new_del_key(udb_base* udb, udb_ptr* last, const char* name);
void task_new_add_pattern(udb_base* udb, udb_ptr* last,
	struct pattern_options* p);
void task_new_del_pattern(udb_base* udb, udb_ptr* last, const char* name);
void task_new_opt_change(udb_base* udb, udb_ptr* last, struct nsd_options* opt);
void task_new_zonestat_inc(udb_base* udb, udb_ptr* last, unsigned sz);
void task_new_cookies(udb_base* udb, udb_ptr* last, int answer_cookie,
		size_t cookie_count, void* cookie_secrets);
int task_new_apply_xfr(udb_base* udb, udb_ptr* last, const dname_type* zone,
	uint32_t old_serial, uint32_t new_serial, uint64_t filenumber);
void task_process_apply_xfr(struct nsd* nsd, udb_base* udb, udb_ptr *task);
void task_process_in_reload(struct nsd* nsd, udb_base* udb, udb_ptr *last_task,
	udb_ptr* task);
void task_process_expire(namedb_type* db, struct task_list_d* task);

#endif /* DIFFFILE_H */
