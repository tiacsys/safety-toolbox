Requirements Traceability
#########################

Test case → requirement mapping
===============================

.. needtable::
   :types: test_case
   :columns: id, title, suite, status, depends_on, verifies
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

Implementation → requirement
============================

API symbols (``impl`` needs from the *API Traceability*) and the requirements
they satisfy (``@satisfies`` in ``include/safe_data/safe_data.h``). A
requirement that constrains the whole API (argument validation, fault
reaction, bounded locking, the ISR check, serialised access, the reduced API
surface) is traced to each public symbol that contains the behaviour:

.. needtable::
   :types: impl
   :columns: id, title, depends_on, satisfies
   :style: table

Requirements satisfied by an API symbol:

.. needtable::
   :filter: type == "requirement" and satisfies_back
   :columns: id, title, satisfies_back
   :style: table

Requirements that no API symbol satisfies:

.. needtable::
   :filter: type == "requirement" and not satisfies_back
   :columns: id, title
   :style: table
