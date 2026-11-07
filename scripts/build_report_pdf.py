#!/usr/bin/env python3
"""Render docs/informe.md as a paginated Spanish PDF using ReportLab."""

from __future__ import annotations

import html
import re
from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_JUSTIFY, TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    LongTable,
    PageBreak,
    PageTemplate,
    Paragraph,
    Preformatted,
    SimpleDocTemplate,
    Spacer,
    TableStyle,
    Frame,
)


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "docs" / "informe.md"
OUTPUT = ROOT / "output" / "pdf" / "informe_sistemas_operativos_parcial3.pdf"
FONT_REGULAR = Path("C:/Windows/Fonts/arial.ttf")
FONT_BOLD = Path("C:/Windows/Fonts/arialbd.ttf")


def inline_markup(text: str) -> str:
    text = html.escape(text, quote=False)
    text = re.sub(r"`([^`]+)`", r'<font name="Arial">\1</font>', text)
    text = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", text)
    text = re.sub(r"(?<!\*)\*([^*]+)\*(?!\*)", r"<i>\1</i>", text)
    text = re.sub(r"\[([^]]+)\]\(([^)]+)\)", r'<link href="\2" color="#176b87">\1</link>', text)
    return text


def parse_table_row(line: str) -> list[str]:
    return [cell.strip() for cell in line.strip().strip("|").split("|")]


def build_story(text: str):
    styles = getSampleStyleSheet()
    styles.add(ParagraphStyle(
        name="ReportTitle", parent=styles["Title"], fontName="Arial-Bold",
        fontSize=21, leading=26, textColor=colors.HexColor("#123047"),
        alignment=TA_CENTER, spaceAfter=8 * mm,
    ))
    styles.add(ParagraphStyle(
        name="H1Custom", parent=styles["Heading1"], fontName="Arial-Bold",
        fontSize=16, leading=20, textColor=colors.HexColor("#123047"),
        spaceBefore=5 * mm, spaceAfter=3 * mm, keepWithNext=True,
    ))
    styles.add(ParagraphStyle(
        name="H2Custom", parent=styles["Heading2"], fontName="Arial-Bold",
        fontSize=12.5, leading=16, textColor=colors.HexColor("#176b87"),
        spaceBefore=4 * mm, spaceAfter=2 * mm, keepWithNext=True,
    ))
    styles.add(ParagraphStyle(
        name="H3Custom", parent=styles["Heading3"], fontName="Arial-Bold",
        fontSize=10.5, leading=13, textColor=colors.HexColor("#176b87"),
        spaceBefore=3 * mm, spaceAfter=1.5 * mm, keepWithNext=True,
    ))
    styles.add(ParagraphStyle(
        name="BodyCustom", parent=styles["BodyText"], fontName="Arial",
        fontSize=9.2, leading=13, alignment=TA_JUSTIFY, spaceAfter=2.5 * mm,
    ))
    styles.add(ParagraphStyle(
        name="BulletCustom", parent=styles["BodyText"], fontName="Arial",
        fontSize=9.2, leading=12.5, leftIndent=5 * mm, firstLineIndent=-3 * mm,
        spaceAfter=1.5 * mm,
    ))
    styles.add(ParagraphStyle(
        name="TableCustom", parent=styles["BodyText"], fontName="Arial",
        fontSize=7.3, leading=9.2, alignment=TA_LEFT,
    ))
    styles.add(ParagraphStyle(
        name="TableHeaderCustom", parent=styles["TableCustom"], fontName="Arial-Bold",
        textColor=colors.white,
    ))
    styles.add(ParagraphStyle(
        name="CodeCustom", parent=styles["Code"], fontName="Courier",
        fontSize=7.8, leading=10, leftIndent=4 * mm, rightIndent=3 * mm,
        borderColor=colors.HexColor("#d8e1e7"), borderWidth=0.5,
        borderPadding=4 * mm, backColor=colors.HexColor("#f4f7f9"),
        spaceBefore=1 * mm, spaceAfter=3 * mm,
    ))

    lines = text.splitlines()
    story = []
    i = 0
    first_heading = True
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()
        if not stripped:
            i += 1
            continue
        if stripped.startswith("```"):
            i += 1
            code = []
            while i < len(lines) and not lines[i].strip().startswith("```"):
                code.append(lines[i])
                i += 1
            i += 1
            story.append(Preformatted("\n".join(code), styles["CodeCustom"], maxLineLength=100))
            continue
        if stripped.startswith("|"):
            raw_rows = []
            while i < len(lines) and lines[i].strip().startswith("|"):
                raw_rows.append(parse_table_row(lines[i]))
                i += 1
            rows = []
            for row in raw_rows:
                if row and all(re.fullmatch(r":?-{3,}:?", cell.replace(" ", "")) for cell in row):
                    continue
                style = styles["TableHeaderCustom"] if not rows else styles["TableCustom"]
                rows.append([Paragraph(inline_markup(cell), style) for cell in row])
            if rows:
                columns = max(map(len, rows))
                width = (A4[0] - 38 * mm) / columns
                table = LongTable(rows, colWidths=[width] * columns, repeatRows=1, hAlign="LEFT")
                table.setStyle(TableStyle([
                    ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#176b87")),
                    ("GRID", (0, 0), (-1, -1), 0.35, colors.HexColor("#aebdc6")),
                    ("VALIGN", (0, 0), (-1, -1), "TOP"),
                    ("LEFTPADDING", (0, 0), (-1, -1), 4),
                    ("RIGHTPADDING", (0, 0), (-1, -1), 4),
                    ("TOPPADDING", (0, 0), (-1, -1), 4),
                    ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
                    ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, colors.HexColor("#f3f7f9")]),
                ]))
                story.extend([table, Spacer(1, 3 * mm)])
            continue
        if stripped.startswith("#"):
            level = len(stripped) - len(stripped.lstrip("#"))
            heading = stripped[level:].strip()
            if level == 1 and first_heading:
                story.append(Paragraph(inline_markup(heading), styles["ReportTitle"]))
                first_heading = False
            else:
                style_name = "H1Custom" if level == 1 else "H2Custom" if level == 2 else "H3Custom"
                story.append(Paragraph(inline_markup(heading), styles[style_name]))
            i += 1
            continue
        if stripped == "---":
            story.append(Spacer(1, 3 * mm))
            i += 1
            continue
        if re.match(r"^[-*]\s+", stripped):
            item = re.sub(r"^[-*]\s+", "", stripped)
            story.append(Paragraph("• " + inline_markup(item), styles["BulletCustom"]))
            i += 1
            continue

        paragraph = [stripped]
        i += 1
        while i < len(lines):
            candidate = lines[i].strip()
            if (not candidate or candidate.startswith(("#", "```", "|", "- ", "* ")) or candidate == "---"):
                break
            paragraph.append(candidate)
            i += 1
        story.append(Paragraph(inline_markup(" ".join(paragraph)), styles["BodyCustom"]))
    return story


def add_page_footer(canvas, document):
    canvas.saveState()
    canvas.setStrokeColor(colors.HexColor("#d8e1e7"))
    canvas.line(19 * mm, 14 * mm, A4[0] - 19 * mm, 14 * mm)
    canvas.setFont("Arial", 8)
    canvas.setFillColor(colors.HexColor("#526571"))
    canvas.drawString(19 * mm, 9 * mm, "Sistemas Operativos - Parcial 3")
    canvas.drawRightString(A4[0] - 19 * mm, 9 * mm, f"Página {document.page}")
    canvas.restoreState()


def main():
    if not SOURCE.is_file():
        raise SystemExit(f"No existe el informe fuente: {SOURCE}")
    if not FONT_REGULAR.is_file() or not FONT_BOLD.is_file():
        raise SystemExit("No se encontraron Arial regular y negrita en C:/Windows/Fonts")
    pdfmetrics.registerFont(TTFont("Arial", str(FONT_REGULAR)))
    pdfmetrics.registerFont(TTFont("Arial-Bold", str(FONT_BOLD)))
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    doc = SimpleDocTemplate(
        str(OUTPUT), pagesize=A4, title="Sistemas Operativos Parcial 3 - Informe",
        author="EAFIT - Sistemas Operativos", leftMargin=19 * mm,
        rightMargin=19 * mm, topMargin=17 * mm, bottomMargin=20 * mm,
    )
    doc.build(build_story(SOURCE.read_text(encoding="utf-8")), onFirstPage=add_page_footer,
              onLaterPages=add_page_footer)
    print(OUTPUT)


if __name__ == "__main__":
    main()

