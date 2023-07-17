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
write = fill("write_file.txt")
write_cache = fill("write_file_cache.txt")

fig, ax = plt.subplots()


plt.plot(sizes, write, label='Write without caching')
plt.plot(sizes, write_cache,label='Write with caching')

ax.set_xscale('log', basex=2)

plt.ylabel(r'Time in microseconds $\mu$')
plt.xlabel('Size of the write in bytes')
plt.title('Filesystem write benchmark', fontsize = 12)
plt.grid(True)

plt.savefig('filesytem_benchmark_write.jpg', dpi=500)

plt.legend()
plt.show()