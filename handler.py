# myservice.py
# simple python-dbus service that exposes 1 method called hello()

import gtk
import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop

dbusName = 'com.bammeson.clusterHandler'
dbusPath = '/com/bammeson/clusterHandler'


class ClusterService(dbus.service.Object):
    def __init__(self):
        bus_name = dbus.service.BusName(dbusName, bus=dbus.SessionBus())
        dbus.service.Object.__init__(self, bus_name, dbusPath)

    @dbus.service.method(dbusName)
    def deviceOffline(self, device):
        print device + ' is offline'

    @dbus.service.method(dbusName)
    def deviceOnline(self, device):
        print device + ' is online'

DBusGMainLoop(set_as_default=True)
myservice = ClusterService()
gtk.main()
