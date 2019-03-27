#ifdef IN_SANDY2X

/*
   REDMASK51 is from amd64-51/consts.s.
*/

#include "consts_namespace.h"
.data
.p2align 4
v0_0: .quad 0, 0
v1_0: .quad 1, 0
v2_1: .quad 2, 1
v9_0: .quad 9, 0
v9_9: .quad 9, 9
v19_19: .quad 19, 19
v38_1: .quad 38, 1
v38_38: .quad 38, 38
v121666_121666: .quad 121666, 121666
m25: .quad 33554431, 33554431
m26: .quad 67108863, 67108863
subc0: .quad 0x07FFFFDA, 0x03FFFFFE
subc2: .quad 0x07FFFFFE, 0x03FFFFFE
REDMASK51:   .quad 0x0007FFFFFFFFFFFF

#endif
