load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")

def define_top_level_rules():
    for skippable in ["abl", "dtc", "test_mapping", "abi"]:
        bool_flag(name = "skip_{}".format(skippable), build_setting_default = False)

        native.config_setting(
            name = "skip_{}_setting".format(skippable),
            flag_values = {":skip_{}".format(skippable): "1"}
        )

        native.config_setting(
            name = "include_{}_setting".format(skippable),
            flag_values = {":skip_{}".format(skippable): "0"}
        )

def define_extras(target, flavor = None):
    return

def get_build_config_fragments(target):
    return []

def get_dtb_list(target):
    return None

def get_dtbo_list(target):
    return None

def get_dtstree(target):
    return None

def get_vendor_ramdisk_binaries(target):
    return None

def get_gki_ramdisk_prebuilt_binary():
    return None
