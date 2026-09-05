# System Architecture & Technical Specifications
## System Log Analyzer & Error Detector

An in-depth guide to the internal architecture, streaming pipeline, memory management, and anomaly detection algorithms of the System Log Analyzer & Error Detector.

---

## 1. High-Level Architecture

The tool is organized into decoupled, single-responsibility modules interacting through passive domain models:

```text
[ Log Ingestion Sources ]
  ├── macOS Unified Log Archive (`log show`)
  ├── Real-Time Darwin Kernel Stream (`log stream`)
  ├── Linux Systemd Journal (`journalctl`) & `/var/log/syslog`
  ├── Windows Event Log (`wevtutil`)
  ├── Local Files (`sample_logs/*.log`)
  └── Interactive Terminal / Stdin
  ▼
[ Ingestion & Noise Filtering Engine ] (src/ingestion.cpp)
  └── Denylist rejects driver/hardware telemetry noise (AppleBCMWLAN, clocksyncd, etc.)
  ▼
[ Parsing & Normalization Engine ] (src/parsers.cpp, src/normalizer.cpp)
  ├── Precompiled Regex Matchers (Syslog & ISO timestamp formats)
  ├── Tolerant Secondary Fallback Strategy (timestamp extraction + raw payload)
  ├── Windows Event Log Multiline Block Parser
  ├── Multiline Continuation Parser (Stack traces)
  └── Parse Transparency Accounting (Parsed, Fallback, Invalid)
  ▼
[ Subsystem Classifier ] (src/classifier.cpp)
  └── Categorizes events into FILE, NETWORK, SECURITY, RESOURCE, PROCESS, SYSTEM
  ▼
[ Single-Pass Statistical & Telemetry Engine ] (src/analyzer.cpp)
  ├── O(N) Linear single-pass accumulation of severities, categories, recurring errors
  ├── O(B) Direct mathematical offset time bucketing (Zero sorting overhead)
  └── Real kernel process RSS telemetry (`getrusage`) & throughput telemetry
  ▼
[ Deterministic Anomaly Detection & Risk Engine ] (src/detection.cpp, src/risk.cpp)
  ├── Evaluates R001 to R005 triggers against centralized policy thresholds
  └── Computes calibrated 0-100 Risk Score with critical event floor (>=60)
  ▼
[ Remediation Engine ] (src/recommendations.cpp)
  └── Generates targeted operational guidance based on failure semantics
  ▼
[ Presentation & Export Layer ] (src/report.cpp, src/exporter.cpp, main.cpp)
  ├── Rich ASCII terminal layout with throughput & memory telemetry
  └── RFC 8259-compliant JSON export (`reports/*.json`)
```

---

## 2. Core Architectural Principles

### 2.1 Pure C++17 Standard Library (Zero Dependencies)
All functionality is implemented using the ISO C++17 standard library. The engine uses `<regex>` for tokenization, `<chrono>` for high-resolution timing, `<filesystem>` for path validation, `<sys/resource.h>` for POSIX memory measurement, and `<poll.h>`/`<unistd.h>` for asynchronous stream capturing. No external libraries (Boost, nlohmann/json, etc.) are required.

### 2.2 Strict $O(N) + O(B+A)$ Computational Complexity
- **Linear Accumulation ($O(N)$)**: Metrics (severity counts, subsystem categories, min/max timestamps, and frequency maps) are accumulated in a single sequential scan over the event collection.
- **Direct Mathematical Bucketing ($O(B)$)**: Standard log analyzers sort timestamps in $O(N \log N)$. This engine calculates time-bucket assignments mathematically:
  $$\text{bucket\_index} = \min\left(\left\lfloor \frac{t_i - t_{\text{start}}}{\Delta t_{\text{bucket}}} \right\rfloor, B - 1\right)$$
  Each event is bucketed in $O(1)$ constant time, keeping total bucketing complexity at $O(N)$ with zero sorting overhead.
- **Rule Evaluations ($O(A)$)**: Evaluated in constant time over precomputed aggregates where $A \le 5 \ll N$.

### 2.3 Non-Blocking POSIX Live Kernel Stream Capture
Capturing live logs from `/usr/bin/log stream` poses deadlock and zombie process risks if child daemons inherit descriptors or block on filled pipe buffers. The ingestion engine mitigates this via direct POSIX system calls:
1. `pipe(pipefd)` creates an internal unidirectional pipe.
2. `fork()` spawns a dedicated worker process redirecting standard output to the pipe while discarding standard error.
3. The parent uses non-blocking `poll()` on the read descriptor, draining byte chunks into memory as they arrive.
4. When the user-specified capture duration elapses, the parent terminates the worker cleanly with `SIGKILL`, reaps the process via `waitpid()`, and drains remaining buffered bytes.

### 2.4 Multi-Tier Fallback Parsing & Parse Accounting
Logs rarely conform to a single schema. The engine uses a 4-tier cascade:
1. **Tier 1 (Strict Parse)**: Matches standard ISO or syslog format strings (`TIMESTAMP HOST PROCESS[PID]: [LEVEL] MESSAGE`).
2. **Tier 2 (Fallback Parse)**: Extracts any valid timestamp at the beginning of the line and treats the remainder as an unformatted message payload with default level assignment.
3. **Tier 3 (Multiline Continuation)**: Detects indented lines and stack traces, appending them to the previous event rather than discarding them.
4. **Tier 4 (Windows Block Parser)**: Accumulates multiline `Event[...]` blocks from `wevtutil` output into single discrete events.

Every report provides mathematical accounting of parsing quality:
$$\text{Total Raw Lines} = \text{Noise Discarded} + \text{Evaluated Events}$$
$$\text{Evaluated Events} = N_{\text{parsed}} + N_{\text{fallback}} + N_{\text{invalid}}$$

---

## 3. Detection Rule Specifications

| Rule ID | Rule Name | Severity | Threshold Trigger | Operational Rationale |
|---|---|---|---|---|
| **R001** | Repeated Error Pattern | ERROR | An identical error message occurs $\ge 3$ times | Flags connection retry loops, broken socket handshakes, or recurring resource locks. |
| **R002** | Critical System Event | CRITICAL | At least 1 event of severity `CRITICAL` or `FAULT` | Flags kernel panics, out-of-memory terminations, and hardware fault interrupts. Guarantees minimum risk score of 60. |
| **R003** | High Error Volume | ERROR | Total error count $\ge 5$ across the analysis period | Indicates elevated subsystem failure rate beyond operational baseline. |
| **R004** | High Error Ratio | WARNING | Errors constitute $> 20\%$ of all parsed events | Flags noisy environments where failures dominate healthy traffic. |
| **R005** | Time-Windowed Error Spike | ERROR | Any time bucket contains errors $> 2\times$ average error density | Catches sudden incident cascades, gateway outages, or deployment failures. |

---

## 4. Risk Assessment Model

The overall system risk score is calculated on a 0–100 bounded scale using a composite formula:

$$\text{Raw Score} = \min\left(100, \; \sum (w_l \times c_l) + 15 \times |A| + \begin{cases} 20 & \text{if } \frac{N_{\text{error}}}{N_{\text{total}}} > 0.20 \\ 0 & \text{otherwise} \end{cases}\right)$$

Where:
- $w_l$ is the severity weight (`CRITICAL = 5`, `ERROR = 3`, `WARNING = 1`, `INFO = 0`).
- $c_l$ is the count of events at severity level $l$.
- $|A|$ is the number of active detection rules triggered.
- If rule `R002` triggers (any `CRITICAL` event found), a floor override guarantees $\text{Risk Score} \ge 60$.

### Risk Bands
- **0 – 19**: `HEALTHY` — System operational within nominal parameters.
- **20 – 39**: `NORMAL` — Minor warnings observed, no critical anomalies.
- **40 – 59**: `WARNING` — Elevated error volume requiring routine review.
- **60 – 79**: `HIGH RISK` — Significant anomalies or recurring failures detected.
- **80 – 100**: `CRITICAL` — Active severe system incident or outage conditions.

---

## 5. Memory & Performance Telemetry

- **macOS & Linux**: Peak Resident Set Size (RSS) is queried via `getrusage(RUSAGE_SELF, &usage)`. On macOS, `ru_maxrss` returns bytes (converted to KB); on Linux, `ru_maxrss` returns KB directly.
- **Windows**: Peak Working Set Size is queried via `K32GetProcessMemoryInfo()`.
- **Throughput**: Calculated by measuring high-precision wall-clock time via `std::chrono::steady_clock` from stream ingestion to report generation:
  $$\text{Throughput} = \frac{N_{\text{events}}}{\Delta t_{\text{seconds}}}$$
  Benchmarked performance consistently ranges between **850,000 and 4,000,000 events/second** on standard x86_64 / ARM64 architectures.

---

## 6. Self-Diagnosis & Automated Test Suite

The built-in test suite (`./bin/log_analyzer --test`) exercises 14 isolated unit and fault-injection cases:
1. **Noise Filtering**: Validates rejection of kernel driver telemetry noise.
2. **Standard Parser**: Verifies exact tokenization of ISO/syslog timestamped lines.
3. **Fallback Parser**: Validates recovery from non-standard formats.
4. **Subsystem Classification**: Verifies keyword mappings across all 6 categories.
5. **Direct Bucketing**: Validates chronological distribution and edge-case timestamps.
6. **Rule R001**: Triggers on 3+ repeated errors.
7. **Rule R002**: Triggers on critical/fault level events.
8. **Rule R003**: Triggers when error volume reaches threshold.
9. **Rule R004**: Triggers when error ratio exceeds 20%.
10. **Rule R005**: Triggers on localized time-bucket density spikes.
11. **Risk Score Floor**: Verifies critical events enforce minimum 60 risk score.
12. **Zero-Event Guard**: Negative test ensuring division-by-zero protection on empty inputs.
13. **Corrupted Timestamp Guard**: Validates graceful fallback on malformed dates.
14. **JSON Serialization**: Verifies RFC 8259-compliant document structure.
