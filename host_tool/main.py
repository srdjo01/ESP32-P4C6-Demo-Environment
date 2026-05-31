"""
ESP32-P4C6 Demo Board Verification Tool
========================================

Entry point.  Run with:
    python main.py

Requirements:
    pip install PyQt6 pyserial
"""

import sys
from PyQt6.QtWidgets import QApplication
from PyQt6.QtCore import Qt
from ui.main_window import MainWindow


def main():
    app = QApplication(sys.argv)
    app.setApplicationName("ESP32-P4C6 Verification Tool")
    app.setOrganizationName("ESP32-P4C6 Demo")

    window = MainWindow()
    window.show()

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
