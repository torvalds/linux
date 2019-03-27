#
# $FreeBSD$
#

document netstat-anr
Print routing tables as 'netstat -anr' does.
end

set $debug = 0

set $AF_INET = 2
set $AF_LINK = 18

set $RNF_ROOT = 2
set $RNF_ACTIVE = 4

set $RTF_UP		= 0x1
set $RTF_GATEWAY	= 0x2
set $RTF_HOST		= 0x4
set $RTF_STATIC		= 0x800

#
# XXX: alas, we can't script "show endian"
#
if (machine[0] == 'a' && machine[1] == 'm' && machine[2] == 'd') || \
   (machine[0] == 'i' && machine[1] == '3' && machine[2] == '8')
	set $byteswap = 1
else
	set $byteswap = 0
end

define routename
	if ($byteswap)
		printf "%u.%u.%u.%u", \
		    $arg0 & 0xff, ($arg0 >> 8) & 0xff, \
		    ($arg0 >> 16) & 0xff, ($arg0 >> 24) & 0xff
	else
		printf "%u.%u.%u.%u", \
		    ($arg0 >> 24) & 0xff, ($arg0 >> 16) & 0xff, \
		    ($arg0 >> 8) & 0xff, $arg0 & 0xff
	end
end

define domask
	set $i = 0
	set $b = 0
	while $b < 32
		if ($arg0 & (1 << $b))
			set $i = $i + 1
		end
		set $b = $b + 1
	end
	printf "/%d", $i
end

define p_flags
	if ($arg0 & $RTF_UP)
		printf "U"
	end
	if ($arg0 & $RTF_GATEWAY)
		printf "G"
	end
	if ($arg0 & $RTF_HOST)
		printf "H"
	end
	if ($arg0 & $RTF_STATIC)
		printf "S"
	end
end

define p_sockaddr
	set $sa = (struct sockaddr *)$arg0
	set $flags = $arg2
	if ($sa->sa_family == $AF_INET)
		set $sin = (struct sockaddr_in *)$arg0
		set $mask = (struct sockaddr_in *)$arg1
		if ($flags & $RTF_HOST)
			routename $sin->sin_addr.s_addr
		else
			routename $sin->sin_addr.s_addr
			if ($mask != 0)
				domask $mask->sin_addr.s_addr
			else
				domask 0
			end
		end
	end
	if ($sa->sa_family == $AF_LINK)
		set $sdl = (struct sockaddr_dl *)$arg0
		if ($sdl->sdl_nlen == 0 && $sdl->sdl_alen == 0 && \
		    $sdl->sdl_slen == 0)
			printf "link#%d", $sdl->sdl_index
		end
	end
end

define p_rtentry
	set $rte = (struct rtentry *)$arg0
	set $rn = (struct radix_node *)$arg0
	set $sa = ((struct sockaddr **)($rn->rn_u.rn_leaf.rn_Key))
	set $sam = ((struct sockaddr **)($rn->rn_u.rn_leaf.rn_Mask))
	set $gw = $rte->rt_gateway

	p_sockaddr $sa $sam $rte->rt_flags
	printf "\t"
	p_sockaddr $gw 0 $RTF_HOST
	printf "\t"
	p_flags $rte->rt_flags
	printf "\t"
	if ($rte->rt_ifp != 0)
		printf "%s", $rte->rt_ifp->if_xname
	end
	printf "\n"
end

define p_rtree
	set $rn_$arg0 = (struct radix_node *)$arg1
	set $left_$arg0 = $arg0 + 1
	set $right_$arg0 = $arg0 + 2
	set $duped_$arg0 = $arg0 + 3

	if ($rn_$arg0->rn_bit < 0 || ($rn_$arg0->rn_flags & $RNF_ACTIVE) == 0)
		if ($debug == 1)
			printf "print "
			p $rn_$arg0
		end
		if (($rn_$arg0->rn_flags & ($RNF_ACTIVE | $RNF_ROOT)) == \
		    $RNF_ACTIVE)
			p_rtentry $rn_$arg0
		end
		if (($rn_$arg0->rn_flags & $RNF_ACTIVE) != 0 && \
		    $rn_$arg0->rn_u.rn_leaf.rn_Dupedkey != 0)
			if ($debug == 1)
				printf "duped "
				p $rn_$arg0
			end
			p_rtree $duped_$arg0 $rn_$arg0->rn_u.rn_leaf.rn_Dupedkey
		end
	else
		if ($rn_$arg0->rn_u.rn_node.rn_R != 0)
			if ($debug == 1)
				printf "right "
				p $rn_$arg0
			end
			p_rtree $right_$arg0 $rn_$arg0->rn_u.rn_node.rn_R
		end
		if ($rn_$arg0->rn_u.rn_node.rn_L != 0)
			if ($debug == 1)
				printf "left "
				p $rn_$arg0
			end
			p_rtree $left_$arg0 $rn_$arg0->rn_u.rn_node.rn_L
		end
	end
end

define netstat-anr
	printf "Routing tables\n\nInternet:\n"
	set $af = $AF_INET
	set $rt = (struct radix_node_head **)rt_tables + $af
	printf "Destination\tGateway\tFlags\tNetif\n"
	p_rtree 0 $rt->rnh_treetop
end
