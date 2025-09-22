/*
 * testpkts. Data file parse for test packets, and query matching.
 *
 * Data storage for specially crafted replies for testing purposes.
 *
 * (c) NLnet Labs, 2005, 2006, 2007
 * See the file LICENSE for the license
 */

#ifndef TESTPKTS_H
#define TESTPKTS_H
struct sldns_buffer;
struct sldns_file_parse_state;

/**
 * \file
 * 
 * This is a debugging aid. It is not efficient, especially
 * with a long config file, but it can give any reply to any query.
 * This can help the developer pre-script replies for queries.
 *
 * You can specify a packet RR by RR with header flags to return.
 *
 * Missing features:
 *		- matching content different from reply content.
 *		- find way to adjust mangled packets?
 *
 */

 /*
	The data file format is as follows:
	
	; comment.
	; a number of entries, these are processed first to last.
	; a line based format.

	$ORIGIN origin
	$TTL default_ttl

	ENTRY_BEGIN
	; first give MATCH lines, that say what queries are matched
	; by this entry.
	; 'opcode' makes the query match the opcode from the reply;
	;	if you leave it out, any opcode matches this entry.
	; 'qtype' makes the query match the qtype from the reply.
	; 'qname' makes the query match the qname from the reply.
	; 'subdomain' makes the query match subdomains of qname from the reply.
	; 'serial=1023' makes the query match if ixfr serial is 1023.
	; 'all' has to match header byte for byte and all rrs in packet.
	; 'all_noedns' has to match header byte for byte and all rrs in packet;
	;	ignoring EDNS.
	; 'ttl' used with all, rrs in packet must also have matching TTLs.
	; 'DO' will match only queries with DO bit set.
	; 'noedns' matches queries without EDNS OPT records.
	; 'rcode' makes the query match the rcode from the reply.
	; 'question' makes the query match the question section.
	; 'answer' makes the query match the answer section.
	; 'ednsdata' matches queries to HEX_EDNS section.
	; 'UDP' matches if the transport is UDP.
	; 'TCP' matches if the transport is TCP.
	; 'ede=2' makes the query match if the EDNS EDE info-code is 2.
	;	It also snips the EDE record out of the packet to facilitate
	;	other matches.
	; 'ede=any' makes the query match any EDNS EDE info-code.
	;	It also snips the EDE record out of the packet to facilitate
	;	other matches.
	; 'client_cookie' makes the query match any DNS Cookie option with
	;	with a length of 8 octets.
	;	It also snips the DNS Cookie record out of the packet to
	;	facilitate other matches.
	; 'server_cookie' makes the query match any DNS Cookie option with
	;	with a length of 24 octets.
	;	It also snips the DNS Cookie record out of the packet to
	;	facilitate other matches.
	MATCH [opcode] [qtype] [qname] [serial=<value>] [all] [ttl]
	MATCH [UDP|TCP] DO
	MATCH ...
	; Then the REPLY header is specified.
	REPLY opcode, rcode or flags.
		(opcode)  QUERY IQUERY STATUS NOTIFY UPDATE
		(rcode)   NOERROR FORMERR SERVFAIL NXDOMAIN NOTIMPL YXDOMAIN
		 		YXRRSET NXRRSET NOTAUTH NOTZONE
		(flags)   QR AA TC RD CD RA AD DO
	REPLY ...
	; any additional actions to do.
	; 'copy_id' copies the ID from the query to the answer.
	ADJUST copy_id
	; 'copy_query' copies the query name, type and class to the answer.
	ADJUST copy_query
	; 'sleep=10' sleeps for 10 seconds before giving the answer (TCP is open)
	ADJUST [sleep=<num>]    ; sleep before giving any reply
	ADJUST [packet_sleep=<num>]  ; sleep before this packet in sequence
	; 'copy_ednsdata_assume_clientsubnet' copies ednsdata to reply, assumes
	; it is clientsubnet and adjusts scopemask to match sourcemask.
	ADJUST copy_ednsdata_assume_clientsubnet
	; 'increment_ecs_scope' increments the ECS scope copied from the
	;  sourcemask by one.
	ADJUST increment_ecs_scope
	SECTION QUESTION
	<RRs, one per line>    ; the RRcount is determined automatically.
	SECTION ANSWER
	<RRs, one per line>
	SECTION AUTHORITY
	<RRs, one per line>
	SECTION ADDITIONAL
	<RRs, one per line>
	EXTRA_PACKET		; follow with SECTION, REPLY for more packets.
	HEX_ANSWER_BEGIN	; follow with hex data
				; this replaces any answer packet constructed
				; with the SECTION keywords (only SECTION QUERY
				; is used to match queries). If the data cannot
				; be parsed, ADJUST rules for the answer packet
				; are ignored. Only copy_id is done.
	HEX_ANSWER_END
	HEX_EDNSDATA_BEGIN	; follow with hex data.
				; Raw EDNS data to match against. It must be an 
				; exact match (all options are matched) and will be 
				; evaluated only when 'MATCH ednsdata' given.
	HEX_EDNSDATA_END
	ENTRY_END


 	Example data file:
$ORIGIN nlnetlabs.nl
$TTL 3600

ENTRY_BEGIN
MATCH qname
REPLY NOERROR
ADJUST copy_id
SECTION QUESTION
www.nlnetlabs.nl.	IN	A
SECTION ANSWER
www.nlnetlabs.nl.	IN	A	195.169.215.155
SECTION AUTHORITY
nlnetlabs.nl.		IN	NS	www.nlnetlabs.nl.
ENTRY_END

ENTRY_BEGIN
MATCH qname
REPLY NOERROR
ADJUST copy_id
SECTION QUESTION
www2.nlnetlabs.nl.	IN	A
HEX_ANSWER_BEGIN
; 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19
;-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
 00 bf 81 80 00 01 00 01 00 02 00 02 03 77 77 77 0b 6b 61 6e	;	   1-  20
 61 72 69 65 70 69 65 74 03 63 6f 6d 00 00 01 00 01 03 77 77	;	  21-  40
 77 0b 6b 61 6e 61 72 69 65 70 69 65 74 03 63 6f 6d 00 00 01	;	  41-  60
 00 01 00 01 50 8b 00 04 52 5e ed 32 0b 6b 61 6e 61 72 69 65	;	  61-  80
 70 69 65 74 03 63 6f 6d 00 00 02 00 01 00 01 50 8b 00 11 03	;	  81- 100
 6e 73 31 08 68 65 78 6f 6e 2d 69 73 02 6e 6c 00 0b 6b 61 6e	;	 101- 120
 61 72 69 65 70 69 65 74 03 63 6f 6d 00 00 02 00 01 00 01 50	;	 121- 140
 8b 00 11 03 6e 73 32 08 68 65 78 6f 6e 2d 69 73 02 6e 6c 00	;	 141- 160
 03 6e 73 31 08 68 65 78 6f 6e 2d 69 73 02 6e 6c 00 00 01 00	;	 161- 180
 01 00 00 46 53 00 04 52 5e ed 02 03 6e 73 32 08 68 65 78 6f	;	 181- 200
 6e 2d 69 73 02 6e 6c 00 00 01 00 01 00 00 46 53 00 04 d4 cc	;	 201- 220
 db 5b
HEX_ANSWER_END
ENTRY_END



   note that this file will link with your
   void verbose(int level, char* format, ...); output function.
*/

/** Type of transport, since some entries match based on UDP or TCP of query */
enum transport_type {transport_any = 0, transport_udp, transport_tcp };

/** struct to keep a linked list of reply packets for a query */
struct reply_packet {
	/** next in list of reply packets, for TCP multiple pkts on wire */
	struct reply_packet* next;
	/** the reply pkt */
	uint8_t* reply_pkt;
	/** length of reply pkt */
	size_t reply_len;
	/** Additional EDNS data for matching queries. */
	struct sldns_buffer* raw_ednsdata;
	/** or reply pkt in hex if not parsable */
	struct sldns_buffer* reply_from_hex;
	/** seconds to sleep before giving packet */
	unsigned int packet_sleep; 
};

/** data structure to keep the canned queries in.
   format is the 'matching query' and the 'canned answer' */
struct entry {
	/* match */
	/* How to match an incoming query with this canned reply */
	/** match query opcode with answer opcode */
	uint8_t match_opcode;
	/** match qtype with answer qtype */
	uint8_t match_qtype;
	/** match qname with answer qname */
	uint8_t match_qname;
	/** match rcode with answer rcode */
	uint8_t match_rcode;
	/** match question section */
	uint8_t match_question;
	/** match answer section */
	uint8_t match_answer;
	/** match qname as subdomain of answer qname */
	uint8_t match_subdomain;
	/** match SOA serial number, from auth section */
	uint8_t match_serial;
	/** match EDNS EDE info-code */
	uint8_t match_ede;
	/** match any EDNS EDE info-code */
	uint8_t match_ede_any;
	/** match all of the packet */
	uint8_t match_all;
	/** match all of the packet; ignore EDNS */
	uint8_t match_all_noedns;
	/** match ttls in the packet */
	uint8_t match_ttl;
	/** match DO bit */
	uint8_t match_do;
	/** match absence of EDNS OPT record in query */
	uint8_t match_noedns;
	/** match edns data field given in hex */
	uint8_t match_ednsdata_raw;
	/** match an DNS cookie of length 8 */
	uint8_t match_client_cookie;
	/** match an DNS cookie of length 24 */
	uint8_t match_server_cookie;
	/** match query serial with this value. */
	uint32_t ixfr_soa_serial;
	/** match on UDP/TCP */
	enum transport_type match_transport;
	/** match EDNS EDE info-code with this value. */
	uint16_t ede_info_code;

	/** pre canned reply */
	struct reply_packet *reply_list;

	/** how to adjust the reply packet */
	/** copy over the ID from the query into the answer */
	uint8_t copy_id; 
	/** copy the query nametypeclass from query into the answer */
	uint8_t copy_query;
	/** copy ednsdata to reply, assume it is clientsubnet and
	 * adjust scopemask to match sourcemask */
	uint8_t copy_ednsdata_assume_clientsubnet;
	/** increment the ECS scope copied from the sourcemask by one */
	uint8_t increment_ecs_scope;
	/** in seconds */
	unsigned int sleeptime;

	/** some number that names this entry, line number in file or so */
	int lineno;

	/** next in list */
	struct entry* next;
};

/**
 * reads the canned reply file and returns a list of structs 
 * does an exit on error.
 * @param name: name of the file to read.
 * @param skip_whitespace: skip leftside whitespace.
 */
struct entry* read_datafile(const char* name, int skip_whitespace);

/**
 * Delete linked list of entries.
 */
void delete_entry(struct entry* list);

/**
 * Read one entry from the data file.
 * @param in: file to read from. Filepos must be at the start of a new line.
 * @param name: name of the file for prettier errors.
 * @param pstate: file parse state with lineno, default_ttl,
 * 	origin and prev_rr name.
 * @param skip_whitespace: skip leftside whitespace.
 * @return: The entry read (malloced) or NULL if no entry could be read.
 */
struct entry* read_entry(FILE* in, const char* name, 
	struct sldns_file_parse_state* pstate, int skip_whitespace);

/**
 * finds entry in list, or returns NULL.
 */
struct entry* find_match(struct entry* entries, uint8_t* query_pkt,
	size_t query_pkt_len, enum transport_type transport);

/**
 * match two packets, all must match
 * @param q: packet 1
 * @param qlen: length of q.
 * @param p: packet 2
 * @param plen: length of p.
 * @param mttl: if true, ttls must match, if false, ttls do not need to match
 * @param noloc: if true, rrs may be reordered in their packet-section.
 * 	rrs are then matches without location of the rr being important.
 * @param noedns: if true, edns is not compared, if false, edns must match.
 * @return true if matched.
 */
int match_all(uint8_t* q, size_t qlen, uint8_t* p, size_t plen, int mttl,
	int noloc, int noedns);

/**
 * copy & adjust packet, mallocs a copy.
 */
void adjust_packet(struct entry* match, uint8_t** answer_pkt,
	size_t* answer_pkt_len, uint8_t* query_pkt, size_t query_pkt_len);

/**
 * Parses data buffer to a query, finds the correct answer 
 * and calls the given function for every packet to send.
 * if verbose_out filename is given, packets are dumped there.
 * @param inbuf: the packet that came in
 * @param inlen: length of packet.
 * @param entries: entries read in from datafile.
 * @param count: is increased to count number of queries answered.
 * @param transport: set to UDP or TCP to match some types of entries.
 * @param sendfunc: called to send answer (buffer, size, userarg).
 * @param userdata: userarg to give to sendfunc.
 * @param verbose_out: if not NULL, verbose messages are printed there.
 */
void handle_query(uint8_t* inbuf, ssize_t inlen, struct entry* entries, 
	int* count, enum transport_type transport, 
	void (*sendfunc)(uint8_t*, size_t, void*), void* userdata,
	FILE* verbose_out);

#endif /* TESTPKTS_H */
