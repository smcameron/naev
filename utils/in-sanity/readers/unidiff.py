# -*- coding: utf-8 -*-
# vim:set shiftwidth=4 tabstop=4 expandtab textwidth=80:

import os,sys
from readers import readers

class unidiff(readers):
    used = list()
    unknown = list()

    def __init__(self, **config):
        uXml = os.path.join(config['datpath'], 'unidiff.xml')
        readers.__init__(self, uXml, config['verbose'])
        self._componentName = 'unidiff'

        self.nameList = list()
        print('Compiling unidiff ...',end='      ')
        for diff in self.xmlData.findall('unidiff'):
            self.nameList.append(diff.attrib['name'])
        print("DONE")

    def find(self, name):
        """
        return True if name is found in unidiff.xml
        And if so, add name in the used list.
        """
        if name in self.nameList:
            if name not in self.used:
                self.used.append(name)
            return True
        else:
            return False

