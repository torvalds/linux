/* 
 * memcopy.c -- subroutines for memory copy functions. 
 * 
 * This program is free software; you can redistribute it and/or modify it 
 * under the terms of the GNU General Public License as published by the Free 
 * Software Foundation; either version 2.1 of the License, or (at your option) 
 * any later version. 
 * 
 * This program is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General 
 * Public License for more details. 
 * 
 * You should have received a copy of the GNU General Public License along 
 * with this program; if not, write to the Free Software Foundation, Inc., 
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA. 
 * 
 * The code is derived from the GNU C Library. 
 * Copyright (C) 1991, 1992, 1993, 1997, 2004 Free Software Foundation, Inc. 
 */ 
 
/* BE VERY CAREFUL IF YOU CHANGE THIS CODE...!  */ 
 
#include <linux/memcopy.h>
 
/* 
 * _wordcopy_fwd_aligned -- Copy block beginning at SRCP to block beginning 
 * at DSTP with LEN `op_t' words (not LEN bytes!). 
 * Both SRCP and DSTP should be aligned for memory operations on `op_t's. 
 */ 
void _wordcopy_fwd_aligned (long int dstp, long int srcp, size_t len) 
{ 
    op_t a0, a1; 
 
    switch (len % 8) { 
    case 2: 
        a0 = ((op_t *) srcp)[0]; 
        srcp -= 6 * OPSIZ; 
        dstp -= 7 * OPSIZ; 
        len += 6; 
        goto do1; 
    case 3: 
        a1 = ((op_t *) srcp)[0]; 
        srcp -= 5 * OPSIZ; 
        dstp -= 6 * OPSIZ; 
        len += 5; 
        goto do2; 
    case 4: 
        a0 = ((op_t *) srcp)[0]; 
        srcp -= 4 * OPSIZ; 
        dstp -= 5 * OPSIZ; 
        len += 4; 
        goto do3; 
    case 5: 
        a1 = ((op_t *) srcp)[0]; 
        srcp -= 3 * OPSIZ; 
        dstp -= 4 * OPSIZ; 
        len += 3; 
        goto do4; 
    case 6: 
        a0 = ((op_t *) srcp)[0]; 
        srcp -= 2 * OPSIZ; 
        dstp -= 3 * OPSIZ; 
        len += 2; 
        goto do5; 
    case 7: 
        a1 = ((op_t *) srcp)[0]; 
        srcp -= 1 * OPSIZ; 
        dstp -= 2 * OPSIZ; 
        len += 1; 
        goto do6; 
    case 0: 
        if (OP_T_THRESHOLD <= 3 * OPSIZ && len == 0) 
            return; 
        a0 = ((op_t *) srcp)[0]; 
        srcp -= 0 * OPSIZ; 
        dstp -= 1 * OPSIZ; 
        goto do7; 
    case 1: 
        a1 = ((op_t *) srcp)[0]; 
        srcp -=-1 * OPSIZ; 
        dstp -= 0 * OPSIZ; 
        len -= 1; 
        if (OP_T_THRESHOLD <= 3 * OPSIZ && len == 0) 
            goto do0; 
        goto do8;            /* No-op.  */ 
    } 
 
    do { 
do8:
        a0 = ((op_t *) srcp)[0]; 
        ((op_t *) dstp)[0] = a1; 
do7:
        a1 = ((op_t *) srcp)[1]; 
        ((op_t *) dstp)[1] = a0; 
do6:
        a0 = ((op_t *) srcp)[2]; 
        ((op_t *) dstp)[2] = a1; 
do5:
        a1 = ((op_t *) srcp)[3]; 
        ((op_t *) dstp)[3] = a0; 
do4:
        a0 = ((op_t *) srcp)[4]; 
        ((op_t *) dstp)[4] = a1; 
do3:
        a1 = ((op_t *) srcp)[5]; 
        ((op_t *) dstp)[5] = a0; 
do2:
        a0 = ((op_t *) srcp)[6]; 
        ((op_t *) dstp)[6] = a1; 
do1:
        a1 = ((op_t *) srcp)[7]; 
        ((op_t *) dstp)[7] = a0; 
 
        srcp += 8 * OPSIZ; 
        dstp += 8 * OPSIZ; 
        len -= 8; 
    } while (len != 0); 
 
    /* 
     * This is the right position for do0.  Please don't move it into 
     * the loop. 
     */ 
do0: 
    ((op_t *) dstp)[0] = a1; 
} 
 
/* 
 * _wordcopy_fwd_dest_aligned -- Copy block beginning at SRCP to block 
 * beginning at DSTP with LEN `op_t' words (not LEN bytes!). DSTP should 
 * be aligned for memory operations on `op_t's, but SRCP must *not* be aligned. 
 */ 
 
void _wordcopy_fwd_dest_aligned (long int dstp, long int srcp, size_t len) 
{ 
    op_t a0, a1, a2, a3; 
    int sh_1, sh_2; 
 
    /* 
     * Calculate how to shift a word read at the memory operation aligned 
     * srcp to make it aligned for copy. 
     */ 
    sh_1 = 8 * (srcp % OPSIZ); 
    sh_2 = 8 * OPSIZ - sh_1; 
 
    /* 
     * Make SRCP aligned by rounding it down to the beginning of the `op_t' 
     * it points in the middle of. 
     */ 
    srcp &= -OPSIZ; 
 
    switch (len % 4) { 
    case 2: 
        a1 = ((op_t *) srcp)[0]; 
        a2 = ((op_t *) srcp)[1]; 
        srcp -= 1 * OPSIZ; 
        dstp -= 3 * OPSIZ; 
        len += 2; 
        goto do1; 
    case 3: 
        a0 = ((op_t *) srcp)[0]; 
        a1 = ((op_t *) srcp)[1]; 
        srcp -= 0 * OPSIZ; 
        dstp -= 2 * OPSIZ; 
        len += 1; 
        goto do2; 
    case 0: 
        if (OP_T_THRESHOLD <= 3 * OPSIZ && len == 0) 
            return; 
        a3 = ((op_t *) srcp)[0]; 
        a0 = ((op_t *) srcp)[1]; 
        srcp -=-1 * OPSIZ; 
        dstp -= 1 * OPSIZ; 
        len += 0; 
        goto do3; 
    case 1: 
        a2 = ((op_t *) srcp)[0]; 
        a3 = ((op_t *) srcp)[1]; 
        srcp -=-2 * OPSIZ; 
        dstp -= 0 * OPSIZ; 
        len -= 1; 
        if (OP_T_THRESHOLD <= 3 * OPSIZ && len == 0) 
            goto do0; 
        goto do4;            /* No-op. */ 
    } 
 
    do { 
do4: 
        a0 = ((op_t *) srcp)[0]; 
        ((op_t *) dstp)[0] = MERGE (a2, sh_1, a3, sh_2); 
do3: 
        a1 = ((op_t *) srcp)[1]; 
        ((op_t *) dstp)[1] = MERGE (a3, sh_1, a0, sh_2); 
do2: 
        a2 = ((op_t *) srcp)[2]; 
        ((op_t *) dstp)[2] = MERGE (a0, sh_1, a1, sh_2); 
do1: 
        a3 = ((op_t *) srcp)[3]; 
        ((op_t *) dstp)[3] = MERGE (a1, sh_1, a2, sh_2); 
 
        srcp += 4 * OPSIZ; 
        dstp += 4 * OPSIZ; 
        len -= 4; 
    } while (len != 0); 
 
    /* 
     * This is the right position for do0.  Please don't move it into 
     * the loop. 
     */ 
do0: 
    ((op_t *) dstp)[0] = MERGE (a2, sh_1, a3, sh_2); 
} 
 
/* 
 * _wordcopy_bwd_aligned -- Copy block finishing right before 
 * SRCP to block finishing right before DSTP with LEN `op_t' words (not LEN 
 * bytes!).  Both SRCP and DSTP should be aligned for memory operations 
 * on `op_t's. 
 */ 
void _wordcopy_bwd_aligned (long int dstp, long int srcp, size_t len) 
{ 
    op_t a0, a1; 
 
    switch (len % 8) { 
    case 2: 
        srcp -= 2 * OPSIZ; 
        dstp -= 1 * OPSIZ; 
        a0 = ((op_t *) srcp)[1]; 
        len += 6; 
        goto do1; 
    case 3: 
        srcp -= 3 * OPSIZ; 
        dstp -= 2 * OPSIZ; 
        a1 = ((op_t *) srcp)[2]; 
        len += 5; 
        goto do2; 
    case 4: 
        srcp -= 4 * OPSIZ; 
        dstp -= 3 * OPSIZ; 
        a0 = ((op_t *) srcp)[3]; 
        len += 4; 
        goto do3; 
    case 5: 
        srcp -= 5 * OPSIZ; 
        dstp -= 4 * OPSIZ; 
        a1 = ((op_t *) srcp)[4]; 
        len += 3; 
        goto do4; 
    case 6: 
        srcp -= 6 * OPSIZ; 
        dstp -= 5 * OPSIZ; 
        a0 = ((op_t *) srcp)[5]; 
        len += 2; 
        goto do5; 
    case 7: 
        srcp -= 7 * OPSIZ; 
        dstp -= 6 * OPSIZ; 
        a1 = ((op_t *) srcp)[6]; 
        len += 1; 
        goto do6; 
    case 0: 
        if (OP_T_THRESHOLD <= 3 * OPSIZ && len == 0) 
            return; 
        srcp -= 8 * OPSIZ; 
        dstp -= 7 * OPSIZ; 
        a0 = ((op_t *) srcp)[7]; 
        goto do7; 
    case 1: 
        srcp -= 9 * OPSIZ; 
        dstp -= 8 * OPSIZ; 
        a1 = ((op_t *) srcp)[8]; 
        len -= 1; 
        if (OP_T_THRESHOLD <= 3 * OPSIZ && len == 0) 
            goto do0; 
        goto do8;            /* No-op.  */ 
    } 
 
    do { 
do8: 
        a0 = ((op_t *) srcp)[7]; 
        ((op_t *) dstp)[7] = a1; 
do7: 
        a1 = ((op_t *) srcp)[6]; 
        ((op_t *) dstp)[6] = a0; 
do6: 
        a0 = ((op_t *) srcp)[5]; 
        ((op_t *) dstp)[5] = a1; 
do5: 
        a1 = ((op_t *) srcp)[4]; 
        ((op_t *) dstp)[4] = a0; 
do4: 
        a0 = ((op_t *) srcp)[3]; 
        ((op_t *) dstp)[3] = a1; 
do3: 
        a1 = ((op_t *) srcp)[2]; 
        ((op_t *) dstp)[2] = a0; 
do2: 
        a0 = ((op_t *) srcp)[1]; 
        ((op_t *) dstp)[1] = a1; 
do1: 
        a1 = ((op_t *) srcp)[0]; 
        ((op_t *) dstp)[0] = a0; 
 
        srcp -= 8 * OPSIZ; 
        dstp -= 8 * OPSIZ; 
        len -= 8; 
    } while (len != 0); 
 
    /* 
     * This is the right position for do0.  Please don't move it into 
     * the loop. 
     */ 
do0: 
    ((op_t *) dstp)[7] = a1; 
} 
 
/* 
 * _wordcopy_bwd_dest_aligned -- Copy block finishing right before SRCP to 
 * block finishing right before DSTP with LEN `op_t' words (not LEN bytes!). 
 * DSTP should be aligned for memory operations on `op_t', but SRCP must *not* 
 * be aligned. 
 */ 
void _wordcopy_bwd_dest_aligned (long int dstp, long int srcp, size_t len) 
{ 
    op_t a0, a1, a2, a3; 
    int sh_1, sh_2; 
 
    /* 
     * Calculate how to shift a word read at the memory operation aligned 
     * srcp to make it aligned for copy. 
     */ 
 
    sh_1 = 8 * (srcp % OPSIZ); 
    sh_2 = 8 * OPSIZ - sh_1; 
 
    /* 
     * Make srcp aligned by rounding it down to the beginning of the op_t 
     * it points in the middle of. 
     */ 
    srcp &= -OPSIZ; 
    srcp += OPSIZ; 
 
    switch (len % 4) { 
    case 2: 
        srcp -= 3 * OPSIZ; 
        dstp -= 1 * OPSIZ; 
        a2 = ((op_t *) srcp)[2]; 
        a1 = ((op_t *) srcp)[1]; 
        len += 2; 
        goto do1; 
    case 3: 
        srcp -= 4 * OPSIZ; 
        dstp -= 2 * OPSIZ; 
        a3 = ((op_t *) srcp)[3]; 
        a2 = ((op_t *) srcp)[2]; 
        len += 1; 
        goto do2; 
    case 0: 
        if (OP_T_THRESHOLD <= 3 * OPSIZ && len == 0) 
            return; 
        srcp -= 5 * OPSIZ; 
        dstp -= 3 * OPSIZ; 
        a0 = ((op_t *) srcp)[4]; 
        a3 = ((op_t *) srcp)[3]; 
        goto do3; 
    case 1: 
        srcp -= 6 * OPSIZ; 
        dstp -= 4 * OPSIZ; 
        a1 = ((op_t *) srcp)[5]; 
        a0 = ((op_t *) srcp)[4]; 
        len -= 1; 
        if (OP_T_THRESHOLD <= 3 * OPSIZ && len == 0) 
            goto do0; 
        goto do4;            /* No-op.  */ 
    } 
 
    do { 
do4: 
        a3 = ((op_t *) srcp)[3]; 
        ((op_t *) dstp)[3] = MERGE (a0, sh_1, a1, sh_2); 
do3: 
        a2 = ((op_t *) srcp)[2]; 
        ((op_t *) dstp)[2] = MERGE (a3, sh_1, a0, sh_2); 
do2: 
        a1 = ((op_t *) srcp)[1]; 
        ((op_t *) dstp)[1] = MERGE (a2, sh_1, a3, sh_2); 
do1: 
        a0 = ((op_t *) srcp)[0]; 
        ((op_t *) dstp)[0] = MERGE (a1, sh_1, a2, sh_2); 
 
        srcp -= 4 * OPSIZ; 
        dstp -= 4 * OPSIZ; 
        len -= 4; 
    } while (len != 0); 
 
    /* 
     * This is the right position for do0.  Please don't move it into 
     * the loop. 
     */ 
do0: 
    ((op_t *) dstp)[3] = MERGE (a0, sh_1, a1, sh_2); 
} 
 
