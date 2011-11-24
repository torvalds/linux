#if !defined(_TEST_UTIL_H)
#define _TEST_UTIL_H

#if defined(_PARSER_4_HOST)
#define _myprintf  printf
#else
#define _myprintf  //
#endif

A_BOOL verifyChecksum(A_UINT16* stream, A_UINT32 len);
A_UINT16 computeChecksumOnly(A_UINT16 *pHalf, A_UINT16 length);

#endif // _TEST_UTIL_H


