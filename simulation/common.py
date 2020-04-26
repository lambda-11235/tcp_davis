
import numpy as np


def calcRates(times, bytesSent, interval=None):
    times = np.array(times)
    bytesSent = np.array(bytesSent)

    rates = []

    j = 0
    for i, t in enumerate(times):
        if interval is None:
            j = i - 1
        else:
            while j < i - 1 and times[j] < t - interval:
                j += 1
            
        rates.append(np.sum(bytesSent[(j+1):(i+1)]/(t - times[j])))

    return np.array(rates)
