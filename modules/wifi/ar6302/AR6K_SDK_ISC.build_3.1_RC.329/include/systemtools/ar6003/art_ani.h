/* art_ani.h - contians ANI functions for ART and mdk                   */
/* Copyright (c) 2003 Atheros Communications, Inc., All Rights Reserved */




#ifndef	__INCartanih
#define	__INCartanih

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#ifndef WIN32
#include <unistd.h>
#ifndef EILSEQ  
    #define EILSEQ EIO
#endif	// EILSEQ
#endif	// #ifndef WIN32

//#ifdef LINUX
//#undef ARCH_BIG_ENDIAN
//#endif
#include "wlanproto.h"

#define  ART_ANI_ENABLED   1
#define  ART_ANI_DISABLED  0

#define  ART_ANI_REUSE_ON   1  // channel data reuse from a previous optimization
#define  ART_ANI_REUSE_OFF  0

#define  ART_ANI_MONITOR_PERIOD       200  // 200ms default

#define  ART_ANI_CHANNEL_HISTORY_SIZE 256

#define NUM_ART_ANI_TYPES 3
#define ART_ANI_TYPE_NI   0
#define ART_ANI_TYPE_BI   1
#define ART_ANI_TYPE_SI   2

#define ART_ANI_FALSE_DETECT_OFDM			0
#define ART_ANI_FALSE_DETECT_CCK			1
#define ART_ANI_FALSE_DETECT_OFDM_AND_CCK   2

#define ART_ANI_OFDM_TRIGGER_COUNT_LOW      100
#define ART_ANI_OFDM_TRIGGER_COUNT_HIGH     250
#define ART_ANI_CCK_TRIGGER_COUNT_HIGH      200
#define ART_ANI_CCK_TRIGGER_COUNT_LOW       100



#define MAX_NUM_PARAMS_IN_ART_ANI_OPTIMIZATION_LEVEL 20  // max_num in NI/BI/SI

typedef struct _artAniSetup 
{
	A_UINT32   Enabled;
	A_UINT32   Reuse;
	A_UINT32   numChannelsInHistory;
	A_UINT32   chanListHistory[ART_ANI_CHANNEL_HISTORY_SIZE];
	A_UINT32   niLevelHistory[ART_ANI_CHANNEL_HISTORY_SIZE]; // noise immunity
	A_UINT32   biLevelHistory[ART_ANI_CHANNEL_HISTORY_SIZE]; // barker immunity
	A_UINT32   siLevelHistory[ART_ANI_CHANNEL_HISTORY_SIZE]; // spur immunity
	A_UINT32   monitorPeriod;  // period over which to gather false detect stats (in millisecond)
	A_UINT32   numCckFD;  // num CCK FALSE DETECTS
	A_UINT32   numOfdmFD; // num OFDM FALSE DETECTS
	A_UINT32   currNILevel;
	A_UINT32   currBILevel;
	A_UINT32   currSILevel;

} ART_ANI_SETUP;

typedef struct _artAniOptLevel {
    A_INT32 paramVal[MAX_NUM_PARAMS_IN_ART_ANI_OPTIMIZATION_LEVEL];    
	A_CHAR	levelName[16];
} ART_ANI_OPTIMIZATION_LEVEL;


typedef struct _artAniLadder{

    A_UINT32				numLevelsInLadder;
	A_UINT32				numParamsInLevel;
	A_UINT32				defaultLevelNum;
	A_UINT32				currLevelNum;
	A_UINT32				currFD; // false detects
	A_UINT32				loTrig;
	A_UINT32				hiTrig;
	A_UINT32				active;

    A_CHAR					paramName[MAX_NUM_PARAMS_IN_ART_ANI_OPTIMIZATION_LEVEL][122]; // pointer to list of param names used for NI/BI/SI

    ART_ANI_OPTIMIZATION_LEVEL	optLevel[15];    // a list of ANI level structs
	ART_ANI_OPTIMIZATION_LEVEL	*currLevel;

} ART_ANI_LADDER;


A_BOOL configArtAniSetup(A_UINT32 devNum, A_UINT32 artAniEnable, A_UINT32 artAniReuse);
A_BOOL enableArtAniSetup(A_UINT32 devNum);
A_BOOL disableArtAniSetup(A_UINT32 devNum);
A_BOOL tweakArtAni(A_UINT32 devNum, A_UINT32 prev_freq, A_UINT32 curr_freq);
A_BOOL artAniHistoryExists(A_UINT32 devNum, A_UINT32 curr_freq);
void artAniRecallLevels(A_UINT32 devNum, A_UINT32 curr_freq);
void artAniOptimizeLevels(A_UINT32 devNum, A_UINT32 curr_freq);
void artAniProgramCurrLevels(A_UINT32 devNum);

void artAniMeasureFalseDetects(A_UINT32 devNum, A_UINT32 artAniFalseDetectType);
A_BOOL artAniOptimized(A_UINT32 devNum, A_UINT32 artAniFalseDetectType);
void artAniFindMinLevel(A_UINT32 devNum, A_UINT32 artAniType);
void artAniUpdateLevels(A_UINT32 devNum);
void artAniUpdateHistory(A_UINT32 devNum);

//A_UINT32 getArtAniLevel(A_UINT32 devNum, A_UINT32 artAniType);
//void     setArtAniLevel(A_UINT32 devNum, A_UINT32 artAniType, A_UINT32 artAniLevel);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif // __INCartanih
