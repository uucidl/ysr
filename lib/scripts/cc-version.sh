#!/bin/sh

if [ $# -lt 1 ]; then
    echo "Usage: $0 <gcc-binary>"
    exit 1
fi

O=$(which "${1}")

if [ -z "${O}" ]; then
    echo "Couldn't find ${1}"
    exit 1
fi

# set localisation for gcc to english
LANG=en

"${1}" -v 2>&1 | awk '/gcc version/ {
	 gsub(/^gcc version */, "");
	 version = $0;
	 minor = version;
	 gsub(/^[0-9]+\./, "", minor);
	 major = substr(version, 0, length(version) - length(minor));
	 gsub(/[^0-9].*$/, "", minor);
	 print major " " minor;
}' | sed -e 's/\.//g'