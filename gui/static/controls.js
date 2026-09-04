document.addEventListener("DOMContentLoaded", () => {
  const tracePlot = document.getElementById("tracePlot");

  var layout = {
    margin: { l: 60, r: 0, t: 0, b: 50 },
    plot_bgcolor: "#000000ff",
    paper_bgcolor: "#ada8b6ff",
    autosize: true,
    font: { color: "#000000" },
    modebar: {
      bgcolor: "rgba(47, 6, 1, 0.6)",
      color: "#dad8df",
      activecolor: "#41ff00",
    },
    xaxis: {
      title: { text: "Time (\u00b5s)", font: { size: 18 } },
      tickfont: { size: 18 },
      showgrid: true,
      gridcolor: "#444444",
      zeroline: true,
      zerolinewidth: 3,
      zerolinecolor: "#666666",
    },
    yaxis: {
      title: { text: "Amplitude (counts)", font: { size: 18 } },
      tickfont: { size: 18 },
      showgrid: true,
      gridcolor: "#444444",
      zeroline: true,
      zerolinewidth: 3,
      zerolinecolor: "#666666",
    },
  };

  var data = [
    {
      x: [],
      y: [],
      line: { color: "#41ff00ff" },
    },
  ];

  var config = {
    responsive: true,
    displaylogo: false,
    displayModeBar: true,
    modeBarButtonsToRemove: ["select2d", "lasso2d", "sendChartToCloud"],
  };

  Plotly.newPlot(tracePlot, data, layout, config);

  let traceX = [];
  let traceXdt = null;
  let plottedTraceCount = null;

  // sampling_freq is in MHz, so 1/f is the sample spacing in microseconds
  function samplePeriod() {
    const cfg = window.chirpConfigs[document.querySelector(".xmit").value];
    return cfg && cfg.sampling_freq ? 1 / cfg.sampling_freq : 1;
  }

  function timeAxis(n, dt) {
    if (traceX.length === n && traceXdt === dt) return traceX;
    traceX = Array.from({ length: n }, (_, i) => i * dt);
    traceXdt = dt;
    return traceX;
  }

  function updateTracePlot(trace, rxTraces) {
    if (!trace || trace.length === 0) {
      if (plottedTraceCount === null) return;
      plottedTraceCount = null;
      Plotly.restyle(tracePlot, { x: [[]], y: [[]] }, [0]);
      return;
    }

    // only redraw once the radar has delivered a new trace
    if (rxTraces != null && rxTraces === plottedTraceCount) return;
    plottedTraceCount = rxTraces;

    Plotly.restyle(
      tracePlot,
      { x: [timeAxis(trace.length, samplePeriod())], y: [trace] },
      [0],
    );
  }

  function displayChirpConfig(key) {
    const config = window.chirpConfigs[key];
    if (!config) return;

    document.getElementById("prf").value = config.prf;
    document.getElementById("stacking").value = config.stacking;
    document.getElementById("sampling").value = config.sampling_freq;

    const centerFreqBoxes = document.querySelectorAll(".centerFrequency");
    const bandwidthBoxes = document.querySelectorAll(".Bandwidth");
    const chirpLengthBoxes = document.querySelectorAll(".chirpLength");
    const traceLengthBoxes = document.querySelectorAll(".traceLength");
    const powerBoxes = document.querySelectorAll(".power");

    [
      centerFreqBoxes,
      bandwidthBoxes,
      chirpLengthBoxes,
      traceLengthBoxes,
      powerBoxes,
    ].forEach((boxes) => boxes.forEach((box) => (box.value = "")));

    config.chirp.forEach((chirp, i) => {
      centerFreqBoxes[i].value = chirp.center_frequency;
      bandwidthBoxes[i].value = chirp.bandwidth;
      chirpLengthBoxes[i].value = chirp.chirp_length;
      traceLengthBoxes[i].value = chirp.trace_length;
      powerBoxes[i].value = chirp.power;
    });
  }

  document.querySelector(".xmit").addEventListener("change", (e) => {
    displayChirpConfig(e.target.value);
  });

  // Show the initially-selected config on page load
  displayChirpConfig(document.querySelector(".xmit").value);

  const MAX_CONSOLE_LINES = 500;

  function logToConsole(message) {
    const consoleEl = document.getElementById("console");
    const timestamp = new Date().toLocaleTimeString("en-US", { hour12: false });
    message
      .split("\n")
      .filter((line) => line.trim() !== "")
      .forEach((line) => {
        consoleEl.value += `[${timestamp}] ${line}\n`;
      });
    const lines = consoleEl.value.split("\n");
    if (lines.length > MAX_CONSOLE_LINES) {
      consoleEl.value = lines.slice(-MAX_CONSOLE_LINES).join("\n");
    }
    consoleEl.scrollTop = consoleEl.scrollHeight;
  }

  document.querySelector(".start").addEventListener("click", () => {
    const xmitValue = document.querySelector(".xmit").value;

    fetch("/start", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ xmit: xmitValue }),
    })
      .then((res) => res.json())
      .then((data) => {
        console.log(data.message);
      })
      .catch((err) => console.error("Start request failed:", err));
  });

  document.querySelector(".stop").addEventListener("click", () => {
    fetch("/stop", { method: "POST" })
      .then((res) => res.json())
      .then((data) => {
        console.log(data.message);
      })
      .catch((err) => console.error("Stop request failed:", err));
  });

  document.querySelector(".queryGPS").addEventListener("click", () => {
    fetch("/queryGPS", { method: "POST" })
      .then((res) => res.json())
      .then((data) => {
        console.log(data.message);
      })
      .catch((err) => console.error("Query GPS request failed:", err));
  });

  document.querySelector(".queryRadio").addEventListener("click", () => {
    fetch("/queryRadio", { method: "POST" })
      .then((res) => res.json())
      .then((data) => {
        console.log(data.message);
      })
      .catch((err) => console.error("Query radio request failed:", err));
  });

  function fmt(value, decimals) {
    const n = Number(value);
    return value == null || !Number.isFinite(n) ? "" : n.toFixed(decimals);
  }

  // strip directories and extension for display
  function baseName(path) {
    if (!path) return "";
    const name = path.split("/").pop();
    const dot = name.lastIndexOf(".");
    return dot > 0 ? name.slice(0, dot) : name;
  }

  function pollState() {
    fetch("/state")
      .then((res) => res.json())
      .then((data) => {
        // Update text boxes
        document.getElementById("gpsStatus").value = data.gpsStatus;
        document.getElementById("time").value = data.gpsTime;
        document.getElementById("latitude").value = fmt(data.gpsLat, 6);
        document.getElementById("longitude").value = fmt(data.gpsLon, 6);
        document.getElementById("height").value = fmt(data.gpsHgt, 2);
        document.getElementById("satellites").value = data.gpsSats;
        document.getElementById("radioStatus").value =
          data.radioStatus === "connected"
            ? `${data.radioStatus} - ${data.acquiring ? "acquiring" : "idle"}`
            : data.radioStatus;
        document.getElementById("fileName").value = baseName(data.fileName);
        document.getElementById("rxTraces").value = data.rxTraces;
        document.getElementById("refClk").value = !data.refClk
          ? ""
          : data.refLock
            ? `${data.refClk} - locked`
            : `${data.refClk} - not locked`;
        document.getElementById("timeSrc").value = data.timeSrc;

        const RADIO_FIELDS = ["fileName", "rxTraces"];

        const radioColor = !data.radioStatus
          ? "" // if null
          : data.radioStatus !== "connected"
            ? "var(--electric-red)"
            : data.acquiring
              ? "var(--medium-jungle)"
              : "var(--golden-pollen)";

        document.getElementById("radioStatus").style.backgroundColor =
          radioColor;

        // fields below the status box mirror it, but only when populated
        RADIO_FIELDS.forEach((id) => {
          const el = document.getElementById(id);
          el.style.backgroundColor = el.value ? radioColor : "";
        });

        function getClockColor(value) {
          if (value === "gpsdo") {
            return "var(--medium-jungle)"; // green
          } else if (value === "internal") {
            return "var(--golden-pollen)"; // yellow
          } else if (!value) {
            // if null
            return "";
          } else {
            return "var(--electric-red)"; // red — populated but not a known value
          }
        }

        // with no radio there is nothing to report, so flag both clocks
        const noRadio = data.radioStatus === "no radio";

        document.getElementById("refClk").style.backgroundColor = noRadio
          ? "var(--electric-red)"
          : !data.refClk
            ? ""
            : data.refLock
              ? getClockColor(data.refClk)
              : "var(--electric-red)";

        document.getElementById("timeSrc").style.backgroundColor = noRadio
          ? "var(--electric-red)"
          : getClockColor(data.timeSrc);

        const GPS_YELLOW_STATES = [
          "warmup",
          "holdover",
          "locking",
          "holdover-locked",
        ];

        const GPS_FIELDS = [
          "time",
          "latitude",
          "longitude",
          "height",
          "satellites",
        ];

        const gpsColor = !data.gpsStatus
          ? ""
          : data.gpsStatus === "locked"
            ? "var(--medium-jungle)"
            : GPS_YELLOW_STATES.includes(data.gpsStatus)
              ? "var(--golden-pollen)"
              : "var(--electric-red)";

        document.getElementById("gpsStatus").style.backgroundColor = gpsColor;

        // fields below the status box mirror it
        GPS_FIELDS.forEach((id) => {
          document.getElementById(id).style.backgroundColor = gpsColor;
        });

        const SEVERITY_RANK = {
          "var(--electric-red)": 3,
          "var(--golden-pollen)": 2,
          "var(--medium-jungle)": 1,
          "": 0,
        };

        function getMostSevereColor(colors) {
          return colors.reduce(
            (mostSevere, color) =>
              SEVERITY_RANK[color] > SEVERITY_RANK[mostSevere]
                ? color
                : mostSevere,
            "",
          );
        }

        document.getElementById("radioHeader").style.backgroundColor =
          getMostSevereColor([
            document.getElementById("radioStatus").style.backgroundColor,
            document.getElementById("refClk").style.backgroundColor,
            document.getElementById("timeSrc").style.backgroundColor,
          ]);

        document.getElementById("gpsHeader").style.backgroundColor =
          getMostSevereColor([
            document.getElementById("gpsStatus").style.backgroundColor,
          ]);

        updateTracePlot(data.lastTrace, data.rxTraces);

        if (data.message) {
          console.log(data.message);
          logToConsole(data.message);
        }
      })
      .catch((err) => console.error("Status poll failed:", err))
      .finally(() => {
        setTimeout(pollState, 500);
      });
  }

  pollState();
});
