/*
 * WPA Supplicant / Example program entrypoint
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
 */

#include "includes.h"

#include "common.h"
#include "wpa_supplicant.h"
#include "wpa_supplicant_i.h"
#include "pcsc_funcs.h"

#include "sim_access.h"

typedef enum {
	SC_IDLE,
	SC_RUNNING,
	SC_DONE,
	SC_ERROR
} state_TYPE;

typedef enum {
	AKA_OK,
	AKA_ERR_AUTH,
	AKA_ERR_SYNC,
	AKA_ERR_OTHER
} aka_status_TYPE;

struct scard_data {
	state_TYPE state;

	/* EAP-SIM */
	char sres[4];
	char kc[8];

	/* EAP-AKA */
	char res[16];
	size_t res_len;
	char autn[16];
	char ik[16];     /* integrity key */
	char ck[16];     /* confidentiality key */
	
	aka_status_TYPE aka_status;
	char auts[14];   /* */
};

static struct scard_data scard_data;

static void
scard_gsm_alg_resp(const void *sres, size_t sres_len,
		   const void *kc, size_t kc_len)
{
	struct scard_data *scard = &scard_data;
	DE_ASSERT(scard->state == SC_RUNNING);
	DE_ASSERT(sres_len == sizeof(scard->sres));
	DE_ASSERT(kc_len == sizeof(scard->kc));
	DBG_PRINTBUF("SRES", sres, sres_len);
	DBG_PRINTBUF("Kc", kc, kc_len);
	memcpy(scard->sres, sres, sres_len);
	memcpy(scard->kc, kc, kc_len);
	scard->state = SC_DONE;
}

static void
scard_umts_alg_resp(const void *res, size_t res_len,
		    const void *ck, size_t ck_len,
		    const void *ik, size_t ik_len)
{
	struct scard_data *scard = &scard_data;
	DE_ASSERT(scard->state == SC_RUNNING);

	DBG_PRINTBUF("RES", res, res_len);
	DBG_PRINTBUF("Ck", ck, ck_len);
	DBG_PRINTBUF("Ik", ik, ik_len);
	DE_ASSERT(res_len <= sizeof(scard->res));
	DE_ASSERT(ck_len == sizeof(scard->ck));
	DE_ASSERT(ik_len == sizeof(scard->ik));
	memcpy(scard->res, res, res_len);
	scard->res_len = res_len;
	memcpy(scard->ck, ck, ck_len);
	memcpy(scard->ik, ik, ik_len);
	memset(scard->auts, 0, sizeof(scard->auts));
	scard->aka_status = AKA_OK;

	scard->state = SC_DONE;
}

static void
scard_umts_alg_err(const void *auts, size_t auts_len, int status)
{
	struct scard_data *scard = &scard_data;
	DE_ASSERT(scard->state == SC_RUNNING);
	DE_TRACE_INT(TR_WPA, "status = %d\n", status);
	DBG_PRINTBUF("AUTS", auts, auts_len);

	DE_ASSERT(auts_len == sizeof(scard->auts));
	memset(scard->res, 0, sizeof(scard->res));
	memset(scard->ck, 0, sizeof(scard->ck));
	memset(scard->ik, 0, sizeof(scard->ik));
	memcpy(scard->auts, auts, auts_len);
	if(status == SIM_ERR_SYNC)
		scard->aka_status = AKA_ERR_SYNC;
	else if(status == SIM_ERR_AUTH)
		scard->aka_status = AKA_ERR_AUTH;
	else
		scard->aka_status = AKA_ERR_OTHER;
	scard->state = SC_DONE;
}

/**
 * scard_init - Initialize SIM/USIM connection using PC/SC
 * @sim_type: Allowed SIM types (SIM, USIM, or both)
 * Returns: Pointer to private data structure, or %NULL on failure
 *
 * This function is used to initialize SIM/USIM connection. PC/SC is used to
 * open connection to the SIM/USIM card and the card is verified to support the
 * selected sim_type. In addition, local flag is set if a PIN is needed to
 * access some of the card functions. Once the connection is not needed
 * anymore, scard_deinit() can be used to close it.
 */
struct scard_data * scard_init(scard_sim_type sim_type)
{
	struct scard_data *scard = NULL;

	nr_rtke_sim_register_gsm_alg_callback(scard_gsm_alg_resp);
	nr_rtke_sim_register_umts_alg_callback(scard_umts_alg_resp, 
					       scard_umts_alg_err);
	switch(sim_type) {
	case SCARD_GSM_SIM_ONLY:
	case SCARD_TRY_BOTH:
		scard = &scard_data;
		if(scard != NULL)
			scard->state = SC_IDLE;
		break;
	default:
		break;
	}

	return scard;
}

/**
 * scard_deinit - Deinitialize SIM/USIM connection
 * @scard: Pointer to private data from scard_init()
 *
 * This function closes the SIM/USIM connect opened with scard_init().
 */
void scard_deinit(struct scard_data *scard)
{
}

/**
 * scard_set_pin - Set PIN (CHV1/PIN1) code for accessing SIM/USIM commands
 * @scard: Pointer to private data from scard_init()
 * pin: PIN code as an ASCII string (e.g., "1234")
 * Returns: 0 on success, -1 on failure
 */
int scard_set_pin(struct scard_data *scard, const char *pin)
{
	return 0;
}

/**
 * scard_get_imsi - Read IMSI from SIM/USIM card
 * @scard: Pointer to private data from scard_init()
 * @imsi: Buffer for IMSI
 * @len: Length of imsi buffer; set to IMSI length on success
 * Returns: 0 on success, -1 if IMSI file cannot be selected, -2 if IMSI file
 * selection returns invalid result code, -3 if parsing FSP template file fails
 * (USIM only), -4 if IMSI does not fit in the provided imsi buffer (len is set
 * to needed length), -5 if reading IMSI file fails.
 *
 * This function can be used to read IMSI from the SIM/USIM card. If the IMSI
 * file is PIN protected, scard_set_pin() must have been used to set the
 * correct PIN code before calling scard_get_imsi().
 */
int scard_get_imsi(struct scard_data *scard, char *imsi, size_t *len)
{
	char buf[100];

	nr_rtke_sim_get_imsi(buf, sizeof(buf));
	if(*len < strlen(buf)) {
		*len = strlen(buf);
		return -4;
	}
	memcpy(imsi, buf, strlen(buf));
	*len = strlen(buf);

	return 0;
}

int scard_gsm_auth_complete(void)
{
	struct scard_data *scard = &scard_data;
	return scard->state == SC_DONE;
}

/**
 * scard_gsm_auth - Run GSM authentication command on SIM card
 * @scard: Pointer to private data from scard_init()
 * @_rand: 16-byte RAND value from HLR/AuC
 * @sres: 4-byte buffer for SRES
 * @kc: 8-byte buffer for Kc
 * Returns: 0 on success, -1 if SIM/USIM connection has not been initialized,
 * -2 if authentication command execution fails, -3 if unknown response code
 * for authentication command is received, -4 if reading of response fails,
 * -5 if if response data is of unexpected length
 *
 * This function performs GSM authentication using SIM/USIM card and the
 * provided RAND value from HLR/AuC. If authentication command can be completed
 * successfully, SRES and Kc values will be written into sres and kc buffers.
 */
int scard_gsm_auth(struct scard_data *scard, const unsigned char *_rand,
		   unsigned char *sres, unsigned char *kc)
{
	switch(scard->state) {
	case SC_IDLE:
		DE_TRACE_STATIC(TR_WPA, "starting\n");
		scard->state = SC_RUNNING;
		nr_rtke_sim_run_gsm_alg(_rand);
		return 1;
	case SC_RUNNING:
		DE_TRACE_STATIC(TR_WPA, "in progress\n");
		return 1;
	case SC_DONE:
		DE_TRACE_STATIC(TR_WPA, "done\n");
		memcpy(sres, scard->sres, sizeof(scard->sres));
		memcpy(kc, scard->kc, sizeof(scard->kc));
		scard->state = SC_IDLE;
		return 0;
	case SC_ERROR:
		DE_TRACE_STATIC(TR_WPA, "error\n");	
		return -1;
	}

	return 0;
}

/**
 * scard_umts_auth - Run UMTS authentication command on USIM card
 * @scard: Pointer to private data from scard_init()
 * @_rand: 16-byte RAND value from HLR/AuC
 * @autn: 16-byte AUTN value from HLR/AuC
 * @res: 16-byte buffer for RES
 * @res_len: Variable that will be set to RES length
 * @ik: 16-byte buffer for IK
 * @ck: 16-byte buffer for CK
 * @auts: 14-byte buffer for AUTS
 * Returns: 0 on success, -1 on failure, or -2 if USIM reports synchronization
 * failure
 *
 * This function performs AKA authentication using USIM card and the provided
 * RAND and AUTN values from HLR/AuC. If authentication command can be
 * completed successfully, RES, IK, and CK values will be written into provided
 * buffers and res_len is set to length of received RES value. If USIM reports
 * synchronization failure, the received AUTS value will be written into auts
 * buffer. In this case, RES, IK, and CK are not valid.
 */
int scard_umts_auth(struct scard_data *scard, const unsigned char *_rand,
		    const unsigned char *autn,
		    unsigned char *res, size_t *res_len,
		    unsigned char *ik, unsigned char *ck, unsigned char *auts)
{
	switch(scard->state) {
	case SC_IDLE:
		DE_TRACE_STATIC(TR_WPA, "starting\n");
		scard->state = SC_RUNNING;
		nr_rtke_sim_run_umts_alg(_rand, autn);
		return -99;
	case SC_RUNNING:
		DE_TRACE_STATIC(TR_WPA, "in progress\n");
		return -99;
	case SC_DONE:
		DE_TRACE_STATIC(TR_WPA, "done\n");
		memcpy(res, scard->res, scard->res_len);
		*res_len = scard->res_len;
		memcpy(ik, scard->ik, sizeof(scard->ik));
		memcpy(ck, scard->ck, sizeof(scard->ck));
		memcpy(auts, scard->auts, sizeof(scard->auts));
		scard->state = SC_IDLE;
		if(scard->aka_status == AKA_OK)
			return 0;
		if(scard->aka_status == AKA_ERR_AUTH)
			return -1;
		if(scard->aka_status == AKA_ERR_SYNC)
			return -2;
		return 0;
	case SC_ERROR:
	default:
		DE_TRACE_STATIC(TR_WPA, "error\n");
		return -3;
	}
}

/* Local Variables: */
/* c-basic-offset: 8 */
/* indent-tabs-mode: t */
/* End: */
