/*
 * File: if_phonet.h
 *
 * Phonet interface kernel definitions
 *
 * Copyright (C) 2008 Nokia Corporation. All rights reserved.
 */

#define PHONET_HEADER_LEN	8	/* Phonet header length */

#define PHONET_MIN_MTU		6
/* 6 bytes header + 65535 bytes payload */
#define PHONET_MAX_MTU		65541
#define PHONET_DEV_MTU		PHONET_MAX_MTU
