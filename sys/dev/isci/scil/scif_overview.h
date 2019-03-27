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
#ifndef _SCIF_OVERVIEW_H_
#define _SCIF_OVERVIEW_H_

/**
@page framework_page SCI Framework

@section scif_introduction_section Introduction

The SCI Framework component provides SAS and SATA storage specific abstraction
to any OS driver devoid of this type of functionality. The functionality
provided by this component is of a higher nature and considered unnecessary
for some driver environments.  Basically, a user should be able to utilize
the same SCI Framework with different SCI Core implementations, with the
little to no changes necessary.

@warning In situations where the SCI framework is utilized, users should NOT
         invoke core methods on core objects for which there are associated
         framework objects and framework methods. Therefore, if a method is
         common to both the core and the framework object, do not invoke the
         core method if utilizing the framework.  Some exceptions to this
         exist and are called out.  It is important to mention that methods
         found only in the core are safe to invoke at times specified per
         that methods definition.

The following is a list of features found in an SCI Framework implementation:

-# SCI Core management
-# Port configuration scheme enforcement.  There are 2 port configuration
schemes:
  -# Automatic Port Configuration (APC).  In APC mode the framework will
     allow for any port configuration based on what is physically connected,
     assuming the underlying SCI Core also supports the configuration.
  -# Manual Port Configuration (MPC).  In MPC mode the framework expects the
     user to supply exactly which phys are to be allocated to each specific
     port.  If the discovered direct attached physical connections do not match
     the user supplied map, then an error is raised and the initialization
     process is halted.
-# Domain Discovery
-# Domain level resets (i.e. bus reset)
-# Task management processing
-# Controller Shutdown management (e.g. release STP affiliations, quiesce IOs)
-# Remote Device Configuration. Potential features:
  -# SSP: maybe mode selects to set timers or modify DIF settings.
  -# STP: IDENTIFY_DEVICE, SET FEATURES, etc.
  -# SMP: CONTROL type requests to set timers.
-# SAT Translation (Actually contained in SATI component)
-# SMP Zoning management

@image latex sci_framework.eps "SCI Framework Class Diagram" width=10cm

@note
For the SCU Driver Standard implementation of the SCI Framework interface the
following definitions should be used to augment the cardinalities described
in the previous diagram:
-# There are exactly 4 scif_domain objects in the scif_controller.  This
   number directly correlates to the number of scic_port objects in the core.
-# The maximum number of supported controllers in a library is a truly flexible
   value, but the likely maximum number is 4.

 */

#endif // _SCIF_OVERVIEW_H_

