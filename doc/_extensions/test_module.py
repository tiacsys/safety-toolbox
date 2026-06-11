"""Sphinx extension: testmodule / testreport / twisterinfo directives.

Ported from zephyr-safety/doc/_extensions/test_module.py for the Safe Data
API documentation. The pipeline is the same:

1. Doxygen runs over the annotated ztest sources (safe-data-testspec
   doxyfile): the PREDEFINED ztest-macro expansions model every ZTEST_SUITE()
   as a doxygen group and every ZTEST() as a function in that group; a module
   group (e.g. ``safe_data_module``) collects the suites of one test
   application.
2. ``.. testmodule:: <module-group>`` walks the doxygen XML
   (``doxygen_parser``) and renders one sphinx-needs ``test_case`` item per
   ZTEST, carrying the ``@testid``/``@reqref``/status annotations.
3. ``.. testreport:: twister_report.xml`` joins twister results against the
   test specification's ``needs.json`` and renders ``test_result`` items.

Adaptations vs. the zephyr-safety original:
- the doxygen XML directory comes from the ``testspec_doxygen_xml`` config
  value (no breathe dependency),
- the module root (for ``testcase.yaml``) is the Zephyr module containing
  ``doc/``, not ``ZEPHYR_BASE``.
"""
import os
from collections import Counter, defaultdict
from pathlib import Path

from docutils import nodes
from docutils.parsers.rst import Directive, directives
from docutils.statemachine import ViewList
from sphinx.util import logging
import xml.etree.ElementTree as ET

from doxygen_parser import load_group_index, parse_memberdef
from rst_builders import (
    build_need_rst,
    build_procedure_need_rst,
    build_result_rst,
    build_scenario_table,
)
from twister_reader import (
    find_handler_log,
    load_spec_lookup,
    load_twister_meta,
    parse_twister_results,
)

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Shared utilities
# ---------------------------------------------------------------------------

def _maybe_dump_rst(app, docname: str, directive: str, arg: str, rst_text: str) -> None:
    dump_dir = getattr(app.config, "dump_generated_rst", "")
    if not dump_dir:
        return
    out = Path(dump_dir)
    out.mkdir(parents=True, exist_ok=True)
    doc_slug = docname.replace("/", "__")
    arg_slug = arg.replace("/", "_").replace(".", "_")
    out_file = out / f"{doc_slug}__{directive}__{arg_slug}.rst"
    out_file.write_text(rst_text, encoding="utf-8")
    logger.debug(f"{directive}: dumped generated RST → {out_file}")


def _render_rst(rst_lines, state, content_offset, match_titles=False):
    """Parse a list of RST lines into docutils nodes via nested_parse."""
    vl = ViewList(rst_lines, source="<generated>")
    container = nodes.container()
    state.nested_parse(vl, content_offset, container, match_titles=match_titles)
    return container.children


# ---------------------------------------------------------------------------
# testmodule helpers
# ---------------------------------------------------------------------------

def _check_no_ztest_members(proc_cdef: ET.Element) -> None:
    """Warn if a procedure group contains ZTEST-annotated functions."""
    proc_name = proc_cdef.findtext("compoundname", "")
    for md in proc_cdef.findall("sectiondef/memberdef[@kind='function']"):
        fn_name = md.findtext("name", "")
        dd = md.find("detaileddescription")
        if dd is not None:
            for x in dd.findall("para/xrefsect"):
                if "testids" in x.get("id", ""):
                    logger.warning(
                        f"testmodule: procedure group '{proc_name}' contains "
                        f"ZTEST-annotated function '{fn_name}' — check @ingroup annotations"
                    )


def _classify_inner_groups(module_cdef: ET.Element, xml_dir: Path):
    """Split inner groups into (suite_refids, proc_refids) by compoundname suffix."""
    suite_refids, proc_refids = [], []
    for ig in module_cdef.findall("innergroup"):
        refid = ig.get("refid")
        ig_xml = xml_dir / f"{refid}.xml"
        if not ig_xml.exists():
            logger.warning(f"testmodule: inner group XML not found: {ig_xml}")
            continue
        ig_cdef = ET.parse(ig_xml).getroot().find("compounddef")
        if ig_cdef.findtext("compoundname", "").endswith("_procedures"):
            proc_refids.append(refid)
        else:
            suite_refids.append(refid)
    return suite_refids, proc_refids


def _build_suite_rst(suite_refid, xml_dir, testspec_html_dir, api_html_dir, module_path):
    """Build RST lines for one test suite group (section heading + test_case needs)."""
    suite_xml = xml_dir / f"{suite_refid}.xml"
    if not suite_xml.exists():
        logger.warning(f"testmodule: suite XML not found: {suite_xml}")
        return []
    suite_cdef = ET.parse(suite_xml).getroot().find("compounddef")
    suite_name = suite_cdef.findtext("compoundname", suite_refid)
    compound_id = suite_cdef.get("id", suite_refid)
    suite_title = suite_cdef.findtext("title", suite_name)

    lines = [suite_title, "-" * len(suite_title), ""]
    for memberdef in suite_cdef.findall(".//memberdef[@kind='function']"):
        info = parse_memberdef(memberdef, compound_id, testspec_html_dir, api_html_dir)
        if not info["name"]:
            continue
        lines.extend(build_need_rst(info, suite_name, module_path, suite_title).splitlines())
        lines.append("")
    return lines


def _build_proc_group_rst(proc_refid, xml_dir, testspec_html_dir, api_html_dir):
    """Build RST lines for one procedure group (section heading + test_procedure needs)."""
    proc_xml = xml_dir / f"{proc_refid}.xml"
    proc_cdef = ET.parse(proc_xml).getroot().find("compounddef")
    proc_compound_id = proc_cdef.get("id", proc_refid)
    proc_group_name = proc_cdef.findtext("compoundname", proc_refid)
    proc_heading = proc_cdef.findtext("title", "Shared Test Procedures")

    _check_no_ztest_members(proc_cdef)

    lines = [proc_heading, "-" * len(proc_heading), ""]
    for md in proc_cdef.findall(".//memberdef[@kind='function']"):
        lines.extend(
            build_procedure_need_rst(
                md, proc_compound_id, proc_group_name, testspec_html_dir, api_html_dir
            ).splitlines()
        )
        lines.append("")
    return lines


# ---------------------------------------------------------------------------
# testreport helpers
# ---------------------------------------------------------------------------

def _group_results(results):
    """Group twister results by (suite, function); return (suite_order, func_order, grouped)."""
    suite_order, func_order, grouped, seen = [], {}, {}, set()
    for r in results:
        s, fn = r["suite"], r["function"]
        if s not in func_order:
            suite_order.append(s)
            func_order[s] = []
        if (s, fn) not in seen:
            func_order[s].append(fn)
            seen.add((s, fn))
        grouped.setdefault((s, fn), []).append(r)
    for key in grouped:
        grouped[key].sort(key=lambda r: (r["platform"], r["scenario"]))
    return suite_order, func_order, grouped


def _build_results_rst(suite_order, func_order, grouped, spec_lookup):
    """Build RST lines for all test_result needs, grouped into one section per suite."""
    lines = []
    for suite in suite_order:
        suite_title = next(
            (
                (spec_lookup.get(fn) or spec_lookup.get("test_" + fn) or {}).get("suite_title")
                for fn in func_order[suite]
                if (spec_lookup.get(fn) or spec_lookup.get("test_" + fn) or {}).get("suite_title")
            ),
            None,
        )
        heading = suite_title or suite.replace("_", " ").title()
        lines += [heading, "-" * len(heading), ""]
        for fn in func_order[suite]:
            info = spec_lookup.get(fn) or spec_lookup.get("test_" + fn)
            if info is None:
                logger.warning(f"testreport: '{fn}' not in spec needs.json — skipped")
                continue
            for r in grouped[(suite, fn)]:
                lines += build_result_rst(
                    r, info["id"], info["test_module"], info.get("req_ids")
                ).splitlines()
                lines.append("")
    return lines


def _build_summary_table_rst(grouped, spec_lookup):
    """Build RST lines for the result summary needtable."""
    modules = sorted({
        (spec_lookup.get(fn) or spec_lookup.get("test_" + fn) or {}).get("test_module", "")
        for _, fn in grouped
    } - {""})
    tbl_filter = (
        f'type == "test_result" and test_module == "{modules[0]}"'
        if len(modules) == 1
        else 'type == "test_result"'
    )
    heading = "Result summary"
    return [
        "----", "",
        heading, "-" * len(heading), "",
        ".. needtable::",
        f"   :filter: {tbl_filter}",
        "   :columns: id, title, platform, scenario, status, execution_time, result_of",
        "   :style: table",
        "",
    ]


def _build_exec_logs_rst(twister_out_dir, module_filter):
    """Build RST lines for the execution logs section; returns [] when unavailable."""
    twister_json = Path(twister_out_dir) / "twister.json" if twister_out_dir else None
    if not twister_json or not twister_json.exists():
        return []
    try:
        tw = load_twister_meta(twister_json)
    except Exception as exc:
        logger.warning(f"testreport: could not load execution logs: {exc}")
        return []

    log_entries = []
    for ts in tw.get("testsuites", []):
        sname = ts["name"]
        if module_filter and not (
            sname == module_filter or sname.startswith(module_filter + ".")
        ):
            continue
        log_entries.append((sname, ts["platform"], ts.get("path", ""), ts.get("toolchain", "")))
    log_entries.sort()
    if not log_entries:
        return []

    lines = ["----", "", "Execution Logs", "-" * len("Execution Logs"), ""]
    for scenario, platform, test_path, toolchain in log_entries:
        sub = f"{scenario} — {platform}"
        lines += [sub, "~" * len(sub), ""]
        log_file = find_handler_log(twister_out_dir, platform, toolchain, test_path, scenario)
        if log_file:
            lines += [".. code-block:: none", ""]
            for line in log_file.read_text(errors="replace").splitlines():
                lines.append("   " + line)
            lines.append("")
        else:
            lines += [
                f"*handler.log not found for* ``{scenario}`` *on* ``{platform}``",
                "",
            ]
    return lines


# ---------------------------------------------------------------------------
# twisterinfo helpers
# ---------------------------------------------------------------------------

def _compute_platform_stats(suites):
    """Compute per-platform pass/fail/skip/error counts."""
    platforms = sorted({s["platform"] for s in suites})
    by_platform = defaultdict(list)
    for s in suites:
        by_platform[s["platform"]].extend(s.get("testcases", []))

    stats, totals = [], [0, 0, 0, 0]
    for plat in platforms:
        counts = Counter(tc.get("status", "") for tc in by_platform[plat])
        p = counts.get("passed", 0)
        f = counts.get("failed", 0)
        s = counts.get("skipped", 0)
        e = counts.get("error", 0)
        totals[0] += p
        totals[1] += f
        totals[2] += s
        totals[3] += e
        stats.append((plat, p, f, s, e, len(by_platform[plat])))
    return platforms, stats, tuple(totals)


def _build_twisterinfo_rst(tw_env, suites):
    """Build RST lines for the run-metadata and per-platform summary tables."""
    run_date_raw = tw_env.get("run_date", "")
    try:
        from datetime import datetime, timezone
        run_date = (
            datetime.fromisoformat(run_date_raw)
            .astimezone(timezone.utc)
            .strftime("%Y-%m-%d %H:%M:%S UTC")
        )
    except Exception:
        run_date = run_date_raw

    scenarios = sorted({s["name"] for s in suites})
    platforms, stats, (tp, tf, ts, te) = _compute_platform_stats(suites)
    total = tp + tf + ts + te
    total_str = (
        f"{total} (passed: {tp}"
        + (f", failed: {tf}" if tf else "")
        + (f", skipped: {ts}" if ts else "")
        + (f", error: {te}" if te else "")
        + ")"
    )

    lines = [
        ".. list-table:: Test Run Metadata",
        "   :header-rows: 0",
        "   :widths: 25 75",
        "",
        "   * - Run date",
        f"     - {run_date}",
        "   * - Zephyr version",
        f"     - ``{tw_env.get('zephyr_version', '—')}``",
        "   * - Toolchain",
        f"     - {tw_env.get('toolchain', '—')}",
        "   * - Host OS",
        f"     - {tw_env.get('os', '—')}",
        "   * - Test scenarios",
        f"     - {', '.join(f'``{s}``' for s in scenarios)}",
        "   * - Platforms",
        f"     - {', '.join(f'``{p}``' for p in platforms)}",
        "   * - Total test cases",
        f"     - {total_str}",
        "",
        ".. list-table:: Results per Platform",
        "   :header-rows: 1",
        "   :widths: 50 15 15 10 10",
        "",
        "   * - Platform",
        "     - Passed",
        "     - Failed",
        "     - Skipped",
        "     - Total",
    ]
    for plat, p, f, s, e, total_plat in stats:
        lines += [
            f"   * - ``{plat}``",
            f"     - {p}",
            f"     - {f + e}",
            f"     - {s}",
            f"     - {total_plat}",
        ]
    lines.append("")
    return lines


# ---------------------------------------------------------------------------
# TestModuleDirective
# ---------------------------------------------------------------------------

class TestModuleDirective(Directive):
    """
    Emit sphinx-needs test_case nodes for all ZTEST functions in a module group.

    Usage::

        .. testmodule:: safe_data_module
           :module: tests/safe_data
    """

    required_arguments = 1
    optional_arguments = 0
    has_content = False
    option_spec = {
        "module": directives.unchanged,
    }

    def run(self):
        group_name = self.arguments[0].strip()
        module_path = self.options.get("module", "").strip("/")
        env = self.state.document.settings.env
        app = env.app

        xml_dir = Path(getattr(app.config, "testspec_doxygen_xml", "") or "")
        if not xml_dir.is_dir():
            return [self.state_machine.reporter.error(
                f"testmodule: testspec_doxygen_xml dir not found: {xml_dir}",
                nodes.literal_block(group_name, group_name),
                line=self.lineno,
            )]

        page_depth = len(Path(env.docname).parts) - 1
        page_prefix = "../" * page_depth
        testspec_html_dir = page_prefix + app.config.testspec_doxygen_url
        api_html_dir = page_prefix + app.config.api_doxygen_url
        # The Zephyr module that contains doc/ (module_path is relative to it).
        module_root = Path(env.srcdir).resolve().parents[1]

        if not hasattr(env, "_testmodule_group_index"):
            try:
                env._testmodule_group_index = load_group_index(xml_dir)
            except RuntimeError as exc:
                logger.warning(str(exc))
                return [nodes.paragraph(text=str(exc))]

        module_refid = env._testmodule_group_index.get(group_name)
        if module_refid is None:
            logger.warning(f"testmodule: Doxygen group '{group_name}' not found in index.xml")
            return [nodes.paragraph(text=f"[testmodule: group '{group_name}' not in Doxygen index]")]

        module_group_xml = xml_dir / f"{module_refid}.xml"
        if not module_group_xml.exists():
            logger.warning(f"testmodule: XML file not found for group '{group_name}': {module_group_xml}")
            return [nodes.paragraph(text=f"[testmodule: XML missing for '{group_name}']")]

        module_cdef = ET.parse(module_group_xml).getroot().find("compounddef")
        suite_refids, proc_refids = _classify_inner_groups(module_cdef, xml_dir)

        scenario_lines = build_scenario_table(module_root / module_path / "testcase.yaml")
        all_rst = list(scenario_lines)
        for suite_refid in suite_refids:
            all_rst += _build_suite_rst(suite_refid, xml_dir, testspec_html_dir, api_html_dir, module_path)
        for proc_refid in proc_refids:
            all_rst += _build_proc_group_rst(proc_refid, xml_dir, testspec_html_dir, api_html_dir)

        _maybe_dump_rst(app, env.docname, "testmodule", group_name, "\n".join(all_rst))
        return _render_rst(all_rst, self.state, self.content_offset, match_titles=True)


# ---------------------------------------------------------------------------
# TestReportDirective
# ---------------------------------------------------------------------------

class TestReportDirective(Directive):
    """
    Emit sphinx-needs test_result nodes from a twister_report.xml.

    Usage::

        .. testreport:: twister_report.xml
           :module: safe_data.api
    """

    required_arguments = 1
    optional_arguments = 0
    has_content = False
    option_spec = {
        "module": directives.unchanged,
    }

    def run(self):
        xml_path = self.arguments[0].strip()
        module_filter = self.options.get("module", "").strip() or None
        env = self.state.document.settings.env
        app = env.app

        if not Path(xml_path).is_absolute():
            base = getattr(app.config, "twister_output_dir", "") or str(
                Path(env.doc2path(env.docname)).parent
            )
            xml_path = str(Path(base) / xml_path)

        ext_needs = getattr(app.config, "needs_external_needs", [])
        spec_json = ext_needs[0].get("json_path", "") if ext_needs else ""
        if not spec_json or not Path(spec_json).exists():
            msg = f"[testreport: spec needs.json not found: {spec_json!r}]"
            logger.warning(f"testreport: {msg}")
            return [nodes.paragraph(text=msg)]

        try:
            spec_lookup = load_spec_lookup(spec_json)
        except Exception as exc:
            logger.warning(str(exc))
            return [nodes.paragraph(text=str(exc))]

        if not Path(xml_path).exists():
            msg = f"[testreport: twister XML not found: {xml_path}]"
            logger.warning(f"testreport: {msg}")
            return [nodes.paragraph(text=msg)]

        try:
            results = parse_twister_results(xml_path, module_filter)
        except Exception as exc:
            logger.warning(str(exc))
            return [nodes.paragraph(text=str(exc))]

        if not results:
            return [nodes.paragraph(text="[testreport: no matching results]")]

        suite_order, func_order, grouped = _group_results(results)
        twister_out_dir = getattr(app.config, "twister_output_dir", "")
        all_rst = (
            _build_results_rst(suite_order, func_order, grouped, spec_lookup)
            + _build_summary_table_rst(grouped, spec_lookup)
            + _build_exec_logs_rst(twister_out_dir, module_filter)
        )

        _maybe_dump_rst(app, env.docname, "testreport", module_filter or "", "\n".join(all_rst))
        return _render_rst(all_rst, self.state, self.content_offset, match_titles=True)


# ---------------------------------------------------------------------------
# TwisterInfoDirective
# ---------------------------------------------------------------------------

class TwisterInfoDirective(Directive):
    """Emit a run-metadata block and per-platform summary table from twister.json.

    Usage::

        .. twisterinfo:: twister.json
    """

    required_arguments = 1
    optional_arguments = 0
    has_content = False
    option_spec = {}

    def run(self):
        json_path = self.arguments[0].strip()
        env = self.state.document.settings.env
        app = env.app

        if not Path(json_path).is_absolute():
            base = getattr(app.config, "twister_output_dir", "") or str(
                Path(env.doc2path(env.docname)).parent
            )
            json_path = str(Path(base) / json_path)

        if not Path(json_path).exists():
            msg = f"[twisterinfo: twister.json not found: {json_path!r}]"
            logger.warning(f"twisterinfo: {msg}")
            return [nodes.paragraph(text=msg)]

        try:
            data = load_twister_meta(json_path)
        except Exception as exc:
            logger.warning(f"twisterinfo: cannot read {json_path}: {exc}")
            return [nodes.paragraph(text=str(exc))]

        lines = _build_twisterinfo_rst(data.get("environment", {}), data.get("testsuites", []))
        _maybe_dump_rst(app, env.docname, "twisterinfo", Path(json_path).name, "\n".join(lines))
        return _render_rst(lines, self.state, self.content_offset)


# ---------------------------------------------------------------------------
# Extension setup
# ---------------------------------------------------------------------------

def setup(app):
    app.add_config_value("testspec_doxygen_xml", "", "env")
    app.add_config_value("testspec_doxygen_url", "", "env")
    app.add_config_value("api_doxygen_url", "", "env")
    app.add_config_value("twister_output_dir", "", "env")
    app.add_config_value("dump_generated_rst", "", "env")
    app.add_directive("testmodule", TestModuleDirective)
    app.add_directive("testreport", TestReportDirective)
    app.add_directive("twisterinfo", TwisterInfoDirective)
    return {"version": "0.2", "parallel_read_safe": True}
