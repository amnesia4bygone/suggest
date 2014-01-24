#!/usr/bin/expect -f
#set file [`*.h *.cpp]
spawn bash -c "ssh qiaoyong@192.168.132.121"
expect  "*password:"
send "adidas2\n"
interact
