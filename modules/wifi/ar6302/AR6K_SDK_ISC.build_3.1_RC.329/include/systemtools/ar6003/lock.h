/* lock.h - function prototypes for locking structure access */

/* Copyright (c) 2001 Atheros Communications, Inc., All Rights Reserved */


#ident  "ACI $Id: //depot/sw/releases/olca3.1-RC/include/systemtools/ar6003/lock.h#1 $, $Header: //depot/sw/releases/olca3.1-RC/include/systemtools/ar6003/lock.h#1 $"

/* 
modification history
--------------------
00a    14may01    fjc    Created.
*/

/**************************************************************************
* initializeLock - Initialize resource lock mechanism
*
* This will initialize a spinlock for use within the kernel environment
*
* RETURNS: N/A
*/
void initializeLock
	(
	void **ppLock
	);


/**************************************************************************
* acquireLock - Aquire lock for resource
*
* This will acquire the spinlock for the resource within the kernel environment
*
* RETURNS: N/A
*/
void acquireLock
	(
	void *pLock
	);

/**************************************************************************
* releaseLock - release lock for resource
*
* This will release the spinlock for the resource within the kernel environment
*
* RETURNS: N/A
*/
void releaseLock
	(
	void *pLock
	);


/**************************************************************************
* deleteLock - delete lock for resource
*
* This will delete the spinlock within the kernel environment
*
* RETURNS: N/A
*/
void deleteLock
	(
	void *pLock
	);

#ifdef AR6000
static __inline void emptypfn(A_CHAR *msg) { }
#define printMsg 1 ? (void)0 : emptypfn
#else
/**************************************************************************
* printMsg - Print within kernel mode
*
* 
*
* RETURNS: N/A
*/
void printMsg
	(
	A_CHAR *msg
	);
#endif



