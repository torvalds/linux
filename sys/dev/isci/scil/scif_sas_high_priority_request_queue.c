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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/**
 * @file
 *
 * @brief This file contains all of method implementations specific to
 *        the high priority request queue (HPRQ).  See the
 *        scif_sas_high_priority_request_queue.h file for additional
 *        information.
 */

#include <dev/isci/scil/scif_sas_high_priority_request_queue.h>

#include <dev/isci/scil/scif_sas_logger.h>
#include <dev/isci/scil/scif_sas_io_request.h>
#include <dev/isci/scil/scif_sas_controller.h>

/**
 * @brief This method initializes the fields of the HPRQ.  It should be
 *        called during the construction of the object containing or
 *        utilizing it (i.e. SCIF_SAS_CONTROLLER).
 *
 * @param[in] fw_hprq This parameter specific the high priority request
 *            queue object being constructed.
 *
 * @return none
 */
void scif_sas_high_priority_request_queue_construct(
   SCIF_SAS_HIGH_PRIORITY_REQUEST_QUEUE_T * fw_hprq,
   SCI_BASE_LOGGER_T                      * logger
)
{
   SCIF_LOG_TRACE((
      logger,
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_high_priority_request_queue_construct(0x%x,0x%x) enter\n",
      fw_hprq, logger
   ));

   sci_base_object_construct((SCI_BASE_OBJECT_T*) &fw_hprq->lock, logger);
   fw_hprq->lock.level = SCI_LOCK_LEVEL_NONE;

   sci_pool_initialize(fw_hprq->pool);
}

/**
 * @brief This method will ensure all internal requests destined for
 *        devices contained in the supplied domain are properly removed
 *        from the high priority request queue.
 *
 * @param[in] fw_hprq This parameter specifies the high priority request
 *            queue object for which to attempt to remove elements.
 * @param[in] fw_domain This parameter specifies the domain for which to
 *            remove all high priority requests.
 *
 * @return none
 */
void scif_sas_high_priority_request_queue_purge_domain(
   SCIF_SAS_HIGH_PRIORITY_REQUEST_QUEUE_T * fw_hprq,
   SCIF_SAS_DOMAIN_T                      * fw_domain
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io;
   POINTER_UINT            io_address;
   U32                     index;
   U32                     element_count;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(&fw_hprq->lock),
      SCIF_LOG_OBJECT_CONTROLLER | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_high_priority_request_queue_purge_domain(0x%x,0x%x) enter\n",
      fw_hprq, fw_domain
   ));

   element_count = sci_pool_count(fw_hprq->pool);

   scif_cb_lock_acquire(fw_domain->controller, &fw_hprq->lock);

   for (index = 0; index < element_count; index++)
   {
      sci_pool_get(fw_hprq->pool, io_address);

      fw_io = (SCIF_SAS_IO_REQUEST_T*) io_address;

      // If the high priority request is not intended for this domain,
      // then it can be left in the pool.
      if (fw_io->parent.device->domain != fw_domain)
      {
         sci_pool_put(fw_hprq->pool, io_address);
      }
      else
      {
         if (fw_io->parent.is_internal)
         {
            SCIF_SAS_INTERNAL_IO_REQUEST_T * fw_internal_io =
               (SCIF_SAS_INTERNAL_IO_REQUEST_T *)fw_io;

            // The request was intended for a device in the domain.  Put it
            // back in the pool of freely available internal request memory
            // objects. The internal IO's timer is to be destoyed.
            scif_sas_internal_io_request_destruct(fw_domain->controller, fw_internal_io);
         }
      }
   }

   scif_cb_lock_release(fw_domain->controller, &fw_hprq->lock);
}

