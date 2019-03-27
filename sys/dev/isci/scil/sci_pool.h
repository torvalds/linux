/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
/**
 * @file
 *
 * @brief This file contains the interface to the pool class.
 *        This class allows two different two different priority tasks to
 *        insert and remove items from the free pool. The user of the pool
 *        is expected to evaluate the pool condition empty before a get
 *        operation and pool condition full before a put operation.
 *        Methods Provided:
 *        - sci_pool_create()
 *        - sci_pool_initialize()
 *        - sci_pool_empty()
 *        - sci_pool_full()
 *        - sci_pool_get()
 *        - sci_pool_put()
 */

#ifndef _SCI_POOL_H_
#define _SCI_POOL_H_

#include <dev/isci/types.h>

/**
 * Private operation for the pool
 */
#define SCI_POOL_INCREMENT(this_pool, index) \
  (((index) + 1) == (this_pool).size ? 0 : (index) + 1)

/**
 * This creates a pool structure of pool_name. The members in the pool are
 * of type with number of elements equal to size.
 */
#define SCI_POOL_CREATE(pool_name, type, pool_size) \
struct \
{ \
   U32 size; \
   U32 get; \
   U32 put; \
   type array[(pool_size) + 1]; \
} pool_name


/**
 * This macro evaluates the pool and returns TRUE if the pool is empty.
 * If the pool is empty the user should not perform any get operation on
 * the pool.
 */
#define sci_pool_empty(this_pool) \
   ((this_pool).get == (this_pool).put)

/**
 * This macro evaluates the pool and returns TRUE if the pool is full.  If
 * the pool is full the user should not perform any put operation.
 */
#define sci_pool_full(this_pool) \
   (SCI_POOL_INCREMENT(this_pool, (this_pool).put) == (this_pool).get)

/**
 * This macro returns the size of the pool created.  The internal size
 * of the pool is actually 1 larger then necessary in order to ensure
 * get and put pointers can be written simultaneously by different
 * users.  As a result, this macro subtracts 1 from the internal size
 */
#define sci_pool_size(this_pool) \
   ((this_pool).size - 1)

/**
 * This macro indicates the number of elements currently contained in the
 * pool.
 */
#define sci_pool_count(this_pool) \
   ( \
      sci_pool_empty((this_pool)) \
      ? 0 \
      : ( \
           sci_pool_full((this_pool)) \
           ? sci_pool_size((this_pool)) \
           : ( \
                (this_pool).get > (this_pool).put \
                ? ((this_pool).size - (this_pool).get + (this_pool).put) \
                : ((this_pool).put - (this_pool).get) \
             ) \
        ) \
   )

/**
 * This macro initializes the pool to an empty condition.
 */
#define sci_pool_initialize(this_pool) \
{ \
   (this_pool).size = (sizeof((this_pool).array) / sizeof((this_pool).array[0])); \
   (this_pool).get = 0; \
   (this_pool).put = 0; \
}

/**
 * This macro will get the next free element from the pool.
 * This should only be called if the pool is not empty.
 */
#define sci_pool_get(this_pool, my_value) \
{ \
   (my_value) = (this_pool).array[(this_pool).get]; \
   (this_pool).get = SCI_POOL_INCREMENT((this_pool), (this_pool).get); \
}

/**
 * This macro will put the value into the pool.
 * This should only be called if the pool is not full.
 */
#define sci_pool_put(this_pool, the_value) \
{ \
   (this_pool).array[(this_pool).put] = (the_value); \
   (this_pool).put = SCI_POOL_INCREMENT((this_pool), (this_pool).put); \
}

/**
 * This macro will search the pool and remove any elements in the pool
 * matching the supplied value.
 * @note This method can only be utilized on pools
 */
#define sci_pool_erase(this_pool, type, the_value) \
{ \
   type tmp_value; \
   U32 index; \
   U32 element_count = sci_pool_count((this_pool)); \
 \
   for (index = 0; index < element_count; index++) \
   { \
      sci_pool_get((this_pool), tmp_value); \
      if (tmp_value != (the_value)) \
         sci_pool_put((this_pool), tmp_value); \
   } \
}

#endif // _SCI_POOL_H_
