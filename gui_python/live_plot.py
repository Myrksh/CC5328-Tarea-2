"""Gráfico en vivo con ventana deslizante de N s: canvas matplotlib que
redibuja los últimos N segundos de señal.
"""
import collections
import time

from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg
from matplotlib.figure import Figure


class LivePlot(FigureCanvasQTAgg):
    def __init__(self, titulo: str, ventana_s: float = 10.0, fps: int = 60):
        super().__init__(Figure(figsize=(4, 2)))
        self.axes = self.figure.subplots()
        self.ventana_s = ventana_s
        self.tiempo = collections.deque()
        self.valores = collections.deque()
        self.axes.set_title(titulo)
        self.axes.set_ylabel("Valor")
        self.axes.set_xlabel("t (s)")
        self.axes.grid(True)
        (self.linea,) = self.axes.plot([], [], "b-", lw=1.0)
        self._inicio = None  # Arranca en None hasta conectar/recibir el primer punto
        self.timer = self.new_timer(int(1000 // fps))
        self.timer.add_callback(self.redibujar)
        self.timer.start()

    def agregar(self, valor: float):
        """Punto nuevo; los más viejos que la ventana se van cayendo."""
        if self._inicio is None:
            self._inicio = time.monotonic()  # Inicia el contador de tiempo al recibir la 1ra muestra

        ahora = time.monotonic() - self._inicio
        self.tiempo.append(ahora)
        self.valores.append(valor)
        while self.tiempo and self.tiempo[0] < ahora - self.ventana_s - 0.5:
            self.tiempo.popleft()
            self.valores.popleft()

    def redibujar(self):
        # 1. Sin datos retenidos: mantener eje X en [0, ventana_s] y gráfico limpio
        if self._inicio is None or not self.tiempo:
            self.linea.set_data([], [])
            self.axes.set_xlim(0, self.ventana_s)
            self.draw_idle()
            return

        # 2. Con datos retenidos: actualizar curva y auto-escalar Y según las mediciones reales
        ahora = time.monotonic() - self._inicio
        self.linea.set_data(list(self.tiempo), list(self.valores))
        
        # Actualizar límites de tiempo en X (ventana deslizante)
        self.axes.set_xlim(max(0, ahora - self.ventana_s), max(self.ventana_s, ahora))
        
        # Re-calcular autoscale solo para el eje Y
        self.axes.relim()
        self.axes.autoscale_view(scalex=False, scaley=True)
        
        self.draw_idle()

    def limpiar(self):
        """Limpia los datos retenidos y reinicia el tiempo de inicio a 0s."""
        self.tiempo.clear()
        self.valores.clear()
        self._inicio = None
        self.linea.set_data([], [])
        self.axes.set_xlim(0, self.ventana_s)
        self.draw_idle()