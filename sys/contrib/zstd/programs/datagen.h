/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */


#ifndef DATAGEN_H
#define DATAGEN_H

#include <stddef.h>   /* size_t */

void RDG_genStdout(unsigned long long size, double matchProba, double litProba, unsigned seed);
void RDG_genBuffer(void* buffer, size_t size, double matchProba, double litProba, unsigned seed);
/*!RDG_genBuffer
   Generate 'size' bytes of compressible data into 'buffer'.
   Compressibility can be controlled using 'matchProba', which is floating point value between 0 and 1.
   'LitProba' is optional, it affect variability of individual bytes. If litProba==0.0, default value will be used.
   Generated data pattern can be modified using different 'seed'.
   For a triplet (matchProba, litProba, seed), the function always generate the same content.

   RDG_genStdout
   Same as RDG_genBuffer, but generates data into stdout
*/

#endif
