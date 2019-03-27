#	$FreeBSD$

PACKAGE=	ipf
LIB=		ipf
INTERNALLIB=

SRCS=		addicmp.c addipopt.c alist_free.c alist_new.c allocmbt.c \
		assigndefined.c bcopywrap.c \
		binprint.c buildopts.c checkrev.c connecttcp.c \
		count4bits.c count6bits.c \
		debug.c dupmbt.c \
		facpri.c familyname.c \
		fill6bits.c findword.c \
		flags.c freembt.c ftov.c \
		genmask.c \
		gethost.c getifname.c geticmptype.c \
		getnattype.c getport.c getportproto.c getproto.c getsumd.c \
		hostname.c icmpcode.c icmptypename.c icmptypes.c \
		initparse.c interror.c ionames.c \
		ipf_dotuning.c ipf_perror.c ipft_hx.c ipft_pc.c \
		ipft_tx.c ipoptsec.c kmem.c kmemcpywrap.c \
		kvatoname.c load_dstlist.c load_dstlistnode.c load_file.c \
		load_hash.c load_hashnode.c \
		load_http.c load_pool.c load_poolnode.c load_url.c \
		mb_hexdump.c msgdsize.c \
		mutex_emul.c nametokva.c nat_setgroupmap.c ntomask.c \
		optname.c optprint.c optprintv6.c optvalue.c parsefields.c \
		parseipfexpr.c parsewhoisline.c poolio.c portname.c \
		prependmbt.c \
		print_toif.c printactiveaddr.c printactivenat.c printaddr.c \
		printaps.c printbuf.c printdstl_live.c printdstlist.c \
		printdstlistdata.c printdstlistnode.c printdstlistpolicy.c \
		printfieldhdr.c \
		printfr.c printfraginfo.c printhash.c printhash_live.c \
		printhashdata.c printhashnode.c printhost.c printhostmap.c \
		printhostmask.c printifname.c printip.c printipfexpr.c printiphdr.c printlog.c printlookup.c \
		printmask.c printnat.c printnataddr.c printnatfield.c printnatside.c printpacket.c printpacket6.c \
		printpool.c printpool_live.c printpooldata.c printpoolfield.c printpoolnode.c \
		printportcmp.c printproto.c printsbuf.c printstate.c printstatefields.c \
		printtcpflags.c \
		printtqtable.c printtunable.c printunit.c remove_hash.c remove_hashnode.c \
		remove_pool.c remove_poolnode.c resetlexer.c rwlock_emul.c \
		save_execute.c save_file.c save_nothing.c save_syslog.c save_v1trap.c save_v2trap.c vtof.c \
		tcp_flags.c tcpflags.c tcpoptnames.c v6ionames.c v6optvalue.c \
		var.c verbose.c

LIBADD=		kvm

.include <bsd.lib.mk>
