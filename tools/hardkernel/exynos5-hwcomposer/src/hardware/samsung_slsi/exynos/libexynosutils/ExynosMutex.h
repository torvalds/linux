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
 * \file      ExynosMutex.h
 * \brief     header file for ExynosMutex
 * \author    Sangwoo, Park(sw5771.park@samsung.com)
 * \date      2011/06/15
 *
 * <b>Revision History: </b>
 * - 2010/06/15 : Sangwoo, Park(sw5771.park@samsung.com) \n
 *   Initial version
 *
 */

#ifndef __EXYNOS_MUTEX_H__
#define __EXYNOS_MUTEX_H__

#ifdef __cplusplus

//! ExynosMutex
/*!
 * \ingroup Exynos
 */
class ExynosMutex
{
public:
    enum TYPE {
        TYPE_BASE = 0,
        TYPE_PRIVATE,  //!< within this process
        TYPE_SHARED,   //!< within whole system
        TYPE_MAX,
    };

public:
    //! Constructor.
    ExynosMutex();

    //! Destructor
    virtual ~ExynosMutex();

    //! Create Mutex
    bool create(int type, char* name);

    //! Destroy Mutex
    void destroy(void);

    //! Get Mutex created status
    bool getCreatedStatus(void);

    //! Lock Mutex
    bool lock(void);

    //! Unlock Mutex
    bool unLock(void);

    //! trylock Mutex
    bool tryLock(void);

    //! Get Mutex type
    int getType(void);

private:
    void *m_mutex;
    bool  m_flagCreate;

    int   m_type;
    char  m_name[128];

public:
    //! Autolock
    /*!
     * \ingroup ExynosMutex
     */
    class Autolock {
    public:
        //! Lock on constructor
        inline Autolock(ExynosMutex& mutex) : mLock(mutex)  { mLock.lock(); }

        //! Lock on constructor
        inline Autolock(ExynosMutex* mutex) : mLock(*mutex) { mLock.lock(); }

        //! Unlock on destructor
        inline ~Autolock() { mLock.unLock(); }
    private:
        ExynosMutex& mLock;
    };
};

extern "C" {
#endif

enum EXYNOS_MUTEX_TYPE {
    EXYNOS_MUTEX_TYPE_BASE = 0,
    EXYNOS_MUTEX_TYPE_PRIVATE,  //!< within this process
    EXYNOS_MUTEX_TYPE_SHARED,   //!< within whole system
    EXYNOS_MUTEX_TYPE_MAX,
};

void *exynos_mutex_create(
    int   type,
    char *name);

bool exynos_mutex_destroy(
    void *handle);

bool exynos_mutex_lock(
    void *handle);

bool exynos_mutex_unlock(
    void *handle);

bool exynos_mutex_trylock(
    void *handle);

int exynos_mutex_type(
    void *handle);

bool exynos_mutex_get_created_status(
    void *handle);

#ifdef __cplusplus
}
#endif

#endif //__EXYNOS_MUTEX_H__
