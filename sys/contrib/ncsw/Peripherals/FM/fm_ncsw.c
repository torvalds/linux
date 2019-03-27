/*
 * Copyright 2008-2012 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/******************************************************************************
 @File          fm.c

 @Description   FM driver routines implementation.
*//***************************************************************************/
#include "std_ext.h"
#include "error_ext.h"
#include "xx_ext.h"
#include "string_ext.h"
#include "sprint_ext.h"
#include "debug_ext.h"
#include "fm_muram_ext.h"
#include <linux/math64.h>

#include "fm_common.h"
#include "fm_ipc.h"
#include "fm.h"
#include "fsl_fman.h"


/****************************************/
/*       static functions               */
/****************************************/

static volatile bool blockingFlag = FALSE;
static void IpcMsgCompletionCB(t_Handle   h_Fm,
                               uint8_t    *p_Msg,
                               uint8_t    *p_Reply,
                               uint32_t   replyLength,
                               t_Error    status)
{
    UNUSED(h_Fm);UNUSED(p_Msg);UNUSED(p_Reply);UNUSED(replyLength);UNUSED(status);
    blockingFlag = FALSE;
}

static void FreeInitResources(t_Fm *p_Fm)
{
    if (p_Fm->camBaseAddr)
       FM_MURAM_FreeMem(p_Fm->h_FmMuram, UINT_TO_PTR(p_Fm->camBaseAddr));
    if (p_Fm->fifoBaseAddr)
       FM_MURAM_FreeMem(p_Fm->h_FmMuram, UINT_TO_PTR(p_Fm->fifoBaseAddr));
    if (p_Fm->resAddr)
       FM_MURAM_FreeMem(p_Fm->h_FmMuram, UINT_TO_PTR(p_Fm->resAddr));
}

static bool IsFmanCtrlCodeLoaded(t_Fm *p_Fm)
{
    t_FMIramRegs    *p_Iram;

    ASSERT_COND(p_Fm);
    p_Iram = (t_FMIramRegs *)UINT_TO_PTR(p_Fm->baseAddr + FM_MM_IMEM);

    return (bool)!!(GET_UINT32(p_Iram->iready) & IRAM_READY);
}

static t_Error CheckFmParameters(t_Fm *p_Fm)
{
    if (IsFmanCtrlCodeLoaded(p_Fm) && !p_Fm->resetOnInit)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Old FMan CTRL code is loaded; FM must be reset!"));
#if (DPAA_VERSION < 11)
    if (!p_Fm->p_FmDriverParam->dma_axi_dbg_num_of_beats ||
        (p_Fm->p_FmDriverParam->dma_axi_dbg_num_of_beats > DMA_MODE_MAX_AXI_DBG_NUM_OF_BEATS))
        RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                     ("axiDbgNumOfBeats has to be in the range 1 - %d", DMA_MODE_MAX_AXI_DBG_NUM_OF_BEATS));
#endif /* (DPAA_VERSION < 11) */
    if (p_Fm->p_FmDriverParam->dma_cam_num_of_entries % DMA_CAM_UNITS)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("dma_cam_num_of_entries has to be divisble by %d", DMA_CAM_UNITS));
//    if (!p_Fm->p_FmDriverParam->dma_cam_num_of_entries || (p_Fm->p_FmDriverParam->dma_cam_num_of_entries > DMA_MODE_MAX_CAM_NUM_OF_ENTRIES))
//        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("dma_cam_num_of_entries has to be in the range 1 - %d", DMA_MODE_MAX_CAM_NUM_OF_ENTRIES));
    if (p_Fm->p_FmDriverParam->dma_comm_qtsh_asrt_emer > DMA_THRESH_MAX_COMMQ)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("dma_comm_qtsh_asrt_emer can not be larger than %d", DMA_THRESH_MAX_COMMQ));
    if (p_Fm->p_FmDriverParam->dma_comm_qtsh_clr_emer > DMA_THRESH_MAX_COMMQ)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("dma_comm_qtsh_clr_emer can not be larger than %d", DMA_THRESH_MAX_COMMQ));
    if (p_Fm->p_FmDriverParam->dma_comm_qtsh_clr_emer >= p_Fm->p_FmDriverParam->dma_comm_qtsh_asrt_emer)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("dma_comm_qtsh_clr_emer must be smaller than dma_comm_qtsh_asrt_emer"));
#if (DPAA_VERSION < 11)
    if (p_Fm->p_FmDriverParam->dma_read_buf_tsh_asrt_emer > DMA_THRESH_MAX_BUF)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("dma_read_buf_tsh_asrt_emer can not be larger than %d", DMA_THRESH_MAX_BUF));
    if (p_Fm->p_FmDriverParam->dma_read_buf_tsh_clr_emer > DMA_THRESH_MAX_BUF)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("dma_read_buf_tsh_clr_emer can not be larger than %d", DMA_THRESH_MAX_BUF));
    if (p_Fm->p_FmDriverParam->dma_read_buf_tsh_clr_emer >= p_Fm->p_FmDriverParam->dma_read_buf_tsh_asrt_emer)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("dma_read_buf_tsh_clr_emer must be smaller than dma_read_buf_tsh_asrt_emer"));
    if (p_Fm->p_FmDriverParam->dma_write_buf_tsh_asrt_emer > DMA_THRESH_MAX_BUF)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("dma_write_buf_tsh_asrt_emer can not be larger than %d", DMA_THRESH_MAX_BUF));
    if (p_Fm->p_FmDriverParam->dma_write_buf_tsh_clr_emer > DMA_THRESH_MAX_BUF)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("dma_write_buf_tsh_clr_emer can not be larger than %d", DMA_THRESH_MAX_BUF));
    if (p_Fm->p_FmDriverParam->dma_write_buf_tsh_clr_emer >= p_Fm->p_FmDriverParam->dma_write_buf_tsh_asrt_emer)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("dma_write_buf_tsh_clr_emer must be smaller than dma_write_buf_tsh_asrt_emer"));
#else /* (DPAA_VERSION >= 11) */
    if ((p_Fm->p_FmDriverParam->dma_dbg_cnt_mode == E_FMAN_DMA_DBG_CNT_INT_READ_EM)||
            (p_Fm->p_FmDriverParam->dma_dbg_cnt_mode == E_FMAN_DMA_DBG_CNT_INT_WRITE_EM) ||
            (p_Fm->p_FmDriverParam->dma_dbg_cnt_mode == E_FMAN_DMA_DBG_CNT_RAW_WAR_PROT))
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("dma_dbg_cnt_mode value not supported by this integration."));
    if ((p_Fm->p_FmDriverParam->dma_emergency_bus_select == FM_DMA_MURAM_READ_EMERGENCY)||
            (p_Fm->p_FmDriverParam->dma_emergency_bus_select == FM_DMA_MURAM_WRITE_EMERGENCY))
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("emergencyBusSelect value not supported by this integration."));
    if (p_Fm->p_FmDriverParam->dma_stop_on_bus_error)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("dma_stop_on_bus_error not supported by this integration."));
#ifdef FM_AID_MODE_NO_TNUM_SW005
    if (p_Fm->p_FmDriverParam->dma_aid_mode != E_FMAN_DMA_AID_OUT_PORT_ID)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("dma_aid_mode not supported by this integration."));
#endif /* FM_AID_MODE_NO_TNUM_SW005 */
    if (p_Fm->p_FmDriverParam->dma_axi_dbg_num_of_beats)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("dma_axi_dbg_num_of_beats not supported by this integration."));
#endif /* (DPAA_VERSION < 11) */

    if (!p_Fm->p_FmStateStruct->fmClkFreq)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("fmClkFreq must be set."));
    if (USEC_TO_CLK(p_Fm->p_FmDriverParam->dma_watchdog, p_Fm->p_FmStateStruct->fmClkFreq) > DMA_MAX_WATCHDOG)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                     ("dma_watchdog depends on FM clock. dma_watchdog(in microseconds) * clk (in Mhz), may not exceed 0x08x", DMA_MAX_WATCHDOG));

#if (DPAA_VERSION >= 11)
    if ((p_Fm->partVSPBase + p_Fm->partNumOfVSPs) > FM_VSP_MAX_NUM_OF_ENTRIES)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("partVSPBase+partNumOfVSPs out of range!!!"));
#endif /* (DPAA_VERSION >= 11) */

    if (p_Fm->p_FmStateStruct->totalFifoSize % BMI_FIFO_UNITS)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("totalFifoSize number has to be divisible by %d", BMI_FIFO_UNITS));
    if (!p_Fm->p_FmStateStruct->totalFifoSize ||
        (p_Fm->p_FmStateStruct->totalFifoSize > BMI_MAX_FIFO_SIZE))
        RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                     ("totalFifoSize (currently defined as %d) has to be in the range of 256 to %d",
                      p_Fm->p_FmStateStruct->totalFifoSize,
                      BMI_MAX_FIFO_SIZE));
    if (!p_Fm->p_FmStateStruct->totalNumOfTasks ||
        (p_Fm->p_FmStateStruct->totalNumOfTasks > BMI_MAX_NUM_OF_TASKS))
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("totalNumOfTasks number has to be in the range 1 - %d", BMI_MAX_NUM_OF_TASKS));

#ifdef FM_HAS_TOTAL_DMAS
    if (!p_Fm->p_FmStateStruct->maxNumOfOpenDmas ||
        (p_Fm->p_FmStateStruct->maxNumOfOpenDmas > BMI_MAX_NUM_OF_DMAS))
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("maxNumOfOpenDmas number has to be in the range 1 - %d", BMI_MAX_NUM_OF_DMAS));
#endif /* FM_HAS_TOTAL_DMAS */

    if (p_Fm->p_FmDriverParam->disp_limit_tsh > FPM_MAX_DISP_LIMIT)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("disp_limit_tsh can't be greater than %d", FPM_MAX_DISP_LIMIT));

    if (!p_Fm->f_Exception)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Exceptions callback not provided"));
    if (!p_Fm->f_BusError)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Exceptions callback not provided"));

#ifdef FM_NO_WATCHDOG
    if ((p_Fm->p_FmStateStruct->revInfo.majorRev == 2) &&
        (p_Fm->p_FmDriverParam->dma_watchdog))
        RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("watchdog!"));
#endif /* FM_NO_WATCHDOG */

#ifdef FM_ECC_HALT_NO_SYNC_ERRATA_10GMAC_A008
    if ((p_Fm->p_FmStateStruct->revInfo.majorRev < 6) &&
        (p_Fm->p_FmDriverParam->halt_on_unrecov_ecc_err))
        RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("HaltOnEccError!"));
#endif /* FM_ECC_HALT_NO_SYNC_ERRATA_10GMAC_A008 */

#ifdef FM_NO_TNUM_AGING
    if ((p_Fm->p_FmStateStruct->revInfo.majorRev != 4) &&
        (p_Fm->p_FmStateStruct->revInfo.majorRev < 6))
        if (p_Fm->p_FmDriverParam->tnum_aging_period)
        RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("Tnum aging!"));
#endif /* FM_NO_TNUM_AGING */

    /* check that user did not set revision-dependent exceptions */
#ifdef FM_NO_DISPATCH_RAM_ECC
    if ((p_Fm->p_FmStateStruct->revInfo.majorRev != 4) &&
        (p_Fm->p_FmStateStruct->revInfo.majorRev < 6))
        if (p_Fm->userSetExceptions & FM_EX_BMI_DISPATCH_RAM_ECC)
            RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("exception e_FM_EX_BMI_DISPATCH_RAM_ECC!"));
#endif /* FM_NO_DISPATCH_RAM_ECC */

#ifdef FM_QMI_NO_ECC_EXCEPTIONS
    if (p_Fm->p_FmStateStruct->revInfo.majorRev == 4)
        if (p_Fm->userSetExceptions & (FM_EX_QMI_SINGLE_ECC | FM_EX_QMI_DOUBLE_ECC))
            RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("exception e_FM_EX_QMI_SINGLE_ECC/e_FM_EX_QMI_DOUBLE_ECC!"));
#endif /* FM_QMI_NO_ECC_EXCEPTIONS */

#ifdef FM_QMI_NO_SINGLE_ECC_EXCEPTION
    if (p_Fm->p_FmStateStruct->revInfo.majorRev >= 6)
        if (p_Fm->userSetExceptions & FM_EX_QMI_SINGLE_ECC)
            RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("exception e_FM_EX_QMI_SINGLE_ECC!"));
#endif /* FM_QMI_NO_SINGLE_ECC_EXCEPTION */

    return E_OK;
}


static void SendIpcIsr(t_Fm *p_Fm, uint32_t macEvent, uint32_t pendingReg)
{
    ASSERT_COND(p_Fm->guestId == NCSW_MASTER_ID);

    if (p_Fm->intrMng[macEvent].guestId == NCSW_MASTER_ID)
        p_Fm->intrMng[macEvent].f_Isr(p_Fm->intrMng[macEvent].h_SrcHandle);

    /* If the MAC is running on guest-partition and we have IPC session with it,
       we inform him about the event through IPC; otherwise, we ignore the event. */
    else if (p_Fm->h_IpcSessions[p_Fm->intrMng[macEvent].guestId])
    {
        t_Error     err;
        t_FmIpcIsr  fmIpcIsr;
        t_FmIpcMsg  msg;

        memset(&msg, 0, sizeof(msg));
        msg.msgId = FM_GUEST_ISR;
        fmIpcIsr.pendingReg = pendingReg;
        fmIpcIsr.boolErr = FALSE;
        memcpy(msg.msgBody, &fmIpcIsr, sizeof(fmIpcIsr));
        err = XX_IpcSendMessage(p_Fm->h_IpcSessions[p_Fm->intrMng[macEvent].guestId],
                                (uint8_t*)&msg,
                                sizeof(msg.msgId) + sizeof(fmIpcIsr),
                                NULL,
                                NULL,
                                NULL,
                                NULL);
        if (err != E_OK)
            REPORT_ERROR(MINOR, err, NO_MSG);
    }
    else
        DBG(TRACE, ("FM Guest mode, without IPC - can't call ISR!"));
}

static void BmiErrEvent(t_Fm *p_Fm)
{
    uint32_t    event;
    struct fman_bmi_regs *bmi_rg = p_Fm->p_FmBmiRegs;


    event = fman_get_bmi_err_event(bmi_rg);

    if (event & BMI_ERR_INTR_EN_STORAGE_PROFILE_ECC)
        p_Fm->f_Exception(p_Fm->h_App,e_FM_EX_BMI_STORAGE_PROFILE_ECC);
    if (event & BMI_ERR_INTR_EN_LIST_RAM_ECC)
        p_Fm->f_Exception(p_Fm->h_App,e_FM_EX_BMI_LIST_RAM_ECC);
    if (event & BMI_ERR_INTR_EN_STATISTICS_RAM_ECC)
        p_Fm->f_Exception(p_Fm->h_App,e_FM_EX_BMI_STATISTICS_RAM_ECC);
    if (event & BMI_ERR_INTR_EN_DISPATCH_RAM_ECC)
        p_Fm->f_Exception(p_Fm->h_App,e_FM_EX_BMI_DISPATCH_RAM_ECC);
}

static void    QmiErrEvent(t_Fm *p_Fm)
{
    uint32_t    event;
    struct fman_qmi_regs *qmi_rg = p_Fm->p_FmQmiRegs;

    event = fman_get_qmi_err_event(qmi_rg);

    if (event & QMI_ERR_INTR_EN_DOUBLE_ECC)
        p_Fm->f_Exception(p_Fm->h_App,e_FM_EX_QMI_DOUBLE_ECC);
    if (event & QMI_ERR_INTR_EN_DEQ_FROM_DEF)
        p_Fm->f_Exception(p_Fm->h_App,e_FM_EX_QMI_DEQ_FROM_UNKNOWN_PORTID);
}

static void    DmaErrEvent(t_Fm *p_Fm)
{
    uint32_t            status, com_id;
    uint8_t             tnum;
    uint8_t             hardwarePortId;
    uint8_t             relativePortId;
    uint16_t            liodn;
    struct fman_dma_regs *dma_rg = p_Fm->p_FmDmaRegs;

    status = fman_get_dma_err_event(dma_rg);

    if (status & DMA_STATUS_BUS_ERR)
    {
        com_id = fman_get_dma_com_id(dma_rg);
        hardwarePortId = (uint8_t)(((com_id & DMA_TRANSFER_PORTID_MASK) >> DMA_TRANSFER_PORTID_SHIFT));
        ASSERT_COND(IN_RANGE(1, hardwarePortId, 63));
        HW_PORT_ID_TO_SW_PORT_ID(relativePortId, hardwarePortId);
        tnum = (uint8_t)((com_id & DMA_TRANSFER_TNUM_MASK) >> DMA_TRANSFER_TNUM_SHIFT);
        liodn = (uint16_t)(com_id & DMA_TRANSFER_LIODN_MASK);
        ASSERT_COND(p_Fm->p_FmStateStruct->portsTypes[hardwarePortId] != e_FM_PORT_TYPE_DUMMY);
        p_Fm->f_BusError(p_Fm->h_App,
                         p_Fm->p_FmStateStruct->portsTypes[hardwarePortId],
                         relativePortId,
                         fman_get_dma_addr(dma_rg),
                         tnum,
                         liodn);
    }
        if (status & DMA_STATUS_FM_SPDAT_ECC)
            p_Fm->f_Exception(p_Fm->h_App, e_FM_EX_DMA_SINGLE_PORT_ECC);
        if (status & DMA_STATUS_READ_ECC)
            p_Fm->f_Exception(p_Fm->h_App, e_FM_EX_DMA_READ_ECC);
        if (status & DMA_STATUS_SYSTEM_WRITE_ECC)
            p_Fm->f_Exception(p_Fm->h_App, e_FM_EX_DMA_SYSTEM_WRITE_ECC);
        if (status & DMA_STATUS_FM_WRITE_ECC)
            p_Fm->f_Exception(p_Fm->h_App, e_FM_EX_DMA_FM_WRITE_ECC);
    }

static void    FpmErrEvent(t_Fm *p_Fm)
{
    uint32_t    event;
    struct fman_fpm_regs *fpm_rg = p_Fm->p_FmFpmRegs;

    event = fman_get_fpm_err_event(fpm_rg);

    if ((event  & FPM_EV_MASK_DOUBLE_ECC) && (event & FPM_EV_MASK_DOUBLE_ECC_EN))
        p_Fm->f_Exception(p_Fm->h_App,e_FM_EX_FPM_DOUBLE_ECC);
    if ((event  & FPM_EV_MASK_STALL) && (event & FPM_EV_MASK_STALL_EN))
        p_Fm->f_Exception(p_Fm->h_App,e_FM_EX_FPM_STALL_ON_TASKS);
    if ((event  & FPM_EV_MASK_SINGLE_ECC) && (event & FPM_EV_MASK_SINGLE_ECC_EN))
        p_Fm->f_Exception(p_Fm->h_App,e_FM_EX_FPM_SINGLE_ECC);
}

static void    MuramErrIntr(t_Fm *p_Fm)
{
    uint32_t    event;
    struct fman_fpm_regs *fpm_rg = p_Fm->p_FmFpmRegs;

    event = fman_get_muram_err_event(fpm_rg);

    if (event & FPM_RAM_MURAM_ECC)
        p_Fm->f_Exception(p_Fm->h_App, e_FM_EX_MURAM_ECC);
}

static void IramErrIntr(t_Fm *p_Fm)
{
    uint32_t    event;
    struct fman_fpm_regs *fpm_rg = p_Fm->p_FmFpmRegs;

    event = fman_get_iram_err_event(fpm_rg);

    if (event & FPM_RAM_IRAM_ECC)
        p_Fm->f_Exception(p_Fm->h_App, e_FM_EX_IRAM_ECC);
}

static void QmiEvent(t_Fm *p_Fm)
{
    uint32_t    event;
    struct fman_qmi_regs *qmi_rg = p_Fm->p_FmQmiRegs;

    event = fman_get_qmi_event(qmi_rg);

    if (event & QMI_INTR_EN_SINGLE_ECC)
        p_Fm->f_Exception(p_Fm->h_App,e_FM_EX_QMI_SINGLE_ECC);
}

static void UnimplementedIsr(t_Handle h_Arg)
{
    UNUSED(h_Arg);

    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Unimplemented ISR!"));
}

static void UnimplementedFmanCtrlIsr(t_Handle h_Arg, uint32_t event)
{
    UNUSED(h_Arg); UNUSED(event);

    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Unimplemented FmCtl ISR!"));
}

static void EnableTimeStamp(t_Fm *p_Fm)
{
    struct fman_fpm_regs *fpm_rg = p_Fm->p_FmFpmRegs;

    ASSERT_COND(p_Fm->p_FmStateStruct);
    ASSERT_COND(p_Fm->p_FmStateStruct->count1MicroBit);

    fman_enable_time_stamp(fpm_rg, p_Fm->p_FmStateStruct->count1MicroBit, p_Fm->p_FmStateStruct->fmClkFreq);

    p_Fm->p_FmStateStruct->enabledTimeStamp = TRUE;
}

static t_Error ClearIRam(t_Fm *p_Fm)
{
    t_FMIramRegs    *p_Iram;
    int             i;
    int             iram_size;

    ASSERT_COND(p_Fm);
    p_Iram = (t_FMIramRegs *)UINT_TO_PTR(p_Fm->baseAddr + FM_MM_IMEM);
    iram_size = FM_IRAM_SIZE(p_Fm->p_FmStateStruct->revInfo.majorRev,p_Fm->p_FmStateStruct->revInfo.minorRev);

    /* Enable the auto-increment */
    WRITE_UINT32(p_Iram->iadd, IRAM_IADD_AIE);
    while (GET_UINT32(p_Iram->iadd) != IRAM_IADD_AIE) ;

    for (i=0; i < (iram_size/4); i++)
        WRITE_UINT32(p_Iram->idata, 0xffffffff);

    WRITE_UINT32(p_Iram->iadd, iram_size - 4);
    CORE_MemoryBarrier();
    while (GET_UINT32(p_Iram->idata) != 0xffffffff) ;

    return E_OK;
}

static t_Error LoadFmanCtrlCode(t_Fm *p_Fm)
{
    t_FMIramRegs    *p_Iram;
    int             i;
    uint32_t        tmp;
    uint8_t         compTo16;

    ASSERT_COND(p_Fm);
    p_Iram = (t_FMIramRegs *)UINT_TO_PTR(p_Fm->baseAddr + FM_MM_IMEM);

    /* Enable the auto-increment */
    WRITE_UINT32(p_Iram->iadd, IRAM_IADD_AIE);
    while (GET_UINT32(p_Iram->iadd) != IRAM_IADD_AIE) ;

    for (i=0; i < (p_Fm->firmware.size / 4); i++)
        WRITE_UINT32(p_Iram->idata, p_Fm->firmware.p_Code[i]);

    compTo16 = (uint8_t)(p_Fm->firmware.size % 16);
    if (compTo16)
        for (i=0; i < ((16-compTo16) / 4); i++)
            WRITE_UINT32(p_Iram->idata, 0xffffffff);

    WRITE_UINT32(p_Iram->iadd,p_Fm->firmware.size-4);
    while (GET_UINT32(p_Iram->iadd) != (p_Fm->firmware.size-4)) ;

    /* verify that writing has completed */
    while (GET_UINT32(p_Iram->idata) != p_Fm->firmware.p_Code[(p_Fm->firmware.size / 4)-1]) ;

    if (p_Fm->fwVerify)
    {
        WRITE_UINT32(p_Iram->iadd, IRAM_IADD_AIE);
        while (GET_UINT32(p_Iram->iadd) != IRAM_IADD_AIE) ;
        for (i=0; i < (p_Fm->firmware.size / 4); i++)
        {
            tmp = GET_UINT32(p_Iram->idata);
            if (tmp != p_Fm->firmware.p_Code[i])
                RETURN_ERROR(MAJOR, E_WRITE_FAILED,
                             ("UCode write error : write 0x%x, read 0x%x",
                              p_Fm->firmware.p_Code[i],tmp));
        }
        WRITE_UINT32(p_Iram->iadd, 0x0);
    }

    /* Enable patch from IRAM */
    WRITE_UINT32(p_Iram->iready, IRAM_READY);
    XX_UDelay(1000);

    DBG(INFO, ("FMan-Controller code (ver %d.%d.%d) loaded to IRAM.",
               ((uint16_t *)p_Fm->firmware.p_Code)[2],
               ((uint8_t *)p_Fm->firmware.p_Code)[6],
               ((uint8_t *)p_Fm->firmware.p_Code)[7]));

    return E_OK;
}

#ifdef FM_UCODE_NOT_RESET_ERRATA_BUGZILLA6173
static t_Error FwNotResetErratumBugzilla6173WA(t_Fm *p_Fm)
{
    t_FMIramRegs    *p_Iram = (t_FMIramRegs *)UINT_TO_PTR(p_Fm->baseAddr + FM_MM_IMEM);
    uint32_t        tmpReg;
    uint32_t        savedSpliodn[63];

    /* write to IRAM first location the debug instruction */
    WRITE_UINT32(p_Iram->iadd, 0);
    while (GET_UINT32(p_Iram->iadd) != 0) ;
    WRITE_UINT32(p_Iram->idata, FM_FW_DEBUG_INSTRUCTION);

    WRITE_UINT32(p_Iram->iadd, 0);
    while (GET_UINT32(p_Iram->iadd) != 0) ;
    while (GET_UINT32(p_Iram->idata) != FM_FW_DEBUG_INSTRUCTION) ;

    /* Enable patch from IRAM */
    WRITE_UINT32(p_Iram->iready, IRAM_READY);
    CORE_MemoryBarrier();
    XX_UDelay(100);
    IO2MemCpy32((uint8_t *)savedSpliodn,
                (uint8_t *)p_Fm->p_FmBmiRegs->fmbm_spliodn,
                63*sizeof(uint32_t));

    /* reset FMAN */
    WRITE_UINT32(p_Fm->p_FmFpmRegs->fm_rstc, FPM_RSTC_FM_RESET);
    CORE_MemoryBarrier();
    XX_UDelay(100);

    /* verify breakpoint debug status register */
    tmpReg = GET_UINT32(*(uint32_t *)UINT_TO_PTR(p_Fm->baseAddr + FM_DEBUG_STATUS_REGISTER_OFFSET));
    if (!tmpReg)
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("Invalid debug status register value is '0'"));

    /*************************************/
    /* Load FMan-Controller code to IRAM */
    /*************************************/
    ClearIRam(p_Fm);
    if (p_Fm->firmware.p_Code &&
        (LoadFmanCtrlCode(p_Fm) != E_OK))
        RETURN_ERROR(MAJOR, E_INVALID_STATE, NO_MSG);
    XX_UDelay(100);

    /* reset FMAN again to start the microcode */
    WRITE_UINT32(p_Fm->p_FmFpmRegs->fm_rstc, FPM_RSTC_FM_RESET);
    CORE_MemoryBarrier();
    XX_UDelay(100);
    Mem2IOCpy32((uint8_t *)p_Fm->p_FmBmiRegs->fmbm_spliodn,
                (uint8_t *)savedSpliodn,
                63*sizeof(uint32_t));

    if (fman_is_qmi_halt_not_busy_state(p_Fm->p_FmQmiRegs))
    {
        fman_resume(p_Fm->p_FmFpmRegs);
        CORE_MemoryBarrier();
        XX_UDelay(100);
    }

    return E_OK;
}
#endif /* FM_UCODE_NOT_RESET_ERRATA_BUGZILLA6173 */

static void GuestErrorIsr(t_Fm *p_Fm, uint32_t pending)
{
#define FM_G_CALL_1G_MAC_ERR_ISR(_id)   \
do {                                    \
    p_Fm->intrMng[(e_FmInterModuleEvent)(e_FM_EV_ERR_1G_MAC0+_id)].f_Isr(p_Fm->intrMng[(e_FmInterModuleEvent)(e_FM_EV_ERR_1G_MAC0+_id)].h_SrcHandle);\
} while (0)
#define FM_G_CALL_10G_MAC_ERR_ISR(_id)  \
do {                                    \
    p_Fm->intrMng[(e_FmInterModuleEvent)(e_FM_EV_ERR_10G_MAC0+_id)].f_Isr(p_Fm->intrMng[(e_FmInterModuleEvent)(e_FM_EV_ERR_10G_MAC0+_id)].h_SrcHandle);\
} while (0)

    /* error interrupts */
    if (pending & ERR_INTR_EN_1G_MAC0)
        FM_G_CALL_1G_MAC_ERR_ISR(0);
    if (pending & ERR_INTR_EN_1G_MAC1)
        FM_G_CALL_1G_MAC_ERR_ISR(1);
    if (pending & ERR_INTR_EN_1G_MAC2)
        FM_G_CALL_1G_MAC_ERR_ISR(2);
    if (pending & ERR_INTR_EN_1G_MAC3)
        FM_G_CALL_1G_MAC_ERR_ISR(3);
    if (pending & ERR_INTR_EN_1G_MAC4)
        FM_G_CALL_1G_MAC_ERR_ISR(4);
    if (pending & ERR_INTR_EN_1G_MAC5)
        FM_G_CALL_1G_MAC_ERR_ISR(5);
    if (pending & ERR_INTR_EN_1G_MAC6)
        FM_G_CALL_1G_MAC_ERR_ISR(6);
    if (pending & ERR_INTR_EN_1G_MAC7)
        FM_G_CALL_1G_MAC_ERR_ISR(7);
    if (pending & ERR_INTR_EN_10G_MAC0)
        FM_G_CALL_10G_MAC_ERR_ISR(0);
    if (pending & ERR_INTR_EN_10G_MAC1)
        FM_G_CALL_10G_MAC_ERR_ISR(1);
}

static void GuestEventIsr(t_Fm *p_Fm, uint32_t pending)
{
#define FM_G_CALL_1G_MAC_ISR(_id)   \
do {                                    \
    p_Fm->intrMng[(e_FmInterModuleEvent)(e_FM_EV_1G_MAC0+_id)].f_Isr(p_Fm->intrMng[(e_FmInterModuleEvent)(e_FM_EV_1G_MAC0+_id)].h_SrcHandle);\
} while (0)
#define FM_G_CALL_10G_MAC_ISR(_id)   \
do {                                    \
    p_Fm->intrMng[(e_FmInterModuleEvent)(e_FM_EV_10G_MAC0+_id)].f_Isr(p_Fm->intrMng[(e_FmInterModuleEvent)(e_FM_EV_10G_MAC0+_id)].h_SrcHandle);\
} while (0)

    if (pending & INTR_EN_1G_MAC0)
        FM_G_CALL_1G_MAC_ISR(0);
    if (pending & INTR_EN_1G_MAC1)
        FM_G_CALL_1G_MAC_ISR(1);
    if (pending & INTR_EN_1G_MAC2)
        FM_G_CALL_1G_MAC_ISR(2);
    if (pending & INTR_EN_1G_MAC3)
        FM_G_CALL_1G_MAC_ISR(3);
    if (pending & INTR_EN_1G_MAC4)
        FM_G_CALL_1G_MAC_ISR(4);
    if (pending & INTR_EN_1G_MAC5)
        FM_G_CALL_1G_MAC_ISR(5);
    if (pending & INTR_EN_1G_MAC6)
        FM_G_CALL_1G_MAC_ISR(6);
    if (pending & INTR_EN_1G_MAC7)
        FM_G_CALL_1G_MAC_ISR(7);
    if (pending & INTR_EN_10G_MAC0)
        FM_G_CALL_10G_MAC_ISR(0);
    if (pending & INTR_EN_10G_MAC1)
        FM_G_CALL_10G_MAC_ISR(1);
    if (pending & INTR_EN_TMR)
        p_Fm->intrMng[e_FM_EV_TMR].f_Isr(p_Fm->intrMng[e_FM_EV_TMR].h_SrcHandle);
}

#if (DPAA_VERSION >= 11)
static t_Error SetVSPWindow(t_Handle  h_Fm,
                            uint8_t   hardwarePortId,
                            uint8_t   baseStorageProfile,
                            uint8_t   log2NumOfProfiles)
{
    t_Fm                    *p_Fm = (t_Fm *)h_Fm;

    ASSERT_COND(h_Fm);
    ASSERT_COND(IN_RANGE(1, hardwarePortId, 63));

    if ((p_Fm->guestId != NCSW_MASTER_ID) &&
        !p_Fm->p_FmBmiRegs &&
        p_Fm->h_IpcSessions[0])
    {
        t_FmIpcVspSetPortWindow fmIpcVspSetPortWindow;
        t_FmIpcMsg              msg;
        t_Error                 err = E_OK;

        memset(&msg, 0, sizeof(msg));
        memset(&fmIpcVspSetPortWindow, 0, sizeof(t_FmIpcVspSetPortWindow));
        fmIpcVspSetPortWindow.hardwarePortId      = hardwarePortId;
        fmIpcVspSetPortWindow.baseStorageProfile  = baseStorageProfile;
        fmIpcVspSetPortWindow.log2NumOfProfiles   = log2NumOfProfiles;
        msg.msgId                                 = FM_VSP_SET_PORT_WINDOW;
        memcpy(msg.msgBody, &fmIpcVspSetPortWindow, sizeof(t_FmIpcVspSetPortWindow));

        err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                (uint8_t*)&msg,
                                sizeof(msg.msgId),
                                NULL,
                                NULL,
                                NULL,
                                NULL);
        if (err != E_OK)
            RETURN_ERROR(MINOR, err, NO_MSG);
        return E_OK;
    }
    else if (!p_Fm->p_FmBmiRegs)
        RETURN_ERROR(MINOR, E_NOT_SUPPORTED,
                     ("Either IPC or 'baseAddress' is required!"));

    fman_set_vsp_window(p_Fm->p_FmBmiRegs,
                        hardwarePortId,
                        baseStorageProfile,
                        log2NumOfProfiles);

    return E_OK;
}

static uint8_t AllocVSPsForPartition(t_Handle  h_Fm, uint8_t base, uint8_t numOfProfiles, uint8_t guestId)
{
    t_Fm        *p_Fm = (t_Fm *)h_Fm;
    uint8_t     profilesFound = 0;
    int         i = 0;
    uint32_t    intFlags;

    if (!numOfProfiles)
        return E_OK;

    if ((numOfProfiles > FM_VSP_MAX_NUM_OF_ENTRIES) ||
        (base + numOfProfiles > FM_VSP_MAX_NUM_OF_ENTRIES))
        return (uint8_t)ILLEGAL_BASE;

    if (p_Fm->h_IpcSessions[0])
    {
        t_FmIpcResourceAllocParams  ipcAllocParams;
        t_FmIpcMsg                  msg;
        t_FmIpcReply                reply;
        t_Error                     err;
        uint32_t                    replyLength;

        memset(&msg, 0, sizeof(msg));
        memset(&reply, 0, sizeof(reply));
        memset(&ipcAllocParams, 0, sizeof(t_FmIpcResourceAllocParams));
        ipcAllocParams.guestId         = p_Fm->guestId;
        ipcAllocParams.num             = p_Fm->partNumOfVSPs;
        ipcAllocParams.base            = p_Fm->partVSPBase;
        msg.msgId                              = FM_VSP_ALLOC;
        memcpy(msg.msgBody, &ipcAllocParams, sizeof(t_FmIpcResourceAllocParams));
        replyLength = sizeof(uint32_t) + sizeof(uint8_t);
        err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                (uint8_t*)&msg,
                                sizeof(msg.msgId) + sizeof(t_FmIpcResourceAllocParams),
                                (uint8_t*)&reply,
                                &replyLength,
                                NULL,
                                NULL);
        if ((err != E_OK) ||
            (replyLength != (sizeof(uint32_t) + sizeof(uint8_t))))
            RETURN_ERROR(MAJOR, err, NO_MSG);
        else
            memcpy((uint8_t*)&p_Fm->partVSPBase, reply.replyBody, sizeof(uint8_t));
        if (p_Fm->partVSPBase == (uint8_t)(ILLEGAL_BASE))
            RETURN_ERROR(MAJOR, err, NO_MSG);
    }
    if (p_Fm->guestId != NCSW_MASTER_ID)
    {
        DBG(WARNING, ("FM Guest mode, without IPC - can't validate VSP range!"));
        return (uint8_t)ILLEGAL_BASE;
    }

    intFlags = XX_LockIntrSpinlock(p_Fm->h_Spinlock);
    for (i = base; i < base + numOfProfiles; i++)
        if (p_Fm->p_FmSp->profiles[i].profilesMng.ownerId == (uint8_t)ILLEGAL_BASE)
            profilesFound++;
        else
            break;

    if (profilesFound == numOfProfiles)
        for (i = base; i<base + numOfProfiles; i++)
            p_Fm->p_FmSp->profiles[i].profilesMng.ownerId = guestId;
    else
    {
        XX_UnlockIntrSpinlock(p_Fm->h_Spinlock, intFlags);
        return (uint8_t)ILLEGAL_BASE;
    }
    XX_UnlockIntrSpinlock(p_Fm->h_Spinlock, intFlags);

    return base;
}

static void FreeVSPsForPartition(t_Handle  h_Fm, uint8_t base, uint8_t numOfProfiles, uint8_t guestId)
{
    t_Fm    *p_Fm = (t_Fm *)h_Fm;
    int     i = 0;

    ASSERT_COND(p_Fm);

    if (p_Fm->h_IpcSessions[0])
    {
        t_FmIpcResourceAllocParams  ipcAllocParams;
        t_FmIpcMsg                  msg;
        t_FmIpcReply                reply;
        uint32_t                    replyLength;
        t_Error                     err;

        memset(&msg, 0, sizeof(msg));
        memset(&reply, 0, sizeof(reply));
        memset(&ipcAllocParams, 0, sizeof(t_FmIpcResourceAllocParams));
        ipcAllocParams.guestId         = p_Fm->guestId;
        ipcAllocParams.num             = p_Fm->partNumOfVSPs;
        ipcAllocParams.base            = p_Fm->partVSPBase;
        msg.msgId                              = FM_VSP_FREE;
        memcpy(msg.msgBody, &ipcAllocParams, sizeof(t_FmIpcResourceAllocParams));
        replyLength = sizeof(uint32_t) + sizeof(uint8_t);
        err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                (uint8_t*)&msg,
                                sizeof(msg.msgId) + sizeof(t_FmIpcResourceAllocParams),
                                (uint8_t*)&reply,
                                &replyLength,
                                NULL,
                                NULL);
        if (err != E_OK)
            REPORT_ERROR(MAJOR, err, NO_MSG);
        return;
    }
    if (p_Fm->guestId != NCSW_MASTER_ID)
    {
        DBG(WARNING, ("FM Guest mode, without IPC - can't validate VSP range!"));
        return;
    }

    ASSERT_COND(p_Fm->p_FmSp);

    for (i=base; i<numOfProfiles; i++)
    {
        if (p_Fm->p_FmSp->profiles[i].profilesMng.ownerId == guestId)
           p_Fm->p_FmSp->profiles[i].profilesMng.ownerId = (uint8_t)ILLEGAL_BASE;
        else
            DBG(WARNING, ("Request for freeing storage profile window which wasn't allocated to this partition"));
    }
}
#endif /* (DPAA_VERSION >= 11) */

static t_Error FmGuestHandleIpcMsgCB(t_Handle  h_Fm,
                                     uint8_t   *p_Msg,
                                     uint32_t  msgLength,
                                     uint8_t   *p_Reply,
                                     uint32_t  *p_ReplyLength)
{
    t_Fm            *p_Fm       = (t_Fm*)h_Fm;
    t_FmIpcMsg      *p_IpcMsg   = (t_FmIpcMsg*)p_Msg;

    UNUSED(p_Reply);
    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((msgLength > sizeof(uint32_t)), E_INVALID_VALUE);

#ifdef DISABLE_SANITY_CHECKS
    UNUSED(msgLength);
#endif /* DISABLE_SANITY_CHECKS */

    ASSERT_COND(p_Msg);

    *p_ReplyLength = 0;

    switch (p_IpcMsg->msgId)
    {
        case (FM_GUEST_ISR):
        {
            t_FmIpcIsr ipcIsr;

            memcpy((uint8_t*)&ipcIsr, p_IpcMsg->msgBody, sizeof(t_FmIpcIsr));
            if (ipcIsr.boolErr)
                GuestErrorIsr(p_Fm, ipcIsr.pendingReg);
            else
                GuestEventIsr(p_Fm, ipcIsr.pendingReg);
            break;
        }
        default:
            *p_ReplyLength = 0;
            RETURN_ERROR(MINOR, E_INVALID_SELECTION, ("command not found!!!"));
    }
    return E_OK;
}

static t_Error FmHandleIpcMsgCB(t_Handle  h_Fm,
                                uint8_t   *p_Msg,
                                uint32_t  msgLength,
                                uint8_t   *p_Reply,
                                uint32_t  *p_ReplyLength)
{
    t_Error         err;
    t_Fm            *p_Fm       = (t_Fm*)h_Fm;
    t_FmIpcMsg      *p_IpcMsg   = (t_FmIpcMsg*)p_Msg;
    t_FmIpcReply    *p_IpcReply = (t_FmIpcReply*)p_Reply;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((msgLength >= sizeof(uint32_t)), E_INVALID_VALUE);

#ifdef DISABLE_SANITY_CHECKS
    UNUSED(msgLength);
#endif /* DISABLE_SANITY_CHECKS */

    ASSERT_COND(p_IpcMsg);

    memset(p_IpcReply, 0, (sizeof(uint8_t) * FM_IPC_MAX_REPLY_SIZE));
    *p_ReplyLength = 0;

    switch (p_IpcMsg->msgId)
    {
        case (FM_GET_SET_PORT_PARAMS):
        {
            t_FmIpcPortInInitParams         ipcInitParams;
            t_FmInterModulePortInitParams   initParams;
            t_FmIpcPortOutInitParams        ipcOutInitParams;

            memcpy((uint8_t*)&ipcInitParams, p_IpcMsg->msgBody, sizeof(t_FmIpcPortInInitParams));
            initParams.hardwarePortId = ipcInitParams.hardwarePortId;
            initParams.portType = (e_FmPortType)ipcInitParams.enumPortType;
            initParams.independentMode = (bool)(ipcInitParams.boolIndependentMode);
            initParams.liodnOffset = ipcInitParams.liodnOffset;
            initParams.numOfTasks = ipcInitParams.numOfTasks;
            initParams.numOfExtraTasks = ipcInitParams.numOfExtraTasks;
            initParams.numOfOpenDmas = ipcInitParams.numOfOpenDmas;
            initParams.numOfExtraOpenDmas = ipcInitParams.numOfExtraOpenDmas;
            initParams.sizeOfFifo = ipcInitParams.sizeOfFifo;
            initParams.extraSizeOfFifo = ipcInitParams.extraSizeOfFifo;
            initParams.deqPipelineDepth = ipcInitParams.deqPipelineDepth;
            initParams.maxFrameLength = ipcInitParams.maxFrameLength;
            initParams.liodnBase = ipcInitParams.liodnBase;

            p_IpcReply->error = (uint32_t)FmGetSetPortParams(h_Fm, &initParams);

            ipcOutInitParams.ipcPhysAddr.high = initParams.fmMuramPhysBaseAddr.high;
            ipcOutInitParams.ipcPhysAddr.low = initParams.fmMuramPhysBaseAddr.low;
            ipcOutInitParams.sizeOfFifo = initParams.sizeOfFifo;
            ipcOutInitParams.extraSizeOfFifo = initParams.extraSizeOfFifo;
            ipcOutInitParams.numOfTasks = initParams.numOfTasks;
            ipcOutInitParams.numOfExtraTasks = initParams.numOfExtraTasks;
            ipcOutInitParams.numOfOpenDmas = initParams.numOfOpenDmas;
            ipcOutInitParams.numOfExtraOpenDmas = initParams.numOfExtraOpenDmas;
            memcpy(p_IpcReply->replyBody, (uint8_t*)&ipcOutInitParams, sizeof(ipcOutInitParams));
            *p_ReplyLength = sizeof(uint32_t) + sizeof(t_FmIpcPortOutInitParams);
            break;
        }
        case (FM_SET_SIZE_OF_FIFO):
        {
            t_FmIpcPortRsrcParams   ipcPortRsrcParams;

            memcpy((uint8_t*)&ipcPortRsrcParams, p_IpcMsg->msgBody, sizeof(t_FmIpcPortRsrcParams));
            p_IpcReply->error = (uint32_t)FmSetSizeOfFifo(h_Fm,
                                                          ipcPortRsrcParams.hardwarePortId,
                                                          &ipcPortRsrcParams.val,
                                                          &ipcPortRsrcParams.extra,
                                                          (bool)ipcPortRsrcParams.boolInitialConfig);
            *p_ReplyLength = sizeof(uint32_t);
            break;
        }
        case (FM_SET_NUM_OF_TASKS):
        {
            t_FmIpcPortRsrcParams   ipcPortRsrcParams;

            memcpy((uint8_t*)&ipcPortRsrcParams, p_IpcMsg->msgBody, sizeof(t_FmIpcPortRsrcParams));
            p_IpcReply->error = (uint32_t)FmSetNumOfTasks(h_Fm, ipcPortRsrcParams.hardwarePortId,
                                                          (uint8_t*)&ipcPortRsrcParams.val,
                                                          (uint8_t*)&ipcPortRsrcParams.extra,
                                                          (bool)ipcPortRsrcParams.boolInitialConfig);
            *p_ReplyLength = sizeof(uint32_t);
            break;
        }
        case (FM_SET_NUM_OF_OPEN_DMAS):
        {
            t_FmIpcPortRsrcParams   ipcPortRsrcParams;

            memcpy((uint8_t*)&ipcPortRsrcParams, p_IpcMsg->msgBody, sizeof(t_FmIpcPortRsrcParams));
            p_IpcReply->error = (uint32_t)FmSetNumOfOpenDmas(h_Fm, ipcPortRsrcParams.hardwarePortId,
                                                               (uint8_t*)&ipcPortRsrcParams.val,
                                                               (uint8_t*)&ipcPortRsrcParams.extra,
                                                               (bool)ipcPortRsrcParams.boolInitialConfig);
            *p_ReplyLength = sizeof(uint32_t);
            break;
        }
        case (FM_RESUME_STALLED_PORT):
            *p_ReplyLength = sizeof(uint32_t);
            p_IpcReply->error = (uint32_t)FmResumeStalledPort(h_Fm, p_IpcMsg->msgBody[0]);
            break;
        case (FM_MASTER_IS_ALIVE):
        {
            uint8_t guestId = p_IpcMsg->msgBody[0];
            /* build the FM master partition IPC address */
            memset(p_Fm->fmIpcHandlerModuleName[guestId], 0, (sizeof(char)) * MODULE_NAME_SIZE);
            if (Sprint (p_Fm->fmIpcHandlerModuleName[guestId], "FM_%d_%d",p_Fm->p_FmStateStruct->fmId, guestId) != (guestId<10 ? 6:7))
                RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Sprint failed"));
            p_Fm->h_IpcSessions[guestId] = XX_IpcInitSession(p_Fm->fmIpcHandlerModuleName[guestId], p_Fm->fmModuleName);
            if (p_Fm->h_IpcSessions[guestId] == NULL)
                RETURN_ERROR(MAJOR, E_NOT_AVAILABLE, ("FM Master IPC session for guest %d", guestId));
            *(uint8_t*)(p_IpcReply->replyBody) = 1;
            *p_ReplyLength = sizeof(uint32_t) + sizeof(uint8_t);
            break;
        }
        case (FM_IS_PORT_STALLED):
        {
            bool tmp;

            p_IpcReply->error = (uint32_t)FmIsPortStalled(h_Fm, p_IpcMsg->msgBody[0], &tmp);
            *(uint8_t*)(p_IpcReply->replyBody) = (uint8_t)tmp;
            *p_ReplyLength = sizeof(uint32_t) + sizeof(uint8_t);
            break;
        }
        case (FM_RESET_MAC):
        {
            t_FmIpcMacParams    ipcMacParams;

            memcpy((uint8_t*)&ipcMacParams, p_IpcMsg->msgBody, sizeof(t_FmIpcMacParams));
            p_IpcReply->error = (uint32_t)FmResetMac(p_Fm,
                                                     (e_FmMacType)(ipcMacParams.enumType),
                                                     ipcMacParams.id);
            *p_ReplyLength = sizeof(uint32_t);
            break;
        }
        case (FM_SET_MAC_MAX_FRAME):
        {
            t_FmIpcMacMaxFrameParams    ipcMacMaxFrameParams;

            memcpy((uint8_t*)&ipcMacMaxFrameParams, p_IpcMsg->msgBody, sizeof(t_FmIpcMacMaxFrameParams));
            err = FmSetMacMaxFrame(p_Fm,
                                  (e_FmMacType)(ipcMacMaxFrameParams.macParams.enumType),
                                  ipcMacMaxFrameParams.macParams.id,
                                  ipcMacMaxFrameParams.maxFrameLength);
            if (err != E_OK)
                REPORT_ERROR(MINOR, err, NO_MSG);
            break;
        }
#if (DPAA_VERSION >= 11)
        case (FM_VSP_ALLOC) :
        {
            t_FmIpcResourceAllocParams  ipcAllocParams;
            uint8_t                     vspBase;
            memcpy(&ipcAllocParams, p_IpcMsg->msgBody, sizeof(t_FmIpcResourceAllocParams));
            vspBase =  AllocVSPsForPartition(h_Fm, (uint8_t)ipcAllocParams.base, (uint8_t)ipcAllocParams.num, ipcAllocParams.guestId);
            memcpy(p_IpcReply->replyBody, (uint8_t*)&vspBase, sizeof(uint8_t));
            *p_ReplyLength = sizeof(uint32_t) + sizeof(uint8_t);
            break;
        }
        case (FM_VSP_FREE) :
        {
            t_FmIpcResourceAllocParams   ipcAllocParams;
            memcpy(&ipcAllocParams, p_IpcMsg->msgBody, sizeof(t_FmIpcResourceAllocParams));
            FreeVSPsForPartition(h_Fm, (uint8_t)ipcAllocParams.base, (uint8_t)ipcAllocParams.num, ipcAllocParams.guestId);
            break;
        }
        case (FM_VSP_SET_PORT_WINDOW) :
        {
            t_FmIpcVspSetPortWindow   ipcVspSetPortWindow;
            memcpy(&ipcVspSetPortWindow, p_IpcMsg->msgBody, sizeof(t_FmIpcVspSetPortWindow));
            err = SetVSPWindow(h_Fm,
                                            ipcVspSetPortWindow.hardwarePortId,
                                            ipcVspSetPortWindow.baseStorageProfile,
                                            ipcVspSetPortWindow.log2NumOfProfiles);
            return err;
        }
        case (FM_SET_CONG_GRP_PFC_PRIO) :
        {
            t_FmIpcSetCongestionGroupPfcPriority    fmIpcSetCongestionGroupPfcPriority;
            memcpy(&fmIpcSetCongestionGroupPfcPriority, p_IpcMsg->msgBody, sizeof(t_FmIpcSetCongestionGroupPfcPriority));
            err = FmSetCongestionGroupPFCpriority(h_Fm,
                                                  fmIpcSetCongestionGroupPfcPriority.congestionGroupId,
                                                  fmIpcSetCongestionGroupPfcPriority.priorityBitMap);
            return err;
        }
#endif /* (DPAA_VERSION >= 11) */

        case (FM_FREE_PORT):
        {
            t_FmInterModulePortFreeParams   portParams;
            t_FmIpcPortFreeParams           ipcPortParams;

            memcpy((uint8_t*)&ipcPortParams, p_IpcMsg->msgBody, sizeof(t_FmIpcPortFreeParams));
            portParams.hardwarePortId = ipcPortParams.hardwarePortId;
            portParams.portType = (e_FmPortType)(ipcPortParams.enumPortType);
            portParams.deqPipelineDepth = ipcPortParams.deqPipelineDepth;
            FmFreePortParams(h_Fm, &portParams);
            break;
        }
        case (FM_REGISTER_INTR):
        {
            t_FmIpcRegisterIntr ipcRegIntr;

            memcpy((uint8_t*)&ipcRegIntr, p_IpcMsg->msgBody, sizeof(ipcRegIntr));
            p_Fm->intrMng[ipcRegIntr.event].guestId = ipcRegIntr.guestId;
            break;
        }
        case (FM_GET_PARAMS):
        {
             t_FmIpcParams  ipcParams;

            /* Get clock frequency */
            ipcParams.fmClkFreq = p_Fm->p_FmStateStruct->fmClkFreq;
            ipcParams.fmMacClkFreq = p_Fm->p_FmStateStruct->fmMacClkFreq;

            fman_get_revision(p_Fm->p_FmFpmRegs,&ipcParams.majorRev,&ipcParams.minorRev);

            memcpy(p_IpcReply->replyBody, (uint8_t*)&ipcParams, sizeof(t_FmIpcParams));
            *p_ReplyLength = sizeof(uint32_t) + sizeof(t_FmIpcParams);
             break;
        }
        case (FM_GET_FMAN_CTRL_CODE_REV):
        {
            t_FmCtrlCodeRevisionInfo        fmanCtrlRevInfo;
            t_FmIpcFmanCtrlCodeRevisionInfo ipcRevInfo;

            p_IpcReply->error = (uint32_t)FM_GetFmanCtrlCodeRevision(h_Fm, &fmanCtrlRevInfo);
            ipcRevInfo.packageRev = fmanCtrlRevInfo.packageRev;
            ipcRevInfo.majorRev = fmanCtrlRevInfo.majorRev;
            ipcRevInfo.minorRev = fmanCtrlRevInfo.minorRev;
            memcpy(p_IpcReply->replyBody, (uint8_t*)&ipcRevInfo, sizeof(t_FmIpcFmanCtrlCodeRevisionInfo));
            *p_ReplyLength = sizeof(uint32_t) + sizeof(t_FmIpcFmanCtrlCodeRevisionInfo);
            break;
        }

        case (FM_DMA_STAT):
        {
            t_FmDmaStatus       dmaStatus;
            t_FmIpcDmaStatus    ipcDmaStatus;

            FM_GetDmaStatus(h_Fm, &dmaStatus);
            ipcDmaStatus.boolCmqNotEmpty = (uint8_t)dmaStatus.cmqNotEmpty;
            ipcDmaStatus.boolBusError = (uint8_t)dmaStatus.busError;
            ipcDmaStatus.boolReadBufEccError = (uint8_t)dmaStatus.readBufEccError;
            ipcDmaStatus.boolWriteBufEccSysError = (uint8_t)dmaStatus.writeBufEccSysError;
            ipcDmaStatus.boolWriteBufEccFmError = (uint8_t)dmaStatus.writeBufEccFmError;
            ipcDmaStatus.boolSinglePortEccError = (uint8_t)dmaStatus.singlePortEccError;
            memcpy(p_IpcReply->replyBody, (uint8_t*)&ipcDmaStatus, sizeof(t_FmIpcDmaStatus));
            *p_ReplyLength = sizeof(uint32_t) + sizeof(t_FmIpcDmaStatus);
            break;
        }
        case (FM_ALLOC_FMAN_CTRL_EVENT_REG):
            p_IpcReply->error = (uint32_t)FmAllocFmanCtrlEventReg(h_Fm, (uint8_t*)p_IpcReply->replyBody);
            *p_ReplyLength = sizeof(uint32_t) + sizeof(uint8_t);
            break;
        case (FM_FREE_FMAN_CTRL_EVENT_REG):
            FmFreeFmanCtrlEventReg(h_Fm, p_IpcMsg->msgBody[0]);
            break;
        case (FM_GET_TIMESTAMP_SCALE):
        {
            uint32_t    timeStamp = FmGetTimeStampScale(h_Fm);

            memcpy(p_IpcReply->replyBody, (uint8_t*)&timeStamp, sizeof(uint32_t));
            *p_ReplyLength = sizeof(uint32_t) + sizeof(uint32_t);
            break;
        }
        case (FM_GET_COUNTER):
        {
            e_FmCounters    inCounter;
            uint32_t        outCounter;

            memcpy((uint8_t*)&inCounter, p_IpcMsg->msgBody, sizeof(uint32_t));
            outCounter = FM_GetCounter(h_Fm, inCounter);
            memcpy(p_IpcReply->replyBody, (uint8_t*)&outCounter, sizeof(uint32_t));
            *p_ReplyLength = sizeof(uint32_t) + sizeof(uint32_t);
            break;
        }
        case (FM_SET_FMAN_CTRL_EVENTS_ENABLE):
        {
            t_FmIpcFmanEvents ipcFmanEvents;

            memcpy((uint8_t*)&ipcFmanEvents, p_IpcMsg->msgBody, sizeof(t_FmIpcFmanEvents));
            FmSetFmanCtrlIntr(h_Fm,
                              ipcFmanEvents.eventRegId,
                              ipcFmanEvents.enableEvents);
            break;
        }
        case (FM_GET_FMAN_CTRL_EVENTS_ENABLE):
        {
            uint32_t    tmp = FmGetFmanCtrlIntr(h_Fm, p_IpcMsg->msgBody[0]);

            memcpy(p_IpcReply->replyBody, (uint8_t*)&tmp, sizeof(uint32_t));
            *p_ReplyLength = sizeof(uint32_t) + sizeof(uint32_t);
            break;
        }
        case (FM_GET_PHYS_MURAM_BASE):
        {
            t_FmPhysAddr        physAddr;
            t_FmIpcPhysAddr     ipcPhysAddr;

            FmGetPhysicalMuramBase(h_Fm, &physAddr);
            ipcPhysAddr.high    = physAddr.high;
            ipcPhysAddr.low     = physAddr.low;
            memcpy(p_IpcReply->replyBody, (uint8_t*)&ipcPhysAddr, sizeof(t_FmIpcPhysAddr));
            *p_ReplyLength = sizeof(uint32_t) + sizeof(t_FmIpcPhysAddr);
            break;
        }
        case (FM_ENABLE_RAM_ECC):
        {
            if (((err = FM_EnableRamsEcc(h_Fm)) != E_OK) ||
                ((err = FM_SetException(h_Fm, e_FM_EX_IRAM_ECC, TRUE)) != E_OK) ||
                ((err = FM_SetException(h_Fm, e_FM_EX_MURAM_ECC, TRUE)) != E_OK))
#if (!(defined(DEBUG_ERRORS)) || (DEBUG_ERRORS == 0))
                UNUSED(err);
#else
                REPORT_ERROR(MINOR, err, NO_MSG);
#endif /* (!(defined(DEBUG_ERRORS)) || (DEBUG_ERRORS == 0)) */
            break;
        }
        case (FM_DISABLE_RAM_ECC):
        {

            if (((err = FM_SetException(h_Fm, e_FM_EX_IRAM_ECC, FALSE)) != E_OK) ||
                ((err = FM_SetException(h_Fm, e_FM_EX_MURAM_ECC, FALSE)) != E_OK) ||
                ((err = FM_DisableRamsEcc(h_Fm)) != E_OK))
#if (!(defined(DEBUG_ERRORS)) || (DEBUG_ERRORS == 0))
                UNUSED(err);
#else
                REPORT_ERROR(MINOR, err, NO_MSG);
#endif /* (!(defined(DEBUG_ERRORS)) || (DEBUG_ERRORS == 0)) */
            break;
        }
        case (FM_SET_NUM_OF_FMAN_CTRL):
        {
            t_FmIpcPortNumOfFmanCtrls   ipcPortNumOfFmanCtrls;

            memcpy((uint8_t*)&ipcPortNumOfFmanCtrls, p_IpcMsg->msgBody, sizeof(t_FmIpcPortNumOfFmanCtrls));
            err = FmSetNumOfRiscsPerPort(h_Fm,
                                         ipcPortNumOfFmanCtrls.hardwarePortId,
                                         ipcPortNumOfFmanCtrls.numOfFmanCtrls,
                                         ipcPortNumOfFmanCtrls.orFmanCtrl);
            if (err != E_OK)
                REPORT_ERROR(MINOR, err, NO_MSG);
            break;
        }
#ifdef FM_TX_ECC_FRMS_ERRATA_10GMAC_A004
        case (FM_10G_TX_ECC_WA):
            p_IpcReply->error = (uint32_t)Fm10GTxEccWorkaround(h_Fm, p_IpcMsg->msgBody[0]);
            *p_ReplyLength = sizeof(uint32_t);
            break;
#endif /* FM_TX_ECC_FRMS_ERRATA_10GMAC_A004 */
        default:
            *p_ReplyLength = 0;
            RETURN_ERROR(MINOR, E_INVALID_SELECTION, ("command not found!!!"));
    }
    return E_OK;
}


/****************************************/
/*       Inter-Module functions         */
/****************************************/
#ifdef FM_TX_ECC_FRMS_ERRATA_10GMAC_A004
t_Error Fm10GTxEccWorkaround(t_Handle h_Fm, uint8_t macId)
{
    t_Fm            *p_Fm = (t_Fm*)h_Fm;
    t_Error         err = E_OK;
    t_FmIpcMsg      msg;
    t_FmIpcReply    reply;
    uint32_t        replyLength;
    uint8_t         rxHardwarePortId, txHardwarePortId;
    struct fman_fpm_regs *fpm_rg = p_Fm->p_FmFpmRegs;

    if (p_Fm->guestId != NCSW_MASTER_ID)
    {
        memset(&msg, 0, sizeof(msg));
        memset(&reply, 0, sizeof(reply));
        msg.msgId = FM_10G_TX_ECC_WA;
        msg.msgBody[0] = macId;
        replyLength = sizeof(uint32_t);
        if ((err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                     (uint8_t*)&msg,
                                     sizeof(msg.msgId)+sizeof(macId),
                                     (uint8_t*)&reply,
                                     &replyLength,
                                     NULL,
                                     NULL)) != E_OK)
            RETURN_ERROR(MINOR, err, NO_MSG);
        if (replyLength != sizeof(uint32_t))
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));
        return (t_Error)(reply.error);
    }

    SANITY_CHECK_RETURN_ERROR((macId == 0), E_NOT_SUPPORTED);
    SANITY_CHECK_RETURN_ERROR(IsFmanCtrlCodeLoaded(p_Fm), E_INVALID_STATE);

    rxHardwarePortId = SwPortIdToHwPortId(e_FM_PORT_TYPE_RX_10G,
                                    macId,
                                    p_Fm->p_FmStateStruct->revInfo.majorRev,
                                    p_Fm->p_FmStateStruct->revInfo.minorRev);
    txHardwarePortId = SwPortIdToHwPortId(e_FM_PORT_TYPE_TX_10G,
                                    macId,
                                    p_Fm->p_FmStateStruct->revInfo.majorRev,
                                    p_Fm->p_FmStateStruct->revInfo.minorRev);
    if ((p_Fm->p_FmStateStruct->portsTypes[rxHardwarePortId] != e_FM_PORT_TYPE_DUMMY) ||
        (p_Fm->p_FmStateStruct->portsTypes[txHardwarePortId] != e_FM_PORT_TYPE_DUMMY))
        RETURN_ERROR(MAJOR, E_INVALID_STATE,
                     ("MAC should be initialized prior to Rx and Tx ports!"));

    return fman_set_erratum_10gmac_a004_wa(fpm_rg);
}
#endif /* FM_TX_ECC_FRMS_ERRATA_10GMAC_A004 */

uint16_t FmGetTnumAgingPeriod(t_Handle h_Fm)
{
    t_Fm *p_Fm = (t_Fm *)h_Fm;

    SANITY_CHECK_RETURN_VALUE(p_Fm, E_INVALID_HANDLE, 0);
    SANITY_CHECK_RETURN_VALUE(!p_Fm->p_FmDriverParam, E_INVALID_STATE, 0);

    return p_Fm->tnumAgingPeriod;
}

t_Error FmSetPortPreFetchConfiguration(t_Handle h_Fm,
                                       uint8_t  portNum,
                                       bool     preFetchConfigured)
{
    t_Fm *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Fm->p_FmDriverParam, E_INVALID_STATE);

    p_Fm->portsPreFetchConfigured[portNum] = TRUE;
    p_Fm->portsPreFetchValue[portNum] = preFetchConfigured;

    return E_OK;
}

t_Error FmGetPortPreFetchConfiguration(t_Handle h_Fm,
                                       uint8_t  portNum,
                                       bool     *p_PortConfigured,
                                       bool     *p_PreFetchConfigured)
{
    t_Fm *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Fm->p_FmDriverParam, E_INVALID_STATE);

    /* If the prefetch wasn't configured yet (not enable or disabled)
       we return the value TRUE as it was already configured */
    if (!p_Fm->portsPreFetchConfigured[portNum])
    {
        *p_PortConfigured = FALSE;
        *p_PreFetchConfigured = FALSE;
    }
    else
    {
        *p_PortConfigured = TRUE;
        *p_PreFetchConfigured = (p_Fm->portsPreFetchConfigured[portNum]);
    }

    return E_OK;
}

t_Error FmSetCongestionGroupPFCpriority(t_Handle    h_Fm,
                                        uint32_t    congestionGroupId,
                                        uint8_t     priorityBitMap)
{
    t_Fm    *p_Fm  = (t_Fm *)h_Fm;
    uint32_t regNum;

    ASSERT_COND(h_Fm);

    if (congestionGroupId > FM_PORT_NUM_OF_CONGESTION_GRPS)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                     ("Congestion group ID bigger than %d",
                      FM_PORT_NUM_OF_CONGESTION_GRPS));

    if (p_Fm->guestId == NCSW_MASTER_ID)
    {
        ASSERT_COND(p_Fm->baseAddr);
        regNum = (FM_PORT_NUM_OF_CONGESTION_GRPS - 1 - congestionGroupId) / 4;
        fman_set_congestion_group_pfc_priority((uint32_t *)((p_Fm->baseAddr+FM_MM_CGP)),
                                               congestionGroupId,
                                               priorityBitMap,
                                               regNum);
    }
    else if (p_Fm->h_IpcSessions[0])
    {
        t_Error                              err;
        t_FmIpcMsg                           msg;
        t_FmIpcSetCongestionGroupPfcPriority fmIpcSetCongestionGroupPfcPriority;

        memset(&msg, 0, sizeof(msg));
        memset(&fmIpcSetCongestionGroupPfcPriority, 0, sizeof(t_FmIpcSetCongestionGroupPfcPriority));
        fmIpcSetCongestionGroupPfcPriority.congestionGroupId = congestionGroupId;
        fmIpcSetCongestionGroupPfcPriority.priorityBitMap    = priorityBitMap;

        msg.msgId = FM_SET_CONG_GRP_PFC_PRIO;
        memcpy(msg.msgBody, &fmIpcSetCongestionGroupPfcPriority, sizeof(t_FmIpcSetCongestionGroupPfcPriority));

        err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                (uint8_t*)&msg,
                                sizeof(msg.msgId),
                                NULL,
                                NULL,
                                NULL,
                                NULL);
        if (err != E_OK)
            RETURN_ERROR(MINOR, err, NO_MSG);
    }
    else
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("guest without IPC!"));

    return E_OK;
}

uintptr_t FmGetPcdPrsBaseAddr(t_Handle h_Fm)
{
    t_Fm        *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_VALUE(p_Fm, E_INVALID_HANDLE, 0);

    if (!p_Fm->baseAddr)
    {
        REPORT_ERROR(MAJOR, E_INVALID_STATE,
                     ("No base-addr; probably Guest with IPC!"));
        return 0;
    }

    return (p_Fm->baseAddr + FM_MM_PRS);
}

uintptr_t FmGetPcdKgBaseAddr(t_Handle h_Fm)
{
    t_Fm        *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_VALUE(p_Fm, E_INVALID_HANDLE, 0);

    if (!p_Fm->baseAddr)
    {
        REPORT_ERROR(MAJOR, E_INVALID_STATE,
                     ("No base-addr; probably Guest with IPC!"));
        return 0;
    }

    return (p_Fm->baseAddr + FM_MM_KG);
}

uintptr_t FmGetPcdPlcrBaseAddr(t_Handle h_Fm)
{
    t_Fm        *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_VALUE(p_Fm, E_INVALID_HANDLE, 0);

    if (!p_Fm->baseAddr)
    {
        REPORT_ERROR(MAJOR, E_INVALID_STATE,
                     ("No base-addr; probably Guest with IPC!"));
        return 0;
    }

    return (p_Fm->baseAddr + FM_MM_PLCR);
}

#if (DPAA_VERSION >= 11)
uintptr_t FmGetVSPBaseAddr(t_Handle h_Fm)
{
    t_Fm        *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_VALUE(p_Fm, E_INVALID_HANDLE, 0);

    return p_Fm->vspBaseAddr;
}
#endif /* (DPAA_VERSION >= 11) */

t_Handle FmGetMuramHandle(t_Handle h_Fm)
{
    t_Fm        *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_VALUE(p_Fm, E_INVALID_HANDLE, NULL);

    return (p_Fm->h_FmMuram);
}

void FmGetPhysicalMuramBase(t_Handle h_Fm, t_FmPhysAddr *p_FmPhysAddr)
{
    t_Fm            *p_Fm = (t_Fm*)h_Fm;

    if (p_Fm->fmMuramPhysBaseAddr)
    {
        /* General FM driver initialization */
        p_FmPhysAddr->low = (uint32_t)p_Fm->fmMuramPhysBaseAddr;
        p_FmPhysAddr->high = (uint8_t)((p_Fm->fmMuramPhysBaseAddr & 0x000000ff00000000LL) >> 32);
        return;
    }

    ASSERT_COND(p_Fm->guestId != NCSW_MASTER_ID);

    if (p_Fm->h_IpcSessions[0])
    {
        t_Error         err;
        t_FmIpcMsg      msg;
        t_FmIpcReply    reply;
        uint32_t        replyLength;
        t_FmIpcPhysAddr ipcPhysAddr;

        memset(&msg, 0, sizeof(msg));
        memset(&reply, 0, sizeof(reply));
        msg.msgId = FM_GET_PHYS_MURAM_BASE;
        replyLength = sizeof(uint32_t) + sizeof(t_FmPhysAddr);
        err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                (uint8_t*)&msg,
                                sizeof(msg.msgId),
                                (uint8_t*)&reply,
                                &replyLength,
                                NULL,
                                NULL);
        if (err != E_OK)
        {
            REPORT_ERROR(MINOR, err, NO_MSG);
            return;
        }
        if (replyLength != (sizeof(uint32_t) + sizeof(t_FmPhysAddr)))
        {
            REPORT_ERROR(MINOR, E_INVALID_VALUE,("IPC reply length mismatch"));
            return;
        }
        memcpy((uint8_t*)&ipcPhysAddr, reply.replyBody, sizeof(t_FmIpcPhysAddr));
        p_FmPhysAddr->high = ipcPhysAddr.high;
        p_FmPhysAddr->low  = ipcPhysAddr.low;
    }
    else
        REPORT_ERROR(MINOR, E_NOT_SUPPORTED,
                     ("running in guest-mode without neither IPC nor mapped register!"));
}

#if (DPAA_VERSION >= 11)
t_Error FmVSPAllocForPort (t_Handle        h_Fm,
                           e_FmPortType    portType,
                           uint8_t         portId,
                           uint8_t         numOfVSPs)
{
    t_Fm           *p_Fm = (t_Fm *)h_Fm;
    t_Error        err = E_OK;
    uint32_t       profilesFound, intFlags;
    uint8_t        first, i;
    uint8_t        log2Num;
    uint8_t        swPortIndex=0, hardwarePortId;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);

     if (!numOfVSPs)
        return E_OK;

    if (numOfVSPs > FM_VSP_MAX_NUM_OF_ENTRIES)
        RETURN_ERROR(MINOR, E_INVALID_VALUE, ("numProfiles can not be bigger than %d.",FM_VSP_MAX_NUM_OF_ENTRIES));

    if (!POWER_OF_2(numOfVSPs))
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("numProfiles must be a power of 2."));

    LOG2((uint64_t)numOfVSPs, log2Num);

    if ((log2Num == 0) || (p_Fm->partVSPBase == 0))
        first = 0;
    else
        first = 1<<log2Num;

    if (first > (p_Fm->partVSPBase + p_Fm->partNumOfVSPs))
         RETURN_ERROR(MINOR, E_INVALID_VALUE, ("can not allocate storage profile port window"));

    if (first < p_Fm->partVSPBase)
        while (first < p_Fm->partVSPBase)
            first = first + numOfVSPs;

    if ((first + numOfVSPs) > (p_Fm->partVSPBase + p_Fm->partNumOfVSPs))
        RETURN_ERROR(MINOR, E_INVALID_VALUE, ("can not allocate storage profile port window"));

    intFlags = XX_LockIntrSpinlock(p_Fm->h_Spinlock);
    profilesFound = 0;
    for (i=first; i < p_Fm->partVSPBase + p_Fm->partNumOfVSPs; )
    {
        if (!p_Fm->p_FmSp->profiles[i].profilesMng.allocated)
        {
            profilesFound++;
            i++;
            if (profilesFound == numOfVSPs)
                break;
        }
        else
        {
            profilesFound = 0;
            /* advance i to the next aligned address */
            first = i = (uint8_t)(first + numOfVSPs);
        }
    }
    if (profilesFound == numOfVSPs)
        for (i = first; i<first + numOfVSPs; i++)
            p_Fm->p_FmSp->profiles[i].profilesMng.allocated = TRUE;
    else
    {
        XX_UnlockIntrSpinlock(p_Fm->h_Spinlock, intFlags);
        RETURN_ERROR(MINOR, E_FULL, ("No profiles."));
    }

    hardwarePortId = SwPortIdToHwPortId(portType,
                                    portId,
                                    p_Fm->p_FmStateStruct->revInfo.majorRev,
                                    p_Fm->p_FmStateStruct->revInfo.minorRev);
    HW_PORT_ID_TO_SW_PORT_INDX(swPortIndex, hardwarePortId);

    p_Fm->p_FmSp->portsMapping[swPortIndex].numOfProfiles = numOfVSPs;
    p_Fm->p_FmSp->portsMapping[swPortIndex].profilesBase = first;

    if ((err = SetVSPWindow(h_Fm,hardwarePortId, first,log2Num)) != E_OK)
        for (i = first; i < first + numOfVSPs; i++)
            p_Fm->p_FmSp->profiles[i].profilesMng.allocated = FALSE;

    XX_UnlockIntrSpinlock(p_Fm->h_Spinlock, intFlags);

    return err;
}

t_Error FmVSPFreeForPort(t_Handle        h_Fm,
                         e_FmPortType    portType,
                         uint8_t         portId)
{
    t_Fm            *p_Fm = (t_Fm *)h_Fm;
    uint8_t         swPortIndex=0, hardwarePortId, first, numOfVSPs, i;
    uint32_t        intFlags;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);

    hardwarePortId = SwPortIdToHwPortId(portType,
                                    portId,
                                    p_Fm->p_FmStateStruct->revInfo.majorRev,
                                    p_Fm->p_FmStateStruct->revInfo.minorRev);
    HW_PORT_ID_TO_SW_PORT_INDX(swPortIndex, hardwarePortId);

    numOfVSPs = (uint8_t)p_Fm->p_FmSp->portsMapping[swPortIndex].numOfProfiles;
    first = (uint8_t)p_Fm->p_FmSp->portsMapping[swPortIndex].profilesBase;

    intFlags = XX_LockIntrSpinlock(p_Fm->h_Spinlock);
    for (i = first; i < first + numOfVSPs; i++)
           p_Fm->p_FmSp->profiles[i].profilesMng.allocated = FALSE;
    XX_UnlockIntrSpinlock(p_Fm->h_Spinlock, intFlags);

    p_Fm->p_FmSp->portsMapping[swPortIndex].numOfProfiles = 0;
    p_Fm->p_FmSp->portsMapping[swPortIndex].profilesBase = 0;

    return E_OK;
}
#endif /* (DPAA_VERSION >= 11) */

t_Error FmAllocFmanCtrlEventReg(t_Handle h_Fm, uint8_t *p_EventId)
{
    t_Fm            *p_Fm = (t_Fm*)h_Fm;
    uint8_t         i;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);

    if ((p_Fm->guestId != NCSW_MASTER_ID) &&
        p_Fm->h_IpcSessions[0])
    {
        t_Error         err;
        t_FmIpcMsg      msg;
        t_FmIpcReply    reply;
        uint32_t        replyLength;

        memset(&msg, 0, sizeof(msg));
        memset(&reply, 0, sizeof(reply));
        msg.msgId = FM_ALLOC_FMAN_CTRL_EVENT_REG;
        replyLength = sizeof(uint32_t) + sizeof(uint8_t);
        if ((err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                     (uint8_t*)&msg,
                                     sizeof(msg.msgId),
                                     (uint8_t*)&reply,
                                     &replyLength,
                                     NULL,
                                     NULL)) != E_OK)
            RETURN_ERROR(MAJOR, err, NO_MSG);

        if (replyLength != (sizeof(uint32_t) + sizeof(uint8_t)))
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));

        *p_EventId = *(uint8_t*)(reply.replyBody);

        return (t_Error)(reply.error);
    }
    else if (p_Fm->guestId != NCSW_MASTER_ID)
        RETURN_ERROR(MINOR, E_NOT_SUPPORTED,
                     ("running in guest-mode without IPC!"));

    for (i=0;i<FM_NUM_OF_FMAN_CTRL_EVENT_REGS;i++)
        if (!p_Fm->usedEventRegs[i])
        {
            p_Fm->usedEventRegs[i] = TRUE;
            *p_EventId = i;
            break;
        }

    if (i==FM_NUM_OF_FMAN_CTRL_EVENT_REGS)
        RETURN_ERROR(MAJOR, E_BUSY, ("No resource - FMan controller event register."));

    return E_OK;
}

void FmFreeFmanCtrlEventReg(t_Handle h_Fm, uint8_t eventId)
{
    t_Fm        *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN(p_Fm, E_INVALID_HANDLE);

    if ((p_Fm->guestId != NCSW_MASTER_ID) &&
        p_Fm->h_IpcSessions[0])
    {
        t_Error     err;
        t_FmIpcMsg  msg;

        memset(&msg, 0, sizeof(msg));
        msg.msgId = FM_FREE_FMAN_CTRL_EVENT_REG;
        msg.msgBody[0] = eventId;
        err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                (uint8_t*)&msg,
                                sizeof(msg.msgId)+sizeof(eventId),
                                NULL,
                                NULL,
                                NULL,
                                NULL);
        if (err != E_OK)
            REPORT_ERROR(MINOR, err, NO_MSG);
        return;
    }
    else if (p_Fm->guestId != NCSW_MASTER_ID)
    {
        REPORT_ERROR(MINOR, E_NOT_SUPPORTED,
                     ("running in guest-mode without IPC!"));
        return;
    }

    ((t_Fm*)h_Fm)->usedEventRegs[eventId] = FALSE;
}

void FmSetFmanCtrlIntr(t_Handle h_Fm, uint8_t eventRegId, uint32_t enableEvents)
{
    t_Fm                *p_Fm = (t_Fm*)h_Fm;
    struct fman_fpm_regs *fpm_rg = p_Fm->p_FmFpmRegs;

    if ((p_Fm->guestId != NCSW_MASTER_ID) &&
        !p_Fm->p_FmFpmRegs &&
        p_Fm->h_IpcSessions[0])
    {
        t_FmIpcFmanEvents   fmanCtrl;
        t_Error             err;
        t_FmIpcMsg          msg;

        fmanCtrl.eventRegId = eventRegId;
        fmanCtrl.enableEvents = enableEvents;
        memset(&msg, 0, sizeof(msg));
        msg.msgId = FM_SET_FMAN_CTRL_EVENTS_ENABLE;
        memcpy(msg.msgBody, &fmanCtrl, sizeof(fmanCtrl));
        err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                (uint8_t*)&msg,
                                sizeof(msg.msgId)+sizeof(fmanCtrl),
                                NULL,
                                NULL,
                                NULL,
                                NULL);
        if (err != E_OK)
            REPORT_ERROR(MINOR, err, NO_MSG);
        return;
    }
    else if (!p_Fm->p_FmFpmRegs)
    {
        REPORT_ERROR(MINOR, E_NOT_SUPPORTED,
                     ("Either IPC or 'baseAddress' is required!"));
        return;
    }

    ASSERT_COND(eventRegId < FM_NUM_OF_FMAN_CTRL_EVENT_REGS);
    fman_set_ctrl_intr(fpm_rg, eventRegId, enableEvents);
}

uint32_t FmGetFmanCtrlIntr(t_Handle h_Fm, uint8_t eventRegId)
{
    t_Fm            *p_Fm = (t_Fm*)h_Fm;
    struct fman_fpm_regs *fpm_rg = p_Fm->p_FmFpmRegs;

    if ((p_Fm->guestId != NCSW_MASTER_ID) &&
        !p_Fm->p_FmFpmRegs &&
        p_Fm->h_IpcSessions[0])
    {
        t_Error         err;
        t_FmIpcMsg      msg;
        t_FmIpcReply    reply;
        uint32_t        replyLength, ctrlIntr;

        memset(&msg, 0, sizeof(msg));
        memset(&reply, 0, sizeof(reply));
        msg.msgId = FM_GET_FMAN_CTRL_EVENTS_ENABLE;
        msg.msgBody[0] = eventRegId;
        replyLength = sizeof(uint32_t) + sizeof(uint32_t);
        err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                (uint8_t*)&msg,
                                sizeof(msg.msgId)+sizeof(eventRegId),
                                (uint8_t*)&reply,
                                &replyLength,
                                NULL,
                                NULL);
        if (err != E_OK)
        {
            REPORT_ERROR(MINOR, err, NO_MSG);
            return 0;
        }
        if (replyLength != (sizeof(uint32_t) + sizeof(uint32_t)))
        {
            REPORT_ERROR(MINOR, E_INVALID_VALUE, ("IPC reply length mismatch"));
            return 0;
        }
        memcpy((uint8_t*)&ctrlIntr, reply.replyBody, sizeof(uint32_t));
        return ctrlIntr;
    }
    else if (!p_Fm->p_FmFpmRegs)
    {
        REPORT_ERROR(MINOR, E_NOT_SUPPORTED,
                     ("Either IPC or 'baseAddress' is required!"));
        return 0;
    }

    return fman_get_ctrl_intr(fpm_rg, eventRegId);
}

void FmRegisterIntr(t_Handle                h_Fm,
                    e_FmEventModules        module,
                    uint8_t                 modId,
                    e_FmIntrType            intrType,
                    void                    (*f_Isr) (t_Handle h_Arg),
                    t_Handle                h_Arg)
{
    t_Fm                *p_Fm = (t_Fm*)h_Fm;
    int                 event = 0;

    ASSERT_COND(h_Fm);

    GET_FM_MODULE_EVENT(module, modId, intrType, event);
    ASSERT_COND(event < e_FM_EV_DUMMY_LAST);

    /* register in local FM structure */
    p_Fm->intrMng[event].f_Isr = f_Isr;
    p_Fm->intrMng[event].h_SrcHandle = h_Arg;

    if ((p_Fm->guestId != NCSW_MASTER_ID) &&
        p_Fm->h_IpcSessions[0])
    {
        t_FmIpcRegisterIntr fmIpcRegisterIntr;
        t_Error             err;
        t_FmIpcMsg          msg;

        /* register in Master FM structure */
        fmIpcRegisterIntr.event = (uint32_t)event;
        fmIpcRegisterIntr.guestId = p_Fm->guestId;
        memset(&msg, 0, sizeof(msg));
        msg.msgId = FM_REGISTER_INTR;
        memcpy(msg.msgBody, &fmIpcRegisterIntr, sizeof(fmIpcRegisterIntr));
        err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                (uint8_t*)&msg,
                                sizeof(msg.msgId) + sizeof(fmIpcRegisterIntr),
                                NULL,
                                NULL,
                                NULL,
                                NULL);
        if (err != E_OK)
            REPORT_ERROR(MINOR, err, NO_MSG);
    }
    else if (p_Fm->guestId != NCSW_MASTER_ID)
        REPORT_ERROR(MINOR, E_NOT_SUPPORTED,
                     ("running in guest-mode without IPC!"));
}

void FmUnregisterIntr(t_Handle                  h_Fm,
                        e_FmEventModules        module,
                        uint8_t                 modId,
                        e_FmIntrType            intrType)
{
    t_Fm        *p_Fm = (t_Fm*)h_Fm;
    int         event = 0;

    ASSERT_COND(h_Fm);

    GET_FM_MODULE_EVENT(module, modId,intrType, event);
    ASSERT_COND(event < e_FM_EV_DUMMY_LAST);

    p_Fm->intrMng[event].f_Isr = UnimplementedIsr;
    p_Fm->intrMng[event].h_SrcHandle = NULL;
}

void  FmRegisterFmanCtrlIntr(t_Handle h_Fm, uint8_t eventRegId, void (*f_Isr) (t_Handle h_Arg, uint32_t event), t_Handle    h_Arg)
{
    t_Fm       *p_Fm = (t_Fm*)h_Fm;

    ASSERT_COND(eventRegId<FM_NUM_OF_FMAN_CTRL_EVENT_REGS);

    if (p_Fm->guestId != NCSW_MASTER_ID)
    {
        REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("FM in guest-mode"));
        return;
    }

    p_Fm->fmanCtrlIntr[eventRegId].f_Isr = f_Isr;
    p_Fm->fmanCtrlIntr[eventRegId].h_SrcHandle = h_Arg;
}

void  FmUnregisterFmanCtrlIntr(t_Handle h_Fm, uint8_t eventRegId)
{
    t_Fm       *p_Fm = (t_Fm*)h_Fm;

    ASSERT_COND(eventRegId<FM_NUM_OF_FMAN_CTRL_EVENT_REGS);

    if (p_Fm->guestId != NCSW_MASTER_ID)
    {
        REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("FM in guest-mode"));
        return;
    }

    p_Fm->fmanCtrlIntr[eventRegId].f_Isr = UnimplementedFmanCtrlIsr;
    p_Fm->fmanCtrlIntr[eventRegId].h_SrcHandle = NULL;
}

void  FmRegisterPcd(t_Handle h_Fm, t_Handle h_FmPcd)
{
    t_Fm       *p_Fm = (t_Fm*)h_Fm;

    if (p_Fm->h_Pcd)
        REPORT_ERROR(MAJOR, E_ALREADY_EXISTS, ("PCD already set"));

    p_Fm->h_Pcd = h_FmPcd;
}

void  FmUnregisterPcd(t_Handle h_Fm)
{
    t_Fm       *p_Fm = (t_Fm*)h_Fm;

    if (!p_Fm->h_Pcd)
        REPORT_ERROR(MAJOR, E_NOT_FOUND, ("PCD handle!"));

    p_Fm->h_Pcd = NULL;
}

t_Handle FmGetPcdHandle(t_Handle h_Fm)
{
    t_Fm       *p_Fm = (t_Fm*)h_Fm;

    return p_Fm->h_Pcd;
}

uint8_t FmGetId(t_Handle h_Fm)
{
    t_Fm *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_VALUE(p_Fm, E_INVALID_HANDLE, 0xff);

    return p_Fm->p_FmStateStruct->fmId;
}

t_Error FmReset(t_Handle h_Fm)
{
	t_Fm *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);

    WRITE_UINT32(p_Fm->p_FmFpmRegs->fm_rstc, FPM_RSTC_FM_RESET);
    CORE_MemoryBarrier();
    XX_UDelay(100);

    return E_OK;
}

t_Error FmSetNumOfRiscsPerPort(t_Handle     h_Fm,
                               uint8_t      hardwarePortId,
                               uint8_t      numOfFmanCtrls,
                               t_FmFmanCtrl orFmanCtrl)
{

    t_Fm                        *p_Fm = (t_Fm*)h_Fm;
    struct fman_fpm_regs *fpm_rg;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(((numOfFmanCtrls > 0) && (numOfFmanCtrls < 3)) , E_INVALID_HANDLE);

    fpm_rg = p_Fm->p_FmFpmRegs;
    if ((p_Fm->guestId != NCSW_MASTER_ID) &&
        !p_Fm->p_FmFpmRegs &&
        p_Fm->h_IpcSessions[0])
    {
        t_Error                     err;
        t_FmIpcPortNumOfFmanCtrls   params;
        t_FmIpcMsg                  msg;

        memset(&msg, 0, sizeof(msg));
        params.hardwarePortId = hardwarePortId;
        params.numOfFmanCtrls = numOfFmanCtrls;
        params.orFmanCtrl = orFmanCtrl;
        msg.msgId = FM_SET_NUM_OF_FMAN_CTRL;
        memcpy(msg.msgBody, &params, sizeof(params));
        err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                (uint8_t*)&msg,
                                sizeof(msg.msgId) +sizeof(params),
                                NULL,
                                NULL,
                                NULL,
                                NULL);
        if (err != E_OK)
            RETURN_ERROR(MINOR, err, NO_MSG);
        return E_OK;
    }
    else if (!p_Fm->p_FmFpmRegs)
        RETURN_ERROR(MINOR, E_NOT_SUPPORTED,
                     ("Either IPC or 'baseAddress' is required!"));

    fman_set_num_of_riscs_per_port(fpm_rg, hardwarePortId, numOfFmanCtrls, orFmanCtrl);

    return E_OK;
}

t_Error FmGetSetPortParams(t_Handle h_Fm, t_FmInterModulePortInitParams *p_PortParams)
{
    t_Fm                    *p_Fm = (t_Fm*)h_Fm;
    t_Error                 err;
    uint32_t                intFlags;
    uint8_t                 hardwarePortId = p_PortParams->hardwarePortId, macId;
    struct fman_rg          fman_rg;

    fman_rg.bmi_rg = p_Fm->p_FmBmiRegs;
    fman_rg.qmi_rg = p_Fm->p_FmQmiRegs;
    fman_rg.fpm_rg = p_Fm->p_FmFpmRegs;
    fman_rg.dma_rg = p_Fm->p_FmDmaRegs;

    if (p_Fm->guestId != NCSW_MASTER_ID)
    {
        t_FmIpcPortInInitParams     portInParams;
        t_FmIpcPortOutInitParams    portOutParams;
        t_FmIpcMsg                  msg;
        t_FmIpcReply                reply;
        uint32_t                    replyLength;

        portInParams.hardwarePortId     = p_PortParams->hardwarePortId;
        portInParams.enumPortType       = (uint32_t)p_PortParams->portType;
        portInParams.boolIndependentMode= (uint8_t)p_PortParams->independentMode;
        portInParams.liodnOffset        = p_PortParams->liodnOffset;
        portInParams.numOfTasks         = p_PortParams->numOfTasks;
        portInParams.numOfExtraTasks    = p_PortParams->numOfExtraTasks;
        portInParams.numOfOpenDmas      = p_PortParams->numOfOpenDmas;
        portInParams.numOfExtraOpenDmas = p_PortParams->numOfExtraOpenDmas;
        portInParams.sizeOfFifo         = p_PortParams->sizeOfFifo;
        portInParams.extraSizeOfFifo    = p_PortParams->extraSizeOfFifo;
        portInParams.deqPipelineDepth   = p_PortParams->deqPipelineDepth;
        portInParams.maxFrameLength     = p_PortParams->maxFrameLength;
        portInParams.liodnBase          = p_PortParams->liodnBase;

        memset(&msg, 0, sizeof(msg));
        memset(&reply, 0, sizeof(reply));
        msg.msgId = FM_GET_SET_PORT_PARAMS;
        memcpy(msg.msgBody, &portInParams, sizeof(portInParams));
        replyLength = (sizeof(uint32_t) + sizeof(t_FmIpcPortOutInitParams));
        if ((err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                     (uint8_t*)&msg,
                                     sizeof(msg.msgId) +sizeof(portInParams),
                                     (uint8_t*)&reply,
                                     &replyLength,
                                     NULL,
                                     NULL)) != E_OK)
            RETURN_ERROR(MINOR, err, NO_MSG);
        if (replyLength != (sizeof(uint32_t) + sizeof(t_FmIpcPortOutInitParams)))
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));
        memcpy((uint8_t*)&portOutParams, reply.replyBody, sizeof(t_FmIpcPortOutInitParams));

        p_PortParams->fmMuramPhysBaseAddr.high = portOutParams.ipcPhysAddr.high;
        p_PortParams->fmMuramPhysBaseAddr.low  = portOutParams.ipcPhysAddr.low;
        p_PortParams->numOfTasks = portOutParams.numOfTasks;
        p_PortParams->numOfExtraTasks = portOutParams.numOfExtraTasks;
        p_PortParams->numOfOpenDmas = portOutParams.numOfOpenDmas;
        p_PortParams->numOfExtraOpenDmas = portOutParams.numOfExtraOpenDmas;
        p_PortParams->sizeOfFifo = portOutParams.sizeOfFifo;
        p_PortParams->extraSizeOfFifo = portOutParams.extraSizeOfFifo;

        return (t_Error)(reply.error);
    }

    ASSERT_COND(IN_RANGE(1, hardwarePortId, 63));

    intFlags = XX_LockIntrSpinlock(p_Fm->h_Spinlock);
    if (p_PortParams->independentMode)
    {
        /* set port parameters */
        p_Fm->independentMode = p_PortParams->independentMode;
        /* disable dispatch limit */
        fman_qmi_disable_dispatch_limit(fman_rg.fpm_rg);
    }

    if (p_PortParams->portType == e_FM_PORT_TYPE_OH_HOST_COMMAND)
    {
        if (p_Fm->hcPortInitialized)
        {
            XX_UnlockIntrSpinlock(p_Fm->h_Spinlock, intFlags);
            RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Only one host command port is allowed."));
        }
        else
            p_Fm->hcPortInitialized = TRUE;
    }
    p_Fm->p_FmStateStruct->portsTypes[hardwarePortId] = p_PortParams->portType;

    err = FmSetNumOfTasks(p_Fm, hardwarePortId, &p_PortParams->numOfTasks, &p_PortParams->numOfExtraTasks, TRUE);
    if (err)
    {
        XX_UnlockIntrSpinlock(p_Fm->h_Spinlock, intFlags);
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

#ifdef FM_QMI_NO_DEQ_OPTIONS_SUPPORT
    if (p_Fm->p_FmStateStruct->revInfo.majorRev != 4)
#endif /* FM_QMI_NO_DEQ_OPTIONS_SUPPORT */
    if ((p_PortParams->portType != e_FM_PORT_TYPE_RX) &&
       (p_PortParams->portType != e_FM_PORT_TYPE_RX_10G))
    /* for transmit & O/H ports */
    {
        uint8_t     enqTh;
        uint8_t     deqTh;

        /* update qmi ENQ/DEQ threshold */
        p_Fm->p_FmStateStruct->accumulatedNumOfDeqTnums += p_PortParams->deqPipelineDepth;
        enqTh = fman_get_qmi_enq_th(fman_rg.qmi_rg);
        /* if enqTh is too big, we reduce it to the max value that is still OK */
        if (enqTh >= (QMI_MAX_NUM_OF_TNUMS - p_Fm->p_FmStateStruct->accumulatedNumOfDeqTnums))
        {
            enqTh = (uint8_t)(QMI_MAX_NUM_OF_TNUMS - p_Fm->p_FmStateStruct->accumulatedNumOfDeqTnums - 1);
            fman_set_qmi_enq_th(fman_rg.qmi_rg, enqTh);
        }

        deqTh = fman_get_qmi_deq_th(fman_rg.qmi_rg);
        /* if deqTh is too small, we enlarge it to the min value that is still OK.
         deqTh may not be larger than 63 (QMI_MAX_NUM_OF_TNUMS-1). */
        if ((deqTh <= p_Fm->p_FmStateStruct->accumulatedNumOfDeqTnums)  && (deqTh < QMI_MAX_NUM_OF_TNUMS-1))
        {
            deqTh = (uint8_t)(p_Fm->p_FmStateStruct->accumulatedNumOfDeqTnums + 1);
            fman_set_qmi_deq_th(fman_rg.qmi_rg, deqTh);
        }
    }

#ifdef FM_LOW_END_RESTRICTION
    if ((hardwarePortId==0x1) || (hardwarePortId==0x29))
    {
        if (p_Fm->p_FmStateStruct->lowEndRestriction)
        {
            XX_UnlockIntrSpinlock(p_Fm->h_Spinlock, intFlags);
            RETURN_ERROR(MAJOR, E_NOT_AVAILABLE, ("OP #0 cannot work with Tx Port #1."));
        }
        else
            p_Fm->p_FmStateStruct->lowEndRestriction = TRUE;
    }
#endif /* FM_LOW_END_RESTRICTION */

    err = FmSetSizeOfFifo(p_Fm,
                          hardwarePortId,
                          &p_PortParams->sizeOfFifo,
                          &p_PortParams->extraSizeOfFifo,
                          TRUE);
    if (err)
    {
        XX_UnlockIntrSpinlock(p_Fm->h_Spinlock, intFlags);
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    err = FmSetNumOfOpenDmas(p_Fm,
                             hardwarePortId,
                             &p_PortParams->numOfOpenDmas,
                             &p_PortParams->numOfExtraOpenDmas,
                             TRUE);
    if (err)
    {
        XX_UnlockIntrSpinlock(p_Fm->h_Spinlock, intFlags);
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    fman_set_liodn_per_port(&fman_rg,
                            hardwarePortId,
                            p_PortParams->liodnBase,
                            p_PortParams->liodnOffset);

    if (p_Fm->p_FmStateStruct->revInfo.majorRev < 6)
        fman_set_order_restoration_per_port(fman_rg.fpm_rg,
                                            hardwarePortId,
                                            p_PortParams->independentMode,
                                            !!((p_PortParams->portType==e_FM_PORT_TYPE_RX) || (p_PortParams->portType==e_FM_PORT_TYPE_RX_10G)));

    HW_PORT_ID_TO_SW_PORT_ID(macId, hardwarePortId);

#if defined(FM_MAX_NUM_OF_10G_MACS) && (FM_MAX_NUM_OF_10G_MACS)
    if ((p_PortParams->portType == e_FM_PORT_TYPE_TX_10G) ||
        (p_PortParams->portType == e_FM_PORT_TYPE_RX_10G))
    {
        ASSERT_COND(macId < FM_MAX_NUM_OF_10G_MACS);
        if (p_PortParams->maxFrameLength >= p_Fm->p_FmStateStruct->macMaxFrameLengths10G[macId])
            p_Fm->p_FmStateStruct->portMaxFrameLengths10G[macId] = p_PortParams->maxFrameLength;
        else
            RETURN_ERROR(MINOR, E_INVALID_VALUE, ("Port maxFrameLength is smaller than MAC current MTU"));
    }
    else
#endif /* defined(FM_MAX_NUM_OF_10G_MACS) && ... */
    if ((p_PortParams->portType == e_FM_PORT_TYPE_TX) ||
        (p_PortParams->portType == e_FM_PORT_TYPE_RX))
    {
        ASSERT_COND(macId < FM_MAX_NUM_OF_1G_MACS);
        if (p_PortParams->maxFrameLength >= p_Fm->p_FmStateStruct->macMaxFrameLengths1G[macId])
            p_Fm->p_FmStateStruct->portMaxFrameLengths1G[macId] = p_PortParams->maxFrameLength;
        else
            RETURN_ERROR(MINOR, E_INVALID_VALUE, ("Port maxFrameLength is smaller than MAC current MTU"));
    }

    FmGetPhysicalMuramBase(p_Fm, &p_PortParams->fmMuramPhysBaseAddr);
    XX_UnlockIntrSpinlock(p_Fm->h_Spinlock, intFlags);

    return E_OK;
}

void FmFreePortParams(t_Handle h_Fm,t_FmInterModulePortFreeParams *p_PortParams)
{
    t_Fm                    *p_Fm = (t_Fm*)h_Fm;
    uint32_t                intFlags;
    uint8_t                 hardwarePortId = p_PortParams->hardwarePortId;
    uint8_t                 numOfTasks, numOfDmas, macId;
    uint16_t                sizeOfFifo;
    t_Error                 err;
    t_FmIpcPortFreeParams   portParams;
    t_FmIpcMsg              msg;
    struct fman_qmi_regs *qmi_rg = p_Fm->p_FmQmiRegs;
    struct fman_bmi_regs *bmi_rg = p_Fm->p_FmBmiRegs;

    if (p_Fm->guestId != NCSW_MASTER_ID)
    {
        portParams.hardwarePortId = p_PortParams->hardwarePortId;
        portParams.enumPortType = (uint32_t)p_PortParams->portType;
        portParams.deqPipelineDepth = p_PortParams->deqPipelineDepth;
        memset(&msg, 0, sizeof(msg));
        msg.msgId = FM_FREE_PORT;
        memcpy(msg.msgBody, &portParams, sizeof(portParams));
        err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                (uint8_t*)&msg,
                                sizeof(msg.msgId)+sizeof(portParams),
                                NULL,
                                NULL,
                                NULL,
                                NULL);
        if (err != E_OK)
            REPORT_ERROR(MINOR, err, NO_MSG);
        return;
    }

    ASSERT_COND(IN_RANGE(1, hardwarePortId, 63));

    intFlags = XX_LockIntrSpinlock(p_Fm->h_Spinlock);

    if (p_PortParams->portType == e_FM_PORT_TYPE_OH_HOST_COMMAND)
    {
        ASSERT_COND(p_Fm->hcPortInitialized);
        p_Fm->hcPortInitialized = FALSE;
    }

    p_Fm->p_FmStateStruct->portsTypes[hardwarePortId] = e_FM_PORT_TYPE_DUMMY;

    /* free numOfTasks */
    numOfTasks = fman_get_num_of_tasks(bmi_rg, hardwarePortId);
    ASSERT_COND(p_Fm->p_FmStateStruct->accumulatedNumOfTasks >= numOfTasks);
    p_Fm->p_FmStateStruct->accumulatedNumOfTasks -= numOfTasks;

    /* free numOfOpenDmas */
    numOfDmas = fman_get_num_of_dmas(bmi_rg, hardwarePortId);
    ASSERT_COND(p_Fm->p_FmStateStruct->accumulatedNumOfOpenDmas >= numOfDmas);
    p_Fm->p_FmStateStruct->accumulatedNumOfOpenDmas -= numOfDmas;

#ifdef FM_HAS_TOTAL_DMAS
    if (p_Fm->p_FmStateStruct->revInfo.majorRev < 6)
    {
        /* update total num of DMA's with committed number of open DMAS, and max uncommitted pool. */
        fman_set_num_of_open_dmas(bmi_rg,
                                  hardwarePortId,
                                  1,
                                  0,
                         (uint8_t)(p_Fm->p_FmStateStruct->accumulatedNumOfOpenDmas + p_Fm->p_FmStateStruct->extraOpenDmasPoolSize));
    }
#endif /* FM_HAS_TOTAL_DMAS */

    /* free sizeOfFifo */
    sizeOfFifo = fman_get_size_of_fifo(bmi_rg, hardwarePortId);
    ASSERT_COND(p_Fm->p_FmStateStruct->accumulatedFifoSize >= (sizeOfFifo * BMI_FIFO_UNITS));
    p_Fm->p_FmStateStruct->accumulatedFifoSize -= (sizeOfFifo * BMI_FIFO_UNITS);

#ifdef FM_QMI_NO_DEQ_OPTIONS_SUPPORT
    if (p_Fm->p_FmStateStruct->revInfo.majorRev != 4)
#endif /* FM_QMI_NO_DEQ_OPTIONS_SUPPORT */
    if ((p_PortParams->portType != e_FM_PORT_TYPE_RX) &&
        (p_PortParams->portType != e_FM_PORT_TYPE_RX_10G))
    /* for transmit & O/H ports */
    {
        uint8_t     enqTh;
        uint8_t     deqTh;

        /* update qmi ENQ/DEQ threshold */
        p_Fm->p_FmStateStruct->accumulatedNumOfDeqTnums -= p_PortParams->deqPipelineDepth;

        /* p_Fm->p_FmStateStruct->accumulatedNumOfDeqTnums is now smaller,
           so we can enlarge enqTh */
        enqTh = (uint8_t)(QMI_MAX_NUM_OF_TNUMS - p_Fm->p_FmStateStruct->accumulatedNumOfDeqTnums - 1);

         /* p_Fm->p_FmStateStruct->accumulatedNumOfDeqTnums is now smaller,
            so we can reduce deqTh */
        deqTh = (uint8_t)(p_Fm->p_FmStateStruct->accumulatedNumOfDeqTnums + 1);

        fman_set_qmi_enq_th(qmi_rg, enqTh);
        fman_set_qmi_deq_th(qmi_rg, deqTh);
    }

    HW_PORT_ID_TO_SW_PORT_ID(macId, hardwarePortId);

#if defined(FM_MAX_NUM_OF_10G_MACS) && (FM_MAX_NUM_OF_10G_MACS)
    if ((p_PortParams->portType == e_FM_PORT_TYPE_TX_10G) ||
        (p_PortParams->portType == e_FM_PORT_TYPE_RX_10G))
    {
        ASSERT_COND(macId < FM_MAX_NUM_OF_10G_MACS);
        p_Fm->p_FmStateStruct->portMaxFrameLengths10G[macId] = 0;
    }
    else
#endif /* defined(FM_MAX_NUM_OF_10G_MACS) && ... */
    if ((p_PortParams->portType == e_FM_PORT_TYPE_TX) ||
        (p_PortParams->portType == e_FM_PORT_TYPE_RX))
    {
        ASSERT_COND(macId < FM_MAX_NUM_OF_1G_MACS);
        p_Fm->p_FmStateStruct->portMaxFrameLengths1G[macId] = 0;
    }

#ifdef FM_LOW_END_RESTRICTION
    if ((hardwarePortId==0x1) || (hardwarePortId==0x29))
        p_Fm->p_FmStateStruct->lowEndRestriction = FALSE;
#endif /* FM_LOW_END_RESTRICTION */
    XX_UnlockIntrSpinlock(p_Fm->h_Spinlock, intFlags);
}

t_Error FmIsPortStalled(t_Handle h_Fm, uint8_t hardwarePortId, bool *p_IsStalled)
{
    t_Fm            *p_Fm = (t_Fm*)h_Fm;
    t_Error         err;
    t_FmIpcMsg      msg;
    t_FmIpcReply    reply;
    uint32_t        replyLength;
    struct fman_fpm_regs *fpm_rg = p_Fm->p_FmFpmRegs;

    if ((p_Fm->guestId != NCSW_MASTER_ID) &&
        !p_Fm->baseAddr &&
        p_Fm->h_IpcSessions[0])
    {
        memset(&msg, 0, sizeof(msg));
        memset(&reply, 0, sizeof(reply));
        msg.msgId = FM_IS_PORT_STALLED;
        msg.msgBody[0] = hardwarePortId;
        replyLength = sizeof(uint32_t) + sizeof(uint8_t);
        err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                (uint8_t*)&msg,
                                sizeof(msg.msgId)+sizeof(hardwarePortId),
                                (uint8_t*)&reply,
                                &replyLength,
                                NULL,
                                NULL);
        if (err != E_OK)
            RETURN_ERROR(MINOR, err, NO_MSG);
        if (replyLength != (sizeof(uint32_t) + sizeof(uint8_t)))
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));

        *p_IsStalled = (bool)!!(*(uint8_t*)(reply.replyBody));

        return (t_Error)(reply.error);
    }
    else if (!p_Fm->baseAddr)
        RETURN_ERROR(MINOR, E_NOT_SUPPORTED,
                     ("Either IPC or 'baseAddress' is required!"));

    *p_IsStalled = fman_is_port_stalled(fpm_rg, hardwarePortId);

    return E_OK;
}

t_Error FmResumeStalledPort(t_Handle h_Fm, uint8_t hardwarePortId)
{
    t_Fm            *p_Fm = (t_Fm*)h_Fm;
    t_Error         err;
    bool            isStalled;
    struct fman_fpm_regs *fpm_rg = p_Fm->p_FmFpmRegs;

    if ((p_Fm->guestId != NCSW_MASTER_ID) &&
        !p_Fm->baseAddr &&
        p_Fm->h_IpcSessions[0])
    {
        t_FmIpcMsg      msg;
        t_FmIpcReply    reply;
        uint32_t        replyLength;

        memset(&msg, 0, sizeof(msg));
        memset(&reply, 0, sizeof(reply));
        msg.msgId = FM_RESUME_STALLED_PORT;
        msg.msgBody[0] = hardwarePortId;
        replyLength = sizeof(uint32_t);
        err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                (uint8_t*)&msg,
                                sizeof(msg.msgId) + sizeof(hardwarePortId),
                                (uint8_t*)&reply,
                                &replyLength,
                                NULL,
                                NULL);
        if (err != E_OK)
            RETURN_ERROR(MINOR, err, NO_MSG);
        if (replyLength != sizeof(uint32_t))
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));
        return (t_Error)(reply.error);
    }
    else if (!p_Fm->baseAddr)
        RETURN_ERROR(MINOR, E_NOT_SUPPORTED,
                     ("Either IPC or 'baseAddress' is required!"));

    if (p_Fm->p_FmStateStruct->revInfo.majorRev >= 6)
        RETURN_ERROR(MINOR, E_NOT_AVAILABLE, ("Not available for this FM revision!"));

    /* Get port status */
    err = FmIsPortStalled(h_Fm, hardwarePortId, &isStalled);
    if (err)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Can't get port status"));
    if (!isStalled)
        return E_OK;

    fman_resume_stalled_port(fpm_rg, hardwarePortId);

    return E_OK;
}

t_Error FmResetMac(t_Handle h_Fm, e_FmMacType type, uint8_t macId)
{
    t_Fm                *p_Fm = (t_Fm*)h_Fm;
    t_Error             err;
    struct fman_fpm_regs *fpm_rg = p_Fm->p_FmFpmRegs;

#if (DPAA_VERSION >= 11)
    if (p_Fm->p_FmStateStruct->revInfo.majorRev >= 6)
        RETURN_ERROR(MINOR, E_NOT_SUPPORTED,
                     ("FMan MAC reset!"));
#endif /*(DPAA_VERSION >= 11)*/

    if ((p_Fm->guestId != NCSW_MASTER_ID) &&
        !p_Fm->baseAddr &&
        p_Fm->h_IpcSessions[0])
    {
        t_FmIpcMacParams    macParams;
        t_FmIpcMsg          msg;
        t_FmIpcReply        reply;
        uint32_t            replyLength;

        memset(&msg, 0, sizeof(msg));
        memset(&reply, 0, sizeof(reply));
        macParams.id = macId;
        macParams.enumType = (uint32_t)type;
        msg.msgId = FM_RESET_MAC;
        memcpy(msg.msgBody,  &macParams, sizeof(macParams));
        replyLength = sizeof(uint32_t);
        err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                (uint8_t*)&msg,
                                sizeof(msg.msgId)+sizeof(macParams),
                                (uint8_t*)&reply,
                                &replyLength,
                                NULL,
                                NULL);
        if (err != E_OK)
            RETURN_ERROR(MINOR, err, NO_MSG);
        if (replyLength != sizeof(uint32_t))
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));
        return (t_Error)(reply.error);
    }
    else if (!p_Fm->baseAddr)
        RETURN_ERROR(MINOR, E_NOT_SUPPORTED,
                     ("Either IPC or 'baseAddress' is required!"));

    err = (t_Error)fman_reset_mac(fpm_rg, macId, !!(type == e_FM_MAC_10G));

    if (err == -EBUSY)
        return ERROR_CODE(E_TIMEOUT);
    else if (err)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Illegal MAC ID"));

    return E_OK;
}

t_Error FmSetMacMaxFrame(t_Handle h_Fm, e_FmMacType type, uint8_t macId, uint16_t mtu)
{
    t_Fm                        *p_Fm = (t_Fm*)h_Fm;

    if ((p_Fm->guestId != NCSW_MASTER_ID) &&
        p_Fm->h_IpcSessions[0])
    {
        t_FmIpcMacMaxFrameParams    macMaxFrameLengthParams;
        t_Error                     err;
        t_FmIpcMsg                  msg;

        memset(&msg, 0, sizeof(msg));
        macMaxFrameLengthParams.macParams.id = macId;
        macMaxFrameLengthParams.macParams.enumType = (uint32_t)type;
        macMaxFrameLengthParams.maxFrameLength = (uint16_t)mtu;
        msg.msgId = FM_SET_MAC_MAX_FRAME;
        memcpy(msg.msgBody,  &macMaxFrameLengthParams, sizeof(macMaxFrameLengthParams));
        err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                (uint8_t*)&msg,
                                sizeof(msg.msgId)+sizeof(macMaxFrameLengthParams),
                                NULL,
                                NULL,
                                NULL,
                                NULL);
        if (err != E_OK)
            RETURN_ERROR(MINOR, err, NO_MSG);
        return E_OK;
    }
    else if (p_Fm->guestId != NCSW_MASTER_ID)
        RETURN_ERROR(MINOR, E_NOT_SUPPORTED,
                     ("running in guest-mode without IPC!"));

    /* if port is already initialized, check that MaxFrameLength is smaller
     * or equal to the port's max */
#if (defined(FM_MAX_NUM_OF_10G_MACS) && (FM_MAX_NUM_OF_10G_MACS))
    if (type == e_FM_MAC_10G)
    {
        if ((!p_Fm->p_FmStateStruct->portMaxFrameLengths10G[macId])
           || (p_Fm->p_FmStateStruct->portMaxFrameLengths10G[macId] &&
              (mtu <= p_Fm->p_FmStateStruct->portMaxFrameLengths10G[macId])))
               p_Fm->p_FmStateStruct->macMaxFrameLengths10G[macId] = mtu;
        else
            RETURN_ERROR(MINOR, E_INVALID_VALUE, ("MAC maxFrameLength is larger than Port maxFrameLength"));

    }
    else
#else
    UNUSED(type);
#endif /* (defined(FM_MAX_NUM_OF_10G_MACS) && ... */
    if ((!p_Fm->p_FmStateStruct->portMaxFrameLengths1G[macId])
       || (p_Fm->p_FmStateStruct->portMaxFrameLengths1G[macId] &&
          (mtu <= p_Fm->p_FmStateStruct->portMaxFrameLengths1G[macId])))
        p_Fm->p_FmStateStruct->macMaxFrameLengths1G[macId] = mtu;
    else
        RETURN_ERROR(MINOR, E_INVALID_VALUE, ("MAC maxFrameLength is larger than Port maxFrameLength"));

    return E_OK;
}

uint16_t FmGetClockFreq(t_Handle h_Fm)
{
    t_Fm *p_Fm = (t_Fm*)h_Fm;

    /* for multicore environment: this depends on the
     * fact that fmClkFreq was properly initialized at "init". */
    return p_Fm->p_FmStateStruct->fmClkFreq;
}

uint16_t FmGetMacClockFreq(t_Handle h_Fm)
{
    t_Fm *p_Fm = (t_Fm*)h_Fm;

    return p_Fm->p_FmStateStruct->fmMacClkFreq;
}

uint32_t FmGetTimeStampScale(t_Handle h_Fm)
{
    t_Fm                *p_Fm = (t_Fm*)h_Fm;

    if ((p_Fm->guestId != NCSW_MASTER_ID) &&
        !p_Fm->baseAddr &&
        p_Fm->h_IpcSessions[0])
    {
        t_Error             err;
        t_FmIpcMsg          msg;
        t_FmIpcReply        reply;
        uint32_t            replyLength, timeStamp;

        memset(&msg, 0, sizeof(msg));
        memset(&reply, 0, sizeof(reply));
        msg.msgId = FM_GET_TIMESTAMP_SCALE;
        replyLength = sizeof(uint32_t) + sizeof(uint32_t);
        if ((err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                     (uint8_t*)&msg,
                                     sizeof(msg.msgId),
                                     (uint8_t*)&reply,
                                     &replyLength,
                                     NULL,
                                     NULL)) != E_OK)
        {
            REPORT_ERROR(MAJOR, err, NO_MSG);
            return 0;
        }
        if (replyLength != (sizeof(uint32_t) + sizeof(uint32_t)))
        {
            REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));
            return 0;
        }

        memcpy((uint8_t*)&timeStamp, reply.replyBody, sizeof(uint32_t));
        return timeStamp;
    }
    else if ((p_Fm->guestId != NCSW_MASTER_ID) &&
             p_Fm->baseAddr)
    {
        if (!(GET_UINT32(p_Fm->p_FmFpmRegs->fmfp_tsc1) & FPM_TS_CTL_EN))
        {
            REPORT_ERROR(MAJOR, E_INVALID_STATE, ("timestamp is not enabled!"));
            return 0;
        }
    }
    else if (p_Fm->guestId != NCSW_MASTER_ID)
        DBG(WARNING, ("No IPC - can't validate FM if timestamp enabled."));

    return p_Fm->p_FmStateStruct->count1MicroBit;
}

t_Error FmEnableRamsEcc(t_Handle h_Fm)
{
    t_Fm        *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);

    p_Fm->p_FmStateStruct->ramsEccOwners++;
    p_Fm->p_FmStateStruct->internalCall = TRUE;

    return FM_EnableRamsEcc(p_Fm);
}

t_Error FmDisableRamsEcc(t_Handle h_Fm)
{
    t_Fm        *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);

    ASSERT_COND(p_Fm->p_FmStateStruct->ramsEccOwners);
    p_Fm->p_FmStateStruct->ramsEccOwners--;

    if (p_Fm->p_FmStateStruct->ramsEccOwners==0)
    {
        p_Fm->p_FmStateStruct->internalCall = TRUE;
        return FM_DisableRamsEcc(p_Fm);
    }

    return E_OK;
}

uint8_t FmGetGuestId(t_Handle h_Fm)
{
    t_Fm     *p_Fm = (t_Fm*)h_Fm;

    return p_Fm->guestId;
}

bool FmIsMaster(t_Handle h_Fm)
{
    t_Fm     *p_Fm = (t_Fm*)h_Fm;

    return (p_Fm->guestId == NCSW_MASTER_ID);
}

t_Error FmSetSizeOfFifo(t_Handle    h_Fm,
                        uint8_t     hardwarePortId,
                        uint32_t    *p_SizeOfFifo,
                        uint32_t    *p_ExtraSizeOfFifo,
                        bool        initialConfig)
{
    t_Fm                    *p_Fm = (t_Fm*)h_Fm;
    t_FmIpcPortRsrcParams   rsrcParams;
    t_Error                 err;
    struct fman_bmi_regs    *bmi_rg = p_Fm->p_FmBmiRegs;
    uint32_t                sizeOfFifo = *p_SizeOfFifo, extraSizeOfFifo = *p_ExtraSizeOfFifo;
    uint16_t                currentVal = 0, currentExtraVal = 0;

    if ((p_Fm->guestId != NCSW_MASTER_ID) &&
        !p_Fm->baseAddr &&
        p_Fm->h_IpcSessions[0])
    {
        t_FmIpcMsg          msg;
        t_FmIpcReply        reply;
        uint32_t            replyLength;

        rsrcParams.hardwarePortId = hardwarePortId;
        rsrcParams.val = sizeOfFifo;
        rsrcParams.extra = extraSizeOfFifo;
        rsrcParams.boolInitialConfig = (uint8_t)initialConfig;

        memset(&msg, 0, sizeof(msg));
        memset(&reply, 0, sizeof(reply));
        msg.msgId = FM_SET_SIZE_OF_FIFO;
        memcpy(msg.msgBody, &rsrcParams, sizeof(rsrcParams));
        replyLength = sizeof(uint32_t);
        if ((err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                     (uint8_t*)&msg,
                                     sizeof(msg.msgId) + sizeof(rsrcParams),
                                     (uint8_t*)&reply,
                                     &replyLength,
                                     NULL,
                                     NULL)) != E_OK)
            RETURN_ERROR(MINOR, err, NO_MSG);
        if (replyLength != sizeof(uint32_t))
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));
        return (t_Error)(reply.error);
    }
    else if ((p_Fm->guestId != NCSW_MASTER_ID) &&
             p_Fm->baseAddr)
    {
        DBG(WARNING, ("No IPC - can't validate FM total-fifo size."));
        fman_set_size_of_fifo(bmi_rg, hardwarePortId, sizeOfFifo, extraSizeOfFifo);
    }
    else if (p_Fm->guestId != NCSW_MASTER_ID)
        RETURN_ERROR(MAJOR, E_NOT_SUPPORTED,
                     ("running in guest-mode without neither IPC nor mapped register!"));

    if (!initialConfig)
    {
        /* !initialConfig - runtime change of existing value.
         * - read the current FIFO and extra FIFO size */
        currentExtraVal = fman_get_size_of_extra_fifo(bmi_rg, hardwarePortId);
        currentVal = fman_get_size_of_fifo(bmi_rg, hardwarePortId);
    }

    if (extraSizeOfFifo > currentExtraVal)
    {
        if (extraSizeOfFifo && !p_Fm->p_FmStateStruct->extraFifoPoolSize)
            /* if this is the first time a port requires extraFifoPoolSize, the total extraFifoPoolSize
             * must be initialized to 1 buffer per port
             */
            p_Fm->p_FmStateStruct->extraFifoPoolSize = FM_MAX_NUM_OF_RX_PORTS*BMI_FIFO_UNITS;

        p_Fm->p_FmStateStruct->extraFifoPoolSize = MAX(p_Fm->p_FmStateStruct->extraFifoPoolSize, extraSizeOfFifo);
    }

    /* check that there are enough uncommitted fifo size */
    if ((p_Fm->p_FmStateStruct->accumulatedFifoSize - currentVal + sizeOfFifo) >
        (p_Fm->p_FmStateStruct->totalFifoSize - p_Fm->p_FmStateStruct->extraFifoPoolSize)){
        REPORT_ERROR(MAJOR, E_INVALID_VALUE,
            ("Port request fifo size + accumulated size > total FIFO size:"));
        RETURN_ERROR(MAJOR, E_INVALID_VALUE,
            ("port 0x%x requested %d bytes, extra size = %d, accumulated size = %d total size = %d",
                hardwarePortId, sizeOfFifo, p_Fm->p_FmStateStruct->extraFifoPoolSize,
                p_Fm->p_FmStateStruct->accumulatedFifoSize,
                p_Fm->p_FmStateStruct->totalFifoSize));
    }
    else
    {
        /* update accumulated */
        ASSERT_COND(p_Fm->p_FmStateStruct->accumulatedFifoSize >= currentVal);
        p_Fm->p_FmStateStruct->accumulatedFifoSize -= currentVal;
        p_Fm->p_FmStateStruct->accumulatedFifoSize += sizeOfFifo;
        fman_set_size_of_fifo(bmi_rg, hardwarePortId, sizeOfFifo, extraSizeOfFifo);
    }

    return E_OK;
}

t_Error FmSetNumOfTasks(t_Handle    h_Fm,
                        uint8_t     hardwarePortId,
                        uint8_t     *p_NumOfTasks,
                        uint8_t     *p_NumOfExtraTasks,
                        bool        initialConfig)
{
    t_Fm                    *p_Fm = (t_Fm *)h_Fm;
    t_Error                 err;
    struct fman_bmi_regs    *bmi_rg = p_Fm->p_FmBmiRegs;
    uint8_t                 currentVal = 0, currentExtraVal = 0, numOfTasks = *p_NumOfTasks, numOfExtraTasks = *p_NumOfExtraTasks;

    ASSERT_COND(IN_RANGE(1, hardwarePortId, 63));

    if ((p_Fm->guestId != NCSW_MASTER_ID) &&
        !p_Fm->baseAddr &&
        p_Fm->h_IpcSessions[0])
    {
        t_FmIpcPortRsrcParams   rsrcParams;
        t_FmIpcMsg              msg;
        t_FmIpcReply            reply;
        uint32_t                replyLength;

        rsrcParams.hardwarePortId = hardwarePortId;
        rsrcParams.val = numOfTasks;
        rsrcParams.extra = numOfExtraTasks;
        rsrcParams.boolInitialConfig = (uint8_t)initialConfig;

        memset(&msg, 0, sizeof(msg));
        memset(&reply, 0, sizeof(reply));
        msg.msgId = FM_SET_NUM_OF_TASKS;
        memcpy(msg.msgBody, &rsrcParams, sizeof(rsrcParams));
        replyLength = sizeof(uint32_t);
        if ((err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                     (uint8_t*)&msg,
                                     sizeof(msg.msgId) + sizeof(rsrcParams),
                                     (uint8_t*)&reply,
                                     &replyLength,
                                     NULL,
                                     NULL)) != E_OK)
            RETURN_ERROR(MINOR, err, NO_MSG);
        if (replyLength != sizeof(uint32_t))
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));
        return (t_Error)(reply.error);
    }
    else if ((p_Fm->guestId != NCSW_MASTER_ID) &&
             p_Fm->baseAddr)
    {
        DBG(WARNING, ("No IPC - can't validate FM total-num-of-tasks."));
        fman_set_num_of_tasks(bmi_rg, hardwarePortId, numOfTasks, numOfExtraTasks);
    }
    else if (p_Fm->guestId != NCSW_MASTER_ID)
        RETURN_ERROR(MAJOR, E_NOT_SUPPORTED,
                     ("running in guest-mode without neither IPC nor mapped register!"));

    if (!initialConfig)
    {
        /* !initialConfig - runtime change of existing value.
         * - read the current number of tasks */
        currentVal = fman_get_num_of_tasks(bmi_rg, hardwarePortId);
        currentExtraVal = fman_get_num_extra_tasks(bmi_rg, hardwarePortId);
    }

    if (numOfExtraTasks > currentExtraVal)
         p_Fm->p_FmStateStruct->extraTasksPoolSize =
             (uint8_t)MAX(p_Fm->p_FmStateStruct->extraTasksPoolSize, numOfExtraTasks);

    /* check that there are enough uncommitted tasks */
    if ((p_Fm->p_FmStateStruct->accumulatedNumOfTasks - currentVal + numOfTasks) >
       (p_Fm->p_FmStateStruct->totalNumOfTasks - p_Fm->p_FmStateStruct->extraTasksPoolSize))
        RETURN_ERROR(MAJOR, E_NOT_AVAILABLE,
                     ("Requested numOfTasks and extra tasks pool for fm%d exceed total numOfTasks.",
                      p_Fm->p_FmStateStruct->fmId));
    else
    {
        ASSERT_COND(p_Fm->p_FmStateStruct->accumulatedNumOfTasks >= currentVal);
        /* update accumulated */
        p_Fm->p_FmStateStruct->accumulatedNumOfTasks -= currentVal;
        p_Fm->p_FmStateStruct->accumulatedNumOfTasks += numOfTasks;
        fman_set_num_of_tasks(bmi_rg, hardwarePortId, numOfTasks, numOfExtraTasks);
    }

    return E_OK;
}

t_Error FmSetNumOfOpenDmas(t_Handle h_Fm,
                           uint8_t hardwarePortId,
                           uint8_t *p_NumOfOpenDmas,
                           uint8_t *p_NumOfExtraOpenDmas,
                           bool    initialConfig)

{
    t_Fm                    *p_Fm = (t_Fm *)h_Fm;
    t_Error                 err;
    struct fman_bmi_regs    *bmi_rg = p_Fm->p_FmBmiRegs;
    uint8_t                 numOfOpenDmas = *p_NumOfOpenDmas, numOfExtraOpenDmas = *p_NumOfExtraOpenDmas;
    uint8_t                 totalNumDmas = 0, currentVal = 0, currentExtraVal = 0;

    ASSERT_COND(IN_RANGE(1, hardwarePortId, 63));

    if ((p_Fm->guestId != NCSW_MASTER_ID) &&
        !p_Fm->baseAddr &&
        p_Fm->h_IpcSessions[0])
    {
        t_FmIpcPortRsrcParams   rsrcParams;
        t_FmIpcMsg              msg;
        t_FmIpcReply            reply;
        uint32_t                replyLength;

        rsrcParams.hardwarePortId = hardwarePortId;
        rsrcParams.val = numOfOpenDmas;
        rsrcParams.extra = numOfExtraOpenDmas;
        rsrcParams.boolInitialConfig = (uint8_t)initialConfig;

        memset(&msg, 0, sizeof(msg));
        memset(&reply, 0, sizeof(reply));
        msg.msgId = FM_SET_NUM_OF_OPEN_DMAS;
        memcpy(msg.msgBody, &rsrcParams, sizeof(rsrcParams));
        replyLength = sizeof(uint32_t);
        if ((err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                     (uint8_t*)&msg,
                                     sizeof(msg.msgId) + sizeof(rsrcParams),
                                     (uint8_t*)&reply,
                                     &replyLength,
                                     NULL,
                                     NULL)) != E_OK)
            RETURN_ERROR(MINOR, err, NO_MSG);
        if (replyLength != sizeof(uint32_t))
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));
        return (t_Error)(reply.error);
    }
#ifdef FM_HAS_TOTAL_DMAS
    else if (p_Fm->guestId != NCSW_MASTER_ID)
        RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("running in guest-mode without IPC!"));
#else
    else if ((p_Fm->guestId != NCSW_MASTER_ID) &&
             p_Fm->baseAddr &&
             (p_Fm->p_FmStateStruct->revInfo.majorRev >= 6))
    {
        /*DBG(WARNING, ("No IPC - can't validate FM total-num-of-dmas."));*/

        if (!numOfOpenDmas)
        {
             /* first config without explic it value: Do Nothing - reset value shouldn't be
                changed, read register for port save */
                *p_NumOfOpenDmas = fman_get_num_of_dmas(bmi_rg, hardwarePortId);
                *p_NumOfExtraOpenDmas = fman_get_num_extra_dmas(bmi_rg, hardwarePortId);
        }
        else
            /* whether it is the first time with explicit value, or runtime "set" - write register */
            fman_set_num_of_open_dmas(bmi_rg,
                                   hardwarePortId,
                                   numOfOpenDmas,
                                   numOfExtraOpenDmas,
                                   p_Fm->p_FmStateStruct->accumulatedNumOfOpenDmas + p_Fm->p_FmStateStruct->extraOpenDmasPoolSize);
    }
    else if (p_Fm->guestId != NCSW_MASTER_ID)
        RETURN_ERROR(MAJOR, E_NOT_SUPPORTED,
                     ("running in guest-mode without neither IPC nor mapped register!"));
#endif /* FM_HAS_TOTAL_DMAS */

    if (!initialConfig)
    {
        /* !initialConfig - runtime change of existing value.
         * - read the current number of open Dma's */
        currentExtraVal = fman_get_num_extra_dmas(bmi_rg, hardwarePortId);
        currentVal = fman_get_num_of_dmas(bmi_rg, hardwarePortId);
    }

#ifdef FM_NO_GUARANTEED_RESET_VALUES
    /* it's illegal to be in a state where this is not the first set and no value is specified */
    ASSERT_COND(initialConfig || numOfOpenDmas);
    if (!numOfOpenDmas)
    {
        /* !numOfOpenDmas - first configuration according to values in regs.
         * - read the current number of open Dma's */
        currentExtraVal = fman_get_num_extra_dmas(bmi_rg, hardwarePortId);
        currentVal = fman_get_num_of_dmas(bmi_rg, hardwarePortId);
        /* This is the first configuration and user did not specify value (!numOfOpenDmas),
         * reset values will be used and we just save these values for resource management */
        p_Fm->p_FmStateStruct->extraOpenDmasPoolSize =
                    (uint8_t)MAX(p_Fm->p_FmStateStruct->extraOpenDmasPoolSize, currentExtraVal);
        p_Fm->p_FmStateStruct->accumulatedNumOfOpenDmas += currentVal;
        *p_NumOfOpenDmas = currentVal;
        *p_NumOfExtraOpenDmas = currentExtraVal;
        return E_OK;
    }
#endif /* FM_NO_GUARANTEED_RESET_VALUES */

        if (numOfExtraOpenDmas > currentExtraVal)
             p_Fm->p_FmStateStruct->extraOpenDmasPoolSize =
                 (uint8_t)MAX(p_Fm->p_FmStateStruct->extraOpenDmasPoolSize, numOfExtraOpenDmas);

#ifdef FM_HAS_TOTAL_DMAS
        if ((p_Fm->p_FmStateStruct->revInfo.majorRev < 6) &&
            (p_Fm->p_FmStateStruct->accumulatedNumOfOpenDmas - currentVal + numOfOpenDmas >
                p_Fm->p_FmStateStruct->maxNumOfOpenDmas))
                RETURN_ERROR(MAJOR, E_NOT_AVAILABLE,
                             ("Requested numOfOpenDmas for fm%d exceeds total numOfOpenDmas.",
                             p_Fm->p_FmStateStruct->fmId));
#else
        if ((p_Fm->p_FmStateStruct->revInfo.majorRev >= 6) &&
#ifdef FM_HEAVY_TRAFFIC_SEQUENCER_HANG_ERRATA_FMAN_A006981
            !((p_Fm->p_FmStateStruct->revInfo.majorRev == 6) &&
              (p_Fm->p_FmStateStruct->revInfo.minorRev == 0)) &&
#endif /* FM_HEAVY_TRAFFIC_SEQUENCER_HANG_ERRATA_FMAN_A006981 */
            (p_Fm->p_FmStateStruct->accumulatedNumOfOpenDmas - currentVal + numOfOpenDmas > DMA_THRESH_MAX_COMMQ + 1))
            RETURN_ERROR(MAJOR, E_NOT_AVAILABLE,
                         ("Requested numOfOpenDmas for fm%d exceeds DMA Command queue (%d)",
                          p_Fm->p_FmStateStruct->fmId, DMA_THRESH_MAX_COMMQ+1));
#endif /* FM_HAS_TOTAL_DMAS */
        else
        {
            ASSERT_COND(p_Fm->p_FmStateStruct->accumulatedNumOfOpenDmas >= currentVal);
            /* update acummulated */
            p_Fm->p_FmStateStruct->accumulatedNumOfOpenDmas -= currentVal;
            p_Fm->p_FmStateStruct->accumulatedNumOfOpenDmas += numOfOpenDmas;

#ifdef FM_HAS_TOTAL_DMAS
            if (p_Fm->p_FmStateStruct->revInfo.majorRev < 6)
            totalNumDmas = (uint8_t)(p_Fm->p_FmStateStruct->accumulatedNumOfOpenDmas + p_Fm->p_FmStateStruct->extraOpenDmasPoolSize);
#endif /* FM_HAS_TOTAL_DMAS */
            fman_set_num_of_open_dmas(bmi_rg,
                               hardwarePortId,
                               numOfOpenDmas,
                               numOfExtraOpenDmas,
                               totalNumDmas);
        }

    return E_OK;
}

#if (DPAA_VERSION >= 11)
t_Error FmVSPCheckRelativeProfile(t_Handle        h_Fm,
                                  e_FmPortType    portType,
                                  uint8_t         portId,
                                  uint16_t        relativeProfile)
{
    t_Fm         *p_Fm;
    t_FmSp      *p_FmPcdSp;
    uint8_t     swPortIndex=0, hardwarePortId;

    ASSERT_COND(h_Fm);
    p_Fm = (t_Fm*)h_Fm;

    hardwarePortId = SwPortIdToHwPortId(portType,
                                    portId,
                                    p_Fm->p_FmStateStruct->revInfo.majorRev,
                                    p_Fm->p_FmStateStruct->revInfo.minorRev);
    ASSERT_COND(hardwarePortId);
    HW_PORT_ID_TO_SW_PORT_INDX(swPortIndex, hardwarePortId);

    p_FmPcdSp = p_Fm->p_FmSp;
    ASSERT_COND(p_FmPcdSp);

    if (!p_FmPcdSp->portsMapping[swPortIndex].numOfProfiles)
        RETURN_ERROR(MAJOR, E_INVALID_STATE , ("Port has no allocated profiles"));
    if (relativeProfile >= p_FmPcdSp->portsMapping[swPortIndex].numOfProfiles)
        RETURN_ERROR(MAJOR, E_NOT_IN_RANGE , ("Profile id is out of range"));

    return E_OK;
}

t_Error FmVSPGetAbsoluteProfileId(t_Handle        h_Fm,
                                  e_FmPortType    portType,
                                  uint8_t         portId,
                                  uint16_t        relativeProfile,
                                  uint16_t        *p_AbsoluteId)
{
    t_Fm         *p_Fm;
    t_FmSp      *p_FmPcdSp;
    uint8_t     swPortIndex=0, hardwarePortId;
    t_Error     err;

    ASSERT_COND(h_Fm);
    p_Fm = (t_Fm*)h_Fm;

    err = FmVSPCheckRelativeProfile(h_Fm, portType, portId, relativeProfile);
    if (err != E_OK)
        return err;

    hardwarePortId = SwPortIdToHwPortId(portType,
                                    portId,
                                    p_Fm->p_FmStateStruct->revInfo.majorRev,
                                    p_Fm->p_FmStateStruct->revInfo.minorRev);
    ASSERT_COND(hardwarePortId);
    HW_PORT_ID_TO_SW_PORT_INDX(swPortIndex, hardwarePortId);

    p_FmPcdSp = p_Fm->p_FmSp;
    ASSERT_COND(p_FmPcdSp);

    *p_AbsoluteId = (uint16_t)(p_FmPcdSp->portsMapping[swPortIndex].profilesBase + relativeProfile);

    return E_OK;
}
#endif /* (DPAA_VERSION >= 11) */

static t_Error InitFmDma(t_Fm *p_Fm)
{
    t_Error err;

    err = (t_Error)fman_dma_init(p_Fm->p_FmDmaRegs, p_Fm->p_FmDriverParam);
    if (err != E_OK)
        return err;

    /* Allocate MURAM for CAM */
    p_Fm->camBaseAddr = PTR_TO_UINT(FM_MURAM_AllocMem(p_Fm->h_FmMuram,
                                                      (uint32_t)(p_Fm->p_FmDriverParam->dma_cam_num_of_entries*DMA_CAM_SIZEOF_ENTRY),
                                                      DMA_CAM_ALIGN));
    if (!p_Fm->camBaseAddr)
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("MURAM alloc for DMA CAM failed"));

    WRITE_BLOCK(UINT_TO_PTR(p_Fm->camBaseAddr),
                0,
                (uint32_t)(p_Fm->p_FmDriverParam->dma_cam_num_of_entries*DMA_CAM_SIZEOF_ENTRY));

    if (p_Fm->p_FmStateStruct->revInfo.majorRev == 2)
    {
        FM_MURAM_FreeMem(p_Fm->h_FmMuram, UINT_TO_PTR(p_Fm->camBaseAddr));

        p_Fm->camBaseAddr = PTR_TO_UINT(FM_MURAM_AllocMem(p_Fm->h_FmMuram,
                                                          (uint32_t)(p_Fm->p_FmDriverParam->dma_cam_num_of_entries*72 + 128),
                                                          64));
        if (!p_Fm->camBaseAddr)
            RETURN_ERROR(MAJOR, E_NO_MEMORY, ("MURAM alloc for DMA CAM failed"));

        WRITE_BLOCK(UINT_TO_PTR(p_Fm->camBaseAddr),
                   0,
               (uint32_t)(p_Fm->p_FmDriverParam->dma_cam_num_of_entries*72 + 128));

        switch(p_Fm->p_FmDriverParam->dma_cam_num_of_entries)
        {
            case (8):
                WRITE_UINT32(*(uint32_t*)p_Fm->camBaseAddr, 0xff000000);
                break;
            case (16):
                WRITE_UINT32(*(uint32_t*)p_Fm->camBaseAddr, 0xffff0000);
                break;
            case (24):
                WRITE_UINT32(*(uint32_t*)p_Fm->camBaseAddr, 0xffffff00);
                break;
            case (32):
                WRITE_UINT32(*(uint32_t*)p_Fm->camBaseAddr, 0xffffffff);
                break;
            default:
                RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("wrong dma_cam_num_of_entries"));
        }
    }

    p_Fm->p_FmDriverParam->cam_base_addr =
                 (uint32_t)(XX_VirtToPhys(UINT_TO_PTR(p_Fm->camBaseAddr)) - p_Fm->fmMuramPhysBaseAddr);

    return E_OK;
}

static t_Error InitFmFpm(t_Fm *p_Fm)
{
    return (t_Error)fman_fpm_init(p_Fm->p_FmFpmRegs, p_Fm->p_FmDriverParam);
}

static t_Error InitFmBmi(t_Fm *p_Fm)
{
    return (t_Error)fman_bmi_init(p_Fm->p_FmBmiRegs, p_Fm->p_FmDriverParam);
}

static t_Error InitFmQmi(t_Fm *p_Fm)
{
    return (t_Error)fman_qmi_init(p_Fm->p_FmQmiRegs, p_Fm->p_FmDriverParam);
}

static t_Error InitGuestMode(t_Fm *p_Fm)
{
    t_Error                 err = E_OK;
    int                     i;
    t_FmIpcMsg              msg;
    t_FmIpcReply            reply;
    uint32_t                replyLength;

    ASSERT_COND(p_Fm);
    ASSERT_COND(p_Fm->guestId != NCSW_MASTER_ID);

    /* build the FM guest partition IPC address */
    if (Sprint (p_Fm->fmModuleName, "FM_%d_%d",p_Fm->p_FmStateStruct->fmId, p_Fm->guestId) != (p_Fm->guestId<10 ? 6:7))
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Sprint failed"));

    /* build the FM master partition IPC address */
    memset(p_Fm->fmIpcHandlerModuleName, 0, (sizeof(char)) * MODULE_NAME_SIZE);
    if (Sprint (p_Fm->fmIpcHandlerModuleName[0], "FM_%d_%d",p_Fm->p_FmStateStruct->fmId, NCSW_MASTER_ID) != 6)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Sprint failed"));

    for (i=0;i<e_FM_EV_DUMMY_LAST;i++)
        p_Fm->intrMng[i].f_Isr = UnimplementedIsr;

    p_Fm->h_IpcSessions[0] = XX_IpcInitSession(p_Fm->fmIpcHandlerModuleName[0], p_Fm->fmModuleName);
    if (p_Fm->h_IpcSessions[0])
    {
        uint8_t                 isMasterAlive;
        t_FmIpcParams           ipcParams;

        err = XX_IpcRegisterMsgHandler(p_Fm->fmModuleName, FmGuestHandleIpcMsgCB, p_Fm, FM_IPC_MAX_REPLY_SIZE);
        if (err)
            RETURN_ERROR(MAJOR, err, NO_MSG);

        memset(&msg, 0, sizeof(msg));
        memset(&reply, 0, sizeof(reply));
        msg.msgId = FM_MASTER_IS_ALIVE;
        msg.msgBody[0] = p_Fm->guestId;
        replyLength = sizeof(uint32_t) + sizeof(uint8_t);
        do
        {
            blockingFlag = TRUE;
            if ((err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                         (uint8_t*)&msg,
                                         sizeof(msg.msgId)+sizeof(p_Fm->guestId),
                                         (uint8_t*)&reply,
                                         &replyLength,
                                         IpcMsgCompletionCB,
                                         p_Fm)) != E_OK)
                REPORT_ERROR(MINOR, err, NO_MSG);
            while (blockingFlag) ;
            if (replyLength != (sizeof(uint32_t) + sizeof(uint8_t)))
                REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));
            isMasterAlive = *(uint8_t*)(reply.replyBody);
        } while (!isMasterAlive);

        /* read FM parameters and save */
        memset(&msg, 0, sizeof(msg));
        memset(&reply, 0, sizeof(reply));
        msg.msgId = FM_GET_PARAMS;
        replyLength = sizeof(uint32_t) + sizeof(t_FmIpcParams);
        if ((err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                     (uint8_t*)&msg,
                                     sizeof(msg.msgId),
                                     (uint8_t*)&reply,
                                     &replyLength,
                                     NULL,
                                     NULL)) != E_OK)
            RETURN_ERROR(MAJOR, err, NO_MSG);
        if (replyLength != (sizeof(uint32_t) + sizeof(t_FmIpcParams)))
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));
        memcpy((uint8_t*)&ipcParams, reply.replyBody, sizeof(t_FmIpcParams));

        p_Fm->p_FmStateStruct->fmClkFreq = ipcParams.fmClkFreq;
        p_Fm->p_FmStateStruct->fmMacClkFreq = ipcParams.fmMacClkFreq;
        p_Fm->p_FmStateStruct->revInfo.majorRev = ipcParams.majorRev;
        p_Fm->p_FmStateStruct->revInfo.minorRev = ipcParams.minorRev;
    }
    else
    {
        DBG(WARNING, ("FM Guest mode - without IPC"));
        if (!p_Fm->p_FmStateStruct->fmClkFreq)
            RETURN_ERROR(MAJOR, E_INVALID_STATE, ("No fmClkFreq configured for guest without IPC"));
        if (p_Fm->baseAddr)
        {
            fman_get_revision(p_Fm->p_FmFpmRegs,
                              &p_Fm->p_FmStateStruct->revInfo.majorRev,
                              &p_Fm->p_FmStateStruct->revInfo.minorRev);

        }
    }

#if (DPAA_VERSION >= 11)
    p_Fm->partVSPBase = AllocVSPsForPartition(p_Fm, p_Fm->partVSPBase, p_Fm->partNumOfVSPs, p_Fm->guestId);
    if (p_Fm->partVSPBase == (uint8_t)(ILLEGAL_BASE))
        DBG(WARNING, ("partition VSPs allocation is FAILED"));
#endif /* (DPAA_VERSION >= 11) */

    /* General FM driver initialization */
    if (p_Fm->baseAddr)
        p_Fm->fmMuramPhysBaseAddr =
            (uint64_t)(XX_VirtToPhys(UINT_TO_PTR(p_Fm->baseAddr + FM_MM_MURAM)));

    XX_Free(p_Fm->p_FmDriverParam);
    p_Fm->p_FmDriverParam = NULL;

    if ((p_Fm->guestId == NCSW_MASTER_ID) ||
        (p_Fm->h_IpcSessions[0]))
    {
        FM_DisableRamsEcc(p_Fm);
        FmMuramClear(p_Fm->h_FmMuram);
        FM_EnableRamsEcc(p_Fm);
    }

    return E_OK;
}

static __inline__ enum fman_exceptions FmanExceptionTrans(e_FmExceptions exception)
{
    switch (exception) {
            case  e_FM_EX_DMA_BUS_ERROR:
                return E_FMAN_EX_DMA_BUS_ERROR;
            case  e_FM_EX_DMA_READ_ECC:
                return E_FMAN_EX_DMA_READ_ECC;
            case  e_FM_EX_DMA_SYSTEM_WRITE_ECC:
                return E_FMAN_EX_DMA_SYSTEM_WRITE_ECC;
            case  e_FM_EX_DMA_FM_WRITE_ECC:
                return E_FMAN_EX_DMA_FM_WRITE_ECC;
            case  e_FM_EX_FPM_STALL_ON_TASKS:
                return E_FMAN_EX_FPM_STALL_ON_TASKS;
            case  e_FM_EX_FPM_SINGLE_ECC:
                return E_FMAN_EX_FPM_SINGLE_ECC;
            case  e_FM_EX_FPM_DOUBLE_ECC:
                return E_FMAN_EX_FPM_DOUBLE_ECC;
            case  e_FM_EX_QMI_SINGLE_ECC:
                return E_FMAN_EX_QMI_SINGLE_ECC;
            case  e_FM_EX_QMI_DOUBLE_ECC:
                return E_FMAN_EX_QMI_DOUBLE_ECC;
            case  e_FM_EX_QMI_DEQ_FROM_UNKNOWN_PORTID:
                return E_FMAN_EX_QMI_DEQ_FROM_UNKNOWN_PORTID;
            case  e_FM_EX_BMI_LIST_RAM_ECC:
                return E_FMAN_EX_BMI_LIST_RAM_ECC;
            case  e_FM_EX_BMI_STORAGE_PROFILE_ECC:
                return E_FMAN_EX_BMI_STORAGE_PROFILE_ECC;
            case  e_FM_EX_BMI_STATISTICS_RAM_ECC:
                return E_FMAN_EX_BMI_STATISTICS_RAM_ECC;
            case  e_FM_EX_BMI_DISPATCH_RAM_ECC:
                return E_FMAN_EX_BMI_DISPATCH_RAM_ECC;
            case  e_FM_EX_IRAM_ECC:
                return E_FMAN_EX_IRAM_ECC;
            case  e_FM_EX_MURAM_ECC:
                return E_FMAN_EX_MURAM_ECC;
            default:
                return E_FMAN_EX_DMA_BUS_ERROR;
        }
}

uint8_t SwPortIdToHwPortId(e_FmPortType type, uint8_t relativePortId, uint8_t majorRev, uint8_t minorRev)
{
	switch (type)
	{
		case (e_FM_PORT_TYPE_OH_OFFLINE_PARSING):
		case (e_FM_PORT_TYPE_OH_HOST_COMMAND):
			CHECK_PORT_ID_OH_PORTS(relativePortId);
			return (uint8_t)(BASE_OH_PORTID + (relativePortId));
		case (e_FM_PORT_TYPE_RX):
			CHECK_PORT_ID_1G_RX_PORTS(relativePortId);
			return (uint8_t)(BASE_1G_RX_PORTID + (relativePortId));
		case (e_FM_PORT_TYPE_RX_10G):
                       /* The 10G port in T1024 (FMan Version 6.4) is the first port.
                        * This is the reason why the 1G port offset is used.
                        */
                       if (majorRev == 6 && minorRev == 4)
                       {
                               CHECK_PORT_ID_1G_RX_PORTS(relativePortId);
                               return (uint8_t)(BASE_1G_RX_PORTID + (relativePortId));
                       }
                       else
                       {
                               CHECK_PORT_ID_10G_RX_PORTS(relativePortId);
                               return (uint8_t)(BASE_10G_RX_PORTID + (relativePortId));
                       }
		case (e_FM_PORT_TYPE_TX):
			CHECK_PORT_ID_1G_TX_PORTS(relativePortId);
			return (uint8_t)(BASE_1G_TX_PORTID + (relativePortId));
		case (e_FM_PORT_TYPE_TX_10G):
                       /* The 10G port in T1024 (FMan Version 6.4) is the first port.
                        * This is the reason why the 1G port offset is used.
                        */
                       if (majorRev == 6 && minorRev == 4)
                       {
                               CHECK_PORT_ID_1G_TX_PORTS(relativePortId);
                               return (uint8_t)(BASE_1G_TX_PORTID + (relativePortId));
                       }
                       else
                       {
                               CHECK_PORT_ID_10G_TX_PORTS(relativePortId);
                               return (uint8_t)(BASE_10G_TX_PORTID + (relativePortId));
                       }
		default:
			REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("Illegal port type"));
			return 0;
	}
}

#if (defined(DEBUG_ERRORS) && (DEBUG_ERRORS > 0))
t_Error FmDumpPortRegs (t_Handle h_Fm, uint8_t hardwarePortId)
{
    t_Fm            *p_Fm = (t_Fm *)h_Fm;

    DECLARE_DUMP;

    ASSERT_COND(IN_RANGE(1, hardwarePortId, 63));

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(((p_Fm->guestId == NCSW_MASTER_ID) ||
                               p_Fm->baseAddr), E_INVALID_OPERATION);

    DUMP_TITLE(&p_Fm->p_FmBmiRegs->fmbm_pp[hardwarePortId-1], ("fmbm_pp for port %u", (hardwarePortId)));
    DUMP_MEMORY(&p_Fm->p_FmBmiRegs->fmbm_pp[hardwarePortId-1], sizeof(uint32_t));

    DUMP_TITLE(&p_Fm->p_FmBmiRegs->fmbm_pfs[hardwarePortId-1], ("fmbm_pfs for port %u", (hardwarePortId )));
    DUMP_MEMORY(&p_Fm->p_FmBmiRegs->fmbm_pfs[hardwarePortId-1], sizeof(uint32_t));

    DUMP_TITLE(&p_Fm->p_FmBmiRegs->fmbm_spliodn[hardwarePortId-1], ("fmbm_spliodn for port %u", (hardwarePortId)));
    DUMP_MEMORY(&p_Fm->p_FmBmiRegs->fmbm_spliodn[hardwarePortId-1], sizeof(uint32_t));

    DUMP_TITLE(&p_Fm->p_FmFpmRegs->fmfp_ps[hardwarePortId], ("fmfp_ps for port %u", (hardwarePortId)));
    DUMP_MEMORY(&p_Fm->p_FmFpmRegs->fmfp_ps[hardwarePortId], sizeof(uint32_t));

    DUMP_TITLE(&p_Fm->p_FmDmaRegs->fmdmplr[hardwarePortId/2], ("fmdmplr for port %u", (hardwarePortId)));
    DUMP_MEMORY(&p_Fm->p_FmDmaRegs->fmdmplr[hardwarePortId/2], sizeof(uint32_t));

    return E_OK;
}
#endif /* (defined(DEBUG_ERRORS) && (DEBUG_ERRORS > 0)) */


/*****************************************************************************/
/*                      API Init unit functions                              */
/*****************************************************************************/
t_Handle FM_Config(t_FmParams *p_FmParam)
{
    t_Fm                *p_Fm;
    uint8_t             i;
    uintptr_t           baseAddr;

    SANITY_CHECK_RETURN_VALUE(p_FmParam, E_NULL_POINTER, NULL);
    SANITY_CHECK_RETURN_VALUE(((p_FmParam->firmware.p_Code && p_FmParam->firmware.size) ||
                               (!p_FmParam->firmware.p_Code && !p_FmParam->firmware.size)),
                              E_INVALID_VALUE, NULL);

    baseAddr = p_FmParam->baseAddr;

    /* Allocate FM structure */
    p_Fm = (t_Fm *) XX_Malloc(sizeof(t_Fm));
    if (!p_Fm)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM driver structure"));
        return NULL;
    }
    memset(p_Fm, 0, sizeof(t_Fm));

    p_Fm->p_FmStateStruct = (t_FmStateStruct *) XX_Malloc(sizeof(t_FmStateStruct));
    if (!p_Fm->p_FmStateStruct)
    {
        XX_Free(p_Fm);
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM Status structure"));
        return NULL;
    }
    memset(p_Fm->p_FmStateStruct, 0, sizeof(t_FmStateStruct));

    /* Initialize FM parameters which will be kept by the driver */
    p_Fm->p_FmStateStruct->fmId = p_FmParam->fmId;
    p_Fm->guestId               = p_FmParam->guestId;

    for (i=0; i<FM_MAX_NUM_OF_HW_PORT_IDS; i++)
        p_Fm->p_FmStateStruct->portsTypes[i] = e_FM_PORT_TYPE_DUMMY;

    /* Allocate the FM driver's parameters structure */
    p_Fm->p_FmDriverParam = (struct fman_cfg *)XX_Malloc(sizeof(struct fman_cfg));
    if (!p_Fm->p_FmDriverParam)
    {
        XX_Free(p_Fm->p_FmStateStruct);
        XX_Free(p_Fm);
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM driver parameters"));
        return NULL;
    }
    memset(p_Fm->p_FmDriverParam, 0, sizeof(struct fman_cfg));

#if (DPAA_VERSION >= 11)
    p_Fm->p_FmSp = (t_FmSp *)XX_Malloc(sizeof(t_FmSp));
    if (!p_Fm->p_FmSp)
    {
        XX_Free(p_Fm->p_FmDriverParam);
        XX_Free(p_Fm->p_FmStateStruct);
        XX_Free(p_Fm);
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("allocation for internal data structure failed"));
        return NULL;
    }
    memset(p_Fm->p_FmSp, 0, sizeof(t_FmSp));

    for (i=0; i<FM_VSP_MAX_NUM_OF_ENTRIES; i++)
        p_Fm->p_FmSp->profiles[i].profilesMng.ownerId = (uint8_t)ILLEGAL_BASE;
#endif /* (DPAA_VERSION >= 11) */

    /* Initialize FM parameters which will be kept by the driver */
    p_Fm->p_FmStateStruct->fmId                 = p_FmParam->fmId;
    p_Fm->h_FmMuram                             = p_FmParam->h_FmMuram;
    p_Fm->h_App                                 = p_FmParam->h_App;
    p_Fm->p_FmStateStruct->fmClkFreq            = p_FmParam->fmClkFreq;
    p_Fm->p_FmStateStruct->fmMacClkFreq         = p_FmParam->fmClkFreq / ((!p_FmParam->fmMacClkRatio)? 2: p_FmParam->fmMacClkRatio);
    p_Fm->f_Exception                           = p_FmParam->f_Exception;
    p_Fm->f_BusError                            = p_FmParam->f_BusError;
    p_Fm->p_FmFpmRegs = (struct fman_fpm_regs *)UINT_TO_PTR(baseAddr + FM_MM_FPM);
    p_Fm->p_FmBmiRegs = (struct fman_bmi_regs *)UINT_TO_PTR(baseAddr + FM_MM_BMI);
    p_Fm->p_FmQmiRegs = (struct fman_qmi_regs *)UINT_TO_PTR(baseAddr + FM_MM_QMI);
    p_Fm->p_FmDmaRegs = (struct fman_dma_regs *)UINT_TO_PTR(baseAddr + FM_MM_DMA);
    p_Fm->p_FmRegs       = (struct fman_regs *)UINT_TO_PTR(baseAddr + FM_MM_BMI);
    p_Fm->baseAddr                              = baseAddr;
    p_Fm->p_FmStateStruct->irq                  = p_FmParam->irq;
    p_Fm->p_FmStateStruct->errIrq               = p_FmParam->errIrq;
    p_Fm->hcPortInitialized                     = FALSE;
    p_Fm->independentMode                       = FALSE;

    p_Fm->h_Spinlock = XX_InitSpinlock();
    if (!p_Fm->h_Spinlock)
    {
        XX_Free(p_Fm->p_FmDriverParam);
        XX_Free(p_Fm->p_FmStateStruct);
        XX_Free(p_Fm);
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("can't allocate spinlock!"));
        return NULL;
    }

#if (DPAA_VERSION >= 11)
    p_Fm->partVSPBase   = p_FmParam->partVSPBase;
    p_Fm->partNumOfVSPs = p_FmParam->partNumOfVSPs;
    p_Fm->vspBaseAddr = p_FmParam->vspBaseAddr;
#endif /* (DPAA_VERSION >= 11) */

    fman_defconfig(p_Fm->p_FmDriverParam,
                  !!(p_Fm->guestId == NCSW_MASTER_ID));
/* overide macros dependent parameters */
#ifdef FM_PEDANTIC_DMA
    p_Fm->p_FmDriverParam->pedantic_dma = TRUE;
    p_Fm->p_FmDriverParam->dma_aid_override = TRUE;
#endif /* FM_PEDANTIC_DMA */
#ifndef FM_QMI_NO_DEQ_OPTIONS_SUPPORT
    p_Fm->p_FmDriverParam->qmi_deq_option_support = TRUE;
#endif /* !FM_QMI_NO_DEQ_OPTIONS_SUPPORT */

    p_Fm->p_FmStateStruct->ramsEccEnable        = FALSE;
    p_Fm->p_FmStateStruct->extraFifoPoolSize    = 0;
    p_Fm->p_FmStateStruct->exceptions           = DEFAULT_exceptions;
    p_Fm->resetOnInit                          = DEFAULT_resetOnInit;
    p_Fm->f_ResetOnInitOverride                = DEFAULT_resetOnInitOverrideCallback;
    p_Fm->fwVerify                             = DEFAULT_VerifyUcode;
    p_Fm->firmware.size                        = p_FmParam->firmware.size;
    if (p_Fm->firmware.size)
    {
        p_Fm->firmware.p_Code = (uint32_t *)XX_Malloc(p_Fm->firmware.size);
        if (!p_Fm->firmware.p_Code)
        {
            XX_FreeSpinlock(p_Fm->h_Spinlock);
            XX_Free(p_Fm->p_FmStateStruct);
            XX_Free(p_Fm->p_FmDriverParam);
            XX_Free(p_Fm);
            REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM firmware code"));
            return NULL;
        }
        memcpy(p_Fm->firmware.p_Code, p_FmParam->firmware.p_Code ,p_Fm->firmware.size);
    }

    if (p_Fm->guestId != NCSW_MASTER_ID)
        return p_Fm;

    /* read revision */
    /* Chip dependent, will be configured in Init */
    fman_get_revision(p_Fm->p_FmFpmRegs,
                      &p_Fm->p_FmStateStruct->revInfo.majorRev,
                      &p_Fm->p_FmStateStruct->revInfo.minorRev);

#ifdef FM_AID_MODE_NO_TNUM_SW005
    if (p_Fm->p_FmStateStruct->revInfo.majorRev >= 6)
        p_Fm->p_FmDriverParam->dma_aid_mode = e_FM_DMA_AID_OUT_PORT_ID;
#endif /* FM_AID_MODE_NO_TNUM_SW005 */
#ifndef FM_QMI_NO_DEQ_OPTIONS_SUPPORT
   if (p_Fm->p_FmStateStruct->revInfo.majorRev != 4)
        p_Fm->p_FmDriverParam->qmi_def_tnums_thresh = QMI_DEF_TNUMS_THRESH;
#endif /* FM_QMI_NO_DEQ_OPTIONS_SUPPORT */

        p_Fm->p_FmStateStruct->totalFifoSize        = 0;
        p_Fm->p_FmStateStruct->totalNumOfTasks      = 
            DEFAULT_totalNumOfTasks(p_Fm->p_FmStateStruct->revInfo.majorRev,
                                    p_Fm->p_FmStateStruct->revInfo.minorRev);

#ifdef FM_HAS_TOTAL_DMAS
        p_Fm->p_FmStateStruct->maxNumOfOpenDmas     = BMI_MAX_NUM_OF_DMAS;
#endif /* FM_HAS_TOTAL_DMAS */
#if (DPAA_VERSION < 11)
        p_Fm->p_FmDriverParam->dma_comm_qtsh_clr_emer        = DEFAULT_dmaCommQLow;
        p_Fm->p_FmDriverParam->dma_comm_qtsh_asrt_emer       = DEFAULT_dmaCommQHigh;
        p_Fm->p_FmDriverParam->dma_cam_num_of_entries        = DEFAULT_dmaCamNumOfEntries;
        p_Fm->p_FmDriverParam->dma_read_buf_tsh_clr_emer      = DEFAULT_dmaReadIntBufLow;
        p_Fm->p_FmDriverParam->dma_read_buf_tsh_asrt_emer     = DEFAULT_dmaReadIntBufHigh;
        p_Fm->p_FmDriverParam->dma_write_buf_tsh_clr_emer     = DEFAULT_dmaWriteIntBufLow;
        p_Fm->p_FmDriverParam->dma_write_buf_tsh_asrt_emer    = DEFAULT_dmaWriteIntBufHigh;
        p_Fm->p_FmDriverParam->dma_axi_dbg_num_of_beats       = DEFAULT_axiDbgNumOfBeats;
#endif /* (DPAA_VERSION < 11) */
#ifdef FM_NO_TNUM_AGING
    p_Fm->p_FmDriverParam->tnum_aging_period = 0;
#endif
    p_Fm->tnumAgingPeriod = p_Fm->p_FmDriverParam->tnum_aging_period;

   return p_Fm;
}

/**************************************************************************//**
 @Function      FM_Init

 @Description   Initializes the FM module

 @Param[in]     h_Fm - FM module descriptor

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error FM_Init(t_Handle h_Fm)
{
    t_Fm                    *p_Fm = (t_Fm*)h_Fm;
    struct fman_cfg         *p_FmDriverParam = NULL;
    t_Error                 err = E_OK;
    int                     i;
    t_FmRevisionInfo        revInfo;
    struct fman_rg          fman_rg;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);

    fman_rg.bmi_rg = p_Fm->p_FmBmiRegs;
    fman_rg.qmi_rg = p_Fm->p_FmQmiRegs;
    fman_rg.fpm_rg = p_Fm->p_FmFpmRegs;
    fman_rg.dma_rg = p_Fm->p_FmDmaRegs;

    p_Fm->p_FmStateStruct->count1MicroBit = FM_TIMESTAMP_1_USEC_BIT;
    p_Fm->p_FmDriverParam->num_of_fman_ctrl_evnt_regs = FM_NUM_OF_FMAN_CTRL_EVENT_REGS;

    if (p_Fm->guestId != NCSW_MASTER_ID)
        return InitGuestMode(p_Fm);

    /* if user didn't configured totalFifoSize - (totalFifoSize=0) we configure default
    * according to chip. otherwise, we use user's configuration.
    */
    if (p_Fm->p_FmStateStruct->totalFifoSize == 0)
        p_Fm->p_FmStateStruct->totalFifoSize = DEFAULT_totalFifoSize(p_Fm->p_FmStateStruct->revInfo.majorRev,
                                                                     p_Fm->p_FmStateStruct->revInfo.minorRev);

    CHECK_INIT_PARAMETERS(p_Fm, CheckFmParameters);

    p_FmDriverParam = p_Fm->p_FmDriverParam;

    FM_GetRevision(p_Fm, &revInfo);

    /* clear revision-dependent non existing exception */
#ifdef FM_NO_DISPATCH_RAM_ECC
    if ((revInfo.majorRev != 4) &&
        (revInfo.majorRev < 6))
        p_Fm->p_FmStateStruct->exceptions &= ~FM_EX_BMI_DISPATCH_RAM_ECC;
#endif /* FM_NO_DISPATCH_RAM_ECC */

#ifdef FM_QMI_NO_ECC_EXCEPTIONS
    if (revInfo.majorRev == 4)
        p_Fm->p_FmStateStruct->exceptions &= ~(FM_EX_QMI_SINGLE_ECC | FM_EX_QMI_DOUBLE_ECC);
#endif /* FM_QMI_NO_ECC_EXCEPTIONS */

#ifdef FM_QMI_NO_SINGLE_ECC_EXCEPTION
    if (revInfo.majorRev >= 6)
       p_Fm->p_FmStateStruct->exceptions &= ~FM_EX_QMI_SINGLE_ECC;
#endif /* FM_QMI_NO_SINGLE_ECC_EXCEPTION */

    FmMuramClear(p_Fm->h_FmMuram);

    /* clear CPG */
    IOMemSet32(UINT_TO_PTR(p_Fm->baseAddr + FM_MM_CGP), 0, FM_PORT_NUM_OF_CONGESTION_GRPS);

    /* add to the default exceptions the user's definitions */
    p_Fm->p_FmStateStruct->exceptions |= p_Fm->userSetExceptions;

    /* Reset the FM if required */
    if (p_Fm->resetOnInit)
    {
#ifdef FM_UCODE_NOT_RESET_ERRATA_BUGZILLA6173
        if ((err = FwNotResetErratumBugzilla6173WA(p_Fm)) != E_OK)
            RETURN_ERROR(MAJOR, err, NO_MSG);
#else  /* not FM_UCODE_NOT_RESET_ERRATA_BUGZILLA6173 */

        if (p_Fm->f_ResetOnInitOverride)
        {
        	/* Perform user specific FMan reset */
        	p_Fm->f_ResetOnInitOverride(h_Fm);
        }
        else
        {
        	/* Perform FMan reset */
        	FmReset(h_Fm);
        }

        if (fman_is_qmi_halt_not_busy_state(p_Fm->p_FmQmiRegs))
        {
            fman_resume(p_Fm->p_FmFpmRegs);
            XX_UDelay(100);
        }
#endif /* not FM_UCODE_NOT_RESET_ERRATA_BUGZILLA6173 */
    }

#ifdef FM_UCODE_NOT_RESET_ERRATA_BUGZILLA6173
    if (!p_Fm->resetOnInit) /* Skip operations done in errata workaround */
    {
#endif /* FM_UCODE_NOT_RESET_ERRATA_BUGZILLA6173 */
    /* Load FMan-Controller code to IRAM */

    ClearIRam(p_Fm);

    if (p_Fm->firmware.p_Code && (LoadFmanCtrlCode(p_Fm) != E_OK))
        RETURN_ERROR(MAJOR, E_INVALID_STATE, NO_MSG);
#ifdef FM_UCODE_NOT_RESET_ERRATA_BUGZILLA6173
    }
#endif /* FM_UCODE_NOT_RESET_ERRATA_BUGZILLA6173 */

#ifdef FM_CAPWAP_SUPPORT
    /* save first 256 byte in MURAM */
    p_Fm->resAddr = PTR_TO_UINT(FM_MURAM_AllocMem(p_Fm->h_FmMuram, 256, 0));
    if (!p_Fm->resAddr)
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("MURAM alloc for reserved Area failed"));

    WRITE_BLOCK(UINT_TO_PTR(p_Fm->resAddr), 0, 256);
#endif /* FM_CAPWAP_SUPPORT */

#if (DPAA_VERSION >= 11)
    p_Fm->partVSPBase = AllocVSPsForPartition(h_Fm, p_Fm->partVSPBase, p_Fm->partNumOfVSPs, p_Fm->guestId);
    if (p_Fm->partVSPBase == (uint8_t)(ILLEGAL_BASE))
        DBG(WARNING, ("partition VSPs allocation is FAILED"));
#endif /* (DPAA_VERSION >= 11) */

    /* General FM driver initialization */
    p_Fm->fmMuramPhysBaseAddr =
        (uint64_t)(XX_VirtToPhys(UINT_TO_PTR(p_Fm->baseAddr + FM_MM_MURAM)));

    for (i=0;i<e_FM_EV_DUMMY_LAST;i++)
        p_Fm->intrMng[i].f_Isr = UnimplementedIsr;
    for (i=0;i<FM_NUM_OF_FMAN_CTRL_EVENT_REGS;i++)
        p_Fm->fmanCtrlIntr[i].f_Isr = UnimplementedFmanCtrlIsr;

    p_FmDriverParam->exceptions = p_Fm->p_FmStateStruct->exceptions;

    /**********************/
    /* Init DMA Registers */
    /**********************/
    err = InitFmDma(p_Fm);
    if (err != E_OK)
    {
        FreeInitResources(p_Fm);
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    /**********************/
    /* Init FPM Registers */
    /**********************/
    err = InitFmFpm(p_Fm);
    if (err != E_OK)
    {
        FreeInitResources(p_Fm);
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    /* define common resources */
    /* allocate MURAM for FIFO according to total size */
    p_Fm->fifoBaseAddr = PTR_TO_UINT(FM_MURAM_AllocMem(p_Fm->h_FmMuram,
                                                       p_Fm->p_FmStateStruct->totalFifoSize,
                                                       BMI_FIFO_ALIGN));
    if (!p_Fm->fifoBaseAddr)
    {
        FreeInitResources(p_Fm);
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("MURAM alloc for BMI FIFO failed"));
    }

    p_FmDriverParam->fifo_base_addr = (uint32_t)(XX_VirtToPhys(UINT_TO_PTR(p_Fm->fifoBaseAddr)) - p_Fm->fmMuramPhysBaseAddr);
    p_FmDriverParam->total_fifo_size = p_Fm->p_FmStateStruct->totalFifoSize;
    p_FmDriverParam->total_num_of_tasks = p_Fm->p_FmStateStruct->totalNumOfTasks;
    p_FmDriverParam->clk_freq = p_Fm->p_FmStateStruct->fmClkFreq;

    /**********************/
    /* Init BMI Registers */
    /**********************/
    err = InitFmBmi(p_Fm);
    if (err != E_OK)
    {
        FreeInitResources(p_Fm);
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    /**********************/
    /* Init QMI Registers */
    /**********************/
    err = InitFmQmi(p_Fm);
    if (err != E_OK)
    {
        FreeInitResources(p_Fm);
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    /* build the FM master partition IPC address */
    if (Sprint (p_Fm->fmModuleName, "FM_%d_%d",p_Fm->p_FmStateStruct->fmId, NCSW_MASTER_ID) != 6)
    {
        FreeInitResources(p_Fm);
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Sprint failed"));
    }

    err = XX_IpcRegisterMsgHandler(p_Fm->fmModuleName, FmHandleIpcMsgCB, p_Fm, FM_IPC_MAX_REPLY_SIZE);
    if (err)
    {
        FreeInitResources(p_Fm);
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    /* Register the FM interrupts handlers */
    if (p_Fm->p_FmStateStruct->irq != NO_IRQ)
    {
        XX_SetIntr(p_Fm->p_FmStateStruct->irq, FM_EventIsr, p_Fm);
        XX_EnableIntr(p_Fm->p_FmStateStruct->irq);
    }

    if (p_Fm->p_FmStateStruct->errIrq != NO_IRQ)
    {
        XX_SetIntr(p_Fm->p_FmStateStruct->errIrq, (void (*) (t_Handle))FM_ErrorIsr, p_Fm);
        XX_EnableIntr(p_Fm->p_FmStateStruct->errIrq);
    }

    err = (t_Error)fman_enable(&fman_rg , p_FmDriverParam);
    if (err != E_OK)
        return err; /* FIXME */

    EnableTimeStamp(p_Fm);

    if (p_Fm->firmware.p_Code)
    {
        XX_Free(p_Fm->firmware.p_Code);
        p_Fm->firmware.p_Code = NULL;
    }

    XX_Free(p_Fm->p_FmDriverParam);
    p_Fm->p_FmDriverParam = NULL;

    return E_OK;
}

/**************************************************************************//**
 @Function      FM_Free

 @Description   Frees all resources that were assigned to FM module.

                Calling this routine invalidates the descriptor.

 @Param[in]     h_Fm - FM module descriptor

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error FM_Free(t_Handle h_Fm)
{
    t_Fm    *p_Fm = (t_Fm*)h_Fm;
    struct fman_rg          fman_rg;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);

    fman_rg.bmi_rg = p_Fm->p_FmBmiRegs;
    fman_rg.qmi_rg = p_Fm->p_FmQmiRegs;
    fman_rg.fpm_rg = p_Fm->p_FmFpmRegs;
    fman_rg.dma_rg = p_Fm->p_FmDmaRegs;

    if (p_Fm->guestId != NCSW_MASTER_ID)
    {
#if (DPAA_VERSION >= 11)
        FreeVSPsForPartition(h_Fm, p_Fm->partVSPBase, p_Fm->partNumOfVSPs, p_Fm->guestId);

        if (p_Fm->p_FmSp)
        {
            XX_Free(p_Fm->p_FmSp);
            p_Fm->p_FmSp = NULL;
        }
#endif /* (DPAA_VERSION >= 11) */

        if (p_Fm->fmModuleName[0] != 0)
            XX_IpcUnregisterMsgHandler(p_Fm->fmModuleName);

        if (!p_Fm->recoveryMode)
            XX_Free(p_Fm->p_FmStateStruct);

        XX_Free(p_Fm);

        return E_OK;
    }

    fman_free_resources(&fman_rg);

    if ((p_Fm->guestId == NCSW_MASTER_ID) && (p_Fm->fmModuleName[0] != 0))
        XX_IpcUnregisterMsgHandler(p_Fm->fmModuleName);

    if (p_Fm->p_FmStateStruct)
    {
        if (p_Fm->p_FmStateStruct->irq != NO_IRQ)
        {
            XX_DisableIntr(p_Fm->p_FmStateStruct->irq);
            XX_FreeIntr(p_Fm->p_FmStateStruct->irq);
        }
        if (p_Fm->p_FmStateStruct->errIrq != NO_IRQ)
        {
            XX_DisableIntr(p_Fm->p_FmStateStruct->errIrq);
            XX_FreeIntr(p_Fm->p_FmStateStruct->errIrq);
        }
    }

#if (DPAA_VERSION >= 11)
    FreeVSPsForPartition(h_Fm, p_Fm->partVSPBase, p_Fm->partNumOfVSPs, p_Fm->guestId);

    if (p_Fm->p_FmSp)
    {
        XX_Free(p_Fm->p_FmSp);
        p_Fm->p_FmSp = NULL;
    }
#endif /* (DPAA_VERSION >= 11) */

    if (p_Fm->h_Spinlock)
        XX_FreeSpinlock(p_Fm->h_Spinlock);

    if (p_Fm->p_FmDriverParam)
    {
        if (p_Fm->firmware.p_Code)
            XX_Free(p_Fm->firmware.p_Code);
        XX_Free(p_Fm->p_FmDriverParam);
        p_Fm->p_FmDriverParam = NULL;
    }

    FreeInitResources(p_Fm);

    if (!p_Fm->recoveryMode && p_Fm->p_FmStateStruct)
        XX_Free(p_Fm->p_FmStateStruct);

    XX_Free(p_Fm);

    return E_OK;
}

/*************************************************/
/*       API Advanced Init unit functions        */
/*************************************************/

t_Error FM_ConfigResetOnInit(t_Handle h_Fm, bool enable)
{
    t_Fm *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    p_Fm->resetOnInit = enable;

    return E_OK;
}

t_Error FM_ConfigResetOnInitOverrideCallback(t_Handle h_Fm, t_FmResetOnInitOverrideCallback *f_ResetOnInitOverride)
{
    t_Fm *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    p_Fm->f_ResetOnInitOverride = f_ResetOnInitOverride;

    return E_OK;
}

t_Error FM_ConfigTotalFifoSize(t_Handle h_Fm, uint32_t totalFifoSize)
{
    t_Fm *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    p_Fm->p_FmStateStruct->totalFifoSize = totalFifoSize;

    return E_OK;
}

t_Error FM_ConfigDmaCacheOverride(t_Handle h_Fm, e_FmDmaCacheOverride cacheOverride)
{
    t_Fm                            *p_Fm = (t_Fm*)h_Fm;
    enum fman_dma_cache_override    fsl_cache_override;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    FMAN_CACHE_OVERRIDE_TRANS(fsl_cache_override, cacheOverride)
    p_Fm->p_FmDriverParam->dma_cache_override = fsl_cache_override;

    return E_OK;
}

t_Error FM_ConfigDmaAidOverride(t_Handle h_Fm, bool aidOverride)
{
    t_Fm *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    p_Fm->p_FmDriverParam->dma_aid_override = aidOverride;

    return E_OK;
}

t_Error FM_ConfigDmaAidMode(t_Handle h_Fm, e_FmDmaAidMode aidMode)
{
    t_Fm                    *p_Fm = (t_Fm*)h_Fm;
    enum fman_dma_aid_mode  fsl_aid_mode;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    FMAN_AID_MODE_TRANS(fsl_aid_mode, aidMode);
    p_Fm->p_FmDriverParam->dma_aid_mode = fsl_aid_mode;

    return E_OK;
}

t_Error FM_ConfigDmaAxiDbgNumOfBeats(t_Handle h_Fm, uint8_t axiDbgNumOfBeats)
{
    t_Fm *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

#if (DPAA_VERSION >= 11)
    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, ("Not available for this FM revision!"));
#else
    p_Fm->p_FmDriverParam->dma_axi_dbg_num_of_beats = axiDbgNumOfBeats;

    return E_OK;
#endif /* (DPAA_VERSION >= 11) */
}

t_Error FM_ConfigDmaCamNumOfEntries(t_Handle h_Fm, uint8_t numOfEntries)
{
    t_Fm *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    p_Fm->p_FmDriverParam->dma_cam_num_of_entries = numOfEntries;

    return E_OK;
}

t_Error FM_ConfigDmaDbgCounter(t_Handle h_Fm, e_FmDmaDbgCntMode fmDmaDbgCntMode)
{
    t_Fm                        *p_Fm = (t_Fm*)h_Fm;
    enum fman_dma_dbg_cnt_mode  fsl_dma_dbg_cnt;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    FMAN_DMA_DBG_CNT_TRANS(fsl_dma_dbg_cnt, fmDmaDbgCntMode);
    p_Fm->p_FmDriverParam->dma_dbg_cnt_mode = fsl_dma_dbg_cnt;

    return E_OK;
}

t_Error FM_ConfigDmaStopOnBusErr(t_Handle h_Fm, bool stop)
{
    t_Fm *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    p_Fm->p_FmDriverParam->dma_stop_on_bus_error = stop;

    return E_OK;
}

t_Error FM_ConfigDmaEmergency(t_Handle h_Fm, t_FmDmaEmergency *p_Emergency)
{
    t_Fm                            *p_Fm = (t_Fm*)h_Fm;
    enum fman_dma_emergency_level   fsl_dma_emer;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    FMAN_DMA_EMER_TRANS(fsl_dma_emer, p_Emergency->emergencyLevel);
    p_Fm->p_FmDriverParam->dma_en_emergency = TRUE;
    p_Fm->p_FmDriverParam->dma_emergency_bus_select = (uint32_t)p_Emergency->emergencyBusSelect;
    p_Fm->p_FmDriverParam->dma_emergency_level = fsl_dma_emer;

    return E_OK;
}

t_Error FM_ConfigDmaEmergencySmoother(t_Handle h_Fm, uint32_t emergencyCnt)
{
    t_Fm *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    p_Fm->p_FmDriverParam->dma_en_emergency_smoother = TRUE;
    p_Fm->p_FmDriverParam->dma_emergency_switch_counter = emergencyCnt;

    return E_OK;
}

t_Error FM_ConfigDmaErr(t_Handle h_Fm, e_FmDmaErr dmaErr)
{
    t_Fm                *p_Fm = (t_Fm*)h_Fm;
    enum fman_dma_err   fsl_dma_err;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    FMAN_DMA_ERR_TRANS(fsl_dma_err, dmaErr);
    p_Fm->p_FmDriverParam->dma_err = fsl_dma_err;

    return E_OK;
}

t_Error FM_ConfigCatastrophicErr(t_Handle h_Fm, e_FmCatastrophicErr catastrophicErr)
{
    t_Fm                        *p_Fm = (t_Fm*)h_Fm;
    enum fman_catastrophic_err  fsl_catastrophic_err;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    FMAN_CATASTROPHIC_ERR_TRANS(fsl_catastrophic_err, catastrophicErr);
    p_Fm->p_FmDriverParam->catastrophic_err = fsl_catastrophic_err;

    return E_OK;
}

t_Error FM_ConfigEnableMuramTestMode(t_Handle h_Fm)
{
    t_Fm *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    if (p_Fm->p_FmStateStruct->revInfo.majorRev >= 6)
        RETURN_ERROR(MINOR, E_NOT_SUPPORTED, ("Not available for this FM revision!"));

    p_Fm->p_FmDriverParam->en_muram_test_mode = TRUE;

    return E_OK;
}

t_Error FM_ConfigEnableIramTestMode(t_Handle h_Fm)
{
    t_Fm *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE );
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    if (p_Fm->p_FmStateStruct->revInfo.majorRev >= 6)
        RETURN_ERROR(MINOR, E_NOT_SUPPORTED, ("Not available for this FM revision!"));

    p_Fm->p_FmDriverParam->en_iram_test_mode = TRUE;

    return E_OK;
}

t_Error FM_ConfigHaltOnExternalActivation(t_Handle h_Fm, bool enable)
{
    t_Fm *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    p_Fm->p_FmDriverParam->halt_on_external_activ = enable;

    return E_OK;
}

t_Error FM_ConfigHaltOnUnrecoverableEccError(t_Handle h_Fm, bool enable)
{
    t_Fm *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    if (p_Fm->p_FmStateStruct->revInfo.majorRev >= 6)
        RETURN_ERROR(MINOR, E_NOT_SUPPORTED, ("Not available for this FM revision!"));

    p_Fm->p_FmDriverParam->halt_on_unrecov_ecc_err = enable;

    return E_OK;
}

t_Error FM_ConfigException(t_Handle h_Fm, e_FmExceptions exception, bool enable)
{
    t_Fm                *p_Fm = (t_Fm*)h_Fm;
    uint32_t            bitMask = 0;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    GET_EXCEPTION_FLAG(bitMask, exception);
    if (bitMask)
    {
        if (enable)
            p_Fm->userSetExceptions |= bitMask;
        else
            p_Fm->p_FmStateStruct->exceptions &= ~bitMask;
    }
    else
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Undefined exception"));

    return E_OK;
}

t_Error FM_ConfigExternalEccRamsEnable(t_Handle h_Fm, bool enable)
{
    t_Fm        *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    p_Fm->p_FmDriverParam->external_ecc_rams_enable = enable;

    return E_OK;
}

t_Error FM_ConfigTnumAgingPeriod(t_Handle h_Fm, uint16_t tnumAgingPeriod)
{
    t_Fm             *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    p_Fm->p_FmDriverParam->tnum_aging_period = tnumAgingPeriod;
    p_Fm->tnumAgingPeriod = p_Fm->p_FmDriverParam->tnum_aging_period;

    return E_OK;
}

/****************************************************/
/*       Hidden-DEBUG Only API                      */
/****************************************************/

t_Error FM_ConfigThresholds(t_Handle h_Fm, t_FmThresholds *p_FmThresholds)
{
    t_Fm *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    p_Fm->p_FmDriverParam->disp_limit_tsh  = p_FmThresholds->dispLimit;
    p_Fm->p_FmDriverParam->prs_disp_tsh    = p_FmThresholds->prsDispTh;
    p_Fm->p_FmDriverParam->plcr_disp_tsh   = p_FmThresholds->plcrDispTh;
    p_Fm->p_FmDriverParam->kg_disp_tsh     = p_FmThresholds->kgDispTh;
    p_Fm->p_FmDriverParam->bmi_disp_tsh    = p_FmThresholds->bmiDispTh;
    p_Fm->p_FmDriverParam->qmi_enq_disp_tsh = p_FmThresholds->qmiEnqDispTh;
    p_Fm->p_FmDriverParam->qmi_deq_disp_tsh = p_FmThresholds->qmiDeqDispTh;
    p_Fm->p_FmDriverParam->fm_ctl1_disp_tsh = p_FmThresholds->fmCtl1DispTh;
    p_Fm->p_FmDriverParam->fm_ctl2_disp_tsh = p_FmThresholds->fmCtl2DispTh;

    return E_OK;
}

t_Error FM_ConfigDmaSosEmergencyThreshold(t_Handle h_Fm, uint32_t dmaSosEmergency)
{
    t_Fm *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    p_Fm->p_FmDriverParam->dma_sos_emergency = dmaSosEmergency;

    return E_OK;
}

t_Error FM_ConfigDmaWriteBufThresholds(t_Handle h_Fm, t_FmDmaThresholds *p_FmDmaThresholds)

{
    t_Fm *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

#if (DPAA_VERSION >= 11)
    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, ("Not available for this FM revision!"));
#else
    p_Fm->p_FmDriverParam->dma_write_buf_tsh_asrt_emer = p_FmDmaThresholds->assertEmergency;
    p_Fm->p_FmDriverParam->dma_write_buf_tsh_clr_emer  = p_FmDmaThresholds->clearEmergency;

    return E_OK;
#endif
}

t_Error FM_ConfigDmaCommQThresholds(t_Handle h_Fm, t_FmDmaThresholds *p_FmDmaThresholds)
{
    t_Fm *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    p_Fm->p_FmDriverParam->dma_comm_qtsh_asrt_emer    = p_FmDmaThresholds->assertEmergency;
    p_Fm->p_FmDriverParam->dma_comm_qtsh_clr_emer     = p_FmDmaThresholds->clearEmergency;

    return E_OK;
}

t_Error FM_ConfigDmaReadBufThresholds(t_Handle h_Fm, t_FmDmaThresholds *p_FmDmaThresholds)
{
    t_Fm *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

#if (DPAA_VERSION >= 11)
    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, ("Not available for this FM revision!"));
#else
    p_Fm->p_FmDriverParam->dma_read_buf_tsh_clr_emer   = p_FmDmaThresholds->clearEmergency;
    p_Fm->p_FmDriverParam->dma_read_buf_tsh_asrt_emer  = p_FmDmaThresholds->assertEmergency;

    return E_OK;
#endif
}

t_Error FM_ConfigDmaWatchdog(t_Handle h_Fm, uint32_t watchdogValue)
{
    t_Fm                *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    p_Fm->p_FmDriverParam->dma_watchdog = watchdogValue;

    return E_OK;
}

t_Error FM_ConfigEnableCounters(t_Handle h_Fm)
{
    t_Fm                *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
UNUSED(p_Fm);

    return E_OK;
}

t_Error FmGetSetParams(t_Handle h_Fm, t_FmGetSetParams *p_Params)
{
	t_Fm* p_Fm = (t_Fm*)h_Fm;
	if (p_Params->setParams.type & UPDATE_FM_CLD)
	{
		WRITE_UINT32(p_Fm->p_FmFpmRegs->fm_cld, GET_UINT32(
				p_Fm->p_FmFpmRegs->fm_cld) | 0x00000800);
	}
	if (p_Params->setParams.type & CLEAR_IRAM_READY)
	{
	    t_FMIramRegs *p_Iram = (t_FMIramRegs *)UINT_TO_PTR(p_Fm->baseAddr + FM_MM_IMEM);
		WRITE_UINT32(p_Iram->iready,GET_UINT32(p_Iram->iready) & ~IRAM_READY);
	}
	if (p_Params->setParams.type & UPDATE_FPM_EXTC)
		WRITE_UINT32(p_Fm->p_FmFpmRegs->fmfp_extc,0x80000000);
	if (p_Params->setParams.type & UPDATE_FPM_EXTC_CLEAR)
		WRITE_UINT32(p_Fm->p_FmFpmRegs->fmfp_extc,0x00800000);
	if (p_Params->setParams.type & UPDATE_FPM_BRKC_SLP)
	{	
		if (p_Params->setParams.sleep)
			WRITE_UINT32(p_Fm->p_FmFpmRegs->fmfp_brkc, GET_UINT32(
				p_Fm->p_FmFpmRegs->fmfp_brkc) | FPM_BRKC_SLP);
		else
			WRITE_UINT32(p_Fm->p_FmFpmRegs->fmfp_brkc, GET_UINT32(
				p_Fm->p_FmFpmRegs->fmfp_brkc) & ~FPM_BRKC_SLP);
	}
	if (p_Params->getParams.type & GET_FM_CLD)
		p_Params->getParams.fm_cld = GET_UINT32(p_Fm->p_FmFpmRegs->fm_cld);
	if (p_Params->getParams.type & GET_FMQM_GS)
		p_Params->getParams.fmqm_gs = GET_UINT32(p_Fm->p_FmQmiRegs->fmqm_gs);
	if (p_Params->getParams.type & GET_FM_NPI)
		p_Params->getParams.fm_npi = GET_UINT32(p_Fm->p_FmFpmRegs->fm_npi);
	if (p_Params->getParams.type & GET_FMFP_EXTC)
		p_Params->getParams.fmfp_extc = GET_UINT32(p_Fm->p_FmFpmRegs->fmfp_extc);
	return E_OK;
}


/****************************************************/
/*       API Run-time Control uint functions        */
/****************************************************/
void FM_EventIsr(t_Handle h_Fm)
{
#define FM_M_CALL_1G_MAC_ISR(_id)    \
    {                                \
        if (p_Fm->guestId != p_Fm->intrMng[(e_FmInterModuleEvent)(e_FM_EV_1G_MAC0+_id)].guestId)    \
            SendIpcIsr(p_Fm, (e_FmInterModuleEvent)(e_FM_EV_1G_MAC0+_id), pending);                 \
        else                                                                                        \
            p_Fm->intrMng[(e_FmInterModuleEvent)(e_FM_EV_1G_MAC0+_id)].f_Isr(p_Fm->intrMng[(e_FmInterModuleEvent)(e_FM_EV_1G_MAC0+_id)].h_SrcHandle);\
    }
#define FM_M_CALL_10G_MAC_ISR(_id)   \
    {                                \
        if (p_Fm->guestId != p_Fm->intrMng[(e_FmInterModuleEvent)(e_FM_EV_10G_MAC0+_id)].guestId)    \
            SendIpcIsr(p_Fm, (e_FmInterModuleEvent)(e_FM_EV_10G_MAC0+_id), pending);                 \
        else                                                                                         \
            p_Fm->intrMng[(e_FmInterModuleEvent)(e_FM_EV_10G_MAC0+_id)].f_Isr(p_Fm->intrMng[(e_FmInterModuleEvent)(e_FM_EV_10G_MAC0+_id)].h_SrcHandle);\
    }
    t_Fm                    *p_Fm = (t_Fm*)h_Fm;
    uint32_t                pending, event;
    struct fman_fpm_regs *fpm_rg;

    SANITY_CHECK_RETURN(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN(!p_Fm->p_FmDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    fpm_rg = p_Fm->p_FmFpmRegs;

    /* normal interrupts */
    pending = fman_get_normal_pending(fpm_rg);
    if (!pending)
        return;
    if (pending & INTR_EN_WAKEUP) // this is a wake up from sleep interrupt
    {
        t_FmGetSetParams fmGetSetParams;
        memset(&fmGetSetParams, 0, sizeof (t_FmGetSetParams));
        fmGetSetParams.setParams.type = UPDATE_FPM_BRKC_SLP;
        fmGetSetParams.setParams.sleep = 0;
        FmGetSetParams(h_Fm, &fmGetSetParams);
    }
    if (pending & INTR_EN_QMI)
        QmiEvent(p_Fm);
    if (pending & INTR_EN_PRS)
        p_Fm->intrMng[e_FM_EV_PRS].f_Isr(p_Fm->intrMng[e_FM_EV_PRS].h_SrcHandle);
    if (pending & INTR_EN_PLCR)
        p_Fm->intrMng[e_FM_EV_PLCR].f_Isr(p_Fm->intrMng[e_FM_EV_PLCR].h_SrcHandle);
    if (pending & INTR_EN_TMR)
            p_Fm->intrMng[e_FM_EV_TMR].f_Isr(p_Fm->intrMng[e_FM_EV_TMR].h_SrcHandle);

    /* MAC events may belong to different partitions */
    if (pending & INTR_EN_1G_MAC0)
        FM_M_CALL_1G_MAC_ISR(0);
    if (pending & INTR_EN_1G_MAC1)
        FM_M_CALL_1G_MAC_ISR(1);
    if (pending & INTR_EN_1G_MAC2)
        FM_M_CALL_1G_MAC_ISR(2);
    if (pending & INTR_EN_1G_MAC3)
        FM_M_CALL_1G_MAC_ISR(3);
    if (pending & INTR_EN_1G_MAC4)
        FM_M_CALL_1G_MAC_ISR(4);
    if (pending & INTR_EN_1G_MAC5)
        FM_M_CALL_1G_MAC_ISR(5);
    if (pending & INTR_EN_1G_MAC6)
        FM_M_CALL_1G_MAC_ISR(6);
    if (pending & INTR_EN_1G_MAC7)
        FM_M_CALL_1G_MAC_ISR(7);
    if (pending & INTR_EN_10G_MAC0)
        FM_M_CALL_10G_MAC_ISR(0);
    if (pending & INTR_EN_10G_MAC1)
        FM_M_CALL_10G_MAC_ISR(1);

    /* IM port events may belong to different partitions */
    if (pending & INTR_EN_REV0)
    {
        event = fman_get_controller_event(fpm_rg, 0);
        if (p_Fm->guestId != p_Fm->intrMng[e_FM_EV_FMAN_CTRL_0].guestId)
            /*TODO IPC ISR For Fman Ctrl */
            ASSERT_COND(0);
            /* SendIpcIsr(p_Fm, e_FM_EV_FMAN_CTRL_0, pending); */
        else
            p_Fm->fmanCtrlIntr[0].f_Isr(p_Fm->fmanCtrlIntr[0].h_SrcHandle, event);

    }
    if (pending & INTR_EN_REV1)
    {
        event = fman_get_controller_event(fpm_rg, 1);
        if (p_Fm->guestId != p_Fm->intrMng[e_FM_EV_FMAN_CTRL_1].guestId)
            /*TODO IPC ISR For Fman Ctrl */
            ASSERT_COND(0);
            /* SendIpcIsr(p_Fm, e_FM_EV_FMAN_CTRL_1, pending); */
        else
            p_Fm->fmanCtrlIntr[1].f_Isr(p_Fm->fmanCtrlIntr[1].h_SrcHandle, event);
    }
    if (pending & INTR_EN_REV2)
    {
        event = fman_get_controller_event(fpm_rg, 2);
        if (p_Fm->guestId != p_Fm->intrMng[e_FM_EV_FMAN_CTRL_2].guestId)
            /*TODO IPC ISR For Fman Ctrl */
            ASSERT_COND(0);
            /* SendIpcIsr(p_Fm, e_FM_EV_FMAN_CTRL_2, pending); */
        else
           p_Fm->fmanCtrlIntr[2].f_Isr(p_Fm->fmanCtrlIntr[2].h_SrcHandle, event);
    }
    if (pending & INTR_EN_REV3)
    {
        event = fman_get_controller_event(fpm_rg, 3);
        if (p_Fm->guestId != p_Fm->intrMng[e_FM_EV_FMAN_CTRL_3].guestId)
            /*TODO IPC ISR For Fman Ctrl */
            ASSERT_COND(0);
            /* SendIpcIsr(p_Fm, e_FM_EV_FMAN_CTRL_2, pendin3); */
        else
            p_Fm->fmanCtrlIntr[3].f_Isr(p_Fm->fmanCtrlIntr[3].h_SrcHandle, event);
    }
#ifdef FM_MACSEC_SUPPORT
    if (pending & INTR_EN_MACSEC_MAC0)
    {
       if (p_Fm->guestId != p_Fm->intrMng[e_FM_EV_MACSEC_MAC0].guestId)
            SendIpcIsr(p_Fm, e_FM_EV_MACSEC_MAC0, pending);
        else
            p_Fm->intrMng[e_FM_EV_MACSEC_MAC0].f_Isr(p_Fm->intrMng[e_FM_EV_MACSEC_MAC0].h_SrcHandle);
    }
#endif /* FM_MACSEC_SUPPORT */
}

t_Error FM_ErrorIsr(t_Handle h_Fm)
{
#define FM_M_CALL_1G_MAC_ERR_ISR(_id)   \
    {                                   \
       if (p_Fm->guestId != p_Fm->intrMng[(e_FmInterModuleEvent)(e_FM_EV_ERR_1G_MAC0+_id)].guestId) \
            SendIpcIsr(p_Fm, (e_FmInterModuleEvent)(e_FM_EV_ERR_1G_MAC0+_id), pending);             \
       else                                                                                         \
            p_Fm->intrMng[(e_FmInterModuleEvent)(e_FM_EV_ERR_1G_MAC0+_id)].f_Isr(p_Fm->intrMng[(e_FmInterModuleEvent)(e_FM_EV_ERR_1G_MAC0+_id)].h_SrcHandle);\
    }
#define FM_M_CALL_10G_MAC_ERR_ISR(_id)   \
    {                                    \
       if (p_Fm->guestId != p_Fm->intrMng[(e_FmInterModuleEvent)(e_FM_EV_ERR_10G_MAC0+_id)].guestId) \
            SendIpcIsr(p_Fm, (e_FmInterModuleEvent)(e_FM_EV_ERR_10G_MAC0+_id), pending);             \
       else                                                                                          \
            p_Fm->intrMng[(e_FmInterModuleEvent)(e_FM_EV_ERR_10G_MAC0+_id)].f_Isr(p_Fm->intrMng[(e_FmInterModuleEvent)(e_FM_EV_ERR_10G_MAC0+_id)].h_SrcHandle);\
    }
    t_Fm                    *p_Fm = (t_Fm*)h_Fm;
    uint32_t                pending;
    struct fman_fpm_regs *fpm_rg;

    SANITY_CHECK_RETURN_ERROR(h_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Fm->p_FmDriverParam, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    fpm_rg = p_Fm->p_FmFpmRegs;

    /* error interrupts */
    pending = fman_get_fpm_error_interrupts(fpm_rg);
    if (!pending)
        return ERROR_CODE(E_EMPTY);

    if (pending & ERR_INTR_EN_BMI)
        BmiErrEvent(p_Fm);
    if (pending & ERR_INTR_EN_QMI)
        QmiErrEvent(p_Fm);
    if (pending & ERR_INTR_EN_FPM)
        FpmErrEvent(p_Fm);
    if (pending & ERR_INTR_EN_DMA)
        DmaErrEvent(p_Fm);
    if (pending & ERR_INTR_EN_IRAM)
        IramErrIntr(p_Fm);
    if (pending & ERR_INTR_EN_MURAM)
        MuramErrIntr(p_Fm);
    if (pending & ERR_INTR_EN_PRS)
        p_Fm->intrMng[e_FM_EV_ERR_PRS].f_Isr(p_Fm->intrMng[e_FM_EV_ERR_PRS].h_SrcHandle);
    if (pending & ERR_INTR_EN_PLCR)
        p_Fm->intrMng[e_FM_EV_ERR_PLCR].f_Isr(p_Fm->intrMng[e_FM_EV_ERR_PLCR].h_SrcHandle);
    if (pending & ERR_INTR_EN_KG)
        p_Fm->intrMng[e_FM_EV_ERR_KG].f_Isr(p_Fm->intrMng[e_FM_EV_ERR_KG].h_SrcHandle);

    /* MAC events may belong to different partitions */
    if (pending & ERR_INTR_EN_1G_MAC0)
        FM_M_CALL_1G_MAC_ERR_ISR(0);
    if (pending & ERR_INTR_EN_1G_MAC1)
        FM_M_CALL_1G_MAC_ERR_ISR(1);
    if (pending & ERR_INTR_EN_1G_MAC2)
        FM_M_CALL_1G_MAC_ERR_ISR(2);
    if (pending & ERR_INTR_EN_1G_MAC3)
        FM_M_CALL_1G_MAC_ERR_ISR(3);
    if (pending & ERR_INTR_EN_1G_MAC4)
        FM_M_CALL_1G_MAC_ERR_ISR(4);
    if (pending & ERR_INTR_EN_1G_MAC5)
        FM_M_CALL_1G_MAC_ERR_ISR(5);
    if (pending & ERR_INTR_EN_1G_MAC6)
        FM_M_CALL_1G_MAC_ERR_ISR(6);
    if (pending & ERR_INTR_EN_1G_MAC7)
        FM_M_CALL_1G_MAC_ERR_ISR(7);
    if (pending & ERR_INTR_EN_10G_MAC0)
        FM_M_CALL_10G_MAC_ERR_ISR(0);
    if (pending & ERR_INTR_EN_10G_MAC1)
        FM_M_CALL_10G_MAC_ERR_ISR(1);

#ifdef FM_MACSEC_SUPPORT
    if (pending & ERR_INTR_EN_MACSEC_MAC0)
    {
       if (p_Fm->guestId != p_Fm->intrMng[e_FM_EV_ERR_MACSEC_MAC0].guestId)
            SendIpcIsr(p_Fm, e_FM_EV_ERR_MACSEC_MAC0, pending);
        else
            p_Fm->intrMng[e_FM_EV_ERR_MACSEC_MAC0].f_Isr(p_Fm->intrMng[e_FM_EV_ERR_MACSEC_MAC0].h_SrcHandle);
    }
#endif /* FM_MACSEC_SUPPORT */

    return E_OK;
}

t_Error FM_SetPortsBandwidth(t_Handle h_Fm, t_FmPortsBandwidthParams *p_PortsBandwidth)
{
    t_Fm        *p_Fm = (t_Fm*)h_Fm;
    int         i;
    uint8_t     sum;
    uint8_t     hardwarePortId;
    uint8_t     weights[64];
    uint8_t     weight, maxPercent = 0;
    struct fman_bmi_regs *bmi_rg;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Fm->p_FmDriverParam, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    bmi_rg = p_Fm->p_FmBmiRegs;

    memset(weights, 0, (sizeof(uint8_t) * 64));

    /* check that all ports add up to 100% */
    sum = 0;
    for (i=0; i < p_PortsBandwidth->numOfPorts; i++)
        sum +=p_PortsBandwidth->portsBandwidths[i].bandwidth;
    if (sum != 100)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Sum of ports bandwidth differ from 100%"));

    /* find highest percent */
    for (i=0; i < p_PortsBandwidth->numOfPorts; i++)
    {
        if (p_PortsBandwidth->portsBandwidths[i].bandwidth > maxPercent)
            maxPercent = p_PortsBandwidth->portsBandwidths[i].bandwidth;
    }

    ASSERT_COND(maxPercent > 0); /* guaranteed by sum = 100 */

    /* calculate weight for each port */
    for (i=0; i < p_PortsBandwidth->numOfPorts; i++)
    {
        weight = (uint8_t)((p_PortsBandwidth->portsBandwidths[i].bandwidth * PORT_MAX_WEIGHT ) / maxPercent);
        /* we want even division between 1-to-PORT_MAX_WEIGHT. so if exact division
           is not reached, we round up so that:
           0 until maxPercent/PORT_MAX_WEIGHT get "1"
           maxPercent/PORT_MAX_WEIGHT+1 until (maxPercent/PORT_MAX_WEIGHT)*2 get "2"
           ...
           maxPercent - maxPercent/PORT_MAX_WEIGHT until maxPercent get "PORT_MAX_WEIGHT: */
        if ((uint8_t)((p_PortsBandwidth->portsBandwidths[i].bandwidth * PORT_MAX_WEIGHT ) % maxPercent))
            weight++;

        /* find the location of this port within the register */
        hardwarePortId =
            SwPortIdToHwPortId(p_PortsBandwidth->portsBandwidths[i].type,
                               p_PortsBandwidth->portsBandwidths[i].relativePortId,
                               p_Fm->p_FmStateStruct->revInfo.majorRev,
                               p_Fm->p_FmStateStruct->revInfo.minorRev);

        ASSERT_COND(IN_RANGE(1, hardwarePortId, 63));
        weights[hardwarePortId] = weight;
    }

    fman_set_ports_bandwidth(bmi_rg, weights);

    return E_OK;
}

t_Error FM_EnableRamsEcc(t_Handle h_Fm)
{
    t_Fm        *p_Fm = (t_Fm*)h_Fm;
    struct fman_fpm_regs *fpm_rg;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);

    fpm_rg = p_Fm->p_FmFpmRegs;

    if (p_Fm->guestId != NCSW_MASTER_ID)
    {
        t_FmIpcMsg      msg;
        t_Error         err;

        memset(&msg, 0, sizeof(msg));
        msg.msgId = FM_ENABLE_RAM_ECC;
        err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                (uint8_t*)&msg,
                                sizeof(msg.msgId),
                                NULL,
                                NULL,
                                NULL,
                                NULL);
        if (err != E_OK)
            RETURN_ERROR(MINOR, err, NO_MSG);
        return E_OK;
    }

    if (!p_Fm->p_FmStateStruct->internalCall)
        p_Fm->p_FmStateStruct->explicitEnable = TRUE;
    p_Fm->p_FmStateStruct->internalCall = FALSE;

    if (p_Fm->p_FmStateStruct->ramsEccEnable)
        return E_OK;
    else
    {
        fman_enable_rams_ecc(fpm_rg);
        p_Fm->p_FmStateStruct->ramsEccEnable = TRUE;
    }

    return E_OK;
}

t_Error FM_DisableRamsEcc(t_Handle h_Fm)
{
    t_Fm        *p_Fm = (t_Fm*)h_Fm;
    bool        explicitDisable = FALSE;
    struct fman_fpm_regs *fpm_rg;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Fm->p_FmDriverParam, E_INVALID_HANDLE);

    fpm_rg = p_Fm->p_FmFpmRegs;

    if (p_Fm->guestId != NCSW_MASTER_ID)
    {
        t_Error             err;
        t_FmIpcMsg          msg;

        memset(&msg, 0, sizeof(msg));
        msg.msgId = FM_DISABLE_RAM_ECC;
        if ((err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                     (uint8_t*)&msg,
                                     sizeof(msg.msgId),
                                     NULL,
                                     NULL,
                                     NULL,
                                     NULL)) != E_OK)
            RETURN_ERROR(MINOR, err, NO_MSG);
        return E_OK;
    }

    if (!p_Fm->p_FmStateStruct->internalCall)
        explicitDisable = TRUE;
    p_Fm->p_FmStateStruct->internalCall = FALSE;

    /* if rams are already disabled, or if rams were explicitly enabled and are
       currently called indirectly (not explicitly), ignore this call. */
    if (!p_Fm->p_FmStateStruct->ramsEccEnable ||
        (p_Fm->p_FmStateStruct->explicitEnable && !explicitDisable))
        return E_OK;
    else
    {
        if (p_Fm->p_FmStateStruct->explicitEnable)
            /* This is the case were both explicit are TRUE.
               Turn off this flag for cases were following ramsEnable
               routines are called */
            p_Fm->p_FmStateStruct->explicitEnable = FALSE;

        fman_enable_rams_ecc(fpm_rg);
        p_Fm->p_FmStateStruct->ramsEccEnable = FALSE;
    }

    return E_OK;
}

t_Error FM_SetException(t_Handle h_Fm, e_FmExceptions exception, bool enable)
{
    t_Fm                *p_Fm = (t_Fm*)h_Fm;
    uint32_t            bitMask = 0;
    enum fman_exceptions fslException;
    struct fman_rg       fman_rg;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Fm->p_FmDriverParam, E_INVALID_STATE);

    fman_rg.bmi_rg = p_Fm->p_FmBmiRegs;
    fman_rg.qmi_rg = p_Fm->p_FmQmiRegs;
    fman_rg.fpm_rg = p_Fm->p_FmFpmRegs;
    fman_rg.dma_rg = p_Fm->p_FmDmaRegs;

    GET_EXCEPTION_FLAG(bitMask, exception);
    if (bitMask)
    {
        if (enable)
            p_Fm->p_FmStateStruct->exceptions |= bitMask;
        else
            p_Fm->p_FmStateStruct->exceptions &= ~bitMask;

        fslException = FmanExceptionTrans(exception);

        return (t_Error)fman_set_exception(&fman_rg,
                                  fslException,
                                  enable);
    }
    else
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Undefined exception"));

    return E_OK;
}

t_Error FM_GetRevision(t_Handle h_Fm, t_FmRevisionInfo *p_FmRevisionInfo)
{
    t_Fm *p_Fm = (t_Fm*)h_Fm;

    p_FmRevisionInfo->majorRev = p_Fm->p_FmStateStruct->revInfo.majorRev;
    p_FmRevisionInfo->minorRev = p_Fm->p_FmStateStruct->revInfo.minorRev;

    return E_OK;
}

t_Error FM_GetFmanCtrlCodeRevision(t_Handle h_Fm, t_FmCtrlCodeRevisionInfo *p_RevisionInfo)
{
    t_Fm                            *p_Fm = (t_Fm*)h_Fm;
    t_FMIramRegs                    *p_Iram;
    uint32_t                        revInfo;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_RevisionInfo, E_NULL_POINTER);

    if ((p_Fm->guestId != NCSW_MASTER_ID) &&
        p_Fm->h_IpcSessions[0])
    {
        t_Error                         err;
        t_FmIpcMsg                      msg;
        t_FmIpcReply                    reply;
        uint32_t                        replyLength;
        t_FmIpcFmanCtrlCodeRevisionInfo ipcRevInfo;

        memset(&msg, 0, sizeof(msg));
        memset(&reply, 0, sizeof(reply));
        msg.msgId = FM_GET_FMAN_CTRL_CODE_REV;
        replyLength = sizeof(uint32_t) + sizeof(t_FmCtrlCodeRevisionInfo);
        if ((err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                     (uint8_t*)&msg,
                                     sizeof(msg.msgId),
                                     (uint8_t*)&reply,
                                     &replyLength,
                                     NULL,
                                     NULL)) != E_OK)
            RETURN_ERROR(MINOR, err, NO_MSG);
        if (replyLength != (sizeof(uint32_t) + sizeof(t_FmCtrlCodeRevisionInfo)))
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));
        memcpy((uint8_t*)&ipcRevInfo, reply.replyBody, sizeof(t_FmCtrlCodeRevisionInfo));
        p_RevisionInfo->packageRev = ipcRevInfo.packageRev;
        p_RevisionInfo->majorRev = ipcRevInfo.majorRev;
        p_RevisionInfo->minorRev = ipcRevInfo.minorRev;
        return (t_Error)(reply.error);
    }
    else if (p_Fm->guestId != NCSW_MASTER_ID)
        RETURN_ERROR(MINOR, E_NOT_SUPPORTED,
                     ("running in guest-mode without IPC!"));

    p_Iram = (t_FMIramRegs *)UINT_TO_PTR(p_Fm->baseAddr + FM_MM_IMEM);
    WRITE_UINT32(p_Iram->iadd, 0x4);
    while (GET_UINT32(p_Iram->iadd) != 0x4) ;
    revInfo = GET_UINT32(p_Iram->idata);
    p_RevisionInfo->packageRev = (uint16_t)((revInfo & 0xFFFF0000) >> 16);
    p_RevisionInfo->majorRev = (uint8_t)((revInfo & 0x0000FF00) >> 8);
    p_RevisionInfo->minorRev = (uint8_t)(revInfo & 0x000000FF);

    return E_OK;
}

uint32_t FM_GetCounter(t_Handle h_Fm, e_FmCounters counter)
{
    t_Fm        *p_Fm = (t_Fm*)h_Fm;
    t_Error     err;
    uint32_t    counterValue;
    struct fman_rg       fman_rg;
    enum fman_counters fsl_counter;

    SANITY_CHECK_RETURN_VALUE(p_Fm, E_INVALID_HANDLE, 0);
    SANITY_CHECK_RETURN_VALUE(!p_Fm->p_FmDriverParam, E_INVALID_STATE, 0);

    fman_rg.bmi_rg = p_Fm->p_FmBmiRegs;
    fman_rg.qmi_rg = p_Fm->p_FmQmiRegs;
    fman_rg.fpm_rg = p_Fm->p_FmFpmRegs;
    fman_rg.dma_rg = p_Fm->p_FmDmaRegs;

    if ((p_Fm->guestId != NCSW_MASTER_ID) &&
        !p_Fm->baseAddr &&
        p_Fm->h_IpcSessions[0])
    {
        t_FmIpcMsg          msg;
        t_FmIpcReply        reply;
        uint32_t            replyLength, outCounter;

        memset(&msg, 0, sizeof(msg));
        memset(&reply, 0, sizeof(reply));
        msg.msgId = FM_GET_COUNTER;
        memcpy(msg.msgBody, (uint8_t *)&counter, sizeof(uint32_t));
        replyLength = sizeof(uint32_t) + sizeof(uint32_t);
        err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                     (uint8_t*)&msg,
                                     sizeof(msg.msgId) +sizeof(counterValue),
                                     (uint8_t*)&reply,
                                     &replyLength,
                                     NULL,
                                     NULL);
        if (err != E_OK)
        {
            REPORT_ERROR(MAJOR, err, NO_MSG);
            return 0;
        }
        if (replyLength != (sizeof(uint32_t) + sizeof(uint32_t)))
        {
            REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));
            return 0;
        }

        memcpy((uint8_t*)&outCounter, reply.replyBody, sizeof(uint32_t));
        return outCounter;
    }
    else if (!p_Fm->baseAddr)
    {
        REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Either IPC or 'baseAddress' is required!"));
        return 0;
    }

    /* When applicable (when there is an 'enable counters' bit,
    check that counters are enabled */
    switch (counter)
    {
        case (e_FM_COUNTERS_DEQ_1):
        case (e_FM_COUNTERS_DEQ_2):
        case (e_FM_COUNTERS_DEQ_3):
            if ((p_Fm->p_FmStateStruct->revInfo.majorRev == 4) ||
                (p_Fm->p_FmStateStruct->revInfo.majorRev >= 6))
            {
                REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Requested counter not supported"));
                return 0;
            }
        case (e_FM_COUNTERS_ENQ_TOTAL_FRAME):
        case (e_FM_COUNTERS_DEQ_TOTAL_FRAME):
        case (e_FM_COUNTERS_DEQ_0):
        case (e_FM_COUNTERS_DEQ_FROM_DEFAULT):
        case (e_FM_COUNTERS_DEQ_FROM_CONTEXT):
        case (e_FM_COUNTERS_DEQ_FROM_FD):
        case (e_FM_COUNTERS_DEQ_CONFIRM):
            if (!(GET_UINT32(p_Fm->p_FmQmiRegs->fmqm_gc) & QMI_CFG_EN_COUNTERS))
            {
                REPORT_ERROR(MAJOR, E_INVALID_STATE, ("Requested counter was not enabled"));
                return 0;
            }
            break;
        default:
            break;
    }

    FMAN_COUNTERS_TRANS(fsl_counter, counter);
    return fman_get_counter(&fman_rg, fsl_counter);
}

t_Error FM_ModifyCounter(t_Handle h_Fm, e_FmCounters counter, uint32_t val)
{
    t_Fm *p_Fm = (t_Fm*)h_Fm;
    struct fman_rg          fman_rg;
    enum fman_counters fsl_counter;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Fm->p_FmDriverParam, E_INVALID_STATE);

   fman_rg.bmi_rg = p_Fm->p_FmBmiRegs;
   fman_rg.qmi_rg = p_Fm->p_FmQmiRegs;
   fman_rg.fpm_rg = p_Fm->p_FmFpmRegs;
   fman_rg.dma_rg = p_Fm->p_FmDmaRegs;

   FMAN_COUNTERS_TRANS(fsl_counter, counter);
   return  (t_Error)fman_modify_counter(&fman_rg, fsl_counter, val);
}

void FM_SetDmaEmergency(t_Handle h_Fm, e_FmDmaMuramPort muramPort, bool enable)
{
    t_Fm *p_Fm = (t_Fm*)h_Fm;
    struct fman_dma_regs *dma_rg;

    SANITY_CHECK_RETURN(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN(!p_Fm->p_FmDriverParam, E_INVALID_STATE);

    dma_rg = p_Fm->p_FmDmaRegs;

    fman_set_dma_emergency(dma_rg, !!(muramPort==e_FM_DMA_MURAM_PORT_WRITE), enable);
}

void FM_SetDmaExtBusPri(t_Handle h_Fm, e_FmDmaExtBusPri pri)
{
    t_Fm *p_Fm = (t_Fm*)h_Fm;
    struct fman_dma_regs *dma_rg;

    SANITY_CHECK_RETURN(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN(!p_Fm->p_FmDriverParam, E_INVALID_STATE);

    dma_rg = p_Fm->p_FmDmaRegs;

    fman_set_dma_ext_bus_pri(dma_rg, pri);
}

void FM_GetDmaStatus(t_Handle h_Fm, t_FmDmaStatus *p_FmDmaStatus)
{
    t_Fm                *p_Fm = (t_Fm*)h_Fm;
    uint32_t             dmaStatus;
    struct fman_dma_regs *dma_rg;

    SANITY_CHECK_RETURN(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN(!p_Fm->p_FmDriverParam, E_INVALID_STATE);

    dma_rg = p_Fm->p_FmDmaRegs;

    if ((p_Fm->guestId != NCSW_MASTER_ID) &&
        !p_Fm->baseAddr &&
        p_Fm->h_IpcSessions[0])
    {
        t_FmIpcDmaStatus    ipcDmaStatus;
        t_FmIpcMsg          msg;
        t_FmIpcReply        reply;
        t_Error             err;
        uint32_t            replyLength;

        memset(&msg, 0, sizeof(msg));
        memset(&reply, 0, sizeof(reply));
        msg.msgId = FM_DMA_STAT;
        replyLength = sizeof(uint32_t) + sizeof(t_FmIpcDmaStatus);
        err = XX_IpcSendMessage(p_Fm->h_IpcSessions[0],
                                (uint8_t*)&msg,
                                sizeof(msg.msgId),
                                (uint8_t*)&reply,
                                &replyLength,
                                NULL,
                                NULL);
        if (err != E_OK)
        {
            REPORT_ERROR(MINOR, err, NO_MSG);
            return;
        }
        if (replyLength != (sizeof(uint32_t) + sizeof(t_FmIpcDmaStatus)))
        {
            REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));
            return;
        }
        memcpy((uint8_t*)&ipcDmaStatus, reply.replyBody, sizeof(t_FmIpcDmaStatus));

        p_FmDmaStatus->cmqNotEmpty = (bool)ipcDmaStatus.boolCmqNotEmpty;            /**< Command queue is not empty */
        p_FmDmaStatus->busError = (bool)ipcDmaStatus.boolBusError;                  /**< Bus error occurred */
        p_FmDmaStatus->readBufEccError = (bool)ipcDmaStatus.boolReadBufEccError;        /**< Double ECC error on buffer Read */
        p_FmDmaStatus->writeBufEccSysError =(bool)ipcDmaStatus.boolWriteBufEccSysError;    /**< Double ECC error on buffer write from system side */
        p_FmDmaStatus->writeBufEccFmError = (bool)ipcDmaStatus.boolWriteBufEccFmError;     /**< Double ECC error on buffer write from FM side */
        p_FmDmaStatus->singlePortEccError = (bool)ipcDmaStatus.boolSinglePortEccError;     /**< Double ECC error on buffer write from FM side */
        return;
    }
    else if (!p_Fm->baseAddr)
    {
        REPORT_ERROR(MINOR, E_NOT_SUPPORTED,
                     ("Either IPC or 'baseAddress' is required!"));
        return;
    }

    dmaStatus = fman_get_dma_status(dma_rg);

    p_FmDmaStatus->cmqNotEmpty = (bool)(dmaStatus & DMA_STATUS_CMD_QUEUE_NOT_EMPTY);
    p_FmDmaStatus->busError = (bool)(dmaStatus & DMA_STATUS_BUS_ERR);
    if (p_Fm->p_FmStateStruct->revInfo.majorRev >= 6)
        p_FmDmaStatus->singlePortEccError = (bool)(dmaStatus & DMA_STATUS_FM_SPDAT_ECC);
    else
    {
        p_FmDmaStatus->readBufEccError = (bool)(dmaStatus & DMA_STATUS_READ_ECC);
        p_FmDmaStatus->writeBufEccSysError = (bool)(dmaStatus & DMA_STATUS_SYSTEM_WRITE_ECC);
        p_FmDmaStatus->writeBufEccFmError = (bool)(dmaStatus & DMA_STATUS_FM_WRITE_ECC);
    }
}

void FM_Resume(t_Handle h_Fm)
{
    t_Fm            *p_Fm = (t_Fm*)h_Fm;
    struct fman_fpm_regs *fpm_rg;

    SANITY_CHECK_RETURN(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN(!p_Fm->p_FmDriverParam, E_INVALID_STATE);
    SANITY_CHECK_RETURN((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    fpm_rg = p_Fm->p_FmFpmRegs;

    fman_resume(fpm_rg);
}

t_Error FM_GetSpecialOperationCoding(t_Handle               h_Fm,
                                     fmSpecialOperations_t  spOper,
                                     uint8_t                *p_SpOperCoding)
{
    t_Fm                        *p_Fm = (t_Fm*)h_Fm;
    t_FmCtrlCodeRevisionInfo    revInfo;
    t_Error                     err;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Fm->p_FmDriverParam, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_SpOperCoding, E_NULL_POINTER);

    if (!spOper)
    {
        *p_SpOperCoding = 0;
        return E_OK;
    }

    if ((err = FM_GetFmanCtrlCodeRevision(p_Fm, &revInfo)) != E_OK)
    {
        DBG(WARNING, ("FM in guest-mode without IPC, can't validate firmware revision."));
        revInfo.packageRev = IP_OFFLOAD_PACKAGE_NUMBER;
    }
    else if (!IS_OFFLOAD_PACKAGE(revInfo.packageRev))
        RETURN_ERROR(MINOR, E_NOT_SUPPORTED, ("Fman ctrl code package"));

    switch (spOper)
    {
        case (FM_SP_OP_CAPWAP_DTLS_DEC):
                *p_SpOperCoding = 9;
                break;
        case (FM_SP_OP_CAPWAP_DTLS_ENC):
                *p_SpOperCoding = 10;
                break;
        case (FM_SP_OP_IPSEC|FM_SP_OP_IPSEC_UPDATE_UDP_LEN|FM_SP_OP_IPSEC_MANIP):
        case (FM_SP_OP_IPSEC|FM_SP_OP_IPSEC_UPDATE_UDP_LEN|FM_SP_OP_IPSEC_MANIP|FM_SP_OP_RPD):
                *p_SpOperCoding = 5;
                break;
        case (FM_SP_OP_IPSEC|FM_SP_OP_IPSEC_MANIP):
        case (FM_SP_OP_IPSEC|FM_SP_OP_IPSEC_MANIP|FM_SP_OP_RPD):
                *p_SpOperCoding = 6;
                break;
        case (FM_SP_OP_IPSEC|FM_SP_OP_IPSEC_UPDATE_UDP_LEN|FM_SP_OP_RPD):
                *p_SpOperCoding = 3;
                break;
        case (FM_SP_OP_IPSEC|FM_SP_OP_IPSEC_UPDATE_UDP_LEN):
                *p_SpOperCoding = 1;
                break;
        case (FM_SP_OP_IPSEC|FM_SP_OP_IPSEC_UPDATE_UDP_LEN|FM_SP_OP_IPSEC_NO_ETH_HDR):
                *p_SpOperCoding = 12;
                break;
        case (FM_SP_OP_IPSEC|FM_SP_OP_RPD):
                *p_SpOperCoding = 4;
                break;
        case (FM_SP_OP_IPSEC):
                *p_SpOperCoding = 2;
                break;
        case (FM_SP_OP_DCL4C):
                *p_SpOperCoding = 7;
                break;
        case (FM_SP_OP_CLEAR_RPD):
                *p_SpOperCoding = 8;
                break;
        default:
            RETURN_ERROR(MINOR, E_INVALID_VALUE, NO_MSG);
    }

    return E_OK;
}

t_Error FM_CtrlMonStart(t_Handle h_Fm)
{
    t_Fm            *p_Fm = (t_Fm *)h_Fm;
    t_FmTrbRegs     *p_MonRegs;
    uint8_t         i;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Fm->p_FmDriverParam, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    WRITE_UINT32(p_Fm->p_FmFpmRegs->fmfp_brkc,
                 GET_UINT32(p_Fm->p_FmFpmRegs->fmfp_brkc) | FPM_BRKC_RDBG);

    for (i = 0; i < FM_NUM_OF_CTRL; i++)
    {
        p_MonRegs = (t_FmTrbRegs *)UINT_TO_PTR(p_Fm->baseAddr + FM_MM_TRB(i));

        /* Reset control registers */
        WRITE_UINT32(p_MonRegs->tcrh, TRB_TCRH_RESET);
        WRITE_UINT32(p_MonRegs->tcrl, TRB_TCRL_RESET);

        /* Configure: counter #1 counts all stalls in risc - ldsched stall
                      counter #2 counts all stalls in risc - other stall*/
        WRITE_UINT32(p_MonRegs->tcrl, TRB_TCRL_RESET | TRB_TCRL_UTIL);

        /* Enable monitoring */
        WRITE_UINT32(p_MonRegs->tcrh, TRB_TCRH_ENABLE_COUNTERS);
    }

    return E_OK;
}

t_Error FM_CtrlMonStop(t_Handle h_Fm)
{
    t_Fm            *p_Fm = (t_Fm *)h_Fm;
    t_FmTrbRegs     *p_MonRegs;
    uint8_t         i;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Fm->p_FmDriverParam, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);

    for (i = 0; i < FM_NUM_OF_CTRL; i++)
    {
        p_MonRegs = (t_FmTrbRegs *)UINT_TO_PTR(p_Fm->baseAddr + FM_MM_TRB(i));
        WRITE_UINT32(p_MonRegs->tcrh, TRB_TCRH_DISABLE_COUNTERS);
    }

    WRITE_UINT32(p_Fm->p_FmFpmRegs->fmfp_brkc,
                 GET_UINT32(p_Fm->p_FmFpmRegs->fmfp_brkc) & ~FPM_BRKC_RDBG);

    return E_OK;
}

t_Error FM_CtrlMonGetCounters(t_Handle h_Fm, uint8_t fmCtrlIndex, t_FmCtrlMon *p_Mon)
{
    t_Fm            *p_Fm = (t_Fm *)h_Fm;
    t_FmTrbRegs     *p_MonRegs;
    uint64_t        clkCnt, utilValue, effValue;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Fm->p_FmDriverParam, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR((p_Fm->guestId == NCSW_MASTER_ID), E_NOT_SUPPORTED);
    SANITY_CHECK_RETURN_ERROR(p_Mon, E_NULL_POINTER);

    if (fmCtrlIndex >= FM_NUM_OF_CTRL)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("FM Controller index"));

    p_MonRegs = (t_FmTrbRegs *)UINT_TO_PTR(p_Fm->baseAddr + FM_MM_TRB(fmCtrlIndex));

    clkCnt = (uint64_t)
            ((uint64_t)GET_UINT32(p_MonRegs->tpcch) << 32 | GET_UINT32(p_MonRegs->tpccl));

    utilValue = (uint64_t)
            ((uint64_t)GET_UINT32(p_MonRegs->tpc1h) << 32 | GET_UINT32(p_MonRegs->tpc1l));

    effValue = (uint64_t)
            ((uint64_t)GET_UINT32(p_MonRegs->tpc2h) << 32 | GET_UINT32(p_MonRegs->tpc2l));

    p_Mon->percentCnt[0] = (uint8_t)(((clkCnt - utilValue) * 100) / clkCnt);
    if (clkCnt != utilValue)
        p_Mon->percentCnt[1] = (uint8_t)((((clkCnt - utilValue) - effValue) * 100) / (clkCnt - utilValue));
    else
        p_Mon->percentCnt[1] = 0;

    return E_OK;
}

t_Handle FM_GetMuramHandle(t_Handle h_Fm)
{
    t_Fm        *p_Fm = (t_Fm*)h_Fm;

    SANITY_CHECK_RETURN_VALUE(p_Fm, E_INVALID_HANDLE, NULL);

    return (p_Fm->h_FmMuram);
}

/****************************************************/
/*       Hidden-DEBUG Only API                      */
/****************************************************/
t_Error FM_ForceIntr (t_Handle h_Fm, e_FmExceptions exception)
{
    t_Fm *p_Fm = (t_Fm*)h_Fm;
    enum fman_exceptions fslException;
    struct fman_rg fman_rg;

    SANITY_CHECK_RETURN_ERROR(p_Fm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Fm->p_FmDriverParam, E_INVALID_STATE);

    fman_rg.bmi_rg = p_Fm->p_FmBmiRegs;
    fman_rg.qmi_rg = p_Fm->p_FmQmiRegs;
    fman_rg.fpm_rg = p_Fm->p_FmFpmRegs;
    fman_rg.dma_rg = p_Fm->p_FmDmaRegs;

    switch (exception)
    {
        case e_FM_EX_QMI_DEQ_FROM_UNKNOWN_PORTID:
            if (!(p_Fm->p_FmStateStruct->exceptions & FM_EX_QMI_DEQ_FROM_UNKNOWN_PORTID))
                RETURN_ERROR(MINOR, E_NOT_SUPPORTED, ("The selected exception is masked"));
            break;
        case e_FM_EX_QMI_SINGLE_ECC:
            if (p_Fm->p_FmStateStruct->revInfo.majorRev >= 6)
                 RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("e_FM_EX_QMI_SINGLE_ECC not supported on this integration."));

            if (!(p_Fm->p_FmStateStruct->exceptions & FM_EX_QMI_SINGLE_ECC))
                RETURN_ERROR(MINOR, E_NOT_SUPPORTED, ("The selected exception is masked"));
            break;
        case e_FM_EX_QMI_DOUBLE_ECC:
            if (!(p_Fm->p_FmStateStruct->exceptions & FM_EX_QMI_DOUBLE_ECC))
                RETURN_ERROR(MINOR, E_NOT_SUPPORTED, ("The selected exception is masked"));
            break;
        case e_FM_EX_BMI_LIST_RAM_ECC:
            if (!(p_Fm->p_FmStateStruct->exceptions & FM_EX_BMI_LIST_RAM_ECC))
                RETURN_ERROR(MINOR, E_NOT_SUPPORTED, ("The selected exception is masked"));
            break;
        case e_FM_EX_BMI_STORAGE_PROFILE_ECC:
            if (!(p_Fm->p_FmStateStruct->exceptions & FM_EX_BMI_STORAGE_PROFILE_ECC))
                RETURN_ERROR(MINOR, E_NOT_SUPPORTED, ("The selected exception is masked"));
            break;
        case e_FM_EX_BMI_STATISTICS_RAM_ECC:
            if (!(p_Fm->p_FmStateStruct->exceptions & FM_EX_BMI_STATISTICS_RAM_ECC))
                RETURN_ERROR(MINOR, E_NOT_SUPPORTED, ("The selected exception is masked"));
            break;
        case e_FM_EX_BMI_DISPATCH_RAM_ECC:
            if (!(p_Fm->p_FmStateStruct->exceptions & FM_EX_BMI_DISPATCH_RAM_ECC))
                RETURN_ERROR(MINOR, E_NOT_SUPPORTED, ("The selected exception is masked"));
            break;
        default:
            RETURN_ERROR(MINOR, E_NOT_SUPPORTED, ("The selected exception may not be forced"));
    }

    fslException = FmanExceptionTrans(exception);
    fman_force_intr (&fman_rg, fslException);

    return E_OK;
}

t_Handle FmGetPcd(t_Handle h_Fm)
{
	return ((t_Fm*)h_Fm)->h_Pcd;
}
#if (DPAA_VERSION >= 11)
extern void *g_MemacRegs;
void fm_clk_down(void);
uint32_t fman_memac_get_event(void *regs, uint32_t ev_mask);
void FM_ChangeClock(t_Handle h_Fm, int hardwarePortId)
{
	int macId;
	uint32_t    event, rcr;
	t_Fm *p_Fm = (t_Fm*)h_Fm;
	rcr = GET_UINT32(p_Fm->p_FmFpmRegs->fm_rcr);
	rcr |= 0x04000000;
	WRITE_UINT32(p_Fm->p_FmFpmRegs->fm_rcr, rcr);

	HW_PORT_ID_TO_SW_PORT_ID(macId, hardwarePortId);
	do
	{
		event = fman_memac_get_event(g_MemacRegs, 0xFFFFFFFF);
	} while ((event & 0x00000020) == 0);
	fm_clk_down();
	rcr = GET_UINT32(p_Fm->p_FmFpmRegs->fm_rcr);
	rcr &= ~0x04000000;
	WRITE_UINT32(p_Fm->p_FmFpmRegs->fm_rcr, rcr);
}
#endif
