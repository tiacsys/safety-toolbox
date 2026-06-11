# Safe Data API — Test Report (Sphinx + sphinx-needs).
#
# Test outcomes are derived from twister output: the `testreport` directive
# (see _extensions/test_module.py) parses twister_report.xml, joins each
# result against the test specification's needs.json, and renders one
# sphinx-needs test_result item per executed test case.
import os
import sys
from pathlib import Path

DOC_BASE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(DOC_BASE / "_extensions"))

project = "Safe Data API — Test Report"
author = "Safe Data API contributors"
copyright = "2026, Safe Data API contributors"
release = "0.2"
version = "0.2"

extensions = [
    "sphinx_needs",
    "test_module",
    "sphinx_rtd_theme",
]

exclude_patterns = ["_build", "Thumbs.db", ".DS_Store"]

# -- sphinx-needs (shared between all Safe Data documents) ----------------------

from needs_common import *  # noqa: E402,F401,F403

# Deploy root: set by doc/CMakeLists.txt (SAFE_DATA_DOC_DEPLOY); falls back to
# the plain sphinx-build layout for direct invocations.
_DEPLOY = Path(os.environ.get("SAFE_DATA_DOC_DEPLOY", DOC_BASE / "_build"))

BASE_URL = os.environ.get("SAFE_DATA_DOC_BASE_URL", "http://localhost:8000")

# NOTE: the testreport directive reads the FIRST entry as the test
# specification needs.json — keep the order.
needs_external_needs = [
    {
        "json_path": str(_DEPLOY / "test-specification" / "html" / "needs.json"),
        "base_url": f"{BASE_URL}/test-specification/html",
        "version": "0.2",
    },
    {
        "json_path": str(_DEPLOY / "requirement-specification" / "html" / "needs.json"),
        "base_url": f"{BASE_URL}/requirement-specification/html",
        "version": "0.2",
    },
]

# -- testreport / twisterinfo directives -----------------------------------------

# Twister output directory containing twister.json / twister_report.xml.
twister_output_dir = os.environ.get(
    "TWISTER_OUT", "/workspace/build-safe_api/twister-out"
)

# -- HTML ----------------------------------------------------------------------

html_theme = "sphinx_rtd_theme"
html_title = project
html_show_sphinx = False
