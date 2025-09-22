//===-- tsan_posix_util.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
// Test POSIX utils.
//===----------------------------------------------------------------------===//
#ifndef TSAN_POSIX_UTIL_H
#define TSAN_POSIX_UTIL_H

#include <pthread.h>

#ifdef __APPLE__
#define __interceptor_memcpy wrap_memcpy
#define __interceptor_memset wrap_memset
#define __interceptor_pthread_create wrap_pthread_create
#define __interceptor_pthread_join wrap_pthread_join
#define __interceptor_pthread_detach wrap_pthread_detach
#define __interceptor_pthread_mutex_init wrap_pthread_mutex_init
#define __interceptor_pthread_mutex_lock wrap_pthread_mutex_lock
#define __interceptor_pthread_mutex_unlock wrap_pthread_mutex_unlock
#define __interceptor_pthread_mutex_destroy wrap_pthread_mutex_destroy
#define __interceptor_pthread_mutex_trylock wrap_pthread_mutex_trylock
#define __interceptor_pthread_rwlock_init wrap_pthread_rwlock_init
#define __interceptor_pthread_rwlock_destroy wrap_pthread_rwlock_destroy
#define __interceptor_pthread_rwlock_trywrlock wrap_pthread_rwlock_trywrlock
#define __interceptor_pthread_rwlock_wrlock wrap_pthread_rwlock_wrlock
#define __interceptor_pthread_rwlock_unlock wrap_pthread_rwlock_unlock
#define __interceptor_pthread_rwlock_rdlock wrap_pthread_rwlock_rdlock
#define __interceptor_pthread_rwlock_tryrdlock wrap_pthread_rwlock_tryrdlock
#define __interceptor_pthread_cond_init wrap_pthread_cond_init
#define __interceptor_pthread_cond_signal wrap_pthread_cond_signal
#define __interceptor_pthread_cond_broadcast wrap_pthread_cond_broadcast
#define __interceptor_pthread_cond_wait wrap_pthread_cond_wait
#define __interceptor_pthread_cond_destroy wrap_pthread_cond_destroy
#endif

extern "C" void *__interceptor_memcpy(void *, const void *, uptr);
extern "C" void *__interceptor_memset(void *, int, uptr);
extern "C" int __interceptor_pthread_create(pthread_t *thread,
                                            const pthread_attr_t *attr,
                                            void *(*start_routine)(void *),
                                            void *arg);
extern "C" int __interceptor_pthread_join(pthread_t thread, void **value_ptr);
extern "C" int __interceptor_pthread_detach(pthread_t thread);

extern "C" int __interceptor_pthread_mutex_init(
    pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
extern "C" int __interceptor_pthread_mutex_lock(pthread_mutex_t *mutex);
extern "C" int __interceptor_pthread_mutex_unlock(pthread_mutex_t *mutex);
extern "C" int __interceptor_pthread_mutex_destroy(pthread_mutex_t *mutex);
extern "C" int __interceptor_pthread_mutex_trylock(pthread_mutex_t *mutex);

extern "C" int __interceptor_pthread_rwlock_init(
    pthread_rwlock_t *rwlock, const pthread_rwlockattr_t *attr);
extern "C" int __interceptor_pthread_rwlock_destroy(pthread_rwlock_t *rwlock);
extern "C" int __interceptor_pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock);
extern "C" int __interceptor_pthread_rwlock_wrlock(pthread_rwlock_t *rwlock);
extern "C" int __interceptor_pthread_rwlock_unlock(pthread_rwlock_t *rwlock);
extern "C" int __interceptor_pthread_rwlock_rdlock(pthread_rwlock_t *rwlock);
extern "C" int __interceptor_pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock);

extern "C" int __interceptor_pthread_cond_init(pthread_cond_t *cond,
                                               const pthread_condattr_t *attr);
extern "C" int __interceptor_pthread_cond_signal(pthread_cond_t *cond);
extern "C" int __interceptor_pthread_cond_broadcast(pthread_cond_t *cond);
extern "C" int __interceptor_pthread_cond_wait(pthread_cond_t *cond,
                                               pthread_mutex_t *mutex);
extern "C" int __interceptor_pthread_cond_destroy(pthread_cond_t *cond);

#endif  // #ifndef TSAN_POSIX_UTIL_H
