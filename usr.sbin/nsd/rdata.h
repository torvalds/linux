/*
 * rdata.h -- RDATA conversion functions.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#ifndef RDATA_H
#define RDATA_H

#include "dns.h"
#include "namedb.h"

/* High bit of the APL length field is the negation bit.  */
#define APL_NEGATION_MASK      0x80U
#define APL_LENGTH_MASK	       (~APL_NEGATION_MASK)

extern lookup_table_type dns_certificate_types[];
extern lookup_table_type dns_algorithms[];
extern const char *svcparamkey_strs[];

int rdata_atom_to_string(buffer_type *output, rdata_zoneformat_type type,
			 rdata_atom_type rdata, rr_type *rr);

/*
 * Split the wireformat RDATA into an array of rdata atoms. Domain
 * names are inserted into the OWNERS table. The number of rdata atoms
 * is returned and the array itself is allocated in REGION and stored
 * in RDATAS.
 *
 * Returns -1 on failure.
 */
ssize_t rdata_wireformat_to_rdata_atoms(region_type *region,
					domain_table_type *owners,
					uint16_t rrtype,
					uint16_t rdata_size,
					buffer_type *packet,
					rdata_atom_type **rdatas);

/*
 * Calculate the maximum size of the rdata assuming domain names are
 * not compressed.
 */
size_t rdata_maximum_wireformat_size(rrtype_descriptor_type *descriptor,
				     size_t rdata_count,
				     rdata_atom_type *rdatas);

int rdata_atoms_to_unknown_string(buffer_type *out,
				  rrtype_descriptor_type *descriptor,
				  size_t rdata_count,
				  rdata_atom_type *rdatas);

/* print rdata to a text string (as for a zone file) returns 0
  on a failure (bufpos is reset to original position).
  returns 1 on success, bufpos is moved. */
int print_rdata(buffer_type *output, rrtype_descriptor_type *descriptor,
            rr_type *record);

#endif /* RDATA_H */
