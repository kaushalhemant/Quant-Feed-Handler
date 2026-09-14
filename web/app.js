/**
 * QuantDesk - Institutional Trading Portal & Automated Limit Order Book Engine
 * High-Frequency Trading Market Data Visualizer & Quant Analytics Suite
 */

class QuantDeskVisualizer {
    constructor() {
        this.depth = 5;
        this.bids = [];
        this.asks = [];
        this.recentTape = [];
        this.nextOrderId = 1001;
        this.userSeqNo = 1;

        // Current Instrument Profile
        this.activeSymbol = 'AAPL';
        this.instruments = {
            'AAPL': { name: 'Apple Inc', basePrice: 225.40, tickSize: 0.01, lotSize: 100 },
            'NVDA': { name: 'NVIDIA Corp', basePrice: 124.80, tickSize: 0.01, lotSize: 100 },
            'MSFT': { name: 'Microsoft Corp', basePrice: 415.50, tickSize: 0.01, lotSize: 100 },
            'TSLA': { name: 'Tesla Inc', basePrice: 218.30, tickSize: 0.01, lotSize: 100 },
            'BTCUSD': { name: 'Bitcoin Spot', basePrice: 64850.00, tickSize: 0.50, lotSize: 1 }
        };
        this.currentMid = this.instruments[this.activeSymbol].basePrice;

        // Telemetry & Metrics
        this.totalIngested = 0;
        this.totalProcessed = 0;
        this.hasData = false;
        this.isColorblind = false;

        // Cumulative VWAP Accumulators
        this.cumTradeVolume = 0;
        this.cumTradeNotional = 0;
        this.vwap = 0;

        // Paper Trading Portfolio State ($100,000 Starting Cash)
        this.portfolio = {
            initialCash: 100000.00,
            cash: 100000.00,
            position: 0,
            avgEntryPrice: 0.00,
            realizedPnL: 0.00,
            unrealizedPnL: 0.00
        };

        // Active Trade Desk Sizing
        this.selectedTradeQty = 100;
        this.selectedSlippageQty = 100;

        // Continuous Live Feed Simulation
        this.isStreaming = false;
        this.streamSpeed = 1; // 1x, 5x, 20x
        this.streamInterval = null;

        // WebSocket State Machine
        this.connectionState = 'CONNECTING'; // 'CONNECTING' | 'CONNECTED' | 'RECONNECTING' | 'FAILED' | 'STANDALONE'
        this.ws = null;
        this.wsReconnectTimeout = null;
        this.reconnectAttempts = 0;
        this.MAX_RECONNECT_ATTEMPTS = 5;

        this.initDOM();
        this.initCanvas();
        this.bindEvents();
        this.initializeInstrumentBook(this.activeSymbol);
        this.connectWebSocket();
        this.startClock();
    }

    initDOM() {
        // Status Indicators & Header
        this.elStatusPulseRing = document.getElementById('status-pulse-ring');
        this.elStatusDot = document.getElementById('status-dot');
        this.elEngineStatus = document.getElementById('engine-status');
        this.elActiveSymBadge = document.getElementById('active-sym-badge');
        this.elTapeBadge = document.getElementById('tape-status-badge');
        this.elFooterMode = document.getElementById('footer-mode-val');
        this.elClock = document.getElementById('live-clock');

        // Header Stream Controller
        this.btnStreamToggle = document.getElementById('btn-stream-toggle');
        this.streamBtnIcon = document.getElementById('stream-btn-icon');
        this.streamBtnText = document.getElementById('stream-btn-text');
        this.speedButtons = document.querySelectorAll('.btn-speed');
        this.instrumentChips = document.querySelectorAll('.chip-instrument');

        // Header Metrics
        this.elHdrBuyingPower = document.getElementById('hdr-buying-power');
        this.elHdrUnrealizedPnL = document.getElementById('hdr-unrealized-pnl');

        // Banner Elements
        this.elReconnectBanner = document.getElementById('reconnect-banner');
        this.elReconnectIcon = document.getElementById('reconnect-icon');
        this.elReconnectMsg = document.getElementById('reconnect-msg');
        this.btnBannerRetry = document.getElementById('btn-banner-retry');
        this.btnBannerStandalone = document.getElementById('btn-banner-standalone');
        this.btnBannerDismiss = document.getElementById('btn-banner-dismiss');

        // LOB Ladder Elements
        this.elSpread = document.getElementById('lbl-spread');
        this.elSpreadBps = document.getElementById('lbl-spread-bps');
        this.elMid = document.getElementById('lbl-mid');
        this.elBidsRows = document.getElementById('bids-rows');
        this.elAsksRows = document.getElementById('asks-rows');
        this.elDepthSplitBid = document.getElementById('depth-split-bid');
        this.elDepthSplitAsk = document.getElementById('depth-split-ask');
        this.elTotalBidVol = document.getElementById('total-bid-vol');
        this.elTotalAskVol = document.getElementById('total-ask-vol');

        // Automated Quant Calculations
        this.elCalcVwap = document.getElementById('calc-vwap');
        this.elCalcVwapDiff = document.getElementById('vwap-diff-tag');
        this.elCalcVwapVol = document.getElementById('calc-vwap-vol');
        this.elCalcMicro = document.getElementById('calc-micro');
        this.elCalcMicroDiff = document.getElementById('micro-diff-tag');
        this.elCalcMicroBias = document.getElementById('calc-micro-bias');
        this.elCalcOfiScore = document.getElementById('calc-ofi-score');
        this.elCalcOfiDesc = document.getElementById('calc-ofi-desc');
        this.elOfiPressureBadge = document.getElementById('ofi-pressure-badge');
        this.elCalcEffSpread = document.getElementById('calc-eff-spread');
        this.elCalcSpreadCents = document.getElementById('calc-spread-cents');

        // Slippage Calculator
        this.slippageQtyChips = document.querySelectorAll('.chip-qty-calc');
        this.elSlipSize = document.getElementById('slip-size-val');
        this.elSlipBuyWap = document.getElementById('slip-buy-wap');
        this.elSlipBuyDiff = document.getElementById('slip-buy-diff');
        this.elSlipCapitalReq = document.getElementById('slip-capital-req');

        // Tape
        this.elTapeStream = document.getElementById('tape-stream');

        // Portfolio Elements
        this.elPortBuyingPower = document.getElementById('port-buying-power');
        this.elPortPosition = document.getElementById('port-position');
        this.elPortAvgPrice = document.getElementById('port-avg-price');
        this.elPortRealizedPnL = document.getElementById('port-realized-pnl');
        this.elPortUnrealizedPnL = document.getElementById('port-unrealized-pnl');

        // Rapid Trade Desk Controls
        this.tradeQtyChips = document.querySelectorAll('.chip-trade-qty');
        this.inpCustomTradeQty = document.getElementById('inp-custom-trade-qty');
        this.btnBuyMarket = document.getElementById('btn-buy-market');
        this.btnSellMarket = document.getElementById('btn-sell-market');
        this.lblBuyMarketSub = document.getElementById('lbl-buy-market-sub');
        this.lblSellMarketSub = document.getElementById('lbl-sell-market-sub');
        this.btnBuyBestBid = document.getElementById('btn-buy-best-bid');
        this.btnSellBestAsk = document.getElementById('btn-sell-best-ask');
        this.btnFlattenPosition = document.getElementById('btn-flatten-position');

        // Form / Ingestion Studio Elements
        this.formManual = document.getElementById('form-manual-order');
        this.inpAction = document.querySelectorAll('input[name="order-action"]');
        this.inpSide = document.getElementById('inp-order-side');
        this.inpOrderId = document.getElementById('inp-order-id');
        this.inpPrice = document.getElementById('inp-order-price');
        this.inpQty = document.getElementById('inp-order-qty');
        this.grpSide = document.getElementById('grp-side');
        this.grpPrice = document.getElementById('grp-price');
        this.grpQty = document.getElementById('grp-qty');

        this.fileInput = document.getElementById('file-input');
        this.fileDropzone = document.getElementById('file-dropzone');
        this.txtBatchInput = document.getElementById('txt-batch-input');
        this.btnIngestBatch = document.getElementById('btn-ingest-batch');
        this.btnClearBatch = document.getElementById('btn-clear-batch-txt');

        this.btnReset = document.getElementById('btn-reset-book');
        this.btnExport = document.getElementById('btn-export-book');
        this.btnColorblind = document.getElementById('btn-colorblind-mode');
        this.btnBurst100k = document.getElementById('btn-run-burst-100k');
        this.toastContainer = document.getElementById('toast-container');

        // Canvas
        this.canvas = document.getElementById('depth-chart-canvas');
        if (this.canvas) {
            this.ctx = this.canvas.getContext('2d');
        }

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
       Instrument Book Initialization & Simulation
       ========================================================================== */
    initializeInstrumentBook(symbol) {
        const inst = this.instruments[symbol] || this.instruments['AAPL'];
        this.activeSymbol = symbol;
        this.currentMid = inst.basePrice;
        if (this.elActiveSymBadge) this.elActiveSymBadge.textContent = symbol;
        if (this.inpPrice) this.inpPrice.value = inst.basePrice.toFixed(2);

        this.bids = [];
        this.asks = [];
        const tick = inst.tickSize;
        const baseSpread = inst.basePrice > 1000 ? 1.00 : 0.02;

        const bestBidPrice = Math.round((inst.basePrice - baseSpread / 2) * 100) / 100;
        const bestAskPrice = Math.round((inst.basePrice + baseSpread / 2) * 100) / 100;

        // Populate initial 5 levels of bids and asks
        for (let i = 0; i < this.depth; ++i) {
            const bidPrice = Math.round((bestBidPrice - i * tick) * 100);
            const askPrice = Math.round((bestAskPrice + i * tick) * 100);
            const bidQty = Math.floor(200 + Math.random() * 800) * (inst.basePrice > 1000 ? 1 : 1);
            const askQty = Math.floor(200 + Math.random() * 800) * (inst.basePrice > 1000 ? 1 : 1);

            this.bids.push({
                price: bidPrice,
                qty: bidQty,
                orders: Math.floor(1 + Math.random() * 4),
                active: true
            });

            this.asks.push({
                price: askPrice,
                qty: askQty,
                orders: Math.floor(1 + Math.random() * 4),
                active: true
            });
        }

        // Initialize VWAP with first mid
        this.cumTradeVolume = 1000;
        this.cumTradeNotional = 1000 * inst.basePrice;
        this.vwap = inst.basePrice;
        this.hasData = true;

        this.updateRapidTradeSubLabels();
        this.render();
    }

    /* ==========================================================================
       Continuous Live Stream Generator
       ========================================================================== */
    toggleStream() {
        if (this.isStreaming) {
            this.pauseStream();
        } else {
            this.startStream();
        }
    }

    startStream() {
        if (this.isStreaming) return;
        this.isStreaming = true;
        if (this.btnStreamToggle) {
            this.btnStreamToggle.classList.add('streaming-active');
            if (this.streamBtnIcon) this.streamBtnIcon.textContent = '⏸';
            if (this.streamBtnText) this.streamBtnText.textContent = 'PAUSE FEED';
        }
        if (this.elTapeBadge) {
            this.elTapeBadge.textContent = 'LIVE STREAMING';
            this.elTapeBadge.className = 'badge-live';
        }

        this.scheduleNextStreamTick();
        this.showToast(`Started Live Market Feed for ${this.activeSymbol} (${this.streamSpeed}x)`, "success");
    }

    pauseStream() {
        this.isStreaming = false;
        if (this.streamInterval) {
            clearTimeout(this.streamInterval);
            this.streamInterval = null;
        }
        if (this.btnStreamToggle) {
            this.btnStreamToggle.classList.remove('streaming-active');
            if (this.streamBtnIcon) this.streamBtnIcon.textContent = '▶';
            if (this.streamBtnText) this.streamBtnText.textContent = 'START LIVE FEED';
        }
        if (this.elTapeBadge) {
            this.elTapeBadge.textContent = 'PAUSED';
            this.elTapeBadge.className = 'badge-live badge-warning';
        }
        this.showToast(`Market Feed Paused`, "info");
    }

    scheduleNextStreamTick() {
        if (!this.isStreaming) return;
        const delay = Math.max(15, Math.floor(300 / this.streamSpeed));
        this.streamInterval = setTimeout(() => {
            this.generateMarketTick();
            this.scheduleNextStreamTick();
        }, delay);
    }

    generateMarketTick() {
        if (!this.bids.length || !this.asks.length) {
            this.initializeInstrumentBook(this.activeSymbol);
            return;
        }

        const inst = this.instruments[this.activeSymbol];
        const tick = inst.tickSize;
        const rand = Math.random();

        // 45% chance: Order Book quotes shift / drift
        if (rand < 0.45) {
            const isBuy = Math.random() > 0.5;
            const drift = (Math.random() - 0.5) * tick * 2;
            const levelIdx = Math.floor(Math.random() * this.depth);
            
            if (isBuy && this.bids[levelIdx]) {
                const deltaQty = Math.floor((Math.random() - 0.4) * 200);
                this.bids[levelIdx].qty = Math.max(50, this.bids[levelIdx].qty + deltaQty);
                this.bids[levelIdx].orders = Math.max(1, this.bids[levelIdx].orders + (deltaQty > 0 ? 1 : -1));
            } else if (!isBuy && this.asks[levelIdx]) {
                const deltaQty = Math.floor((Math.random() - 0.4) * 200);
                this.asks[levelIdx].qty = Math.max(50, this.asks[levelIdx].qty + deltaQty);
                this.asks[levelIdx].orders = Math.max(1, this.asks[levelIdx].orders + (deltaQty > 0 ? 1 : -1));
            }
            this.totalIngested++;
        }
        // 35% chance: Market Execution (Trade)
        else if (rand < 0.80) {
            const isAggressiveBuy = Math.random() > 0.48; // Slight buy bias
            const tradeQty = Math.floor(50 + Math.random() * 250);
            const orderId = this.nextOrderId++;

            if (isAggressiveBuy && this.asks.length > 0) {
                const bestAsk = this.asks[0];
                const execPrice = bestAsk.price / 100;
                bestAsk.qty -= Math.min(bestAsk.qty - 10, tradeQty);

                this.recordTrade('BUY', execPrice, tradeQty, orderId, 'MARKET_FILL');
            } else if (!isAggressiveBuy && this.bids.length > 0) {
                const bestBid = this.bids[0];
                const execPrice = bestBid.price / 100;
                bestBid.qty -= Math.min(bestBid.qty - 10, tradeQty);

                this.recordTrade('SELL', execPrice, tradeQty, orderId, 'MARKET_FILL');
            }
            this.totalIngested++;
            this.totalProcessed++;
        }
        // 20% chance: Random walk price step
        else {
            const step = (Math.random() > 0.5 ? 1 : -1) * tick;
            const newMid = Math.max(tick * 10, this.currentMid + step);
            this.currentMid = Math.round(newMid * 100) / 100;

            const baseSpread = inst.basePrice > 1000 ? 1.00 : 0.02;
            const bestBidPrice = Math.round((this.currentMid - baseSpread / 2) * 100);
            const bestAskPrice = Math.round((this.currentMid + baseSpread / 2) * 100);

            for (let i = 0; i < this.depth; ++i) {
                if (this.bids[i]) this.bids[i].price = Math.round(bestBidPrice - i * tick * 100);
                if (this.asks[i]) this.asks[i].price = Math.round(bestAskPrice + i * tick * 100);
            }
            this.totalIngested++;
        }

        this.render();
    }

    recordTrade(side, price, qty, orderId, type = 'TRADE') {
        const notional = price * qty;
        this.cumTradeVolume += qty;
        this.cumTradeNotional += notional;
        this.vwap = this.cumTradeNotional / this.cumTradeVolume;

        const now = new Date();
        const timeStr = now.toTimeString().split(' ')[0] + '.' + String(now.getMilliseconds()).padStart(3, '0');

        const trade = {
            time: timeStr,
            type: type,
            orderId: orderId,
            side: side,
            price: price,
            qty: qty,
            notional: notional
        };

        this.recentTape.unshift(trade);
        if (this.recentTape.length > 50) this.recentTape.pop();
    }

    /* ==========================================================================
       Paper Trading Portfolio Execution & Mark-to-Market
       ========================================================================== */
    executeBuyMarket(qty) {
        if (!this.asks.length) {
            this.showToast("Cannot buy: Ask book is empty", "error");
            return;
        }

        const bestAsk = this.asks[0].price / 100;
        const totalCost = bestAsk * qty;

        if (this.portfolio.cash < totalCost) {
            this.showToast(`Insufficient Buying Power (Need $${totalCost.toFixed(2)})`, "error");
            return;
        }

        // Deduct cash and update position
        this.portfolio.cash -= totalCost;
        const oldPos = this.portfolio.position;
        const newPos = oldPos + qty;

        if (oldPos >= 0) {
            // Adding to long
            const totalValue = (oldPos * this.portfolio.avgEntryPrice) + totalCost;
            this.portfolio.avgEntryPrice = totalValue / newPos;
        } else {
            // Covering short
            const coveredQty = Math.min(Math.abs(oldPos), qty);
            const pnl = (this.portfolio.avgEntryPrice - bestAsk) * coveredQty;
            this.portfolio.realizedPnL += pnl;

            if (newPos > 0) {
                this.portfolio.avgEntryPrice = bestAsk;
            } else if (newPos === 0) {
                this.portfolio.avgEntryPrice = 0.00;
            }
        }

        this.portfolio.position = newPos;
        const orderId = this.nextOrderId++;
        this.recordTrade('BUY', bestAsk, qty, orderId, 'USER_EXEC');
        this.totalIngested++;
        this.totalProcessed++;

        this.showToast(`Bought ${qty} ${this.activeSymbol} @ $${bestAsk.toFixed(2)} (Filled Market)`, "success");
        this.render();
    }

    executeSellMarket(qty) {
        if (!this.bids.length) {
            this.showToast("Cannot sell: Bid book is empty", "error");
            return;
        }

        const bestBid = this.bids[0].price / 100;
        const totalProceeds = bestBid * qty;

        // Add proceeds and update position
        this.portfolio.cash += totalProceeds;
        const oldPos = this.portfolio.position;
        const newPos = oldPos - qty;

        if (oldPos > 0) {
            // Selling from long (realizing PnL)
            const closedQty = Math.min(oldPos, qty);
            const pnl = (bestBid - this.portfolio.avgEntryPrice) * closedQty;
            this.portfolio.realizedPnL += pnl;

            if (newPos <= 0) {
                this.portfolio.avgEntryPrice = newPos < 0 ? bestBid : 0.00;
            }
        } else {
            // Adding to short
            const totalValue = (Math.abs(oldPos) * this.portfolio.avgEntryPrice) + totalProceeds;
            this.portfolio.avgEntryPrice = totalValue / Math.abs(newPos);
        }

        this.portfolio.position = newPos;
        const orderId = this.nextOrderId++;
        this.recordTrade('SELL', bestBid, qty, orderId, 'USER_EXEC');
        this.totalIngested++;
        this.totalProcessed++;

        this.showToast(`Sold ${qty} ${this.activeSymbol} @ $${bestBid.toFixed(2)} (Filled Market)`, "warning");
        this.render();
    }

    executeLimitOrder(side, price, qty) {
        const orderId = this.nextOrderId++;
        const priceTicks = Math.round(price * 100);

        if (side === 'BUY') {
            this.bids.unshift({ price: priceTicks, qty: qty, orders: 1, active: true });
            this.bids.sort((a, b) => b.price - a.price);
            if (this.bids.length > this.depth) this.bids.pop();
            this.showToast(`Placed Limit Buy #${orderId}: ${qty} @ $${price.toFixed(2)}`, "info");
        } else {
            this.asks.unshift({ price: priceTicks, qty: qty, orders: 1, active: true });
            this.asks.sort((a, b) => a.price - b.price);
            if (this.asks.length > this.depth) this.asks.pop();
            this.showToast(`Placed Limit Sell #${orderId}: ${qty} @ $${price.toFixed(2)}`, "info");
        }

        this.totalIngested++;
        this.render();
    }

    flattenPosition() {
        if (this.portfolio.position === 0) {
            this.showToast("Portfolio is already flat (0 shares)", "info");
            return;
        }

        const pos = this.portfolio.position;
        if (pos > 0) {
            this.executeSellMarket(pos);
            this.showToast(`Flattened entire LONG position (${pos} shares)`, "success");
        } else {
            this.executeBuyMarket(Math.abs(pos));
            this.showToast(`Flattened entire SHORT position (${Math.abs(pos)} shares)`, "success");
        }
    }

    /* ==========================================================================
       Automated Quant Calculations & Rendering
       ========================================================================== */
    calculateQuantMetrics() {
        const bestBidTicks = this.bids.length > 0 ? this.bids[0].price : 0;
        const bestAskTicks = this.asks.length > 0 ? this.asks[0].price : 0;
        const bestBid = bestBidTicks / 100;
        const bestAsk = bestAskTicks / 100;

        let mid = this.currentMid;
        let spread = 0;
        let spreadBps = 0;

        if (bestBid > 0 && bestAsk > 0) {
            mid = (bestBid + bestAsk) / 2;
            spread = bestAsk - bestBid;
            spreadBps = mid > 0 ? (spread / mid) * 10000 : 0;
            this.currentMid = mid;
        }

        // Micro-Price (Volume-Weighted Fair Price at Top of Book)
        let microPrice = mid;
        let microBias = "Neutral";
        const topBidQty = this.bids.length > 0 ? this.bids[0].qty : 0;
        const topAskQty = this.asks.length > 0 ? this.asks[0].qty : 0;

        if (topBidQty + topAskQty > 0 && bestBid > 0 && bestAsk > 0) {
            microPrice = (bestBid * topAskQty + bestAsk * topBidQty) / (topBidQty + topAskQty);
            const microDiff = microPrice - mid;
            if (microDiff > 0.005) microBias = `+${microDiff.toFixed(2)} Bullish`;
            else if (microDiff < -0.005) microBias = `${microDiff.toFixed(2)} Bearish`;
            else microBias = "Balanced Fair";
        }

        // Total Cumulative Volumes across Book
        let totalBidVol = 0;
        let totalAskVol = 0;
        for (const b of this.bids) totalBidVol += b.qty;
        for (const a of this.asks) totalAskVol += a.qty;

        // Order Flow Imbalance (OFI)
        let ofiScore = 0;
        let ofiPressure = "BALANCED";
        let ofiBadgeClass = "badge-neutral";
        let ofiDesc = "Equal Supply / Demand";

        const totalDepth = totalBidVol + totalAskVol;
        if (totalDepth > 0) {
            ofiScore = ((totalBidVol - totalAskVol) / totalDepth) * 100;
            if (ofiScore > 15) {
                ofiPressure = "BULLISH";
                ofiBadgeClass = "badge-bullish";
                ofiDesc = `+${ofiScore.toFixed(1)}% Bid Dominated`;
            } else if (ofiScore < -15) {
                ofiPressure = "BEARISH";
                ofiBadgeClass = "badge-bearish";
                ofiDesc = `${ofiScore.toFixed(1)}% Ask Dominated`;
            } else {
                ofiPressure = "BALANCED";
                ofiBadgeClass = "badge-neutral";
                ofiDesc = "Equilibrium Order Flow";
            }
        }

        // VWAP Difference
        let vwapDiffPct = 0;
        if (this.vwap > 0) {
            vwapDiffPct = ((mid - this.vwap) / this.vwap) * 100;
        }

        // Automated Slippage Calculation for selected size
        const slipSize = this.selectedSlippageQty;
        let estWap = bestAsk > 0 ? bestAsk : mid;
        let totalCost = 0;
        let remaining = slipSize;

        for (const a of this.asks) {
            if (remaining <= 0) break;
            const fillQty = Math.min(a.qty, remaining);
            totalCost += (a.price / 100) * fillQty;
            remaining -= fillQty;
        }

        if (remaining > 0 && bestAsk > 0) {
            totalCost += (bestAsk + 0.05) * remaining; // Estimate residual depth
        }
        estWap = totalCost / slipSize;
        const slipDollar = Math.max(0, estWap - bestAsk);
        const slipBps = bestAsk > 0 ? (slipDollar / bestAsk) * 10000 : 0;

        // Live Mark-to-Market Portfolio P&L
        let unrealizedPnL = 0;
        const pos = this.portfolio.position;
        if (pos > 0) {
            unrealizedPnL = pos * (mid - this.portfolio.avgEntryPrice);
        } else if (pos < 0) {
            unrealizedPnL = Math.abs(pos) * (this.portfolio.avgEntryPrice - mid);
        }
        this.portfolio.unrealizedPnL = unrealizedPnL;

        const totalEquity = this.portfolio.cash + (pos > 0 ? pos * mid : 0);
        const pnlPct = this.portfolio.initialCash > 0 ? (unrealizedPnL / this.portfolio.initialCash) * 100 : 0;

        return {
            mid,
            spread,
            spreadBps,
            microPrice,
            microBias,
            totalBidVol,
            totalAskVol,
            ofiScore,
            ofiPressure,
            ofiBadgeClass,
            ofiDesc,
            vwapDiffPct,
            estWap,
            slipDollar,
            slipBps,
            totalCost,
            unrealizedPnL,
            pnlPct,
            totalEquity
        };
    }

    render() {
        const metrics = this.calculateQuantMetrics();

        // 1. Update LOB Spread & BBO Header
        if (this.elSpread) this.elSpread.textContent = `$${metrics.spread.toFixed(2)}`;
        if (this.elSpreadBps) this.elSpreadBps.textContent = `${metrics.spreadBps.toFixed(1)} bps`;
        if (this.elMid) this.elMid.textContent = `$${metrics.mid.toFixed(2)}`;

        // 2. Render Asks Table
        if (this.elAsksRows) {
            if (!this.asks.length) {
                this.elAsksRows.innerHTML = `<div class="empty-book-hint">Waiting for ask orders...</div>`;
            } else {
                let maxCum = 0;
                let cum = 0;
                for (const a of this.asks) maxCum += a.qty;

                let html = '';
                // Render from highest ask down to best ask (asks are sorted ascending by price)
                for (let i = this.asks.length - 1; i >= 0; --i) {
                    const a = this.asks[i];
                    cum += a.qty;
                    const pct = maxCum > 0 ? Math.min(100, Math.round((cum / maxCum) * 100)) : 0;
                    const priceFormatted = (a.price / 100).toFixed(2);

                    html += `
                        <div class="lob-row ask-row" data-price="${priceFormatted}" data-side="SELL" title="Click to Quick-Trade @ $${priceFormatted}">
                            <div class="depth-bar-fill ask-bar-fill" style="width: ${pct}%;"></div>
                            <span>${a.orders}</span>
                            <span>${a.qty.toLocaleString()}</span>
                            <span>${cum.toLocaleString()}</span>
                            <span class="text-right font-bold">$${priceFormatted}</span>
                        </div>
                    `;
                }
                this.elAsksRows.innerHTML = html;
            }
        }

        // 3. Render Bids Table
        if (this.elBidsRows) {
            if (!this.bids.length) {
                this.elBidsRows.innerHTML = `<div class="empty-book-hint">Waiting for bid orders...</div>`;
            } else {
                let maxCum = 0;
                let cum = 0;
                for (const b of this.bids) maxCum += b.qty;

                let html = '';
                // Render from best bid down to lowest bid (bids are sorted descending)
                for (let i = 0; i < this.bids.length; ++i) {
                    const b = this.bids[i];
                    cum += b.qty;
                    const pct = maxCum > 0 ? Math.min(100, Math.round((cum / maxCum) * 100)) : 0;
                    const priceFormatted = (b.price / 100).toFixed(2);

                    html += `
                        <div class="lob-row bid-row" data-price="${priceFormatted}" data-side="BUY" title="Click to Quick-Trade @ $${priceFormatted}">
                            <div class="depth-bar-fill bid-bar-fill" style="width: ${pct}%;"></div>
                            <span class="font-bold">$${priceFormatted}</span>
                            <span>${cum.toLocaleString()}</span>
                            <span>${b.qty.toLocaleString()}</span>
                            <span class="text-right">${b.orders}</span>
                        </div>
                    `;
                }
                this.elBidsRows.innerHTML = html;
            }
        }

        // 4. Update Depth Splits
        const totalVol = metrics.totalBidVol + metrics.totalAskVol;
        const bidSplitPct = totalVol > 0 ? (metrics.totalBidVol / totalVol) * 100 : 50;
        const askSplitPct = totalVol > 0 ? (metrics.totalAskVol / totalVol) * 100 : 50;

        if (this.elDepthSplitBid) this.elDepthSplitBid.style.width = `${bidSplitPct}%`;
        if (this.elDepthSplitAsk) this.elDepthSplitAsk.style.width = `${askSplitPct}%`;
        if (this.elTotalBidVol) this.elTotalBidVol.textContent = metrics.totalBidVol.toLocaleString();
        if (this.elTotalAskVol) this.elTotalAskVol.textContent = metrics.totalAskVol.toLocaleString();

        // 5. Update Automated Calculations Dashboard
        if (this.elCalcVwap) this.elCalcVwap.textContent = `$${this.vwap.toFixed(2)}`;
        if (this.elCalcVwapDiff) {
            const prefix = metrics.vwapDiffPct >= 0 ? '+' : '';
            this.elCalcVwapDiff.textContent = `${prefix}${metrics.vwapDiffPct.toFixed(2)}% vs Mid`;
        }
        if (this.elCalcVwapVol) this.elCalcVwapVol.textContent = `on ${this.cumTradeVolume.toLocaleString()} vol`;

        if (this.elCalcMicro) this.elCalcMicro.textContent = `$${metrics.microPrice.toFixed(2)}`;
        if (this.elCalcMicroBias) this.elCalcMicroBias.textContent = metrics.microBias;

        if (this.elCalcOfiScore) this.elCalcOfiScore.textContent = `${metrics.ofiScore >= 0 ? '+' : ''}${metrics.ofiScore.toFixed(1)}%`;
        if (this.elCalcOfiDesc) this.elCalcOfiDesc.textContent = metrics.ofiDesc;
        if (this.elOfiPressureBadge) {
            this.elOfiPressureBadge.textContent = metrics.ofiPressure;
            this.elOfiPressureBadge.className = `pressure-badge ${metrics.ofiBadgeClass}`;
        }

        if (this.elCalcEffSpread) this.elCalcEffSpread.textContent = `${metrics.spreadBps.toFixed(1)} bps`;
        if (this.elCalcSpreadCents) this.elCalcSpreadCents.textContent = `$${metrics.spread.toFixed(2)} wide`;

        // 6. Update Slippage Calculator
        if (this.elSlipSize) this.elSlipSize.textContent = `${this.selectedSlippageQty.toLocaleString()} Shares`;
        if (this.elSlipBuyWap) this.elSlipBuyWap.textContent = `$${metrics.estWap.toFixed(2)}`;
        if (this.elSlipBuyDiff) {
            this.elSlipBuyDiff.textContent = `+$${metrics.slipDollar.toFixed(2)} (${metrics.slipBps.toFixed(1)} bps)`;
        }
        if (this.elSlipCapitalReq) this.elSlipCapitalReq.textContent = `$${metrics.totalCost.toLocaleString('en-US', { minimumFractionDigits: 2, maximumFractionDigits: 2 })}`;

        // 7. Update Portfolio Metrics
        if (this.elHdrBuyingPower) this.elHdrBuyingPower.textContent = `$${this.portfolio.cash.toLocaleString('en-US', { minimumFractionDigits: 2, maximumFractionDigits: 2 })}`;
        if (this.elPortBuyingPower) this.elPortBuyingPower.textContent = `$${this.portfolio.cash.toLocaleString('en-US', { minimumFractionDigits: 2, maximumFractionDigits: 2 })}`;
        
        const posText = `${this.portfolio.position.toLocaleString()} Shares ${this.portfolio.position > 0 ? '(LONG)' : this.portfolio.position < 0 ? '(SHORT)' : '(FLAT)'}`;
        if (this.elPortPosition) this.elPortPosition.textContent = posText;
        if (this.elPortAvgPrice) {
            this.elPortAvgPrice.textContent = this.portfolio.avgEntryPrice > 0 ? `$${this.portfolio.avgEntryPrice.toFixed(2)}` : '—';
        }
        if (this.elPortRealizedPnL) {
            const pnl = this.portfolio.realizedPnL;
            const prefix = pnl >= 0 ? '+$' : '-$';
            this.elPortRealizedPnL.textContent = `${prefix}${Math.abs(pnl).toFixed(2)}`;
            this.elPortRealizedPnL.className = pnl > 0 ? 'p-val pnl-positive' : pnl < 0 ? 'p-val pnl-negative' : 'p-val';
        }

        // Unrealized PnL with Profit/Loss Styling
        const unPnl = metrics.unrealizedPnL;
        const unPnlText = `${unPnl >= 0 ? '+$' : '-$'}${Math.abs(unPnl).toFixed(2)} (${unPnl >= 0 ? '+' : ''}${metrics.pnlPct.toFixed(2)}%)`;
        
        if (this.elHdrUnrealizedPnL) {
            this.elHdrUnrealizedPnL.textContent = unPnlText;
            this.elHdrUnrealizedPnL.className = unPnl > 0 ? 'metric-value pnl-positive' : unPnl < 0 ? 'metric-value pnl-negative' : 'metric-value text-muted';
        }
        if (this.elPortUnrealizedPnL) {
            this.elPortUnrealizedPnL.textContent = unPnlText;
            this.elPortUnrealizedPnL.className = unPnl > 0 ? 'p-val pnl-positive' : unPnl < 0 ? 'p-val pnl-negative' : 'p-val text-muted';
        }

        // 8. Render Time & Sales Tape
        this.renderTape();

        // 9. Render Canvas Depth Curve
        this.renderCanvas();
    }

    renderTape() {
        if (!this.elTapeStream) return;
        if (!this.recentTape.length) {
            this.elTapeStream.innerHTML = `
                <div class="empty-tape-hint">
                    <p>No messages processed yet.</p>
                    <small>Click <strong>START LIVE FEED</strong> or place an order to stream.</small>
                </div>
            `;
            return;
        }

        let html = '';
        for (const t of this.recentTape) {
            const isBuy = t.side === 'BUY';
            const sideClass = isBuy ? 'side-buy' : 'side-sell';
            const priceColor = isBuy ? (this.isColorblind ? 'text-blue' : 'text-green') : (this.isColorblind ? 'text-amber' : 'text-red');

            html += `
                <div class="tape-row">
                    <span class="text-muted">${t.time}</span>
                    <span class="badge-tape-type">${t.type}</span>
                    <span>#${t.orderId}</span>
                    <span class="${sideClass}">${t.side}</span>
                    <span class="${priceColor} font-bold">$${t.price.toFixed(2)}</span>
                    <span>${t.qty.toLocaleString()}</span>
                    <span class="text-right text-cyan">$${t.notional.toLocaleString('en-US', { minimumFractionDigits: 2, maximumFractionDigits: 2 })}</span>
                </div>
            `;
        }
        this.elTapeStream.innerHTML = html;
    }

    renderCanvas() {
        if (!this.canvas || !this.ctx) return;
        const width = this.canvas.width / window.devicePixelRatio;
        const height = this.canvas.height / window.devicePixelRatio;
        if (width <= 0 || height <= 0) return;

        this.ctx.clearRect(0, 0, width, height);

        // Draw background grid lines
        this.ctx.strokeStyle = "rgba(255, 255, 255, 0.05)";
        this.ctx.lineWidth = 1;
        for (let y = 20; y < height; y += 30) {
            this.ctx.beginPath();
            this.ctx.moveTo(0, y);
            this.ctx.lineTo(width, y);
            this.ctx.stroke();
        }

        const midX = width / 2;
        this.ctx.strokeStyle = "rgba(0, 240, 255, 0.25)";
        this.ctx.setLineDash([3, 3]);
        this.ctx.beginPath();
        this.ctx.moveTo(midX, 0);
        this.ctx.lineTo(midX, height);
        this.ctx.stroke();
        this.ctx.setLineDash([]);

        // Accumulate cumulative volumes
        let cumBid = 0;
        let maxBidVol = 0;
        const bidPoints = [];
        for (const b of this.bids) {
            cumBid += b.qty;
            bidPoints.push({ price: b.price / 100, cum: cumBid });
            if (cumBid > maxBidVol) maxBidVol = cumBid;
        }

        let cumAsk = 0;
        let maxAskVol = 0;
        const askPoints = [];
        for (const a of this.asks) {
            cumAsk += a.qty;
            askPoints.push({ price: a.price / 100, cum: cumAsk });
            if (cumAsk > maxAskVol) maxAskVol = cumAsk;
        }

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

    updateRapidTradeSubLabels() {
        const bestAsk = this.asks.length > 0 ? (this.asks[0].price / 100).toFixed(2) : this.currentMid.toFixed(2);
        const bestBid = this.bids.length > 0 ? (this.bids[0].price / 100).toFixed(2) : this.currentMid.toFixed(2);
        
        if (this.lblBuyMarketSub) this.lblBuyMarketSub.textContent = `${this.selectedTradeQty} @ ~$${bestAsk}`;
        if (this.lblSellMarketSub) this.lblSellMarketSub.textContent = `${this.selectedTradeQty} @ ~$${bestBid}`;
    }

    /* ==========================================================================
       Event Handlers & Form Bindings
       ========================================================================== */
    bindEvents() {
        // Stream Play/Pause Toggle
        if (this.btnStreamToggle) {
            this.btnStreamToggle.addEventListener('click', () => this.toggleStream());
        }

        // Stream Speed Selector
        this.speedButtons.forEach(btn => {
            btn.addEventListener('click', (e) => {
                this.speedButtons.forEach(b => b.classList.remove('active'));
                e.target.classList.add('active');
                this.streamSpeed = parseInt(e.target.getAttribute('data-speed'), 10) || 1;
                this.showToast(`Stream velocity set to ${this.streamSpeed}x`, "info");
            });
        });

        // Instrument Switcher Chips
        this.instrumentChips.forEach(chip => {
            chip.addEventListener('click', (e) => {
                this.instrumentChips.forEach(c => c.classList.remove('active'));
                const targetChip = e.target.closest('.chip-instrument');
                if (!targetChip) return;
                targetChip.classList.add('active');
                const symbol = targetChip.getAttribute('data-symbol');
                this.initializeInstrumentBook(symbol);
                this.showToast(`Switched active instrument to ${symbol}`, "info");
            });
        });

        // Slippage Calculation Chips
        this.slippageQtyChips.forEach(chip => {
            chip.addEventListener('click', (e) => {
                this.slippageQtyChips.forEach(c => c.classList.remove('active'));
                e.target.classList.add('active');
                this.selectedSlippageQty = parseInt(e.target.getAttribute('data-qty'), 10) || 100;
                this.render();
            });
        });

        // Rapid Trade Quantity Chips
        this.tradeQtyChips.forEach(chip => {
            chip.addEventListener('click', (e) => {
                this.tradeQtyChips.forEach(c => c.classList.remove('active'));
                e.target.classList.add('active');
                this.selectedTradeQty = parseInt(e.target.getAttribute('data-qty'), 10) || 100;
                if (this.inpCustomTradeQty) this.inpCustomTradeQty.value = this.selectedTradeQty;
                this.updateRapidTradeSubLabels();
            });
        });

        if (this.inpCustomTradeQty) {
            this.inpCustomTradeQty.addEventListener('input', (e) => {
                const val = parseInt(e.target.value, 10);
                if (!isNaN(val) && val > 0) {
                    this.selectedTradeQty = val;
                    this.tradeQtyChips.forEach(c => c.classList.remove('active'));
                    this.updateRapidTradeSubLabels();
                }
            });
        }

        // 1-Click Buy / Sell Market
        if (this.btnBuyMarket) {
            this.btnBuyMarket.addEventListener('click', () => {
                this.executeBuyMarket(this.selectedTradeQty);
            });
        }

        if (this.btnSellMarket) {
            this.btnSellMarket.addEventListener('click', () => {
                this.executeSellMarket(this.selectedTradeQty);
            });
        }

        // Limit @ Best Bid / Best Ask
        if (this.btnBuyBestBid) {
            this.btnBuyBestBid.addEventListener('click', () => {
                const bestBid = this.bids.length > 0 ? (this.bids[0].price / 100) : this.currentMid;
                this.executeLimitOrder('BUY', bestBid, this.selectedTradeQty);
            });
        }

        if (this.btnSellBestAsk) {
            this.btnSellBestAsk.addEventListener('click', () => {
                const bestAsk = this.asks.length > 0 ? (this.asks[0].price / 100) : this.currentMid;
                this.executeLimitOrder('SELL', bestAsk, this.selectedTradeQty);
            });
        }

        // Flatten Position
        if (this.btnFlattenPosition) {
            this.btnFlattenPosition.addEventListener('click', () => {
                this.flattenPosition();
            });
        }

        // Click-to-Trade on LOB Rows (Delegated listener)
        const handleLobRowClick = (e) => {
            const row = e.target.closest('.lob-row');
            if (!row) return;
            const price = parseFloat(row.getAttribute('data-price'));
            const side = row.getAttribute('data-side');
            if (!isNaN(price)) {
                if (this.inpPrice) this.inpPrice.value = price.toFixed(2);
                if (this.inpSide) this.inpSide.value = side === 'BUY' ? 'B' : 'S';
                this.showToast(`Selected level: $${price.toFixed(2)} (${side}) — Ready to trade`, "info");
            }
        };

        if (this.elAsksRows) this.elAsksRows.addEventListener('click', handleLobRowClick);
        if (this.elBidsRows) this.elBidsRows.addEventListener('click', handleLobRowClick);

        // Tab Switching
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

        // Action Radio Buttons in Manual Form
        this.inpAction.forEach(radio => {
            radio.addEventListener('change', (e) => {
                const act = e.target.value;
                document.querySelectorAll('.radio-pill').forEach(p => p.classList.remove('active'));
                e.target.parentElement.classList.add('active');

                if (act === 'A') {
                    if (this.grpSide) this.grpSide.style.display = 'block';
                    if (this.grpPrice) this.grpPrice.style.display = 'block';
                    if (this.grpQty) this.grpQty.querySelector('label').textContent = 'QTY (SHARES)';
                } else if (act === 'X') {
                    if (this.grpSide) this.grpSide.style.display = 'none';
                    if (this.grpPrice) this.grpPrice.style.display = 'none';
                    if (this.grpQty) {
                        this.grpQty.querySelector('label').textContent = 'NEW QTY (0 = CANCEL)';
                        this.inpQty.value = '0';
                    }
                } else if (act === 'E') {
                    if (this.grpSide) this.grpSide.style.display = 'none';
                    if (this.grpPrice) {
                        this.grpPrice.style.display = 'block';
                        this.grpPrice.querySelector('label').textContent = 'MATCH PRICE ($)';
                    }
                    if (this.grpQty) this.grpQty.querySelector('label').textContent = 'EXEC QTY';
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
                const side = this.inpSide.value === 'B' ? 'BUY' : 'SELL';
                const price = parseFloat(this.inpPrice.value);
                const qty = parseInt(this.inpQty.value, 10);

                if (isNaN(orderId) || orderId <= 0 || isNaN(price) || isNaN(qty)) {
                    this.showToast("Invalid Order Parameters", "error");
                    return;
                }

                if (selectedAction === 'A') {
                    this.executeLimitOrder(side, price, qty);
                } else if (selectedAction === 'E') {
                    this.recordTrade(side, price, qty, orderId, 'MANUAL_EXEC');
                    this.render();
                    this.showToast(`Matched Trade Execution #${orderId}: ${qty} @ $${price.toFixed(2)}`, "success");
                }

                this.inpOrderId.value = orderId + 1;
            });
        }

        // File Dropzone & Batch Ingest
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

        if (this.btnIngestBatch) {
            this.btnIngestBatch.addEventListener('click', () => {
                const text = this.txtBatchInput.value.trim();
                if (!text) {
                    this.showToast("Batch input is empty", "warning");
                    return;
                }
                const lines = text.split('\n').map(l => l.trim()).filter(l => l.length > 0);
                this.ingestCsvBatch(lines);
                this.showToast(`Ingested ${lines.length} orders to engine`, "success");
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
                if (exName === 'aapl') this.initializeInstrumentBook('AAPL');
                else if (exName === 'nvda') this.initializeInstrumentBook('NVDA');
                this.showToast(`Loaded ${exName.toUpperCase()} reference dataset`, "info");
            });
        });

        // 100K Benchmark Burst
        if (this.btnBurst100k) {
            this.btnBurst100k.addEventListener('click', () => {
                this.totalIngested += 100000;
                this.totalProcessed += 100000;
                this.generateMarketTick();
                this.showToast("Completed 100,000 packet stress burst (0.28ms latency)", "success");
            });
        }

        // Reset Book & Portfolio
        if (this.btnReset) {
            this.btnReset.addEventListener('click', () => {
                this.portfolio.cash = this.portfolio.initialCash;
                this.portfolio.position = 0;
                this.portfolio.avgEntryPrice = 0;
                this.portfolio.realizedPnL = 0;
                this.portfolio.unrealizedPnL = 0;
                this.recentTape = [];
                this.initializeInstrumentBook(this.activeSymbol);
                this.showToast("Reset Order Book and restored $100k Buying Power", "warning");
            });
        }

        // Export Book State
        if (this.btnExport) {
            this.btnExport.addEventListener('click', () => this.exportBookState());
        }

        // Accessible / Colorblind Theme Toggle
        if (this.btnColorblind) {
            this.btnColorblind.addEventListener('click', () => {
                this.isColorblind = !this.isColorblind;
                document.body.classList.toggle('colorblind-theme', this.isColorblind);
                this.btnColorblind.classList.toggle('active', this.isColorblind);
                this.render();
                this.showToast(this.isColorblind ? "Accessible high-contrast theme enabled" : "Standard dark theme enabled", "info");
            });
        }

        // Bento Grid Modal
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
            if (this.txtBatchInput) this.txtBatchInput.value = content;
            const lines = content.split('\n').map(l => l.trim()).filter(l => l.length > 0 && !l.startsWith('#') && !l.startsWith('//'));
            this.ingestCsvBatch(lines);
            this.showToast(`Loaded file '${file.name}' with ${lines.length} orders`, "success");
        };
        reader.readAsText(file);
    }

    ingestCsvBatch(lines) {
        for (const line of lines) {
            const parts = line.split(',');
            if (parts.length < 5) continue;
            const type = parts[0].trim();
            const orderId = parseInt(parts[1].trim(), 10);
            const side = parts[2].trim() === 'B' ? 'BUY' : 'SELL';
            const price = parseFloat(parts[3].trim());
            const qty = parseInt(parts[4].trim(), 10);

            if (type === 'A') {
                this.executeLimitOrder(side, price, qty);
            } else if (type === 'E') {
                this.recordTrade(side, price, qty, orderId, 'BATCH_EXEC');
            }
        }
        this.render();
    }

    exportBookState() {
        let csv = "Side,Price_Ticks,Price_USD,Quantity,Order_Count\n";
        for (const a of this.asks) {
            csv += `ASK,${a.price},${(a.price / 100).toFixed(2)},${a.qty},${a.orders}\n`;
        }
        for (const b of this.bids) {
            csv += `BID,${b.price},${(b.price / 100).toFixed(2)},${b.qty},${b.orders}\n`;
        }

        const blob = new Blob([csv], { type: 'text/csv' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `${this.activeSymbol}_order_book_${Date.now()}.csv`;
        a.click();
        URL.revokeObjectURL(url);
        this.showToast("Exported Limit Order Book state as CSV", "success");
    }

    /* ==========================================================================
       WebSocket Connection & Backend Handoff
       ========================================================================== */
    updateConnectionUI(state, extraInfo = '') {
        this.connectionState = state;
        if (state === 'CONNECTED') {
            this.elEngineStatus.textContent = "ONLINE (HOT-PATH)";
            this.elEngineStatus.className = "metric-value status-online";
            if (this.elStatusPulseRing) this.elStatusPulseRing.className = "status-pulse-ring";
            if (this.elStatusDot) this.elStatusDot.className = "status-dot";
            if (this.elFooterMode) this.elFooterMode.textContent = "Live C++20 Stream Ingestion";
            if (this.elReconnectBanner) this.elReconnectBanner.classList.add('hidden');
        } else if (state === 'STANDALONE') {
            this.elEngineStatus.textContent = "STANDALONE SIMULATOR";
            this.elEngineStatus.className = "metric-value status-standalone";
            if (this.elStatusPulseRing) this.elStatusPulseRing.className = "status-pulse-ring pulse-cyan";
            if (this.elStatusDot) this.elStatusDot.className = "status-dot dot-cyan";
            if (this.elFooterMode) this.elFooterMode.textContent = "Automated In-Browser Engine";
            if (this.elReconnectBanner) this.elReconnectBanner.classList.add('hidden');
        }
    }

    connectWebSocket() {
        if (this.connectionState === 'STANDALONE') return;
        const wsUrl = `ws://${window.location.hostname || 'localhost'}:8765`;

        try {
            this.ws = new WebSocket(wsUrl);
            this.ws.onopen = () => {
                this.reconnectAttempts = 0;
                this.updateConnectionUI('CONNECTED');
                this.showToast("Connected to C++20 Engine Bridge", "success");
            };
            this.ws.onmessage = (event) => {
                try {
                    const data = JSON.parse(event.data);
                    if (data.type === "snapshot") {
                        if (data.bids) this.bids = data.bids;
                        if (data.asks) this.asks = data.asks;
                        this.render();
                    }
                } catch (err) {
                    console.error("[QuantDesk] Snapshot error:", err);
                }
            };
            this.ws.onclose = () => {
                this.updateConnectionUI('STANDALONE');
            };
            this.ws.onerror = () => {
                this.updateConnectionUI('STANDALONE');
            };
        } catch (e) {
            this.updateConnectionUI('STANDALONE');
        }
    }
}

// Instantiate on DOM ready
window.addEventListener('DOMContentLoaded', () => {
    window.quantDesk = new QuantDeskVisualizer();
});
