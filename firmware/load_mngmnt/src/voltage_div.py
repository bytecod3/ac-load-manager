vin = 5
vo = 3.3
r1 = 1000
r2 = 2700

# r2 = (vo / (vin - vo)) * r1

vo = (r2 / (r1 + r2)) * vin

print(vo)