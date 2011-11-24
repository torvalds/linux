/*
 * EAP peer state machine functions (RFC 4137)
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
 */

#ifndef EAP_H
#define EAP_H

#include "defs.h"
#include "eap_defs.h"
#include "eap_methods.h"

struct eap_sm;
struct wpa_ssid;
struct wpa_config_blob;

struct eap_method_type {
	int vendor;
	u32 method;
};

#ifdef IEEE8021X_EAPOL

/**
 * enum eapol_bool_var - EAPOL boolean state variables for EAP state machine
 *
 * These variables are used in the interface between EAP peer state machine and
 * lower layer. These are defined in RFC 4137, Sect. 4.1. Lower layer code is
 * expected to maintain these variables and register a callback functions for
 * EAP state machine to get and set the variables.
 */
enum eapol_bool_var {
	/**
	 * EAPOL_eapSuccess - EAP SUCCESS state reached
	 *
	 * EAP state machine reads and writes this value.
	 */
	EAPOL_eapSuccess,

	/**
	 * EAPOL_eapRestart - Lower layer request to restart authentication
	 *
	 * Set to TRUE in lower layer, FALSE in EAP state machine.
	 */
	EAPOL_eapRestart,

	/**
	 * EAPOL_eapFail - EAP FAILURE state reached
	 *
	 * EAP state machine writes this value.
	 */
	EAPOL_eapFail,

	/**
	 * EAPOL_eapResp - Response to send
	 *
	 * Set to TRUE in EAP state machine, FALSE in lower layer.
	 */
	EAPOL_eapResp,

	/**
	 * EAPOL_eapNoResp - Request has been process; no response to send
	 *
	 * Set to TRUE in EAP state machine, FALSE in lower layer.
	 */
	EAPOL_eapNoResp,

	/**
	 * EAPOL_eapReq - EAP request available from lower layer
	 *
	 * Set to TRUE in lower layer, FALSE in EAP state machine.
	 */
	EAPOL_eapReq,

	/**
	 * EAPOL_portEnabled - Lower layer is ready for communication
	 *
	 * EAP state machines reads this value.
	 */
	EAPOL_portEnabled,

	/**
	 * EAPOL_altAccept - Alternate indication of success (RFC3748)
	 *
	 * EAP state machines reads this value.
	 */
	EAPOL_altAccept,

	/**
	 * EAPOL_altReject - Alternate indication of failure (RFC3748)
	 *
	 * EAP state machines reads this value.
	 */
	EAPOL_altReject
};

/**
 * enum eapol_int_var - EAPOL integer state variables for EAP state machine
 *
 * These variables are used in the interface between EAP peer state machine and
 * lower layer. These are defined in RFC 4137, Sect. 4.1. Lower layer code is
 * expected to maintain these variables and register a callback functions for
 * EAP state machine to get and set the variables.
 */
enum eapol_int_var {
	/**
	 * EAPOL_idleWhile - Outside time for EAP peer timeout
	 *
	 * This integer variable is used to provide an outside timer that the
	 * external (to EAP state machine) code must decrement by one every
	 * second until the value reaches zero. This is used in the same way as
	 * EAPOL state machine timers. EAP state machine reads and writes this
	 * value.
	 */
	EAPOL_idleWhile
};

/**
 * struct eapol_callbacks - Callback functions from EAP to lower layer
 *
 * This structure defines the callback functions that EAP state machine
 * requires from the lower layer (usually EAPOL state machine) for updating
 * state variables and requesting information. eapol_ctx from eap_sm_init()
 * call will be used as the ctx parameter for these callback functions.
 */
struct eapol_callbacks {
	/**
	 * get_config - Get pointer to the current network configuration
	 * @ctx: eapol_ctx from eap_sm_init() call
	 */
	struct wpa_ssid * (*get_config)(void *ctx);

	/**
	 * get_bool - Get a boolean EAPOL state variable
	 * @variable: EAPOL boolean variable to get
	 * Returns: Value of the EAPOL variable
	 */
	Boolean (*get_bool)(void *ctx, enum eapol_bool_var variable);

	/**
	 * set_bool - Set a boolean EAPOL state variable
	 * @ctx: eapol_ctx from eap_sm_init() call
	 * @variable: EAPOL boolean variable to set
	 * @value: Value for the EAPOL variable
	 */
	void (*set_bool)(void *ctx, enum eapol_bool_var variable,
			 Boolean value);

	/**
	 * get_int - Get an integer EAPOL state variable
	 * @ctx: eapol_ctx from eap_sm_init() call
	 * @variable: EAPOL integer variable to get
	 * Returns: Value of the EAPOL variable
	 */
	unsigned int (*get_int)(void *ctx, enum eapol_int_var variable);

	/**
	 * set_int - Set an integer EAPOL state variable
	 * @ctx: eapol_ctx from eap_sm_init() call
	 * @variable: EAPOL integer variable to set
	 * @value: Value for the EAPOL variable
	 */
	void (*set_int)(void *ctx, enum eapol_int_var variable,
			unsigned int value);

	/**
	 * get_eapReqData - Get EAP-Request data
	 * @ctx: eapol_ctx from eap_sm_init() call
	 * @len: Pointer to variable that will be set to eapReqDataLen
	 * Returns: Reference to eapReqData (EAP state machine will not free
	 * this) or %NULL if eapReqData not available.
	 */
	u8 * (*get_eapReqData)(void *ctx, size_t *len);

	/**
	 * set_config_blob - Set named configuration blob
	 * @ctx: eapol_ctx from eap_sm_init() call
	 * @blob: New value for the blob
	 *
	 * Adds a new configuration blob or replaces the current value of an
	 * existing blob.
	 */
	void (*set_config_blob)(void *ctx, struct wpa_config_blob *blob);

	/**
	 * get_config_blob - Get a named configuration blob
	 * @ctx: eapol_ctx from eap_sm_init() call
	 * @name: Name of the blob
	 * Returns: Pointer to blob data or %NULL if not found
	 */
	const struct wpa_config_blob * (*get_config_blob)(void *ctx,
							  const char *name);

	/**
	 * notify_pending - Notify that a pending request can be retried
	 * @ctx: eapol_ctx from eap_sm_init() call
	 *
	 * An EAP method can perform a pending operation (e.g., to get a
	 * response from an external process). Once the response is available,
	 * this callback function can be used to request EAPOL state machine to
	 * retry delivering the previously received (and still unanswered) EAP
	 * request to EAP state machine.
	 */
	void (*notify_pending)(void *ctx);
};

/**
 * struct eap_config - Configuration for EAP state machine
 */
struct eap_config {
	/**
	 * opensc_engine_path - OpenSC engine for OpenSSL engine support
	 *
	 * Usually, path to engine_opensc.so.
	 */
	const char *opensc_engine_path;
	/**
	 * pkcs11_engine_path - PKCS#11 engine for OpenSSL engine support
	 *
	 * Usually, path to engine_pkcs11.so.
	 */
	const char *pkcs11_engine_path;
	/**
	 * pkcs11_module_path - OpenSC PKCS#11 module for OpenSSL engine
	 *
	 * Usually, path to opensc-pkcs11.so.
	 */
	const char *pkcs11_module_path;
};

struct eap_sm * eap_sm_init(void *eapol_ctx, struct eapol_callbacks *eapol_cb,
			    void *msg_ctx, struct eap_config *conf);
void eap_sm_deinit(struct eap_sm *sm);
int eap_sm_step(struct eap_sm *sm);
void eap_sm_abort(struct eap_sm *sm);
int eap_sm_get_status(struct eap_sm *sm, char *buf, size_t buflen,
		      int verbose);
u8 * eap_sm_buildIdentity(struct eap_sm *sm, int id, size_t *len,
			  int encrypted);
void eap_sm_request_identity(struct eap_sm *sm);
void eap_sm_request_password(struct eap_sm *sm);
void eap_sm_request_new_password(struct eap_sm *sm);
void eap_sm_request_pin(struct eap_sm *sm);
void eap_sm_request_otp(struct eap_sm *sm, const char *msg, size_t msg_len);
void eap_sm_request_passphrase(struct eap_sm *sm);
void eap_sm_notify_ctrl_attached(struct eap_sm *sm);
u32 eap_get_phase2_type(const char *name, int *vendor);
struct eap_method_type * eap_get_phase2_types(struct wpa_ssid *config,
					      size_t *count);
void eap_set_fast_reauth(struct eap_sm *sm, int enabled);
void eap_set_workaround(struct eap_sm *sm, unsigned int workaround);
void eap_set_force_disabled(struct eap_sm *sm, int disabled);
int eap_key_available(struct eap_sm *sm);
void eap_notify_success(struct eap_sm *sm);
void eap_notify_lower_layer_success(struct eap_sm *sm);
const u8 * eap_get_eapKeyData(struct eap_sm *sm, size_t *len);
u8 * eap_get_eapRespData(struct eap_sm *sm, size_t *len);
void eap_register_scard_ctx(struct eap_sm *sm, void *ctx);
void eap_invalidate_cached_session(struct eap_sm *sm);

#endif /* IEEE8021X_EAPOL */

#endif /* EAP_H */
