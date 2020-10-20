# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(syn-read) begin
(syn-read) create "data"
(syn-read) open "data"
(syn-read) write "data"
(syn-read) close "data"
(syn-read) exec child 1 of 10: "child-syn-read 0"
(syn-read) exec child 2 of 10: "child-syn-read 1"
(syn-read) exec child 3 of 10: "child-syn-read 2"
(syn-read) exec child 4 of 10: "child-syn-read 3"
(syn-read) exec child 5 of 10: "child-syn-read 4"
(syn-read) exec child 6 of 10: "child-syn-read 5"
(syn-read) exec child 7 of 10: "child-syn-read 6"
(syn-read) exec child 8 of 10: "child-syn-read 7"
(syn-read) exec child 9 of 10: "child-syn-read 8"
(syn-read) exec child 10 of 10: "child-syn-read 9"
(syn-read) end
EOF
pass;
