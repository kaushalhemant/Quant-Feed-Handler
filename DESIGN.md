# QuantDesk Design System & UI Architecture (DESIGN.md)

This document defines the visual design system, token specifications, typography, component patterns, interaction states, and accessibility standards for the **QuantDesk** High-Frequency Trading (HFT) Limit Order Book visualizer.

> **MANDATORY INSTRUCTION FOR AGENTS & DEVELOPERS:**
> Always read and adhere to `DESIGN.md` before making any changes, refactors, or extensions to the user interface, CSS stylesheets, or visual components.

---

## 1. Visual Philosophy & Design Principles

1. **Institutional HFT Terminal Aesthetic**:
   - Dark, low-glare, high-contrast visual environment optimized for long trading sessions and dense data density.
   - Precision numeric presentation where numbers are tabular, aligned, and easily scanned.

2. **Ultra-Low Latency & Real-Time Feedback**:
   - Every user action (order injection, cancel, burst replay, stock switch) gives immediate visual feedback via micro-animations, flash cues, or status transitions.
   - Micro-animations (e.g., depth bar updates, pulse rings, toast slide-ins) are hardware-accelerated (`transform`, `opacity`) to ensure 60+ FPS rendering under high-throughput data streams.

3. **Accessibility First (Dual-Theme System)**:
   - Default Dark Theme with high-contrast neon green (`#00e676`) for Bids and neon crimson (`#ff3366`) for Asks.
   - Accessible Theme (Colorblind Deuteranopia/Protanopia Safe) with Blue (`#2196f3`) for Bids and Amber/Orange (`#ff9800`) for Asks.

---

## 2. Typography & Fonts

All typography uses curated Google Fonts loaded via `<link>` in `web/index.html`:

```html
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=JetBrains+Mono:ital,wght@0,300;0,400;0,500;0,700;1,400&family=Outfit:wght@400;500;600;700;800&display=swap" rel="stylesheet">
```

### Font Roles
- **Numeric & Technical Data (`--font-mono`)**: `'JetBrains Mono', monospace`
  - Used for order book price levels, quantities, cumulative volumes, time & sales tape, timestamps, session tokens, latency percentiles, and code snippets.
  - Tabular figures provide strict vertical alignment.
- **Brand & Headings (`--font-sans`)**: `'Outfit', -apple-system, BlinkMacSystemFont, sans-serif`
  - Used for application headers, panel titles, modal headings, and high-impact badges.

---

## 3. Design Tokens & Color Palette

All core tokens are declared in `:root` in `web/style.css`:

### Surface & Background Tokens
| Token | Value | Description |
|---|---|---|
| `--bg-main` | `#07090e` | Master canvas background |
| `--bg-panel` | `#0d111a` | Panel and terminal header surface |
| `--bg-subpanel` | `#121824` | Nested metric cards, input wells, cards |
| `--bg-row-hover` | `#192233` | Table row and interactive hover state |

### Border Tokens
| Token | Value | Description |
|---|---|---|
| `--border-subtle` | `rgba(255, 255, 255, 0.08)` | Subtle panel and divider borders |
| `--border-accent` | `rgba(0, 240, 255, 0.25)` | Focused, active, or highlighted element borders |

### Text Color Tokens
| Token | Value | Description |
|---|---|---|
| `--text-primary` | `#e6edf3` | High-contrast body text and headers |
| `--text-secondary` | `#8b949e` | Secondary labels, descriptions, and hints |
| `--text-muted` | `#545d68` | Inactive icons, timestamps, placeholders |

### Market Direction Tokens (Default Dark Theme)
| Token | Value | Description |
|---|---|---|
| `--color-bid` | `#00e676` | Buy / Bid price text & accents |
| `--color-bid-bg` | `rgba(0, 230, 118, 0.12)` | Bid depth bar fill |
| `--color-bid-glow` | `rgba(0, 230, 118, 0.35)` | Bid row flash glow |
| `--color-ask` | `#ff3366` | Sell / Ask price text & accents |
| `--color-ask-bg` | `rgba(255, 51, 102, 0.12)` | Ask depth bar fill |
| `--color-ask-glow` | `rgba(255, 51, 102, 0.35)` | Ask row flash glow |

### Market Direction Tokens (Accessible Colorblind Mode)
When `.colorblind-theme` is applied to `<body>`:
| Token | Value | Description |
|---|---|---|
| `--color-bid` | `#2196f3` | High-contrast blue for Bids |
| `--color-bid-bg` | `rgba(33, 150, 243, 0.16)` | Blue depth bar fill |
| `--color-bid-glow` | `rgba(33, 150, 243, 0.4)` | Blue glow |
| `--color-ask` | `#ff9800` | High-contrast amber/orange for Asks |
| `--color-ask-bg` | `rgba(255, 152, 0, 0.16)` | Amber depth bar fill |
| `--color-ask-glow` | `rgba(255, 152, 0, 0.4)` | Amber glow |

### Accent Tokens
| Token | Value | Description |
|---|---|---|
| `--color-cyan` | `#00f0ff` | Primary system accent, active indicators, token glow |
| `--color-purple` | `#b388ff` | Secondary metrics, special event tags |
| `--color-yellow` | `#ffd600` | Warnings, reconnecting state, benchmark highlights |
| `--color-orange` | `#ff9100` | Mid-level alerts, banners |

### Border Radius Tokens
| Token | Value |
|---|---|
| `--radius-sm` | `4px` |
| `--radius-md` | `8px` |
| `--radius-lg` | `12px` |

---

## 4. Component Standards

### 4.1 Buttons & Controls
- **Primary CTA (`.btn-primary`)**: Cyan background (`#00f0ff`), dark text (`#07090e`), bold font, subtle cyan glow shadow.
- **Outline / Filter (`.btn-outline`)**: Transparent background, 1px cyan border, cyan text.
- **Danger / Reset (`.btn-danger`)**: Crimson background/border (`#ff3366`), white text.
- **Secondary / Action (`.btn-secondary`)**: Charcoal background (`#1a2233`), subtle border, text `#e6edf3`.
- **Demo Pulse CTA (`.btn-demo-pulse`)**: Animated pulsing glow to draw immediate user attention to interactive live data.
- **Icon Buttons (`.btn-icon-only`)**: Transparent background with opacity hover transitions.

### 4.2 Status Rings & Signal Dots
The global connection status uses a nested ring-and-dot component (`.status-pulse-ring` and `.status-dot`):
- **Connected / Online**: Green pulse (`#00e676`)
- **Connecting / Reconnecting**: Yellow pulse (`#ffd600`)
- **Standalone / Offline Ingestion**: Cyan pulse (`#00f0ff`)
- **Error / Disconnected**: Crimson pulse (`#ff3366`)

### 4.3 Level 2 Limit Order Book (LOB) Ladder
- Split into two mirrored sections: Asks (top, descending from highest ask down to best ask) and Bids (bottom, best bid down to lowest bid).
- Spread banner in between showing Best Bid, Spread in cents/bps, and Best Ask.
- Depth bars rendered as background fills (`linear-gradient`) proportional to cumulative depth percentage.

### 4.4 Time & Sales Tape
- Monospace rows displaying Sequence ID, Timestamp (`HH:MM:SS.mmm`), Side (`BUY`/`SELL`), Price, Quantity, Order ID, and Ingestion Mode (`USER`/`REPLAY`).
- Dynamic flash animation on new trade arrivals.

### 4.5 Toast Notification System
- Container fixed at top right (`#toast-container`).
- Four severity classes: `.toast-success`, `.toast-error`, `.toast-info`, `.toast-warning`.
- Backdrop filter blur with slide-in animation.

### 4.6 Reconnect & Status Banners
- Top-mounted sliding notification (`.reconnect-banner`) with action buttons (`Retry Now`, `Use Standalone Mode`, `Dismiss`).

---

## 5. Responsive Grid Layout

The dashboard utilizes CSS Grid (`.dashboard-grid`):
```css
.dashboard-grid {
    display: grid;
    grid-template-columns: 380px 1fr 420px;
    gap: 12px;
    padding: 12px 20px;
    min-height: calc(100vh - 65px);
}
```

### Breakpoints
- **Wide Screens (>1280px)**: 3-column layout (LOB Book Ladder, Central Depth Chart & Analytics, Right Ingestion Studio & Time & Sales Tape).
- **Medium Screens (1024px – 1280px)**: 2-column layout (Book ladder on left, Center and Right stacked on right).
- **Compact & Mobile (<1024px)**: Single column stacked layout with auto vertical scrolling.

---

## 6. Rules for Making UI Changes

1. **Always preserve `:root` token inheritance**: Do not hardcode ad-hoc hex colors into components. Always reference CSS variables (`var(--bg-panel)`, `var(--color-bid)`, `var(--color-ask)`, etc.).
2. **Support Accessible Theme**: Ensure any new trade/order/price visualizers respect `.colorblind-theme` overrides.
3. **Preserve Tabular Alignment**: All financial, volume, order ID, and latency numbers must use `--font-mono`.
4. **Hardware Acceleration**: Use CSS `transform` and `opacity` for high-frequency DOM updates.
5. **Conflict Resolution**: If a new stylesheet or library is imported, resolve class naming collisions before altering existing component classes.
