from serial.tools import list_ports

ports = list(list_ports.comports())
if not ports:
    print("no serial ports found - is the board plugged in?")
for p in ports:
    print(p.device, "|", p.description, "|", p.hwid)
