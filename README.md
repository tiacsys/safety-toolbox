# Safe Data API

Generic Zephyr facility for protecting a variable or data structure with an
integrity tag (CRC), an optional redundant shadow copy, and a mutex — so that
data integrity is guaranteed *by construction* rather than by remembering to
call the right function at the right time.

The repository is also a self-contained example of generated, auditable safety
evidence. Its requirements, annotated tests and twister results are rendered
into a requirement specification, a test specification, a test report and a
traceability matrix (see `doc/README.md`).

## Why

In a safety-critical context, RAM and stored values can be corrupted by SEUs,
stack overflow, rogue DMA, or plain bugs. A container bundles the data with an
integrity tag and serialises every access so that:

* every mutation reseals the tag atomically (it can never go stale),
* every read verifies before handing data out,
* corruption is either **recovered** (redundant copy) or reported via a
  **configurable** reaction (return error / handler / panic) — not always a
  hard panic.

## Layout

```
include/safe_data/safe_data.h   typed macros + core prototypes
src/safe_data.c                 type-erased core (one audited copy of the logic)
Kconfig, CMakeLists.txt          Zephyr module wiring
zephyr/module.yml
samples/config_guard            copy-in/out style; corruption -> recover defaults
samples/transaction             atomic RMW, scoped section, shadow recovery
samples/producer_consumer       two threads share one struct; lock prevents torn reads
tests/safe_data                 ztest suite (incl. a concurrency test)
doc/                            requirements, test specification, test report
west.yml                        standalone workspace manifest
```

## Usage

```c
#include <safe_data/safe_data.h>

struct system_config { int temperature; bool fan_enabled; };

SAFE_CONTAINER_DEFINE(safe_config, struct system_config);
static struct safe_config cfg;

void init(void)
{
    struct system_config v = { .temperature = 25 };
    SAFE_INIT(&cfg);          /* init mutex + seal current contents */
    SAFE_WRITE(&cfg, &v);     /* install a value + reseal           */
}
```

### Read / write (copy semantics — nothing touches storage directly)

```c
struct system_config local;
if (SAFE_READ(&cfg, &local) == 0) {       /* validated snapshot */
    local.temperature = 90;
    SAFE_WRITE(&cfg, &local);              /* install + reseal   */
}
```

### Atomic read-modify-write (callback transaction)

The lock is held across the whole transaction; the tag is resealed on success.
If the mutator returns an error the change is **rolled back** (when the
redundant shadow is enabled) and the error is propagated.

```c
static int bump(void *payload, void *user)
{
    struct system_config *c = payload;
    c->temperature += *(int *)user;
    return (c->temperature > 125) ? -ERANGE : 0;   /* abort -> rollback */
}

int delta = 5;
SAFE_UPDATE(&cfg, bump, &delta);
```

### Scoped section (plain-C access, auto reseal + unlock)

The ergonomic replacement for the original "checkout a pointer / remember to
update" idea — but the lock is held for the whole block and the tag is resealed
automatically at the end.

```c
SAFE_SECTION(&cfg, p) {        /* p is a struct system_config * */
    p->temperature = 90;
    p->fan_enabled = (p->temperature > 85);
}                              /* tag resealed, lock released here */
```

> Do not `return` / `break` / `goto` out of a `SAFE_SECTION` block — that would
> skip the reseal + unlock. `continue` exits the block cleanly.

### Two threads sharing a container

The mutex embedded in each container (the default) serialises every access, so a
reader thread can never observe a half-finished update from a writer thread — each
`SAFE_READ` snapshot is internally consistent and CRC-valid. See
`samples/producer_consumer` for a runnable demo: a
producer mutates a multi-field struct with an invariant (`sum == x + y`) via
`SAFE_UPDATE` while a consumer validates snapshots via `SAFE_READ`; the invariant
holds on every read.

### Reseal after an external modification

If an audited low-level path writes the storage directly, `SAFE_COMMIT` reseals the
tag (and refreshes the shadow) over the current contents under the lock:

```c
SAFE_COMMIT(&cfg);   /* recompute the integrity tag in place */
```

## Configuration (Kconfig)

| Option | Default | Meaning |
|--------|---------|---------|
| `SAFE_DATA` | n | Enable the library (selects `CRC`). |
| `SAFE_DATA_LEVEL_BASIC` / `_LEVEL_ENHANCED` / `_LEVEL_STRICT` | BASIC | Integrity-level preset. ENHANCED implies redundancy + statistics; STRICT additionally implies boot self-test + checked writes and removes `SAFE_COMMIT`/`SAFE_SECTION` from the API surface. Implied options remain individually overridable. |
| `SAFE_DATA_CRC32_IEEE` / `_CRC16_CCITT` / `_CRC8_CCITT` | CRC32 | Integrity-tag algorithm. A compile-time bound ties tag strength to payload size (CRC-8 ≤ 16 B, CRC-16 ≤ 256 B). |
| `SAFE_DATA_LOCKING` | y | Embed a `k_mutex` in each container and serialise access. Disable to drop the mutex when a container is only touched from one context (caller then owns mutual exclusion). |
| `SAFE_DATA_LOCK_TIMEOUT_MS` | 0 | Bound on lock acquisition; on expiry the operation returns `-ETIMEDOUT` (counted + reported). 0 = wait forever. |
| `SAFE_DATA_REDUNDANT` | n | Keep an inverted shadow copy; enables recovery + RMW rollback (2× payload RAM). |
| `SAFE_DATA_STATS` | n | Global fault/recovery counters via `safe_data_stats_get()` — the observability hook for a health monitor. |
| `SAFE_DATA_WRITE_CHECKS_OLD` | n | Verify old contents before overwriting (observe-only; corruption is reported, the write still succeeds). |
| `SAFE_DATA_SELFTEST` (+ `_SELFTEST_BOOT`) | n | `safe_data_selftest()`: CRC known-answer test + seal/corrupt/detect/recover round trip. `_BOOT` runs it from `SYS_INIT` and panics on failure. |
| `SAFE_DATA_ALLOW_UNCHECKED_COMMIT` | y (n at STRICT) | Keep `SAFE_COMMIT` (reseal without verify) in the API surface. |
| `SAFE_DATA_GNU_EXTENSIONS` | y (n at STRICT) | Keep `SAFE_SECTION` (statement expressions, `__typeof__`) in the API surface. |
| `SAFE_DATA_FAULT_RETURN` / `_FAULT_HANDLER` / `_FAULT_PANIC` | RETURN | Control-flow reaction to an *unrecoverable* fault. Independently of it, a callback registered via `safe_data_fault_handler_register()` receives **all** integrity events (recoveries, timeouts, overwrites, clamped mutator returns). The callback may run with the container lock held: it must not block or touch safe containers. |

Return codes: `0` success, `-EILSEQ` unrecoverable corruption, `-EINVAL` bad
args, `SAFE_DATA_RECOVERED` (`-EAGAIN`) for a shadow-coverable fault — from
`safe_data_verify()` it means *recoverable* (pure check, payload untouched);
from `safe_data_verify_repair()` (lock-held, used internally by
read/update/`SAFE_SECTION`) it means *repaired in place*.

## Workspace

The repository is its own west manifest (`west.yml`): Zephyr at a
release tag plus the zdocs documentation engine, nothing else.

```sh
west init -m git@github.com:tiacsys/safety-toolbox.git toolbox-ws
cd toolbox-ws
west update
west zephyr-export
pip install -r zephyr/scripts/requirements.txt
```

All commands below run from the workspace root (`toolbox-ws/`).

## Build & run (native_sim)

The samples are self-contained applications and add this repository as a
module themselves. On a 64-bit host without 32-bit multilib (e.g. aarch64),
use `native_sim/native/64` in place of `native_sim`.

```sh
# atomic read-modify-write, scoped section, shadow recovery
west build -p always -b native_sim -d build/transaction \
     safety-toolbox/samples/transaction
build/transaction/zephyr/zephyr.exe          # Ctrl-C to stop (idles after main)

# two threads sharing one protected struct
west build -p always -b native_sim -d build/producer_consumer \
     safety-toolbox/samples/producer_consumer
build/producer_consumer/zephyr/zephyr.exe    # Ctrl-C to stop (idles after main)
```

## Tests

The ztest suite has four twister scenarios (default, detection-only without
the shadow copy, bounded locking, and the STRICT preset):

```sh
west twister -T safety-toolbox/tests -p native_sim -O twister-out
```

The twister output directory is also the input to the generated test report
(see `doc/README.md`).

## Using the module

Add the repository to your own manifest, or pass it on the command line:

```sh
west build -b <board> <app> -- -DEXTRA_ZEPHYR_MODULES=<path to safety-toolbox>
```

## Limitations / notes

* With locking enabled (the default) containers are for **thread context**
  (the mutex must not be taken from an ISR). For data shared between threads
  *and* ISRs, use a spinlock-based variant (future work) or copy under the lock
  from a thread. With `CONFIG_SAFE_DATA_LOCKING=n` the API makes no blocking
  calls and may be used from a single context such as one ISR — but then no
  mutual exclusion is provided, so it must not be shared concurrently.
* Fully populate the payload before `SAFE_INIT` — the tag covers all
  `sizeof(payload)` bytes including any padding. `SAFE_INIT` does not zero the
  struct; zero-initialise the instance (e.g. `static`) so padding is defined.
* Without `SAFE_DATA_REDUNDANT` there is no rollback buffer: a mutator that
  returns an error must not have modified the payload first (validate up front).
* The shadow scheme detects and recovers a *single* corrupted copy; a
  simultaneous double fault is reported as unrecoverable.
