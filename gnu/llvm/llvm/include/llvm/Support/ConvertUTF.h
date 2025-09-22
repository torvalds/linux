/*===--- ConvertUTF.h - Universal Character Names conversions ---------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *==------------------------------------------------------------------------==*/
/*
 * Copyright © 1991-2015 Unicode, Inc. All rights reserved.
 * Distributed under the Terms of Use in
 * http://www.unicode.org/copyright.html.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of the Unicode data files and any associated documentation
 * (the "Data Files") or Unicode software and any associated documentation
 * (the "Software") to deal in the Data Files or Software
 * without restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, and/or sell copies of
 * the Data Files or Software, and to permit persons to whom the Data Files
 * or Software are furnished to do so, provided that
 * (a) this copyright and permission notice appear with all copies
 * of the Data Files or Software,
 * (b) this copyright and permission notice appear in associated
 * documentation, and
 * (c) there is clear notice in each modified Data File or in the Software
 * as well as in the documentation associated with the Data File(s) or
 * Software that the data or software has been modified.
 *
 * THE DATA FILES AND SOFTWARE ARE PROVIDED "AS IS", WITHOUT WARRANTY OF
 * ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT OF THIRD PARTY RIGHTS.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN THIS
 * NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL
 * DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THE DATA FILES OR SOFTWARE.
 *
 * Except as contained in this notice, the name of a copyright holder
 * shall not be used in advertising or otherwise to promote the sale,
 * use or other dealings in these Data Files or Software without prior
 * written authorization of the copyright holder.
 */

/* ---------------------------------------------------------------------

    Conversions between UTF32, UTF-16, and UTF-8.  Header file.

    Several funtions are included here, forming a complete set of
    conversions between the three formats.  UTF-7 is not included
    here, but is handled in a separate source file.

    Each of these routines takes pointers to input buffers and output
    buffers.  The input buffers are const.

    Each routine converts the text between *sourceStart and sourceEnd,
    putting the result into the buffer between *targetStart and
    targetEnd. Note: the end pointers are *after* the last item: e.g.
    *(sourceEnd - 1) is the last item.

    The return result indicates whether the conversion was successful,
    and if not, whether the problem was in the source or target buffers.
    (Only the first encountered problem is indicated.)

    After the conversion, *sourceStart and *targetStart are both
    updated to point to the end of last text successfully converted in
    the respective buffers.

    Input parameters:
        sourceStart - pointer to a pointer to the source buffer.
                The contents of this are modified on return so that
                it points at the next thing to be converted.
        targetStart - similarly, pointer to pointer to the target buffer.
        sourceEnd, targetEnd - respectively pointers to the ends of the
                two buffers, for overflow checking only.

    These conversion functions take a ConversionFlags argument. When this
    flag is set to strict, both irregular sequences and isolated surrogates
    will cause an error.  When the flag is set to lenient, both irregular
    sequences and isolated surrogates are converted.

    Whether the flag is strict or lenient, all illegal sequences will cause
    an error return. This includes sequences such as: <F4 90 80 80>, <C0 80>,
    or <A0> in UTF-8, and values above 0x10FFFF in UTF-32. Conformant code
    must check for illegal sequences.

    When the flag is set to lenient, characters over 0x10FFFF are converted
    to the replacement character; otherwise (when the flag is set to strict)
    they constitute an error.

    Output parameters:
        The value "sourceIllegal" is returned from some routines if the input
        sequence is malformed.  When "sourceIllegal" is returned, the source
        value will point to the illegal value that caused the problem. E.g.,
        in UTF-8 when a sequence is malformed, it points to the start of the
        malformed sequence.

    Author: Mark E. Davis, 1994.
    Rev History: Rick McGowan, fixes & updates May 2001.
         Fixes & updates, Sept 2001.

------------------------------------------------------------------------ */

#ifndef LLVM_SUPPORT_CONVERTUTF_H
#define LLVM_SUPPORT_CONVERTUTF_H

#include <cstddef>
#include <string>

#if defined(_WIN32)
#include <system_error>
#endif

// Wrap everything in namespace llvm so that programs can link with llvm and
// their own version of the unicode libraries.

namespace llvm {

/* ---------------------------------------------------------------------
    The following 4 definitions are compiler-specific.
    The C standard does not guarantee that wchar_t has at least
    16 bits, so wchar_t is no less portable than unsigned short!
    All should be unsigned values to avoid sign extension during
    bit mask & shift operations.
------------------------------------------------------------------------ */

typedef unsigned int    UTF32;  /* at least 32 bits */
typedef unsigned short  UTF16;  /* at least 16 bits */
typedef unsigned char   UTF8;   /* typically 8 bits */
typedef unsigned char   Boolean; /* 0 or 1 */

/* Some fundamental constants */
#define UNI_REPLACEMENT_CHAR (UTF32)0x0000FFFD
#define UNI_MAX_BMP (UTF32)0x0000FFFF
#define UNI_MAX_UTF16 (UTF32)0x0010FFFF
#define UNI_MAX_UTF32 (UTF32)0x7FFFFFFF
#define UNI_MAX_LEGAL_UTF32 (UTF32)0x0010FFFF

#define UNI_MAX_UTF8_BYTES_PER_CODE_POINT 4

#define UNI_UTF16_BYTE_ORDER_MARK_NATIVE  0xFEFF
#define UNI_UTF16_BYTE_ORDER_MARK_SWAPPED 0xFFFE

#define UNI_UTF32_BYTE_ORDER_MARK_NATIVE 0x0000FEFF
#define UNI_UTF32_BYTE_ORDER_MARK_SWAPPED 0xFFFE0000

typedef enum {
  conversionOK,           /* conversion successful */
  sourceExhausted,        /* partial character in source, but hit end */
  targetExhausted,        /* insuff. room in target for conversion */
  sourceIllegal           /* source sequence is illegal/malformed */
} ConversionResult;

typedef enum {
  strictConversion = 0,
  lenientConversion
} ConversionFlags;

ConversionResult ConvertUTF8toUTF16 (
  const UTF8** sourceStart, const UTF8* sourceEnd,
  UTF16** targetStart, UTF16* targetEnd, ConversionFlags flags);

/**
 * Convert a partial UTF8 sequence to UTF32.  If the sequence ends in an
 * incomplete code unit sequence, returns \c sourceExhausted.
 */
ConversionResult ConvertUTF8toUTF32Partial(
  const UTF8** sourceStart, const UTF8* sourceEnd,
  UTF32** targetStart, UTF32* targetEnd, ConversionFlags flags);

/**
 * Convert a partial UTF8 sequence to UTF32.  If the sequence ends in an
 * incomplete code unit sequence, returns \c sourceIllegal.
 */
ConversionResult ConvertUTF8toUTF32(
  const UTF8** sourceStart, const UTF8* sourceEnd,
  UTF32** targetStart, UTF32* targetEnd, ConversionFlags flags);

ConversionResult ConvertUTF16toUTF8 (
  const UTF16** sourceStart, const UTF16* sourceEnd,
  UTF8** targetStart, UTF8* targetEnd, ConversionFlags flags);

ConversionResult ConvertUTF32toUTF8 (
  const UTF32** sourceStart, const UTF32* sourceEnd,
  UTF8** targetStart, UTF8* targetEnd, ConversionFlags flags);

ConversionResult ConvertUTF16toUTF32 (
  const UTF16** sourceStart, const UTF16* sourceEnd,
  UTF32** targetStart, UTF32* targetEnd, ConversionFlags flags);

ConversionResult ConvertUTF32toUTF16 (
  const UTF32** sourceStart, const UTF32* sourceEnd,
  UTF16** targetStart, UTF16* targetEnd, ConversionFlags flags);

Boolean isLegalUTF8Sequence(const UTF8 *source, const UTF8 *sourceEnd);

Boolean isLegalUTF8String(const UTF8 **source, const UTF8 *sourceEnd);

unsigned getUTF8SequenceSize(const UTF8 *source, const UTF8 *sourceEnd);

unsigned getNumBytesForUTF8(UTF8 firstByte);

/*************************************************************************/
/* Below are LLVM-specific wrappers of the functions above. */

template <typename T> class ArrayRef;
template <typename T> class SmallVectorImpl;
class StringRef;

/**
 * Convert an UTF8 StringRef to UTF8, UTF16, or UTF32 depending on
 * WideCharWidth. The converted data is written to ResultPtr, which needs to
 * point to at least WideCharWidth * (Source.Size() + 1) bytes. On success,
 * ResultPtr will point one after the end of the copied string. On failure,
 * ResultPtr will not be changed, and ErrorPtr will be set to the location of
 * the first character which could not be converted.
 * \return true on success.
 */
bool ConvertUTF8toWide(unsigned WideCharWidth, llvm::StringRef Source,
                       char *&ResultPtr, const UTF8 *&ErrorPtr);

/**
* Converts a UTF-8 StringRef to a std::wstring.
* \return true on success.
*/
bool ConvertUTF8toWide(llvm::StringRef Source, std::wstring &Result);

/**
* Converts a UTF-8 C-string to a std::wstring.
* \return true on success.
*/
bool ConvertUTF8toWide(const char *Source, std::wstring &Result);

/**
* Converts a std::wstring to a UTF-8 encoded std::string.
* \return true on success.
*/
bool convertWideToUTF8(const std::wstring &Source, std::string &Result);


/**
 * Convert an Unicode code point to UTF8 sequence.
 *
 * \param Source a Unicode code point.
 * \param [in,out] ResultPtr pointer to the output buffer, needs to be at least
 * \c UNI_MAX_UTF8_BYTES_PER_CODE_POINT bytes.  On success \c ResultPtr is
 * updated one past end of the converted sequence.
 *
 * \returns true on success.
 */
bool ConvertCodePointToUTF8(unsigned Source, char *&ResultPtr);

/**
 * Convert the first UTF8 sequence in the given source buffer to a UTF32
 * code point.
 *
 * \param [in,out] source A pointer to the source buffer. If the conversion
 * succeeds, this pointer will be updated to point to the byte just past the
 * end of the converted sequence.
 * \param sourceEnd A pointer just past the end of the source buffer.
 * \param [out] target The converted code
 * \param flags Whether the conversion is strict or lenient.
 *
 * \returns conversionOK on success
 *
 * \sa ConvertUTF8toUTF32
 */
inline ConversionResult convertUTF8Sequence(const UTF8 **source,
                                            const UTF8 *sourceEnd,
                                            UTF32 *target,
                                            ConversionFlags flags) {
  if (*source == sourceEnd)
    return sourceExhausted;
  unsigned size = getNumBytesForUTF8(**source);
  if ((ptrdiff_t)size > sourceEnd - *source)
    return sourceExhausted;
  return ConvertUTF8toUTF32(source, *source + size, &target, target + 1, flags);
}

/**
 * Returns true if a blob of text starts with a UTF-16 big or little endian byte
 * order mark.
 */
bool hasUTF16ByteOrderMark(ArrayRef<char> SrcBytes);

/**
 * Converts a stream of raw bytes assumed to be UTF16 into a UTF8 std::string.
 *
 * \param [in] SrcBytes A buffer of what is assumed to be UTF-16 encoded text.
 * \param [out] Out Converted UTF-8 is stored here on success.
 * \returns true on success
 */
bool convertUTF16ToUTF8String(ArrayRef<char> SrcBytes, std::string &Out);

/**
* Converts a UTF16 string into a UTF8 std::string.
*
* \param [in] Src A buffer of UTF-16 encoded text.
* \param [out] Out Converted UTF-8 is stored here on success.
* \returns true on success
*/
bool convertUTF16ToUTF8String(ArrayRef<UTF16> Src, std::string &Out);

/**
 * Converts a stream of raw bytes assumed to be UTF32 into a UTF8 std::string.
 *
 * \param [in] SrcBytes A buffer of what is assumed to be UTF-32 encoded text.
 * \param [out] Out Converted UTF-8 is stored here on success.
 * \returns true on success
 */
bool convertUTF32ToUTF8String(ArrayRef<char> SrcBytes, std::string &Out);

/**
 * Converts a UTF32 string into a UTF8 std::string.
 *
 * \param [in] Src A buffer of UTF-32 encoded text.
 * \param [out] Out Converted UTF-8 is stored here on success.
 * \returns true on success
 */
bool convertUTF32ToUTF8String(ArrayRef<UTF32> Src, std::string &Out);

/**
 * Converts a UTF-8 string into a UTF-16 string with native endianness.
 *
 * \returns true on success
 */
bool convertUTF8ToUTF16String(StringRef SrcUTF8,
                              SmallVectorImpl<UTF16> &DstUTF16);

#if defined(_WIN32)
namespace sys {
namespace windows {
std::error_code UTF8ToUTF16(StringRef utf8, SmallVectorImpl<wchar_t> &utf16);
/// Convert to UTF16 from the current code page used in the system
std::error_code CurCPToUTF16(StringRef utf8, SmallVectorImpl<wchar_t> &utf16);
std::error_code UTF16ToUTF8(const wchar_t *utf16, size_t utf16_len,
                            SmallVectorImpl<char> &utf8);
/// Convert from UTF16 to the current code page used in the system
std::error_code UTF16ToCurCP(const wchar_t *utf16, size_t utf16_len,
                             SmallVectorImpl<char> &utf8);
} // namespace windows
} // namespace sys
#endif

} /* end namespace llvm */

#endif
