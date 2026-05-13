---
name: embedded-documentation-design
description: Design spec for professional embedded systems documentation using Doxygen, Markdown design spec, and Mermaid diagrams for the LoRa Weather Base GSM Sensor Node firmware.
---

# Embedded Documentation Design Specification

**Project:** LoRa Weather Base — GSM Sensor Node Firmware
**Document ID:** PCB_GSM_SENSOR_IN_DOC_DSN_R01
**Date:** 2026-05-13
**Author:** Armmylool

---

## 1. Objective

Generate professional-grade documentation for the ESP32-C3 XIAO weather station firmware using industry-standard embedded systems documentation tooling. The deliverable is three integrated components:

1. **Doxygen HTML API documentation** — auto-generated from annotated source code
2. **Formal design specification** — Markdown document with embedded Mermaid diagrams
3. **Architecture diagrams** — six Mermaid diagrams covering hardware, software, and data flow

---

## 2. Approach: Doxygen-First

**Selected approach:** Doxygen for code-level API documentation + Markdown design spec + Mermaid diagrams.

**Rationale:** Doxygen is the industry standard for C/C++ embedded projects. It keeps documentation close to code (single source of truth), generates navigable HTML with cross-references, and requires only a single binary dependency. Mermaid diagrams render natively on GitHub and VS Code without additional tooling.

**Alternatives considered:**
- Markdown-only: Zero toolchain but API docs drift from code
- Doxygen + Sphinx + Breathe: Maximum professionalism but heavy Python toolchain, overkill for a single firmware project

---

## 3. Documentation Structure

```
docs/
├── Doxyfile                      # Doxygen configuration
├── doxygen-custom.css            # Custom HTML styling
├── html/                         # Generated Doxygen output (git-ignored)
├── design-spec.md                # Formal design specification
├── diagrams/
│   ├── class-diagram.md          # UML class relationships
│   ├── state-machine.md          # State machine diagram
│   ├── sequence-normal.md        # Normal operation sequence
│   ├── sequence-fallback.md      # GSM failure fallback sequence
│   ├── component-diagram.md      # Hardware/software component map
│   └── data-flow.md              # Data flow: sensor → MQTT
└── api/
    └── api-reference.md          # High-level API reference (links to Doxygen)
```

---

## 4. Doxygen Configuration

### 4.1 Input & Extraction

| Setting | Value | Rationale |
|---|---|---|
| `INPUT` | `src/ include/` | Only project source |
| `FILE_PATTERNS` | `*.cpp *.h` | C++ source and headers |
| `RECURSIVE` | `YES` | Scan subdirectories |
| `EXTRACT_ALL` | `YES` | Document all entities |
| `EXTRACT_PRIVATE` | `NO` | Public API only |
| `EXTRACT_STATIC` | `YES` | Include static functions/defines |

### 4.2 Output

| Setting | Value | Rationale |
|---|---|---|
| `OUTPUT_DIRECTORY` | `docs` | Keep docs self-contained |
| `GENERATE_HTML` | `YES` | Primary output format |
| `GENERATE_LATEX` | `NO` | Not needed |
| `HTML_OUTPUT` | `html` | Standard output folder |
| `GENERATE_TREEVIEW` | `YES` | Navigation sidebar |
| `SEARCHENGINE` | `YES` | Full-text search |
| `HTML_TIMESTAMP` | `YES` | Show generation date |

### 4.3 Project Metadata

| Setting | Value |
|---|---|
| `PROJECT_NAME` | LoRa Weather Base - GSM Sensor Node Firmware |
| `PROJECT_NUMBER` | 1.0 |
| `PROJECT_BRIEF` | ESP32-C3 XIAO weather station with GSM/GPRS, RS485 Modbus, WiFi AP, and MQTT |
| `OUTPUT_LANGUAGE` | English |

### 4.4 Source Browsing

| Setting | Value | Rationale |
|---|---|---|
| `SOURCE_BROWSER` | `YES` | Link to source from docs |
| `INLINE_SOURCES` | `NO` | Avoid bloating HTML |
| `REFERENCED_BY_RELATION` | `YES` | Show what calls each function |
| `REFERENCES_RELATION` | `YES` | Show what each function calls |

### 4.5 Preprocessor

| Setting | Value | Rationale |
|---|---|---|
| `ENABLE_PREPROCESSING` | `YES` | Handle Arduino macros |
| `MACRO_EXPANSION` | `YES` | Expand macros in docs |
| `EXPAND_ONLY_PREDEF` | `YES` | Only expand known macros |
| `PREDEFINED` | `F(x)=x PROGMEM= ICACHE_RODATA_ATTR= ARDUINO=100` | Arduino framework macros |

### 4.6 Custom Styling

A `doxygen-custom.css` file will provide clean styling appropriate for embedded systems documentation — dark header, light body, monospace code blocks, readable table formatting.

---

## 5. Code Annotation Plan

### 5.1 Annotation Standards

Every file gets:
- `@file` block with project name, brief description, author, date
- `@since` tag with version number

Every class gets:
- `@brief` — one-line summary
- `@details` — usage notes, constraints, thread safety if applicable

Every public method gets:
- `@brief` — what it does
- `@param` — each parameter with units and valid ranges
- `@return` — return value semantics
- `@note` — non-obvious constraints or side effects (optional)

Every struct/enum gets:
- `@brief` for the type
- `@brief` for each field with units and scaling

Every `#define` / `const` gets:
- `@brief` with units, default value rationale, and valid range

### 5.2 Per-File Annotation Scope

| File | Current State | Work Needed |
|---|---|---|
| `include/utilities.h` | Section headers only | `@brief` on every `#define` and `const` |
| `include/sensor_v2.h` | Minimal `@brief` on 3 methods | Full class docs, struct field docs, enum docs |
| `include/GsmHandler.h` | No docs | Full class docs, all public/private method docs |
| `include/Memory.h` | Minimal `@brief` | Full class docs, method docs with file path semantics |
| `include/WifiApServer.h` | No docs | Full class docs, method docs, HTTP endpoint docs |
| `src/main.cpp` | Section headers | File-level doc, state machine overview |
| `src/sensor_v2.cpp` | No docs | Implementation notes for Modbus logic and median filter |
| `src/GsmHandler.cpp` | No docs | AT command flow notes, timeout strategy |
| `src/Memory.cpp` | No docs | CSV format notes, rollover strategy |
| `src/WifiApServer.cpp` | No docs | HTTP handler flow notes |

### 5.3 .gitignore Entry

The `docs/html/` directory is added to `.gitignore` to avoid committing generated artifacts.

---

## 6. Mermaid Diagrams

### 6.1 Diagram Inventory

| # | File | Diagram Type | Content |
|---|---|---|---|
| 1 | `component-diagram.md` | Block diagram | ESP32-C3 at center, SIM800L, RS485 transceiver, battery ADC, TPL5110, software layers |
| 2 | `class-diagram.md` | UML class diagram | RS485sensor, GsmHandler, Memory, WifiApServer, dataProcess with methods and relationships |
| 3 | `state-machine.md` | stateDiagram-v2 | S0-S7 states with all transitions including failure paths |
| 4 | `sequence-normal.md` | sequenceDiagram | Happy path: power-on through sensor reads, save, MQTT publish, DONE |
| 5 | `sequence-fallback.md` | sequenceDiagram | GSM failure: backup time estimation, data retention, publish skip |
| 6 | `data-flow.md` | flowchart | Raw Modbus registers → median filter → CSV → LittleFS → MQTT JSON |

### 6.2 Rendering

Each diagram is a standalone `.md` file with a Mermaid code block. They render in:
- VS Code Mermaid preview extension
- GitHub Markdown rendering
- Any Mermaid-compatible viewer

### 6.3 Design Spec Integration

The design specification (`design-spec.md`) embeds diagrams via standard Markdown links to the `diagrams/` folder, allowing standalone viewing or in-context reference.

---

## 7. Design Specification Document

### 7.1 Template

The design spec follows a professional embedded systems template:

1. **Document Control** — ID, revision, author, approval status, change log
2. **Introduction** — Purpose, scope, definitions/acronyms, references
3. **System Overview** — High-level description, operating environment
4. **Hardware Interface Specification** — Pin assignments, UART allocation, ADC circuit, TPL5110
5. **Software Architecture** — Module decomposition, dependency diagram, memory budget
6. **State Machine Specification** — State table, transition rules, timing budget
7. **Sensor Interface Specification** — Modbus register maps, data scaling, acquisition timing
8. **Data Persistence Design** — LittleFS layout, CSV format, rollover strategy
9. **Communication Protocol** — GSM/GPRS, NTP, MQTT, WiFi AP HTTP
10. **Error Handling & Fallback** — GSM failure, sensor timeout, watchdog, power-cut safety
11. **Configuration Reference** — Compile-time constants with defaults and valid ranges
12. **Build & Deployment** — PlatformIO commands, dependencies, flash procedure

### 7.2 Content Source

The design spec synthesizes information from:
- Existing `README.md` (timing analysis, register maps, state machine)
- Source code (actual implementation details, defaults, constants)
- Mermaid diagrams (embedded or linked)

It does not duplicate the README — it supersedes it as the authoritative design document. The existing README.md will be updated to contain a brief project overview and links to the new documentation files.

---

## 8. Implementation Sequence

The work proceeds in this order:

1. Create directory structure (`docs/diagrams/`, `docs/api/`)
2. Write `Doxyfile` and `doxygen-custom.css`
3. Update `.gitignore` to exclude `docs/html/`
4. Annotate header files (`.h`) with Doxygen comments
5. Annotate source files (`.cpp`) with Doxygen comments
6. Write all 6 Mermaid diagram files
7. Write the design specification document
8. Write the API reference overview
9. Run Doxygen and verify HTML output
10. Commit all documentation files

---

## 9. Success Criteria

- [ ] Doxygen generates clean HTML with all classes, structs, enums, functions documented
- [ ] All 6 Mermaid diagrams render correctly
- [ ] Design spec is complete with all 12 sections
- [ ] No placeholder content (no TBD, TODO, or incomplete sections)
- [ ] All code annotations are accurate and match implementation
- [ ] Documentation builds with a single `doxygen` command
