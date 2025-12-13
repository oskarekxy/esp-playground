import serial

s = serial.Serial("/dev/ttyACM0")
print("Open port: ", s.name)
s.write(b'abcd')
data = s.read(4)

print("Got data: ", str(data, 'utf-8'))
