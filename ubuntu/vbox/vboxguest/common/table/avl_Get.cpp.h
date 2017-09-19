/* $Id: avl_Get.cpp.h $ */
/** @file
 * kAVLGet - get routine for AVL trees.
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

#ifndef _kAVLGet_h_
#define _kAVLGet_h_


/**
 * Gets a node from the tree (does not remove it!)
 * @returns   Pointer to the node holding the given key.
 * @param     ppTree  Pointer to the AVL-tree root node pointer.
 * @param     Key     Key value of the node which is to be found.
 * @author    knut st. osmundsen
 */
KAVL_DECL(PKAVLNODECORE) KAVL_FN(Get)(PPKAVLNODECORE ppTree, KAVLKEY Key)
{
    register PKAVLNODECORE  pNode = KAVL_GET_POINTER_NULL(ppTree);

    if (pNode)
    {
        while (KAVL_NE(pNode->Key, Key))
        {
            if (KAVL_G(pNode->Key, Key))
            {
                if (pNode->pLeft != KAVL_NULL)
                    pNode = KAVL_GET_POINTER(&pNode->pLeft);
                else
                    return NULL;
            }
            else
            {
                if (pNode->pRight != KAVL_NULL)
                    pNode = KAVL_GET_POINTER(&pNode->pRight);
                else
                    return NULL;
            }
        }
    }

    return pNode;
}


#endif
