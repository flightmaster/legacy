#!/usr/bin/sed -f
s/\(.*\), \(.*\), \([^ ]\{,10\}\)\(.*\)/\3 \1 \2 -3.0 0.0\n\3\4\nType: VRP\nXXX/
