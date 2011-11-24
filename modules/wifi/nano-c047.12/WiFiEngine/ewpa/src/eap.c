/*
 * EAP peer state machines (RFC 4137)
 * Copyright (c) 2004-2006, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 *
 * This file implements the Peer State Machine as defined in RFC 4137. The used
 * states and state transitions match mostly with the RFC. However, there are
 * couple of additional transitions for working around small issues noticed
 * during testing. These exceptions are explained in comments within the
 * functions in this file. The method functions, m.func(), are similar to the
 * ones used in RFC 4137, but some small changes have used here to optimize
 * operations and to add functionality needed for fast re-authentication
 * (session resumption).
 */

#include "includes.h"

#include "common.h"
#include "eap_i.h"
#include "config_ssid.h"
#include "tls.h"
#include "crypto.h"
#include "pcsc_funcs.h"
#include "wpa_ctrl.h"
#include "state_machine.h"
#include "registry.h"
#include "registryAccess.h"
#include "build_config.h"

#define STATE_MACHINE_DATA struct eap_sm
#define STATE_MACHINE_DEBUG_PREFIX "EAP"

#define EAP_MAX_AUTH_ROUNDS 50


static Boolean eap_sm_allowMethod(struct eap_sm *sm, int vendor,
				  EapType method);
static u8 * eap_sm_buildNak(struct eap_sm *sm, int id, size_t *len);
static void eap_sm_processIdentity(struct eap_sm *sm, const u8 *req);
static void eap_sm_processNotify(struct eap_sm *sm, const u8 *req);
static u8 * eap_sm_buildNotify(int id, size_t *len);
static void eap_sm_parseEapReq(struct eap_sm *sm, const u8 *req, size_t len);
#if defined(CONFIG_CTRL_IFACE) || !defined(CONFIG_NO_STDOUT_DEBUG)
static const char * eap_sm_method_state_txt(EapMethodState state);
static const char * eap_sm_decision_txt(EapDecision decision);
#endif /* CONFIG_CTRL_IFACE || !CONFIG_NO_STDOUT_DEBUG */



static Boolean eapol_get_bool(struct eap_sm *sm, enum eapol_bool_var var)
{
	return sm->eapol_cb->get_bool(sm->eapol_ctx, var);
}


static void eapol_set_bool(struct eap_sm *sm, enum eapol_bool_var var,
			   Boolean value)
{
	sm->eapol_cb->set_bool(sm->eapol_ctx, var, value);
}


static unsigned int eapol_get_int(struct eap_sm *sm, enum eapol_int_var var)
{
	return sm->eapol_cb->get_int(sm->eapol_ctx, var);
}


static void eapol_set_int(struct eap_sm *sm, enum eapol_int_var var,
			  unsigned int value)
{
	sm->eapol_cb->set_int(sm->eapol_ctx, var, value);
}


static u8 * eapol_get_eapReqData(struct eap_sm *sm, size_t *len)
{
	return sm->eapol_cb->get_eapReqData(sm->eapol_ctx, len);
}


static void eap_deinit_prev_method(struct eap_sm *sm, const char *txt)
{
	if (sm->m == NULL || sm->eap_method_priv == NULL)
		return;

	wpa_printf(MSG_DEBUG, "EAP: deinitialize previously used EAP method "
		   "(%d, %s) at %s", sm->selectedMethod, sm->m->name, txt);
	sm->m->deinit(sm, sm->eap_method_priv);
	sm->eap_method_priv = NULL;
	sm->m = NULL;
}


/*
 * This state initializes state machine variables when the machine is
 * activated (portEnabled = TRUE). This is also used when re-starting
 * authentication (eapRestart == TRUE).
 */
SM_STATE(EAP, INITIALIZE)
{
	SM_ENTRY(EAP, INITIALIZE);
	if (sm->fast_reauth && sm->m && sm->m->has_reauth_data &&
	    sm->m->has_reauth_data(sm, sm->eap_method_priv)) {
		wpa_printf(MSG_DEBUG, "EAP: maintaining EAP method data for "
			   "fast reauthentication");
		sm->m->deinit_for_reauth(sm, sm->eap_method_priv);
	} else {
		eap_deinit_prev_method(sm, "INITIALIZE");
	}
	sm->selectedMethod = EAP_TYPE_NONE;
	sm->methodState = METHOD_NONE;
	sm->allowNotifications = TRUE;
	sm->decision = DECISION_FAIL;
	eapol_set_int(sm, EAPOL_idleWhile, sm->ClientTimeout);
	eapol_set_bool(sm, EAPOL_eapSuccess, FALSE);
	eapol_set_bool(sm, EAPOL_eapFail, FALSE);
	os_free(sm->eapKeyData);
	sm->eapKeyData = NULL;
	sm->eapKeyAvailable = FALSE;
	eapol_set_bool(sm, EAPOL_eapRestart, FALSE);
	sm->lastId = -1; /* new session - make sure this does not match with
			  * the first EAP-Packet */
	/*
	 * RFC 4137 does not reset eapResp and eapNoResp here. However, this
	 * seemed to be able to trigger cases where both were set and if EAPOL
	 * state machine uses eapNoResp first, it may end up not sending a real
	 * reply correctly. This occurred when the workaround in FAIL state set
	 * eapNoResp = TRUE.. Maybe that workaround needs to be fixed to do
	 * something else(?)
	 */
	eapol_set_bool(sm, EAPOL_eapResp, FALSE);
	eapol_set_bool(sm, EAPOL_eapNoResp, FALSE);
	sm->num_rounds = 0;
}


/*
 * This state is reached whenever service from the lower layer is interrupted
 * or unavailable (portEnabled == FALSE). Immediate transition to INITIALIZE
 * occurs when the port becomes enabled.
 */
SM_STATE(EAP, DISABLED)
{
	SM_ENTRY(EAP, DISABLED);
	sm->num_rounds = 0;
}


/*
 * The state machine spends most of its time here, waiting for something to
 * happen. This state is entered unconditionally from INITIALIZE, DISCARD, and
 * SEND_RESPONSE states.
 */
SM_STATE(EAP, IDLE)
{
	SM_ENTRY(EAP, IDLE);
}


/*
 * This state is entered when an EAP packet is received (eapReq == TRUE) to
 * parse the packet header.
 */
SM_STATE(EAP, RECEIVED)
{
	const u8 *eapReqData;
	size_t eapReqDataLen;

	SM_ENTRY(EAP, RECEIVED);
	eapReqData = eapol_get_eapReqData(sm, &eapReqDataLen);
	/* parse rxReq, rxSuccess, rxFailure, reqId, reqMethod */
	eap_sm_parseEapReq(sm, eapReqData, eapReqDataLen);
	sm->num_rounds++;
}


/*
 * This state is entered when a request for a new type comes in. Either the
 * correct method is started, or a Nak response is built.
 */
SM_STATE(EAP, GET_METHOD)
{
	int reinit;
	EapType method;

	SM_ENTRY(EAP, GET_METHOD);

	if (sm->reqMethod == EAP_TYPE_EXPANDED)
		method = (EapType)sm->reqVendorMethod;
	else
		method = (EapType)sm->reqMethod;

	if (!eap_sm_allowMethod(sm, sm->reqVendor, method)) {
		wpa_printf(MSG_DEBUG, "EAP: vendor %u method %u not allowed",
			   sm->reqVendor, method);
		goto nak;
	}

	/*
	 * RFC 4137 does not define specific operation for fast
	 * re-authentication (session resumption). The design here is to allow
	 * the previously used method data to be maintained for
	 * re-authentication if the method support session resumption.
	 * Otherwise, the previously used method data is freed and a new method
	 * is allocated here.
	 */
	if (sm->fast_reauth &&
	    sm->m && sm->m->vendor == sm->reqVendor &&
	    sm->m->method == method &&
	    sm->m->has_reauth_data &&
	    sm->m->has_reauth_data(sm, sm->eap_method_priv)) {
		wpa_printf(MSG_DEBUG, "EAP: Using previous method data"
			   " for fast re-authentication");
		reinit = 1;
	} else {
		eap_deinit_prev_method(sm, "GET_METHOD");
		reinit = 0;
	}

	sm->selectedMethod = sm->reqMethod;
	if (sm->m == NULL)
		sm->m = eap_sm_get_eap_methods(sm->reqVendor, method);
	if (!sm->m) {
		wpa_printf(MSG_DEBUG, "EAP: Could not find selected method: "
			   "vendor %d method %d",
			   sm->reqVendor, method);
		goto nak;
	}

	wpa_printf(MSG_DEBUG, "EAP: Initialize selected EAP method: "
		   "vendor %u method %u (%s)",
		   sm->reqVendor, method, sm->m->name);
	if (reinit)
		sm->eap_method_priv = sm->m->init_for_reauth(
			sm, sm->eap_method_priv);
	else
		sm->eap_method_priv = sm->m->init(sm);

	if (sm->eap_method_priv == NULL) {
		struct wpa_ssid *config = eap_get_config(sm);
		wpa_msg(sm->msg_ctx, MSG_INFO,
			"EAP: Failed to initialize EAP method: vendor %u "
			"method %u (%s)",
			sm->reqVendor, method, sm->m->name);
		sm->m = NULL;
		sm->methodState = METHOD_NONE;
		sm->selectedMethod = EAP_TYPE_NONE;
		if (sm->reqMethod == EAP_TYPE_TLS && config &&
		    (config->pending_req_pin ||
		     config->pending_req_passphrase)) {
			/*
			 * Return without generating Nak in order to allow
			 * entering of PIN code or passphrase to retry the
			 * current EAP packet.
			 */
			wpa_printf(MSG_DEBUG, "EAP: Pending PIN/passphrase "
				   "request - skip Nak");
			return;
		}

		goto nak;
	}

	sm->methodState = METHOD_INIT;
	wpa_msg(sm->msg_ctx, MSG_INFO, WPA_EVENT_EAP_METHOD
		"EAP vendor %u method %u (%s) selected",
		sm->reqVendor, method, sm->m->name);
	return;

nak:
	os_free(sm->eapRespData);
	sm->eapRespData = NULL;
	sm->eapRespData = eap_sm_buildNak(sm, sm->reqId, &sm->eapRespDataLen);
}


/*
 * The method processing happens here. The request from the authenticator is
 * processed, and an appropriate response packet is built.
 */
SM_STATE(EAP, METHOD)
{
	u8 *eapReqData;
	size_t eapReqDataLen;
	struct eap_method_ret ret;

	SM_ENTRY(EAP, METHOD);
	if (sm->m == NULL) {
		wpa_printf(MSG_WARNING, "EAP::METHOD - method not selected");
		return;
	}

	eapReqData = eapol_get_eapReqData(sm, &eapReqDataLen);

	/*
	 * Get ignore, methodState, decision, allowNotifications, and
	 * eapRespData. RFC 4137 uses three separate method procedure (check,
	 * process, and buildResp) in this state. These have been combined into
	 * a single function call to m->process() in order to optimize EAP
	 * method implementation interface a bit. These procedures are only
	 * used from within this METHOD state, so there is no need to keep
	 * these as separate C functions.
	 *
	 * The RFC 4137 procedures return values as follows:
	 * ignore = m.check(eapReqData)
	 * (methodState, decision, allowNotifications) = m.process(eapReqData)
	 * eapRespData = m.buildResp(reqId)
	 */
	os_memset(&ret, 0, sizeof(ret));
	ret.ignore = sm->ignore;
	ret.methodState = sm->methodState;
	ret.decision = sm->decision;
	ret.allowNotifications = sm->allowNotifications;
	os_free(sm->eapRespData);
	sm->eapRespData = NULL;
	sm->again = FALSE;
	sm->eapRespData = sm->m->process(sm, sm->eap_method_priv, &ret,
					 eapReqData, eapReqDataLen,
					 &sm->eapRespDataLen);
	wpa_printf(MSG_DEBUG, "EAP: method process -> ignore=%s "
		   "methodState=%s decision=%s",
		   ret.ignore ? "TRUE" : "FALSE",
		   eap_sm_method_state_txt(ret.methodState),
		   eap_sm_decision_txt(ret.decision));

	sm->ignore = ret.ignore;
	if (sm->ignore || sm->again)
		return;
	sm->methodState = ret.methodState;
	sm->decision = ret.decision;
	sm->allowNotifications = ret.allowNotifications;

	if (sm->m->isKeyAvailable && sm->m->getKey &&
	    sm->m->isKeyAvailable(sm, sm->eap_method_priv)) {
		os_free(sm->eapKeyData);
		sm->eapKeyData = sm->m->getKey(sm, sm->eap_method_priv,
					       &sm->eapKeyDataLen);
	}
}

#ifdef EAP_SIM
/*
 * Special wait-state for EAP-SIM. Since we don't get the response
 * immediately, we have to return and come back here again for a new
 * pass.
 */
int scard_gsm_auth_complete(void);
SM_STATE(EAP, METHOD_SIMWAIT)
{
	SM_ENTRY(EAP, METHOD_SIMWAIT);
	if (sm->m == NULL) {
		wpa_printf(MSG_WARNING, "EAP::METHOD_SIMWAIT - method not selected");
		return;
	}
	/* by setting the changed flag, we ensure another eapol loop, 
	   else we let a timer find the way back here */
	sm->changed = (Boolean) scard_gsm_auth_complete();
}
#endif /* EAP_SIM */

/*
 * This state signals the lower layer that a response packet is ready to be
 * sent.
 */
SM_STATE(EAP, SEND_RESPONSE)
{
	SM_ENTRY(EAP, SEND_RESPONSE);
	os_free(sm->lastRespData);
	if (sm->eapRespData) {
		if (sm->workaround)
			os_memcpy(sm->last_md5, sm->req_md5, 16);
		sm->lastId = sm->reqId;
		sm->lastRespData = os_malloc(sm->eapRespDataLen);
		if (sm->lastRespData) {
			os_memcpy(sm->lastRespData, sm->eapRespData,
				  sm->eapRespDataLen);
			sm->lastRespDataLen = sm->eapRespDataLen;
		}
		eapol_set_bool(sm, EAPOL_eapResp, TRUE);
	} else
		sm->lastRespData = NULL;
	eapol_set_bool(sm, EAPOL_eapReq, FALSE);
	eapol_set_int(sm, EAPOL_idleWhile, sm->ClientTimeout);
}


/*
 * This state signals the lower layer that the request was discarded, and no
 * response packet will be sent at this time.
 */
SM_STATE(EAP, DISCARD)
{
	SM_ENTRY(EAP, DISCARD);
	eapol_set_bool(sm, EAPOL_eapReq, FALSE);
	eapol_set_bool(sm, EAPOL_eapNoResp, TRUE);
}


/*
 * Handles requests for Identity method and builds a response.
 */
SM_STATE(EAP, IDENTITY)
{
	const u8 *eapReqData;
	size_t eapReqDataLen;

	SM_ENTRY(EAP, IDENTITY);
	eapReqData = eapol_get_eapReqData(sm, &eapReqDataLen);
	eap_sm_processIdentity(sm, eapReqData);
	os_free(sm->eapRespData);
	sm->eapRespData = NULL;
	sm->eapRespData = eap_sm_buildIdentity(sm, sm->reqId,
					       &sm->eapRespDataLen, 0);
}


/*
 * Handles requests for Notification method and builds a response.
 */
SM_STATE(EAP, NOTIFICATION)
{
	const u8 *eapReqData;
	size_t eapReqDataLen;

	SM_ENTRY(EAP, NOTIFICATION);
	eapReqData = eapol_get_eapReqData(sm, &eapReqDataLen);
	eap_sm_processNotify(sm, eapReqData);
	os_free(sm->eapRespData);
	sm->eapRespData = NULL;
	sm->eapRespData = eap_sm_buildNotify(sm->reqId, &sm->eapRespDataLen);
}


/*
 * This state retransmits the previous response packet.
 */
SM_STATE(EAP, RETRANSMIT)
{
	SM_ENTRY(EAP, RETRANSMIT);
	os_free(sm->eapRespData);
	if (sm->lastRespData) {
		sm->eapRespData = os_malloc(sm->lastRespDataLen);
		if (sm->eapRespData) {
			os_memcpy(sm->eapRespData, sm->lastRespData,
				  sm->lastRespDataLen);
			sm->eapRespDataLen = sm->lastRespDataLen;
		}
	} else
		sm->eapRespData = NULL;
}


/*
 * This state is entered in case of a successful completion of authentication
 * and state machine waits here until port is disabled or EAP authentication is
 * restarted.
 */
SM_STATE(EAP, SUCCESS)
{
	SM_ENTRY(EAP, SUCCESS);
	if (sm->eapKeyData != NULL)
		sm->eapKeyAvailable = TRUE;
	eapol_set_bool(sm, EAPOL_eapSuccess, TRUE);

	/*
	 * RFC 4137 does not clear eapReq here, but this seems to be required
	 * to avoid processing the same request twice when state machine is
	 * initialized.
	 */
	eapol_set_bool(sm, EAPOL_eapReq, FALSE);

	/*
	 * RFC 4137 does not set eapNoResp here, but this seems to be required
	 * to get EAPOL Supplicant backend state machine into SUCCESS state. In
	 * addition, either eapResp or eapNoResp is required to be set after
	 * processing the received EAP frame.
	 */
	eapol_set_bool(sm, EAPOL_eapNoResp, TRUE);

	wpa_msg(sm->msg_ctx, MSG_INFO, WPA_EVENT_EAP_SUCCESS
		"EAP authentication completed successfully");
}


/*
 * This state is entered in case of a failure and state machine waits here
 * until port is disabled or EAP authentication is restarted.
 */
SM_STATE(EAP, FAILURE)
{
	SM_ENTRY(EAP, FAILURE);
	eapol_set_bool(sm, EAPOL_eapFail, TRUE);

	/*
	 * RFC 4137 does not clear eapReq here, but this seems to be required
	 * to avoid processing the same request twice when state machine is
	 * initialized.
	 */
	eapol_set_bool(sm, EAPOL_eapReq, FALSE);

	/*
	 * RFC 4137 does not set eapNoResp here. However, either eapResp or
	 * eapNoResp is required to be set after processing the received EAP
	 * frame.
	 */
	eapol_set_bool(sm, EAPOL_eapNoResp, TRUE);

	wpa_msg(sm->msg_ctx, MSG_INFO, WPA_EVENT_EAP_FAILURE
		"EAP authentication failed");
}


static int eap_success_workaround(struct eap_sm *sm, int reqId, int lastId)
{
	/*
	 * At least Microsoft IAS and Meetinghouse Aegis seem to be sending
	 * EAP-Success/Failure with lastId + 1 even though RFC 3748 and
	 * RFC 4137 require that reqId == lastId. In addition, it looks like
	 * Ringmaster v2.1.2.0 would be using lastId + 2 in EAP-Success.
	 *
	 * Accept this kind of Id if EAP workarounds are enabled. These are
	 * unauthenticated plaintext messages, so this should have minimal
	 * security implications (bit easier to fake EAP-Success/Failure).
	 */
	if (sm->workaround && (reqId == ((lastId + 1) & 0xff) ||
			       reqId == ((lastId + 2) & 0xff))) {
		wpa_printf(MSG_DEBUG, "EAP: Workaround for unexpected "
			   "identifier field in EAP Success: "
			   "reqId=%d lastId=%d (these are supposed to be "
			   "same)", reqId, lastId);
		return 1;
	}
	wpa_printf(MSG_DEBUG, "EAP: EAP-Success Id mismatch - reqId=%d "
		   "lastId=%d", reqId, lastId);
	return 0;
}



/*
 * RFC 4137 - Appendix A.1: EAP Peer State Machine - State transitions
 */
SM_STEP(EAP)
{
	int duplicate;

	if (eapol_get_bool(sm, EAPOL_eapRestart) &&
	    eapol_get_bool(sm, EAPOL_portEnabled))
		SM_ENTER_GLOBAL(EAP, INITIALIZE);
	else if (!eapol_get_bool(sm, EAPOL_portEnabled) || sm->force_disabled)
		SM_ENTER_GLOBAL(EAP, DISABLED);
	else if (sm->num_rounds > EAP_MAX_AUTH_ROUNDS) {
		/* RFC 4137 does not place any limit on number of EAP messages
		 * in an authentication session. However, some error cases have
		 * ended up in a state were EAP messages were sent between the
		 * peer and server in a loop (e.g., TLS ACK frame in both
		 * direction). Since this is quite undesired outcome, limit the
		 * total number of EAP round-trips and abort authentication if
		 * this limit is exceeded.
		 */
		if (sm->num_rounds == EAP_MAX_AUTH_ROUNDS + 1) {
			wpa_msg(sm->msg_ctx, MSG_INFO, "EAP: more than %d "
				"authentication rounds - abort",
				EAP_MAX_AUTH_ROUNDS);
			sm->num_rounds++;
			SM_ENTER_GLOBAL(EAP, FAILURE);
		}
	} else switch (sm->EAP_state) {
	case EAP_INITIALIZE:
		SM_ENTER(EAP, IDLE);
		break;
	case EAP_DISABLED:
		if (eapol_get_bool(sm, EAPOL_portEnabled) &&
		    !sm->force_disabled)
			SM_ENTER(EAP, INITIALIZE);
		break;
	case EAP_IDLE:
		/*
		 * The first three transitions are from RFC 4137. The last two
		 * are local additions to handle special cases with LEAP and
		 * PEAP server not sending EAP-Success in some cases.
		 */
		if (eapol_get_bool(sm, EAPOL_eapReq))
			SM_ENTER(EAP, RECEIVED);
		else if ((eapol_get_bool(sm, EAPOL_altAccept) &&
			  sm->decision != DECISION_FAIL) ||
			 (eapol_get_int(sm, EAPOL_idleWhile) == 0 &&
			  sm->decision == DECISION_UNCOND_SUCC)){
			/*Fix:WPS may fall in that case in the middle of
			 authentication.*/
			/*SM_ENTER(EAP, SUCCESS);*/
		}
		else if (eapol_get_bool(sm, EAPOL_altReject) ||
			 (eapol_get_int(sm, EAPOL_idleWhile) == 0 &&
			  sm->decision != DECISION_UNCOND_SUCC) ||
			 (eapol_get_bool(sm, EAPOL_altAccept) &&
			  sm->methodState != METHOD_CONT &&
			  sm->decision == DECISION_FAIL)) {
			/*Fix: WPS workaround required for EAP-failure in the
			  middle of authentication*/
			/*SM_ENTER(EAP, FAILURE);*/
		}
		else if (sm->selectedMethod == EAP_TYPE_LEAP &&
			 sm->leap_done && sm->decision != DECISION_FAIL &&
			 sm->methodState == METHOD_DONE)
			SM_ENTER(EAP, SUCCESS);
		else if (sm->selectedMethod == EAP_TYPE_PEAP &&
			 sm->peap_done && sm->decision != DECISION_FAIL &&
			 sm->methodState == METHOD_DONE)
			SM_ENTER(EAP, SUCCESS);
		break;
	case EAP_RECEIVED:
		duplicate = (sm->reqId == sm->lastId) && sm->rxReq;
		if (sm->workaround && duplicate &&
		    os_memcmp(sm->req_md5, sm->last_md5, 16) != 0) {
			/*
			 * RFC 4137 uses (reqId == lastId) as the only
			 * verification for duplicate EAP requests. However,
			 * this misses cases where the AS is incorrectly using
			 * the same id again; and unfortunately, such
			 * implementations exist. Use MD5 hash as an extra
			 * verification for the packets being duplicate to
			 * workaround these issues.
			 */
			wpa_printf(MSG_DEBUG, "EAP: AS used the same Id again,"
				   " but EAP packets were not identical");
			wpa_printf(MSG_DEBUG, "EAP: workaround - assume this "
				   "is not a duplicate packet");
			duplicate = 0;
		}

		/*
		 * Two special cases below for LEAP are local additions to work
		 * around odd LEAP behavior (EAP-Success in the middle of
		 * authentication and then swapped roles). Other transitions
		 * are based on RFC 4137.
		 */
		if (sm->rxSuccess && sm->decision != DECISION_FAIL &&
		    (sm->reqId == sm->lastId ||
		     eap_success_workaround(sm, sm->reqId, sm->lastId)))
			SM_ENTER(EAP, SUCCESS);
#ifdef USE_WPS
		/* FIXME: Cleanup the infinite checks here */
		else if ((sm->selectedMethod == EAP_TYPE_WSC) &&
			 (sm->methodState != METHOD_CONT &&
			  ((sm->rxFailure &&
			    sm->decision != DECISION_UNCOND_SUCC) ||
			   (sm->rxSuccess && sm->decision == DECISION_FAIL &&
			    (sm->selectedMethod == EAP_TYPE_WSC ||
			     sm->methodState != METHOD_MAY_CONT))) &&
			  (sm->reqId == sm->lastId ||
			   eap_success_workaround(sm, sm->reqId, sm->lastId))))
		{
			sm->rxFailure = FALSE;
			sm->rxSuccess = TRUE;
			SM_ENTER(EAP, SUCCESS);
			/* if we don't get deauthenticated then we must deauthenticate our selfs */ 
			WiFiEngine_Deauthenticate();
		}
#endif
		else if (sm->methodState != METHOD_CONT &&
			 ((sm->rxFailure &&
			   sm->decision != DECISION_UNCOND_SUCC) ||
			  (sm->rxSuccess && sm->decision == DECISION_FAIL &&
			   (sm->selectedMethod != EAP_TYPE_LEAP ||
			    sm->methodState != METHOD_MAY_CONT))) &&
			 (sm->reqId == sm->lastId ||
			  eap_success_workaround(sm, sm->reqId, sm->lastId)))
			SM_ENTER(EAP, FAILURE);
		else if (sm->rxReq && duplicate)
			SM_ENTER(EAP, RETRANSMIT);
		else if (sm->rxReq && !duplicate &&
			 sm->reqMethod == EAP_TYPE_NOTIFICATION &&
			 sm->allowNotifications)
			SM_ENTER(EAP, NOTIFICATION);
		else if (sm->rxReq && !duplicate &&
			 sm->selectedMethod == EAP_TYPE_NONE &&
			 sm->reqMethod == EAP_TYPE_IDENTITY)
			SM_ENTER(EAP, IDENTITY);
		else if (sm->rxReq && !duplicate &&
			 sm->selectedMethod == EAP_TYPE_NONE &&
			 sm->reqMethod != EAP_TYPE_IDENTITY &&
			 sm->reqMethod != EAP_TYPE_NOTIFICATION)
			SM_ENTER(EAP, GET_METHOD);
		else if (sm->rxReq && !duplicate &&
			 sm->reqMethod == sm->selectedMethod &&
			 sm->methodState != METHOD_DONE)
			SM_ENTER(EAP, METHOD);
		else if (sm->selectedMethod == EAP_TYPE_LEAP &&
			 (sm->rxSuccess || sm->rxResp))
			SM_ENTER(EAP, METHOD);
		else
			SM_ENTER(EAP, DISCARD);
		break;
	case EAP_GET_METHOD:
		if (sm->selectedMethod == sm->reqMethod)
			SM_ENTER(EAP, METHOD);
		else
			SM_ENTER(EAP, SEND_RESPONSE);
		break;
#ifdef EAP_SIM
	case EAP_METHOD_SIMWAIT:
		SM_ENTER(EAP, METHOD);
		break;
#endif /* EAP_SIM */
	case EAP_METHOD:
		if (sm->ignore)
			SM_ENTER(EAP, DISCARD);
#ifdef EAP_SIM
		else if (sm->again)
			SM_ENTER(EAP, METHOD_SIMWAIT);
#endif /* EAP_SIM */
		else
			SM_ENTER(EAP, SEND_RESPONSE);
		break;
	case EAP_SEND_RESPONSE:
		SM_ENTER(EAP, IDLE);
		break;
	case EAP_DISCARD:
		SM_ENTER(EAP, IDLE);
		break;
	case EAP_IDENTITY:
		SM_ENTER(EAP, SEND_RESPONSE);
		break;
	case EAP_NOTIFICATION:
		SM_ENTER(EAP, SEND_RESPONSE);
		break;
	case EAP_RETRANSMIT:
		SM_ENTER(EAP, SEND_RESPONSE);
		break;
	case EAP_SUCCESS:
		break;
	case EAP_FAILURE:
		break;
	}
}


static Boolean eap_sm_allowMethod(struct eap_sm *sm, int vendor,
				  EapType method)
{
	struct wpa_ssid *config = eap_get_config(sm);

	if (!wpa_config_allowed_eap_method(config, vendor, method)) {
		wpa_printf(MSG_DEBUG, "EAP: configuration does not allow: "
			   "vendor %u method %u", vendor, method);
		return FALSE;
	}
	if (eap_sm_get_eap_methods(vendor, method))
		return TRUE;
	wpa_printf(MSG_DEBUG, "EAP: not included in build: "
		   "vendor %u method %u", vendor, method);
	return FALSE;
}


static u8 * eap_sm_build_expanded_nak(struct eap_sm *sm, int id, size_t *len,
				      const struct eap_method *methods,
				      size_t count)
{
	struct wpa_ssid *config = eap_get_config(sm);
	struct eap_hdr *resp;
	u8 *pos;
	int found = 0;
	const struct eap_method *m;

	wpa_printf(MSG_DEBUG, "EAP: Building expanded EAP-Nak");

	/* RFC 3748 - 5.3.2: Expanded Nak */
	*len = sizeof(struct eap_hdr) + 8;
	resp = os_malloc(*len + 8 * (count + 1));
	if (resp == NULL)
		return NULL;

	resp->code = EAP_CODE_RESPONSE;
	resp->identifier = id;
	pos = (u8 *) (resp + 1);
	*pos++ = EAP_TYPE_EXPANDED;
	WPA_PUT_BE24(pos, EAP_VENDOR_IETF);
	pos += 3;
	WPA_PUT_BE32(pos, EAP_TYPE_NAK);
	pos += 4;

	for (m = methods; m; m = m->next) {
		if (sm->reqVendor == m->vendor &&
		    sm->reqVendorMethod == m->method)
			continue; /* do not allow the current method again */
		if (wpa_config_allowed_eap_method(config, m->vendor,
						  m->method)) {
			wpa_printf(MSG_DEBUG, "EAP: allowed type: "
				   "vendor=%u method=%u",
				   m->vendor, m->method);
			*pos++ = EAP_TYPE_EXPANDED;
			WPA_PUT_BE24(pos, m->vendor);
			pos += 3;
			WPA_PUT_BE32(pos, m->method);
			pos += 4;

			(*len) += 8;
			found++;
		}
	}
	if (!found) {
		wpa_printf(MSG_DEBUG, "EAP: no more allowed methods");
		*pos++ = EAP_TYPE_EXPANDED;
		WPA_PUT_BE24(pos, EAP_VENDOR_IETF);
		pos += 3;
		WPA_PUT_BE32(pos, EAP_TYPE_NONE);
		pos += 4;

		(*len) += 8;
	}

	resp->length = host_to_be16(*len);

	return (u8 *) resp;
}


static u8 * eap_sm_buildNak(struct eap_sm *sm, int id, size_t *len)
{
	struct wpa_ssid *config = eap_get_config(sm);
	struct eap_hdr *resp;
	u8 *pos;
	int found = 0, expanded_found = 0;
	size_t count;
	const struct eap_method *methods, *m;

	wpa_printf(MSG_DEBUG, "EAP: Building EAP-Nak (requested type %u "
		   "vendor=%u method=%u not allowed)", sm->reqMethod,
		   sm->reqVendor, sm->reqVendorMethod);
	methods = eap_peer_get_methods(&count);
	if (methods == NULL)
		return NULL;
	if (sm->reqMethod == EAP_TYPE_EXPANDED)
		return eap_sm_build_expanded_nak(sm, id, len, methods, count);

	/* RFC 3748 - 5.3.1: Legacy Nak */
	*len = sizeof(struct eap_hdr) + 1;
	resp = os_malloc(*len + count + 1);
	if (resp == NULL)
		return NULL;

	resp->code = EAP_CODE_RESPONSE;
	resp->identifier = id;
	pos = (u8 *) (resp + 1);
	*pos++ = EAP_TYPE_NAK;

	for (m = methods; m; m = m->next) {
		if (m->vendor == EAP_VENDOR_IETF && m->method == sm->reqMethod)
			continue; /* do not allow the current method again */
		if (wpa_config_allowed_eap_method(config, m->vendor,
						  m->method)) {
			if (m->vendor != EAP_VENDOR_IETF) {
				if (expanded_found)
					continue;
				expanded_found = 1;
				*pos++ = EAP_TYPE_EXPANDED;
			} else
				*pos++ = m->method;
			(*len)++;
			found++;
		}
	}
	if (!found) {
		*pos = EAP_TYPE_NONE;
		(*len)++;
	}
	wpa_hexdump(MSG_DEBUG, "EAP: allowed methods",
		    ((u8 *) (resp + 1)) + 1, found);

	resp->length = host_to_be16(*len);

	return (u8 *) resp;
}


static void eap_sm_processIdentity(struct eap_sm *sm, const u8 *req)
{
	const struct eap_hdr *hdr = (const struct eap_hdr *) req;
	const u8 *pos = (const u8 *) (hdr + 1);
	pos++;

	wpa_msg(sm->msg_ctx, MSG_INFO, WPA_EVENT_EAP_STARTED
		"EAP authentication started");

	/*
	 * RFC 3748 - 5.1: Identity
	 * Data field may contain a displayable message in UTF-8. If this
	 * includes NUL-character, only the data before that should be
	 * displayed. Some EAP implementasitons may piggy-back additional
	 * options after the NUL.
	 */
	/* TODO: could save displayable message so that it can be shown to the
	 * user in case of interaction is required */
	wpa_hexdump_ascii(MSG_DEBUG, "EAP: EAP-Request Identity data",
			  pos, be_to_host16(hdr->length) - 5);
}


#ifdef PCSC_FUNCS
int nr_rtke_generate_eap_sim_nai(const char *imsi, size_t imsi_len,
				 char *nai, size_t *nai_len);
static int
get_nai(struct wpa_ssid *ssid, const char *imsi, size_t imsi_len)
{
	char nai[100];
	size_t nai_len;

	nai_len = sizeof(nai);
	if(nr_rtke_generate_eap_sim_nai(imsi, imsi_len, nai, &nai_len)) {
		wpa_printf(MSG_WARNING, "Failed to get NAI from IMSI");
		return -1;
	}

	os_free(ssid->nai);
	ssid->nai = os_malloc(nai_len);
	if(ssid->nai == NULL) {
		wpa_printf(MSG_WARNING, "Failed to get memory for NAI");
		return -1;
	}
	os_memcpy(ssid->nai, nai, nai_len);
	ssid->nai_len = nai_len;
	return 0;
}

static int eap_sm_imsi_identity(struct eap_sm *sm, struct wpa_ssid *ssid)
{
	int aka = 0;
	char imsi[100];
	size_t imsi_len;
	struct eap_method_type *m = ssid->eap_methods;
	int i;

	imsi_len = sizeof(imsi);
	if (scard_get_imsi(sm->scard_ctx, imsi, &imsi_len)) {
		wpa_printf(MSG_WARNING, "Failed to get IMSI from SIM");
		return -1;
	}

	wpa_hexdump_ascii(MSG_DEBUG, "IMSI", (u8 *) imsi, imsi_len);

	for (i = 0; m && (m[i].vendor != EAP_VENDOR_IETF ||
			  m[i].method != EAP_TYPE_NONE); i++) {
		if (m[i].vendor == EAP_VENDOR_IETF &&
		    m[i].method == EAP_TYPE_AKA) {
			aka = 1;
			break;
		}
	}

	if(get_nai(ssid, imsi, imsi_len)) {
		return -1;
	}

	os_free(ssid->identity);
	ssid->identity = os_malloc(1 + imsi_len + ssid->nai_len);
	if (ssid->identity == NULL) {
		wpa_printf(MSG_WARNING, "Failed to allocate buffer for "
			   "IMSI-based identity");
		return -1;
	}

	ssid->identity[0] = aka ? '0' : '1';
	os_memcpy(ssid->identity + 1, imsi, imsi_len);
	os_memcpy(ssid->identity + 1 + imsi_len, ssid->nai, ssid->nai_len);
	ssid->identity_len = 1 + imsi_len + ssid->nai_len;

	return 0;
}
#endif /* PCSC_FUNCS */


static int eap_sm_get_scard_identity(struct eap_sm *sm, struct wpa_ssid *ssid)
{
#ifdef PCSC_FUNCS
	if (scard_set_pin(sm->scard_ctx, ssid->pin)) {
		/*
		 * Make sure the same PIN is not tried again in order to avoid
		 * blocking SIM.
		 */
		os_free(ssid->pin);
		ssid->pin = NULL;

		wpa_printf(MSG_WARNING, "PIN validation failed");
		eap_sm_request_pin(sm);
		return -1;
	}

	return eap_sm_imsi_identity(sm, ssid);
#else /* PCSC_FUNCS */
	return -1;
#endif /* PCSC_FUNCS */
}


/**
 * eap_sm_buildIdentity - Build EAP-Identity/Response for the current network
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 * @id: EAP identifier for the packet
 * @len: Pointer to a variable that will be set to the length of the response
 * @encrypted: Whether the packet is for encrypted tunnel (EAP phase 2)
 * Returns: Pointer to the allocated EAP-Identity/Response packet or %NULL on
 * failure
 *
 * This function allocates and builds an EAP-Identity/Response packet for the
 * current network. The caller is responsible for freeing the returned data.
 */
u8 * eap_sm_buildIdentity(struct eap_sm *sm, int id, size_t *len,
			  int encrypted)
{
	struct wpa_ssid *config = eap_get_config(sm);
	struct eap_hdr *resp;
	u8 *pos;
	const u8 *identity;
	size_t identity_len;

	if (config == NULL) {
		wpa_printf(MSG_WARNING, "EAP: buildIdentity: configuration "
			   "was not available");
		return NULL;
	}

	if (sm->m && sm->m->get_identity &&
	    (identity = sm->m->get_identity(sm, sm->eap_method_priv,
					    &identity_len)) != NULL) {
		wpa_hexdump_ascii(MSG_DEBUG, "EAP: using method re-auth "
				  "identity", identity, identity_len);
	} else if (!encrypted && config->anonymous_identity) {
		identity = config->anonymous_identity;
		identity_len = config->anonymous_identity_len;
		wpa_hexdump_ascii(MSG_DEBUG, "EAP: using anonymous identity",
				  identity, identity_len);
	} else {
		identity = config->identity;
		identity_len = config->identity_len;
		wpa_hexdump_ascii(MSG_DEBUG, "EAP: using real identity",
				  identity, identity_len);
	}

	if (identity == NULL) {
		wpa_printf(MSG_WARNING, "EAP: buildIdentity: identity "
			   "configuration was not available");
		if (config->pcsc) {
			if (eap_sm_get_scard_identity(sm, config) < 0)
				return NULL;
			identity = config->identity;
			identity_len = config->identity_len;
			wpa_hexdump_ascii(MSG_DEBUG, "permanent identity from "
					  "IMSI", identity, identity_len);
		} else {
			eap_sm_request_identity(sm);
			return NULL;
		}
	}

	*len = sizeof(struct eap_hdr) + 1 + identity_len;
	resp = os_malloc(*len);
	if (resp == NULL)
		return NULL;

	resp->code = EAP_CODE_RESPONSE;
	resp->identifier = id;
	resp->length = host_to_be16(*len);
	pos = (u8 *) (resp + 1);
	*pos++ = EAP_TYPE_IDENTITY;
	os_memcpy(pos, identity, identity_len);

	return (u8 *) resp;
}


static void eap_sm_processNotify(struct eap_sm *sm, const u8 *req)
{
	const struct eap_hdr *hdr = (const struct eap_hdr *) req;
	const u8 *pos;
	char *msg;
	size_t i, msg_len;

	pos = (const u8 *) (hdr + 1);
	pos++;

	msg_len = be_to_host16(hdr->length);
	if (msg_len < 5)
		return;
	msg_len -= 5;
	wpa_hexdump_ascii(MSG_DEBUG, "EAP: EAP-Request Notification data",
			  pos, msg_len);

	msg = os_malloc(msg_len + 1);
	if (msg == NULL)
		return;
	for (i = 0; i < msg_len; i++)
		msg[i] = isprint(pos[i]) ? (char) pos[i] : '_';
	msg[msg_len] = '\0';
	wpa_msg(sm->msg_ctx, MSG_INFO, "%s%s",
		WPA_EVENT_EAP_NOTIFICATION, msg);
	os_free(msg);
}


static u8 * eap_sm_buildNotify(int id, size_t *len)
{
	struct eap_hdr *resp;
	u8 *pos;

	wpa_printf(MSG_DEBUG, "EAP: Generating EAP-Response Notification");
	*len = sizeof(struct eap_hdr) + 1;
	resp = os_malloc(*len);
	if (resp == NULL)
		return NULL;

	resp->code = EAP_CODE_RESPONSE;
	resp->identifier = id;
	resp->length = host_to_be16(*len);
	pos = (u8 *) (resp + 1);
	*pos = EAP_TYPE_NOTIFICATION;

	return (u8 *) resp;
}


static void eap_sm_parseEapReq(struct eap_sm *sm, const u8 *req, size_t len)
{
	const struct eap_hdr *hdr;
	size_t plen;
	const u8 *pos;

	sm->rxReq = sm->rxResp = sm->rxSuccess = sm->rxFailure = FALSE;
	sm->reqId = 0;
	sm->reqMethod = EAP_TYPE_NONE;
	sm->reqVendor = EAP_VENDOR_IETF;
	sm->reqVendorMethod = EAP_TYPE_NONE;

	if (req == NULL || len < sizeof(*hdr))
		return;

	hdr = (const struct eap_hdr *) req;
	plen = be_to_host16(hdr->length);
	if (plen > len) {
		wpa_printf(MSG_DEBUG, "EAP: Ignored truncated EAP-Packet "
			   "(len=%lu plen=%lu)",
			   (unsigned long) len, (unsigned long) plen);
		return;
	}

	sm->reqId = hdr->identifier;

	if (sm->workaround) {
		md5_vector(1, (const u8 **) &req, &plen, sm->req_md5);
	}

	switch (hdr->code) {
	case EAP_CODE_REQUEST:
		if (plen < sizeof(*hdr) + 1) {
			wpa_printf(MSG_DEBUG, "EAP: Too short EAP-Request - "
				   "no Type field");
			return;
		}
		sm->rxReq = TRUE;
		pos = (const u8 *) (hdr + 1);
		sm->reqMethod = (EapType) *pos++;
		if (sm->reqMethod == EAP_TYPE_EXPANDED) {
			if (plen < sizeof(*hdr) + 8) {
				wpa_printf(MSG_DEBUG, "EAP: Ignored truncated "
					   "expanded EAP-Packet (plen=%lu)",
					   (unsigned long) plen);
				return;
			}
			sm->reqVendor = WPA_GET_BE24(pos);
			pos += 3;
			sm->reqVendorMethod = WPA_GET_BE32(pos);
		}
		wpa_printf(MSG_DEBUG, "EAP: Received EAP-Request id=%d "
			   "method=%u vendor=%u vendorMethod=%u",
			   sm->reqId, sm->reqMethod, sm->reqVendor,
			   sm->reqVendorMethod);
		break;
	case EAP_CODE_RESPONSE:
		if (sm->selectedMethod == EAP_TYPE_LEAP) {
			/*
			 * LEAP differs from RFC 4137 by using reversed roles
			 * for mutual authentication and because of this, we
			 * need to accept EAP-Response frames if LEAP is used.
			 */
			if (plen < sizeof(*hdr) + 1) {
				wpa_printf(MSG_DEBUG, "EAP: Too short "
					   "EAP-Response - no Type field");
				return;
			}
			sm->rxResp = TRUE;
			pos = (const u8 *) (hdr + 1);
			sm->reqMethod = (EapType) *pos;
			wpa_printf(MSG_DEBUG, "EAP: Received EAP-Response for "
				   "LEAP method=%d id=%d",
				   sm->reqMethod, sm->reqId);
			break;
		}
		wpa_printf(MSG_DEBUG, "EAP: Ignored EAP-Response");
		break;
	case EAP_CODE_SUCCESS:
		wpa_printf(MSG_DEBUG, "EAP: Received EAP-Success");
		sm->rxSuccess = TRUE;
		break;
	case EAP_CODE_FAILURE:
		wpa_printf(MSG_DEBUG, "EAP: Received EAP-Failure");
		sm->rxFailure = TRUE;
		break;
	default:
		wpa_printf(MSG_DEBUG, "EAP: Ignored EAP-Packet with unknown "
			   "code %d", hdr->code);
		break;
	}
}


/**
 * eap_sm_init - Allocate and initialize EAP state machine
 * @eapol_ctx: Context data to be used with eapol_cb calls
 * @eapol_cb: Pointer to EAPOL callback functions
 * @msg_ctx: Context data for wpa_msg() calls
 * @conf: EAP configuration
 * Returns: Pointer to the allocated EAP state machine or %NULL on failure
 *
 * This function allocates and initializes an EAP state machine. In addition,
 * this initializes TLS library for the new EAP state machine. eapol_cb pointer
 * will be in use until eap_sm_deinit() is used to deinitialize this EAP state
 * machine. Consequently, the caller must make sure that this data structure
 * remains alive while the EAP state machine is active.
 */
struct eap_sm * eap_sm_init(void *eapol_ctx, struct eapol_callbacks *eapol_cb,
			    void *msg_ctx, struct eap_config *conf)
{
	struct eap_sm *sm;
	struct tls_config tlsconf;

	sm = os_zalloc(sizeof(*sm));
	if (sm == NULL)
		return NULL;
	sm->eapol_ctx = eapol_ctx;
	sm->eapol_cb = eapol_cb;
	sm->msg_ctx = msg_ctx;
	sm->ClientTimeout = 60;

	os_memset(&tlsconf, 0, sizeof(tlsconf));
	tlsconf.opensc_engine_path = conf->opensc_engine_path;
	tlsconf.pkcs11_engine_path = conf->pkcs11_engine_path;
	tlsconf.pkcs11_module_path = conf->pkcs11_module_path;
	sm->ssl_ctx = tls_init(&tlsconf);
	if (sm->ssl_ctx == NULL) {
		wpa_printf(MSG_WARNING, "SSL: Failed to initialize TLS "
			   "context.");
		os_free(sm);
		return NULL;
	}

	return sm;
}


/**
 * eap_sm_deinit - Deinitialize and free an EAP state machine
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 *
 * This function deinitializes EAP state machine and frees all allocated
 * resources.
 */
void eap_sm_deinit(struct eap_sm *sm)
{
	if (sm == NULL)
		return;
	eap_deinit_prev_method(sm, "EAP deinit");
	eap_sm_abort(sm);
	tls_deinit(sm->ssl_ctx);
	os_free(sm);
}


/**
 * eap_sm_step - Step EAP state machine
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 * Returns: 1 if EAP state was changed or 0 if not
 *
 * This function advances EAP state machine to a new state to match with the
 * current variables. This should be called whenever variables used by the EAP
 * state machine have changed.
 */
int eap_sm_step(struct eap_sm *sm)
{
	int res = 0;
	do {
		sm->changed = FALSE;
		SM_STEP_RUN(EAP);
		if (sm->changed || sm->again)
			res = 1;
	} while (sm->changed);
	return res;
}


/**
 * eap_sm_abort - Abort EAP authentication
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 *
 * Release system resources that have been allocated for the authentication
 * session without fully deinitializing the EAP state machine.
 */
void eap_sm_abort(struct eap_sm *sm)
{
	os_free(sm->lastRespData);
	sm->lastRespData = NULL;
	os_free(sm->eapRespData);
	sm->eapRespData = NULL;
	os_free(sm->eapKeyData);
	sm->eapKeyData = NULL;

	/* This is not clearly specified in the EAP statemachines draft, but
	 * it seems necessary to make sure that some of the EAPOL variables get
	 * cleared for the next authentication. */
	eapol_set_bool(sm, EAPOL_eapSuccess, FALSE);
}


#ifdef CONFIG_CTRL_IFACE
static const char * eap_sm_state_txt(int state)
{
	switch (state) {
	case EAP_INITIALIZE:
		return "INITIALIZE";
	case EAP_DISABLED:
		return "DISABLED";
	case EAP_IDLE:
		return "IDLE";
	case EAP_RECEIVED:
		return "RECEIVED";
	case EAP_GET_METHOD:
		return "GET_METHOD";
	case EAP_METHOD:
		return "METHOD";
	case EAP_SEND_RESPONSE:
		return "SEND_RESPONSE";
	case EAP_DISCARD:
		return "DISCARD";
	case EAP_IDENTITY:
		return "IDENTITY";
	case EAP_NOTIFICATION:
		return "NOTIFICATION";
	case EAP_RETRANSMIT:
		return "RETRANSMIT";
	case EAP_SUCCESS:
		return "SUCCESS";
	case EAP_FAILURE:
		return "FAILURE";
	default:
		return "UNKNOWN";
	}
}
#endif /* CONFIG_CTRL_IFACE */


#if defined(CONFIG_CTRL_IFACE) || !defined(CONFIG_NO_STDOUT_DEBUG)
static const char * eap_sm_method_state_txt(EapMethodState state)
{
	switch (state) {
	case METHOD_NONE:
		return "NONE";
	case METHOD_INIT:
		return "INIT";
	case METHOD_CONT:
		return "CONT";
	case METHOD_MAY_CONT:
		return "MAY_CONT";
	case METHOD_DONE:
		return "DONE";
	default:
		return "UNKNOWN";
	}
}


static const char * eap_sm_decision_txt(EapDecision decision)
{
	switch (decision) {
	case DECISION_FAIL:
		return "FAIL";
	case DECISION_COND_SUCC:
		return "COND_SUCC";
	case DECISION_UNCOND_SUCC:
		return "UNCOND_SUCC";
	default:
		return "UNKNOWN";
	}
}
#endif /* CONFIG_CTRL_IFACE || !CONFIG_NO_STDOUT_DEBUG */


#ifdef CONFIG_CTRL_IFACE

/**
 * eap_sm_get_status - Get EAP state machine status
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 * @buf: Buffer for status information
 * @buflen: Maximum buffer length
 * @verbose: Whether to include verbose status information
 * Returns: Number of bytes written to buf.
 *
 * Query EAP state machine for status information. This function fills in a
 * text area with current status information from the EAPOL state machine. If
 * the buffer (buf) is not large enough, status information will be truncated
 * to fit the buffer.
 */
int eap_sm_get_status(struct eap_sm *sm, char *buf, size_t buflen, int verbose)
{
	int len, ret;

	if (sm == NULL)
		return 0;

	len = os_snprintf(buf, buflen,
			  "EAP state=%s\n",
			  eap_sm_state_txt(sm->EAP_state));
	if (len < 0 || (size_t) len >= buflen)
		return 0;

	if (sm->selectedMethod != EAP_TYPE_NONE) {
		const char *name;
		if (sm->m) {
			name = sm->m->name;
		} else {
			const struct eap_method *m =
				eap_sm_get_eap_methods(EAP_VENDOR_IETF,
						       sm->selectedMethod);
			if (m)
				name = m->name;
			else
				name = "?";
		}
		ret = os_snprintf(buf + len, buflen - len,
				  "selectedMethod=%d (EAP-%s)\n",
				  sm->selectedMethod, name);
		if (ret < 0 || (size_t) ret >= buflen - len)
			return len;
		len += ret;

		if (sm->m && sm->m->get_status) {
			len += sm->m->get_status(sm, sm->eap_method_priv,
						 buf + len, buflen - len,
						 verbose);
		}
	}

	if (verbose) {
		ret = os_snprintf(buf + len, buflen - len,
				  "reqMethod=%d\n"
				  "methodState=%s\n"
				  "decision=%s\n"
				  "ClientTimeout=%d\n",
				  sm->reqMethod,
				  eap_sm_method_state_txt(sm->methodState),
				  eap_sm_decision_txt(sm->decision),
				  sm->ClientTimeout);
		if (ret < 0 || (size_t) ret >= buflen - len)
			return len;
		len += ret;
	}

	return len;
}
#endif /* CONFIG_CTRL_IFACE */


#if defined(CONFIG_CTRL_IFACE) || !defined(CONFIG_NO_STDOUT_DEBUG)
typedef enum {
	TYPE_IDENTITY, TYPE_PASSWORD, TYPE_OTP, TYPE_PIN, TYPE_NEW_PASSWORD,
	TYPE_PASSPHRASE
} eap_ctrl_req_type;

static void eap_sm_request(struct eap_sm *sm, eap_ctrl_req_type type,
			   const char *msg, size_t msglen)
{
	struct wpa_ssid *config;
	char *buf;
	size_t buflen;
	int len;
	char *field;
	char *txt, *tmp;

	if (sm == NULL)
		return;
	config = eap_get_config(sm);
	if (config == NULL)
		return;

	switch (type) {
	case TYPE_IDENTITY:
		field = "IDENTITY";
		txt = "Identity";
		config->pending_req_identity++;
		break;
	case TYPE_PASSWORD:
		field = "PASSWORD";
		txt = "Password";
		config->pending_req_password++;
		break;
	case TYPE_NEW_PASSWORD:
		field = "NEW_PASSWORD";
		txt = "New Password";
		config->pending_req_new_password++;
		break;
	case TYPE_PIN:
		field = "PIN";
		txt = "PIN";
		config->pending_req_pin++;
		break;
	case TYPE_OTP:
		field = "OTP";
		if (msg) {
			tmp = os_malloc(msglen + 3);
			if (tmp == NULL)
				return;
			tmp[0] = '[';
			os_memcpy(tmp + 1, msg, msglen);
			tmp[msglen + 1] = ']';
			tmp[msglen + 2] = '\0';
			txt = tmp;
			os_free(config->pending_req_otp);
			config->pending_req_otp = tmp;
			config->pending_req_otp_len = msglen + 3;
		} else {
			if (config->pending_req_otp == NULL)
				return;
			txt = config->pending_req_otp;
		}
		break;
	case TYPE_PASSPHRASE:
		field = "PASSPHRASE";
		txt = "Private key passphrase";
		config->pending_req_passphrase++;
		break;
	default:
		return;
	}

	buflen = 100 + os_strlen(txt) + config->ssid_len;
	buf = os_malloc(buflen);
	if (buf == NULL)
		return;
	len = os_snprintf(buf, buflen,
			  WPA_CTRL_REQ "%s-%d:%s needed for SSID ",
			  field, config->id, txt);
	if (len < 0 || (size_t) len >= buflen) {
		os_free(buf);
		return;
	}
	if (config->ssid && buflen > len + config->ssid_len) {
		os_memcpy(buf + len, config->ssid, config->ssid_len);
		len += config->ssid_len;
		buf[len] = '\0';
	}
	buf[buflen - 1] = '\0';
	wpa_msg(sm->msg_ctx, MSG_INFO, "%s", buf);
	os_free(buf);
}
#else /* CONFIG_CTRL_IFACE || !CONFIG_NO_STDOUT_DEBUG */
#define eap_sm_request(sm, type, msg, msglen) do { } while (0)
#endif /* CONFIG_CTRL_IFACE || !CONFIG_NO_STDOUT_DEBUG */


/**
 * eap_sm_request_identity - Request identity from user (ctrl_iface)
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 *
 * EAP methods can call this function to request identity information for the
 * current network. This is normally called when the identity is not included
 * in the network configuration. The request will be sent to monitor programs
 * through the control interface.
 */
void eap_sm_request_identity(struct eap_sm *sm)
{
	eap_sm_request(sm, TYPE_IDENTITY, NULL, 0);
}


/**
 * eap_sm_request_password - Request password from user (ctrl_iface)
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 *
 * EAP methods can call this function to request password information for the
 * current network. This is normally called when the password is not included
 * in the network configuration. The request will be sent to monitor programs
 * through the control interface.
 */
void eap_sm_request_password(struct eap_sm *sm)
{
	eap_sm_request(sm, TYPE_PASSWORD, NULL, 0);
}


/**
 * eap_sm_request_new_password - Request new password from user (ctrl_iface)
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 *
 * EAP methods can call this function to request new password information for
 * the current network. This is normally called when the EAP method indicates
 * that the current password has expired and password change is required. The
 * request will be sent to monitor programs through the control interface.
 */
void eap_sm_request_new_password(struct eap_sm *sm)
{
	eap_sm_request(sm, TYPE_NEW_PASSWORD, NULL, 0);
}


/**
 * eap_sm_request_pin - Request SIM or smart card PIN from user (ctrl_iface)
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 *
 * EAP methods can call this function to request SIM or smart card PIN
 * information for the current network. This is normally called when the PIN is
 * not included in the network configuration. The request will be sent to
 * monitor programs through the control interface.
 */
void eap_sm_request_pin(struct eap_sm *sm)
{
	eap_sm_request(sm, TYPE_PIN, NULL, 0);
}


/**
 * eap_sm_request_otp - Request one time password from user (ctrl_iface)
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 * @msg: Message to be displayed to the user when asking for OTP
 * @msg_len: Length of the user displayable message
 *
 * EAP methods can call this function to request open time password (OTP) for
 * the current network. The request will be sent to monitor programs through
 * the control interface.
 */
void eap_sm_request_otp(struct eap_sm *sm, const char *msg, size_t msg_len)
{
	eap_sm_request(sm, TYPE_OTP, msg, msg_len);
}


/**
 * eap_sm_request_passphrase - Request passphrase from user (ctrl_iface)
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 *
 * EAP methods can call this function to request passphrase for a private key
 * for the current network. This is normally called when the passphrase is not
 * included in the network configuration. The request will be sent to monitor
 * programs through the control interface.
 */
void eap_sm_request_passphrase(struct eap_sm *sm)
{
	eap_sm_request(sm, TYPE_PASSPHRASE, NULL, 0);
}


/**
 * eap_sm_notify_ctrl_attached - Notification of attached monitor
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 *
 * Notify EAP state machines that a monitor was attached to the control
 * interface to trigger re-sending of pending requests for user input.
 */
void eap_sm_notify_ctrl_attached(struct eap_sm *sm)
{
	struct wpa_ssid *config = eap_get_config(sm);

	if (config == NULL)
		return;

	/* Re-send any pending requests for user data since a new control
	 * interface was added. This handles cases where the EAP authentication
	 * starts immediately after system startup when the user interface is
	 * not yet running. */
	if (config->pending_req_identity)
		eap_sm_request_identity(sm);
	if (config->pending_req_password)
		eap_sm_request_password(sm);
	if (config->pending_req_new_password)
		eap_sm_request_new_password(sm);
	if (config->pending_req_otp)
		eap_sm_request_otp(sm, NULL, 0);
	if (config->pending_req_pin)
		eap_sm_request_pin(sm);
	if (config->pending_req_passphrase)
		eap_sm_request_passphrase(sm);
}


static int eap_allowed_phase2_type(int vendor, int type)
{
	if (vendor != EAP_VENDOR_IETF)
		return 0;
	return type != EAP_TYPE_PEAP && type != EAP_TYPE_TTLS &&
		type != EAP_TYPE_FAST;
}


/**
 * eap_get_phase2_type - Get EAP type for the given EAP phase 2 method name
 * @name: EAP method name, e.g., MD5
 * @vendor: Buffer for returning EAP Vendor-Id
 * Returns: EAP method type or %EAP_TYPE_NONE if not found
 *
 * This function maps EAP type names into EAP type numbers that are allowed for
 * Phase 2, i.e., for tunneled authentication. Phase 2 is used, e.g., with
 * EAP-PEAP, EAP-TTLS, and EAP-FAST.
 */
u32 eap_get_phase2_type(const char *name, int *vendor)
{
	int v;
	u8 type = eap_get_type(name, &v);
	if (eap_allowed_phase2_type(v, type)) {
		*vendor = v;
		return type;
	}
	*vendor = EAP_VENDOR_IETF;
	return EAP_TYPE_NONE;
}


/**
 * eap_get_phase2_types - Get list of allowed EAP phase 2 types
 * @config: Pointer to a network configuration
 * @count: Pointer to a variable to be filled with number of returned EAP types
 * Returns: Pointer to allocated type list or %NULL on failure
 *
 * This function generates an array of allowed EAP phase 2 (tunneled) types for
 * the given network configuration.
 */
struct eap_method_type * eap_get_phase2_types(struct wpa_ssid *config,
					      size_t *count)
{
	struct eap_method_type *buf;
	u32 method;
	int vendor;
	size_t mcount;
	const struct eap_method *methods, *m;

	methods = eap_peer_get_methods(&mcount);
	if (methods == NULL)
		return NULL;
	*count = 0;
	buf = os_malloc(mcount * sizeof(struct eap_method_type));
	if (buf == NULL)
		return NULL;

	for (m = methods; m; m = m->next) {
		vendor = m->vendor;
		method = m->method;
		if (eap_allowed_phase2_type(vendor, method)) {
			if (vendor == EAP_VENDOR_IETF &&
			    method == EAP_TYPE_TLS && config &&
			    config->private_key2 == NULL)
				continue;
			buf[*count].vendor = vendor;
			buf[*count].method = method;
			(*count)++;
		}
	}

	return buf;
}


/**
 * eap_set_fast_reauth - Update fast_reauth setting
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 * @enabled: 1 = Fast reauthentication is enabled, 0 = Disabled
 */
void eap_set_fast_reauth(struct eap_sm *sm, int enabled)
{
	sm->fast_reauth = enabled;
}


/**
 * eap_set_workaround - Update EAP workarounds setting
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 * @workaround: 1 = Enable EAP workarounds, 0 = Disable EAP workarounds
 */
void eap_set_workaround(struct eap_sm *sm, unsigned int workaround)
{
	sm->workaround = workaround;
}


/**
 * eap_get_config - Get current network configuration
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 * Returns: Pointer to the current network configuration or %NULL if not found
 *
 * EAP peer methods should avoid using this function if they can use other
 * access functions, like eap_get_config_identity() and
 * eap_get_config_password(), that do not require direct access to
 * struct wpa_ssid.
 */
struct wpa_ssid * eap_get_config(struct eap_sm *sm)
{
	return sm->eapol_cb->get_config(sm->eapol_ctx);
}


/**
 * eap_get_config_password - Get identity from the network configuration
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 * @len: Buffer for the length of the identity
 * Returns: Pointer to the identity or %NULL if not found
 */
const u8 * eap_get_config_identity(struct eap_sm *sm, size_t *len)
{
	struct wpa_ssid *config = eap_get_config(sm);
	if (config == NULL)
		return NULL;
	*len = config->identity_len;
	return config->identity;
}


/**
 * eap_get_config_password - Get password from the network configuration
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 * @len: Buffer for the length of the password
 * Returns: Pointer to the password or %NULL if not found
 */
const u8 * eap_get_config_password(struct eap_sm *sm, size_t *len)
{
	struct wpa_ssid *config = eap_get_config(sm);
	if (config == NULL)
		return NULL;
	*len = config->password_len;
	return config->password;
}


/**
 * eap_get_config_new_password - Get new password from network configuration
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 * @len: Buffer for the length of the new password
 * Returns: Pointer to the new password or %NULL if not found
 */
const u8 * eap_get_config_new_password(struct eap_sm *sm, size_t *len)
{
	struct wpa_ssid *config = eap_get_config(sm);
	if (config == NULL)
		return NULL;
	*len = config->new_password_len;
	return config->new_password;
}


/**
 * eap_get_config_otp - Get one-time password from the network configuration
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 * @len: Buffer for the length of the one-time password
 * Returns: Pointer to the one-time password or %NULL if not found
 */
const u8 * eap_get_config_otp(struct eap_sm *sm, size_t *len)
{
	struct wpa_ssid *config = eap_get_config(sm);
	if (config == NULL)
		return NULL;
	*len = config->otp_len;
	return config->otp;
}


/**
 * eap_clear_config_otp - Clear used one-time password
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 *
 * This function clears a used one-time password (OTP) from the current network
 * configuration. This should be called when the OTP has been used and is not
 * needed anymore.
 */
void eap_clear_config_otp(struct eap_sm *sm)
{
	struct wpa_ssid *config = eap_get_config(sm);
	if (config == NULL)
		return;
	os_memset(config->otp, 0, config->otp_len);
	os_free(config->otp);
	config->otp = NULL;
	config->otp_len = 0;
}


/**
 * eap_key_available - Get key availability (eapKeyAvailable variable)
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 * Returns: 1 if EAP keying material is available, 0 if not
 */
int eap_key_available(struct eap_sm *sm)
{
	return sm ? sm->eapKeyAvailable : 0;
}


/**
 * eap_notify_success - Notify EAP state machine about external success trigger
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 *
 * This function is called when external event, e.g., successful completion of
 * WPA-PSK key handshake, is indicating that EAP state machine should move to
 * success state. This is mainly used with security modes that do not use EAP
 * state machine (e.g., WPA-PSK).
 */
void eap_notify_success(struct eap_sm *sm)
{
	if (sm) {
		sm->decision = DECISION_COND_SUCC;
		sm->EAP_state = EAP_SUCCESS;
	}
}


/**
 * eap_notify_lower_layer_success - Notification of lower layer success
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 *
 * Notify EAP state machines that a lower layer has detected a successful
 * authentication. This is used to recover from dropped EAP-Success messages.
 */
void eap_notify_lower_layer_success(struct eap_sm *sm)
{
	if (sm == NULL)
		return;

	if (eapol_get_bool(sm, EAPOL_eapSuccess) ||
	    sm->decision == DECISION_FAIL ||
	    (sm->methodState != METHOD_MAY_CONT &&
	     sm->methodState != METHOD_DONE))
		return;

	if (sm->eapKeyData != NULL)
		sm->eapKeyAvailable = TRUE;
	eapol_set_bool(sm, EAPOL_eapSuccess, TRUE);
	wpa_msg(sm->msg_ctx, MSG_INFO, WPA_EVENT_EAP_SUCCESS
		"EAP authentication completed successfully (based on lower "
		"layer success)");
}


/**
 * eap_get_eapKeyData - Get master session key (MSK) from EAP state machine
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 * @len: Pointer to variable that will be set to number of bytes in the key
 * Returns: Pointer to the EAP keying data or %NULL on failure
 *
 * Fetch EAP keying material (MSK, eapKeyData) from the EAP state machine. The
 * key is available only after a successful authentication. EAP state machine
 * continues to manage the key data and the caller must not change or free the
 * returned data.
 */
const u8 * eap_get_eapKeyData(struct eap_sm *sm, size_t *len)
{
	if (sm == NULL || sm->eapKeyData == NULL) {
		*len = 0;
		return NULL;
	}

	*len = sm->eapKeyDataLen;
	return sm->eapKeyData;
}


/**
 * eap_get_eapKeyData - Get EAP response data
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 * @len: Pointer to variable that will be set to the length of the response
 * Returns: Pointer to the EAP response (eapRespData) or %NULL on failure
 *
 * Fetch EAP response (eapRespData) from the EAP state machine. This data is
 * available when EAP state machine has processed an incoming EAP request. The
 * EAP state machine does not maintain a reference to the response after this
 * function is called and the caller is responsible for freeing the data.
 */
u8 * eap_get_eapRespData(struct eap_sm *sm, size_t *len)
{
	u8 *resp;

	if (sm == NULL || sm->eapRespData == NULL) {
		*len = 0;
		return NULL;
	}

	resp = sm->eapRespData;
	*len = sm->eapRespDataLen;
	sm->eapRespData = NULL;
	sm->eapRespDataLen = 0;

	return resp;
}


/**
 * eap_sm_register_scard_ctx - Notification of smart card context
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 * @ctx: Context data for smart card operations
 *
 * Notify EAP state machines of context data for smart card operations. This
 * context data will be used as a parameter for scard_*() functions.
 */
void eap_register_scard_ctx(struct eap_sm *sm, void *ctx)
{
	if (sm)
		sm->scard_ctx = ctx;
}


/**
 * eap_hdr_validate - Validate EAP header
 * @vendor: Expected EAP Vendor-Id (0 = IETF)
 * @eap_type: Expected EAP type number
 * @msg: EAP frame (starting with EAP header)
 * @msglen: Length of msg
 * @plen: Pointer to variable to contain the returned payload length
 * Returns: Pointer to EAP payload (after type field), or %NULL on failure
 *
 * This is a helper function for EAP method implementations. This is usually
 * called in the beginning of struct eap_method::process() function to verify
 * that the received EAP request packet has a valid header. This function is
 * able to process both legacy and expanded EAP headers and in most cases, the
 * caller can just use the returned payload pointer (into *plen) for processing
 * the payload regardless of whether the packet used the expanded EAP header or
 * not.
 */
const u8 * eap_hdr_validate(int vendor, EapType eap_type,
			    const u8 *msg, size_t msglen, size_t *plen)
{
	const struct eap_hdr *hdr;
	const u8 *pos;
	size_t len;

	hdr = (const struct eap_hdr *) msg;

	if (msglen < sizeof(*hdr)) {
		wpa_printf(MSG_INFO, "EAP: Too short EAP frame");
		return NULL;
	}

	len = be_to_host16(hdr->length);
	if (len < sizeof(*hdr) + 1 || len > msglen) {
		wpa_printf(MSG_INFO, "EAP: Invalid EAP length");
		return NULL;
	}

	pos = (const u8 *) (hdr + 1);

	if (*pos == EAP_TYPE_EXPANDED) {
		int exp_vendor;
		u32 exp_type;
		if (len < sizeof(*hdr) + 8) {
			wpa_printf(MSG_INFO, "EAP: Invalid expanded EAP "
				   "length");
			return NULL;
		}
		pos++;
		exp_vendor = WPA_GET_BE24(pos);
		pos += 3;
		exp_type = WPA_GET_BE32(pos);
		pos += 4;
		if (exp_vendor != vendor || exp_type != (u32) eap_type) {
			wpa_printf(MSG_INFO, "EAP: Invalid expanded frame "
				   "type");
			return NULL;
		}

		*plen = len - sizeof(*hdr) - 8;
		return pos;
	} else {
		if (vendor != EAP_VENDOR_IETF || *pos != eap_type) {
			wpa_printf(MSG_INFO, "EAP: Invalid frame type");
			return NULL;
		}
		*plen = len - sizeof(*hdr) - 1;
		return pos + 1;
	}
}


/**
 * eap_set_config_blob - Set or add a named configuration blob
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 * @blob: New value for the blob
 *
 * Adds a new configuration blob or replaces the current value of an existing
 * blob.
 */
void eap_set_config_blob(struct eap_sm *sm, struct wpa_config_blob *blob)
{
	sm->eapol_cb->set_config_blob(sm->eapol_ctx, blob);
}


/**
 * eap_get_config_blob - Get a named configuration blob
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 * @name: Name of the blob
 * Returns: Pointer to blob data or %NULL if not found
 */
const struct wpa_config_blob * eap_get_config_blob(struct eap_sm *sm,
						   const char *name)
{
	return sm->eapol_cb->get_config_blob(sm->eapol_ctx, name);
}


/**
 * eap_set_force_disabled - Set force_disabled flag
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 * @disabled: 1 = EAP disabled, 0 = EAP enabled
 *
 * This function is used to force EAP state machine to be disabled when it is
 * not in use (e.g., with WPA-PSK or plaintext connections).
 */
void eap_set_force_disabled(struct eap_sm *sm, int disabled)
{
	sm->force_disabled = disabled;
}


/**
 * eap_msg_alloc - Allocate a buffer for an EAP message
 * @vendor: Vendor-Id (0 = IETF)
 * @type: EAP type
 * @len: Buffer for returning message length
 * @payload_len: Payload length in bytes (data after Type)
 * @code: Message Code (EAP_CODE_*)
 * @identifier: Identifier
 * @payload: Pointer to payload pointer that will be set to point to the
 * beginning of the payload or %NULL if payload pointer is not needed
 * Returns: Pointer to the allocated message buffer or %NULL on error
 *
 * This function can be used to allocate a buffer for an EAP message and fill
 * in the EAP header. This function is automatically using expanded EAP header
 * if the selected Vendor-Id is not IETF. In other words, most EAP methods do
 * not need to separately select which header type to use when using this
 * function to allocate the message buffers.
 */
struct eap_hdr * eap_msg_alloc(int vendor, EapType type, size_t *len,
			       size_t payload_len, u8 code, u8 identifier,
			       u8 **payload)
{
	struct eap_hdr *hdr;
	u8 *pos;

	*len = sizeof(struct eap_hdr) + (vendor == EAP_VENDOR_IETF ? 1 : 8) +
		payload_len;
	hdr = os_malloc(*len);
	if (hdr) {
		hdr->code = code;
		hdr->identifier = identifier;
		hdr->length = host_to_be16(*len);
		pos = (u8 *) (hdr + 1);
		if (vendor == EAP_VENDOR_IETF) {
			*pos++ = type;
		} else {
			*pos++ = EAP_TYPE_EXPANDED;
			WPA_PUT_BE24(pos, vendor);
			pos += 3;
			WPA_PUT_BE32(pos, type);
			pos += 4;
		}
		if (payload)
			*payload = pos;
	}

	return hdr;
}


 /**
 * eap_notify_pending - Notify that EAP method is ready to re-process a request
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 *
 * An EAP method can perform a pending operation (e.g., to get a response from
 * an external process). Once the response is available, this function can be
 * used to request EAPOL state machine to retry delivering the previously
 * received (and still unanswered) EAP request to EAP state machine.
 */
void eap_notify_pending(struct eap_sm *sm)
{
	sm->eapol_cb->notify_pending(sm->eapol_ctx);
}


/**
 * eap_invalidate_cached_session - Mark cached session data invalid
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 */
void eap_invalidate_cached_session(struct eap_sm *sm)
{
	if (sm)
		eap_deinit_prev_method(sm, "invalidate");
}

/* Local Variables: */
/* c-basic-offset: 8 */
/* indent-tabs-mode: t */
/* End: */
