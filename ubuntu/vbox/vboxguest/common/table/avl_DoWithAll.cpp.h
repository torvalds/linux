/* $Id: avl_DoWithAll.cpp.h $ */
/** @file
 * kAVLDoWithAll - Do with all nodes routine for AVL trees.
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

#ifndef _kAVLDoWithAll_h_
#define _kAVLDoWithAll_h_


/**
 * Iterates thru all nodes in the given tree.
 * @returns   0 on success. Return from callback on failure.
 * @param     ppTree   Pointer to the AVL-tree root node pointer.
 * @param     fFromLeft    TRUE:  Left to right.
 *                         FALSE: Right to left.
 * @param     pfnCallBack  Pointer to callback function.
 * @param     pvParam      Userparameter passed on to the callback function.
 */
KAVL_DECL(int) KAVL_FN(DoWithAll)(PPKAVLNODECORE ppTree, int fFromLeft, PKAVLCALLBACK pfnCallBack, void * pvParam)
{
    KAVLSTACK2      AVLStack;
    PKAVLNODECORE   pNode;
#ifdef KAVL_EQUAL_ALLOWED
    PKAVLNODECORE   pEqual;
#endif
    int             rc;

    if (*ppTree == KAVL_NULL)
        return VINF_SUCCESS;

    AVLStack.cEntries = 1;
    AVLStack.achFlags[0] = 0;
    AVLStack.aEntries[0] = KAVL_GET_POINTER(ppTree);

    if (fFromLeft)
    {   /* from left */
        while (AVLStack.cEntries > 0)
        {
            pNode = AVLStack.aEntries[AVLStack.cEntries - 1];

            /* left */
            if (!AVLStack.achFlags[AVLStack.cEntries - 1]++)
            {
                if (pNode->pLeft != KAVL_NULL)
                {
                    AVLStack.achFlags[AVLStack.cEntries] = 0; /* 0 first, 1 last */
                    AVLStack.aEntries[AVLStack.cEntries++] = KAVL_GET_POINTER(&pNode->pLeft);
                    continue;
                }
            }

            /* center */
            rc = pfnCallBack(pNode, pvParam);
            if (rc != VINF_SUCCESS)
                return rc;
#ifdef KAVL_EQUAL_ALLOWED
            if (pNode->pList != KAVL_NULL)
                for (pEqual = KAVL_GET_POINTER(&pNode->pList); pEqual; pEqual = KAVL_GET_POINTER_NULL(&pEqual->pList))
                {
                    rc = pfnCallBack(pEqual, pvParam);
                    if (rc != VINF_SUCCESS)
                        return rc;
                }
#endif

            /* right */
            AVLStack.cEntries--;
            if (pNode->pRight != KAVL_NULL)
            {
                AVLStack.achFlags[AVLStack.cEntries] = 0;
                AVLStack.aEntries[AVLStack.cEntries++] = KAVL_GET_POINTER(&pNode->pRight);
            }
        } /* while */
    }
    else
    {   /* from right */
        while (AVLStack.cEntries > 0)
        {
            pNode = AVLStack.aEntries[AVLStack.cEntries - 1];

            /* right */
            if (!AVLStack.achFlags[AVLStack.cEntries - 1]++)
            {
                if (pNode->pRight != KAVL_NULL)
                {
                    AVLStack.achFlags[AVLStack.cEntries] = 0;  /* 0 first, 1 last */
                    AVLStack.aEntries[AVLStack.cEntries++] = KAVL_GET_POINTER(&pNode->pRight);
                    continue;
                }
            }

            /* center */
            rc = pfnCallBack(pNode, pvParam);
            if (rc != VINF_SUCCESS)
                return rc;
#ifdef KAVL_EQUAL_ALLOWED
            if (pNode->pList != KAVL_NULL)
                for (pEqual = KAVL_GET_POINTER(&pNode->pList); pEqual; pEqual = KAVL_GET_POINTER_NULL(&pEqual->pList))
                {
                    rc = pfnCallBack(pEqual, pvParam);
                    if (rc != VINF_SUCCESS)
                        return rc;
                }
#endif

            /* left */
            AVLStack.cEntries--;
            if (pNode->pLeft != KAVL_NULL)
            {
                AVLStack.achFlags[AVLStack.cEntries] = 0;
                AVLStack.aEntries[AVLStack.cEntries++] = KAVL_GET_POINTER(&pNode->pLeft);
            }
        } /* while */
    }

    return VINF_SUCCESS;
}


#endif

