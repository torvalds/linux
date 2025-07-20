// SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause
/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

/* The purpose of this file is to have a single list of error strings embedded in binary */

#include "error_private.h"

const char* ERR_getErrorString(ERR_enum code)
{
#ifdef ZSTD_STRIP_ERROR_STRINGS
    (void)code;
    return "Error strings stripped";
#else
    static const char* const notErrorCode = "Unspecified error code";
    switch( code )
    {
    case PREFIX(no_error): return "No error detected";
    case PREFIX(GENERIC):  return "Error (generic)";
    case PREFIX(prefix_unknown): return "Unknown frame descriptor";
    case PREFIX(version_unsupported): return "Version not supported";
    case PREFIX(frameParameter_unsupported): return "Unsupported frame parameter";
    case PREFIX(frameParameter_windowTooLarge): return "Frame requires too much memory for decoding";
    case PREFIX(corruption_detected): return "Data corruption detected";
    case PREFIX(checksum_wrong): return "Restored data doesn't match checksum";
    case PREFIX(literals_headerWrong): return "Header of Literals' block doesn't respect format specification";
    case PREFIX(parameter_unsupported): return "Unsupported parameter";
    case PREFIX(parameter_combination_unsupported): return "Unsupported combination of parameters";
    case PREFIX(parameter_outOfBound): return "Parameter is out of bound";
    case PREFIX(init_missing): return "Context should be init first";
    case PREFIX(memory_allocation): return "Allocation error : not enough memory";
    case PREFIX(workSpace_tooSmall): return "workSpace buffer is not large enough";
    case PREFIX(stage_wrong): return "Operation not authorized at current processing stage";
    case PREFIX(tableLog_tooLarge): return "tableLog requires too much memory : unsupported";
    case PREFIX(maxSymbolValue_tooLarge): return "Unsupported max Symbol Value : too large";
    case PREFIX(maxSymbolValue_tooSmall): return "Specified maxSymbolValue is too small";
    case PREFIX(cannotProduce_uncompressedBlock): return "This mode cannot generate an uncompressed block";
    case PREFIX(stabilityCondition_notRespected): return "pledged buffer stability condition is not respected";
    case PREFIX(dictionary_corrupted): return "Dictionary is corrupted";
    case PREFIX(dictionary_wrong): return "Dictionary mismatch";
    case PREFIX(dictionaryCreation_failed): return "Cannot create Dictionary from provided samples";
    case PREFIX(dstSize_tooSmall): return "Destination buffer is too small";
    case PREFIX(srcSize_wrong): return "Src size is incorrect";
    case PREFIX(dstBuffer_null): return "Operation on NULL destination buffer";
    case PREFIX(noForwardProgress_destFull): return "Operation made no progress over multiple calls, due to output buffer being full";
    case PREFIX(noForwardProgress_inputEmpty): return "Operation made no progress over multiple calls, due to input being empty";
        /* following error codes are not stable and may be removed or changed in a future version */
    case PREFIX(frameIndex_tooLarge): return "Frame index is too large";
    case PREFIX(seekableIO): return "An I/O error occurred when reading/seeking";
    case PREFIX(dstBuffer_wrong): return "Destination buffer is wrong";
    case PREFIX(srcBuffer_wrong): return "Source buffer is wrong";
    case PREFIX(sequenceProducer_failed): return "Block-level external sequence producer returned an error code";
    case PREFIX(externalSequences_invalid): return "External sequences are not valid";
    case PREFIX(maxCode):
    default: return notErrorCode;
    }
#endif
}
