
import json
import numpy as np
import pandas as pd


def getTestData(inFile):
    with open(inFile) as data:
        pdata = json.load(data)

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

    def __init__(self, test):
        """
        :test: The name of the test to gather information for.
        """
        testFile = test

        (self.rawTest, self.connections, self.streams) = getTestData(testFile)
        startTime = self.rawTest['start']['timestamp']['timesecs']
