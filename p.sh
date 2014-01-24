#!/usr/bin/expect -f
#set file [`*.h *.cpp]
spawn bash -c "scp *.cpp *.h build.sh conf/char.p qiaoyong@192.168.132.121:~/qiaoyong/sug/"
expect  "*password:"
send "adidas2\n"
interact
