TOPDIR=../../..

#lib or target you want to build: eg. LIB=libmisc.a  or TARGET=wlan
LIB		=
TARGET	= boot

# modules depend by this module
MODULES =

#libraries depend: eg. LIBS = -lbsp -ldrv -lrtos
LIBS	= 
#ifeq ($(IC_BOARD), 0)
#LIBS	+= -ldrv_fpga
#else
#LIBS	+= -ldrv
#endif

#exclude subdirs
exclude_dirs=

#files want to build: eg. CSRCS=a.c  CPPSRCS=b.cpp  SSRCS=c.S
CSRCS	= $(wildcard *.c) $(wildcard bsp/*.c) $(wildcard bsp/gcc/stubs/*.c) $(wildcard ota/*.c) \
         $(wildcard contiki/*.c) \
         $(wildcard driver/*/*.c)

CPPSRCS	=
SSRCS	= bsp/gcc/intexc_demosoc.S bsp/gcc/startup_demosoc.S

#includings and flags
CFLAGS = -I $(TOPDIR)/include/NMSIS/Core/Include \
         -I $(TOPDIR)/include \
         -I ./contiki/include \
         -I ./ota/include	\
         -I ./driver \
         -I ./include \

# copy cflags from BTIP
CFLAGS  += -DCFG_STATIC  -DCFG_ROM -DNDEBUG

BUILD_ROM=1

#OPTMIZED_SIZE
BUILD_NANO=1

# ext:
# 	mkdir -p $(TOPDIR)/chip/$(CHIP)/rom
# 	cp $(TGTOUT)/$(TARGET).bin $(TOPDIR)/chip/$(CHIP)/rom/
# 	cp $(TGTOUT)/$(TARGET).dis $(TOPDIR)/chip/$(CHIP)/rom/
# 	cp $(TGTOUT)/$(TARGET).readelf $(TOPDIR)/chip/$(CHIP)/rom/
# 	cp $(TGTOUT)/$(TARGET).symbol $(TOPDIR)/chip/$(CHIP)/rom/
# 	cp $(TGTOUT)/$(TARGET) $(TOPDIR)/chip/$(CHIP)/rom/
# 	python $(TOPDIR)/modules/patch/py/sym_to_ld.py $(TGTOUT)/$(TARGET).symbol  $(TOPDIR)/chip/$(CHIP)/rom/boot_sym.ld
	
include $(TOPDIR)/rules.mk
OPTIM     = -Os
LDSCRIPT  = ./rom.ld




