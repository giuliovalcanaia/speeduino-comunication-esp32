#!/usr/bin/env python3
"""
Compila, faz upload da sketch set_rtc_time e envia a hora exata do PC
para o ESP32 ajustar o RTC via Serial.

Uso:
    python3 upload_set_rtc_time.py

Requisitos:
    - arduino-cli no PATH
    - pyserial (pip install pyserial)
    - ESP32 conectado em /dev/ttyACM0
"""

import subprocess
import serial
import time
import sys

PORT = "/dev/ttyACM0"
BAUD = 115200
FQBN = "esp32:esp32:esp32"
SKETCH_DIR = "set_rtc_time"
TIMEOUT_SERIAL_S = 12  # tempo total de espera pela resposta


def run(cmd, description):
    print(f"\n>>> {description}")
    print(f"$ {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.stdout:
        print(result.stdout)
    if result.stderr:
        print(result.stderr, file=sys.stderr)
    if result.returncode != 0:
        raise RuntimeError(f"Falha em: {description}")
    return result


def build_iso_timestamp():
    t = time.localtime()
    return time.strftime("SET:%Y-%m-%dT%H:%M:%S", t)


def main():
    # 1. Compilar
    run(["arduino-cli", "compile", "--fqbn", FQBN, SKETCH_DIR], "Compilando sketch")

    # 2. Upload
    run(
        ["arduino-cli", "upload", "--fqbn", FQBN, "-p", PORT, SKETCH_DIR],
        "Fazendo upload",
    )

    # Pequena pausa para o ESP32 reiniciar
    time.sleep(1.5)

    # 3. Enviar hora exata pela Serial
    iso_cmd = build_iso_timestamp()
    print(f"\n>>> Enviando ajuste fino de horario: {iso_cmd}")

    with serial.Serial(PORT, BAUD, timeout=1) as ser:
        # Reset opcional para garantir que o ESP32 esteja no inicio do setup
        ser.dtr = False
        ser.rts = True
        time.sleep(0.1)
        ser.rts = False
        time.sleep(0.5)

        start = time.time()
        sent = False

        while time.time() - start < TIMEOUT_SERIAL_S:
            # Envia o comando uma vez, logo no inicio
            if not sent:
                ser.write((iso_cmd + "\n").encode("utf-8"))
                ser.flush()
                sent = True
                print(f"Comando enviado: {iso_cmd}")

            line = ser.readline()
            if line:
                text = line.decode("utf-8", errors="replace").rstrip()
                print(text)

                if "RTC ajustado via Serial" in text:
                    print("\n✅ RTC sincronizado com sucesso!")
                    return
                if "Timeout: mantendo horario de compilacao" in text:
                    print("\n⚠️  O ESP32 nao recebeu o ajuste a tempo.")
                    return

        print("\n⚠️  Tempo de espera esgotado sem confirmacao do ajuste.")


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"\n❌ Erro: {e}", file=sys.stderr)
        sys.exit(1)
