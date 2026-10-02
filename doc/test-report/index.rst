Safe Data API — Test Report
############################

Test outcomes for the Safe Data API, derived from the twister results
(``twister_report.xml``). Every executed test case becomes a sphinx-needs
``test_result`` item linked ``result_of`` its test case in the *Test
Specification* and ``covers`` the requirements that test case verifies.

A ``skipped`` outcome means the test's mechanism is not part of that
scenario's configuration (e.g. recovery tests in the ``plain`` scenario, the
lock-timeout test outside the ``timeout`` scenario) — see the scenario table
in the test specification.

The results come from two test applications: ``tests/safe_data`` (scenarios
``safe_data.api.*``) and ``tests/safe_data_fault`` (scenarios
``safe_data.fault.*``, one per unrecoverable-fault reaction).

Test run
========

.. twisterinfo:: twister.json

Results: Safe Data Test Application
====================================

.. testreport:: twister_report.xml
   :module: safe_data.api

Results: Safe Data Fault-Reaction Test Application
===================================================

.. testreport:: twister_report.xml
   :module: safe_data.fault

.. toctree::
   :hidden:

   xref-test
