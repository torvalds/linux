/*
 * Copyright 2008-2012 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/*

 @File          error.c

 @Description   General errors and events reporting utilities.
*//***************************************************************************/
#if (defined(DEBUG_ERRORS) && (DEBUG_ERRORS > 0))
#include "error_ext.h"


const char *dbgLevelStrings[] =
{
     "CRITICAL"
    ,"MAJOR"
    ,"MINOR"
    ,"WARNING"
    ,"INFO"
    ,"TRACE"
};


char * ErrTypeStrings (e_ErrorType err)
{
    switch (err)
    {
        case (E_OK):                    return "OK";
        case (E_WRITE_FAILED):          return "Write Access Failed";
        case (E_NO_DEVICE):             return "No Device";
        case (E_NOT_AVAILABLE):         return "Resource Is Unavailable";
        case (E_NO_MEMORY):             return "Memory Allocation Failed";
        case (E_INVALID_ADDRESS):       return "Invalid Address";
        case (E_BUSY):                  return "Resource Is Busy";
        case (E_ALREADY_EXISTS):        return "Resource Already Exists";
        case (E_INVALID_OPERATION):     return "Invalid Operation";
        case (E_INVALID_VALUE):         return "Invalid Value";
        case (E_NOT_IN_RANGE):          return "Value Out Of Range";
        case (E_NOT_SUPPORTED):         return "Unsupported Operation";
        case (E_INVALID_STATE):         return "Invalid State";
        case (E_INVALID_HANDLE):        return "Invalid Handle";
        case (E_INVALID_ID):            return "Invalid ID";
        case (E_NULL_POINTER):          return "Unexpected NULL Pointer";
        case (E_INVALID_SELECTION):     return "Invalid Selection";
        case (E_INVALID_COMM_MODE):     return "Invalid Communication Mode";
        case (E_INVALID_MEMORY_TYPE):   return "Invalid Memory Type";
        case (E_INVALID_CLOCK):         return "Invalid Clock";
        case (E_CONFLICT):              return "Conflict In Settings";
        case (E_NOT_ALIGNED):           return "Incorrect Alignment";
        case (E_NOT_FOUND):             return "Resource Not Found";
        case (E_FULL):                  return "Resource Is Full";
        case (E_EMPTY):                 return "Resource Is Empty";
        case (E_ALREADY_FREE):          return "Resource Already Free";
        case (E_READ_FAILED):           return "Read Access Failed";
        case (E_INVALID_FRAME):         return "Invalid Frame";
        case (E_SEND_FAILED):           return "Send Operation Failed";
        case (E_RECEIVE_FAILED):        return "Receive Operation Failed";
        case (E_TIMEOUT):               return "Operation Timed Out";
        default:
            break;
    }
    return NULL;
}
#endif /* (defined(DEBUG_ERRORS) && (DEBUG_ERRORS > 0)) */
