load(":image_opts.bzl", "boot_image_opts")
load(":msm_kernel_la.bzl", "define_msm_la")
load(":target_variants.bzl", "la_variants")

target_name = "neo-la"

def define_neo_la():
    _neo_in_tree_modules = [
        # keep sorted
        "drivers/clk/qcom/camcc-neo.ko",
        "drivers/clk/qcom/clk-dummy.ko",
        "drivers/clk/qcom/clk-qcom.ko",
        "drivers/clk/qcom/clk-rpmh.ko",
        "drivers/clk/qcom/debugcc-neo.ko",
        "drivers/clk/qcom/dispcc-neo.ko",
        "drivers/clk/qcom/gcc-neo.ko",
        "drivers/clk/qcom/gdsc-regulator.ko",
        "drivers/clk/qcom/gpucc-neo.ko",
        "drivers/clk/qcom/tcsrcc-neo.ko",
        "drivers/clk/qcom/videocc-neo.ko",
        "drivers/cpufreq/qcom-cpufreq-hw.ko",
        "drivers/cpufreq/qcom-cpufreq-hw-debug.ko",
        "drivers/cpuidle/governors/qcom_lpm.ko",
        "drivers/dma-buf/heaps/qcom_dma_heaps.ko",
        "drivers/dma/qcom/msm_gpi.ko",
        "drivers/edac/kryo_arm64_edac.ko",
        "drivers/edac/qcom_edac.ko",
        "drivers/firmware/qcom-scm.ko",
        "drivers/hwspinlock/qcom_hwspinlock.ko",
        "drivers/i2c/busses/i2c-msm-geni.ko",
        "drivers/interconnect/qcom/icc-bcm-voter.ko",
        "drivers/interconnect/qcom/icc-debug.ko",
        "drivers/interconnect/qcom/icc-rpmh.ko",
        "drivers/interconnect/qcom/qnoc-neo.ko",
        "drivers/interconnect/qcom/qnoc-qos.ko",
        "drivers/iommu/arm/arm-smmu/arm_smmu.ko",
        "drivers/iommu/iommu-logger.ko",
        "drivers/iommu/msm_dma_iommu_mapping.ko",
        "drivers/iommu/qcom_iommu_debug.ko",
        "drivers/iommu/qcom_iommu_util.ko",
        "drivers/irqchip/msm_show_resume_irq.ko",
        "drivers/irqchip/qcom-pdc.ko",
        "drivers/mfd/qcom-i2c-pmic.ko",
        "drivers/perf/qcom_llcc_pmu.ko",
        "drivers/pinctrl/qcom/pinctrl-msm.ko",
        "drivers/pinctrl/qcom/pinctrl-neo.ko",
        "drivers/power/reset/qcom-dload-mode.ko",
        "drivers/power/reset/qcom-reboot-reason.ko",
        "drivers/regulator/debug-regulator.ko",
        "drivers/regulator/proxy-consumer.ko",
        "drivers/regulator/qcom_pm8008-regulator.ko",
        "drivers/regulator/qti-fixed-regulator.ko",
        "drivers/regulator/rpmh-regulator.ko",
        "drivers/soc/qcom/boot_stats.ko",
        "drivers/soc/qcom/cmd-db.ko",
        "drivers/soc/qcom/core_hang_detect.ko",
        "drivers/soc/qcom/llcc-qcom.ko",
        "drivers/soc/qcom/llcc_perfmon.ko",
        "drivers/soc/qcom/mem-hooks.ko",
        "drivers/soc/qcom/mem_buf/mem_buf.ko",
        "drivers/soc/qcom/mem_buf/mem_buf_dev.ko",
        "drivers/soc/qcom/memory_dump_v2.ko",
        "drivers/soc/qcom/msm_performance.ko",
        "drivers/soc/qcom/qcom_cpuss_sleep_stats.ko",
        "drivers/soc/qcom/qcom_ramdump.ko",
        "drivers/soc/qcom/qcom_rpmh.ko",
        "drivers/soc/qcom/qcom_soc_wdt.ko",
        "drivers/soc/qcom/qcom_stats.ko",
        "drivers/soc/qcom/qcom_wdt_core.ko",
        "drivers/soc/qcom/rq_stats.ko",
        "drivers/soc/qcom/secure_buffer.ko",
        "drivers/soc/qcom/smem.ko",
        "drivers/soc/qcom/socinfo.ko",
        "drivers/spi/spi-msm-geni.ko",
        "drivers/tty/serial/msm_geni_serial.ko",
        "drivers/usb/dwc3/dwc3-msm.ko",
        "drivers/usb/gadget/function/usb_f_diag.ko",
        "drivers/usb/gadget/function/usb_f_qdss.ko",
        "drivers/usb/phy/phy-msm-snps-eusb2.ko",
        "drivers/usb/phy/phy-msm-ssusb-qmp.ko",
        "drivers/usb/repeater/repeater.ko",
        "drivers/usb/repeater/repeater-i2c-eusb2.ko",
        "drivers/virt/gunyah/gh_msgq.ko",
        "drivers/virt/gunyah/gh_rm_drv.ko",
        "drivers/virt/gunyah/gh_virt_wdt.ko",
        "kernel/msm_sysstats.ko",
        "kernel/sched/walt/sched-walt.ko",
    ]

    _neo_consolidate_in_tree_modules = _neo_in_tree_modules + [
        # keep sorted
        "drivers/misc/lkdtm/lkdtm.ko",
        "kernel/sched/walt/sched-walt-debug.ko",
    ]

    kernel_vendor_cmdline_extras = [
        # do not sort
        "console=ttyMSM0,115200n8",
        "qcom_geni_serial.con_enabled=1",
        "bootconfig",
    ]

    for variant in la_variants:
        board_kernel_cmdline_extras = []
        board_bootconfig_extras = []

        if variant == "consolidate":
            mod_list = _neo_consolidate_in_tree_modules
        else:
            mod_list = _neo_in_tree_modules
            board_kernel_cmdline_extras += ["nosoftlockup"]
            kernel_vendor_cmdline_extras += ["nosoftlockup"]
            board_bootconfig_extras += ["androidboot.console=0"]

        define_msm_la(
            msm_target = target_name,
            variant = variant,
            in_tree_module_list = mod_list,
            boot_image_opts = boot_image_opts(
                earlycon_addr = "qcom_geni,0x00a94000",
                kernel_vendor_cmdline_extras = kernel_vendor_cmdline_extras,
                board_kernel_cmdline_extras = board_kernel_cmdline_extras,
                board_bootconfig_extras = board_bootconfig_extras,
            ),
            #TODO: Need to enable this
            #dpm_overlay = True,
        )
