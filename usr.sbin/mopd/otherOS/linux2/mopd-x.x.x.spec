# $OpenBSD: mopd-x.x.x.spec,v 1.2 2022/12/28 21:30:17 jmc Exp $
Summary: MOP (Maintenance Operations Protocol) loader daemon
Name: mopd
Version: 2.5.4
Release: 2
Copyright: BSD
Group: Networking
Source: ftp://ftp.stacken.kth.se/pub/NetBSD/mopd/mopd-2.5.4.tar.gz
BuildRoot: /var/tmp/mopd-2.5.4
Packager: Mats O Jansson <moj@stacken.kth.se>

%description
Mopd services MOP Load requests on the Ethernet connected to interface or
all interfaces if a `-a' is given.  In a load request received by mopd a
filename can be given. This is the normal case for e.g. terminal servers.
If a filename isn't given mopd must know what image to load.

Upon receiving a request, mopd checks if the requested file exists in
/tftpboot/mop, the filename is normally uppercase and with an extension of
.SYS. If the filename isn't given, the ethernet address of the target is
used as filename, e.g.  08002b09f4de.SYS and it might be a soft link to
another file.

Mopd supports two kinds of files. The first type that is check is if the
file is in a.out(5) format. If not, a couple of Digital's formats are
checked.

%prep
%setup -q -c mopd-2.5.4

%build
(cd otherOS && make)

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p -m 755 $RPM_BUILD_ROOT/etc/rc.d/init.d
mkdir -p -m 755 $RPM_BUILD_ROOT/usr/man/man1
mkdir -p -m 755 $RPM_BUILD_ROOT/usr/man/man8
mkdir -p -m 755 $RPM_BUILD_ROOT/usr/sbin
mkdir -p -m 755 $RPM_BUILD_ROOT/tftpboot/mop
(cd otherOS && make INSTALL="$RPM_BUILD_ROOT" install)

%clean
cd ..
rm -rf mopd-2.5.4
rm -rf $RPM_BUILD_ROOT

%post
/sbin/chkconfig --add mopd

%postun
if [ $1 = 0 ]; then
    /sbin/chkconfig --del mopd
fi

%files
%attr(755, root, root) /tftpboot/mop
%attr(755, root, root) /usr/sbin/mopchk
%attr(644, root, root) /usr/man/man1/mopchk.1
%attr(755, root, root) /usr/sbin/mopd
%attr(644, root, root) /usr/man/man8/mopd.8
%attr(755, root, root) /usr/sbin/mopprobe
%attr(644, root, root) /usr/man/man1/mopprobe.1
%attr(755, root, root) /usr/sbin/moptrace
%attr(644, root, root) /usr/man/man1/moptrace.1
%config %attr(755, root, root) /etc/rc.d/init.d/mopd

%changelog
* Fri Mar 26 1998 Mats O Jansson <moj@stacken.kth.se>
- incorparated lots of ideas from <xenophon@irtnog.org> who had written
  the mopd-linux-2.5.3 package.

* Wed Mar 24 1998 Mats O Jansson <moj@stacken.kth.se>
- initial build
