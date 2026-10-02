/*
 * Copyright (c) 2026 The safety-toolbox contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * producer_consumer - two threads sharing one protected struct.
 *
 * This is the scenario the mutex inside a safe container exists for: a producer
 * thread mutates a multi-field struct that has an *internal invariant*
 * (sum == x + y), while a consumer thread reads it concurrently. Because every
 * mutation runs under the container lock (SAFE_UPDATE holds it across the whole
 * read-modify-write) and every read verifies + copies under the same lock
 * (SAFE_READ), the consumer can never observe a torn, half-updated struct: each
 * snapshot it gets is internally consistent and CRC-valid.
 *
 * To make that guarantee visible, the producer deliberately writes the fields
 * one at a time *inside* the transaction (with a tiny yield between them, so the
 * window for a race is wide). Without serialisation a reader would frequently
 * catch sum != x + y; with the safe container it never does. The consumer
 * counts any invariant violation and the run FAILs if the count is non-zero.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <safe_data/safe_data.h>

LOG_MODULE_REGISTER(producer_consumer, LOG_LEVEL_INF);

/* Internal invariant the consumer relies on: sum == x + y, and all three fields
 * belong to the same generation (seq).
 */
struct shared {
	uint32_t seq;
	uint32_t x;
	uint32_t y;
	uint32_t sum;
};

SAFE_CONTAINER_DEFINE(safe_shared, struct shared);
static struct safe_shared shr;

#define ITERATIONS 2000

/* Stats the consumer reports back to main. */
static atomic_t reads_done;
static atomic_t invariant_breaks;

#define STACK_SIZE 2048
#define PRODUCER_PRIO 5
#define CONSUMER_PRIO 5

static K_THREAD_STACK_DEFINE(producer_stack, STACK_SIZE);
static K_THREAD_STACK_DEFINE(consumer_stack, STACK_SIZE);
static struct k_thread producer_thread;
static struct k_thread consumer_thread;

/*
 * Mutator run with the lock held by SAFE_UPDATE. It writes the three dependent
 * fields one at a time and yields between them: the struct is momentarily
 * inconsistent (sum != x + y) *inside* this function, but the lock guarantees no
 * reader can observe that intermediate state.
 */
static int advance(void *payload, void *user)
{
	struct shared *s = payload;
	uint32_t n = ++s->seq;

	ARG_UNUSED(user);

	s->x = n;
	k_yield();          /* widen the inconsistency window on purpose */
	s->y = n * 10U;
	k_yield();
	s->sum = s->x + s->y;
	return 0;
}

static void producer_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (int i = 0; i < ITERATIONS; i++) {
		(void)SAFE_UPDATE(&shr, advance, NULL);
		k_yield();
	}
}

static void consumer_fn(void *a, void *b, void *c)
{
	struct shared snap;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (int i = 0; i < ITERATIONS; i++) {
		if (SAFE_READ(&shr, &snap) == 0) {
			/* The whole point: this snapshot must be internally
			 * consistent, never a torn write.
			 */
			if (snap.sum != snap.x + snap.y) {
				atomic_inc(&invariant_breaks);
			}
			atomic_inc(&reads_done);
		}
		k_yield();
	}
}

int main(void)
{
	struct shared init = { 0 };
	struct shared final_snap;
	long breaks;

	LOG_INF("=== Safe API: producer_consumer (two threads, one struct) ===");

	SAFE_INIT(&shr);
	(void)SAFE_WRITE(&shr, &init);

	k_thread_create(&producer_thread, producer_stack, STACK_SIZE,
			producer_fn, NULL, NULL, NULL,
			PRODUCER_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&producer_thread, "producer");

	k_thread_create(&consumer_thread, consumer_stack, STACK_SIZE,
			consumer_fn, NULL, NULL, NULL,
			CONSUMER_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&consumer_thread, "consumer");

	k_thread_join(&producer_thread, K_FOREVER);
	k_thread_join(&consumer_thread, K_FOREVER);

	(void)SAFE_READ(&shr, &final_snap);
	breaks = atomic_get(&invariant_breaks);

	LOG_INF("producer ran %d transactions; final seq=%u x=%u y=%u sum=%u",
		ITERATIONS, final_snap.seq, final_snap.x, final_snap.y,
		final_snap.sum);
	LOG_INF("consumer validated %ld snapshots", atomic_get(&reads_done));

	if (breaks == 0) {
		LOG_INF("invariant (sum == x + y) held on every snapshot -> "
			"the lock serialised all access; no torn reads");
		LOG_INF("=== done: PASS ===");
		return 0;
	}

	LOG_ERR("invariant broken on %ld snapshots -> a torn update leaked",
		breaks);
	LOG_ERR("=== done: FAIL ===");
	return 1;
}
