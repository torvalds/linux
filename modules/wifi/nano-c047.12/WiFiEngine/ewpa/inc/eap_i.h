/*
 * EAP peer state machines internal structures (RFC 4137)
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

#ifndef EAP_I_H
#define EAP_I_H

#include "eap.h"

/* RFC 4137 - EAP Peer state machine */

typedef enum {
	DECISION_FAIL, DECISION_COND_SUCC, DECISION_UNCOND_SUCC
} EapDecision;

typedef enum {
	METHOD_NONE, METHOD_INIT, METHOD_CONT, METHOD_MAY_CONT, METHOD_DONE
} EapMethodState;

/**
 * struct eap_method_ret - EAP return values from struct eap_method::process()
 *
 * These structure contains OUT variables for the interface between peer state
 * machine and methods (RFC 4137, Sect. 4.2). eapRespData will be returned as
 * the return value of struct eap_method::process() so it is not included in
 * this structure.
 */
struct eap_method_ret {
	/**
	 * ignore - Whether method decided to drop the current packed (OUT)
	 */
	Boolean ignore;

	/**
	 * methodState - Method-specific state (IN/OUT)
	 */
	EapMethodState methodState;

	/**
	 * decision - Authentication decision (OUT)
	 */
	EapDecision decision;

	/**
	 * allowNotifications - Whether method allows notifications (OUT)
	 */
	Boolean allowNotifications;
};


/**
 * struct eap_method - EAP method interface
 * This structure defines the EAP method interface. Each method will need to
 * register its own EAP type, EAP name, and set of function pointers for method
 * specific operations. This interface is based on section 4.4 of RFC 4137.
 */
struct eap_method {
	/**
	 * vendor - EAP Vendor-ID (EAP_VENDOR_*) (0 = IETF)
	 */
	int vendor;

	/**
	 * method - EAP type number (EAP_TYPE_*)
	 */
	EapType method;

	/**
	 * name - Name of the method (e.g., "TLS")
	 */
	const char *name;

	/**
	 * init - Initialize an EAP method
	 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
	 * Returns: Pointer to allocated private data, or %NULL on failure
	 *
	 * This function is used to initialize the EAP method explicitly
	 * instead of using METHOD_INIT state as specific in RFC 4137. The
	 * method is expected to initialize it method-specific state and return
	 * a pointer that will be used as the priv argument to other calls.
	 */
	void * (*init)(struct eap_sm *sm);

	/**
	 * deinit - Deinitialize an EAP method
	 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
	 * @priv: Pointer to private EAP method data from eap_method::init()
	 *
	 * Deinitialize the EAP method and free any allocated private data.
	 */
	void (*deinit)(struct eap_sm *sm, void *priv);

	/**
	 * process - Process an EAP request
	 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
	 * @priv: Pointer to private EAP method data from eap_method::init()
	 * @ret: Return values from EAP request validation and processing
	 * @reqData: EAP request to be processed (eapReqData)
	 * @reqDataLen: Length of the EAP request
	 * @respDataLen: Length of the returned EAP response
	 * Returns: Pointer to allocated EAP response packet (eapRespData)
	 *
	 * This function is a combination of m.check(), m.process(), and
	 * m.buildResp() procedures defined in section 4.4 of RFC 4137 In other
	 * words, this function validates the incoming request, processes it,
	 * and build a response packet. m.check() and m.process() return values
	 * are returned through struct eap_method_ret *ret variable. Caller is
	 * responsible for freeing the returned EAP response packet.
	 */
	u8 * (*process)(struct eap_sm *sm, void *priv,
			struct eap_method_ret *ret,
			const u8 *reqData, size_t reqDataLen,
			size_t *respDataLen);

	/**
	 * isKeyAvailable - Find out whether EAP method has keying material
	 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
	 * @priv: Pointer to private EAP method data from eap_method::init()
	 * Returns: %TRUE if key material (eapKeyData) is available
	 */
	Boolean (*isKeyAvailable)(struct eap_sm *sm, void *priv);

	/**
	 * getKey - Get EAP method specific keying material (eapKeyData)
	 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
	 * @priv: Pointer to private EAP method data from eap_method::init()
	 * @len: Pointer to variable to store key length (eapKeyDataLen)
	 * Returns: Keying material (eapKeyData) or %NULL if not available
	 *
	 * This function can be used to get the keying material from the EAP
	 * method. The key may already be stored in the method-specific private
	 * data or this function may derive the key.
	 */
	u8 * (*getKey)(struct eap_sm *sm, void *priv, size_t *len);

	/**
	 * get_status - Get EAP method status
	 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
	 * @priv: Pointer to private EAP method data from eap_method::init()
	 * @buf: Buffer for status information
	 * @buflen: Maximum buffer length
	 * @verbose: Whether to include verbose status information
	 * Returns: Number of bytes written to buf
	 *
	 * Query EAP method for status information. This function fills in a
	 * text area with current status information from the EAP method. If
	 * the buffer (buf) is not large enough, status information will be
	 * truncated to fit the buffer.
	 */
	int (*get_status)(struct eap_sm *sm, void *priv, char *buf,
			  size_t buflen, int verbose);

	/**
	 * has_reauth_data - Whether method is ready for fast reauthentication
	 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
	 * @priv: Pointer to private EAP method data from eap_method::init()
	 * Returns: %TRUE or %FALSE based on whether fast reauthentication is
	 * possible
	 *
	 * This function is an optional handler that only EAP methods
	 * supporting fast re-authentication need to implement.
	 */
	Boolean (*has_reauth_data)(struct eap_sm *sm, void *priv);

	/**
	 * deinit_for_reauth - Release data that is not needed for fast re-auth
	 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
	 * @priv: Pointer to private EAP method data from eap_method::init()
	 *
	 * This function is an optional handler that only EAP methods
	 * supporting fast re-authentication need to implement. This is called
	 * when authentication has been completed and EAP state machine is
	 * requesting that enough state information is maintained for fast
	 * re-authentication
	 */
	void (*deinit_for_reauth)(struct eap_sm *sm, void *priv);

	/**
	 * init_for_reauth - Prepare for start of fast re-authentication
	 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
	 * @priv: Pointer to private EAP method data from eap_method::init()
	 *
	 * This function is an optional handler that only EAP methods
	 * supporting fast re-authentication need to implement. This is called
	 * when EAP authentication is started and EAP state machine is
	 * requesting fast re-authentication to be used.
	 */
	void * (*init_for_reauth)(struct eap_sm *sm, void *priv);

	/**
	 * get_identity - Get method specific identity for re-authentication
	 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
	 * @priv: Pointer to private EAP method data from eap_method::init()
	 * @len: Length of the returned identity
	 * Returns: Pointer to the method specific identity or %NULL if default
	 * identity is to be used
	 *
	 * This function is an optional handler that only EAP methods
	 * that use method specific identity need to implement.
	 */
	const u8 * (*get_identity)(struct eap_sm *sm, void *priv, size_t *len);

	/**
	 * free - Free EAP method data
	 * @method: Pointer to the method data registered with
	 * eap_peer_method_register().
	 *
	 * This function will be called when the EAP method is being
	 * unregistered. If the EAP method allocated resources during
	 * registration (e.g., allocated struct eap_method), they should be
	 * freed in this function. No other method functions will be called
	 * after this call. If this function is not defined (i.e., function
	 * pointer is %NULL), a default handler is used to release the method
	 * data with free(method). This is suitable for most cases.
	 */
	void (*free)(struct eap_method *method);

#define EAP_PEER_METHOD_INTERFACE_VERSION 1
	/**
	 * version - Version of the EAP peer method interface
	 *
	 * The EAP peer method implementation should set this variable to
	 * EAP_PEER_METHOD_INTERFACE_VERSION. This is used to verify that the
	 * EAP method is using supported API version when using dynamically
	 * loadable EAP methods.
	 */
	int version;

	/**
	 * next - Pointer to the next EAP method
	 *
	 * This variable is used internally in the EAP method registration code
	 * to create a linked list of registered EAP methods.
	 */
	struct eap_method *next;

#ifdef CONFIG_DYNAMIC_EAP_METHODS
	/**
	 * dl_handle - Handle for the dynamic library
	 *
	 * This variable is used internally in the EAP method registration code
	 * to store a handle for the dynamic library. If the method is linked
	 * in statically, this is %NULL.
	 */
	void *dl_handle;
#endif /* CONFIG_DYNAMIC_EAP_METHODS */

	/**
	 * get_emsk - Get EAP method specific keying extended material (EMSK)
	 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
	 * @priv: Pointer to private EAP method data from eap_method::init()
	 * @len: Pointer to a variable to store EMSK length
	 * Returns: EMSK or %NULL if not available
	 *
	 * This function can be used to get the extended keying material from
	 * the EAP method. The key may already be stored in the method-specific
	 * private data or this function may derive the key.
	 */
	u8 * (*get_emsk)(struct eap_sm *sm, void *priv, size_t *len);
};


typedef enum {
    EAP_INITIALIZE,
	EAP_DISABLED,
	EAP_IDLE,
	EAP_RECEIVED,
    EAP_GET_METHOD,
	EAP_METHOD,
	EAP_SEND_RESPONSE,
	EAP_DISCARD,
    EAP_IDENTITY,
	EAP_NOTIFICATION,
	EAP_RETRANSMIT,
	EAP_SUCCESS,
    EAP_FAILURE,
	EAP_METHOD_SIMWAIT
} EAP_state_TYPE;

/**
 * struct eap_sm - EAP state machine data
 */
struct eap_sm { 
	EAP_state_TYPE EAP_state;
	/* Long-term local variables */
	EapType selectedMethod;
	EapMethodState methodState;
	int lastId;
	u8 *lastRespData;
	size_t lastRespDataLen;
	EapDecision decision;
	/* Short-term local variables */
	Boolean rxReq;
	Boolean rxSuccess;
	Boolean rxFailure;
	int reqId;
	EapType reqMethod;
	int reqVendor;
	u32 reqVendorMethod;
	Boolean ignore;
	/* Constants */
	int ClientTimeout;

	/* Miscellaneous variables */
	Boolean allowNotifications; /* peer state machine <-> methods */
	u8 *eapRespData; /* peer to lower layer */
	size_t eapRespDataLen; /* peer to lower layer */
	Boolean eapKeyAvailable; /* peer to lower layer */
	u8 *eapKeyData; /* peer to lower layer */
	size_t eapKeyDataLen; /* peer to lower layer */
	const struct eap_method *m; /* selected EAP method */
	Boolean again;
	/* not defined in RFC 4137 */
	Boolean changed;
	void *eapol_ctx;
	struct eapol_callbacks *eapol_cb;
	void *eap_method_priv;
	int init_phase2;
	int fast_reauth;

	Boolean rxResp /* LEAP only */;
	Boolean leap_done;
	Boolean peap_done;
	u8 req_md5[16]; /* MD5() of the current EAP packet */
	u8 last_md5[16]; /* MD5() of the previously received EAP packet; used
			  * in duplicate request detection. */

	void *msg_ctx;
	void *scard_ctx;
	void *ssl_ctx;

	unsigned int workaround;

	/* Optional challenges generated in Phase 1 (EAP-FAST) */
	u8 *peer_challenge, *auth_challenge;
	int mschapv2_full_key; /* Request full MSCHAPv2 key */

	int num_rounds;
	int force_disabled;
};

const u8 * eap_hdr_validate(int vendor, EapType eap_type,
			    const u8 *msg, size_t msglen, size_t *plen);
const u8 * eap_get_config_identity(struct eap_sm *sm, size_t *len);
const u8 * eap_get_config_password(struct eap_sm *sm, size_t *len);
const u8 * eap_get_config_new_password(struct eap_sm *sm, size_t *len);
const u8 * eap_get_config_otp(struct eap_sm *sm, size_t *len);
void eap_clear_config_otp(struct eap_sm *sm);
struct wpa_ssid * eap_get_config(struct eap_sm *sm);
void eap_set_config_blob(struct eap_sm *sm, struct wpa_config_blob *blob);
const struct wpa_config_blob *
eap_get_config_blob(struct eap_sm *sm, const char *name);
struct eap_hdr * eap_msg_alloc(int vendor, EapType type, size_t *len,
			       size_t payload_len, u8 code, u8 identifier,
			       u8 **payload);
void eap_notify_pending(struct eap_sm *sm);

#endif /* EAP_I_H */

/* Local Variables: */
/* c-basic-offset: 8 */
/* indent-tabs-mode: t */
/* End: */
