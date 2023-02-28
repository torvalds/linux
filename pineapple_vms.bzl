load(":target_variants.bzl", "vm_types", "vm_variants")
load("//msm-kernel:msm_common.bzl", "get_out_dir")
load("//build/bazel_common_rules/dist:dist.bzl", "copy_to_dist_dir")
load(":image_opts.bzl", "vm_image_opts")

target_name = "pineapple-vms"

def create_combined_vm_image(image_list, variant, vm_size_ext4):
    out_files = [
        "{}_{}/vm-bootsys.img".format(target_name, variant),
        "{}_{}/vm-bootsys_unsparsed.img".format(target_name, variant),
        "{}_{}/vm-images".format(target_name, variant),
    ]

    merge_images = "\nimage_dist=\"{}\"\n".format(" ".join(["$(location {})".format(l) for l in image_list]))
    merge_images += "vm_image_out_path=\"$(location {})\"\n".format(out_files[0])
    merge_images += "vm_image_unsparsed_out_path=\"$(location {})\"\n".format(out_files[1])
    merge_images += "vm_images_out_path=\"$(location {})\"\n".format(out_files[2])
    merge_images += """
    mkdir -p scratch
    for dist in $$image_dist; do
      # Unpack everything from tuivm
      if echo $$dist | grep -q "tuivm"; then
          tar -C scratch/ -xf "$$dist"
      # Only unpack boot for all other VMs
      else
        tar -C scratch/ --wildcards -xf "$$dist" "./vm-images/*/boot"
      fi
    done

    # Remove existing vm-bootsys images
    rm -f scratch/*.img

    # Create non-sparse vm-bootsys.img
    $(location //prebuilts/kernel-build-tools:linux-x86/bin/mkuserimg_mke2fs) \
            "scratch/vm-images" "$$vm_image_unsparsed_out_path" ext4 / {vm_size_ext4}

    # Create sparse vm-bootsys.img
    $(location //prebuilts/kernel-build-tools:linux-x86/bin/img2simg) \
            "$$vm_image_unsparsed_out_path" "$$vm_image_out_path"

    cp -r "scratch/vm-images" "$$vm_images_out_path"
    """.format(vm_size_ext4=vm_size_ext4)

    native.genrule(
        name = "{}_{}_combo".format(target_name, variant),
        srcs = image_list,
        outs = out_files,
        tools = [
            "//prebuilts/kernel-build-tools:linux-x86/bin/mkuserimg_mke2fs",
            "//prebuilts/kernel-build-tools:linux-x86/bin/img2simg",
        ],
        cmd_bash = merge_images,
    )

def define_pineapple_vms(vm_image_opts = vm_image_opts()):
    base_target = "pineapple-tuivm"
    for variant in vm_variants:
        base_tv = "{}_{}".format(base_target, variant)
        image_list = [":pineapple-{}_{}_vm_images".format(vt, variant) for vt in vm_types]
        create_combined_vm_image(image_list, variant, vm_image_opts.vm_size_ext4)

        out_dtb_list = [":pineapple-{}_{}_vm_dtb_img".format(vt, variant) for vt in vm_types]

        dist_targets = [
            # do not sort
            ":{}".format(base_tv),
            ":{}_images".format(base_tv),
            ":{}_merged_kernel_uapi_headers".format(base_tv),
            ":{}_build_config".format(base_tv),
            ":signing_key",
            ":verity_key",
            "{}_{}_combo".format(target_name, variant),
        ] + out_dtb_list

        copy_to_dist_dir(
            name = "{}_{}_dist".format(target_name, variant),
            data = dist_targets,
            dist_dir = "{}/dist".format(get_out_dir(target_name, variant)),
            flat = True,
            wipe_dist_dir = True,
            allow_duplicate_filenames = True,
            mode_overrides = {
                # do not sort
                "**/vmlinux": "755",
                "**/Image": "755",
                "**/*.dtb*": "755",
                "**/gen_init_cpio": "755",
                "**/sign-file": "755",
                "**/*": "644",
            },
        )
