/* $FreeBSD$ */
  printf("sizeof(union req_data_u_tag) = %d\n", 
	 (int) sizeof(union req_data_u_tag));
  printf("offsetof(u32) = %d\n", 
	 (int) offsetof(union req_data_u_tag, u32));
  printf("offsetof(data) = %d\n", 
	 (int) offsetof(union req_data_u_tag, data));
  printf("\n");

  printf("sizeof(struct req_pkt) = %d\n", 
	 (int) sizeof(struct req_pkt));
  printf("offsetof(rm_vn_mode) = %d\n", 
	 (int) offsetof(struct req_pkt, rm_vn_mode));
  printf("offsetof(auth_seq) = %d\n", 
	 (int) offsetof(struct req_pkt, auth_seq));
  printf("offsetof(implementation) = %d\n", 
	 (int) offsetof(struct req_pkt, implementation));
  printf("offsetof(request) = %d\n", 
	 (int) offsetof(struct req_pkt, request));
  printf("offsetof(err_nitems) = %d\n", 
	 (int) offsetof(struct req_pkt, err_nitems));
  printf("offsetof(mbz_itemsize) = %d\n", 
	 (int) offsetof(struct req_pkt, mbz_itemsize));
  printf("offsetof(u) = %d\n", 
	 (int) offsetof(struct req_pkt, u));
  printf("offsetof(tstamp) = %d\n", 
	 (int) offsetof(struct req_pkt, tstamp));
  printf("offsetof(keyid) = %d\n", 
	 (int) offsetof(struct req_pkt, keyid));
  printf("offsetof(mac) = %d\n", 
	 (int) offsetof(struct req_pkt, mac));
  printf("\n");

  printf("sizeof(struct req_pkt_tail) = %d\n", 
	 (int) sizeof(struct req_pkt_tail));
  printf("offsetof(tstamp) = %d\n", 
	 (int) offsetof(struct req_pkt_tail, tstamp));
  printf("offsetof(keyid) = %d\n", 
	 (int) offsetof(struct req_pkt_tail, keyid));
  printf("offsetof(mac) = %d\n", 
	 (int) offsetof(struct req_pkt_tail, mac));
  printf("\n");

  printf("sizeof(union resp_pkt_u_tag) = %d\n", 
	 (int) sizeof(union resp_pkt_u_tag));
  printf("offsetof(data) = %d\n", 
	 (int) offsetof(union resp_pkt_u_tag, data));
  printf("offsetof(u32) = %d\n", 
	 (int) offsetof(union resp_pkt_u_tag, u32));
  printf("\n");

  printf("sizeof(struct resp_pkt) = %d\n", 
	 (int) sizeof(struct resp_pkt));
  printf("offsetof(rm_vn_mode) = %d\n", 
	 (int) offsetof(struct resp_pkt, rm_vn_mode));
  printf("offsetof(auth_seq) = %d\n", 
	 (int) offsetof(struct resp_pkt, auth_seq));
  printf("offsetof(implementation) = %d\n", 
	 (int) offsetof(struct resp_pkt, implementation));
  printf("offsetof(request) = %d\n", 
	 (int) offsetof(struct resp_pkt, request));
  printf("offsetof(err_nitems) = %d\n", 
	 (int) offsetof(struct resp_pkt, err_nitems));
  printf("offsetof(mbz_itemsize) = %d\n", 
	 (int) offsetof(struct resp_pkt, mbz_itemsize));
  printf("offsetof(u) = %d\n", 
	 (int) offsetof(struct resp_pkt, u));
  printf("\n");

  printf("sizeof(struct info_peer_list) = %d\n", 
	 (int) sizeof(struct info_peer_list));
  printf("offsetof(addr) = %d\n", 
	 (int) offsetof(struct info_peer_list, addr));
  printf("offsetof(port) = %d\n", 
	 (int) offsetof(struct info_peer_list, port));
  printf("offsetof(hmode) = %d\n", 
	 (int) offsetof(struct info_peer_list, hmode));
  printf("offsetof(flags) = %d\n", 
	 (int) offsetof(struct info_peer_list, flags));
  printf("offsetof(v6_flag) = %d\n", 
	 (int) offsetof(struct info_peer_list, v6_flag));
  printf("offsetof(unused1) = %d\n", 
	 (int) offsetof(struct info_peer_list, unused1));
  printf("offsetof(addr6) = %d\n", 
	 (int) offsetof(struct info_peer_list, addr6));
  printf("\n");

  printf("sizeof(struct info_peer_summary) = %d\n", 
	 (int) sizeof(struct info_peer_summary));
  printf("offsetof(dstadr) = %d\n", 
	 (int) offsetof(struct info_peer_summary, dstadr));
  printf("offsetof(srcadr) = %d\n", 
	 (int) offsetof(struct info_peer_summary, srcadr));
  printf("offsetof(srcport) = %d\n", 
	 (int) offsetof(struct info_peer_summary, srcport));
  printf("offsetof(stratum) = %d\n", 
	 (int) offsetof(struct info_peer_summary, stratum));
  printf("offsetof(hpoll) = %d\n", 
	 (int) offsetof(struct info_peer_summary, hpoll));
  printf("offsetof(ppoll) = %d\n", 
	 (int) offsetof(struct info_peer_summary, ppoll));
  printf("offsetof(reach) = %d\n", 
	 (int) offsetof(struct info_peer_summary, reach));
  printf("offsetof(flags) = %d\n", 
	 (int) offsetof(struct info_peer_summary, flags));
  printf("offsetof(hmode) = %d\n", 
	 (int) offsetof(struct info_peer_summary, hmode));
  printf("offsetof(delay) = %d\n", 
	 (int) offsetof(struct info_peer_summary, delay));
  printf("offsetof(offset) = %d\n", 
	 (int) offsetof(struct info_peer_summary, offset));
  printf("offsetof(dispersion) = %d\n", 
	 (int) offsetof(struct info_peer_summary, dispersion));
  printf("offsetof(v6_flag) = %d\n", 
	 (int) offsetof(struct info_peer_summary, v6_flag));
  printf("offsetof(unused1) = %d\n", 
	 (int) offsetof(struct info_peer_summary, unused1));
  printf("offsetof(dstadr6) = %d\n", 
	 (int) offsetof(struct info_peer_summary, dstadr6));
  printf("offsetof(srcadr6) = %d\n", 
	 (int) offsetof(struct info_peer_summary, srcadr6));
  printf("\n");

  printf("sizeof(struct info_peer) = %d\n", 
	 (int) sizeof(struct info_peer));
  printf("offsetof(dstadr) = %d\n", 
	 (int) offsetof(struct info_peer, dstadr));
  printf("offsetof(srcadr) = %d\n", 
	 (int) offsetof(struct info_peer, srcadr));
  printf("offsetof(srcport) = %d\n", 
	 (int) offsetof(struct info_peer, srcport));
  printf("offsetof(flags) = %d\n", 
	 (int) offsetof(struct info_peer, flags));
  printf("offsetof(leap) = %d\n", 
	 (int) offsetof(struct info_peer, leap));
  printf("offsetof(hmode) = %d\n", 
	 (int) offsetof(struct info_peer, hmode));
  printf("offsetof(pmode) = %d\n", 
	 (int) offsetof(struct info_peer, pmode));
  printf("offsetof(stratum) = %d\n", 
	 (int) offsetof(struct info_peer, stratum));
  printf("offsetof(ppoll) = %d\n", 
	 (int) offsetof(struct info_peer, ppoll));
  printf("offsetof(hpoll) = %d\n", 
	 (int) offsetof(struct info_peer, hpoll));
  printf("offsetof(precision) = %d\n", 
	 (int) offsetof(struct info_peer, precision));
  printf("offsetof(version) = %d\n", 
	 (int) offsetof(struct info_peer, version));
  printf("offsetof(unused8) = %d\n", 
	 (int) offsetof(struct info_peer, unused8));
  printf("offsetof(reach) = %d\n", 
	 (int) offsetof(struct info_peer, reach));
  printf("offsetof(unreach) = %d\n", 
	 (int) offsetof(struct info_peer, unreach));
  printf("offsetof(flash) = %d\n", 
	 (int) offsetof(struct info_peer, flash));
  printf("offsetof(ttl) = %d\n", 
	 (int) offsetof(struct info_peer, ttl));
  printf("offsetof(flash2) = %d\n", 
	 (int) offsetof(struct info_peer, flash2));
  printf("offsetof(associd) = %d\n", 
	 (int) offsetof(struct info_peer, associd));
  printf("offsetof(keyid) = %d\n", 
	 (int) offsetof(struct info_peer, keyid));
  printf("offsetof(pkeyid) = %d\n", 
	 (int) offsetof(struct info_peer, pkeyid));
  printf("offsetof(refid) = %d\n", 
	 (int) offsetof(struct info_peer, refid));
  printf("offsetof(timer) = %d\n", 
	 (int) offsetof(struct info_peer, timer));
  printf("offsetof(rootdelay) = %d\n", 
	 (int) offsetof(struct info_peer, rootdelay));
  printf("offsetof(rootdispersion) = %d\n", 
	 (int) offsetof(struct info_peer, rootdispersion));
  printf("offsetof(reftime) = %d\n", 
	 (int) offsetof(struct info_peer, reftime));
  printf("offsetof(org) = %d\n", 
	 (int) offsetof(struct info_peer, org));
  printf("offsetof(rec) = %d\n", 
	 (int) offsetof(struct info_peer, rec));
  printf("offsetof(xmt) = %d\n", 
	 (int) offsetof(struct info_peer, xmt));
  printf("offsetof(filtdelay) = %d\n", 
	 (int) offsetof(struct info_peer, filtdelay));
  printf("offsetof(filtoffset) = %d\n", 
	 (int) offsetof(struct info_peer, filtoffset));
  printf("offsetof(order) = %d\n", 
	 (int) offsetof(struct info_peer, order));
  printf("offsetof(delay) = %d\n", 
	 (int) offsetof(struct info_peer, delay));
  printf("offsetof(dispersion) = %d\n", 
	 (int) offsetof(struct info_peer, dispersion));
  printf("offsetof(offset) = %d\n", 
	 (int) offsetof(struct info_peer, offset));
  printf("offsetof(selectdisp) = %d\n", 
	 (int) offsetof(struct info_peer, selectdisp));
  printf("offsetof(unused1) = %d\n", 
	 (int) offsetof(struct info_peer, unused1));
  printf("offsetof(unused2) = %d\n", 
	 (int) offsetof(struct info_peer, unused2));
  printf("offsetof(unused3) = %d\n", 
	 (int) offsetof(struct info_peer, unused3));
  printf("offsetof(unused4) = %d\n", 
	 (int) offsetof(struct info_peer, unused4));
  printf("offsetof(unused5) = %d\n", 
	 (int) offsetof(struct info_peer, unused5));
  printf("offsetof(unused6) = %d\n", 
	 (int) offsetof(struct info_peer, unused6));
  printf("offsetof(unused7) = %d\n", 
	 (int) offsetof(struct info_peer, unused7));
  printf("offsetof(estbdelay) = %d\n", 
	 (int) offsetof(struct info_peer, estbdelay));
  printf("offsetof(v6_flag) = %d\n", 
	 (int) offsetof(struct info_peer, v6_flag));
  printf("offsetof(unused9) = %d\n", 
	 (int) offsetof(struct info_peer, unused9));
  printf("offsetof(dstadr6) = %d\n", 
	 (int) offsetof(struct info_peer, dstadr6));
  printf("offsetof(srcadr6) = %d\n", 
	 (int) offsetof(struct info_peer, srcadr6));
  printf("\n");

  printf("sizeof(struct info_peer_stats) = %d\n", 
	 (int) sizeof(struct info_peer_stats));
  printf("offsetof(dstadr) = %d\n", 
	 (int) offsetof(struct info_peer_stats, dstadr));
  printf("offsetof(srcadr) = %d\n", 
	 (int) offsetof(struct info_peer_stats, srcadr));
  printf("offsetof(srcport) = %d\n", 
	 (int) offsetof(struct info_peer_stats, srcport));
  printf("offsetof(flags) = %d\n", 
	 (int) offsetof(struct info_peer_stats, flags));
  printf("offsetof(timereset) = %d\n", 
	 (int) offsetof(struct info_peer_stats, timereset));
  printf("offsetof(timereceived) = %d\n", 
	 (int) offsetof(struct info_peer_stats, timereceived));
  printf("offsetof(timetosend) = %d\n", 
	 (int) offsetof(struct info_peer_stats, timetosend));
  printf("offsetof(timereachable) = %d\n", 
	 (int) offsetof(struct info_peer_stats, timereachable));
  printf("offsetof(sent) = %d\n", 
	 (int) offsetof(struct info_peer_stats, sent));
  printf("offsetof(unused1) = %d\n", 
	 (int) offsetof(struct info_peer_stats, unused1));
  printf("offsetof(processed) = %d\n", 
	 (int) offsetof(struct info_peer_stats, processed));
  printf("offsetof(unused2) = %d\n", 
	 (int) offsetof(struct info_peer_stats, unused2));
  printf("offsetof(badauth) = %d\n", 
	 (int) offsetof(struct info_peer_stats, badauth));
  printf("offsetof(bogusorg) = %d\n", 
	 (int) offsetof(struct info_peer_stats, bogusorg));
  printf("offsetof(oldpkt) = %d\n", 
	 (int) offsetof(struct info_peer_stats, oldpkt));
  printf("offsetof(unused3) = %d\n", 
	 (int) offsetof(struct info_peer_stats, unused3));
  printf("offsetof(unused4) = %d\n", 
	 (int) offsetof(struct info_peer_stats, unused4));
  printf("offsetof(seldisp) = %d\n", 
	 (int) offsetof(struct info_peer_stats, seldisp));
  printf("offsetof(selbroken) = %d\n", 
	 (int) offsetof(struct info_peer_stats, selbroken));
  printf("offsetof(unused5) = %d\n", 
	 (int) offsetof(struct info_peer_stats, unused5));
  printf("offsetof(candidate) = %d\n", 
	 (int) offsetof(struct info_peer_stats, candidate));
  printf("offsetof(unused6) = %d\n", 
	 (int) offsetof(struct info_peer_stats, unused6));
  printf("offsetof(unused7) = %d\n", 
	 (int) offsetof(struct info_peer_stats, unused7));
  printf("offsetof(unused8) = %d\n", 
	 (int) offsetof(struct info_peer_stats, unused8));
  printf("offsetof(v6_flag) = %d\n", 
	 (int) offsetof(struct info_peer_stats, v6_flag));
  printf("offsetof(unused9) = %d\n", 
	 (int) offsetof(struct info_peer_stats, unused9));
  printf("offsetof(dstadr6) = %d\n", 
	 (int) offsetof(struct info_peer_stats, dstadr6));
  printf("offsetof(srcadr6) = %d\n", 
	 (int) offsetof(struct info_peer_stats, srcadr6));
  printf("\n");

  printf("sizeof(struct info_loop) = %d\n", 
	 (int) sizeof(struct info_loop));
  printf("offsetof(last_offset) = %d\n", 
	 (int) offsetof(struct info_loop, last_offset));
  printf("offsetof(drift_comp) = %d\n", 
	 (int) offsetof(struct info_loop, drift_comp));
  printf("offsetof(compliance) = %d\n", 
	 (int) offsetof(struct info_loop, compliance));
  printf("offsetof(watchdog_timer) = %d\n", 
	 (int) offsetof(struct info_loop, watchdog_timer));
  printf("\n");

  printf("sizeof(struct info_sys) = %d\n", 
	 (int) sizeof(struct info_sys));
  printf("offsetof(peer) = %d\n", 
	 (int) offsetof(struct info_sys, peer));
  printf("offsetof(peer_mode) = %d\n", 
	 (int) offsetof(struct info_sys, peer_mode));
  printf("offsetof(leap) = %d\n", 
	 (int) offsetof(struct info_sys, leap));
  printf("offsetof(stratum) = %d\n", 
	 (int) offsetof(struct info_sys, stratum));
  printf("offsetof(precision) = %d\n", 
	 (int) offsetof(struct info_sys, precision));
  printf("offsetof(rootdelay) = %d\n", 
	 (int) offsetof(struct info_sys, rootdelay));
  printf("offsetof(rootdispersion) = %d\n", 
	 (int) offsetof(struct info_sys, rootdispersion));
  printf("offsetof(refid) = %d\n", 
	 (int) offsetof(struct info_sys, refid));
  printf("offsetof(reftime) = %d\n", 
	 (int) offsetof(struct info_sys, reftime));
  printf("offsetof(poll) = %d\n", 
	 (int) offsetof(struct info_sys, poll));
  printf("offsetof(flags) = %d\n", 
	 (int) offsetof(struct info_sys, flags));
  printf("offsetof(unused1) = %d\n", 
	 (int) offsetof(struct info_sys, unused1));
  printf("offsetof(unused2) = %d\n", 
	 (int) offsetof(struct info_sys, unused2));
  printf("offsetof(unused3) = %d\n", 
	 (int) offsetof(struct info_sys, unused3));
  printf("offsetof(bdelay) = %d\n", 
	 (int) offsetof(struct info_sys, bdelay));
  printf("offsetof(frequency) = %d\n", 
	 (int) offsetof(struct info_sys, frequency));
  printf("offsetof(authdelay) = %d\n", 
	 (int) offsetof(struct info_sys, authdelay));
  printf("offsetof(stability) = %d\n", 
	 (int) offsetof(struct info_sys, stability));
  printf("offsetof(v6_flag) = %d\n", 
	 (int) offsetof(struct info_sys, v6_flag));
  printf("offsetof(unused4) = %d\n", 
	 (int) offsetof(struct info_sys, unused4));
  printf("offsetof(peer6) = %d\n", 
	 (int) offsetof(struct info_sys, peer6));
  printf("\n");

  printf("sizeof(struct info_sys_stats) = %d\n", 
	 (int) sizeof(struct info_sys_stats));
  printf("offsetof(timeup) = %d\n", 
	 (int) offsetof(struct info_sys_stats, timeup));
  printf("offsetof(timereset) = %d\n", 
	 (int) offsetof(struct info_sys_stats, timereset));
  printf("offsetof(denied) = %d\n", 
	 (int) offsetof(struct info_sys_stats, denied));
  printf("offsetof(oldversionpkt) = %d\n", 
	 (int) offsetof(struct info_sys_stats, oldversionpkt));
  printf("offsetof(newversionpkt) = %d\n", 
	 (int) offsetof(struct info_sys_stats, newversionpkt));
  printf("offsetof(unknownversion) = %d\n", 
	 (int) offsetof(struct info_sys_stats, unknownversion));
  printf("offsetof(badlength) = %d\n", 
	 (int) offsetof(struct info_sys_stats, badlength));
  printf("offsetof(processed) = %d\n", 
	 (int) offsetof(struct info_sys_stats, processed));
  printf("offsetof(badauth) = %d\n", 
	 (int) offsetof(struct info_sys_stats, badauth));
  printf("offsetof(received) = %d\n", 
	 (int) offsetof(struct info_sys_stats, received));
  printf("offsetof(limitrejected) = %d\n", 
	 (int) offsetof(struct info_sys_stats, limitrejected));
  printf("\n");

  printf("sizeof(struct old_info_sys_stats) = %d\n", 
	 (int) sizeof(struct old_info_sys_stats));
  printf("offsetof(timeup) = %d\n", 
	 (int) offsetof(struct old_info_sys_stats, timeup));
  printf("offsetof(timereset) = %d\n", 
	 (int) offsetof(struct old_info_sys_stats, timereset));
  printf("offsetof(denied) = %d\n", 
	 (int) offsetof(struct old_info_sys_stats, denied));
  printf("offsetof(oldversionpkt) = %d\n", 
	 (int) offsetof(struct old_info_sys_stats, oldversionpkt));
  printf("offsetof(newversionpkt) = %d\n", 
	 (int) offsetof(struct old_info_sys_stats, newversionpkt));
  printf("offsetof(unknownversion) = %d\n", 
	 (int) offsetof(struct old_info_sys_stats, unknownversion));
  printf("offsetof(badlength) = %d\n", 
	 (int) offsetof(struct old_info_sys_stats, badlength));
  printf("offsetof(processed) = %d\n", 
	 (int) offsetof(struct old_info_sys_stats, processed));
  printf("offsetof(badauth) = %d\n", 
	 (int) offsetof(struct old_info_sys_stats, badauth));
  printf("offsetof(wanderhold) = %d\n", 
	 (int) offsetof(struct old_info_sys_stats, wanderhold));
  printf("\n");

  printf("sizeof(struct info_mem_stats) = %d\n", 
	 (int) sizeof(struct info_mem_stats));
  printf("offsetof(timereset) = %d\n", 
	 (int) offsetof(struct info_mem_stats, timereset));
  printf("offsetof(totalpeermem) = %d\n", 
	 (int) offsetof(struct info_mem_stats, totalpeermem));
  printf("offsetof(freepeermem) = %d\n", 
	 (int) offsetof(struct info_mem_stats, freepeermem));
  printf("offsetof(findpeer_calls) = %d\n", 
	 (int) offsetof(struct info_mem_stats, findpeer_calls));
  printf("offsetof(allocations) = %d\n", 
	 (int) offsetof(struct info_mem_stats, allocations));
  printf("offsetof(demobilizations) = %d\n", 
	 (int) offsetof(struct info_mem_stats, demobilizations));
  printf("offsetof(hashcount) = %d\n", 
	 (int) offsetof(struct info_mem_stats, hashcount));
  printf("\n");

  printf("sizeof(struct info_io_stats) = %d\n", 
	 (int) sizeof(struct info_io_stats));
  printf("offsetof(timereset) = %d\n", 
	 (int) offsetof(struct info_io_stats, timereset));
  printf("offsetof(totalrecvbufs) = %d\n", 
	 (int) offsetof(struct info_io_stats, totalrecvbufs));
  printf("offsetof(freerecvbufs) = %d\n", 
	 (int) offsetof(struct info_io_stats, freerecvbufs));
  printf("offsetof(fullrecvbufs) = %d\n", 
	 (int) offsetof(struct info_io_stats, fullrecvbufs));
  printf("offsetof(lowwater) = %d\n", 
	 (int) offsetof(struct info_io_stats, lowwater));
  printf("offsetof(dropped) = %d\n", 
	 (int) offsetof(struct info_io_stats, dropped));
  printf("offsetof(ignored) = %d\n", 
	 (int) offsetof(struct info_io_stats, ignored));
  printf("offsetof(received) = %d\n", 
	 (int) offsetof(struct info_io_stats, received));
  printf("offsetof(sent) = %d\n", 
	 (int) offsetof(struct info_io_stats, sent));
  printf("offsetof(notsent) = %d\n", 
	 (int) offsetof(struct info_io_stats, notsent));
  printf("offsetof(interrupts) = %d\n", 
	 (int) offsetof(struct info_io_stats, interrupts));
  printf("offsetof(int_received) = %d\n", 
	 (int) offsetof(struct info_io_stats, int_received));
  printf("\n");

  printf("sizeof(struct info_timer_stats) = %d\n", 
	 (int) sizeof(struct info_timer_stats));
  printf("offsetof(timereset) = %d\n", 
	 (int) offsetof(struct info_timer_stats, timereset));
  printf("offsetof(alarms) = %d\n", 
	 (int) offsetof(struct info_timer_stats, alarms));
  printf("offsetof(overflows) = %d\n", 
	 (int) offsetof(struct info_timer_stats, overflows));
  printf("offsetof(xmtcalls) = %d\n", 
	 (int) offsetof(struct info_timer_stats, xmtcalls));
  printf("\n");

  printf("sizeof(struct old_conf_peer) = %d\n", 
	 (int) sizeof(struct old_conf_peer));
  printf("offsetof(peeraddr) = %d\n", 
	 (int) offsetof(struct old_conf_peer, peeraddr));
  printf("offsetof(hmode) = %d\n", 
	 (int) offsetof(struct old_conf_peer, hmode));
  printf("offsetof(version) = %d\n", 
	 (int) offsetof(struct old_conf_peer, version));
  printf("offsetof(minpoll) = %d\n", 
	 (int) offsetof(struct old_conf_peer, minpoll));
  printf("offsetof(maxpoll) = %d\n", 
	 (int) offsetof(struct old_conf_peer, maxpoll));
  printf("offsetof(flags) = %d\n", 
	 (int) offsetof(struct old_conf_peer, flags));
  printf("offsetof(ttl) = %d\n", 
	 (int) offsetof(struct old_conf_peer, ttl));
  printf("offsetof(unused) = %d\n", 
	 (int) offsetof(struct old_conf_peer, unused));
  printf("offsetof(keyid) = %d\n", 
	 (int) offsetof(struct old_conf_peer, keyid));
  printf("\n");

  printf("sizeof(struct conf_peer) = %d\n", 
	 (int) sizeof(struct conf_peer));
  printf("offsetof(peeraddr) = %d\n", 
	 (int) offsetof(struct conf_peer, peeraddr));
  printf("offsetof(hmode) = %d\n", 
	 (int) offsetof(struct conf_peer, hmode));
  printf("offsetof(version) = %d\n", 
	 (int) offsetof(struct conf_peer, version));
  printf("offsetof(minpoll) = %d\n", 
	 (int) offsetof(struct conf_peer, minpoll));
  printf("offsetof(maxpoll) = %d\n", 
	 (int) offsetof(struct conf_peer, maxpoll));
  printf("offsetof(flags) = %d\n", 
	 (int) offsetof(struct conf_peer, flags));
  printf("offsetof(ttl) = %d\n", 
	 (int) offsetof(struct conf_peer, ttl));
  printf("offsetof(unused1) = %d\n", 
	 (int) offsetof(struct conf_peer, unused1));
  printf("offsetof(keyid) = %d\n", 
	 (int) offsetof(struct conf_peer, keyid));
  printf("offsetof(keystr) = %d\n", 
	 (int) offsetof(struct conf_peer, keystr));
  printf("offsetof(v6_flag) = %d\n", 
	 (int) offsetof(struct conf_peer, v6_flag));
  printf("offsetof(unused2) = %d\n", 
	 (int) offsetof(struct conf_peer, unused2));
  printf("offsetof(peeraddr6) = %d\n", 
	 (int) offsetof(struct conf_peer, peeraddr6));
  printf("\n");

  printf("sizeof(struct conf_unpeer) = %d\n", 
	 (int) sizeof(struct conf_unpeer));
  printf("offsetof(peeraddr) = %d\n", 
	 (int) offsetof(struct conf_unpeer, peeraddr));
  printf("offsetof(v6_flag) = %d\n", 
	 (int) offsetof(struct conf_unpeer, v6_flag));
  printf("offsetof(peeraddr6) = %d\n", 
	 (int) offsetof(struct conf_unpeer, peeraddr6));
  printf("\n");

  printf("sizeof(struct conf_sys_flags) = %d\n", 
	 (int) sizeof(struct conf_sys_flags));
  printf("offsetof(flags) = %d\n", 
	 (int) offsetof(struct conf_sys_flags, flags));
  printf("\n");

  printf("sizeof(struct info_restrict) = %d\n", 
	 (int) sizeof(struct info_restrict));
  printf("offsetof(addr) = %d\n", 
	 (int) offsetof(struct info_restrict, addr));
  printf("offsetof(mask) = %d\n", 
	 (int) offsetof(struct info_restrict, mask));
  printf("offsetof(count) = %d\n", 
	 (int) offsetof(struct info_restrict, count));
  printf("offsetof(flags) = %d\n", 
	 (int) offsetof(struct info_restrict, flags));
  printf("offsetof(mflags) = %d\n", 
	 (int) offsetof(struct info_restrict, mflags));
  printf("offsetof(v6_flag) = %d\n", 
	 (int) offsetof(struct info_restrict, v6_flag));
  printf("offsetof(unused1) = %d\n", 
	 (int) offsetof(struct info_restrict, unused1));
  printf("offsetof(addr6) = %d\n", 
	 (int) offsetof(struct info_restrict, addr6));
  printf("offsetof(mask6) = %d\n", 
	 (int) offsetof(struct info_restrict, mask6));
  printf("\n");

  printf("sizeof(struct conf_restrict) = %d\n", 
	 (int) sizeof(struct conf_restrict));
  printf("offsetof(addr) = %d\n", 
	 (int) offsetof(struct conf_restrict, addr));
  printf("offsetof(mask) = %d\n", 
	 (int) offsetof(struct conf_restrict, mask));
  printf("offsetof(flags) = %d\n", 
	 (int) offsetof(struct conf_restrict, flags));
  printf("offsetof(mflags) = %d\n", 
	 (int) offsetof(struct conf_restrict, mflags));
  printf("offsetof(v6_flag) = %d\n", 
	 (int) offsetof(struct conf_restrict, v6_flag));
  printf("offsetof(addr6) = %d\n", 
	 (int) offsetof(struct conf_restrict, addr6));
  printf("offsetof(mask6) = %d\n", 
	 (int) offsetof(struct conf_restrict, mask6));
  printf("\n");

  printf("sizeof(struct info_monitor_1) = %d\n", 
	 (int) sizeof(struct info_monitor_1));
  printf("offsetof(avg_int) = %d\n", 
	 (int) offsetof(struct info_monitor_1, avg_int));
  printf("offsetof(last_int) = %d\n", 
	 (int) offsetof(struct info_monitor_1, last_int));
  printf("offsetof(restr) = %d\n", 
	 (int) offsetof(struct info_monitor_1, restr));
  printf("offsetof(count) = %d\n", 
	 (int) offsetof(struct info_monitor_1, count));
  printf("offsetof(addr) = %d\n", 
	 (int) offsetof(struct info_monitor_1, addr));
  printf("offsetof(daddr) = %d\n", 
	 (int) offsetof(struct info_monitor_1, daddr));
  printf("offsetof(flags) = %d\n", 
	 (int) offsetof(struct info_monitor_1, flags));
  printf("offsetof(port) = %d\n", 
	 (int) offsetof(struct info_monitor_1, port));
  printf("offsetof(mode) = %d\n", 
	 (int) offsetof(struct info_monitor_1, mode));
  printf("offsetof(version) = %d\n", 
	 (int) offsetof(struct info_monitor_1, version));
  printf("offsetof(v6_flag) = %d\n", 
	 (int) offsetof(struct info_monitor_1, v6_flag));
  printf("offsetof(unused1) = %d\n", 
	 (int) offsetof(struct info_monitor_1, unused1));
  printf("offsetof(addr6) = %d\n", 
	 (int) offsetof(struct info_monitor_1, addr6));
  printf("offsetof(daddr6) = %d\n", 
	 (int) offsetof(struct info_monitor_1, daddr6));
  printf("\n");

  printf("sizeof(struct info_monitor) = %d\n", 
	 (int) sizeof(struct info_monitor));
  printf("offsetof(avg_int) = %d\n", 
	 (int) offsetof(struct info_monitor, avg_int));
  printf("offsetof(last_int) = %d\n", 
	 (int) offsetof(struct info_monitor, last_int));
  printf("offsetof(restr) = %d\n", 
	 (int) offsetof(struct info_monitor, restr));
  printf("offsetof(count) = %d\n", 
	 (int) offsetof(struct info_monitor, count));
  printf("offsetof(addr) = %d\n", 
	 (int) offsetof(struct info_monitor, addr));
  printf("offsetof(port) = %d\n", 
	 (int) offsetof(struct info_monitor, port));
  printf("offsetof(mode) = %d\n", 
	 (int) offsetof(struct info_monitor, mode));
  printf("offsetof(version) = %d\n", 
	 (int) offsetof(struct info_monitor, version));
  printf("offsetof(v6_flag) = %d\n", 
	 (int) offsetof(struct info_monitor, v6_flag));
  printf("offsetof(unused1) = %d\n", 
	 (int) offsetof(struct info_monitor, unused1));
  printf("offsetof(addr6) = %d\n", 
	 (int) offsetof(struct info_monitor, addr6));
  printf("\n");

  printf("sizeof(struct old_info_monitor) = %d\n", 
	 (int) sizeof(struct old_info_monitor));
  printf("offsetof(lasttime) = %d\n", 
	 (int) offsetof(struct old_info_monitor, lasttime));
  printf("offsetof(firsttime) = %d\n", 
	 (int) offsetof(struct old_info_monitor, firsttime));
  printf("offsetof(count) = %d\n", 
	 (int) offsetof(struct old_info_monitor, count));
  printf("offsetof(addr) = %d\n", 
	 (int) offsetof(struct old_info_monitor, addr));
  printf("offsetof(port) = %d\n", 
	 (int) offsetof(struct old_info_monitor, port));
  printf("offsetof(mode) = %d\n", 
	 (int) offsetof(struct old_info_monitor, mode));
  printf("offsetof(version) = %d\n", 
	 (int) offsetof(struct old_info_monitor, version));
  printf("offsetof(v6_flag) = %d\n", 
	 (int) offsetof(struct old_info_monitor, v6_flag));
  printf("offsetof(addr6) = %d\n", 
	 (int) offsetof(struct old_info_monitor, addr6));
  printf("\n");

  printf("sizeof(struct reset_flags) = %d\n", 
	 (int) sizeof(struct reset_flags));
  printf("offsetof(flags) = %d\n", 
	 (int) offsetof(struct reset_flags, flags));
  printf("\n");

  printf("sizeof(struct info_auth) = %d\n", 
	 (int) sizeof(struct info_auth));
  printf("offsetof(timereset) = %d\n", 
	 (int) offsetof(struct info_auth, timereset));
  printf("offsetof(numkeys) = %d\n", 
	 (int) offsetof(struct info_auth, numkeys));
  printf("offsetof(numfreekeys) = %d\n", 
	 (int) offsetof(struct info_auth, numfreekeys));
  printf("offsetof(keylookups) = %d\n", 
	 (int) offsetof(struct info_auth, keylookups));
  printf("offsetof(keynotfound) = %d\n", 
	 (int) offsetof(struct info_auth, keynotfound));
  printf("offsetof(encryptions) = %d\n", 
	 (int) offsetof(struct info_auth, encryptions));
  printf("offsetof(decryptions) = %d\n", 
	 (int) offsetof(struct info_auth, decryptions));
  printf("offsetof(expired) = %d\n", 
	 (int) offsetof(struct info_auth, expired));
  printf("offsetof(keyuncached) = %d\n", 
	 (int) offsetof(struct info_auth, keyuncached));
  printf("\n");

  printf("sizeof(struct info_trap) = %d\n", 
	 (int) sizeof(struct info_trap));
  printf("offsetof(local_address) = %d\n", 
	 (int) offsetof(struct info_trap, local_address));
  printf("offsetof(trap_address) = %d\n", 
	 (int) offsetof(struct info_trap, trap_address));
  printf("offsetof(trap_port) = %d\n", 
	 (int) offsetof(struct info_trap, trap_port));
  printf("offsetof(sequence) = %d\n", 
	 (int) offsetof(struct info_trap, sequence));
  printf("offsetof(settime) = %d\n", 
	 (int) offsetof(struct info_trap, settime));
  printf("offsetof(origtime) = %d\n", 
	 (int) offsetof(struct info_trap, origtime));
  printf("offsetof(resets) = %d\n", 
	 (int) offsetof(struct info_trap, resets));
  printf("offsetof(flags) = %d\n", 
	 (int) offsetof(struct info_trap, flags));
  printf("offsetof(v6_flag) = %d\n", 
	 (int) offsetof(struct info_trap, v6_flag));
  printf("offsetof(local_address6) = %d\n", 
	 (int) offsetof(struct info_trap, local_address6));
  printf("offsetof(trap_address6) = %d\n", 
	 (int) offsetof(struct info_trap, trap_address6));
  printf("\n");

  printf("sizeof(struct conf_trap) = %d\n", 
	 (int) sizeof(struct conf_trap));
  printf("offsetof(local_address) = %d\n", 
	 (int) offsetof(struct conf_trap, local_address));
  printf("offsetof(trap_address) = %d\n", 
	 (int) offsetof(struct conf_trap, trap_address));
  printf("offsetof(trap_port) = %d\n", 
	 (int) offsetof(struct conf_trap, trap_port));
  printf("offsetof(unused) = %d\n", 
	 (int) offsetof(struct conf_trap, unused));
  printf("offsetof(v6_flag) = %d\n", 
	 (int) offsetof(struct conf_trap, v6_flag));
  printf("offsetof(local_address6) = %d\n", 
	 (int) offsetof(struct conf_trap, local_address6));
  printf("offsetof(trap_address6) = %d\n", 
	 (int) offsetof(struct conf_trap, trap_address6));
  printf("\n");

  printf("sizeof(struct info_control) = %d\n", 
	 (int) sizeof(struct info_control));
  printf("offsetof(ctltimereset) = %d\n", 
	 (int) offsetof(struct info_control, ctltimereset));
  printf("offsetof(numctlreq) = %d\n", 
	 (int) offsetof(struct info_control, numctlreq));
  printf("offsetof(numctlbadpkts) = %d\n", 
	 (int) offsetof(struct info_control, numctlbadpkts));
  printf("offsetof(numctlresponses) = %d\n", 
	 (int) offsetof(struct info_control, numctlresponses));
  printf("offsetof(numctlfrags) = %d\n", 
	 (int) offsetof(struct info_control, numctlfrags));
  printf("offsetof(numctlerrors) = %d\n", 
	 (int) offsetof(struct info_control, numctlerrors));
  printf("offsetof(numctltooshort) = %d\n", 
	 (int) offsetof(struct info_control, numctltooshort));
  printf("offsetof(numctlinputresp) = %d\n", 
	 (int) offsetof(struct info_control, numctlinputresp));
  printf("offsetof(numctlinputfrag) = %d\n", 
	 (int) offsetof(struct info_control, numctlinputfrag));
  printf("offsetof(numctlinputerr) = %d\n", 
	 (int) offsetof(struct info_control, numctlinputerr));
  printf("offsetof(numctlbadoffset) = %d\n", 
	 (int) offsetof(struct info_control, numctlbadoffset));
  printf("offsetof(numctlbadversion) = %d\n", 
	 (int) offsetof(struct info_control, numctlbadversion));
  printf("offsetof(numctldatatooshort) = %d\n", 
	 (int) offsetof(struct info_control, numctldatatooshort));
  printf("offsetof(numctlbadop) = %d\n", 
	 (int) offsetof(struct info_control, numctlbadop));
  printf("offsetof(numasyncmsgs) = %d\n", 
	 (int) offsetof(struct info_control, numasyncmsgs));
  printf("\n");

  printf("sizeof(struct info_clock) = %d\n", 
	 (int) sizeof(struct info_clock));
  printf("offsetof(clockadr) = %d\n", 
	 (int) offsetof(struct info_clock, clockadr));
  printf("offsetof(type) = %d\n", 
	 (int) offsetof(struct info_clock, type));
  printf("offsetof(flags) = %d\n", 
	 (int) offsetof(struct info_clock, flags));
  printf("offsetof(lastevent) = %d\n", 
	 (int) offsetof(struct info_clock, lastevent));
  printf("offsetof(currentstatus) = %d\n", 
	 (int) offsetof(struct info_clock, currentstatus));
  printf("offsetof(polls) = %d\n", 
	 (int) offsetof(struct info_clock, polls));
  printf("offsetof(noresponse) = %d\n", 
	 (int) offsetof(struct info_clock, noresponse));
  printf("offsetof(badformat) = %d\n", 
	 (int) offsetof(struct info_clock, badformat));
  printf("offsetof(baddata) = %d\n", 
	 (int) offsetof(struct info_clock, baddata));
  printf("offsetof(timestarted) = %d\n", 
	 (int) offsetof(struct info_clock, timestarted));
  printf("offsetof(fudgetime1) = %d\n", 
	 (int) offsetof(struct info_clock, fudgetime1));
  printf("offsetof(fudgetime2) = %d\n", 
	 (int) offsetof(struct info_clock, fudgetime2));
  printf("offsetof(fudgeval1) = %d\n", 
	 (int) offsetof(struct info_clock, fudgeval1));
  printf("offsetof(fudgeval2) = %d\n", 
	 (int) offsetof(struct info_clock, fudgeval2));
  printf("\n");

  printf("sizeof(struct conf_fudge) = %d\n", 
	 (int) sizeof(struct conf_fudge));
  printf("offsetof(clockadr) = %d\n", 
	 (int) offsetof(struct conf_fudge, clockadr));
  printf("offsetof(which) = %d\n", 
	 (int) offsetof(struct conf_fudge, which));
  printf("offsetof(fudgetime) = %d\n", 
	 (int) offsetof(struct conf_fudge, fudgetime));
  printf("offsetof(fudgeval_flags) = %d\n", 
	 (int) offsetof(struct conf_fudge, fudgeval_flags));
  printf("\n");

  printf("sizeof(struct info_clkbug) = %d\n", 
	 (int) sizeof(struct info_clkbug));
  printf("offsetof(clockadr) = %d\n", 
	 (int) offsetof(struct info_clkbug, clockadr));
  printf("offsetof(nvalues) = %d\n", 
	 (int) offsetof(struct info_clkbug, nvalues));
  printf("offsetof(ntimes) = %d\n", 
	 (int) offsetof(struct info_clkbug, ntimes));
  printf("offsetof(svalues) = %d\n", 
	 (int) offsetof(struct info_clkbug, svalues));
  printf("offsetof(stimes) = %d\n", 
	 (int) offsetof(struct info_clkbug, stimes));
  printf("offsetof(values) = %d\n", 
	 (int) offsetof(struct info_clkbug, values));
  printf("offsetof(times) = %d\n", 
	 (int) offsetof(struct info_clkbug, times));
  printf("\n");

  printf("sizeof(struct info_kernel) = %d\n", 
	 (int) sizeof(struct info_kernel));
  printf("offsetof(offset) = %d\n", 
	 (int) offsetof(struct info_kernel, offset));
  printf("offsetof(freq) = %d\n", 
	 (int) offsetof(struct info_kernel, freq));
  printf("offsetof(maxerror) = %d\n", 
	 (int) offsetof(struct info_kernel, maxerror));
  printf("offsetof(esterror) = %d\n", 
	 (int) offsetof(struct info_kernel, esterror));
  printf("offsetof(status) = %d\n", 
	 (int) offsetof(struct info_kernel, status));
  printf("offsetof(shift) = %d\n", 
	 (int) offsetof(struct info_kernel, shift));
  printf("offsetof(constant) = %d\n", 
	 (int) offsetof(struct info_kernel, constant));
  printf("offsetof(precision) = %d\n", 
	 (int) offsetof(struct info_kernel, precision));
  printf("offsetof(tolerance) = %d\n", 
	 (int) offsetof(struct info_kernel, tolerance));
  printf("offsetof(ppsfreq) = %d\n", 
	 (int) offsetof(struct info_kernel, ppsfreq));
  printf("offsetof(jitter) = %d\n", 
	 (int) offsetof(struct info_kernel, jitter));
  printf("offsetof(stabil) = %d\n", 
	 (int) offsetof(struct info_kernel, stabil));
  printf("offsetof(jitcnt) = %d\n", 
	 (int) offsetof(struct info_kernel, jitcnt));
  printf("offsetof(calcnt) = %d\n", 
	 (int) offsetof(struct info_kernel, calcnt));
  printf("offsetof(errcnt) = %d\n", 
	 (int) offsetof(struct info_kernel, errcnt));
  printf("offsetof(stbcnt) = %d\n", 
	 (int) offsetof(struct info_kernel, stbcnt));
  printf("\n");

  printf("sizeof(struct info_if_stats) = %d\n", 
	 (int) sizeof(struct info_if_stats));
  printf("offsetof(unaddr) = %d\n", 
	 (int) offsetof(struct info_if_stats, unaddr));
  printf("offsetof(unbcast) = %d\n", 
	 (int) offsetof(struct info_if_stats, unbcast));
  printf("offsetof(unmask) = %d\n", 
	 (int) offsetof(struct info_if_stats, unmask));
  printf("offsetof(v6_flag) = %d\n", 
	 (int) offsetof(struct info_if_stats, v6_flag));
  printf("offsetof(name) = %d\n", 
	 (int) offsetof(struct info_if_stats, name));
  printf("offsetof(flags) = %d\n", 
	 (int) offsetof(struct info_if_stats, flags));
  printf("offsetof(last_ttl) = %d\n", 
	 (int) offsetof(struct info_if_stats, last_ttl));
  printf("offsetof(num_mcast) = %d\n", 
	 (int) offsetof(struct info_if_stats, num_mcast));
  printf("offsetof(received) = %d\n", 
	 (int) offsetof(struct info_if_stats, received));
  printf("offsetof(sent) = %d\n", 
	 (int) offsetof(struct info_if_stats, sent));
  printf("offsetof(notsent) = %d\n", 
	 (int) offsetof(struct info_if_stats, notsent));
  printf("offsetof(uptime) = %d\n", 
	 (int) offsetof(struct info_if_stats, uptime));
  printf("offsetof(scopeid) = %d\n", 
	 (int) offsetof(struct info_if_stats, scopeid));
  printf("offsetof(ifindex) = %d\n", 
	 (int) offsetof(struct info_if_stats, ifindex));
  printf("offsetof(ifnum) = %d\n", 
	 (int) offsetof(struct info_if_stats, ifnum));
  printf("offsetof(peercnt) = %d\n", 
	 (int) offsetof(struct info_if_stats, peercnt));
  printf("offsetof(family) = %d\n", 
	 (int) offsetof(struct info_if_stats, family));
  printf("offsetof(ignore_packets) = %d\n", 
	 (int) offsetof(struct info_if_stats, ignore_packets));
  printf("offsetof(action) = %d\n", 
	 (int) offsetof(struct info_if_stats, action));
  printf("offsetof(_filler0) = %d\n", 
	 (int) offsetof(struct info_if_stats, _filler0));
  printf("\n");

  printf("sizeof(struct info_dns_assoc) = %d\n", 
	 (int) sizeof(struct info_dns_assoc));
  printf("offsetof(peeraddr) = %d\n", 
	 (int) offsetof(struct info_dns_assoc, peeraddr));
  printf("offsetof(associd) = %d\n", 
	 (int) offsetof(struct info_dns_assoc, associd));
  printf("offsetof(hostname) = %d\n", 
	 (int) offsetof(struct info_dns_assoc, hostname));
  printf("\n");

