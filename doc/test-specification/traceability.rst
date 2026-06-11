Requirements Traceability
#########################

Test case → requirement mapping
===============================

.. needtable::
   :types: test_case
   :columns: id, title, suite, status, verifies
   :style: table

Covered requirements
====================

Requirements with at least one test case linked to them.

.. needtable::
   :filter: type == "requirement" and verifies_back
   :columns: id, title, verifies_back
   :style: table

Coverage gaps
=============

Requirements not yet covered by any test case.

.. needtable::
   :filter: type == "requirement" and not verifies_back
   :columns: id, title
   :style: table
