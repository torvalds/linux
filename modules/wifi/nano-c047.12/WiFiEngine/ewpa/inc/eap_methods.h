/*
 * EAP peer: Method registration
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

#ifndef EAP_METHODS_H
#define EAP_METHODS_H

#include "eap_defs.h"

const struct eap_method * eap_sm_get_eap_methods(int vendor, EapType method);
const struct eap_method * eap_peer_get_methods(size_t *count);

struct eap_method * eap_peer_method_alloc(int version, int vendor,
					  EapType method, const char *name);
void eap_peer_method_free(struct eap_method *method);
int eap_peer_method_register(struct eap_method *method);


#ifdef IEEE8021X_EAPOL

EapType eap_get_type(const char *name, int *vendor);
const char * eap_get_name(int vendor, EapType type);
size_t eap_get_names(char *buf, size_t buflen);
char ** eap_get_names_as_string_array(size_t *num);
int eap_peer_register_methods(void);
void eap_peer_unregister_methods(void);

#else /* IEEE8021X_EAPOL */

static inline EapType eap_get_type(const char *name, int *vendor)
{
	*vendor = EAP_VENDOR_IETF;
	return EAP_TYPE_NONE;
}

static inline const char * eap_get_name(int vendor, EapType type)
{
	return NULL;
}

static inline size_t eap_get_names(char *buf, size_t buflen)
{
	return 0;
}

static inline int eap_peer_register_methods(void)
{
	return 0;
}

static inline void eap_peer_unregister_methods(void)
{
}

#endif /* IEEE8021X_EAPOL */


#ifdef CONFIG_DYNAMIC_EAP_METHODS

int eap_peer_method_load(const char *so);
int eap_peer_method_unload(struct eap_method *method);

#else /* CONFIG_DYNAMIC_EAP_METHODS */

static inline int eap_peer_method_load(const char *so)
{
	return 0;
}

static inline int eap_peer_method_unload(struct eap_method *method)
{
	return 0;
}

#endif /* CONFIG_DYNAMIC_EAP_METHODS */

#endif /* EAP_METHODS_H */
