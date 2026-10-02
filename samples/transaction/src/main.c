/*
 * Copyright (c) 2026 The safety-toolbox contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * transaction - atomic read-modify-write and shadow-copy recovery.
 *
 * Demonstrates the two ergonomic, race-free ways to mutate protected data:
 *   1. SAFE_UPDATE() - a callback transaction (lock held across the whole RMW).
 *   2. SAFE_SECTION() - a scoped block with a typed payload pointer; the tag is
 *      resealed and the lock released automatically at the end of the block.
 *
 * Then it corrupts one copy of the data and shows verify recovering it from the
 * redundant shadow copy (CONFIG_SAFE_DATA_REDUNDANT=y).
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <safe_data/safe_data.h>

LOG_MODULE_REGISTER(transaction, LOG_LEVEL_INF);

struct counters {
	uint32_t events;
	uint32_t errors;
	int32_t  balance;
};

SAFE_CONTAINER_DEFINE(safe_counters, struct counters);
static struct safe_counters ctr;

/* Mutator for SAFE_UPDATE: applied atomically with the lock held. */
static int on_event(void *payload, void *user)
{
	struct counters *c = payload;
	int delta = *(const int *)user;

	c->events++;
	c->balance += delta;
	if (c->balance < 0) {
		c->errors++;
		return -ERANGE; /* abort: leaves payload + tag untouched */
	}
	return 0;
}

int main(void)
{
	struct counters snap;
	int delta;
	int ret;

	LOG_INF("=== Safe API: transaction ===");
	SAFE_INIT(&ctr);

	/* 1. Callback transaction. */
	delta = 100;
	ret = SAFE_UPDATE(&ctr, on_event, &delta);
	LOG_INF("update(+100) -> %d", ret);

	/* 2. Scoped section: plain-C access, auto reseal + unlock at block end. */
	SAFE_SECTION(&ctr, c) {
		c->events++;
		c->balance += 50;
	}

	/* 3. An aborting transaction does not corrupt the data. */
	delta = -10000;
	ret = SAFE_UPDATE(&ctr, on_event, &delta);
	LOG_INF("update(-10000) -> %d (expected %d; balance unchanged)",
		ret, -ERANGE);

	(void)SAFE_READ(&ctr, &snap);
	LOG_INF("state: events=%u errors=%u balance=%d",
		snap.events, snap.errors, snap.balance);

	/* 4. Corrupt the live payload directly, then read it back. With the
	 * redundant shadow enabled, verify repairs it from the good copy.
	 */
	LOG_INF("--- corrupting live payload ---");
	ctr.payload.balance = 0x7FFFFFFF;

	ret = SAFE_READ(&ctr, &snap);
	if (ret == 0) {
		LOG_INF("recovered: balance restored to %d", snap.balance);
	} else {
		LOG_ERR("unrecoverable corruption: %d", ret);
	}

	LOG_INF("=== done ===");
	return 0;
}
