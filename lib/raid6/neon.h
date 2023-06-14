// SPDX-License-Identifier: GPL-2.0-only

void raid6_neon1_gen_syndrome_real(int disks, unsigned long bytes, void **ptrs);
void raid6_neon1_xor_syndrome_real(int disks, int start, int stop,
				    unsigned long bytes, void **ptrs);
void raid6_neon2_gen_syndrome_real(int disks, unsigned long bytes, void **ptrs);
void raid6_neon2_xor_syndrome_real(int disks, int start, int stop,
				    unsigned long bytes, void **ptrs);
void raid6_neon4_gen_syndrome_real(int disks, unsigned long bytes, void **ptrs);
void raid6_neon4_xor_syndrome_real(int disks, int start, int stop,
				    unsigned long bytes, void **ptrs);
void raid6_neon8_gen_syndrome_real(int disks, unsigned long bytes, void **ptrs);
void raid6_neon8_xor_syndrome_real(int disks, int start, int stop,
				    unsigned long bytes, void **ptrs);
void __raid6_2data_recov_neon(int bytes, uint8_t *p, uint8_t *q, uint8_t *dp,
			      uint8_t *dq, const uint8_t *pbmul,
			      const uint8_t *qmul);

void __raid6_datap_recov_neon(int bytes, uint8_t *p, uint8_t *q, uint8_t *dq,
			      const uint8_t *qmul);


