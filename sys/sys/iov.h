/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013-2015 Sandvine Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _SYS_IOV_H_
#define _SYS_IOV_H_

#include <sys/ioccom.h>

#define	PF_CONFIG_NAME		"PF"
#define	VF_SCHEMA_NAME		"VF"

#define	VF_PREFIX		"VF-"
#define	VF_PREFIX_LEN		3
#define	VF_NUM_LEN		5	/* The maximum VF num is 65535. */
#define	VF_MAX_NAME		(VF_PREFIX_LEN + VF_NUM_LEN + 1)

#define	DRIVER_CONFIG_NAME	"DRIVER"
#define	IOV_CONFIG_NAME		"IOV"

#define	TYPE_SCHEMA_NAME	"TYPE"
#define	DEFAULT_SCHEMA_NAME	"DEFAULT"
#define	REQUIRED_SCHEMA_NAME	"REQUIRED"

/*
 * Because each PF device is expected to expose a unique set of possible
 * configurations, the SR-IOV infrastructure dynamically queries the PF
 * driver for its capabilities.  These capabilities are exposed to userland
 * with a configuration schema.  The schema is exported from the kernel as a
 * packed nvlist.  See nv(3) for the details of the nvlist API.  The expected
 * format of the nvlist is:
 *
 * BASIC RULES
 *   1) All keys are case-insensitive.
 *   2) No keys that are not specified below may exist at any level of the
 *      schema.
 *   3) All keys are mandatory unless explicitly documented as optional.  If a
 *      key is mandatory then the associated value is also mandatory.
 *   4) Order of keys is irrelevant.
 *
 * TOP LEVEL
 *   1) There must be a top-level key with the name PF_CONFIG_NAME.  The value
 *      associated with this key is a nvlist that follows the device schema
 *      node format.  The parameters in this node specify the configuration
 *      parameters that may be applied to a PF.
 *   2) There must be a top-level key with the name VF_SCHEMA_NAME.  The value
 *      associated with this key is a nvlist that follows the device schema
 *      node format.  The parameters in this node specify the configuration
 *      parameters that may be applied to a VF.
 *
 * DEVICE SCHEMA NODE
 *   1) There must be a key with the name DRIVER_CONFIG_NAME.  The value
 *      associated with this key is a nvlist that follows the device/subsystem
 *      schema node format.  The parameters in this node specify the
 *      configuration parameters that are specific to a particular device
 *      driver.
 *   2) There must be a key with the name IOV_CONFIG_NAME.  The value associated
 *      with this key is an nvlist that follows the device/subsystem schema node
 *      format.  The parameters in this node specify the configuration
 *      parameters that are applied by the SR-IOV infrastructure.
 *
 * DEVICE/SUBSYSTEM SCHEMA NODE
 *   1) All keys in the device/subsystem schema node are optional.
 *   2) Each key specifies the name of a valid configuration parameter that may
 *      be applied to the device/subsystem combination specified by this node.
 *      The value associated with the key specifies the format of valid
 *      configuration values, and must be a nvlist in parameter schema node
 *      format.
 *
 * PARAMETER SCHEMA NODE
 *   1) The parameter schema node must contain a key with the name
 *      TYPE_SCHEMA_NAME.  The value associated with this key must be a string.
 *      This string specifies the type of value that the parameter specified by
 *      this node must take.  The string must have one of the following values:
 *         - "bool"     - The configuration value must be a boolean.
 *         - "mac-addr" - The configuration value must be a binary value.  In
 *                         addition, the value must be exactly 6 bytes long and
 *                         the value must not be a multicast or broadcast mac.
 *         - "uint8_t"  - The configuration value must be a integer value in
 *                         the range [0, UINT8_MAX].
 *         - "uint16_t" - The configuration value must be a integer value in
 *                         the range [0, UINT16_MAX].
 *         - "uint32_t" - The configuration value must be a integer value in
 *                         the range [0, UINT32_MAX].
 *         - "uint64_t" - The configuration value must be a integer value in
 *                         the range [0, UINT64_MAX].
 *  2) The parameter schema may contain a key with the name
 *     REQUIRED_SCHEMA_NAME.  This key is optional.  If this key is present, the
 *     value associated with it must have a boolean type.  If the value is true,
 *     then the parameter specified by this schema is a required parameter.  All
 *     valid configurations must include all required parameters.
 *  3) The parameter schema may contain a key with the name DEFAULT_SCHEMA_NAME.
 *     This key is optional.  This key must not be present if the parameter
 *     specified by this schema is required.  If this key is present, the value
 *     associated with the parent key must follow all restrictions specified by
 *     the type specified by this schema.  If a configuration does not supply a
 *     value for the parameter specified by this schema, then the kernel will
 *     apply the value associated with this key in its place.
 *
 * The following is an example of a valid schema, as printed by nvlist_dump.
 * Keys are printed followed by the type of the value in parantheses.  The
 * value is displayed following a colon.  The indentation level reflects the
 * level of nesting of nvlists.  String values are displayed between []
 * brackets.  Binary values are shown with the length of the binary value (in
 * bytes) followed by the actual binary values.
 *
 *  PF (NVLIST):
 *      IOV (NVLIST):
 *          num_vfs (NVLIST):
 *              type (STRING): [uint16_t]
 *              required (BOOL): TRUE
 *          device (NVLIST):
 *              type (STRING): [string]
 *              required (BOOL): TRUE
 *      DRIVER (NVLIST):
 *  VF (NVLIST):
 *      IOV (NVLIST):
 *          passthrough (NVLIST):
 *              type (STRING): [bool]
 *              default (BOOL): FALSE
 *      DRIVER (NVLIST):
 *          mac-addr (NVLIST):
 *              type (STRING): [mac-addr]
 *              default (BINARY): 6 000000000000
 *          vlan (NVLIST):
 *               type (STRING): [uint16_t]
 *          spoof-check (NVLIST):
 *              type (STRING): [bool]
 *              default (BOOL): TRUE
 *          allow-set-mac (NVLIST):
 *              type (STRING): [bool]
 *              default (BOOL): FALSE
 */
struct pci_iov_schema
{
	void *schema;
	size_t len;
	int error;
};

/*
 * SR-IOV configuration is passed to the kernel as a packed nvlist.  See nv(3)
 * for the details of the nvlist API.  The expected format of the nvlist is:
 *
 * BASIC RULES
 *   1) All keys are case-insensitive.
 *   2) No keys that are not specified below may exist at any level of the
 *      config nvlist.
 *   3) Unless otherwise specified, all keys are optional.  It should go without
 *      saying a key being mandatory is transitive: that is, if a key is
 *      specified to contain a sub-nodes that contains a mandatory key, then
 *      the outer key is implicitly mandatory.  If a key is mandatory then the
 *      associated value is also mandatory.
 *   4) Order of keys is irrelevant.
 *
 * TOP LEVEL OF CONFIG NVLIST
 * 1) All keys specified in this section are mandatory.
 * 2) There must be a top-level key with the name PF_CONFIG_NAME.  The value
 *    associated is an nvlist that follows the "device node" format.  The
 *    parameters in this node specify parameters that apply to the PF.
 * 3) For every VF being configured (this is set via the "num_vfs" parameter
 *    in the PF section), there must be a top-level key whose name is VF_PREFIX
 *    immediately followed by the index of the VF as a decimal integer.  For
 *    example, this would be VF-0 for the first VF.  VFs are numbered starting
 *    from 0.  The value associated with this key follows the "device node"
 *    format.  The parameters in this node specify configuration that applies
 *    to the VF specified in the key.  Leading zeros are not permitted in VF
 *    index.  Configuration for the second VF must be specified in a node with
 *    the key VF-1.  VF-01 is not a valid key.
 *
 * DEVICE NODES
 * 1) All keys specified in this section are mandatory.
 * 2) The device node must contain a key with the name DRIVER_CONFIG_NAME.  The
 *    value associated with this key is an nvlist following the subsystem node
 *    format.  The parameters in this key specify configuration that is specific
 *    to a particular device driver.
 * 3) The device node must contain a key with the name IOV_CONFIG_NAME.  The
 *    value associated with this key is an nvlist following the subsystem node
 *    format.  The parameters in this key specify configuration that is consumed
 *    by the SR-IOV infrastructure.
 *
 * SUBSYSTEM NODES
 * 1) A subsystem node specifies configuration parameters that apply to a
 *    particular subsystem (driver or infrastructure) of a particular device
 *    (PF or individual VF).
 *         Note: We will refer to the section of the configuration schema that
 *               specifies the parameters for this subsystem and device
 *               configuration as the device/subystem schema.
 * 2) The subsystem node must contain only keys that correspond to parameters
 *    that are specified in the device/subsystem schema.
 * 3) Every parameter specified as required in the device/subsystem schema is
 *    a mandatory key in the subsystem node.
 *    Note:  All parameters that are not required in device/subsystem schema are
 *           optional keys.  In particular, any parameter specified to have a
 *           default value in the device/subsystem schema is optional.  The
 *           kernel is responsible for applying default values.
 * 4) The value of every parameter in the device node must conform to the
 *    restrictions of the type specified for that parameter in the device/
 *    subsystem schema.
 *
 * The following is an example of a valid configuration, when validated against
 * the schema example given above.
 *
 * PF (NVLIST):
 *     driver (NVLIST):
 *     iov (NVLIST):
 *         num_vfs (NUMBER): 3 (3) (0x3)
 *         device (STRING): [ix0]
 * VF-0 (NVLIST):
 *     driver (NVLIST):
 *         vlan (NUMBER): 1000 (1000) (0x3e8)
 *     iov (NVLIST):
 *         passthrough (BOOL): TRUE
 * VF-1 (NVLIST):
 *     driver (NVLIST):
 *     iov (NVLIST):
 * VF-2 (NVLIST):
 *     driver (NVLIST):
 *         mac-addr (BINARY): 6 020102030405
 *     iov (NVLIST):
 */
struct pci_iov_arg
{
	void *config;
	size_t len;
};

#define	IOV_CONFIG	_IOW('p', 10, struct pci_iov_arg)
#define	IOV_DELETE	_IO('p', 11)
#define	IOV_GET_SCHEMA	_IOWR('p', 12, struct pci_iov_schema)

#endif

