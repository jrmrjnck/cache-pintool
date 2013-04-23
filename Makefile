obj_dir = obj-intel64
target = SafeAccess.so
src = SafeAccess.cpp Cache.cpp Directory.cpp Util.cpp

objects = $(patsubst %.cpp,$(obj_dir)/%.o,$(src))

CXX = g++
CXXFLAGS = -DBIGARRAY_MULTIPLIER=-1 -DUSING_XED -Wall -Wno-unknown-pragmas -fno-stack-protector\
            -DTARGET_IA32E -DHOST_IA32E -fPIC -DTARGET_LINUX -I/opt/pin/source/include/pin\
				-I/opt/pin/source/include/pin/gen -I/opt/pin/extras/components/include\
				-I/opt/pin/extras/xed2-intel64/include -I/opt/pin/source/tools/InstLib -O0\
				-fomit-frame-pointer -fno-strict-aliasing\
				-std=c++11 -g
LDFLAGS = -shared -Wl,--hash-style=sysv -Wl,-Bsymbolic -Wl,--version-script=/opt/pin/source/include/pin/pintool.ver -g
LIB_DIRS = -L/opt/pin/intel64/lib -L/opt/pin/intel64/lib-ext -L/opt/pin/intel64/runtime/glibc\
			  -L/opt/pin/extras/xed2-intel64/lib
LIBS = -lpin -lxed -ldwarf -lelf -ldl

$(obj_dir)/$(target):$(objects)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIB_DIRS) $(LIBS)

$(obj_dir)/%.o : %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $^

clean:
	rm -f ./$(obj_dir)/*
