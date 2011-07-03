#!/bin/sh
HERE=`dirname "${0}"`

if [ -z "`which git 2> /dev/null`" ] ; then
	exec sh "${HERE}"/get-revision-none.sh
fi

git --no-pager log -n 24 --shortstat | awk '
  BEGIN { EXPECT_GITSVN=1; COMMITS=0 }
  EXPECT_GITSVN && /commit/ { COMMITS++ }
  EXPECT_GITSVN && /git-svn-id:/ {
      rev = "unknown"
      if (match($0, "@[0-9]*")) {
	rev = substr($0, RSTART, RLENGTH);
      }

      if (COMMITS > 1) {
	rev = rev "(+local)";
      }
      print rev;
      EXPECT_GITSVN=0;
  }
  !EXPECT_GITSVN { next }'
