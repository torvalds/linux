/* $Id: wpa_param.h 10006 2008-09-22 08:55:36Z joda $ */

#ifndef __wpa_param_h__
#define __wpa_param_h__ 1

/* we need to obtain certificates from somewhere, presumably there is
 * already a cerificate store in the phone somewhere that we can use,
 * so if this is passed as a raw certificate, as a filename, or as a
 * handle to some existing store doesn't really matter. */ 
/* for now, assume this is a filename */
typedef const char *certificate_t;
/* the same goes for the private key, one problem is that currently we
 * don't support reading private keys and certificates on PKCS#12
 * format, but require that it is converted to raw unencrypted DER */ 
/* for now, assume this is a filename */
typedef const char *private_key_t;


/* EAP-TLS uses certificates for both server and client
 * authentication */
struct eap_tls_data {
   const char *identity;              /* the identity to use 
                                         for authentication */
   certificate_t client_cert;         /* the client certificate to use */
   private_key_t private_key;         /* private key to use */
   const char *private_key_password;  /* ...and it password, but see above */
   certificate_t ca_certs;            /* a list of CA certificates */
};

/* EAP-TTLS uses certificates for server authentication, but uses an
 * inner phase 2 protocol for client authentication, currently
 * supported phase 2 protocols are EAP-MD5 and EAP-MSCHAPv2 */
/* Q: do we need to distinguish between MD5 and MSCHAPv2, currently
 * use use whichever the server says? */ 
struct eap_ttls_data {
   const char *identity_phase1;  /* the identity to use 
                                    for phase 1 only, can be NULL */
   char identity_phase2;  /* the identity to use for phase 2,
                                    and phase 1 if identity_phase1 is
                                    NULL */
   const char *password;         /* password to use for phase 2
                                  * authentication */
   certificate_t ca_certs;       /* a list of CA certificates */
};

/* EAP-PEAP uses certificates for server authentication, but uses an
 * inner phase 2 protocol for client authentication, the currently
 * supported phase 2 protocols is EAP-MSCHAPv2 */ 
struct eap_peap_data {
   const char *identity_phase1;  /* the identity to use 
                                    for phase 1 only, can be NULL */
   char identity_phase2;  /* the identity to use for phase 2,
                                    and phase 1 if identity_phase1 is
                                    NULL */
   const char *password;         /* password to use for phase 2
                                  * authentication */
   certificate_t ca_certs;       /* a list of CA certificates */
};

/* EAP-SIM uses GSM SIM card for authentication */ 
struct eap_sim_data {
   int dummy; /* currently we need no additional information, but some
               * may be necessary in the future */
 };

/* EAP-AKA uses UMTS SIM card for authentication */
struct eap_aka_data {
   int dummy; /* currently we need no additional information, but some
               * may be necessary in the future */
};

/* WAPI Certificate uses certificates for both server and client
 * authentication */
struct wapi_cert_param_data {
   certificate_t client_cert;         /* the client certificate to use */
   private_key_t private_key;         /* private key to use */
   certificate_t ca_certs;            /* a list of CA certificates */
};

enum wpa_auth_mode { 
   WPA_PSK,
   WPA_EAP_TTLS,
   WPA_EAP_TLS,
   WPA_EAP_PEAP,
   WPA_EAP_SIM,
   WPA_EAP_AKA,
   WAPI_PSK,
   WAPI_CERT
};

/* this struct contains information needed for WPA authentication */ 
struct wpa_param {
   enum wpa_auth_mode auth_mode;  /* this specifies which mode to use */
   union {
      const char* key;            /* WPA-PSK */
      struct eap_tls_data tls;    /* WPA-EAP w/ EAP-TLS */
      struct eap_ttls_data ttls;  /* WPA-EAP w/ EAP-TTLS */
      struct eap_peap_data peap;  /* WPA-EAP w/ EAP-PEAP */
      struct eap_sim_data sim;    /* WPA-EAP w/ EAP-SIM */
      struct eap_aka_data aka;    /* WPA-EAP w/ EAP-AKA */
      struct wapi_cert_param_data wapi_param_cert; /* WAPI Certificate */
   } u;
};

int wpa_new_network(const char *ssid, const struct wpa_param *wp);

#endif /* __wpa_param_h__ */
