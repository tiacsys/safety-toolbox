Safe Data Fault-Reaction Test Application
#########################################

All test cases of the ``tests/safe_data_fault`` test application, generated
from the doxygen XML of its annotated sources (module group
``safe_data_fault_module``). Each scenario selects one unrecoverable-fault
reaction (return, handler, panic). The tests cause a fault or an ISR-context
call on purpose, so they are kept out of the main test application.

.. testmodule:: safe_data_fault_module
   :module: tests/safe_data_fault
