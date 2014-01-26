#g++ -DCN2PY_DEBUG -g py.cpp  md5_64bit.cpp  -I ./  -I ./sparsehash/include/ -I ./sparsehash/include/google -I ./sparsehash/include/sparsehash -lcrypto -o py
rm -rf sug core
g++ -Wall  -g py.cpp pool.cpp contents.cpp sug.cpp md5_64bit.cpp  -I ./  -I ./sparsehash/include/ -I ./sparsehash/include/google -I ./sparsehash/include/sparsehash -lpthread -lcrypto -o sug
