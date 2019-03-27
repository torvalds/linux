# $FreeBSD$

import threading
import scapy.all as sp

class Sniffer(threading.Thread):
	def __init__(self, args, check_function):
		threading.Thread.__init__(self)

		self._args = args
		self._recvif = args.recvif[0]
		self._check_function = check_function
		self.foundCorrectPacket = False

		self.start()

	def _checkPacket(self, packet):
		ret = self._check_function(self._args, packet)
		if ret:
			self.foundCorrectPacket = True
		return ret

	def run(self):
		self.packets = sp.sniff(iface=self._recvif,
				stop_filter=self._checkPacket, timeout=3)
