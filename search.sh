#!/bin/bash
#sufu() {
  find -type f -name '*.h' -exec fgrep -Hn --color=auto "$1" {} +
#}
