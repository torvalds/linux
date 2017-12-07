/* $Id: VBoxGuestR0LibSharedFolders.c $ */
/** @file
 * VBoxGuestR0LibSharedFolders - Ring 0 Shared Folders calls.
 */

/*
 * Copyright (C) 2006-2017 Oracle Corporation
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_SHARED_FOLDERS
#include "VBoxGuestR0LibInternal.h"
#include <VBox/VBoxGuestLibSharedFolders.h>
#include <VBox/log.h>
#include <iprt/time.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/string.h>

#ifdef VBGL_VBOXGUEST
# error "This file shouldn't be part of the VBoxGuestR0LibBase library that is linked into VBoxGuest.  It's client code."
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define SHFL_CPARMS_SET_UTF8 0
#define SHFL_CPARMS_SET_SYMLINKS 0

#define VBOX_INIT_CALL(a, b, c) \
    LogFunc(("%s, idClient=%d\n", "SHFL_FN_" # b, (c)->idClient)); \
    VBGL_HGCM_HDR_INIT(a, (c)->idClient, SHFL_FN_##b, SHFL_CPARMS_##b); \
    (a)->fInterruptible = false /* Currently we do like nfs with -o hard (default). */

#define VBOX_INIT_CALL_EX(a, b, c, a_cbReq) \
    LogFunc(("%s, idClient=%d\n", "SHFL_FN_" # b, (c)->idClient)); \
    VBGL_HGCM_HDR_INIT_EX(a, (c)->idClient, SHFL_FN_##b, SHFL_CPARMS_##b, a_cbReq); \
    (a)->fInterruptible = false /* Currently we do like nfs with -o hard (default). */



/** @todo We only need HGCM, not physical memory, so other guests should also
 *        switch to calling vbglR0HGCMInit() and vbglR0HGCMTerminate() instead
 *        of VbglR0SfInit() and VbglR0SfTerm(). */
#ifndef RT_OS_LINUX
DECLVBGL(int) VbglR0SfInit(void)
{
    return VbglR0InitClient();
}

DECLVBGL(void) VbglR0SfTerm(void)
{
    VbglR0TerminateClient();
}
#endif

DECLVBGL(int) VbglR0SfConnect(PVBGLSFCLIENT pClient)
{
    int rc = VbglR0HGCMConnect(&pClient->handle, "VBoxSharedFolders", &pClient->idClient);
    if (RT_SUCCESS(rc))
        LogFunc(("idClient=%d\n", pClient->idClient));
    else
        LogFunc(("VbglR0HGCMConnect failed -> rc=%Rrc\n", rc));
    return rc;
}

DECLVBGL(void) VbglR0SfDisconnect(PVBGLSFCLIENT pClient)
{
    int rc;
    LogFunc(("u32ClientID=%d\n", pClient->idClient));
    if (pClient->handle == NULL)
        return;                 /* not connected */

    rc = VbglR0HGCMDisconnect(pClient->handle, pClient->idClient);
    NOREF(rc);
/*    Log(("VBOXSF: VbglR0SfDisconnect: VbglR0HGCMDisconnect -> %#x\n", rc)); */
    pClient->idClient = 0;
    pClient->handle   = NULL;
    return;
}

DECLVBGL(int) VbglR0SfQueryMappings(PVBGLSFCLIENT pClient, SHFLMAPPING paMappings[], uint32_t *pcMappings)
{
    int rc;
    VBoxSFQueryMappings data;

    VBOX_INIT_CALL(&data.callInfo, QUERY_MAPPINGS, pClient);

    data.flags.type                      = VMMDevHGCMParmType_32bit;
    data.flags.u.value32                 = SHFL_MF_UCS2;

    data.numberOfMappings.type           = VMMDevHGCMParmType_32bit;
    data.numberOfMappings.u.value32      = *pcMappings;

    data.mappings.type                   = VMMDevHGCMParmType_LinAddr;
    data.mappings.u.Pointer.size         = sizeof(SHFLMAPPING) * *pcMappings;
    data.mappings.u.Pointer.u.linearAddr = (uintptr_t)&paMappings[0];

/*    Log(("VBOXSF: in ifs difference %d\n", (char *)&data.flags.type - (char *)&data.callInfo.cParms)); */
    rc = VbglR0HGCMCall(pClient->handle, &data.callInfo, sizeof(data));
/*    Log(("VBOXSF: VbglR0SfQueryMappings: VbglR0HGCMCall rc = %#x, result = %#x\n", rc, data.callInfo.result)); */
    if (RT_SUCCESS(rc))
        *pcMappings = data.numberOfMappings.u.value32;

    return rc;
}

DECLVBGL(int) VbglR0SfQueryMapName(PVBGLSFCLIENT pClient, SHFLROOT root, SHFLSTRING *pString, uint32_t size)
{
    int rc;
    VBoxSFQueryMapName data;

    VBOX_INIT_CALL(&data.callInfo, QUERY_MAP_NAME, pClient);

    data.root.type                   = VMMDevHGCMParmType_32bit;
    data.root.u.value32              = root;

    data.name.type                   = VMMDevHGCMParmType_LinAddr;
    data.name.u.Pointer.size         = size;
    data.name.u.Pointer.u.linearAddr = (uintptr_t)pString;

    rc = VbglR0HGCMCall(pClient->handle, &data.callInfo, sizeof(data));
/*    Log(("VBOXSF: VbglR0SfQueryMapName: VbglR0HGCMCall rc = %#x, result = %#x\n", rc, data.callInfo.Hdr.rc)); */
    return rc;
}

DECLVBGL(int) VbglR0SfMapFolder(PVBGLSFCLIENT pClient, PSHFLSTRING szFolderName, PVBGLSFMAP pMap)
{
    int rc;
    VBoxSFMapFolder data;

    VBOX_INIT_CALL(&data.callInfo, MAP_FOLDER, pClient);

    data.path.type                    = VMMDevHGCMParmType_LinAddr;
    data.path.u.Pointer.size          = ShflStringSizeOfBuffer(szFolderName);
    data.path.u.Pointer.u.linearAddr  = (uintptr_t)szFolderName;

    data.root.type                    = VMMDevHGCMParmType_32bit;
    data.root.u.value32               = 0;

    data.delimiter.type               = VMMDevHGCMParmType_32bit;
    data.delimiter.u.value32          = RTPATH_DELIMITER;

    data.fCaseSensitive.type          = VMMDevHGCMParmType_32bit;
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    data.fCaseSensitive.u.value32     = 0;
#else
    data.fCaseSensitive.u.value32     = 1;
#endif

    rc = VbglR0HGCMCallRaw(pClient->handle, &data.callInfo, sizeof(data));
/*    Log(("VBOXSF: VbglR0SfMapFolder: VbglR0HGCMCall rc = %#x, result = %#x\n", rc, data.callInfo.Hdr.rc)); */
    if (RT_SUCCESS(rc))
    {
        pMap->root = data.root.u.value32;
        rc         = data.callInfo.Hdr.rc;
    }
    return rc;
}

DECLVBGL(int) VbglR0SfUnmapFolder(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap)
{
    int rc;
    VBoxSFUnmapFolder data;

    VBOX_INIT_CALL(&data.callInfo, UNMAP_FOLDER, pClient);

    data.root.type                      = VMMDevHGCMParmType_32bit;
    data.root.u.value32                 = pMap->root;

    rc = VbglR0HGCMCall(pClient->handle, &data.callInfo, sizeof(data));
/*    Log(("VBOXSF: VbglR0SfUnmapFolder: VbglR0HGCMCall rc = %#x, result = %#x\n", rc, data.callInfo.Hdr.rc)); */
    return rc;
}

DECLVBGL(int) VbglR0SfCreate(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, PSHFLSTRING pParsedPath, PSHFLCREATEPARMS pCreateParms)
{
    /** @todo copy buffers to physical or mapped memory. */
    int rc;
    VBoxSFCreate data;

    VBOX_INIT_CALL(&data.callInfo, CREATE, pClient);

    data.root.type                    = VMMDevHGCMParmType_32bit;
    data.root.u.value32               = pMap->root;

    data.path.type                    = VMMDevHGCMParmType_LinAddr;
    data.path.u.Pointer.size          = ShflStringSizeOfBuffer (pParsedPath);
    data.path.u.Pointer.u.linearAddr  = (uintptr_t)pParsedPath;

    data.parms.type                   = VMMDevHGCMParmType_LinAddr;
    data.parms.u.Pointer.size         = sizeof(SHFLCREATEPARMS);
    data.parms.u.Pointer.u.linearAddr = (uintptr_t)pCreateParms;

    rc = VbglR0HGCMCall(pClient->handle, &data.callInfo, sizeof(data));
/*    Log(("VBOXSF: VbglR0SfCreate: VbglR0HGCMCall rc = %#x, result = %#x\n", rc, data.callInfo.Hdr.rc)); */
    return rc;
}

DECLVBGL(int) VbglR0SfClose(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, SHFLHANDLE Handle)
{
    int rc;
    VBoxSFClose data;

    VBOX_INIT_CALL(&data.callInfo, CLOSE, pClient);

    data.root.type                      = VMMDevHGCMParmType_32bit;
    data.root.u.value32                 = pMap->root;

    data.handle.type                    = VMMDevHGCMParmType_64bit;
    data.handle.u.value64               = Handle;

    rc = VbglR0HGCMCall(pClient->handle, &data.callInfo, sizeof(data));
/*    Log(("VBOXSF: VbglR0SfClose: VbglR0HGCMCall rc = %#x, result = %#x\n", rc, data.callInfo.Hdr.rc)); */
    return rc;
}

DECLVBGL(int) VbglR0SfRemove(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, PSHFLSTRING pParsedPath, uint32_t flags)
{
    int rc = VINF_SUCCESS;

    VBoxSFRemove data;

    VBOX_INIT_CALL(&data.callInfo, REMOVE, pClient);

    data.root.type                      = VMMDevHGCMParmType_32bit;
    data.root.u.value32                 = pMap->root;

    data.path.type                      = VMMDevHGCMParmType_LinAddr_In;
    data.path.u.Pointer.size            = ShflStringSizeOfBuffer(pParsedPath);
    data.path.u.Pointer.u.linearAddr    = (uintptr_t)pParsedPath;

    data.flags.type                     = VMMDevHGCMParmType_32bit;
    data.flags.u.value32                = flags;

    rc = VbglR0HGCMCall(pClient->handle, &data.callInfo, sizeof(data));
/*    Log(("VBOXSF: VbglR0SfRemove: VbglR0HGCMCall rc = %#x, result = %#x\n", rc, data.callInfo.Hdr.rc)); */
    return rc;
}

DECLVBGL(int) VbglR0SfRename(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, PSHFLSTRING pSrcPath, PSHFLSTRING pDestPath, uint32_t flags)
{
    int rc;
    VBoxSFRename data;

    VBOX_INIT_CALL(&data.callInfo, RENAME, pClient);

    data.root.type                      = VMMDevHGCMParmType_32bit;
    data.root.u.value32                 = pMap->root;

    data.src.type                       = VMMDevHGCMParmType_LinAddr_In;
    data.src.u.Pointer.size             = ShflStringSizeOfBuffer(pSrcPath);
    data.src.u.Pointer.u.linearAddr     = (uintptr_t)pSrcPath;

    data.dest.type                      = VMMDevHGCMParmType_LinAddr_In;
    data.dest.u.Pointer.size            = ShflStringSizeOfBuffer(pDestPath);
    data.dest.u.Pointer.u.linearAddr    = (uintptr_t)pDestPath;

    data.flags.type                     = VMMDevHGCMParmType_32bit;
    data.flags.u.value32                = flags;

    rc = VbglR0HGCMCall(pClient->handle, &data.callInfo, sizeof(data));
/*    Log(("VBOXSF: VbglR0SfRename: VbglR0HGCMCall rc = %#x, result = %#x\n", rc, data.callInfo.Hdr.rc)); */
    return rc;
}

DECLVBGL(int) VbglR0SfRead(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, SHFLHANDLE hFile,
                           uint64_t offset, uint32_t *pcbBuffer, uint8_t *pBuffer, bool fLocked)
{
    int rc;
    VBoxSFRead data;

    VBOX_INIT_CALL(&data.callInfo, READ, pClient);

    data.root.type                      = VMMDevHGCMParmType_32bit;
    data.root.u.value32                 = pMap->root;

    data.handle.type                    = VMMDevHGCMParmType_64bit;
    data.handle.u.value64               = hFile;
    data.offset.type                    = VMMDevHGCMParmType_64bit;
    data.offset.u.value64               = offset;
    data.cb.type                        = VMMDevHGCMParmType_32bit;
    data.cb.u.value32                   = *pcbBuffer;
    data.buffer.type                    = (fLocked) ? VMMDevHGCMParmType_LinAddr_Locked_Out : VMMDevHGCMParmType_LinAddr_Out;
    data.buffer.u.Pointer.size          = *pcbBuffer;
    data.buffer.u.Pointer.u.linearAddr  = (uintptr_t)pBuffer;

    rc = VbglR0HGCMCallRaw(pClient->handle, &data.callInfo, sizeof(data));
/*    Log(("VBOXSF: VbglR0SfRead: VbglR0HGCMCall rc = %#x, result = %#x\n", rc, data.callInfo.Hdr.rc)); */
    if (RT_SUCCESS(rc))
    {
        rc = data.callInfo.Hdr.rc;
        *pcbBuffer = data.cb.u.value32;
    }
    return rc;
}

DECLVBGL(int) VbglR0SfReadPageList(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, SHFLHANDLE hFile, uint64_t offset, uint32_t *pcbBuffer,
                                   uint16_t offFirstPage, uint16_t cPages, RTGCPHYS64 *paPages)
{
    uint32_t            cbToRead  = *pcbBuffer;
    uint32_t            cbData    = (uint32_t)(sizeof(VBoxSFRead) + RT_UOFFSETOF(HGCMPageListInfo, aPages[cPages]));
    VBoxSFRead         *pData     = (VBoxSFRead *)RTMemTmpAlloc(cbData);
    HGCMPageListInfo   *pPgLst    = (HGCMPageListInfo *)(pData + 1);
    uint16_t            iPage;
    int                 rc;

    if (RT_UNLIKELY(!pData))
        return VERR_NO_TMP_MEMORY;

    VBOX_INIT_CALL_EX(&pData->callInfo, READ, pClient, cbData);

    pData->root.type                      = VMMDevHGCMParmType_32bit;
    pData->root.u.value32                 = pMap->root;

    pData->handle.type                    = VMMDevHGCMParmType_64bit;
    pData->handle.u.value64               = hFile;
    pData->offset.type                    = VMMDevHGCMParmType_64bit;
    pData->offset.u.value64               = offset;
    pData->cb.type                        = VMMDevHGCMParmType_32bit;
    pData->cb.u.value32                   = cbToRead;
    pData->buffer.type                    = VMMDevHGCMParmType_PageList;
    pData->buffer.u.PageList.size         = cbToRead;
    pData->buffer.u.PageList.offset       = sizeof(VBoxSFRead);

    pPgLst->flags = VBOX_HGCM_F_PARM_DIRECTION_FROM_HOST;
    pPgLst->offFirstPage = offFirstPage;
    pPgLst->cPages = cPages;
    for (iPage = 0; iPage < cPages; iPage++)
        pPgLst->aPages[iPage] = paPages[iPage];

    rc = VbglR0HGCMCallRaw(pClient->handle, &pData->callInfo, cbData);
/*    Log(("VBOXSF: VbglR0SfReadPageList: VbglR0HGCMCall rc = %#x, result = %#x\n", rc, data.callInfo.Hdr.rc)); */
    if (RT_SUCCESS(rc))
    {
        rc = pData->callInfo.Hdr.rc;
        *pcbBuffer = pData->cb.u.value32;
    }

    RTMemTmpFree(pData);
    return rc;
}

DECLVBGL(int) VbglR0SfWrite(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, SHFLHANDLE hFile,
                            uint64_t offset, uint32_t *pcbBuffer, uint8_t *pBuffer, bool fLocked)
{
    int rc;
    VBoxSFWrite data;

    VBOX_INIT_CALL(&data.callInfo, WRITE, pClient);

    data.root.type                      = VMMDevHGCMParmType_32bit;
    data.root.u.value32                 = pMap->root;

    data.handle.type                    = VMMDevHGCMParmType_64bit;
    data.handle.u.value64               = hFile;
    data.offset.type                    = VMMDevHGCMParmType_64bit;
    data.offset.u.value64               = offset;
    data.cb.type                        = VMMDevHGCMParmType_32bit;
    data.cb.u.value32                   = *pcbBuffer;
    data.buffer.type                    = fLocked ? VMMDevHGCMParmType_LinAddr_Locked_In : VMMDevHGCMParmType_LinAddr_In;
    data.buffer.u.Pointer.size          = *pcbBuffer;
    data.buffer.u.Pointer.u.linearAddr  = (uintptr_t)pBuffer;

    rc = VbglR0HGCMCallRaw(pClient->handle, &data.callInfo, sizeof(data));
/*    Log(("VBOXSF: VbglR0SfWrite: VbglR0HGCMCall rc = %#x, result = %#x\n", rc, data.callInfo.Hdr.rc)); */
    if (RT_SUCCESS(rc))
    {
        rc = data.callInfo.Hdr.rc;
        *pcbBuffer = data.cb.u.value32;
    }
    return rc;
}

DECLVBGL(int) VbglR0SfWritePhysCont(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, SHFLHANDLE hFile, uint64_t offset,
                                    uint32_t *pcbBuffer, RTCCPHYS PhysBuffer)
{
    uint32_t            cbToWrite = *pcbBuffer;
    uint32_t            cPages    = RT_ALIGN_32((PhysBuffer & PAGE_OFFSET_MASK) + cbToWrite, PAGE_SIZE) >> PAGE_SHIFT;
    uint32_t            cbData    = (uint32_t)(sizeof(VBoxSFWrite) + RT_UOFFSETOF(HGCMPageListInfo, aPages[cPages]));
    VBoxSFWrite        *pData     = (VBoxSFWrite *)RTMemTmpAlloc(cbData);
    HGCMPageListInfo   *pPgLst    = (HGCMPageListInfo *)(pData + 1);
    uint32_t            iPage;
    int                 rc;

    if (RT_UNLIKELY(!pData))
        return VERR_NO_TMP_MEMORY;

    VBOX_INIT_CALL_EX(&pData->callInfo, WRITE, pClient, cbData);

    pData->root.type                      = VMMDevHGCMParmType_32bit;
    pData->root.u.value32                 = pMap->root;

    pData->handle.type                    = VMMDevHGCMParmType_64bit;
    pData->handle.u.value64               = hFile;
    pData->offset.type                    = VMMDevHGCMParmType_64bit;
    pData->offset.u.value64               = offset;
    pData->cb.type                        = VMMDevHGCMParmType_32bit;
    pData->cb.u.value32                   = cbToWrite;
    pData->buffer.type                    = VMMDevHGCMParmType_PageList;
    pData->buffer.u.PageList.size         = cbToWrite;
    pData->buffer.u.PageList.offset       = sizeof(VBoxSFWrite);

    pPgLst->flags = VBOX_HGCM_F_PARM_DIRECTION_TO_HOST;
    pPgLst->offFirstPage = (uint16_t)(PhysBuffer & PAGE_OFFSET_MASK);
    pPgLst->cPages = cPages;
    PhysBuffer &= ~(RTCCPHYS)PAGE_OFFSET_MASK;
    for (iPage = 0; iPage < cPages; iPage++, PhysBuffer += PAGE_SIZE)
        pPgLst->aPages[iPage] = PhysBuffer;

    rc = VbglR0HGCMCallRaw(pClient->handle, &pData->callInfo, cbData);
/*    Log(("VBOXSF: VbglR0SfWritePhysCont: VbglR0HGCMCall rc = %#x, result = %#x\n", rc, data.callInfo.Hdr.rc)); */
    if (RT_SUCCESS(rc))
    {
        rc = pData->callInfo.Hdr.rc;
        *pcbBuffer = pData->cb.u.value32;
    }

    RTMemTmpFree(pData);
    return rc;

}

DECLVBGL(int) VbglR0SfWritePageList(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, SHFLHANDLE hFile, uint64_t offset, uint32_t *pcbBuffer,
                                    uint16_t offFirstPage, uint16_t cPages, RTGCPHYS64 *paPages)
{
    uint32_t            cbToWrite = *pcbBuffer;
    uint32_t            cbData    = (uint32_t)(sizeof(VBoxSFWrite) + RT_UOFFSETOF(HGCMPageListInfo, aPages[cPages]));
    VBoxSFWrite        *pData     = (VBoxSFWrite *)RTMemTmpAlloc(cbData);
    HGCMPageListInfo   *pPgLst    = (HGCMPageListInfo *)(pData + 1);
    uint16_t            iPage;
    int                 rc;

    if (RT_UNLIKELY(!pData))
        return VERR_NO_TMP_MEMORY;

    VBOX_INIT_CALL_EX(&pData->callInfo, WRITE, pClient, cbData);

    pData->root.type                      = VMMDevHGCMParmType_32bit;
    pData->root.u.value32                 = pMap->root;

    pData->handle.type                    = VMMDevHGCMParmType_64bit;
    pData->handle.u.value64               = hFile;
    pData->offset.type                    = VMMDevHGCMParmType_64bit;
    pData->offset.u.value64               = offset;
    pData->cb.type                        = VMMDevHGCMParmType_32bit;
    pData->cb.u.value32                   = cbToWrite;
    pData->buffer.type                    = VMMDevHGCMParmType_PageList;
    pData->buffer.u.PageList.size         = cbToWrite;
    pData->buffer.u.PageList.offset       = sizeof(VBoxSFWrite);

    pPgLst->flags = VBOX_HGCM_F_PARM_DIRECTION_TO_HOST;
    pPgLst->offFirstPage = offFirstPage;
    pPgLst->cPages = cPages;
    for (iPage = 0; iPage < cPages; iPage++)
        pPgLst->aPages[iPage] = paPages[iPage];

    rc = VbglR0HGCMCallRaw(pClient->handle, &pData->callInfo, cbData);
/*    Log(("VBOXSF: VbglR0SfWritePageList: VbglR0HGCMCall rc = %#x, result = %#x\n", rc, data.callInfo.Hdr.rc)); */
    if (RT_SUCCESS(rc))
    {
        rc = pData->callInfo.Hdr.rc;
        *pcbBuffer = pData->cb.u.value32;
    }

    RTMemTmpFree(pData);
    return rc;
}

DECLVBGL(int) VbglR0SfFlush(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, SHFLHANDLE hFile)
{
    int rc;
    VBoxSFFlush data;

    VBOX_INIT_CALL(&data.callInfo, FLUSH, pClient);

    data.root.type                      = VMMDevHGCMParmType_32bit;
    data.root.u.value32                 = pMap->root;

    data.handle.type                    = VMMDevHGCMParmType_64bit;
    data.handle.u.value64               = hFile;

    rc = VbglR0HGCMCall(pClient->handle, &data.callInfo, sizeof(data));
/*    Log(("VBOXSF: VbglR0SfFlush: VbglR0HGCMCall rc = %#x, result = %#x\n", rc, data.callInfo.Hdr.rc)); */
    return rc;
}

DECLVBGL(int) VbglR0SfDirInfo(
    PVBGLSFCLIENT pClient,
    PVBGLSFMAP pMap,
    SHFLHANDLE hFile,
    PSHFLSTRING ParsedPath,
    uint32_t flags,
    uint32_t index,
    uint32_t *pcbBuffer,
    PSHFLDIRINFO pBuffer,
    uint32_t *pcFiles)
{
    int rc;
    VBoxSFList data;

    VBOX_INIT_CALL(&data.callInfo, LIST, pClient);

    data.root.type                      = VMMDevHGCMParmType_32bit;
    data.root.u.value32                 = pMap->root;

    data.handle.type                    = VMMDevHGCMParmType_64bit;
    data.handle.u.value64               = hFile;
    data.flags.type                     = VMMDevHGCMParmType_32bit;
    data.flags.u.value32                = flags;
    data.cb.type                        = VMMDevHGCMParmType_32bit;
    data.cb.u.value32                   = *pcbBuffer;
    data.path.type                      = VMMDevHGCMParmType_LinAddr_In;
    data.path.u.Pointer.size            = ParsedPath ? ShflStringSizeOfBuffer(ParsedPath) : 0;
    data.path.u.Pointer.u.linearAddr    = (uintptr_t) ParsedPath;

    data.buffer.type                    = VMMDevHGCMParmType_LinAddr_Out;
    data.buffer.u.Pointer.size          = *pcbBuffer;
    data.buffer.u.Pointer.u.linearAddr  = (uintptr_t)pBuffer;

    data.resumePoint.type               = VMMDevHGCMParmType_32bit;
    data.resumePoint.u.value32          = index;
    data.cFiles.type                    = VMMDevHGCMParmType_32bit;
    data.cFiles.u.value32               = 0; /* out parameters only */

    rc = VbglR0HGCMCall(pClient->handle, &data.callInfo, sizeof(data));
/*    Log(("VBOXSF: VbglR0SfDirInfo: rc = %#x, result = %#x\n", rc, data.callInfo.Hdr.rc)); */
    *pcbBuffer = data.cb.u.value32;
    *pcFiles   = data.cFiles.u.value32;
    return rc;
}

DECLVBGL(int) VbglR0SfFsInfo(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, SHFLHANDLE hFile,
                             uint32_t flags, uint32_t *pcbBuffer, PSHFLDIRINFO pBuffer)
{
    int rc;
    VBoxSFInformation data;

    VBOX_INIT_CALL(&data.callInfo, INFORMATION, pClient);

    data.root.type                      = VMMDevHGCMParmType_32bit;
    data.root.u.value32                 = pMap->root;

    data.handle.type                    = VMMDevHGCMParmType_64bit;
    data.handle.u.value64               = hFile;
    data.flags.type                     = VMMDevHGCMParmType_32bit;
    data.flags.u.value32                = flags;
    data.cb.type                        = VMMDevHGCMParmType_32bit;
    data.cb.u.value32                   = *pcbBuffer;
    data.info.type                      = VMMDevHGCMParmType_LinAddr;
    data.info.u.Pointer.size            = *pcbBuffer;
    data.info.u.Pointer.u.linearAddr    = (uintptr_t)pBuffer;

    rc = VbglR0HGCMCallRaw(pClient->handle, &data.callInfo, sizeof(data));
/*    Log(("VBOXSF: VbglR0SfFsInfo: VbglR0HGCMCall rc = %#x, result = %#x\n", rc, data.callInfo.Hdr.rc)); */
    if (RT_SUCCESS(rc))
    {
        rc = data.callInfo.Hdr.rc;
        *pcbBuffer = data.cb.u.value32;
    }
    return rc;
}

DECLVBGL(int) VbglR0SfLock(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, SHFLHANDLE hFile,
                           uint64_t offset, uint64_t cbSize, uint32_t fLock)
{
    int rc;
    VBoxSFLock data;

    VBOX_INIT_CALL(&data.callInfo, LOCK, pClient);

    data.root.type                      = VMMDevHGCMParmType_32bit;
    data.root.u.value32                 = pMap->root;

    data.handle.type                    = VMMDevHGCMParmType_64bit;
    data.handle.u.value64               = hFile;
    data.offset.type                    = VMMDevHGCMParmType_64bit;
    data.offset.u.value64               = offset;
    data.length.type                    = VMMDevHGCMParmType_64bit;
    data.length.u.value64               = cbSize;

    data.flags.type                     = VMMDevHGCMParmType_32bit;
    data.flags.u.value32                = fLock;

    rc = VbglR0HGCMCall(pClient->handle, &data.callInfo, sizeof(data));
/*    Log(("VBOXSF: VbglR0SfLock: VbglR0HGCMCall rc = %#x, result = %#x\n", rc, data.callInfo.Hdr.rc)); */
    return rc;
}

DECLVBGL(int) VbglR0SfSetUtf8(PVBGLSFCLIENT pClient)
{
    int rc;
    VBGLIOCHGCMCALL callInfo;

    VBOX_INIT_CALL(&callInfo, SET_UTF8, pClient);
    rc = VbglR0HGCMCall(pClient->handle, &callInfo, sizeof(callInfo));
/*    Log(("VBOXSF: VbglR0SfSetUtf8: VbglR0HGCMCall rc = %#x, result = %#x\n", rc, data.callInfo.Hdr.rc)); */
    return rc;
}

DECLVBGL(int) VbglR0SfReadLink(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, PSHFLSTRING pParsedPath, uint32_t cbBuffer, uint8_t *pBuffer)
{
    int rc;
    VBoxSFReadLink data;

    VBOX_INIT_CALL(&data.callInfo, READLINK, pClient);

    data.root.type                      = VMMDevHGCMParmType_32bit;
    data.root.u.value32                 = pMap->root;

    data.path.type                      = VMMDevHGCMParmType_LinAddr_In;
    data.path.u.Pointer.size            = ShflStringSizeOfBuffer (pParsedPath);
    data.path.u.Pointer.u.linearAddr    = (uintptr_t)pParsedPath;

    data.buffer.type                    = VMMDevHGCMParmType_LinAddr_Out;
    data.buffer.u.Pointer.size          = cbBuffer;
    data.buffer.u.Pointer.u.linearAddr  = (uintptr_t)pBuffer;

    rc = VbglR0HGCMCall(pClient->handle, &data.callInfo, sizeof(data));
/*    Log(("VBOXSF: VbglR0SfReadLink: VbglR0HGCMCall rc = %#x, result = %#x\n", rc, data.callInfo.Hdr.rc)); */
    return rc;
}

DECLVBGL(int) VbglR0SfSymlink(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, PSHFLSTRING pNewPath, PSHFLSTRING pOldPath,
                              PSHFLFSOBJINFO pBuffer)
{
    int rc;
    VBoxSFSymlink data;

    VBOX_INIT_CALL(&data.callInfo, SYMLINK, pClient);

    data.root.type                      = VMMDevHGCMParmType_32bit;
    data.root.u.value32                 = pMap->root;

    data.newPath.type                   = VMMDevHGCMParmType_LinAddr_In;
    data.newPath.u.Pointer.size         = ShflStringSizeOfBuffer (pNewPath);
    data.newPath.u.Pointer.u.linearAddr = (uintptr_t)pNewPath;

    data.oldPath.type                   = VMMDevHGCMParmType_LinAddr_In;
    data.oldPath.u.Pointer.size         = ShflStringSizeOfBuffer (pOldPath);
    data.oldPath.u.Pointer.u.linearAddr = (uintptr_t)pOldPath;

    data.info.type                      = VMMDevHGCMParmType_LinAddr_Out;
    data.info.u.Pointer.size            = sizeof(SHFLFSOBJINFO);
    data.info.u.Pointer.u.linearAddr    = (uintptr_t)pBuffer;

    rc = VbglR0HGCMCall(pClient->handle, &data.callInfo, sizeof(data));
/*    Log(("VBOXSF: VbglR0SfSymlink: VbglR0HGCMCall rc = %#x, result = %#x\n", rc, data.callInfo.Hdr.rc)); */
    return rc;
}

DECLVBGL(int) VbglR0SfSetSymlinks(PVBGLSFCLIENT pClient)
{
    int rc;
    VBGLIOCHGCMCALL callInfo;

    VBOX_INIT_CALL(&callInfo, SET_SYMLINKS, pClient);
    rc = VbglR0HGCMCall(pClient->handle, &callInfo, sizeof(callInfo));
/*    Log(("VBOXSF: VbglR0SfSetSymlinks: VbglR0HGCMCall rc = %#x, result = %#x\n", rc, data.callInfo.Hdr.rc)); */
    return rc;
}

