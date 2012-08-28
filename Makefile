#set following as environment variables
#do not use backslash in windows, path should be eg. c:/dm365/dvsdk
CROSS_COMPILE ?= arm-none-linux-gnueabi-
SDK_DIR ?= /home/honza/_dev/TI/eye03_sdk
LINUX_HEADERS_INC ?= $(SDK_DIR)/linux-headers-davinci/include
SYSROOT ?= $(SDK_DIR)/sysroot
DVSDK_ROOT ?= $(SDK_DIR)/dvsdk
FFDIR ?= /home/honza/_dev/ffbuild/i/lib

APP_NAME = ff_example

CC = $(CROSS_COMPILE)gcc
STRIP = $(CROSS_COMPILE)strip

CFLAGS = -I$(LINUX_HEADERS_INC)
CFLAGS += -g -O0 -Wall
CFLAGS += -I/home/honza/_dev/ffbuild/i/include
CFLAGS += -march=armv5te -mtune=arm926ej-s -mcpu=arm926ej-s
CFLAGS += --sysroot=$(SYSROOT)

LDFLAGS = --sysroot=$(SYSROOT)
CMEMLIB = lib/cmem.a470MV


CC_DEPFLAGS = -MMD -MF $(@:.o=.d) -MT $@

OBJS = ff_example.o common.o

all:	$(APP_NAME) 

	
$(APP_NAME): $(OBJS) $(FFDIR)/libavformat.a $(FFDIR)/libavcodec.a $(FFDIR)/libavutil.a xdclink.cmd
	$(CC) $(LDFLAGS) -o $@ $^ -lpthread -lm -lz
	

xdclib: xdclink.cmd

xdclink.cmd:
	$(MAKE) -f Makefile.xdc xdclib ROOTDIR=$(DVSDK_ROOT)

xdclib_clean:
	rm -f xdclink.cmd lib/*	
	$(MAKE) -f Makefile.xdc clean ROOTDIR=$(DVSDK_ROOT)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CC_DEPFLAGS) -c -o $@ $<

clean:
	rm -f *.o
	rm -f *.d
	rm -f $(APP_NAME)
	$(MAKE) -f Makefile.xdc clean ROOTDIR=$(DVSDK_ROOT)

-include $(wildcard $(OBJS:.o=.d) $(TESTOBJS:.o=.d))
