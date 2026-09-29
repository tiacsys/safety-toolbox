# Offline twister fixture

One committed twister run, so the generated test report and the traceability
matrix can be built without a toolchain (for example for a demo). Only
`twister.json` and `twister_report.xml` are kept. The report reads exactly
these two files; the per-scenario build directories and `twister.log` are
left out. The "Execution Logs" section of the report therefore shows
"handler.log not found" for each scenario.

## Provenance

- Run date: 2026-09-29T20:25:54+02:00
- Command (cwd = workspace root):
  `west twister -T safety-toolbox/tests -p native_sim/native/64 -O twister-out-toolbox-r3`
- Platform: `native_sim/native/64`
- Scenarios: `safe_data.api`, `safe_data.api.plain`, `safe_data.api.timeout`,
  `safe_data.api.strict`
- Result: 65 passed, 11 skipped, 0 failed (76 test case results)
- safety-toolbox: `5847f3fdca777b8d62615d84b8926fdc8ce125ed`
- Zephyr: `77e25d8f3cb2e94adb5a44426b98e088f1bef3fe` (`v4.4.0-13461-g77e25d8f3cb2`).
  The standalone `west.yml` pins upstream v4.4.1; the results are identical there.

## Use

```sh
cmake -S <workspace>/safety-toolbox/doc -B build/doc \
  -DZDOCS_TWISTER_OUT=<abs path to>/safety-toolbox/doc/_fixtures/twister
```

## Refresh

Rerun twister, copy `twister.json` and `twister_report.xml` from the new
output directory over the two files here (`cp -p`), and update the date,
command and commit SHAs in this file.

Warning: a fixture is a snapshot. A live `west twister` run is the real
evidence.
