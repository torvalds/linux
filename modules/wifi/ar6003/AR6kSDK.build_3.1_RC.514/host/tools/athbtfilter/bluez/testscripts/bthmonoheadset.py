#!/usr/bin/python

import dbus
import os
import sys

def printusage():
	print 'bthmonoheadset.py <options>'
	print '    create - create a mono headset'
	print '    start  - connect and play '
	print '    stop   - stop and disconnect'
	return

headsetAddress = os.getenv("BTMONO_HEADSET")

print 'BT Mono Headset Is :  => %s' % headsetAddress

bus = dbus.SystemBus()
manager = dbus.Interface(bus.get_object('org.bluez', '/org/bluez'), 'org.bluez.Manager')
bus_id = manager.ActivateService('audio')
audio = dbus.Interface(bus.get_object(bus_id, '/org/bluez/audio'), 'org.bluez.audio.Manager')
    
if len(sys.argv) == 1 :
	printusage()
elif len(sys.argv) > 1 and sys.argv[1] == 'create'  :
	path = audio.CreateHeadset(headsetAddress)
	audio.ChangeDefaultHeadset(path)
	headset = dbus.Interface (bus.get_object(bus_id, path), 'org.bluez.audio.Headset')
	headset.Connect()
	print 'Mono headset setup complete'
elif len(sys.argv) > 1 and sys.argv[1] == 'start'  :
	path = audio.DefaultDevice()
	headset = dbus.Interface (bus.get_object(bus_id, path), 'org.bluez.audio.Headset')
	if not headset.IsConnected() :
		headset.Connect()		
	if len(sys.argv) > 2 and sys.argv[2] == 'pcm'  :
		print "Turning on PCM ...."
		try : 
			headset.Play()
		except dbus.exceptions.DBusException:
			print 'Play Failed'
elif len(sys.argv) > 1 and sys.argv[1] == 'stop'  :
	path = audio.DefaultDevice()
	headset = dbus.Interface (bus.get_object(bus_id, path), 'org.bluez.audio.Headset')
	try : 
		headset.Stop()
	except dbus.exceptions.DBusException:
		print 'Stop Failed'
	headset.Disconnect()
	print 'Mono headset disconnect complete'
elif len(sys.argv) > 1 and sys.argv[1] == 'delete'  :
	path = audio.DefaultDevice()
	print 'Deleting: %s ' % path
	audio.RemoveDevice(path)

        
  


