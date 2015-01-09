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
# tinymon.py
#
# GnuRadio based monitoring tool. DSP core.
#

from gnuradio import gr
import gnuradio.filter as gr_filter
import gnuradio.analog as gr_analog
import gnuradio.digital as gr_digital
import gnuradio.blocks as gr_blocks
import gnuradio.gr.gr_threading as _threading
import osmosdr

from math import pi
from binascii import hexlify

from PyQt4 import Qt
from gnuradio import qtgui
import sys,sip

from datetime import datetime
from tinyhan_mac import *

TIME_FORMAT='%Y-%m-%d %H:%M:%S.%f'

class queue_thread(_threading.Thread):
	def __init__(self, queue, callback):
		_threading.Thread.__init__(self)
		self.setDaemon(1)
		self.payload_queue = queue
		self.keep_running = True
		self.start()

	def run(self):
		sr = 0
		synced = False
		bitcount = 0
		bytecount = 0
		packet = ''

		while self.keep_running:
			msg = self.payload_queue.delete_head()
			if msg == None:
				break

			for b in msg.to_string():
				b = ord(b)
				sr = ((sr << 1) | (b & 1)) & 0xff
				if b & 2:
					bitcount = 0
					bytecount = 0
					length = 0
					synced = True
					packet = ''
				if synced:
					bitcount = bitcount + 1
					if bitcount == 8:
						packet = packet + chr(sr)
						bitcount = 0
						bytecount = bytecount + 1
						if bytecount == 1:
							length = sr + 2 + 1 # allow for CRC and length byte
					if length > 0 and bytecount == length:
						bytecount = 0
						synced = False

						# Decode and display
						try:
							msg = parse_mac(packet)
						except Exception as a:
							msg = str(a)

						# Print with timestamp
						print datetime.now().strftime(TIME_FORMAT) + ': ' + msg

class tinymon(gr.top_block):
	qtwidgets = []
	
	def __init__(self):
		gr.top_block.__init__(self)

		sdr_device = ''

		# Front end
		error_ppm = 40
		freq_c0 = 869000000

		# Modulation parameters
		sample_rate = 1200000
		bit_rate = 50000
		deviation = 25000
		max_freq_error = 50000
		decim = 2
		squelch_threshold = -20

		sync_word = "01010010110111010010" # preamble + 2dd2

		# Source
		self.src = osmosdr.source(sdr_device)
		self.src.set_sample_rate(sample_rate)
		self.src.set_center_freq(freq_c0)
		self.src.set_freq_corr(error_ppm)
		self.src.set_dc_offset_mode(0, 0)
		self.src.set_iq_balance_mode(0, 0)
		self.src.set_gain_mode(False, 0)
		self.src.set_gain(20, 0)
		self.src.set_if_gain(20, 0)
		self.src.set_bb_gain(20, 0)

		# Channel filter (bandwidth is relative to centre of channel so /2
		bandwidth = 2. * (deviation + bit_rate / 2)
		filter_taps = gr_filter.firdes.low_pass(1, sample_rate, max_freq_error + bandwidth / 2., bit_rate / 2., gr_filter.firdes.WIN_BLACKMAN, 6.76)
		self.filt = gr_filter.freq_xlating_fir_filter_ccc(decim, filter_taps, 0.0, sample_rate)
		
		# FSK demod
		m = 2. * deviation / bit_rate # Modulation index
		demod_gain = float(sample_rate) / decim / bit_rate / (pi * m)
		squelch = gr_analog.simple_squelch_cc(squelch_threshold, 1.)
		demod = gr_analog.quadrature_demod_cf(demod_gain)

		# AM demod (RSSI)
		ctof = gr_blocks.complex_to_mag()

		# Clock recovery and slicer
		gain_mu = 0.175
		gain_omega = 0.25 * gain_mu * gain_mu
		omega_rel_limit = 0.005
		clock = gr_digital.clock_recovery_mm_ff(sample_rate / decim / bit_rate,
			gain_omega, 0.5, gain_mu, omega_rel_limit)
		slicer = gr_digital.binary_slicer_fb()
		sync = gr_digital.correlate_access_code_bb(sync_word, 0)

		# Sink to queue
		self.queue = gr.msg_queue()
		self.watcher = queue_thread(self.queue, None)
		sink = gr_blocks.message_sink(gr.sizeof_char, self.queue, False)
		
		# GUI elements
		fft = qtgui.freq_sink_c(512, gr_filter.firdes.WIN_BLACKMAN, freq_c0, sample_rate/decim, "Spectrum", 1)
		fft.enable_grid(True)
		fft.set_line_label(0, 'Signal')
		qtfft = sip.wrapinstance(fft.pyqwidget(), Qt.QWidget)
		self.qtwidgets.append(qtfft)
		
		plot = qtgui.time_sink_f(int(0.1 * sample_rate / decim), sample_rate / decim, "Scope", 2)
		plot.enable_grid(True)
		plot.set_update_time(0.1)
		plot.set_y_axis(-2, 2)
		plot.set_line_label(0, 'RSSI')
		plot.set_line_label(1, 'FSK')
		plot.set_trigger_mode(qtgui.TRIG_MODE_AUTO, qtgui.TRIG_SLOPE_POS, 0.1, 0, 0, '')
		qtplot = sip.wrapinstance(plot.pyqwidget(), Qt.QWidget)
		self.qtwidgets.append(qtplot)
		
		plot2 = qtgui.time_sink_f(int(0.005 * sample_rate / decim), sample_rate / decim, "Packet View", 1)
		plot2.enable_grid(True)
		plot2.set_update_time(0.1)
		plot2.set_y_axis(-2, 2)
		plot2.set_line_label(0, 'FSK')
		plot2.set_trigger_mode(qtgui.TRIG_MODE_AUTO, qtgui.TRIG_SLOPE_POS, 0.1, 0, 0, '')
		qtplot2 = sip.wrapinstance(plot2.pyqwidget(), Qt.QWidget)
		self.qtwidgets.append(qtplot2)
		
		# Flowgraph
		self.connect(self.src, self.filt, squelch, demod, clock, slicer, sync, sink)
		self.connect(self.src, fft)
		self.connect(demod, (plot, 0))
		self.connect(self.filt, ctof, (plot, 1))
		self.connect(demod, (plot2, 0))
	
	def tune_offset(self, freq):
		self.filt.set_center_freq(freq)
		
	
	def get_qtwidgets(self):
		return self.qtwidgets

if __name__ == '__main__':
	a = tinymon()
	a.run()

