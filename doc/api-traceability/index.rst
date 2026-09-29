Safe Data API — API Traceability
################################

The implementation half of the traceability chain. Every public API function
or macro that names a requirement with Doxygen's native ``@satisfies`` becomes
an ``impl`` need, generated from the API documentation's Doxygen XML. Each one
is linked ``satisfies`` to its requirements in the *Requirement
Specification*, and links to its page in the API reference.

The annotation sits on the declaration's doc block in
``include/safe_data/safe_data.h``, and only where the requirement names the
symbol. Requirements that constrain the whole API rather than one symbol are
not tagged; the traceability matrix lists them as not satisfied by a symbol.

.. symbolneeds::
