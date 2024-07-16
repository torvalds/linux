cat <<EOF
static __always_inline ${ret}
arch_${atomic}_${pfx}inc${sfx}${order}(${atomic}_t *v)
{
	${retstmt}arch_${atomic}_${pfx}add${sfx}${order}(1, v);
}
EOF
