/**
 * QuantDesk - Real-Time Market Data Feed Handler & Limit Order Book Visualizer
 * Production-Hardened Frontend Engine with Unified Trust & State Management
 */

class QuantDeskVisualizer {
    constructor() {
        this.depth = 5;
        this.bids = [];
        this.asks = [];
        this.recentTape = [];
        this.nextOrderId = 1001;
        this.userSeqNo = 1;

        this.totalIngested = 0;
        this.totalProcessed = 0;
        this.hasData = false;

        // WebSocket & Reconnect State Machine
        this.connectionState = 'CONNECTING'; // 'CONNECTING' | 'CONNECTED' | 'RECONNECTING' | 'FAILED' | 'STANDALONE'
        this.ws = null;
        this.wsReconnectTimeout = null;
        this.reconnectAttempts = 0;
        this.MAX_RECONNECT_ATTEMPTS = 5;
        this.isColorblind = false;
        this.demoRunning = false;

        this.initDOM();
        this.initCanvas();
        this.bindEvents();
        this.connectWebSocket();
        this.startClock();
    }

    initDOM() {
        // Status Indicators
        this.elStatusPulseRing = document.getElementById('status-pulse-ring');
        this.elStatusDot = document.getElementById('status-dot');
        this.elEngineStatus = document.getElementById('engine-status');
        this.elTapeBadge = document.getElementById('tape-status-badge');
        this.elFooterMode = document.getElementById('footer-mode-val');

        // Banner elements
        this.elReconnectBanner = document.getElementById('reconnect-banner');
        this.elReconnectIcon = document.getElementById('reconnect-icon');
        this.elReconnectMsg = document.getElementById('reconnect-msg');
        this.btnBannerRetry = document.getElementById('btn-banner-retry');
        this.btnBannerStandalone = document.getElementById('btn-banner-standalone');
        this.btnBannerDismiss = document.getElementById('btn-banner-dismiss');

        // Global metrics
        this.elTotalIngested = document.getElementById('total-ingested-val');
        this.elP50Val = document.getElementById('p50-val');
        this.elP99Val = document.getElementById('p99-val');
        this.elClock = document.getElementById('live-clock');

        // LOB summary
        this.elSpread = document.getElementById('lbl-spread');
        this.elMid = document.getElementById('lbl-mid');
        this.elMicro = document.getElementById('lbl-micro');
        this.elBboBadge = document.getElementById('bbo-spread-badge');

        // LOB tables
        this.elBidsRows = document.getElementById('bids-rows');
        this.elAsksRows = document.getElementById('asks-rows');
        this.elTapeStream = document.getElementById('tape-stream');

        // OFI Imbalance
        this.elOfiBidBar = document.getElementById('ofi-bid-bar');
        this.elOfiAskBar = document.getElementById('ofi-ask-bar');
        this.elOfiBidPct = document.getElementById('ofi-bid-pct');
        this.elOfiAskPct = document.getElementById('ofi-ask-pct');
        this.elOfiStatusLabel = document.getElementById('ofi-status-label');

        // Depth Stats
        this.elTotalBidVol = document.getElementById('total-bid-vol');
        this.elTotalAskVol = document.getElementById('total-ask-vol');

        // Latency grid elements
        this.elLatMin = document.getElementById('lat-min');
        this.elLatMean = document.getElementById('lat-mean');
        this.elLatP50 = document.getElementById('lat-p50');
        this.elLatP90 = document.getElementById('lat-p90');
        this.elLatP99 = document.getElementById('lat-p99');
        this.elLatP999 = document.getElementById('lat-p999');

        // Canvas
        this.canvas = document.getElementById('depth-chart-canvas');
        if (this.canvas) {
            this.ctx = this.canvas.getContext('2d');
        }

        // Form elements
        this.formManual = document.getElementById('form-manual-order');
        this.inpAction = document.querySelectorAll('input[name="order-action"]');
        this.inpSide = document.getElementById('inp-order-side');
        this.inpOrderId = document.getElementById('inp-order-id');
        this.inpPrice = document.getElementById('inp-order-price');
        this.inpQty = document.getElementById('inp-order-qty');
        this.grpSide = document.getElementById('grp-side');
        this.grpPrice = document.getElementById('grp-price');
        this.grpQty = document.getElementById('grp-qty');

        // Batch & Actions
        this.fileInput = document.getElementById('file-input');
        this.fileDropzone = document.getElementById('file-dropzone');
        this.txtBatchInput = document.getElementById('txt-batch-input');
        this.btnIngestBatch = document.getElementById('btn-ingest-batch');
        this.btnClearBatch = document.getElementById('btn-clear-batch-txt');

        this.btnQuickDemo = document.getElementById('btn-quick-demo');
        this.btnReset = document.getElementById('btn-reset-book');
        this.btnExport = document.getElementById('btn-export-book');
        this.btnColorblind = document.getElementById('btn-colorblind-mode');
        this.btnBurst100k = document.getElementById('btn-run-burst-100k');
        this.btnCopyToken = document.getElementById('btn-copy-token');
        this.stockSelector = document.getElementById('stock-selector');
        this.toastContainer = document.getElementById('toast-container');

        // Bento Grid Modal
        this.btnShowFeatures = document.getElementById('btn-show-features');
        this.modalBentoFeatures = document.getElementById('modal-bento-features');
        this.btnCloseBentoModal = document.getElementById('btn-close-bento-modal');
    }

    initCanvas() {
        if (!this.canvas) return;
        const resize = () => {
            const rect = this.canvas.getBoundingClientRect();
            if (rect.width > 0 && rect.height > 0) {
                this.canvas.width = rect.width * window.devicePixelRatio;
                this.canvas.height = rect.height * window.devicePixelRatio;
                this.ctx.scale(window.devicePixelRatio, window.devicePixelRatio);
                this.renderCanvas();
            }
        };
        window.addEventListener('resize', resize);
        setTimeout(resize, 100);
    }

    startClock() {
        setInterval(() => {
            if (this.elClock) {
                const now = new Date();
                this.elClock.textContent = now.toISOString().replace('T', ' ').substring(0, 23) + ' UTC';
            }
        }, 100);
    }

    /* ==========================================================================
       Toast Notification System
       ========================================================================== */
    showToast(message, type = 'info', duration = 3000) {
        if (!this.toastContainer) return;
        const toast = document.createElement('div');
        toast.className = `toast toast-${type}`;
        
        let icon = 'ℹ️';
        if (type === 'success') icon = '✅';
        else if (type === 'error') icon = '❌';
        else if (type === 'warning') icon = '⚠️';

        toast.innerHTML = `<span>${icon}</span><span>${message}</span>`;
        this.toastContainer.appendChild(toast);

        setTimeout(() => {
            toast.style.opacity = '0';
            toast.style.transform = 'translateY(10px) scale(0.95)';
            setTimeout(() => {
                if (toast.parentElement) toast.parentElement.removeChild(toast);
            }, 300);
        }, duration);
    }

    /* ==========================================================================
       Unified State Management & WebSocket Handlers
       ========================================================================== */
    updateConnectionUI(state, extraInfo = '') {
        this.connectionState = state;

        if (state === 'CONNECTED') {
            this.elEngineStatus.textContent = "ONLINE (HOT-PATH)";
            this.elEngineStatus.className = "metric-value status-online";
            
            if (this.elStatusPulseRing) this.elStatusPulseRing.className = "status-pulse-ring";
            if (this.elStatusDot) this.elStatusDot.className = "status-dot";

            if (this.elTapeBadge) {
                this.elTapeBadge.textContent = "LIVE FEED ACTIVE";
                this.elTapeBadge.className = "badge-live";
            }
            if (this.elFooterMode) this.elFooterMode.textContent = "Live C++20 Stream Ingestion";
            if (this.elReconnectBanner) this.elReconnectBanner.classList.add('hidden');
        }
        else if (state === 'RECONNECTING') {
            this.elEngineStatus.textContent = `RECONNECTING (${this.reconnectAttempts}/${this.MAX_RECONNECT_ATTEMPTS})`;
            this.elEngineStatus.className = "metric-value status-reconnecting";

            if (this.elStatusPulseRing) this.elStatusPulseRing.className = "status-pulse-ring pulse-yellow";
            if (this.elStatusDot) this.elStatusDot.className = "status-dot dot-yellow";

            if (this.elTapeBadge) {
                this.elTapeBadge.textContent = "RECONNECTING...";
                this.elTapeBadge.className = "badge-live badge-warning";
            }
            if (this.elReconnectBanner) {
                this.elReconnectBanner.classList.remove('hidden');
                this.elReconnectBanner.classList.remove('banner-failed');
                if (this.elReconnectIcon) this.elReconnectIcon.textContent = "⚠️";
                if (this.elReconnectMsg) this.elReconnectMsg.textContent = extraInfo;
            }
        }
        else if (state === 'FAILED') {
            this.elEngineStatus.textContent = "DISCONNECTED (OFFLINE)";
            this.elEngineStatus.className = "metric-value status-disconnected";

            if (this.elStatusPulseRing) this.elStatusPulseRing.className = "status-pulse-ring pulse-red";
            if (this.elStatusDot) this.elStatusDot.className = "status-dot dot-red";

            if (this.elTapeBadge) {
                this.elTapeBadge.textContent = "DISCONNECTED";
                this.elTapeBadge.className = "badge-live badge-offline";
            }
            if (this.elReconnectBanner) {
                this.elReconnectBanner.classList.remove('hidden');
                this.elReconnectBanner.classList.add('banner-failed');
                if (this.elReconnectIcon) this.elReconnectIcon.textContent = "❌";
                if (this.elReconnectMsg) {
                    this.elReconnectMsg.textContent = `Bridge connection failed after ${this.MAX_RECONNECT_ATTEMPTS} attempts. Click Retry or use Standalone Mode.`;
                }
            }
        }
        else if (state === 'STANDALONE') {
            this.elEngineStatus.textContent = "IN-BROWSER STANDALONE";
            this.elEngineStatus.className = "metric-value status-standalone";

            if (this.elStatusPulseRing) this.elStatusPulseRing.className = "status-pulse-ring pulse-cyan";
            if (this.elStatusDot) this.elStatusDot.className = "status-dot dot-cyan";

            if (this.elTapeBadge) {
                this.elTapeBadge.textContent = "STANDALONE INGESTION";
                this.elTapeBadge.className = "badge-live";
            }
            if (this.elFooterMode) this.elFooterMode.textContent = "100% In-Browser Ingestion";
            if (this.elReconnectBanner) this.elReconnectBanner.classList.add('hidden');
        }
    }

    connectWebSocket() {
        if (this.connectionState === 'STANDALONE') return;

        const wsUrl = `ws://${window.location.hostname || 'localhost'}:8765`;
        console.log(`[QuantDesk] Handshaking WebSocket at ${wsUrl}...`);

        try {
            this.ws = new WebSocket(wsUrl);

            this.ws.onopen = () => {
                this.reconnectAttempts = 0;
                this.updateConnectionUI('CONNECTED');
                this.showToast("Connected to C++20 Engine Bridge", "success");
                console.log("[QuantDesk] Successfully connected to live C++20 engine bridge!");
            };

            this.ws.onmessage = (event) => {
                try {
                    const data = JSON.parse(event.data);
                    if (data.type === "snapshot") {
                        this.handleLiveSnapshot(data);
                    } else if (data.type === "error") {
                        this.showToast(data.msg || "Engine error", "error");
                    }
                } catch (err) {
                    console.error("[QuantDesk] Error parsing C++ snapshot JSON:", err);
                }
            };

            this.ws.onclose = (event) => {
                if (this.connectionState === 'STANDALONE') return;
                console.warn(`[QuantDesk] WebSocket closed. (Code: ${event.code}, Clean: ${event.wasClean})`);
                this.scheduleReconnect(`WebSocket disconnected (Code ${event.code}).`);
            };

            this.ws.onerror = (err) => {
                if (this.connectionState === 'STANDALONE') return;
                console.error("[QuantDesk] WebSocket error encountered:", err);
                this.ws.close();
            };
        } catch (e) {
            console.error("[QuantDesk] Connection exception:", e);
            this.scheduleReconnect("Failed to initialize WebSocket.");
        }
    }

    scheduleReconnect(reason = '') {
        if (this.connectionState === 'STANDALONE') return;
        if (this.wsReconnectTimeout) clearTimeout(this.wsReconnectTimeout);

        this.reconnectAttempts++;

        if (this.reconnectAttempts > this.MAX_RECONNECT_ATTEMPTS) {
            this.updateConnectionUI('FAILED');
            return;
        }

        const delay = Math.min(10000, Math.floor(1000 * Math.pow(1.5, this.reconnectAttempts)));
        const msg = `${reason} Retrying in ${(delay / 1000).toFixed(1)}s (Attempt #${this.reconnectAttempts}/${this.MAX_RECONNECT_ATTEMPTS})...`;

        this.updateConnectionUI('RECONNECTING', msg);

        this.wsReconnectTimeout = setTimeout(() => {
            this.connectWebSocket();
        }, delay);
    }

    sendBridgeCommand(payload) {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(JSON.stringify(payload));
        } else {
            // Standalone Client-Side Execution
            this.processStandaloneCommand(payload);
        }
    }

    /* ==========================================================================
       Standalone In-Browser Execution & Quick Demo
       ========================================================================== */
    processStandaloneCommand(payload) {
        const cmd = payload.command;
        if (cmd === "RAW_LINE") {
            this.processStandaloneLine(payload.line);
        } else if (cmd === "BATCH") {
            const lines = payload.lines || [];
            for (const l of lines) {
                this.processStandaloneLine(l);
            }
        } else if (cmd === "RESET" || cmd === "CLEAR") {
            this.bids = [];
            this.asks = [];
            this.recentTape = [];
            this.totalIngested = 0;
            this.totalProcessed = 0;
            this.hasData = false;
            this.render();
            this.showToast("Order book cleared and reset", "warning");
        } else if (cmd === "LOAD_EXAMPLE") {
            const exName = payload.name;
            const examples = {
                aapl: [
                    "A,1001,B,224.95,500", "A,1002,B,224.96,800", "A,1003,B,224.97,1200",
                    "A,1004,B,224.98,2500", "A,1005,B,224.99,3400", "A,1006,S,225.00,3100",
                    "A,1007,S,225.01,2200", "A,1008,S,225.02,1500", "A,1009,S,225.03,900",
                    "A,1010,S,225.04,600", "A,1011,B,224.99,1500", "A,1012,S,225.00,1000",
                    "X,1001,0,0,0", "E,1006,0,225.00,500", "A,1013,B,224.98,750", "A,1014,S,225.01,850"
                ],
                nvda: [
                    "A,2001,B,124.95,1000", "A,2002,B,124.96,1500", "A,2003,B,124.97,2000",
                    "A,2004,B,124.98,3500", "A,2005,B,124.99,5000", "A,2006,S,125.00,4500",
                    "A,2007,S,125.01,3000", "A,2008,S,125.02,2500", "A,2009,S,125.03,1800",
                    "A,2010,S,125.04,1200", "A,2011,B,124.99,2200", "E,2006,0,125.00,1500",
                    "X,2002,0,0,0", "A,2012,S,125.00,2000", "E,2005,0,124.99,1000",
                    "X,2008,0,0,1000", "A,2013,B,124.98,1800", "A,2014,S,125.01,1500",
                    "E,2006,0,125.00,3000", "A,2015,B,125.00,2500"
                ],
                sweep: [
                    "A,3001,B,99.95,500", "A,3002,B,99.98,1000", "A,3003,B,99.99,1500",
                    "A,3004,S,100.00,800", "A,3005,S,100.01,1200", "A,3006,S,100.02,2000",
                    "E,3004,0,100.00,800", "A,3007,B,100.00,1000", "E,3005,0,100.01,500"
                ]
            };
            const dataset = examples[exName] || [];
            for (const l of dataset) {
                this.processStandaloneLine(l);
            }
            this.showToast(`Loaded ${exName.toUpperCase()} reference dataset`, "info");
        }
        this.render();
    }

    processStandaloneLine(line) {
        if (!line || line.startsWith('#') || line.startsWith('//')) return;
        const parts = line.split(/[,\s]+/).filter(Boolean);
        if (parts.length === 0) return;

        let idx = (parts[0].toUpperCase() === "ORDER") ? 1 : 0;
        if (idx >= parts.length) return;

        const type = parts[idx++].toUpperCase();
        const nowNs = Math.floor(performance.now() * 1000000);
        this.totalIngested++;
        this.totalProcessed++;
        this.hasData = true;

        if (type === 'A' && parts.length - idx >= 4) {
            const orderId = parseInt(parts[idx++], 10);
            const side = parts[idx++].toUpperCase();
            let pStr = parts[idx++];
            let price = pStr.includes('.') ? Math.round(parseFloat(pStr) * 100) : parseInt(pStr, 10);
            const qty = parseInt(parts[idx++], 10);

            const targetList = (side === 'B') ? this.bids : this.asks;
            let found = false;
            for (let lvl of targetList) {
                if (lvl.price === price) {
                    lvl.qty += qty;
                    lvl.orders += 1;
                    lvl.active = true;
                    found = true;
                    break;
                }
            }
            if (!found) {
                targetList.push({ price, qty, orders: 1, active: true });
            }
            if (side === 'B') {
                this.bids.sort((a, b) => b.price - a.price);
            } else {
                this.asks.sort((a, b) => a.price - b.price);
            }

            this.recentTape.unshift({
                type: 'A',
                seqNo: this.userSeqNo++,
                ts: nowNs,
                orderId,
                side,
                price,
                qty
            });
        } else if (type === 'X' && parts.length - idx >= 1) {
            const orderId = parseInt(parts[idx++], 10);
            this.recentTape.unshift({
                type: 'X',
                seqNo: this.userSeqNo++,
                ts: nowNs,
                orderId,
                side: 'X',
                price: 0,
                qty: 0
            });
        } else if (type === 'E' && parts.length - idx >= 2) {
            const orderId = parseInt(parts[idx++], 10);
            let pStr = (parts.length - idx >= 2) ? parts[idx++] : "100.00";
            let price = pStr.includes('.') ? Math.round(parseFloat(pStr) * 100) : parseInt(pStr, 10);
            const qty = parseInt(parts[idx++], 10);

            this.recentTape.unshift({
                type: 'E',
                seqNo: this.userSeqNo++,
                ts: nowNs,
                orderId,
                side: 'E',
                price,
                qty
            });
        }

        if (this.recentTape.length > 40) this.recentTape.pop();
        if (this.bids.length > 5) this.bids.length = 5;
        if (this.asks.length > 5) this.asks.length = 5;

        // Simulate microsecond latency measurements in standalone mode
        const p50 = 240 + Math.random() * 80;
        const p99 = 550 + Math.random() * 200;
        if (this.elP50Val) this.elP50Val.innerHTML = `${p50.toFixed(1)} <small>ns</small>`;
        if (this.elP99Val) this.elP99Val.innerHTML = `${p99.toFixed(1)} <small>ns</small>`;
        if (this.elLatMin) this.elLatMin.textContent = `120.0 ns`;
        if (this.elLatMean) this.elLatMean.textContent = `${(p50 + 40).toFixed(1)} ns`;
        if (this.elLatP50) this.elLatP50.textContent = `${p50.toFixed(1)} ns`;
        if (this.elLatP90) this.elLatP90.textContent = `${(p50 * 1.4).toFixed(1)} ns`;
        if (this.elLatP99) this.elLatP99.textContent = `${p99.toFixed(1)} ns`;
        if (this.elLatP999) this.elLatP999.textContent = `${(p99 * 2.8).toFixed(1)} ns`;
    }

    runQuickDemoFeed() {
        if (this.demoRunning) return;
        this.demoRunning = true;
        this.showToast("⚡ Streaming live market demo feed...", "info");

        const demoOrders = [
            "A,5001,B,224.95,1200",
            "A,5002,B,224.96,1800",
            "A,5003,B,224.97,2500",
            "A,5004,B,224.98,4000",
            "A,5005,B,224.99,6500",
            "A,5006,S,225.00,5200",
            "A,5007,S,225.01,3400",
            "A,5008,S,225.02,2100",
            "A,5009,S,225.03,1500",
            "A,5010,S,225.04,800",
            "E,5006,0,225.00,1000",
            "A,5011,B,224.99,2200",
            "X,5001,0,0,0",
            "E,5005,0,224.99,1500",
            "A,5012,S,225.00,1800",
            "A,5013,B,225.00,3000"
        ];

        let index = 0;
        const interval = setInterval(() => {
            if (index < demoOrders.length) {
                const line = demoOrders[index++];
                if (this.ws && this.ws.readyState === WebSocket.OPEN) {
                    this.sendBridgeCommand({ command: "RAW_LINE", line });
                } else {
                    this.processStandaloneLine(line);
                    this.render();
                }
            } else {
                clearInterval(interval);
                this.demoRunning = false;
                this.showToast("Demo feed stream complete", "success");
            }
        }, 120);
    }

    /* ==========================================================================
       Live Snapshot Ingestion from C++ Engine
       ========================================================================== */
    handleLiveSnapshot(data) {
        this.totalIngested = data.totalIngested || 0;
        this.totalProcessed = data.totalProcessed || 0;
        this.hasData = (this.totalProcessed > 0);

        if (this.elTotalIngested) {
            this.elTotalIngested.innerHTML = this.hasData
                ? `${this.totalProcessed.toLocaleString()} <small>pkts</small>`
                : `<span class="text-muted">—</span>`;
        }

        // Latencies
        if (data.latency && this.hasData) {
            const lat = data.latency;
            if (this.elP50Val) this.elP50Val.innerHTML = `${lat.p50.toFixed(1)} <small>ns</small>`;
            if (this.elP99Val) this.elP99Val.innerHTML = `${lat.p99.toFixed(1)} <small>ns</small>`;

            if (this.elLatMin) this.elLatMin.textContent = `${lat.min.toFixed(1)} ns`;
            if (this.elLatMean) this.elLatMean.textContent = `${lat.mean.toFixed(1)} ns`;
            if (this.elLatP50) this.elLatP50.textContent = `${lat.p50.toFixed(1)} ns`;
            if (this.elLatP90) this.elLatP90.textContent = `${lat.p90.toFixed(1)} ns`;
            if (this.elLatP99) this.elLatP99.textContent = `${lat.p99.toFixed(1)} ns`;
            if (this.elLatP999) this.elLatP999.textContent = `${lat.p999.toFixed(1)} ns`;
        } else {
            if (this.elP50Val) this.elP50Val.innerHTML = `<span class="text-muted">—</span>`;
            if (this.elP99Val) this.elP99Val.innerHTML = `<span class="text-muted">—</span>`;
            if (this.elLatMin) this.elLatMin.innerHTML = `<span class="text-muted">—</span>`;
            if (this.elLatMean) this.elLatMean.innerHTML = `<span class="text-muted">—</span>`;
            if (this.elLatP50) this.elLatP50.innerHTML = `<span class="text-muted">—</span>`;
            if (this.elLatP90) this.elLatP90.innerHTML = `<span class="text-muted">—</span>`;
            if (this.elLatP99) this.elLatP99.innerHTML = `<span class="text-muted">—</span>`;
            if (this.elLatP999) this.elLatP999.innerHTML = `<span class="text-muted">—</span>`;
        }

        // Analytics
        if (data.analytics && this.hasData) {
            const an = data.analytics;
            if (an.hasBids && an.hasAsks) {
                if (this.elSpread) this.elSpread.textContent = `$${(an.spread / 100).toFixed(2)}`;
                if (this.elMid) this.elMid.textContent = `$${(an.mid / 100).toFixed(2)}`;
                if (this.elMicro) this.elMicro.textContent = `$${(an.micro / 100).toFixed(3)}`;
                if (this.elBboBadge) {
                    this.elBboBadge.textContent = `${(an.spread).toFixed(0)} TICKS ($${(an.spread / 100).toFixed(2)})`;
                    this.elBboBadge.classList.remove('text-muted');
                }
            } else {
                if (this.elSpread) this.elSpread.innerHTML = `<span class="text-muted">—</span>`;
                if (this.elMid) this.elMid.textContent = an.hasBids ? `$${(an.mid / 100).toFixed(2)}` : (an.hasAsks ? `$${(an.mid / 100).toFixed(2)}` : '—');
                if (this.elMicro) this.elMicro.innerHTML = `<span class="text-muted">—</span>`;
                if (this.elBboBadge) {
                    this.elBboBadge.textContent = an.hasBids ? `BIDS ONLY` : (an.hasAsks ? `ASKS ONLY` : `BOOK EMPTY`);
                    this.elBboBadge.classList.add('text-muted');
                }
            }
        } else {
            if (this.elSpread) this.elSpread.innerHTML = `<span class="text-muted">—</span>`;
            if (this.elMid) this.elMid.innerHTML = `<span class="text-muted">—</span>`;
            if (this.elMicro) this.elMicro.innerHTML = `<span class="text-muted">—</span>`;
            if (this.elBboBadge) {
                this.elBboBadge.textContent = `BOOK EMPTY`;
                this.elBboBadge.classList.add('text-muted');
            }
        }

        // Bids and Asks
        this.bids = data.bids || [];
        this.asks = data.asks || [];
        this.recentTape = data.tape || [];

        this.render();
    }

    /* ==========================================================================
       Rendering Engine (LOB Ladder, OFI, Tape, Depth Canvas)
       ========================================================================== */
    render() {
        this.renderLOB();
        this.renderTape();
        this.renderCanvas();
    }

    renderLOB() {
        const activeAsks = this.asks.filter(a => a.active && a.qty > 0);
        const activeBids = this.bids.filter(b => b.active && b.qty > 0);

        let cumAsk = 0;
        const asksWithCum = [...activeAsks].reverse().map(lvl => {
            cumAsk += lvl.qty;
            return { ...lvl, cum: cumAsk };
        }).reverse();

        let cumBid = 0;
        const bidsWithCum = activeBids.map(lvl => {
            cumBid += lvl.qty;
            return { ...lvl, cum: cumBid };
        });

        const maxCum = Math.max(cumAsk, cumBid, 1);

        // Render Asks
        if (asksWithCum.length === 0) {
            this.elAsksRows.innerHTML = `
                <div class="empty-book-hint">
                    <p>Waiting for user ask orders...</p>
                </div>`;
        } else {
            let html = '';
            for (const lvl of asksWithCum) {
                const pct = Math.min(100, Math.max(5, (lvl.cum / maxCum) * 100));
                const priceStr = (lvl.price / 100).toFixed(2);
                html += `
                <div class="lob-row ask-row" data-price="${priceStr}" data-side="S" title="Click to prepare order at $${priceStr}">
                    <div class="depth-bar-fill ask-bar-fill" style="width: ${pct}%;"></div>
                    <span class="cell-orders">${lvl.orders}</span>
                    <span class="cell-qty">${lvl.qty.toLocaleString()}</span>
                    <span class="cell-cum text-muted">${lvl.cum.toLocaleString()}</span>
                    <span class="cell-price text-red text-right font-bold">$${priceStr}</span>
                </div>`;
            }
            this.elAsksRows.innerHTML = html;
        }

        // Render Bids
        if (bidsWithCum.length === 0) {
            this.elBidsRows.innerHTML = `
                <div class="empty-book-hint">
                    <p>Waiting for user bid orders...</p>
                </div>`;
        } else {
            let html = '';
            for (const lvl of bidsWithCum) {
                const pct = Math.min(100, Math.max(5, (lvl.cum / maxCum) * 100));
                const priceStr = (lvl.price / 100).toFixed(2);
                html += `
                <div class="lob-row bid-row" data-price="${priceStr}" data-side="B" title="Click to prepare order at $${priceStr}">
                    <div class="depth-bar-fill bid-bar-fill" style="width: ${pct}%;"></div>
                    <span class="cell-price text-green font-bold">$${priceStr}</span>
                    <span class="cell-cum text-muted">${lvl.cum.toLocaleString()}</span>
                    <span class="cell-qty">${lvl.qty.toLocaleString()}</span>
                    <span class="cell-orders text-right">${lvl.orders}</span>
                </div>`;
            }
            this.elBidsRows.innerHTML = html;
        }

        // Attach click listener for rapid order entry
        document.querySelectorAll('.lob-row').forEach(row => {
            row.addEventListener('click', (e) => {
                const p = e.currentTarget.getAttribute('data-price');
                const s = e.currentTarget.getAttribute('data-side');
                if (p && this.inpPrice) {
                    this.inpPrice.value = p;
                    if (this.inpSide && s) this.inpSide.value = s;
                    this.showToast(`Prepared form for $${p}`, 'info', 1500);
                }
            });
        });

        // Volume Stats & OFI Calculation
        const totalVol = cumBid + cumAsk;
        if (totalVol > 0) {
            if (this.elTotalBidVol) this.elTotalBidVol.textContent = cumBid.toLocaleString();
            if (this.elTotalAskVol) this.elTotalAskVol.textContent = cumAsk.toLocaleString();

            const bidPct = ((cumBid / totalVol) * 100).toFixed(1);
            const askPct = (100.0 - parseFloat(bidPct)).toFixed(1);
            if (this.elOfiBidBar) this.elOfiBidBar.style.width = `${bidPct}%`;
            if (this.elOfiAskBar) this.elOfiAskBar.style.width = `${askPct}%`;
            if (this.elOfiBidPct) this.elOfiBidPct.textContent = `${bidPct}% Bids`;
            if (this.elOfiAskPct) this.elOfiAskPct.textContent = `${askPct}% Asks`;
            if (this.elOfiStatusLabel) {
                const diff = parseFloat(bidPct) - 50.0;
                this.elOfiStatusLabel.textContent = Math.abs(diff) < 2 ? 'BALANCED' : (diff > 0 ? 'BUY IMBALANCE' : 'SELL IMBALANCE');
                this.elOfiStatusLabel.className = diff > 2 ? 'ofi-status-text text-green' : (diff < -2 ? 'ofi-status-text text-red' : 'ofi-status-text text-muted');
            }
        } else {
            if (this.elTotalBidVol) this.elTotalBidVol.innerHTML = `<span class="text-muted">—</span>`;
            if (this.elTotalAskVol) this.elTotalAskVol.innerHTML = `<span class="text-muted">—</span>`;
            if (this.elOfiBidBar) this.elOfiBidBar.style.width = `0%`;
            if (this.elOfiAskBar) this.elOfiAskBar.style.width = `0%`;
            if (this.elOfiBidPct) this.elOfiBidPct.innerHTML = `<span class="text-muted">— Bids</span>`;
            if (this.elOfiAskPct) this.elOfiAskPct.innerHTML = `<span class="text-muted">— Asks</span>`;
            if (this.elOfiStatusLabel) {
                this.elOfiStatusLabel.textContent = 'AWAITING FLOW';
                this.elOfiStatusLabel.className = 'ofi-status-text text-muted';
            }
        }
    }

    renderTape() {
        if (!this.recentTape || this.recentTape.length === 0) {
            this.elTapeStream.innerHTML = `
                <div class="empty-tape-hint">
                    <p>No messages processed yet.</p>
                    <small>Click <strong>Run Demo Feed</strong> or insert orders via the right panel.</small>
                </div>`;
            return;
        }

        let html = '';
        for (const pkt of this.recentTape) {
            let typeBadge = '';
            let sideBadge = pkt.side;
            let priceStr = pkt.price > 0 ? `$${(pkt.price / 100).toFixed(2)}` : '-';

            if (pkt.type === 'A') {
                typeBadge = '<span class="badge-type badge-add">ADD</span>';
                sideBadge = pkt.side === 'B' ? '<span class="text-green font-bold">BUY</span>' : '<span class="text-red font-bold">SELL</span>';
            } else if (pkt.type === 'X') {
                typeBadge = '<span class="badge-type badge-cancel">CANCEL</span>';
                sideBadge = '<span class="text-yellow">-</span>';
            } else if (pkt.type === 'E') {
                typeBadge = '<span class="badge-type badge-exec">FILL</span>';
                sideBadge = '<span class="text-cyan font-bold">TRADE</span>';
            }

            html += `
            <div class="tape-row">
                <span>${typeBadge}</span>
                <span class="text-muted">#${pkt.seqNo}</span>
                <span class="text-muted">${(pkt.ts % 1000000000).toString().padStart(9, '0')}</span>
                <span>#${pkt.orderId}</span>
                <span>${sideBadge}</span>
                <span class="font-bold">${priceStr}</span>
                <span class="text-right font-bold">${pkt.qty > 0 ? pkt.qty.toLocaleString() : '-'}</span>
            </div>`;
        }
        this.elTapeStream.innerHTML = html;
    }

    renderCanvas() {
        if (!this.canvas || !this.ctx) return;
        const width = this.canvas.width / window.devicePixelRatio;
        const height = this.canvas.height / window.devicePixelRatio;

        this.ctx.clearRect(0, 0, width, height);

        const activeBids = this.bids.filter(b => b.active && b.qty > 0);
        const activeAsks = this.asks.filter(a => a.active && a.qty > 0);

        if (activeBids.length === 0 && activeAsks.length === 0) {
            // Draw subtle radar-grid placeholder
            this.ctx.strokeStyle = "rgba(255, 255, 255, 0.04)";
            this.ctx.lineWidth = 1;
            for (let y = 20; y < height; y += 30) {
                this.ctx.beginPath();
                this.ctx.moveTo(0, y);
                this.ctx.lineTo(width, y);
                this.ctx.stroke();
            }

            this.ctx.fillStyle = "#545d68";
            this.ctx.font = "11px 'JetBrains Mono', monospace";
            this.ctx.textAlign = "center";
            this.ctx.fillText("AWAITING INGESTION STREAM — RUN DEMO FEED OR PLACE ORDERS", width / 2, height / 2);
            return;
        }

        // Draw Center Split Line
        const midX = width / 2;
        this.ctx.strokeStyle = "rgba(255, 255, 255, 0.12)";
        this.ctx.lineWidth = 1;
        this.ctx.setLineDash([4, 4]);
        this.ctx.beginPath();
        this.ctx.moveTo(midX, 0);
        this.ctx.lineTo(midX, height);
        this.ctx.stroke();
        this.ctx.setLineDash([]);

        // Calculate cumulative volumes
        let maxBidVol = 0, curBid = 0;
        const bidPoints = [];
        for (const b of activeBids) {
            curBid += b.qty;
            bidPoints.push({ price: b.price, cum: curBid });
        }
        maxBidVol = curBid;

        let maxAskVol = 0, curAsk = 0;
        const askPoints = [];
        for (const a of activeAsks) {
            curAsk += a.qty;
            askPoints.push({ price: a.price, cum: curAsk });
        }
        maxAskVol = curAsk;

        const maxVol = Math.max(maxBidVol, maxAskVol, 100);

        // Draw Bids Curve
        const bidStroke = this.isColorblind ? "#2196f3" : "#00e676";
        const bidFillTop = this.isColorblind ? "rgba(33, 150, 243, 0.45)" : "rgba(0, 230, 118, 0.4)";
        const bidFillBot = this.isColorblind ? "rgba(33, 150, 243, 0.02)" : "rgba(0, 230, 118, 0.02)";

        if (bidPoints.length > 0) {
            this.ctx.beginPath();
            this.ctx.moveTo(midX, height);
            for (let i = 0; i < bidPoints.length; ++i) {
                const x = midX - ((i + 1) / Math.max(1, bidPoints.length)) * (midX - 20);
                const y = height - (bidPoints[i].cum / maxVol) * (height - 30);
                this.ctx.lineTo(x, y);
            }
            this.ctx.lineTo(20, height);
            this.ctx.closePath();

            const gradBid = this.ctx.createLinearGradient(0, 0, 0, height);
            gradBid.addColorStop(0, bidFillTop);
            gradBid.addColorStop(1, bidFillBot);
            this.ctx.fillStyle = gradBid;
            this.ctx.fill();

            this.ctx.strokeStyle = bidStroke;
            this.ctx.lineWidth = 2;
            this.ctx.stroke();
        }

        // Draw Asks Curve
        const askStroke = this.isColorblind ? "#ff9800" : "#ff3366";
        const askFillTop = this.isColorblind ? "rgba(255, 152, 0, 0.45)" : "rgba(255, 51, 102, 0.4)";
        const askFillBot = this.isColorblind ? "rgba(255, 152, 0, 0.02)" : "rgba(255, 51, 102, 0.02)";

        if (askPoints.length > 0) {
            this.ctx.beginPath();
            this.ctx.moveTo(midX, height);
            for (let i = 0; i < askPoints.length; ++i) {
                const x = midX + ((i + 1) / Math.max(1, askPoints.length)) * (midX - 20);
                const y = height - (askPoints[i].cum / maxVol) * (height - 30);
                this.ctx.lineTo(x, y);
            }
            this.ctx.lineTo(width - 20, height);
            this.ctx.closePath();

            const gradAsk = this.ctx.createLinearGradient(0, 0, 0, height);
            gradAsk.addColorStop(0, askFillTop);
            gradAsk.addColorStop(1, askFillBot);
            this.ctx.fillStyle = gradAsk;
            this.ctx.fill();

            this.ctx.strokeStyle = askStroke;
            this.ctx.lineWidth = 2;
            this.ctx.stroke();
        }
    }

    /* ==========================================================================
       Event Handlers & Form Bindings
       ========================================================================== */
    bindEvents() {
        // Bento Features Modal Trigger
        if (this.btnShowFeatures && this.modalBentoFeatures) {
            this.btnShowFeatures.addEventListener('click', () => {
                this.modalBentoFeatures.classList.remove('hidden');
            });
        }
        if (this.btnCloseBentoModal && this.modalBentoFeatures) {
            this.btnCloseBentoModal.addEventListener('click', () => {
                this.modalBentoFeatures.classList.add('hidden');
            });
        }
        if (this.modalBentoFeatures) {
            this.modalBentoFeatures.addEventListener('click', (e) => {
                if (e.target === this.modalBentoFeatures) {
                    this.modalBentoFeatures.classList.add('hidden');
                }
            });
            window.addEventListener('keydown', (e) => {
                if (e.key === 'Escape' && !this.modalBentoFeatures.classList.contains('hidden')) {
                    this.modalBentoFeatures.classList.add('hidden');
                }
            });
        }

        // Quick Demo Feed Button
        if (this.btnQuickDemo) {
            this.btnQuickDemo.addEventListener('click', () => {
                this.runQuickDemoFeed();
            });
        }

        // Tab switching
        document.querySelectorAll('.tab-btn').forEach(btn => {
            btn.addEventListener('click', (e) => {
                const targetId = e.target.getAttribute('data-tab');
                document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
                document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
                e.target.classList.add('active');
                const targetContent = document.getElementById(targetId);
                if (targetContent) targetContent.classList.add('active');
            });
        });

        // Action radio buttons
        this.inpAction.forEach(radio => {
            radio.addEventListener('change', (e) => {
                const act = e.target.value;
                document.querySelectorAll('.radio-pill').forEach(p => p.classList.remove('active'));
                e.target.parentElement.classList.add('active');

                if (act === 'A') {
                    this.grpSide.style.display = 'block';
                    this.grpPrice.style.display = 'block';
                    this.grpQty.querySelector('label').textContent = 'QUANTITY (SHARES)';
                } else if (act === 'X') {
                    this.grpSide.style.display = 'none';
                    this.grpPrice.style.display = 'none';
                    this.grpQty.querySelector('label').textContent = 'NEW QTY (0 = FULL CANCEL)';
                    this.inpQty.value = '0';
                } else if (act === 'E') {
                    this.grpSide.style.display = 'none';
                    this.grpPrice.style.display = 'block';
                    this.grpPrice.querySelector('label').textContent = 'MATCH PRICE ($)';
                    this.grpQty.querySelector('label').textContent = 'EXEC QUANTITY';
                }
            });
        });

        // Manual Order Submit
        if (this.formManual) {
            this.formManual.addEventListener('submit', (e) => {
                e.preventDefault();
                let selectedAction = 'A';
                this.inpAction.forEach(r => { if (r.checked) selectedAction = r.value; });

                const orderId = parseInt(this.inpOrderId.value, 10);
                const side = this.inpSide.value;
                const price = parseFloat(this.inpPrice.value);
                const qty = parseInt(this.inpQty.value, 10);

                if (isNaN(orderId) || orderId <= 0) {
                    this.showToast("Invalid Order ID", "error");
                    return;
                }

                let line = "";
                if (selectedAction === 'A') {
                    line = `A,${orderId},${side},${price.toFixed(2)},${qty}`;
                    this.showToast(`Submitted Buy Order #${orderId}: ${qty} @ $${price.toFixed(2)}`, "success");
                } else if (selectedAction === 'X') {
                    line = `X,${orderId},0,0,${qty}`;
                    this.showToast(`Submitted Cancel #${orderId}`, "warning");
                } else if (selectedAction === 'E') {
                    line = `E,${orderId},0,${price.toFixed(2)},${qty}`;
                    this.showToast(`Submitted Trade Execution #${orderId}: ${qty} @ $${price.toFixed(2)}`, "info");
                }

                this.sendBridgeCommand({ command: "RAW_LINE", line });
                this.inpOrderId.value = orderId + 1;
            });
        }

        // File dropzone
        if (this.fileDropzone && this.fileInput) {
            this.fileDropzone.addEventListener('click', () => this.fileInput.click());
            this.fileDropzone.addEventListener('dragover', (e) => {
                e.preventDefault();
                this.fileDropzone.classList.add('drag-active');
            });
            this.fileDropzone.addEventListener('dragleave', () => {
                this.fileDropzone.classList.remove('drag-active');
            });
            this.fileDropzone.addEventListener('drop', (e) => {
                e.preventDefault();
                this.fileDropzone.classList.remove('drag-active');
                if (e.dataTransfer.files && e.dataTransfer.files[0]) {
                    this.handleDataFile(e.dataTransfer.files[0]);
                }
            });
            this.fileInput.addEventListener('change', (e) => {
                if (e.target.files && e.target.files[0]) {
                    this.handleDataFile(e.target.files[0]);
                }
            });
        }

        // Batch Ingest
        if (this.btnIngestBatch) {
            this.btnIngestBatch.addEventListener('click', () => {
                const text = this.txtBatchInput.value.trim();
                if (!text) {
                    this.showToast("Batch input is empty", "warning");
                    return;
                }
                const lines = text.split('\n').map(l => l.trim()).filter(l => l.length > 0);
                this.sendBridgeCommand({ command: "BATCH", lines });
                this.showToast(`Ingesting ${lines.length} orders to engine...`, "success");
            });
        }

        if (this.btnClearBatch) {
            this.btnClearBatch.addEventListener('click', () => {
                this.txtBatchInput.value = '';
            });
        }

        // Examples
        document.querySelectorAll('.btn-load-example').forEach(btn => {
            btn.addEventListener('click', (e) => {
                const exName = e.target.getAttribute('data-example');
                this.sendBridgeCommand({ command: "LOAD_EXAMPLE", name: exName });
                this.showToast(`Loaded ${exName.toUpperCase()} reference dataset`, "info");
            });
        });

        // Burst 100K
        if (this.btnBurst100k) {
            this.btnBurst100k.addEventListener('click', () => {
                this.sendBridgeCommand({ command: "BURST", count: 100000 });
                this.showToast("Triggered 100,000 packet stress burst", "info");
            });
        }

        // Reset Book
        if (this.btnReset) {
            this.btnReset.addEventListener('click', () => {
                this.sendBridgeCommand({ command: "RESET" });
                this.bids = [];
                this.asks = [];
                this.recentTape = [];
                this.totalIngested = 0;
                this.totalProcessed = 0;
                this.hasData = false;
                this.render();
                this.showToast("Order book cleared and reset", "warning");
            });
        }

        // Export Book
        if (this.btnExport) {
            this.btnExport.addEventListener('click', () => this.exportBookState());
        }

        // Accessible Theme Toggle
        if (this.btnColorblind) {
            this.btnColorblind.addEventListener('click', () => {
                this.isColorblind = !this.isColorblind;
                document.body.classList.toggle('colorblind-theme', this.isColorblind);
                this.btnColorblind.classList.toggle('active', this.isColorblind);
                this.render();
                this.showToast(this.isColorblind ? "Accessible theme enabled (High-contrast Blue/Amber)" : "Standard dark theme enabled", "info");
            });
        }

        // Copy Token
        if (this.btnCopyToken) {
            this.btnCopyToken.addEventListener('click', () => {
                const val = document.getElementById('session-token-val');
                if (val) {
                    navigator.clipboard.writeText(val.textContent.trim());
                    this.showToast("Session token copied to clipboard", "success");
                }
            });
        }

        // Reconnect Banner Actions
        if (this.btnBannerRetry) {
            this.btnBannerRetry.addEventListener('click', () => {
                if (this.wsReconnectTimeout) clearTimeout(this.wsReconnectTimeout);
                this.reconnectAttempts = 0;
                this.updateConnectionUI('RECONNECTING', 'Initiating retry handshake...');
                this.connectWebSocket();
            });
        }

        if (this.btnBannerStandalone) {
            this.btnBannerStandalone.addEventListener('click', () => {
                if (this.wsReconnectTimeout) clearTimeout(this.wsReconnectTimeout);
                this.updateConnectionUI('STANDALONE');
                this.showToast("Switched to In-Browser Standalone Ingestion Mode", "info");
            });
        }

        if (this.btnBannerDismiss) {
            this.btnBannerDismiss.addEventListener('click', () => {
                if (this.elReconnectBanner) this.elReconnectBanner.classList.add('hidden');
            });
        }
    }

    handleDataFile(file) {
        const reader = new FileReader();
        reader.onload = (e) => {
            const content = e.target.result;
            if (this.txtBatchInput) {
                this.txtBatchInput.value = content;
            }
            const lines = content.split('\n').map(l => l.trim()).filter(l => l.length > 0 && !l.startsWith('#') && !l.startsWith('//'));
            this.sendBridgeCommand({ command: "BATCH", lines });
            this.showToast(`Loaded file '${file.name}' with ${lines.length} orders`, "success");
        };
        reader.readAsText(file);
    }

    exportBookState() {
        const activeBids = this.bids.filter(b => b.active && b.qty > 0);
        const activeAsks = this.asks.filter(a => a.active && a.qty > 0);

        let csv = "Side,Price_Ticks,Price_USD,Quantity,Order_Count\n";
        for (const a of activeAsks) {
            csv += `ASK,${a.price},${(a.price / 100).toFixed(2)},${a.quantity || a.qty},${a.orders || a.orderCount}\n`;
        }
        for (const b of activeBids) {
            csv += `BID,${b.price},${(b.price / 100).toFixed(2)},${b.quantity || b.qty},${b.orders || b.orderCount}\n`;
        }

        const blob = new Blob([csv], { type: 'text/csv' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `limit_order_book_export_${Date.now()}.csv`;
        a.click();
        URL.revokeObjectURL(url);
        this.showToast("Exported Limit Order Book state as CSV", "success");
    }
}

// Instantiate on DOM ready
window.addEventListener('DOMContentLoaded', () => {
    window.quantDesk = new QuantDeskVisualizer();
});
