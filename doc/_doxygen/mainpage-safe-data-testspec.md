# Safe Data API — Test Specification (Doxygen)

Doxygen rendering of the annotated `ZTEST()` functions of the Safe Data API
test application (`tests/safe_data`). The group hierarchy models the test
structure:

- **Safe Data API Tests** (`all_tests`)
  - **Safe Data Test Suites** (`safe_data_tests`)
    - **Safe Data Test Application** (`safe_data_module`, one per test app)
      - ztest suite **`safe_data`** (one doxygen group per `ZTEST_SUITE()`)

The Sphinx test specification consumes the XML output of this build via the
`testmodule` directive and turns every test case into a traceable
sphinx-needs item.
