------------------------------------------------------------------------------
# python-obd
### Implementation of transport part of ISO 15031-5 as Python Extension Modul
------------------------------------------------------------------------------

> ## Description
This python extension module is compatible only with Python 3.

It is based on CAN_RAW socket.

The extension module makes easy to send/receive OBD requests.

> ## Setup
1. Install/compile module in python3:
     
     `python3 setup.py install`
  
2. Load can module.

3. Setup can interface.

> ## Have a fun!
```
>>> import pyobd
>>> pyobd.init("can0")
0
```
init() expects a can-interface as string.

return value:  0->ok / -1->nok

```
>>> pyobd.send(0x7DF,bytearray([0x01,0x00]))
0
```
send() expects: 1. CAN ID to be send
                2. OBD request (max. 6 bytes)
                
return value:  0->ok / -1->nok 

```
>>> pyobd.receive()
[0,0,7,232,0,6,65,1,255,255,255,255]
```
return value is a list containing OBD Response.

Structure of response message:

/first canid:4 bytes/ /data lenght:2 bytes/ /data/

/next canid:4 bytes/ /data lenght:2 bytes/ /data/

/next canid:4 bytes/ /data lenght:2 bytes/ /data/

...

/last canid:4 bytes/ /data lenght:2 bytes/ /data/ 

Example response (hex value):

| 0 0 7 e8 |     0 6     | 41 00 fe 21 01 34 |

|  can id  | data length |        data       |
                 
                 
```
>>> pyobd.close()
```


