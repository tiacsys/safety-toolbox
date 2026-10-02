Cross-Reference Test
####################

Smoke test for the cross-document reference channels. Each bullet exercises
one channel into one peer document and must render as a link. ``doc-check``
checks this page (``xref_smoketest:`` in ``doc/documents.yaml``) and fails the
build if a bullet comes out as plain text. The page is part of the test
specification, so it builds with and without the test report.

Intersphinx — other Sphinx documents
====================================

* Requirement Specification: :external+req:doc:`index`
* API Traceability: :external+apitrace:doc:`index`

Doxylink — Doxygen documents
============================

* API Reference (Doxygen): :dox_api:`safe_data_init`
* Test Sources (Doxygen): :dox_testspec:`safe_data_module`

External needs — sphinx-needs imports
=====================================

* Requirement Specification: :need:`SD-REQ-001`
* API Traceability: :need:`IMPL-safe_data_init`
