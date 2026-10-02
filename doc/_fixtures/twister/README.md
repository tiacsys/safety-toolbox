# Offline twister fixture

One committed twister run, so the generated test report and the traceability
matrix can be built without a toolchain (for example for a demo). Only
`twister.json` and `twister_report.xml` are kept. The report reads exactly
these two files; the per-scenario build directories and `twister.log` are
left out. The "Execution Logs" section of the report therefore shows
"handler.log not found" for each scenario.

## Provenance

- Run date: 2026-10-02T08:15:42+02:00
- Command (cwd = workspace root):
  `west twister -T safety-toolbox/tests -p native_sim/native/64 -O twister-out-toolbox-r5`
- Platform: `native_sim/native/64`
- Scenarios: `safe_data.api`, `safe_data.api.plain`, `safe_data.api.timeout`,
  `safe_data.api.strict` (tests/safe_data, 20 tests each) and
  `safe_data.fault.return`, `safe_data.fault.handler`, `safe_data.fault.panic`
  (tests/safe_data_fault, 2 tests each)
- Result: 75 passed, 11 skipped, 0 failed (86 test case results)
- safety-toolbox: `4cea72e00a3c7d6fd1b1e1b371e9fc42a7768ce4`
- Zephyr: `77e25d8f3cb2e94adb5a44426b98e088f1bef3fe` (`v4.4.0-13461-g77e25d8f3cb`).
  `twister.json` names another local tag on the same commit as its
  `zephyr_version`. This run was not repeated against the upstream v4.4.1 that
  the standalone `west.yml` pins.

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
