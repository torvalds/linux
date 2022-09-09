/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __QCOM_PDR_HELPER__
#define __QCOM_PDR_HELPER__

#include <linux/soc/qcom/qmi.h>

#define SERVREG_NAME_LENGTH	64

struct pdr_service;
struct pdr_handle;

enum servreg_service_state {
	SERVREG_LOCATOR_ERR = 0x1,
	SERVREG_SERVICE_STATE_DOWN = 0x0FFFFFFF,
	SERVREG_SERVICE_STATE_UP = 0x1FFFFFFF,
	SERVREG_SERVICE_STATE_EARLY_DOWN = 0x2FFFFFFF,
	SERVREG_SERVICE_STATE_UNINIT = 0x7FFFFFFF,
};

struct pdr_handle *pdr_handle_alloc(void (*status)(int state,
						   char *service_path,
						   void *priv), void *priv);
struct pdr_service *pdr_add_lookup(struct pdr_handle *pdr,
				   const char *service_name,
				   const char *service_path);
int pdr_restart_pd(struct pdr_handle *pdr, struct pdr_service *pds);
void pdr_handle_release(struct pdr_handle *pdr);

#endif
