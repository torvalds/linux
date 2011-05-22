/*
 *  pNFS Objects layout implementation over open-osd initiator library
 *
 *  Copyright (C) 2009 Panasas Inc. [year of first publication]
 *  All rights reserved.
 *
 *  Benny Halevy <bhalevy@panasas.com>
 *  Boaz Harrosh <bharrosh@panasas.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  See the file COPYING included with this distribution for more details.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the Panasas company nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/module.h>
#include "../pnfs.h"

static struct pnfs_layoutdriver_type objlayout_type = {
	.id = LAYOUT_OSD2_OBJECTS,
	.name = "LAYOUT_OSD2_OBJECTS",
};

MODULE_DESCRIPTION("pNFS Layout Driver for OSD2 objects");
MODULE_AUTHOR("Benny Halevy <bhalevy@panasas.com>");
MODULE_LICENSE("GPL");

static int __init
objlayout_init(void)
{
	int ret = pnfs_register_layoutdriver(&objlayout_type);

	if (ret)
		printk(KERN_INFO
			"%s: Registering OSD pNFS Layout Driver failed: error=%d\n",
			__func__, ret);
	else
		printk(KERN_INFO "%s: Registered OSD pNFS Layout Driver\n",
			__func__);
	return ret;
}

static void __exit
objlayout_exit(void)
{
	pnfs_unregister_layoutdriver(&objlayout_type);
	printk(KERN_INFO "%s: Unregistered OSD pNFS Layout Driver\n",
	       __func__);
}

module_init(objlayout_init);
module_exit(objlayout_exit);
