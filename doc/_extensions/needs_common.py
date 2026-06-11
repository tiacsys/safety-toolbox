"""Shared sphinx-needs configuration for all Safe Data API documents.

Imported (star-import) by every conf.py so need types, link types and extra
fields cannot diverge between the requirement specification, the test
specification and the test report. Vocabulary follows the zephyr-safety
prototype: test cases ``verify`` requirements; test results are the
``result_of`` a test case and ``cover`` requirements.
"""

needs_types = [
    dict(directive="top_requirement", title="Top-Level Requirement", prefix="SD-TOP-",
         color="#F9D9C4", style="node"),
    dict(directive="requirement", title="Requirement", prefix="SD-REQ-",
         color="#FDEBD0", style="node"),
    dict(directive="test_case", title="Test Case", prefix="TC_",
         color="#E2EFDA", style="node"),
    dict(directive="test_procedure", title="Test Procedure", prefix="TPROC_",
         color="#D6E4F7", style="node"),
    dict(directive="test_result", title="Test Result", prefix="TR-",
         color="#D6E4F7", style="node"),
]

needs_links = {
    "refines": {
        "description": "Detailed requirement refines a top-level requirement",
        "incoming": "refined by",
        "outgoing": "refines",
    },
    "verifies": {
        "description": "Test case verifies a requirement",
        "incoming": "verified by",
        "outgoing": "verifies",
    },
    "result_of": {
        "description": "Test result belongs to a test case",
        "incoming": "results",
        "outgoing": "result of",
    },
    "covers": {
        "description": "Test result covers a requirement",
        "incoming": "covered by",
        "outgoing": "covers",
    },
}

_str_field = {"schema": {"type": "string"}, "nullable": True}
needs_fields = {
    "test_function":  {**_str_field, "description": "C function name of the test"},
    "test_module":    {**_str_field, "description": "Test application path (e.g. tests/safe_data)"},
    "suite":          {**_str_field, "description": "ztest suite (doxygen group) name"},
    "suite_title":    {**_str_field, "description": "Human-readable doxygen group title"},
    "platform":       {**_str_field, "description": "Twister platform"},
    "scenario":       {**_str_field, "description": "Twister scenario"},
    "twister_id":     {**_str_field, "description": "Full twister test case identifier"},
    "execution_time": {**_str_field, "description": "Execution time in seconds"},
    "reason":         {**_str_field, "description": "Skip/failure reason"},
}

needs_id_regex = r"^[A-Za-z][A-Za-z0-9_-]*$"

needs_build_json = True
