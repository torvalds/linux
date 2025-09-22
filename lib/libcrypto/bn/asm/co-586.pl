#!/usr/local/bin/perl

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
push(@INC,"${dir}","${dir}../../perlasm");
require "x86asm.pl";

&asm_init($ARGV[0],$0);

&bn_mul_comba("bn_mul_comba8",8);
&bn_mul_comba("bn_mul_comba4",4);
&bn_sqr_comba("bn_sqr_comba8",8);
&bn_sqr_comba("bn_sqr_comba4",4);

&asm_finish();

sub mul_add_c
	{
	local($a,$ai,$b,$bi,$c0,$c1,$c2,$pos,$i,$na,$nb)=@_;

	# pos == -1 if eax and edx are pre-loaded, 0 to load from next
	# words, and 1 if load return value

	&comment("mul a[$ai]*b[$bi]");

	# "eax" and "edx" will always be pre-loaded.
	# &mov("eax",&DWP($ai*4,$a,"",0)) ;
	# &mov("edx",&DWP($bi*4,$b,"",0));

	&mul("edx");
	&add($c0,"eax");
	 &mov("eax",&DWP(($na)*4,$a,"",0)) if $pos == 0;	# load next a
	 &mov("eax",&wparam(0)) if $pos > 0;			# load r[]
	 ###
	&adc($c1,"edx");
	 &mov("edx",&DWP(($nb)*4,$b,"",0)) if $pos == 0;	# load next b
	 &mov("edx",&DWP(($nb)*4,$b,"",0)) if $pos == 1;	# load next b
	 ###
	&adc($c2,0);
	 # is pos > 1, it means it is the last loop 
	 &mov(&DWP($i*4,"eax","",0),$c0) if $pos > 0;		# save r[];
	&mov("eax",&DWP(($na)*4,$a,"",0)) if $pos == 1;		# load next a
	}

sub sqr_add_c
	{
	local($r,$a,$ai,$bi,$c0,$c1,$c2,$pos,$i,$na,$nb)=@_;

	# pos == -1 if eax and edx are pre-loaded, 0 to load from next
	# words, and 1 if load return value

	&comment("sqr a[$ai]*a[$bi]");

	# "eax" and "edx" will always be pre-loaded.
	# &mov("eax",&DWP($ai*4,$a,"",0)) ;
	# &mov("edx",&DWP($bi*4,$b,"",0));

	if ($ai == $bi)
		{ &mul("eax");}
	else
		{ &mul("edx");}
	&add($c0,"eax");
	 &mov("eax",&DWP(($na)*4,$a,"",0)) if $pos == 0;	# load next a
	 ###
	&adc($c1,"edx");
	 &mov("edx",&DWP(($nb)*4,$a,"",0)) if ($pos == 1) && ($na != $nb);
	 ###
	&adc($c2,0);
	 # is pos > 1, it means it is the last loop 
	 &mov(&DWP($i*4,$r,"",0),$c0) if $pos > 0;		# save r[];
	&mov("eax",&DWP(($na)*4,$a,"",0)) if $pos == 1;		# load next b
	}

sub sqr_add_c2
	{
	local($r,$a,$ai,$bi,$c0,$c1,$c2,$pos,$i,$na,$nb)=@_;

	# pos == -1 if eax and edx are pre-loaded, 0 to load from next
	# words, and 1 if load return value

	&comment("sqr a[$ai]*a[$bi]");

	# "eax" and "edx" will always be pre-loaded.
	# &mov("eax",&DWP($ai*4,$a,"",0)) ;
	# &mov("edx",&DWP($bi*4,$a,"",0));

	if ($ai == $bi)
		{ &mul("eax");}
	else
		{ &mul("edx");}
	&add("eax","eax");
	 ###
	&adc("edx","edx");
	 ###
	&adc($c2,0);
	 &add($c0,"eax");
	&adc($c1,"edx");
	 &mov("eax",&DWP(($na)*4,$a,"",0)) if $pos == 0;	# load next a
	 &mov("eax",&DWP(($na)*4,$a,"",0)) if $pos == 1;	# load next b
	&adc($c2,0);
	&mov(&DWP($i*4,$r,"",0),$c0) if $pos > 0;		# save r[];
	 &mov("edx",&DWP(($nb)*4,$a,"",0)) if ($pos <= 1) && ($na != $nb);
	 ###
	}

sub bn_mul_comba
	{
	local($name,$num)=@_;
	local($a,$b,$c0,$c1,$c2);
	local($i,$as,$ae,$bs,$be,$ai,$bi);
	local($tot,$end);

	&function_begin_B($name,"");

	$c0="ebx";
	$c1="ecx";
	$c2="ebp";
	$a="esi";
	$b="edi";
	
	$as=0;
	$ae=0;
	$bs=0;
	$be=0;
	$tot=$num+$num-1;

	&push("esi");
	 &mov($a,&wparam(1));
	&push("edi");
	 &mov($b,&wparam(2));
	&push("ebp");
	 &push("ebx");

	&xor($c0,$c0);
	 &mov("eax",&DWP(0,$a,"",0));	# load the first word 
	&xor($c1,$c1);
	 &mov("edx",&DWP(0,$b,"",0));	# load the first second 

	for ($i=0; $i<$tot; $i++)
		{
		$ai=$as;
		$bi=$bs;
		$end=$be+1;

		&comment("################## Calculate word $i"); 

		for ($j=$bs; $j<$end; $j++)
			{
			&xor($c2,$c2) if ($j == $bs);
			if (($j+1) == $end)
				{
				$v=1;
				$v=2 if (($i+1) == $tot);
				}
			else
				{ $v=0; }
			if (($j+1) != $end)
				{
				$na=($ai-1);
				$nb=($bi+1);
				}
			else
				{
				$na=$as+($i < ($num-1));
				$nb=$bs+($i >= ($num-1));
				}
#printf STDERR "[$ai,$bi] -> [$na,$nb]\n";
			&mul_add_c($a,$ai,$b,$bi,$c0,$c1,$c2,$v,$i,$na,$nb);
			if ($v)
				{
				&comment("saved r[$i]");
				# &mov("eax",&wparam(0));
				# &mov(&DWP($i*4,"eax","",0),$c0);
				($c0,$c1,$c2)=($c1,$c2,$c0);
				}
			$ai--;
			$bi++;
			}
		$as++ if ($i < ($num-1));
		$ae++ if ($i >= ($num-1));

		$bs++ if ($i >= ($num-1));
		$be++ if ($i < ($num-1));
		}
	&comment("save r[$i]");
	# &mov("eax",&wparam(0));
	&mov(&DWP($i*4,"eax","",0),$c0);

	&pop("ebx");
	&pop("ebp");
	&pop("edi");
	&pop("esi");
	&ret();
	&function_end_B($name);
	}

sub bn_sqr_comba
	{
	local($name,$num)=@_;
	local($r,$a,$c0,$c1,$c2)=@_;
	local($i,$as,$ae,$bs,$be,$ai,$bi);
	local($b,$tot,$end,$half);

	&function_begin_B($name,"");

	$c0="ebx";
	$c1="ecx";
	$c2="ebp";
	$a="esi";
	$r="edi";

	&push("esi");
	 &push("edi");
	&push("ebp");
	 &push("ebx");
	&mov($r,&wparam(0));
	 &mov($a,&wparam(1));
	&xor($c0,$c0);
	 &xor($c1,$c1);
	&mov("eax",&DWP(0,$a,"",0)); # load the first word

	$as=0;
	$ae=0;
	$bs=0;
	$be=0;
	$tot=$num+$num-1;

	for ($i=0; $i<$tot; $i++)
		{
		$ai=$as;
		$bi=$bs;
		$end=$be+1;

		&comment("############### Calculate word $i");
		for ($j=$bs; $j<$end; $j++)
			{
			&xor($c2,$c2) if ($j == $bs);
			if (($ai-1) < ($bi+1))
				{
				$v=1;
				$v=2 if ($i+1) == $tot;
				}
			else
				{ $v=0; }
			if (!$v)
				{
				$na=$ai-1;
				$nb=$bi+1;
				}
			else
				{
				$na=$as+($i < ($num-1));
				$nb=$bs+($i >= ($num-1));
				}
			if ($ai == $bi)
				{
				&sqr_add_c($r,$a,$ai,$bi,
					$c0,$c1,$c2,$v,$i,$na,$nb);
				}
			else
				{
				&sqr_add_c2($r,$a,$ai,$bi,
					$c0,$c1,$c2,$v,$i,$na,$nb);
				}
			if ($v)
				{
				&comment("saved r[$i]");
				#&mov(&DWP($i*4,$r,"",0),$c0);
				($c0,$c1,$c2)=($c1,$c2,$c0);
				last;
				}
			$ai--;
			$bi++;
			}
		$as++ if ($i < ($num-1));
		$ae++ if ($i >= ($num-1));

		$bs++ if ($i >= ($num-1));
		$be++ if ($i < ($num-1));
		}
	&mov(&DWP($i*4,$r,"",0),$c0);
	&pop("ebx");
	&pop("ebp");
	&pop("edi");
	&pop("esi");
	&ret();
	&function_end_B($name);
	}
