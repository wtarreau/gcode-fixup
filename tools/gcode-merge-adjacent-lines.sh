#!/bin/sh
# merge G1 commands which only move X with no strength change
exec awk 'BEGIN{line="";}/^G1 X[-0-9. ]*$/{line=$0;next;}{ if (line != "") print line; line=""; print $0;} END{ print line;}' "$@"
