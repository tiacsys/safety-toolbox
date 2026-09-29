Cross-Reference Test
####################

Smoke test for the cross-document reference channels. Each bullet exercises
one channel into one peer document and must render as a link. ``doc-check``
checks this page (``xref_smoketest:`` in ``doc/documents.yaml``) and fails the
build if a bullet comes out as plain text.

Intersphinx — other Sphinx documents
====================================

* Requirement Specification: :external+req:doc:`index`
* Test Specification: :external+testspec:doc:`index`

Doxylink — Doxygen documents
============================

* API Reference (Doxygen): :dox_api:`safe_data_init`
* Test Sources (Doxygen): :dox_testspec:`safe_data_module`

External needs — sphinx-needs imports
=====================================

* Requirement Specification: :need:`SD-REQ-001`
* Test Specification: :need:`TC_SAFE_DATA_INIT_AND_VERIFY`
