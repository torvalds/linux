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
	{(unsigned long)GFP_HIGHUSER_MOVABLE,	"GFP_HIGHUSER_MOVABLE"}, \
	{(unsigned long)GFP_HIGHUSER,		"GFP_HIGHUSER"},	\
	{(unsigned long)GFP_USER,		"GFP_USER"},		\
	{(unsigned long)GFP_TEMPORARY,		"GFP_TEMPORARY"},	\
	{(unsigned long)GFP_KERNEL,		"GFP_KERNEL"},		\
	{(unsigned long)GFP_NOFS,		"GFP_NOFS"},		\
	{(unsigned long)GFP_ATOMIC,		"GFP_ATOMIC"},		\
	{(unsigned long)GFP_NOIO,		"GFP_NOIO"},		\
	{(unsigned long)__GFP_HIGH,		"GFP_HIGH"},		\
	{(unsigned long)__GFP_WAIT,		"GFP_WAIT"},		\
	{(unsigned long)__GFP_IO,		"GFP_IO"},		\
	{(unsigned long)__GFP_COLD,		"GFP_COLD"},		\
	{(unsigned long)__GFP_NOWARN,		"GFP_NOWARN"},		\
	{(unsigned long)__GFP_REPEAT,		"GFP_REPEAT"},		\
	{(unsigned long)__GFP_NOFAIL,		"GFP_NOFAIL"},		\
	{(unsigned long)__GFP_NORETRY,		"GFP_NORETRY"},		\
	{(unsigned long)__GFP_COMP,		"GFP_COMP"},		\
	{(unsigned long)__GFP_ZERO,		"GFP_ZERO"},		\
	{(unsigned long)__GFP_NOMEMALLOC,	"GFP_NOMEMALLOC"},	\
	{(unsigned long)__GFP_HARDWALL,		"GFP_HARDWALL"},	\
	{(unsigned long)__GFP_THISNODE,		"GFP_THISNODE"},	\
	{(unsigned long)__GFP_RECLAIMABLE,	"GFP_RECLAIMABLE"},	\
	{(unsigned long)__GFP_MOVABLE,		"GFP_MOVABLE"},		\
	{(unsigned long)__GFP_NOTRACK,		"GFP_NOTRACK"},		\
	{(unsigned long)__GFP_NO_KSWAPD,	"GFP_NO_KSWAPD"},	\
	{(unsigned long)__GFP_OTHER_NODE,	"GFP_OTHER_NODE"}	\
	) : "GFP_NOWAIT"

