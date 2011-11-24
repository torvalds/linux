#ifndef _RTC_REG_REG_H_
#define _RTC_REG_REG_H_

#define RESET_CONTROL_ADDRESS                    0x00000000
#define RESET_CONTROL_OFFSET                     0x00000000
#define RESET_CONTROL_CPU_INIT_RESET_MSB         11
#define RESET_CONTROL_CPU_INIT_RESET_LSB         11
#define RESET_CONTROL_CPU_INIT_RESET_MASK        0x00000800
#define RESET_CONTROL_CPU_INIT_RESET_GET(x)      (((x) & RESET_CONTROL_CPU_INIT_RESET_MASK) >> RESET_CONTROL_CPU_INIT_RESET_LSB)
#define RESET_CONTROL_CPU_INIT_RESET_SET(x)      (((x) << RESET_CONTROL_CPU_INIT_RESET_LSB) & RESET_CONTROL_CPU_INIT_RESET_MASK)
#define RESET_CONTROL_VMC_REMAP_RESET_MSB        10
#define RESET_CONTROL_VMC_REMAP_RESET_LSB        10
#define RESET_CONTROL_VMC_REMAP_RESET_MASK       0x00000400
#define RESET_CONTROL_VMC_REMAP_RESET_GET(x)     (((x) & RESET_CONTROL_VMC_REMAP_RESET_MASK) >> RESET_CONTROL_VMC_REMAP_RESET_LSB)
#define RESET_CONTROL_VMC_REMAP_RESET_SET(x)     (((x) << RESET_CONTROL_VMC_REMAP_RESET_LSB) & RESET_CONTROL_VMC_REMAP_RESET_MASK)
#define RESET_CONTROL_RST_OUT_MSB                9
#define RESET_CONTROL_RST_OUT_LSB                9
#define RESET_CONTROL_RST_OUT_MASK               0x00000200
#define RESET_CONTROL_RST_OUT_GET(x)             (((x) & RESET_CONTROL_RST_OUT_MASK) >> RESET_CONTROL_RST_OUT_LSB)
#define RESET_CONTROL_RST_OUT_SET(x)             (((x) << RESET_CONTROL_RST_OUT_LSB) & RESET_CONTROL_RST_OUT_MASK)
#define RESET_CONTROL_COLD_RST_MSB               8
#define RESET_CONTROL_COLD_RST_LSB               8
#define RESET_CONTROL_COLD_RST_MASK              0x00000100
#define RESET_CONTROL_COLD_RST_GET(x)            (((x) & RESET_CONTROL_COLD_RST_MASK) >> RESET_CONTROL_COLD_RST_LSB)
#define RESET_CONTROL_COLD_RST_SET(x)            (((x) << RESET_CONTROL_COLD_RST_LSB) & RESET_CONTROL_COLD_RST_MASK)
#define RESET_CONTROL_WARM_RST_MSB               7
#define RESET_CONTROL_WARM_RST_LSB               7
#define RESET_CONTROL_WARM_RST_MASK              0x00000080
#define RESET_CONTROL_WARM_RST_GET(x)            (((x) & RESET_CONTROL_WARM_RST_MASK) >> RESET_CONTROL_WARM_RST_LSB)
#define RESET_CONTROL_WARM_RST_SET(x)            (((x) << RESET_CONTROL_WARM_RST_LSB) & RESET_CONTROL_WARM_RST_MASK)
#define RESET_CONTROL_CPU_WARM_RST_MSB           6
#define RESET_CONTROL_CPU_WARM_RST_LSB           6
#define RESET_CONTROL_CPU_WARM_RST_MASK          0x00000040
#define RESET_CONTROL_CPU_WARM_RST_GET(x)        (((x) & RESET_CONTROL_CPU_WARM_RST_MASK) >> RESET_CONTROL_CPU_WARM_RST_LSB)
#define RESET_CONTROL_CPU_WARM_RST_SET(x)        (((x) << RESET_CONTROL_CPU_WARM_RST_LSB) & RESET_CONTROL_CPU_WARM_RST_MASK)
#define RESET_CONTROL_MAC_COLD_RST_MSB           5
#define RESET_CONTROL_MAC_COLD_RST_LSB           5
#define RESET_CONTROL_MAC_COLD_RST_MASK          0x00000020
#define RESET_CONTROL_MAC_COLD_RST_GET(x)        (((x) & RESET_CONTROL_MAC_COLD_RST_MASK) >> RESET_CONTROL_MAC_COLD_RST_LSB)
#define RESET_CONTROL_MAC_COLD_RST_SET(x)        (((x) << RESET_CONTROL_MAC_COLD_RST_LSB) & RESET_CONTROL_MAC_COLD_RST_MASK)
#define RESET_CONTROL_MAC_WARM_RST_MSB           4
#define RESET_CONTROL_MAC_WARM_RST_LSB           4
#define RESET_CONTROL_MAC_WARM_RST_MASK          0x00000010
#define RESET_CONTROL_MAC_WARM_RST_GET(x)        (((x) & RESET_CONTROL_MAC_WARM_RST_MASK) >> RESET_CONTROL_MAC_WARM_RST_LSB)
#define RESET_CONTROL_MAC_WARM_RST_SET(x)        (((x) << RESET_CONTROL_MAC_WARM_RST_LSB) & RESET_CONTROL_MAC_WARM_RST_MASK)
#define RESET_CONTROL_MBOX_RST_MSB               2
#define RESET_CONTROL_MBOX_RST_LSB               2
#define RESET_CONTROL_MBOX_RST_MASK              0x00000004
#define RESET_CONTROL_MBOX_RST_GET(x)            (((x) & RESET_CONTROL_MBOX_RST_MASK) >> RESET_CONTROL_MBOX_RST_LSB)
#define RESET_CONTROL_MBOX_RST_SET(x)            (((x) << RESET_CONTROL_MBOX_RST_LSB) & RESET_CONTROL_MBOX_RST_MASK)
#define RESET_CONTROL_UART_RST_MSB               1
#define RESET_CONTROL_UART_RST_LSB               1
#define RESET_CONTROL_UART_RST_MASK              0x00000002
#define RESET_CONTROL_UART_RST_GET(x)            (((x) & RESET_CONTROL_UART_RST_MASK) >> RESET_CONTROL_UART_RST_LSB)
#define RESET_CONTROL_UART_RST_SET(x)            (((x) << RESET_CONTROL_UART_RST_LSB) & RESET_CONTROL_UART_RST_MASK)
#define RESET_CONTROL_SI0_RST_MSB                0
#define RESET_CONTROL_SI0_RST_LSB                0
#define RESET_CONTROL_SI0_RST_MASK               0x00000001
#define RESET_CONTROL_SI0_RST_GET(x)             (((x) & RESET_CONTROL_SI0_RST_MASK) >> RESET_CONTROL_SI0_RST_LSB)
#define RESET_CONTROL_SI0_RST_SET(x)             (((x) << RESET_CONTROL_SI0_RST_LSB) & RESET_CONTROL_SI0_RST_MASK)

#define XTAL_CONTROL_ADDRESS                     0x00000004
#define XTAL_CONTROL_OFFSET                      0x00000004
#define XTAL_CONTROL_TCXO_MSB                    0
#define XTAL_CONTROL_TCXO_LSB                    0
#define XTAL_CONTROL_TCXO_MASK                   0x00000001
#define XTAL_CONTROL_TCXO_GET(x)                 (((x) & XTAL_CONTROL_TCXO_MASK) >> XTAL_CONTROL_TCXO_LSB)
#define XTAL_CONTROL_TCXO_SET(x)                 (((x) << XTAL_CONTROL_TCXO_LSB) & XTAL_CONTROL_TCXO_MASK)

#define TCXO_DETECT_ADDRESS                      0x00000008
#define TCXO_DETECT_OFFSET                       0x00000008
#define TCXO_DETECT_PRESENT_MSB                  0
#define TCXO_DETECT_PRESENT_LSB                  0
#define TCXO_DETECT_PRESENT_MASK                 0x00000001
#define TCXO_DETECT_PRESENT_GET(x)               (((x) & TCXO_DETECT_PRESENT_MASK) >> TCXO_DETECT_PRESENT_LSB)
#define TCXO_DETECT_PRESENT_SET(x)               (((x) << TCXO_DETECT_PRESENT_LSB) & TCXO_DETECT_PRESENT_MASK)

#define XTAL_TEST_ADDRESS                        0x0000000c
#define XTAL_TEST_OFFSET                         0x0000000c
#define XTAL_TEST_NOTCXODET_MSB                  0
#define XTAL_TEST_NOTCXODET_LSB                  0
#define XTAL_TEST_NOTCXODET_MASK                 0x00000001
#define XTAL_TEST_NOTCXODET_GET(x)               (((x) & XTAL_TEST_NOTCXODET_MASK) >> XTAL_TEST_NOTCXODET_LSB)
#define XTAL_TEST_NOTCXODET_SET(x)               (((x) << XTAL_TEST_NOTCXODET_LSB) & XTAL_TEST_NOTCXODET_MASK)

#define QUADRATURE_ADDRESS                       0x00000010
#define QUADRATURE_OFFSET                        0x00000010
#define QUADRATURE_ADC_MSB                       5
#define QUADRATURE_ADC_LSB                       4
#define QUADRATURE_ADC_MASK                      0x00000030
#define QUADRATURE_ADC_GET(x)                    (((x) & QUADRATURE_ADC_MASK) >> QUADRATURE_ADC_LSB)
#define QUADRATURE_ADC_SET(x)                    (((x) << QUADRATURE_ADC_LSB) & QUADRATURE_ADC_MASK)
#define QUADRATURE_SEL_MSB                       2
#define QUADRATURE_SEL_LSB                       2
#define QUADRATURE_SEL_MASK                      0x00000004
#define QUADRATURE_SEL_GET(x)                    (((x) & QUADRATURE_SEL_MASK) >> QUADRATURE_SEL_LSB)
#define QUADRATURE_SEL_SET(x)                    (((x) << QUADRATURE_SEL_LSB) & QUADRATURE_SEL_MASK)
#define QUADRATURE_DAC_MSB                       1
#define QUADRATURE_DAC_LSB                       0
#define QUADRATURE_DAC_MASK                      0x00000003
#define QUADRATURE_DAC_GET(x)                    (((x) & QUADRATURE_DAC_MASK) >> QUADRATURE_DAC_LSB)
#define QUADRATURE_DAC_SET(x)                    (((x) << QUADRATURE_DAC_LSB) & QUADRATURE_DAC_MASK)

#define PLL_CONTROL_ADDRESS                      0x00000014
#define PLL_CONTROL_OFFSET                       0x00000014
#define PLL_CONTROL_DIG_TEST_CLK_MSB             20
#define PLL_CONTROL_DIG_TEST_CLK_LSB             20
#define PLL_CONTROL_DIG_TEST_CLK_MASK            0x00100000
#define PLL_CONTROL_DIG_TEST_CLK_GET(x)          (((x) & PLL_CONTROL_DIG_TEST_CLK_MASK) >> PLL_CONTROL_DIG_TEST_CLK_LSB)
#define PLL_CONTROL_DIG_TEST_CLK_SET(x)          (((x) << PLL_CONTROL_DIG_TEST_CLK_LSB) & PLL_CONTROL_DIG_TEST_CLK_MASK)
#define PLL_CONTROL_MAC_OVERRIDE_MSB             19
#define PLL_CONTROL_MAC_OVERRIDE_LSB             19
#define PLL_CONTROL_MAC_OVERRIDE_MASK            0x00080000
#define PLL_CONTROL_MAC_OVERRIDE_GET(x)          (((x) & PLL_CONTROL_MAC_OVERRIDE_MASK) >> PLL_CONTROL_MAC_OVERRIDE_LSB)
#define PLL_CONTROL_MAC_OVERRIDE_SET(x)          (((x) << PLL_CONTROL_MAC_OVERRIDE_LSB) & PLL_CONTROL_MAC_OVERRIDE_MASK)
#define PLL_CONTROL_NOPWD_MSB                    18
#define PLL_CONTROL_NOPWD_LSB                    18
#define PLL_CONTROL_NOPWD_MASK                   0x00040000
#define PLL_CONTROL_NOPWD_GET(x)                 (((x) & PLL_CONTROL_NOPWD_MASK) >> PLL_CONTROL_NOPWD_LSB)
#define PLL_CONTROL_NOPWD_SET(x)                 (((x) << PLL_CONTROL_NOPWD_LSB) & PLL_CONTROL_NOPWD_MASK)
#define PLL_CONTROL_UPDATING_MSB                 17
#define PLL_CONTROL_UPDATING_LSB                 17
#define PLL_CONTROL_UPDATING_MASK                0x00020000
#define PLL_CONTROL_UPDATING_GET(x)              (((x) & PLL_CONTROL_UPDATING_MASK) >> PLL_CONTROL_UPDATING_LSB)
#define PLL_CONTROL_UPDATING_SET(x)              (((x) << PLL_CONTROL_UPDATING_LSB) & PLL_CONTROL_UPDATING_MASK)
#define PLL_CONTROL_BYPASS_MSB                   16
#define PLL_CONTROL_BYPASS_LSB                   16
#define PLL_CONTROL_BYPASS_MASK                  0x00010000
#define PLL_CONTROL_BYPASS_GET(x)                (((x) & PLL_CONTROL_BYPASS_MASK) >> PLL_CONTROL_BYPASS_LSB)
#define PLL_CONTROL_BYPASS_SET(x)                (((x) << PLL_CONTROL_BYPASS_LSB) & PLL_CONTROL_BYPASS_MASK)
#define PLL_CONTROL_REFDIV_MSB                   15
#define PLL_CONTROL_REFDIV_LSB                   12
#define PLL_CONTROL_REFDIV_MASK                  0x0000f000
#define PLL_CONTROL_REFDIV_GET(x)                (((x) & PLL_CONTROL_REFDIV_MASK) >> PLL_CONTROL_REFDIV_LSB)
#define PLL_CONTROL_REFDIV_SET(x)                (((x) << PLL_CONTROL_REFDIV_LSB) & PLL_CONTROL_REFDIV_MASK)
#define PLL_CONTROL_DIV_MSB                      9
#define PLL_CONTROL_DIV_LSB                      0
#define PLL_CONTROL_DIV_MASK                     0x000003ff
#define PLL_CONTROL_DIV_GET(x)                   (((x) & PLL_CONTROL_DIV_MASK) >> PLL_CONTROL_DIV_LSB)
#define PLL_CONTROL_DIV_SET(x)                   (((x) << PLL_CONTROL_DIV_LSB) & PLL_CONTROL_DIV_MASK)

#define PLL_SETTLE_ADDRESS                       0x00000018
#define PLL_SETTLE_OFFSET                        0x00000018
#define PLL_SETTLE_TIME_MSB                      11
#define PLL_SETTLE_TIME_LSB                      0
#define PLL_SETTLE_TIME_MASK                     0x00000fff
#define PLL_SETTLE_TIME_GET(x)                   (((x) & PLL_SETTLE_TIME_MASK) >> PLL_SETTLE_TIME_LSB)
#define PLL_SETTLE_TIME_SET(x)                   (((x) << PLL_SETTLE_TIME_LSB) & PLL_SETTLE_TIME_MASK)

#define XTAL_SETTLE_ADDRESS                      0x0000001c
#define XTAL_SETTLE_OFFSET                       0x0000001c
#define XTAL_SETTLE_TIME_MSB                     7
#define XTAL_SETTLE_TIME_LSB                     0
#define XTAL_SETTLE_TIME_MASK                    0x000000ff
#define XTAL_SETTLE_TIME_GET(x)                  (((x) & XTAL_SETTLE_TIME_MASK) >> XTAL_SETTLE_TIME_LSB)
#define XTAL_SETTLE_TIME_SET(x)                  (((x) << XTAL_SETTLE_TIME_LSB) & XTAL_SETTLE_TIME_MASK)

#define CPU_CLOCK_ADDRESS                        0x00000020
#define CPU_CLOCK_OFFSET                         0x00000020
#define CPU_CLOCK_STANDARD_MSB                   1
#define CPU_CLOCK_STANDARD_LSB                   0
#define CPU_CLOCK_STANDARD_MASK                  0x00000003
#define CPU_CLOCK_STANDARD_GET(x)                (((x) & CPU_CLOCK_STANDARD_MASK) >> CPU_CLOCK_STANDARD_LSB)
#define CPU_CLOCK_STANDARD_SET(x)                (((x) << CPU_CLOCK_STANDARD_LSB) & CPU_CLOCK_STANDARD_MASK)

#define CLOCK_OUT_ADDRESS                        0x00000024
#define CLOCK_OUT_OFFSET                         0x00000024
#define CLOCK_OUT_SELECT_MSB                     3
#define CLOCK_OUT_SELECT_LSB                     0
#define CLOCK_OUT_SELECT_MASK                    0x0000000f
#define CLOCK_OUT_SELECT_GET(x)                  (((x) & CLOCK_OUT_SELECT_MASK) >> CLOCK_OUT_SELECT_LSB)
#define CLOCK_OUT_SELECT_SET(x)                  (((x) << CLOCK_OUT_SELECT_LSB) & CLOCK_OUT_SELECT_MASK)

#define CLOCK_CONTROL_ADDRESS                    0x00000028
#define CLOCK_CONTROL_OFFSET                     0x00000028
#define CLOCK_CONTROL_LF_CLK32_MSB               2
#define CLOCK_CONTROL_LF_CLK32_LSB               2
#define CLOCK_CONTROL_LF_CLK32_MASK              0x00000004
#define CLOCK_CONTROL_LF_CLK32_GET(x)            (((x) & CLOCK_CONTROL_LF_CLK32_MASK) >> CLOCK_CONTROL_LF_CLK32_LSB)
#define CLOCK_CONTROL_LF_CLK32_SET(x)            (((x) << CLOCK_CONTROL_LF_CLK32_LSB) & CLOCK_CONTROL_LF_CLK32_MASK)
#define CLOCK_CONTROL_UART_CLK_MSB               1
#define CLOCK_CONTROL_UART_CLK_LSB               1
#define CLOCK_CONTROL_UART_CLK_MASK              0x00000002
#define CLOCK_CONTROL_UART_CLK_GET(x)            (((x) & CLOCK_CONTROL_UART_CLK_MASK) >> CLOCK_CONTROL_UART_CLK_LSB)
#define CLOCK_CONTROL_UART_CLK_SET(x)            (((x) << CLOCK_CONTROL_UART_CLK_LSB) & CLOCK_CONTROL_UART_CLK_MASK)
#define CLOCK_CONTROL_SI0_CLK_MSB                0
#define CLOCK_CONTROL_SI0_CLK_LSB                0
#define CLOCK_CONTROL_SI0_CLK_MASK               0x00000001
#define CLOCK_CONTROL_SI0_CLK_GET(x)             (((x) & CLOCK_CONTROL_SI0_CLK_MASK) >> CLOCK_CONTROL_SI0_CLK_LSB)
#define CLOCK_CONTROL_SI0_CLK_SET(x)             (((x) << CLOCK_CONTROL_SI0_CLK_LSB) & CLOCK_CONTROL_SI0_CLK_MASK)

#define BIAS_OVERRIDE_ADDRESS                    0x0000002c
#define BIAS_OVERRIDE_OFFSET                     0x0000002c
#define BIAS_OVERRIDE_ON_MSB                     0
#define BIAS_OVERRIDE_ON_LSB                     0
#define BIAS_OVERRIDE_ON_MASK                    0x00000001
#define BIAS_OVERRIDE_ON_GET(x)                  (((x) & BIAS_OVERRIDE_ON_MASK) >> BIAS_OVERRIDE_ON_LSB)
#define BIAS_OVERRIDE_ON_SET(x)                  (((x) << BIAS_OVERRIDE_ON_LSB) & BIAS_OVERRIDE_ON_MASK)

#define WDT_CONTROL_ADDRESS                      0x00000030
#define WDT_CONTROL_OFFSET                       0x00000030
#define WDT_CONTROL_ACTION_MSB                   2
#define WDT_CONTROL_ACTION_LSB                   0
#define WDT_CONTROL_ACTION_MASK                  0x00000007
#define WDT_CONTROL_ACTION_GET(x)                (((x) & WDT_CONTROL_ACTION_MASK) >> WDT_CONTROL_ACTION_LSB)
#define WDT_CONTROL_ACTION_SET(x)                (((x) << WDT_CONTROL_ACTION_LSB) & WDT_CONTROL_ACTION_MASK)

#define WDT_STATUS_ADDRESS                       0x00000034
#define WDT_STATUS_OFFSET                        0x00000034
#define WDT_STATUS_INTERRUPT_MSB                 0
#define WDT_STATUS_INTERRUPT_LSB                 0
#define WDT_STATUS_INTERRUPT_MASK                0x00000001
#define WDT_STATUS_INTERRUPT_GET(x)              (((x) & WDT_STATUS_INTERRUPT_MASK) >> WDT_STATUS_INTERRUPT_LSB)
#define WDT_STATUS_INTERRUPT_SET(x)              (((x) << WDT_STATUS_INTERRUPT_LSB) & WDT_STATUS_INTERRUPT_MASK)

#define WDT_ADDRESS                              0x00000038
#define WDT_OFFSET                               0x00000038
#define WDT_TARGET_MSB                           21
#define WDT_TARGET_LSB                           0
#define WDT_TARGET_MASK                          0x003fffff
#define WDT_TARGET_GET(x)                        (((x) & WDT_TARGET_MASK) >> WDT_TARGET_LSB)
#define WDT_TARGET_SET(x)                        (((x) << WDT_TARGET_LSB) & WDT_TARGET_MASK)

#define WDT_COUNT_ADDRESS                        0x0000003c
#define WDT_COUNT_OFFSET                         0x0000003c
#define WDT_COUNT_VALUE_MSB                      21
#define WDT_COUNT_VALUE_LSB                      0
#define WDT_COUNT_VALUE_MASK                     0x003fffff
#define WDT_COUNT_VALUE_GET(x)                   (((x) & WDT_COUNT_VALUE_MASK) >> WDT_COUNT_VALUE_LSB)
#define WDT_COUNT_VALUE_SET(x)                   (((x) << WDT_COUNT_VALUE_LSB) & WDT_COUNT_VALUE_MASK)

#define WDT_RESET_ADDRESS                        0x00000040
#define WDT_RESET_OFFSET                         0x00000040
#define WDT_RESET_VALUE_MSB                      0
#define WDT_RESET_VALUE_LSB                      0
#define WDT_RESET_VALUE_MASK                     0x00000001
#define WDT_RESET_VALUE_GET(x)                   (((x) & WDT_RESET_VALUE_MASK) >> WDT_RESET_VALUE_LSB)
#define WDT_RESET_VALUE_SET(x)                   (((x) << WDT_RESET_VALUE_LSB) & WDT_RESET_VALUE_MASK)

#define INT_STATUS_ADDRESS                       0x00000044
#define INT_STATUS_OFFSET                        0x00000044
#define INT_STATUS_RTC_POWER_MSB                 14
#define INT_STATUS_RTC_POWER_LSB                 14
#define INT_STATUS_RTC_POWER_MASK                0x00004000
#define INT_STATUS_RTC_POWER_GET(x)              (((x) & INT_STATUS_RTC_POWER_MASK) >> INT_STATUS_RTC_POWER_LSB)
#define INT_STATUS_RTC_POWER_SET(x)              (((x) << INT_STATUS_RTC_POWER_LSB) & INT_STATUS_RTC_POWER_MASK)
#define INT_STATUS_MAC_MSB                       13
#define INT_STATUS_MAC_LSB                       13
#define INT_STATUS_MAC_MASK                      0x00002000
#define INT_STATUS_MAC_GET(x)                    (((x) & INT_STATUS_MAC_MASK) >> INT_STATUS_MAC_LSB)
#define INT_STATUS_MAC_SET(x)                    (((x) << INT_STATUS_MAC_LSB) & INT_STATUS_MAC_MASK)
#define INT_STATUS_MAILBOX_MSB                   12
#define INT_STATUS_MAILBOX_LSB                   12
#define INT_STATUS_MAILBOX_MASK                  0x00001000
#define INT_STATUS_MAILBOX_GET(x)                (((x) & INT_STATUS_MAILBOX_MASK) >> INT_STATUS_MAILBOX_LSB)
#define INT_STATUS_MAILBOX_SET(x)                (((x) << INT_STATUS_MAILBOX_LSB) & INT_STATUS_MAILBOX_MASK)
#define INT_STATUS_RTC_ALARM_MSB                 11
#define INT_STATUS_RTC_ALARM_LSB                 11
#define INT_STATUS_RTC_ALARM_MASK                0x00000800
#define INT_STATUS_RTC_ALARM_GET(x)              (((x) & INT_STATUS_RTC_ALARM_MASK) >> INT_STATUS_RTC_ALARM_LSB)
#define INT_STATUS_RTC_ALARM_SET(x)              (((x) << INT_STATUS_RTC_ALARM_LSB) & INT_STATUS_RTC_ALARM_MASK)
#define INT_STATUS_HF_TIMER_MSB                  10
#define INT_STATUS_HF_TIMER_LSB                  10
#define INT_STATUS_HF_TIMER_MASK                 0x00000400
#define INT_STATUS_HF_TIMER_GET(x)               (((x) & INT_STATUS_HF_TIMER_MASK) >> INT_STATUS_HF_TIMER_LSB)
#define INT_STATUS_HF_TIMER_SET(x)               (((x) << INT_STATUS_HF_TIMER_LSB) & INT_STATUS_HF_TIMER_MASK)
#define INT_STATUS_LF_TIMER3_MSB                 9
#define INT_STATUS_LF_TIMER3_LSB                 9
#define INT_STATUS_LF_TIMER3_MASK                0x00000200
#define INT_STATUS_LF_TIMER3_GET(x)              (((x) & INT_STATUS_LF_TIMER3_MASK) >> INT_STATUS_LF_TIMER3_LSB)
#define INT_STATUS_LF_TIMER3_SET(x)              (((x) << INT_STATUS_LF_TIMER3_LSB) & INT_STATUS_LF_TIMER3_MASK)
#define INT_STATUS_LF_TIMER2_MSB                 8
#define INT_STATUS_LF_TIMER2_LSB                 8
#define INT_STATUS_LF_TIMER2_MASK                0x00000100
#define INT_STATUS_LF_TIMER2_GET(x)              (((x) & INT_STATUS_LF_TIMER2_MASK) >> INT_STATUS_LF_TIMER2_LSB)
#define INT_STATUS_LF_TIMER2_SET(x)              (((x) << INT_STATUS_LF_TIMER2_LSB) & INT_STATUS_LF_TIMER2_MASK)
#define INT_STATUS_LF_TIMER1_MSB                 7
#define INT_STATUS_LF_TIMER1_LSB                 7
#define INT_STATUS_LF_TIMER1_MASK                0x00000080
#define INT_STATUS_LF_TIMER1_GET(x)              (((x) & INT_STATUS_LF_TIMER1_MASK) >> INT_STATUS_LF_TIMER1_LSB)
#define INT_STATUS_LF_TIMER1_SET(x)              (((x) << INT_STATUS_LF_TIMER1_LSB) & INT_STATUS_LF_TIMER1_MASK)
#define INT_STATUS_LF_TIMER0_MSB                 6
#define INT_STATUS_LF_TIMER0_LSB                 6
#define INT_STATUS_LF_TIMER0_MASK                0x00000040
#define INT_STATUS_LF_TIMER0_GET(x)              (((x) & INT_STATUS_LF_TIMER0_MASK) >> INT_STATUS_LF_TIMER0_LSB)
#define INT_STATUS_LF_TIMER0_SET(x)              (((x) << INT_STATUS_LF_TIMER0_LSB) & INT_STATUS_LF_TIMER0_MASK)
#define INT_STATUS_KEYPAD_MSB                    5
#define INT_STATUS_KEYPAD_LSB                    5
#define INT_STATUS_KEYPAD_MASK                   0x00000020
#define INT_STATUS_KEYPAD_GET(x)                 (((x) & INT_STATUS_KEYPAD_MASK) >> INT_STATUS_KEYPAD_LSB)
#define INT_STATUS_KEYPAD_SET(x)                 (((x) << INT_STATUS_KEYPAD_LSB) & INT_STATUS_KEYPAD_MASK)
#define INT_STATUS_SI_MSB                        4
#define INT_STATUS_SI_LSB                        4
#define INT_STATUS_SI_MASK                       0x00000010
#define INT_STATUS_SI_GET(x)                     (((x) & INT_STATUS_SI_MASK) >> INT_STATUS_SI_LSB)
#define INT_STATUS_SI_SET(x)                     (((x) << INT_STATUS_SI_LSB) & INT_STATUS_SI_MASK)
#define INT_STATUS_GPIO_MSB                      3
#define INT_STATUS_GPIO_LSB                      3
#define INT_STATUS_GPIO_MASK                     0x00000008
#define INT_STATUS_GPIO_GET(x)                   (((x) & INT_STATUS_GPIO_MASK) >> INT_STATUS_GPIO_LSB)
#define INT_STATUS_GPIO_SET(x)                   (((x) << INT_STATUS_GPIO_LSB) & INT_STATUS_GPIO_MASK)
#define INT_STATUS_UART_MSB                      2
#define INT_STATUS_UART_LSB                      2
#define INT_STATUS_UART_MASK                     0x00000004
#define INT_STATUS_UART_GET(x)                   (((x) & INT_STATUS_UART_MASK) >> INT_STATUS_UART_LSB)
#define INT_STATUS_UART_SET(x)                   (((x) << INT_STATUS_UART_LSB) & INT_STATUS_UART_MASK)
#define INT_STATUS_ERROR_MSB                     1
#define INT_STATUS_ERROR_LSB                     1
#define INT_STATUS_ERROR_MASK                    0x00000002
#define INT_STATUS_ERROR_GET(x)                  (((x) & INT_STATUS_ERROR_MASK) >> INT_STATUS_ERROR_LSB)
#define INT_STATUS_ERROR_SET(x)                  (((x) << INT_STATUS_ERROR_LSB) & INT_STATUS_ERROR_MASK)
#define INT_STATUS_WDT_INT_MSB                   0
#define INT_STATUS_WDT_INT_LSB                   0
#define INT_STATUS_WDT_INT_MASK                  0x00000001
#define INT_STATUS_WDT_INT_GET(x)                (((x) & INT_STATUS_WDT_INT_MASK) >> INT_STATUS_WDT_INT_LSB)
#define INT_STATUS_WDT_INT_SET(x)                (((x) << INT_STATUS_WDT_INT_LSB) & INT_STATUS_WDT_INT_MASK)

#define LF_TIMER0_ADDRESS                        0x00000048
#define LF_TIMER0_OFFSET                         0x00000048
#define LF_TIMER0_TARGET_MSB                     31
#define LF_TIMER0_TARGET_LSB                     0
#define LF_TIMER0_TARGET_MASK                    0xffffffff
#define LF_TIMER0_TARGET_GET(x)                  (((x) & LF_TIMER0_TARGET_MASK) >> LF_TIMER0_TARGET_LSB)
#define LF_TIMER0_TARGET_SET(x)                  (((x) << LF_TIMER0_TARGET_LSB) & LF_TIMER0_TARGET_MASK)

#define LF_TIMER_COUNT0_ADDRESS                  0x0000004c
#define LF_TIMER_COUNT0_OFFSET                   0x0000004c
#define LF_TIMER_COUNT0_VALUE_MSB                31
#define LF_TIMER_COUNT0_VALUE_LSB                0
#define LF_TIMER_COUNT0_VALUE_MASK               0xffffffff
#define LF_TIMER_COUNT0_VALUE_GET(x)             (((x) & LF_TIMER_COUNT0_VALUE_MASK) >> LF_TIMER_COUNT0_VALUE_LSB)
#define LF_TIMER_COUNT0_VALUE_SET(x)             (((x) << LF_TIMER_COUNT0_VALUE_LSB) & LF_TIMER_COUNT0_VALUE_MASK)

#define LF_TIMER_CONTROL0_ADDRESS                0x00000050
#define LF_TIMER_CONTROL0_OFFSET                 0x00000050
#define LF_TIMER_CONTROL0_ENABLE_MSB             2
#define LF_TIMER_CONTROL0_ENABLE_LSB             2
#define LF_TIMER_CONTROL0_ENABLE_MASK            0x00000004
#define LF_TIMER_CONTROL0_ENABLE_GET(x)          (((x) & LF_TIMER_CONTROL0_ENABLE_MASK) >> LF_TIMER_CONTROL0_ENABLE_LSB)
#define LF_TIMER_CONTROL0_ENABLE_SET(x)          (((x) << LF_TIMER_CONTROL0_ENABLE_LSB) & LF_TIMER_CONTROL0_ENABLE_MASK)
#define LF_TIMER_CONTROL0_AUTO_RESTART_MSB       1
#define LF_TIMER_CONTROL0_AUTO_RESTART_LSB       1
#define LF_TIMER_CONTROL0_AUTO_RESTART_MASK      0x00000002
#define LF_TIMER_CONTROL0_AUTO_RESTART_GET(x)    (((x) & LF_TIMER_CONTROL0_AUTO_RESTART_MASK) >> LF_TIMER_CONTROL0_AUTO_RESTART_LSB)
#define LF_TIMER_CONTROL0_AUTO_RESTART_SET(x)    (((x) << LF_TIMER_CONTROL0_AUTO_RESTART_LSB) & LF_TIMER_CONTROL0_AUTO_RESTART_MASK)
#define LF_TIMER_CONTROL0_RESET_MSB              0
#define LF_TIMER_CONTROL0_RESET_LSB              0
#define LF_TIMER_CONTROL0_RESET_MASK             0x00000001
#define LF_TIMER_CONTROL0_RESET_GET(x)           (((x) & LF_TIMER_CONTROL0_RESET_MASK) >> LF_TIMER_CONTROL0_RESET_LSB)
#define LF_TIMER_CONTROL0_RESET_SET(x)           (((x) << LF_TIMER_CONTROL0_RESET_LSB) & LF_TIMER_CONTROL0_RESET_MASK)

#define LF_TIMER_STATUS0_ADDRESS                 0x00000054
#define LF_TIMER_STATUS0_OFFSET                  0x00000054
#define LF_TIMER_STATUS0_INTERRUPT_MSB           0
#define LF_TIMER_STATUS0_INTERRUPT_LSB           0
#define LF_TIMER_STATUS0_INTERRUPT_MASK          0x00000001
#define LF_TIMER_STATUS0_INTERRUPT_GET(x)        (((x) & LF_TIMER_STATUS0_INTERRUPT_MASK) >> LF_TIMER_STATUS0_INTERRUPT_LSB)
#define LF_TIMER_STATUS0_INTERRUPT_SET(x)        (((x) << LF_TIMER_STATUS0_INTERRUPT_LSB) & LF_TIMER_STATUS0_INTERRUPT_MASK)

#define LF_TIMER1_ADDRESS                        0x00000058
#define LF_TIMER1_OFFSET                         0x00000058
#define LF_TIMER1_TARGET_MSB                     31
#define LF_TIMER1_TARGET_LSB                     0
#define LF_TIMER1_TARGET_MASK                    0xffffffff
#define LF_TIMER1_TARGET_GET(x)                  (((x) & LF_TIMER1_TARGET_MASK) >> LF_TIMER1_TARGET_LSB)
#define LF_TIMER1_TARGET_SET(x)                  (((x) << LF_TIMER1_TARGET_LSB) & LF_TIMER1_TARGET_MASK)

#define LF_TIMER_COUNT1_ADDRESS                  0x0000005c
#define LF_TIMER_COUNT1_OFFSET                   0x0000005c
#define LF_TIMER_COUNT1_VALUE_MSB                31
#define LF_TIMER_COUNT1_VALUE_LSB                0
#define LF_TIMER_COUNT1_VALUE_MASK               0xffffffff
#define LF_TIMER_COUNT1_VALUE_GET(x)             (((x) & LF_TIMER_COUNT1_VALUE_MASK) >> LF_TIMER_COUNT1_VALUE_LSB)
#define LF_TIMER_COUNT1_VALUE_SET(x)             (((x) << LF_TIMER_COUNT1_VALUE_LSB) & LF_TIMER_COUNT1_VALUE_MASK)

#define LF_TIMER_CONTROL1_ADDRESS                0x00000060
#define LF_TIMER_CONTROL1_OFFSET                 0x00000060
#define LF_TIMER_CONTROL1_ENABLE_MSB             2
#define LF_TIMER_CONTROL1_ENABLE_LSB             2
#define LF_TIMER_CONTROL1_ENABLE_MASK            0x00000004
#define LF_TIMER_CONTROL1_ENABLE_GET(x)          (((x) & LF_TIMER_CONTROL1_ENABLE_MASK) >> LF_TIMER_CONTROL1_ENABLE_LSB)
#define LF_TIMER_CONTROL1_ENABLE_SET(x)          (((x) << LF_TIMER_CONTROL1_ENABLE_LSB) & LF_TIMER_CONTROL1_ENABLE_MASK)
#define LF_TIMER_CONTROL1_AUTO_RESTART_MSB       1
#define LF_TIMER_CONTROL1_AUTO_RESTART_LSB       1
#define LF_TIMER_CONTROL1_AUTO_RESTART_MASK      0x00000002
#define LF_TIMER_CONTROL1_AUTO_RESTART_GET(x)    (((x) & LF_TIMER_CONTROL1_AUTO_RESTART_MASK) >> LF_TIMER_CONTROL1_AUTO_RESTART_LSB)
#define LF_TIMER_CONTROL1_AUTO_RESTART_SET(x)    (((x) << LF_TIMER_CONTROL1_AUTO_RESTART_LSB) & LF_TIMER_CONTROL1_AUTO_RESTART_MASK)
#define LF_TIMER_CONTROL1_RESET_MSB              0
#define LF_TIMER_CONTROL1_RESET_LSB              0
#define LF_TIMER_CONTROL1_RESET_MASK             0x00000001
#define LF_TIMER_CONTROL1_RESET_GET(x)           (((x) & LF_TIMER_CONTROL1_RESET_MASK) >> LF_TIMER_CONTROL1_RESET_LSB)
#define LF_TIMER_CONTROL1_RESET_SET(x)           (((x) << LF_TIMER_CONTROL1_RESET_LSB) & LF_TIMER_CONTROL1_RESET_MASK)

#define LF_TIMER_STATUS1_ADDRESS                 0x00000064
#define LF_TIMER_STATUS1_OFFSET                  0x00000064
#define LF_TIMER_STATUS1_INTERRUPT_MSB           0
#define LF_TIMER_STATUS1_INTERRUPT_LSB           0
#define LF_TIMER_STATUS1_INTERRUPT_MASK          0x00000001
#define LF_TIMER_STATUS1_INTERRUPT_GET(x)        (((x) & LF_TIMER_STATUS1_INTERRUPT_MASK) >> LF_TIMER_STATUS1_INTERRUPT_LSB)
#define LF_TIMER_STATUS1_INTERRUPT_SET(x)        (((x) << LF_TIMER_STATUS1_INTERRUPT_LSB) & LF_TIMER_STATUS1_INTERRUPT_MASK)

#define LF_TIMER2_ADDRESS                        0x00000068
#define LF_TIMER2_OFFSET                         0x00000068
#define LF_TIMER2_TARGET_MSB                     31
#define LF_TIMER2_TARGET_LSB                     0
#define LF_TIMER2_TARGET_MASK                    0xffffffff
#define LF_TIMER2_TARGET_GET(x)                  (((x) & LF_TIMER2_TARGET_MASK) >> LF_TIMER2_TARGET_LSB)
#define LF_TIMER2_TARGET_SET(x)                  (((x) << LF_TIMER2_TARGET_LSB) & LF_TIMER2_TARGET_MASK)

#define LF_TIMER_COUNT2_ADDRESS                  0x0000006c
#define LF_TIMER_COUNT2_OFFSET                   0x0000006c
#define LF_TIMER_COUNT2_VALUE_MSB                31
#define LF_TIMER_COUNT2_VALUE_LSB                0
#define LF_TIMER_COUNT2_VALUE_MASK               0xffffffff
#define LF_TIMER_COUNT2_VALUE_GET(x)             (((x) & LF_TIMER_COUNT2_VALUE_MASK) >> LF_TIMER_COUNT2_VALUE_LSB)
#define LF_TIMER_COUNT2_VALUE_SET(x)             (((x) << LF_TIMER_COUNT2_VALUE_LSB) & LF_TIMER_COUNT2_VALUE_MASK)

#define LF_TIMER_CONTROL2_ADDRESS                0x00000070
#define LF_TIMER_CONTROL2_OFFSET                 0x00000070
#define LF_TIMER_CONTROL2_ENABLE_MSB             2
#define LF_TIMER_CONTROL2_ENABLE_LSB             2
#define LF_TIMER_CONTROL2_ENABLE_MASK            0x00000004
#define LF_TIMER_CONTROL2_ENABLE_GET(x)          (((x) & LF_TIMER_CONTROL2_ENABLE_MASK) >> LF_TIMER_CONTROL2_ENABLE_LSB)
#define LF_TIMER_CONTROL2_ENABLE_SET(x)          (((x) << LF_TIMER_CONTROL2_ENABLE_LSB) & LF_TIMER_CONTROL2_ENABLE_MASK)
#define LF_TIMER_CONTROL2_AUTO_RESTART_MSB       1
#define LF_TIMER_CONTROL2_AUTO_RESTART_LSB       1
#define LF_TIMER_CONTROL2_AUTO_RESTART_MASK      0x00000002
#define LF_TIMER_CONTROL2_AUTO_RESTART_GET(x)    (((x) & LF_TIMER_CONTROL2_AUTO_RESTART_MASK) >> LF_TIMER_CONTROL2_AUTO_RESTART_LSB)
#define LF_TIMER_CONTROL2_AUTO_RESTART_SET(x)    (((x) << LF_TIMER_CONTROL2_AUTO_RESTART_LSB) & LF_TIMER_CONTROL2_AUTO_RESTART_MASK)
#define LF_TIMER_CONTROL2_RESET_MSB              0
#define LF_TIMER_CONTROL2_RESET_LSB              0
#define LF_TIMER_CONTROL2_RESET_MASK             0x00000001
#define LF_TIMER_CONTROL2_RESET_GET(x)           (((x) & LF_TIMER_CONTROL2_RESET_MASK) >> LF_TIMER_CONTROL2_RESET_LSB)
#define LF_TIMER_CONTROL2_RESET_SET(x)           (((x) << LF_TIMER_CONTROL2_RESET_LSB) & LF_TIMER_CONTROL2_RESET_MASK)

#define LF_TIMER_STATUS2_ADDRESS                 0x00000074
#define LF_TIMER_STATUS2_OFFSET                  0x00000074
#define LF_TIMER_STATUS2_INTERRUPT_MSB           0
#define LF_TIMER_STATUS2_INTERRUPT_LSB           0
#define LF_TIMER_STATUS2_INTERRUPT_MASK          0x00000001
#define LF_TIMER_STATUS2_INTERRUPT_GET(x)        (((x) & LF_TIMER_STATUS2_INTERRUPT_MASK) >> LF_TIMER_STATUS2_INTERRUPT_LSB)
#define LF_TIMER_STATUS2_INTERRUPT_SET(x)        (((x) << LF_TIMER_STATUS2_INTERRUPT_LSB) & LF_TIMER_STATUS2_INTERRUPT_MASK)

#define LF_TIMER3_ADDRESS                        0x00000078
#define LF_TIMER3_OFFSET                         0x00000078
#define LF_TIMER3_TARGET_MSB                     31
#define LF_TIMER3_TARGET_LSB                     0
#define LF_TIMER3_TARGET_MASK                    0xffffffff
#define LF_TIMER3_TARGET_GET(x)                  (((x) & LF_TIMER3_TARGET_MASK) >> LF_TIMER3_TARGET_LSB)
#define LF_TIMER3_TARGET_SET(x)                  (((x) << LF_TIMER3_TARGET_LSB) & LF_TIMER3_TARGET_MASK)

#define LF_TIMER_COUNT3_ADDRESS                  0x0000007c
#define LF_TIMER_COUNT3_OFFSET                   0x0000007c
#define LF_TIMER_COUNT3_VALUE_MSB                31
#define LF_TIMER_COUNT3_VALUE_LSB                0
#define LF_TIMER_COUNT3_VALUE_MASK               0xffffffff
#define LF_TIMER_COUNT3_VALUE_GET(x)             (((x) & LF_TIMER_COUNT3_VALUE_MASK) >> LF_TIMER_COUNT3_VALUE_LSB)
#define LF_TIMER_COUNT3_VALUE_SET(x)             (((x) << LF_TIMER_COUNT3_VALUE_LSB) & LF_TIMER_COUNT3_VALUE_MASK)

#define LF_TIMER_CONTROL3_ADDRESS                0x00000080
#define LF_TIMER_CONTROL3_OFFSET                 0x00000080
#define LF_TIMER_CONTROL3_ENABLE_MSB             2
#define LF_TIMER_CONTROL3_ENABLE_LSB             2
#define LF_TIMER_CONTROL3_ENABLE_MASK            0x00000004
#define LF_TIMER_CONTROL3_ENABLE_GET(x)          (((x) & LF_TIMER_CONTROL3_ENABLE_MASK) >> LF_TIMER_CONTROL3_ENABLE_LSB)
#define LF_TIMER_CONTROL3_ENABLE_SET(x)          (((x) << LF_TIMER_CONTROL3_ENABLE_LSB) & LF_TIMER_CONTROL3_ENABLE_MASK)
#define LF_TIMER_CONTROL3_AUTO_RESTART_MSB       1
#define LF_TIMER_CONTROL3_AUTO_RESTART_LSB       1
#define LF_TIMER_CONTROL3_AUTO_RESTART_MASK      0x00000002
#define LF_TIMER_CONTROL3_AUTO_RESTART_GET(x)    (((x) & LF_TIMER_CONTROL3_AUTO_RESTART_MASK) >> LF_TIMER_CONTROL3_AUTO_RESTART_LSB)
#define LF_TIMER_CONTROL3_AUTO_RESTART_SET(x)    (((x) << LF_TIMER_CONTROL3_AUTO_RESTART_LSB) & LF_TIMER_CONTROL3_AUTO_RESTART_MASK)
#define LF_TIMER_CONTROL3_RESET_MSB              0
#define LF_TIMER_CONTROL3_RESET_LSB              0
#define LF_TIMER_CONTROL3_RESET_MASK             0x00000001
#define LF_TIMER_CONTROL3_RESET_GET(x)           (((x) & LF_TIMER_CONTROL3_RESET_MASK) >> LF_TIMER_CONTROL3_RESET_LSB)
#define LF_TIMER_CONTROL3_RESET_SET(x)           (((x) << LF_TIMER_CONTROL3_RESET_LSB) & LF_TIMER_CONTROL3_RESET_MASK)

#define LF_TIMER_STATUS3_ADDRESS                 0x00000084
#define LF_TIMER_STATUS3_OFFSET                  0x00000084
#define LF_TIMER_STATUS3_INTERRUPT_MSB           0
#define LF_TIMER_STATUS3_INTERRUPT_LSB           0
#define LF_TIMER_STATUS3_INTERRUPT_MASK          0x00000001
#define LF_TIMER_STATUS3_INTERRUPT_GET(x)        (((x) & LF_TIMER_STATUS3_INTERRUPT_MASK) >> LF_TIMER_STATUS3_INTERRUPT_LSB)
#define LF_TIMER_STATUS3_INTERRUPT_SET(x)        (((x) << LF_TIMER_STATUS3_INTERRUPT_LSB) & LF_TIMER_STATUS3_INTERRUPT_MASK)

#define HF_TIMER_ADDRESS                         0x00000088
#define HF_TIMER_OFFSET                          0x00000088
#define HF_TIMER_TARGET_MSB                      31
#define HF_TIMER_TARGET_LSB                      12
#define HF_TIMER_TARGET_MASK                     0xfffff000
#define HF_TIMER_TARGET_GET(x)                   (((x) & HF_TIMER_TARGET_MASK) >> HF_TIMER_TARGET_LSB)
#define HF_TIMER_TARGET_SET(x)                   (((x) << HF_TIMER_TARGET_LSB) & HF_TIMER_TARGET_MASK)

#define HF_TIMER_COUNT_ADDRESS                   0x0000008c
#define HF_TIMER_COUNT_OFFSET                    0x0000008c
#define HF_TIMER_COUNT_VALUE_MSB                 31
#define HF_TIMER_COUNT_VALUE_LSB                 12
#define HF_TIMER_COUNT_VALUE_MASK                0xfffff000
#define HF_TIMER_COUNT_VALUE_GET(x)              (((x) & HF_TIMER_COUNT_VALUE_MASK) >> HF_TIMER_COUNT_VALUE_LSB)
#define HF_TIMER_COUNT_VALUE_SET(x)              (((x) << HF_TIMER_COUNT_VALUE_LSB) & HF_TIMER_COUNT_VALUE_MASK)

#define HF_LF_COUNT_ADDRESS                      0x00000090
#define HF_LF_COUNT_OFFSET                       0x00000090
#define HF_LF_COUNT_VALUE_MSB                    31
#define HF_LF_COUNT_VALUE_LSB                    0
#define HF_LF_COUNT_VALUE_MASK                   0xffffffff
#define HF_LF_COUNT_VALUE_GET(x)                 (((x) & HF_LF_COUNT_VALUE_MASK) >> HF_LF_COUNT_VALUE_LSB)
#define HF_LF_COUNT_VALUE_SET(x)                 (((x) << HF_LF_COUNT_VALUE_LSB) & HF_LF_COUNT_VALUE_MASK)

#define HF_TIMER_CONTROL_ADDRESS                 0x00000094
#define HF_TIMER_CONTROL_OFFSET                  0x00000094
#define HF_TIMER_CONTROL_ENABLE_MSB              3
#define HF_TIMER_CONTROL_ENABLE_LSB              3
#define HF_TIMER_CONTROL_ENABLE_MASK             0x00000008
#define HF_TIMER_CONTROL_ENABLE_GET(x)           (((x) & HF_TIMER_CONTROL_ENABLE_MASK) >> HF_TIMER_CONTROL_ENABLE_LSB)
#define HF_TIMER_CONTROL_ENABLE_SET(x)           (((x) << HF_TIMER_CONTROL_ENABLE_LSB) & HF_TIMER_CONTROL_ENABLE_MASK)
#define HF_TIMER_CONTROL_ON_MSB                  2
#define HF_TIMER_CONTROL_ON_LSB                  2
#define HF_TIMER_CONTROL_ON_MASK                 0x00000004
#define HF_TIMER_CONTROL_ON_GET(x)               (((x) & HF_TIMER_CONTROL_ON_MASK) >> HF_TIMER_CONTROL_ON_LSB)
#define HF_TIMER_CONTROL_ON_SET(x)               (((x) << HF_TIMER_CONTROL_ON_LSB) & HF_TIMER_CONTROL_ON_MASK)
#define HF_TIMER_CONTROL_AUTO_RESTART_MSB        1
#define HF_TIMER_CONTROL_AUTO_RESTART_LSB        1
#define HF_TIMER_CONTROL_AUTO_RESTART_MASK       0x00000002
#define HF_TIMER_CONTROL_AUTO_RESTART_GET(x)     (((x) & HF_TIMER_CONTROL_AUTO_RESTART_MASK) >> HF_TIMER_CONTROL_AUTO_RESTART_LSB)
#define HF_TIMER_CONTROL_AUTO_RESTART_SET(x)     (((x) << HF_TIMER_CONTROL_AUTO_RESTART_LSB) & HF_TIMER_CONTROL_AUTO_RESTART_MASK)
#define HF_TIMER_CONTROL_RESET_MSB               0
#define HF_TIMER_CONTROL_RESET_LSB               0
#define HF_TIMER_CONTROL_RESET_MASK              0x00000001
#define HF_TIMER_CONTROL_RESET_GET(x)            (((x) & HF_TIMER_CONTROL_RESET_MASK) >> HF_TIMER_CONTROL_RESET_LSB)
#define HF_TIMER_CONTROL_RESET_SET(x)            (((x) << HF_TIMER_CONTROL_RESET_LSB) & HF_TIMER_CONTROL_RESET_MASK)

#define HF_TIMER_STATUS_ADDRESS                  0x00000098
#define HF_TIMER_STATUS_OFFSET                   0x00000098
#define HF_TIMER_STATUS_INTERRUPT_MSB            0
#define HF_TIMER_STATUS_INTERRUPT_LSB            0
#define HF_TIMER_STATUS_INTERRUPT_MASK           0x00000001
#define HF_TIMER_STATUS_INTERRUPT_GET(x)         (((x) & HF_TIMER_STATUS_INTERRUPT_MASK) >> HF_TIMER_STATUS_INTERRUPT_LSB)
#define HF_TIMER_STATUS_INTERRUPT_SET(x)         (((x) << HF_TIMER_STATUS_INTERRUPT_LSB) & HF_TIMER_STATUS_INTERRUPT_MASK)

#define RTC_CONTROL_ADDRESS                      0x0000009c
#define RTC_CONTROL_OFFSET                       0x0000009c
#define RTC_CONTROL_ENABLE_MSB                   2
#define RTC_CONTROL_ENABLE_LSB                   2
#define RTC_CONTROL_ENABLE_MASK                  0x00000004
#define RTC_CONTROL_ENABLE_GET(x)                (((x) & RTC_CONTROL_ENABLE_MASK) >> RTC_CONTROL_ENABLE_LSB)
#define RTC_CONTROL_ENABLE_SET(x)                (((x) << RTC_CONTROL_ENABLE_LSB) & RTC_CONTROL_ENABLE_MASK)
#define RTC_CONTROL_LOAD_RTC_MSB                 1
#define RTC_CONTROL_LOAD_RTC_LSB                 1
#define RTC_CONTROL_LOAD_RTC_MASK                0x00000002
#define RTC_CONTROL_LOAD_RTC_GET(x)              (((x) & RTC_CONTROL_LOAD_RTC_MASK) >> RTC_CONTROL_LOAD_RTC_LSB)
#define RTC_CONTROL_LOAD_RTC_SET(x)              (((x) << RTC_CONTROL_LOAD_RTC_LSB) & RTC_CONTROL_LOAD_RTC_MASK)
#define RTC_CONTROL_LOAD_ALARM_MSB               0
#define RTC_CONTROL_LOAD_ALARM_LSB               0
#define RTC_CONTROL_LOAD_ALARM_MASK              0x00000001
#define RTC_CONTROL_LOAD_ALARM_GET(x)            (((x) & RTC_CONTROL_LOAD_ALARM_MASK) >> RTC_CONTROL_LOAD_ALARM_LSB)
#define RTC_CONTROL_LOAD_ALARM_SET(x)            (((x) << RTC_CONTROL_LOAD_ALARM_LSB) & RTC_CONTROL_LOAD_ALARM_MASK)

#define RTC_TIME_ADDRESS                         0x000000a0
#define RTC_TIME_OFFSET                          0x000000a0
#define RTC_TIME_WEEK_DAY_MSB                    26
#define RTC_TIME_WEEK_DAY_LSB                    24
#define RTC_TIME_WEEK_DAY_MASK                   0x07000000
#define RTC_TIME_WEEK_DAY_GET(x)                 (((x) & RTC_TIME_WEEK_DAY_MASK) >> RTC_TIME_WEEK_DAY_LSB)
#define RTC_TIME_WEEK_DAY_SET(x)                 (((x) << RTC_TIME_WEEK_DAY_LSB) & RTC_TIME_WEEK_DAY_MASK)
#define RTC_TIME_HOUR_MSB                        21
#define RTC_TIME_HOUR_LSB                        16
#define RTC_TIME_HOUR_MASK                       0x003f0000
#define RTC_TIME_HOUR_GET(x)                     (((x) & RTC_TIME_HOUR_MASK) >> RTC_TIME_HOUR_LSB)
#define RTC_TIME_HOUR_SET(x)                     (((x) << RTC_TIME_HOUR_LSB) & RTC_TIME_HOUR_MASK)
#define RTC_TIME_MINUTE_MSB                      14
#define RTC_TIME_MINUTE_LSB                      8
#define RTC_TIME_MINUTE_MASK                     0x00007f00
#define RTC_TIME_MINUTE_GET(x)                   (((x) & RTC_TIME_MINUTE_MASK) >> RTC_TIME_MINUTE_LSB)
#define RTC_TIME_MINUTE_SET(x)                   (((x) << RTC_TIME_MINUTE_LSB) & RTC_TIME_MINUTE_MASK)
#define RTC_TIME_SECOND_MSB                      6
#define RTC_TIME_SECOND_LSB                      0
#define RTC_TIME_SECOND_MASK                     0x0000007f
#define RTC_TIME_SECOND_GET(x)                   (((x) & RTC_TIME_SECOND_MASK) >> RTC_TIME_SECOND_LSB)
#define RTC_TIME_SECOND_SET(x)                   (((x) << RTC_TIME_SECOND_LSB) & RTC_TIME_SECOND_MASK)

#define RTC_DATE_ADDRESS                         0x000000a4
#define RTC_DATE_OFFSET                          0x000000a4
#define RTC_DATE_YEAR_MSB                        23
#define RTC_DATE_YEAR_LSB                        16
#define RTC_DATE_YEAR_MASK                       0x00ff0000
#define RTC_DATE_YEAR_GET(x)                     (((x) & RTC_DATE_YEAR_MASK) >> RTC_DATE_YEAR_LSB)
#define RTC_DATE_YEAR_SET(x)                     (((x) << RTC_DATE_YEAR_LSB) & RTC_DATE_YEAR_MASK)
#define RTC_DATE_MONTH_MSB                       12
#define RTC_DATE_MONTH_LSB                       8
#define RTC_DATE_MONTH_MASK                      0x00001f00
#define RTC_DATE_MONTH_GET(x)                    (((x) & RTC_DATE_MONTH_MASK) >> RTC_DATE_MONTH_LSB)
#define RTC_DATE_MONTH_SET(x)                    (((x) << RTC_DATE_MONTH_LSB) & RTC_DATE_MONTH_MASK)
#define RTC_DATE_MONTH_DAY_MSB                   5
#define RTC_DATE_MONTH_DAY_LSB                   0
#define RTC_DATE_MONTH_DAY_MASK                  0x0000003f
#define RTC_DATE_MONTH_DAY_GET(x)                (((x) & RTC_DATE_MONTH_DAY_MASK) >> RTC_DATE_MONTH_DAY_LSB)
#define RTC_DATE_MONTH_DAY_SET(x)                (((x) << RTC_DATE_MONTH_DAY_LSB) & RTC_DATE_MONTH_DAY_MASK)

#define RTC_SET_TIME_ADDRESS                     0x000000a8
#define RTC_SET_TIME_OFFSET                      0x000000a8
#define RTC_SET_TIME_WEEK_DAY_MSB                26
#define RTC_SET_TIME_WEEK_DAY_LSB                24
#define RTC_SET_TIME_WEEK_DAY_MASK               0x07000000
#define RTC_SET_TIME_WEEK_DAY_GET(x)             (((x) & RTC_SET_TIME_WEEK_DAY_MASK) >> RTC_SET_TIME_WEEK_DAY_LSB)
#define RTC_SET_TIME_WEEK_DAY_SET(x)             (((x) << RTC_SET_TIME_WEEK_DAY_LSB) & RTC_SET_TIME_WEEK_DAY_MASK)
#define RTC_SET_TIME_HOUR_MSB                    21
#define RTC_SET_TIME_HOUR_LSB                    16
#define RTC_SET_TIME_HOUR_MASK                   0x003f0000
#define RTC_SET_TIME_HOUR_GET(x)                 (((x) & RTC_SET_TIME_HOUR_MASK) >> RTC_SET_TIME_HOUR_LSB)
#define RTC_SET_TIME_HOUR_SET(x)                 (((x) << RTC_SET_TIME_HOUR_LSB) & RTC_SET_TIME_HOUR_MASK)
#define RTC_SET_TIME_MINUTE_MSB                  14
#define RTC_SET_TIME_MINUTE_LSB                  8
#define RTC_SET_TIME_MINUTE_MASK                 0x00007f00
#define RTC_SET_TIME_MINUTE_GET(x)               (((x) & RTC_SET_TIME_MINUTE_MASK) >> RTC_SET_TIME_MINUTE_LSB)
#define RTC_SET_TIME_MINUTE_SET(x)               (((x) << RTC_SET_TIME_MINUTE_LSB) & RTC_SET_TIME_MINUTE_MASK)
#define RTC_SET_TIME_SECOND_MSB                  6
#define RTC_SET_TIME_SECOND_LSB                  0
#define RTC_SET_TIME_SECOND_MASK                 0x0000007f
#define RTC_SET_TIME_SECOND_GET(x)               (((x) & RTC_SET_TIME_SECOND_MASK) >> RTC_SET_TIME_SECOND_LSB)
#define RTC_SET_TIME_SECOND_SET(x)               (((x) << RTC_SET_TIME_SECOND_LSB) & RTC_SET_TIME_SECOND_MASK)

#define RTC_SET_DATE_ADDRESS                     0x000000ac
#define RTC_SET_DATE_OFFSET                      0x000000ac
#define RTC_SET_DATE_YEAR_MSB                    23
#define RTC_SET_DATE_YEAR_LSB                    16
#define RTC_SET_DATE_YEAR_MASK                   0x00ff0000
#define RTC_SET_DATE_YEAR_GET(x)                 (((x) & RTC_SET_DATE_YEAR_MASK) >> RTC_SET_DATE_YEAR_LSB)
#define RTC_SET_DATE_YEAR_SET(x)                 (((x) << RTC_SET_DATE_YEAR_LSB) & RTC_SET_DATE_YEAR_MASK)
#define RTC_SET_DATE_MONTH_MSB                   12
#define RTC_SET_DATE_MONTH_LSB                   8
#define RTC_SET_DATE_MONTH_MASK                  0x00001f00
#define RTC_SET_DATE_MONTH_GET(x)                (((x) & RTC_SET_DATE_MONTH_MASK) >> RTC_SET_DATE_MONTH_LSB)
#define RTC_SET_DATE_MONTH_SET(x)                (((x) << RTC_SET_DATE_MONTH_LSB) & RTC_SET_DATE_MONTH_MASK)
#define RTC_SET_DATE_MONTH_DAY_MSB               5
#define RTC_SET_DATE_MONTH_DAY_LSB               0
#define RTC_SET_DATE_MONTH_DAY_MASK              0x0000003f
#define RTC_SET_DATE_MONTH_DAY_GET(x)            (((x) & RTC_SET_DATE_MONTH_DAY_MASK) >> RTC_SET_DATE_MONTH_DAY_LSB)
#define RTC_SET_DATE_MONTH_DAY_SET(x)            (((x) << RTC_SET_DATE_MONTH_DAY_LSB) & RTC_SET_DATE_MONTH_DAY_MASK)

#define RTC_SET_ALARM_ADDRESS                    0x000000b0
#define RTC_SET_ALARM_OFFSET                     0x000000b0
#define RTC_SET_ALARM_HOUR_MSB                   21
#define RTC_SET_ALARM_HOUR_LSB                   16
#define RTC_SET_ALARM_HOUR_MASK                  0x003f0000
#define RTC_SET_ALARM_HOUR_GET(x)                (((x) & RTC_SET_ALARM_HOUR_MASK) >> RTC_SET_ALARM_HOUR_LSB)
#define RTC_SET_ALARM_HOUR_SET(x)                (((x) << RTC_SET_ALARM_HOUR_LSB) & RTC_SET_ALARM_HOUR_MASK)
#define RTC_SET_ALARM_MINUTE_MSB                 14
#define RTC_SET_ALARM_MINUTE_LSB                 8
#define RTC_SET_ALARM_MINUTE_MASK                0x00007f00
#define RTC_SET_ALARM_MINUTE_GET(x)              (((x) & RTC_SET_ALARM_MINUTE_MASK) >> RTC_SET_ALARM_MINUTE_LSB)
#define RTC_SET_ALARM_MINUTE_SET(x)              (((x) << RTC_SET_ALARM_MINUTE_LSB) & RTC_SET_ALARM_MINUTE_MASK)
#define RTC_SET_ALARM_SECOND_MSB                 6
#define RTC_SET_ALARM_SECOND_LSB                 0
#define RTC_SET_ALARM_SECOND_MASK                0x0000007f
#define RTC_SET_ALARM_SECOND_GET(x)              (((x) & RTC_SET_ALARM_SECOND_MASK) >> RTC_SET_ALARM_SECOND_LSB)
#define RTC_SET_ALARM_SECOND_SET(x)              (((x) << RTC_SET_ALARM_SECOND_LSB) & RTC_SET_ALARM_SECOND_MASK)

#define RTC_CONFIG_ADDRESS                       0x000000b4
#define RTC_CONFIG_OFFSET                        0x000000b4
#define RTC_CONFIG_BCD_MSB                       2
#define RTC_CONFIG_BCD_LSB                       2
#define RTC_CONFIG_BCD_MASK                      0x00000004
#define RTC_CONFIG_BCD_GET(x)                    (((x) & RTC_CONFIG_BCD_MASK) >> RTC_CONFIG_BCD_LSB)
#define RTC_CONFIG_BCD_SET(x)                    (((x) << RTC_CONFIG_BCD_LSB) & RTC_CONFIG_BCD_MASK)
#define RTC_CONFIG_TWELVE_HOUR_MSB               1
#define RTC_CONFIG_TWELVE_HOUR_LSB               1
#define RTC_CONFIG_TWELVE_HOUR_MASK              0x00000002
#define RTC_CONFIG_TWELVE_HOUR_GET(x)            (((x) & RTC_CONFIG_TWELVE_HOUR_MASK) >> RTC_CONFIG_TWELVE_HOUR_LSB)
#define RTC_CONFIG_TWELVE_HOUR_SET(x)            (((x) << RTC_CONFIG_TWELVE_HOUR_LSB) & RTC_CONFIG_TWELVE_HOUR_MASK)
#define RTC_CONFIG_DSE_MSB                       0
#define RTC_CONFIG_DSE_LSB                       0
#define RTC_CONFIG_DSE_MASK                      0x00000001
#define RTC_CONFIG_DSE_GET(x)                    (((x) & RTC_CONFIG_DSE_MASK) >> RTC_CONFIG_DSE_LSB)
#define RTC_CONFIG_DSE_SET(x)                    (((x) << RTC_CONFIG_DSE_LSB) & RTC_CONFIG_DSE_MASK)

#define RTC_ALARM_STATUS_ADDRESS                 0x000000b8
#define RTC_ALARM_STATUS_OFFSET                  0x000000b8
#define RTC_ALARM_STATUS_ENABLE_MSB              1
#define RTC_ALARM_STATUS_ENABLE_LSB              1
#define RTC_ALARM_STATUS_ENABLE_MASK             0x00000002
#define RTC_ALARM_STATUS_ENABLE_GET(x)           (((x) & RTC_ALARM_STATUS_ENABLE_MASK) >> RTC_ALARM_STATUS_ENABLE_LSB)
#define RTC_ALARM_STATUS_ENABLE_SET(x)           (((x) << RTC_ALARM_STATUS_ENABLE_LSB) & RTC_ALARM_STATUS_ENABLE_MASK)
#define RTC_ALARM_STATUS_INTERRUPT_MSB           0
#define RTC_ALARM_STATUS_INTERRUPT_LSB           0
#define RTC_ALARM_STATUS_INTERRUPT_MASK          0x00000001
#define RTC_ALARM_STATUS_INTERRUPT_GET(x)        (((x) & RTC_ALARM_STATUS_INTERRUPT_MASK) >> RTC_ALARM_STATUS_INTERRUPT_LSB)
#define RTC_ALARM_STATUS_INTERRUPT_SET(x)        (((x) << RTC_ALARM_STATUS_INTERRUPT_LSB) & RTC_ALARM_STATUS_INTERRUPT_MASK)

#define UART_WAKEUP_ADDRESS                      0x000000bc
#define UART_WAKEUP_OFFSET                       0x000000bc
#define UART_WAKEUP_ENABLE_MSB                   0
#define UART_WAKEUP_ENABLE_LSB                   0
#define UART_WAKEUP_ENABLE_MASK                  0x00000001
#define UART_WAKEUP_ENABLE_GET(x)                (((x) & UART_WAKEUP_ENABLE_MASK) >> UART_WAKEUP_ENABLE_LSB)
#define UART_WAKEUP_ENABLE_SET(x)                (((x) << UART_WAKEUP_ENABLE_LSB) & UART_WAKEUP_ENABLE_MASK)

#define RESET_CAUSE_ADDRESS                      0x000000c0
#define RESET_CAUSE_OFFSET                       0x000000c0
#define RESET_CAUSE_LAST_MSB                     2
#define RESET_CAUSE_LAST_LSB                     0
#define RESET_CAUSE_LAST_MASK                    0x00000007
#define RESET_CAUSE_LAST_GET(x)                  (((x) & RESET_CAUSE_LAST_MASK) >> RESET_CAUSE_LAST_LSB)
#define RESET_CAUSE_LAST_SET(x)                  (((x) << RESET_CAUSE_LAST_LSB) & RESET_CAUSE_LAST_MASK)

#define SYSTEM_SLEEP_ADDRESS                     0x000000c4
#define SYSTEM_SLEEP_OFFSET                      0x000000c4
#define SYSTEM_SLEEP_HOST_IF_MSB                 4
#define SYSTEM_SLEEP_HOST_IF_LSB                 4
#define SYSTEM_SLEEP_HOST_IF_MASK                0x00000010
#define SYSTEM_SLEEP_HOST_IF_GET(x)              (((x) & SYSTEM_SLEEP_HOST_IF_MASK) >> SYSTEM_SLEEP_HOST_IF_LSB)
#define SYSTEM_SLEEP_HOST_IF_SET(x)              (((x) << SYSTEM_SLEEP_HOST_IF_LSB) & SYSTEM_SLEEP_HOST_IF_MASK)
#define SYSTEM_SLEEP_MBOX_MSB                    3
#define SYSTEM_SLEEP_MBOX_LSB                    3
#define SYSTEM_SLEEP_MBOX_MASK                   0x00000008
#define SYSTEM_SLEEP_MBOX_GET(x)                 (((x) & SYSTEM_SLEEP_MBOX_MASK) >> SYSTEM_SLEEP_MBOX_LSB)
#define SYSTEM_SLEEP_MBOX_SET(x)                 (((x) << SYSTEM_SLEEP_MBOX_LSB) & SYSTEM_SLEEP_MBOX_MASK)
#define SYSTEM_SLEEP_MAC_IF_MSB                  2
#define SYSTEM_SLEEP_MAC_IF_LSB                  2
#define SYSTEM_SLEEP_MAC_IF_MASK                 0x00000004
#define SYSTEM_SLEEP_MAC_IF_GET(x)               (((x) & SYSTEM_SLEEP_MAC_IF_MASK) >> SYSTEM_SLEEP_MAC_IF_LSB)
#define SYSTEM_SLEEP_MAC_IF_SET(x)               (((x) << SYSTEM_SLEEP_MAC_IF_LSB) & SYSTEM_SLEEP_MAC_IF_MASK)
#define SYSTEM_SLEEP_LIGHT_MSB                   1
#define SYSTEM_SLEEP_LIGHT_LSB                   1
#define SYSTEM_SLEEP_LIGHT_MASK                  0x00000002
#define SYSTEM_SLEEP_LIGHT_GET(x)                (((x) & SYSTEM_SLEEP_LIGHT_MASK) >> SYSTEM_SLEEP_LIGHT_LSB)
#define SYSTEM_SLEEP_LIGHT_SET(x)                (((x) << SYSTEM_SLEEP_LIGHT_LSB) & SYSTEM_SLEEP_LIGHT_MASK)
#define SYSTEM_SLEEP_DISABLE_MSB                 0
#define SYSTEM_SLEEP_DISABLE_LSB                 0
#define SYSTEM_SLEEP_DISABLE_MASK                0x00000001
#define SYSTEM_SLEEP_DISABLE_GET(x)              (((x) & SYSTEM_SLEEP_DISABLE_MASK) >> SYSTEM_SLEEP_DISABLE_LSB)
#define SYSTEM_SLEEP_DISABLE_SET(x)              (((x) << SYSTEM_SLEEP_DISABLE_LSB) & SYSTEM_SLEEP_DISABLE_MASK)

#define SDIO_WRAPPER_ADDRESS                     0x000000c8
#define SDIO_WRAPPER_OFFSET                      0x000000c8
#define SDIO_WRAPPER_SLEEP_MSB                   3
#define SDIO_WRAPPER_SLEEP_LSB                   3
#define SDIO_WRAPPER_SLEEP_MASK                  0x00000008
#define SDIO_WRAPPER_SLEEP_GET(x)                (((x) & SDIO_WRAPPER_SLEEP_MASK) >> SDIO_WRAPPER_SLEEP_LSB)
#define SDIO_WRAPPER_SLEEP_SET(x)                (((x) << SDIO_WRAPPER_SLEEP_LSB) & SDIO_WRAPPER_SLEEP_MASK)
#define SDIO_WRAPPER_WAKEUP_MSB                  2
#define SDIO_WRAPPER_WAKEUP_LSB                  2
#define SDIO_WRAPPER_WAKEUP_MASK                 0x00000004
#define SDIO_WRAPPER_WAKEUP_GET(x)               (((x) & SDIO_WRAPPER_WAKEUP_MASK) >> SDIO_WRAPPER_WAKEUP_LSB)
#define SDIO_WRAPPER_WAKEUP_SET(x)               (((x) << SDIO_WRAPPER_WAKEUP_LSB) & SDIO_WRAPPER_WAKEUP_MASK)
#define SDIO_WRAPPER_SOC_ON_MSB                  1
#define SDIO_WRAPPER_SOC_ON_LSB                  1
#define SDIO_WRAPPER_SOC_ON_MASK                 0x00000002
#define SDIO_WRAPPER_SOC_ON_GET(x)               (((x) & SDIO_WRAPPER_SOC_ON_MASK) >> SDIO_WRAPPER_SOC_ON_LSB)
#define SDIO_WRAPPER_SOC_ON_SET(x)               (((x) << SDIO_WRAPPER_SOC_ON_LSB) & SDIO_WRAPPER_SOC_ON_MASK)
#define SDIO_WRAPPER_ON_MSB                      0
#define SDIO_WRAPPER_ON_LSB                      0
#define SDIO_WRAPPER_ON_MASK                     0x00000001
#define SDIO_WRAPPER_ON_GET(x)                   (((x) & SDIO_WRAPPER_ON_MASK) >> SDIO_WRAPPER_ON_LSB)
#define SDIO_WRAPPER_ON_SET(x)                   (((x) << SDIO_WRAPPER_ON_LSB) & SDIO_WRAPPER_ON_MASK)

#define MAC_SLEEP_CONTROL_ADDRESS                0x000000cc
#define MAC_SLEEP_CONTROL_OFFSET                 0x000000cc
#define MAC_SLEEP_CONTROL_ENABLE_MSB             1
#define MAC_SLEEP_CONTROL_ENABLE_LSB             0
#define MAC_SLEEP_CONTROL_ENABLE_MASK            0x00000003
#define MAC_SLEEP_CONTROL_ENABLE_GET(x)          (((x) & MAC_SLEEP_CONTROL_ENABLE_MASK) >> MAC_SLEEP_CONTROL_ENABLE_LSB)
#define MAC_SLEEP_CONTROL_ENABLE_SET(x)          (((x) << MAC_SLEEP_CONTROL_ENABLE_LSB) & MAC_SLEEP_CONTROL_ENABLE_MASK)

#define KEEP_AWAKE_ADDRESS                       0x000000d0
#define KEEP_AWAKE_OFFSET                        0x000000d0
#define KEEP_AWAKE_COUNT_MSB                     7
#define KEEP_AWAKE_COUNT_LSB                     0
#define KEEP_AWAKE_COUNT_MASK                    0x000000ff
#define KEEP_AWAKE_COUNT_GET(x)                  (((x) & KEEP_AWAKE_COUNT_MASK) >> KEEP_AWAKE_COUNT_LSB)
#define KEEP_AWAKE_COUNT_SET(x)                  (((x) << KEEP_AWAKE_COUNT_LSB) & KEEP_AWAKE_COUNT_MASK)

#define LPO_CAL_TIME_ADDRESS                     0x000000d4
#define LPO_CAL_TIME_OFFSET                      0x000000d4
#define LPO_CAL_TIME_LENGTH_MSB                  13
#define LPO_CAL_TIME_LENGTH_LSB                  0
#define LPO_CAL_TIME_LENGTH_MASK                 0x00003fff
#define LPO_CAL_TIME_LENGTH_GET(x)               (((x) & LPO_CAL_TIME_LENGTH_MASK) >> LPO_CAL_TIME_LENGTH_LSB)
#define LPO_CAL_TIME_LENGTH_SET(x)               (((x) << LPO_CAL_TIME_LENGTH_LSB) & LPO_CAL_TIME_LENGTH_MASK)

#define LPO_INIT_DIVIDEND_INT_ADDRESS            0x000000d8
#define LPO_INIT_DIVIDEND_INT_OFFSET             0x000000d8
#define LPO_INIT_DIVIDEND_INT_VALUE_MSB          23
#define LPO_INIT_DIVIDEND_INT_VALUE_LSB          0
#define LPO_INIT_DIVIDEND_INT_VALUE_MASK         0x00ffffff
#define LPO_INIT_DIVIDEND_INT_VALUE_GET(x)       (((x) & LPO_INIT_DIVIDEND_INT_VALUE_MASK) >> LPO_INIT_DIVIDEND_INT_VALUE_LSB)
#define LPO_INIT_DIVIDEND_INT_VALUE_SET(x)       (((x) << LPO_INIT_DIVIDEND_INT_VALUE_LSB) & LPO_INIT_DIVIDEND_INT_VALUE_MASK)

#define LPO_INIT_DIVIDEND_FRACTION_ADDRESS       0x000000dc
#define LPO_INIT_DIVIDEND_FRACTION_OFFSET        0x000000dc
#define LPO_INIT_DIVIDEND_FRACTION_VALUE_MSB     10
#define LPO_INIT_DIVIDEND_FRACTION_VALUE_LSB     0
#define LPO_INIT_DIVIDEND_FRACTION_VALUE_MASK    0x000007ff
#define LPO_INIT_DIVIDEND_FRACTION_VALUE_GET(x)  (((x) & LPO_INIT_DIVIDEND_FRACTION_VALUE_MASK) >> LPO_INIT_DIVIDEND_FRACTION_VALUE_LSB)
#define LPO_INIT_DIVIDEND_FRACTION_VALUE_SET(x)  (((x) << LPO_INIT_DIVIDEND_FRACTION_VALUE_LSB) & LPO_INIT_DIVIDEND_FRACTION_VALUE_MASK)

#define LPO_CAL_ADDRESS                          0x000000e0
#define LPO_CAL_OFFSET                           0x000000e0
#define LPO_CAL_ENABLE_MSB                       20
#define LPO_CAL_ENABLE_LSB                       20
#define LPO_CAL_ENABLE_MASK                      0x00100000
#define LPO_CAL_ENABLE_GET(x)                    (((x) & LPO_CAL_ENABLE_MASK) >> LPO_CAL_ENABLE_LSB)
#define LPO_CAL_ENABLE_SET(x)                    (((x) << LPO_CAL_ENABLE_LSB) & LPO_CAL_ENABLE_MASK)
#define LPO_CAL_COUNT_MSB                        19
#define LPO_CAL_COUNT_LSB                        0
#define LPO_CAL_COUNT_MASK                       0x000fffff
#define LPO_CAL_COUNT_GET(x)                     (((x) & LPO_CAL_COUNT_MASK) >> LPO_CAL_COUNT_LSB)
#define LPO_CAL_COUNT_SET(x)                     (((x) << LPO_CAL_COUNT_LSB) & LPO_CAL_COUNT_MASK)

#define LPO_CAL_TEST_CONTROL_ADDRESS             0x000000e4
#define LPO_CAL_TEST_CONTROL_OFFSET              0x000000e4
#define LPO_CAL_TEST_CONTROL_ENABLE_MSB          5
#define LPO_CAL_TEST_CONTROL_ENABLE_LSB          5
#define LPO_CAL_TEST_CONTROL_ENABLE_MASK         0x00000020
#define LPO_CAL_TEST_CONTROL_ENABLE_GET(x)       (((x) & LPO_CAL_TEST_CONTROL_ENABLE_MASK) >> LPO_CAL_TEST_CONTROL_ENABLE_LSB)
#define LPO_CAL_TEST_CONTROL_ENABLE_SET(x)       (((x) << LPO_CAL_TEST_CONTROL_ENABLE_LSB) & LPO_CAL_TEST_CONTROL_ENABLE_MASK)
#define LPO_CAL_TEST_CONTROL_RTC_CYCLES_MSB      4
#define LPO_CAL_TEST_CONTROL_RTC_CYCLES_LSB      0
#define LPO_CAL_TEST_CONTROL_RTC_CYCLES_MASK     0x0000001f
#define LPO_CAL_TEST_CONTROL_RTC_CYCLES_GET(x)   (((x) & LPO_CAL_TEST_CONTROL_RTC_CYCLES_MASK) >> LPO_CAL_TEST_CONTROL_RTC_CYCLES_LSB)
#define LPO_CAL_TEST_CONTROL_RTC_CYCLES_SET(x)   (((x) << LPO_CAL_TEST_CONTROL_RTC_CYCLES_LSB) & LPO_CAL_TEST_CONTROL_RTC_CYCLES_MASK)

#define LPO_CAL_TEST_STATUS_ADDRESS              0x000000e8
#define LPO_CAL_TEST_STATUS_OFFSET               0x000000e8
#define LPO_CAL_TEST_STATUS_READY_MSB            16
#define LPO_CAL_TEST_STATUS_READY_LSB            16
#define LPO_CAL_TEST_STATUS_READY_MASK           0x00010000
#define LPO_CAL_TEST_STATUS_READY_GET(x)         (((x) & LPO_CAL_TEST_STATUS_READY_MASK) >> LPO_CAL_TEST_STATUS_READY_LSB)
#define LPO_CAL_TEST_STATUS_READY_SET(x)         (((x) << LPO_CAL_TEST_STATUS_READY_LSB) & LPO_CAL_TEST_STATUS_READY_MASK)
#define LPO_CAL_TEST_STATUS_COUNT_MSB            15
#define LPO_CAL_TEST_STATUS_COUNT_LSB            0
#define LPO_CAL_TEST_STATUS_COUNT_MASK           0x0000ffff
#define LPO_CAL_TEST_STATUS_COUNT_GET(x)         (((x) & LPO_CAL_TEST_STATUS_COUNT_MASK) >> LPO_CAL_TEST_STATUS_COUNT_LSB)
#define LPO_CAL_TEST_STATUS_COUNT_SET(x)         (((x) << LPO_CAL_TEST_STATUS_COUNT_LSB) & LPO_CAL_TEST_STATUS_COUNT_MASK)

#define CHIP_ID_ADDRESS                          0x000000ec
#define CHIP_ID_OFFSET                           0x000000ec
#define CHIP_ID_DEVICE_ID_MSB                    31
#define CHIP_ID_DEVICE_ID_LSB                    16
#define CHIP_ID_DEVICE_ID_MASK                   0xffff0000
#define CHIP_ID_DEVICE_ID_GET(x)                 (((x) & CHIP_ID_DEVICE_ID_MASK) >> CHIP_ID_DEVICE_ID_LSB)
#define CHIP_ID_DEVICE_ID_SET(x)                 (((x) << CHIP_ID_DEVICE_ID_LSB) & CHIP_ID_DEVICE_ID_MASK)
#define CHIP_ID_CONFIG_ID_MSB                    15
#define CHIP_ID_CONFIG_ID_LSB                    4
#define CHIP_ID_CONFIG_ID_MASK                   0x0000fff0
#define CHIP_ID_CONFIG_ID_GET(x)                 (((x) & CHIP_ID_CONFIG_ID_MASK) >> CHIP_ID_CONFIG_ID_LSB)
#define CHIP_ID_CONFIG_ID_SET(x)                 (((x) << CHIP_ID_CONFIG_ID_LSB) & CHIP_ID_CONFIG_ID_MASK)
#define CHIP_ID_VERSION_ID_MSB                   3
#define CHIP_ID_VERSION_ID_LSB                   0
#define CHIP_ID_VERSION_ID_MASK                  0x0000000f
#define CHIP_ID_VERSION_ID_GET(x)                (((x) & CHIP_ID_VERSION_ID_MASK) >> CHIP_ID_VERSION_ID_LSB)
#define CHIP_ID_VERSION_ID_SET(x)                (((x) << CHIP_ID_VERSION_ID_LSB) & CHIP_ID_VERSION_ID_MASK)

#define DERIVED_RTC_CLK_ADDRESS                  0x000000f0
#define DERIVED_RTC_CLK_OFFSET                   0x000000f0
#define DERIVED_RTC_CLK_EXTERNAL_DETECT_EN_MSB   20
#define DERIVED_RTC_CLK_EXTERNAL_DETECT_EN_LSB   20
#define DERIVED_RTC_CLK_EXTERNAL_DETECT_EN_MASK  0x00100000
#define DERIVED_RTC_CLK_EXTERNAL_DETECT_EN_GET(x) (((x) & DERIVED_RTC_CLK_EXTERNAL_DETECT_EN_MASK) >> DERIVED_RTC_CLK_EXTERNAL_DETECT_EN_LSB)
#define DERIVED_RTC_CLK_EXTERNAL_DETECT_EN_SET(x) (((x) << DERIVED_RTC_CLK_EXTERNAL_DETECT_EN_LSB) & DERIVED_RTC_CLK_EXTERNAL_DETECT_EN_MASK)
#define DERIVED_RTC_CLK_EXTERNAL_DETECT_MSB      18
#define DERIVED_RTC_CLK_EXTERNAL_DETECT_LSB      18
#define DERIVED_RTC_CLK_EXTERNAL_DETECT_MASK     0x00040000
#define DERIVED_RTC_CLK_EXTERNAL_DETECT_GET(x)   (((x) & DERIVED_RTC_CLK_EXTERNAL_DETECT_MASK) >> DERIVED_RTC_CLK_EXTERNAL_DETECT_LSB)
#define DERIVED_RTC_CLK_EXTERNAL_DETECT_SET(x)   (((x) << DERIVED_RTC_CLK_EXTERNAL_DETECT_LSB) & DERIVED_RTC_CLK_EXTERNAL_DETECT_MASK)
#define DERIVED_RTC_CLK_FORCE_MSB                17
#define DERIVED_RTC_CLK_FORCE_LSB                16
#define DERIVED_RTC_CLK_FORCE_MASK               0x00030000
#define DERIVED_RTC_CLK_FORCE_GET(x)             (((x) & DERIVED_RTC_CLK_FORCE_MASK) >> DERIVED_RTC_CLK_FORCE_LSB)
#define DERIVED_RTC_CLK_FORCE_SET(x)             (((x) << DERIVED_RTC_CLK_FORCE_LSB) & DERIVED_RTC_CLK_FORCE_MASK)
#define DERIVED_RTC_CLK_PERIOD_MSB               15
#define DERIVED_RTC_CLK_PERIOD_LSB               1
#define DERIVED_RTC_CLK_PERIOD_MASK              0x0000fffe
#define DERIVED_RTC_CLK_PERIOD_GET(x)            (((x) & DERIVED_RTC_CLK_PERIOD_MASK) >> DERIVED_RTC_CLK_PERIOD_LSB)
#define DERIVED_RTC_CLK_PERIOD_SET(x)            (((x) << DERIVED_RTC_CLK_PERIOD_LSB) & DERIVED_RTC_CLK_PERIOD_MASK)

#define MAC_PCU_SLP32_MODE_ADDRESS               0x000000f4
#define MAC_PCU_SLP32_MODE_OFFSET                0x000000f4
#define MAC_PCU_SLP32_MODE_TSF_WRITE_PENDING_MSB 21
#define MAC_PCU_SLP32_MODE_TSF_WRITE_PENDING_LSB 21
#define MAC_PCU_SLP32_MODE_TSF_WRITE_PENDING_MASK 0x00200000
#define MAC_PCU_SLP32_MODE_TSF_WRITE_PENDING_GET(x) (((x) & MAC_PCU_SLP32_MODE_TSF_WRITE_PENDING_MASK) >> MAC_PCU_SLP32_MODE_TSF_WRITE_PENDING_LSB)
#define MAC_PCU_SLP32_MODE_TSF_WRITE_PENDING_SET(x) (((x) << MAC_PCU_SLP32_MODE_TSF_WRITE_PENDING_LSB) & MAC_PCU_SLP32_MODE_TSF_WRITE_PENDING_MASK)
#define MAC_PCU_SLP32_MODE_HALF_CLK_LATENCY_MSB  19
#define MAC_PCU_SLP32_MODE_HALF_CLK_LATENCY_LSB  0
#define MAC_PCU_SLP32_MODE_HALF_CLK_LATENCY_MASK 0x000fffff
#define MAC_PCU_SLP32_MODE_HALF_CLK_LATENCY_GET(x) (((x) & MAC_PCU_SLP32_MODE_HALF_CLK_LATENCY_MASK) >> MAC_PCU_SLP32_MODE_HALF_CLK_LATENCY_LSB)
#define MAC_PCU_SLP32_MODE_HALF_CLK_LATENCY_SET(x) (((x) << MAC_PCU_SLP32_MODE_HALF_CLK_LATENCY_LSB) & MAC_PCU_SLP32_MODE_HALF_CLK_LATENCY_MASK)

#define MAC_PCU_SLP32_WAKE_ADDRESS               0x000000f8
#define MAC_PCU_SLP32_WAKE_OFFSET                0x000000f8
#define MAC_PCU_SLP32_WAKE_XTL_TIME_MSB          15
#define MAC_PCU_SLP32_WAKE_XTL_TIME_LSB          0
#define MAC_PCU_SLP32_WAKE_XTL_TIME_MASK         0x0000ffff
#define MAC_PCU_SLP32_WAKE_XTL_TIME_GET(x)       (((x) & MAC_PCU_SLP32_WAKE_XTL_TIME_MASK) >> MAC_PCU_SLP32_WAKE_XTL_TIME_LSB)
#define MAC_PCU_SLP32_WAKE_XTL_TIME_SET(x)       (((x) << MAC_PCU_SLP32_WAKE_XTL_TIME_LSB) & MAC_PCU_SLP32_WAKE_XTL_TIME_MASK)

#define MAC_PCU_SLP32_INC_ADDRESS                0x000000fc
#define MAC_PCU_SLP32_INC_OFFSET                 0x000000fc
#define MAC_PCU_SLP32_INC_TSF_INC_MSB            19
#define MAC_PCU_SLP32_INC_TSF_INC_LSB            0
#define MAC_PCU_SLP32_INC_TSF_INC_MASK           0x000fffff
#define MAC_PCU_SLP32_INC_TSF_INC_GET(x)         (((x) & MAC_PCU_SLP32_INC_TSF_INC_MASK) >> MAC_PCU_SLP32_INC_TSF_INC_LSB)
#define MAC_PCU_SLP32_INC_TSF_INC_SET(x)         (((x) << MAC_PCU_SLP32_INC_TSF_INC_LSB) & MAC_PCU_SLP32_INC_TSF_INC_MASK)

#define MAC_PCU_SLP_MIB1_ADDRESS                 0x00000100
#define MAC_PCU_SLP_MIB1_OFFSET                  0x00000100
#define MAC_PCU_SLP_MIB1_SLEEP_CNT_MSB           31
#define MAC_PCU_SLP_MIB1_SLEEP_CNT_LSB           0
#define MAC_PCU_SLP_MIB1_SLEEP_CNT_MASK          0xffffffff
#define MAC_PCU_SLP_MIB1_SLEEP_CNT_GET(x)        (((x) & MAC_PCU_SLP_MIB1_SLEEP_CNT_MASK) >> MAC_PCU_SLP_MIB1_SLEEP_CNT_LSB)
#define MAC_PCU_SLP_MIB1_SLEEP_CNT_SET(x)        (((x) << MAC_PCU_SLP_MIB1_SLEEP_CNT_LSB) & MAC_PCU_SLP_MIB1_SLEEP_CNT_MASK)

#define MAC_PCU_SLP_MIB2_ADDRESS                 0x00000104
#define MAC_PCU_SLP_MIB2_OFFSET                  0x00000104
#define MAC_PCU_SLP_MIB2_CYCLE_CNT_MSB           31
#define MAC_PCU_SLP_MIB2_CYCLE_CNT_LSB           0
#define MAC_PCU_SLP_MIB2_CYCLE_CNT_MASK          0xffffffff
#define MAC_PCU_SLP_MIB2_CYCLE_CNT_GET(x)        (((x) & MAC_PCU_SLP_MIB2_CYCLE_CNT_MASK) >> MAC_PCU_SLP_MIB2_CYCLE_CNT_LSB)
#define MAC_PCU_SLP_MIB2_CYCLE_CNT_SET(x)        (((x) << MAC_PCU_SLP_MIB2_CYCLE_CNT_LSB) & MAC_PCU_SLP_MIB2_CYCLE_CNT_MASK)

#define MAC_PCU_SLP_MIB3_ADDRESS                 0x00000108
#define MAC_PCU_SLP_MIB3_OFFSET                  0x00000108
#define MAC_PCU_SLP_MIB3_PENDING_MSB             1
#define MAC_PCU_SLP_MIB3_PENDING_LSB             1
#define MAC_PCU_SLP_MIB3_PENDING_MASK            0x00000002
#define MAC_PCU_SLP_MIB3_PENDING_GET(x)          (((x) & MAC_PCU_SLP_MIB3_PENDING_MASK) >> MAC_PCU_SLP_MIB3_PENDING_LSB)
#define MAC_PCU_SLP_MIB3_PENDING_SET(x)          (((x) << MAC_PCU_SLP_MIB3_PENDING_LSB) & MAC_PCU_SLP_MIB3_PENDING_MASK)
#define MAC_PCU_SLP_MIB3_CLR_CNT_MSB             0
#define MAC_PCU_SLP_MIB3_CLR_CNT_LSB             0
#define MAC_PCU_SLP_MIB3_CLR_CNT_MASK            0x00000001
#define MAC_PCU_SLP_MIB3_CLR_CNT_GET(x)          (((x) & MAC_PCU_SLP_MIB3_CLR_CNT_MASK) >> MAC_PCU_SLP_MIB3_CLR_CNT_LSB)
#define MAC_PCU_SLP_MIB3_CLR_CNT_SET(x)          (((x) << MAC_PCU_SLP_MIB3_CLR_CNT_LSB) & MAC_PCU_SLP_MIB3_CLR_CNT_MASK)

#define MAC_PCU_SLP_BEACON_ADDRESS               0x0000010c
#define MAC_PCU_SLP_BEACON_OFFSET                0x0000010c
#define MAC_PCU_SLP_BEACON_BMISS_TIMEOUT_ENABLE_MSB 24
#define MAC_PCU_SLP_BEACON_BMISS_TIMEOUT_ENABLE_LSB 24
#define MAC_PCU_SLP_BEACON_BMISS_TIMEOUT_ENABLE_MASK 0x01000000
#define MAC_PCU_SLP_BEACON_BMISS_TIMEOUT_ENABLE_GET(x) (((x) & MAC_PCU_SLP_BEACON_BMISS_TIMEOUT_ENABLE_MASK) >> MAC_PCU_SLP_BEACON_BMISS_TIMEOUT_ENABLE_LSB)
#define MAC_PCU_SLP_BEACON_BMISS_TIMEOUT_ENABLE_SET(x) (((x) << MAC_PCU_SLP_BEACON_BMISS_TIMEOUT_ENABLE_LSB) & MAC_PCU_SLP_BEACON_BMISS_TIMEOUT_ENABLE_MASK)
#define MAC_PCU_SLP_BEACON_BMISS_TIMEOUT_MSB     23
#define MAC_PCU_SLP_BEACON_BMISS_TIMEOUT_LSB     0
#define MAC_PCU_SLP_BEACON_BMISS_TIMEOUT_MASK    0x00ffffff
#define MAC_PCU_SLP_BEACON_BMISS_TIMEOUT_GET(x)  (((x) & MAC_PCU_SLP_BEACON_BMISS_TIMEOUT_MASK) >> MAC_PCU_SLP_BEACON_BMISS_TIMEOUT_LSB)
#define MAC_PCU_SLP_BEACON_BMISS_TIMEOUT_SET(x)  (((x) << MAC_PCU_SLP_BEACON_BMISS_TIMEOUT_LSB) & MAC_PCU_SLP_BEACON_BMISS_TIMEOUT_MASK)

#define POWER_REG_ADDRESS                        0x00000110
#define POWER_REG_OFFSET                         0x00000110
#define POWER_REG_VLVL_MSB                       11
#define POWER_REG_VLVL_LSB                       8
#define POWER_REG_VLVL_MASK                      0x00000f00
#define POWER_REG_VLVL_GET(x)                    (((x) & POWER_REG_VLVL_MASK) >> POWER_REG_VLVL_LSB)
#define POWER_REG_VLVL_SET(x)                    (((x) << POWER_REG_VLVL_LSB) & POWER_REG_VLVL_MASK)
#define POWER_REG_CPU_INT_ENABLE_MSB             7
#define POWER_REG_CPU_INT_ENABLE_LSB             7
#define POWER_REG_CPU_INT_ENABLE_MASK            0x00000080
#define POWER_REG_CPU_INT_ENABLE_GET(x)          (((x) & POWER_REG_CPU_INT_ENABLE_MASK) >> POWER_REG_CPU_INT_ENABLE_LSB)
#define POWER_REG_CPU_INT_ENABLE_SET(x)          (((x) << POWER_REG_CPU_INT_ENABLE_LSB) & POWER_REG_CPU_INT_ENABLE_MASK)
#define POWER_REG_WLAN_ISO_DIS_MSB               6
#define POWER_REG_WLAN_ISO_DIS_LSB               6
#define POWER_REG_WLAN_ISO_DIS_MASK              0x00000040
#define POWER_REG_WLAN_ISO_DIS_GET(x)            (((x) & POWER_REG_WLAN_ISO_DIS_MASK) >> POWER_REG_WLAN_ISO_DIS_LSB)
#define POWER_REG_WLAN_ISO_DIS_SET(x)            (((x) << POWER_REG_WLAN_ISO_DIS_LSB) & POWER_REG_WLAN_ISO_DIS_MASK)
#define POWER_REG_WLAN_ISO_CNTL_MSB              5
#define POWER_REG_WLAN_ISO_CNTL_LSB              5
#define POWER_REG_WLAN_ISO_CNTL_MASK             0x00000020
#define POWER_REG_WLAN_ISO_CNTL_GET(x)           (((x) & POWER_REG_WLAN_ISO_CNTL_MASK) >> POWER_REG_WLAN_ISO_CNTL_LSB)
#define POWER_REG_WLAN_ISO_CNTL_SET(x)           (((x) << POWER_REG_WLAN_ISO_CNTL_LSB) & POWER_REG_WLAN_ISO_CNTL_MASK)
#define POWER_REG_RADIO_PWD_EN_MSB               4
#define POWER_REG_RADIO_PWD_EN_LSB               4
#define POWER_REG_RADIO_PWD_EN_MASK              0x00000010
#define POWER_REG_RADIO_PWD_EN_GET(x)            (((x) & POWER_REG_RADIO_PWD_EN_MASK) >> POWER_REG_RADIO_PWD_EN_LSB)
#define POWER_REG_RADIO_PWD_EN_SET(x)            (((x) << POWER_REG_RADIO_PWD_EN_LSB) & POWER_REG_RADIO_PWD_EN_MASK)
#define POWER_REG_SOC_SCALE_EN_MSB               3
#define POWER_REG_SOC_SCALE_EN_LSB               3
#define POWER_REG_SOC_SCALE_EN_MASK              0x00000008
#define POWER_REG_SOC_SCALE_EN_GET(x)            (((x) & POWER_REG_SOC_SCALE_EN_MASK) >> POWER_REG_SOC_SCALE_EN_LSB)
#define POWER_REG_SOC_SCALE_EN_SET(x)            (((x) << POWER_REG_SOC_SCALE_EN_LSB) & POWER_REG_SOC_SCALE_EN_MASK)
#define POWER_REG_WLAN_SCALE_EN_MSB              2
#define POWER_REG_WLAN_SCALE_EN_LSB              2
#define POWER_REG_WLAN_SCALE_EN_MASK             0x00000004
#define POWER_REG_WLAN_SCALE_EN_GET(x)           (((x) & POWER_REG_WLAN_SCALE_EN_MASK) >> POWER_REG_WLAN_SCALE_EN_LSB)
#define POWER_REG_WLAN_SCALE_EN_SET(x)           (((x) << POWER_REG_WLAN_SCALE_EN_LSB) & POWER_REG_WLAN_SCALE_EN_MASK)
#define POWER_REG_WLAN_PWD_EN_MSB                1
#define POWER_REG_WLAN_PWD_EN_LSB                1
#define POWER_REG_WLAN_PWD_EN_MASK               0x00000002
#define POWER_REG_WLAN_PWD_EN_GET(x)             (((x) & POWER_REG_WLAN_PWD_EN_MASK) >> POWER_REG_WLAN_PWD_EN_LSB)
#define POWER_REG_WLAN_PWD_EN_SET(x)             (((x) << POWER_REG_WLAN_PWD_EN_LSB) & POWER_REG_WLAN_PWD_EN_MASK)
#define POWER_REG_POWER_EN_MSB                   0
#define POWER_REG_POWER_EN_LSB                   0
#define POWER_REG_POWER_EN_MASK                  0x00000001
#define POWER_REG_POWER_EN_GET(x)                (((x) & POWER_REG_POWER_EN_MASK) >> POWER_REG_POWER_EN_LSB)
#define POWER_REG_POWER_EN_SET(x)                (((x) << POWER_REG_POWER_EN_LSB) & POWER_REG_POWER_EN_MASK)

#define CORE_CLK_CTRL_ADDRESS                    0x00000114
#define CORE_CLK_CTRL_OFFSET                     0x00000114
#define CORE_CLK_CTRL_DIV_MSB                    2
#define CORE_CLK_CTRL_DIV_LSB                    0
#define CORE_CLK_CTRL_DIV_MASK                   0x00000007
#define CORE_CLK_CTRL_DIV_GET(x)                 (((x) & CORE_CLK_CTRL_DIV_MASK) >> CORE_CLK_CTRL_DIV_LSB)
#define CORE_CLK_CTRL_DIV_SET(x)                 (((x) << CORE_CLK_CTRL_DIV_LSB) & CORE_CLK_CTRL_DIV_MASK)

#define SDIO_SETUP_CIRCUIT_ADDRESS               0x00000120
#define SDIO_SETUP_CIRCUIT_OFFSET                0x00000120
#define SDIO_SETUP_CIRCUIT_VECTOR_MSB            7
#define SDIO_SETUP_CIRCUIT_VECTOR_LSB            0
#define SDIO_SETUP_CIRCUIT_VECTOR_MASK           0x000000ff
#define SDIO_SETUP_CIRCUIT_VECTOR_GET(x)         (((x) & SDIO_SETUP_CIRCUIT_VECTOR_MASK) >> SDIO_SETUP_CIRCUIT_VECTOR_LSB)
#define SDIO_SETUP_CIRCUIT_VECTOR_SET(x)         (((x) << SDIO_SETUP_CIRCUIT_VECTOR_LSB) & SDIO_SETUP_CIRCUIT_VECTOR_MASK)

#define SDIO_SETUP_CONFIG_ADDRESS                0x00000140
#define SDIO_SETUP_CONFIG_OFFSET                 0x00000140
#define SDIO_SETUP_CONFIG_ENABLE_MSB             1
#define SDIO_SETUP_CONFIG_ENABLE_LSB             1
#define SDIO_SETUP_CONFIG_ENABLE_MASK            0x00000002
#define SDIO_SETUP_CONFIG_ENABLE_GET(x)          (((x) & SDIO_SETUP_CONFIG_ENABLE_MASK) >> SDIO_SETUP_CONFIG_ENABLE_LSB)
#define SDIO_SETUP_CONFIG_ENABLE_SET(x)          (((x) << SDIO_SETUP_CONFIG_ENABLE_LSB) & SDIO_SETUP_CONFIG_ENABLE_MASK)
#define SDIO_SETUP_CONFIG_CLEAR_MSB              0
#define SDIO_SETUP_CONFIG_CLEAR_LSB              0
#define SDIO_SETUP_CONFIG_CLEAR_MASK             0x00000001
#define SDIO_SETUP_CONFIG_CLEAR_GET(x)           (((x) & SDIO_SETUP_CONFIG_CLEAR_MASK) >> SDIO_SETUP_CONFIG_CLEAR_LSB)
#define SDIO_SETUP_CONFIG_CLEAR_SET(x)           (((x) << SDIO_SETUP_CONFIG_CLEAR_LSB) & SDIO_SETUP_CONFIG_CLEAR_MASK)

#define CPU_SETUP_CONFIG_ADDRESS                 0x00000144
#define CPU_SETUP_CONFIG_OFFSET                  0x00000144
#define CPU_SETUP_CONFIG_ENABLE_MSB              1
#define CPU_SETUP_CONFIG_ENABLE_LSB              1
#define CPU_SETUP_CONFIG_ENABLE_MASK             0x00000002
#define CPU_SETUP_CONFIG_ENABLE_GET(x)           (((x) & CPU_SETUP_CONFIG_ENABLE_MASK) >> CPU_SETUP_CONFIG_ENABLE_LSB)
#define CPU_SETUP_CONFIG_ENABLE_SET(x)           (((x) << CPU_SETUP_CONFIG_ENABLE_LSB) & CPU_SETUP_CONFIG_ENABLE_MASK)
#define CPU_SETUP_CONFIG_CLEAR_MSB               0
#define CPU_SETUP_CONFIG_CLEAR_LSB               0
#define CPU_SETUP_CONFIG_CLEAR_MASK              0x00000001
#define CPU_SETUP_CONFIG_CLEAR_GET(x)            (((x) & CPU_SETUP_CONFIG_CLEAR_MASK) >> CPU_SETUP_CONFIG_CLEAR_LSB)
#define CPU_SETUP_CONFIG_CLEAR_SET(x)            (((x) << CPU_SETUP_CONFIG_CLEAR_LSB) & CPU_SETUP_CONFIG_CLEAR_MASK)

#define CPU_SETUP_CIRCUIT_ADDRESS                0x00000160
#define CPU_SETUP_CIRCUIT_OFFSET                 0x00000160
#define CPU_SETUP_CIRCUIT_VECTOR_MSB             7
#define CPU_SETUP_CIRCUIT_VECTOR_LSB             0
#define CPU_SETUP_CIRCUIT_VECTOR_MASK            0x000000ff
#define CPU_SETUP_CIRCUIT_VECTOR_GET(x)          (((x) & CPU_SETUP_CIRCUIT_VECTOR_MASK) >> CPU_SETUP_CIRCUIT_VECTOR_LSB)
#define CPU_SETUP_CIRCUIT_VECTOR_SET(x)          (((x) << CPU_SETUP_CIRCUIT_VECTOR_LSB) & CPU_SETUP_CIRCUIT_VECTOR_MASK)

#define BB_SETUP_CONFIG_ADDRESS                  0x00000180
#define BB_SETUP_CONFIG_OFFSET                   0x00000180
#define BB_SETUP_CONFIG_ENABLE_MSB               1
#define BB_SETUP_CONFIG_ENABLE_LSB               1
#define BB_SETUP_CONFIG_ENABLE_MASK              0x00000002
#define BB_SETUP_CONFIG_ENABLE_GET(x)            (((x) & BB_SETUP_CONFIG_ENABLE_MASK) >> BB_SETUP_CONFIG_ENABLE_LSB)
#define BB_SETUP_CONFIG_ENABLE_SET(x)            (((x) << BB_SETUP_CONFIG_ENABLE_LSB) & BB_SETUP_CONFIG_ENABLE_MASK)
#define BB_SETUP_CONFIG_CLEAR_MSB                0
#define BB_SETUP_CONFIG_CLEAR_LSB                0
#define BB_SETUP_CONFIG_CLEAR_MASK               0x00000001
#define BB_SETUP_CONFIG_CLEAR_GET(x)             (((x) & BB_SETUP_CONFIG_CLEAR_MASK) >> BB_SETUP_CONFIG_CLEAR_LSB)
#define BB_SETUP_CONFIG_CLEAR_SET(x)             (((x) << BB_SETUP_CONFIG_CLEAR_LSB) & BB_SETUP_CONFIG_CLEAR_MASK)

#define BB_SETUP_CIRCUIT_ADDRESS                 0x000001a0
#define BB_SETUP_CIRCUIT_OFFSET                  0x000001a0
#define BB_SETUP_CIRCUIT_VECTOR_MSB              7
#define BB_SETUP_CIRCUIT_VECTOR_LSB              0
#define BB_SETUP_CIRCUIT_VECTOR_MASK             0x000000ff
#define BB_SETUP_CIRCUIT_VECTOR_GET(x)           (((x) & BB_SETUP_CIRCUIT_VECTOR_MASK) >> BB_SETUP_CIRCUIT_VECTOR_LSB)
#define BB_SETUP_CIRCUIT_VECTOR_SET(x)           (((x) << BB_SETUP_CIRCUIT_VECTOR_LSB) & BB_SETUP_CIRCUIT_VECTOR_MASK)

#define GPIO_WAKEUP_CONTROL_ADDRESS              0x000001c0
#define GPIO_WAKEUP_CONTROL_OFFSET               0x000001c0
#define GPIO_WAKEUP_CONTROL_ENABLE_MSB           0
#define GPIO_WAKEUP_CONTROL_ENABLE_LSB           0
#define GPIO_WAKEUP_CONTROL_ENABLE_MASK          0x00000001
#define GPIO_WAKEUP_CONTROL_ENABLE_GET(x)        (((x) & GPIO_WAKEUP_CONTROL_ENABLE_MASK) >> GPIO_WAKEUP_CONTROL_ENABLE_LSB)
#define GPIO_WAKEUP_CONTROL_ENABLE_SET(x)        (((x) << GPIO_WAKEUP_CONTROL_ENABLE_LSB) & GPIO_WAKEUP_CONTROL_ENABLE_MASK)


#ifndef __ASSEMBLER__

typedef struct rtc_reg_reg_s {
  volatile unsigned int reset_control;
  volatile unsigned int xtal_control;
  volatile unsigned int tcxo_detect;
  volatile unsigned int xtal_test;
  volatile unsigned int quadrature;
  volatile unsigned int pll_control;
  volatile unsigned int pll_settle;
  volatile unsigned int xtal_settle;
  volatile unsigned int cpu_clock;
  volatile unsigned int clock_out;
  volatile unsigned int clock_control;
  volatile unsigned int bias_override;
  volatile unsigned int wdt_control;
  volatile unsigned int wdt_status;
  volatile unsigned int wdt;
  volatile unsigned int wdt_count;
  volatile unsigned int wdt_reset;
  volatile unsigned int int_status;
  volatile unsigned int lf_timer0;
  volatile unsigned int lf_timer_count0;
  volatile unsigned int lf_timer_control0;
  volatile unsigned int lf_timer_status0;
  volatile unsigned int lf_timer1;
  volatile unsigned int lf_timer_count1;
  volatile unsigned int lf_timer_control1;
  volatile unsigned int lf_timer_status1;
  volatile unsigned int lf_timer2;
  volatile unsigned int lf_timer_count2;
  volatile unsigned int lf_timer_control2;
  volatile unsigned int lf_timer_status2;
  volatile unsigned int lf_timer3;
  volatile unsigned int lf_timer_count3;
  volatile unsigned int lf_timer_control3;
  volatile unsigned int lf_timer_status3;
  volatile unsigned int hf_timer;
  volatile unsigned int hf_timer_count;
  volatile unsigned int hf_lf_count;
  volatile unsigned int hf_timer_control;
  volatile unsigned int hf_timer_status;
  volatile unsigned int rtc_control;
  volatile unsigned int rtc_time;
  volatile unsigned int rtc_date;
  volatile unsigned int rtc_set_time;
  volatile unsigned int rtc_set_date;
  volatile unsigned int rtc_set_alarm;
  volatile unsigned int rtc_config;
  volatile unsigned int rtc_alarm_status;
  volatile unsigned int uart_wakeup;
  volatile unsigned int reset_cause;
  volatile unsigned int system_sleep;
  volatile unsigned int sdio_wrapper;
  volatile unsigned int mac_sleep_control;
  volatile unsigned int keep_awake;
  volatile unsigned int lpo_cal_time;
  volatile unsigned int lpo_init_dividend_int;
  volatile unsigned int lpo_init_dividend_fraction;
  volatile unsigned int lpo_cal;
  volatile unsigned int lpo_cal_test_control;
  volatile unsigned int lpo_cal_test_status;
  volatile unsigned int chip_id;
  volatile unsigned int derived_rtc_clk;
  volatile unsigned int mac_pcu_slp32_mode;
  volatile unsigned int mac_pcu_slp32_wake;
  volatile unsigned int mac_pcu_slp32_inc;
  volatile unsigned int mac_pcu_slp_mib1;
  volatile unsigned int mac_pcu_slp_mib2;
  volatile unsigned int mac_pcu_slp_mib3;
  volatile unsigned int mac_pcu_slp_beacon;
  volatile unsigned int power_reg;
  volatile unsigned int core_clk_ctrl;
  unsigned char pad0[8]; /* pad to 0x120 */
  volatile unsigned int sdio_setup_circuit[8];
  volatile unsigned int sdio_setup_config;
  volatile unsigned int cpu_setup_config;
  unsigned char pad1[24]; /* pad to 0x160 */
  volatile unsigned int cpu_setup_circuit[8];
  volatile unsigned int bb_setup_config;
  unsigned char pad2[28]; /* pad to 0x1a0 */
  volatile unsigned int bb_setup_circuit[8];
  volatile unsigned int gpio_wakeup_control;
} rtc_reg_reg_t;

#endif /* __ASSEMBLER__ */

#endif /* _RTC_REG_H_ */
