/*
 * The order of these masks is important. Matching masks will be seen
 * first and the left over flags will end up showing by themselves.
 *
 * For example, if we have GFP_KERNEL before GFP_USER we wil get:
 *
 *  GFP_KERNEL|GFP_HARDWALL
 *
 * Thus most bits set go first.
 */
#define show_gfp_flags(flags)						\
	(flags) ? __print_flags(flags, "|",				\
	{(unsigned long)GFP_TRANSHUGE,		"GFP_TRANSHUGE"},	\
	{(unsigned long)GFP_HIGHUSER_MOVABLE,	"GFP_HIGHUSER_MOVABLE"},\
	{(unsigned long)GFP_HIGHUSER,		"GFP_HIGHUSER"},	\
	{(unsigned long)GFP_USER,		"GFP_USER"},		\
	{(unsigned long)GFP_TEMPORARY,		"GFP_TEMPORARY"},	\
	{(unsigned long)GFP_KERNEL_ACCOUNT,	"GFP_KERNEL_ACCOUNT"},	\
	{(unsigned long)GFP_KERNEL,		"GFP_KERNEL"},		\
	{(unsigned long)GFP_NOFS,		"GFP_NOFS"},		\
	{(unsigned long)GFP_ATOMIC,		"GFP_ATOMIC"},		\
	{(unsigned long)GFP_NOIO,		"GFP_NOIO"},		\
	{(unsigned long)GFP_NOWAIT,		"GFP_NOWAIT"},		\
	{(unsigned long)GFP_DMA,		"GFP_DMA"},		\
	{(unsigned long)__GFP_HIGHMEM,		"__GFP_HIGHMEM"},	\
	{(unsigned long)GFP_DMA32,		"GFP_DMA32"},		\
	{(unsigned long)__GFP_HIGH,		"__GFP_HIGH"},		\
	{(unsigned long)__GFP_ATOMIC,		"__GFP_ATOMIC"},	\
	{(unsigned long)__GFP_IO,		"__GFP_IO"},		\
	{(unsigned long)__GFP_FS,		"__GFP_FS"},		\
	{(unsigned long)__GFP_COLD,		"__GFP_COLD"},		\
	{(unsigned long)__GFP_NOWARN,		"__GFP_NOWARN"},	\
	{(unsigned long)__GFP_REPEAT,		"__GFP_REPEAT"},	\
	{(unsigned long)__GFP_NOFAIL,		"__GFP_NOFAIL"},	\
	{(unsigned long)__GFP_NORETRY,		"__GFP_NORETRY"},	\
	{(unsigned long)__GFP_COMP,		"__GFP_COMP"},		\
	{(unsigned long)__GFP_ZERO,		"__GFP_ZERO"},		\
	{(unsigned long)__GFP_NOMEMALLOC,	"__GFP_NOMEMALLOC"},	\
	{(unsigned long)__GFP_MEMALLOC,		"__GFP_MEMALLOC"},	\
	{(unsigned long)__GFP_HARDWALL,		"__GFP_HARDWALL"},	\
	{(unsigned long)__GFP_THISNODE,		"__GFP_THISNODE"},	\
	{(unsigned long)__GFP_RECLAIMABLE,	"__GFP_RECLAIMABLE"},	\
	{(unsigned long)__GFP_MOVABLE,		"__GFP_MOVABLE"},	\
	{(unsigned long)__GFP_ACCOUNT,		"__GFP_ACCOUNT"},	\
	{(unsigned long)__GFP_NOTRACK,		"__GFP_NOTRACK"},	\
	{(unsigned long)__GFP_WRITE,		"__GFP_WRITE"},		\
	{(unsigned long)__GFP_RECLAIM,		"__GFP_RECLAIM"},	\
	{(unsigned long)__GFP_DIRECT_RECLAIM,	"__GFP_DIRECT_RECLAIM"},\
	{(unsigned long)__GFP_KSWAPD_RECLAIM,	"__GFP_KSWAPD_RECLAIM"},\
	{(unsigned long)__GFP_OTHER_NODE,	"__GFP_OTHER_NODE"}	\
	) : "none"

