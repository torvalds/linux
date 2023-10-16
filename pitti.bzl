load(":target_variants.bzl", "la_variants")
load(":msm_kernel_la.bzl", "define_msm_la")
load(":image_opts.bzl", "boot_image_opts")

target_name = "pitti"

def define_pitti():
    _pitti_in_tree_modules = [
        # keep sorted
        # TODO: Need to add GKI modules
        "drivers/char/rdbg.ko",
        "drivers/clk/qcom/clk-dummy.ko",
        "drivers/clk/qcom/clk-qcom.ko",
        "drivers/clk/qcom/gcc-pitti.ko",
        "drivers/clk/qcom/gdsc-regulator.ko",
        "drivers/cpuidle/governors/qcom_lpm.ko",
        "drivers/dma-buf/heaps/qcom_dma_heaps.ko",
        "drivers/firmware/qcom-scm.ko",
        "drivers/hwspinlock/qcom_hwspinlock.ko",
        "drivers/iommu/arm/arm-smmu/arm_smmu.ko",
        "drivers/iommu/msm_dma_iommu_mapping.ko",
        "drivers/iommu/qcom_iommu_util.ko",
        "drivers/irqchip/qcom-mpm.ko",
        "drivers/mailbox/msm_qmp.ko",
        "drivers/mailbox/qcom-ipcc.ko",
        "drivers/mmc/host/cqhci.ko",
        "drivers/mmc/host/sdhci-msm.ko",
        "drivers/phy/qualcomm/phy-qcom-ufs.ko",
        "drivers/phy/qualcomm/phy-qcom-ufs-qmp-v4.ko",
        "drivers/phy/qualcomm/phy-qcom-ufs-qrbtc-sdm845.ko",
        "drivers/pinctrl/qcom/pinctrl-msm.ko",
        "drivers/pinctrl/qcom/pinctrl-pitti.ko",
        "drivers/regulator/stub-regulator.ko",
        "drivers/rpmsg/rpm-smd.ko",
        "drivers/slimbus/slimbus.ko",
        "drivers/soc/qcom/mem-hooks.ko",
        "drivers/soc/qcom/mem_buf/mem_buf.ko",
        "drivers/soc/qcom/mem_buf/mem_buf_dev.ko",
        "drivers/soc/qcom/pdr_interface.ko",
        "drivers/soc/qcom/qcom_ramdump.ko",
        "drivers/soc/qcom/qcom_stats.ko",
        "drivers/soc/qcom/qmi_helpers.ko",
        "drivers/soc/qcom/rpm_master_stat.ko",
        "drivers/soc/qcom/smem.ko",
        "drivers/soc/qcom/socinfo.ko",
        "drivers/soc/qcom/wcd_usbss_i2c.ko",
        "drivers/ufs/host/ufs_qcom.ko",
    ]

    _pitti_consolidate_in_tree_modules = _pitti_in_tree_modules + [
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
            mod_list = _pitti_consolidate_in_tree_modules
        else:
            mod_list = _pitti_in_tree_modules
            board_kernel_cmdline_extras += ["nosoftlockup"]
            kernel_vendor_cmdline_extras += ["nosoftlockup"]
            board_bootconfig_extras += ["androidboot.console=0"]

        define_msm_la(
            msm_target = target_name,
            variant = variant,
            in_tree_module_list = mod_list,
            boot_image_opts = boot_image_opts(
                earlycon_addr = "qcom_geni,0x04a90000",
                kernel_vendor_cmdline_extras = kernel_vendor_cmdline_extras,
                board_kernel_cmdline_extras = board_kernel_cmdline_extras,
                board_bootconfig_extras = board_bootconfig_extras,
            ),
        )
