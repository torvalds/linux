#include <sys/socket.h>

#include <err.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "regress.h"

__dead void usage(void);

const struct {
	char *target;
	void (*function)(void);
} targets[] = {
	{ "agentx_open_nnbo", agentx_open_nnbo },
	{ "agentx_open_nbo", agentx_open_nbo },
	{ "agentx_open_invalidversion", agentx_open_invalidversion },
	{ "agentx_open_ignore_sessionid", agentx_open_ignore_sessionid },
	{ "agentx_open_invalid_oid", agentx_open_invalid_oid },
	{ "agentx_open_descr_too_long", agentx_open_descr_too_long },
	{ "agentx_open_descr_invalid", agentx_open_descr_invalid },
	{ "agentx_open_context", agentx_open_context },
	{ "agentx_open_instance_registration", agentx_open_instance_registration },
	{ "agentx_open_new_index", agentx_open_new_index },
	{ "agentx_open_any_index", agentx_open_any_index },
	{ "agentx_ping_notopen", agentx_ping_notopen },
	{ "agentx_ping_invalid_sessionid", agentx_ping_invalid_sessionid },
	{ "agentx_ping_default", agentx_ping_default },
	{ "agentx_ping_context", agentx_ping_context },
	{ "agentx_ping_invalid_version", agentx_ping_invalid_version },
	{ "agentx_ping_instance_registration", agentx_ping_instance_registration },
	{ "agentx_ping_new_index", agentx_ping_new_index },
	{ "agentx_ping_any_index", agentx_ping_any_index },
	{ "agentx_ping_nbo_nnbo", agentx_ping_nbo_nnbo },
	{ "agentx_ping_nnbo_nbo", agentx_ping_nnbo_nbo },
	{ "agentx_ping_invalid_version_close", agentx_ping_invalid_version_close },
	{ "agentx_close_notopen", agentx_close_notopen },
	{ "agentx_close_reasonother", agentx_close_reasonother },
	{ "agentx_close_reasonparseerror", agentx_close_reasonparseerror },
	{ "agentx_close_reasonprotocolerror", agentx_close_reasonprotocolerror },
	{ "agentx_close_reasontimouts", agentx_close_reasontimouts },
	{ "agentx_close_reasonshutdown", agentx_close_reasonshutdown },
	{ "agentx_close_reasonbymanager", agentx_close_reasonbymanager },
	{ "agentx_close_reasoninvalid", agentx_close_reasoninvalid },
	{ "agentx_close_single", agentx_close_single },
	{ "agentx_close_notowned", agentx_close_notowned },
	{ "agentx_close_invalid_sessionid", agentx_close_invalid_sessionid },
	{ "agentx_close_context", agentx_close_context },
	{ "agentx_close_invalid_version", agentx_close_invalid_version },
	{ "agentx_close_instance_registration", agentx_close_instance_registration },
	{ "agentx_close_new_index", agentx_close_new_index },
	{ "agentx_close_any_index", agentx_close_any_index },
	{ "agentx_close_nnbo_nbo", agentx_close_nnbo_nbo },
	{ "agentx_register_notopen", agentx_register_notopen },
	{ "agentx_register_invalid_sessionid", agentx_register_invalid_sessionid },
	{ "agentx_register_default", agentx_register_default },
	{ "agentx_register_context", agentx_register_context },
	{ "agentx_register_invalid_version", agentx_register_invalid_version },
	{ "agentx_register_instance_registration", agentx_register_instance_registration },
	{ "agentx_register_new_index", agentx_register_new_index },
	{ "agentx_register_any_index", agentx_register_any_index },
	{ "agentx_register_duplicate_self", agentx_register_duplicate_self },
	{ "agentx_register_duplicate_twocon", agentx_register_duplicate_twocon },
	{ "agentx_register_duplicate_priority", agentx_register_duplicate_priority },
	{ "agentx_register_range", agentx_register_range },
	{ "agentx_register_range_invalidupperbound", agentx_register_range_invalidupperbound },
	{ "agentx_register_range_single", agentx_register_range_single },
	{ "agentx_register_range_overlap_single", agentx_register_range_overlap_single },
	{ "agentx_register_single_overlap_range", agentx_register_single_overlap_range },
	{ "agentx_register_range_overlap_range", agentx_register_range_overlap_range },
	{ "agentx_register_below", agentx_register_below },
	{ "agentx_register_above", agentx_register_above },
	{ "agentx_register_restricted", agentx_register_restricted },
	{ "agentx_unregister_notopen", agentx_unregister_notopen },
	{ "agentx_unregister_invalid_sessionid", agentx_unregister_invalid_sessionid },
	{ "agentx_unregister_notregistered", agentx_unregister_notregistered },
	{ "agentx_unregister_single", agentx_unregister_single },
	{ "agentx_unregister_single_notowned", agentx_unregister_single_notowned },
	{ "agentx_unregister_range", agentx_unregister_range },
	{ "agentx_unregister_range_single", agentx_unregister_range_single },
	{ "agentx_unregister_range_subset", agentx_unregister_range_subset },
	{ "agentx_unregister_range_extra", agentx_unregister_range_extra },
	{ "agentx_unregister_range_priority", agentx_unregister_range_priority },
	{ "agentx_unregister_range_notowned", agentx_unregister_range_notowned },
	{ "backend_get_integer", backend_get_integer },
	{ "backend_get_octetstring", backend_get_octetstring },
	{ "backend_get_objectidentifier", backend_get_objectidentifier },
	{ "backend_get_ipaddress", backend_get_ipaddress },
	{ "backend_get_counter32", backend_get_counter32 },
	{ "backend_get_gauge32", backend_get_gauge32 },
	{ "backend_get_timeticks", backend_get_timeticks },
	{ "backend_get_opaque", backend_get_opaque },
	{ "backend_get_counter64", backend_get_counter64 },
	{ "backend_get_nosuchobject", backend_get_nosuchobject },
	{ "backend_get_nosuchinstance", backend_get_nosuchinstance },
	{ "backend_get_endofmibview", backend_get_endofmibview },
	{ "backend_get_two_single_backend", backend_get_two_single_backend },
	{ "backend_get_two_double_backend", backend_get_two_double_backend },
	{ "backend_get_wrongorder", backend_get_wrongorder },
	{ "backend_get_toofew", backend_get_toofew },
	{ "backend_get_toomany", backend_get_toomany },
	{ "backend_get_instance", backend_get_instance },
	{ "backend_get_instance_below", backend_get_instance_below },
	{ "backend_get_timeout_default", backend_get_timeout_default },
	{ "backend_get_timeout_session_lower", backend_get_timeout_session_lower },
	{ "backend_get_timeout_session_higher", backend_get_timeout_session_higher },
	{ "backend_get_timeout_region_lower", backend_get_timeout_region_lower },
	{ "backend_get_timeout_region_higher", backend_get_timeout_region_higher },
	{ "backend_get_priority_lower", backend_get_priority_lower },
	{ "backend_get_priority_higher", backend_get_priority_higher },
	{ "backend_get_priority_below_lower", backend_get_priority_below_lower },
	{ "backend_get_priority_below_higher", backend_get_priority_below_higher },
	{ "backend_get_close", backend_get_close },
	{ "backend_get_close_overlap", backend_get_close_overlap },
	{ "backend_get_disappear", backend_get_disappear },
	{ "backend_get_disappear_overlap", backend_get_disappear_overlap },
	{ "backend_get_disappear_doublesession", backend_get_disappear_doublesession },
	{ "backend_get_octetstring_max", backend_get_octetstring_max },
	{ "backend_get_octetstring_too_long", backend_get_octetstring_too_long },
	{ "backend_get_ipaddress_too_short", backend_get_ipaddress_too_short },
	{ "backend_get_ipaddress_too_long", backend_get_ipaddress_too_long },
	{ "backend_get_opaque_non_ber", backend_get_opaque_non_ber },
	{ "backend_get_opaque_double_value", backend_get_opaque_double_value },
	{ "backend_getnext_selfbound", backend_getnext_selfbound },
	{ "backend_getnext_lowerbound", backend_getnext_lowerbound },
	{ "backend_getnext_lowerbound_self", backend_getnext_lowerbound_self },
	{ "backend_getnext_lowerbound_highprio", backend_getnext_lowerbound_highprio },
	{ "backend_getnext_lowerbound_lowprio", backend_getnext_lowerbound_lowprio },
	{ "backend_getnext_sibling", backend_getnext_sibling },
	{ "backend_getnext_child_gap", backend_getnext_child_gap },
	{ "backend_getnext_nosuchobject", backend_getnext_nosuchobject },
	{ "backend_getnext_nosuchinstance", backend_getnext_nosuchinstance },
	{ "backend_getnext_endofmibview", backend_getnext_endofmibview },
	{ "backend_getnext_inclusive", backend_getnext_inclusive },
	{ "backend_getnext_jumpnext", backend_getnext_jumpnext },
	{ "backend_getnext_jumpnext_endofmibview", backend_getnext_jumpnext_endofmibview },
	{ "backend_getnext_jump_up", backend_getnext_jump_up },
	{ "backend_getnext_two_single_backend", backend_getnext_two_single_backend },
	{ "backend_getnext_two_double_backend", backend_getnext_two_double_backend },
	{ "backend_getnext_instance_below", backend_getnext_instance_below },
	{ "backend_getnext_instance", backend_getnext_instance },
	{ "backend_getnext_instance_exact", backend_getnext_instance_exact },
	{ "backend_getnext_instance_ignore", backend_getnext_instance_ignore },
	{ "backend_getnext_backwards", backend_getnext_backwards },
	{ "backend_getnext_stale", backend_getnext_stale },
	{ "backend_getnext_inclusive_backwards", backend_getnext_inclusive_backwards },
	{ "backend_getnext_toofew", backend_getnext_toofew },
	{ "backend_getnext_toomany", backend_getnext_toomany },
	{ "backend_getnext_response_equal_end", backend_getnext_response_equal_end },
	{ "backend_getnext_instance_below_region_before_instance", backend_getnext_instance_below_region_before_instance },
	{ "backend_getnext_instance_below_region_on_instance", backend_getnext_instance_below_region_on_instance },
	{ "backend_getnext_instance_below_region_below_instance", backend_getnext_instance_below_region_below_instance },
	{ "backend_getbulk_nonrep_zero_maxrep_one", backend_getbulk_nonrep_zero_maxrep_one },
	{ "backend_getbulk_nonrep_zero_maxrep_two", backend_getbulk_nonrep_zero_maxrep_two },
	{ "backend_getbulk_nonrep_one_maxrep_one", backend_getbulk_nonrep_one_maxrep_one },
	{ "backend_getbulk_nonrep_one_maxrep_two", backend_getbulk_nonrep_one_maxrep_two },
	{ "backend_getbulk_nonrep_two_maxrep_two", backend_getbulk_nonrep_two_maxrep_two },
	{ "backend_getbulk_nonrep_negative", backend_getbulk_nonrep_negative },
	{ "backend_getbulk_endofmibview", backend_getbulk_endofmibview },
	{ "backend_getbulk_endofmibview_second_rep", backend_getbulk_endofmibview_second_rep },
	{ "backend_getbulk_endofmibview_two_varbinds", backend_getbulk_endofmibview_two_varbinds },
	{ "backend_error_get_toobig", backend_error_get_toobig },
	{ "backend_error_get_nosuchname", backend_error_get_nosuchname },
	{ "backend_error_get_badvalue", backend_error_get_badvalue },
	{ "backend_error_get_readonly", backend_error_get_readonly },
	{ "backend_error_get_generr", backend_error_get_generr },
	{ "backend_error_get_wrongtype", backend_error_get_wrongtype },
	{ "backend_error_get_wronglength", backend_error_get_wronglength },
	{ "backend_error_get_wrongvalue", backend_error_get_wrongvalue },
	{ "backend_error_get_nocreation", backend_error_get_nocreation },
	{ "backend_error_get_inconsistentvalue", backend_error_get_inconsistentvalue },
	{ "backend_error_get_commitfailed", backend_error_get_commitfailed },
	{ "backend_error_get_undofailed", backend_error_get_undofailed },
	{ "backend_error_get_authorizationerror", backend_error_get_authorizationerror },
	{ "backend_error_get_notwritable", backend_error_get_notwritable },
	{ "backend_error_get_inconsistentname", backend_error_get_inconsistentname },
	{ "backend_error_get_openfailed", backend_error_get_openfailed },
	{ "backend_error_get_notopen", backend_error_get_notopen },
	{ "backend_error_get_indexwrongtype", backend_error_get_indexwrongtype },
	{ "backend_error_get_indexalreadyallocated", backend_error_get_indexalreadyallocated },
	{ "backend_error_get_indexnonavailable", backend_error_get_indexnonavailable },
	{ "backend_error_get_indexnotallocated", backend_error_get_indexnotallocated },
	{ "backend_error_get_duplicateregistration", backend_error_get_duplicateregistration },
	{ "backend_error_get_requestdenied", backend_error_get_requestdenied },
	{ "backend_error_get_processingerror", backend_error_get_processingerror },
	{ "backend_error_get_nonstandard", backend_error_get_nonstandard },
	{ "backend_error_getnext_toobig", backend_error_getnext_toobig },
	{ "backend_error_getnext_nosuchname", backend_error_getnext_nosuchname },
	{ "backend_error_getnext_badvalue", backend_error_getnext_badvalue },
	{ "backend_error_getnext_readonly", backend_error_getnext_readonly },
	{ "backend_error_getnext_generr", backend_error_getnext_generr },
	{ "backend_error_getnext_noaccess", backend_error_getnext_noaccess },
	{ "backend_error_getnext_wrongtype", backend_error_getnext_wrongtype },
	{ "backend_error_getnext_wronglength", backend_error_getnext_wronglength },
	{ "backend_error_getnext_wrongencoding", backend_error_getnext_wrongencoding },
	{ "backend_error_getnext_wrongvalue", backend_error_getnext_wrongvalue },
	{ "backend_error_getnext_nocreation", backend_error_getnext_nocreation },
	{ "backend_error_getnext_inconsistentvalue", backend_error_getnext_inconsistentvalue },
	{ "backend_error_getnext_resourceunavailable", backend_error_getnext_resourceunavailable },
	{ "backend_error_getnext_commitfailed", backend_error_getnext_commitfailed },
	{ "backend_error_getnext_undofailed", backend_error_getnext_undofailed },
	{ "backend_error_getnext_notwritable", backend_error_getnext_notwritable },
	{ "backend_error_getnext_inconsistentname", backend_error_getnext_inconsistentname },
	{ "backend_error_getnext_openfailed", backend_error_getnext_openfailed },
	{ "backend_error_getnext_notopen", backend_error_getnext_notopen },
	{ "backend_error_getnext_indexwrongtype", backend_error_getnext_indexwrongtype },
	{ "backend_error_getnext_indexalreadyallocated", backend_error_getnext_indexalreadyallocated },
	{ "backend_error_getnext_indexnonavailable", backend_error_getnext_indexnonavailable },
	{ "backend_error_getnext_indexnotallocated", backend_error_getnext_indexnotallocated },
	{ "backend_error_getnext_unsupportedcontext", backend_error_getnext_unsupportedcontext },
	{ "backend_error_getnext_duplicateregistration", backend_error_getnext_duplicateregistration },
	{ "backend_error_getnext_unknownregistration", backend_error_getnext_unknownregistration },
	{ "backend_error_getnext_parseerror", backend_error_getnext_parseerror },
	{ "backend_error_getnext_requestdenied", backend_error_getnext_requestdenied },
	{ "backend_error_getnext_processingerror", backend_error_getnext_processingerror },
	{ "backend_error_getnext_nonstandard", backend_error_getnext_nonstandard },
	{ "backend_error_getbulk_firstrepetition", backend_error_getbulk_firstrepetition },
	{ "backend_error_getbulk_secondrepetition", backend_error_getbulk_secondrepetition },
	{ "snmp_v3_usm_noauthpriv", snmp_v3_usm_noauthpriv },
	{ "transport_tcp_get", transport_tcp_get },
	{ "transport_tcp_disconnect", transport_tcp_disconnect },
	{ "transport_tcp_double_get_disconnect", transport_tcp_double_get_disconnect },
	{ NULL, NULL }
};

int verbose = 0;
char *axsocket = NULL;

char *hostname = NULL;
char *servname = NULL;

char *community = NULL;

int
main(int argc, char *argv[])
{
	size_t i, j;
	int c;

	while ((c = getopt(argc, argv, "a:h:p:v")) != -1) {
		switch (c) {
		case 'a':
			axsocket = optarg;
			break;
		case 'c':
			community = optarg;
			break;
		case 'h':
			hostname = optarg;
			break;
		case 'p':
			servname = optarg;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

	for (i = 0; i < argc; i++) {
		for (j = 0; targets[j].target != NULL; j++) {
			if (strcmp(argv[i], targets[j].target) == 0) {
				targets[j].function();
				return 0;
			}
		}
	}

	errx(1, "Unknown target: %s", argv[1]);
}

__dead void
usage(void)
{
	fprintf(stderr, "%s: [-v] [-a axsocket] test\n", getprogname());
	exit(1);
}

int
oid_cmp(struct oid *a, struct oid *b) 
{
	size_t   i, min;

	min = a->n_subid < b->n_subid ? a->n_subid : b->n_subid;
	for (i = 0; i < min; i++) {
		if (a->subid[i] < b->subid[i]) 
			return (-1);
		if (a->subid[i] > b->subid[i])
			return (1);
	}
	/* a is parent of b */
	if (a->n_subid < b->n_subid)
		return (-2);
	/* a is child of b */
	if (a->n_subid > b->n_subid)
		return 2;
	return (0);
}

char *
oid_print(struct oid *oid, char *buf, size_t len)
{
	char digit[11];
	size_t i;

	buf[0] = '\0';
	for (i = 0; i < oid->n_subid; i++) {
		snprintf(digit, sizeof(digit), "%"PRIu32, oid->subid[i]);
		if (i > 0)
			strlcat(buf, ".", len);
		strlcat(buf, digit, len);
	}
	if (oid->include) {
		strlcat(buf, "incl", len);
		snprintf(digit, sizeof(digit), "(%d)", oid->include);
		strlcat(buf, digit, len);
	}
	return buf;
}
