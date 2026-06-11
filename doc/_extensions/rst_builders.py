"""RST string builders — no Sphinx dependency."""
# Ported from zephyr-safety/doc/_extensions (same name) for the Safe Data API
# documentation; keep in sync conceptually with that prototype.
import logging
import re
from pathlib import Path

import yaml

__all__ = [
    "slugify",
    "build_need_rst",
    "build_procedure_need_rst",
    "build_result_rst",
    "build_scenario_table",
]

logger = logging.getLogger(__name__)


def slugify(s):
    """Replace non-alphanumeric runs with '-' and strip leading/trailing dashes."""
    return re.sub(r'[^a-zA-Z0-9]+', '-', s).strip('-')


def build_need_rst(info, suite_name, module_path="", suite_title=""):
    """Build the RST block for a single test_case need."""
    name = info["name"]
    test_id = info["test_id"]
    req_ids = info["req_ids"]
    status = info["status"]
    source_file = info["source_file"]
    doxygen_url = info["doxygen_url"]
    brief = info["brief"]
    detail_paras = info.get("detail_paras", [])
    see_rst = info["see_rst"]
    body_sections = info["body_sections"]

    stem = name[5:] if name.startswith("test_") else name
    title = stem.replace("_", " ")

    if test_id:
        need_id = test_id
    else:
        need_id = f"testspec-{suite_name}-{name}"
        logger.warning(f"testmodule: {suite_name}/{name} has no @testid annotation")

    lines = [f".. test_case:: {title}"]
    lines.append(f"   :id: {need_id}")
    lines.append(f"   :test_function: {name}")
    if module_path:
        lines.append(f"   :test_module: {module_path}")
    lines.append(f"   :suite: {suite_name}")
    if suite_title:
        lines.append(f"   :suite_title: {suite_title}")
    lines.append(f"   :status: {status}")
    if req_ids:
        lines.append(f"   :verifies: {'; '.join(req_ids)}")
    lines.append("")

    if brief:
        lines.append("   .. rst-class:: need-brief")
        lines.append("")
        lines.append(f"   {brief}")
        lines.append("")

    for para in detail_paras:
        lines.append(f"   {para}")
        lines.append("")

    for section_lines in body_sections:
        for sline in section_lines:
            lines.append(f"   {sline}" if sline else "")
        lines.append("")

    if source_file and doxygen_url:
        lines.append(f"   **Source:** `{source_file} <{doxygen_url}>`__")
        lines.append("")
    elif source_file:
        lines.append(f"   **Source:** {source_file}")
        lines.append("")

    if see_rst:
        lines.append(f"   {see_rst}")
        lines.append("")

    return "\n".join(lines)


def build_procedure_need_rst(memberdef, proc_compound_id, proc_group_name, testspec_html_dir, api_html_dir):
    """Build a test_procedure needs item for one shared test procedure."""
    from doxygen_parser import para_text, extract_params, see_to_rst

    name = memberdef.findtext("name", "").strip()
    need_id = f"test-proc-{proc_group_name}-{name}"

    brief = para_text(memberdef.find(".//briefdescription/para"))
    title = (brief[:90] + "…") if len(brief) > 90 else brief
    if not title:
        title = name

    loc = memberdef.find("location")
    source_file = ""
    if loc is not None:
        fpath = loc.get("bodyfile") or loc.get("file", "")
        line = loc.get("bodystart") or loc.get("line", "")
        if fpath:
            source_file = f"{Path(fpath).name} (line {line})"

    member_id = memberdef.get("id", "")
    prefix = proc_compound_id + "_1"
    anchor = member_id[len(prefix):] if member_id.startswith(prefix) else member_id
    doxygen_url = f"{testspec_html_dir}/{proc_compound_id}.html#{anchor}"

    dd = memberdef.find("detaileddescription")
    detail_lines = []
    params = []
    see_rst_str = ""
    if dd is not None:
        params = extract_params(dd)
        for para in dd.findall("para"):
            text = para_text(para).strip()
            if text:
                detail_lines.append(text)
        see_sect = dd.find(".//simplesect[@kind='see']")
        if see_sect is not None:
            see_rst_str = see_to_rst(see_sect, api_html_dir)

    lines = []
    lines.append(f".. test_procedure:: {title}")
    lines.append(f"   :id: {need_id}")
    lines.append("   :status: active")
    lines.append("")

    for text in detail_lines:
        lines.append(f"   {text}")
        lines.append("")

    if params:
        for pname, pdesc in params:
            lines.append(f"   :``{pname}``: {pdesc}")
        lines.append("")

    if source_file and doxygen_url:
        lines.append(
            f"   **Source:** :c:func:`{name}` — `{source_file} <{doxygen_url}>`__"
        )
        lines.append("")

    if see_rst_str:
        lines.append(f"   {see_rst_str}")
        lines.append("")

    return "\n".join(lines)


def build_result_rst(r, spec_id, test_module, req_ids=None):
    """Build RST block for one test_result need."""
    need_id = f"TR-{slugify(r['platform'])}-{slugify(r['scenario'])}-{spec_id}"
    fn = r["function"]
    title = (fn[5:] if fn.startswith("test_") else fn).replace("_", " ")
    lines = [
        f".. test_result:: {title}",
        f"   :id: {need_id}",
        f"   :status: {r['status']}",
    ]
    if test_module:
        lines.append(f"   :test_module: {test_module}")
    lines += [
        f"   :platform: {r['platform']}",
        f"   :scenario: {r['scenario']}",
        f"   :twister_id: {r['twister_id']}",
        f"   :execution_time: {r['time']}",
        f"   :result_of: {spec_id}",
    ]
    if req_ids:
        lines.append(f"   :covers: {'; '.join(req_ids)}")
    if r["reason"]:
        lines.append(f"   :reason: {r['reason']}")
    lines.append("")
    return "\n".join(lines)


def build_scenario_table(testcase_yaml_path):
    """Return RST lines for a list-table of scenarios from testcase.yaml."""
    try:
        with open(testcase_yaml_path) as f:
            data = yaml.safe_load(f)
    except (OSError, yaml.YAMLError) as e:
        logger.warning(f"testmodule: could not read {testcase_yaml_path}: {e}")
        return []

    scenarios = data.get("tests", {})
    if not scenarios:
        return []

    heading = "Test Scenarios"
    lines = [
        heading,
        "-" * len(heading),
        "",
        ".. list-table:: Test Scenarios",
        "   :header-rows: 1",
        "   :widths: 30 25 45",
        "",
        "   * - Scenario",
        "     - Tags",
        "     - Extra config",
    ]
    for scenario_name, scenario_data in scenarios.items():
        tags = ", ".join(scenario_data.get("tags", []))
        extra = ", ".join(scenario_data.get("extra_configs", []))
        lines.append(f"   * - ``{scenario_name}``")
        lines.append(f"     - {tags}")
        lines.append(f"     - {extra or '—'}")
    lines.append("")
    return lines
