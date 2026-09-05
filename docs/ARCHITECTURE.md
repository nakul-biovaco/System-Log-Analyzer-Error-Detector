# System Architecture & Technical Specifications

An in-depth specification of the internal architecture, streaming pipelines, self-healing error recovery, memory management, and anomaly detection algorithms of the System Log Analyzer & Error Detector.

> **Authors:** Built by **Nakul Mundhada** &bull; Co-authored by **Prasad Akle**

---

## 1. High-Level Architectural Pipeline

The system is organized into decoupled, single-responsibility modules communicating through lightweight, passive domain structures:

```text
[ Real-Time Ingestion Layer ]
  ├── macOS Unified Log Archive (`/usr/bin/log show --last <mins>m`)
  ├── Real-Time Darwin Kernel Stream (`/usr/bin/log stream --style syslog`)
  ├── Linux Systemd Journal (`journalctl -n <count> --no-pager`)
  ├── Windows Event Log (`wevtutil qe <channel> /c:<count>`)
  └── File Stream / Standard Input
  ▼
[ Self-Healing Error Correction Engine ] (src/recovery.cpp)
  ├── Stage 1: ASCII Sanitization (Strips unprintable control bytes < 0x20)
  ├── Stage 2: Multiline Traceback Stitching (Detects 'at ...', 'File "..."' and joins parent)
  ├── Stage 3: Chronological Timestamp Imputation (Calculates missing timestamps from sequence)
  └── Stage 4: Semantic Severity Restoration (Infers ERROR/WARNING via failure keywords)
  ▼
[ Constant-Memory Streaming Ring Buffer ] (src/recovery.cpp)
  └── Caches working set to 5,000 discrete records, shedding oldest events to bound RSS to ~5 MB
  ▼
[ Subsystem Classification Engine ] (src/classifier.cpp)
  └── Maps normalized records to SYSTEM, NETWORK, SECURITY, RESOURCE, FILE, or PROCESS
  ▼
[ Single-Pass Analytical & Telemetry Engine ] (src/analyzer.cpp)
  ├── Strict O(N) linear single-pass accumulation of frequencies and category distributions
  ├── O(B) direct mathematical offset time bucketing with zero sorting overhead
  └── Kernel-level process RSS telemetry via getrusage() / GetProcessMemoryInfo
  ▼
[ Deterministic Anomaly Detection Engine ] (src/detection.cpp)
  └── Evaluates rules R001 to R005 against centralized operational policies (include/rules.hpp)
  ▼
[ Operational Risk & Remediation Engine ] (src/risk.cpp, src/recommendations.cpp)
  ├── Calibrated 0-100 composite risk score with critical event floor override (score >= 60)
  └── Synthesizes actionable troubleshooting guidance from triggered anomaly evidence
  ▼
[ Dual Presentation Layer ] (src/report.cpp, src/server.cpp, gui/app.js)
  ├── High-throughput terminal ASCII telemetry report
  ├── Standalone Desktop Application with VS Code Integrated Terminal, dock drawer, and inspector
  └── RFC 8259-compliant JSON analysis report exports
```

---

## 2. Core Architectural Principles

### 2.1 Pure ISO C++17 Standard Library (Zero External Dependencies)
All functionality is implemented using the ISO/IEC 14882:2017 standard library:
- `<regex>` for tokenization and timestamp extraction.
- `<chrono>` for microsecond-resolution timing and duration arithmetic.
- `<filesystem>` for cross-platform path validation.
- `<sys/resource.h>` and `<sys/wait.h>` for POSIX process monitoring.
- `<poll.h>` and `<fcntl.h>` for asynchronous non-blocking descriptor polling.
- Zero dependencies on Boost, third-party parsers, or external JSON libraries.

### 2.2 Strict $O(N) + O(B+A)$ Computational Complexity
Standard log analyzers sort event timestamps chronologically in $O(N \log N)$ time. This engine bypasses sorting entirely by evaluating events in a single linear pass combined with mathematical direct-offset bucketing:
1. **Linear Scan ($O(N)$)**: Accumulates severity counts, subsystem distributions, minimum/maximum timestamps, and top error frequency hashes in one forward pass.
2. **Direct Mathematical Bucketing ($O(B)$)**: Each timestamp $t_i$ is mapped directly into bucket index $k$ in $O(1)$ constant time:
   $$k = \min\left(\left\lfloor \frac{t_i - t_{\text{start}}}{\Delta t_{\text{bucket}}} \right\rfloor, \; B - 1\right)$$
   Where:
   $$\Delta t_{\text{bucket}} = \max\left(1.0, \; \frac{t_{\text{end}} - t_{\text{start}}}{B}\right)$$
   Total bucketing complexity across $N$ events is strictly $O(N)$, completely eliminating the $O(N \log N)$ sorting bottleneck.
3. **Rule Evaluations ($O(A)$)**: All detection rules ($A \le 5$) are evaluated over pre-aggregated histograms in constant time ($O(1)$).

---

## 3. Self-Healing Error Recovery Pipeline

In real-world operating environments, logs often suffer from corruption, dropped headers, split lines, or missing fields. The self-healing recovery pipeline ([include/recovery.hpp](include/recovery.hpp), [src/recovery.cpp](src/recovery.cpp)) repairs these defects in four sequential stages:

```text
[Raw Unstructured Line]
        │
        ▼
[Stage 1: ASCII Sanitizer] ──────► Strips 0x00-0x1F control noise & trims whitespace
        │
        ▼
[Stage 2: Traceback Stitcher] ───► Detects continuation prefix ('at ', 'File "', '0x...')
        │                          Welds exception lines to parent error record with ' | '
        ▼
[Stage 3: Timestamp Imputer] ────► Regex search for ISO/Syslog date; if absent,
        │                          calculates timestamp from preceding chronological event
        ▼
[Stage 4: Semantic Classifier] ──► Token scan ('timeout', 'refused', 'fail', 'oom')
        │                          Restores level to ERROR / CRITICAL; attaches repair note
        ▼
[Clean Repaired LogRecord: ParseStatus::REPAIRED]
```

### 3.1 Stage 1: ASCII Sanitization
Removes unprintable binary artifacts, terminal escape sequences, and ASCII control characters (`< 32`) while preserving tabs, newlines, and valid UTF-8 sequences. Strips extraneous leading and trailing whitespace.

### 3.2 Stage 2: Multiline Traceback Stitching
Production crash traces (Python tracebacks, Java exceptions, Darwin backtraces) frequently split across 5 to 20 lines. The stitcher identifies continuation signatures:
- Leading whitespace (`    ` or `\t`)
- Common stack frame markers (`at `, `File "`, `Caused by:`, `Traceback`, `Exception in`, `0x`)
Instead of discarding these lines as invalid, the engine concatenates them directly to the previous error record:
$$\text{Record}_{k}.\text{message} \leftarrow \text{Record}_{k}.\text{message} + \text{" | "} + \text{Line}_{i}$$

### 3.3 Stage 3: Chronological Timestamp Imputation
If a log entry arrives without a valid timestamp delimiter, the engine attempts regular expression extraction. If no date pattern exists, it imputes the timestamp from the most recent valid chronologically preceding event:
$$t_{\text{repaired}} = t_{\text{last\_valid}}$$
If no preceding valid timestamp exists, it synthesizes the timestamp from the system baseline. A diagnostic annotation (`Timestamp imputed from chronological sequence`) is attached to the record.

### 3.4 Stage 4: Semantic Severity Restoration
Unstructured lines that lack standard `[LEVEL]` tags are analyzed using an NLP keyword classifier:
- Keywords indicating failure (`refused`, `timeout`, `fail`, `error`, `oom`, `segfault`) are restored as `Level::ERROR`.
- Keywords indicating system degradation (`warn`, `threshold`, `slow`, `degraded`) are restored as `Level::WARNING`.
- Critical crash tokens (`panic`, `fatal`, `emerg`) are restored as `Level::CRITICAL`.
- A repair note is appended to the record: `"Severity restored from semantic message tokens"`.

---

## 4. Constant-Memory Streaming Ring Buffer

During high-volume real-time kernel log streaming (`log stream`), hundreds of events arrive per second. Unbounded growth causes process memory expansion and eventual termination.

To guarantee zero memory leaks and stable footprint:
1. The engine passes accumulated records through `apply_sliding_ring_buffer(records, 5000)`.
2. When the working set exceeds 5,000 records, the oldest records are dropped using iterator slicing:
   ```cpp
   std::size_t offset = records.size() - max_capacity;
   std::vector<LogRecord> capped(
       std::make_move_iterator(records.begin() + offset),
       std::make_move_iterator(records.end())
   );
   capped.shrink_to_fit();
   ```
3. Process heap memory is released immediately, maintaining peak Resident Set Size (RSS) stably around **~5.0 MB** regardless of how long the stream runs.

---

## 5. Non-Blocking POSIX Live Kernel Stream Capture

Capturing real-time kernel streams from `/usr/bin/log stream` poses deadlock risks if the child process stalls or fills the kernel pipe buffer.

The ingestion engine handles streaming safely using POSIX system calls:
1. `pipe(pipefd)` creates an internal unidirectional pipe.
2. `fork()` spawns a dedicated worker process that executes `/usr/bin/log stream --style syslog` with standard output bound to `pipefd[1]` and standard error silenced via `/dev/null`.
3. The parent process monitors `pipefd[0]` using non-blocking `poll()` calls:
   ```cpp
   struct pollfd pfd;
   pfd.fd = pipefd[0];
   pfd.events = POLLIN;
   int ret = poll(&pfd, 1, remaining_ms > 100 ? 100 : remaining_ms);
   ```
4. Data is accumulated line by line. When the capture duration elapses, the parent terminates the worker process with `SIGKILL`, reaps the child with `waitpid()`, closes descriptors, and returns the captured events.

---

## 6. Integrated Terminal & Asynchronous Process Management

The desktop application features a fully integrated terminal embedded in the bottom dock drawer:

### 6.1 Top-Aligned Sequential Visual Flow
- On startup or following `clear` / `Ctrl+L`, the terminal prompt and cursor appear at **line 1 (top-left)** of the blank screen, exactly matching VS Code.
- When commands execute, outputs stream directly underneath, and subsequent prompts appear immediately beneath the output.
- Clicking anywhere on the empty black canvas automatically focuses the input.

### 6.2 Asynchronous Worker & Standard Input Isolation
- The backend HTTP daemon handles incoming requests on detached threads (`std::thread(handle_client, client_fd).detach()`).
- In `execute_shell_command()`, the spawned child process redirects `STDIN_FILENO` to `/dev/null` (`open("/dev/null", O_RDONLY)`). Interactive CLI programs that wait on standard input receive EOF immediately, preventing server worker thread freezes.
- A non-blocking `poll()` loop monitors child process output with a 30-second timeout guard.
- When the user presses `Ctrl+C`, the frontend issues a call to `/api/terminal/kill`, which immediately delivers `SIGINT` and `SIGKILL` to the child process group.

### 6.3 Dynamic Hostname & User Resolution
- The `/api/terminal/info` endpoint queries dynamic user identity via `USER`, `LOGNAME`, or `USERNAME` (e.g., `nakulamundhada23`).
- It extracts the clean local hostname via `gethostname()`, stripping any `.local` suffix (e.g., transforming `Nakulas-MacBook-Air.local` into `Nakulas-MacBook-Air`), matching standard macOS terminal behavior.

---

## 7. Deterministic Anomaly Detection Rules (R001–R005)

| Rule ID | Rule Name | Severity | Condition Threshold | Operational Purpose |
|:---|:---|:---|:---|:---|
| **R001** | Repeated Error Pattern | `ERROR` | Single error repeats $\ge 3$ times | Flags connection retry loops, broken socket handshakes, or recurring database lockouts. |
| **R002** | Critical System Event | `CRITICAL` | $\ge 1$ event with severity `CRITICAL` / `FAULT` | Flags kernel panics, OOM kills, or hardware interrupts. Enforces an immediate risk floor $\ge 60$. |
| **R003** | High Error Volume | `ERROR` | Total error count $\ge 5$ across analysis window | Flags elevated baseline failure rates across monitored services. |
| **R004** | High Error Ratio | `WARNING` | Error events represent $> 20\%$ of total traffic | Identifies environments where failures overwhelm normal application traffic. |
| **R005** | Time-Windowed Error Spike | `ERROR` | Any time bucket contains errors $> 2.0\times$ average baseline | Detects sudden cascading incidents, network partition drops, or bad deployments. |

---

## 8. Calibrated Risk Assessment Model

System risk is evaluated on a 0–100 scale using a deterministic weighted composite formula:

$$\text{Raw Score} = \min\left(100, \; \sum_{l} (w_l \times c_l) + 15 \times |A| + \text{Penalty}\right)$$

Where:
- $w_l$ represents severity weight (`CRITICAL = 5`, `ERROR = 3`, `WARNING = 1`, `INFO = 0`).
- $c_l$ is the total count of events at severity level $l$.
- $|A|$ is the count of active detection rules triggered.
- $\text{Penalty} = 20$ if error ratio $\frac{N_{\text{error}}}{N_{\text{total}}} > 0.20$, else $0$.
- **R002 Risk Floor Override**: If rule R002 triggers (indicating at least one critical event), the calculated score is overridden to enforce a minimum risk floor:
  $$\text{Final Score} = \max(60, \; \text{Raw Score})$$

### Risk Bands

| Score Range | Status Band | Operational State |
|:---|:---|:---|
| **0 – 19** | `HEALTHY` | Nominal system behavior; standard operational baseline. |
| **20 – 39** | `NORMAL` | Minor transient warnings; no active service disruption. |
| **40 – 59** | `WARNING` | Elevated error rates; routine investigation recommended. |
| **60 – 79** | `HIGH RISK` | Critical incidents or recurring failures active; immediate triage needed. |
| **80 – 100** | `CRITICAL` | Severe system incident, cascade failure, or service outage in progress. |

---

## 9. Automated Self-Diagnosis & Test Harness

The integrated test harness (`./bin/log_analyzer --test`) exercises 16 isolated verification and fault-injection cases:

1. **Level Normalization**: Verifies standard parsing of `CRIT`, `ERR`, `WARN`, `INFO`, `DEBUG`.
2. **Message Cleaning**: Validates regex removal of process IDs, thread names, and memory addresses.
3. **Timestamp Parsing**: Verifies parsing of ISO 8601 and syslog date formats.
4. **Subsystem Classification**: Confirms accurate category assignment across all 6 subsystems.
5. **Standard Log Parsing**: Tests exact field extraction from valid syslog records.
6. **Fault-Injection (Unparseable Lines)**: Validates graceful handling of corrupted text.
7. **Noise Filter Denylist**: Verifies automatic suppression of hardware driver telemetry.
8. **Syslog Fallback Parsing**: Tests extraction of unstructured log lines with valid dates.
9. **Single-Pass Aggregation**: Confirms accuracy of linear accumulation metrics.
10. **Fault-Injection (Zero-Event Guard)**: Negative test ensuring division-by-zero safety on empty streams.
11. **Fault-Injection (Single-Event Duration)**: Validates timeline boundaries when start equals end.
12. **Rule R001 Activation**: Verifies trigger on 3 repeated error messages.
13. **Rule R002 & Risk Floor**: Confirms critical events force minimum risk score of 60.
14. **Rule R005 Spike Detection**: Validates mathematical identification of localized error bursts.
15. **Self-Healing Auto-Repair**: Validates traceback stitching, timestamp imputation, and semantic level recovery.
16. **Constant-Memory Ring Buffer**: Verifies 6,000 streaming events cap safely to 5,000 without memory leaks.

All 16 tests execute in **0.016 to 0.023 seconds** on standard hardware.

---

## Authors & License

- **Authors**: Built by **Nakul Mundhada** &bull; Co-authored by **Prasad Akle**
- **License**: MIT License &copy; 2026 Nakul Mundhada & Prasad Akle.
