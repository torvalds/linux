/*************************************************************************
SPDX-License-Identifier: BSD-3-Clause

Copyright (c) 2003-2007  Cavium Networks (support@cavium.com). All rights
reserved.


Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.

    * Neither the name of Cavium Networks nor the names of
      its contributors may be used to endorse or promote products
      derived from this software without specific prior written
      permission.

This Software, including technical data, may be subject to U.S. export  control laws, including the U.S. Export Administration Act and its  associated regulations, and may be subject to export or import  regulations in other countries.

TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
AND WITH ALL FAULTS AND CAVIUM  NETWORKS MAKES NO PROMISES, REPRESENTATIONS OR WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE, MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
*************************************************************************/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/rman.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/module.h>
#include <sys/smp.h>
#include <sys/taskqueue.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>

#include "wrapper-cvmx-includes.h"
#include "ethernet-headers.h"

#include "octebusvar.h"

/*
 * XXX/juli
 * Convert 0444 to tunables, 0644 to sysctls.
 */
#if defined(CONFIG_CAVIUM_OCTEON_NUM_PACKET_BUFFERS) && CONFIG_CAVIUM_OCTEON_NUM_PACKET_BUFFERS
int num_packet_buffers = CONFIG_CAVIUM_OCTEON_NUM_PACKET_BUFFERS;
#else
int num_packet_buffers = 2048;
#endif
TUNABLE_INT("hw.octe.num_packet_buffers", &num_packet_buffers);
/*
		 "\t\tNumber of packet buffers to allocate and store in the\n"
		 "\t\tFPA. By default, 1024 packet buffers are used unless\n"
		 "\t\tCONFIG_CAVIUM_OCTEON_NUM_PACKET_BUFFERS is defined." */

int pow_receive_group = 15;
TUNABLE_INT("hw.octe.pow_receive_group", &pow_receive_group);
/*
		 "\t\tPOW group to receive packets from. All ethernet hardware\n"
		 "\t\twill be configured to send incomming packets to this POW\n"
		 "\t\tgroup. Also any other software can submit packets to this\n"
		 "\t\tgroup for the kernel to process." */

/**
 * Periodic timer to check auto negotiation
 */
static struct callout cvm_oct_poll_timer;

/**
 * Array of every ethernet device owned by this driver indexed by
 * the ipd input port number.
 */
struct ifnet *cvm_oct_device[TOTAL_NUMBER_OF_PORTS];

/**
 * Task to handle link status changes.
 */
static struct taskqueue *cvm_oct_link_taskq;

/*
 * Number of buffers in output buffer pool.
 */
static int cvm_oct_num_output_buffers;

/**
 * Function to update link status.
 */
static void cvm_oct_update_link(void *context, int pending)
{
	cvm_oct_private_t *priv = (cvm_oct_private_t *)context;
	struct ifnet *ifp = priv->ifp;
	cvmx_helper_link_info_t link_info;

	link_info.u64 = priv->link_info;

	if (link_info.s.link_up) {
		if_link_state_change(ifp, LINK_STATE_UP);
		DEBUGPRINT("%s: %u Mbps %s duplex, port %2d, queue %2d\n",
			   if_name(ifp), link_info.s.speed,
			   (link_info.s.full_duplex) ? "Full" : "Half",
			   priv->port, priv->queue);
	} else {
		if_link_state_change(ifp, LINK_STATE_DOWN);
		DEBUGPRINT("%s: Link down\n", if_name(ifp));
	}
	priv->need_link_update = 0;
}

/**
 * Periodic timer tick for slow management operations
 *
 * @param arg    Device to check
 */
static void cvm_do_timer(void *arg)
{
	static int port;
	static int updated;
	if (port < CVMX_PIP_NUM_INPUT_PORTS) {
		if (cvm_oct_device[port]) {
			int queues_per_port;
			int qos;
			cvm_oct_private_t *priv = (cvm_oct_private_t *)cvm_oct_device[port]->if_softc;

			cvm_oct_common_poll(priv->ifp);
			if (priv->need_link_update) {
				updated++;
				taskqueue_enqueue(cvm_oct_link_taskq, &priv->link_task);
			}

			queues_per_port = cvmx_pko_get_num_queues(port);
			/* Drain any pending packets in the free list */
			for (qos = 0; qos < queues_per_port; qos++) {
				if (_IF_QLEN(&priv->tx_free_queue[qos]) > 0) {
					IF_LOCK(&priv->tx_free_queue[qos]);
					while (_IF_QLEN(&priv->tx_free_queue[qos]) > cvmx_fau_fetch_and_add32(priv->fau+qos*4, 0)) {
						struct mbuf *m;

						_IF_DEQUEUE(&priv->tx_free_queue[qos], m);
						m_freem(m);
					}
					IF_UNLOCK(&priv->tx_free_queue[qos]);

					/*
					 * XXX locking!
					 */
					priv->ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
				}
			}
		}
		port++;
		/* Poll the next port in a 50th of a second.
		   This spreads the polling of ports out a little bit */
		callout_reset(&cvm_oct_poll_timer, hz / 50, cvm_do_timer, NULL);
	} else {
		port = 0;
		/* If any updates were made in this run, continue iterating at
		 * 1/50th of a second, so that if a link has merely gone down
		 * temporarily (e.g. because of interface reinitialization) it
		 * will not be forced to stay down for an entire second.
		 */
		if (updated > 0) {
			updated = 0;
			callout_reset(&cvm_oct_poll_timer, hz / 50, cvm_do_timer, NULL);
		} else {
			/* All ports have been polled. Start the next iteration through
			   the ports in one second */
			callout_reset(&cvm_oct_poll_timer, hz, cvm_do_timer, NULL);
		}
	}
}

/**
 * Configure common hardware for all interfaces
 */
static void cvm_oct_configure_common_hw(device_t bus)
{
	struct octebus_softc *sc;
	int pko_queues;
	int error;
	int rid;

        sc = device_get_softc(bus);

	/* Setup the FPA */
	cvmx_fpa_enable();
	cvm_oct_mem_fill_fpa(CVMX_FPA_PACKET_POOL, CVMX_FPA_PACKET_POOL_SIZE,
			     num_packet_buffers);
	cvm_oct_mem_fill_fpa(CVMX_FPA_WQE_POOL, CVMX_FPA_WQE_POOL_SIZE,
			     num_packet_buffers);
	if (CVMX_FPA_OUTPUT_BUFFER_POOL != CVMX_FPA_PACKET_POOL) {
		/*
		 * If the FPA uses different pools for output buffers and
		 * packets, size the output buffer pool based on the number
		 * of PKO queues.
		 */
		if (OCTEON_IS_MODEL(OCTEON_CN38XX))
			pko_queues = 128;
		else if (OCTEON_IS_MODEL(OCTEON_CN3XXX))
			pko_queues = 32;
		else if (OCTEON_IS_MODEL(OCTEON_CN50XX))
			pko_queues = 32;
		else
			pko_queues = 256;

		cvm_oct_num_output_buffers = 4 * pko_queues;
		cvm_oct_mem_fill_fpa(CVMX_FPA_OUTPUT_BUFFER_POOL,
				     CVMX_FPA_OUTPUT_BUFFER_POOL_SIZE,
				     cvm_oct_num_output_buffers);
	}

	if (USE_RED)
		cvmx_helper_setup_red(num_packet_buffers/4,
				      num_packet_buffers/8);

	/* Enable the MII interface */
	if (cvmx_sysinfo_get()->board_type != CVMX_BOARD_TYPE_SIM)
		cvmx_write_csr(CVMX_SMI_EN, 1);

	/* Register an IRQ hander for to receive POW interrupts */
        rid = 0;
        sc->sc_rx_irq = bus_alloc_resource(bus, SYS_RES_IRQ, &rid,
					   OCTEON_IRQ_WORKQ0 + pow_receive_group,
					   OCTEON_IRQ_WORKQ0 + pow_receive_group,
					   1, RF_ACTIVE);
        if (sc->sc_rx_irq == NULL) {
                device_printf(bus, "could not allocate workq irq");
		return;
        }

        error = bus_setup_intr(bus, sc->sc_rx_irq, INTR_TYPE_NET | INTR_MPSAFE,
			       cvm_oct_do_interrupt, NULL, cvm_oct_device,
			       &sc->sc_rx_intr_cookie);
        if (error != 0) {
                device_printf(bus, "could not setup workq irq");
		return;
        }


#ifdef SMP
	{
		cvmx_ciu_intx0_t en;
		int core;

		CPU_FOREACH(core) {
			if (core == PCPU_GET(cpuid))
				continue;

			en.u64 = cvmx_read_csr(CVMX_CIU_INTX_EN0(core*2));
			en.s.workq |= (1<<pow_receive_group);
			cvmx_write_csr(CVMX_CIU_INTX_EN0(core*2), en.u64);
		}
	}
#endif
}


/**
 * Free a work queue entry received in a intercept callback.
 *
 * @param work_queue_entry
 *               Work queue entry to free
 * @return Zero on success, Negative on failure.
 */
int cvm_oct_free_work(void *work_queue_entry)
{
	cvmx_wqe_t *work = work_queue_entry;

	int segments = work->word2.s.bufs;
	cvmx_buf_ptr_t segment_ptr = work->packet_ptr;

	while (segments--) {
		cvmx_buf_ptr_t next_ptr = *(cvmx_buf_ptr_t *)cvmx_phys_to_ptr(segment_ptr.s.addr-8);
		if (__predict_false(!segment_ptr.s.i))
			cvmx_fpa_free(cvm_oct_get_buffer_ptr(segment_ptr), segment_ptr.s.pool, DONT_WRITEBACK(CVMX_FPA_PACKET_POOL_SIZE/128));
		segment_ptr = next_ptr;
	}
	cvmx_fpa_free(work, CVMX_FPA_WQE_POOL, DONT_WRITEBACK(1));

	return 0;
}


/**
 * Module/ driver initialization. Creates the linux network
 * devices.
 *
 * @return Zero on success
 */
int cvm_oct_init_module(device_t bus)
{
	device_t dev;
	int ifnum;
	int num_interfaces;
	int interface;
	int fau = FAU_NUM_PACKET_BUFFERS_TO_FREE;
	int qos;

	cvm_oct_rx_initialize();
	cvm_oct_configure_common_hw(bus);

	cvmx_helper_initialize_packet_io_global();

	/* Change the input group for all ports before input is enabled */
	num_interfaces = cvmx_helper_get_number_of_interfaces();
	for (interface = 0; interface < num_interfaces; interface++) {
		int num_ports = cvmx_helper_ports_on_interface(interface);
		int port;

		for (port = 0; port < num_ports; port++) {
			cvmx_pip_prt_tagx_t pip_prt_tagx;
			int pkind = cvmx_helper_get_ipd_port(interface, port);

			pip_prt_tagx.u64 = cvmx_read_csr(CVMX_PIP_PRT_TAGX(pkind));
			pip_prt_tagx.s.grp = pow_receive_group;
			cvmx_write_csr(CVMX_PIP_PRT_TAGX(pkind), pip_prt_tagx.u64);
		}
	}

	cvmx_helper_ipd_and_packet_input_enable();

	memset(cvm_oct_device, 0, sizeof(cvm_oct_device));

	cvm_oct_link_taskq = taskqueue_create("octe link", M_NOWAIT,
	    taskqueue_thread_enqueue, &cvm_oct_link_taskq);
	taskqueue_start_threads(&cvm_oct_link_taskq, 1, PI_NET,
	    "octe link taskq");

	/* Initialize the FAU used for counting packet buffers that need to be freed */
	cvmx_fau_atomic_write32(FAU_NUM_PACKET_BUFFERS_TO_FREE, 0);

	ifnum = 0;
	num_interfaces = cvmx_helper_get_number_of_interfaces();
	for (interface = 0; interface < num_interfaces; interface++) {
		cvmx_helper_interface_mode_t imode = cvmx_helper_interface_get_mode(interface);
		int num_ports = cvmx_helper_ports_on_interface(interface);
		int port;

		for (port = cvmx_helper_get_ipd_port(interface, 0);
		     port < cvmx_helper_get_ipd_port(interface, num_ports);
		     ifnum++, port++) {
			cvm_oct_private_t *priv;
			struct ifnet *ifp;
			
			dev = BUS_ADD_CHILD(bus, 0, "octe", ifnum);
			if (dev != NULL)
				ifp = if_alloc(IFT_ETHER);
			if (dev == NULL || ifp == NULL) {
				printf("Failed to allocate ethernet device for interface %d port %d\n", interface, port);
				continue;
			}

			/* Initialize the device private structure. */
			device_probe(dev);
			priv = device_get_softc(dev);
			priv->dev = dev;
			priv->ifp = ifp;
			priv->imode = imode;
			priv->port = port;
			priv->queue = cvmx_pko_get_base_queue(priv->port);
			priv->fau = fau - cvmx_pko_get_num_queues(port) * 4;
			for (qos = 0; qos < cvmx_pko_get_num_queues(port); qos++)
				cvmx_fau_atomic_write32(priv->fau+qos*4, 0);
			TASK_INIT(&priv->link_task, 0, cvm_oct_update_link, priv);

			switch (priv->imode) {

			/* These types don't support ports to IPD/PKO */
			case CVMX_HELPER_INTERFACE_MODE_DISABLED:
			case CVMX_HELPER_INTERFACE_MODE_PCIE:
			case CVMX_HELPER_INTERFACE_MODE_PICMG:
				break;

			case CVMX_HELPER_INTERFACE_MODE_NPI:
				priv->init = cvm_oct_common_init;
				priv->uninit = cvm_oct_common_uninit;
				device_set_desc(dev, "Cavium Octeon NPI Ethernet");
				break;

			case CVMX_HELPER_INTERFACE_MODE_XAUI:
				priv->init = cvm_oct_xaui_init;
				priv->uninit = cvm_oct_common_uninit;
				device_set_desc(dev, "Cavium Octeon XAUI Ethernet");
				break;

			case CVMX_HELPER_INTERFACE_MODE_LOOP:
				priv->init = cvm_oct_common_init;
				priv->uninit = cvm_oct_common_uninit;
				device_set_desc(dev, "Cavium Octeon LOOP Ethernet");
				break;

			case CVMX_HELPER_INTERFACE_MODE_SGMII:
				priv->init = cvm_oct_sgmii_init;
				priv->uninit = cvm_oct_common_uninit;
				device_set_desc(dev, "Cavium Octeon SGMII Ethernet");
				break;

			case CVMX_HELPER_INTERFACE_MODE_SPI:
				priv->init = cvm_oct_spi_init;
				priv->uninit = cvm_oct_spi_uninit;
				device_set_desc(dev, "Cavium Octeon SPI Ethernet");
				break;

			case CVMX_HELPER_INTERFACE_MODE_RGMII:
				priv->init = cvm_oct_rgmii_init;
				priv->uninit = cvm_oct_rgmii_uninit;
				device_set_desc(dev, "Cavium Octeon RGMII Ethernet");
				break;

			case CVMX_HELPER_INTERFACE_MODE_GMII:
				priv->init = cvm_oct_rgmii_init;
				priv->uninit = cvm_oct_rgmii_uninit;
				device_set_desc(dev, "Cavium Octeon GMII Ethernet");
				break;
			}

			ifp->if_softc = priv;

			if (!priv->init) {
				printf("octe%d: unsupported device type interface %d, port %d\n",
				       ifnum, interface, priv->port);
				if_free(ifp);
			} else if (priv->init(ifp) != 0) {
				printf("octe%d: failed to register device for interface %d, port %d\n",
				       ifnum, interface, priv->port);
				if_free(ifp);
			} else {
				cvm_oct_device[priv->port] = ifp;
				fau -= cvmx_pko_get_num_queues(priv->port) * sizeof(uint32_t);
			}
		}
	}

	if (INTERRUPT_LIMIT) {
		/* Set the POW timer rate to give an interrupt at most INTERRUPT_LIMIT times per second */
		cvmx_write_csr(CVMX_POW_WQ_INT_PC, cvmx_clock_get_rate(CVMX_CLOCK_CORE)/((INTERRUPT_LIMIT+1)*16*256)<<8);

		/* Enable POW timer interrupt. It will count when there are packets available */
		cvmx_write_csr(CVMX_POW_WQ_INT_THRX(pow_receive_group), 0x1ful<<24);
	} else {
		/* Enable POW interrupt when our port has at least one packet */
		cvmx_write_csr(CVMX_POW_WQ_INT_THRX(pow_receive_group), 0x1001);
	}

	callout_init(&cvm_oct_poll_timer, 1);
	callout_reset(&cvm_oct_poll_timer, hz, cvm_do_timer, NULL);

	return 0;
}


/**
 * Module / driver shutdown
 *
 * @return Zero on success
 */
void cvm_oct_cleanup_module(device_t bus)
{
	int port;
	struct octebus_softc *sc = device_get_softc(bus);

	/* Disable POW interrupt */
	cvmx_write_csr(CVMX_POW_WQ_INT_THRX(pow_receive_group), 0);

	/* Free the interrupt handler */
	bus_teardown_intr(bus, sc->sc_rx_irq, sc->sc_rx_intr_cookie);

	callout_stop(&cvm_oct_poll_timer);
	cvm_oct_rx_shutdown();

	cvmx_helper_shutdown_packet_io_global();

	/* Free the ethernet devices */
	for (port = 0; port < TOTAL_NUMBER_OF_PORTS; port++) {
		if (cvm_oct_device[port]) {
			cvm_oct_tx_shutdown(cvm_oct_device[port]);
#if 0
			unregister_netdev(cvm_oct_device[port]);
			kfree(cvm_oct_device[port]);
#else
			panic("%s: need to detach and free interface.", __func__);
#endif
			cvm_oct_device[port] = NULL;
		}
	}
	/* Free the HW pools */
	cvm_oct_mem_empty_fpa(CVMX_FPA_PACKET_POOL, CVMX_FPA_PACKET_POOL_SIZE, num_packet_buffers);
	cvm_oct_mem_empty_fpa(CVMX_FPA_WQE_POOL, CVMX_FPA_WQE_POOL_SIZE, num_packet_buffers);

	if (CVMX_FPA_OUTPUT_BUFFER_POOL != CVMX_FPA_PACKET_POOL)
		cvm_oct_mem_empty_fpa(CVMX_FPA_OUTPUT_BUFFER_POOL, CVMX_FPA_OUTPUT_BUFFER_POOL_SIZE, cvm_oct_num_output_buffers);

	/* Disable FPA, all buffers are free, not done by helper shutdown. */
	cvmx_fpa_disable();
}
