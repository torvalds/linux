import os
import lit.formats

class CustomFormat(lit.formats.ShTest):
    def getTestsForPath(self, testSuite, path_in_suite, litConfig, localConfig):
        for sub in ['one.test', 'two.test']:
            basePath = os.path.dirname(testSuite.getExecPath(path_in_suite))
            os.makedirs(basePath, exist_ok=True)
            generatedFile = os.path.join(basePath, sub)
            with open(generatedFile, 'w') as f:
                f.write('RUN: true')
            yield lit.Test.Test(testSuite, (generatedFile, ), localConfig)
