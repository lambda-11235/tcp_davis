
import json
import numpy as np
import pandas as pd


def getTestData(inFile, pscheduler):
    with open(inFile) as data:
        pdata = json.load(data)

        if pscheduler:
            s = pdata['diags']

            # FIXME: This has no guarantee of working
            s = s[(s.find('\n\n')+2):]
            s = s[:s.find('\n\n')]

            pdata = json.loads(s)

        conns = pdata['start']['connected']

        strms = list(map(lambda i: i['streams'], pdata['intervals']))
        strms = list(zip(*strms)) #[x for y in strms for x in y] # Flatten

        return (pdata, pd.DataFrame(conns),
            list(map(lambda s: pd.DataFrame(list(s)), strms)))


class Data(object):
    """
    A class to collect and present data on tests.

    :rawTest: Raw test data.
    :startTime: Test start time.
    :stream: Data from BWCtl's streams.
    """

    def __init__(self, test, pscheduler=False):
        """
        :test: The name of the test to gather information for.
        """
        (self.rawTest, self.connections, self.streams) = getTestData(test, pscheduler)
        startTime = self.rawTest['start']['timestamp']['timesecs']
