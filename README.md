# hddled_tmj33

Kernel module to control HDD LEDs on Terramaster NAS that run on Intel Celeron J33xx

I got the source code for module named led_drv_TMJ33 that was running on my F2-221's
TOS 4.1 by emailing their support email. That module gives the ability to control the
two HDD LEDs on that NAS (up to 5 on F5-221).
I didn't like TOS and wanted to run any Linux distro on the NAS which is why I asked
for the source code.

The led_drv_TMJ33 module (the one provided by Terramaster support) creates char devices
/dev/leddrv[1-10] that you can pipe into "led[1-10]on" and "led[1-10]off". However
they don't do any tracking on which char device the user is writing into, meaning
`echo led2on > /dev/leddrv5` will actually turn on led2, so will `echo led2on > /dev/leddrv3`.
I didn't like that interface so I wrote my own with the original one as reference on how
to interface with the hardware.

It finds the base address of the device that has the relevant GPIO pins that control
the LEDs. This is done in function read_base. That address is stored in `base` in the
following explanation.

The module ioremaps address with hardcoded offset from base as follows.

```
led1   =  (volatile unsigned int *)ioremap(base+0xC505B8, 1);    //GPIO23
led3   =  (volatile unsigned int *)ioremap(base+0xC505C0, 1);    //GPIO24
led5   =  (volatile unsigned int *)ioremap(base+0xC505C8, 1);    //GPIO25
led7   =  (volatile unsigned int *)ioremap(base+0xC505D0, 1);    //GPIO26
led9   =  (volatile unsigned int *)ioremap(base+0xC505D8, 1);    //GPIO27
led2   =  (volatile unsigned int *)ioremap(base+0xC505E0, 1);    //GPIO28
led4   =  (volatile unsigned int *)ioremap(base+0xC505E8, 1);    //GPIO29
led6   =  (volatile unsigned int *)ioremap(base+0xC505F0, 1);    //GPIO30
led8   =  (volatile unsigned int *)ioremap(base+0xC505F8, 1);    //GPIO31
led10  =  (volatile unsigned int *)ioremap(base+0xC50600, 1);    //GPIO32
```

Green color of physical HDD LED[1-5] is connected to the first 5 in the list above
(odd numbers in 1-9).
Red color of physical HDD LED[1-5] is connected to the last 5 in the list above (even
numbers in 2-10).

So the green color of the physical HDD LEDs are offset of base in 0x8 intervals (starting
at base+0xC505B8) and the red color are offset +0x28 from the green color of the same
physical LED.

This module creates char devices /dev/hddled[1-5]. Each device can only control a single
LED. You can write [0-3] into them to control the LED in question.

```
0 - OFF
1 - GREEN
2 - RED
3 - BOTH (orange)
```

`echo 1 > /dev/hddled1`
