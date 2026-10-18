import json
import os
import re
import sys
import threading
import time
import zipfile
import urllib.request
import tkinter as tk
from tkinter import filedialog, messagebox, colorchooser
from tkinter import font as tkfont
from tkinter import ttk

APP_TITLE = "Advanced Overlay Teleprompter v7.11"

# --- UI font sizing (v7.11) -----------------------------------------------
# Centralized so the whole control panel scales together. Bumped up from
# the implicit default of 8-9pt to sizes that are comfortable on modern
# high-DPI displays.
UI_FONT_FAMILY = "Segoe UI"
UI_FONT_BASE = 11           # default body text
UI_FONT_BTN = 11            # button labels
UI_FONT_BTN_TOP = 12        # top-row primary buttons (slightly larger)
UI_FONT_HEADER = 13         # panel headers ("Playback", "Voice", etc.)
UI_FONT_SMALL = 10          # hint / status text
UI_FONT_TIMER = 32          # the big MM:SS readout
SETTINGS_FILE = "teleprompter_settings.json"
DEBUG_LOG_FILE = "teleprompter_debug.log"


def _debug_log(msg):
    """Append a debug line to the log file next to the script. Safe to call
    from any thread. Used primarily for click-through diagnostics."""
    try:
        path = os.path.join(
            os.path.dirname(os.path.abspath(__file__)),
            DEBUG_LOG_FILE
        )
        with open(path, "a", encoding="utf-8") as f:
            f.write(f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] {msg}\n")
    except Exception:
        pass

try:
    import speech_recognition as sr
    SPEECH_OK = True
except Exception:
    sr = None
    SPEECH_OK = False

try:
    import sounddevice as sd
    SOUNDDEVICE_OK = True
except Exception:
    sd = None
    SOUNDDEVICE_OK = False

# Vosk is an offline streaming speech recognizer. It returns interim (partial)
# results as you speak, which is crucial for low-latency teleprompter
# following. Without streaming, there's an inherent floor of ~1-2 seconds of
# lag no matter how much we optimize the batch path.
try:
    import vosk
    # Quiet Vosk's default log chatter; we don't need it in the app.
    try:
        vosk.SetLogLevel(-1)
    except Exception:
        pass
    VOSK_OK = True
except Exception:
    vosk = None
    VOSK_OK = False

# The small English model, which is ~40MB zipped. Good accuracy for most
# teleprompter use cases. Larger models exist (gigaword, etc.) but are
# overkill and much slower to load.
VOSK_MODEL_URL = "https://alphacephei.com/vosk/models/vosk-model-small-en-us-0.15.zip"
VOSK_MODEL_DIR_NAME = "vosk-model-small-en-us-0.15"

# Detect PyAudio at import time so the status label can report it accurately.
# We try to instantiate a PyAudio object because a successful `import pyaudio`
# does NOT guarantee PortAudio can actually open a stream on this machine.
PYAUDIO_OK = False
PYAUDIO_ERR = ""
try:
    import pyaudio as _pyaudio_probe
    try:
        _pa = _pyaudio_probe.PyAudio()
        _pa.terminate()
        PYAUDIO_OK = True
    except Exception as _e:
        PYAUDIO_ERR = f"PyAudio imported but failed to init: {_e}"
except Exception as _e:
    PYAUDIO_ERR = f"PyAudio not importable: {_e}"

try:
    import ctypes
    from ctypes import wintypes
    WINDOWS_OK = True
except Exception:
    ctypes = None
    wintypes = None
    WINDOWS_OK = False

GWL_EXSTYLE = -20
WS_EX_LAYERED = 0x00080000
WS_EX_TRANSPARENT = 0x00000020


class CollapsibleSection(tk.Frame):
    def __init__(self, parent, title, bg="#181818", fg="white", open_by_default=True):
        super().__init__(parent, bg=bg, bd=1, relief="groove")
        self.is_open = tk.BooleanVar(value=open_by_default)

        header = tk.Frame(self, bg=bg)
        header.pack(fill="x")

        arrow = "▼" if open_by_default else "►"
        self.btn = tk.Button(
            header,
            text=f"{arrow} {title}",
            anchor="w",
            bg="#222222",
            fg=fg,
            activebackground="#333333",
            activeforeground=fg,
            relief="flat",
            font=(UI_FONT_FAMILY, UI_FONT_HEADER, "bold"),
            padx=8,
            pady=6,
            command=self.toggle
        )
        self.btn.pack(fill="x")

        self.body = tk.Frame(self, bg=bg)
        if open_by_default:
            self.body.pack(fill="x", expand=True)

    def toggle(self):
        if self.is_open.get():
            self.body.pack_forget()
            self.is_open.set(False)
            self.btn.config(text=self.btn.cget("text").replace("▼", "►", 1))
        else:
            self.body.pack(fill="x", expand=True)
            self.is_open.set(True)
            self.btn.config(text=self.btn.cget("text").replace("►", "▼", 1))


class TeleprompterApp:
    def __init__(self, root):
        self.root = root
        self.root.withdraw()
        self.settings_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), SETTINGS_FILE)

        self.display_lines = []
        self.display_line_offsets = []
        self.active_line_index = 0
        self.scroll_position = 0.0
        self.is_scrolling = False
        self.overlay_visible = True
        self.voice_mode_running = False
        self.stop_voice_flag = False
        self.voice_thread = None
        self.click_through_enabled = tk.BooleanVar(value=False)

        # --- Word-level model for voice karaoke highlighting ---
        # words: list of dicts: {text, norm, line_idx, x, y, width, char_start, char_end}
        # where norm = lowercase tokenized form (may be empty for pure punctuation),
        # x,y are pixel positions in script-space (y is relative to top of script),
        # line_idx identifies which visual line this word belongs to.
        self.words = []
        # Index into self.words of the "current" word. Words < cursor are spoken.
        self.voice_cursor_word = 0
        # Smoothed scroll target (px), used for gentle viewport follow.
        self.scroll_target = 0.0

        self.window_transparency = tk.DoubleVar(value=0.95)
        self.font_size = tk.IntVar(value=50)
        self.line_spacing = tk.IntVar(value=16)
        self.scroll_speed = tk.DoubleVar(value=55.0)
        self.focus_line_y = tk.DoubleVar(value=0.62)
        # NEW in v7.10: focus band can cover multiple lines. When >1, every line
        # whose y falls inside the band renders in the highlight color.
        self.focus_band_lines = tk.IntVar(value=1)
        self.side_margin = tk.IntVar(value=110)
        self.wrap_ratio = tk.DoubleVar(value=0.88)
        self.always_on_top = tk.BooleanVar(value=True)
        self.borderless_mode = tk.BooleanVar(value=False)
        # NEW in v7.10: Windows color-key transparency. When true, pixels of
        # the current background color become fully see-through (including
        # click-through by default — the OS treats transparent pixels as not
        # belonging to the window for hit-testing).
        self.transparent_background = tk.BooleanVar(value=False)
        # NEW in v7.10: text opacity simulated by blending text colors toward
        # the background color. 1.0 = original color, 0.0 = invisible.
        self.text_opacity = tk.DoubleVar(value=1.0)

        self.text_color = tk.StringVar(value="#ffffff")
        self.near_text_color = tk.StringVar(value="#d8d8d8")
        self.far_text_color = tk.StringVar(value="#8c8c8c")
        self.highlight_text_color = tk.StringVar(value="#fff2a6")
        self.spoken_text_color = tk.StringVar(value="#4a4a4a")  # dimmed "already said"
        self.outline_color = tk.StringVar(value="#000000")
        self.band_color = tk.StringVar(value="#4a4a4a")
        self.background_color = tk.StringVar(value="#000000")
        self.outline_width = tk.IntVar(value=2)
        self.show_center_guide = tk.BooleanVar(value=False)

        self.voice_backend = tk.StringVar(value="auto")
        self.voice_seconds = tk.DoubleVar(value=1.2)

        # --- Read timer (v7.11) ---------------------------------------------
        # Tracks how long the script has been actively read. The timer ticks
        # while it is "running"; toggle_scroll() and the voice-follow
        # start/stop hooks call timer_resume()/timer_pause() so the user can
        # just hit Start and trust the readout. The timer is also fully
        # manual: dedicated Start/Pause and Reset buttons live in the new
        # Read Timer panel.
        self.timer_running = False
        self.timer_accum = 0.0       # seconds counted while running
        self.timer_started_at = None # wall-clock time when last resumed
        self.timer_var = tk.StringVar(value="00:00")
        self._timer_after_id = None

        # Mic selection: -1 = system default, otherwise an index into the backend's device list.
        self.voice_mic_index = tk.IntVar(value=-1)
        self.voice_mic_label = tk.StringVar(value="System Default")
        # Cache of available mics for the current backend: list of (index, name)
        self._mic_cache_backend = None
        self._mic_cache = []
        # Tokenized script for word-matching
        self._script_tokens = []  # list of (line_index, [normalized_word, ...])

        self._build_overlay()
        self._build_grab_strip()
        self._build_controls()
        self._bind_shortcuts()

        self.load_settings()
        self.set_default_script_if_empty()
        self.update_overlay_style()
        self.rebuild_and_render()

        # Re-sync the mic dropdown NOW that load_settings has restored the saved
        # voice_mic_index / voice_mic_label. _build_controls ran before
        # load_settings, so the first refresh saw defaults; this second call
        # picks the saved mic if it still exists in the enumeration.
        try:
            self._refresh_mic_list()
        except Exception:
            pass

        self.controls.attributes("-topmost", False)
        self.controls.lift()

        # If click-through was persisted as ON, apply it once the overlay HWND
        # is actually realized. Defer via after() so GetAncestor() finds a real root.
        if self.click_through_enabled.get():
            self.overlay.after(300, lambda: self.set_click_through(True))

    def status_msg(self, msg):
        self.status.config(text=msg)

    def settings_dict(self):
        return {
            "window_transparency": float(self.window_transparency.get()),
            "font_size": int(self.font_size.get()),
            "line_spacing": int(self.line_spacing.get()),
            "scroll_speed": float(self.scroll_speed.get()),
            "focus_line_y": float(self.focus_line_y.get()),
            "focus_band_lines": int(self.focus_band_lines.get()),
            "side_margin": int(self.side_margin.get()),
            "wrap_ratio": float(self.wrap_ratio.get()),
            "always_on_top": bool(self.always_on_top.get()),
            "borderless_mode": bool(self.borderless_mode.get()),
            "transparent_background": bool(self.transparent_background.get()),
            "text_opacity": float(self.text_opacity.get()),
            "click_through_enabled": bool(self.click_through_enabled.get()),
            "text_color": self.text_color.get(),
            "near_text_color": self.near_text_color.get(),
            "far_text_color": self.far_text_color.get(),
            "highlight_text_color": self.highlight_text_color.get(),
            "spoken_text_color": self.spoken_text_color.get(),
            "outline_color": self.outline_color.get(),
            "band_color": self.band_color.get(),
            "background_color": self.background_color.get(),
            "outline_width": int(self.outline_width.get()),
            "show_center_guide": bool(self.show_center_guide.get()),
            "voice_backend": self.voice_backend.get(),
            "voice_seconds": float(self.voice_seconds.get()),
            "voice_mic_index": int(self.voice_mic_index.get()),
            "voice_mic_label": self.voice_mic_label.get(),
            "overlay_geometry": self.overlay.geometry(),
            "controls_geometry": self.controls.geometry(),
            "script_text": self.editor.get("1.0", "end-1c") if hasattr(self, "editor") else "",
        }

    def save_settings(self):
        try:
            with open(self.settings_path, "w", encoding="utf-8") as f:
                json.dump(self.settings_dict(), f, indent=2)
            self.status_msg(f"Settings saved to {os.path.basename(self.settings_path)}")
        except Exception as e:
            self.status_msg(f"Could not save settings: {e}")

    def load_settings(self):
        if not os.path.exists(self.settings_path):
            return
        try:
            with open(self.settings_path, "r", encoding="utf-8") as f:
                data = json.load(f)

            mapping = [
                ("window_transparency", self.window_transparency),
                ("font_size", self.font_size),
                ("line_spacing", self.line_spacing),
                ("scroll_speed", self.scroll_speed),
                ("focus_line_y", self.focus_line_y),
                ("focus_band_lines", self.focus_band_lines),
                ("side_margin", self.side_margin),
                ("wrap_ratio", self.wrap_ratio),
                ("always_on_top", self.always_on_top),
                ("borderless_mode", self.borderless_mode),
                ("transparent_background", self.transparent_background),
                ("text_opacity", self.text_opacity),
                ("click_through_enabled", self.click_through_enabled),
                ("text_color", self.text_color),
                ("near_text_color", self.near_text_color),
                ("far_text_color", self.far_text_color),
                ("highlight_text_color", self.highlight_text_color),
                ("spoken_text_color", self.spoken_text_color),
                ("outline_color", self.outline_color),
                ("band_color", self.band_color),
                ("background_color", self.background_color),
                ("outline_width", self.outline_width),
                ("show_center_guide", self.show_center_guide),
                ("voice_backend", self.voice_backend),
                ("voice_seconds", self.voice_seconds),
                ("voice_mic_index", self.voice_mic_index),
                ("voice_mic_label", self.voice_mic_label),
            ]
            for name, var in mapping:
                if name in data:
                    var.set(data[name])

            if "script_text" in data and data["script_text"]:
                self.editor.delete("1.0", "end")
                self.editor.insert("1.0", data["script_text"])

            if "overlay_geometry" in data:
                self.overlay.geometry(data["overlay_geometry"])
            if "controls_geometry" in data:
                self.controls.geometry(data["controls_geometry"])

            self.toggle_borderless_mode(apply_only=True)
        except Exception as e:
            self.status_msg(f"Could not load settings: {e}")

    def set_default_script_if_empty(self):
        if not self.editor.get("1.0", "end-1c").strip():
            demo = (
                "Paste your script here.\n\n"
                "This version saves settings, keeps the control panel off always-on-top, "
                "and adds overlay click-through so you can click OBS or other windows underneath.\n\n"
                "Toggle click-through when you want the overlay visible but non-blocking."
            )
            self.editor.delete("1.0", "end")
            self.editor.insert("1.0", demo)

    def apply_script(self):
        self.active_line_index = 0
        self.scroll_position = 0.0
        self.rebuild_and_render()
        self.save_settings()
        self.status_msg("Script applied.")

    def add_number_control(self, parent, label, var, from_, to, step, command=None, width=8, is_float=False):
        row = tk.Frame(parent, bg="#181818")
        row.pack(fill="x", padx=10, pady=5)
        tk.Label(row, text=label, width=22, anchor="w", bg="#181818", fg="white",
                 font=(UI_FONT_FAMILY, UI_FONT_BASE)).pack(side="left")

        def clamp_and_apply():
            try:
                value = float(var.get()) if is_float else int(var.get())
                value = max(from_, min(to, value))
                var.set(round(value, 2) if is_float else int(value))
                if command:
                    command()
                self.save_settings()
            except Exception:
                pass

        def dec():
            value = float(var.get()) if is_float else int(var.get())
            var.set(round(value - step, 2) if is_float else int(value - step))
            clamp_and_apply()

        def inc():
            value = float(var.get()) if is_float else int(var.get())
            var.set(round(value + step, 2) if is_float else int(value + step))
            clamp_and_apply()

        tk.Button(row, text="−", width=3, bg="#2b2b2b", fg="white",
                  activebackground="#404040", activeforeground="white",
                  font=(UI_FONT_FAMILY, UI_FONT_BTN, "bold"),
                  command=dec).pack(side="left", padx=(0, 4))

        spin = tk.Spinbox(
            row, from_=from_, to=to, increment=step, textvariable=var,
            width=width, command=clamp_and_apply, bg="#111111", fg="white",
            buttonbackground="#2b2b2b", insertbackground="white", relief="solid",
            font=(UI_FONT_FAMILY, UI_FONT_BASE)
        )
        spin.pack(side="left")
        spin.bind("<Return>", lambda e: clamp_and_apply())
        spin.bind("<FocusOut>", lambda e: clamp_and_apply())

        tk.Button(row, text="+", width=3, bg="#2b2b2b", fg="white",
                  activebackground="#404040", activeforeground="white",
                  font=(UI_FONT_FAMILY, UI_FONT_BTN, "bold"),
                  command=inc).pack(side="left", padx=(4, 0))

    def add_color_control(self, parent, label, var, command=None):
        row = tk.Frame(parent, bg="#181818")
        row.pack(fill="x", padx=10, pady=5)
        tk.Label(row, text=label, width=22, anchor="w", bg="#181818", fg="white",
                 font=(UI_FONT_FAMILY, UI_FONT_BASE)).pack(side="left")

        entry = tk.Entry(row, textvariable=var, width=12, bg="#111111", fg="white",
                         insertbackground="white", font=(UI_FONT_FAMILY, UI_FONT_BASE))
        entry.pack(side="left", padx=(0, 6))
        swatch = tk.Label(row, width=4, bg=var.get(), relief="solid", bd=1)
        swatch.pack(side="left", padx=(0, 6))

        def apply_color(*_):
            color = var.get().strip()
            if not color.startswith("#") or len(color) != 7:
                return
            try:
                swatch.config(bg=color)
                if command:
                    command()
                self.save_settings()
            except Exception:
                pass

        def choose():
            chosen = colorchooser.askcolor(color=var.get(), parent=self.controls)[1]
            if chosen:
                var.set(chosen)
                apply_color()

        entry.bind("<Return>", apply_color)
        entry.bind("<FocusOut>", apply_color)
        tk.Button(row, text="Pick", width=6, command=choose,
                  bg="#2b2b2b", fg="white", activebackground="#404040", activeforeground="white",
                  font=(UI_FONT_FAMILY, UI_FONT_BTN)).pack(side="left")

    def make_dark_checkbox(self, parent, text, variable, command=None):
        """Create a clickable label that behaves like a checkbox and shows its
        state unambiguously on a dark theme.

        We deliberately avoid tk.Checkbutton here because its indicator is
        hard to see in a dark theme, and its indicatoron=False mode has
        quirky variable/state sync on Windows. This is a Frame holding two
        Labels — a prominent state indicator and the text — both bound to
        <Button-1>.

        Returns the container Frame.
        """
        # Bright green when on, dim grey when off — unmistakably different.
        ON_COLOR = "#4ade80"   # bright green
        OFF_COLOR = "#666666"  # dim grey
        ON_GLYPH = "[\u2713]"  # [✓]
        OFF_GLYPH = "[ ]"

        container = tk.Frame(parent, bg="#181818", cursor="hand2")

        indicator = tk.Label(
            container,
            text=ON_GLYPH if bool(variable.get()) else OFF_GLYPH,
            bg="#181818",
            fg=ON_COLOR if bool(variable.get()) else OFF_COLOR,
            font=("Consolas", UI_FONT_BASE + 2, "bold"),
            padx=0, pady=0, bd=0, highlightthickness=0,
            cursor="hand2",
        )
        indicator.pack(side="left", padx=(6, 4))

        txt = tk.Label(
            container,
            text=text,
            bg="#181818", fg="white",
            anchor="w", padx=0, pady=3,
            font=(UI_FONT_FAMILY, UI_FONT_BASE),
            cursor="hand2",
        )
        txt.pack(side="left", padx=(0, 6))

        def refresh_indicator(*_):
            try:
                if bool(variable.get()):
                    indicator.config(text=ON_GLYPH, fg=ON_COLOR)
                else:
                    indicator.config(text=OFF_GLYPH, fg=OFF_COLOR)
            except tk.TclError:
                pass

        def on_click(_event=None):
            variable.set(not bool(variable.get()))
            refresh_indicator()
            if command:
                try:
                    command()
                except Exception as e:
                    _debug_log(f"make_dark_checkbox command error: {e}")

        for widget in (container, indicator, txt):
            widget.bind("<Button-1>", on_click)

        # Keep indicator in sync if the variable is changed elsewhere
        # (keyboard shortcuts, settings restore, programmatic toggles).
        try:
            variable.trace_add("write", lambda *a: refresh_indicator())
        except Exception:
            try:
                variable.trace("w", lambda *a: refresh_indicator())
            except Exception:
                pass
        return container

    def bind_mousewheel(self, widget, target_canvas):
        def _on_mousewheel(event):
            try:
                if event.delta:
                    target_canvas.yview_scroll(int(-1 * (event.delta / 120)), "units")
                else:
                    if getattr(event, "num", None) == 4:
                        target_canvas.yview_scroll(-3, "units")
                    elif getattr(event, "num", None) == 5:
                        target_canvas.yview_scroll(3, "units")
            except Exception:
                pass
        widget.bind("<MouseWheel>", _on_mousewheel)
        widget.bind("<Button-4>", _on_mousewheel)
        widget.bind("<Button-5>", _on_mousewheel)

    def _build_overlay(self):
        self.overlay = tk.Toplevel()
        self.overlay.title(APP_TITLE)
        self.overlay.geometry("1500x820+30+20")
        self.overlay.configure(bg="black")
        self.overlay.minsize(700, 260)
        self.overlay.attributes("-topmost", True)
        self.overlay.attributes("-alpha", float(self.window_transparency.get()))
        self.canvas = tk.Canvas(self.overlay, bg="black", highlightthickness=0, bd=0)
        self.canvas.pack(fill="both", expand=True)
        self.canvas.bind("<Configure>", lambda e: self.rebuild_and_render())

    def _build_grab_strip(self):
        """A small separate top-level window that sits on the right edge of
        the overlay and is never click-through. It gives the user a reliable
        mouse-accessible handle to scroll the script even when the overlay
        itself is fully click-transparent.

        This is implemented as a companion Toplevel (not a sub-widget of the
        overlay) because Win32's WS_EX_TRANSPARENT applies to the entire
        window — you can't have a non-click-through region inside a
        click-through window.
        """
        self.grab_strip = tk.Toplevel()
        self.grab_strip.title("Scroll")
        self.grab_strip.overrideredirect(True)       # no title bar / borders
        self.grab_strip.attributes("-topmost", True)
        self.grab_strip.attributes("-alpha", 0.85)
        self.grab_strip.configure(bg="#222222")
        # Don't steal focus when clicked — keeps keyboard shortcuts working
        # on the control/overlay windows.
        try:
            self.grab_strip.attributes("-toolwindow", True)
        except Exception:
            pass

        # Width is configurable; defaults to 18 px.
        self.grab_strip_width = 18

        # The whole strip acts as a mouse grab area. Hovering lights it up
        # so the user can see where to click.
        inner = tk.Frame(self.grab_strip, bg="#2b2b2b", bd=0, highlightthickness=0)
        inner.pack(fill="both", expand=True)

        # A simple visual indicator: up-arrow at top, down-arrow at bottom,
        # and a "grip" hint in the middle.
        up_lbl = tk.Label(inner, text="▲", bg="#2b2b2b", fg="#9ad0ff",
                          font=("Arial", 10, "bold"), cursor="hand2")
        up_lbl.pack(side="top", pady=(4, 0))
        grip = tk.Label(inner, text="≡\n≡\n≡", bg="#2b2b2b", fg="#666666",
                        font=("Arial", 9), justify="center")
        grip.pack(side="top", expand=True)
        down_lbl = tk.Label(inner, text="▼", bg="#2b2b2b", fg="#9ad0ff",
                            font=("Arial", 10, "bold"), cursor="hand2")
        down_lbl.pack(side="bottom", pady=(0, 4))

        # Wheel events on the strip → fast scroll the script.
        def _strip_wheel(event):
            step = 3
            if getattr(event, "delta", 0):
                direction = -1 if event.delta > 0 else 1
                self.jump_lines(direction * step)
            elif getattr(event, "num", None) == 4:
                self.jump_lines(-step)
            elif getattr(event, "num", None) == 5:
                self.jump_lines(step)
            return "break"

        # Click-and-hold the arrows for continuous scroll.
        def _hold_scroll(direction):
            self._strip_hold_dir = direction
            self._strip_hold_speed = 1
            self._strip_hold_job = self.grab_strip.after(0, _strip_tick)

        def _strip_tick():
            if getattr(self, "_strip_hold_dir", 0) == 0:
                self._strip_hold_job = None
                return
            self.jump_lines(self._strip_hold_dir * self._strip_hold_speed)
            if self._strip_hold_speed < 6:
                self._strip_hold_speed += 1
            self._strip_hold_job = self.grab_strip.after(70, _strip_tick)

        def _release_scroll(_e=None):
            self._strip_hold_dir = 0
            self._strip_hold_speed = 0
            if getattr(self, "_strip_hold_job", None):
                try:
                    self.grab_strip.after_cancel(self._strip_hold_job)
                except Exception:
                    pass
                self._strip_hold_job = None

        # Drag-to-scroll on the grip: move mouse up/down over the strip
        # body while holding Button-1 → scroll by pixel movement.
        def _drag_start(event):
            self._strip_drag_y = event.y_root
        def _drag_move(event):
            last = getattr(self, "_strip_drag_y", None)
            if last is None:
                return
            dy = event.y_root - last
            # Every ~20px of drag = 1 line. Natural drag direction: pulling
            # down scrolls the script down (reveals later content).
            if abs(dy) >= 20:
                lines = int(dy // 20)
                self.jump_lines(lines)
                self._strip_drag_y = event.y_root
        def _drag_end(_e):
            self._strip_drag_y = None

        for w in (self.grab_strip, inner, grip):
            w.bind("<MouseWheel>", _strip_wheel)
            w.bind("<Button-4>", _strip_wheel)
            w.bind("<Button-5>", _strip_wheel)
            w.bind("<ButtonPress-1>", _drag_start)
            w.bind("<B1-Motion>", _drag_move)
            w.bind("<ButtonRelease-1>", _drag_end)

        # Arrow buttons: click-and-hold for continuous fast scroll.
        up_lbl.bind("<ButtonPress-1>", lambda e: _hold_scroll(-1))
        up_lbl.bind("<ButtonRelease-1>", _release_scroll)
        down_lbl.bind("<ButtonPress-1>", lambda e: _hold_scroll(1))
        down_lbl.bind("<ButtonRelease-1>", _release_scroll)

        # Hover feedback.
        def _enter(_e):
            inner.configure(bg="#3a3a3a")
            grip.configure(bg="#3a3a3a")
        def _leave(_e):
            inner.configure(bg="#2b2b2b")
            grip.configure(bg="#2b2b2b")
        for w in (self.grab_strip, inner, grip, up_lbl, down_lbl):
            w.bind("<Enter>", _enter)
            w.bind("<Leave>", _leave)

        # Keep the strip glued to the overlay's right edge.
        self._reposition_grab_strip()
        self.overlay.bind("<Configure>", self._on_overlay_configure, add="+")

    def _on_overlay_configure(self, _event=None):
        # Existing canvas <Configure> handler triggers a rebuild;
        # we only need to re-place the strip here.
        self._reposition_grab_strip()

    def _reposition_grab_strip(self):
        """Place the grab strip flush against the right edge of the overlay."""
        if not hasattr(self, "grab_strip"):
            return
        try:
            self.overlay.update_idletasks()
            ox = self.overlay.winfo_rootx()
            oy = self.overlay.winfo_rooty()
            ow = self.overlay.winfo_width()
            oh = self.overlay.winfo_height()
            sw = self.grab_strip_width
            x = ox + ow - sw
            self.grab_strip.geometry(f"{sw}x{oh}+{x}+{oy}")
            # Track overlay visibility.
            if getattr(self, "overlay_visible", True):
                self.grab_strip.deiconify()
            else:
                self.grab_strip.withdraw()
        except Exception:
            pass

    # ---------------------------------------------------------------- Timer
    def _format_timer(self, seconds):
        """Format a duration in seconds as MM:SS, or H:MM:SS once we cross
        an hour. Negative inputs are clamped to zero so a clock skew can't
        produce a "-00:01" flicker."""
        s = int(max(0, seconds))
        h, rem = divmod(s, 3600)
        m, sec = divmod(rem, 60)
        if h > 0:
            return f"{h}:{m:02d}:{sec:02d}"
        return f"{m:02d}:{sec:02d}"

    def _current_timer_seconds(self):
        """How many seconds the timer has counted, including the partial
        slice since it was last resumed."""
        if self.timer_running and self.timer_started_at is not None:
            return self.timer_accum + (time.time() - self.timer_started_at)
        return self.timer_accum

    def _timer_tick(self):
        """Update the displayed time. Re-arms itself every 250ms while
        running. We deliberately don't tick when paused — the readout
        already shows the frozen accumulated value."""
        try:
            self.timer_var.set(self._format_timer(self._current_timer_seconds()))
        except tk.TclError:
            return
        if self.timer_running:
            self._timer_after_id = self.controls.after(250, self._timer_tick)
        else:
            self._timer_after_id = None

    def timer_resume(self):
        """Start the timer, or resume it from the last paused value. Safe
        to call when already running — it's a no-op in that case."""
        if self.timer_running:
            return
        self.timer_running = True
        self.timer_started_at = time.time()
        if self._timer_after_id is None:
            self._timer_tick()
        self._refresh_timer_button()

    def timer_pause(self):
        """Stop counting but keep the accumulated time. Safe to call when
        already paused."""
        if not self.timer_running:
            return
        if self.timer_started_at is not None:
            self.timer_accum += time.time() - self.timer_started_at
        self.timer_running = False
        self.timer_started_at = None
        # Snap the readout to the final paused value immediately so the
        # user doesn't see a stale tick.
        try:
            self.timer_var.set(self._format_timer(self.timer_accum))
        except tk.TclError:
            pass
        self._refresh_timer_button()

    def timer_toggle(self):
        if self.timer_running:
            self.timer_pause()
        else:
            self.timer_resume()

    def timer_reset(self):
        """Zero the timer regardless of run state. If it was running,
        leave it running but counting from zero — that's the behavior
        most people expect from a "reset" button on a stopwatch."""
        was_running = self.timer_running
        self.timer_running = False
        self.timer_started_at = None
        self.timer_accum = 0.0
        try:
            self.timer_var.set(self._format_timer(0))
        except tk.TclError:
            pass
        if was_running:
            self.timer_resume()
        else:
            self._refresh_timer_button()

    def _refresh_timer_button(self):
        """Keep the Start/Pause label in sync with timer state."""
        if not hasattr(self, "_timer_btn") or self._timer_btn is None:
            return
        try:
            if self.timer_running:
                self._timer_btn.config(text="❚❚  Pause", bg="#7a3a3a", activebackground="#8a4a4a")
            else:
                self._timer_btn.config(text="▶  Start", bg="#2e6a3a", activebackground="#3a7a46")
        except tk.TclError:
            pass


    def _build_controls(self):
        self.controls = tk.Toplevel()
        self.controls.title(f"{APP_TITLE} Controls")
        # Slightly wider + taller to accommodate the larger fonts and the
        # new side-by-side Overlay/Voice layout.
        self.controls.geometry("1180x980+70+70")
        self.controls.configure(bg="#181818")
        self.controls.minsize(900, 700)

        # Color palette used across the redesigned panels.
        BG = "#181818"
        PANEL_BG = "#1f1f1f"
        PANEL_BORDER = "#2a2a2a"
        BTN_BG = "#2b2b2b"
        BTN_HOVER = "#404040"
        ACCENT = "#9ad0ff"
        HEADER_FG = "#e8e8e8"
        HINT_FG = "#9a9a9a"

        outer = tk.Frame(self.controls, bg=BG)
        outer.pack(fill="both", expand=True)

        self.ctrl_canvas = tk.Canvas(outer, bg=BG, highlightthickness=0, bd=0)
        self.ctrl_scroll = tk.Scrollbar(outer, orient="vertical", command=self.ctrl_canvas.yview)
        self.ctrl_canvas.configure(yscrollcommand=self.ctrl_scroll.set)
        self.ctrl_scroll.pack(side="right", fill="y")
        self.ctrl_canvas.pack(side="left", fill="both", expand=True)

        self.ctrl_inner = tk.Frame(self.ctrl_canvas, bg=BG)
        self.ctrl_window = self.ctrl_canvas.create_window((0, 0), window=self.ctrl_inner, anchor="nw")

        self.ctrl_inner.bind("<Configure>", lambda e=None: self.ctrl_canvas.configure(scrollregion=self.ctrl_canvas.bbox("all")))
        self.ctrl_canvas.bind("<Configure>", lambda e: self.ctrl_canvas.itemconfigure(self.ctrl_window, width=e.width))
        self.bind_mousewheel(self.ctrl_canvas, self.ctrl_canvas)
        self.bind_mousewheel(self.ctrl_inner, self.ctrl_canvas)

        # Helper to build a panel with a styled header. Centralizes the
        # look so we don't drift between sections.
        def make_panel(parent, title):
            box = tk.Frame(parent, bg=PANEL_BG, bd=0,
                           highlightbackground=PANEL_BORDER, highlightthickness=1)
            head = tk.Frame(box, bg=PANEL_BG)
            head.pack(fill="x", padx=12, pady=(10, 2))
            tk.Label(head, text=title, bg=PANEL_BG, fg=HEADER_FG,
                     font=(UI_FONT_FAMILY, UI_FONT_HEADER, "bold")
                     ).pack(side="left")
            sep = tk.Frame(box, bg=PANEL_BORDER, height=1)
            sep.pack(fill="x", padx=12, pady=(4, 6))
            return box

        btn_cfg = {
            "bg": BTN_BG, "fg": "white",
            "activebackground": BTN_HOVER, "activeforeground": "white",
            "font": (UI_FONT_FAMILY, UI_FONT_BTN),
            "bd": 0, "relief": "flat",
            "padx": 12, "pady": 6,
            "cursor": "hand2",
        }
        # Larger, more prominent variant used for the top action row.
        top_btn_cfg = dict(btn_cfg)
        top_btn_cfg["font"] = (UI_FONT_FAMILY, UI_FONT_BTN_TOP, "bold")
        top_btn_cfg["padx"] = 14
        top_btn_cfg["pady"] = 10

        # ============================================================ TOP ROW
        # Primary actions. Bigger, bolder, and laid out with grid so they
        # share the row evenly even when the window is resized.
        top = tk.Frame(self.ctrl_inner, bg=BG)
        top.pack(fill="x", padx=12, pady=(12, 10))
        self._top_buttons = {}
        top_actions = [
            ("▶  Start / Pause Scroll", self.toggle_scroll, "scroll"),
            ("◀  Prev Line", self.prev_line, "prev"),
            ("Next Line  ▶", self.next_line, "next"),
            ("⟲  Reset to Top", self.reset_to_top, "reset"),
            ("Load .txt", self.load_text_file, "load"),
            ("Apply Script", self.apply_script, "apply"),
            ("Hide / Show Overlay", self.toggle_overlay_visibility, "hide"),
            ("Save Settings", self.save_settings, "save"),
        ]
        # Two rows of 4 buttons works better than one row of 8 — keeps
        # individual button widths comfortable on smaller windows.
        for col in range(4):
            top.grid_columnconfigure(col, weight=1, uniform="topbtn")
        for i, (text, cmd, key) in enumerate(top_actions):
            b = tk.Button(top, text=text, command=cmd, **top_btn_cfg)
            r, c = divmod(i, 4)
            b.grid(row=r, column=c, sticky="ew", padx=4, pady=4)
            self._top_buttons[key] = b

        # ====================================================== TIMER + PLAYBACK
        # Timer on the left, playback (scroll speed) on the right.
        tp_row = tk.Frame(self.ctrl_inner, bg=BG)
        tp_row.pack(fill="x", padx=12, pady=(0, 10))
        tp_row.grid_columnconfigure(0, weight=1, uniform="tp")
        tp_row.grid_columnconfigure(1, weight=1, uniform="tp")

        # --- Read Timer panel ---
        timer_panel = make_panel(tp_row, "Read Timer")
        timer_panel.grid(row=0, column=0, sticky="nsew", padx=(0, 6))

        timer_body = tk.Frame(timer_panel, bg=PANEL_BG)
        timer_body.pack(fill="x", padx=12, pady=(0, 10))

        # Big readout. Uses a fixed-width font so digit changes don't
        # cause the row to jitter horizontally.
        self.timer_label = tk.Label(
            timer_body, textvariable=self.timer_var,
            bg=PANEL_BG, fg="#88ff99",
            font=("Consolas", UI_FONT_TIMER, "bold")
        )
        self.timer_label.pack(side="left", padx=(2, 14))

        timer_btns = tk.Frame(timer_body, bg=PANEL_BG)
        timer_btns.pack(side="left", fill="y")

        # The Start/Pause button changes color + label depending on state
        # (handled by _refresh_timer_button). Keep ref so we can update it.
        self._timer_btn = tk.Button(
            timer_btns, text="▶  Start", command=self.timer_toggle,
            bg="#2e6a3a", fg="white",
            activebackground="#3a7a46", activeforeground="white",
            font=(UI_FONT_FAMILY, UI_FONT_BTN, "bold"),
            bd=0, relief="flat", padx=14, pady=8, cursor="hand2", width=10,
        )
        self._timer_btn.pack(side="left", padx=(0, 6))

        tk.Button(timer_btns, text="⟲  Reset", command=self.timer_reset,
                  width=10, **btn_cfg
                  ).pack(side="left")

        tk.Label(timer_panel,
                 text="Auto-starts with Start Scroll / Voice Follow. Pause and Reset are independent.",
                 bg=PANEL_BG, fg=HINT_FG,
                 font=(UI_FONT_FAMILY, UI_FONT_SMALL),
                 wraplength=520, justify="left", anchor="w"
                 ).pack(fill="x", padx=12, pady=(0, 10))

        # --- Playback panel (Smooth Scroll Speed) ---
        playback = make_panel(tp_row, "Playback")
        playback.grid(row=0, column=1, sticky="nsew", padx=(6, 0))
        self.add_number_control(playback, "Smooth Scroll Speed", self.scroll_speed,
                                0.0, 220.0, 2.0, None, is_float=True)
        tk.Label(playback, text="Set speed to 0 for no movement.",
                 bg=PANEL_BG, fg=HINT_FG,
                 font=(UI_FONT_FAMILY, UI_FONT_SMALL),
                 anchor="w").pack(anchor="w", padx=12, pady=(2, 10))

        # ================================================== OVERLAY + VOICE ROW
        # Side-by-side panels (the user explicitly asked for this layout).
        ov_row = tk.Frame(self.ctrl_inner, bg=BG)
        ov_row.pack(fill="x", padx=12, pady=(0, 10))
        ov_row.grid_columnconfigure(0, weight=1, uniform="ov")
        ov_row.grid_columnconfigure(1, weight=1, uniform="ov")

        # ----------------------------------------------------- Overlay panel
        clickbox = make_panel(ov_row, "Overlay Window")
        clickbox.grid(row=0, column=0, sticky="nsew", padx=(0, 6))

        # Two-column grid of checkboxes inside the overlay panel.
        ov_grid = tk.Frame(clickbox, bg=PANEL_BG)
        ov_grid.pack(fill="x", padx=12, pady=(0, 4))
        ov_grid.grid_columnconfigure(0, weight=1, uniform="ovcb")
        ov_grid.grid_columnconfigure(1, weight=1, uniform="ovcb")

        self.make_dark_checkbox(
            ov_grid, "Overlay Always On Top", self.always_on_top,
            command=lambda: (self.update_overlay_style(), self.save_settings()),
        ).grid(row=0, column=0, sticky="w", padx=4, pady=2)
        self.make_dark_checkbox(
            ov_grid, "Overlay Click-Through", self.click_through_enabled,
            command=self.toggle_click_through,
        ).grid(row=0, column=1, sticky="w", padx=4, pady=2)
        self.make_dark_checkbox(
            ov_grid, "Borderless Mode", self.borderless_mode,
            command=lambda: (self.toggle_borderless_mode(), self.save_settings()),
        ).grid(row=1, column=0, sticky="w", padx=4, pady=2)
        self.make_dark_checkbox(
            ov_grid, "Show Center Guide", self.show_center_guide,
            command=lambda: (self.render_lines(), self.save_settings()),
        ).grid(row=1, column=1, sticky="w", padx=4, pady=2)
        self.make_dark_checkbox(
            ov_grid, "Transparent Background (Windows)", self.transparent_background,
            command=lambda: (self.update_overlay_style(), self.save_settings()),
        ).grid(row=2, column=0, columnspan=2, sticky="w", padx=4, pady=2)

        tk.Label(clickbox,
                 text="When click-through is ON, you can click apps underneath the overlay.",
                 bg=PANEL_BG, fg=HINT_FG,
                 font=(UI_FONT_FAMILY, UI_FONT_SMALL),
                 wraplength=520, justify="left", anchor="w"
                 ).pack(anchor="w", padx=12, pady=(6, 2))
        tk.Label(clickbox,
                 text=("Transparent Background uses color-key transparency: "
                       "pixels matching the Background Color become fully see-through. "
                       "Clicks on transparent pixels pass through automatically."),
                 bg=PANEL_BG, fg=HINT_FG,
                 font=(UI_FONT_FAMILY, UI_FONT_SMALL),
                 wraplength=520, justify="left", anchor="w"
                 ).pack(anchor="w", padx=12, pady=(0, 10))

        # ------------------------------------------------------- Voice panel
        voicebox = make_panel(ov_row, "Voice")
        voicebox.grid(row=0, column=1, sticky="nsew", padx=(6, 0))

        # Action buttons + status. Use grid so they share the row width and
        # don't clip when the panel is narrow.
        v_actions = tk.Frame(voicebox, bg=PANEL_BG)
        v_actions.pack(fill="x", padx=8, pady=(0, 6))
        v_actions.grid_columnconfigure(0, weight=1, uniform="vact")
        v_actions.grid_columnconfigure(1, weight=1, uniform="vact")
        tk.Button(v_actions, text="Start / Stop Voice Follow",
                  command=self.toggle_voice_follow, **btn_cfg
                  ).grid(row=0, column=0, columnspan=2, sticky="ew", padx=4, pady=2)
        tk.Button(v_actions, text="Reset Voice Cursor",
                  command=self.reset_voice_cursor, **btn_cfg
                  ).grid(row=1, column=0, sticky="ew", padx=4, pady=2)
        tk.Button(v_actions, text="Download Vosk Model",
                  command=self.download_vosk_model, **btn_cfg
                  ).grid(row=1, column=1, sticky="ew", padx=4, pady=2)
        self.voice_label = tk.Label(voicebox, text="Voice follow: OFF",
                                    bg=PANEL_BG, fg="#ffcc66",
                                    font=(UI_FONT_FAMILY, UI_FONT_BASE, "bold"))
        self.voice_label.pack(anchor="w", padx=12, pady=(0, 4))

        backend_row = tk.Frame(voicebox, bg=PANEL_BG)
        backend_row.pack(fill="x", padx=8, pady=(0, 4))
        tk.Label(backend_row, text="Backend", width=10, anchor="w",
                 bg=PANEL_BG, fg="white",
                 font=(UI_FONT_FAMILY, UI_FONT_BASE)
                 ).pack(side="left", padx=(4, 6), anchor="n")
        # 2x2 grid of backend choices — fits in narrow side-by-side panel.
        radio_frame = tk.Frame(backend_row, bg=PANEL_BG)
        radio_frame.pack(side="left", fill="x", expand=True)
        radio_frame.grid_columnconfigure(0, weight=1, uniform="rd")
        radio_frame.grid_columnconfigure(1, weight=1, uniform="rd")
        backends = [("Auto", "auto"),
                    ("Vosk (offline)", "vosk"),
                    ("Google (Sounddevice)", "sounddevice"),
                    ("Google (PyAudio)", "pyaudio")]
        for i, (label, value) in enumerate(backends):
            r, c = divmod(i, 2)
            tk.Radiobutton(
                radio_frame, text=label, value=value, variable=self.voice_backend,
                bg=PANEL_BG, fg="white", selectcolor="#222222",
                activebackground=PANEL_BG, activeforeground="white",
                font=(UI_FONT_FAMILY, UI_FONT_BASE),
                anchor="w",
                command=self._on_backend_changed
            ).grid(row=r, column=c, sticky="w", padx=(0, 8), pady=1)

        # --- Microphone picker ---
        mic_row = tk.Frame(voicebox, bg=PANEL_BG)
        mic_row.pack(fill="x", padx=8, pady=(0, 4))
        tk.Label(mic_row, text="Microphone", width=10, anchor="w",
                 bg=PANEL_BG, fg="white",
                 font=(UI_FONT_FAMILY, UI_FONT_BASE)
                 ).pack(side="left", padx=(4, 6))
        self.mic_combo = ttk.Combobox(mic_row, state="readonly",
                                      font=(UI_FONT_FAMILY, UI_FONT_BASE))
        self.mic_combo.pack(side="left", fill="x", expand=True, padx=(0, 6))
        self.mic_combo.bind("<<ComboboxSelected>>", self._on_mic_selected)
        tk.Button(mic_row, text="Refresh", width=8,
                  command=self._refresh_mic_list, **btn_cfg
                  ).pack(side="left")

        # Live active-mic indicator
        active_row = tk.Frame(voicebox, bg=PANEL_BG)
        active_row.pack(fill="x", padx=8, pady=(0, 4))
        tk.Label(active_row, text="Active Mic", width=10, anchor="w",
                 bg=PANEL_BG, fg="white",
                 font=(UI_FONT_FAMILY, UI_FONT_BASE)
                 ).pack(side="left", padx=(4, 6))
        self.active_mic_label = tk.Label(active_row, textvariable=self.voice_mic_label,
                                         bg=PANEL_BG, fg=ACCENT, anchor="w",
                                         font=(UI_FONT_FAMILY, UI_FONT_BASE))
        self.active_mic_label.pack(side="left", fill="x", expand=True)

        self.add_number_control(voicebox, "Voice Listen Seconds", self.voice_seconds,
                                0.5, 5.0, 0.25, None, is_float=True)

        # Status line reports all recognizer dependencies including Vosk.
        vosk_model_ok = False
        try:
            vosk_model_ok = self._vosk_model_available()
        except Exception:
            pass

        if VOSK_OK and vosk_model_ok:
            vosk_status = "Vosk OK (model ready)"
            vosk_ok = True
        elif VOSK_OK:
            vosk_status = "Vosk installed (model not downloaded)"
            vosk_ok = False
        else:
            vosk_status = "Vosk missing"
            vosk_ok = False

        status_parts = [
            (vosk_status, vosk_ok),
            ("SpeechRecognition OK" if SPEECH_OK else "SpeechRecognition missing", SPEECH_OK),
            ("sounddevice OK" if SOUNDDEVICE_OK else "sounddevice missing", SOUNDDEVICE_OK),
            ("PyAudio OK" if PYAUDIO_OK else f"PyAudio not working ({PYAUDIO_ERR[:60]})" if PYAUDIO_ERR else "PyAudio missing", PYAUDIO_OK),
        ]
        all_ok = all(ok for _, ok in status_parts)
        any_ok = any(ok for _, ok in status_parts)
        tk.Label(
            voicebox, text=" | ".join(t for t, _ in status_parts), bg=PANEL_BG,
            fg="#88ff99" if all_ok else ("#ffcc66" if any_ok else "#ff8888"),
            font=(UI_FONT_FAMILY, UI_FONT_SMALL),
            wraplength=520, justify="left"
        ).pack(anchor="w", padx=12, pady=(2, 4))

        # Show which Python is running, so user can install deps into the RIGHT one.
        py_info_row = tk.Frame(voicebox, bg=PANEL_BG)
        py_info_row.pack(fill="x", padx=12, pady=(0, 4))
        py_short = sys.executable
        if len(py_short) > 60:
            py_short = "..." + py_short[-57:]
        tk.Label(
            py_info_row,
            text=f"Python: {py_short}",
            bg=PANEL_BG, fg=ACCENT, anchor="w",
            font=("Consolas", UI_FONT_SMALL)
        ).pack(side="left", fill="x", expand=True)
        tk.Button(
            py_info_row, text="Copy pip command",
            command=self._copy_pip_command, **btn_cfg
        ).pack(side="right")

        tk.Label(
            voicebox,
            text="If PyAudio is missing, install it into the Python shown above.",
            bg=PANEL_BG, fg=HINT_FG,
            font=(UI_FONT_FAMILY, UI_FONT_SMALL),
            wraplength=520, justify="left"
        ).pack(anchor="w", padx=12, pady=(0, 10))

        # Populate the mic dropdown now that widgets exist.
        self._refresh_mic_list()

        # ============================================== COLLAPSIBLE SECTIONS
        sec1 = CollapsibleSection(self.ctrl_inner, "Overlay / Layout", open_by_default=False)
        sec1.pack(fill="x", padx=12, pady=(0, 8))
        self.add_number_control(sec1.body, "Window Transparency", self.window_transparency, 0.0, 1.0, 0.01, self.update_overlay_style, is_float=True)
        self.add_number_control(sec1.body, "Text Opacity", self.text_opacity, 0.0, 1.0, 0.05, self.render_lines, is_float=True)
        self.add_number_control(sec1.body, "Font Size", self.font_size, 24, 100, 1, self.rebuild_and_render)
        self.add_number_control(sec1.body, "Line Spacing", self.line_spacing, 0, 60, 1, self.rebuild_and_render)
        self.add_number_control(sec1.body, "Focus Line Y", self.focus_line_y, 0.18, 0.85, 0.01, self.render_lines, is_float=True)
        self.add_number_control(sec1.body, "Focus Band Lines", self.focus_band_lines, 1, 10, 1, self.render_lines)
        self.add_number_control(sec1.body, "Side Margin", self.side_margin, 20, 280, 2, self.rebuild_and_render)
        self.add_number_control(sec1.body, "Wrap Width", self.wrap_ratio, 0.50, 0.98, 0.01, self.rebuild_and_render, is_float=True)

        sec2 = CollapsibleSection(self.ctrl_inner, "Colors / Contrast", open_by_default=False)
        sec2.pack(fill="x", padx=12, pady=(0, 8))
        # Background color change must also re-apply the transparent-color
        # key if transparent_background is active.
        self.add_color_control(sec2.body, "Background Color", self.background_color,
                               lambda: (self.update_overlay_style(), self.render_lines()))
        self.add_color_control(sec2.body, "Band Color", self.band_color, self.render_lines)
        self.add_color_control(sec2.body, "Highlight Text Color", self.highlight_text_color, self.render_lines)
        self.add_color_control(sec2.body, "Spoken Word Color (dim)", self.spoken_text_color, self.render_lines)
        self.add_color_control(sec2.body, "Main Text Color", self.text_color, self.render_lines)
        self.add_color_control(sec2.body, "Near Text Color", self.near_text_color, self.render_lines)
        self.add_color_control(sec2.body, "Far Text Color", self.far_text_color, self.render_lines)
        self.add_color_control(sec2.body, "Text Stroke Color", self.outline_color, self.render_lines)
        self.add_number_control(sec2.body, "Text Stroke Width", self.outline_width, 0, 6, 1, self.render_lines)

        sec3 = CollapsibleSection(self.ctrl_inner, "Script", open_by_default=False)
        sec3.pack(fill="both", expand=True, padx=12, pady=(0, 8))
        self.editor = tk.Text(
            sec3.body, wrap="word", bg="#101010", fg="white", insertbackground="white",
            relief="flat", bd=0, padx=10, pady=10,
            font=(UI_FONT_FAMILY, UI_FONT_BASE + 2), height=16
        )
        self.editor.pack(fill="both", expand=True, padx=8, pady=8)
        self.editor.bind("<FocusOut>", lambda e: self.save_settings())

        bottom = tk.Frame(self.ctrl_inner, bg=BG)
        bottom.pack(fill="x", padx=12, pady=(0, 12))
        self.status = tk.Label(bottom, text="Ready", anchor="w",
                               bg=BG, fg=ACCENT,
                               font=(UI_FONT_FAMILY, UI_FONT_BASE))
        self.status.pack(side="left")

        # Initial timer button styling — has to wait until the button exists.
        self._refresh_timer_button()

    def _bind_shortcuts(self):
        # When the script editor has focus, the user is typing — don't hijack
        # arrow keys, PgUp/PgDn, Home/End. Our shortcuts only act when focus
        # is elsewhere (the control/overlay windows themselves).
        def _editor_has_focus():
            try:
                return self.controls.focus_get() is self.editor
            except Exception:
                return False

        for win in (self.controls, self.overlay):
            win.bind("<space>", lambda e: self.toggle_scroll())
            win.bind("<Control-r>", lambda e: self.reset_to_top())
            win.bind("<Control-v>", lambda e: self.toggle_voice_follow())
            win.bind("<Control-Shift-T>", lambda e: (
                self.click_through_enabled.set(not bool(self.click_through_enabled.get())),
                self.toggle_click_through(),
            ))

            # --- Fast-scroll shortcuts -------------------------------------
            # Page Up / Page Down: jump 10 lines.
            win.bind("<Prior>", lambda e: (None if _editor_has_focus() else self.jump_lines(-10)))
            win.bind("<Next>", lambda e: (None if _editor_has_focus() else self.jump_lines(10)))
            # Home / End: jump to start / end of script.
            win.bind("<Home>", lambda e: (None if _editor_has_focus() else self.jump_to_top()))
            win.bind("<End>", lambda e: (None if _editor_has_focus() else self.jump_to_end()))

            # Shift+MouseWheel: fast scroll the script. Bound to both windows
            # but only the control window actually receives wheel events when
            # overlay click-through is on — which is exactly what we want.
            def _shift_wheel(event):
                if _editor_has_focus():
                    return
                # event.delta is +120/-120 per notch on Windows; +/- on mac.
                step = 3  # lines per notch
                if getattr(event, "delta", 0):
                    direction = -1 if event.delta > 0 else 1  # wheel-up = back
                    self.jump_lines(direction * step)
                elif getattr(event, "num", None) == 4:
                    self.jump_lines(-step)
                elif getattr(event, "num", None) == 5:
                    self.jump_lines(step)
                return "break"  # don't also scroll the control panel
            win.bind("<Shift-MouseWheel>", _shift_wheel)
            win.bind("<Shift-Button-4>", _shift_wheel)
            win.bind("<Shift-Button-5>", _shift_wheel)

        # --- Up/Down: tap = 1 line, hold = accelerating fast-scroll --------
        # OS auto-repeat fires <KeyPress-Up> repeatedly while held. We move
        # 1 line on the very first press (the "tap" behavior), then treat
        # subsequent repeats as a held key and run our own accelerator.
        self._held_arrow_job = None
        self._held_arrow_dir = 0
        self._held_arrow_speed = 0
        self._arrow_key_down = False

        def _on_arrow_press(direction):
            if _editor_has_focus():
                return
            if not self._arrow_key_down:
                # First press: tap behavior — move exactly one line.
                self._arrow_key_down = True
                self._held_arrow_dir = direction
                self._held_arrow_speed = 2  # ramp start for hold
                self.jump_lines(direction)
                # Schedule the hold accelerator. If user releases before
                # this fires, KeyRelease cancels it — so taps stay at 1 line.
                self._held_arrow_job = self.controls.after(280, _tick)

        def _tick():
            if not self._arrow_key_down or self._held_arrow_dir == 0:
                self._held_arrow_job = None
                return
            self.jump_lines(self._held_arrow_dir * self._held_arrow_speed)
            if self._held_arrow_speed < 6:
                self._held_arrow_speed += 1
            self._held_arrow_job = self.controls.after(55, _tick)

        def _on_arrow_release(_event=None):
            self._arrow_key_down = False
            if self._held_arrow_job is not None:
                try:
                    self.controls.after_cancel(self._held_arrow_job)
                except Exception:
                    pass
                self._held_arrow_job = None
            self._held_arrow_dir = 0
            self._held_arrow_speed = 0

        for win in (self.controls, self.overlay):
            win.bind("<KeyPress-Up>", lambda e: _on_arrow_press(-1))
            win.bind("<KeyPress-Down>", lambda e: _on_arrow_press(1))
            win.bind("<KeyRelease-Up>", _on_arrow_release)
            win.bind("<KeyRelease-Down>", _on_arrow_release)

        # --- Alt-hold: temporarily release click-through ------------------
        # While Alt is held, the overlay becomes interactive (click-through
        # off) so the mouse wheel works over it. Releasing Alt restores
        # click-through if it was on. This lets the user grab the overlay
        # with the wheel mid-presentation without fully toggling the mode.
        self._alt_suspended_click_through = False

        def _alt_press(_e=None):
            # Only engage if click-through is currently ON. Otherwise there's
            # nothing to temporarily suspend.
            if self._alt_suspended_click_through:
                return
            if bool(self.click_through_enabled.get()):
                self._alt_suspended_click_through = True
                # Turn off click-through without flipping the BooleanVar,
                # so the UI still reflects the user's intended mode.
                try:
                    self.set_click_through(False)
                    # set_click_through flips the BooleanVar on us; put it
                    # back so the indicator still shows "on" — we'll
                    # re-apply on release.
                    self.click_through_enabled.set(True)
                    self.status_msg("Alt held — click-through paused.")
                except Exception as e:
                    _debug_log(f"alt press error: {e}")

        def _alt_release(_e=None):
            if not self._alt_suspended_click_through:
                return
            self._alt_suspended_click_through = False
            # Restore click-through to match the BooleanVar state.
            try:
                if bool(self.click_through_enabled.get()):
                    self.set_click_through(True)
                    self.status_msg("Click-through resumed.")
            except Exception as e:
                _debug_log(f"alt release error: {e}")

        for win in (self.controls, self.overlay):
            # Alt_L and Alt_R cover both sides of keyboard.
            win.bind("<KeyPress-Alt_L>", _alt_press)
            win.bind("<KeyPress-Alt_R>", _alt_press)
            win.bind("<KeyRelease-Alt_L>", _alt_release)
            win.bind("<KeyRelease-Alt_R>", _alt_release)

        # Wheel-anywhere on the overlay: while click-through is paused
        # (Alt held), a raw wheel event on the overlay canvas scrolls the
        # script. This path ALSO works normally when click-through is off
        # in general, so it's a good default for everyone.
        def _overlay_wheel(event):
            step = 3
            if getattr(event, "delta", 0):
                direction = -1 if event.delta > 0 else 1
                self.jump_lines(direction * step)
            elif getattr(event, "num", None) == 4:
                self.jump_lines(-step)
            elif getattr(event, "num", None) == 5:
                self.jump_lines(step)
            return "break"
        self.canvas.bind("<MouseWheel>", _overlay_wheel)
        self.canvas.bind("<Button-4>", _overlay_wheel)
        self.canvas.bind("<Button-5>", _overlay_wheel)

    def _get_overlay_hwnd(self):
        """
        Return the actual top-level Win32 HWND for the overlay window.

        Tk overlays are notoriously fiddly on Windows — winfo_id() returns a
        child handle, titles may get decorated, and GetAncestor sometimes
        returns the wrong root in multi-monitor setups. Try several strategies:

          1. EnumWindows filtered to THIS process's PID, matching the overlay title.
             This is the most reliable approach because it guarantees we pick a
             top-level window belonging to our Python process.
          2. FindWindowW by exact title.
          3. GetAncestor(winfo_id(), GA_ROOT).

        Returns the HWND as an integer, or None if nothing was found.
        """
        if not WINDOWS_OK or os.name != "nt":
            return None

        try:
            user32 = ctypes.windll.user32
            kernel32 = ctypes.windll.kernel32

            user32.FindWindowW.restype = wintypes.HWND
            user32.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
            user32.GetAncestor.restype = wintypes.HWND
            user32.GetAncestor.argtypes = [wintypes.HWND, ctypes.c_uint]
            user32.GetWindowThreadProcessId.restype = wintypes.DWORD
            user32.GetWindowTextLengthW.restype = ctypes.c_int
            user32.GetWindowTextLengthW.argtypes = [wintypes.HWND]
            user32.GetWindowTextW.restype = ctypes.c_int
            user32.IsWindowVisible.restype = wintypes.BOOL
            user32.IsWindowVisible.argtypes = [wintypes.HWND]

            my_pid = kernel32.GetCurrentProcessId()
            target_title = self.overlay.title()

            # Strategy 1: enumerate top-level windows owned by this process.
            EnumWindowsProc = ctypes.WINFUNCTYPE(
                wintypes.BOOL, wintypes.HWND, wintypes.LPARAM
            )
            found = []

            def _enum_cb(hwnd, _lparam):
                if not user32.IsWindowVisible(hwnd):
                    return True
                pid = wintypes.DWORD(0)
                user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                if pid.value != my_pid:
                    return True
                length = user32.GetWindowTextLengthW(hwnd)
                if length == 0:
                    return True
                buf = ctypes.create_unicode_buffer(length + 1)
                user32.GetWindowTextW(hwnd, buf, length + 1)
                title = buf.value
                if title == target_title or title.startswith(target_title):
                    found.append(int(hwnd))
                return True

            user32.EnumWindows(EnumWindowsProc(_enum_cb), 0)
            _debug_log(f"_get_overlay_hwnd: EnumWindows found {len(found)} candidates matching title '{target_title}' for PID {my_pid}")
            if found:
                _debug_log(f"  using HWND from EnumWindows: {hex(found[0])}")
                return found[0]

            # Strategy 2: exact title via FindWindowW.
            hwnd = user32.FindWindowW(None, target_title)
            _debug_log(f"  FindWindowW returned: {hex(int(hwnd)) if hwnd else 'NULL'}")
            if hwnd:
                return int(hwnd)

            # Strategy 3: walk up from Tk's child handle.
            child = self.overlay.winfo_id()
            GA_ROOT = 2
            hwnd = user32.GetAncestor(child, GA_ROOT)
            _debug_log(f"  GetAncestor(winfo_id={hex(child)}) returned: {hex(int(hwnd)) if hwnd else 'NULL'}")
            return int(hwnd) if hwnd else None

        except Exception as e:
            self.status_msg(f"HWND lookup error: {e}")
            return None

    def set_click_through(self, enabled):
        _debug_log(f"set_click_through(enabled={enabled}) called")

        if not WINDOWS_OK or os.name != "nt":
            self.status_msg("Click-through is only available on Windows.")
            self.click_through_enabled.set(False)
            _debug_log(f"  aborted: not Windows (os.name={os.name}, WINDOWS_OK={WINDOWS_OK})")
            return

        hwnd = self._get_overlay_hwnd()
        _debug_log(f"  hwnd={hwnd}")
        if not hwnd:
            self.status_msg("Click-through failed: could not find overlay window handle.")
            self.click_through_enabled.set(False)
            _debug_log("  aborted: no HWND")
            return

        try:
            user32 = ctypes.windll.user32
            GetWindowLong = getattr(user32, "GetWindowLongPtrW", user32.GetWindowLongW)
            SetWindowLong = getattr(user32, "SetWindowLongPtrW", user32.SetWindowLongW)

            GetWindowLong.restype = ctypes.c_ssize_t
            GetWindowLong.argtypes = [wintypes.HWND, ctypes.c_int]
            SetWindowLong.restype = ctypes.c_ssize_t
            SetWindowLong.argtypes = [wintypes.HWND, ctypes.c_int, ctypes.c_ssize_t]

            user32.SetWindowPos.restype = wintypes.BOOL
            user32.SetWindowPos.argtypes = [
                wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int,
                ctypes.c_int, ctypes.c_int, ctypes.c_uint
            ]

            ex_style = GetWindowLong(hwnd, GWL_EXSTYLE)
            _debug_log(f"  ex_style before: {hex(ex_style)}")
            _debug_log(f"    has WS_EX_LAYERED (0x00080000): {bool(ex_style & WS_EX_LAYERED)}")
            _debug_log(f"    has WS_EX_TRANSPARENT (0x00000020): {bool(ex_style & WS_EX_TRANSPARENT)}")

            SWP_NOMOVE, SWP_NOSIZE, SWP_NOZORDER, SWP_FRAMECHANGED = 0x2, 0x1, 0x4, 0x20
            flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED

            if enabled:
                new_style = ex_style | WS_EX_LAYERED | WS_EX_TRANSPARENT
                _debug_log(f"  new_style target: {hex(new_style)}")

                # Tk's -alpha attribute on Windows sets WS_EX_LAYERED already,
                # but on some Tk builds our SetWindowLong can race with Tk's own
                # calls. Call SetWindowLongW returns the PREVIOUS value as a
                # signed integer, not an error code; to detect genuine failure
                # we have to SetLastError(0) first per Microsoft docs.
                kernel32 = ctypes.windll.kernel32
                kernel32.SetLastError(0)
                prev = SetWindowLong(hwnd, GWL_EXSTYLE, new_style)
                last_err = kernel32.GetLastError()
                _debug_log(f"  SetWindowLong returned prev={hex(prev) if prev else 0}, last_err={last_err}")

                # CRITICAL for Windows click-through: a window marked
                # WS_EX_LAYERED does NOT actually render or hit-test as a
                # layered window until SetLayeredWindowAttributes is called.
                # Tk's -alpha attribute DOES call this internally, but only
                # for alpha values <1.0. If the user has alpha=1.0 (fully
                # opaque), Tk skips the call and our WS_EX_TRANSPARENT bit
                # has no effect. We call it explicitly with the current alpha
                # to guarantee layered hit-testing actually works.
                #
                # ALSO CRITICAL: SetLayeredWindowAttributes completely replaces
                # any prior layered-window config. If transparent_background
                # is on (Tk's -transparentcolor), calling this with LWA_ALPHA
                # alone wipes out the color-key. We must pass both
                # LWA_ALPHA | LWA_COLORKEY with the current background color
                # so the color-key transparency survives.
                try:
                    user32.SetLayeredWindowAttributes.restype = wintypes.BOOL
                    user32.SetLayeredWindowAttributes.argtypes = [
                        wintypes.HWND, wintypes.DWORD, wintypes.BYTE, wintypes.DWORD
                    ]
                    LWA_ALPHA = 0x00000002
                    LWA_COLORKEY = 0x00000001
                    alpha_byte = max(0, min(255, int(float(self.window_transparency.get()) * 255)))

                    flags_lwa = LWA_ALPHA
                    colorref = 0
                    if self.transparent_background.get():
                        # Parse "#RRGGBB" into a Windows COLORREF (0x00BBGGRR).
                        hex_color = self.background_color.get().strip().lstrip("#")
                        if len(hex_color) == 6:
                            try:
                                r_ = int(hex_color[0:2], 16)
                                g_ = int(hex_color[2:4], 16)
                                b_ = int(hex_color[4:6], 16)
                                colorref = (b_ << 16) | (g_ << 8) | r_
                                flags_lwa = LWA_ALPHA | LWA_COLORKEY
                                _debug_log(
                                    f"  preserving transparent_background: "
                                    f"colorref={hex(colorref)} (BGR of #{hex_color})"
                                )
                            except ValueError:
                                _debug_log(f"  bad background_color hex: '{hex_color}'")

                    r = user32.SetLayeredWindowAttributes(hwnd, colorref, alpha_byte, flags_lwa)
                    _debug_log(
                        f"  SetLayeredWindowAttributes(colorref={hex(colorref)}, "
                        f"alpha={alpha_byte}, flags={hex(flags_lwa)}) returned {r}"
                    )
                except Exception as la_e:
                    _debug_log(f"  SetLayeredWindowAttributes failed: {la_e}")

                set_pos_ok = user32.SetWindowPos(hwnd, 0, 0, 0, 0, 0, flags)
                _debug_log(f"  SetWindowPos returned {set_pos_ok}")

                verify = GetWindowLong(hwnd, GWL_EXSTYLE)
                _debug_log(f"  ex_style after: {hex(verify)}")
                _debug_log(f"    has WS_EX_TRANSPARENT now: {bool(verify & WS_EX_TRANSPARENT)}")

                if verify & WS_EX_TRANSPARENT:
                    self.click_through_enabled.set(True)
                    self.status_msg(f"Click-through ON (hwnd={hwnd:#x}).")
                    _debug_log("  RESULT: success")
                else:
                    self.click_through_enabled.set(False)
                    self.status_msg(
                        f"Click-through FAILED (hwnd={hwnd:#x}, "
                        f"before={ex_style:#x}, after={verify:#x}). See teleprompter_debug.log."
                    )
                    _debug_log("  RESULT: style did not stick")
            else:
                new_style = (ex_style | WS_EX_LAYERED) & ~WS_EX_TRANSPARENT
                _debug_log(f"  new_style target: {hex(new_style)}")
                kernel32 = ctypes.windll.kernel32
                kernel32.SetLastError(0)
                SetWindowLong(hwnd, GWL_EXSTYLE, new_style)
                last_err = kernel32.GetLastError()
                user32.SetWindowPos(hwnd, 0, 0, 0, 0, 0, flags)
                verify = GetWindowLong(hwnd, GWL_EXSTYLE)
                _debug_log(f"  ex_style after disable: {hex(verify)}, last_err={last_err}")
                self.click_through_enabled.set(False)
                self.status_msg(f"Click-through OFF (hwnd={hwnd:#x}).")
        except Exception as e:
            self.status_msg(f"Click-through error: {e}")
            self.click_through_enabled.set(False)
            _debug_log(f"  EXCEPTION: {e}")
            return

        self.save_settings()

    def toggle_click_through(self):
        # The variable has already been updated to its new desired state by
        # the caller (the dark-theme checkbox helper flips it before invoking
        # this command). Apply that state directly rather than flipping it
        # again, which would produce a net-zero change.
        self.set_click_through(bool(self.click_through_enabled.get()))

    def rebuild_and_render(self):
        """
        Build both the line-level and word-level layout.

        The word-level pass lays out each word at a specific (x, y) in
        script-space so the renderer can colorize individual words for the
        voice karaoke effect. Lines are also kept for legacy line-jump paths.
        """
        raw = self.editor.get("1.0", "end-1c").replace("\r\n", "\n")
        paragraphs = [p.strip() for p in raw.split("\n") if p.strip()]
        if not paragraphs:
            self.display_lines = [" "]
            self.display_line_offsets = [0]
            self.words = []
            self.render_lines()
            return

        w = max(900, self.canvas.winfo_width())
        font = tkfont.Font(family="Arial", size=self.font_size.get(), weight="bold")
        usable_width = max(280, int((w - self.side_margin.get() * 2) * self.wrap_ratio.get()))
        space_w = font.measure(" ")

        # --- Word layout pass ---
        # For each paragraph, greedy-wrap words into visual lines, recording each
        # word's position. A word's x-position is relative to the line's left
        # edge (we'll center the whole line at draw time). Y-position is absolute
        # in script-space (distance from top of script).
        words = []
        lines = []  # display_lines (strings, kept for the line-level code path)
        line_widths = []  # pixel width of each line (for centering)
        gap = self.font_size.get() + self.line_spacing.get()
        para_gap = max(10, gap // 2)

        y_cursor = 0
        line_idx_counter = 0

        for p in paragraphs:
            tokens = p.split()
            current_words = []
            current_width = 0
            current_text = ""

            def flush_line():
                nonlocal current_words, current_width, current_text, y_cursor, line_idx_counter
                if not current_words:
                    return
                # Assign line_idx and final x positions.
                # current_words is a list of (word_text, norm, width). Positions
                # will be computed relative to line left = 0.
                x = 0
                for (wtext, wnorm, wwidth) in current_words:
                    words.append({
                        "text": wtext,
                        "norm": wnorm,
                        "line_idx": line_idx_counter,
                        "x": x,
                        "y": y_cursor,
                        "width": wwidth,
                    })
                    x += wwidth + space_w
                lines.append(current_text)
                line_widths.append(current_width)
                y_cursor += gap
                line_idx_counter += 1
                current_words = []
                current_width = 0
                current_text = ""

            for tok in tokens:
                norm = re.sub(r"[^a-z0-9']", "", tok.lower())
                tok_w = font.measure(tok)
                # Measure candidate width including a preceding space if not first.
                addition = tok_w if not current_words else (space_w + tok_w)
                if current_width + addition <= usable_width:
                    current_words.append((tok, norm, tok_w))
                    current_width += addition
                    current_text = (current_text + " " + tok) if current_text else tok
                else:
                    # Flush the current line and start a new one with this word.
                    flush_line()
                    # Handle the (rare) case of a single word wider than the line.
                    if tok_w > usable_width:
                        # Just place it on its own line; don't try to hyphenate.
                        current_words.append((tok, norm, tok_w))
                        current_width = tok_w
                        current_text = tok
                        flush_line()
                    else:
                        current_words.append((tok, norm, tok_w))
                        current_width = tok_w
                        current_text = tok

            flush_line()
            # Paragraph break: insert a blank line (no words) for visual spacing.
            lines.append("")
            line_widths.append(0)
            y_cursor += para_gap  # smaller gap than full line
            line_idx_counter += 1

        if lines and lines[-1] == "":
            lines.pop()
            line_widths.pop()
            y_cursor -= para_gap
            line_idx_counter -= 1

        self.words = words
        self.display_lines = lines if lines else [" "]
        self._line_widths = line_widths if line_widths else [0]

        # Compute line offsets (y of each line's top-left) for legacy code paths.
        self.display_line_offsets = []
        y = 0
        for line in self.display_lines:
            self.display_line_offsets.append(y)
            y += gap if line else para_gap

        # Clamp legacy active_line_index.
        if self.active_line_index >= len(self.display_lines):
            self.active_line_index = max(0, len(self.display_lines) - 1)

        # Clamp voice cursor into the new word list.
        if self.voice_cursor_word >= len(self.words):
            self.voice_cursor_word = max(0, len(self.words) - 1)

        # If voice is not active, keep scroll_position tied to active_line_index
        # for backward compatibility with Prev/Next Line buttons.
        if not self.voice_mode_running:
            if self.display_line_offsets:
                self.scroll_position = self.display_line_offsets[self.active_line_index]
            self.scroll_target = self.scroll_position
        else:
            # Voice is running — re-seed scroll_target to track the cursor after
            # the layout changes (e.g. window resize, font change, script edit).
            if self.words:
                self.scroll_target = self.words[self.voice_cursor_word]["y"]

        self.render_lines()
        self.save_settings()

    def _blend_with_bg(self, hex_color):
        """Blend a hex color toward the current background color by (1 - text_opacity).

        At text_opacity=1.0, returns the original color unchanged.
        At text_opacity=0.0, returns the background color (text vanishes).
        Used to simulate text-only alpha since Tk's canvas text items can't be
        alpha-blended individually.
        """
        opacity = max(0.0, min(1.0, float(self.text_opacity.get())))
        if opacity >= 0.999:
            return hex_color
        try:
            bg = self.background_color.get().strip().lstrip("#")
            fg = hex_color.strip().lstrip("#")
            if len(bg) != 6 or len(fg) != 6:
                return hex_color
            br, bgn, bb = int(bg[0:2], 16), int(bg[2:4], 16), int(bg[4:6], 16)
            fr, fgn, fb = int(fg[0:2], 16), int(fg[2:4], 16), int(fg[4:6], 16)
            r = int(br + opacity * (fr - br))
            g = int(bgn + opacity * (fgn - bgn))
            b = int(bb + opacity * (fb - bb))
            return f"#{r:02x}{g:02x}{b:02x}"
        except Exception:
            return hex_color

    def draw_outlined_text(self, x, y, text, fill, width, font, anchor="center", justify="center"):
        """
        Draw text with an outline/stroke effect.

        The stroke is achieved by drawing the text multiple times offset in
        different directions before drawing the fill on top. To keep frame
        cost low, we use a sparse 8-direction pattern (N/NE/E/SE/S/SW/W/NW)
        scaled by stroke width, rather than a dense disc of every integer
        offset. Visually this is nearly indistinguishable from the dense
        approach at normal stroke widths but costs 3-5x less to render — a
        significant win since render_lines runs every frame during scroll.

        Text opacity is simulated by blending both fill and stroke toward the
        background color — see _blend_with_bg.
        """
        stroke = int(self.outline_width.get())
        outline = self._blend_with_bg(self.outline_color.get().strip())
        fill = self._blend_with_bg(fill)
        if stroke > 0:
            s = stroke
            offsets = (
                (-s, -s), (0, -s), (s, -s),
                (-s,  0),          (s,  0),
                (-s,  s), (0,  s), (s,  s),
            )
            for dx, dy in offsets:
                self.canvas.create_text(
                    x + dx, y + dy, text=text, font=font, fill=outline,
                    width=width, justify=justify, anchor=anchor
                )
        self.canvas.create_text(
            x, y, text=text, font=font, fill=fill,
            width=width, justify=justify, anchor=anchor
        )

    def _get_render_font(self):
        """Return a cached (tkfont.Font, tuple) pair for the current font size.

        Re-allocating tkfont.Font and its internal measure cache on every frame
        is a measurable cost during auto-scroll — Tk has to parse the family
        string, query the font system, and build the metric cache. We keep one
        instance per font size and reuse it across frames."""
        size = self.font_size.get()
        cached = getattr(self, "_render_font_cache", None)
        if cached and cached[0] == size:
            return cached[1], cached[2]
        font_obj = tkfont.Font(family="Arial", size=size, weight="bold")
        font_tuple = ("Arial", size, "bold")
        self._render_font_cache = (size, font_obj, font_tuple)
        return font_obj, font_tuple

    def render_lines(self):
        """
        Render the script. When voice follow is active, render word-by-word
        with karaoke colors. Otherwise use the simpler line-by-line path.

        The focus band can span multiple lines (focus_band_lines). Any line
        whose rendered y falls inside the band region is drawn in the
        highlight color, making it a true "multi-line focus zone" rather than
        just a visual indicator. Lines outside the band use the distance-based
        fade tiers (text → near → far).
        """
        self.canvas.delete("all")
        w = max(1, self.canvas.winfo_width())
        h = max(1, self.canvas.winfo_height())
        center_y = int(h * float(self.focus_line_y.get()))
        gap = max(1, self.font_size.get() + self.line_spacing.get())
        band_lines = max(1, int(self.focus_band_lines.get()))

        # Band height scales with the number of focused lines. A single-line
        # band retains the classic ~1.38× font-size look; multi-line bands
        # extend by one full line-gap per additional line.
        band_h = max(44, int(self.font_size.get() * 1.38)) + (band_lines - 1) * gap
        band_top = center_y - band_h // 2
        band_bottom = center_y + band_h // 2

        self.canvas.create_rectangle(0, 0, w, h, fill=self.background_color.get().strip(), outline="")
        self.canvas.create_rectangle(
            0, band_top, w, band_bottom,
            fill=self.band_color.get().strip(), outline=""
        )
        if self.show_center_guide.get():
            self.canvas.create_line(0, center_y, w, center_y, fill="#888888", width=1)

        usable_width = max(260, int((w - self.side_margin.get() * 2) * self.wrap_ratio.get()))
        font_obj, font_tuple = self._get_render_font()

        # --- Voice karaoke path ---
        if self.voice_mode_running and self.words:
            self._render_words(w, h, center_y, usable_width, font_obj, font_tuple, gap,
                               band_top=band_top, band_bottom=band_bottom)
            return

        # --- Legacy line-level path ---
        if self.display_line_offsets:
            best_idx = min(
                range(len(self.display_line_offsets)),
                key=lambda i: abs(self.display_line_offsets[i] - self.scroll_position)
            )
            if self.display_lines[best_idx] != "":
                self.active_line_index = best_idx

        for i, line in enumerate(self.display_lines):
            if not line:
                continue
            # Round y to integer pixel — sub-pixel y positions cause Tk to
            # re-rasterize glyphs at slightly different offsets each frame,
            # producing visible wobble even at constant scroll speed.
            y = int(round(center_y + (self.display_line_offsets[i] - self.scroll_position)))
            if y < -200 or y > h + 200:
                continue

            # Highlight-zone test: any line whose center y falls inside the
            # band region gets the highlight color. This makes the focus band
            # a true multi-line highlight zone when focus_band_lines > 1.
            if band_top <= y <= band_bottom:
                fill = self.highlight_text_color.get().strip()
            else:
                # Outside the band: use distance-based fade tiers as before.
                distance = abs(self.display_line_offsets[i] - self.scroll_position)
                if distance < gap * 1.5:
                    fill = self.text_color.get().strip()
                elif distance < gap * 4:
                    fill = self.near_text_color.get().strip()
                else:
                    fill = self.far_text_color.get().strip()

            self.draw_outlined_text(w // 2, y, line, fill, usable_width, font_tuple)

    def _render_words(self, w, h, center_y, usable_width, font_obj, font_tuple, gap,
                      band_top=None, band_bottom=None):
        """Render each word individually so the voice cursor can colorize them.

        When band_top/band_bottom are provided (the multi-line focus band),
        any upcoming word whose y is inside the band uses the upcoming color
        rather than the fade tiers — keeping the band visually consistent.
        The current word (cursor) still uses the highlight color, and already-
        spoken words still use the spoken/dim color."""
        if not self.words:
            return

        space_w = font_obj.measure(" ")
        cursor = self.voice_cursor_word
        spoken_col = self.spoken_text_color.get().strip()
        current_col = self.highlight_text_color.get().strip()
        upcoming_col = self.text_color.get().strip()
        near_col = self.near_text_color.get().strip()
        far_col = self.far_text_color.get().strip()

        # Group words by line index so we can center each line properly.
        by_line = {}
        for idx, word in enumerate(self.words):
            by_line.setdefault(word["line_idx"], []).append((idx, word))

        for line_idx, word_list in by_line.items():
            if not word_list:
                continue
            _, last_w = word_list[-1]
            line_width = last_w["x"] + last_w["width"]
            line_left = (w - line_width) // 2

            _, first_w = word_list[0]
            line_y_script = first_w["y"]
            y = int(round(center_y + (line_y_script - self.scroll_position)))
            if y < -gap * 3 or y > h + gap * 3:
                continue  # offscreen; skip whole line

            dist_from_cursor_line = abs(line_y_script - self._current_word_y())

            # Is this line inside the multi-line focus band?
            in_band = (band_top is not None and band_bottom is not None
                       and band_top <= y <= band_bottom)

            for idx, word in word_list:
                wx = line_left + word["x"] + word["width"] // 2

                if self.voice_mode_running:
                    # Voice-follow logic:
                    #   - The word at the cursor → Highlight Text Color
                    #     (the user's chosen "current word" color).
                    #   - Any other word currently inside the focus band
                    #     (whether already spoken or upcoming) → Main Text
                    #     Color. The band is the "reading zone", so nothing
                    #     inside it should be dimmed or faded.
                    #   - Already-spoken words that have LEFT the band
                    #     (scrolled above it) → Spoken/dim color.
                    #   - Upcoming words outside the band → distance fade.
                    if idx == cursor:
                        fill = current_col
                    elif in_band:
                        fill = upcoming_col
                    elif idx < cursor:
                        # Past the cursor and already out of the band.
                        fill = spoken_col
                    else:
                        # Upcoming, outside the band — distance fade.
                        if dist_from_cursor_line < gap * 1.5:
                            fill = upcoming_col
                        elif dist_from_cursor_line < gap * 4:
                            fill = near_col
                        else:
                            fill = far_col
                else:
                    # Voice not running — legacy coloring based on the
                    # active_line_index / scroll_position fade tiers.
                    if idx < cursor:
                        fill = spoken_col
                    elif idx == cursor:
                        fill = current_col
                    elif in_band:
                        fill = upcoming_col
                    else:
                        if dist_from_cursor_line < gap * 1.5:
                            fill = upcoming_col
                        elif dist_from_cursor_line < gap * 4:
                            fill = near_col
                        else:
                            fill = far_col

                self.draw_outlined_text(
                    wx, y, word["text"], fill, 0, font_tuple,
                    anchor="center", justify="center"
                )

    def _current_word_y(self):
        """Script-space y of the current voice cursor word (or 0 if empty)."""
        if not self.words:
            return 0
        cursor = max(0, min(self.voice_cursor_word, len(self.words) - 1))
        return self.words[cursor]["y"]

    def _sync_voice_to_scroll(self):
        """After a manual scroll (keyboard/wheel/grab-strip), anchor the voice
        cursor and viewport to the new scroll position.

        Without this, voice-follow's smooth viewport loop snaps the view
        back to the old cursor word within a frame or two, making manual
        scrolling feel 'locked'. We also clear the matcher history so the
        voice recognizer re-anchors to speech after the jump rather than
        trying to splice the new position onto the old transcript.

        Safe to call whether voice is running or not.
        """
        if not self.words:
            self.scroll_target = self.scroll_position
            return
        # Find the word whose y is closest to the new scroll position —
        # that's the one now sitting at the focus line.
        target_y = self.scroll_position
        best_idx = 0
        best_dist = float("inf")
        for i, w in enumerate(self.words):
            d = abs(w["y"] - target_y)
            if d < best_dist:
                best_dist = d
                best_idx = i
            elif w["y"] > target_y and d > best_dist:
                # Past the target and getting farther — stop scanning.
                break
        self.voice_cursor_word = best_idx
        # Park the smooth-follow loop on the new position so it doesn't
        # fight the manual scroll.
        self.scroll_target = self.scroll_position
        # Reset the matcher so it re-anchors to the next phrase spoken
        # instead of trying to continue from wherever it was.
        self._prev_heard_tokens = []
        self._pending_backward = 0

    def reset_to_top(self):
        self.active_line_index = 0
        self.scroll_position = self.display_line_offsets[0] if self.display_line_offsets else 0
        self._sync_voice_to_scroll()
        self.render_lines()
        self.status_msg("Reset to top.")

    def next_line(self):
        idx = self.active_line_index + 1
        while idx < len(self.display_lines) and self.display_lines[idx] == "":
            idx += 1
        if idx < len(self.display_lines):
            self.active_line_index = idx
            self.scroll_position = self.display_line_offsets[idx]
            self._sync_voice_to_scroll()
            self.render_lines()
            self.status_msg(f"Line {self.active_line_index + 1} / {len(self.display_lines)}")

    def prev_line(self):
        idx = self.active_line_index - 1
        while idx >= 0 and self.display_lines[idx] == "":
            idx -= 1
        if idx >= 0:
            self.active_line_index = idx
            self.scroll_position = self.display_line_offsets[idx]
            self._sync_voice_to_scroll()
            self.render_lines()
            self.status_msg(f"Line {self.active_line_index + 1} / {len(self.display_lines)}")

    def jump_lines(self, delta):
        """Jump N non-empty lines forward (delta>0) or back (delta<0).

        Used by fast-scroll shortcuts: Shift+MouseWheel, PageUp/PageDown, and
        held Up/Down. Stops gracefully at the top/bottom rather than wrapping.
        """
        if not self.display_lines:
            return
        step = 1 if delta > 0 else -1
        remaining = abs(int(delta))
        idx = self.active_line_index
        last_valid = idx
        while remaining > 0:
            idx += step
            if idx < 0 or idx >= len(self.display_lines):
                break
            if self.display_lines[idx] == "":
                continue
            last_valid = idx
            remaining -= 1
        if last_valid == self.active_line_index:
            return
        self.active_line_index = last_valid
        self.scroll_position = self.display_line_offsets[last_valid]
        self._sync_voice_to_scroll()
        self.render_lines()
        self.status_msg(f"Line {self.active_line_index + 1} / {len(self.display_lines)}")

    def jump_to_top(self):
        self.reset_to_top()

    def jump_to_end(self):
        if not self.display_lines:
            return
        idx = len(self.display_lines) - 1
        while idx > 0 and self.display_lines[idx] == "":
            idx -= 1
        self.active_line_index = idx
        self.scroll_position = self.display_line_offsets[idx] if self.display_line_offsets else 0
        self._sync_voice_to_scroll()
        self.render_lines()
        self.status_msg(f"Jumped to end (line {idx + 1}).")

    def toggle_scroll(self):
        # Voice follow takes priority. Don't let auto-scroll fight it.
        if self.voice_mode_running:
            self.status_msg("Auto-scroll disabled while Voice Follow is active. Stop voice follow first.")
            return
        self.is_scrolling = not self.is_scrolling
        if self.is_scrolling:
            self.status_msg("Smooth auto-scroll started.")
            self._last_scroll_time = time.time()
            # Track the integer-rounded pixel position we last rendered at, so we
            # can skip redraws when the scroll position hasn't advanced by at
            # least one full pixel yet.
            self._last_rendered_scroll_px = int(round(self.scroll_position))
            # Also track next frame's target wall-clock time for proper pacing.
            self._next_frame_time = time.time()
            # Auto-start the read timer when scrolling starts (v7.11). The
            # user can still pause/reset it manually from the Read Timer panel.
            self.timer_resume()
            self.auto_scroll_step()
        else:
            self.status_msg("Auto-scroll paused.")
            self.timer_pause()

    def auto_scroll_step(self):
        """
        Smooth auto-scroll with consistent frame pacing.

        The old implementation called `after(16, ...)` blindly, which caused
        jerkiness for three reasons:

          1. Tk's `after()` has ~10-15ms granularity on Windows, not 1ms, so
             real frame intervals were 15-30ms with visible variance.
          2. Frame time `dt` was measured from `time.time()` each call, so
             scheduler jitter turned into scroll-speed jitter.
          3. Every frame did a full canvas redraw (delete-all + recreate all
             text items with stroke offsets), which at realistic script lengths
             cannot hit 60fps and caused further jitter.

        This version:
          * Uses an integer-target-time pacer so accumulated scheduling error
            doesn't drift the scroll rate.
          * Tracks the last-rendered integer pixel position. If the new scroll
            position hasn't moved at least 1 pixel since the last render, skip
            the render entirely (but still advance scroll_position for the
            next frame).
          * Continues to compute scroll_position from real dt so the visible
            speed matches the configured pixels/second regardless of how fast
            or slow frames actually come out.
        """
        if not self.is_scrolling:
            return
        # Safety: if voice follow turned on between frames, bail out.
        if self.voice_mode_running:
            self.is_scrolling = False
            return

        now = time.time()
        dt = now - getattr(self, "_last_scroll_time", now)
        # Clamp dt: if the window was dragged, alt-tabbed, or a modal dialog
        # stalled the loop for seconds, don't lurch the scroll forward by that
        # accumulated amount. Anything over ~100ms is treated as a pause.
        if dt > 0.1:
            dt = 0.016
        self._last_scroll_time = now

        speed = max(0.0, float(self.scroll_speed.get()))
        self.scroll_position += speed * dt

        # Only redraw if we've actually moved a whole pixel. This is a big win
        # when scroll_speed is small (a few px/s) — we can go 3-5 frames
        # between redraws, letting Tk breathe.
        new_px = int(round(self.scroll_position))
        if new_px != getattr(self, "_last_rendered_scroll_px", new_px - 1):
            self._last_rendered_scroll_px = new_px
            self.render_lines()

        # Pacing: target 60fps, but schedule the NEXT frame relative to when
        # THIS frame was supposed to happen, not when it actually ran. This
        # prevents scheduling jitter from compounding into wobble.
        FRAME_MS = 16
        self._next_frame_time = getattr(self, "_next_frame_time", now) + (FRAME_MS / 1000.0)
        remaining_ms = int(max(1, (self._next_frame_time - time.time()) * 1000))
        # If we fell behind by more than one frame, don't try to catch up —
        # just reset the target to now + one frame. Otherwise we'd spin in a
        # tight loop on a slow machine.
        if remaining_ms > FRAME_MS * 2:
            remaining_ms = FRAME_MS
            self._next_frame_time = time.time() + (FRAME_MS / 1000.0)
        self.overlay.after(remaining_ms, self.auto_scroll_step)

    def _capture_with_sounddevice(self, seconds=2.0, sample_rate=16000):
        frames = int(sample_rate * seconds)
        mic_idx = int(self.voice_mic_index.get())
        kwargs = dict(samplerate=sample_rate, channels=1, dtype='int16')
        if mic_idx >= 0:
            kwargs["device"] = mic_idx
        audio = sd.rec(frames, **kwargs)
        sd.wait()
        raw = audio.tobytes()
        return sr.AudioData(raw, sample_rate, 2)

    def _capture_with_pyaudio_mic(self, recognizer):
        mic_idx = int(self.voice_mic_index.get())
        mic_kwargs = dict(sample_rate=16000)
        if mic_idx >= 0:
            mic_kwargs["device_index"] = mic_idx

        # Calibrate energy threshold once per session, not per capture —
        # recalibrating every ~1s eats valuable recognition latency.
        if not getattr(self, "_pyaudio_calibrated", False):
            try:
                with sr.Microphone(**mic_kwargs) as source:
                    recognizer.adjust_for_ambient_noise(source, duration=0.3)
                self._pyaudio_calibrated = True
            except Exception:
                pass

        with sr.Microphone(**mic_kwargs) as source:
            # timeout=1: if nothing is heard within 1s, raise WaitTimeoutError
            # and let the outer loop try again. This keeps latency bounded.
            audio = recognizer.listen(
                source,
                timeout=1,
                phrase_time_limit=max(0.8, float(self.voice_seconds.get()))
            )
        return audio

    def _copy_pip_command(self):
        """Copy a ready-to-paste install command tied to THIS interpreter."""
        cmd = f'"{sys.executable}" -m pip install vosk pyaudio sounddevice SpeechRecognition'
        try:
            self.controls.clipboard_clear()
            self.controls.clipboard_append(cmd)
            self.controls.update()
            messagebox.showinfo(
                "Install Command Copied",
                "Paste this in a command prompt (NOT inside Python):\n\n"
                f"{cmd}\n\n"
                "This uses the exact Python that's running this app, so the install "
                "will go to the right place."
            )
        except Exception as e:
            messagebox.showerror("Copy Failed", str(e))

    def _on_backend_changed(self):
        self.save_settings()
        # Mic list depends on backend, so rebuild it.
        self._refresh_mic_list()

    def _list_mics_for_backend(self, backend):
        """Return a list of (index, name) tuples for the given backend. index=-1 means default.

        Vosk and sounddevice both use PortAudio via the sounddevice package —
        Vosk's streaming loop opens an sd.RawInputStream internally — so they
        share the same device enumeration. Only the pyaudio backend uses a
        different device list (enumerated via SpeechRecognition/PortAudio
        through PyAudio's own API).
        """
        mics = [(-1, "System Default")]
        try:
            if backend in ("sounddevice", "vosk"):
                if SOUNDDEVICE_OK:
                    devices = sd.query_devices()
                    for i, d in enumerate(devices):
                        if d.get("max_input_channels", 0) > 0:
                            name = d.get("name", f"Device {i}")
                            mics.append((i, name))
            elif backend == "pyaudio":
                if SPEECH_OK and PYAUDIO_OK:
                    try:
                        names = sr.Microphone.list_microphone_names()
                        for i, name in enumerate(names):
                            if name:
                                mics.append((i, name))
                    except Exception:
                        pass
        except Exception as e:
            self.status_msg(f"Mic enumeration failed: {e}")
        return mics

    def _refresh_mic_list(self):
        """Rebuild the mic dropdown based on the effective backend.

        Preserves the previously-selected mic by matching BOTH the saved index
        AND saved name. Matching by name lets the selection survive Windows
        re-numbering input devices when audio hardware is plugged/unplugged.
        Matching by index handles the case where names collide or were never
        saved (legacy settings files).
        """
        backend = self._choose_voice_backend()
        self._mic_cache_backend = backend
        self._mic_cache = self._list_mics_for_backend(backend)

        display = [f"[{backend}] {name}" for _, name in self._mic_cache]
        self.mic_combo["values"] = display

        if not display:
            return

        prev_idx = int(self.voice_mic_index.get())
        # Extract the bare name from the stored label — strip the " (#N)" suffix
        # we added at display time if present.
        saved_label = self.voice_mic_label.get()
        saved_name = re.sub(r"\s*\(#\d+\)\s*$", "", saved_label) if saved_label else ""

        target_row = 0  # fallback = "System Default" at row 0
        # Priority 1: exact index match AND name match.
        for row, (idx, name) in enumerate(self._mic_cache):
            if idx == prev_idx and (not saved_name or name == saved_name or saved_name == "System Default"):
                target_row = row
                break
        else:
            # Priority 2: name-only match (device may have been renumbered).
            if saved_name and saved_name != "System Default":
                for row, (idx, name) in enumerate(self._mic_cache):
                    if name == saved_name:
                        target_row = row
                        break
            # Priority 3: index-only match (legacy without saved name).
            elif prev_idx != -1:
                for row, (idx, _) in enumerate(self._mic_cache):
                    if idx == prev_idx:
                        target_row = row
                        break

        self.mic_combo.current(target_row)
        sel_idx, sel_name = self._mic_cache[target_row]
        self.voice_mic_index.set(sel_idx)
        self.voice_mic_label.set(sel_name if sel_idx == -1 else f"{sel_name} (#{sel_idx})")

    def _on_mic_selected(self, _event=None):
        row = self.mic_combo.current()
        if row < 0 or row >= len(self._mic_cache):
            return
        sel_idx, sel_name = self._mic_cache[row]
        self.voice_mic_index.set(sel_idx)
        self.voice_mic_label.set(sel_name if sel_idx == -1 else f"{sel_name} (#{sel_idx})")
        self.save_settings()
        self.status_msg(f"Mic set to: {self.voice_mic_label.get()}")

    def _choose_voice_backend(self):
        """Resolve the effective backend from the user's radio selection.

        Auto mode prefers Vosk (genuinely streaming, offline, ~300ms latency)
        when both the library and the downloaded model are available. Falls
        back to the Google-cloud paths otherwise, preferring PyAudio since it
        integrates more smoothly with SpeechRecognition's phrase detection.
        """
        choice = self.voice_backend.get()
        if choice == "vosk":
            return "vosk"
        if choice == "sounddevice":
            return "sounddevice"
        if choice == "pyaudio":
            return "pyaudio"
        # Auto:
        if VOSK_OK and SOUNDDEVICE_OK and self._vosk_model_available():
            return "vosk"
        if PYAUDIO_OK:
            return "pyaudio"
        if SOUNDDEVICE_OK:
            return "sounddevice"
        return "pyaudio"

    def _vosk_model_dir(self):
        """Return the expected path of the Vosk model directory next to the script."""
        return os.path.join(
            os.path.dirname(os.path.abspath(__file__)),
            VOSK_MODEL_DIR_NAME
        )

    def _vosk_model_available(self):
        """True if the Vosk model directory exists and looks plausible."""
        d = self._vosk_model_dir()
        if not os.path.isdir(d):
            return False
        # Vosk models must contain these subdirs/files; quick sanity check.
        return os.path.exists(os.path.join(d, "am")) or os.path.exists(os.path.join(d, "conf"))

    def _get_or_load_vosk_model(self):
        """Load (and cache) the Vosk model. Raises RuntimeError with a clear
        message if the model isn't available or fails to load."""
        if not VOSK_OK:
            raise RuntimeError(
                "Vosk is not installed.\n\n"
                f'Install it with:\n"{sys.executable}" -m pip install vosk'
            )
        cached = getattr(self, "_vosk_model_cache", None)
        if cached is not None:
            return cached

        model_dir = self._vosk_model_dir()
        if not self._vosk_model_available():
            raise RuntimeError(
                f"Vosk model not found at:\n{model_dir}\n\n"
                'Click "Download Vosk Model" in the Voice panel to fetch it '
                "automatically (~40MB), or download manually from:\n"
                f"{VOSK_MODEL_URL}"
            )
        # Loading the model takes a few seconds the first time.
        self.status_msg("Loading Vosk model (first time only)...")
        self.controls.update_idletasks()
        model = vosk.Model(model_dir)
        self._vosk_model_cache = model
        self.status_msg("Vosk model loaded.")
        return model

    def download_vosk_model(self, skip_confirm=False):
        """Download and unzip the small English Vosk model next to the script.
        Runs on a background thread so the UI stays responsive.

        Parameters:
          skip_confirm: If True, skip the confirmation dialog (useful when the
            caller has already confirmed — e.g. the "Vosk Model Missing" prompt
            in toggle_voice_follow).
        """
        target_dir = self._vosk_model_dir()
        if self._vosk_model_available():
            messagebox.showinfo(
                "Already Installed",
                f"Vosk model already exists at:\n{target_dir}",
                parent=self.controls
            )
            return
        script_dir = os.path.dirname(os.path.abspath(__file__))
        if not skip_confirm:
            if not messagebox.askyesno(
                "Download Vosk Model?",
                f"This will download approximately 40MB from:\n{VOSK_MODEL_URL}\n\n"
                f"to:\n{script_dir}\n\n"
                "Continue?",
                parent=self.controls
            ):
                return

        # Make sure the worker can see the intended download path even if an
        # exception fires before the inner assignment (previously a potential
        # NameError in the except block).
        zip_path = os.path.join(script_dir, "vosk-model.zip")

        def _do_download():
            try:
                self.root.after(0, lambda: self.status_msg("Downloading Vosk model..."))

                def _progress(block_num, block_size, total_size):
                    if total_size > 0:
                        pct = min(100, int(block_num * block_size * 100 / total_size))
                        self.root.after(0, lambda p=pct: self.status_msg(
                            f"Downloading Vosk model... {p}%"
                        ))

                urllib.request.urlretrieve(VOSK_MODEL_URL, zip_path, _progress)

                self.root.after(0, lambda: self.status_msg("Extracting Vosk model..."))
                with zipfile.ZipFile(zip_path, "r") as zf:
                    zf.extractall(script_dir)
                try:
                    os.remove(zip_path)
                except Exception:
                    pass

                if self._vosk_model_available():
                    self.root.after(0, lambda: self.status_msg(
                        "Vosk model installed. Voice backend ready."
                    ))
                    self.root.after(0, lambda: messagebox.showinfo(
                        "Model Installed",
                        f"Vosk model installed at:\n{target_dir}\n\n"
                        "You can now use the 'Vosk (offline)' voice backend.",
                        parent=self.controls
                    ))
                else:
                    self.root.after(0, lambda: messagebox.showerror(
                        "Install Failed",
                        "Downloaded and extracted, but the expected model "
                        "folder was not found. Check the script directory "
                        "manually.",
                        parent=self.controls
                    ))
            except Exception as e:
                err = str(e)
                self.root.after(0, lambda err=err: self.status_msg(f"Download failed: {err}"))
                self.root.after(0, lambda err=err: messagebox.showerror(
                    "Download Failed",
                    f"Could not download the Vosk model:\n\n{err}\n\n"
                    f"You can download it manually from:\n{VOSK_MODEL_URL}\n"
                    f"and unzip it into:\n{script_dir}",
                    parent=self.controls
                ))
                try:
                    if os.path.exists(zip_path):
                        os.remove(zip_path)
                except Exception:
                    pass

        threading.Thread(target=_do_download, daemon=True).start()
        choice = self.voice_backend.get()
        if choice == "vosk":
            return "vosk"
        if choice == "sounddevice":
            return "sounddevice"
        if choice == "pyaudio":
            return "pyaudio"
        # Auto: prefer Vosk first (genuinely streaming, offline, low latency),
        # then fall back to the cloud-backed Google paths.
        if VOSK_OK and SOUNDDEVICE_OK and self._vosk_model_available():
            return "vosk"
        if PYAUDIO_OK:
            return "pyaudio"
        if SOUNDDEVICE_OK:
            return "sounddevice"
        return "pyaudio"

    def toggle_voice_follow(self):
        backend = self._choose_voice_backend()

        # Pre-flight checks per backend. Vosk has completely different
        # dependencies than the Google paths — it doesn't need
        # SpeechRecognition and uses sounddevice directly.
        if backend == "vosk":
            if not VOSK_OK:
                messagebox.showinfo(
                    "Voice Follow Unavailable",
                    "Vosk is not installed.\n\n"
                    f'Install it with:\n"{sys.executable}" -m pip install vosk\n\n'
                    'Then restart the app. Or pick a different Voice Backend.'
                )
                return
            if not SOUNDDEVICE_OK:
                messagebox.showinfo(
                    "Voice Follow Unavailable",
                    "Vosk needs sounddevice for audio input.\n\n"
                    f'Install it with:\n"{sys.executable}" -m pip install sounddevice'
                )
                return
            if not self._vosk_model_available():
                if messagebox.askyesno(
                    "Vosk Model Missing",
                    "Vosk is installed but the speech model hasn't been "
                    "downloaded yet.\n\n"
                    "Download the small English model now (~40MB)?",
                    parent=self.controls
                ):
                    # User already confirmed in THIS dialog — skip the second
                    # confirmation inside download_vosk_model() so the
                    # download actually starts.
                    self.download_vosk_model(skip_confirm=True)
                return
        else:
            # Google-backed paths need SpeechRecognition.
            if not SPEECH_OK:
                messagebox.showinfo(
                    "Voice Follow Unavailable",
                    "SpeechRecognition is not installed (needed for Google "
                    "recognition).\n\n"
                    f'Install it with:\n"{sys.executable}" -m pip install SpeechRecognition\n\n'
                    "Or switch Voice Backend to Vosk (offline)."
                )
                return

            if backend == "sounddevice" and not SOUNDDEVICE_OK:
                messagebox.showinfo(
                    "Voice Follow Unavailable",
                    "sounddevice is not installed or not working.\n\nTry:\n"
                    f'"{sys.executable}" -m pip install sounddevice\n\n'
                    "Or switch Voice Backend to PyAudio."
                )
                return

            if backend == "pyaudio" and not PYAUDIO_OK:
                msg = (
                    "PyAudio is not working in this Python.\n\n"
                    f"Details: {PYAUDIO_ERR or 'unknown'}\n\n"
                    "Fixes to try:\n"
                    "  1. Switch Voice Backend to Vosk (offline) or Sounddevice.\n"
                    "  2. Reinstall PyAudio matching your Python shown in the\n"
                    "     Voice panel's Python path.\n"
                    "  3. On Windows, use a prebuilt wheel for your Python version."
                )
                messagebox.showinfo("Voice Follow Unavailable", msg)
                return

        if self.voice_mode_running:
            self.stop_voice_follow()
        else:
            self.start_voice_follow()

    def start_voice_follow(self):
        # Remember whether auto-scroll was running so we can restore it cleanly
        # (but we won't auto-resume it — user can press Start/Pause again if they want).
        self._was_scrolling_before_voice = self.is_scrolling
        if self.is_scrolling:
            self.is_scrolling = False  # pause auto-scroll; voice is authoritative now

        self.stop_voice_flag = False
        self.voice_mode_running = True
        self._voice_err_count = 0
        self._prev_heard_tokens = []
        self._pending_backward = 0
        # Force ambient-noise recalibration on the first PyAudio capture of
        # this session. Reusing the prior session's threshold would miss any
        # background noise changes (fan turning on, window opened, etc.).
        self._pyaudio_calibrated = False

        # Preserve the voice cursor across pause/resume — the user almost
        # always wants to pick up where they left off. Only clamp if the script
        # has changed and the cursor is now out of range. Use the explicit
        # "Reset Voice Cursor" button to restart from the top.
        if self.voice_cursor_word >= len(self.words):
            self.voice_cursor_word = max(0, len(self.words) - 1 if self.words else 0)

        # Seed the smooth-follow target so the viewport centers on wherever
        # we're resuming from (not necessarily word 0).
        if self.words:
            self.scroll_target = self.words[self.voice_cursor_word]["y"]
        else:
            self.scroll_target = self.scroll_position
        # Snap scroll_position to target so the first karaoke render shows the
        # correct position instead of interpolating from wherever the
        # line-based viewport was.
        self.scroll_position = self.scroll_target

        # Grey out the scroll button to make the priority visible.
        try:
            self._top_buttons["scroll"].config(state="disabled")
        except Exception:
            pass

        self.voice_label.config(
            text=f"Voice follow: ON ({self._choose_voice_backend()})",
            fg="#88ff99"
        )
        self.status_msg("Voice follow started. Auto-scroll paused.")
        # Auto-start the read timer when voice follow starts (v7.11).
        self.timer_resume()
        self.voice_thread = threading.Thread(target=self._voice_follow_loop, daemon=True)
        self.voice_thread.start()

        # Kick off the smooth viewport-follow loop on the Tk main thread.
        # This runs at ~60Hz independently of the speech-recognition thread,
        # so the screen glides toward wherever the voice cursor lands.
        self.overlay.after(0, self._viewport_follow_step)
        # Force an immediate re-render so the karaoke path engages right away.
        self.render_lines()

    def stop_voice_follow(self):
        self.stop_voice_flag = True
        self.voice_mode_running = False
        self.voice_label.config(text="Voice follow: OFF", fg="#ffcc66")

        # Re-enable the scroll button.
        try:
            self._top_buttons["scroll"].config(state="normal")
        except Exception:
            pass

        # Switch the canvas back to the legacy line renderer immediately.
        self.render_lines()
        # Auto-pause the read timer (v7.11). User can resume manually.
        self.timer_pause()
        self.status_msg("Voice follow stopped. You can resume auto-scroll manually.")

    def reset_voice_cursor(self):
        """Send the voice cursor back to word 0 without stopping voice follow.
        Useful if the matcher drifts or you want to restart from the top."""
        self.voice_cursor_word = 0
        self._prev_heard_tokens = []
        if self.words:
            self.scroll_target = self.words[0]["y"]
            # Snap immediately if voice is running; otherwise set scroll_position too.
            if not self.voice_mode_running:
                self.scroll_position = self.scroll_target
        self.render_lines()
        self.status_msg("Voice cursor reset to top.")

    def _tokenize(self, text):
        """Lowercase, strip non-letters, return word list."""
        return [w for w in re.findall(r"[a-z0-9']+", text.lower()) if w]

    def _build_script_tokens(self):
        """Legacy: kept so older code paths don't break. Word-level voice
        follow uses self.words directly instead."""
        self._script_tokens = []
        for i, line in enumerate(self.display_lines):
            if not line.strip():
                continue
            toks = self._tokenize(line)
            if toks:
                self._script_tokens.append((i, toks))

    def _word_slide_match(self, heard_tokens, is_partial=False):
        """
        Word-level sliding match.

        Given a phrase from speech recognition, find the best forward alignment
        of those tokens to self.words starting from the current cursor.

        Parameters:
          heard_tokens: list of normalized word tokens from the recognizer
          is_partial: True if this is a streaming partial from Vosk (which
            *extends* over time: "the", "the quick", "the quick brown", ...).
            False for Google batch results (which overlap between captures).

        The is_partial distinction matters for dedup logic:
          * Batch captures OVERLAP — we strip the shared prefix from each new
            phrase so we only match the genuinely-new tail.
          * Streaming partials EXTEND — stripping the shared prefix would strip
            everything, since the new partial always starts with the old one.
            Instead we work with the tail window of the partial, since that's
            where the speaker currently is.
        """
        if not heard_tokens or not self.words:
            return None, 0.0

        script_norm = [w["norm"] for w in self.words]
        N = len(script_norm)
        cursor = max(0, min(self.voice_cursor_word, N - 1))

        if is_partial:
            # For streaming partials, focus on the last ~8 words of the phrase
            # since that's where the speaker currently is. Older words in the
            # partial have (hopefully) already been used to advance the cursor
            # via previous partial updates.
            effective = heard_tokens[-8:]
            # Don't update _prev_heard_tokens on partials — the batch dedup
            # path needs its own independent tracking.
        else:
            # Batch mode: dedup overlapping audio windows.
            prev = getattr(self, "_prev_heard_tokens", [])
            fresh = heard_tokens
            if prev:
                max_overlap = min(len(prev), len(heard_tokens))
                for k in range(max_overlap, 0, -1):
                    if prev[-k:] == heard_tokens[:k]:
                        fresh = heard_tokens[k:]
                        break
            self._prev_heard_tokens = heard_tokens
            effective = fresh if len(fresh) >= 2 else heard_tokens

        if not effective:
            return None, 0.0

        # Search window: small lookback (re-reads) + forward window scaled with
        # phrase length so we can handle pauses / long phrases.
        lookback = 3
        lookahead = max(20, len(effective) * 6)
        start_lo = max(0, cursor - lookback)
        start_hi = min(N, cursor + lookahead)

        best_end = None
        best_score = 0.0
        best_start = None

        for start in range(start_lo, start_hi):
            if script_norm[start] == "":
                continue
            si = start
            matched = 0
            last_matched = start
            MAX_GAP = 3
            for tok in effective:
                found = -1
                for probe in range(si, min(N, si + MAX_GAP + 1)):
                    if script_norm[probe] == tok:
                        found = probe
                        break
                if found >= 0:
                    matched += 1
                    last_matched = found
                    si = found + 1
                else:
                    si += 1
                    if si >= N:
                        break

            if matched == 0:
                continue
            score = matched / len(effective)

            distance_from_cursor = start - cursor
            if 0 <= distance_from_cursor <= 3:
                score += 0.15
            elif distance_from_cursor < 0:
                score -= 0.10 * abs(distance_from_cursor)
            if distance_from_cursor > 15:
                score -= 0.10

            if score > best_score:
                best_score = score
                best_end = last_matched
                best_start = start

        # Minimum confidence: require at least ~40% of heard tokens matched
        # AND a reasonable absolute score.
        MIN_SCORE = 0.50
        if best_end is None or best_score < MIN_SCORE:
            return None, best_score

        # Don't allow cursor to go backward unless the evidence is strong.
        if best_end < cursor:
            # Require a very high score for a re-read rewind.
            if best_score < 0.80:
                return None, best_score

        return best_end, best_score

    def _advance_voice_cursor(self, new_cursor):
        """Advance the voice cursor and set the viewport target to center it."""
        if 0 <= new_cursor < len(self.words):
            self.voice_cursor_word = new_cursor
            # Set scroll target so that this word sits at the focus line.
            self.scroll_target = self.words[new_cursor]["y"]
            # Render immediately to update the karaoke highlight, even if the
            # viewport doesn't need to move (e.g. cursor advanced within the
            # same line). The _viewport_follow_step will do additional renders
            # while it's interpolating toward the target.
            self.render_lines()

    def _viewport_follow_step(self):
        """
        Smoothly interpolate scroll_position toward scroll_target.
        Runs on the overlay's Tk loop at ~60Hz while voice is active.
        """
        if not self.voice_mode_running:
            return
        # Exponential smoothing toward target. Higher alpha = snappier.
        alpha = 0.18
        delta = self.scroll_target - self.scroll_position
        if abs(delta) < 0.5:
            self.scroll_position = self.scroll_target
        else:
            self.scroll_position += delta * alpha
            self.render_lines()
        # Always schedule another step while voice is running, even at rest —
        # cheap, and keeps the loop alive for the next advance.
        self.overlay.after(16, self._viewport_follow_step)

    def _voice_follow_loop(self):
        """
        Top-level voice follow dispatcher. Routes to the appropriate backend
        worker based on the current setting. Vosk is a genuinely streaming
        recognizer and uses its own loop; Google backends share a pipelined
        batch loop.
        """
        backend = self._choose_voice_backend()
        if backend == "vosk":
            self._voice_follow_loop_vosk()
        else:
            self._voice_follow_loop_google(backend)

    def _voice_follow_loop_vosk(self):
        """
        Streaming voice follow using Vosk + sounddevice.

        This is the low-latency path. Instead of capturing discrete chunks and
        sending them to a network API (which has ~1-2s of unavoidable lag per
        chunk), we open a continuous audio stream and feed it to Vosk's
        KaldiRecognizer in small blocks. Vosk returns:

          * partial results: interim transcription that updates roughly every
            200ms as you keep speaking
          * final results: emitted when Vosk decides you've reached a phrase
            boundary (pause in speech)

        For teleprompter following, partials are what we want — they let the
        highlight advance almost word-by-word with ~300ms of perceived lag.

        Architecture:
          * sounddevice opens an InputStream in a dedicated PortAudio thread
            (that's how sd.RawInputStream works) and fills a queue with small
            audio blocks.
          * This function sits in a loop on the voice-follow thread, pulling
            blocks out of the queue, feeding them to vosk, and sending each
            partial/final transcript to the main Tk thread via root.after for
            matching + cursor advance.
        """
        import queue as _queue

        # Load model (may raise if not available — catch and show a message).
        try:
            model = self._get_or_load_vosk_model()
        except Exception as e:
            self.root.after(0, lambda err=str(e): messagebox.showerror("Vosk Error", err))
            self.root.after(0, self.stop_voice_follow)
            return

        SAMPLE_RATE = 16000
        # Small block size means low latency into Vosk. 4000 samples = 250ms
        # at 16kHz. Going smaller (1600 = 100ms) is possible but increases
        # CPU overhead without improving perceived latency much.
        BLOCK_FRAMES = 4000

        recognizer = vosk.KaldiRecognizer(model, SAMPLE_RATE)
        recognizer.SetWords(False)  # we don't need per-word timing
        # Enable partial results so we get interim transcripts as the user speaks.
        # (PartialWords would give us timing info per partial word but is heavier.)
        try:
            recognizer.SetPartialWords(False)
        except Exception:
            pass

        audio_q = _queue.Queue(maxsize=32)

        def _sd_callback(indata, frames, time_info, status):
            # Called from sounddevice's internal audio thread. Keep it cheap.
            if status:
                # Overflow/underflow info — non-fatal, just log.
                pass
            try:
                audio_q.put_nowait(bytes(indata))
            except _queue.Full:
                # Drop blocks if we're falling behind rather than growing the
                # queue unboundedly. A couple of dropped blocks at startup is
                # fine; sustained drops would indicate a real problem.
                pass

        mic_idx = int(self.voice_mic_index.get())
        stream_kwargs = dict(
            samplerate=SAMPLE_RATE,
            blocksize=BLOCK_FRAMES,
            dtype="int16",
            channels=1,
            callback=_sd_callback,
        )
        if mic_idx >= 0:
            stream_kwargs["device"] = mic_idx

        # Track the last partial transcript we processed so we don't re-run
        # the matcher unnecessarily on identical partials. Vosk emits the same
        # partial repeatedly until something changes.
        last_partial = ""

        try:
            with sd.RawInputStream(**stream_kwargs):
                self.root.after(0, lambda: self.status_msg(
                    "Vosk streaming active. Speak normally."
                ))
                while not self.stop_voice_flag:
                    try:
                        block = audio_q.get(timeout=0.1)
                    except _queue.Empty:
                        continue

                    if recognizer.AcceptWaveform(block):
                        # A final (phrase-boundary) result. Use it, then reset
                        # so the partial tracker starts fresh for next phrase.
                        result = json.loads(recognizer.Result())
                        text = result.get("text", "").strip()
                        last_partial = ""
                        if text:
                            self._dispatch_vosk_text(text, is_final=True)
                    else:
                        # Partial result — interim transcript of the current
                        # ongoing phrase.
                        partial = json.loads(recognizer.PartialResult())
                        text = partial.get("partial", "").strip()
                        if text and text != last_partial:
                            last_partial = text
                            self._dispatch_vosk_text(text, is_final=False)

        except Exception as e:
            err_text = str(e)
            self.root.after(0, lambda err=err_text: messagebox.showerror(
                "Vosk Stream Error",
                f"Voice follow stopped:\n\n{err}\n\n"
                "If this mentions the audio device, try picking a different "
                "Microphone or refreshing the mic list."
            ))
            self.root.after(0, self.stop_voice_follow)

    def _dispatch_vosk_text(self, text, is_final):
        """Send a Vosk transcript to the main thread for matching + highlight
        advance. Called from the Vosk audio thread."""
        heard_tokens = self._tokenize(text)
        if not heard_tokens:
            return

        def _apply():
            if not self.voice_mode_running:
                return
            # Partials are streaming extensions of the current phrase; finals
            # are phrase-boundary snapshots. The matcher handles them
            # differently — see _word_slide_match docstring.
            new_cursor, score = self._word_slide_match(heard_tokens, is_partial=not is_final)
            if new_cursor is not None:
                self._advance_voice_cursor(new_cursor)
                tag = "FINAL" if is_final else "partial"
                self.status_msg(
                    f'[{tag}] "{text[:35]}" → word {new_cursor + 1}/{len(self.words)} '
                    f'(score={score:.2f})'
                )
            # On no-match we deliberately don't update the status line — with
            # partials arriving several times per second, logging every miss
            # would be noisy.
        self.root.after(0, _apply)

    def _voice_follow_loop_google(self, backend):
        """
        Legacy Google-cloud path (pipelined but still batch). Kept as a
        fallback when Vosk isn't installed or the model isn't downloaded.
        Produces ~1-2s of latency vs Vosk's ~300ms.
        """
        recognizer = sr.Recognizer()

        # Unbounded-but-drained queue of recognition results, (sequence, text).
        self._recog_results = []
        self._recog_lock = threading.Lock()
        seq_counter = [0]

        def _recognize_worker(seq, audio):
            try:
                text = recognizer.recognize_google(audio)
            except sr.UnknownValueError:
                text = ""
            except sr.WaitTimeoutError:
                text = ""
            except Exception as e:
                text = ""
                self.root.after(0, lambda err=str(e): self.status_msg(f"Recognition: {err}"))
            with self._recog_lock:
                self._recog_results.append((seq, text))

        def _drain_results():
            if not self.voice_mode_running:
                return
            with self._recog_lock:
                self._recog_results.sort(key=lambda x: x[0])
                pending = self._recog_results[:]
                self._recog_results.clear()

            for seq, text in pending:
                if not text:
                    continue
                heard_tokens = self._tokenize(text)
                new_cursor, score = self._word_slide_match(heard_tokens)
                if new_cursor is not None:
                    self._advance_voice_cursor(new_cursor)
                    self.status_msg(
                        f'"{text[:40]}" → word {new_cursor + 1}/{len(self.words)} '
                        f'(score={score:.2f})'
                    )
                else:
                    self.status_msg(f'Heard (no match): "{text[:60]}"')

            self.overlay.after(50, _drain_results)

        self.overlay.after(50, _drain_results)

        while not self.stop_voice_flag:
            try:
                if backend == "sounddevice":
                    audio = self._capture_with_sounddevice(
                        seconds=float(self.voice_seconds.get())
                    )
                else:
                    audio = self._capture_with_pyaudio_mic(recognizer)

                seq_counter[0] += 1
                threading.Thread(
                    target=_recognize_worker,
                    args=(seq_counter[0], audio),
                    daemon=True
                ).start()

            except sr.WaitTimeoutError:
                time.sleep(0.02)

            except Exception as e:
                err_text = str(e)
                self.root.after(0, lambda err=err_text: self.status_msg(f"Voice capture: {err}"))
                time.sleep(0.3)
                self._voice_err_count = getattr(self, "_voice_err_count", 0) + 1
                if self._voice_err_count >= 5:
                    self.root.after(0, lambda err=err_text: messagebox.showerror(
                        "Voice Follow Stopped",
                        f"Voice follow stopped after repeated errors:\n\n{err}"
                    ))
                    self.root.after(0, self.stop_voice_follow)
                    return
            else:
                self._voice_err_count = 0

    def update_overlay_style(self):
        """Apply window transparency, always-on-top, and (new in v7.10) the
        color-key transparent-background setting.

        Color-key transparency uses Windows' built-in -transparentcolor
        attribute: any pixel of the specified color becomes fully see-through
        AND receives mouse input through to windows behind. This is a
        different mechanism than -alpha (which blends the entire window
        uniformly) — they can coexist, but when both are active the
        transparentcolor "wins" for pixels matching the color.

        When transparent_background is OFF, we clear -transparentcolor so the
        alpha-blend path takes over.
        """
        try:
            self.overlay.attributes("-alpha", float(self.window_transparency.get()))
        except Exception:
            pass
        self.overlay.attributes("-topmost", bool(self.always_on_top.get()))

        # Apply (or clear) the color-key transparency.
        if os.name == "nt":
            try:
                if self.transparent_background.get():
                    key_color = self.background_color.get().strip()
                    self.overlay.attributes("-transparentcolor", key_color)
                else:
                    # Clearing the attribute on Tk means setting it to an
                    # empty string. Some Tk versions reject "" — fall back
                    # to a highly unlikely color so no real pixels match.
                    try:
                        self.overlay.attributes("-transparentcolor", "")
                    except Exception:
                        self.overlay.attributes("-transparentcolor", "#fe00fe")
            except Exception as e:
                self.status_msg(f"Transparency: {e}")

        # Re-apply click-through if it was on — changing transparentcolor
        # can reset extended window styles on some Tk/Windows combinations.
        if self.click_through_enabled.get():
            self.overlay.after(50, lambda: self.set_click_through(True))

        self.render_lines()

    def toggle_borderless_mode(self, apply_only=False):
        self.overlay.overrideredirect(bool(self.borderless_mode.get()))
        # Changing overrideredirect can reset extended window styles on Windows,
        # so re-apply click-through if it was on.
        if self.click_through_enabled.get():
            self.overlay.after(50, lambda: self.set_click_through(True))
        self.overlay.after(100, self.render_lines)
        if not apply_only:
            self.status_msg("Borderless mode ON." if self.borderless_mode.get() else "Borderless mode OFF.")

    def toggle_overlay_visibility(self):
        if self.overlay_visible:
            self.overlay.withdraw()
            if hasattr(self, "grab_strip"):
                self.grab_strip.withdraw()
            self.overlay_visible = False
            self.status_msg("Overlay hidden.")
        else:
            self.overlay.deiconify()
            self.overlay_visible = True
            self._reposition_grab_strip()
            self.status_msg("Overlay shown.")

    def load_text_file(self):
        path = filedialog.askopenfilename(title="Load script", filetypes=[("Text Files", "*.txt"), ("All Files", "*.*")])
        if not path:
            return
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = f.read()
            self.editor.delete("1.0", "end")
            self.editor.insert("1.0", data)
            self.apply_script()
            self.status_msg(f"Loaded: {path}")
        except Exception as e:
            messagebox.showerror("Load Error", str(e))


def main():
    from teleprompter_instance import InstanceGate
    gate = InstanceGate()
    try:
        if not gate.acquire():
            return
        root = tk.Tk()
        app = TeleprompterApp(root)

        def activate():
            if not app.overlay_visible:
                app.toggle_overlay_visibility()
            app.overlay.deiconify()
            app.overlay.lift()
            app.controls.deiconify()
            app.controls.lift()
            app.controls.focus_force()

        def on_close():
            app.save_settings()
            root.destroy()

        gate.bind(root, activate)
        app.overlay.protocol("WM_DELETE_WINDOW", on_close)
        app.controls.protocol("WM_DELETE_WINDOW", on_close)
        root.mainloop()
    finally:
        gate.close()


if __name__ == "__main__":
    main()
