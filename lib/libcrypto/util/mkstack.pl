#!/usr/local/bin/perl -w

# This is a utility that searches out "DECLARE_STACK_OF()"
# declarations in .h and .c files, and updates/creates/replaces
# the corresponding macro declarations in crypto/stack/safestack.h.
# As it's not generally possible to have macros that generate macros,
# we need to control this from the "outside", here in this script.
#
# Geoff Thorpe, June, 2000 (with massive Perl-hacking
#                           help from Steve Robb)

my $safestack = "crypto/stack/safestack";

my $do_write;
while (@ARGV) {
	my $arg = $ARGV[0];
	if($arg eq "-write") {
		$do_write = 1;
	}
	shift @ARGV;
}


@source = (<crypto/*.[ch]>, <crypto/*/*.[ch]>, <ssl/*.[ch]>, <apps/*.[ch]>);
foreach $file (@source) {
	next if -l $file;

	# Open the .c/.h file for reading
	open(IN, "< $file") || die "Can't open $file for reading: $!";

	while(<IN>) {
		if (/^DECLARE_STACK_OF\(([^)]+)\)/) {
			push @stacklst, $1;
		}
	        if (/^DECLARE_SPECIAL_STACK_OF\(([^,\s]+)\s*,\s*([^>\s]+)\)/) {
		    	push @sstacklst, [$1, $2];
		}
		if (/^DECLARE_ASN1_SET_OF\(([^)]+)\)/) {
			push @asn1setlst, $1;
		}
		if (/^DECLARE_PKCS12_STACK_OF\(([^)]+)\)/) {
			push @p12stklst, $1;
		}
		if (/^DECLARE_LHASH_OF\(([^)]+)\)/) {
			push @lhashlst, $1;
		}
	}
	close(IN);
}



my $old_stackfile = "";
my $new_stackfile = "";
my $inside_block = 0;
my $type_thing;

open(IN, "< $safestack.h") || die "Can't open input file: $!";
while(<IN>) {
	$old_stackfile .= $_;

	if (m|^/\* This block of defines is updated by util/mkstack.pl, please do not touch! \*/|) {
		$inside_block = 1;
	}
	if (m|^/\* End of util/mkstack.pl block, you may now edit :-\) \*/|) {
		$inside_block = 0;
	} elsif ($inside_block == 0) {
		$new_stackfile .= $_;
	}
	next if($inside_block != 1);
	$new_stackfile .= "/* This block of defines is updated by util/mkstack.pl, please do not touch! */";
		
	foreach $type_thing (sort @stacklst) {
		$new_stackfile .= <<EOF;

#define sk_${type_thing}_new(cmp) SKM_sk_new($type_thing, (cmp))
#define sk_${type_thing}_new_null() SKM_sk_new_null($type_thing)
#define sk_${type_thing}_free(st) SKM_sk_free($type_thing, (st))
#define sk_${type_thing}_num(st) SKM_sk_num($type_thing, (st))
#define sk_${type_thing}_value(st, i) SKM_sk_value($type_thing, (st), (i))
#define sk_${type_thing}_set(st, i, val) SKM_sk_set($type_thing, (st), (i), (val))
#define sk_${type_thing}_zero(st) SKM_sk_zero($type_thing, (st))
#define sk_${type_thing}_push(st, val) SKM_sk_push($type_thing, (st), (val))
#define sk_${type_thing}_unshift(st, val) SKM_sk_unshift($type_thing, (st), (val))
#define sk_${type_thing}_find(st, val) SKM_sk_find($type_thing, (st), (val))
#define sk_${type_thing}_delete(st, i) SKM_sk_delete($type_thing, (st), (i))
#define sk_${type_thing}_delete_ptr(st, ptr) SKM_sk_delete_ptr($type_thing, (st), (ptr))
#define sk_${type_thing}_insert(st, val, i) SKM_sk_insert($type_thing, (st), (val), (i))
#define sk_${type_thing}_set_cmp_func(st, cmp) SKM_sk_set_cmp_func($type_thing, (st), (cmp))
#define sk_${type_thing}_dup(st) SKM_sk_dup($type_thing, st)
#define sk_${type_thing}_pop_free(st, free_func) SKM_sk_pop_free($type_thing, (st), (free_func))
#define sk_${type_thing}_shift(st) SKM_sk_shift($type_thing, (st))
#define sk_${type_thing}_pop(st) SKM_sk_pop($type_thing, (st))
#define sk_${type_thing}_sort(st) SKM_sk_sort($type_thing, (st))
#define sk_${type_thing}_is_sorted(st) SKM_sk_is_sorted($type_thing, (st))
EOF
	}

	foreach $type_thing (sort @sstacklst) {
	    my $t1 = $type_thing->[0];
	    my $t2 = $type_thing->[1];
	    $new_stackfile .= <<EOF;

#define sk_${t1}_new(cmp) ((STACK_OF($t1) *)sk_new(CHECKED_SK_CMP_FUNC($t2, cmp)))
#define sk_${t1}_new_null() ((STACK_OF($t1) *)sk_new_null())
#define sk_${t1}_push(st, val) sk_push(CHECKED_STACK_OF($t1, st), CHECKED_PTR_OF($t2, val))
#define sk_${t1}_find(st, val) sk_find(CHECKED_STACK_OF($t1, st), CHECKED_PTR_OF($t2, val))
#define sk_${t1}_value(st, i) (($t1)sk_value(CHECKED_STACK_OF($t1, st), i))
#define sk_${t1}_num(st) SKM_sk_num($t1, st)
#define sk_${t1}_pop_free(st, free_func) sk_pop_free(CHECKED_STACK_OF($t1, st), CHECKED_SK_FREE_FUNC2($t1, free_func))
#define sk_${t1}_insert(st, val, i) sk_insert(CHECKED_STACK_OF($t1, st), CHECKED_PTR_OF($t2, val), i)
#define sk_${t1}_free(st) SKM_sk_free(${t1}, st)
#define sk_${t1}_set(st, i, val) sk_set(CHECKED_STACK_OF($t1, st), i, CHECKED_PTR_OF($t2, val))
#define sk_${t1}_zero(st) SKM_sk_zero($t1, (st))
#define sk_${t1}_unshift(st, val) sk_unshift(CHECKED_STACK_OF($t1, st), CHECKED_PTR_OF($t2, val))
#define sk_${t1}_delete(st, i) SKM_sk_delete($t1, (st), (i))
#define sk_${t1}_delete_ptr(st, ptr) ($t1 *)sk_delete_ptr(CHECKED_STACK_OF($t1, st), CHECKED_PTR_OF($t2, ptr))
#define sk_${t1}_set_cmp_func(st, cmp)  \\
	((int (*)(const $t2 * const *,const $t2 * const *)) \\
	sk_set_cmp_func(CHECKED_STACK_OF($t1, st), CHECKED_SK_CMP_FUNC($t2, cmp)))
#define sk_${t1}_dup(st) SKM_sk_dup($t1, st)
#define sk_${t1}_shift(st) SKM_sk_shift($t1, (st))
#define sk_${t1}_pop(st) ($t2 *)sk_pop(CHECKED_STACK_OF($t1, st))
#define sk_${t1}_sort(st) SKM_sk_sort($t1, (st))
#define sk_${t1}_is_sorted(st) SKM_sk_is_sorted($t1, (st))

EOF
	}

	foreach $type_thing (sort @p12stklst) {
		$new_stackfile .= <<EOF;

#define PKCS12_decrypt_d2i_${type_thing}(algor, d2i_func, free_func, pass, passlen, oct, seq) \\
	SKM_PKCS12_decrypt_d2i($type_thing, (algor), (d2i_func), (free_func), (pass), (passlen), (oct), (seq))
EOF
	}

	foreach $type_thing (sort @lhashlst) {
		my $lc_tt = lc $type_thing;
		$new_stackfile .= <<EOF;

#define lh_${type_thing}_new() LHM_lh_new(${type_thing},${lc_tt})
#define lh_${type_thing}_insert(lh,inst) LHM_lh_insert(${type_thing},lh,inst)
#define lh_${type_thing}_retrieve(lh,inst) LHM_lh_retrieve(${type_thing},lh,inst)
#define lh_${type_thing}_delete(lh,inst) LHM_lh_delete(${type_thing},lh,inst)
#define lh_${type_thing}_doall(lh,fn) LHM_lh_doall(${type_thing},lh,fn)
#define lh_${type_thing}_doall_arg(lh,fn,arg_type,arg) \\
  LHM_lh_doall_arg(${type_thing},lh,fn,arg_type,arg)
#define lh_${type_thing}_error(lh) LHM_lh_error(${type_thing},lh)
#define lh_${type_thing}_num_items(lh) LHM_lh_num_items(${type_thing},lh)
#define lh_${type_thing}_down_load(lh) LHM_lh_down_load(${type_thing},lh)
#define lh_${type_thing}_free(lh) LHM_lh_free(${type_thing},lh)
EOF
	}

	$new_stackfile .= "/* End of util/mkstack.pl block, you may now edit :-) */\n";
	$inside_block = 2;
}


if ($new_stackfile eq $old_stackfile) {
	print "No changes to $safestack.h.\n";
	exit 0; # avoid unnecessary rebuild
}

if ($do_write) {
	print "Writing new $safestack.h.\n";
	open OUT, ">$safestack.h" || die "Can't open output file";
	print OUT $new_stackfile;
	close OUT;
}
