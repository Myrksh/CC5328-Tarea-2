"""Ensamblaje de la GUI: carga el .ui generado por pyuic6, monta los
gráficos en los layouts, y conecta los controles con el ReceptorSerial
(QThread). Patrones de context/embeb-qt/main.py y main_qthread.py
(repo del auxiliar).
"""
import sys

from PyQt6.QtCore import QTimer, QThread, pyqtSlot
from PyQt6.QtWidgets import QApplication, QMainWindow
from serial.tools import list_ports

import protocol
from live_plot import LivePlot
from ui_not_celerometro import Ui_MainWindow
from receptor import ReceptorSerial

EJES = ("x", "y", "z")
AMPLITUD_MENU = (4, 8, 16)


class Ventana(QMainWindow):
    def __init__(self):
        super().__init__()
        self.ui = Ui_MainWindow()
        self.ui.setupUi(self)
        self.setWindowTitle("Tarea 1 — Acelerómetro")
        self.ui.pestanas.setCurrentIndex(0)

        self.hilo = None
        self.receptor = None
        self.pausado = False

        # Un gráfico por eje, insertado en los layouts vacíos del .ui
        self.graficos = {}
        for eje in EJES:
            grafico = LivePlot(f"Eje {eje.upper()}")
            getattr(self.ui, f"plot_layout_{eje}").addWidget(grafico)
            self.graficos[eje] = grafico

        self.llenar_puertos()
        self.conectar_senales()
        self.ui.action_desconectar.setEnabled(False)

    def conectar_senales(self):
        self.ui.action_conectar.triggered.connect(self.conectar)
        self.ui.action_desconectar.triggered.connect(self.desconectar)
        self.ui.action_inicializar.triggered.connect(self.inicializar)
        self.ui.pausa_graficos.setCheckable(True)
        self.ui.pausa_graficos.clicked.connect(self.alternar_pausa)
        for eje in EJES:
            getattr(self.ui, f"funcion_{eje}").currentIndexChanged.connect(
                self.enviar_config)
            getattr(self.ui, f"amplitud_{eje}").currentIndexChanged.connect(
                self.enviar_config)
            getattr(self.ui, f"frecuencia_{eje}").currentIndexChanged.connect(
                self.enviar_config)
        self.ui.periodo_combo.currentIndexChanged.connect(self.enviar_periodo)
        self.ui.baudios_combo.currentIndexChanged.connect(self.enviar_baudios)

    def llenar_puertos(self):
        """Puertos USB/ACM disponibles para llenar el combo"""
        self.ui.puerto_combo.clear()
        usb = [p.device for p in list_ports.comports()
               if "USB" in p.device or "ACM" in p.device]
        for puerto in sorted(usb):
            self.ui.puerto_combo.addItem(puerto)

    # --- conexión ---

    def conectado(self) -> bool:
        return self.receptor is not None and self.hilo is not None \
            and self.hilo.isRunning()

    def conectar(self):
        if self.conectado():
            return
        if not self.ui.puerto_combo.currentText():
            self.llenar_puertos()  # re-escaneo por si la placa llegó tarde
        puerto = self.ui.puerto_combo.currentText()
        if not puerto:
            self.ui.statusbar.showMessage("No hay puertos USB/ACM disponibles")
            return
        self.receptor = ReceptorSerial(puerto, protocol.BAUDIOS_MENU[0])
        self.hilo = QThread()
        self.receptor.moveToThread(self.hilo)
        self.hilo.started.connect(self.receptor.arrancar)
        self.hilo.finished.connect(self.receptor.cerrar)
        self.receptor.dato_eje.connect(self.recibir_dato)
        self.receptor.ambiente.connect(self.recibir_ambiente)
        self.receptor.respuesta.connect(self.mostrar_respuesta)
        self.receptor.conexion.connect(self.cambio_conexion)
        self.hilo.start()

    def desconectar(self):
        if self.hilo is not None:
            self.hilo.quit()
            self.hilo.wait()
            self.hilo = None
            self.receptor = None
        self.ui.action_conectar.setEnabled(True)
        self.ui.action_desconectar.setEnabled(False)
        self.ui.statusbar.showMessage("Desconectado")

    def cerrar_hilo(self):
        if self.hilo is not None:
            self.hilo.quit()
            self.hilo.wait()
            self.hilo = None
            self.receptor = None

    def cambio_conexion(self, ok: bool, motivo: str):
        if ok:
            self.ui.action_conectar.setEnabled(False)
            self.ui.action_desconectar.setEnabled(True)
            # el INICIAR automático dejó al ESP en defaults y 115200
            self.poner_defaults()
            self.ui.statusbar.showMessage(
                f"Conectado a {self.ui.puerto_combo.currentText()}")
        else:
            self.cerrar_hilo()
            self.ui.action_conectar.setEnabled(True)
            self.ui.action_desconectar.setEnabled(False)
            self.ui.statusbar.showMessage(f"Fallo de conexión: {motivo}")

    def inicializar(self):
        if not self.conectado():
            return
        self.receptor.mandar(protocol.comando_iniciar())
        self.poner_defaults()
        QTimer.singleShot(150,
                          lambda: self._pedir_reapertura(protocol.BAUDIOS_MENU[0]))

    # --- señales del receptor ---

    def recibir_dato(self, eje: str, valor: float):
        self.graficos[eje.lower()].agregar(valor)

    def recibir_ambiente(self, temp: float, hr: int):
        self.ui.temp_value.display(temp)
        self.ui.hum_value.display(hr)

    def mostrar_respuesta(self, cuerpo: str):
        if cuerpo.startswith("OK,BAUDIOS"):
            campos = cuerpo.split(",")
            if len(campos) == 3:
                nuevo = int(campos[2])
                QTimer.singleShot(100, lambda: self._pedir_reapertura(nuevo))
        self.ui.statusbar.showMessage(cuerpo, 4000)

    def alternar_pausa(self):
        self.pausado = self.ui.pausa_graficos.isChecked()
        if self.receptor is not None:
            self.receptor.pausado = self.pausado
        self.ui.statusbar.showMessage(
            "Lecturas pausadas" if self.pausado else "Lecturas activas")

    # --- comandos desde los combos ---

    def enviar_config(self):
        if not self.conectado():
            return
        nombre = self.sender().objectName()
        eje = nombre[-1].upper()
        combo = self.sender()
        if nombre.startswith("funcion_"):
            cmd = protocol.comando_funcion(eje, combo.currentIndex() + 1)
        elif nombre.startswith("amplitud_"):
            cmd = protocol.comando_amplitud(eje, AMPLITUD_MENU[combo.currentIndex()])
        else:
            cmd = protocol.comando_frecuencia(eje, int(combo.currentText()))
        self.receptor.mandar(cmd)

    def enviar_periodo(self):
        if self.conectado():
            self.receptor.mandar(
                protocol.comando_periodo(int(self.ui.periodo_combo.currentText())))

    def enviar_baudios(self):
        if not self.conectado():
            return
        self.receptor.mandar(
            protocol.comando_baudios(int(self.ui.baudios_combo.currentText())))

    def _pedir_reapertura(self, nuevo: int):
        if self.receptor is not None and self.receptor.baudios() != nuevo:
            self.receptor.pedir_baudios(nuevo)

    def poner_defaults(self):
        """Combos a los defaults del firmware, sin disparar comandos."""
        combos = []
        for eje in EJES:
            combos += [getattr(self.ui, f"funcion_{eje}"),
                       getattr(self.ui, f"amplitud_{eje}"),
                       getattr(self.ui, f"frecuencia_{eje}")]
        combos += [self.ui.periodo_combo, self.ui.baudios_combo]
        for combo in combos:
            combo.blockSignals(True)
        for eje in EJES:
            getattr(self.ui, f"funcion_{eje}").setCurrentIndex(0)
            getattr(self.ui, f"amplitud_{eje}").setCurrentIndex(0)
            getattr(self.ui, f"frecuencia_{eje}").setCurrentIndex(1)
        self.ui.periodo_combo.setCurrentIndex(0)
        self.ui.baudios_combo.setCurrentIndex(0)
        for combo in combos:
            combo.blockSignals(False)

    def closeEvent(self, evento):
        if self.hilo is not None:
            self.hilo.quit()
            self.hilo.wait()
        super().closeEvent(evento)


def main():
    app = QApplication(sys.argv)
    ventana = Ventana()
    ventana.show()
    app.exec()


if __name__ == "__main__":
    main()
