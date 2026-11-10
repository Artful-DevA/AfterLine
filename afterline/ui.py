"""Native Qt desktop interface with CPU-bound work off the UI thread."""
import shutil
import sys
import time
from pathlib import Path
from PySide6.QtCore import QObject, QThread, Qt, QTimer, Signal
from PySide6.QtWidgets import (QApplication, QCheckBox, QComboBox, QHBoxLayout,
    QLabel, QListWidget, QMainWindow, QMessageBox, QPushButton, QSplitter,
    QTabWidget, QTextEdit, QVBoxLayout, QWidget)
from . import __version__
from .audio import Capture, microphones, sources
from .processing import make_notes, transcribe
from .storage import create_session, finish_session, sessions

STYLE = """
QWidget {background:#F7F3EC; color:#292522; font-size:13px}
QLabel#heading {font-size:28px; font-weight:700}
QLabel#muted {color:#797067}
QPushButton {background:#ECE3D8; border:1px solid #D8CDBF;
border-radius:9px; padding:10px 14px; font-weight:600}
QPushButton:hover {background:#E4D5C6}
QPushButton#primary {background:#B85B3F; border:0; color:white}
QPushButton#primary:hover {background:#A14E36}
QPushButton:disabled {color:#9A8F82; background:#E6DDD1}
QComboBox {background:#FFFDF8; border:1px solid #D7CBBD;
border-radius:8px; padding:8px}
QTextEdit, QListWidget {background:#FFFDF9; border:1px solid #E2D7C9;
border-radius:11px; padding:8px; selection-background-color:#E4B39A}
QTabWidget::pane {border:0}
QTabBar::tab {background:#E8DED1; padding:10px 16px; border-radius:7px}
QTabBar::tab:selected {background:#D4AC97}
"""

class Events(QObject):
    progress = Signal(str)
    done = Signal(str, str)

class Worker(QThread):
    def __init__(self, folder, include_notes):
        super().__init__()
        self.folder, self.include_notes = Path(folder), include_notes
        self.events = Events()
    def run(self):
        try:
            text = transcribe(self.folder, self.events.progress.emit)
            if self.include_notes:
                make_notes(self.folder, text, self.events.progress.emit)
            self.events.done.emit(str(self.folder), "")
        except Exception as exc:
            self.events.done.emit(str(self.folder), str(exc))

class Window(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle(f"AfterLine {__version__} · Conversations, kept.")
        self.resize(1080, 710)
        self.captures = []
        self.folder = None
        self.worker = None
        self.started = None
        self.init_ui()
        self.refresh_sources()
        self.reload_sessions()
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.tick)
        self.timer.start(500)

    def init_ui(self):
        root = QWidget()
        layout = QVBoxLayout(root)
        layout.setContentsMargins(24, 24, 24, 24)
        layout.setSpacing(16)
        heading = QHBoxLayout()
        label = QLabel("AfterLine")
        label.setObjectName("heading")
        heading.addWidget(label)
        heading.addStretch()
        label = QLabel("LOCAL FIRST  ·  PRIVATE BY DESIGN")
        label.setObjectName("muted")
        heading.addWidget(label)
        layout.addLayout(heading)
        controls = QHBoxLayout()
        controls.addWidget(QLabel("Computer audio"))
        self.source = QComboBox()
        self.source.setMinimumWidth(320)
        controls.addWidget(self.source, 1)
        self.with_mic = QCheckBox("Include microphone")
        controls.addWidget(self.with_mic)
        self.microphone = QComboBox()
        self.microphone.setMinimumWidth(180)
        controls.addWidget(self.microphone)
        reload_button = QPushButton("↻ Sources")
        reload_button.clicked.connect(self.refresh_sources)
        controls.addWidget(reload_button)
        layout.addLayout(controls)
        actions = QHBoxLayout()
        self.record = QPushButton("●  Start recording")
        self.record.setObjectName("primary")
        self.record.clicked.connect(self.toggle)
        actions.addWidget(self.record)
        self.elapsed = QLabel("00:00")
        actions.addWidget(self.elapsed)
        self.transcript_button = QPushButton("Transcribe recording")
        self.transcript_button.clicked.connect(lambda: self.process(False))
        actions.addWidget(self.transcript_button)
        self.notes_button = QPushButton("Transcript + AI notes")
        self.notes_button.clicked.connect(lambda: self.process(True))
        actions.addWidget(self.notes_button)
        actions.addStretch()
        layout.addLayout(actions)
        self.status = QLabel("Choose a source and record a short test before your meeting.")
        self.status.setObjectName("muted")
        layout.addWidget(self.status)
        split = QSplitter(Qt.Horizontal)
        left = QWidget()
        side = QVBoxLayout(left)
        side.setContentsMargins(0, 0, 0, 0)
        side.addWidget(QLabel("RECORDINGS"))
        self.list = QListWidget()
        self.list.currentItemChanged.connect(self.show_session)
        side.addWidget(self.list)
        split.addWidget(left)
        tabs = QTabWidget()
        self.transcript = QTextEdit(readOnly=True)
        self.notes = QTextEdit(readOnly=True)
        tabs.addTab(self.transcript, "Full transcript")
        tabs.addTab(self.notes, "Meeting notes")
        split.addWidget(tabs)
        split.setSizes([270, 750])
        layout.addWidget(split, 1)
        self.setCentralWidget(root)
        self.setStyleSheet(STYLE)

    def refresh_sources(self):
        if self.captures:
            return
        old = self.source.currentData()
        self.source.clear()
        self.microphone.clear()
        try:
            for name, data in sources():
                self.source.addItem(name, data)
            for name, data in microphones():
                self.microphone.addItem(name, data)
            if old:
                index = self.source.findData(old)
                if index >= 0:
                    self.source.setCurrentIndex(index)
            self.status.setText("Sources updated. Application streams appear while the app is playing audio.")
        except Exception as exc:
            self.status.setText(f"Audio server unavailable: {exc}")

    def reload_sessions(self, selected=None):
        self.list.clear()
        for folder in sessions():
            self.list.addItem(folder.name.replace("recording-", "").replace("_", " "))
            self.list.item(self.list.count()-1).setData(Qt.UserRole, str(folder))
        if selected is not None:
            for index in range(self.list.count()):
                if self.list.item(index).data(Qt.UserRole) == str(selected):
                    self.list.setCurrentRow(index)
                    return
        if self.list.count():
            self.list.setCurrentRow(0)

    def current_folder(self):
        item = self.list.currentItem()
        return Path(item.data(Qt.UserRole)) if item else None

    def show_session(self, *_):
        folder = self.current_folder()
        if not folder:
            self.transcript.clear()
            self.notes.clear()
            return
        for file, target in [("transcript.txt", self.transcript), ("notes.md", self.notes)]:
            path = folder / file
            target.setPlainText(path.read_text(encoding="utf-8") if path.exists() else "")

    def toggle(self):
        if self.captures:
            self.stop_recording()
        else:
            self.start_recording()

    def start_recording(self):
        chosen = self.source.currentData()
        if not chosen:
            QMessageBox.warning(self, "No audio source", "Select an audio source.")
            return
        folder = create_session(self.source.currentText(), self.with_mic.isChecked())
        captures = []
        try:
            captures.append(Capture(folder / "computer.wav", *chosen))
            if self.with_mic.isChecked():
                source = self.microphone.currentData()
                if not source:
                    raise RuntimeError("No microphone is available")
                captures.append(Capture(folder / "microphone.wav", "monitor", source))
        except Exception as exc:
            for capture in captures:
                capture.stop()
            finish_session(folder, [str(exc)])
            QMessageBox.critical(self, "Recording error", str(exc))
            return
        self.captures, self.folder = captures, folder
        self.started = time.monotonic()
        self.record.setText("■  Stop & save")
        for widget in (self.source, self.microphone, self.with_mic,
                       self.transcript_button, self.notes_button):
            widget.setEnabled(False)
        self.status.setText("● Recording. Audio is saved locally as it arrives.")

    def stop_recording(self):
        errors = [error for capture in self.captures if (error := capture.stop())]
        self.captures = []
        finish_session(self.folder, errors)
        self.record.setText("●  Start recording")
        self.started = None
        self.elapsed.setText("00:00")
        for widget in (self.source, self.microphone, self.with_mic,
                       self.transcript_button, self.notes_button):
            widget.setEnabled(True)
        self.reload_sessions(self.folder)
        self.status.setText("Recording saved." if not errors else "Recording warning: " + "; ".join(errors))
        if errors:
            QMessageBox.warning(self, "Audio warning", "\n".join(errors))

    def tick(self):
        if self.started is not None:
            seconds = int(time.monotonic()-self.started)
            self.elapsed.setText(f"{seconds//60:02d}:{seconds%60:02d}")
            if any(cap.proc.poll() is not None for cap in self.captures):
                self.status.setText("WARNING: An audio capture process ended; stop and check the recording.")

    def process(self, notes):
        folder = self.current_folder()
        if not folder or self.captures or (self.worker and self.worker.isRunning()):
            return
        self.worker = Worker(folder, notes)
        self.worker.events.progress.connect(self.status.setText)
        self.worker.events.done.connect(self.processing_finished)
        self.transcript_button.setEnabled(False)
        self.notes_button.setEnabled(False)
        self.worker.start()

    def processing_finished(self, folder, error):
        self.transcript_button.setEnabled(True)
        self.notes_button.setEnabled(True)
        if self.current_folder() == Path(folder):
            self.show_session()
        self.status.setText("Processing complete." if not error else "Processing error: " + error)
        if error:
            QMessageBox.warning(self, "Processing error", error)

    def closeEvent(self, event):
        if self.captures:
            result = QMessageBox.question(self, "Save recording?",
                "Recording is active. Stop and save before quitting?",
                QMessageBox.Yes | QMessageBox.Cancel)
            if result != QMessageBox.Yes:
                event.ignore()
                return
            self.stop_recording()
        if self.worker and self.worker.isRunning():
            QMessageBox.information(self, "Still processing",
                                    "Please finish transcription before closing.")
            event.ignore()
            return
        event.accept()

def main():
    if not all(shutil.which(binary) for binary in ("pactl", "parec")):
        print("Install pulseaudio-utils (pactl and parec) first.", file=sys.stderr)
        raise SystemExit(1)
    app = QApplication(sys.argv)
    app.setApplicationName("AfterLine")
    window = Window()
    window.show()
    raise SystemExit(app.exec())
