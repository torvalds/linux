#!/usr/bin/perl
#
# $FreeBSD$
#

if ($#ARGV != 0) {
	print "runit.pl kernelname\n";
	exit(-1);
}

$tcpp_dir = "/rwatson/svn/base/head/tools/tools/netrate/tcpp";

($sec,$min,$hour,$mday,$mon,$year,$wday,$yday,$isdst) = localtime(time);
$mon++;
$year += 1900;
$date = sprintf("%04d%02d%02d", $year, $mon, $mday);

$kernel = $ARGV[0];
$outfile = $date."_".$kernel.".csv";
unlink($outfile);
open(OUTFILE, ">".$outfile) || die $outfile;
print OUTFILE "# $kernel $date\n";
print OUTFILE "# hydra1: ".`ssh root\@hydra1 uname -a`."\n";
print OUTFILE "# hydra2: ".`ssh root\@hydra2 uname -a`."\n";
print OUTFILE "#\n";
print OUTFILE "kernel,tso,lro,mtu,cores,trial,";
print OUTFILE "bytes,seconds,conns,bandwidth,user,nice,sys,intr,idle\n";
close(OUTFILE);

system("ssh root\@hydra1 killall tcpp");
system("ssh root\@hydra2 killall tcpp");
sleep(1);
system("ssh root\@hydra2 ${tcpp_dir}/tcpp -s -p 8&");
sleep(1);

sub test {
	my ($kernel, $tso, $lro, $mtu) = @_;

	$prefix = "$kernel,$tso,$lro,$mtu";
	print "Configuring $prefix\n";

	system("ssh root\@hydra1 ifconfig cxgb0 $tso $lro mtu $mtu");

	system("ssh root\@hydra2 ifconfig cxgb0 $tso $lro mtu $mtu");

	print "Running $prefix\n";
	system("ssh root\@hydra1 '(cd $tcpp_dir ; csh parallelism.csh ".
	    "$outfile $prefix)'");
}

# Frobbing MTU requires resetting the host cache, which we don't do,
# so don't frob MTU.
@mtu_options = ("1500");
@tso_options = ("tso", "-tso");
@lro_options = ("lro", "-lro");

foreach $mtu (@mtu_options) {
	foreach $tso (@tso_options) {
		foreach $lro (@lro_options) {
			sleep(5);
			test($kernel, $tso, $lro, $mtu);
		}
	}
}
