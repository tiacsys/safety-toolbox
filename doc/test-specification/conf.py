# Safe Data API — Test Specification (Sphinx + sphinx-needs).
#
# Pipeline (mirrors zephyr-safety): Doxygen first runs over the annotated
# ztest sources (safe-data-testspec doxyfile, driven by CMake); the
# `testmodule` directive then parses that doxygen XML and renders one
# sphinx-needs test_case item per ZTEST function.
import os
import sys
from pathlib import Path

DOC_BASE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(DOC_BASE / "_extensions"))

project = "Safe Data API — Test Specification"
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

# Link test cases against the requirement specification without a combined
# build: import its needs.json (written by needs_build_json = True).
BASE_URL = os.environ.get("SAFE_DATA_DOC_BASE_URL", "http://localhost:8000")

needs_external_needs = [
    {
        "json_path": str(_DEPLOY / "requirement-specification" / "html" / "needs.json"),
        "base_url": f"{BASE_URL}/requirement-specification/html",
        "version": "0.2",
    },
]

# -- testmodule directive --------------------------------------------------------

# Doxygen XML of the annotated test sources (built by the
# doxygen-safe-data-testspec CMake target before this document).
testspec_doxygen_xml = str(_DEPLOY / "doxygen-safe-data-testspec" / "xml")

# URLs of the doxygen HTML renderings, relative to this document's HTML root
# (both live under the same deploy/ tree).
_OUT = Path(os.environ.get("OUTPUT_DIR", _DEPLOY / "test-specification" / "html"))
testspec_doxygen_url = os.path.relpath(
    _DEPLOY / "doxygen-safe-data-testspec" / "html", _OUT).replace(os.sep, "/")
api_doxygen_url = os.path.relpath(
    _DEPLOY / "api-documentation" / "html", _OUT).replace(os.sep, "/")

# -- HTML ----------------------------------------------------------------------

html_theme = "sphinx_rtd_theme"
html_title = project
html_show_sphinx = False
