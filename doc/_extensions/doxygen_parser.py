"""Doxygen XML parsing — no Sphinx dependency."""
# Ported from zephyr-safety/doc/_extensions (same name) for the Safe Data API
# documentation; keep in sync conceptually with that prototype.
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import TypedDict

__all__ = [
    "MemberInfo",
    "elem_text",
    "para_text",
    "list_to_rst_lines",
    "section_to_rst",
    "see_to_rst",
    "extract_params",
    "parse_memberdef",
    "load_group_index",
]


class MemberInfo(TypedDict):
    name: str
    test_id: str
    req_ids: list[str]
    status: str
    source_file: str
    doxygen_url: str
    brief: str
    detail_paras: list[str]
    see_rst: str
    body_sections: list[list[str]]


def elem_text(elem: ET.Element | None) -> str:
    """Walk the element tree collecting all text nodes (.text and .tail),
    join them, then normalise any runs of whitespace down to single spaces."""
    if elem is None:
        return ""
    buf = []
    if elem.text:  # skips empty string — intentional, Doxygen never emits meaningful empty text nodes
        buf.append(elem.text)
    for child in elem:
        buf.append(elem_text(child))
        if child.tail:
            buf.append(child.tail)
    return " ".join(" ".join(buf).split())


def para_text(para: ET.Element | None) -> str:
    """Extract inline text from a <para>, rendering code/ref as plain text."""
    if para is None:
        return ""
    parts = []
    if para.text:
        parts.append(para.text)
    for child in para:
        if child.tag in ("xrefsect", "simplesect", "orderedlist", "itemizedlist", "parameterlist"):
            pass  # skip structural children
        elif child.tag == "computeroutput":
            ref = child.find("ref[@kindref='member']")
            if ref is not None:
                parts.append(f":c:func:`{(ref.text or '').rstrip('()').strip()}`")
            else:
                parts.append(f"``{elem_text(child)}``")
        elif child.tag in ("bold", "emphasis"):
            parts.append(elem_text(child))
        elif child.tag == "ref":
            kindref = child.get("kindref", "")
            t = (child.text or "").strip()
            if kindref == "member" and t:
                parts.append(f":c:func:`{t.rstrip('()').strip()}`")
            else:
                parts.append(elem_text(child))
        else:
            parts.append(elem_text(child))
        if child.tail:
            parts.append(child.tail)
    return " ".join(" ".join(parts).split())


def list_to_rst_lines(listelem: ET.Element, marker: str) -> list[str]:
    """Convert <orderedlist> or <itemizedlist> children to RST list lines."""
    lines = []
    for item in listelem.findall("listitem"):
        item_parts = []
        for para in item.findall("para"):
            t = para_text(para).strip()
            if t:
                item_parts.append(t)
        if item_parts:
            first = item_parts[0]
            rest = item_parts[1:]
            indent = " " * (len(marker) + 1)
            lines.append(f"{marker} {first}")
            for extra in rest:
                lines.append(f"{indent}{extra}")
    return lines


def section_to_rst(simplesect: ET.Element) -> list[str]:
    """Render a <simplesect kind="par"> (Arrange/Act/Assert) into RST lines.
    Emits a .. rubric:: for the title, then renders each <para> child as an
    ordered list, unordered list, or plain prose paragraph."""
    title_el = simplesect.find("title")
    title = elem_text(title_el).strip() if title_el is not None else ""

    lines: list[str] = []
    if title:
        lines.append(f".. rubric:: {title}")
        lines.append("")

    for child in simplesect:
        if child.tag != "para":
            continue

        ol = child.find("orderedlist")
        il = child.find("itemizedlist")
        if ol is not None:
            lines.extend(list_to_rst_lines(ol, "#."))
            lines.append("")
        elif il is not None:
            lines.extend(list_to_rst_lines(il, "-"))
            lines.append("")
        else:
            text = para_text(child).strip()
            if text:
                lines.append(text)
                lines.append("")

    while lines and lines[-1] == "":
        lines.pop()
    return lines


def see_to_rst(simplesect_see: ET.Element, api_html_dir: str) -> str:
    """Render a <simplesect kind="see"> into a 'See also:' RST line.
    Each <ref> becomes a hyperlink (safety-API refs), a :c:func: role
    (member refs), or a plain code span, depending on its attributes."""
    refs: list[str] = []
    for ref in simplesect_see.findall("para/ref"):
        name = (ref.text or "").strip()
        if not name:
            continue
        refid = ref.get("refid", "")
        external = ref.get("external", "")
        kindref = ref.get("kindref", "")
        if refid and "_1" in refid:
            idx = refid.rfind("_1")
            compound = refid[:idx]
            anchor = refid[idx + 2:]
            if external:
                url = f"{api_html_dir}/{compound}.html#{anchor}"
                refs.append(f"`{name} <{url}>`__")
            elif kindref == "member":
                func_name = name.rstrip("()").strip()
                refs.append(f":c:func:`{func_name}`")
            else:
                refs.append(f"``{name}``")
        else:
            refs.append(f":c:func:`{name.rstrip('()').strip()}`")
    if refs:
        return "**See also:** " + ", ".join(refs)
    return ""


def extract_params(detaileddesc: ET.Element) -> list[tuple[str, str]]:
    """Walk all <parameterlist kind="param"> elements in the detailed description,
    collect each parameter's name(s) and prose description, and return them as
    (name, description) pairs. Multiple names per item are joined with ', '."""
    params: list[tuple[str, str]] = []
    for pl in detaileddesc.findall(".//parameterlist[@kind='param']"):
        for item in pl.findall("parameteritem"):
            names = [
                (n.text or "").strip()
                for n in item.findall("parameternamelist/parametername")
            ]
            name = ", ".join(n for n in names if n)
            desc = " ".join(
                para_text(p)
                for p in item.findall(".//parameterdescription/para")
            ).strip()
            if name:
                params.append((name, desc))
    return params



def parse_memberdef(
    memberdef: ET.Element,
    compound_id: str,
    testspec_html_dir: str,
    api_html_dir: str,
) -> MemberInfo:
    """Extract all structured fields from a <memberdef> element — name, source
    location, Doxygen URL, brief description, test ID, requirement refs, status,
    see-also, and Arrange/Act/Assert body sections — and return them as a MemberInfo."""
    name = memberdef.findtext("name", "").strip()

    loc = memberdef.find("location")
    source_file = ""
    if loc is not None:
        fpath = loc.get("bodyfile") or loc.get("file", "")
        line = loc.get("bodystart") or loc.get("line", "")
        if fpath:
            source_file = f"{Path(fpath).name} (line {line})"

    member_id = memberdef.get("id", "")
    prefix = compound_id + "_1"
    anchor = member_id[len(prefix):] if member_id.startswith(prefix) else member_id
    doxygen_url = f"{testspec_html_dir}/{compound_id}.html#{anchor}"

    brief = para_text(memberdef.find("briefdescription/para"))

    dd = memberdef.find("detaileddescription")
    test_id = ""
    req_ids: list[str] = []
    status = "draft"
    detail_paras: list[str] = []
    see_rst = ""
    if dd is not None:
        for xrefsect in dd.findall("para/xrefsect"):
            xid = xrefsect.get("id", "")
            xdesc = (xrefsect.findtext("xrefdescription/para") or "").strip()
            if "testids" in xid:
                test_id = xdesc
            elif "reqrefs" in xid:
                for _p in xrefsect.findall("xrefdescription/para"):
                    _rid = (_p.text or "").strip()
                    if _rid:
                        req_ids.append(_rid)
            elif "test_active" in xid:
                status = "active"
            elif "test_obsolete" in xid:
                status = "obsolete"
        for para in dd.findall("para"):
            text = para_text(para).strip()
            if text:
                detail_paras.append(text)
        see_sect = dd.find("para/simplesect[@kind='see']")
        if see_sect is not None:
            see_rst = see_to_rst(see_sect, api_html_dir)

    ibd = memberdef.find("inbodydescription")
    body_sections: list[list[str]] = []
    if ibd is not None:
        for ss in ibd.findall("para/simplesect[@kind='par']"):
            section_lines = section_to_rst(ss)
            if section_lines:
                body_sections.append(section_lines)

    return MemberInfo(
        name=name,
        test_id=test_id,
        req_ids=req_ids,
        status=status,
        source_file=source_file,
        doxygen_url=doxygen_url,
        brief=brief,
        detail_paras=detail_paras,
        see_rst=see_rst,
        body_sections=body_sections,
    )


def load_group_index(xml_dir: Path) -> dict[str, str]:
    """Parse Doxygen's index.xml and return a dict mapping each group's name
    to its refid, which is used as the filename stem for the group's XML file."""
    index_file = xml_dir / "index.xml"
    try:
        root = ET.parse(index_file).getroot()
    except (OSError, ET.ParseError) as e:
        raise RuntimeError(f"testmodule: cannot parse {index_file}: {e}") from e
    return {
        name: refid
        for c in root.findall("compound[@kind='group']")
        if (name := c.findtext("name")) and (refid := c.get("refid"))
    }
