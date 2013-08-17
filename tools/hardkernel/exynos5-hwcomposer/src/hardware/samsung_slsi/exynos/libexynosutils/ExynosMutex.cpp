/*
 * Copyright@ Samsung Electronics Co. LTD
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

/*!
 * \file      ExynosMutex.cpp
 * \brief     source file for ExynosMutex
 * \author    Sangwoo, Park(sw5771.park@samsung.com)
 * \date      2011/06/15
 *
 * <b>Revision History: </b>
 * - 2010/06/15 : Sangwoo, Park(sw5771.park@samsung.com) \n
 *   Initial version
 *
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "ExynosMutex"
#if defined(ANDROID)
#include <utils/Log.h>

#include <utils/threads.h>
#else
#include <pthread.h>
#include <log.h>
#include <utils/Mutex.h>
#endif
using namespace android;

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "ExynosMutex.h"

//#define EXYNOS_MUTEX_DEBUG

ExynosMutex::ExynosMutex()
{
    m_mutex = NULL;
    m_flagCreate = false;
    m_type = TYPE_BASE;
    memset(m_name, 0, 128);
}

ExynosMutex::~ExynosMutex()
{
    if (m_flagCreate == true)
        this->destroy();
}

bool ExynosMutex::create(int type, char* name)
{
    if (m_flagCreate == true) {
        ALOGE("%s::Already created", __func__);
        return false;
    }

    int androidMutexType = 0;

    m_type = TYPE_BASE;

    switch (type) {
    case TYPE_PRIVATE:
        androidMutexType = Mutex::PRIVATE;
        break;
    case TYPE_SHARED:
        androidMutexType = Mutex::SHARED;
        break;
    default:
        ALOGE("%s::unmatched type(%d) fail", __func__, type);
        return false;
    }

    m_mutex = new Mutex(androidMutexType, name);
    if (m_mutex == NULL) {
        ALOGE("%s::Mutex create fail", __func__);
        return false;
    }

    m_type = type;
    strncpy(m_name, name, 128 - 1);

    m_flagCreate = true;

    return true;
}

void ExynosMutex::destroy(void)
{
    if (m_flagCreate == false) {
        ALOGE("%s::Not yet created", __func__);
        return;
    }

    if (m_mutex)
        delete ((Mutex *)m_mutex);
    m_mutex = NULL;

    m_flagCreate = false;
}

bool ExynosMutex::getCreatedStatus(void)
{
    return m_flagCreate;
}

bool ExynosMutex::lock(void)
{
    if (m_flagCreate == false) {
        ALOGE("%s::Not yet created", __func__);
        return false;
    }

#ifdef EXYNOS_MUTEX_DEBUG
    ALOGD("%s::%s'lock() start", __func__, m_name);
#endif

    if (((Mutex *)m_mutex)->lock() != 0) {
        ALOGE("%s::m_core->lock() fail", __func__);
        return false;
    }

#ifdef EXYNOS_MUTEX_DEBUG
    ALOGD("%s::%s'lock() end", __func__, m_name);
#endif

    return true;
}

bool ExynosMutex::unLock(void)
{
    if (m_flagCreate == false) {
        ALOGE("%s::Not yet created", __func__);
        return false;
    }

#ifdef EXYNOS_MUTEX_DEBUG
    ALOGD("%s::%s'unlock() start", __func__, m_name);
#endif

    ((Mutex *)m_mutex)->unlock();

#ifdef EXYNOS_MUTEX_DEBUG
    ALOGD("%s::%s'unlock() end", __func__, m_name);
#endif

    return true;
}

bool ExynosMutex::tryLock(void)
{
    if (m_flagCreate == false) {
        ALOGE("%s::Not yet created", __func__);
        return false;
    }

    int ret = 0;

#ifdef EXYNOS_MUTEX_DEBUG
    ALOGD("%s::%s'trylock() start", __func__, m_name);
#endif

    ret = ((Mutex *)m_mutex)->tryLock();

#ifdef EXYNOS_MUTEX_DEBUG
    ALOGD("%s::%s'trylock() end", __func__, m_name);
#endif

    return (ret == 0) ? true : false;
}

int ExynosMutex::getType(void)
{
    return m_type;
}

void *exynos_mutex_create(
    int type,
    char *name)
{
    ExynosMutex *mutex = new ExynosMutex();

    if (mutex->create(type, name) == false) {
        ALOGE("%s::mutex->create() fail", __func__);
        delete mutex;
        mutex = NULL;
    }

    return (void*)mutex;
}

bool exynos_mutex_destroy(
    void *handle)
{
    if (handle == NULL) {
        ALOGE("%s::handle is null", __func__);
        return false;
    }

    if (((ExynosMutex *)handle)->getCreatedStatus() == true)
        ((ExynosMutex *)handle)->destroy();

    delete (ExynosMutex *)handle;

    return true;
}

bool exynos_mutex_lock(
    void *handle)
{
    if (handle == NULL) {
        ALOGE("%s::handle is null", __func__);
        return false;
    }

    return ((ExynosMutex *)handle)->lock();

}

bool exynos_mutex_unlock(
    void *handle)
{
    if (handle == NULL) {
        ALOGE("%s::handle is null", __func__);
        return false;
    }

    return ((ExynosMutex *)handle)->unLock();

}

bool exynos_mutex_trylock(
    void *handle)
{
    if (handle == NULL) {
        ALOGE("%s::handle is null", __func__);
        return false;
    }

    return ((ExynosMutex *)handle)->tryLock();

}

int exynos_mutex_get_type(
    void *handle)
{
    if (handle == NULL) {
        ALOGE("%s::handle is null", __func__);
        return false;
    }

    return ((ExynosMutex *)handle)->getType();
}

bool exynos_mutex_get_created_status(
    void *handle)
{
    if (handle == NULL) {
        ALOGE("%s::handle is null", __func__);
        return false;
    }

    return ((ExynosMutex *)handle)->getCreatedStatus();
}

