
#ifndef __INCrateconstantsh
#define __INCrateconstantsh

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

//const A_UINT16 numRateCodes = sizeof(rateCodes)/sizeof(A_UCHAR);
#define numRateCodes 64
extern const A_UCHAR rateValues[numRateCodes];
extern const A_UCHAR rateCodes[numRateCodes];

#define UNKNOWN_RATE_CODE 0xff
#define IS_2STREAM_RATE_INDEX(x) (((x) >= 40 && (x) <= 47) || ((x) >= 56 && (x) <= 63))
#define IS_HT40_RATE_INDEX(x)    ((x) >= 48 && (x) <= 63)

extern A_UINT32 descRate2bin(A_UINT32 descRateCode);
extern A_UINT32 descRate2RateIndex(A_UINT32 descRateCode, A_BOOL ht40);
extern A_UINT32 rate2bin(A_UINT32 rateCode);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif

