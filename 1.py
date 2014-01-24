#!/usr/bin/python
#-*-coding=utf-8-*

import os
import sys



for line in sys.stdin:
    strs = line.strip()
    print "%s\t10000000" %(strs)

    print len(strs)