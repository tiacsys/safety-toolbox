/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Safe Data API - generic CRC-protected (optionally redundant) containers.
 *
 * A "safe container" bundles a user payload together with a mutex and an
 * integrity tag (CRC, plus an optional inverted shadow copy). All access goes
 * through a small set of functions/macros that hold the lock across the whole
 * read-modify-write and (re)seal the integrity tag automatically, so a stale
 * tag or a torn update cannot happen "by construction".
 *
 * The heavy lifting lives in a single type-erased core (safe_data.c); the
 * macros below only derive (&payload, sizeof(payload), &crc, &lock, shadow)
 * from a typed container and forward to that core.
 */

#ifndef SAFE_DATA_H_
#define SAFE_DATA_H_

#include <stdint.h>
#include <stddef.h>
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The library may be built standalone (e.g. for host unit testing) without the
 * Kconfig fragment present. Provide safe defaults so the header is usable
 * either way.
 */
#if !defined(CONFIG_SAFE_DATA_REDUNDANT)
#define CONFIG_SAFE_DATA_REDUNDANT 0
#endif

/* Locking defaults to on; standalone builds without the Kconfig fragment get a
 * mutex per container too. Set CONFIG_SAFE_DATA_LOCKING=n to drop the mutex when
 * the data is only ever accessed from a single context.
 */
#if !defined(CONFIG_SAFE_DATA_LOCKING) && !defined(CONFIG_SAFE_DATA)
#define CONFIG_SAFE_DATA_LOCKING 1
#endif

/* Remaining standalone defaults (Kconfig fragment absent): wait forever for
 * the lock, keep the full API surface.
 */
#if !defined(CONFIG_SAFE_DATA)
#if !defined(CONFIG_SAFE_DATA_LOCK_TIMEOUT_MS)
#define CONFIG_SAFE_DATA_LOCK_TIMEOUT_MS 0
#endif
#if !defined(CONFIG_SAFE_DATA_GNU_EXTENSIONS)
#define CONFIG_SAFE_DATA_GNU_EXTENSIONS 1
#endif
#if !defined(CONFIG_SAFE_DATA_ALLOW_UNCHECKED_COMMIT)
#define CONFIG_SAFE_DATA_ALLOW_UNCHECKED_COMMIT 1
#endif
#endif /* !CONFIG_SAFE_DATA */

/**
 * @defgroup safe_data_core_apis Type-Erased Core
 * @ingroup safe_data_apis
 * @brief Type-erased core operations; all integrity logic in one audited place.
 *
 * Application code should prefer the typed container layer
 * (@ref safe_data_container_apis) - the macros there only derive
 * `(&payload, sizeof(payload), &crc, &lock, shadow)` from a typed container
 * and forward to these functions.
 * @{
 */

/** @brief Return code for a fault that the redundant shadow copy covers.
 *
 * From safe_data_verify() (pure) it means the fault is *recoverable*: the
 * payload was NOT modified. From safe_data_verify_repair() it means the payload
 * has been repaired in place and is valid again once the tag is resealed.
 */
#define SAFE_DATA_RECOVERED (-EAGAIN)

/**
 * @brief Mutator callback used by safe_data_update() / SAFE_UPDATE().
 *
 * Invoked with the lock held and the payload already verified. Mutate
 * @p payload in place. Return 0 to commit (the integrity tag is resealed) or a
 * negative errno to abort (the payload is rolled back / left as-is and NOT
 * resealed). A positive return value is a contract violation: it is clamped to
 * -EINVAL, the transaction aborts, and the event is counted in the statistics.
 *
 * @param payload Pointer to the (verified) payload, cast to its real type.
 * @param user    Opaque user context forwarded from the call site.
 * @return 0 to commit, negative errno to abort the transaction.
 */
typedef int (*safe_data_mutator_t)(void *payload, void *user);

/** @} */ /* safe_data_core_apis */

/**
 * @defgroup safe_data_diag_apis Diagnostics and Fault Handling
 * @ingroup safe_data_apis
 * @brief Integrity-event reporting: fault handler, statistics, self-test.
 *
 * The observability interface a health monitor hooks into. Every integrity
 * event (recovery, unrecoverable corruption, lock timeout, overwritten
 * contents, clamped mutator return) is reported exactly once per detection.
 * @{
 */

/** @brief Kind of integrity event reported to the fault handler / statistics. */
enum safe_data_fault_kind {
	/** Corruption detected, no redundant copy could vouch for the data. */
	SAFE_DATA_FAULT_KIND_UNRECOVERABLE,
	/** Stored tag was stale/corrupt; payload+shadow consistent, resealed. */
	SAFE_DATA_FAULT_KIND_RECOVERED_TAG,
	/** Payload was corrupt; restored from the shadow copy. */
	SAFE_DATA_FAULT_KIND_RECOVERED_PAYLOAD,
	/** A container lock could not be acquired within the configured bound. */
	SAFE_DATA_FAULT_KIND_LOCK_TIMEOUT,
	/** Pre-write check found the old contents corrupt (observe-only). */
	SAFE_DATA_FAULT_KIND_OVERWRITTEN,
	/** A mutator returned a positive value (contract violation, clamped). */
	SAFE_DATA_FAULT_KIND_MUTATOR_CLAMPED,
};

/** @brief Event descriptor passed to the registered fault handler. */
struct safe_data_fault_info {
	/** Kind of integrity event being reported. */
	enum safe_data_fault_kind kind;
	/** Affected payload, or NULL when not applicable (e.g. lock timeout). */
	const void *payload;
	/** Payload size in bytes, 0 when not applicable. */
	size_t len;
};

/**
 * @brief Fault/diagnostic event callback.
 *
 * May be invoked while the affected container's lock is held: the handler must
 * not block and must not access any safe container (AoU: record the event,
 * set a flag/semaphore for a monitor thread, or drive the safe state).
 *
 * @param info Event descriptor (kind, affected payload, size). Only valid for
 *             the duration of the call; copy what you need.
 */
typedef void (*safe_data_fault_cb_t)(const struct safe_data_fault_info *info);

/**
 * @brief Register the fault handler callback.
 *
 * When registered, the callback receives ALL integrity events (recoveries,
 * timeouts, overwritten-corrupt, clamped returns), independent of the
 * configured CONFIG_SAFE_DATA_FAULT_* reaction - which still governs the
 * control-flow response to an unrecoverable fault (return vs panic).
 *
 * @param cb Handler, or NULL to unregister.
 * @return 0 on success, -EALREADY if a (different) handler is registered.
 *
 * @satisfies SD-REQ-013
 */
int safe_data_fault_handler_register(safe_data_fault_cb_t cb);

/**
 * @brief Legacy user-overridable (weak) fault handler.
 *
 * Called for *unrecoverable* faults when CONFIG_SAFE_DATA_FAULT_HANDLER is
 * selected and no callback is registered via safe_data_fault_handler_register()
 * (the registered callback takes precedence). The default implementation logs
 * and returns. The same execution-context constraints apply as for
 * safe_data_fault_cb_t.
 *
 * @param payload Pointer to the payload whose integrity check failed.
 * @param len     Payload size in bytes.
 */
void safe_data_fault_handler(const void *payload, size_t len);

/** @brief Global fault/recovery statistics (CONFIG_SAFE_DATA_STATS). */
struct safe_data_stats {
	/** Stale/corrupt stored tag repaired by resealing (data was intact). */
	uint32_t recovered_tag;
	/** Corrupt payload restored from the shadow copy. */
	uint32_t recovered_payload;
	/** Unrecoverable corruption detections. */
	uint32_t unrecoverable;
	/** Lock acquisitions that hit CONFIG_SAFE_DATA_LOCK_TIMEOUT_MS. */
	uint32_t lock_timeouts;
	/** Corrupt old contents observed (and replaced) by a write. */
	uint32_t overwritten;
	/** Positive mutator return values clamped to -EINVAL. */
	uint32_t mutator_clamped;
};

/**
 * @brief Snapshot the global statistics.
 *
 * @param out Destination for the counter snapshot.
 * @return 0 on success, -EINVAL on NULL, -ENOTSUP without CONFIG_SAFE_DATA_STATS.
 *
 * @satisfies SD-REQ-012
 */
int safe_data_stats_get(struct safe_data_stats *out);

/** @brief Reset all statistics counters to zero. */
void safe_data_stats_reset(void);

#if defined(CONFIG_SAFE_DATA_SELFTEST) || defined(__DOXYGEN__)
/**
 * @brief Self-test of the integrity mechanism itself.
 *
 * Runs a known-answer test of the configured CRC backend and a
 * seal/corrupt/detect(/recover) round trip on a scratch container, without
 * invoking the configured fault reaction and without polluting the statistics.
 *
 * @return 0 if the mechanism is healthy, -EFAULT if any step fails.
 *
 * @satisfies SD-REQ-017
 * @kconfig_depends{defined(CONFIG_SAFE_DATA_SELFTEST)}
 */
int safe_data_selftest(void);

/**
 * @brief Result of the boot-time self-test (CONFIG_SAFE_DATA_SELFTEST_BOOT).
 * @return 0 healthy, -EFAULT failed, -EAGAIN not run (yet).
 *
 * @kconfig_depends{defined(CONFIG_SAFE_DATA_SELFTEST)}
 */
int safe_data_selftest_result(void);
#endif /* CONFIG_SAFE_DATA_SELFTEST */

/** @} */ /* safe_data_diag_apis */

/* ------------------------------------------------------------------------- *
 * Type-erased core API. Prefer the typed macros below in application code.
 * All functions return 0 on success or a negative errno; NULL pointers for
 * required arguments yield -EINVAL. The @p lock argument is optional: pass NULL
 * (as the typed macros do when CONFIG_SAFE_DATA_LOCKING=n) to skip locking.
 * ------------------------------------------------------------------------- */

/**
 * @addtogroup safe_data_core_apis
 * @{
 */

/**
 * @brief Initialise the lock and seal the current payload contents.
 *
 * Must be called once before any other operation. The payload should be fully
 * written (no indeterminate padding) before sealing.
 *
 * @param lock    Container mutex (initialised here).
 * @param payload Payload buffer.
 * @param len     Payload size in bytes (> 0).
 * @param crc     Integrity tag storage.
 * @param shadow  Redundant shadow buffer of @p len bytes, or NULL if disabled.
 * @return 0 on success, -EINVAL on bad arguments.
 *
 * @satisfies SD-REQ-001
 */
int safe_data_init(struct k_mutex *lock, void *payload, size_t len,
		   uint32_t *crc, void *shadow);

/**
 * @brief Verify payload integrity. Pure: read-only and lock-free; it NEVER
 *        modifies the payload, so it is safe to call concurrently with writers
 *        (a transient mismatch may then be reported - callers needing
 *        atomicity should use the higher-level helpers).
 *
 * @param payload Payload buffer to check.
 * @param len     Payload size in bytes (> 0).
 * @param crc     Stored integrity tag to check against.
 * @param shadow  Redundant shadow buffer of @p len bytes, or NULL if disabled.
 * @return 0 if valid, SAFE_DATA_RECOVERED (-EAGAIN) if corrupt but recoverable
 *         from the shadow copy (payload untouched), -EILSEQ if corrupt and
 *         unrecoverable, -EINVAL on bad args.
 *
 * @satisfies SD-REQ-003
 */
int safe_data_verify(const void *payload, size_t len, uint32_t crc,
		     const void *shadow);

/**
 * @brief Verify payload integrity and repair a recoverable fault in place.
 *
 * MUST be called with the container lock held (or with equivalent external
 * mutual exclusion): the repair writes to the payload, and doing that
 * concurrently with a writer would corrupt live data. The locked helpers
 * (read/update/SAFE_SECTION) call this internally; after SAFE_DATA_RECOVERED
 * the caller reseals the stored tag to make the repair durable.
 *
 * @param payload Payload buffer to check and, if recoverable, repair in place.
 * @param len     Payload size in bytes (> 0).
 * @param crc     Stored integrity tag to check against.
 * @param shadow  Redundant shadow buffer of @p len bytes, or NULL if disabled.
 * @return 0 if valid, SAFE_DATA_RECOVERED (-EAGAIN) if the payload was repaired
 *         from the shadow copy (reseal required), -EILSEQ if corrupt and
 *         unrecoverable, -EINVAL on bad args.
 *
 * @satisfies SD-REQ-004
 * @satisfies SD-REQ-009
 * @satisfies SD-REQ-010
 */
int safe_data_verify_repair(void *payload, size_t len, uint32_t crc,
			    void *shadow);

/**
 * @brief Verify under lock and copy the payload out to @p out.
 *
 * A recoverable fault is repaired and resealed before copying, so a 0 return
 * always means @p out holds a valid snapshot.
 *
 * @param lock    Container mutex, or NULL to skip locking.
 * @param payload Payload buffer.
 * @param len     Payload size in bytes (> 0).
 * @param crc     Integrity tag storage (resealed after a repair).
 * @param shadow  Redundant shadow buffer of @p len bytes, or NULL if disabled.
 * @param out     Destination buffer of @p len bytes for the validated copy.
 * @return 0 on success (incl. recovered), -EILSEQ if unrecoverable, -EINVAL on
 *         bad args, -ETIMEDOUT if the lock bound expired.
 *
 * @satisfies SD-REQ-005
 */
int safe_data_read(struct k_mutex *lock, void *payload, size_t len,
		   uint32_t *crc, void *shadow, void *out);

/**
 * @brief Copy @p in into the payload under lock and reseal the integrity tag.
 *
 * With CONFIG_SAFE_DATA_WRITE_CHECKS_OLD the previous contents are verified
 * first; a detected corruption is reported (stats + fault callback, kind
 * OVERWRITTEN) but does not change the outcome of the write.
 *
 * @param lock    Container mutex, or NULL to skip locking.
 * @param payload Payload buffer (overwritten).
 * @param len     Payload size in bytes (> 0).
 * @param crc     Integrity tag storage (resealed).
 * @param shadow  Redundant shadow buffer of @p len bytes, or NULL if disabled.
 * @param in      Source buffer of @p len bytes to copy into the payload.
 * @return 0 on success, -EINVAL on bad args, -ETIMEDOUT if the lock bound
 *         expired.
 *
 * @satisfies SD-REQ-006
 * @satisfies SD-REQ-018
 */
int safe_data_write(struct k_mutex *lock, void *payload, size_t len,
		    uint32_t *crc, void *shadow, const void *in);

/**
 * @brief Atomic read-modify-write: lock, verify, run @p fn, reseal on success.
 *
 * This is the correct "checkout": the lock is held for the entire transaction
 * and the integrity tag is resealed automatically, so it can never go stale.
 *
 * @param lock    Container mutex, or NULL to skip locking.
 * @param payload Payload buffer, verified before and resealed after @p fn.
 * @param len     Payload size in bytes (> 0).
 * @param crc     Integrity tag storage (resealed on commit).
 * @param shadow  Redundant shadow buffer of @p len bytes, or NULL if disabled
 *                (without it an aborting mutator must not have modified the
 *                payload - there is no rollback buffer).
 * @param fn      Mutator invoked with the lock held and the payload verified.
 * @param user    Opaque context forwarded to @p fn.
 * @return 0 on success, the mutator's negative return value if it aborts
 *         (a positive mutator return is clamped to -EINVAL), -EILSEQ if the
 *         pre-check fails unrecoverably, -EINVAL on bad args, -ETIMEDOUT if
 *         the lock bound expired.
 *
 * @satisfies SD-REQ-007
 * @satisfies SD-REQ-008
 */
int safe_data_update(struct k_mutex *lock, void *payload, size_t len,
		     uint32_t *crc, void *shadow,
		     safe_data_mutator_t fn, void *user);

#if defined(CONFIG_SAFE_DATA_ALLOW_UNCHECKED_COMMIT) || defined(__DOXYGEN__)
/**
 * @brief Reseal after an explicit external modification (advanced use).
 *        Lock, recompute the integrity tag, unlock.
 *
 * @warning Re-tags the current memory contents with NO verification - this can
 *          seal corrupted data. Only available with
 *          CONFIG_SAFE_DATA_ALLOW_UNCHECKED_COMMIT (off at the Strict level).
 *
 * @param lock    Container mutex, or NULL to skip locking.
 * @param payload Payload buffer whose current contents are sealed as-is.
 * @param len     Payload size in bytes (> 0).
 * @param crc     Integrity tag storage (recomputed).
 * @param shadow  Redundant shadow buffer of @p len bytes (refreshed), or NULL
 *                if disabled.
 * @return 0 on success, -EINVAL on bad args, -ETIMEDOUT if the lock bound
 *         expired.
 *
 * @satisfies SD-REQ-022
 * @kconfig_depends{defined(CONFIG_SAFE_DATA_ALLOW_UNCHECKED_COMMIT)}
 */
int safe_data_commit(struct k_mutex *lock, void *payload, size_t len,
		     uint32_t *crc, void *shadow);
#endif /* CONFIG_SAFE_DATA_ALLOW_UNCHECKED_COMMIT */

/** @} */ /* safe_data_core_apis */

/* Implementation details shared with the SAFE_SECTION macro. */
void z_safe_data_seal(void *payload, size_t len, uint32_t *crc, void *shadow);
void z_safe_data_lock_failed(void);

/* Bound on lock acquisition; 0 keeps the legacy wait-forever behaviour. */
#if CONFIG_SAFE_DATA_LOCK_TIMEOUT_MS > 0
#define Z_SAFE_LOCK_TIMEOUT K_MSEC(CONFIG_SAFE_DATA_LOCK_TIMEOUT_MS)
#else
#define Z_SAFE_LOCK_TIMEOUT K_FOREVER
#endif

/* NULL-tolerant lock helpers: a NULL lock (locking disabled) is a no-op. Used
 * by the core and by SAFE_SECTION so the same code works with or without a mutex.
 *
 * Returns 0 when the lock is held (or not needed), -ETIMEDOUT when the
 * configured acquisition bound expired. Never legal from an ISR.
 */
static inline int z_safe_lock(struct k_mutex *lock)
{
	if (lock != NULL) {
		__ASSERT(!k_is_in_isr(),
			 "safe_data: locking API is not ISR-safe");
		if (k_mutex_lock(lock, Z_SAFE_LOCK_TIMEOUT) != 0) {
			/* Map -EAGAIN (kernel timeout) away from the
			 * SAFE_DATA_RECOVERED alias.
			 */
			return -ETIMEDOUT;
		}
	}
	return 0;
}

static inline void z_safe_unlock(struct k_mutex *lock)
{
	if (lock != NULL) {
		k_mutex_unlock(lock);
	}
}

/* ------------------------------------------------------------------------- *
 * Typed convenience layer.
 * ------------------------------------------------------------------------- */

/**
 * @defgroup safe_data_container_apis Containers and Typed Access
 * @ingroup safe_data_apis
 * @brief Typed container definition and access macros - the primary API.
 *
 * Declare a container type with SAFE_CONTAINER_DEFINE(), then access it
 * exclusively through the SAFE_* operations; they derive the type-erased
 * arguments at compile time and forward to @ref safe_data_core_apis.
 * @{
 */

/* Detection strength of a CRC collapses once the payload outgrows it. These
 * bounds keep the configured tag inside its useful Hamming-distance range; use
 * SAFE_CONTAINER_DEFINE_UNCHECKED only with a documented deviation.
 */
#if defined(CONFIG_SAFE_DATA_CRC8_CCITT)
#define Z_SAFE_DATA_MAX_PAYLOAD 16
#elif defined(CONFIG_SAFE_DATA_CRC16_CCITT)
#define Z_SAFE_DATA_MAX_PAYLOAD 256
#else /* CRC32 */
#define Z_SAFE_DATA_MAX_PAYLOAD SIZE_MAX
#endif

#define Z_SAFE_CONTAINER_BODY(_payload_type)                                   \
		IF_ENABLED(CONFIG_SAFE_DATA_LOCKING, (struct k_mutex _lock;))   \
		uint32_t _crc;                                                 \
		IF_ENABLED(CONFIG_SAFE_DATA_REDUNDANT,                         \
			   (uint8_t _shadow[sizeof(_payload_type)];))          \
		_payload_type payload;

/**
 * @brief Declare a CRC-protected container type wrapping @p _payload_type.
 *
 * Generates a struct with an optional mutex (only when CONFIG_SAFE_DATA_LOCKING,
 * the default), an integrity tag, an optional redundant shadow copy (only when
 * CONFIG_SAFE_DATA_REDUNDANT), and the user payload.
 *
 * Rejects zero-size payloads and payloads exceeding the size bound of the
 * configured tag (CRC-8: 16 bytes, CRC-16: 256 bytes) at compile time.
 *
 * Usage:
 * @code
 *   SAFE_CONTAINER_DEFINE(safe_config, struct system_config);
 *   static struct safe_config cfg;
 * @endcode
 *
 * @param _container_name Name of the generated struct type.
 * @param _payload_type   Complete type of the protected payload.
 *
 * @satisfies SD-REQ-019
 */
#define SAFE_CONTAINER_DEFINE(_container_name, _payload_type)                  \
	struct _container_name {                                               \
		BUILD_ASSERT(sizeof(_payload_type) > 0,                        \
			     "safe_data: zero-size payload");                  \
		BUILD_ASSERT(sizeof(_payload_type) <= Z_SAFE_DATA_MAX_PAYLOAD, \
			     "safe_data: payload too large for the configured" \
			     " integrity tag (see Z_SAFE_DATA_MAX_PAYLOAD)");  \
		Z_SAFE_CONTAINER_BODY(_payload_type)                           \
	}

/**
 * @brief As SAFE_CONTAINER_DEFINE but without the tag-strength size bound.
 *        For documented deviations only.
 *
 * @param _container_name Name of the generated struct type.
 * @param _payload_type   Complete type of the protected payload.
 */
#define SAFE_CONTAINER_DEFINE_UNCHECKED(_container_name, _payload_type)        \
	struct _container_name {                                               \
		BUILD_ASSERT(sizeof(_payload_type) > 0,                        \
			     "safe_data: zero-size payload");                  \
		Z_SAFE_CONTAINER_BODY(_payload_type)                           \
	}

/* Resolve the shadow pointer to either &_shadow[0] or NULL at compile time. */
#define _SAFE_SHADOW(_c)                                                       \
	COND_CODE_1(CONFIG_SAFE_DATA_REDUNDANT, ((_c)->_shadow), (NULL))

/* Resolve the lock pointer to either &_lock or NULL at compile time. When
 * locking is disabled the core functions receive NULL and skip serialisation.
 */
#define _SAFE_LOCK(_c)                                                         \
	COND_CODE_1(CONFIG_SAFE_DATA_LOCKING, (&(_c)->_lock), (NULL))

/**
 * @brief Initialise and seal a container. @see safe_data_init
 * @param _c Pointer to a container declared with SAFE_CONTAINER_DEFINE().
 * @return As safe_data_init().
 */
#define SAFE_INIT(_c)                                                          \
	safe_data_init(_SAFE_LOCK(_c), &(_c)->payload, sizeof((_c)->payload),  \
		       &(_c)->_crc, _SAFE_SHADOW(_c))

/**
 * @brief Verify integrity only (pure, never modifies the payload).
 * @see safe_data_verify
 * @param _c Pointer to a container declared with SAFE_CONTAINER_DEFINE().
 * @return As safe_data_verify().
 */
#define SAFE_VERIFY(_c)                                                        \
	safe_data_verify(&(_c)->payload, sizeof((_c)->payload), (_c)->_crc,    \
			 _SAFE_SHADOW(_c))

/**
 * @brief Validated copy out into @p _out_ptr. @see safe_data_read
 * @param _c      Pointer to a container declared with SAFE_CONTAINER_DEFINE().
 * @param _out_ptr Destination buffer of at least sizeof(payload) bytes.
 * @return As safe_data_read().
 */
#define SAFE_READ(_c, _out_ptr)                                                \
	safe_data_read(_SAFE_LOCK(_c), &(_c)->payload, sizeof((_c)->payload),  \
		       &(_c)->_crc, _SAFE_SHADOW(_c), (_out_ptr))

/**
 * @brief Validated copy in from @p _in_ptr + reseal. @see safe_data_write
 * @param _c     Pointer to a container declared with SAFE_CONTAINER_DEFINE().
 * @param _in_ptr Source buffer of at least sizeof(payload) bytes.
 * @return As safe_data_write().
 */
#define SAFE_WRITE(_c, _in_ptr)                                                \
	safe_data_write(_SAFE_LOCK(_c), &(_c)->payload, sizeof((_c)->payload), \
			&(_c)->_crc, _SAFE_SHADOW(_c), (_in_ptr))

/**
 * @brief Atomic read-modify-write via callback. @see safe_data_update
 * @param _c    Pointer to a container declared with SAFE_CONTAINER_DEFINE().
 * @param _fn   Mutator of type safe_data_mutator_t.
 * @param _user Opaque context forwarded to @p _fn.
 * @return As safe_data_update().
 */
#define SAFE_UPDATE(_c, _fn, _user)                                            \
	safe_data_update(_SAFE_LOCK(_c), &(_c)->payload, sizeof((_c)->payload),\
			 &(_c)->_crc, _SAFE_SHADOW(_c), (_fn), (_user))

#if defined(CONFIG_SAFE_DATA_ALLOW_UNCHECKED_COMMIT) || defined(__DOXYGEN__)
/**
 * @brief Reseal after an explicit external modification. @see safe_data_commit
 * @param _c Pointer to a container declared with SAFE_CONTAINER_DEFINE().
 * @return As safe_data_commit().
 *
 * @kconfig_depends{defined(CONFIG_SAFE_DATA_ALLOW_UNCHECKED_COMMIT)}
 */
#define SAFE_COMMIT(_c)                                                        \
	safe_data_commit(_SAFE_LOCK(_c), &(_c)->payload,                       \
			 sizeof((_c)->payload), &(_c)->_crc, _SAFE_SHADOW(_c))
#endif /* CONFIG_SAFE_DATA_ALLOW_UNCHECKED_COMMIT */

/**
 * @brief Scoped lock + verify ... reseal + unlock around a block.
 *
 * Inside the block, @p _pvar is a typed pointer to the (verified) payload and
 * may be used with plain C syntax. On normal exit the integrity tag is resealed
 * and the lock released. This is the ergonomic, leak-free replacement for the
 * original "checkout pointer / remember to update" pattern.
 *
 * The block is entered only if the pre-check passes (or the data was recovered
 * from the shadow copy). On an unrecoverable fault the block is skipped and the
 * lock is released; the configured fault action has already run inside
 * safe_data_verify().
 *
 * @code
 *   SAFE_SECTION(&cfg, p) {
 *       p->temperature = 90;
 *       p->fan_enabled = (p->temperature > 85);
 *   }
 * @endcode
 *
 * @note Do not `return`/`break`/`goto` out of the block: that would skip the
 *       reseal+unlock. `continue` is fine (it exits the block normally).
 *
 * @note Uses GNU C extensions (statement expressions, __typeof__) and hides
 *       control flow in a macro - only available with
 *       CONFIG_SAFE_DATA_GNU_EXTENSIONS (off at the Strict level). SAFE_UPDATE
 *       gives the same atomicity guarantee in standard C.
 *
 * The block is also skipped (and the lock-timeout event reported) when the
 * container lock cannot be acquired within CONFIG_SAFE_DATA_LOCK_TIMEOUT_MS.
 *
 * @param _c    Pointer to a container declared with SAFE_CONTAINER_DEFINE().
 * @param _pvar Name of the typed payload-pointer variable scoped to the block.
 *
 * @satisfies SD-REQ-021
 * @kconfig_depends{defined(CONFIG_SAFE_DATA_GNU_EXTENSIONS)}
 */
#if defined(CONFIG_SAFE_DATA_GNU_EXTENSIONS) || defined(__DOXYGEN__)
#define SAFE_SECTION(_c, _pvar)                                                \
	for (int _sd_st = ((z_safe_lock(_SAFE_LOCK(_c)) == 0)                  \
			   ? safe_data_verify_repair(&(_c)->payload,           \
					    sizeof((_c)->payload), (_c)->_crc, \
					    _SAFE_SHADOW(_c))                  \
			   : (z_safe_data_lock_failed(), -ETIMEDOUT));         \
	     /* enter on valid (0) or recovered (-EAGAIN) */                   \
	     ({ bool _ok = (_sd_st == 0 || _sd_st == SAFE_DATA_RECOVERED);     \
		if (!_ok && _sd_st != -ETIMEDOUT) {                            \
			z_safe_unlock(_SAFE_LOCK(_c));                         \
		}                                                              \
		_ok; });                                                       \
	     z_safe_data_seal(&(_c)->payload, sizeof((_c)->payload),           \
			      &(_c)->_crc, _SAFE_SHADOW(_c)),                  \
	     z_safe_unlock(_SAFE_LOCK(_c)), _sd_st = 1)                        \
		for (__typeof__((_c)->payload) *_pvar = &(_c)->payload;        \
		     _pvar != NULL; _pvar = NULL)
#endif /* CONFIG_SAFE_DATA_GNU_EXTENSIONS */

/** @} */ /* safe_data_container_apis */

#ifdef __cplusplus
}
#endif

#endif /* SAFE_DATA_H_ */
