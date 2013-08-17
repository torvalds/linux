/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _LIBS_UTILS_MUTEX_H
#define _LIBS_UTILS_MUTEX_H

#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#if defined(HAVE_PTHREADS)
# include <pthread.h>
#endif

#include <utils/Errors.h>

// ---------------------------------------------------------------------------
namespace android {
// ---------------------------------------------------------------------------

class Condition;

/*
 * Simple mutex class.  The implementation is system-dependent.
 *
 * The mutex must be unlocked by the thread that locked it.  They are not
 * recursive, i.e. the same thread can't lock it multiple times.
 */
class Mutex {
public:
    enum {
        PRIVATE = 0,
        SHARED = 1
    };
    
                Mutex();
                Mutex(const char* name);
                Mutex(int type, const char* name = NULL);
                ~Mutex();

    // lock or unlock the mutex
    status_t    lock();
    void        unlock();

    // lock if possible; returns 0 on success, error otherwise
    status_t    tryLock();

    // Manages the mutex automatically. It'll be locked when Autolock is
    // constructed and released when Autolock goes out of scope.
    class Autolock {
    public:
        inline Autolock(Mutex& mutex) : mLock(mutex)  { mLock.lock(); }
        inline Autolock(Mutex* mutex) : mLock(*mutex) { mLock.lock(); }
        inline ~Autolock() { mLock.unlock(); }
    private:
        Mutex& mLock;
    };

private:
    friend class Condition;
    
    // A mutex cannot be copied
                Mutex(const Mutex&);
    Mutex&      operator = (const Mutex&);
    
#if defined(HAVE_PTHREADS)
    pthread_mutex_t mMutex;
#else
    void    _init();
    void*   mState;
#endif
};

// ---------------------------------------------------------------------------

#if defined(HAVE_PTHREADS)

inline Mutex::Mutex() {
    pthread_mutex_init(&mMutex, NULL);
}
inline Mutex::Mutex(const char* name) {
    pthread_mutex_init(&mMutex, NULL);
}
inline Mutex::Mutex(int type, const char* name) {
    if (type == SHARED) {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(&mMutex, &attr);
        pthread_mutexattr_destroy(&attr);
    } else {
        pthread_mutex_init(&mMutex, NULL);
    }
}
inline Mutex::~Mutex() {
    pthread_mutex_destroy(&mMutex);
}
inline status_t Mutex::lock() {
    return -pthread_mutex_lock(&mMutex);
}
inline void Mutex::unlock() {
    pthread_mutex_unlock(&mMutex);
}
inline status_t Mutex::tryLock() {
    return -pthread_mutex_trylock(&mMutex);
}

#endif // HAVE_PTHREADS

// ---------------------------------------------------------------------------

/*
 * Automatic mutex.  Declare one of these at the top of a function.
 * When the function returns, it will go out of scope, and release the
 * mutex.
 */
 
typedef Mutex::Autolock AutoMutex;

// ---------------------------------------------------------------------------
}; // namespace android
// ---------------------------------------------------------------------------

#endif // _LIBS_UTILS_MUTEX_H
