/* This file was created automatically
 * Source file: $Begemot: libunimsg/atm/msg/ie.def,v 1.3 2003/09/19 11:58:15 hbb Exp $
 * $FreeBSD$
 */


static void uni_ie_print_itu_cause(struct uni_ie_cause *, struct unicx *);
static int uni_ie_check_itu_cause(struct uni_ie_cause *, struct unicx *);
static int uni_ie_encode_itu_cause(struct uni_msg *, struct uni_ie_cause *, struct unicx *);
static int uni_ie_decode_itu_cause(struct uni_ie_cause *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_cause = {
	0,
	34,
	(uni_print_f)uni_ie_print_itu_cause,
	(uni_check_f)uni_ie_check_itu_cause,
	(uni_encode_f)uni_ie_encode_itu_cause,
	(uni_decode_f)uni_ie_decode_itu_cause
};

static void uni_ie_print_net_cause(struct uni_ie_cause *, struct unicx *);
static int uni_ie_check_net_cause(struct uni_ie_cause *, struct unicx *);
static int uni_ie_encode_net_cause(struct uni_msg *, struct uni_ie_cause *, struct unicx *);
static int uni_ie_decode_net_cause(struct uni_ie_cause *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_net_cause = {
	0,
	34,
	(uni_print_f)uni_ie_print_net_cause,
	(uni_check_f)uni_ie_check_net_cause,
	(uni_encode_f)uni_ie_encode_net_cause,
	(uni_decode_f)uni_ie_decode_net_cause
};

static void uni_ie_print_itu_callstate(struct uni_ie_callstate *, struct unicx *);
static int uni_ie_check_itu_callstate(struct uni_ie_callstate *, struct unicx *);
static int uni_ie_encode_itu_callstate(struct uni_msg *, struct uni_ie_callstate *, struct unicx *);
static int uni_ie_decode_itu_callstate(struct uni_ie_callstate *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_callstate = {
	0,
	5,
	(uni_print_f)uni_ie_print_itu_callstate,
	(uni_check_f)uni_ie_check_itu_callstate,
	(uni_encode_f)uni_ie_encode_itu_callstate,
	(uni_decode_f)uni_ie_decode_itu_callstate
};

static void uni_ie_print_itu_facility(struct uni_ie_facility *, struct unicx *);
static int uni_ie_check_itu_facility(struct uni_ie_facility *, struct unicx *);
static int uni_ie_encode_itu_facility(struct uni_msg *, struct uni_ie_facility *, struct unicx *);
static int uni_ie_decode_itu_facility(struct uni_ie_facility *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_facility = {
	0,
	UNI_FACILITY_MAXAPDU+1+4,
	(uni_print_f)uni_ie_print_itu_facility,
	(uni_check_f)uni_ie_check_itu_facility,
	(uni_encode_f)uni_ie_encode_itu_facility,
	(uni_decode_f)uni_ie_decode_itu_facility
};

static void uni_ie_print_itu_notify(struct uni_ie_notify *, struct unicx *);
static int uni_ie_check_itu_notify(struct uni_ie_notify *, struct unicx *);
static int uni_ie_encode_itu_notify(struct uni_msg *, struct uni_ie_notify *, struct unicx *);
static int uni_ie_decode_itu_notify(struct uni_ie_notify *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_notify = {
	0,
	UNI_NOTIFY_MAXLEN+4,
	(uni_print_f)uni_ie_print_itu_notify,
	(uni_check_f)uni_ie_check_itu_notify,
	(uni_encode_f)uni_ie_encode_itu_notify,
	(uni_decode_f)uni_ie_decode_itu_notify
};

static void uni_ie_print_itu_eetd(struct uni_ie_eetd *, struct unicx *);
static int uni_ie_check_itu_eetd(struct uni_ie_eetd *, struct unicx *);
static int uni_ie_encode_itu_eetd(struct uni_msg *, struct uni_ie_eetd *, struct unicx *);
static int uni_ie_decode_itu_eetd(struct uni_ie_eetd *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_eetd = {
	0,
	11,
	(uni_print_f)uni_ie_print_itu_eetd,
	(uni_check_f)uni_ie_check_itu_eetd,
	(uni_encode_f)uni_ie_encode_itu_eetd,
	(uni_decode_f)uni_ie_decode_itu_eetd
};

static void uni_ie_print_net_eetd(struct uni_ie_eetd *, struct unicx *);
static int uni_ie_check_net_eetd(struct uni_ie_eetd *, struct unicx *);
static int uni_ie_encode_net_eetd(struct uni_msg *, struct uni_ie_eetd *, struct unicx *);
static int uni_ie_decode_net_eetd(struct uni_ie_eetd *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_net_eetd = {
	0,
	13,
	(uni_print_f)uni_ie_print_net_eetd,
	(uni_check_f)uni_ie_check_net_eetd,
	(uni_encode_f)uni_ie_encode_net_eetd,
	(uni_decode_f)uni_ie_decode_net_eetd
};

static void uni_ie_print_itu_conned(struct uni_ie_conned *, struct unicx *);
static int uni_ie_check_itu_conned(struct uni_ie_conned *, struct unicx *);
static int uni_ie_encode_itu_conned(struct uni_msg *, struct uni_ie_conned *, struct unicx *);
static int uni_ie_decode_itu_conned(struct uni_ie_conned *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_conned = {
	0,
	25,
	(uni_print_f)uni_ie_print_itu_conned,
	(uni_check_f)uni_ie_check_itu_conned,
	(uni_encode_f)uni_ie_encode_itu_conned,
	(uni_decode_f)uni_ie_decode_itu_conned
};

static void uni_ie_print_itu_connedsub(struct uni_ie_connedsub *, struct unicx *);
static int uni_ie_check_itu_connedsub(struct uni_ie_connedsub *, struct unicx *);
static int uni_ie_encode_itu_connedsub(struct uni_msg *, struct uni_ie_connedsub *, struct unicx *);
static int uni_ie_decode_itu_connedsub(struct uni_ie_connedsub *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_connedsub = {
	UNIFL_ACCESS,
	25,
	(uni_print_f)uni_ie_print_itu_connedsub,
	(uni_check_f)uni_ie_check_itu_connedsub,
	(uni_encode_f)uni_ie_encode_itu_connedsub,
	(uni_decode_f)uni_ie_decode_itu_connedsub
};

static void uni_ie_print_itu_epref(struct uni_ie_epref *, struct unicx *);
static int uni_ie_check_itu_epref(struct uni_ie_epref *, struct unicx *);
static int uni_ie_encode_itu_epref(struct uni_msg *, struct uni_ie_epref *, struct unicx *);
static int uni_ie_decode_itu_epref(struct uni_ie_epref *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_epref = {
	0,
	7,
	(uni_print_f)uni_ie_print_itu_epref,
	(uni_check_f)uni_ie_check_itu_epref,
	(uni_encode_f)uni_ie_encode_itu_epref,
	(uni_decode_f)uni_ie_decode_itu_epref
};

static void uni_ie_print_itu_epstate(struct uni_ie_epstate *, struct unicx *);
static int uni_ie_check_itu_epstate(struct uni_ie_epstate *, struct unicx *);
static int uni_ie_encode_itu_epstate(struct uni_msg *, struct uni_ie_epstate *, struct unicx *);
static int uni_ie_decode_itu_epstate(struct uni_ie_epstate *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_epstate = {
	0,
	5,
	(uni_print_f)uni_ie_print_itu_epstate,
	(uni_check_f)uni_ie_check_itu_epstate,
	(uni_encode_f)uni_ie_encode_itu_epstate,
	(uni_decode_f)uni_ie_decode_itu_epstate
};

static void uni_ie_print_itu_aal(struct uni_ie_aal *, struct unicx *);
static int uni_ie_check_itu_aal(struct uni_ie_aal *, struct unicx *);
static int uni_ie_encode_itu_aal(struct uni_msg *, struct uni_ie_aal *, struct unicx *);
static int uni_ie_decode_itu_aal(struct uni_ie_aal *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_aal = {
	UNIFL_ACCESS,
	21,
	(uni_print_f)uni_ie_print_itu_aal,
	(uni_check_f)uni_ie_check_itu_aal,
	(uni_encode_f)uni_ie_encode_itu_aal,
	(uni_decode_f)uni_ie_decode_itu_aal
};

static void uni_ie_print_itu_traffic(struct uni_ie_traffic *, struct unicx *);
static int uni_ie_check_itu_traffic(struct uni_ie_traffic *, struct unicx *);
static int uni_ie_encode_itu_traffic(struct uni_msg *, struct uni_ie_traffic *, struct unicx *);
static int uni_ie_decode_itu_traffic(struct uni_ie_traffic *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_traffic = {
	0,
	30,
	(uni_print_f)uni_ie_print_itu_traffic,
	(uni_check_f)uni_ie_check_itu_traffic,
	(uni_encode_f)uni_ie_encode_itu_traffic,
	(uni_decode_f)uni_ie_decode_itu_traffic
};

static const struct iedecl decl_net_traffic = {
	UNIFL_DEFAULT,
	0,
	(uni_print_f)NULL,
	(uni_check_f)NULL,
	(uni_encode_f)NULL,
	(uni_decode_f)NULL
};

static void uni_ie_print_itu_connid(struct uni_ie_connid *, struct unicx *);
static int uni_ie_check_itu_connid(struct uni_ie_connid *, struct unicx *);
static int uni_ie_encode_itu_connid(struct uni_msg *, struct uni_ie_connid *, struct unicx *);
static int uni_ie_decode_itu_connid(struct uni_ie_connid *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_connid = {
	0,
	9,
	(uni_print_f)uni_ie_print_itu_connid,
	(uni_check_f)uni_ie_check_itu_connid,
	(uni_encode_f)uni_ie_encode_itu_connid,
	(uni_decode_f)uni_ie_decode_itu_connid
};

static void uni_ie_print_itu_qos(struct uni_ie_qos *, struct unicx *);
static int uni_ie_check_itu_qos(struct uni_ie_qos *, struct unicx *);
static int uni_ie_encode_itu_qos(struct uni_msg *, struct uni_ie_qos *, struct unicx *);
static int uni_ie_decode_itu_qos(struct uni_ie_qos *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_qos = {
	0,
	6,
	(uni_print_f)uni_ie_print_itu_qos,
	(uni_check_f)uni_ie_check_itu_qos,
	(uni_encode_f)uni_ie_encode_itu_qos,
	(uni_decode_f)uni_ie_decode_itu_qos
};

static void uni_ie_print_net_qos(struct uni_ie_qos *, struct unicx *);
static int uni_ie_check_net_qos(struct uni_ie_qos *, struct unicx *);
static int uni_ie_encode_net_qos(struct uni_msg *, struct uni_ie_qos *, struct unicx *);
static int uni_ie_decode_net_qos(struct uni_ie_qos *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_net_qos = {
	0,
	6,
	(uni_print_f)uni_ie_print_net_qos,
	(uni_check_f)uni_ie_check_net_qos,
	(uni_encode_f)uni_ie_encode_net_qos,
	(uni_decode_f)uni_ie_decode_net_qos
};

static void uni_ie_print_itu_bhli(struct uni_ie_bhli *, struct unicx *);
static int uni_ie_check_itu_bhli(struct uni_ie_bhli *, struct unicx *);
static int uni_ie_encode_itu_bhli(struct uni_msg *, struct uni_ie_bhli *, struct unicx *);
static int uni_ie_decode_itu_bhli(struct uni_ie_bhli *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_bhli = {
	UNIFL_ACCESS,
	13,
	(uni_print_f)uni_ie_print_itu_bhli,
	(uni_check_f)uni_ie_check_itu_bhli,
	(uni_encode_f)uni_ie_encode_itu_bhli,
	(uni_decode_f)uni_ie_decode_itu_bhli
};

static void uni_ie_print_itu_bearer(struct uni_ie_bearer *, struct unicx *);
static int uni_ie_check_itu_bearer(struct uni_ie_bearer *, struct unicx *);
static int uni_ie_encode_itu_bearer(struct uni_msg *, struct uni_ie_bearer *, struct unicx *);
static int uni_ie_decode_itu_bearer(struct uni_ie_bearer *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_bearer = {
	0,
	7,
	(uni_print_f)uni_ie_print_itu_bearer,
	(uni_check_f)uni_ie_check_itu_bearer,
	(uni_encode_f)uni_ie_encode_itu_bearer,
	(uni_decode_f)uni_ie_decode_itu_bearer
};

static void uni_ie_print_itu_blli(struct uni_ie_blli *, struct unicx *);
static int uni_ie_check_itu_blli(struct uni_ie_blli *, struct unicx *);
static int uni_ie_encode_itu_blli(struct uni_msg *, struct uni_ie_blli *, struct unicx *);
static int uni_ie_decode_itu_blli(struct uni_ie_blli *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_blli = {
	UNIFL_ACCESS,
	17,
	(uni_print_f)uni_ie_print_itu_blli,
	(uni_check_f)uni_ie_check_itu_blli,
	(uni_encode_f)uni_ie_encode_itu_blli,
	(uni_decode_f)uni_ie_decode_itu_blli
};

static void uni_ie_print_itu_lshift(struct uni_ie_lshift *, struct unicx *);
static int uni_ie_check_itu_lshift(struct uni_ie_lshift *, struct unicx *);
static int uni_ie_encode_itu_lshift(struct uni_msg *, struct uni_ie_lshift *, struct unicx *);
static int uni_ie_decode_itu_lshift(struct uni_ie_lshift *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_lshift = {
	0,
	5,
	(uni_print_f)uni_ie_print_itu_lshift,
	(uni_check_f)uni_ie_check_itu_lshift,
	(uni_encode_f)uni_ie_encode_itu_lshift,
	(uni_decode_f)uni_ie_decode_itu_lshift
};

static void uni_ie_print_itu_nlshift(struct uni_ie_nlshift *, struct unicx *);
static int uni_ie_check_itu_nlshift(struct uni_ie_nlshift *, struct unicx *);
static int uni_ie_encode_itu_nlshift(struct uni_msg *, struct uni_ie_nlshift *, struct unicx *);
static int uni_ie_decode_itu_nlshift(struct uni_ie_nlshift *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_nlshift = {
	0,
	5,
	(uni_print_f)uni_ie_print_itu_nlshift,
	(uni_check_f)uni_ie_check_itu_nlshift,
	(uni_encode_f)uni_ie_encode_itu_nlshift,
	(uni_decode_f)uni_ie_decode_itu_nlshift
};

static void uni_ie_print_itu_scompl(struct uni_ie_scompl *, struct unicx *);
static int uni_ie_check_itu_scompl(struct uni_ie_scompl *, struct unicx *);
static int uni_ie_encode_itu_scompl(struct uni_msg *, struct uni_ie_scompl *, struct unicx *);
static int uni_ie_decode_itu_scompl(struct uni_ie_scompl *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_scompl = {
	0,
	5,
	(uni_print_f)uni_ie_print_itu_scompl,
	(uni_check_f)uni_ie_check_itu_scompl,
	(uni_encode_f)uni_ie_encode_itu_scompl,
	(uni_decode_f)uni_ie_decode_itu_scompl
};

static void uni_ie_print_itu_repeat(struct uni_ie_repeat *, struct unicx *);
static int uni_ie_check_itu_repeat(struct uni_ie_repeat *, struct unicx *);
static int uni_ie_encode_itu_repeat(struct uni_msg *, struct uni_ie_repeat *, struct unicx *);
static int uni_ie_decode_itu_repeat(struct uni_ie_repeat *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_repeat = {
	0,
	5,
	(uni_print_f)uni_ie_print_itu_repeat,
	(uni_check_f)uni_ie_check_itu_repeat,
	(uni_encode_f)uni_ie_encode_itu_repeat,
	(uni_decode_f)uni_ie_decode_itu_repeat
};

static void uni_ie_print_itu_calling(struct uni_ie_calling *, struct unicx *);
static int uni_ie_check_itu_calling(struct uni_ie_calling *, struct unicx *);
static int uni_ie_encode_itu_calling(struct uni_msg *, struct uni_ie_calling *, struct unicx *);
static int uni_ie_decode_itu_calling(struct uni_ie_calling *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_calling = {
	0,
	26,
	(uni_print_f)uni_ie_print_itu_calling,
	(uni_check_f)uni_ie_check_itu_calling,
	(uni_encode_f)uni_ie_encode_itu_calling,
	(uni_decode_f)uni_ie_decode_itu_calling
};

static void uni_ie_print_itu_callingsub(struct uni_ie_callingsub *, struct unicx *);
static int uni_ie_check_itu_callingsub(struct uni_ie_callingsub *, struct unicx *);
static int uni_ie_encode_itu_callingsub(struct uni_msg *, struct uni_ie_callingsub *, struct unicx *);
static int uni_ie_decode_itu_callingsub(struct uni_ie_callingsub *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_callingsub = {
	UNIFL_ACCESS,
	25,
	(uni_print_f)uni_ie_print_itu_callingsub,
	(uni_check_f)uni_ie_check_itu_callingsub,
	(uni_encode_f)uni_ie_encode_itu_callingsub,
	(uni_decode_f)uni_ie_decode_itu_callingsub
};

static void uni_ie_print_itu_called(struct uni_ie_called *, struct unicx *);
static int uni_ie_check_itu_called(struct uni_ie_called *, struct unicx *);
static int uni_ie_encode_itu_called(struct uni_msg *, struct uni_ie_called *, struct unicx *);
static int uni_ie_decode_itu_called(struct uni_ie_called *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_called = {
	0,
	25,
	(uni_print_f)uni_ie_print_itu_called,
	(uni_check_f)uni_ie_check_itu_called,
	(uni_encode_f)uni_ie_encode_itu_called,
	(uni_decode_f)uni_ie_decode_itu_called
};

static void uni_ie_print_itu_calledsub(struct uni_ie_calledsub *, struct unicx *);
static int uni_ie_check_itu_calledsub(struct uni_ie_calledsub *, struct unicx *);
static int uni_ie_encode_itu_calledsub(struct uni_msg *, struct uni_ie_calledsub *, struct unicx *);
static int uni_ie_decode_itu_calledsub(struct uni_ie_calledsub *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_calledsub = {
	UNIFL_ACCESS,
	25,
	(uni_print_f)uni_ie_print_itu_calledsub,
	(uni_check_f)uni_ie_check_itu_calledsub,
	(uni_encode_f)uni_ie_encode_itu_calledsub,
	(uni_decode_f)uni_ie_decode_itu_calledsub
};

static void uni_ie_print_itu_tns(struct uni_ie_tns *, struct unicx *);
static int uni_ie_check_itu_tns(struct uni_ie_tns *, struct unicx *);
static int uni_ie_encode_itu_tns(struct uni_msg *, struct uni_ie_tns *, struct unicx *);
static int uni_ie_decode_itu_tns(struct uni_ie_tns *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_tns = {
	0,
	9,
	(uni_print_f)uni_ie_print_itu_tns,
	(uni_check_f)uni_ie_check_itu_tns,
	(uni_encode_f)uni_ie_encode_itu_tns,
	(uni_decode_f)uni_ie_decode_itu_tns
};

static const struct iedecl decl_net_tns = {
	UNIFL_DEFAULT,
	0,
	(uni_print_f)NULL,
	(uni_check_f)NULL,
	(uni_encode_f)NULL,
	(uni_decode_f)NULL
};

static void uni_ie_print_itu_restart(struct uni_ie_restart *, struct unicx *);
static int uni_ie_check_itu_restart(struct uni_ie_restart *, struct unicx *);
static int uni_ie_encode_itu_restart(struct uni_msg *, struct uni_ie_restart *, struct unicx *);
static int uni_ie_decode_itu_restart(struct uni_ie_restart *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_restart = {
	0,
	5,
	(uni_print_f)uni_ie_print_itu_restart,
	(uni_check_f)uni_ie_check_itu_restart,
	(uni_encode_f)uni_ie_encode_itu_restart,
	(uni_decode_f)uni_ie_decode_itu_restart
};

static void uni_ie_print_itu_uu(struct uni_ie_uu *, struct unicx *);
static int uni_ie_check_itu_uu(struct uni_ie_uu *, struct unicx *);
static int uni_ie_encode_itu_uu(struct uni_msg *, struct uni_ie_uu *, struct unicx *);
static int uni_ie_decode_itu_uu(struct uni_ie_uu *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_uu = {
	UNIFL_ACCESS,
	UNI_UU_MAXLEN+4,
	(uni_print_f)uni_ie_print_itu_uu,
	(uni_check_f)uni_ie_check_itu_uu,
	(uni_encode_f)uni_ie_encode_itu_uu,
	(uni_decode_f)uni_ie_decode_itu_uu
};

static void uni_ie_print_net_git(struct uni_ie_git *, struct unicx *);
static int uni_ie_check_net_git(struct uni_ie_git *, struct unicx *);
static int uni_ie_encode_net_git(struct uni_msg *, struct uni_ie_git *, struct unicx *);
static int uni_ie_decode_net_git(struct uni_ie_git *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_net_git = {
	0,
	33,
	(uni_print_f)uni_ie_print_net_git,
	(uni_check_f)uni_ie_check_net_git,
	(uni_encode_f)uni_ie_encode_net_git,
	(uni_decode_f)uni_ie_decode_net_git
};

static void uni_ie_print_itu_mintraffic(struct uni_ie_mintraffic *, struct unicx *);
static int uni_ie_check_itu_mintraffic(struct uni_ie_mintraffic *, struct unicx *);
static int uni_ie_encode_itu_mintraffic(struct uni_msg *, struct uni_ie_mintraffic *, struct unicx *);
static int uni_ie_decode_itu_mintraffic(struct uni_ie_mintraffic *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_mintraffic = {
	0,
	20,
	(uni_print_f)uni_ie_print_itu_mintraffic,
	(uni_check_f)uni_ie_check_itu_mintraffic,
	(uni_encode_f)uni_ie_encode_itu_mintraffic,
	(uni_decode_f)uni_ie_decode_itu_mintraffic
};

static const struct iedecl decl_net_mintraffic = {
	UNIFL_DEFAULT,
	0,
	(uni_print_f)NULL,
	(uni_check_f)NULL,
	(uni_encode_f)NULL,
	(uni_decode_f)NULL
};

static void uni_ie_print_itu_atraffic(struct uni_ie_atraffic *, struct unicx *);
static int uni_ie_check_itu_atraffic(struct uni_ie_atraffic *, struct unicx *);
static int uni_ie_encode_itu_atraffic(struct uni_msg *, struct uni_ie_atraffic *, struct unicx *);
static int uni_ie_decode_itu_atraffic(struct uni_ie_atraffic *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_atraffic = {
	0,
	30,
	(uni_print_f)uni_ie_print_itu_atraffic,
	(uni_check_f)uni_ie_check_itu_atraffic,
	(uni_encode_f)uni_ie_encode_itu_atraffic,
	(uni_decode_f)uni_ie_decode_itu_atraffic
};

static const struct iedecl decl_net_atraffic = {
	UNIFL_DEFAULT,
	0,
	(uni_print_f)NULL,
	(uni_check_f)NULL,
	(uni_encode_f)NULL,
	(uni_decode_f)NULL
};

static void uni_ie_print_net_abrsetup(struct uni_ie_abrsetup *, struct unicx *);
static int uni_ie_check_net_abrsetup(struct uni_ie_abrsetup *, struct unicx *);
static int uni_ie_encode_net_abrsetup(struct uni_msg *, struct uni_ie_abrsetup *, struct unicx *);
static int uni_ie_decode_net_abrsetup(struct uni_ie_abrsetup *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_net_abrsetup = {
	0,
	36,
	(uni_print_f)uni_ie_print_net_abrsetup,
	(uni_check_f)uni_ie_check_net_abrsetup,
	(uni_encode_f)uni_ie_encode_net_abrsetup,
	(uni_decode_f)uni_ie_decode_net_abrsetup
};

static void uni_ie_print_itu_report(struct uni_ie_report *, struct unicx *);
static int uni_ie_check_itu_report(struct uni_ie_report *, struct unicx *);
static int uni_ie_encode_itu_report(struct uni_msg *, struct uni_ie_report *, struct unicx *);
static int uni_ie_decode_itu_report(struct uni_ie_report *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_report = {
	0,
	5,
	(uni_print_f)uni_ie_print_itu_report,
	(uni_check_f)uni_ie_check_itu_report,
	(uni_encode_f)uni_ie_encode_itu_report,
	(uni_decode_f)uni_ie_decode_itu_report
};

static void uni_ie_print_net_called_soft(struct uni_ie_called_soft *, struct unicx *);
static int uni_ie_check_net_called_soft(struct uni_ie_called_soft *, struct unicx *);
static int uni_ie_encode_net_called_soft(struct uni_msg *, struct uni_ie_called_soft *, struct unicx *);
static int uni_ie_decode_net_called_soft(struct uni_ie_called_soft *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_net_called_soft = {
	0,
	11,
	(uni_print_f)uni_ie_print_net_called_soft,
	(uni_check_f)uni_ie_check_net_called_soft,
	(uni_encode_f)uni_ie_encode_net_called_soft,
	(uni_decode_f)uni_ie_decode_net_called_soft
};

static void uni_ie_print_net_crankback(struct uni_ie_crankback *, struct unicx *);
static int uni_ie_check_net_crankback(struct uni_ie_crankback *, struct unicx *);
static int uni_ie_encode_net_crankback(struct uni_msg *, struct uni_ie_crankback *, struct unicx *);
static int uni_ie_decode_net_crankback(struct uni_ie_crankback *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_net_crankback = {
	0,
	72,
	(uni_print_f)uni_ie_print_net_crankback,
	(uni_check_f)uni_ie_check_net_crankback,
	(uni_encode_f)uni_ie_encode_net_crankback,
	(uni_decode_f)uni_ie_decode_net_crankback
};

static void uni_ie_print_net_dtl(struct uni_ie_dtl *, struct unicx *);
static int uni_ie_check_net_dtl(struct uni_ie_dtl *, struct unicx *);
static int uni_ie_encode_net_dtl(struct uni_msg *, struct uni_ie_dtl *, struct unicx *);
static int uni_ie_decode_net_dtl(struct uni_ie_dtl *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_net_dtl = {
	0,
	UNI_DTL_LOGNP_SIZE*UNI_DTL_MAXNUM+6,
	(uni_print_f)uni_ie_print_net_dtl,
	(uni_check_f)uni_ie_check_net_dtl,
	(uni_encode_f)uni_ie_encode_net_dtl,
	(uni_decode_f)uni_ie_decode_net_dtl
};

static void uni_ie_print_net_calling_soft(struct uni_ie_calling_soft *, struct unicx *);
static int uni_ie_check_net_calling_soft(struct uni_ie_calling_soft *, struct unicx *);
static int uni_ie_encode_net_calling_soft(struct uni_msg *, struct uni_ie_calling_soft *, struct unicx *);
static int uni_ie_decode_net_calling_soft(struct uni_ie_calling_soft *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_net_calling_soft = {
	0,
	10,
	(uni_print_f)uni_ie_print_net_calling_soft,
	(uni_check_f)uni_ie_check_net_calling_soft,
	(uni_encode_f)uni_ie_encode_net_calling_soft,
	(uni_decode_f)uni_ie_decode_net_calling_soft
};

static void uni_ie_print_net_abradd(struct uni_ie_abradd *, struct unicx *);
static int uni_ie_check_net_abradd(struct uni_ie_abradd *, struct unicx *);
static int uni_ie_encode_net_abradd(struct uni_msg *, struct uni_ie_abradd *, struct unicx *);
static int uni_ie_decode_net_abradd(struct uni_ie_abradd *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_net_abradd = {
	0,
	14,
	(uni_print_f)uni_ie_print_net_abradd,
	(uni_check_f)uni_ie_check_net_abradd,
	(uni_encode_f)uni_ie_encode_net_abradd,
	(uni_decode_f)uni_ie_decode_net_abradd
};

static void uni_ie_print_net_lij_callid(struct uni_ie_lij_callid *, struct unicx *);
static int uni_ie_check_net_lij_callid(struct uni_ie_lij_callid *, struct unicx *);
static int uni_ie_encode_net_lij_callid(struct uni_msg *, struct uni_ie_lij_callid *, struct unicx *);
static int uni_ie_decode_net_lij_callid(struct uni_ie_lij_callid *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_net_lij_callid = {
	0,
	9,
	(uni_print_f)uni_ie_print_net_lij_callid,
	(uni_check_f)uni_ie_check_net_lij_callid,
	(uni_encode_f)uni_ie_encode_net_lij_callid,
	(uni_decode_f)uni_ie_decode_net_lij_callid
};

static void uni_ie_print_net_lij_param(struct uni_ie_lij_param *, struct unicx *);
static int uni_ie_check_net_lij_param(struct uni_ie_lij_param *, struct unicx *);
static int uni_ie_encode_net_lij_param(struct uni_msg *, struct uni_ie_lij_param *, struct unicx *);
static int uni_ie_decode_net_lij_param(struct uni_ie_lij_param *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_net_lij_param = {
	0,
	5,
	(uni_print_f)uni_ie_print_net_lij_param,
	(uni_check_f)uni_ie_check_net_lij_param,
	(uni_encode_f)uni_ie_encode_net_lij_param,
	(uni_decode_f)uni_ie_decode_net_lij_param
};

static void uni_ie_print_net_lij_seqno(struct uni_ie_lij_seqno *, struct unicx *);
static int uni_ie_check_net_lij_seqno(struct uni_ie_lij_seqno *, struct unicx *);
static int uni_ie_encode_net_lij_seqno(struct uni_msg *, struct uni_ie_lij_seqno *, struct unicx *);
static int uni_ie_decode_net_lij_seqno(struct uni_ie_lij_seqno *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_net_lij_seqno = {
	0,
	8,
	(uni_print_f)uni_ie_print_net_lij_seqno,
	(uni_check_f)uni_ie_check_net_lij_seqno,
	(uni_encode_f)uni_ie_encode_net_lij_seqno,
	(uni_decode_f)uni_ie_decode_net_lij_seqno
};

static void uni_ie_print_net_cscope(struct uni_ie_cscope *, struct unicx *);
static int uni_ie_check_net_cscope(struct uni_ie_cscope *, struct unicx *);
static int uni_ie_encode_net_cscope(struct uni_msg *, struct uni_ie_cscope *, struct unicx *);
static int uni_ie_decode_net_cscope(struct uni_ie_cscope *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_net_cscope = {
	0,
	6,
	(uni_print_f)uni_ie_print_net_cscope,
	(uni_check_f)uni_ie_check_net_cscope,
	(uni_encode_f)uni_ie_encode_net_cscope,
	(uni_decode_f)uni_ie_decode_net_cscope
};

static void uni_ie_print_net_exqos(struct uni_ie_exqos *, struct unicx *);
static int uni_ie_check_net_exqos(struct uni_ie_exqos *, struct unicx *);
static int uni_ie_encode_net_exqos(struct uni_msg *, struct uni_ie_exqos *, struct unicx *);
static int uni_ie_decode_net_exqos(struct uni_ie_exqos *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_net_exqos = {
	0,
	25,
	(uni_print_f)uni_ie_print_net_exqos,
	(uni_check_f)uni_ie_check_net_exqos,
	(uni_encode_f)uni_ie_encode_net_exqos,
	(uni_decode_f)uni_ie_decode_net_exqos
};

static void uni_ie_print_net_mdcr(struct uni_ie_mdcr *, struct unicx *);
static int uni_ie_check_net_mdcr(struct uni_ie_mdcr *, struct unicx *);
static int uni_ie_encode_net_mdcr(struct uni_msg *, struct uni_ie_mdcr *, struct unicx *);
static int uni_ie_decode_net_mdcr(struct uni_ie_mdcr *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_net_mdcr = {
	0,
	13,
	(uni_print_f)uni_ie_print_net_mdcr,
	(uni_check_f)uni_ie_check_net_mdcr,
	(uni_encode_f)uni_ie_encode_net_mdcr,
	(uni_decode_f)uni_ie_decode_net_mdcr
};

static void uni_ie_print_itu_unrec(struct uni_ie_unrec *, struct unicx *);
static int uni_ie_check_itu_unrec(struct uni_ie_unrec *, struct unicx *);
static int uni_ie_encode_itu_unrec(struct uni_msg *, struct uni_ie_unrec *, struct unicx *);
static int uni_ie_decode_itu_unrec(struct uni_ie_unrec *, struct uni_msg *, u_int, struct unicx *);

static struct iedecl decl_itu_unrec = {
	0,
	128,
	(uni_print_f)uni_ie_print_itu_unrec,
	(uni_check_f)uni_ie_check_itu_unrec,
	(uni_encode_f)uni_ie_encode_itu_unrec,
	(uni_decode_f)uni_ie_decode_itu_unrec
};

const struct iedecl *uni_ietable[256][4] = {
	{ NULL, NULL, NULL, NULL, }, /* 0x00 */
	{ NULL, NULL, NULL, NULL, }, /* 0x01 */
	{ NULL, NULL, NULL, NULL, }, /* 0x02 */
	{ NULL, NULL, NULL, NULL, }, /* 0x03 */
	{ NULL, NULL, NULL, NULL, }, /* 0x04 */
	{ NULL, NULL, NULL, NULL, }, /* 0x05 */
	{ NULL, NULL, NULL, NULL, }, /* 0x06 */
	{ NULL, NULL, NULL, NULL, }, /* 0x07 */
	{ &decl_itu_cause, NULL, NULL, &decl_net_cause, }, /* 0x08 */
	{ NULL, NULL, NULL, NULL, }, /* 0x09 */
	{ NULL, NULL, NULL, NULL, }, /* 0x0a */
	{ NULL, NULL, NULL, NULL, }, /* 0x0b */
	{ NULL, NULL, NULL, NULL, }, /* 0x0c */
	{ NULL, NULL, NULL, NULL, }, /* 0x0d */
	{ NULL, NULL, NULL, NULL, }, /* 0x0e */
	{ NULL, NULL, NULL, NULL, }, /* 0x0f */
	{ NULL, NULL, NULL, NULL, }, /* 0x10 */
	{ NULL, NULL, NULL, NULL, }, /* 0x11 */
	{ NULL, NULL, NULL, NULL, }, /* 0x12 */
	{ NULL, NULL, NULL, NULL, }, /* 0x13 */
	{ &decl_itu_callstate, NULL, NULL, NULL, }, /* 0x14 */
	{ NULL, NULL, NULL, NULL, }, /* 0x15 */
	{ NULL, NULL, NULL, NULL, }, /* 0x16 */
	{ NULL, NULL, NULL, NULL, }, /* 0x17 */
	{ NULL, NULL, NULL, NULL, }, /* 0x18 */
	{ NULL, NULL, NULL, NULL, }, /* 0x19 */
	{ NULL, NULL, NULL, NULL, }, /* 0x1a */
	{ NULL, NULL, NULL, NULL, }, /* 0x1b */
	{ &decl_itu_facility, NULL, NULL, NULL, }, /* 0x1c */
	{ NULL, NULL, NULL, NULL, }, /* 0x1d */
	{ NULL, NULL, NULL, NULL, }, /* 0x1e */
	{ NULL, NULL, NULL, NULL, }, /* 0x1f */
	{ NULL, NULL, NULL, NULL, }, /* 0x20 */
	{ NULL, NULL, NULL, NULL, }, /* 0x21 */
	{ NULL, NULL, NULL, NULL, }, /* 0x22 */
	{ NULL, NULL, NULL, NULL, }, /* 0x23 */
	{ NULL, NULL, NULL, NULL, }, /* 0x24 */
	{ NULL, NULL, NULL, NULL, }, /* 0x25 */
	{ NULL, NULL, NULL, NULL, }, /* 0x26 */
	{ &decl_itu_notify, NULL, NULL, NULL, }, /* 0x27 */
	{ NULL, NULL, NULL, NULL, }, /* 0x28 */
	{ NULL, NULL, NULL, NULL, }, /* 0x29 */
	{ NULL, NULL, NULL, NULL, }, /* 0x2a */
	{ NULL, NULL, NULL, NULL, }, /* 0x2b */
	{ NULL, NULL, NULL, NULL, }, /* 0x2c */
	{ NULL, NULL, NULL, NULL, }, /* 0x2d */
	{ NULL, NULL, NULL, NULL, }, /* 0x2e */
	{ NULL, NULL, NULL, NULL, }, /* 0x2f */
	{ NULL, NULL, NULL, NULL, }, /* 0x30 */
	{ NULL, NULL, NULL, NULL, }, /* 0x31 */
	{ NULL, NULL, NULL, NULL, }, /* 0x32 */
	{ NULL, NULL, NULL, NULL, }, /* 0x33 */
	{ NULL, NULL, NULL, NULL, }, /* 0x34 */
	{ NULL, NULL, NULL, NULL, }, /* 0x35 */
	{ NULL, NULL, NULL, NULL, }, /* 0x36 */
	{ NULL, NULL, NULL, NULL, }, /* 0x37 */
	{ NULL, NULL, NULL, NULL, }, /* 0x38 */
	{ NULL, NULL, NULL, NULL, }, /* 0x39 */
	{ NULL, NULL, NULL, NULL, }, /* 0x3a */
	{ NULL, NULL, NULL, NULL, }, /* 0x3b */
	{ NULL, NULL, NULL, NULL, }, /* 0x3c */
	{ NULL, NULL, NULL, NULL, }, /* 0x3d */
	{ NULL, NULL, NULL, NULL, }, /* 0x3e */
	{ NULL, NULL, NULL, NULL, }, /* 0x3f */
	{ NULL, NULL, NULL, NULL, }, /* 0x40 */
	{ NULL, NULL, NULL, NULL, }, /* 0x41 */
	{ &decl_itu_eetd, NULL, NULL, &decl_net_eetd, }, /* 0x42 */
	{ NULL, NULL, NULL, NULL, }, /* 0x43 */
	{ NULL, NULL, NULL, NULL, }, /* 0x44 */
	{ NULL, NULL, NULL, NULL, }, /* 0x45 */
	{ NULL, NULL, NULL, NULL, }, /* 0x46 */
	{ NULL, NULL, NULL, NULL, }, /* 0x47 */
	{ NULL, NULL, NULL, NULL, }, /* 0x48 */
	{ NULL, NULL, NULL, NULL, }, /* 0x49 */
	{ NULL, NULL, NULL, NULL, }, /* 0x4a */
	{ NULL, NULL, NULL, NULL, }, /* 0x4b */
	{ &decl_itu_conned, NULL, NULL, NULL, }, /* 0x4c */
	{ &decl_itu_connedsub, NULL, NULL, NULL, }, /* 0x4d */
	{ NULL, NULL, NULL, NULL, }, /* 0x4e */
	{ NULL, NULL, NULL, NULL, }, /* 0x4f */
	{ NULL, NULL, NULL, NULL, }, /* 0x50 */
	{ NULL, NULL, NULL, NULL, }, /* 0x51 */
	{ NULL, NULL, NULL, NULL, }, /* 0x52 */
	{ NULL, NULL, NULL, NULL, }, /* 0x53 */
	{ &decl_itu_epref, NULL, NULL, NULL, }, /* 0x54 */
	{ &decl_itu_epstate, NULL, NULL, NULL, }, /* 0x55 */
	{ NULL, NULL, NULL, NULL, }, /* 0x56 */
	{ NULL, NULL, NULL, NULL, }, /* 0x57 */
	{ &decl_itu_aal, NULL, NULL, NULL, }, /* 0x58 */
	{ &decl_itu_traffic, NULL, NULL, &decl_net_traffic, }, /* 0x59 */
	{ &decl_itu_connid, NULL, NULL, NULL, }, /* 0x5a */
	{ NULL, NULL, NULL, NULL, }, /* 0x5b */
	{ &decl_itu_qos, NULL, NULL, &decl_net_qos, }, /* 0x5c */
	{ &decl_itu_bhli, NULL, NULL, NULL, }, /* 0x5d */
	{ &decl_itu_bearer, NULL, NULL, NULL, }, /* 0x5e */
	{ &decl_itu_blli, NULL, NULL, NULL, }, /* 0x5f */
	{ &decl_itu_lshift, NULL, NULL, NULL, }, /* 0x60 */
	{ &decl_itu_nlshift, NULL, NULL, NULL, }, /* 0x61 */
	{ &decl_itu_scompl, NULL, NULL, NULL, }, /* 0x62 */
	{ &decl_itu_repeat, NULL, NULL, NULL, }, /* 0x63 */
	{ NULL, NULL, NULL, NULL, }, /* 0x64 */
	{ NULL, NULL, NULL, NULL, }, /* 0x65 */
	{ NULL, NULL, NULL, NULL, }, /* 0x66 */
	{ NULL, NULL, NULL, NULL, }, /* 0x67 */
	{ NULL, NULL, NULL, NULL, }, /* 0x68 */
	{ NULL, NULL, NULL, NULL, }, /* 0x69 */
	{ NULL, NULL, NULL, NULL, }, /* 0x6a */
	{ NULL, NULL, NULL, NULL, }, /* 0x6b */
	{ &decl_itu_calling, NULL, NULL, NULL, }, /* 0x6c */
	{ &decl_itu_callingsub, NULL, NULL, NULL, }, /* 0x6d */
	{ NULL, NULL, NULL, NULL, }, /* 0x6e */
	{ NULL, NULL, NULL, NULL, }, /* 0x6f */
	{ &decl_itu_called, NULL, NULL, NULL, }, /* 0x70 */
	{ &decl_itu_calledsub, NULL, NULL, NULL, }, /* 0x71 */
	{ NULL, NULL, NULL, NULL, }, /* 0x72 */
	{ NULL, NULL, NULL, NULL, }, /* 0x73 */
	{ NULL, NULL, NULL, NULL, }, /* 0x74 */
	{ NULL, NULL, NULL, NULL, }, /* 0x75 */
	{ NULL, NULL, NULL, NULL, }, /* 0x76 */
	{ NULL, NULL, NULL, NULL, }, /* 0x77 */
	{ &decl_itu_tns, NULL, NULL, &decl_net_tns, }, /* 0x78 */
	{ &decl_itu_restart, NULL, NULL, NULL, }, /* 0x79 */
	{ NULL, NULL, NULL, NULL, }, /* 0x7a */
	{ NULL, NULL, NULL, NULL, }, /* 0x7b */
	{ NULL, NULL, NULL, NULL, }, /* 0x7c */
	{ NULL, NULL, NULL, NULL, }, /* 0x7d */
	{ &decl_itu_uu, NULL, NULL, NULL, }, /* 0x7e */
	{ NULL, NULL, NULL, &decl_net_git, }, /* 0x7f */
	{ NULL, NULL, NULL, NULL, }, /* 0x80 */
	{ &decl_itu_mintraffic, NULL, NULL, &decl_net_mintraffic, }, /* 0x81 */
	{ &decl_itu_atraffic, NULL, NULL, &decl_net_atraffic, }, /* 0x82 */
	{ NULL, NULL, NULL, NULL, }, /* 0x83 */
	{ NULL, NULL, NULL, &decl_net_abrsetup, }, /* 0x84 */
	{ NULL, NULL, NULL, NULL, }, /* 0x85 */
	{ NULL, NULL, NULL, NULL, }, /* 0x86 */
	{ NULL, NULL, NULL, NULL, }, /* 0x87 */
	{ NULL, NULL, NULL, NULL, }, /* 0x88 */
	{ &decl_itu_report, NULL, NULL, NULL, }, /* 0x89 */
	{ NULL, NULL, NULL, NULL, }, /* 0x8a */
	{ NULL, NULL, NULL, NULL, }, /* 0x8b */
	{ NULL, NULL, NULL, NULL, }, /* 0x8c */
	{ NULL, NULL, NULL, NULL, }, /* 0x8d */
	{ NULL, NULL, NULL, NULL, }, /* 0x8e */
	{ NULL, NULL, NULL, NULL, }, /* 0x8f */
	{ NULL, NULL, NULL, NULL, }, /* 0x90 */
	{ NULL, NULL, NULL, NULL, }, /* 0x91 */
	{ NULL, NULL, NULL, NULL, }, /* 0x92 */
	{ NULL, NULL, NULL, NULL, }, /* 0x93 */
	{ NULL, NULL, NULL, NULL, }, /* 0x94 */
	{ NULL, NULL, NULL, NULL, }, /* 0x95 */
	{ NULL, NULL, NULL, NULL, }, /* 0x96 */
	{ NULL, NULL, NULL, NULL, }, /* 0x97 */
	{ NULL, NULL, NULL, NULL, }, /* 0x98 */
	{ NULL, NULL, NULL, NULL, }, /* 0x99 */
	{ NULL, NULL, NULL, NULL, }, /* 0x9a */
	{ NULL, NULL, NULL, NULL, }, /* 0x9b */
	{ NULL, NULL, NULL, NULL, }, /* 0x9c */
	{ NULL, NULL, NULL, NULL, }, /* 0x9d */
	{ NULL, NULL, NULL, NULL, }, /* 0x9e */
	{ NULL, NULL, NULL, NULL, }, /* 0x9f */
	{ NULL, NULL, NULL, NULL, }, /* 0xa0 */
	{ NULL, NULL, NULL, NULL, }, /* 0xa1 */
	{ NULL, NULL, NULL, NULL, }, /* 0xa2 */
	{ NULL, NULL, NULL, NULL, }, /* 0xa3 */
	{ NULL, NULL, NULL, NULL, }, /* 0xa4 */
	{ NULL, NULL, NULL, NULL, }, /* 0xa5 */
	{ NULL, NULL, NULL, NULL, }, /* 0xa6 */
	{ NULL, NULL, NULL, NULL, }, /* 0xa7 */
	{ NULL, NULL, NULL, NULL, }, /* 0xa8 */
	{ NULL, NULL, NULL, NULL, }, /* 0xa9 */
	{ NULL, NULL, NULL, NULL, }, /* 0xaa */
	{ NULL, NULL, NULL, NULL, }, /* 0xab */
	{ NULL, NULL, NULL, NULL, }, /* 0xac */
	{ NULL, NULL, NULL, NULL, }, /* 0xad */
	{ NULL, NULL, NULL, NULL, }, /* 0xae */
	{ NULL, NULL, NULL, NULL, }, /* 0xaf */
	{ NULL, NULL, NULL, NULL, }, /* 0xb0 */
	{ NULL, NULL, NULL, NULL, }, /* 0xb1 */
	{ NULL, NULL, NULL, NULL, }, /* 0xb2 */
	{ NULL, NULL, NULL, NULL, }, /* 0xb3 */
	{ NULL, NULL, NULL, NULL, }, /* 0xb4 */
	{ NULL, NULL, NULL, NULL, }, /* 0xb5 */
	{ NULL, NULL, NULL, NULL, }, /* 0xb6 */
	{ NULL, NULL, NULL, NULL, }, /* 0xb7 */
	{ NULL, NULL, NULL, NULL, }, /* 0xb8 */
	{ NULL, NULL, NULL, NULL, }, /* 0xb9 */
	{ NULL, NULL, NULL, NULL, }, /* 0xba */
	{ NULL, NULL, NULL, NULL, }, /* 0xbb */
	{ NULL, NULL, NULL, NULL, }, /* 0xbc */
	{ NULL, NULL, NULL, NULL, }, /* 0xbd */
	{ NULL, NULL, NULL, NULL, }, /* 0xbe */
	{ NULL, NULL, NULL, NULL, }, /* 0xbf */
	{ NULL, NULL, NULL, NULL, }, /* 0xc0 */
	{ NULL, NULL, NULL, NULL, }, /* 0xc1 */
	{ NULL, NULL, NULL, NULL, }, /* 0xc2 */
	{ NULL, NULL, NULL, NULL, }, /* 0xc3 */
	{ NULL, NULL, NULL, NULL, }, /* 0xc4 */
	{ NULL, NULL, NULL, NULL, }, /* 0xc5 */
	{ NULL, NULL, NULL, NULL, }, /* 0xc6 */
	{ NULL, NULL, NULL, NULL, }, /* 0xc7 */
	{ NULL, NULL, NULL, NULL, }, /* 0xc8 */
	{ NULL, NULL, NULL, NULL, }, /* 0xc9 */
	{ NULL, NULL, NULL, NULL, }, /* 0xca */
	{ NULL, NULL, NULL, NULL, }, /* 0xcb */
	{ NULL, NULL, NULL, NULL, }, /* 0xcc */
	{ NULL, NULL, NULL, NULL, }, /* 0xcd */
	{ NULL, NULL, NULL, NULL, }, /* 0xce */
	{ NULL, NULL, NULL, NULL, }, /* 0xcf */
	{ NULL, NULL, NULL, NULL, }, /* 0xd0 */
	{ NULL, NULL, NULL, NULL, }, /* 0xd1 */
	{ NULL, NULL, NULL, NULL, }, /* 0xd2 */
	{ NULL, NULL, NULL, NULL, }, /* 0xd3 */
	{ NULL, NULL, NULL, NULL, }, /* 0xd4 */
	{ NULL, NULL, NULL, NULL, }, /* 0xd5 */
	{ NULL, NULL, NULL, NULL, }, /* 0xd6 */
	{ NULL, NULL, NULL, NULL, }, /* 0xd7 */
	{ NULL, NULL, NULL, NULL, }, /* 0xd8 */
	{ NULL, NULL, NULL, NULL, }, /* 0xd9 */
	{ NULL, NULL, NULL, NULL, }, /* 0xda */
	{ NULL, NULL, NULL, NULL, }, /* 0xdb */
	{ NULL, NULL, NULL, NULL, }, /* 0xdc */
	{ NULL, NULL, NULL, NULL, }, /* 0xdd */
	{ NULL, NULL, NULL, NULL, }, /* 0xde */
	{ NULL, NULL, NULL, NULL, }, /* 0xdf */
	{ NULL, NULL, NULL, &decl_net_called_soft, }, /* 0xe0 */
	{ NULL, NULL, NULL, &decl_net_crankback, }, /* 0xe1 */
	{ NULL, NULL, NULL, &decl_net_dtl, }, /* 0xe2 */
	{ NULL, NULL, NULL, &decl_net_calling_soft, }, /* 0xe3 */
	{ NULL, NULL, NULL, &decl_net_abradd, }, /* 0xe4 */
	{ NULL, NULL, NULL, NULL, }, /* 0xe5 */
	{ NULL, NULL, NULL, NULL, }, /* 0xe6 */
	{ NULL, NULL, NULL, NULL, }, /* 0xe7 */
	{ NULL, NULL, NULL, &decl_net_lij_callid, }, /* 0xe8 */
	{ NULL, NULL, NULL, &decl_net_lij_param, }, /* 0xe9 */
	{ NULL, NULL, NULL, &decl_net_lij_seqno, }, /* 0xea */
	{ NULL, NULL, NULL, &decl_net_cscope, }, /* 0xeb */
	{ NULL, NULL, NULL, &decl_net_exqos, }, /* 0xec */
	{ NULL, NULL, NULL, NULL, }, /* 0xed */
	{ NULL, NULL, NULL, NULL, }, /* 0xee */
	{ NULL, NULL, NULL, NULL, }, /* 0xef */
	{ NULL, NULL, NULL, &decl_net_mdcr, }, /* 0xf0 */
	{ NULL, NULL, NULL, NULL, }, /* 0xf1 */
	{ NULL, NULL, NULL, NULL, }, /* 0xf2 */
	{ NULL, NULL, NULL, NULL, }, /* 0xf3 */
	{ NULL, NULL, NULL, NULL, }, /* 0xf4 */
	{ NULL, NULL, NULL, NULL, }, /* 0xf5 */
	{ NULL, NULL, NULL, NULL, }, /* 0xf6 */
	{ NULL, NULL, NULL, NULL, }, /* 0xf7 */
	{ NULL, NULL, NULL, NULL, }, /* 0xf8 */
	{ NULL, NULL, NULL, NULL, }, /* 0xf9 */
	{ NULL, NULL, NULL, NULL, }, /* 0xfa */
	{ NULL, NULL, NULL, NULL, }, /* 0xfb */
	{ NULL, NULL, NULL, NULL, }, /* 0xfc */
	{ NULL, NULL, NULL, NULL, }, /* 0xfd */
	{ &decl_itu_unrec, NULL, NULL, NULL, }, /* 0xfe */
	{ NULL, NULL, NULL, NULL, }, /* 0xff */
};
