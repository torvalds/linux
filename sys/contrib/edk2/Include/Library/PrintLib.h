/** @file
  Provides services to print a formatted string to a buffer. All combinations of
  Unicode and ASCII strings are supported.

Copyright (c) 2006 - 2017, Intel Corporation. All rights reserved.<BR>
This program and the accompanying materials are licensed and made available under 
the terms and conditions of the BSD License that accompanies this distribution.  
The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php.

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

  The Print Library functions provide a simple means to produce formatted output 
  strings.  Many of the output functions use a format string to describe how to 
  format the output of variable arguments.  The format string consists of normal 
  text and argument descriptors.  There are no restrictions for how the normal 
  text and argument descriptors can be mixed.  The following end of line(EOL) 
  translations must be performed on the contents of the format string:
  
     - '\\r' is translated to '\\r'
     - '\\r\\n' is translated to '\\r\\n'
     - '\\n' is translated to '\\r\\n' 
     - '\\n\\r' is translated to '\\r\\n'
  
  This does not follow the ANSI C standard for sprint().  The format of argument 
  descriptors is described below.  The ANSI C standard for sprint() has been 
  followed for some of the format types, and has not been followed for others.  
  The exceptions are noted below.

    %[flags][width][.precision]type

  [flags]:
    - -       
      - The field is left justified.  If not flag is not specified, then the 
        field is right justified.
    - space   
      - Prefix a space character to a number.  Only valid for types X, x, and d.
    - + 
      - Prefix a plus character to a number.  Only valid for types X, x, and d.  
        If both space and + are specified, then space is ignored.
    - 0
      - Pad with 0 characters to the left of a number.  Only valid for types 
        X, x, and d.
    - ,
      - Place a comma every 3rd digit of the number.  Only valid for type d.
        If 0 is also specified, then 0 is ignored.
    - L, l
      - The number being printed is size UINT64.  Only valid for types X, x, and d.
        If this flag is not specified, then the number being printed is size int.
    - NOTE: All invalid flags are ignored.

  [width]:

    - *
      - The width of the field is specified by a UINTN argument in the 
        argument list.
    - number
      - The number specified as a decimal value represents the width of 
        the field.
    - NOTE: If [width] is not specified, then a field width of 0 is assumed.

  [.precision]:

    - *
      - The precision of the field is specified by a UINTN argument in the 
        argument list.
    - number
      - The number specified as a decimal value represents the precision of 
        the field.
    - NOTE: If [.precision] is not specified, then a precision of 0 is assumed.

  type:

    - %
      - Print a %%.
    - c
      - The argument is a Unicode character.  ASCII characters can be printed 
        using this type too by making sure bits 8..15 of the argument are set to 0.
    - x
      - The argument is an unsigned hexadecimal number.  The characters used are 0..9 and 
        A..F.  If the flag 'L' is not specified, then the argument is assumed 
        to be size int.  This does not follow ANSI C.
    - X
      - The argument is an unsigned hexadecimal number and the number is padded with 
        zeros.  This is equivalent to a format string of "0x". If the flag 
        'L' is not specified, then the argument is assumed to be size int.  
        This does not follow ANSI C.
    - d
      - The argument is a signed decimal number.  If the flag 'L' is not specified, 
        then the argument is assumed to be size int.  
    - u
      - The argument is a unsigned decimal number.  If the flag 'L' is not specified, 
        then the argument is assumed to be size int.
    - p
      - The argument is a pointer that is a (VOID *), and it is printed as an 
        unsigned hexadecimal number  The characters used are 0..9 and A..F.
    - a
      - The argument is a pointer to an ASCII string.  
        This does not follow ANSI C.
    - S, s
      - The argument is a pointer to a Unicode string.  
        This does not follow ANSI C.
    - g
      - The argument is a pointer to a GUID structure.  The GUID is printed 
        in the format XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX.  
        This does not follow ANSI C.
    - t
      - The argument is a pointer to an EFI_TIME structure.  The time and 
        date are printed in the format "mm/dd/yyyy hh:mm" where mm is the 
        month zero padded, dd is the day zero padded, yyyy is the year zero 
        padded, hh is the hour zero padded, and mm is minutes zero padded.  
        This does not follow ANSI C. 
    - r
      - The argument is a RETURN_STATUS value.  This value is converted to 
        a string following the table below.  This does not follow ANSI C. 
      - RETURN_SUCCESS               
        - "Success"
      - RETURN_LOAD_ERROR            
        - "Load Error"
      - RETURN_INVALID_PARAMETER     
        - "Invalid Parameter"
      - RETURN_UNSUPPORTED           
        - "Unsupported"
      - RETURN_BAD_BUFFER_SIZE       
        - "Bad Buffer Size"
      - RETURN_BUFFER_TOO_SMALL      
        - "Buffer Too Small"
      - RETURN_NOT_READY             
        - "Not Ready"
      - RETURN_DEVICE_ERROR          
        - "Device Error"
      - RETURN_WRITE_PROTECTED       
        - "Write Protected"
      - RETURN_OUT_OF_RESOURCES      
        - "Out of Resources"
      - RETURN_VOLUME_CORRUPTED      
        - "Volume Corrupt"
      - RETURN_VOLUME_FULL           
        - "Volume Full"
      - RETURN_NO_MEDIA              
        - "No Media"
      - RETURN_MEDIA_CHANGED         
        - "Media changed"
      - RETURN_NOT_FOUND             
        - "Not Found"
      - RETURN_ACCESS_DENIED         
        - "Access Denied"
      - RETURN_NO_RESPONSE           
        - "No Response"
      - RETURN_NO_MAPPING            
        - "No mapping"
      - RETURN_TIMEOUT               
        - "Time out"
      - RETURN_NOT_STARTED           
        - "Not started"
      - RETURN_ALREADY_STARTED       
        - "Already started"
      - RETURN_ABORTED               
        - "Aborted"
      - RETURN_ICMP_ERROR            
        - "ICMP Error"
      - RETURN_TFTP_ERROR            
        - "TFTP Error"
      - RETURN_PROTOCOL_ERROR        
        - "Protocol Error"
      - RETURN_WARN_UNKNOWN_GLYPH    
        - "Warning Unknown Glyph"
      - RETURN_WARN_DELETE_FAILURE   
        - "Warning Delete Failure"
      - RETURN_WARN_WRITE_FAILURE    
        - "Warning Write Failure"
      - RETURN_WARN_BUFFER_TOO_SMALL 
        - "Warning Buffer Too Small"

**/

#ifndef __PRINT_LIB_H__
#define __PRINT_LIB_H__

///
/// Define the maximum number of characters that are required to
/// encode with a NULL terminator a decimal, hexadecimal, GUID,   
/// or TIME value.
///  
///  Maximum Length Decimal String     = 28
///    "-9,223,372,036,854,775,808"
///  Maximum Length Hexadecimal String = 17
///    "FFFFFFFFFFFFFFFF"
///  Maximum Length GUID               = 37
///    "00000000-0000-0000-0000-000000000000"
///  Maximum Length TIME               = 18
///    "12/12/2006  12:12"
///
#define MAXIMUM_VALUE_CHARACTERS  38

///
/// Flags bitmask values use in UnicodeValueToString() and 
/// AsciiValueToString()
///
#define LEFT_JUSTIFY      0x01
#define COMMA_TYPE        0x08
#define PREFIX_ZERO       0x20
#define RADIX_HEX         0x80

/**
  Produces a Null-terminated Unicode string in an output buffer based on
  a Null-terminated Unicode format string and a VA_LIST argument list.

  This function is similar as vsnprintf_s defined in C11.

  Produces a Null-terminated Unicode string in the output buffer specified by StartOfBuffer
  and BufferSize.
  The Unicode string is produced by parsing the format string specified by FormatString.
  Arguments are pulled from the variable argument list specified by Marker based on the
  contents of the format string.
  The number of Unicode characters in the produced output buffer is returned not including
  the Null-terminator.

  If StartOfBuffer is not aligned on a 16-bit boundary, then ASSERT().
  If FormatString is not aligned on a 16-bit boundary, then ASSERT().

  If BufferSize > 1 and StartOfBuffer is NULL, then ASSERT(). Also, the output buffer is
  unmodified and 0 is returned.
  If BufferSize > 1 and FormatString is NULL, then ASSERT(). Also, the output buffer is
  unmodified and 0 is returned.
  If PcdMaximumUnicodeStringLength is not zero, and BufferSize >
  (PcdMaximumUnicodeStringLength * sizeof (CHAR16) + 1), then ASSERT(). Also, the output
  buffer is unmodified and 0 is returned.
  If PcdMaximumUnicodeStringLength is not zero, and FormatString contains more than
  PcdMaximumUnicodeStringLength Unicode characters not including the Null-terminator, then
  ASSERT(). Also, the output buffer is unmodified and 0 is returned.

  If BufferSize is 0 or 1, then the output buffer is unmodified and 0 is returned.

  @param  StartOfBuffer   A pointer to the output buffer for the produced Null-terminated
                          Unicode string.
  @param  BufferSize      The size, in bytes, of the output buffer specified by StartOfBuffer.
  @param  FormatString    A Null-terminated Unicode format string.
  @param  Marker          VA_LIST marker for the variable argument list.

  @return The number of Unicode characters in the produced output buffer not including the
          Null-terminator.

**/
UINTN
EFIAPI
UnicodeVSPrint (
  OUT CHAR16        *StartOfBuffer,
  IN  UINTN         BufferSize,
  IN  CONST CHAR16  *FormatString,
  IN  VA_LIST       Marker
  );

/**
  Produces a Null-terminated Unicode string in an output buffer based on
  a Null-terminated Unicode format string and a BASE_LIST argument list.

  Produces a Null-terminated Unicode string in the output buffer specified by StartOfBuffer
  and BufferSize.
  The Unicode string is produced by parsing the format string specified by FormatString.
  Arguments are pulled from the variable argument list specified by Marker based on the
  contents of the format string.
  The number of Unicode characters in the produced output buffer is returned not including
  the Null-terminator.

  If StartOfBuffer is not aligned on a 16-bit boundary, then ASSERT().
  If FormatString is not aligned on a 16-bit boundary, then ASSERT().

  If BufferSize > 1 and StartOfBuffer is NULL, then ASSERT(). Also, the output buffer is
  unmodified and 0 is returned.
  If BufferSize > 1 and FormatString is NULL, then ASSERT(). Also, the output buffer is
  unmodified and 0 is returned.
  If PcdMaximumUnicodeStringLength is not zero, and BufferSize >
  (PcdMaximumUnicodeStringLength * sizeof (CHAR16) + 1), then ASSERT(). Also, the output
  buffer is unmodified and 0 is returned.
  If PcdMaximumUnicodeStringLength is not zero, and FormatString contains more than
  PcdMaximumUnicodeStringLength Unicode characters not including the Null-terminator, then
  ASSERT(). Also, the output buffer is unmodified and 0 is returned.

  If BufferSize is 0 or 1, then the output buffer is unmodified and 0 is returned.

  @param  StartOfBuffer   A pointer to the output buffer for the produced Null-terminated
                          Unicode string.
  @param  BufferSize      The size, in bytes, of the output buffer specified by StartOfBuffer.
  @param  FormatString    A Null-terminated Unicode format string.
  @param  Marker          BASE_LIST marker for the variable argument list.

  @return The number of Unicode characters in the produced output buffer not including the
          Null-terminator.

**/
UINTN
EFIAPI
UnicodeBSPrint (
  OUT CHAR16        *StartOfBuffer,
  IN  UINTN         BufferSize,
  IN  CONST CHAR16  *FormatString,
  IN  BASE_LIST     Marker
  );

/**
  Produces a Null-terminated Unicode string in an output buffer based on a Null-terminated
  Unicode format string and variable argument list.

  This function is similar as snprintf_s defined in C11.

  Produces a Null-terminated Unicode string in the output buffer specified by StartOfBuffer
  and BufferSize.
  The Unicode string is produced by parsing the format string specified by FormatString.
  Arguments are pulled from the variable argument list based on the contents of the format string.
  The number of Unicode characters in the produced output buffer is returned not including
  the Null-terminator.

  If StartOfBuffer is not aligned on a 16-bit boundary, then ASSERT().
  If FormatString is not aligned on a 16-bit boundary, then ASSERT().

  If BufferSize > 1 and StartOfBuffer is NULL, then ASSERT(). Also, the output buffer is
  unmodified and 0 is returned.
  If BufferSize > 1 and FormatString is NULL, then ASSERT(). Also, the output buffer is
  unmodified and 0 is returned.
  If PcdMaximumUnicodeStringLength is not zero, and BufferSize >
  (PcdMaximumUnicodeStringLength * sizeof (CHAR16) + 1), then ASSERT(). Also, the output
  buffer is unmodified and 0 is returned.
  If PcdMaximumUnicodeStringLength is not zero, and FormatString contains more than
  PcdMaximumUnicodeStringLength Unicode characters not including the Null-terminator, then
  ASSERT(). Also, the output buffer is unmodified and 0 is returned.

  If BufferSize is 0 or 1, then the output buffer is unmodified and 0 is returned.

  @param  StartOfBuffer   A pointer to the output buffer for the produced Null-terminated
                          Unicode string.
  @param  BufferSize      The size, in bytes, of the output buffer specified by StartOfBuffer.
  @param  FormatString    A Null-terminated Unicode format string.
  @param  ...             Variable argument list whose contents are accessed based on the
                          format string specified by FormatString.

  @return The number of Unicode characters in the produced output buffer not including the
          Null-terminator.

**/
UINTN
EFIAPI
UnicodeSPrint (
  OUT CHAR16        *StartOfBuffer,
  IN  UINTN         BufferSize,
  IN  CONST CHAR16  *FormatString,
  ...
  );

/**
  Produces a Null-terminated Unicode string in an output buffer based on a Null-terminated
  ASCII format string and a VA_LIST argument list.

  This function is similar as vsnprintf_s defined in C11.

  Produces a Null-terminated Unicode string in the output buffer specified by StartOfBuffer
  and BufferSize.
  The Unicode string is produced by parsing the format string specified by FormatString.
  Arguments are pulled from the variable argument list specified by Marker based on the
  contents of the format string.
  The number of Unicode characters in the produced output buffer is returned not including
  the Null-terminator.

  If StartOfBuffer is not aligned on a 16-bit boundary, then ASSERT().

  If BufferSize > 1 and StartOfBuffer is NULL, then ASSERT(). Also, the output buffer is
  unmodified and 0 is returned.
  If BufferSize > 1 and FormatString is NULL, then ASSERT(). Also, the output buffer is
  unmodified and 0 is returned.
  If PcdMaximumUnicodeStringLength is not zero, and BufferSize >
  (PcdMaximumUnicodeStringLength * sizeof (CHAR16) + 1), then ASSERT(). Also, the output
  buffer is unmodified and 0 is returned.
  If PcdMaximumAsciiStringLength is not zero, and FormatString contains more than
  PcdMaximumAsciiStringLength Ascii characters not including the Null-terminator, then
  ASSERT(). Also, the output buffer is unmodified and 0 is returned.

  If BufferSize is 0 or 1, then no output buffer is produced and 0 is returned.

  @param  StartOfBuffer   A pointer to the output buffer for the produced Null-terminated
                          Unicode string.
  @param  BufferSize      The size, in bytes, of the output buffer specified by StartOfBuffer.
  @param  FormatString    A Null-terminated ASCII format string.
  @param  Marker          VA_LIST marker for the variable argument list.

  @return The number of Unicode characters in the produced output buffer not including the
          Null-terminator.

**/
UINTN
EFIAPI
UnicodeVSPrintAsciiFormat (
  OUT CHAR16       *StartOfBuffer,
  IN  UINTN        BufferSize,
  IN  CONST CHAR8  *FormatString,
  IN  VA_LIST      Marker
  );

/**
  Produces a Null-terminated Unicode string in an output buffer based on a Null-terminated
  ASCII format string and a BASE_LIST argument list.

  Produces a Null-terminated Unicode string in the output buffer specified by StartOfBuffer
  and BufferSize.
  The Unicode string is produced by parsing the format string specified by FormatString.
  Arguments are pulled from the variable argument list specified by Marker based on the
  contents of the format string.
  The number of Unicode characters in the produced output buffer is returned not including
  the Null-terminator.

  If StartOfBuffer is not aligned on a 16-bit boundary, then ASSERT().

  If BufferSize > 1 and StartOfBuffer is NULL, then ASSERT(). Also, the output buffer is
  unmodified and 0 is returned.
  If BufferSize > 1 and FormatString is NULL, then ASSERT(). Also, the output buffer is
  unmodified and 0 is returned.
  If PcdMaximumUnicodeStringLength is not zero, and BufferSize >
  (PcdMaximumUnicodeStringLength * sizeof (CHAR16) + 1), then ASSERT(). Also, the output
  buffer is unmodified and 0 is returned.
  If PcdMaximumAsciiStringLength is not zero, and FormatString contains more than
  PcdMaximumAsciiStringLength Ascii characters not including the Null-terminator, then
  ASSERT(). Also, the output buffer is unmodified and 0 is returned.

  If BufferSize is 0 or 1, then no output buffer is produced and 0 is returned.

  @param  StartOfBuffer   A pointer to the output buffer for the produced Null-terminated
                          Unicode string.
  @param  BufferSize      The size, in bytes, of the output buffer specified by StartOfBuffer.
  @param  FormatString    A Null-terminated ASCII format string.
  @param  Marker          BASE_LIST marker for the variable argument list.

  @return The number of Unicode characters in the produced output buffer not including the
          Null-terminator.

**/
UINTN
EFIAPI
UnicodeBSPrintAsciiFormat (
  OUT CHAR16       *StartOfBuffer,
  IN  UINTN        BufferSize,
  IN  CONST CHAR8  *FormatString,
  IN  BASE_LIST    Marker
  );

/**
  Produces a Null-terminated Unicode string in an output buffer based on a Null-terminated
  ASCII format string and  variable argument list.

  This function is similar as snprintf_s defined in C11.

  Produces a Null-terminated Unicode string in the output buffer specified by StartOfBuffer
  and BufferSize.
  The Unicode string is produced by parsing the format string specified by FormatString.
  Arguments are pulled from the variable argument list based on the contents of the
  format string.
  The number of Unicode characters in the produced output buffer is returned not including
  the Null-terminator.

  If StartOfBuffer is not aligned on a 16-bit boundary, then ASSERT().

  If BufferSize > 1 and StartOfBuffer is NULL, then ASSERT(). Also, the output buffer is
  unmodified and 0 is returned.
  If BufferSize > 1 and FormatString is NULL, then ASSERT(). Also, the output buffer is
  unmodified and 0 is returned.
  If PcdMaximumUnicodeStringLength is not zero, and BufferSize >
  (PcdMaximumUnicodeStringLength * sizeof (CHAR16) + 1), then ASSERT(). Also, the output
  buffer is unmodified and 0 is returned.
  If PcdMaximumAsciiStringLength is not zero, and FormatString contains more than
  PcdMaximumAsciiStringLength Ascii characters not including the Null-terminator, then
  ASSERT(). Also, the output buffer is unmodified and 0 is returned.

  If BufferSize is 0 or 1, then no output buffer is produced and 0 is returned.

  @param  StartOfBuffer   A pointer to the output buffer for the produced Null-terminated
                          Unicode string.
  @param  BufferSize      The size, in bytes, of the output buffer specified by StartOfBuffer.
  @param  FormatString    A Null-terminated ASCII format string.
  @param  ...             Variable argument list whose contents are accessed based on the
                          format string specified by FormatString.

  @return The number of Unicode characters in the produced output buffer not including the
          Null-terminator.

**/
UINTN
EFIAPI
UnicodeSPrintAsciiFormat (
  OUT CHAR16       *StartOfBuffer,
  IN  UINTN        BufferSize,
  IN  CONST CHAR8  *FormatString,
  ...
  );

#ifndef DISABLE_NEW_DEPRECATED_INTERFACES

/**
  [ATTENTION] This function is deprecated for security reason.

  Converts a decimal value to a Null-terminated Unicode string.
  
  Converts the decimal number specified by Value to a Null-terminated Unicode 
  string specified by Buffer containing at most Width characters. No padding of spaces 
  is ever performed. If Width is 0 then a width of MAXIMUM_VALUE_CHARACTERS is assumed.
  The number of Unicode characters in Buffer is returned, not including the Null-terminator.
  If the conversion contains more than Width characters, then only the first
  Width characters are returned, and the total number of characters 
  required to perform the conversion is returned.
  Additional conversion parameters are specified in Flags.  
  
  The Flags bit LEFT_JUSTIFY is always ignored.
  All conversions are left justified in Buffer.
  If Width is 0, PREFIX_ZERO is ignored in Flags.
  If COMMA_TYPE is set in Flags, then PREFIX_ZERO is ignored in Flags, and commas
  are inserted every 3rd digit starting from the right.
  If RADIX_HEX is set in Flags, then the output buffer will be 
  formatted in hexadecimal format.
  If Value is < 0 and RADIX_HEX is not set in Flags, then the fist character in Buffer is a '-'.
  If PREFIX_ZERO is set in Flags and PREFIX_ZERO is not being ignored, 
  then Buffer is padded with '0' characters so the combination of the optional '-' 
  sign character, '0' characters, digit characters for Value, and the Null-terminator
  add up to Width characters.
  If both COMMA_TYPE and RADIX_HEX are set in Flags, then ASSERT().
  If Buffer is NULL, then ASSERT().
  If Buffer is not aligned on a 16-bit boundary, then ASSERT().
  If unsupported bits are set in Flags, then ASSERT().
  If both COMMA_TYPE and RADIX_HEX are set in Flags, then ASSERT().
  If Width >= MAXIMUM_VALUE_CHARACTERS, then ASSERT()

  @param  Buffer  The pointer to the output buffer for the produced Null-terminated
                  Unicode string.
  @param  Flags   The bitmask of flags that specify left justification, zero pad, and commas.
  @param  Value   The 64-bit signed value to convert to a string.
  @param  Width   The maximum number of Unicode characters to place in Buffer, not including
                  the Null-terminator.
  
  @return The number of Unicode characters in Buffer, not including the Null-terminator.

**/
UINTN
EFIAPI
UnicodeValueToString (
  IN OUT CHAR16  *Buffer,
  IN UINTN       Flags,
  IN INT64       Value,
  IN UINTN       Width
  );

#endif

/**
  Converts a decimal value to a Null-terminated Unicode string.

  Converts the decimal number specified by Value to a Null-terminated Unicode
  string specified by Buffer containing at most Width characters. No padding of
  spaces is ever performed. If Width is 0 then a width of
  MAXIMUM_VALUE_CHARACTERS is assumed. If the conversion contains more than
  Width characters, then only the first Width characters are placed in Buffer.
  Additional conversion parameters are specified in Flags.

  The Flags bit LEFT_JUSTIFY is always ignored.
  All conversions are left justified in Buffer.
  If Width is 0, PREFIX_ZERO is ignored in Flags.
  If COMMA_TYPE is set in Flags, then PREFIX_ZERO is ignored in Flags, and
  commas are inserted every 3rd digit starting from the right.
  If RADIX_HEX is set in Flags, then the output buffer will be formatted in
  hexadecimal format.
  If Value is < 0 and RADIX_HEX is not set in Flags, then the fist character in
  Buffer is a '-'.
  If PREFIX_ZERO is set in Flags and PREFIX_ZERO is not being ignored, then
  Buffer is padded with '0' characters so the combination of the optional '-'
  sign character, '0' characters, digit characters for Value, and the
  Null-terminator add up to Width characters.

  If Buffer is not aligned on a 16-bit boundary, then ASSERT().
  If an error would be returned, then the function will also ASSERT().

  @param  Buffer      The pointer to the output buffer for the produced
                      Null-terminated Unicode string.
  @param  BufferSize  The size of Buffer in bytes, including the
                      Null-terminator.
  @param  Flags       The bitmask of flags that specify left justification,
                      zero pad, and commas.
  @param  Value       The 64-bit signed value to convert to a string.
  @param  Width       The maximum number of Unicode characters to place in
                      Buffer, not including the Null-terminator.

  @retval RETURN_SUCCESS           The decimal value is converted.
  @retval RETURN_BUFFER_TOO_SMALL  If BufferSize cannot hold the converted
                                   value.
  @retval RETURN_INVALID_PARAMETER If Buffer is NULL.
                                   If PcdMaximumUnicodeStringLength is not
                                   zero, and BufferSize is greater than
                                   (PcdMaximumUnicodeStringLength *
                                   sizeof (CHAR16) + 1).
                                   If unsupported bits are set in Flags.
                                   If both COMMA_TYPE and RADIX_HEX are set in
                                   Flags.
                                   If Width >= MAXIMUM_VALUE_CHARACTERS.

**/
RETURN_STATUS
EFIAPI
UnicodeValueToStringS (
  IN OUT CHAR16  *Buffer,
  IN UINTN       BufferSize,
  IN UINTN       Flags,
  IN INT64       Value,
  IN UINTN       Width
  );

/**
  Produces a Null-terminated ASCII string in an output buffer based on a Null-terminated
  ASCII format string and a VA_LIST argument list.

  This function is similar as vsnprintf_s defined in C11.

  Produces a Null-terminated ASCII string in the output buffer specified by StartOfBuffer
  and BufferSize.
  The ASCII string is produced by parsing the format string specified by FormatString.
  Arguments are pulled from the variable argument list specified by Marker based on
  the contents of the format string.
  The number of ASCII characters in the produced output buffer is returned not including
  the Null-terminator.

  If BufferSize > 0 and StartOfBuffer is NULL, then ASSERT(). Also, the output buffer is
  unmodified and 0 is returned.
  If BufferSize > 0 and FormatString is NULL, then ASSERT(). Also, the output buffer is
  unmodified and 0 is returned.
  If PcdMaximumAsciiStringLength is not zero, and BufferSize >
  (PcdMaximumAsciiStringLength * sizeof (CHAR8)), then ASSERT(). Also, the output buffer
  is unmodified and 0 is returned.
  If PcdMaximumAsciiStringLength is not zero, and FormatString contains more than
  PcdMaximumAsciiStringLength Ascii characters not including the Null-terminator, then
  ASSERT(). Also, the output buffer is unmodified and 0 is returned.

  If BufferSize is 0, then no output buffer is produced and 0 is returned.

  @param  StartOfBuffer   A pointer to the output buffer for the produced Null-terminated
                          ASCII string.
  @param  BufferSize      The size, in bytes, of the output buffer specified by StartOfBuffer.
  @param  FormatString    A Null-terminated ASCII format string.
  @param  Marker          VA_LIST marker for the variable argument list.

  @return The number of ASCII characters in the produced output buffer not including the
          Null-terminator.

**/
UINTN
EFIAPI
AsciiVSPrint (
  OUT CHAR8         *StartOfBuffer,
  IN  UINTN         BufferSize,
  IN  CONST CHAR8   *FormatString,
  IN  VA_LIST       Marker
  );

/**
  Produces a Null-terminated ASCII string in an output buffer based on a Null-terminated
  ASCII format string and a BASE_LIST argument list.

  Produces a Null-terminated ASCII string in the output buffer specified by StartOfBuffer
  and BufferSize.
  The ASCII string is produced by parsing the format string specified by FormatString.
  Arguments are pulled from the variable argument list specified by Marker based on
  the contents of the format string.
  The number of ASCII characters in the produced output buffer is returned not including
  the Null-terminator.

  If BufferSize > 0 and StartOfBuffer is NULL, then ASSERT(). Also, the output buffer is
  unmodified and 0 is returned.
  If BufferSize > 0 and FormatString is NULL, then ASSERT(). Also, the output buffer is
  unmodified and 0 is returned.
  If PcdMaximumAsciiStringLength is not zero, and BufferSize >
  (PcdMaximumAsciiStringLength * sizeof (CHAR8)), then ASSERT(). Also, the output buffer
  is unmodified and 0 is returned.
  If PcdMaximumAsciiStringLength is not zero, and FormatString contains more than
  PcdMaximumAsciiStringLength Ascii characters not including the Null-terminator, then
  ASSERT(). Also, the output buffer is unmodified and 0 is returned.

  If BufferSize is 0, then no output buffer is produced and 0 is returned.

  @param  StartOfBuffer   A pointer to the output buffer for the produced Null-terminated
                          ASCII string.
  @param  BufferSize      The size, in bytes, of the output buffer specified by StartOfBuffer.
  @param  FormatString    A Null-terminated ASCII format string.
  @param  Marker          BASE_LIST marker for the variable argument list.

  @return The number of ASCII characters in the produced output buffer not including the
          Null-terminator.

**/
UINTN
EFIAPI
AsciiBSPrint (
  OUT CHAR8         *StartOfBuffer,
  IN  UINTN         BufferSize,
  IN  CONST CHAR8   *FormatString,
  IN  BASE_LIST     Marker
  );

/**
  Produces a Null-terminated ASCII string in an output buffer based on a Null-terminated
  ASCII format string and  variable argument list.

  This function is similar as snprintf_s defined in C11.

  Produces a Null-terminated ASCII string in the output buffer specified by StartOfBuffer
  and BufferSize.
  The ASCII string is produced by parsing the format string specified by FormatString.
  Arguments are pulled from the variable argument list based on the contents of the
  format string.
  The number of ASCII characters in the produced output buffer is returned not including
  the Null-terminator.

  If BufferSize > 0 and StartOfBuffer is NULL, then ASSERT(). Also, the output buffer is
  unmodified and 0 is returned.
  If BufferSize > 0 and FormatString is NULL, then ASSERT(). Also, the output buffer is
  unmodified and 0 is returned.
  If PcdMaximumAsciiStringLength is not zero, and BufferSize >
  (PcdMaximumAsciiStringLength * sizeof (CHAR8)), then ASSERT(). Also, the output buffer
  is unmodified and 0 is returned.
  If PcdMaximumAsciiStringLength is not zero, and FormatString contains more than
  PcdMaximumAsciiStringLength Ascii characters not including the Null-terminator, then
  ASSERT(). Also, the output buffer is unmodified and 0 is returned.

  If BufferSize is 0, then no output buffer is produced and 0 is returned.

  @param  StartOfBuffer   A pointer to the output buffer for the produced Null-terminated
                          ASCII string.
  @param  BufferSize      The size, in bytes, of the output buffer specified by StartOfBuffer.
  @param  FormatString    A Null-terminated ASCII format string.
  @param  ...             Variable argument list whose contents are accessed based on the
                          format string specified by FormatString.

  @return The number of ASCII characters in the produced output buffer not including the
          Null-terminator.

**/
UINTN
EFIAPI
AsciiSPrint (
  OUT CHAR8        *StartOfBuffer,
  IN  UINTN        BufferSize,
  IN  CONST CHAR8  *FormatString,
  ...
  );

/**
  Produces a Null-terminated ASCII string in an output buffer based on a Null-terminated
  Unicode format string and a VA_LIST argument list.

  This function is similar as vsnprintf_s defined in C11.

  Produces a Null-terminated ASCII string in the output buffer specified by StartOfBuffer
  and BufferSize.
  The ASCII string is produced by parsing the format string specified by FormatString.
  Arguments are pulled from the variable argument list specified by Marker based on
  the contents of the format string.
  The number of ASCII characters in the produced output buffer is returned not including
  the Null-terminator.

  If FormatString is not aligned on a 16-bit boundary, then ASSERT().

  If BufferSize > 0 and StartOfBuffer is NULL, then ASSERT(). Also, the output buffer is
  unmodified and 0 is returned.
  If BufferSize > 0 and FormatString is NULL, then ASSERT(). Also, the output buffer is
  unmodified and 0 is returned.
  If PcdMaximumAsciiStringLength is not zero, and BufferSize >
  (PcdMaximumAsciiStringLength * sizeof (CHAR8)), then ASSERT(). Also, the output buffer
  is unmodified and 0 is returned.
  If PcdMaximumUnicodeStringLength is not zero, and FormatString contains more than
  PcdMaximumUnicodeStringLength Unicode characters not including the Null-terminator, then
  ASSERT(). Also, the output buffer is unmodified and 0 is returned.

  If BufferSize is 0, then no output buffer is produced and 0 is returned.

  @param  StartOfBuffer   A pointer to the output buffer for the produced Null-terminated
                          ASCII string.
  @param  BufferSize      The size, in bytes, of the output buffer specified by StartOfBuffer.
  @param  FormatString    A Null-terminated Unicode format string.
  @param  Marker          VA_LIST marker for the variable argument list.

  @return The number of ASCII characters in the produced output buffer not including the
          Null-terminator.

**/
UINTN
EFIAPI
AsciiVSPrintUnicodeFormat (
  OUT CHAR8         *StartOfBuffer,
  IN  UINTN         BufferSize,
  IN  CONST CHAR16  *FormatString,
  IN  VA_LIST       Marker
  );

/**
  Produces a Null-terminated ASCII string in an output buffer based on a Null-terminated
  Unicode format string and a BASE_LIST argument list.

  Produces a Null-terminated ASCII string in the output buffer specified by StartOfBuffer
  and BufferSize.
  The ASCII string is produced by parsing the format string specified by FormatString.
  Arguments are pulled from the variable argument list specified by Marker based on
  the contents of the format string.
  The number of ASCII characters in the produced output buffer is returned not including
  the Null-terminator.

  If FormatString is not aligned on a 16-bit boundary, then ASSERT().

  If BufferSize > 0 and StartOfBuffer is NULL, then ASSERT(). Also, the output buffer is
  unmodified and 0 is returned.
  If BufferSize > 0 and FormatString is NULL, then ASSERT(). Also, the output buffer is
  unmodified and 0 is returned.
  If PcdMaximumAsciiStringLength is not zero, and BufferSize >
  (PcdMaximumAsciiStringLength * sizeof (CHAR8)), then ASSERT(). Also, the output buffer
  is unmodified and 0 is returned.
  If PcdMaximumUnicodeStringLength is not zero, and FormatString contains more than
  PcdMaximumUnicodeStringLength Unicode characters not including the Null-terminator, then
  ASSERT(). Also, the output buffer is unmodified and 0 is returned.

  If BufferSize is 0, then no output buffer is produced and 0 is returned.

  @param  StartOfBuffer   A pointer to the output buffer for the produced Null-terminated
                          ASCII string.
  @param  BufferSize      The size, in bytes, of the output buffer specified by StartOfBuffer.
  @param  FormatString    A Null-terminated Unicode format string.
  @param  Marker          BASE_LIST marker for the variable argument list.

  @return The number of ASCII characters in the produced output buffer not including the
          Null-terminator.

**/
UINTN
EFIAPI
AsciiBSPrintUnicodeFormat (
  OUT CHAR8         *StartOfBuffer,
  IN  UINTN         BufferSize,
  IN  CONST CHAR16  *FormatString,
  IN  BASE_LIST     Marker
  );

/**
  Produces a Null-terminated ASCII string in an output buffer based on a Null-terminated
  Unicode format string and  variable argument list.

  This function is similar as snprintf_s defined in C11.

  Produces a Null-terminated ASCII string in the output buffer specified by StartOfBuffer
  and BufferSize.
  The ASCII string is produced by parsing the format string specified by FormatString.
  Arguments are pulled from the variable argument list based on the contents of the
  format string.
  The number of ASCII characters in the produced output buffer is returned not including
  the Null-terminator.

  If FormatString is not aligned on a 16-bit boundary, then ASSERT().

  If BufferSize > 0 and StartOfBuffer is NULL, then ASSERT(). Also, the output buffer is
  unmodified and 0 is returned.
  If BufferSize > 0 and FormatString is NULL, then ASSERT(). Also, the output buffer is
  unmodified and 0 is returned.
  If PcdMaximumAsciiStringLength is not zero, and BufferSize >
  (PcdMaximumAsciiStringLength * sizeof (CHAR8)), then ASSERT(). Also, the output buffer
  is unmodified and 0 is returned.
  If PcdMaximumUnicodeStringLength is not zero, and FormatString contains more than
  PcdMaximumUnicodeStringLength Unicode characters not including the Null-terminator, then
  ASSERT(). Also, the output buffer is unmodified and 0 is returned.

  If BufferSize is 0, then no output buffer is produced and 0 is returned.

  @param  StartOfBuffer   A pointer to the output buffer for the produced Null-terminated
                          ASCII string.
  @param  BufferSize      The size, in bytes, of the output buffer specified by StartOfBuffer.
  @param  FormatString    A Null-terminated Unicode format string.
  @param  ...             Variable argument list whose contents are accessed based on the
                          format string specified by FormatString.

  @return The number of ASCII characters in the produced output buffer not including the
          Null-terminator.

**/
UINTN
EFIAPI
AsciiSPrintUnicodeFormat (
  OUT CHAR8         *StartOfBuffer,
  IN  UINTN         BufferSize,
  IN  CONST CHAR16  *FormatString,
  ...
  );

#ifndef DISABLE_NEW_DEPRECATED_INTERFACES

/**
  [ATTENTION] This function is deprecated for security reason.

  Converts a decimal value to a Null-terminated ASCII string.
  
  Converts the decimal number specified by Value to a Null-terminated ASCII string 
  specified by Buffer containing at most Width characters. No padding of spaces 
  is ever performed.
  If Width is 0 then a width of  MAXIMUM_VALUE_CHARACTERS is assumed.
  The number of ASCII characters in Buffer is returned, not including the Null-terminator.
  If the conversion contains more than Width characters, then only the first Width
  characters are returned, and the total number of characters required to perform
  the conversion is returned.
  Additional conversion parameters are specified in Flags.  
  The Flags bit LEFT_JUSTIFY is always ignored.
  All conversions are left justified in Buffer.
  If Width is 0, PREFIX_ZERO is ignored in Flags.
  If COMMA_TYPE is set in Flags, then PREFIX_ZERO is ignored in Flags, and commas
  are inserted every 3rd digit starting from the right.
  If RADIX_HEX is set in Flags, then the output buffer will be 
  formatted in hexadecimal format.
  If Value is < 0 and RADIX_HEX is not set in Flags, then the fist character in Buffer is a '-'.
  If PREFIX_ZERO is set in Flags and PREFIX_ZERO is not being ignored, 
  then Buffer is padded with '0' characters so the combination of the optional '-' 
  sign character, '0' characters, digit characters for Value, and the Null-terminator
  add up to Width characters.
  
  If Buffer is NULL, then ASSERT().
  If unsupported bits are set in Flags, then ASSERT().
  If both COMMA_TYPE and RADIX_HEX are set in Flags, then ASSERT().
  If Width >= MAXIMUM_VALUE_CHARACTERS, then ASSERT()

  @param  Buffer  A pointer to the output buffer for the produced Null-terminated
                  ASCII string.
  @param  Flags   The bitmask of flags that specify left justification, zero pad, and commas.
  @param  Value   The 64-bit signed value to convert to a string.
  @param  Width   The maximum number of ASCII characters to place in Buffer, not including
                  the Null-terminator.
  
  @return The number of ASCII characters in Buffer, not including the Null-terminator.

**/
UINTN
EFIAPI
AsciiValueToString (
  OUT CHAR8      *Buffer,
  IN  UINTN      Flags,
  IN  INT64      Value,
  IN  UINTN      Width
  );

#endif

/**
  Converts a decimal value to a Null-terminated Ascii string.

  Converts the decimal number specified by Value to a Null-terminated Ascii
  string specified by Buffer containing at most Width characters. No padding of
  spaces is ever performed. If Width is 0 then a width of
  MAXIMUM_VALUE_CHARACTERS is assumed. If the conversion contains more than
  Width characters, then only the first Width characters are placed in Buffer.
  Additional conversion parameters are specified in Flags.

  The Flags bit LEFT_JUSTIFY is always ignored.
  All conversions are left justified in Buffer.
  If Width is 0, PREFIX_ZERO is ignored in Flags.
  If COMMA_TYPE is set in Flags, then PREFIX_ZERO is ignored in Flags, and
  commas are inserted every 3rd digit starting from the right.
  If RADIX_HEX is set in Flags, then the output buffer will be formatted in
  hexadecimal format.
  If Value is < 0 and RADIX_HEX is not set in Flags, then the fist character in
  Buffer is a '-'.
  If PREFIX_ZERO is set in Flags and PREFIX_ZERO is not being ignored, then
  Buffer is padded with '0' characters so the combination of the optional '-'
  sign character, '0' characters, digit characters for Value, and the
  Null-terminator add up to Width characters.

  If Buffer is not aligned on a 16-bit boundary, then ASSERT().
  If an error would be returned, then the function will also ASSERT().

  @param  Buffer      The pointer to the output buffer for the produced
                      Null-terminated Ascii string.
  @param  BufferSize  The size of Buffer in bytes, including the
                      Null-terminator.
  @param  Flags       The bitmask of flags that specify left justification,
                      zero pad, and commas.
  @param  Value       The 64-bit signed value to convert to a string.
  @param  Width       The maximum number of Ascii characters to place in
                      Buffer, not including the Null-terminator.

  @retval RETURN_SUCCESS           The decimal value is converted.
  @retval RETURN_BUFFER_TOO_SMALL  If BufferSize cannot hold the converted
                                   value.
  @retval RETURN_INVALID_PARAMETER If Buffer is NULL.
                                   If PcdMaximumAsciiStringLength is not
                                   zero, and BufferSize is greater than
                                   PcdMaximumAsciiStringLength.
                                   If unsupported bits are set in Flags.
                                   If both COMMA_TYPE and RADIX_HEX are set in
                                   Flags.
                                   If Width >= MAXIMUM_VALUE_CHARACTERS.

**/
RETURN_STATUS
EFIAPI
AsciiValueToStringS (
  IN OUT CHAR8   *Buffer,
  IN UINTN       BufferSize,
  IN UINTN       Flags,
  IN INT64       Value,
  IN UINTN       Width
  );

/**
  Returns the number of characters that would be produced by if the formatted 
  output were produced not including the Null-terminator.

  If FormatString is not aligned on a 16-bit boundary, then ASSERT().

  If FormatString is NULL, then ASSERT() and 0 is returned.
  If PcdMaximumUnicodeStringLength is not zero, and FormatString contains more
  than PcdMaximumUnicodeStringLength Unicode characters not including the
  Null-terminator, then ASSERT() and 0 is returned.

  @param[in]  FormatString    A Null-terminated Unicode format string.
  @param[in]  Marker          VA_LIST marker for the variable argument list.

  @return The number of characters that would be produced, not including the 
          Null-terminator.
**/
UINTN
EFIAPI
SPrintLength (
  IN  CONST CHAR16   *FormatString,
  IN  VA_LIST       Marker
  );

/**
  Returns the number of characters that would be produced by if the formatted 
  output were produced not including the Null-terminator.

  If FormatString is NULL, then ASSERT() and 0 is returned.
  If PcdMaximumAsciiStringLength is not zero, and FormatString contains more
  than PcdMaximumAsciiStringLength Ascii characters not including the
  Null-terminator, then ASSERT() and 0 is returned.

  @param[in]  FormatString    A Null-terminated ASCII format string.
  @param[in]  Marker          VA_LIST marker for the variable argument list.

  @return The number of characters that would be produced, not including the 
          Null-terminator.
**/
UINTN
EFIAPI
SPrintLengthAsciiFormat (
  IN  CONST CHAR8   *FormatString,
  IN  VA_LIST       Marker
  );

#endif
