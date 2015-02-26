obj-y += time.o timer.o hrtimer.o itimer.o posix-timers.o posix-cpu-timers.o
obj-y += timekeeping.o ntp.o clocksource.o jiffies.o timer_list.o
obj-y += timeconv.o timecounter.o posix-clock.o alarmtimer.o

obj-$(CONFIG_GENERIC_CLOCKEVENTS_BUILD)		+= clockevents.o
obj-$(CONFIG_GENERIC_CLOCKEVENTS)		+= tick-common.o
ifeq ($(CONFIG_GENERIC_CLOCKEVENTS_BROADCAST),y)
 obj-y						+= tick-broadcast.o
 obj-$(CONFIG_TICK_ONESHOT)			+= tick-broadcast-hrtimer.o
endif
obj-$(CONFIG_GENERIC_SCHED_CLOCK)		+= sched_clock.o
obj-$(CONFIG_TICK_ONESHOT)			+= tick-oneshot.o
obj-$(CONFIG_TICK_ONESHOT)			+= tick-sched.o
obj-$(CONFIG_TIMER_STATS)			+= timer_stats.o
obj-$(CONFIG_DEBUG_FS)				+= timekeeping_debug.o
obj-$(CONFIG_TEST_UDELAY)			+= test_udelay.o

$(obj)/time.o: $(obj)/timeconst.h

quiet_cmd_hzfile = HZFILE  $@
      cmd_hzfile = echo "hz=$(CONFIG_HZ)" > $@

targets += hz.bc
$(obj)/hz.bc: $(objtree)/include/config/hz.h FORCE
	$(call if_changed,hzfile)

quiet_cmd_bc  = BC      $@
      cmd_bc  = bc -q $(filter-out FORCE,$^) > $@

targets += timeconst.h
$(obj)/timeconst.h: $(obj)/hz.bc $(src)/timeconst.bc FORCE
	$(call if_changed,bc)

