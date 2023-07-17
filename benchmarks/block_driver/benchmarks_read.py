import numpy as np
from scipy.stats import norm
import matplotlib.pyplot as plt

arch = "input_read.txt"
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
plt.hist(data, bins=15, density=True, alpha=0.6, color='b')

# Plot the PDF.
xmin, xmax = plt.xlim()
x = np.linspace(35080, 35339, 100)
p = norm.pdf(x, mu, std)

min_value=min(data)
max_value=max(data)

plt.xlim([35080, 35339])

plt.ylabel('Density of the reads (500 samples)')
plt.xlabel(r'Time in microseconds $\mu$')
plt.title(r'Blockdriver reads $\mu=%.3f,\ \sigma=%.3f\ min=%.3f\ max=%.3f$' %(mu, std, min_value, max_value), fontsize = 10)
plt.grid(True)

#plt.show()
plt.savefig('benchmark_read.jpg', dpi=500)
