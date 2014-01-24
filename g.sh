#!/usr/bin/expect -f
#set file [`*.h *.cpp]
spawn scp qiaoyong@192.168.132.121:~/qiaoyong/sug/*.cpp qiaoyong@192.168.132.121:~/qiaoyong/sug/*.h  .
#send "ssh work@$host\n"
expect  "*password:"
send "adidas2\n"
#spawn scp test_cn2py.cpp qiaoyong@172.23.0.141:/home/qiaoyong/code/hashmap/
#expect  "*password:"
#send "Adidas|1234\n"
expect  "*password:"
send "adidas2\n"
interact
