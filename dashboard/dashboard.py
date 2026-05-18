#!/usr/bin/env python3

import json
import queue
import sys
import threading

import numpy as np
import pyqtgraph as pg
import zmq
from pyqtgraph.Qt import QtCore, QtWidgets

try:
    from scipy import stats as scipy_stats
    SCIPY_AVAILABLE = True
except ImportError:
    SCIPY_AVAILABLE = False

ZMQ_DATA_ADDR = "tcp://127.0.0.1:5555"
ZMQ_CMD_ADDR  = "tcp://127.0.0.1:5556"
POLL_INTERVAL = 50
HISTORY_LEN   = 500
HIST_BINS     = 40

SYMBOLS = [
    "btcusdt", "ethusdt", "solusdt",
    "bnbusdt", "xrpusdt", "dogeusdt",
    "adausdt", "avaxusdt",
]

BG_COLOR      = "#0d1117"
PANEL_COLOR   = "#161b22"
GRID_COLOR    = "#21262d"
BID_COLOR     = "#3fb950"
ASK_COLOR     = "#f85149"
PRICE_COLOR   = "#58a6ff"
TEXT_COLOR    = "#e6edf3"
ACCENT_COLOR  = "#d2a8ff"
WARN_COLOR    = "#f0883e"
KALMAN_COLOR  = "#79c0ff"
INNOV_COLOR   = "#ffa657"
LAMBDA_COLOR  = "#ff7b72"
VOL_COLOR     = "#a5d6ff"
COMPOSITE_POS = "#3fb950"
COMPOSITE_NEG = "#f85149"
ZSCORE_COLOR  = "#e3b341"


def make_pen(color, width=1.5):
    return pg.mkPen(color, width=width)


class ZmqThread(threading.Thread):

    def __init__(self, q: queue.Queue):
        super().__init__(daemon=True)
        self.q = q
        self._stop = threading.Event()

    def run(self):
        ctx  = zmq.Context()
        sock = ctx.socket(zmq.SUB)
        sock.setsockopt_string(zmq.SUBSCRIBE, "")
        sock.connect(ZMQ_DATA_ADDR)

        poller = zmq.Poller()
        poller.register(sock, zmq.POLLIN)

        while not self._stop.is_set():
            evts = dict(poller.poll(timeout=100))
            if sock in evts:
                raw = sock.recv_string()
                try:
                    data = json.loads(raw)
                    # drop stale messages if we're falling behind
                    if self.q.qsize() > 20:
                        try:
                            self.q.get_nowait()
                        except queue.Empty:
                            pass
                    self.q.put_nowait(data)
                except json.JSONDecodeError:
                    pass

        sock.close()
        ctx.term()

    def stop(self):
        self._stop.set()


class RollingBuffer:
    def __init__(self, maxlen: int):
        self._buf  = np.zeros(maxlen)
        self._n    = 0
        self._head = 0
        self._cap  = maxlen

    def push(self, v: float):
        self._buf[self._head] = v
        self._head = (self._head + 1) % self._cap
        if self._n < self._cap:
            self._n += 1

    def get(self) -> np.ndarray:
        if self._n < self._cap:
            return self._buf[:self._n].copy()
        return np.roll(self._buf, -self._head)

    def __len__(self):
        return self._n


class Dashboard:

    def __init__(self):
        self.app = QtWidgets.QApplication.instance() or QtWidgets.QApplication(sys.argv)

        pg.setConfigOption("background", BG_COLOR)
        pg.setConfigOption("foreground", TEXT_COLOR)
        pg.setConfigOption("antialias",  True)

        # command publisher — sends config changes to C++ engine
        self._cmd_ctx  = zmq.Context()
        self._cmd_sock = self._cmd_ctx.socket(zmq.PUB)
        self._cmd_sock.bind(ZMQ_CMD_ADDR)

        self._active_symbol = "btcusdt"
        self._active_dt_ms  = 500

        self.win = QtWidgets.QMainWindow()
        self.win.setWindowTitle("HFT Engine — Microstructure Dashboard")
        self.win.resize(1600, 960)
        self.win.setStyleSheet(f"background-color: {BG_COLOR};")

        central = QtWidgets.QWidget()
        self.win.setCentralWidget(central)
        root = QtWidgets.QVBoxLayout(central)
        root.setContentsMargins(12, 12, 12, 12)
        root.setSpacing(8)

        # ── title + control bar row ───────────────────────────────────────────
        header = QtWidgets.QHBoxLayout()
        header.setSpacing(14)

        self.title_label = QtWidgets.QLabel("⚡  BTC/USDT  ·  Real-Time Microstructure")
        self.title_label.setStyleSheet(
            f"color: {TEXT_COLOR}; font-size: 17px; font-weight: bold;"
            "font-family: 'Consolas', 'Courier New', monospace;"
        )
        header.addWidget(self.title_label)
        header.addStretch()

        ctrl_style = (
            f"background-color: {PANEL_COLOR}; color: {TEXT_COLOR};"
            f"border: 1px solid {GRID_COLOR}; border-radius: 4px;"
            "font-family: Consolas; font-size: 13px; padding: 4px 8px;"
        )

        lbl_sym = QtWidgets.QLabel("Symbol")
        lbl_sym.setStyleSheet(f"color: #8b949e; font-size: 11px; font-family: Consolas;")
        header.addWidget(lbl_sym)

        self.combo_symbol = QtWidgets.QComboBox()
        for s in SYMBOLS:
            self.combo_symbol.addItem(s.upper(), s)
        self.combo_symbol.setCurrentIndex(0)
        self.combo_symbol.setStyleSheet(ctrl_style + " min-width: 110px;")
        header.addWidget(self.combo_symbol)

        lbl_dt = QtWidgets.QLabel("Horizon (ms)")
        lbl_dt.setStyleSheet(f"color: #8b949e; font-size: 11px; font-family: Consolas;")
        header.addWidget(lbl_dt)

        self.spin_dt = QtWidgets.QSpinBox()
        self.spin_dt.setRange(100, 5000)
        self.spin_dt.setSingleStep(100)
        self.spin_dt.setValue(500)
        self.spin_dt.setSuffix(" ms")
        self.spin_dt.setStyleSheet(ctrl_style + " min-width: 90px;")
        header.addWidget(self.spin_dt)

        self.btn_apply = QtWidgets.QPushButton("Apply")
        self.btn_apply.setStyleSheet(
            f"background-color: #238636; color: {TEXT_COLOR};"
            "border: 1px solid #2ea043; border-radius: 4px;"
            "font-family: Consolas; font-size: 13px; font-weight: bold;"
            "padding: 5px 16px;"
        )
        self.btn_apply.clicked.connect(self._on_apply)
        header.addWidget(self.btn_apply)

        root.addLayout(header)

        stat_row = QtWidgets.QHBoxLayout()
        stat_row.setSpacing(12)
        self.lbl_mid       = self._stat_label("Mid Price",   "--")
        self.lbl_obi       = self._stat_label("OBI (raw)",   "--")
        self.lbl_rz        = self._stat_label("Return Z",    "--")
        self.lbl_vpin      = self._stat_label("VPIN",        "--")
        self.lbl_park      = self._stat_label("Park Vol",    "--")
        self.lbl_composite = self._stat_label("Composite",   "--")
        self.lbl_frames    = self._stat_label("Frames",      "0")
        self.lbl_conn      = self._stat_label("Status",      "Connecting…")
        for w in (self.lbl_mid, self.lbl_obi, self.lbl_rz,
                  self.lbl_vpin, self.lbl_park, self.lbl_composite,
                  self.lbl_frames, self.lbl_conn):
            stat_row.addWidget(w)
        stat_row.addStretch()
        root.addLayout(stat_row)

        row1 = QtWidgets.QHBoxLayout()
        row1.setSpacing(8)
        row2 = QtWidgets.QHBoxLayout()
        row2.setSpacing(8)

        self.p1 = self._plot("Mid-Price  (BTC/USDT)", "Price (USDT)", "Ticks")
        self.c_price = self.p1.plot(pen=make_pen(PRICE_COLOR, 2), name="Mid-Price")
        row1.addWidget(self.p1, stretch=3)

        self.p2 = self._plot("Return Z-Score  (Welford)", "Z-Score", "Ticks")
        self.c_zscore = self.p2.plot(pen=make_pen(ZSCORE_COLOR, 1.5))
        for y in (2.5, -2.5):
            tl = pg.InfiniteLine(
                pos=y, angle=0,
                pen=pg.mkPen(WARN_COLOR, width=1, style=QtCore.Qt.PenStyle.DashLine),
            )
            self.p2.addItem(tl)
        self.p2.setYRange(-5, 5, padding=0)
        row1.addWidget(self.p2, stretch=2)

        self.p3 = self._plot("Return Distribution  (last 500)", "Count", "Return")
        self.hist_bars: pg.BarGraphItem | None = None
        self.pdf_curve = self.p3.plot(pen=make_pen(ACCENT_COLOR, 2))
        row1.addWidget(self.p3, stretch=2)

        self.p4 = self._plot("Parkinson Vol  & Vol-Adj OBI", "Value", "Ticks")
        self.c_park    = self.p4.plot(pen=make_pen(VOL_COLOR, 1.5),    name="Park Vol")
        self.c_obinorm = self.p4.plot(pen=make_pen(ACCENT_COLOR, 1.5), name="OBI/Vol")
        legend4 = self.p4.addLegend(offset=(10, 10))
        legend4.setParentItem(self.p4.graphicsItem())
        row2.addWidget(self.p4, stretch=2)

        self.p5 = self._plot("VPIN  (Order Flow Toxicity)", "VPIN", "Ticks")
        self.c_vpin = self.p5.plot(pen=make_pen(WARN_COLOR, 1.5))
        vpin_thresh = pg.InfiniteLine(
            pos=0.6, angle=0,
            pen=pg.mkPen(ASK_COLOR, width=1, style=QtCore.Qt.PenStyle.DashLine),
        )
        self.p5.addItem(vpin_thresh)
        self.p5.setYRange(0, 1, padding=0.05)
        row2.addWidget(self.p5, stretch=2)

        self.p6 = self._plot("Kalman Filter  (OBI)", "OBI", "Ticks")
        self.c_obi_raw    = self.p6.plot(pen=make_pen(GRID_COLOR, 1),    name="Raw OBI")
        self.c_obi_kalman = self.p6.plot(pen=make_pen(KALMAN_COLOR, 2),  name="Kalman")
        self.c_innov      = self.p6.plot(pen=make_pen(INNOV_COLOR, 1.2), name="Innovation")
        legend6 = self.p6.addLegend(offset=(10, 10))
        legend6.setParentItem(self.p6.graphicsItem())
        row2.addWidget(self.p6, stretch=2)

        self.p7 = self._plot("Kyle's λ  (Price Impact)", "λ", "Ticks")
        self.c_lambda = self.p7.plot(pen=make_pen(LAMBDA_COLOR, 1.5))
        row2.addWidget(self.p7, stretch=2)

        root.addLayout(row1, stretch=5)
        root.addLayout(row2, stretch=5)

        self.buf_price   = RollingBuffer(HISTORY_LEN)
        self.buf_zscore  = RollingBuffer(HISTORY_LEN)
        self.buf_returns = RollingBuffer(HISTORY_LEN)
        self.buf_park    = RollingBuffer(HISTORY_LEN)
        self.buf_obinorm = RollingBuffer(HISTORY_LEN)
        self.buf_vpin    = RollingBuffer(HISTORY_LEN)
        self.buf_obi_raw = RollingBuffer(HISTORY_LEN)
        self.buf_obi_kal = RollingBuffer(HISTORY_LEN)
        self.buf_innov   = RollingBuffer(HISTORY_LEN)
        self.buf_lambda  = RollingBuffer(HISTORY_LEN)

        self.frame_count = 0

        self.data_queue = queue.Queue()
        self.zmq_thread = ZmqThread(self.data_queue)
        self.zmq_thread.start()

        self.timer = QtCore.QTimer()
        self.timer.setInterval(POLL_INTERVAL)
        self.timer.timeout.connect(self._on_tick)
        self.timer.start()

    def _stat_label(self, heading: str, value: str) -> QtWidgets.QFrame:
        frame = QtWidgets.QFrame()
        frame.setStyleSheet(
            f"background-color: {PANEL_COLOR}; border-radius: 8px;"
            f"border: 1px solid {GRID_COLOR};"
        )
        frame.setMinimumWidth(130)
        layout = QtWidgets.QVBoxLayout(frame)
        layout.setContentsMargins(10, 6, 10, 6)
        layout.setSpacing(2)

        h = QtWidgets.QLabel(heading)
        h.setStyleSheet("color: #8b949e; font-size: 10px; font-family: Consolas;")
        layout.addWidget(h)

        v = QtWidgets.QLabel(value)
        v.setStyleSheet(
            f"color: {TEXT_COLOR}; font-size: 17px; font-weight: bold;"
            "font-family: Consolas;"
        )
        v.setObjectName("value")
        layout.addWidget(v)

        frame._value_label = v  # type: ignore[attr-defined]
        return frame

    def _set_stat(self, frame: QtWidgets.QFrame, text: str, color: str = TEXT_COLOR):
        frame._value_label.setText(text)          # type: ignore[attr-defined]
        frame._value_label.setStyleSheet(         # type: ignore[attr-defined]
            f"color: {color}; font-size: 17px; font-weight: bold;"
            "font-family: Consolas;"
        )

    def _plot(self, title: str, left: str, bottom: str) -> pg.PlotWidget:
        pw = pg.PlotWidget(title=title)
        pw.setBackground(PANEL_COLOR)
        pw.showGrid(x=True, y=True, alpha=0.12)
        pw.setLabel("left",   left,   color=TEXT_COLOR)
        pw.setLabel("bottom", bottom, color=TEXT_COLOR)
        pw.getPlotItem().titleLabel.setText(
            f'<span style="color:{TEXT_COLOR}; font-size:12px;">{title}</span>'
        )
        for axis in ("left", "bottom"):
            pw.getAxis(axis).setTextPen(pg.mkPen(TEXT_COLOR))
            pw.getAxis(axis).setPen(pg.mkPen(GRID_COLOR))
        return pw

    def _on_tick(self):
        latest = None
        while not self.data_queue.empty():
            try:
                latest = self.data_queue.get_nowait()
            except queue.Empty:
                break
        if latest is None:
            return
        self.frame_count += 1
        self._update(latest)

    def _update(self, d: dict):
        mid       = d.get("mid_price",       0.0)
        obi       = d.get("obi",             0.0)
        ret       = d.get("ret",             0.0)
        rz        = d.get("return_zscore",   0.0)
        park_vol  = d.get("park_vol",        0.0)
        obi_norm  = d.get("obi_normalized",  0.0)
        vpin      = d.get("vpin",            0.0)
        obi_kal   = d.get("obi_kalman",      0.0)
        innov     = d.get("kalman_innovation", 0.0)
        lam       = d.get("kyle_lambda",     0.0)
        composite = d.get("composite",       0.0)

        self.buf_price.push(mid)
        self.buf_zscore.push(rz)
        self.buf_returns.push(ret)
        self.buf_park.push(park_vol)
        self.buf_obinorm.push(obi_norm)
        self.buf_vpin.push(vpin)
        self.buf_obi_raw.push(obi)
        self.buf_obi_kal.push(obi_kal)
        self.buf_innov.push(innov)
        self.buf_lambda.push(lam)

        self._set_stat(self.lbl_mid, f"${mid:,.2f}", PRICE_COLOR)

        obi_col  = BID_COLOR if obi >= 0 else ASK_COLOR
        sign_str = "+" if obi >= 0 else ""
        self._set_stat(self.lbl_obi, f"{sign_str}{obi:.4f}", obi_col)

        rz_col = (BID_COLOR if abs(rz) < 2.5
                  else WARN_COLOR if abs(rz) < 3.5
                  else ASK_COLOR)
        self._set_stat(self.lbl_rz, f"{rz:+.2f}σ", rz_col)

        vpin_col = BID_COLOR if vpin < 0.4 else WARN_COLOR if vpin < 0.6 else ASK_COLOR
        self._set_stat(self.lbl_vpin, f"{vpin:.3f}", vpin_col)
        self._set_stat(self.lbl_park, f"{park_vol:.5f}", VOL_COLOR)

        comp_col = COMPOSITE_POS if composite > 0 else COMPOSITE_NEG
        self._set_stat(self.lbl_composite, f"{composite:+.3f}", comp_col)
        self._set_stat(self.lbl_frames, str(self.frame_count))
        self._set_stat(self.lbl_conn, "● Live", BID_COLOR)

        n = len(self.buf_price)
        x = np.arange(n, dtype=np.float64)

        y1 = self.buf_price.get()
        self.c_price.setData(x, y1)
        if len(y1) > 1:
            pad = max((y1.max() - y1.min()) * 0.1, 1.0)
            self.p1.setYRange(y1.min() - pad, y1.max() + pad, padding=0)

        y2 = self.buf_zscore.get()
        self.c_zscore.setData(np.arange(len(y2), dtype=np.float64), y2)

        rets = self.buf_returns.get()
        if len(rets) > 10:
            counts, edges = np.histogram(rets, bins=HIST_BINS)
            centers = (edges[:-1] + edges[1:]) / 2
            width   = edges[1] - edges[0]

            if self.hist_bars is not None:
                self.p3.removeItem(self.hist_bars)
            self.hist_bars = pg.BarGraphItem(
                x=centers, height=counts, width=width * 0.85,
                brush=pg.mkBrush(ZSCORE_COLOR + "55"), pen=pg.mkPen(ZSCORE_COLOR),
            )
            self.p3.addItem(self.hist_bars)

            if SCIPY_AVAILABLE and len(rets) > 30:
                try:
                    df, loc, scale = scipy_stats.t.fit(rets, f0=4)
                except Exception:
                    df, loc, scale = 4, float(np.mean(rets)), float(np.std(rets))
                x_pdf = np.linspace(edges[0], edges[-1], 200)
                y_pdf = scipy_stats.t.pdf(x_pdf, df, loc, scale) * len(rets) * width
                self.pdf_curve.setData(x_pdf, y_pdf)
            else:
                self.pdf_curve.setData([], [])

        y_park   = self.buf_park.get()
        y_obinrm = self.buf_obinorm.get()
        self.c_park.setData(np.arange(len(y_park), dtype=np.float64), y_park)
        self.c_obinorm.setData(np.arange(len(y_obinrm), dtype=np.float64), y_obinrm)

        y5 = self.buf_vpin.get()
        self.c_vpin.setData(np.arange(len(y5), dtype=np.float64), y5)

        y_raw = self.buf_obi_raw.get()
        y_kal = self.buf_obi_kal.get()
        y_inn = self.buf_innov.get()
        self.c_obi_raw.setData(np.arange(len(y_raw), dtype=np.float64), y_raw)
        self.c_obi_kalman.setData(np.arange(len(y_kal), dtype=np.float64), y_kal)
        self.c_innov.setData(np.arange(len(y_inn), dtype=np.float64), y_inn)

        y7 = self.buf_lambda.get()
        self.c_lambda.setData(np.arange(len(y7), dtype=np.float64), y7)

    # ── config change handler ──────────────────────────────────────────────

    def _on_apply(self):
        new_sym = self.combo_symbol.currentData()
        new_dt  = self.spin_dt.value()

        cmd = json.dumps({
            "command": "update_config",
            "symbol":  new_sym,
            "dt_ms":   new_dt,
        })
        self._cmd_sock.send_string(cmd)

        self._active_symbol = new_sym
        self._active_dt_ms  = new_dt

        # update title to reflect new symbol
        pretty = new_sym.upper().replace("USDT", "/USDT")
        self.title_label.setText(f"⚡  {pretty}  ·  Real-Time Microstructure")

        # purge all rolling buffers so stale data doesn't mix with the new feed
        for buf in (self.buf_price, self.buf_zscore, self.buf_returns,
                    self.buf_park, self.buf_obinorm, self.buf_vpin,
                    self.buf_obi_raw, self.buf_obi_kal, self.buf_innov,
                    self.buf_lambda):
            buf._n    = 0
            buf._head = 0
            buf._buf[:] = 0.0

        # clear all plot curves immediately
        self.c_price.setData([], [])
        self.c_zscore.setData([], [])
        self.c_park.setData([], [])
        self.c_obinorm.setData([], [])
        self.c_vpin.setData([], [])
        self.c_obi_raw.setData([], [])
        self.c_obi_kalman.setData([], [])
        self.c_innov.setData([], [])
        self.c_lambda.setData([], [])
        self.pdf_curve.setData([], [])
        if self.hist_bars is not None:
            self.p3.removeItem(self.hist_bars)
            self.hist_bars = None

        self.frame_count = 0
        self._set_stat(self.lbl_frames, "0")
        self._set_stat(self.lbl_conn, "● Switching…", WARN_COLOR)

        print(f"[CMD] Sent: {cmd}")

    def run(self):
        self.win.show()
        code = self.app.exec()
        self.zmq_thread.stop()
        self.zmq_thread.join(timeout=2)
        self._cmd_sock.close()
        self._cmd_ctx.term()
        sys.exit(code)


def main():
    db = Dashboard()
    db.run()


if __name__ == "__main__":
    main()
