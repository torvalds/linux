/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015-2016 Landon Fuller <landon@landonf.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 * 
 * $FreeBSD$
 */

#ifndef _BHND_BHND_MATCH_H_
#define _BHND_BHND_MATCH_H_

#include "bhnd_types.h"

/**
 * A hardware revision match descriptor.
 */
struct bhnd_hwrev_match {
	uint16_t	start;	/**< first revision, or BHND_HWREV_INVALID
					     to match on any revision. */
	uint16_t	end;	/**< last revision, or BHND_HWREV_INVALID
					     to match on any revision. */
};

/* Copy match field @p _name from @p _src */
#define	_BHND_COPY_MATCH_FIELD(_src, _name)	\
	.m.match._name = (_src)->m.match._name,	\
	._name = (_src)->_name
	
/* Set match field @p _name with @p _value */
#define	_BHND_SET_MATCH_FIELD(_name, _value)	\
	.m.match._name = 1, ._name = _value

/** 
 * Wildcard hardware revision match descriptor.
 */
#define	BHND_HWREV_ANY		{ BHND_HWREV_INVALID, BHND_HWREV_INVALID }
#define	BHND_HWREV_IS_ANY(_m)	\
	((_m)->start == BHND_HWREV_INVALID && (_m)->end == BHND_HWREV_INVALID)

/**
 * Hardware revision match descriptor for an inclusive range.
 * 
 * @param _start The first applicable hardware revision.
 * @param _end The last applicable hardware revision, or BHND_HWREV_INVALID
 * to match on any revision.
 */
#define	BHND_HWREV_RANGE(_start, _end)	{ _start, _end }

/**
 * Hardware revision match descriptor for a single revision.
 * 
 * @param _hwrev The hardware revision to match on.
 */
#define	BHND_HWREV_EQ(_hwrev)	BHND_HWREV_RANGE(_hwrev, _hwrev)

/**
 * Hardware revision match descriptor for any revision equal to or greater
 * than @p _start.
 * 
 * @param _start The first hardware revision to match on.
 */
#define	BHND_HWREV_GTE(_start)	BHND_HWREV_RANGE(_start, BHND_HWREV_INVALID)

/**
 * Hardware revision match descriptor for any revision equal to or less
 * than @p _end.
 * 
 * @param _end The last hardware revision to match on.
 */
#define	BHND_HWREV_LTE(_end)	BHND_HWREV_RANGE(0, _end)

/**
 * A bhnd(4) core match descriptor.
 */
struct bhnd_core_match {
	/** Select fields to be matched */
	union {
		uint8_t match_flags;
		struct {
			uint8_t
			    core_vendor:1,
			    core_id:1,
			    core_rev:1,
			    core_class:1,
			    core_idx:1,
			    core_unit:1,
			    flags_unused:2;
		} match;
	} m;
	
	uint16_t		core_vendor;	/**< required JEP106 device vendor */
	uint16_t		core_id;	/**< required core ID */
	struct bhnd_hwrev_match	core_rev;	/**< matching core revisions. */
	bhnd_devclass_t		core_class;	/**< required bhnd class */
	u_int			core_idx;	/**< required core index */
	int			core_unit;	/**< required core unit */
};

#define	_BHND_CORE_MATCH_COPY(_src)			\
	_BHND_COPY_MATCH_FIELD(_src, core_vendor),	\
	_BHND_COPY_MATCH_FIELD(_src, core_id),		\
	_BHND_COPY_MATCH_FIELD(_src, core_rev),		\
	_BHND_COPY_MATCH_FIELD(_src, core_class),	\
	_BHND_COPY_MATCH_FIELD(_src, core_idx),		\
	_BHND_COPY_MATCH_FIELD(_src, core_unit)		\

#define	BHND_MATCH_CORE_VENDOR(_v)	_BHND_SET_MATCH_FIELD(core_vendor, _v)
#define	BHND_MATCH_CORE_ID(_id)		_BHND_SET_MATCH_FIELD(core_id, _id)
#define	BHND_MATCH_CORE_REV(_rev)	_BHND_SET_MATCH_FIELD(core_rev,	\
					    BHND_ ## _rev)
#define	BHND_MATCH_CORE_CLASS(_cls)	_BHND_SET_MATCH_FIELD(core_class, _cls)
#define	BHND_MATCH_CORE_IDX(_idx)	_BHND_SET_MATCH_FIELD(core_idx, _idx)
#define	BHND_MATCH_CORE_UNIT(_unit)	_BHND_SET_MATCH_FIELD(core_unit, _unit)

/**
 * Match against the given @p _vendor and @p _id,
 */
#define	BHND_MATCH_CORE(_vendor, _id)		\
	BHND_MATCH_CORE_VENDOR(_vendor),	\
	BHND_MATCH_CORE_ID(_id)

/**
 * A bhnd(4) chip match descriptor.
 */
struct bhnd_chip_match {
	/** Select fields to be matched */
	union {
		uint8_t match_flags;
		struct {
			uint8_t
			    chip_id:1,
			    chip_rev:1,
			    chip_pkg:1,
			    chip_type:1,
			    flags_unused:4;
		} match;

	} m;

	uint16_t		chip_id;	/**< required chip id */
	struct bhnd_hwrev_match	chip_rev;	/**< matching chip revisions */
	uint8_t			chip_pkg;	/**< required package */
	uint8_t			chip_type;	/**< required chip type (BHND_CHIPTYPE_*) */
};

#define	_BHND_CHIP_MATCH_COPY(_src)		\
	_BHND_COPY_MATCH_FIELD(_src, chip_id),	\
	_BHND_COPY_MATCH_FIELD(_src, chip_rev),	\
	_BHND_COPY_MATCH_FIELD(_src, chip_pkg),	\
	_BHND_COPY_MATCH_FIELD(_src, chip_type),\

/** Set the required chip ID within a bhnd match descriptor */
#define	BHND_MATCH_CHIP_ID(_cid)	_BHND_SET_MATCH_FIELD(chip_id,	\
					    BHND_CHIPID_ ## _cid)

/** Set the required chip revision range within a bhnd match descriptor */
#define	BHND_MATCH_CHIP_REV(_rev)	_BHND_SET_MATCH_FIELD(chip_rev,	\
					    BHND_ ## _rev)

/** Set the required package ID within a bhnd match descriptor */
#define	BHND_MATCH_CHIP_PKG(_pkg)	_BHND_SET_MATCH_FIELD(chip_pkg,	\
					    BHND_PKGID_ ## _pkg)

/** Set the required chip type within a bhnd match descriptor */
#define	BHND_MATCH_CHIP_TYPE(_type)	_BHND_SET_MATCH_FIELD(chip_type,	\
					    BHND_CHIPTYPE_ ## _type)

/** Set the required chip and package ID within a bhnd match descriptor */
#define	BHND_MATCH_CHIP_IP(_cid, _pkg)	\
    BHND_MATCH_CHIP_ID(_cid), BHND_MATCH_CHIP_PKG(_pkg)

/** Set the required chip ID, package ID, and revision within a bhnd_device_match
 *  instance */
#define	BHND_MATCH_CHIP_IPR(_cid, _pkg, _rev)	\
    BHND_MATCH_CHIP_ID(_cid),			\
    BHND_MATCH_CHIP_PKG(_pkg),			\
    BHND_MATCH_CHIP_REV(_rev)

/** Set the required chip ID and revision within a bhnd_device_match
 *  instance */
#define	BHND_MATCH_CHIP_IR(_cid, _rev)	\
    BHND_MATCH_CHIP_ID(_cid), BHND_MATCH_CHIP_REV(_rev)

/**
 * A bhnd(4) board match descriptor.
 */
struct bhnd_board_match {
	/** Select fields to be matched */
	union {
		uint8_t match_flags;
		struct {
			uint8_t
			    board_vendor:1,
			    board_type:1,
			    board_devid:1,
			    board_rev:1,
			    board_srom_rev:1,
			    flags_unused:3;
		} match;
	} m;

	uint16_t		board_vendor;	/**< required board vendor */
	uint16_t		board_type;	/**< required board type */
	uint16_t		board_devid;	/**< required board devid */
	struct bhnd_hwrev_match	board_rev;	/**< matching board revisions */
	struct bhnd_hwrev_match	board_srom_rev;	/**< matching board srom revisions */
};

#define	_BHND_BOARD_MATCH_COPY(_src)			\
	_BHND_COPY_MATCH_FIELD(_src, board_vendor),	\
	_BHND_COPY_MATCH_FIELD(_src, board_type),	\
	_BHND_COPY_MATCH_FIELD(_src, board_devid),	\
	_BHND_COPY_MATCH_FIELD(_src, board_rev),	\
	_BHND_COPY_MATCH_FIELD(_src, board_srom_rev)

/** Set the required board vendor within a bhnd match descriptor */
#define	BHND_MATCH_BOARD_VENDOR(_v)	_BHND_SET_MATCH_FIELD(board_vendor, _v)

/** Set the required board type within a bhnd match descriptor */
#define	BHND_MATCH_BOARD_TYPE(_type)	_BHND_SET_MATCH_FIELD(board_type, \
					    BHND_BOARD_ ## _type)

/** Set the required board devid within a bhnd match descriptor */
#define	BHND_MATCH_BOARD_DEVID(_devid)	_BHND_SET_MATCH_FIELD(board_devid, \
					    (_devid))

/** Set the required SROM revision range within a bhnd match descriptor */
#define	BHND_MATCH_SROMREV(_rev)	_BHND_SET_MATCH_FIELD(board_srom_rev, \
					    BHND_HWREV_ ## _rev)

/** Set the required board revision range within a bhnd match descriptor */
#define	BHND_MATCH_BOARD_REV(_rev)	_BHND_SET_MATCH_FIELD(board_rev, \
					    BHND_ ## _rev)

/** Set the required board vendor and type within a bhnd match descriptor */
#define	BHND_MATCH_BOARD(_vend, _type)	\
	BHND_MATCH_BOARD_VENDOR(_vend), BHND_MATCH_BOARD_TYPE(_type)


/**
 * A bhnd(4) device match descriptor.
 *
 * @warning Matching on board attributes relies on NVRAM access, and will
 * fail if a valid NVRAM device cannot be found, or is not yet attached.
 */
struct bhnd_device_match {
	/** Select fields to be matched */
	union {
		uint32_t match_flags;
		struct {
			uint32_t
			core_vendor:1,
			core_id:1,
			core_rev:1,
			core_class:1,
			core_idx:1,
			core_unit:1,
			chip_id:1,
			chip_rev:1,
			chip_pkg:1,
			chip_type:1,
			board_vendor:1,
			board_type:1,
			board_devid:1,
			board_rev:1,
			board_srom_rev:1,
			flags_unused:15;
		} match;
	} m;
	
	uint16_t		core_vendor;	/**< required JEP106 device vendor */
	uint16_t		core_id;	/**< required core ID */
	struct bhnd_hwrev_match	core_rev;	/**< matching core revisions. */
	bhnd_devclass_t		core_class;	/**< required bhnd class */
	u_int			core_idx;	/**< required core index */
	int			core_unit;	/**< required core unit */

	uint16_t		chip_id;	/**< required chip id */
	struct bhnd_hwrev_match	chip_rev;	/**< matching chip revisions */
	uint8_t			chip_pkg;	/**< required package */
	uint8_t			chip_type;	/**< required chip type (BHND_CHIPTYPE_*) */

	uint16_t		board_vendor;	/**< required board vendor */
	uint16_t		board_type;	/**< required board type */
	uint16_t		board_devid;	/**< required board devid */
	struct bhnd_hwrev_match	board_rev;	/**< matching board revisions */
	struct bhnd_hwrev_match	board_srom_rev;	/**< matching board srom revisions */
};

/** Define a wildcard match requirement (matches on any device). */
#define	BHND_MATCH_ANY		.m.match_flags = 0
#define	BHND_MATCH_IS_ANY(_m)	\
	((_m)->m.match_flags == 0)

#endif /* _BHND_BHND_MATCH_H_ */
