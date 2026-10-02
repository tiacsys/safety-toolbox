/*
 * Copyright (c) 2026 The safety-toolbox contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * config_guard - the original "system_config" demo, done safely.
 *
 * Shows the copy-in / copy-out style: callers never touch the protected
 * storage directly. SAFE_READ hands back a validated copy; SAFE_WRITE installs
 * a new value and reseals the integrity tag atomically. A simulated RAM
 * corruption is then caught and handled as an error (no panic), which is the
 * behaviour a safety application usually wants.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <safe_data/safe_data.h>

LOG_MODULE_REGISTER(config_guard, LOG_LEVEL_INF);

struct system_config {
	int temperature;
	bool fan_enabled;
};

/* Define the protected container type and instantiate it. */
SAFE_CONTAINER_DEFINE(safe_config, struct system_config);
static struct safe_config cfg;

static int apply_temperature(int new_temp)
{
	struct system_config local;
	int ret;

	/* Read a validated snapshot. */
	ret = SAFE_READ(&cfg, &local);
	if (ret < 0) {
		LOG_ERR("config read failed: %d", ret);
		return ret;
	}
	LOG_INF("current safe temperature: %d", local.temperature);

	/* Apply business logic on the local copy. */
	local.temperature = new_temp;
	local.fan_enabled = (new_temp > 85);
	if (local.fan_enabled) {
		LOG_WRN("temperature high (%d) -> fan enabled", new_temp);
	}

	/* Install + reseal atomically. */
	ret = SAFE_WRITE(&cfg, &local);
	if (ret < 0) {
		LOG_ERR("config write failed: %d", ret);
		return ret;
	}
	LOG_INF("new configuration committed safely");
	return 0;
}

int main(void)
{
	struct system_config init = { .temperature = 25, .fan_enabled = false };

	LOG_INF("=== Safe API: config_guard ===");

	SAFE_INIT(&cfg);
	(void)SAFE_WRITE(&cfg, &init);

	LOG_INF("--- normal operation ---");
	(void)apply_temperature(90);

	LOG_INF("--- simulating RAM corruption ---");
	/* Forcibly poke the protected storage WITHOUT going through the API. */
	cfg.payload.temperature = 999;

	int ret = apply_temperature(40);

	if (ret == -EILSEQ) {
		LOG_ERR("corruption detected and rejected (ret=%d) - "
			"recovering to safe defaults", ret);
		SAFE_INIT(&cfg);
		(void)SAFE_WRITE(&cfg, &init);
		LOG_INF("restored safe defaults: temp=%d", init.temperature);
	}

	LOG_INF("=== done (survived without a panic) ===");
	return 0;
}
