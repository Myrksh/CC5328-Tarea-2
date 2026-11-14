"""Receptor serial: QObject movido a un QThread que sondea el puerto,
valida las líneas con protocol.validar() y las reparte como señales Qt.
"""
import queue
import time

import serial
from PyQt6.QtCore import QObject, QTimer, pyqtSignal, pyqtSlot

import protocol

class ReceptorSerial(QObject):
    lecturas = pyqtSignal(dict) # Lectura de Temp, Hum, Press
    promedio = pyqtSignal(float, bool)  # promedio de lectura en la ventana w
    picos = pyqtSignal(float, float, int)  # picos
    respuesta = pyqtSignal(str)  # cuerpo completo OK/ERROR
    conexion = pyqtSignal(bool, str)  # estado + motivo del fallo
    seq = 0

    def __init__(self, puerto, baudios):
        super().__init__()
        self.serie = None
        self.puerto = puerto
        self.pausado = False
        self.cola_tx = queue.Queue()
        self.buffer = ""
        self._baudios = baudios
        self._baudios_pendientes = None

        self._transmision_en_curso = None      # Ej: "METRICAS,6*4F\n"
        self._tag_en_transmision = None        # Ej: "METRICAS"
        self._t_envio = 0.0
        self._reintentos = 0
        self._max_reintentos = 3
        self._timeout_ack = 0.25

        self.timer = QTimer(self)
        self.timer.setInterval(20)
        self.timer.timeout.connect(self.leer)

    @pyqtSlot()
    def arrancar(self):
        try:
            self.serie = serial.Serial(self.puerto, self._baudios, timeout=0)
            self.serie.write(protocol.comando_iniciar().encode("ascii"))
        except (serial.SerialException, OSError) as motivo:
            self.conexion.emit(False, str(motivo))
            self.serie = None
            return
        self.conexion.emit(True, "")
        self.timer.start()

    @pyqtSlot()
    def leer(self):
        if self.serie is None or not self.serie.is_open:
            return
        try:
            self.chequear_baudios()
            self.escribir_pendientes()
            n = self.serie.in_waiting
            if n == 0:
                return
            self.buffer += self.serie.read(n).decode(errors="replace")
        except (serial.SerialException, OSError) as motivo:
            # puerto desaparecido o falla de I/O: avisar y quedar muerto
            # en orden (la GUI limpia el hilo al recibir conexion(False))
            self.conexion.emit(False, str(motivo))
            self.serie = None
            return
        if len(self.buffer) > 4096:  # línea corrupta sin cierre: descarte
            self.buffer = ""
            return
        lineas = self.buffer.split("\n")
        self.buffer = lineas.pop()  # resto parcial para el próximo sondeo
        for linea in lineas:
            cuerpo = protocol.validar(linea)
            if cuerpo is None:
                continue  # descarte silencioso
            self.repartir(cuerpo)

    def chequear_baudios(self):
        """Si la GUI pidió otro baudio (comando BAUDIOS ya enviado), cierra
        y reabre el puerto a la velocidad nueva."""
        if self._baudios_pendientes is None:
            return
        self._baudios = self._baudios_pendientes
        self._baudios_pendientes = None
        self.serie.close()
        self.serie = serial.Serial(self.puerto, self._baudios, timeout=0)

    def escribir_pendientes(self):
        """Envía un comando a la vez y reintenta si el ESP32 no responde con ACK."""
        ahora = time.monotonic()

        # 1. Si hay un comando esperando ACK, verificar timeout
        if self._transmision_en_curso is not None:
            if (ahora - self._t_envio) >= self._timeout_ack:
                if self._reintentos < self._max_reintentos:
                    self._reintentos += 1
                    self._t_envio = ahora
                    self.serie.write(self._transmision_en_curso.encode("ascii"))
                else:
                    # Agotó reintentos: avisar a la GUI y liberar cola
                    fallido = self._tag_en_transmision
                    self._transmision_en_curso = None
                    self._tag_en_transmision = None
                    self.respuesta.emit(fallido)
            return  # No enviar el siguiente hasta resolver el actual

        # 2. Si el canal está libre, sacar el siguiente comando de la cola
        try:
            texto = self.cola_tx.get_nowait()
        except queue.Empty:
            return

        # Extraer el nombre del comando (ej: "METRICAS,6*4F\n" -> "METRICAS")
        cuerpo = texto.split("*", 1)[0]
        self._tag_en_transmision = cuerpo.split(",", 1)[0]
        self._transmision_en_curso = texto
        self._reintentos = 0
        self._t_envio = ahora

        self.serie.write(texto.encode("ascii"))

    @pyqtSlot()
    def alternar_pausa(self):
        self.pausado = not self.pausado

    def baudios(self):
        """Baudio actual del puerto abierto."""
        return self._baudios

    def mandar(self, texto):
        """Encola un comando listo para el puerto."""
        self.cola_tx.put(texto)

    def pedir_baudios(self, baudios):
        self._baudios_pendientes = baudios

    def cerrar(self):
        if self.serie is not None and self.serie.is_open:
            self.serie.close()

    ## FUNCION QUE DESEMPAQUETA UNA TRANSMISIÓN
    def repartir(self, cuerpo):
        print(cuerpo)
        if cuerpo.startswith("OK,") or cuerpo.startswith("ERROR,"):
            self.respuesta.emit(cuerpo)

        campos = {"TEMP": None, "HUM": None, "PRES": None,
                  "TEMP_AVG_C": None, "TEMP_AVG_OK": None,
                  "PICO": None, "UMBRAL": None, "PICO_T": None}
        
        if cuerpo.startswith("DATO,"):
            if self.pausado: return

            cuerpo = cuerpo[5:] # Omitir dato

            for item in cuerpo.split(","):
                try:
                    etiqueta, valor = item.split(":", 1)
                    if etiqueta in campos: campos[etiqueta] = valor
                except: return # Se encontró bassura

            if campos["TEMP"] is not None:
                valor = campos["TEMP"]
                try:
                    self.lecturas.emit({"temp": float(valor)})
                except ValueError: return  # campo raro con checksum: descarte
            if campos["HUM"] is not None:
                valor = campos["HUM"]
                try:
                    self.lecturas.emit({"hum": float(valor)})
                except ValueError: return
            if campos["PRES"] is not None:
                valor = campos["PRES"]
                try:
                    self.lecturas.emit({"press": float(valor)})
                except ValueError: return

            if campos["TEMP_AVG_C"] is not None:
                valor = campos["TEMP_AVG_C"]
                completado = campos["TEMP_AVG_OK"]
                try:
                    self.promedio.emit(float(valor), int(completado)==1)
                except ValueError: return
            if campos["PICO"] is not None:
                valor = campos["PICO"]
                umbral = campos["UMBRAL"]
                time = campos["PICO_T"]
                try:
                    self.picos.emit(float(valor), float(umbral), int(time))
                except ValueError: return
