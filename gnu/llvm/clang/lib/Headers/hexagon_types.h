/******************************************************************************/
/*   (c) 2020 Qualcomm Innovation Center, Inc. All rights reserved.           */
/*                                                                            */
/******************************************************************************/
#ifndef HEXAGON_TYPES_H
#define HEXAGON_TYPES_H

#include <hexagon_protos.h>

/* Hexagon names */
#define HEXAGON_Vect HEXAGON_Vect64
#define HEXAGON_V_GET_D HEXAGON_V64_GET_D
#define HEXAGON_V_GET_UD HEXAGON_V64_GET_UD
#define HEXAGON_V_GET_W0 HEXAGON_V64_GET_W0
#define HEXAGON_V_GET_W1 HEXAGON_V64_GET_W1
#define HEXAGON_V_GET_UW0 HEXAGON_V64_GET_UW0
#define HEXAGON_V_GET_UW1 HEXAGON_V64_GET_UW1
#define HEXAGON_V_GET_H0 HEXAGON_V64_GET_H0
#define HEXAGON_V_GET_H1 HEXAGON_V64_GET_H1
#define HEXAGON_V_GET_H2 HEXAGON_V64_GET_H2
#define HEXAGON_V_GET_H3 HEXAGON_V64_GET_H3
#define HEXAGON_V_GET_UH0 HEXAGON_V64_GET_UH0
#define HEXAGON_V_GET_UH1 HEXAGON_V64_GET_UH1
#define HEXAGON_V_GET_UH2 HEXAGON_V64_GET_UH2
#define HEXAGON_V_GET_UH3 HEXAGON_V64_GET_UH3
#define HEXAGON_V_GET_B0 HEXAGON_V64_GET_B0
#define HEXAGON_V_GET_B1 HEXAGON_V64_GET_B1
#define HEXAGON_V_GET_B2 HEXAGON_V64_GET_B2
#define HEXAGON_V_GET_B3 HEXAGON_V64_GET_B3
#define HEXAGON_V_GET_B4 HEXAGON_V64_GET_B4
#define HEXAGON_V_GET_B5 HEXAGON_V64_GET_B5
#define HEXAGON_V_GET_B6 HEXAGON_V64_GET_B6
#define HEXAGON_V_GET_B7 HEXAGON_V64_GET_B7
#define HEXAGON_V_GET_UB0 HEXAGON_V64_GET_UB0
#define HEXAGON_V_GET_UB1 HEXAGON_V64_GET_UB1
#define HEXAGON_V_GET_UB2 HEXAGON_V64_GET_UB2
#define HEXAGON_V_GET_UB3 HEXAGON_V64_GET_UB3
#define HEXAGON_V_GET_UB4 HEXAGON_V64_GET_UB4
#define HEXAGON_V_GET_UB5 HEXAGON_V64_GET_UB5
#define HEXAGON_V_GET_UB6 HEXAGON_V64_GET_UB6
#define HEXAGON_V_GET_UB7 HEXAGON_V64_GET_UB7
#define HEXAGON_V_PUT_D HEXAGON_V64_PUT_D
#define HEXAGON_V_PUT_W0 HEXAGON_V64_PUT_W0
#define HEXAGON_V_PUT_W1 HEXAGON_V64_PUT_W1
#define HEXAGON_V_PUT_H0 HEXAGON_V64_PUT_H0
#define HEXAGON_V_PUT_H1 HEXAGON_V64_PUT_H1
#define HEXAGON_V_PUT_H2 HEXAGON_V64_PUT_H2
#define HEXAGON_V_PUT_H3 HEXAGON_V64_PUT_H3
#define HEXAGON_V_PUT_B0 HEXAGON_V64_PUT_B0
#define HEXAGON_V_PUT_B1 HEXAGON_V64_PUT_B1
#define HEXAGON_V_PUT_B2 HEXAGON_V64_PUT_B2
#define HEXAGON_V_PUT_B3 HEXAGON_V64_PUT_B3
#define HEXAGON_V_PUT_B4 HEXAGON_V64_PUT_B4
#define HEXAGON_V_PUT_B5 HEXAGON_V64_PUT_B5
#define HEXAGON_V_PUT_B6 HEXAGON_V64_PUT_B6
#define HEXAGON_V_PUT_B7 HEXAGON_V64_PUT_B7
#define HEXAGON_V_CREATE_D HEXAGON_V64_CREATE_D
#define HEXAGON_V_CREATE_W HEXAGON_V64_CREATE_W
#define HEXAGON_V_CREATE_H HEXAGON_V64_CREATE_H
#define HEXAGON_V_CREATE_B HEXAGON_V64_CREATE_B

#ifdef __cplusplus
#define HEXAGON_VectC HEXAGON_Vect64C
#endif /* __cplusplus */

/* 64 Bit Vectors */

typedef long long __attribute__((__may_alias__)) HEXAGON_Vect64;

/* Extract doubleword macros */

#define HEXAGON_V64_GET_D(v) (v)
#define HEXAGON_V64_GET_UD(v) ((unsigned long long)(v))

/* Extract word macros */

#define HEXAGON_V64_GET_W0(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      int w[2];                                                                \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.w[0];                                                \
  })
#define HEXAGON_V64_GET_W1(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      int w[2];                                                                \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.w[1];                                                \
  })
#define HEXAGON_V64_GET_UW0(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned int uw[2];                                                      \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.uw[0];                                               \
  })
#define HEXAGON_V64_GET_UW1(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned int uw[2];                                                      \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.uw[1];                                               \
  })

/* Extract half word macros */

#define HEXAGON_V64_GET_H0(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      short h[4];                                                              \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.h[0];                                                \
  })
#define HEXAGON_V64_GET_H1(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      short h[4];                                                              \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.h[1];                                                \
  })
#define HEXAGON_V64_GET_H2(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      short h[4];                                                              \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.h[2];                                                \
  })
#define HEXAGON_V64_GET_H3(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      short h[4];                                                              \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.h[3];                                                \
  })
#define HEXAGON_V64_GET_UH0(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned short uh[4];                                                    \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.uh[0];                                               \
  })
#define HEXAGON_V64_GET_UH1(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned short uh[4];                                                    \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.uh[1];                                               \
  })
#define HEXAGON_V64_GET_UH2(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned short uh[4];                                                    \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.uh[2];                                               \
  })
#define HEXAGON_V64_GET_UH3(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned short uh[4];                                                    \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.uh[3];                                               \
  })

/* Extract byte macros */

#define HEXAGON_V64_GET_B0(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      signed char b[8];                                                        \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.b[0];                                                \
  })
#define HEXAGON_V64_GET_B1(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      signed char b[8];                                                        \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.b[1];                                                \
  })
#define HEXAGON_V64_GET_B2(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      signed char b[8];                                                        \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.b[2];                                                \
  })
#define HEXAGON_V64_GET_B3(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      signed char b[8];                                                        \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.b[3];                                                \
  })
#define HEXAGON_V64_GET_B4(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      signed char b[8];                                                        \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.b[4];                                                \
  })
#define HEXAGON_V64_GET_B5(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      signed char b[8];                                                        \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.b[5];                                                \
  })
#define HEXAGON_V64_GET_B6(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      signed char b[8];                                                        \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.b[6];                                                \
  })
#define HEXAGON_V64_GET_B7(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      signed char b[8];                                                        \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.b[7];                                                \
  })
#define HEXAGON_V64_GET_UB0(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned char ub[8];                                                     \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.ub[0];                                               \
  })
#define HEXAGON_V64_GET_UB1(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned char ub[8];                                                     \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.ub[1];                                               \
  })
#define HEXAGON_V64_GET_UB2(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned char ub[8];                                                     \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.ub[2];                                               \
  })
#define HEXAGON_V64_GET_UB3(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned char ub[8];                                                     \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.ub[3];                                               \
  })
#define HEXAGON_V64_GET_UB4(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned char ub[8];                                                     \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.ub[4];                                               \
  })
#define HEXAGON_V64_GET_UB5(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned char ub[8];                                                     \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.ub[5];                                               \
  })
#define HEXAGON_V64_GET_UB6(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned char ub[8];                                                     \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.ub[6];                                               \
  })
#define HEXAGON_V64_GET_UB7(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned char ub[8];                                                     \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.ub[7];                                               \
  })

/* NOTE: All set macros return a HEXAGON_Vect64 type */

/* Set doubleword macro */

#define HEXAGON_V64_PUT_D(v, new) (new)

/* Set word macros */

#ifdef __hexagon__

#define HEXAGON_V64_PUT_W0(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      int w[2];                                                                \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.w[0] = (new);                                        \
    _HEXAGON_V64_internal_union.d;                                                   \
  })
#define HEXAGON_V64_PUT_W1(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      int w[2];                                                                \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.w[1] = (new);                                        \
    _HEXAGON_V64_internal_union.d;                                                   \
  })

#else /* !__hexagon__ */

#define HEXAGON_V64_PUT_W0(v, new)                                                   \
  (((v) & 0xffffffff00000000LL) | ((HEXAGON_Vect64)((unsigned int)(new))))
#define HEXAGON_V64_PUT_W1(v, new)                                                   \
  (((v) & 0x00000000ffffffffLL) | (((HEXAGON_Vect64)(new)) << 32LL))

#endif /* !__hexagon__ */

/* Set half word macros */

#ifdef __hexagon__

#define HEXAGON_V64_PUT_H0(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      short h[4];                                                              \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.h[0] = (new);                                        \
    _HEXAGON_V64_internal_union.d;                                                   \
  })
#define HEXAGON_V64_PUT_H1(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      short h[4];                                                              \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.h[1] = (new);                                        \
    _HEXAGON_V64_internal_union.d;                                                   \
  })
#define HEXAGON_V64_PUT_H2(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      short h[4];                                                              \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.h[2] = (new);                                        \
    _HEXAGON_V64_internal_union.d;                                                   \
  })
#define HEXAGON_V64_PUT_H3(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      short h[4];                                                              \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.h[3] = (new);                                        \
    _HEXAGON_V64_internal_union.d;                                                   \
  })

#else /* !__hexagon__ */

#define HEXAGON_V64_PUT_H0(v, new)                                                   \
  (((v) & 0xffffffffffff0000LL) | ((HEXAGON_Vect64)((unsigned short)(new))))
#define HEXAGON_V64_PUT_H1(v, new)                                                   \
  (((v) & 0xffffffff0000ffffLL) | (((HEXAGON_Vect64)((unsigned short)(new))) << 16LL))
#define HEXAGON_V64_PUT_H2(v, new)                                                   \
  (((v) & 0xffff0000ffffffffLL) | (((HEXAGON_Vect64)((unsigned short)(new))) << 32LL))
#define HEXAGON_V64_PUT_H3(v, new)                                                   \
  (((v) & 0x0000ffffffffffffLL) | (((HEXAGON_Vect64)(new)) << 48LL))

#endif /* !__hexagon__ */

/* Set byte macros */

#ifdef __hexagon__

#define HEXAGON_V64_PUT_B0(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      char b[8];                                                               \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.b[0] = (new);                                        \
    _HEXAGON_V64_internal_union.d;                                                   \
  })
#define HEXAGON_V64_PUT_B1(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      char b[8];                                                               \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.b[1] = (new);                                        \
    _HEXAGON_V64_internal_union.d;                                                   \
  })
#define HEXAGON_V64_PUT_B2(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      char b[8];                                                               \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.b[2] = (new);                                        \
    _HEXAGON_V64_internal_union.d;                                                   \
  })
#define HEXAGON_V64_PUT_B3(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      char b[8];                                                               \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.b[3] = (new);                                        \
    _HEXAGON_V64_internal_union.d;                                                   \
  })
#define HEXAGON_V64_PUT_B4(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      char b[8];                                                               \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.b[4] = (new);                                        \
    _HEXAGON_V64_internal_union.d;                                                   \
  })
#define HEXAGON_V64_PUT_B5(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      char b[8];                                                               \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.b[5] = (new);                                        \
    _HEXAGON_V64_internal_union.d;                                                   \
  })
#define HEXAGON_V64_PUT_B6(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      char b[8];                                                               \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.b[6] = (new);                                        \
    _HEXAGON_V64_internal_union.d;                                                   \
  })
#define HEXAGON_V64_PUT_B7(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      char b[8];                                                               \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.d = (v);                                             \
    _HEXAGON_V64_internal_union.b[7] = (new);                                        \
    _HEXAGON_V64_internal_union.d;                                                   \
  })

#else /* !__hexagon__ */

#define HEXAGON_V64_PUT_B0(v, new)                                                   \
  (((v) & 0xffffffffffffff00LL) | ((HEXAGON_Vect64)((unsigned char)(new))))
#define HEXAGON_V64_PUT_B1(v, new)                                                   \
  (((v) & 0xffffffffffff00ffLL) | (((HEXAGON_Vect64)((unsigned char)(new))) << 8LL))
#define HEXAGON_V64_PUT_B2(v, new)                                                   \
  (((v) & 0xffffffffff00ffffLL) | (((HEXAGON_Vect64)((unsigned char)(new))) << 16LL))
#define HEXAGON_V64_PUT_B3(v, new)                                                   \
  (((v) & 0xffffffff00ffffffLL) | (((HEXAGON_Vect64)((unsigned char)(new))) << 24LL))
#define HEXAGON_V64_PUT_B4(v, new)                                                   \
  (((v) & 0xffffff00ffffffffLL) | (((HEXAGON_Vect64)((unsigned char)(new))) << 32LL))
#define HEXAGON_V64_PUT_B5(v, new)                                                   \
  (((v) & 0xffff00ffffffffffLL) | (((HEXAGON_Vect64)((unsigned char)(new))) << 40LL))
#define HEXAGON_V64_PUT_B6(v, new)                                                   \
  (((v) & 0xff00ffffffffffffLL) | (((HEXAGON_Vect64)((unsigned char)(new))) << 48LL))
#define HEXAGON_V64_PUT_B7(v, new)                                                   \
  (((v) & 0x00ffffffffffffffLL) | (((HEXAGON_Vect64)(new)) << 56LL))

#endif /* !__hexagon__ */

/* NOTE: All create macros return a HEXAGON_Vect64 type */

/* Create from a doubleword */

#define HEXAGON_V64_CREATE_D(d) (d)

/* Create from words */

#ifdef __hexagon__

#define HEXAGON_V64_CREATE_W(w1, w0)                                                 \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      int w[2];                                                                \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.w[0] = (w0);                                         \
    _HEXAGON_V64_internal_union.w[1] = (w1);                                         \
    _HEXAGON_V64_internal_union.d;                                                   \
  })

#else /* !__hexagon__ */

#define HEXAGON_V64_CREATE_W(w1, w0)                                                 \
  ((((HEXAGON_Vect64)(w1)) << 32LL) | ((HEXAGON_Vect64)((w0) & 0xffffffff)))

#endif /* !__hexagon__ */

/* Create from half words */

#ifdef __hexagon__

#define HEXAGON_V64_CREATE_H(h3, h2, h1, h0)                                         \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      short h[4];                                                              \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.h[0] = (h0);                                         \
    _HEXAGON_V64_internal_union.h[1] = (h1);                                         \
    _HEXAGON_V64_internal_union.h[2] = (h2);                                         \
    _HEXAGON_V64_internal_union.h[3] = (h3);                                         \
    _HEXAGON_V64_internal_union.d;                                                   \
  })

#else /* !__hexagon__ */

#define HEXAGON_V64_CREATE_H(h3, h2, h1, h0)                                         \
  ((((HEXAGON_Vect64)(h3)) << 48LL) | (((HEXAGON_Vect64)((h2) & 0xffff)) << 32LL) |        \
   (((HEXAGON_Vect64)((h1) & 0xffff)) << 16LL) | ((HEXAGON_Vect64)((h0) & 0xffff)))

#endif /* !__hexagon__ */

/* Create from bytes */

#ifdef __hexagon__

#define HEXAGON_V64_CREATE_B(b7, b6, b5, b4, b3, b2, b1, b0)                         \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      char b[8];                                                               \
    } _HEXAGON_V64_internal_union;                                                   \
    _HEXAGON_V64_internal_union.b[0] = (b0);                                         \
    _HEXAGON_V64_internal_union.b[1] = (b1);                                         \
    _HEXAGON_V64_internal_union.b[2] = (b2);                                         \
    _HEXAGON_V64_internal_union.b[3] = (b3);                                         \
    _HEXAGON_V64_internal_union.b[4] = (b4);                                         \
    _HEXAGON_V64_internal_union.b[5] = (b5);                                         \
    _HEXAGON_V64_internal_union.b[6] = (b6);                                         \
    _HEXAGON_V64_internal_union.b[7] = (b7);                                         \
    _HEXAGON_V64_internal_union.d;                                                   \
  })

#else /* !__hexagon__ */

#define HEXAGON_V64_CREATE_B(b7, b6, b5, b4, b3, b2, b1, b0)                         \
  ((((HEXAGON_Vect64)(b7)) << 56LL) | (((HEXAGON_Vect64)((b6) & 0xff)) << 48LL) |          \
   (((HEXAGON_Vect64)((b5) & 0xff)) << 40LL) | (((HEXAGON_Vect64)((b4) & 0xff)) << 32LL) | \
   (((HEXAGON_Vect64)((b3) & 0xff)) << 24LL) | (((HEXAGON_Vect64)((b2) & 0xff)) << 16LL) | \
   (((HEXAGON_Vect64)((b1) & 0xff)) << 8LL) | ((HEXAGON_Vect64)((b0) & 0xff)))

#endif /* !__hexagon__ */

#ifdef __cplusplus

class HEXAGON_Vect64C {
public:
  // Constructors
  HEXAGON_Vect64C(long long d = 0) : data(d) {};
  HEXAGON_Vect64C(int w1, int w0) : data(HEXAGON_V64_CREATE_W(w1, w0)) {};
  HEXAGON_Vect64C(short h3, short h2, short h1, short h0)
      : data(HEXAGON_V64_CREATE_H(h3, h2, h1, h0)) {};
  HEXAGON_Vect64C(signed char b7, signed char b6, signed char b5, signed char b4,
            signed char b3, signed char b2, signed char b1, signed char b0)
      : data(HEXAGON_V64_CREATE_B(b7, b6, b5, b4, b3, b2, b1, b0)) {};
  HEXAGON_Vect64C(const HEXAGON_Vect64C &v) : data(v.data) {};

  HEXAGON_Vect64C &operator=(const HEXAGON_Vect64C &v) {
    data = v.data;
    return *this;
  };

  operator long long() {
    return data;
  };

  // Extract doubleword methods
  long long D(void) {
    return HEXAGON_V64_GET_D(data);
  };
  unsigned long long UD(void) {
    return HEXAGON_V64_GET_UD(data);
  };

  // Extract word methods
  int W0(void) {
    return HEXAGON_V64_GET_W0(data);
  };
  int W1(void) {
    return HEXAGON_V64_GET_W1(data);
  };
  unsigned int UW0(void) {
    return HEXAGON_V64_GET_UW0(data);
  };
  unsigned int UW1(void) {
    return HEXAGON_V64_GET_UW1(data);
  };

  // Extract half word methods
  short H0(void) {
    return HEXAGON_V64_GET_H0(data);
  };
  short H1(void) {
    return HEXAGON_V64_GET_H1(data);
  };
  short H2(void) {
    return HEXAGON_V64_GET_H2(data);
  };
  short H3(void) {
    return HEXAGON_V64_GET_H3(data);
  };
  unsigned short UH0(void) {
    return HEXAGON_V64_GET_UH0(data);
  };
  unsigned short UH1(void) {
    return HEXAGON_V64_GET_UH1(data);
  };
  unsigned short UH2(void) {
    return HEXAGON_V64_GET_UH2(data);
  };
  unsigned short UH3(void) {
    return HEXAGON_V64_GET_UH3(data);
  };

  // Extract byte methods
  signed char B0(void) {
    return HEXAGON_V64_GET_B0(data);
  };
  signed char B1(void) {
    return HEXAGON_V64_GET_B1(data);
  };
  signed char B2(void) {
    return HEXAGON_V64_GET_B2(data);
  };
  signed char B3(void) {
    return HEXAGON_V64_GET_B3(data);
  };
  signed char B4(void) {
    return HEXAGON_V64_GET_B4(data);
  };
  signed char B5(void) {
    return HEXAGON_V64_GET_B5(data);
  };
  signed char B6(void) {
    return HEXAGON_V64_GET_B6(data);
  };
  signed char B7(void) {
    return HEXAGON_V64_GET_B7(data);
  };
  unsigned char UB0(void) {
    return HEXAGON_V64_GET_UB0(data);
  };
  unsigned char UB1(void) {
    return HEXAGON_V64_GET_UB1(data);
  };
  unsigned char UB2(void) {
    return HEXAGON_V64_GET_UB2(data);
  };
  unsigned char UB3(void) {
    return HEXAGON_V64_GET_UB3(data);
  };
  unsigned char UB4(void) {
    return HEXAGON_V64_GET_UB4(data);
  };
  unsigned char UB5(void) {
    return HEXAGON_V64_GET_UB5(data);
  };
  unsigned char UB6(void) {
    return HEXAGON_V64_GET_UB6(data);
  };
  unsigned char UB7(void) {
    return HEXAGON_V64_GET_UB7(data);
  };

  // NOTE: All set methods return a HEXAGON_Vect64C type

  // Set doubleword method
  HEXAGON_Vect64C D(long long d) {
    return HEXAGON_Vect64C(HEXAGON_V64_PUT_D(data, d));
  };

  // Set word methods
  HEXAGON_Vect64C W0(int w) {
    return HEXAGON_Vect64C(HEXAGON_V64_PUT_W0(data, w));
  };
  HEXAGON_Vect64C W1(int w) {
    return HEXAGON_Vect64C(HEXAGON_V64_PUT_W1(data, w));
  };

  // Set half word methods
  HEXAGON_Vect64C H0(short h) {
    return HEXAGON_Vect64C(HEXAGON_V64_PUT_H0(data, h));
  };
  HEXAGON_Vect64C H1(short h) {
    return HEXAGON_Vect64C(HEXAGON_V64_PUT_H1(data, h));
  };
  HEXAGON_Vect64C H2(short h) {
    return HEXAGON_Vect64C(HEXAGON_V64_PUT_H2(data, h));
  };
  HEXAGON_Vect64C H3(short h) {
    return HEXAGON_Vect64C(HEXAGON_V64_PUT_H3(data, h));
  };

  // Set byte methods
  HEXAGON_Vect64C B0(signed char b) {
    return HEXAGON_Vect64C(HEXAGON_V64_PUT_B0(data, b));
  };
  HEXAGON_Vect64C B1(signed char b) {
    return HEXAGON_Vect64C(HEXAGON_V64_PUT_B1(data, b));
  };
  HEXAGON_Vect64C B2(signed char b) {
    return HEXAGON_Vect64C(HEXAGON_V64_PUT_B2(data, b));
  };
  HEXAGON_Vect64C B3(signed char b) {
    return HEXAGON_Vect64C(HEXAGON_V64_PUT_B3(data, b));
  };
  HEXAGON_Vect64C B4(signed char b) {
    return HEXAGON_Vect64C(HEXAGON_V64_PUT_B4(data, b));
  };
  HEXAGON_Vect64C B5(signed char b) {
    return HEXAGON_Vect64C(HEXAGON_V64_PUT_B5(data, b));
  };
  HEXAGON_Vect64C B6(signed char b) {
    return HEXAGON_Vect64C(HEXAGON_V64_PUT_B6(data, b));
  };
  HEXAGON_Vect64C B7(signed char b) {
    return HEXAGON_Vect64C(HEXAGON_V64_PUT_B7(data, b));
  };

private:
  long long data;
};

#endif /* __cplusplus */

/* 32 Bit Vectors */

typedef int HEXAGON_Vect32;

/* Extract word macros */

#define HEXAGON_V32_GET_W(v) (v)
#define HEXAGON_V32_GET_UW(v) ((unsigned int)(v))

/* Extract half word macros */

#define HEXAGON_V32_GET_H0(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      short h[2];                                                              \
    } _HEXAGON_V32_internal_union;                                                   \
    _HEXAGON_V32_internal_union.w = (v);                                             \
    _HEXAGON_V32_internal_union.h[0];                                                \
  })
#define HEXAGON_V32_GET_H1(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      short h[2];                                                              \
    } _HEXAGON_V32_internal_union;                                                   \
    _HEXAGON_V32_internal_union.w = (v);                                             \
    _HEXAGON_V32_internal_union.h[1];                                                \
  })
#define HEXAGON_V32_GET_UH0(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      unsigned short uh[2];                                                    \
    } _HEXAGON_V32_internal_union;                                                   \
    _HEXAGON_V32_internal_union.w = (v);                                             \
    _HEXAGON_V32_internal_union.uh[0];                                               \
  })
#define HEXAGON_V32_GET_UH1(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      unsigned short uh[2];                                                    \
    } _HEXAGON_V32_internal_union;                                                   \
    _HEXAGON_V32_internal_union.w = (v);                                             \
    _HEXAGON_V32_internal_union.uh[1];                                               \
  })

/* Extract byte macros */

#define HEXAGON_V32_GET_B0(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      signed char b[4];                                                        \
    } _HEXAGON_V32_internal_union;                                                   \
    _HEXAGON_V32_internal_union.w = (v);                                             \
    _HEXAGON_V32_internal_union.b[0];                                                \
  })
#define HEXAGON_V32_GET_B1(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      signed char b[4];                                                        \
    } _HEXAGON_V32_internal_union;                                                   \
    _HEXAGON_V32_internal_union.w = (v);                                             \
    _HEXAGON_V32_internal_union.b[1];                                                \
  })
#define HEXAGON_V32_GET_B2(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      signed char b[4];                                                        \
    } _HEXAGON_V32_internal_union;                                                   \
    _HEXAGON_V32_internal_union.w = (v);                                             \
    _HEXAGON_V32_internal_union.b[2];                                                \
  })
#define HEXAGON_V32_GET_B3(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      signed char b[4];                                                        \
    } _HEXAGON_V32_internal_union;                                                   \
    _HEXAGON_V32_internal_union.w = (v);                                             \
    _HEXAGON_V32_internal_union.b[3];                                                \
  })
#define HEXAGON_V32_GET_UB0(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      unsigned char ub[4];                                                     \
    } _HEXAGON_V32_internal_union;                                                   \
    _HEXAGON_V32_internal_union.w = (v);                                             \
    _HEXAGON_V32_internal_union.ub[0];                                               \
  })
#define HEXAGON_V32_GET_UB1(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      unsigned char ub[4];                                                     \
    } _HEXAGON_V32_internal_union;                                                   \
    _HEXAGON_V32_internal_union.w = (v);                                             \
    _HEXAGON_V32_internal_union.ub[1];                                               \
  })
#define HEXAGON_V32_GET_UB2(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      unsigned char ub[4];                                                     \
    } _HEXAGON_V32_internal_union;                                                   \
    _HEXAGON_V32_internal_union.w = (v);                                             \
    _HEXAGON_V32_internal_union.ub[2];                                               \
  })
#define HEXAGON_V32_GET_UB3(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      unsigned char ub[4];                                                     \
    } _HEXAGON_V32_internal_union;                                                   \
    _HEXAGON_V32_internal_union.w = (v);                                             \
    _HEXAGON_V32_internal_union.ub[3];                                               \
  })

/* NOTE: All set macros return a HEXAGON_Vect32 type */

/* Set word macro */

#define HEXAGON_V32_PUT_W(v, new) (new)

/* Set half word macros */

#ifdef __hexagon__

#define HEXAGON_V32_PUT_H0(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      short h[2];                                                              \
    } _HEXAGON_V32_internal_union;                                                   \
    _HEXAGON_V32_internal_union.w = (v);                                             \
    _HEXAGON_V32_internal_union.h[0] = (new);                                        \
    _HEXAGON_V32_internal_union.w;                                                   \
  })
#define HEXAGON_V32_PUT_H1(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      short h[2];                                                              \
    } _HEXAGON_V32_internal_union;                                                   \
    _HEXAGON_V32_internal_union.w = (v);                                             \
    _HEXAGON_V32_internal_union.h[1] = (new);                                        \
    _HEXAGON_V32_internal_union.w;                                                   \
  })

#else /* !__hexagon__ */

#define HEXAGON_V32_PUT_H0(v, new)                                                   \
  (((v) & 0xffff0000) | ((HEXAGON_Vect32)((unsigned short)(new))))
#define HEXAGON_V32_PUT_H1(v, new) (((v) & 0x0000ffff) | (((HEXAGON_Vect32)(new)) << 16))

#endif /* !__hexagon__ */

/* Set byte macros */

#ifdef __hexagon__

#define HEXAGON_V32_PUT_B0(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      char b[4];                                                               \
    } _HEXAGON_V32_internal_union;                                                   \
    _HEXAGON_V32_internal_union.w = (v);                                             \
    _HEXAGON_V32_internal_union.b[0] = (new);                                        \
    _HEXAGON_V32_internal_union.w;                                                   \
  })
#define HEXAGON_V32_PUT_B1(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      char b[4];                                                               \
    } _HEXAGON_V32_internal_union;                                                   \
    _HEXAGON_V32_internal_union.w = (v);                                             \
    _HEXAGON_V32_internal_union.b[1] = (new);                                        \
    _HEXAGON_V32_internal_union.w;                                                   \
  })
#define HEXAGON_V32_PUT_B2(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      char b[4];                                                               \
    } _HEXAGON_V32_internal_union;                                                   \
    _HEXAGON_V32_internal_union.w = (v);                                             \
    _HEXAGON_V32_internal_union.b[2] = (new);                                        \
    _HEXAGON_V32_internal_union.w;                                                   \
  })
#define HEXAGON_V32_PUT_B3(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      char b[4];                                                               \
    } _HEXAGON_V32_internal_union;                                                   \
    _HEXAGON_V32_internal_union.w = (v);                                             \
    _HEXAGON_V32_internal_union.b[3] = (new);                                        \
    _HEXAGON_V32_internal_union.w;                                                   \
  })

#else /* !__hexagon__ */

#define HEXAGON_V32_PUT_B0(v, new)                                                   \
  (((v) & 0xffffff00) | ((HEXAGON_Vect32)((unsigned char)(new))))
#define HEXAGON_V32_PUT_B1(v, new)                                                   \
  (((v) & 0xffff00ff) | (((HEXAGON_Vect32)((unsigned char)(new))) << 8))
#define HEXAGON_V32_PUT_B2(v, new)                                                   \
  (((v) & 0xff00ffff) | (((HEXAGON_Vect32)((unsigned char)(new))) << 16))
#define HEXAGON_V32_PUT_B3(v, new) (((v) & 0x00ffffff) | (((HEXAGON_Vect32)(new)) << 24))

#endif /* !__hexagon__ */

/* NOTE: All create macros return a HEXAGON_Vect32 type */

/* Create from a word */

#define HEXAGON_V32_CREATE_W(w) (w)

/* Create from half words */

#ifdef __hexagon__

#define HEXAGON_V32_CREATE_H(h1, h0)                                                 \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      short h[2];                                                              \
    } _HEXAGON_V32_internal_union;                                                   \
    _HEXAGON_V32_internal_union.h[0] = (h0);                                         \
    _HEXAGON_V32_internal_union.h[1] = (h1);                                         \
    _HEXAGON_V32_internal_union.d;                                                   \
  })

#else /* !__hexagon__ */

#define HEXAGON_V32_CREATE_H(h1, h0)                                                 \
  ((((HEXAGON_Vect32)(h1)) << 16) | ((HEXAGON_Vect32)((h0) & 0xffff)))

#endif /* !__hexagon__ */

/* Create from bytes */
#ifdef __hexagon__

#define HEXAGON_V32_CREATE_B(b3, b2, b1, b0)                                         \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      char b[4];                                                               \
    } _HEXAGON_V32_internal_union;                                                   \
    _HEXAGON_V32_internal_union.b[0] = (b0);                                         \
    _HEXAGON_V32_internal_union.b[1] = (b1);                                         \
    _HEXAGON_V32_internal_union.b[2] = (b2);                                         \
    _HEXAGON_V32_internal_union.b[3] = (b3);                                         \
    _HEXAGON_V32_internal_union.d;                                                   \
  })

#else /* !__hexagon__ */

#define HEXAGON_V32_CREATE_B(b3, b2, b1, b0)                                         \
  ((((HEXAGON_Vect32)(b3)) << 24) | (((HEXAGON_Vect32)((b2) & 0xff)) << 16) |              \
   (((HEXAGON_Vect32)((b1) & 0xff)) << 8) | ((HEXAGON_Vect32)((b0) & 0xff)))

#endif /* !__hexagon__ */

#ifdef __cplusplus

class HEXAGON_Vect32C {
public:
  // Constructors
  HEXAGON_Vect32C(int w = 0) : data(w) {};
  HEXAGON_Vect32C(short h1, short h0) : data(HEXAGON_V32_CREATE_H(h1, h0)) {};
  HEXAGON_Vect32C(signed char b3, signed char b2, signed char b1, signed char b0)
      : data(HEXAGON_V32_CREATE_B(b3, b2, b1, b0)) {};
  HEXAGON_Vect32C(const HEXAGON_Vect32C &v) : data(v.data) {};

  HEXAGON_Vect32C &operator=(const HEXAGON_Vect32C &v) {
    data = v.data;
    return *this;
  };

  operator int() {
    return data;
  };

  // Extract word methods
  int W(void) {
    return HEXAGON_V32_GET_W(data);
  };
  unsigned int UW(void) {
    return HEXAGON_V32_GET_UW(data);
  };

  // Extract half word methods
  short H0(void) {
    return HEXAGON_V32_GET_H0(data);
  };
  short H1(void) {
    return HEXAGON_V32_GET_H1(data);
  };
  unsigned short UH0(void) {
    return HEXAGON_V32_GET_UH0(data);
  };
  unsigned short UH1(void) {
    return HEXAGON_V32_GET_UH1(data);
  };

  // Extract byte methods
  signed char B0(void) {
    return HEXAGON_V32_GET_B0(data);
  };
  signed char B1(void) {
    return HEXAGON_V32_GET_B1(data);
  };
  signed char B2(void) {
    return HEXAGON_V32_GET_B2(data);
  };
  signed char B3(void) {
    return HEXAGON_V32_GET_B3(data);
  };
  unsigned char UB0(void) {
    return HEXAGON_V32_GET_UB0(data);
  };
  unsigned char UB1(void) {
    return HEXAGON_V32_GET_UB1(data);
  };
  unsigned char UB2(void) {
    return HEXAGON_V32_GET_UB2(data);
  };
  unsigned char UB3(void) {
    return HEXAGON_V32_GET_UB3(data);
  };

  // NOTE: All set methods return a HEXAGON_Vect32C type

  // Set word method
  HEXAGON_Vect32C W(int w) {
    return HEXAGON_Vect32C(HEXAGON_V32_PUT_W(data, w));
  };

  // Set half word methods
  HEXAGON_Vect32C H0(short h) {
    return HEXAGON_Vect32C(HEXAGON_V32_PUT_H0(data, h));
  };
  HEXAGON_Vect32C H1(short h) {
    return HEXAGON_Vect32C(HEXAGON_V32_PUT_H1(data, h));
  };

  // Set byte methods
  HEXAGON_Vect32C B0(signed char b) {
    return HEXAGON_Vect32C(HEXAGON_V32_PUT_B0(data, b));
  };
  HEXAGON_Vect32C B1(signed char b) {
    return HEXAGON_Vect32C(HEXAGON_V32_PUT_B1(data, b));
  };
  HEXAGON_Vect32C B2(signed char b) {
    return HEXAGON_Vect32C(HEXAGON_V32_PUT_B2(data, b));
  };
  HEXAGON_Vect32C B3(signed char b) {
    return HEXAGON_Vect32C(HEXAGON_V32_PUT_B3(data, b));
  };

private:
  int data;
};

#endif /* __cplusplus */

// V65 Vector types
#if __HVX_ARCH__ >= 65
#if defined __HVX__ && (__HVX_LENGTH__ == 128)
  typedef long HEXAGON_VecPred128 __attribute__((__vector_size__(128)))
    __attribute__((aligned(128)));

  typedef long HEXAGON_Vect1024 __attribute__((__vector_size__(128)))
    __attribute__((aligned(128)));

  typedef long HEXAGON_Vect2048 __attribute__((__vector_size__(256)))
    __attribute__((aligned(256)));

  typedef long HEXAGON_UVect1024 __attribute__((__vector_size__(128)))
    __attribute__((aligned(4)));

  typedef long HEXAGON_UVect2048 __attribute__((__vector_size__(256)))
    __attribute__((aligned(4)));

  #define HVX_VectorPred     HEXAGON_VecPred128
  #define HVX_Vector         HEXAGON_Vect1024
  #define HVX_VectorPair     HEXAGON_Vect2048
  #define HVX_UVector        HEXAGON_UVect1024
  #define HVX_UVectorPair    HEXAGON_UVect2048
#else /* defined __HVX__ && (__HVX_LENGTH__ == 128) */
#if defined __HVX__ &&  (__HVX_LENGTH__ == 64)
  typedef long HEXAGON_VecPred64 __attribute__((__vector_size__(64)))
    __attribute__((aligned(64)));

  typedef long HEXAGON_Vect512 __attribute__((__vector_size__(64)))
    __attribute__((aligned(64)));

  typedef long HEXAGON_Vect1024 __attribute__((__vector_size__(128)))
    __attribute__((aligned(128)));

  typedef long HEXAGON_UVect512 __attribute__((__vector_size__(64)))
    __attribute__((aligned(4)));

  typedef long HEXAGON_UVect1024 __attribute__((__vector_size__(128)))
    __attribute__((aligned(4)));

  #define HVX_VectorPred     HEXAGON_VecPred64
  #define HVX_Vector         HEXAGON_Vect512
  #define HVX_VectorPair     HEXAGON_Vect1024
  #define HVX_UVector        HEXAGON_UVect512
  #define HVX_UVectorPair    HEXAGON_UVect1024
#endif /* defined __HVX__ &&  (__HVX_LENGTH__ == 64) */
#endif /* defined __HVX__ && (__HVX_LENGTH__ == 128) */
#endif /* __HVX_ARCH__ >= 65 */

/* Predicates */

typedef int HEXAGON_Pred;

/***
 *** backward compatibility aliases
 ***/

/* Old names */
#define Q6Vect Q6Vect64
#define Q6V_GET_D Q6V64_GET_D
#define Q6V_GET_UD Q6V64_GET_UD
#define Q6V_GET_W0 Q6V64_GET_W0
#define Q6V_GET_W1 Q6V64_GET_W1
#define Q6V_GET_UW0 Q6V64_GET_UW0
#define Q6V_GET_UW1 Q6V64_GET_UW1
#define Q6V_GET_H0 Q6V64_GET_H0
#define Q6V_GET_H1 Q6V64_GET_H1
#define Q6V_GET_H2 Q6V64_GET_H2
#define Q6V_GET_H3 Q6V64_GET_H3
#define Q6V_GET_UH0 Q6V64_GET_UH0
#define Q6V_GET_UH1 Q6V64_GET_UH1
#define Q6V_GET_UH2 Q6V64_GET_UH2
#define Q6V_GET_UH3 Q6V64_GET_UH3
#define Q6V_GET_B0 Q6V64_GET_B0
#define Q6V_GET_B1 Q6V64_GET_B1
#define Q6V_GET_B2 Q6V64_GET_B2
#define Q6V_GET_B3 Q6V64_GET_B3
#define Q6V_GET_B4 Q6V64_GET_B4
#define Q6V_GET_B5 Q6V64_GET_B5
#define Q6V_GET_B6 Q6V64_GET_B6
#define Q6V_GET_B7 Q6V64_GET_B7
#define Q6V_GET_UB0 Q6V64_GET_UB0
#define Q6V_GET_UB1 Q6V64_GET_UB1
#define Q6V_GET_UB2 Q6V64_GET_UB2
#define Q6V_GET_UB3 Q6V64_GET_UB3
#define Q6V_GET_UB4 Q6V64_GET_UB4
#define Q6V_GET_UB5 Q6V64_GET_UB5
#define Q6V_GET_UB6 Q6V64_GET_UB6
#define Q6V_GET_UB7 Q6V64_GET_UB7
#define Q6V_PUT_D Q6V64_PUT_D
#define Q6V_PUT_W0 Q6V64_PUT_W0
#define Q6V_PUT_W1 Q6V64_PUT_W1
#define Q6V_PUT_H0 Q6V64_PUT_H0
#define Q6V_PUT_H1 Q6V64_PUT_H1
#define Q6V_PUT_H2 Q6V64_PUT_H2
#define Q6V_PUT_H3 Q6V64_PUT_H3
#define Q6V_PUT_B0 Q6V64_PUT_B0
#define Q6V_PUT_B1 Q6V64_PUT_B1
#define Q6V_PUT_B2 Q6V64_PUT_B2
#define Q6V_PUT_B3 Q6V64_PUT_B3
#define Q6V_PUT_B4 Q6V64_PUT_B4
#define Q6V_PUT_B5 Q6V64_PUT_B5
#define Q6V_PUT_B6 Q6V64_PUT_B6
#define Q6V_PUT_B7 Q6V64_PUT_B7
#define Q6V_CREATE_D Q6V64_CREATE_D
#define Q6V_CREATE_W Q6V64_CREATE_W
#define Q6V_CREATE_H Q6V64_CREATE_H
#define Q6V_CREATE_B Q6V64_CREATE_B

#ifdef __cplusplus
#define Q6VectC Q6Vect64C
#endif /* __cplusplus */

/* 64 Bit Vectors */

typedef long long __attribute__((__may_alias__)) Q6Vect64;

/* Extract doubleword macros */

#define Q6V64_GET_D(v) (v)
#define Q6V64_GET_UD(v) ((unsigned long long)(v))

/* Extract word macros */

#define Q6V64_GET_W0(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      int w[2];                                                                \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.w[0];                                                \
  })
#define Q6V64_GET_W1(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      int w[2];                                                                \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.w[1];                                                \
  })
#define Q6V64_GET_UW0(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned int uw[2];                                                      \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.uw[0];                                               \
  })
#define Q6V64_GET_UW1(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned int uw[2];                                                      \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.uw[1];                                               \
  })

/* Extract half word macros */

#define Q6V64_GET_H0(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      short h[4];                                                              \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.h[0];                                                \
  })
#define Q6V64_GET_H1(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      short h[4];                                                              \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.h[1];                                                \
  })
#define Q6V64_GET_H2(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      short h[4];                                                              \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.h[2];                                                \
  })
#define Q6V64_GET_H3(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      short h[4];                                                              \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.h[3];                                                \
  })
#define Q6V64_GET_UH0(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned short uh[4];                                                    \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.uh[0];                                               \
  })
#define Q6V64_GET_UH1(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned short uh[4];                                                    \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.uh[1];                                               \
  })
#define Q6V64_GET_UH2(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned short uh[4];                                                    \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.uh[2];                                               \
  })
#define Q6V64_GET_UH3(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned short uh[4];                                                    \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.uh[3];                                               \
  })

/* Extract byte macros */

#define Q6V64_GET_B0(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      signed char b[8];                                                        \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.b[0];                                                \
  })
#define Q6V64_GET_B1(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      signed char b[8];                                                        \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.b[1];                                                \
  })
#define Q6V64_GET_B2(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      signed char b[8];                                                        \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.b[2];                                                \
  })
#define Q6V64_GET_B3(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      signed char b[8];                                                        \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.b[3];                                                \
  })
#define Q6V64_GET_B4(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      signed char b[8];                                                        \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.b[4];                                                \
  })
#define Q6V64_GET_B5(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      signed char b[8];                                                        \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.b[5];                                                \
  })
#define Q6V64_GET_B6(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      signed char b[8];                                                        \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.b[6];                                                \
  })
#define Q6V64_GET_B7(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      signed char b[8];                                                        \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.b[7];                                                \
  })
#define Q6V64_GET_UB0(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned char ub[8];                                                     \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.ub[0];                                               \
  })
#define Q6V64_GET_UB1(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned char ub[8];                                                     \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.ub[1];                                               \
  })
#define Q6V64_GET_UB2(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned char ub[8];                                                     \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.ub[2];                                               \
  })
#define Q6V64_GET_UB3(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned char ub[8];                                                     \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.ub[3];                                               \
  })
#define Q6V64_GET_UB4(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned char ub[8];                                                     \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.ub[4];                                               \
  })
#define Q6V64_GET_UB5(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned char ub[8];                                                     \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.ub[5];                                               \
  })
#define Q6V64_GET_UB6(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned char ub[8];                                                     \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.ub[6];                                               \
  })
#define Q6V64_GET_UB7(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      unsigned char ub[8];                                                     \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.ub[7];                                               \
  })

/* NOTE: All set macros return a Q6Vect64 type */

/* Set doubleword macro */

#define Q6V64_PUT_D(v, new) (new)

/* Set word macros */

#ifdef __qdsp6__

#define Q6V64_PUT_W0(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      int w[2];                                                                \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.w[0] = (new);                                        \
    _Q6V64_internal_union.d;                                                   \
  })
#define Q6V64_PUT_W1(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      int w[2];                                                                \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.w[1] = (new);                                        \
    _Q6V64_internal_union.d;                                                   \
  })

#else /* !__qdsp6__ */

#define Q6V64_PUT_W0(v, new)                                                   \
  (((v) & 0xffffffff00000000LL) | ((Q6Vect64)((unsigned int)(new))))
#define Q6V64_PUT_W1(v, new)                                                   \
  (((v) & 0x00000000ffffffffLL) | (((Q6Vect64)(new)) << 32LL))

#endif /* !__qdsp6__ */

/* Set half word macros */

#ifdef __qdsp6__

#define Q6V64_PUT_H0(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      short h[4];                                                              \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.h[0] = (new);                                        \
    _Q6V64_internal_union.d;                                                   \
  })
#define Q6V64_PUT_H1(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      short h[4];                                                              \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.h[1] = (new);                                        \
    _Q6V64_internal_union.d;                                                   \
  })
#define Q6V64_PUT_H2(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      short h[4];                                                              \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.h[2] = (new);                                        \
    _Q6V64_internal_union.d;                                                   \
  })
#define Q6V64_PUT_H3(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      short h[4];                                                              \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.h[3] = (new);                                        \
    _Q6V64_internal_union.d;                                                   \
  })

#else /* !__qdsp6__ */

#define Q6V64_PUT_H0(v, new)                                                   \
  (((v) & 0xffffffffffff0000LL) | ((Q6Vect64)((unsigned short)(new))))
#define Q6V64_PUT_H1(v, new)                                                   \
  (((v) & 0xffffffff0000ffffLL) | (((Q6Vect64)((unsigned short)(new))) << 16LL))
#define Q6V64_PUT_H2(v, new)                                                   \
  (((v) & 0xffff0000ffffffffLL) | (((Q6Vect64)((unsigned short)(new))) << 32LL))
#define Q6V64_PUT_H3(v, new)                                                   \
  (((v) & 0x0000ffffffffffffLL) | (((Q6Vect64)(new)) << 48LL))

#endif /* !__qdsp6__ */

/* Set byte macros */

#ifdef __qdsp6__

#define Q6V64_PUT_B0(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      char b[8];                                                               \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.b[0] = (new);                                        \
    _Q6V64_internal_union.d;                                                   \
  })
#define Q6V64_PUT_B1(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      char b[8];                                                               \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.b[1] = (new);                                        \
    _Q6V64_internal_union.d;                                                   \
  })
#define Q6V64_PUT_B2(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      char b[8];                                                               \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.b[2] = (new);                                        \
    _Q6V64_internal_union.d;                                                   \
  })
#define Q6V64_PUT_B3(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      char b[8];                                                               \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.b[3] = (new);                                        \
    _Q6V64_internal_union.d;                                                   \
  })
#define Q6V64_PUT_B4(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      char b[8];                                                               \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.b[4] = (new);                                        \
    _Q6V64_internal_union.d;                                                   \
  })
#define Q6V64_PUT_B5(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      char b[8];                                                               \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.b[5] = (new);                                        \
    _Q6V64_internal_union.d;                                                   \
  })
#define Q6V64_PUT_B6(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      char b[8];                                                               \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.b[6] = (new);                                        \
    _Q6V64_internal_union.d;                                                   \
  })
#define Q6V64_PUT_B7(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      char b[8];                                                               \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.d = (v);                                             \
    _Q6V64_internal_union.b[7] = (new);                                        \
    _Q6V64_internal_union.d;                                                   \
  })

#else /* !__qdsp6__ */

#define Q6V64_PUT_B0(v, new)                                                   \
  (((v) & 0xffffffffffffff00LL) | ((Q6Vect64)((unsigned char)(new))))
#define Q6V64_PUT_B1(v, new)                                                   \
  (((v) & 0xffffffffffff00ffLL) | (((Q6Vect64)((unsigned char)(new))) << 8LL))
#define Q6V64_PUT_B2(v, new)                                                   \
  (((v) & 0xffffffffff00ffffLL) | (((Q6Vect64)((unsigned char)(new))) << 16LL))
#define Q6V64_PUT_B3(v, new)                                                   \
  (((v) & 0xffffffff00ffffffLL) | (((Q6Vect64)((unsigned char)(new))) << 24LL))
#define Q6V64_PUT_B4(v, new)                                                   \
  (((v) & 0xffffff00ffffffffLL) | (((Q6Vect64)((unsigned char)(new))) << 32LL))
#define Q6V64_PUT_B5(v, new)                                                   \
  (((v) & 0xffff00ffffffffffLL) | (((Q6Vect64)((unsigned char)(new))) << 40LL))
#define Q6V64_PUT_B6(v, new)                                                   \
  (((v) & 0xff00ffffffffffffLL) | (((Q6Vect64)((unsigned char)(new))) << 48LL))
#define Q6V64_PUT_B7(v, new)                                                   \
  (((v) & 0x00ffffffffffffffLL) | (((Q6Vect64)(new)) << 56LL))

#endif /* !__qdsp6__ */

/* NOTE: All create macros return a Q6Vect64 type */

/* Create from a doubleword */

#define Q6V64_CREATE_D(d) (d)

/* Create from words */

#ifdef __qdsp6__

#define Q6V64_CREATE_W(w1, w0)                                                 \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      int w[2];                                                                \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.w[0] = (w0);                                         \
    _Q6V64_internal_union.w[1] = (w1);                                         \
    _Q6V64_internal_union.d;                                                   \
  })

#else /* !__qdsp6__ */

#define Q6V64_CREATE_W(w1, w0)                                                 \
  ((((Q6Vect64)(w1)) << 32LL) | ((Q6Vect64)((w0) & 0xffffffff)))

#endif /* !__qdsp6__ */

/* Create from half words */

#ifdef __qdsp6__

#define Q6V64_CREATE_H(h3, h2, h1, h0)                                         \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      short h[4];                                                              \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.h[0] = (h0);                                         \
    _Q6V64_internal_union.h[1] = (h1);                                         \
    _Q6V64_internal_union.h[2] = (h2);                                         \
    _Q6V64_internal_union.h[3] = (h3);                                         \
    _Q6V64_internal_union.d;                                                   \
  })

#else /* !__qdsp6__ */

#define Q6V64_CREATE_H(h3, h2, h1, h0)                                         \
  ((((Q6Vect64)(h3)) << 48LL) | (((Q6Vect64)((h2) & 0xffff)) << 32LL) |        \
   (((Q6Vect64)((h1) & 0xffff)) << 16LL) | ((Q6Vect64)((h0) & 0xffff)))

#endif /* !__qdsp6__ */

/* Create from bytes */

#ifdef __qdsp6__

#define Q6V64_CREATE_B(b7, b6, b5, b4, b3, b2, b1, b0)                         \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      char b[8];                                                               \
    } _Q6V64_internal_union;                                                   \
    _Q6V64_internal_union.b[0] = (b0);                                         \
    _Q6V64_internal_union.b[1] = (b1);                                         \
    _Q6V64_internal_union.b[2] = (b2);                                         \
    _Q6V64_internal_union.b[3] = (b3);                                         \
    _Q6V64_internal_union.b[4] = (b4);                                         \
    _Q6V64_internal_union.b[5] = (b5);                                         \
    _Q6V64_internal_union.b[6] = (b6);                                         \
    _Q6V64_internal_union.b[7] = (b7);                                         \
    _Q6V64_internal_union.d;                                                   \
  })

#else /* !__qdsp6__ */

#define Q6V64_CREATE_B(b7, b6, b5, b4, b3, b2, b1, b0)                         \
  ((((Q6Vect64)(b7)) << 56LL) | (((Q6Vect64)((b6) & 0xff)) << 48LL) |          \
   (((Q6Vect64)((b5) & 0xff)) << 40LL) | (((Q6Vect64)((b4) & 0xff)) << 32LL) | \
   (((Q6Vect64)((b3) & 0xff)) << 24LL) | (((Q6Vect64)((b2) & 0xff)) << 16LL) | \
   (((Q6Vect64)((b1) & 0xff)) << 8LL) | ((Q6Vect64)((b0) & 0xff)))

#endif /* !__qdsp6__ */

#ifdef __cplusplus

class Q6Vect64C {
public:
  // Constructors
  Q6Vect64C(long long d = 0) : data(d) {};
  Q6Vect64C(int w1, int w0) : data(Q6V64_CREATE_W(w1, w0)) {};
  Q6Vect64C(short h3, short h2, short h1, short h0)
      : data(Q6V64_CREATE_H(h3, h2, h1, h0)) {};
  Q6Vect64C(signed char b7, signed char b6, signed char b5, signed char b4,
            signed char b3, signed char b2, signed char b1, signed char b0)
      : data(Q6V64_CREATE_B(b7, b6, b5, b4, b3, b2, b1, b0)) {};
  Q6Vect64C(const Q6Vect64C &v) : data(v.data) {};

  Q6Vect64C &operator=(const Q6Vect64C &v) {
    data = v.data;
    return *this;
  };

  operator long long() {
    return data;
  };

  // Extract doubleword methods
  long long D(void) {
    return Q6V64_GET_D(data);
  };
  unsigned long long UD(void) {
    return Q6V64_GET_UD(data);
  };

  // Extract word methods
  int W0(void) {
    return Q6V64_GET_W0(data);
  };
  int W1(void) {
    return Q6V64_GET_W1(data);
  };
  unsigned int UW0(void) {
    return Q6V64_GET_UW0(data);
  };
  unsigned int UW1(void) {
    return Q6V64_GET_UW1(data);
  };

  // Extract half word methods
  short H0(void) {
    return Q6V64_GET_H0(data);
  };
  short H1(void) {
    return Q6V64_GET_H1(data);
  };
  short H2(void) {
    return Q6V64_GET_H2(data);
  };
  short H3(void) {
    return Q6V64_GET_H3(data);
  };
  unsigned short UH0(void) {
    return Q6V64_GET_UH0(data);
  };
  unsigned short UH1(void) {
    return Q6V64_GET_UH1(data);
  };
  unsigned short UH2(void) {
    return Q6V64_GET_UH2(data);
  };
  unsigned short UH3(void) {
    return Q6V64_GET_UH3(data);
  };

  // Extract byte methods
  signed char B0(void) {
    return Q6V64_GET_B0(data);
  };
  signed char B1(void) {
    return Q6V64_GET_B1(data);
  };
  signed char B2(void) {
    return Q6V64_GET_B2(data);
  };
  signed char B3(void) {
    return Q6V64_GET_B3(data);
  };
  signed char B4(void) {
    return Q6V64_GET_B4(data);
  };
  signed char B5(void) {
    return Q6V64_GET_B5(data);
  };
  signed char B6(void) {
    return Q6V64_GET_B6(data);
  };
  signed char B7(void) {
    return Q6V64_GET_B7(data);
  };
  unsigned char UB0(void) {
    return Q6V64_GET_UB0(data);
  };
  unsigned char UB1(void) {
    return Q6V64_GET_UB1(data);
  };
  unsigned char UB2(void) {
    return Q6V64_GET_UB2(data);
  };
  unsigned char UB3(void) {
    return Q6V64_GET_UB3(data);
  };
  unsigned char UB4(void) {
    return Q6V64_GET_UB4(data);
  };
  unsigned char UB5(void) {
    return Q6V64_GET_UB5(data);
  };
  unsigned char UB6(void) {
    return Q6V64_GET_UB6(data);
  };
  unsigned char UB7(void) {
    return Q6V64_GET_UB7(data);
  };

  // NOTE: All set methods return a Q6Vect64C type

  // Set doubleword method
  Q6Vect64C D(long long d) {
    return Q6Vect64C(Q6V64_PUT_D(data, d));
  };

  // Set word methods
  Q6Vect64C W0(int w) {
    return Q6Vect64C(Q6V64_PUT_W0(data, w));
  };
  Q6Vect64C W1(int w) {
    return Q6Vect64C(Q6V64_PUT_W1(data, w));
  };

  // Set half word methods
  Q6Vect64C H0(short h) {
    return Q6Vect64C(Q6V64_PUT_H0(data, h));
  };
  Q6Vect64C H1(short h) {
    return Q6Vect64C(Q6V64_PUT_H1(data, h));
  };
  Q6Vect64C H2(short h) {
    return Q6Vect64C(Q6V64_PUT_H2(data, h));
  };
  Q6Vect64C H3(short h) {
    return Q6Vect64C(Q6V64_PUT_H3(data, h));
  };

  // Set byte methods
  Q6Vect64C B0(signed char b) {
    return Q6Vect64C(Q6V64_PUT_B0(data, b));
  };
  Q6Vect64C B1(signed char b) {
    return Q6Vect64C(Q6V64_PUT_B1(data, b));
  };
  Q6Vect64C B2(signed char b) {
    return Q6Vect64C(Q6V64_PUT_B2(data, b));
  };
  Q6Vect64C B3(signed char b) {
    return Q6Vect64C(Q6V64_PUT_B3(data, b));
  };
  Q6Vect64C B4(signed char b) {
    return Q6Vect64C(Q6V64_PUT_B4(data, b));
  };
  Q6Vect64C B5(signed char b) {
    return Q6Vect64C(Q6V64_PUT_B5(data, b));
  };
  Q6Vect64C B6(signed char b) {
    return Q6Vect64C(Q6V64_PUT_B6(data, b));
  };
  Q6Vect64C B7(signed char b) {
    return Q6Vect64C(Q6V64_PUT_B7(data, b));
  };

private:
  long long data;
};

#endif /* __cplusplus */

/* 32 Bit Vectors */

typedef int Q6Vect32;

/* Extract word macros */

#define Q6V32_GET_W(v) (v)
#define Q6V32_GET_UW(v) ((unsigned int)(v))

/* Extract half word macros */

#define Q6V32_GET_H0(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      short h[2];                                                              \
    } _Q6V32_internal_union;                                                   \
    _Q6V32_internal_union.w = (v);                                             \
    _Q6V32_internal_union.h[0];                                                \
  })
#define Q6V32_GET_H1(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      short h[2];                                                              \
    } _Q6V32_internal_union;                                                   \
    _Q6V32_internal_union.w = (v);                                             \
    _Q6V32_internal_union.h[1];                                                \
  })
#define Q6V32_GET_UH0(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      unsigned short uh[2];                                                    \
    } _Q6V32_internal_union;                                                   \
    _Q6V32_internal_union.w = (v);                                             \
    _Q6V32_internal_union.uh[0];                                               \
  })
#define Q6V32_GET_UH1(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      unsigned short uh[2];                                                    \
    } _Q6V32_internal_union;                                                   \
    _Q6V32_internal_union.w = (v);                                             \
    _Q6V32_internal_union.uh[1];                                               \
  })

/* Extract byte macros */

#define Q6V32_GET_B0(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      signed char b[4];                                                        \
    } _Q6V32_internal_union;                                                   \
    _Q6V32_internal_union.w = (v);                                             \
    _Q6V32_internal_union.b[0];                                                \
  })
#define Q6V32_GET_B1(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      signed char b[4];                                                        \
    } _Q6V32_internal_union;                                                   \
    _Q6V32_internal_union.w = (v);                                             \
    _Q6V32_internal_union.b[1];                                                \
  })
#define Q6V32_GET_B2(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      signed char b[4];                                                        \
    } _Q6V32_internal_union;                                                   \
    _Q6V32_internal_union.w = (v);                                             \
    _Q6V32_internal_union.b[2];                                                \
  })
#define Q6V32_GET_B3(v)                                                        \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      signed char b[4];                                                        \
    } _Q6V32_internal_union;                                                   \
    _Q6V32_internal_union.w = (v);                                             \
    _Q6V32_internal_union.b[3];                                                \
  })
#define Q6V32_GET_UB0(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      unsigned char ub[4];                                                     \
    } _Q6V32_internal_union;                                                   \
    _Q6V32_internal_union.w = (v);                                             \
    _Q6V32_internal_union.ub[0];                                               \
  })
#define Q6V32_GET_UB1(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      unsigned char ub[4];                                                     \
    } _Q6V32_internal_union;                                                   \
    _Q6V32_internal_union.w = (v);                                             \
    _Q6V32_internal_union.ub[1];                                               \
  })
#define Q6V32_GET_UB2(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      unsigned char ub[4];                                                     \
    } _Q6V32_internal_union;                                                   \
    _Q6V32_internal_union.w = (v);                                             \
    _Q6V32_internal_union.ub[2];                                               \
  })
#define Q6V32_GET_UB3(v)                                                       \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      unsigned char ub[4];                                                     \
    } _Q6V32_internal_union;                                                   \
    _Q6V32_internal_union.w = (v);                                             \
    _Q6V32_internal_union.ub[3];                                               \
  })

/* NOTE: All set macros return a Q6Vect32 type */

/* Set word macro */

#define Q6V32_PUT_W(v, new) (new)

/* Set half word macros */

#ifdef __qdsp6__

#define Q6V32_PUT_H0(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      short h[2];                                                              \
    } _Q6V32_internal_union;                                                   \
    _Q6V32_internal_union.w = (v);                                             \
    _Q6V32_internal_union.h[0] = (new);                                        \
    _Q6V32_internal_union.w;                                                   \
  })
#define Q6V32_PUT_H1(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      short h[2];                                                              \
    } _Q6V32_internal_union;                                                   \
    _Q6V32_internal_union.w = (v);                                             \
    _Q6V32_internal_union.h[1] = (new);                                        \
    _Q6V32_internal_union.w;                                                   \
  })

#else /* !__qdsp6__ */

#define Q6V32_PUT_H0(v, new)                                                   \
  (((v) & 0xffff0000) | ((Q6Vect32)((unsigned short)(new))))
#define Q6V32_PUT_H1(v, new) (((v) & 0x0000ffff) | (((Q6Vect32)(new)) << 16))

#endif /* !__qdsp6__ */

/* Set byte macros */

#ifdef __qdsp6__

#define Q6V32_PUT_B0(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      char b[4];                                                               \
    } _Q6V32_internal_union;                                                   \
    _Q6V32_internal_union.w = (v);                                             \
    _Q6V32_internal_union.b[0] = (new);                                        \
    _Q6V32_internal_union.w;                                                   \
  })
#define Q6V32_PUT_B1(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      char b[4];                                                               \
    } _Q6V32_internal_union;                                                   \
    _Q6V32_internal_union.w = (v);                                             \
    _Q6V32_internal_union.b[1] = (new);                                        \
    _Q6V32_internal_union.w;                                                   \
  })
#define Q6V32_PUT_B2(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      char b[4];                                                               \
    } _Q6V32_internal_union;                                                   \
    _Q6V32_internal_union.w = (v);                                             \
    _Q6V32_internal_union.b[2] = (new);                                        \
    _Q6V32_internal_union.w;                                                   \
  })
#define Q6V32_PUT_B3(v, new)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      int w;                                                                   \
      char b[4];                                                               \
    } _Q6V32_internal_union;                                                   \
    _Q6V32_internal_union.w = (v);                                             \
    _Q6V32_internal_union.b[3] = (new);                                        \
    _Q6V32_internal_union.w;                                                   \
  })

#else /* !__qdsp6__ */

#define Q6V32_PUT_B0(v, new)                                                   \
  (((v) & 0xffffff00) | ((Q6Vect32)((unsigned char)(new))))
#define Q6V32_PUT_B1(v, new)                                                   \
  (((v) & 0xffff00ff) | (((Q6Vect32)((unsigned char)(new))) << 8))
#define Q6V32_PUT_B2(v, new)                                                   \
  (((v) & 0xff00ffff) | (((Q6Vect32)((unsigned char)(new))) << 16))
#define Q6V32_PUT_B3(v, new) (((v) & 0x00ffffff) | (((Q6Vect32)(new)) << 24))

#endif /* !__qdsp6__ */

/* NOTE: All create macros return a Q6Vect32 type */

/* Create from a word */

#define Q6V32_CREATE_W(w) (w)

/* Create from half words */

#ifdef __qdsp6__

#define Q6V32_CREATE_H(h1, h0)                                                 \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      short h[2];                                                              \
    } _Q6V32_internal_union;                                                   \
    _Q6V32_internal_union.h[0] = (h0);                                         \
    _Q6V32_internal_union.h[1] = (h1);                                         \
    _Q6V32_internal_union.d;                                                   \
  })

#else /* !__qdsp6__ */

#define Q6V32_CREATE_H(h1, h0)                                                 \
  ((((Q6Vect32)(h1)) << 16) | ((Q6Vect32)((h0) & 0xffff)))

#endif /* !__qdsp6__ */

/* Create from bytes */
#ifdef __qdsp6__

#define Q6V32_CREATE_B(b3, b2, b1, b0)                                         \
  __extension__({                                                              \
    union {                                                                    \
      long long d;                                                             \
      char b[4];                                                               \
    } _Q6V32_internal_union;                                                   \
    _Q6V32_internal_union.b[0] = (b0);                                         \
    _Q6V32_internal_union.b[1] = (b1);                                         \
    _Q6V32_internal_union.b[2] = (b2);                                         \
    _Q6V32_internal_union.b[3] = (b3);                                         \
    _Q6V32_internal_union.d;                                                   \
  })

#else /* !__qdsp6__ */

#define Q6V32_CREATE_B(b3, b2, b1, b0)                                         \
  ((((Q6Vect32)(b3)) << 24) | (((Q6Vect32)((b2) & 0xff)) << 16) |              \
   (((Q6Vect32)((b1) & 0xff)) << 8) | ((Q6Vect32)((b0) & 0xff)))

#endif /* !__qdsp6__ */

#ifdef __cplusplus

class Q6Vect32C {
public:
  // Constructors
  Q6Vect32C(int w = 0) : data(w) {};
  Q6Vect32C(short h1, short h0) : data(Q6V32_CREATE_H(h1, h0)) {};
  Q6Vect32C(signed char b3, signed char b2, signed char b1, signed char b0)
      : data(Q6V32_CREATE_B(b3, b2, b1, b0)) {};
  Q6Vect32C(const Q6Vect32C &v) : data(v.data) {};

  Q6Vect32C &operator=(const Q6Vect32C &v) {
    data = v.data;
    return *this;
  };

  operator int() {
    return data;
  };

  // Extract word methods
  int W(void) {
    return Q6V32_GET_W(data);
  };
  unsigned int UW(void) {
    return Q6V32_GET_UW(data);
  };

  // Extract half word methods
  short H0(void) {
    return Q6V32_GET_H0(data);
  };
  short H1(void) {
    return Q6V32_GET_H1(data);
  };
  unsigned short UH0(void) {
    return Q6V32_GET_UH0(data);
  };
  unsigned short UH1(void) {
    return Q6V32_GET_UH1(data);
  };

  // Extract byte methods
  signed char B0(void) {
    return Q6V32_GET_B0(data);
  };
  signed char B1(void) {
    return Q6V32_GET_B1(data);
  };
  signed char B2(void) {
    return Q6V32_GET_B2(data);
  };
  signed char B3(void) {
    return Q6V32_GET_B3(data);
  };
  unsigned char UB0(void) {
    return Q6V32_GET_UB0(data);
  };
  unsigned char UB1(void) {
    return Q6V32_GET_UB1(data);
  };
  unsigned char UB2(void) {
    return Q6V32_GET_UB2(data);
  };
  unsigned char UB3(void) {
    return Q6V32_GET_UB3(data);
  };

  // NOTE: All set methods return a Q6Vect32C type

  // Set word method
  Q6Vect32C W(int w) {
    return Q6Vect32C(Q6V32_PUT_W(data, w));
  };

  // Set half word methods
  Q6Vect32C H0(short h) {
    return Q6Vect32C(Q6V32_PUT_H0(data, h));
  };
  Q6Vect32C H1(short h) {
    return Q6Vect32C(Q6V32_PUT_H1(data, h));
  };

  // Set byte methods
  Q6Vect32C B0(signed char b) {
    return Q6Vect32C(Q6V32_PUT_B0(data, b));
  };
  Q6Vect32C B1(signed char b) {
    return Q6Vect32C(Q6V32_PUT_B1(data, b));
  };
  Q6Vect32C B2(signed char b) {
    return Q6Vect32C(Q6V32_PUT_B2(data, b));
  };
  Q6Vect32C B3(signed char b) {
    return Q6Vect32C(Q6V32_PUT_B3(data, b));
  };

private:
  int data;
};

#endif /* __cplusplus */

// V65 Vector types
#if __HVX_ARCH__ >= 65
#if defined __HVX__ && (__HVX_LENGTH__ == 128)
typedef long Q6VecPred128 __attribute__((__vector_size__(128)))
    __attribute__((aligned(128)));

typedef long Q6Vect1024 __attribute__((__vector_size__(128)))
    __attribute__((aligned(128)));

typedef long Q6Vect2048 __attribute__((__vector_size__(256)))
    __attribute__((aligned(256)));

#else /* defined __HVX__ && (__HVX_LENGTH__ == 128) */
#if defined __HVX__ &&  (__HVX_LENGTH__ == 64)
typedef long Q6VecPred64 __attribute__((__vector_size__(64)))
    __attribute__((aligned(64)));

typedef long Q6Vect512 __attribute__((__vector_size__(64)))
    __attribute__((aligned(64)));

typedef long Q6Vect1024 __attribute__((__vector_size__(128)))
    __attribute__((aligned(128)));

#endif /* defined __HVX__ &&  (__HVX_LENGTH__ == 64) */
#endif /* defined __HVX__ && (__HVX_LENGTH__ == 128) */
#endif /* __HVX_ARCH__ >= 65 */

/* Predicates */

typedef int Q6Pred;


#ifdef __HVX__

// Extract HVX VectorPair macro.
#define HEXAGON_HVX_GET_W(v) (v)

// Extract HVX Vector macros.
#define HEXAGON_HVX_GET_V0(v)                                                  \
  __extension__({                                                              \
    union {                                                                    \
      HVX_VectorPair W;                                                        \
      HVX_Vector V[2];                                                         \
    } _HEXAGON_HVX_internal_union;                                             \
    _HEXAGON_HVX_internal_union.W = (v);                                       \
    _HEXAGON_HVX_internal_union.V[0];                                          \
  })
#define HEXAGON_HVX_GET_V1(v)                                                  \
  __extension__({                                                              \
    union {                                                                    \
      HVX_VectorPair W;                                                        \
      HVX_Vector V[2];                                                         \
    } _HEXAGON_HVX_internal_union;                                             \
    _HEXAGON_HVX_internal_union.W = (v);                                       \
    _HEXAGON_HVX_internal_union.V[1];                                          \
  })
#define HEXAGON_HVX_GET_P(v)                                                   \
  __extension__({                                                              \
    union {                                                                    \
      HVX_VectorPair W;                                                        \
      HVX_VectorPred P[2];                                                     \
    } _HEXAGON_HVX_internal_union;                                             \
    _HEXAGON_HVX_internal_union.W = (v);                                       \
    _HEXAGON_HVX_internal_union.P[0];                                          \
  })

// Set HVX VectorPair macro.
#define HEXAGON_HVX_PUT_W(v, new) (new)

// Set HVX Vector macros.
#define HEXAGON_HVX_PUT_V0(v, new)                                             \
  __extension__({                                                              \
    union {                                                                    \
      HVX_VectorPair W;                                                        \
      HVX_Vector V[2];                                                         \
    } _HEXAGON_HVX_internal_union;                                             \
    _HEXAGON_HVX_internal_union.W = (v);                                       \
    _HEXAGON_HVX_internal_union.V[0] = (new);                                  \
    _HEXAGON_HVX_internal_union.W;                                             \
  })

#define HEXAGON_HVX_PUT_V1(v, new)                                             \
  __extension__({                                                              \
    union {                                                                    \
      HVX_VectorPair W;                                                        \
      HVX_Vector V[2];                                                         \
    } _HEXAGON_HVX_internal_union;                                             \
    _HEXAGON_HVX_internal_union.W = (v);                                       \
    _HEXAGON_HVX_internal_union.V[1] = (new);                                  \
    _HEXAGON_HVX_internal_union.W;                                             \
  })

#define HEXAGON_HVX_PUT_P(v, new)                                              \
  __extension__({                                                              \
    union {                                                                    \
      HVX_VectorPair W;                                                        \
      HVX_VectorPred P[2];                                                     \
    } _HEXAGON_HVX_internal_union;                                             \
    _HEXAGON_HVX_internal_union.W = (v);                                       \
    _HEXAGON_HVX_internal_union.P[0] = (new);                                  \
    _HEXAGON_HVX_internal_union.W;                                             \
  })


#define HEXAGON_HVX_CREATE_W(v1, v0)                                           \
  __extension__({                                                              \
    union {                                                                    \
      HVX_VectorPair W;                                                        \
      HVX_Vector V[2];                                                         \
    } _HEXAGON_HVX_internal_union;                                             \
    _HEXAGON_HVX_internal_union.V[0] = (v0);                                   \
    _HEXAGON_HVX_internal_union.V[1] = (v1);                                   \
    _HEXAGON_HVX_internal_union.W;                                             \
  })

#ifdef __cplusplus

class HVX_Vect {
public:
  // Constructors.
  // Default.
  HVX_Vect() : data(Q6_W_vcombine_VV(Q6_V_vzero(), Q6_V_vzero())){};

  // Custom constructors.
  HVX_Vect(HVX_VectorPair W) : data(W){};
  HVX_Vect(HVX_Vector v1, HVX_Vector v0) : data(HEXAGON_HVX_CREATE_W(v1, v0)){};

  // Copy constructor.
  HVX_Vect(const HVX_Vect &W) = default;

  // Move constructor.
  HVX_Vect(HVX_Vect &&W) = default;

  // Assignment operator.
  HVX_Vect &operator=(const HVX_Vect &W) = default;

  operator HVX_VectorPair() { return data; };

  // Extract VectorPair method.
  HVX_VectorPair W(void) { return HEXAGON_HVX_GET_W(data); };

  // Extract Vector methods.
  HVX_Vector V0(void) { return HEXAGON_HVX_GET_V0(data); };
  HVX_Vector V1(void) { return HEXAGON_HVX_GET_V1(data); };
  HVX_VectorPred P(void) { return HEXAGON_HVX_GET_P(data); };

  // NOTE: All set methods return a HVX_Vect type.
  // Set HVX VectorPair method.
  HVX_Vect W(HVX_VectorPair w) { return HVX_Vect(HEXAGON_HVX_PUT_W(data, w)); };

  // Set HVX Vector methods.
  HVX_Vect V0(HVX_Vector v) { return HVX_Vect(HEXAGON_HVX_PUT_V0(data, v)); };
  HVX_Vect V1(HVX_Vector v) { return HVX_Vect(HEXAGON_HVX_PUT_V1(data, v)); };
  HVX_Vect P(HVX_VectorPred p) { return HVX_Vect(HEXAGON_HVX_PUT_P(data, p)); };

private:
  HVX_VectorPair data;
};

#endif /* __cplusplus */
#endif /* __HVX__ */

#define HEXAGON_UDMA_DM0_STATUS_IDLE             0x00000000
#define HEXAGON_UDMA_DM0_STATUS_RUN              0x00000001
#define HEXAGON_UDMA_DM0_STATUS_ERROR            0x00000002
#define HEXAGON_UDMA_DESC_DSTATE_INCOMPLETE      0
#define HEXAGON_UDMA_DESC_DSTATE_COMPLETE        1
#define HEXAGON_UDMA_DESC_ORDER_NOORDER          0
#define HEXAGON_UDMA_DESC_ORDER_ORDER            1
#define HEXAGON_UDMA_DESC_BYPASS_OFF             0
#define HEXAGON_UDMA_DESC_BYPASS_ON              1
#define HEXAGON_UDMA_DESC_COMP_NONE              0
#define HEXAGON_UDMA_DESC_COMP_DLBC              1
#define HEXAGON_UDMA_DESC_DESCTYPE_TYPE0         0
#define HEXAGON_UDMA_DESC_DESCTYPE_TYPE1         1

typedef struct hexagon_udma_descriptor_type0_s
{
    void *next;
    unsigned int length:24;
    unsigned int desctype:2;
    unsigned int dstcomp:1;
    unsigned int srccomp:1;
    unsigned int dstbypass:1;
    unsigned int srcbypass:1;
    unsigned int order:1;
    unsigned int dstate:1;
    void *src;
    void *dst;
} hexagon_udma_descriptor_type0_t;

typedef struct hexagon_udma_descriptor_type1_s
{
    void *next;
    unsigned int length:24;
    unsigned int desctype:2;
    unsigned int dstcomp:1;
    unsigned int srccomp:1;
    unsigned int dstbypass:1;
    unsigned int srcbypass:1;
    unsigned int order:1;
    unsigned int dstate:1;
    void *src;
    void *dst;
    unsigned int allocation:28;
    unsigned int padding:4;
    unsigned int roiwidth:16;
    unsigned int roiheight:16;
    unsigned int srcstride:16;
    unsigned int dststride:16;
    unsigned int srcwidthoffset:16;
    unsigned int dstwidthoffset:16;
} hexagon_udma_descriptor_type1_t;

#endif /* !HEXAGON_TYPES_H */
