/** @file
 * IPRT - AVL Trees.
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

#ifndef ___iprt_avl_h
#define ___iprt_avl_h

#include <iprt/cdefs.h>
#include <iprt/types.h>

RT_C_DECLS_BEGIN

/** @defgroup grp_rt_avl    RTAvl - AVL Trees
 * @ingroup grp_rt
 * @{
 */


/** AVL tree of void pointers.
 * @{
 */

/**
 * AVL key type
 */
typedef void *  AVLPVKEY;

/**
 * AVL Core node.
 */
typedef struct _AVLPVNodeCore
{
    AVLPVKEY                Key;        /** Key value. */
    struct _AVLPVNodeCore  *pLeft;      /** Pointer to left leaf node. */
    struct _AVLPVNodeCore  *pRight;     /** Pointer to right leaf node. */
    unsigned char           uchHeight;  /** Height of this tree: max(height(left), height(right)) + 1 */
} AVLPVNODECORE, *PAVLPVNODECORE, **PPAVLPVNODECORE;

/** A tree with void pointer keys. */
typedef PAVLPVNODECORE     AVLPVTREE;
/** Pointer to a tree with void pointer keys. */
typedef PPAVLPVNODECORE    PAVLPVTREE;

/** Callback function for AVLPVDoWithAll().
 *  @returns IPRT status codes. */
typedef DECLCALLBACK(int) AVLPVCALLBACK(PAVLPVNODECORE, void *);
/** Pointer to callback function for AVLPVDoWithAll(). */
typedef AVLPVCALLBACK *PAVLPVCALLBACK;

/*
 * Functions.
 */
RTDECL(bool)            RTAvlPVInsert(PAVLPVTREE ppTree, PAVLPVNODECORE pNode);
RTDECL(PAVLPVNODECORE)  RTAvlPVRemove(PAVLPVTREE ppTree, AVLPVKEY Key);
RTDECL(PAVLPVNODECORE)  RTAvlPVGet(PAVLPVTREE ppTree, AVLPVKEY Key);
RTDECL(PAVLPVNODECORE)  RTAvlPVGetBestFit(PAVLPVTREE ppTree, AVLPVKEY Key, bool fAbove);
RTDECL(PAVLPVNODECORE)  RTAvlPVRemoveBestFit(PAVLPVTREE ppTree, AVLPVKEY Key, bool fAbove);
RTDECL(int)             RTAvlPVDoWithAll(PAVLPVTREE ppTree, int fFromLeft, PAVLPVCALLBACK pfnCallBack, void *pvParam);
RTDECL(int)             RTAvlPVDestroy(PAVLPVTREE ppTree, PAVLPVCALLBACK pfnCallBack, void *pvParam);

/** @} */


/** AVL tree of unsigned long.
 * @{
 */

/**
 * AVL key type
 */
typedef unsigned long   AVLULKEY;

/**
 * AVL Core node.
 */
typedef struct _AVLULNodeCore
{
    AVLULKEY                Key;        /** Key value. */
    struct _AVLULNodeCore  *pLeft;      /** Pointer to left leaf node. */
    struct _AVLULNodeCore  *pRight;     /** Pointer to right leaf node. */
    unsigned char           uchHeight;  /** Height of this tree: max(height(left), height(right)) + 1 */
} AVLULNODECORE, *PAVLULNODECORE, **PPAVLULNODECORE;


/** Callback function for AVLULDoWithAll().
 *  @returns IPRT status codes. */
typedef DECLCALLBACK(int) AVLULCALLBACK(PAVLULNODECORE, void*);
/** Pointer to callback function for AVLULDoWithAll(). */
typedef AVLULCALLBACK *PAVLULCALLBACK;


/*
 * Functions.
 */
RTDECL(bool)            RTAvlULInsert(PPAVLULNODECORE ppTree, PAVLULNODECORE pNode);
RTDECL(PAVLULNODECORE)  RTAvlULRemove(PPAVLULNODECORE ppTree, AVLULKEY Key);
RTDECL(PAVLULNODECORE)  RTAvlULGet(PPAVLULNODECORE ppTree, AVLULKEY Key);
RTDECL(PAVLULNODECORE)  RTAvlULGetBestFit(PPAVLULNODECORE ppTree, AVLULKEY Key, bool fAbove);
RTDECL(PAVLULNODECORE)  RTAvlULRemoveBestFit(PPAVLULNODECORE ppTree, AVLULKEY Key, bool fAbove);
RTDECL(int)             RTAvlULDoWithAll(PPAVLULNODECORE ppTree, int fFromLeft, PAVLULCALLBACK pfnCallBack, void *pvParam);
RTDECL(int)             RTAvlULDestroy(PPAVLULNODECORE pTree, PAVLULCALLBACK pfnCallBack, void *pvParam);

/** @} */



/** AVL tree of void pointer ranges.
 * @{
 */

/**
 * AVL key type
 */
typedef void *AVLRPVKEY;

/**
 * AVL Core node.
 */
typedef struct AVLRPVNodeCore
{
    AVLRPVKEY               Key;        /**< First key value in the range (inclusive). */
    AVLRPVKEY               KeyLast;    /**< Last key value in the range (inclusive). */
    struct AVLRPVNodeCore  *pLeft;      /**< Pointer to left leaf node. */
    struct AVLRPVNodeCore  *pRight;     /**< Pointer to right leaf node. */
    unsigned char           uchHeight;  /**< Height of this tree: max(height(left), height(right)) + 1 */
} AVLRPVNODECORE, *PAVLRPVNODECORE, **PPAVLRPVNODECORE;

/** A tree with void pointer keys. */
typedef PAVLRPVNODECORE    AVLRPVTREE;
/** Pointer to a tree with void pointer keys. */
typedef PPAVLRPVNODECORE   PAVLRPVTREE;

/** Callback function for AVLPVDoWithAll().
 *  @returns IPRT status codes. */
typedef DECLCALLBACK(int) AVLRPVCALLBACK(PAVLRPVNODECORE, void *);
/** Pointer to callback function for AVLPVDoWithAll(). */
typedef AVLRPVCALLBACK *PAVLRPVCALLBACK;

/*
 * Functions.
 */
RTDECL(bool)            RTAvlrPVInsert(PAVLRPVTREE ppTree, PAVLRPVNODECORE pNode);
RTDECL(PAVLRPVNODECORE) RTAvlrPVRemove(PAVLRPVTREE ppTree, AVLRPVKEY Key);
RTDECL(PAVLRPVNODECORE) RTAvlrPVGet(PAVLRPVTREE ppTree, AVLRPVKEY Key);
RTDECL(PAVLRPVNODECORE) RTAvlrPVRangeGet(PAVLRPVTREE ppTree, AVLRPVKEY Key);
RTDECL(PAVLRPVNODECORE) RTAvlrPVRangeRemove(PAVLRPVTREE ppTree, AVLRPVKEY Key);
RTDECL(PAVLRPVNODECORE) RTAvlrPVGetBestFit(PAVLRPVTREE ppTree, AVLRPVKEY Key, bool fAbove);
RTDECL(PAVLRPVNODECORE) RTAvlrPVRemoveBestFit(PAVLRPVTREE ppTree, AVLRPVKEY Key, bool fAbove);
RTDECL(int)             RTAvlrPVDoWithAll(PAVLRPVTREE ppTree, int fFromLeft, PAVLRPVCALLBACK pfnCallBack, void *pvParam);
RTDECL(int)             RTAvlrPVDestroy(PAVLRPVTREE ppTree, PAVLRPVCALLBACK pfnCallBack, void *pvParam);

/** @} */



/** AVL tree of uint32_t
 * @{
 */

/** AVL key type. */
typedef uint32_t    AVLU32KEY;

/** AVL Core node. */
typedef struct _AVLU32NodeCore
{
    struct _AVLU32NodeCore *pLeft;      /**< Pointer to left leaf node. */
    struct _AVLU32NodeCore *pRight;     /**< Pointer to right leaf node. */
    AVLU32KEY               Key;        /**< Key value. */
    unsigned char           uchHeight;  /**< Height of this tree: max(height(left), height(right)) + 1 */
} AVLU32NODECORE, *PAVLU32NODECORE, **PPAVLU32NODECORE;

/** A tree with void pointer keys. */
typedef PAVLU32NODECORE     AVLU32TREE;
/** Pointer to a tree with void pointer keys. */
typedef PPAVLU32NODECORE    PAVLU32TREE;

/** Callback function for AVLU32DoWithAll() & AVLU32Destroy().
 *  @returns IPRT status codes. */
typedef DECLCALLBACK(int) AVLU32CALLBACK(PAVLU32NODECORE, void*);
/** Pointer to callback function for AVLU32DoWithAll() & AVLU32Destroy(). */
typedef AVLU32CALLBACK *PAVLU32CALLBACK;


/*
 * Functions.
 */
RTDECL(bool)            RTAvlU32Insert(PAVLU32TREE pTree, PAVLU32NODECORE pNode);
RTDECL(PAVLU32NODECORE) RTAvlU32Remove(PAVLU32TREE pTree, AVLU32KEY Key);
RTDECL(PAVLU32NODECORE) RTAvlU32Get(PAVLU32TREE pTree, AVLU32KEY Key);
RTDECL(PAVLU32NODECORE) RTAvlU32GetBestFit(PAVLU32TREE pTree, AVLU32KEY Key, bool fAbove);
RTDECL(PAVLU32NODECORE) RTAvlU32RemoveBestFit(PAVLU32TREE pTree, AVLU32KEY Key, bool fAbove);
RTDECL(int)             RTAvlU32DoWithAll(PAVLU32TREE pTree, int fFromLeft, PAVLU32CALLBACK pfnCallBack, void *pvParam);
RTDECL(int)             RTAvlU32Destroy(PAVLU32TREE pTree, PAVLU32CALLBACK pfnCallBack, void *pvParam);

/** @} */

/**
 * AVL uint32_t type for the relative offset pointer scheme.
 */
typedef int32_t     AVLOU32;

typedef uint32_t     AVLOU32KEY;

/**
 * AVL Core node.
 */
typedef struct _AVLOU32NodeCore
{
    /** Key value. */
    AVLOU32KEY          Key;
    /** Offset to the left leaf node, relative to this field. */
    AVLOU32             pLeft;
    /** Offset to the right leaf node, relative to this field. */
    AVLOU32             pRight;
    /** Height of this tree: max(height(left), height(right)) + 1 */
    unsigned char       uchHeight;
} AVLOU32NODECORE, *PAVLOU32NODECORE;

/** A offset base tree with uint32_t keys. */
typedef AVLOU32         AVLOU32TREE;
/** Pointer to an offset base tree with uint32_t keys. */
typedef AVLOU32TREE    *PAVLOU32TREE;

/** Pointer to an internal tree pointer.
 * In this case it's a pointer to a relative offset. */
typedef AVLOU32TREE    *PPAVLOU32NODECORE;

/** Callback function for RTAvloU32DoWithAll().
 *  @returns IPRT status codes. */
typedef DECLCALLBACK(int)   AVLOU32CALLBACK(PAVLOU32NODECORE pNode, void *pvUser);
/** Pointer to callback function for RTAvloU32DoWithAll(). */
typedef AVLOU32CALLBACK *PAVLOU32CALLBACK;

RTDECL(bool)                  RTAvloU32Insert(PAVLOU32TREE pTree, PAVLOU32NODECORE pNode);
RTDECL(PAVLOU32NODECORE)      RTAvloU32Remove(PAVLOU32TREE pTree, AVLOU32KEY Key);
RTDECL(PAVLOU32NODECORE)      RTAvloU32Get(PAVLOU32TREE pTree, AVLOU32KEY Key);
RTDECL(int)                   RTAvloU32DoWithAll(PAVLOU32TREE pTree, int fFromLeft, PAVLOU32CALLBACK pfnCallBack, void *pvParam);
RTDECL(PAVLOU32NODECORE)      RTAvloU32GetBestFit(PAVLOU32TREE ppTree, AVLOU32KEY Key, bool fAbove);
RTDECL(PAVLOU32NODECORE)      RTAvloU32RemoveBestFit(PAVLOU32TREE ppTree, AVLOU32KEY Key, bool fAbove);
RTDECL(int)                   RTAvloU32Destroy(PAVLOU32TREE pTree, PAVLOU32CALLBACK pfnCallBack, void *pvParam);

/** @} */


/** AVL tree of uint32_t, list duplicates.
 * @{
 */

/** AVL key type. */
typedef uint32_t    AVLLU32KEY;

/** AVL Core node. */
typedef struct _AVLLU32NodeCore
{
    AVLLU32KEY                  Key;        /**< Key value. */
    unsigned char               uchHeight;  /**< Height of this tree: max(height(left), height(right)) + 1 */
    struct _AVLLU32NodeCore    *pLeft;      /**< Pointer to left leaf node. */
    struct _AVLLU32NodeCore    *pRight;     /**< Pointer to right leaf node. */
    struct _AVLLU32NodeCore    *pList;      /**< Pointer to next node with the same key. */
} AVLLU32NODECORE, *PAVLLU32NODECORE, **PPAVLLU32NODECORE;

/** Callback function for RTAvllU32DoWithAll() & RTAvllU32Destroy().
 *  @returns IPRT status codes. */
typedef DECLCALLBACK(int) AVLLU32CALLBACK(PAVLLU32NODECORE, void*);
/** Pointer to callback function for RTAvllU32DoWithAll() & RTAvllU32Destroy(). */
typedef AVLLU32CALLBACK *PAVLLU32CALLBACK;


/*
 * Functions.
 */
RTDECL(bool)                RTAvllU32Insert(PPAVLLU32NODECORE ppTree, PAVLLU32NODECORE pNode);
RTDECL(PAVLLU32NODECORE)    RTAvllU32Remove(PPAVLLU32NODECORE ppTree, AVLLU32KEY Key);
RTDECL(PAVLLU32NODECORE)    RTAvllU32RemoveNode(PPAVLLU32NODECORE ppTree, PAVLLU32NODECORE pNode);
RTDECL(PAVLLU32NODECORE)    RTAvllU32Get(PPAVLLU32NODECORE ppTree, AVLLU32KEY Key);
RTDECL(PAVLLU32NODECORE)    RTAvllU32GetBestFit(PPAVLLU32NODECORE ppTree, AVLLU32KEY Key, bool fAbove);
RTDECL(PAVLLU32NODECORE)    RTAvllU32RemoveBestFit(PPAVLLU32NODECORE ppTree, AVLLU32KEY Key, bool fAbove);
RTDECL(int)                 RTAvllU32DoWithAll(PPAVLLU32NODECORE ppTree, int fFromLeft, PAVLLU32CALLBACK pfnCallBack, void *pvParam);
RTDECL(int)                 RTAvllU32Destroy(PPAVLLU32NODECORE pTree, PAVLLU32CALLBACK pfnCallBack, void *pvParam);

/** @} */



/** AVL tree of uint64_t ranges.
 * @{
 */

/**
 * AVL key type
 */
typedef uint64_t AVLRU64KEY;

/**
 * AVL Core node.
 */
typedef struct AVLRU64NodeCore
{
    AVLRU64KEY               Key;        /**< First key value in the range (inclusive). */
    AVLRU64KEY               KeyLast;    /**< Last key value in the range (inclusive). */
    struct AVLRU64NodeCore  *pLeft;      /**< Pointer to left leaf node. */
    struct AVLRU64NodeCore  *pRight;     /**< Pointer to right leaf node. */
    unsigned char            uchHeight;  /**< Height of this tree: max(height(left), height(right)) + 1 */
} AVLRU64NODECORE, *PAVLRU64NODECORE, **PPAVLRU64NODECORE;

/** A tree with void pointer keys. */
typedef PAVLRU64NODECORE    AVLRU64TREE;
/** Pointer to a tree with void pointer keys. */
typedef PPAVLRU64NODECORE   PAVLRU64TREE;

/** Callback function for AVLRU64DoWithAll().
 *  @returns IPRT status codes. */
typedef DECLCALLBACK(int) AVLRU64CALLBACK(PAVLRU64NODECORE, void *);
/** Pointer to callback function for AVLU64DoWithAll(). */
typedef AVLRU64CALLBACK *PAVLRU64CALLBACK;

/*
 * Functions.
 */
RTDECL(bool)             RTAvlrU64Insert(PAVLRU64TREE ppTree, PAVLRU64NODECORE pNode);
RTDECL(PAVLRU64NODECORE) RTAvlrU64Remove(PAVLRU64TREE ppTree, AVLRU64KEY Key);
RTDECL(PAVLRU64NODECORE) RTAvlrU64Get(PAVLRU64TREE ppTree, AVLRU64KEY Key);
RTDECL(PAVLRU64NODECORE) RTAvlrU64RangeGet(PAVLRU64TREE ppTree, AVLRU64KEY Key);
RTDECL(PAVLRU64NODECORE) RTAvlrU64RangeRemove(PAVLRU64TREE ppTree, AVLRU64KEY Key);
RTDECL(PAVLRU64NODECORE) RTAvlrU64GetBestFit(PAVLRU64TREE ppTree, AVLRU64KEY Key, bool fAbove);
RTDECL(PAVLRU64NODECORE) RTAvlrU64RemoveBestFit(PAVLRU64TREE ppTree, AVLRU64KEY Key, bool fAbove);
RTDECL(int)              RTAvlrU64DoWithAll(PAVLRU64TREE ppTree, int fFromLeft, PAVLRU64CALLBACK pfnCallBack, void *pvParam);
RTDECL(int)              RTAvlrU64Destroy(PAVLRU64TREE ppTree, PAVLRU64CALLBACK pfnCallBack, void *pvParam);

/** @} */



/** AVL tree of RTGCPHYSes - using relative offsets internally.
 * @{
 */

/**
 * AVL 'pointer' type for the relative offset pointer scheme.
 */
typedef int32_t     AVLOGCPHYS;

/**
 * AVL Core node.
 */
typedef struct _AVLOGCPhysNodeCore
{
    /** Key value. */
    RTGCPHYS            Key;
    /** Offset to the left leaf node, relative to this field. */
    AVLOGCPHYS          pLeft;
    /** Offset to the right leaf node, relative to this field. */
    AVLOGCPHYS          pRight;
    /** Height of this tree: max(height(left), height(right)) + 1 */
    unsigned char       uchHeight;
    /** Padding */
    unsigned char       Padding[7];
} AVLOGCPHYSNODECORE, *PAVLOGCPHYSNODECORE;

/** A offset base tree with uint32_t keys. */
typedef AVLOGCPHYS         AVLOGCPHYSTREE;
/** Pointer to an offset base tree with uint32_t keys. */
typedef AVLOGCPHYSTREE    *PAVLOGCPHYSTREE;

/** Pointer to an internal tree pointer.
 * In this case it's a pointer to a relative offset. */
typedef AVLOGCPHYSTREE    *PPAVLOGCPHYSNODECORE;

/** Callback function for RTAvloGCPhysDoWithAll() and RTAvloGCPhysDestroy().
 *  @returns IPRT status codes. */
typedef DECLCALLBACK(int)   AVLOGCPHYSCALLBACK(PAVLOGCPHYSNODECORE pNode, void *pvUser);
/** Pointer to callback function for RTAvloGCPhysDoWithAll() and RTAvloGCPhysDestroy(). */
typedef AVLOGCPHYSCALLBACK *PAVLOGCPHYSCALLBACK;

RTDECL(bool)                    RTAvloGCPhysInsert(PAVLOGCPHYSTREE pTree, PAVLOGCPHYSNODECORE pNode);
RTDECL(PAVLOGCPHYSNODECORE)     RTAvloGCPhysRemove(PAVLOGCPHYSTREE pTree, RTGCPHYS Key);
RTDECL(PAVLOGCPHYSNODECORE)     RTAvloGCPhysGet(PAVLOGCPHYSTREE pTree, RTGCPHYS Key);
RTDECL(int)                     RTAvloGCPhysDoWithAll(PAVLOGCPHYSTREE pTree, int fFromLeft, PAVLOGCPHYSCALLBACK pfnCallBack, void *pvParam);
RTDECL(PAVLOGCPHYSNODECORE)     RTAvloGCPhysGetBestFit(PAVLOGCPHYSTREE ppTree, RTGCPHYS Key, bool fAbove);
RTDECL(PAVLOGCPHYSNODECORE)     RTAvloGCPhysRemoveBestFit(PAVLOGCPHYSTREE ppTree, RTGCPHYS Key, bool fAbove);
RTDECL(int)                     RTAvloGCPhysDestroy(PAVLOGCPHYSTREE pTree, PAVLOGCPHYSCALLBACK pfnCallBack, void *pvParam);

/** @} */


/** AVL tree of RTGCPHYS ranges - using relative offsets internally.
 * @{
 */

/**
 * AVL 'pointer' type for the relative offset pointer scheme.
 */
typedef int32_t     AVLROGCPHYS;

/**
 * AVL Core node.
 */
typedef struct _AVLROGCPhysNodeCore
{
    /** First key value in the range (inclusive). */
    RTGCPHYS            Key;
    /** Last key value in the range (inclusive). */
    RTGCPHYS            KeyLast;
    /** Offset to the left leaf node, relative to this field. */
    AVLROGCPHYS         pLeft;
    /** Offset to the right leaf node, relative to this field. */
    AVLROGCPHYS         pRight;
    /** Height of this tree: max(height(left), height(right)) + 1 */
    unsigned char       uchHeight;
    /** Padding */
    unsigned char       Padding[7];
} AVLROGCPHYSNODECORE, *PAVLROGCPHYSNODECORE;

/** A offset base tree with uint32_t keys. */
typedef AVLROGCPHYS         AVLROGCPHYSTREE;
/** Pointer to an offset base tree with uint32_t keys. */
typedef AVLROGCPHYSTREE    *PAVLROGCPHYSTREE;

/** Pointer to an internal tree pointer.
 * In this case it's a pointer to a relative offset. */
typedef AVLROGCPHYSTREE    *PPAVLROGCPHYSNODECORE;

/** Callback function for RTAvlroGCPhysDoWithAll() and RTAvlroGCPhysDestroy().
 *  @returns IPRT status codes. */
typedef DECLCALLBACK(int)   AVLROGCPHYSCALLBACK(PAVLROGCPHYSNODECORE pNode, void *pvUser);
/** Pointer to callback function for RTAvlroGCPhysDoWithAll() and RTAvlroGCPhysDestroy(). */
typedef AVLROGCPHYSCALLBACK *PAVLROGCPHYSCALLBACK;

RTDECL(bool)                    RTAvlroGCPhysInsert(PAVLROGCPHYSTREE pTree, PAVLROGCPHYSNODECORE pNode);
RTDECL(PAVLROGCPHYSNODECORE)    RTAvlroGCPhysRemove(PAVLROGCPHYSTREE pTree, RTGCPHYS Key);
RTDECL(PAVLROGCPHYSNODECORE)    RTAvlroGCPhysGet(PAVLROGCPHYSTREE pTree, RTGCPHYS Key);
RTDECL(PAVLROGCPHYSNODECORE)    RTAvlroGCPhysRangeGet(PAVLROGCPHYSTREE pTree, RTGCPHYS Key);
RTDECL(PAVLROGCPHYSNODECORE)    RTAvlroGCPhysRangeRemove(PAVLROGCPHYSTREE pTree, RTGCPHYS Key);
RTDECL(PAVLROGCPHYSNODECORE)    RTAvlroGCPhysGetBestFit(PAVLROGCPHYSTREE ppTree, RTGCPHYS Key, bool fAbove);
RTDECL(int)                     RTAvlroGCPhysDoWithAll(PAVLROGCPHYSTREE pTree, int fFromLeft, PAVLROGCPHYSCALLBACK pfnCallBack, void *pvParam);
RTDECL(int)                     RTAvlroGCPhysDestroy(PAVLROGCPHYSTREE pTree, PAVLROGCPHYSCALLBACK pfnCallBack, void *pvParam);
RTDECL(PAVLROGCPHYSNODECORE)    RTAvlroGCPhysGetRoot(PAVLROGCPHYSTREE pTree);
RTDECL(PAVLROGCPHYSNODECORE)    RTAvlroGCPhysGetLeft(PAVLROGCPHYSNODECORE pNode);
RTDECL(PAVLROGCPHYSNODECORE)    RTAvlroGCPhysGetRight(PAVLROGCPHYSNODECORE pNode);

/** @} */


/** AVL tree of RTGCPTRs.
 * @{
 */

/**
 * AVL Core node.
 */
typedef struct _AVLGCPtrNodeCore
{
    /** Key value. */
    RTGCPTR             Key;
    /** Pointer to the left node. */
    struct _AVLGCPtrNodeCore *pLeft;
    /** Pointer to the right node. */
    struct _AVLGCPtrNodeCore *pRight;
    /** Height of this tree: max(height(left), height(right)) + 1 */
    unsigned char       uchHeight;
} AVLGCPTRNODECORE, *PAVLGCPTRNODECORE, **PPAVLGCPTRNODECORE;

/** A tree of RTGCPTR keys. */
typedef PAVLGCPTRNODECORE     AVLGCPTRTREE;
/** Pointer to a tree of RTGCPTR keys. */
typedef PPAVLGCPTRNODECORE    PAVLGCPTRTREE;

/** Callback function for RTAvlGCPtrDoWithAll().
 *  @returns IPRT status codes. */
typedef DECLCALLBACK(int)   AVLGCPTRCALLBACK(PAVLGCPTRNODECORE pNode, void *pvUser);
/** Pointer to callback function for RTAvlGCPtrDoWithAll(). */
typedef AVLGCPTRCALLBACK *PAVLGCPTRCALLBACK;

RTDECL(bool)                    RTAvlGCPtrInsert(PAVLGCPTRTREE pTree, PAVLGCPTRNODECORE pNode);
RTDECL(PAVLGCPTRNODECORE)       RTAvlGCPtrRemove(PAVLGCPTRTREE pTree, RTGCPTR Key);
RTDECL(PAVLGCPTRNODECORE)       RTAvlGCPtrGet(PAVLGCPTRTREE pTree, RTGCPTR Key);
RTDECL(int)                     RTAvlGCPtrDoWithAll(PAVLGCPTRTREE pTree, int fFromLeft, PAVLGCPTRCALLBACK pfnCallBack, void *pvParam);
RTDECL(PAVLGCPTRNODECORE)       RTAvlGCPtrGetBestFit(PAVLGCPTRTREE ppTree, RTGCPTR Key, bool fAbove);
RTDECL(PAVLGCPTRNODECORE)       RTAvlGCPtrRemoveBestFit(PAVLGCPTRTREE ppTree, RTGCPTR Key, bool fAbove);
RTDECL(int)                     RTAvlGCPtrDestroy(PAVLGCPTRTREE pTree, PAVLGCPTRCALLBACK pfnCallBack, void *pvParam);

/** @} */


/** AVL tree of RTGCPTRs - using relative offsets internally.
 * @{
 */

/**
 * AVL 'pointer' type for the relative offset pointer scheme.
 */
typedef int32_t     AVLOGCPTR;

/**
 * AVL Core node.
 */
typedef struct _AVLOGCPtrNodeCore
{
    /** Key value. */
    RTGCPTR             Key;
    /** Offset to the left leaf node, relative to this field. */
    AVLOGCPTR           pLeft;
    /** Offset to the right leaf node, relative to this field. */
    AVLOGCPTR           pRight;
    /** Height of this tree: max(height(left), height(right)) + 1 */
    unsigned char       uchHeight;
    unsigned char       padding[GC_ARCH_BITS == 64 ? 7 : 3];
} AVLOGCPTRNODECORE, *PAVLOGCPTRNODECORE;

/** A offset base tree with uint32_t keys. */
typedef AVLOGCPTR         AVLOGCPTRTREE;
/** Pointer to an offset base tree with uint32_t keys. */
typedef AVLOGCPTRTREE    *PAVLOGCPTRTREE;

/** Pointer to an internal tree pointer.
 * In this case it's a pointer to a relative offset. */
typedef AVLOGCPTRTREE    *PPAVLOGCPTRNODECORE;

/** Callback function for RTAvloGCPtrDoWithAll().
 *  @returns IPRT status codes. */
typedef DECLCALLBACK(int)   AVLOGCPTRCALLBACK(PAVLOGCPTRNODECORE pNode, void *pvUser);
/** Pointer to callback function for RTAvloGCPtrDoWithAll(). */
typedef AVLOGCPTRCALLBACK *PAVLOGCPTRCALLBACK;

RTDECL(bool)                    RTAvloGCPtrInsert(PAVLOGCPTRTREE pTree, PAVLOGCPTRNODECORE pNode);
RTDECL(PAVLOGCPTRNODECORE)      RTAvloGCPtrRemove(PAVLOGCPTRTREE pTree, RTGCPTR Key);
RTDECL(PAVLOGCPTRNODECORE)      RTAvloGCPtrGet(PAVLOGCPTRTREE pTree, RTGCPTR Key);
RTDECL(int)                     RTAvloGCPtrDoWithAll(PAVLOGCPTRTREE pTree, int fFromLeft, PAVLOGCPTRCALLBACK pfnCallBack, void *pvParam);
RTDECL(PAVLOGCPTRNODECORE)      RTAvloGCPtrGetBestFit(PAVLOGCPTRTREE ppTree, RTGCPTR Key, bool fAbove);
RTDECL(PAVLOGCPTRNODECORE)      RTAvloGCPtrRemoveBestFit(PAVLOGCPTRTREE ppTree, RTGCPTR Key, bool fAbove);
RTDECL(int)                     RTAvloGCPtrDestroy(PAVLOGCPTRTREE pTree, PAVLOGCPTRCALLBACK pfnCallBack, void *pvParam);

/** @} */


/** AVL tree of RTGCPTR ranges.
 * @{
 */

/**
 * AVL Core node.
 */
typedef struct _AVLRGCPtrNodeCore
{
    /** First key value in the range (inclusive). */
    RTGCPTR             Key;
    /** Last key value in the range (inclusive). */
    RTGCPTR             KeyLast;
    /** Offset to the left leaf node, relative to this field. */
    struct _AVLRGCPtrNodeCore  *pLeft;
    /** Offset to the right leaf node, relative to this field. */
    struct _AVLRGCPtrNodeCore  *pRight;
    /** Height of this tree: max(height(left), height(right)) + 1 */
    unsigned char       uchHeight;
} AVLRGCPTRNODECORE, *PAVLRGCPTRNODECORE;

/** A offset base tree with RTGCPTR keys. */
typedef PAVLRGCPTRNODECORE AVLRGCPTRTREE;
/** Pointer to an offset base tree with RTGCPTR keys. */
typedef AVLRGCPTRTREE    *PAVLRGCPTRTREE;

/** Pointer to an internal tree pointer.
 * In this case it's a pointer to a relative offset. */
typedef AVLRGCPTRTREE    *PPAVLRGCPTRNODECORE;

/** Callback function for RTAvlrGCPtrDoWithAll() and RTAvlrGCPtrDestroy().
 *  @returns IPRT status codes. */
typedef DECLCALLBACK(int)   AVLRGCPTRCALLBACK(PAVLRGCPTRNODECORE pNode, void *pvUser);
/** Pointer to callback function for RTAvlrGCPtrDoWithAll() and RTAvlrGCPtrDestroy(). */
typedef AVLRGCPTRCALLBACK *PAVLRGCPTRCALLBACK;

RTDECL(bool)                   RTAvlrGCPtrInsert(       PAVLRGCPTRTREE pTree, PAVLRGCPTRNODECORE pNode);
RTDECL(PAVLRGCPTRNODECORE)     RTAvlrGCPtrRemove(       PAVLRGCPTRTREE pTree, RTGCPTR Key);
RTDECL(PAVLRGCPTRNODECORE)     RTAvlrGCPtrGet(          PAVLRGCPTRTREE pTree, RTGCPTR Key);
RTDECL(PAVLRGCPTRNODECORE)     RTAvlrGCPtrGetBestFit(   PAVLRGCPTRTREE pTree, RTGCPTR Key, bool fAbove);
RTDECL(PAVLRGCPTRNODECORE)     RTAvlrGCPtrRangeGet(     PAVLRGCPTRTREE pTree, RTGCPTR Key);
RTDECL(PAVLRGCPTRNODECORE)     RTAvlrGCPtrRangeRemove(  PAVLRGCPTRTREE pTree, RTGCPTR Key);
RTDECL(int)                    RTAvlrGCPtrDoWithAll(    PAVLRGCPTRTREE pTree, int fFromLeft, PAVLRGCPTRCALLBACK pfnCallBack, void *pvParam);
RTDECL(int)                    RTAvlrGCPtrDestroy(      PAVLRGCPTRTREE pTree, PAVLRGCPTRCALLBACK pfnCallBack, void *pvParam);
RTDECL(PAVLRGCPTRNODECORE)     RTAvlrGCPtrGetRoot(      PAVLRGCPTRTREE pTree);
RTDECL(PAVLRGCPTRNODECORE)     RTAvlrGCPtrGetLeft(      PAVLRGCPTRNODECORE pNode);
RTDECL(PAVLRGCPTRNODECORE)     RTAvlrGCPtrGetRight(     PAVLRGCPTRNODECORE pNode);

/** @} */


/** AVL tree of RTGCPTR ranges - using relative offsets internally.
 * @{
 */

/**
 * AVL 'pointer' type for the relative offset pointer scheme.
 */
typedef int32_t     AVLROGCPTR;

/**
 * AVL Core node.
 */
typedef struct _AVLROGCPtrNodeCore
{
    /** First key value in the range (inclusive). */
    RTGCPTR             Key;
    /** Last key value in the range (inclusive). */
    RTGCPTR             KeyLast;
    /** Offset to the left leaf node, relative to this field. */
    AVLROGCPTR          pLeft;
    /** Offset to the right leaf node, relative to this field. */
    AVLROGCPTR          pRight;
    /** Height of this tree: max(height(left), height(right)) + 1 */
    unsigned char       uchHeight;
    unsigned char       padding[GC_ARCH_BITS == 64 ? 7 : 7];
} AVLROGCPTRNODECORE, *PAVLROGCPTRNODECORE;

/** A offset base tree with uint32_t keys. */
typedef AVLROGCPTR         AVLROGCPTRTREE;
/** Pointer to an offset base tree with uint32_t keys. */
typedef AVLROGCPTRTREE    *PAVLROGCPTRTREE;

/** Pointer to an internal tree pointer.
 * In this case it's a pointer to a relative offset. */
typedef AVLROGCPTRTREE    *PPAVLROGCPTRNODECORE;

/** Callback function for RTAvlroGCPtrDoWithAll() and RTAvlroGCPtrDestroy().
 *  @returns IPRT status codes. */
typedef DECLCALLBACK(int)   AVLROGCPTRCALLBACK(PAVLROGCPTRNODECORE pNode, void *pvUser);
/** Pointer to callback function for RTAvlroGCPtrDoWithAll() and RTAvlroGCPtrDestroy(). */
typedef AVLROGCPTRCALLBACK *PAVLROGCPTRCALLBACK;

RTDECL(bool)                    RTAvlroGCPtrInsert(PAVLROGCPTRTREE pTree, PAVLROGCPTRNODECORE pNode);
RTDECL(PAVLROGCPTRNODECORE)     RTAvlroGCPtrRemove(PAVLROGCPTRTREE pTree, RTGCPTR Key);
RTDECL(PAVLROGCPTRNODECORE)     RTAvlroGCPtrGet(PAVLROGCPTRTREE pTree, RTGCPTR Key);
RTDECL(PAVLROGCPTRNODECORE)     RTAvlroGCPtrGetBestFit(PAVLROGCPTRTREE ppTree, RTGCPTR Key, bool fAbove);
RTDECL(PAVLROGCPTRNODECORE)     RTAvlroGCPtrRangeGet(PAVLROGCPTRTREE pTree, RTGCPTR Key);
RTDECL(PAVLROGCPTRNODECORE)     RTAvlroGCPtrRangeRemove(PAVLROGCPTRTREE pTree, RTGCPTR Key);
RTDECL(int)                     RTAvlroGCPtrDoWithAll(PAVLROGCPTRTREE pTree, int fFromLeft, PAVLROGCPTRCALLBACK pfnCallBack, void *pvParam);
RTDECL(int)                     RTAvlroGCPtrDestroy(PAVLROGCPTRTREE pTree, PAVLROGCPTRCALLBACK pfnCallBack, void *pvParam);
RTDECL(PAVLROGCPTRNODECORE)     RTAvlroGCPtrGetRoot(PAVLROGCPTRTREE pTree);
RTDECL(PAVLROGCPTRNODECORE)     RTAvlroGCPtrGetLeft(PAVLROGCPTRNODECORE pNode);
RTDECL(PAVLROGCPTRNODECORE)     RTAvlroGCPtrGetRight(PAVLROGCPTRNODECORE pNode);

/** @} */


/** AVL tree of RTGCPTR ranges (overlapping supported) - using relative offsets internally.
 * @{
 */

/**
 * AVL 'pointer' type for the relative offset pointer scheme.
 */
typedef int32_t     AVLROOGCPTR;

/**
 * AVL Core node.
 */
typedef struct _AVLROOGCPtrNodeCore
{
    /** First key value in the range (inclusive). */
    RTGCPTR             Key;
    /** Last key value in the range (inclusive). */
    RTGCPTR             KeyLast;
    /** Offset to the left leaf node, relative to this field. */
    AVLROOGCPTR         pLeft;
    /** Offset to the right leaf node, relative to this field. */
    AVLROOGCPTR         pRight;
    /** Pointer to the list of string with the same key. Don't touch. */
    AVLROOGCPTR         pList;
    /** Height of this tree: max(height(left), height(right)) + 1 */
    unsigned char       uchHeight;
} AVLROOGCPTRNODECORE, *PAVLROOGCPTRNODECORE;

/** A offset base tree with uint32_t keys. */
typedef AVLROOGCPTR         AVLROOGCPTRTREE;
/** Pointer to an offset base tree with uint32_t keys. */
typedef AVLROOGCPTRTREE    *PAVLROOGCPTRTREE;

/** Pointer to an internal tree pointer.
 * In this case it's a pointer to a relative offset. */
typedef AVLROOGCPTRTREE    *PPAVLROOGCPTRNODECORE;

/** Callback function for RTAvlrooGCPtrDoWithAll() and RTAvlrooGCPtrDestroy().
 *  @returns IPRT status codes. */
typedef DECLCALLBACK(int)   AVLROOGCPTRCALLBACK(PAVLROOGCPTRNODECORE pNode, void *pvUser);
/** Pointer to callback function for RTAvlrooGCPtrDoWithAll() and RTAvlrooGCPtrDestroy(). */
typedef AVLROOGCPTRCALLBACK *PAVLROOGCPTRCALLBACK;

RTDECL(bool)                    RTAvlrooGCPtrInsert(PAVLROOGCPTRTREE pTree, PAVLROOGCPTRNODECORE pNode);
RTDECL(PAVLROOGCPTRNODECORE)    RTAvlrooGCPtrRemove(PAVLROOGCPTRTREE pTree, RTGCPTR Key);
RTDECL(PAVLROOGCPTRNODECORE)    RTAvlrooGCPtrGet(PAVLROOGCPTRTREE pTree, RTGCPTR Key);
RTDECL(PAVLROOGCPTRNODECORE)    RTAvlrooGCPtrGetBestFit(PAVLROOGCPTRTREE ppTree, RTGCPTR Key, bool fAbove);
RTDECL(PAVLROOGCPTRNODECORE)    RTAvlrooGCPtrRangeGet(PAVLROOGCPTRTREE pTree, RTGCPTR Key);
RTDECL(PAVLROOGCPTRNODECORE)    RTAvlrooGCPtrRangeRemove(PAVLROOGCPTRTREE pTree, RTGCPTR Key);
RTDECL(int)                     RTAvlrooGCPtrDoWithAll(PAVLROOGCPTRTREE pTree, int fFromLeft, PAVLROOGCPTRCALLBACK pfnCallBack, void *pvParam);
RTDECL(int)                     RTAvlrooGCPtrDestroy(PAVLROOGCPTRTREE pTree, PAVLROOGCPTRCALLBACK pfnCallBack, void *pvParam);
RTDECL(PAVLROOGCPTRNODECORE)    RTAvlrooGCPtrGetRoot(PAVLROOGCPTRTREE pTree);
RTDECL(PAVLROOGCPTRNODECORE)    RTAvlrooGCPtrGetLeft(PAVLROOGCPTRNODECORE pNode);
RTDECL(PAVLROOGCPTRNODECORE)    RTAvlrooGCPtrGetRight(PAVLROOGCPTRNODECORE pNode);
RTDECL(PAVLROOGCPTRNODECORE)    RTAvlrooGCPtrGetNextEqual(PAVLROOGCPTRNODECORE pNode);

/** @} */


/** AVL tree of RTUINTPTR.
 * @{
 */

/**
 * AVL RTUINTPTR node core.
 */
typedef struct _AVLUIntPtrNodeCore
{
    /** Key value. */
    RTUINTPTR                   Key;
    /** Offset to the left leaf node, relative to this field. */
    struct _AVLUIntPtrNodeCore *pLeft;
    /** Offset to the right leaf node, relative to this field. */
    struct _AVLUIntPtrNodeCore *pRight;
    /** Height of this tree: max(height(left), height(right)) + 1 */
    unsigned char               uchHeight;
} AVLUINTPTRNODECORE;
/** Pointer to a RTUINTPTR AVL node core.*/
typedef AVLUINTPTRNODECORE *PAVLUINTPTRNODECORE;

/** A pointer based tree with RTUINTPTR keys. */
typedef PAVLUINTPTRNODECORE AVLUINTPTRTREE;
/** Pointer to an offset base tree with RTUINTPTR keys. */
typedef AVLUINTPTRTREE     *PAVLUINTPTRTREE;

/** Pointer to an internal tree pointer.
 * In this case it's a pointer to a pointer. */
typedef AVLUINTPTRTREE     *PPAVLUINTPTRNODECORE;

/** Callback function for RTAvlUIntPtrDoWithAll() and RTAvlUIntPtrDestroy().
 *  @returns IPRT status codes. */
typedef DECLCALLBACK(int)    AVLUINTPTRCALLBACK(PAVLUINTPTRNODECORE pNode, void *pvUser);
/** Pointer to callback function for RTAvlUIntPtrDoWithAll() and RTAvlUIntPtrDestroy(). */
typedef AVLUINTPTRCALLBACK *PAVLUINTPTRCALLBACK;

RTDECL(bool)                    RTAvlUIntPtrInsert(    PAVLUINTPTRTREE pTree, PAVLUINTPTRNODECORE pNode);
RTDECL(PAVLUINTPTRNODECORE)     RTAvlUIntPtrRemove(    PAVLUINTPTRTREE pTree, RTUINTPTR Key);
RTDECL(PAVLUINTPTRNODECORE)     RTAvlUIntPtrGet(       PAVLUINTPTRTREE pTree, RTUINTPTR Key);
RTDECL(PAVLUINTPTRNODECORE)     RTAvlUIntPtrGetBestFit(PAVLUINTPTRTREE pTree, RTUINTPTR Key, bool fAbove);
RTDECL(int)                     RTAvlUIntPtrDoWithAll( PAVLUINTPTRTREE pTree, int fFromLeft, PAVLUINTPTRCALLBACK pfnCallBack, void *pvParam);
RTDECL(int)                     RTAvlUIntPtrDestroy(   PAVLUINTPTRTREE pTree, PAVLUINTPTRCALLBACK pfnCallBack, void *pvParam);
RTDECL(PAVLUINTPTRNODECORE)     RTAvlUIntPtrGetRoot(   PAVLUINTPTRTREE pTree);
RTDECL(PAVLUINTPTRNODECORE)     RTAvlUIntPtrGetLeft(   PAVLUINTPTRNODECORE pNode);
RTDECL(PAVLUINTPTRNODECORE)     RTAvlUIntPtrGetRight(  PAVLUINTPTRNODECORE pNode);

/** @} */


/** AVL tree of RTUINTPTR ranges.
 * @{
 */

/**
 * AVL RTUINTPTR range node core.
 */
typedef struct _AVLRUIntPtrNodeCore
{
    /** First key value in the range (inclusive). */
    RTUINTPTR                       Key;
    /** Last key value in the range (inclusive). */
    RTUINTPTR                       KeyLast;
    /** Offset to the left leaf node, relative to this field. */
    struct _AVLRUIntPtrNodeCore    *pLeft;
    /** Offset to the right leaf node, relative to this field. */
    struct _AVLRUIntPtrNodeCore    *pRight;
    /** Height of this tree: max(height(left), height(right)) + 1 */
    unsigned char                   uchHeight;
} AVLRUINTPTRNODECORE;
/** Pointer to an AVL RTUINTPTR range node code. */
typedef AVLRUINTPTRNODECORE *PAVLRUINTPTRNODECORE;

/** A pointer based tree with RTUINTPTR ranges. */
typedef PAVLRUINTPTRNODECORE AVLRUINTPTRTREE;
/** Pointer to a pointer based tree with RTUINTPTR ranges. */
typedef AVLRUINTPTRTREE     *PAVLRUINTPTRTREE;

/** Pointer to an internal tree pointer.
 * In this case it's a pointer to a pointer. */
typedef AVLRUINTPTRTREE     *PPAVLRUINTPTRNODECORE;

/** Callback function for RTAvlrUIntPtrDoWithAll() and RTAvlrUIntPtrDestroy().
 *  @returns IPRT status codes. */
typedef DECLCALLBACK(int)    AVLRUINTPTRCALLBACK(PAVLRUINTPTRNODECORE pNode, void *pvUser);
/** Pointer to callback function for RTAvlrUIntPtrDoWithAll() and RTAvlrUIntPtrDestroy(). */
typedef AVLRUINTPTRCALLBACK *PAVLRUINTPTRCALLBACK;

RTDECL(bool)                   RTAvlrUIntPtrInsert(     PAVLRUINTPTRTREE pTree, PAVLRUINTPTRNODECORE pNode);
RTDECL(PAVLRUINTPTRNODECORE)   RTAvlrUIntPtrRemove(     PAVLRUINTPTRTREE pTree, RTUINTPTR Key);
RTDECL(PAVLRUINTPTRNODECORE)   RTAvlrUIntPtrGet(        PAVLRUINTPTRTREE pTree, RTUINTPTR Key);
RTDECL(PAVLRUINTPTRNODECORE)   RTAvlrUIntPtrGetBestFit( PAVLRUINTPTRTREE pTree, RTUINTPTR Key, bool fAbove);
RTDECL(PAVLRUINTPTRNODECORE)   RTAvlrUIntPtrRangeGet(   PAVLRUINTPTRTREE pTree, RTUINTPTR Key);
RTDECL(PAVLRUINTPTRNODECORE)   RTAvlrUIntPtrRangeRemove(PAVLRUINTPTRTREE pTree, RTUINTPTR Key);
RTDECL(int)                    RTAvlrUIntPtrDoWithAll(  PAVLRUINTPTRTREE pTree, int fFromLeft, PAVLRUINTPTRCALLBACK pfnCallBack, void *pvParam);
RTDECL(int)                    RTAvlrUIntPtrDestroy(    PAVLRUINTPTRTREE pTree, PAVLRUINTPTRCALLBACK pfnCallBack, void *pvParam);
RTDECL(PAVLRUINTPTRNODECORE)   RTAvlrUIntPtrGetRoot(    PAVLRUINTPTRTREE pTree);
RTDECL(PAVLRUINTPTRNODECORE)   RTAvlrUIntPtrGetLeft(    PAVLRUINTPTRNODECORE pNode);
RTDECL(PAVLRUINTPTRNODECORE)   RTAvlrUIntPtrGetRight(   PAVLRUINTPTRNODECORE pNode);

/** @} */


/** AVL tree of RTHCPHYSes - using relative offsets internally.
 * @{
 */

/**
 * AVL 'pointer' type for the relative offset pointer scheme.
 */
typedef int32_t     AVLOHCPHYS;

/**
 * AVL Core node.
 */
typedef struct _AVLOHCPhysNodeCore
{
    /** Key value. */
    RTHCPHYS            Key;
    /** Offset to the left leaf node, relative to this field. */
    AVLOHCPHYS          pLeft;
    /** Offset to the right leaf node, relative to this field. */
    AVLOHCPHYS          pRight;
    /** Height of this tree: max(height(left), height(right)) + 1 */
    unsigned char       uchHeight;
#if HC_ARCH_BITS == 64 || GC_ARCH_BITS == 64
    unsigned char       Padding[7]; /**< Alignment padding. */
#endif
} AVLOHCPHYSNODECORE, *PAVLOHCPHYSNODECORE;

/** A offset base tree with uint32_t keys. */
typedef AVLOHCPHYS         AVLOHCPHYSTREE;
/** Pointer to an offset base tree with uint32_t keys. */
typedef AVLOHCPHYSTREE    *PAVLOHCPHYSTREE;

/** Pointer to an internal tree pointer.
 * In this case it's a pointer to a relative offset. */
typedef AVLOHCPHYSTREE    *PPAVLOHCPHYSNODECORE;

/** Callback function for RTAvloHCPhysDoWithAll() and RTAvloHCPhysDestroy().
 *  @returns IPRT status codes. */
typedef DECLCALLBACK(int)   AVLOHCPHYSCALLBACK(PAVLOHCPHYSNODECORE pNode, void *pvUser);
/** Pointer to callback function for RTAvloHCPhysDoWithAll() and RTAvloHCPhysDestroy(). */
typedef AVLOHCPHYSCALLBACK *PAVLOHCPHYSCALLBACK;

RTDECL(bool)                    RTAvloHCPhysInsert(PAVLOHCPHYSTREE pTree, PAVLOHCPHYSNODECORE pNode);
RTDECL(PAVLOHCPHYSNODECORE)     RTAvloHCPhysRemove(PAVLOHCPHYSTREE pTree, RTHCPHYS Key);
RTDECL(PAVLOHCPHYSNODECORE)     RTAvloHCPhysGet(PAVLOHCPHYSTREE pTree, RTHCPHYS Key);
RTDECL(int)                     RTAvloHCPhysDoWithAll(PAVLOHCPHYSTREE pTree, int fFromLeft, PAVLOHCPHYSCALLBACK pfnCallBack, void *pvParam);
RTDECL(PAVLOHCPHYSNODECORE)     RTAvloHCPhysGetBestFit(PAVLOHCPHYSTREE ppTree, RTHCPHYS Key, bool fAbove);
RTDECL(PAVLOHCPHYSNODECORE)     RTAvloHCPhysRemoveBestFit(PAVLOHCPHYSTREE ppTree, RTHCPHYS Key, bool fAbove);
RTDECL(int)                     RTAvloHCPhysDestroy(PAVLOHCPHYSTREE pTree, PAVLOHCPHYSCALLBACK pfnCallBack, void *pvParam);

/** @} */



/** AVL tree of RTIOPORTs - using relative offsets internally.
 * @{
 */

/**
 * AVL 'pointer' type for the relative offset pointer scheme.
 */
typedef int32_t     AVLOIOPORTPTR;

/**
 * AVL Core node.
 */
typedef struct _AVLOIOPortNodeCore
{
    /** Offset to the left leaf node, relative to this field. */
    AVLOIOPORTPTR       pLeft;
    /** Offset to the right leaf node, relative to this field. */
    AVLOIOPORTPTR       pRight;
    /** Key value. */
    RTIOPORT            Key;
    /** Height of this tree: max(height(left), height(right)) + 1 */
    unsigned char       uchHeight;
} AVLOIOPORTNODECORE, *PAVLOIOPORTNODECORE;

/** A offset base tree with uint32_t keys. */
typedef AVLOIOPORTPTR      AVLOIOPORTTREE;
/** Pointer to an offset base tree with uint32_t keys. */
typedef AVLOIOPORTTREE    *PAVLOIOPORTTREE;

/** Pointer to an internal tree pointer.
 * In this case it's a pointer to a relative offset. */
typedef AVLOIOPORTTREE    *PPAVLOIOPORTNODECORE;

/** Callback function for RTAvloIOPortDoWithAll() and RTAvloIOPortDestroy().
 *  @returns IPRT status codes. */
typedef DECLCALLBACK(int)   AVLOIOPORTCALLBACK(PAVLOIOPORTNODECORE pNode, void *pvUser);
/** Pointer to callback function for RTAvloIOPortDoWithAll() and RTAvloIOPortDestroy(). */
typedef AVLOIOPORTCALLBACK *PAVLOIOPORTCALLBACK;

RTDECL(bool)                    RTAvloIOPortInsert(PAVLOIOPORTTREE pTree, PAVLOIOPORTNODECORE pNode);
RTDECL(PAVLOIOPORTNODECORE)     RTAvloIOPortRemove(PAVLOIOPORTTREE pTree, RTIOPORT Key);
RTDECL(PAVLOIOPORTNODECORE)     RTAvloIOPortGet(PAVLOIOPORTTREE pTree, RTIOPORT Key);
RTDECL(int)                     RTAvloIOPortDoWithAll(PAVLOIOPORTTREE pTree, int fFromLeft, PAVLOIOPORTCALLBACK pfnCallBack, void *pvParam);
RTDECL(PAVLOIOPORTNODECORE)     RTAvloIOPortGetBestFit(PAVLOIOPORTTREE ppTree, RTIOPORT Key, bool fAbove);
RTDECL(PAVLOIOPORTNODECORE)     RTAvloIOPortRemoveBestFit(PAVLOIOPORTTREE ppTree, RTIOPORT Key, bool fAbove);
RTDECL(int)                     RTAvloIOPortDestroy(PAVLOIOPORTTREE pTree, PAVLOIOPORTCALLBACK pfnCallBack, void *pvParam);

/** @} */


/** AVL tree of RTIOPORT ranges - using relative offsets internally.
 * @{
 */

/**
 * AVL 'pointer' type for the relative offset pointer scheme.
 */
typedef int32_t     AVLROIOPORTPTR;

/**
 * AVL Core node.
 */
typedef struct _AVLROIOPortNodeCore
{
    /** First key value in the range (inclusive). */
    RTIOPORT            Key;
    /** Last key value in the range (inclusive). */
    RTIOPORT            KeyLast;
    /** Offset to the left leaf node, relative to this field. */
    AVLROIOPORTPTR      pLeft;
    /** Offset to the right leaf node, relative to this field. */
    AVLROIOPORTPTR      pRight;
    /** Height of this tree: max(height(left), height(right)) + 1 */
    unsigned char       uchHeight;
} AVLROIOPORTNODECORE, *PAVLROIOPORTNODECORE;

/** A offset base tree with uint32_t keys. */
typedef AVLROIOPORTPTR      AVLROIOPORTTREE;
/** Pointer to an offset base tree with uint32_t keys. */
typedef AVLROIOPORTTREE    *PAVLROIOPORTTREE;

/** Pointer to an internal tree pointer.
 * In this case it's a pointer to a relative offset. */
typedef AVLROIOPORTTREE    *PPAVLROIOPORTNODECORE;

/** Callback function for RTAvlroIOPortDoWithAll() and RTAvlroIOPortDestroy().
 *  @returns IPRT status codes. */
typedef DECLCALLBACK(int)   AVLROIOPORTCALLBACK(PAVLROIOPORTNODECORE pNode, void *pvUser);
/** Pointer to callback function for RTAvlroIOPortDoWithAll() and RTAvlroIOPortDestroy(). */
typedef AVLROIOPORTCALLBACK *PAVLROIOPORTCALLBACK;

RTDECL(bool)                    RTAvlroIOPortInsert(PAVLROIOPORTTREE pTree, PAVLROIOPORTNODECORE pNode);
RTDECL(PAVLROIOPORTNODECORE)    RTAvlroIOPortRemove(PAVLROIOPORTTREE pTree, RTIOPORT Key);
RTDECL(PAVLROIOPORTNODECORE)    RTAvlroIOPortGet(PAVLROIOPORTTREE pTree, RTIOPORT Key);
RTDECL(PAVLROIOPORTNODECORE)    RTAvlroIOPortRangeGet(PAVLROIOPORTTREE pTree, RTIOPORT Key);
RTDECL(PAVLROIOPORTNODECORE)    RTAvlroIOPortRangeRemove(PAVLROIOPORTTREE pTree, RTIOPORT Key);
RTDECL(int)                     RTAvlroIOPortDoWithAll(PAVLROIOPORTTREE pTree, int fFromLeft, PAVLROIOPORTCALLBACK pfnCallBack, void *pvParam);
RTDECL(int)                     RTAvlroIOPortDestroy(PAVLROIOPORTTREE pTree, PAVLROIOPORTCALLBACK pfnCallBack, void *pvParam);

/** @} */


/** AVL tree of RTHCPHYSes.
 * @{
 */

/**
 * AVL 'pointer' type for the relative offset pointer scheme.
 */
typedef struct _AVLHCPhysNodeCore  *AVLHCPHYSPTR;

/**
 * AVL Core node.
 */
typedef struct _AVLHCPhysNodeCore
{
    /** Offset to the left leaf node, relative to this field. */
    AVLHCPHYSPTR        pLeft;
    /** Offset to the right leaf node, relative to this field. */
    AVLHCPHYSPTR        pRight;
    /** Key value. */
    RTHCPHYS            Key;
    /** Height of this tree: max(height(left), height(right)) + 1 */
    unsigned char       uchHeight;
} AVLHCPHYSNODECORE, *PAVLHCPHYSNODECORE;

/** A offset base tree with RTHCPHYS keys. */
typedef AVLHCPHYSPTR      AVLHCPHYSTREE;
/** Pointer to an offset base tree with RTHCPHYS keys. */
typedef AVLHCPHYSTREE    *PAVLHCPHYSTREE;

/** Pointer to an internal tree pointer.
 * In this case it's a pointer to a relative offset. */
typedef AVLHCPHYSTREE    *PPAVLHCPHYSNODECORE;

/** Callback function for RTAvlHCPhysDoWithAll() and RTAvlHCPhysDestroy().
 *  @returns IPRT status codes. */
typedef DECLCALLBACK(int)   AVLHCPHYSCALLBACK(PAVLHCPHYSNODECORE pNode, void *pvUser);
/** Pointer to callback function for RTAvlHCPhysDoWithAll() and RTAvlHCPhysDestroy(). */
typedef AVLHCPHYSCALLBACK *PAVLHCPHYSCALLBACK;

RTDECL(bool)                    RTAvlHCPhysInsert(PAVLHCPHYSTREE pTree, PAVLHCPHYSNODECORE pNode);
RTDECL(PAVLHCPHYSNODECORE)      RTAvlHCPhysRemove(PAVLHCPHYSTREE pTree, RTHCPHYS Key);
RTDECL(PAVLHCPHYSNODECORE)      RTAvlHCPhysGet(PAVLHCPHYSTREE pTree, RTHCPHYS Key);
RTDECL(int)                     RTAvlHCPhysDoWithAll(PAVLHCPHYSTREE pTree, int fFromLeft, PAVLHCPHYSCALLBACK pfnCallBack, void *pvParam);
RTDECL(PAVLHCPHYSNODECORE)      RTAvlHCPhysGetBestFit(PAVLHCPHYSTREE ppTree, RTHCPHYS Key, bool fAbove);
RTDECL(PAVLHCPHYSNODECORE)      RTAvlHCPhysRemoveBestFit(PAVLHCPHYSTREE ppTree, RTHCPHYS Key, bool fAbove);
RTDECL(int)                     RTAvlHCPhysDestroy(PAVLHCPHYSTREE pTree, PAVLHCPHYSCALLBACK pfnCallBack, void *pvParam);

/** @} */

/** AVL tree of RTGCPHYSes.
 * @{
 */

/**
 * AVL 'pointer' type for the relative offset pointer scheme.
 */
typedef struct _AVLGCPhysNodeCore  *AVLGCPHYSPTR;

/**
 * AVL Core node.
 */
typedef struct _AVLGCPhysNodeCore
{
    /** Offset to the left leaf node, relative to this field. */
    AVLGCPHYSPTR        pLeft;
    /** Offset to the right leaf node, relative to this field. */
    AVLGCPHYSPTR        pRight;
    /** Key value. */
    RTGCPHYS            Key;
    /** Height of this tree: max(height(left), height(right)) + 1 */
    unsigned char       uchHeight;
} AVLGCPHYSNODECORE, *PAVLGCPHYSNODECORE;

/** A offset base tree with RTGCPHYS keys. */
typedef AVLGCPHYSPTR      AVLGCPHYSTREE;
/** Pointer to an offset base tree with RTGCPHYS keys. */
typedef AVLGCPHYSTREE    *PAVLGCPHYSTREE;

/** Pointer to an internal tree pointer.
 * In this case it's a pointer to a relative offset. */
typedef AVLGCPHYSTREE    *PPAVLGCPHYSNODECORE;

/** Callback function for RTAvlGCPhysDoWithAll() and RTAvlGCPhysDestroy().
 *  @returns IPRT status codes. */
typedef DECLCALLBACK(int)   AVLGCPHYSCALLBACK(PAVLGCPHYSNODECORE pNode, void *pvUser);
/** Pointer to callback function for RTAvlGCPhysDoWithAll() and RTAvlGCPhysDestroy(). */
typedef AVLGCPHYSCALLBACK *PAVLGCPHYSCALLBACK;

RTDECL(bool)                    RTAvlGCPhysInsert(PAVLGCPHYSTREE pTree, PAVLGCPHYSNODECORE pNode);
RTDECL(PAVLGCPHYSNODECORE)      RTAvlGCPhysRemove(PAVLGCPHYSTREE pTree, RTGCPHYS Key);
RTDECL(PAVLGCPHYSNODECORE)      RTAvlGCPhysGet(PAVLGCPHYSTREE pTree, RTGCPHYS Key);
RTDECL(int)                     RTAvlGCPhysDoWithAll(PAVLGCPHYSTREE pTree, int fFromLeft, PAVLGCPHYSCALLBACK pfnCallBack, void *pvParam);
RTDECL(PAVLGCPHYSNODECORE)      RTAvlGCPhysGetBestFit(PAVLGCPHYSTREE ppTree, RTGCPHYS Key, bool fAbove);
RTDECL(PAVLGCPHYSNODECORE)      RTAvlGCPhysRemoveBestFit(PAVLGCPHYSTREE ppTree, RTGCPHYS Key, bool fAbove);
RTDECL(int)                     RTAvlGCPhysDestroy(PAVLGCPHYSTREE pTree, PAVLGCPHYSCALLBACK pfnCallBack, void *pvParam);

/** @} */


/** AVL tree of RTFOFF ranges.
 * @{
 */

/**
 * AVL Core node.
 */
typedef struct _AVLRFOFFNodeCore
{
    /** First key value in the range (inclusive). */
    RTFOFF             Key;
    /** Last key value in the range (inclusive). */
    RTFOFF             KeyLast;
    /** Offset to the left leaf node, relative to this field. */
    struct _AVLRFOFFNodeCore  *pLeft;
    /** Offset to the right leaf node, relative to this field. */
    struct _AVLRFOFFNodeCore  *pRight;
    /** Height of this tree: max(height(left), height(right)) + 1 */
    unsigned char       uchHeight;
} AVLRFOFFNODECORE, *PAVLRFOFFNODECORE;

/** A pointer based tree with RTFOFF ranges. */
typedef PAVLRFOFFNODECORE AVLRFOFFTREE;
/** Pointer to a pointer based tree with RTFOFF ranges. */
typedef AVLRFOFFTREE    *PAVLRFOFFTREE;

/** Pointer to an internal tree pointer.
 * In this case it's a pointer to a relative offset. */
typedef AVLRFOFFTREE    *PPAVLRFOFFNODECORE;

/** Callback function for RTAvlrGCPtrDoWithAll() and RTAvlrGCPtrDestroy().
 *  @returns IPRT status codes. */
typedef DECLCALLBACK(int)   AVLRFOFFCALLBACK(PAVLRFOFFNODECORE pNode, void *pvUser);
/** Pointer to callback function for RTAvlrGCPtrDoWithAll() and RTAvlrGCPtrDestroy(). */
typedef AVLRFOFFCALLBACK *PAVLRFOFFCALLBACK;

RTDECL(bool)                  RTAvlrFileOffsetInsert(       PAVLRFOFFTREE pTree, PAVLRFOFFNODECORE pNode);
RTDECL(PAVLRFOFFNODECORE)     RTAvlrFileOffsetRemove(       PAVLRFOFFTREE pTree, RTFOFF Key);
RTDECL(PAVLRFOFFNODECORE)     RTAvlrFileOffsetGet(          PAVLRFOFFTREE pTree, RTFOFF Key);
RTDECL(PAVLRFOFFNODECORE)     RTAvlrFileOffsetGetBestFit(   PAVLRFOFFTREE pTree, RTFOFF Key, bool fAbove);
RTDECL(PAVLRFOFFNODECORE)     RTAvlrFileOffsetRangeGet(     PAVLRFOFFTREE pTree, RTFOFF Key);
RTDECL(PAVLRFOFFNODECORE)     RTAvlrFileOffsetRangeRemove(  PAVLRFOFFTREE pTree, RTFOFF Key);
RTDECL(int)                   RTAvlrFileOffsetDoWithAll(    PAVLRFOFFTREE pTree, int fFromLeft, PAVLRFOFFCALLBACK pfnCallBack, void *pvParam);
RTDECL(int)                   RTAvlrFileOffsetDestroy(      PAVLRFOFFTREE pTree, PAVLRFOFFCALLBACK pfnCallBack, void *pvParam);
RTDECL(PAVLRFOFFNODECORE)     RTAvlrFileOffsetGetRoot(      PAVLRFOFFTREE pTree);
RTDECL(PAVLRFOFFNODECORE)     RTAvlrFileOffsetGetLeft(      PAVLRFOFFNODECORE pNode);
RTDECL(PAVLRFOFFNODECORE)     RTAvlrFileOffsetGetRight(     PAVLRFOFFNODECORE pNode);

/** @} */

/** @} */

RT_C_DECLS_END

#endif

