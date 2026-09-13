# Project Instructions & Agent Guidelines

## 1. UI & Styling Guidelines (MANDATORY)

> [!IMPORTANT]
> **Always read [DESIGN.md](file:///d:/Quant/DESIGN.md) in the project root BEFORE making any modifications, additions, or refactors to the UI, HTML templates, CSS stylesheets, or visual components.**

When performing UI work on this project:
- **Design System Reference**: Follow [DESIGN.md](file:///d:/Quant/DESIGN.md) for all color tokens, typography, component styling, layout grids, and interaction states.
- **Design System Stylesheet**: The core stylesheet is located at [`web/style.css`](file:///d:/Quant/web/style.css). Always reuse established CSS variables (`var(--color-bid)`, `var(--color-ask)`, `var(--bg-main)`, `var(--bg-panel)`, etc.) instead of hardcoded hex values.
- **Typography**: Strictly use `'JetBrains Mono', monospace` (`--font-mono`) for all tabular data, prices, quantities, timestamps, and order books. Use `'Outfit', sans-serif` (`--font-sans`) for headings and branding.
- **Accessibility & Themes**: Preserve compatibility with both the default Dark Theme (`body.dark-theme`) and the Colorblind / Accessible Theme (`body.colorblind-theme`).
- **Conflict Prevention**: If introducing external styles or new CSS modules, import them cleanly without overriding core variables or breaking companion stylesheets.

---

## 2. Architecture & Tech Stack

- **C++20 Engine**: Located in `src/` and `include/`, ultra-low latency lock-free limit order book engine.
- **Python Bridge**: Located in `bridge.py` and `server.py`, asyncio WebSocket and REST bridge.
- **Web Frontend**: Located in `web/` (`index.html`, `style.css`, `app.js`) with a root redirect `index.html`.
