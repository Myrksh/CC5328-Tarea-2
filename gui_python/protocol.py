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
    """INICIAR*CK: defaults, fase 0, ambiente 30 s y baudio 115200."""
    return linea("INICIAR")


def comando_funcion(eje, funcion):
    """FUNCION,<eje>,<n>*CK: 1 armónica, 2 modulada AM, 3 multicomponente."""
    return linea(f"FUNCION,{eje},{funcion}")


def comando_amplitud(eje, amplitud):
    """AMPLITUD,<eje>,<g>*CK: amplitud en g, {4, 8, 16}."""
    return linea(f"AMPLITUD,{eje},{amplitud}")


def comando_frecuencia(eje, fs):
    """FRECUENCIA,<eje>,<fs>*CK: fs de muestreo en {50, 100, 200, 500, 1000}."""
    return linea(f"FRECUENCIA,{eje},{fs}")


def comando_periodo(periodo):
    """PERIODO,<s>*CK: periodo de emisión del ambiente, {30, 60}."""
    return linea(f"PERIODO,{periodo}")


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
