# Display a process of packets and processed time.
# It helps us to investigate networking or network device.
#
# options
# tx: show only tx chart
# rx: show only rx chart
# dev=: show only thing related to specified device
# debug: work with debug mode. It shows buffer status.

import os
import sys

sys.path.append(os.environ['PERF_EXEC_PATH'] + \
	'/scripts/python/Perf-Trace-Util/lib/Perf/Trace')

from perf_trace_context import *
from Core import *
from Util import *

all_event_list = []; # insert all tracepoint event related with this script
irq_dic = {}; # key is cpu and value is a list which stacks irqs
              # which raise NET_RX softirq
net_rx_dic = {}; # key is cpu and value include time of NET_RX softirq-entry
		 # and a list which stacks receive
receive_hunk_list = []; # a list which include a sequence of receive events
rx_skb_list = []; # received packet list for matching
		       # skb_copy_datagram_iovec

buffer_budget = 65536; # the budget of rx_skb_list, tx_queue_list and
		       # tx_xmit_list
of_count_rx_skb_list = 0; # overflow count

tx_queue_list = []; # list of packets which pass through dev_queue_xmit
of_count_tx_queue_list = 0; # overflow count

tx_xmit_list = [];  # list of packets which pass through dev_hard_start_xmit
of_count_tx_xmit_list = 0; # overflow count

tx_free_list = [];  # list of packets which is freed

# options
show_tx = 0;
show_rx = 0;
dev = 0; # store a name of device specified by option "dev="
debug = 0;

# indices of event_info tuple
EINFO_IDX_NAME=   0
EINFO_IDX_CONTEXT=1
EINFO_IDX_CPU=    2
EINFO_IDX_TIME=   3
EINFO_IDX_PID=    4
EINFO_IDX_COMM=   5

# Calculate a time interval(msec) from src(nsec) to dst(nsec)
def diff_msec(src, dst):
	return (dst - src) / 1000000.0

# Display a process of transmitting a packet
def print_transmit(hunk):
	if dev != 0 and hunk['dev'].find(dev) < 0:
		return
	print "%7s %5d %6d.%06dsec %12.3fmsec      %12.3fmsec" % \
		(hunk['dev'], hunk['len'],
		nsecs_secs(hunk['queue_t']),
		nsecs_nsecs(hunk['queue_t'])/1000,
		diff_msec(hunk['queue_t'], hunk['xmit_t']),
		diff_msec(hunk['xmit_t'], hunk['free_t']))

# Format for displaying rx packet processing
PF_IRQ_ENTRY= "  irq_entry(+%.3fmsec irq=%d:%s)"
PF_SOFT_ENTRY="  softirq_entry(+%.3fmsec)"
PF_NAPI_POLL= "  napi_poll_exit(+%.3fmsec %s)"
PF_JOINT=     "         |"
PF_WJOINT=    "         |            |"
PF_NET_RECV=  "         |---netif_receive_skb(+%.3fmsec skb=%x len=%d)"
PF_NET_RX=    "         |---netif_rx(+%.3fmsec skb=%x)"
PF_CPY_DGRAM= "         |      skb_copy_datagram_iovec(+%.3fmsec %d:%s)"
PF_KFREE_SKB= "         |      kfree_skb(+%.3fmsec location=%x)"
PF_CONS_SKB=  "         |      consume_skb(+%.3fmsec)"

# Display a process of received packets and interrputs associated with
# a NET_RX softirq
def print_receive(hunk):
	show_hunk = 0
	irq_list = hunk['irq_list']
	cpu = irq_list[0]['cpu']
	base_t = irq_list[0]['irq_ent_t']
	# check if this hunk should be showed
	if dev != 0:
		for i in range(len(irq_list)):
			if irq_list[i]['name'].find(dev) >= 0:
				show_hunk = 1
				break
	else:
		show_hunk = 1
	if show_hunk == 0:
		return

	print "%d.%06dsec cpu=%d" % \
		(nsecs_secs(base_t), nsecs_nsecs(base_t)/1000, cpu)
	for i in range(len(irq_list)):
		print PF_IRQ_ENTRY % \
			(diff_msec(base_t, irq_list[i]['irq_ent_t']),
			irq_list[i]['irq'], irq_list[i]['name'])
		print PF_JOINT
		irq_event_list = irq_list[i]['event_list']
		for j in range(len(irq_event_list)):
			irq_event = irq_event_list[j]
			if irq_event['event'] == 'netif_rx':
				print PF_NET_RX % \
					(diff_msec(base_t, irq_event['time']),
					irq_event['skbaddr'])
				print PF_JOINT
	print PF_SOFT_ENTRY % \
		diff_msec(base_t, hunk['sirq_ent_t'])
	print PF_JOINT
	event_list = hunk['event_list']
	for i in range(len(event_list)):
		event = event_list[i]
		if event['event_name'] == 'napi_poll':
			print PF_NAPI_POLL % \
			    (diff_msec(base_t, event['event_t']), event['dev'])
			if i == len(event_list) - 1:
				print ""
			else:
				print PF_JOINT
		else:
			print PF_NET_RECV % \
			    (diff_msec(base_t, event['event_t']), event['skbaddr'],
				event['len'])
			if 'comm' in event.keys():
				print PF_WJOINT
				print PF_CPY_DGRAM % \
					(diff_msec(base_t, event['comm_t']),
					event['pid'], event['comm'])
			elif 'handle' in event.keys():
				print PF_WJOINT
				if event['handle'] == "kfree_skb":
					print PF_KFREE_SKB % \
						(diff_msec(base_t,
						event['comm_t']),
						event['location'])
				elif event['handle'] == "consume_skb":
					print PF_CONS_SKB % \
						diff_msec(base_t,
							event['comm_t'])
			print PF_JOINT

def trace_begin():
	global show_tx
	global show_rx
	global dev
	global debug

	for i in range(len(sys.argv)):
		if i == 0:
			continue
		arg = sys.argv[i]
		if arg == 'tx':
			show_tx = 1
		elif arg =='rx':
			show_rx = 1
		elif arg.find('dev=',0, 4) >= 0:
			dev = arg[4:]
		elif arg == 'debug':
			debug = 1
	if show_tx == 0  and show_rx == 0:
		show_tx = 1
		show_rx = 1

def trace_end():
	# order all events in time
	all_event_list.sort(lambda a,b :cmp(a[EINFO_IDX_TIME],
					    b[EINFO_IDX_TIME]))
	# process all events
	for i in range(len(all_event_list)):
		event_info = all_event_list[i]
		name = event_info[EINFO_IDX_NAME]
		if name == 'irq__softirq_exit':
			handle_irq_softirq_exit(event_info)
		elif name == 'irq__softirq_entry':
			handle_irq_softirq_entry(event_info)
		elif name == 'irq__softirq_raise':
			handle_irq_softirq_raise(event_info)
		elif name == 'irq__irq_handler_entry':
			handle_irq_handler_entry(event_info)
		elif name == 'irq__irq_handler_exit':
			handle_irq_handler_exit(event_info)
		elif name == 'napi__napi_poll':
			handle_napi_poll(event_info)
		elif name == 'net__netif_receive_skb':
			handle_netif_receive_skb(event_info)
		elif name == 'net__netif_rx':
			handle_netif_rx(event_info)
		elif name == 'skb__skb_copy_datagram_iovec':
			handle_skb_copy_datagram_iovec(event_info)
		elif name == 'net__net_dev_queue':
			handle_net_dev_queue(event_info)
		elif name == 'net__net_dev_xmit':
			handle_net_dev_xmit(event_info)
		elif name == 'skb__kfree_skb':
			handle_kfree_skb(event_info)
		elif name == 'skb__consume_skb':
			handle_consume_skb(event_info)
	# display receive hunks
	if show_rx:
		for i in range(len(receive_hunk_list)):
			print_receive(receive_hunk_list[i])
	# display transmit hunks
	if show_tx:
		print "   dev    len      Qdisc        " \
			"       netdevice             free"
		for i in range(len(tx_free_list)):
			print_transmit(tx_free_list[i])
	if debug:
		print "debug buffer status"
		print "----------------------------"
		print "xmit Qdisc:remain:%d overflow:%d" % \
			(len(tx_queue_list), of_count_tx_queue_list)
		print "xmit netdevice:remain:%d overflow:%d" % \
			(len(tx_xmit_list), of_count_tx_xmit_list)
		print "receive:remain:%d overflow:%d" % \
			(len(rx_skb_list), of_count_rx_skb_list)

# called from perf, when it finds a correspoinding event
def irq__softirq_entry(name, context, cpu, sec, nsec, pid, comm, callchain, vec):
	if symbol_str("irq__softirq_entry", "vec", vec) != "NET_RX":
		return
	event_info = (name, context, cpu, nsecs(sec, nsec), pid, comm, vec)
	all_event_list.append(event_info)

def irq__softirq_exit(name, context, cpu, sec, nsec, pid, comm, callchain, vec):
	if symbol_str("irq__softirq_entry", "vec", vec) != "NET_RX":
		return
	event_info = (name, context, cpu, nsecs(sec, nsec), pid, comm, vec)
	all_event_list.append(event_info)

def irq__softirq_raise(name, context, cpu, sec, nsec, pid, comm, callchain, vec):
	if symbol_str("irq__softirq_entry", "vec", vec) != "NET_RX":
		return
	event_info = (name, context, cpu, nsecs(sec, nsec), pid, comm, vec)
	all_event_list.append(event_info)

def irq__irq_handler_entry(name, context, cpu, sec, nsec, pid, comm,
			callchain, irq, irq_name):
	event_info = (name, context, cpu, nsecs(sec, nsec), pid, comm,
			irq, irq_name)
	all_event_list.append(event_info)

def irq__irq_handler_exit(name, context, cpu, sec, nsec, pid, comm, callchain, irq, ret):
	event_info = (name, context, cpu, nsecs(sec, nsec), pid, comm, irq, ret)
	all_event_list.append(event_info)

def napi__napi_poll(name, context, cpu, sec, nsec, pid, comm, callchain, napi, dev_name):
	event_info = (name, context, cpu, nsecs(sec, nsec), pid, comm,
			napi, dev_name)
	all_event_list.append(event_info)

def net__netif_receive_skb(name, context, cpu, sec, nsec, pid, comm, callchain, skbaddr,
			skblen, dev_name):
	event_info = (name, context, cpu, nsecs(sec, nsec), pid, comm,
			skbaddr, skblen, dev_name)
	all_event_list.append(event_info)

def net__netif_rx(name, context, cpu, sec, nsec, pid, comm, callchain, skbaddr,
			skblen, dev_name):
	event_info = (name, context, cpu, nsecs(sec, nsec), pid, comm,
			skbaddr, skblen, dev_name)
	all_event_list.append(event_info)

def net__net_dev_queue(name, context, cpu, sec, nsec, pid, comm, callchain,
			skbaddr, skblen, dev_name):
	event_info = (name, context, cpu, nsecs(sec, nsec), pid, comm,
			skbaddr, skblen, dev_name)
	all_event_list.append(event_info)

def net__net_dev_xmit(name, context, cpu, sec, nsec, pid, comm, callchain,
			skbaddr, skblen, rc, dev_name):
	event_info = (name, context, cpu, nsecs(sec, nsec), pid, comm,
			skbaddr, skblen, rc ,dev_name)
	all_event_list.append(event_info)

def skb__kfree_skb(name, context, cpu, sec, nsec, pid, comm, callchain,
			skbaddr, protocol, location):
	event_info = (name, context, cpu, nsecs(sec, nsec), pid, comm,
			skbaddr, protocol, location)
	all_event_list.append(event_info)

def skb__consume_skb(name, context, cpu, sec, nsec, pid, comm, callchain, skbaddr):
	event_info = (name, context, cpu, nsecs(sec, nsec), pid, comm,
			skbaddr)
	all_event_list.append(event_info)

def skb__skb_copy_datagram_iovec(name, context, cpu, sec, nsec, pid, comm, callchain,
	skbaddr, skblen):
	event_info = (name, context, cpu, nsecs(sec, nsec), pid, comm,
			skbaddr, skblen)
	all_event_list.append(event_info)

def handle_irq_handler_entry(event_info):
	(name, context, cpu, time, pid, comm, irq, irq_name) = event_info
	if cpu not in irq_dic.keys():
		irq_dic[cpu] = []
	irq_record = {'irq':irq, 'name':irq_name, 'cpu':cpu, 'irq_ent_t':time}
	irq_dic[cpu].append(irq_record)

def handle_irq_handler_exit(event_info):
	(name, context, cpu, time, pid, comm, irq, ret) = event_info
	if cpu not in irq_dic.keys():
		return
	irq_record = irq_dic[cpu].pop()
	if irq != irq_record['irq']:
		return
	irq_record.update({'irq_ext_t':time})
	# if an irq doesn't include NET_RX softirq, drop.
	if 'event_list' in irq_record.keys():
		irq_dic[cpu].append(irq_record)

def handle_irq_softirq_raise(event_info):
	(name, context, cpu, time, pid, comm, vec) = event_info
	if cpu not in irq_dic.keys() \
	or len(irq_dic[cpu]) == 0:
		return
	irq_record = irq_dic[cpu].pop()
	if 'event_list' in irq_record.keys():
		irq_event_list = irq_record['event_list']
	else:
		irq_event_list = []
	irq_event_list.append({'time':time, 'event':'sirq_raise'})
	irq_record.update({'event_list':irq_event_list})
	irq_dic[cpu].append(irq_record)

def handle_irq_softirq_entry(event_info):
	(name, context, cpu, time, pid, comm, vec) = event_info
	net_rx_dic[cpu] = {'sirq_ent_t':time, 'event_list':[]}

def handle_irq_softirq_exit(event_info):
	(name, context, cpu, time, pid, comm, vec) = event_info
	irq_list = []
	event_list = 0
	if cpu in irq_dic.keys():
		irq_list = irq_dic[cpu]
		del irq_dic[cpu]
	if cpu in net_rx_dic.keys():
		sirq_ent_t = net_rx_dic[cpu]['sirq_ent_t']
		event_list = net_rx_dic[cpu]['event_list']
		del net_rx_dic[cpu]
	if irq_list == [] or event_list == 0:
		return
	rec_data = {'sirq_ent_t':sirq_ent_t, 'sirq_ext_t':time,
		    'irq_list':irq_list, 'event_list':event_list}
	# merge information realted to a NET_RX softirq
	receive_hunk_list.append(rec_data)

def handle_napi_poll(event_info):
	(name, context, cpu, time, pid, comm, napi, dev_name) = event_info
	if cpu in net_rx_dic.keys():
		event_list = net_rx_dic[cpu]['event_list']
		rec_data = {'event_name':'napi_poll',
				'dev':dev_name, 'event_t':time}
		event_list.append(rec_data)

def handle_netif_rx(event_info):
	(name, context, cpu, time, pid, comm,
		skbaddr, skblen, dev_name) = event_info
	if cpu not in irq_dic.keys() \
	or len(irq_dic[cpu]) == 0:
		return
	irq_record = irq_dic[cpu].pop()
	if 'event_list' in irq_record.keys():
		irq_event_list = irq_record['event_list']
	else:
		irq_event_list = []
	irq_event_list.append({'time':time, 'event':'netif_rx',
		'skbaddr':skbaddr, 'skblen':skblen, 'dev_name':dev_name})
	irq_record.update({'event_list':irq_event_list})
	irq_dic[cpu].append(irq_record)

def handle_netif_receive_skb(event_info):
	global of_count_rx_skb_list

	(name, context, cpu, time, pid, comm,
		skbaddr, skblen, dev_name) = event_info
	if cpu in net_rx_dic.keys():
		rec_data = {'event_name':'netif_receive_skb',
			    'event_t':time, 'skbaddr':skbaddr, 'len':skblen}
		event_list = net_rx_dic[cpu]['event_list']
		event_list.append(rec_data)
		rx_skb_list.insert(0, rec_data)
		if len(rx_skb_list) > buffer_budget:
			rx_skb_list.pop()
			of_count_rx_skb_list += 1

def handle_net_dev_queue(event_info):
	global of_count_tx_queue_list

	(name, context, cpu, time, pid, comm,
		skbaddr, skblen, dev_name) = event_info
	skb = {'dev':dev_name, 'skbaddr':skbaddr, 'len':skblen, 'queue_t':time}
	tx_queue_list.insert(0, skb)
	if len(tx_queue_list) > buffer_budget:
		tx_queue_list.pop()
		of_count_tx_queue_list += 1

def handle_net_dev_xmit(event_info):
	global of_count_tx_xmit_list

	(name, context, cpu, time, pid, comm,
		skbaddr, skblen, rc, dev_name) = event_info
	if rc == 0: # NETDEV_TX_OK
		for i in range(len(tx_queue_list)):
			skb = tx_queue_list[i]
			if skb['skbaddr'] == skbaddr:
				skb['xmit_t'] = time
				tx_xmit_list.insert(0, skb)
				del tx_queue_list[i]
				if len(tx_xmit_list) > buffer_budget:
					tx_xmit_list.pop()
					of_count_tx_xmit_list += 1
				return

def handle_kfree_skb(event_info):
	(name, context, cpu, time, pid, comm,
		skbaddr, protocol, location) = event_info
	for i in range(len(tx_queue_list)):
		skb = tx_queue_list[i]
		if skb['skbaddr'] == skbaddr:
			del tx_queue_list[i]
			return
	for i in range(len(tx_xmit_list)):
		skb = tx_xmit_list[i]
		if skb['skbaddr'] == skbaddr:
			skb['free_t'] = time
			tx_free_list.append(skb)
			del tx_xmit_list[i]
			return
	for i in range(len(rx_skb_list)):
		rec_data = rx_skb_list[i]
		if rec_data['skbaddr'] == skbaddr:
			rec_data.update({'handle':"kfree_skb",
					'comm':comm, 'pid':pid, 'comm_t':time})
			del rx_skb_list[i]
			return

def handle_consume_skb(event_info):
	(name, context, cpu, time, pid, comm, skbaddr) = event_info
	for i in range(len(tx_xmit_list)):
		skb = tx_xmit_list[i]
		if skb['skbaddr'] == skbaddr:
			skb['free_t'] = time
			tx_free_list.append(skb)
			del tx_xmit_list[i]
			return

def handle_skb_copy_datagram_iovec(event_info):
	(name, context, cpu, time, pid, comm, skbaddr, skblen) = event_info
	for i in range(len(rx_skb_list)):
		rec_data = rx_skb_list[i]
		if skbaddr == rec_data['skbaddr']:
			rec_data.update({'handle':"skb_copy_datagram_iovec",
					'comm':comm, 'pid':pid, 'comm_t':time})
			del rx_skb_list[i]
			return
