/*	$OpenBSD: pspvar.h,v 1.7 2025/04/25 19:10:50 bluhm Exp $ */

/*
 * Copyright (c) 2023, 2024 Hans-Joerg Hoexer <hshoexer@genua.de>
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

#include <sys/ioctl.h>

/* AMD 17h */
#define PSPV1_REG_INTEN		0x10610
#define PSPV1_REG_INTSTS	0x10614
#define PSPV1_REG_CMDRESP	0x10580
#define PSPV1_REG_ADDRLO	0x105e0
#define PSPV1_REG_ADDRHI	0x105e4
#define PSPV1_REG_CAPABILITIES	0x105fc

#define PSP_REG_INTEN		0x10690
#define PSP_REG_INTSTS		0x10694
#define PSP_REG_CMDRESP		0x10980
#define PSP_REG_ADDRLO		0x109e0
#define PSP_REG_ADDRHI		0x109e4
#define PSP_REG_CAPABILITIES	0x109fc

#define PSP_PSTATE_UNINIT	0x0
#define PSP_PSTATE_INIT		0x1
#define PSP_PSTATE_WORKING	0x2

#define PSP_GSTATE_UNINIT	0x0
#define PSP_GSTATE_LUPDATE	0x1
#define PSP_GSTATE_LSECRET	0x2
#define PSP_GSTATE_RUNNING	0x3
#define PSP_GSTATE_SUPDATE	0x4
#define PSP_GSTATE_RUPDATE	0x5
#define PSP_GSTATE_SENT		0x6

#define PSP_CAP_SEV					(1 << 0)
#define PSP_CAP_TEE					(1 << 1)
#define PSP_CAP_DBC_THRU_EXT				(1 << 2)
#define PSP_CAP_SECURITY_REPORTING			(1 << 7)
#define PSP_CAP_SECURITY_FUSED_PART			(1 << 8)
#define PSP_CAP_SECURITY_DEBUG_LOCK_ON			(1 << 10)
#define PSP_CAP_SECURITY_TSME_STATUS			(1 << 13)
#define PSP_CAP_SECURITY_ANTI_ROLLBACK_STATUS		(1 << 15)
#define PSP_CAP_SECURITY_RPMC_PRODUCTION_ENABLED	(1 << 16)
#define PSP_CAP_SECURITY_RPMC_SPIROM_AVAILABLE		(1 << 17)
#define PSP_CAP_SECURITY_HSP_TPM_AVAILABLE		(1 << 18)
#define PSP_CAP_SECURITY_ROM_ARMOR_ENFORCED		(1 << 19)

#define PSP_CAP_BITS	"\20\001SEV\002TEE\003DBC_THRU_EXT\010REPORTING\011FUSED_PART\013DEBUG_LOCK_ON\016TSME_STATUS\020ANTI_ROLLBACK_STATUS\021RPMC_PRODUCTION_ENABLED\022RPMC_SPIROM_AVAILABLE\023HSP_TPM_AVAILABLE\024ROM_ARMOR_ENFORCED"

#define PSP_CMDRESP_IOC		(1 << 0)
#define PSP_CMDRESP_COMPLETE	(1 << 1)
#define PSP_CMDRESP_RESPONSE	(1 << 31)

#define PSP_STATUS_MASK				0xffff
#define PSP_STATUS_SUCCESS			0x0000
#define PSP_STATUS_INVALID_PLATFORM_STATE	0x0001

#define PSP_TMR_SIZE		(1024*1024)	/* 1 Mb */

#define PSP_SUCCESS		0x0000
#define PSP_INVALID_ADDRESS	0x0009

/* Selection of PSP commands of the SEV API Version 0.24 */

#define PSP_CMD_INIT			0x1
#define PSP_CMD_SHUTDOWN		0x2
#define PSP_CMD_PLATFORMSTATUS		0x4
#define PSP_CMD_DF_FLUSH		0xa
#define PSP_CMD_DOWNLOADFIRMWARE	0xb
#define PSP_CMD_DECOMMISSION		0x20
#define PSP_CMD_ACTIVATE		0x21
#define PSP_CMD_DEACTIVATE		0x22
#define PSP_CMD_GUESTSTATUS		0x23
#define PSP_CMD_LAUNCH_START		0x30
#define PSP_CMD_LAUNCH_UPDATE_DATA	0x31
#define PSP_CMD_LAUNCH_UPDATE_VMSA	0x32
#define PSP_CMD_LAUNCH_MEASURE		0x33
#define PSP_CMD_LAUNCH_FINISH		0x35
#define PSP_CMD_ATTESTATION		0x36

struct psp_platform_status {
	/* Output parameters from PSP_CMD_PLATFORMSTATUS */
	uint8_t			api_major;
	uint8_t			api_minor;
	uint8_t			state;
	uint8_t			owner;
	uint32_t		cfges_build;
	uint32_t		guest_count;
} __packed;

struct psp_guest_status {
	/* Input parameter for PSP_CMD_GUESTSTATUS */
	uint32_t		handle;

	/* Output parameters from PSP_CMD_GUESTSTATUS */
	uint32_t		policy;
	uint32_t		asid;
	uint8_t			state;
} __packed;

struct psp_launch_start {
	/* Input/Output parameter for PSP_CMD_LAUNCH_START */
	uint32_t		handle;

	/* Input parameters for PSP_CMD_LAUNCH_START */
	uint32_t		policy;

	/* The following input parameters are not used yet */
	uint64_t		dh_cert_paddr;
	uint32_t		dh_cert_len;
	uint32_t		reserved;
	uint64_t		session_paddr;
	uint32_t		session_len;
} __packed;

struct psp_launch_update_data {
	/* Input parameters for PSP_CMD_LAUNCH_UPDATE_DATA */
	uint32_t		handle;
	uint32_t		reserved;
	uint64_t		paddr;
	uint32_t		length;
} __packed;

struct psp_launch_update_vmsa {
	/* Input parameters for PSP_CMD_LAUNCH_UPDATE_VMSA */
	uint32_t		handle;
	uint32_t		reserved;
	uint64_t		paddr;
	uint32_t		length;
} __packed;

struct psp_encrypt_state {
	/* Input parameters state encryption */
	uint32_t		handle;
	uint32_t		asid;
	uint32_t		vmid;
	uint32_t		vcpuid;
} __packed;

struct psp_measure {
	/* Output buffer for PSP_CMD_LAUNCH_MEASURE */
	uint8_t			measure[32];
	uint8_t			measure_nonce[16];
} __packed;

struct psp_launch_measure {
	/* Input parameters for PSP_CMD_LAUNCH_MEASURE */
	uint32_t		handle;
	uint32_t		reserved;
	uint64_t		measure_paddr;

	/* Input/output parameter for PSP_CMD_LAUNCH_MEASURE */
	uint32_t		measure_len;
	uint32_t		padding;

	/* Output buffer from PSP_CMD_LAUNCH_MEASURE */
	struct psp_measure	psp_measure;	/* 64bit aligned */
#define measure		psp_measure.measure
#define measure_nonce	psp_measure.measure_nonce
} __packed;

struct psp_launch_finish {
	/* Input parameter for PSP_CMD_LAUNCH_FINISH */
	uint32_t		handle;
} __packed;

struct psp_report {
	/* Output buffer for PSP_CMD_ATTESTATION */
	uint8_t			report_nonce[16];
	uint8_t			report_launch_digest[32];
	uint32_t		report_policy;
	uint32_t		report_sig_usage;
	uint32_t		report_sig_algo;
	uint32_t		reserved2;
	uint8_t			report_sig1[144];
} __packed;

struct psp_attestation {
	/* Input parameters for PSP_CMD_ATTESTATION */
	uint32_t		handle;
	uint32_t		reserved;
	uint64_t		attest_paddr;
	uint8_t			attest_nonce[16];

	/* Input/output parameter from PSP_CMD_ATTESTATION */
	uint32_t		attest_len;
	uint32_t		padding;

	/* Output parameter from PSP_CMD_ATTESTATION */
	struct psp_report	psp_report;	/* 64bit aligned */
#define report_nonce		psp_report.report_nonce
#define report_launch_digest	psp_report.report_launch_digest
#define report_policy		psp_report.report_policy
#define report_sig_usage	psp_report.report_sig_usage;
#define report_report_sig_alg	psp_report.report_sig_algo;
#define report_report_sig1	psp_report.report_sig1;
} __packed;

struct psp_activate {
	/* Input parameters for PSP_CMD_ACTIVATE */
	uint32_t		handle;
	uint32_t		asid;
} __packed;

struct psp_deactivate {
	/* Input parameter for PSP_CMD_DEACTIVATE */
	uint32_t		handle;
} __packed;

struct psp_decommission {
	/* Input parameter for PSP_CMD_DECOMMISSION */
	uint32_t		handle;
} __packed;

struct psp_init {
	/* Output parameters from PSP_CMD_INIT */
	uint32_t		enable_es;
	uint32_t		reserved;
	uint64_t		tmr_paddr;
	uint32_t		tmr_length;
} __packed;

struct psp_downloadfirmware {
	/* Input parameters for PSP_CMD_DOWNLOADFIRMWARE */
	uint64_t		fw_paddr;
	uint32_t		fw_len;
} __packed;

struct psp_guest_shutdown {
	/* Input parameter for PSP_CMD_GUEST_SHUTDOWN */
	uint32_t		handle;
} __packed;

/* Selection of PSP commands of the SEV-SNP ABI Version 1.55 */

#define PSP_CMD_SNP_PLATFORMSTATUS	0x81

struct psp_snp_platform_status {
	uint8_t			api_major;
	uint8_t			api_minor;
	uint8_t			state;
	uint8_t			is_rmp_init;
	uint32_t		build;
	uint32_t		features;
	uint32_t		guest_count;
	uint64_t		current_tcb;
	uint64_t		reported_tcb;
} __packed;

#define PSP_IOC_GET_PSTATUS	_IOR('P', 0, struct psp_platform_status)
#define PSP_IOC_DF_FLUSH	_IO('P', 1)
#define PSP_IOC_DECOMMISSION	_IOW('P', 2, struct psp_decommission)
#define PSP_IOC_GET_GSTATUS	_IOWR('P', 3, struct psp_guest_status)
#define PSP_IOC_LAUNCH_START	_IOWR('P', 4, struct psp_launch_start)
#define PSP_IOC_LAUNCH_UPDATE_DATA \
				_IOW('P', 5, struct psp_launch_update_data)
#define PSP_IOC_LAUNCH_MEASURE	_IOWR('P', 6, struct psp_launch_measure)
#define PSP_IOC_LAUNCH_FINISH	_IOW('P', 7, struct psp_launch_finish)
#define PSP_IOC_ATTESTATION	_IOWR('P', 8, struct psp_attestation)
#define PSP_IOC_ACTIVATE	_IOW('P', 9, struct psp_activate)
#define PSP_IOC_DEACTIVATE	_IOW('P', 10, struct psp_deactivate)
#define PSP_IOC_SNP_GET_PSTATUS	_IOR('P', 11, struct psp_snp_platform_status)
#define PSP_IOC_INIT		_IO('P', 12)
#define PSP_IOC_SHUTDOWN	_IO('P', 13)
#define PSP_IOC_ENCRYPT_STATE	_IOW('P', 254, struct psp_encrypt_state)
#define PSP_IOC_GUEST_SHUTDOWN	_IOW('P', 255, struct psp_guest_shutdown)

#ifdef _KERNEL

struct psp_attach_args {
	bus_space_tag_t		iot;
	bus_space_handle_t	ioh;

	bus_dma_tag_t		dmat;
	uint32_t		capabilities;
	int			version;
};

int pspsubmatch(struct device *, void *, void *);
int pspprint(void *aux, const char *pnp);
int psp_sev_intr(void *);

struct ccp_softc;
struct pci_attach_args;

int psp_pci_match(struct ccp_softc *, struct pci_attach_args *);
void psp_pci_intr_map(struct ccp_softc *, struct pci_attach_args *);
void psp_pci_attach(struct ccp_softc *, struct pci_attach_args *);

#endif	/* _KERNEL */
