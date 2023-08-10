load("@bazel_skylib//rules:write_file.bzl", "write_file")

def define_top_level_config(target):
    """Define common top-level variables in build.config"""
    rule_name = "{}_top_level_config".format(target)
    write_file(
        name = rule_name,
        out = "build.config.bazel.top.level.{}".format(target),
        content = [
            "# === define_top_level_config ===",
            "BUILDING_WITH_BAZEL=true",
            "# === end define_top_level_config ===",
            "",  # Needed for newline at end of file
        ],
    )

    return ":{}".format(rule_name)

def gen_config_without_source_lines(build_config, target):
    """Replace "." or "source" lines in build.config files with shell null operator"""
    rule_name = "{}.{}".format(target, build_config)
    out_file_name = rule_name + ".generated"
    native.genrule(
        name = rule_name,
        srcs = [build_config],
        outs = [out_file_name],
        cmd_bash = """
          sed -e 's/^ *\\. /: # &/' \
              -e 's/^ *source /: # &/' \
              $(location {}) > "$@"
        """.format(build_config),
    )

    return ":" + rule_name

def get_out_dir(msm_target, variant):
    if "allyes" in msm_target:
        return "out/msm-kernel-{}-{}".format(msm_target.replace("_", "-"), variant.replace("-", "_"))
    return "out/msm-kernel-{}-{}".format(msm_target.replace("-", "_"), variant.replace("-", "_"))

def define_signing_keys():
    native.genrule(
        name = "signing_key",
        srcs = ["//msm-kernel:certs/qcom_x509.genkey"],
        outs = ["signing_key.pem"],
        tools = ["//prebuilts/build-tools:linux-x86/bin/openssl"],
        cmd_bash = """
          $(location //prebuilts/build-tools:linux-x86/bin/openssl) req -new -nodes -utf8 -sha256 -days 36500 \
            -batch -x509 -config $(location //msm-kernel:certs/qcom_x509.genkey) \
            -outform PEM -out "$@" -keyout "$@"
        """,
    )

    native.genrule(
        name = "verity_key",
        srcs = ["//msm-kernel:certs/qcom_x509.genkey"],
        outs = ["verity_cert.pem", "verity_key.pem"],
        tools = ["//prebuilts/build-tools:linux-x86/bin/openssl"],
        cmd_bash = """
          $(location //prebuilts/build-tools:linux-x86/bin/openssl) req -new -nodes -utf8 -newkey rsa:1024 -days 36500 \
            -batch -x509 -config $(location //msm-kernel:certs/qcom_x509.genkey) \
            -outform PEM -out $(location verity_cert.pem) -keyout $(location verity_key.pem)
        """,
    )
