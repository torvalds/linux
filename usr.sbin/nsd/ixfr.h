/*
 * ixfr.h -- generating IXFR responses.
 *
 * Copyright (c) 2021, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#ifndef IXFR_H
#define IXFR_H
struct nsd;
#include "query.h"
#include "rbtree.h"
struct ixfr_data;
struct zone;
struct buffer;
struct region;

/* default for number of ixfr versions in files */
#define IXFR_NUMBER_DEFAULT 5 /* number of versions */
/* default for IXFR storage */
#define IXFR_SIZE_DEFAULT 1048576 /* in bytes, 1M */

/* data structure that stores IXFR contents for a zone. */
struct zone_ixfr {
	/* Items are of type ixfr_data. The key is old_serial.
	 * So it can be looked up on an incoming IXFR. They are sorted
	 * by old_serial, so the looked up and next are the versions needed.
	 * Tree of ixfr data for versions */
	struct rbtree* data;
	/* total size stored at this time, in bytes,
	 * sum of sizes of the ixfr data elements */
	size_t total_size;
	/* the oldest serial number in the tree, searchable by old_serial */
	uint32_t oldest_serial;
	/* the newest serial number in the tree, that is searchable in the
	 * tree, so it is the old_serial of the newest data entry, that
	 * has an even newer new_serial of that entry */
	uint32_t newest_serial;
};

/* Data structure that stores one IXFR.
 * The RRs are stored in uncompressed wireformat, that means
 * an uncompressed domain name, type, class, TTL, rdatalen,
 * uncompressed rdata in wireformat.
 *
 * The data structure is formatted like this so that making an IXFR
 * that moves across several versions can be done by collating the
 * pieces precisely from the versions involved. In particular, for
 * an IXFR from olddata to newdata, for a combined output:
 * newdata.newsoa olddata.oldsoa olddata.del olddata.add
 * newdata.del newdata.add
 * in sequence should produce a valid, non-condensed, IXFR with multiple
 * versions inside.
 */
struct ixfr_data {
	/* Node in the rbtree. Key is oldserial */
	struct rbnode node;
	/* from what serial the IXFR starts from, the 'old' serial */
	uint32_t oldserial;
	/* where to IXFR goes to, the 'new' serial */
	uint32_t newserial;
	/* the new SOA record, with newserial */
	uint8_t* newsoa;
	/* byte length of the uncompressed wireformat RR in newsoa */
	size_t newsoa_len;
	/* the old SOA record, with oldserial */
	uint8_t* oldsoa;
	/* byte length of the uncompressed wireformat RR in oldsoa*/
	size_t oldsoa_len;
	/* the deleted RRs, ends with the newserial SOA record.
	 * if the ixfr is collated out multiple versions, then
	 * this deleted RRs section contains several add and del sections
	 * for the older versions, and ends with the last del section,
	 * and the SOA record with the newserial.
	 * That is everything except the final add section for newserial. */
	uint8_t* del;
	/* byte length of the uncompressed wireformat RRs in del */
	size_t del_len;
	/* the added RRs, ends with the newserial SOA record. */
	uint8_t* add;
	/* byte length of the uncompressed wireformat RRs in add */
	size_t add_len;
	/* log string (if not NULL) about where data is from */
	char* log_str;
	/* the number of the ixfr.<num> file on disk. If 0, there is no
	 * file. If 1, it is file ixfr<nothingafterit>. */
	int file_num;
};

/* process queries in IXFR state */
query_state_type query_ixfr(struct nsd *nsd, struct query *query);

/*
 * While an IXFR is processed, in incoming IXFR that is downloaded by NSD,
 * this structure keeps track of how to store the data from it. That data
 * can then be used to answer IXFR queries.
 *
 * The structure keeps track of allocation data for the IXFR records.
 * If it is cancelled, that is flagged so storage stops.
 */
struct ixfr_store {
	/* the zone info, with options and zone ixfr reference */
	struct zone* zone;
	/* are we cancelled, it is not an IXFR, no need to store information
	 * any more. */
	int cancelled;
	/* data has been trimmed and newsoa added */
	int data_trimmed;
	/* the ixfr data that we are storing into */
	struct ixfr_data* data;
	/* capacity for the delrrs storage, size of ixfr del allocation */
	size_t del_capacity;
	/* capacity for the addrrs storage, size of ixfr add allocation */
	size_t add_capacity;
};

/*
 * Start the storage of the IXFR data from this IXFR.
 * If it returns NULL, the IXFR storage stops. On malloc failure, the
 * storage is returned NULL, or cancelled if failures happen later on.
 *
 * When done, the finish routine links the data into the memory for the zone.
 * If it turns out to not be used, use the cancel routine. Or the free
 * routine if the ixfr_store itself needs to be deleted too, like on error.
 *
 * zone: the zone structure
 * ixfr_store_mem: preallocated by caller, used to allocate the store struct.
 * old_serial: the start serial of the IXFR.
 * new_serial: the end serial of the IXFR.
 * return NULL or a fresh ixfr_store structure for adding records to the
 * 	IXFR with this serial number. The NULL is on error.
 */
struct ixfr_store* ixfr_store_start(struct zone* zone,
	struct ixfr_store* ixfr_store_mem);

/*
 * Cancel the ixfr store in progress. The pointer remains valid, no store done.
 * ixfr_store: this is set to cancel.
 */
void ixfr_store_cancel(struct ixfr_store* ixfr_store);

/*
 * Free ixfr store structure, it is no longer used.
 * ixfr_store: deleted
 */
void ixfr_store_free(struct ixfr_store* ixfr_store);

/*
 * Finish ixfr store processing. Links the data into the zone ixfr data.
 * ixfr_store: Data is linked into the zone struct. The ixfr_store is freed.
 * nsd: nsd structure for allocation region and global options.
 * log_buf: log string for the update.
 */
void ixfr_store_finish(struct ixfr_store* ixfr_store, struct nsd* nsd,
	char* log_buf);

/* finish just the data activities, trim up the storage and append newsoa */
void ixfr_store_finish_data(struct ixfr_store* ixfr_store);

/*
 * Add the new SOA record to the ixfr store.
 * ixfr_store: stores ixfr data that is collected.
 * ttl: the TTL of the SOA record
 * packet: DNS packet that contains the SOA. position restored on function
 * 	exit.
 * rrlen: wire rdata length of the SOA.
 */
void ixfr_store_add_newsoa(struct ixfr_store* ixfr_store, uint32_t ttl,
	struct buffer* packet, size_t rrlen);

/*
 * Add the old SOA record to the ixfr store.
 * ixfr_store: stores ixfr data that is collected.
 * ttl: the TTL of the SOA record
 * packet: DNS packet that contains the SOA. position restored on function
 * 	exit.
 * rrlen: wire rdata length of the SOA.
 */
void ixfr_store_add_oldsoa(struct ixfr_store* ixfr_store, uint32_t ttl,
	struct buffer* packet, size_t rrlen);

void ixfr_store_delrr(struct ixfr_store* ixfr_store, const struct dname* dname,
	uint16_t type, uint16_t klass, uint32_t ttl, struct buffer* packet,
	uint16_t rrlen, struct region* temp_region);
void ixfr_store_addrr(struct ixfr_store* ixfr_store, const struct dname* dname,
	uint16_t type, uint16_t klass, uint32_t ttl, struct buffer* packet,
	uint16_t rrlen, struct region* temp_region);
int ixfr_store_addrr_rdatas(struct ixfr_store* ixfr_store,
	const struct dname* dname, uint16_t type, uint16_t klass,
	uint32_t ttl, rdata_atom_type* rdatas, ssize_t rdata_num);
int ixfr_store_delrr_uncompressed(struct ixfr_store* ixfr_store,
	uint8_t* dname, size_t dname_len, uint16_t type, uint16_t klass,
	uint32_t ttl, uint8_t* rdata, size_t rdata_len);
int ixfr_store_add_newsoa_rdatas(struct ixfr_store* ixfr_store,
	const struct dname* dname, uint16_t type, uint16_t klass,
	uint32_t ttl, rdata_atom_type* rdatas, ssize_t rdata_num);
int ixfr_store_oldsoa_uncompressed(struct ixfr_store* ixfr_store,
	uint8_t* dname, size_t dname_len, uint16_t type, uint16_t klass,
	uint32_t ttl, uint8_t* rdata, size_t rdata_len);

/* an AXFR has been received, the IXFRs do not connect in version number.
 * Delete the unconnected IXFRs from memory */
void ixfr_store_delixfrs(struct zone* zone);

/* return if the zone has ixfr storage enabled for it */
int zone_is_ixfr_enabled(struct zone* zone);

/* create new zone_ixfr structure */
struct zone_ixfr* zone_ixfr_create(struct nsd* nsd);

/* free the zone_ixfr */
void zone_ixfr_free(struct zone_ixfr* ixfr);

/* make space to fit in the data */
void zone_ixfr_make_space(struct zone_ixfr* ixfr, struct zone* zone,
	struct ixfr_data* data, struct ixfr_store* ixfr_store);

/* remove ixfr data from the zone_ixfr */
void zone_ixfr_remove(struct zone_ixfr* ixfr, struct ixfr_data* data);

/* add ixfr data to the zone_ixfr */
void zone_ixfr_add(struct zone_ixfr* ixfr, struct ixfr_data* data, int isnew);

/* find serial number in ixfr list, or NULL if not found */
struct ixfr_data* zone_ixfr_find_serial(struct zone_ixfr* ixfr,
	uint32_t qserial);

/* size of the ixfr data */
size_t ixfr_data_size(struct ixfr_data* data);

/* write ixfr contents to file for the zone */
void ixfr_write_to_file(struct zone* zone, const char* zfile);

/* read ixfr contents from file for the zone */
void ixfr_read_from_file(struct nsd* nsd, struct zone* zone, const char* zfile);

/* get the current serial from the zone */
uint32_t zone_get_current_serial(struct zone* zone);

/* write the ixfr data to file */
int ixfr_write_file(struct zone* zone, struct ixfr_data* data,
	const char* zfile, int file_num);

/* see if ixfr file exists */
int ixfr_file_exists(const char* zfile, int file_num);

/* rename the ixfr file */
int ixfr_rename_it(const char* zname, const char* zfile, int oldnum,
	int oldtemp, int newnum, int newtemp);

/* read the file header of an ixfr file and return serial numbers. */
int ixfr_read_file_header(const char* zname, const char* zfile,
	int file_num, uint32_t* oldserial, uint32_t* newserial,
	size_t* data_size, int enoent_is_err);

/* unlink an ixfr file */
int ixfr_unlink_it(const char* zname, const char* zfile, int file_num,
	int silent_enoent);

/* delete the ixfr files that are too many */
void ixfr_delete_superfluous_files(struct zone* zone, const char* zfile,
	int dest_num_files);

#endif /* IXFR_H */
