#!/bin/sh

## returns a list of tokens representing the system we are building on
##
## grammar: { denotes alternatives } [ optional values ]
##
## { WIN32, MACOSX, LINUX } [ CYGWIN ]

UNAME_MACHINE=`(uname -m) 2>/dev/null` || UNAME_MACHINE=unknown
UNAME_RELEASE=`(uname -r) 2>/dev/null` || UNAME_RELEASE=unknown
UNAME_SYSTEM=`(uname -s) 2>/dev/null`  || UNAME_SYSTEM=unknown
UNAME_VERSION=`(uname -v) 2>/dev/null` || UNAME_VERSION=unknown

case "${UNAME_MACHINE}:${UNAME_SYSTEM}:${UNAME_RELEASE}:${UNAME_VERSION}" in
    i*:MINGW*:*)
        echo 'WIN32'
        exit 0 ;;
    i*:CYGWIN*:*)
	echo 'WIN32 CYGWIN'
	exit 0 ;;
    *:Darwin:*:*)
        echo 'MACOSX'
        exit 0 ;;
    *:*:*:*)
	echo 'LINUX'
	exit 0 ;;
esac


    
