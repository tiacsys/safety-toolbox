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

Implementation → requirement
============================

The API side of the chain is recorded in the source: each public function
names the requirements it satisfies with Doxygen's native ``@satisfies``,
resolved against this specification's requirements like the tests'
``@verifies``. Doxygen renders that half, with every requirement's
"satisfied by" members and the requirements that no API entity satisfies, on
the `API requirements page <../dox-safe-data-api/requirements.html>`_.
Requirements that constrain the whole API rather than one function (argument
validation, bounded locking, serialised access, ISR restrictions, the Strict
API surface) are listed there as unsatisfied on purpose: they are traced at
the design level, not to a single member.
