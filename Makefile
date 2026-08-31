# dmltagent Makefile

TSF4G_INC := /data/home/holyjing/gcloudservice/tsf4g_proj/include
TSF4G_LIB := /data/home/holyjing/gcloudservice/tsf4g_proj/lib
TCM_ROOT  := /data/home/holyjing/gcloudservice/GOpen_tcm
TCM_LIB   := $(TCM_ROOT)/lib
TDR_TOOL  := /data/home/holyjing/gcloudservice/tsf4g_proj/tools/tdr

CXX := g++
CC  := gcc

INCLUDES := -I$(TSF4G_INC) -I$(TCM_ROOT)/include/apps \
            -I$(TCM_ROOT)/src/util_lib -I$(TCM_ROOT)/src/protocol \
            -Iproto -Isrc

CXXFLAGS := -g -O2 -Wall -Wno-unused-variable -Wno-deprecated-declarations $(INCLUDES)
CFLAGS   := -g -O2 $(INCLUDES)

LIBS := -Wl,--start-group \
        -ltcmutil -ltaa -ltapp -ltbus -ltdr -ltlog -ltloghelp -lcomm -lpal -ltsf4g \
        -Wl,--end-group \
        -lscew -lexpat -lreadline -lncurses -lanl -lpthread -ldl -lrt -lm

LIBPATHS := -L$(TCM_LIB) -L$(TSF4G_LIB)

SRCS := src/dml_main.cpp src/dml_conf.cpp src/dml_plugin.cpp src/dml_centerd.cpp src/dml_ftp.cpp
OBJS := $(SRCS:.cpp=.o) proto/tagent_centerd_proto.o

BIN := release/bin/dmltagent

all: $(BIN)

proto/tagent_centerd_proto.c proto/tagent_centerd_proto.h: tagent_centerd_proto.xml
	mkdir -p proto
	$(TDR_TOOL) -C -o proto/tagent_centerd_proto.c tagent_centerd_proto.xml
	$(TDR_TOOL) -H -o tagent_centerd_proto.h tagent_centerd_proto.xml
	mv -f tagent_centerd_proto.h proto/

proto/tagent_centerd_proto.o: proto/tagent_centerd_proto.c proto/tagent_centerd_proto.h
	$(CC) $(CFLAGS) -c proto/tagent_centerd_proto.c -o $@

%.o: %.cpp src/dml_common.h proto/tagent_centerd_proto.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN): $(OBJS)
	mkdir -p release/bin
	$(CXX) -o $@ $(OBJS) $(LIBPATHS) $(LIBS)

clean:
	rm -f $(OBJS) $(BIN)

.PHONY: all clean
