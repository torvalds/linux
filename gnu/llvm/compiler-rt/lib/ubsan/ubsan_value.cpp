//===-- ubsan_value.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Representation of a runtime value, as marshaled from the generated code to
// the ubsan runtime.
//
//===----------------------------------------------------------------------===//

#include "ubsan_platform.h"
#if CAN_SANITIZE_UB
#include "ubsan_value.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_mutex.h"

#if SANITIZER_APPLE
#include <dlfcn.h>
#endif

using namespace __ubsan;

typedef const char *(*ObjCGetClassNameTy)(void *);

const char *__ubsan::getObjCClassName(ValueHandle Pointer) {
#if SANITIZER_APPLE
  // We need to query the ObjC runtime for some information, but do not want
  // to introduce a static dependency from the ubsan runtime onto ObjC. Try to
  // grab a handle to the ObjC runtime used by the process.
  static bool AttemptedDlopen = false;
  static void *ObjCHandle = nullptr;
  static void *ObjCObjectGetClassName = nullptr;

  // Prevent threads from racing to dlopen().
  static __sanitizer::StaticSpinMutex Lock;
  {
    __sanitizer::SpinMutexLock Guard(&Lock);

    if (!AttemptedDlopen) {
      ObjCHandle = dlopen(
          "/usr/lib/libobjc.A.dylib",
          RTLD_LAZY         // Only bind symbols when used.
              | RTLD_LOCAL  // Only make symbols available via the handle.
              | RTLD_NOLOAD // Do not load the dylib, just grab a handle if the
                            // image is already loaded.
              | RTLD_FIRST  // Only search the image pointed-to by the handle.
      );
      AttemptedDlopen = true;
      if (!ObjCHandle)
        return nullptr;
      ObjCObjectGetClassName = dlsym(ObjCHandle, "object_getClassName");
    }
  }

  if (!ObjCObjectGetClassName)
    return nullptr;

  return ObjCGetClassNameTy(ObjCObjectGetClassName)((void *)Pointer);
#else
  return nullptr;
#endif
}

SIntMax Value::getSIntValue() const {
  CHECK(getType().isSignedIntegerTy());
  if (isInlineInt()) {
    // Val was zero-extended to ValueHandle. Sign-extend from original width
    // to SIntMax.
    const unsigned ExtraBits =
      sizeof(SIntMax) * 8 - getType().getIntegerBitWidth();
    return SIntMax(UIntMax(Val) << ExtraBits) >> ExtraBits;
  }
  if (getType().getIntegerBitWidth() == 64)
    return *reinterpret_cast<s64*>(Val);
#if HAVE_INT128_T
  if (getType().getIntegerBitWidth() == 128)
    return *reinterpret_cast<s128*>(Val);
#else
  if (getType().getIntegerBitWidth() == 128)
    UNREACHABLE("libclang_rt.ubsan was built without __int128 support");
#endif
  UNREACHABLE("unexpected bit width");
}

UIntMax Value::getUIntValue() const {
  CHECK(getType().isUnsignedIntegerTy());
  if (isInlineInt())
    return Val;
  if (getType().getIntegerBitWidth() == 64)
    return *reinterpret_cast<u64*>(Val);
#if HAVE_INT128_T
  if (getType().getIntegerBitWidth() == 128)
    return *reinterpret_cast<u128*>(Val);
#else
  if (getType().getIntegerBitWidth() == 128)
    UNREACHABLE("libclang_rt.ubsan was built without __int128 support");
#endif
  UNREACHABLE("unexpected bit width");
}

UIntMax Value::getPositiveIntValue() const {
  if (getType().isUnsignedIntegerTy())
    return getUIntValue();
  SIntMax Val = getSIntValue();
  CHECK(Val >= 0);
  return Val;
}

/// Get the floating-point value of this object, extended to a long double.
/// These are always passed by address (our calling convention doesn't allow
/// them to be passed in floating-point registers, so this has little cost).
FloatMax Value::getFloatValue() const {
  CHECK(getType().isFloatTy());
  if (isInlineFloat()) {
    switch (getType().getFloatBitWidth()) {
#if 0
      // FIXME: OpenCL / NEON 'half' type. LLVM can't lower the conversion
      //        from '__fp16' to 'long double'.
      case 16: {
        __fp16 Value;
        internal_memcpy(&Value, &Val, 4);
        return Value;
      }
#endif
      case 32: {
        float Value;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
       // For big endian the float value is in the last 4 bytes.
       // On some targets we may only have 4 bytes so we count backwards from
       // the end of Val to account for both the 32-bit and 64-bit cases.
       internal_memcpy(&Value, ((const char*)(&Val + 1)) - 4, 4);
#else
       internal_memcpy(&Value, &Val, 4);
#endif
        return Value;
      }
      case 64: {
        double Value;
        internal_memcpy(&Value, &Val, 8);
        return Value;
      }
    }
  } else {
    switch (getType().getFloatBitWidth()) {
    case 64: return *reinterpret_cast<double*>(Val);
    case 80: return *reinterpret_cast<long double*>(Val);
    case 96: return *reinterpret_cast<long double*>(Val);
    case 128: return *reinterpret_cast<long double*>(Val);
    }
  }
  UNREACHABLE("unexpected floating point bit width");
}

#endif  // CAN_SANITIZE_UB
