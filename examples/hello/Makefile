include ysr.mk

## example 1: how to build a shared library
printerlib_impl_OBJS=$(DEST)/hello/src/printerlib.o
printerlib_impl_LIBNAME=printer
$(call ysr-add-c-lib-shared,printerlib_impl)

## example 2: a module which when used, let your program depend on a shared library
printerlib=1
printerlib_SHLIBS+=$(printerlib_impl_LIB)
printerlib_LIBS=$(printerlib_impl_LIBNAME)
printerlib_LIBSPATH=$(printerlib_impl_LIBDIR)
printerlib_DEPS=$(printerlib_impl_LIB)

## example 2.1: of course you can have dependencies on third-party libraries
mathlib=1
mathlib_LIBS+=m

## example 3: a program with dependency on a shared library
helloworld_OBJS=$(DEST)/hello/src/printer.o
helloworld_REQUIRES=printerlib mathlib
### example 3.1: how to customize flags
helloworld_CFLAGS=-ffast-math
### example 3.2: how to define preprocessor constants
helloworld_DEFINES=VALUE="\"Hello_from_CPP\""

$(call ysr-add-c-console-prog,helloworld)

all: help
