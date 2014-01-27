#!/usr/bin/python
#-*-coding=utf-8-*

import os
import sys
import urllib, urllib2
import httplib


def myhttp( query):
    url = "/mod_searcher/Search?query=" + urllib.quote(query)
    head = {
        'User-Agent'   : 'Mozilla/5.0 (Windows NT 5.1; WOW64; rv:13.0) Gecko/20100101 Firefox/13.0',
        'Host' : "www.bjguahao.gov.cn",
        'Accept' : 'text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8',
        'Accept-Language' : 'zh-cn,zh;q=0.8,en-us;q=0.5,en;q=0.3',
        'Accept-Encoding' : 'text',
        'Connection' : 'keep-alive',
        'Content-Type' : 'application/x-www-form-urlencoded',
            'Cache-Control': 'max-age=0',
        'Origin': 'http://www.bjguahao.gov.cn',
        'Accept-Charset': 'GBK,utf-8;q=0.7,*;q=0.3',
    }
    conn = ''
    r1 = ''
    data = ''
    conn = httplib.HTTPConnection("192.168.132.121","2020")
    conn.request("GET", url, "", head)
    r1 = conn.getresponse()
    data = r1.read()
    conn.close()
    r1.close()
    return data


print myhttp("nvzhuang")
