/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2010, LSI Corp.
 * All rights reserved.
 * Author : Manjunath Ranganathaiah
 * Support: freebsdraid@lsi.com
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of the <ORGANIZATION> nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/tws/tws.h>
#include <dev/tws/tws_services.h>
#include <dev/tws/tws_hdm.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>

MALLOC_DEFINE(M_TWS, "twsbuf", "buffers used by tws driver");
int tws_queue_depth = TWS_MAX_REQS;
int tws_enable_msi = 0;
int tws_enable_msix = 0;



/* externs */
extern int tws_cam_attach(struct tws_softc *sc);
extern void tws_cam_detach(struct tws_softc *sc);
extern int tws_init_ctlr(struct tws_softc *sc);
extern boolean tws_ctlr_ready(struct tws_softc *sc);
extern void tws_turn_off_interrupts(struct tws_softc *sc);
extern void tws_q_insert_tail(struct tws_softc *sc, struct tws_request *req,
                                u_int8_t q_type );
extern struct tws_request *tws_q_remove_request(struct tws_softc *sc,
                                   struct tws_request *req, u_int8_t q_type );
extern struct tws_request *tws_q_remove_head(struct tws_softc *sc, 
                                                       u_int8_t q_type );
extern boolean tws_get_response(struct tws_softc *sc, u_int16_t *req_id);
extern boolean tws_ctlr_reset(struct tws_softc *sc);
extern void tws_intr(void *arg);
extern int tws_use_32bit_sgls;


struct tws_request *tws_get_request(struct tws_softc *sc, u_int16_t type);
int tws_init_connect(struct tws_softc *sc, u_int16_t mc);
void tws_send_event(struct tws_softc *sc, u_int8_t event);
uint8_t tws_get_state(struct tws_softc *sc);
void tws_release_request(struct tws_request *req);



/* Function prototypes */
static d_open_t     tws_open;
static d_close_t    tws_close;
static d_read_t     tws_read;
static d_write_t    tws_write;
extern d_ioctl_t    tws_ioctl;

static int tws_init(struct tws_softc *sc);
static void tws_dmamap_cmds_load_cbfn(void *arg, bus_dma_segment_t *segs,
                           int nseg, int error);

static int tws_init_reqs(struct tws_softc *sc, u_int32_t dma_mem_size);
static int tws_init_aen_q(struct tws_softc *sc);
static int tws_init_trace_q(struct tws_softc *sc);
static int tws_setup_irq(struct tws_softc *sc);
int tws_setup_intr(struct tws_softc *sc, int irqs);
int tws_teardown_intr(struct tws_softc *sc);


/* Character device entry points */

static struct cdevsw tws_cdevsw = {
    .d_version =    D_VERSION,
    .d_open =   tws_open,
    .d_close =  tws_close,
    .d_read =   tws_read,
    .d_write =  tws_write,
    .d_ioctl =  tws_ioctl,
    .d_name =   "tws",
};

/*
 * In the cdevsw routines, we find our softc by using the si_drv1 member
 * of struct cdev.  We set this variable to point to our softc in our
 * attach routine when we create the /dev entry.
 */

int
tws_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
    struct tws_softc *sc = dev->si_drv1;

    if ( sc ) 
        TWS_TRACE_DEBUG(sc, "entry", dev, oflags);
    return (0);
}

int
tws_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
    struct tws_softc *sc = dev->si_drv1;

    if ( sc )
        TWS_TRACE_DEBUG(sc, "entry", dev, fflag);
    return (0);
}

int
tws_read(struct cdev *dev, struct uio *uio, int ioflag)
{
    struct tws_softc *sc = dev->si_drv1;

    if ( sc )
        TWS_TRACE_DEBUG(sc, "entry", dev, ioflag);
    return (0);
}

int
tws_write(struct cdev *dev, struct uio *uio, int ioflag)
{
    struct tws_softc *sc = dev->si_drv1;

    if ( sc ) 
        TWS_TRACE_DEBUG(sc, "entry", dev, ioflag);
    return (0);
}

/* PCI Support Functions */

/*
 * Compare the device ID of this device against the IDs that this driver
 * supports.  If there is a match, set the description and return success.
 */
static int
tws_probe(device_t dev)
{
    static u_int8_t first_ctlr = 1;

    if ((pci_get_vendor(dev) == TWS_VENDOR_ID) &&
        (pci_get_device(dev) == TWS_DEVICE_ID)) {
        device_set_desc(dev, "LSI 3ware SAS/SATA Storage Controller");
        if (first_ctlr) {
            printf("LSI 3ware device driver for SAS/SATA storage "
                    "controllers, version: %s\n", TWS_DRIVER_VERSION_STRING);
            first_ctlr = 0;
        }

        return(BUS_PROBE_DEFAULT);
    }
    return (ENXIO);
}

/* Attach function is only called if the probe is successful. */

static int
tws_attach(device_t dev)
{
    struct tws_softc *sc = device_get_softc(dev);
    u_int32_t bar;
    int error=0,i;

    /* no tracing yet */
    /* Look up our softc and initialize its fields. */
    sc->tws_dev = dev;
    sc->device_id = pci_get_device(dev);
    sc->subvendor_id = pci_get_subvendor(dev);
    sc->subdevice_id = pci_get_subdevice(dev);

    /* Intialize mutexes */
    mtx_init( &sc->q_lock, "tws_q_lock", NULL, MTX_DEF);
    mtx_init( &sc->sim_lock,  "tws_sim_lock", NULL, MTX_DEF);
    mtx_init( &sc->gen_lock,  "tws_gen_lock", NULL, MTX_DEF);
    mtx_init( &sc->io_lock,  "tws_io_lock", NULL, MTX_DEF | MTX_RECURSE);
    callout_init(&sc->stats_timer, 1);

    if ( tws_init_trace_q(sc) == FAILURE )
        printf("trace init failure\n");
    /* send init event */
    mtx_lock(&sc->gen_lock);
    tws_send_event(sc, TWS_INIT_START);
    mtx_unlock(&sc->gen_lock);


#if _BYTE_ORDER == _BIG_ENDIAN
    TWS_TRACE(sc, "BIG endian", 0, 0);
#endif
    /* sysctl context setup */
    sysctl_ctx_init(&sc->tws_clist);
    sc->tws_oidp = SYSCTL_ADD_NODE(&sc->tws_clist,
                                   SYSCTL_STATIC_CHILDREN(_hw), OID_AUTO,
                                   device_get_nameunit(dev), 
                                   CTLFLAG_RD, 0, "");
    if ( sc->tws_oidp == NULL ) {
        tws_log(sc, SYSCTL_TREE_NODE_ADD);
        goto attach_fail_1;
    }
    SYSCTL_ADD_STRING(&sc->tws_clist, SYSCTL_CHILDREN(sc->tws_oidp),
                      OID_AUTO, "driver_version", CTLFLAG_RD,
                      TWS_DRIVER_VERSION_STRING, 0, "TWS driver version");

    pci_enable_busmaster(dev);

    bar = pci_read_config(dev, TWS_PCI_BAR0, 4);
    TWS_TRACE_DEBUG(sc, "bar0 ", bar, 0);
    bar = pci_read_config(dev, TWS_PCI_BAR1, 4);
    bar = bar & ~TWS_BIT2;
    TWS_TRACE_DEBUG(sc, "bar1 ", bar, 0);
 
    /* MFA base address is BAR2 register used for 
     * push mode. Firmware will evatualy move to 
     * pull mode during witch this needs to change
     */ 
#ifndef TWS_PULL_MODE_ENABLE
    sc->mfa_base = (u_int64_t)pci_read_config(dev, TWS_PCI_BAR2, 4);
    sc->mfa_base = sc->mfa_base & ~TWS_BIT2;
    TWS_TRACE_DEBUG(sc, "bar2 ", sc->mfa_base, 0);
#endif

    /* allocate MMIO register space */ 
    sc->reg_res_id = TWS_PCI_BAR1; /* BAR1 offset */
    if ((sc->reg_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
                                &(sc->reg_res_id), RF_ACTIVE))
                                == NULL) {
        tws_log(sc, ALLOC_MEMORY_RES);
        goto attach_fail_1;
    }
    sc->bus_tag = rman_get_bustag(sc->reg_res);
    sc->bus_handle = rman_get_bushandle(sc->reg_res);

#ifndef TWS_PULL_MODE_ENABLE
    /* Allocate bus space for inbound mfa */ 
    sc->mfa_res_id = TWS_PCI_BAR2; /* BAR2 offset */
    if ((sc->mfa_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
                          &(sc->mfa_res_id), RF_ACTIVE))
                                == NULL) {
        tws_log(sc, ALLOC_MEMORY_RES);
        goto attach_fail_2;
    }
    sc->bus_mfa_tag = rman_get_bustag(sc->mfa_res);
    sc->bus_mfa_handle = rman_get_bushandle(sc->mfa_res);
#endif

    /* Allocate and register our interrupt. */
    sc->intr_type = TWS_INTx; /* default */

    if ( tws_enable_msi )
        sc->intr_type = TWS_MSI;
    if ( tws_setup_irq(sc) == FAILURE ) {
        tws_log(sc, ALLOC_MEMORY_RES);
        goto attach_fail_3;
    }

    /*
     * Create a /dev entry for this device.  The kernel will assign us
     * a major number automatically.  We use the unit number of this
     * device as the minor number and name the character device
     * "tws<unit>".
     */
    sc->tws_cdev = make_dev(&tws_cdevsw, device_get_unit(dev),
        UID_ROOT, GID_OPERATOR, S_IRUSR | S_IWUSR, "tws%u", 
        device_get_unit(dev));
    sc->tws_cdev->si_drv1 = sc;

    if ( tws_init(sc) == FAILURE ) {
        tws_log(sc, TWS_INIT_FAILURE);
        goto attach_fail_4;
    }
    if ( tws_init_ctlr(sc) == FAILURE ) {
        tws_log(sc, TWS_CTLR_INIT_FAILURE);
        goto attach_fail_4;
    }
    if ((error = tws_cam_attach(sc))) {
        tws_log(sc, TWS_CAM_ATTACH);
        goto attach_fail_4;
    }
    /* send init complete event */
    mtx_lock(&sc->gen_lock);
    tws_send_event(sc, TWS_INIT_COMPLETE);
    mtx_unlock(&sc->gen_lock);
        
    TWS_TRACE_DEBUG(sc, "attached successfully", 0, sc->device_id);
    return(0);

attach_fail_4:
    tws_teardown_intr(sc);
    destroy_dev(sc->tws_cdev);
    if (sc->dma_mem_phys)
	    bus_dmamap_unload(sc->cmd_tag, sc->cmd_map);
    if (sc->dma_mem)
	    bus_dmamem_free(sc->cmd_tag, sc->dma_mem, sc->cmd_map);
    if (sc->cmd_tag)
	    bus_dma_tag_destroy(sc->cmd_tag);
attach_fail_3:
    for(i=0;i<sc->irqs;i++) {
        if ( sc->irq_res[i] ){
            if (bus_release_resource(sc->tws_dev,
                 SYS_RES_IRQ, sc->irq_res_id[i], sc->irq_res[i]))
                TWS_TRACE(sc, "bus irq res", 0, 0);
        }
    }
#ifndef TWS_PULL_MODE_ENABLE
attach_fail_2: 
#endif
    if ( sc->mfa_res ){
        if (bus_release_resource(sc->tws_dev,
                 SYS_RES_MEMORY, sc->mfa_res_id, sc->mfa_res))
            TWS_TRACE(sc, "bus release ", 0, sc->mfa_res_id);
    }
    if ( sc->reg_res ){
        if (bus_release_resource(sc->tws_dev,
                 SYS_RES_MEMORY, sc->reg_res_id, sc->reg_res))
            TWS_TRACE(sc, "bus release2 ", 0, sc->reg_res_id);
    }
attach_fail_1:
    mtx_destroy(&sc->q_lock);
    mtx_destroy(&sc->sim_lock);
    mtx_destroy(&sc->gen_lock);
    mtx_destroy(&sc->io_lock);
    sysctl_ctx_free(&sc->tws_clist);
    return (ENXIO);
}

/* Detach device. */

static int
tws_detach(device_t dev)
{
    struct tws_softc *sc = device_get_softc(dev);
    int i;
    u_int32_t reg;

    TWS_TRACE_DEBUG(sc, "entry", 0, 0);

    mtx_lock(&sc->gen_lock);
    tws_send_event(sc, TWS_UNINIT_START);
    mtx_unlock(&sc->gen_lock);

    /* needs to disable interrupt before detaching from cam */
    tws_turn_off_interrupts(sc);
    /* clear door bell */
    tws_write_reg(sc, TWS_I2O0_HOBDBC, ~0, 4);
    reg = tws_read_reg(sc, TWS_I2O0_HIMASK, 4);
    TWS_TRACE_DEBUG(sc, "turn-off-intr", reg, 0);
    sc->obfl_q_overrun = false;
    tws_init_connect(sc, 1);

    /* Teardown the state in our softc created in our attach routine. */
    /* Disconnect the interrupt handler. */
    tws_teardown_intr(sc);

    /* Release irq resource */
    for(i=0;i<sc->irqs;i++) {
        if ( sc->irq_res[i] ){
            if (bus_release_resource(sc->tws_dev,
                     SYS_RES_IRQ, sc->irq_res_id[i], sc->irq_res[i]))
                TWS_TRACE(sc, "bus release irq resource", 
                                       i, sc->irq_res_id[i]);
        }
    }
    if ( sc->intr_type == TWS_MSI ) {
        pci_release_msi(sc->tws_dev);
    }

    tws_cam_detach(sc);

    if (sc->dma_mem_phys)
	    bus_dmamap_unload(sc->cmd_tag, sc->cmd_map);
    if (sc->dma_mem)
	    bus_dmamem_free(sc->cmd_tag, sc->dma_mem, sc->cmd_map);
    if (sc->cmd_tag)
	    bus_dma_tag_destroy(sc->cmd_tag);

    /* Release memory resource */
    if ( sc->mfa_res ){
        if (bus_release_resource(sc->tws_dev,
                 SYS_RES_MEMORY, sc->mfa_res_id, sc->mfa_res))
            TWS_TRACE(sc, "bus release mem resource", 0, sc->mfa_res_id);
    }
    if ( sc->reg_res ){
        if (bus_release_resource(sc->tws_dev,
                 SYS_RES_MEMORY, sc->reg_res_id, sc->reg_res))
            TWS_TRACE(sc, "bus release mem resource", 0, sc->reg_res_id);
    }

    for ( i=0; i< tws_queue_depth; i++) {
	    if (sc->reqs[i].dma_map)
		    bus_dmamap_destroy(sc->data_tag, sc->reqs[i].dma_map);
	    callout_drain(&sc->reqs[i].timeout);
    }

    callout_drain(&sc->stats_timer);
    free(sc->reqs, M_TWS);
    free(sc->sense_bufs, M_TWS);
    free(sc->scan_ccb, M_TWS);
    if (sc->ioctl_data_mem)
            bus_dmamem_free(sc->data_tag, sc->ioctl_data_mem, sc->ioctl_data_map);
    if (sc->data_tag)
	    bus_dma_tag_destroy(sc->data_tag);
    free(sc->aen_q.q, M_TWS);
    free(sc->trace_q.q, M_TWS);
    mtx_destroy(&sc->q_lock);
    mtx_destroy(&sc->sim_lock);
    mtx_destroy(&sc->gen_lock);
    mtx_destroy(&sc->io_lock);
    destroy_dev(sc->tws_cdev);
    sysctl_ctx_free(&sc->tws_clist);
    return (0);
}

int
tws_setup_intr(struct tws_softc *sc, int irqs)
{
    int i, error;

    for(i=0;i<irqs;i++) {
        if (!(sc->intr_handle[i])) {
            if ((error = bus_setup_intr(sc->tws_dev, sc->irq_res[i],
                                    INTR_TYPE_CAM | INTR_MPSAFE,
                                    NULL, 
                                    tws_intr, sc, &sc->intr_handle[i]))) {
                tws_log(sc, SETUP_INTR_RES);
                return(FAILURE);
            }
        }
    }
    return(SUCCESS);

}


int
tws_teardown_intr(struct tws_softc *sc)
{
    int i, error;

    for(i=0;i<sc->irqs;i++) {
        if (sc->intr_handle[i]) {
            error = bus_teardown_intr(sc->tws_dev,
                                      sc->irq_res[i], sc->intr_handle[i]);
            sc->intr_handle[i] = NULL;
        }
    }
    return(SUCCESS);
}


static int 
tws_setup_irq(struct tws_softc *sc)
{
    int messages;

    switch(sc->intr_type) {
        case TWS_INTx :
            sc->irqs = 1;
            sc->irq_res_id[0] = 0;
            sc->irq_res[0] = bus_alloc_resource_any(sc->tws_dev, SYS_RES_IRQ,
                            &sc->irq_res_id[0], RF_SHAREABLE | RF_ACTIVE);
            if ( ! sc->irq_res[0] )
                return(FAILURE);
            if ( tws_setup_intr(sc, sc->irqs) == FAILURE )
                return(FAILURE);
            device_printf(sc->tws_dev, "Using legacy INTx\n");
            break;
        case TWS_MSI :
            sc->irqs = 1;
            sc->irq_res_id[0] = 1;
            messages = 1;
            if (pci_alloc_msi(sc->tws_dev, &messages) != 0 ) {
                TWS_TRACE(sc, "pci alloc msi fail", 0, messages);
                return(FAILURE);
            }
            sc->irq_res[0] = bus_alloc_resource_any(sc->tws_dev, SYS_RES_IRQ,
                              &sc->irq_res_id[0], RF_SHAREABLE | RF_ACTIVE);
              
            if ( !sc->irq_res[0]  )
                return(FAILURE);
            if ( tws_setup_intr(sc, sc->irqs) == FAILURE )
                return(FAILURE);
            device_printf(sc->tws_dev, "Using MSI\n");
            break;

    }

    return(SUCCESS);
}

static int
tws_init(struct tws_softc *sc)
{

    u_int32_t max_sg_elements;
    u_int32_t dma_mem_size;
    int error;
    u_int32_t reg;

    sc->seq_id = 0;
    if ( tws_queue_depth > TWS_MAX_REQS )
        tws_queue_depth = TWS_MAX_REQS;
    if (tws_queue_depth < TWS_RESERVED_REQS+1)
        tws_queue_depth = TWS_RESERVED_REQS+1;
    sc->is64bit = (sizeof(bus_addr_t) == 8) ? true : false;
    max_sg_elements = (sc->is64bit && !tws_use_32bit_sgls) ? 
                                 TWS_MAX_64BIT_SG_ELEMENTS : 
                                 TWS_MAX_32BIT_SG_ELEMENTS;
    dma_mem_size = (sizeof(struct tws_command_packet) * tws_queue_depth) +
                             (TWS_SECTOR_SIZE) ;
    if ( bus_dma_tag_create(bus_get_dma_tag(sc->tws_dev), /* PCI parent */ 
                            TWS_ALIGNMENT,           /* alignment */
                            0,                       /* boundary */
                            BUS_SPACE_MAXADDR_32BIT, /* lowaddr */
                            BUS_SPACE_MAXADDR,       /* highaddr */
                            NULL, NULL,              /* filter, filterarg */
                            BUS_SPACE_MAXSIZE,       /* maxsize */
                            max_sg_elements,         /* numsegs */
                            BUS_SPACE_MAXSIZE,       /* maxsegsize */
                            0,                       /* flags */
                            NULL, NULL,              /* lockfunc, lockfuncarg */
                            &sc->parent_tag          /* tag */
                           )) {
        TWS_TRACE_DEBUG(sc, "DMA parent tag Create fail", max_sg_elements, 
                                                    sc->is64bit);
        return(ENOMEM);
    }
    /* In bound message frame requires 16byte alignment.
     * Outbound MF's can live with 4byte alignment - for now just 
     * use 16 for both.
     */
    if ( bus_dma_tag_create(sc->parent_tag,       /* parent */          
                            TWS_IN_MF_ALIGNMENT,  /* alignment */
                            0,                    /* boundary */
                            BUS_SPACE_MAXADDR_32BIT, /* lowaddr */
                            BUS_SPACE_MAXADDR,    /* highaddr */
                            NULL, NULL,           /* filter, filterarg */
                            dma_mem_size,         /* maxsize */
                            1,                    /* numsegs */
                            BUS_SPACE_MAXSIZE,    /* maxsegsize */
                            0,                    /* flags */
                            NULL, NULL,           /* lockfunc, lockfuncarg */
                            &sc->cmd_tag          /* tag */
                           )) {
        TWS_TRACE_DEBUG(sc, "DMA cmd tag Create fail", max_sg_elements, sc->is64bit);
        return(ENOMEM);
    }

    if (bus_dmamem_alloc(sc->cmd_tag, &sc->dma_mem,
                    BUS_DMA_NOWAIT, &sc->cmd_map)) {
        TWS_TRACE_DEBUG(sc, "DMA mem alloc fail", max_sg_elements, sc->is64bit);
        return(ENOMEM);
    }

    /* if bus_dmamem_alloc succeeds then bus_dmamap_load will succeed */
    sc->dma_mem_phys=0;
    error = bus_dmamap_load(sc->cmd_tag, sc->cmd_map, sc->dma_mem,
                    dma_mem_size, tws_dmamap_cmds_load_cbfn,
                    &sc->dma_mem_phys, 0);

   /*
    * Create a dma tag for data buffers; size will be the maximum
    * possible I/O size (128kB).
    */
    if (bus_dma_tag_create(sc->parent_tag,         /* parent */
                           TWS_ALIGNMENT,          /* alignment */
                           0,                      /* boundary */
                           BUS_SPACE_MAXADDR_32BIT,/* lowaddr */
                           BUS_SPACE_MAXADDR,      /* highaddr */
                           NULL, NULL,             /* filter, filterarg */
                           TWS_MAX_IO_SIZE,        /* maxsize */
                           max_sg_elements,        /* nsegments */
                           TWS_MAX_IO_SIZE,        /* maxsegsize */
                           BUS_DMA_ALLOCNOW,       /* flags */
                           busdma_lock_mutex,      /* lockfunc */
                           &sc->io_lock,           /* lockfuncarg */
                           &sc->data_tag           /* tag */)) {
        TWS_TRACE_DEBUG(sc, "DMA cmd tag Create fail", max_sg_elements, sc->is64bit);
        return(ENOMEM);
    }

    sc->reqs = malloc(sizeof(struct tws_request) * tws_queue_depth, M_TWS,
                      M_WAITOK | M_ZERO);
    sc->sense_bufs = malloc(sizeof(struct tws_sense) * tws_queue_depth, M_TWS,
                      M_WAITOK | M_ZERO);
    sc->scan_ccb = malloc(sizeof(union ccb), M_TWS, M_WAITOK | M_ZERO);
    if (bus_dmamem_alloc(sc->data_tag, (void **)&sc->ioctl_data_mem,
            (BUS_DMA_NOWAIT | BUS_DMA_ZERO), &sc->ioctl_data_map)) {
        device_printf(sc->tws_dev, "Cannot allocate ioctl data mem\n");
        return(ENOMEM);
    }

    if ( !tws_ctlr_ready(sc) )
        if( !tws_ctlr_reset(sc) )
            return(FAILURE);
    
    bzero(&sc->stats, sizeof(struct tws_stats));
    tws_init_qs(sc);
    tws_turn_off_interrupts(sc);

    /* 
     * enable pull mode by setting bit1 .
     * setting bit0 to 1 will enable interrupt coalesing 
     * will revisit. 
     */

#ifdef TWS_PULL_MODE_ENABLE

    reg = tws_read_reg(sc, TWS_I2O0_CTL, 4);
    TWS_TRACE_DEBUG(sc, "i20 ctl", reg, TWS_I2O0_CTL);
    tws_write_reg(sc, TWS_I2O0_CTL, reg | TWS_BIT1, 4);

#endif

    TWS_TRACE_DEBUG(sc, "dma_mem_phys", sc->dma_mem_phys, TWS_I2O0_CTL);
    if ( tws_init_reqs(sc, dma_mem_size) == FAILURE )
        return(FAILURE);
    if ( tws_init_aen_q(sc) == FAILURE )
        return(FAILURE);

    return(SUCCESS);
    
} 

static int
tws_init_aen_q(struct tws_softc *sc)
{
    sc->aen_q.head=0;
    sc->aen_q.tail=0;
    sc->aen_q.depth=256;
    sc->aen_q.overflow=0;
    sc->aen_q.q = malloc(sizeof(struct tws_event_packet)*sc->aen_q.depth, 
                              M_TWS, M_WAITOK | M_ZERO);
    return(SUCCESS);
}

static int
tws_init_trace_q(struct tws_softc *sc)
{
    sc->trace_q.head=0;
    sc->trace_q.tail=0;
    sc->trace_q.depth=256;
    sc->trace_q.overflow=0;
    sc->trace_q.q = malloc(sizeof(struct tws_trace_rec)*sc->trace_q.depth,
                              M_TWS, M_WAITOK | M_ZERO);
    return(SUCCESS);
}

static int
tws_init_reqs(struct tws_softc *sc, u_int32_t dma_mem_size)
{

    struct tws_command_packet *cmd_buf;
    cmd_buf = (struct tws_command_packet *)sc->dma_mem;
    int i;

    bzero(cmd_buf, dma_mem_size);
    TWS_TRACE_DEBUG(sc, "phy cmd", sc->dma_mem_phys, 0);
    mtx_lock(&sc->q_lock);
    for ( i=0; i< tws_queue_depth; i++)
    {
        if (bus_dmamap_create(sc->data_tag, 0, &sc->reqs[i].dma_map)) {
            /* log a ENOMEM failure msg here */
            mtx_unlock(&sc->q_lock);
            return(FAILURE);
        } 
        sc->reqs[i].cmd_pkt =  &cmd_buf[i];

        sc->sense_bufs[i].hdr = &cmd_buf[i].hdr ;
        sc->sense_bufs[i].hdr_pkt_phy = sc->dma_mem_phys + 
                              (i * sizeof(struct tws_command_packet));

        sc->reqs[i].cmd_pkt_phy = sc->dma_mem_phys + 
                              sizeof(struct tws_command_header) +
                              (i * sizeof(struct tws_command_packet));
        sc->reqs[i].request_id = i;
        sc->reqs[i].sc = sc;

        sc->reqs[i].cmd_pkt->hdr.header_desc.size_header = 128;

	callout_init(&sc->reqs[i].timeout, 1);
        sc->reqs[i].state = TWS_REQ_STATE_FREE;
        if ( i >= TWS_RESERVED_REQS )
            tws_q_insert_tail(sc, &sc->reqs[i], TWS_FREE_Q);
    }
    mtx_unlock(&sc->q_lock);
    return(SUCCESS);
}

static void
tws_dmamap_cmds_load_cbfn(void *arg, bus_dma_segment_t *segs,
                           int nseg, int error)
{

    /* printf("command load done \n"); */

    *((bus_addr_t *)arg) = segs[0].ds_addr;
}

void
tws_send_event(struct tws_softc *sc, u_int8_t event)
{
    mtx_assert(&sc->gen_lock, MA_OWNED);
    TWS_TRACE_DEBUG(sc, "received event ", 0, event);
    switch (event) {

        case TWS_INIT_START:
            sc->tws_state = TWS_INIT;
            break;

        case TWS_INIT_COMPLETE:
            if (sc->tws_state != TWS_INIT) {
                device_printf(sc->tws_dev, "invalid state transition %d => TWS_ONLINE\n", sc->tws_state);
            } else {
                sc->tws_state = TWS_ONLINE;
            }
            break;

        case TWS_RESET_START:
            /* We can transition to reset state from any state except reset*/ 
            if (sc->tws_state != TWS_RESET) {
                sc->tws_prev_state = sc->tws_state;
                sc->tws_state = TWS_RESET;
            }
            break;

        case TWS_RESET_COMPLETE:
            if (sc->tws_state != TWS_RESET) {
                device_printf(sc->tws_dev, "invalid state transition %d => %d (previous state)\n", sc->tws_state, sc->tws_prev_state);
            } else {
                sc->tws_state = sc->tws_prev_state;
            }
            break;

        case TWS_SCAN_FAILURE:
            if (sc->tws_state != TWS_ONLINE) {
                device_printf(sc->tws_dev, "invalid state transition %d => TWS_OFFLINE\n", sc->tws_state);
            } else {
                sc->tws_state = TWS_OFFLINE;
            }
            break;

        case TWS_UNINIT_START:
            if ((sc->tws_state != TWS_ONLINE) && (sc->tws_state != TWS_OFFLINE)) {
                device_printf(sc->tws_dev, "invalid state transition %d => TWS_UNINIT\n", sc->tws_state);
            } else {
                sc->tws_state = TWS_UNINIT;
            }
            break;
    }

}

uint8_t
tws_get_state(struct tws_softc *sc)
{
  
    return((u_int8_t)sc->tws_state);

}

/* Called during system shutdown after sync. */

static int
tws_shutdown(device_t dev)
{

    struct tws_softc *sc = device_get_softc(dev);

    TWS_TRACE_DEBUG(sc, "entry", 0, 0);

    tws_turn_off_interrupts(sc);
    tws_init_connect(sc, 1);

    return (0);
}

/*
 * Device suspend routine.
 */
static int
tws_suspend(device_t dev)
{
    struct tws_softc *sc = device_get_softc(dev);

    if ( sc )
        TWS_TRACE_DEBUG(sc, "entry", 0, 0);
    return (0);
}

/*
 * Device resume routine.
 */
static int
tws_resume(device_t dev)
{

    struct tws_softc *sc = device_get_softc(dev);

    if ( sc )
        TWS_TRACE_DEBUG(sc, "entry", 0, 0);
    return (0);
}


struct tws_request *
tws_get_request(struct tws_softc *sc, u_int16_t type)
{
    struct mtx *my_mutex = ((type == TWS_REQ_TYPE_SCSI_IO) ? &sc->q_lock : &sc->gen_lock);
    struct tws_request *r = NULL;

    mtx_lock(my_mutex);

    if (type == TWS_REQ_TYPE_SCSI_IO) {
        r = tws_q_remove_head(sc, TWS_FREE_Q);
    } else {
        if ( sc->reqs[type].state == TWS_REQ_STATE_FREE ) {
            r = &sc->reqs[type];
        }
    }

    if ( r ) {
        bzero(&r->cmd_pkt->cmd, sizeof(struct tws_command_apache));
        r->data = NULL;
        r->length = 0;
        r->type = type;
        r->flags = TWS_DIR_UNKNOWN;
        r->error_code = TWS_REQ_RET_INVALID;
        r->cb = NULL;
        r->ccb_ptr = NULL;
	callout_stop(&r->timeout);
        r->next = r->prev = NULL;

        r->state = ((type == TWS_REQ_TYPE_SCSI_IO) ? TWS_REQ_STATE_TRAN : TWS_REQ_STATE_BUSY);
    }

    mtx_unlock(my_mutex);

    return(r);
}

void
tws_release_request(struct tws_request *req)
{

    struct tws_softc *sc = req->sc;

    TWS_TRACE_DEBUG(sc, "entry", sc, 0);
    mtx_lock(&sc->q_lock);
    tws_q_insert_tail(sc, req, TWS_FREE_Q);
    mtx_unlock(&sc->q_lock);
}

static device_method_t tws_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,     tws_probe),
    DEVMETHOD(device_attach,    tws_attach),
    DEVMETHOD(device_detach,    tws_detach),
    DEVMETHOD(device_shutdown,  tws_shutdown),
    DEVMETHOD(device_suspend,   tws_suspend),
    DEVMETHOD(device_resume,    tws_resume),

    DEVMETHOD_END
};

static driver_t tws_driver = {
        "tws",
        tws_methods,
        sizeof(struct tws_softc)
};


static devclass_t tws_devclass;

/* DEFINE_CLASS_0(tws, tws_driver, tws_methods, sizeof(struct tws_softc)); */
DRIVER_MODULE(tws, pci, tws_driver, tws_devclass, 0, 0);
MODULE_DEPEND(tws, cam, 1, 1, 1);
MODULE_DEPEND(tws, pci, 1, 1, 1);

TUNABLE_INT("hw.tws.queue_depth", &tws_queue_depth);
TUNABLE_INT("hw.tws.enable_msi", &tws_enable_msi);
