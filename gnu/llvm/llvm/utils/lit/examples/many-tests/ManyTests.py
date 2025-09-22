from lit import Test, TestFormat


class ManyTests(TestFormat):
    def __init__(self, N=10000):
        self.N = N

    def getTestsInDirectory(self, testSuite, path_in_suite, litConfig, localConfig):
        for i in range(self.N):
            test_name = "test-%04d" % (i,)
            yield Test.Test(testSuite, path_in_suite + (test_name,), localConfig)

    def execute(self, test, litConfig):
        # Do a "non-trivial" amount of Python work.
        sum = 0
        for i in range(10000):
            sum += i
        return Test.PASS, ""
