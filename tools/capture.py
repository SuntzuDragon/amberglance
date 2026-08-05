import sys, time, serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM4"
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0

s = serial.Serial(port, 115200, timeout=0.2)
# RTS is wired to EN on this board: pulse it to reset so we catch the boot log.
s.setDTR(False)
s.setRTS(True)
time.sleep(0.15)
s.setRTS(False)

end = time.time() + secs
while time.time() < end:
    data = s.read(4096)
    if data:
        sys.stdout.write(data.decode("utf-8", "replace"))
        sys.stdout.flush()
s.close()
