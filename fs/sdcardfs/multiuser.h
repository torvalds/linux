/*
 * fs/sdcardfs/multiuser.h
 *
 * Copyright (c) 2013 Samsung Electronics Co. Ltd
 *   Authors: Daeho Jeong, Woojoong Lee, Seunghwan Hyun,
 *               Sunghwan Yun, Sungjong Seo
 *
 * This program has been developed as a stackable file system based on
 * the WrapFS which written by
 *
 * Copyright (c) 1998-2011 Erez Zadok
 * Copyright (c) 2009     Shrikar Archak
 * Copyright (c) 2003-2011 Stony Brook University
 * Copyright (c) 2003-2011 The Research Foundation of SUNY
 *
 * This file is dual licensed.  It may be redistributed and/or modified
 * under the terms of the Apache 2.0 License OR version 2 of the GNU
 * General Public License.
 */

#define MULTIUSER_APP_PER_USER_RANGE 100000

typedef uid_t userid_t;
typedef uid_t appid_t;

static inline userid_t multiuser_get_user_id(uid_t uid) {
    return uid / MULTIUSER_APP_PER_USER_RANGE;
}

static inline appid_t multiuser_get_app_id(uid_t uid) {
    return uid % MULTIUSER_APP_PER_USER_RANGE;
}

static inline uid_t multiuser_get_uid(userid_t userId, appid_t appId) {
    return userId * MULTIUSER_APP_PER_USER_RANGE + (appId % MULTIUSER_APP_PER_USER_RANGE);
}

