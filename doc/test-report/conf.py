# Copyright (c) 2026 inovex GmbH
# SPDX-License-Identifier: Apache-2.0
#
# Sphinx configuration shim: the zdocs engine provides the shared
# configuration (theme, sphinx-needs, cross-document links, version).

import os
import sys
from pathlib import Path

sys.path.insert(0, os.environ["ZDOCS_CONF_DIR"])
from zdocs_conf import configure  # noqa: E402

configure(
    globals(),
    doc_dir=Path(__file__).resolve().parent,
    project="Safe Data API — Test Report",
    author="Safe Data API contributors",
    copyright_holder="Safe Data API contributors",
)
