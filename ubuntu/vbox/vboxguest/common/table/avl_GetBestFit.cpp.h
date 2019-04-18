/* $Id: avl_GetBestFit.cpp.h $ */
/** @file
 * kAVLGetBestFit - Get Best Fit routine for AVL trees.
 *                  Intended specially on heaps. The tree should allow duplicate keys.
 *
 */

/*
 * Copyright (C) 2006-2019 Oracle Corporation
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

#ifndef _kAVLGetBestFit_h_
#define _kAVLGetBestFit_h_


/**
 * Finds the best fitting node in the tree for the given Key value.
 * @returns   Pointer to the best fitting node found.
 * @param     ppTree  Pointer to Pointer to the tree root node.
 * @param     Key     The Key of which is to be found a best fitting match for..
 * @param     fAbove  TRUE:  Returned node is have the closest key to Key from above.
 *                    FALSE: Returned node is have the closest key to Key from below.
 * @sketch    The best fitting node is always located in the searchpath above you.
 *            >= (above): The node where you last turned left.
 *            <= (below): the node where you last turned right.
 */
KAVL_DECL(PKAVLNODECORE) KAVL_FN(GetBestFit)(PPKAVLNODECORE ppTree, KAVLKEY Key, bool fAbove)
{
    PKAVLNODECORE  pNode = KAVL_GET_POINTER_NULL(ppTree);
    if (pNode)
    {
        PKAVLNODECORE           pNodeLast = NULL;
        if (fAbove)
        {   /* pNode->Key >= Key */
            while (KAVL_NE(pNode->Key, Key))
            {
                if (KAVL_G(pNode->Key, Key))
                {
                    if (pNode->pLeft != KAVL_NULL)
                    {
                        pNodeLast = pNode;
                        pNode = KAVL_GET_POINTER(&pNode->pLeft);
                    }
                    else
                        return pNode;
                }
                else
                {
                    if (pNode->pRight != KAVL_NULL)
                        pNode = KAVL_GET_POINTER(&pNode->pRight);
                    else
                        return pNodeLast;
                }
            }
        }
        else
        {   /* pNode->Key <= Key */
            while (KAVL_NE(pNode->Key, Key))
            {
                if (KAVL_G(pNode->Key, Key))
                {
                    if (pNode->pLeft != KAVL_NULL)
                        pNode = KAVL_GET_POINTER(&pNode->pLeft);
                    else
                        return pNodeLast;
                }
                else
                {
                    if (pNode->pRight != KAVL_NULL)
                    {
                        pNodeLast = pNode;
                        pNode = KAVL_GET_POINTER(&pNode->pRight);
                    }
                    else
                        return pNode;
                }
            }
        }
    }

    /* perfect match or nothing. */
    return pNode;
}


#endif
