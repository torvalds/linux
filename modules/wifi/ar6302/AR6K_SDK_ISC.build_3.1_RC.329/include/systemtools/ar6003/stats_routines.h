#ifndef	__INCstats_routinesh
#define	__INCstats_routinesh

#include "wlantype.h"
#include "athreg.h"
#include "manlib.h"
#include "mdata.h"


void getSignalStrengthStats ( SIG_STRENGTH_STATS *pStats, A_INT8 signalStrength);
void fillRateThroughput ( TX_STATS_STRUCT *txStats, A_UINT32 descRate, A_UINT32 dataBodyLen);
void extractTxStats ( TX_STATS_TEMP_INFO *pStatsInfo, TX_STATS_STRUCT *txStats);
void fillTxStats ( A_UINT32 devNum, A_UINT32 descAddress, A_UINT32 numDesc, A_UINT32 dataBodyLen, A_UINT32 txTime, TX_STATS_STRUCT *txStats);

#endif

