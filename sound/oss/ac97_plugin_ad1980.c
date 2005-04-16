/*
    ac97_plugin_ad1980.c  Copyright (C) 2003 Red Hat, Inc. All rights reserved.

   The contents of this file are subject to the Open Software License version 1.1
   that can be found at http://www.opensource.org/licenses/osl-1.1.txt and is 
   included herein by reference. 
   
   Alternatively, the contents of this file may be used under the
   terms of the GNU General Public License version 2 (the "GPL") as 
   distributed in the kernel source COPYING file, in which
   case the provisions of the GPL are applicable instead of the
   above.  If you wish to allow the use of your version of this file
   only under the terms of the GPL and not to allow others to use
   your version of this file under the OSL, indicate your decision
   by deleting the provisions above and replace them with the notice
   and other provisions required by the GPL.  If you do not delete
   the provisions above, a recipient may use your version of this
   file under either the OSL or the GPL.
   
   Authors: 	Alan Cox <alan@redhat.com>

   This is an example codec plugin. This one switches the connections
   around to match the setups some vendors use with audio switched to
   non standard front connectors not the normal rear ones

   This code primarily exists to demonstrate how to use the codec
   interface

*/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ac97_codec.h>

/**
 *	ad1980_remove		-	codec remove callback
 *	@codec: The codec that is being removed
 *
 *	This callback occurs when an AC97 codec is being removed. A
 *	codec remove call will not occur for a codec during that codec
 *	probe callback.
 *
 *	Most drivers will need to lock their remove versus their 
 *	use of the codec after the probe function.
 */
 
static void __devexit ad1980_remove(struct ac97_codec *codec, struct ac97_driver *driver)
{
	/* Nothing to do in the simple example */
}


/**
 *	ad1980_probe		-	codec found callback
 *	@codec: ac97 codec matching the idents
 *	@driver: ac97_driver it matched
 *
 *	This entry point is called when a codec is found which matches
 *	the driver. At the point it is called the codec is basically
 *	operational, mixer operations have been initialised and can
 *	be overriden. Called in process context. The field driver_private
 *	is available for the driver to use to store stuff.
 *
 *	The caller can claim the device by returning zero, or return
 *	a negative error code. 
 */
 
static int ad1980_probe(struct ac97_codec *codec, struct ac97_driver *driver)
{
	u16 control;

#define AC97_AD_MISC	0x76

	/* Switch the inputs/outputs over (from Dell code) */
	control = codec->codec_read(codec, AC97_AD_MISC);
	codec->codec_write(codec, AC97_AD_MISC, control | 0x4420);
	
	/* We could refuse the device since we dont need to hang around,
	   but we will claim it */
	return 0;
}
	
 
static struct ac97_driver ad1980_driver = {
	.codec_id	= 0x41445370,
	.codec_mask	= 0xFFFFFFFF,
	.name		= "AD1980 example",
	.probe		= ad1980_probe,
	.remove		= __devexit_p(ad1980_remove),
};

/**
 *	ad1980_exit		-	module exit path
 *
 *	Our module is being unloaded. At this point unregister_driver
 *	will call back our remove handler for any existing codecs. You
 *	may not unregister_driver from interrupt context or from a 
 *	probe/remove callback.
 */

static void ad1980_exit(void)
{
	ac97_unregister_driver(&ad1980_driver);
}

/**
 *	ad1980_init		-	set up ad1980 handlers
 *
 *	After we call the register function it will call our probe
 *	function for each existing matching device before returning to us.
 *	Any devices appearing afterwards whose id's match the codec_id
 *	will also cause the probe function to be called.
 *	You may not register_driver from interrupt context or from a 
 *	probe/remove callback.
 */
 
static int ad1980_init(void)
{
	return ac97_register_driver(&ad1980_driver);
}

module_init(ad1980_init);
module_exit(ad1980_exit);
MODULE_LICENSE("GPL");
