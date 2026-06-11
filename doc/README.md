# Safe Data API — Documentation

Four documents, forming the traceability chain
**requirement → test case → test result**, plus the rendered API reference:

| Document | Source | Built with | Content |
|----------|--------|------------|---------|
| Requirement specification | `requirement-specification/` | Sphinx + [sphinx-needs](https://sphinx-needs.readthedocs.io) | `SD-REQ-…` requirement items with stable IDs |
| Test specification | `test-specification/` | Doxygen XML → Sphinx + sphinx-needs (`testmodule` directive) | `TC_…` test-case items generated from the **doxygen XML of the annotated `ZTEST()` sources**; each `verifies` requirement IDs |
| Test report | `test-report/` | Sphinx + sphinx-needs (`testreport`/`twisterinfo` directives) | `TR-…` test-result items **derived from twister output** (`twister_report.xml`); each is the `result_of` a test case and `covers` its requirements; embeds run metadata and `handler.log` excerpts |
| API documentation | deploy `api-documentation/` (from `safe-data-api.doxyfile.in`) | Doxygen (HTML) | Rendering of the in-source doxygen comments of `safe_data.h` / `safe_data.c` |

### Test-specification pipeline (mirrors `zephyr-safety/doc`)

1. **Doxygen first** (`safe-data-testspec.doxyfile.in` → CMake target
   `doxygen-safe-data-testspec`): runs over the annotated test sources. The
   `PREDEFINED` ztest-macro expansions model the test structure as **doxygen
   groups** — every `ZTEST_SUITE()` becomes a group named after the ztest
   suite, every `ZTEST()` a function in that group. The hierarchy above the
   suites lives in [`_doxygen/safe-data-test-groups.dox`](_doxygen/safe-data-test-groups.dox)
   and the test sources:

   ```
   all_tests                      (category, groups.dox)
   └── safe_data_tests            (category, groups.dox)
       └── safe_data_module       (test application, @defgroup in tests main.c)
           └── safe_data          (ztest suite, via ZTEST_SUITE + @addtogroup nesting)
   ```

2. **Then Sphinx**: `.. testmodule:: safe_data_module` walks the doxygen XML
   (`_extensions/doxygen_parser.py`) and renders one `test_case` need per
   ZTEST, with section headings per suite, a scenario table from
   `testcase.yaml`, and source links into the doxygen HTML.

Test annotations use the zephyr-safety vocabulary: `@testid{TC_…}` (stable
need ID), one `@reqref{SD-REQ-…}` per verified requirement,
`@active`/`@draft`/`@obsolete` (status).

The Sphinx extensions live in [`_extensions/`](_extensions/):
`test_module.py` (testmodule/testreport/twisterinfo directives),
`doxygen_parser.py`, `rst_builders.py`, `twister_reader.py` (ported from
`zephyr-safety/doc/_extensions`), and `needs_common.py` (shared need/link
types so the documents cannot diverge).

## Prerequisites

```sh
python3 -m venv /workspace/.venv-docs
/workspace/.venv-docs/bin/pip install sphinx sphinx-needs sphinx_rtd_theme pyyaml
# doxygen must be on PATH (any recent version)
```

## Build

The build system is CMake ([CMakeLists.txt](CMakeLists.txt), modelled on
`zephyr-safety/doc`): every document gets a `<name>-html` target plus a
`<name>-html-nodeps` twin that skips the inter-document dependencies.

```sh
# 1. produce twister results (consumed by the test report)
export ZEPHYR_SDK_INSTALL_DIR=/opt/toolchains/zephyr-sdk-1.0.1
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr ZEPHYR_BASE=/workspace/zephyr
/workspace/zephyr/scripts/twister -T /workspace/safe_api/improved/tests \
    -p native_sim --outdir /workspace/build-safe_api/twister-out

# 2. configure (sphinx-build from the docs venv must be on PATH)
PATH=/workspace/.venv-docs/bin:$PATH \
    cmake -B /workspace/build-safe_api/doc /workspace/safe_api/improved/doc

# 3. build everything — or a single target, e.g. test-report-html
cmake --build /workspace/build-safe_api/doc --target docs
```

Targets: `requirement-specification-html`, `test-specification-html`,
`test-report-html`, `api-documentation-html` (Doxygen), `docs` (all of them),
`serve`. Cache options: `-DTWISTER_OUT=…`, `-DSAFE_DATA_DOC_BASE_URL=…`,
`-DSPHINXOPTS=…`.

Outputs land in `<builddir>/deploy/<document>/html/`. Build order is encoded
as target dependencies: the test specification imports the requirement
specification's `needs.json` (sphinx-needs `needs_external_needs`), the test
report imports both — that is what makes the `verifies`/`result_of`/`covers`
links resolve across the separately built documents. The test specification
additionally depends on the `doxygen-safe-data-testspec` target that produces
the XML it consumes.

## View

Cross-document links use a common base URL (default `http://localhost:8000`,
override with `-DSAFE_DATA_DOC_BASE_URL=…`):

```sh
cmake --build /workspace/build-safe_api/doc --target serve
# then open http://localhost:8000/
```

## Conventions

- **Test annotations:** every `ZTEST()` carries a doxygen block with
  `@brief` (title), a description, `@testid{TC_<SUITE>_<NAME>}` (the stable
  need ID), one `@reqref{SD-REQ-…}` per verified requirement, and a status
  tag (`@active`/`@draft`/`@obsolete`).
- **New test application/suite:** give the application a module group
  (`@defgroup <app>_module @ingroup safe_data_tests`) and nest each ztest
  suite group into it (`@addtogroup <suite>` + `@ingroup <app>_module`) next
  to its `ZTEST_SUITE()`; then add a page with
  `.. testmodule:: <app>_module` to the test specification.
- **Result IDs:** `TR-<platform>-<scenario>-<test-case-id>`, one per twister
  scenario × test case, status `passed`/`failed`/`skipped`.
- **Adding a requirement:** add an `SD-REQ-…` item in
  `requirement-specification/index.rst`, reference it from the relevant test
  annotations (`@reqref`), rebuild.
