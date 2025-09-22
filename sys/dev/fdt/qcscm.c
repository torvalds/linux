/* $OpenBSD: qcscm.c,v 1.9 2024/08/04 15:30:08 kettenis Exp $ */
/*
 * Copyright (c) 2022 Patrick Wildt <patrick@blueri.se>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/queue.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/device.h>
#include <sys/evcount.h>
#include <sys/socket.h>
#include <sys/timeout.h>
#include <sys/atomic.h>

#include <uvm/uvm_extern.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/efi/efi.h>
#include <machine/efivar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

#include "efi.h"

/* #define QCSCM_DEBUG */

#define ARM_SMCCC_STD_CALL			(0U << 31)
#define ARM_SMCCC_FAST_CALL			(1U << 31)
#define ARM_SMCCC_LP64				(1U << 30)
#define ARM_SMCCC_OWNER_SIP			2

#define QCTEE_TZ_OWNER_TZ_APPS			48
#define QCTEE_TZ_OWNER_QSEE_OS			50

#define QCTEE_TZ_SVC_APP_ID_PLACEHOLDER		0
#define QCTEE_TZ_SVC_APP_MGR			1

#define QCTEE_OS_RESULT_SUCCESS			0
#define QCTEE_OS_RESULT_INCOMPLETE		1
#define QCTEE_OS_RESULT_BLOCKED_ON_LISTENER	2
#define QCTEE_OS_RESULT_FAILURE			0xffffffff

#define QCTEE_OS_SCM_RES_APP_ID			0xee01
#define QCTEE_OS_SCM_RES_QSEOS_LISTENER_ID	0xee02

#define QCTEE_UEFI_GET_VARIABLE			0x8000
#define QCTEE_UEFI_SET_VARIABLE			0x8001
#define QCTEE_UEFI_GET_NEXT_VARIABLE		0x8002
#define QCTEE_UEFI_QUERY_VARIABLE_INFO		0x8003

#define QCTEE_UEFI_SUCCESS			0
#define QCTEE_UEFI_BUFFER_TOO_SMALL		0x80000005
#define QCTEE_UEFI_DEVICE_ERROR			0x80000007
#define QCTEE_UEFI_NOT_FOUND			0x8000000e

#define QCSCM_SVC_PIL			0x02
#define QCSCM_PIL_PAS_INIT_IMAGE	0x01
#define QCSCM_PIL_PAS_MEM_SETUP		0x02
#define QCSCM_PIL_PAS_AUTH_AND_RESET	0x05
#define QCSCM_PIL_PAS_SHUTDOWN		0x06
#define QCSCM_PIL_PAS_IS_SUPPORTED	0x07
#define QCSCM_PIL_PAS_MSS_RESET		0x0a

#define QCSCM_INTERRUPTED		1

#define QCSCM_ARGINFO_NUM(x)		(((x) & 0xf) << 0)
#define QCSCM_ARGINFO_TYPE(x, y)	(((y) & 0x3) << (4 + 2 * (x)))
#define QCSCM_ARGINFO_TYPE_VAL		0
#define QCSCM_ARGINFO_TYPE_RO		1
#define QCSCM_ARGINFO_TYPE_RW		2
#define QCSCM_ARGINFO_TYPE_BUFVAL	3

#define EFI_VARIABLE_NON_VOLATILE	0x00000001
#define EFI_VARIABLE_BOOTSERVICE_ACCESS	0x00000002
#define EFI_VARIABLE_RUNTIME_ACCESS	0x00000004

#define UNIX_GPS_EPOCH_OFFSET		315964800

struct qcscm_dmamem {
	bus_dmamap_t		qdm_map;
	bus_dma_segment_t	qdm_seg;
	size_t			qdm_size;
	caddr_t			qdm_kva;
};

#define QCSCM_DMA_MAP(_qdm)	((_qdm)->qdm_map)
#define QCSCM_DMA_LEN(_qdm)	((_qdm)->qdm_size)
#define QCSCM_DMA_DVA(_qdm)	((uint64_t)(_qdm)->qdm_map->dm_segs[0].ds_addr)
#define QCSCM_DMA_KVA(_qdm)	((void *)(_qdm)->qdm_kva)

EFI_GUID qcscm_uefi_rtcinfo_guid =
  { 0x882f8c2b, 0x9646, 0x435f,
    { 0x8d, 0xe5, 0xf2, 0x08, 0xff, 0x80, 0xc1, 0xbd } };

struct qcscm_softc {
	struct device		sc_dev;
	int			sc_node;
	bus_dma_tag_t		sc_dmat;

	struct qcscm_dmamem	*sc_extarg;
	uint32_t		sc_uefi_id;
};

int	qcscm_match(struct device *, void *, void *);
void	qcscm_attach(struct device *parent, struct device *self, void *args);

const struct cfattach	qcscm_ca = {
	sizeof (struct qcscm_softc), qcscm_match, qcscm_attach
};

struct cfdriver qcscm_cd = {
	NULL, "qcscm", DV_DULL
};

void	qcscm_smc_exec(uint64_t *, uint64_t *);
int	qcscm_smc_call(struct qcscm_softc *, uint8_t, uint8_t, uint8_t,
	    uint32_t, uint64_t *, int, uint64_t *);
int	qcscm_tee_app_get_id(struct qcscm_softc *, const char *, uint32_t *);
int	qcscm_tee_app_send(struct qcscm_softc *, uint32_t, uint64_t, uint64_t,
	    uint64_t, uint64_t);

EFI_STATUS qcscm_uefi_get_variable(struct qcscm_softc *, CHAR16 *,
	    int, EFI_GUID *, uint32_t *, uint8_t *, int *);
EFI_STATUS qcscm_uefi_set_variable(struct qcscm_softc *, CHAR16 *,
	    int, EFI_GUID *, uint32_t, uint8_t *, int);
EFI_STATUS qcscm_uefi_get_next_variable(struct qcscm_softc *,
	    CHAR16 *, int *, EFI_GUID *);

EFI_STATUS qcscm_efi_get_variable(CHAR16 *, EFI_GUID *, UINT32 *,
	    UINTN *, VOID *);
EFI_STATUS qcscm_efi_set_variable(CHAR16 *, EFI_GUID *, UINT32,
	    UINTN, VOID *);
EFI_STATUS qcscm_efi_get_next_variable_name(UINTN *, CHAR16 *, EFI_GUID *);

#ifdef QCSCM_DEBUG
void	qcscm_uefi_dump_variables(struct qcscm_softc *);
void	qcscm_uefi_dump_variable(struct qcscm_softc *, CHAR16 *, int,
	    EFI_GUID *);
#endif

int	qcscm_uefi_rtc_get(uint32_t *);
int	qcscm_uefi_rtc_set(uint32_t);

struct qcscm_dmamem *
	 qcscm_dmamem_alloc(struct qcscm_softc *, bus_size_t, bus_size_t);
void	 qcscm_dmamem_free(struct qcscm_softc *, struct qcscm_dmamem *);

struct qcscm_softc *qcscm_sc;

int
qcscm_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "qcom,scm");
}

void
qcscm_attach(struct device *parent, struct device *self, void *aux)
{
	struct qcscm_softc *sc = (struct qcscm_softc *)self;
	struct fdt_attach_args *faa = aux;

	sc->sc_node = faa->fa_node;
	sc->sc_dmat = faa->fa_dmat;

	sc->sc_extarg = qcscm_dmamem_alloc(sc, PAGE_SIZE, 8);
	if (sc->sc_extarg == NULL) {
		printf(": can't allocate memory for extended args\n");
		return;
	}

	if (qcscm_tee_app_get_id(sc, "qcom.tz.uefisecapp", &sc->sc_uefi_id)) {
		printf(": can't retrieve UEFI App\n");
		return;
	}

	printf("\n");
	qcscm_sc = sc;

#if NEFI > 0
	efi_get_variable = qcscm_efi_get_variable;
	efi_set_variable = qcscm_efi_set_variable;
	efi_get_next_variable_name = qcscm_efi_get_next_variable_name;
#endif

#ifdef QCSCM_DEBUG
	qcscm_uefi_dump_variables(sc);
	qcscm_uefi_dump_variable(sc, u"RTCInfo", sizeof(u"RTCInfo"),
	    &qcscm_uefi_rtcinfo_guid);
#endif
}

/* Expects an uint64_t[8] */
void
qcscm_smc_exec(uint64_t *in, uint64_t *out)
{
	__asm(
	    "ldp x0, x1, [%0, #0]\n"
	    "ldp x2, x3, [%0, #16]\n"
	    "ldp x4, x5, [%0, #32]\n"
	    "ldp x6, x7, [%0, #48]\n"
	    "ldp x8, x9, [%0, #64]\n"
	    "ldp x10, x11, [%0, #80]\n"
	    "ldp x12, x13, [%0, #96]\n"
	    "ldp x14, x15, [%0, #112]\n"
	    "ldp x16, x17, [%0, #128]\n"
	    "smc #0\n"
	    "stp x0, x1, [%1, #0]\n"
	    "stp x2, x3, [%1, #16]\n"
	    "stp x4, x5, [%1, #32]\n"
	    "stp x6, x7, [%1, #48]\n"
	    "stp x8, x9, [%1, #64]\n"
	    "stp x10, x11, [%1, #80]\n"
	    "stp x12, x13, [%1, #96]\n"
	    "stp x14, x15, [%1, #112]\n"
	    "stp x16, x17, [%1, #128]\n" ::
	    "r" (in), "r" (out) :
	    "x0", "x1", "x2", "x3", "x4", "x5",
	    "x6", "x7", "x8", "x9", "x10", "x11",
	    "x12", "x13", "x14", "x15", "x16", "x17",
	    "memory");
}

int
qcscm_smc_call(struct qcscm_softc *sc, uint8_t owner, uint8_t svc, uint8_t cmd,
    uint32_t arginfo, uint64_t *args, int arglen, uint64_t *res)
{
	uint64_t smcreq[18] = { 0 }, smcres[18] = { 0 };
	uint64_t *smcextreq;
	int i;

	/* 4 of our 10 possible args fit into x2-x5 */
	smcreq[0] = ARM_SMCCC_STD_CALL | ARM_SMCCC_LP64 |
	    owner << 24 | svc << 8 | cmd;
	smcreq[1] = arginfo;
	for (i = 0; i < min(arglen, 4); i++)
		smcreq[2 + i] = args[i];

	/* In case we have more than 4, use x5 as ptr to extra args */
	smcextreq = QCSCM_DMA_KVA(sc->sc_extarg);
	if (arglen > 4) {
		smcreq[5] = QCSCM_DMA_DVA(sc->sc_extarg);
		smcextreq = QCSCM_DMA_KVA(sc->sc_extarg);
		for (i = 0; i < min(arglen - 3, 7); i++) {
			smcextreq[i] = args[3 + i];
		}
	}

	for (;;) {
		qcscm_smc_exec(smcreq, smcres);
		/* If the call gets interrupted, try again and re-pass x0/x6 */
		if (smcres[0] == QCSCM_INTERRUPTED) {
			smcreq[0] = smcres[0];
			smcreq[6] = smcres[6];
			continue;
		}
		break;
	}

	if (res) {
		res[0] = smcres[1];
		res[1] = smcres[2];
		res[2] = smcres[3];
	}

	return smcres[0];
}

/* Retrieve id of app running in TEE by name */
int
qcscm_tee_app_get_id(struct qcscm_softc *sc, const char *name, uint32_t *id)
{
	struct qcscm_dmamem *qdm;
	uint64_t res[3];
	uint64_t args[2];
	uint32_t arginfo;
	int ret;

	/* Max name length is 64 */
	if (strlen(name) > 64)
		return EINVAL;

	/* Alloc some phys mem to hold the name */
	qdm = qcscm_dmamem_alloc(sc, PAGE_SIZE, 8);
	if (qdm == NULL)
		return ENOMEM;

	/* Copy name of app we want to get an id for to page */
	memcpy(QCSCM_DMA_KVA(qdm), name, strlen(name));

	/* Pass address of name and length */
	arginfo = QCSCM_ARGINFO_NUM(nitems(args));
	arginfo |= QCSCM_ARGINFO_TYPE(0, QCSCM_ARGINFO_TYPE_RW);
	arginfo |= QCSCM_ARGINFO_TYPE(1, QCSCM_ARGINFO_TYPE_VAL);
	args[0] = QCSCM_DMA_DVA(qdm);
	args[1] = strlen(name);

	/* Make call into TEE */
	ret = qcscm_smc_call(sc, QCTEE_TZ_OWNER_QSEE_OS, QCTEE_TZ_SVC_APP_MGR,
	    0x03, arginfo, args, nitems(args), res);

	/* If the call succeeded, check the response status */
	if (ret == 0)
		ret = res[0];

	/* If the response status is successful as well, retrieve data */
	if (ret == 0)
		*id = res[2];

	qcscm_dmamem_free(sc, qdm);
	return ret;
}

/* Message interface to app running in TEE */
int
qcscm_tee_app_send(struct qcscm_softc *sc, uint32_t id, uint64_t req_phys,
    uint64_t req_len, uint64_t rsp_phys, uint64_t rsp_len)
{
	uint64_t res[3];
	uint64_t args[5];
	uint32_t arginfo;
	int ret;

	/* Pass id of app we target, plus request and response buffers */
	arginfo = QCSCM_ARGINFO_NUM(nitems(args));
	arginfo |= QCSCM_ARGINFO_TYPE(0, QCSCM_ARGINFO_TYPE_VAL);
	arginfo |= QCSCM_ARGINFO_TYPE(1, QCSCM_ARGINFO_TYPE_RW);
	arginfo |= QCSCM_ARGINFO_TYPE(2, QCSCM_ARGINFO_TYPE_VAL);
	arginfo |= QCSCM_ARGINFO_TYPE(3, QCSCM_ARGINFO_TYPE_RW);
	arginfo |= QCSCM_ARGINFO_TYPE(4, QCSCM_ARGINFO_TYPE_VAL);
	args[0] = id;
	args[1] = req_phys;
	args[2] = req_len;
	args[3] = rsp_phys;
	args[4] = rsp_len;

	membar_sync();

	/* Make call into TEE */
	ret = qcscm_smc_call(sc, QCTEE_TZ_OWNER_TZ_APPS,
	    QCTEE_TZ_SVC_APP_ID_PLACEHOLDER, 0x01,
	    arginfo, args, nitems(args), res);

	membar_sync();

	/* If the call succeeded, check the response status */
	if (ret == 0)
		ret = res[0];

	return ret;
}

struct qcscm_req_uefi_get_variable {
	uint32_t command_id;
	uint32_t length;
	uint32_t name_offset;
	uint32_t name_size;
	uint32_t guid_offset;
	uint32_t guid_size;
	uint32_t data_size;
};

struct qcscm_rsp_uefi_get_variable {
	uint32_t command_id;
	uint32_t length;
	uint32_t status;
	uint32_t attributes;
	uint32_t data_offset;
	uint32_t data_size;
};

EFI_STATUS
qcscm_uefi_get_variable(struct qcscm_softc *sc,
    CHAR16 *name, int name_size, EFI_GUID *guid,
    uint32_t *attributes, uint8_t *data, int *data_size)
{
	struct qcscm_req_uefi_get_variable *req;
	struct qcscm_rsp_uefi_get_variable *resp;
	struct qcscm_dmamem *qdm;
	size_t reqsize, respsize;
	off_t reqoff, respoff;
	int ret;

	reqsize = ALIGN(sizeof(*req)) + ALIGN(name_size) + ALIGN(sizeof(*guid));
	respsize = ALIGN(sizeof(*resp)) + ALIGN(*data_size);

	reqoff = 0;
	respoff = reqsize;

	qdm = qcscm_dmamem_alloc(sc, round_page(reqsize + respsize), 8);
	if (qdm == NULL)
		return QCTEE_UEFI_DEVICE_ERROR;

	req = QCSCM_DMA_KVA(qdm) + reqoff;
	req->command_id = QCTEE_UEFI_GET_VARIABLE;
	req->data_size = *data_size;
	req->name_offset = ALIGN(sizeof(*req));
	req->name_size = name_size;
	req->guid_offset = ALIGN(req->name_offset + req->name_size);
	req->guid_size = sizeof(*guid);
	req->length = req->guid_offset + req->guid_size;

	memcpy((char *)req + req->guid_offset, guid, sizeof(*guid));
	memcpy((char *)req + req->name_offset, name, name_size);

	ret = qcscm_tee_app_send(sc, sc->sc_uefi_id,
	    QCSCM_DMA_DVA(qdm) + reqoff, reqsize,
	    QCSCM_DMA_DVA(qdm) + respoff, respsize);
	if (ret) {
		qcscm_dmamem_free(sc, qdm);
		return QCTEE_UEFI_DEVICE_ERROR;
	}

	resp = QCSCM_DMA_KVA(qdm) + respoff;
	if (resp->command_id != QCTEE_UEFI_GET_VARIABLE ||
	    resp->length < sizeof(*resp)) {
		qcscm_dmamem_free(sc, qdm);
		return QCTEE_UEFI_DEVICE_ERROR;
	}

	if (resp->status) {
		if (resp->status == QCTEE_UEFI_BUFFER_TOO_SMALL)
			*data_size = resp->data_size;
		if (attributes)
			*attributes = resp->attributes;
		ret = resp->status;
		qcscm_dmamem_free(sc, qdm);
		return ret;
	}

	if (resp->length > respsize ||
	    resp->data_offset + resp->data_size > resp->length) {
		qcscm_dmamem_free(sc, qdm);
		return QCTEE_UEFI_DEVICE_ERROR;
	}

	if (attributes)
		*attributes = resp->attributes;

	if (*data_size == 0) {
		*data_size = resp->data_size;
		qcscm_dmamem_free(sc, qdm);
		return QCTEE_UEFI_SUCCESS;
	}

	if (resp->data_size > *data_size) {
		*data_size = resp->data_size;
		qcscm_dmamem_free(sc, qdm);
		return QCTEE_UEFI_BUFFER_TOO_SMALL;
	}

	memcpy(data, (char *)resp + resp->data_offset, resp->data_size);
	*data_size = resp->data_size;

	qcscm_dmamem_free(sc, qdm);
	return EFI_SUCCESS;
}

struct qcscm_req_uefi_set_variable {
	uint32_t command_id;
	uint32_t length;
	uint32_t name_offset;
	uint32_t name_size;
	uint32_t guid_offset;
	uint32_t guid_size;
	uint32_t attributes;
	uint32_t data_offset;
	uint32_t data_size;
};

struct qcscm_rsp_uefi_set_variable {
	uint32_t command_id;
	uint32_t length;
	uint32_t status;
	uint32_t unknown[2];
};

EFI_STATUS
qcscm_uefi_set_variable(struct qcscm_softc *sc,
    CHAR16 *name, int name_size, EFI_GUID *guid,
    uint32_t attributes, uint8_t *data, int data_size)
{
	struct qcscm_req_uefi_set_variable *req;
	struct qcscm_rsp_uefi_set_variable *resp;
	struct qcscm_dmamem *qdm;
	size_t reqsize, respsize;
	off_t reqoff, respoff;
	int ret;

	reqsize = ALIGN(sizeof(*req)) + ALIGN(name_size) + ALIGN(sizeof(*guid)) +
	    ALIGN(data_size);
	respsize = ALIGN(sizeof(*resp));

	reqoff = 0;
	respoff = reqsize;

	qdm = qcscm_dmamem_alloc(sc, round_page(reqsize + respsize), 8);
	if (qdm == NULL)
		return QCTEE_UEFI_DEVICE_ERROR;

	req = QCSCM_DMA_KVA(qdm) + reqoff;
	req->command_id = QCTEE_UEFI_SET_VARIABLE;
	req->attributes = attributes;
	req->name_offset = ALIGN(sizeof(*req));
	req->name_size = name_size;
	req->guid_offset = ALIGN(req->name_offset + req->name_size);
	req->guid_size = sizeof(*guid);
	req->data_offset = ALIGN(req->guid_offset + req->guid_size);
	req->data_size = data_size;
	req->length = req->data_offset + req->data_size;

	memcpy((char *)req + req->name_offset, name, name_size);
	memcpy((char *)req + req->guid_offset, guid, sizeof(*guid));
	memcpy((char *)req + req->data_offset, data, data_size);

	ret = qcscm_tee_app_send(sc, sc->sc_uefi_id,
	    QCSCM_DMA_DVA(qdm) + reqoff, reqsize,
	    QCSCM_DMA_DVA(qdm) + respoff, respsize);
	if (ret) {
		qcscm_dmamem_free(sc, qdm);
		return QCTEE_UEFI_DEVICE_ERROR;
	}

	resp = QCSCM_DMA_KVA(qdm) + respoff;
	if (resp->command_id != QCTEE_UEFI_SET_VARIABLE ||
	    resp->length < sizeof(*resp) || resp->length > respsize) {
		qcscm_dmamem_free(sc, qdm);
		return QCTEE_UEFI_DEVICE_ERROR;
	}

	if (resp->status) {
		ret = resp->status;
		qcscm_dmamem_free(sc, qdm);
		return ret;
	}

	qcscm_dmamem_free(sc, qdm);
	return QCTEE_UEFI_SUCCESS;
}

struct qcscm_req_uefi_get_next_variable {
	uint32_t command_id;
	uint32_t length;
	uint32_t guid_offset;
	uint32_t guid_size;
	uint32_t name_offset;
	uint32_t name_size;
};

struct qcscm_rsp_uefi_get_next_variable {
	uint32_t command_id;
	uint32_t length;
	uint32_t status;
	uint32_t guid_offset;
	uint32_t guid_size;
	uint32_t name_offset;
	uint32_t name_size;
};

EFI_STATUS
qcscm_uefi_get_next_variable(struct qcscm_softc *sc,
    CHAR16 *name, int *name_size, EFI_GUID *guid)
{
	struct qcscm_req_uefi_get_next_variable *req;
	struct qcscm_rsp_uefi_get_next_variable *resp;
	struct qcscm_dmamem *qdm;
	size_t reqsize, respsize;
	off_t reqoff, respoff;
	int ret;

	reqsize = ALIGN(sizeof(*req)) + ALIGN(sizeof(*guid)) + ALIGN(*name_size);
	respsize = ALIGN(sizeof(*resp)) + ALIGN(sizeof(*guid)) + ALIGN(*name_size);

	reqoff = 0;
	respoff = reqsize;

	qdm = qcscm_dmamem_alloc(sc, round_page(reqsize + respsize), 8);
	if (qdm == NULL)
		return QCTEE_UEFI_DEVICE_ERROR;

	req = QCSCM_DMA_KVA(qdm) + reqoff;
	req->command_id = QCTEE_UEFI_GET_NEXT_VARIABLE;
	req->guid_offset = ALIGN(sizeof(*req));
	req->guid_size = sizeof(*guid);
	req->name_offset = ALIGN(req->guid_offset + req->guid_size);
	req->name_size = *name_size;
	req->length = req->name_offset + req->name_size;

	memcpy((char *)req + req->guid_offset, guid, sizeof(*guid));
	memcpy((char *)req + req->name_offset, name, *name_size);

	ret = qcscm_tee_app_send(sc, sc->sc_uefi_id,
	    QCSCM_DMA_DVA(qdm) + reqoff, reqsize,
	    QCSCM_DMA_DVA(qdm) + respoff, respsize);
	if (ret) {
		qcscm_dmamem_free(sc, qdm);
		return QCTEE_UEFI_DEVICE_ERROR;
	}

	resp = QCSCM_DMA_KVA(qdm) + respoff;
	if (resp->command_id != QCTEE_UEFI_GET_NEXT_VARIABLE ||
	    resp->length < sizeof(*resp) || resp->length > respsize) {
		qcscm_dmamem_free(sc, qdm);
		return QCTEE_UEFI_DEVICE_ERROR;
	}

	if (resp->status) {
		if (resp->status == QCTEE_UEFI_BUFFER_TOO_SMALL)
			*name_size = resp->name_size;
		ret = resp->status;
		qcscm_dmamem_free(sc, qdm);
		return ret;
	}

	if (resp->guid_offset + resp->guid_size > resp->length ||
	    resp->name_offset + resp->name_size > resp->length) {
		qcscm_dmamem_free(sc, qdm);
		return QCTEE_UEFI_DEVICE_ERROR;
	}

	if (resp->guid_size != sizeof(*guid)) {
		qcscm_dmamem_free(sc, qdm);
		return QCTEE_UEFI_DEVICE_ERROR;
	}

	if (resp->name_size > *name_size) {
		*name_size = resp->name_size;
		qcscm_dmamem_free(sc, qdm);
		return QCTEE_UEFI_BUFFER_TOO_SMALL;
	}

	memcpy(guid, (char *)resp + resp->guid_offset, sizeof(*guid));
	memcpy(name, (char *)resp + resp->name_offset, resp->name_size);
	*name_size = resp->name_size;

	qcscm_dmamem_free(sc, qdm);
	return QCTEE_UEFI_SUCCESS;
}

#if NEFI > 0

EFI_STATUS
qcscm_efi_get_variable(CHAR16 *name, EFI_GUID *guid, UINT32 *attributes,
   UINTN *data_size, VOID *data)
{
	struct qcscm_softc *sc = qcscm_sc;
	EFI_STATUS status;
	int name_size;
	int size;

	name_size = 0;
	while (name[name_size])
		name_size++;
	name_size++;

	size = *data_size;
	status = qcscm_uefi_get_variable(sc, name, name_size * 2, guid,
	    attributes, data, &size);
	*data_size = size;

	/* Convert 32-bit status code to 64-bit. */
	return ((status & 0xf0000000) << 32 | (status & 0x0fffffff));
}

EFI_STATUS
qcscm_efi_set_variable(CHAR16 *name, EFI_GUID *guid, UINT32 attributes,
    UINTN data_size, VOID *data)
{
	struct qcscm_softc *sc = qcscm_sc;
	EFI_STATUS status;
	int name_size;

	name_size = 0;
	while (name[name_size])
		name_size++;
	name_size++;

	status = qcscm_uefi_set_variable(sc, name, name_size * 2, guid,
	    attributes, data, data_size);

	/* Convert 32-bit status code to 64-bit. */
	return ((status & 0xf0000000) << 32 | (status & 0x0fffffff));
}

EFI_STATUS
qcscm_efi_get_next_variable_name(UINTN *name_size, CHAR16 *name,
    EFI_GUID *guid)
{
	struct qcscm_softc *sc = qcscm_sc;
	EFI_STATUS status;
	int size;

	size = *name_size;
	status = qcscm_uefi_get_next_variable(sc, name, &size, guid);
	*name_size = size;

	/* Convert 32-bit status code to 64-bit. */
	return ((status & 0xf0000000) << 32 | (status & 0x0fffffff));
}

#endif

#ifdef QCSCM_DEBUG

void
qcscm_uefi_dump_variables(struct qcscm_softc *sc)
{
	CHAR16 name[128];
	EFI_GUID guid;
	int namesize = sizeof(name);
	int i, ret;

	memset(name, 0, sizeof(name));
	memset(&guid, 0, sizeof(guid));

	for (;;) {
		ret = qcscm_uefi_get_next_variable(sc, name, &namesize, &guid);
		if (ret == 0) {
			printf("%s: ", sc->sc_dev.dv_xname);
			for (i = 0; i < namesize / 2; i++)
				printf("%c", name[i]);
			printf(" { 0x%08x, 0x%04x, 0x%04x, { ",
			   guid.Data1, guid.Data2, guid.Data3);
			for (i = 0; i < 8; i++) {
				printf(" 0x%02x,", guid.Data4[i]);
			}
			printf(" }");
			printf("\n");
			namesize = sizeof(name);
			continue;
		}
		break;
	}
}

void
qcscm_uefi_dump_variable(struct qcscm_softc *sc, CHAR16 *name, int namesize,
    EFI_GUID *guid)
{
	uint8_t data[512];
	int datasize = sizeof(data);
	int i, ret;

	ret = qcscm_uefi_get_variable(sc, name, namesize, guid,
	    NULL, data, &datasize);
	if (ret != QCTEE_UEFI_SUCCESS) {
		printf("%s: error reading ", sc->sc_dev.dv_xname);
		for (i = 0; i < namesize / 2; i++)
			printf("%c", name[i]);
		printf("\n");
		return;
	}

	printf("%s: ", sc->sc_dev.dv_xname);
	for (i = 0; i < namesize / 2; i++)
		printf("%c", name[i]);
	printf(" = ");
	for (i = 0; i < datasize; i++)
		printf("%02x", data[i]);
	printf("\n");
}

#endif

int
qcscm_uefi_rtc_get(uint32_t *off)
{
	struct qcscm_softc *sc = qcscm_sc;
	uint32_t rtcinfo[3];
	int rtcinfosize = sizeof(rtcinfo);

	if (sc == NULL)
		return ENXIO;

	if (qcscm_uefi_get_variable(sc, u"RTCInfo", sizeof(u"RTCInfo"),
	    &qcscm_uefi_rtcinfo_guid, NULL, (uint8_t *)rtcinfo,
	    &rtcinfosize) != 0)
		return EIO;

	/* UEFI stores the offset based on GPS epoch */
	*off = rtcinfo[0] + UNIX_GPS_EPOCH_OFFSET;
	return 0;
}

int
qcscm_uefi_rtc_set(uint32_t off)
{
	struct qcscm_softc *sc = qcscm_sc;
	uint32_t rtcinfo[3];
	int rtcinfosize = sizeof(rtcinfo);

	if (sc == NULL)
		return ENXIO;

	if (qcscm_uefi_get_variable(sc, u"RTCInfo", sizeof(u"RTCInfo"),
	    &qcscm_uefi_rtcinfo_guid, NULL, (uint8_t *)rtcinfo,
	    &rtcinfosize) != 0)
		return EIO;

	/* UEFI stores the offset based on GPS epoch */
	off -= UNIX_GPS_EPOCH_OFFSET;

	/* No need to set if we're not changing anything */
	if (rtcinfo[0] == off)
		return 0;

	rtcinfo[0] = off;

	if (qcscm_uefi_set_variable(sc, u"RTCInfo", sizeof(u"RTCInfo"),
	    &qcscm_uefi_rtcinfo_guid, EFI_VARIABLE_NON_VOLATILE |
	    EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
	    (uint8_t *)rtcinfo, sizeof(rtcinfo)) != 0)
		return EIO;

	return 0;
}

int
qcscm_pas_init_image(uint32_t peripheral, paddr_t metadata)
{
	struct qcscm_softc *sc = qcscm_sc;
	uint64_t res[3];
	uint64_t args[2];
	uint32_t arginfo;
	int ret;

	if (sc == NULL)
		return ENXIO;

	arginfo = QCSCM_ARGINFO_NUM(nitems(args));
	arginfo |= QCSCM_ARGINFO_TYPE(0, QCSCM_ARGINFO_TYPE_VAL);
	arginfo |= QCSCM_ARGINFO_TYPE(1, QCSCM_ARGINFO_TYPE_RW);
	args[0] = peripheral;
	args[1] = metadata;

	/* Make call into TEE */
	ret = qcscm_smc_call(sc, ARM_SMCCC_OWNER_SIP, QCSCM_SVC_PIL,
	    QCSCM_PIL_PAS_INIT_IMAGE, arginfo, args, nitems(args), res);

	/* If the call succeeded, check the response status */
	if (ret == 0)
		ret = res[0];

	return ret;
}

int
qcscm_pas_mem_setup(uint32_t peripheral, paddr_t addr, size_t size)
{
	struct qcscm_softc *sc = qcscm_sc;
	uint64_t res[3];
	uint64_t args[3];
	uint32_t arginfo;
	int ret;

	if (sc == NULL)
		return ENXIO;

	arginfo = QCSCM_ARGINFO_NUM(nitems(args));
	arginfo |= QCSCM_ARGINFO_TYPE(0, QCSCM_ARGINFO_TYPE_VAL);
	arginfo |= QCSCM_ARGINFO_TYPE(1, QCSCM_ARGINFO_TYPE_VAL);
	arginfo |= QCSCM_ARGINFO_TYPE(2, QCSCM_ARGINFO_TYPE_VAL);
	args[0] = peripheral;
	args[1] = addr;
	args[2] = size;

	/* Make call into TEE */
	ret = qcscm_smc_call(sc, ARM_SMCCC_OWNER_SIP, QCSCM_SVC_PIL,
	    QCSCM_PIL_PAS_MEM_SETUP, arginfo, args, nitems(args), res);

	/* If the call succeeded, check the response status */
	if (ret == 0)
		ret = res[0];

	return ret;
}

int
qcscm_pas_auth_and_reset(uint32_t peripheral)
{
	struct qcscm_softc *sc = qcscm_sc;
	uint64_t res[3];
	uint64_t args[1];
	uint32_t arginfo;
	int ret;

	if (sc == NULL)
		return ENXIO;

	arginfo = QCSCM_ARGINFO_NUM(nitems(args));
	arginfo |= QCSCM_ARGINFO_TYPE(0, QCSCM_ARGINFO_TYPE_VAL);
	args[0] = peripheral;

	/* Make call into TEE */
	ret = qcscm_smc_call(sc, ARM_SMCCC_OWNER_SIP, QCSCM_SVC_PIL,
	    QCSCM_PIL_PAS_AUTH_AND_RESET, arginfo, args, nitems(args), res);

	/* If the call succeeded, check the response status */
	if (ret == 0)
		ret = res[0];

	return ret;
}

int
qcscm_pas_shutdown(uint32_t peripheral)
{
	struct qcscm_softc *sc = qcscm_sc;
	uint64_t res[3];
	uint64_t args[1];
	uint32_t arginfo;
	int ret;

	if (sc == NULL)
		return ENXIO;

	arginfo = QCSCM_ARGINFO_NUM(nitems(args));
	arginfo |= QCSCM_ARGINFO_TYPE(0, QCSCM_ARGINFO_TYPE_VAL);
	args[0] = peripheral;

	/* Make call into TEE */
	ret = qcscm_smc_call(sc, ARM_SMCCC_OWNER_SIP, QCSCM_SVC_PIL,
	    QCSCM_PIL_PAS_SHUTDOWN, arginfo, args, nitems(args), res);

	/* If the call succeeded, check the response status */
	if (ret == 0)
		ret = res[0];

	return ret;
}

/* DMA code */
struct qcscm_dmamem *
qcscm_dmamem_alloc(struct qcscm_softc *sc, bus_size_t size, bus_size_t align)
{
	struct qcscm_dmamem *qdm;
	int nsegs;

	qdm = malloc(sizeof(*qdm), M_DEVBUF, M_WAITOK | M_ZERO);
	qdm->qdm_size = size;

	if (bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW, &qdm->qdm_map) != 0)
		goto qdmfree;

	if (bus_dmamem_alloc_range(sc->sc_dmat, size, align, 0,
	    &qdm->qdm_seg, 1, &nsegs, BUS_DMA_WAITOK, 0, 0xffffffff) != 0)
		goto destroy;

	if (bus_dmamem_map(sc->sc_dmat, &qdm->qdm_seg, nsegs, size,
	    &qdm->qdm_kva, BUS_DMA_WAITOK | BUS_DMA_COHERENT) != 0)
		goto free;

	if (bus_dmamap_load(sc->sc_dmat, qdm->qdm_map, qdm->qdm_kva, size,
	    NULL, BUS_DMA_WAITOK) != 0)
		goto unmap;

	bzero(qdm->qdm_kva, size);

	return (qdm);

unmap:
	bus_dmamem_unmap(sc->sc_dmat, qdm->qdm_kva, size);
free:
	bus_dmamem_free(sc->sc_dmat, &qdm->qdm_seg, 1);
destroy:
	bus_dmamap_destroy(sc->sc_dmat, qdm->qdm_map);
qdmfree:
	free(qdm, M_DEVBUF, sizeof(*qdm));

	return (NULL);
}

void
qcscm_dmamem_free(struct qcscm_softc *sc, struct qcscm_dmamem *qdm)
{
	bus_dmamem_unmap(sc->sc_dmat, qdm->qdm_kva, qdm->qdm_size);
	bus_dmamem_free(sc->sc_dmat, &qdm->qdm_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, qdm->qdm_map);
	free(qdm, M_DEVBUF, sizeof(*qdm));
}
