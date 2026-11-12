"""
GUI Tarea 2 — BME688 + ESP32
Control de adquisición remota, procesamiento embebido (Media Móvil y Picos)
y visualización en tiempo real mediante PyQt6.
"""
import sys
from PyQt6.QtCore import QTimer, QThread, pyqtSlot
from PyQt6.QtWidgets import QApplication, QMainWindow, QMessageBox
from serial.tools import list_ports

import protocol
from live_plot import LivePlot
from bme688_gui import Ui_MainWindow  # Ajustar si cambias el nombre del UI
from receptor import ReceptorSerial

VARIABLES = ("temp", "hum", "press")
FRECUENCIAS = (0.5, 1, 2, 5)
VENTANAS_W = (5, 10, 30, 60)

class Ventana(QMainWindow):
    def __init__(self):
        super().__init__()
        self.ui = Ui_MainWindow()
        self.ui.setupUi(self)
        self.setWindowTitle("Tarea 2 — BME688")

        self.ui.horizontalLayout_temp_box.setStretch(0, 2)
        self.ui.horizontalLayout_temp_box.setStretch(1, 1)
        self.ui.horizontalLayout_hum_box.setStretch(0, 2)
        self.ui.horizontalLayout_hum_box.setStretch(1, 1)
        self.ui.horizontalLayout_pres_box.setStretch(0, 2)
        self.ui.horizontalLayout_pres_box.setStretch(1, 1)

        self.hilo = None
        self.receptor = None
        self.pausado = False

        self.graficos = {
            "temp": LivePlot("Temperatura (°C)"),
            "hum": LivePlot("Humedad (%HR)"),
            "press": LivePlot("Presión (hPa)")
        }

        self.ui.plot_layout_temp.addWidget(self.graficos["temp"])
        self.ui.plot_layout_hum.addWidget(self.graficos["hum"])
        self.ui.plot_layout_pres.addWidget(self.graficos["press"])

        self.llenar_puertos()
        self.conectar_senales()
        self.ui.action_desconectar.setEnabled(False)

    def conectar_senales(self):
        self.ui.action_conectar.triggered.connect(self.conectar)
        self.ui.action_desconectar.triggered.connect(self.desconectar)
        self.ui.pausa_graficos.setCheckable(True)
        self.ui.pausa_graficos.clicked.connect(self.alternar_pausa)

        self.ui.chk_temp.stateChanged.connect(self.validar_y_enviar_variables)
        self.ui.chk_hum.stateChanged.connect(self.validar_y_enviar_variables)
        self.ui.chk_pres.stateChanged.connect(self.validar_y_enviar_variables)

        self.ui.combo_frecuencia.currentIndexChanged.connect(self.enviar_frecuencia)

        self.ui.chk_media_movil.stateChanged.connect(self.enviar_config_promedio)
        self.ui.combo_ventana_w.currentIndexChanged.connect(self.enviar_config_promedio)

        self.ui.chk_picos.stateChanged.connect(self.enviar_config_picos)
        self.ui.spin_umbral_pico.valueChanged.connect(self.enviar_config_picos)

    def llenar_puertos(self):
        """Busca y añade los puertos COM/USB disponibles."""
        self.ui.puerto_combo.clear()
        usb = [p.device for p in list_ports.comports()
               if "USB" in p.device or "ACM" in p.device or "COM" in p.device]
        for puerto in sorted(usb):
            self.ui.puerto_combo.addItem(puerto)

    def conectado(self) -> bool:
        return self.receptor is not None and self.hilo is not None and self.hilo.isRunning()

    def conectar(self):
        if self.conectado():
            return
        if not self.ui.puerto_combo.currentText():
            self.llenar_puertos()
        puerto = self.ui.puerto_combo.currentText()
        if not puerto:
            self.ui.statusbar.showMessage("No hay puertos USB/ACM disponibles")
            return

        baudios = int(self.ui.baudios_combo.currentText()) if hasattr(self.ui, 'baudios_combo') else 115200
        self.receptor = ReceptorSerial(puerto, baudios)
        self.hilo = QThread()
        self.receptor.moveToThread(self.hilo)

        self.hilo.started.connect(self.receptor.arrancar)
        self.hilo.finished.connect(self.receptor.cerrar)

        self.receptor.lecturas.connect(self.recibir_telemetria)
        self.receptor.picos.connect(self.recibir_pico)
        self.receptor.promedio.connect(self.recibir_promedio)
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

    def cambio_conexion(self, ok: bool, motivo: str):
        if ok:
            self.ui.action_conectar.setEnabled(False)
            self.ui.action_desconectar.setEnabled(True)

            self.sincronizar_configuracion_inicial()
            for grafico in self.graficos.values():
                grafico.limpiar()
            self.ui.statusbar.showMessage(f"Conectado a {self.ui.puerto_combo.currentText()}")

        else:
            self.desconectar()
            self.ui.statusbar.showMessage(f"Fallo de conexión: {motivo}")


    def validar_y_enviar_variables(self):
        """Impide dejar 0 variables seleccionadas y notifica al ESP32."""
        t_active = self.ui.chk_temp.isChecked()
        h_active = self.ui.chk_hum.isChecked()
        p_active = self.ui.chk_pres.isChecked()

        if not (t_active or h_active or p_active):
            QMessageBox.warning(self, "Selección inválida", "Debe transmitir al menos una variable (T, H o P).")
            sender = self.sender()
            sender.blockSignals(True)
            sender.setChecked(True)
            sender.blockSignals(False)
            return

        # Ajustar estado visual del gráfico según si se recibe o no
        self.graficos["temp"].setEnabled(t_active)
        self.graficos["hum"].setEnabled(h_active)
        self.graficos["press"].setEnabled(p_active)

        if not t_active: self.ui.lbl_val_temp.setText("-- °C")
        if not h_active: self.ui.lbl_val_hum.setText("-- %HR")
        if not p_active: self.ui.lbl_val_pres.setText("-- hPa")

        if self.conectado():
            cmd = protocol.comando_metricas(temp=t_active, hum=h_active, press=p_active)
            self.receptor.mandar(cmd)

    def enviar_frecuencia(self):
        if self.conectado():
            freq = float(self.ui.combo_frecuencia.currentText())
            self.receptor.mandar(protocol.comando_frecuencia(freq))

    def enviar_config_promedio(self):
        if self.conectado():
            enabled = self.ui.chk_media_movil.isChecked()
            w_sec = int(self.ui.combo_ventana_w.currentText())
            self.receptor.mandar(protocol.comando_promedio(enabled, w_sec))

    def enviar_config_picos(self):
        if self.conectado():
            enabled = self.ui.chk_picos.isChecked()
            umbral = float(self.ui.spin_umbral_pico.value())
            self.receptor.mandar(protocol.comando_picos(enabled, umbral))


    @pyqtSlot(dict)
    def recibir_telemetria(self, datos: dict):
        """Recibe un diccionario con las lecturas transmitidas por el ESP32."""
        if "temp" in datos:
            self.graficos["temp"].agregar(datos["temp"])
            self.ui.lbl_val_temp.setText(f"{datos['temp']:.2f} °C")
        if "hum" in datos:
            self.graficos["hum"].agregar(datos["hum"])
            self.ui.lbl_val_hum.setText(f"{datos['hum']:.1f} %HR")
        if "press" in datos:
            self.graficos["press"].agregar(datos["press"])
            self.ui.lbl_val_pres.setText(f"{datos['press']:.1f} hPa")

        # Promedio calculado internamente en el ESP32 (temp_avg_c)
        if "temp_avg_c" in datos:
            avg_val = datos["temp_avg_c"]
            is_complete = datos.get("avg_complete", True)
            status_str = f"{avg_val:.2f} °C" if is_complete else f"{avg_val:.2f} °C (Incompleto)"
            self.ui.lbl_val_temp_avg.setText(status_str)

    @pyqtSlot(float, float, int)
    def recibir_pico(self, valor_pico: float, umbral: float, timestamp_ms: int):
        """Registra en el historial los eventos de picos locales superados en el ESP32."""
        mensaje = f"[{timestamp_ms/1000} s] ¡PICO DETECTADO! Valor: {valor_pico:.2f} °C (Umbral: {umbral:.1f} °C)"
        self.ui.lista_eventos_picos.addItem(mensaje)
        self.ui.lista_eventos_picos.scrollToBottom()

    @pyqtSlot(float, bool)
    def recibir_promedio(self, temp_avg_c: float, es_completo: bool):
        """Recibe el promedio calculado en el ESP32 y actualiza la UI."""
        if self.pausado: return
        status_str = f"{temp_avg_c:.2f} °C" if es_completo else f"{temp_avg_c:.2f} °C (Incompleto)"
        self.ui.lbl_val_temp_avg.setText(status_str)

    @pyqtSlot(str)
    def mostrar_error_sensor(self, error_msg: str):
        """Indica fallos de inicialización o lectura del BME688."""
        self.ui.statusbar.showMessage(f"Error BME688: {error_msg}")
        self.ui.lbl_val_temp.setText("N/A (Error)")

    def mostrar_respuesta(self, cuerpo: str):
        self.ui.statusbar.showMessage(cuerpo, 4000)

    def alternar_pausa(self):
        self.pausado = self.ui.pausa_graficos.isChecked()
        if self.receptor is not None:
            self.receptor.pausado = self.pausado
        self.ui.statusbar.showMessage("Lecturas pausadas" if self.pausado else "Lecturas activas")

    def sincronizar_configuracion_inicial(self):
        """Envía el estado actual de los controles de la GUI apenas se conecta."""
        self.validar_y_enviar_variables()
        self.enviar_frecuencia()
        self.enviar_config_promedio()
        self.enviar_config_picos()

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