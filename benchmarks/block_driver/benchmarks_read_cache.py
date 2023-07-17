import numpy as np
from scipy.stats import norm
import matplotlib.pyplot as plt

arch = "input_read_cache.txt"
data = []
for item in open(arch,'r'):
    item = item.strip()
    if item != '':
        try:
            data.append(float(item))
        except ValueError:
            pass


mu, std = norm.fit(data)

# Plot the histogram.
plt.hist(data, bins=2, density=True, alpha=0.6, color='b')

# Plot the PDF.
xmin, xmax = plt.xlim()
x = np.linspace(3, 4, 200)
p = norm.pdf(x, mu, std)


min_value=min(data)
max_value=max(data)

plt.xlim([3, 4])

plt.ylabel('Density of the reads (500 samples)')
plt.xlabel(r'Time in microseconds $\mu$')
plt.title(r'Blockdriver reads (with cache hit) $\mu=%.3f,\ \sigma=%.3f\ min=%.3f\ max=%.3f$' %(mu, std, min_value, max_value), fontsize = 10)
plt.grid(True)


plt.savefig('benchmark_read_cache.jpg', dpi=500)
