#!/usr/bin/env python3

class TdcPlugin:
    def __init__(self):
        super().__init__()
        print(' -- {}.__init__'.format(self.sub_class))

    def pre_suite(self, testcount, testlist):
        '''run commands before test_runner goes into a test loop'''
        self.testcount = testcount
        self.testlist = testlist
        if self.args.verbose > 1:
            print(' -- {}.pre_suite'.format(self.sub_class))

    def post_suite(self, index):
        '''run commands after test_runner completes the test loop
        index is the last ordinal number of test that was attempted'''
        if self.args.verbose > 1:
            print(' -- {}.post_suite'.format(self.sub_class))

    def pre_case(self, caseinfo, test_skip):
        '''run commands before test_runner does one test'''
        if self.args.verbose > 1:
            print(' -- {}.pre_case'.format(self.sub_class))
        self.args.caseinfo = caseinfo
        self.args.test_skip = test_skip

    def post_case(self):
        '''run commands after test_runner does one test'''
        if self.args.verbose > 1:
            print(' -- {}.post_case'.format(self.sub_class))

    def pre_execute(self):
        '''run command before test-runner does the execute step'''
        if self.args.verbose > 1:
            print(' -- {}.pre_execute'.format(self.sub_class))

    def post_execute(self):
        '''run command after test-runner does the execute step'''
        if self.args.verbose > 1:
            print(' -- {}.post_execute'.format(self.sub_class))

    def adjust_command(self, stage, command):
        '''adjust the command'''
        if self.args.verbose > 1:
            print(' -- {}.adjust_command {}'.format(self.sub_class, stage))

        # if stage == 'pre':
        #     pass
        # elif stage == 'setup':
        #     pass
        # elif stage == 'execute':
        #     pass
        # elif stage == 'verify':
        #     pass
        # elif stage == 'teardown':
        #     pass
        # elif stage == 'post':
        #     pass
        # else:
        #     pass

        return command

    def add_args(self, parser):
        '''Get the plugin args from the command line'''
        self.argparser = parser
        return self.argparser

    def check_args(self, args, remaining):
        '''Check that the args are set correctly'''
        self.args = args
        if self.args.verbose > 1:
            print(' -- {}.check_args'.format(self.sub_class))
