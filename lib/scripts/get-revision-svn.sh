#!/bin/sh

HERE=`dirname "${0}"`

if [ -z "`which svn 2> /dev/null`" ] ; then
	exec sh "${HERE}"/get-revision-none.sh
fi

svn log --non-interactive --limit 1 -q -v | awk '
/^r[0-9]*/ {
    if (match($0, "^r[0-9]*")) {
      rev = "@" substr($0, RSTART + 1, RLENGTH)
    }
    print rev
}'
