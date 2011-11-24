//------------------------------------------------------------------------------
// <copyright file="main.c" company="Atheros">
//    Copyright (c) 2007-2008 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

   bus device load/unload utility

#notes: this utility will load/unload a built-in streams device driver instance.
        The caller invokes this using a set of command line switches.

        if -d is used, the utility enumerates either the user supplied registry path or
        a set of common registry paths for device instances.  The utility also dumps all 
        keys in \\Drivers\\Active for the user to view.
        
        if -x is used, the utility enumerates \\Drivers\\Active looking for a match of the
        supplied key token. A handle is obtained and the streams device instance is forced to
        unload.
        
        if -l is used, the utility calls ActivateDevice using the supplied key token
 
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#include <windows.h>
#include <windef.h>
#include <stdio.h>
#include <tchar.h>
#include <devload.h>

#define PRINT_CONSOLE(s) RETAILMSG(TRUE, s) 
    

/**+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
@function: PrintUsage() - print test application usage message

@input: none

@output: none

@return: none

+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
VOID PrintUsage() {
    PRINT_CONSOLE((TEXT("Bus enumerator unload usage: \r\n")));
    PRINT_CONSOLE((TEXT("    cedeviceloader [switch] [key token]\r\n"))); 
    PRINT_CONSOLE((TEXT("         [switch] must be -x,-l,-d \r\n\r\n"))); 
    PRINT_CONSOLE((TEXT("    load   ex: cedeviceloader -l Drivers\\BuiltIn\\PCI\\Instance\\mydeviceinstance \r\n"))); 
    PRINT_CONSOLE((TEXT("    unload ex: cedeviceloader -x Drivers\\BuiltIn\\PCI\\Instance \r\n"))); 
    PRINT_CONSOLE((TEXT("    dump   ex: cedeviceloader -d Drivers\\BuiltIn\\PCI\\Instance -or- \r\n"))); 
    PRINT_CONSOLE((TEXT("               cedeviceloader -d  \r\n"))); 

}

TCHAR *g_pKeyMatchString = NULL;
TCHAR *g_pStartPath = NULL;

BOOL g_Load = FALSE;

#define ACTIVE_BASE_PATH DEVLOAD_ACTIVE_KEY

#define MAX_REG_KEY_LENGTH 128


BOOL FindActiveMatchandUnload(PTCHAR pPath, PTCHAR pKeyMatch)
{
    BOOL found = FALSE;
    LONG  status;       /* reg api status */
    HKEY  hOpenKey = NULL;     /* opened key handle */
    DWORD hndValue;
    ULONG bufferSize;
    TCHAR keyMatchBuffer[MAX_REG_KEY_LENGTH];
    
    
    do {
        status = RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                              pPath,
                              0,
                              0,
                              &hOpenKey);
    
        if (status != ERROR_SUCCESS) {
            break;
        }
        
        bufferSize = sizeof(DWORD);
        
        status = RegQueryValueEx(hOpenKey,
                                 DEVLOAD_HANDLE_VALNAME,
                                 NULL,
                                 NULL,
                                 (PUCHAR)&hndValue,
                                 &bufferSize);
    
        
        
        if (status != ERROR_SUCCESS) {  
            break;    
        } 
        
        bufferSize = sizeof(keyMatchBuffer);
        
        status = RegQueryValueEx(hOpenKey,
                                 DEVLOAD_DEVKEY_VALNAME,
                                 NULL,
                                 NULL,
                                 (PUCHAR)&keyMatchBuffer,
                                 &bufferSize);
    
        
        
        if (status != ERROR_SUCCESS) {  
            break;    
        } 
        
        if (NULL == pKeyMatch) {
                /* output verbose if we are not matching */
            PRINT_CONSOLE((TEXT("       Found Key:%s, Hnd:0x%X\r\n"),keyMatchBuffer, hndValue)); 
        }   
    
        if (pKeyMatch != NULL) {
            if (_tcscmp(keyMatchBuffer,pKeyMatch) == 0) {
                PRINT_CONSOLE((TEXT("--- Unloading Driver: %s .... \r\n"),keyMatchBuffer));    
                found = TRUE;   
                DeactivateDevice((HANDLE)hndValue);
            }
        }
        
    } while (FALSE);
   
    if (hOpenKey != NULL) {
        RegCloseKey(hOpenKey);     
    } 
    
    return found;    
}


VOID DoEnum(TCHAR *pPath, PTCHAR pMatchForUnload)
{
    LONG   status;       /* reg api status */
    HKEY   hOpenKey;     /* opened key handle */
    DWORD  index;        /* enumeration index */
    DWORD  keyBufLength; /* key buffer length */ 
    TCHAR  enumKeyBuffer[MAX_REG_KEY_LENGTH];
    TCHAR  fullEnumPath[MAX_REG_KEY_LENGTH * 2];
    
    hOpenKey = NULL;
    
    if (NULL == pMatchForUnload) { 
        PRINT_CONSOLE((TEXT("Enumerating :%s \r\n"),pPath));
    }
    
    status = RegOpenKeyEx(HKEY_LOCAL_MACHINE ,
                          pPath,
                          0,
                          0,
                          &hOpenKey);
    index = 0;
    
        /* enumerate sub keys */
    while (ERROR_SUCCESS == status) {

        keyBufLength = MAX_REG_KEY_LENGTH * sizeof(TCHAR);

        status = RegEnumKeyEx(hOpenKey, 
                              index, 
                              enumKeyBuffer, 
                              &keyBufLength, 
                              NULL, 
                              NULL, 
                              NULL, 
                              NULL); 
                              
        if (ERROR_SUCCESS == status) {
            _tcscpy(fullEnumPath,pPath);
            _tcscat(fullEnumPath,TEXT("\\"));
            _tcscat(fullEnumPath,enumKeyBuffer);
            if (NULL == pMatchForUnload) { 
                PRINT_CONSOLE((TEXT("    Found :%s \r\n"),fullEnumPath));
            }
            if (FindActiveMatchandUnload(fullEnumPath,pMatchForUnload)) {
                break;    
            }
            index++;
        }
        
    }

    if (hOpenKey != NULL) {
        RegCloseKey(hOpenKey);
    }
        
}


/**+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
@function: _tmain() - test application main.

@input: argc - number of command line arguments (including application name)
@input: argv - command line argument strings

@return: application exit code

@notes:                         
                    
                    
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
VOID _tmain(int argc, TCHAR * argv[])
{
    BOOL dump = FALSE;
    BOOL badArgs = TRUE;
    
    do {
        /* get argument string */
        if (argc < 2) {
            break;   
        }
        
        if (_tcscmp(argv[1], TEXT("-x")) == 0) {
            g_Load = FALSE;    
            if (argc < 3) {
                break;
            }
            g_pKeyMatchString = argv[2];
        } else if(_tcscmp(argv[1], TEXT("-l")) == 0) {
            g_Load = TRUE;  
            if (argc < 3) {
                break;
            }  
            g_pStartPath = argv[2];
        } else if(_tcscmp(argv[1], TEXT("-d")) == 0) {
            dump = TRUE;        
        } else {
            break;   
        }
        
        badArgs = FALSE;
        
        if (dump) {
            if (argc > 2) {  
                    /* enumerate the key supplied by the user */
                DoEnum(argv[2], NULL);
            } else {
                    /* else enumerate some common ones */
                PRINT_CONSOLE((TEXT("******* Dumping PCI Instances...********** \r\n"))); 
                DoEnum(TEXT("Drivers\\BuiltIn\\PCI\\Instance"), NULL);
                PRINT_CONSOLE((TEXT("****************************************** \r\n")));       
            } 
            
            PRINT_CONSOLE((TEXT("******* Dumping Active Devices...********** \r\n"))); 
                /* enumerate the active path */
            DoEnum(ACTIVE_BASE_PATH, NULL);   
            PRINT_CONSOLE((TEXT("****************************************** \r\n")));  
            break;
        }    
        
        
        if (!g_Load) {
            DoEnum(ACTIVE_BASE_PATH,g_pKeyMatchString);        
        } else {
            PRINT_CONSOLE((TEXT("+++ Loading Driver: %s .... \r\n"),g_pStartPath));    
            ActivateDeviceEx(g_pStartPath,NULL,0,NULL);
        }
        
    } while (FALSE);
    
    if (badArgs) {
        PrintUsage();
        exit(-1);     
    }
    
    exit(0);
}

