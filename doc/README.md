# Safe Data API — Documentation

Five documents form the traceability chain
**requirement → test case → test result**, plus the rendered API reference.
They are built by the [zdocs](https://github.com/tiacsys/zdocs) engine, a
Zephyr module declared in the repository's `west.yml`. Every document is
declared once in [`documents.yaml`](documents.yaml), and zdocs derives all
cross-document links from it.

| Document (registry id) | Source | Content |
|---|---|---|
| Requirement Specification (`requirement-specification`) | `requirement-specification/*.rst` | `SD-TOP-…` / `SD-REQ-…` sphinx-needs requirements, authored here |
| Test Specification (`test-specification`) | the annotated `ZTEST()` sources, via `dox-safe-data-testspec` | one `TC_…` test case per ZTEST; each `verifies` requirements |
| Test Report (`test-report`) | twister output (`twister_report.xml`, `twister.json`) | one `TR-…` result per scenario × test case; `result_of` its test case, `covers` its requirements |
| API Reference (`dox-safe-data-api`) | `include/`, `src/`, `README.md` | Doxygen rendering of the API |
| Test Sources (`dox-safe-data-testspec`) | `tests/safe_data/src/main.c` | Doxygen rendering of the annotated tests; its XML feeds the test specification |

## How the chain is built

1. **Requirements are rst.** `requirement-specification/` holds the
   requirements as sphinx-needs directives. That is the only place they are
   edited. The registry key `doxygen_tag:` makes zdocs publish them as a
   Doxygen tag file too (`deploy/html/requirement-specification/needs.tag`).
   So a `\verifies SD-REQ-…` in a test resolves against the real requirement
   and links to its page.
2. **Tests carry their own traceability.** Every `ZTEST()` has a Doxygen block:

   ```c
   /**
    * @brief Initialisation seals the payload and verification passes.
    * ...
    * @testid{TC_SAFE_DATA_INIT_AND_VERIFY}
    * @verifies SD-REQ-001
    * @verifies SD-REQ-003
    * @active
    */
   ZTEST(safe_data, test_init_and_verify)
   ```

   `@testid` is the stable need id; `@verifies` is Doxygen's native command
   (one UID per line); `@active` / `@draft` / `@obsolete` is the status.
   A `@verifies` naming a requirement that does not exist **fails the build**
   ("Reference to unknown requirement").
3. **Doxygen, then Sphinx.** `dox-safe-data-testspec` renders the tests to
   XML. The `ZTEST` macros are modelled as Doxygen groups: one group per
   ztest suite, nested under the test application's group, as set up in
   `dox/safe-data-testspec/groups.dox` and the test sources. In the test
   specification, `.. testmodule:: safe_data_module` turns every test
   function into a `test_case` need.
4. **Twister results.** In the test report, `.. testreport::` and
   `.. twisterinfo::` read the twister output directory and emit one
   `test_result` need per scenario × test case.
5. **Traceability matrix.** `test-specification/traceability.rst` renders
   test case → requirement, the covered requirements, and the **coverage
   gaps**.

## Build

From the workspace root (see the top-level `README.md` for `west init`):

```sh
# 1. produce the twister results the test report is built from
west twister -T safety-toolbox/tests -p native_sim -O twister-out

# 2. configure and build all documents; doc-check runs at the end
cmake -S safety-toolbox/doc -B build/doc
cmake --build build/doc
```

The output is the deploy tree `build/doc/deploy/html/<registry id>/`.
Serve it from `build/doc/deploy/html/`; cross-document links assume
`base_url` from `documents.yaml` (`http://localhost:8000/`).

Useful cache options:

- `-DZDOCS_TWISTER_OUT=<dir>`: the twister output directory. The default is
  `<workspace>/twister-out`.
- `-DZDOCS_DOC_BASE_URL=<url>`: the URL the deploy tree is served under.

Useful targets:

- `doc-index`: every document's stage-1 index.
- `<id>-html`: a single Sphinx document.
- `<id>`: a single Doxygen document.
- `doc-check`: re-check an existing deploy tree.
- `clean-docs`

## Conventions

- **Adding a requirement:** add a `.. requirement::` with an `SD-REQ-…` id
  and a `:refines:` link to its top-level requirement in
  `requirement-specification/detailed.rst`, then reference it from tests with
  `@verifies`.
- **Adding a test:** give the `ZTEST()` a Doxygen block with `@brief`,
  a description, `@testid{TC_<SUITE>_<NAME>}`, one `@verifies` per
  requirement, and a status tag.
- **New test application:** give it a module group (`@defgroup <app>_module`,
  `@ingroup safe_data_tests`), nest each ztest suite group into it, add its
  `src/` to `dox/safe-data-testspec/Doxyfile.in`, and add a page with
  `.. testmodule:: <app>_module` to the test specification.
- **Result ids:** `TR-<platform>-<scenario>-<test case id>`.
