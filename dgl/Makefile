#!/usr/bin/make -f
# Makefile for dgl #
# ---------------- #
# Created by falkTX
#

include Makefile.mk

# --------------------------------------------------------------

BUILD_C_FLAGS   += $(DGL_FLAGS) -I. -Isrc -DDGL_NAMESPACE=$(DGL_NAMESPACE)
BUILD_CXX_FLAGS += $(DGL_FLAGS) -I. -Isrc -DDGL_NAMESPACE=$(DGL_NAMESPACE)

ifeq ($(FONS_USE_FREETYPE),true)
BUILD_C_FLAGS   += -DFONS_USE_FREETYPE=$(FONS_USE_FREETYPE)
BUILD_CXX_FLAGS += -DFONS_USE_FREETYPE=$(FONS_USE_FREETYPE)
endif

LINK_FLAGS      += $(DGL_LIBS)

ifneq ($(MACOS_OLD),true)
# needed by sofd right now, fix later
BUILD_CXX_FLAGS += -Wno-type-limits -fpermissive
endif

# --------------------------------------------------------------

OBJS = \
	src/Application.cpp.o \
	src/Color.cpp.o \
	src/Geometry.cpp.o \
	src/Image.cpp.o \
	src/ImageWidgets.cpp.o \
	src/NanoVG.cpp.o \
	src/Resources.cpp.o \
	src/Widget.cpp.o

ifeq ($(MACOS),true)
OBJS += src/Window.mm.o
else
OBJS += src/Window.cpp.o
endif

TARGET = ../libdgl.a

# --------------------------------------------------------------

all: $(TARGET)

# --------------------------------------------------------------

../libdgl.a: $(OBJS)
	rm -f $@
	$(AR) crs $@ $^

../libdgl.dll: $(OBJS)
	# -Wl,--output-def,$@.def,--out-implib,$@.a
	$(CXX) $^ -shared $(LINK_FLAGS) -o $@

../libdgl.dylib: $(OBJS)
	$(CXX) $^ -dynamiclib $(LINK_FLAGS) -o $@

../libdgl.so: $(OBJS)
	$(CXX) $^ -shared $(LINK_FLAGS) -o $@

# --------------------------------------------------------------

%.c.o: %.c
	$(CC) $< $(BUILD_C_FLAGS) -c -o $@

%.cpp.o: %.cpp
	$(CXX) $< $(BUILD_CXX_FLAGS) -c -o $@

src/Window.cpp.o: src/Window.cpp src/pugl/*
	$(CXX) $< $(BUILD_CXX_FLAGS) -c -o $@

src/Window.mm.o: src/Window.cpp src/pugl/*
	$(CXX) $< $(BUILD_CXX_FLAGS) -ObjC++ -c -o $@

# --------------------------------------------------------------

clean:
	rm -f src/*.d src/*.o ../libdgl.*

debug:
	$(MAKE) DEBUG=true

# --------------------------------------------------------------

-include $(OBJS:%.o=%.d)

# --------------------------------------------------------------
