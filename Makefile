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
CFLAGS += -I"$(DVSDK_ROOT)/codec-engine_2_26_02_11/packages"
CFLAGS += -I"$(DVSDK_ROOT)/framework-components_2_26_00_01/packages"
CFLAGS += -I"$(DVSDK_ROOT)/xdais_6_26_01_03/packages"
CFLAGS += -I"$(DVSDK_ROOT)/linuxutils_2_26_03_06/packages"
CFLAGS += -I"$(DVSDK_ROOT)/codecs-dm365/packages"
CFLAGS += -I"$(DVSDK_ROOT)/codec-engine_2_26_02_11/examples"
CFLAGS += -I"$(DVSDK_ROOT)/xdctools_3_16_03_36/packages"
CFLAGS += -Dxdc_target_types__="gnu/targets/arm/std.h"
CFLAGS += -Dxdc_target_name__=GCArmv5T


LDFLAGS = --sysroot=$(SYSROOT)
CMEMLIB = lib/cmem.a470MV


CC_DEPFLAGS = -MMD -MF $(@:.o=.d) -MT $@

OBJS = ff_example.o

all:	$(APP_NAME) 

	
$(APP_NAME): $(OBJS) $(FFDIR)/libswscale.a $(FFDIR)/libavformat.a $(FFDIR)/libavcodec.a $(FFDIR)/libavutil.a xdclink.cmd
	$(CC) $(LDFLAGS) -o $@ $^ -lpthread -lm -lz
	

xdclib: xdclink.cmd

xdclink.cmd: xdc.cfg
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
