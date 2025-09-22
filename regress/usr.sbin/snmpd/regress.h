/*
 * Generic
 */
#define OID_MAX 128

struct oid {
	uint32_t subid[OID_MAX];
	size_t n_subid;
	int include;
};

struct searchrange {
	struct oid start;
	struct oid end;
};

enum type {
	TYPE_INTEGER = 2,
	TYPE_OCTETSTRING = 4,
	TYPE_NULL = 5,
	TYPE_OBJECTIDENTIFIER = 6,
	TYPE_IPADDRESS = 64,
	TYPE_COUNTER32 = 65,
	TYPE_GAUGE32 = 66,
	TYPE_TIMETICKS = 67,
	TYPE_OPAQUE = 68,
	TYPE_COUNTER64 = 70,
	TYPE_NOSUCHOBJECT = 128,
	TYPE_NOSUCHINSTANCE = 129,
	TYPE_ENDOFMIBVIEW = 130,
	/* Don't expect a specific value: mod 1000 */
	TYPE_INTEGER_UNKNOWN = 1002,
	TYPE_OCTETSTRING_UNKNOWN = 1004,
	TYPE_OBJECTIDENTIFIER_UNKNOWN = 1006,
	TYPE_IPADDRESS_UNKNOWN = 1064
};

enum error {
	NOERROR = 0,
	NOAGENTXERROR = 0,
	TOOBIG = 1,
	NOSUCHNAME = 2,
	BADVALUE = 3,
	READONLY = 4,
	GENERR = 5,
	NOACCESS = 6,
	WRONGTYPE = 7,
	WRONGLENGTH = 8,
	WRONGENCODING = 9,
	WRONGVALUE = 10,
	NOCREATION = 11,
	INCONSISTENTVALUE = 12,
	RESOURCEUNAVAILABLE = 13,
	COMMITFAILED = 14,
	UNDOFAILED = 15,
	AUTHORIZATIONERROR = 16,
	NOTWRITABLE = 17,
	INCONSISTENTNAME = 18,
	OPENFAILED = 256,
	NOTOPEN = 257,
	INDEXWRONGTYPE = 258,
	INDEXALREADYALLOCATED = 259,
	INDEXNONEAVAILABLE = 260,
	INDEXNOTALLOCATED = 261,
	UNSUPPORTEDCONTEXT = 262,
	DUPLICATEREGISTRATION = 263,
	UNKNOWNREGISTRATION = 264,
	UNKNOWNAGENTCAPS = 265,
	PARSEERROR = 266,
	REQUESTDENIED = 267,
	PROCESSINGERROR = 268
};

enum close_reason {
	REASONOTHER = 1,
	REASONPARSEERROR = 2,
	REASONPROTOCOLERROR = 3,
	REASONTIMEOUTS = 4,
	REASONSHUTDOWN = 5,
	REASONBYMANAGER = 6
};

struct varbind {
	int		typeunknown;
	int		nameunknown;
	int		dataunknown;
	enum type	type;
	struct oid	name;
	union data {
		int32_t		 int32;
		uint32_t	 uint32;
		uint64_t	 uint64;
		struct oid	 oid;
		struct octetstring {
			char	*string;
			size_t	 len;
		}		 octetstring;
	}		data;
};

enum snmp_request {
	REQUEST_GET = 0,
	REQUEST_GETNEXT = 1,
	REQUEST_RESPONSE = 2,
	REQUEST_SET = 3,
	REQUEST_TRAP = 4,
	REQUEST_GETBULK = 5,
	REQUEST_INFORM = 6,
	REQUEST_TRAPV2 = 7,
	REQUEST_REPORT = 8
};


extern int verbose;
extern char *axsocket;
extern char *hostname;
extern char *servname;
extern char *community;

int oid_cmp(struct oid *, struct oid *);
char *oid_print(struct oid *, char *, size_t);

#define OID_STRUCT(...) (struct oid){					\
	.subid = { __VA_ARGS__ },					\
	.n_subid = (sizeof((uint32_t []) { __VA_ARGS__ }) / sizeof(uint32_t)) \
}

#define OID_ARG(...) (uint32_t []) { __VA_ARGS__ },	\
    (sizeof((uint32_t []) { __VA_ARGS__ }) / sizeof(uint32_t))

/*
 * AgentX
 */
#define MIB_OPENBSD_REGRESS 1, 3, 6, 1, 4, 1, 30155, 42
/* Subagent names */
#define MIB_SUBAGENTS MIB_OPENBSD_REGRESS, 1
#define MIB_SUBAGENT_OPEN MIB_SUBAGENTS, 1
#define MIB_SUBAGENT_PING MIB_SUBAGENTS, 2
#define MIB_SUBAGENT_CLOSE MIB_SUBAGENTS, 3
#define MIB_SUBAGENT_REGISTER MIB_SUBAGENTS, 4
#define MIB_SUBAGENT_UNREGISTER MIB_SUBAGENTS, 4
#define MIB_SUBAGENT_BACKEND MIB_SUBAGENTS, 5
#define MIB_SUBAGENT_SNMP MIB_SUBAGENTS, 6
#define MIB_SUBAGENT_TRANSPORT MIB_SUBAGENTS, 7
/* Region used for registration testing */
#define MIB_REGISTER MIB_OPENBSD_REGRESS, 2
#define MIB_UNREGISTER MIB_OPENBSD_REGRESS, 3
#define MIB_BACKEND MIB_OPENBSD_REGRESS, 4
#define MIB_SNMP MIB_OPENBSD_REGRESS, 5
#define MIB_TRANSPORT MIB_OPENBSD_REGRESS, 6

#define SYSORTABLE 1, 3, 6, 1, 2, 1, 1, 9

int agentx_connect(const char *);
uint32_t agentx_open(int, int, uint8_t, uint32_t[], size_t, const char *);
void agentx_close(int, uint32_t, enum close_reason);
void agentx_register(int, uint32_t, uint8_t, uint8_t, uint8_t, uint8_t,
    uint32_t[], size_t, uint32_t);
void agentx_response(int, void *, enum error, uint16_t, struct varbind *,
    size_t);
void agentx_get_handle(const char *, const void *, size_t, uint8_t, uint32_t,
    struct varbind *, size_t);
void agentx_getnext_handle(const char *, const void *, size_t, uint8_t,
    uint32_t, struct searchrange *, struct varbind *, size_t);
size_t agentx_getbulk_handle(const char *, const void *, size_t, uint8_t, int32_t,
    struct varbind *, size_t, struct varbind *);
size_t agentx_read(int, void *, size_t, int);
void agentx_timeout(int, int);


/* Tests */
void agentx_open_nnbo(void);
void agentx_open_nbo(void);
void agentx_open_invalidversion(void);
void agentx_open_ignore_sessionid(void);
void agentx_open_invalid_oid(void);
void agentx_open_descr_too_long(void);
void agentx_open_descr_invalid(void);
void agentx_open_context(void);
void agentx_open_instance_registration(void);
void agentx_open_new_index(void);
void agentx_open_any_index(void);
void agentx_ping_notopen(void);
void agentx_ping_invalid_sessionid(void);
void agentx_ping_default(void);
void agentx_ping_context(void);
void agentx_ping_invalid_version(void);
void agentx_ping_instance_registration(void);
void agentx_ping_new_index(void);
void agentx_ping_any_index(void);
void agentx_ping_nbo_nnbo(void);
void agentx_ping_nnbo_nbo(void);
void agentx_ping_invalid_version_close(void);
void agentx_close_notopen(void);
void agentx_close_reasonother(void);
void agentx_close_reasonparseerror(void);
void agentx_close_reasonprotocolerror(void);
void agentx_close_reasontimouts(void);
void agentx_close_reasonshutdown(void);
void agentx_close_reasonbymanager(void);
void agentx_close_reasoninvalid(void);
void agentx_close_single(void);
void agentx_close_notowned(void);
void agentx_close_invalid_sessionid(void);
void agentx_close_context(void);
void agentx_close_invalid_version(void);
void agentx_close_instance_registration(void);
void agentx_close_new_index(void);
void agentx_close_any_index(void);
void agentx_close_nnbo_nbo(void);
void agentx_register_notopen(void);
void agentx_register_invalid_sessionid(void);
void agentx_register_default(void);
void agentx_register_context(void);
void agentx_register_invalid_version(void);
void agentx_register_instance_registration(void);
void agentx_register_new_index(void);
void agentx_register_any_index(void);
void agentx_register_duplicate_self(void);
void agentx_register_duplicate_twocon(void);
void agentx_register_duplicate_priority(void);
void agentx_register_range(void);
void agentx_register_range_invalidupperbound(void);
void agentx_register_range_single(void);
void agentx_register_range_overlap_single(void);
void agentx_register_single_overlap_range(void);
void agentx_register_range_overlap_range(void);
void agentx_register_below(void);
void agentx_register_above(void);
void agentx_register_restricted(void);
void agentx_unregister_notopen(void);
void agentx_unregister_invalid_sessionid(void);
void agentx_unregister_notregistered(void);
void agentx_unregister_single(void);
void agentx_unregister_single_notowned(void);
void agentx_unregister_range(void);
void agentx_unregister_range_single(void);
void agentx_unregister_range_subset(void);
void agentx_unregister_range_extra(void);
void agentx_unregister_range_priority(void);
void agentx_unregister_range_notowned(void);

/*
 * SNMP
 */
socklen_t snmp_resolve(int, const char *, const char *, struct sockaddr *);
int snmp_connect(int, struct sockaddr *, socklen_t);
int32_t snmpv2_get(int, const char *, int32_t, struct varbind *, size_t);
int32_t snmpv2_getnext(int, const char *, int32_t, struct varbind *, size_t);
int32_t snmpv2_getbulk(int, const char *, int32_t, int32_t, int32_t,
    struct varbind *, size_t);
struct ber_element *snmpv2_build(const char *, enum snmp_request, int32_t,
    int32_t, int32_t, struct varbind *, size_t);
void snmpv2_response_validate(int, int, const char *, int32_t, int32_t, int32_t,
    struct varbind *, size_t);
void snmp_timeout(int, int);
void smi_debug_elements(struct ber_element *);

void backend_get_integer(void);
void backend_get_octetstring(void);
void backend_get_objectidentifier(void);
void backend_get_ipaddress(void);
void backend_get_counter32(void);
void backend_get_gauge32(void);
void backend_get_timeticks(void);
void backend_get_opaque(void);
void backend_get_counter64(void);
void backend_get_nosuchobject(void);
void backend_get_nosuchinstance(void);
void backend_get_endofmibview(void);
void backend_get_two_single_backend(void);
void backend_get_two_double_backend(void);
void backend_get_wrongorder(void);
void backend_get_toofew(void);
void backend_get_toomany(void);
void backend_get_instance(void);
void backend_get_instance_below(void);
void backend_get_timeout_default(void);
void backend_get_timeout_session_lower(void);
void backend_get_timeout_session_higher(void);
void backend_get_timeout_region_lower(void);
void backend_get_timeout_region_higher(void);
void backend_get_priority_lower(void);
void backend_get_priority_higher(void);
void backend_get_priority_below_lower(void);
void backend_get_priority_below_higher(void);
void backend_get_close(void);
void backend_get_close_overlap(void);
void backend_get_disappear(void);
void backend_get_disappear_overlap(void);
void backend_get_disappear_doublesession(void);
void backend_get_octetstring_max(void);
void backend_get_octetstring_too_long(void);
void backend_get_ipaddress_too_short(void);
void backend_get_ipaddress_too_long(void);
void backend_get_opaque_non_ber(void);
void backend_get_opaque_double_value(void);
void backend_getnext_selfbound(void);
void backend_getnext_lowerbound(void);
void backend_getnext_lowerbound_self(void);
void backend_getnext_lowerbound_highprio(void);
void backend_getnext_lowerbound_lowprio(void);
void backend_getnext_sibling(void);
void backend_getnext_child_gap(void);
void backend_getnext_nosuchobject(void);
void backend_getnext_nosuchinstance(void);
void backend_getnext_endofmibview(void);
void backend_getnext_inclusive(void);
void backend_getnext_jumpnext(void);
void backend_getnext_jumpnext_endofmibview(void);
void backend_getnext_jump_up(void);
void backend_getnext_jump_up(void);
void backend_getnext_two_single_backend(void);
void backend_getnext_two_double_backend(void);
void backend_getnext_instance_below(void);
void backend_getnext_instance(void);
void backend_getnext_instance_exact(void);
void backend_getnext_instance_ignore(void);
void backend_getnext_instance_ignore(void);
void backend_getnext_backwards(void);
void backend_getnext_stale(void);
void backend_getnext_inclusive_backwards(void);
void backend_getnext_toofew(void);
void backend_getnext_toomany(void);
void backend_getnext_response_equal_end(void);
void backend_getnext_instance_below_region_before_instance(void);
void backend_getnext_instance_below_region_on_instance(void);
void backend_getnext_instance_below_region_below_instance(void);
void backend_getbulk_nonrep_zero_maxrep_one(void);
void backend_getbulk_nonrep_zero_maxrep_two(void);
void backend_getbulk_nonrep_one_maxrep_one(void);
void backend_getbulk_nonrep_one_maxrep_two(void);
void backend_getbulk_nonrep_two_maxrep_two(void);
void backend_getbulk_nonrep_negative(void);
void backend_getbulk_endofmibview(void);
void backend_getbulk_endofmibview_second_rep(void);
void backend_getbulk_endofmibview_two_varbinds(void);
void backend_error_get_toobig(void);
void backend_error_get_nosuchname(void);
void backend_error_get_badvalue(void);
void backend_error_get_readonly(void);
void backend_error_get_generr(void);
void backend_error_get_wrongtype(void);
void backend_error_get_wronglength(void);
void backend_error_get_wrongencoding(void);
void backend_error_get_wrongvalue(void);
void backend_error_get_nocreation(void);
void backend_error_get_inconsistentvalue(void);
void backend_error_get_commitfailed(void);
void backend_error_get_undofailed(void);
void backend_error_get_authorizationerror(void);
void backend_error_get_notwritable(void);
void backend_error_get_inconsistentname(void);
void backend_error_get_openfailed(void);
void backend_error_get_notopen(void);
void backend_error_get_indexwrongtype(void);
void backend_error_get_indexalreadyallocated(void);
void backend_error_get_indexnonavailable(void);
void backend_error_get_indexnotallocated(void);
void backend_error_get_duplicateregistration(void);
void backend_error_get_requestdenied(void);
void backend_error_get_processingerror(void);
void backend_error_get_nonstandard(void);
void backend_error_getnext_toobig(void);
void backend_error_getnext_nosuchname(void);
void backend_error_getnext_badvalue(void);
void backend_error_getnext_readonly(void);
void backend_error_getnext_generr(void);
void backend_error_getnext_noaccess(void);
void backend_error_getnext_wrongtype(void);
void backend_error_getnext_wronglength(void);
void backend_error_getnext_wrongencoding(void);
void backend_error_getnext_wrongvalue(void);
void backend_error_getnext_nocreation(void);
void backend_error_getnext_inconsistentvalue(void);
void backend_error_getnext_resourceunavailable(void);
void backend_error_getnext_commitfailed(void);
void backend_error_getnext_undofailed(void);
void backend_error_getnext_notwritable(void);
void backend_error_getnext_inconsistentname(void);
void backend_error_getnext_openfailed(void);
void backend_error_getnext_notopen(void);
void backend_error_getnext_indexwrongtype(void);
void backend_error_getnext_indexalreadyallocated(void);
void backend_error_getnext_indexnonavailable(void);
void backend_error_getnext_indexnotallocated(void);
void backend_error_getnext_unsupportedcontext(void);
void backend_error_getnext_duplicateregistration(void);
void backend_error_getnext_unknownregistration(void);
void backend_error_getnext_parseerror(void);
void backend_error_getnext_requestdenied(void);
void backend_error_getnext_processingerror(void);
void backend_error_getnext_nonstandard(void);
void backend_error_getbulk_firstrepetition(void);
void backend_error_getbulk_secondrepetition(void);
void snmp_v3_usm_noauthpriv(void);
void transport_tcp_get(void);
void transport_tcp_disconnect(void);
void transport_tcp_double_get_disconnect(void);
