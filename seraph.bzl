load(":image_opts.bzl", "boot_image_opts")
load(":msm_kernel_la.bzl", "define_msm_la")
load(":target_variants.bzl", "la_variants")

target_name = "seraph"

def define_seraph():
    _seraph_in_tree_modules = [
        # keep sorted
        # TODO: Need to add GKI modules
        "drivers/bus/mhi/devices/mhi_dev_uci.ko",
        "drivers/bus/mhi/host/mhi.ko",
        "drivers/clk/qcom/camcc-seraph.ko",
        "drivers/clk/qcom/clk-dummy.ko",
        "drivers/clk/qcom/clk-qcom.ko",
        "drivers/clk/qcom/evacc-seraph.ko",
        "drivers/clk/qcom/gcc-seraph.ko",
        "drivers/clk/qcom/gdsc-regulator.ko",
        "drivers/clk/qcom/gpucc-seraph.ko",
        "drivers/clk/qcom/lsrcc-seraph.ko",
        "drivers/clk/qcom/tcsrcc-seraph.ko",
        "drivers/clk/qcom/videocc-seraph.ko",
        "drivers/dma-buf/heaps/qcom_dma_heaps.ko",
        "drivers/firmware/qcom-scm.ko",
        "drivers/hwspinlock/qcom_hwspinlock.ko",
        "drivers/interconnect/icc-test.ko",
        "drivers/interconnect/qcom/icc-bcm-voter.ko",
        "drivers/interconnect/qcom/icc-debug.ko",
        "drivers/interconnect/qcom/icc-rpmh.ko",
        "drivers/interconnect/qcom/qnoc-qos.ko",
        "drivers/interconnect/qcom/qnoc-seraph.ko",
        "drivers/iommu/arm/arm-smmu/arm_smmu.ko",
        "drivers/iommu/iommu-logger.ko",
        "drivers/iommu/msm_dma_iommu_mapping.ko",
        "drivers/iommu/qcom_iommu_debug.ko",
        "drivers/iommu/qcom_iommu_util.ko",
        "drivers/irqchip/qcom-pdc.ko",
        "drivers/mailbox/msm_qmp.ko",
        "drivers/mailbox/qcom-ipcc.ko",
        "drivers/mmc/host/cqhci.ko",
        "drivers/mmc/host/sdhci-msm.ko",
        "drivers/nvmem/nvmem_qfprom.ko",
        "drivers/pci/controller/pci-msm-drv.ko",
        "drivers/pinctrl/qcom/pinctrl-msm.ko",
        "drivers/pinctrl/qcom/pinctrl-seraph.ko",
        "drivers/regulator/stub-regulator.ko",
        "drivers/remoteproc/qcom_pil_info.ko",
        "drivers/remoteproc/qcom_q6v5.ko",
        "drivers/remoteproc/qcom_q6v5_pas.ko",
        "drivers/remoteproc/qcom_sysmon.ko",
        "drivers/remoteproc/rproc_qcom_common.ko",
        "drivers/rpmsg/qcom_smd.ko",
        "drivers/soc/qcom/cmd-db.ko",
        "drivers/soc/qcom/core_hang_detect.ko",
        "drivers/soc/qcom/mdt_loader.ko",
        "drivers/soc/qcom/mem-hooks.ko",
        "drivers/soc/qcom/mem-offline.ko",
        "drivers/soc/qcom/mem_buf/mem_buf.ko",
        "drivers/soc/qcom/mem_buf/mem_buf_dev.ko",
        "drivers/soc/qcom/pdr_interface.ko",
        "drivers/soc/qcom/qcom_rpmh.ko",
        "drivers/soc/qcom/qcom_wdt_core.ko",
        "drivers/soc/qcom/qmi_helpers.ko",
        "drivers/soc/qcom/smem.ko",
        "drivers/soc/qcom/socinfo.ko",
        "drivers/soc/qcom/tmecom/tmecom-intf.ko",
        "drivers/usb/dwc3/dwc3-msm.ko",
        "drivers/usb/phy/phy-generic.ko",
        "drivers/usb/phy/phy-qcom-emu.ko",
        "drivers/virt/gunyah/gh_virt_wdt.ko",
        "net/mac80211/mac80211.ko",
        "net/wireless/cfg80211.ko",
    ]

    _seraph_consolidate_in_tree_modules = _seraph_in_tree_modules + [
        # keep sorted
        "drivers/misc/lkdtm/lkdtm.ko",
        "kernel/locking/locktorture.ko",
        "kernel/rcu/rcutorture.ko",
        "kernel/torture.ko",
        "lib/atomic64_test.ko",
        "lib/test_user_copy.ko",
    ]

    kernel_vendor_cmdline_extras = [
        # do not sort
        "console=ttyMSM0,115200n8",
        "qcom_geni_serial.con_enabled=1",
        "bootconfig",
    ]

    board_kernel_cmdline_extras = []
    board_bootconfig_extras = []

    for variant in la_variants:
        if variant == "consolidate":
            mod_list = _seraph_consolidate_in_tree_modules
        else:
            mod_list = _seraph_in_tree_modules
            board_kernel_cmdline_extras += ["nosoftlockup"]
            kernel_vendor_cmdline_extras += ["nosoftlockup"]
            board_bootconfig_extras += ["androidboot.console=0"]

        define_msm_la(
            msm_target = target_name,
            variant = variant,
            in_tree_module_list = mod_list,
            boot_image_opts = boot_image_opts(
                earlycon_addr = "qcom_geni,0xa94000",
                kernel_vendor_cmdline_extras = kernel_vendor_cmdline_extras,
                board_kernel_cmdline_extras = board_kernel_cmdline_extras,
                board_bootconfig_extras = board_bootconfig_extras,
            ),
        )
