// wM-Buster ADV — Web Server + Comprehensive Dashboard
// GPL-3.0
#include "web_server.h"

#ifndef NATIVE_TEST
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <SD.h>
#include <Update.h>
#include "../ui/meter_db.h"
#include "../ui/ui_display.h"
#include "gnss/gnss_handler.h"
#include "../storage/config_store.h"
#include "radio_sx1262/wmbus_radio.h"
#include "mqtt_client.h"
#include "config.h"

namespace wmb {

static WebServer     server(80);
static DNSServer     dns;
static AnalyzeCallbackFn s_analyze_cb = nullptr;
extern WMBStats g_stats;

void set_analyze_callback(AnalyzeCallbackFn cb) { s_analyze_cb = cb; }
static void webui_task(void* pvParameters);

// ─────────────────────────────────────────────────────────────────────────────
// INDEX HTML  (single ~20 KB self-contained SPA)
// ─────────────────────────────────────────────────────────────────────────────
// Modern dark dashboard — zinc/violet palette, Inter-style typography
// ─────────────────────────────────────────────────────────────────────────────
static const char INDEX_HTML[] PROGMEM = R"RAW(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>wM-Buster ADV</title>
    <style>
        @import url('https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;500;700;800&family=JetBrains+Mono:wght@400;500&display=swap');
        
        :root {
            --bg-base: #000000;
            --bg-panel: rgba(13, 13, 18, 0.8);
            --bg-card: #0D0D12;
            --border: #220033;
            --primary: #BB00FF;
            --primary-glow: rgba(187, 0, 255, 0.4);
            --accent: #00E5C3;
            --success: #2ECC71;
            --warning: #F39C12;
            --danger: #E74C3C;
            --text-main: #DDE6F0;
            --text-muted: #8BA3BF;
            --font-ui: 'Outfit', sans-serif;
            --font-mono: 'JetBrains Mono', monospace;
        }

        * { margin: 0; padding: 0; box-sizing: border-box; }
        
        body {
            background-color: var(--bg-base);
            color: var(--text-main);
            font-family: var(--font-ui);
            line-height: 1.5;
            -webkit-font-smoothing: antialiased;
            overflow-x: hidden;
            width: 100%;
        }

        /* Glassmorphism Utilities */
        .glass {
            background: var(--bg-panel);
            backdrop-filter: blur(12px);
            -webkit-backdrop-filter: blur(12px);
            border: 1px solid var(--border);
            border-radius: 16px;
        }
        
        .card {
            background: var(--bg-card);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 16px;
            transition: transform 0.2s, box-shadow 0.2s;
        }

        /* Layout */
        .app-container {
            max-width: 1200px;
            width: 100%;
            margin: 0 auto;
            padding: 12px;
            display: grid;
            grid-template-rows: auto 1fr;
            min-height: 100vh;
            gap: 16px;
            box-sizing: border-box;
        }

        /* Header */
        header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 12px 16px;
            box-shadow: 0 4px 30px rgba(0, 0, 0, 0.1);
            flex-wrap: wrap;
            gap: 12px;
            width: 100%;
            box-sizing: border-box;
            min-width: 0;
        }

        .logo-area {
            display: flex;
            align-items: center;
            gap: 10px;
        }

        .logo-text {
            font-size: 20px;
            font-weight: 800;
            background: linear-gradient(135deg, var(--accent), var(--primary));
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            letter-spacing: -0.5px;
        }

        .status-pills {
            display: flex;
            gap: 8px;
            flex-wrap: wrap;
        }

        .pill {
            padding: 4px 10px;
            border-radius: 20px;
            font-size: 11px;
            font-weight: 600;
            letter-spacing: 0.5px;
            background: rgba(255,255,255,0.05);
            border: 1px solid var(--border);
            display: flex;
            align-items: center;
            gap: 6px;
            white-space: nowrap;
        }
        
        .pill-indicator {
            width: 8px;
            height: 8px;
            border-radius: 50%;
        }

        .pill.active .pill-indicator { background: var(--success); box-shadow: 0 0 8px var(--success); }
        .pill.search .pill-indicator { background: var(--warning); box-shadow: 0 0 8px var(--warning); }
        .pill.error .pill-indicator { background: var(--danger); box-shadow: 0 0 8px var(--danger); }

        /* Navigation */
        nav {
            display: flex;
            gap: 6px;
            background: rgba(0,0,0,0.2);
            padding: 4px;
            border-radius: 12px;
            border: 1px solid var(--border);
            overflow-x: auto;
            scrollbar-width: none;
            width: 100%;
            min-width: 0;
            box-sizing: border-box;
        }
        nav::-webkit-scrollbar { display: none; }

        .nav-btn {
            background: transparent;
            border: none;
            color: var(--text-muted);
            font-family: inherit;
            font-size: 13px;
            font-weight: 600;
            padding: 8px 12px;
            border-radius: 8px;
            cursor: pointer;
            transition: all 0.2s;
            white-space: nowrap;
            flex: 1;
            text-align: center;
        }

        .nav-btn.active { color: #fff; background: var(--primary); box-shadow: 0 4px 12px var(--primary-glow); }

        /* Content Area */
        main { min-width: 0; width: 100%; box-sizing: border-box; display: block; }
        .tab-content { display: none; animation: fadeIn 0.3s ease; min-width: 0; width: 100%; box-sizing: border-box; }
        .tab-content.active { display: block; }
        
        @keyframes fadeIn { from { opacity: 0; transform: translateY(5px); } to { opacity: 1; transform: translateY(0); } }

        /* Grid Layouts */
        .grid-2 { display: grid; grid-template-columns: repeat(auto-fit, minmax(100%, 1fr)); gap: 16px; width: 100%; }
        .grid-3 { display: grid; grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); gap: 12px; width: 100%; }
        .grid-2 > *, .grid-3 > * { min-width: 0; }
        
        .card { width: 100%; overflow: hidden; box-sizing: border-box; min-width: 0; }

        @media (min-width: 768px) {
            header { flex-wrap: nowrap; }
            nav { width: auto; }
            .grid-2 { grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); }
            .grid-3 { grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 16px; }
        }

        /* Typography & Data Display */
        h2 { font-size: 18px; font-weight: 700; margin-bottom: 12px; color: var(--text-main); display: flex; align-items: center; gap: 8px; }
        h3 { font-size: 14px; font-weight: 600; margin-bottom: 10px; color: var(--text-muted); text-transform: uppercase; letter-spacing: 1px; }
        
        .stat-box { display: flex; flex-direction: column; gap: 4px; }
        .stat-label { font-size: 11px; color: var(--text-muted); font-weight: 500; text-transform: uppercase; letter-spacing: 0.5px; }
        .stat-value { font-size: 24px; font-weight: 700; font-variant-numeric: tabular-nums; }

        /* Tables & Lists */
        .table-responsive { width: 100%; max-width: 100%; overflow-x: auto; -webkit-overflow-scrolling: touch; box-sizing: border-box; }
        .data-table { width: 100%; border-collapse: collapse; font-size: 13px; min-width: 500px; }
        .data-table th { text-align: left; padding: 10px; color: var(--text-muted); font-weight: 500; border-bottom: 1px solid var(--border); }
        .data-table td { padding: 10px; border-bottom: 1px solid rgba(51, 65, 85, 0.2); }
        .data-table tr:last-child td { border-bottom: none; }

        .mono { font-family: var(--font-mono); }
        
        /* Meters Grid */
        .meter-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(280px, 1fr)); gap: 16px; }
        .meter-card { position: relative; overflow: hidden; }
        .meter-card::before {
            content: ''; position: absolute; top: 0; left: 0; width: 4px; height: 100%;
            background: var(--border); transition: background 0.3s;
        }
        .meter-card.starred::before { background: var(--warning); box-shadow: 0 0 12px var(--warning); }
        
        .meter-hdr { display: flex; justify-content: space-between; margin-bottom: 8px; }
        .meter-title { font-weight: 700; font-size: 15px; color: var(--accent); }
        .meter-id { font-family: var(--font-mono); color: var(--text-muted); font-size: 12px; background: rgba(0,0,0,0.3); padding: 2px 6px; border-radius: 4px; }
        .meter-value { font-size: 20px; font-weight: 800; margin-bottom: 6px; }
        .meter-meta { display: flex; gap: 8px; font-size: 11px; color: var(--text-muted); flex-wrap: wrap; }
        .meter-meta span { display: flex; align-items: center; gap: 4px; }

        /* Forms & Inputs */
        .form-group { margin-bottom: 12px; }
        .form-group label { display: block; margin-bottom: 4px; font-size: 12px; color: var(--text-muted); font-weight: 500; }
        .form-control {
            width: 100%; background: rgba(0,0,0,0.2); border: 1px solid var(--border);
            color: var(--text-main); font-family: inherit; font-size: 14px;
            padding: 10px 12px; border-radius: 8px; transition: border-color 0.2s, box-shadow 0.2s;
        }
        .form-control:focus { outline: none; border-color: var(--primary); box-shadow: 0 0 0 2px var(--primary-glow); }
        .form-control.mono { font-family: var(--font-mono); }
        select.form-control { appearance: none; background-image: url("data:image/svg+xml;charset=UTF-8,%3csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%2394a3b8' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'%3e%3cpolyline points='6 9 12 15 18 9'%3e%3c/polyline%3e%3c/svg%3e"); background-repeat: no-repeat; background-position: right 12px center; background-size: 16px; padding-right: 36px; }

        .checkbox-wrapper { display: flex; align-items: center; gap: 8px; margin-top: 6px; cursor: pointer; }
        .checkbox-wrapper input[type="checkbox"] { width: 16px; height: 16px; accent-color: var(--primary); cursor: pointer; }
        .checkbox-wrapper span { font-size: 13px; color: var(--text-main); }

        .btn {
            display: inline-flex; align-items: center; justify-content: center; gap: 6px;
            background: var(--bg-card); border: 1px solid var(--border); color: var(--text-main);
            font-family: inherit; font-size: 13px; font-weight: 600;
            padding: 10px 16px; border-radius: 8px; cursor: pointer; transition: all 0.2s;
        }
        .btn-primary { background: var(--primary); border-color: var(--primary); color: #fff; }
        .btn-danger { background: rgba(239, 68, 68, 0.1); border-color: rgba(239, 68, 68, 0.3); color: var(--danger); }

        /* Utilities */
        .text-green { color: var(--success); }
        .text-orange { color: var(--warning); }
        .text-red { color: var(--danger); }
        .text-blue { color: var(--accent); }
        .flex-between { display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 10px; }
        .mt-4 { margin-top: 16px; }
        .mb-4 { margin-bottom: 16px; }
        
        /* Modal */
        .modal-overlay {
            position: fixed; top: 0; left: 0; width: 100%; height: 100%;
            background: rgba(0,0,0,0.7); backdrop-filter: blur(4px);
            display: none; justify-content: center; align-items: flex-end; z-index: 100;
            opacity: 0; transition: opacity 0.2s;
        }
        .modal-overlay.active { display: flex; opacity: 1; }
        .modal-content {
            background: var(--bg-base); border: 1px solid var(--border);
            border-radius: 16px 16px 0 0; width: 100%; max-width: 500px; padding: 20px;
            transform: translateY(100%); transition: transform 0.3s cubic-bezier(0.175, 0.885, 0.32, 1.275);
            max-height: 90vh; overflow-y: auto;
        }
        @media (min-width: 768px) {
            .modal-overlay { align-items: center; }
            .modal-content { border-radius: 16px; transform: scale(0.95); transition: transform 0.2s; }
            .modal-overlay.active .modal-content { transform: scale(1); }
        }
        .modal-overlay.active .modal-content { transform: translateY(0); }
    </style>
</head>
<body>

<div class="app-container">
    <header class="glass">
        <div class="logo-area">
            <div class="logo-text">wM-Buster ADV</div>
        </div>
        
        <div class="status-pills">
            <div class="pill active" id="pill-radio"><div class="pill-indicator"></div>CT</div>
            <div class="pill search" id="pill-gps"><div class="pill-indicator"></div>GPS</div>
        </div>
        
        <nav id="main-nav">
            <button class="nav-btn active" onclick="switchTab('dashboard')">Live</button>
            <button class="nav-btn" onclick="switchTab('meters')">Meters</button>
            <button class="nav-btn" onclick="switchTab('settings')">Settings</button>
            <button class="nav-btn" onclick="switchTab('tools')">OTA</button>
        </nav>
    </header>

    <main>
        <div id="tab-dashboard" class="tab-content active">
            <div class="grid-3 mb-4">
                <div class="card glass"><div class="stat-box"><div class="stat-label">Total Signals</div><div class="stat-value text-blue" id="st-rx">--</div></div></div>
                <div class="card glass"><div class="stat-box"><div class="stat-label">Decoded</div><div class="stat-value text-green" id="st-ok">--</div></div></div>
                <div class="card glass"><div class="stat-box"><div class="stat-label">Active Meters</div><div class="stat-value" id="st-meters">--</div></div></div>
            </div>
            
            <div class="card glass">
                <div class="flex-between mb-4">
                    <h2>Live Feed</h2>
                    <button class="btn btn-primary" onclick="switchTab('meters')">Full DB</button>
                </div>
                <div class="table-responsive">
                    <table class="data-table">
                        <thead>
                            <tr>
                                <th>Meter ID</th>
                                <th>Type / Driver</th>
                                <th>Mfg</th>
                                <th>Value</th>
                                <th>RSSI</th>
                                <th>Seen</th>
                            </tr>
                        </thead>
                        <tbody id="live-feed-tb">
                            <tr><td colspan="6" style="text-align:center;">Waiting for signals...</td></tr>
                        </tbody>
                    </table>
                </div>
            </div>
        </div>

        <div id="tab-meters" class="tab-content">
            <div class="flex-between mb-4">
                <h2>Meter Database <span id="db-count-badge" style="font-size: 12px; font-weight: normal; color: var(--text-muted); background: rgba(255,255,255,0.1); padding: 2px 8px; border-radius: 10px; margin-left: 8px;"></span></h2>
                <button class="btn btn-primary" onclick="openMeterModal()">+ Add Key</button>
            </div>
            
            <div class="card glass mb-4">
                <div class="grid-3" style="gap: 12px;">
                    <div class="form-group" style="margin: 0;">
                        <input type="text" id="filter-search" class="form-control" placeholder="Search ID or Name..." onkeyup="refreshData()">
                    </div>
                    <div class="form-group" style="margin: 0;">
                        <select id="filter-type" class="form-control" onchange="refreshData()">
                            <option value="">All Types</option>
                            <option value="water">Water</option>
                            <option value="heat">Heat</option>
                            <option value="gas">Gas</option>
                            <option value="electricity">Electricity</option>
                        </select>
                    </div>
                    <div class="form-group" style="margin: 0;">
                        <select id="filter-sort" class="form-control" onchange="refreshData()">
                            <option value="latest">Sort: Latest</option>
                            <option value="rssi">Sort: Strongest RSSI</option>
                            <option value="id">Sort: Meter ID</option>
                        </select>
                    </div>
                </div>
                <label class="checkbox-wrapper" style="margin-top: 12px;">
                    <input type="checkbox" id="filter-starred" onchange="refreshData()">
                    <span>★ Show Starred Only</span>
                </label>
            </div>
            <div class="meter-grid" id="meter-grid-container">
                <div style="color:var(--text-muted); grid-column: 1/-1; text-align:center; padding: 40px;">Loading meters...</div>
            </div>
        </div>

        <div id="tab-settings" class="tab-content">
            <h2>Device Configuration</h2>
            <div class="grid-2">
                <div class="card glass">
                    <h3>Radio & System</h3>
                    <div class="form-group">
                        <label>Listening Mode</label>
                        <select class="form-control" id="s-mode">
                            <option value="CT">C1 + T1 (868.95 MHz)</option>
                            <option value="S1">S1 (868.30 MHz)</option>
                        </select>
                    </div>
                </div>
                
                <div class="card glass">
                    <h3>WiFi Network Mode</h3>
                    <div class="form-group">
                        <label>WebUI Network Mode (Reboot req)</label>
                        <select class="form-control" id="s-wmode">
                            <option value="1">AP Only (Hotspot mode)</option>
                            <option value="2">STA Only (Join Home WiFi)</option>
                        </select>
                    </div>
                    <div class="form-group">
                        <label>Home SSID</label>
                        <input type="text" class="form-control" id="s-wssid">
                    </div>
                    <div class="form-group">
                        <label>WiFi Password</label>
                        <input type="password" class="form-control" id="s-wpass">
                    </div>
                </div>
                
                <div class="card glass">
                    <h3>MQTT Broker (HA)</h3>
                    <div class="form-group"><label>Broker IP</label><input type="text" class="form-control" id="s-mhost"></div>
                    <div class="grid-2" style="gap:10px;">
                        <div class="form-group"><label>Port</label><input type="number" class="form-control mono" id="s-mport" value="1883"></div>
                        <div class="form-group"><label>Username</label><input type="text" class="form-control" id="s-musr"></div>
                    </div>
                    <div class="form-group"><label>Password</label><input type="password" class="form-control" id="s-mpwd"></div>
                    <label class="checkbox-wrapper"><input type="checkbox" id="s-ha"><span>Enable Auto-Discovery</span></label>
                </div>
                
                <div class="card glass">
                    <h3>Ntfy.sh Alerts</h3>
                    <div class="form-group">
                        <label>Topic Name</label>
                        <input type="text" class="form-control mono" id="s-ntfy" placeholder="my_secret_topic">
                    </div>
                </div>
            </div>
            <div class="mt-4">
                <button class="btn btn-primary" onclick="saveSettings()" style="width:100%; padding:14px;">Save All Configuration</button>
            </div>
        </div>

        <div id="tab-tools" class="tab-content">
            <div class="grid-2">
                <div class="card glass">
                    <h2>Raw Hex Analyzer</h2>
                    <div class="form-group"><textarea class="form-control mono" id="hex-ta" rows="4" placeholder="1E44..."></textarea></div>
                    <div class="flex-between">
                        <button class="btn btn-primary" onclick="analyzeHex()">Analyze</button>
                        <button class="btn" onclick="document.getElementById('hex-ta').value=''; document.getElementById('az-res').innerHTML='';">Clear</button>
                    </div>
                    <div id="az-res" class="mt-4"></div>
                </div>
                
                <div class="card glass">
                    <h2>Firmware OTA Update</h2>
                    <form method="POST" action="/update" enctype="multipart/form-data">
                        <div class="form-group"><input type="file" class="form-control" name="update" accept=".bin" required></div>
                        <button type="submit" class="btn btn-danger" style="width:100%">Flash Firmware</button>
                    </form>
                </div>
            </div>
        </div>
    </main>
</div>

<!-- METER CONFIG MODAL -->
<div class="modal-overlay" id="meter-modal">
    <div class="modal-content">
        <h2 id="modal-title">Configure Meter</h2>
        <div class="form-group">
            <label>Meter ID (8 Hex Digits)</label>
            <input type="text" class="form-control mono" id="m-id" maxlength="8">
        </div>
        <div class="form-group">
            <label>Display Name</label>
            <input type="text" class="form-control" id="m-name">
        </div>
        <div class="form-group">
            <label>AES-128 Key (32 Hex)</label>
            <input type="text" class="form-control mono" id="m-key" maxlength="32">
        </div>
        <div class="form-group">
            <label>Force Driver (Optional)</label>
            <select class="form-control" id="m-drv">
                <option value="auto">Auto-detect (Recommended)</option>
                <option value="iperl">iperl - Water</option>
                <option value="multical21">multical21 - Water</option>
                <option value="kamheat">kamheat - Heat</option>
                <option value="sharky">sharky - Heat</option>
                <option value="fhkvdataiv">fhkvdataiv - Heat Allocator</option>
                <option value="gwfgas">gwfgas - Gas</option>
                <option value="amiplus">amiplus - Electric</option>
            </select>
        </div>
        <div class="flex-between mt-4">
            <button class="btn btn-danger" id="m-del-btn" onclick="deleteMeter()" style="display:none;">Delete</button>
            <div style="margin-left:auto; display:flex; gap:8px;">
                <button class="btn" onclick="closeMeterModal()">Cancel</button>
                <button class="btn btn-primary" onclick="saveMeter()">Save</button>
            </div>
        </div>
    </div>
</div>

<script>
    function switchTab(id) {
        document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));
        document.querySelectorAll('.nav-btn').forEach(el => el.classList.remove('active'));
        document.getElementById('tab-' + id).classList.add('active');
        event.currentTarget.classList.add('active');
    }

    const modal = document.getElementById('meter-modal');
    function openMeterModal(id='', name='', key='', drv='auto') {
        document.getElementById('m-id').value = id;
        document.getElementById('m-name').value = name;
        document.getElementById('m-key').value = key;
        document.getElementById('m-drv').value = drv;
        document.getElementById('m-del-btn').style.display = id ? 'block' : 'none';
        modal.classList.add('active');
    }
    function closeMeterModal() { modal.classList.remove('active'); }

    const timeAgo = (ms) => {
        const s = Math.floor((Date.now() - ms)/1000);
        if(s < 60) return s + "s";
        if(s < 3600) return Math.floor(s/60) + "m";
        return Math.floor(s/3600) + "h";
    };

    let meterCfgs = {};
    async function loadConfigs() {
        try {
            const arr = await (await fetch('/api/configs')).json();
            meterCfgs = {}; arr.forEach(c => { meterCfgs[c.id] = c; });
        } catch(e) {}
    }

    async function refreshData() {
        try {
            const st = await (await fetch('/api/stats')).json();
            document.getElementById('st-rx').textContent = st.rx_total || '--';
            document.getElementById('st-ok').textContent = st.rx_good || '--';
            document.getElementById('pill-radio').innerHTML = `<div class="pill-indicator"></div>${st.mode||'--'} (${st.rssi?.toFixed(0)||0}dBm)`;
            const pGps = document.getElementById('pill-gps');
            if(st.gnss_fix) { pGps.className = "pill active"; pGps.innerHTML = `<div class="pill-indicator"></div>GPS Fix`; } 
            else { pGps.className = "pill search"; pGps.innerHTML = `<div class="pill-indicator"></div>GPS Wait`; }

            const meters = await (await fetch('/api/meters')).json();
            document.getElementById('st-meters').textContent = meters.length;
            
            const tb = document.getElementById('live-feed-tb');
            tb.innerHTML = meters.slice(0, 5).map(m => {
                const starStr = m.is_starred ? '<span style="color:var(--warning)">★</span>' : '☆';
                return `
                <tr>
                    <td class="mono"><a href="#" onclick="toggleStar('${m.id}')" style="text-decoration:none; color:inherit;">${starStr}</a> ${m.id}</td>
                    <td><span class="text-blue" style="font-weight:600;">${m.type}</span><br><span style="font-size:10px;color:var(--text-muted);">${m.driver}</span></td>
                    <td><span class="pill search" style="display:inline-block;padding:2px 6px;">${m.mfct || 'UNK'}</span></td>
                    <td style="font-weight:700;">${m.primary}</td>
                    <td class="mono" style="color:${m.rssi > -80 ? 'var(--success)' : (m.rssi > -95 ? 'var(--warning)' : 'var(--danger)')}">${m.rssi}</td>
                    <td>${timeAgo(m.ts)}</td>
                </tr>
            `}).join('') || `<tr><td colspan="6" style="text-align:center;">No meters found yet.</td></tr>`;

            // Filter logic
            const qSearch = document.getElementById('filter-search').value.toLowerCase();
            const qType = document.getElementById('filter-type').value.toLowerCase();
            const qStarred = document.getElementById('filter-starred').checked;
            const qSort = document.getElementById('filter-sort').value;
            
            let filtered = meters.filter(m => {
                if (qStarred && !m.is_starred) return false;
                if (qType && !(m.media || '').toLowerCase().includes(qType) && !(m.type || '').toLowerCase().includes(qType)) return false;
                if (qSearch) {
                    const str = `${m.id} ${m.name||''} ${m.mfct||''} ${m.driver||''}`.toLowerCase();
                    if (!str.includes(qSearch)) return false;
                }
                return true;
            });
            
            if (qSort === 'latest') filtered.sort((a,b) => b.ts - a.ts);
            else if (qSort === 'rssi') filtered.sort((a,b) => b.rssi - a.rssi);
            else if (qSort === 'id') filtered.sort((a,b) => a.id.localeCompare(b.id));
            
            document.getElementById('db-count-badge').textContent = `Showing ${filtered.length} of ${meters.length} Captures`;

            const mg = document.getElementById('meter-grid-container');
            mg.innerHTML = filtered.map(m => {
                const cfg = meterCfgs[m.id] || {};
                const name = m.name || m.id;
                const starClass = m.is_starred ? 'starred' : ''; 
                const starStr = m.is_starred ? '★' : '☆';
                return `
                <div class="card glass meter-card ${starClass}">
                    <div class="meter-hdr">
                        <div class="meter-title">${name}</div>
                        <div class="meter-id" style="cursor:pointer;" onclick="toggleStar('${m.id}')">${starStr} ${m.id}</div>
                    </div>
                    <div class="meter-value">${m.primary}</div>
                    <div class="meter-meta">
                        <span>RSSI: <span class="mono" style="color:${m.rssi > -80 ? 'var(--success)' : 'var(--warning)'}">${m.rssi}</span></span>
                        <span>Mfg: ${m.mfct || 'UNK'}</span>
                    </div>
                    <div class="mt-4">
                        <button class="btn" style="width:100%; font-size:12px; padding:8px;" onclick="openMeterModal('${m.id}','${cfg.name||''}','${cfg.key||''}','${cfg.driver||'auto'}')">Config / Key</button>
                    </div>
                </div>
            `}).join('') || `<div style="color:var(--text-muted); grid-column: 1/-1; text-align:center; padding: 40px;">No meters match your filters.</div>`;
        } catch(e) {}
    }
    
    async function toggleStar(id) {
        try {
            await fetch('/api/star', { method: 'POST', body: JSON.stringify({id}) });
            refreshData();
        } catch(e) {}
    }

    async function loadSettings() {
        try {
            const s = await (await fetch('/api/settings')).json();
            document.getElementById('s-mode').value = s.radio_mode || 'CT';
            document.getElementById('s-wmode').value = (s.webui_mode == 2) ? 2 : 1;
            document.getElementById('s-wssid').value = s.wifi_ssid || '';
            document.getElementById('s-mhost').value = s.mqtt_host || '';
            document.getElementById('s-mport').value = s.mqtt_port || 1883;
            document.getElementById('s-musr').value = s.mqtt_user || '';
            document.getElementById('s-ha').checked = !!s.ha_discovery;
            
            let ntfy = s.ntfy_url || '';
            if (ntfy.startsWith('https://ntfy.sh/')) ntfy = ntfy.substring(16);
            document.getElementById('s-ntfy').value = ntfy;
        } catch(e) {}
    }

    async function saveSettings() {
        const body = {
            radio_mode: document.getElementById('s-mode').value,
            webui_mode: parseInt(document.getElementById('s-wmode').value)||1,
            wifi_ssid: document.getElementById('s-wssid').value,
            wifi_pass: document.getElementById('s-wpass').value,
            mqtt_host: document.getElementById('s-mhost').value,
            mqtt_port: parseInt(document.getElementById('s-mport').value)||1883,
            mqtt_user: document.getElementById('s-musr').value,
            mqtt_pass: document.getElementById('s-mpwd').value,
            ha_discovery: document.getElementById('s-ha').checked,
            ntfy_url: document.getElementById('s-ntfy').value
        };
        try {
            const r = await fetch('/api/settings', { method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(body) });
            if(r.ok) alert('Settings saved!');
            else alert('Failed to save.');
        } catch(e) { alert(e.message); }
    }

    async function saveMeter() {
        let body = { id: document.getElementById('m-id').value, name: document.getElementById('m-name').value, key: document.getElementById('m-key').value, driver: document.getElementById('m-drv').value };
        if(body.driver === 'auto') body.driver = '';
        try { await fetch('/api/configs', { method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(body) }); closeMeterModal(); loadConfigs(); refreshData(); } catch(e) {}
    }
    
    async function deleteMeter() {
        if(!confirm('Delete?')) return;
        try { await fetch('/api/configs/delete', { method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({id: document.getElementById('m-id').value}) }); closeMeterModal(); loadConfigs(); refreshData(); } catch(e) {}
    }

    async function analyzeHex() {
        const hex = document.getElementById('hex-ta').value.replace(/\s+/g,'');
        if(!hex) return;
        const res = document.getElementById('az-res');
        res.innerHTML = '<div class="text-blue">Analyzing...</div>';
        try {
            const r = await fetch('/api/analyze', { method:'POST', headers:{'Content-Type':'text/plain'}, body:hex });
            const d = await r.json();
            if(d.error) { res.innerHTML = `<div class="text-red">Error: ${d.error}</div>`; return; }
            let html = `<div class="card" style="background:rgba(0,0,0,0.3); border-color:var(--border);">
                    <div style="display:flex; gap:6px; margin-bottom:12px; flex-wrap:wrap;">
                        <span class="pill active">${d.driver}</span><span class="pill mono">${d.id}</span>
                        ${d.success ? '<span class="pill active text-green">Decoded</span>' : '<span class="pill search">Encrypted</span>'}
                    </div><div class="table-responsive"><table class="data-table">`;
            if(d.fields && d.fields.length) { html += d.fields.map(f => `<tr><td style="color:var(--text-muted);">${f.name}</td><td style="font-weight:600;">${f.val}</td></tr>`).join(''); } 
            else { html += `<tr><td>No fields</td></tr>`; }
            html += `</table></div></div>`;
            res.innerHTML = html;
        } catch(e) { res.innerHTML = `<div class="text-red">Error: ${e.message}</div>`; }
    }

    loadSettings();
    loadConfigs().then(refreshData);
    setInterval(refreshData, 4000);
</script>
</body>
</html>

)RAW";

// ─────────────────────────────────────────────────────────────────────────────
// Route handlers
// ─────────────────────────────────────────────────────────────────────────────

static void h_root()   { server.send(200,"text/html",INDEX_HTML); }

static void h_status() {
    char b[192];
    snprintf(b,sizeof(b),
        "{\"gnss_fix\":%s,\"lat\":%.6f,\"lon\":%.6f,"
        "\"meters_count\":%zu,\"sd_logging\":%s}",
        g_stats.gnss_fix?"true":"false", g_stats.gnss_lat, g_stats.gnss_lon,
        get_global_meter_db().get_meter_count(),
        get_global_ui_display().is_sd_logging_enabled()?"true":"false");
    server.send(200,"application/json",b);
}

static void h_stats() {
    char b[256];
    snprintf(b,sizeof(b),
        "{\"rx_total\":%lu,\"rx_good\":%lu,\"rx_bad\":%lu,\"rx_enc\":%lu,"
        "\"rssi\":%.1f,\"snr\":%.1f,\"uptime\":%lu,"
        "\"gnss_fix\":%s,\"lat\":%.6f,\"lon\":%.6f,"
        "\"meters\":{\"count\":%zu,\"max\":%d}}",
        (unsigned long)g_stats.radio_rx_total,(unsigned long)g_stats.radio_rx_good,
        (unsigned long)g_stats.radio_rx_bad,(unsigned long)g_stats.radio_rx_encrypted,
        g_stats.radio_rssi_live,g_stats.radio_snr_live,(unsigned long)g_stats.uptime_s,
        g_stats.gnss_fix?"true":"false",g_stats.gnss_lat,g_stats.gnss_lon,
        get_global_meter_db().get_meter_count(),MAX_METERS);
    server.send(200,"application/json",b);
}

static void h_meters() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200,"application/json","");
    server.sendContent("[");
    
    get_global_meter_db().lock();
    size_t cnt = get_global_meter_db().get_meter_count();
    get_global_meter_db().unlock();

    bool first=true;
    for(size_t i=0;i<cnt;i++){
        get_global_meter_db().lock();
        // Since cnt might have changed or array might be sorted, bound check is needed.
        // It's safest to just try to get the i-th meter. If sort happened, we might show
        // one twice or skip one, but it doesn't matter for the web UI feed.
        const MeterEntry* p = get_global_meter_db().get_meter(i);
        if (!p) {
            get_global_meter_db().unlock();
            break;
        }
        MeterEntry m_copy;
        memcpy(&m_copy, p, sizeof(MeterEntry));
        get_global_meter_db().unlock();

        const MeterEntry* m = &m_copy;

        if(!first) server.sendContent(",");
        first=false;

        // Look up config for key / custom name
        MeterConfig mc; memset(&mc,0,sizeof(mc));
        bool hc=cs_find_meter(m->id,&mc);
        const char* dn=(hc&&mc.name[0])?mc.name:m->name;

        char hdr[400];
        snprintf(hdr,sizeof(hdr),
            "{\"driver\":\"%s\",\"mfct\":\"%s\",\"type\":\"%s\",\"id\":\"%s\","
            "\"name\":\"%s\",\"media\":\"%s\",\"rssi\":%.0f,"
            "\"count\":%lu,\"primary\":\"%s\","
            "\"lat\":%.6f,\"lon\":%.6f,\"gnss_fix\":%s,"
            "\"has_key\":%s,\"ts\":%lu,\"is_starred\":%s,\"fields\":[",
            m->driver_name,m->mfct,m->friendly_type,m->id,dn,m->media,m->last_rssi,
            (unsigned long)m->telegram_count,m->primary_value_str,
            m->lat,m->lon,m->gnss_fix?"true":"false",
            (hc&&mc.has_key)?"true":"false",
            (unsigned long)m->last_seen_ms,
            m->is_starred?"true":"false");
        server.sendContent(hdr);

        // Stream fields from display_fields
        char dbuf[sizeof(m->display_fields)];
        memcpy(dbuf, m->display_fields, sizeof(dbuf));
        dbuf[sizeof(dbuf)-1] = '\0';

        char* ptr = dbuf;
        bool ff = true;
        while (*ptr) {
            char* n1 = strchr(ptr, '\n');
            if (!n1) break;
            *n1 = '\0';
            char* k = ptr;
            
            ptr = n1 + 1;
            char* n2 = strchr(ptr, '\n');
            if (!n2) break;
            *n2 = '\0';
            char* v = ptr;
            
            ptr = n2 + 1;
            
            if (!ff) server.sendContent(",");
            ff = false;
            
            char line[128];
            snprintf(line, sizeof(line), "{\"name\":\"%s\",\"val\":\"%s\"}", k, v);
            server.sendContent(line);
        }
        server.sendContent("]}");
    }
    server.sendContent("]");
}

static void h_configs_get() {
    MeterConfig cfgs[CONFIG_MAX_METERS];
    size_t n=cs_get_all_meters(cfgs,CONFIG_MAX_METERS);
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200,"application/json","");
    server.sendContent("[");
    for(size_t i=0;i<n;i++){
        if(i) server.sendContent(",");
        char b[160];
        snprintf(b,sizeof(b),
            "{\"id\":\"%.8s\",\"name\":\"%s\",\"driver\":\"%s\",\"has_key\":%s}",
            cfgs[i].id,cfgs[i].name,cfgs[i].driver,cfgs[i].has_key?"true":"false");
        server.sendContent(b);
    }
    server.sendContent("]");
}

// Very minimal JSON value extractor (no deps on ArduinoJson)
static String jval(const String& body, const char* key) {
    String needle = String("\"") + key + "\":";
    int p = body.indexOf(needle);
    if (p < 0) return "";
    p += needle.length();
    while (p < (int)body.length() && body[p] == ' ') p++;
    if (body[p] == '"') {
        p++;
        int e = body.indexOf('"', p);
        return e < 0 ? "" : body.substring(p, e);
    }
    // number / bool / null
    int e = p;
    while (e < (int)body.length() && body[e] != ',' && body[e] != '}') e++;
    return body.substring(p, e);
}

static void h_configs_post() {
    String body = server.arg("plain");
    if (body.isEmpty()) { server.send(400,"text/plain","empty"); return; }

    MeterConfig mc; memset(&mc,0,sizeof(mc));
    snprintf(mc.id,     sizeof(mc.id),     "%.8s", jval(body,"id").c_str());
    snprintf(mc.name,   sizeof(mc.name),   "%s",   jval(body,"name").c_str());
    snprintf(mc.driver, sizeof(mc.driver), "%s",   jval(body,"driver").c_str());
    if (!mc.driver[0]) snprintf(mc.driver, sizeof(mc.driver), "auto");

    String keyhex = jval(body, "key");
    keyhex.replace(" ","");
    if (keyhex.length() >= 32) {
        mc.has_key = cs_hex_to_key(keyhex.c_str(), mc.key);
    }

    if (!mc.id[0] || strlen(mc.id) != 8) { server.send(400,"text/plain","bad id"); return; }
    cs_save_meter(mc);
    server.send(200,"text/plain","OK");
}

static void h_configs_delete() {
    String body = server.arg("plain");
    String id = jval(body, "id");
    if (id.length() != 8) { server.send(400,"text/plain","bad id"); return; }
    cs_delete_meter(id.c_str());
    server.send(200,"text/plain","OK");
}

static void h_settings_get() {
    DeviceSettings s; cs_load_settings(&s);
    char b[512];
    snprintf(b,sizeof(b),
        "{\"radio_mode\":\"%s\",\"mqtt_host\":\"%s\",\"mqtt_port\":%u,"
        "\"mqtt_user\":\"%s\",\"wifi_ssid\":\"%s\",\"ha_discovery\":%s,"
        "\"webui_mode\":%u,\"ntfy_url\":\"%s\"}",
        s.radio_mode,s.mqtt_host,(unsigned)s.mqtt_port,s.mqtt_user,
        s.wifi_ssid,s.ha_discovery?"true":"false",
        s.webui_mode,s.ntfy_url);
    server.send(200,"application/json",b);
}

static void h_settings_post() {
    String body=server.arg("plain");
    DeviceSettings s; cs_load_settings(&s);

    String m=jval(body,"radio_mode");
    if(m.length()) snprintf(s.radio_mode,sizeof(s.radio_mode),"%s",m.c_str());
    String mh=jval(body,"mqtt_host");
    if(mh.length()) snprintf(s.mqtt_host,sizeof(s.mqtt_host),"%s",mh.c_str());
    String mp=jval(body,"mqtt_port"); if(mp.length()) s.mqtt_port=(uint16_t)mp.toInt();
    String mu=jval(body,"mqtt_user"); if(mu.length()) snprintf(s.mqtt_user,sizeof(s.mqtt_user),"%s",mu.c_str());
    String mpw=jval(body,"mqtt_pass"); if(mpw.length()&&mpw!="null") snprintf(s.mqtt_pass,sizeof(s.mqtt_pass),"%s",mpw.c_str());
    String ws=jval(body,"wifi_ssid"); if(ws.length()) snprintf(s.wifi_ssid,sizeof(s.wifi_ssid),"%s",ws.c_str());
    String wp=jval(body,"wifi_pass"); if(wp.length()&&wp!="null") snprintf(s.wifi_pass,sizeof(s.wifi_pass),"%s",wp.c_str());
    String ha=jval(body,"ha_discovery"); if(ha.length()) s.ha_discovery=(ha=="true");
    
    String wm=jval(body,"webui_mode"); if(wm.length()) s.webui_mode=(uint8_t)wm.toInt();
    String ntfy=jval(body,"ntfy_url"); 
    if(ntfy.length()||body.indexOf("\"ntfy_url\"")>0) {
        if (ntfy.length() > 0 && !ntfy.startsWith("http")) {
            snprintf(s.ntfy_url, sizeof(s.ntfy_url), "https://ntfy.sh/%s", ntfy.c_str());
        } else {
            snprintf(s.ntfy_url, sizeof(s.ntfy_url), "%s", ntfy.c_str());
        }
    }

    cs_save_settings(s);

    // Apply MQTT immediately
    if(s.mqtt_host[0]){ set_mqtt_server(s.mqtt_host,s.mqtt_port); set_mqtt_enabled(true); }

    // Apply radio mode immediately
    bool c1t1=(strcmp(s.radio_mode,"S1")!=0);
    radio_switch_mode(c1t1);

    server.send(200,"text/plain","OK");
}

static void h_history() {
    if(!SD.cardType()||!SD.exists("/captures.csv")){server.send(200,"application/json","[]");return;}
    File f=SD.open("/captures.csv",FILE_READ);
    if(!f){server.send(200,"application/json","[]");return;}
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200,"application/json","");
    server.sendContent("[");
    f.readStringUntil('\n');
    bool first=true;
    while(f.available()){
        String line=f.readStringUntil('\n'); line.trim();
        if(line.length()<10) continue;
        // CSV: ts,id,driver,media,rssi,lat,lon,json
        int c[7]; int ci=0,p=0;
        while(ci<7&&p<(int)line.length()){int nx=line.indexOf(',',p);if(nx<0)nx=line.length();c[ci++]=nx;p=nx+1;}
        if(ci<5) continue;
        String ts=line.substring(0,c[0]),id=line.substring(c[0]+1,c[1]);
        String drv=line.substring(c[1]+1,c[2]),med=line.substring(c[2]+1,c[3]);
        String rssi=line.substring(c[3]+1,c[4]);
        String fields=ci>=7?line.substring(c[6]+1):"{}";
        if(fields.startsWith("\"")&&fields.endsWith("\""))fields=fields.substring(1,fields.length()-1);
        fields.replace("\"\"","\"");
        if(!first) server.sendContent(","); first=false;
        server.sendContent("{\"timestamp\":"+ts+",\"id\":\""+id+"\",\"driver\":\""+drv+
            "\",\"media\":\""+med+"\",\"rssi\":"+rssi+",\"fields\":"+
            (fields.length()>1?fields:"{}")+"}");
    }
    server.sendContent("]"); f.close();
}

static void h_csv() {
    if(SD.exists("/captures.csv")){File f=SD.open("/captures.csv",FILE_READ);if(f){server.streamFile(f,"text/csv");f.close();return;}}
    server.send(404,"text/plain","no csv");
}

static void h_mqtt_compat() {  // legacy GET /api/mqtt?host=&port=
    if(server.hasArg("host")){
        String host=server.arg("host");
        uint16_t port=server.hasArg("port")?server.arg("port").toInt():1883;
        set_mqtt_server(host.c_str(),port); set_mqtt_enabled(true);
    }
    server.send(200,"text/plain","OK");
}

static void h_analyze() {
    if(!s_analyze_cb){server.send(503,"application/json","{\"error\":\"not ready\"}");return;}
    if(server.method()!=HTTP_POST){server.send(405,"text/plain","POST required");return;}
    String body=server.arg("plain"); body.replace(" ",""); body.replace("\n",""); body.replace("\r","");
    if(body.isEmpty()){server.send(400,"application/json","{\"error\":\"empty\"}");return;}
    static char jbuf[4096];
    s_analyze_cb(body.c_str(),jbuf,sizeof(jbuf));
    server.send(200,"application/json",jbuf);
}

static void h_star() {
    if(server.method()!=HTTP_POST){server.send(405,"text/plain","POST required");return;}
    String body = server.arg("plain");
    String id = jval(body, "id");
    if (id.length() != 8) { server.send(400,"text/plain","bad id"); return; }
    
    get_global_meter_db().lock();
    bool found = false;
    for(size_t i=0; i<get_global_meter_db().get_meter_count(); i++) {
        MeterEntry* m = const_cast<MeterEntry*>(get_global_meter_db().get_meter(i));
        if (m && strncmp(m->id, id.c_str(), 8) == 0) {
            m->is_starred = !m->is_starred; // Toggle star
            found = true;
            break;
        }
    }
    get_global_meter_db().unlock();
    
    if (found) {
        server.send(200,"text/plain","OK");
    } else {
        server.send(404,"text/plain","Not Found");
    }
}

// ── init ─────────────────────────────────────────────────────────────────────
void init_web_server() {
    bool has_sta = g_settings.wifi_ssid[0] != '\0';
    if (g_settings.webui_enabled) {
        IPAddress ip(192,168,4,1),nm(255,255,255,0);
        if (g_settings.webui_mode == 2 && has_sta) { // STA Only
            WiFi.mode(WIFI_STA);
        } else if (g_settings.webui_mode == 1) { // AP Only
            WiFi.mode(WIFI_AP);
            WiFi.softAPConfig(ip,ip,nm);
            WiFi.softAP("wM-Buster ADV", g_settings.webui_ap_pass);
            dns.start(53,"*",ip);
        } else { // AP + STA (0)
            WiFi.mode(has_sta ? WIFI_AP_STA : WIFI_AP);
            WiFi.softAPConfig(ip,ip,nm);
            WiFi.softAP("wM-Buster ADV", g_settings.webui_ap_pass);
            dns.start(53,"*",ip);
        }
    } else {
        WiFi.mode(has_sta ? WIFI_STA : WIFI_OFF);
    }
    if (has_sta) {
        WiFi.begin(g_settings.wifi_ssid, g_settings.wifi_pass);
    }

    server.on("/",                    h_root);
    server.on("/api/status",          h_status);
    server.on("/api/stats",           h_stats);
    server.on("/api/meters",          h_meters);
    server.on("/api/configs",         HTTP_GET,  h_configs_get);
    server.on("/api/configs",         HTTP_POST, h_configs_post);
    server.on("/api/configs/delete",  HTTP_POST, h_configs_delete);
    server.on("/api/settings",        HTTP_GET,  h_settings_get);
    server.on("/api/settings",        HTTP_POST, h_settings_post);
    server.on("/api/history",         h_history);
    server.on("/api/captures.csv",    h_csv);
    server.on("/api/csv",             h_csv);
    server.on("/api/mqtt",            h_mqtt_compat);
    server.on("/api/analyze",         HTTP_POST, h_analyze);
    server.on("/api/star",            HTTP_POST, h_star);

    server.on("/update",HTTP_POST,[](){
        server.sendHeader("Connection","close");
        server.send(200,"text/html",Update.hasError()
            ?"<h2 style='color:#ef4444;font-family:sans-serif'>Update FAILED</h2>"
            :"<h2 style='color:#00d4ff;font-family:sans-serif'>Updated! Rebooting…</h2><script>setTimeout(()=>location.href='/',5000)</script>");
        ESP.restart();
    },[](){
        HTTPUpload& u=server.upload();
        if(u.status==UPLOAD_FILE_START) Update.begin(UPDATE_SIZE_UNKNOWN);
        else if(u.status==UPLOAD_FILE_WRITE) Update.write(u.buf,u.currentSize);
        else if(u.status==UPLOAD_FILE_END) Update.end(true);
    });

    server.onNotFound(h_root);
    // Silence browser auto-requests
    server.on("/favicon.ico", [](){server.send(204);});
    server.on("/apple-touch-icon.png", [](){server.send(204);});
    if (g_settings.webui_enabled) {
        server.begin();
    }

    xTaskCreatePinnedToCore(
        webui_task,
        "webui",
        8192,
        NULL,
        1,
        NULL,
        0 // Core 0 (Network)
    );
}

void toggle_webui_ap() {
    bool has_sta = g_settings.wifi_ssid[0] != '\0';
    if (g_settings.webui_enabled) {
        IPAddress ip(192,168,4,1),nm(255,255,255,0);
        if (g_settings.webui_mode == 2 && has_sta) {
            WiFi.mode(WIFI_STA);
        } else {
            WiFi.mode(WIFI_AP);
            WiFi.softAPConfig(ip,ip,nm);
            WiFi.softAP("wM-Buster ADV", g_settings.webui_ap_pass);
            dns.start(53,"*",ip);
        }
        server.begin();
    } else {
        dns.stop();
        server.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(has_sta ? WIFI_STA : WIFI_OFF);
        if (has_sta && WiFi.status() != WL_CONNECTED) {
            WiFi.begin(g_settings.wifi_ssid, g_settings.wifi_pass);
        }
    }
}

static void webui_task(void* pvParameters) {
    while (true) {
        if (g_settings.webui_enabled) {
            dns.processNextRequest();
            server.handleClient();
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void update_web_server() {
    // Deprecated. WebUI now runs autonomously on Core 0.
}

} // namespace wmb
#else
namespace wmb {
void set_analyze_callback(AnalyzeCallbackFn){}
void init_web_server(){}
void update_web_server(){}
}
#endif
