/* $Id: VBoxGuestLibSharedFolders.h $ */
/** @file
 * VBoxGuestLib - Central calls header.
 */

/*
 * Copyright (C) 2006-2016 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */

#ifndef ___VBox_VBoxGuestLibSharedFolders_h_
#define ___VBox_VBoxGuestLibSharedFolders_h_

#include <VBox/VBoxGuestLib.h>
#include <VBox/shflsvc.h>

RT_C_DECLS_BEGIN


/** @addtogroup grp_vboxguest_lib_r0
 * @{
 */

typedef struct VBGLSFCLIENT
{
    HGCMCLIENTID idClient;
    VBGLHGCMHANDLE handle;
} VBGLSFCLIENT;
typedef VBGLSFCLIENT *PVBGLSFCLIENT;

typedef struct VBGLSFMAP
{
    SHFLROOT root;
} VBGLSFMAP, *PVBGLSFMAP;

DECLVBGL(int)  VbglR0SfInit(void);
DECLVBGL(void) VbglR0SfTerm(void);
DECLVBGL(int)  VbglR0SfConnect(PVBGLSFCLIENT pClient);
DECLVBGL(void) VbglR0SfDisconnect(PVBGLSFCLIENT pClient);

DECLVBGL(int)  VbglR0SfQueryMappings(PVBGLSFCLIENT pClient, SHFLMAPPING paMappings[], uint32_t *pcMappings);

DECLVBGL(int)  VbglR0SfQueryMapName(PVBGLSFCLIENT pClient, SHFLROOT root, SHFLSTRING *pString, uint32_t size);

/**
 * Create a new file or folder or open an existing one in a shared folder.  Proxies
 * to vbsfCreate in the host shared folder service.
 *
 * @returns IPRT status code, but see note below
 * @param   pClient      Host-guest communication connection
 * @param   pMap         The mapping for the shared folder in which the file
 *                       or folder is to be created
 * @param   pParsedPath  The path of the file or folder relative to the shared
 *                       folder
 * @param   pCreateParms Parameters for file/folder creation.  See the
 *                       structure description in shflsvc.h
 * @retval  pCreateParms See the structure description in shflsvc.h
 *
 * @note This function reports errors as follows.  The return value is always
 *       VINF_SUCCESS unless an exceptional condition occurs - out of
 *       memory, invalid arguments, etc.  If the file or folder could not be
 *       opened or created, pCreateParms->Handle will be set to
 *       SHFL_HANDLE_NIL on return.  In this case the value in
 *       pCreateParms->Result provides information as to why (e.g.
 *       SHFL_FILE_EXISTS).  pCreateParms->Result is also set on success
 *       as additional information.
 */
DECLVBGL(int)  VbglR0SfCreate(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, PSHFLSTRING pParsedPath, PSHFLCREATEPARMS pCreateParms);

DECLVBGL(int)  VbglR0SfClose(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, SHFLHANDLE Handle);
DECLVBGL(int)  VbglR0SfRemove(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, PSHFLSTRING pParsedPath, uint32_t flags);
DECLVBGL(int)  VbglR0SfRename(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, PSHFLSTRING pSrcPath, PSHFLSTRING pDestPath, uint32_t flags);
DECLVBGL(int)  VbglR0SfFlush(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, SHFLHANDLE hFile);

DECLVBGL(int)  VbglR0SfRead(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, SHFLHANDLE hFile, uint64_t offset, uint32_t *pcbBuffer, uint8_t *pBuffer, bool fLocked);
DECLVBGL(int)  VbglR0SfReadPageList(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, SHFLHANDLE hFile, uint64_t offset, uint32_t *pcbBuffer,
                                    uint16_t offFirstPage, uint16_t cPages, RTGCPHYS64 *paPages);
DECLVBGL(int)  VbglR0SfWrite(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, SHFLHANDLE hFile, uint64_t offset,
                             uint32_t *pcbBuffer,  uint8_t *pBuffer, bool fLocked);
DECLVBGL(int)  VbglR0SfWritePhysCont(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, SHFLHANDLE hFile, uint64_t offset,
                                     uint32_t *pcbBuffer, RTCCPHYS PhysBuffer);
DECLVBGL(int)  VbglR0SfWritePageList(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, SHFLHANDLE hFile, uint64_t offset, uint32_t *pcbBuffer,
                                     uint16_t offFirstPage, uint16_t cPages, RTGCPHYS64 *paPages);

DECLVBGL(int)  VbglR0SfLock(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, SHFLHANDLE hFile, uint64_t offset, uint64_t cbSize, uint32_t fLock);

DECLVBGL(int)  VbglR0SfDirInfo(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, SHFLHANDLE hFile,PSHFLSTRING ParsedPath, uint32_t flags,
                               uint32_t index, uint32_t *pcbBuffer, PSHFLDIRINFO pBuffer, uint32_t *pcFiles);
DECLVBGL(int)  VbglR0SfFsInfo(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, SHFLHANDLE hFile, uint32_t flags, uint32_t *pcbBuffer, PSHFLDIRINFO pBuffer);

DECLVBGL(int)  VbglR0SfMapFolder(PVBGLSFCLIENT pClient, PSHFLSTRING szFolderName, PVBGLSFMAP pMap);
DECLVBGL(int)  VbglR0SfUnmapFolder(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap);
DECLVBGL(int)  VbglR0SfSetUtf8(PVBGLSFCLIENT pClient);

DECLVBGL(int)  VbglR0SfReadLink(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, PSHFLSTRING ParsedPath, uint32_t pcbBuffer, uint8_t *pBuffer);
DECLVBGL(int)  VbglR0SfSymlink(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, PSHFLSTRING pNewPath, PSHFLSTRING pOldPath, PSHFLFSOBJINFO pBuffer);
DECLVBGL(int)  VbglR0SfSetSymlinks(PVBGLSFCLIENT pClient);

/** @} */

RT_C_DECLS_END

#endif

