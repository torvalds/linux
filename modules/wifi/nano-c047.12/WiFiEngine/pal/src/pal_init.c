
/******************************************************************************
 *  Copyright (c) 2010 - Sweden Connectivity AB.
 *  ALL RIGHTS RESERVED
 *  
 *  PAL_Init.c
 *  This file implements the tty for communication between PAL/driver and Stack
 *	It's starts pal_main_thread and register callback for PAL events and data
 *
 *****************************************************************************/

/******************************************************************************
I N C L U D E S
******************************************************************************/

#include "driverenv.h"
#include "pal_init.h"
#include "hmg_pal.h"
#include "wifi_engine_internal.h"

//Callback function for HCI events and Data
//Send to tty, using sam process as caller
net_rx_cb_t HCI_response_function(mac_api_net_id_t net, char* buf, int size)
{

	return (net_rx_cb_t)1;
}
/*******************************************************************************
 * FUNCTION:	pal_main_thread
 * 
 * DESCRIPTION:   
 *
 * PARAMETERS:	
 *
 * RETURN:	
 *
 ******************************************************************************/
/*static int pal_main_thread(void *_priv)
{
 
}
*/

static int palTty_startup(struct uart_port *port)
{
  
    return 0;
}

/*******************************************************************************
 * FUNCTION:	palTty_shutdown
 * 
 * DESCRIPTION:   
 *
 * PARAMETERS:	
 *
 * RETURN:	
 *
 ******************************************************************************/
static void palTty_shutdown(struct uart_port *port)
{
}



void nano_To_Pal(int msg)
{
	

}


/*******************************************************************************
 * FUNCTION:	pal_Init
 * 
 * DESCRIPTION:   
 *
 * PARAMETERS:	
 *
 * RETURN:	
 *
 ******************************************************************************/
int pal_Init (void)
{


  
	return 1;
}

/*******************************************************************************
 * FUNCTION:	__exit
 * 
 * DESCRIPTION:   
 *
 * PARAMETERS:	
 *
 * RETURN:	
 *
 ******************************************************************************/
void pal_Exit (void)
{
 
}

 

