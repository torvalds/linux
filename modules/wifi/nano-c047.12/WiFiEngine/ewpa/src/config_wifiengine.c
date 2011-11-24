/*
 * WPA Supplicant / Configuration backend: empty starting point
 * Copyright (c) 2003-2005, Jouni Malinen <j@w1.fi>
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
 * This file implements dummy example of a configuration backend. None of the
 * functions are actually implemented so this can be used as a simple
 * compilation test or a starting point for a new configuration backend.
 */

#include "driverenv.h"

#include "includes.h"

#include "common.h"
#include "config.h"
#include "base64.h"
#include "eap_methods.h"
#include "eloop.h"

#include "wpa_param.h"

#include "wapi.h"
#include "wapi_i.h"
#include "cert.h"

/* 
 * defined in cert.h
 * TODO: This should be moved to eloop structs
 */
extern struct wapi_cert_data wapi_cert;

int wpa_config_find_and_add_pkey(const char *name, const void *src, int src_len);
int wpa_config_find_and_add_certificate(const char *name, const void *src, int src_len);

static int
set_ssid_psk(struct wpa_ssid *new, const char *ssid, const char *psk)
{
	char str[128];

	DE_SNPRINTF(str, sizeof(str), "\"%s\"", ssid);
	if(wpa_config_set(new, "ssid", str, 1) != 0) {
		return -1;
	}
	if(DE_STRLEN(psk) == 64) {
		if(wpa_config_set(new, "psk", psk, 1) != 0) {
			return -1;
		}
	} else {
		DE_SNPRINTF(str, sizeof(str), "\"%s\"", psk);
		if(wpa_config_set(new, "psk", str, 1) != 0) {
			return -1;
		}
		wpa_config_update_psk(new);
	}
	return 0;
}

static int
set_ssid_wps(struct wpa_ssid *new, const char *ssid, const char *pin, int wps_mode)
{
	char str[64];

#ifdef EAP_WSC
	new->use_wps = wps_mode;
#endif
	
	if(wps_mode == 2) { /* PIN Config method selected */
		DE_SNPRINTF(str, sizeof(str), "\"%s\"", pin);
		if(wpa_config_set(new, "password", str, 1) != 0) {
			wpa_config_free_ssid(new);
			return -1;
		}
	}

	DE_SNPRINTF(str, sizeof(str), "\"%s\"", ssid);
	if(wpa_config_set(new, "ssid", str, 1) != 0) {
		return -1;
	}

	if(wpa_config_set(new, "eap", "WSC", 1) != 0) {
		wpa_config_free_ssid(new);
		return -1;
	}
	DE_SNPRINTF(str, sizeof(str), "\"%s\"", "WFA-SimpleConfig-Enrollee-1-0");
	if(wpa_config_set(new, "identity", str, 1) != 0) {
		wpa_config_free_ssid(new);
		return -1;
	}
	new->key_mgmt = WPA_KEY_MGMT_IEEE8021X_NO_WPA;
	return 0;
}


#if defined (__embos__) || defined (__rtke__) || defined (__mqx__) || defined(__linux__)
#define fgets _wpa_fgets
static char* fgets(char *str, int size, de_file_ref_t stream)
{
	char *p = str;
	while(p < str + size - 1 && de_fread(stream, p, 1) > 0) {
		p++;
		if(p[-1] == '\n') {
			*p++ = '\0';
			return str;
		}
	}
	if(p > str) {
		*p = '\0';
		return str;
	}
	return NULL;
}

/************************************************************/
/* code from config_file.c */
/************************************************************/


/**
 * wpa_config_get_line - Read the next configuration file line
 * @s: Buffer for the line
 * @size: The buffer length
 * @stream: File stream to read from
 * @line: Pointer to a variable storing the file line number
 * @_pos: Buffer for the pointer to the beginning of data on the text line or
 * %NULL if not needed (returned value used instead)
 * Returns: Pointer to the beginning of data on the text line or %NULL if no
 * more text lines are available.
 *
 * This function reads the next non-empty line from the configuration file and
 * removes comments. The returned string is guaranteed to be null-terminated.
 */
static char * wpa_config_get_line(char *s, int size, de_file_ref_t stream, int *line,
				  char **_pos)
{
	char *pos, *end, *sstart;

	while (fgets(s, size, stream)) {
		(*line)++;
		s[size - 1] = '\0';
		pos = s;

		/* Skip white space from the beginning of line. */
		while (*pos == ' ' || *pos == '\t' || *pos == '\r')
			pos++;

		/* Skip comment lines and empty lines */
		if (*pos == '#' || *pos == '\n' || *pos == '\0')
			continue;

		/*
		 * Remove # comments unless they are within a double quoted
		 * string.
		 */
		sstart = os_strchr(pos, '"');
		if (sstart)
			sstart = os_strrchr(sstart + 1, '"');
		if (!sstart)
			sstart = pos;
		end = os_strchr(sstart, '#');
		if (end)
			*end-- = '\0';
		else
			end = pos + os_strlen(pos) - 1;

		/* Remove trailing white space. */
		while (end > pos &&
		       (*end == '\n' || *end == ' ' || *end == '\t' ||
			*end == '\r'))
			*end-- = '\0';

		if (*pos == '\0')
			continue;

		if (_pos)
			*_pos = pos;
		return pos;
	}

	if (_pos)
		*_pos = NULL;
	return NULL;
}


static int wpa_config_validate_network(struct wpa_ssid *ssid, int line)
{
	int errors = 0;

	if (ssid->passphrase) {
		if (ssid->psk_set) {
			wpa_printf(MSG_ERROR, "Line %d: both PSK and "
				   "passphrase configured.", line);
			errors++;
		}
		wpa_config_update_psk(ssid);
	}

	if ((ssid->key_mgmt & WPA_KEY_MGMT_PSK) && !ssid->psk_set) {
		wpa_printf(MSG_ERROR, "Line %d: WPA-PSK accepted for key "
			   "management, but no PSK configured.", line);
		errors++;
	}

	if ((ssid->group_cipher & WPA_CIPHER_CCMP) &&
	    !(ssid->pairwise_cipher & WPA_CIPHER_CCMP) &&
	    !(ssid->pairwise_cipher & WPA_CIPHER_NONE)) {
		/* Group cipher cannot be stronger than the pairwise cipher. */
		wpa_printf(MSG_DEBUG, "Line %d: removed CCMP from group cipher"
			   " list since it was not allowed for pairwise "
			   "cipher", line);
		ssid->group_cipher &= ~WPA_CIPHER_CCMP;
	}

	return errors;
}


static struct wpa_ssid * wpa_config_read_network(de_file_ref_t f, int *line, int id)
{
	struct wpa_ssid *ssid;
	int errors = 0, end = 0;
	char buf[256], *pos, *pos2;

	wpa_printf(MSG_MSGDUMP, "Line: %d - start of a new network block",
		   *line);
	ssid = os_zalloc(sizeof(*ssid));
	if (ssid == NULL)
		return NULL;
	ssid->id = id;

	wpa_config_set_network_defaults(ssid);

	while (wpa_config_get_line(buf, sizeof(buf), f, line, &pos)) {
		if (os_strcmp(pos, "}") == 0) {
			end = 1;
			break;
		}

		pos2 = os_strchr(pos, '=');
		if (pos2 == NULL) {
			wpa_printf(MSG_ERROR, "Line %d: Invalid SSID line "
				   "'%s'.", *line, pos);
			errors++;
			continue;
		}

		*pos2++ = '\0';
		if (*pos2 == '"') {
			if (os_strchr(pos2 + 1, '"') == NULL) {
				wpa_printf(MSG_ERROR, "Line %d: invalid "
					   "quotation '%s'.", *line, pos2);
				errors++;
				continue;
			}
		}

		if (wpa_config_set(ssid, pos, pos2, *line) < 0)
			errors++;
	}

	if (!end) {
		wpa_printf(MSG_ERROR, "Line %d: network block was not "
			   "terminated properly.", *line);
		errors++;
	}

#ifdef EAP_WSC
	if(!ssid->use_wps)
#endif
		errors += wpa_config_validate_network(ssid, *line);

	if (errors) {
		wpa_config_free_ssid(ssid);
		ssid = NULL;
	}

	return ssid;
}


static struct wpa_config_blob * wpa_config_read_blob(de_file_ref_t f, int *line,
						     const char *name)
{
	struct wpa_config_blob *blob;
	char buf[256], *pos;
	unsigned char *encoded = NULL, *nencoded;
	int end = 0;
	size_t encoded_len = 0, len;

	wpa_printf(MSG_MSGDUMP, "Line: %d - start of a new named blob '%s'",
		   *line, name);

	while (wpa_config_get_line(buf, sizeof(buf), f, line, &pos)) {
		if (os_strcmp(pos, "}") == 0) {
			end = 1;
			break;
		}

		len = os_strlen(pos);
		nencoded = os_realloc(encoded, encoded_len + len);
		if (nencoded == NULL) {
			wpa_printf(MSG_ERROR, "Line %d: not enough memory for "
				   "blob", *line);
			os_free(encoded);
			return NULL;
		}
		encoded = nencoded;
		os_memcpy(encoded + encoded_len, pos, len);
		encoded_len += len;
	}

	if (!end) {
		wpa_printf(MSG_ERROR, "Line %d: blob was not terminated "
			   "properly", *line);
		os_free(encoded);
		return NULL;
	}

	blob = os_zalloc(sizeof(*blob));
	if (blob == NULL) {
		os_free(encoded);
		return NULL;
	}
	blob->name = os_strdup(name);
	blob->data = base64_decode(encoded, encoded_len, &blob->len);
	os_free(encoded);

	if (blob->name == NULL || blob->data == NULL) {
		wpa_config_free_blob(blob);
		return NULL;
	}

	return blob;
}


static int wpa_config_read_file(struct wpa_config *config,
				const char *name)
{
	de_file_ref_t f;
	char buf[256], *pos;
	int errors = 0, line = 0;
	struct wpa_ssid *ssid, *tail = NULL, *head = NULL;
	int id = 0;

	f = de_fopen(name, DE_FRDONLY);
	if (!de_f_is_open(f))
	{
		wpa_printf(MSG_DEBUG, "Failed to open '%s'", name);
		return -1;
	}
	wpa_printf(MSG_INFO, "Reading configuration file '%s'", name);

	while (wpa_config_get_line(buf, sizeof(buf), f, &line, &pos)) {
		if (os_strcmp(pos, "network={") == 0) {
			ssid = wpa_config_read_network(f, &line, id++);
			if (ssid == NULL) {
				wpa_printf(MSG_ERROR, "Line %d: failed to "
					   "parse network block.", line);
				errors++;
				continue;
			}
			if (head == NULL) {
				head = tail = ssid;
			} else {
				tail->next = ssid;
				tail = ssid;
			}
			if (wpa_config_add_prio_network(config, ssid)) {
				wpa_printf(MSG_ERROR, "Line %d: failed to add "
					   "network block to priority list.",
					   line);
				errors++;
				continue;
			}
		} else if (os_strncmp(pos, "blob-base64-", 12) == 0) {
			char *bname = pos + 12, *name_end;
			struct wpa_config_blob *blob;

			name_end = os_strchr(bname, '=');
			if (name_end == NULL) {
				wpa_printf(MSG_ERROR, "Line %d: no blob name "
					   "terminator", line);
				errors++;
				continue;
			}
			*name_end = '\0';

			blob = wpa_config_read_blob(f, &line, bname);
			if (blob == NULL) {
				wpa_printf(MSG_ERROR, "Line %d: failed to read"
					   " blob %s", line, bname);
				errors++;
				continue;
			}
			wpa_config_set_blob(config, blob);
#ifdef CONFIG_CTRL_IFACE
		} else if (os_strncmp(pos, "ctrl_interface=", 15) == 0) {
			os_free(config->ctrl_interface);
			config->ctrl_interface = os_strdup(pos + 15);
			wpa_printf(MSG_DEBUG, "ctrl_interface='%s'",
				   config->ctrl_interface);
		} else if (os_strncmp(pos, "ctrl_interface_group=", 21) == 0) {
			os_free(config->ctrl_interface_group);
			config->ctrl_interface_group = os_strdup(pos + 21);
			wpa_printf(MSG_DEBUG, "ctrl_interface_group='%s' "
				   "(DEPRECATED)",
				   config->ctrl_interface_group);
#endif /* CONFIG_CTRL_IFACE */
		} else if (os_strncmp(pos, "eapol_version=", 14) == 0) {
			config->eapol_version = atoi(pos + 14);
			if (config->eapol_version < 1 ||
			    config->eapol_version > 2) {
				wpa_printf(MSG_ERROR, "Line %d: Invalid EAPOL "
					   "version (%d): '%s'.",
					   line, config->eapol_version, pos);
				errors++;
				continue;
			}
			wpa_printf(MSG_DEBUG, "eapol_version=%d",
				   config->eapol_version);
		} else if (os_strncmp(pos, "ap_scan=", 8) == 0) {
			config->ap_scan = atoi(pos + 8);
			wpa_printf(MSG_DEBUG, "ap_scan=%d", config->ap_scan);
		} else if (os_strncmp(pos, "fast_reauth=", 12) == 0) {
			config->fast_reauth = atoi(pos + 12);
			wpa_printf(MSG_DEBUG, "fast_reauth=%d",
				   config->fast_reauth);
		} else if (os_strncmp(pos, "opensc_engine_path=", 19) == 0) {
			os_free(config->opensc_engine_path);
			config->opensc_engine_path = os_strdup(pos + 19);
			wpa_printf(MSG_DEBUG, "opensc_engine_path='%s'",
				   config->opensc_engine_path);
		} else if (os_strncmp(pos, "pkcs11_engine_path=", 19) == 0) {
			os_free(config->pkcs11_engine_path);
			config->pkcs11_engine_path = os_strdup(pos + 19);
			wpa_printf(MSG_DEBUG, "pkcs11_engine_path='%s'",
				   config->pkcs11_engine_path);
		} else if (os_strncmp(pos, "pkcs11_module_path=", 19) == 0) {
			os_free(config->pkcs11_module_path);
			config->pkcs11_module_path = os_strdup(pos + 19);
			wpa_printf(MSG_DEBUG, "pkcs11_module_path='%s'",
				   config->pkcs11_module_path);
		} else if (os_strncmp(pos, "driver_param=", 13) == 0) {
			os_free(config->driver_param);
			config->driver_param = os_strdup(pos + 13);
			wpa_printf(MSG_DEBUG, "driver_param='%s'",
				   config->driver_param);
		} else if (os_strncmp(pos, "dot11RSNAConfigPMKLifetime=", 27)
			   == 0) {
			config->dot11RSNAConfigPMKLifetime = atoi(pos + 27);
			wpa_printf(MSG_DEBUG, "dot11RSNAConfigPMKLifetime=%d",
				   config->dot11RSNAConfigPMKLifetime);
		} else if (os_strncmp(pos,
				      "dot11RSNAConfigPMKReauthThreshold=", 34)
			   == 0) {
			config->dot11RSNAConfigPMKReauthThreshold =
				atoi(pos + 34);
			wpa_printf(MSG_DEBUG,
				   "dot11RSNAConfigPMKReauthThreshold=%d",
				   config->dot11RSNAConfigPMKReauthThreshold);
		} else if (os_strncmp(pos, "dot11RSNAConfigSATimeout=", 25) ==
			   0) {
			config->dot11RSNAConfigSATimeout = atoi(pos + 25);
			wpa_printf(MSG_DEBUG, "dot11RSNAConfigSATimeout=%d",
				   config->dot11RSNAConfigSATimeout);
		} else if (os_strncmp(pos, "update_config=", 14) == 0) {
			config->update_config = atoi(pos + 14);
			wpa_printf(MSG_DEBUG, "update_config=%d",
				   config->update_config);
		} else if (os_strncmp(pos, "load_dynamic_eap=", 17) == 0) {
			char *so = pos + 17;
			int ret;
			wpa_printf(MSG_DEBUG, "load_dynamic_eap=%s", so);
			ret = eap_peer_method_load(so);
			if (ret == -2) {
				wpa_printf(MSG_DEBUG, "This EAP type was "
					   "already loaded - not reloading.");
			} else if (ret) {
				wpa_printf(MSG_ERROR, "Line %d: Failed to "
					   "load dynamic EAP method '%s'.",
					   line, so);
				errors++;
			}
		} else {
			wpa_printf(MSG_ERROR, "Line %d: Invalid configuration "
				   "line '%s'.", line, pos);
			errors++;
			continue;
		}
	}

	de_fclose(f);

	config->ssid = head;

	return 0;
}
#endif /* __embos__ */

#if defined (__nucleus__) || defined (__embos__)
#define wpa_config_read_file(a,b) ((int)-1)
#endif

static int dummy_id = -1;
struct wpa_config *config;
static char mmi_ssid[33] = "dummy_ssid";
static char mmi_passphrase[65] = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

void
wpa_set_ssid_psk(const char *name, const char *psk)
{
	struct wpa_ssid *ssid;
	DE_STRNCPY(mmi_ssid, name, sizeof(mmi_ssid));
	mmi_ssid[sizeof(mmi_ssid) - 1] = '\0';
	DE_STRNCPY(mmi_passphrase, psk, sizeof(mmi_passphrase));
	mmi_passphrase[sizeof(mmi_passphrase) - 1] = '\0';
	ssid = wpa_config_get_network(config, dummy_id);
	if(ssid != NULL) {
		wpa_config_set_network_defaults(ssid);
		set_ssid_psk(ssid, mmi_ssid, mmi_passphrase);
		
		/* This is a bit ugly, but there is no simple way to
		 * get the PSK from the configuration entry, so we
		 * might just as well use the raw fields directly.
		 */
		if (ssid->psk_set) {
			wpa_snprintf_hex(mmi_passphrase, sizeof(mmi_passphrase),
					 ssid->psk, 32);
		}
	}
}

#ifdef USE_WPS
#ifdef EAP_WSC
void
wps_set_ssid_credentials(struct wpa_ssid *wps_ssid)
{
	config->ssid->use_wps = 0;
	if(!wps_ssid->psk_set)
		wpa_config_update_psk(wps_ssid);
}


void
wps_set_ssid(const char *pin, char *ssid_str, int wps_mode)
{
	/*
	 * Use tpin only for WPS WiFi Certification if 
	 * PIN Code method is not available through MMI
	 */

	/* char tpin[8] = {0x31, 0x32, 0x31, 0x32, 0x31, 0x32, 0x31, 0x32}; */

	switch(wps_mode) {
	case 1: /* PBC Method */
		config->ssid->use_wps = 1;
		set_ssid_wps(config->ssid, ssid_str, pin, wps_mode);
		break;
	case 2: /* PIN Config Method */
		DE_ASSERT( DE_STRLEN(pin) > 0 );
                config->ssid->use_wps = 2;
                set_ssid_wps(config->ssid, ssid_str, pin, wps_mode);
		break;
	case 3: /* WPS not used */
		config->ssid->use_wps = 0;

                /* not sure if this is needed */
                config->ssid->id = 0;
                wpa_config_set_network_defaults(config->ssid);

		break;
	default:
		config->ssid->use_wps = 2;
	}
}

int
wps_enabled(void)
{
	wpa_printf(MSG_INFO, "wps_enabled: %d\n", config->ssid->use_wps);
	return config->ssid->use_wps;
}
#endif /* USE_WPS */
#endif // EAP_WSC


/* Enterprise mode : Eap Mode */
typedef enum
{
	X_EAP_SIM = 0,
	X_EAP_AKA,
	X_EAP_TTLS_PAP,			/* Tunnel PAP through TTLS */
	X_EAP_TTLS_CHAP,			/* Tunnel CHAP through TTLS */
	X_EAP_TTLS_MD5,			/* Tunnel MD5 through TTLS */
	X_EAP_TTLS_MSCHAP, 		/* Tunnel MSCHAP through TTLS */
	X_EAP_TTLS_MSCHAPV2,		/* Tunnel MSCHAPV2 through TTLS */
	X_EAP_PEAP_MSCHAPV2,		/* Tunnel MSCHAPV2 through PEAP */
	X_EAP_PEAP_GTC,			/* Tunnel GTC through PEAP */
	X_EAP_LEAP,
	X_EAP_FAST_MSCHAPV2,
	X_EAP_TLS
}t_EapMode;

static unsigned char* eap_phase2_to_str(t_EapMode mode)
{
	switch (mode) {
		case X_EAP_TTLS_PAP:
			return "auth=PAP";
		case X_EAP_TTLS_CHAP:
			return "auth=CHAP";
		case X_EAP_TTLS_MD5:
			return "auth=MD5";
		case X_EAP_TTLS_MSCHAP:
			return "auth=MSCHAP";
		case X_EAP_TTLS_MSCHAPV2:
			return "auth=MSCHAPV2";
		case X_EAP_PEAP_MSCHAPV2:
			return "auth=MSCHAPV2";
		case X_EAP_PEAP_GTC:
			return "auth=GTC";
		default:
			return 0;
	}
}
static int
wpa_set_network_param(struct wpa_ssid *wpa_conf, const struct wpa_param *wp)
{
   char s[128];

#define WCS(K, V) if(wpa_config_set(wpa_conf, (K), (V), 1) != 0) return -1

   switch(wp->auth_mode) {
      case WPA_PSK:
         WCS("key_mgmt", "WPA-PSK");
         if(os_strlen(wp->u.key) == 64) {
            if(wpa_config_set(wpa_conf, "psk", wp->u.key, 1) != 0) {
               return -1;
            }
         } else {
		 /* if this is identical to the previous net we should
		  * use saved psk to speed things up */
            os_snprintf(s, sizeof(s), "\"%s\"", wp->u.key);
            if(wpa_config_set(wpa_conf, "psk", s, 1) != 0) {
               return -1;
            }
            wpa_config_update_psk(wpa_conf);
         }
         break;
      case WAPI_PSK:
         WCS("key_mgmt", "WAPI-PSK");
         /* not sure if needed */
         WCS("proto", "WAPI");
         WCS("group", "SMS4");
         WCS("pairwise", "SMS4");
         if(os_strlen(wp->u.key) == 64) {
            if(wpa_config_set(wpa_conf, "psk", wp->u.key, 1) != 0) {
               return -1;
            }
         } else {
            /* if this is identical to the previous net we should
             * use saved psk to speed things up */
            os_snprintf(s, sizeof(s), "\"%s\"", wp->u.key);
            if(wpa_config_set(wpa_conf, "psk", s, 1) != 0) {
               return -1;
            }
            wpa_config_update_psk(wpa_conf);
         }
         break;
   case WAPI_CERT:
         WCS("key_mgmt", "WAPI-CERT");
         /* not sure if needed */
         WCS("proto", "WAPI");
         WCS("group", "SMS4");
         WCS("pairwise", "SMS4");
#if 0 /* Use wpa config blobs to store the certificates */
			res = wpa_config_find_and_add_certificate("ca-cert", wp->u.wapi_cert.ca_certs , sizeof( wp->u.wapi_cert.ca_certs));
			res = wpa_config_find_and_add_certificate("client-cert", wp->u.wapi_cert.client_cert , sizeof( wp->u.wapi_cert.client_cert));
			res = wpa_config_find_and_add_pkey("client-key", wp->u.wapi_cert.client_cert, sizeof(wp->u.wapi_cert.client_cert));
			if(!res) {
				wpa_printf(MSG_ERROR, "error parsing wapi certificates, res: %d", res);
				return -1;
			}

         os_snprintf(s, sizeof(s), "\"%s\"", wp->u.wapi_cert.ca_certs);
			WCS("ca_cert", "\"blob://ca-cert\"");
        
	   os_snprintf(s, sizeof(s), "\"%s\"", wp->u.wapi_cert.client_cert);
			WCS("client_cert", "\"blob://client-key\"");
	 
	   os_snprintf(s, sizeof(s), "\"%s\"", wp->u.wapi_cert.private_key);
			WCS("private_key", "\"blob://client-key\"");
#endif	   
			wapi_cert.cert_info.config.used_cert = 1; /* 1: x509 Certificate */
			if((wp->u.wapi_param_cert.ca_certs == NULL) || (wp->u.wapi_param_cert.client_cert == NULL))
			{
				wpa_printf(MSG_ERROR, "error getting wapi certificates!");
				return -1;
			}
			os_memcpy(wapi_cert.asu, wp->u.wapi_param_cert.ca_certs, strlen(wp->u.wapi_param_cert.ca_certs));
			os_memcpy(wapi_cert.user, wp->u.wapi_param_cert.client_cert, strlen(wp->u.wapi_param_cert.client_cert));
         break;
      case WPA_EAP_TLS:
         WCS("key_mgmt", "WPA-EAP");
         WCS("eap", "TLS");

         os_snprintf(s, sizeof(s), "\"%s\"", wp->u.tls.identity);
         WCS("identity", s);

         os_snprintf(s, sizeof(s), "\"%s\"", wp->u.tls.ca_certs);
         WCS("ca_cert", s);

         os_snprintf(s, sizeof(s), "\"%s\"", wp->u.tls.client_cert);
         WCS("client_cert", s);

         os_snprintf(s, sizeof(s), "\"%s\"", wp->u.tls.private_key);
         WCS("private_key", s);

         os_snprintf(s, sizeof(s), "\"%s\"", wp->u.tls.private_key_password);
         WCS("private_key_passwd", s);
         break;
      case WPA_EAP_TTLS:
         WCS("key_mgmt", "WPA-EAP");
         WCS("eap", "TTLS");
         
         //WCS("phase2", "\"auth=CHAP\"");
         os_snprintf(s, sizeof(s), "\"%s\"", eap_phase2_to_str(wp->u.ttls.identity_phase2));
         WCS("phase2", s);          
		 
         /*if(wp->u.ttls.identity_phase1 != NULL) {
            os_snprintf(s, sizeof(s), "\"%s\"", wp->u.ttls.identity_phase1);
            WCS("anonymous_identity", s);
         }*/

         os_snprintf(s, sizeof(s), "\"%s\"", wp->u.ttls.identity_phase1);
         WCS("identity", s);
         
         os_snprintf(s, sizeof(s), "\"%s\"", wp->u.ttls.password);
         WCS("password", s);

         //os_snprintf(s, sizeof(s), "\"%s\"", wp->u.ttls.ca_certs);
         //WCS("ca_cert", s);
         break;
      case WPA_EAP_PEAP:
         WCS("key_mgmt", "WPA-EAP");
         WCS("eap", "PEAP");
         //WCS("phase2", "\"auth=MSCHAPV2\"");
         os_snprintf(s, sizeof(s), "\"%s\"", eap_phase2_to_str(wp->u.peap.identity_phase2));
         WCS("phase2", s); 

         /*if(wp->u.peap.identity_phase1 != NULL) {
            os_snprintf(s, sizeof(s), "\"%s\"", wp->u.peap.identity_phase1);
            WCS("anonymous_identity", s);
         }*/

         os_snprintf(s, sizeof(s), "\"%s\"", wp->u.peap.identity_phase1);
         WCS("identity", s);

         os_snprintf(s, sizeof(s), "\"%s\"", wp->u.peap.password);
         WCS("password", s);

         //os_snprintf(s, sizeof(s), "\"%s\"", wp->u.peap.ca_certs);
         //WCS("ca_cert", s);
         break;
      case WPA_EAP_SIM:
         WCS("key_mgmt", "WPA-EAP");
         WCS("eap", "SIM");
         WCS("pin", "\"1234\"");
         WCS("pcsc", "\"\"");
         break;
      case WPA_EAP_AKA:
         WCS("key_mgmt", "WPA-EAP");
         WCS("eap", "AKA");
         WCS("pin", "\"1234\"");
         WCS("pcsc", "\"\"");
         break;
      default:
         return -1; /* unknown method */
   }
   return 0;
}

int
wpa_new_network(const char *ssid, const struct wpa_param *wp)
{
    struct wpa_ssid *wpa_conf;
    char s[128];

    /*
        Check Bug 1861 to see why we are not using this method
        to add new networks
        
        wpa_conf = wpa_config_add_network(config);
        if(wpa_conf == NULL)
            return -1;
    */
    
    wpa_conf = wpa_config_get_network(config, dummy_id);
	if(wpa_conf != NULL) {
        wpa_config_set_network_defaults(wpa_conf);

        os_snprintf(s, sizeof(s), "\"%s\"", ssid);
        if(wpa_config_set(wpa_conf, "ssid", s, 1) != 0) {
            /* DE_ASSERT(wpa_config_remove_network(config, wpa_conf->id) == 0); */
            return -1;
        }

        if(wpa_set_network_param(wpa_conf, wp) != 0) {
            /* DE_ASSERT(wpa_config_remove_network(config, wpa_conf->id) == 0); */
            return -1;
        }
    }
    return 0;
}

struct wpa_config * wpa_config_read(const char *name)
{
	int i;
	int use_wps = 0;
	const char* paths[] = {
		"wpa_supplicant.conf"
	};

	config = wpa_config_alloc_empty(NULL, NULL);
	if (config == NULL)
		return NULL;

        for (i = 0; i < DE_ARRAY_SIZE(paths); i++) {
                if( wpa_config_read_file(config, paths[i]) >= 0) 
                        // success 
                        break;
        }
	
#ifdef EAP_WSC
	use_wps = config->ssid != NULL && config->ssid->use_wps;
#endif // EAP_WSC
	if(use_wps > 0) { /* TODO: redesign the use of profiles here... */
		if(set_ssid_wps(config->ssid, "JunkAP", "1234", 1) < 0) {
			wpa_config_free_ssid(config->ssid);
			config->ssid = NULL;
		}
		dummy_id = config->ssid->id;
	} else {
		struct wpa_ssid *ssid;
		ssid = wpa_config_add_network(config);
		if(ssid != NULL) {
			wpa_config_set_network_defaults(ssid);
			if(set_ssid_psk(ssid, mmi_ssid, mmi_passphrase) < 0) {
				wpa_config_free_ssid(ssid);
				ssid = NULL;
                config->ssid = NULL;                
			} else {
				dummy_id = ssid->id;
#if 0
				/* this makes it possible to use
				 * EAP-SIM without a special config
				 * file, but has the negative effect
				 * that it sends the IMSI to anyone
				 * requesting EAP-Identity
				 */
#if defined(EAP_SIM) || defined(EAP_AKA)
				wpa_config_set(ssid, "eap", 
#if defined(EAP_SIM)
					       "SIM "
#endif
#if 0 && defined(EAP_AKA)
					       "AKA "
#endif
					       ,1);
				wpa_config_set(ssid, "pin", "\"1234\"", 1);
				wpa_config_set(ssid, "pcsc", "\"\"", 1);
#endif
#endif
			}
		}
	}

	config->ap_scan = 0;
	wpa_config_debug_dump_networks(config);
	
	return config;
}


int wpa_config_write(const char *name, struct wpa_config *config)
{
	struct wpa_ssid *ssid;
	struct wpa_config_blob *blob;

	wpa_printf(MSG_DEBUG, "Writing configuration file '%s'", name);

	/* TODO: write global config parameters */


	for (ssid = config->ssid; ssid; ssid = ssid->next) {
		/* TODO: write networks */
	}

	for (blob = config->blobs; blob; blob = blob->next) {
		/* TODO: write blobs */
	}

	return 0;
}

/* QUICK AND DIRTY */

static const unsigned char* findmark_mem(const unsigned char* src, int lsrc, const  char* mark, int lmark)
{
	const unsigned char* p = src;
	const unsigned char* pe = src+lsrc;
	if (NULL==src || NULL==mark || lsrc<0 || lmark<0 || lsrc<lmark)
	{
		return NULL;
	}
	pe -= lmark;
	for (; p<=pe; p++)
	{
		if (0 == os_memcmp(p, mark, lmark))
		{
			return p;
		}
	}
	return NULL;
}

static const unsigned char* findstrmark_mem(const unsigned char* src, int lsrc, const char* mark)
{
  return findmark_mem(src, lsrc, mark, strlen(mark));
}



static struct wpa_config_blob*
find_and_decode_base64(
      const char* name,
      const void *src, int src_len,
      const char* mark_s,
      const char* mark_e,
      int *err)
{
   struct wpa_config_blob *blob;
   const unsigned char *encoded = NULL;
   const unsigned char *p = NULL;
   int encoded_len;

   p = findstrmark_mem(src, src_len, mark_s);
   if(p == NULL)
   {
      *err = __LINE__;
      return NULL;
   }

   p += os_strlen(mark_s); // skip mark
   encoded = p;

   p = findstrmark_mem(src, src_len, mark_e);
   if(p == NULL)
   {
      *err = __LINE__;
      return NULL;
   }

   encoded_len = (p - encoded);

   wpa_hexdump_ascii(MSG_DEBUG, "base64 data:", encoded, encoded_len);

   blob = os_zalloc(sizeof(*blob));
   if (blob == NULL)
   {
      *err = __LINE__;
      return NULL;
   }
   blob->name = os_strdup(name);
   blob->data = base64_decode(encoded, encoded_len, &blob->len);

   if (blob->data == NULL)
   {
      wpa_hexdump_ascii(MSG_ERROR, "base64 data:", encoded, encoded_len);
      *err = __LINE__;
      os_free(blob->name);
      os_free(blob);
      return NULL;
   }

   return blob;
}

int wpa_config_find_and_add_certificate(const char *name, const void *src, int src_len)
{
   const char* mark_cert_s =  "-----BEGIN CERTIFICATE-----";
   const char* mark_cert_e = "-----END CERTIFICATE-----";
   struct wpa_config_blob *blob = NULL;
   int err;

   if(config==NULL)
      return -__LINE__;

   wpa_hexdump_ascii(MSG_ERROR, "src:", src, src_len);

   blob = find_and_decode_base64(name, src, src_len, mark_cert_s, mark_cert_e, &err);
   if(blob == NULL)
   {
      wpa_printf(MSG_ERROR, "failed to find or decode %s %s returned %d",
            mark_cert_s, mark_cert_e, err);
      return -__LINE__;
   }

   wpa_config_set_blob(config, blob);

   return 0;
}

int wpa_config_find_and_add_pkey(const char *name, const void *src, int src_len)
{
   const char* mark_pkey_s = "-----BEGIN EC PRIVATE KEY-----";
   const char* mark_pkey_e = "-----END EC PRIVATE KEY-----";
   struct wpa_config_blob *blob = NULL;
   int err;

   if(config==NULL)
      return -__LINE__;

   wpa_hexdump_ascii(MSG_ERROR, "src:", src, src_len);

   blob = find_and_decode_base64(name, src, src_len, mark_pkey_s, mark_pkey_e, &err);
   if(blob == NULL)
   {
      wpa_printf(MSG_ERROR, "failed to find or decode %s %s returned %d", 
            mark_pkey_s, mark_pkey_e, err);

      return -__LINE__;
   }

   wpa_config_set_blob(config, blob);

   return 0;
}

/* Local Variables: */
/* c-basic-offset: 8 */
/* indent-tabs-mode: t */
/* End: */
