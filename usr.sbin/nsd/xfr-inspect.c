/* xfr-inspect - list the contents and inspect an zone transfer XFR file
 * By W.C.A. Wijngaards
 * Copyright 2017, NLnet Labs.
 * BSD, see LICENSE.
 */

#include "config.h"
#include "util.h"
#include "buffer.h"
#include "packet.h"
#include "rdata.h"
#include "namedb.h"
#include "difffile.h"
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

/** verbosity for inspect */
static int v = 0;
/** shorthand for ease */
#ifdef ULL
#undef ULL
#endif
#define ULL (unsigned long long)

/** print usage text */
static void
usage(void)
{
	printf("usage:	xfr-inspect [options] file\n");
	printf(" -h		this help\n");
	printf(" -v		increase verbosity: "
	       "with -v(list chunks), -vv(inside chunks)\n");
	printf(" -l		list contents of transfer\n");
}

static int
xi_diff_read_64(FILE *in, uint64_t* result)
{
	if (fread(result, sizeof(*result), 1, in) == 1) {
		return 1;
	} else {
		return 0;
	}
}

static int
xi_diff_read_32(FILE *in, uint32_t* result)
{
	if (fread(result, sizeof(*result), 1, in) == 1) {
		*result = ntohl(*result);
		return 1;
	} else {
		return 0;
	}
}

static int
xi_diff_read_8(FILE *in, uint8_t* result)
{
        if (fread(result, sizeof(*result), 1, in) == 1) {
                return 1;
        } else {
                return 0;
        }
}

static int
xi_diff_read_str(FILE* in, char* buf, size_t len)
{
	uint32_t disklen;
	if(!xi_diff_read_32(in, &disklen))
		return 0;
	if(disklen >= len)
		return 0;
	if(fread(buf, disklen, 1, in) != 1)
		return 0;
	buf[disklen] = 0;
	return 1;
}


/** inspect header of xfr file, return num_parts */
static int
inspect_header(FILE* in)
{
	char zone_buf[3072];
	char patname_buf[2048];

	uint32_t old_serial, new_serial, num_parts, type;
	uint64_t time_end_0, time_start_0;
	uint32_t time_end_1, time_start_1;
	uint8_t committed;

	time_t time_end, time_start;

	if(!xi_diff_read_32(in, &type)) {
		printf("could not read type, file short\n");
		fclose(in);
		exit(1);
	}
	if(type != DIFF_PART_XFRF) {
		printf("type:	%x (BAD FILE TYPE)\n", type);
		fclose(in);
		exit(1);
	}
	if(!xi_diff_read_8(in, &committed) ||
		!xi_diff_read_32(in, &num_parts) ||
		!xi_diff_read_64(in, &time_end_0) ||
		!xi_diff_read_32(in, &time_end_1) ||
		!xi_diff_read_32(in, &old_serial) ||
		!xi_diff_read_32(in, &new_serial) ||
		!xi_diff_read_64(in, &time_start_0) ||
		!xi_diff_read_32(in, &time_start_1) ||
		!xi_diff_read_str(in, zone_buf, sizeof(zone_buf)) ||
		!xi_diff_read_str(in, patname_buf, sizeof(patname_buf))) {
		printf("diff file bad commit part, file too short");
		fclose(in);
		exit(1);
	}
	time_end = (time_t)time_end_0;
	time_start = (time_t)time_start_0;

	/* printf("type:		%x\n", (int)type); */
	printf("committed:	%d (%s)\n", (int)committed,
		committed?"yes":"no");
	printf("num_parts:	%d\n", (int)num_parts);
	printf("time_end:	%d.%6.6d %s", (int)time_end_0,
		(int)time_end_1, ctime(&time_end));
	printf("old_serial:	%u\n", (unsigned)old_serial);
	printf("new_serial:	%u\n", (unsigned)new_serial);
	printf("time_start:	%d.%6.6d %s", (int)time_start_0,
		(int)time_start_1, ctime(&time_start));
	printf("zone:		%s\n", zone_buf);
	printf("patname:	%s\n", patname_buf);

	return num_parts;
}

/** print records in packet */
static void
print_records(region_type* region, buffer_type* pkt, int num, int qsection)
{
	domain_table_type* table;
	int i;
	rr_type* rr;
	region_type* tmpregion = region_create(xalloc, free);
	buffer_type* tmpbuf;
	if(!tmpregion) {
		printf("out of memory\n");
		return;
	}
	tmpbuf = buffer_create(region, QIOBUFSZ);
	if(!tmpbuf) {
		printf("out of memory\n");
		return;
	}
	table = domain_table_create(tmpregion);
	if(!table) {
		printf("out of memory\n");
		return;
	}

	for(i=0; i<num; ++i) {
		rr = packet_read_rr(region, table, pkt, qsection);
		if(!rr) {
			printf("; cannot read rr %d\n", i);
			return;
		}
		if(qsection) {
			printf("%s", dname_to_string(domain_dname(rr->owner),
				NULL));
			printf("\t%s", rrclass_to_string(rr->klass));
			if(rr->type == TYPE_IXFR)
				printf("\tIXFR\n");
			else if(rr->type == TYPE_AXFR)
				printf("\tAXFR\n");
			else printf("\t%s\n", rrtype_to_string(rr->type));
		} else {
			if(!print_rr(stdout, NULL, rr, tmpregion, tmpbuf)) {
				printf("; cannot print rr %d\n", i);
			}
		}
	}
	region_destroy(tmpregion);
}

/** inspect packet (IXFR or AXFR) */
static void
inspect_packet(region_type* region, buffer_type* pkt)
{
	printf("\n");
	if(buffer_limit(pkt) < QHEADERSZ) {
		printf("packet too short\n");
		return;
	}
	printf("; id=%4.4x ; flags%s%s%s%s%s%s%s%s ; rcode %s ; opcode %d\n",
		ID(pkt), QR(pkt)?" QR":"", AA(pkt)?" AA":"", TC(pkt)?" TC":"",
		RD(pkt)?" RD":"", RA(pkt)?" RA":"", Z(pkt)?" Z":"",
		AD(pkt)?" AD":"", CD(pkt)?" CD":"", rcode2str(RCODE(pkt)),
		OPCODE(pkt));
	printf("; qdcount %d ; ancount %d ; nscount %d ; arcount %d\n",
		QDCOUNT(pkt), ANCOUNT(pkt), NSCOUNT(pkt), ARCOUNT(pkt));
	buffer_skip(pkt, QHEADERSZ);

	if(QDCOUNT(pkt) != 0) {
		printf("; QUESTION SECTION\n");
		print_records(region, pkt, QDCOUNT(pkt), 1);
	}
	if(ANCOUNT(pkt) != 0) {
		printf("; ANSWER SECTION\n");
		print_records(region, pkt, ANCOUNT(pkt), 0);
	}
	if(NSCOUNT(pkt) != 0) {
		printf("; AUTHORITY SECTION\n");
		print_records(region, pkt, NSCOUNT(pkt), 0);
	}
	if(ARCOUNT(pkt) != 0) {
		printf("; ADDITIONAL SECTION\n");
		print_records(region, pkt, ARCOUNT(pkt), 0);
	}
}

/** inspect part of xfr file */
static void
inspect_part(FILE* in, int partnum)
{
	uint32_t pkttype, msglen, msglen2;
	region_type* region;
	buffer_type* packet;
	region = region_create(xalloc, free);
	if(!region) {
		printf("out of memory\n");
		fclose(in);
		exit(1);
	}
	packet = buffer_create(region, QIOBUFSZ);
	if(!xi_diff_read_32(in, &pkttype)) {
		printf("cannot read part %d\n", partnum);
		fclose(in);
		exit(1);
	}
	if(pkttype != DIFF_PART_XXFR) {
		printf("bad part %d: not type XXFR\n", partnum);
		fclose(in);
		exit(1);
	}
	if(!xi_diff_read_32(in, &msglen)) {
		printf("bad part %d: not msglen, file too short\n", partnum);
		fclose(in);
		exit(1);
	}
	if(msglen < QHEADERSZ || msglen > QIOBUFSZ) {
		printf("bad part %d: msglen %u (too short or too long)\n",
			partnum, (unsigned)msglen);
		fclose(in);
		exit(1);
	}
	if(fread(buffer_begin(packet), msglen, 1, in) != 1) {
		printf("bad part %d: short packet, file too short, %s\n",
			partnum, strerror(errno));
		fclose(in);
		exit(1);
	}
	if(!xi_diff_read_32(in, &msglen2)) {
		printf("bad part %d: cannot read msglen2, file too short\n", partnum);
		fclose(in);
		exit(1);
	}
	if(v==0) {
		region_destroy(region);
		return;
	}

	printf("\n");
	/* printf("type	: %x\n", pkttype); */
	printf("part	: %d\n", partnum);
	printf("msglen	: %u\n", (unsigned)msglen);
	printf("msglen2	: %u (%s)\n", (unsigned)msglen2,
		(msglen==msglen2)?"ok":"wrong");

	if(v>=2) {
		buffer_set_limit(packet, msglen);
		inspect_packet(region, packet);
	}

	region_destroy(region);
}

/** inspect parts of xfr file */
static void
inspect_parts(FILE* in, int num)
{
	int i;
	for(i=0; i<num; i++) {
		inspect_part(in, i);
	}
}

/** inspect trail of xfr file */
static void
inspect_trail(FILE* in)
{
	char log_buf[5120];
	if(!xi_diff_read_str(in, log_buf, sizeof(log_buf))) {
		printf("bad trail: cannot read log string\n");
		fclose(in);
		exit(1);
	}
	printf("\n");
	printf("log:	%s\n", log_buf);
}

/** inspect contents of xfr file */
static void
inspect_file(char* fname)
{
	FILE* in;
	int num;
	log_init("udb-inspect");
	if(!(in=fopen(fname, "r"))) {
		printf("cannot open %s: %s\n", fname, strerror(errno));
		exit(1);
	}
	printf("file:	%s\n", fname);
	num = inspect_header(in);
	inspect_parts(in, num);
	inspect_trail(in);
	fclose(in);
}

/** list header of xfr file, return num_parts */
static int
list_header(FILE* in)
{
	char zone_buf[3072];
	char patname_buf[2048];

	uint32_t old_serial, new_serial, num_parts, type;
	uint64_t time_end_0, time_start_0;
	uint32_t time_end_1, time_start_1;
	uint8_t committed;

	time_t time_end, time_start;

	if(!xi_diff_read_32(in, &type)) {
		printf("could not read type, file short\n");
		fclose(in);
		exit(1);
	}
	if(type != DIFF_PART_XFRF) {
		printf("type:	%x (BAD FILE TYPE)\n", type);
		fclose(in);
		exit(1);
	}
	if(!xi_diff_read_8(in, &committed) ||
		!xi_diff_read_32(in, &num_parts) ||
		!xi_diff_read_64(in, &time_end_0) ||
		!xi_diff_read_32(in, &time_end_1) ||
		!xi_diff_read_32(in, &old_serial) ||
		!xi_diff_read_32(in, &new_serial) ||
		!xi_diff_read_64(in, &time_start_0) ||
		!xi_diff_read_32(in, &time_start_1) ||
		!xi_diff_read_str(in, zone_buf, sizeof(zone_buf)) ||
		!xi_diff_read_str(in, patname_buf, sizeof(patname_buf))) {
		printf("diff file bad commit part, file too short");
		fclose(in);
		exit(1);
	}
	time_end = (time_t)time_end_0;
	time_start = (time_t)time_start_0;

	/* printf("; type:		%x\n", (int)type); */
	printf("; committed:	%d (%s)\n", (int)committed,
		committed?"yes":"no");
	printf("; num_parts:	%d\n", (int)num_parts);
	printf("; time_end:	%d.%6.6d %s", (int)time_end_0,
		(int)time_end_1, ctime(&time_end));
	printf("; old_serial:	%u\n", (unsigned)old_serial);
	printf("; new_serial:	%u\n", (unsigned)new_serial);
	printf("; time_start:	%d.%6.6d %s", (int)time_start_0,
		(int)time_start_1, ctime(&time_start));
	printf("; zone:		%s\n", zone_buf);
	printf("; patname:	%s\n", patname_buf);

	return num_parts;
}

/** list packet (IXFR or AXFR) */
static void
list_packet(region_type* region, buffer_type* pkt, int partnum)
{
	if(buffer_limit(pkt) < QHEADERSZ) {
		printf("packet too short\n");
		return;
	}
	buffer_skip(pkt, QHEADERSZ);

	if(partnum == 0 && QDCOUNT(pkt) == 1) {
		/* print query AXFR or IXFR */
		printf("; ");
		print_records(region, pkt, QDCOUNT(pkt), 1);
	}
	if(ANCOUNT(pkt) != 0) {
		print_records(region, pkt, ANCOUNT(pkt), 0);
	}
}

/** list part of xfr file */
static void
list_part(FILE* in, int partnum)
{
	uint32_t pkttype, msglen, msglen2;
	region_type* region;
	buffer_type* packet;
	region = region_create(xalloc, free);
	if(!region) {
		printf("out of memory\n");
		fclose(in);
		exit(1);
	}
	packet = buffer_create(region, QIOBUFSZ);
	if(!xi_diff_read_32(in, &pkttype)) {
		printf("cannot read part %d\n", partnum);
		fclose(in);
		exit(1);
	}
	if(pkttype != DIFF_PART_XXFR) {
		printf("bad part %d: not type XXFR\n", partnum);
		fclose(in);
		exit(1);
	}
	if(!xi_diff_read_32(in, &msglen)) {
		printf("bad part %d: not msglen, file too short\n", partnum);
		fclose(in);
		exit(1);
	}
	if(msglen < QHEADERSZ || msglen > QIOBUFSZ) {
		printf("bad part %d: msglen %u (too short or too long)\n",
			partnum, (unsigned)msglen);
		fclose(in);
		exit(1);
	}
	if(fread(buffer_begin(packet), msglen, 1, in) != 1) {
		printf("bad part %d: short packet, file too short, %s\n",
			partnum, strerror(errno));
		fclose(in);
		exit(1);
	}
	if(!xi_diff_read_32(in, &msglen2)) {
		printf("bad part %d: cannot read msglen2, file too short\n", partnum);
		fclose(in);
		exit(1);
	}

	buffer_set_limit(packet, msglen);
	list_packet(region, packet, partnum);
	region_destroy(region);
}

/** list parts of xfr file */
static void
list_parts(FILE* in, int num)
{
	int i;
	for(i=0; i<num; i++) {
		list_part(in, i);
	}
}

/** list contents of xfr file */
static void
list_file(char* fname)
{
	FILE* in;
	int num;
	log_init("udb-inspect");
	if(!(in=fopen(fname, "r"))) {
		printf("cannot open %s: %s\n", fname, strerror(errno));
		exit(1);
	}
	num = list_header(in);
	list_parts(in, num);

	fclose(in);
}

/** getopt global, in case header files fail to declare it. */
extern int optind;
/** getopt global, in case header files fail to declare it. */
extern char* optarg;

/**
 * main program. Set options given commandline arguments.
 * @param argc: number of commandline arguments.
 * @param argv: array of commandline arguments.
 * @return: exit status of the program.
 */
int
main(int argc, char* argv[])
{
	int c, list=0;
	while( (c=getopt(argc, argv, "hlv")) != -1) {
		switch(c) {
		case 'l':
			list=1;
			break;
		case 'v':
			v++;
			break;
		default:
		case 'h':
			usage();
			return 1;
		}
	}
	argc -= optind;
	argv += optind;
	if(argc != 1) {
		usage();
		return 1;
	}
	if(list) list_file(argv[0]);
	else	inspect_file(argv[0]);

	return 0;
}
