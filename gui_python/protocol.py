"""
Formato de "comando": líneas ASCII terminadas en '\n', campos separados por comas,
checksum en 2 dígitos hexa mayúsculas tras '*'.
"""

BAUDIOS_MENU = (115200, 230400, 460800, 921600)


def checksum(cuerpo):
    """XOR-8 de los bytes del cuerpo, en 2 dígitos hexa mayúsculas."""
    x = 0
    for byte in cuerpo.encode("ascii"):
        x ^= byte
    return f"{x:02X}"


def linea(cuerpo):
    """Cuerpo completo listo para el puerto: <cuerpo>*<CK>\n"""
    return f"{cuerpo}*{checksum(cuerpo)}\n"


def comando_iniciar():
    """INICIAR*CK: defaults y baudio 115200."""
    return linea("INICIAR")

def comando_metricas(temp, hum, press):
    """METRICAS,<mascara>*CK: mascara que indica temperatura, humedad y presion. {1, ..., 7}
       Ej: 6 -> 0b110 -> se mide temperatura y humedad, presion desactivada."""
    mascara = temp
    mascara = mascara << 1 | hum
    mascara = mascara << 1 | press
    return linea(f"METRICAS,{mascara}")


def comando_frecuencia(fs):
    """FRECUENCIA,<valor>*CK: fs de muestreo en dHz {5, 10, 20, 50}."""
    return linea(f"FRECUENCIA,{int(fs*10)}")

def comando_promedio(enabled, w):
    """PROMEDIO,<enabled>,<w>*CK: ventana w de tiempo que se promediara {5, 10, 30, 60}."""
    enabled = 1 if enabled else 0
    return linea(f"PROMEDIO,{enabled},{w}")

def comando_picos(enabled, umbral):
    """PICOS,<enabled>,<umbral>*CK: umbral de temperatura en la que se considerara pico (-100,100)."""
    enabled = 1 if enabled else 0
    return linea(f"PICOS,{enabled},{umbral}")


def comando_baudios(baudios):
    """BAUDIOS,<b>*CK: hot-swap del baudio del UART0."""
    return linea(f"BAUDIOS,{baudios}")


def validar(texto):
    """Devuelve el cuerpo si la línea trae checksum correcto; None si no
    (descarte silencioso)."""
    limpio = texto.rstrip("\r\n")
    cuerpo, sep, ck = limpio.partition("*")
    if not sep or len(ck) != 2:
        return None
    try:
        esperado = checksum(cuerpo)
    except UnicodeEncodeError:
        return None  # bytes basura de una decodificación con sustitución
    if esperado != ck.upper():
        return None
    return cuerpo
