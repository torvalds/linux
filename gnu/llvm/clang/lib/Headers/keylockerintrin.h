/*===----------------- keylockerintrin.h - KL Intrinsics -------------------===
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __IMMINTRIN_H
#error "Never use <keylockerintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef _KEYLOCKERINTRIN_H
#define _KEYLOCKERINTRIN_H

#if !defined(__SCE__) || __has_feature(modules) || defined(__KL__)

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS \
  __attribute__((__always_inline__, __nodebug__, __target__("kl"),\
                 __min_vector_width__(128)))

/// Load internal wrapping key from __intkey, __enkey_lo and __enkey_hi. __ctl
/// will assigned to EAX, whch specifies the KeySource and whether backing up
/// the key is permitted. The 256-bit encryption key is loaded from the two
/// explicit operands (__enkey_lo and __enkey_hi). The 128-bit integrity key is
/// loaded from the implicit operand XMM0 which assigned by __intkey.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> LOADIWKEY </c> instructions.
///
/// \code{.operation}
/// IF CPL > 0 // LOADKWKEY only allowed at ring 0 (supervisor mode)
///   GP (0)
/// FI
/// IF “LOADIWKEY exiting” VM execution control set
///   VMexit
/// FI
/// IF __ctl[4:1] > 1 // Reserved KeySource encoding used
///   GP (0)
/// FI
/// IF __ctl[31:5] != 0 // Reserved bit in __ctl is set
///   GP (0)
/// FI
/// IF __ctl[0] AND (CPUID.19H.ECX[0] == 0) // NoBackup is not supported on this part
///   GP (0)
/// FI
/// IF (__ctl[4:1] == 1) AND (CPUID.19H.ECX[1] == 0) // KeySource of 1 is not supported on this part
///   GP (0)
/// FI
/// IF (__ctl[4:1] == 0) // KeySource of 0.
///   IWKey.Encryption Key[127:0] := __enkey_hi[127:0]:
///   IWKey.Encryption Key[255:128] := __enkey_lo[127:0]
///   IWKey.IntegrityKey[127:0] := __intkey[127:0]
///   IWKey.NoBackup := __ctl[0]
///   IWKey.KeySource := __ctl[4:1]
///   ZF := 0
/// ELSE // KeySource of 1. See RDSEED definition for details of randomness
///   IF HW_NRND_GEN.ready == 1 // Full-entropy random data from RDSEED was received
///     IWKey.Encryption Key[127:0] := __enkey_hi[127:0] XOR HW_NRND_GEN.data[127:0]
///     IWKey.Encryption Key[255:128] := __enkey_lo[127:0] XOR HW_NRND_GEN.data[255:128]
///     IWKey.Encryption Key[255:0] := __enkey_hi[127:0]:__enkey_lo[127:0] XOR HW_NRND_GEN.data[255:0]
///     IWKey.IntegrityKey[127:0] := __intkey[127:0] XOR HW_NRND_GEN.data[383:256]
///     IWKey.NoBackup := __ctl[0]
///     IWKey.KeySource := __ctl[4:1]
///     ZF := 0
///   ELSE // Random data was not returned from RDSEED. IWKey was not loaded
///     ZF := 1
///   FI
/// FI
/// dst := ZF
/// OF := 0
/// SF := 0
/// AF := 0
/// PF := 0
/// CF := 0
/// \endcode
static __inline__ void __DEFAULT_FN_ATTRS
_mm_loadiwkey (unsigned int __ctl, __m128i __intkey,
               __m128i __enkey_lo, __m128i __enkey_hi) {
  __builtin_ia32_loadiwkey (__intkey, __enkey_lo, __enkey_hi, __ctl);
}

/// Wrap a 128-bit AES key from __key into a key handle and output in
/// ((__m128i*)__h) to ((__m128i*)__h) + 2  and a 32-bit value as return.
/// The explicit source operand __htype specifies handle restrictions.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> ENCODEKEY128 </c> instructions.
///
/// \code{.operation}
/// InputKey[127:0] := __key[127:0]
/// KeyMetadata[2:0] := __htype[2:0]
/// KeyMetadata[23:3] := 0 // Reserved for future usage
/// KeyMetadata[27:24] := 0 // KeyType is AES-128 (value of 0)
/// KeyMetadata[127:28] := 0 // Reserved for future usage
/// Handle[383:0] := WrapKey128(InputKey[127:0], KeyMetadata[127:0],
///                  IWKey.Integrity Key[127:0], IWKey.Encryption Key[255:0])
/// dst[0] := IWKey.NoBackup
/// dst[4:1] := IWKey.KeySource[3:0]
/// dst[31:5] := 0
/// MEM[__h+127:__h] := Handle[127:0]   // AAD
/// MEM[__h+255:__h+128] := Handle[255:128] // Integrity Tag
/// MEM[__h+383:__h+256] := Handle[383:256] // CipherText
/// OF := 0
/// SF := 0
/// ZF := 0
/// AF := 0
/// PF := 0
/// CF := 0
/// \endcode
static __inline__ unsigned int __DEFAULT_FN_ATTRS
_mm_encodekey128_u32(unsigned int __htype, __m128i __key, void *__h) {
  return __builtin_ia32_encodekey128_u32(__htype, (__v2di)__key, __h);
}

/// Wrap a 256-bit AES key from __key_hi:__key_lo into a key handle, then
/// output handle in ((__m128i*)__h) to ((__m128i*)__h) + 3 and
/// a 32-bit value as return.
/// The explicit source operand __htype specifies handle restrictions.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> ENCODEKEY256 </c> instructions.
///
/// \code{.operation}
/// InputKey[127:0] := __key_lo[127:0]
/// InputKey[255:128] := __key_hi[255:128]
/// KeyMetadata[2:0] := __htype[2:0]
/// KeyMetadata[23:3] := 0 // Reserved for future usage
/// KeyMetadata[27:24] := 1 // KeyType is AES-256 (value of 1)
/// KeyMetadata[127:28] := 0 // Reserved for future usage
/// Handle[511:0] := WrapKey256(InputKey[255:0], KeyMetadata[127:0],
///                  IWKey.Integrity Key[127:0], IWKey.Encryption Key[255:0])
/// dst[0] := IWKey.NoBackup
/// dst[4:1] := IWKey.KeySource[3:0]
/// dst[31:5] := 0
/// MEM[__h+127:__h]   := Handle[127:0] // AAD
/// MEM[__h+255:__h+128] := Handle[255:128] // Tag
/// MEM[__h+383:__h+256] := Handle[383:256] // CipherText[127:0]
/// MEM[__h+511:__h+384] := Handle[511:384] // CipherText[255:128]
/// OF := 0
/// SF := 0
/// ZF := 0
/// AF := 0
/// PF := 0
/// CF := 0
/// \endcode
static __inline__ unsigned int __DEFAULT_FN_ATTRS
_mm_encodekey256_u32(unsigned int __htype, __m128i __key_lo, __m128i __key_hi,
                     void *__h) {
  return __builtin_ia32_encodekey256_u32(__htype, (__v2di)__key_lo,
                                         (__v2di)__key_hi, __h);
}

/// The AESENC128KL performs 10 rounds of AES to encrypt the __idata using
/// the 128-bit key in the handle from the __h. It stores the result in the
/// __odata. And return the affected ZF flag status.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> AESENC128KL </c> instructions.
///
/// \code{.operation}
/// Handle[383:0] := MEM[__h+383:__h] // Load is not guaranteed to be atomic.
/// IllegalHandle := ( HandleReservedBitSet (Handle[383:0]) ||
///                    (Handle[127:0] AND (CPL > 0)) ||
///                    Handle[383:256] ||
///                    HandleKeyType (Handle[383:0]) != HANDLE_KEY_TYPE_AES128 )
/// IF (IllegalHandle)
///   ZF := 1
/// ELSE
///   (UnwrappedKey, Authentic) := UnwrapKeyAndAuthenticate384 (Handle[383:0], IWKey)
///   IF (Authentic == 0)
///     ZF := 1
///   ELSE
///     MEM[__odata+127:__odata] := AES128Encrypt (__idata[127:0], UnwrappedKey)
///     ZF := 0
///   FI
/// FI
/// dst := ZF
/// OF := 0
/// SF := 0
/// AF := 0
/// PF := 0
/// CF := 0
/// \endcode
static __inline__ unsigned char __DEFAULT_FN_ATTRS
_mm_aesenc128kl_u8(__m128i* __odata, __m128i __idata, const void *__h) {
  return __builtin_ia32_aesenc128kl_u8((__v2di *)__odata, (__v2di)__idata, __h);
}

/// The AESENC256KL performs 14 rounds of AES to encrypt the __idata using
/// the 256-bit key in the handle from the __h. It stores the result in the
/// __odata. And return the affected ZF flag status.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> AESENC256KL </c> instructions.
///
/// \code{.operation}
/// Handle[511:0] := MEM[__h+511:__h] // Load is not guaranteed to be atomic.
/// IllegalHandle := ( HandleReservedBitSet (Handle[511:0]) ||
///                    (Handle[127:0] AND (CPL > 0)) ||
///                    Handle[255:128] ||
///                    HandleKeyType (Handle[511:0]) != HANDLE_KEY_TYPE_AES256 )
/// IF (IllegalHandle)
///   ZF := 1
///   MEM[__odata+127:__odata] := 0
/// ELSE
///   (UnwrappedKey, Authentic) := UnwrapKeyAndAuthenticate512 (Handle[511:0], IWKey)
///   IF (Authentic == 0)
///     ZF := 1
///     MEM[__odata+127:__odata] := 0
///   ELSE
///     MEM[__odata+127:__odata] := AES256Encrypt (__idata[127:0], UnwrappedKey)
///     ZF := 0
///   FI
/// FI
/// dst := ZF
/// OF := 0
/// SF := 0
/// AF := 0
/// PF := 0
/// CF := 0
/// \endcode
static __inline__ unsigned char __DEFAULT_FN_ATTRS
_mm_aesenc256kl_u8(__m128i* __odata, __m128i __idata, const void *__h) {
  return __builtin_ia32_aesenc256kl_u8((__v2di *)__odata, (__v2di)__idata, __h);
}

/// The AESDEC128KL performs 10 rounds of AES to decrypt the __idata using
/// the 128-bit key in the handle from the __h. It stores the result in the
/// __odata. And return the affected ZF flag status.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> AESDEC128KL </c> instructions.
///
/// \code{.operation}
/// Handle[383:0] := MEM[__h+383:__h] // Load is not guaranteed to be atomic.
/// IllegalHandle := (HandleReservedBitSet (Handle[383:0]) ||
///                  (Handle[127:0] AND (CPL > 0)) ||
///                  Handle[383:256] ||
///                  HandleKeyType (Handle[383:0]) != HANDLE_KEY_TYPE_AES128)
/// IF (IllegalHandle)
///   ZF := 1
///   MEM[__odata+127:__odata] := 0
/// ELSE
///   (UnwrappedKey, Authentic) := UnwrapKeyAndAuthenticate384 (Handle[383:0], IWKey)
///   IF (Authentic == 0)
///     ZF := 1
///     MEM[__odata+127:__odata] := 0
///   ELSE
///     MEM[__odata+127:__odata] := AES128Decrypt (__idata[127:0], UnwrappedKey)
///     ZF := 0
///   FI
/// FI
/// dst := ZF
/// OF := 0
/// SF := 0
/// AF := 0
/// PF := 0
/// CF := 0
/// \endcode
static __inline__ unsigned char __DEFAULT_FN_ATTRS
_mm_aesdec128kl_u8(__m128i* __odata, __m128i __idata, const void *__h) {
  return __builtin_ia32_aesdec128kl_u8((__v2di *)__odata, (__v2di)__idata, __h);
}

/// The AESDEC256KL performs 10 rounds of AES to decrypt the __idata using
/// the 256-bit key in the handle from the __h. It stores the result in the
/// __odata. And return the affected ZF flag status.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> AESDEC256KL </c> instructions.
///
/// \code{.operation}
/// Handle[511:0] := MEM[__h+511:__h]
/// IllegalHandle := (HandleReservedBitSet (Handle[511:0]) ||
///                   (Handle[127:0] AND (CPL > 0)) ||
///                   Handle[383:256] ||
///                   HandleKeyType (Handle[511:0]) != HANDLE_KEY_TYPE_AES256)
/// IF (IllegalHandle)
///   ZF := 1
///   MEM[__odata+127:__odata] := 0
/// ELSE
///   (UnwrappedKey, Authentic) := UnwrapKeyAndAuthenticate512 (Handle[511:0], IWKey)
///   IF (Authentic == 0)
///     ZF := 1
///     MEM[__odata+127:__odata] := 0
///   ELSE
///     MEM[__odata+127:__odata] := AES256Decrypt (__idata[127:0], UnwrappedKey)
///     ZF := 0
///   FI
/// FI
/// dst := ZF
/// OF := 0
/// SF := 0
/// AF := 0
/// PF := 0
/// CF := 0
/// \endcode
static __inline__ unsigned char __DEFAULT_FN_ATTRS
_mm_aesdec256kl_u8(__m128i* __odata, __m128i __idata, const void *__h) {
  return __builtin_ia32_aesdec256kl_u8((__v2di *)__odata, (__v2di)__idata, __h);
}

#undef __DEFAULT_FN_ATTRS

#endif /* !defined(__SCE__ || __has_feature(modules) || defined(__KL__) */

#if !defined(__SCE__) || __has_feature(modules) || defined(__WIDEKL__)

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS \
  __attribute__((__always_inline__, __nodebug__, __target__("kl,widekl"),\
                 __min_vector_width__(128)))

/// Encrypt __idata[0] to __idata[7] using 128-bit AES key indicated by handle
/// at __h and store each resultant block back from __odata to __odata+7. And
/// return the affected ZF flag status.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> AESENCWIDE128KL </c> instructions.
///
/// \code{.operation}
/// Handle := MEM[__h+383:__h]
/// IllegalHandle := ( HandleReservedBitSet (Handle[383:0]) ||
///                    (Handle[127:0] AND (CPL > 0)) ||
///                    Handle[255:128] ||
///                    HandleKeyType (Handle[383:0]) != HANDLE_KEY_TYPE_AES128 )
/// IF (IllegalHandle)
///   ZF := 1
///   FOR i := 0 to 7
///     __odata[i] := 0
///   ENDFOR
/// ELSE
///   (UnwrappedKey, Authentic) := UnwrapKeyAndAuthenticate384 (Handle[383:0], IWKey)
///   IF Authentic == 0
///     ZF := 1
///     FOR i := 0 to 7
///       __odata[i] := 0
///     ENDFOR
///   ELSE
///     FOR i := 0 to 7
///       __odata[i] := AES128Encrypt (__idata[i], UnwrappedKey)
///     ENDFOR
///     ZF := 0
///   FI
/// FI
/// dst := ZF
/// OF := 0
/// SF := 0
/// AF := 0
/// PF := 0
/// CF := 0
/// \endcode
static __inline__ unsigned char __DEFAULT_FN_ATTRS
_mm_aesencwide128kl_u8(__m128i __odata[8], const __m128i __idata[8], const void* __h) {
  return __builtin_ia32_aesencwide128kl_u8((__v2di *)__odata,
                                           (const __v2di *)__idata, __h);
}

/// Encrypt __idata[0] to __idata[7] using 256-bit AES key indicated by handle
/// at __h and store each resultant block back from __odata to __odata+7. And
/// return the affected ZF flag status.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> AESENCWIDE256KL </c> instructions.
///
/// \code{.operation}
/// Handle[511:0] := MEM[__h+511:__h]
/// IllegalHandle := ( HandleReservedBitSet (Handle[511:0]) ||
///                    (Handle[127:0] AND (CPL > 0)) ||
///                    Handle[255:128] ||
///                    HandleKeyType (Handle[511:0]) != HANDLE_KEY_TYPE_AES512 )
/// IF (IllegalHandle)
///   ZF := 1
///   FOR i := 0 to 7
///     __odata[i] := 0
///   ENDFOR
/// ELSE
///   (UnwrappedKey, Authentic) := UnwrapKeyAndAuthenticate512 (Handle[511:0], IWKey)
///   IF Authentic == 0
///     ZF := 1
///     FOR i := 0 to 7
///       __odata[i] := 0
///     ENDFOR
///   ELSE
///     FOR i := 0 to 7
///       __odata[i] := AES256Encrypt (__idata[i], UnwrappedKey)
///     ENDFOR
///     ZF := 0
///   FI
/// FI
/// dst := ZF
/// OF := 0
/// SF := 0
/// AF := 0
/// PF := 0
/// CF := 0
/// \endcode
static __inline__ unsigned char __DEFAULT_FN_ATTRS
_mm_aesencwide256kl_u8(__m128i __odata[8], const __m128i __idata[8], const void* __h) {
  return __builtin_ia32_aesencwide256kl_u8((__v2di *)__odata,
                                           (const __v2di *)__idata, __h);
}

/// Decrypt __idata[0] to __idata[7] using 128-bit AES key indicated by handle
/// at __h and store each resultant block back from __odata to __odata+7. And
/// return the affected ZF flag status.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> AESDECWIDE128KL </c> instructions.
///
/// \code{.operation}
/// Handle[383:0] := MEM[__h+383:__h]
/// IllegalHandle := ( HandleReservedBitSet (Handle[383:0]) ||
///                    (Handle[127:0] AND (CPL > 0)) ||
///                    Handle[255:128] ||
///                    HandleKeyType (Handle) != HANDLE_KEY_TYPE_AES128 )
/// IF (IllegalHandle)
///   ZF := 1
///   FOR i := 0 to 7
///     __odata[i] := 0
///   ENDFOR
/// ELSE
///   (UnwrappedKey, Authentic) := UnwrapKeyAndAuthenticate384 (Handle[383:0], IWKey)
///   IF Authentic == 0
///     ZF := 1
///     FOR i := 0 to 7
///       __odata[i] := 0
///     ENDFOR
///   ELSE
///     FOR i := 0 to 7
///       __odata[i] := AES128Decrypt (__idata[i], UnwrappedKey)
///     ENDFOR
///     ZF := 0
///   FI
/// FI
/// dst := ZF
/// OF := 0
/// SF := 0
/// AF := 0
/// PF := 0
/// CF := 0
/// \endcode
static __inline__ unsigned char __DEFAULT_FN_ATTRS
_mm_aesdecwide128kl_u8(__m128i __odata[8], const __m128i __idata[8], const void* __h) {
  return __builtin_ia32_aesdecwide128kl_u8((__v2di *)__odata,
                                           (const __v2di *)__idata, __h);
}

/// Decrypt __idata[0] to __idata[7] using 256-bit AES key indicated by handle
/// at __h and store each resultant block back from __odata to __odata+7. And
/// return the affected ZF flag status.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> AESDECWIDE256KL </c> instructions.
///
/// \code{.operation}
/// Handle[511:0] := MEM[__h+511:__h]
/// IllegalHandle = ( HandleReservedBitSet (Handle[511:0]) ||
///                   (Handle[127:0] AND (CPL > 0)) ||
///                   Handle[255:128] ||
///                   HandleKeyType (Handle) != HANDLE_KEY_TYPE_AES512 )
/// If (IllegalHandle)
///   ZF := 1
///   FOR i := 0 to 7
///     __odata[i] := 0
///   ENDFOR
/// ELSE
///   (UnwrappedKey, Authentic) := UnwrapKeyAndAuthenticate512 (Handle[511:0], IWKey)
///   IF Authentic == 0
///     ZF := 1
///     FOR i := 0 to 7
///       __odata[i] := 0
///     ENDFOR
///   ELSE
///     FOR i := 0 to 7
///       __odata[i] := AES256Decrypt (__idata[i], UnwrappedKey)
///     ENDFOR
///     ZF := 0
///   FI
/// FI
/// dst := ZF
/// OF := 0
/// SF := 0
/// AF := 0
/// PF := 0
/// CF := 0
/// \endcode
static __inline__ unsigned char __DEFAULT_FN_ATTRS
_mm_aesdecwide256kl_u8(__m128i __odata[8], const __m128i __idata[8], const void* __h) {
  return __builtin_ia32_aesdecwide256kl_u8((__v2di *)__odata,
                                           (const __v2di *)__idata, __h);
}

#undef __DEFAULT_FN_ATTRS

#endif /* !defined(__SCE__) || __has_feature(modules) || defined(__WIDEKL__)   \
        */

#endif /* _KEYLOCKERINTRIN_H */
