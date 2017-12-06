"""
# SPDX-License-Identifier: GPL-2.0
tdc_helper.py - tdc helper functions

Copyright (C) 2017 Lucas Bates <lucasb@mojatatu.com>
"""

def get_categorized_testlist(alltests, ucat):
    """ Sort the master test list into categories. """
    testcases = dict()

    for category in ucat:
        testcases[category] = list(filter(lambda x: category in x['category'], alltests))

    return(testcases)


def get_unique_item(lst):
    """ For a list, return a set of the unique items in the list. """
    return list(set(lst))


def get_test_categories(alltests):
    """ Discover all unique test categories present in the test case file. """
    ucat = []
    for t in alltests:
        ucat.extend(get_unique_item(t['category']))
        ucat = get_unique_item(ucat)
    return ucat

def list_test_cases(testlist):
    """ Print IDs and names of all test cases. """
    for curcase in testlist:
        print(curcase['id'] + ': (' + ', '.join(curcase['category']) + ") " + curcase['name'])


def list_categories(testlist):
    """ Show all categories that are present in a test case file. """
    categories = set(map(lambda x: x['category'], testlist))
    print("Available categories:")
    print(", ".join(str(s) for s in categories))
    print("")


def print_list(cmdlist):
    """ Print a list of strings prepended with a tab. """
    for l in cmdlist:
        if (type(l) == list):
            print("\t" + str(l[0]))
        else:
            print("\t" + str(l))


def print_sll(items):
    print("\n".join(str(s) for s in items))


def print_test_case(tcase):
    """ Pretty-printing of a given test case. """
    for k in tcase.keys():
        if (type(tcase[k]) == list):
            print(k + ":")
            print_list(tcase[k])
        else:
            print(k + ": " + tcase[k])


def show_test_case_by_id(testlist, caseID):
    """ Find the specified test case to pretty-print. """
    if not any(d.get('id', None) == caseID for d in testlist):
        print("That ID does not exist.")
        exit(1)
    else:
        print_test_case(next((d for d in testlist if d['id'] == caseID)))


