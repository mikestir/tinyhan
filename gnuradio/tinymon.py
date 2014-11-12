#!/usr/bin/env python

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

class demod_top_block(gr.top_block):
	def __init__(self):
		gr.top_block.__init__(self)

		sdr_device = ''

		# Front end
		offset = -30000
		freq_c0 = 869000000
		frequency = freq_c0 + offset
		gain = 60
		agc = 0

		# Modulation parameters
		sample_rate = 1200000
		bit_rate = 50000
		deviation = 25000
		m = 2. * deviation / bit_rate # Modulation index
		sync_word = "010101010010110111010010" # preamble + 2dd2

		# Channel filter
		bandwidth = 2. * (deviation + bit_rate / 2)
		skirts = bit_rate
		adj_channel_atten = 20
		decim = 4

		# Demodulator
		squelch_threshold = -10
		demod_gain = float(sample_rate) / decim / bit_rate / (pi * m)
		print demod_gain

		# Source
		src = osmosdr.source(sdr_device)
		src.set_gain_mode(agc)
		src.set_gain(gain)
		src.set_sample_rate(sample_rate)
		src.set_center_freq(frequency)

		# Channel filter
		filter_taps = gr_filter.firdes.low_pass_2(1.0, sample_rate, bandwidth, skirts, adj_channel_atten)
		low_pass = gr_filter.fir_filter_ccf(1, filter_taps)
		decimate = gr_blocks.keep_one_in_n(gr.sizeof_gr_complex, decim)
		
		# FSK demod
		squelch = gr_analog.simple_squelch_cc(squelch_threshold, 1.)
		demod = gr_analog.quadrature_demod_cf(demod_gain)

		# AM demod (RSSI)
		ctof = gr_blocks.complex_to_mag()

		# Clock recovery and slicer
		gain_mu = 0.01
		gain_omega = 0.25 * gain_mu * gain_mu
		clock = gr_digital.clock_recovery_mm_ff(sample_rate / decim / bit_rate,
			gain_omega, 0.5, gain_mu, 0.3)
		slicer = gr_digital.binary_slicer_fb()
		sync = gr_digital.correlate_access_code_bb(sync_word, 0)

		# Sink to queue
		self.queue = gr.msg_queue()
		self.watcher = queue_thread(self.queue, None)
		sink = gr_blocks.message_sink(gr.sizeof_char, self.queue, False)

		self.connect(src, low_pass, decimate, squelch, demod, clock, slicer, sync, sink)
		
		# GUI
		plot = qtgui.time_sink_f(int(0.05 * sample_rate / decim), sample_rate / decim, "Scope", 2)
		plot.enable_grid(True)
		plot.set_trigger_mode(qtgui.TRIG_MODE_AUTO, qtgui.TRIG_SLOPE_POS, 0.5, 0.0, 1)
		self.connect(decimate, ctof, (plot, 1))
		self.connect(demod, (plot, 0))
		win = sip.wrapinstance(plot.pyqwidget(), Qt.QWidget)
		win.show()

if __name__ == '__main__':
	qapp = Qt.QApplication(sys.argv)
	a = demod_top_block()
	a.start()
	qapp.exec_()
	a.stop()

