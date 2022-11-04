load("//build/kernel/kleaf/impl:image/image_utils.bzl", "image_utils")

def _super_image_impl(ctx):
    super_img = ctx.actions.declare_file("{}/super.img".format(ctx.label.name))
    super_img_size = ctx.attr.super_img_size
    super_unsparsed_img = ctx.actions.declare_file("{}/super_unsparsed.img".format(ctx.label.name))
    staging_dir = super_img.dirname + "/staging"

    # Create a s bash array of input images
    super_img_contents = "("
    for dep in ctx.attr.deps:
        for f in dep.files.to_list():
            if f.extension == "img":
                super_img_contents += f.path + " "
    super_img_contents += ")"

    command = """
            # Build super
              (
                SUPER_IMAGE_CONTENTS={super_img_contents}
                SUPER_IMAGE_SIZE={super_img_size}
                build_super
              )
            # Move output files into place
              mv "${{DIST_DIR}}/super.img" {super_img}
              mv "${{DIST_DIR}}/super_unsparsed.img" {super_unsparsed_img}
    """.format(
        super_img = super_img.path,
        super_img_size = super_img_size,
        super_unsparsed_img = super_unsparsed_img.path,
        super_img_contents = super_img_contents,
    )

    return image_utils.build_modules_image_impl_common(
        ctx = ctx,
        what = "super",
        outputs = [super_img, super_unsparsed_img],
        build_command = command,
        modules_staging_dir = staging_dir,
        mnemonic = "SuperImage",
    )

super_image = rule(
    implementation = _super_image_impl,
    doc = """Build super image.

Execute `build_super` in `build_utils.sh`.

When included in a `copy_to_dist_dir` rule, this rule copies a `super.img` to `DIST_DIR`.
""",
    attrs = image_utils.build_modules_image_attrs_common({
        "deps": attr.label_list(
            allow_files = True,
            mandatory = True,
        ),
        "super_img_size": attr.int(
            default = 0x10000000,
            doc = "Size of super.img",
        ),
    }),
)
