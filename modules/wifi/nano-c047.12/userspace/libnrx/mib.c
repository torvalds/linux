/* Copyright (C) 2007 Nanoradio AB */
/* $Id: mib.c 9954 2008-09-15 09:41:38Z joda $ */

#include "nrx_priv.h"

/*!
 * @internal
 * @ingroup MISC
 * @brief <b>Retrieve the value of a MIB</b>
 *
 * This is a low-level API function and usage directly from an application should
 * be avoided if possible.
 *
 * @param ctx A valid nrx_context.
 * @param mib_id The MIB id to retrieve.
 * @param buf A buffer where the MIB value will be stored.
 * @param len The size of buf.
 *
 * @return Zero on success or an error code.
 *
 * <!-- NRX_API_FOR_TESTING -->
 */
int
nrx_get_mib_val(nrx_context ctx, 
                const char *mib_id, 
                void *buf, 
                size_t *len)
{
   int ret;
   struct nrx_ioc_mib_value param;

   memset(&param, 0, sizeof(param));
   strlcpy(param.mib_id, mib_id, sizeof(param.mib_id));
   param.mib_param = buf;
   param.mib_param_size = *len;
   ret = nrx_nrxioctl(ctx, NRXIOWRGETMIB, &param.ioc);
   if(ret)
      return ret;

   *len = param.mib_param_size;
   
   return 0;
}

/*!
 * @internal
 * @ingroup MISC
 * @brief <b>Set the value of a MIB</b>
 *
 * This is a low-level API function and usage directly from an application should
 * be avoided if possible.
 *
 * @param ctx A valid nrx_context.
 * @param mib_id The MIB id to set.
 * @param buf A buffer where the MIB value is stored.
 * @param len The size of buf.
 *
 * @return Zero on success or an error code.
 *
 * <!-- NRX_API_FOR_TESTING -->
 */
int
nrx_set_mib_val(nrx_context ctx, 
                const char *mib_id, 
                const void *buf, 
                size_t len)
{
   struct nrx_ioc_mib_value param;

   memset(&param, 0, sizeof(param));

   strlcpy(param.mib_id, mib_id, sizeof(param.mib_id));
   param.mib_param = (void*)buf;
   param.mib_param_size = len;
   
   return nrx_nrxioctl(ctx, NRXIOWSETMIB, &param.ioc);
}

