/* Public Domain */


/*
 * Clocks Signals
 */

/* A10/A20 */

#define A10_CLK_HOSC		1
#define A10_CLK_PLL_CORE	2
#define A10_CLK_PLL_PERIPH_BASE	14
#define A10_CLK_PLL_PERIPH	15

#define A10_CLK_CPU		20
#define A10_CLK_AXI		21
#define A10_CLK_AHB		23
#define A10_CLK_APB1		25

#define A10_CLK_AHB_EHCI0	27
#define A10_CLK_AHB_OHCI0	28
#define A10_CLK_AHB_EHCI1	29
#define A10_CLK_AHB_OHCI1	30
#define A10_CLK_AHB_MMC0	34
#define A10_CLK_AHB_MMC1	35
#define A10_CLK_AHB_MMC2	36
#define A10_CLK_AHB_MMC3	37
#define A10_CLK_AHB_EMAC	42
#define A10_CLK_AHB_SATA	49
#define A10_CLK_AHB_GMAC	66
#define A10_CLK_APB0_PIO	74
#define A10_CLK_APB1_I2C0	79
#define A10_CLK_APB1_I2C1	80
#define A10_CLK_APB1_I2C2	81
#define A10_CLK_APB1_I2C3	82
#define A10_CLK_APB1_I2C4	87
#define A10_CLK_APB1_UART0	88
#define A10_CLK_APB1_UART1	89
#define A10_CLK_APB1_UART2	90
#define A10_CLK_APB1_UART3	91
#define A10_CLK_APB1_UART4	92
#define A10_CLK_APB1_UART5	93
#define A10_CLK_APB1_UART6	94
#define A10_CLK_APB1_UART7	95

#define A10_CLK_MMC0		98
#define A10_CLK_MMC1		101
#define A10_CLK_MMC2		104
#define A10_CLK_MMC3		107
#define A10_CLK_SATA		122
#define A10_CLK_USB_OHCI0	123
#define A10_CLK_USB_OHCI1	124
#define A10_CLK_USB_PHY		125

#define A10_CLK_LOSC		254

const struct sxiccmu_ccu_bit sun4i_a10_gates[] = {
	[A10_CLK_AHB_EHCI0] =  { 0x0060, 1 },
	[A10_CLK_AHB_OHCI0] =  { 0x0060, 2 },
	[A10_CLK_AHB_EHCI1] =  { 0x0060, 3 },
	[A10_CLK_AHB_OHCI1] =  { 0x0060, 4 },
	[A10_CLK_AHB_MMC0] =   { 0x0060, 8 },
	[A10_CLK_AHB_MMC1] =   { 0x0060, 9 },
	[A10_CLK_AHB_MMC2] =   { 0x0060, 10 },
	[A10_CLK_AHB_MMC3] =   { 0x0060, 11 },
	[A10_CLK_AHB_EMAC] =   { 0x0060, 17 },
	[A10_CLK_AHB_SATA] =   { 0x0060, 25 },
	[A10_CLK_AHB_GMAC] =   { 0x0064, 17, A10_CLK_AHB },
	[A10_CLK_APB0_PIO] =   { 0x0068, 5 },
	[A10_CLK_APB1_I2C0] =  { 0x006c, 0, A10_CLK_APB1 },
	[A10_CLK_APB1_I2C1] =  { 0x006c, 1, A10_CLK_APB1 },
	[A10_CLK_APB1_I2C2] =  { 0x006c, 2, A10_CLK_APB1 },
	[A10_CLK_APB1_I2C3] =  { 0x006c, 3, A10_CLK_APB1 },
	[A10_CLK_APB1_I2C4] =  { 0x006c, 15, A10_CLK_APB1 },
	[A10_CLK_APB1_UART0] = { 0x006c, 16, A10_CLK_APB1 },
	[A10_CLK_APB1_UART1] = { 0x006c, 17, A10_CLK_APB1 },
	[A10_CLK_APB1_UART2] = { 0x006c, 18, A10_CLK_APB1 },
	[A10_CLK_APB1_UART3] = { 0x006c, 19, A10_CLK_APB1 },
	[A10_CLK_APB1_UART4] = { 0x006c, 20, A10_CLK_APB1 },
	[A10_CLK_APB1_UART5] = { 0x006c, 21, A10_CLK_APB1 },
	[A10_CLK_APB1_UART6] = { 0x006c, 22, A10_CLK_APB1 },
	[A10_CLK_APB1_UART7] = { 0x006c, 23, A10_CLK_APB1 },
	[A10_CLK_MMC0] =       { 0x0088, 31 },
	[A10_CLK_MMC1] =       { 0x008c, 31 },
	[A10_CLK_MMC2] =       { 0x0090, 31 },
	[A10_CLK_MMC3] =       { 0x0094, 31 },
	[A10_CLK_SATA] =       { 0x00c8, 31 },
	[A10_CLK_USB_OHCI0] =  { 0x00cc, 6 },
	[A10_CLK_USB_OHCI1] =  { 0x00cc, 7 },
	[A10_CLK_USB_PHY] =    { 0x00cc, 8 },
};

/* A10s */

#define A10S_CLK_HOSC		1
#define A10S_CLK_PLL_CORE	2
#define A10S_CLK_PLL_PERIPH	14

#define A10S_CLK_CPU		17
#define A10S_CLK_AXI		18
#define A10S_CLK_AHB		19
#define A10S_CLK_APB1		21

#define A10S_CLK_AHB_EHCI0	24
#define A10S_CLK_AHB_OHCI0	25
#define A10S_CLK_AHB_MMC0	29
#define A10S_CLK_AHB_MMC1	30
#define A10S_CLK_AHB_MMC2	31
#define A10S_CLK_AHB_EMAC	34
#define A10S_CLK_APB0_PIO	53
#define A10S_CLK_APB1_I2C0	56
#define A10S_CLK_APB1_I2C1	57
#define A10S_CLK_APB1_I2C2	58
#define A10S_CLK_APB1_UART0	59
#define A10S_CLK_APB1_UART1	60
#define A10S_CLK_APB1_UART2	61
#define A10S_CLK_APB1_UART3	62

#define A10S_CLK_MMC0		64
#define A10S_CLK_MMC1		65
#define A10S_CLK_MMC2		66
#define A10S_CLK_USB_OHCI0	76
#define A10S_CLK_USB_PHY0	77
#define A10S_CLK_USB_PHY1	78

#define A10S_CLK_LOSC		254

const struct sxiccmu_ccu_bit sun5i_a10s_gates[] = {
	[A10S_CLK_AHB_EHCI0] =  { 0x0060, 1 },
	[A10S_CLK_AHB_OHCI0] =  { 0x0060, 2 },
	[A10S_CLK_AHB_MMC0] =   { 0x0060, 8 },
	[A10S_CLK_AHB_MMC1] =   { 0x0060, 9 },
	[A10S_CLK_AHB_MMC2] =   { 0x0060, 10 },
	[A10S_CLK_AHB_EMAC] =   { 0x0060, 17 },
	[A10S_CLK_APB0_PIO] =   { 0x0068, 5 },
	[A10S_CLK_APB1_I2C0] =  { 0x006c, 0, A10S_CLK_APB1 },
	[A10S_CLK_APB1_I2C1] =  { 0x006c, 1, A10S_CLK_APB1 },
	[A10S_CLK_APB1_I2C2] =  { 0x006c, 2, A10S_CLK_APB1 },
	[A10S_CLK_APB1_UART0] = { 0x006c, 16, A10S_CLK_APB1 },
	[A10S_CLK_APB1_UART1] = { 0x006c, 17, A10S_CLK_APB1 },
	[A10S_CLK_APB1_UART2] = { 0x006c, 18, A10S_CLK_APB1 },
	[A10S_CLK_APB1_UART3] = { 0x006c, 19, A10S_CLK_APB1 },
	[A10S_CLK_MMC0] =       { 0x0088, 31 },
	[A10S_CLK_MMC1] =       { 0x008c, 31 },
	[A10S_CLK_MMC2] =       { 0x0090, 31 },
	[A10S_CLK_USB_OHCI0] =  { 0x00cc, 6 },
	[A10S_CLK_USB_PHY0] =   { 0x00cc, 8 },
	[A10S_CLK_USB_PHY1] =   { 0x00cc, 9 },
};

/* A23/A33 */

#define A23_CLK_PLL_PERIPH	10

#define A23_CLK_AXI		19
#define A23_CLK_AHB1		20
#define A23_CLK_APB1		21
#define A23_CLK_APB2		22

#define A23_CLK_BUS_MMC0	26
#define A23_CLK_BUS_MMC1	27
#define A23_CLK_BUS_MMC2	28
#define A23_CLK_BUS_EHCI	35
#define A23_CLK_BUS_OHCI	36
#define A23_CLK_BUS_PIO		48
#define A23_CLK_BUS_I2C0	51
#define A23_CLK_BUS_I2C1	52
#define A23_CLK_BUS_I2C2	53
#define A23_CLK_BUS_UART0	54
#define A23_CLK_BUS_UART1	55
#define A23_CLK_BUS_UART2	56
#define A23_CLK_BUS_UART3	57
#define A23_CLK_BUS_UART4	58

#define A23_CLK_MMC0		60
#define A23_CLK_MMC1		63
#define A23_CLK_MMC2		66
#define A23_CLK_USB_OHCI	78

const struct sxiccmu_ccu_bit sun8i_a23_gates[] = {
	[A23_CLK_BUS_MMC0] =  { 0x0060, 8 },
	[A23_CLK_BUS_MMC1] =  { 0x0060, 9 },
	[A23_CLK_BUS_MMC2] =  { 0x0060, 10 },
	[A23_CLK_BUS_EHCI] =  { 0x0060, 26 },
	[A23_CLK_BUS_OHCI] =  { 0x0060, 29 },
	[A23_CLK_BUS_PIO] =   { 0x0068, 5 },
	[A23_CLK_BUS_I2C0] =  { 0x006c, 0, A23_CLK_APB2 },
	[A23_CLK_BUS_I2C1] =  { 0x006c, 1, A23_CLK_APB2 },
	[A23_CLK_BUS_I2C2] =  { 0x006c, 2, A23_CLK_APB2 },
	[A23_CLK_BUS_UART0] = { 0x006c, 16, A23_CLK_APB2 },
	[A23_CLK_BUS_UART1] = { 0x006c, 17, A23_CLK_APB2 },
	[A23_CLK_BUS_UART2] = { 0x006c, 18, A23_CLK_APB2 },
	[A23_CLK_BUS_UART3] = { 0x006c, 19, A23_CLK_APB2 },
	[A23_CLK_BUS_UART4] = { 0x006c, 20, A23_CLK_APB2 },
	[A23_CLK_MMC0] =      { 0x0088, 31 },
	[A23_CLK_MMC1] =      { 0x008c, 31 },
	[A23_CLK_MMC2] =      { 0x0090, 31 },
	[A23_CLK_USB_OHCI] =  { 0x00cc, 16 },
};

/* A64 */

#define A64_CLK_PLL_CPUX	1

#define A64_CLK_PLL_PERIPH0	11
#define A64_CLK_PLL_PERIPH0_2X	12

#define A64_CLK_CPUX		21
#define A64_CLK_AXI		22
#define A64_CLK_APB		23
#define A64_CLK_AHB1		24
#define A64_CLK_APB1		25
#define A64_CLK_APB2		26
#define A64_CLK_AHB2		27

#define A64_CLK_BUS_MMC0	31
#define A64_CLK_BUS_MMC1	32
#define A64_CLK_BUS_MMC2	33
#define A64_CLK_BUS_EMAC	36
#define A64_CLK_BUS_EHCI0	42
#define A64_CLK_BUS_EHCI1	43
#define A64_CLK_BUS_OHCI0	44
#define A64_CLK_BUS_OHCI1	45
#define A64_CLK_BUS_PIO		58
#define A64_CLK_BUS_THS		59
#define A64_CLK_BUS_I2C0	63
#define A64_CLK_BUS_I2C1	64
#define A64_CLK_BUS_I2C2	65
#define A64_CLK_BUS_UART0	67
#define A64_CLK_BUS_UART1	68
#define A64_CLK_BUS_UART2	69
#define A64_CLK_BUS_UART3	70
#define A64_CLK_BUS_UART4	71

#define A64_CLK_THS		73
#define A64_CLK_MMC0		75
#define A64_CLK_MMC1		76
#define A64_CLK_MMC2		77
#define A64_CLK_USB_OHCI0	91
#define A64_CLK_USB_OHCI1	93
#define A64_CLK_USB_PHY0	86
#define A64_CLK_USB_PHY1	87

#define A64_CLK_LOSC		254
#define A64_CLK_HOSC		253

const struct sxiccmu_ccu_bit sun50i_a64_gates[] = {
	[A64_CLK_PLL_PERIPH0] = { 0x0028, 31 },
	[A64_CLK_BUS_MMC0] =  { 0x0060, 8 },
	[A64_CLK_BUS_MMC1] =  { 0x0060, 9 },
	[A64_CLK_BUS_MMC2] =  { 0x0060, 10 },
	[A64_CLK_BUS_EMAC] =  { 0x0060, 17, A64_CLK_AHB2 },
	[A64_CLK_BUS_EHCI0] = { 0x0060, 24 },
	[A64_CLK_BUS_EHCI1] = { 0x0060, 25 },
	[A64_CLK_BUS_OHCI0] = { 0x0060, 28 },
	[A64_CLK_BUS_OHCI1] = { 0x0060, 29 },
	[A64_CLK_BUS_PIO] =   { 0x0068, 5 },
	[A64_CLK_BUS_THS] =   { 0x0068, 8 },
	[A64_CLK_BUS_I2C0] =  { 0x006c, 0, A64_CLK_APB2 },
	[A64_CLK_BUS_I2C1] =  { 0x006c, 1, A64_CLK_APB2 },
	[A64_CLK_BUS_I2C2] =  { 0x006c, 2, A64_CLK_APB2 },
	[A64_CLK_BUS_UART0] = { 0x006c, 16, A64_CLK_APB2 },
	[A64_CLK_BUS_UART1] = { 0x006c, 17, A64_CLK_APB2 },
	[A64_CLK_BUS_UART2] = { 0x006c, 18, A64_CLK_APB2 },
	[A64_CLK_BUS_UART3] = { 0x006c, 19, A64_CLK_APB2 },
	[A64_CLK_BUS_UART4] = { 0x006c, 20, A64_CLK_APB2 },
	[A64_CLK_THS] =       { 0x0074, 31 },
	[A64_CLK_MMC0] =      { 0x0088, 31 },
	[A64_CLK_MMC1] =      { 0x008c, 31 },
	[A64_CLK_MMC2] =      { 0x0090, 31 },
	[A64_CLK_USB_OHCI0] = { 0x00cc, 16 },
	[A64_CLK_USB_OHCI1] = { 0x00cc, 17 },
	[A64_CLK_USB_PHY0] =  { 0x00cc,  8 },
	[A64_CLK_USB_PHY1] =  { 0x00cc,  9 },
};

/* A80 */

#define A80_CLK_PLL_PERIPH0	3
#define A80_CLK_PLL_PERIPH1	11

#define A80_CLK_GTBUS		18
#define A80_CLK_AHB1		20
#define A80_CLK_APB1		23

#define A80_CLK_MMC0		33
#define A80_CLK_MMC1		36
#define A80_CLK_MMC2		39
#define A80_CLK_MMC3		42

#define A80_CLK_BUS_MMC		84
#define A80_CLK_BUS_USB		96
#define A80_CLK_BUS_GMAC	97
#define A80_CLK_BUS_PIO		111
#define A80_CLK_BUS_I2C0	119
#define A80_CLK_BUS_I2C1	120
#define A80_CLK_BUS_I2C2	121
#define A80_CLK_BUS_I2C3	122
#define A80_CLK_BUS_I2C4	123
#define A80_CLK_BUS_UART0	124
#define A80_CLK_BUS_UART1	125
#define A80_CLK_BUS_UART2	126
#define A80_CLK_BUS_UART3	127
#define A80_CLK_BUS_UART4	128
#define A80_CLK_BUS_UART5	129

const struct sxiccmu_ccu_bit sun9i_a80_gates[] = {
	[A80_CLK_MMC0] =      { 0x0410, 31 },
	[A80_CLK_MMC1] =      { 0x0414, 31 },
	[A80_CLK_MMC2] =      { 0x0418, 31 },
	[A80_CLK_MMC3] =      { 0x041c, 31 }, /* Undocumented */
	[A80_CLK_BUS_MMC] =   { 0x0580, 8 },
	[A80_CLK_BUS_GMAC] =  { 0x0584, 17, A80_CLK_AHB1 },
	[A80_CLK_BUS_USB] =   { 0x0584, 1 },
	[A80_CLK_BUS_PIO] =   { 0x0590, 5 },
	[A80_CLK_BUS_I2C0] =  { 0x0594, 0, A80_CLK_APB1 },
	[A80_CLK_BUS_I2C1] =  { 0x0594, 1, A80_CLK_APB1 },
	[A80_CLK_BUS_I2C2] =  { 0x0594, 2, A80_CLK_APB1 },
	[A80_CLK_BUS_I2C3] =  { 0x0594, 3, A80_CLK_APB1 },
	[A80_CLK_BUS_I2C4] =  { 0x0594, 4, A80_CLK_APB1 },
	[A80_CLK_BUS_UART0] = { 0x0594, 16, A80_CLK_APB1 },
	[A80_CLK_BUS_UART1] = { 0x0594, 17, A80_CLK_APB1 },
	[A80_CLK_BUS_UART2] = { 0x0594, 18, A80_CLK_APB1 },
	[A80_CLK_BUS_UART3] = { 0x0594, 19, A80_CLK_APB1 },
	[A80_CLK_BUS_UART4] = { 0x0594, 20, A80_CLK_APB1 },
	[A80_CLK_BUS_UART5] = { 0x0594, 21, A80_CLK_APB1 },
};

#define A80_USB_CLK_HCI0	0
#define A80_USB_CLK_OHCI0	1
#define A80_USB_CLK_HCI1	2
#define A80_USB_CLK_HCI2	3
#define A80_USB_CLK_OHCI2	4

#define A80_USB_CLK_HCI0_PHY		5
#define A80_USB_CLK_HCI1_HSIC		6
#define A80_USB_CLK_HCI1_PHY		7
#define A80_USB_CLK_HCI2_HSIC		8
#define A80_USB_CLK_HCI2_UTMIPHY	9
#define A80_USB_CLK_HCI1_HSIC_12M	10

const struct sxiccmu_ccu_bit sun9i_a80_usb_gates[] = {
	[A80_USB_CLK_HCI0] =          { 0x0000, 1 },
	[A80_USB_CLK_OHCI0] =         { 0x0000, 2 },
	[A80_USB_CLK_HCI1] =          { 0x0000, 3 },
	[A80_USB_CLK_HCI2] =          { 0x0000, 5 },
	[A80_USB_CLK_OHCI2] =         { 0x0000, 6 },
	[A80_USB_CLK_HCI0_PHY] =      { 0x0004, 1 },
	[A80_USB_CLK_HCI1_HSIC] =     { 0x0004, 2 },
	[A80_USB_CLK_HCI1_PHY] =      { 0x0004, 3 }, /* Undocumented */
	[A80_USB_CLK_HCI2_HSIC] =     { 0x0004, 4 },
	[A80_USB_CLK_HCI2_UTMIPHY] =  { 0x0004, 5 },
	[A80_USB_CLK_HCI1_HSIC_12M] = { 0x0004, 10 },
};

const struct sxiccmu_ccu_bit sun9i_a80_mmc_gates[] = {
	{ 0x0000, 16 },
	{ 0x0004, 16 },
	{ 0x0008, 16 },
	{ 0x000c, 16 },
};

/* D1 */

#define D1_CLK_PLL_CPU		0
#define D1_CLK_PLL_PERIPH0	5
#define D1_CLK_PSI_AHB		23
#define D1_CLK_APB1		25
#define D1_CLK_MMC0		56
#define D1_CLK_MMC1		57
#define D1_CLK_MMC2		58
#define D1_CLK_BUS_MMC0		59
#define D1_CLK_BUS_MMC1		60
#define D1_CLK_BUS_MMC2		61
#define D1_CLK_BUS_UART0	62
#define D1_CLK_BUS_UART1	63
#define D1_CLK_BUS_UART2	64
#define D1_CLK_BUS_UART3	65
#define D1_CLK_BUS_UART4	66
#define D1_CLK_BUS_UART5	67
#define D1_CLK_BUS_EMAC		77
#define D1_CLK_USB_OHCI0	97
#define D1_CLK_USB_OHCI1	98
#define D1_CLK_BUS_OHCI0	99
#define D1_CLK_BUS_OHCI1	100
#define D1_CLK_BUS_EHCI0	101
#define D1_CLK_BUS_EHCI1	102
#define D1_CLK_RISCV		132

#define D1_CLK_HOSC		255

const struct sxiccmu_ccu_bit sun20i_d1_gates[] = {
	[D1_CLK_MMC0] =      { 0x0830, 31 },
	[D1_CLK_MMC1] =      { 0x0834, 31 },
	[D1_CLK_MMC2] =      { 0x0838, 31 },
	[D1_CLK_BUS_MMC0] =  { 0x084c, 0 },
	[D1_CLK_BUS_MMC1] =  { 0x084c, 1 },
	[D1_CLK_BUS_MMC2] =  { 0x084c, 2 },
	[D1_CLK_BUS_UART0] = { 0x090c, 0, D1_CLK_APB1 },
	[D1_CLK_BUS_UART1] = { 0x090c, 1, D1_CLK_APB1 },
	[D1_CLK_BUS_UART2] = { 0x090c, 2, D1_CLK_APB1 },
	[D1_CLK_BUS_UART3] = { 0x090c, 3, D1_CLK_APB1 },
	[D1_CLK_BUS_UART4] = { 0x090c, 4, D1_CLK_APB1 },
	[D1_CLK_BUS_UART5] = { 0x090c, 5, D1_CLK_APB1 },
	[D1_CLK_BUS_EMAC] =  { 0x097c, 0, D1_CLK_PSI_AHB },
	[D1_CLK_USB_OHCI0] = { 0x0a70, 31 },
	[D1_CLK_USB_OHCI1] = { 0x0a74, 31 },
	[D1_CLK_BUS_OHCI0] = { 0x0a8c, 0 },
	[D1_CLK_BUS_OHCI1] = { 0x0a8c, 1 },
	[D1_CLK_BUS_EHCI0] = { 0x0a8c, 4 },
	[D1_CLK_BUS_EHCI1] = { 0x0a8c, 5 },
};

/* H3/H5 */

#define H3_CLK_PLL_CPUX		0
#define H3_CLK_PLL_PERIPH0	9

#define H3_CLK_CPUX		14
#define H3_CLK_AXI		15
#define H3_CLK_AHB1		16
#define H3_CLK_APB1		17
#define H3_CLK_APB2		18
#define H3_CLK_AHB2		19

#define H3_CLK_BUS_MMC0		22
#define H3_CLK_BUS_MMC1		23
#define H3_CLK_BUS_MMC2		24
#define H3_CLK_BUS_EMAC		27
#define H3_CLK_BUS_EHCI0	33
#define H3_CLK_BUS_EHCI1	34
#define H3_CLK_BUS_EHCI2	35
#define H3_CLK_BUS_EHCI3	36
#define H3_CLK_BUS_OHCI0	37
#define H3_CLK_BUS_OHCI1	38
#define H3_CLK_BUS_OHCI2	39
#define H3_CLK_BUS_OHCI3	40
#define H3_CLK_BUS_PIO		54
#define H3_CLK_BUS_THS		55
#define H3_CLK_BUS_I2C0		59
#define H3_CLK_BUS_I2C1		60
#define H3_CLK_BUS_I2C2		61
#define H3_CLK_BUS_UART0	62
#define H3_CLK_BUS_UART1	63
#define H3_CLK_BUS_UART2	64
#define H3_CLK_BUS_UART3	65
#define H3_CLK_BUS_EPHY		67

#define H3_CLK_THS		69
#define H3_CLK_MMC0		71
#define H3_CLK_MMC1		74
#define H3_CLK_MMC2		77
#define H3_CLK_USB_PHY0		88
#define H3_CLK_USB_PHY1		89
#define H3_CLK_USB_PHY2		90
#define H3_CLK_USB_PHY3		91
#define H3_CLK_USB_OHCI0	92
#define H3_CLK_USB_OHCI1	93
#define H3_CLK_USB_OHCI2	94
#define H3_CLK_USB_OHCI3	95

#define H3_CLK_LOSC		254
#define H3_CLK_HOSC		253

const struct sxiccmu_ccu_bit sun8i_h3_gates[] = {
	[H3_CLK_PLL_PERIPH0] = { 0x0028, 31 },
	[H3_CLK_BUS_MMC0] = { 0x0060, 8 },
	[H3_CLK_BUS_MMC1] = { 0x0060, 9 },
	[H3_CLK_BUS_MMC2] = { 0x0060, 10 },
	[H3_CLK_BUS_EMAC] = { 0x0060, 17, H3_CLK_AHB2 },
	[H3_CLK_BUS_EHCI0] = { 0x0060, 24 },
	[H3_CLK_BUS_EHCI1] = { 0x0060, 25 },
	[H3_CLK_BUS_EHCI2] = { 0x0060, 26 },
	[H3_CLK_BUS_EHCI3] = { 0x0060, 27 },
	[H3_CLK_BUS_OHCI0] = { 0x0060, 28 },
	[H3_CLK_BUS_OHCI1] = { 0x0060, 29 },
	[H3_CLK_BUS_OHCI2] = { 0x0060, 30 },
	[H3_CLK_BUS_OHCI3] = { 0x0060, 31 },
	[H3_CLK_BUS_PIO]   = { 0x0068, 5 },
	[H3_CLK_BUS_THS]   = { 0x0068, 8 },
	[H3_CLK_BUS_I2C0]  = { 0x006c, 0, H3_CLK_APB2 },
	[H3_CLK_BUS_I2C1]  = { 0x006c, 1, H3_CLK_APB2 },
	[H3_CLK_BUS_I2C2]  = { 0x006c, 2, H3_CLK_APB2 },
	[H3_CLK_BUS_UART0] = { 0x006c, 16, H3_CLK_APB2 },
	[H3_CLK_BUS_UART1] = { 0x006c, 17, H3_CLK_APB2 },
	[H3_CLK_BUS_UART2] = { 0x006c, 18, H3_CLK_APB2 },
	[H3_CLK_BUS_UART3] = { 0x006c, 19, H3_CLK_APB2 },
	[H3_CLK_BUS_EPHY]  = { 0x0070, 0 },
	[H3_CLK_THS]       = { 0x0074, 31 },
	[H3_CLK_MMC0]      = { 0x0088, 31 },
	[H3_CLK_MMC1]      = { 0x008c, 31 },
	[H3_CLK_MMC2]      = { 0x0090, 31 },
	[H3_CLK_USB_PHY0]  = { 0x00cc, 8 },
	[H3_CLK_USB_PHY1]  = { 0x00cc, 9 },
	[H3_CLK_USB_PHY2]  = { 0x00cc, 10 },
	[H3_CLK_USB_PHY3]  = { 0x00cc, 11 },
	[H3_CLK_USB_OHCI0] = { 0x00cc, 16 },
	[H3_CLK_USB_OHCI1] = { 0x00cc, 17 },
	[H3_CLK_USB_OHCI2] = { 0x00cc, 18 },
	[H3_CLK_USB_OHCI3] = { 0x00cc, 19 },
};

#define H3_R_CLK_AHB0		1
#define H3_R_CLK_APB0		2

#define H3_R_CLK_APB0_PIO	3
#define H3_R_CLK_APB0_RSB	6
#define H3_R_CLK_APB0_I2C	9

const struct sxiccmu_ccu_bit sun8i_h3_r_gates[] = {
	[H3_R_CLK_APB0_PIO] = { 0x0028, 0 },
	[H3_R_CLK_APB0_RSB] = { 0x0028, 3, H3_R_CLK_APB0 },
	[H3_R_CLK_APB0_I2C] = { 0x0028, 6, H3_R_CLK_APB0 },
};

/* H6 */

#define H6_CLK_PLL_PERIPH0	3
#define H6_CLK_PLL_PERIPH0_2X	4
#define H6_CLK_AHB3		25
#define H6_CLK_APB1		26
#define H6_CLK_APB2		27
#define H6_CLK_MMC0		64
#define H6_CLK_MMC1		65
#define H6_CLK_MMC2		66
#define H6_CLK_BUS_MMC0		67
#define H6_CLK_BUS_MMC1		68
#define H6_CLK_BUS_MMC2		69
#define H6_CLK_BUS_UART0	70
#define H6_CLK_BUS_UART1	71
#define H6_CLK_BUS_UART2	72
#define H6_CLK_BUS_UART3	73
#define H6_CLK_BUS_EMAC		84
#define H6_CLK_USB_OHCI0	104
#define H6_CLK_USB_PHY0		105
#define H6_CLK_USB_PHY1		106
#define H6_CLK_USB_OHCI3	107
#define H6_CLK_USB_PHY3		108
#define H6_CLK_BUS_OHCI0	111
#define H6_CLK_BUS_OHCI3	112
#define H6_CLK_BUS_EHCI0	113
#define H6_CLK_BUS_EHCI3	115

const struct sxiccmu_ccu_bit sun50i_h6_gates[] = {
	[H6_CLK_PLL_PERIPH0] = { 0x0020, 31 },
	[H6_CLK_APB1] = { 0xffff, 0xff },
	[H6_CLK_MMC0] = { 0x0830, 31 },
	[H6_CLK_MMC1] = { 0x0834, 31 },
	[H6_CLK_MMC2] = { 0x0838, 31 },
	[H6_CLK_BUS_MMC0] = { 0x084c, 0 },
	[H6_CLK_BUS_MMC1] = { 0x084c, 1 },
	[H6_CLK_BUS_MMC2] = { 0x084c, 2 },
	[H6_CLK_BUS_UART0] = { 0x090c, 0, H6_CLK_APB2 },
	[H6_CLK_BUS_UART1] = { 0x090c, 1, H6_CLK_APB2 },
	[H6_CLK_BUS_UART2] = { 0x090c, 2, H6_CLK_APB2 },
	[H6_CLK_BUS_UART3] = { 0x090c, 3, H6_CLK_APB2 },
	[H6_CLK_BUS_EMAC] = { 0x097c, 0, H6_CLK_AHB3 },
	[H6_CLK_USB_OHCI0] = { 0x0a70, 31 },
	[H6_CLK_USB_PHY0] = { 0x0a70, 29 },
	[H6_CLK_USB_PHY1] = { 0x0a74, 29 },
	[H6_CLK_USB_OHCI3] = { 0x0a7c, 31 },
	[H6_CLK_USB_PHY3] = { 0x0a7c, 29 },
	[H6_CLK_BUS_OHCI0] = { 0x0a8c, 0 },
	[H6_CLK_BUS_OHCI3] = { 0x0a8c, 3 },
	[H6_CLK_BUS_EHCI0] = { 0x0a8c, 4 },
	[H6_CLK_BUS_EHCI3] = { 0x0a8c, 7 },
};

#define H6_R_CLK_APB1		2
#define H6_R_CLK_APB2		3
#define H6_R_CLK_APB2_I2C	8
#define H6_R_CLK_APB2_RSB	13

const struct sxiccmu_ccu_bit sun50i_h6_r_gates[] = {
	[H6_R_CLK_APB1] = { 0xffff, 0xff },
	[H6_R_CLK_APB2_I2C] = { 0x019c, 0, H6_R_CLK_APB2 },
	[H6_R_CLK_APB2_RSB] = { 0x01bc, 0, H6_R_CLK_APB2 },
};

/* H616 */

#define H616_CLK_PLL_PERIPH0	4
#define H616_CLK_PLL_PERIPH0_2X	5
#define H616_CLK_AHB3		25
#define H616_CLK_APB1		26
#define H616_CLK_APB2		27
#define H616_CLK_MMC0		60
#define H616_CLK_MMC1		61
#define H616_CLK_MMC2		62
#define H616_CLK_BUS_MMC0	63
#define H616_CLK_BUS_MMC1	64
#define H616_CLK_BUS_MMC2	65
#define H616_CLK_BUS_UART0	66
#define H616_CLK_BUS_UART1	67
#define H616_CLK_BUS_UART2	68
#define H616_CLK_BUS_UART3	69
#define H616_CLK_BUS_UART4	70
#define H616_CLK_BUS_UART5	71
#define H616_CLK_BUS_I2C0	72
#define H616_CLK_BUS_I2C1	73
#define H616_CLK_BUS_I2C2	74
#define H616_CLK_BUS_I2C3	75
#define H616_CLK_BUS_I2C4	76
#define H616_CLK_BUS_EMAC0	82
#define H616_CLK_BUS_EMAC1	83
#define H616_CLK_USB_OHCI0	96
#define H616_CLK_USB_PHY0	97
#define H616_CLK_USB_OHCI1	98
#define H616_CLK_USB_PHY1	99
#define H616_CLK_USB_OHCI2	100
#define H616_CLK_USB_PHY2	101
#define H616_CLK_USB_OHCI3	102
#define H616_CLK_USB_PHY3	103
#define H616_CLK_BUS_OHCI0	104
#define H616_CLK_BUS_OHCI1	105
#define H616_CLK_BUS_OHCI2	106
#define H616_CLK_BUS_OHCI3	107
#define H616_CLK_BUS_EHCI0	108
#define H616_CLK_BUS_EHCI1	109
#define H616_CLK_BUS_EHCI2	110
#define H616_CLK_BUS_EHCI3	111

struct sxiccmu_ccu_bit sun50i_h616_gates[] = {
	[H616_CLK_PLL_PERIPH0] = { 0x0020, 31 },
	[H616_CLK_APB1] = { 0xffff, 0xff },
	[H616_CLK_MMC0] = { 0x0830, 31 },
	[H616_CLK_MMC1] = { 0x0834, 31 },
	[H616_CLK_MMC2] = { 0x0838, 31 },
	[H616_CLK_BUS_MMC0] = { 0x084c, 0 },
	[H616_CLK_BUS_MMC1] = { 0x084c, 1 },
	[H616_CLK_BUS_MMC2] = { 0x084c, 2 },
	[H616_CLK_BUS_UART0] = { 0x090c, 0, H616_CLK_APB2 },
	[H616_CLK_BUS_UART1] = { 0x090c, 1, H616_CLK_APB2 },
	[H616_CLK_BUS_UART2] = { 0x090c, 2, H616_CLK_APB2 },
	[H616_CLK_BUS_UART3] = { 0x090c, 3, H616_CLK_APB2 },
	[H616_CLK_BUS_UART4] = { 0x090c, 4, H616_CLK_APB2 },
	[H616_CLK_BUS_UART5] = { 0x090c, 5, H616_CLK_APB2 },
	[H616_CLK_BUS_I2C0] = { 0x091c, 0, H616_CLK_APB2 },
	[H616_CLK_BUS_I2C1] = { 0x091c, 1, H616_CLK_APB2 },
	[H616_CLK_BUS_I2C2] = { 0x091c, 2, H616_CLK_APB2 },
	[H616_CLK_BUS_I2C3] = { 0x091c, 3, H616_CLK_APB2 },
	[H616_CLK_BUS_I2C4] = { 0x091c, 4, H616_CLK_APB2 },
	[H616_CLK_BUS_EMAC0] = { 0x097c, 0, H616_CLK_AHB3 },
	[H616_CLK_BUS_EMAC1] = { 0x097c, 1, H616_CLK_AHB3 },
	[H616_CLK_USB_OHCI0] = { 0x0a70, 31 },
	[H616_CLK_USB_PHY0] = { 0x0a70, 29 },
	[H616_CLK_USB_OHCI1] = { 0x0a74, 31 },
	[H616_CLK_USB_PHY1] = { 0x0a74, 29 },
	[H616_CLK_USB_OHCI2] = { 0x0a78, 31 },
	[H616_CLK_USB_PHY2] = { 0x0a78, 29 },
	[H616_CLK_USB_OHCI3] = { 0x0a7c, 31 },
	[H616_CLK_USB_PHY3] = { 0x0a7c, 29 },
	[H616_CLK_BUS_OHCI0] = { 0x0a8c, 0 },
	[H616_CLK_BUS_OHCI1] = { 0x0a8c, 1 },
	[H616_CLK_BUS_OHCI2] = { 0x0a8c, 2 },
	[H616_CLK_BUS_OHCI3] = { 0x0a8c, 3 },
	[H616_CLK_BUS_EHCI0] = { 0x0a8c, 4 },
	[H616_CLK_BUS_EHCI1] = { 0x0a8c, 5 },
	[H616_CLK_BUS_EHCI2] = { 0x0a8c, 6 },
	[H616_CLK_BUS_EHCI3] = { 0x0a8c, 7 },
};

#define H616_R_CLK_APB1		2
#define H616_R_CLK_APB2		3
#define H616_R_CLK_APB2_I2C	8
#define H616_R_CLK_APB2_RSB	13

struct sxiccmu_ccu_bit sun50i_h616_r_gates[] = {
	[H616_R_CLK_APB1] = { 0xffff, 0xff },
	[H616_R_CLK_APB2_I2C] = { 0x019c, 0, H616_R_CLK_APB2 },
	[H616_R_CLK_APB2_RSB] = { 0x01bc, 0, H616_R_CLK_APB2 },
};

/* R40 */

#define R40_CLK_PLL_PERIPH0	11
#define R40_CLK_PLL_PERIPH0_2X	13

#define R40_CLK_AXI		25
#define R40_CLK_AHB1		26
#define R40_CLK_APB2		28

#define R40_CLK_BUS_MMC0	32
#define R40_CLK_BUS_MMC1	33
#define R40_CLK_BUS_MMC2	34
#define R40_CLK_BUS_MMC3	35
#define R40_CLK_BUS_SATA	45
#define R40_CLK_BUS_EHCI0	47
#define R40_CLK_BUS_EHCI1	48
#define R40_CLK_BUS_EHCI2	49
#define R40_CLK_BUS_OHCI0	50
#define R40_CLK_BUS_OHCI1	51
#define R40_CLK_BUS_OHCI2	52
#define R40_CLK_BUS_GMAC	64
#define R40_CLK_BUS_PIO		79
#define R40_CLK_BUS_THS		82
#define R40_CLK_BUS_I2C0	87
#define R40_CLK_BUS_I2C1	88
#define R40_CLK_BUS_I2C2	89
#define R40_CLK_BUS_I2C3	90
#define R40_CLK_BUS_I2C4	95
#define R40_CLK_BUS_UART0	96
#define R40_CLK_BUS_UART1	97
#define R40_CLK_BUS_UART2	98
#define R40_CLK_BUS_UART3	99
#define R40_CLK_BUS_UART4	100
#define R40_CLK_BUS_UART5	101
#define R40_CLK_BUS_UART6	102
#define R40_CLK_BUS_UART7	103

#define R40_CLK_THS		105
#define R40_CLK_MMC0		107
#define R40_CLK_MMC1		108
#define R40_CLK_MMC2		109
#define R40_CLK_MMC3		110
#define R40_CLK_SATA		123
#define R40_CLK_USB_PHY0	124
#define R40_CLK_USB_PHY1	125
#define R40_CLK_USB_PHY2	126
#define R40_CLK_USB_OHCI0	127
#define R40_CLK_USB_OHCI1	128
#define R40_CLK_USB_OHCI2	129

#define R40_CLK_HOSC		253
#define R40_CLK_LOSC		254

const struct sxiccmu_ccu_bit sun8i_r40_gates[] = {
	[R40_CLK_BUS_MMC0] =  { 0x0060, 8 },
	[R40_CLK_BUS_MMC1] =  { 0x0060, 9 },
	[R40_CLK_BUS_MMC2] =  { 0x0060, 10 },
	[R40_CLK_BUS_MMC3] =  { 0x0060, 11 },
	[R40_CLK_BUS_SATA] =  { 0x0060, 24 },
	[R40_CLK_BUS_EHCI0] = { 0x0060, 26 },
	[R40_CLK_BUS_EHCI1] = { 0x0060, 27 },
	[R40_CLK_BUS_EHCI2] = { 0x0060, 28 },
	[R40_CLK_BUS_OHCI0] = { 0x0060, 29 },
	[R40_CLK_BUS_OHCI1] = { 0x0060, 30 },
	[R40_CLK_BUS_OHCI2] = { 0x0060, 31 },
	[R40_CLK_BUS_GMAC] =  { 0x0064, 17, R40_CLK_AHB1 },
	[R40_CLK_BUS_PIO] =   { 0x0068, 5 },
	[R40_CLK_BUS_THS] =   { 0x0068, 8 },
	[R40_CLK_BUS_I2C0] =  { 0x006c, 0, R40_CLK_APB2 },
	[R40_CLK_BUS_I2C1] =  { 0x006c, 1, R40_CLK_APB2 },
	[R40_CLK_BUS_I2C2] =  { 0x006c, 2, R40_CLK_APB2 },
	[R40_CLK_BUS_I2C3] =  { 0x006c, 3, R40_CLK_APB2 },
	[R40_CLK_BUS_I2C4] =  { 0x006c, 15, R40_CLK_APB2 },
	[R40_CLK_BUS_UART0] = { 0x006c, 16, R40_CLK_APB2 },
	[R40_CLK_BUS_UART1] = { 0x006c, 17, R40_CLK_APB2 },
	[R40_CLK_BUS_UART2] = { 0x006c, 18, R40_CLK_APB2 },
	[R40_CLK_BUS_UART3] = { 0x006c, 19, R40_CLK_APB2 },
	[R40_CLK_BUS_UART4] = { 0x006c, 20, R40_CLK_APB2 },
	[R40_CLK_BUS_UART5] = { 0x006c, 21, R40_CLK_APB2 },
	[R40_CLK_BUS_UART6] = { 0x006c, 22, R40_CLK_APB2 },
	[R40_CLK_BUS_UART7] = { 0x006c, 23, R40_CLK_APB2 },
	[R40_CLK_THS]       = { 0x0074, 31 },
	[R40_CLK_MMC0]      = { 0x0088, 31 },
	[R40_CLK_MMC1]      = { 0x008c, 31 },
	[R40_CLK_MMC2]      = { 0x0090, 31 },
	[R40_CLK_MMC3]      = { 0x0094, 31 },
	[R40_CLK_SATA]      = { 0x00c8, 31 },
	[R40_CLK_USB_PHY0]  = { 0x00cc, 8 },
	[R40_CLK_USB_PHY1]  = { 0x00cc, 9 },
	[R40_CLK_USB_PHY2]  = { 0x00cc, 10 },
	[R40_CLK_USB_OHCI0] = { 0x00cc, 16 },
	[R40_CLK_USB_OHCI1] = { 0x00cc, 17 },
	[R40_CLK_USB_OHCI2] = { 0x00cc, 18 },
};

/* V3s */

#define V3S_CLK_PLL_PERIPH0	9
#define V3S_CLK_AXI		15
#define V3S_CLK_AHB1		16
#define V3S_CLK_APB2		18
#define V3S_CLK_AHB2		19

#define V3S_CLK_BUS_MMC0	22
#define V3S_CLK_BUS_MMC1	23
#define V3S_CLK_BUS_MMC2	24
#define V3S_CLK_BUS_EMAC	26
#define V3S_CLK_BUS_EHCI0	30
#define V3S_CLK_BUS_OHCI0	31
#define V3S_CLK_BUS_PIO		37
#define V3S_CLK_BUS_I2C0	38
#define V3S_CLK_BUS_I2C1	39
#define V3S_CLK_BUS_UART0	40
#define V3S_CLK_BUS_UART1	41
#define V3S_CLK_BUS_UART2	42
#define V3S_CLK_BUS_EPHY	43

#define V3S_CLK_MMC0		45
#define V3S_CLK_MMC1		48
#define V3S_CLK_MMC2		51
#define V3S_CLK_USB_PHY0	56
#define V3S_CLK_USB_OHCI0	57

#define V3S_CLK_LOSC		254
#define V3S_CLK_HOSC		253

const struct sxiccmu_ccu_bit sun8i_v3s_gates[] = {
	[V3S_CLK_BUS_OHCI0] =	{ 0x0060, 29 },
	[V3S_CLK_BUS_EHCI0] =	{ 0x0060, 26 },
	[V3S_CLK_BUS_EMAC] =	{ 0x0060, 17, V3S_CLK_AHB2 },
	[V3S_CLK_BUS_MMC2] =	{ 0x0060, 10 },
	[V3S_CLK_BUS_MMC1] =	{ 0x0060, 9 },
	[V3S_CLK_BUS_MMC0] =	{ 0x0060, 8 },
	[V3S_CLK_BUS_PIO] =	{ 0x0068, 5 },
	[V3S_CLK_BUS_UART2] =	{ 0x006c, 18, V3S_CLK_APB2 },
	[V3S_CLK_BUS_UART1] =	{ 0x006c, 17, V3S_CLK_APB2 },
	[V3S_CLK_BUS_UART0] =	{ 0x006c, 16, V3S_CLK_APB2 },
	[V3S_CLK_BUS_I2C1] =	{ 0x006c, 1, V3S_CLK_APB2 },
	[V3S_CLK_BUS_I2C0] =	{ 0x006c, 0, V3S_CLK_APB2 },
	[V3S_CLK_BUS_EPHY] =	{ 0x0070, 0 },
	[V3S_CLK_MMC0] =	{ 0x0088, 31 },
	[V3S_CLK_MMC1] =	{ 0x008c, 31 },
	[V3S_CLK_MMC2] =	{ 0x0090, 31 },
	[V3S_CLK_USB_OHCI0] =	{ 0x00cc, 16 },
	[V3S_CLK_USB_PHY0] =	{ 0x00cc, 8 },
};

/*
 * Reset Signals
 */

/* A10 */

#define A10_RST_USB_PHY0	1
#define A10_RST_USB_PHY1	2
#define A10_RST_USB_PHY2	3

const struct sxiccmu_ccu_bit sun4i_a10_resets[] = {
	[A10_RST_USB_PHY0] = { 0x00cc, 0 },
	[A10_RST_USB_PHY1] = { 0x00cc, 1 },
	[A10_RST_USB_PHY2] = { 0x00cc, 2 },
};

/* A10s */

#define A10S_RST_USB_PHY0	0
#define A10S_RST_USB_PHY1	1

const struct sxiccmu_ccu_bit sun5i_a10s_resets[] = {
	[A10S_RST_USB_PHY0] = { 0x00cc, 0 },
	[A10S_RST_USB_PHY1] = { 0x00cc, 1 },
};

/* A23/A33 */

#define A23_RST_USB_PHY0	0
#define A23_RST_USB_PHY1	1

#define A23_RST_BUS_MMC0	7
#define A23_RST_BUS_MMC1	8
#define A23_RST_BUS_MMC2	9

#define A23_RST_BUS_EHCI	16
#define A23_RST_BUS_OHCI	17

#define A23_RST_BUS_I2C0	32
#define A23_RST_BUS_I2C1	33
#define A23_RST_BUS_I2C2	34

#define A23_CLK_HOSC		253
#define A23_CLK_LOSC		254

const struct sxiccmu_ccu_bit sun8i_a23_resets[] = {
	[A23_RST_USB_PHY0] =  { 0x00cc, 0 },
	[A23_RST_USB_PHY1] =  { 0x00cc, 1 },
	[A23_RST_BUS_MMC0] =  { 0x02c0, 8 },
	[A23_RST_BUS_MMC1] =  { 0x02c0, 9 },
	[A23_RST_BUS_MMC2] =  { 0x02c0, 10 },
	[A23_RST_BUS_EHCI] =  { 0x02c0, 26 },
	[A23_RST_BUS_OHCI] =  { 0x02c0, 29 },
	[A23_RST_BUS_I2C0] =  { 0x02d8, 0 },
	[A23_RST_BUS_I2C1] =  { 0x02d8, 1 },
	[A23_RST_BUS_I2C2] =  { 0x02d8, 2 },
};

/* A64 */

#define A64_RST_USB_PHY0	0
#define A64_RST_USB_PHY1	1

#define A64_RST_BUS_MMC0	8
#define A64_RST_BUS_MMC1	9
#define A64_RST_BUS_MMC2	10
#define A64_RST_BUS_EMAC	13
#define A64_RST_BUS_EHCI0	19
#define A64_RST_BUS_EHCI1	20
#define A64_RST_BUS_OHCI0	21
#define A64_RST_BUS_OHCI1	22
#define A64_RST_BUS_THS		38
#define A64_RST_BUS_I2C0	42
#define A64_RST_BUS_I2C1	43
#define A64_RST_BUS_I2C2	44
#define A64_RST_BUS_UART0	46
#define A64_RST_BUS_UART1	47
#define A64_RST_BUS_UART2	48
#define A64_RST_BUS_UART3	49
#define A64_RST_BUS_UART4	50

const struct sxiccmu_ccu_bit sun50i_a64_resets[] = {
	[A64_RST_USB_PHY0] =  { 0x00cc, 0 },
	[A64_RST_USB_PHY1] =  { 0x00cc, 1 },
	[A64_RST_BUS_MMC0] =  { 0x02c0, 8 },
	[A64_RST_BUS_MMC1] =  { 0x02c0, 9 },
	[A64_RST_BUS_MMC2] =  { 0x02c0, 10 },
	[A64_RST_BUS_EMAC] =  { 0x02c0, 17 },
	[A64_RST_BUS_EHCI0] = { 0x02c0, 24 },
	[A64_RST_BUS_EHCI1] = { 0x02c0, 25 },
	[A64_RST_BUS_OHCI0] = { 0x02c0, 28 },
	[A64_RST_BUS_OHCI1] = { 0x02c0, 29 },
	[A64_RST_BUS_THS] =   { 0x02d0, 8 },
	[A64_RST_BUS_I2C0] =  { 0x02d8, 0 },
	[A64_RST_BUS_I2C1] =  { 0x02d8, 1 },
	[A64_RST_BUS_I2C2] =  { 0x02d8, 2 },
	[A64_RST_BUS_UART0] = { 0x02d8, 16 },
	[A64_RST_BUS_UART1] = { 0x02d8, 17 },
	[A64_RST_BUS_UART2] = { 0x02d8, 18 },
	[A64_RST_BUS_UART3] = { 0x02d8, 19 },
	[A64_RST_BUS_UART4] = { 0x02d8, 20 },
};

/* A80 */

#define A80_RST_BUS_MMC		4
#define A80_RST_BUS_GMAC	17
#define A80_RST_BUS_I2C0	40
#define A80_RST_BUS_I2C1	41
#define A80_RST_BUS_I2C2	42
#define A80_RST_BUS_I2C3	43
#define A80_RST_BUS_I2C4	44
#define A80_RST_BUS_UART0	45
#define A80_RST_BUS_UART1	46
#define A80_RST_BUS_UART2	47
#define A80_RST_BUS_UART3	48
#define A80_RST_BUS_UART4	49
#define A80_RST_BUS_UART5	50

const struct sxiccmu_ccu_bit sun9i_a80_resets[] = {
	[A80_RST_BUS_MMC] =   { 0x05a0, 8 },
	[A80_RST_BUS_GMAC] =  { 0x05a4, 17 },
	[A80_RST_BUS_I2C0] =  { 0x05b4, 0 },
	[A80_RST_BUS_I2C1] =  { 0x05b4, 1 },
	[A80_RST_BUS_I2C2] =  { 0x05b4, 2 },
	[A80_RST_BUS_I2C3] =  { 0x05b4, 3 },
	[A80_RST_BUS_I2C4] =  { 0x05b4, 4 },
	[A80_RST_BUS_UART0] = { 0x05b4, 16 },
	[A80_RST_BUS_UART1] = { 0x05b4, 17 },
	[A80_RST_BUS_UART2] = { 0x05b4, 18 },
	[A80_RST_BUS_UART3] = { 0x05b4, 19 },
	[A80_RST_BUS_UART4] = { 0x05b4, 20 },
	[A80_RST_BUS_UART5] = { 0x05b4, 21 },
};

#define A80_USB_RST_HCI0		0
#define A80_USB_RST_HCI1		1
#define A80_USB_RST_HCI2		2

#define A80_USB_RST_HCI0_PHY		3
#define A80_USB_RST_HCI1_HSIC		4
#define A80_USB_RST_HCI1_PHY		5
#define A80_USB_RST_HCI2_HSIC		6
#define A80_USB_RST_HCI2_UTMIPHY	7

const struct sxiccmu_ccu_bit sun9i_a80_usb_resets[] = {
	[A80_USB_RST_HCI0] =         { 0x0000, 17 },
	[A80_USB_RST_HCI1] =         { 0x0000, 18 },
	[A80_USB_RST_HCI2] =         { 0x0000, 19 },
	[A80_USB_RST_HCI0_PHY] =     { 0x0004, 17 },
	[A80_USB_RST_HCI1_HSIC]=     { 0x0004, 18 },
	[A80_USB_RST_HCI1_PHY]=      { 0x0004, 19 }, /* Undocumented */
	[A80_USB_RST_HCI2_HSIC]=     { 0x0004, 20 }, /* Undocumented */
	[A80_USB_RST_HCI2_UTMIPHY] = { 0x0004, 21 },
};

const struct sxiccmu_ccu_bit sun9i_a80_mmc_resets[] = {
	{ 0x0000, 18 },
	{ 0x0004, 18 },
	{ 0x0008, 18 },
	{ 0x000c, 18 },
};

/* D1 */

#define D1_RST_BUS_MMC0		15
#define D1_RST_BUS_MMC1		16
#define D1_RST_BUS_MMC2		17
#define D1_RST_BUS_UART0	18
#define D1_RST_BUS_UART1	19
#define D1_RST_BUS_UART2	20
#define D1_RST_BUS_UART3	21
#define D1_RST_BUS_UART4	22
#define D1_RST_BUS_UART5	23
#define D1_RST_BUS_EMAC		30
#define D1_RST_USB_PHY0		40
#define D1_RST_USB_PHY1		41
#define D1_RST_BUS_OHCI0	42
#define D1_RST_BUS_OHCI1	43
#define D1_RST_BUS_EHCI0	44
#define D1_RST_BUS_EHCI1	45

const struct sxiccmu_ccu_bit sun20i_d1_resets[] = {
	[D1_RST_BUS_MMC0] =  { 0x084c, 16 },
	[D1_RST_BUS_MMC1] =  { 0x084c, 17 },
	[D1_RST_BUS_MMC2] =  { 0x084c, 18 },
	[D1_RST_BUS_UART0] = { 0x090c, 16 },
	[D1_RST_BUS_UART1] = { 0x090c, 17 },
	[D1_RST_BUS_UART2] = { 0x090c, 18 },
	[D1_RST_BUS_UART3] = { 0x090c, 19 },
	[D1_RST_BUS_UART4] = { 0x090c, 20 },
	[D1_RST_BUS_UART5] = { 0x090c, 21 },
	[D1_RST_BUS_EMAC] =  { 0x097c, 16 },
	[D1_RST_USB_PHY0] =  { 0x0a70, 30 },
	[D1_RST_USB_PHY1] =  { 0x0a74, 30 },
	[D1_RST_BUS_OHCI0] = { 0x0a8c, 16 },
	[D1_RST_BUS_OHCI1] = { 0x0a8c, 17 },
	[D1_RST_BUS_EHCI0] = { 0x0a8c, 20 },
	[D1_RST_BUS_EHCI1] = { 0x0a8c, 21 },
};

/* H3/H5 */

#define H3_RST_USB_PHY0		0
#define H3_RST_USB_PHY1		1
#define H3_RST_USB_PHY2		2
#define H3_RST_USB_PHY3		3

#define H3_RST_BUS_MMC0		7
#define H3_RST_BUS_MMC1		8
#define H3_RST_BUS_MMC2		9

#define H3_RST_BUS_EMAC		12

#define H3_RST_BUS_EHCI0	18
#define H3_RST_BUS_EHCI1	19
#define H3_RST_BUS_EHCI2	20
#define H3_RST_BUS_EHCI3	21
#define H3_RST_BUS_OHCI0	22
#define H3_RST_BUS_OHCI1	23
#define H3_RST_BUS_OHCI2	24
#define H3_RST_BUS_OHCI3	25
#define H3_RST_BUS_EPHY		39
#define H3_RST_BUS_THS		42
#define H3_RST_BUS_I2C0		46
#define H3_RST_BUS_I2C1		47
#define H3_RST_BUS_I2C2		48
#define H3_RST_BUS_UART0	49
#define H3_RST_BUS_UART1	50
#define H3_RST_BUS_UART2	51
#define H3_RST_BUS_UART3	52

const struct sxiccmu_ccu_bit sun8i_h3_resets[] = {
	[H3_RST_USB_PHY0] =  { 0x00cc, 0 },
	[H3_RST_USB_PHY1] =  { 0x00cc, 1 },
	[H3_RST_USB_PHY2] =  { 0x00cc, 2 },
	[H3_RST_USB_PHY3] =  { 0x00cc, 3 },
	[H3_RST_BUS_MMC0] =  { 0x02c0, 8 },
	[H3_RST_BUS_MMC1] =  { 0x02c0, 9 },
	[H3_RST_BUS_MMC2] =  { 0x02c0, 10 },
	[H3_RST_BUS_EMAC] =  { 0x02c0, 17 },
	[H3_RST_BUS_EHCI0] = { 0x02c0, 24 },
	[H3_RST_BUS_EHCI1] = { 0x02c0, 25 },
	[H3_RST_BUS_EHCI2] = { 0x02c0, 26 },
	[H3_RST_BUS_EHCI3] = { 0x02c0, 27 },
	[H3_RST_BUS_OHCI0] = { 0x02c0, 28 },
	[H3_RST_BUS_OHCI1] = { 0x02c0, 29 },
	[H3_RST_BUS_OHCI2] = { 0x02c0, 30 },
	[H3_RST_BUS_OHCI3] = { 0x02c0, 31 },
	[H3_RST_BUS_EPHY]  = { 0x02c8, 2 },
	[H3_RST_BUS_THS]   = { 0x02d0, 8 },
	[H3_RST_BUS_I2C0]  = { 0x02d8, 0 },
	[H3_RST_BUS_I2C1]  = { 0x02d8, 1 },
	[H3_RST_BUS_I2C2]  = { 0x02d8, 2 },
	[H3_RST_BUS_UART0] = { 0x02d8, 16 },
	[H3_RST_BUS_UART1] = { 0x02d8, 17 },
	[H3_RST_BUS_UART2] = { 0x02d8, 18 },
	[H3_RST_BUS_UART3] = { 0x02d8, 19 },
};

#define H3_R_RST_APB0_RSB	2
#define H3_R_RST_APB0_I2C	5

const struct sxiccmu_ccu_bit sun8i_h3_r_resets[] = {
	[H3_R_RST_APB0_RSB] = { 0x00b0, 3 },
	[H3_R_RST_APB0_I2C] = { 0x00b0, 6 },
};

/* H6 */

#define H6_RST_BUS_MMC0		18
#define H6_RST_BUS_MMC1		19
#define H6_RST_BUS_MMC2		20
#define H6_RST_BUS_UART0	21
#define H6_RST_BUS_UART1	22
#define H6_RST_BUS_UART2	23
#define H6_RST_BUS_UART3	24
#define H6_RST_BUS_EMAC		33
#define H6_RST_USB_PHY0		44
#define H6_RST_USB_PHY1		45
#define H6_RST_USB_PHY3		46
#define H6_RST_BUS_OHCI0	48
#define H6_RST_BUS_OHCI3	49
#define H6_RST_BUS_EHCI0	50
#define H6_RST_BUS_EHCI3	52

const struct sxiccmu_ccu_bit sun50i_h6_resets[] = {
	[H6_RST_BUS_MMC0] = { 0x084c, 16 },
	[H6_RST_BUS_MMC1] = { 0x084c, 17 },
	[H6_RST_BUS_MMC2] = { 0x084c, 18 },
	[H6_RST_BUS_UART0] = { 0x090c, 16 },
	[H6_RST_BUS_UART1] = { 0x090c, 17 },
	[H6_RST_BUS_UART2] = { 0x090c, 18 },
	[H6_RST_BUS_UART3] = { 0x090c, 19 },
	[H6_RST_BUS_EMAC] = { 0x097c, 16 },
	[H6_RST_USB_PHY0] = { 0x0a70, 30 },
	[H6_RST_USB_PHY1] = { 0x0a74, 30 },
	[H6_RST_USB_PHY3] = { 0x0a7c, 30 },
	[H6_RST_BUS_OHCI0] = { 0x0a8c, 16 },
	[H6_RST_BUS_OHCI3] = { 0x0a8c, 19 },
	[H6_RST_BUS_EHCI0] = { 0x0a8c, 20 },
	[H6_RST_BUS_EHCI3] = { 0x0a8c, 23 },
};

#define H6_R_RST_APB2_I2C	4
#define H6_R_RST_APB2_RSB	7

const struct sxiccmu_ccu_bit sun50i_h6_r_resets[] = {
	[H6_R_RST_APB2_I2C] = { 0x019c, 16 },
	[H6_R_RST_APB2_RSB] = { 0x01bc, 16 },
};

/* H616 */

#define H616_RST_BUS_MMC0	14
#define H616_RST_BUS_MMC1	15
#define H616_RST_BUS_MMC2	16
#define H616_RST_BUS_UART0	17
#define H616_RST_BUS_UART1	18
#define H616_RST_BUS_UART2	19
#define H616_RST_BUS_UART3	20
#define H616_RST_BUS_UART4	21
#define H616_RST_BUS_UART5	22
#define H616_RST_BUS_I2C0	23
#define H616_RST_BUS_I2C1	24
#define H616_RST_BUS_I2C2	25
#define H616_RST_BUS_I2C3	26
#define H616_RST_BUS_I2C4	27
#define H616_RST_BUS_EMAC0	30
#define H616_RST_BUS_EMAC1	31
#define H616_RST_USB_PHY0	38
#define H616_RST_USB_PHY1	39
#define H616_RST_USB_PHY2	40
#define H616_RST_USB_PHY3	41
#define H616_RST_BUS_OHCI0	42
#define H616_RST_BUS_OHCI1	43
#define H616_RST_BUS_OHCI2	44
#define H616_RST_BUS_OHCI3	45
#define H616_RST_BUS_EHCI0	46
#define H616_RST_BUS_EHCI1	47
#define H616_RST_BUS_EHCI2	48
#define H616_RST_BUS_EHCI3	49

struct sxiccmu_ccu_bit sun50i_h616_resets[] = {
	[H616_RST_BUS_MMC0] = { 0x084c, 16 },
	[H616_RST_BUS_MMC1] = { 0x084c, 17 },
	[H616_RST_BUS_MMC2] = { 0x084c, 18 },
	[H616_RST_BUS_UART0] = { 0x090c, 16 },
	[H616_RST_BUS_UART1] = { 0x090c, 17 },
	[H616_RST_BUS_UART2] = { 0x090c, 18 },
	[H616_RST_BUS_UART3] = { 0x090c, 19 },
	[H616_RST_BUS_UART4] = { 0x090c, 20 },
	[H616_RST_BUS_UART5] = { 0x090c, 21 },
	[H616_RST_BUS_I2C0] = { 0x091c, 16 },
	[H616_RST_BUS_I2C1] = { 0x091c, 17 },
	[H616_RST_BUS_I2C2] = { 0x091c, 18 },
	[H616_RST_BUS_I2C3] = { 0x091c, 19 },
	[H616_RST_BUS_I2C4] = { 0x091c, 20 },
	[H616_RST_BUS_EMAC0] = { 0x097c, 16 },
	[H616_RST_BUS_EMAC1] = { 0x097c, 17 },
	[H616_RST_USB_PHY0] = { 0x0a70, 30 },
	[H616_RST_USB_PHY1] = { 0x0a74, 30 },
	[H616_RST_USB_PHY2] = { 0x0a78, 30 },
	[H616_RST_USB_PHY3] = { 0x0a7c, 30 },
	[H616_RST_BUS_OHCI0] = { 0x0a8c, 16 },
	[H616_RST_BUS_OHCI1] = { 0x0a8c, 17 },
	[H616_RST_BUS_OHCI2] = { 0x0a8c, 18 },
	[H616_RST_BUS_OHCI3] = { 0x0a8c, 19 },
	[H616_RST_BUS_EHCI0] = { 0x0a8c, 20 },
	[H616_RST_BUS_EHCI1] = { 0x0a8c, 21 },
	[H616_RST_BUS_EHCI2] = { 0x0a8c, 22 },
	[H616_RST_BUS_EHCI3] = { 0x0a8c, 23 },
};

#define H616_R_RST_APB2_I2C	4
#define H616_R_RST_APB2_RSB	7

struct sxiccmu_ccu_bit sun50i_h616_r_resets[] = {
	[H616_R_RST_APB2_I2C] = { 0x019c, 16 },
	[H616_R_RST_APB2_RSB] = { 0x01bc, 16 },
};

/* R40 */

#define R40_RST_USB_PHY0	0
#define R40_RST_USB_PHY1	1
#define R40_RST_USB_PHY2	2

#define R40_RST_BUS_MMC0	8
#define R40_RST_BUS_MMC1	9
#define R40_RST_BUS_MMC2	10
#define R40_RST_BUS_MMC3	11
#define R40_RST_BUS_SATA	21
#define R40_RST_BUS_EHCI0	23
#define R40_RST_BUS_EHCI1	24
#define R40_RST_BUS_EHCI2	25
#define R40_RST_BUS_OHCI0	26
#define R40_RST_BUS_OHCI1	27
#define R40_RST_BUS_OHCI2	28
#define R40_RST_BUS_GMAC	40
#define R40_RST_BUS_THS		59
#define R40_RST_BUS_I2C0	64
#define R40_RST_BUS_I2C1	65
#define R40_RST_BUS_I2C2	66
#define R40_RST_BUS_I2C3	67
#define R40_RST_BUS_I2C4	72
#define R40_RST_BUS_UART0	73
#define R40_RST_BUS_UART1	74
#define R40_RST_BUS_UART2	75
#define R40_RST_BUS_UART3	76
#define R40_RST_BUS_UART4	77
#define R40_RST_BUS_UART5	78
#define R40_RST_BUS_UART6	79
#define R40_RST_BUS_UART7	80

const struct sxiccmu_ccu_bit sun8i_r40_resets[] = {
	[R40_RST_USB_PHY0] =  { 0x00cc, 0 },
	[R40_RST_USB_PHY1] =  { 0x00cc, 1 },
	[R40_RST_USB_PHY2] =  { 0x00cc, 2 },
	[R40_RST_BUS_MMC0] =  { 0x02c0, 8 },
	[R40_RST_BUS_MMC1] =  { 0x02c0, 9 },
	[R40_RST_BUS_MMC2] =  { 0x02c0, 10 },
	[R40_RST_BUS_MMC3] =  { 0x02c0, 11 },
	[R40_RST_BUS_SATA] =  { 0x02c0, 24 },
	[R40_RST_BUS_EHCI0] = { 0x02c0, 26 },
	[R40_RST_BUS_EHCI1] = { 0x02c0, 27 },
	[R40_RST_BUS_EHCI2] = { 0x02c0, 28 },
	[R40_RST_BUS_OHCI0] = { 0x02c0, 29 },
	[R40_RST_BUS_OHCI1] = { 0x02c0, 30 },
	[R40_RST_BUS_OHCI2] = { 0x02c0, 31 },
	[R40_RST_BUS_GMAC] =  { 0x02c4, 17 },
	[R40_RST_BUS_THS] =   { 0x02d0, 8 },
	[R40_RST_BUS_I2C0] =  { 0x02d8, 0 },
	[R40_RST_BUS_I2C1] =  { 0x02d8, 1 },
	[R40_RST_BUS_I2C2] =  { 0x02d8, 2 },
	[R40_RST_BUS_I2C3] =  { 0x02d8, 3 },
	[R40_RST_BUS_I2C4] =  { 0x02d8, 15 },
	[R40_RST_BUS_UART0] = { 0x02d8, 16 },
	[R40_RST_BUS_UART1] = { 0x02d8, 17 },
	[R40_RST_BUS_UART2] = { 0x02d8, 18 },
	[R40_RST_BUS_UART3] = { 0x02d8, 19 },
	[R40_RST_BUS_UART4] = { 0x02d8, 20 },
	[R40_RST_BUS_UART5] = { 0x02d8, 21 },
	[R40_RST_BUS_UART6] = { 0x02d8, 22 },
	[R40_RST_BUS_UART7] = { 0x02d8, 23 },
};

/* V3s */

#define V3S_RST_USB_PHY0	0

#define V3S_RST_BUS_MMC0	7
#define V3S_RST_BUS_MMC1	8
#define V3S_RST_BUS_MMC2	9
#define V3S_RST_BUS_EMAC	12
#define V3S_RST_BUS_EHCI0	18
#define V3S_RST_BUS_OHCI0	22
#define V3S_RST_BUS_EPHY	39
#define V3S_RST_BUS_I2C0	46
#define V3S_RST_BUS_I2C1	47
#define V3S_RST_BUS_UART0	49
#define V3S_RST_BUS_UART1	50
#define V3S_RST_BUS_UART2	51

const struct sxiccmu_ccu_bit sun8i_v3s_resets[] = {
	[V3S_RST_USB_PHY0] =	{ 0x00cc, 0 },
	[V3S_RST_BUS_OHCI0] =	{ 0x02c0, 29 },
	[V3S_RST_BUS_EHCI0] =	{ 0x02c0, 26 },
	[V3S_RST_BUS_EMAC] =	{ 0x02c0, 17 },
	[V3S_RST_BUS_MMC2] =	{ 0x02c0, 10 },
	[V3S_RST_BUS_MMC1] =	{ 0x02c0, 9 },
	[V3S_RST_BUS_MMC0] =	{ 0x02c0, 8 },
	[V3S_RST_BUS_EPHY] =	{ 0x02c8, 2 },
	[V3S_RST_BUS_UART2] =	{ 0x02d8, 18 },
	[V3S_RST_BUS_UART1] =	{ 0x02d8, 17 },
	[V3S_RST_BUS_UART0] =	{ 0x02d8, 16 },
	[V3S_RST_BUS_I2C1] =	{ 0x02d8, 1 },
	[V3S_RST_BUS_I2C0] =	{ 0x02d8, 0 },
};
