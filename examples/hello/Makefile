include ysr.mk

## example 1: how to build a shared library
printerlib_impl_OBJS=$(DEST)/src/printerlib.o
printerlib_impl_LIBNAME=printer
$(call mk-c-sharedlib-rule,printerlib_impl)

## example 2: how to depend on a shared library
printerlib=1
printerlib_SHLIBS+=$(printerlib_impl_LIB)
printerlib_LIBS=$(printerlib_impl_LIBNAME)
printerlib_LIBSPATH=$(printerlib_impl_LIBDIR)
printerlib_DEPS=$(printerlib_impl_LIB)

## example 3: a program with dependency on a shared library
helloworld_OBJS=$(DEST)/src/printer.o
helloworld_REQUIRES=printerlib
$(call mk-c-prog-rule,helloworld)

all: help
