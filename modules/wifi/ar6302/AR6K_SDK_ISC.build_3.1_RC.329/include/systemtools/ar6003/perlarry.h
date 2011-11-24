/* perlarry.h structure used to convert from a Perl array to C and vice-versa using SWIG */

#ident  "ACI $Id: //depot/sw/releases/olca3.1-RC/include/systemtools/ar6003/perlarry.h#1 $, $Header: //depot/sw/releases/olca3.1-RC/include/systemtools/ar6003/perlarry.h#1 $"

/* Copyright (c) 2000 Atheros Communications, Inc., All Rights Reserved */

#ifndef	__INCperlarryh
#define	__INCperlarryh

#include "wlantype.h"

// structure to pass Perl arrays to C and vice-versa
#ifndef AR6000
typedef struct tagDataBuffer
{
	A_UCHAR* pData;
	A_UINT32 Length;
} DATABUFFER, *PDATABUFFER;
#else
#define MAX_DATA_LENGTH (256*4)   // 256 eeprom locs read limit at the host, 4bytes per loc
typedef struct tagDataBuffer
{
	A_UCHAR pData[MAX_DATA_LENGTH];
	A_UINT32 Length;
} DATABUFFER, *PDATABUFFER;
#endif

// structure to pass 16bit C array to Perl and vice-versa
typedef struct tagWordBuffer
{
	A_UINT16* pWord;
	A_UINT32 Length;
} WORDBUFFER, *PWORDBUFFER;

// structure to pass 32bit C array to Perl and vice-versa
typedef struct tagDwordBuffer
{
	A_UINT32* pDword;
	A_UINT32 Length;
} DWORDBUFFER, *PDWORDBUFFER;

// structure to pass double C array to Perl and vice-versa
typedef struct tagIntValDoubleList
{
	double* pData;
	A_UINT32 Length;
	A_UINT32 retVal;
} INTVALDOUBLELIST, *PINTVALDOUBLELIST;

typedef struct tagDoubleValIntList
{
	A_UINT32* pData;
	A_UINT32  Length;
	double    retVal;
} DOUBLEVALINTLIST, *PDOUBLEVALINTLIST;

// structure for passing all of the arguments from wait_on_event()
typedef struct waitEventStruct
{
	A_UINT32 eventID;
	A_UINT32 simulationTime;
	A_UINT32 returnValue;
} WAITEVENT, *PWAITEVENT;


#define MAX_FILE_LENGTH		265

typedef struct cfgTableElement {
	A_UINT16	subsystemID;
	A_CHAR		eepFilename[MAX_FILE_LENGTH];
	A_CHAR      earFilename[MAX_FILE_LENGTH];
} CFG_TABLE_ELEMENT;

typedef struct cfgTable {
	A_UINT32	sizeCfgTable;
	CFG_TABLE_ELEMENT *pCurrentElement;
	CFG_TABLE_ELEMENT *pCfgTableElements;
} CFG_TABLE;



#endif
