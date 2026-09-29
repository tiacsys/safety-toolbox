Safe Data API — Test Specification
###################################

Test cases for the Safe Data API. The content is generated from the annotated
``ZTEST()`` functions: Doxygen first renders the test sources into XML (the
ztest macros are modelled as doxygen groups — one group per test application,
one nested group per ztest suite); the ``testmodule`` directive then turns
every test function into a sphinx-needs ``test_case`` item carrying its
``@testid``, ``@verifies`` (→ ``verifies`` links into the *Requirement
Specification*) and status annotations.

.. toctree::
   :maxdepth: 2

   safe_data/test-spec
   traceability
