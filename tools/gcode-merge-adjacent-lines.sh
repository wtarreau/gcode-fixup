#!/bin/sh
# merge G0 or G1 commands which only move X with no strength change
awk 'BEGIN{line="";}/^G1 X[-0-9. ]*$/{line=$0;next;}{ if (line != "") print line; line=""; print $0;} END{ if (line != "") print line;}' "$@" | \
  awk 'BEGIN{line="";}/^G0 X[-0-9. ]*$/{line=$0;next;}{ if (line != "") print line; line=""; print $0;} END{ if (line != "") print line;}'
