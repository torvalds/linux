/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2015 Anshuman Khandual, IBM Corporation.
 */
#define VEC_MAX 128
#define VSX_MAX 32
#define VMX_MAX 32

/*
 * unsigned long vsx[32]
 * unsigned long load[128]
 */
int validate_vsx(unsigned long *vsx, unsigned long *load)
{
	int i;

	for (i = 0; i < VSX_MAX; i++) {
		if (vsx[i] != load[2 * i + 1]) {
			printf("vsx[%d]: %lx load[%d] %lx\n",
					i, vsx[i], 2 * i + 1, load[2 * i + 1]);
			return TEST_FAIL;
		}
	}
	return TEST_PASS;
}

/*
 * unsigned long vmx[32][2]
 * unsigned long load[128]
 */
int validate_vmx(unsigned long vmx[][2], unsigned long *load)
{
	int i;

	for (i = 0; i < VMX_MAX; i++) {
		#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
		if ((vmx[i][0] != load[64 + 2 * i]) ||
				(vmx[i][1] != load[65 + 2 * i])) {
			printf("vmx[%d][0]: %lx load[%d] %lx\n",
					i, vmx[i][0], 64 + 2 * i,
					load[64 + 2 * i]);
			printf("vmx[%d][1]: %lx load[%d] %lx\n",
					i, vmx[i][1], 65 + 2 * i,
					load[65 + 2 * i]);
			return TEST_FAIL;
		}
		#else  /*
			* In LE each value pair is stored in an
			* alternate manner.
			*/
		if ((vmx[i][0] != load[65 + 2 * i]) ||
				(vmx[i][1] != load[64 + 2 * i])) {
			printf("vmx[%d][0]: %lx load[%d] %lx\n",
					i, vmx[i][0], 65 + 2 * i,
					load[65 + 2 * i]);
			printf("vmx[%d][1]: %lx load[%d] %lx\n",
					i, vmx[i][1], 64 + 2 * i,
					load[64 + 2 * i]);
			return TEST_FAIL;
		}
		#endif
	}
	return TEST_PASS;
}

/*
 * unsigned long store[128]
 * unsigned long load[128]
 */
int compare_vsx_vmx(unsigned long *store, unsigned long *load)
{
	int i;

	for (i = 0; i < VSX_MAX; i++) {
		if (store[1 + 2 * i] != load[1 + 2 * i]) {
			printf("store[%d]: %lx load[%d] %lx\n",
					1 + 2 * i, store[i],
					1 + 2 * i, load[i]);
			return TEST_FAIL;
		}
	}

	#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	for (i = 64; i < VEC_MAX; i++) {
		if (store[i] != load[i]) {
			printf("store[%d]: %lx load[%d] %lx\n",
					i, store[i], i, load[i]);
			return TEST_FAIL;
		}
	}
	#else	/* In LE each value pair is stored in an alternate manner */
	for (i = 64; i < VEC_MAX; i++) {
		if (!(i % 2) && (store[i] != load[i+1])) {
			printf("store[%d]: %lx load[%d] %lx\n",
					i, store[i], i+1, load[i+1]);
			return TEST_FAIL;
		}
		if ((i % 2) && (store[i] != load[i-1])) {
			printf("here store[%d]: %lx load[%d] %lx\n",
					i, store[i], i-1, load[i-1]);
			return TEST_FAIL;
		}
	}
	#endif
	return TEST_PASS;
}

void load_vsx_vmx(unsigned long *load, unsigned long *vsx,
		unsigned long vmx[][2])
{
	int i;

	for (i = 0; i < VSX_MAX; i++)
		vsx[i] = load[1 + 2 * i];

	for (i = 0; i < VMX_MAX; i++) {
		vmx[i][0] = load[64 + 2 * i];
		vmx[i][1] = load[65 + 2 * i];
	}
}

void loadvsx(void *p, int tmp);
void storevsx(void *p, int tmp);
