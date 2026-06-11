"""Twister output parsing — no Sphinx dependency."""
# Ported from zephyr-safety/doc/_extensions (same name) for the Safe Data API
# documentation; keep in sync conceptually with that prototype.
import json
import xml.etree.ElementTree as ET
from pathlib import Path

__all__ = [
    "parse_twister_results",
    "load_spec_lookup",
    "find_handler_log",
    "load_twister_meta",
]


def _elem_text(elem):
    """Collapse all text nodes in elem into a single whitespace-normalised string."""
    if elem is None:
        return ""
    return " ".join("".join(elem.itertext()).split())


def parse_twister_results(xml_path, module_filter=None, exact=False):
    """Parse twister_report.xml into a list of result dicts.

    The 'function' field has any leading 'test_' prefix stripped so it matches
    the keys used in spec_lookup.
    """
    root = ET.parse(xml_path).getroot()
    results = []
    for ts in root.findall("testsuite"):
        platform = ts.get("name", "")
        for tc in ts.findall("testcase"):
            classname = tc.get("classname", "")
            if module_filter:
                if exact:
                    if classname != module_filter:
                        continue
                elif not (classname == module_filter or classname.startswith(module_filter + ".")):
                    continue
            name = tc.get("name", "")
            scenario = classname
            suffix = name[len(scenario) + 1:] if name.startswith(scenario + ".") else name
            parts = suffix.rsplit(".", 1)
            suite = parts[0] if len(parts) == 2 else ""
            function = parts[-1]
            if function.startswith("test_"):
                function = function[5:]
            failure = tc.find("failure")
            error = tc.find("error")
            skipped = tc.find("skipped")
            if failure is not None:
                status, reason = "failed", failure.get("message", "") or _elem_text(failure)
            elif error is not None:
                status, reason = "error", error.get("message", "") or _elem_text(error)
            elif skipped is not None:
                status, reason = "skipped", skipped.get("message", "") or _elem_text(skipped)
            else:
                status, reason = "passed", ""
            results.append({
                "platform": platform,
                "scenario": scenario,
                "suite": suite,
                "function": function,
                "twister_id": name,
                "time": tc.get("time", ""),
                "status": status,
                "reason": reason,
            })
    return results


def load_spec_lookup(json_path):
    """Read spec needs.json; return {test_function: {id, test_module, suite, req_ids}}."""
    with open(json_path) as f:
        data = json.load(f)
    versions = data.get("versions", {})
    if not versions:
        raise RuntimeError(f"testreport: no versions key in {json_path}")
    current = data.get("current_version") or next(iter(versions))
    needs = versions.get(current, {}).get("needs", {})
    lookup = {}
    for need_id, need in needs.items():
        if need.get("type") != "test_case":
            continue
        fn = need.get("test_function", "")
        if fn:
            lookup[fn] = {
                "id": need_id,
                "test_module": need.get("test_module", ""),
                "suite": need.get("suite", ""),
                "suite_title": need.get("suite_title", ""),
                "req_ids": need.get("verifies", []),
            }
    return lookup


def find_handler_log(twister_out_dir, platform, toolchain, test_path, scenario_name):
    """Return the Path to handler.log for a (platform, scenario) run, or None."""
    platform_slug = platform.replace("/", "_")
    toolchain_slug = toolchain.replace("/", "_")
    # twister.json 'path' is relative to ZEPHYR_BASE and may climb out of it
    # ("../safe_api/..."); on disk the run directory drops the leading "../".
    while test_path.startswith("../"):
        test_path = test_path[3:]
    base = Path(twister_out_dir) / platform_slug / toolchain_slug / test_path
    exact = base / scenario_name / "handler.log"
    if exact.exists():
        return exact
    candidates = sorted(base.glob("*/handler.log")) if base.exists() else []
    if len(candidates) == 1:
        return candidates[0]
    for c in candidates:
        if scenario_name.startswith(c.parent.name):
            return c
    return None


def load_twister_meta(json_path):
    """Load and validate twister.json; return the dict."""
    with open(json_path) as f:
        data = json.load(f)
    if "environment" not in data:
        raise RuntimeError(f"load_twister_meta: missing 'environment' key in {json_path}")
    if "testsuites" not in data:
        raise RuntimeError(f"load_twister_meta: missing 'testsuites' key in {json_path}")
    return data
