const NOISE_DENYLIST = [
  "AppleBCMWLAN",
  "clocksyncd",
  "SCAN_INFO",
  "Clock Statistics",
  "CoreAnalytics",
  "IOPlatformPluginFamily: thermal"
];

const PRELOADED_SAMPLES = {
  demo: [
    "2026-09-05 10:00:01 [INFO] System kernel initialization completed on host-alpha",
    "2026-09-05 10:00:05 [INFO] User alice authenticated successfully via ssh publickey",
    "2026-09-05 10:01:12 [WARNING] Storage space warning: volume /var at 87% utilization",
    "2026-09-05 10:02:15 [ERROR] Connection refused to database primary on port 5432",
    "2026-09-05 10:02:18 [ERROR] Connection refused to database primary on port 5432",
    "2026-09-05 10:02:22 [ERROR] Connection refused to database primary on port 5432",
    "2026-09-05 10:03:40 [WARNING] Resource threshold exceeded: process worker-3 memory at 92%",
    "2026-09-05 10:04:10 [ERROR] Authentication failure: invalid credentials for user admin",
    "2026-09-05 10:04:55 [ERROR] File read error: configuration path /etc/app/config.json not found",
    "2026-09-05 10:05:01 [CRITICAL] Kernel out of memory: terminated process 4102 (worker)",
    "2026-09-05 10:06:14 [INFO] Network interface en0 reconnected to default gateway",
    "2026-09-05 10:07:00 [ERROR] Network timeout after 10000ms while polling payment gateway",
    "CORRUPT_RECORD_MISSING_HEADER_AND_TIMESTAMP",
    "2026-09-05 10:08:30 INCOMPLETE_RECORD_NO_LEVEL_DELIMITERS"
  ],
  auth: [
    "2026-09-05 15:00:00 [INFO] sshd[1020]: Server listening on 0.0.0.0 port 22",
    "2026-09-05 15:00:12 [WARNING] sshd[1025]: Connection from 192.168.1.105 port 49201",
    "2026-09-05 15:00:14 [ERROR] sshd[1025]: Authentication failure: invalid credentials for user root",
    "2026-09-05 15:00:16 [ERROR] sshd[1026]: Authentication failure: invalid credentials for user root",
    "2026-09-05 15:00:18 [ERROR] sshd[1027]: Authentication failure: invalid credentials for user root",
    "2026-09-05 15:00:22 [ERROR] sshd[1028]: Authentication failure: invalid credentials for user root",
    "2026-09-05 15:00:25 [ERROR] sshd[1029]: Authentication failure: invalid credentials for user admin",
    "2026-09-05 15:00:35 [INFO] sshd[1030]: Disconnecting invalid user root 192.168.1.105 port 49201",
    "2026-09-05 15:00:40 [INFO] firewall[500]: Rule block applied to IP 192.168.1.105 after 5 attempts"
  ],
  spike: [
    "2026-09-05 12:00:00 [INFO] gateway[8080]: Healthcheck probe OK 200",
    "2026-09-05 12:05:00 [INFO] gateway[8080]: Healthcheck probe OK 200",
    "2026-09-05 12:10:00 [INFO] gateway[8080]: Healthcheck probe OK 200",
    "2026-09-05 12:15:01 [ERROR] gateway[8080]: Network timeout after 10000ms while polling payment gateway",
    "2026-09-05 12:15:03 [ERROR] gateway[8080]: Network timeout after 10000ms while polling payment gateway",
    "2026-09-05 12:15:05 [ERROR] gateway[8080]: Network timeout after 10000ms while polling payment gateway",
    "2026-09-05 12:15:08 [ERROR] gateway[8080]: Network timeout after 10000ms while polling payment gateway",
    "2026-09-05 12:15:10 [ERROR] gateway[8080]: Network timeout after 10000ms while polling payment gateway",
    "2026-09-05 12:20:00 [INFO] gateway[8080]: Recovering connection to secondary payment provider",
    "2026-09-05 12:25:00 [INFO] gateway[8080]: Primary route restored, queue processed"
  ],
  system: [
    "2026-09-05 14:00:01 [INFO] System kernel initialization completed on host-alpha",
    "2026-09-05 14:00:05 [INFO] User alice authenticated successfully via ssh publickey",
    "2026-09-05 14:01:12 [WARNING] Storage space warning: volume /var at 87% utilization",
    "2026-09-05 14:02:15 [ERROR] Connection refused to database primary on port 5432",
    "2026-09-05 14:02:18 [ERROR] Connection refused to database primary on port 5432",
    "2026-09-05 14:02:22 [ERROR] Connection refused to database primary on port 5432",
    "2026-09-05 14:03:40 [WARNING] Resource threshold exceeded: process worker-3 memory at 92%",
    "2026-09-05 14:04:10 [ERROR] Authentication failure: invalid credentials for user admin",
    "2026-09-05 14:04:55 [ERROR] File read error: configuration path /etc/app/config.json not found",
    "2026-09-05 14:05:01 [CRITICAL] Kernel out of memory: terminated process 4102 (worker)",
    "2026-09-05 14:06:14 [INFO] Network interface en0 reconnected to default gateway",
    "2026-09-05 14:07:00 [ERROR] Network timeout after 10000ms while polling payment gateway"
  ]
};

let currentAnalysis = null;
let currentRecords = [];

document.addEventListener("DOMContentLoaded", () => {
  detectClientPlatform();
  setupEventListeners();
  loadSampleDataset("demo");
});

function detectClientPlatform() {
  const el = document.getElementById("platformName");
  const ua = navigator.userAgent;
  let os = "macOS Darwin";
  if (ua.indexOf("Win") !== -1) os = "Windows NT";
  else if (ua.indexOf("Linux") !== -1) os = "Linux (POSIX)";
  else if (ua.indexOf("Mac") !== -1) os = "macOS Darwin";
  if (el) el.textContent = os;
}

function setupEventListeners() {
  const fileInput = document.getElementById("fileInput");
  const btnUpload = document.getElementById("btnUpload");
  const btnExport = document.getElementById("btnExportJson");
  const searchInput = document.getElementById("logSearchInput");
  const levelSelect = document.getElementById("logLevelSelect");

  btnUpload.addEventListener("click", () => fileInput.click());
  fileInput.addEventListener("change", handleFileSelect);
  btnExport.addEventListener("click", handleExportJson);
  searchInput.addEventListener("input", filterLogTable);
  levelSelect.addEventListener("change", filterLogTable);

  document.querySelectorAll(".sample-chip").forEach(chip => {
    chip.addEventListener("click", () => {
      const sample = chip.getAttribute("data-sample");
      if (sample === "clear") clearDashboard();
      else loadSampleDataset(sample);
    });
  });

  const dragOverlay = document.getElementById("dragOverlay");
  window.addEventListener("dragenter", (e) => {
    e.preventDefault();
    dragOverlay.classList.add("active");
  });
  dragOverlay.addEventListener("dragleave", (e) => {
    e.preventDefault();
    if (e.relatedTarget === null) dragOverlay.classList.remove("active");
  });
  dragOverlay.addEventListener("dragover", (e) => e.preventDefault());
  dragOverlay.addEventListener("drop", (e) => {
    e.preventDefault();
    dragOverlay.classList.remove("active");
    if (e.dataTransfer.files.length > 0) processFile(e.dataTransfer.files[0]);
  });
}

function loadSampleDataset(key) {
  const lines = PRELOADED_SAMPLES[key];
  if (!lines) return;
  analyzeLines(lines, key);
}

function clearDashboard() {
  currentAnalysis = null;
  currentRecords = [];
  document.getElementById("valTotalEvents").textContent = "0";
  document.getElementById("valRawLines").textContent = "Raw Lines: 0";
  document.getElementById("valParsed").textContent = "0";
  document.getElementById("valParsedPct").textContent = "0.0% match";
  document.getElementById("valFallback").textContent = "0";
  document.getElementById("valFallbackPct").textContent = "0.0% tolerant";
  document.getElementById("valNoise").textContent = "0";
  document.getElementById("valThroughput").textContent = "0";
  document.getElementById("valTime").textContent = "0.000s processing";
  updateRiskGauge(0, "HEALTHY", "No log events loaded.");
  document.getElementById("severityBarsContainer").innerHTML = "";
  document.getElementById("categoryGridContainer").innerHTML = "";
  document.getElementById("rulesStackContainer").innerHTML = "";
  document.getElementById("densityChartContainer").innerHTML = "";
  document.getElementById("topErrorsContainer").innerHTML = "";
  document.getElementById("recommendationsList").innerHTML = '<li class="rec-item text-muted">Load or paste log records to generate targeted remediation actions.</li>';
  document.getElementById("logTableBody").innerHTML = '<tr><td colspan="5" class="empty-state">No logs loaded. Click "Upload Log" or select a quick dataset above.</td></tr>';
}

function handleFileSelect(e) {
  if (e.target.files.length > 0) processFile(e.target.files[0]);
}

function processFile(file) {
  const reader = new FileReader();
  if (file.name.endsWith(".json")) {
    reader.onload = (e) => {
      try {
        const json = JSON.parse(e.target.result);
        renderFromExportedJson(json);
      } catch (err) {
        alert("Invalid JSON report format: " + err.message);
      }
    };
    reader.readAsText(file);
  } else {
    reader.onload = (e) => {
      const text = e.target.result;
      const lines = text.split(/\r?\n/).filter(l => l.trim().length > 0);
      analyzeLines(lines, file.name);
    };
    reader.readAsText(file);
  }
}

function isNoiseLine(line) {
  for (const n of NOISE_DENYLIST) {
    if (line.includes(n)) return true;
  }
  return false;
}

function normalizeLevel(text) {
  const upper = text.toUpperCase();
  if (upper.includes("CRIT") || upper.includes("FATAL") || upper.includes("FAULT") || upper.includes("EMERGENCY")) return "CRITICAL";
  if (upper.includes("ERR") || upper.includes("FAIL")) return "ERROR";
  if (upper.includes("WARN")) return "WARNING";
  if (upper.includes("INFO") || upper.includes("NOTICE") || upper.includes("DEFAULT")) return "INFO";
  return "INFO";
}

function classifyCategory(text) {
  const low = text.toLowerCase();
  if (low.includes("auth") || low.includes("security") || low.includes("ssh") || low.includes("root") || low.includes("token") || low.includes("password") || low.includes("firewall")) return "SECURITY";
  if (low.includes("network") || low.includes("connection") || low.includes("socket") || low.includes("timeout") || low.includes("gateway") || low.includes("port") || low.includes("dns")) return "NETWORK";
  if (low.includes("file") || low.includes("path") || low.includes("directory") || low.includes("read error") || low.includes("io error") || low.includes("disk")) return "FILE";
  if (low.includes("memory") || low.includes("storage") || low.includes("volume") || low.includes("oom") || low.includes("cpu") || low.includes("quota")) return "RESOURCE";
  if (low.includes("process") || low.includes("daemon") || low.includes("service") || low.includes("terminated") || low.includes("killed") || low.includes("worker")) return "PROCESS";
  return "SYSTEM";
}

function parseLogLine(raw, index) {
  const line = raw.trim();
  if (!line || isNoiseLine(line)) return null;

  const simpleRegex = /^(\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}(?:\.\d+)?)\s+(?:\[([A-Za-z]+)\]|([A-Za-z]+))\s+(.*)$/;
  const m1 = line.match(simpleRegex);
  if (m1) {
    const rawLvl = m1[2] || m1[3] || "INFO";
    const msg = m1[4].trim();
    return {
      seq: `EVT-${String(index).padStart(5, '0')}`,
      timestampStr: m1[1],
      timestamp: parseDate(m1[1]),
      level: normalizeLevel(rawLvl),
      category: classifyCategory(msg),
      message: msg,
      raw: raw,
      status: "PARSED"
    };
  }

  const syslogRegex = /^(\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:[+-]\d{4})?)\s+([^\s]+)\s+([^:]+):\s*(.*)$/;
  const m2 = line.match(syslogRegex);
  if (m2) {
    const sender = m2[3].trim();
    const msg = m2[4].trim();
    return {
      seq: `SYS-${String(index).padStart(5, '0')}`,
      timestampStr: m2[1],
      timestamp: parseDate(m2[1]),
      level: normalizeLevel(msg),
      category: classifyCategory(msg),
      message: `[${sender}] ${msg}`,
      raw: raw,
      status: "PARSED"
    };
  }

  const fallbackRegex = /^(\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}(?:\.\d+)?)\s+(.*)$/;
  const m3 = line.match(fallbackRegex);
  if (m3) {
    const msg = m3[2].trim();
    return {
      seq: `FLB-${String(index).padStart(5, '0')}`,
      timestampStr: m3[1],
      timestamp: parseDate(m3[1]),
      level: normalizeLevel(msg),
      category: classifyCategory(msg),
      message: msg,
      raw: raw,
      status: "FALLBACK"
    };
  }

  return {
    seq: `RAW-${String(index).padStart(5, '0')}`,
    timestampStr: "N/A",
    timestamp: null,
    level: "UNKNOWN",
    category: classifyCategory(line),
    message: line,
    raw: raw,
    status: "INVALID"
  };
}

function parseDate(str) {
  let s = str.replace(/[+].*$/, '').replace(/\..*$/, '').replace('T', ' ');
  const d = new Date(s);
  return isNaN(d.getTime()) ? null : d;
}

function analyzeLines(rawLines, sourceName) {
  const startTime = performance.now();
  let noiseCount = 0;
  let parsedCount = 0;
  let fallbackCount = 0;
  let invalidCount = 0;
  const records = [];

  for (let i = 0; i < rawLines.length; i++) {
    const line = rawLines[i].trim();
    if (!line) continue;
    if (isNoiseLine(line)) {
      noiseCount++;
      continue;
    }
    const rec = parseLogLine(line, i + 1);
    if (!rec) {
      noiseCount++;
      continue;
    }
    records.push(rec);
    if (rec.status === "PARSED") parsedCount++;
    else if (rec.status === "FALLBACK") fallbackCount++;
    else invalidCount++;
  }

  const elapsed = (performance.now() - startTime) / 1000.0;
  const total = records.length;
  const throughput = elapsed > 0 ? Math.round(total / elapsed) : total * 1000;

  const severityCounts = { INFO: 0, WARNING: 0, ERROR: 0, CRITICAL: 0, UNKNOWN: 0 };
  const categoryCounts = { FILE: 0, NETWORK: 0, SECURITY: 0, RESOURCE: 0, PROCESS: 0, SYSTEM: 0 };
  const errorFreq = {};

  let earliestTs = null;
  let latestTs = null;

  for (const r of records) {
    if (severityCounts[r.level] !== undefined) severityCounts[r.level]++;
    if (categoryCounts[r.category] !== undefined) categoryCounts[r.category]++;
    if (r.level === "ERROR" || r.level === "CRITICAL") {
      errorFreq[r.message] = (errorFreq[r.message] || 0) + 1;
    }
    if (r.timestamp) {
      if (!earliestTs || r.timestamp < earliestTs) earliestTs = r.timestamp;
      if (!latestTs || r.timestamp > latestTs) latestTs = r.timestamp;
    }
  }

  const topErrors = Object.entries(errorFreq)
    .sort((a, b) => b[1] - a[1])
    .slice(0, 5)
    .map(([msg, cnt]) => ({ message: msg, count: cnt }));

  const timeBuckets = computeBuckets(records, earliestTs, latestTs, 4);
  const detections = evaluateDetectionRules(records, severityCounts, topErrors, timeBuckets, total);
  const risk = evaluateDynamicRiskScore(total, severityCounts, topErrors, timeBuckets);
  const recommendations = generateRecommendations(detections);

  currentAnalysis = {
    source: sourceName,
    summary: {
      totalEvents: total,
      rawLines: rawLines.length,
      fullyParsed: parsedCount,
      fallback: fallbackCount,
      invalid: invalidCount,
      noiseFiltered: noiseCount,
      throughput: throughput,
      elapsed: elapsed,
      timeline: earliestTs && latestTs ? `${formatShortTime(earliestTs)} - ${formatShortTime(latestTs)}` : "Real-time"
    },
    severityDistribution: severityCounts,
    categorySummary: categoryCounts,
    topErrors: topErrors,
    timeBuckets: timeBuckets,
    detections: detections,
    risk: risk,
    recommendations: recommendations
  };

  currentRecords = records;
  renderDashboard(currentAnalysis, records);
}

function computeBuckets(records, start, end, count) {
  if (!start || !end || start.getTime() === end.getTime() || count <= 0) {
    let errs = 0;
    for (const r of records) if (r.level === "ERROR" || r.level === "CRITICAL") errs++;
    return [{ label: "All Events", total: records.length, errors: errs }];
  }

  const durationMs = end.getTime() - start.getTime();
  const stepMs = durationMs / count;
  const buckets = [];
  for (let i = 0; i < count; i++) {
    const bStart = new Date(start.getTime() + i * stepMs);
    const bEnd = new Date(start.getTime() + (i + 1) * stepMs);
    buckets.push({
      label: `${formatShortTime(bStart)} - ${formatShortTime(bEnd)}`,
      total: 0,
      errors: 0
    });
  }

  for (const r of records) {
    if (!r.timestamp) continue;
    let offset = r.timestamp.getTime() - start.getTime();
    let idx = Math.floor(offset / stepMs);
    if (idx < 0) idx = 0;
    if (idx >= count) idx = count - 1;
    buckets[idx].total++;
    if (r.level === "ERROR" || r.level === "CRITICAL") buckets[idx].errors++;
  }

  return buckets;
}

function evaluateDetectionRules(records, severities, topErrors, buckets, total) {
  const results = [];
  const errCnt = severities.ERROR + severities.CRITICAL;

  for (const t of topErrors) {
    if (t.count >= 3) {
      results.push({
        id: "R001",
        name: "Repeated Error Pattern",
        severity: "ERROR",
        evidence: `Error '${t.message.slice(0, 65)}...' occurred ${t.count} times (threshold: >= 3)`
      });
      break;
    }
  }

  if (severities.CRITICAL > 0) {
    results.push({
      id: "R002",
      name: "Critical System Event",
      severity: "CRITICAL",
      evidence: `Found ${severities.CRITICAL} CRITICAL/FAULT level event(s) requiring immediate review`
    });
  }

  if (errCnt >= 5) {
    results.push({
      id: "R003",
      name: "High Error Volume",
      severity: "ERROR",
      evidence: `Total error events (${errCnt}) reached threshold of >= 5`
    });
  }

  if (total > 0) {
    const ratio = errCnt / total;
    if (ratio > 0.20) {
      results.push({
        id: "R004",
        name: "High Error Ratio",
        severity: "WARNING",
        evidence: `Error ratio ${(ratio * 100).toFixed(1)}% (${errCnt}/${total}) exceeds 20% baseline`
      });
    }
  }

  if (buckets.length > 0) {
    const totalBucketErr = buckets.reduce((acc, b) => acc + b.errors, 0);
    const avg = totalBucketErr / buckets.length;
    for (const b of buckets) {
      if (b.errors >= 2 && b.errors > avg * 1.8) {
        results.push({
          id: "R005",
          name: "Time-Windowed Error Spike",
          severity: "ERROR",
          evidence: `Window [${b.label}] recorded ${b.errors} errors (exceeds 2.0x baseline average of ${avg.toFixed(1)})`
        });
        break;
      }
    }
  }

  return results;
}

function evaluateDynamicRiskScore(total, severities, topErrors, buckets) {
  if (total === 0) return { score: 0, band: "HEALTHY", summary: "No events analyzed." };

  const errCnt = severities.ERROR;
  const critCnt = severities.CRITICAL;
  const warnCnt = severities.WARNING;

  const errorRate = errCnt / total;
  const critRate = critCnt / total;
  const warnRate = warnCnt / total;

  const densityScore = Math.min(40.0, (errorRate * 55.0) + (critRate * 75.0) + (warnRate * 10.0));
  const weightedSum = (warnCnt * 1.0) + (errCnt * 3.0) + (critCnt * 5.0);
  const maxWeight = total * 5.0;
  const severityScore = (weightedSum / maxWeight) * 35.0;

  let anomalyScore = 0.0;
  for (const t of topErrors) {
    if (t.count >= 3) {
      const freqRatio = t.count / total;
      anomalyScore += Math.min(5.0, 2.0 + freqRatio * 15.0);
    }
  }
  anomalyScore = Math.min(15.0, anomalyScore);

  if (buckets.length > 0) {
    let maxBucketErr = 0;
    let totalBucketErr = 0;
    for (const b of buckets) {
      if (b.errors > maxBucketErr) maxBucketErr = b.errors;
      totalBucketErr += b.errors;
    }
    const avg = totalBucketErr / buckets.length;
    if (avg > 0 && maxBucketErr > avg * 1.8) {
      const factor = maxBucketErr / avg;
      anomalyScore += Math.min(10.0, factor * 2.5);
    }
  }

  let critBonus = 0.0;
  if (critCnt > 0) {
    critBonus = Math.min(15.0, 3.0 + (critRate * 40.0));
    if (critRate >= 0.50) critBonus = Math.max(critBonus, 25.0);
  }

  let raw = Math.round(densityScore + severityScore + anomalyScore + critBonus);
  let score = Math.max(0, Math.min(100, raw));

  if (critCnt > 0 && total === critCnt) {
    score = Math.max(score, 60);
  }

  let band = "HEALTHY";
  let summary = "System operational within nominal parameters.";
  if (score >= 80) {
    band = "CRITICAL";
    summary = "Active severe incident cascade or elevated critical failure density.";
  } else if (score >= 60) {
    band = "HIGH RISK";
    summary = "High failure rate or recurring error loops detected requiring prompt attention.";
  } else if (score >= 40) {
    band = "WARNING";
    summary = "Elevated error density observed beyond normal operational baseline.";
  } else if (score >= 20) {
    band = "NORMAL";
    summary = "Minor operational warnings detected, system functioning normally.";
  }

  return { score: score, band: band, summary: summary };
}

function generateRecommendations(detections) {
  const recs = [];
  for (const d of detections) {
    if (d.id === "R001") {
      recs.push("Review daemon retry policies, socket timeouts, and connection pool exhaustion for recurring failure loops.");
    } else if (d.id === "R002") {
      recs.push("Immediately inspect core crash dumps, kernel ring-buffers, and service exit codes for critical faults.");
    } else if (d.id === "R003") {
      recs.push("Investigate elevated subsystem failure volume across recently deployed components or microservices.");
    } else if (d.id === "R004") {
      recs.push("Audit error ratio baseline against historical service metrics to isolate noisy dependencies.");
    } else if (d.id === "R005") {
      recs.push("Correlate the localized error spike window with cron executions, external network events, or deployment triggers.");
    }
  }
  if (recs.length === 0) {
    recs.push("All operational indicators healthy. Continue routine telemetry monitoring.");
  }
  return recs;
}

function renderDashboard(data, records) {
  document.getElementById("valTotalEvents").textContent = data.summary.totalEvents.toLocaleString();
  document.getElementById("valRawLines").textContent = `Raw Lines: ${data.summary.rawLines.toLocaleString()}`;
  document.getElementById("valParsed").textContent = data.summary.fullyParsed.toLocaleString();
  const parsedPct = data.summary.totalEvents > 0 ? ((data.summary.fullyParsed / data.summary.totalEvents) * 100).toFixed(1) : "0.0";
  document.getElementById("valParsedPct").textContent = `${parsedPct}% match`;
  document.getElementById("valFallback").textContent = data.summary.fallback.toLocaleString();
  const fallbackPct = data.summary.totalEvents > 0 ? ((data.summary.fallback / data.summary.totalEvents) * 100).toFixed(1) : "0.0";
  document.getElementById("valFallbackPct").textContent = `${fallbackPct}% fallback`;
  document.getElementById("valNoise").textContent = data.summary.noiseFiltered.toLocaleString();
  document.getElementById("valThroughput").textContent = data.summary.throughput.toLocaleString() + " EPS";
  document.getElementById("valTime").textContent = `${data.summary.elapsed.toFixed(3)}s processing`;
  document.getElementById("valTimeline").textContent = data.summary.timeline;

  updateRiskGauge(data.risk.score, data.risk.band, data.risk.summary);
  renderSeverityBars(data.severityDistribution, data.summary.totalEvents);
  renderCategoryGrid(data.categorySummary);
  renderRulesStack(data.detections);
  renderDensityChart(data.timeBuckets);
  renderTopErrors(data.topErrors);
  renderRecommendations(data.recommendations);
  renderLogTable(records);
}

function updateRiskGauge(score, band, summary) {
  const scoreEl = document.getElementById("riskScore");
  const badgeEl = document.getElementById("riskBadge");
  const fillEl = document.getElementById("gaugeFill");
  const explEl = document.getElementById("riskExplanation");

  scoreEl.textContent = score;
  badgeEl.textContent = band;
  badgeEl.className = `risk-badge ${band.toLowerCase().replace(" ", "-")}`;
  explEl.textContent = summary;

  const maxOffset = 251.2;
  const offset = maxOffset - (maxOffset * (score / 100.0));
  fillEl.style.strokeDashoffset = offset;

  if (score >= 80) fillEl.style.stroke = "var(--color-critical)";
  else if (score >= 60) fillEl.style.stroke = "var(--color-error)";
  else if (score >= 40) fillEl.style.stroke = "var(--color-warning)";
  else if (score >= 20) fillEl.style.stroke = "var(--color-info)";
  else fillEl.style.stroke = "var(--color-success)";
}

function renderSeverityBars(dist, total) {
  const container = document.getElementById("severityBarsContainer");
  container.innerHTML = "";
  const levels = ["CRITICAL", "ERROR", "WARNING", "INFO"];

  levels.forEach(lvl => {
    const count = dist[lvl] || 0;
    const pct = total > 0 ? ((count / total) * 100).toFixed(1) : "0.0";
    const row = document.createElement("div");
    row.className = "sev-row";
    row.innerHTML = `
      <div class="sev-meta">
        <span>${lvl}</span>
        <span>${count.toLocaleString()} (${pct}%)</span>
      </div>
      <div class="sev-track">
        <div class="sev-bar ${lvl.toLowerCase()}" style="width: ${pct}%"></div>
      </div>
    `;
    container.appendChild(row);
  });
}

function renderCategoryGrid(cats) {
  const container = document.getElementById("categoryGridContainer");
  container.innerHTML = "";
  Object.entries(cats).forEach(([name, count]) => {
    const card = document.createElement("div");
    card.className = "cat-card";
    card.innerHTML = `
      <span class="cat-name">${name}</span>
      <span class="cat-count">${count.toLocaleString()}</span>
    `;
    container.appendChild(card);
  });
}

function renderRulesStack(detections) {
  const container = document.getElementById("rulesStackContainer");
  const countBadge = document.getElementById("valRulesTriggered");
  container.innerHTML = "";
  countBadge.textContent = `${detections.length} Triggered`;

  if (detections.length === 0) {
    container.innerHTML = '<div class="text-muted" style="font-size:0.8rem; padding: 10px 0;">No anomaly rule thresholds triggered. Nominal state.</div>';
    return;
  }

  detections.forEach(d => {
    const item = document.createElement("div");
    item.className = "rule-item triggered";
    item.innerHTML = `
      <span class="rule-badge ${d.severity.toLowerCase()}">${d.id}</span>
      <div class="rule-content">
        <div class="rule-name">${d.name}</div>
        <div class="rule-evidence">${d.evidence}</div>
      </div>
    `;
    container.appendChild(item);
  });
}

function renderDensityChart(buckets) {
  const container = document.getElementById("densityChartContainer");
  container.innerHTML = "";
  if (!buckets || buckets.length === 0) {
    container.innerHTML = '<div class="text-muted" style="font-size:0.8rem;">No timestamp distribution available.</div>';
    return;
  }

  const maxTotal = Math.max(...buckets.map(b => b.total), 1);
  const avgErr = buckets.reduce((acc, b) => acc + b.errors, 0) / buckets.length;

  buckets.forEach(b => {
    const heightPct = Math.max(8, Math.round((b.total / maxTotal) * 100));
    const isSpike = b.errors >= 2 && b.errors > avgErr * 1.8;
    const bucketEl = document.createElement("div");
    bucketEl.className = "density-bucket";
    bucketEl.innerHTML = `
      <div class="density-bar-wrap" title="${b.label} | Total: ${b.total}, Errors: ${b.errors}">
        <div class="density-bar ${isSpike ? 'spike' : ''}" style="height: ${heightPct}%"></div>
      </div>
      <span class="density-label">${b.label.split(' - ')[0] || b.label}</span>
    `;
    container.appendChild(bucketEl);
  });
}

function renderTopErrors(errors) {
  const container = document.getElementById("topErrorsContainer");
  container.innerHTML = "";
  if (errors.length === 0) {
    container.innerHTML = '<div class="text-muted" style="font-size:0.8rem; padding: 6px 0;">Zero error events recorded.</div>';
    return;
  }

  errors.forEach(e => {
    const item = document.createElement("div");
    item.className = "top-error-item";
    item.innerHTML = `
      <span class="top-error-msg" title="${e.message}">${e.message}</span>
      <span class="top-error-count">${e.count}x</span>
    `;
    container.appendChild(item);
  });
}

function renderRecommendations(recs) {
  const list = document.getElementById("recommendationsList");
  list.innerHTML = "";
  recs.forEach(r => {
    const li = document.createElement("li");
    li.className = "rec-item";
    li.textContent = r;
    list.appendChild(li);
  });
}

function renderLogTable(records) {
  const tbody = document.getElementById("logTableBody");
  tbody.innerHTML = "";
  if (records.length === 0) {
    tbody.innerHTML = '<tr><td colspan="5" class="empty-state">No events matched current filter.</td></tr>';
    return;
  }

  const slice = records.slice(0, 250);
  slice.forEach(r => {
    const tr = document.createElement("tr");
    tr.innerHTML = `
      <td style="color:var(--text-dim);">${r.seq}</td>
      <td style="color:var(--text-muted);">${r.timestampStr}</td>
      <td><span class="tag-level ${r.level.toLowerCase()}">${r.level}</span></td>
      <td><span class="tag-cat">${r.category}</span></td>
      <td style="color:var(--text-main);">${escapeHtml(r.message)}</td>
    `;
    tbody.appendChild(tr);
  });
}

function filterLogTable() {
  if (!currentRecords) return;
  const q = document.getElementById("logSearchInput").value.toLowerCase();
  const lvl = document.getElementById("logLevelSelect").value;

  const filtered = currentRecords.filter(r => {
    if (lvl === "CRITICAL" && r.level !== "CRITICAL") return false;
    if (lvl === "ERROR" && r.level !== "ERROR" && r.level !== "CRITICAL") return false;
    if (lvl === "WARNING" && r.level !== "WARNING") return false;
    if (lvl === "INFO" && r.level !== "INFO") return false;
    if (q && !r.message.toLowerCase().includes(q) && !r.raw.toLowerCase().includes(q)) return false;
    return true;
  });

  renderLogTable(filtered);
}

function handleExportJson() {
  if (!currentAnalysis) {
    alert("No active analysis to export.");
    return;
  }
  const str = JSON.stringify(currentAnalysis, null, 2);
  const blob = new Blob([str], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = `log_analysis_${Date.now()}.json`;
  a.click();
  URL.revokeObjectURL(url);
}

function renderFromExportedJson(json) {
  const summary = json.summary || {};
  const severities = json.severity_distribution || { INFO: 0, WARNING: 0, ERROR: 0, CRITICAL: 0, UNKNOWN: 0 };
  const categories = json.category_summary || { FILE: 0, NETWORK: 0, SECURITY: 0, RESOURCE: 0, PROCESS: 0, SYSTEM: 0 };
  const topErrors = json.top_errors || [];
  const risk = json.risk_assessment || { score: 0, band: "HEALTHY", summary: "Loaded from exported JSON report." };
  const detections = json.detections || [];
  const recs = json.recommendations || [];

  const analysis = {
    source: "Exported Report",
    summary: {
      totalEvents: summary.total_events || 0,
      rawLines: summary.total_events || 0,
      fullyParsed: summary.fully_parsed || 0,
      fallback: summary.partial_fallback || 0,
      invalid: summary.invalid || 0,
      noiseFiltered: 0,
      throughput: json.performance ? Math.round(json.performance.throughput_eps) : 100000,
      elapsed: json.performance ? json.performance.elapsed_seconds : 0.001,
      timeline: summary.timeline ? `${summary.timeline.start || ''} - ${summary.timeline.end || ''}` : "Imported"
    },
    severityDistribution: severities,
    categorySummary: categories,
    topErrors: topErrors,
    timeBuckets: [],
    detections: detections.map(d => ({ id: d.rule_id, name: d.name, severity: d.severity, evidence: d.evidence })),
    risk: { score: risk.score || 0, band: risk.band || "HEALTHY", summary: risk.disclaimer || "Imported analysis." },
    recommendations: recs
  };

  currentAnalysis = analysis;
  renderDashboard(analysis, []);
}

function formatShortTime(d) {
  if (!d) return "";
  const h = String(d.getHours()).padStart(2, '0');
  const m = String(d.getMinutes()).padStart(2, '0');
  const s = String(d.getSeconds()).padStart(2, '0');
  return `${h}:${m}:${s}`;
}

function escapeHtml(str) {
  return str.replace(/[&<>'"]/g, tag => ({
    '&': '&amp;',
    '<': '&lt;',
    '>': '&gt;',
    "'": '&#39;',
    '"': '&quot;'
  }[tag] || tag));
}
