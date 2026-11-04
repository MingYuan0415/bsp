**English** | [简体中文](README_ZH.md)

# Hardware Documents

`documents.yaml` is the board's document registry. It records the official
source, revision, retrieval date, scope and redistribution state. The registry
is a set of references, not a substitute for the vendor's current document.

The PDFs and 3D files are `link-only` by default. Put a legally obtained local
copy under `layers/bsp/docs-local/<board>/`; that directory is ignored by Git.
When a downloaded file is used for a design decision, record its SHA-256 and
revision in `documents.yaml` before relying on it.

Hardware facts that code depends on (pins, buses, addresses, reset order,
strapping and power rails) stay in the board backend and its README. Do not
copy a whole datasheet into a source comment; cite the registry entry and the
concrete board file instead.
