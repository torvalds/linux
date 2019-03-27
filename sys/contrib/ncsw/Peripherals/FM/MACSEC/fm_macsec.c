/*
 * Copyright 2008-2015 Freescale Semiconductor Inc.
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

 @File          fm_macsec.c

 @Description   FM MACSEC driver routines implementation.
*//***************************************************************************/

#include "std_ext.h"
#include "error_ext.h"
#include "xx_ext.h"
#include "string_ext.h"
#include "sprint_ext.h"
#include "debug_ext.h"

#include "fm_macsec.h"


/****************************************/
/*       API Init unit functions        */
/****************************************/
t_Handle FM_MACSEC_Config(t_FmMacsecParams *p_FmMacsecParam)
{
    t_FmMacsecControllerDriver *p_FmMacsecControllerDriver;

    SANITY_CHECK_RETURN_VALUE(p_FmMacsecParam, E_INVALID_HANDLE, NULL);

    if (p_FmMacsecParam->guestMode)
        p_FmMacsecControllerDriver = (t_FmMacsecControllerDriver *)FM_MACSEC_GUEST_Config(p_FmMacsecParam);
    else
        p_FmMacsecControllerDriver = (t_FmMacsecControllerDriver *)FM_MACSEC_MASTER_Config(p_FmMacsecParam);

    if (!p_FmMacsecControllerDriver)
        return NULL;

    return (t_Handle)p_FmMacsecControllerDriver;
}

t_Error FM_MACSEC_Init(t_Handle h_FmMacsec)
{
    t_FmMacsecControllerDriver *p_FmMacsecControllerDriver = (t_FmMacsecControllerDriver *)h_FmMacsec;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacsecControllerDriver->f_FM_MACSEC_Init)
        return p_FmMacsecControllerDriver->f_FM_MACSEC_Init(h_FmMacsec);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

t_Error FM_MACSEC_Free(t_Handle h_FmMacsec)
{
    t_FmMacsecControllerDriver *p_FmMacsecControllerDriver = (t_FmMacsecControllerDriver *)h_FmMacsec;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacsecControllerDriver->f_FM_MACSEC_Free)
        return p_FmMacsecControllerDriver->f_FM_MACSEC_Free(h_FmMacsec);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

t_Error FM_MACSEC_ConfigUnknownSciFrameTreatment(t_Handle h_FmMacsec, e_FmMacsecUnknownSciFrameTreatment treatMode)
{
    t_FmMacsecControllerDriver *p_FmMacsecControllerDriver = (t_FmMacsecControllerDriver *)h_FmMacsec;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacsecControllerDriver->f_FM_MACSEC_ConfigUnknownSciFrameTreatment)
        return p_FmMacsecControllerDriver->f_FM_MACSEC_ConfigUnknownSciFrameTreatment(h_FmMacsec, treatMode);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

t_Error FM_MACSEC_ConfigInvalidTagsFrameTreatment(t_Handle h_FmMacsec, bool deliverUncontrolled)
{
    t_FmMacsecControllerDriver *p_FmMacsecControllerDriver = (t_FmMacsecControllerDriver *)h_FmMacsec;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacsecControllerDriver->f_FM_MACSEC_ConfigInvalidTagsFrameTreatment)
        return p_FmMacsecControllerDriver->f_FM_MACSEC_ConfigInvalidTagsFrameTreatment(h_FmMacsec, deliverUncontrolled);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

t_Error FM_MACSEC_ConfigEncryptWithNoChangedTextFrameTreatment(t_Handle h_FmMacsec, bool discardUncontrolled)
{
    t_FmMacsecControllerDriver *p_FmMacsecControllerDriver = (t_FmMacsecControllerDriver *)h_FmMacsec;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacsecControllerDriver->f_FM_MACSEC_ConfigEncryptWithNoChangedTextFrameTreatment)
        return p_FmMacsecControllerDriver->f_FM_MACSEC_ConfigEncryptWithNoChangedTextFrameTreatment(h_FmMacsec, discardUncontrolled);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

t_Error FM_MACSEC_ConfigUntagFrameTreatment(t_Handle h_FmMacsec, e_FmMacsecUntagFrameTreatment treatMode)
{
    t_FmMacsecControllerDriver *p_FmMacsecControllerDriver = (t_FmMacsecControllerDriver *)h_FmMacsec;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacsecControllerDriver->f_FM_MACSEC_ConfigUntagFrameTreatment)
        return p_FmMacsecControllerDriver->f_FM_MACSEC_ConfigUntagFrameTreatment(h_FmMacsec, treatMode);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

t_Error FM_MACSEC_ConfigPnExhaustionThreshold(t_Handle h_FmMacsec, uint32_t pnExhThr)
{
    t_FmMacsecControllerDriver *p_FmMacsecControllerDriver = (t_FmMacsecControllerDriver *)h_FmMacsec;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacsecControllerDriver->f_FM_MACSEC_ConfigPnExhaustionThreshold)
        return p_FmMacsecControllerDriver->f_FM_MACSEC_ConfigPnExhaustionThreshold(h_FmMacsec, pnExhThr);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

t_Error FM_MACSEC_ConfigKeysUnreadable(t_Handle h_FmMacsec)
{
    t_FmMacsecControllerDriver *p_FmMacsecControllerDriver = (t_FmMacsecControllerDriver *)h_FmMacsec;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacsecControllerDriver->f_FM_MACSEC_ConfigKeysUnreadable)
        return p_FmMacsecControllerDriver->f_FM_MACSEC_ConfigKeysUnreadable(h_FmMacsec);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

t_Error FM_MACSEC_ConfigSectagWithoutSCI(t_Handle h_FmMacsec)
{
    t_FmMacsecControllerDriver *p_FmMacsecControllerDriver = (t_FmMacsecControllerDriver *)h_FmMacsec;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacsecControllerDriver->f_FM_MACSEC_ConfigSectagWithoutSCI)
        return p_FmMacsecControllerDriver->f_FM_MACSEC_ConfigSectagWithoutSCI(h_FmMacsec);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

t_Error FM_MACSEC_ConfigException(t_Handle h_FmMacsec, e_FmMacsecExceptions exception, bool enable)
{
    t_FmMacsecControllerDriver *p_FmMacsecControllerDriver = (t_FmMacsecControllerDriver *)h_FmMacsec;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacsecControllerDriver->f_FM_MACSEC_ConfigException)
        return p_FmMacsecControllerDriver->f_FM_MACSEC_ConfigException(h_FmMacsec, exception, enable);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

t_Error FM_MACSEC_GetRevision(t_Handle h_FmMacsec, uint32_t *p_MacsecRevision)
{
    t_FmMacsecControllerDriver *p_FmMacsecControllerDriver = (t_FmMacsecControllerDriver *)h_FmMacsec;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacsecControllerDriver->f_FM_MACSEC_GetRevision)
        return p_FmMacsecControllerDriver->f_FM_MACSEC_GetRevision(h_FmMacsec, p_MacsecRevision);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}


t_Error FM_MACSEC_Enable(t_Handle h_FmMacsec)
{
    t_FmMacsecControllerDriver *p_FmMacsecControllerDriver = (t_FmMacsecControllerDriver *)h_FmMacsec;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacsecControllerDriver->f_FM_MACSEC_Enable)
        return p_FmMacsecControllerDriver->f_FM_MACSEC_Enable(h_FmMacsec);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

t_Error FM_MACSEC_Disable(t_Handle h_FmMacsec)
{
    t_FmMacsecControllerDriver *p_FmMacsecControllerDriver = (t_FmMacsecControllerDriver *)h_FmMacsec;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacsecControllerDriver->f_FM_MACSEC_Disable)
        return p_FmMacsecControllerDriver->f_FM_MACSEC_Disable(h_FmMacsec);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

t_Error FM_MACSEC_SetException(t_Handle h_FmMacsec, e_FmMacsecExceptions exception, bool enable)
{
    t_FmMacsecControllerDriver *p_FmMacsecControllerDriver = (t_FmMacsecControllerDriver *)h_FmMacsec;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacsecControllerDriver->f_FM_MACSEC_SetException)
        return p_FmMacsecControllerDriver->f_FM_MACSEC_SetException(h_FmMacsec, exception, enable);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

