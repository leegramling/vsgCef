<script>
  import { onMount } from "svelte";
  import { app } from "./bridge.js";

  const tabs = ["Identity", "Vehicle", "Payload", "Power", "Mission", "Review"];
  let activeTab = "Identity";
  let saved = false;
  let attachments = [
    { type: "Camera", name: "Low-light stereo camera", mount: "Forward", power: 12, depth: 300 },
    { type: "Lighting", name: "Lumen light pair", mount: "Front brackets", power: 24, depth: 500 },
    { type: "Sonar", name: "Imaging sonar", mount: "Top rail", power: 8, depth: 300 }
  ];
  let form = {
    name: "Monterey Bay Inspection Unit 01",
    assetId: "MBI-ROV-001",
    vehicleClass: "ROV",
    manufacturer: "Blue Robotics",
    model: "BlueROV2 R4",
    operator: "IOOS Coastal Robotics Lab",
    description: "Inspection-class vehicle for benthic habitat and infrastructure surveys.",
    depth: 100,
    length: 457,
    width: 338,
    height: 254,
    weight: 11.5,
    payload: 1.2,
    batteryVoltage: 14.8,
    batteryAh: 18,
    batteryCount: 1,
    tethered: true,
    mission: "Harbor structure inspection",
    targetDepth: 35,
    samplingRate: 10,
    launchSite: "Monterey Harbor, CA",
    notes: "Demo configuration — submit creates one IOOS scene object."
  };

  $: energyWh = Number(form.batteryVoltage || 0) * Number(form.batteryAh || 0) * Number(form.batteryCount || 0);
  $: payloadMass = attachments.reduce((sum, item) => sum + Number(item.mass || 0), 0);
  $: warnings = [
    Number(form.targetDepth) > Number(form.depth) ? "Mission depth exceeds vehicle rating." : "",
    payloadMass > Number(form.payload) ? "Payload mass exceeds the configured capacity." : "",
    Number(form.batteryVoltage) > 20 ? "Battery voltage exceeds the T200 recommended range." : ""
  ].filter(Boolean);

  onMount(() => {
    app.ready();
  });

  function addAttachment() {
    attachments = [...attachments, { type: "Sensor", name: "New payload", mount: "Select mount", power: 0, depth: 100, mass: 0 }];
  }

  function removeAttachment(index) {
    attachments = attachments.filter((_, i) => i !== index);
  }

  function nextTab() {
    const index = tabs.indexOf(activeTab);
    activeTab = tabs[Math.min(index + 1, tabs.length - 1)];
  }

  function previousTab() {
    const index = tabs.indexOf(activeTab);
    activeTab = tabs[Math.max(index - 1, 0)];
  }

  function saveMockup() {
    app.action("object.createIoos", {
      name: form.name,
      assetId: form.assetId,
      manufacturer: form.manufacturer,
      model: form.model,
      vehicleClass: form.vehicleClass,
      operator: form.operator,
      depthRating: Number(form.depth),
      payloadCapacity: Number(form.payload),
      batteryEnergyWh: energyWh,
      attachmentCount: attachments.length
    });
    saved = true;
  }

  function cancel() {
    app.action("ui.closeRobotConfigurator");
  }
</script>

<main>
  <header>
    <div>
      <div class="eyebrow">OCEAN ASSET REGISTRY</div>
      <h1>Add Ocean Robot</h1>
      <p>Configure a complete inspection vehicle profile.</p>
    </div>
    <span class="framework-badge">Svelte mockup</span>
  </header>

  <nav class="tabs" aria-label="Robot configuration steps">
    {#each tabs as tab}
      <button class:active={activeTab === tab} type="button" on:click={() => activeTab = tab}>{tab}</button>
    {/each}
  </nav>

  {#if activeTab === "Identity"}
    <section class="panel-section">
      <div class="section-heading"><div><h2>Identity</h2><p>How this vehicle appears in the fleet.</p></div><span class="step">01 / 06</span></div>
      <div class="form-grid two">
        <label>Robot name<input bind:value={form.name} /></label>
        <label>Asset ID<input bind:value={form.assetId} /></label>
        <label>Vehicle class<select bind:value={form.vehicleClass}><option>ROV</option><option>AUV</option><option>Glider</option><option>USV</option></select></label>
        <label>Status<select><option>Draft</option><option>Active</option><option>Maintenance</option></select></label>
        <label>Manufacturer<input bind:value={form.manufacturer} /></label>
        <label>Model<input bind:value={form.model} /></label>
        <label>Operator<input bind:value={form.operator} /></label>
        <label>Data steward<input value="IOOS Coastal Data Team" /></label>
      </div>
      <label>Description<textarea bind:value={form.description} rows="3"></textarea></label>
    </section>
  {:else if activeTab === "Vehicle"}
    <section class="panel-section">
      <div class="section-heading"><div><h2>Vehicle configuration</h2><p>Physical envelope, performance, and operating limits.</p></div><span class="step">02 / 06</span></div>
      <div class="metric-grid"><div><strong>6</strong><span>thrusters</span></div><div><strong>5</strong><span>degrees of freedom</span></div><div><strong>1.5 m/s</strong><span>max forward speed</span></div></div>
      <div class="form-grid three">
        <label>Length <span class="unit">mm</span><input type="number" bind:value={form.length} /></label>
        <label>Width <span class="unit">mm</span><input type="number" bind:value={form.width} /></label>
        <label>Height <span class="unit">mm</span><input type="number" bind:value={form.height} /></label>
        <label>Weight in air <span class="unit">kg</span><input type="number" step="0.1" bind:value={form.weight} /></label>
        <label>Payload capacity <span class="unit">kg</span><input type="number" step="0.1" bind:value={form.payload} /></label>
        <label>Depth rating <span class="unit">m</span><input type="number" bind:value={form.depth} /></label>
      </div>
      <div class="toggle-row"><div><strong>Tethered vehicle</strong><p>Surface power and continuous control link available.</p></div><input type="checkbox" bind:checked={form.tethered} /></div>
    </section>
  {:else if activeTab === "Payload"}
    <section class="panel-section">
      <div class="section-heading"><div><h2>Attachments & payload</h2><p>Configure the instruments mounted to the vehicle.</p></div><button class="secondary" type="button" on:click={addAttachment}>＋ Add payload</button></div>
      <div class="attachment-table" role="table">
        <div class="table-row table-head" role="row"><span>Type</span><span>Payload</span><span>Mount</span><span>Power</span><span></span></div>
        {#each attachments as item, index}
          <div class="table-row" role="row">
            <select bind:value={item.type}><option>Camera</option><option>Lighting</option><option>Sonar</option><option>CTD</option><option>Sensor</option><option>Manipulator</option></select>
            <input bind:value={item.name} aria-label="Payload name" />
            <input bind:value={item.mount} aria-label="Mount position" />
            <label class="inline-input"><input type="number" bind:value={item.power} /> W</label>
            <button class="icon-button" type="button" aria-label="Remove payload" on:click={() => removeAttachment(index)}>×</button>
          </div>
        {/each}
      </div>
      <div class="info-strip"><span>Payload mass</span><strong>{payloadMass.toFixed(1)} kg</strong><span>Capacity</span><strong>{Number(form.payload).toFixed(1)} kg</strong></div>
    </section>
  {:else if activeTab === "Power"}
    <section class="panel-section">
      <div class="section-heading"><div><h2>Power system</h2><p>Battery configuration and estimated energy budget.</p></div><span class="step">04 / 06</span></div>
      <div class="form-grid three">
        <label>Battery count<input type="number" min="1" bind:value={form.batteryCount} /></label>
        <label>Nominal voltage <span class="unit">V</span><input type="number" step="0.1" bind:value={form.batteryVoltage} /></label>
        <label>Capacity per pack <span class="unit">Ah</span><input type="number" step="0.1" bind:value={form.batteryAh} /></label>
      </div>
      <div class="energy-card"><span>Estimated energy</span><strong>{energyWh.toFixed(0)} Wh</strong><div class="battery-bar"><span style={`width: ${Math.min(100, energyWh / 3)}%`}></span></div><small>Approx. 2 hours normal inspection use</small></div>
      <div class="form-grid two"><label>Connector<select><option>XT90</option><option>XT60</option><option>Custom</option></select></label><label>Battery chemistry<select><option>Li-ion</option><option>LiPo</option><option>Surface power</option></select></label></div>
    </section>
  {:else if activeTab === "Mission"}
    <section class="panel-section">
      <div class="section-heading"><div><h2>Mission profile</h2><p>Define the first inspection plan and data stream.</p></div><span class="step">05 / 06</span></div>
      <div class="form-grid two"><label>Mission name<input bind:value={form.mission} /></label><label>Launch site<input bind:value={form.launchSite} /></label><label>Target depth <span class="unit">m</span><input type="number" bind:value={form.targetDepth} /></label><label>Sampling rate <span class="unit">Hz</span><input type="number" bind:value={form.samplingRate} /></label></div>
      <div class="map-placeholder"><div class="map-grid"></div><span>Mission area preview</span><i></i></div>
      <label>Mission notes<textarea bind:value={form.notes} rows="3"></textarea></label>
    </section>
  {:else}
    <section class="panel-section review">
      <div class="section-heading"><div><h2>Review configuration</h2><p>Preview only — saving will not create a scene object.</p></div><span class="step">06 / 06</span></div>
      <div class="review-card"><div class="robot-icon">ROV</div><div><h3>{form.name}</h3><p>{form.manufacturer} {form.model} · {form.assetId}</p><span class="badge">{form.vehicleClass}</span> <span class="badge">{attachments.length} payloads</span></div></div>
      <div class="summary-grid"><div><span>Depth rating</span><strong>{form.depth} m</strong></div><div><span>Target depth</span><strong>{form.targetDepth} m</strong></div><div><span>Battery</span><strong>{energyWh.toFixed(0)} Wh</strong></div><div><span>Payload</span><strong>{payloadMass.toFixed(1)} / {form.payload} kg</strong></div></div>
      {#if warnings.length}<div class="warnings"><strong>Review warnings</strong>{#each warnings as warning}<p>⚠ {warning}</p>{/each}</div>{:else}<div class="success">✓ Configuration is ready for review</div>{/if}
      <button class="primary" type="button" on:click={saveMockup}>{saved ? "IOOS robot added" : "Add IOOS robot"}</button>
    </section>
  {/if}

  <footer><button class="secondary" type="button" on:click={cancel}>Cancel</button><button class="secondary" type="button" on:click={previousTab} disabled={activeTab === tabs[0]}>Back</button><span>IOOS properties become a scene object on submit</span><button class="primary" type="button" on:click={nextTab} disabled={activeTab === tabs[tabs.length - 1]}>Continue</button></footer>
</main>

<style>
  :global(*) { box-sizing: border-box; }
  :global(body) { margin: 0; min-width: 620px; background: #101719; color: #e8f0ef; font: 13px/1.35 system-ui, sans-serif; }
  main { min-height: 100vh; padding: 16px; }
  header { display: flex; justify-content: space-between; gap: 12px; margin-bottom: 12px; padding: 12px; border: 1px solid #28545b; border-left: 4px solid #45c0b4; border-radius: 6px; background: linear-gradient(120deg, #173238, #20262b); }
  .eyebrow { color: #62c9bf; font-size: 10px; letter-spacing: .12em; }
  h1, h2, h3, p { margin: 0; } h1 { margin-top: 3px; font-size: 21px; } h2 { font-size: 16px; } h3 { font-size: 15px; } header p, .section-heading p { color: #94aeb2; }
  .framework-badge, .badge, .step { align-self: flex-start; padding: 4px 8px; border: 1px solid #337d80; border-radius: 999px; color: #8fe0d4; font-size: 10px; white-space: nowrap; }
  .tabs { display: grid; grid-template-columns: repeat(6, 1fr); gap: 3px; margin-bottom: 12px; padding: 3px; border: 1px solid #26363a; border-radius: 6px; background: #182124; }
  .tabs button, button { border: 0; border-radius: 4px; padding: 8px 9px; color: #a9b9bb; background: transparent; font: inherit; cursor: pointer; } .tabs button.active { color: #eafffb; background: #27625f; } button:disabled { cursor: default; opacity: .4; }
  .panel-section { min-height: 570px; padding: 14px; border: 1px solid #2a3c40; border-radius: 6px; background: #172023; }
  .section-heading { display: flex; justify-content: space-between; gap: 12px; margin-bottom: 16px; } .section-heading > div { display: grid; gap: 3px; }
  .form-grid { display: grid; gap: 11px; margin-bottom: 14px; } .form-grid.two { grid-template-columns: repeat(2, minmax(0, 1fr)); } .form-grid.three { grid-template-columns: repeat(3, minmax(0, 1fr)); }
  label { display: grid; gap: 5px; color: #a9bfc0; font-size: 11px; } .unit { float: right; color: #637e81; font-size: 10px; }
  input, select, textarea { width: 100%; border: 1px solid #385056; border-radius: 4px; padding: 8px; color: #e4f1ef; background: #202c30; font: inherit; } textarea { resize: vertical; } input:focus, select:focus, textarea:focus { outline: 2px solid #45c0b4; outline-offset: 1px; }
  .metric-grid, .summary-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 7px; margin-bottom: 15px; } .metric-grid div, .summary-grid div { padding: 10px; border: 1px solid #2c484d; border-radius: 4px; background: #1c2a2d; } .metric-grid strong, .summary-grid strong { display: block; color: #8fe0d4; font-size: 16px; } .metric-grid span, .summary-grid span { color: #789497; font-size: 10px; }
  .toggle-row, .info-strip, .energy-card, .success, .warnings { margin-top: 12px; padding: 11px; border: 1px solid #2c484d; border-radius: 4px; background: #1c2a2d; } .toggle-row { display: flex; justify-content: space-between; } .toggle-row p { color: #789497; font-size: 11px; } .toggle-row input { width: 38px; accent-color: #45c0b4; }
  .secondary { border: 1px solid #38565a; background: #202f32; } .primary { color: #082321; background: #62c9bf; font-weight: 650; }
  .attachment-table { display: grid; gap: 4px; overflow: auto; } .table-row { display: grid; grid-template-columns: 95px minmax(130px, 1.4fr) minmax(100px, 1fr) 70px 28px; gap: 5px; align-items: center; } .table-head { padding: 0 5px 4px; color: #6f8b8e; font-size: 10px; text-transform: uppercase; } .table-row input, .table-row select { padding: 7px 5px; font-size: 11px; } .inline-input { display: flex; align-items: center; gap: 2px; color: #6f8b8e; } .icon-button { padding: 5px; color: #dc8d8d; }
  .info-strip { display: flex; gap: 8px; color: #7c999b; } .info-strip strong { margin-right: 14px; color: #8fe0d4; }
  .energy-card { display: grid; gap: 5px; margin-bottom: 14px; } .energy-card strong { color: #8fe0d4; font-size: 23px; } .energy-card small { color: #789497; } .battery-bar { height: 7px; overflow: hidden; border-radius: 5px; background: #263b3e; } .battery-bar span { display: block; height: 100%; border-radius: 5px; background: #45c0b4; }
  .map-placeholder { position: relative; height: 155px; overflow: hidden; margin-bottom: 13px; border: 1px solid #30575a; border-radius: 5px; background: #123036; } .map-grid { position: absolute; inset: 0; opacity: .35; background-image: linear-gradient(#4b9290 1px, transparent 1px), linear-gradient(90deg, #4b9290 1px, transparent 1px); background-size: 35px 35px; transform: perspective(180px) rotateX(48deg) scale(1.5); } .map-placeholder span { position: absolute; top: 10px; left: 12px; color: #8fd7cd; font-size: 11px; } .map-placeholder i { position: absolute; top: 58%; left: 48%; width: 13px; height: 13px; border: 3px solid #d4f6e9; border-radius: 50%; background: #45c0b4; box-shadow: 0 0 0 8px #45c0b433; }
  .review-card { display: flex; gap: 12px; align-items: center; margin-bottom: 14px; padding: 12px; border: 1px solid #2e5558; border-radius: 5px; background: #1b3033; } .robot-icon { display: grid; place-items: center; width: 58px; height: 46px; border: 1px solid #4a9b97; border-radius: 6px; color: #8fe0d4; background: #245653; font-size: 11px; } .review-card p { margin: 3px 0 6px; color: #8ea8aa; } .badge { display: inline-block; padding: 2px 6px; margin-right: 4px; }
  .summary-grid { grid-template-columns: repeat(4, 1fr); } .warnings { color: #f0bd78; border-color: #73532f; background: #302719; } .warnings p { margin-top: 5px; } .success { color: #8fe0d4; border-color: #327267; background: #1b3431; }
  footer { display: flex; align-items: center; justify-content: space-between; gap: 10px; margin-top: 12px; } footer span { color: #627d80; font-size: 10px; }
</style>
