/**
 * QuantDesk - Real-Time Market Data Feed Handler & Limit Order Book Visualizer
 * 100% User Data Ingestion Driver (Connected to C++20 Core via WebSocket)
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
        this.isLiveConnected = false;
        this.ws = null;
        this.wsReconnectTimeout = null;

        this.initDOM();
        this.initCanvas();
        this.bindEvents();
        this.connectWebSocket();
        this.startClock();
    }

    initDOM() {
        this.elBidsRows = document.getElementById('bids-rows');
        this.elAsksRows = document.getElementById('asks-rows');
        this.elTapeStream = document.getElementById('tape-stream');
        this.elTapeBadge = document.getElementById('tape-status-badge');

        this.elSpread = document.getElementById('lbl-spread');
        this.elMid = document.getElementById('lbl-mid');
        this.elMicro = document.getElementById('lbl-micro');
        this.elBboBadge = document.getElementById('bbo-spread-badge');

        this.elOfiBidBar = document.getElementById('ofi-bid-bar');
        this.elOfiAskBar = document.getElementById('ofi-ask-bar');
        this.elOfiBidPct = document.getElementById('ofi-bid-pct');
        this.elOfiAskPct = document.getElementById('ofi-ask-pct');

        this.elTotalBidVol = document.getElementById('total-bid-vol');
        this.elTotalAskVol = document.getElementById('total-ask-vol');

        this.elEngineStatus = document.getElementById('engine-status');
        this.elTotalIngested = document.getElementById('total-ingested-val');
        this.elP50Val = document.getElementById('p50-val');
        this.elP99Val = document.getElementById('p99-val');
        this.elClock = document.getElementById('live-clock');

        // Latency grid elements
        this.elLatMin = document.getElementById('lat-min');
        this.elLatMean = document.getElementById('lat-mean');
        this.elLatP50 = document.getElementById('lat-p50');
        this.elLatP90 = document.getElementById('lat-p90');
        this.elLatP99 = document.getElementById('lat-p99');
        this.elLatP999 = document.getElementById('lat-p999');

        // Canvas
        this.canvas = document.getElementById('depth-chart-canvas');
        this.ctx = this.canvas.getContext('2d');

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

        // Batch / File elements
        this.fileInput = document.getElementById('file-input');
        this.fileDropzone = document.getElementById('file-dropzone');
        this.txtBatchInput = document.getElementById('txt-batch-input');
        this.btnIngestBatch = document.getElementById('btn-ingest-batch');
        this.btnClearBatch = document.getElementById('btn-clear-batch-txt');

        // Actions
        this.btnReset = document.getElementById('btn-reset-book');
        this.btnExport = document.getElementById('btn-export-book');
        this.btnBurst100k = document.getElementById('btn-run-burst-100k');
    }

    initCanvas() {
        const resize = () => {
            const rect = this.canvas.getBoundingClientRect();
            this.canvas.width = rect.width * window.devicePixelRatio;
            this.canvas.height = rect.height * window.devicePixelRatio;
            this.ctx.scale(window.devicePixelRatio, window.devicePixelRatio);
            this.renderCanvas();
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

    connectWebSocket() {
        const wsUrl = `ws://${window.location.hostname || 'localhost'}:8765`;
        console.log(`[QuantDesk] Connecting to C++ engine bridge at ${wsUrl}...`);

        try {
            this.ws = new WebSocket(wsUrl);

            this.ws.onopen = () => {
                this.isLiveConnected = true;
                this.elEngineStatus.textContent = "LIVE C++20 CORE (CONNECTED)";
                this.elEngineStatus.className = "metric-value status-online";
                if (this.elTapeBadge) this.elTapeBadge.textContent = "USER INGESTION ACTIVE";
                console.log("[QuantDesk] Successfully connected to live C++20 engine bridge!");
            };

            this.ws.onmessage = (event) => {
                try {
                    const data = JSON.parse(event.data);
                    if (data.type === "snapshot") {
                        this.handleLiveSnapshot(data);
                    }
                } catch (err) {
                    console.error("[QuantDesk] Error parsing C++ snapshot JSON:", err);
                }
            };

            this.ws.onclose = () => {
                this.isLiveConnected = false;
                this.elEngineStatus.textContent = "STANDBY / OFFLINE";
                this.elEngineStatus.className = "metric-value text-yellow";
                this.scheduleReconnect();
            };

            this.ws.onerror = () => {
                this.isLiveConnected = false;
                this.ws.close();
            };
        } catch (e) {
            this.isLiveConnected = false;
            this.scheduleReconnect();
        }
    }

    scheduleReconnect() {
        if (this.wsReconnectTimeout) clearTimeout(this.wsReconnectTimeout);
        this.wsReconnectTimeout = setTimeout(() => {
            this.connectWebSocket();
        }, 2000);
    }

    sendBridgeCommand(payload) {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(JSON.stringify(payload));
        } else {
            console.warn("[QuantDesk] WebSocket not open. Command queued or ignored:", payload);
        }
    }

    bindEvents() {
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

        // Action radio buttons (Add vs Cancel vs Execute)
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

                if (isNaN(orderId) || orderId <= 0) return;

                let line = "";
                if (selectedAction === 'A') {
                    line = `A,${orderId},${side},${price.toFixed(2)},${qty}`;
                } else if (selectedAction === 'X') {
                    line = `X,${orderId},0,0,${qty}`;
                } else if (selectedAction === 'E') {
                    line = `E,${orderId},0,${price.toFixed(2)},${qty}`;
                }

                this.sendBridgeCommand({ command: "RAW_LINE", line });
                
                // Auto-increment Order ID for rapid insertion
                this.inpOrderId.value = orderId + 1;
            });
        }

        // File dropzone & input
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

        // Batch Ingestion Button
        if (this.btnIngestBatch) {
            this.btnIngestBatch.addEventListener('click', () => {
                const text = this.txtBatchInput.value.trim();
                if (!text) return;
                const lines = text.split('\n').map(l => l.trim()).filter(l => l.length > 0);
                this.sendBridgeCommand({ command: "BATCH", lines });
            });
        }

        if (this.btnClearBatch) {
            this.btnClearBatch.addEventListener('click', () => {
                this.txtBatchInput.value = '';
            });
        }

        // Load Example Dataset Buttons
        document.querySelectorAll('.btn-load-example').forEach(btn => {
            btn.addEventListener('click', (e) => {
                const exName = e.target.getAttribute('data-example');
                this.sendBridgeCommand({ command: "LOAD_EXAMPLE", name: exName });
            });
        });

        // Burst 100K button
        if (this.btnBurst100k) {
            this.btnBurst100k.addEventListener('click', () => {
                this.sendBridgeCommand({ command: "BURST", count: 100000 });
            });
        }

        // Reset Book
        if (this.btnReset) {
            this.btnReset.addEventListener('click', () => {
                this.sendBridgeCommand({ command: "RESET" });
                this.bids = [];
                this.asks = [];
                this.recentTape = [];
                this.render();
            });
        }

        // Export Book
        if (this.btnExport) {
            this.btnExport.addEventListener('click', () => this.exportBookState());
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
        };
        reader.readAsText(file);
    }

    handleLiveSnapshot(data) {
        this.totalIngested = data.totalIngested || 0;
        this.totalProcessed = data.totalProcessed || 0;

        if (this.elTotalIngested) {
            this.elTotalIngested.innerHTML = `${this.totalProcessed.toLocaleString()} <small>pkts</small>`;
        }

        // Latencies
        if (data.latency) {
            const lat = data.latency;
            if (this.elP50Val) this.elP50Val.innerHTML = `${lat.p50.toFixed(1)} <small>ns</small>`;
            if (this.elP99Val) this.elP99Val.innerHTML = `${lat.p99.toFixed(1)} <small>ns</small>`;

            if (this.elLatMin) this.elLatMin.textContent = `${lat.min.toFixed(1)} ns`;
            if (this.elLatMean) this.elLatMean.textContent = `${lat.mean.toFixed(1)} ns`;
            if (this.elLatP50) this.elLatP50.textContent = `${lat.p50.toFixed(1)} ns`;
            if (this.elLatP90) this.elLatP90.textContent = `${lat.p90.toFixed(1)} ns`;
            if (this.elLatP99) this.elLatP99.textContent = `${lat.p99.toFixed(1)} ns`;
            if (this.elLatP999) this.elLatP999.textContent = `${lat.p999.toFixed(1)} ns`;
        }

        // Analytics
        if (data.analytics) {
            const an = data.analytics;
            if (this.elSpread) this.elSpread.textContent = `$${(an.spread / 100).toFixed(2)}`;
            if (this.elMid) this.elMid.textContent = `$${(an.mid / 100).toFixed(2)}`;
            if (this.elMicro) this.elMicro.textContent = `$${(an.micro / 100).toFixed(3)}`;

            if (this.elBboBadge) {
                if (an.hasBids && an.hasAsks) {
                    const ticks = (an.spread).toFixed(0);
                    this.elBboBadge.textContent = `${ticks} TICKS ($${(an.spread / 100).toFixed(2)})`;
                } else if (an.hasBids) {
                    this.elBboBadge.textContent = `BIDS ONLY (NO ASKS)`;
                } else if (an.hasAsks) {
                    this.elBboBadge.textContent = `ASKS ONLY (NO BIDS)`;
                } else {
                    this.elBboBadge.textContent = `BOOK EMPTY`;
                }
            }
        }

        // Bids and Asks
        this.bids = data.bids || [];
        this.asks = data.asks || [];
        this.recentTape = data.tape || [];

        this.render();
    }

    render() {
        this.renderLOB();
        this.renderTape();
        this.renderCanvas();
    }

    renderLOB() {
        // Active bids and asks
        const activeAsks = this.asks.filter(a => a.active && a.qty > 0);
        const activeBids = this.bids.filter(b => b.active && b.qty > 0);

        // Calculate max cumulative qty for background bars
        let maxCum = 1;
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

        maxCum = Math.max(cumAsk, cumBid, 1);

        // Render Asks (Display from highest price to lowest price near spread)
        if (asksWithCum.length === 0) {
            this.elAsksRows.innerHTML = '<div class="empty-book-hint">Waiting for user ask orders...</div>';
        } else {
            let html = '';
            for (const lvl of asksWithCum) {
                const pct = Math.min(100, Math.max(5, (lvl.cum / maxCum) * 100));
                const priceStr = (lvl.price / 100).toFixed(2);
                html += `
                <div class="lob-row ask-row">
                    <div class="depth-bar-fill ask-bar-fill" style="width: ${pct}%;"></div>
                    <span class="cell-orders">${lvl.orders}</span>
                    <span class="cell-qty">${lvl.qty.toLocaleString()}</span>
                    <span class="cell-cum text-muted">${lvl.cum.toLocaleString()}</span>
                    <span class="cell-price text-red text-right">$${priceStr}</span>
                </div>`;
            }
            this.elAsksRows.innerHTML = html;
        }

        // Render Bids (Display from highest price near spread to lowest price)
        if (bidsWithCum.length === 0) {
            this.elBidsRows.innerHTML = '<div class="empty-book-hint">Waiting for user bid orders...</div>';
        } else {
            let html = '';
            for (const lvl of bidsWithCum) {
                const pct = Math.min(100, Math.max(5, (lvl.cum / maxCum) * 100));
                const priceStr = (lvl.price / 100).toFixed(2);
                html += `
                <div class="lob-row bid-row">
                    <div class="depth-bar-fill bid-bar-fill" style="width: ${pct}%;"></div>
                    <span class="cell-price text-green">$${priceStr}</span>
                    <span class="cell-cum text-muted">${lvl.cum.toLocaleString()}</span>
                    <span class="cell-qty">${lvl.qty.toLocaleString()}</span>
                    <span class="cell-orders text-right">${lvl.orders}</span>
                </div>`;
            }
            this.elBidsRows.innerHTML = html;
        }

        // Total Volumes & Imbalance
        if (this.elTotalBidVol) this.elTotalBidVol.textContent = cumBid.toLocaleString();
        if (this.elTotalAskVol) this.elTotalAskVol.textContent = cumAsk.toLocaleString();

        const totalVol = cumBid + cumAsk;
        if (totalVol > 0) {
            const bidPct = ((cumBid / totalVol) * 100).toFixed(1);
            const askPct = (100.0 - parseFloat(bidPct)).toFixed(1);
            if (this.elOfiBidBar) this.elOfiBidBar.style.width = `${bidPct}%`;
            if (this.elOfiAskBar) this.elOfiAskBar.style.width = `${askPct}%`;
            if (this.elOfiBidPct) this.elOfiBidPct.textContent = `${bidPct}% Bids`;
            if (this.elOfiAskPct) this.elOfiAskPct.textContent = `${askPct}% Asks`;
        } else {
            if (this.elOfiBidBar) this.elOfiBidBar.style.width = `50%`;
            if (this.elOfiAskBar) this.elOfiAskBar.style.width = `50%`;
            if (this.elOfiBidPct) this.elOfiBidPct.textContent = `50.0% Bids`;
            if (this.elOfiAskPct) this.elOfiAskPct.textContent = `50.0% Asks`;
        }
    }

    renderTape() {
        if (!this.recentTape || this.recentTape.length === 0) {
            this.elTapeStream.innerHTML = '<div class="empty-tape-hint">No messages processed yet. Insert orders using the panel on the right.</div>';
            return;
        }

        let html = '';
        for (const pkt of this.recentTape) {
            let typeBadge = '';
            let sideBadge = pkt.side;
            let priceStr = pkt.price > 0 ? `$${(pkt.price / 100).toFixed(2)}` : '-';

            if (pkt.type === 'A') {
                typeBadge = '<span class="badge-type badge-add">ADD</span>';
                sideBadge = pkt.side === 'B' ? '<span class="text-green">BUY</span>' : '<span class="text-red">SELL</span>';
            } else if (pkt.type === 'X') {
                typeBadge = '<span class="badge-type badge-cancel">CANCEL</span>';
                sideBadge = '<span class="text-yellow">-</span>';
            } else if (pkt.type === 'E') {
                typeBadge = '<span class="badge-type badge-exec">FILL</span>';
                sideBadge = '<span class="text-cyan">TRADE</span>';
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
            this.ctx.fillStyle = "#4a5568";
            this.ctx.font = "12px 'JetBrains Mono', monospace";
            this.ctx.textAlign = "center";
            this.ctx.fillText("WAITING FOR USER LIMIT ORDERS TO PLOT DEPTH CURVE", width / 2, height / 2);
            return;
        }

        // Draw Center Split Line
        const midX = width / 2;
        this.ctx.strokeStyle = "rgba(255, 255, 255, 0.1)";
        this.ctx.lineWidth = 1;
        this.ctx.setLineDash([4, 4]);
        this.ctx.beginPath();
        this.ctx.moveTo(midX, 0);
        this.ctx.lineTo(midX, height);
        this.ctx.stroke();
        this.ctx.setLineDash([]);

        // Calculate max volume
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

        // Draw Bids Curve (Left half: Green)
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
            gradBid.addColorStop(0, "rgba(0, 230, 118, 0.35)");
            gradBid.addColorStop(1, "rgba(0, 230, 118, 0.02)");
            this.ctx.fillStyle = gradBid;
            this.ctx.fill();

            this.ctx.strokeStyle = "#00e676";
            this.ctx.lineWidth = 2;
            this.ctx.stroke();
        }

        // Draw Asks Curve (Right half: Red)
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
            gradAsk.addColorStop(0, "rgba(255, 23, 68, 0.35)");
            gradAsk.addColorStop(1, "rgba(255, 23, 68, 0.02)");
            this.ctx.fillStyle = gradAsk;
            this.ctx.fill();

            this.ctx.strokeStyle = "#ff1744";
            this.ctx.lineWidth = 2;
            this.ctx.stroke();
        }
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
    }
}

// Instantiate on load
window.addEventListener('DOMContentLoaded', () => {
    window.quantDesk = new QuantDeskVisualizer();
});
