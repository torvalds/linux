# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

from dataclasses import dataclass, field
from sbom.spdx.core import Element


@dataclass(kw_only=True)
class AnyLicenseInfo(Element):
    """https://spdx.github.io/spdx-spec/v3.0.1/model/SimpleLicensing/Classes/AnyLicenseInfo/"""

    type: str = field(init=False, default="simplelicensing_AnyLicenseInfo")


@dataclass(kw_only=True)
class LicenseExpression(AnyLicenseInfo):
    """https://spdx.github.io/spdx-spec/v3.0.1/model/SimpleLicensing/Classes/LicenseExpression/"""

    type: str = field(init=False, default="simplelicensing_LicenseExpression")
    simplelicensing_licenseExpression: str
