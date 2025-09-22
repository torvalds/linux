#if __has_include(<sys/hwcap.h>)
#include <sys/hwcap.h>
#define HAVE_SYS_HWCAP_H
#endif

#ifndef _IFUNC_ARG_HWCAP
#define _IFUNC_ARG_HWCAP (1ULL << 62)
#endif
#ifndef AT_HWCAP
#define AT_HWCAP 16
#endif
#ifndef HWCAP_CPUID
#define HWCAP_CPUID (1 << 11)
#endif
#ifndef HWCAP_FP
#define HWCAP_FP (1 << 0)
#endif
#ifndef HWCAP_ASIMD
#define HWCAP_ASIMD (1 << 1)
#endif
#ifndef HWCAP_AES
#define HWCAP_AES (1 << 3)
#endif
#ifndef HWCAP_PMULL
#define HWCAP_PMULL (1 << 4)
#endif
#ifndef HWCAP_SHA1
#define HWCAP_SHA1 (1 << 5)
#endif
#ifndef HWCAP_SHA2
#define HWCAP_SHA2 (1 << 6)
#endif
#ifndef HWCAP_CRC32
#define HWCAP_CRC32 (1 << 7)
#endif
#ifndef HWCAP_ATOMICS
#define HWCAP_ATOMICS (1 << 8)
#endif
#ifndef HWCAP_FPHP
#define HWCAP_FPHP (1 << 9)
#endif
#ifndef HWCAP_ASIMDHP
#define HWCAP_ASIMDHP (1 << 10)
#endif
#ifndef HWCAP_ASIMDRDM
#define HWCAP_ASIMDRDM (1 << 12)
#endif
#ifndef HWCAP_JSCVT
#define HWCAP_JSCVT (1 << 13)
#endif
#ifndef HWCAP_FCMA
#define HWCAP_FCMA (1 << 14)
#endif
#ifndef HWCAP_LRCPC
#define HWCAP_LRCPC (1 << 15)
#endif
#ifndef HWCAP_DCPOP
#define HWCAP_DCPOP (1 << 16)
#endif
#ifndef HWCAP_SHA3
#define HWCAP_SHA3 (1 << 17)
#endif
#ifndef HWCAP_SM3
#define HWCAP_SM3 (1 << 18)
#endif
#ifndef HWCAP_SM4
#define HWCAP_SM4 (1 << 19)
#endif
#ifndef HWCAP_ASIMDDP
#define HWCAP_ASIMDDP (1 << 20)
#endif
#ifndef HWCAP_SHA512
#define HWCAP_SHA512 (1 << 21)
#endif
#ifndef HWCAP_SVE
#define HWCAP_SVE (1 << 22)
#endif
#ifndef HWCAP_ASIMDFHM
#define HWCAP_ASIMDFHM (1 << 23)
#endif
#ifndef HWCAP_DIT
#define HWCAP_DIT (1 << 24)
#endif
#ifndef HWCAP_ILRCPC
#define HWCAP_ILRCPC (1 << 26)
#endif
#ifndef HWCAP_FLAGM
#define HWCAP_FLAGM (1 << 27)
#endif
#ifndef HWCAP_SSBS
#define HWCAP_SSBS (1 << 28)
#endif
#ifndef HWCAP_SB
#define HWCAP_SB (1 << 29)
#endif

#ifndef AT_HWCAP2
#define AT_HWCAP2 26
#endif
#ifndef HWCAP2_DCPODP
#define HWCAP2_DCPODP (1 << 0)
#endif
#ifndef HWCAP2_SVE2
#define HWCAP2_SVE2 (1 << 1)
#endif
#ifndef HWCAP2_SVEAES
#define HWCAP2_SVEAES (1 << 2)
#endif
#ifndef HWCAP2_SVEPMULL
#define HWCAP2_SVEPMULL (1 << 3)
#endif
#ifndef HWCAP2_SVEBITPERM
#define HWCAP2_SVEBITPERM (1 << 4)
#endif
#ifndef HWCAP2_SVESHA3
#define HWCAP2_SVESHA3 (1 << 5)
#endif
#ifndef HWCAP2_SVESM4
#define HWCAP2_SVESM4 (1 << 6)
#endif
#ifndef HWCAP2_FLAGM2
#define HWCAP2_FLAGM2 (1 << 7)
#endif
#ifndef HWCAP2_FRINT
#define HWCAP2_FRINT (1 << 8)
#endif
#ifndef HWCAP2_SVEI8MM
#define HWCAP2_SVEI8MM (1 << 9)
#endif
#ifndef HWCAP2_SVEF32MM
#define HWCAP2_SVEF32MM (1 << 10)
#endif
#ifndef HWCAP2_SVEF64MM
#define HWCAP2_SVEF64MM (1 << 11)
#endif
#ifndef HWCAP2_SVEBF16
#define HWCAP2_SVEBF16 (1 << 12)
#endif
#ifndef HWCAP2_I8MM
#define HWCAP2_I8MM (1 << 13)
#endif
#ifndef HWCAP2_BF16
#define HWCAP2_BF16 (1 << 14)
#endif
#ifndef HWCAP2_DGH
#define HWCAP2_DGH (1 << 15)
#endif
#ifndef HWCAP2_RNG
#define HWCAP2_RNG (1 << 16)
#endif
#ifndef HWCAP2_BTI
#define HWCAP2_BTI (1 << 17)
#endif
#ifndef HWCAP2_MTE
#define HWCAP2_MTE (1 << 18)
#endif
#ifndef HWCAP2_RPRES
#define HWCAP2_RPRES (1 << 21)
#endif
#ifndef HWCAP2_MTE3
#define HWCAP2_MTE3 (1 << 22)
#endif
#ifndef HWCAP2_SME
#define HWCAP2_SME (1 << 23)
#endif
#ifndef HWCAP2_SME_I16I64
#define HWCAP2_SME_I16I64 (1 << 24)
#endif
#ifndef HWCAP2_SME_F64F64
#define HWCAP2_SME_F64F64 (1 << 25)
#endif
#ifndef HWCAP2_WFXT
#define HWCAP2_WFXT (1UL << 31)
#endif
#ifndef HWCAP2_EBF16
#define HWCAP2_EBF16 (1ULL << 32)
#endif
#ifndef HWCAP2_SVE_EBF16
#define HWCAP2_SVE_EBF16 (1ULL << 33)
#endif
#ifndef HWCAP2_SME2
#define HWCAP2_SME2 (1UL << 37)
#endif
#ifndef HWCAP2_MOPS
#define HWCAP2_MOPS (1ULL << 43)
#endif
#ifndef HWCAP2_LRCPC3
#define HWCAP2_LRCPC3 (1UL << 46)
#endif
