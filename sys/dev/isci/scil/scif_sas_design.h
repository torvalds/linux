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
#ifndef _SCIF_SAS_DESIGN_H_
#define _SCIF_SAS_DESIGN_H_

/**
@page scif_sas_design_page SCIF SAS High Level Design

<b>Authors:</b>
- Nathan Marushak

<b>Key Contributors:</b>
- Richard Boyd

@section scif_sas_scope_and_audience Scope and Audience

This document provides design information relating to the SAS specific
implementation of the SCI Framework.  Driver developers are the primary
audience for this document.  The reader is expected to have an understanding
of the SCU Software Architecture Specification, the Storage Controller
Interface Specification, and the SCI Base Design.

@section scif_sas_overview Overview

To begin, it's important to discuss the utilization of state machines in
the design.  State machines are pervasive in this design, because of the
abilities they provide.  A properly implemented state machine allows the
developer to code for a specific task.  The developer is not encumbered
with needed to handle other situations all in a single function.  For
example, if a specific event can only occur when the object is in a specific
state, then the event handler is added to handle such an event.  Thus, a
single function is not spliced to handle multiple events under various
potentially disparate conditions.

Additionally, the SCI Base Design document specifies a number of state
machines, objects, and methods that are heavily utilized by this design.
Please refer to Base Design specification for further information.

Many of the framework objects have state machines associated with them.
As a result, there are a number of state entrance and exit methods as well
as event handlers for each individual state.  This design places all of
the state entrance and exit methods for a given state machine into a single
file (e.g. scif_sas_controller_states.c).  Furthermore, all of the state
event handler methods are also placed into a single file (e.g.
scif_sas_controller_state_handlers.c).  This format is reused for each
object that contains state machine(s).

Some of the SAS framework objects contain sub-state machines.  These
sub-state machines are started upon entrance to the super-state and stopped
upon exit of the super-state.

All other method, data, constant description information will be found in
the remaining source file (e.g. scif_sas_controller.c).  As a result, please
be sure to follow the link to that specific object/file definition for
further information.

@note Currently a large number of function pointers are utilized during the
course of a normal IO request.  Once stability of the driver is achieved,
performance improvements will be made as needed.  This likely will include
removal of the function pointers from the IO path.

@section scif_sas_use_cases Use Cases

The following use case diagram depicts the high-level user interactions with
the SAS framework.  This diagram does not encompass all use cases implemented
in the system.  The low-level design section will contain detailed use cases
for each significant object and their associated detailed sequences and/or
activities.  For the purposes of readability, the use cases are not directly
connected to the associated actor utilizing the use case.  Instead naming
is utilized to different which actor is involved with the use case.

Actors:
- The Framework user also called the OS Specific Driver initiates activities in
the Framework.
- The SCI Core calls back into the Framework as a result of an operation either
started by the OS Specific Driver or by the Framework itself.

@image latex Use_Case_Diagram__SCIF_SAS__Use_Cases.eps "SCIF SAS OS Use Cases" width=11cm
@image html Use_Case_Diagram__SCIF_SAS__Use_Cases.jpg "SCIF SAS OS Use Cases"

@section scif_sas_class_hierarchy Class Hierarchy

This section delineates the high-level class organization for the SCIF_SAS
component.  Details concerning each class will be found in the corresponding
low-level design sections.  Furthermore, additional classes not germane to
the overall architecture of the component will also be defined in these
low-level design sections.

@image latex Class_Diagram__scif_sas__Class_Diagram.eps "SCIF SAS Class Diagram" width=16cm
@image html Class_Diagram__scif_sas__Class_Diagram.jpg "SCIF SAS Class Diagram"

For more information on each object appearing in the diagram, please
reference the subsequent sections.

@section scif_sas_library SCIF SAS Library

First, the SCIF_SAS_LIBRARY object provides an implementation
for the roles and responsibilities defined in the Storage Controller
Interface (SCI) specification.  It is suggested that the user read the
storage controller interface specification for background information on
the library object.

The SCIF_SAS_LIBRARY object is broken down into 2 individual source files
and one direct header file.  These files delineate the methods, members, etc.
associated with this object.  Please reference these files directly for
further design information:
- scif_sas_library.h
- scif_sas_library.c

@section scif_sas_controller SCIF SAS Controller

First, the SCIF_SAS_CONTROLLER object provides an implementation
for the roles and responsibilities defined in the Storage Controller
Interface (SCI) specification.  It is suggested that the user read the
storage controller interface specification for background information on
the controller object.

The SCIF_SAS_CONTROLLER object is broken down into 3 individual source files
and one direct header file.  These files delineate the methods, members, etc.
associated with this object.  Please reference these files directly for
further design information:
- scif_sas_controller.h
- scif_sas_controller.c
- scif_sas_controller_state_handlers.c
- scif_sas_controller_states.c

@section scif_sas_domain SCIF SAS Domain

First, the SCIF_SAS_DOMAIN object provides an implementation
for the roles and responsibilities defined in the Storage Controller
Interface (SCI) specification.  It is suggested that the user read the
storage controller interface specification for background information on
the SCIF_SAS_DOMAIN object.

The SCIF_SAS_DOMAIN object is broken down into 3 individual
source files and one direct header file.  These files delineate the
methods, members, etc. associated with this object.  Please reference
these files directly for
further design information:
- scif_sas_domain.h
- scif_sas_domain.c
- scif_sas_domain_state_handlers.c
- scif_sas_domain_states.c

@section scif_sas_remote_device SCIF SAS Remote Device

First, the SCIF_SAS_REMOTE_DEVICE object provides an implementation
for the roles and responsibilities defined in the Storage Controller
Interface (SCI) specification.  It is suggested that the user read the
storage controller interface specification for background information on
the SCIF_SAS_REMOTE_DEVICE object.

The SCIF_SAS_REMOTE_DEVICE object is broken down into 7 individual source files
and one direct header file.  These files delineate the methods, members, etc.
associated with this object.  Methods, data, and functionality specific to a
particular protocol type (e.g. SMP, STP, etc.) are broken out into their own
object/file.  SSP specific remote device functionality is covered by the base
classes (common files).  Please reference these files directly for further
design information:
- scif_sas_remote_device.h
- scif_sas_smp_remote_device.h
- scif_sas_stp_remote_device.h
- scif_sas_remote_device.c
- scif_sas_remote_device_state_handlers.c
- scif_sas_remote_device_states.c
- scif_sas_remote_device_starting_substate_handlers.c
- scif_sas_remote_device_starting_substates.c
- scif_sas_remote_device_ready_substate_handlers.c
- scif_sas_remote_device_ready_substates.c
- scif_sas_smp_remote_device.c
- scif_sas_stp_remote_device.c

The SCIF_SAS_REMOTE_DEVICE object has sub-state machines defined for
the READY and STARTING super-states.  For more information on the
super-state machine please refer to SCI_BASE_REMOTE_DEVICE_STATES
in the SCI Base design document.

In the SCIF_SAS_REMOTE_DEVICE_STARTING_SUBSTATES sub-state machine,
the remote device currently has to wait for the core to
return an indication that the remote device has successfully started
and become ready.  If all goes well, then the remote device will
transition into the READY state.

For more information on the starting sub-state machine states please refer
to the scif_sas_remote_device.h::_SCIF_SAS_REMOTE_DEVICE_STARTING_SUBSTATES
enumeration.

@image latex State_Machine_Diagram__STARTING_SUB-STATE__STARTING_SUB-STATE.eps "SCIF SAS Remote Device Starting Sub-state Machine Diagram" width=16cm
@image html State_Machine_Diagram__STARTING_SUB-STATE__STARTING_SUB-STATE.jpg "SCIF SAS Remote Device Starting Sub-state Machine Diagram"

In the SCIF_SAS_REMOTE_DEVICE_READY_SUBSTATES sub-state machine,
the remote device currently only allows new host IO requests during the
OPERATIONAL state.  In the TASK MANAGEMENT state only new task management
requests are allowed.

For more information on the ready sub-state machine states please refer
to the scif_sas_remote_device.h::_SCIF_SAS_REMOTE_DEVICE_READY_SUBSTATES
enumeration.

@image latex State_Machine_Diagram__READY_SUB-STATE__READY_SUB-STATE.eps "SCIF SAS Remote Device Ready Sub-state Machine Diagram" width=16cm
@image html State_Machine_Diagram__READY_SUB-STATE__READY_SUB-STATE.jpg "SCIF SAS Remote Device Ready Sub-state Machine Diagram"

@section scif_sas_request SCIF SAS Request

The SCIF_SAS_REQUEST object provide common functionality for the
SCIF_SAS_IO_REQUEST and the SCIF_SAS_TASK_REQUEST objects.  This object
does not directly map to an SCI defined object, but its children do.  For
additional information, you may reference the SCIF_SAS_IO_REQUEST or
SCIF_SAS_TASK_REQUEST objects.

The SCIF_SAS_REQUEST object is broken down into 1 individual source file
and one direct header file.  These files delineate the methods, members, etc.
associated with this object.  Please reference these files directly for
further design information:
- scif_sas_request.h
- scif_sas_request.c

@section scif_sas_io_request SCIF SAS IO Request

First, the SCIF_SAS_IO_REQUEST object provides an implementation
for the roles and responsibilities defined in the Storage Controller
Interface (SCI) specification.  It is suggested that the user read the
storage controller interface specification for background information on
the SCIF_SAS_IO_REQUEST object.

The SCIF_SAS_IO_REQUEST object is broken down into 3 individual
source files and one direct header file.  These files delineate the
methods, members, etc. associated with this object.  Please reference
these files directly for further design information:
- scif_sas_io_request.h
- scif_sas_smp_io_request.h
- scif_sas_stp_io_request.h
- scif_sas_sati_binding.h
- scif_sas_io_request.c
- scif_sas_io_request_state_handlers.c
- scif_sas_io_request_states.c
- scif_sas_smp_io_request.c
- scif_sas_stp_io_request.c

@section scif_sas_task_request SCIF SAS Task Request

First, the SCIF_SAS_TASK_REQUEST object provides an implementation
for the roles and responsibilities defined in the Storage Controller
Interface (SCI) specification.  It is suggested that the user read the
storage controller interface specification for background information on
the SCIF_SAS_TASK_REQUEST object.

The SCIF_SAS_TASK_REQUEST object is broken down into 3 individual
source files and one direct header file.  These files delineate the
methods, members, etc. associated with this object.  Please reference
these files directly for further design information:
- scif_sas_task_request.h
- scif_sas_stp_task_request.h
- scif_sas_task_request.c
- scif_sas_task_request_state_handlers.c
- scif_sas_task_request_states.c
- scif_sas_stp_task_request.c

@section scif_sas_internal_io_request SCIF SAS INTERNAL IO Request

The SCIF_SAS_INTERNAL_IO_REQUEST object fulfills the SCI's need to create
and send out the internal io request. These internal io requests could be
smp request for expander device discover process, or stp request for NCQ
error handling. Internal IOs consume the reserved internal io space in
scif_sas_controller. When an internal IO is constructed, it is put into an
internal high priority queue. A defferred task (start_internal_io_task) will be
scheduled at the end of every completion process. The task looks up the high
priority queue and starts each internal io in the queue. There is one exception
that start_internal_io_task is scheduled immediately when the first internal io
is constructed. A retry mechanism is also provided for internal io. When an
internal io response is decoded, if the decoding indicates a retry is needed,
the internal io will be retried.

Please refer to these files directly for further design information:
- scif_sas_internal_io_request.h
- scif_sas_internal_io_request.c
- scif_sas_controller.h

@section scif_sas_smp_remote_device SCIF SAS SMP REMOTE DEVICE

The SCIF SAS SMP REMOTE DEVICE object represents the expander device and fulfills
its SMP discover activities. The discover procedure includes a initial discover
phase and a following SATA spinup_hold release phase, if there are expander attached
SATA device is discovered and in spinup_hold conditon. The SCIF SAS SMP REMOTE DEVICE
object also fulfills expander attached device Target Reset (Phy Control) activity.

@image latex Discover Process.eps "SMP Discover Activity Diagram" width=10cm
@image html Discover Process.jpg "SMP Discover Activity Diagram"

Please refer to these files directly for further design information:
- scif_sas_smp_remote_device.h
- scif_sas_smp_remote_device.c
- scif_sas_smp_request.h
- scif_sas_smp_request.c
*/

#endif // _SCIF_SAS_DESIGN_H_
