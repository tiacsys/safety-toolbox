/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Type-erased core for the Safe Data API. All the integrity logic (CRC choice,
 * redundancy/recovery, fault reaction) lives here in exactly one place so it can
 * be audited once; the typed macros in safe_data.h merely forward to it.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/sys/crc.h>
#include <zephyr/logging/log.h>

#include <safe_data/safe_data.h>

LOG_MODULE_REGISTER(safe_data, CONFIG_SAFE_DATA_LOG_LEVEL);

/* --- CRC backend selection (default to CRC32-IEEE if Kconfig absent) ------
 * Exposed as a seed + chainable update pair so the tag can also be computed
 * over data that is transformed on the fly (see safe_tag_inverted()).
 */
#if defined(CONFIG_SAFE_DATA_CRC16_CCITT)
#define SAFE_TAG_SEED 0xFFFFu
static inline uint32_t safe_tag_update(uint32_t c, const uint8_t *p, size_t len)
{
	return crc16_ccitt((uint16_t)c, p, len);
}
#elif defined(CONFIG_SAFE_DATA_CRC8_CCITT)
#define SAFE_TAG_SEED 0xFFu
static inline uint32_t safe_tag_update(uint32_t c, const uint8_t *p, size_t len)
{
	return crc8_ccitt((uint8_t)c, p, len);
}
#else /* CONFIG_SAFE_DATA_CRC32_IEEE or standalone default */
#define SAFE_TAG_SEED 0x0u
static inline uint32_t safe_tag_update(uint32_t c, const uint8_t *p, size_t len)
{
	return crc32_ieee_update(c, p, len);
}
#endif

static inline uint32_t safe_tag(const void *p, size_t len)
{
	return safe_tag_update(SAFE_TAG_SEED, p, len);
}

/* Tag of the payload the shadow implies (~shadow), computed chunk-wise so the
 * payload itself is never touched. Lets verification stay read-only.
 */
static uint32_t safe_tag_inverted(const void *shadow, size_t len)
{
	const uint8_t *sh = shadow;
	uint32_t c = SAFE_TAG_SEED;
	uint8_t tmp[32];

	while (len > 0) {
		size_t n = MIN(len, sizeof(tmp));

		for (size_t i = 0; i < n; i++) {
			tmp[i] = (uint8_t)~sh[i];
		}
		c = safe_tag_update(c, tmp, n);
		sh += n;
		len -= n;
	}

	return c;
}

/* Does the live payload still match the shadow (payload == ~shadow)? */
static bool payload_matches_shadow(const void *payload, const void *shadow,
				   size_t len)
{
	const uint8_t *pl = payload;
	const uint8_t *sh = shadow;

	for (size_t i = 0; i < len; i++) {
		if (pl[i] != (uint8_t)~sh[i]) {
			return false;
		}
	}

	return true;
}

/* --- Fault/diagnostic event sink (centralised policy) --------------------- */
__weak void safe_data_fault_handler(const void *payload, size_t len)
{
	LOG_ERR("integrity fault: payload %p, %zu bytes", payload, len);
}

static safe_data_fault_cb_t fault_cb;

int safe_data_fault_handler_register(safe_data_fault_cb_t cb)
{
	if (cb != NULL && fault_cb != NULL && fault_cb != cb) {
		return -EALREADY;
	}
	fault_cb = cb;

	return 0;
}

#define SAFE_DATA_KIND_COUNT (SAFE_DATA_FAULT_KIND_MUTATOR_CLAMPED + 1)

#if defined(CONFIG_SAFE_DATA_STATS)
static atomic_t stat_counters[SAFE_DATA_KIND_COUNT];

int safe_data_stats_get(struct safe_data_stats *out)
{
	if (out == NULL) {
		return -EINVAL;
	}

	out->unrecoverable = (uint32_t)atomic_get(
		&stat_counters[SAFE_DATA_FAULT_KIND_UNRECOVERABLE]);
	out->recovered_tag = (uint32_t)atomic_get(
		&stat_counters[SAFE_DATA_FAULT_KIND_RECOVERED_TAG]);
	out->recovered_payload = (uint32_t)atomic_get(
		&stat_counters[SAFE_DATA_FAULT_KIND_RECOVERED_PAYLOAD]);
	out->lock_timeouts = (uint32_t)atomic_get(
		&stat_counters[SAFE_DATA_FAULT_KIND_LOCK_TIMEOUT]);
	out->overwritten = (uint32_t)atomic_get(
		&stat_counters[SAFE_DATA_FAULT_KIND_OVERWRITTEN]);
	out->mutator_clamped = (uint32_t)atomic_get(
		&stat_counters[SAFE_DATA_FAULT_KIND_MUTATOR_CLAMPED]);

	return 0;
}

void safe_data_stats_reset(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(stat_counters); i++) {
		atomic_clear(&stat_counters[i]);
	}
}
#else /* !CONFIG_SAFE_DATA_STATS */
int safe_data_stats_get(struct safe_data_stats *out)
{
	if (out == NULL) {
		return -EINVAL;
	}

	return -ENOTSUP;
}

void safe_data_stats_reset(void)
{
}
#endif /* CONFIG_SAFE_DATA_STATS */

/* Set during the self-test so its deliberate corruption neither pollutes the
 * statistics nor triggers the configured fault reaction. Only written by
 * safe_data_selftest(), which is meant to run from boot/diagnostic context.
 */
static volatile bool event_mute;

/* Every integrity event funnels through here: statistics, the registered
 * callback (all kinds), and the configured control-flow reaction
 * (unrecoverable faults only).
 *
 * NOTE: may run while the affected container's lock is held - the callback
 * contract (see safe_data_fault_cb_t) forbids blocking or touching containers.
 */
static void safe_event(enum safe_data_fault_kind kind, const void *payload,
		       size_t len)
{
	if (event_mute) {
		return;
	}

#if defined(CONFIG_SAFE_DATA_STATS)
	atomic_inc(&stat_counters[kind]);
#endif

	if (fault_cb != NULL) {
		const struct safe_data_fault_info info = {
			.kind = kind,
			.payload = payload,
			.len = len,
		};

		fault_cb(&info);
	}

	if (kind == SAFE_DATA_FAULT_KIND_UNRECOVERABLE) {
		if (IS_ENABLED(CONFIG_SAFE_DATA_FAULT_PANIC)) {
			LOG_ERR("integrity fault -> k_panic()");
			k_panic();
		} else if (IS_ENABLED(CONFIG_SAFE_DATA_FAULT_HANDLER) &&
			   fault_cb == NULL) {
			safe_data_fault_handler(payload, len);
		}
		/* CONFIG_SAFE_DATA_FAULT_RETURN: caller inspects the code. */
	}
}

/* Lock-timeout reporting hook for the SAFE_SECTION macro. */
void z_safe_data_lock_failed(void)
{
	safe_event(SAFE_DATA_FAULT_KIND_LOCK_TIMEOUT, NULL, 0);
}

/* --- Sealing (write the integrity tag, refresh the shadow copy) ----------- */
void z_safe_data_seal(void *payload, size_t len, uint32_t *crc, void *shadow)
{
	*crc = safe_tag(payload, len);

	if (IS_ENABLED(CONFIG_SAFE_DATA_REDUNDANT) && shadow != NULL) {
		const uint8_t *src = payload;
		uint8_t *dst = shadow;

		for (size_t i = 0; i < len; i++) {
			dst[i] = (uint8_t)~src[i];
		}
	}
}

/* ------------------------------------------------------------------------- */

int safe_data_init(struct k_mutex *lock, void *payload, size_t len,
		   uint32_t *crc, void *shadow)
{
	if (payload == NULL || crc == NULL || len == 0) {
		return -EINVAL;
	}

	if (lock != NULL) {
		k_mutex_init(lock);
	}
	z_safe_data_seal(payload, len, crc, shadow);

	return 0;
}

/* Pure diagnosis with no side effects at all (no events, no logging). The
 * public entry points wrap this and report exactly one event per detection.
 */
static int verify_quiet(const void *payload, size_t len, uint32_t crc,
			const void *shadow)
{
	if (payload == NULL || len == 0) {
		return -EINVAL;
	}

	if (safe_tag(payload, len) == crc) {
		return 0; /* primary copy intact */
	}

	/* Primary CRC mismatch - is the fault recoverable from the shadow copy?
	 * The actual in-place repair happens in safe_data_verify_repair(),
	 * under the container lock.
	 */
	if (IS_ENABLED(CONFIG_SAFE_DATA_REDUNDANT) && shadow != NULL) {
		if (payload_matches_shadow(payload, shadow, len)) {
			/* Payload and shadow are consistent => only the stored
			 * integrity tag was corrupted. The data itself is fine.
			 */
			return SAFE_DATA_RECOVERED;
		}

		/* They disagree: does the payload the shadow implies (~shadow)
		 * match the stored tag? If so, the shadow is the good copy.
		 * (If not, the data is lost either way.)
		 */
		if (safe_tag_inverted(shadow, len) == crc) {
			return SAFE_DATA_RECOVERED;
		}
	}

	return -EILSEQ;
}

int safe_data_verify(const void *payload, size_t len, uint32_t crc,
		     const void *shadow)
{
	int ret = verify_quiet(payload, len, crc, shadow);

	if (ret == -EILSEQ) {
		LOG_ERR("corruption detected; unrecoverable");
		safe_event(SAFE_DATA_FAULT_KIND_UNRECOVERABLE, payload, len);
	}

	return ret;
}

int safe_data_verify_repair(void *payload, size_t len, uint32_t crc,
			    void *shadow)
{
	int ret = verify_quiet(payload, len, crc, shadow);

	if (ret == -EILSEQ) {
		LOG_ERR("payload and shadow both corrupt; unrecoverable");
		safe_event(SAFE_DATA_FAULT_KIND_UNRECOVERABLE, payload, len);
		return ret;
	}
	if (ret != SAFE_DATA_RECOVERED) {
		return ret; /* 0 or -EINVAL */
	}

	/* Recoverable fault: repair the payload in place. Only reached with
	 * redundancy enabled and a shadow present (verify_quiet cannot return
	 * SAFE_DATA_RECOVERED otherwise). The caller reseals the stored tag to
	 * make the repair durable.
	 */
	if (payload_matches_shadow(payload, shadow, len)) {
		LOG_WRN("integrity tag stale; data consistent, will reseal");
		safe_event(SAFE_DATA_FAULT_KIND_RECOVERED_TAG, payload, len);
	} else {
		const uint8_t *sh = shadow;
		uint8_t *pl = payload;

		for (size_t i = 0; i < len; i++) {
			pl[i] = (uint8_t)~sh[i];
		}
		LOG_WRN("payload corrupt; recovered from shadow copy");
		safe_event(SAFE_DATA_FAULT_KIND_RECOVERED_PAYLOAD, payload, len);
	}

	return SAFE_DATA_RECOVERED;
}

int safe_data_read(struct k_mutex *lock, void *payload, size_t len,
		   uint32_t *crc, void *shadow, void *out)
{
	int ret;

	if (payload == NULL || crc == NULL || out == NULL || len == 0) {
		return -EINVAL;
	}

	ret = z_safe_lock(lock);
	if (ret != 0) {
		safe_event(SAFE_DATA_FAULT_KIND_LOCK_TIMEOUT, payload, len);
		return ret;
	}
	ret = safe_data_verify_repair(payload, len, *crc, shadow);
	if (ret == SAFE_DATA_RECOVERED) {
		/* Make the recovery durable (repairs a stale stored tag). */
		z_safe_data_seal(payload, len, crc, shadow);
		ret = 0;
	}
	if (ret == 0) {
		memcpy(out, payload, len);
	}
	z_safe_unlock(lock);

	return ret;
}

int safe_data_write(struct k_mutex *lock, void *payload, size_t len,
		    uint32_t *crc, void *shadow, const void *in)
{
	int ret;

	if (payload == NULL || crc == NULL || in == NULL || len == 0) {
		return -EINVAL;
	}

	ret = z_safe_lock(lock);
	if (ret != 0) {
		safe_event(SAFE_DATA_FAULT_KIND_LOCK_TIMEOUT, payload, len);
		return ret;
	}

	/* Observe-only pre-check: overwriting destroys the evidence of any
	 * pre-existing corruption, so report it first. Does not change the
	 * outcome of the write.
	 */
	if (IS_ENABLED(CONFIG_SAFE_DATA_WRITE_CHECKS_OLD) &&
	    verify_quiet(payload, len, *crc, shadow) != 0) {
		LOG_WRN("overwriting corrupt contents");
		safe_event(SAFE_DATA_FAULT_KIND_OVERWRITTEN, payload, len);
	}

	memcpy(payload, in, len);
	z_safe_data_seal(payload, len, crc, shadow);
	z_safe_unlock(lock);

	return 0;
}

int safe_data_update(struct k_mutex *lock, void *payload, size_t len,
		     uint32_t *crc, void *shadow,
		     safe_data_mutator_t fn, void *user)
{
	int ret;

	if (payload == NULL || crc == NULL || fn == NULL || len == 0) {
		return -EINVAL;
	}

	ret = z_safe_lock(lock);
	if (ret != 0) {
		safe_event(SAFE_DATA_FAULT_KIND_LOCK_TIMEOUT, payload, len);
		return ret;
	}

	ret = safe_data_verify_repair(payload, len, *crc, shadow);
	if (ret == SAFE_DATA_RECOVERED) {
		/* Pre-check repaired the data in place; make it durable. */
		z_safe_data_seal(payload, len, crc, shadow);
		ret = 0;
	}

	if (ret == 0) {
		int mret = fn(payload, user);

		if (mret > 0) {
			/* Contract violation: only 0 (commit) or a negative
			 * errno (abort) are legal. Treat as an abort so a
			 * confused mutator can never commit by accident.
			 */
			LOG_WRN("mutator returned %d; clamped to -EINVAL", mret);
			safe_event(SAFE_DATA_FAULT_KIND_MUTATOR_CLAMPED,
				   payload, len);
			mret = -EINVAL;
		}

		if (mret == 0) {
			/* Commit: reseal tag (and refresh shadow). */
			z_safe_data_seal(payload, len, crc, shadow);
		} else {
			/* Abort: roll back any partial writes from the shadow
			 * copy so the container is never left inconsistent. The
			 * stored tag already matches the rolled-back payload.
			 *
			 * Without a shadow copy there is no rollback buffer; the
			 * contract is then that a mutator must validate up front
			 * and not modify the payload before returning an error.
			 */
			if (IS_ENABLED(CONFIG_SAFE_DATA_REDUNDANT) &&
			    shadow != NULL) {
				const uint8_t *sh = shadow;
				uint8_t *pl = payload;

				for (size_t i = 0; i < len; i++) {
					pl[i] = (uint8_t)~sh[i];
				}
			}
			ret = mret;
		}
	}

	z_safe_unlock(lock);

	return ret;
}

#if defined(CONFIG_SAFE_DATA_ALLOW_UNCHECKED_COMMIT)
int safe_data_commit(struct k_mutex *lock, void *payload, size_t len,
		     uint32_t *crc, void *shadow)
{
	int ret;

	if (payload == NULL || crc == NULL || len == 0) {
		return -EINVAL;
	}

	ret = z_safe_lock(lock);
	if (ret != 0) {
		safe_event(SAFE_DATA_FAULT_KIND_LOCK_TIMEOUT, payload, len);
		return ret;
	}
	z_safe_data_seal(payload, len, crc, shadow);
	z_safe_unlock(lock);

	return 0;
}
#endif /* CONFIG_SAFE_DATA_ALLOW_UNCHECKED_COMMIT */

/* --- Self-test of the integrity mechanism itself (B7) ---------------------- */
#if defined(CONFIG_SAFE_DATA_SELFTEST)

/* Known-answer for the standard "123456789" check input, per backend, against
 * the exact Zephyr implementation (crc8_ccitt: poly 0x07 MSB-first, seed 0xFF;
 * crc16_ccitt: reflected poly 0x8408, seed 0xFFFF; crc32_ieee: reflected).
 */
#if defined(CONFIG_SAFE_DATA_CRC16_CCITT)
#define SAFE_DATA_KAT_EXPECTED 0x6F91u
#elif defined(CONFIG_SAFE_DATA_CRC8_CCITT)
#define SAFE_DATA_KAT_EXPECTED 0xFBu
#else /* CRC32-IEEE */
#define SAFE_DATA_KAT_EXPECTED 0xCBF43926u
#endif

int safe_data_selftest(void)
{
	static const uint8_t kat[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
	uint8_t buf[8] = {0xA5, 0x5A, 0x01, 0xFE, 0x10, 0x20, 0x40, 0x80};
	uint8_t shadow[sizeof(buf)];
	void *sh = IS_ENABLED(CONFIG_SAFE_DATA_REDUNDANT) ? shadow : NULL;
	uint32_t crc = 0;
	int ret = 0;

	/* The deliberate corruption below must not pollute the statistics or
	 * trigger the configured fault reaction.
	 */
	event_mute = true;

	/* 1. Known-answer test: a broken CRC backend (e.g. corrupted lookup
	 * table) is the one fault everything else cannot detect.
	 */
	if (safe_tag(kat, sizeof(kat)) != SAFE_DATA_KAT_EXPECTED) {
		ret = -EFAULT;
	}

	/* 2. Seal + verify round trip on a scratch container. */
	if (ret == 0) {
		z_safe_data_seal(buf, sizeof(buf), &crc, sh);
		if (verify_quiet(buf, sizeof(buf), crc, sh) != 0) {
			ret = -EFAULT;
		}
	}

	/* 3. Detection: a single flipped bit must change the tag. */
	if (ret == 0) {
		buf[0] ^= 0x01u;
		if (safe_tag(buf, sizeof(buf)) == crc) {
			ret = -EFAULT;
		}
	}

	/* 4. Recovery (redundant only): the repair path must restore the
	 * corrupted payload from the shadow and verify clean again.
	 */
	if (ret == 0 && IS_ENABLED(CONFIG_SAFE_DATA_REDUNDANT)) {
		if (safe_data_verify_repair(buf, sizeof(buf), crc, sh) !=
			    SAFE_DATA_RECOVERED ||
		    buf[0] != 0xA5u ||
		    verify_quiet(buf, sizeof(buf), crc, sh) != 0) {
			ret = -EFAULT;
		}
	}

	event_mute = false;

	if (ret != 0) {
		LOG_ERR("integrity-mechanism self-test FAILED");
	}

	return ret;
}

#if defined(CONFIG_SAFE_DATA_SELFTEST_BOOT)
static int boot_selftest_result = -EAGAIN;

int safe_data_selftest_result(void)
{
	return boot_selftest_result;
}

static int safe_data_boot_selftest(void)
{
	boot_selftest_result = safe_data_selftest();
	if (boot_selftest_result != 0) {
		/* A broken integrity mechanism means no protected data can be
		 * trusted: that is unconditionally unrecoverable.
		 */
		k_panic();
	}

	return boot_selftest_result;
}
SYS_INIT(safe_data_boot_selftest, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
#else  /* !CONFIG_SAFE_DATA_SELFTEST_BOOT */
int safe_data_selftest_result(void)
{
	return -EAGAIN; /* not run */
}
#endif /* CONFIG_SAFE_DATA_SELFTEST_BOOT */

#endif /* CONFIG_SAFE_DATA_SELFTEST */
