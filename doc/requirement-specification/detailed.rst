Detailed Requirements
#####################

Each detailed requirement refines exactly one :doc:`top-level requirement
<top-level>` and is verified by test cases of the *Test Specification* via
their ``verifies`` links.

.. contents::
   :local:
   :depth: 1

Integrity detection (SD-TOP-001)
================================

.. requirement:: Sealing on initialisation
   :id: SD-REQ-001
   :status: approved
   :tags: core
   :refines: SD-TOP-001

   ``safe_data_init()`` shall initialise the container mutex (when locking is
   configured) and seal the current payload contents: compute the integrity
   tag over the full payload and, when ``CONFIG_SAFE_DATA_REDUNDANT`` is
   enabled, refresh the inverted shadow copy.

.. requirement:: Pure verification
   :id: SD-REQ-003
   :status: approved
   :tags: core
   :refines: SD-TOP-001

   ``safe_data_verify()`` shall be free of side effects on the protected data:
   it shall never modify the payload, the stored tag, or the shadow copy. It
   shall return ``0`` for an intact payload, ``SAFE_DATA_RECOVERED`` for a
   corruption that the shadow copy can repair, and ``-EILSEQ`` for an
   unrecoverable corruption.

.. requirement:: Validated read
   :id: SD-REQ-005
   :status: approved
   :tags: operations
   :refines: SD-TOP-001

   ``safe_data_read()`` shall, under the container lock: verify the payload,
   repair it from the shadow copy if recoverable (making the repair durable by
   resealing), and only then copy the payload to the caller's buffer. An
   unrecoverable corruption shall fail with ``-EILSEQ`` and no data shall be
   copied.

Consistent modification (SD-TOP-002)
====================================

.. requirement:: Atomic sealed write
   :id: SD-REQ-006
   :status: approved
   :tags: operations
   :refines: SD-TOP-002

   ``safe_data_write()`` shall replace the payload and reseal the integrity
   tag (and shadow copy) as one operation under the container lock, so that a
   stale tag after a write is impossible by construction.

.. requirement:: Transactional read-modify-write
   :id: SD-REQ-007
   :status: approved
   :tags: operations
   :refines: SD-TOP-002

   ``safe_data_update()`` shall hold the container lock across the entire
   verify → mutate → reseal sequence. A mutator returning ``0`` commits (the
   tag is resealed); a mutator returning a negative errno aborts: with the
   shadow copy the payload shall be rolled back to the pre-update value, and
   the mutator's error code shall be returned unchanged.

.. requirement:: Serialised access — no torn snapshots
   :id: SD-REQ-016
   :status: approved
   :tags: concurrency
   :refines: SD-TOP-002

   With locking enabled, concurrent readers and writers of one container
   shall be serialised such that a reader can never observe a torn
   (internally inconsistent) payload snapshot.

.. requirement:: Scoped access block
   :id: SD-REQ-021
   :status: approved
   :tags: operations, kconfig-SAFE_DATA_GNU_EXTENSIONS
   :refines: SD-TOP-002

   ``SAFE_SECTION`` shall acquire the container lock and verify (and, if
   recoverable, repair) the payload before the block executes, expose a typed
   payload pointer inside the block, and reseal + unlock on normal scope
   exit. On verification failure or lock timeout the block shall be skipped.

.. requirement:: Unchecked commit
   :id: SD-REQ-022
   :status: approved
   :tags: operations, kconfig-SAFE_DATA_ALLOW_UNCHECKED_COMMIT
   :refines: SD-TOP-002

   When available, ``safe_data_commit()`` shall recompute the integrity tag
   (and refresh the shadow copy) over the current payload contents under the
   container lock, without prior verification. This is the audited escape
   hatch for external low-level modifications.

Single-fault recovery (SD-TOP-003)
==================================

.. requirement:: In-place repair under mutual exclusion
   :id: SD-REQ-004
   :status: approved
   :tags: core, recovery
   :refines: SD-TOP-003

   ``safe_data_verify_repair()`` shall restore a recoverable payload from the
   shadow copy in place and report ``SAFE_DATA_RECOVERED``. The caller must
   hold the container lock (or provide equivalent external mutual exclusion);
   the locked operations (read, update, ``SAFE_SECTION``) use this function
   internally.

.. requirement:: Single-copy fault recovery
   :id: SD-REQ-009
   :status: approved
   :tags: recovery
   :links: SD-REQ-004
   :refines: SD-TOP-003

   With ``CONFIG_SAFE_DATA_REDUNDANT`` enabled, the locked operations shall
   recover from either single-copy fault class: (a) corrupted payload with an
   intact shadow — restore the payload; (b) corrupted stored tag with payload
   and shadow consistent — reseal the tag. The recovered value shall equal the
   last committed value.

.. requirement:: Double-fault detection
   :id: SD-REQ-010
   :status: approved
   :tags: recovery
   :refines: SD-TOP-003

   When the payload and the shadow copy are inconsistent and neither matches
   the stored tag, no copy can vouch for the data: verification shall report
   ``-EILSEQ`` (unrecoverable) and shall not fabricate a repair.

Fault reaction and observability (SD-TOP-004)
=============================================

.. requirement:: Configurable unrecoverable-fault reaction
   :id: SD-REQ-011
   :status: approved
   :tags: fault-handling
   :refines: SD-TOP-004

   The control-flow reaction to an unrecoverable fault shall be selectable at
   build time: return the error code (``CONFIG_SAFE_DATA_FAULT_RETURN``),
   invoke the fault handler (``CONFIG_SAFE_DATA_FAULT_HANDLER``), or panic
   (``CONFIG_SAFE_DATA_FAULT_PANIC``).

.. requirement:: Integrity-event statistics
   :id: SD-REQ-012
   :status: approved
   :tags: diagnostics, kconfig-SAFE_DATA_STATS
   :refines: SD-TOP-004

   With ``CONFIG_SAFE_DATA_STATS`` enabled, every integrity event shall be
   counted exactly once per detection in a global statistics block readable
   via ``safe_data_stats_get()``: recovered-from-tag, recovered-from-shadow,
   unrecoverable, lock timeout, overwritten-corrupt, and clamped mutator
   return.

.. requirement:: Fault-handler registration and event reporting
   :id: SD-REQ-013
   :status: approved
   :tags: diagnostics
   :refines: SD-TOP-004

   ``safe_data_fault_handler_register()`` shall accept exactly one callback
   (a second, different registration shall be rejected with ``-EALREADY``;
   re-registering the same callback and unregistering with ``NULL`` shall
   succeed). A registered callback shall receive **all** integrity event
   kinds with a populated event descriptor, independent of the configured
   unrecoverable-fault reaction.

.. requirement:: Write may not silently mask corruption
   :id: SD-REQ-018
   :status: approved
   :tags: diagnostics, kconfig-SAFE_DATA_WRITE_CHECKS_OLD
   :refines: SD-TOP-004

   With ``CONFIG_SAFE_DATA_WRITE_CHECKS_OLD`` enabled, ``safe_data_write()``
   shall verify the old contents before overwriting them. A detected
   corruption shall be reported as an observe-only diagnostic event
   (statistics + callback) and shall not change the outcome of the write.

Timing and execution-context safety (SD-TOP-005)
================================================

.. requirement:: Bounded lock acquisition
   :id: SD-REQ-014
   :status: approved
   :tags: timing, kconfig-SAFE_DATA_LOCK_TIMEOUT_MS
   :refines: SD-TOP-005

   With ``CONFIG_SAFE_DATA_LOCK_TIMEOUT_MS`` > 0, no API operation shall block
   on a container lock longer than the configured bound. On expiry the
   operation shall fail with ``-ETIMEDOUT`` and the timeout shall be reported
   as a diagnostic event. The container shall remain fully usable after the
   lock holder releases.

.. requirement:: No ISR-context locking
   :id: SD-REQ-015
   :status: approved
   :tags: timing, defensive
   :refines: SD-TOP-005

   The locking entry points are not ISR-safe. Calling them from interrupt
   context shall be caught by an assertion (``CONFIG_ASSERT``) instead of
   producing undefined kernel behaviour.

Defensive interface (SD-TOP-006)
================================

.. requirement:: Argument validation
   :id: SD-REQ-002
   :status: approved
   :tags: core, defensive
   :refines: SD-TOP-006

   Every API entry point shall reject missing required pointer arguments and
   zero-length payloads with ``-EINVAL``. A ``NULL`` lock pointer is not an
   error: it selects the lock-free single-context mode.

.. requirement:: Mutator contract enforcement
   :id: SD-REQ-008
   :status: approved
   :tags: operations, defensive
   :refines: SD-TOP-006

   A mutator returning a positive value violates the transaction contract.
   ``safe_data_update()`` shall treat it as an abort, return ``-EINVAL``, and
   report the violation as a diagnostic event so a confused mutator can never
   commit by accident.

.. requirement:: Compile-time payload guards
   :id: SD-REQ-019
   :status: approved
   :tags: compile-time
   :refines: SD-TOP-006

   ``SAFE_CONTAINER_DEFINE`` shall reject zero-size payload types at compile
   time, and shall enforce an upper payload-size bound tied to the configured
   tag strength (CRC-8 ≤ 16 bytes, CRC-16 ≤ 256 bytes). A documented
   deviation path (``SAFE_CONTAINER_DEFINE_UNCHECKED``) bypasses only the
   size bound.

.. requirement:: Reduced API surface at the Strict level
   :id: SD-REQ-020
   :status: approved
   :tags: compile-time, kconfig-SAFE_DATA_LEVEL_STRICT
   :refines: SD-TOP-006

   With ``CONFIG_SAFE_DATA_ALLOW_UNCHECKED_COMMIT`` disabled the unchecked
   reseal (``SAFE_COMMIT``/``safe_data_commit``) shall not be part of the
   compiled API surface; with ``CONFIG_SAFE_DATA_GNU_EXTENSIONS`` disabled the
   same shall hold for ``SAFE_SECTION``. Uses shall fail at build time, not at
   run time.

Self-diagnosis (SD-TOP-007)
===========================

.. requirement:: Integrity-mechanism self-test
   :id: SD-REQ-017
   :status: approved
   :tags: diagnostics, kconfig-SAFE_DATA_SELFTEST
   :refines: SD-TOP-007

   ``safe_data_selftest()`` shall diagnose the integrity mechanism itself: a
   known-answer test of the configured CRC backend and a
   seal/corrupt/detect(/recover) round trip on a scratch container, without
   invoking the configured fault reaction and without polluting the
   statistics. It shall return ``-EFAULT`` on any failure. With
   ``CONFIG_SAFE_DATA_SELFTEST_BOOT`` the self-test shall run from
   ``SYS_INIT`` and panic on failure.

Requirement overview
====================

.. needtable::
   :types: requirement
   :columns: id, title, status, refines, tags
   :style: table
