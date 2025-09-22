#	$OpenBSD: Makefile,v 1.870 2025/09/08 19:44:45 kettenis Exp $

MAN=	aac.4 abcrtc.4 abl.4 ac97.4 acphy.4 acrtc.4 \
	acpi.4 acpiac.4 acpials.4 acpiasus.4 acpibat.4 \
	acpibtn.4 acpicbkbd.4 acpicmos.4 acpicpu.4 acpidock.4 \
	acpiec.4 acpige.4 acpihid.4 acpihpet.4 acpihve.4 \
	acpimadt.4 acpimcfg.4 acpipci.4 acpiprt.4 acpipwrres.4 \
	acpisbs.4 acpisony.4 acpisurface.4 acpithinkpad.4 acpitoshiba.4 \
	acpitimer.4 acpitz.4 acpivideo.4 acpivout.4 acpiwmi.4 \
	acx.4 adc.4 addcom.4 adl.4 admcts.4 admlc.4 admtemp.4 \
	admtm.4 admtmp.4 admtt.4 adt.4 adtfsm.4 adv.4 age.4 alc.4 ale.4 \
	aggr.4 agp.4 \
	ahc.4 ahci.4 ahd.4 aibs.4 aic.4 \
	akbd.4 alipm.4 amas.4 amdgpio.4 amdiic.4 amdpm.4 amdpmc.4 ami.4 \
	amlclock.4 amldwusb.4 amliic.4 amlmmc.4 amlpciephy.4 amlpinctrl.4 \
	amlpwm.4 amlreset.4 amlrng.4 amlsm.4 amltemp.4 amluart.4 amlusbphy.4 \
	amphy.4 ams.4 an.4 andl.4 aplgpio.4 aps.4 aq.4 arc.4 arcofi.4 \
	asbtm.4 asmc.4 ast.4 atapiscsi.4 atphy.4 ath.4 athn.4 atu.4 atw.4 \
	auacer.4 audio.4 aue.4 auglx.4 auich.4 auixp.4 autri.4 auvia.4 \
	axe.4 axen.4 axppmic.4 azalia.4 \
	bce.4 bcmaux.4 bcmbsc.4 bcmclock.4 bcmdmac.4 bcmdog.4 bcmgpio.4 \
	bcmirng.4 bcmmbox.4 bcmpcie.4 bcmrng.4 bcmsdhost.4 \
	bcmstbgpio.4 bcmstbintc.4 bcmstbpinctrl.4 bcmstbrescal.4 \
	bcmstbreset.4 bcmtemp.4 bcmtmon.4 bdpmic.4 \
	berkwdt.4 bge.4 bgw.4 bio.4 bpe.4 bktr.4 bmtphy.4 bnx.4 bnxt.4 \
	boca.4 bpf.4 brgphy.4 bridge.4 brswphy.4 bse.4 bwfm.4 bwi.4 bytgpio.4 \
	cac.4 cad.4 cas.4 cardbus.4 carp.4 ccp.4 ccpmic.4 cd.4 cdce.4 \
	cdsdhc.4 cduart.4 cfxga.4 \
	ch.4 chvgpio.4 ciphy.4 ciss.4 clcs.4 clct.4 cmpci.4 \
	com.4 cue.4 cwfg.4 cy.4 cz.4 \
	dapmic.4 \
	dc.4 dcphy.4 ddb.4 de.4 diskmap.4 divert.4 drm.4 dsxrtc.4 dt.4 \
	dwctwo.4 dwdog.4 dwge.4 dwgpio.4 dwiic.4 dwmmc.4 dwmshc.4 dwpcie.4 \
	dwqe.4 dwxe.4 \
	eap.4 ec.4 eephy.4 ef.4 efi.4 eg.4 ehci.4 eisa.4 el.4 em.4 emc.4 gcu.4 \
	emu.4 enc.4 endrun.4 envy.4 eoip.4 ep.4 epic.4 \
	erspan.4 esa.4 escodec.4 \
	eso.4 ess.4 et.4 etherip.4 etphy.4 ex.4 exphy.4 exrtc.4 \
	fanpwr.4 fd.4 fdc.4 fec.4 fido.4 fins.4 fintek.4 fms.4 frame.4 \
	fusbtc.4 fuse.4 fxp.4 \
	gdt.4 gentbi.4 gem.4 gfrtc.4 gif.4 glenv.4 glkgpio.4 gpio.4 \
	gpiocharger.4 gpiodcf.4 \
	gpioiic.4 gpiokeys.4 gpioleds.4 gpioow.4 graphaudio.4 gre.4 gscsio.4 \
	hds.4 hiclock.4 hidwusb.4 hil.4 hilid.4 hilkbd.4 hilms.4 \
	hireset.4 hitemp.4 hme.4 hotplug.4 hsq.4 \
	hvn.4 hvs.4 hyperv.4 \
	iasuskbd.4 iatp.4 iavf.4 icc.4 ice.4 ichiic.4 ichwdt.4 \
	icmp.4 icmp6.4 icsphy.4 ietp.4 ifmedia.4 \
	igc.4 iha.4 ihidev.4 iic.4 iicmux.4 ikbd.4 ims.4 imt.4 imxanatop.4 \
	imxdog.4 imxesdhc.4 imxgpc.4 imxgpio.4 imxiic.4 imxpciephy.4 \
	imxpwm.4 imxrtc.4 imxspi.4 imxsrc.4 imxtmu.4 imxuart.4 \
	inet.4 inet6.4 inphy.4 intelpmc.4 \
	iophy.4 iosf.4 ip.4 ip6.4 ipcomp.4 ipgphy.4 \
	ipmi.4 ips.4 ipsec.4 ipw.4 isa.4 isagpio.4 isapnp.4 islrtc.4 \
	it.4 itherm.4 iwi.4 iwn.4 iwm.4 iwx.4 ix.4 ixv.4 ixgb.4 ixl.4 \
	jmb.4 jme.4 jmphy.4 \
	kate.4 kcov.4 km.4 ksmn.4 kstat.4 ksyms.4 kubsan.4 kue.4 \
	lc.4 lge.4 lii.4 \
	lisa.4 lm.4 lmenv.4 lmn.4 lmtemp.4 lo.4 lpt.4 lxtphy.4 luphy.4 \
	maestro.4 mainbus.4 malo.4 maxds.4 maxrtc.4 maxtmp.4 mbg.4 \
	mcprtc.4 mcx.4 midi.4 mii.4 mfi.4 mfii.4 mfokrtc.4 \
	mlphy.4 moscom.4 mos.4 mpe.4 mpath.4 mpi.4 mpii.4 \
	mpip.4 mpu.4 msk.4 mpw.4 msts.4 mtd.4 mtdphy.4 \
	mtintc.4 mtio.4 mtrng.4 mtw.4 mtxhci.4 mue.4 \
	multicast.4 mvclock.4 mvdog.4 mvgicp.4 mvgpio.4 mvicu.4 mviic.4 \
	mvkpcie.4 mvneta.4 mvpinctrl.4 mvpp.4 mvrng.4 mvrtc.4 mvspi.4 \
	mvtemp.4 mvsw.4 mvuart.4 myx.4 \
	ne.4 neo.4 nep.4 netintro.4 nfe.4 ngbe.4 nge.4 nmea.4 \
	nsclpcsio.4 nsgphy.4 nsphy.4 nsphyter.4 null.4 nviic.4 nvme.4 nvt.4 \
	oce.4 ociic.4 ohci.4 onewire.4 oosiop.4 openprom.4 options.4 \
	osiop.4 otus.4 owid.4 owctr.4 owsbm.4 owtemp.4 \
	pair.4 pcagpio.4 pcaled.4 pcamux.4 \
	pcdisplay.4 pchb.4 pchtemp.4 pci.4 pcib.4 \
	pcfadc.4 pcfiic.4 pcfrtc.4 pchgpio.4 pciide.4 pckbc.4 pckbd.4 \
	pcmcia.4 pcn.4 pcppi.4 pcscp.4 pcxrtc.4 pcyrtc.4 \
	pf.4 pflog.4 pflow.4 pfsync.4 \
	pgt.4 piixpm.4 pijuice.4 pinctrl.4 pipex.4 plgpio.4 plrtc.4 pluart.4 \
	pms.4 ppb.4 ppp.4 pppoe.4 pppx.4 psci.4 psp.4 pty.4 puc.4 pvbus.4 \
	pvclock.4 pwdog.4 pwmbl.4 pwmfan.4 pwmleds.4 pwmreg.4 \
	qccpu.4 qccpucp.4 qcdpc.4 qcdrm.4 qcdwusb.4 qcgpio.4 \
	qciic.4 qcpdc.4 qcpmic.4 \
	qcpmicgpio.4 qcpon.4 qcpwm.4 qcrng.4 qcrtc.4 qcspmi.4 \
	qla.4 qle.4 qlw.4 qsphy.4 qwx.4 \
	radio.4 ral.4 random.4 rdomain.4 rd.4 rdac.4 re.4 rdcphy.4 rgephy.4 \
	rge.4 ricohrtc.4 rkanxdp.4 rkclock.4 rkcomphy.4 \
	rkdrm.4 rkdwhdmi.4 rkdwusb.4 \
	rkemmcphy.4 rkgpio.4 rkgrf.4 rkiic.4 rkiis.4 rkiovd.4 \
	rkpcie.4 rkpciephy.4 rkpinctrl.4 rkpmic.4 rkpwm.4 \
	rkrng.4 rkspi.4 rktcphy.4 rktemp.4 rkusbphy.4 rkvop.4 \
	rl.4 rlphy.4 route.4 rport.4 rsu.4 rtsx.4 rum.4 run.4 rtw.4 rtwn.4 \
	safte.4 sbus.4 schsio.4 scmi.4 scsi.4 sd.4 \
	sdmmc.4 sdhc.4 se.4 sec.4 ses.4 sf.4 sili.4 \
	simpleamp.4 simpleaudio.4 simplefb.4 simplepanel.4 siop.4 sis.4 sk.4 \
	sm.4 smsc.4 sncodec.4 \
	softraid.4 spdmem.4 sdtemp.4 speaker.4 sppp.4 sqphy.4 \
	ssdfb.4 st.4 ste.4 stge.4 sti.4 stp.4 sv.4 sxiccmu.4 \
	sxidog.4 sximmc.4 sxipio.4 sxipwm.4 sxirintc.4 sxirsb.4 sxirtc.4 \
	sxisid.4 sxisyscon.4 sxitemp.4 sxitwi.4 sym.4 sypwr.4 syscon.4 \
	tascodec.4 tcic.4 tcp.4 tcpci.4 termios.4 tht.4 ti.4 tipd.4 \
	tipmic.4 titmp.4 tl.4 \
	tlphy.4 thmc.4 tpm.4 tpmr.4 tqphy.4 trm.4 trunk.4 tsl.4 tty.4 \
	tun.4 twe.4 \
	txp.4 txphy.4 uaudio.4 uaq.4 uark.4 uath.4 ubcmtp.4 uberry.4 ubsa.4 \
	ucc.4 ucom.4 uchcom.4 ucrcom.4 ucycom.4 ukspan.4 uslhcom.4 \
	udav.4 udcf.4 udl.4 udp.4 udsbr.4 ufshci.4 \
	uftdi.4 ugen.4 ugl.4 ugold.4 uguru.4 uhci.4 uhid.4 uhidev.4 uhidpp.4 \
	uipaq.4 ujoy.4 uk.4 ukbd.4 \
	ukphy.4 ulpt.4 umass.4 umb.4 umbg.4 umcs.4 umct.4 umidi.4 umodem.4 \
	ums.4 umsm.4 umstc.4 umt.4 unix.4 uonerng.4 uow.4 uoaklux.4 uoakrh.4 \
	uoakv.4 upd.4 upgt.4 upl.4 uplcom.4 ural.4 ure.4 url.4 urlphy.4 \
	urndis.4 urng.4 urtw.4 urtwn.4 usb.4 uscom.4 uslcom.4 usps.4 \
	uthum.4 uticom.4 utpms.4 utwitch.4 utrh.4 uts.4 utvfu.4 uvideo.4 \
	uvisor.4 uvscom.4 uwacom.4 uxrcom.4 \
	veb.4 vether.4 vga.4 vgafb.4 vge.4 \
	viapm.4 viasio.4 vic.4 video.4 vio.4 \
	vioblk.4 viocon.4 viogpu.4 viomb.4 viornd.4 vioscsi.4 virtio.4 vlan.4 \
	vmmci.4 vmt.4 vmwpvs.4 vmx.4 vnd.4 vr.4 \
	vscsi.4 vte.4 vxlan.4 \
	watchdog.4 wb.4 wbenv.4 wbng.4 wbsd.4 wbsio.4 wd.4 wdc.4 we.4 \
	wg.4 wi.4 witness.4 wpi.4 wscons.4 wsdisplay.4 wskbd.4 wsmouse.4 \
	wsmux.4 \
	xbf.4 xe.4 xen.4 xf86.4 xge.4 xhci.4 xl.4 xmphy.4 xnf.4 xspd.4 \
	yds.4 ytphy.4 zero.4 zyd.4

SUBDIR=	man4.alpha man4.amd64 man4.arm64 man4.armv7 \
	man4.hppa man4.i386 man4.landisk man4.loongson man4.luna88k \
	man4.macppc man4.octeon man4.powerpc64 man4.riscv64 man4.sparc64

.include <bsd.prog.mk>
