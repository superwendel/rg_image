#!/usr/bin/env python3
"""Render the two-page RGI companion sheet from its Markdown source."""

from __future__ import annotations

import argparse
from html import escape
from pathlib import Path
import re

from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.platypus import (
    BaseDocTemplate, Flowable, Frame, FrameBreak, PageBreak, PageTemplate,
    Paragraph, Spacer, Table, TableStyle,
)


ROOT = Path(__file__).resolve().parents[1]
INK = colors.HexColor("#152C36")
ACCENT = colors.HexColor("#16746B")
MUTED = colors.HexColor("#53656C")
RULE = colors.HexColor("#CCD8DC")
PALE = colors.HexColor("#EFF5F5")
WIDTH, HEIGHT = A4
MARGIN = 32
GUTTER = 20
COL = (WIDTH - 2 * MARGIN - GUTTER) / 2

BODY = ParagraphStyle(
    "Body", fontName="Helvetica", fontSize=9, leading=11.7,
    textColor=INK, spaceAfter=6, alignment=TA_LEFT,
)
HEADING = ParagraphStyle(
    "Heading", parent=BODY, fontName="Helvetica-Bold", fontSize=10.2,
    leading=13, spaceBefore=9, spaceAfter=6, textColor=ACCENT,
    keepWithNext=True,
)
CELL = ParagraphStyle("Cell", parent=BODY, fontSize=8.15, leading=10.3, spaceAfter=0)
CODE = ParagraphStyle(
    "Code", parent=BODY, fontName="Courier", fontSize=8, leading=10.5,
    backColor=PALE, borderPadding=5, spaceBefore=4, spaceAfter=10,
)


def inline(text: str) -> str:
    """The sheet uses only links, code, bold, and italic inline markup."""
    text = escape(text)
    text = re.sub(r"`([^`]+)`", r'<font name="Courier">\1</font>', text)
    text = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", text)
    text = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r'<link href="\2" color="#16746B">\1</link>', text)
    if text.startswith("*") and text.endswith("*"):
        text = "<i>" + text[1:-1] + "</i>"
    return text


class PackedByte(Flowable):
    """A byte diagram for the worked two-bit palette example."""

    def __init__(self):
        super().__init__()
        self.width = COL
        self.height = 65

    def draw(self):
        c = self.canv
        step = COL / 8
        c.setFont("Helvetica", 7.5)
        c.setFillColor(MUTED)
        c.drawString(0, 55, "bit 7 (MSB)")
        c.drawRightString(COL, 55, "bit 0 (LSB)")
        for i, bit in enumerate("00100111"):
            c.setFillColor(PALE if i < 2 else colors.white)
            c.setStrokeColor(RULE)
            c.rect(i * step, 29, step, 19, stroke=1, fill=1)
            c.setFillColor(INK)
            c.setFont("Courier-Bold", 9)
            c.drawCentredString((i + 0.5) * step, 35, bit)
        for i, label in enumerate(("padding", "index 2", "index 1", "index 0")):
            c.setFont("Helvetica", 7.5)
            c.setFillColor(MUTED)
            c.drawCentredString((i + 0.5) * 2 * step, 17, label)


class Sheet(BaseDocTemplate):
    def afterPage(self):
        if self.page > 2:
            raise ValueError("Specification overflowed two pages; revise layout or content.")


def page_chrome(canvas, doc):
    canvas.saveState()
    canvas.setFillColor(INK)
    canvas.setFont("Helvetica-Bold", 29)
    canvas.drawString(MARGIN, HEIGHT - 51, "RGI")
    canvas.setFont("Helvetica", 12)
    canvas.drawString(MARGIN + 67, HEIGHT - 49, "Reverse Gravity Image")
    canvas.setFont("Helvetica-Bold", 8)
    canvas.setFillColor(ACCENT)
    canvas.drawRightString(WIDTH - MARGIN, HEIGHT - 32, "SPECIFICATION SHEET")
    canvas.setFillColor(MUTED)
    canvas.setFont("Helvetica", 8)
    canvas.drawString(MARGIN, HEIGHT - 70, doc.revision)
    canvas.setStrokeColor(ACCENT)
    canvas.setLineWidth(1.2)
    canvas.line(MARGIN, HEIGHT - 81, WIDTH - MARGIN, HEIGHT - 81)
    canvas.setFont("Helvetica-Bold", 8.5)
    canvas.setFillColor(INK)
    label = "STREAM LAYOUT / PROFILES 0 + 1" if doc.page == 1 else "PROFILE 2 / VALIDATION"
    canvas.drawString(MARGIN, HEIGHT - 98, label)
    canvas.setStrokeColor(RULE)
    canvas.setLineWidth(0.5)
    canvas.line(MARGIN, 37, WIDTH - MARGIN, 37)
    canvas.setFont("Helvetica", 7)
    canvas.setFillColor(MUTED)
    canvas.drawString(MARGIN, 25, "github.com/superwendel/rg_image")
    canvas.linkURL("https://github.com/superwendel/rg_image", (MARGIN, 23, MARGIN + 175, 33))
    canvas.drawCentredString(WIDTH / 2 + 25, 25, "Normative contract: docs/rgi_format.md")
    canvas.linkURL("https://github.com/superwendel/rg_image/blob/main/docs/rgi_format.md",
                   (WIDTH / 2 - 60, 23, WIDTH / 2 + 125, 33))
    canvas.drawRightString(WIDTH - MARGIN, 25, f"{doc.page} / 2")
    canvas.restoreState()


def table(lines: list[str]) -> Table:
    rows = [[cell.strip() for cell in line.strip().strip("|").split("|")] for line in lines]
    rows.pop(1)  # Markdown separator.
    count = len(rows[0])
    widths = [COL * .17, COL * .12, COL * .71] if count == 3 else [COL * .47, COL * .53]
    cells = [[Paragraph(inline(cell), CELL) for cell in row] for row in rows]
    item = Table(cells, colWidths=widths, hAlign="LEFT", repeatRows=1)
    item.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), PALE),
        ("LINEBELOW", (0, 0), (-1, 0), .6, ACCENT),
        ("LINEBELOW", (0, 1), (-1, -1), .35, RULE),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 4),
        ("RIGHTPADDING", (0, 0), (-1, -1), 4),
        ("TOPPADDING", (0, 0), (-1, -1), 4),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
    ]))
    return item


def flowables(source: str) -> list:
    lines = source.splitlines()
    story = []
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        if not line or line.startswith(("# ", "## ", "**Specification", "Source:", "Normative specification:")):
            i += 1
            continue
        if line.startswith("<!-- Page break:"):
            story.append(PageBreak())
        elif line.startswith("### "):
            heading = line[4:]
            if heading.startswith(("Profile 0 -", "Literal packing")):
                story.append(FrameBreak())
            story.append(Paragraph(inline(heading), HEADING))
        elif line.startswith("|"):
            chunk = []
            while i < len(lines) and lines[i].startswith("|"):
                chunk.append(lines[i])
                i += 1
            story.extend((table(chunk), Spacer(1, 7)))
            continue
        elif line.startswith("```"):
            chunk = []
            i += 1
            while i < len(lines) and not lines[i].startswith("```"):
                chunk.append(lines[i])
                i += 1
            if chunk[0].startswith("bit 7"):
                story.append(PackedByte())
            else:
                story.append(Paragraph("<br/>".join(escape(x) for x in chunk), CODE))
        else:
            chunk = [line]
            i += 1
            while i < len(lines) and lines[i].strip() and not lines[i].startswith(("#", "|", "```", "<!--")):
                chunk.append(lines[i].strip())
                i += 1
            story.append(Paragraph(inline(" ".join(chunk)), BODY))
            continue
        i += 1
    return story


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "docs/rgi-specification.pdf")
    args = parser.parse_args()
    source = (ROOT / "docs/rgi-specification-sheet.md").read_text(encoding="utf-8")
    revision = re.search(r"\*\*(Specification sheet.+)\*\*", source)
    if revision is None:
        raise ValueError("Missing document revision in Markdown source")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(".tmp.pdf")
    doc = Sheet(
        str(temporary), pagesize=A4, invariant=1, pageCompression=1,
        title="RGI - Reverse Gravity Image: Specification Sheet",
        author="Steven Wendel", subject="The rgif RGBA8 stream, profiles 0/1/2",
    )
    doc.revision = revision.group(1)
    frames = [Frame(MARGIN + i * (COL + GUTTER), 49, COL, HEIGHT - 157,
                    leftPadding=0, rightPadding=0, topPadding=0, bottomPadding=0)
              for i in range(2)]
    doc.addPageTemplates(PageTemplate(id="TwoColumns", frames=frames, onPage=page_chrome))
    try:
        doc.build(flowables(source))
        if doc.page != 2:
            raise ValueError(f"Expected two pages, got {doc.page}")
        temporary.replace(args.output)
    finally:
        temporary.unlink(missing_ok=True)
    print(f"Wrote {args.output} (2 pages)")


if __name__ == "__main__":
    main()
