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
they satisfy (``@satisfies`` in ``include/safe_data/safe_data.h``):

.. needtable::
   :types: impl
   :columns: id, title, depends_on, satisfies
   :style: table

Requirements satisfied by an API symbol:

.. needtable::
   :filter: type == "requirement" and satisfies_back
   :columns: id, title, satisfies_back
   :style: table

Requirements not satisfied by a single API symbol. These constrain the whole
API (argument validation, fault reaction, bounded locking, ISR restrictions,
serialised access, the Strict API surface) and are left for a design-level
trace on purpose:

.. needtable::
   :filter: type == "requirement" and not satisfies_back
   :columns: id, title
   :style: table
