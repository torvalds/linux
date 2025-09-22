/*	$OpenBSD: eap.h,v 1.7 2024/07/13 12:22:46 yasuoka Exp $	*/

/*
 * Copyright (c) 2010-2013 Reyk Floeter <reyk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef IKED_EAP_H
#define IKED_EAP_H

struct eap_header {
	uint8_t		eap_code;
	uint8_t		eap_id;
	uint16_t	eap_length;
} __packed;

struct eap_message {
	uint8_t		eap_code;
	uint8_t		eap_id;
	uint16_t	eap_length;
	uint8_t		eap_type;
	/* Followed by type-specific data */
} __packed;

#define EAP_CODE_REQUEST	1	/* Request */
#define EAP_CODE_RESPONSE	2	/* Response */
#define EAP_CODE_SUCCESS	3	/* Success */
#define EAP_CODE_FAILURE	4	/* Failure */

extern struct iked_constmap eap_code_map[];

/* http://www.iana.org/assignments/eap-numbers */
#define EAP_TYPE_NONE		0	/* NONE */
#define EAP_TYPE_IDENTITY	1	/* RFC3748 */
#define EAP_TYPE_NOTIFICATION	2	/* RFC3748 */
#define EAP_TYPE_NAK		3	/* RFC3748 */
#define EAP_TYPE_MD5		4	/* RFC3748 */
#define EAP_TYPE_OTP		5	/* RFC3748 */
#define EAP_TYPE_GTC		6	/* RFC3748 */
#define EAP_TYPE_RSA		9	/* Whelan */
#define EAP_TYPE_DSS		10	/* Nace */
#define EAP_TYPE_KEA		11	/* Nace */
#define EAP_TYPE_KEA_VALIDATE	12	/* Nace */
#define EAP_TYPE_TLS		13	/* RFC5216 */
#define EAP_TYPE_AXENT		14	/* Rosselli */
#define EAP_TYPE_SECURID	15	/* Nystrm */
#define EAP_TYPE_ARCOT		16	/* Jerdonek */
#define EAP_TYPE_CISCO		17	/* Norman */
#define EAP_TYPE_SIM		18	/* RFC4186 */
#define EAP_TYPE_SRP_SHA1	19	/* Carlson */
#define EAP_TYPE_TTLS		21	/* Funk */
#define EAP_TYPE_RAS		22	/* Fields */
#define EAP_TYPE_OAAKA		23	/* RFC4187 */
#define EAP_TYPE_3COM		24	/* Young */
#define EAP_TYPE_PEAP		25	/* Palekar */
#define EAP_TYPE_MSCHAP_V2	26	/* Palekar */
#define EAP_TYPE_MAKE		27	/* Berrendonner */
#define EAP_TYPE_CRYPTOCARD	28	/* Webb */
#define EAP_TYPE_MSCHAP_V2_2	29	/* Potter */
#define EAP_TYPE_DYNAMID	30	/* Merlin */
#define EAP_TYPE_ROB		31	/* Ullah */
#define EAP_TYPE_POTP		32	/* RFC4794 */
#define EAP_TYPE_MS_TLV		33	/* Palekar */
#define EAP_TYPE_SENTRINET	34	/* Kelleher */
#define EAP_TYPE_ACTIONTEC	35	/* Chang */
#define EAP_TYPE_BIOMETRICS	36	/* Xiong */
#define EAP_TYPE_AIRFORTRESS	37	/* Hibbard */
#define EAP_TYPE_HTTP_DIGEST	38	/* Tavakoli */
#define EAP_TYPE_SECURESUITE	39	/* Clements */
#define EAP_TYPE_DEVICECONNECT	40	/* Pitard */
#define EAP_TYPE_SPEKE		41	/* Zick */
#define EAP_TYPE_MOBAC		42	/* Rixom */
#define EAP_TYPE_FAST		43	/* Cam-Winget */
#define EAP_TYPE_ZLX		44	/* Bogue */
#define EAP_TYPE_LINK		45	/* Zick */
#define EAP_TYPE_PAX		46	/* Clancy */
#define EAP_TYPE_PSK		47	/* RFC-bersani-eap-psk-11.txt */
#define EAP_TYPE_SAKE		48	/* RFC-vanderveen-eap-sake-02.txt */
#define EAP_TYPE_IKEV2		49	/* RFC5106 */
#define EAP_TYPE_AKA2		50	/* RFC5448 */
#define EAP_TYPE_GPSK		51	/* RFC5106 */
#define EAP_TYPE_PWD		52	/* RFC-harkins-emu-eap-pwd-12.txt */
#define EAP_TYPE_EXPANDED_TYPE	254	/* RFC3748 */
#define EAP_TYPE_EXPERIMENTAL	255	/* RFC3748 */
#define EAP_TYPE_RADIUS		10001	/* internal use for EAP RADIUS */

extern struct iked_constmap eap_type_map[];

/*
 * EAP MSCHAP-V2
 */

#define EAP_MSCHAP_CHALLENGE_SZ		16
#define EAP_MSCHAP_RESPONSE_SZ		49
#define EAP_MSCHAP_NTRESPONSE_SZ	24
#define EAP_MSCHAP_SUCCESS_SZ		42

#define EAP_MSOPCODE_CHALLENGE		1	/* Challenge */
#define EAP_MSOPCODE_RESPONSE		2	/* Response */
#define EAP_MSOPCODE_SUCCESS		3	/* Success */
#define EAP_MSOPCODE_FAILURE		4	/* Failure */
#define EAP_MSOPCODE_CHANGE_PASSWORD	7	/* Change Password */

extern struct iked_constmap eap_msopcode_map[];

struct eap_mschap {
	uint8_t				ms_opcode;
} __packed;

struct eap_mschap_challenge {
	uint8_t				msc_opcode;
	uint8_t				msc_id;
	uint16_t			msc_length;
	uint8_t				msc_valuesize;
	uint8_t				msc_challenge[EAP_MSCHAP_CHALLENGE_SZ];
	/* Followed by variable-size name field */
} __packed;

struct eap_mschap_peer {
	uint8_t				msp_challenge[EAP_MSCHAP_CHALLENGE_SZ];
	uint8_t				msp_reserved[8];
	uint8_t				msp_ntresponse[EAP_MSCHAP_NTRESPONSE_SZ];
	uint8_t				msp_flags;
};

struct eap_mschap_response {
	uint8_t				msr_opcode;
	uint8_t				msr_id;
	uint16_t			msr_length;
	uint8_t				msr_valuesize;
	union {
		uint8_t			resp_data[EAP_MSCHAP_RESPONSE_SZ];
		struct eap_mschap_peer	resp_peer;
	}				msr_response;
	/* Followed by variable-size name field */
} __packed;

struct eap_mschap_success {
	uint8_t				mss_opcode;
	uint8_t				mss_id;
	uint16_t			mss_length;
	/* Followed by variable-size success message */
} __packed;

struct eap_mschap_failure {
	uint8_t				msf_opcode;
	uint8_t				msf_id;
	uint16_t			msf_length;
	/* Followed by variable-size message field */
} __packed;

#define EAP_MSERROR_RESTRICTED_LOGON_HOURS	646	/* eap-mschapv2 */
#define EAP_MSERROR_ACCT_DISABLED		647	/* eap-mschapv2 */
#define EAP_MSERROR_PASSWD_EXPIRED		648	/* eap-mschapv2 */
#define EAP_MSERROR_NO_DIALIN_PERMISSION	649	/* eap-mschapv2 */
#define EAP_MSERROR_AUTHENTICATION_FAILURE	691	/* eap-mschapv2 */
#define EAP_MSERROR_CHANGING_PASSWORD		709	/* eap-mschapv2 */

extern struct iked_constmap eap_mserror_map[];

#endif /* IKED_EAP_H */
