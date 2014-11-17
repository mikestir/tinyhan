#!/usr/bin/env python
#
# Copyright 2013-2014 Mike Stirling
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# This file is part of the Tiny Home Area Network stack.
#
# http://www.tinyhan.co.uk/
#
# tinymon_gui.py
#
# GnuRadio based monitoring tool. Qt GUI
#

from tinymon import tinymon
from Queue import Queue
from PyQt4 import Qt, QtGui, QtCore
import PyQt4.Qwt5 as Qwt
import sys

class LogStream(object):
	def __init__(self, queue):
		self.queue = queue
		
	def write(self, text):
		self.queue.put(text)
		
class LogReceiver(QtCore.QObject):
	rxready = QtCore.pyqtSignal(str)
	
	def __init__(self, queue, *args, **kwargs):
		QtCore.QObject.__init__(self, *args, **kwargs)
		self.queue = queue
		
	@QtCore.pyqtSlot()
	def run(self):
		while True:
			text = self.queue.get()
			self.rxready.emit(text)

class TinymonGui(QtGui.QWidget):
	def __init__(self, rx, grwidgets, parent = None):
		QtGui.QWidget.__init__(self, parent)
		self.setWindowTitle('TinyHAN Radio Monitor')
		
		self.rx = rx
		
		self.tune = Qwt.QwtSlider(None, Qt.Qt.Horizontal, Qwt.QwtSlider.BottomScale, Qwt.QwtSlider.BgSlot)
		self.tune.setRange(-200000, 200000, 100)
		self.tune.setValue(0)
		self.tune.valueChanged.connect(self.do_tune)
		
		self.output = QtGui.QPlainTextEdit()
		self.output.setReadOnly(True)
				
		llayout = QtGui.QVBoxLayout()
		llayout.addWidget(self.tune)
		llayout.addWidget(grwidgets[0])
		
		grwidgets[0].setMaximumSize(400,400)
		grwidgets[1].setMaximumHeight(250)
		grwidgets[2].setMaximumHeight(250)
		
		mainlayout = QtGui.QGridLayout()
		mainlayout.addLayout(llayout,0,0,2,1)
		mainlayout.addWidget(grwidgets[1],0,1,1,1)
		mainlayout.addWidget(grwidgets[2],1,1,1,1)
		mainlayout.addWidget(self.output,2,0,1,2)
		mainlayout.setColumnStretch(1,3)
		mainlayout.setRowStretch(2,3)
		
		self.setLayout(mainlayout)
		self.resize(800,600)
		
	def do_tune(self, freq):
		self.rx.tune_offset(freq)
		
	@QtCore.pyqtSlot(str)
	def append_string(self, text):
		self.output.moveCursor(QtGui.QTextCursor.End)
		self.output.insertPlainText(text)

if __name__=='__main__':
	# Redirect stdout
	queue = Queue()
	sys.stdout = LogStream(queue)

	gui = QtGui.QApplication(sys.argv)
	rx = tinymon()
	win = TinymonGui(rx, rx.get_qtwidgets())
	win.show()
	
	logrxthread = QtCore.QThread()
	logrx = LogReceiver(queue)
	logrx.rxready.connect(win.append_string)
	logrx.moveToThread(logrxthread)
	logrxthread.started.connect(logrx.run)
	logrxthread.start()
	
	rx.start()
	gui.exec_()
	rx.stop()
