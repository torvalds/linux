import sys
from collections import OrderedDict

class Trie:
    def __init__(self, name):
        self.name = name
        self.children = OrderedDict()
        self.count = 1

    def add(self, name):
        if name in self.children:
            self.children[name].count += 1
        else:
            self.children[name] = Trie(name)
        return self.children[name]

    def print(self, depth):
        if depth > 0:
            print('|', end="")
        for i in range(depth):
            print('-', end="")
        if depth > 0:
            print(end=" ")
        print(self.name, '#', self.count)
        for key, child in self.children.items():
            child.print(depth + 1)


Root = Trie("Root")

if __name__ == "__main__":
    for line in sys.stdin:
        words = line.split('==>')
        words = [word.strip() for word in words]
        MyTrie = Root;
        for word in words:
            MyTrie = MyTrie.add(word)

    Root.print(0)
