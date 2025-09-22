# $OpenBSD: Makefile,v 1.14 2024/02/08 17:38:41 martijn Exp $
# Regress tests for snmpd

PROG =				snmpd_regress
SRCS =				snmpd_regress.c agentx.c backend.c snmp.c
SRCS+=				transport.c

LDADD=				-lutil
DPADD=				${LIBUTIL}

SNMPD ?=			/usr/sbin/snmpd
SNMPD_START =			${SUDO} ${SNMPD} -Nvvdf ${.CURDIR}/snmpd.conf >> snmpd.log 2>&1 &

REGRESS_CLEANUP =		cleanup
REGRESS_SETUP_ONCE =		setup
CLEANFILES =			snmpd.log

setup:
	-${SUDO} /etc/rc.d/snmpd stop
	> snmpd.log

cleanup:
	-${SUDO} pkill -f ${SNMPD}
	@sleep 1
	-${SUDO} rcctl restart snmpd

REGRESS_TARGETS +=	 run-snmpd
run-snmpd:
	-${SUDO} pkill -f ${SNMPD}
	@sleep 1
	${SUDO} env OBJDIR=${.OBJDIR} sh ${.CURDIR}/snmpd.sh

# AgentX administrative commands: application_agentx.c
AGENTX_TARGETS =		agentx_open_nnbo
AGENTX_TARGETS+=		agentx_open_nbo
AGENTX_TARGETS+=		agentx_open_invalidversion
AGENTX_TARGETS+=		agentx_open_ignore_sessionid
AGENTX_TARGETS+=		agentx_open_invalid_oid
AGENTX_TARGETS+=		agentx_open_descr_too_long
AGENTX_TARGETS+=		agentx_open_descr_invalid
AGENTX_TARGETS+=		agentx_open_context
AGENTX_TARGETS+=		agentx_open_instance_registration
AGENTX_TARGETS+=		agentx_open_new_index
AGENTX_TARGETS+=		agentx_open_any_index
AGENTX_TARGETS+=		agentx_ping_notopen
AGENTX_TARGETS+=		agentx_ping_invalid_sessionid
AGENTX_TARGETS+=		agentx_ping_default
AGENTX_TARGETS+=		agentx_ping_context
AGENTX_TARGETS+=		agentx_ping_invalid_version
AGENTX_TARGETS+=		agentx_ping_instance_registration
AGENTX_TARGETS+=		agentx_ping_new_index
AGENTX_TARGETS+=		agentx_ping_any_index
AGENTX_TARGETS+=		agentx_ping_nbo_nnbo
AGENTX_TARGETS+=		agentx_ping_nnbo_nbo
AGENTX_TARGETS+=		agentx_ping_invalid_version_close
AGENTX_TARGETS+=		agentx_close_notopen
AGENTX_TARGETS+=		agentx_close_reasonother
AGENTX_TARGETS+=		agentx_close_reasonparseerror
AGENTX_TARGETS+=		agentx_close_reasonprotocolerror
AGENTX_TARGETS+=		agentx_close_reasontimouts
AGENTX_TARGETS+=		agentx_close_reasonshutdown
AGENTX_TARGETS+=		agentx_close_reasonbymanager
AGENTX_TARGETS+=		agentx_close_reasoninvalid
AGENTX_TARGETS+=		agentx_close_single
AGENTX_TARGETS+=		agentx_close_notowned
AGENTX_TARGETS+=		agentx_close_invalid_sessionid
AGENTX_TARGETS+=		agentx_close_context
AGENTX_TARGETS+=		agentx_close_invalid_version
AGENTX_TARGETS+=		agentx_close_instance_registration
AGENTX_TARGETS+=		agentx_close_new_index
AGENTX_TARGETS+=		agentx_close_any_index
AGENTX_TARGETS+=		agentx_close_nnbo_nbo
AGENTX_TARGETS+=		agentx_register_notopen
AGENTX_TARGETS+=		agentx_register_invalid_sessionid
AGENTX_TARGETS+=		agentx_register_default
AGENTX_TARGETS+=		agentx_register_context
AGENTX_TARGETS+=		agentx_register_invalid_version
AGENTX_TARGETS+=		agentx_register_instance_registration
AGENTX_TARGETS+=		agentx_register_new_index
AGENTX_TARGETS+=		agentx_register_duplicate_self
AGENTX_TARGETS+=		agentx_register_duplicate_twocon
AGENTX_TARGETS+=		agentx_register_duplicate_priority
AGENTX_TARGETS+=		agentx_register_range
AGENTX_TARGETS+=		agentx_register_range_invalidupperbound
AGENTX_TARGETS+=		agentx_register_range_single
AGENTX_TARGETS+=		agentx_register_range_overlap_single
AGENTX_TARGETS+=		agentx_register_single_overlap_range
AGENTX_TARGETS+=		agentx_register_range_overlap_range
AGENTX_TARGETS+=		agentx_register_below
AGENTX_TARGETS+=		agentx_register_above
AGENTX_TARGETS+=		agentx_register_restricted
AGENTX_TARGETS+=		agentx_unregister_notopen
AGENTX_TARGETS+=		agentx_unregister_invalid_sessionid
AGENTX_TARGETS+=		agentx_unregister_notregistered
AGENTX_TARGETS+=		agentx_unregister_notregistered
AGENTX_TARGETS+=		agentx_unregister_single
AGENTX_TARGETS+=		agentx_unregister_single_notowned
AGENTX_TARGETS+=		agentx_unregister_range
AGENTX_TARGETS+=		agentx_unregister_range_single
# Final choice on if this is the desired behaviour pending on tech@
REGRESS_EXPECTED_FAILURES+=	agentx_unregister_range_subset
AGENTX_TARGETS+=		agentx_unregister_range_subset
AGENTX_TARGETS+=		agentx_unregister_range_extra
AGENTX_TARGETS+=		agentx_unregister_range_priority
AGENTX_TARGETS+=		agentx_unregister_range_notowned

REGRESS_TARGETS+=		${AGENTX_TARGETS}
agentx: ${AGENTX_TARGETS}

# SNMP application core logic: application.c
BACKEND_TARGETS+=		backend_get_integer
BACKEND_TARGETS+=		backend_get_octetstring
BACKEND_TARGETS+=		backend_get_objectidentifier
BACKEND_TARGETS+=		backend_get_ipaddress
BACKEND_TARGETS+=		backend_get_counter32
BACKEND_TARGETS+=		backend_get_gauge32
BACKEND_TARGETS+=		backend_get_timeticks
BACKEND_TARGETS+=		backend_get_opaque
BACKEND_TARGETS+=		backend_get_counter64
BACKEND_TARGETS+=		backend_get_nosuchobject
BACKEND_TARGETS+=		backend_get_nosuchinstance
BACKEND_TARGETS+=		backend_get_endofmibview
BACKEND_TARGETS+=		backend_get_two_single_backend
BACKEND_TARGETS+=		backend_get_two_double_backend
BACKEND_TARGETS+=		backend_get_wrongorder
BACKEND_TARGETS+=		backend_get_toofew
BACKEND_TARGETS+=		backend_get_toomany
BACKEND_TARGETS+=		backend_get_instance
BACKEND_TARGETS+=		backend_get_instance_below
BACKEND_TARGETS+=		backend_get_timeout_default
REGRESS_SLOW_TARGETS+=		backend_get_timeout_default
BACKEND_TARGETS+=		backend_get_timeout_session_lower
REGRESS_SLOW_TARGETS+=		backend_get_timeout_session_lower
BACKEND_TARGETS+=		backend_get_timeout_session_higher
REGRESS_SLOW_TARGETS+=		backend_get_timeout_session_higher
BACKEND_TARGETS+=		backend_get_timeout_region_lower
REGRESS_SLOW_TARGETS+=		backend_get_timeout_region_lower
BACKEND_TARGETS+=		backend_get_timeout_region_higher
REGRESS_SLOW_TARGETS+=		backend_get_timeout_region_higher
BACKEND_TARGETS+=		backend_get_priority_lower
BACKEND_TARGETS+=		backend_get_priority_higher
BACKEND_TARGETS+=		backend_get_priority_below_lower
BACKEND_TARGETS+=		backend_get_priority_below_higher
BACKEND_TARGETS+=		backend_get_close
BACKEND_TARGETS+=		backend_get_close_overlap
BACKEND_TARGETS+=		backend_get_disappear
BACKEND_TARGETS+=		backend_get_disappear_overlap
BACKEND_TARGETS+=		backend_get_disappear_doublesession
BACKEND_TARGETS+=		backend_get_octetstring_max
BACKEND_TARGETS+=		backend_get_octetstring_too_long
BACKEND_TARGETS+=		backend_get_ipaddress_too_short
BACKEND_TARGETS+=		backend_get_ipaddress_too_long
BACKEND_TARGETS+=		backend_get_opaque_non_ber
BACKEND_TARGETS+=		backend_get_opaque_double_value
BACKEND_TARGETS+=		backend_getnext_selfbound
BACKEND_TARGETS+=		backend_getnext_lowerbound
BACKEND_TARGETS+=		backend_getnext_lowerbound_self
BACKEND_TARGETS+=		backend_getnext_lowerbound_highprio
BACKEND_TARGETS+=		backend_getnext_lowerbound_lowprio
BACKEND_TARGETS+=		backend_getnext_sibling
BACKEND_TARGETS+=		backend_getnext_child_gap
BACKEND_TARGETS+=		backend_getnext_nosuchobject
BACKEND_TARGETS+=		backend_getnext_nosuchinstance
BACKEND_TARGETS+=		backend_getnext_endofmibview
BACKEND_TARGETS+=		backend_getnext_inclusive
BACKEND_TARGETS+=		backend_getnext_jumpnext
BACKEND_TARGETS+=		backend_getnext_jumpnext_endofmibview
BACKEND_TARGETS+=		backend_getnext_jump_up
BACKEND_TARGETS+=		backend_getnext_two_single_backend
BACKEND_TARGETS+=		backend_getnext_two_double_backend
BACKEND_TARGETS+=		backend_getnext_instance_below
BACKEND_TARGETS+=		backend_getnext_instance
BACKEND_TARGETS+=		backend_getnext_instance_exact
BACKEND_TARGETS+=		backend_getnext_instance_ignore
BACKEND_TARGETS+=		backend_getnext_backwards
BACKEND_TARGETS+=		backend_getnext_stale
BACKEND_TARGETS+=		backend_getnext_inclusive_backwards
BACKEND_TARGETS+=		backend_getnext_toofew
BACKEND_TARGETS+=		backend_getnext_toomany
BACKEND_TARGETS+=		backend_getnext_response_equal_end
BACKEND_TARGETS+=		backend_getnext_instance_below_region_before_instance
BACKEND_TARGETS+=		backend_getnext_instance_below_region_on_instance
BACKEND_TARGETS+=		backend_getnext_instance_below_region_below_instance
BACKEND_TARGETS+=		backend_getbulk_nonrep_zero_maxrep_one
BACKEND_TARGETS+=		backend_getbulk_nonrep_zero_maxrep_two
BACKEND_TARGETS+=		backend_getbulk_nonrep_one_maxrep_one
BACKEND_TARGETS+=		backend_getbulk_nonrep_one_maxrep_two
BACKEND_TARGETS+=		backend_getbulk_nonrep_two_maxrep_two
BACKEND_TARGETS+=		backend_getbulk_nonrep_negative
BACKEND_TARGETS+=		backend_getbulk_endofmibview
BACKEND_TARGETS+=		backend_getbulk_endofmibview_second_rep
BACKEND_TARGETS+=		backend_getbulk_endofmibview_two_varbinds
BACKEND_TARGETS+=		backend_error_get_toobig
BACKEND_TARGETS+=		backend_error_get_nosuchname
BACKEND_TARGETS+=		backend_error_get_badvalue
BACKEND_TARGETS+=		backend_error_get_readonly
BACKEND_TARGETS+=		backend_error_get_generr
BACKEND_TARGETS+=		backend_error_get_wrongtype
BACKEND_TARGETS+=		backend_error_get_wronglength
BACKEND_TARGETS+=		backend_error_get_wrongvalue
BACKEND_TARGETS+=		backend_error_get_nocreation
BACKEND_TARGETS+=		backend_error_get_inconsistentvalue
BACKEND_TARGETS+=		backend_error_get_commitfailed
BACKEND_TARGETS+=		backend_error_get_undofailed
BACKEND_TARGETS+=		backend_error_get_authorizationerror
BACKEND_TARGETS+=		backend_error_get_notwritable
BACKEND_TARGETS+=		backend_error_get_inconsistentname
BACKEND_TARGETS+=		backend_error_get_openfailed
BACKEND_TARGETS+=		backend_error_get_notopen
BACKEND_TARGETS+=		backend_error_get_indexwrongtype
BACKEND_TARGETS+=		backend_error_get_indexalreadyallocated
BACKEND_TARGETS+=		backend_error_get_indexnonavailable
BACKEND_TARGETS+=		backend_error_get_indexnotallocated
BACKEND_TARGETS+=		backend_error_get_duplicateregistration
BACKEND_TARGETS+=		backend_error_get_requestdenied
BACKEND_TARGETS+=		backend_error_get_processingerror
BACKEND_TARGETS+=		backend_error_get_nonstandard
BACKEND_TARGETS+=		backend_error_getnext_toobig
BACKEND_TARGETS+=		backend_error_getnext_nosuchname
BACKEND_TARGETS+=		backend_error_getnext_badvalue
BACKEND_TARGETS+=		backend_error_getnext_readonly
BACKEND_TARGETS+=		backend_error_getnext_generr
BACKEND_TARGETS+=		backend_error_getnext_noaccess
BACKEND_TARGETS+=		backend_error_getnext_wrongtype
BACKEND_TARGETS+=		backend_error_getnext_wronglength
BACKEND_TARGETS+=		backend_error_getnext_wrongencoding
BACKEND_TARGETS+=		backend_error_getnext_wrongvalue
BACKEND_TARGETS+=		backend_error_getnext_nocreation
BACKEND_TARGETS+=		backend_error_getnext_inconsistentvalue
BACKEND_TARGETS+=		backend_error_getnext_resourceunavailable
BACKEND_TARGETS+=		backend_error_getnext_commitfailed
BACKEND_TARGETS+=		backend_error_getnext_undofailed
BACKEND_TARGETS+=		backend_error_getnext_notwritable
BACKEND_TARGETS+=		backend_error_getnext_inconsistentname
BACKEND_TARGETS+=		backend_error_getnext_openfailed
BACKEND_TARGETS+=		backend_error_getnext_notopen
BACKEND_TARGETS+=		backend_error_getnext_indexwrongtype
BACKEND_TARGETS+=		backend_error_getnext_indexalreadyallocated
BACKEND_TARGETS+=		backend_error_getnext_indexnonavailable
BACKEND_TARGETS+=		backend_error_getnext_indexnotallocated
BACKEND_TARGETS+=		backend_error_getnext_unsupportedcontext
BACKEND_TARGETS+=		backend_error_getnext_duplicateregistration
BACKEND_TARGETS+=		backend_error_getnext_unknownregistration
BACKEND_TARGETS+=		backend_error_getnext_parseerror
BACKEND_TARGETS+=		backend_error_getnext_requestdenied
BACKEND_TARGETS+=		backend_error_getnext_processingerror
BACKEND_TARGETS+=		backend_error_getnext_nonstandard
BACKEND_TARGETS+=		backend_error_getbulk_firstrepetition
BACKEND_TARGETS+=		backend_error_getbulk_secondrepetition

REGRESS_TARGETS+=		${BACKEND_TARGETS}
backend: ${BACKEND_TARGETS}

SNMP_TARGETS+=			snmp_v3_usm_noauthpriv

REGRESS_TARGETS+=		${SNMP_TARGETS}
snmp: ${SNMP_TARGETS}

TRANSPORT_TARGETS+=		transport_tcp_get
TRANSPORT_TARGETS+=		transport_tcp_disconnect
TRANSPORT_TARGETS+=		transport_tcp_double_get_disconnect

REGRESS_TARGETS+=		${TRANSPORT_TARGETS}
transport: ${TRANSPORT_TARGETS}

.PHONY:				agentx backend snmp transport

${REGRESS_TARGETS}: snmpd_regress
# Always start snmpd if it's not running
	@pgrep -q snmpd || ( \
		printf "%s\n" "${SNMPD_START}"; \
		rm -f /tmp/agentx 2>/dev/null; \
		${SNMPD_START} \
		i=0; \
		while [ ! -S /tmp/agentx ]; do \
			i=$$((i + 1)); \
			[ $$i -eq 100 ] && ( \
				printf "Failed to start snmpd\n"; >&2 \
				${SUDO} pkill -f ${SNMPD}; \
				exit 1; \
			); \
			sleep 0.1; \
		done \
	)
	./snmpd_regress ${SNMPD_REGRESS_FLAGS} $@
# Make sure that snmpd hasn't crashed in the meantime.
	@sleep 0.01
	@pgrep -q snmpd

.include <bsd.regress.mk>
