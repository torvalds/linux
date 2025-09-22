#	$OpenBSD: Makefile,v 1.311 2025/07/13 05:49:36 dlg Exp $
#	$NetBSD: Makefile,v 1.4 1996/01/09 03:23:01 thorpej Exp $

#	Makefile for section 9 (kernel function and variable) manual pages.

MAN=	aml_evalnode.9 atomic_add_int.9 atomic_cas_uint.9 \
	atomic_dec_int.9 atomic_inc_int.9 atomic_load_int.9 \
	atomic_setbits_int.9 atomic_sub_int.9 atomic_swap_uint.9 \
	audio.9 autoconf.9 \
	bemtoh32.9 bio_register.9 bintimeadd.9 boot.9 bpf_mtap.9 buffercache.9 \
	bufq_init.9 bus_dma.9 bus_space.9 \
	clockintr.9 clockintr_bind.9 \
	copy.9 cond_init.9 config_attach.9 config_defer.9 counters_alloc.9 \
	cpumem_get.9 cpu_xcall.9 crypto.9 \
	delay.9 disk.9 disklabel.9 dma_alloc.9 dohooks.9 \
	dostartuphooks.9 \
	evcount.9 extent.9 fb_setup.9 file.9 fork1.9 ffs.9 \
	getdevvp.9 getnewvnode.9 getsn.9 hashinit.9 \
	hardclock.9 hook_establish.9 hz.9 idgen32.9 \
	ieee80211.9 ieee80211_crypto.9 ieee80211_input.9 ieee80211_ioctl.9 \
	ieee80211_node.9 ieee80211_output.9 ieee80211_proto.9 \
	ieee80211_radiotap.9 if_addrhook_add.9 if_get.9 if_rxr_init.9 \
	ifiq_input.9 ifq_enqueue.9 \
	ifq_deq_begin.9 imax.9 iic.9 intro.9 inittodr.9 \
	intr_barrier.9 intrmap_create.9 \
	KASSERT.9 kcov_remote_register.9 km_alloc.9 knote.9 kstat_create.9 \
	kstat_kv_init.9 kthread.9 ktrace.9 \
	lim_cur.9 loadfirmware.9 log.9 \
	malloc.9 membar_sync.9 memcmp.9 mbuf.9 mbuf_tags.9 md5.9 mi_switch.9 \
	microtime.9 ml_init.9 mq_init.9 mutex.9 \
	namei.9 \
	panic.9 pci_conf_read.9 pci_mapreg_map.9 pci_intr_map.9 physio.9 \
	pmap.9 pool.9 pool_cache_init.9 ppsratecheck.9 printf.9 psignal.9 \
	RBT_INIT.9 \
	radio.9 arc4random.9 rasops.9 ratecheck.9 refcnt_init.9 resettodr.9 \
	rssadapt.9 route.9 rt_ifa_add.9 rt_timer_add.9 \
	rtalloc.9 rtable_add.9 rtable_walk.9 \
	rtlabel_id2name.9 rtrequest.9 rwlock.9 SRPL_EMPTY_LOCKED.9 SipHash24.9 \
	sensor_attach.9 sigio_init.9 \
	SMR_LIST_INIT.9 SMR_PTR_GET.9 smr_call.9 \
	spl.9 srp_enter.9 srpl_rc_init.9 startuphook_establish.9 socreate.9 \
	sosplice.9 stoeplitz_to_key.9 strcmp.9 strnstr.9 \
	style.9 syscall.9 sysctl_int.9 \
	task_add.9 tc_init.9 tfind.9 thread_fork.9 \
	timeout.9 tsleep.9 tvtohz.9 \
	uiomove.9 \
	usb_add_task.9 usbd_close_pipe.9 usbd_open_pipe.9 \
	usbd_ref_wait.9 usbd_transfer.9 \
	uvm_fault.9 uvm_init.9 uvm_km_alloc.9 uvm_map.9 uvm_pagealloc.9 \
	uvm_vslock.9 uvn_attach.9 \
	vfs.9 vfs_busy.9 \
	vfs_cache.9 vaccess.9 vclean.9 vcount.9 vdevgone.9 vfinddev.9 vflush.9 \
	vflushbuf.9 vget.9 vgone.9 vhold.9 vinvalbuf.9 vnode.9 vnsubr.9 \
	VOP_LOOKUP.9 vput.9 vrecycle.9 vref.9 vrele.9 \
	vwaitforio.9 vwakeup.9 wdog_register.9 wsfont_init.9

.include <bsd.prog.mk>
