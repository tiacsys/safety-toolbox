Top-Level Requirements
######################

The seven top-level requirements state *what* the Safe Data API guarantees at
the level of the safety function. Each is refined by detailed requirements
(``SD-REQ-…``, see :doc:`detailed`) that state *how* the guarantee is
delivered and that are individually verified by test cases.

.. top_requirement:: Integrity detection by construction
   :id: SD-TOP-001
   :status: approved
   :tags: detection

   All data protected by the Safe Data API shall be sealed with an integrity
   tag, and integrity shall be checked before protected data is handed to the
   application, so that corruption of the payload, the redundant copy, or the
   stored tag is detected before corrupt data can be consumed.

.. top_requirement:: Consistent modification
   :id: SD-TOP-002
   :status: approved
   :tags: consistency

   Every modification of protected data shall be atomic with respect to
   concurrent access and shall leave the container in a sealed, verifiable
   state. A torn, half-applied, or stale-tagged modification shall not be
   observable by any reader.

.. top_requirement:: Single-fault recovery
   :id: SD-TOP-003
   :status: approved
   :tags: recovery

   When redundant storage is configured, the API shall recover protected data
   from any single-copy fault, and shall report — never mask or "repair" by
   fabrication — faults that exceed the recovery capability.

.. top_requirement:: Fault reaction and observability
   :id: SD-TOP-004
   :status: approved
   :tags: diagnostics

   Every integrity-relevant event shall be observable by the application or a
   health monitor, and the control-flow reaction to unrecoverable faults
   shall be configurable to match the integrating system's safety concept.

.. top_requirement:: Timing and execution-context safety
   :id: SD-TOP-005
   :status: approved
   :tags: timing

   API operations shall provide bounded blocking behaviour when configured,
   and misuse from an illegal execution context shall be caught instead of
   producing undefined kernel behaviour.

.. top_requirement:: Defensive interface
   :id: SD-TOP-006
   :status: approved
   :tags: defensive

   The API shall reject invalid use at the earliest possible binding time —
   at compile time where expressible, otherwise at run time with a defined
   error code — and shall allow reducing the API surface for certified
   builds.

.. top_requirement:: Self-diagnosis of the integrity mechanism
   :id: SD-TOP-007
   :status: approved
   :tags: diagnostics

   The integrity mechanism itself shall be diagnosable: a self-test shall
   detect a defective tag computation as well as broken detection and
   recovery logic.

Refinement overview
===================

.. needtable::
   :types: top_requirement
   :columns: id, title, refines_back
   :style: table
