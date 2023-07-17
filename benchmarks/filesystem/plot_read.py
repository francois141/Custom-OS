import numpy as np
import matplotlib.pyplot as plt
import matplotlib.ticker

def fill(path):
    data = []
    for item in open(path,'r'):
        item = item.strip()
        if item != '':
            try:
                data.append(float(item))
            except ValueError:
                pass
    return np.array(data).astype("float32")

sizes = fill("sizes.txt")
read = fill("read_file.txt")
read_cache = fill("read_file_cache.txt")

fig, ax = plt.subplots()

plt.plot(sizes, read,label='Read without caching')
plt.plot(sizes, read_cache,label='Read with caching')

ax.set_xscale('log', basex=2)

plt.ylabel(r'Time in microseconds $\mu$')
plt.xlabel('Size of the read in bytes')
plt.title('Filesystem read benchmark', fontsize = 12)
plt.grid(True)

plt.savefig('filesystem_benchmark_read.jpg', dpi=500)

plt.legend()
plt.show()