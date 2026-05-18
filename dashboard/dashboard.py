#!/usr/bin/env python3
"""
Step 5 — Real-Time HFT Dashboard
Subscribes to the ZMQ stream from the C++ engine and renders:
  - Visual 1: Live Mid-Price line chart
  - Visual 2: Bid vs Ask bar chart (top 5 levels)
  - Visual 3: OBI gauge (text metric)
"""

import json
import queue
import sys
import threading
import time

import numpy as np
import pyqtgraph as pg
import zmq
from pyqtgraph.Qt import QtCore, QtWidgets

# ── Config ───────────────────────────────────────────────────────────────────
ZMQ_ADDRESS   = "tcp://127.0.0.1:5555"
POLL_INTERVAL = 50          # ms — QTimer tick
HISTORY_LEN   = 300         # number of mid-price samples to keep

# ── Colour palette ────────────────────────────────────────────────────────────
BG_COLOR      = "#0d1117"
PANEL_COLOR   = "#161b22"
GRID_COLOR    = "#21262d"
BID_COLOR     = "#3fb950"   # green
ASK_COLOR     = "#f85149"   # red
PRICE_COLOR   = "#58a6ff"   # blue
TEXT_COLOR    = "#e6edf3"
ACCENT_COLOR  = "#d2a8ff"   # purple for OBI


# ─────────────────────────────────────────────────────────────────────────────
# Background ZMQ reader thread
# ─────────────────────────────────────────────────────────────────────────────
class ZmqThread(threading.Thread):
    """Daemon thread: reads ZMQ messages and stuffs them into a queue."""

    def __init__(self, data_queue: queue.Queue):
        super().__init__(daemon=True)
        self.data_queue = data_queue
        self._stop_event = threading.Event()

    def run(self):
        ctx = zmq.Context()
        sock = ctx.socket(zmq.SUB)
        sock.setsockopt_string(zmq.SUBSCRIBE, "")
        sock.connect(ZMQ_ADDRESS)

        poller = zmq.Poller()
        poller.register(sock, zmq.POLLIN)

        while not self._stop_event.is_set():
            events = dict(poller.poll(timeout=100))
            if sock in events:
                raw = sock.recv_string()
                try:
                    data = json.loads(raw)
                    # Drop old frames so the queue never grows unbounded
                    if self.data_queue.qsize() > 20:
                        try:
                            self.data_queue.get_nowait()
                        except queue.Empty:
                            pass
                    self.data_queue.put_nowait(data)
                except json.JSONDecodeError:
                    pass

        sock.close()
        ctx.term()

    def stop(self):
        self._stop_event.set()


# ─────────────────────────────────────────────────────────────────────────────
# Main dashboard window
# ─────────────────────────────────────────────────────────────────────────────
class Dashboard:
    def __init__(self):
        # ── App ──────────────────────────────────────────────────────────────
        self.app = QtWidgets.QApplication.instance() or QtWidgets.QApplication(sys.argv)

        pg.setConfigOption("background", BG_COLOR)
        pg.setConfigOption("foreground", TEXT_COLOR)
        pg.setConfigOption("antialias", True)

        # ── Main window ──────────────────────────────────────────────────────
        self.win = QtWidgets.QMainWindow()
        self.win.setWindowTitle("HFT Engine — Live Dashboard")
        self.win.resize(1280, 780)
        self.win.setStyleSheet(f"background-color: {BG_COLOR};")

        central = QtWidgets.QWidget()
        self.win.setCentralWidget(central)
        root_layout = QtWidgets.QVBoxLayout(central)
        root_layout.setContentsMargins(12, 12, 12, 12)
        root_layout.setSpacing(10)

        # ── Title bar ────────────────────────────────────────────────────────
        title = QtWidgets.QLabel("⚡  BTC/USDT  ·  Real-Time Microstructure Dashboard")
        title.setStyleSheet(
            f"color: {TEXT_COLOR}; font-size: 18px; font-weight: bold;"
            "font-family: 'Consolas', 'Courier New', monospace;"
        )
        root_layout.addWidget(title)

        # ── OBI / stat bar ───────────────────────────────────────────────────
        stat_row = QtWidgets.QHBoxLayout()
        stat_row.setSpacing(20)
        self.lbl_mid   = self._stat_label("Mid Price",  "--")
        self.lbl_obi   = self._stat_label("OBI",        "--")
        self.lbl_msgs  = self._stat_label("Frames",     "0")
        self.lbl_conn  = self._stat_label("Status",     "Connecting…")
        for w in (self.lbl_mid, self.lbl_obi, self.lbl_msgs, self.lbl_conn):
            stat_row.addWidget(w)
        stat_row.addStretch()
        root_layout.addLayout(stat_row)

        # ── Charts container ─────────────────────────────────────────────────
        charts_layout = QtWidgets.QHBoxLayout()
        charts_layout.setSpacing(10)
        root_layout.addLayout(charts_layout)

        # Visual 1 — Mid-Price line chart (left, wider)
        self.price_plot = self._make_plot_widget(
            title="Mid-Price  (BTC/USDT)",
            left_label="Price (USDT)",
            bottom_label="Samples",
        )
        self.price_curve = self.price_plot.plot(
            pen=pg.mkPen(PRICE_COLOR, width=2),
            name="Mid-Price",
        )
        charts_layout.addWidget(self.price_plot, stretch=3)

        # Visual 2 — Bid vs Ask bar chart (right)
        self.book_plot = self._make_plot_widget(
            title="Order Book Depth  (Top 5 Levels)",
            left_label="Qty",
            bottom_label="Price Level",
        )
        # We'll manage bars manually via BarGraphItem
        self.bid_bars: pg.BarGraphItem | None = None
        self.ask_bars: pg.BarGraphItem | None = None
        charts_layout.addWidget(self.book_plot, stretch=2)

        # ── State ────────────────────────────────────────────────────────────
        self.mid_history: list[float] = []
        self.frame_count  = 0

        # ── Data queue & ZMQ thread ──────────────────────────────────────────
        self.data_queue = queue.Queue()
        self.zmq_thread = ZmqThread(self.data_queue)
        self.zmq_thread.start()

        # ── QTimer ───────────────────────────────────────────────────────────
        self.timer = QtCore.QTimer()
        self.timer.setInterval(POLL_INTERVAL)
        self.timer.timeout.connect(self._on_tick)
        self.timer.start()

    # ── Helpers ──────────────────────────────────────────────────────────────

    def _stat_label(self, heading: str, value: str) -> QtWidgets.QFrame:
        """Returns a framed stat widget (heading + value)."""
        frame = QtWidgets.QFrame()
        frame.setStyleSheet(
            f"background-color: {PANEL_COLOR}; border-radius: 8px;"
            f"border: 1px solid {GRID_COLOR};"
        )
        frame.setMinimumWidth(160)
        layout = QtWidgets.QVBoxLayout(frame)
        layout.setContentsMargins(12, 8, 12, 8)
        layout.setSpacing(2)

        h = QtWidgets.QLabel(heading)
        h.setStyleSheet(f"color: #8b949e; font-size: 11px; font-family: Consolas;")
        layout.addWidget(h)

        v = QtWidgets.QLabel(value)
        v.setStyleSheet(
            f"color: {TEXT_COLOR}; font-size: 20px; font-weight: bold;"
            "font-family: Consolas;"
        )
        v.setObjectName("value")
        layout.addWidget(v)

        # Store reference to value label inside frame
        frame._value_label = v  # type: ignore[attr-defined]
        return frame

    def _set_stat(self, frame: QtWidgets.QFrame, text: str, color: str = TEXT_COLOR):
        frame._value_label.setText(text)           # type: ignore[attr-defined]
        frame._value_label.setStyleSheet(          # type: ignore[attr-defined]
            f"color: {color}; font-size: 20px; font-weight: bold;"
            "font-family: Consolas;"
        )

    def _make_plot_widget(
        self, title: str, left_label: str, bottom_label: str
    ) -> pg.PlotWidget:
        pw = pg.PlotWidget(title=title)
        pw.setBackground(PANEL_COLOR)
        pw.showGrid(x=True, y=True, alpha=0.15)
        pw.setLabel("left",   left_label,   color=TEXT_COLOR)
        pw.setLabel("bottom", bottom_label, color=TEXT_COLOR)
        pw.getPlotItem().titleLabel.setText(
            f'<span style="color:{TEXT_COLOR}; font-size:13px;">{title}</span>'
        )
        # Style axes
        for axis in ("left", "bottom"):
            pw.getAxis(axis).setTextPen(pg.mkPen(TEXT_COLOR))
            pw.getAxis(axis).setPen(pg.mkPen(GRID_COLOR))
        return pw

    # ── Timer tick ───────────────────────────────────────────────────────────

    def _on_tick(self):
        """Drain the queue and update all visuals."""
        latest: dict | None = None

        # Drain — keep only the newest frame from this tick
        while not self.data_queue.empty():
            try:
                latest = self.data_queue.get_nowait()
            except queue.Empty:
                break

        if latest is None:
            return

        self.frame_count += 1
        self._update_visuals(latest)

    def _update_visuals(self, data: dict):
        mid_price: float = data.get("mid_price", 0.0)
        obi: float       = data.get("obi", 0.0)
        bids: list       = data.get("bids", [])
        asks: list       = data.get("asks", [])

        # ── Stat bar ──────────────────────────────────────────────────────────
        self._set_stat(self.lbl_mid,  f"${mid_price:,.2f}", PRICE_COLOR)

        obi_color = BID_COLOR if obi >= 0 else ASK_COLOR
        sign      = "+" if obi >= 0 else ""
        self._set_stat(self.lbl_obi,  f"{sign}{obi:.4f}", obi_color)
        self._set_stat(self.lbl_msgs, str(self.frame_count))
        self._set_stat(self.lbl_conn, "● Live", BID_COLOR)

        # ── Visual 1: Mid-Price history ───────────────────────────────────────
        self.mid_history.append(mid_price)
        if len(self.mid_history) > HISTORY_LEN:
            self.mid_history = self.mid_history[-HISTORY_LEN:]
        y = np.array(self.mid_history, dtype=np.float64)
        x = np.arange(len(y), dtype=np.float64)
        self.price_curve.setData(x, y)
        # Auto-zoom Y to ±0.5 of current range so chart doesn't drift wildly
        if len(y) > 1:
            pad = max((y.max() - y.min()) * 0.1, 1.0)
            self.price_plot.setYRange(y.min() - pad, y.max() + pad, padding=0)

        # ── Visual 2: Order book depth bars ───────────────────────────────────
        if not bids or not asks:
            return

        # Top 5 bid prices (descending) and ask prices (ascending)
        bid_prices = np.array([b["price"] for b in bids[:5]], dtype=np.float64)
        bid_qtys   = np.array([b["qty"]   for b in bids[:5]], dtype=np.float64)
        ask_prices = np.array([a["price"] for a in asks[:5]], dtype=np.float64)
        ask_qtys   = np.array([a["qty"]   for a in asks[:5]], dtype=np.float64)

        # Remove old bars
        if self.bid_bars is not None:
            self.book_plot.removeItem(self.bid_bars)
        if self.ask_bars is not None:
            self.book_plot.removeItem(self.ask_bars)

        bar_w = 0.3  # tick spacing is typically $0.01; keep bars thin
        self.bid_bars = pg.BarGraphItem(
            x=bid_prices - bar_w / 2, height=bid_qtys,
            width=bar_w, brush=BID_COLOR, pen=pg.mkPen(None),
        )
        self.ask_bars = pg.BarGraphItem(
            x=ask_prices + bar_w / 2, height=ask_qtys,
            width=bar_w, brush=ASK_COLOR, pen=pg.mkPen(None),
        )
        self.book_plot.addItem(self.bid_bars)
        self.book_plot.addItem(self.ask_bars)

        all_prices = np.concatenate([bid_prices, ask_prices])
        spread     = max(all_prices.max() - all_prices.min(), 0.05)
        self.book_plot.setXRange(
            all_prices.min() - spread * 0.5,
            all_prices.max() + spread * 0.5,
            padding=0,
        )

    # ── Run ──────────────────────────────────────────────────────────────────

    def run(self):
        self.win.show()
        exit_code = self.app.exec()
        self.zmq_thread.stop()
        self.zmq_thread.join(timeout=2)
        sys.exit(exit_code)


# ─────────────────────────────────────────────────────────────────────────────

def main():
    db = Dashboard()
    db.run()


if __name__ == "__main__":
    main()
