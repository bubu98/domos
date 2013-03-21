#! /usr/bin/env python

# Copyright (C) Bruno Garnier
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
#
# Authors:
# Bruno Garnier


# -*- coding: iso-8859-1 -*-
# vi:ts=4:et
#%retab! 
#set noexpandtab 
#set tabstop=2
# $Id: basicfirst.py,v 1.5 2005/02/11 11:09:11 mfx Exp $

import sys
import pycurl
import time 
import json
from daemon import runner
import memcache
import argparse
import socket
import syslog

CARBON_SERVER = '0.0.0.0'
CARBON_PORT = 2003
VISONIC_PASSWORD = 'password'
VISONIC_LOGIN = 'login'
VISONIC_URL_LOGIN = 'http://192.168.0.200/mobile/login/index/?JsHttpRequest=%d-xml'
VISONIC_URL_STATUS = 'http://192.168.0.200/mobile/dam/index/ind/-1?JsHttpRequest=%d-xml'
VISONIC_METRICS = 'domos.visonic.state'
VISONIC_MEMCACHE_STATUS = 'visonic_state'
MEMCACHE_URL = '127.0.0.1:11211'
REFRESH = 60

class Visonicd():
    def __init__(self):
        self.stdin_path = '/dev/null'
        self.stdout_path = '/dev/tty'
        self.stderr_path = '/dev/tty'
        self.pidfile_path =  '/tmp/visonicd.pid'
        self.pidfile_timeout = 5
        self.__c = pycurl.Curl()
        self.__contents = ''

    def run(self):
        self.auth()
        while 1:
            self.getState() 
            
    def auth(self):
        self.__contents = ''
        syslog.syslog('Visonicd started')
        c = self.__c
        c.setopt(c.URL, VISONIC_URL_LOGIN %(int(time.time())) )
        c.setopt(c.WRITEFUNCTION, self.body_callback)
        c.setopt(c.CONNECTTIMEOUT, 5)
        c.setopt(c.TIMEOUT, 8)
        c.setopt(c.FAILONERROR, True)
        c.setopt(c.HTTPHEADER, ['Accept: text/html', 'Accept-Charset: UTF-8'])
        c.setopt(c.POSTFIELDS, 'login=%s&password=%s&time=%d' %(VISONIC_LOGIN, VISONIC_PASSWORD, int(time.time())))
        c.setopt(c.COOKIEFILE, '')
        c.setopt(c.COOKIEJAR, '')
        try: 
            c.perform()
        except pycurl.error, error:
            errno, errstr = error
            syslog.syslog("Visonicd Auth Error %s", errstr)

    def getState(self):
        self.__contents = ''
        
        c = self.__c
        c.setopt(c.POST, 0) 
        c.setopt(c.URL, VISONIC_URL_STATUS %(int(time.time())))
        
        try: 
            c.perform()
        except pycurl.error, error:
            errno, errstr = error
            syslog.syslog("Visonicd Auth Error %s", errstr)

        json_object = json.loads(self.__contents)
        
        #print self.contents
        mc = memcache.Client([MEMCACHE_URL], debug = 0)
        value = str(json_object['js']['reply']['configuration']['system']['status'])
        
        mc.set(VISONIC_MEMCACHE_STATUS, value )
        self.sendMetrics(VISONIC_METRICS, self.visonic_state(value))
        
        syslog.syslog("Visonicd debug %s" %value)
        time.sleep(REFRESH)
       
    def body_callback(self, buf):
        self.__contents = self.__contents + buf

    def visonic_state(self, name):
        return {
            'Ready': 0,
            'HOME': 1,
            'AWAY': 2,
        }.get(name, 3)

    def sendMetrics(self, metric_path, value):
        timestamp = int(time.time())
        message = '%s %s %d\n' % (metric_path, value, timestamp)
        sock = socket.socket()
        sock.connect((CARBON_SERVER, CARBON_PORT))
        sock.sendall(message)
        sock.close()

visonicd = Visonicd()
daemon_runner = runner.DaemonRunner(visonicd)
daemon_runner.do_action()
