import numpy as np
import struct
import sys
import matplotlib.pyplot as plt

with open(sys.argv[1], mode="rb") as fd:
    data = fd.read()

sig = data[:8]
ver = data[8:16]
hdr = data[16:86]
rx = data[86:]

print(sig)

vmaj, vmin = struct.unpack("II", ver)

print(vmaj, vmin)

hdrfmt = "dddddQQdcccccc"

hdr = struct.unpack(hdrfmt, hdr)

hdr = {
    "chirp_cf": hdr[0],
    "chirp_bw": hdr[1],
    "chirp_len": hdr[2],
    "chirp_amp": hdr[3],
    "chirp_prf": hdr[4],
    "stack": hdr[5],
    "spt": hdr[6],
    "fs": hdr[7]
}

print(hdr)

bpt = hdr["spt"] * 4 + 16
ntr = len(rx) // bpt
print(ntr, "traces")
rgram = np.zeros((hdr["spt"], ntr), dtype=np.complex64)
time = np.zeros(ntr, dtype=np.uint64)

for i in range(ntr):
    time[i] = struct.unpack(
        "QQ", rx[bpt * i : bpt * i + 16]
    )[0]
    trace = struct.unpack(
        "i" * hdr["spt"],
        rx[bpt * i + 16 : bpt * (i + 1)],
    )
    trace = np.array(trace)
    rgram[:, i] = trace
    # fig, axs = plt.subplots(2, 1, figsize=(6, 6))
    # axs[0].plot(np.real(rgram[:, i]), "k-")
    # axs[1].plot(np.imag(rgram[:, i]), "k-")
    # plt.show()

plt.imshow(np.real(rgram), aspect="auto")
plt.show()
