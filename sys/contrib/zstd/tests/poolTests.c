/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */


#include "pool.h"
#include "threading.h"
#include "util.h"
#include <stddef.h>
#include <stdio.h>

#define ASSERT_TRUE(p)                                                       \
  do {                                                                       \
    if (!(p)) {                                                              \
      return 1;                                                              \
    }                                                                        \
  } while (0)
#define ASSERT_FALSE(p) ASSERT_TRUE(!(p))
#define ASSERT_EQ(lhs, rhs) ASSERT_TRUE((lhs) == (rhs))

struct data {
  pthread_mutex_t mutex;
  unsigned data[16];
  size_t i;
};

static void fn(void *opaque) {
  struct data *data = (struct data *)opaque;
  ZSTD_pthread_mutex_lock(&data->mutex);
  data->data[data->i] = data->i;
  ++data->i;
  ZSTD_pthread_mutex_unlock(&data->mutex);
}

static int testOrder(size_t numThreads, size_t queueSize) {
  struct data data;
  POOL_ctx *ctx = POOL_create(numThreads, queueSize);
  ASSERT_TRUE(ctx);
  data.i = 0;
  ZSTD_pthread_mutex_init(&data.mutex, NULL);
  { size_t i;
    for (i = 0; i < 16; ++i) {
      POOL_add(ctx, &fn, &data);
    }
  }
  POOL_free(ctx);
  ASSERT_EQ(16, data.i);
  { size_t i;
    for (i = 0; i < data.i; ++i) {
      ASSERT_EQ(i, data.data[i]);
    }
  }
  ZSTD_pthread_mutex_destroy(&data.mutex);
  return 0;
}


/* --- test deadlocks --- */

static void waitFn(void *opaque) {
  (void)opaque;
  UTIL_sleepMilli(1);
}

/* Tests for deadlock */
static int testWait(size_t numThreads, size_t queueSize) {
  struct data data;
  POOL_ctx *ctx = POOL_create(numThreads, queueSize);
  ASSERT_TRUE(ctx);
  { size_t i;
    for (i = 0; i < 16; ++i) {
        POOL_add(ctx, &waitFn, &data);
    }
  }
  POOL_free(ctx);
  return 0;
}


/* --- test POOL_resize() --- */

typedef struct {
    ZSTD_pthread_mutex_t mut;
    int val;
    int max;
    ZSTD_pthread_cond_t cond;
} poolTest_t;

static void waitLongFn(void *opaque) {
  poolTest_t* test = (poolTest_t*) opaque;
  UTIL_sleepMilli(10);
  ZSTD_pthread_mutex_lock(&test->mut);
  test->val = test->val + 1;
  if (test->val == test->max)
    ZSTD_pthread_cond_signal(&test->cond);
  ZSTD_pthread_mutex_unlock(&test->mut);
}

static int testThreadReduction_internal(POOL_ctx* ctx, poolTest_t test)
{
    int const nbWaits = 16;
    UTIL_time_t startTime;
    U64 time4threads, time2threads;

    test.val = 0;
    test.max = nbWaits;

    startTime = UTIL_getTime();
    {   int i;
        for (i=0; i<nbWaits; i++)
            POOL_add(ctx, &waitLongFn, &test);
    }
    ZSTD_pthread_mutex_lock(&test.mut);
    ZSTD_pthread_cond_wait(&test.cond, &test.mut);
    ASSERT_EQ(test.val, nbWaits);
    ZSTD_pthread_mutex_unlock(&test.mut);
    time4threads = UTIL_clockSpanNano(startTime);

    ASSERT_EQ( POOL_resize(ctx, 2/*nbThreads*/) , 0 );
    test.val = 0;
    startTime = UTIL_getTime();
    {   int i;
        for (i=0; i<nbWaits; i++)
            POOL_add(ctx, &waitLongFn, &test);
    }
    ZSTD_pthread_mutex_lock(&test.mut);
    ZSTD_pthread_cond_wait(&test.cond, &test.mut);
    ASSERT_EQ(test.val, nbWaits);
    ZSTD_pthread_mutex_unlock(&test.mut);
    time2threads = UTIL_clockSpanNano(startTime);

    if (time4threads >= time2threads) return 1;   /* check 4 threads were effectively faster than 2 */
    return 0;
}

static int testThreadReduction(void) {
    int result;
    poolTest_t test;
    POOL_ctx* const ctx = POOL_create(4 /*nbThreads*/, 2 /*queueSize*/);

    ASSERT_TRUE(ctx);

    memset(&test, 0, sizeof(test));
    ASSERT_FALSE( ZSTD_pthread_mutex_init(&test.mut, NULL) );
    ASSERT_FALSE( ZSTD_pthread_cond_init(&test.cond, NULL) );

    result = testThreadReduction_internal(ctx, test);

    ZSTD_pthread_mutex_destroy(&test.mut);
    ZSTD_pthread_cond_destroy(&test.cond);
    POOL_free(ctx);

    return result;
}


/* --- test abrupt ending --- */

typedef struct {
    ZSTD_pthread_mutex_t mut;
    int val;
} abruptEndCanary_t;

static void waitIncFn(void *opaque) {
  abruptEndCanary_t* test = (abruptEndCanary_t*) opaque;
  UTIL_sleepMilli(10);
  ZSTD_pthread_mutex_lock(&test->mut);
  test->val = test->val + 1;
  ZSTD_pthread_mutex_unlock(&test->mut);
}

static int testAbruptEnding_internal(abruptEndCanary_t test)
{
    int const nbWaits = 16;

    POOL_ctx* const ctx = POOL_create(3 /*numThreads*/, nbWaits /*queueSize*/);
    ASSERT_TRUE(ctx);
    test.val = 0;

    {   int i;
        for (i=0; i<nbWaits; i++)
            POOL_add(ctx, &waitIncFn, &test);  /* all jobs pushed into queue */
    }
    ASSERT_EQ( POOL_resize(ctx, 1 /*numThreads*/) , 0 );   /* downsize numThreads, to try to break end condition */

    POOL_free(ctx);  /* must finish all jobs in queue before giving back control */
    ASSERT_EQ(test.val, nbWaits);
    return 0;
}

static int testAbruptEnding(void) {
    int result;
    abruptEndCanary_t test;

    memset(&test, 0, sizeof(test));
    ASSERT_FALSE( ZSTD_pthread_mutex_init(&test.mut, NULL) );

    result = testAbruptEnding_internal(test);

    ZSTD_pthread_mutex_destroy(&test.mut);
    return result;
}



/* --- test launcher --- */

int main(int argc, const char **argv) {
  size_t numThreads;
  (void)argc;
  (void)argv;

  if (POOL_create(0, 1)) {   /* should not be possible */
    printf("FAIL: should not create POOL with 0 threads\n");
    return 1;
  }

  for (numThreads = 1; numThreads <= 4; ++numThreads) {
    size_t queueSize;
    for (queueSize = 0; queueSize <= 2; ++queueSize) {
      printf("queueSize==%u, numThreads=%u \n",
            (unsigned)queueSize, (unsigned)numThreads);
      if (testOrder(numThreads, queueSize)) {
        printf("FAIL: testOrder\n");
        return 1;
      }
      printf("SUCCESS: testOrder\n");
      if (testWait(numThreads, queueSize)) {
        printf("FAIL: testWait\n");
        return 1;
      }
      printf("SUCCESS: testWait\n");
    }
  }

  if (testThreadReduction()) {
      printf("FAIL: thread reduction not effective \n");
      return 1;
  } else {
      printf("SUCCESS: thread reduction effective (slower execution) \n");
  }

  if (testAbruptEnding()) {
      printf("FAIL: jobs in queue not completed on early end \n");
      return 1;
  } else {
      printf("SUCCESS: all jobs in queue completed on early end \n");
  }

  printf("PASS: all POOL tests\n");

  return 0;
}
