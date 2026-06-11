Safe Data API — Requirement Specification
##########################################

Software requirements for the Safe Data API (``CONFIG_SAFE_DATA``), a generic
Zephyr facility for protecting variables and data structures with an integrity
tag (CRC), an optional redundant inverted shadow copy, and a per-container
mutex.

The specification is structured in two tiers:

- **Top-level requirements** (``SD-TOP-…``) state the guarantees of the
  safety function — what the API promises.
- **Detailed requirements** (``SD-REQ-…``) refine exactly one top-level
  requirement each (``refines`` relation) — how the promise is delivered.

Every requirement is a `sphinx-needs <https://sphinx-needs.readthedocs.io>`_
item with a stable ID. Test cases in the *Test Specification* link to the
detailed requirements via their ``verifies`` relation; the *Test Report*
links observed outcomes to the test cases. Together the documents form the
traceability chain top-level requirement → detailed requirement → test case
→ test result.

.. toctree::
   :maxdepth: 2

   top-level
   detailed

Specification consistency
=========================

Detailed requirements not refining any top-level requirement (must be empty):

.. needtable::
   :filter: type == "requirement" and not refines
   :columns: id, title
   :style: table

Top-level requirements without refinement (must be empty):

.. needtable::
   :filter: type == "top_requirement" and not refines_back
   :columns: id, title
   :style: table
