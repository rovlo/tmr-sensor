import numpy as np
from matplotlib.lines import Line2D
import matplotlib.pyplot as plt
import matplotlib.animation as animation
import serial
from struct import unpack

device = serial.Serial('COM5', baudrate = 9600, bytesize = 8)

class Scope(object):
    def __init__(self, ax, maxt=5, dt=0.02):
        self.ax = ax
        self.dt = dt
        self.maxt = maxt
        self.tdata = [0]
        self.ydata = [0]
        self.line = Line2D(self.tdata, self.ydata)
        self.ax.add_line(self.line)
        self.ax.set_ylim(0, 3.6)
        self.ax.set_xlim(0, self.maxt)

    def update(self, y):
        lastt = self.tdata[-1]
        if lastt > self.tdata[0] + self.maxt:  # reset the arrays
            self.tdata = [self.tdata[-1]]
            self.ydata = [self.ydata[-1]]
            self.ax.set_xlim(self.tdata[0], self.tdata[0] + self.maxt)
            self.ax.figure.canvas.draw()

        t = self.tdata[-1] + self.dt
        self.tdata.append(t)
        self.ydata.append(y)
        self.line.set_data(self.tdata, self.ydata)
        return self.line,


def drawer():
    while True:
        readValue = 0
        lowByte = unpack('<H', device.read(2))[0]
        highByte = unpack('<H', device.read(2))[0]
        readValue = highByte << 8
        readValue += lowByte
        yield readValue/1024*3.58;

fig, ax = plt.subplots()
scope = Scope(ax)

ani = animation.FuncAnimation(fig, scope.update, drawer, interval=10,
                              blit=True)


plt.show()
