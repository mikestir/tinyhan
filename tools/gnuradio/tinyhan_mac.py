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
# GnuRadio based monitoring tool. MAC layer protocol decoder.
#

import crcmod

from ctypes import *
from binascii import hexlify

BEACON = 0x0
BEACON_REQUEST = 0x2
ACK = 0x3
REG_REQUEST = 0x4
DEREG_REQUEST = 0x5
PING = 0x7

TINYHAN_APP = 0x10
TINYHAN_MQTTSN = 0x11
TINYHAN_EXTENDED = 0x1f

TYPE_NAMES = [
	'BEACON',
	'BEACON_REQ',
	'POLL',
	'ACK',
	'REG_REQ',
	'DEREG_REQ',
	'REG_ACK',
	'RESERVED07',
	'RESERVED08',
	'RESERVED09',
	'RESERVED0A',
	'RESERVED0B',
	'RESERVED0C',
	'RESERVED0D',
	'RESERVED0E',
	'RESERVED0F',
	'RAW_DATA',
	'TINYHAN',
	'MQTT-SN',
	'6LOWPAN',
	'RESERVED14',
	'RESERVED15',
	'RESERVED16',
	'RESERVED17',
	'RESERVED18',
	'RESERVED19',
	'RESERVED1A',
	'RESERVED1B',
	'RESERVED1C',
	'RESERVED1D',
	'SYSLOG',
	'EXTENDED' ]

HEADER_FLAGS_TYPE_MASK = 0x1f
HEADER_FLAGS_AR = 1 << 6
HEADER_FLAGS_DP = 1 << 7

class Header(Structure):
	_pack_ = 1
	_fields_ = [
		('length', c_uint8),
		('flags', c_uint16),
		('net', c_uint8),
		('dest', c_uint8),
		('src', c_uint8),
		('seq', c_uint8) ]

BEACON_FLAGS_SYNC = 1 << 0
BEACON_FLAGS_PERMIT_ATTACH = 1 << 1

class PayloadBeacon(Structure):
	_pack_ = 1
	_fields_ = [
		('uuid', c_uint64),
		('timestamp', c_uint16),
		('flags', c_uint8),
		('interval', c_uint8) ]

REG_FLAGS_SLEEPY = 1 << 4
REG_FLAGS_HEARTBEAT_MASK = 15 << 0

class PayloadRegRequest(Structure):
	_pack_ = 1
	_fields_ = [
		('uuid', c_uint64),
		('flags', c_uint16) ]

DEREG_REASON_USER = 0
DEREG_REASON_POWER = 1

class PayloadDeregRequest(Structure):
	_pack_ = 1
	_fields_ = [
		('uuid', c_uint64),
		('reason', c_uint8) ]

REG_STATUS_OK = 0
REG_STATUS_DENIED = 1
REG_STATUS_FULL = 2
REG_STATUS_SHUTDOWN = 3
REG_STATUS_ADMIN = 4
REG_STATUS_ADDRESS_INVALID = 5

class PayloadRegResponse(Structure):
	_pack_ = 1
	_fields_ = [
		('uuid', c_ulonglong),
		('addr', c_ubyte),
		('status', c_ubyte) ]

def parse_beacon(payload):
	s = PayloadBeacon.from_buffer_copy(payload)
	msg = "UUID=%016X t=%u" % (s.uuid, s.timestamp)
	if s.flags & BEACON_FLAGS_SYNC:
		msg = msg + ' (SYNC)'
	if s.flags & BEACON_FLAGS_PERMIT_ATTACH:
		msg = msg + ' (ATTACH)'
	return msg

def parse_beacon_req(payload):
	pass

def parse_poll(payload):
	pass

def parse_ack(payload):
	pass

def parse_reg_req(payload):
	s = PayloadRegRequest.from_buffer_copy(payload)
	heartbeat = 1 << (s.flags & REG_FLAGS_HEARTBEAT_MASK)
	msg = "UUID=%016X HEARTBEAT=%u secs" % (s.uuid, heartbeat)
	if s.flags & REG_FLAGS_SLEEPY:
		msg = msg + ' (SLEEPY)'
	return msg

def parse_dereg_req(payload):
	s = PayloadDeregRequest.from_buffer_copy(payload)
	msg = "UUID=%016X" % (s.uuid)
	if s.reason == DEREG_REASON_USER:
		msg = msg + ' (USER REQUEST)'
	elif s.reason == DEREG_REASON_POWER:
		msg = msg + ' (POWER DOWN)'
	else:
		msg = msg + ' (UNKNOWN REASON)'
	return msg

def parse_reg_ack(payload):
	s = PayloadRegResponse.from_buffer_copy(payload)
	msg = "UUID=%016X ADDR=%02X" % (s.uuid, s.addr)
	if s.status == REG_STATUS_OK:
		msg = msg + ' (OK)'
	elif s.status == REG_STATUS_DENIED:
		msg = msg + ' (ACCESS DENIED)'
	elif s.status == REG_STATUS_FULL:
		msg = msg + ' (NETWORK BUSY)'
	elif s.status == REG_STATUS_SHUTDOWN:
		msg = msg + ' (SHUTTING DOWN)'
	elif s.status == REG_STATUS_ADMIN:
		msg = msg + ' (ADMIN REQUEST)'
	elif s.status == REG_STATUS_ADDRESS_INVALID:
		msg = msg + ' (ADDRESS INVALID)'
	else:
		msg = msg + ' (UNKNOWN STATUS)'
	return msg

def parse_dummy(payload):
	return hexlify(payload)

def parse_syslog(payload):
	return str(payload[1:])

parserfunc = [
	parse_beacon,
	parse_beacon_req,
	parse_poll,
	parse_ack,
	parse_reg_req,
	parse_dereg_req,
	parse_reg_ack,
	parse_dummy,
	parse_dummy,
	parse_dummy,
	parse_dummy,
	parse_dummy,
	parse_dummy,
	parse_dummy,
	parse_dummy,
	parse_dummy,

	parse_dummy,
	parse_dummy,
	parse_dummy,
	parse_dummy,
	parse_dummy,
	parse_dummy,
	parse_dummy,
	parse_dummy,
	parse_dummy,
	parse_dummy,
	parse_dummy,
	parse_dummy,
	parse_dummy,
	parse_dummy,
	parse_syslog,
	parse_dummy,
	]

def parse_payload(payload, type):
	try:
		return parserfunc[type](payload)
	except:
		return hexlify(payload)

def parse_mac(packet):
	if len(packet) < sizeof(Header) + 2:
		raise Exception("Packet too short")
	payload = packet[sizeof(Header):-2]
	crc = (ord(packet[-2]) << 8) | ord(packet[-1])

	# Validate CRC
	crcfunc = crcmod.predefined.mkCrcFun('xmodem')
	crccalc = crcfunc(packet[0:-2])
	if crccalc != crc:
		raise Exception("CRC error")

	h = Header.from_buffer_copy(packet)
	if len(packet) < h.length + 3:
		raise Exception("Payload too short")

	type = h.flags & HEADER_FLAGS_TYPE_MASK
	if h.flags & HEADER_FLAGS_AR:
		ar = 'AR'
	else:
		ar = '  '
	if h.flags & HEADER_FLAGS_DP:
		dp = 'DP'
	else:
		dp = '  '

	payload_info = parse_payload(payload, type)
	return "[%02X] %02X->%02X (%02X) %s %s : %12s : %s" % (h.net, h.src, h.dest, h.seq, ar, dp, TYPE_NAMES[type], payload_info)



if __name__ == '__main__':
	print "Testing MAC decoder functions"

	packet = '\x00\x00\xab\xff\x00\x03\x88\x77\x66\x55\x44\x33\x22\x11\x10\x00\x03\x0f'
	packet = chr(len(packet)) + packet + '\xaa\xbb'

	parse_mac(packet)

