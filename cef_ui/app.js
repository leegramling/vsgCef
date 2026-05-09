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
];

const initialStats = Object.fromEntries(statFields.map(([key]) => [key, 0]));

const initialTypes = [
  { enabled: true, label: "A", color: "#e6332e", spawn: 25, bin: "North", speed: 1.0, live: 0 },
  { enabled: true, label: "B", color: "#3373f2", spawn: 25, bin: "East", speed: 0.9, live: 0 },
  { enabled: true, label: "C", color: "#40c752", spawn: 25, bin: "South", speed: 1.1, live: 0 },
  { enabled: true, label: "D", color: "#f2c733", spawn: 25, bin: "West", speed: 1.0, live: 0 },
];

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
  const [settings, setSettings] = React.useState({
    rate: "1.5",
    maxObjects: 100,
    conveyorSpeed: 3,
    sortingStrength: 0.65,
    friction: 0.35,
    randomness: 0.25,
    cubeMix: 45,
  });
  const [types, setTypes] = React.useState(initialTypes);

  React.useEffect(() => {
    window.vsgCef = {
      receiveFrameData(frame) {
        setTypes((current) => current.map((type, index) => {
          if (index === 0) return { ...type, live: frame.cubeCount ?? type.live };
          if (index === 1) return { ...type, live: frame.sphereCount ?? type.live };
          return type;
        }));
      },
    };
    return () => {
      delete window.vsgCef;
    };
  }, []);

  function updateSetting(id, value) {
    setSettings((current) => ({ ...current, [id]: value }));
    sendToCpp("mockSettingChanged", { id, value });
  }

  function updateType(index, patch, eventType) {
    setTypes((current) => current.map((type, row) => row === index ? { ...type, ...patch } : type));
    sendToCpp(eventType, { index, ...patch });
  }

  return h("main", { className: "single-panel" },
    h("section", { className: "panel form-panel", "aria-label": "Sorting form mockup" },
      h("header", null,
        h("h1", null, "Sorting Form Mockup"),
        h("span", { className: "status" }, "React local state")
      ),
      h("div", { className: "form-grid" },
        h(TextField, { label: "Rate", value: settings.rate, onChange: (value) => updateSetting("rate", value) }),
        h(NumberField, { label: "Max objects", min: 0, max: 1000, value: settings.maxObjects, onChange: (value) => updateSetting("maxObjects", value) }),
        h(RangeField, { label: "Conveyor speed", min: 0, max: 10, step: 0.1, value: settings.conveyorSpeed, onChange: (value) => updateSetting("conveyorSpeed", value) }),
        h(RangeField, { label: "Sorting strength", min: 0, max: 1, step: 0.01, value: settings.sortingStrength, onChange: (value) => updateSetting("sortingStrength", value) }),
        h(RangeField, { label: "Friction", min: 0, max: 1, step: 0.01, value: settings.friction, onChange: (value) => updateSetting("friction", value) }),
        h(RangeField, { label: "Randomness", min: 0, max: 1, step: 0.01, value: settings.randomness, onChange: (value) => updateSetting("randomness", value) }),
        h(RangeField, { label: "Cube mix", min: 0, max: 100, step: 1, value: settings.cubeMix, suffix: "%", onChange: (value) => updateSetting("cubeMix", value) })
      ),
      h("table", null,
        h("thead", null,
          h("tr", null,
            ["On", "Type", "Color", "Spawn %", "Bin", "Speed", "Live"].map((heading) => h("th", { key: heading }, heading))
          )
        ),
        h("tbody", null,
          types.map((type, index) =>
            h("tr", { key: type.label },
              h("td", null,
                h("input", {
                  className: "type-enabled",
                  type: "checkbox",
                  checked: type.enabled,
                  onChange: (event) => updateType(index, { enabled: event.target.checked }, "mockTypeEnabledChanged"),
                })
              ),
              h("td", null, type.label),
              h("td", null, h("div", { className: "swatch", style: { background: type.color } })),
              h("td", null,
                h("input", {
                  type: "range",
                  min: "0",
                  max: "100",
                  step: "1",
                  value: type.spawn,
                  onChange: (event) => updateType(index, { spawn: Number(event.target.value) }, "mockTypeSpawnChanged"),
                })
              ),
              h("td", null, type.bin),
              h("td", null,
                h("input", {
                  type: "range",
                  min: "0.25",
                  max: "2",
                  step: "0.01",
                  value: type.speed,
                  onChange: (event) => updateType(index, { speed: Number(event.target.value) }, "mockTypeSpeedChanged"),
                })
              ),
              h("td", null, type.live)
            )
          )
        )
      )
    )
  );
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
