import time
import serial

ser = serial.Serial('COM11', 115200, timeout=0.2)
# esptool 的重置时序：DTR 控制 GPIO0，RTS 控制 EN
ser.dtr = False
ser.rts = True      # EN low -> reset
time.sleep(0.1)
ser.rts = False     # release reset
time.sleep(0.05)
ser.dtr = False

t0 = time.time()
buf = b''
while time.time() - t0 < 40:
    chunk = ser.read(4096)
    if chunk:
        buf += chunk
ser.close()

print(buf.decode('utf-8', 'replace'))
