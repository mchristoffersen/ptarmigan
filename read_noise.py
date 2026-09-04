import numpy as np
import scipy.signal
import struct
import sys
import matplotlib.pyplot as plt

with open(sys.argv[1], mode="rb") as fd:
    data = fd.read()

time = data[:16]
rx = data[16:]

full, frac = struct.unpack("qd", time)

print(full, frac)

nsamp = len(rx) // 2

rx = struct.unpack("h" * nsamp, rx)

# b, a = scipy.signal.butter(4, [49e6], btype="low", fs=100e6)
# rx = scipy.signal.filtfilt(b, a, rx)

plt.plot(rx[:10000])

plt.figure()
f, Pxx_den = scipy.signal.welch(rx, 100e6, nperseg=2**12)
plt.semilogy(f / 1e6, Pxx_den, "k-")
# plt.ylim([0.5e-3, 1])
plt.xlim(0, 50)
plt.xlabel("frequency (MHz)")
plt.ylabel("PSD (Counts**2/Hz)")
plt.show()
