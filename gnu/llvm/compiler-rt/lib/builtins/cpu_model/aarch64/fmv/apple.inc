#include <TargetConditionals.h>
#if TARGET_OS_OSX || TARGET_OS_IPHONE
#include <sys/sysctl.h>

#if __has_include(<arm/cpu_capabilities_public.h>)
#include <arm/cpu_capabilities_public.h>
#define HAS_CPU_CAPABILITIES_PUBLIC_H 1

// FB13964283 - A few of these didn't make it into the public SDK yet.
#ifndef CAP_BIT_FEAT_SME
#define CAP_BIT_FEAT_SME            40
#endif
#ifndef CAP_BIT_FEAT_SME2
#define CAP_BIT_FEAT_SME2           41
#endif
#ifndef CAP_BIT_FEAT_SME_F64F64
#define CAP_BIT_FEAT_SME_F64F64     42
#endif
#ifndef CAP_BIT_FEAT_SME_I16I64
#define CAP_BIT_FEAT_SME_I16I64     43
#endif

#endif

static bool isKnownAndSupported(const char *name) {
  int32_t val = 0;
  size_t size = sizeof(val);
  if (sysctlbyname(name, &val, &size, NULL, 0))
    return false;
  return val;
}

static uint64_t deriveImplicitFeatures(uint64_t features) {
  // FEAT_SSBS2 implies FEAT_SSBS
  if ((1ULL << FEAT_SSBS2) & features)
    features |= (1ULL << FEAT_SSBS);

  // FEAT_FP is always enabled
  features |= (1ULL << FEAT_FP);

  features |= (1ULL << FEAT_INIT);

  return features;
}

void __init_cpu_features_resolver(void) {
  // On Darwin platforms, this may be called concurrently by multiple threads
  // because the resolvers that use it are called lazily at runtime (unlike on
  // ELF platforms, where IFuncs are resolved serially at load time). This
  // function's effect on __aarch64_cpu_features must be idempotent.

  if (__atomic_load_n(&__aarch64_cpu_features.features, __ATOMIC_RELAXED))
    return;

  uint64_t features = 0;

#ifdef HAS_CPU_CAPABILITIES_PUBLIC_H
  uint8_t feats_bitvec[(CAP_BIT_NB + 7) / 8] = {0};
  size_t len = sizeof(feats_bitvec);
  // When hw.optional.arm.feats is available (macOS 15.0+, iOS 18.0+), use the
  // fast path to get all the feature bits, otherwise fall back to the slow
  // ~20-something sysctls path.
  if (!sysctlbyname("hw.optional.arm.caps", &feats_bitvec, &len, 0, 0)) {

#define CHECK_BIT(FROM, TO)                                                    \
  do {                                                                         \
    if (feats_bitvec[FROM / 8] & (1u << ((FROM) & 7))) {                       \
      features |= (1ULL << TO);                                                \
    }                                                                          \
  } while (0)

    CHECK_BIT(CAP_BIT_FEAT_FlagM, FEAT_FLAGM);
    CHECK_BIT(CAP_BIT_FEAT_FlagM2, FEAT_FLAGM2);
    CHECK_BIT(CAP_BIT_FEAT_FHM, FEAT_FP16FML);
    CHECK_BIT(CAP_BIT_FEAT_DotProd, FEAT_DOTPROD);
    CHECK_BIT(CAP_BIT_FEAT_SHA3, FEAT_SHA3);
    CHECK_BIT(CAP_BIT_FEAT_RDM, FEAT_RDM);
    CHECK_BIT(CAP_BIT_FEAT_LSE, FEAT_LSE);
    CHECK_BIT(CAP_BIT_FEAT_SHA256, FEAT_SHA2);
    CHECK_BIT(CAP_BIT_FEAT_SHA1, FEAT_SHA1);
    CHECK_BIT(CAP_BIT_FEAT_AES, FEAT_AES);
    CHECK_BIT(CAP_BIT_FEAT_PMULL, FEAT_PMULL);
    CHECK_BIT(CAP_BIT_FEAT_SPECRES, FEAT_PREDRES);
    CHECK_BIT(CAP_BIT_FEAT_SB, FEAT_SB);
    CHECK_BIT(CAP_BIT_FEAT_FRINTTS, FEAT_FRINTTS);
    CHECK_BIT(CAP_BIT_FEAT_LRCPC, FEAT_RCPC);
    CHECK_BIT(CAP_BIT_FEAT_LRCPC2, FEAT_RCPC2);
    CHECK_BIT(CAP_BIT_FEAT_FCMA, FEAT_FCMA);
    CHECK_BIT(CAP_BIT_FEAT_JSCVT, FEAT_JSCVT);
    CHECK_BIT(CAP_BIT_FEAT_DPB, FEAT_DPB);
    CHECK_BIT(CAP_BIT_FEAT_DPB2, FEAT_DPB2);
    CHECK_BIT(CAP_BIT_FEAT_BF16, FEAT_BF16);
    CHECK_BIT(CAP_BIT_FEAT_I8MM, FEAT_I8MM);
    CHECK_BIT(CAP_BIT_FEAT_DIT, FEAT_DIT);
    CHECK_BIT(CAP_BIT_FEAT_FP16, FEAT_FP16);
    CHECK_BIT(CAP_BIT_FEAT_SSBS, FEAT_SSBS2);
    CHECK_BIT(CAP_BIT_FEAT_BTI, FEAT_BTI);
    CHECK_BIT(CAP_BIT_AdvSIMD, FEAT_SIMD);
    CHECK_BIT(CAP_BIT_CRC32, FEAT_CRC);
    CHECK_BIT(CAP_BIT_FEAT_SME, FEAT_SME);
    CHECK_BIT(CAP_BIT_FEAT_SME2, FEAT_SME2);
    CHECK_BIT(CAP_BIT_FEAT_SME_F64F64, FEAT_SME_F64);
    CHECK_BIT(CAP_BIT_FEAT_SME_I16I64, FEAT_SME_I64);

    features = deriveImplicitFeatures(features);

    __atomic_store(&__aarch64_cpu_features.features, &features,
                   __ATOMIC_RELAXED);
    return;
  }
#endif

  // https://developer.apple.com/documentation/kernel/1387446-sysctlbyname/determining_instruction_set_characteristics
  static const struct {
    const char *sysctl_name;
    enum CPUFeatures feature;
  } feature_checks[] = {
      {"hw.optional.arm.FEAT_FlagM", FEAT_FLAGM},
      {"hw.optional.arm.FEAT_FlagM2", FEAT_FLAGM2},
      {"hw.optional.arm.FEAT_FHM", FEAT_FP16FML},
      {"hw.optional.arm.FEAT_DotProd", FEAT_DOTPROD},
      {"hw.optional.arm.FEAT_RDM", FEAT_RDM},
      {"hw.optional.arm.FEAT_LSE", FEAT_LSE},
      {"hw.optional.AdvSIMD", FEAT_SIMD},
      {"hw.optional.armv8_crc32", FEAT_CRC},
      {"hw.optional.arm.FEAT_SHA1", FEAT_SHA1},
      {"hw.optional.arm.FEAT_SHA256", FEAT_SHA2},
      {"hw.optional.arm.FEAT_SHA3", FEAT_SHA3},
      {"hw.optional.arm.FEAT_AES", FEAT_AES},
      {"hw.optional.arm.FEAT_PMULL", FEAT_PMULL},
      {"hw.optional.arm.FEAT_FP16", FEAT_FP16},
      {"hw.optional.arm.FEAT_DIT", FEAT_DIT},
      {"hw.optional.arm.FEAT_DPB", FEAT_DPB},
      {"hw.optional.arm.FEAT_DPB2", FEAT_DPB2},
      {"hw.optional.arm.FEAT_JSCVT", FEAT_JSCVT},
      {"hw.optional.arm.FEAT_FCMA", FEAT_FCMA},
      {"hw.optional.arm.FEAT_LRCPC", FEAT_RCPC},
      {"hw.optional.arm.FEAT_LRCPC2", FEAT_RCPC2},
      {"hw.optional.arm.FEAT_FRINTTS", FEAT_FRINTTS},
      {"hw.optional.arm.FEAT_I8MM", FEAT_I8MM},
      {"hw.optional.arm.FEAT_BF16", FEAT_BF16},
      {"hw.optional.arm.FEAT_SB", FEAT_SB},
      {"hw.optional.arm.FEAT_SPECRES", FEAT_PREDRES},
      {"hw.optional.arm.FEAT_SSBS", FEAT_SSBS2},
      {"hw.optional.arm.FEAT_BTI", FEAT_BTI},
  };

  for (size_t I = 0, E = sizeof(feature_checks) / sizeof(feature_checks[0]);
        I != E; ++I)
    if (isKnownAndSupported(feature_checks[I].sysctl_name))
      features |= (1ULL << feature_checks[I].feature);

  features = deriveImplicitFeatures(features);

  __atomic_store(&__aarch64_cpu_features.features, &features,
                  __ATOMIC_RELAXED);
}

#endif // TARGET_OS_OSX || TARGET_OS_IPHONE
