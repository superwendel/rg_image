# Building the documentation

The [introductory article](blog/introducing-rgi.md) is an original, ready-to-edit
blog draft. Its tables describe the current codec and link to the committed
aggregate measurements; no private artwork or per-image results are included.
It has not been published to an external blog.

The [specification sheet](rgi-specification-sheet.md) is the editable source for
the [two-page PDF](../output/pdf/rgi-specification.pdf). The longer
[wire specification](rgi_format.md) remains normative. Document revision 0.1.0
describes all three `rgif` profiles; it is not a new field in the file header.

To rebuild the PDF from the repository root, use Python 3.10 or newer:

```sh
python -m pip install -r tools/requirements-docs.txt
python tools/build_spec_pdf.py
```

The generator reads the Markdown sheet, uses standard PDF fonts, and writes
deterministic output without machine paths or a build timestamp. It fails if
the source no longer fits the intended two-page layout. No documentation
dependencies are needed by the codec or its normal build.

After editing, render both pages with Poppler and inspect them at readable size:

```sh
pdftoppm -scale-to 1600 -png output/pdf/rgi-specification.pdf build/rgi-spec
```

Check opcode fields, line wrapping, table borders, and page/column breaks against
the source. Keep the source, PDF, normative spec, and codec synchronized.
