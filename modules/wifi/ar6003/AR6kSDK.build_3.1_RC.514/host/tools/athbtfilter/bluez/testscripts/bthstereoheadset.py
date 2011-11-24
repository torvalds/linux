#!/usr/bin/python

import dbus
import os
import sys

def printusage():
	print 'bthstereoheadset.py <options>'
	print '    create - create a stereo headset'
	print '    start  - connect sink'
	print '    stop   - disconnect sink'
	return
	
headsetAddress = os.getenv("BTSTEREO_HEADSET")

print 'BT Stereo Headset Is :  => %s' % headsetAddress

bus = dbus.SystemBus()
manager = dbus.Interface(bus.get_object('org.bluez', '/org/bluez'), 'org.bluez.Manager')
bus_id = manager.ActivateService('audio')
audio = dbus.Interface(bus.get_object(bus_id, '/org/bluez/audio'), 'org.bluez.audio.Manager')
   
if len(sys.argv) == 1 :
	printusage()
elif len(sys.argv) > 1 and sys.argv[1] == 'create'  :
	path = audio.CreateDevice(headsetAddress)
	audio.ChangeDefaultDevice(path) 
	sink = dbus.Interface (bus.get_object(bus_id, path), 'org.bluez.audio.Sink')
	sink.Connect()
	print 'Stereo Headset Connect Done'
elif len(sys.argv) > 1 and sys.argv[1] == 'start'  :
	path = audio.DefaultDevice()
	sink = dbus.Interface (bus.get_object(bus_id, path), 'org.bluez.audio.Sink') 
	if not sink.IsConnected() :
		sink.Connect()				
	print 'Audio connected'
elif len(sys.argv) > 1 and sys.argv[1] == 'stop'  :
	path = audio.DefaultDevice()
	sink = dbus.Interface (bus.get_object(bus_id, path), 'org.bluez.audio.Sink') 
	try : 
		sink.Disconnect()
	except dbus.exceptions.DBusException:
		print 'Disconnect Failed'		
	print 'Stereo headset disconnect complete'
elif len(sys.argv) > 1 and sys.argv[1] == 'delete'  :
	path = audio.DefaultDevice()
	print 'Deleting: %s ' % path
	audio.RemoveDevice(path)

