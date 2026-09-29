/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for the Safe Data API. Built with CONFIG_SAFE_DATA_REDUNDANT=y so
 * the recovery paths are exercised; redundancy-specific assertions are guarded
 * with IS_ENABLED().
 */

#include <zephyr/ztest.h>
#include <string.h>

#include <safe_data/safe_data.h>

struct payload {
	uint32_t a;
	uint16_t b;
	uint8_t  c;
};

SAFE_CONTAINER_DEFINE(safe_payload, struct payload);

static struct safe_payload sp;

static void reset_to(uint32_t a, uint16_t b, uint8_t c)
{
	struct payload v = { .a = a, .b = b, .c = c };

	zassert_ok(SAFE_INIT(&sp));
	zassert_ok(SAFE_WRITE(&sp, &v));
}

/**
 * @brief Init seals and verification passes
 *
 * SAFE_INIT seals the freshly written payload; SAFE_VERIFY on the untouched
 * container returns 0.
 *
 * @testid{TC_SAFE_DATA_INIT_AND_VERIFY}
 * @verifies SD-REQ-001
 * @verifies SD-REQ-003
 * @active
 */
ZTEST(safe_data, test_init_and_verify)
{
	reset_to(1, 2, 3);
	zassert_ok(SAFE_VERIFY(&sp), "freshly sealed data must verify");
}

/**
 * @brief Read returns the sealed value
 *
 * A value written through SAFE_WRITE is returned bit-exact by a validated
 * SAFE_READ.
 *
 * @testid{TC_SAFE_DATA_READ_ROUNDTRIP}
 * @verifies SD-REQ-005
 * @verifies SD-REQ-006
 * @active
 */
ZTEST(safe_data, test_read_roundtrip)
{
	struct payload out;

	reset_to(0xDEADBEEF, 0xCAFE, 0x42);
	zassert_ok(SAFE_READ(&sp, &out));
	zassert_equal(out.a, 0xDEADBEEF);
	zassert_equal(out.b, 0xCAFE);
	zassert_equal(out.c, 0x42);
}

/**
 * @brief Write replaces the payload and reseals
 *
 * After SAFE_WRITE the container verifies clean and a subsequent read returns
 * the new value: the tag can never go stale across a write.
 *
 * Status obsolete (demonstration of status handling): superseded by
 * TC_SAFE_DATA_READ_ROUNDTRIP, which checks the resealed write through a
 * validated read. The test still runs.
 *
 * @testid{TC_SAFE_DATA_WRITE_RESEALS}
 * @verifies SD-REQ-006
 * @obsolete
 */
ZTEST(safe_data, test_write_reseals)
{
	struct payload v = { .a = 7, .b = 8, .c = 9 };
	struct payload out;

	reset_to(0, 0, 0);
	zassert_ok(SAFE_WRITE(&sp, &v));
	zassert_ok(SAFE_VERIFY(&sp));
	zassert_ok(SAFE_READ(&sp, &out));
	zassert_equal(out.a, 7);
}

static int mut_add(void *payload, void *user)
{
	struct payload *p = payload;

	p->a += *(uint32_t *)user;
	return 0;
}

static int mut_abort(void *payload, void *user)
{
	struct payload *p = payload;

	ARG_UNUSED(user);
	p->a = 0xFFFFFFFF; /* must be rolled back logically (tag not resealed) */
	return -ERANGE;
}

/**
 * @brief Update commits a mutator transaction
 *
 * SAFE_UPDATE runs the mutator under the lock and reseals on a zero return;
 * the committed value is visible to the next read.
 *
 * @testid{TC_SAFE_DATA_UPDATE_COMMITS}
 * @verifies SD-REQ-007
 * @active
 */
ZTEST(safe_data, test_update_commits)
{
	uint32_t delta = 5;
	struct payload out;

	reset_to(10, 0, 0);
	zassert_ok(SAFE_UPDATE(&sp, mut_add, &delta));
	zassert_ok(SAFE_READ(&sp, &out));
	zassert_equal(out.a, 15);
}

/**
 * @brief Aborted update leaves a consistent container
 *
 * A mutator returning a negative errno aborts the transaction. With the
 * redundant shadow the payload is rolled back to the pre-update value; without
 * it the partial write is detectable via the (not resealed) tag.
 *
 * @testid{TC_SAFE_DATA_UPDATE_ABORT_KEEPS_INTEGRITY}
 * @verifies SD-REQ-007
 * @active
 */
ZTEST(safe_data, test_update_abort_keeps_integrity)
{
	int ret;

	struct payload out;

	reset_to(10, 0, 0);
	ret = SAFE_UPDATE(&sp, mut_abort, NULL);
	zassert_equal(ret, -ERANGE, "mutator's error must propagate");

	/* The mutator wrote a->0xFFFFFFFF then aborted. With the shadow copy
	 * the transaction rolls the payload back, leaving the container clean.
	 */
	if (IS_ENABLED(CONFIG_SAFE_DATA_REDUNDANT)) {
		zassert_ok(SAFE_VERIFY(&sp), "abort must roll back cleanly");
		zassert_ok(SAFE_READ(&sp, &out));
		zassert_equal(out.a, 10, "payload must be the pre-update value");
	} else {
		/* No rollback buffer: the partial write is detectable. */
		zassert_equal(SAFE_VERIFY(&sp), -EILSEQ);
	}
}

/**
 * @brief SAFE_SECTION reseals at scope exit
 *
 * Modifications made through the typed pointer inside a SAFE_SECTION block are
 * sealed automatically on normal scope exit and visible to the next read.
 *
 * @testid{TC_SAFE_DATA_SECTION_SCOPE}
 * @verifies SD-REQ-021
 * @kconfig_depends{CONFIG_SAFE_DATA_GNU_EXTENSIONS}
 * @active
 */
ZTEST(safe_data, test_section_scope)
{
#if defined(CONFIG_SAFE_DATA_GNU_EXTENSIONS)
	struct payload out;

	reset_to(1, 0, 0);
	SAFE_SECTION(&sp, p) {
		p->a += 100;
		p->c = 0xAB;
	}
	zassert_ok(SAFE_VERIFY(&sp), "section must reseal on exit");
	zassert_ok(SAFE_READ(&sp, &out));
	zassert_equal(out.a, 101);
	zassert_equal(out.c, 0xAB);
#else
	ztest_test_skip(); /* SAFE_SECTION not in the configured API surface */
#endif
}

/**
 * @brief Payload corruption detected; verification is pure
 *
 * Out-of-band payload corruption is detected by SAFE_VERIFY without modifying
 * the payload (pure check). With the shadow copy the subsequent locked read
 * repairs and reseals; without it the fault is unrecoverable.
 *
 * @testid{TC_SAFE_DATA_DETECTS_CORRUPTION}
 * @verifies SD-REQ-003
 * @verifies SD-REQ-009
 * @active
 */
ZTEST(safe_data, test_detects_corruption)
{
	reset_to(100, 200, 30);

	/* Corrupt the live payload behind the API's back. */
	sp.payload.a = 0x12345678;

	if (IS_ENABLED(CONFIG_SAFE_DATA_REDUNDANT)) {
		/* shadow still good -> recoverable; verify is PURE and must
		 * not have repaired anything yet.
		 */
		struct payload out;

		zassert_equal(SAFE_VERIFY(&sp), SAFE_DATA_RECOVERED);
		zassert_equal(sp.payload.a, 0x12345678,
			      "verify must not modify the payload");

		/* The locked read repairs in place and reseals. */
		zassert_ok(SAFE_READ(&sp, &out));
		zassert_equal(out.a, 100, "payload must be restored from shadow");
		zassert_equal(sp.payload.a, 100);
	} else {
		zassert_equal(SAFE_VERIFY(&sp), -EILSEQ);
	}
}

/**
 * @brief verify_repair restores the payload in place
 *
 * safe_data_verify_repair() (lock-held contract) restores a corrupted payload
 * from the shadow copy in place; the container verifies clean afterwards.
 *
 * @testid{TC_SAFE_DATA_VERIFY_REPAIR_FIXES_IN_PLACE}
 * @verifies SD-REQ-004
 * @verifies SD-REQ-009
 * @kconfig_depends{CONFIG_SAFE_DATA_REDUNDANT}
 * @active
 */
ZTEST(safe_data, test_verify_repair_fixes_in_place)
{
	if (!IS_ENABLED(CONFIG_SAFE_DATA_REDUNDANT)) {
		ztest_test_skip(); /* repair requires the shadow copy */
	}

	reset_to(100, 200, 30);
	sp.payload.a = 0x12345678;

	/* verify_repair (here with external mutual exclusion: single thread)
	 * restores the payload; the tag is still stale until a reseal.
	 */
	zassert_equal(safe_data_verify_repair(&sp.payload, sizeof(sp.payload),
					      sp._crc, _SAFE_SHADOW(&sp)),
		      SAFE_DATA_RECOVERED);
	zassert_equal(sp.payload.a, 100, "repair must restore the payload");
	/* The restored payload matches the stored tag again. */
	zassert_ok(SAFE_VERIFY(&sp));
}

/**
 * @brief Unchecked commit reseals an external modification
 *
 * SAFE_COMMIT re-tags the current contents after an audited direct
 * modification of the storage (only available with
 * CONFIG_SAFE_DATA_ALLOW_UNCHECKED_COMMIT).
 *
 * @testid{TC_SAFE_DATA_COMMIT_RESEALS_EXTERNAL_WRITE}
 * @verifies SD-REQ-022
 * @kconfig_depends{CONFIG_SAFE_DATA_ALLOW_UNCHECKED_COMMIT}
 * @active
 */
ZTEST(safe_data, test_commit_reseals_external_write)
{
#if !defined(CONFIG_SAFE_DATA_ALLOW_UNCHECKED_COMMIT)
	ztest_test_skip(); /* SAFE_COMMIT not in the configured API surface */
#else
	struct payload out;

	reset_to(1, 2, 3);

	/* Modify the storage directly (as an audited low-level path might),
	 * then SAFE_COMMIT reseals the tag (and refreshes the shadow) over the
	 * new contents. commit does not verify first, so this works the same
	 * with or without the redundant copy.
	 */
	sp.payload.a = 77;
	zassert_ok(SAFE_COMMIT(&sp));
	zassert_ok(SAFE_VERIFY(&sp), "commit must reseal over current contents");
	zassert_ok(SAFE_READ(&sp, &out));
	zassert_equal(out.a, 77);
#endif /* CONFIG_SAFE_DATA_ALLOW_UNCHECKED_COMMIT */
}

/**
 * @brief Stale tag with consistent data recovers
 *
 * When only the stored tag is corrupted (payload and shadow agree), the fault
 * is recoverable; a locked read makes the reseal durable.
 *
 * Status draft (demonstration of status handling): SD-REQ-009 is also
 * verified by TC_SAFE_DATA_DETECTS_CORRUPTION and
 * TC_SAFE_DATA_VERIFY_REPAIR_FIXES_IN_PLACE. The test still runs.
 *
 * @testid{TC_SAFE_DATA_STALE_TAG_RECOVERS}
 * @verifies SD-REQ-009
 * @kconfig_depends{CONFIG_SAFE_DATA_REDUNDANT}
 * @draft
 */
ZTEST(safe_data, test_stale_tag_recovers)
{
	struct payload out;

	if (!IS_ENABLED(CONFIG_SAFE_DATA_REDUNDANT)) {
		ztest_test_skip(); /* recovery requires the shadow copy */
	}

	reset_to(5, 6, 7);

	/* Corrupt ONLY the stored tag; payload and shadow stay consistent.
	 * verify() must notice the data itself is fine and report RECOVERED.
	 */
	sp._crc ^= 0xA5A5A5A5u;
	zassert_equal(SAFE_VERIFY(&sp), SAFE_DATA_RECOVERED,
		      "consistent data with a stale tag must be recoverable");

	/* A read makes the recovery durable (reseals the tag). */
	zassert_ok(SAFE_READ(&sp, &out));
	zassert_equal(out.a, 5);
	zassert_ok(SAFE_VERIFY(&sp), "tag must be resealed after recovery");
}

/**
 * @brief Double fault is unrecoverable
 *
 * Inconsistent corruption of payload AND shadow leaves no copy to vouch for
 * the data: verification reports -EILSEQ.
 *
 * @testid{TC_SAFE_DATA_DOUBLE_FAULT_UNRECOVERABLE}
 * @verifies SD-REQ-010
 * @kconfig_depends{CONFIG_SAFE_DATA_REDUNDANT}
 * @active
 */
ZTEST(safe_data, test_double_fault_unrecoverable)
{
	/* The shadow field only exists when redundancy is compiled in, so this
	 * test body must be guarded at compile time (a runtime skip would still
	 * try to compile the sp._shadow reference).
	 */
#if CONFIG_SAFE_DATA_REDUNDANT
	reset_to(9, 9, 9);

	/* Corrupt the payload AND the shadow inconsistently: neither copy can
	 * vouch for the other, so the fault is unrecoverable.
	 */
	sp.payload.a = 0x11111111u;
	sp._shadow[0] ^= 0xAAu;
	zassert_equal(SAFE_VERIFY(&sp), -EILSEQ,
		      "a double fault must be reported as unrecoverable");
#else
	ztest_test_skip(); /* the double-fault path only exists with a shadow */
#endif
}

/**
 * @brief NULL and zero-size arguments are rejected
 *
 * Every API entry point rejects missing required pointers and zero-length
 * payloads with -EINVAL (a NULL lock is legal: locking is optional).
 *
 * @testid{TC_SAFE_DATA_NULL_ARGS}
 * @verifies SD-REQ-002
 * @active
 */
ZTEST(safe_data, test_null_args)
{
	uint32_t crc = 0;
	uint8_t buf[4] = {0};
	uint8_t out[4] = {0};

	/* A NULL lock is allowed (locking optional); NULL payload/crc is not. */
	zassert_ok(safe_data_init(NULL, buf, sizeof(buf), &crc, NULL),
		   "NULL lock must be accepted");
	zassert_equal(safe_data_init(NULL, NULL, sizeof(buf), &crc, NULL),
		      -EINVAL);
	zassert_equal(safe_data_init(NULL, buf, 0, &crc, NULL), -EINVAL);
	zassert_equal(safe_data_verify(NULL, sizeof(buf), crc, NULL), -EINVAL);
	zassert_equal(safe_data_verify(buf, 0, crc, NULL), -EINVAL);

	/* The remaining entry points must reject their required NULL args too. */
	zassert_equal(safe_data_read(NULL, buf, sizeof(buf), &crc, NULL, NULL),
		      -EINVAL, "read needs an out buffer");
	zassert_equal(safe_data_read(NULL, NULL, sizeof(buf), &crc, NULL, out),
		      -EINVAL);
	zassert_equal(safe_data_write(NULL, buf, sizeof(buf), &crc, NULL, NULL),
		      -EINVAL, "write needs an input buffer");
	zassert_equal(safe_data_update(NULL, buf, sizeof(buf), &crc, NULL, NULL,
				       NULL),
		      -EINVAL, "update needs a mutator");
#if defined(CONFIG_SAFE_DATA_ALLOW_UNCHECKED_COMMIT)
	zassert_equal(safe_data_commit(NULL, NULL, sizeof(buf), &crc, NULL),
		      -EINVAL);
#endif
}

/* ----------------------------------------------------------------------- *
 * Concurrency: two threads share one container. The mutex inside the
 * container must serialise the producer's read-modify-write against the
 * consumer's reads, so the consumer never observes a torn (internally
 * inconsistent) snapshot. The producer deliberately writes the dependent
 * fields one at a time with yields between them to widen the race window.
 * ----------------------------------------------------------------------- */
struct cc_payload {
	uint32_t seq;
	uint32_t a;
	uint32_t b;
	uint32_t sum; /* invariant: sum == a + b */
};

SAFE_CONTAINER_DEFINE(safe_cc, struct cc_payload);
static struct safe_cc cc;

#define CC_ITERS 1000

static atomic_t cc_breaks;
static atomic_t cc_reads;

static int cc_advance(void *payload, void *user)
{
	struct cc_payload *s = payload;
	uint32_t n = ++s->seq;

	ARG_UNUSED(user);

	s->a = n;
	k_yield(); /* momentarily inconsistent - but under the lock */
	s->b = n * 3U;
	k_yield();
	s->sum = s->a + s->b;
	return 0;
}

static K_THREAD_STACK_DEFINE(cc_prod_stack, 2048);
static K_THREAD_STACK_DEFINE(cc_cons_stack, 2048);
static struct k_thread cc_prod;
static struct k_thread cc_cons;

static void cc_producer(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

	for (int i = 0; i < CC_ITERS; i++) {
		(void)SAFE_UPDATE(&cc, cc_advance, NULL);
		k_yield();
	}
}

static void cc_consumer(void *a, void *b, void *c)
{
	struct cc_payload snap;

	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

	for (int i = 0; i < CC_ITERS; i++) {
		if (SAFE_READ(&cc, &snap) == 0) {
			if (snap.sum != snap.a + snap.b) {
				atomic_inc(&cc_breaks);
			}
			atomic_inc(&cc_reads);
		}
		k_yield();
	}
}

/**
 * @brief Concurrent access never yields torn snapshots
 *
 * A producer mutating dependent fields under SAFE_UPDATE and a consumer using
 * SAFE_READ run concurrently for many iterations; the consumer never observes
 * a snapshot violating the payload invariant.
 *
 * @testid{TC_SAFE_DATA_CONCURRENT_ACCESS_IS_SERIALISED}
 * @verifies SD-REQ-016
 * @active
 */
ZTEST(safe_data, test_concurrent_access_is_serialised)
{
	struct cc_payload init = {0};

	zassert_ok(SAFE_INIT(&cc));
	zassert_ok(SAFE_WRITE(&cc, &init));
	atomic_clear(&cc_breaks);
	atomic_clear(&cc_reads);

	k_thread_create(&cc_prod, cc_prod_stack,
			K_THREAD_STACK_SIZEOF(cc_prod_stack), cc_producer,
			NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	k_thread_create(&cc_cons, cc_cons_stack,
			K_THREAD_STACK_SIZEOF(cc_cons_stack), cc_consumer,
			NULL, NULL, NULL, 5, 0, K_NO_WAIT);

	zassert_ok(k_thread_join(&cc_prod, K_FOREVER));
	zassert_ok(k_thread_join(&cc_cons, K_FOREVER));

	zassert_true(atomic_get(&cc_reads) > 0, "consumer made no reads");
	zassert_equal(atomic_get(&cc_breaks), 0,
		      "lock failed to serialise: a torn snapshot was observed");
}

/* ----------------------------------------------------------------------- *
 * Diagnostics: self-test, statistics, fault-handler events, contract
 * enforcement, bounded locking.
 * ----------------------------------------------------------------------- */

/**
 * @brief Self-test reports a healthy mechanism
 *
 * safe_data_selftest() passes on a healthy system: CRC known-answer test plus
 * a seal/corrupt/detect(/recover) round trip on a scratch container.
 *
 * @testid{TC_SAFE_DATA_SELFTEST}
 * @verifies SD-REQ-017
 * @kconfig_depends{CONFIG_SAFE_DATA_SELFTEST}
 * @active
 */
ZTEST(safe_data, test_selftest)
{
#if defined(CONFIG_SAFE_DATA_SELFTEST)
	zassert_ok(safe_data_selftest(), "integrity mechanism must be healthy");
#else
	ztest_test_skip();
#endif
}

/**
 * @brief Statistics count every integrity event
 *
 * Recovered-payload, recovered-tag and unrecoverable detections each increment
 * their dedicated counter exactly once per detection.
 *
 * @testid{TC_SAFE_DATA_STATS_COUNT_EVENTS}
 * @verifies SD-REQ-012
 * @kconfig_depends{CONFIG_SAFE_DATA_STATS}
 * @active
 */
ZTEST(safe_data, test_stats_count_events)
{
#if defined(CONFIG_SAFE_DATA_STATS)
	struct safe_data_stats st;
	struct payload out;

	safe_data_stats_reset();
	reset_to(1, 2, 3);

	if (IS_ENABLED(CONFIG_SAFE_DATA_REDUNDANT)) {
		/* Corrupt payload -> locked read repairs from the shadow. */
		sp.payload.a = 99;
		zassert_ok(SAFE_READ(&sp, &out));
		zassert_ok(safe_data_stats_get(&st));
		zassert_equal(st.recovered_payload, 1);

		/* Corrupt only the stored tag -> recovered by resealing. */
		sp._crc ^= 0xFFu;
		zassert_ok(SAFE_READ(&sp, &out));
		zassert_ok(safe_data_stats_get(&st));
		zassert_equal(st.recovered_tag, 1);
	}

	/* Unrecoverable corruption must be counted too. */
#if CONFIG_SAFE_DATA_REDUNDANT
	sp.payload.a ^= 0x1111u;
	sp._shadow[0] ^= 0xAAu;
#else
	sp.payload.a ^= 0x1111u;
#endif
	zassert_equal(SAFE_VERIFY(&sp), -EILSEQ);
	zassert_ok(safe_data_stats_get(&st));
	zassert_equal(st.unrecoverable, 1);
#else
	struct safe_data_stats st;

	zassert_equal(safe_data_stats_get(&st), -ENOTSUP);
	ztest_test_skip();
#endif
}

static atomic_t cb_count;
static struct safe_data_fault_info cb_last;

static void fault_event_cb(const struct safe_data_fault_info *info)
{
	cb_last = *info;
	atomic_inc(&cb_count);
}

static void fault_event_cb_other(const struct safe_data_fault_info *info)
{
	ARG_UNUSED(info);
}

/**
 * @brief Registered handler receives all event kinds
 *
 * safe_data_fault_handler_register() accepts one callback (second registration
 * is rejected with -EALREADY, re-registration is idempotent) and the callback
 * receives recovery events as well as unrecoverable faults with a populated
 * event descriptor.
 *
 * @testid{TC_SAFE_DATA_FAULT_HANDLER_EVENTS}
 * @verifies SD-REQ-013
 * @verifies SD-REQ-011
 * @active
 */
ZTEST(safe_data, test_fault_handler_events)
{
	atomic_clear(&cb_count);
	zassert_ok(safe_data_fault_handler_register(fault_event_cb));
	zassert_equal(safe_data_fault_handler_register(fault_event_cb_other),
		      -EALREADY, "second handler must be rejected");
	zassert_ok(safe_data_fault_handler_register(fault_event_cb),
		   "re-registering the same handler is idempotent");

	reset_to(4, 5, 6);

	if (IS_ENABLED(CONFIG_SAFE_DATA_REDUNDANT)) {
		struct payload out;

		/* Recovery must be observable, not silent. */
		sp.payload.a = 0xBAD;
		zassert_ok(SAFE_READ(&sp, &out));
		zassert_true(atomic_get(&cb_count) >= 1);
		zassert_equal(cb_last.kind,
			      SAFE_DATA_FAULT_KIND_RECOVERED_PAYLOAD);
	}

	/* Unrecoverable corruption must reach the handler too. */
#if CONFIG_SAFE_DATA_REDUNDANT
	sp.payload.a ^= 0x2222u;
	sp._shadow[1] ^= 0x55u;
#else
	sp.payload.a ^= 0x2222u;
#endif
	zassert_equal(SAFE_VERIFY(&sp), -EILSEQ);
	zassert_equal(cb_last.kind, SAFE_DATA_FAULT_KIND_UNRECOVERABLE);
	zassert_equal(cb_last.len, sizeof(struct payload));

	zassert_ok(safe_data_fault_handler_register(NULL), "unregister");
}

static int mut_positive(void *payload, void *user)
{
	struct payload *p = payload;

	ARG_UNUSED(user);
	p->a = 0xDEAD;
	return 7; /* contract violation: positive return */
}

/**
 * @brief Positive mutator return is clamped and aborted
 *
 * A mutator returning a positive value violates the contract: SAFE_UPDATE
 * returns -EINVAL, the transaction aborts (rollback with shadow), and the
 * violation is counted.
 *
 * @testid{TC_SAFE_DATA_UPDATE_CLAMPS_POSITIVE_MUTATOR_RETURN}
 * @verifies SD-REQ-008
 * @active
 */
ZTEST(safe_data, test_update_clamps_positive_mutator_return)
{
	reset_to(10, 0, 0);
	zassert_equal(SAFE_UPDATE(&sp, mut_positive, NULL), -EINVAL,
		      "positive mutator return must be clamped to -EINVAL");

	if (IS_ENABLED(CONFIG_SAFE_DATA_REDUNDANT)) {
		struct payload out;

		zassert_ok(SAFE_READ(&sp, &out), "clamp must abort + roll back");
		zassert_equal(out.a, 10);
	} else {
		/* No rollback buffer: the partial write is detectable. */
		zassert_equal(SAFE_VERIFY(&sp), -EILSEQ);
	}

#if defined(CONFIG_SAFE_DATA_STATS)
	{
		struct safe_data_stats st;

		zassert_ok(safe_data_stats_get(&st));
		zassert_true(st.mutator_clamped >= 1);
	}
#endif
}

/**
 * @brief Write observes corruption before overwriting
 *
 * With CONFIG_SAFE_DATA_WRITE_CHECKS_OLD a write over corrupted contents still
 * succeeds, but the destroyed evidence is counted in the statistics first.
 *
 * @testid{TC_SAFE_DATA_WRITE_OBSERVES_OVERWRITTEN_CORRUPTION}
 * @verifies SD-REQ-018
 * @kconfig_depends{CONFIG_SAFE_DATA_WRITE_CHECKS_OLD}
 * @kconfig_depends{CONFIG_SAFE_DATA_STATS}
 * @active
 */
ZTEST(safe_data, test_write_observes_overwritten_corruption)
{
#if defined(CONFIG_SAFE_DATA_WRITE_CHECKS_OLD) && defined(CONFIG_SAFE_DATA_STATS)
	struct payload v = { .a = 1, .b = 2, .c = 3 };
	struct safe_data_stats st;

	reset_to(100, 200, 30);
	safe_data_stats_reset();

	/* Corrupt the old contents, then overwrite: the write must succeed
	 * but the destroyed evidence must be counted.
	 */
	sp.payload.a = 0xBADF00D;
	zassert_ok(SAFE_WRITE(&sp, &v));
	zassert_ok(safe_data_stats_get(&st));
	zassert_equal(st.overwritten, 1,
		      "overwritten corruption must be observed");
	zassert_ok(SAFE_VERIFY(&sp));
#else
	ztest_test_skip();
#endif
}

#if CONFIG_SAFE_DATA_LOCK_TIMEOUT_MS > 0 && defined(CONFIG_SAFE_DATA_LOCKING)
static int mut_hold(void *payload, void *user)
{
	ARG_UNUSED(payload);
	ARG_UNUSED(user);
	k_sleep(K_MSEC(10 * CONFIG_SAFE_DATA_LOCK_TIMEOUT_MS));
	return 0;
}

static K_THREAD_STACK_DEFINE(holder_stack, 2048);
static struct k_thread holder_thread;

static void holder_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	(void)SAFE_UPDATE(&sp, mut_hold, NULL);
}
#endif

/**
 * @brief Blocked access fails within the lock bound
 *
 * While another thread holds the container lock, an access fails with
 * -ETIMEDOUT within CONFIG_SAFE_DATA_LOCK_TIMEOUT_MS and the timeout is
 * counted; the container is usable again after the holder releases.
 *
 * @testid{TC_SAFE_DATA_LOCK_TIMEOUT_IS_BOUNDED_AND_REPORTED}
 * @verifies SD-REQ-014
 * @kconfig_depends{CONFIG_SAFE_DATA_LOCK_TIMEOUT_MS > 0}
 * @kconfig_depends{CONFIG_SAFE_DATA_LOCKING}
 * @active
 */
ZTEST(safe_data, test_lock_timeout_is_bounded_and_reported)
{
#if CONFIG_SAFE_DATA_LOCK_TIMEOUT_MS > 0 && defined(CONFIG_SAFE_DATA_LOCKING)
	struct payload out;

	reset_to(1, 1, 1);

	k_thread_create(&holder_thread, holder_stack,
			K_THREAD_STACK_SIZEOF(holder_stack), holder_fn,
			NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	/* Let the holder take the container lock. */
	k_sleep(K_MSEC(CONFIG_SAFE_DATA_LOCK_TIMEOUT_MS));

	zassert_equal(SAFE_READ(&sp, &out), -ETIMEDOUT,
		      "blocked access must fail within the configured bound");

#if defined(CONFIG_SAFE_DATA_STATS)
	{
		struct safe_data_stats st;

		zassert_ok(safe_data_stats_get(&st));
		zassert_true(st.lock_timeouts >= 1,
			     "the timeout must be observable");
	}
#endif

	zassert_ok(k_thread_join(&holder_thread, K_FOREVER));
	zassert_ok(SAFE_READ(&sp, &out), "container must be usable again");
#else
	ztest_test_skip(); /* needs CONFIG_SAFE_DATA_LOCK_TIMEOUT_MS > 0 */
#endif
}

/**
 * @defgroup safe_data_module Safe Data Test Application
 * @ingroup safe_data_tests
 * @brief Unit-test application for the Safe Data API (tests/safe_data).
 */

/**
 * @addtogroup safe_data
 * @ingroup safe_data_module
 */
ZTEST_SUITE(safe_data, NULL, NULL, NULL, NULL, NULL);
