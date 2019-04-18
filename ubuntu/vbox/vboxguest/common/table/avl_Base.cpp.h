/* $Id: avl_Base.cpp.h $ */
/** @file
 * kAVLBase - basic routines for all AVL trees.
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

#ifndef _kAVLBase_h_
#define _kAVLBase_h_


/** @page   pg_rt_kAVL kAVL Template configuration.
 * @internal
 *
 *  This is a template made to implement multiple AVL trees. The differences
 *  among the implementations are related to the key used.
 *
 *  \#define KAVL_FN
 *  Use this to alter the names of the AVL functions.
 *  Must be defined.
 *
 *  \#define KAVL_EQUAL_ALLOWED
 *  Define this to tell us that equal keys are allowed.
 *  Then Equal keys will be put in a list pointed to by pList in the KAVLNODECORE.
 *  This is by default not defined.
 *
 *  \#define KAVL_CHECK_FOR_EQUAL_INSERT
 *  Define this to enable insert check for equal nodes.
 *  This is by default not defined.
 *
 *  \#define KAVL_MAX_STACK
 *  Use this to specify the number of stack entries the stack will use when inserting
 *  and removing nodes from the tree. I think the size should be about
 *      log2(<max nodes>) + 3
 *  Must be defined.
 *
 */

/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
#define AVL_HEIGHTOF(pNode) ((unsigned char)((pNode) != NULL ? pNode->uchHeight : 0))

/** @def KAVL_GET_POINTER
 * Reads a 'pointer' value.
 *
 * @returns The native pointer.
 * @param   pp      Pointer to the pointer to read.
 */

/** @def KAVL_GET_POINTER_NULL
 * Reads a 'pointer' value which can be KAVL_NULL.
 *
 * @returns The native pointer.
 * @returns NULL pointer if KAVL_NULL.
 * @param   pp      Pointer to the pointer to read.
 */

/** @def KAVL_SET_POINTER
 * Writes a 'pointer' value.
 * For offset-based schemes offset relative to pp is calculated and assigned to *pp.
 *
 * @returns stored pointer.
 * @param   pp      Pointer to where to store the pointer.
 * @param   p       Native pointer to assign to *pp.
 */

/** @def KAVL_SET_POINTER_NULL
 * Writes a 'pointer' value which can be KAVL_NULL.
 *
 * For offset-based schemes offset relative to pp is calculated and assigned to *pp,
 * if p is not KAVL_NULL of course.
 *
 * @returns stored pointer.
 * @param   pp      Pointer to where to store the pointer.
 * @param   pp2     Pointer to where to pointer to assign to pp. This can be KAVL_NULL
 */

#ifndef KAVL_GET_POINTER
# ifdef KAVL_OFFSET
#  define KAVL_GET_POINTER(pp)              ( (PKAVLNODECORE)((intptr_t)(pp) + *(pp)) )
#  define KAVL_GET_POINTER_NULL(pp)         ( *(pp) != KAVL_NULL ? KAVL_GET_POINTER(pp) : NULL )
#  define KAVL_SET_POINTER(pp, p)           ( (*(pp)) = ((intptr_t)(p) - (intptr_t)(pp)) )
#  define KAVL_SET_POINTER_NULL(pp, pp2)    ( (*(pp)) = *(pp2) != KAVL_NULL ? (intptr_t)KAVL_GET_POINTER(pp2) - (intptr_t)(pp) : KAVL_NULL )
# else
#  define KAVL_GET_POINTER(pp)              ( *(pp) )
#  define KAVL_GET_POINTER_NULL(pp)         ( *(pp) )
#  define KAVL_SET_POINTER(pp, p)           ( (*(pp)) = (p) )
#  define KAVL_SET_POINTER_NULL(pp, pp2)    ( (*(pp)) = *(pp2) )
# endif
#endif


/** @def KAVL_NULL
 * The NULL 'pointer' equivalent.
 */
#ifndef KAVL_NULL
# ifdef KAVL_OFFSET
#  define KAVL_NULL     0
# else
#  define KAVL_NULL     NULL
# endif
#endif

#ifndef KAVL_RANGE
# define KAVL_R_IS_INTERSECTING(key1B, key2B, key1E, key2E) KAVL_E(key1B, key2B)
# define KAVL_R_IS_IDENTICAL(key1B, key2B, key1E, key2E)    KAVL_E(key1B, key2B)
#endif

/** @def KAVL_DECL
 * Function declation macro in the RTDECL tradition.
 * @param   a_Type      The function return type.  */
#ifndef KAVL_DECL
# define KAVL_DECL(a_Type)  RTDECL(a_Type)
#endif


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/*
 * A stack used to avoid recursive calls...
 */
typedef struct _kAvlStack
{
    unsigned        cEntries;
    PPKAVLNODECORE  aEntries[KAVL_MAX_STACK];
} KAVLSTACK, *PKAVLSTACK;

typedef struct _kAvlStack2
{
    unsigned        cEntries;
    PKAVLNODECORE   aEntries[KAVL_MAX_STACK];
    char            achFlags[KAVL_MAX_STACK];
} KAVLSTACK2, *PLAVLSTACK2;



/**
 * Rewinds a stack of pointers to pointers to nodes, rebalancing the tree.
 * @param     pStack  Pointer to stack to rewind.
 * @sketch    LOOP thru all stack entries
 *            BEGIN
 *                Get pointer to pointer to node (and pointer to node) from the stack.
 *                IF 2 higher left subtree than in right subtree THEN
 *                BEGIN
 *                    IF higher (or equal) left-sub-subtree than right-sub-subtree THEN
 *                                *                       n+2|n+3
 *                              /   \                     /     \
 *                            n+2    n       ==>         n+1   n+1|n+2
 *                           /   \                             /     \
 *                         n+1 n|n+1                          n|n+1  n
 *
 *                         Or with keys:
 *
 *                               4                           2
 *                             /   \                       /   \
 *                            2     5        ==>          1     4
 *                           / \                               / \
 *                          1   3                             3   5
 *
 *                    ELSE
 *                                *                         n+2
 *                              /   \                      /   \
 *                            n+2    n                   n+1   n+1
 *                           /   \           ==>        /  \   /  \
 *                          n    n+1                    n  L   R   n
 *                               / \
 *                              L   R
 *
 *                         Or with keys:
 *                               6                           4
 *                             /   \                       /   \
 *                            2     7        ==>          2     6
 *                          /   \                       /  \  /  \
 *                          1    4                      1  3  5  7
 *                              / \
 *                             3   5
 *                END
 *                ELSE IF 2 higher in right subtree than in left subtree THEN
 *                BEGIN
 *                    Same as above but left <==> right. (invert the picture)
 *                ELSE
 *                    IF correct height THEN break
 *                    ELSE correct height.
 *            END
 */
DECLINLINE(void) KAVL_FN(Rebalance)(PKAVLSTACK pStack)
{
    while (pStack->cEntries > 0)
    {
        /** @todo Perhaps some of these KAVL_SET_POINTER_NULL() cases could be optimized away.. */
        PPKAVLNODECORE   ppNode = pStack->aEntries[--pStack->cEntries];
        PKAVLNODECORE    pNode = KAVL_GET_POINTER(ppNode);
        PKAVLNODECORE    pLeftNode = KAVL_GET_POINTER_NULL(&pNode->pLeft);
        unsigned char    uchLeftHeight = AVL_HEIGHTOF(pLeftNode);
        PKAVLNODECORE    pRightNode = KAVL_GET_POINTER_NULL(&pNode->pRight);
        unsigned char    uchRightHeight = AVL_HEIGHTOF(pRightNode);

        if (uchRightHeight + 1 < uchLeftHeight)
        {
            PKAVLNODECORE    pLeftLeftNode = KAVL_GET_POINTER_NULL(&pLeftNode->pLeft);
            PKAVLNODECORE    pLeftRightNode = KAVL_GET_POINTER_NULL(&pLeftNode->pRight);
            unsigned char    uchLeftRightHeight = AVL_HEIGHTOF(pLeftRightNode);

            if (AVL_HEIGHTOF(pLeftLeftNode) >= uchLeftRightHeight)
            {
                KAVL_SET_POINTER_NULL(&pNode->pLeft, &pLeftNode->pRight);
                KAVL_SET_POINTER(&pLeftNode->pRight, pNode);
                pLeftNode->uchHeight = (unsigned char)(1 + (pNode->uchHeight = (unsigned char)(1 + uchLeftRightHeight)));
                KAVL_SET_POINTER(ppNode, pLeftNode);
            }
            else
            {
                KAVL_SET_POINTER_NULL(&pLeftNode->pRight, &pLeftRightNode->pLeft);
                KAVL_SET_POINTER_NULL(&pNode->pLeft, &pLeftRightNode->pRight);
                KAVL_SET_POINTER(&pLeftRightNode->pLeft, pLeftNode);
                KAVL_SET_POINTER(&pLeftRightNode->pRight, pNode);
                pLeftNode->uchHeight = pNode->uchHeight = uchLeftRightHeight;
                pLeftRightNode->uchHeight = uchLeftHeight;
                KAVL_SET_POINTER(ppNode, pLeftRightNode);
            }
        }
        else if (uchLeftHeight + 1 < uchRightHeight)
        {
            PKAVLNODECORE    pRightLeftNode = KAVL_GET_POINTER_NULL(&pRightNode->pLeft);
            unsigned char    uchRightLeftHeight = AVL_HEIGHTOF(pRightLeftNode);
            PKAVLNODECORE    pRightRightNode = KAVL_GET_POINTER_NULL(&pRightNode->pRight);

            if (AVL_HEIGHTOF(pRightRightNode) >= uchRightLeftHeight)
            {
                KAVL_SET_POINTER_NULL(&pNode->pRight, &pRightNode->pLeft);
                KAVL_SET_POINTER(&pRightNode->pLeft, pNode);
                pRightNode->uchHeight = (unsigned char)(1 + (pNode->uchHeight = (unsigned char)(1 + uchRightLeftHeight)));
                KAVL_SET_POINTER(ppNode, pRightNode);
            }
            else
            {
                KAVL_SET_POINTER_NULL(&pRightNode->pLeft, &pRightLeftNode->pRight);
                KAVL_SET_POINTER_NULL(&pNode->pRight, &pRightLeftNode->pLeft);
                KAVL_SET_POINTER(&pRightLeftNode->pRight, pRightNode);
                KAVL_SET_POINTER(&pRightLeftNode->pLeft, pNode);
                pRightNode->uchHeight = pNode->uchHeight = uchRightLeftHeight;
                pRightLeftNode->uchHeight = uchRightHeight;
                KAVL_SET_POINTER(ppNode, pRightLeftNode);
            }
        }
        else
        {
            unsigned char uchHeight = (unsigned char)(KMAX(uchLeftHeight, uchRightHeight) + 1);
            if (uchHeight == pNode->uchHeight)
                break;
            pNode->uchHeight = uchHeight;
        }
    }

}




/**
 * Inserts a node into the AVL-tree.
 * @returns   TRUE if inserted.
 *            FALSE if node exists in tree.
 * @param     ppTree  Pointer to the AVL-tree root node pointer.
 * @param     pNode   Pointer to the node which is to be added.
 * @sketch    Find the location of the node (using binary tree algorithm.):
 *            LOOP until KAVL_NULL leaf pointer
 *            BEGIN
 *                Add node pointer pointer to the AVL-stack.
 *                IF new-node-key < node key THEN
 *                    left
 *                ELSE
 *                    right
 *            END
 *            Fill in leaf node and insert it.
 *            Rebalance the tree.
 */
KAVL_DECL(bool) KAVL_FN(Insert)(PPKAVLNODECORE ppTree, PKAVLNODECORE pNode)
{
    KAVLSTACK                  AVLStack;
    PPKAVLNODECORE             ppCurNode = ppTree;
    PKAVLNODECORE              pCurNode;
    KAVLKEY                    Key = pNode->Key; NOREF(Key);
#ifdef KAVL_RANGE
    KAVLKEY                    KeyLast = pNode->KeyLast; NOREF(KeyLast);
#endif

    AVLStack.cEntries = 0;

#ifdef KAVL_RANGE
    if (Key > KeyLast)
        return false;
#endif

    for (;;)
    {
        if (*ppCurNode != KAVL_NULL)
            pCurNode = KAVL_GET_POINTER(ppCurNode);
        else
            break;

        kASSERT(AVLStack.cEntries < KAVL_MAX_STACK);
        AVLStack.aEntries[AVLStack.cEntries++] = ppCurNode;
#ifdef KAVL_EQUAL_ALLOWED
        if (KAVL_R_IS_IDENTICAL(pCurNode->Key, Key, pCurNode->KeyLast, KeyLast))
        {
            /*
             * If equal then we'll use a list of equal nodes.
             */
            pNode->pLeft = pNode->pRight = KAVL_NULL;
            pNode->uchHeight = 0;
            KAVL_SET_POINTER_NULL(&pNode->pList, &pCurNode->pList);
            KAVL_SET_POINTER(&pCurNode->pList, pNode);
            return true;
        }
#endif
#ifdef KAVL_CHECK_FOR_EQUAL_INSERT
        if (KAVL_R_IS_INTERSECTING(pCurNode->Key, Key, pCurNode->KeyLast, KeyLast))
            return false;
#endif
        if (KAVL_G(pCurNode->Key, Key))
            ppCurNode = &pCurNode->pLeft;
        else
            ppCurNode = &pCurNode->pRight;
    }

    pNode->pLeft = pNode->pRight = KAVL_NULL;
#ifdef KAVL_EQUAL_ALLOWED
    pNode->pList = KAVL_NULL;
#endif
    pNode->uchHeight = 1;
    KAVL_SET_POINTER(ppCurNode, pNode);

    KAVL_FN(Rebalance)(SSToDS(&AVLStack));
    return true;
}


/**
 * Removes a node from the AVL-tree.
 * @returns   Pointer to the node.
 * @param     ppTree  Pointer to the AVL-tree root node pointer.
 * @param     Key     Key value of the node which is to be removed.
 * @sketch    Find the node which is to be removed:
 *            LOOP until not found
 *            BEGIN
 *                Add node pointer pointer to the AVL-stack.
 *                IF the keys matches THEN break!
 *                IF remove key < node key THEN
 *                    left
 *                ELSE
 *                    right
 *            END
 *            IF found THEN
 *            BEGIN
 *                IF left node not empty THEN
 *                BEGIN
 *                    Find the right most node in the left tree while adding the pointer to the pointer to it's parent to the stack:
 *                    Start at left node.
 *                    LOOP until right node is empty
 *                    BEGIN
 *                        Add to stack.
 *                        go right.
 *                    END
 *                    Link out the found node.
 *                    Replace the node which is to be removed with the found node.
 *                    Correct the stack entry for the pointer to the left tree.
 *                END
 *                ELSE
 *                BEGIN
 *                    Move up right node.
 *                    Remove last stack entry.
 *                END
 *                Balance tree using stack.
 *            END
 *            return pointer to the removed node (if found).
 */
KAVL_DECL(PKAVLNODECORE) KAVL_FN(Remove)(PPKAVLNODECORE ppTree, KAVLKEY Key)
{
    KAVLSTACK        AVLStack;
    PPKAVLNODECORE   ppDeleteNode = ppTree;
    PKAVLNODECORE    pDeleteNode;

    AVLStack.cEntries = 0;

    for (;;)
    {
        if (*ppDeleteNode != KAVL_NULL)
            pDeleteNode = KAVL_GET_POINTER(ppDeleteNode);
        else
            return NULL;

        kASSERT(AVLStack.cEntries < KAVL_MAX_STACK);
        AVLStack.aEntries[AVLStack.cEntries++] = ppDeleteNode;
        if (KAVL_E(pDeleteNode->Key, Key))
            break;

        if (KAVL_G(pDeleteNode->Key, Key))
            ppDeleteNode = &pDeleteNode->pLeft;
        else
            ppDeleteNode = &pDeleteNode->pRight;
    }

    if (pDeleteNode->pLeft != KAVL_NULL)
    {
        /* find the rightmost node in the left tree. */
        const unsigned  iStackEntry = AVLStack.cEntries;
        PPKAVLNODECORE  ppLeftLeast = &pDeleteNode->pLeft;
        PKAVLNODECORE   pLeftLeast = KAVL_GET_POINTER(ppLeftLeast);

        while (pLeftLeast->pRight != KAVL_NULL)
        {
            kASSERT(AVLStack.cEntries < KAVL_MAX_STACK);
            AVLStack.aEntries[AVLStack.cEntries++] = ppLeftLeast;
            ppLeftLeast = &pLeftLeast->pRight;
            pLeftLeast  = KAVL_GET_POINTER(ppLeftLeast);
        }

        /* link out pLeftLeast */
        KAVL_SET_POINTER_NULL(ppLeftLeast, &pLeftLeast->pLeft);

        /* link it in place of the delete node. */
        KAVL_SET_POINTER_NULL(&pLeftLeast->pLeft, &pDeleteNode->pLeft);
        KAVL_SET_POINTER_NULL(&pLeftLeast->pRight, &pDeleteNode->pRight);
        pLeftLeast->uchHeight = pDeleteNode->uchHeight;
        KAVL_SET_POINTER(ppDeleteNode, pLeftLeast);
        AVLStack.aEntries[iStackEntry] = &pLeftLeast->pLeft;
    }
    else
    {
        KAVL_SET_POINTER_NULL(ppDeleteNode, &pDeleteNode->pRight);
        AVLStack.cEntries--;
    }

    KAVL_FN(Rebalance)(SSToDS(&AVLStack));
    return pDeleteNode;
}

#endif
