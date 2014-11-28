import os
import gtk
import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop

dbusMainName = 'com.bammeson.cluster'
dbusMainPath = '/com/bammeson/cluster'
dbusName = 'com.bammeson.clusterhandler'
dbusPath = '/com/bammeson/clusterhandler'


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
bus = dbus.SessionBus()
obj = bus.get_object(dbusMainName, dbusMainPath)
iface = dbus.Interface(obj, dbusMainName)
iface.updateHandlerPID(os.getpid())
myservice = ClusterService()
gtk.main()
