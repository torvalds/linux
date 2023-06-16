load(":target_variants.bzl", "la_variants")
load(":msm_kernel_la.bzl", "define_msm_la")
load(":image_opts.bzl", "boot_image_opts")

target_name = "kalama"

def define_kalama():
    _kalama_in_tree_modules = [
        # keep sorted
        "arch/arm64/gunyah/gh_arm_drv.ko",
        "drivers/clk/qcom/camcc-kalama.ko",
        "drivers/clk/qcom/clk-dummy.ko",
        "drivers/clk/qcom/clk-qcom.ko",
        "drivers/clk/qcom/clk-rpmh.ko",
        "drivers/clk/qcom/debugcc-kalama.ko",
        "drivers/clk/qcom/dispcc-kalama.ko",
        "drivers/clk/qcom/gcc-kalama.ko",
        "drivers/clk/qcom/gdsc-regulator.ko",
        "drivers/clk/qcom/gpucc-kalama.ko",
        "drivers/clk/qcom/tcsrcc-kalama.ko",
        "drivers/clk/qcom/videocc-kalama.ko",
        "drivers/cpufreq/qcom-cpufreq-hw.ko",
        "drivers/cpufreq/qcom-cpufreq-hw-debug.ko",
        "drivers/cpuidle/governors/qcom_lpm.ko",
        "drivers/dma-buf/heaps/qcom_dma_heaps.ko",
        "drivers/firmware/qcom-scm.ko",
        "drivers/hwspinlock/qcom_hwspinlock.ko",
        "drivers/interconnect/icc-test.ko",
        "drivers/interconnect/qcom/icc-bcm-voter.ko",
        "drivers/interconnect/qcom/icc-debug.ko",
        "drivers/interconnect/qcom/icc-rpmh.ko",
        "drivers/interconnect/qcom/qnoc-kalama.ko",
        "drivers/interconnect/qcom/qnoc-qos.ko",
        "drivers/iommu/arm/arm-smmu/arm_smmu.ko",
        "drivers/iommu/msm_dma_iommu_mapping.ko",
        "drivers/iommu/qcom_iommu_util.ko",
        "drivers/irqchip/qcom-pdc.ko",
        "drivers/mailbox/qcom-ipcc.ko",
        "drivers/mfd/qcom-spmi-pmic.ko",
        "drivers/phy/qualcomm/phy-qcom-ufs.ko",
        "drivers/phy/qualcomm/phy-qcom-ufs-qmp-v4.ko",
        "drivers/phy/qualcomm/phy-qcom-ufs-qmp-v4-kalama.ko",
        "drivers/phy/qualcomm/phy-qcom-ufs-qmp-v4-pineapple.ko",
        "drivers/phy/qualcomm/phy-qcom-ufs-qmp-v4-waipio.ko",
        "drivers/pinctrl/qcom/pinctrl-kalama.ko",
        "drivers/pinctrl/qcom/pinctrl-msm.ko",
        "drivers/power/reset/qcom-dload-mode.ko",
        "drivers/power/reset/qcom-reboot-reason.ko",
        "drivers/regulator/debug-regulator.ko",
        "drivers/regulator/proxy-consumer.ko",
        "drivers/regulator/qti-fixed-regulator.ko",
        "drivers/regulator/rpmh-regulator.ko",
        "drivers/regulator/stub-regulator.ko",
        "drivers/soc/qcom/boot_stats.ko",
        "drivers/soc/qcom/cmd-db.ko",
        "drivers/soc/qcom/eud.ko",
        "drivers/soc/qcom/gh_tlmm_vm_mem_access.ko",
        "drivers/soc/qcom/llcc-qcom.ko",
        "drivers/soc/qcom/llcc_perfmon.ko",
        "drivers/soc/qcom/mdt_loader.ko",
        "drivers/soc/qcom/mem_buf/mem_buf.ko",
        "drivers/soc/qcom/mem_buf/mem_buf_dev.ko",
        "drivers/soc/qcom/msm_performance.ko",
        "drivers/soc/qcom/qcom_aoss.ko",
        "drivers/soc/qcom/qcom_rpmh.ko",
        "drivers/soc/qcom/qcom_wdt_core.ko",
        "drivers/soc/qcom/secure_buffer.ko",
        "drivers/soc/qcom/smem.ko",
        "drivers/soc/qcom/smp2p.ko",
        "drivers/soc/qcom/smp2p_sleepstate.ko",
        "drivers/soc/qcom/socinfo.ko",
        "drivers/spmi/spmi-pmic-arb.ko",
        "drivers/tty/hvc/hvc_gunyah.ko",
        "drivers/ufs/host/ufs_qcom.ko",
        "drivers/usb/dwc3/dwc3-msm.ko",
        "drivers/usb/gadget/function/usb_f_ccid.ko",
        "drivers/usb/gadget/function/usb_f_cdev.ko",
        "drivers/usb/gadget/function/usb_f_qdss.ko",
        "drivers/usb/phy/phy-generic.ko",
        "drivers/usb/phy/phy-msm-snps-eusb2.ko",
        "drivers/usb/phy/phy-msm-snps-hs.ko",
        "drivers/usb/phy/phy-msm-ssusb-qmp.ko",
        "drivers/usb/repeater/repeater.ko",
        "drivers/usb/repeater/repeater-qti-pmic-eusb2.ko",
        "drivers/virt/gunyah/gh_ctrl.ko",
        "drivers/virt/gunyah/gh_dbl.ko",
        "drivers/virt/gunyah/gh_irq_lend.ko",
        "drivers/virt/gunyah/gh_mem_notifier.ko",
        "drivers/virt/gunyah/gh_msgq.ko",
        "drivers/virt/gunyah/gh_rm_drv.ko",
        "drivers/virt/gunyah/gh_virt_wdt.ko",
        "drivers/virt/gunyah/gunyah.ko",
        "kernel/trace/qcom_ipc_logging.ko",
    ]

    _kalama_consolidate_in_tree_modules = _kalama_in_tree_modules + [
        # keep sorted
    ]

    for variant in la_variants:
        if variant == "consolidate":
            mod_list = _kalama_consolidate_in_tree_modules
        else:
            mod_list = _kalama_in_tree_modules

        define_msm_la(
            msm_target = target_name,
            variant = variant,
            in_tree_module_list = mod_list,
            boot_image_opts = boot_image_opts(
                earlycon_addr = "qcom_geni,0x00a9C000",
                kernel_vendor_cmdline_extras = [
                    # do not sort
                    "console=ttyMSM0,115200n8",
                    "qcom_geni_serial.con_enabled=1",
                    "bootconfig",
                ],
            ),
        )
