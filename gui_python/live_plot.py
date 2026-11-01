"""Gráfico en vivo con ventana deslizante de 5 s: canvas matplotlib que
redibuja a ~30 fps los últimos N segundos de señal.
Patrón de context/embeb-qt/main.py (repo del auxiliar).
"""
import collections
import time

from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg
from matplotlib.figure import Figure


class LivePlot(FigureCanvasQTAgg):
    def __init__(self, titulo: str, ventana_s: float = 5.0, fps: int = 30):
        super().__init__(Figure(figsize=(4, 2)))
        self.axes = self.figure.subplots()
        self.ventana_s = ventana_s
        self.tiempo = collections.deque()
        self.valores = collections.deque()
        self.axes.set_title(titulo)
        self.axes.set_ylabel("g")
        self.axes.set_xlabel("t (s)")
        self.axes.grid(True)
        (self.linea,) = self.axes.plot([], [], "b-", lw=0.8)
        self._inicio = time.monotonic()
        self.timer = self.new_timer(int(1000 // fps))
        self.timer.add_callback(self.redibujar)
        self.timer.start()

    def agregar(self, valor: float):
        """Punto nuevo; los más viejos que la ventana se van cayendo."""
        ahora = time.monotonic() - self._inicio
        self.tiempo.append(ahora)
        self.valores.append(valor)
        while self.tiempo and self.tiempo[0] < ahora - self.ventana_s - 0.5:
            self.tiempo.popleft()
            self.valores.popleft()

    def redibujar(self):
        ahora = time.monotonic() - self._inicio
        self.linea.set_data(self.tiempo, self.valores)
        self.axes.relim()
        self.axes.autoscale_view()
        self.axes.set_xlim(ahora - self.ventana_s, ahora)
        self.draw_idle()
