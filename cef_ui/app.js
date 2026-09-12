const h = React.createElement;

const statFields = [
  ["renderFps", "Render FPS", 1],
  ["simulationFps", "Simulation FPS", 1],
  ["simulationFrame", "Frame", 0],
  ["totalObjects", "Objects", 0],
  ["cubeCount", "Cubes", 0],
  ["sphereCount", "Spheres", 0],
  ["createdThisFrame", "Created", 0],
  ["updatedThisFrame", "Updated", 0],
  ["removedThisFrame", "Removed", 0],
  ["collisionCount", "Collisions", 0],
  ["pendingAppEvents", "App Events", 0],
  ["workerBacklog", "Backlog", 0],
  ["packedCount", "Packed", 0],
  ["missedPickups", "Missed", 0],
  ["robotBattery", "Battery", 1],
  ["sensorHealth", "Sensors", 1],
  ["commsHealth", "Comms", 1],
  ["cefStatsCpu", "CEF Stats CPU", 1],
  ["cefStatsMemoryMb", "CEF Stats MB", 1],
  ["cefStatsGpuMemoryMb", "CEF Stats GPU MB", 1],
  ["cefSortingCpu", "CEF Sort CPU", 1],
  ["cefSortingMemoryMb", "CEF Sort MB", 1],
  ["cefSortingGpuMemoryMb", "CEF Sort GPU MB", 1],
];

const initialStats = Object.fromEntries(statFields.map(([key]) => [key, 0]));

const colorNames = ["Red", "Blue", "Green"];
const modeNames = ["Idle", "Seeking", "Picking", "Delivering", "Charging", "Faulted"];

function sendToCpp(type, payload = {}, setBridgeStatus) {
  const message = JSON.stringify({ type, payload });
  if (typeof window.cefQuery === "function") {
    window.cefQuery({
      request: message,
      onSuccess: () => setBridgeStatus && setBridgeStatus("CEF bridge connected"),
      onFailure: (_code, text) => setBridgeStatus && setBridgeStatus(text || "CEF bridge failure"),
    });
    return;
  }

  console.debug("CEF bridge unavailable", message);
  if (setBridgeStatus) setBridgeStatus("CEF bridge standby");
}

function formatStat(value, decimals) {
  const number = Number(value || 0);
  if (number < 0) return "n/a";
  return number.toFixed(decimals);
}

function StatsPanel() {
  const [stats, setStats] = React.useState(initialStats);
  const [bridgeStatus, setBridgeStatus] = React.useState("CEF bridge standby");
  const [paused, setPaused] = React.useState(false);
  const [spawnRate, setSpawnRate] = React.useState(1.5);

  React.useEffect(() => {
    window.vsgCef = {
      receiveFrameData(frame) {
        setStats((current) => ({ ...current, ...frame }));
      },
    };
    return () => {
      delete window.vsgCef;
    };
  }, []);

  return h("main", { className: "stats-strip" },
    h("section", { className: "stats-panel stats-panel-attached", "aria-label": "Simulation stats" },
      h("header", null,
        h("h1", null, "vsgCef Stats"),
        h("span", { className: "status" }, bridgeStatus)
      ),
      h("dl", { className: "stats-grid" },
        statFields.map(([key, label, decimals]) =>
          h("div", { key },
            h("dt", null, label),
            h("dd", null, formatStat(stats[key], decimals))
          )
        )
      ),
      h("div", { className: "controls" },
        h("label", { className: "check-row" },
          h("input", {
            type: "checkbox",
            checked: paused,
            onChange(event) {
              const value = event.target.checked;
              setPaused(value);
              sendToCpp("setPaused", { paused: value }, setBridgeStatus);
            },
          }),
          h("span", null, "Pause simulation")
        ),
        h("label", null,
          h("span", null, "Spawn rate"),
          h("input", {
            type: "range",
            min: "0",
            max: "20",
            step: "0.1",
            value: spawnRate,
            onChange(event) {
              const value = Number(event.target.value);
              setSpawnRate(value);
              sendToCpp("setSpawnRate", { objectsPerSecond: value }, setBridgeStatus);
            },
          }),
          h("output", null, `${spawnRate.toFixed(1)}/s`)
        ),
        h("div", { className: "button-row" },
          h("button", { type: "button", onClick: () => sendToCpp("spawnBurst", { count: 8 }, setBridgeStatus) }, "Spawn burst"),
          h("button", { type: "button", onClick: () => sendToCpp("clearObjects", {}, setBridgeStatus) }, "Clear")
        )
      )
    )
  );
}

function SortingPanel() {
  const [frame, setFrame] = React.useState({
    activePanel: "robot",
    robotBattery: 100,
    robotSpeedLimit: 1,
    robotAutoMode: true,
    robotMode: 0,
    sensorHealth: 100,
    commsHealth: 100,
    sensorNoise: 0,
    commsDropout: 0,
    jamRate: 0,
  });
  const [bridgeStatus, setBridgeStatus] = React.useState("CEF bridge standby");

  React.useEffect(() => {
    window.vsgCef = {
      receiveFrameData(frame) {
        setFrame((current) => ({ ...current, ...frame }));
      },
    };
    return () => {
      delete window.vsgCef;
    };
  }, []);

  function updateValue(command, stateKey, value) {
    setFrame((current) => ({ ...current, [stateKey]: value }));
    sendToCpp(command, { value }, setBridgeStatus);
  }

  return h("main", { className: "single-panel" },
    h("section", { className: "panel form-panel", "aria-label": "Robot packer controls" },
      h("header", null,
        h("h1", null, activePanelTitle(frame.activePanel)),
        h("span", { className: "status" }, bridgeStatus)
      ),
      frame.activePanel === "orders"
        ? h(OrdersPanel, { frame, setBridgeStatus })
        : frame.activePanel === "diagnostics"
          ? h(DiagnosticsPanel, { frame, updateValue, setBridgeStatus })
          : h(RobotPanel, { frame, updateValue, setBridgeStatus })
    )
  );
}

function activePanelTitle(panel) {
  if (panel === "orders") return "Orders";
  if (panel === "diagnostics") return "System Diagnostics";
  return "Robot";
}

function RobotPanel({ frame, updateValue, setBridgeStatus }) {
  return h("div", { className: "control-stack" },
    h("dl", { className: "stats-grid" },
      h(StatCell, { label: "Mode", value: modeNames[frame.robotMode] || "Idle" }),
      h(StatCell, { label: "Battery", value: `${formatStat(frame.robotBattery, 1)}%` }),
      h(StatCell, { label: "Carrying", value: frame.robotCarrying ? "Yes" : "No" }),
      h(StatCell, { label: "Fault", value: frame.robotFaulted ? "Faulted" : "Clear" })
    ),
    h("label", { className: "check-row" },
      h("input", {
        type: "checkbox",
        checked: !!frame.robotAutoMode,
        onChange: (event) => sendToCpp("setRobotAuto", { enabled: event.target.checked }, setBridgeStatus),
      }),
      h("span", null, "Auto mode")
    ),
    h(RangeField, {
      label: "Speed limit",
      min: 0.25,
      max: 2.5,
      step: 0.05,
      value: frame.robotSpeedLimit ?? 1,
      suffix: "x",
      onChange: (value) => updateValue("setRobotSpeed", "robotSpeedLimit", value),
    }),
    h("div", { className: "button-row" },
      h("button", { type: "button", onClick: () => sendToCpp("sendRobotCharge", {}, setBridgeStatus) }, "Charge"),
      h("button", { type: "button", onClick: () => sendToCpp("resetRobotFault", {}, setBridgeStatus) }, "Reset fault")
    )
  );
}

function OrdersPanel({ frame, setBridgeStatus }) {
  return h("div", { className: "control-stack" },
    h("div", { className: "order-pair" },
      h(OrderCard, {
        title: "Current",
        id: frame.currentOrderId,
        color: frame.currentOrderColor,
        required: frame.currentOrderRequired,
        packed: frame.currentOrderPacked,
      }),
      h(OrderCard, {
        title: "Next",
        id: frame.nextOrderId,
        color: frame.nextOrderColor,
        required: frame.nextOrderRequired,
        packed: 0,
      })
    ),
    h("dl", { className: "stats-grid" },
      h(StatCell, { label: "Packed", value: formatStat(frame.packedCount, 0) }),
      h(StatCell, { label: "Backlog", value: formatStat(frame.orderBacklog, 0) })
    ),
    h("div", { className: "button-row" },
      h("button", { type: "button", onClick: () => sendToCpp("addRushOrder", {}, setBridgeStatus) }, "Add rush order"),
      h("button", { type: "button", onClick: () => sendToCpp("spawnBurst", { count: 8 }, setBridgeStatus) }, "Feed balls")
    )
  );
}

function DiagnosticsPanel({ frame, updateValue }) {
  return h("div", { className: "control-stack" },
    h("dl", { className: "stats-grid" },
      h(StatCell, { label: "Sensors", value: `${formatStat(frame.sensorHealth, 1)}%` }),
      h(StatCell, { label: "Comms", value: `${formatStat(frame.commsHealth, 1)}%` }),
      h(StatCell, { label: "Missed", value: formatStat(frame.missedPickups, 0) }),
      h(StatCell, { label: "Balls", value: formatStat(frame.sphereCount, 0) })
    ),
    h(RangeField, { label: "Sensor noise", min: 0, max: 1, step: 0.01, value: frame.sensorNoise ?? 0, onChange: (value) => updateValue("setSensorNoise", "sensorNoise", value) }),
    h(RangeField, { label: "Comms dropout", min: 0, max: 1, step: 0.01, value: frame.commsDropout ?? 0, onChange: (value) => updateValue("setCommsDropout", "commsDropout", value) }),
    h(RangeField, { label: "Jam rate", min: 0, max: 1, step: 0.01, value: frame.jamRate ?? 0, onChange: (value) => updateValue("setJamRate", "jamRate", value) })
  );
}

function OrderCard({ title, id, color, required, packed }) {
  const pct = required > 0 ? Math.min(100, (Number(packed || 0) / Number(required)) * 100) : 0;
  return h("article", { className: "order-card" },
    h("header", null,
      h("h2", null, title),
      h("span", { className: "status" }, `#${id || "-"}`)
    ),
    h("div", { className: "order-color" },
      h("span", { className: `dot dot-${color || 0}` }),
      h("strong", null, colorNames[color || 0])
    ),
    h("div", { className: "meter" }, h("span", { style: { width: `${pct}%` } })),
    h("p", null, `${packed || 0} / ${required || 0}`)
  );
}

function StatCell({ label, value }) {
  return h("div", null, h("dt", null, label), h("dd", null, value));
}

function TextField({ label, value, onChange }) {
  return h("label", null,
    h("span", null, label),
    h("input", { type: "text", value, onChange: (event) => onChange(event.target.value) })
  );
}

function NumberField({ label, min, max, value, onChange }) {
  return h("label", null,
    h("span", null, label),
    h("input", { type: "number", min, max, value, onChange: (event) => onChange(Number(event.target.value)) })
  );
}

function RangeField({ label, min, max, step, value, suffix = "", onChange }) {
  const number = Number(value);
  return h("label", null,
    h("span", null, label),
    h("input", { type: "range", min, max, step, value, onChange: (event) => onChange(Number(event.target.value)) }),
    h("output", null, `${Number.isInteger(number) ? number : number.toFixed(2)}${suffix}`)
  );
}

const root = document.getElementById("root");
const panel = root ? root.dataset.panel : "stats";
ReactDOM.createRoot(root).render(panel === "sorting" ? h(SortingPanel) : h(StatsPanel));
