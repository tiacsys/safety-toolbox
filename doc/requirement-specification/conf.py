# Safe Data API — Requirement Specification (Sphinx + sphinx-needs)
import os
import sys
from pathlib import Path

DOC_BASE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(DOC_BASE / "_extensions"))

project = "Safe Data API — Requirement Specification"
author = "Safe Data API contributors"
copyright = "2026, Safe Data API contributors"
release = "0.2"
version = "0.2"

extensions = [
    "sphinx_needs",
    "sphinx_rtd_theme",
]

exclude_patterns = ["_build", "Thumbs.db", ".DS_Store"]

# -- sphinx-needs (shared between all Safe Data documents) ----------------------

from needs_common import *  # noqa: E402,F401,F403

# -- HTML ----------------------------------------------------------------------

html_theme = "sphinx_rtd_theme"
html_title = project
html_show_sphinx = False
