#-----------------------------------------------------------------------------
# archdefs.mk               						
#
# Description: 
#       Architecture specific definitions for compiling and linking
#		programs using GigE-V Framework API and GenICam libraries.
#		For ARMhf 
#
#-----------------------------------------------------------------------------#
# Architecture-dependent definitions.
# (GenICam libraries have different paths and names depending on the architecture)
#

#
# Architecture specific environment defs for self-hosted environment 
#
ifndef ARCH
  ARCH := $(shell uname -m | sed -e s/aarch64/aarch64/)
endif

ifeq  ($(ARCH), aarch64)
	# Very Old
	ARCHNAME=armv8-a
	ARCH_GENICAM_BIN=Linux64_ARM
	ARCH_OPTIONS= -D__arm__ -D_REENTRANT
	ARCH_GCCVER=54
else
# Not supported
$(error Architecture $(ARCH) not configured for this installation.)
endif

ARCHLIBDIR=/usr/lib/aarch64-linux-gnu
ARCH_LINK_OPTIONS=

#
# Arch dependent GenICam library specification
#
GENICAM_PATH_VERSION=v3_0
GENICAM_PATH:=$(GENICAM_ROOT_V3_0)
INC_GENICAM=-I$(GENICAM_PATH)/library/CPP/include
GENICAM_LIBS=-L$(GENICAM_PATH)/bin/$(ARCH_GENICAM_BIN)\
					-lGenApi_gcc$(ARCH_GCCVER)_$(GENICAM_PATH_VERSION)\
					-lGCBase_gcc$(ARCH_GCCVER)_$(GENICAM_PATH_VERSION)			

