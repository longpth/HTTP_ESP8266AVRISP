AVR ISP via HTTP for ESP8266
===============================================
Description:
--------

This is ported from TCP to HTTP of ESP8266AVRISP library.(https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266AVRISP)

Hardware connection:
--------

ESP8266 NodeMCU

ESP8266----->AVR / SPI   
GPIO12(D6)-->MISO        
GPIO13(D7)-->MOSI        
GPIO14(D5)-->SCK         
any\*------->RESET       

License and Authors
--------

This Library is actually ported from library ESP8266AVRISP of Kiril Zyapkov <kiril@robotev.com> from using TCP to HTTP.
It is combined with ESP8266WebServer library of Ivan Grokhtkov <ivan@esp8266.com> by being created a class which derived from ESP8266WebServer class.

ArduinoISP version 04m3
Copyright (c) 2008-2011 Randall Bohn
If you require a license, see
    http://www.opensource.org/licenses/bsd-license.php

Support for TCP on ESP8266
Copyright (c) Kiril Zyapkov <kiril@robotev.com>.

Support for HTTP on ESP8266
Copyright (c) Long Pham <it.farmer.vn@gmail.com>.