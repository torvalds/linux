#if defined(__i386__) || defined(__x86_64__)
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>

#include <pci/pci.h>

#include "helpers/helpers.h"

#define MSR_AMD_PSTATE_STATUS	0xc0010063
#define MSR_AMD_PSTATE		0xc0010064
#define MSR_AMD_PSTATE_LIMIT	0xc0010061

union msr_pstate {
	struct {
		unsigned fid:6;
		unsigned did:3;
		unsigned vid:7;
		unsigned res1:6;
		unsigned nbdid:1;
		unsigned res2:2;
		unsigned nbvid:7;
		unsigned iddval:8;
		unsigned idddiv:2;
		unsigned res3:21;
		unsigned en:1;
	} bits;
	struct {
		unsigned fid:8;
		unsigned did:6;
		unsigned vid:8;
		unsigned iddval:8;
		unsigned idddiv:2;
		unsigned res1:30;
		unsigned en:1;
	} fam17h_bits;
	unsigned long long val;
};

static int get_did(int family, union msr_pstate pstate)
{
	int t;

	if (family == 0x12)
		t = pstate.val & 0xf;
	else if (family == 0x17)
		t = pstate.fam17h_bits.did;
	else
		t = pstate.bits.did;

	return t;
}

static int get_cof(int family, union msr_pstate pstate)
{
	int t;
	int fid, did, cof;

	did = get_did(family, pstate);
	if (family == 0x17) {
		fid = pstate.fam17h_bits.fid;
		cof = 200 * fid / did;
	} else {
		t = 0x10;
		fid = pstate.bits.fid;
		if (family == 0x11)
			t = 0x8;
		cof = (100 * (fid + t)) >> did;
	}
	return cof;
}

/* Needs:
 * cpu          -> the cpu that gets evaluated
 * cpu_family   -> The cpu's family (0x10, 0x12,...)
 * boots_states -> how much boost states the machines support
 *
 * Fills up:
 * pstates -> a pointer to an array of size MAX_HW_PSTATES
 *            must be initialized with zeros.
 *            All available  HW pstates (including boost states)
 * no      -> amount of pstates above array got filled up with
 *
 * returns zero on success, -1 on failure
 */
int decode_pstates(unsigned int cpu, unsigned int cpu_family,
		   int boost_states, unsigned long *pstates, int *no)
{
	int i, psmax, pscur;
	union msr_pstate pstate;
	unsigned long long val;

	/* Only read out frequencies from HW when CPU might be boostable
	   to keep the code as short and clean as possible.
	   Otherwise frequencies are exported via ACPI tables.
	*/
	if (cpu_family < 0x10 || cpu_family == 0x14)
		return -1;

	if (read_msr(cpu, MSR_AMD_PSTATE_LIMIT, &val))
		return -1;

	psmax = (val >> 4) & 0x7;

	if (read_msr(cpu, MSR_AMD_PSTATE_STATUS, &val))
		return -1;

	pscur = val & 0x7;

	pscur += boost_states;
	psmax += boost_states;
	for (i = 0; i <= psmax; i++) {
		if (i >= MAX_HW_PSTATES) {
			fprintf(stderr, "HW pstates [%d] exceeding max [%d]\n",
				psmax, MAX_HW_PSTATES);
			return -1;
		}
		if (read_msr(cpu, MSR_AMD_PSTATE + i, &pstate.val))
			return -1;
		pstates[i] = get_cof(cpu_family, pstate);
	}
	*no = i;
	return 0;
}

int amd_pci_get_num_boost_states(int *active, int *states)
{
	struct pci_access *pci_acc;
	struct pci_dev *device;
	uint8_t val = 0;

	*active = *states = 0;

	device = pci_slot_func_init(&pci_acc, 0x18, 4);

	if (device == NULL)
		return -ENODEV;

	val = pci_read_byte(device, 0x15c);
	if (val & 3)
		*active = 1;
	else
		*active = 0;
	*states = (val >> 2) & 7;

	pci_cleanup(pci_acc);
	return 0;
}
#endif /* defined(__i386__) || defined(__x86_64__) */
