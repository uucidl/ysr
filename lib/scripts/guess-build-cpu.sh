#!/bin/sh

UNAME_MACHINE=`(uname -m) 2>/dev/null` || UNAME_MACHINE=unknown
UNAME_CPU=`(uname -p) 2>/dev/null` || UNAME_CPU=unknown

case "${UNAME_MACHINE}:${UNAME_CPU}" in
    i586:*)
	echo 'PENTIUM'
	exit 0 ;;
    i686:*)
	echo 'PENTIUMPRO'
	exit 0 ;;
    i*86:*)
	echo 'i386'
	exit 0 ;;
    x86_64:*AMD*)
	echo 'K8'
	exit 0 ;;
    Power\ PC*:*)
	echo 'POWERPC'
	exit 0 ;;
    *:powerpc)
	echo 'POWERPC'
	exit 0 ;;
    *:ppc)
	echo 'POWERPC'
	exit 0 ;;
esac
