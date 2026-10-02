/*
 * Copyright (c) 2026 The safety-toolbox contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Fault-reaction and misuse tests for the Safe Data API. Each scenario in
 * testcase.yaml selects one CONFIG_SAFE_DATA_FAULT_* reaction. These tests
 * cause an unrecoverable fault (a kernel panic in one scenario) or an
 * ISR-context call on purpose, so they live outside the main test suite.
 */

#include <zephyr/ztest.h>
#include <zephyr/ztest_error_hook.h>
#include <zephyr/irq_offload.h>
#include <string.h>

#include <safe_data/safe_data.h>

struct payload {
	uint32_t a;
	uint32_t b;
};

SAFE_CONTAINER_DEFINE(safe_payload, struct payload);

static struct safe_payload sp;

static void seal(uint32_t a, uint32_t b)
{
	struct payload v = { .a = a, .b = b };

	zassert_ok(SAFE_INIT(&sp));
	zassert_ok(SAFE_WRITE(&sp, &v));
}

/* ----------------------------------------------------------------------- *
 * Assertion recorder. CONFIG_ASSERT_TEST lets the post action return, so a
 * test can see which assertion fired first and in which context. Any other
 * test checks that no assertion fired at all.
 * ----------------------------------------------------------------------- */
static volatile int assert_count;
static const char *volatile assert_file;
static volatile bool assert_in_isr;

#ifdef CONFIG_ASSERT_NO_FILE_INFO
void assert_post_action(void)
{
	const char *file = NULL;
#else
void assert_post_action(const char *file, unsigned int line)
{
	ARG_UNUSED(line);
#endif
	if (assert_count++ == 0) {
		assert_file = file;
		assert_in_isr = k_is_in_isr();
	}
}

/* ----------------------------------------------------------------------- *
 * Reaction HANDLER: this strong definition replaces the library's weak
 * safe_data_fault_handler().
 * ----------------------------------------------------------------------- */
static atomic_t handler_calls;
static const void *volatile handler_payload;
static volatile size_t handler_len;

void safe_data_fault_handler(const void *payload, size_t len)
{
	handler_payload = payload;
	handler_len = len;
	atomic_inc(&handler_calls);
}

#if defined(CONFIG_SAFE_DATA_FAULT_HANDLER)
static atomic_t cb_unrecoverable;

static void fault_cb(const struct safe_data_fault_info *info)
{
	if (info->kind == SAFE_DATA_FAULT_KIND_UNRECOVERABLE) {
		atomic_inc(&cb_unrecoverable);
	}
}
#endif

/* ----------------------------------------------------------------------- *
 * Reaction PANIC: the fault happens in a helper thread. The ztest fatal
 * hook accepts one expected panic from that thread; the kernel then aborts
 * the thread, and the test thread joins it.
 * ----------------------------------------------------------------------- */
#if defined(CONFIG_SAFE_DATA_FAULT_PANIC)
static volatile bool panic_caught;
static volatile bool returned_after_fault;

void ztest_post_fatal_error_hook(unsigned int reason,
				 const struct arch_esf *esf)
{
	ARG_UNUSED(esf);
	panic_caught = (reason == K_ERR_KERNEL_PANIC);
}

static K_THREAD_STACK_DEFINE(fault_stack, 2048);
static struct k_thread fault_thread;

static void fault_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

	ztest_set_fault_valid(true);
	(void)SAFE_VERIFY(&sp);
	/* Not reached when the reaction panics. */
	returned_after_fault = true;
	ztest_set_fault_valid(false);
}
#endif

/**
 * @brief Unrecoverable fault gets the configured reaction
 *
 * An unrecoverable corruption (no shadow copy) gets the reaction that
 * CONFIG_SAFE_DATA_FAULT_* selects: RETURN gives -EILSEQ and calls no
 * handler; HANDLER calls safe_data_fault_handler() with the affected payload
 * and then gives -EILSEQ, and a registered callback takes precedence over
 * that handler; PANIC halts the faulting thread with a kernel panic, and the
 * faulting call does not return.
 *
 * @testid{TC_SAFE_DATA_FAULT_REACTION}
 * @verifies SD-REQ-011
 * @active
 */
ZTEST(safe_data_fault, test_fault_reaction)
{
	atomic_clear(&handler_calls);
#if defined(CONFIG_SAFE_DATA_FAULT_HANDLER)
	atomic_clear(&cb_unrecoverable);
#endif
	assert_count = 0;

	seal(1, 2);
	sp.payload.a ^= 0x5A5A5A5Au; /* corrupt behind the API's back */

#if defined(CONFIG_SAFE_DATA_FAULT_PANIC)
	panic_caught = false;
	returned_after_fault = false;
	k_thread_create(&fault_thread, fault_stack,
			K_THREAD_STACK_SIZEOF(fault_stack), fault_fn,
			NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	zassert_ok(k_thread_join(&fault_thread, K_FOREVER));
	zassert_true(panic_caught, "PANIC: the fault must cause a kernel panic");
	zassert_false(returned_after_fault,
		      "PANIC: the faulting call must not return");
	zassert_equal(atomic_get(&handler_calls), 0,
		      "PANIC: the handler must not run");
#else
	zassert_equal(SAFE_VERIFY(&sp), -EILSEQ,
		      "the error code must reach the caller");
#if defined(CONFIG_SAFE_DATA_FAULT_HANDLER)
	zassert_equal(atomic_get(&handler_calls), 1,
		      "HANDLER: the handler must run once");
	zassert_equal_ptr(handler_payload, &sp.payload);
	zassert_equal(handler_len, sizeof(sp.payload));

	/* A registered callback takes precedence over the weak handler. */
	zassert_ok(safe_data_fault_handler_register(fault_cb));
	zassert_equal(SAFE_VERIFY(&sp), -EILSEQ);
	zassert_equal(atomic_get(&cb_unrecoverable), 1);
	zassert_equal(atomic_get(&handler_calls), 1,
		      "HANDLER: a registered callback replaces the handler");
	zassert_ok(safe_data_fault_handler_register(NULL));
#else
	zassert_equal(atomic_get(&handler_calls), 0,
		      "RETURN: the handler must not run");
#endif
#endif
	zassert_equal(assert_count, 0, "no assertion expected");
}

static volatile int isr_ret;

static void isr_read(const void *arg)
{
	struct payload out;

	ARG_UNUSED(arg);
	isr_ret = SAFE_READ(&sp, &out);
}

/**
 * @brief Locking call from an ISR is caught by an assertion
 *
 * A locked operation (SAFE_READ) runs in ISR context through irq_offload().
 * With CONFIG_ASSERT enabled, the first assertion that fires is the ISR
 * check in the Safe Data API header, not a kernel assertion further
 * down. The same call from thread context fires no assertion.
 *
 * @testid{TC_SAFE_DATA_LOCK_FROM_ISR_ASSERTS}
 * @verifies SD-REQ-015
 * @kconfig_depends{CONFIG_ASSERT}
 * @active
 */
ZTEST(safe_data_fault, test_lock_from_isr_asserts)
{
	struct payload out;

	seal(3, 4);

	assert_count = 0;
	zassert_ok(SAFE_READ(&sp, &out));
	zassert_equal(assert_count, 0, "thread context: no assertion");

	assert_file = NULL;
	assert_in_isr = false;
	irq_offload(isr_read, NULL);

	zassert_true(assert_count >= 1, "ISR context: the call must assert");
	zassert_true(assert_in_isr, "the assertion must fire in the ISR");
	zassert_not_null(assert_file);
	zassert_not_null(strstr(assert_file, "safe_data/safe_data.h"),
			 "the first assertion must be the Safe Data ISR check");
	assert_count = 0;
}

/**
 * @defgroup safe_data_fault_module Safe Data Fault-Reaction Test Application
 * @ingroup safe_data_tests
 * @brief Fault-reaction and misuse tests (tests/safe_data_fault).
 */

/**
 * @addtogroup safe_data_fault
 * @ingroup safe_data_fault_module
 */
ZTEST_SUITE(safe_data_fault, NULL, NULL, NULL, NULL, NULL);
